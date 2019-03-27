//===- lib/ReaderWriter/MachO/MachONormalizedFileYAML.cpp -----------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

///
/// \file For mach-o object files, this implementation uses YAML I/O to
/// provide the convert between YAML and the normalized mach-o (NM).
///
///                  +------------+         +------+
///                  | normalized |   <->   | yaml |
///                  +------------+         +------+

#include "MachONormalizedFile.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/Error.h"
#include "lld/ReaderWriter/YamlContext.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <system_error>

using llvm::StringRef;
using namespace llvm::yaml;
using namespace llvm::MachO;
using namespace lld::mach_o::normalized;
using lld::YamlContext;

LLVM_YAML_IS_SEQUENCE_VECTOR(Segment)
LLVM_YAML_IS_SEQUENCE_VECTOR(DependentDylib)
LLVM_YAML_IS_SEQUENCE_VECTOR(RebaseLocation)
LLVM_YAML_IS_SEQUENCE_VECTOR(BindLocation)
LLVM_YAML_IS_SEQUENCE_VECTOR(Export)
LLVM_YAML_IS_SEQUENCE_VECTOR(DataInCode)


// for compatibility with gcc-4.7 in C++11 mode, add extra namespace
namespace llvm {
namespace yaml {

// A vector of Sections is a sequence.
template<>
struct SequenceTraits< std::vector<Section> > {
  static size_t size(IO &io, std::vector<Section> &seq) {
    return seq.size();
  }
  static Section& element(IO &io, std::vector<Section> &seq, size_t index) {
    if ( index >= seq.size() )
      seq.resize(index+1);
    return seq[index];
  }
};

template<>
struct SequenceTraits< std::vector<Symbol> > {
  static size_t size(IO &io, std::vector<Symbol> &seq) {
    return seq.size();
  }
  static Symbol& element(IO &io, std::vector<Symbol> &seq, size_t index) {
    if ( index >= seq.size() )
      seq.resize(index+1);
    return seq[index];
  }
};

// A vector of Relocations is a sequence.
template<>
struct SequenceTraits< Relocations > {
  static size_t size(IO &io, Relocations &seq) {
    return seq.size();
  }
  static Relocation& element(IO &io, Relocations &seq, size_t index) {
    if ( index >= seq.size() )
      seq.resize(index+1);
    return seq[index];
  }
};

// The content for a section is represented as a flow sequence of hex bytes.
template<>
struct SequenceTraits< ContentBytes > {
  static size_t size(IO &io, ContentBytes &seq) {
    return seq.size();
  }
  static Hex8& element(IO &io, ContentBytes &seq, size_t index) {
    if ( index >= seq.size() )
      seq.resize(index+1);
    return seq[index];
  }
  static const bool flow = true;
};

// The indirect symbols for a section is represented as a flow sequence
// of numbers (symbol table indexes).
template<>
struct SequenceTraits< IndirectSymbols > {
  static size_t size(IO &io, IndirectSymbols &seq) {
    return seq.size();
  }
  static uint32_t& element(IO &io, IndirectSymbols &seq, size_t index) {
    if ( index >= seq.size() )
      seq.resize(index+1);
    return seq[index];
  }
  static const bool flow = true;
};

template <>
struct ScalarEnumerationTraits<lld::MachOLinkingContext::Arch> {
  static void enumeration(IO &io, lld::MachOLinkingContext::Arch &value) {
    io.enumCase(value, "unknown",lld::MachOLinkingContext::arch_unknown);
    io.enumCase(value, "ppc",    lld::MachOLinkingContext::arch_ppc);
    io.enumCase(value, "x86",    lld::MachOLinkingContext::arch_x86);
    io.enumCase(value, "x86_64", lld::MachOLinkingContext::arch_x86_64);
    io.enumCase(value, "armv6",  lld::MachOLinkingContext::arch_armv6);
    io.enumCase(value, "armv7",  lld::MachOLinkingContext::arch_armv7);
    io.enumCase(value, "armv7s", lld::MachOLinkingContext::arch_armv7s);
    io.enumCase(value, "arm64",  lld::MachOLinkingContext::arch_arm64);
  }
};

template <>
struct ScalarEnumerationTraits<lld::MachOLinkingContext::OS> {
  static void enumeration(IO &io, lld::MachOLinkingContext::OS &value) {
    io.enumCase(value, "unknown",
                          lld::MachOLinkingContext::OS::unknown);
    io.enumCase(value, "Mac OS X",
                          lld::MachOLinkingContext::OS::macOSX);
    io.enumCase(value, "iOS",
                          lld::MachOLinkingContext::OS::iOS);
    io.enumCase(value, "iOS Simulator",
                          lld::MachOLinkingContext::OS::iOS_simulator);
  }
};


template <>
struct ScalarEnumerationTraits<HeaderFileType> {
  static void enumeration(IO &io, HeaderFileType &value) {
    io.enumCase(value, "MH_OBJECT",   llvm::MachO::MH_OBJECT);
    io.enumCase(value, "MH_DYLIB",    llvm::MachO::MH_DYLIB);
    io.enumCase(value, "MH_EXECUTE",  llvm::MachO::MH_EXECUTE);
    io.enumCase(value, "MH_BUNDLE",   llvm::MachO::MH_BUNDLE);
  }
};


template <>
struct ScalarBitSetTraits<FileFlags> {
  static void bitset(IO &io, FileFlags &value) {
    io.bitSetCase(value, "MH_TWOLEVEL",
                          llvm::MachO::MH_TWOLEVEL);
    io.bitSetCase(value, "MH_SUBSECTIONS_VIA_SYMBOLS",
                          llvm::MachO::MH_SUBSECTIONS_VIA_SYMBOLS);
  }
};


template <>
struct ScalarEnumerationTraits<SectionType> {
  static void enumeration(IO &io, SectionType &value) {
    io.enumCase(value, "S_REGULAR",
                        llvm::MachO::S_REGULAR);
    io.enumCase(value, "S_ZEROFILL",
                        llvm::MachO::S_ZEROFILL);
    io.enumCase(value, "S_CSTRING_LITERALS",
                        llvm::MachO::S_CSTRING_LITERALS);
    io.enumCase(value, "S_4BYTE_LITERALS",
                        llvm::MachO::S_4BYTE_LITERALS);
    io.enumCase(value, "S_8BYTE_LITERALS",
                        llvm::MachO::S_8BYTE_LITERALS);
    io.enumCase(value, "S_LITERAL_POINTERS",
                        llvm::MachO::S_LITERAL_POINTERS);
    io.enumCase(value, "S_NON_LAZY_SYMBOL_POINTERS",
                        llvm::MachO::S_NON_LAZY_SYMBOL_POINTERS);
    io.enumCase(value, "S_LAZY_SYMBOL_POINTERS",
                        llvm::MachO::S_LAZY_SYMBOL_POINTERS);
    io.enumCase(value, "S_SYMBOL_STUBS",
                        llvm::MachO::S_SYMBOL_STUBS);
    io.enumCase(value, "S_MOD_INIT_FUNC_POINTERS",
                        llvm::MachO::S_MOD_INIT_FUNC_POINTERS);
    io.enumCase(value, "S_MOD_TERM_FUNC_POINTERS",
                        llvm::MachO::S_MOD_TERM_FUNC_POINTERS);
    io.enumCase(value, "S_COALESCED",
                        llvm::MachO::S_COALESCED);
    io.enumCase(value, "S_GB_ZEROFILL",
                        llvm::MachO::S_GB_ZEROFILL);
    io.enumCase(value, "S_INTERPOSING",
                        llvm::MachO::S_INTERPOSING);
    io.enumCase(value, "S_16BYTE_LITERALS",
                        llvm::MachO::S_16BYTE_LITERALS);
    io.enumCase(value, "S_DTRACE_DOF",
                        llvm::MachO::S_DTRACE_DOF);
    io.enumCase(value, "S_LAZY_DYLIB_SYMBOL_POINTERS",
                        llvm::MachO::S_LAZY_DYLIB_SYMBOL_POINTERS);
    io.enumCase(value, "S_THREAD_LOCAL_REGULAR",
                        llvm::MachO::S_THREAD_LOCAL_REGULAR);
    io.enumCase(value, "S_THREAD_LOCAL_ZEROFILL",
                        llvm::MachO::S_THREAD_LOCAL_ZEROFILL);
    io.enumCase(value, "S_THREAD_LOCAL_VARIABLES",
                        llvm::MachO::S_THREAD_LOCAL_VARIABLES);
    io.enumCase(value, "S_THREAD_LOCAL_VARIABLE_POINTERS",
                        llvm::MachO::S_THREAD_LOCAL_VARIABLE_POINTERS);
    io.enumCase(value, "S_THREAD_LOCAL_INIT_FUNCTION_POINTERS",
                        llvm::MachO::S_THREAD_LOCAL_INIT_FUNCTION_POINTERS);
  }
};

template <>
struct ScalarBitSetTraits<SectionAttr> {
  static void bitset(IO &io, SectionAttr &value) {
    io.bitSetCase(value, "S_ATTR_PURE_INSTRUCTIONS",
                          llvm::MachO::S_ATTR_PURE_INSTRUCTIONS);
    io.bitSetCase(value, "S_ATTR_SOME_INSTRUCTIONS",
                          llvm::MachO::S_ATTR_SOME_INSTRUCTIONS);
    io.bitSetCase(value, "S_ATTR_NO_DEAD_STRIP",
                          llvm::MachO::S_ATTR_NO_DEAD_STRIP);
    io.bitSetCase(value, "S_ATTR_EXT_RELOC",
                          llvm::MachO::S_ATTR_EXT_RELOC);
    io.bitSetCase(value, "S_ATTR_LOC_RELOC",
                          llvm::MachO::S_ATTR_LOC_RELOC);
    io.bitSetCase(value, "S_ATTR_DEBUG",
                         llvm::MachO::S_ATTR_DEBUG);
  }
};

/// This is a custom formatter for SectionAlignment.  Values are
/// the power to raise by, ie, the n in 2^n.
template <> struct ScalarTraits<SectionAlignment> {
  static void output(const SectionAlignment &value, void *ctxt,
                     raw_ostream &out) {
    out << llvm::format("%d", (uint32_t)value);
  }

  static StringRef input(StringRef scalar, void *ctxt,
                         SectionAlignment &value) {
    uint32_t alignment;
    if (scalar.getAsInteger(0, alignment)) {
      return "malformed alignment value";
    }
    if (!llvm::isPowerOf2_32(alignment))
      return "alignment must be a power of 2";
    value = alignment;
    return StringRef(); // returning empty string means success
  }

  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template <>
struct ScalarEnumerationTraits<NListType> {
  static void enumeration(IO &io, NListType &value) {
    io.enumCase(value, "N_UNDF",  llvm::MachO::N_UNDF);
    io.enumCase(value, "N_ABS",   llvm::MachO::N_ABS);
    io.enumCase(value, "N_SECT",  llvm::MachO::N_SECT);
    io.enumCase(value, "N_PBUD",  llvm::MachO::N_PBUD);
    io.enumCase(value, "N_INDR",  llvm::MachO::N_INDR);
  }
};

template <>
struct ScalarBitSetTraits<SymbolScope> {
  static void bitset(IO &io, SymbolScope &value) {
    io.bitSetCase(value, "N_EXT",   llvm::MachO::N_EXT);
    io.bitSetCase(value, "N_PEXT",  llvm::MachO::N_PEXT);
  }
};

template <>
struct ScalarBitSetTraits<SymbolDesc> {
  static void bitset(IO &io, SymbolDesc &value) {
    io.bitSetCase(value, "N_NO_DEAD_STRIP",   llvm::MachO::N_NO_DEAD_STRIP);
    io.bitSetCase(value, "N_WEAK_REF",        llvm::MachO::N_WEAK_REF);
    io.bitSetCase(value, "N_WEAK_DEF",        llvm::MachO::N_WEAK_DEF);
    io.bitSetCase(value, "N_ARM_THUMB_DEF",   llvm::MachO::N_ARM_THUMB_DEF);
    io.bitSetCase(value, "N_SYMBOL_RESOLVER", llvm::MachO::N_SYMBOL_RESOLVER);
  }
};


template <>
struct MappingTraits<Section> {
  struct NormalizedContentBytes;
  static void mapping(IO &io, Section &sect) {
    io.mapRequired("segment",         sect.segmentName);
    io.mapRequired("section",         sect.sectionName);
    io.mapRequired("type",            sect.type);
    io.mapOptional("attributes",      sect.attributes);
    io.mapOptional("alignment",       sect.alignment, (SectionAlignment)1);
    io.mapRequired("address",         sect.address);
    if (isZeroFillSection(sect.type)) {
      // S_ZEROFILL sections use "size:" instead of "content:"
      uint64_t size = sect.content.size();
      io.mapOptional("size",          size);
      if (!io.outputting()) {
        uint8_t *bytes = nullptr;
        sect.content = makeArrayRef(bytes, size);
      }
    } else {
      MappingNormalization<NormalizedContent, ArrayRef<uint8_t>> content(
        io, sect.content);
      io.mapOptional("content",         content->_normalizedContent);
    }
    io.mapOptional("relocations",     sect.relocations);
    io.mapOptional("indirect-syms",   sect.indirectSymbols);
  }

  struct NormalizedContent {
    NormalizedContent(IO &io) : _io(io) {}
    NormalizedContent(IO &io, ArrayRef<uint8_t> content) : _io(io) {
      // When writing yaml, copy content byte array to Hex8 vector.
      for (auto &c : content) {
        _normalizedContent.push_back(c);
      }
    }
    ArrayRef<uint8_t> denormalize(IO &io) {
      // When reading yaml, allocate byte array owned by NormalizedFile and
      // copy Hex8 vector to byte array.
      YamlContext *info = reinterpret_cast<YamlContext *>(io.getContext());
      assert(info != nullptr);
      NormalizedFile *file = info->_normalizeMachOFile;
      assert(file != nullptr);
      size_t size = _normalizedContent.size();
      if (!size)
        return None;
      uint8_t *bytes = file->ownedAllocations.Allocate<uint8_t>(size);
      std::copy(_normalizedContent.begin(), _normalizedContent.end(), bytes);
      return makeArrayRef(bytes, size);
    }

    IO                &_io;
    ContentBytes       _normalizedContent;
  };
};


template <>
struct MappingTraits<Relocation> {
  static void mapping(IO &io, Relocation &reloc) {
    io.mapRequired("offset",    reloc.offset);
    io.mapOptional("scattered", reloc.scattered, false);
    io.mapRequired("type",      reloc.type);
    io.mapRequired("length",    reloc.length);
    io.mapRequired("pc-rel",    reloc.pcRel);
    if ( !reloc.scattered )
     io.mapRequired("extern",   reloc.isExtern);
    if ( reloc.scattered )
     io.mapRequired("value",    reloc.value);
    if ( !reloc.scattered )
     io.mapRequired("symbol",   reloc.symbol);
  }
};


template <>
struct ScalarEnumerationTraits<RelocationInfoType> {
  static void enumeration(IO &io, RelocationInfoType &value) {
    YamlContext *info = reinterpret_cast<YamlContext *>(io.getContext());
    assert(info != nullptr);
    NormalizedFile *file = info->_normalizeMachOFile;
    assert(file != nullptr);
    switch (file->arch) {
    case lld::MachOLinkingContext::arch_x86_64:
      io.enumCase(value, "X86_64_RELOC_UNSIGNED",
                                  llvm::MachO::X86_64_RELOC_UNSIGNED);
      io.enumCase(value, "X86_64_RELOC_SIGNED",
                                  llvm::MachO::X86_64_RELOC_SIGNED);
      io.enumCase(value, "X86_64_RELOC_BRANCH",
                                  llvm::MachO::X86_64_RELOC_BRANCH);
      io.enumCase(value, "X86_64_RELOC_GOT_LOAD",
                                  llvm::MachO::X86_64_RELOC_GOT_LOAD);
      io.enumCase(value, "X86_64_RELOC_GOT",
                                  llvm::MachO::X86_64_RELOC_GOT);
      io.enumCase(value, "X86_64_RELOC_SUBTRACTOR",
                                  llvm::MachO::X86_64_RELOC_SUBTRACTOR);
      io.enumCase(value, "X86_64_RELOC_SIGNED_1",
                                  llvm::MachO::X86_64_RELOC_SIGNED_1);
      io.enumCase(value, "X86_64_RELOC_SIGNED_2",
                                  llvm::MachO::X86_64_RELOC_SIGNED_2);
      io.enumCase(value, "X86_64_RELOC_SIGNED_4",
                                  llvm::MachO::X86_64_RELOC_SIGNED_4);
      io.enumCase(value, "X86_64_RELOC_TLV",
                                  llvm::MachO::X86_64_RELOC_TLV);
      break;
    case lld::MachOLinkingContext::arch_x86:
      io.enumCase(value, "GENERIC_RELOC_VANILLA",
                                  llvm::MachO::GENERIC_RELOC_VANILLA);
      io.enumCase(value, "GENERIC_RELOC_PAIR",
                                  llvm::MachO::GENERIC_RELOC_PAIR);
      io.enumCase(value, "GENERIC_RELOC_SECTDIFF",
                                  llvm::MachO::GENERIC_RELOC_SECTDIFF);
      io.enumCase(value, "GENERIC_RELOC_LOCAL_SECTDIFF",
                                  llvm::MachO::GENERIC_RELOC_LOCAL_SECTDIFF);
      io.enumCase(value, "GENERIC_RELOC_TLV",
                                  llvm::MachO::GENERIC_RELOC_TLV);
      break;
    case lld::MachOLinkingContext::arch_armv6:
    case lld::MachOLinkingContext::arch_armv7:
    case lld::MachOLinkingContext::arch_armv7s:
       io.enumCase(value, "ARM_RELOC_VANILLA",
                                  llvm::MachO::ARM_RELOC_VANILLA);
      io.enumCase(value, "ARM_RELOC_PAIR",
                                  llvm::MachO::ARM_RELOC_PAIR);
      io.enumCase(value, "ARM_RELOC_SECTDIFF",
                                  llvm::MachO::ARM_RELOC_SECTDIFF);
      io.enumCase(value, "ARM_RELOC_LOCAL_SECTDIFF",
                                  llvm::MachO::ARM_RELOC_LOCAL_SECTDIFF);
      io.enumCase(value, "ARM_RELOC_BR24",
                                  llvm::MachO::ARM_RELOC_BR24);
      io.enumCase(value, "ARM_THUMB_RELOC_BR22",
                                  llvm::MachO::ARM_THUMB_RELOC_BR22);
      io.enumCase(value, "ARM_RELOC_HALF",
                                  llvm::MachO::ARM_RELOC_HALF);
      io.enumCase(value, "ARM_RELOC_HALF_SECTDIFF",
                                  llvm::MachO::ARM_RELOC_HALF_SECTDIFF);
      break;
    case lld::MachOLinkingContext::arch_arm64:
      io.enumCase(value, "ARM64_RELOC_UNSIGNED",
                                  llvm::MachO::ARM64_RELOC_UNSIGNED);
      io.enumCase(value, "ARM64_RELOC_SUBTRACTOR",
                                  llvm::MachO::ARM64_RELOC_SUBTRACTOR);
      io.enumCase(value, "ARM64_RELOC_BRANCH26",
                                  llvm::MachO::ARM64_RELOC_BRANCH26);
      io.enumCase(value, "ARM64_RELOC_PAGE21",
                                  llvm::MachO::ARM64_RELOC_PAGE21);
      io.enumCase(value, "ARM64_RELOC_PAGEOFF12",
                                  llvm::MachO::ARM64_RELOC_PAGEOFF12);
      io.enumCase(value, "ARM64_RELOC_GOT_LOAD_PAGE21",
                                  llvm::MachO::ARM64_RELOC_GOT_LOAD_PAGE21);
      io.enumCase(value, "ARM64_RELOC_GOT_LOAD_PAGEOFF12",
                                  llvm::MachO::ARM64_RELOC_GOT_LOAD_PAGEOFF12);
      io.enumCase(value, "ARM64_RELOC_POINTER_TO_GOT",
                                  llvm::MachO::ARM64_RELOC_POINTER_TO_GOT);
      io.enumCase(value, "ARM64_RELOC_TLVP_LOAD_PAGE21",
                                  llvm::MachO::ARM64_RELOC_TLVP_LOAD_PAGE21);
      io.enumCase(value, "ARM64_RELOC_TLVP_LOAD_PAGEOFF12",
                                  llvm::MachO::ARM64_RELOC_TLVP_LOAD_PAGEOFF12);
      io.enumCase(value, "ARM64_RELOC_ADDEND",
                                  llvm::MachO::ARM64_RELOC_ADDEND);
      break;
    default:
      llvm_unreachable("unknown architecture");
    }
 }
};


template <>
struct MappingTraits<Symbol> {
  static void mapping(IO &io, Symbol& sym) {
    io.mapRequired("name",    sym.name);
    io.mapRequired("type",    sym.type);
    io.mapOptional("scope",   sym.scope, SymbolScope(0));
    io.mapOptional("sect",    sym.sect, (uint8_t)0);
    if (sym.type == llvm::MachO::N_UNDF) {
      // In undef symbols, desc field contains alignment/ordinal info
      // which is better represented as a hex vaule.
      uint16_t t1 = sym.desc;
      Hex16 t2 = t1;
      io.mapOptional("desc",  t2, Hex16(0));
      sym.desc = t2;
    } else {
      // In defined symbols, desc fit is a set of option bits.
      io.mapOptional("desc",    sym.desc, SymbolDesc(0));
    }
    io.mapRequired("value",  sym.value);
  }
};

// Custom mapping for VMProtect (e.g. "r-x").
template <>
struct ScalarTraits<VMProtect> {
  static void output(const VMProtect &value, void*, raw_ostream &out) {
    out << ( (value & llvm::MachO::VM_PROT_READ)    ? 'r' : '-');
    out << ( (value & llvm::MachO::VM_PROT_WRITE)   ? 'w' : '-');
    out << ( (value & llvm::MachO::VM_PROT_EXECUTE) ? 'x' : '-');
  }
  static StringRef input(StringRef scalar, void*, VMProtect &value) {
    value = 0;
    if (scalar.size() != 3)
      return "segment access protection must be three chars (e.g. \"r-x\")";
    switch (scalar[0]) {
    case 'r':
      value = llvm::MachO::VM_PROT_READ;
      break;
    case '-':
      break;
    default:
      return "segment access protection first char must be 'r' or '-'";
    }
    switch (scalar[1]) {
    case 'w':
      value = value | llvm::MachO::VM_PROT_WRITE;
      break;
    case '-':
      break;
    default:
      return "segment access protection second char must be 'w' or '-'";
    }
    switch (scalar[2]) {
    case 'x':
      value = value | llvm::MachO::VM_PROT_EXECUTE;
      break;
    case '-':
      break;
    default:
      return "segment access protection third char must be 'x' or '-'";
    }
    // Return the empty string on success,
    return StringRef();
  }
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};


template <>
struct MappingTraits<Segment> {
  static void mapping(IO &io, Segment& seg) {
    io.mapRequired("name",            seg.name);
    io.mapRequired("address",         seg.address);
    io.mapRequired("size",            seg.size);
    io.mapRequired("init-access",     seg.init_access);
    io.mapRequired("max-access",      seg.max_access);
  }
};

template <>
struct ScalarEnumerationTraits<LoadCommandType> {
  static void enumeration(IO &io, LoadCommandType &value) {
    io.enumCase(value, "LC_LOAD_DYLIB",
                        llvm::MachO::LC_LOAD_DYLIB);
    io.enumCase(value, "LC_LOAD_WEAK_DYLIB",
                        llvm::MachO::LC_LOAD_WEAK_DYLIB);
    io.enumCase(value, "LC_REEXPORT_DYLIB",
                        llvm::MachO::LC_REEXPORT_DYLIB);
    io.enumCase(value, "LC_LOAD_UPWARD_DYLIB",
                        llvm::MachO::LC_LOAD_UPWARD_DYLIB);
    io.enumCase(value, "LC_LAZY_LOAD_DYLIB",
                        llvm::MachO::LC_LAZY_LOAD_DYLIB);
    io.enumCase(value, "LC_VERSION_MIN_MACOSX",
                        llvm::MachO::LC_VERSION_MIN_MACOSX);
    io.enumCase(value, "LC_VERSION_MIN_IPHONEOS",
                        llvm::MachO::LC_VERSION_MIN_IPHONEOS);
    io.enumCase(value, "LC_VERSION_MIN_TVOS",
                        llvm::MachO::LC_VERSION_MIN_TVOS);
    io.enumCase(value, "LC_VERSION_MIN_WATCHOS",
                        llvm::MachO::LC_VERSION_MIN_WATCHOS);
  }
};

template <>
struct MappingTraits<DependentDylib> {
  static void mapping(IO &io, DependentDylib& dylib) {
    io.mapRequired("path",            dylib.path);
    io.mapOptional("kind",            dylib.kind,
                                      llvm::MachO::LC_LOAD_DYLIB);
    io.mapOptional("compat-version",  dylib.compatVersion,
                                      PackedVersion(0x10000));
    io.mapOptional("current-version", dylib.currentVersion,
                                      PackedVersion(0x10000));
  }
};

template <>
struct ScalarEnumerationTraits<RebaseType> {
  static void enumeration(IO &io, RebaseType &value) {
    io.enumCase(value, "REBASE_TYPE_POINTER",
                        llvm::MachO::REBASE_TYPE_POINTER);
    io.enumCase(value, "REBASE_TYPE_TEXT_PCREL32",
                        llvm::MachO::REBASE_TYPE_TEXT_PCREL32);
    io.enumCase(value, "REBASE_TYPE_TEXT_ABSOLUTE32",
                        llvm::MachO::REBASE_TYPE_TEXT_ABSOLUTE32);
  }
};


template <>
struct MappingTraits<RebaseLocation> {
  static void mapping(IO &io, RebaseLocation& rebase) {
    io.mapRequired("segment-index",   rebase.segIndex);
    io.mapRequired("segment-offset",  rebase.segOffset);
    io.mapOptional("kind",            rebase.kind,
                                      llvm::MachO::REBASE_TYPE_POINTER);
  }
};



template <>
struct ScalarEnumerationTraits<BindType> {
  static void enumeration(IO &io, BindType &value) {
    io.enumCase(value, "BIND_TYPE_POINTER",
                        llvm::MachO::BIND_TYPE_POINTER);
    io.enumCase(value, "BIND_TYPE_TEXT_ABSOLUTE32",
                        llvm::MachO::BIND_TYPE_TEXT_ABSOLUTE32);
    io.enumCase(value, "BIND_TYPE_TEXT_PCREL32",
                        llvm::MachO::BIND_TYPE_TEXT_PCREL32);
  }
};

template <>
struct MappingTraits<BindLocation> {
  static void mapping(IO &io, BindLocation &bind) {
    io.mapRequired("segment-index",   bind.segIndex);
    io.mapRequired("segment-offset",  bind.segOffset);
    io.mapOptional("kind",            bind.kind,
                                      llvm::MachO::BIND_TYPE_POINTER);
    io.mapOptional("can-be-null",     bind.canBeNull, false);
    io.mapRequired("ordinal",         bind.ordinal);
    io.mapRequired("symbol-name",     bind.symbolName);
    io.mapOptional("addend",          bind.addend, Hex64(0));
  }
};


template <>
struct ScalarEnumerationTraits<ExportSymbolKind> {
  static void enumeration(IO &io, ExportSymbolKind &value) {
    io.enumCase(value, "EXPORT_SYMBOL_FLAGS_KIND_REGULAR",
                        llvm::MachO::EXPORT_SYMBOL_FLAGS_KIND_REGULAR);
    io.enumCase(value, "EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL",
                        llvm::MachO::EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL);
    io.enumCase(value, "EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE",
                        llvm::MachO::EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE);
  }
};

template <>
struct ScalarBitSetTraits<ExportFlags> {
  static void bitset(IO &io, ExportFlags &value) {
    io.bitSetCase(value, "EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION",
                          llvm::MachO::EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION);
    io.bitSetCase(value, "EXPORT_SYMBOL_FLAGS_REEXPORT",
                          llvm::MachO::EXPORT_SYMBOL_FLAGS_REEXPORT);
    io.bitSetCase(value, "EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER",
                          llvm::MachO::EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER);
  }
};


template <>
struct MappingTraits<Export> {
  static void mapping(IO &io, Export &exp) {
    io.mapRequired("name",         exp.name);
    io.mapOptional("offset",       exp.offset);
    io.mapOptional("kind",         exp.kind,
                                llvm::MachO::EXPORT_SYMBOL_FLAGS_KIND_REGULAR);
    if (!io.outputting() || exp.flags)
      io.mapOptional("flags",      exp.flags);
    io.mapOptional("other",        exp.otherOffset, Hex32(0));
    io.mapOptional("other-name",   exp.otherName, StringRef());
  }
};

template <>
struct ScalarEnumerationTraits<DataRegionType> {
  static void enumeration(IO &io, DataRegionType &value) {
    io.enumCase(value, "DICE_KIND_DATA",
                        llvm::MachO::DICE_KIND_DATA);
    io.enumCase(value, "DICE_KIND_JUMP_TABLE8",
                        llvm::MachO::DICE_KIND_JUMP_TABLE8);
    io.enumCase(value, "DICE_KIND_JUMP_TABLE16",
                        llvm::MachO::DICE_KIND_JUMP_TABLE16);
    io.enumCase(value, "DICE_KIND_JUMP_TABLE32",
                        llvm::MachO::DICE_KIND_JUMP_TABLE32);
    io.enumCase(value, "DICE_KIND_ABS_JUMP_TABLE32",
                        llvm::MachO::DICE_KIND_ABS_JUMP_TABLE32);
  }
};

template <>
struct MappingTraits<DataInCode> {
  static void mapping(IO &io, DataInCode &entry) {
    io.mapRequired("offset",       entry.offset);
    io.mapRequired("length",       entry.length);
    io.mapRequired("kind",         entry.kind);
  }
};

template <>
struct ScalarTraits<PackedVersion> {
  static void output(const PackedVersion &value, void*, raw_ostream &out) {
    out << llvm::format("%d.%d", (value >> 16), (value >> 8) & 0xFF);
    if (value & 0xFF) {
      out << llvm::format(".%d", (value & 0xFF));
    }
  }
  static StringRef input(StringRef scalar, void*, PackedVersion &result) {
    uint32_t value;
    if (lld::MachOLinkingContext::parsePackedVersion(scalar, value))
      return "malformed version number";
    result = value;
    // Return the empty string on success,
    return StringRef();
  }
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template <>
struct MappingTraits<NormalizedFile> {
  static void mapping(IO &io, NormalizedFile &file) {
    io.mapRequired("arch",             file.arch);
    io.mapRequired("file-type",        file.fileType);
    io.mapOptional("flags",            file.flags);
    io.mapOptional("dependents",       file.dependentDylibs);
    io.mapOptional("install-name",     file.installName,    StringRef());
    io.mapOptional("compat-version",   file.compatVersion,  PackedVersion(0x10000));
    io.mapOptional("current-version",  file.currentVersion, PackedVersion(0x10000));
    io.mapOptional("has-UUID",         file.hasUUID,        true);
    io.mapOptional("rpaths",           file.rpaths);
    io.mapOptional("entry-point",      file.entryAddress,   Hex64(0));
    io.mapOptional("stack-size",       file.stackSize,      Hex64(0));
    io.mapOptional("source-version",   file.sourceVersion,  Hex64(0));
    io.mapOptional("OS",               file.os);
    io.mapOptional("min-os-version",   file.minOSverson,    PackedVersion(0));
    io.mapOptional("min-os-version-kind",   file.minOSVersionKind, (LoadCommandType)0);
    io.mapOptional("sdk-version",      file.sdkVersion,     PackedVersion(0));
    io.mapOptional("segments",         file.segments);
    io.mapOptional("sections",         file.sections);
    io.mapOptional("local-symbols",    file.localSymbols);
    io.mapOptional("global-symbols",   file.globalSymbols);
    io.mapOptional("undefined-symbols",file.undefinedSymbols);
    io.mapOptional("page-size",        file.pageSize,       Hex32(4096));
    io.mapOptional("rebasings",        file.rebasingInfo);
    io.mapOptional("bindings",         file.bindingInfo);
    io.mapOptional("weak-bindings",    file.weakBindingInfo);
    io.mapOptional("lazy-bindings",    file.lazyBindingInfo);
    io.mapOptional("exports",          file.exportInfo);
    io.mapOptional("dataInCode",       file.dataInCode);
  }
  static StringRef validate(IO &io, NormalizedFile &file) {
    return StringRef();
  }
};

} // namespace llvm
} // namespace yaml


namespace lld {
namespace mach_o {

/// Handles !mach-o tagged yaml documents.
bool MachOYamlIOTaggedDocumentHandler::handledDocTag(llvm::yaml::IO &io,
                                                 const lld::File *&file) const {
  if (!io.mapTag("!mach-o"))
    return false;
  // Step 1: parse yaml into normalized mach-o struct.
  NormalizedFile nf;
  YamlContext *info = reinterpret_cast<YamlContext *>(io.getContext());
  assert(info != nullptr);
  assert(info->_normalizeMachOFile == nullptr);
  info->_normalizeMachOFile = &nf;
  MappingTraits<NormalizedFile>::mapping(io, nf);
  // Step 2: parse normalized mach-o struct into atoms.
  auto fileOrError = normalizedToAtoms(nf, info->_path, true);

  // Check that we parsed successfully.
  if (!fileOrError) {
    std::string buffer;
    llvm::raw_string_ostream stream(buffer);
    handleAllErrors(fileOrError.takeError(),
                    [&](const llvm::ErrorInfoBase &EI) {
      EI.log(stream);
      stream << "\n";
    });
    io.setError(stream.str());
    return false;
  }

  if (nf.arch != _arch) {
    io.setError(Twine("file is wrong architecture. Expected ("
                      + MachOLinkingContext::nameFromArch(_arch)
                      + ") found ("
                      + MachOLinkingContext::nameFromArch(nf.arch)
                      + ")"));
    return false;
  }
  info->_normalizeMachOFile = nullptr;
  file = fileOrError->release();
  return true;
}



namespace normalized {

/// Parses a yaml encoded mach-o file to produce an in-memory normalized view.
llvm::Expected<std::unique_ptr<NormalizedFile>>
readYaml(std::unique_ptr<MemoryBuffer> &mb) {
  // Make empty NormalizedFile.
  std::unique_ptr<NormalizedFile> f(new NormalizedFile());

  // Create YAML Input parser.
  YamlContext yamlContext;
  yamlContext._normalizeMachOFile = f.get();
  llvm::yaml::Input yin(mb->getBuffer(), &yamlContext);

  // Fill NormalizedFile by parsing yaml.
  yin >> *f;

  // Return error if there were parsing problems.
  if (auto ec = yin.error())
    return llvm::make_error<GenericError>(Twine("YAML parsing error: ")
                                          + ec.message());

  // Hand ownership of instantiated NormalizedFile to caller.
  return std::move(f);
}


/// Writes a yaml encoded mach-o files from an in-memory normalized view.
std::error_code writeYaml(const NormalizedFile &file, raw_ostream &out) {
  // YAML I/O is not const aware, so need to cast away ;-(
  NormalizedFile *f = const_cast<NormalizedFile*>(&file);

  // Create yaml Output writer, using yaml options for context.
  YamlContext yamlContext;
  yamlContext._normalizeMachOFile = f;
  llvm::yaml::Output yout(out, &yamlContext);

  // Stream out yaml.
  yout << *f;

  return std::error_code();
}

} // namespace normalized
} // namespace mach_o
} // namespace lld
