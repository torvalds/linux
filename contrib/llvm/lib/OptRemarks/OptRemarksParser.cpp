//===- OptRemarksParser.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides utility methods used by clients that want to use the
// parser for optimization remarks in LLVM.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/OptRemarks.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLTraits.h"

using namespace llvm;

namespace {
struct RemarkParser {
  /// Source manager for better error messages.
  SourceMgr SM;
  /// Stream for yaml parsing.
  yaml::Stream Stream;
  /// Storage for the error stream.
  std::string ErrorString;
  /// The error stream.
  raw_string_ostream ErrorStream;
  /// Iterator in the YAML stream.
  yaml::document_iterator DI;
  /// The parsed remark (if any).
  Optional<LLVMOptRemarkEntry> LastRemark;
  /// Temporary parsing buffer for the arguments.
  SmallVector<LLVMOptRemarkArg, 8> TmpArgs;
  /// The state used by the parser to parse a remark entry. Invalidated with
  /// every call to `parseYAMLElement`.
  struct ParseState {
    /// Temporary parsing buffer for the arguments.
    SmallVectorImpl<LLVMOptRemarkArg> *Args;
    StringRef Type;
    StringRef Pass;
    StringRef Name;
    StringRef Function;
    /// Optional.
    Optional<StringRef> File;
    Optional<unsigned> Line;
    Optional<unsigned> Column;
    Optional<unsigned> Hotness;

    ParseState(SmallVectorImpl<LLVMOptRemarkArg> &Args) : Args(&Args) {}
    /// Use Args only as a **temporary** buffer.
    ~ParseState() { Args->clear(); }
  };

  ParseState State;

  /// Set to `true` if we had any errors during parsing.
  bool HadAnyErrors = false;

  RemarkParser(StringRef Buf)
      : SM(), Stream(Buf, SM), ErrorString(), ErrorStream(ErrorString),
        DI(Stream.begin()), LastRemark(), TmpArgs(), State(TmpArgs) {
    SM.setDiagHandler(RemarkParser::HandleDiagnostic, this);
  }

  /// Parse a YAML element.
  Error parseYAMLElement(yaml::Document &Remark);

private:
  /// Parse one key to a string.
  /// otherwise.
  Error parseKey(StringRef &Result, yaml::KeyValueNode &Node);
  /// Parse one value to a string.
  Error parseValue(StringRef &Result, yaml::KeyValueNode &Node);
  /// Parse one value to an unsigned.
  Error parseValue(Optional<unsigned> &Result, yaml::KeyValueNode &Node);
  /// Parse a debug location.
  Error parseDebugLoc(Optional<StringRef> &File, Optional<unsigned> &Line,
                      Optional<unsigned> &Column, yaml::KeyValueNode &Node);
  /// Parse an argument.
  Error parseArg(SmallVectorImpl<LLVMOptRemarkArg> &TmpArgs, yaml::Node &Node);

  /// Handle a diagnostic from the YAML stream. Records the error in the
  /// RemarkParser class.
  static void HandleDiagnostic(const SMDiagnostic &Diag, void *Ctx) {
    assert(Ctx && "Expected non-null Ctx in diagnostic handler.");
    auto *Parser = static_cast<RemarkParser *>(Ctx);
    Diag.print(/*ProgName=*/nullptr, Parser->ErrorStream, /*ShowColors*/ false,
               /*ShowKindLabels*/ true);
  }
};

class ParseError : public ErrorInfo<ParseError> {
public:
  static char ID;

  ParseError(StringRef Message, yaml::Node &Node)
      : Message(Message), Node(Node) {}

  void log(raw_ostream &OS) const override { OS << Message; }
  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }

  StringRef getMessage() const { return Message; }
  yaml::Node &getNode() const { return Node; }

private:
  StringRef Message; // No need to hold a full copy of the buffer.
  yaml::Node &Node;
};

char ParseError::ID = 0;

static LLVMOptRemarkStringRef toOptRemarkStr(StringRef Str) {
  return {Str.data(), static_cast<uint32_t>(Str.size())};
}

Error RemarkParser::parseKey(StringRef &Result, yaml::KeyValueNode &Node) {
  auto *Key = dyn_cast<yaml::ScalarNode>(Node.getKey());
  if (!Key)
    return make_error<ParseError>("key is not a string.", Node);

  Result = Key->getRawValue();
  return Error::success();
}

Error RemarkParser::parseValue(StringRef &Result, yaml::KeyValueNode &Node) {
  auto *Value = dyn_cast<yaml::ScalarNode>(Node.getValue());
  if (!Value)
    return make_error<ParseError>("expected a value of scalar type.", Node);
  Result = Value->getRawValue();

  if (Result.front() == '\'')
    Result = Result.drop_front();

  if (Result.back() == '\'')
    Result = Result.drop_back();

  return Error::success();
}

Error RemarkParser::parseValue(Optional<unsigned> &Result,
                               yaml::KeyValueNode &Node) {
  SmallVector<char, 4> Tmp;
  auto *Value = dyn_cast<yaml::ScalarNode>(Node.getValue());
  if (!Value)
    return make_error<ParseError>("expected a value of scalar type.", Node);
  unsigned UnsignedValue = 0;
  if (Value->getValue(Tmp).getAsInteger(10, UnsignedValue))
    return make_error<ParseError>("expected a value of integer type.", *Value);
  Result = UnsignedValue;
  return Error::success();
}

Error RemarkParser::parseDebugLoc(Optional<StringRef> &File,
                                  Optional<unsigned> &Line,
                                  Optional<unsigned> &Column,
                                  yaml::KeyValueNode &Node) {
  auto *DebugLoc = dyn_cast<yaml::MappingNode>(Node.getValue());
  if (!DebugLoc)
    return make_error<ParseError>("expected a value of mapping type.", Node);

  for (yaml::KeyValueNode &DLNode : *DebugLoc) {
    StringRef KeyName;
    if (Error E = parseKey(KeyName, DLNode))
      return E;
    if (KeyName == "File") {
      File = StringRef(); // Set the optional to contain a default constructed
                          // value, to be passed to the parsing function.
      if (Error E = parseValue(*File, DLNode))
        return E;
    } else if (KeyName == "Column") {
      if (Error E = parseValue(Column, DLNode))
        return E;
    } else if (KeyName == "Line") {
      if (Error E = parseValue(Line, DLNode))
        return E;
    } else {
      return make_error<ParseError>("unknown entry in DebugLoc map.", DLNode);
    }
  }

  // If any of the debug loc fields is missing, return an error.
  if (!File || !Line || !Column)
    return make_error<ParseError>("DebugLoc node incomplete.", Node);

  return Error::success();
}

Error RemarkParser::parseArg(SmallVectorImpl<LLVMOptRemarkArg> &Args,
                             yaml::Node &Node) {
  auto *ArgMap = dyn_cast<yaml::MappingNode>(&Node);
  if (!ArgMap)
    return make_error<ParseError>("expected a value of mapping type.", Node);

  StringRef ValueStr;
  StringRef KeyStr;
  Optional<StringRef> File;
  Optional<unsigned> Line;
  Optional<unsigned> Column;

  for (yaml::KeyValueNode &ArgEntry : *ArgMap) {
    StringRef KeyName;
    if (Error E = parseKey(KeyName, ArgEntry))
      return E;

    // Try to parse debug locs.
    if (KeyName == "DebugLoc") {
      // Can't have multiple DebugLoc entries per argument.
      if (File || Line || Column)
        return make_error<ParseError>(
            "only one DebugLoc entry is allowed per argument.", ArgEntry);

      if (Error E = parseDebugLoc(File, Line, Column, ArgEntry))
        return E;
      continue;
    }

    // If we already have a string, error out.
    if (!ValueStr.empty())
      return make_error<ParseError>(
          "only one string entry is allowed per argument.", ArgEntry);

    // Try to parse a string.
    if (Error E = parseValue(ValueStr, ArgEntry))
      return E;

    // Keep the key from the string.
    KeyStr = KeyName;
  }

  if (KeyStr.empty())
    return make_error<ParseError>("argument key is missing.", *ArgMap);
  if (ValueStr.empty())
    return make_error<ParseError>("argument value is missing.", *ArgMap);

  Args.push_back(LLVMOptRemarkArg{
      toOptRemarkStr(KeyStr), toOptRemarkStr(ValueStr),
      LLVMOptRemarkDebugLoc{toOptRemarkStr(File.getValueOr(StringRef())),
                            Line.getValueOr(0), Column.getValueOr(0)}});

  return Error::success();
}

Error RemarkParser::parseYAMLElement(yaml::Document &Remark) {
  // Parsing a new remark, clear the previous one.
  LastRemark = None;
  State = ParseState(TmpArgs);

  auto *Root = dyn_cast<yaml::MappingNode>(Remark.getRoot());
  if (!Root)
    return make_error<ParseError>("document root is not of mapping type.",
                                  *Remark.getRoot());

  State.Type = Root->getRawTag();

  for (yaml::KeyValueNode &RemarkField : *Root) {
    StringRef KeyName;
    if (Error E = parseKey(KeyName, RemarkField))
      return E;

    if (KeyName == "Pass") {
      if (Error E = parseValue(State.Pass, RemarkField))
        return E;
    } else if (KeyName == "Name") {
      if (Error E = parseValue(State.Name, RemarkField))
        return E;
    } else if (KeyName == "Function") {
      if (Error E = parseValue(State.Function, RemarkField))
        return E;
    } else if (KeyName == "Hotness") {
      if (Error E = parseValue(State.Hotness, RemarkField))
        return E;
    } else if (KeyName == "DebugLoc") {
      if (Error E =
              parseDebugLoc(State.File, State.Line, State.Column, RemarkField))
        return E;
    } else if (KeyName == "Args") {
      auto *Args = dyn_cast<yaml::SequenceNode>(RemarkField.getValue());
      if (!Args)
        return make_error<ParseError>("wrong value type for key.", RemarkField);

      for (yaml::Node &Arg : *Args)
        if (Error E = parseArg(*State.Args, Arg))
          return E;
    } else {
      return make_error<ParseError>("unknown key.", RemarkField);
    }
  }

  // If the YAML parsing failed, don't even continue parsing. We might
  // encounter malformed YAML.
  if (Stream.failed())
    return make_error<ParseError>("YAML parsing failed.", *Remark.getRoot());

  // Check if any of the mandatory fields are missing.
  if (State.Type.empty() || State.Pass.empty() || State.Name.empty() ||
      State.Function.empty())
    return make_error<ParseError>("Type, Pass, Name or Function missing.",
                                  *Remark.getRoot());

  LastRemark = LLVMOptRemarkEntry{
      toOptRemarkStr(State.Type),
      toOptRemarkStr(State.Pass),
      toOptRemarkStr(State.Name),
      toOptRemarkStr(State.Function),
      LLVMOptRemarkDebugLoc{toOptRemarkStr(State.File.getValueOr(StringRef())),
                            State.Line.getValueOr(0),
                            State.Column.getValueOr(0)},
      State.Hotness.getValueOr(0),
      static_cast<uint32_t>(State.Args->size()),
      State.Args->data()};

  return Error::success();
}
} // namespace

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(RemarkParser, LLVMOptRemarkParserRef)

extern "C" LLVMOptRemarkParserRef LLVMOptRemarkParserCreate(const void *Buf,
                                                            uint64_t Size) {
  return wrap(
      new RemarkParser(StringRef(static_cast<const char *>(Buf), Size)));
}

extern "C" LLVMOptRemarkEntry *
LLVMOptRemarkParserGetNext(LLVMOptRemarkParserRef Parser) {
  RemarkParser &TheParser = *unwrap(Parser);
  // Check for EOF.
  if (TheParser.HadAnyErrors || TheParser.DI == TheParser.Stream.end())
    return nullptr;

  // Try to parse an entry.
  if (Error E = TheParser.parseYAMLElement(*TheParser.DI)) {
    handleAllErrors(std::move(E), [&](const ParseError &PE) {
      TheParser.Stream.printError(&PE.getNode(),
                                  Twine(PE.getMessage()) + Twine('\n'));
      TheParser.HadAnyErrors = true;
    });
    return nullptr;
  }

  // Move on.
  ++TheParser.DI;

  // Return the just-parsed remark.
  if (Optional<LLVMOptRemarkEntry> &Entry = TheParser.LastRemark)
    return &*Entry;
  return nullptr;
}

extern "C" LLVMBool LLVMOptRemarkParserHasError(LLVMOptRemarkParserRef Parser) {
  return unwrap(Parser)->HadAnyErrors;
}

extern "C" const char *
LLVMOptRemarkParserGetErrorMessage(LLVMOptRemarkParserRef Parser) {
  return unwrap(Parser)->ErrorStream.str().c_str();
}

extern "C" void LLVMOptRemarkParserDispose(LLVMOptRemarkParserRef Parser) {
  delete unwrap(Parser);
}
