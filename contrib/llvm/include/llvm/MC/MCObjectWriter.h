//===- llvm/MC/MCObjectWriter.h - Object File Writer Interface --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCOBJECTWRITER_H
#define LLVM_MC_MCOBJECTWRITER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class MCAsmLayout;
class MCAssembler;
class MCFixup;
class MCFragment;
class MCSymbol;
class MCSymbolRefExpr;
class MCValue;

/// Defines the object file and target independent interfaces used by the
/// assembler backend to write native file format object files.
///
/// The object writer contains a few callbacks used by the assembler to allow
/// the object writer to modify the assembler data structures at appropriate
/// points. Once assembly is complete, the object writer is given the
/// MCAssembler instance, which contains all the symbol and section data which
/// should be emitted as part of writeObject().
class MCObjectWriter {
protected:
  MCObjectWriter() = default;

public:
  MCObjectWriter(const MCObjectWriter &) = delete;
  MCObjectWriter &operator=(const MCObjectWriter &) = delete;
  virtual ~MCObjectWriter();

  /// lifetime management
  virtual void reset() {}

  /// \name High-Level API
  /// @{

  /// Perform any late binding of symbols (for example, to assign symbol
  /// indices for use when generating relocations).
  ///
  /// This routine is called by the assembler after layout and relaxation is
  /// complete.
  virtual void executePostLayoutBinding(MCAssembler &Asm,
                                        const MCAsmLayout &Layout) = 0;

  /// Record a relocation entry.
  ///
  /// This routine is called by the assembler after layout and relaxation, and
  /// post layout binding. The implementation is responsible for storing
  /// information about the relocation so that it can be emitted during
  /// writeObject().
  virtual void recordRelocation(MCAssembler &Asm, const MCAsmLayout &Layout,
                                const MCFragment *Fragment,
                                const MCFixup &Fixup, MCValue Target,
                                uint64_t &FixedValue) = 0;

  /// Check whether the difference (A - B) between two symbol references is
  /// fully resolved.
  ///
  /// Clients are not required to answer precisely and may conservatively return
  /// false, even when a difference is fully resolved.
  bool isSymbolRefDifferenceFullyResolved(const MCAssembler &Asm,
                                          const MCSymbolRefExpr *A,
                                          const MCSymbolRefExpr *B,
                                          bool InSet) const;

  virtual bool isSymbolRefDifferenceFullyResolvedImpl(const MCAssembler &Asm,
                                                      const MCSymbol &A,
                                                      const MCSymbol &B,
                                                      bool InSet) const;

  virtual bool isSymbolRefDifferenceFullyResolvedImpl(const MCAssembler &Asm,
                                                      const MCSymbol &SymA,
                                                      const MCFragment &FB,
                                                      bool InSet,
                                                      bool IsPCRel) const;

  /// Tell the object writer to emit an address-significance table during
  /// writeObject(). If this function is not called, all symbols are treated as
  /// address-significant.
  virtual void emitAddrsigSection() {}

  /// Record the given symbol in the address-significance table to be written
  /// diring writeObject().
  virtual void addAddrsigSymbol(const MCSymbol *Sym) {}

  /// Write the object file and returns the number of bytes written.
  ///
  /// This routine is called by the assembler after layout and relaxation is
  /// complete, fixups have been evaluated and applied, and relocations
  /// generated.
  virtual uint64_t writeObject(MCAssembler &Asm, const MCAsmLayout &Layout) = 0;

  /// @}
};

/// Base class for classes that define behaviour that is specific to both the
/// target and the object format.
class MCObjectTargetWriter {
public:
  virtual ~MCObjectTargetWriter() = default;
  virtual Triple::ObjectFormatType getFormat() const = 0;
};

} // end namespace llvm

#endif // LLVM_MC_MCOBJECTWRITER_H
