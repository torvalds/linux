//===- OutputSections.h -----------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_OUTPUT_SECTIONS_H
#define LLD_ELF_OUTPUT_SECTIONS_H

#include "Config.h"
#include "InputSection.h"
#include "LinkerScript.h"
#include "Relocations.h"
#include "lld/Common/LLVM.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Object/ELF.h"
#include <array>

namespace lld {
namespace elf {

struct PhdrEntry;
class Symbol;
struct EhSectionPiece;
class EhInputSection;
class InputSection;
class InputSectionBase;
class MergeInputSection;
class OutputSection;
template <class ELFT> class ObjFile;
template <class ELFT> class SharedFile;
class SharedSymbol;
class Defined;

// This represents a section in an output file.
// It is composed of multiple InputSections.
// The writer creates multiple OutputSections and assign them unique,
// non-overlapping file offsets and VAs.
class OutputSection final : public BaseCommand, public SectionBase {
public:
  OutputSection(StringRef Name, uint32_t Type, uint64_t Flags);

  static bool classof(const SectionBase *S) {
    return S->kind() == SectionBase::Output;
  }

  static bool classof(const BaseCommand *C);

  uint64_t getLMA() const { return PtLoad ? Addr + PtLoad->LMAOffset : Addr; }
  template <typename ELFT> void writeHeaderTo(typename ELFT::Shdr *SHdr);

  uint32_t SectionIndex = UINT32_MAX;
  unsigned SortRank;

  uint32_t getPhdrFlags() const;

  // Pointer to the PT_LOAD segment, which this section resides in. This field
  // is used to correctly compute file offset of a section. When two sections
  // share the same load segment, difference between their file offsets should
  // be equal to difference between their virtual addresses. To compute some
  // section offset we use the following formula: Off = Off_first + VA -
  // VA_first, where Off_first and VA_first is file offset and VA of first
  // section in PT_LOAD.
  PhdrEntry *PtLoad = nullptr;

  // Pointer to a relocation section for this section. Usually nullptr because
  // we consume relocations, but if --emit-relocs is specified (which is rare),
  // it may have a non-null value.
  OutputSection *RelocationSection = nullptr;

  // Initially this field is the number of InputSections that have been added to
  // the OutputSection so far. Later on, after a call to assignAddresses, it
  // corresponds to the Elf_Shdr member.
  uint64_t Size = 0;

  // The following fields correspond to Elf_Shdr members.
  uint64_t Offset = 0;
  uint64_t Addr = 0;
  uint32_t ShName = 0;

  void addSection(InputSection *IS);

  // Location in the output buffer.
  uint8_t *Loc = nullptr;

  // The following members are normally only used in linker scripts.
  MemoryRegion *MemRegion = nullptr;
  MemoryRegion *LMARegion = nullptr;
  Expr AddrExpr;
  Expr AlignExpr;
  Expr LMAExpr;
  Expr SubalignExpr;
  std::vector<BaseCommand *> SectionCommands;
  std::vector<StringRef> Phdrs;
  llvm::Optional<std::array<uint8_t, 4>> Filler;
  ConstraintKind Constraint = ConstraintKind::NoConstraint;
  std::string Location;
  std::string MemoryRegionName;
  std::string LMARegionName;
  bool NonAlloc = false;
  bool Noload = false;
  bool ExpressionsUseSymbols = false;
  bool InOverlay = false;

  template <class ELFT> void finalize();
  template <class ELFT> void writeTo(uint8_t *Buf);
  template <class ELFT> void maybeCompress();

  void sort(llvm::function_ref<int(InputSectionBase *S)> Order);
  void sortInitFini();
  void sortCtorsDtors();

private:
  // Used for implementation of --compress-debug-sections option.
  std::vector<uint8_t> ZDebugHeader;
  llvm::SmallVector<char, 1> CompressedData;

  std::array<uint8_t, 4> getFiller();
};

int getPriority(StringRef S);

std::vector<InputSection *> getInputSections(OutputSection* OS);

// All output sections that are handled by the linker specially are
// globally accessible. Writer initializes them, so don't use them
// until Writer is initialized.
struct Out {
  static uint8_t First;
  static PhdrEntry *TlsPhdr;
  static OutputSection *ElfHeader;
  static OutputSection *ProgramHeaders;
  static OutputSection *PreinitArray;
  static OutputSection *InitArray;
  static OutputSection *FiniArray;
};

} // namespace elf
} // namespace lld

namespace lld {
namespace elf {

uint64_t getHeaderSize();

extern std::vector<OutputSection *> OutputSections;
} // namespace elf
} // namespace lld

#endif
