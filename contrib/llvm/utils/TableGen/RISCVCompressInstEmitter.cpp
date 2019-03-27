//===- RISCVCompressInstEmitter.cpp - Generator for RISCV Compression -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// RISCVCompressInstEmitter implements a tablegen-driven CompressPat based
// RISCV Instruction Compression mechanism.
//
//===--------------------------------------------------------------===//
//
// RISCVCompressInstEmitter implements a tablegen-driven CompressPat Instruction
// Compression mechanism for generating RISCV compressed instructions
// (C ISA Extension) from the expanded instruction form.

// This tablegen backend processes CompressPat declarations in a
// td file and generates all the required checks to validate the pattern
// declarations; validate the input and output operands to generate the correct
// compressed instructions. The checks include validating  different types of
// operands; register operands, immediate operands, fixed register and fixed
// immediate inputs.
//
// Example:
// class CompressPat<dag input, dag output> {
//   dag Input  = input;
//   dag Output    = output;
//   list<Predicate> Predicates = [];
// }
//
// let Predicates = [HasStdExtC] in {
// def : CompressPat<(ADD GPRNoX0:$rs1, GPRNoX0:$rs1, GPRNoX0:$rs2),
//                   (C_ADD GPRNoX0:$rs1, GPRNoX0:$rs2)>;
// }
//
// The result is an auto-generated header file
// 'RISCVGenCompressInstEmitter.inc' which exports two functions for
// compressing/uncompressing MCInst instructions, plus
// some helper functions:
//
// bool compressInst(MCInst& OutInst, const MCInst &MI,
//                   const MCSubtargetInfo &STI,
//                   MCContext &Context);
//
// bool uncompressInst(MCInst& OutInst, const MCInst &MI,
//                     const MCRegisterInfo &MRI,
//                     const MCSubtargetInfo &STI);
//
// The clients that include this auto-generated header file and
// invoke these functions can compress an instruction before emitting
// it in the target-specific ASM or ELF streamer or can uncompress
// an instruction before printing it when the expanded instruction
// format aliases is favored.

//===----------------------------------------------------------------------===//

#include "CodeGenInstruction.h"
#include "CodeGenTarget.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <vector>
using namespace llvm;

#define DEBUG_TYPE "compress-inst-emitter"

namespace {
class RISCVCompressInstEmitter {
  struct OpData {
    enum MapKind { Operand, Imm, Reg };
    MapKind Kind;
    union {
      unsigned Operand; // Operand number mapped to.
      uint64_t Imm;     // Integer immediate value.
      Record *Reg;      // Physical register.
    } Data;
    int TiedOpIdx = -1; // Tied operand index within the instruction.
  };
  struct CompressPat {
    CodeGenInstruction Source; // The source instruction definition.
    CodeGenInstruction Dest;   // The destination instruction to transform to.
    std::vector<Record *>
        PatReqFeatures; // Required target features to enable pattern.
    IndexedMap<OpData>
        SourceOperandMap; // Maps operands in the Source Instruction to
                          // the corresponding Dest instruction operand.
    IndexedMap<OpData>
        DestOperandMap; // Maps operands in the Dest Instruction
                        // to the corresponding Source instruction operand.
    CompressPat(CodeGenInstruction &S, CodeGenInstruction &D,
                std::vector<Record *> RF, IndexedMap<OpData> &SourceMap,
                IndexedMap<OpData> &DestMap)
        : Source(S), Dest(D), PatReqFeatures(RF), SourceOperandMap(SourceMap),
          DestOperandMap(DestMap) {}
  };

  RecordKeeper &Records;
  CodeGenTarget Target;
  SmallVector<CompressPat, 4> CompressPatterns;

  void addDagOperandMapping(Record *Rec, DagInit *Dag, CodeGenInstruction &Inst,
                            IndexedMap<OpData> &OperandMap, bool IsSourceInst);
  void evaluateCompressPat(Record *Compress);
  void emitCompressInstEmitter(raw_ostream &o, bool Compress);
  bool validateTypes(Record *SubType, Record *Type, bool IsSourceInst);
  bool validateRegister(Record *Reg, Record *RegClass);
  void createDagOperandMapping(Record *Rec, StringMap<unsigned> &SourceOperands,
                               StringMap<unsigned> &DestOperands,
                               DagInit *SourceDag, DagInit *DestDag,
                               IndexedMap<OpData> &SourceOperandMap);

  void createInstOperandMapping(Record *Rec, DagInit *SourceDag,
                                DagInit *DestDag,
                                IndexedMap<OpData> &SourceOperandMap,
                                IndexedMap<OpData> &DestOperandMap,
                                StringMap<unsigned> &SourceOperands,
                                CodeGenInstruction &DestInst);

public:
  RISCVCompressInstEmitter(RecordKeeper &R) : Records(R), Target(R) {}

  void run(raw_ostream &o);
};
} // End anonymous namespace.

bool RISCVCompressInstEmitter::validateRegister(Record *Reg, Record *RegClass) {
  assert(Reg->isSubClassOf("Register") && "Reg record should be a Register\n");
  assert(RegClass->isSubClassOf("RegisterClass") && "RegClass record should be"
                                                    " a RegisterClass\n");
  CodeGenRegisterClass RC = Target.getRegisterClass(RegClass);
  const CodeGenRegister *R = Target.getRegisterByName(Reg->getName().lower());
  assert((R != nullptr) &&
         ("Register" + Reg->getName().str() + " not defined!!\n").c_str());
  return RC.contains(R);
}

bool RISCVCompressInstEmitter::validateTypes(Record *DagOpType,
                                             Record *InstOpType,
                                             bool IsSourceInst) {
  if (DagOpType == InstOpType)
    return true;
  // Only source instruction operands are allowed to not match Input Dag
  // operands.
  if (!IsSourceInst)
    return false;

  if (DagOpType->isSubClassOf("RegisterClass") &&
      InstOpType->isSubClassOf("RegisterClass")) {
    CodeGenRegisterClass RC = Target.getRegisterClass(InstOpType);
    CodeGenRegisterClass SubRC = Target.getRegisterClass(DagOpType);
    return RC.hasSubClass(&SubRC);
  }

  // At this point either or both types are not registers, reject the pattern.
  if (DagOpType->isSubClassOf("RegisterClass") ||
      InstOpType->isSubClassOf("RegisterClass"))
    return false;

  // Let further validation happen when compress()/uncompress() functions are
  // invoked.
  LLVM_DEBUG(dbgs() << (IsSourceInst ? "Input" : "Output")
                    << " Dag Operand Type: '" << DagOpType->getName()
                    << "' and "
                    << "Instruction Operand Type: '" << InstOpType->getName()
                    << "' can't be checked at pattern validation time!\n");
  return true;
}

/// The patterns in the Dag contain different types of operands:
/// Register operands, e.g.: GPRC:$rs1; Fixed registers, e.g: X1; Immediate
/// operands, e.g.: simm6:$imm; Fixed immediate operands, e.g.: 0. This function
/// maps Dag operands to its corresponding instruction operands. For register
/// operands and fixed registers it expects the Dag operand type to be contained
/// in the instantiated instruction operand type. For immediate operands and
/// immediates no validation checks are enforced at pattern validation time.
void RISCVCompressInstEmitter::addDagOperandMapping(
    Record *Rec, DagInit *Dag, CodeGenInstruction &Inst,
    IndexedMap<OpData> &OperandMap, bool IsSourceInst) {
  // TiedCount keeps track of the number of operands skipped in Inst
  // operands list to get to the corresponding Dag operand. This is
  // necessary because the number of operands in Inst might be greater
  // than number of operands in the Dag due to how tied operands
  // are represented.
  unsigned TiedCount = 0;
  for (unsigned i = 0, e = Inst.Operands.size(); i != e; ++i) {
    int TiedOpIdx = Inst.Operands[i].getTiedRegister();
    if (-1 != TiedOpIdx) {
      // Set the entry in OperandMap for the tied operand we're skipping.
      OperandMap[i].Kind = OperandMap[TiedOpIdx].Kind;
      OperandMap[i].Data = OperandMap[TiedOpIdx].Data;
      TiedCount++;
      continue;
    }
    if (DefInit *DI = dyn_cast<DefInit>(Dag->getArg(i - TiedCount))) {
      if (DI->getDef()->isSubClassOf("Register")) {
        // Check if the fixed register belongs to the Register class.
        if (!validateRegister(DI->getDef(), Inst.Operands[i].Rec))
          PrintFatalError(Rec->getLoc(),
                          "Error in Dag '" + Dag->getAsString() +
                              "'Register: '" + DI->getDef()->getName() +
                              "' is not in register class '" +
                              Inst.Operands[i].Rec->getName() + "'");
        OperandMap[i].Kind = OpData::Reg;
        OperandMap[i].Data.Reg = DI->getDef();
        continue;
      }
      // Validate that Dag operand type matches the type defined in the
      // corresponding instruction. Operands in the input Dag pattern are
      // allowed to be a subclass of the type specified in corresponding
      // instruction operand instead of being an exact match.
      if (!validateTypes(DI->getDef(), Inst.Operands[i].Rec, IsSourceInst))
        PrintFatalError(Rec->getLoc(),
                        "Error in Dag '" + Dag->getAsString() + "'. Operand '" +
                            Dag->getArgNameStr(i - TiedCount) + "' has type '" +
                            DI->getDef()->getName() +
                            "' which does not match the type '" +
                            Inst.Operands[i].Rec->getName() +
                            "' in the corresponding instruction operand!");

      OperandMap[i].Kind = OpData::Operand;
    } else if (IntInit *II = dyn_cast<IntInit>(Dag->getArg(i - TiedCount))) {
      // Validate that corresponding instruction operand expects an immediate.
      if (Inst.Operands[i].Rec->isSubClassOf("RegisterClass"))
        PrintFatalError(
            Rec->getLoc(),
            ("Error in Dag '" + Dag->getAsString() + "' Found immediate: '" +
             II->getAsString() +
             "' but corresponding instruction operand expected a register!"));
      // No pattern validation check possible for values of fixed immediate.
      OperandMap[i].Kind = OpData::Imm;
      OperandMap[i].Data.Imm = II->getValue();
      LLVM_DEBUG(
          dbgs() << "  Found immediate '" << II->getValue() << "' at "
                 << (IsSourceInst ? "input " : "output ")
                 << "Dag. No validation time check possible for values of "
                    "fixed immediate.\n");
    } else
      llvm_unreachable("Unhandled CompressPat argument type!");
  }
}

// Verify the Dag operand count is enough to build an instruction.
static bool verifyDagOpCount(CodeGenInstruction &Inst, DagInit *Dag,
                             bool IsSource) {
  if (Dag->getNumArgs() == Inst.Operands.size())
    return true;
  // Source instructions are non compressed instructions and don't have tied
  // operands.
  if (IsSource)
    PrintFatalError("Input operands for Inst '" + Inst.TheDef->getName() +
                    "' and input Dag operand count mismatch");
  // The Dag can't have more arguments than the Instruction.
  if (Dag->getNumArgs() > Inst.Operands.size())
    PrintFatalError("Inst '" + Inst.TheDef->getName() +
                    "' and Dag operand count mismatch");

  // The Instruction might have tied operands so the Dag might have
  //  a fewer operand count.
  unsigned RealCount = Inst.Operands.size();
  for (unsigned i = 0; i < Inst.Operands.size(); i++)
    if (Inst.Operands[i].getTiedRegister() != -1)
      --RealCount;

  if (Dag->getNumArgs() != RealCount)
    PrintFatalError("Inst '" + Inst.TheDef->getName() +
                    "' and Dag operand count mismatch");
  return true;
}

static bool validateArgsTypes(Init *Arg1, Init *Arg2) {
  DefInit *Type1 = dyn_cast<DefInit>(Arg1);
  DefInit *Type2 = dyn_cast<DefInit>(Arg2);
  assert(Type1 && ("Arg1 type not found\n"));
  assert(Type2 && ("Arg2 type not found\n"));
  return Type1->getDef() == Type2->getDef();
}

// Creates a mapping between the operand name in the Dag (e.g. $rs1) and
// its index in the list of Dag operands and checks that operands with the same
// name have the same types. For example in 'C_ADD $rs1, $rs2' we generate the
// mapping $rs1 --> 0, $rs2 ---> 1. If the operand appears twice in the (tied)
// same Dag we use the last occurrence for indexing.
void RISCVCompressInstEmitter::createDagOperandMapping(
    Record *Rec, StringMap<unsigned> &SourceOperands,
    StringMap<unsigned> &DestOperands, DagInit *SourceDag, DagInit *DestDag,
    IndexedMap<OpData> &SourceOperandMap) {
  for (unsigned i = 0; i < DestDag->getNumArgs(); ++i) {
    // Skip fixed immediates and registers, they were handled in
    // addDagOperandMapping.
    if ("" == DestDag->getArgNameStr(i))
      continue;
    DestOperands[DestDag->getArgNameStr(i)] = i;
  }

  for (unsigned i = 0; i < SourceDag->getNumArgs(); ++i) {
    // Skip fixed immediates and registers, they were handled in
    // addDagOperandMapping.
    if ("" == SourceDag->getArgNameStr(i))
      continue;

    StringMap<unsigned>::iterator it =
        SourceOperands.find(SourceDag->getArgNameStr(i));
    if (it != SourceOperands.end()) {
      // Operand sharing the same name in the Dag should be mapped as tied.
      SourceOperandMap[i].TiedOpIdx = it->getValue();
      if (!validateArgsTypes(SourceDag->getArg(it->getValue()),
                             SourceDag->getArg(i)))
        PrintFatalError(Rec->getLoc(),
                        "Input Operand '" + SourceDag->getArgNameStr(i) +
                            "' has a mismatched tied operand!\n");
    }
    it = DestOperands.find(SourceDag->getArgNameStr(i));
    if (it == DestOperands.end())
      PrintFatalError(Rec->getLoc(), "Operand " + SourceDag->getArgNameStr(i) +
                                         " defined in Input Dag but not used in"
                                         " Output Dag!\n");
    // Input Dag operand types must match output Dag operand type.
    if (!validateArgsTypes(DestDag->getArg(it->getValue()),
                           SourceDag->getArg(i)))
      PrintFatalError(Rec->getLoc(), "Type mismatch between Input and "
                                     "Output Dag operand '" +
                                         SourceDag->getArgNameStr(i) + "'!");
    SourceOperands[SourceDag->getArgNameStr(i)] = i;
  }
}

/// Map operand names in the Dag to their index in both corresponding input and
/// output instructions. Validate that operands defined in the input are
/// used in the output pattern while populating the maps.
void RISCVCompressInstEmitter::createInstOperandMapping(
    Record *Rec, DagInit *SourceDag, DagInit *DestDag,
    IndexedMap<OpData> &SourceOperandMap, IndexedMap<OpData> &DestOperandMap,
    StringMap<unsigned> &SourceOperands, CodeGenInstruction &DestInst) {
  // TiedCount keeps track of the number of operands skipped in Inst
  // operands list to get to the corresponding Dag operand.
  unsigned TiedCount = 0;
  LLVM_DEBUG(dbgs() << "  Operand mapping:\n  Source   Dest\n");
  for (unsigned i = 0, e = DestInst.Operands.size(); i != e; ++i) {
    int TiedInstOpIdx = DestInst.Operands[i].getTiedRegister();
    if (TiedInstOpIdx != -1) {
      ++TiedCount;
      DestOperandMap[i].Data = DestOperandMap[TiedInstOpIdx].Data;
      DestOperandMap[i].Kind = DestOperandMap[TiedInstOpIdx].Kind;
      if (DestOperandMap[i].Kind == OpData::Operand)
        // No need to fill the SourceOperandMap here since it was mapped to
        // destination operand 'TiedInstOpIdx' in a previous iteration.
        LLVM_DEBUG(dbgs() << "    " << DestOperandMap[i].Data.Operand
                          << " ====> " << i
                          << "  Dest operand tied with operand '"
                          << TiedInstOpIdx << "'\n");
      continue;
    }
    // Skip fixed immediates and registers, they were handled in
    // addDagOperandMapping.
    if (DestOperandMap[i].Kind != OpData::Operand)
      continue;

    unsigned DagArgIdx = i - TiedCount;
    StringMap<unsigned>::iterator SourceOp =
        SourceOperands.find(DestDag->getArgNameStr(DagArgIdx));
    if (SourceOp == SourceOperands.end())
      PrintFatalError(Rec->getLoc(),
                      "Output Dag operand '" +
                          DestDag->getArgNameStr(DagArgIdx) +
                          "' has no matching input Dag operand.");

    assert(DestDag->getArgNameStr(DagArgIdx) ==
               SourceDag->getArgNameStr(SourceOp->getValue()) &&
           "Incorrect operand mapping detected!\n");
    DestOperandMap[i].Data.Operand = SourceOp->getValue();
    SourceOperandMap[SourceOp->getValue()].Data.Operand = i;
    LLVM_DEBUG(dbgs() << "    " << SourceOp->getValue() << " ====> " << i
                      << "\n");
  }
}

/// Validates the CompressPattern and create operand mapping.
/// These are the checks to validate a CompressPat pattern declarations.
/// Error out with message under these conditions:
/// - Dag Input opcode is an expanded instruction and Dag Output opcode is a
///   compressed instruction.
/// - Operands in Dag Input must be all used in Dag Output.
///   Register Operand type in Dag Input Type  must be contained in the
///   corresponding Source Instruction type.
/// - Register Operand type in Dag Input must be the  same as in  Dag Ouput.
/// - Register Operand type in  Dag Output must be the same  as the
///   corresponding Destination Inst type.
/// - Immediate Operand type in Dag Input must be the same as in Dag Ouput.
/// - Immediate Operand type in Dag Ouput must be the same as the corresponding
///   Destination Instruction type.
/// - Fixed register must be contained in the corresponding Source Instruction
///   type.
/// - Fixed register must be contained in the corresponding Destination
///   Instruction type. Warning message printed under these conditions:
/// - Fixed immediate in Dag Input or Dag Ouput cannot be checked at this time
///   and generate warning.
/// - Immediate operand type in Dag Input differs from the corresponding Source
///   Instruction type  and generate a warning.
void RISCVCompressInstEmitter::evaluateCompressPat(Record *Rec) {
  // Validate input Dag operands.
  DagInit *SourceDag = Rec->getValueAsDag("Input");
  assert(SourceDag && "Missing 'Input' in compress pattern!");
  LLVM_DEBUG(dbgs() << "Input: " << *SourceDag << "\n");

  DefInit *OpDef = dyn_cast<DefInit>(SourceDag->getOperator());
  if (!OpDef)
    PrintFatalError(Rec->getLoc(),
                    Rec->getName() + " has unexpected operator type!");
  // Checking we are transforming from compressed to uncompressed instructions.
  Record *Operator = OpDef->getDef();
  if (!Operator->isSubClassOf("RVInst"))
    PrintFatalError(Rec->getLoc(), "Input instruction '" + Operator->getName() +
                                       "' is not a 32 bit wide instruction!");
  CodeGenInstruction SourceInst(Operator);
  verifyDagOpCount(SourceInst, SourceDag, true);

  // Validate output Dag operands.
  DagInit *DestDag = Rec->getValueAsDag("Output");
  assert(DestDag && "Missing 'Output' in compress pattern!");
  LLVM_DEBUG(dbgs() << "Output: " << *DestDag << "\n");

  DefInit *DestOpDef = dyn_cast<DefInit>(DestDag->getOperator());
  if (!DestOpDef)
    PrintFatalError(Rec->getLoc(),
                    Rec->getName() + " has unexpected operator type!");

  Record *DestOperator = DestOpDef->getDef();
  if (!DestOperator->isSubClassOf("RVInst16"))
    PrintFatalError(Rec->getLoc(), "Output instruction  '" +
                                       DestOperator->getName() +
                                       "' is not a 16 bit wide instruction!");
  CodeGenInstruction DestInst(DestOperator);
  verifyDagOpCount(DestInst, DestDag, false);

  // Fill the mapping from the source to destination instructions.

  IndexedMap<OpData> SourceOperandMap;
  SourceOperandMap.grow(SourceInst.Operands.size());
  // Create a mapping between source Dag operands and source Inst operands.
  addDagOperandMapping(Rec, SourceDag, SourceInst, SourceOperandMap,
                       /*IsSourceInst*/ true);

  IndexedMap<OpData> DestOperandMap;
  DestOperandMap.grow(DestInst.Operands.size());
  // Create a mapping between destination Dag operands and destination Inst
  // operands.
  addDagOperandMapping(Rec, DestDag, DestInst, DestOperandMap,
                       /*IsSourceInst*/ false);

  StringMap<unsigned> SourceOperands;
  StringMap<unsigned> DestOperands;
  createDagOperandMapping(Rec, SourceOperands, DestOperands, SourceDag, DestDag,
                          SourceOperandMap);
  // Create operand mapping between the source and destination instructions.
  createInstOperandMapping(Rec, SourceDag, DestDag, SourceOperandMap,
                           DestOperandMap, SourceOperands, DestInst);

  // Get the target features for the CompressPat.
  std::vector<Record *> PatReqFeatures;
  std::vector<Record *> RF = Rec->getValueAsListOfDefs("Predicates");
  copy_if(RF, std::back_inserter(PatReqFeatures), [](Record *R) {
    return R->getValueAsBit("AssemblerMatcherPredicate");
  });

  CompressPatterns.push_back(CompressPat(SourceInst, DestInst, PatReqFeatures,
                                         SourceOperandMap, DestOperandMap));
}

static void getReqFeatures(std::map<StringRef, int> &FeaturesMap,
                           const std::vector<Record *> &ReqFeatures) {
  for (auto &R : ReqFeatures) {
    StringRef AsmCondString = R->getValueAsString("AssemblerCondString");

    // AsmCondString has syntax [!]F(,[!]F)*
    SmallVector<StringRef, 4> Ops;
    SplitString(AsmCondString, Ops, ",");
    assert(!Ops.empty() && "AssemblerCondString cannot be empty");

    for (auto &Op : Ops) {
      assert(!Op.empty() && "Empty operator");
      if (FeaturesMap.find(Op) == FeaturesMap.end())
        FeaturesMap[Op] = FeaturesMap.size();
    }
  }
}

unsigned getMCOpPredicate(DenseMap<const Record *, unsigned> &MCOpPredicateMap,
                          std::vector<const Record *> &MCOpPredicates,
                          Record *Rec) {
  unsigned Entry = MCOpPredicateMap[Rec];
  if (Entry)
    return Entry;

  if (!Rec->isValueUnset("MCOperandPredicate")) {
    MCOpPredicates.push_back(Rec);
    Entry = MCOpPredicates.size();
    MCOpPredicateMap[Rec] = Entry;
    return Entry;
  }

  PrintFatalError(Rec->getLoc(),
                  "No MCOperandPredicate on this operand at all: " +
                      Rec->getName().str() + "'");
  return 0;
}

static std::string mergeCondAndCode(raw_string_ostream &CondStream,
                                    raw_string_ostream &CodeStream) {
  std::string S;
  raw_string_ostream CombinedStream(S);
  CombinedStream.indent(4)
      << "if ("
      << CondStream.str().substr(
             6, CondStream.str().length() -
                    10) // remove first indentation and last '&&'.
      << ") {\n";
  CombinedStream << CodeStream.str();
  CombinedStream.indent(4) << "  return true;\n";
  CombinedStream.indent(4) << "} // if\n";
  return CombinedStream.str();
}

void RISCVCompressInstEmitter::emitCompressInstEmitter(raw_ostream &o,
                                                       bool Compress) {
  Record *AsmWriter = Target.getAsmWriter();
  if (!AsmWriter->getValueAsInt("PassSubtarget"))
    PrintFatalError("'PassSubtarget' is false. SubTargetInfo object is needed "
                    "for target features.\n");

  std::string Namespace = Target.getName();

  // Sort entries in CompressPatterns to handle instructions that can have more
  // than one candidate for compression\uncompression, e.g ADD can be
  // transformed to a C_ADD or a C_MV. When emitting 'uncompress()' function the
  // source and destination are flipped and the sort key needs to change
  // accordingly.
  std::stable_sort(CompressPatterns.begin(), CompressPatterns.end(),
                   [Compress](const CompressPat &LHS, const CompressPat &RHS) {
                     if (Compress)
                       return (LHS.Source.TheDef->getName().str() <
                               RHS.Source.TheDef->getName().str());
                     else
                       return (LHS.Dest.TheDef->getName().str() <
                               RHS.Dest.TheDef->getName().str());
                   });

  // A list of MCOperandPredicates for all operands in use, and the reverse map.
  std::vector<const Record *> MCOpPredicates;
  DenseMap<const Record *, unsigned> MCOpPredicateMap;

  std::string F;
  std::string FH;
  raw_string_ostream Func(F);
  raw_string_ostream FuncH(FH);
  bool NeedMRI = false;

  if (Compress)
    o << "\n#ifdef GEN_COMPRESS_INSTR\n"
      << "#undef GEN_COMPRESS_INSTR\n\n";
  else
    o << "\n#ifdef GEN_UNCOMPRESS_INSTR\n"
      << "#undef GEN_UNCOMPRESS_INSTR\n\n";

  if (Compress) {
    FuncH << "static bool compressInst(MCInst& OutInst,\n";
    FuncH.indent(25) << "const MCInst &MI,\n";
    FuncH.indent(25) << "const MCSubtargetInfo &STI,\n";
    FuncH.indent(25) << "MCContext &Context) {\n";
  } else {
    FuncH << "static bool uncompressInst(MCInst& OutInst,\n";
    FuncH.indent(27) << "const MCInst &MI,\n";
    FuncH.indent(27) << "const MCRegisterInfo &MRI,\n";
    FuncH.indent(27) << "const MCSubtargetInfo &STI) {\n";
  }

  if (CompressPatterns.empty()) {
    o << FuncH.str();
    o.indent(2) << "return false;\n}\n";
    if (Compress)
      o << "\n#endif //GEN_COMPRESS_INSTR\n";
    else
      o << "\n#endif //GEN_UNCOMPRESS_INSTR\n\n";
    return;
  }

  std::string CaseString("");
  raw_string_ostream CaseStream(CaseString);
  std::string PrevOp("");
  std::string CurOp("");
  CaseStream << "  switch (MI.getOpcode()) {\n";
  CaseStream << "    default: return false;\n";

  for (auto &CompressPat : CompressPatterns) {
    std::string CondString;
    std::string CodeString;
    raw_string_ostream CondStream(CondString);
    raw_string_ostream CodeStream(CodeString);
    CodeGenInstruction &Source =
        Compress ? CompressPat.Source : CompressPat.Dest;
    CodeGenInstruction &Dest = Compress ? CompressPat.Dest : CompressPat.Source;
    IndexedMap<OpData> SourceOperandMap =
        Compress ? CompressPat.SourceOperandMap : CompressPat.DestOperandMap;
    IndexedMap<OpData> &DestOperandMap =
        Compress ? CompressPat.DestOperandMap : CompressPat.SourceOperandMap;

    CurOp = Source.TheDef->getName().str();
    // Check current and previous opcode to decide to continue or end a case.
    if (CurOp != PrevOp) {
      if (PrevOp != "")
        CaseStream.indent(6) << "break;\n    } // case " + PrevOp + "\n";
      CaseStream.indent(4) << "case " + Namespace + "::" + CurOp + ": {\n";
    }

    std::map<StringRef, int> FeaturesMap;
    // Add CompressPat required features.
    getReqFeatures(FeaturesMap, CompressPat.PatReqFeatures);

    // Add Dest instruction required features.
    std::vector<Record *> ReqFeatures;
    std::vector<Record *> RF = Dest.TheDef->getValueAsListOfDefs("Predicates");
    copy_if(RF, std::back_inserter(ReqFeatures), [](Record *R) {
      return R->getValueAsBit("AssemblerMatcherPredicate");
    });
    getReqFeatures(FeaturesMap, ReqFeatures);

    // Emit checks for all required features.
    for (auto &F : FeaturesMap) {
      StringRef Op = F.first;
      if (Op[0] == '!')
        CondStream.indent(6) << ("!STI.getFeatureBits()[" + Namespace +
                                 "::" + Op.substr(1) + "]")
                                        .str() +
                                    " &&\n";
      else
        CondStream.indent(6)
            << ("STI.getFeatureBits()[" + Namespace + "::" + Op + "]").str() +
                   " &&\n";
    }

    // Start Source Inst operands validation.
    unsigned OpNo = 0;
    for (OpNo = 0; OpNo < Source.Operands.size(); ++OpNo) {
      if (SourceOperandMap[OpNo].TiedOpIdx != -1) {
        if (Source.Operands[OpNo].Rec->isSubClassOf("RegisterClass"))
          CondStream.indent(6)
              << "(MI.getOperand("
              << std::to_string(OpNo) + ").getReg() ==  MI.getOperand("
              << std::to_string(SourceOperandMap[OpNo].TiedOpIdx)
              << ").getReg()) &&\n";
        else
          PrintFatalError("Unexpected tied operand types!\n");
      }
      // Check for fixed immediates\registers in the source instruction.
      switch (SourceOperandMap[OpNo].Kind) {
      case OpData::Operand:
        // We don't need to do anything for source instruction operand checks.
        break;
      case OpData::Imm:
        CondStream.indent(6)
            << "(MI.getOperand(" + std::to_string(OpNo) + ").isImm()) &&\n" +
                   "      (MI.getOperand(" + std::to_string(OpNo) +
                   ").getImm() == " +
                   std::to_string(SourceOperandMap[OpNo].Data.Imm) + ") &&\n";
        break;
      case OpData::Reg: {
        Record *Reg = SourceOperandMap[OpNo].Data.Reg;
        CondStream.indent(6) << "(MI.getOperand(" + std::to_string(OpNo) +
                                    ").getReg() == " + Namespace +
                                    "::" + Reg->getName().str() + ") &&\n";
        break;
      }
      }
    }
    CodeStream.indent(6) << "// " + Dest.AsmString + "\n";
    CodeStream.indent(6) << "OutInst.setOpcode(" + Namespace +
                                "::" + Dest.TheDef->getName().str() + ");\n";
    OpNo = 0;
    for (const auto &DestOperand : Dest.Operands) {
      CodeStream.indent(6) << "// Operand: " + DestOperand.Name + "\n";
      switch (DestOperandMap[OpNo].Kind) {
      case OpData::Operand: {
        unsigned OpIdx = DestOperandMap[OpNo].Data.Operand;
        // Check that the operand in the Source instruction fits
        // the type for the Dest instruction.
        if (DestOperand.Rec->isSubClassOf("RegisterClass")) {
          NeedMRI = true;
          // This is a register operand. Check the register class.
          // Don't check register class if this is a tied operand, it was done
          // for the operand its tied to.
          if (DestOperand.getTiedRegister() == -1)
            CondStream.indent(6)
                << "(MRI.getRegClass(" + Namespace +
                       "::" + DestOperand.Rec->getName().str() +
                       "RegClassID).contains(" + "MI.getOperand(" +
                       std::to_string(OpIdx) + ").getReg())) &&\n";

          CodeStream.indent(6) << "OutInst.addOperand(MI.getOperand(" +
                                      std::to_string(OpIdx) + "));\n";
        } else {
          // Handling immediate operands.
          unsigned Entry = getMCOpPredicate(MCOpPredicateMap, MCOpPredicates,
                                            DestOperand.Rec);
          CondStream.indent(6) << Namespace + "ValidateMCOperand(" +
                                      "MI.getOperand(" + std::to_string(OpIdx) +
                                      "), STI, " + std::to_string(Entry) +
                                      ") &&\n";
          CodeStream.indent(6) << "OutInst.addOperand(MI.getOperand(" +
                                      std::to_string(OpIdx) + "));\n";
        }
        break;
      }
      case OpData::Imm: {
        unsigned Entry =
            getMCOpPredicate(MCOpPredicateMap, MCOpPredicates, DestOperand.Rec);
        CondStream.indent(6)
            << Namespace + "ValidateMCOperand(" + "MCOperand::createImm(" +
                   std::to_string(DestOperandMap[OpNo].Data.Imm) + "), STI, " +
                   std::to_string(Entry) + ") &&\n";
        CodeStream.indent(6)
            << "OutInst.addOperand(MCOperand::createImm(" +
                   std::to_string(DestOperandMap[OpNo].Data.Imm) + "));\n";
      } break;
      case OpData::Reg: {
        // Fixed register has been validated at pattern validation time.
        Record *Reg = DestOperandMap[OpNo].Data.Reg;
        CodeStream.indent(6) << "OutInst.addOperand(MCOperand::createReg(" +
                                    Namespace + "::" + Reg->getName().str() +
                                    "));\n";
      } break;
      }
      ++OpNo;
    }
    CaseStream << mergeCondAndCode(CondStream, CodeStream);
    PrevOp = CurOp;
  }
  Func << CaseStream.str() << "\n";
  // Close brace for the last case.
  Func.indent(4) << "} // case " + CurOp + "\n";
  Func.indent(2) << "} // switch\n";
  Func.indent(2) << "return false;\n}\n";

  if (!MCOpPredicates.empty()) {
    o << "static bool " << Namespace
      << "ValidateMCOperand(const MCOperand &MCOp,\n"
      << "                  const MCSubtargetInfo &STI,\n"
      << "                  unsigned PredicateIndex) {\n"
      << "  switch (PredicateIndex) {\n"
      << "  default:\n"
      << "    llvm_unreachable(\"Unknown MCOperandPredicate kind\");\n"
      << "    break;\n";

    for (unsigned i = 0; i < MCOpPredicates.size(); ++i) {
      Init *MCOpPred = MCOpPredicates[i]->getValueInit("MCOperandPredicate");
      if (CodeInit *SI = dyn_cast<CodeInit>(MCOpPred))
        o << "  case " << i + 1 << ": {\n"
          << "   // " << MCOpPredicates[i]->getName().str() << SI->getValue()
          << "\n"
          << "    }\n";
      else
        llvm_unreachable("Unexpected MCOperandPredicate field!");
    }
    o << "  }\n"
      << "}\n\n";
  }

  o << FuncH.str();
  if (NeedMRI && Compress)
    o.indent(2) << "const MCRegisterInfo &MRI = *Context.getRegisterInfo();\n";
  o << Func.str();

  if (Compress)
    o << "\n#endif //GEN_COMPRESS_INSTR\n";
  else
    o << "\n#endif //GEN_UNCOMPRESS_INSTR\n\n";
}

void RISCVCompressInstEmitter::run(raw_ostream &o) {
  Record *CompressClass = Records.getClass("CompressPat");
  assert(CompressClass && "Compress class definition missing!");
  std::vector<Record *> Insts;
  for (const auto &D : Records.getDefs()) {
    if (D.second->isSubClassOf(CompressClass))
      Insts.push_back(D.second.get());
  }

  // Process the CompressPat definitions, validating them as we do so.
  for (unsigned i = 0, e = Insts.size(); i != e; ++i)
    evaluateCompressPat(Insts[i]);

  // Emit file header.
  emitSourceFileHeader("Compress instruction Source Fragment", o);
  // Generate compressInst() function.
  emitCompressInstEmitter(o, true);
  // Generate uncompressInst() function.
  emitCompressInstEmitter(o, false);
}

namespace llvm {

void EmitCompressInst(RecordKeeper &RK, raw_ostream &OS) {
  RISCVCompressInstEmitter(RK).run(OS);
}

} // namespace llvm
