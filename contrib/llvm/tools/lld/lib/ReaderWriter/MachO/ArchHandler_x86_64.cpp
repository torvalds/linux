//===- lib/FileFormat/MachO/ArchHandler_x86_64.cpp ------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ArchHandler.h"
#include "Atoms.h"
#include "MachONormalizedFileBinaryUtils.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm::MachO;
using namespace lld::mach_o::normalized;

namespace lld {
namespace mach_o {

using llvm::support::ulittle32_t;
using llvm::support::ulittle64_t;

using llvm::support::little32_t;
using llvm::support::little64_t;

class ArchHandler_x86_64 : public ArchHandler {
public:
  ArchHandler_x86_64() = default;
  ~ArchHandler_x86_64() override = default;

  const Registry::KindStrings *kindStrings() override { return _sKindStrings; }

  Reference::KindArch kindArch() override {
    return Reference::KindArch::x86_64;
  }

  /// Used by GOTPass to locate GOT References
  bool isGOTAccess(const Reference &ref, bool &canBypassGOT) override {
    if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
      return false;
    assert(ref.kindArch() == Reference::KindArch::x86_64);
    switch (ref.kindValue()) {
    case ripRel32GotLoad:
      canBypassGOT = true;
      return true;
    case ripRel32Got:
      canBypassGOT = false;
      return true;
    case imageOffsetGot:
      canBypassGOT = false;
      return true;
    default:
      return false;
    }
  }

  bool isTLVAccess(const Reference &ref) const override {
    assert(ref.kindNamespace() == Reference::KindNamespace::mach_o);
    assert(ref.kindArch() == Reference::KindArch::x86_64);
    return ref.kindValue() == ripRel32Tlv;
  }

  void updateReferenceToTLV(const Reference *ref) override {
    assert(ref->kindNamespace() == Reference::KindNamespace::mach_o);
    assert(ref->kindArch() == Reference::KindArch::x86_64);
    assert(ref->kindValue() == ripRel32Tlv);
    const_cast<Reference*>(ref)->setKindValue(ripRel32);
  }

  /// Used by GOTPass to update GOT References
  void updateReferenceToGOT(const Reference *ref, bool targetNowGOT) override {
    assert(ref->kindNamespace() == Reference::KindNamespace::mach_o);
    assert(ref->kindArch() == Reference::KindArch::x86_64);

    switch (ref->kindValue()) {
    case ripRel32Got:
      assert(targetNowGOT && "target must be GOT");
      LLVM_FALLTHROUGH;
    case ripRel32GotLoad:
      const_cast<Reference *>(ref)
        ->setKindValue(targetNowGOT ? ripRel32 : ripRel32GotLoadNowLea);
      break;
    case imageOffsetGot:
      const_cast<Reference *>(ref)->setKindValue(imageOffset);
      break;
    default:
      llvm_unreachable("unknown GOT reference kind");
    }
  }

  bool needsCompactUnwind() override {
    return true;
  }

  Reference::KindValue imageOffsetKind() override {
    return imageOffset;
  }

  Reference::KindValue imageOffsetKindIndirect() override {
    return imageOffsetGot;
  }

  Reference::KindValue unwindRefToPersonalityFunctionKind() override {
    return ripRel32Got;
  }

  Reference::KindValue unwindRefToCIEKind() override {
    return negDelta32;
  }

  Reference::KindValue unwindRefToFunctionKind() override{
    return unwindFDEToFunction;
  }

  Reference::KindValue lazyImmediateLocationKind() override {
    return lazyImmediateLocation;
  }

  Reference::KindValue unwindRefToEhFrameKind() override {
    return unwindInfoToEhFrame;
  }

  Reference::KindValue pointerKind() override {
    return pointer64;
  }

  uint32_t dwarfCompactUnwindType() override {
    return 0x04000000U;
  }

  const StubInfo &stubInfo() override { return _sStubInfo; }

  bool isNonCallBranch(const Reference &) override {
    return false;
  }

  bool isCallSite(const Reference &) override;
  bool isPointer(const Reference &) override;
  bool isPairedReloc(const normalized::Relocation &) override;

  llvm::Error getReferenceInfo(const normalized::Relocation &reloc,
                               const DefinedAtom *inAtom,
                               uint32_t offsetInAtom,
                               uint64_t fixupAddress, bool swap,
                               FindAtomBySectionAndAddress atomFromAddress,
                               FindAtomBySymbolIndex atomFromSymbolIndex,
                               Reference::KindValue *kind,
                               const lld::Atom **target,
                               Reference::Addend *addend) override;
  llvm::Error
      getPairReferenceInfo(const normalized::Relocation &reloc1,
                           const normalized::Relocation &reloc2,
                           const DefinedAtom *inAtom,
                           uint32_t offsetInAtom,
                           uint64_t fixupAddress, bool swap, bool scatterable,
                           FindAtomBySectionAndAddress atomFromAddress,
                           FindAtomBySymbolIndex atomFromSymbolIndex,
                           Reference::KindValue *kind,
                           const lld::Atom **target,
                           Reference::Addend *addend) override;

  bool needsLocalSymbolInRelocatableFile(const DefinedAtom *atom) override {
    return (atom->contentType() == DefinedAtom::typeCString);
  }

  void generateAtomContent(const DefinedAtom &atom, bool relocatable,
                           FindAddressForAtom findAddress,
                           FindAddressForAtom findSectionAddress,
                           uint64_t imageBase,
                    llvm::MutableArrayRef<uint8_t> atomContentBuffer) override;

  void appendSectionRelocations(const DefinedAtom &atom,
                                uint64_t atomSectionOffset,
                                const Reference &ref,
                                FindSymbolIndexForAtom symbolIndexForAtom,
                                FindSectionIndexForAtom sectionIndexForAtom,
                                FindAddressForAtom addressForAtom,
                                normalized::Relocations &relocs) override;

private:
  static const Registry::KindStrings _sKindStrings[];
  static const StubInfo              _sStubInfo;

  enum X86_64Kind: Reference::KindValue {
    invalid,               /// for error condition

    // Kinds found in mach-o .o files:
    branch32,              /// ex: call _foo
    ripRel32,              /// ex: movq _foo(%rip), %rax
    ripRel32Minus1,        /// ex: movb $0x12, _foo(%rip)
    ripRel32Minus2,        /// ex: movw $0x1234, _foo(%rip)
    ripRel32Minus4,        /// ex: movl $0x12345678, _foo(%rip)
    ripRel32Anon,          /// ex: movq L1(%rip), %rax
    ripRel32Minus1Anon,    /// ex: movb $0x12, L1(%rip)
    ripRel32Minus2Anon,    /// ex: movw $0x1234, L1(%rip)
    ripRel32Minus4Anon,    /// ex: movw $0x12345678, L1(%rip)
    ripRel32GotLoad,       /// ex: movq  _foo@GOTPCREL(%rip), %rax
    ripRel32Got,           /// ex: pushq _foo@GOTPCREL(%rip)
    ripRel32Tlv,           /// ex: movq  _foo@TLVP(%rip), %rdi
    pointer64,             /// ex: .quad _foo
    pointer64Anon,         /// ex: .quad L1
    delta64,               /// ex: .quad _foo - .
    delta32,               /// ex: .long _foo - .
    delta64Anon,           /// ex: .quad L1 - .
    delta32Anon,           /// ex: .long L1 - .
    negDelta64,            /// ex: .quad . - _foo
    negDelta32,            /// ex: .long . - _foo

    // Kinds introduced by Passes:
    ripRel32GotLoadNowLea, /// Target of GOT load is in linkage unit so
                           ///  "movq  _foo@GOTPCREL(%rip), %rax" can be changed
                           /// to "leaq _foo(%rip), %rax
    lazyPointer,           /// Location contains a lazy pointer.
    lazyImmediateLocation, /// Location contains immediate value used in stub.

    imageOffset,           /// Location contains offset of atom in final image
    imageOffsetGot,        /// Location contains offset of GOT entry for atom in
                           /// final image (typically personality function).
    unwindFDEToFunction,   /// Nearly delta64, but cannot be rematerialized in
                           /// relocatable object (yay for implicit contracts!).
    unwindInfoToEhFrame,   /// Fix low 24 bits of compact unwind encoding to
                           /// refer to __eh_frame entry.
    tlvInitSectionOffset   /// Location contains offset tlv init-value atom
                           /// within the __thread_data section.
  };

  Reference::KindValue kindFromReloc(const normalized::Relocation &reloc);

  void applyFixupFinal(const Reference &ref, uint8_t *location,
                       uint64_t fixupAddress, uint64_t targetAddress,
                       uint64_t inAtomAddress, uint64_t imageBaseAddress,
                       FindAddressForAtom findSectionAddress);

  void applyFixupRelocatable(const Reference &ref, uint8_t *location,
                             uint64_t fixupAddress,
                             uint64_t targetAddress,
                             uint64_t inAtomAddress);
};

const Registry::KindStrings ArchHandler_x86_64::_sKindStrings[] = {
  LLD_KIND_STRING_ENTRY(invalid), LLD_KIND_STRING_ENTRY(branch32),
  LLD_KIND_STRING_ENTRY(ripRel32), LLD_KIND_STRING_ENTRY(ripRel32Minus1),
  LLD_KIND_STRING_ENTRY(ripRel32Minus2), LLD_KIND_STRING_ENTRY(ripRel32Minus4),
  LLD_KIND_STRING_ENTRY(ripRel32Anon),
  LLD_KIND_STRING_ENTRY(ripRel32Minus1Anon),
  LLD_KIND_STRING_ENTRY(ripRel32Minus2Anon),
  LLD_KIND_STRING_ENTRY(ripRel32Minus4Anon),
  LLD_KIND_STRING_ENTRY(ripRel32GotLoad),
  LLD_KIND_STRING_ENTRY(ripRel32GotLoadNowLea),
  LLD_KIND_STRING_ENTRY(ripRel32Got), LLD_KIND_STRING_ENTRY(ripRel32Tlv),
  LLD_KIND_STRING_ENTRY(lazyPointer),
  LLD_KIND_STRING_ENTRY(lazyImmediateLocation),
  LLD_KIND_STRING_ENTRY(pointer64), LLD_KIND_STRING_ENTRY(pointer64Anon),
  LLD_KIND_STRING_ENTRY(delta32), LLD_KIND_STRING_ENTRY(delta64),
  LLD_KIND_STRING_ENTRY(delta32Anon), LLD_KIND_STRING_ENTRY(delta64Anon),
  LLD_KIND_STRING_ENTRY(negDelta64),
  LLD_KIND_STRING_ENTRY(negDelta32),
  LLD_KIND_STRING_ENTRY(imageOffset), LLD_KIND_STRING_ENTRY(imageOffsetGot),
  LLD_KIND_STRING_ENTRY(unwindFDEToFunction),
  LLD_KIND_STRING_ENTRY(unwindInfoToEhFrame),
  LLD_KIND_STRING_ENTRY(tlvInitSectionOffset),
  LLD_KIND_STRING_END
};

const ArchHandler::StubInfo ArchHandler_x86_64::_sStubInfo = {
  "dyld_stub_binder",

  // Lazy pointer references
  { Reference::KindArch::x86_64, pointer64, 0, 0 },
  { Reference::KindArch::x86_64, lazyPointer, 0, 0 },

  // GOT pointer to dyld_stub_binder
  { Reference::KindArch::x86_64, pointer64, 0, 0 },

  // x86_64 code alignment 2^1
  1,

  // Stub size and code
  6,
  { 0xff, 0x25, 0x00, 0x00, 0x00, 0x00 },       // jmp *lazyPointer
  { Reference::KindArch::x86_64, ripRel32, 2, 0 },
  { false, 0, 0, 0 },

  // Stub Helper size and code
  10,
  { 0x68, 0x00, 0x00, 0x00, 0x00,               // pushq $lazy-info-offset
    0xE9, 0x00, 0x00, 0x00, 0x00 },             // jmp helperhelper
  { Reference::KindArch::x86_64, lazyImmediateLocation, 1, 0 },
  { Reference::KindArch::x86_64, branch32, 6, 0 },

  // Stub helper image cache content type
  DefinedAtom::typeNonLazyPointer,

  // Stub Helper-Common size and code
  16,
  // Stub helper alignment
  2,
  { 0x4C, 0x8D, 0x1D, 0x00, 0x00, 0x00, 0x00,   // leaq cache(%rip),%r11
    0x41, 0x53,                                 // push %r11
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,         // jmp *binder(%rip)
    0x90 },                                     // nop
  { Reference::KindArch::x86_64, ripRel32, 3, 0 },
  { false, 0, 0, 0 },
  { Reference::KindArch::x86_64, ripRel32, 11, 0 },
  { false, 0, 0, 0 }

};

bool ArchHandler_x86_64::isCallSite(const Reference &ref) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return false;
  assert(ref.kindArch() == Reference::KindArch::x86_64);
  return (ref.kindValue() == branch32);
}

bool ArchHandler_x86_64::isPointer(const Reference &ref) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return false;
  assert(ref.kindArch() == Reference::KindArch::x86_64);
  Reference::KindValue kind = ref.kindValue();
  return (kind == pointer64 || kind == pointer64Anon);
}

bool ArchHandler_x86_64::isPairedReloc(const Relocation &reloc) {
  return (reloc.type == X86_64_RELOC_SUBTRACTOR);
}

Reference::KindValue
ArchHandler_x86_64::kindFromReloc(const Relocation &reloc) {
  switch(relocPattern(reloc)) {
  case X86_64_RELOC_BRANCH   | rPcRel | rExtern | rLength4:
    return branch32;
  case X86_64_RELOC_SIGNED   | rPcRel | rExtern | rLength4:
    return ripRel32;
  case X86_64_RELOC_SIGNED   | rPcRel |           rLength4:
    return ripRel32Anon;
  case X86_64_RELOC_SIGNED_1 | rPcRel | rExtern | rLength4:
    return ripRel32Minus1;
  case X86_64_RELOC_SIGNED_1 | rPcRel |           rLength4:
    return ripRel32Minus1Anon;
  case X86_64_RELOC_SIGNED_2 | rPcRel | rExtern | rLength4:
    return ripRel32Minus2;
  case X86_64_RELOC_SIGNED_2 | rPcRel |           rLength4:
    return ripRel32Minus2Anon;
  case X86_64_RELOC_SIGNED_4 | rPcRel | rExtern | rLength4:
    return ripRel32Minus4;
  case X86_64_RELOC_SIGNED_4 | rPcRel |           rLength4:
    return ripRel32Minus4Anon;
  case X86_64_RELOC_GOT_LOAD | rPcRel | rExtern | rLength4:
    return ripRel32GotLoad;
  case X86_64_RELOC_GOT      | rPcRel | rExtern | rLength4:
    return ripRel32Got;
  case X86_64_RELOC_TLV      | rPcRel | rExtern | rLength4:
    return ripRel32Tlv;
  case X86_64_RELOC_UNSIGNED          | rExtern | rLength8:
    return pointer64;
  case X86_64_RELOC_UNSIGNED                    | rLength8:
    return pointer64Anon;
  default:
    return invalid;
  }
}

llvm::Error
ArchHandler_x86_64::getReferenceInfo(const Relocation &reloc,
                                    const DefinedAtom *inAtom,
                                    uint32_t offsetInAtom,
                                    uint64_t fixupAddress, bool swap,
                                    FindAtomBySectionAndAddress atomFromAddress,
                                    FindAtomBySymbolIndex atomFromSymbolIndex,
                                    Reference::KindValue *kind,
                                    const lld::Atom **target,
                                    Reference::Addend *addend) {
  *kind = kindFromReloc(reloc);
  if (*kind == invalid)
    return llvm::make_error<GenericError>("unknown type");
  const uint8_t *fixupContent = &inAtom->rawContent()[offsetInAtom];
  uint64_t targetAddress;
  switch (*kind) {
  case branch32:
  case ripRel32:
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = *(const little32_t *)fixupContent;
    return llvm::Error::success();
  case ripRel32Minus1:
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = (int32_t)*(const little32_t *)fixupContent + 1;
    return llvm::Error::success();
  case ripRel32Minus2:
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = (int32_t)*(const little32_t *)fixupContent + 2;
    return llvm::Error::success();
  case ripRel32Minus4:
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = (int32_t)*(const little32_t *)fixupContent + 4;
    return llvm::Error::success();
  case ripRel32Anon:
    targetAddress = fixupAddress + 4 + *(const little32_t *)fixupContent;
    return atomFromAddress(reloc.symbol, targetAddress, target, addend);
  case ripRel32Minus1Anon:
    targetAddress = fixupAddress + 5 + *(const little32_t *)fixupContent;
    return atomFromAddress(reloc.symbol, targetAddress, target, addend);
  case ripRel32Minus2Anon:
    targetAddress = fixupAddress + 6 + *(const little32_t *)fixupContent;
    return atomFromAddress(reloc.symbol, targetAddress, target, addend);
  case ripRel32Minus4Anon:
    targetAddress = fixupAddress + 8 + *(const little32_t *)fixupContent;
    return atomFromAddress(reloc.symbol, targetAddress, target, addend);
  case ripRel32GotLoad:
  case ripRel32Got:
  case ripRel32Tlv:
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = *(const little32_t *)fixupContent;
    return llvm::Error::success();
  case tlvInitSectionOffset:
  case pointer64:
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    // If this is the 3rd pointer of a tlv-thunk (i.e. the pointer to the TLV's
    // initial value) we need to handle it specially.
    if (inAtom->contentType() == DefinedAtom::typeThunkTLV &&
        offsetInAtom == 16) {
      *kind = tlvInitSectionOffset;
      assert(*addend == 0 && "TLV-init has non-zero addend?");
    } else
      *addend = *(const little64_t *)fixupContent;
    return llvm::Error::success();
  case pointer64Anon:
    targetAddress = *(const little64_t *)fixupContent;
    return atomFromAddress(reloc.symbol, targetAddress, target, addend);
  default:
    llvm_unreachable("bad reloc kind");
  }
}

llvm::Error
ArchHandler_x86_64::getPairReferenceInfo(const normalized::Relocation &reloc1,
                                   const normalized::Relocation &reloc2,
                                   const DefinedAtom *inAtom,
                                   uint32_t offsetInAtom,
                                   uint64_t fixupAddress, bool swap,
                                   bool scatterable,
                                   FindAtomBySectionAndAddress atomFromAddress,
                                   FindAtomBySymbolIndex atomFromSymbolIndex,
                                   Reference::KindValue *kind,
                                   const lld::Atom **target,
                                   Reference::Addend *addend) {
  const uint8_t *fixupContent = &inAtom->rawContent()[offsetInAtom];
  uint64_t targetAddress;
  const lld::Atom *fromTarget;
  if (auto ec = atomFromSymbolIndex(reloc1.symbol, &fromTarget))
    return ec;

  switch(relocPattern(reloc1) << 16 | relocPattern(reloc2)) {
  case ((X86_64_RELOC_SUBTRACTOR | rExtern | rLength8) << 16 |
        X86_64_RELOC_UNSIGNED    | rExtern | rLength8): {
    if (auto ec = atomFromSymbolIndex(reloc2.symbol, target))
      return ec;
    uint64_t encodedAddend = (int64_t)*(const little64_t *)fixupContent;
    if (inAtom == fromTarget) {
      if (inAtom->contentType() == DefinedAtom::typeCFI)
        *kind = unwindFDEToFunction;
      else
        *kind = delta64;
      *addend = encodedAddend + offsetInAtom;
    } else if (inAtom == *target) {
      *kind = negDelta64;
      *addend = encodedAddend - offsetInAtom;
      *target = fromTarget;
    } else
      return llvm::make_error<GenericError>("Invalid pointer diff");
    return llvm::Error::success();
  }
  case ((X86_64_RELOC_SUBTRACTOR | rExtern | rLength4) << 16 |
        X86_64_RELOC_UNSIGNED    | rExtern | rLength4): {
    if (auto ec = atomFromSymbolIndex(reloc2.symbol, target))
      return ec;
    uint32_t encodedAddend = (int32_t)*(const little32_t *)fixupContent;
    if (inAtom == fromTarget) {
      *kind = delta32;
      *addend = encodedAddend + offsetInAtom;
    } else if (inAtom == *target) {
      *kind = negDelta32;
      *addend = encodedAddend - offsetInAtom;
      *target = fromTarget;
    } else
      return llvm::make_error<GenericError>("Invalid pointer diff");
    return llvm::Error::success();
  }
  case ((X86_64_RELOC_SUBTRACTOR | rExtern | rLength8) << 16 |
        X86_64_RELOC_UNSIGNED              | rLength8):
    if (fromTarget != inAtom)
      return llvm::make_error<GenericError>("pointer diff not in base atom");
    *kind = delta64Anon;
    targetAddress = offsetInAtom + (int64_t)*(const little64_t *)fixupContent;
    return atomFromAddress(reloc2.symbol, targetAddress, target, addend);
  case ((X86_64_RELOC_SUBTRACTOR | rExtern | rLength4) << 16 |
        X86_64_RELOC_UNSIGNED              | rLength4):
    if (fromTarget != inAtom)
      return llvm::make_error<GenericError>("pointer diff not in base atom");
    *kind = delta32Anon;
    targetAddress = offsetInAtom + (int32_t)*(const little32_t *)fixupContent;
    return atomFromAddress(reloc2.symbol, targetAddress, target, addend);
  default:
    return llvm::make_error<GenericError>("unknown pair");
  }
}

void ArchHandler_x86_64::generateAtomContent(
    const DefinedAtom &atom, bool relocatable, FindAddressForAtom findAddress,
    FindAddressForAtom findSectionAddress, uint64_t imageBaseAddress,
    llvm::MutableArrayRef<uint8_t> atomContentBuffer) {
  // Copy raw bytes.
  std::copy(atom.rawContent().begin(), atom.rawContent().end(),
            atomContentBuffer.begin());
  // Apply fix-ups.
  for (const Reference *ref : atom) {
    uint32_t offset = ref->offsetInAtom();
    const Atom *target = ref->target();
    uint64_t targetAddress = 0;
    if (isa<DefinedAtom>(target))
      targetAddress = findAddress(*target);
    uint64_t atomAddress = findAddress(atom);
    uint64_t fixupAddress = atomAddress + offset;
    if (relocatable) {
      applyFixupRelocatable(*ref, &atomContentBuffer[offset],
                                        fixupAddress, targetAddress,
                                        atomAddress);
    } else {
      applyFixupFinal(*ref, &atomContentBuffer[offset],
                      fixupAddress, targetAddress,
                      atomAddress, imageBaseAddress, findSectionAddress);
    }
  }
}

void ArchHandler_x86_64::applyFixupFinal(
    const Reference &ref, uint8_t *loc, uint64_t fixupAddress,
    uint64_t targetAddress, uint64_t inAtomAddress, uint64_t imageBaseAddress,
    FindAddressForAtom findSectionAddress) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::x86_64);
  ulittle32_t *loc32 = reinterpret_cast<ulittle32_t *>(loc);
  ulittle64_t *loc64 = reinterpret_cast<ulittle64_t *>(loc);
  switch (static_cast<X86_64Kind>(ref.kindValue())) {
  case branch32:
  case ripRel32:
  case ripRel32Anon:
  case ripRel32Got:
  case ripRel32GotLoad:
  case ripRel32Tlv:
    *loc32 = targetAddress - (fixupAddress + 4) + ref.addend();
    return;
  case pointer64:
  case pointer64Anon:
    *loc64 = targetAddress + ref.addend();
    return;
  case tlvInitSectionOffset:
    *loc64 = targetAddress - findSectionAddress(*ref.target()) + ref.addend();
    return;
  case ripRel32Minus1:
  case ripRel32Minus1Anon:
    *loc32 = targetAddress - (fixupAddress + 5) + ref.addend();
    return;
  case ripRel32Minus2:
  case ripRel32Minus2Anon:
    *loc32 = targetAddress - (fixupAddress + 6) + ref.addend();
    return;
  case ripRel32Minus4:
  case ripRel32Minus4Anon:
    *loc32 = targetAddress - (fixupAddress + 8) + ref.addend();
    return;
  case delta32:
  case delta32Anon:
    *loc32 = targetAddress - fixupAddress + ref.addend();
    return;
  case delta64:
  case delta64Anon:
  case unwindFDEToFunction:
    *loc64 = targetAddress - fixupAddress + ref.addend();
    return;
  case ripRel32GotLoadNowLea:
    // Change MOVQ to LEA
    assert(loc[-2] == 0x8B);
    loc[-2] = 0x8D;
    *loc32 = targetAddress - (fixupAddress + 4) + ref.addend();
    return;
  case negDelta64:
    *loc64 = fixupAddress - targetAddress + ref.addend();
    return;
  case negDelta32:
    *loc32 = fixupAddress - targetAddress + ref.addend();
    return;
  case lazyPointer:
    // Do nothing
    return;
  case lazyImmediateLocation:
    *loc32 = ref.addend();
    return;
  case imageOffset:
  case imageOffsetGot:
    *loc32 = (targetAddress - imageBaseAddress) + ref.addend();
    return;
  case unwindInfoToEhFrame: {
    uint64_t val = targetAddress - findSectionAddress(*ref.target()) + ref.addend();
    assert(val < 0xffffffU && "offset in __eh_frame too large");
    *loc32 = (*loc32 & 0xff000000U) | val;
    return;
  }
  case invalid:
    // Fall into llvm_unreachable().
    break;
  }
  llvm_unreachable("invalid x86_64 Reference Kind");
}

void ArchHandler_x86_64::applyFixupRelocatable(const Reference &ref,
                                               uint8_t *loc,
                                               uint64_t fixupAddress,
                                               uint64_t targetAddress,
                                               uint64_t inAtomAddress)  {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::x86_64);
  ulittle32_t *loc32 = reinterpret_cast<ulittle32_t *>(loc);
  ulittle64_t *loc64 = reinterpret_cast<ulittle64_t *>(loc);
  switch (static_cast<X86_64Kind>(ref.kindValue())) {
  case branch32:
  case ripRel32:
  case ripRel32Got:
  case ripRel32GotLoad:
  case ripRel32Tlv:
    *loc32 = ref.addend();
    return;
  case ripRel32Anon:
    *loc32 = (targetAddress - (fixupAddress + 4)) + ref.addend();
    return;
  case tlvInitSectionOffset:
  case pointer64:
    *loc64 = ref.addend();
    return;
  case pointer64Anon:
    *loc64 = targetAddress + ref.addend();
    return;
  case ripRel32Minus1:
    *loc32 = ref.addend() - 1;
    return;
  case ripRel32Minus1Anon:
    *loc32 = (targetAddress - (fixupAddress + 5)) + ref.addend();
    return;
  case ripRel32Minus2:
    *loc32 = ref.addend() - 2;
    return;
  case ripRel32Minus2Anon:
    *loc32 = (targetAddress - (fixupAddress + 6)) + ref.addend();
    return;
  case ripRel32Minus4:
    *loc32 = ref.addend() - 4;
    return;
  case ripRel32Minus4Anon:
    *loc32 = (targetAddress - (fixupAddress + 8)) + ref.addend();
    return;
  case delta32:
    *loc32 = ref.addend() + inAtomAddress - fixupAddress;
    return;
  case delta32Anon:
    // The value we write here should be the delta to the target
    // after taking in to account the difference from the fixup back to the
    // last defined label
    // ie, if we have:
    // _base: ...
    // Lfixup: .quad Ltarget - .
    // ...
    // Ltarget:
    //
    // Then we want to encode the value (Ltarget + addend) - (LFixup - _base)
    *loc32 = (targetAddress + ref.addend()) - (fixupAddress - inAtomAddress);
    return;
  case delta64:
    *loc64 = ref.addend() + inAtomAddress - fixupAddress;
    return;
  case delta64Anon:
    // The value we write here should be the delta to the target
    // after taking in to account the difference from the fixup back to the
    // last defined label
    // ie, if we have:
    // _base: ...
    // Lfixup: .quad Ltarget - .
    // ...
    // Ltarget:
    //
    // Then we want to encode the value (Ltarget + addend) - (LFixup - _base)
    *loc64 = (targetAddress + ref.addend()) - (fixupAddress - inAtomAddress);
    return;
  case negDelta64:
    *loc64 = ref.addend() + fixupAddress - inAtomAddress;
    return;
  case negDelta32:
    *loc32 = ref.addend() + fixupAddress - inAtomAddress;
    return;
  case ripRel32GotLoadNowLea:
    llvm_unreachable("ripRel32GotLoadNowLea implies GOT pass was run");
    return;
  case lazyPointer:
  case lazyImmediateLocation:
    llvm_unreachable("lazy reference kind implies Stubs pass was run");
    return;
  case imageOffset:
  case imageOffsetGot:
  case unwindInfoToEhFrame:
    llvm_unreachable("fixup implies __unwind_info");
    return;
  case unwindFDEToFunction:
    // Do nothing for now
    return;
  case invalid:
    // Fall into llvm_unreachable().
    break;
  }
  llvm_unreachable("unknown x86_64 Reference Kind");
}

void ArchHandler_x86_64::appendSectionRelocations(
                                   const DefinedAtom &atom,
                                   uint64_t atomSectionOffset,
                                   const Reference &ref,
                                   FindSymbolIndexForAtom symbolIndexForAtom,
                                   FindSectionIndexForAtom sectionIndexForAtom,
                                   FindAddressForAtom addressForAtom,
                                   normalized::Relocations &relocs) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::x86_64);
  uint32_t sectionOffset = atomSectionOffset + ref.offsetInAtom();
  switch (static_cast<X86_64Kind>(ref.kindValue())) {
  case branch32:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_BRANCH | rPcRel | rExtern | rLength4);
    return;
  case ripRel32:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_SIGNED | rPcRel | rExtern | rLength4 );
    return;
  case ripRel32Anon:
    appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_SIGNED | rPcRel           | rLength4 );
    return;
  case ripRel32Got:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_GOT | rPcRel | rExtern | rLength4 );
    return;
  case ripRel32GotLoad:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_GOT_LOAD | rPcRel | rExtern | rLength4 );
    return;
  case ripRel32Tlv:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_TLV | rPcRel | rExtern | rLength4 );
    return;
  case tlvInitSectionOffset:
  case pointer64:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_UNSIGNED  | rExtern | rLength8);
    return;
  case pointer64Anon:
    appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_UNSIGNED | rLength8);
    return;
  case ripRel32Minus1:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_SIGNED_1 | rPcRel | rExtern | rLength4 );
    return;
  case ripRel32Minus1Anon:
    appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_SIGNED_1 | rPcRel           | rLength4 );
    return;
  case ripRel32Minus2:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_SIGNED_2 | rPcRel | rExtern | rLength4 );
    return;
  case ripRel32Minus2Anon:
    appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_SIGNED_2 | rPcRel           | rLength4 );
    return;
  case ripRel32Minus4:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_SIGNED_4 | rPcRel | rExtern | rLength4 );
    return;
  case ripRel32Minus4Anon:
    appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_SIGNED_4 | rPcRel           | rLength4 );
    return;
  case delta32:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(atom), 0,
                X86_64_RELOC_SUBTRACTOR | rExtern | rLength4 );
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_UNSIGNED   | rExtern | rLength4 );
    return;
  case delta32Anon:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(atom), 0,
                X86_64_RELOC_SUBTRACTOR | rExtern | rLength4 );
    appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_UNSIGNED             | rLength4 );
    return;
  case delta64:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(atom), 0,
                X86_64_RELOC_SUBTRACTOR | rExtern | rLength8 );
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_UNSIGNED   | rExtern | rLength8 );
    return;
  case delta64Anon:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(atom), 0,
                X86_64_RELOC_SUBTRACTOR | rExtern | rLength8 );
    appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_UNSIGNED             | rLength8 );
    return;
  case unwindFDEToFunction:
  case unwindInfoToEhFrame:
    return;
  case negDelta32:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_SUBTRACTOR | rExtern | rLength4 );
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(atom), 0,
                X86_64_RELOC_UNSIGNED   | rExtern | rLength4 );
    return;
  case negDelta64:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                X86_64_RELOC_SUBTRACTOR | rExtern | rLength8 );
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(atom), 0,
                X86_64_RELOC_UNSIGNED   | rExtern | rLength8 );
    return;
  case ripRel32GotLoadNowLea:
    llvm_unreachable("ripRel32GotLoadNowLea implies GOT pass was run");
    return;
  case lazyPointer:
  case lazyImmediateLocation:
    llvm_unreachable("lazy reference kind implies Stubs pass was run");
    return;
  case imageOffset:
  case imageOffsetGot:
    llvm_unreachable("__unwind_info references should have been resolved");
    return;
  case invalid:
    // Fall into llvm_unreachable().
    break;
  }
  llvm_unreachable("unknown x86_64 Reference Kind");
}

std::unique_ptr<mach_o::ArchHandler> ArchHandler::create_x86_64() {
  return std::unique_ptr<mach_o::ArchHandler>(new ArchHandler_x86_64());
}

} // namespace mach_o
} // namespace lld
