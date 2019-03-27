//===---------------------StructuredData.cpp ---------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/JSON.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cerrno>
#include <cstdlib>
#include <inttypes.h>
#include <limits>

using namespace lldb_private;

//----------------------------------------------------------------------
// Functions that use a JSONParser to parse JSON into StructuredData
//----------------------------------------------------------------------
static StructuredData::ObjectSP ParseJSONValue(JSONParser &json_parser);
static StructuredData::ObjectSP ParseJSONObject(JSONParser &json_parser);
static StructuredData::ObjectSP ParseJSONArray(JSONParser &json_parser);

StructuredData::ObjectSP
StructuredData::ParseJSONFromFile(const FileSpec &input_spec, Status &error) {
  StructuredData::ObjectSP return_sp;

  auto buffer_or_error = llvm::MemoryBuffer::getFile(input_spec.GetPath());
  if (!buffer_or_error) {
    error.SetErrorStringWithFormatv("could not open input file: {0} - {1}.",
                                    input_spec.GetPath(),
                                    buffer_or_error.getError().message());
    return return_sp;
  }

  JSONParser json_parser(buffer_or_error.get()->getBuffer());
  return_sp = ParseJSONValue(json_parser);
  return return_sp;
}

static StructuredData::ObjectSP ParseJSONObject(JSONParser &json_parser) {
  // The "JSONParser::Token::ObjectStart" token should have already been
  // consumed by the time this function is called
  auto dict_up = llvm::make_unique<StructuredData::Dictionary>();

  std::string value;
  std::string key;
  while (1) {
    JSONParser::Token token = json_parser.GetToken(value);

    if (token == JSONParser::Token::String) {
      key.swap(value);
      token = json_parser.GetToken(value);
      if (token == JSONParser::Token::Colon) {
        StructuredData::ObjectSP value_sp = ParseJSONValue(json_parser);
        if (value_sp)
          dict_up->AddItem(key, value_sp);
        else
          break;
      }
    } else if (token == JSONParser::Token::ObjectEnd) {
      return StructuredData::ObjectSP(dict_up.release());
    } else if (token == JSONParser::Token::Comma) {
      continue;
    } else {
      break;
    }
  }
  return StructuredData::ObjectSP();
}

static StructuredData::ObjectSP ParseJSONArray(JSONParser &json_parser) {
  // The "JSONParser::Token::ObjectStart" token should have already been
  // consumed by the time this function is called
  auto array_up = llvm::make_unique<StructuredData::Array>();

  std::string value;
  std::string key;
  while (1) {
    StructuredData::ObjectSP value_sp = ParseJSONValue(json_parser);
    if (value_sp)
      array_up->AddItem(value_sp);
    else
      break;

    JSONParser::Token token = json_parser.GetToken(value);
    if (token == JSONParser::Token::Comma) {
      continue;
    } else if (token == JSONParser::Token::ArrayEnd) {
      return StructuredData::ObjectSP(array_up.release());
    } else {
      break;
    }
  }
  return StructuredData::ObjectSP();
}

static StructuredData::ObjectSP ParseJSONValue(JSONParser &json_parser) {
  std::string value;
  const JSONParser::Token token = json_parser.GetToken(value);
  switch (token) {
  case JSONParser::Token::ObjectStart:
    return ParseJSONObject(json_parser);

  case JSONParser::Token::ArrayStart:
    return ParseJSONArray(json_parser);

  case JSONParser::Token::Integer: {
    uint64_t uval;
    if (llvm::to_integer(value, uval, 0))
      return std::make_shared<StructuredData::Integer>(uval);
  } break;

  case JSONParser::Token::Float: {
    double val;
    if (llvm::to_float(value, val))
      return std::make_shared<StructuredData::Float>(val);
  } break;

  case JSONParser::Token::String:
    return std::make_shared<StructuredData::String>(value);

  case JSONParser::Token::True:
  case JSONParser::Token::False:
    return std::make_shared<StructuredData::Boolean>(token ==
                                                     JSONParser::Token::True);

  case JSONParser::Token::Null:
    return std::make_shared<StructuredData::Null>();

  default:
    break;
  }
  return StructuredData::ObjectSP();
}

StructuredData::ObjectSP StructuredData::ParseJSON(std::string json_text) {
  JSONParser json_parser(json_text);
  StructuredData::ObjectSP object_sp = ParseJSONValue(json_parser);
  return object_sp;
}

StructuredData::ObjectSP
StructuredData::Object::GetObjectForDotSeparatedPath(llvm::StringRef path) {
  if (this->GetType() == lldb::eStructuredDataTypeDictionary) {
    std::pair<llvm::StringRef, llvm::StringRef> match = path.split('.');
    std::string key = match.first.str();
    ObjectSP value = this->GetAsDictionary()->GetValueForKey(key);
    if (value.get()) {
      // Do we have additional words to descend?  If not, return the value
      // we're at right now.
      if (match.second.empty()) {
        return value;
      } else {
        return value->GetObjectForDotSeparatedPath(match.second);
      }
    }
    return ObjectSP();
  }

  if (this->GetType() == lldb::eStructuredDataTypeArray) {
    std::pair<llvm::StringRef, llvm::StringRef> match = path.split('[');
    if (match.second.empty()) {
      return this->shared_from_this();
    }
    errno = 0;
    uint64_t val = strtoul(match.second.str().c_str(), nullptr, 10);
    if (errno == 0) {
      return this->GetAsArray()->GetItemAtIndex(val);
    }
    return ObjectSP();
  }

  return this->shared_from_this();
}

void StructuredData::Object::DumpToStdout(bool pretty_print) const {
  StreamString stream;
  Dump(stream, pretty_print);
  llvm::outs() << stream.GetString();
}

void StructuredData::Array::Dump(Stream &s, bool pretty_print) const {
  bool first = true;
  s << "[";
  if (pretty_print) {
    s << "\n";
    s.IndentMore();
  }
  for (const auto &item_sp : m_items) {
    if (first) {
      first = false;
    } else {
      s << ",";
      if (pretty_print)
        s << "\n";
    }

    if (pretty_print)
      s.Indent();
    item_sp->Dump(s, pretty_print);
  }
  if (pretty_print) {
    s.IndentLess();
    s.EOL();
    s.Indent();
  }
  s << "]";
}

void StructuredData::Integer::Dump(Stream &s, bool pretty_print) const {
  s.Printf("%" PRIu64, m_value);
}

void StructuredData::Float::Dump(Stream &s, bool pretty_print) const {
  s.Printf("%lg", m_value);
}

void StructuredData::Boolean::Dump(Stream &s, bool pretty_print) const {
  if (m_value)
    s.PutCString("true");
  else
    s.PutCString("false");
}

void StructuredData::String::Dump(Stream &s, bool pretty_print) const {
  std::string quoted;
  const size_t strsize = m_value.size();
  for (size_t i = 0; i < strsize; ++i) {
    char ch = m_value[i];
    if (ch == '"' || ch == '\\')
      quoted.push_back('\\');
    quoted.push_back(ch);
  }
  s.Printf("\"%s\"", quoted.c_str());
}

void StructuredData::Dictionary::Dump(Stream &s, bool pretty_print) const {
  bool first = true;
  s << "{";
  if (pretty_print) {
    s << "\n";
    s.IndentMore();
  }
  for (const auto &pair : m_dict) {
    if (first)
      first = false;
    else {
      s << ",";
      if (pretty_print)
        s << "\n";
    }
    if (pretty_print)
      s.Indent();
    s << "\"" << pair.first.AsCString() << "\" : ";
    pair.second->Dump(s, pretty_print);
  }
  if (pretty_print) {
    s.IndentLess();
    s.EOL();
    s.Indent();
  }
  s << "}";
}

void StructuredData::Null::Dump(Stream &s, bool pretty_print) const {
  s << "null";
}

void StructuredData::Generic::Dump(Stream &s, bool pretty_print) const {
  s << "0x" << m_object;
}
