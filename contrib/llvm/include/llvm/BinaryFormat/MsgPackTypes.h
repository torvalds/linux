//===- MsgPackTypes.h - MsgPack Types ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This is a data structure for representing MessagePack "documents", with
/// methods to go to and from MessagePack. The types also specialize YAMLIO
/// traits in order to go to and from YAML.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Optional.h"
#include "llvm/BinaryFormat/MsgPackReader.h"
#include "llvm/BinaryFormat/MsgPackWriter.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/YAMLTraits.h"
#include <vector>

#ifndef LLVM_BINARYFORMAT_MSGPACKTYPES_H
#define LLVM_BINARYFORMAT_MSGPACKTYPES_H

namespace llvm {
namespace msgpack {

class Node;

/// Short-hand for a Node pointer.
using NodePtr = std::shared_ptr<Node>;

/// Short-hand for an Optional Node pointer.
using OptNodePtr = Optional<NodePtr>;

/// Abstract base-class which can be any MessagePack type.
class Node {
public:
  enum NodeKind {
    NK_Scalar,
    NK_Array,
    NK_Map,
  };

private:
  virtual void anchor() = 0;
  const NodeKind Kind;

  static Expected<OptNodePtr> readArray(Reader &MPReader, size_t Length);
  static Expected<OptNodePtr> readMap(Reader &MPReader, size_t Length);

public:
  NodeKind getKind() const { return Kind; }

  /// Construct a Node. Used by derived classes to track kind information.
  Node(NodeKind Kind) : Kind(Kind) {}

  virtual ~Node() = default;

  /// Read from a MessagePack reader \p MPReader, returning an error if one is
  /// encountered, or None if \p MPReader is at the end of stream, or some Node
  /// pointer if some type is read.
  static Expected<OptNodePtr> read(Reader &MPReader);

  /// Write to a MessagePack writer \p MPWriter.
  virtual void write(Writer &MPWriter) = 0;
};

/// A MessagePack scalar.
class ScalarNode : public Node {
public:
  enum ScalarKind {
    SK_Int,
    SK_UInt,
    SK_Nil,
    SK_Boolean,
    SK_Float,
    SK_String,
    SK_Binary,
  };

private:
  void anchor() override;

  void destroy();

  ScalarKind SKind;

  union {
    int64_t IntValue;
    uint64_t UIntValue;
    bool BoolValue;
    double FloatValue;
    std::string StringValue;
  };

public:
  /// Construct an Int ScalarNode.
  ScalarNode(int64_t IntValue);
  /// Construct an Int ScalarNode.
  ScalarNode(int32_t IntValue);
  /// Construct an UInt ScalarNode.
  ScalarNode(uint64_t UIntValue);
  /// Construct an UInt ScalarNode.
  ScalarNode(uint32_t UIntValue);
  /// Construct a Nil ScalarNode.
  ScalarNode();
  /// Construct a Boolean ScalarNode.
  ScalarNode(bool BoolValue);
  /// Construct a Float ScalarNode.
  ScalarNode(double FloatValue);
  /// Construct a String ScalarNode.
  ScalarNode(StringRef StringValue);
  /// Construct a String ScalarNode.
  ScalarNode(const char *StringValue);
  /// Construct a String ScalarNode.
  ScalarNode(std::string &&StringValue);
  /// Construct a Binary ScalarNode.
  ScalarNode(MemoryBufferRef BinaryValue);

  ~ScalarNode();

  ScalarNode &operator=(const ScalarNode &RHS) = delete;
  /// A ScalarNode can only be move assigned.
  ScalarNode &operator=(ScalarNode &&RHS);

  /// Change the kind of this ScalarNode, zero initializing it to the new type.
  void setScalarKind(ScalarKind SKind) {
    switch (SKind) {
    case SK_Int:
      *this = int64_t(0);
      break;
    case SK_UInt:
      *this = uint64_t(0);
      break;
    case SK_Boolean:
      *this = false;
      break;
    case SK_Float:
      *this = 0.0;
      break;
    case SK_String:
      *this = StringRef();
      break;
    case SK_Binary:
      *this = MemoryBufferRef("", "");
      break;
    case SK_Nil:
      *this = ScalarNode();
      break;
    }
  }

  /// Get the current kind of ScalarNode.
  ScalarKind getScalarKind() { return SKind; }

  /// Get the value of an Int scalar.
  ///
  /// \warning Assumes getScalarKind() == SK_Int
  int64_t getInt() {
    assert(SKind == SK_Int);
    return IntValue;
  }

  /// Get the value of a UInt scalar.
  ///
  /// \warning Assumes getScalarKind() == SK_UInt
  uint64_t getUInt() {
    assert(SKind == SK_UInt);
    return UIntValue;
  }

  /// Get the value of an Boolean scalar.
  ///
  /// \warning Assumes getScalarKind() == SK_Boolean
  bool getBool() {
    assert(SKind == SK_Boolean);
    return BoolValue;
  }

  /// Get the value of an Float scalar.
  ///
  /// \warning Assumes getScalarKind() == SK_Float
  double getFloat() {
    assert(SKind == SK_Float);
    return FloatValue;
  }

  /// Get the value of a String scalar.
  ///
  /// \warning Assumes getScalarKind() == SK_String
  StringRef getString() {
    assert(SKind == SK_String);
    return StringValue;
  }

  /// Get the value of a Binary scalar.
  ///
  /// \warning Assumes getScalarKind() == SK_Binary
  StringRef getBinary() {
    assert(SKind == SK_Binary);
    return StringValue;
  }

  static bool classof(const Node *N) { return N->getKind() == NK_Scalar; }

  void write(Writer &MPWriter) override;

  /// Parse a YAML scalar of the current ScalarKind from \p ScalarStr.
  ///
  /// \returns An empty string on success, otherwise an error message.
  StringRef inputYAML(StringRef ScalarStr);

  /// Output a YAML scalar of the current ScalarKind into \p OS.
  void outputYAML(raw_ostream &OS) const;

  /// Determine which YAML quoting type the current value would need when
  /// output.
  yaml::QuotingType mustQuoteYAML(StringRef ScalarStr) const;

  /// Get the YAML tag for the current ScalarKind.
  StringRef getYAMLTag() const;

  /// Flag which affects how the type handles YAML tags when reading and
  /// writing.
  ///
  /// When false, tags are used when reading and writing. When reading, the tag
  /// is used to decide the ScalarKind before parsing. When writing, the tag is
  /// output along with the value.
  ///
  /// When true, tags are ignored when reading and writing. When reading, the
  /// ScalarKind is always assumed to be String. When writing, the tag is not
  /// output.
  bool IgnoreTag = false;

  static const char *IntTag;
  static const char *NilTag;
  static const char *BooleanTag;
  static const char *FloatTag;
  static const char *StringTag;
  static const char *BinaryTag;
};

class ArrayNode : public Node, public std::vector<NodePtr> {
  void anchor() override;

public:
  ArrayNode() : Node(NK_Array) {}
  static bool classof(const Node *N) { return N->getKind() == NK_Array; }

  void write(Writer &MPWriter) override {
    MPWriter.writeArraySize(this->size());
    for (auto &N : *this)
      N->write(MPWriter);
  }
};

class MapNode : public Node, public StringMap<NodePtr> {
  void anchor() override;

public:
  MapNode() : Node(NK_Map) {}
  static bool classof(const Node *N) { return N->getKind() == NK_Map; }

  void write(Writer &MPWriter) override {
    MPWriter.writeMapSize(this->size());
    for (auto &N : *this) {
      MPWriter.write(N.first());
      N.second->write(MPWriter);
    }
  }
};

} // end namespace msgpack

namespace yaml {

template <> struct PolymorphicTraits<msgpack::NodePtr> {
  static NodeKind getKind(const msgpack::NodePtr &N) {
    if (isa<msgpack::ScalarNode>(*N))
      return NodeKind::Scalar;
    if (isa<msgpack::MapNode>(*N))
      return NodeKind::Map;
    if (isa<msgpack::ArrayNode>(*N))
      return NodeKind::Sequence;
    llvm_unreachable("NodeKind not supported");
  }
  static msgpack::ScalarNode &getAsScalar(msgpack::NodePtr &N) {
    if (!N || !isa<msgpack::ScalarNode>(*N))
      N.reset(new msgpack::ScalarNode());
    return *cast<msgpack::ScalarNode>(N.get());
  }
  static msgpack::MapNode &getAsMap(msgpack::NodePtr &N) {
    if (!N || !isa<msgpack::MapNode>(*N))
      N.reset(new msgpack::MapNode());
    return *cast<msgpack::MapNode>(N.get());
  }
  static msgpack::ArrayNode &getAsSequence(msgpack::NodePtr &N) {
    if (!N || !isa<msgpack::ArrayNode>(*N))
      N.reset(new msgpack::ArrayNode());
    return *cast<msgpack::ArrayNode>(N.get());
  }
};

template <> struct TaggedScalarTraits<msgpack::ScalarNode> {
  static void output(const msgpack::ScalarNode &S, void *Ctxt,
                     raw_ostream &ScalarOS, raw_ostream &TagOS) {
    if (!S.IgnoreTag)
      TagOS << S.getYAMLTag();
    S.outputYAML(ScalarOS);
  }

  static StringRef input(StringRef ScalarStr, StringRef Tag, void *Ctxt,
                         msgpack::ScalarNode &S) {
    if (Tag == msgpack::ScalarNode::IntTag) {
      S.setScalarKind(msgpack::ScalarNode::SK_UInt);
      if (S.inputYAML(ScalarStr) == StringRef())
        return StringRef();
      S.setScalarKind(msgpack::ScalarNode::SK_Int);
      return S.inputYAML(ScalarStr);
    }

    if (S.IgnoreTag || Tag == msgpack::ScalarNode::StringTag ||
        Tag == "tag:yaml.org,2002:str")
      S.setScalarKind(msgpack::ScalarNode::SK_String);
    else if (Tag == msgpack::ScalarNode::NilTag)
      S.setScalarKind(msgpack::ScalarNode::SK_Nil);
    else if (Tag == msgpack::ScalarNode::BooleanTag)
      S.setScalarKind(msgpack::ScalarNode::SK_Boolean);
    else if (Tag == msgpack::ScalarNode::FloatTag)
      S.setScalarKind(msgpack::ScalarNode::SK_Float);
    else if (Tag == msgpack::ScalarNode::StringTag)
      S.setScalarKind(msgpack::ScalarNode::SK_String);
    else if (Tag == msgpack::ScalarNode::BinaryTag)
      S.setScalarKind(msgpack::ScalarNode::SK_Binary);
    else
      return "Unsupported messagepack tag";

    return S.inputYAML(ScalarStr);
  }

  static QuotingType mustQuote(const msgpack::ScalarNode &S, StringRef Str) {
    return S.mustQuoteYAML(Str);
  }
};

template <> struct CustomMappingTraits<msgpack::MapNode> {
  static void inputOne(IO &IO, StringRef Key, msgpack::MapNode &M) {
    IO.mapRequired(Key.str().c_str(), M[Key]);
  }
  static void output(IO &IO, msgpack::MapNode &M) {
    for (auto &N : M)
      IO.mapRequired(N.getKey().str().c_str(), N.getValue());
  }
};

template <> struct SequenceTraits<msgpack::ArrayNode> {
  static size_t size(IO &IO, msgpack::ArrayNode &A) { return A.size(); }
  static msgpack::NodePtr &element(IO &IO, msgpack::ArrayNode &A,
                                   size_t Index) {
    if (Index >= A.size())
      A.resize(Index + 1);
    return A[Index];
  }
};

} // end namespace yaml
} // end namespace llvm

#endif //  LLVM_BINARYFORMAT_MSGPACKTYPES_H
