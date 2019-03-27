//===- AsmWriterEmitter.cpp - Generate an assembly writer -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits an assembly printer for the current target.
// Note that this is currently fairly skeletal, but will grow over time.
//
//===----------------------------------------------------------------------===//

#include "AsmWriterInst.h"
#include "CodeGenInstruction.h"
#include "CodeGenRegisters.h"
#include "CodeGenTarget.h"
#include "SequenceToOffsetTable.h"
#include "Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "asm-writer-emitter"

namespace {

class AsmWriterEmitter {
  RecordKeeper &Records;
  CodeGenTarget Target;
  ArrayRef<const CodeGenInstruction *> NumberedInstructions;
  std::vector<AsmWriterInst> Instructions;

public:
  AsmWriterEmitter(RecordKeeper &R);

  void run(raw_ostream &o);

private:
  void EmitPrintInstruction(raw_ostream &o);
  void EmitGetRegisterName(raw_ostream &o);
  void EmitPrintAliasInstruction(raw_ostream &O);

  void FindUniqueOperandCommands(std::vector<std::string> &UOC,
                                 std::vector<std::vector<unsigned>> &InstIdxs,
                                 std::vector<unsigned> &InstOpsUsed,
                                 bool PassSubtarget) const;
};

} // end anonymous namespace

static void PrintCases(std::vector<std::pair<std::string,
                       AsmWriterOperand>> &OpsToPrint, raw_ostream &O,
                       bool PassSubtarget) {
  O << "    case " << OpsToPrint.back().first << ":";
  AsmWriterOperand TheOp = OpsToPrint.back().second;
  OpsToPrint.pop_back();

  // Check to see if any other operands are identical in this list, and if so,
  // emit a case label for them.
  for (unsigned i = OpsToPrint.size(); i != 0; --i)
    if (OpsToPrint[i-1].second == TheOp) {
      O << "\n    case " << OpsToPrint[i-1].first << ":";
      OpsToPrint.erase(OpsToPrint.begin()+i-1);
    }

  // Finally, emit the code.
  O << "\n      " << TheOp.getCode(PassSubtarget);
  O << "\n      break;\n";
}

/// EmitInstructions - Emit the last instruction in the vector and any other
/// instructions that are suitably similar to it.
static void EmitInstructions(std::vector<AsmWriterInst> &Insts,
                             raw_ostream &O, bool PassSubtarget) {
  AsmWriterInst FirstInst = Insts.back();
  Insts.pop_back();

  std::vector<AsmWriterInst> SimilarInsts;
  unsigned DifferingOperand = ~0;
  for (unsigned i = Insts.size(); i != 0; --i) {
    unsigned DiffOp = Insts[i-1].MatchesAllButOneOp(FirstInst);
    if (DiffOp != ~1U) {
      if (DifferingOperand == ~0U)  // First match!
        DifferingOperand = DiffOp;

      // If this differs in the same operand as the rest of the instructions in
      // this class, move it to the SimilarInsts list.
      if (DifferingOperand == DiffOp || DiffOp == ~0U) {
        SimilarInsts.push_back(Insts[i-1]);
        Insts.erase(Insts.begin()+i-1);
      }
    }
  }

  O << "  case " << FirstInst.CGI->Namespace << "::"
    << FirstInst.CGI->TheDef->getName() << ":\n";
  for (const AsmWriterInst &AWI : SimilarInsts)
    O << "  case " << AWI.CGI->Namespace << "::"
      << AWI.CGI->TheDef->getName() << ":\n";
  for (unsigned i = 0, e = FirstInst.Operands.size(); i != e; ++i) {
    if (i != DifferingOperand) {
      // If the operand is the same for all instructions, just print it.
      O << "    " << FirstInst.Operands[i].getCode(PassSubtarget);
    } else {
      // If this is the operand that varies between all of the instructions,
      // emit a switch for just this operand now.
      O << "    switch (MI->getOpcode()) {\n";
      O << "    default: llvm_unreachable(\"Unexpected opcode.\");\n";
      std::vector<std::pair<std::string, AsmWriterOperand>> OpsToPrint;
      OpsToPrint.push_back(std::make_pair(FirstInst.CGI->Namespace.str() + "::" +
                                          FirstInst.CGI->TheDef->getName().str(),
                                          FirstInst.Operands[i]));

      for (const AsmWriterInst &AWI : SimilarInsts) {
        OpsToPrint.push_back(std::make_pair(AWI.CGI->Namespace.str()+"::" +
                                            AWI.CGI->TheDef->getName().str(),
                                            AWI.Operands[i]));
      }
      std::reverse(OpsToPrint.begin(), OpsToPrint.end());
      while (!OpsToPrint.empty())
        PrintCases(OpsToPrint, O, PassSubtarget);
      O << "    }";
    }
    O << "\n";
  }
  O << "    break;\n";
}

void AsmWriterEmitter::
FindUniqueOperandCommands(std::vector<std::string> &UniqueOperandCommands,
                          std::vector<std::vector<unsigned>> &InstIdxs,
                          std::vector<unsigned> &InstOpsUsed,
                          bool PassSubtarget) const {
  // This vector parallels UniqueOperandCommands, keeping track of which
  // instructions each case are used for.  It is a comma separated string of
  // enums.
  std::vector<std::string> InstrsForCase;
  InstrsForCase.resize(UniqueOperandCommands.size());
  InstOpsUsed.assign(UniqueOperandCommands.size(), 0);

  for (size_t i = 0, e = Instructions.size(); i != e; ++i) {
    const AsmWriterInst &Inst = Instructions[i];
    if (Inst.Operands.empty())
      continue;   // Instruction already done.

    std::string Command = "    "+Inst.Operands[0].getCode(PassSubtarget)+"\n";

    // Check to see if we already have 'Command' in UniqueOperandCommands.
    // If not, add it.
    auto I = llvm::find(UniqueOperandCommands, Command);
    if (I != UniqueOperandCommands.end()) {
      size_t idx = I - UniqueOperandCommands.begin();
      InstrsForCase[idx] += ", ";
      InstrsForCase[idx] += Inst.CGI->TheDef->getName();
      InstIdxs[idx].push_back(i);
    } else {
      UniqueOperandCommands.push_back(std::move(Command));
      InstrsForCase.push_back(Inst.CGI->TheDef->getName());
      InstIdxs.emplace_back();
      InstIdxs.back().push_back(i);

      // This command matches one operand so far.
      InstOpsUsed.push_back(1);
    }
  }

  // For each entry of UniqueOperandCommands, there is a set of instructions
  // that uses it.  If the next command of all instructions in the set are
  // identical, fold it into the command.
  for (size_t CommandIdx = 0, e = UniqueOperandCommands.size();
       CommandIdx != e; ++CommandIdx) {

    const auto &Idxs = InstIdxs[CommandIdx];

    for (unsigned Op = 1; ; ++Op) {
      // Find the first instruction in the set.
      const AsmWriterInst &FirstInst = Instructions[Idxs.front()];
      // If this instruction has no more operands, we isn't anything to merge
      // into this command.
      if (FirstInst.Operands.size() == Op)
        break;

      // Otherwise, scan to see if all of the other instructions in this command
      // set share the operand.
      if (std::any_of(Idxs.begin()+1, Idxs.end(),
                      [&](unsigned Idx) {
                        const AsmWriterInst &OtherInst = Instructions[Idx];
                        return OtherInst.Operands.size() == Op ||
                          OtherInst.Operands[Op] != FirstInst.Operands[Op];
                      }))
        break;

      // Okay, everything in this command set has the same next operand.  Add it
      // to UniqueOperandCommands and remember that it was consumed.
      std::string Command = "    " +
        FirstInst.Operands[Op].getCode(PassSubtarget) + "\n";

      UniqueOperandCommands[CommandIdx] += Command;
      InstOpsUsed[CommandIdx]++;
    }
  }

  // Prepend some of the instructions each case is used for onto the case val.
  for (unsigned i = 0, e = InstrsForCase.size(); i != e; ++i) {
    std::string Instrs = InstrsForCase[i];
    if (Instrs.size() > 70) {
      Instrs.erase(Instrs.begin()+70, Instrs.end());
      Instrs += "...";
    }

    if (!Instrs.empty())
      UniqueOperandCommands[i] = "    // " + Instrs + "\n" +
        UniqueOperandCommands[i];
  }
}

static void UnescapeString(std::string &Str) {
  for (unsigned i = 0; i != Str.size(); ++i) {
    if (Str[i] == '\\' && i != Str.size()-1) {
      switch (Str[i+1]) {
      default: continue;  // Don't execute the code after the switch.
      case 'a': Str[i] = '\a'; break;
      case 'b': Str[i] = '\b'; break;
      case 'e': Str[i] = 27; break;
      case 'f': Str[i] = '\f'; break;
      case 'n': Str[i] = '\n'; break;
      case 'r': Str[i] = '\r'; break;
      case 't': Str[i] = '\t'; break;
      case 'v': Str[i] = '\v'; break;
      case '"': Str[i] = '\"'; break;
      case '\'': Str[i] = '\''; break;
      case '\\': Str[i] = '\\'; break;
      }
      // Nuke the second character.
      Str.erase(Str.begin()+i+1);
    }
  }
}

/// EmitPrintInstruction - Generate the code for the "printInstruction" method
/// implementation. Destroys all instances of AsmWriterInst information, by
/// clearing the Instructions vector.
void AsmWriterEmitter::EmitPrintInstruction(raw_ostream &O) {
  Record *AsmWriter = Target.getAsmWriter();
  StringRef ClassName = AsmWriter->getValueAsString("AsmWriterClassName");
  bool PassSubtarget = AsmWriter->getValueAsInt("PassSubtarget");

  O <<
  "/// printInstruction - This method is automatically generated by tablegen\n"
  "/// from the instruction set description.\n"
    "void " << Target.getName() << ClassName
            << "::printInstruction(const MCInst *MI, "
            << (PassSubtarget ? "const MCSubtargetInfo &STI, " : "")
            << "raw_ostream &O) {\n";

  // Build an aggregate string, and build a table of offsets into it.
  SequenceToOffsetTable<std::string> StringTable;

  /// OpcodeInfo - This encodes the index of the string to use for the first
  /// chunk of the output as well as indices used for operand printing.
  std::vector<uint64_t> OpcodeInfo(NumberedInstructions.size());
  const unsigned OpcodeInfoBits = 64;

  // Add all strings to the string table upfront so it can generate an optimized
  // representation.
  for (AsmWriterInst &AWI : Instructions) {
    if (AWI.Operands[0].OperandType ==
                 AsmWriterOperand::isLiteralTextOperand &&
        !AWI.Operands[0].Str.empty()) {
      std::string Str = AWI.Operands[0].Str;
      UnescapeString(Str);
      StringTable.add(Str);
    }
  }

  StringTable.layout();

  unsigned MaxStringIdx = 0;
  for (AsmWriterInst &AWI : Instructions) {
    unsigned Idx;
    if (AWI.Operands[0].OperandType != AsmWriterOperand::isLiteralTextOperand ||
        AWI.Operands[0].Str.empty()) {
      // Something handled by the asmwriter printer, but with no leading string.
      Idx = StringTable.get("");
    } else {
      std::string Str = AWI.Operands[0].Str;
      UnescapeString(Str);
      Idx = StringTable.get(Str);
      MaxStringIdx = std::max(MaxStringIdx, Idx);

      // Nuke the string from the operand list.  It is now handled!
      AWI.Operands.erase(AWI.Operands.begin());
    }

    // Bias offset by one since we want 0 as a sentinel.
    OpcodeInfo[AWI.CGIIndex] = Idx+1;
  }

  // Figure out how many bits we used for the string index.
  unsigned AsmStrBits = Log2_32_Ceil(MaxStringIdx+2);

  // To reduce code size, we compactify common instructions into a few bits
  // in the opcode-indexed table.
  unsigned BitsLeft = OpcodeInfoBits-AsmStrBits;

  std::vector<std::vector<std::string>> TableDrivenOperandPrinters;

  while (true) {
    std::vector<std::string> UniqueOperandCommands;
    std::vector<std::vector<unsigned>> InstIdxs;
    std::vector<unsigned> NumInstOpsHandled;
    FindUniqueOperandCommands(UniqueOperandCommands, InstIdxs,
                              NumInstOpsHandled, PassSubtarget);

    // If we ran out of operands to print, we're done.
    if (UniqueOperandCommands.empty()) break;

    // Compute the number of bits we need to represent these cases, this is
    // ceil(log2(numentries)).
    unsigned NumBits = Log2_32_Ceil(UniqueOperandCommands.size());

    // If we don't have enough bits for this operand, don't include it.
    if (NumBits > BitsLeft) {
      LLVM_DEBUG(errs() << "Not enough bits to densely encode " << NumBits
                        << " more bits\n");
      break;
    }

    // Otherwise, we can include this in the initial lookup table.  Add it in.
    for (size_t i = 0, e = InstIdxs.size(); i != e; ++i) {
      unsigned NumOps = NumInstOpsHandled[i];
      for (unsigned Idx : InstIdxs[i]) {
        OpcodeInfo[Instructions[Idx].CGIIndex] |=
          (uint64_t)i << (OpcodeInfoBits-BitsLeft);
        // Remove the info about this operand from the instruction.
        AsmWriterInst &Inst = Instructions[Idx];
        if (!Inst.Operands.empty()) {
          assert(NumOps <= Inst.Operands.size() &&
                 "Can't remove this many ops!");
          Inst.Operands.erase(Inst.Operands.begin(),
                              Inst.Operands.begin()+NumOps);
        }
      }
    }
    BitsLeft -= NumBits;

    // Remember the handlers for this set of operands.
    TableDrivenOperandPrinters.push_back(std::move(UniqueOperandCommands));
  }

  // Emit the string table itself.
  O << "  static const char AsmStrs[] = {\n";
  StringTable.emit(O, printChar);
  O << "  };\n\n";

  // Emit the lookup tables in pieces to minimize wasted bytes.
  unsigned BytesNeeded = ((OpcodeInfoBits - BitsLeft) + 7) / 8;
  unsigned Table = 0, Shift = 0;
  SmallString<128> BitsString;
  raw_svector_ostream BitsOS(BitsString);
  // If the total bits is more than 32-bits we need to use a 64-bit type.
  BitsOS << "  uint" << ((BitsLeft < (OpcodeInfoBits - 32)) ? 64 : 32)
         << "_t Bits = 0;\n";
  while (BytesNeeded != 0) {
    // Figure out how big this table section needs to be, but no bigger than 4.
    unsigned TableSize = std::min(1 << Log2_32(BytesNeeded), 4);
    BytesNeeded -= TableSize;
    TableSize *= 8; // Convert to bits;
    uint64_t Mask = (1ULL << TableSize) - 1;
    O << "  static const uint" << TableSize << "_t OpInfo" << Table
      << "[] = {\n";
    for (unsigned i = 0, e = NumberedInstructions.size(); i != e; ++i) {
      O << "    " << ((OpcodeInfo[i] >> Shift) & Mask) << "U,\t// "
        << NumberedInstructions[i]->TheDef->getName() << "\n";
    }
    O << "  };\n\n";
    // Emit string to combine the individual table lookups.
    BitsOS << "  Bits |= ";
    // If the total bits is more than 32-bits we need to use a 64-bit type.
    if (BitsLeft < (OpcodeInfoBits - 32))
      BitsOS << "(uint64_t)";
    BitsOS << "OpInfo" << Table << "[MI->getOpcode()] << " << Shift << ";\n";
    // Prepare the shift for the next iteration and increment the table count.
    Shift += TableSize;
    ++Table;
  }

  // Emit the initial tab character.
  O << "  O << \"\\t\";\n\n";

  O << "  // Emit the opcode for the instruction.\n";
  O << BitsString;

  // Emit the starting string.
  O << "  assert(Bits != 0 && \"Cannot print this instruction.\");\n"
    << "  O << AsmStrs+(Bits & " << (1 << AsmStrBits)-1 << ")-1;\n\n";

  // Output the table driven operand information.
  BitsLeft = OpcodeInfoBits-AsmStrBits;
  for (unsigned i = 0, e = TableDrivenOperandPrinters.size(); i != e; ++i) {
    std::vector<std::string> &Commands = TableDrivenOperandPrinters[i];

    // Compute the number of bits we need to represent these cases, this is
    // ceil(log2(numentries)).
    unsigned NumBits = Log2_32_Ceil(Commands.size());
    assert(NumBits <= BitsLeft && "consistency error");

    // Emit code to extract this field from Bits.
    O << "\n  // Fragment " << i << " encoded into " << NumBits
      << " bits for " << Commands.size() << " unique commands.\n";

    if (Commands.size() == 2) {
      // Emit two possibilitys with if/else.
      O << "  if ((Bits >> "
        << (OpcodeInfoBits-BitsLeft) << ") & "
        << ((1 << NumBits)-1) << ") {\n"
        << Commands[1]
        << "  } else {\n"
        << Commands[0]
        << "  }\n\n";
    } else if (Commands.size() == 1) {
      // Emit a single possibility.
      O << Commands[0] << "\n\n";
    } else {
      O << "  switch ((Bits >> "
        << (OpcodeInfoBits-BitsLeft) << ") & "
        << ((1 << NumBits)-1) << ") {\n"
        << "  default: llvm_unreachable(\"Invalid command number.\");\n";

      // Print out all the cases.
      for (unsigned j = 0, e = Commands.size(); j != e; ++j) {
        O << "  case " << j << ":\n";
        O << Commands[j];
        O << "    break;\n";
      }
      O << "  }\n\n";
    }
    BitsLeft -= NumBits;
  }

  // Okay, delete instructions with no operand info left.
  auto I = llvm::remove_if(Instructions,
                     [](AsmWriterInst &Inst) { return Inst.Operands.empty(); });
  Instructions.erase(I, Instructions.end());


  // Because this is a vector, we want to emit from the end.  Reverse all of the
  // elements in the vector.
  std::reverse(Instructions.begin(), Instructions.end());


  // Now that we've emitted all of the operand info that fit into 64 bits, emit
  // information for those instructions that are left.  This is a less dense
  // encoding, but we expect the main 64-bit table to handle the majority of
  // instructions.
  if (!Instructions.empty()) {
    // Find the opcode # of inline asm.
    O << "  switch (MI->getOpcode()) {\n";
    O << "  default: llvm_unreachable(\"Unexpected opcode.\");\n";
    while (!Instructions.empty())
      EmitInstructions(Instructions, O, PassSubtarget);

    O << "  }\n";
  }

  O << "}\n";
}

static void
emitRegisterNameString(raw_ostream &O, StringRef AltName,
                       const std::deque<CodeGenRegister> &Registers) {
  SequenceToOffsetTable<std::string> StringTable;
  SmallVector<std::string, 4> AsmNames(Registers.size());
  unsigned i = 0;
  for (const auto &Reg : Registers) {
    std::string &AsmName = AsmNames[i++];

    // "NoRegAltName" is special. We don't need to do a lookup for that,
    // as it's just a reference to the default register name.
    if (AltName == "" || AltName == "NoRegAltName") {
      AsmName = Reg.TheDef->getValueAsString("AsmName");
      if (AsmName.empty())
        AsmName = Reg.getName();
    } else {
      // Make sure the register has an alternate name for this index.
      std::vector<Record*> AltNameList =
        Reg.TheDef->getValueAsListOfDefs("RegAltNameIndices");
      unsigned Idx = 0, e;
      for (e = AltNameList.size();
           Idx < e && (AltNameList[Idx]->getName() != AltName);
           ++Idx)
        ;
      // If the register has an alternate name for this index, use it.
      // Otherwise, leave it empty as an error flag.
      if (Idx < e) {
        std::vector<StringRef> AltNames =
          Reg.TheDef->getValueAsListOfStrings("AltNames");
        if (AltNames.size() <= Idx)
          PrintFatalError(Reg.TheDef->getLoc(),
                          "Register definition missing alt name for '" +
                          AltName + "'.");
        AsmName = AltNames[Idx];
      }
    }
    StringTable.add(AsmName);
  }

  StringTable.layout();
  O << "  static const char AsmStrs" << AltName << "[] = {\n";
  StringTable.emit(O, printChar);
  O << "  };\n\n";

  O << "  static const " << getMinimalTypeForRange(StringTable.size() - 1, 32)
    << " RegAsmOffset" << AltName << "[] = {";
  for (unsigned i = 0, e = Registers.size(); i != e; ++i) {
    if ((i % 14) == 0)
      O << "\n    ";
    O << StringTable.get(AsmNames[i]) << ", ";
  }
  O << "\n  };\n"
    << "\n";
}

void AsmWriterEmitter::EmitGetRegisterName(raw_ostream &O) {
  Record *AsmWriter = Target.getAsmWriter();
  StringRef ClassName = AsmWriter->getValueAsString("AsmWriterClassName");
  const auto &Registers = Target.getRegBank().getRegisters();
  const std::vector<Record*> &AltNameIndices = Target.getRegAltNameIndices();
  bool hasAltNames = AltNameIndices.size() > 1;
  StringRef Namespace = Registers.front().TheDef->getValueAsString("Namespace");

  O <<
  "\n\n/// getRegisterName - This method is automatically generated by tblgen\n"
  "/// from the register set description.  This returns the assembler name\n"
  "/// for the specified register.\n"
  "const char *" << Target.getName() << ClassName << "::";
  if (hasAltNames)
    O << "\ngetRegisterName(unsigned RegNo, unsigned AltIdx) {\n";
  else
    O << "getRegisterName(unsigned RegNo) {\n";
  O << "  assert(RegNo && RegNo < " << (Registers.size()+1)
    << " && \"Invalid register number!\");\n"
    << "\n";

  if (hasAltNames) {
    for (const Record *R : AltNameIndices)
      emitRegisterNameString(O, R->getName(), Registers);
  } else
    emitRegisterNameString(O, "", Registers);

  if (hasAltNames) {
    O << "  switch(AltIdx) {\n"
      << "  default: llvm_unreachable(\"Invalid register alt name index!\");\n";
    for (const Record *R : AltNameIndices) {
      StringRef AltName = R->getName();
      O << "  case ";
      if (!Namespace.empty())
        O << Namespace << "::";
      O << AltName << ":\n"
        << "    assert(*(AsmStrs" << AltName << "+RegAsmOffset" << AltName
        << "[RegNo-1]) &&\n"
        << "           \"Invalid alt name index for register!\");\n"
        << "    return AsmStrs" << AltName << "+RegAsmOffset" << AltName
        << "[RegNo-1];\n";
    }
    O << "  }\n";
  } else {
    O << "  assert (*(AsmStrs+RegAsmOffset[RegNo-1]) &&\n"
      << "          \"Invalid alt name index for register!\");\n"
      << "  return AsmStrs+RegAsmOffset[RegNo-1];\n";
  }
  O << "}\n";
}

namespace {

// IAPrinter - Holds information about an InstAlias. Two InstAliases match if
// they both have the same conditionals. In which case, we cannot print out the
// alias for that pattern.
class IAPrinter {
  std::vector<std::string> Conds;
  std::map<StringRef, std::pair<int, int>> OpMap;

  std::string Result;
  std::string AsmString;

public:
  IAPrinter(std::string R, std::string AS)
      : Result(std::move(R)), AsmString(std::move(AS)) {}

  void addCond(const std::string &C) { Conds.push_back(C); }

  void addOperand(StringRef Op, int OpIdx, int PrintMethodIdx = -1) {
    assert(OpIdx >= 0 && OpIdx < 0xFE && "Idx out of range");
    assert(PrintMethodIdx >= -1 && PrintMethodIdx < 0xFF &&
           "Idx out of range");
    OpMap[Op] = std::make_pair(OpIdx, PrintMethodIdx);
  }

  bool isOpMapped(StringRef Op) { return OpMap.find(Op) != OpMap.end(); }
  int getOpIndex(StringRef Op) { return OpMap[Op].first; }
  std::pair<int, int> &getOpData(StringRef Op) { return OpMap[Op]; }

  std::pair<StringRef, StringRef::iterator> parseName(StringRef::iterator Start,
                                                      StringRef::iterator End) {
    StringRef::iterator I = Start;
    StringRef::iterator Next;
    if (*I == '{') {
      // ${some_name}
      Start = ++I;
      while (I != End && *I != '}')
        ++I;
      Next = I;
      // eat the final '}'
      if (Next != End)
        ++Next;
    } else {
      // $name, just eat the usual suspects.
      while (I != End &&
             ((*I >= 'a' && *I <= 'z') || (*I >= 'A' && *I <= 'Z') ||
              (*I >= '0' && *I <= '9') || *I == '_'))
        ++I;
      Next = I;
    }

    return std::make_pair(StringRef(Start, I - Start), Next);
  }

  void print(raw_ostream &O) {
    if (Conds.empty()) {
      O.indent(6) << "return true;\n";
      return;
    }

    O << "if (";

    for (std::vector<std::string>::iterator
           I = Conds.begin(), E = Conds.end(); I != E; ++I) {
      if (I != Conds.begin()) {
        O << " &&\n";
        O.indent(8);
      }

      O << *I;
    }

    O << ") {\n";
    O.indent(6) << "// " << Result << "\n";

    // Directly mangle mapped operands into the string. Each operand is
    // identified by a '$' sign followed by a byte identifying the number of the
    // operand. We add one to the index to avoid zero bytes.
    StringRef ASM(AsmString);
    SmallString<128> OutString;
    raw_svector_ostream OS(OutString);
    for (StringRef::iterator I = ASM.begin(), E = ASM.end(); I != E;) {
      OS << *I;
      if (*I == '$') {
        StringRef Name;
        std::tie(Name, I) = parseName(++I, E);
        assert(isOpMapped(Name) && "Unmapped operand!");

        int OpIndex, PrintIndex;
        std::tie(OpIndex, PrintIndex) = getOpData(Name);
        if (PrintIndex == -1) {
          // Can use the default printOperand route.
          OS << format("\\x%02X", (unsigned char)OpIndex + 1);
        } else
          // 3 bytes if a PrintMethod is needed: 0xFF, the MCInst operand
          // number, and which of our pre-detected Methods to call.
          OS << format("\\xFF\\x%02X\\x%02X", OpIndex + 1, PrintIndex + 1);
      } else {
        ++I;
      }
    }

    // Emit the string.
    O.indent(6) << "AsmString = \"" << OutString << "\";\n";

    O.indent(6) << "break;\n";
    O.indent(4) << '}';
  }

  bool operator==(const IAPrinter &RHS) const {
    if (Conds.size() != RHS.Conds.size())
      return false;

    unsigned Idx = 0;
    for (const auto &str : Conds)
      if (str != RHS.Conds[Idx++])
        return false;

    return true;
  }
};

} // end anonymous namespace

static unsigned CountNumOperands(StringRef AsmString, unsigned Variant) {
  return AsmString.count(' ') + AsmString.count('\t');
}

namespace {

struct AliasPriorityComparator {
  typedef std::pair<CodeGenInstAlias, int> ValueType;
  bool operator()(const ValueType &LHS, const ValueType &RHS) const {
    if (LHS.second ==  RHS.second) {
      // We don't actually care about the order, but for consistency it
      // shouldn't depend on pointer comparisons.
      return LessRecordByID()(LHS.first.TheDef, RHS.first.TheDef);
    }

    // Aliases with larger priorities should be considered first.
    return LHS.second > RHS.second;
  }
};

} // end anonymous namespace

void AsmWriterEmitter::EmitPrintAliasInstruction(raw_ostream &O) {
  Record *AsmWriter = Target.getAsmWriter();

  O << "\n#ifdef PRINT_ALIAS_INSTR\n";
  O << "#undef PRINT_ALIAS_INSTR\n\n";

  //////////////////////////////
  // Gather information about aliases we need to print
  //////////////////////////////

  // Emit the method that prints the alias instruction.
  StringRef ClassName = AsmWriter->getValueAsString("AsmWriterClassName");
  unsigned Variant = AsmWriter->getValueAsInt("Variant");
  bool PassSubtarget = AsmWriter->getValueAsInt("PassSubtarget");

  std::vector<Record*> AllInstAliases =
    Records.getAllDerivedDefinitions("InstAlias");

  // Create a map from the qualified name to a list of potential matches.
  typedef std::set<std::pair<CodeGenInstAlias, int>, AliasPriorityComparator>
      AliasWithPriority;
  std::map<std::string, AliasWithPriority> AliasMap;
  for (Record *R : AllInstAliases) {
    int Priority = R->getValueAsInt("EmitPriority");
    if (Priority < 1)
      continue; // Aliases with priority 0 are never emitted.

    const DagInit *DI = R->getValueAsDag("ResultInst");
    const DefInit *Op = cast<DefInit>(DI->getOperator());
    AliasMap[getQualifiedName(Op->getDef())].insert(
        std::make_pair(CodeGenInstAlias(R, Target), Priority));
  }

  // A map of which conditions need to be met for each instruction operand
  // before it can be matched to the mnemonic.
  std::map<std::string, std::vector<IAPrinter>> IAPrinterMap;

  std::vector<std::string> PrintMethods;

  // A list of MCOperandPredicates for all operands in use, and the reverse map
  std::vector<const Record*> MCOpPredicates;
  DenseMap<const Record*, unsigned> MCOpPredicateMap;

  for (auto &Aliases : AliasMap) {
    for (auto &Alias : Aliases.second) {
      const CodeGenInstAlias &CGA = Alias.first;
      unsigned LastOpNo = CGA.ResultInstOperandIndex.size();
      std::string FlatInstAsmString =
         CodeGenInstruction::FlattenAsmStringVariants(CGA.ResultInst->AsmString,
                                                      Variant);
      unsigned NumResultOps = CountNumOperands(FlatInstAsmString, Variant);

      std::string FlatAliasAsmString =
        CodeGenInstruction::FlattenAsmStringVariants(CGA.AsmString,
                                                      Variant);

      // Don't emit the alias if it has more operands than what it's aliasing.
      if (NumResultOps < CountNumOperands(FlatAliasAsmString, Variant))
        continue;

      IAPrinter IAP(CGA.Result->getAsString(), FlatAliasAsmString);

      StringRef Namespace = Target.getName();
      std::vector<Record *> ReqFeatures;
      if (PassSubtarget) {
        // We only consider ReqFeatures predicates if PassSubtarget
        std::vector<Record *> RF =
            CGA.TheDef->getValueAsListOfDefs("Predicates");
        copy_if(RF, std::back_inserter(ReqFeatures), [](Record *R) {
          return R->getValueAsBit("AssemblerMatcherPredicate");
        });
      }

      unsigned NumMIOps = 0;
      for (auto &ResultInstOpnd : CGA.ResultInst->Operands)
        NumMIOps += ResultInstOpnd.MINumOperands;

      std::string Cond;
      Cond = std::string("MI->getNumOperands() == ") + utostr(NumMIOps);
      IAP.addCond(Cond);

      bool CantHandle = false;

      unsigned MIOpNum = 0;
      for (unsigned i = 0, e = LastOpNo; i != e; ++i) {
        // Skip over tied operands as they're not part of an alias declaration.
        auto &Operands = CGA.ResultInst->Operands;
        while (true) {
          unsigned OpNum = Operands.getSubOperandNumber(MIOpNum).first;
          if (Operands[OpNum].MINumOperands == 1 &&
              Operands[OpNum].getTiedRegister() != -1) {
            // Tied operands of different RegisterClass should be explicit within
            // an instruction's syntax and so cannot be skipped.
            int TiedOpNum = Operands[OpNum].getTiedRegister();
            if (Operands[OpNum].Rec->getName() ==
                Operands[TiedOpNum].Rec->getName()) {
              ++MIOpNum;
              continue;
            }
          }
          break;
        }

        std::string Op = "MI->getOperand(" + utostr(MIOpNum) + ")";

        const CodeGenInstAlias::ResultOperand &RO = CGA.ResultOperands[i];

        switch (RO.Kind) {
        case CodeGenInstAlias::ResultOperand::K_Record: {
          const Record *Rec = RO.getRecord();
          StringRef ROName = RO.getName();
          int PrintMethodIdx = -1;

          // These two may have a PrintMethod, which we want to record (if it's
          // the first time we've seen it) and provide an index for the aliasing
          // code to use.
          if (Rec->isSubClassOf("RegisterOperand") ||
              Rec->isSubClassOf("Operand")) {
            StringRef PrintMethod = Rec->getValueAsString("PrintMethod");
            if (PrintMethod != "" && PrintMethod != "printOperand") {
              PrintMethodIdx =
                  llvm::find(PrintMethods, PrintMethod) - PrintMethods.begin();
              if (static_cast<unsigned>(PrintMethodIdx) == PrintMethods.size())
                PrintMethods.push_back(PrintMethod);
            }
          }

          if (Rec->isSubClassOf("RegisterOperand"))
            Rec = Rec->getValueAsDef("RegClass");
          if (Rec->isSubClassOf("RegisterClass")) {
            IAP.addCond(Op + ".isReg()");

            if (!IAP.isOpMapped(ROName)) {
              IAP.addOperand(ROName, MIOpNum, PrintMethodIdx);
              Record *R = CGA.ResultOperands[i].getRecord();
              if (R->isSubClassOf("RegisterOperand"))
                R = R->getValueAsDef("RegClass");
              Cond = std::string("MRI.getRegClass(") + Target.getName().str() +
                     "::" + R->getName().str() + "RegClassID).contains(" + Op +
                     ".getReg())";
            } else {
              Cond = Op + ".getReg() == MI->getOperand(" +
                     utostr(IAP.getOpIndex(ROName)) + ").getReg()";
            }
          } else {
            // Assume all printable operands are desired for now. This can be
            // overridden in the InstAlias instantiation if necessary.
            IAP.addOperand(ROName, MIOpNum, PrintMethodIdx);

            // There might be an additional predicate on the MCOperand
            unsigned Entry = MCOpPredicateMap[Rec];
            if (!Entry) {
              if (!Rec->isValueUnset("MCOperandPredicate")) {
                MCOpPredicates.push_back(Rec);
                Entry = MCOpPredicates.size();
                MCOpPredicateMap[Rec] = Entry;
              } else
                break; // No conditions on this operand at all
            }
            Cond = (Target.getName() + ClassName + "ValidateMCOperand(" + Op +
                    ", STI, " + utostr(Entry) + ")")
                       .str();
          }
          // for all subcases of ResultOperand::K_Record:
          IAP.addCond(Cond);
          break;
        }
        case CodeGenInstAlias::ResultOperand::K_Imm: {
          // Just because the alias has an immediate result, doesn't mean the
          // MCInst will. An MCExpr could be present, for example.
          IAP.addCond(Op + ".isImm()");

          Cond = Op + ".getImm() == " + itostr(CGA.ResultOperands[i].getImm());
          IAP.addCond(Cond);
          break;
        }
        case CodeGenInstAlias::ResultOperand::K_Reg:
          // If this is zero_reg, something's playing tricks we're not
          // equipped to handle.
          if (!CGA.ResultOperands[i].getRegister()) {
            CantHandle = true;
            break;
          }

          Cond = Op + ".getReg() == " + Target.getName().str() + "::" +
                 CGA.ResultOperands[i].getRegister()->getName().str();
          IAP.addCond(Cond);
          break;
        }

        MIOpNum += RO.getMINumOperands();
      }

      if (CantHandle) continue;

      for (auto I = ReqFeatures.cbegin(); I != ReqFeatures.cend(); I++) {
        Record *R = *I;
        StringRef AsmCondString = R->getValueAsString("AssemblerCondString");

        // AsmCondString has syntax [!]F(,[!]F)*
        SmallVector<StringRef, 4> Ops;
        SplitString(AsmCondString, Ops, ",");
        assert(!Ops.empty() && "AssemblerCondString cannot be empty");

        for (auto &Op : Ops) {
          assert(!Op.empty() && "Empty operator");
          if (Op[0] == '!')
            Cond = ("!STI.getFeatureBits()[" + Namespace + "::" + Op.substr(1) +
                    "]")
                       .str();
          else
            Cond =
                ("STI.getFeatureBits()[" + Namespace + "::" + Op + "]").str();
          IAP.addCond(Cond);
        }
      }

      IAPrinterMap[Aliases.first].push_back(std::move(IAP));
    }
  }

  //////////////////////////////
  // Write out the printAliasInstr function
  //////////////////////////////

  std::string Header;
  raw_string_ostream HeaderO(Header);

  HeaderO << "bool " << Target.getName() << ClassName
          << "::printAliasInstr(const MCInst"
          << " *MI, " << (PassSubtarget ? "const MCSubtargetInfo &STI, " : "")
          << "raw_ostream &OS) {\n";

  std::string Cases;
  raw_string_ostream CasesO(Cases);

  for (auto &Entry : IAPrinterMap) {
    std::vector<IAPrinter> &IAPs = Entry.second;
    std::vector<IAPrinter*> UniqueIAPs;

    for (auto &LHS : IAPs) {
      bool IsDup = false;
      for (const auto &RHS : IAPs) {
        if (&LHS != &RHS && LHS == RHS) {
          IsDup = true;
          break;
        }
      }

      if (!IsDup)
        UniqueIAPs.push_back(&LHS);
    }

    if (UniqueIAPs.empty()) continue;

    CasesO.indent(2) << "case " << Entry.first << ":\n";

    for (IAPrinter *IAP : UniqueIAPs) {
      CasesO.indent(4);
      IAP->print(CasesO);
      CasesO << '\n';
    }

    CasesO.indent(4) << "return false;\n";
  }

  if (CasesO.str().empty()) {
    O << HeaderO.str();
    O << "  return false;\n";
    O << "}\n\n";
    O << "#endif // PRINT_ALIAS_INSTR\n";
    return;
  }

  if (!MCOpPredicates.empty())
    O << "static bool " << Target.getName() << ClassName
      << "ValidateMCOperand(const MCOperand &MCOp,\n"
      << "                  const MCSubtargetInfo &STI,\n"
      << "                  unsigned PredicateIndex);\n";

  O << HeaderO.str();
  O.indent(2) << "const char *AsmString;\n";
  O.indent(2) << "switch (MI->getOpcode()) {\n";
  O.indent(2) << "default: return false;\n";
  O << CasesO.str();
  O.indent(2) << "}\n\n";

  // Code that prints the alias, replacing the operands with the ones from the
  // MCInst.
  O << "  unsigned I = 0;\n";
  O << "  while (AsmString[I] != ' ' && AsmString[I] != '\\t' &&\n";
  O << "         AsmString[I] != '$' && AsmString[I] != '\\0')\n";
  O << "    ++I;\n";
  O << "  OS << '\\t' << StringRef(AsmString, I);\n";

  O << "  if (AsmString[I] != '\\0') {\n";
  O << "    if (AsmString[I] == ' ' || AsmString[I] == '\\t') {\n";
  O << "      OS << '\\t';\n";
  O << "      ++I;\n";
  O << "    }\n";
  O << "    do {\n";
  O << "      if (AsmString[I] == '$') {\n";
  O << "        ++I;\n";
  O << "        if (AsmString[I] == (char)0xff) {\n";
  O << "          ++I;\n";
  O << "          int OpIdx = AsmString[I++] - 1;\n";
  O << "          int PrintMethodIdx = AsmString[I++] - 1;\n";
  O << "          printCustomAliasOperand(MI, OpIdx, PrintMethodIdx, ";
  O << (PassSubtarget ? "STI, " : "");
  O << "OS);\n";
  O << "        } else\n";
  O << "          printOperand(MI, unsigned(AsmString[I++]) - 1, ";
  O << (PassSubtarget ? "STI, " : "");
  O << "OS);\n";
  O << "      } else {\n";
  O << "        OS << AsmString[I++];\n";
  O << "      }\n";
  O << "    } while (AsmString[I] != '\\0');\n";
  O << "  }\n\n";

  O << "  return true;\n";
  O << "}\n\n";

  //////////////////////////////
  // Write out the printCustomAliasOperand function
  //////////////////////////////

  O << "void " << Target.getName() << ClassName << "::"
    << "printCustomAliasOperand(\n"
    << "         const MCInst *MI, unsigned OpIdx,\n"
    << "         unsigned PrintMethodIdx,\n"
    << (PassSubtarget ? "         const MCSubtargetInfo &STI,\n" : "")
    << "         raw_ostream &OS) {\n";
  if (PrintMethods.empty())
    O << "  llvm_unreachable(\"Unknown PrintMethod kind\");\n";
  else {
    O << "  switch (PrintMethodIdx) {\n"
      << "  default:\n"
      << "    llvm_unreachable(\"Unknown PrintMethod kind\");\n"
      << "    break;\n";

    for (unsigned i = 0; i < PrintMethods.size(); ++i) {
      O << "  case " << i << ":\n"
        << "    " << PrintMethods[i] << "(MI, OpIdx, "
        << (PassSubtarget ? "STI, " : "") << "OS);\n"
        << "    break;\n";
    }
    O << "  }\n";
  }    
  O << "}\n\n";

  if (!MCOpPredicates.empty()) {
    O << "static bool " << Target.getName() << ClassName
      << "ValidateMCOperand(const MCOperand &MCOp,\n"
      << "                  const MCSubtargetInfo &STI,\n"
      << "                  unsigned PredicateIndex) {\n"      
      << "  switch (PredicateIndex) {\n"
      << "  default:\n"
      << "    llvm_unreachable(\"Unknown MCOperandPredicate kind\");\n"
      << "    break;\n";

    for (unsigned i = 0; i < MCOpPredicates.size(); ++i) {
      Init *MCOpPred = MCOpPredicates[i]->getValueInit("MCOperandPredicate");
      if (CodeInit *SI = dyn_cast<CodeInit>(MCOpPred)) {
        O << "  case " << i + 1 << ": {\n"
          << SI->getValue() << "\n"
          << "    }\n";
      } else
        llvm_unreachable("Unexpected MCOperandPredicate field!");
    }
    O << "  }\n"
      << "}\n\n";
  }

  O << "#endif // PRINT_ALIAS_INSTR\n";
}

AsmWriterEmitter::AsmWriterEmitter(RecordKeeper &R) : Records(R), Target(R) {
  Record *AsmWriter = Target.getAsmWriter();
  unsigned Variant = AsmWriter->getValueAsInt("Variant");

  // Get the instruction numbering.
  NumberedInstructions = Target.getInstructionsByEnumValue();

  for (unsigned i = 0, e = NumberedInstructions.size(); i != e; ++i) {
    const CodeGenInstruction *I = NumberedInstructions[i];
    if (!I->AsmString.empty() && I->TheDef->getName() != "PHI")
      Instructions.emplace_back(*I, i, Variant);
  }
}

void AsmWriterEmitter::run(raw_ostream &O) {
  EmitPrintInstruction(O);
  EmitGetRegisterName(O);
  EmitPrintAliasInstruction(O);
}

namespace llvm {

void EmitAsmWriter(RecordKeeper &RK, raw_ostream &OS) {
  emitSourceFileHeader("Assembly Writer Source Fragment", OS);
  AsmWriterEmitter(RK).run(OS);
}

} // end namespace llvm
