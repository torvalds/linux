//===- lib/FileFormat/MachO/ArchHandler_arm64.cpp -------------------------===//
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
#include "llvm/Support/Format.h"

using namespace llvm::MachO;
using namespace lld::mach_o::normalized;

namespace lld {
namespace mach_o {

using llvm::support::ulittle32_t;
using llvm::support::ulittle64_t;

using llvm::support::little32_t;
using llvm::support::little64_t;

class ArchHandler_arm64 : public ArchHandler {
public:
  ArchHandler_arm64() = default;
  ~ArchHandler_arm64() override = default;

  const Registry::KindStrings *kindStrings() override { return _sKindStrings; }

  Reference::KindArch kindArch() override {
    return Reference::KindArch::AArch64;
  }

  /// Used by GOTPass to locate GOT References
  bool isGOTAccess(const Reference &ref, bool &canBypassGOT) override {
    if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
      return false;
    assert(ref.kindArch() == Reference::KindArch::AArch64);
    switch (ref.kindValue()) {
    case gotPage21:
    case gotOffset12:
      canBypassGOT = true;
      return true;
    case delta32ToGOT:
    case unwindCIEToPersonalityFunction:
    case imageOffsetGot:
      canBypassGOT = false;
      return true;
    default:
      return false;
    }
  }

  /// Used by GOTPass to update GOT References.
  void updateReferenceToGOT(const Reference *ref, bool targetNowGOT) override {
    // If GOT slot was instanciated, transform:
    //   gotPage21/gotOffset12 -> page21/offset12scale8
    // If GOT slot optimized away, transform:
    //   gotPage21/gotOffset12 -> page21/addOffset12
    assert(ref->kindNamespace() == Reference::KindNamespace::mach_o);
    assert(ref->kindArch() == Reference::KindArch::AArch64);
    switch (ref->kindValue()) {
    case gotPage21:
      const_cast<Reference *>(ref)->setKindValue(page21);
      break;
    case gotOffset12:
      const_cast<Reference *>(ref)->setKindValue(targetNowGOT ?
                                                 offset12scale8 : addOffset12);
      break;
    case delta32ToGOT:
      const_cast<Reference *>(ref)->setKindValue(delta32);
      break;
    case imageOffsetGot:
      const_cast<Reference *>(ref)->setKindValue(imageOffset);
      break;
    default:
      llvm_unreachable("Not a GOT reference");
    }
  }

  const StubInfo &stubInfo() override { return _sStubInfo; }

  bool isCallSite(const Reference &) override;
  bool isNonCallBranch(const Reference &) override {
    return false;
  }

  bool isPointer(const Reference &) override;
  bool isPairedReloc(const normalized::Relocation &) override;

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
    return unwindCIEToPersonalityFunction;
  }

  Reference::KindValue unwindRefToCIEKind() override {
    return negDelta32;
  }

  Reference::KindValue unwindRefToFunctionKind() override {
    return unwindFDEToFunction;
  }

  Reference::KindValue unwindRefToEhFrameKind() override {
    return unwindInfoToEhFrame;
  }

  Reference::KindValue pointerKind() override {
    return pointer64;
  }

  Reference::KindValue lazyImmediateLocationKind() override {
    return lazyImmediateLocation;
  }

  uint32_t dwarfCompactUnwindType() override {
    return 0x03000000;
  }

  llvm::Error getReferenceInfo(const normalized::Relocation &reloc,
                               const DefinedAtom *inAtom,
                               uint32_t offsetInAtom,
                               uint64_t fixupAddress, bool isBig,
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
                           uint64_t fixupAddress, bool isBig, bool scatterable,
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
                           uint64_t imageBaseAddress,
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
  static const StubInfo _sStubInfo;

  enum Arm64Kind : Reference::KindValue {
    invalid,               /// for error condition

    // Kinds found in mach-o .o files:
    branch26,              /// ex: bl   _foo
    page21,                /// ex: adrp x1, _foo@PAGE
    offset12,              /// ex: ldrb w0, [x1, _foo@PAGEOFF]
    offset12scale2,        /// ex: ldrs w0, [x1, _foo@PAGEOFF]
    offset12scale4,        /// ex: ldr  w0, [x1, _foo@PAGEOFF]
    offset12scale8,        /// ex: ldr  x0, [x1, _foo@PAGEOFF]
    offset12scale16,       /// ex: ldr  q0, [x1, _foo@PAGEOFF]
    gotPage21,             /// ex: adrp x1, _foo@GOTPAGE
    gotOffset12,           /// ex: ldr  w0, [x1, _foo@GOTPAGEOFF]
    tlvPage21,             /// ex: adrp x1, _foo@TLVPAGE
    tlvOffset12,           /// ex: ldr  w0, [x1, _foo@TLVPAGEOFF]

    pointer64,             /// ex: .quad _foo
    delta64,               /// ex: .quad _foo - .
    delta32,               /// ex: .long _foo - .
    negDelta32,            /// ex: .long . - _foo
    pointer64ToGOT,        /// ex: .quad _foo@GOT
    delta32ToGOT,          /// ex: .long _foo@GOT - .

    // Kinds introduced by Passes:
    addOffset12,           /// Location contains LDR to change into ADD.
    lazyPointer,           /// Location contains a lazy pointer.
    lazyImmediateLocation, /// Location contains immediate value used in stub.
    imageOffset,           /// Location contains offset of atom in final image
    imageOffsetGot,        /// Location contains offset of GOT entry for atom in
                           /// final image (typically personality function).
    unwindCIEToPersonalityFunction,   /// Nearly delta32ToGOT, but cannot be
                           /// rematerialized in relocatable object
                           /// (yay for implicit contracts!).
    unwindFDEToFunction,   /// Nearly delta64, but cannot be rematerialized in
                           /// relocatable object (yay for implicit contracts!).
    unwindInfoToEhFrame,   /// Fix low 24 bits of compact unwind encoding to
                           /// refer to __eh_frame entry.
  };

  void applyFixupFinal(const Reference &ref, uint8_t *location,
                       uint64_t fixupAddress, uint64_t targetAddress,
                       uint64_t inAtomAddress, uint64_t imageBaseAddress,
                       FindAddressForAtom findSectionAddress);

  void applyFixupRelocatable(const Reference &ref, uint8_t *location,
                             uint64_t fixupAddress, uint64_t targetAddress,
                             uint64_t inAtomAddress, bool targetUnnamed);

  // Utility functions for inspecting/updating instructions.
  static uint32_t setDisplacementInBranch26(uint32_t instr, int32_t disp);
  static uint32_t setDisplacementInADRP(uint32_t instr, int64_t disp);
  static Arm64Kind offset12KindFromInstruction(uint32_t instr);
  static uint32_t setImm12(uint32_t instr, uint32_t offset);
};

const Registry::KindStrings ArchHandler_arm64::_sKindStrings[] = {
  LLD_KIND_STRING_ENTRY(invalid),
  LLD_KIND_STRING_ENTRY(branch26),
  LLD_KIND_STRING_ENTRY(page21),
  LLD_KIND_STRING_ENTRY(offset12),
  LLD_KIND_STRING_ENTRY(offset12scale2),
  LLD_KIND_STRING_ENTRY(offset12scale4),
  LLD_KIND_STRING_ENTRY(offset12scale8),
  LLD_KIND_STRING_ENTRY(offset12scale16),
  LLD_KIND_STRING_ENTRY(gotPage21),
  LLD_KIND_STRING_ENTRY(gotOffset12),
  LLD_KIND_STRING_ENTRY(tlvPage21),
  LLD_KIND_STRING_ENTRY(tlvOffset12),
  LLD_KIND_STRING_ENTRY(pointer64),
  LLD_KIND_STRING_ENTRY(delta64),
  LLD_KIND_STRING_ENTRY(delta32),
  LLD_KIND_STRING_ENTRY(negDelta32),
  LLD_KIND_STRING_ENTRY(pointer64ToGOT),
  LLD_KIND_STRING_ENTRY(delta32ToGOT),

  LLD_KIND_STRING_ENTRY(addOffset12),
  LLD_KIND_STRING_ENTRY(lazyPointer),
  LLD_KIND_STRING_ENTRY(lazyImmediateLocation),
  LLD_KIND_STRING_ENTRY(imageOffset),
  LLD_KIND_STRING_ENTRY(imageOffsetGot),
  LLD_KIND_STRING_ENTRY(unwindCIEToPersonalityFunction),
  LLD_KIND_STRING_ENTRY(unwindFDEToFunction),
  LLD_KIND_STRING_ENTRY(unwindInfoToEhFrame),

  LLD_KIND_STRING_END
};

const ArchHandler::StubInfo ArchHandler_arm64::_sStubInfo = {
  "dyld_stub_binder",

  // Lazy pointer references
  { Reference::KindArch::AArch64, pointer64, 0, 0 },
  { Reference::KindArch::AArch64, lazyPointer, 0, 0 },

  // GOT pointer to dyld_stub_binder
  { Reference::KindArch::AArch64, pointer64, 0, 0 },

  // arm64 code alignment 2^1
  1,

  // Stub size and code
  12,
  { 0x10, 0x00, 0x00, 0x90,   // ADRP  X16, lazy_pointer@page
    0x10, 0x02, 0x40, 0xF9,   // LDR   X16, [X16, lazy_pointer@pageoff]
    0x00, 0x02, 0x1F, 0xD6 }, // BR    X16
  { Reference::KindArch::AArch64, page21, 0, 0 },
  { true,                         offset12scale8, 4, 0 },

  // Stub Helper size and code
  12,
  { 0x50, 0x00, 0x00, 0x18,   //      LDR   W16, L0
    0x00, 0x00, 0x00, 0x14,   //      LDR   B  helperhelper
    0x00, 0x00, 0x00, 0x00 }, // L0: .long 0
  { Reference::KindArch::AArch64, lazyImmediateLocation, 8, 0 },
  { Reference::KindArch::AArch64, branch26, 4, 0 },

  // Stub helper image cache content type
  DefinedAtom::typeGOT,

  // Stub Helper-Common size and code
  24,
  // Stub helper alignment
  2,
  { 0x11, 0x00, 0x00, 0x90,   //  ADRP  X17, dyld_ImageLoaderCache@page
    0x31, 0x02, 0x00, 0x91,   //  ADD   X17, X17, dyld_ImageLoaderCache@pageoff
    0xF0, 0x47, 0xBF, 0xA9,   //  STP   X16/X17, [SP, #-16]!
    0x10, 0x00, 0x00, 0x90,   //  ADRP  X16, _fast_lazy_bind@page
    0x10, 0x02, 0x40, 0xF9,   //  LDR   X16, [X16,_fast_lazy_bind@pageoff]
    0x00, 0x02, 0x1F, 0xD6 }, //  BR    X16
  { Reference::KindArch::AArch64, page21,   0, 0 },
  { true,                         offset12, 4, 0 },
  { Reference::KindArch::AArch64, page21,   12, 0 },
  { true,                         offset12scale8, 16, 0 }
};

bool ArchHandler_arm64::isCallSite(const Reference &ref) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return false;
  assert(ref.kindArch() == Reference::KindArch::AArch64);
  return (ref.kindValue() == branch26);
}

bool ArchHandler_arm64::isPointer(const Reference &ref) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return false;
  assert(ref.kindArch() == Reference::KindArch::AArch64);
  Reference::KindValue kind = ref.kindValue();
  return (kind == pointer64);
}

bool ArchHandler_arm64::isPairedReloc(const Relocation &r) {
  return ((r.type == ARM64_RELOC_ADDEND) || (r.type == ARM64_RELOC_SUBTRACTOR));
}

uint32_t ArchHandler_arm64::setDisplacementInBranch26(uint32_t instr,
                                                      int32_t displacement) {
  assert((displacement <= 134217727) && (displacement > (-134217728)) &&
         "arm64 branch out of range");
  return (instr & 0xFC000000) | ((uint32_t)(displacement >> 2) & 0x03FFFFFF);
}

uint32_t ArchHandler_arm64::setDisplacementInADRP(uint32_t instruction,
                                                  int64_t displacement) {
  assert((displacement <= 0x100000000LL) && (displacement > (-0x100000000LL)) &&
         "arm64 ADRP out of range");
  assert(((instruction & 0x9F000000) == 0x90000000) &&
         "reloc not on ADRP instruction");
  uint32_t immhi = (displacement >> 9) & (0x00FFFFE0);
  uint32_t immlo = (displacement << 17) & (0x60000000);
  return (instruction & 0x9F00001F) | immlo | immhi;
}

ArchHandler_arm64::Arm64Kind
ArchHandler_arm64::offset12KindFromInstruction(uint32_t instruction) {
  if (instruction & 0x08000000) {
    switch ((instruction >> 30) & 0x3) {
    case 0:
      if ((instruction & 0x04800000) == 0x04800000)
        return offset12scale16;
      return offset12;
    case 1:
      return offset12scale2;
    case 2:
      return offset12scale4;
    case 3:
      return offset12scale8;
    }
  }
  return offset12;
}

uint32_t ArchHandler_arm64::setImm12(uint32_t instruction, uint32_t offset) {
  assert(((offset & 0xFFFFF000) == 0) && "imm12 offset out of range");
  uint32_t imm12 = offset << 10;
  return (instruction & 0xFFC003FF) | imm12;
}

llvm::Error ArchHandler_arm64::getReferenceInfo(
    const Relocation &reloc, const DefinedAtom *inAtom, uint32_t offsetInAtom,
    uint64_t fixupAddress, bool isBig,
    FindAtomBySectionAndAddress atomFromAddress,
    FindAtomBySymbolIndex atomFromSymbolIndex, Reference::KindValue *kind,
    const lld::Atom **target, Reference::Addend *addend) {
  const uint8_t *fixupContent = &inAtom->rawContent()[offsetInAtom];
  switch (relocPattern(reloc)) {
  case ARM64_RELOC_BRANCH26           | rPcRel | rExtern | rLength4:
    // ex: bl _foo
    *kind = branch26;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = 0;
    return llvm::Error::success();
  case ARM64_RELOC_PAGE21             | rPcRel | rExtern | rLength4:
    // ex: adrp x1, _foo@PAGE
    *kind = page21;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = 0;
    return llvm::Error::success();
  case ARM64_RELOC_PAGEOFF12                   | rExtern | rLength4:
    // ex: ldr x0, [x1, _foo@PAGEOFF]
    *kind = offset12KindFromInstruction(*(const little32_t *)fixupContent);
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = 0;
    return llvm::Error::success();
  case ARM64_RELOC_GOT_LOAD_PAGE21    | rPcRel | rExtern | rLength4:
    // ex: adrp x1, _foo@GOTPAGE
    *kind = gotPage21;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = 0;
    return llvm::Error::success();
  case ARM64_RELOC_GOT_LOAD_PAGEOFF12          | rExtern | rLength4:
    // ex: ldr x0, [x1, _foo@GOTPAGEOFF]
    *kind = gotOffset12;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = 0;
    return llvm::Error::success();
  case ARM64_RELOC_TLVP_LOAD_PAGE21   | rPcRel | rExtern | rLength4:
    // ex: adrp x1, _foo@TLVPAGE
    *kind = tlvPage21;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = 0;
    return llvm::Error::success();
  case ARM64_RELOC_TLVP_LOAD_PAGEOFF12         | rExtern | rLength4:
    // ex: ldr x0, [x1, _foo@TLVPAGEOFF]
    *kind = tlvOffset12;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = 0;
    return llvm::Error::success();
  case ARM64_RELOC_UNSIGNED                    | rExtern | rLength8:
    // ex: .quad _foo + N
    *kind = pointer64;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = *(const little64_t *)fixupContent;
    return llvm::Error::success();
  case ARM64_RELOC_UNSIGNED                              | rLength8:
     // ex: .quad Lfoo + N
     *kind = pointer64;
     return atomFromAddress(reloc.symbol, *(const little64_t *)fixupContent,
                            target, addend);
  case ARM64_RELOC_POINTER_TO_GOT              | rExtern | rLength8:
    // ex: .quad _foo@GOT
    *kind = pointer64ToGOT;
    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = 0;
    return llvm::Error::success();
  case ARM64_RELOC_POINTER_TO_GOT     | rPcRel | rExtern | rLength4:
    // ex: .long _foo@GOT - .

    // If we are in an .eh_frame section, then the kind of the relocation should
    // not be delta32ToGOT.  It may instead be unwindCIEToPersonalityFunction.
    if (inAtom->contentType() == DefinedAtom::typeCFI)
      *kind = unwindCIEToPersonalityFunction;
    else
      *kind = delta32ToGOT;

    if (auto ec = atomFromSymbolIndex(reloc.symbol, target))
      return ec;
    *addend = 0;
    return llvm::Error::success();
  default:
    return llvm::make_error<GenericError>("unsupported arm64 relocation type");
  }
}

llvm::Error ArchHandler_arm64::getPairReferenceInfo(
    const normalized::Relocation &reloc1, const normalized::Relocation &reloc2,
    const DefinedAtom *inAtom, uint32_t offsetInAtom, uint64_t fixupAddress,
    bool swap, bool scatterable, FindAtomBySectionAndAddress atomFromAddress,
    FindAtomBySymbolIndex atomFromSymbolIndex, Reference::KindValue *kind,
    const lld::Atom **target, Reference::Addend *addend) {
  const uint8_t *fixupContent = &inAtom->rawContent()[offsetInAtom];
  switch (relocPattern(reloc1) << 16 | relocPattern(reloc2)) {
  case ((ARM64_RELOC_ADDEND                                | rLength4) << 16 |
         ARM64_RELOC_BRANCH26           | rPcRel | rExtern | rLength4):
    // ex: bl _foo+8
    *kind = branch26;
    if (auto ec = atomFromSymbolIndex(reloc2.symbol, target))
      return ec;
    *addend = reloc1.symbol;
    return llvm::Error::success();
  case ((ARM64_RELOC_ADDEND                                | rLength4) << 16 |
         ARM64_RELOC_PAGE21             | rPcRel | rExtern | rLength4):
    // ex: adrp x1, _foo@PAGE
    *kind = page21;
    if (auto ec = atomFromSymbolIndex(reloc2.symbol, target))
      return ec;
    *addend = reloc1.symbol;
    return llvm::Error::success();
  case ((ARM64_RELOC_ADDEND                                | rLength4) << 16 |
         ARM64_RELOC_PAGEOFF12                   | rExtern | rLength4): {
    // ex: ldr w0, [x1, _foo@PAGEOFF]
    uint32_t cont32 = (int32_t)*(const little32_t *)fixupContent;
    *kind = offset12KindFromInstruction(cont32);
    if (auto ec = atomFromSymbolIndex(reloc2.symbol, target))
      return ec;
    *addend = reloc1.symbol;
    return llvm::Error::success();
  }
  case ((ARM64_RELOC_SUBTRACTOR                  | rExtern | rLength8) << 16 |
         ARM64_RELOC_UNSIGNED                    | rExtern | rLength8):
    // ex: .quad _foo - .
    if (auto ec = atomFromSymbolIndex(reloc2.symbol, target))
      return ec;

    // If we are in an .eh_frame section, then the kind of the relocation should
    // not be delta64.  It may instead be unwindFDEToFunction.
    if (inAtom->contentType() == DefinedAtom::typeCFI)
      *kind = unwindFDEToFunction;
    else
      *kind = delta64;

    // The offsets of the 2 relocations must match
    if (reloc1.offset != reloc2.offset)
      return llvm::make_error<GenericError>(
                                    "paired relocs must have the same offset");
    *addend = (int64_t)*(const little64_t *)fixupContent + offsetInAtom;
    return llvm::Error::success();
  case ((ARM64_RELOC_SUBTRACTOR                  | rExtern | rLength4) << 16 |
         ARM64_RELOC_UNSIGNED                    | rExtern | rLength4):
    // ex: .quad _foo - .
    *kind = delta32;
    if (auto ec = atomFromSymbolIndex(reloc2.symbol, target))
      return ec;
    *addend = (int32_t)*(const little32_t *)fixupContent + offsetInAtom;
    return llvm::Error::success();
  default:
    return llvm::make_error<GenericError>("unsupported arm64 relocation pair");
  }
}

void ArchHandler_arm64::generateAtomContent(
    const DefinedAtom &atom, bool relocatable, FindAddressForAtom findAddress,
    FindAddressForAtom findSectionAddress, uint64_t imageBaseAddress,
    llvm::MutableArrayRef<uint8_t> atomContentBuffer) {
  // Copy raw bytes.
  std::copy(atom.rawContent().begin(), atom.rawContent().end(),
            atomContentBuffer.begin());
  // Apply fix-ups.
#ifndef NDEBUG
  if (atom.begin() != atom.end()) {
    DEBUG_WITH_TYPE("atom-content", llvm::dbgs()
                    << "Applying fixups to atom:\n"
                    << "   address="
                    << llvm::format("    0x%09lX", &atom)
                    << ", file=#"
                    << atom.file().ordinal()
                    << ", atom=#"
                    << atom.ordinal()
                    << ", name="
                    << atom.name()
                    << ", type="
                    << atom.contentType()
                    << "\n");
  }
#endif
  for (const Reference *ref : atom) {
    uint32_t offset = ref->offsetInAtom();
    const Atom *target = ref->target();
    bool targetUnnamed = target->name().empty();
    uint64_t targetAddress = 0;
    if (isa<DefinedAtom>(target))
      targetAddress = findAddress(*target);
    uint64_t atomAddress = findAddress(atom);
    uint64_t fixupAddress = atomAddress + offset;
    if (relocatable) {
      applyFixupRelocatable(*ref, &atomContentBuffer[offset], fixupAddress,
                            targetAddress, atomAddress, targetUnnamed);
    } else {
      applyFixupFinal(*ref, &atomContentBuffer[offset], fixupAddress,
                      targetAddress, atomAddress, imageBaseAddress,
                      findSectionAddress);
    }
  }
}

void ArchHandler_arm64::applyFixupFinal(const Reference &ref, uint8_t *loc,
                                        uint64_t fixupAddress,
                                        uint64_t targetAddress,
                                        uint64_t inAtomAddress,
                                        uint64_t imageBaseAddress,
                                        FindAddressForAtom findSectionAddress) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::AArch64);
  ulittle32_t *loc32 = reinterpret_cast<ulittle32_t *>(loc);
  ulittle64_t *loc64 = reinterpret_cast<ulittle64_t *>(loc);
  int32_t displacement;
  uint32_t instruction;
  uint32_t value32;
  uint32_t value64;
  switch (static_cast<Arm64Kind>(ref.kindValue())) {
  case branch26:
    displacement = (targetAddress - fixupAddress) + ref.addend();
    *loc32 = setDisplacementInBranch26(*loc32, displacement);
    return;
  case page21:
  case gotPage21:
  case tlvPage21:
    displacement =
        ((targetAddress + ref.addend()) & (-4096)) - (fixupAddress & (-4096));
    *loc32 = setDisplacementInADRP(*loc32, displacement);
    return;
  case offset12:
  case gotOffset12:
  case tlvOffset12:
    displacement = (targetAddress + ref.addend()) & 0x00000FFF;
    *loc32 = setImm12(*loc32, displacement);
    return;
  case offset12scale2:
    displacement = (targetAddress + ref.addend()) & 0x00000FFF;
    assert(((displacement & 0x1) == 0) &&
           "scaled imm12 not accessing 2-byte aligneds");
    *loc32 = setImm12(*loc32, displacement >> 1);
    return;
  case offset12scale4:
    displacement = (targetAddress + ref.addend()) & 0x00000FFF;
    assert(((displacement & 0x3) == 0) &&
           "scaled imm12 not accessing 4-byte aligned");
    *loc32 = setImm12(*loc32, displacement >> 2);
    return;
  case offset12scale8:
    displacement = (targetAddress + ref.addend()) & 0x00000FFF;
    assert(((displacement & 0x7) == 0) &&
           "scaled imm12 not accessing 8-byte aligned");
    *loc32 = setImm12(*loc32, displacement >> 3);
    return;
  case offset12scale16:
    displacement = (targetAddress + ref.addend()) & 0x00000FFF;
    assert(((displacement & 0xF) == 0) &&
           "scaled imm12 not accessing 16-byte aligned");
    *loc32 = setImm12(*loc32, displacement >> 4);
    return;
  case addOffset12:
    instruction = *loc32;
    assert(((instruction & 0xFFC00000) == 0xF9400000) &&
           "GOT reloc is not an LDR instruction");
    displacement = (targetAddress + ref.addend()) & 0x00000FFF;
    value32 = 0x91000000 | (instruction & 0x000003FF);
    instruction = setImm12(value32, displacement);
    *loc32 = instruction;
    return;
  case pointer64:
  case pointer64ToGOT:
    *loc64 = targetAddress + ref.addend();
    return;
  case delta64:
  case unwindFDEToFunction:
    *loc64 = (targetAddress - fixupAddress) + ref.addend();
    return;
  case delta32:
  case delta32ToGOT:
  case unwindCIEToPersonalityFunction:
    *loc32 = (targetAddress - fixupAddress) + ref.addend();
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
    *loc32 = (targetAddress - imageBaseAddress) + ref.addend();
    return;
  case imageOffsetGot:
    llvm_unreachable("imageOffsetGot should have been changed to imageOffset");
    break;
  case unwindInfoToEhFrame:
    value64 = targetAddress - findSectionAddress(*ref.target()) + ref.addend();
    assert(value64 < 0xffffffU && "offset in __eh_frame too large");
    *loc32 = (*loc32 & 0xff000000U) | value64;
    return;
  case invalid:
    // Fall into llvm_unreachable().
    break;
  }
  llvm_unreachable("invalid arm64 Reference Kind");
}

void ArchHandler_arm64::applyFixupRelocatable(const Reference &ref,
                                              uint8_t *loc,
                                              uint64_t fixupAddress,
                                              uint64_t targetAddress,
                                              uint64_t inAtomAddress,
                                              bool targetUnnamed) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::AArch64);
  ulittle32_t *loc32 = reinterpret_cast<ulittle32_t *>(loc);
  ulittle64_t *loc64 = reinterpret_cast<ulittle64_t *>(loc);
  switch (static_cast<Arm64Kind>(ref.kindValue())) {
  case branch26:
    *loc32 = setDisplacementInBranch26(*loc32, 0);
    return;
  case page21:
  case gotPage21:
  case tlvPage21:
    *loc32 = setDisplacementInADRP(*loc32, 0);
    return;
  case offset12:
  case offset12scale2:
  case offset12scale4:
  case offset12scale8:
  case offset12scale16:
  case gotOffset12:
  case tlvOffset12:
    *loc32 = setImm12(*loc32, 0);
    return;
  case pointer64:
    if (targetUnnamed)
      *loc64 = targetAddress + ref.addend();
    else
      *loc64 = ref.addend();
    return;
  case delta64:
    *loc64 = ref.addend() + inAtomAddress - fixupAddress;
    return;
  case unwindFDEToFunction:
    // We don't emit unwindFDEToFunction in -r mode as they are implicitly
    // generated from the data in the __eh_frame section.  So here we need
    // to use the targetAddress so that we can generate the full relocation
    // when we parse again later.
    *loc64 = targetAddress - fixupAddress;
    return;
  case delta32:
    *loc32 = ref.addend() + inAtomAddress - fixupAddress;
    return;
  case negDelta32:
    // We don't emit negDelta32 in -r mode as they are implicitly
    // generated from the data in the __eh_frame section.  So here we need
    // to use the targetAddress so that we can generate the full relocation
    // when we parse again later.
    *loc32 = fixupAddress - targetAddress + ref.addend();
    return;
  case pointer64ToGOT:
    *loc64 = 0;
    return;
  case delta32ToGOT:
    *loc32 = inAtomAddress - fixupAddress;
    return;
  case unwindCIEToPersonalityFunction:
    // We don't emit unwindCIEToPersonalityFunction in -r mode as they are
    // implicitly generated from the data in the __eh_frame section.  So here we
    // need to use the targetAddress so that we can generate the full relocation
    // when we parse again later.
    *loc32 = targetAddress - fixupAddress;
    return;
  case addOffset12:
    llvm_unreachable("lazy reference kind implies GOT pass was run");
  case lazyPointer:
  case lazyImmediateLocation:
    llvm_unreachable("lazy reference kind implies Stubs pass was run");
  case imageOffset:
  case imageOffsetGot:
  case unwindInfoToEhFrame:
    llvm_unreachable("fixup implies __unwind_info");
    return;
  case invalid:
    // Fall into llvm_unreachable().
    break;
  }
  llvm_unreachable("unknown arm64 Reference Kind");
}

void ArchHandler_arm64::appendSectionRelocations(
    const DefinedAtom &atom, uint64_t atomSectionOffset, const Reference &ref,
    FindSymbolIndexForAtom symbolIndexForAtom,
    FindSectionIndexForAtom sectionIndexForAtom,
    FindAddressForAtom addressForAtom, normalized::Relocations &relocs) {
  if (ref.kindNamespace() != Reference::KindNamespace::mach_o)
    return;
  assert(ref.kindArch() == Reference::KindArch::AArch64);
  uint32_t sectionOffset = atomSectionOffset + ref.offsetInAtom();
  switch (static_cast<Arm64Kind>(ref.kindValue())) {
  case branch26:
    if (ref.addend()) {
      appendReloc(relocs, sectionOffset, ref.addend(), 0,
                  ARM64_RELOC_ADDEND | rLength4);
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_BRANCH26 | rPcRel | rExtern | rLength4);
     } else {
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_BRANCH26 | rPcRel | rExtern | rLength4);
    }
    return;
  case page21:
    if (ref.addend()) {
      appendReloc(relocs, sectionOffset, ref.addend(), 0,
                  ARM64_RELOC_ADDEND | rLength4);
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_PAGE21 | rPcRel | rExtern | rLength4);
     } else {
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_PAGE21 | rPcRel | rExtern | rLength4);
    }
    return;
  case offset12:
  case offset12scale2:
  case offset12scale4:
  case offset12scale8:
  case offset12scale16:
    if (ref.addend()) {
      appendReloc(relocs, sectionOffset, ref.addend(), 0,
                  ARM64_RELOC_ADDEND | rLength4);
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_PAGEOFF12  | rExtern | rLength4);
     } else {
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_PAGEOFF12 | rExtern | rLength4);
    }
    return;
  case gotPage21:
    assert(ref.addend() == 0);
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_GOT_LOAD_PAGE21 | rPcRel | rExtern | rLength4);
    return;
  case gotOffset12:
    assert(ref.addend() == 0);
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_GOT_LOAD_PAGEOFF12 | rExtern | rLength4);
    return;
  case tlvPage21:
    assert(ref.addend() == 0);
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_TLVP_LOAD_PAGE21 | rPcRel | rExtern | rLength4);
    return;
  case tlvOffset12:
    assert(ref.addend() == 0);
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_TLVP_LOAD_PAGEOFF12 | rExtern | rLength4);
    return;
  case pointer64:
    if (ref.target()->name().empty())
      appendReloc(relocs, sectionOffset, sectionIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_UNSIGNED           | rLength8);
    else
      appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_UNSIGNED | rExtern | rLength8);
    return;
  case delta64:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(atom), 0,
                ARM64_RELOC_SUBTRACTOR | rExtern | rLength8);
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                ARM64_RELOC_UNSIGNED  | rExtern | rLength8);
    return;
  case delta32:
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(atom), 0,
                ARM64_RELOC_SUBTRACTOR | rExtern | rLength4 );
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                ARM64_RELOC_UNSIGNED   | rExtern | rLength4 );
    return;
  case pointer64ToGOT:
    assert(ref.addend() == 0);
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_POINTER_TO_GOT | rExtern | rLength8);
    return;
  case delta32ToGOT:
    assert(ref.addend() == 0);
    appendReloc(relocs, sectionOffset, symbolIndexForAtom(*ref.target()), 0,
                  ARM64_RELOC_POINTER_TO_GOT | rPcRel | rExtern | rLength4);
    return;
  case addOffset12:
    llvm_unreachable("lazy reference kind implies GOT pass was run");
  case lazyPointer:
  case lazyImmediateLocation:
    llvm_unreachable("lazy reference kind implies Stubs pass was run");
  case imageOffset:
  case imageOffsetGot:
    llvm_unreachable("deltas from mach_header can only be in final images");
  case unwindCIEToPersonalityFunction:
  case unwindFDEToFunction:
  case unwindInfoToEhFrame:
  case negDelta32:
    // Do nothing.
    return;
  case invalid:
    // Fall into llvm_unreachable().
    break;
  }
  llvm_unreachable("unknown arm64 Reference Kind");
}

std::unique_ptr<mach_o::ArchHandler> ArchHandler::create_arm64() {
  return std::unique_ptr<mach_o::ArchHandler>(new ArchHandler_arm64());
}

} // namespace mach_o
} // namespace lld
