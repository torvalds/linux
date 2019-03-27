//===- lib/FileFormat/MachO/ArchHandler.h ---------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_MACHO_ARCH_HANDLER_H
#define LLD_READER_WRITER_MACHO_ARCH_HANDLER_H

#include "Atoms.h"
#include "File.h"
#include "MachONormalizedFile.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/Error.h"
#include "lld/Core/Reference.h"
#include "lld/Core/Simple.h"
#include "lld/ReaderWriter/MachOLinkingContext.h"
#include "llvm/ADT/Triple.h"

namespace lld {
namespace mach_o {

///
/// The ArchHandler class handles all architecture specific aspects of
/// mach-o linking.
///
class ArchHandler {
public:
  virtual ~ArchHandler();

  /// There is no public interface to subclasses of ArchHandler, so this
  /// is the only way to instantiate an ArchHandler.
  static std::unique_ptr<ArchHandler> create(MachOLinkingContext::Arch arch);

  /// Get (arch specific) kind strings used by Registry.
  virtual const Registry::KindStrings *kindStrings() = 0;

  /// Convert mach-o Arch to Reference::KindArch.
  virtual Reference::KindArch kindArch() = 0;

  /// Used by StubPass to update References to shared library functions
  /// to be references to a stub.
  virtual bool isCallSite(const Reference &) = 0;

  /// Used by GOTPass to locate GOT References
  virtual bool isGOTAccess(const Reference &, bool &canBypassGOT) {
    return false;
  }

  /// Used by TLVPass to locate TLV References.
  virtual bool isTLVAccess(const Reference &) const { return false; }

  /// Used by the TLVPass to update TLV References.
  virtual void updateReferenceToTLV(const Reference *) {}

  /// Used by ShimPass to insert shims in branches that switch mode.
  virtual bool isNonCallBranch(const Reference &) = 0;

  /// Used by GOTPass to update GOT References
  virtual void updateReferenceToGOT(const Reference *, bool targetIsNowGOT) {}

  /// Does this architecture make use of __unwind_info sections for exception
  /// handling? If so, it will need a separate pass to create them.
  virtual bool needsCompactUnwind() = 0;

  /// Returns the kind of reference to use to synthesize a 32-bit image-offset
  /// value, used in the __unwind_info section.
  virtual Reference::KindValue imageOffsetKind() = 0;

  /// Returns the kind of reference to use to synthesize a 32-bit image-offset
  /// indirect value. Used for personality functions in the __unwind_info
  /// section.
  virtual Reference::KindValue imageOffsetKindIndirect() = 0;

  /// Architecture specific compact unwind type that signals __eh_frame should
  /// actually be used.
  virtual uint32_t dwarfCompactUnwindType() = 0;

  /// Reference from an __eh_frame CIE atom to its personality function it's
  /// describing. Usually pointer-sized and PC-relative, but differs in whether
  /// it needs to be in relocatable objects.
  virtual Reference::KindValue unwindRefToPersonalityFunctionKind() = 0;

  /// Reference from an __eh_frame FDE to the CIE it's based on.
  virtual Reference::KindValue unwindRefToCIEKind() = 0;

  /// Reference from an __eh_frame FDE atom to the function it's
  /// describing. Usually pointer-sized and PC-relative, but differs in whether
  /// it needs to be in relocatable objects.
  virtual Reference::KindValue unwindRefToFunctionKind() = 0;

  /// Reference from an __unwind_info entry of dwarfCompactUnwindType to the
  /// required __eh_frame entry. On current architectures, the low 24 bits
  /// represent the offset of the function's FDE entry from the start of
  /// __eh_frame.
  virtual Reference::KindValue unwindRefToEhFrameKind() = 0;

  /// Returns a pointer sized reference kind.  On 64-bit targets this will
  /// likely be something like pointer64, and pointer32 on 32-bit targets.
  virtual Reference::KindValue pointerKind() = 0;

  virtual const Atom *fdeTargetFunction(const DefinedAtom *fde);

  /// Used by normalizedFromAtoms() to know where to generated rebasing and
  /// binding info in final executables.
  virtual bool isPointer(const Reference &) = 0;

  /// Used by normalizedFromAtoms() to know where to generated lazy binding
  /// info in final executables.
  virtual bool isLazyPointer(const Reference &);

  /// Reference from an __stub_helper entry to the required offset of the
  /// lazy bind commands.
  virtual Reference::KindValue lazyImmediateLocationKind() = 0;

  /// Returns true if the specified relocation is paired to the next relocation.
  virtual bool isPairedReloc(const normalized::Relocation &) = 0;

  /// Prototype for a helper function.  Given a sectionIndex and address,
  /// finds the atom and offset with that atom of that address.
  typedef std::function<llvm::Error (uint32_t sectionIndex, uint64_t addr,
                        const lld::Atom **, Reference::Addend *)>
                        FindAtomBySectionAndAddress;

  /// Prototype for a helper function.  Given a symbolIndex, finds the atom
  /// representing that symbol.
  typedef std::function<llvm::Error (uint32_t symbolIndex,
                        const lld::Atom **)> FindAtomBySymbolIndex;

  /// Analyzes a relocation from a .o file and returns the info
  /// (kind, target, addend) needed to instantiate a Reference.
  /// Two helper functions are passed as parameters to find the target atom
  /// given a symbol index or address.
  virtual llvm::Error
          getReferenceInfo(const normalized::Relocation &reloc,
                           const DefinedAtom *inAtom,
                           uint32_t offsetInAtom,
                           uint64_t fixupAddress, bool isBigEndian,
                           FindAtomBySectionAndAddress atomFromAddress,
                           FindAtomBySymbolIndex atomFromSymbolIndex,
                           Reference::KindValue *kind,
                           const lld::Atom **target,
                           Reference::Addend *addend) = 0;

  /// Analyzes a pair of relocations from a .o file and returns the info
  /// (kind, target, addend) needed to instantiate a Reference.
  /// Two helper functions are passed as parameters to find the target atom
  /// given a symbol index or address.
  virtual llvm::Error
      getPairReferenceInfo(const normalized::Relocation &reloc1,
                           const normalized::Relocation &reloc2,
                           const DefinedAtom *inAtom,
                           uint32_t offsetInAtom,
                           uint64_t fixupAddress, bool isBig, bool scatterable,
                           FindAtomBySectionAndAddress atomFromAddress,
                           FindAtomBySymbolIndex atomFromSymbolIndex,
                           Reference::KindValue *kind,
                           const lld::Atom **target,
                           Reference::Addend *addend) = 0;

  /// Prototype for a helper function.  Given an atom, finds the symbol table
  /// index for it in the output file.
  typedef std::function<uint32_t (const Atom &atom)> FindSymbolIndexForAtom;

  /// Prototype for a helper function.  Given an atom, finds the index
  /// of the section that will contain the atom.
  typedef std::function<uint32_t (const Atom &atom)> FindSectionIndexForAtom;

  /// Prototype for a helper function.  Given an atom, finds the address
  /// assigned to it in the output file.
  typedef std::function<uint64_t (const Atom &atom)> FindAddressForAtom;

  /// Some architectures require local symbols on anonymous atoms.
  virtual bool needsLocalSymbolInRelocatableFile(const DefinedAtom *atom) {
    return false;
  }

  /// Copy raw content then apply all fixup References on an Atom.
  virtual void generateAtomContent(const DefinedAtom &atom, bool relocatable,
                                   FindAddressForAtom findAddress,
                                   FindAddressForAtom findSectionAddress,
                                   uint64_t imageBaseAddress,
                          llvm::MutableArrayRef<uint8_t> atomContentBuffer) = 0;

  /// Used in -r mode to convert a Reference to a mach-o relocation.
  virtual void appendSectionRelocations(const DefinedAtom &atom,
                                        uint64_t atomSectionOffset,
                                        const Reference &ref,
                                        FindSymbolIndexForAtom,
                                        FindSectionIndexForAtom,
                                        FindAddressForAtom,
                                        normalized::Relocations&) = 0;

  /// Add arch-specific References.
  virtual void addAdditionalReferences(MachODefinedAtom &atom) { }

  // Add Reference for data-in-code marker.
  virtual void addDataInCodeReference(MachODefinedAtom &atom, uint32_t atomOff,
                                      uint16_t length, uint16_t kind) { }

  /// Returns true if the specificed Reference value marks the start or end
  /// of a data-in-code range in an atom.
  virtual bool isDataInCodeTransition(Reference::KindValue refKind) {
    return false;
  }

  /// Returns the Reference value for a Reference that marks that start of
  /// a data-in-code range.
  virtual Reference::KindValue dataInCodeTransitionStart(
                                                const MachODefinedAtom &atom) {
    return 0;
  }

  /// Returns the Reference value for a Reference that marks that end of
  /// a data-in-code range.
  virtual Reference::KindValue dataInCodeTransitionEnd(
                                                const MachODefinedAtom &atom) {
    return 0;
  }

  /// Only relevant for 32-bit arm archs.
  virtual bool isThumbFunction(const DefinedAtom &atom) { return false; }

  /// Only relevant for 32-bit arm archs.
  virtual const DefinedAtom *createShim(MachOFile &file, bool thumbToArm,
                                        const DefinedAtom &) {
    llvm_unreachable("shims only support on arm");
  }

  /// Does a given unwind-cfi atom represent a CIE (as opposed to an FDE).
  static bool isDwarfCIE(bool isBig, const DefinedAtom *atom);

  struct ReferenceInfo {
    Reference::KindArch arch;
    uint16_t            kind;
    uint32_t            offset;
    int32_t             addend;
  };

  struct OptionalRefInfo {
    bool                used;
    uint16_t            kind;
    uint32_t            offset;
    int32_t             addend;
  };

  /// Table of architecture specific information for creating stubs.
  struct StubInfo {
    const char*     binderSymbolName;
    ReferenceInfo   lazyPointerReferenceToHelper;
    ReferenceInfo   lazyPointerReferenceToFinal;
    ReferenceInfo   nonLazyPointerReferenceToBinder;
    uint8_t         codeAlignment;

    uint32_t        stubSize;
    uint8_t         stubBytes[16];
    ReferenceInfo   stubReferenceToLP;
    OptionalRefInfo optStubReferenceToLP;

    uint32_t        stubHelperSize;
    uint8_t         stubHelperBytes[16];
    ReferenceInfo   stubHelperReferenceToImm;
    ReferenceInfo   stubHelperReferenceToHelperCommon;

    DefinedAtom::ContentType stubHelperImageCacheContentType;

    uint32_t        stubHelperCommonSize;
    uint8_t         stubHelperCommonAlignment;
    uint8_t         stubHelperCommonBytes[36];
    ReferenceInfo   stubHelperCommonReferenceToCache;
    OptionalRefInfo optStubHelperCommonReferenceToCache;
    ReferenceInfo   stubHelperCommonReferenceToBinder;
    OptionalRefInfo optStubHelperCommonReferenceToBinder;
  };

  virtual const StubInfo &stubInfo() = 0;

protected:
  ArchHandler();

  static std::unique_ptr<mach_o::ArchHandler> create_x86_64();
  static std::unique_ptr<mach_o::ArchHandler> create_x86();
  static std::unique_ptr<mach_o::ArchHandler> create_arm();
  static std::unique_ptr<mach_o::ArchHandler> create_arm64();

  // Handy way to pack mach-o r_type and other bit fields into one 16-bit value.
  typedef uint16_t RelocPattern;
  enum {
    rScattered = 0x8000,
    rPcRel     = 0x4000,
    rExtern    = 0x2000,
    rLength1   = 0x0000,
    rLength2   = 0x0100,
    rLength4   = 0x0200,
    rLength8   = 0x0300,
    rLenArmLo  = rLength1,
    rLenArmHi  = rLength2,
    rLenThmbLo = rLength4,
    rLenThmbHi = rLength8
  };
  /// Extract RelocPattern from normalized mach-o relocation.
  static RelocPattern relocPattern(const normalized::Relocation &reloc);
  /// Create normalized Relocation initialized from pattern.
  static normalized::Relocation relocFromPattern(RelocPattern pattern);
  /// One liner to add a relocation.
  static void appendReloc(normalized::Relocations &relocs, uint32_t offset,
                          uint32_t symbol, uint32_t value,
                          RelocPattern pattern);


  static int16_t  readS16(const uint8_t *addr, bool isBig);
  static int32_t  readS32(const uint8_t *addr, bool isBig);
  static uint32_t readU32(const uint8_t *addr, bool isBig);
  static int64_t  readS64(const uint8_t *addr, bool isBig);
};

} // namespace mach_o
} // namespace lld

#endif // LLD_READER_WRITER_MACHO_ARCH_HANDLER_H
