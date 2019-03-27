//===---- RuntimeDyldMachOI386.h ---- MachO/I386 specific code. ---*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDMACHOI386_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDMACHOI386_H

#include "../RuntimeDyldMachO.h"
#include <string>

#define DEBUG_TYPE "dyld"

namespace llvm {

class RuntimeDyldMachOI386
    : public RuntimeDyldMachOCRTPBase<RuntimeDyldMachOI386> {
public:

  typedef uint32_t TargetPtrT;

  RuntimeDyldMachOI386(RuntimeDyld::MemoryManager &MM,
                       JITSymbolResolver &Resolver)
      : RuntimeDyldMachOCRTPBase(MM, Resolver) {}

  unsigned getMaxStubSize() override { return 0; }

  unsigned getStubAlignment() override { return 1; }

  Expected<relocation_iterator>
  processRelocationRef(unsigned SectionID, relocation_iterator RelI,
                       const ObjectFile &BaseObjT,
                       ObjSectionToIDMap &ObjSectionToID,
                       StubMap &Stubs) override {
    const MachOObjectFile &Obj =
        static_cast<const MachOObjectFile &>(BaseObjT);
    MachO::any_relocation_info RelInfo =
        Obj.getRelocation(RelI->getRawDataRefImpl());
    uint32_t RelType = Obj.getAnyRelocationType(RelInfo);

    if (Obj.isRelocationScattered(RelInfo)) {
      if (RelType == MachO::GENERIC_RELOC_SECTDIFF ||
          RelType == MachO::GENERIC_RELOC_LOCAL_SECTDIFF)
        return processSECTDIFFRelocation(SectionID, RelI, Obj,
                                         ObjSectionToID);
      else if (RelType == MachO::GENERIC_RELOC_VANILLA)
        return processScatteredVANILLA(SectionID, RelI, Obj, ObjSectionToID);
      return make_error<RuntimeDyldError>(("Unhandled I386 scattered relocation "
                                           "type: " + Twine(RelType)).str());
    }

    switch (RelType) {
    UNIMPLEMENTED_RELOC(MachO::GENERIC_RELOC_PAIR);
    UNIMPLEMENTED_RELOC(MachO::GENERIC_RELOC_PB_LA_PTR);
    UNIMPLEMENTED_RELOC(MachO::GENERIC_RELOC_TLV);
    default:
      if (RelType > MachO::GENERIC_RELOC_TLV)
        return make_error<RuntimeDyldError>(("MachO I386 relocation type " +
                                             Twine(RelType) +
                                             " is out of range").str());
      break;
    }

    RelocationEntry RE(getRelocationEntry(SectionID, Obj, RelI));
    RE.Addend = memcpyAddend(RE);
    RelocationValueRef Value;
    if (auto ValueOrErr = getRelocationValueRef(Obj, RelI, RE, ObjSectionToID))
      Value = *ValueOrErr;
    else
      return ValueOrErr.takeError();

    // Addends for external, PC-rel relocations on i386 point back to the zero
    // offset. Calculate the final offset from the relocation target instead.
    // This allows us to use the same logic for both external and internal
    // relocations in resolveI386RelocationRef.
    // bool IsExtern = Obj.getPlainRelocationExternal(RelInfo);
    // if (IsExtern && RE.IsPCRel) {
    //   uint64_t RelocAddr = 0;
    //   RelI->getAddress(RelocAddr);
    //   Value.Addend += RelocAddr + 4;
    // }
    if (RE.IsPCRel)
      makeValueAddendPCRel(Value, RelI, 1 << RE.Size);

    RE.Addend = Value.Offset;

    if (Value.SymbolName)
      addRelocationForSymbol(RE, Value.SymbolName);
    else
      addRelocationForSection(RE, Value.SectionID);

    return ++RelI;
  }

  void resolveRelocation(const RelocationEntry &RE, uint64_t Value) override {
    LLVM_DEBUG(dumpRelocationToResolve(RE, Value));

    const SectionEntry &Section = Sections[RE.SectionID];
    uint8_t *LocalAddress = Section.getAddressWithOffset(RE.Offset);

    if (RE.IsPCRel) {
      uint64_t FinalAddress = Section.getLoadAddressWithOffset(RE.Offset);
      Value -= FinalAddress + 4; // see MachOX86_64::resolveRelocation.
    }

    switch (RE.RelType) {
    case MachO::GENERIC_RELOC_VANILLA:
      writeBytesUnaligned(Value + RE.Addend, LocalAddress, 1 << RE.Size);
      break;
    case MachO::GENERIC_RELOC_SECTDIFF:
    case MachO::GENERIC_RELOC_LOCAL_SECTDIFF: {
      uint64_t SectionABase = Sections[RE.Sections.SectionA].getLoadAddress();
      uint64_t SectionBBase = Sections[RE.Sections.SectionB].getLoadAddress();
      assert((Value == SectionABase || Value == SectionBBase) &&
             "Unexpected SECTDIFF relocation value.");
      Value = SectionABase - SectionBBase + RE.Addend;
      writeBytesUnaligned(Value, LocalAddress, 1 << RE.Size);
      break;
    }
    default:
      llvm_unreachable("Invalid relocation type!");
    }
  }

  Error finalizeSection(const ObjectFile &Obj, unsigned SectionID,
                       const SectionRef &Section) {
    StringRef Name;
    Section.getName(Name);

    if (Name == "__jump_table")
      return populateJumpTable(cast<MachOObjectFile>(Obj), Section, SectionID);
    else if (Name == "__pointers")
      return populateIndirectSymbolPointersSection(cast<MachOObjectFile>(Obj),
                                                   Section, SectionID);
    return Error::success();
  }

private:
  Expected<relocation_iterator>
  processSECTDIFFRelocation(unsigned SectionID, relocation_iterator RelI,
                            const ObjectFile &BaseObjT,
                            ObjSectionToIDMap &ObjSectionToID) {
    const MachOObjectFile &Obj =
        static_cast<const MachOObjectFile&>(BaseObjT);
    MachO::any_relocation_info RE =
        Obj.getRelocation(RelI->getRawDataRefImpl());

    SectionEntry &Section = Sections[SectionID];
    uint32_t RelocType = Obj.getAnyRelocationType(RE);
    bool IsPCRel = Obj.getAnyRelocationPCRel(RE);
    unsigned Size = Obj.getAnyRelocationLength(RE);
    uint64_t Offset = RelI->getOffset();
    uint8_t *LocalAddress = Section.getAddressWithOffset(Offset);
    unsigned NumBytes = 1 << Size;
    uint64_t Addend = readBytesUnaligned(LocalAddress, NumBytes);

    ++RelI;
    MachO::any_relocation_info RE2 =
        Obj.getRelocation(RelI->getRawDataRefImpl());

    uint32_t AddrA = Obj.getScatteredRelocationValue(RE);
    section_iterator SAI = getSectionByAddress(Obj, AddrA);
    assert(SAI != Obj.section_end() && "Can't find section for address A");
    uint64_t SectionABase = SAI->getAddress();
    uint64_t SectionAOffset = AddrA - SectionABase;
    SectionRef SectionA = *SAI;
    bool IsCode = SectionA.isText();
    uint32_t SectionAID = ~0U;
    if (auto SectionAIDOrErr =
        findOrEmitSection(Obj, SectionA, IsCode, ObjSectionToID))
      SectionAID = *SectionAIDOrErr;
    else
      return SectionAIDOrErr.takeError();

    uint32_t AddrB = Obj.getScatteredRelocationValue(RE2);
    section_iterator SBI = getSectionByAddress(Obj, AddrB);
    assert(SBI != Obj.section_end() && "Can't find section for address B");
    uint64_t SectionBBase = SBI->getAddress();
    uint64_t SectionBOffset = AddrB - SectionBBase;
    SectionRef SectionB = *SBI;
    uint32_t SectionBID = ~0U;
    if (auto SectionBIDOrErr =
        findOrEmitSection(Obj, SectionB, IsCode, ObjSectionToID))
      SectionBID = *SectionBIDOrErr;
    else
      return SectionBIDOrErr.takeError();

    // Compute the addend 'C' from the original expression 'A - B + C'.
    Addend -= AddrA - AddrB;

    LLVM_DEBUG(dbgs() << "Found SECTDIFF: AddrA: " << AddrA
                      << ", AddrB: " << AddrB << ", Addend: " << Addend
                      << ", SectionA ID: " << SectionAID << ", SectionAOffset: "
                      << SectionAOffset << ", SectionB ID: " << SectionBID
                      << ", SectionBOffset: " << SectionBOffset << "\n");
    RelocationEntry R(SectionID, Offset, RelocType, Addend, SectionAID,
                      SectionAOffset, SectionBID, SectionBOffset,
                      IsPCRel, Size);

    addRelocationForSection(R, SectionAID);

    return ++RelI;
  }

  // Populate stubs in __jump_table section.
  Error populateJumpTable(const MachOObjectFile &Obj,
                          const SectionRef &JTSection,
                         unsigned JTSectionID) {
    MachO::dysymtab_command DySymTabCmd = Obj.getDysymtabLoadCommand();
    MachO::section Sec32 = Obj.getSection(JTSection.getRawDataRefImpl());
    uint32_t JTSectionSize = Sec32.size;
    unsigned FirstIndirectSymbol = Sec32.reserved1;
    unsigned JTEntrySize = Sec32.reserved2;
    unsigned NumJTEntries = JTSectionSize / JTEntrySize;
    uint8_t *JTSectionAddr = getSectionAddress(JTSectionID);
    unsigned JTEntryOffset = 0;

    if (JTSectionSize % JTEntrySize != 0)
      return make_error<RuntimeDyldError>("Jump-table section does not contain "
                                          "a whole number of stubs?");

    for (unsigned i = 0; i < NumJTEntries; ++i) {
      unsigned SymbolIndex =
          Obj.getIndirectSymbolTableEntry(DySymTabCmd, FirstIndirectSymbol + i);
      symbol_iterator SI = Obj.getSymbolByIndex(SymbolIndex);
      Expected<StringRef> IndirectSymbolName = SI->getName();
      if (!IndirectSymbolName)
        return IndirectSymbolName.takeError();
      uint8_t *JTEntryAddr = JTSectionAddr + JTEntryOffset;
      createStubFunction(JTEntryAddr);
      RelocationEntry RE(JTSectionID, JTEntryOffset + 1,
                         MachO::GENERIC_RELOC_VANILLA, 0, true, 2);
      addRelocationForSymbol(RE, *IndirectSymbolName);
      JTEntryOffset += JTEntrySize;
    }

    return Error::success();
  }

};
}

#undef DEBUG_TYPE

#endif
