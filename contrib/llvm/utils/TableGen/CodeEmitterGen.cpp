//===- CodeEmitterGen.cpp - Code Emitter Generator ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// CodeEmitterGen uses the descriptions of instructions and their fields to
// construct an automated code emitter: a function that, given a MachineInstr,
// returns the (currently, 32-bit unsigned) value of the instruction.
//
//===----------------------------------------------------------------------===//

#include "CodeGenInstruction.h"
#include "CodeGenTarget.h"
#include "SubtargetFeatureInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

class CodeEmitterGen {
  RecordKeeper &Records;

public:
  CodeEmitterGen(RecordKeeper &R) : Records(R) {}

  void run(raw_ostream &o);

private:
  int getVariableBit(const std::string &VarName, BitsInit *BI, int bit);
  std::string getInstructionCase(Record *R, CodeGenTarget &Target);
  void AddCodeToMergeInOperand(Record *R, BitsInit *BI,
                               const std::string &VarName,
                               unsigned &NumberedOp,
                               std::set<unsigned> &NamedOpIndices,
                               std::string &Case, CodeGenTarget &Target);

};

// If the VarBitInit at position 'bit' matches the specified variable then
// return the variable bit position.  Otherwise return -1.
int CodeEmitterGen::getVariableBit(const std::string &VarName,
                                   BitsInit *BI, int bit) {
  if (VarBitInit *VBI = dyn_cast<VarBitInit>(BI->getBit(bit))) {
    if (VarInit *VI = dyn_cast<VarInit>(VBI->getBitVar()))
      if (VI->getName() == VarName)
        return VBI->getBitNum();
  } else if (VarInit *VI = dyn_cast<VarInit>(BI->getBit(bit))) {
    if (VI->getName() == VarName)
      return 0;
  }

  return -1;
}

void CodeEmitterGen::
AddCodeToMergeInOperand(Record *R, BitsInit *BI, const std::string &VarName,
                        unsigned &NumberedOp,
                        std::set<unsigned> &NamedOpIndices,
                        std::string &Case, CodeGenTarget &Target) {
  CodeGenInstruction &CGI = Target.getInstruction(R);

  // Determine if VarName actually contributes to the Inst encoding.
  int bit = BI->getNumBits()-1;

  // Scan for a bit that this contributed to.
  for (; bit >= 0; ) {
    if (getVariableBit(VarName, BI, bit) != -1)
      break;
    
    --bit;
  }
  
  // If we found no bits, ignore this value, otherwise emit the call to get the
  // operand encoding.
  if (bit < 0) return;
  
  // If the operand matches by name, reference according to that
  // operand number. Non-matching operands are assumed to be in
  // order.
  unsigned OpIdx;
  if (CGI.Operands.hasOperandNamed(VarName, OpIdx)) {
    // Get the machine operand number for the indicated operand.
    OpIdx = CGI.Operands[OpIdx].MIOperandNo;
    assert(!CGI.Operands.isFlatOperandNotEmitted(OpIdx) &&
           "Explicitly used operand also marked as not emitted!");
  } else {
    unsigned NumberOps = CGI.Operands.size();
    /// If this operand is not supposed to be emitted by the
    /// generated emitter, skip it.
    while (NumberedOp < NumberOps &&
           (CGI.Operands.isFlatOperandNotEmitted(NumberedOp) ||
              (!NamedOpIndices.empty() && NamedOpIndices.count(
                CGI.Operands.getSubOperandNumber(NumberedOp).first)))) {
      ++NumberedOp;

      if (NumberedOp >= CGI.Operands.back().MIOperandNo +
                        CGI.Operands.back().MINumOperands) {
        errs() << "Too few operands in record " << R->getName() <<
                  " (no match for variable " << VarName << "):\n";
        errs() << *R;
        errs() << '\n';

        return;
      }
    }

    OpIdx = NumberedOp++;
  }
  
  std::pair<unsigned, unsigned> SO = CGI.Operands.getSubOperandNumber(OpIdx);
  std::string &EncoderMethodName = CGI.Operands[SO.first].EncoderMethodName;
  
  // If the source operand has a custom encoder, use it. This will
  // get the encoding for all of the suboperands.
  if (!EncoderMethodName.empty()) {
    // A custom encoder has all of the information for the
    // sub-operands, if there are more than one, so only
    // query the encoder once per source operand.
    if (SO.second == 0) {
      Case += "      // op: " + VarName + "\n" +
              "      op = " + EncoderMethodName + "(MI, " + utostr(OpIdx);
      Case += ", Fixups, STI";
      Case += ");\n";
    }
  } else {
    Case += "      // op: " + VarName + "\n" +
      "      op = getMachineOpValue(MI, MI.getOperand(" + utostr(OpIdx) + ")";
    Case += ", Fixups, STI";
    Case += ");\n";
  }
  
  for (; bit >= 0; ) {
    int varBit = getVariableBit(VarName, BI, bit);
    
    // If this bit isn't from a variable, skip it.
    if (varBit == -1) {
      --bit;
      continue;
    }
    
    // Figure out the consecutive range of bits covered by this operand, in
    // order to generate better encoding code.
    int beginInstBit = bit;
    int beginVarBit = varBit;
    int N = 1;
    for (--bit; bit >= 0;) {
      varBit = getVariableBit(VarName, BI, bit);
      if (varBit == -1 || varBit != (beginVarBit - N)) break;
      ++N;
      --bit;
    }
     
    uint64_t opMask = ~(uint64_t)0 >> (64-N);
    int opShift = beginVarBit - N + 1;
    opMask <<= opShift;
    opShift = beginInstBit - beginVarBit;
    
    if (opShift > 0) {
      Case += "      Value |= (op & UINT64_C(" + utostr(opMask) + ")) << " +
              itostr(opShift) + ";\n";
    } else if (opShift < 0) {
      Case += "      Value |= (op & UINT64_C(" + utostr(opMask) + ")) >> " + 
              itostr(-opShift) + ";\n";
    } else {
      Case += "      Value |= op & UINT64_C(" + utostr(opMask) + ");\n";
    }
  }
}

std::string CodeEmitterGen::getInstructionCase(Record *R,
                                               CodeGenTarget &Target) {
  std::string Case;
  BitsInit *BI = R->getValueAsBitsInit("Inst");
  unsigned NumberedOp = 0;
  std::set<unsigned> NamedOpIndices;

  // Collect the set of operand indices that might correspond to named
  // operand, and skip these when assigning operands based on position.
  if (Target.getInstructionSet()->
       getValueAsBit("noNamedPositionallyEncodedOperands")) {
    CodeGenInstruction &CGI = Target.getInstruction(R);
    for (const RecordVal &RV : R->getValues()) {
      unsigned OpIdx;
      if (!CGI.Operands.hasOperandNamed(RV.getName(), OpIdx))
        continue;

      NamedOpIndices.insert(OpIdx);
    }
  }

  // Loop over all of the fields in the instruction, determining which are the
  // operands to the instruction.
  for (const RecordVal &RV : R->getValues()) { 
    // Ignore fixed fields in the record, we're looking for values like:
    //    bits<5> RST = { ?, ?, ?, ?, ? };
    if (RV.getPrefix() || RV.getValue()->isComplete())
      continue;
    
    AddCodeToMergeInOperand(R, BI, RV.getName(), NumberedOp,
                            NamedOpIndices, Case, Target);
  }

  StringRef PostEmitter = R->getValueAsString("PostEncoderMethod");
  if (!PostEmitter.empty()) {
    Case += "      Value = ";
    Case += PostEmitter;
    Case += "(MI, Value";
    Case += ", STI";
    Case += ");\n";
  }
  
  return Case;
}

void CodeEmitterGen::run(raw_ostream &o) {
  CodeGenTarget Target(Records);
  std::vector<Record*> Insts = Records.getAllDerivedDefinitions("Instruction");

  // For little-endian instruction bit encodings, reverse the bit order
  Target.reverseBitsForLittleEndianEncoding();

  ArrayRef<const CodeGenInstruction*> NumberedInstructions =
    Target.getInstructionsByEnumValue();

  // Emit function declaration
  o << "uint64_t " << Target.getName();
  o << "MCCodeEmitter::getBinaryCodeForInstr(const MCInst &MI,\n"
    << "    SmallVectorImpl<MCFixup> &Fixups,\n"
    << "    const MCSubtargetInfo &STI) const {\n";

  // Emit instruction base values
  o << "  static const uint64_t InstBits[] = {\n";
  for (const CodeGenInstruction *CGI : NumberedInstructions) {
    Record *R = CGI->TheDef;

    if (R->getValueAsString("Namespace") == "TargetOpcode" ||
        R->getValueAsBit("isPseudo")) {
      o << "    UINT64_C(0),\n";
      continue;
    }

    BitsInit *BI = R->getValueAsBitsInit("Inst");

    // Start by filling in fixed values.
    uint64_t Value = 0;
    for (unsigned i = 0, e = BI->getNumBits(); i != e; ++i) {
      if (BitInit *B = dyn_cast<BitInit>(BI->getBit(e-i-1)))
        Value |= (uint64_t)B->getValue() << (e-i-1);
    }
    o << "    UINT64_C(" << Value << ")," << '\t' << "// " << R->getName() << "\n";
  }
  o << "    UINT64_C(0)\n  };\n";

  // Map to accumulate all the cases.
  std::map<std::string, std::vector<std::string>> CaseMap;

  // Construct all cases statement for each opcode
  for (std::vector<Record*>::iterator IC = Insts.begin(), EC = Insts.end();
        IC != EC; ++IC) {
    Record *R = *IC;
    if (R->getValueAsString("Namespace") == "TargetOpcode" ||
        R->getValueAsBit("isPseudo"))
      continue;
    std::string InstName =
        (R->getValueAsString("Namespace") + "::" + R->getName()).str();
    std::string Case = getInstructionCase(R, Target);

    CaseMap[Case].push_back(std::move(InstName));
  }

  // Emit initial function code
  o << "  const unsigned opcode = MI.getOpcode();\n"
    << "  uint64_t Value = InstBits[opcode];\n"
    << "  uint64_t op = 0;\n"
    << "  (void)op;  // suppress warning\n"
    << "  switch (opcode) {\n";

  // Emit each case statement
  std::map<std::string, std::vector<std::string>>::iterator IE, EE;
  for (IE = CaseMap.begin(), EE = CaseMap.end(); IE != EE; ++IE) {
    const std::string &Case = IE->first;
    std::vector<std::string> &InstList = IE->second;

    for (int i = 0, N = InstList.size(); i < N; i++) {
      if (i) o << "\n";
      o << "    case " << InstList[i]  << ":";
    }
    o << " {\n";
    o << Case;
    o << "      break;\n"
      << "    }\n";
  }

  // Default case: unhandled opcode
  o << "  default:\n"
    << "    std::string msg;\n"
    << "    raw_string_ostream Msg(msg);\n"
    << "    Msg << \"Not supported instr: \" << MI;\n"
    << "    report_fatal_error(Msg.str());\n"
    << "  }\n"
    << "  return Value;\n"
    << "}\n\n";

  const auto &All = SubtargetFeatureInfo::getAll(Records);
  std::map<Record *, SubtargetFeatureInfo, LessRecordByID> SubtargetFeatures;
  SubtargetFeatures.insert(All.begin(), All.end());

  o << "#ifdef ENABLE_INSTR_PREDICATE_VERIFIER\n"
    << "#undef ENABLE_INSTR_PREDICATE_VERIFIER\n"
    << "#include <sstream>\n\n";

  // Emit the subtarget feature enumeration.
  SubtargetFeatureInfo::emitSubtargetFeatureFlagEnumeration(SubtargetFeatures,
                                                            o);

  // Emit the name table for error messages.
  o << "#ifndef NDEBUG\n";
  SubtargetFeatureInfo::emitNameTable(SubtargetFeatures, o);
  o << "#endif // NDEBUG\n";

  // Emit the available features compute function.
  SubtargetFeatureInfo::emitComputeAssemblerAvailableFeatures(
      Target.getName(), "MCCodeEmitter", "computeAvailableFeatures",
      SubtargetFeatures, o);

  // Emit the predicate verifier.
  o << "void " << Target.getName()
    << "MCCodeEmitter::verifyInstructionPredicates(\n"
    << "    const MCInst &Inst, uint64_t AvailableFeatures) const {\n"
    << "#ifndef NDEBUG\n"
    << "  static uint64_t RequiredFeatures[] = {\n";
  unsigned InstIdx = 0;
  for (const CodeGenInstruction *Inst : Target.getInstructionsByEnumValue()) {
    o << "    ";
    for (Record *Predicate : Inst->TheDef->getValueAsListOfDefs("Predicates")) {
      const auto &I = SubtargetFeatures.find(Predicate);
      if (I != SubtargetFeatures.end())
        o << I->second.getEnumName() << " | ";
    }
    o << "0, // " << Inst->TheDef->getName() << " = " << InstIdx << "\n";
    InstIdx++;
  }
  o << "  };\n\n";
  o << "  assert(Inst.getOpcode() < " << InstIdx << ");\n";
  o << "  uint64_t MissingFeatures =\n"
    << "      (AvailableFeatures & RequiredFeatures[Inst.getOpcode()]) ^\n"
    << "      RequiredFeatures[Inst.getOpcode()];\n"
    << "  if (MissingFeatures) {\n"
    << "    std::ostringstream Msg;\n"
    << "    Msg << \"Attempting to emit \" << "
       "MCII.getName(Inst.getOpcode()).str()\n"
    << "        << \" instruction but the \";\n"
    << "    for (unsigned i = 0; i < 8 * sizeof(MissingFeatures); ++i)\n"
    << "      if (MissingFeatures & (1ULL << i))\n"
    << "        Msg << SubtargetFeatureNames[i] << \" \";\n"
    << "    Msg << \"predicate(s) are not met\";\n"
    << "    report_fatal_error(Msg.str());\n"
    << "  }\n"
    << "#else\n"
    << "// Silence unused variable warning on targets that don't use MCII for "
       "other purposes (e.g. BPF).\n"
    << "(void)MCII;\n"
    << "#endif // NDEBUG\n";
  o << "}\n";
  o << "#endif\n";
}

} // end anonymous namespace

namespace llvm {

void EmitCodeEmitter(RecordKeeper &RK, raw_ostream &OS) {
  emitSourceFileHeader("Machine Code Emitter", OS);
  CodeEmitterGen(RK).run(OS);
}

} // end namespace llvm
