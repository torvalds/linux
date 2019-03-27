//===- llvm/MC/MCSymbolizer.h - MCSymbolizer class --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCSymbolizer class, which is used
// to symbolize instructions decoded from an object, that is, transform their
// immediate operands to MCExprs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDISASSEMBLER_MCSYMBOLIZER_H
#define LLVM_MC_MCDISASSEMBLER_MCSYMBOLIZER_H

#include "llvm/MC/MCDisassembler/MCRelocationInfo.h"
#include <algorithm>
#include <cstdint>
#include <memory>

namespace llvm {

class MCContext;
class MCInst;
class raw_ostream;

/// Symbolize and annotate disassembled instructions.
///
/// For now this mimics the old symbolization logic (from both ARM and x86), that
/// relied on user-provided (C API) callbacks to do the actual symbol lookup in
/// the object file. This was moved to MCExternalSymbolizer.
/// A better API would not rely on actually calling the two methods here from
/// inside each disassembler, but would use the instr info to determine what
/// operands are actually symbolizable, and in what way. I don't think this
/// information exists right now.
class MCSymbolizer {
protected:
  MCContext &Ctx;
  std::unique_ptr<MCRelocationInfo> RelInfo;

public:
  /// Construct an MCSymbolizer, taking ownership of \p RelInfo.
  MCSymbolizer(MCContext &Ctx, std::unique_ptr<MCRelocationInfo> RelInfo)
    : Ctx(Ctx), RelInfo(std::move(RelInfo)) {
  }

  MCSymbolizer(const MCSymbolizer &) = delete;
  MCSymbolizer &operator=(const MCSymbolizer &) = delete;
  virtual ~MCSymbolizer();

  /// Try to add a symbolic operand instead of \p Value to the MCInst.
  ///
  /// Instead of having a difficult to read immediate, a symbolic operand would
  /// represent this immediate in a more understandable way, for instance as a
  /// symbol or an offset from a symbol. Relocations can also be used to enrich
  /// the symbolic expression.
  /// \param Inst      - The MCInst where to insert the symbolic operand.
  /// \param cStream   - Stream to print comments and annotations on.
  /// \param Value     - Operand value, pc-adjusted by the caller if necessary.
  /// \param Address   - Load address of the instruction.
  /// \param IsBranch  - Is the instruction a branch?
  /// \param Offset    - Byte offset of the operand inside the inst.
  /// \param InstSize  - Size of the instruction in bytes.
  /// \return Whether a symbolic operand was added.
  virtual bool tryAddingSymbolicOperand(MCInst &Inst, raw_ostream &cStream,
                                        int64_t Value, uint64_t Address,
                                        bool IsBranch, uint64_t Offset,
                                        uint64_t InstSize) = 0;

  /// Try to add a comment on the PC-relative load.
  /// For instance, in Mach-O, this is used to add annotations to instructions
  /// that use C string literals, as found in __cstring.
  virtual void tryAddingPcLoadReferenceComment(raw_ostream &cStream,
                                               int64_t Value,
                                               uint64_t Address) = 0;
};

} // end namespace llvm

#endif // LLVM_MC_MCDISASSEMBLER_MCSYMBOLIZER_H
