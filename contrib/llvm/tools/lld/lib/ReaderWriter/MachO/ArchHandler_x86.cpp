//===- lib/FileFormat/MachO/ArchHandler_x86.cpp ---------------------------===//
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

using llvm::support::ulittle16_t;
using llvm::support::ulittle32_t;

using llvm::support::little16_t;
using llvm::support::little32_t;

class ArchHandler_x86 : public ArchHandler {
public:
  ArchHandler_x86() = default;
  ~ArchHandler_x86() override = default;

  const Registry::KindStrings *kindStrings() override { return _sKindStrings; }

  Reference::KindArch kindArch() override { return Reference::KindArch::x86; }

  const StubInfo &stubInfo() override { return _sStubInfo; }
  bool isCallSite(const Reference &) override;
  bool isNonCallBranch(const Reference &) override {
    return false;
  }

  bool isPointer(const Reference &) override;
  bool isPairedReloc(const normalized::Relocation &) override;

  bool needsCompactUnwind() override {
    return false;
  }

  Reference::KindValue imageOffsetKind() override {
    return invalid;
  }

  Reference::KindValue imageOffsetKindIndirect() override {
    return invalid;
  }

  Reference::KindValue unwindRefToPersonalityFunctionKind() override {
    return invalid;
  }

  Reference::KindValue unwindRefToCIEKind() override {
    return negDelta32;
  }

  Reference::KindValue unwindRefToFunctionKind() override{
    return delta32;
  }

  Reference::KindValue lazyImmediateLocationKind() override {
    return lazyImmediateLocation;
  }

  Reference::KindValue unwindRefToEhFrameKind() override {
    return invalid;
  }

  Reference::KindValue pointerKind() override {
    return invalid;
  }

  uint32_t dwarfCompactUnwindType() override {
    return 0x04000000U;
  }

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

  void generateAtomContent(const DefinedAtom &atom, bool relocatable,
                           FindAddressForAtom findAddress,
                           FindAddressForAtom findSectionAddress,
                           uint64_t imageBaseAddress,
                    llvm::MutableArrayRef<uint8_t> atomContentBuffer) override;

  void appendSectionRelocations(const DefinedAtom &atom,
                                uint64_t atomSectionOffset,
                                const Reference &ref,
                                FindSymbolIndexForAtom symbolIndexForAtom,
                                FindSectionIndexForAtom sectionIndexForAtom,
                                FindAddressForAtom addressForAtom,
                                normalized::Relocations &relocs) override;

  bool isDataInCodeTransition(Reference::KindValue refKind) override {
    return refKind == modeCode || refKind == modeData;
  }

  Reference::KindValue dataInCodeTransitionStart(
                                        const MachODefinedAtom &atom) override {
    return modeData;
  }

  Reference::KindValue dataInCodeTransitionEnd(
                                        const MachODefinedAtom &atom) override {
    return modeCode;
  }

private:
  static const Registry::KindStrings _sKindStrings[];
  static const StubInfo              _sStubInfo;

  enum X86Kind : Reference::KindValue {
    invalid,               /// for error condition

    modeCode,              /// Content starting at this offset is code.
    modeData,              /// Content starting at this offset is data.

    // Kinds found in mach-o .o files:
    branch32,              /// ex: call _foo
    branch16,              /// ex: callw _foo
    abs32,                 /// ex: movl _foo, %eax
    funcRel32,             /// ex: movl _foo-L1(%eax), %eax
    pointer32,             /// ex: .long _foo
    delta32,               /// ex: .long _foo - .
    negDelta32,            /// ex: .long . - _foo

    // Kinds introduced by Passes:
    lazyPointer,           /// Location contains a lazy pointer.
    lazyImmediateLocation, /// Location contains immediate value used in stub.
  };

  static bool useExternalRelocationTo(const Atom &target);

  void applyFixupFinal(const Reference &ref, uint8_t *location,
                       uint64_t fixupAddress, uint64_t targetAddress,
                       uint64_t inAtomAddress);

  void applyFixupRelocatable(const Reference &ref, uint8_t *location,
                             uint64_t fixupAddress,
                             uint64_t targetAddress,
                             uint64_t inAtomAddress);
};

//===----------------------------------------------------------------------===//
//  ArchHandler_x86
//===----------------------------------------------------------------------===//

const Registry::KindStrings ArchHandler_x86::_sKindStrings[] = {
  LLD_KIND_STRING_ENTRY(invalid),
  LLD_KIND_STRING_ENTRY(modeCode),
  LLD_KIND_STRING_ENTRY(modeData),
  LLD_KIND_STRING_ENTRY(branch32),
  LLD_KIND_STRING_ENTRY(branch16),
  LLD_KIND_STRING_ENTRY(abs32),
  LLD_KIND_STRING_ENTRY(funcRel32),
  LLD_KIND_STRING_ENTRY(pointer32),
  LLD_KIND_STRING_ENTRY(delta32),
  LLD_KIND_STRING_ENTRY(negDelta32),
  LLD_KIND_STRING_ENTRY(lazyPointer),
  LLD_KIND_STRING_ENTRY(lazyImmediateLocation),
  LLD_KIND_STRING_END
};

const ArchHandler::StubInfo ArchHandler_x86::_sStubInfo = {
  "dyld_stub_binder",

  // Lazy pointer references
  { Reference::KindArch::x86, pointer32, 0, 0 },
  { Reference::KindArch::x86, lazyPointer, 0, 0 },

  // GOT pointer to dyld_stub_binder
  { Reference::KindArch::x86, pointer32, 0, 0 },

  // x86 code alignment
  1,

  // Stub size and code
  6,
  { 0xff, 0x25, 0x00, 0x00, 0x00, 0x00 },       // jmp *lazyPointer
  { Reference::KindArch::x86, abs32, 2, 0 },
  { false, 0, 0, 0 },

  // Stub Helper size and code
  10,
  { 0x68, 0x00, 0x00, 0x00, 0x00,               // pushl $lazy-info-offset
    0xE9, 0x00, 0x00, 0x00, 0x00 },             // jmp helperhelper
  { Reference::KindArch::x86, lazyImmediateLocation, 1, 0 },
  { Reference::KindArch::x86, branch32, 6, 0 },

  // Stub helper image cache content type
  DefinedAtom::typeNonLazyPointer,

  // Stub Helper-Common size and code
  12,
  // Stub helper alignment
  2,
  { 0x68, 0x00, 0x00, 0x00, 0x00,               // pushl $dyld_ImageLoaderCache
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,         // jmp *_fast_lazy_bind
    0x90 },                                     // nop
  { Reference::KindArch::x86, abs32, 1, 0 },
  { false, 0, 0, 0 },
  { Reference::KindArch::x86, abs32, 7, 0 },
  { false, 0, 0, 0 }
};

bool ArchHandler_x86::isCallSite(const Reference &ref) {
  return (ref.kindValue() == branch32);
}

bool ArchHandler_x86::isPointer(const Reference &ref) {
  return (ref.kindValue() == pointer32);
}

bool ArchHandler_x86::isPairedReloc(const Relocation &reloc) {
  if (!reloc.scattered)
    return false;
  return (reloc.type == GENERIC_RELOC_LOCAL_SECTDIFF) ||
         (reloc.type == GENERIC_RELOC_SECTDIFF);
}

llvm::Error
ArchHandler_x86::getReferenceInfo(const Relocation &reloc,
                                  const DefinedAtom *inAtom,
                                  uint32_t offsetInAtom,
                                  uint64_t fixupAddress, bool swap,
                                  FindAtomBySectionAndAddress atomFromAddress,
                                  FindAtomBySymbolIndex atomFromSymbolIndex,
                                  Reference::KindValue *kind,
                                  const lld::Atom **target,
                                  Reference::Addend *addend) {
  DefinedAtom::ContentPermissions perms;
  const uint8_t *fixupContent = &inAtom->rawContent()[offsetInAtom];
  uint64_t targetAddress;
  switch (relocPattern(reloc)) {
  case GENERIC_RELOC_VANILLA | rPcRel | rExtern | rLength4:
    // ex: call _foo (and _foo undefined)
    *kind = branch32;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = fixupAddress + 4 + (int32_t)*(const little32_t *)fixupContent;
    break;
  case GENERIC_RELOC_VANILLA | rPcRel | rLength4:
    // ex: call _foo (and _foo defined)
    *kind = branch32;
    targetAddress =
        fixupAddress + 4 + (int32_t) * (const little32_t *)fixupContent;
    return atomFromAddress(reloc.symbol, targetAddress, target, addend);
    break;
  case GENERIC_RELOC_VANILLA | rScattered | rPcRel | rLength4:
    // ex: call _foo+n (and _foo defined)
    *kind = branch32;
    targetAddress =
        fixupAddress + 4 + (int32_t) * (const little32_t *)fixupContent;
    if (auto ec = atomFromAddress(0, reloc.value, target, addend))
      return ec;
    *addend = targetAddress - reloc.value;
    break;
  case GENERIC_RELOC_VANILLA | rPcRel | rExtern | rLength2:
    // ex: callw _foo (and _foo undefined)
    *kind = branch16;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = fixupAddress + 2 + (int16_t)*(const little16_t *)fixupContent;
    break;
  case GENERIC_RELOC_VANILLA | rPcRel | rLength2:
    // ex: callw _foo (and _foo defined)
    *kind = branch16;
    targetAddress =
        fixupAddress + 2 + (int16_t) * (const little16_t *)fixupContent;
    return atomFromAddress(reloc.symbol, targetAddress, target, addend);
    break;
  case GENERIC_RELOC_VANILLA | rScattered | rPcRel | rLength2:
    // ex: callw _foo+n (and _foo defined)
    *kind = branch16;
    targetAddress =
        fixupAddress + 2 + (int16_t) * (const little16_t *)fixupContent;
    if (auto ec = atomFromAddress(0, reloc.value, target, addend))
      return ec;
    *addend = targetAddress - reloc.value;
    break;
  case GENERIC_RELOC_VANILLA | rExtern | rLength4:
    // ex: movl	_foo, %eax   (and _foo undefined)
    // ex: .long _foo        (and _foo undefined)
    perms = inAtom->permissions();
    *kind =
        ((perms & DefinedAtom::permR_X) == DefinedAtom::permR_X) ? abs32
                                                                 : pointer32;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = *(const ulittle32_t *)fixupContent;
    break;
  case GENERIC_RELOC_VANILLA | rLength4:
    // ex: movl	_foo, %eax   (and _foo defined)
    // ex: .long _foo        (and _foo defined)
    perms = inAtom->permissions();
    *kind =
        ((perms & DefinedAtom::permR_X) == DefinedAtom::permR_X) ? abs32
                                                                 : pointer32;
    targetAddress = *(const ulittle32_t *)fixupContent;
    return atomFromAddress(reloc.symbol, targetAddress, target, addend);
    break;
  case GENERIC_RELOC_VANILLA | rScattered | rLength4:
    // ex: .long _foo+n      (and _foo defined)
    perms = inAtom->permissions();
    *kind =
        ((perms & DefinedAtom::permR_X) == DefinedAtom::permR_X) ? abs32
                                                                 : pointer32;
    if (auto ec = atomFromAddress(0, reloc.value, target, addend))
      return ec;
    *addend = *(const ulittle32_t *)fixupContent - reloc.value;
    break;
  default:
    return llvm::make_error<GenericError>("unsupported i386 relocation type");
  }
  return llvm::Error::success();
}

llvm::Error
ArchHandler_x86::getPairReferenceInfo(const normalized::Relocation &reloc1,
                                      const normalized::Relocation &reloc2,
                                      const DefinedAtom *inAtom,
                                      uint32_t offsetInAtom,
                                      uint64_t fixupAddress, bool swap,
                                      bool scatterable,
                                      FindAtomBySectionAndAddress atomFromAddr,
                                      FindAtomBySymbolIndex atomFromSymbolIndex,
                                      Reference::KindValue *kind,
                                      const lld::Atom **target,
                                      Reference::Addend *addend) {
  const uint8_t *fixupContent = &inAtom->rawContent()[offsetInAtom];
  DefinedAtom::ContentPermissions perms = inAtom->permissions();
  uint32_t fromAddress;
  uint32_t toAddress;
  uint32_t value;
  const lld::Atom *fromTarget;
  Reference::Addend offsetInTo;
  Reference::Addend offsetInFrom;
  switch (relocPattern(reloc1) << 16 | relocPattern(reloc2)) {
  case ((GENERIC_RELOC_SECTDIFF | rScattered | rLength4) << 16 |
         GENERIC_RELOC_PAIR | rScattered | rLength4):
  case ((GENERIC_RELOC_LOCAL_SECTDIFF | rScattered | rLength4) << 16 |
         GENERIC_RELOC_PAIR | rScattered | rLength4):
    toAddress = reloc1.value;
    fromAddress = reloc2.value;
    value = *(const little32_t *)fixupContent;
    if (auto ec = atomFromAddr(0, toAddress, target, &offsetInTo))
      return ec;
    if (auto ec = atomFromAddr(0, fromAddress, &fromTarget, &offsetInFrom))
      return ec;
    if (fromTarget != inAtom) {
      if (*target != inAtom)
        return llvm::make_error<GenericError>(
            "SECTDIFF relocation where neither target is in atom");
      *kind = negDelta32;
      *addend = toAddress - value - fromAddress;
      *target = fromTarget;
    } else {
      if ((perms & DefinedAtom::permR_X) == DefinedAtom::permR_X) {
        // SECTDIFF relocations are used in i386 codegen where the function
        // prolog does a CALL to the next instruction which POPs the return
        // address into EBX which becomes the pic-base register.  The POP
        // instruction is label the used for the subtrahend in expressions.
        // The funcRel32 kind represents the 32-bit delta to some symbol from
        // the start of the function (atom) containing the funcRel32.
        *kind = funcRel32;
        uint32_t ta = fromAddress + value - toAddress;
        *addend = ta - offsetInFrom;
      } else {
        *kind = delta32;
        *addend = fromAddress + value - toAddress;
      }
    }
    return llvm::Error::success();
    break;
  default:
    return llvm::make_error<GenericError>("unsupported i386 relocation type");
  }
}

void ArchHandler_x86::generateAtomContent(const DefinedAtom &atom,
                                          bool relocatable,
                                          FindAddressForAtom findAddress,
                                          FindAddressForAtom findSectionAddress,
                                          uint64_t imageBaseAddress,
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
                                  atomAddress);
    }
  }
}

void ArchHandler_x86::applyFixupFinal(const Reference &ref, uint8_t *loc,
                                      uint64_t fixupAddress,
                                      uint64_t targetAddress,
                                      uint64_t inAtomAddress) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::x86);
  ulittle32_t *loc32 = reinterpret_cast<ulittle32_t *>(loc);
  switch (static_cast<X86Kind>(ref.kindValue())) {
  case branch32:
    *loc32 = (targetAddress - (fixupAddress + 4)) + ref.addend();
    break;
  case branch16:
    *loc32 = (targetAddress - (fixupAddress + 2)) + ref.addend();
    break;
  case pointer32:
  case abs32:
    *loc32 = targetAddress + ref.addend();
    break;
  case funcRel32:
    *loc32 = targetAddress - inAtomAddress + ref.addend();
    break;
  case delta32:
    *loc32 = targetAddress - fixupAddress + ref.addend();
    break;
  case negDelta32:
    *loc32 = fixupAddress - targetAddress + ref.addend();
    break;
  case modeCode:
  case modeData:
  case lazyPointer:
    // do nothing
    break;
  case lazyImmediateLocation:
    *loc32 = ref.addend();
    break;
  case invalid:
    llvm_unreachable("invalid x86 Reference Kind");
    break;
  }
}

void ArchHandler_x86::applyFixupRelocatable(const Reference &ref,
                                               uint8_t *loc,
                                               uint64_t fixupAddress,
                                               uint64_t targetAddress,
                                               uint64_t inAtomAddress) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::x86);
  bool useExternalReloc = useExternalRelocationTo(*ref.target());
  ulittle16_t *loc16 = reinterpret_cast<ulittle16_t *>(loc);
  ulittle32_t *loc32 = reinterpret_cast<ulittle32_t *>(loc);
  switch (static_cast<X86Kind>(ref.kindValue())) {
  case branch32:
    if (useExternalReloc)
      *loc32 = ref.addend() - (fixupAddress + 4);
    else
      *loc32  =(targetAddress - (fixupAddress+4)) + ref.addend();
    break;
  case branch16:
    if (useExternalReloc)
      *loc16 = ref.addend() - (fixupAddress + 2);
    else
      *loc16 = (targetAddress - (fixupAddress+2)) + ref.addend();
    break;
  case pointer32:
  case abs32:
    *loc32 = targetAddress + ref.addend();
    break;
  case funcRel32:
    *loc32 = targetAddress - inAtomAddress + ref.addend(); // FIXME
    break;
  case delta32:
    *loc32 = targetAddress - fixupAddress + ref.addend();
    break;
  case negDelta32:
    *loc32 = fixupAddress - targetAddress + ref.addend();
    break;
  case modeCode:
  case modeData:
  case lazyPointer:
  case lazyImmediateLocation:
    // do nothing
    break;
  case invalid:
    llvm_unreachable("invalid x86 Reference Kind");
    break;
  }
}

bool ArchHandler_x86::useExternalRelocationTo(const Atom &target) {
  // Undefined symbols are referenced via external relocations.
  if (isa<UndefinedAtom>(&target))
    return true;
  if (const DefinedAtom *defAtom = dyn_cast<DefinedAtom>(&target)) {
     switch (defAtom->merge()) {
     case DefinedAtom::mergeAsTentative:
       // Tentative definitions are referenced via external relocations.
       return true;
     case DefinedAtom::mergeAsWeak:
     case DefinedAtom::mergeAsWeakAndAddressUsed:
       // Global weak-defs are referenced via external relocations.
       return (defAtom->scope() == DefinedAtom::scopeGlobal);
     default:
       break;
    }
  }
  // Everything else is reference via an internal relocation.
  return false;
}

void ArchHandler_x86::appendSectionRelocations(
                                   const DefinedAtom &atom,
                                   uint64_t atomSectionOffset,
                                   const Reference &ref,
                                   FindSymbolIndexForAtom symbolIndexForAtom,
                                   FindSectionIndexForAtom sectionIndexForAtom,
                                   FindAddressForAtom addressForAtom,
                                   normalized::Relocations &relocs) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::x86);
  uint32_t sectionOffset = atomSectionOffset + ref.offsetInAtom();
  bool useExternalReloc = useExternalRelocationTo(*ref.target());
  switch (static_cast<X86Kind>(ref.kindValue())) {
  case modeCode:
  case modeData:
    break;
  case branch32:
    if (useExternalReloc) {
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  GENERIC_RELOC_VANILLA | rExtern    | rPcRel | rLength4);
    } else {
      if (ref.addend() != 0)
        appendReloc(relocs, sectionOffset, 0, addressForAtom(*ref.target()),
                  GENERIC_RELOC_VANILLA | rScattered | rPcRel |  rLength4);
      else
        appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()),0,
                  GENERIC_RELOC_VANILLA |              rPcRel | rLength4);
    }
    break;
  case branch16:
    if (useExternalReloc) {
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  GENERIC_RELOC_VANILLA | rExtern    | rPcRel | rLength2);
    } else {
      if (ref.addend() != 0)
        appendReloc(relocs, sectionOffset, 0, addressForAtom(*ref.target()),
                  GENERIC_RELOC_VANILLA | rScattered | rPcRel |  rLength2);
      else
        appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()),0,
                  GENERIC_RELOC_VANILLA |              rPcRel | rLength2);
    }
    break;
  case pointer32:
  case abs32:
    if (useExternalReloc)
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()),  0,
                GENERIC_RELOC_VANILLA |    rExtern     |  rLength4);
    else {
      if (ref.addend() != 0)
        appendReloc(relocs, sectionOffset, 0, addressForAtom(*ref.target()),
                GENERIC_RELOC_VANILLA |    rScattered  |  rLength4);
      else
        appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()), 0,
                GENERIC_RELOC_VANILLA |                   rLength4);
    }
    break;
  case funcRel32:
    appendReloc(relocs, sectionOffset, 0, addressForAtom(*ref.target()),
              GENERIC_RELOC_SECTDIFF |  rScattered    | rLength4);
    appendReloc(relocs, sectionOffset, 0, addressForAtom(atom) - ref.addend(),
              GENERIC_RELOC_PAIR     |  rScattered    | rLength4);
    break;
  case delta32:
    appendReloc(relocs, sectionOffset, 0, addressForAtom(*ref.target()),
              GENERIC_RELOC_SECTDIFF |  rScattered    | rLength4);
    appendReloc(relocs, sectionOffset, 0, addressForAtom(atom) +
                                                           ref.offsetInAtom(),
              GENERIC_RELOC_PAIR     |  rScattered    | rLength4);
    break;
  case negDelta32:
    appendReloc(relocs, sectionOffset, 0, addressForAtom(atom) +
                                                           ref.offsetInAtom(),
              GENERIC_RELOC_SECTDIFF |  rScattered    | rLength4);
    appendReloc(relocs, sectionOffset, 0, addressForAtom(*ref.target()),
              GENERIC_RELOC_PAIR     |  rScattered    | rLength4);
    break;
  case lazyPointer:
  case lazyImmediateLocation:
    llvm_unreachable("lazy reference kind implies Stubs pass was run");
    break;
  case invalid:
    llvm_unreachable("unknown x86 Reference Kind");
    break;
  }
}

std::unique_ptr<mach_o::ArchHandler> ArchHandler::create_x86() {
  return std::unique_ptr<mach_o::ArchHandler>(new ArchHandler_x86());
}

} // namespace mach_o
} // namespace lld
