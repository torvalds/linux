//===- MCAsmLayout.h - Assembly Layout Object -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMLAYOUT_H
#define LLVM_MC_MCASMLAYOUT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
class MCAssembler;
class MCFragment;
class MCSection;
class MCSymbol;

/// Encapsulates the layout of an assembly file at a particular point in time.
///
/// Assembly may require computing multiple layouts for a particular assembly
/// file as part of the relaxation process. This class encapsulates the layout
/// at a single point in time in such a way that it is always possible to
/// efficiently compute the exact address of any symbol in the assembly file,
/// even during the relaxation process.
class MCAsmLayout {
  MCAssembler &Assembler;

  /// List of sections in layout order.
  llvm::SmallVector<MCSection *, 16> SectionOrder;

  /// The last fragment which was laid out, or 0 if nothing has been laid
  /// out. Fragments are always laid out in order, so all fragments with a
  /// lower ordinal will be valid.
  mutable DenseMap<const MCSection *, MCFragment *> LastValidFragment;

  /// Make sure that the layout for the given fragment is valid, lazily
  /// computing it if necessary.
  void ensureValid(const MCFragment *F) const;

  /// Is the layout for this fragment valid?
  bool isFragmentValid(const MCFragment *F) const;

public:
  MCAsmLayout(MCAssembler &Assembler);

  /// Get the assembler object this is a layout for.
  MCAssembler &getAssembler() const { return Assembler; }

  /// Invalidate the fragments starting with F because it has been
  /// resized. The fragment's size should have already been updated, but
  /// its bundle padding will be recomputed.
  void invalidateFragmentsFrom(MCFragment *F);

  /// Perform layout for a single fragment, assuming that the previous
  /// fragment has already been laid out correctly, and the parent section has
  /// been initialized.
  void layoutFragment(MCFragment *Fragment);

  /// \name Section Access (in layout order)
  /// @{

  llvm::SmallVectorImpl<MCSection *> &getSectionOrder() { return SectionOrder; }
  const llvm::SmallVectorImpl<MCSection *> &getSectionOrder() const {
    return SectionOrder;
  }

  /// @}
  /// \name Fragment Layout Data
  /// @{

  /// Get the offset of the given fragment inside its containing section.
  uint64_t getFragmentOffset(const MCFragment *F) const;

  /// @}
  /// \name Utility Functions
  /// @{

  /// Get the address space size of the given section, as it effects
  /// layout. This may differ from the size reported by \see getSectionSize() by
  /// not including section tail padding.
  uint64_t getSectionAddressSize(const MCSection *Sec) const;

  /// Get the data size of the given section, as emitted to the object
  /// file. This may include additional padding, or be 0 for virtual sections.
  uint64_t getSectionFileSize(const MCSection *Sec) const;

  /// Get the offset of the given symbol, as computed in the current
  /// layout.
  /// \return True on success.
  bool getSymbolOffset(const MCSymbol &S, uint64_t &Val) const;

  /// Variant that reports a fatal error if the offset is not computable.
  uint64_t getSymbolOffset(const MCSymbol &S) const;

  /// If this symbol is equivalent to A + Constant, return A.
  const MCSymbol *getBaseSymbol(const MCSymbol &Symbol) const;

  /// @}
};

} // end namespace llvm

#endif
