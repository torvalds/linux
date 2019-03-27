//===- AsmWriterInst.h - Classes encapsulating a printable inst -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These classes implement a parser for assembly strings.
//
//===----------------------------------------------------------------------===//

#include "AsmWriterInst.h"
#include "CodeGenTarget.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;

static bool isIdentChar(char C) {
  return (C >= 'a' && C <= 'z') ||
  (C >= 'A' && C <= 'Z') ||
  (C >= '0' && C <= '9') ||
  C == '_';
}

std::string AsmWriterOperand::getCode(bool PassSubtarget) const {
  if (OperandType == isLiteralTextOperand) {
    if (Str.size() == 1)
      return "O << '" + Str + "';";
    return "O << \"" + Str + "\";";
  }

  if (OperandType == isLiteralStatementOperand)
    return Str;

  std::string Result = Str + "(MI";
  if (MIOpNo != ~0U)
    Result += ", " + utostr(MIOpNo);
  if (PassSubtarget)
    Result += ", STI";
  Result += ", O";
  if (!MiModifier.empty())
    Result += ", \"" + MiModifier + '"';
  return Result + ");";
}

/// ParseAsmString - Parse the specified Instruction's AsmString into this
/// AsmWriterInst.
///
AsmWriterInst::AsmWriterInst(const CodeGenInstruction &CGI, unsigned CGIIndex,
                             unsigned Variant)
    : CGI(&CGI), CGIIndex(CGIIndex) {

  // NOTE: Any extensions to this code need to be mirrored in the
  // AsmPrinter::printInlineAsm code that executes as compile time (assuming
  // that inline asm strings should also get the new feature)!
  std::string AsmString = CGI.FlattenAsmStringVariants(CGI.AsmString, Variant);
  std::string::size_type LastEmitted = 0;
  while (LastEmitted != AsmString.size()) {
    std::string::size_type DollarPos =
      AsmString.find_first_of("$\\", LastEmitted);
    if (DollarPos == std::string::npos) DollarPos = AsmString.size();

    // Emit a constant string fragment.
    if (DollarPos != LastEmitted) {
      for (; LastEmitted != DollarPos; ++LastEmitted)
        switch (AsmString[LastEmitted]) {
          case '\n':
            AddLiteralString("\\n");
            break;
          case '\t':
            AddLiteralString("\\t");
            break;
          case '"':
            AddLiteralString("\\\"");
            break;
          case '\\':
            AddLiteralString("\\\\");
            break;
          default:
            AddLiteralString(std::string(1, AsmString[LastEmitted]));
            break;
        }
    } else if (AsmString[DollarPos] == '\\') {
      if (DollarPos+1 != AsmString.size()) {
        if (AsmString[DollarPos+1] == 'n') {
          AddLiteralString("\\n");
        } else if (AsmString[DollarPos+1] == 't') {
          AddLiteralString("\\t");
        } else if (std::string("${|}\\").find(AsmString[DollarPos+1])
                   != std::string::npos) {
          AddLiteralString(std::string(1, AsmString[DollarPos+1]));
        } else {
          PrintFatalError("Non-supported escaped character found in instruction '" +
            CGI.TheDef->getName() + "'!");
        }
        LastEmitted = DollarPos+2;
        continue;
      }
    } else if (DollarPos+1 != AsmString.size() &&
               AsmString[DollarPos+1] == '$') {
      AddLiteralString("$");  // "$$" -> $
      LastEmitted = DollarPos+2;
    } else {
      // Get the name of the variable.
      std::string::size_type VarEnd = DollarPos+1;

      // handle ${foo}bar as $foo by detecting whether the character following
      // the dollar sign is a curly brace.  If so, advance VarEnd and DollarPos
      // so the variable name does not contain the leading curly brace.
      bool hasCurlyBraces = false;
      if (VarEnd < AsmString.size() && '{' == AsmString[VarEnd]) {
        hasCurlyBraces = true;
        ++DollarPos;
        ++VarEnd;
      }

      while (VarEnd < AsmString.size() && isIdentChar(AsmString[VarEnd]))
        ++VarEnd;
      StringRef VarName(AsmString.data()+DollarPos+1, VarEnd-DollarPos-1);

      // Modifier - Support ${foo:modifier} syntax, where "modifier" is passed
      // into printOperand.  Also support ${:feature}, which is passed into
      // PrintSpecial.
      std::string Modifier;

      // In order to avoid starting the next string at the terminating curly
      // brace, advance the end position past it if we found an opening curly
      // brace.
      if (hasCurlyBraces) {
        if (VarEnd >= AsmString.size())
          PrintFatalError("Reached end of string before terminating curly brace in '"
            + CGI.TheDef->getName() + "'");

        // Look for a modifier string.
        if (AsmString[VarEnd] == ':') {
          ++VarEnd;
          if (VarEnd >= AsmString.size())
            PrintFatalError("Reached end of string before terminating curly brace in '"
              + CGI.TheDef->getName() + "'");

          std::string::size_type ModifierStart = VarEnd;
          while (VarEnd < AsmString.size() && isIdentChar(AsmString[VarEnd]))
            ++VarEnd;
          Modifier = std::string(AsmString.begin()+ModifierStart,
                                 AsmString.begin()+VarEnd);
          if (Modifier.empty())
            PrintFatalError("Bad operand modifier name in '"+ CGI.TheDef->getName() + "'");
        }

        if (AsmString[VarEnd] != '}')
          PrintFatalError("Variable name beginning with '{' did not end with '}' in '"
            + CGI.TheDef->getName() + "'");
        ++VarEnd;
      }
      if (VarName.empty() && Modifier.empty())
        PrintFatalError("Stray '$' in '" + CGI.TheDef->getName() +
          "' asm string, maybe you want $$?");

      if (VarName.empty()) {
        // Just a modifier, pass this into PrintSpecial.
        Operands.emplace_back("PrintSpecial", ~0U, Modifier);
      } else {
        // Otherwise, normal operand.
        unsigned OpNo = CGI.Operands.getOperandNamed(VarName);
        CGIOperandList::OperandInfo OpInfo = CGI.Operands[OpNo];

        unsigned MIOp = OpInfo.MIOperandNo;
        Operands.emplace_back(OpInfo.PrinterMethodName, MIOp, Modifier);
      }
      LastEmitted = VarEnd;
    }
  }

  Operands.emplace_back("return;", AsmWriterOperand::isLiteralStatementOperand);
}

/// MatchesAllButOneOp - If this instruction is exactly identical to the
/// specified instruction except for one differing operand, return the differing
/// operand number.  If more than one operand mismatches, return ~1, otherwise
/// if the instructions are identical return ~0.
unsigned AsmWriterInst::MatchesAllButOneOp(const AsmWriterInst &Other)const{
  if (Operands.size() != Other.Operands.size()) return ~1;

  unsigned MismatchOperand = ~0U;
  for (unsigned i = 0, e = Operands.size(); i != e; ++i) {
    if (Operands[i] != Other.Operands[i]) {
      if (MismatchOperand != ~0U)  // Already have one mismatch?
        return ~1U;
      MismatchOperand = i;
    }
  }
  return MismatchOperand;
}
