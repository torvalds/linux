//===- lib/Support/YAMLTraits.cpp -----------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/YAMLTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Unicode.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace llvm;
using namespace yaml;

//===----------------------------------------------------------------------===//
//  IO
//===----------------------------------------------------------------------===//

IO::IO(void *Context) : Ctxt(Context) {}

IO::~IO() = default;

void *IO::getContext() {
  return Ctxt;
}

void IO::setContext(void *Context) {
  Ctxt = Context;
}

//===----------------------------------------------------------------------===//
//  Input
//===----------------------------------------------------------------------===//

Input::Input(StringRef InputContent, void *Ctxt,
             SourceMgr::DiagHandlerTy DiagHandler, void *DiagHandlerCtxt)
    : IO(Ctxt), Strm(new Stream(InputContent, SrcMgr, false, &EC)) {
  if (DiagHandler)
    SrcMgr.setDiagHandler(DiagHandler, DiagHandlerCtxt);
  DocIterator = Strm->begin();
}

Input::Input(MemoryBufferRef Input, void *Ctxt,
             SourceMgr::DiagHandlerTy DiagHandler, void *DiagHandlerCtxt)
    : IO(Ctxt), Strm(new Stream(Input, SrcMgr, false, &EC)) {
  if (DiagHandler)
    SrcMgr.setDiagHandler(DiagHandler, DiagHandlerCtxt);
  DocIterator = Strm->begin();
}

Input::~Input() = default;

std::error_code Input::error() { return EC; }

// Pin the vtables to this file.
void Input::HNode::anchor() {}
void Input::EmptyHNode::anchor() {}
void Input::ScalarHNode::anchor() {}
void Input::MapHNode::anchor() {}
void Input::SequenceHNode::anchor() {}

bool Input::outputting() {
  return false;
}

bool Input::setCurrentDocument() {
  if (DocIterator != Strm->end()) {
    Node *N = DocIterator->getRoot();
    if (!N) {
      assert(Strm->failed() && "Root is NULL iff parsing failed");
      EC = make_error_code(errc::invalid_argument);
      return false;
    }

    if (isa<NullNode>(N)) {
      // Empty files are allowed and ignored
      ++DocIterator;
      return setCurrentDocument();
    }
    TopNode = createHNodes(N);
    CurrentNode = TopNode.get();
    return true;
  }
  return false;
}

bool Input::nextDocument() {
  return ++DocIterator != Strm->end();
}

const Node *Input::getCurrentNode() const {
  return CurrentNode ? CurrentNode->_node : nullptr;
}

bool Input::mapTag(StringRef Tag, bool Default) {
  std::string foundTag = CurrentNode->_node->getVerbatimTag();
  if (foundTag.empty()) {
    // If no tag found and 'Tag' is the default, say it was found.
    return Default;
  }
  // Return true iff found tag matches supplied tag.
  return Tag.equals(foundTag);
}

void Input::beginMapping() {
  if (EC)
    return;
  // CurrentNode can be null if the document is empty.
  MapHNode *MN = dyn_cast_or_null<MapHNode>(CurrentNode);
  if (MN) {
    MN->ValidKeys.clear();
  }
}

std::vector<StringRef> Input::keys() {
  MapHNode *MN = dyn_cast<MapHNode>(CurrentNode);
  std::vector<StringRef> Ret;
  if (!MN) {
    setError(CurrentNode, "not a mapping");
    return Ret;
  }
  for (auto &P : MN->Mapping)
    Ret.push_back(P.first());
  return Ret;
}

bool Input::preflightKey(const char *Key, bool Required, bool, bool &UseDefault,
                         void *&SaveInfo) {
  UseDefault = false;
  if (EC)
    return false;

  // CurrentNode is null for empty documents, which is an error in case required
  // nodes are present.
  if (!CurrentNode) {
    if (Required)
      EC = make_error_code(errc::invalid_argument);
    return false;
  }

  MapHNode *MN = dyn_cast<MapHNode>(CurrentNode);
  if (!MN) {
    if (Required || !isa<EmptyHNode>(CurrentNode))
      setError(CurrentNode, "not a mapping");
    return false;
  }
  MN->ValidKeys.push_back(Key);
  HNode *Value = MN->Mapping[Key].get();
  if (!Value) {
    if (Required)
      setError(CurrentNode, Twine("missing required key '") + Key + "'");
    else
      UseDefault = true;
    return false;
  }
  SaveInfo = CurrentNode;
  CurrentNode = Value;
  return true;
}

void Input::postflightKey(void *saveInfo) {
  CurrentNode = reinterpret_cast<HNode *>(saveInfo);
}

void Input::endMapping() {
  if (EC)
    return;
  // CurrentNode can be null if the document is empty.
  MapHNode *MN = dyn_cast_or_null<MapHNode>(CurrentNode);
  if (!MN)
    return;
  for (const auto &NN : MN->Mapping) {
    if (!is_contained(MN->ValidKeys, NN.first())) {
      setError(NN.second.get(), Twine("unknown key '") + NN.first() + "'");
      break;
    }
  }
}

void Input::beginFlowMapping() { beginMapping(); }

void Input::endFlowMapping() { endMapping(); }

unsigned Input::beginSequence() {
  if (SequenceHNode *SQ = dyn_cast<SequenceHNode>(CurrentNode))
    return SQ->Entries.size();
  if (isa<EmptyHNode>(CurrentNode))
    return 0;
  // Treat case where there's a scalar "null" value as an empty sequence.
  if (ScalarHNode *SN = dyn_cast<ScalarHNode>(CurrentNode)) {
    if (isNull(SN->value()))
      return 0;
  }
  // Any other type of HNode is an error.
  setError(CurrentNode, "not a sequence");
  return 0;
}

void Input::endSequence() {
}

bool Input::preflightElement(unsigned Index, void *&SaveInfo) {
  if (EC)
    return false;
  if (SequenceHNode *SQ = dyn_cast<SequenceHNode>(CurrentNode)) {
    SaveInfo = CurrentNode;
    CurrentNode = SQ->Entries[Index].get();
    return true;
  }
  return false;
}

void Input::postflightElement(void *SaveInfo) {
  CurrentNode = reinterpret_cast<HNode *>(SaveInfo);
}

unsigned Input::beginFlowSequence() { return beginSequence(); }

bool Input::preflightFlowElement(unsigned index, void *&SaveInfo) {
  if (EC)
    return false;
  if (SequenceHNode *SQ = dyn_cast<SequenceHNode>(CurrentNode)) {
    SaveInfo = CurrentNode;
    CurrentNode = SQ->Entries[index].get();
    return true;
  }
  return false;
}

void Input::postflightFlowElement(void *SaveInfo) {
  CurrentNode = reinterpret_cast<HNode *>(SaveInfo);
}

void Input::endFlowSequence() {
}

void Input::beginEnumScalar() {
  ScalarMatchFound = false;
}

bool Input::matchEnumScalar(const char *Str, bool) {
  if (ScalarMatchFound)
    return false;
  if (ScalarHNode *SN = dyn_cast<ScalarHNode>(CurrentNode)) {
    if (SN->value().equals(Str)) {
      ScalarMatchFound = true;
      return true;
    }
  }
  return false;
}

bool Input::matchEnumFallback() {
  if (ScalarMatchFound)
    return false;
  ScalarMatchFound = true;
  return true;
}

void Input::endEnumScalar() {
  if (!ScalarMatchFound) {
    setError(CurrentNode, "unknown enumerated scalar");
  }
}

bool Input::beginBitSetScalar(bool &DoClear) {
  BitValuesUsed.clear();
  if (SequenceHNode *SQ = dyn_cast<SequenceHNode>(CurrentNode)) {
    BitValuesUsed.insert(BitValuesUsed.begin(), SQ->Entries.size(), false);
  } else {
    setError(CurrentNode, "expected sequence of bit values");
  }
  DoClear = true;
  return true;
}

bool Input::bitSetMatch(const char *Str, bool) {
  if (EC)
    return false;
  if (SequenceHNode *SQ = dyn_cast<SequenceHNode>(CurrentNode)) {
    unsigned Index = 0;
    for (auto &N : SQ->Entries) {
      if (ScalarHNode *SN = dyn_cast<ScalarHNode>(N.get())) {
        if (SN->value().equals(Str)) {
          BitValuesUsed[Index] = true;
          return true;
        }
      } else {
        setError(CurrentNode, "unexpected scalar in sequence of bit values");
      }
      ++Index;
    }
  } else {
    setError(CurrentNode, "expected sequence of bit values");
  }
  return false;
}

void Input::endBitSetScalar() {
  if (EC)
    return;
  if (SequenceHNode *SQ = dyn_cast<SequenceHNode>(CurrentNode)) {
    assert(BitValuesUsed.size() == SQ->Entries.size());
    for (unsigned i = 0; i < SQ->Entries.size(); ++i) {
      if (!BitValuesUsed[i]) {
        setError(SQ->Entries[i].get(), "unknown bit value");
        return;
      }
    }
  }
}

void Input::scalarString(StringRef &S, QuotingType) {
  if (ScalarHNode *SN = dyn_cast<ScalarHNode>(CurrentNode)) {
    S = SN->value();
  } else {
    setError(CurrentNode, "unexpected scalar");
  }
}

void Input::blockScalarString(StringRef &S) { scalarString(S, QuotingType::None); }

void Input::scalarTag(std::string &Tag) {
  Tag = CurrentNode->_node->getVerbatimTag();
}

void Input::setError(HNode *hnode, const Twine &message) {
  assert(hnode && "HNode must not be NULL");
  setError(hnode->_node, message);
}

NodeKind Input::getNodeKind() {
  if (isa<ScalarHNode>(CurrentNode))
    return NodeKind::Scalar;
  else if (isa<MapHNode>(CurrentNode))
    return NodeKind::Map;
  else if (isa<SequenceHNode>(CurrentNode))
    return NodeKind::Sequence;
  llvm_unreachable("Unsupported node kind");
}

void Input::setError(Node *node, const Twine &message) {
  Strm->printError(node, message);
  EC = make_error_code(errc::invalid_argument);
}

std::unique_ptr<Input::HNode> Input::createHNodes(Node *N) {
  SmallString<128> StringStorage;
  if (ScalarNode *SN = dyn_cast<ScalarNode>(N)) {
    StringRef KeyStr = SN->getValue(StringStorage);
    if (!StringStorage.empty()) {
      // Copy string to permanent storage
      KeyStr = StringStorage.str().copy(StringAllocator);
    }
    return llvm::make_unique<ScalarHNode>(N, KeyStr);
  } else if (BlockScalarNode *BSN = dyn_cast<BlockScalarNode>(N)) {
    StringRef ValueCopy = BSN->getValue().copy(StringAllocator);
    return llvm::make_unique<ScalarHNode>(N, ValueCopy);
  } else if (SequenceNode *SQ = dyn_cast<SequenceNode>(N)) {
    auto SQHNode = llvm::make_unique<SequenceHNode>(N);
    for (Node &SN : *SQ) {
      auto Entry = createHNodes(&SN);
      if (EC)
        break;
      SQHNode->Entries.push_back(std::move(Entry));
    }
    return std::move(SQHNode);
  } else if (MappingNode *Map = dyn_cast<MappingNode>(N)) {
    auto mapHNode = llvm::make_unique<MapHNode>(N);
    for (KeyValueNode &KVN : *Map) {
      Node *KeyNode = KVN.getKey();
      ScalarNode *Key = dyn_cast<ScalarNode>(KeyNode);
      Node *Value = KVN.getValue();
      if (!Key || !Value) {
        if (!Key)
          setError(KeyNode, "Map key must be a scalar");
        if (!Value)
          setError(KeyNode, "Map value must not be empty");
        break;
      }
      StringStorage.clear();
      StringRef KeyStr = Key->getValue(StringStorage);
      if (!StringStorage.empty()) {
        // Copy string to permanent storage
        KeyStr = StringStorage.str().copy(StringAllocator);
      }
      auto ValueHNode = createHNodes(Value);
      if (EC)
        break;
      mapHNode->Mapping[KeyStr] = std::move(ValueHNode);
    }
    return std::move(mapHNode);
  } else if (isa<NullNode>(N)) {
    return llvm::make_unique<EmptyHNode>(N);
  } else {
    setError(N, "unknown node kind");
    return nullptr;
  }
}

void Input::setError(const Twine &Message) {
  setError(CurrentNode, Message);
}

bool Input::canElideEmptySequence() {
  return false;
}

//===----------------------------------------------------------------------===//
//  Output
//===----------------------------------------------------------------------===//

Output::Output(raw_ostream &yout, void *context, int WrapColumn)
    : IO(context), Out(yout), WrapColumn(WrapColumn) {}

Output::~Output() = default;

bool Output::outputting() {
  return true;
}

void Output::beginMapping() {
  StateStack.push_back(inMapFirstKey);
  NeedsNewLine = true;
}

bool Output::mapTag(StringRef Tag, bool Use) {
  if (Use) {
    // If this tag is being written inside a sequence we should write the start
    // of the sequence before writing the tag, otherwise the tag won't be
    // attached to the element in the sequence, but rather the sequence itself.
    bool SequenceElement = false;
    if (StateStack.size() > 1) {
      auto &E = StateStack[StateStack.size() - 2];
      SequenceElement = inSeqAnyElement(E) || inFlowSeqAnyElement(E);
    }
    if (SequenceElement && StateStack.back() == inMapFirstKey) {
      newLineCheck();
    } else {
      output(" ");
    }
    output(Tag);
    if (SequenceElement) {
      // If we're writing the tag during the first element of a map, the tag
      // takes the place of the first element in the sequence.
      if (StateStack.back() == inMapFirstKey) {
        StateStack.pop_back();
        StateStack.push_back(inMapOtherKey);
      }
      // Tags inside maps in sequences should act as keys in the map from a
      // formatting perspective, so we always want a newline in a sequence.
      NeedsNewLine = true;
    }
  }
  return Use;
}

void Output::endMapping() {
  // If we did not map anything, we should explicitly emit an empty map
  if (StateStack.back() == inMapFirstKey)
    output("{}");
  StateStack.pop_back();
}

std::vector<StringRef> Output::keys() {
  report_fatal_error("invalid call");
}

bool Output::preflightKey(const char *Key, bool Required, bool SameAsDefault,
                          bool &UseDefault, void *&) {
  UseDefault = false;
  if (Required || !SameAsDefault || WriteDefaultValues) {
    auto State = StateStack.back();
    if (State == inFlowMapFirstKey || State == inFlowMapOtherKey) {
      flowKey(Key);
    } else {
      newLineCheck();
      paddedKey(Key);
    }
    return true;
  }
  return false;
}

void Output::postflightKey(void *) {
  if (StateStack.back() == inMapFirstKey) {
    StateStack.pop_back();
    StateStack.push_back(inMapOtherKey);
  } else if (StateStack.back() == inFlowMapFirstKey) {
    StateStack.pop_back();
    StateStack.push_back(inFlowMapOtherKey);
  }
}

void Output::beginFlowMapping() {
  StateStack.push_back(inFlowMapFirstKey);
  newLineCheck();
  ColumnAtMapFlowStart = Column;
  output("{ ");
}

void Output::endFlowMapping() {
  StateStack.pop_back();
  outputUpToEndOfLine(" }");
}

void Output::beginDocuments() {
  outputUpToEndOfLine("---");
}

bool Output::preflightDocument(unsigned index) {
  if (index > 0)
    outputUpToEndOfLine("\n---");
  return true;
}

void Output::postflightDocument() {
}

void Output::endDocuments() {
  output("\n...\n");
}

unsigned Output::beginSequence() {
  StateStack.push_back(inSeqFirstElement);
  NeedsNewLine = true;
  return 0;
}

void Output::endSequence() {
  // If we did not emit anything, we should explicitly emit an empty sequence
  if (StateStack.back() == inSeqFirstElement)
    output("[]");
  StateStack.pop_back();
}

bool Output::preflightElement(unsigned, void *&) {
  return true;
}

void Output::postflightElement(void *) {
  if (StateStack.back() == inSeqFirstElement) {
    StateStack.pop_back();
    StateStack.push_back(inSeqOtherElement);
  } else if (StateStack.back() == inFlowSeqFirstElement) {
    StateStack.pop_back();
    StateStack.push_back(inFlowSeqOtherElement);
  }
}

unsigned Output::beginFlowSequence() {
  StateStack.push_back(inFlowSeqFirstElement);
  newLineCheck();
  ColumnAtFlowStart = Column;
  output("[ ");
  NeedFlowSequenceComma = false;
  return 0;
}

void Output::endFlowSequence() {
  StateStack.pop_back();
  outputUpToEndOfLine(" ]");
}

bool Output::preflightFlowElement(unsigned, void *&) {
  if (NeedFlowSequenceComma)
    output(", ");
  if (WrapColumn && Column > WrapColumn) {
    output("\n");
    for (int i = 0; i < ColumnAtFlowStart; ++i)
      output(" ");
    Column = ColumnAtFlowStart;
    output("  ");
  }
  return true;
}

void Output::postflightFlowElement(void *) {
  NeedFlowSequenceComma = true;
}

void Output::beginEnumScalar() {
  EnumerationMatchFound = false;
}

bool Output::matchEnumScalar(const char *Str, bool Match) {
  if (Match && !EnumerationMatchFound) {
    newLineCheck();
    outputUpToEndOfLine(Str);
    EnumerationMatchFound = true;
  }
  return false;
}

bool Output::matchEnumFallback() {
  if (EnumerationMatchFound)
    return false;
  EnumerationMatchFound = true;
  return true;
}

void Output::endEnumScalar() {
  if (!EnumerationMatchFound)
    llvm_unreachable("bad runtime enum value");
}

bool Output::beginBitSetScalar(bool &DoClear) {
  newLineCheck();
  output("[ ");
  NeedBitValueComma = false;
  DoClear = false;
  return true;
}

bool Output::bitSetMatch(const char *Str, bool Matches) {
  if (Matches) {
    if (NeedBitValueComma)
      output(", ");
    output(Str);
    NeedBitValueComma = true;
  }
  return false;
}

void Output::endBitSetScalar() {
  outputUpToEndOfLine(" ]");
}

void Output::scalarString(StringRef &S, QuotingType MustQuote) {
  newLineCheck();
  if (S.empty()) {
    // Print '' for the empty string because leaving the field empty is not
    // allowed.
    outputUpToEndOfLine("''");
    return;
  }
  if (MustQuote == QuotingType::None) {
    // Only quote if we must.
    outputUpToEndOfLine(S);
    return;
  }

  unsigned i = 0;
  unsigned j = 0;
  unsigned End = S.size();
  const char *Base = S.data();

  const char *const Quote = MustQuote == QuotingType::Single ? "'" : "\"";
  output(Quote); // Starting quote.

  // When using double-quoted strings (and only in that case), non-printable characters may be
  // present, and will be escaped using a variety of unicode-scalar and special short-form
  // escapes. This is handled in yaml::escape.
  if (MustQuote == QuotingType::Double) {
    output(yaml::escape(Base, /* EscapePrintable= */ false));
    outputUpToEndOfLine(Quote);
    return;
  }

  // When using single-quoted strings, any single quote ' must be doubled to be escaped.
  while (j < End) {
    if (S[j] == '\'') {                    // Escape quotes.
      output(StringRef(&Base[i], j - i));  // "flush".
      output(StringLiteral("''"));         // Print it as ''
      i = j + 1;
    }
    ++j;
  }
  output(StringRef(&Base[i], j - i));
  outputUpToEndOfLine(Quote); // Ending quote.
}

void Output::blockScalarString(StringRef &S) {
  if (!StateStack.empty())
    newLineCheck();
  output(" |");
  outputNewLine();

  unsigned Indent = StateStack.empty() ? 1 : StateStack.size();

  auto Buffer = MemoryBuffer::getMemBuffer(S, "", false);
  for (line_iterator Lines(*Buffer, false); !Lines.is_at_end(); ++Lines) {
    for (unsigned I = 0; I < Indent; ++I) {
      output("  ");
    }
    output(*Lines);
    outputNewLine();
  }
}

void Output::scalarTag(std::string &Tag) {
  if (Tag.empty())
    return;
  newLineCheck();
  output(Tag);
  output(" ");
}

void Output::setError(const Twine &message) {
}

bool Output::canElideEmptySequence() {
  // Normally, with an optional key/value where the value is an empty sequence,
  // the whole key/value can be not written.  But, that produces wrong yaml
  // if the key/value is the only thing in the map and the map is used in
  // a sequence.  This detects if the this sequence is the first key/value
  // in map that itself is embedded in a sequnce.
  if (StateStack.size() < 2)
    return true;
  if (StateStack.back() != inMapFirstKey)
    return true;
  return !inSeqAnyElement(StateStack[StateStack.size() - 2]);
}

void Output::output(StringRef s) {
  Column += s.size();
  Out << s;
}

void Output::outputUpToEndOfLine(StringRef s) {
  output(s);
  if (StateStack.empty() || (!inFlowSeqAnyElement(StateStack.back()) &&
                             !inFlowMapAnyKey(StateStack.back())))
    NeedsNewLine = true;
}

void Output::outputNewLine() {
  Out << "\n";
  Column = 0;
}

// if seq at top, indent as if map, then add "- "
// if seq in middle, use "- " if firstKey, else use "  "
//

void Output::newLineCheck() {
  if (!NeedsNewLine)
    return;
  NeedsNewLine = false;

  outputNewLine();

  if (StateStack.size() == 0)
    return;

  unsigned Indent = StateStack.size() - 1;
  bool OutputDash = false;

  if (StateStack.back() == inSeqFirstElement ||
      StateStack.back() == inSeqOtherElement) {
    OutputDash = true;
  } else if ((StateStack.size() > 1) &&
             ((StateStack.back() == inMapFirstKey) ||
              inFlowSeqAnyElement(StateStack.back()) ||
              (StateStack.back() == inFlowMapFirstKey)) &&
             inSeqAnyElement(StateStack[StateStack.size() - 2])) {
    --Indent;
    OutputDash = true;
  }

  for (unsigned i = 0; i < Indent; ++i) {
    output("  ");
  }
  if (OutputDash) {
    output("- ");
  }

}

void Output::paddedKey(StringRef key) {
  output(key);
  output(":");
  const char *spaces = "                ";
  if (key.size() < strlen(spaces))
    output(&spaces[key.size()]);
  else
    output(" ");
}

void Output::flowKey(StringRef Key) {
  if (StateStack.back() == inFlowMapOtherKey)
    output(", ");
  if (WrapColumn && Column > WrapColumn) {
    output("\n");
    for (int I = 0; I < ColumnAtMapFlowStart; ++I)
      output(" ");
    Column = ColumnAtMapFlowStart;
    output("  ");
  }
  output(Key);
  output(": ");
}

NodeKind Output::getNodeKind() { report_fatal_error("invalid call"); }

bool Output::inSeqAnyElement(InState State) {
  return State == inSeqFirstElement || State == inSeqOtherElement;
}

bool Output::inFlowSeqAnyElement(InState State) {
  return State == inFlowSeqFirstElement || State == inFlowSeqOtherElement;
}

bool Output::inMapAnyKey(InState State) {
  return State == inMapFirstKey || State == inMapOtherKey;
}

bool Output::inFlowMapAnyKey(InState State) {
  return State == inFlowMapFirstKey || State == inFlowMapOtherKey;
}

//===----------------------------------------------------------------------===//
//  traits for built-in types
//===----------------------------------------------------------------------===//

void ScalarTraits<bool>::output(const bool &Val, void *, raw_ostream &Out) {
  Out << (Val ? "true" : "false");
}

StringRef ScalarTraits<bool>::input(StringRef Scalar, void *, bool &Val) {
  if (Scalar.equals("true")) {
    Val = true;
    return StringRef();
  } else if (Scalar.equals("false")) {
    Val = false;
    return StringRef();
  }
  return "invalid boolean";
}

void ScalarTraits<StringRef>::output(const StringRef &Val, void *,
                                     raw_ostream &Out) {
  Out << Val;
}

StringRef ScalarTraits<StringRef>::input(StringRef Scalar, void *,
                                         StringRef &Val) {
  Val = Scalar;
  return StringRef();
}

void ScalarTraits<std::string>::output(const std::string &Val, void *,
                                     raw_ostream &Out) {
  Out << Val;
}

StringRef ScalarTraits<std::string>::input(StringRef Scalar, void *,
                                         std::string &Val) {
  Val = Scalar.str();
  return StringRef();
}

void ScalarTraits<uint8_t>::output(const uint8_t &Val, void *,
                                   raw_ostream &Out) {
  // use temp uin32_t because ostream thinks uint8_t is a character
  uint32_t Num = Val;
  Out << Num;
}

StringRef ScalarTraits<uint8_t>::input(StringRef Scalar, void *, uint8_t &Val) {
  unsigned long long n;
  if (getAsUnsignedInteger(Scalar, 0, n))
    return "invalid number";
  if (n > 0xFF)
    return "out of range number";
  Val = n;
  return StringRef();
}

void ScalarTraits<uint16_t>::output(const uint16_t &Val, void *,
                                    raw_ostream &Out) {
  Out << Val;
}

StringRef ScalarTraits<uint16_t>::input(StringRef Scalar, void *,
                                        uint16_t &Val) {
  unsigned long long n;
  if (getAsUnsignedInteger(Scalar, 0, n))
    return "invalid number";
  if (n > 0xFFFF)
    return "out of range number";
  Val = n;
  return StringRef();
}

void ScalarTraits<uint32_t>::output(const uint32_t &Val, void *,
                                    raw_ostream &Out) {
  Out << Val;
}

StringRef ScalarTraits<uint32_t>::input(StringRef Scalar, void *,
                                        uint32_t &Val) {
  unsigned long long n;
  if (getAsUnsignedInteger(Scalar, 0, n))
    return "invalid number";
  if (n > 0xFFFFFFFFUL)
    return "out of range number";
  Val = n;
  return StringRef();
}

void ScalarTraits<uint64_t>::output(const uint64_t &Val, void *,
                                    raw_ostream &Out) {
  Out << Val;
}

StringRef ScalarTraits<uint64_t>::input(StringRef Scalar, void *,
                                        uint64_t &Val) {
  unsigned long long N;
  if (getAsUnsignedInteger(Scalar, 0, N))
    return "invalid number";
  Val = N;
  return StringRef();
}

void ScalarTraits<int8_t>::output(const int8_t &Val, void *, raw_ostream &Out) {
  // use temp in32_t because ostream thinks int8_t is a character
  int32_t Num = Val;
  Out << Num;
}

StringRef ScalarTraits<int8_t>::input(StringRef Scalar, void *, int8_t &Val) {
  long long N;
  if (getAsSignedInteger(Scalar, 0, N))
    return "invalid number";
  if ((N > 127) || (N < -128))
    return "out of range number";
  Val = N;
  return StringRef();
}

void ScalarTraits<int16_t>::output(const int16_t &Val, void *,
                                   raw_ostream &Out) {
  Out << Val;
}

StringRef ScalarTraits<int16_t>::input(StringRef Scalar, void *, int16_t &Val) {
  long long N;
  if (getAsSignedInteger(Scalar, 0, N))
    return "invalid number";
  if ((N > INT16_MAX) || (N < INT16_MIN))
    return "out of range number";
  Val = N;
  return StringRef();
}

void ScalarTraits<int32_t>::output(const int32_t &Val, void *,
                                   raw_ostream &Out) {
  Out << Val;
}

StringRef ScalarTraits<int32_t>::input(StringRef Scalar, void *, int32_t &Val) {
  long long N;
  if (getAsSignedInteger(Scalar, 0, N))
    return "invalid number";
  if ((N > INT32_MAX) || (N < INT32_MIN))
    return "out of range number";
  Val = N;
  return StringRef();
}

void ScalarTraits<int64_t>::output(const int64_t &Val, void *,
                                   raw_ostream &Out) {
  Out << Val;
}

StringRef ScalarTraits<int64_t>::input(StringRef Scalar, void *, int64_t &Val) {
  long long N;
  if (getAsSignedInteger(Scalar, 0, N))
    return "invalid number";
  Val = N;
  return StringRef();
}

void ScalarTraits<double>::output(const double &Val, void *, raw_ostream &Out) {
  Out << format("%g", Val);
}

StringRef ScalarTraits<double>::input(StringRef Scalar, void *, double &Val) {
  if (to_float(Scalar, Val))
    return StringRef();
  return "invalid floating point number";
}

void ScalarTraits<float>::output(const float &Val, void *, raw_ostream &Out) {
  Out << format("%g", Val);
}

StringRef ScalarTraits<float>::input(StringRef Scalar, void *, float &Val) {
  if (to_float(Scalar, Val))
    return StringRef();
  return "invalid floating point number";
}

void ScalarTraits<Hex8>::output(const Hex8 &Val, void *, raw_ostream &Out) {
  uint8_t Num = Val;
  Out << format("0x%02X", Num);
}

StringRef ScalarTraits<Hex8>::input(StringRef Scalar, void *, Hex8 &Val) {
  unsigned long long n;
  if (getAsUnsignedInteger(Scalar, 0, n))
    return "invalid hex8 number";
  if (n > 0xFF)
    return "out of range hex8 number";
  Val = n;
  return StringRef();
}

void ScalarTraits<Hex16>::output(const Hex16 &Val, void *, raw_ostream &Out) {
  uint16_t Num = Val;
  Out << format("0x%04X", Num);
}

StringRef ScalarTraits<Hex16>::input(StringRef Scalar, void *, Hex16 &Val) {
  unsigned long long n;
  if (getAsUnsignedInteger(Scalar, 0, n))
    return "invalid hex16 number";
  if (n > 0xFFFF)
    return "out of range hex16 number";
  Val = n;
  return StringRef();
}

void ScalarTraits<Hex32>::output(const Hex32 &Val, void *, raw_ostream &Out) {
  uint32_t Num = Val;
  Out << format("0x%08X", Num);
}

StringRef ScalarTraits<Hex32>::input(StringRef Scalar, void *, Hex32 &Val) {
  unsigned long long n;
  if (getAsUnsignedInteger(Scalar, 0, n))
    return "invalid hex32 number";
  if (n > 0xFFFFFFFFUL)
    return "out of range hex32 number";
  Val = n;
  return StringRef();
}

void ScalarTraits<Hex64>::output(const Hex64 &Val, void *, raw_ostream &Out) {
  uint64_t Num = Val;
  Out << format("0x%016llX", Num);
}

StringRef ScalarTraits<Hex64>::input(StringRef Scalar, void *, Hex64 &Val) {
  unsigned long long Num;
  if (getAsUnsignedInteger(Scalar, 0, Num))
    return "invalid hex64 number";
  Val = Num;
  return StringRef();
}
