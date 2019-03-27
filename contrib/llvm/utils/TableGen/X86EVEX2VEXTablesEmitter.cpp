//===- utils/TableGen/X86EVEX2VEXTablesEmitter.cpp - X86 backend-*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// This tablegen backend is responsible for emitting the X86 backend EVEX2VEX
/// compression tables.
///
//===----------------------------------------------------------------------===//

#include "CodeGenTarget.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/TableGenBackend.h"

using namespace llvm;

namespace {

class X86EVEX2VEXTablesEmitter {
  RecordKeeper &Records;
  CodeGenTarget Target;

  // Hold all non-masked & non-broadcasted EVEX encoded instructions
  std::vector<const CodeGenInstruction *> EVEXInsts;
  // Hold all VEX encoded instructions. Divided into groups with same opcodes
  // to make the search more efficient
  std::map<uint64_t, std::vector<const CodeGenInstruction *>> VEXInsts;

  typedef std::pair<const CodeGenInstruction *, const CodeGenInstruction *> Entry;

  // Represent both compress tables
  std::vector<Entry> EVEX2VEX128;
  std::vector<Entry> EVEX2VEX256;

public:
  X86EVEX2VEXTablesEmitter(RecordKeeper &R) : Records(R), Target(R) {}

  // run - Output X86 EVEX2VEX tables.
  void run(raw_ostream &OS);

private:
  // Prints the given table as a C++ array of type
  // X86EvexToVexCompressTableEntry
  void printTable(const std::vector<Entry> &Table, raw_ostream &OS);
};

void X86EVEX2VEXTablesEmitter::printTable(const std::vector<Entry> &Table,
                                          raw_ostream &OS) {
  StringRef Size = (Table == EVEX2VEX128) ? "128" : "256";

  OS << "// X86 EVEX encoded instructions that have a VEX " << Size
     << " encoding\n"
     << "// (table format: <EVEX opcode, VEX-" << Size << " opcode>).\n"
     << "static const X86EvexToVexCompressTableEntry X86EvexToVex" << Size
     << "CompressTable[] = {\n"
     << "  // EVEX scalar with corresponding VEX.\n";

  // Print all entries added to the table
  for (auto Pair : Table) {
    OS << "  { X86::" << Pair.first->TheDef->getName()
       << ", X86::" << Pair.second->TheDef->getName() << " },\n";
  }

  OS << "};\n\n";
}

// Return true if the 2 BitsInits are equal
static inline bool equalBitsInits(const BitsInit *B1, const BitsInit *B2) {
  if (B1->getNumBits() != B2->getNumBits())
    PrintFatalError("Comparing two BitsInits with different sizes!");

  for (unsigned i = 0, e = B1->getNumBits(); i != e; ++i) {
    if (BitInit *Bit1 = dyn_cast<BitInit>(B1->getBit(i))) {
      if (BitInit *Bit2 = dyn_cast<BitInit>(B2->getBit(i))) {
        if (Bit1->getValue() != Bit2->getValue())
          return false;
      } else
        PrintFatalError("Invalid BitsInit bit");
    } else
      PrintFatalError("Invalid BitsInit bit");
  }
  return true;
}

// Calculates the integer value residing BitsInit object
static inline uint64_t getValueFromBitsInit(const BitsInit *B) {
  uint64_t Value = 0;
  for (unsigned i = 0, e = B->getNumBits(); i != e; ++i) {
    if (BitInit *Bit = dyn_cast<BitInit>(B->getBit(i)))
      Value |= uint64_t(Bit->getValue()) << i;
    else
      PrintFatalError("Invalid VectSize bit");
  }
  return Value;
}

// Function object - Operator() returns true if the given VEX instruction
// matches the EVEX instruction of this object.
class IsMatch {
  const CodeGenInstruction *EVEXInst;

public:
  IsMatch(const CodeGenInstruction *EVEXInst) : EVEXInst(EVEXInst) {}

  bool operator()(const CodeGenInstruction *VEXInst) {
    Record *RecE = EVEXInst->TheDef;
    Record *RecV = VEXInst->TheDef;
    uint64_t EVEX_W =
        getValueFromBitsInit(RecE->getValueAsBitsInit("VEX_WPrefix"));
    uint64_t VEX_W =
        getValueFromBitsInit(RecV->getValueAsBitsInit("VEX_WPrefix"));

    if (RecV->getValueAsDef("OpEnc")->getName().str() != "EncVEX" ||
        // VEX/EVEX fields
        RecV->getValueAsDef("OpPrefix") != RecE->getValueAsDef("OpPrefix") ||
        RecV->getValueAsDef("OpMap") != RecE->getValueAsDef("OpMap") ||
        RecV->getValueAsBit("hasVEX_4V") != RecE->getValueAsBit("hasVEX_4V") ||
        !equalBitsInits(RecV->getValueAsBitsInit("EVEX_LL"),
                        RecE->getValueAsBitsInit("EVEX_LL")) ||
        // Match is allowed if either is VEX_WIG, or they match, or EVEX
        // is VEX_W1X and VEX is VEX_W0.
        (!(EVEX_W == 2 || VEX_W == 2 || EVEX_W == VEX_W ||
           (EVEX_W == 3 && VEX_W == 0))) ||
        // Instruction's format
        RecV->getValueAsDef("Form") != RecE->getValueAsDef("Form") ||
        RecV->getValueAsBit("isAsmParserOnly") !=
            RecE->getValueAsBit("isAsmParserOnly"))
      return false;

    // This is needed for instructions with intrinsic version (_Int).
    // Where the only difference is the size of the operands.
    // For example: VUCOMISDZrm and Int_VUCOMISDrm
    // Also for instructions that their EVEX version was upgraded to work with
    // k-registers. For example VPCMPEQBrm (xmm output register) and
    // VPCMPEQBZ128rm (k register output register).
    for (unsigned i = 0, e = EVEXInst->Operands.size(); i < e; i++) {
      Record *OpRec1 = EVEXInst->Operands[i].Rec;
      Record *OpRec2 = VEXInst->Operands[i].Rec;

      if (OpRec1 == OpRec2)
        continue;

      if (isRegisterOperand(OpRec1) && isRegisterOperand(OpRec2)) {
        if (getRegOperandSize(OpRec1) != getRegOperandSize(OpRec2))
          return false;
      } else if (isMemoryOperand(OpRec1) && isMemoryOperand(OpRec2)) {
        return false;
      } else if (isImmediateOperand(OpRec1) && isImmediateOperand(OpRec2)) {
        if (OpRec1->getValueAsDef("Type") != OpRec2->getValueAsDef("Type"))
          return false;
      } else
        return false;
    }

    return true;
  }

private:
  static inline bool isRegisterOperand(const Record *Rec) {
    return Rec->isSubClassOf("RegisterClass") ||
           Rec->isSubClassOf("RegisterOperand");
  }

  static inline bool isMemoryOperand(const Record *Rec) {
    return Rec->isSubClassOf("Operand") &&
           Rec->getValueAsString("OperandType") == "OPERAND_MEMORY";
  }

  static inline bool isImmediateOperand(const Record *Rec) {
    return Rec->isSubClassOf("Operand") &&
           Rec->getValueAsString("OperandType") == "OPERAND_IMMEDIATE";
  }

  static inline unsigned int getRegOperandSize(const Record *RegRec) {
    if (RegRec->isSubClassOf("RegisterClass"))
      return RegRec->getValueAsInt("Alignment");
    if (RegRec->isSubClassOf("RegisterOperand"))
      return RegRec->getValueAsDef("RegClass")->getValueAsInt("Alignment");

    llvm_unreachable("Register operand's size not known!");
  }
};

void X86EVEX2VEXTablesEmitter::run(raw_ostream &OS) {
  emitSourceFileHeader("X86 EVEX2VEX tables", OS);

  ArrayRef<const CodeGenInstruction *> NumberedInstructions =
      Target.getInstructionsByEnumValue();

  for (const CodeGenInstruction *Inst : NumberedInstructions) {
    // Filter non-X86 instructions.
    if (!Inst->TheDef->isSubClassOf("X86Inst"))
      continue;

    // Add VEX encoded instructions to one of VEXInsts vectors according to
    // it's opcode.
    if (Inst->TheDef->getValueAsDef("OpEnc")->getName() == "EncVEX") {
      uint64_t Opcode = getValueFromBitsInit(Inst->TheDef->
                                             getValueAsBitsInit("Opcode"));
      VEXInsts[Opcode].push_back(Inst);
    }
    // Add relevant EVEX encoded instructions to EVEXInsts
    else if (Inst->TheDef->getValueAsDef("OpEnc")->getName() == "EncEVEX" &&
             !Inst->TheDef->getValueAsBit("hasEVEX_K") &&
             !Inst->TheDef->getValueAsBit("hasEVEX_B") &&
             getValueFromBitsInit(Inst->TheDef->
                                        getValueAsBitsInit("EVEX_LL")) != 2 &&
             !Inst->TheDef->getValueAsBit("notEVEX2VEXConvertible"))
      EVEXInsts.push_back(Inst);
  }

  for (const CodeGenInstruction *EVEXInst : EVEXInsts) {
    uint64_t Opcode = getValueFromBitsInit(EVEXInst->TheDef->
                                           getValueAsBitsInit("Opcode"));
    // For each EVEX instruction look for a VEX match in the appropriate vector
    // (instructions with the same opcode) using function object IsMatch.
    // Allow EVEX2VEXOverride to explicitly specify a match.
    const CodeGenInstruction *VEXInst = nullptr;
    if (!EVEXInst->TheDef->isValueUnset("EVEX2VEXOverride")) {
      StringRef AltInstStr =
        EVEXInst->TheDef->getValueAsString("EVEX2VEXOverride");
      Record *AltInstRec = Records.getDef(AltInstStr);
      assert(AltInstRec && "EVEX2VEXOverride instruction not found!");
      VEXInst = &Target.getInstruction(AltInstRec);
    } else {
      auto Match = llvm::find_if(VEXInsts[Opcode], IsMatch(EVEXInst));
      if (Match != VEXInsts[Opcode].end())
        VEXInst = *Match;
    }

    if (!VEXInst)
      continue;

    // In case a match is found add new entry to the appropriate table
    switch (getValueFromBitsInit(
        EVEXInst->TheDef->getValueAsBitsInit("EVEX_LL"))) {
    case 0:
      EVEX2VEX128.push_back(std::make_pair(EVEXInst, VEXInst)); // {0,0}
      break;
    case 1:
      EVEX2VEX256.push_back(std::make_pair(EVEXInst, VEXInst)); // {0,1}
      break;
    default:
      llvm_unreachable("Instruction's size not fit for the mapping!");
    }
  }

  // Print both tables
  printTable(EVEX2VEX128, OS);
  printTable(EVEX2VEX256, OS);
}
}

namespace llvm {
void EmitX86EVEX2VEXTables(RecordKeeper &RK, raw_ostream &OS) {
  X86EVEX2VEXTablesEmitter(RK).run(OS);
}
}
