//===-- RuntimeDyldCOFFX86_64.h --- COFF/X86_64 specific code ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// COFF x86_x64 support for MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFF86_64_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFF86_64_H

#include "../RuntimeDyldCOFF.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"

#define DEBUG_TYPE "dyld"

namespace llvm {

class RuntimeDyldCOFFX86_64 : public RuntimeDyldCOFF {

private:
  // When a module is loaded we save the SectionID of the unwind
  // sections in a table until we receive a request to register all
  // unregisteredEH frame sections with the memory manager.
  SmallVector<SID, 2> UnregisteredEHFrameSections;
  SmallVector<SID, 2> RegisteredEHFrameSections;
  uint64_t ImageBase;

  // Fake an __ImageBase pointer by returning the section with the lowest adress
  uint64_t getImageBase() {
    if (!ImageBase) {
      ImageBase = std::numeric_limits<uint64_t>::max();
      for (const SectionEntry &Section : Sections)
        // The Sections list may contain sections that weren't loaded for
        // whatever reason: they may be debug sections, and ProcessAllSections
        // is false, or they may be sections that contain 0 bytes. If the
        // section isn't loaded, the load address will be 0, and it should not
        // be included in the ImageBase calculation.
        if (Section.getLoadAddress() != 0)
          ImageBase = std::min(ImageBase, Section.getLoadAddress());
    }
    return ImageBase;
  }

  void write32BitOffset(uint8_t *Target, int64_t Addend, uint64_t Delta) {
    uint64_t Result = Addend + Delta;
    assert(Result <= UINT32_MAX && "Relocation overflow");
    writeBytesUnaligned(Result, Target, 4);
  }

public:
  RuntimeDyldCOFFX86_64(RuntimeDyld::MemoryManager &MM,
                        JITSymbolResolver &Resolver)
    : RuntimeDyldCOFF(MM, Resolver), ImageBase(0) {}

  unsigned getStubAlignment() override { return 1; }

  // 2-byte jmp instruction + 32-bit relative address + 64-bit absolute jump
  unsigned getMaxStubSize() override { return 14; }

  // The target location for the relocation is described by RE.SectionID and
  // RE.Offset.  RE.SectionID can be used to find the SectionEntry.  Each
  // SectionEntry has three members describing its location.
  // SectionEntry::Address is the address at which the section has been loaded
  // into memory in the current (host) process.  SectionEntry::LoadAddress is
  // the address that the section will have in the target process.
  // SectionEntry::ObjAddress is the address of the bits for this section in the
  // original emitted object image (also in the current address space).
  //
  // Relocations will be applied as if the section were loaded at
  // SectionEntry::LoadAddress, but they will be applied at an address based
  // on SectionEntry::Address.  SectionEntry::ObjAddress will be used to refer
  // to Target memory contents if they are required for value calculations.
  //
  // The Value parameter here is the load address of the symbol for the
  // relocation to be applied.  For relocations which refer to symbols in the
  // current object Value will be the LoadAddress of the section in which
  // the symbol resides (RE.Addend provides additional information about the
  // symbol location).  For external symbols, Value will be the address of the
  // symbol in the target address space.
  void resolveRelocation(const RelocationEntry &RE, uint64_t Value) override {
    const SectionEntry &Section = Sections[RE.SectionID];
    uint8_t *Target = Section.getAddressWithOffset(RE.Offset);

    switch (RE.RelType) {

    case COFF::IMAGE_REL_AMD64_REL32:
    case COFF::IMAGE_REL_AMD64_REL32_1:
    case COFF::IMAGE_REL_AMD64_REL32_2:
    case COFF::IMAGE_REL_AMD64_REL32_3:
    case COFF::IMAGE_REL_AMD64_REL32_4:
    case COFF::IMAGE_REL_AMD64_REL32_5: {
      uint64_t FinalAddress = Section.getLoadAddressWithOffset(RE.Offset);
      // Delta is the distance from the start of the reloc to the end of the
      // instruction with the reloc.
      uint64_t Delta = 4 + (RE.RelType - COFF::IMAGE_REL_AMD64_REL32);
      Value -= FinalAddress + Delta;
      uint64_t Result = Value + RE.Addend;
      assert(((int64_t)Result <= INT32_MAX) && "Relocation overflow");
      assert(((int64_t)Result >= INT32_MIN) && "Relocation underflow");
      writeBytesUnaligned(Result, Target, 4);
      break;
    }

    case COFF::IMAGE_REL_AMD64_ADDR32NB: {
      // ADDR32NB requires an offset less than 2GB from 'ImageBase'.
      // The MemoryManager can make sure this is always true by forcing the
      // memory layout to be: CodeSection < ReadOnlySection < ReadWriteSection.
      const uint64_t ImageBase = getImageBase();
      if (Value < ImageBase || ((Value - ImageBase) > UINT32_MAX)) {
        llvm::errs() << "IMAGE_REL_AMD64_ADDR32NB relocation requires an"
                     << "ordered section layout.\n";
        write32BitOffset(Target, 0, 0);
      } else {
        write32BitOffset(Target, RE.Addend, Value - ImageBase);
      }
      break;
    }

    case COFF::IMAGE_REL_AMD64_ADDR64: {
      writeBytesUnaligned(Value + RE.Addend, Target, 8);
      break;
    }

    case COFF::IMAGE_REL_AMD64_SECREL: {
      assert(static_cast<int64_t>(RE.Addend) <= INT32_MAX && "Relocation overflow");
      assert(static_cast<int64_t>(RE.Addend) >= INT32_MIN && "Relocation underflow");
      writeBytesUnaligned(RE.Addend, Target, 4);
      break;
    }

    default:
      llvm_unreachable("Relocation type not implemented yet!");
      break;
    }
  }

  std::tuple<uint64_t, uint64_t, uint64_t>
  generateRelocationStub(unsigned SectionID, StringRef TargetName,
                         uint64_t Offset, uint64_t RelType, uint64_t Addend,
                         StubMap &Stubs) {
    uintptr_t StubOffset;
    SectionEntry &Section = Sections[SectionID];

    RelocationValueRef OriginalRelValueRef;
    OriginalRelValueRef.SectionID = SectionID;
    OriginalRelValueRef.Offset = Offset;
    OriginalRelValueRef.Addend = Addend;
    OriginalRelValueRef.SymbolName = TargetName.data();

    auto Stub = Stubs.find(OriginalRelValueRef);
    if (Stub == Stubs.end()) {
      LLVM_DEBUG(dbgs() << " Create a new stub function for "
                        << TargetName.data() << "\n");

      StubOffset = Section.getStubOffset();
      Stubs[OriginalRelValueRef] = StubOffset;
      createStubFunction(Section.getAddressWithOffset(StubOffset));
      Section.advanceStubOffset(getMaxStubSize());
    } else {
      LLVM_DEBUG(dbgs() << " Stub function found for " << TargetName.data()
                        << "\n");
      StubOffset = Stub->second;
    }

    // FIXME: If RelType == COFF::IMAGE_REL_AMD64_ADDR32NB we should be able
    // to ignore the __ImageBase requirement and just forward to the stub
    // directly as an offset of this section:
    // write32BitOffset(Section.getAddressWithOffset(Offset), 0, StubOffset);
    // .xdata exception handler's aren't having this though.

    // Resolve original relocation to stub function.
    const RelocationEntry RE(SectionID, Offset, RelType, Addend);
    resolveRelocation(RE, Section.getLoadAddressWithOffset(StubOffset));

    // adjust relocation info so resolution writes to the stub function
    Addend = 0;
    Offset = StubOffset + 6;
    RelType = COFF::IMAGE_REL_AMD64_ADDR64;

    return std::make_tuple(Offset, RelType, Addend);
  }

  Expected<relocation_iterator>
  processRelocationRef(unsigned SectionID,
                       relocation_iterator RelI,
                       const ObjectFile &Obj,
                       ObjSectionToIDMap &ObjSectionToID,
                       StubMap &Stubs) override {
    // If possible, find the symbol referred to in the relocation,
    // and the section that contains it.
    symbol_iterator Symbol = RelI->getSymbol();
    if (Symbol == Obj.symbol_end())
      report_fatal_error("Unknown symbol in relocation");
    auto SectionOrError = Symbol->getSection();
    if (!SectionOrError)
      return SectionOrError.takeError();
    section_iterator SecI = *SectionOrError;
    // If there is no section, this must be an external reference.
    const bool IsExtern = SecI == Obj.section_end();

    // Determine the Addend used to adjust the relocation value.
    uint64_t RelType = RelI->getType();
    uint64_t Offset = RelI->getOffset();
    uint64_t Addend = 0;
    SectionEntry &Section = Sections[SectionID];
    uintptr_t ObjTarget = Section.getObjAddress() + Offset;

    Expected<StringRef> TargetNameOrErr = Symbol->getName();
    if (!TargetNameOrErr)
      return TargetNameOrErr.takeError();
    StringRef TargetName = *TargetNameOrErr;

    switch (RelType) {

    case COFF::IMAGE_REL_AMD64_REL32:
    case COFF::IMAGE_REL_AMD64_REL32_1:
    case COFF::IMAGE_REL_AMD64_REL32_2:
    case COFF::IMAGE_REL_AMD64_REL32_3:
    case COFF::IMAGE_REL_AMD64_REL32_4:
    case COFF::IMAGE_REL_AMD64_REL32_5:
    case COFF::IMAGE_REL_AMD64_ADDR32NB: {
      uint8_t *Displacement = (uint8_t *)ObjTarget;
      Addend = readBytesUnaligned(Displacement, 4);

      if (IsExtern)
        std::tie(Offset, RelType, Addend) = generateRelocationStub(
          SectionID, TargetName, Offset, RelType, Addend, Stubs);

      break;
    }

    case COFF::IMAGE_REL_AMD64_ADDR64: {
      uint8_t *Displacement = (uint8_t *)ObjTarget;
      Addend = readBytesUnaligned(Displacement, 8);
      break;
    }

    default:
      break;
    }

    LLVM_DEBUG(dbgs() << "\t\tIn Section " << SectionID << " Offset " << Offset
                      << " RelType: " << RelType << " TargetName: "
                      << TargetName << " Addend " << Addend << "\n");

    if (IsExtern) {
      RelocationEntry RE(SectionID, Offset, RelType, Addend);
      addRelocationForSymbol(RE, TargetName);
    } else {
      bool IsCode = SecI->isText();
      unsigned TargetSectionID;
      if (auto TargetSectionIDOrErr =
          findOrEmitSection(Obj, *SecI, IsCode, ObjSectionToID))
        TargetSectionID = *TargetSectionIDOrErr;
      else
        return TargetSectionIDOrErr.takeError();
      uint64_t TargetOffset = getSymbolOffset(*Symbol);
      RelocationEntry RE(SectionID, Offset, RelType, TargetOffset + Addend);
      addRelocationForSection(RE, TargetSectionID);
    }

    return ++RelI;
  }

  void registerEHFrames() override {
    for (auto const &EHFrameSID : UnregisteredEHFrameSections) {
      uint8_t *EHFrameAddr = Sections[EHFrameSID].getAddress();
      uint64_t EHFrameLoadAddr = Sections[EHFrameSID].getLoadAddress();
      size_t EHFrameSize = Sections[EHFrameSID].getSize();
      MemMgr.registerEHFrames(EHFrameAddr, EHFrameLoadAddr, EHFrameSize);
      RegisteredEHFrameSections.push_back(EHFrameSID);
    }
    UnregisteredEHFrameSections.clear();
  }

  Error finalizeLoad(const ObjectFile &Obj,
                     ObjSectionToIDMap &SectionMap) override {
    // Look for and record the EH frame section IDs.
    for (const auto &SectionPair : SectionMap) {
      const SectionRef &Section = SectionPair.first;
      StringRef Name;
      if (auto EC = Section.getName(Name))
        return errorCodeToError(EC);

      // Note unwind info is stored in .pdata but often points to .xdata
      // with an IMAGE_REL_AMD64_ADDR32NB relocation. Using a memory manager
      // that keeps sections ordered in relation to __ImageBase is necessary.
      if (Name == ".pdata")
        UnregisteredEHFrameSections.push_back(SectionPair.second);
    }
    return Error::success();
  }
};

} // end namespace llvm

#undef DEBUG_TYPE

#endif
