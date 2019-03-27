//===- DAGISelMatcherEmitter.cpp - Matcher Emitter ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains code to generate C++ code for a matcher.
//
//===----------------------------------------------------------------------===//

#include "CodeGenDAGPatterns.h"
#include "DAGISelMatcher.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
using namespace llvm;

enum {
  IndexWidth = 6,
  FullIndexWidth = IndexWidth + 4,
  HistOpcWidth = 40,
};

cl::OptionCategory DAGISelCat("Options for -gen-dag-isel");

// To reduce generated source code size.
static cl::opt<bool> OmitComments("omit-comments",
                                  cl::desc("Do not generate comments"),
                                  cl::init(false), cl::cat(DAGISelCat));

static cl::opt<bool> InstrumentCoverage(
    "instrument-coverage",
    cl::desc("Generates tables to help identify patterns matched"),
    cl::init(false), cl::cat(DAGISelCat));

namespace {
class MatcherTableEmitter {
  const CodeGenDAGPatterns &CGP;

  DenseMap<TreePattern *, unsigned> NodePredicateMap;
  std::vector<TreePredicateFn> NodePredicates;
  std::vector<TreePredicateFn> NodePredicatesWithOperands;

  // We de-duplicate the predicates by code string, and use this map to track
  // all the patterns with "identical" predicates.
  StringMap<TinyPtrVector<TreePattern *>> NodePredicatesByCodeToRun;

  StringMap<unsigned> PatternPredicateMap;
  std::vector<std::string> PatternPredicates;

  DenseMap<const ComplexPattern*, unsigned> ComplexPatternMap;
  std::vector<const ComplexPattern*> ComplexPatterns;


  DenseMap<Record*, unsigned> NodeXFormMap;
  std::vector<Record*> NodeXForms;

  std::vector<std::string> VecIncludeStrings;
  MapVector<std::string, unsigned, StringMap<unsigned> > VecPatterns;

  unsigned getPatternIdxFromTable(std::string &&P, std::string &&include_loc) {
    const auto It = VecPatterns.find(P);
    if (It == VecPatterns.end()) {
      VecPatterns.insert(make_pair(std::move(P), VecPatterns.size()));
      VecIncludeStrings.push_back(std::move(include_loc));
      return VecIncludeStrings.size() - 1;
    }
    return It->second;
  }

public:
  MatcherTableEmitter(const CodeGenDAGPatterns &cgp)
    : CGP(cgp) {}

  unsigned EmitMatcherList(const Matcher *N, unsigned Indent,
                           unsigned StartIdx, raw_ostream &OS);

  void EmitPredicateFunctions(raw_ostream &OS);

  void EmitHistogram(const Matcher *N, raw_ostream &OS);

  void EmitPatternMatchTable(raw_ostream &OS);

private:
  void EmitNodePredicatesFunction(const std::vector<TreePredicateFn> &Preds,
                                  StringRef Decl, raw_ostream &OS);

  unsigned EmitMatcher(const Matcher *N, unsigned Indent, unsigned CurrentIdx,
                       raw_ostream &OS);

  unsigned getNodePredicate(TreePredicateFn Pred) {
    TreePattern *TP = Pred.getOrigPatFragRecord();
    unsigned &Entry = NodePredicateMap[TP];
    if (Entry == 0) {
      TinyPtrVector<TreePattern *> &SameCodePreds =
          NodePredicatesByCodeToRun[Pred.getCodeToRunOnSDNode()];
      if (SameCodePreds.empty()) {
        // We've never seen a predicate with the same code: allocate an entry.
        if (Pred.usesOperands()) {
          NodePredicatesWithOperands.push_back(Pred);
          Entry = NodePredicatesWithOperands.size();
        } else {
          NodePredicates.push_back(Pred);
          Entry = NodePredicates.size();
        }
      } else {
        // We did see an identical predicate: re-use it.
        Entry = NodePredicateMap[SameCodePreds.front()];
        assert(Entry != 0);
        assert(TreePredicateFn(SameCodePreds.front()).usesOperands() ==
               Pred.usesOperands() &&
               "PatFrags with some code must have same usesOperands setting");
      }
      // In both cases, we've never seen this particular predicate before, so
      // mark it in the list of predicates sharing the same code.
      SameCodePreds.push_back(TP);
    }
    return Entry-1;
  }

  unsigned getPatternPredicate(StringRef PredName) {
    unsigned &Entry = PatternPredicateMap[PredName];
    if (Entry == 0) {
      PatternPredicates.push_back(PredName.str());
      Entry = PatternPredicates.size();
    }
    return Entry-1;
  }
  unsigned getComplexPat(const ComplexPattern &P) {
    unsigned &Entry = ComplexPatternMap[&P];
    if (Entry == 0) {
      ComplexPatterns.push_back(&P);
      Entry = ComplexPatterns.size();
    }
    return Entry-1;
  }

  unsigned getNodeXFormID(Record *Rec) {
    unsigned &Entry = NodeXFormMap[Rec];
    if (Entry == 0) {
      NodeXForms.push_back(Rec);
      Entry = NodeXForms.size();
    }
    return Entry-1;
  }

};
} // end anonymous namespace.

static std::string GetPatFromTreePatternNode(const TreePatternNode *N) {
  std::string str;
  raw_string_ostream Stream(str);
  Stream << *N;
  Stream.str();
  return str;
}

static unsigned GetVBRSize(unsigned Val) {
  if (Val <= 127) return 1;

  unsigned NumBytes = 0;
  while (Val >= 128) {
    Val >>= 7;
    ++NumBytes;
  }
  return NumBytes+1;
}

/// EmitVBRValue - Emit the specified value as a VBR, returning the number of
/// bytes emitted.
static uint64_t EmitVBRValue(uint64_t Val, raw_ostream &OS) {
  if (Val <= 127) {
    OS << Val << ", ";
    return 1;
  }

  uint64_t InVal = Val;
  unsigned NumBytes = 0;
  while (Val >= 128) {
    OS << (Val&127) << "|128,";
    Val >>= 7;
    ++NumBytes;
  }
  OS << Val;
  if (!OmitComments)
    OS << "/*" << InVal << "*/";
  OS << ", ";
  return NumBytes+1;
}

// This is expensive and slow.
static std::string getIncludePath(const Record *R) {
  std::string str;
  raw_string_ostream Stream(str);
  auto Locs = R->getLoc();
  SMLoc L;
  if (Locs.size() > 1) {
    // Get where the pattern prototype was instantiated
    L = Locs[1];
  } else if (Locs.size() == 1) {
    L = Locs[0];
  }
  unsigned CurBuf = SrcMgr.FindBufferContainingLoc(L);
  assert(CurBuf && "Invalid or unspecified location!");

  Stream << SrcMgr.getBufferInfo(CurBuf).Buffer->getBufferIdentifier() << ":"
         << SrcMgr.FindLineNumber(L, CurBuf);
  Stream.str();
  return str;
}

static void BeginEmitFunction(raw_ostream &OS, StringRef RetType,
                              StringRef Decl, bool AddOverride) {
  OS << "#ifdef GET_DAGISEL_DECL\n";
  OS << RetType << ' ' << Decl;
  if (AddOverride)
    OS << " override";
  OS << ";\n"
        "#endif\n"
        "#if defined(GET_DAGISEL_BODY) || DAGISEL_INLINE\n";
  OS << RetType << " DAGISEL_CLASS_COLONCOLON " << Decl << "\n";
  if (AddOverride) {
    OS << "#if DAGISEL_INLINE\n"
          "  override\n"
          "#endif\n";
  }
}

static void EndEmitFunction(raw_ostream &OS) {
  OS << "#endif // GET_DAGISEL_BODY\n\n";
}

void MatcherTableEmitter::EmitPatternMatchTable(raw_ostream &OS) {

  assert(isUInt<16>(VecPatterns.size()) &&
         "Using only 16 bits to encode offset into Pattern Table");
  assert(VecPatterns.size() == VecIncludeStrings.size() &&
         "The sizes of Pattern and include vectors should be the same");

  BeginEmitFunction(OS, "StringRef", "getPatternForIndex(unsigned Index)",
                    true/*AddOverride*/);
  OS << "{\n";
  OS << "static const char * PATTERN_MATCH_TABLE[] = {\n";

  for (const auto &It : VecPatterns) {
    OS << "\"" << It.first << "\",\n";
  }

  OS << "\n};";
  OS << "\nreturn StringRef(PATTERN_MATCH_TABLE[Index]);";
  OS << "\n}";
  EndEmitFunction(OS);

  BeginEmitFunction(OS, "StringRef", "getIncludePathForIndex(unsigned Index)",
                    true/*AddOverride*/);
  OS << "{\n";
  OS << "static const char * INCLUDE_PATH_TABLE[] = {\n";

  for (const auto &It : VecIncludeStrings) {
    OS << "\"" << It << "\",\n";
  }

  OS << "\n};";
  OS << "\nreturn StringRef(INCLUDE_PATH_TABLE[Index]);";
  OS << "\n}";
  EndEmitFunction(OS);
}

/// EmitMatcher - Emit bytes for the specified matcher and return
/// the number of bytes emitted.
unsigned MatcherTableEmitter::
EmitMatcher(const Matcher *N, unsigned Indent, unsigned CurrentIdx,
            raw_ostream &OS) {
  OS.indent(Indent*2);

  switch (N->getKind()) {
  case Matcher::Scope: {
    const ScopeMatcher *SM = cast<ScopeMatcher>(N);
    assert(SM->getNext() == nullptr && "Shouldn't have next after scope");

    unsigned StartIdx = CurrentIdx;

    // Emit all of the children.
    for (unsigned i = 0, e = SM->getNumChildren(); i != e; ++i) {
      if (i == 0) {
        OS << "OPC_Scope, ";
        ++CurrentIdx;
      } else  {
        if (!OmitComments) {
          OS << "/*" << format_decimal(CurrentIdx, IndexWidth) << "*/";
          OS.indent(Indent*2) << "/*Scope*/ ";
        } else
          OS.indent(Indent*2);
      }

      // We need to encode the child and the offset of the failure code before
      // emitting either of them.  Handle this by buffering the output into a
      // string while we get the size.  Unfortunately, the offset of the
      // children depends on the VBR size of the child, so for large children we
      // have to iterate a bit.
      SmallString<128> TmpBuf;
      unsigned ChildSize = 0;
      unsigned VBRSize = 0;
      do {
        VBRSize = GetVBRSize(ChildSize);

        TmpBuf.clear();
        raw_svector_ostream OS(TmpBuf);
        ChildSize = EmitMatcherList(SM->getChild(i), Indent+1,
                                    CurrentIdx+VBRSize, OS);
      } while (GetVBRSize(ChildSize) != VBRSize);

      assert(ChildSize != 0 && "Should not have a zero-sized child!");

      CurrentIdx += EmitVBRValue(ChildSize, OS);
      if (!OmitComments) {
        OS << "/*->" << CurrentIdx+ChildSize << "*/";

        if (i == 0)
          OS << " // " << SM->getNumChildren() << " children in Scope";
      }

      OS << '\n' << TmpBuf;
      CurrentIdx += ChildSize;
    }

    // Emit a zero as a sentinel indicating end of 'Scope'.
    if (!OmitComments)
      OS << "/*" << format_decimal(CurrentIdx, IndexWidth) << "*/";
    OS.indent(Indent*2) << "0, ";
    if (!OmitComments)
      OS << "/*End of Scope*/";
    OS << '\n';
    return CurrentIdx - StartIdx + 1;
  }

  case Matcher::RecordNode:
    OS << "OPC_RecordNode,";
    if (!OmitComments)
      OS << " // #"
         << cast<RecordMatcher>(N)->getResultNo() << " = "
         << cast<RecordMatcher>(N)->getWhatFor();
    OS << '\n';
    return 1;

  case Matcher::RecordChild:
    OS << "OPC_RecordChild" << cast<RecordChildMatcher>(N)->getChildNo()
       << ',';
    if (!OmitComments)
      OS << " // #"
         << cast<RecordChildMatcher>(N)->getResultNo() << " = "
         << cast<RecordChildMatcher>(N)->getWhatFor();
    OS << '\n';
    return 1;

  case Matcher::RecordMemRef:
    OS << "OPC_RecordMemRef,\n";
    return 1;

  case Matcher::CaptureGlueInput:
    OS << "OPC_CaptureGlueInput,\n";
    return 1;

  case Matcher::MoveChild: {
    const auto *MCM = cast<MoveChildMatcher>(N);

    OS << "OPC_MoveChild";
    // Handle the specialized forms.
    if (MCM->getChildNo() >= 8)
      OS << ", ";
    OS << MCM->getChildNo() << ",\n";
    return (MCM->getChildNo() >= 8) ? 2 : 1;
  }

  case Matcher::MoveParent:
    OS << "OPC_MoveParent,\n";
    return 1;

  case Matcher::CheckSame:
    OS << "OPC_CheckSame, "
       << cast<CheckSameMatcher>(N)->getMatchNumber() << ",\n";
    return 2;

  case Matcher::CheckChildSame:
    OS << "OPC_CheckChild"
       << cast<CheckChildSameMatcher>(N)->getChildNo() << "Same, "
       << cast<CheckChildSameMatcher>(N)->getMatchNumber() << ",\n";
    return 2;

  case Matcher::CheckPatternPredicate: {
    StringRef Pred =cast<CheckPatternPredicateMatcher>(N)->getPredicate();
    OS << "OPC_CheckPatternPredicate, " << getPatternPredicate(Pred) << ',';
    if (!OmitComments)
      OS << " // " << Pred;
    OS << '\n';
    return 2;
  }
  case Matcher::CheckPredicate: {
    TreePredicateFn Pred = cast<CheckPredicateMatcher>(N)->getPredicate();
    unsigned OperandBytes = 0;

    if (Pred.usesOperands()) {
      unsigned NumOps = cast<CheckPredicateMatcher>(N)->getNumOperands();
      OS << "OPC_CheckPredicateWithOperands, " << NumOps << "/*#Ops*/, ";
      for (unsigned i = 0; i < NumOps; ++i)
        OS << cast<CheckPredicateMatcher>(N)->getOperandNo(i) << ", ";
      OperandBytes = 1 + NumOps;
    } else {
      OS << "OPC_CheckPredicate, ";
    }

    OS << getNodePredicate(Pred) << ',';
    if (!OmitComments)
      OS << " // " << Pred.getFnName();
    OS << '\n';
    return 2 + OperandBytes;
  }

  case Matcher::CheckOpcode:
    OS << "OPC_CheckOpcode, TARGET_VAL("
       << cast<CheckOpcodeMatcher>(N)->getOpcode().getEnumName() << "),\n";
    return 3;

  case Matcher::SwitchOpcode:
  case Matcher::SwitchType: {
    unsigned StartIdx = CurrentIdx;

    unsigned NumCases;
    if (const SwitchOpcodeMatcher *SOM = dyn_cast<SwitchOpcodeMatcher>(N)) {
      OS << "OPC_SwitchOpcode ";
      NumCases = SOM->getNumCases();
    } else {
      OS << "OPC_SwitchType ";
      NumCases = cast<SwitchTypeMatcher>(N)->getNumCases();
    }

    if (!OmitComments)
      OS << "/*" << NumCases << " cases */";
    OS << ", ";
    ++CurrentIdx;

    // For each case we emit the size, then the opcode, then the matcher.
    for (unsigned i = 0, e = NumCases; i != e; ++i) {
      const Matcher *Child;
      unsigned IdxSize;
      if (const SwitchOpcodeMatcher *SOM = dyn_cast<SwitchOpcodeMatcher>(N)) {
        Child = SOM->getCaseMatcher(i);
        IdxSize = 2;  // size of opcode in table is 2 bytes.
      } else {
        Child = cast<SwitchTypeMatcher>(N)->getCaseMatcher(i);
        IdxSize = 1;  // size of type in table is 1 byte.
      }

      // We need to encode the opcode and the offset of the case code before
      // emitting the case code.  Handle this by buffering the output into a
      // string while we get the size.  Unfortunately, the offset of the
      // children depends on the VBR size of the child, so for large children we
      // have to iterate a bit.
      SmallString<128> TmpBuf;
      unsigned ChildSize = 0;
      unsigned VBRSize = 0;
      do {
        VBRSize = GetVBRSize(ChildSize);

        TmpBuf.clear();
        raw_svector_ostream OS(TmpBuf);
        ChildSize = EmitMatcherList(Child, Indent+1, CurrentIdx+VBRSize+IdxSize,
                                    OS);
      } while (GetVBRSize(ChildSize) != VBRSize);

      assert(ChildSize != 0 && "Should not have a zero-sized child!");

      if (i != 0) {
        if (!OmitComments)
          OS << "/*" << format_decimal(CurrentIdx, IndexWidth) << "*/";
        OS.indent(Indent*2);
        if (!OmitComments)
          OS << (isa<SwitchOpcodeMatcher>(N) ?
                     "/*SwitchOpcode*/ " : "/*SwitchType*/ ");
      }

      // Emit the VBR.
      CurrentIdx += EmitVBRValue(ChildSize, OS);

      if (const SwitchOpcodeMatcher *SOM = dyn_cast<SwitchOpcodeMatcher>(N))
        OS << "TARGET_VAL(" << SOM->getCaseOpcode(i).getEnumName() << "),";
      else
        OS << getEnumName(cast<SwitchTypeMatcher>(N)->getCaseType(i)) << ',';

      CurrentIdx += IdxSize;

      if (!OmitComments)
        OS << "// ->" << CurrentIdx+ChildSize;
      OS << '\n';
      OS << TmpBuf;
      CurrentIdx += ChildSize;
    }

    // Emit the final zero to terminate the switch.
    if (!OmitComments)
      OS << "/*" << format_decimal(CurrentIdx, IndexWidth) << "*/";
    OS.indent(Indent*2) << "0,";
    if (!OmitComments)
      OS << (isa<SwitchOpcodeMatcher>(N) ?
             " // EndSwitchOpcode" : " // EndSwitchType");

    OS << '\n';
    ++CurrentIdx;
    return CurrentIdx-StartIdx;
  }

 case Matcher::CheckType:
    if (cast<CheckTypeMatcher>(N)->getResNo() == 0) {
      OS << "OPC_CheckType, "
         << getEnumName(cast<CheckTypeMatcher>(N)->getType()) << ",\n";
      return 2;
    }
    OS << "OPC_CheckTypeRes, " << cast<CheckTypeMatcher>(N)->getResNo()
       << ", " << getEnumName(cast<CheckTypeMatcher>(N)->getType()) << ",\n";
    return 3;

  case Matcher::CheckChildType:
    OS << "OPC_CheckChild"
       << cast<CheckChildTypeMatcher>(N)->getChildNo() << "Type, "
       << getEnumName(cast<CheckChildTypeMatcher>(N)->getType()) << ",\n";
    return 2;

  case Matcher::CheckInteger: {
    OS << "OPC_CheckInteger, ";
    unsigned Bytes=1+EmitVBRValue(cast<CheckIntegerMatcher>(N)->getValue(), OS);
    OS << '\n';
    return Bytes;
  }
  case Matcher::CheckChildInteger: {
    OS << "OPC_CheckChild" << cast<CheckChildIntegerMatcher>(N)->getChildNo()
       << "Integer, ";
    unsigned Bytes=1+EmitVBRValue(cast<CheckChildIntegerMatcher>(N)->getValue(),
                                  OS);
    OS << '\n';
    return Bytes;
  }
  case Matcher::CheckCondCode:
    OS << "OPC_CheckCondCode, ISD::"
       << cast<CheckCondCodeMatcher>(N)->getCondCodeName() << ",\n";
    return 2;

  case Matcher::CheckValueType:
    OS << "OPC_CheckValueType, MVT::"
       << cast<CheckValueTypeMatcher>(N)->getTypeName() << ",\n";
    return 2;

  case Matcher::CheckComplexPat: {
    const CheckComplexPatMatcher *CCPM = cast<CheckComplexPatMatcher>(N);
    const ComplexPattern &Pattern = CCPM->getPattern();
    OS << "OPC_CheckComplexPat, /*CP*/" << getComplexPat(Pattern) << ", /*#*/"
       << CCPM->getMatchNumber() << ',';

    if (!OmitComments) {
      OS << " // " << Pattern.getSelectFunc();
      OS << ":$" << CCPM->getName();
      for (unsigned i = 0, e = Pattern.getNumOperands(); i != e; ++i)
        OS << " #" << CCPM->getFirstResult()+i;

      if (Pattern.hasProperty(SDNPHasChain))
        OS << " + chain result";
    }
    OS << '\n';
    return 3;
  }

  case Matcher::CheckAndImm: {
    OS << "OPC_CheckAndImm, ";
    unsigned Bytes=1+EmitVBRValue(cast<CheckAndImmMatcher>(N)->getValue(), OS);
    OS << '\n';
    return Bytes;
  }

  case Matcher::CheckOrImm: {
    OS << "OPC_CheckOrImm, ";
    unsigned Bytes = 1+EmitVBRValue(cast<CheckOrImmMatcher>(N)->getValue(), OS);
    OS << '\n';
    return Bytes;
  }

  case Matcher::CheckFoldableChainNode:
    OS << "OPC_CheckFoldableChainNode,\n";
    return 1;

  case Matcher::EmitInteger: {
    int64_t Val = cast<EmitIntegerMatcher>(N)->getValue();
    OS << "OPC_EmitInteger, "
       << getEnumName(cast<EmitIntegerMatcher>(N)->getVT()) << ", ";
    unsigned Bytes = 2+EmitVBRValue(Val, OS);
    OS << '\n';
    return Bytes;
  }
  case Matcher::EmitStringInteger: {
    const std::string &Val = cast<EmitStringIntegerMatcher>(N)->getValue();
    // These should always fit into one byte.
    OS << "OPC_EmitInteger, "
      << getEnumName(cast<EmitStringIntegerMatcher>(N)->getVT()) << ", "
      << Val << ",\n";
    return 3;
  }

  case Matcher::EmitRegister: {
    const EmitRegisterMatcher *Matcher = cast<EmitRegisterMatcher>(N);
    const CodeGenRegister *Reg = Matcher->getReg();
    // If the enum value of the register is larger than one byte can handle,
    // use EmitRegister2.
    if (Reg && Reg->EnumValue > 255) {
      OS << "OPC_EmitRegister2, " << getEnumName(Matcher->getVT()) << ", ";
      OS << "TARGET_VAL(" << getQualifiedName(Reg->TheDef) << "),\n";
      return 4;
    } else {
      OS << "OPC_EmitRegister, " << getEnumName(Matcher->getVT()) << ", ";
      if (Reg) {
        OS << getQualifiedName(Reg->TheDef) << ",\n";
      } else {
        OS << "0 ";
        if (!OmitComments)
          OS << "/*zero_reg*/";
        OS << ",\n";
      }
      return 3;
    }
  }

  case Matcher::EmitConvertToTarget:
    OS << "OPC_EmitConvertToTarget, "
       << cast<EmitConvertToTargetMatcher>(N)->getSlot() << ",\n";
    return 2;

  case Matcher::EmitMergeInputChains: {
    const EmitMergeInputChainsMatcher *MN =
      cast<EmitMergeInputChainsMatcher>(N);

    // Handle the specialized forms OPC_EmitMergeInputChains1_0, 1_1, and 1_2.
    if (MN->getNumNodes() == 1 && MN->getNode(0) < 3) {
      OS << "OPC_EmitMergeInputChains1_" << MN->getNode(0) << ",\n";
      return 1;
    }

    OS << "OPC_EmitMergeInputChains, " << MN->getNumNodes() << ", ";
    for (unsigned i = 0, e = MN->getNumNodes(); i != e; ++i)
      OS << MN->getNode(i) << ", ";
    OS << '\n';
    return 2+MN->getNumNodes();
  }
  case Matcher::EmitCopyToReg:
    OS << "OPC_EmitCopyToReg, "
       << cast<EmitCopyToRegMatcher>(N)->getSrcSlot() << ", "
       << getQualifiedName(cast<EmitCopyToRegMatcher>(N)->getDestPhysReg())
       << ",\n";
    return 3;
  case Matcher::EmitNodeXForm: {
    const EmitNodeXFormMatcher *XF = cast<EmitNodeXFormMatcher>(N);
    OS << "OPC_EmitNodeXForm, " << getNodeXFormID(XF->getNodeXForm()) << ", "
       << XF->getSlot() << ',';
    if (!OmitComments)
      OS << " // "<<XF->getNodeXForm()->getName();
    OS <<'\n';
    return 3;
  }

  case Matcher::EmitNode:
  case Matcher::MorphNodeTo: {
    auto NumCoveredBytes = 0;
    if (InstrumentCoverage) {
      if (const MorphNodeToMatcher *SNT = dyn_cast<MorphNodeToMatcher>(N)) {
        NumCoveredBytes = 3;
        OS << "OPC_Coverage, ";
        std::string src =
            GetPatFromTreePatternNode(SNT->getPattern().getSrcPattern());
        std::string dst =
            GetPatFromTreePatternNode(SNT->getPattern().getDstPattern());
        Record *PatRecord = SNT->getPattern().getSrcRecord();
        std::string include_src = getIncludePath(PatRecord);
        unsigned Offset =
            getPatternIdxFromTable(src + " -> " + dst, std::move(include_src));
        OS << "TARGET_VAL(" << Offset << "),\n";
        OS.indent(FullIndexWidth + Indent * 2);
      }
    }
    const EmitNodeMatcherCommon *EN = cast<EmitNodeMatcherCommon>(N);
    OS << (isa<EmitNodeMatcher>(EN) ? "OPC_EmitNode" : "OPC_MorphNodeTo");
    bool CompressVTs = EN->getNumVTs() < 3;
    if (CompressVTs)
      OS << EN->getNumVTs();

    OS << ", TARGET_VAL(" << EN->getOpcodeName() << "), 0";

    if (EN->hasChain())   OS << "|OPFL_Chain";
    if (EN->hasInFlag())  OS << "|OPFL_GlueInput";
    if (EN->hasOutFlag()) OS << "|OPFL_GlueOutput";
    if (EN->hasMemRefs()) OS << "|OPFL_MemRefs";
    if (EN->getNumFixedArityOperands() != -1)
      OS << "|OPFL_Variadic" << EN->getNumFixedArityOperands();
    OS << ",\n";

    OS.indent(FullIndexWidth + Indent*2+4);
    if (!CompressVTs) {
      OS << EN->getNumVTs();
      if (!OmitComments)
        OS << "/*#VTs*/";
      OS << ", ";
    }
    for (unsigned i = 0, e = EN->getNumVTs(); i != e; ++i)
      OS << getEnumName(EN->getVT(i)) << ", ";

    OS << EN->getNumOperands();
    if (!OmitComments)
      OS << "/*#Ops*/";
    OS << ", ";
    unsigned NumOperandBytes = 0;
    for (unsigned i = 0, e = EN->getNumOperands(); i != e; ++i)
      NumOperandBytes += EmitVBRValue(EN->getOperand(i), OS);

    if (!OmitComments) {
      // Print the result #'s for EmitNode.
      if (const EmitNodeMatcher *E = dyn_cast<EmitNodeMatcher>(EN)) {
        if (unsigned NumResults = EN->getNumVTs()) {
          OS << " // Results =";
          unsigned First = E->getFirstResultSlot();
          for (unsigned i = 0; i != NumResults; ++i)
            OS << " #" << First+i;
        }
      }
      OS << '\n';

      if (const MorphNodeToMatcher *SNT = dyn_cast<MorphNodeToMatcher>(N)) {
        OS.indent(FullIndexWidth + Indent*2) << "// Src: "
          << *SNT->getPattern().getSrcPattern() << " - Complexity = "
          << SNT->getPattern().getPatternComplexity(CGP) << '\n';
        OS.indent(FullIndexWidth + Indent*2) << "// Dst: "
          << *SNT->getPattern().getDstPattern() << '\n';
      }
    } else
      OS << '\n';

    return 5 + !CompressVTs + EN->getNumVTs() + NumOperandBytes +
           NumCoveredBytes;
  }
  case Matcher::CompleteMatch: {
    const CompleteMatchMatcher *CM = cast<CompleteMatchMatcher>(N);
    auto NumCoveredBytes = 0;
    if (InstrumentCoverage) {
      NumCoveredBytes = 3;
      OS << "OPC_Coverage, ";
      std::string src =
          GetPatFromTreePatternNode(CM->getPattern().getSrcPattern());
      std::string dst =
          GetPatFromTreePatternNode(CM->getPattern().getDstPattern());
      Record *PatRecord = CM->getPattern().getSrcRecord();
      std::string include_src = getIncludePath(PatRecord);
      unsigned Offset =
          getPatternIdxFromTable(src + " -> " + dst, std::move(include_src));
      OS << "TARGET_VAL(" << Offset << "),\n";
      OS.indent(FullIndexWidth + Indent * 2);
    }
    OS << "OPC_CompleteMatch, " << CM->getNumResults() << ", ";
    unsigned NumResultBytes = 0;
    for (unsigned i = 0, e = CM->getNumResults(); i != e; ++i)
      NumResultBytes += EmitVBRValue(CM->getResult(i), OS);
    OS << '\n';
    if (!OmitComments) {
      OS.indent(FullIndexWidth + Indent*2) << " // Src: "
        << *CM->getPattern().getSrcPattern() << " - Complexity = "
        << CM->getPattern().getPatternComplexity(CGP) << '\n';
      OS.indent(FullIndexWidth + Indent*2) << " // Dst: "
        << *CM->getPattern().getDstPattern();
    }
    OS << '\n';
    return 2 + NumResultBytes + NumCoveredBytes;
  }
  }
  llvm_unreachable("Unreachable");
}

/// EmitMatcherList - Emit the bytes for the specified matcher subtree.
unsigned MatcherTableEmitter::
EmitMatcherList(const Matcher *N, unsigned Indent, unsigned CurrentIdx,
                raw_ostream &OS) {
  unsigned Size = 0;
  while (N) {
    if (!OmitComments)
      OS << "/*" << format_decimal(CurrentIdx, IndexWidth) << "*/";
    unsigned MatcherSize = EmitMatcher(N, Indent, CurrentIdx, OS);
    Size += MatcherSize;
    CurrentIdx += MatcherSize;

    // If there are other nodes in this list, iterate to them, otherwise we're
    // done.
    N = N->getNext();
  }
  return Size;
}

void MatcherTableEmitter::EmitNodePredicatesFunction(
    const std::vector<TreePredicateFn> &Preds, StringRef Decl,
    raw_ostream &OS) {
  if (Preds.empty())
    return;

  BeginEmitFunction(OS, "bool", Decl, true/*AddOverride*/);
  OS << "{\n";
  OS << "  switch (PredNo) {\n";
  OS << "  default: llvm_unreachable(\"Invalid predicate in table?\");\n";
  for (unsigned i = 0, e = Preds.size(); i != e; ++i) {
    // Emit the predicate code corresponding to this pattern.
    TreePredicateFn PredFn = Preds[i];

    assert(!PredFn.isAlwaysTrue() && "No code in this predicate");
    OS << "  case " << i << ": { \n";
    for (auto *SimilarPred :
          NodePredicatesByCodeToRun[PredFn.getCodeToRunOnSDNode()])
      OS << "    // " << TreePredicateFn(SimilarPred).getFnName() <<'\n';

    OS << PredFn.getCodeToRunOnSDNode() << "\n  }\n";
  }
  OS << "  }\n";
  OS << "}\n";
  EndEmitFunction(OS);
}

void MatcherTableEmitter::EmitPredicateFunctions(raw_ostream &OS) {
  // Emit pattern predicates.
  if (!PatternPredicates.empty()) {
    BeginEmitFunction(OS, "bool",
          "CheckPatternPredicate(unsigned PredNo) const", true/*AddOverride*/);
    OS << "{\n";
    OS << "  switch (PredNo) {\n";
    OS << "  default: llvm_unreachable(\"Invalid predicate in table?\");\n";
    for (unsigned i = 0, e = PatternPredicates.size(); i != e; ++i)
      OS << "  case " << i << ": return "  << PatternPredicates[i] << ";\n";
    OS << "  }\n";
    OS << "}\n";
    EndEmitFunction(OS);
  }

  // Emit Node predicates.
  EmitNodePredicatesFunction(
      NodePredicates, "CheckNodePredicate(SDNode *Node, unsigned PredNo) const",
      OS);
  EmitNodePredicatesFunction(
      NodePredicatesWithOperands,
      "CheckNodePredicateWithOperands(SDNode *Node, unsigned PredNo, "
      "const SmallVectorImpl<SDValue> &Operands) const",
      OS);

  // Emit CompletePattern matchers.
  // FIXME: This should be const.
  if (!ComplexPatterns.empty()) {
    BeginEmitFunction(OS, "bool",
          "CheckComplexPattern(SDNode *Root, SDNode *Parent,\n"
          "      SDValue N, unsigned PatternNo,\n"
          "      SmallVectorImpl<std::pair<SDValue, SDNode*>> &Result)",
          true/*AddOverride*/);
    OS << "{\n";
    OS << "  unsigned NextRes = Result.size();\n";
    OS << "  switch (PatternNo) {\n";
    OS << "  default: llvm_unreachable(\"Invalid pattern # in table?\");\n";
    for (unsigned i = 0, e = ComplexPatterns.size(); i != e; ++i) {
      const ComplexPattern &P = *ComplexPatterns[i];
      unsigned NumOps = P.getNumOperands();

      if (P.hasProperty(SDNPHasChain))
        ++NumOps;  // Get the chained node too.

      OS << "  case " << i << ":\n";
      if (InstrumentCoverage)
        OS << "  {\n";
      OS << "    Result.resize(NextRes+" << NumOps << ");\n";
      if (InstrumentCoverage)
        OS << "    bool Succeeded = " << P.getSelectFunc();
      else
        OS << "  return " << P.getSelectFunc();

      OS << "(";
      // If the complex pattern wants the root of the match, pass it in as the
      // first argument.
      if (P.hasProperty(SDNPWantRoot))
        OS << "Root, ";

      // If the complex pattern wants the parent of the operand being matched,
      // pass it in as the next argument.
      if (P.hasProperty(SDNPWantParent))
        OS << "Parent, ";

      OS << "N";
      for (unsigned i = 0; i != NumOps; ++i)
        OS << ", Result[NextRes+" << i << "].first";
      OS << ");\n";
      if (InstrumentCoverage) {
        OS << "    if (Succeeded)\n";
        OS << "       dbgs() << \"\\nCOMPLEX_PATTERN: " << P.getSelectFunc()
           << "\\n\" ;\n";
        OS << "    return Succeeded;\n";
        OS << "    }\n";
      }
    }
    OS << "  }\n";
    OS << "}\n";
    EndEmitFunction(OS);
  }


  // Emit SDNodeXForm handlers.
  // FIXME: This should be const.
  if (!NodeXForms.empty()) {
    BeginEmitFunction(OS, "SDValue",
          "RunSDNodeXForm(SDValue V, unsigned XFormNo)", true/*AddOverride*/);
    OS << "{\n";
    OS << "  switch (XFormNo) {\n";
    OS << "  default: llvm_unreachable(\"Invalid xform # in table?\");\n";

    // FIXME: The node xform could take SDValue's instead of SDNode*'s.
    for (unsigned i = 0, e = NodeXForms.size(); i != e; ++i) {
      const CodeGenDAGPatterns::NodeXForm &Entry =
        CGP.getSDNodeTransform(NodeXForms[i]);

      Record *SDNode = Entry.first;
      const std::string &Code = Entry.second;

      OS << "  case " << i << ": {  ";
      if (!OmitComments)
        OS << "// " << NodeXForms[i]->getName();
      OS << '\n';

      std::string ClassName = CGP.getSDNodeInfo(SDNode).getSDClassName();
      if (ClassName == "SDNode")
        OS << "    SDNode *N = V.getNode();\n";
      else
        OS << "    " << ClassName << " *N = cast<" << ClassName
           << ">(V.getNode());\n";
      OS << Code << "\n  }\n";
    }
    OS << "  }\n";
    OS << "}\n";
    EndEmitFunction(OS);
  }
}

static void BuildHistogram(const Matcher *M, std::vector<unsigned> &OpcodeFreq){
  for (; M != nullptr; M = M->getNext()) {
    // Count this node.
    if (unsigned(M->getKind()) >= OpcodeFreq.size())
      OpcodeFreq.resize(M->getKind()+1);
    OpcodeFreq[M->getKind()]++;

    // Handle recursive nodes.
    if (const ScopeMatcher *SM = dyn_cast<ScopeMatcher>(M)) {
      for (unsigned i = 0, e = SM->getNumChildren(); i != e; ++i)
        BuildHistogram(SM->getChild(i), OpcodeFreq);
    } else if (const SwitchOpcodeMatcher *SOM =
                 dyn_cast<SwitchOpcodeMatcher>(M)) {
      for (unsigned i = 0, e = SOM->getNumCases(); i != e; ++i)
        BuildHistogram(SOM->getCaseMatcher(i), OpcodeFreq);
    } else if (const SwitchTypeMatcher *STM = dyn_cast<SwitchTypeMatcher>(M)) {
      for (unsigned i = 0, e = STM->getNumCases(); i != e; ++i)
        BuildHistogram(STM->getCaseMatcher(i), OpcodeFreq);
    }
  }
}

static StringRef getOpcodeString(Matcher::KindTy Kind) {
  switch (Kind) {
  case Matcher::Scope: return "OPC_Scope"; break;
  case Matcher::RecordNode: return "OPC_RecordNode"; break;
  case Matcher::RecordChild: return "OPC_RecordChild"; break;
  case Matcher::RecordMemRef: return "OPC_RecordMemRef"; break;
  case Matcher::CaptureGlueInput: return "OPC_CaptureGlueInput"; break;
  case Matcher::MoveChild: return "OPC_MoveChild"; break;
  case Matcher::MoveParent: return "OPC_MoveParent"; break;
  case Matcher::CheckSame: return "OPC_CheckSame"; break;
  case Matcher::CheckChildSame: return "OPC_CheckChildSame"; break;
  case Matcher::CheckPatternPredicate:
    return "OPC_CheckPatternPredicate"; break;
  case Matcher::CheckPredicate: return "OPC_CheckPredicate"; break;
  case Matcher::CheckOpcode: return "OPC_CheckOpcode"; break;
  case Matcher::SwitchOpcode: return "OPC_SwitchOpcode"; break;
  case Matcher::CheckType: return "OPC_CheckType"; break;
  case Matcher::SwitchType: return "OPC_SwitchType"; break;
  case Matcher::CheckChildType: return "OPC_CheckChildType"; break;
  case Matcher::CheckInteger: return "OPC_CheckInteger"; break;
  case Matcher::CheckChildInteger: return "OPC_CheckChildInteger"; break;
  case Matcher::CheckCondCode: return "OPC_CheckCondCode"; break;
  case Matcher::CheckValueType: return "OPC_CheckValueType"; break;
  case Matcher::CheckComplexPat: return "OPC_CheckComplexPat"; break;
  case Matcher::CheckAndImm: return "OPC_CheckAndImm"; break;
  case Matcher::CheckOrImm: return "OPC_CheckOrImm"; break;
  case Matcher::CheckFoldableChainNode:
    return "OPC_CheckFoldableChainNode"; break;
  case Matcher::EmitInteger: return "OPC_EmitInteger"; break;
  case Matcher::EmitStringInteger: return "OPC_EmitStringInteger"; break;
  case Matcher::EmitRegister: return "OPC_EmitRegister"; break;
  case Matcher::EmitConvertToTarget: return "OPC_EmitConvertToTarget"; break;
  case Matcher::EmitMergeInputChains: return "OPC_EmitMergeInputChains"; break;
  case Matcher::EmitCopyToReg: return "OPC_EmitCopyToReg"; break;
  case Matcher::EmitNode: return "OPC_EmitNode"; break;
  case Matcher::MorphNodeTo: return "OPC_MorphNodeTo"; break;
  case Matcher::EmitNodeXForm: return "OPC_EmitNodeXForm"; break;
  case Matcher::CompleteMatch: return "OPC_CompleteMatch"; break;
  }

  llvm_unreachable("Unhandled opcode?");
}

void MatcherTableEmitter::EmitHistogram(const Matcher *M,
                                        raw_ostream &OS) {
  if (OmitComments)
    return;

  std::vector<unsigned> OpcodeFreq;
  BuildHistogram(M, OpcodeFreq);

  OS << "  // Opcode Histogram:\n";
  for (unsigned i = 0, e = OpcodeFreq.size(); i != e; ++i) {
    OS << "  // #"
       << left_justify(getOpcodeString((Matcher::KindTy)i), HistOpcWidth)
       << " = " << OpcodeFreq[i] << '\n';
  }
  OS << '\n';
}


void llvm::EmitMatcherTable(const Matcher *TheMatcher,
                            const CodeGenDAGPatterns &CGP,
                            raw_ostream &OS) {
  OS << "#if defined(GET_DAGISEL_DECL) && defined(GET_DAGISEL_BODY)\n";
  OS << "#error GET_DAGISEL_DECL and GET_DAGISEL_BODY cannot be both defined, ";
  OS << "undef both for inline definitions\n";
  OS << "#endif\n\n";

  // Emit a check for omitted class name.
  OS << "#ifdef GET_DAGISEL_BODY\n";
  OS << "#define LOCAL_DAGISEL_STRINGIZE(X) LOCAL_DAGISEL_STRINGIZE_(X)\n";
  OS << "#define LOCAL_DAGISEL_STRINGIZE_(X) #X\n";
  OS << "static_assert(sizeof(LOCAL_DAGISEL_STRINGIZE(GET_DAGISEL_BODY)) > 1,"
        "\n";
  OS << "   \"GET_DAGISEL_BODY is empty: it should be defined with the class "
        "name\");\n";
  OS << "#undef LOCAL_DAGISEL_STRINGIZE_\n";
  OS << "#undef LOCAL_DAGISEL_STRINGIZE\n";
  OS << "#endif\n\n";

  OS << "#if !defined(GET_DAGISEL_DECL) && !defined(GET_DAGISEL_BODY)\n";
  OS << "#define DAGISEL_INLINE 1\n";
  OS << "#else\n";
  OS << "#define DAGISEL_INLINE 0\n";
  OS << "#endif\n\n";

  OS << "#if !DAGISEL_INLINE\n";
  OS << "#define DAGISEL_CLASS_COLONCOLON GET_DAGISEL_BODY ::\n";
  OS << "#else\n";
  OS << "#define DAGISEL_CLASS_COLONCOLON\n";
  OS << "#endif\n\n";

  BeginEmitFunction(OS, "void", "SelectCode(SDNode *N)", false/*AddOverride*/);
  MatcherTableEmitter MatcherEmitter(CGP);

  OS << "{\n";
  OS << "  // Some target values are emitted as 2 bytes, TARGET_VAL handles\n";
  OS << "  // this.\n";
  OS << "  #define TARGET_VAL(X) X & 255, unsigned(X) >> 8\n";
  OS << "  static const unsigned char MatcherTable[] = {\n";
  unsigned TotalSize = MatcherEmitter.EmitMatcherList(TheMatcher, 1, 0, OS);
  OS << "    0\n  }; // Total Array size is " << (TotalSize+1) << " bytes\n\n";

  MatcherEmitter.EmitHistogram(TheMatcher, OS);

  OS << "  #undef TARGET_VAL\n";
  OS << "  SelectCodeCommon(N, MatcherTable,sizeof(MatcherTable));\n";
  OS << "}\n";
  EndEmitFunction(OS);

  // Next up, emit the function for node and pattern predicates:
  MatcherEmitter.EmitPredicateFunctions(OS);

  if (InstrumentCoverage)
    MatcherEmitter.EmitPatternMatchTable(OS);

  // Clean up the preprocessor macros.
  OS << "\n";
  OS << "#ifdef DAGISEL_INLINE\n";
  OS << "#undef DAGISEL_INLINE\n";
  OS << "#endif\n";
  OS << "#ifdef DAGISEL_CLASS_COLONCOLON\n";
  OS << "#undef DAGISEL_CLASS_COLONCOLON\n";
  OS << "#endif\n";
  OS << "#ifdef GET_DAGISEL_DECL\n";
  OS << "#undef GET_DAGISEL_DECL\n";
  OS << "#endif\n";
  OS << "#ifdef GET_DAGISEL_BODY\n";
  OS << "#undef GET_DAGISEL_BODY\n";
  OS << "#endif\n";
}
