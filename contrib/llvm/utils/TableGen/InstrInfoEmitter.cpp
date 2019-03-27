//===- InstrInfoEmitter.cpp - Generate a Instruction Set Desc. --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend is responsible for emitting a description of the target
// instruction set for the code generator.
//
//===----------------------------------------------------------------------===//

#include "CodeGenDAGPatterns.h"
#include "CodeGenInstruction.h"
#include "CodeGenSchedule.h"
#include "CodeGenTarget.h"
#include "PredicateExpander.h"
#include "SequenceToOffsetTable.h"
#include "TableGenBackends.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

class InstrInfoEmitter {
  RecordKeeper &Records;
  CodeGenDAGPatterns CDP;
  const CodeGenSchedModels &SchedModels;

public:
  InstrInfoEmitter(RecordKeeper &R):
    Records(R), CDP(R), SchedModels(CDP.getTargetInfo().getSchedModels()) {}

  // run - Output the instruction set description.
  void run(raw_ostream &OS);

private:
  void emitEnums(raw_ostream &OS);

  typedef std::map<std::vector<std::string>, unsigned> OperandInfoMapTy;

  /// The keys of this map are maps which have OpName enum values as their keys
  /// and instruction operand indices as their values.  The values of this map
  /// are lists of instruction names.
  typedef std::map<std::map<unsigned, unsigned>,
                   std::vector<std::string>> OpNameMapTy;
  typedef std::map<std::string, unsigned>::iterator StrUintMapIter;

  /// Generate member functions in the target-specific GenInstrInfo class.
  ///
  /// This method is used to custom expand TIIPredicate definitions.
  /// See file llvm/Target/TargetInstPredicates.td for a description of what is
  /// a TIIPredicate and how to use it.
  void emitTIIHelperMethods(raw_ostream &OS, StringRef TargetName,
                            bool ExpandDefinition = true);

  /// Expand TIIPredicate definitions to functions that accept a const MCInst
  /// reference.
  void emitMCIIHelperMethods(raw_ostream &OS, StringRef TargetName);
  void emitRecord(const CodeGenInstruction &Inst, unsigned Num,
                  Record *InstrInfo,
                  std::map<std::vector<Record*>, unsigned> &EL,
                  const OperandInfoMapTy &OpInfo,
                  raw_ostream &OS);
  void emitOperandTypesEnum(raw_ostream &OS, const CodeGenTarget &Target);
  void initOperandMapData(
            ArrayRef<const CodeGenInstruction *> NumberedInstructions,
            StringRef Namespace,
            std::map<std::string, unsigned> &Operands,
            OpNameMapTy &OperandMap);
  void emitOperandNameMappings(raw_ostream &OS, const CodeGenTarget &Target,
            ArrayRef<const CodeGenInstruction*> NumberedInstructions);

  // Operand information.
  void EmitOperandInfo(raw_ostream &OS, OperandInfoMapTy &OperandInfoIDs);
  std::vector<std::string> GetOperandInfo(const CodeGenInstruction &Inst);
};

} // end anonymous namespace

static void PrintDefList(const std::vector<Record*> &Uses,
                         unsigned Num, raw_ostream &OS) {
  OS << "static const MCPhysReg ImplicitList" << Num << "[] = { ";
  for (Record *U : Uses)
    OS << getQualifiedName(U) << ", ";
  OS << "0 };\n";
}

//===----------------------------------------------------------------------===//
// Operand Info Emission.
//===----------------------------------------------------------------------===//

std::vector<std::string>
InstrInfoEmitter::GetOperandInfo(const CodeGenInstruction &Inst) {
  std::vector<std::string> Result;

  for (auto &Op : Inst.Operands) {
    // Handle aggregate operands and normal operands the same way by expanding
    // either case into a list of operands for this op.
    std::vector<CGIOperandList::OperandInfo> OperandList;

    // This might be a multiple operand thing.  Targets like X86 have
    // registers in their multi-operand operands.  It may also be an anonymous
    // operand, which has a single operand, but no declared class for the
    // operand.
    DagInit *MIOI = Op.MIOperandInfo;

    if (!MIOI || MIOI->getNumArgs() == 0) {
      // Single, anonymous, operand.
      OperandList.push_back(Op);
    } else {
      for (unsigned j = 0, e = Op.MINumOperands; j != e; ++j) {
        OperandList.push_back(Op);

        auto *OpR = cast<DefInit>(MIOI->getArg(j))->getDef();
        OperandList.back().Rec = OpR;
      }
    }

    for (unsigned j = 0, e = OperandList.size(); j != e; ++j) {
      Record *OpR = OperandList[j].Rec;
      std::string Res;

      if (OpR->isSubClassOf("RegisterOperand"))
        OpR = OpR->getValueAsDef("RegClass");
      if (OpR->isSubClassOf("RegisterClass"))
        Res += getQualifiedName(OpR) + "RegClassID, ";
      else if (OpR->isSubClassOf("PointerLikeRegClass"))
        Res += utostr(OpR->getValueAsInt("RegClassKind")) + ", ";
      else
        // -1 means the operand does not have a fixed register class.
        Res += "-1, ";

      // Fill in applicable flags.
      Res += "0";

      // Ptr value whose register class is resolved via callback.
      if (OpR->isSubClassOf("PointerLikeRegClass"))
        Res += "|(1<<MCOI::LookupPtrRegClass)";

      // Predicate operands.  Check to see if the original unexpanded operand
      // was of type PredicateOp.
      if (Op.Rec->isSubClassOf("PredicateOp"))
        Res += "|(1<<MCOI::Predicate)";

      // Optional def operands.  Check to see if the original unexpanded operand
      // was of type OptionalDefOperand.
      if (Op.Rec->isSubClassOf("OptionalDefOperand"))
        Res += "|(1<<MCOI::OptionalDef)";

      // Fill in operand type.
      Res += ", ";
      assert(!Op.OperandType.empty() && "Invalid operand type.");
      Res += Op.OperandType;

      // Fill in constraint info.
      Res += ", ";

      const CGIOperandList::ConstraintInfo &Constraint =
        Op.Constraints[j];
      if (Constraint.isNone())
        Res += "0";
      else if (Constraint.isEarlyClobber())
        Res += "(1 << MCOI::EARLY_CLOBBER)";
      else {
        assert(Constraint.isTied());
        Res += "((" + utostr(Constraint.getTiedOperand()) +
                    " << 16) | (1 << MCOI::TIED_TO))";
      }

      Result.push_back(Res);
    }
  }

  return Result;
}

void InstrInfoEmitter::EmitOperandInfo(raw_ostream &OS,
                                       OperandInfoMapTy &OperandInfoIDs) {
  // ID #0 is for no operand info.
  unsigned OperandListNum = 0;
  OperandInfoIDs[std::vector<std::string>()] = ++OperandListNum;

  OS << "\n";
  const CodeGenTarget &Target = CDP.getTargetInfo();
  for (const CodeGenInstruction *Inst : Target.getInstructionsByEnumValue()) {
    std::vector<std::string> OperandInfo = GetOperandInfo(*Inst);
    unsigned &N = OperandInfoIDs[OperandInfo];
    if (N != 0) continue;

    N = ++OperandListNum;
    OS << "static const MCOperandInfo OperandInfo" << N << "[] = { ";
    for (const std::string &Info : OperandInfo)
      OS << "{ " << Info << " }, ";
    OS << "};\n";
  }
}

/// Initialize data structures for generating operand name mappings.
/// 
/// \param Operands [out] A map used to generate the OpName enum with operand
///        names as its keys and operand enum values as its values.
/// \param OperandMap [out] A map for representing the operand name mappings for
///        each instructions.  This is used to generate the OperandMap table as
///        well as the getNamedOperandIdx() function.
void InstrInfoEmitter::initOperandMapData(
        ArrayRef<const CodeGenInstruction *> NumberedInstructions,
        StringRef Namespace,
        std::map<std::string, unsigned> &Operands,
        OpNameMapTy &OperandMap) {
  unsigned NumOperands = 0;
  for (const CodeGenInstruction *Inst : NumberedInstructions) {
    if (!Inst->TheDef->getValueAsBit("UseNamedOperandTable"))
      continue;
    std::map<unsigned, unsigned> OpList;
    for (const auto &Info : Inst->Operands) {
      StrUintMapIter I = Operands.find(Info.Name);

      if (I == Operands.end()) {
        I = Operands.insert(Operands.begin(),
                    std::pair<std::string, unsigned>(Info.Name, NumOperands++));
      }
      OpList[I->second] = Info.MIOperandNo;
    }
    OperandMap[OpList].push_back(Namespace.str() + "::" +
                                 Inst->TheDef->getName().str());
  }
}

/// Generate a table and function for looking up the indices of operands by
/// name.
///
/// This code generates:
/// - An enum in the llvm::TargetNamespace::OpName namespace, with one entry
///   for each operand name.
/// - A 2-dimensional table called OperandMap for mapping OpName enum values to
///   operand indices.
/// - A function called getNamedOperandIdx(uint16_t Opcode, uint16_t NamedIdx)
///   for looking up the operand index for an instruction, given a value from
///   OpName enum
void InstrInfoEmitter::emitOperandNameMappings(raw_ostream &OS,
           const CodeGenTarget &Target,
           ArrayRef<const CodeGenInstruction*> NumberedInstructions) {
  StringRef Namespace = Target.getInstNamespace();
  std::string OpNameNS = "OpName";
  // Map of operand names to their enumeration value.  This will be used to
  // generate the OpName enum.
  std::map<std::string, unsigned> Operands;
  OpNameMapTy OperandMap;

  initOperandMapData(NumberedInstructions, Namespace, Operands, OperandMap);

  OS << "#ifdef GET_INSTRINFO_OPERAND_ENUM\n";
  OS << "#undef GET_INSTRINFO_OPERAND_ENUM\n";
  OS << "namespace llvm {\n";
  OS << "namespace " << Namespace << " {\n";
  OS << "namespace " << OpNameNS << " {\n";
  OS << "enum {\n";
  for (const auto &Op : Operands)
    OS << "  " << Op.first << " = " << Op.second << ",\n";

  OS << "OPERAND_LAST";
  OS << "\n};\n";
  OS << "} // end namespace OpName\n";
  OS << "} // end namespace " << Namespace << "\n";
  OS << "} // end namespace llvm\n";
  OS << "#endif //GET_INSTRINFO_OPERAND_ENUM\n\n";

  OS << "#ifdef GET_INSTRINFO_NAMED_OPS\n";
  OS << "#undef GET_INSTRINFO_NAMED_OPS\n";
  OS << "namespace llvm {\n";
  OS << "namespace " << Namespace << " {\n";
  OS << "LLVM_READONLY\n";
  OS << "int16_t getNamedOperandIdx(uint16_t Opcode, uint16_t NamedIdx) {\n";
  if (!Operands.empty()) {
    OS << "  static const int16_t OperandMap [][" << Operands.size()
       << "] = {\n";
    for (const auto &Entry : OperandMap) {
      const std::map<unsigned, unsigned> &OpList = Entry.first;
      OS << "{";

      // Emit a row of the OperandMap table
      for (unsigned i = 0, e = Operands.size(); i != e; ++i)
        OS << (OpList.count(i) == 0 ? -1 : (int)OpList.find(i)->second) << ", ";

      OS << "},\n";
    }
    OS << "};\n";

    OS << "  switch(Opcode) {\n";
    unsigned TableIndex = 0;
    for (const auto &Entry : OperandMap) {
      for (const std::string &Name : Entry.second)
        OS << "  case " << Name << ":\n";

      OS << "    return OperandMap[" << TableIndex++ << "][NamedIdx];\n";
    }
    OS << "    default: return -1;\n";
    OS << "  }\n";
  } else {
    // There are no operands, so no need to emit anything
    OS << "  return -1;\n";
  }
  OS << "}\n";
  OS << "} // end namespace " << Namespace << "\n";
  OS << "} // end namespace llvm\n";
  OS << "#endif //GET_INSTRINFO_NAMED_OPS\n\n";
}

/// Generate an enum for all the operand types for this target, under the
/// llvm::TargetNamespace::OpTypes namespace.
/// Operand types are all definitions derived of the Operand Target.td class.
void InstrInfoEmitter::emitOperandTypesEnum(raw_ostream &OS,
                                            const CodeGenTarget &Target) {

  StringRef Namespace = Target.getInstNamespace();
  std::vector<Record *> Operands = Records.getAllDerivedDefinitions("Operand");

  OS << "#ifdef GET_INSTRINFO_OPERAND_TYPES_ENUM\n";
  OS << "#undef GET_INSTRINFO_OPERAND_TYPES_ENUM\n";
  OS << "namespace llvm {\n";
  OS << "namespace " << Namespace << " {\n";
  OS << "namespace OpTypes {\n";
  OS << "enum OperandType {\n";

  unsigned EnumVal = 0;
  for (const Record *Op : Operands) {
    if (!Op->isAnonymous())
      OS << "  " << Op->getName() << " = " << EnumVal << ",\n";
    ++EnumVal;
  }

  OS << "  OPERAND_TYPE_LIST_END" << "\n};\n";
  OS << "} // end namespace OpTypes\n";
  OS << "} // end namespace " << Namespace << "\n";
  OS << "} // end namespace llvm\n";
  OS << "#endif // GET_INSTRINFO_OPERAND_TYPES_ENUM\n\n";
}

void InstrInfoEmitter::emitMCIIHelperMethods(raw_ostream &OS,
                                             StringRef TargetName) {
  RecVec TIIPredicates = Records.getAllDerivedDefinitions("TIIPredicate");
  if (TIIPredicates.empty())
    return;

  OS << "#ifdef GET_INSTRINFO_MC_HELPER_DECLS\n";
  OS << "#undef GET_INSTRINFO_MC_HELPER_DECLS\n\n";

  OS << "namespace llvm {\n";
  OS << "class MCInst;\n\n";

  OS << "namespace " << TargetName << "_MC {\n\n";

  for (const Record *Rec : TIIPredicates) {
    OS << "bool " << Rec->getValueAsString("FunctionName")
        << "(const MCInst &MI);\n";
  }

  OS << "\n} // end " << TargetName << "_MC namespace\n";
  OS << "} // end llvm namespace\n\n";

  OS << "#endif // GET_INSTRINFO_MC_HELPER_DECLS\n\n";

  OS << "#ifdef GET_INSTRINFO_MC_HELPERS\n";
  OS << "#undef GET_INSTRINFO_MC_HELPERS\n\n";

  OS << "namespace llvm {\n";
  OS << "namespace " << TargetName << "_MC {\n\n";

  PredicateExpander PE(TargetName);
  PE.setExpandForMC(true);

  for (const Record *Rec : TIIPredicates) {
    OS << "bool " << Rec->getValueAsString("FunctionName");
    OS << "(const MCInst &MI) {\n";

    OS.indent(PE.getIndentLevel() * 2);
    PE.expandStatement(OS, Rec->getValueAsDef("Body"));
    OS << "\n}\n\n";
  }

  OS << "} // end " << TargetName << "_MC namespace\n";
  OS << "} // end llvm namespace\n\n";

  OS << "#endif // GET_GENISTRINFO_MC_HELPERS\n";
}

void InstrInfoEmitter::emitTIIHelperMethods(raw_ostream &OS,
                                            StringRef TargetName,
                                            bool ExpandDefinition) {
  RecVec TIIPredicates = Records.getAllDerivedDefinitions("TIIPredicate");
  if (TIIPredicates.empty())
    return;

  PredicateExpander PE(TargetName);
  PE.setExpandForMC(false);

  for (const Record *Rec : TIIPredicates) {
    OS << (ExpandDefinition ? "" : "static ") << "bool ";
    if (ExpandDefinition)
      OS << TargetName << "InstrInfo::";
    OS << Rec->getValueAsString("FunctionName");
    OS << "(const MachineInstr &MI)";
    if (!ExpandDefinition) {
      OS << ";\n";
      continue;
    }

    OS << " {\n";
    OS.indent(PE.getIndentLevel() * 2);
    PE.expandStatement(OS, Rec->getValueAsDef("Body"));
    OS << "\n}\n\n";
  }
}

//===----------------------------------------------------------------------===//
// Main Output.
//===----------------------------------------------------------------------===//

// run - Emit the main instruction description records for the target...
void InstrInfoEmitter::run(raw_ostream &OS) {
  emitSourceFileHeader("Target Instruction Enum Values and Descriptors", OS);
  emitEnums(OS);

  OS << "#ifdef GET_INSTRINFO_MC_DESC\n";
  OS << "#undef GET_INSTRINFO_MC_DESC\n";

  OS << "namespace llvm {\n\n";

  CodeGenTarget &Target = CDP.getTargetInfo();
  const std::string &TargetName = Target.getName();
  Record *InstrInfo = Target.getInstructionSet();

  // Keep track of all of the def lists we have emitted already.
  std::map<std::vector<Record*>, unsigned> EmittedLists;
  unsigned ListNumber = 0;

  // Emit all of the instruction's implicit uses and defs.
  for (const CodeGenInstruction *II : Target.getInstructionsByEnumValue()) {
    Record *Inst = II->TheDef;
    std::vector<Record*> Uses = Inst->getValueAsListOfDefs("Uses");
    if (!Uses.empty()) {
      unsigned &IL = EmittedLists[Uses];
      if (!IL) PrintDefList(Uses, IL = ++ListNumber, OS);
    }
    std::vector<Record*> Defs = Inst->getValueAsListOfDefs("Defs");
    if (!Defs.empty()) {
      unsigned &IL = EmittedLists[Defs];
      if (!IL) PrintDefList(Defs, IL = ++ListNumber, OS);
    }
  }

  OperandInfoMapTy OperandInfoIDs;

  // Emit all of the operand info records.
  EmitOperandInfo(OS, OperandInfoIDs);

  // Emit all of the MCInstrDesc records in their ENUM ordering.
  //
  OS << "\nextern const MCInstrDesc " << TargetName << "Insts[] = {\n";
  ArrayRef<const CodeGenInstruction*> NumberedInstructions =
    Target.getInstructionsByEnumValue();

  SequenceToOffsetTable<std::string> InstrNames;
  unsigned Num = 0;
  for (const CodeGenInstruction *Inst : NumberedInstructions) {
    // Keep a list of the instruction names.
    InstrNames.add(Inst->TheDef->getName());
    // Emit the record into the table.
    emitRecord(*Inst, Num++, InstrInfo, EmittedLists, OperandInfoIDs, OS);
  }
  OS << "};\n\n";

  // Emit the array of instruction names.
  InstrNames.layout();
  OS << "extern const char " << TargetName << "InstrNameData[] = {\n";
  InstrNames.emit(OS, printChar);
  OS << "};\n\n";

  OS << "extern const unsigned " << TargetName <<"InstrNameIndices[] = {";
  Num = 0;
  for (const CodeGenInstruction *Inst : NumberedInstructions) {
    // Newline every eight entries.
    if (Num % 8 == 0)
      OS << "\n    ";
    OS << InstrNames.get(Inst->TheDef->getName()) << "U, ";
    ++Num;
  }

  OS << "\n};\n\n";

  // MCInstrInfo initialization routine.
  OS << "static inline void Init" << TargetName
     << "MCInstrInfo(MCInstrInfo *II) {\n";
  OS << "  II->InitMCInstrInfo(" << TargetName << "Insts, "
     << TargetName << "InstrNameIndices, " << TargetName << "InstrNameData, "
     << NumberedInstructions.size() << ");\n}\n\n";

  OS << "} // end llvm namespace\n";

  OS << "#endif // GET_INSTRINFO_MC_DESC\n\n";

  // Create a TargetInstrInfo subclass to hide the MC layer initialization.
  OS << "#ifdef GET_INSTRINFO_HEADER\n";
  OS << "#undef GET_INSTRINFO_HEADER\n";

  std::string ClassName = TargetName + "GenInstrInfo";
  OS << "namespace llvm {\n";
  OS << "struct " << ClassName << " : public TargetInstrInfo {\n"
     << "  explicit " << ClassName
     << "(int CFSetupOpcode = -1, int CFDestroyOpcode = -1, int CatchRetOpcode = -1, int ReturnOpcode = -1);\n"
     << "  ~" << ClassName << "() override = default;\n";


  OS << "\n};\n} // end llvm namespace\n";

  OS << "#endif // GET_INSTRINFO_HEADER\n\n";

  OS << "#ifdef GET_INSTRINFO_HELPER_DECLS\n";
  OS << "#undef GET_INSTRINFO_HELPER_DECLS\n\n";
  emitTIIHelperMethods(OS, TargetName, /* ExpandDefintion = */false);
  OS << "\n";
  OS << "#endif // GET_INSTRINFO_HELPER_DECLS\n\n";

  OS << "#ifdef GET_INSTRINFO_HELPERS\n";
  OS << "#undef GET_INSTRINFO_HELPERS\n\n";
  emitTIIHelperMethods(OS, TargetName, /* ExpandDefintion = */true);
  OS << "#endif // GET_INSTRINFO_HELPERS\n\n";

  OS << "#ifdef GET_INSTRINFO_CTOR_DTOR\n";
  OS << "#undef GET_INSTRINFO_CTOR_DTOR\n";

  OS << "namespace llvm {\n";
  OS << "extern const MCInstrDesc " << TargetName << "Insts[];\n";
  OS << "extern const unsigned " << TargetName << "InstrNameIndices[];\n";
  OS << "extern const char " << TargetName << "InstrNameData[];\n";
  OS << ClassName << "::" << ClassName
     << "(int CFSetupOpcode, int CFDestroyOpcode, int CatchRetOpcode, int ReturnOpcode)\n"
     << "  : TargetInstrInfo(CFSetupOpcode, CFDestroyOpcode, CatchRetOpcode, ReturnOpcode) {\n"
     << "  InitMCInstrInfo(" << TargetName << "Insts, " << TargetName
     << "InstrNameIndices, " << TargetName << "InstrNameData, "
     << NumberedInstructions.size() << ");\n}\n";
  OS << "} // end llvm namespace\n";

  OS << "#endif // GET_INSTRINFO_CTOR_DTOR\n\n";

  emitOperandNameMappings(OS, Target, NumberedInstructions);

  emitOperandTypesEnum(OS, Target);

  emitMCIIHelperMethods(OS, TargetName);
}

void InstrInfoEmitter::emitRecord(const CodeGenInstruction &Inst, unsigned Num,
                                  Record *InstrInfo,
                         std::map<std::vector<Record*>, unsigned> &EmittedLists,
                                  const OperandInfoMapTy &OpInfo,
                                  raw_ostream &OS) {
  int MinOperands = 0;
  if (!Inst.Operands.empty())
    // Each logical operand can be multiple MI operands.
    MinOperands = Inst.Operands.back().MIOperandNo +
                  Inst.Operands.back().MINumOperands;

  OS << "  { ";
  OS << Num << ",\t" << MinOperands << ",\t"
     << Inst.Operands.NumDefs << ",\t"
     << Inst.TheDef->getValueAsInt("Size") << ",\t"
     << SchedModels.getSchedClassIdx(Inst) << ",\t0";

  CodeGenTarget &Target = CDP.getTargetInfo();

  // Emit all of the target independent flags...
  if (Inst.isPseudo)           OS << "|(1ULL<<MCID::Pseudo)";
  if (Inst.isReturn)           OS << "|(1ULL<<MCID::Return)";
  if (Inst.isEHScopeReturn)    OS << "|(1ULL<<MCID::EHScopeReturn)";
  if (Inst.isBranch)           OS << "|(1ULL<<MCID::Branch)";
  if (Inst.isIndirectBranch)   OS << "|(1ULL<<MCID::IndirectBranch)";
  if (Inst.isCompare)          OS << "|(1ULL<<MCID::Compare)";
  if (Inst.isMoveImm)          OS << "|(1ULL<<MCID::MoveImm)";
  if (Inst.isMoveReg)          OS << "|(1ULL<<MCID::MoveReg)";
  if (Inst.isBitcast)          OS << "|(1ULL<<MCID::Bitcast)";
  if (Inst.isAdd)              OS << "|(1ULL<<MCID::Add)";
  if (Inst.isTrap)             OS << "|(1ULL<<MCID::Trap)";
  if (Inst.isSelect)           OS << "|(1ULL<<MCID::Select)";
  if (Inst.isBarrier)          OS << "|(1ULL<<MCID::Barrier)";
  if (Inst.hasDelaySlot)       OS << "|(1ULL<<MCID::DelaySlot)";
  if (Inst.isCall)             OS << "|(1ULL<<MCID::Call)";
  if (Inst.canFoldAsLoad)      OS << "|(1ULL<<MCID::FoldableAsLoad)";
  if (Inst.mayLoad)            OS << "|(1ULL<<MCID::MayLoad)";
  if (Inst.mayStore)           OS << "|(1ULL<<MCID::MayStore)";
  if (Inst.isPredicable)       OS << "|(1ULL<<MCID::Predicable)";
  if (Inst.isConvertibleToThreeAddress) OS << "|(1ULL<<MCID::ConvertibleTo3Addr)";
  if (Inst.isCommutable)       OS << "|(1ULL<<MCID::Commutable)";
  if (Inst.isTerminator)       OS << "|(1ULL<<MCID::Terminator)";
  if (Inst.isReMaterializable) OS << "|(1ULL<<MCID::Rematerializable)";
  if (Inst.isNotDuplicable)    OS << "|(1ULL<<MCID::NotDuplicable)";
  if (Inst.Operands.hasOptionalDef) OS << "|(1ULL<<MCID::HasOptionalDef)";
  if (Inst.usesCustomInserter) OS << "|(1ULL<<MCID::UsesCustomInserter)";
  if (Inst.hasPostISelHook)    OS << "|(1ULL<<MCID::HasPostISelHook)";
  if (Inst.Operands.isVariadic)OS << "|(1ULL<<MCID::Variadic)";
  if (Inst.hasSideEffects)     OS << "|(1ULL<<MCID::UnmodeledSideEffects)";
  if (Inst.isAsCheapAsAMove)   OS << "|(1ULL<<MCID::CheapAsAMove)";
  if (!Target.getAllowRegisterRenaming() || Inst.hasExtraSrcRegAllocReq)
    OS << "|(1ULL<<MCID::ExtraSrcRegAllocReq)";
  if (!Target.getAllowRegisterRenaming() || Inst.hasExtraDefRegAllocReq)
    OS << "|(1ULL<<MCID::ExtraDefRegAllocReq)";
  if (Inst.isRegSequence) OS << "|(1ULL<<MCID::RegSequence)";
  if (Inst.isExtractSubreg) OS << "|(1ULL<<MCID::ExtractSubreg)";
  if (Inst.isInsertSubreg) OS << "|(1ULL<<MCID::InsertSubreg)";
  if (Inst.isConvergent) OS << "|(1ULL<<MCID::Convergent)";
  if (Inst.variadicOpsAreDefs) OS << "|(1ULL<<MCID::VariadicOpsAreDefs)";

  // Emit all of the target-specific flags...
  BitsInit *TSF = Inst.TheDef->getValueAsBitsInit("TSFlags");
  if (!TSF)
    PrintFatalError("no TSFlags?");
  uint64_t Value = 0;
  for (unsigned i = 0, e = TSF->getNumBits(); i != e; ++i) {
    if (const auto *Bit = dyn_cast<BitInit>(TSF->getBit(i)))
      Value |= uint64_t(Bit->getValue()) << i;
    else
      PrintFatalError("Invalid TSFlags bit in " + Inst.TheDef->getName());
  }
  OS << ", 0x";
  OS.write_hex(Value);
  OS << "ULL, ";

  // Emit the implicit uses and defs lists...
  std::vector<Record*> UseList = Inst.TheDef->getValueAsListOfDefs("Uses");
  if (UseList.empty())
    OS << "nullptr, ";
  else
    OS << "ImplicitList" << EmittedLists[UseList] << ", ";

  std::vector<Record*> DefList = Inst.TheDef->getValueAsListOfDefs("Defs");
  if (DefList.empty())
    OS << "nullptr, ";
  else
    OS << "ImplicitList" << EmittedLists[DefList] << ", ";

  // Emit the operand info.
  std::vector<std::string> OperandInfo = GetOperandInfo(Inst);
  if (OperandInfo.empty())
    OS << "nullptr";
  else
    OS << "OperandInfo" << OpInfo.find(OperandInfo)->second;

  if (Inst.HasComplexDeprecationPredicate)
    // Emit a function pointer to the complex predicate method.
    OS << ", -1 "
       << ",&get" << Inst.DeprecatedReason << "DeprecationInfo";
  else if (!Inst.DeprecatedReason.empty())
    // Emit the Subtarget feature.
    OS << ", " << Target.getInstNamespace() << "::" << Inst.DeprecatedReason
       << " ,nullptr";
  else
    // Instruction isn't deprecated.
    OS << ", -1 ,nullptr";

  OS << " },  // Inst #" << Num << " = " << Inst.TheDef->getName() << "\n";
}

// emitEnums - Print out enum values for all of the instructions.
void InstrInfoEmitter::emitEnums(raw_ostream &OS) {
  OS << "#ifdef GET_INSTRINFO_ENUM\n";
  OS << "#undef GET_INSTRINFO_ENUM\n";

  OS << "namespace llvm {\n\n";

  CodeGenTarget Target(Records);

  // We must emit the PHI opcode first...
  StringRef Namespace = Target.getInstNamespace();

  if (Namespace.empty())
    PrintFatalError("No instructions defined!");

  OS << "namespace " << Namespace << " {\n";
  OS << "  enum {\n";
  unsigned Num = 0;
  for (const CodeGenInstruction *Inst : Target.getInstructionsByEnumValue())
    OS << "    " << Inst->TheDef->getName() << "\t= " << Num++ << ",\n";
  OS << "    INSTRUCTION_LIST_END = " << Num << "\n";
  OS << "  };\n\n";
  OS << "} // end " << Namespace << " namespace\n";
  OS << "} // end llvm namespace\n";
  OS << "#endif // GET_INSTRINFO_ENUM\n\n";

  OS << "#ifdef GET_INSTRINFO_SCHED_ENUM\n";
  OS << "#undef GET_INSTRINFO_SCHED_ENUM\n";
  OS << "namespace llvm {\n\n";
  OS << "namespace " << Namespace << " {\n";
  OS << "namespace Sched {\n";
  OS << "  enum {\n";
  Num = 0;
  for (const auto &Class : SchedModels.explicit_classes())
    OS << "    " << Class.Name << "\t= " << Num++ << ",\n";
  OS << "    SCHED_LIST_END = " << Num << "\n";
  OS << "  };\n";
  OS << "} // end Sched namespace\n";
  OS << "} // end " << Namespace << " namespace\n";
  OS << "} // end llvm namespace\n";

  OS << "#endif // GET_INSTRINFO_SCHED_ENUM\n\n";
}

namespace llvm {

void EmitInstrInfo(RecordKeeper &RK, raw_ostream &OS) {
  InstrInfoEmitter(RK).run(OS);
  EmitMapTable(RK, OS);
}

} // end llvm namespace
