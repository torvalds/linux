//===- utils/TableGen/X86FoldTablesEmitter.cpp - X86 backend-*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend is responsible for emitting the memory fold tables of
// the X86 backend instructions.
//
//===----------------------------------------------------------------------===//

#include "CodeGenTarget.h"
#include "X86RecognizableInstr.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/TableGenBackend.h"

using namespace llvm;

namespace {

// 3 possible strategies for the unfolding flag (TB_NO_REVERSE) of the
// manual added entries.
enum UnfoldStrategy {
  UNFOLD,     // Allow unfolding
  NO_UNFOLD,  // Prevent unfolding
  NO_STRATEGY // Make decision according to operands' sizes
};

// Represents an entry in the manual mapped instructions set.
struct ManualMapEntry {
  const char *RegInstStr;
  const char *MemInstStr;
  UnfoldStrategy Strategy;

  ManualMapEntry(const char *RegInstStr, const char *MemInstStr,
                 UnfoldStrategy Strategy = NO_STRATEGY)
      : RegInstStr(RegInstStr), MemInstStr(MemInstStr), Strategy(Strategy) {}
};

class IsMatch;

// List of instructions requiring explicitly aligned memory.
const char *ExplicitAlign[] = {"MOVDQA",  "MOVAPS",  "MOVAPD",  "MOVNTPS",
                               "MOVNTPD", "MOVNTDQ", "MOVNTDQA"};

// List of instructions NOT requiring explicit memory alignment.
const char *ExplicitUnalign[] = {"MOVDQU", "MOVUPS", "MOVUPD",
                                 "PCMPESTRM", "PCMPESTRI",
                                 "PCMPISTRM", "PCMPISTRI" };

// For manually mapping instructions that do not match by their encoding.
const ManualMapEntry ManualMapSet[] = {
    { "ADD16ri_DB",       "ADD16mi",         NO_UNFOLD  },
    { "ADD16ri8_DB",      "ADD16mi8",        NO_UNFOLD  },
    { "ADD16rr_DB",       "ADD16mr",         NO_UNFOLD  },
    { "ADD32ri_DB",       "ADD32mi",         NO_UNFOLD  },
    { "ADD32ri8_DB",      "ADD32mi8",        NO_UNFOLD  },
    { "ADD32rr_DB",       "ADD32mr",         NO_UNFOLD  },
    { "ADD64ri32_DB",     "ADD64mi32",       NO_UNFOLD  },
    { "ADD64ri8_DB",      "ADD64mi8",        NO_UNFOLD  },
    { "ADD64rr_DB",       "ADD64mr",         NO_UNFOLD  },
    { "ADD16rr_DB",       "ADD16rm",         NO_UNFOLD  },
    { "ADD32rr_DB",       "ADD32rm",         NO_UNFOLD  },
    { "ADD64rr_DB",       "ADD64rm",         NO_UNFOLD  },
    { "PUSH16r",          "PUSH16rmm",       UNFOLD },
    { "PUSH32r",          "PUSH32rmm",       UNFOLD },
    { "PUSH64r",          "PUSH64rmm",       UNFOLD },
    { "TAILJMPr",         "TAILJMPm",        UNFOLD },
    { "TAILJMPr64",       "TAILJMPm64",      UNFOLD },
    { "TAILJMPr64_REX",   "TAILJMPm64_REX",  UNFOLD },
};


static bool isExplicitAlign(const CodeGenInstruction *Inst) {
  return any_of(ExplicitAlign, [Inst](const char *InstStr) {
    return Inst->TheDef->getName().find(InstStr) != StringRef::npos;
  });
}

static bool isExplicitUnalign(const CodeGenInstruction *Inst) {
  return any_of(ExplicitUnalign, [Inst](const char *InstStr) {
    return Inst->TheDef->getName().find(InstStr) != StringRef::npos;
  });
}

class X86FoldTablesEmitter {
  RecordKeeper &Records;
  CodeGenTarget Target;

  // Represents an entry in the folding table
  class X86FoldTableEntry {
    const CodeGenInstruction *RegInst;
    const CodeGenInstruction *MemInst;

  public:
    bool CannotUnfold = false;
    bool IsLoad = false;
    bool IsStore = false;
    bool IsAligned = false;
    unsigned int Alignment = 0;

    X86FoldTableEntry(const CodeGenInstruction *RegInst,
                      const CodeGenInstruction *MemInst)
        : RegInst(RegInst), MemInst(MemInst) {}

    friend raw_ostream &operator<<(raw_ostream &OS,
                                   const X86FoldTableEntry &E) {
      OS << "{ X86::" << E.RegInst->TheDef->getName()
         << ", X86::" << E.MemInst->TheDef->getName() << ", ";

      if (E.IsLoad)
        OS << "TB_FOLDED_LOAD | ";
      if (E.IsStore)
        OS << "TB_FOLDED_STORE | ";
      if (E.CannotUnfold)
        OS << "TB_NO_REVERSE | ";
      if (E.IsAligned)
        OS << "TB_ALIGN_" << E.Alignment << " | ";

      OS << "0 },\n";

      return OS;
    }
  };

  typedef std::vector<X86FoldTableEntry> FoldTable;
  // std::vector for each folding table.
  // Table2Addr - Holds instructions which their memory form performs load+store
  // Table#i - Holds instructions which the their memory form perform a load OR
  //           a store,  and their #i'th operand is folded.
  FoldTable Table2Addr;
  FoldTable Table0;
  FoldTable Table1;
  FoldTable Table2;
  FoldTable Table3;
  FoldTable Table4;

public:
  X86FoldTablesEmitter(RecordKeeper &R) : Records(R), Target(R) {}

  // run - Generate the 6 X86 memory fold tables.
  void run(raw_ostream &OS);

private:
  // Decides to which table to add the entry with the given instructions.
  // S sets the strategy of adding the TB_NO_REVERSE flag.
  void updateTables(const CodeGenInstruction *RegInstr,
                    const CodeGenInstruction *MemInstr,
                    const UnfoldStrategy S = NO_STRATEGY);

  // Generates X86FoldTableEntry with the given instructions and fill it with
  // the appropriate flags - then adds it to Table.
  void addEntryWithFlags(FoldTable &Table, const CodeGenInstruction *RegInstr,
                         const CodeGenInstruction *MemInstr,
                         const UnfoldStrategy S, const unsigned int FoldedInd);

  // Print the given table as a static const C++ array of type
  // X86MemoryFoldTableEntry.
  void printTable(const FoldTable &Table, StringRef TableName,
                  raw_ostream &OS) {
    OS << "static const X86MemoryFoldTableEntry MemoryFold" << TableName
       << "[] = {\n";

    for (const X86FoldTableEntry &E : Table)
      OS << E;

    OS << "};\n";
  }
};

// Return true if one of the instruction's operands is a RST register class
static bool hasRSTRegClass(const CodeGenInstruction *Inst) {
  return any_of(Inst->Operands, [](const CGIOperandList::OperandInfo &OpIn) {
    return OpIn.Rec->getName() == "RST";
  });
}

// Return true if one of the instruction's operands is a ptr_rc_tailcall
static bool hasPtrTailcallRegClass(const CodeGenInstruction *Inst) {
  return any_of(Inst->Operands, [](const CGIOperandList::OperandInfo &OpIn) {
    return OpIn.Rec->getName() == "ptr_rc_tailcall";
  });
}

// Calculates the integer value representing the BitsInit object
static inline uint64_t getValueFromBitsInit(const BitsInit *B) {
  assert(B->getNumBits() <= sizeof(uint64_t) * 8 && "BitInits' too long!");

  uint64_t Value = 0;
  for (unsigned i = 0, e = B->getNumBits(); i != e; ++i) {
    BitInit *Bit = cast<BitInit>(B->getBit(i));
    Value |= uint64_t(Bit->getValue()) << i;
  }
  return Value;
}

// Returns true if the two given BitsInits represent the same integer value
static inline bool equalBitsInits(const BitsInit *B1, const BitsInit *B2) {
  if (B1->getNumBits() != B2->getNumBits())
    PrintFatalError("Comparing two BitsInits with different sizes!");

  for (unsigned i = 0, e = B1->getNumBits(); i != e; ++i) {
    BitInit *Bit1 = cast<BitInit>(B1->getBit(i));
    BitInit *Bit2 = cast<BitInit>(B2->getBit(i));
    if (Bit1->getValue() != Bit2->getValue())
      return false;
  }
  return true;
}

// Return the size of the register operand
static inline unsigned int getRegOperandSize(const Record *RegRec) {
  if (RegRec->isSubClassOf("RegisterOperand"))
    RegRec = RegRec->getValueAsDef("RegClass");
  if (RegRec->isSubClassOf("RegisterClass"))
    return RegRec->getValueAsListOfDefs("RegTypes")[0]->getValueAsInt("Size");

  llvm_unreachable("Register operand's size not known!");
}

// Return the size of the memory operand
static inline unsigned int
getMemOperandSize(const Record *MemRec, const bool IntrinsicSensitive = false) {
  if (MemRec->isSubClassOf("Operand")) {
    // Intrinsic memory instructions use ssmem/sdmem.
    if (IntrinsicSensitive &&
        (MemRec->getName() == "sdmem" || MemRec->getName() == "ssmem"))
      return 128;

    StringRef Name =
        MemRec->getValueAsDef("ParserMatchClass")->getValueAsString("Name");
    if (Name == "Mem8")
      return 8;
    if (Name == "Mem16")
      return 16;
    if (Name == "Mem32")
      return 32;
    if (Name == "Mem64")
      return 64;
    if (Name == "Mem80")
      return 80;
    if (Name == "Mem128")
      return 128;
    if (Name == "Mem256")
      return 256;
    if (Name == "Mem512")
      return 512;
  }

  llvm_unreachable("Memory operand's size not known!");
}

// Return true if the instruction defined as a register flavor.
static inline bool hasRegisterFormat(const Record *Inst) {
  const BitsInit *FormBits = Inst->getValueAsBitsInit("FormBits");
  uint64_t FormBitsNum = getValueFromBitsInit(FormBits);

  // Values from X86Local namespace defined in X86RecognizableInstr.cpp
  return FormBitsNum >= X86Local::MRMDestReg && FormBitsNum <= X86Local::MRM7r;
}

// Return true if the instruction defined as a memory flavor.
static inline bool hasMemoryFormat(const Record *Inst) {
  const BitsInit *FormBits = Inst->getValueAsBitsInit("FormBits");
  uint64_t FormBitsNum = getValueFromBitsInit(FormBits);

  // Values from X86Local namespace defined in X86RecognizableInstr.cpp
  return FormBitsNum >= X86Local::MRMDestMem && FormBitsNum <= X86Local::MRM7m;
}

static inline bool isNOREXRegClass(const Record *Op) {
  return Op->getName().find("_NOREX") != StringRef::npos;
}

static inline bool isRegisterOperand(const Record *Rec) {
  return Rec->isSubClassOf("RegisterClass") ||
         Rec->isSubClassOf("RegisterOperand") ||
         Rec->isSubClassOf("PointerLikeRegClass");
}

static inline bool isMemoryOperand(const Record *Rec) {
  return Rec->isSubClassOf("Operand") &&
         Rec->getValueAsString("OperandType") == "OPERAND_MEMORY";
}

static inline bool isImmediateOperand(const Record *Rec) {
  return Rec->isSubClassOf("Operand") &&
         Rec->getValueAsString("OperandType") == "OPERAND_IMMEDIATE";
}

// Get the alternative instruction pointed by "FoldGenRegForm" field.
static inline const CodeGenInstruction *
getAltRegInst(const CodeGenInstruction *I, const RecordKeeper &Records,
              const CodeGenTarget &Target) {

  StringRef AltRegInstStr = I->TheDef->getValueAsString("FoldGenRegForm");
  Record *AltRegInstRec = Records.getDef(AltRegInstStr);
  assert(AltRegInstRec &&
         "Alternative register form instruction def not found");
  CodeGenInstruction &AltRegInst = Target.getInstruction(AltRegInstRec);
  return &AltRegInst;
}

// Function object - Operator() returns true if the given VEX instruction
// matches the EVEX instruction of this object.
class IsMatch {
  const CodeGenInstruction *MemInst;

public:
  IsMatch(const CodeGenInstruction *Inst, const RecordKeeper &Records)
      : MemInst(Inst) {}

  bool operator()(const CodeGenInstruction *RegInst) {
    Record *MemRec = MemInst->TheDef;
    Record *RegRec = RegInst->TheDef;

    // Return false if one (at least) of the encoding fields of both
    // instructions do not match.
    if (RegRec->getValueAsDef("OpEnc") != MemRec->getValueAsDef("OpEnc") ||
        !equalBitsInits(RegRec->getValueAsBitsInit("Opcode"),
                        MemRec->getValueAsBitsInit("Opcode")) ||
        // VEX/EVEX fields
        RegRec->getValueAsDef("OpPrefix") !=
            MemRec->getValueAsDef("OpPrefix") ||
        RegRec->getValueAsDef("OpMap") != MemRec->getValueAsDef("OpMap") ||
        RegRec->getValueAsDef("OpSize") != MemRec->getValueAsDef("OpSize") ||
        RegRec->getValueAsDef("AdSize") != MemRec->getValueAsDef("AdSize") ||
        RegRec->getValueAsBit("hasVEX_4V") !=
            MemRec->getValueAsBit("hasVEX_4V") ||
        RegRec->getValueAsBit("hasEVEX_K") !=
            MemRec->getValueAsBit("hasEVEX_K") ||
        RegRec->getValueAsBit("hasEVEX_Z") !=
            MemRec->getValueAsBit("hasEVEX_Z") ||
        // EVEX_B means different things for memory and register forms.
        RegRec->getValueAsBit("hasEVEX_B") != 0 ||
        MemRec->getValueAsBit("hasEVEX_B") != 0 ||
        RegRec->getValueAsBit("hasEVEX_RC") !=
            MemRec->getValueAsBit("hasEVEX_RC") ||
        RegRec->getValueAsBit("hasREX_WPrefix") !=
            MemRec->getValueAsBit("hasREX_WPrefix") ||
        RegRec->getValueAsBit("hasLockPrefix") !=
            MemRec->getValueAsBit("hasLockPrefix") ||
        RegRec->getValueAsBit("hasNoTrackPrefix") !=
            MemRec->getValueAsBit("hasNoTrackPrefix") ||
        !equalBitsInits(RegRec->getValueAsBitsInit("EVEX_LL"),
                        MemRec->getValueAsBitsInit("EVEX_LL")) ||
        !equalBitsInits(RegRec->getValueAsBitsInit("VEX_WPrefix"),
                        MemRec->getValueAsBitsInit("VEX_WPrefix")) ||
        // Instruction's format - The register form's "Form" field should be
        // the opposite of the memory form's "Form" field.
        !areOppositeForms(RegRec->getValueAsBitsInit("FormBits"),
                          MemRec->getValueAsBitsInit("FormBits")) ||
        RegRec->getValueAsBit("isAsmParserOnly") !=
            MemRec->getValueAsBit("isAsmParserOnly"))
      return false;

    // Make sure the sizes of the operands of both instructions suit each other.
    // This is needed for instructions with intrinsic version (_Int).
    // Where the only difference is the size of the operands.
    // For example: VUCOMISDZrm and Int_VUCOMISDrm
    // Also for instructions that their EVEX version was upgraded to work with
    // k-registers. For example VPCMPEQBrm (xmm output register) and
    // VPCMPEQBZ128rm (k register output register).
    bool ArgFolded = false;
    unsigned MemOutSize = MemRec->getValueAsDag("OutOperandList")->getNumArgs();
    unsigned RegOutSize = RegRec->getValueAsDag("OutOperandList")->getNumArgs();
    unsigned MemInSize = MemRec->getValueAsDag("InOperandList")->getNumArgs();
    unsigned RegInSize = RegRec->getValueAsDag("InOperandList")->getNumArgs();

    // Instructions with one output in their memory form use the memory folded
    // operand as source and destination (Read-Modify-Write).
    unsigned RegStartIdx =
        (MemOutSize + 1 == RegOutSize) && (MemInSize == RegInSize) ? 1 : 0;

    for (unsigned i = 0, e = MemInst->Operands.size(); i < e; i++) {
      Record *MemOpRec = MemInst->Operands[i].Rec;
      Record *RegOpRec = RegInst->Operands[i + RegStartIdx].Rec;

      if (MemOpRec == RegOpRec)
        continue;

      if (isRegisterOperand(MemOpRec) && isRegisterOperand(RegOpRec)) {
        if (getRegOperandSize(MemOpRec) != getRegOperandSize(RegOpRec) ||
            isNOREXRegClass(MemOpRec) != isNOREXRegClass(RegOpRec))
          return false;
      } else if (isMemoryOperand(MemOpRec) && isMemoryOperand(RegOpRec)) {
        if (getMemOperandSize(MemOpRec) != getMemOperandSize(RegOpRec))
          return false;
      } else if (isImmediateOperand(MemOpRec) && isImmediateOperand(RegOpRec)) {
        if (MemOpRec->getValueAsDef("Type") != RegOpRec->getValueAsDef("Type"))
          return false;
      } else {
        // Only one operand can be folded.
        if (ArgFolded)
          return false;

        assert(isRegisterOperand(RegOpRec) && isMemoryOperand(MemOpRec));
        ArgFolded = true;
      }
    }

    return true;
  }

private:
  // Return true of the 2 given forms are the opposite of each other.
  bool areOppositeForms(const BitsInit *RegFormBits,
                        const BitsInit *MemFormBits) {
    uint64_t MemFormNum = getValueFromBitsInit(MemFormBits);
    uint64_t RegFormNum = getValueFromBitsInit(RegFormBits);

    if ((MemFormNum == X86Local::MRM0m && RegFormNum == X86Local::MRM0r) ||
        (MemFormNum == X86Local::MRM1m && RegFormNum == X86Local::MRM1r) ||
        (MemFormNum == X86Local::MRM2m && RegFormNum == X86Local::MRM2r) ||
        (MemFormNum == X86Local::MRM3m && RegFormNum == X86Local::MRM3r) ||
        (MemFormNum == X86Local::MRM4m && RegFormNum == X86Local::MRM4r) ||
        (MemFormNum == X86Local::MRM5m && RegFormNum == X86Local::MRM5r) ||
        (MemFormNum == X86Local::MRM6m && RegFormNum == X86Local::MRM6r) ||
        (MemFormNum == X86Local::MRM7m && RegFormNum == X86Local::MRM7r) ||
        (MemFormNum == X86Local::MRMXm && RegFormNum == X86Local::MRMXr) ||
        (MemFormNum == X86Local::MRMDestMem &&
         RegFormNum == X86Local::MRMDestReg) ||
        (MemFormNum == X86Local::MRMSrcMem &&
         RegFormNum == X86Local::MRMSrcReg) ||
        (MemFormNum == X86Local::MRMSrcMem4VOp3 &&
         RegFormNum == X86Local::MRMSrcReg4VOp3) ||
        (MemFormNum == X86Local::MRMSrcMemOp4 &&
         RegFormNum == X86Local::MRMSrcRegOp4))
      return true;

    return false;
  }
};

} // end anonymous namespace

void X86FoldTablesEmitter::addEntryWithFlags(FoldTable &Table,
                                             const CodeGenInstruction *RegInstr,
                                             const CodeGenInstruction *MemInstr,
                                             const UnfoldStrategy S,
                                             const unsigned int FoldedInd) {

  X86FoldTableEntry Result = X86FoldTableEntry(RegInstr, MemInstr);
  Record *RegRec = RegInstr->TheDef;
  Record *MemRec = MemInstr->TheDef;

  // Only table0 entries should explicitly specify a load or store flag.
  if (&Table == &Table0) {
    unsigned MemInOpsNum = MemRec->getValueAsDag("InOperandList")->getNumArgs();
    unsigned RegInOpsNum = RegRec->getValueAsDag("InOperandList")->getNumArgs();
    // If the instruction writes to the folded operand, it will appear as an
    // output in the register form instruction and as an input in the memory
    // form instruction.
    // If the instruction reads from the folded operand, it well appear as in
    // input in both forms.
    if (MemInOpsNum == RegInOpsNum)
      Result.IsLoad = true;
    else
      Result.IsStore = true;
  }

  Record *RegOpRec = RegInstr->Operands[FoldedInd].Rec;
  Record *MemOpRec = MemInstr->Operands[FoldedInd].Rec;

  // Unfolding code generates a load/store instruction according to the size of
  // the register in the register form instruction.
  // If the register's size is greater than the memory's operand size, do not
  // allow unfolding.
  if (S == UNFOLD)
    Result.CannotUnfold = false;
  else if (S == NO_UNFOLD)
    Result.CannotUnfold = true;
  else if (getRegOperandSize(RegOpRec) > getMemOperandSize(MemOpRec))
    Result.CannotUnfold = true; // S == NO_STRATEGY

  uint64_t Enc = getValueFromBitsInit(RegRec->getValueAsBitsInit("OpEncBits"));
  if (isExplicitAlign(RegInstr)) {
    // The instruction require explicitly aligned memory.
    BitsInit *VectSize = RegRec->getValueAsBitsInit("VectSize");
    uint64_t Value = getValueFromBitsInit(VectSize);
    Result.IsAligned = true;
    Result.Alignment = Value;
  } else if (Enc != X86Local::XOP && Enc != X86Local::VEX &&
             Enc != X86Local::EVEX) {
    // Instructions with VEX encoding do not require alignment.
    if (!isExplicitUnalign(RegInstr) && getMemOperandSize(MemOpRec) > 64) {
      // SSE packed vector instructions require a 16 byte alignment.
      Result.IsAligned = true;
      Result.Alignment = 16;
    }
  }

  Table.push_back(Result);
}

void X86FoldTablesEmitter::updateTables(const CodeGenInstruction *RegInstr,
                                        const CodeGenInstruction *MemInstr,
                                        const UnfoldStrategy S) {

  Record *RegRec = RegInstr->TheDef;
  Record *MemRec = MemInstr->TheDef;
  unsigned MemOutSize = MemRec->getValueAsDag("OutOperandList")->getNumArgs();
  unsigned RegOutSize = RegRec->getValueAsDag("OutOperandList")->getNumArgs();
  unsigned MemInSize = MemRec->getValueAsDag("InOperandList")->getNumArgs();
  unsigned RegInSize = RegRec->getValueAsDag("InOperandList")->getNumArgs();

  // Instructions which Read-Modify-Write should be added to Table2Addr.
  if (MemOutSize != RegOutSize && MemInSize == RegInSize) {
    addEntryWithFlags(Table2Addr, RegInstr, MemInstr, S, 0);
    return;
  }

  if (MemInSize == RegInSize && MemOutSize == RegOutSize) {
    // Load-Folding cases.
    // If the i'th register form operand is a register and the i'th memory form
    // operand is a memory operand, add instructions to Table#i.
    for (unsigned i = RegOutSize, e = RegInstr->Operands.size(); i < e; i++) {
      Record *RegOpRec = RegInstr->Operands[i].Rec;
      Record *MemOpRec = MemInstr->Operands[i].Rec;
      if (isRegisterOperand(RegOpRec) && isMemoryOperand(MemOpRec)) {
        switch (i) {
        case 0:
          addEntryWithFlags(Table0, RegInstr, MemInstr, S, 0);
          return;
        case 1:
          addEntryWithFlags(Table1, RegInstr, MemInstr, S, 1);
          return;
        case 2:
          addEntryWithFlags(Table2, RegInstr, MemInstr, S, 2);
          return;
        case 3:
          addEntryWithFlags(Table3, RegInstr, MemInstr, S, 3);
          return;
        case 4:
          addEntryWithFlags(Table4, RegInstr, MemInstr, S, 4);
          return;
        }
      }
    }
  } else if (MemInSize == RegInSize + 1 && MemOutSize + 1 == RegOutSize) {
    // Store-Folding cases.
    // If the memory form instruction performs a store, the *output*
    // register of the register form instructions disappear and instead a
    // memory *input* operand appears in the memory form instruction.
    // For example:
    //   MOVAPSrr => (outs VR128:$dst), (ins VR128:$src)
    //   MOVAPSmr => (outs), (ins f128mem:$dst, VR128:$src)
    Record *RegOpRec = RegInstr->Operands[RegOutSize - 1].Rec;
    Record *MemOpRec = MemInstr->Operands[RegOutSize - 1].Rec;
    if (isRegisterOperand(RegOpRec) && isMemoryOperand(MemOpRec) &&
        getRegOperandSize(RegOpRec) == getMemOperandSize(MemOpRec))
      addEntryWithFlags(Table0, RegInstr, MemInstr, S, 0);
  }

  return;
}

void X86FoldTablesEmitter::run(raw_ostream &OS) {
  emitSourceFileHeader("X86 fold tables", OS);

  // Holds all memory instructions
  std::vector<const CodeGenInstruction *> MemInsts;
  // Holds all register instructions - divided according to opcode.
  std::map<uint8_t, std::vector<const CodeGenInstruction *>> RegInsts;

  ArrayRef<const CodeGenInstruction *> NumberedInstructions =
      Target.getInstructionsByEnumValue();

  for (const CodeGenInstruction *Inst : NumberedInstructions) {
    if (!Inst->TheDef->getNameInit() || !Inst->TheDef->isSubClassOf("X86Inst"))
      continue;

    const Record *Rec = Inst->TheDef;

    // - Do not proceed if the instruction is marked as notMemoryFoldable.
    // - Instructions including RST register class operands are not relevant
    //   for memory folding (for further details check the explanation in
    //   lib/Target/X86/X86InstrFPStack.td file).
    // - Some instructions (listed in the manual map above) use the register
    //   class ptr_rc_tailcall, which can be of a size 32 or 64, to ensure
    //   safe mapping of these instruction we manually map them and exclude
    //   them from the automation.
    if (Rec->getValueAsBit("isMemoryFoldable") == false ||
        hasRSTRegClass(Inst) || hasPtrTailcallRegClass(Inst))
      continue;

    // Add all the memory form instructions to MemInsts, and all the register
    // form instructions to RegInsts[Opc], where Opc in the opcode of each
    // instructions. this helps reducing the runtime of the backend.
    if (hasMemoryFormat(Rec))
      MemInsts.push_back(Inst);
    else if (hasRegisterFormat(Rec)) {
      uint8_t Opc = getValueFromBitsInit(Rec->getValueAsBitsInit("Opcode"));
      RegInsts[Opc].push_back(Inst);
    }
  }

  // For each memory form instruction, try to find its register form
  // instruction.
  for (const CodeGenInstruction *MemInst : MemInsts) {
    uint8_t Opc =
        getValueFromBitsInit(MemInst->TheDef->getValueAsBitsInit("Opcode"));

    if (RegInsts.count(Opc) == 0)
      continue;

    // Two forms (memory & register) of the same instruction must have the same
    // opcode. try matching only with register form instructions with the same
    // opcode.
    std::vector<const CodeGenInstruction *> &OpcRegInsts =
        RegInsts.find(Opc)->second;

    auto Match = find_if(OpcRegInsts, IsMatch(MemInst, Records));
    if (Match != OpcRegInsts.end()) {
      const CodeGenInstruction *RegInst = *Match;
      // If the matched instruction has it's "FoldGenRegForm" set, map the
      // memory form instruction to the register form instruction pointed by
      // this field
      if (RegInst->TheDef->isValueUnset("FoldGenRegForm")) {
        updateTables(RegInst, MemInst);
      } else {
        const CodeGenInstruction *AltRegInst =
            getAltRegInst(RegInst, Records, Target);
        updateTables(AltRegInst, MemInst);
      }
      OpcRegInsts.erase(Match);
    }
  }

  // Add the manually mapped instructions listed above.
  for (const ManualMapEntry &Entry : ManualMapSet) {
    Record *RegInstIter = Records.getDef(Entry.RegInstStr);
    Record *MemInstIter = Records.getDef(Entry.MemInstStr);

    updateTables(&(Target.getInstruction(RegInstIter)),
                 &(Target.getInstruction(MemInstIter)), Entry.Strategy);
  }

  // Print all tables to raw_ostream OS.
  printTable(Table2Addr, "Table2Addr", OS);
  printTable(Table0, "Table0", OS);
  printTable(Table1, "Table1", OS);
  printTable(Table2, "Table2", OS);
  printTable(Table3, "Table3", OS);
  printTable(Table4, "Table4", OS);
}

namespace llvm {

void EmitX86FoldTables(RecordKeeper &RK, raw_ostream &OS) {
  X86FoldTablesEmitter(RK).run(OS);
}
} // namespace llvm
