//===- MsgPackTypes.cpp - MsgPack Types -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Implementation of types representing MessagePack "documents".
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MsgPackTypes.h"
#include "llvm/Support/Error.h"

using namespace llvm;
using namespace msgpack;

namespace llvm {
namespace msgpack {
void ScalarNode::anchor() {}
void ArrayNode::anchor() {}
void MapNode::anchor() {}
}
}

Expected<OptNodePtr> Node::readArray(Reader &MPReader, size_t Length) {
  auto A = std::make_shared<ArrayNode>();
  for (size_t I = 0; I < Length; ++I) {
    auto OptNodeOrErr = Node::read(MPReader);
    if (auto Err = OptNodeOrErr.takeError())
      return std::move(Err);
    if (!*OptNodeOrErr)
      return make_error<StringError>(
          "Insufficient array elements",
          std::make_error_code(std::errc::invalid_argument));
    A->push_back(std::move(**OptNodeOrErr));
  }
  return OptNodePtr(std::move(A));
}

Expected<OptNodePtr> Node::readMap(Reader &MPReader, size_t Length) {
  auto M = std::make_shared<MapNode>();
  for (size_t I = 0; I < Length; ++I) {
    auto OptKeyOrErr = Node::read(MPReader);
    if (auto Err = OptKeyOrErr.takeError())
      return std::move(Err);
    if (!*OptKeyOrErr)
      return make_error<StringError>(
          "Insufficient map elements",
          std::make_error_code(std::errc::invalid_argument));
    auto OptValOrErr = Node::read(MPReader);
    if (auto Err = OptValOrErr.takeError())
      return std::move(Err);
    if (!*OptValOrErr)
      return make_error<StringError>(
          "Insufficient map elements",
          std::make_error_code(std::errc::invalid_argument));
    auto *Key = dyn_cast<ScalarNode>((*OptKeyOrErr)->get());
    if (!Key)
      return make_error<StringError>(
          "Only string map keys are supported",
          std::make_error_code(std::errc::invalid_argument));
    if (Key->getScalarKind() != ScalarNode::SK_String)
      return make_error<StringError>(
          "Only string map keys are supported",
          std::make_error_code(std::errc::invalid_argument));
    M->try_emplace(Key->getString(), std::move(**OptValOrErr));
  }
  return OptNodePtr(std::move(M));
}

Expected<OptNodePtr> Node::read(Reader &MPReader) {
  Object Obj;

  auto ContinueOrErr = MPReader.read(Obj);
  if (auto Err = ContinueOrErr.takeError())
    return std::move(Err);
  if (!*ContinueOrErr)
    return None;

  switch (Obj.Kind) {
  case Type::Int:
    return OptNodePtr(std::make_shared<ScalarNode>(Obj.Int));
  case Type::UInt:
    return OptNodePtr(std::make_shared<ScalarNode>(Obj.UInt));
  case Type::Nil:
    return OptNodePtr(std::make_shared<ScalarNode>());
  case Type::Boolean:
    return OptNodePtr(std::make_shared<ScalarNode>(Obj.Bool));
  case Type::Float:
    return OptNodePtr(std::make_shared<ScalarNode>(Obj.Float));
  case Type::String:
    return OptNodePtr(std::make_shared<ScalarNode>(Obj.Raw));
  case Type::Binary:
    return OptNodePtr(std::make_shared<ScalarNode>(Obj.Raw));
  case Type::Array:
    return Node::readArray(MPReader, Obj.Length);
  case Type::Map:
    return Node::readMap(MPReader, Obj.Length);
  case Type::Extension:
    return make_error<StringError>(
        "Extension types are not supported",
        std::make_error_code(std::errc::invalid_argument));
  }
  llvm_unreachable("msgpack::Type not handled");
}

void ScalarNode::destroy() {
  switch (SKind) {
  case SK_String:
  case SK_Binary:
    StringValue.~basic_string();
    break;
  default:
    // POD types do not require destruction
    break;
  }
}

ScalarNode::ScalarNode(int64_t IntValue)
    : Node(NK_Scalar), SKind(SK_Int), IntValue(IntValue) {}

ScalarNode::ScalarNode(int32_t IntValue)
    : ScalarNode(static_cast<int64_t>(IntValue)) {}

ScalarNode::ScalarNode(uint64_t UIntValue)
    : Node(NK_Scalar), SKind(SK_UInt), UIntValue(UIntValue) {}

ScalarNode::ScalarNode(uint32_t IntValue)
    : ScalarNode(static_cast<uint64_t>(IntValue)) {}

ScalarNode::ScalarNode() : Node(NK_Scalar), SKind(SK_Nil) {}

ScalarNode::ScalarNode(bool BoolValue)
    : Node(NK_Scalar), SKind(SK_Boolean), BoolValue(BoolValue) {}

ScalarNode::ScalarNode(double FloatValue)
    : Node(NK_Scalar), SKind(SK_Float), BoolValue(FloatValue) {}

ScalarNode::ScalarNode(StringRef StringValue)
    : Node(NK_Scalar), SKind(SK_String) {
  new (&this->StringValue) std::string(StringValue);
}

ScalarNode::ScalarNode(const char *StringValue)
    : ScalarNode(StringRef(StringValue)) {}

ScalarNode::ScalarNode(std::string &&StringValue)
    : Node(NK_Scalar), SKind(SK_String) {
  new (&this->StringValue) std::string(StringValue);
}

ScalarNode::ScalarNode(MemoryBufferRef BinaryValue)
    : Node(NK_Scalar), SKind(SK_Binary) {
  new (&StringValue) std::string(BinaryValue.getBuffer());
}

ScalarNode::~ScalarNode() { destroy(); }

ScalarNode &ScalarNode::operator=(ScalarNode &&RHS) {
  destroy();
  switch (SKind = RHS.SKind) {
  case SK_Int:
    IntValue = RHS.IntValue;
    break;
  case SK_UInt:
    UIntValue = RHS.UIntValue;
    break;
  case SK_Boolean:
    BoolValue = RHS.BoolValue;
    break;
  case SK_Float:
    FloatValue = RHS.FloatValue;
    break;
  case SK_String:
  case SK_Binary:
    new (&StringValue) std::string(std::move(RHS.StringValue));
    break;
  case SK_Nil:
    // pass
    break;
  }
  return *this;
}

StringRef ScalarNode::inputYAML(StringRef ScalarStr) {
  switch (SKind) {
  case SK_Int:
    return yaml::ScalarTraits<int64_t>::input(ScalarStr, nullptr, IntValue);
  case SK_UInt:
    return yaml::ScalarTraits<uint64_t>::input(ScalarStr, nullptr, UIntValue);
  case SK_Nil:
    return StringRef();
  case SK_Boolean:
    return yaml::ScalarTraits<bool>::input(ScalarStr, nullptr, BoolValue);
  case SK_Float:
    return yaml::ScalarTraits<double>::input(ScalarStr, nullptr, FloatValue);
  case SK_Binary:
  case SK_String:
    return yaml::ScalarTraits<std::string>::input(ScalarStr, nullptr,
                                                  StringValue);
  }
  llvm_unreachable("unrecognized ScalarKind");
}

void ScalarNode::outputYAML(raw_ostream &OS) const {
  switch (SKind) {
  case SK_Int:
    yaml::ScalarTraits<int64_t>::output(IntValue, nullptr, OS);
    break;
  case SK_UInt:
    yaml::ScalarTraits<uint64_t>::output(UIntValue, nullptr, OS);
    break;
  case SK_Nil:
    yaml::ScalarTraits<StringRef>::output("", nullptr, OS);
    break;
  case SK_Boolean:
    yaml::ScalarTraits<bool>::output(BoolValue, nullptr, OS);
    break;
  case SK_Float:
    yaml::ScalarTraits<double>::output(FloatValue, nullptr, OS);
    break;
  case SK_Binary:
  case SK_String:
    yaml::ScalarTraits<std::string>::output(StringValue, nullptr, OS);
    break;
  }
}

yaml::QuotingType ScalarNode::mustQuoteYAML(StringRef ScalarStr) const {
  switch (SKind) {
  case SK_Int:
    return yaml::ScalarTraits<int64_t>::mustQuote(ScalarStr);
  case SK_UInt:
    return yaml::ScalarTraits<uint64_t>::mustQuote(ScalarStr);
  case SK_Nil:
    return yaml::ScalarTraits<StringRef>::mustQuote(ScalarStr);
  case SK_Boolean:
    return yaml::ScalarTraits<bool>::mustQuote(ScalarStr);
  case SK_Float:
    return yaml::ScalarTraits<double>::mustQuote(ScalarStr);
  case SK_Binary:
  case SK_String:
    return yaml::ScalarTraits<std::string>::mustQuote(ScalarStr);
  }
  llvm_unreachable("unrecognized ScalarKind");
}

const char *ScalarNode::IntTag = "!int";
const char *ScalarNode::NilTag = "!nil";
const char *ScalarNode::BooleanTag = "!bool";
const char *ScalarNode::FloatTag = "!float";
const char *ScalarNode::StringTag = "!str";
const char *ScalarNode::BinaryTag = "!bin";

StringRef ScalarNode::getYAMLTag() const {
  switch (SKind) {
  case SK_Int:
    return IntTag;
  case SK_UInt:
    return IntTag;
  case SK_Nil:
    return NilTag;
  case SK_Boolean:
    return BooleanTag;
  case SK_Float:
    return FloatTag;
  case SK_String:
    return StringTag;
  case SK_Binary:
    return BinaryTag;
  }
  llvm_unreachable("unrecognized ScalarKind");
}

void ScalarNode::write(Writer &MPWriter) {
  switch (SKind) {
  case SK_Int:
    MPWriter.write(IntValue);
    break;
  case SK_UInt:
    MPWriter.write(UIntValue);
    break;
  case SK_Nil:
    MPWriter.writeNil();
    break;
  case SK_Boolean:
    MPWriter.write(BoolValue);
    break;
  case SK_Float:
    MPWriter.write(FloatValue);
    break;
  case SK_String:
    MPWriter.write(StringValue);
    break;
  case SK_Binary:
    MPWriter.write(MemoryBufferRef(StringValue, ""));
    break;
  }
}
