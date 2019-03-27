//===-- RuntimeDyld.cpp - Run-time dynamic linker for MC-JIT ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "RuntimeDyldCOFF.h"
#include "RuntimeDyldCheckerImpl.h"
#include "RuntimeDyldELF.h"
#include "RuntimeDyldImpl.h"
#include "RuntimeDyldMachO.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MutexGuard.h"

#include <future>

using namespace llvm;
using namespace llvm::object;

#define DEBUG_TYPE "dyld"

namespace {

enum RuntimeDyldErrorCode {
  GenericRTDyldError = 1
};

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class RuntimeDyldErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "runtimedyld"; }

  std::string message(int Condition) const override {
    switch (static_cast<RuntimeDyldErrorCode>(Condition)) {
      case GenericRTDyldError: return "Generic RuntimeDyld error";
    }
    llvm_unreachable("Unrecognized RuntimeDyldErrorCode");
  }
};

static ManagedStatic<RuntimeDyldErrorCategory> RTDyldErrorCategory;

}

char RuntimeDyldError::ID = 0;

void RuntimeDyldError::log(raw_ostream &OS) const {
  OS << ErrMsg << "\n";
}

std::error_code RuntimeDyldError::convertToErrorCode() const {
  return std::error_code(GenericRTDyldError, *RTDyldErrorCategory);
}

// Empty out-of-line virtual destructor as the key function.
RuntimeDyldImpl::~RuntimeDyldImpl() {}

// Pin LoadedObjectInfo's vtables to this file.
void RuntimeDyld::LoadedObjectInfo::anchor() {}

namespace llvm {

void RuntimeDyldImpl::registerEHFrames() {}

void RuntimeDyldImpl::deregisterEHFrames() {
  MemMgr.deregisterEHFrames();
}

#ifndef NDEBUG
static void dumpSectionMemory(const SectionEntry &S, StringRef State) {
  dbgs() << "----- Contents of section " << S.getName() << " " << State
         << " -----";

  if (S.getAddress() == nullptr) {
    dbgs() << "\n          <section not emitted>\n";
    return;
  }

  const unsigned ColsPerRow = 16;

  uint8_t *DataAddr = S.getAddress();
  uint64_t LoadAddr = S.getLoadAddress();

  unsigned StartPadding = LoadAddr & (ColsPerRow - 1);
  unsigned BytesRemaining = S.getSize();

  if (StartPadding) {
    dbgs() << "\n" << format("0x%016" PRIx64,
                             LoadAddr & ~(uint64_t)(ColsPerRow - 1)) << ":";
    while (StartPadding--)
      dbgs() << "   ";
  }

  while (BytesRemaining > 0) {
    if ((LoadAddr & (ColsPerRow - 1)) == 0)
      dbgs() << "\n" << format("0x%016" PRIx64, LoadAddr) << ":";

    dbgs() << " " << format("%02x", *DataAddr);

    ++DataAddr;
    ++LoadAddr;
    --BytesRemaining;
  }

  dbgs() << "\n";
}
#endif

// Resolve the relocations for all symbols we currently know about.
void RuntimeDyldImpl::resolveRelocations() {
  MutexGuard locked(lock);

  // Print out the sections prior to relocation.
  LLVM_DEBUG(for (int i = 0, e = Sections.size(); i != e; ++i)
                 dumpSectionMemory(Sections[i], "before relocations"););

  // First, resolve relocations associated with external symbols.
  if (auto Err = resolveExternalSymbols()) {
    HasError = true;
    ErrorStr = toString(std::move(Err));
  }

  resolveLocalRelocations();

  // Print out sections after relocation.
  LLVM_DEBUG(for (int i = 0, e = Sections.size(); i != e; ++i)
                 dumpSectionMemory(Sections[i], "after relocations"););
}

void RuntimeDyldImpl::resolveLocalRelocations() {
  // Iterate over all outstanding relocations
  for (auto it = Relocations.begin(), e = Relocations.end(); it != e; ++it) {
    // The Section here (Sections[i]) refers to the section in which the
    // symbol for the relocation is located.  The SectionID in the relocation
    // entry provides the section to which the relocation will be applied.
    int Idx = it->first;
    uint64_t Addr = Sections[Idx].getLoadAddress();
    LLVM_DEBUG(dbgs() << "Resolving relocations Section #" << Idx << "\t"
                      << format("%p", (uintptr_t)Addr) << "\n");
    resolveRelocationList(it->second, Addr);
  }
  Relocations.clear();
}

void RuntimeDyldImpl::mapSectionAddress(const void *LocalAddress,
                                        uint64_t TargetAddress) {
  MutexGuard locked(lock);
  for (unsigned i = 0, e = Sections.size(); i != e; ++i) {
    if (Sections[i].getAddress() == LocalAddress) {
      reassignSectionAddress(i, TargetAddress);
      return;
    }
  }
  llvm_unreachable("Attempting to remap address of unknown section!");
}

static Error getOffset(const SymbolRef &Sym, SectionRef Sec,
                       uint64_t &Result) {
  Expected<uint64_t> AddressOrErr = Sym.getAddress();
  if (!AddressOrErr)
    return AddressOrErr.takeError();
  Result = *AddressOrErr - Sec.getAddress();
  return Error::success();
}

Expected<RuntimeDyldImpl::ObjSectionToIDMap>
RuntimeDyldImpl::loadObjectImpl(const object::ObjectFile &Obj) {
  MutexGuard locked(lock);

  // Save information about our target
  Arch = (Triple::ArchType)Obj.getArch();
  IsTargetLittleEndian = Obj.isLittleEndian();
  setMipsABI(Obj);

  // Compute the memory size required to load all sections to be loaded
  // and pass this information to the memory manager
  if (MemMgr.needsToReserveAllocationSpace()) {
    uint64_t CodeSize = 0, RODataSize = 0, RWDataSize = 0;
    uint32_t CodeAlign = 1, RODataAlign = 1, RWDataAlign = 1;
    if (auto Err = computeTotalAllocSize(Obj,
                                         CodeSize, CodeAlign,
                                         RODataSize, RODataAlign,
                                         RWDataSize, RWDataAlign))
      return std::move(Err);
    MemMgr.reserveAllocationSpace(CodeSize, CodeAlign, RODataSize, RODataAlign,
                                  RWDataSize, RWDataAlign);
  }

  // Used sections from the object file
  ObjSectionToIDMap LocalSections;

  // Common symbols requiring allocation, with their sizes and alignments
  CommonSymbolList CommonSymbolsToAllocate;

  uint64_t CommonSize = 0;
  uint32_t CommonAlign = 0;

  // First, collect all weak and common symbols. We need to know if stronger
  // definitions occur elsewhere.
  JITSymbolResolver::LookupSet ResponsibilitySet;
  {
    JITSymbolResolver::LookupSet Symbols;
    for (auto &Sym : Obj.symbols()) {
      uint32_t Flags = Sym.getFlags();
      if ((Flags & SymbolRef::SF_Common) || (Flags & SymbolRef::SF_Weak)) {
        // Get symbol name.
        if (auto NameOrErr = Sym.getName())
          Symbols.insert(*NameOrErr);
        else
          return NameOrErr.takeError();
      }
    }

    if (auto ResultOrErr = Resolver.getResponsibilitySet(Symbols))
      ResponsibilitySet = std::move(*ResultOrErr);
    else
      return ResultOrErr.takeError();
  }

  // Parse symbols
  LLVM_DEBUG(dbgs() << "Parse symbols:\n");
  for (symbol_iterator I = Obj.symbol_begin(), E = Obj.symbol_end(); I != E;
       ++I) {
    uint32_t Flags = I->getFlags();

    // Skip undefined symbols.
    if (Flags & SymbolRef::SF_Undefined)
      continue;

    // Get the symbol type.
    object::SymbolRef::Type SymType;
    if (auto SymTypeOrErr = I->getType())
      SymType = *SymTypeOrErr;
    else
      return SymTypeOrErr.takeError();

    // Get symbol name.
    StringRef Name;
    if (auto NameOrErr = I->getName())
      Name = *NameOrErr;
    else
      return NameOrErr.takeError();

    // Compute JIT symbol flags.
    auto JITSymFlags = getJITSymbolFlags(*I);
    if (!JITSymFlags)
      return JITSymFlags.takeError();

    // If this is a weak definition, check to see if there's a strong one.
    // If there is, skip this symbol (we won't be providing it: the strong
    // definition will). If there's no strong definition, make this definition
    // strong.
    if (JITSymFlags->isWeak() || JITSymFlags->isCommon()) {
      // First check whether there's already a definition in this instance.
      if (GlobalSymbolTable.count(Name))
        continue;

      // If we're not responsible for this symbol, skip it.
      if (!ResponsibilitySet.count(Name))
        continue;

      // Otherwise update the flags on the symbol to make this definition
      // strong.
      if (JITSymFlags->isWeak())
        *JITSymFlags &= ~JITSymbolFlags::Weak;
      if (JITSymFlags->isCommon()) {
        *JITSymFlags &= ~JITSymbolFlags::Common;
        uint32_t Align = I->getAlignment();
        uint64_t Size = I->getCommonSize();
        if (!CommonAlign)
          CommonAlign = Align;
        CommonSize = alignTo(CommonSize, Align) + Size;
        CommonSymbolsToAllocate.push_back(*I);
      }
    }

    if (Flags & SymbolRef::SF_Absolute &&
        SymType != object::SymbolRef::ST_File) {
      uint64_t Addr = 0;
      if (auto AddrOrErr = I->getAddress())
        Addr = *AddrOrErr;
      else
        return AddrOrErr.takeError();

      unsigned SectionID = AbsoluteSymbolSection;

      LLVM_DEBUG(dbgs() << "\tType: " << SymType << " (absolute) Name: " << Name
                        << " SID: " << SectionID
                        << " Offset: " << format("%p", (uintptr_t)Addr)
                        << " flags: " << Flags << "\n");
      GlobalSymbolTable[Name] = SymbolTableEntry(SectionID, Addr, *JITSymFlags);
    } else if (SymType == object::SymbolRef::ST_Function ||
               SymType == object::SymbolRef::ST_Data ||
               SymType == object::SymbolRef::ST_Unknown ||
               SymType == object::SymbolRef::ST_Other) {

      section_iterator SI = Obj.section_end();
      if (auto SIOrErr = I->getSection())
        SI = *SIOrErr;
      else
        return SIOrErr.takeError();

      if (SI == Obj.section_end())
        continue;

      // Get symbol offset.
      uint64_t SectOffset;
      if (auto Err = getOffset(*I, *SI, SectOffset))
        return std::move(Err);

      bool IsCode = SI->isText();
      unsigned SectionID;
      if (auto SectionIDOrErr =
              findOrEmitSection(Obj, *SI, IsCode, LocalSections))
        SectionID = *SectionIDOrErr;
      else
        return SectionIDOrErr.takeError();

      LLVM_DEBUG(dbgs() << "\tType: " << SymType << " Name: " << Name
                        << " SID: " << SectionID
                        << " Offset: " << format("%p", (uintptr_t)SectOffset)
                        << " flags: " << Flags << "\n");
      GlobalSymbolTable[Name] =
          SymbolTableEntry(SectionID, SectOffset, *JITSymFlags);
    }
  }

  // Allocate common symbols
  if (auto Err = emitCommonSymbols(Obj, CommonSymbolsToAllocate, CommonSize,
                                   CommonAlign))
    return std::move(Err);

  // Parse and process relocations
  LLVM_DEBUG(dbgs() << "Parse relocations:\n");
  for (section_iterator SI = Obj.section_begin(), SE = Obj.section_end();
       SI != SE; ++SI) {
    StubMap Stubs;
    section_iterator RelocatedSection = SI->getRelocatedSection();

    if (RelocatedSection == SE)
      continue;

    relocation_iterator I = SI->relocation_begin();
    relocation_iterator E = SI->relocation_end();

    if (I == E && !ProcessAllSections)
      continue;

    bool IsCode = RelocatedSection->isText();
    unsigned SectionID = 0;
    if (auto SectionIDOrErr = findOrEmitSection(Obj, *RelocatedSection, IsCode,
                                                LocalSections))
      SectionID = *SectionIDOrErr;
    else
      return SectionIDOrErr.takeError();

    LLVM_DEBUG(dbgs() << "\tSectionID: " << SectionID << "\n");

    for (; I != E;)
      if (auto IOrErr = processRelocationRef(SectionID, I, Obj, LocalSections, Stubs))
        I = *IOrErr;
      else
        return IOrErr.takeError();

    // If there is an attached checker, notify it about the stubs for this
    // section so that they can be verified.
    if (Checker)
      Checker->registerStubMap(Obj.getFileName(), SectionID, Stubs);
  }

  // Give the subclasses a chance to tie-up any loose ends.
  if (auto Err = finalizeLoad(Obj, LocalSections))
    return std::move(Err);

//   for (auto E : LocalSections)
//     llvm::dbgs() << "Added: " << E.first.getRawDataRefImpl() << " -> " << E.second << "\n";

  return LocalSections;
}

// A helper method for computeTotalAllocSize.
// Computes the memory size required to allocate sections with the given sizes,
// assuming that all sections are allocated with the given alignment
static uint64_t
computeAllocationSizeForSections(std::vector<uint64_t> &SectionSizes,
                                 uint64_t Alignment) {
  uint64_t TotalSize = 0;
  for (size_t Idx = 0, Cnt = SectionSizes.size(); Idx < Cnt; Idx++) {
    uint64_t AlignedSize =
        (SectionSizes[Idx] + Alignment - 1) / Alignment * Alignment;
    TotalSize += AlignedSize;
  }
  return TotalSize;
}

static bool isRequiredForExecution(const SectionRef Section) {
  const ObjectFile *Obj = Section.getObject();
  if (isa<object::ELFObjectFileBase>(Obj))
    return ELFSectionRef(Section).getFlags() & ELF::SHF_ALLOC;
  if (auto *COFFObj = dyn_cast<object::COFFObjectFile>(Obj)) {
    const coff_section *CoffSection = COFFObj->getCOFFSection(Section);
    // Avoid loading zero-sized COFF sections.
    // In PE files, VirtualSize gives the section size, and SizeOfRawData
    // may be zero for sections with content. In Obj files, SizeOfRawData
    // gives the section size, and VirtualSize is always zero. Hence
    // the need to check for both cases below.
    bool HasContent =
        (CoffSection->VirtualSize > 0) || (CoffSection->SizeOfRawData > 0);
    bool IsDiscardable =
        CoffSection->Characteristics &
        (COFF::IMAGE_SCN_MEM_DISCARDABLE | COFF::IMAGE_SCN_LNK_INFO);
    return HasContent && !IsDiscardable;
  }

  assert(isa<MachOObjectFile>(Obj));
  return true;
}

static bool isReadOnlyData(const SectionRef Section) {
  const ObjectFile *Obj = Section.getObject();
  if (isa<object::ELFObjectFileBase>(Obj))
    return !(ELFSectionRef(Section).getFlags() &
             (ELF::SHF_WRITE | ELF::SHF_EXECINSTR));
  if (auto *COFFObj = dyn_cast<object::COFFObjectFile>(Obj))
    return ((COFFObj->getCOFFSection(Section)->Characteristics &
             (COFF::IMAGE_SCN_CNT_INITIALIZED_DATA
             | COFF::IMAGE_SCN_MEM_READ
             | COFF::IMAGE_SCN_MEM_WRITE))
             ==
             (COFF::IMAGE_SCN_CNT_INITIALIZED_DATA
             | COFF::IMAGE_SCN_MEM_READ));

  assert(isa<MachOObjectFile>(Obj));
  return false;
}

static bool isZeroInit(const SectionRef Section) {
  const ObjectFile *Obj = Section.getObject();
  if (isa<object::ELFObjectFileBase>(Obj))
    return ELFSectionRef(Section).getType() == ELF::SHT_NOBITS;
  if (auto *COFFObj = dyn_cast<object::COFFObjectFile>(Obj))
    return COFFObj->getCOFFSection(Section)->Characteristics &
            COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA;

  auto *MachO = cast<MachOObjectFile>(Obj);
  unsigned SectionType = MachO->getSectionType(Section);
  return SectionType == MachO::S_ZEROFILL ||
         SectionType == MachO::S_GB_ZEROFILL;
}

// Compute an upper bound of the memory size that is required to load all
// sections
Error RuntimeDyldImpl::computeTotalAllocSize(const ObjectFile &Obj,
                                             uint64_t &CodeSize,
                                             uint32_t &CodeAlign,
                                             uint64_t &RODataSize,
                                             uint32_t &RODataAlign,
                                             uint64_t &RWDataSize,
                                             uint32_t &RWDataAlign) {
  // Compute the size of all sections required for execution
  std::vector<uint64_t> CodeSectionSizes;
  std::vector<uint64_t> ROSectionSizes;
  std::vector<uint64_t> RWSectionSizes;

  // Collect sizes of all sections to be loaded;
  // also determine the max alignment of all sections
  for (section_iterator SI = Obj.section_begin(), SE = Obj.section_end();
       SI != SE; ++SI) {
    const SectionRef &Section = *SI;

    bool IsRequired = isRequiredForExecution(Section) || ProcessAllSections;

    // Consider only the sections that are required to be loaded for execution
    if (IsRequired) {
      uint64_t DataSize = Section.getSize();
      uint64_t Alignment64 = Section.getAlignment();
      unsigned Alignment = (unsigned)Alignment64 & 0xffffffffL;
      bool IsCode = Section.isText();
      bool IsReadOnly = isReadOnlyData(Section);

      StringRef Name;
      if (auto EC = Section.getName(Name))
        return errorCodeToError(EC);

      uint64_t StubBufSize = computeSectionStubBufSize(Obj, Section);
      uint64_t SectionSize = DataSize + StubBufSize;

      // The .eh_frame section (at least on Linux) needs an extra four bytes
      // padded
      // with zeroes added at the end.  For MachO objects, this section has a
      // slightly different name, so this won't have any effect for MachO
      // objects.
      if (Name == ".eh_frame")
        SectionSize += 4;

      if (!SectionSize)
        SectionSize = 1;

      if (IsCode) {
        CodeAlign = std::max(CodeAlign, Alignment);
        CodeSectionSizes.push_back(SectionSize);
      } else if (IsReadOnly) {
        RODataAlign = std::max(RODataAlign, Alignment);
        ROSectionSizes.push_back(SectionSize);
      } else {
        RWDataAlign = std::max(RWDataAlign, Alignment);
        RWSectionSizes.push_back(SectionSize);
      }
    }
  }

  // Compute Global Offset Table size. If it is not zero we
  // also update alignment, which is equal to a size of a
  // single GOT entry.
  if (unsigned GotSize = computeGOTSize(Obj)) {
    RWSectionSizes.push_back(GotSize);
    RWDataAlign = std::max<uint32_t>(RWDataAlign, getGOTEntrySize());
  }

  // Compute the size of all common symbols
  uint64_t CommonSize = 0;
  uint32_t CommonAlign = 1;
  for (symbol_iterator I = Obj.symbol_begin(), E = Obj.symbol_end(); I != E;
       ++I) {
    uint32_t Flags = I->getFlags();
    if (Flags & SymbolRef::SF_Common) {
      // Add the common symbols to a list.  We'll allocate them all below.
      uint64_t Size = I->getCommonSize();
      uint32_t Align = I->getAlignment();
      // If this is the first common symbol, use its alignment as the alignment
      // for the common symbols section.
      if (CommonSize == 0)
        CommonAlign = Align;
      CommonSize = alignTo(CommonSize, Align) + Size;
    }
  }
  if (CommonSize != 0) {
    RWSectionSizes.push_back(CommonSize);
    RWDataAlign = std::max(RWDataAlign, CommonAlign);
  }

  // Compute the required allocation space for each different type of sections
  // (code, read-only data, read-write data) assuming that all sections are
  // allocated with the max alignment. Note that we cannot compute with the
  // individual alignments of the sections, because then the required size
  // depends on the order, in which the sections are allocated.
  CodeSize = computeAllocationSizeForSections(CodeSectionSizes, CodeAlign);
  RODataSize = computeAllocationSizeForSections(ROSectionSizes, RODataAlign);
  RWDataSize = computeAllocationSizeForSections(RWSectionSizes, RWDataAlign);

  return Error::success();
}

// compute GOT size
unsigned RuntimeDyldImpl::computeGOTSize(const ObjectFile &Obj) {
  size_t GotEntrySize = getGOTEntrySize();
  if (!GotEntrySize)
    return 0;

  size_t GotSize = 0;
  for (section_iterator SI = Obj.section_begin(), SE = Obj.section_end();
       SI != SE; ++SI) {

    for (const RelocationRef &Reloc : SI->relocations())
      if (relocationNeedsGot(Reloc))
        GotSize += GotEntrySize;
  }

  return GotSize;
}

// compute stub buffer size for the given section
unsigned RuntimeDyldImpl::computeSectionStubBufSize(const ObjectFile &Obj,
                                                    const SectionRef &Section) {
  unsigned StubSize = getMaxStubSize();
  if (StubSize == 0) {
    return 0;
  }
  // FIXME: this is an inefficient way to handle this. We should computed the
  // necessary section allocation size in loadObject by walking all the sections
  // once.
  unsigned StubBufSize = 0;
  for (section_iterator SI = Obj.section_begin(), SE = Obj.section_end();
       SI != SE; ++SI) {
    section_iterator RelSecI = SI->getRelocatedSection();
    if (!(RelSecI == Section))
      continue;

    for (const RelocationRef &Reloc : SI->relocations())
      if (relocationNeedsStub(Reloc))
        StubBufSize += StubSize;
  }

  // Get section data size and alignment
  uint64_t DataSize = Section.getSize();
  uint64_t Alignment64 = Section.getAlignment();

  // Add stubbuf size alignment
  unsigned Alignment = (unsigned)Alignment64 & 0xffffffffL;
  unsigned StubAlignment = getStubAlignment();
  unsigned EndAlignment = (DataSize | Alignment) & -(DataSize | Alignment);
  if (StubAlignment > EndAlignment)
    StubBufSize += StubAlignment - EndAlignment;
  return StubBufSize;
}

uint64_t RuntimeDyldImpl::readBytesUnaligned(uint8_t *Src,
                                             unsigned Size) const {
  uint64_t Result = 0;
  if (IsTargetLittleEndian) {
    Src += Size - 1;
    while (Size--)
      Result = (Result << 8) | *Src--;
  } else
    while (Size--)
      Result = (Result << 8) | *Src++;

  return Result;
}

void RuntimeDyldImpl::writeBytesUnaligned(uint64_t Value, uint8_t *Dst,
                                          unsigned Size) const {
  if (IsTargetLittleEndian) {
    while (Size--) {
      *Dst++ = Value & 0xFF;
      Value >>= 8;
    }
  } else {
    Dst += Size - 1;
    while (Size--) {
      *Dst-- = Value & 0xFF;
      Value >>= 8;
    }
  }
}

Expected<JITSymbolFlags>
RuntimeDyldImpl::getJITSymbolFlags(const SymbolRef &SR) {
  return JITSymbolFlags::fromObjectSymbol(SR);
}

Error RuntimeDyldImpl::emitCommonSymbols(const ObjectFile &Obj,
                                         CommonSymbolList &SymbolsToAllocate,
                                         uint64_t CommonSize,
                                         uint32_t CommonAlign) {
  if (SymbolsToAllocate.empty())
    return Error::success();

  // Allocate memory for the section
  unsigned SectionID = Sections.size();
  uint8_t *Addr = MemMgr.allocateDataSection(CommonSize, CommonAlign, SectionID,
                                             "<common symbols>", false);
  if (!Addr)
    report_fatal_error("Unable to allocate memory for common symbols!");
  uint64_t Offset = 0;
  Sections.push_back(
      SectionEntry("<common symbols>", Addr, CommonSize, CommonSize, 0));
  memset(Addr, 0, CommonSize);

  LLVM_DEBUG(dbgs() << "emitCommonSection SectionID: " << SectionID
                    << " new addr: " << format("%p", Addr)
                    << " DataSize: " << CommonSize << "\n");

  // Assign the address of each symbol
  for (auto &Sym : SymbolsToAllocate) {
    uint32_t Align = Sym.getAlignment();
    uint64_t Size = Sym.getCommonSize();
    StringRef Name;
    if (auto NameOrErr = Sym.getName())
      Name = *NameOrErr;
    else
      return NameOrErr.takeError();
    if (Align) {
      // This symbol has an alignment requirement.
      uint64_t AlignOffset = OffsetToAlignment((uint64_t)Addr, Align);
      Addr += AlignOffset;
      Offset += AlignOffset;
    }
    auto JITSymFlags = getJITSymbolFlags(Sym);

    if (!JITSymFlags)
      return JITSymFlags.takeError();

    LLVM_DEBUG(dbgs() << "Allocating common symbol " << Name << " address "
                      << format("%p", Addr) << "\n");
    GlobalSymbolTable[Name] =
        SymbolTableEntry(SectionID, Offset, std::move(*JITSymFlags));
    Offset += Size;
    Addr += Size;
  }

  if (Checker)
    Checker->registerSection(Obj.getFileName(), SectionID);

  return Error::success();
}

Expected<unsigned>
RuntimeDyldImpl::emitSection(const ObjectFile &Obj,
                             const SectionRef &Section,
                             bool IsCode) {
  StringRef data;
  uint64_t Alignment64 = Section.getAlignment();

  unsigned Alignment = (unsigned)Alignment64 & 0xffffffffL;
  unsigned PaddingSize = 0;
  unsigned StubBufSize = 0;
  bool IsRequired = isRequiredForExecution(Section);
  bool IsVirtual = Section.isVirtual();
  bool IsZeroInit = isZeroInit(Section);
  bool IsReadOnly = isReadOnlyData(Section);
  uint64_t DataSize = Section.getSize();

  StringRef Name;
  if (auto EC = Section.getName(Name))
    return errorCodeToError(EC);

  StubBufSize = computeSectionStubBufSize(Obj, Section);

  // The .eh_frame section (at least on Linux) needs an extra four bytes padded
  // with zeroes added at the end.  For MachO objects, this section has a
  // slightly different name, so this won't have any effect for MachO objects.
  if (Name == ".eh_frame")
    PaddingSize = 4;

  uintptr_t Allocate;
  unsigned SectionID = Sections.size();
  uint8_t *Addr;
  const char *pData = nullptr;

  // If this section contains any bits (i.e. isn't a virtual or bss section),
  // grab a reference to them.
  if (!IsVirtual && !IsZeroInit) {
    // In either case, set the location of the unrelocated section in memory,
    // since we still process relocations for it even if we're not applying them.
    if (auto EC = Section.getContents(data))
      return errorCodeToError(EC);
    pData = data.data();
  }

  // Code section alignment needs to be at least as high as stub alignment or
  // padding calculations may by incorrect when the section is remapped to a
  // higher alignment.
  if (IsCode) {
    Alignment = std::max(Alignment, getStubAlignment());
    if (StubBufSize > 0)
      PaddingSize += getStubAlignment() - 1;
  }

  // Some sections, such as debug info, don't need to be loaded for execution.
  // Process those only if explicitly requested.
  if (IsRequired || ProcessAllSections) {
    Allocate = DataSize + PaddingSize + StubBufSize;
    if (!Allocate)
      Allocate = 1;
    Addr = IsCode ? MemMgr.allocateCodeSection(Allocate, Alignment, SectionID,
                                               Name)
                  : MemMgr.allocateDataSection(Allocate, Alignment, SectionID,
                                               Name, IsReadOnly);
    if (!Addr)
      report_fatal_error("Unable to allocate section memory!");

    // Zero-initialize or copy the data from the image
    if (IsZeroInit || IsVirtual)
      memset(Addr, 0, DataSize);
    else
      memcpy(Addr, pData, DataSize);

    // Fill in any extra bytes we allocated for padding
    if (PaddingSize != 0) {
      memset(Addr + DataSize, 0, PaddingSize);
      // Update the DataSize variable to include padding.
      DataSize += PaddingSize;

      // Align DataSize to stub alignment if we have any stubs (PaddingSize will
      // have been increased above to account for this).
      if (StubBufSize > 0)
        DataSize &= ~(getStubAlignment() - 1);
    }

    LLVM_DEBUG(dbgs() << "emitSection SectionID: " << SectionID << " Name: "
                      << Name << " obj addr: " << format("%p", pData)
                      << " new addr: " << format("%p", Addr) << " DataSize: "
                      << DataSize << " StubBufSize: " << StubBufSize
                      << " Allocate: " << Allocate << "\n");
  } else {
    // Even if we didn't load the section, we need to record an entry for it
    // to handle later processing (and by 'handle' I mean don't do anything
    // with these sections).
    Allocate = 0;
    Addr = nullptr;
    LLVM_DEBUG(
        dbgs() << "emitSection SectionID: " << SectionID << " Name: " << Name
               << " obj addr: " << format("%p", data.data()) << " new addr: 0"
               << " DataSize: " << DataSize << " StubBufSize: " << StubBufSize
               << " Allocate: " << Allocate << "\n");
  }

  Sections.push_back(
      SectionEntry(Name, Addr, DataSize, Allocate, (uintptr_t)pData));

  // Debug info sections are linked as if their load address was zero
  if (!IsRequired)
    Sections.back().setLoadAddress(0);

  if (Checker)
    Checker->registerSection(Obj.getFileName(), SectionID);

  return SectionID;
}

Expected<unsigned>
RuntimeDyldImpl::findOrEmitSection(const ObjectFile &Obj,
                                   const SectionRef &Section,
                                   bool IsCode,
                                   ObjSectionToIDMap &LocalSections) {

  unsigned SectionID = 0;
  ObjSectionToIDMap::iterator i = LocalSections.find(Section);
  if (i != LocalSections.end())
    SectionID = i->second;
  else {
    if (auto SectionIDOrErr = emitSection(Obj, Section, IsCode))
      SectionID = *SectionIDOrErr;
    else
      return SectionIDOrErr.takeError();
    LocalSections[Section] = SectionID;
  }
  return SectionID;
}

void RuntimeDyldImpl::addRelocationForSection(const RelocationEntry &RE,
                                              unsigned SectionID) {
  Relocations[SectionID].push_back(RE);
}

void RuntimeDyldImpl::addRelocationForSymbol(const RelocationEntry &RE,
                                             StringRef SymbolName) {
  // Relocation by symbol.  If the symbol is found in the global symbol table,
  // create an appropriate section relocation.  Otherwise, add it to
  // ExternalSymbolRelocations.
  RTDyldSymbolTable::const_iterator Loc = GlobalSymbolTable.find(SymbolName);
  if (Loc == GlobalSymbolTable.end()) {
    ExternalSymbolRelocations[SymbolName].push_back(RE);
  } else {
    // Copy the RE since we want to modify its addend.
    RelocationEntry RECopy = RE;
    const auto &SymInfo = Loc->second;
    RECopy.Addend += SymInfo.getOffset();
    Relocations[SymInfo.getSectionID()].push_back(RECopy);
  }
}

uint8_t *RuntimeDyldImpl::createStubFunction(uint8_t *Addr,
                                             unsigned AbiVariant) {
  if (Arch == Triple::aarch64 || Arch == Triple::aarch64_be) {
    // This stub has to be able to access the full address space,
    // since symbol lookup won't necessarily find a handy, in-range,
    // PLT stub for functions which could be anywhere.
    // Stub can use ip0 (== x16) to calculate address
    writeBytesUnaligned(0xd2e00010, Addr,    4); // movz ip0, #:abs_g3:<addr>
    writeBytesUnaligned(0xf2c00010, Addr+4,  4); // movk ip0, #:abs_g2_nc:<addr>
    writeBytesUnaligned(0xf2a00010, Addr+8,  4); // movk ip0, #:abs_g1_nc:<addr>
    writeBytesUnaligned(0xf2800010, Addr+12, 4); // movk ip0, #:abs_g0_nc:<addr>
    writeBytesUnaligned(0xd61f0200, Addr+16, 4); // br ip0

    return Addr;
  } else if (Arch == Triple::arm || Arch == Triple::armeb) {
    // TODO: There is only ARM far stub now. We should add the Thumb stub,
    // and stubs for branches Thumb - ARM and ARM - Thumb.
    writeBytesUnaligned(0xe51ff004, Addr, 4); // ldr pc, [pc, #-4]
    return Addr + 4;
  } else if (IsMipsO32ABI || IsMipsN32ABI) {
    // 0:   3c190000        lui     t9,%hi(addr).
    // 4:   27390000        addiu   t9,t9,%lo(addr).
    // 8:   03200008        jr      t9.
    // c:   00000000        nop.
    const unsigned LuiT9Instr = 0x3c190000, AdduiT9Instr = 0x27390000;
    const unsigned NopInstr = 0x0;
    unsigned JrT9Instr = 0x03200008;
    if ((AbiVariant & ELF::EF_MIPS_ARCH) == ELF::EF_MIPS_ARCH_32R6 ||
        (AbiVariant & ELF::EF_MIPS_ARCH) == ELF::EF_MIPS_ARCH_64R6)
      JrT9Instr = 0x03200009;

    writeBytesUnaligned(LuiT9Instr, Addr, 4);
    writeBytesUnaligned(AdduiT9Instr, Addr + 4, 4);
    writeBytesUnaligned(JrT9Instr, Addr + 8, 4);
    writeBytesUnaligned(NopInstr, Addr + 12, 4);
    return Addr;
  } else if (IsMipsN64ABI) {
    // 0:   3c190000        lui     t9,%highest(addr).
    // 4:   67390000        daddiu  t9,t9,%higher(addr).
    // 8:   0019CC38        dsll    t9,t9,16.
    // c:   67390000        daddiu  t9,t9,%hi(addr).
    // 10:  0019CC38        dsll    t9,t9,16.
    // 14:  67390000        daddiu  t9,t9,%lo(addr).
    // 18:  03200008        jr      t9.
    // 1c:  00000000        nop.
    const unsigned LuiT9Instr = 0x3c190000, DaddiuT9Instr = 0x67390000,
                   DsllT9Instr = 0x19CC38;
    const unsigned NopInstr = 0x0;
    unsigned JrT9Instr = 0x03200008;
    if ((AbiVariant & ELF::EF_MIPS_ARCH) == ELF::EF_MIPS_ARCH_64R6)
      JrT9Instr = 0x03200009;

    writeBytesUnaligned(LuiT9Instr, Addr, 4);
    writeBytesUnaligned(DaddiuT9Instr, Addr + 4, 4);
    writeBytesUnaligned(DsllT9Instr, Addr + 8, 4);
    writeBytesUnaligned(DaddiuT9Instr, Addr + 12, 4);
    writeBytesUnaligned(DsllT9Instr, Addr + 16, 4);
    writeBytesUnaligned(DaddiuT9Instr, Addr + 20, 4);
    writeBytesUnaligned(JrT9Instr, Addr + 24, 4);
    writeBytesUnaligned(NopInstr, Addr + 28, 4);
    return Addr;
  } else if (Arch == Triple::ppc64 || Arch == Triple::ppc64le) {
    // Depending on which version of the ELF ABI is in use, we need to
    // generate one of two variants of the stub.  They both start with
    // the same sequence to load the target address into r12.
    writeInt32BE(Addr,    0x3D800000); // lis   r12, highest(addr)
    writeInt32BE(Addr+4,  0x618C0000); // ori   r12, higher(addr)
    writeInt32BE(Addr+8,  0x798C07C6); // sldi  r12, r12, 32
    writeInt32BE(Addr+12, 0x658C0000); // oris  r12, r12, h(addr)
    writeInt32BE(Addr+16, 0x618C0000); // ori   r12, r12, l(addr)
    if (AbiVariant == 2) {
      // PowerPC64 stub ELFv2 ABI: The address points to the function itself.
      // The address is already in r12 as required by the ABI.  Branch to it.
      writeInt32BE(Addr+20, 0xF8410018); // std   r2,  24(r1)
      writeInt32BE(Addr+24, 0x7D8903A6); // mtctr r12
      writeInt32BE(Addr+28, 0x4E800420); // bctr
    } else {
      // PowerPC64 stub ELFv1 ABI: The address points to a function descriptor.
      // Load the function address on r11 and sets it to control register. Also
      // loads the function TOC in r2 and environment pointer to r11.
      writeInt32BE(Addr+20, 0xF8410028); // std   r2,  40(r1)
      writeInt32BE(Addr+24, 0xE96C0000); // ld    r11, 0(r12)
      writeInt32BE(Addr+28, 0xE84C0008); // ld    r2,  0(r12)
      writeInt32BE(Addr+32, 0x7D6903A6); // mtctr r11
      writeInt32BE(Addr+36, 0xE96C0010); // ld    r11, 16(r2)
      writeInt32BE(Addr+40, 0x4E800420); // bctr
    }
    return Addr;
  } else if (Arch == Triple::systemz) {
    writeInt16BE(Addr,    0xC418);     // lgrl %r1,.+8
    writeInt16BE(Addr+2,  0x0000);
    writeInt16BE(Addr+4,  0x0004);
    writeInt16BE(Addr+6,  0x07F1);     // brc 15,%r1
    // 8-byte address stored at Addr + 8
    return Addr;
  } else if (Arch == Triple::x86_64) {
    *Addr      = 0xFF; // jmp
    *(Addr+1)  = 0x25; // rip
    // 32-bit PC-relative address of the GOT entry will be stored at Addr+2
  } else if (Arch == Triple::x86) {
    *Addr      = 0xE9; // 32-bit pc-relative jump.
  }
  return Addr;
}

// Assign an address to a symbol name and resolve all the relocations
// associated with it.
void RuntimeDyldImpl::reassignSectionAddress(unsigned SectionID,
                                             uint64_t Addr) {
  // The address to use for relocation resolution is not
  // the address of the local section buffer. We must be doing
  // a remote execution environment of some sort. Relocations can't
  // be applied until all the sections have been moved.  The client must
  // trigger this with a call to MCJIT::finalize() or
  // RuntimeDyld::resolveRelocations().
  //
  // Addr is a uint64_t because we can't assume the pointer width
  // of the target is the same as that of the host. Just use a generic
  // "big enough" type.
  LLVM_DEBUG(
      dbgs() << "Reassigning address for section " << SectionID << " ("
             << Sections[SectionID].getName() << "): "
             << format("0x%016" PRIx64, Sections[SectionID].getLoadAddress())
             << " -> " << format("0x%016" PRIx64, Addr) << "\n");
  Sections[SectionID].setLoadAddress(Addr);
}

void RuntimeDyldImpl::resolveRelocationList(const RelocationList &Relocs,
                                            uint64_t Value) {
  for (unsigned i = 0, e = Relocs.size(); i != e; ++i) {
    const RelocationEntry &RE = Relocs[i];
    // Ignore relocations for sections that were not loaded
    if (Sections[RE.SectionID].getAddress() == nullptr)
      continue;
    resolveRelocation(RE, Value);
  }
}

void RuntimeDyldImpl::applyExternalSymbolRelocations(
    const StringMap<JITEvaluatedSymbol> ExternalSymbolMap) {
  while (!ExternalSymbolRelocations.empty()) {

    StringMap<RelocationList>::iterator i = ExternalSymbolRelocations.begin();

    StringRef Name = i->first();
    if (Name.size() == 0) {
      // This is an absolute symbol, use an address of zero.
      LLVM_DEBUG(dbgs() << "Resolving absolute relocations."
                        << "\n");
      RelocationList &Relocs = i->second;
      resolveRelocationList(Relocs, 0);
    } else {
      uint64_t Addr = 0;
      JITSymbolFlags Flags;
      RTDyldSymbolTable::const_iterator Loc = GlobalSymbolTable.find(Name);
      if (Loc == GlobalSymbolTable.end()) {
        auto RRI = ExternalSymbolMap.find(Name);
        assert(RRI != ExternalSymbolMap.end() && "No result for symbol");
        Addr = RRI->second.getAddress();
        Flags = RRI->second.getFlags();
        // The call to getSymbolAddress may have caused additional modules to
        // be loaded, which may have added new entries to the
        // ExternalSymbolRelocations map.  Consquently, we need to update our
        // iterator.  This is also why retrieval of the relocation list
        // associated with this symbol is deferred until below this point.
        // New entries may have been added to the relocation list.
        i = ExternalSymbolRelocations.find(Name);
      } else {
        // We found the symbol in our global table.  It was probably in a
        // Module that we loaded previously.
        const auto &SymInfo = Loc->second;
        Addr = getSectionLoadAddress(SymInfo.getSectionID()) +
               SymInfo.getOffset();
        Flags = SymInfo.getFlags();
      }

      // FIXME: Implement error handling that doesn't kill the host program!
      if (!Addr)
        report_fatal_error("Program used external function '" + Name +
                           "' which could not be resolved!");

      // If Resolver returned UINT64_MAX, the client wants to handle this symbol
      // manually and we shouldn't resolve its relocations.
      if (Addr != UINT64_MAX) {

        // Tweak the address based on the symbol flags if necessary.
        // For example, this is used by RuntimeDyldMachOARM to toggle the low bit
        // if the target symbol is Thumb.
        Addr = modifyAddressBasedOnFlags(Addr, Flags);

        LLVM_DEBUG(dbgs() << "Resolving relocations Name: " << Name << "\t"
                          << format("0x%lx", Addr) << "\n");
        // This list may have been updated when we called getSymbolAddress, so
        // don't change this code to get the list earlier.
        RelocationList &Relocs = i->second;
        resolveRelocationList(Relocs, Addr);
      }
    }

    ExternalSymbolRelocations.erase(i);
  }
}

Error RuntimeDyldImpl::resolveExternalSymbols() {
  StringMap<JITEvaluatedSymbol> ExternalSymbolMap;

  // Resolution can trigger emission of more symbols, so iterate until
  // we've resolved *everything*.
  {
    JITSymbolResolver::LookupSet ResolvedSymbols;

    while (true) {
      JITSymbolResolver::LookupSet NewSymbols;

      for (auto &RelocKV : ExternalSymbolRelocations) {
        StringRef Name = RelocKV.first();
        if (!Name.empty() && !GlobalSymbolTable.count(Name) &&
            !ResolvedSymbols.count(Name))
          NewSymbols.insert(Name);
      }

      if (NewSymbols.empty())
        break;

#ifdef _MSC_VER
      using ExpectedLookupResult =
          MSVCPExpected<JITSymbolResolver::LookupResult>;
#else
      using ExpectedLookupResult = Expected<JITSymbolResolver::LookupResult>;
#endif

      auto NewSymbolsP = std::make_shared<std::promise<ExpectedLookupResult>>();
      auto NewSymbolsF = NewSymbolsP->get_future();
      Resolver.lookup(NewSymbols,
                      [=](Expected<JITSymbolResolver::LookupResult> Result) {
                        NewSymbolsP->set_value(std::move(Result));
                      });

      auto NewResolverResults = NewSymbolsF.get();

      if (!NewResolverResults)
        return NewResolverResults.takeError();

      assert(NewResolverResults->size() == NewSymbols.size() &&
             "Should have errored on unresolved symbols");

      for (auto &RRKV : *NewResolverResults) {
        assert(!ResolvedSymbols.count(RRKV.first) && "Redundant resolution?");
        ExternalSymbolMap.insert(RRKV);
        ResolvedSymbols.insert(RRKV.first);
      }
    }
  }

  applyExternalSymbolRelocations(ExternalSymbolMap);

  return Error::success();
}

void RuntimeDyldImpl::finalizeAsync(
    std::unique_ptr<RuntimeDyldImpl> This, std::function<void(Error)> OnEmitted,
    std::unique_ptr<MemoryBuffer> UnderlyingBuffer) {

  // FIXME: Move-capture OnRelocsApplied and UnderlyingBuffer once we have
  // c++14.
  auto SharedUnderlyingBuffer =
      std::shared_ptr<MemoryBuffer>(std::move(UnderlyingBuffer));
  auto SharedThis = std::shared_ptr<RuntimeDyldImpl>(std::move(This));
  auto PostResolveContinuation =
      [SharedThis, OnEmitted, SharedUnderlyingBuffer](
          Expected<JITSymbolResolver::LookupResult> Result) {
        if (!Result) {
          OnEmitted(Result.takeError());
          return;
        }

        /// Copy the result into a StringMap, where the keys are held by value.
        StringMap<JITEvaluatedSymbol> Resolved;
        for (auto &KV : *Result)
          Resolved[KV.first] = KV.second;

        SharedThis->applyExternalSymbolRelocations(Resolved);
        SharedThis->resolveLocalRelocations();
        SharedThis->registerEHFrames();
        std::string ErrMsg;
        if (SharedThis->MemMgr.finalizeMemory(&ErrMsg))
          OnEmitted(make_error<StringError>(std::move(ErrMsg),
                                            inconvertibleErrorCode()));
        else
          OnEmitted(Error::success());
      };

  JITSymbolResolver::LookupSet Symbols;

  for (auto &RelocKV : SharedThis->ExternalSymbolRelocations) {
    StringRef Name = RelocKV.first();
    assert(!Name.empty() && "Symbol has no name?");
    assert(!SharedThis->GlobalSymbolTable.count(Name) &&
           "Name already processed. RuntimeDyld instances can not be re-used "
           "when finalizing with finalizeAsync.");
    Symbols.insert(Name);
  }

  if (!Symbols.empty()) {
    SharedThis->Resolver.lookup(Symbols, PostResolveContinuation);
  } else
    PostResolveContinuation(std::map<StringRef, JITEvaluatedSymbol>());
}

//===----------------------------------------------------------------------===//
// RuntimeDyld class implementation

uint64_t RuntimeDyld::LoadedObjectInfo::getSectionLoadAddress(
                                          const object::SectionRef &Sec) const {

  auto I = ObjSecToIDMap.find(Sec);
  if (I != ObjSecToIDMap.end())
    return RTDyld.Sections[I->second].getLoadAddress();

  return 0;
}

void RuntimeDyld::MemoryManager::anchor() {}
void JITSymbolResolver::anchor() {}
void LegacyJITSymbolResolver::anchor() {}

RuntimeDyld::RuntimeDyld(RuntimeDyld::MemoryManager &MemMgr,
                         JITSymbolResolver &Resolver)
    : MemMgr(MemMgr), Resolver(Resolver) {
  // FIXME: There's a potential issue lurking here if a single instance of
  // RuntimeDyld is used to load multiple objects.  The current implementation
  // associates a single memory manager with a RuntimeDyld instance.  Even
  // though the public class spawns a new 'impl' instance for each load,
  // they share a single memory manager.  This can become a problem when page
  // permissions are applied.
  Dyld = nullptr;
  ProcessAllSections = false;
  Checker = nullptr;
}

RuntimeDyld::~RuntimeDyld() {}

static std::unique_ptr<RuntimeDyldCOFF>
createRuntimeDyldCOFF(Triple::ArchType Arch, RuntimeDyld::MemoryManager &MM,
                      JITSymbolResolver &Resolver, bool ProcessAllSections,
                      RuntimeDyldCheckerImpl *Checker) {
  std::unique_ptr<RuntimeDyldCOFF> Dyld =
    RuntimeDyldCOFF::create(Arch, MM, Resolver);
  Dyld->setProcessAllSections(ProcessAllSections);
  Dyld->setRuntimeDyldChecker(Checker);
  return Dyld;
}

static std::unique_ptr<RuntimeDyldELF>
createRuntimeDyldELF(Triple::ArchType Arch, RuntimeDyld::MemoryManager &MM,
                     JITSymbolResolver &Resolver, bool ProcessAllSections,
                     RuntimeDyldCheckerImpl *Checker) {
  std::unique_ptr<RuntimeDyldELF> Dyld =
      RuntimeDyldELF::create(Arch, MM, Resolver);
  Dyld->setProcessAllSections(ProcessAllSections);
  Dyld->setRuntimeDyldChecker(Checker);
  return Dyld;
}

static std::unique_ptr<RuntimeDyldMachO>
createRuntimeDyldMachO(Triple::ArchType Arch, RuntimeDyld::MemoryManager &MM,
                       JITSymbolResolver &Resolver,
                       bool ProcessAllSections,
                       RuntimeDyldCheckerImpl *Checker) {
  std::unique_ptr<RuntimeDyldMachO> Dyld =
    RuntimeDyldMachO::create(Arch, MM, Resolver);
  Dyld->setProcessAllSections(ProcessAllSections);
  Dyld->setRuntimeDyldChecker(Checker);
  return Dyld;
}

std::unique_ptr<RuntimeDyld::LoadedObjectInfo>
RuntimeDyld::loadObject(const ObjectFile &Obj) {
  if (!Dyld) {
    if (Obj.isELF())
      Dyld =
          createRuntimeDyldELF(static_cast<Triple::ArchType>(Obj.getArch()),
                               MemMgr, Resolver, ProcessAllSections, Checker);
    else if (Obj.isMachO())
      Dyld = createRuntimeDyldMachO(
               static_cast<Triple::ArchType>(Obj.getArch()), MemMgr, Resolver,
               ProcessAllSections, Checker);
    else if (Obj.isCOFF())
      Dyld = createRuntimeDyldCOFF(
               static_cast<Triple::ArchType>(Obj.getArch()), MemMgr, Resolver,
               ProcessAllSections, Checker);
    else
      report_fatal_error("Incompatible object format!");
  }

  if (!Dyld->isCompatibleFile(Obj))
    report_fatal_error("Incompatible object format!");

  auto LoadedObjInfo = Dyld->loadObject(Obj);
  MemMgr.notifyObjectLoaded(*this, Obj);
  return LoadedObjInfo;
}

void *RuntimeDyld::getSymbolLocalAddress(StringRef Name) const {
  if (!Dyld)
    return nullptr;
  return Dyld->getSymbolLocalAddress(Name);
}

JITEvaluatedSymbol RuntimeDyld::getSymbol(StringRef Name) const {
  if (!Dyld)
    return nullptr;
  return Dyld->getSymbol(Name);
}

std::map<StringRef, JITEvaluatedSymbol> RuntimeDyld::getSymbolTable() const {
  if (!Dyld)
    return std::map<StringRef, JITEvaluatedSymbol>();
  return Dyld->getSymbolTable();
}

void RuntimeDyld::resolveRelocations() { Dyld->resolveRelocations(); }

void RuntimeDyld::reassignSectionAddress(unsigned SectionID, uint64_t Addr) {
  Dyld->reassignSectionAddress(SectionID, Addr);
}

void RuntimeDyld::mapSectionAddress(const void *LocalAddress,
                                    uint64_t TargetAddress) {
  Dyld->mapSectionAddress(LocalAddress, TargetAddress);
}

bool RuntimeDyld::hasError() { return Dyld->hasError(); }

StringRef RuntimeDyld::getErrorString() { return Dyld->getErrorString(); }

void RuntimeDyld::finalizeWithMemoryManagerLocking() {
  bool MemoryFinalizationLocked = MemMgr.FinalizationLocked;
  MemMgr.FinalizationLocked = true;
  resolveRelocations();
  registerEHFrames();
  if (!MemoryFinalizationLocked) {
    MemMgr.finalizeMemory();
    MemMgr.FinalizationLocked = false;
  }
}

void RuntimeDyld::registerEHFrames() {
  if (Dyld)
    Dyld->registerEHFrames();
}

void RuntimeDyld::deregisterEHFrames() {
  if (Dyld)
    Dyld->deregisterEHFrames();
}
// FIXME: Kill this with fire once we have a new JIT linker: this is only here
// so that we can re-use RuntimeDyld's implementation without twisting the
// interface any further for ORC's purposes.
void jitLinkForORC(object::ObjectFile &Obj,
                   std::unique_ptr<MemoryBuffer> UnderlyingBuffer,
                   RuntimeDyld::MemoryManager &MemMgr,
                   JITSymbolResolver &Resolver, bool ProcessAllSections,
                   std::function<Error(
                       std::unique_ptr<RuntimeDyld::LoadedObjectInfo> LoadedObj,
                       std::map<StringRef, JITEvaluatedSymbol>)>
                       OnLoaded,
                   std::function<void(Error)> OnEmitted) {

  RuntimeDyld RTDyld(MemMgr, Resolver);
  RTDyld.setProcessAllSections(ProcessAllSections);

  auto Info = RTDyld.loadObject(Obj);

  if (RTDyld.hasError()) {
    OnEmitted(make_error<StringError>(RTDyld.getErrorString(),
                                      inconvertibleErrorCode()));
    return;
  }

  if (auto Err = OnLoaded(std::move(Info), RTDyld.getSymbolTable()))
    OnEmitted(std::move(Err));

  RuntimeDyldImpl::finalizeAsync(std::move(RTDyld.Dyld), std::move(OnEmitted),
                                 std::move(UnderlyingBuffer));
}

} // end namespace llvm
