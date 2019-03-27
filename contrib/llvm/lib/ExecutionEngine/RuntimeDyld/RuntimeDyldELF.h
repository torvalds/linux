//===-- RuntimeDyldELF.h - Run-time dynamic linker for MC-JIT ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// ELF support for MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_RUNTIMEDYLDELF_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_RUNTIMEDYLDELF_H

#include "RuntimeDyldImpl.h"
#include "llvm/ADT/DenseMap.h"

using namespace llvm;

namespace llvm {
namespace object {
class ELFObjectFileBase;
}

class RuntimeDyldELF : public RuntimeDyldImpl {

  void resolveRelocation(const SectionEntry &Section, uint64_t Offset,
                         uint64_t Value, uint32_t Type, int64_t Addend,
                         uint64_t SymOffset = 0, SID SectionID = 0);

  void resolveX86_64Relocation(const SectionEntry &Section, uint64_t Offset,
                               uint64_t Value, uint32_t Type, int64_t Addend,
                               uint64_t SymOffset);

  void resolveX86Relocation(const SectionEntry &Section, uint64_t Offset,
                            uint32_t Value, uint32_t Type, int32_t Addend);

  void resolveAArch64Relocation(const SectionEntry &Section, uint64_t Offset,
                                uint64_t Value, uint32_t Type, int64_t Addend);

  bool resolveAArch64ShortBranch(unsigned SectionID, relocation_iterator RelI,
                                 const RelocationValueRef &Value);

  void resolveAArch64Branch(unsigned SectionID, const RelocationValueRef &Value,
                            relocation_iterator RelI, StubMap &Stubs);

  void resolveARMRelocation(const SectionEntry &Section, uint64_t Offset,
                            uint32_t Value, uint32_t Type, int32_t Addend);

  void resolvePPC32Relocation(const SectionEntry &Section, uint64_t Offset,
                              uint64_t Value, uint32_t Type, int64_t Addend);

  void resolvePPC64Relocation(const SectionEntry &Section, uint64_t Offset,
                              uint64_t Value, uint32_t Type, int64_t Addend);

  void resolveSystemZRelocation(const SectionEntry &Section, uint64_t Offset,
                                uint64_t Value, uint32_t Type, int64_t Addend);

  void resolveBPFRelocation(const SectionEntry &Section, uint64_t Offset,
                            uint64_t Value, uint32_t Type, int64_t Addend);

  unsigned getMaxStubSize() override {
    if (Arch == Triple::aarch64 || Arch == Triple::aarch64_be)
      return 20; // movz; movk; movk; movk; br
    if (Arch == Triple::arm || Arch == Triple::thumb)
      return 8; // 32-bit instruction and 32-bit address
    else if (IsMipsO32ABI || IsMipsN32ABI)
      return 16;
    else if (IsMipsN64ABI)
      return 32;
    else if (Arch == Triple::ppc64 || Arch == Triple::ppc64le)
      return 44;
    else if (Arch == Triple::x86_64)
      return 6; // 2-byte jmp instruction + 32-bit relative address
    else if (Arch == Triple::systemz)
      return 16;
    else
      return 0;
  }

  unsigned getStubAlignment() override {
    if (Arch == Triple::systemz)
      return 8;
    else
      return 1;
  }

  void setMipsABI(const ObjectFile &Obj) override;

  Error findPPC64TOCSection(const ELFObjectFileBase &Obj,
                            ObjSectionToIDMap &LocalSections,
                            RelocationValueRef &Rel);
  Error findOPDEntrySection(const ELFObjectFileBase &Obj,
                            ObjSectionToIDMap &LocalSections,
                            RelocationValueRef &Rel);
protected:
  size_t getGOTEntrySize() override;

private:
  SectionEntry &getSection(unsigned SectionID) { return Sections[SectionID]; }

  // Allocate no GOT entries for use in the given section.
  uint64_t allocateGOTEntries(unsigned no);

  // Find GOT entry corresponding to relocation or create new one.
  uint64_t findOrAllocGOTEntry(const RelocationValueRef &Value,
                               unsigned GOTRelType);

  // Resolve the relvative address of GOTOffset in Section ID and place
  // it at the given Offset
  void resolveGOTOffsetRelocation(unsigned SectionID, uint64_t Offset,
                                  uint64_t GOTOffset, uint32_t Type);

  // For a GOT entry referenced from SectionID, compute a relocation entry
  // that will place the final resolved value in the GOT slot
  RelocationEntry computeGOTOffsetRE(uint64_t GOTOffset, uint64_t SymbolOffset,
                                     unsigned Type);

  // Compute the address in memory where we can find the placeholder
  void *computePlaceholderAddress(unsigned SectionID, uint64_t Offset) const;

  // Split out common case for createing the RelocationEntry for when the relocation requires
  // no particular advanced processing.
  void processSimpleRelocation(unsigned SectionID, uint64_t Offset, unsigned RelType, RelocationValueRef Value);

  // Return matching *LO16 relocation (Mips specific)
  uint32_t getMatchingLoRelocation(uint32_t RelType,
                                   bool IsLocal = false) const;

  // The tentative ID for the GOT section
  unsigned GOTSectionID;

  // Records the current number of allocated slots in the GOT
  // (This would be equivalent to GOTEntries.size() were it not for relocations
  // that consume more than one slot)
  unsigned CurrentGOTIndex;

protected:
  // A map from section to a GOT section that has entries for section's GOT
  // relocations. (Mips64 specific)
  DenseMap<SID, SID> SectionToGOTMap;

private:
  // A map to avoid duplicate got entries (Mips64 specific)
  StringMap<uint64_t> GOTSymbolOffsets;

  // *HI16 relocations will be added for resolving when we find matching
  // *LO16 part. (Mips specific)
  SmallVector<std::pair<RelocationValueRef, RelocationEntry>, 8> PendingRelocs;

  // When a module is loaded we save the SectionID of the EH frame section
  // in a table until we receive a request to register all unregistered
  // EH frame sections with the memory manager.
  SmallVector<SID, 2> UnregisteredEHFrameSections;

  // Map between GOT relocation value and corresponding GOT offset
  std::map<RelocationValueRef, uint64_t> GOTOffsetMap;

  bool relocationNeedsGot(const RelocationRef &R) const override;
  bool relocationNeedsStub(const RelocationRef &R) const override;

public:
  RuntimeDyldELF(RuntimeDyld::MemoryManager &MemMgr,
                 JITSymbolResolver &Resolver);
  ~RuntimeDyldELF() override;

  static std::unique_ptr<RuntimeDyldELF>
  create(Triple::ArchType Arch, RuntimeDyld::MemoryManager &MemMgr,
         JITSymbolResolver &Resolver);

  std::unique_ptr<RuntimeDyld::LoadedObjectInfo>
  loadObject(const object::ObjectFile &O) override;

  void resolveRelocation(const RelocationEntry &RE, uint64_t Value) override;
  Expected<relocation_iterator>
  processRelocationRef(unsigned SectionID, relocation_iterator RelI,
                       const ObjectFile &Obj,
                       ObjSectionToIDMap &ObjSectionToID,
                       StubMap &Stubs) override;
  bool isCompatibleFile(const object::ObjectFile &Obj) const override;
  void registerEHFrames() override;
  Error finalizeLoad(const ObjectFile &Obj,
                     ObjSectionToIDMap &SectionMap) override;
};

} // end namespace llvm

#endif
