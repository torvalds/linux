//===- AsmWriterInst.h - Classes encapsulating a printable inst -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These classes implement a parser for assembly strings.  The parser splits
// the string into operands, which can be literal strings (the constant bits of
// the string), actual operands (i.e., operands from the MachineInstr), and
// dynamically-generated text, specified by raw C++ code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_ASMWRITERINST_H
#define LLVM_UTILS_TABLEGEN_ASMWRITERINST_H

#include <string>
#include <vector>

namespace llvm {
  class CodeGenInstruction;
  class Record;

  struct AsmWriterOperand {
    enum OpType {
      // Output this text surrounded by quotes to the asm.
      isLiteralTextOperand,
      // This is the name of a routine to call to print the operand.
      isMachineInstrOperand,
      // Output this text verbatim to the asm writer.  It is code that
      // will output some text to the asm.
      isLiteralStatementOperand
    } OperandType;

    /// MiOpNo - For isMachineInstrOperand, this is the operand number of the
    /// machine instruction.
    unsigned MIOpNo;

    /// Str - For isLiteralTextOperand, this IS the literal text.  For
    /// isMachineInstrOperand, this is the PrinterMethodName for the operand..
    /// For isLiteralStatementOperand, this is the code to insert verbatim
    /// into the asm writer.
    std::string Str;

    /// MiModifier - For isMachineInstrOperand, this is the modifier string for
    /// an operand, specified with syntax like ${opname:modifier}.
    std::string MiModifier;

    // To make VS STL happy
    AsmWriterOperand(OpType op = isLiteralTextOperand):OperandType(op) {}

    AsmWriterOperand(const std::string &LitStr,
                     OpType op = isLiteralTextOperand)
    : OperandType(op), Str(LitStr) {}

    AsmWriterOperand(const std::string &Printer,
                     unsigned _MIOpNo,
                     const std::string &Modifier,
                     OpType op = isMachineInstrOperand)
    : OperandType(op), MIOpNo(_MIOpNo), Str(Printer), MiModifier(Modifier) {}

    bool operator!=(const AsmWriterOperand &Other) const {
      if (OperandType != Other.OperandType || Str != Other.Str) return true;
      if (OperandType == isMachineInstrOperand)
        return MIOpNo != Other.MIOpNo || MiModifier != Other.MiModifier;
      return false;
    }
    bool operator==(const AsmWriterOperand &Other) const {
      return !operator!=(Other);
    }

    /// getCode - Return the code that prints this operand.
    std::string getCode(bool PassSubtarget) const;
  };

  class AsmWriterInst {
  public:
    std::vector<AsmWriterOperand> Operands;
    const CodeGenInstruction *CGI;
    unsigned CGIIndex;

    AsmWriterInst(const CodeGenInstruction &CGI, unsigned CGIIndex,
                  unsigned Variant);

    /// MatchesAllButOneOp - If this instruction is exactly identical to the
    /// specified instruction except for one differing operand, return the
    /// differing operand number.  Otherwise return ~0.
    unsigned MatchesAllButOneOp(const AsmWriterInst &Other) const;

  private:
    void AddLiteralString(const std::string &Str) {
      // If the last operand was already a literal text string, append this to
      // it, otherwise add a new operand.
      if (!Operands.empty() &&
          Operands.back().OperandType == AsmWriterOperand::isLiteralTextOperand)
        Operands.back().Str.append(Str);
      else
        Operands.push_back(AsmWriterOperand(Str));
    }
  };
}

#endif
