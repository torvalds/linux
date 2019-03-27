//===- X86RecognizableInstr.h - Disassembler instruction spec ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is part of the X86 Disassembler Emitter.
// It contains the interface of a single recognizable instruction.
// Documentation for the disassembler emitter in general can be found in
//  X86DisassemblerEmitter.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_X86RECOGNIZABLEINSTR_H
#define LLVM_UTILS_TABLEGEN_X86RECOGNIZABLEINSTR_H

#include "CodeGenTarget.h"
#include "X86DisassemblerTables.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/TableGen/Record.h"

namespace llvm {

#define X86_INSTR_MRM_MAPPING     \
  MAP(C0, 64)                     \
  MAP(C1, 65)                     \
  MAP(C2, 66)                     \
  MAP(C3, 67)                     \
  MAP(C4, 68)                     \
  MAP(C5, 69)                     \
  MAP(C6, 70)                     \
  MAP(C7, 71)                     \
  MAP(C8, 72)                     \
  MAP(C9, 73)                     \
  MAP(CA, 74)                     \
  MAP(CB, 75)                     \
  MAP(CC, 76)                     \
  MAP(CD, 77)                     \
  MAP(CE, 78)                     \
  MAP(CF, 79)                     \
  MAP(D0, 80)                     \
  MAP(D1, 81)                     \
  MAP(D2, 82)                     \
  MAP(D3, 83)                     \
  MAP(D4, 84)                     \
  MAP(D5, 85)                     \
  MAP(D6, 86)                     \
  MAP(D7, 87)                     \
  MAP(D8, 88)                     \
  MAP(D9, 89)                     \
  MAP(DA, 90)                     \
  MAP(DB, 91)                     \
  MAP(DC, 92)                     \
  MAP(DD, 93)                     \
  MAP(DE, 94)                     \
  MAP(DF, 95)                     \
  MAP(E0, 96)                     \
  MAP(E1, 97)                     \
  MAP(E2, 98)                     \
  MAP(E3, 99)                     \
  MAP(E4, 100)                    \
  MAP(E5, 101)                    \
  MAP(E6, 102)                    \
  MAP(E7, 103)                    \
  MAP(E8, 104)                    \
  MAP(E9, 105)                    \
  MAP(EA, 106)                    \
  MAP(EB, 107)                    \
  MAP(EC, 108)                    \
  MAP(ED, 109)                    \
  MAP(EE, 110)                    \
  MAP(EF, 111)                    \
  MAP(F0, 112)                    \
  MAP(F1, 113)                    \
  MAP(F2, 114)                    \
  MAP(F3, 115)                    \
  MAP(F4, 116)                    \
  MAP(F5, 117)                    \
  MAP(F6, 118)                    \
  MAP(F7, 119)                    \
  MAP(F8, 120)                    \
  MAP(F9, 121)                    \
  MAP(FA, 122)                    \
  MAP(FB, 123)                    \
  MAP(FC, 124)                    \
  MAP(FD, 125)                    \
  MAP(FE, 126)                    \
  MAP(FF, 127)

// A clone of X86 since we can't depend on something that is generated.
namespace X86Local {
  enum {
    Pseudo        = 0,
    RawFrm        = 1,
    AddRegFrm     = 2,
    RawFrmMemOffs = 3,
    RawFrmSrc     = 4,
    RawFrmDst     = 5,
    RawFrmDstSrc  = 6,
    RawFrmImm8    = 7,
    RawFrmImm16   = 8,
    MRMDestMem     = 32,
    MRMSrcMem      = 33,
    MRMSrcMem4VOp3 = 34,
    MRMSrcMemOp4   = 35,
    MRMXm = 39,
    MRM0m = 40, MRM1m = 41, MRM2m = 42, MRM3m = 43,
    MRM4m = 44, MRM5m = 45, MRM6m = 46, MRM7m = 47,
    MRMDestReg     = 48,
    MRMSrcReg      = 49,
    MRMSrcReg4VOp3 = 50,
    MRMSrcRegOp4   = 51,
    MRMXr = 55,
    MRM0r = 56, MRM1r = 57, MRM2r = 58, MRM3r = 59,
    MRM4r = 60, MRM5r = 61, MRM6r = 62, MRM7r = 63,
#define MAP(from, to) MRM_##from = to,
    X86_INSTR_MRM_MAPPING
#undef MAP
  };

  enum {
    OB = 0, TB = 1, T8 = 2, TA = 3, XOP8 = 4, XOP9 = 5, XOPA = 6, ThreeDNow = 7
  };

  enum {
    PD = 1, XS = 2, XD = 3, PS = 4
  };

  enum {
    VEX = 1, XOP = 2, EVEX = 3
  };

  enum {
    OpSize16 = 1, OpSize32 = 2
  };

  enum {
    AdSize16 = 1, AdSize32 = 2, AdSize64 = 3
  };

  enum {
    VEX_W0 = 0, VEX_W1 = 1, VEX_WIG = 2, VEX_W1X = 3
  };
}

namespace X86Disassembler {

/// RecognizableInstr - Encapsulates all information required to decode a single
///   instruction, as extracted from the LLVM instruction tables.  Has methods
///   to interpret the information available in the LLVM tables, and to emit the
///   instruction into DisassemblerTables.
class RecognizableInstr {
private:
  /// The opcode of the instruction, as used in an MCInst
  InstrUID UID;
  /// The record from the .td files corresponding to this instruction
  const Record* Rec;
  /// The OpPrefix field from the record
  uint8_t OpPrefix;
  /// The OpMap field from the record
  uint8_t OpMap;
  /// The opcode field from the record; this is the opcode used in the Intel
  /// encoding and therefore distinct from the UID
  uint8_t Opcode;
  /// The form field from the record
  uint8_t Form;
  // The encoding field from the record
  uint8_t Encoding;
  /// The OpSize field from the record
  uint8_t OpSize;
  /// The AdSize field from the record
  uint8_t AdSize;
  /// The hasREX_WPrefix field from the record
  bool HasREX_WPrefix;
  /// The hasVEX_4V field from the record
  bool HasVEX_4V;
  /// The VEX_WPrefix field from the record
  uint8_t VEX_WPrefix;
  /// Inferred from the operands; indicates whether the L bit in the VEX prefix is set
  bool HasVEX_LPrefix;
  /// The ignoreVEX_L field from the record
  bool IgnoresVEX_L;
  /// The hasEVEX_L2Prefix field from the record
  bool HasEVEX_L2Prefix;
  /// The hasEVEX_K field from the record
  bool HasEVEX_K;
  /// The hasEVEX_KZ field from the record
  bool HasEVEX_KZ;
  /// The hasEVEX_B field from the record
  bool HasEVEX_B;
  /// Indicates that the instruction uses the L and L' fields for RC.
  bool EncodeRC;
  /// The isCodeGenOnly field from the record
  bool IsCodeGenOnly;
  /// The ForceDisassemble field from the record
  bool ForceDisassemble;
  // The CD8_Scale field from the record
  uint8_t CD8_Scale;
  // Whether the instruction has the predicate "In64BitMode"
  bool Is64Bit;
  // Whether the instruction has the predicate "In32BitMode"
  bool Is32Bit;

  /// The instruction name as listed in the tables
  std::string Name;

  /// Indicates whether the instruction should be emitted into the decode
  /// tables; regardless, it will be emitted into the instruction info table
  bool ShouldBeEmitted;

  /// The operands of the instruction, as listed in the CodeGenInstruction.
  /// They are not one-to-one with operands listed in the MCInst; for example,
  /// memory operands expand to 5 operands in the MCInst
  const std::vector<CGIOperandList::OperandInfo>* Operands;

  /// The description of the instruction that is emitted into the instruction
  /// info table
  InstructionSpecifier* Spec;

  /// insnContext - Returns the primary context in which the instruction is
  ///   valid.
  ///
  /// @return - The context in which the instruction is valid.
  InstructionContext insnContext() const;

  /// typeFromString - Translates an operand type from the string provided in
  ///   the LLVM tables to an OperandType for use in the operand specifier.
  ///
  /// @param s              - The string, as extracted by calling Rec->getName()
  ///                         on a CodeGenInstruction::OperandInfo.
  /// @param hasREX_WPrefix - Indicates whether the instruction has a REX.W
  ///                         prefix.  If it does, 32-bit register operands stay
  ///                         32-bit regardless of the operand size.
  /// @param OpSize           Indicates the operand size of the instruction.
  ///                         If register size does not match OpSize, then
  ///                         register sizes keep their size.
  /// @return               - The operand's type.
  static OperandType typeFromString(const std::string& s,
                                    bool hasREX_WPrefix, uint8_t OpSize);

  /// immediateEncodingFromString - Translates an immediate encoding from the
  ///   string provided in the LLVM tables to an OperandEncoding for use in
  ///   the operand specifier.
  ///
  /// @param s       - See typeFromString().
  /// @param OpSize  - Indicates whether this is an OpSize16 instruction.
  ///                  If it is not, then 16-bit immediate operands stay 16-bit.
  /// @return        - The operand's encoding.
  static OperandEncoding immediateEncodingFromString(const std::string &s,
                                                     uint8_t OpSize);

  /// rmRegisterEncodingFromString - Like immediateEncodingFromString, but
  ///   handles operands that are in the REG field of the ModR/M byte.
  static OperandEncoding rmRegisterEncodingFromString(const std::string &s,
                                                      uint8_t OpSize);

  /// rmRegisterEncodingFromString - Like immediateEncodingFromString, but
  ///   handles operands that are in the REG field of the ModR/M byte.
  static OperandEncoding roRegisterEncodingFromString(const std::string &s,
                                                      uint8_t OpSize);
  static OperandEncoding memoryEncodingFromString(const std::string &s,
                                                  uint8_t OpSize);
  static OperandEncoding relocationEncodingFromString(const std::string &s,
                                                      uint8_t OpSize);
  static OperandEncoding opcodeModifierEncodingFromString(const std::string &s,
                                                          uint8_t OpSize);
  static OperandEncoding vvvvRegisterEncodingFromString(const std::string &s,
                                                        uint8_t OpSize);
  static OperandEncoding writemaskRegisterEncodingFromString(const std::string &s,
                                                             uint8_t OpSize);

  /// Adjust the encoding type for an operand based on the instruction.
  void adjustOperandEncoding(OperandEncoding &encoding);

  /// handleOperand - Converts a single operand from the LLVM table format to
  ///   the emitted table format, handling any duplicate operands it encounters
  ///   and then one non-duplicate.
  ///
  /// @param optional             - Determines whether to assert that the
  ///                               operand exists.
  /// @param operandIndex         - The index into the generated operand table.
  ///                               Incremented by this function one or more
  ///                               times to reflect possible duplicate
  ///                               operands).
  /// @param physicalOperandIndex - The index of the current operand into the
  ///                               set of non-duplicate ('physical') operands.
  ///                               Incremented by this function once.
  /// @param numPhysicalOperands  - The number of non-duplicate operands in the
  ///                               instructions.
  /// @param operandMapping       - The operand mapping, which has an entry for
  ///                               each operand that indicates whether it is a
  ///                               duplicate, and of what.
  void handleOperand(bool optional,
                     unsigned &operandIndex,
                     unsigned &physicalOperandIndex,
                     unsigned numPhysicalOperands,
                     const unsigned *operandMapping,
                     OperandEncoding (*encodingFromString)
                       (const std::string&,
                        uint8_t OpSize));

  /// shouldBeEmitted - Returns the shouldBeEmitted field.  Although filter()
  ///   filters out many instructions, at various points in decoding we
  ///   determine that the instruction should not actually be decodable.  In
  ///   particular, MMX MOV instructions aren't emitted, but they're only
  ///   identified during operand parsing.
  ///
  /// @return - true if at this point we believe the instruction should be
  ///   emitted; false if not.  This will return false if filter() returns false
  ///   once emitInstructionSpecifier() has been called.
  bool shouldBeEmitted() const {
    return ShouldBeEmitted;
  }

  /// emitInstructionSpecifier - Loads the instruction specifier for the current
  ///   instruction into a DisassemblerTables.
  ///
  void emitInstructionSpecifier();

  /// emitDecodePath - Populates the proper fields in the decode tables
  ///   corresponding to the decode paths for this instruction.
  ///
  /// \param tables The DisassemblerTables to populate with the decode
  ///               decode information for the current instruction.
  void emitDecodePath(DisassemblerTables &tables) const;

  /// Constructor - Initializes a RecognizableInstr with the appropriate fields
  ///   from a CodeGenInstruction.
  ///
  /// \param tables The DisassemblerTables that the specifier will be added to.
  /// \param insn   The CodeGenInstruction to extract information from.
  /// \param uid    The unique ID of the current instruction.
  RecognizableInstr(DisassemblerTables &tables,
                    const CodeGenInstruction &insn,
                    InstrUID uid);
public:
  /// processInstr - Accepts a CodeGenInstruction and loads decode information
  ///   for it into a DisassemblerTables if appropriate.
  ///
  /// \param tables The DiassemblerTables to be populated with decode
  ///               information.
  /// \param insn   The CodeGenInstruction to be used as a source for this
  ///               information.
  /// \param uid    The unique ID of the instruction.
  static void processInstr(DisassemblerTables &tables,
                           const CodeGenInstruction &insn,
                           InstrUID uid);
};

} // namespace X86Disassembler

} // namespace llvm

#endif
