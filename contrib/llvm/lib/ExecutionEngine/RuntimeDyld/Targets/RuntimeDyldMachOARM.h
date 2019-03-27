//===----- RuntimeDyldMachOARM.h ---- MachO/ARM specific code. ----*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDMACHOARM_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDMACHOARM_H

#include "../RuntimeDyldMachO.h"
#include <string>

#define DEBUG_TYPE "dyld"

namespace llvm {

class RuntimeDyldMachOARM
    : public RuntimeDyldMachOCRTPBase<RuntimeDyldMachOARM> {
private:
  typedef RuntimeDyldMachOCRTPBase<RuntimeDyldMachOARM> ParentT;

public:

  typedef uint32_t TargetPtrT;

  RuntimeDyldMachOARM(RuntimeDyld::MemoryManager &MM,
                      JITSymbolResolver &Resolver)
    : RuntimeDyldMachOCRTPBase(MM, Resolver) {}

  unsigned getMaxStubSize() override { return 8; }

  unsigned getStubAlignment() override { return 4; }

  Expected<JITSymbolFlags> getJITSymbolFlags(const SymbolRef &SR) override {
    auto Flags = RuntimeDyldImpl::getJITSymbolFlags(SR);
    if (!Flags)
      return Flags.takeError();
    Flags->getTargetFlags() = ARMJITSymbolFlags::fromObjectSymbol(SR);
    return Flags;
  }

  uint64_t modifyAddressBasedOnFlags(uint64_t Addr,
                                     JITSymbolFlags Flags) const override {
    if (Flags.getTargetFlags() & ARMJITSymbolFlags::Thumb)
      Addr |= 0x1;
    return Addr;
  }

  bool isAddrTargetThumb(unsigned SectionID, uint64_t Offset) {
    auto TargetObjAddr = Sections[SectionID].getObjAddress() + Offset;
    for (auto &KV : GlobalSymbolTable) {
      auto &Entry = KV.second;
      auto SymbolObjAddr =
          Sections[Entry.getSectionID()].getObjAddress() + Entry.getOffset();
      if (TargetObjAddr == SymbolObjAddr)
        return (Entry.getFlags().getTargetFlags() & ARMJITSymbolFlags::Thumb);
    }
    return false;
  }

  Expected<int64_t> decodeAddend(const RelocationEntry &RE) const {
    const SectionEntry &Section = Sections[RE.SectionID];
    uint8_t *LocalAddress = Section.getAddressWithOffset(RE.Offset);

    switch (RE.RelType) {
      default:
        return memcpyAddend(RE);
      case MachO::ARM_RELOC_BR24: {
        uint32_t Temp = readBytesUnaligned(LocalAddress, 4);
        Temp &= 0x00ffffff; // Mask out the opcode.
        // Now we've got the shifted immediate, shift by 2, sign extend and ret.
        return SignExtend32<26>(Temp << 2);
      }

      case MachO::ARM_THUMB_RELOC_BR22: {
        // This is a pair of instructions whose operands combine to provide 22
        // bits of displacement:
        // Encoding for high bits 1111 0XXX XXXX XXXX
        // Encoding for low bits  1111 1XXX XXXX XXXX
        uint16_t HighInsn = readBytesUnaligned(LocalAddress, 2);
        if ((HighInsn & 0xf800) != 0xf000)
          return make_error<StringError>("Unrecognized thumb branch encoding "
                                         "(BR22 high bits)",
                                         inconvertibleErrorCode());

        uint16_t LowInsn = readBytesUnaligned(LocalAddress + 2, 2);
        if ((LowInsn & 0xf800) != 0xf800)
          return make_error<StringError>("Unrecognized thumb branch encoding "
                                         "(BR22 low bits)",
                                         inconvertibleErrorCode());

        return SignExtend64<23>(((HighInsn & 0x7ff) << 12) |
                                ((LowInsn & 0x7ff) << 1));
      }
    }
  }

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

    // Set to true for thumb functions in this (or previous) TUs.
    // Will be used to set the TargetIsThumbFunc member on the relocation entry.
    bool TargetIsLocalThumbFunc = false;
    if (Obj.getPlainRelocationExternal(RelInfo)) {
      auto Symbol = RelI->getSymbol();
      StringRef TargetName;
      if (auto TargetNameOrErr = Symbol->getName())
        TargetName = *TargetNameOrErr;
      else
        return TargetNameOrErr.takeError();

      // If the target is external but the value doesn't have a name then we've
      // converted the value to a section/offset pair, but we still need to set
      // the IsTargetThumbFunc bit, so look the value up in the globla symbol table.
      auto EntryItr = GlobalSymbolTable.find(TargetName);
      if (EntryItr != GlobalSymbolTable.end()) {
        TargetIsLocalThumbFunc =
          EntryItr->second.getFlags().getTargetFlags() &
          ARMJITSymbolFlags::Thumb;
      }
    }

    if (Obj.isRelocationScattered(RelInfo)) {
      if (RelType == MachO::ARM_RELOC_HALF_SECTDIFF)
        return processHALFSECTDIFFRelocation(SectionID, RelI, Obj,
                                             ObjSectionToID);
      else if (RelType == MachO::GENERIC_RELOC_VANILLA)
        return processScatteredVANILLA(SectionID, RelI, Obj, ObjSectionToID,
                                       TargetIsLocalThumbFunc);
      else
        return ++RelI;
    }

    // Sanity check relocation type.
    switch (RelType) {
    UNIMPLEMENTED_RELOC(MachO::ARM_RELOC_PAIR);
    UNIMPLEMENTED_RELOC(MachO::ARM_RELOC_SECTDIFF);
    UNIMPLEMENTED_RELOC(MachO::ARM_RELOC_LOCAL_SECTDIFF);
    UNIMPLEMENTED_RELOC(MachO::ARM_RELOC_PB_LA_PTR);
    UNIMPLEMENTED_RELOC(MachO::ARM_THUMB_32BIT_BRANCH);
    UNIMPLEMENTED_RELOC(MachO::ARM_RELOC_HALF);
    default:
      if (RelType > MachO::ARM_RELOC_HALF_SECTDIFF)
        return make_error<RuntimeDyldError>(("MachO ARM relocation type " +
                                             Twine(RelType) +
                                             " is out of range").str());
      break;
    }

    RelocationEntry RE(getRelocationEntry(SectionID, Obj, RelI));
    if (auto AddendOrErr = decodeAddend(RE))
      RE.Addend = *AddendOrErr;
    else
      return AddendOrErr.takeError();
    RE.IsTargetThumbFunc = TargetIsLocalThumbFunc;

    RelocationValueRef Value;
    if (auto ValueOrErr = getRelocationValueRef(Obj, RelI, RE, ObjSectionToID))
      Value = *ValueOrErr;
    else
      return ValueOrErr.takeError();

    // If this is a branch from a thumb function (BR22) then make sure we mark
    // the value as being a thumb stub: we don't want to mix it up with an ARM
    // stub targeting the same function.
    if (RE.RelType == MachO::ARM_THUMB_RELOC_BR22)
      Value.IsStubThumb = true;

    if (RE.IsPCRel)
      makeValueAddendPCRel(Value, RelI,
                           (RE.RelType == MachO::ARM_THUMB_RELOC_BR22) ? 4 : 8);

    // If this is a non-external branch target check whether Value points to a
    // thumb func.
    if (!Value.SymbolName && (RelType == MachO::ARM_RELOC_BR24 ||
                              RelType == MachO::ARM_THUMB_RELOC_BR22))
      RE.IsTargetThumbFunc = isAddrTargetThumb(Value.SectionID, Value.Offset);

    if (RE.RelType == MachO::ARM_RELOC_BR24 ||
        RE.RelType == MachO::ARM_THUMB_RELOC_BR22)
      processBranchRelocation(RE, Value, Stubs);
    else {
      RE.Addend = Value.Offset;
      if (Value.SymbolName)
        addRelocationForSymbol(RE, Value.SymbolName);
      else
        addRelocationForSection(RE, Value.SectionID);
    }

    return ++RelI;
  }

  void resolveRelocation(const RelocationEntry &RE, uint64_t Value) override {
    LLVM_DEBUG(dumpRelocationToResolve(RE, Value));
    const SectionEntry &Section = Sections[RE.SectionID];
    uint8_t *LocalAddress = Section.getAddressWithOffset(RE.Offset);

    // If the relocation is PC-relative, the value to be encoded is the
    // pointer difference.
    if (RE.IsPCRel) {
      uint64_t FinalAddress = Section.getLoadAddressWithOffset(RE.Offset);
      Value -= FinalAddress;
      // ARM PCRel relocations have an effective-PC offset of two instructions
      // (four bytes in Thumb mode, 8 bytes in ARM mode).
      Value -= (RE.RelType == MachO::ARM_THUMB_RELOC_BR22) ? 4 : 8;
    }

    switch (RE.RelType) {
    case MachO::ARM_THUMB_RELOC_BR22: {
      Value += RE.Addend;
      uint16_t HighInsn = readBytesUnaligned(LocalAddress, 2);
      assert((HighInsn & 0xf800) == 0xf000 &&
             "Unrecognized thumb branch encoding (BR22 high bits)");
      HighInsn = (HighInsn & 0xf800) | ((Value >> 12) & 0x7ff);

      uint16_t LowInsn = readBytesUnaligned(LocalAddress + 2, 2);
      assert((LowInsn & 0xf800) != 0xf8000 &&
             "Unrecognized thumb branch encoding (BR22 low bits)");
      LowInsn = (LowInsn & 0xf800) | ((Value >> 1) & 0x7ff);

      writeBytesUnaligned(HighInsn, LocalAddress, 2);
      writeBytesUnaligned(LowInsn, LocalAddress + 2, 2);
      break;
    }

    case MachO::ARM_RELOC_VANILLA:
      if (RE.IsTargetThumbFunc)
        Value |= 0x01;
      writeBytesUnaligned(Value + RE.Addend, LocalAddress, 1 << RE.Size);
      break;
    case MachO::ARM_RELOC_BR24: {
      // Mask the value into the target address. We know instructions are
      // 32-bit aligned, so we can do it all at once.
      Value += RE.Addend;
      // The low two bits of the value are not encoded.
      Value >>= 2;
      // Mask the value to 24 bits.
      uint64_t FinalValue = Value & 0xffffff;
      // FIXME: If the destination is a Thumb function (and the instruction
      // is a non-predicated BL instruction), we need to change it to a BLX
      // instruction instead.

      // Insert the value into the instruction.
      uint32_t Temp = readBytesUnaligned(LocalAddress, 4);
      writeBytesUnaligned((Temp & ~0xffffff) | FinalValue, LocalAddress, 4);

      break;
    }
    case MachO::ARM_RELOC_HALF_SECTDIFF: {
      uint64_t SectionABase = Sections[RE.Sections.SectionA].getLoadAddress();
      uint64_t SectionBBase = Sections[RE.Sections.SectionB].getLoadAddress();
      assert((Value == SectionABase || Value == SectionBBase) &&
             "Unexpected HALFSECTDIFF relocation value.");
      Value = SectionABase - SectionBBase + RE.Addend;
      if (RE.Size & 0x1) // :upper16:
        Value = (Value >> 16);

      bool IsThumb = RE.Size & 0x2;

      Value &= 0xffff;

      uint32_t Insn = readBytesUnaligned(LocalAddress, 4);

      if (IsThumb)
        Insn = (Insn & 0x8f00fbf0) | ((Value & 0xf000) >> 12) |
               ((Value & 0x0800) >> 1) | ((Value & 0x0700) << 20) |
               ((Value & 0x00ff) << 16);
      else
        Insn = (Insn & 0xfff0f000) | ((Value & 0xf000) << 4) | (Value & 0x0fff);
      writeBytesUnaligned(Insn, LocalAddress, 4);
      break;
    }

    default:
      llvm_unreachable("Invalid relocation type");
    }
  }

  Error finalizeSection(const ObjectFile &Obj, unsigned SectionID,
                       const SectionRef &Section) {
    StringRef Name;
    Section.getName(Name);

    if (Name == "__nl_symbol_ptr")
      return populateIndirectSymbolPointersSection(cast<MachOObjectFile>(Obj),
                                                   Section, SectionID);
    return Error::success();
  }

private:

  void processBranchRelocation(const RelocationEntry &RE,
                               const RelocationValueRef &Value,
                               StubMap &Stubs) {
    // This is an ARM branch relocation, need to use a stub function.
    // Look up for existing stub.
    SectionEntry &Section = Sections[RE.SectionID];
    RuntimeDyldMachO::StubMap::const_iterator i = Stubs.find(Value);
    uint8_t *Addr;
    if (i != Stubs.end()) {
      Addr = Section.getAddressWithOffset(i->second);
    } else {
      // Create a new stub function.
      assert(Section.getStubOffset() % 4 == 0 && "Misaligned stub");
      Stubs[Value] = Section.getStubOffset();
      uint32_t StubOpcode = 0;
      if (RE.RelType == MachO::ARM_RELOC_BR24)
        StubOpcode = 0xe51ff004; // ldr pc, [pc, #-4]
      else if (RE.RelType == MachO::ARM_THUMB_RELOC_BR22)
        StubOpcode = 0xf000f8df; // ldr pc, [pc]
      else
        llvm_unreachable("Unrecognized relocation");
      Addr = Section.getAddressWithOffset(Section.getStubOffset());
      writeBytesUnaligned(StubOpcode, Addr, 4);
      uint8_t *StubTargetAddr = Addr + 4;
      RelocationEntry StubRE(
          RE.SectionID, StubTargetAddr - Section.getAddress(),
          MachO::GENERIC_RELOC_VANILLA, Value.Offset, false, 2);
      StubRE.IsTargetThumbFunc = RE.IsTargetThumbFunc;
      if (Value.SymbolName)
        addRelocationForSymbol(StubRE, Value.SymbolName);
      else
        addRelocationForSection(StubRE, Value.SectionID);
      Section.advanceStubOffset(getMaxStubSize());
    }
    RelocationEntry TargetRE(RE.SectionID, RE.Offset, RE.RelType, 0,
                             RE.IsPCRel, RE.Size);
    resolveRelocation(TargetRE, (uint64_t)Addr);
  }

  Expected<relocation_iterator>
  processHALFSECTDIFFRelocation(unsigned SectionID, relocation_iterator RelI,
                                const ObjectFile &BaseTObj,
                                ObjSectionToIDMap &ObjSectionToID) {
    const MachOObjectFile &MachO =
        static_cast<const MachOObjectFile&>(BaseTObj);
    MachO::any_relocation_info RE =
        MachO.getRelocation(RelI->getRawDataRefImpl());

    // For a half-diff relocation the length bits actually record whether this
    // is a movw/movt, and whether this is arm or thumb.
    // Bit 0 indicates movw (b0 == 0) or movt (b0 == 1).
    // Bit 1 indicates arm (b1 == 0) or thumb (b1 == 1).
    unsigned HalfDiffKindBits = MachO.getAnyRelocationLength(RE);
    bool IsThumb = HalfDiffKindBits & 0x2;

    SectionEntry &Section = Sections[SectionID];
    uint32_t RelocType = MachO.getAnyRelocationType(RE);
    bool IsPCRel = MachO.getAnyRelocationPCRel(RE);
    uint64_t Offset = RelI->getOffset();
    uint8_t *LocalAddress = Section.getAddressWithOffset(Offset);
    int64_t Immediate = readBytesUnaligned(LocalAddress, 4); // Copy the whole instruction out.

    if (IsThumb)
      Immediate = ((Immediate & 0x0000000f) << 12) |
                  ((Immediate & 0x00000400) << 1) |
                  ((Immediate & 0x70000000) >> 20) |
                  ((Immediate & 0x00ff0000) >> 16);
    else
      Immediate = ((Immediate >> 4) & 0xf000) | (Immediate & 0xfff);

    ++RelI;
    MachO::any_relocation_info RE2 =
      MachO.getRelocation(RelI->getRawDataRefImpl());
    uint32_t AddrA = MachO.getScatteredRelocationValue(RE);
    section_iterator SAI = getSectionByAddress(MachO, AddrA);
    assert(SAI != MachO.section_end() && "Can't find section for address A");
    uint64_t SectionABase = SAI->getAddress();
    uint64_t SectionAOffset = AddrA - SectionABase;
    SectionRef SectionA = *SAI;
    bool IsCode = SectionA.isText();
    uint32_t SectionAID = ~0U;
    if (auto SectionAIDOrErr =
          findOrEmitSection(MachO, SectionA, IsCode, ObjSectionToID))
      SectionAID = *SectionAIDOrErr;
    else
      return SectionAIDOrErr.takeError();

    uint32_t AddrB = MachO.getScatteredRelocationValue(RE2);
    section_iterator SBI = getSectionByAddress(MachO, AddrB);
    assert(SBI != MachO.section_end() && "Can't find section for address B");
    uint64_t SectionBBase = SBI->getAddress();
    uint64_t SectionBOffset = AddrB - SectionBBase;
    SectionRef SectionB = *SBI;
    uint32_t SectionBID = ~0U;
    if (auto SectionBIDOrErr =
          findOrEmitSection(MachO, SectionB, IsCode, ObjSectionToID))
      SectionBID = *SectionBIDOrErr;
    else
      return SectionBIDOrErr.takeError();

    uint32_t OtherHalf = MachO.getAnyRelocationAddress(RE2) & 0xffff;
    unsigned Shift = (HalfDiffKindBits & 0x1) ? 16 : 0;
    uint32_t FullImmVal = (Immediate << Shift) | (OtherHalf << (16 - Shift));
    int64_t Addend = FullImmVal - (AddrA - AddrB);

    // addend = Encoded - Expected
    //        = Encoded - (AddrA - AddrB)

    LLVM_DEBUG(dbgs() << "Found SECTDIFF: AddrA: " << AddrA
                      << ", AddrB: " << AddrB << ", Addend: " << Addend
                      << ", SectionA ID: " << SectionAID << ", SectionAOffset: "
                      << SectionAOffset << ", SectionB ID: " << SectionBID
                      << ", SectionBOffset: " << SectionBOffset << "\n");
    RelocationEntry R(SectionID, Offset, RelocType, Addend, SectionAID,
                      SectionAOffset, SectionBID, SectionBOffset, IsPCRel,
                      HalfDiffKindBits);

    addRelocationForSection(R, SectionAID);

    return ++RelI;
  }

};
}

#undef DEBUG_TYPE

#endif
