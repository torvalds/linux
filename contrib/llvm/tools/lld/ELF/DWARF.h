//===- DWARF.h -----------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===-------------------------------------------------------------------===//

#ifndef LLD_ELF_DWARF_H
#define LLD_ELF_DWARF_H

#include "InputFiles.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ELF.h"

namespace lld {
namespace elf {

class InputSection;

struct LLDDWARFSection final : public llvm::DWARFSection {
  InputSectionBase *Sec = nullptr;
};

template <class ELFT> class LLDDwarfObj final : public llvm::DWARFObject {
public:
  explicit LLDDwarfObj(ObjFile<ELFT> *Obj);

  void forEachInfoSections(
      llvm::function_ref<void(const llvm::DWARFSection &)> F) const override {
    F(InfoSection);
  }

  const llvm::DWARFSection &getRangeSection() const override {
    return RangeSection;
  }

  const llvm::DWARFSection &getRnglistsSection() const override {
    return RngListsSection;
  }

  const llvm::DWARFSection &getLineSection() const override {
    return LineSection;
  }

  const llvm::DWARFSection &getAddrSection() const override {
    return AddrSection;
  }

  const llvm::DWARFSection &getGnuPubNamesSection() const override {
    return GnuPubNamesSection;
  }

  const llvm::DWARFSection &getGnuPubTypesSection() const override {
    return GnuPubTypesSection;
  }

  StringRef getFileName() const override { return ""; }
  StringRef getAbbrevSection() const override { return AbbrevSection; }
  StringRef getStringSection() const override { return StrSection; }
  StringRef getLineStringSection() const override { return LineStringSection; }

  bool isLittleEndian() const override {
    return ELFT::TargetEndianness == llvm::support::little;
  }

  llvm::Optional<llvm::RelocAddrEntry> find(const llvm::DWARFSection &Sec,
                                            uint64_t Pos) const override;

private:
  template <class RelTy>
  llvm::Optional<llvm::RelocAddrEntry> findAux(const InputSectionBase &Sec,
                                               uint64_t Pos,
                                               ArrayRef<RelTy> Rels) const;

  LLDDWARFSection GnuPubNamesSection;
  LLDDWARFSection GnuPubTypesSection;
  LLDDWARFSection InfoSection;
  LLDDWARFSection RangeSection;
  LLDDWARFSection RngListsSection;
  LLDDWARFSection LineSection;
  LLDDWARFSection AddrSection;
  StringRef AbbrevSection;
  StringRef StrSection;
  StringRef LineStringSection;
};

} // namespace elf
} // namespace lld

#endif
