#include "parser.hpp"
#include "inner_signals.hpp"
#include "types.hpp"
#include <iostream>
using std::string_view, std::endl, std::cout;

namespace ml {

Parser::Parser() {}

shared_ptr<Object> Parser::parse(std::string input) {
  _input = input;
  tokenize();

  // cout << "*******************\n";
  // for (auto t : tokens)
  //   cout << t << " ";
  // cout << endl;
  // cout << "*******************\n" << endl;

  token_parse_index = 0;
  if (not tokens.empty()) {
    return parse_form();
  } else
    return nil();
}

void Parser::tokenize() {
  string_view view(_input);
  unsigned int index = 0, offset = 0;

  enum TOKEN_TYPE {
    UNKNOWN,
    STRING,
    SPACES,
  };

  TOKEN_TYPE current_token = UNKNOWN;
  auto add_token = [this](string_view token) { this->tokens.push_back(token); };

  while (index + offset < view.length()) {
    char ch = view.at(index + offset);
    switch (current_token) {
    case STRING:
      if (ch == '"' and index > 0 and view.at(index + offset - 1) != '\\') {
        current_token = UNKNOWN;
        add_token(view.substr(index, offset + 1));
        index += offset + 1;
        offset = 0;
      } else
        offset++;
      break;
    case SPACES:
      switch (ch) {
      case ' ':
      case '\n':
      case '\t':
        index++;
        break;
      case '"':
        index += offset;
        offset = 1;
        current_token = STRING;
        break;
      default:
        offset = 0;
        current_token = UNKNOWN;
      }
      break;
    case UNKNOWN:
      switch (ch) {
      case ' ':
        if (offset != 0) {
          add_token(view.substr(index, offset));
          index += offset;
          offset = 0;
        }
        current_token = SPACES;
        break;
      case '"':
        current_token = STRING;
        index += offset;
        offset++;
        break;
      case '~':
        if (view.at(index + offset + 1) == '@') {
          if (offset != 0)
            add_token(view.substr(index, offset));
          add_token(view.substr(index + offset, 2));
          index += offset + 2;
          offset = 0;
          break;
        }
      case '(':
      case ')':
      case '[':
      case ']':
      case '{':
      case '}':
      case '@':
      case '&':
      case '\'':
      case '`':
      case ';':
        if (offset != 0)
          add_token(view.substr(index, offset));
        add_token(view.substr(index + offset, 1));
        index += offset + 1;
        offset = 0;
        break;
      case '\n':
      case '\t':
        if (offset > 0)
          add_token(view.substr(index, offset));
        index += offset + 1;
        offset = 0;
        break;
      default:
        offset++;
      }
    }
#ifdef DEBUG_Parser_tokenize_steps
    for (auto tok : tokens)
      cout << tok << " ";
    cout << endl << view << endl;
    for (unsigned int i = 0; i < view.length(); i++)
      cout << (i == index ? '*' : ' ');
    cout << endl;
    for (unsigned int i = 0; i < view.length(); i++)
      cout << (i == index + offset ? '*' : ' ');
    cout << endl;
    cout << " index: " << index << endl << " offset: " << offset << endl;
    std::cin.get();
#endif
  }
  if (view.length() > 0 and index < view.length()) {
    add_token(view.substr(index, offset));
  }
}

std::string_view Parser::peak() { return tokens[token_parse_index]; }

std::string_view Parser::next() { return tokens[++token_parse_index]; }

void Parser::reset() { token_parse_index = -1; }

#ifdef DEBUG_Parser_debug
void Parser::debug() {
  std::string token_pri = "";
  for (auto token : tokens)
    token_pri += std::string(token) + " ";
  cout << token_pri << endl;
}
#endif

shared_ptr<Object> Parser::parse_form() {
#ifdef DEBUG_Parser_parse_form_steps
  cout << tokens[token_parse_index] << endl << token_parse_index << endl;
  std::cin.get();
#endif
  if (token_parse_index < tokens.size()) {
#ifdef DEBUG_Parser_parse_form_steps
    cout << "parsing token " << tokens[token_parse_index] << endl;
#endif
    if (tokens[token_parse_index] == "(") {
      brackets.push(CURVE);
      token_parse_index++;
      return parse_list();
    } else if (tokens[token_parse_index] == ")") {
      token_parse_index++;
      return to_obj(signal(CURVE_BRACKET_CLOSE));
    } else if (tokens[token_parse_index] == "[") {
      brackets.push(SQUARE);
      token_parse_index++;
      return parse_vec();
    } else if (tokens[token_parse_index] == "]") {
      token_parse_index++;
      return to_obj(signal(SQUARE_BRACKET_CLOSE));
    } else if (tokens[token_parse_index] == "{") {
      brackets.push(GRAPH);
      token_parse_index++;
      return parse_dict();
    } else if (tokens[token_parse_index] == "}") {
      token_parse_index++;
      return to_obj(signal(GRAPH_BRACKET_CLOSE));
    } else if (tokens[token_parse_index] == "'") {
      shared_ptr<List> quoted_list = list();
      quoted_list->append(symbol("quote"));
      token_parse_index++;
      quoted_list->append(parse_form());
      // token_parse_index++;
      return quoted_list;
    } else if (tokens[token_parse_index] == "`") {
      shared_ptr<List> quasiquoted_list = list();
      quasiquoted_list->append(symbol("quasiquote"));
      token_parse_index++;
      quasiquoted_list->append(parse_form());
      // token_parse_index++;
      return quasiquoted_list;
    } else if (tokens[token_parse_index] == "~") {
      shared_ptr<List> unquoted_list = list();
      unquoted_list->append(symbol("unquote"));
      token_parse_index++;
      unquoted_list->append(parse_form());
      return unquoted_list;
    } else if (tokens[token_parse_index] == "~@") {
      shared_ptr<List> splice_unquoted_list = list();
      splice_unquoted_list->append(symbol("splice-unquote"));
      token_parse_index++;
      splice_unquoted_list->append(parse_form());
      return splice_unquoted_list;
    } else {
      return parse_atom();
    }
  } else {
    cout << "Parser: going over tokens end" << endl;
    return nil();
  }
}

shared_ptr<List> Parser::parse_list() {
  shared_ptr<List> ret = list();
  while (token_parse_index < tokens.size()) {
    shared_ptr<Object> el = parse_form();
    if (el->type == SIGNAL) {
      switch (to_signal(el)->_value) {
      case CURVE_BRACKET_CLOSE:
        if (CURVE != brackets.top()) {
          cout << "ERROR balancing ()" << endl;
          exit(1);
        } else {
          brackets.pop();
          return ret;
        }
        break;
      case SQUARE_BRACKET_CLOSE:
      case GRAPH_BRACKET_CLOSE:
      case END_OF_TOKENS:
      case QUIT:
        cout << "ERROR balancing ()" << endl;
        exit(1);
      }
    } else {
      ret->append(el);
    }
  }
  return to_list(nil());
}

shared_ptr<Vec> Parser::parse_vec() {
  shared_ptr<Vec> ret = vec();
  while (token_parse_index < tokens.size()) {
    shared_ptr<Object> el = parse_form();
    if (el->type == SIGNAL) {
      switch (to_signal(el)->_value) {
      case SQUARE_BRACKET_CLOSE:
        if (SQUARE != brackets.top()) {
          cout << "ERROR balancing []" << endl;
          exit(1);
        } else {
          brackets.pop();
          return ret;
        }
        break;
      case CURVE_BRACKET_CLOSE:
      case GRAPH_BRACKET_CLOSE:
      case END_OF_TOKENS:
      case QUIT:
        cout << "ERROR balancing []" << endl;
        exit(1);
      }
    } else {
      ret->append(el);
    }
  }
  return to_vec(nil());
}

shared_ptr<Dict> Parser::parse_dict() {
  shared_ptr<Dict> ret = dict();
  while (token_parse_index < tokens.size()) {
    shared_ptr<Object> key = parse_form();
    switch (key->type) {
    case KEYWORD:
    case STRING: {
      shared_ptr<Object> value = parse_form();
      if (value->type == SIGNAL) {
        switch (to_signal(value)->_value) {
        case CURVE_BRACKET_CLOSE:
        case SQUARE_BRACKET_CLOSE:
        case GRAPH_BRACKET_CLOSE:
        case END_OF_TOKENS:
        case QUIT:
          cout << "error balancing {}" << endl;
          return to_dict(nil());
        }
      } else {
        ret->append(key, value);
      }
    } break;
    case SIGNAL: {
      switch (to_signal(key)->_value) {
      case GRAPH_BRACKET_CLOSE:
        return ret;
      default:
        return to_dict(nil());
      }
    } break;
    default:
      cout << "only keyword and string are valid keys" << endl;
      return to_dict(nil());
    }
  }
  cout << "uncorrect dict parsing" << endl;
  return to_dict(nil());
}

shared_ptr<Object> Parser::parse_atom() {
  string_view token = tokens[token_parse_index];
  token_parse_index++;

  char ch = token.at(0);
  if (ch == '"') {
    return str(std::string(token.substr(1, token.length() - 2)));
  } else if (isdigit(ch)) {
    bool is_number = true;
    for (char c : token) {
      if (not(isdigit(c) or c == '.')) {
        is_number = false;
        break;
      }
    }
    if (is_number)
      return number(std::stod(std::string(token)));
    else
      return symbol(std::string(token));
  } else if (ch == ':') {
    return keyword(std::string(token));
  } else
    return symbol(std::string(token));
}

} // namespace ml
