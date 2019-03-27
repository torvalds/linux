//===- lib/ReaderWriter/MachO/MachONormalizedFileBinaryWriter.cpp ---------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

///
/// \file For mach-o object files, this implementation converts normalized
/// mach-o in memory to mach-o binary on disk.
///
///                 +---------------+
///                 | binary mach-o |
///                 +---------------+
///                        ^
///                        |
///                        |
///                  +------------+
///                  | normalized |
///                  +------------+

#include "MachONormalizedFile.h"
#include "MachONormalizedFileBinaryUtils.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/Error.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <list>
#include <map>
#include <system_error>

using namespace llvm::MachO;

namespace lld {
namespace mach_o {
namespace normalized {

struct TrieNode; // Forward declaration.

struct TrieEdge : public llvm::ilist_node<TrieEdge> {
  TrieEdge(StringRef s, TrieNode *node) : _subString(s), _child(node) {}

  StringRef          _subString;
  struct TrieNode   *_child;
};

} // namespace normalized
} // namespace mach_o
} // namespace lld


namespace llvm {
using lld::mach_o::normalized::TrieEdge;
template <>
struct ilist_alloc_traits<TrieEdge> : ilist_noalloc_traits<TrieEdge> {};
} // namespace llvm


namespace lld {
namespace mach_o {
namespace normalized {

struct TrieNode {
  typedef llvm::ilist<TrieEdge> TrieEdgeList;

  TrieNode(StringRef s)
      : _cummulativeString(s), _address(0), _flags(0), _other(0),
        _trieOffset(0), _hasExportInfo(false) {}
  ~TrieNode() = default;

  void addSymbol(const Export &entry, BumpPtrAllocator &allocator,
                 std::vector<TrieNode *> &allNodes);

  void addOrderedNodes(const Export &entry,
                       std::vector<TrieNode *> &allNodes);
  bool updateOffset(uint32_t &offset);
  void appendToByteBuffer(ByteBuffer &out);

private:
  StringRef                 _cummulativeString;
  TrieEdgeList              _children;
  uint64_t                  _address;
  uint64_t                  _flags;
  uint64_t                  _other;
  StringRef                 _importedName;
  uint32_t                  _trieOffset;
  bool                      _hasExportInfo;
  bool                      _ordered = false;
};

/// Utility class for writing a mach-o binary file given an in-memory
/// normalized file.
class MachOFileLayout {
public:
  /// All layout computation is done in the constructor.
  MachOFileLayout(const NormalizedFile &file);

  /// Returns the final file size as computed in the constructor.
  size_t      size() const;

  // Returns size of the mach_header and load commands.
  size_t      headerAndLoadCommandsSize() const;

  /// Writes the normalized file as a binary mach-o file to the specified
  /// path.  This does not have a stream interface because the generated
  /// file may need the 'x' bit set.
  llvm::Error writeBinary(StringRef path);

private:
  uint32_t    loadCommandsSize(uint32_t &count);
  void        buildFileOffsets();
  void        writeMachHeader();
  llvm::Error writeLoadCommands();
  void        writeSectionContent();
  void        writeRelocations();
  void        writeSymbolTable();
  void        writeRebaseInfo();
  void        writeBindingInfo();
  void        writeLazyBindingInfo();
  void        writeExportInfo();
  void        writeFunctionStartsInfo();
  void        writeDataInCodeInfo();
  void        writeLinkEditContent();
  void        buildLinkEditInfo();
  void        buildRebaseInfo();
  void        buildBindInfo();
  void        buildLazyBindInfo();
  void        buildExportTrie();
  void        computeFunctionStartsSize();
  void        computeDataInCodeSize();
  void        computeSymbolTableSizes();
  void        buildSectionRelocations();
  void        appendSymbols(const std::vector<Symbol> &symbols,
                                      uint32_t &symOffset, uint32_t &strOffset);
  uint32_t    indirectSymbolIndex(const Section &sect, uint32_t &index);
  uint32_t    indirectSymbolElementSize(const Section &sect);

  // For use as template parameter to load command methods.
  struct MachO64Trait {
    typedef llvm::MachO::segment_command_64 command;
    typedef llvm::MachO::section_64         section;
    enum { LC = llvm::MachO::LC_SEGMENT_64 };
  };

  // For use as template parameter to load command methods.
  struct MachO32Trait {
    typedef llvm::MachO::segment_command   command;
    typedef llvm::MachO::section           section;
    enum { LC = llvm::MachO::LC_SEGMENT };
  };

  template <typename T>
  llvm::Error writeSingleSegmentLoadCommand(uint8_t *&lc);
  template <typename T> llvm::Error writeSegmentLoadCommands(uint8_t *&lc);

  uint32_t pointerAlign(uint32_t value);
  static StringRef dyldPath();

  struct SegExtraInfo {
    uint32_t                    fileOffset;
    uint32_t                    fileSize;
    std::vector<const Section*> sections;
  };
  typedef std::map<const Segment*, SegExtraInfo> SegMap;
  struct SectionExtraInfo {
    uint32_t                    fileOffset;
  };
  typedef std::map<const Section*, SectionExtraInfo> SectionMap;

  const NormalizedFile &_file;
  std::error_code _ec;
  uint8_t              *_buffer;
  const bool            _is64;
  const bool            _swap;
  const bool            _bigEndianArch;
  uint64_t              _seg1addr;
  uint32_t              _startOfLoadCommands;
  uint32_t              _countOfLoadCommands;
  uint32_t              _endOfLoadCommands;
  uint32_t              _startOfRelocations;
  uint32_t              _startOfFunctionStarts;
  uint32_t              _startOfDataInCode;
  uint32_t              _startOfSymbols;
  uint32_t              _startOfIndirectSymbols;
  uint32_t              _startOfSymbolStrings;
  uint32_t              _endOfSymbolStrings;
  uint32_t              _symbolTableLocalsStartIndex;
  uint32_t              _symbolTableGlobalsStartIndex;
  uint32_t              _symbolTableUndefinesStartIndex;
  uint32_t              _symbolStringPoolSize;
  uint32_t              _symbolTableSize;
  uint32_t              _functionStartsSize;
  uint32_t              _dataInCodeSize;
  uint32_t              _indirectSymbolTableCount;
  // Used in object file creation only
  uint32_t              _startOfSectionsContent;
  uint32_t              _endOfSectionsContent;
  // Used in final linked image only
  uint32_t              _startOfLinkEdit;
  uint32_t              _startOfRebaseInfo;
  uint32_t              _endOfRebaseInfo;
  uint32_t              _startOfBindingInfo;
  uint32_t              _endOfBindingInfo;
  uint32_t              _startOfLazyBindingInfo;
  uint32_t              _endOfLazyBindingInfo;
  uint32_t              _startOfExportTrie;
  uint32_t              _endOfExportTrie;
  uint32_t              _endOfLinkEdit;
  uint64_t              _addressOfLinkEdit;
  SegMap                _segInfo;
  SectionMap            _sectInfo;
  ByteBuffer            _rebaseInfo;
  ByteBuffer            _bindingInfo;
  ByteBuffer            _lazyBindingInfo;
  ByteBuffer            _weakBindingInfo;
  ByteBuffer            _exportTrie;
};

size_t headerAndLoadCommandsSize(const NormalizedFile &file) {
  MachOFileLayout layout(file);
  return layout.headerAndLoadCommandsSize();
}

StringRef MachOFileLayout::dyldPath() {
  return "/usr/lib/dyld";
}

uint32_t MachOFileLayout::pointerAlign(uint32_t value) {
  return llvm::alignTo(value, _is64 ? 8 : 4);
}


size_t MachOFileLayout::headerAndLoadCommandsSize() const {
  return _endOfLoadCommands;
}

MachOFileLayout::MachOFileLayout(const NormalizedFile &file)
    : _file(file),
      _is64(MachOLinkingContext::is64Bit(file.arch)),
      _swap(!MachOLinkingContext::isHostEndian(file.arch)),
      _bigEndianArch(MachOLinkingContext::isBigEndian(file.arch)),
      _seg1addr(INT64_MAX) {
  _startOfLoadCommands = _is64 ? sizeof(mach_header_64) : sizeof(mach_header);
  const size_t segCommandBaseSize =
          (_is64 ? sizeof(segment_command_64) : sizeof(segment_command));
  const size_t sectsSize = (_is64 ? sizeof(section_64) : sizeof(section));
  if (file.fileType == llvm::MachO::MH_OBJECT) {
    // object files have just one segment load command containing all sections
    _endOfLoadCommands = _startOfLoadCommands
                               + segCommandBaseSize
                               + file.sections.size() * sectsSize
                               + sizeof(symtab_command);
    _countOfLoadCommands = 2;
    if (file.hasMinVersionLoadCommand) {
      _endOfLoadCommands += sizeof(version_min_command);
      _countOfLoadCommands++;
    }
    if (!_file.functionStarts.empty()) {
      _endOfLoadCommands += sizeof(linkedit_data_command);
      _countOfLoadCommands++;
    }
    if (_file.generateDataInCodeLoadCommand) {
      _endOfLoadCommands += sizeof(linkedit_data_command);
      _countOfLoadCommands++;
    }
    // Assign file offsets to each section.
    _startOfSectionsContent = _endOfLoadCommands;
    unsigned relocCount = 0;
    uint64_t offset = _startOfSectionsContent;
    for (const Section &sect : file.sections) {
      if (isZeroFillSection(sect.type))
        _sectInfo[&sect].fileOffset = 0;
      else {
        offset = llvm::alignTo(offset, sect.alignment);
        _sectInfo[&sect].fileOffset = offset;
        offset += sect.content.size();
      }
      relocCount += sect.relocations.size();
    }
    _endOfSectionsContent = offset;

    computeSymbolTableSizes();
    computeFunctionStartsSize();
    computeDataInCodeSize();

    // Align start of relocations.
    _startOfRelocations = pointerAlign(_endOfSectionsContent);
    _startOfFunctionStarts = _startOfRelocations + relocCount * 8;
    _startOfDataInCode = _startOfFunctionStarts + _functionStartsSize;
    _startOfSymbols = _startOfDataInCode + _dataInCodeSize;
    // Add Indirect symbol table.
    _startOfIndirectSymbols = _startOfSymbols + _symbolTableSize;
    // Align start of symbol table and symbol strings.
    _startOfSymbolStrings = _startOfIndirectSymbols
                  + pointerAlign(_indirectSymbolTableCount * sizeof(uint32_t));
    _endOfSymbolStrings = _startOfSymbolStrings
                          + pointerAlign(_symbolStringPoolSize);
    _endOfLinkEdit = _endOfSymbolStrings;
    DEBUG_WITH_TYPE("MachOFileLayout",
                  llvm::dbgs() << "MachOFileLayout()\n"
      << "  startOfLoadCommands=" << _startOfLoadCommands << "\n"
      << "  countOfLoadCommands=" << _countOfLoadCommands << "\n"
      << "  endOfLoadCommands=" << _endOfLoadCommands << "\n"
      << "  startOfRelocations=" << _startOfRelocations << "\n"
      << "  startOfSymbols=" << _startOfSymbols << "\n"
      << "  startOfSymbolStrings=" << _startOfSymbolStrings << "\n"
      << "  endOfSymbolStrings=" << _endOfSymbolStrings << "\n"
      << "  startOfSectionsContent=" << _startOfSectionsContent << "\n"
      << "  endOfSectionsContent=" << _endOfSectionsContent << "\n");
  } else {
    // Final linked images have one load command per segment.
    _endOfLoadCommands = _startOfLoadCommands
                          + loadCommandsSize(_countOfLoadCommands);

    // Assign section file offsets.
    buildFileOffsets();
    buildLinkEditInfo();

    // LINKEDIT of final linked images has in order:
    // rebase info, binding info, lazy binding info, weak binding info,
    // data-in-code, symbol table, indirect symbol table, symbol table strings.
    _startOfRebaseInfo = _startOfLinkEdit;
    _endOfRebaseInfo = _startOfRebaseInfo + _rebaseInfo.size();
    _startOfBindingInfo = _endOfRebaseInfo;
    _endOfBindingInfo = _startOfBindingInfo + _bindingInfo.size();
    _startOfLazyBindingInfo = _endOfBindingInfo;
    _endOfLazyBindingInfo = _startOfLazyBindingInfo + _lazyBindingInfo.size();
    _startOfExportTrie = _endOfLazyBindingInfo;
    _endOfExportTrie = _startOfExportTrie + _exportTrie.size();
    _startOfFunctionStarts = _endOfExportTrie;
    _startOfDataInCode = _startOfFunctionStarts + _functionStartsSize;
    _startOfSymbols = _startOfDataInCode + _dataInCodeSize;
    _startOfIndirectSymbols = _startOfSymbols + _symbolTableSize;
    _startOfSymbolStrings = _startOfIndirectSymbols
                  + pointerAlign(_indirectSymbolTableCount * sizeof(uint32_t));
    _endOfSymbolStrings = _startOfSymbolStrings
                          + pointerAlign(_symbolStringPoolSize);
    _endOfLinkEdit = _endOfSymbolStrings;
    DEBUG_WITH_TYPE("MachOFileLayout",
                  llvm::dbgs() << "MachOFileLayout()\n"
      << "  startOfLoadCommands=" << _startOfLoadCommands << "\n"
      << "  countOfLoadCommands=" << _countOfLoadCommands << "\n"
      << "  endOfLoadCommands=" << _endOfLoadCommands << "\n"
      << "  startOfLinkEdit=" << _startOfLinkEdit << "\n"
      << "  startOfRebaseInfo=" << _startOfRebaseInfo << "\n"
      << "  endOfRebaseInfo=" << _endOfRebaseInfo << "\n"
      << "  startOfBindingInfo=" << _startOfBindingInfo << "\n"
      << "  endOfBindingInfo=" << _endOfBindingInfo << "\n"
      << "  startOfLazyBindingInfo=" << _startOfLazyBindingInfo << "\n"
      << "  endOfLazyBindingInfo=" << _endOfLazyBindingInfo << "\n"
      << "  startOfExportTrie=" << _startOfExportTrie << "\n"
      << "  endOfExportTrie=" << _endOfExportTrie << "\n"
      << "  startOfFunctionStarts=" << _startOfFunctionStarts << "\n"
      << "  startOfDataInCode=" << _startOfDataInCode << "\n"
      << "  startOfSymbols=" << _startOfSymbols << "\n"
      << "  startOfSymbolStrings=" << _startOfSymbolStrings << "\n"
      << "  endOfSymbolStrings=" << _endOfSymbolStrings << "\n"
      << "  addressOfLinkEdit=" << _addressOfLinkEdit << "\n");
  }
}

uint32_t MachOFileLayout::loadCommandsSize(uint32_t &count) {
  uint32_t size = 0;
  count = 0;

  const size_t segCommandSize =
          (_is64 ? sizeof(segment_command_64) : sizeof(segment_command));
  const size_t sectionSize = (_is64 ? sizeof(section_64) : sizeof(section));

  // Add LC_SEGMENT for each segment.
  size += _file.segments.size() * segCommandSize;
  count += _file.segments.size();
  // Add section record for each section.
  size += _file.sections.size() * sectionSize;

  // If creating a dylib, add LC_ID_DYLIB.
  if (_file.fileType == llvm::MachO::MH_DYLIB) {
    size += sizeof(dylib_command) + pointerAlign(_file.installName.size() + 1);
    ++count;
  }

  // Add LC_DYLD_INFO
  size += sizeof(dyld_info_command);
  ++count;

  // Add LC_SYMTAB
  size += sizeof(symtab_command);
  ++count;

  // Add LC_DYSYMTAB
  if (_file.fileType != llvm::MachO::MH_PRELOAD) {
    size += sizeof(dysymtab_command);
    ++count;
  }

  // If main executable add LC_LOAD_DYLINKER
  if (_file.fileType == llvm::MachO::MH_EXECUTE) {
    size += pointerAlign(sizeof(dylinker_command) + dyldPath().size()+1);
    ++count;
  }

  // Add LC_VERSION_MIN_MACOSX, LC_VERSION_MIN_IPHONEOS, LC_VERSION_MIN_WATCHOS,
  // LC_VERSION_MIN_TVOS
  if (_file.hasMinVersionLoadCommand) {
    size += sizeof(version_min_command);
    ++count;
  }

  // Add LC_SOURCE_VERSION
  size += sizeof(source_version_command);
  ++count;

  // If main executable add LC_MAIN
  if (_file.fileType == llvm::MachO::MH_EXECUTE) {
    size += sizeof(entry_point_command);
    ++count;
  }

  // Add LC_LOAD_DYLIB for each dependent dylib.
  for (const DependentDylib &dep : _file.dependentDylibs) {
    size += sizeof(dylib_command) + pointerAlign(dep.path.size()+1);
    ++count;
  }

  // Add LC_RPATH
  for (const StringRef &path : _file.rpaths) {
    size += pointerAlign(sizeof(rpath_command) + path.size() + 1);
    ++count;
  }

  // Add LC_FUNCTION_STARTS if needed
  if (!_file.functionStarts.empty()) {
    size += sizeof(linkedit_data_command);
    ++count;
  }

  // Add LC_DATA_IN_CODE if requested.  Note, we do encode zero length entries.
  // FIXME: Zero length entries is only to match ld64.  Should we change this?
  if (_file.generateDataInCodeLoadCommand) {
    size += sizeof(linkedit_data_command);
    ++count;
  }

  return size;
}

static bool overlaps(const Segment &s1, const Segment &s2) {
  if (s2.address >= s1.address+s1.size)
    return false;
  if (s1.address >= s2.address+s2.size)
    return false;
  return true;
}

static bool overlaps(const Section &s1, const Section &s2) {
  if (s2.address >= s1.address+s1.content.size())
    return false;
  if (s1.address >= s2.address+s2.content.size())
    return false;
  return true;
}

void MachOFileLayout::buildFileOffsets() {
  // Verify no segments overlap
  for (const Segment &sg1 : _file.segments) {
    for (const Segment &sg2 : _file.segments) {
      if (&sg1 == &sg2)
        continue;
      if (overlaps(sg1,sg2)) {
        _ec = make_error_code(llvm::errc::executable_format_error);
        return;
      }
    }
  }

  // Verify no sections overlap
  for (const Section &s1 : _file.sections) {
    for (const Section &s2 : _file.sections) {
      if (&s1 == &s2)
        continue;
      if (overlaps(s1,s2)) {
        _ec = make_error_code(llvm::errc::executable_format_error);
        return;
      }
    }
  }

  // Build side table of extra info about segments and sections.
  SegExtraInfo t;
  t.fileOffset = 0;
  for (const Segment &sg : _file.segments) {
    _segInfo[&sg] = t;
  }
  SectionExtraInfo t2;
  t2.fileOffset = 0;
  // Assign sections to segments.
  for (const Section &s : _file.sections) {
    _sectInfo[&s] = t2;
    bool foundSegment = false;
    for (const Segment &sg : _file.segments) {
      if (sg.name.equals(s.segmentName)) {
        if ((s.address >= sg.address)
                        && (s.address+s.content.size() <= sg.address+sg.size)) {
          _segInfo[&sg].sections.push_back(&s);
          foundSegment = true;
          break;
        }
      }
    }
    if (!foundSegment) {
      _ec = make_error_code(llvm::errc::executable_format_error);
      return;
    }
  }

  // Assign file offsets.
  uint32_t fileOffset = 0;
  DEBUG_WITH_TYPE("MachOFileLayout",
                  llvm::dbgs() << "buildFileOffsets()\n");
  for (const Segment &sg : _file.segments) {
    _segInfo[&sg].fileOffset = fileOffset;
    if ((_seg1addr == INT64_MAX) && sg.init_access)
      _seg1addr = sg.address;
    DEBUG_WITH_TYPE("MachOFileLayout",
                  llvm::dbgs() << "  segment=" << sg.name
                  << ", fileOffset=" << _segInfo[&sg].fileOffset << "\n");

    uint32_t segFileSize = 0;
    // A segment that is not zero-fill must use a least one page of disk space.
    if (sg.init_access)
      segFileSize = _file.pageSize;
    for (const Section *s : _segInfo[&sg].sections) {
      uint32_t sectOffset = s->address - sg.address;
      uint32_t sectFileSize =
        isZeroFillSection(s->type) ? 0 : s->content.size();
      segFileSize = std::max(segFileSize, sectOffset + sectFileSize);

      _sectInfo[s].fileOffset = _segInfo[&sg].fileOffset + sectOffset;
      DEBUG_WITH_TYPE("MachOFileLayout",
                  llvm::dbgs() << "    section=" << s->sectionName
                  << ", fileOffset=" << fileOffset << "\n");
    }

    // round up all segments to page aligned, except __LINKEDIT
    if (!sg.name.equals("__LINKEDIT")) {
      _segInfo[&sg].fileSize = llvm::alignTo(segFileSize, _file.pageSize);
      fileOffset = llvm::alignTo(fileOffset + segFileSize, _file.pageSize);
    }
    _addressOfLinkEdit = sg.address + sg.size;
  }
  _startOfLinkEdit = fileOffset;
}

size_t MachOFileLayout::size() const {
  return _endOfSymbolStrings;
}

void MachOFileLayout::writeMachHeader() {
  auto cpusubtype = MachOLinkingContext::cpuSubtypeFromArch(_file.arch);
  // dynamic x86 executables on newer OS version should also set the
  // CPU_SUBTYPE_LIB64 mask in the CPU subtype.
  // FIXME: Check that this is a dynamic executable, not a static one.
  if (_file.fileType == llvm::MachO::MH_EXECUTE &&
      cpusubtype == CPU_SUBTYPE_X86_64_ALL &&
      _file.os == MachOLinkingContext::OS::macOSX) {
    uint32_t version;
    bool failed = MachOLinkingContext::parsePackedVersion("10.5", version);
    if (!failed && _file.minOSverson >= version)
      cpusubtype |= CPU_SUBTYPE_LIB64;
  }

  mach_header *mh = reinterpret_cast<mach_header*>(_buffer);
  mh->magic = _is64 ? llvm::MachO::MH_MAGIC_64 : llvm::MachO::MH_MAGIC;
  mh->cputype =  MachOLinkingContext::cpuTypeFromArch(_file.arch);
  mh->cpusubtype = cpusubtype;
  mh->filetype = _file.fileType;
  mh->ncmds = _countOfLoadCommands;
  mh->sizeofcmds = _endOfLoadCommands - _startOfLoadCommands;
  mh->flags = _file.flags;
  if (_swap)
    swapStruct(*mh);
}

uint32_t MachOFileLayout::indirectSymbolIndex(const Section &sect,
                                                   uint32_t &index) {
  if (sect.indirectSymbols.empty())
    return 0;
  uint32_t result = index;
  index += sect.indirectSymbols.size();
  return result;
}

uint32_t MachOFileLayout::indirectSymbolElementSize(const Section &sect) {
  if (sect.indirectSymbols.empty())
    return 0;
  if (sect.type != S_SYMBOL_STUBS)
    return 0;
  return sect.content.size() / sect.indirectSymbols.size();
}

template <typename T>
llvm::Error MachOFileLayout::writeSingleSegmentLoadCommand(uint8_t *&lc) {
  typename T::command* seg = reinterpret_cast<typename T::command*>(lc);
  seg->cmd = T::LC;
  seg->cmdsize = sizeof(typename T::command)
                          + _file.sections.size() * sizeof(typename T::section);
  uint8_t *next = lc + seg->cmdsize;
  memset(seg->segname, 0, 16);
  seg->vmaddr = 0;
  seg->vmsize = _file.sections.back().address
              + _file.sections.back().content.size();
  seg->fileoff = _endOfLoadCommands;
  seg->filesize = _sectInfo[&_file.sections.back()].fileOffset +
                  _file.sections.back().content.size() -
                  _sectInfo[&_file.sections.front()].fileOffset;
  seg->maxprot = VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE;
  seg->initprot = VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE;
  seg->nsects = _file.sections.size();
  seg->flags = 0;
  if (_swap)
    swapStruct(*seg);
  typename T::section *sout = reinterpret_cast<typename T::section*>
                                              (lc+sizeof(typename T::command));
  uint32_t relOffset = _startOfRelocations;
  uint32_t indirectSymRunningIndex = 0;
  for (const Section &sin : _file.sections) {
    setString16(sin.sectionName, sout->sectname);
    setString16(sin.segmentName, sout->segname);
    sout->addr = sin.address;
    sout->size = sin.content.size();
    sout->offset = _sectInfo[&sin].fileOffset;
    sout->align = llvm::Log2_32(sin.alignment);
    sout->reloff = sin.relocations.empty() ? 0 : relOffset;
    sout->nreloc = sin.relocations.size();
    sout->flags = sin.type | sin.attributes;
    sout->reserved1 = indirectSymbolIndex(sin, indirectSymRunningIndex);
    sout->reserved2 = indirectSymbolElementSize(sin);
    relOffset += sin.relocations.size() * sizeof(any_relocation_info);
    if (_swap)
      swapStruct(*sout);
    ++sout;
  }
  lc = next;
  return llvm::Error::success();
}

template <typename T>
llvm::Error MachOFileLayout::writeSegmentLoadCommands(uint8_t *&lc) {
  uint32_t indirectSymRunningIndex = 0;
  for (const Segment &seg : _file.segments) {
    // Link edit has no sections and a custom range of address, so handle it
    // specially.
    SegExtraInfo &segInfo = _segInfo[&seg];
    if (seg.name.equals("__LINKEDIT")) {
      size_t linkeditSize = _endOfLinkEdit - _startOfLinkEdit;
      typename T::command* cmd = reinterpret_cast<typename T::command*>(lc);
      cmd->cmd = T::LC;
      cmd->cmdsize = sizeof(typename T::command);
      uint8_t *next = lc + cmd->cmdsize;
      setString16("__LINKEDIT", cmd->segname);
      cmd->vmaddr   = _addressOfLinkEdit;
      cmd->vmsize   = llvm::alignTo(linkeditSize, _file.pageSize);
      cmd->fileoff  = _startOfLinkEdit;
      cmd->filesize = linkeditSize;
      cmd->initprot = seg.init_access;
      cmd->maxprot  = seg.max_access;
      cmd->nsects   = 0;
      cmd->flags    = 0;
      if (_swap)
        swapStruct(*cmd);
      lc = next;
      continue;
    }
    // Write segment command with trailing sections.
    typename T::command* cmd = reinterpret_cast<typename T::command*>(lc);
    cmd->cmd = T::LC;
    cmd->cmdsize = sizeof(typename T::command)
                        + segInfo.sections.size() * sizeof(typename T::section);
    uint8_t *next = lc + cmd->cmdsize;
    setString16(seg.name, cmd->segname);
    cmd->vmaddr   = seg.address;
    cmd->vmsize   = seg.size;
    cmd->fileoff  = segInfo.fileOffset;
    cmd->filesize = segInfo.fileSize;
    cmd->initprot = seg.init_access;
    cmd->maxprot  = seg.max_access;
    cmd->nsects   = segInfo.sections.size();
    cmd->flags    = 0;
    if (_swap)
      swapStruct(*cmd);
    typename T::section *sect = reinterpret_cast<typename T::section*>
                                               (lc+sizeof(typename T::command));
    for (const Section *section : segInfo.sections) {
      setString16(section->sectionName, sect->sectname);
      setString16(section->segmentName, sect->segname);
      sect->addr      = section->address;
      sect->size      = section->content.size();
      if (isZeroFillSection(section->type))
        sect->offset  = 0;
      else
        sect->offset  = section->address - seg.address + segInfo.fileOffset;
      sect->align     = llvm::Log2_32(section->alignment);
      sect->reloff    = 0;
      sect->nreloc    = 0;
      sect->flags     = section->type | section->attributes;
      sect->reserved1 = indirectSymbolIndex(*section, indirectSymRunningIndex);
      sect->reserved2 = indirectSymbolElementSize(*section);
      if (_swap)
        swapStruct(*sect);
      ++sect;
    }
    lc = reinterpret_cast<uint8_t*>(next);
  }
  return llvm::Error::success();
}

static void writeVersionMinLoadCommand(const NormalizedFile &_file,
                                       bool _swap,
                                       uint8_t *&lc) {
  if (!_file.hasMinVersionLoadCommand)
    return;
  version_min_command *vm = reinterpret_cast<version_min_command*>(lc);
  switch (_file.os) {
    case MachOLinkingContext::OS::unknown:
      vm->cmd     = _file.minOSVersionKind;
      vm->cmdsize = sizeof(version_min_command);
      vm->version = _file.minOSverson;
      vm->sdk     = 0;
      break;
    case MachOLinkingContext::OS::macOSX:
      vm->cmd     = LC_VERSION_MIN_MACOSX;
      vm->cmdsize = sizeof(version_min_command);
      vm->version = _file.minOSverson;
      vm->sdk     = _file.sdkVersion;
      break;
    case MachOLinkingContext::OS::iOS:
    case MachOLinkingContext::OS::iOS_simulator:
      vm->cmd     = LC_VERSION_MIN_IPHONEOS;
      vm->cmdsize = sizeof(version_min_command);
      vm->version = _file.minOSverson;
      vm->sdk     = _file.sdkVersion;
      break;
  }
  if (_swap)
    swapStruct(*vm);
  lc += sizeof(version_min_command);
}

llvm::Error MachOFileLayout::writeLoadCommands() {
  uint8_t *lc = &_buffer[_startOfLoadCommands];
  if (_file.fileType == llvm::MachO::MH_OBJECT) {
    // Object files have one unnamed segment which holds all sections.
    if (_is64) {
     if (auto ec = writeSingleSegmentLoadCommand<MachO64Trait>(lc))
       return ec;
    } else {
      if (auto ec = writeSingleSegmentLoadCommand<MachO32Trait>(lc))
        return ec;
    }
    // Add LC_SYMTAB with symbol table info
    symtab_command* st = reinterpret_cast<symtab_command*>(lc);
    st->cmd     = LC_SYMTAB;
    st->cmdsize = sizeof(symtab_command);
    st->symoff  = _startOfSymbols;
    st->nsyms   = _file.stabsSymbols.size() + _file.localSymbols.size() +
                  _file.globalSymbols.size() + _file.undefinedSymbols.size();
    st->stroff  = _startOfSymbolStrings;
    st->strsize = _endOfSymbolStrings - _startOfSymbolStrings;
    if (_swap)
      swapStruct(*st);
    lc += sizeof(symtab_command);

    // Add LC_VERSION_MIN_MACOSX, LC_VERSION_MIN_IPHONEOS,
    // LC_VERSION_MIN_WATCHOS, LC_VERSION_MIN_TVOS
    writeVersionMinLoadCommand(_file, _swap, lc);

    // Add LC_FUNCTION_STARTS if needed.
    if (_functionStartsSize != 0) {
      linkedit_data_command* dl = reinterpret_cast<linkedit_data_command*>(lc);
      dl->cmd      = LC_FUNCTION_STARTS;
      dl->cmdsize  = sizeof(linkedit_data_command);
      dl->dataoff  = _startOfFunctionStarts;
      dl->datasize = _functionStartsSize;
      if (_swap)
        swapStruct(*dl);
      lc += sizeof(linkedit_data_command);
    }

    // Add LC_DATA_IN_CODE if requested.
    if (_file.generateDataInCodeLoadCommand) {
      linkedit_data_command* dl = reinterpret_cast<linkedit_data_command*>(lc);
      dl->cmd      = LC_DATA_IN_CODE;
      dl->cmdsize  = sizeof(linkedit_data_command);
      dl->dataoff  = _startOfDataInCode;
      dl->datasize = _dataInCodeSize;
      if (_swap)
        swapStruct(*dl);
      lc += sizeof(linkedit_data_command);
    }
  } else {
    // Final linked images have sections under segments.
    if (_is64) {
      if (auto ec = writeSegmentLoadCommands<MachO64Trait>(lc))
        return ec;
    } else {
      if (auto ec = writeSegmentLoadCommands<MachO32Trait>(lc))
        return ec;
    }

    // Add LC_ID_DYLIB command for dynamic libraries.
    if (_file.fileType == llvm::MachO::MH_DYLIB) {
      dylib_command *dc = reinterpret_cast<dylib_command*>(lc);
      StringRef path = _file.installName;
      uint32_t size = sizeof(dylib_command) + pointerAlign(path.size() + 1);
      dc->cmd                         = LC_ID_DYLIB;
      dc->cmdsize                     = size;
      dc->dylib.name                  = sizeof(dylib_command); // offset
      // needs to be some constant value different than the one in LC_LOAD_DYLIB
      dc->dylib.timestamp             = 1;
      dc->dylib.current_version       = _file.currentVersion;
      dc->dylib.compatibility_version = _file.compatVersion;
      if (_swap)
        swapStruct(*dc);
      memcpy(lc + sizeof(dylib_command), path.begin(), path.size());
      lc[sizeof(dylib_command) + path.size()] = '\0';
      lc += size;
    }

    // Add LC_DYLD_INFO_ONLY.
    dyld_info_command* di = reinterpret_cast<dyld_info_command*>(lc);
    di->cmd            = LC_DYLD_INFO_ONLY;
    di->cmdsize        = sizeof(dyld_info_command);
    di->rebase_off     = _rebaseInfo.size() ? _startOfRebaseInfo : 0;
    di->rebase_size    = _rebaseInfo.size();
    di->bind_off       = _bindingInfo.size() ? _startOfBindingInfo : 0;
    di->bind_size      = _bindingInfo.size();
    di->weak_bind_off  = 0;
    di->weak_bind_size = 0;
    di->lazy_bind_off  = _lazyBindingInfo.size() ? _startOfLazyBindingInfo : 0;
    di->lazy_bind_size = _lazyBindingInfo.size();
    di->export_off     = _exportTrie.size() ? _startOfExportTrie : 0;
    di->export_size    = _exportTrie.size();
    if (_swap)
      swapStruct(*di);
    lc += sizeof(dyld_info_command);

    // Add LC_SYMTAB with symbol table info.
    symtab_command* st = reinterpret_cast<symtab_command*>(lc);
    st->cmd     = LC_SYMTAB;
    st->cmdsize = sizeof(symtab_command);
    st->symoff  = _startOfSymbols;
    st->nsyms   = _file.stabsSymbols.size() + _file.localSymbols.size() +
                  _file.globalSymbols.size() + _file.undefinedSymbols.size();
    st->stroff  = _startOfSymbolStrings;
    st->strsize = _endOfSymbolStrings - _startOfSymbolStrings;
    if (_swap)
      swapStruct(*st);
    lc += sizeof(symtab_command);

    // Add LC_DYSYMTAB
    if (_file.fileType != llvm::MachO::MH_PRELOAD) {
      dysymtab_command* dst = reinterpret_cast<dysymtab_command*>(lc);
      dst->cmd            = LC_DYSYMTAB;
      dst->cmdsize        = sizeof(dysymtab_command);
      dst->ilocalsym      = _symbolTableLocalsStartIndex;
      dst->nlocalsym      = _file.stabsSymbols.size() +
                            _file.localSymbols.size();
      dst->iextdefsym     = _symbolTableGlobalsStartIndex;
      dst->nextdefsym     = _file.globalSymbols.size();
      dst->iundefsym      = _symbolTableUndefinesStartIndex;
      dst->nundefsym      = _file.undefinedSymbols.size();
      dst->tocoff         = 0;
      dst->ntoc           = 0;
      dst->modtaboff      = 0;
      dst->nmodtab        = 0;
      dst->extrefsymoff   = 0;
      dst->nextrefsyms    = 0;
      dst->indirectsymoff = _startOfIndirectSymbols;
      dst->nindirectsyms  = _indirectSymbolTableCount;
      dst->extreloff      = 0;
      dst->nextrel        = 0;
      dst->locreloff      = 0;
      dst->nlocrel        = 0;
      if (_swap)
        swapStruct(*dst);
      lc += sizeof(dysymtab_command);
    }

    // If main executable, add LC_LOAD_DYLINKER
    if (_file.fileType == llvm::MachO::MH_EXECUTE) {
      // Build LC_LOAD_DYLINKER load command.
      uint32_t size=pointerAlign(sizeof(dylinker_command)+dyldPath().size()+1);
      dylinker_command* dl = reinterpret_cast<dylinker_command*>(lc);
      dl->cmd              = LC_LOAD_DYLINKER;
      dl->cmdsize          = size;
      dl->name             = sizeof(dylinker_command); // offset
      if (_swap)
        swapStruct(*dl);
      memcpy(lc+sizeof(dylinker_command), dyldPath().data(), dyldPath().size());
      lc[sizeof(dylinker_command)+dyldPath().size()] = '\0';
      lc += size;
    }

    // Add LC_VERSION_MIN_MACOSX, LC_VERSION_MIN_IPHONEOS, LC_VERSION_MIN_WATCHOS,
    // LC_VERSION_MIN_TVOS
    writeVersionMinLoadCommand(_file, _swap, lc);

    // Add LC_SOURCE_VERSION
    {
      // Note, using a temporary here to appease UB as we may not be aligned
      // enough for a struct containing a uint64_t when emitting a 32-bit binary
      source_version_command sv;
      sv.cmd       = LC_SOURCE_VERSION;
      sv.cmdsize   = sizeof(source_version_command);
      sv.version   = _file.sourceVersion;
      if (_swap)
        swapStruct(sv);
      memcpy(lc, &sv, sizeof(source_version_command));
      lc += sizeof(source_version_command);
    }

    // If main executable, add LC_MAIN.
    if (_file.fileType == llvm::MachO::MH_EXECUTE) {
      // Build LC_MAIN load command.
      // Note, using a temporary here to appease UB as we may not be aligned
      // enough for a struct containing a uint64_t when emitting a 32-bit binary
      entry_point_command ep;
      ep.cmd       = LC_MAIN;
      ep.cmdsize   = sizeof(entry_point_command);
      ep.entryoff  = _file.entryAddress - _seg1addr;
      ep.stacksize = _file.stackSize;
      if (_swap)
        swapStruct(ep);
      memcpy(lc, &ep, sizeof(entry_point_command));
      lc += sizeof(entry_point_command);
    }

    // Add LC_LOAD_DYLIB commands
    for (const DependentDylib &dep : _file.dependentDylibs) {
      dylib_command* dc = reinterpret_cast<dylib_command*>(lc);
      uint32_t size = sizeof(dylib_command) + pointerAlign(dep.path.size()+1);
      dc->cmd                         = dep.kind;
      dc->cmdsize                     = size;
      dc->dylib.name                  = sizeof(dylib_command); // offset
      // needs to be some constant value different than the one in LC_ID_DYLIB
      dc->dylib.timestamp             = 2;
      dc->dylib.current_version       = dep.currentVersion;
      dc->dylib.compatibility_version = dep.compatVersion;
      if (_swap)
        swapStruct(*dc);
      memcpy(lc+sizeof(dylib_command), dep.path.begin(), dep.path.size());
      lc[sizeof(dylib_command)+dep.path.size()] = '\0';
      lc += size;
    }

    // Add LC_RPATH
    for (const StringRef &path : _file.rpaths) {
      rpath_command *rpc = reinterpret_cast<rpath_command *>(lc);
      uint32_t size = pointerAlign(sizeof(rpath_command) + path.size() + 1);
      rpc->cmd                         = LC_RPATH;
      rpc->cmdsize                     = size;
      rpc->path                        = sizeof(rpath_command); // offset
      if (_swap)
        swapStruct(*rpc);
      memcpy(lc+sizeof(rpath_command), path.begin(), path.size());
      lc[sizeof(rpath_command)+path.size()] = '\0';
      lc += size;
    }

    // Add LC_FUNCTION_STARTS if needed.
    if (_functionStartsSize != 0) {
      linkedit_data_command* dl = reinterpret_cast<linkedit_data_command*>(lc);
      dl->cmd      = LC_FUNCTION_STARTS;
      dl->cmdsize  = sizeof(linkedit_data_command);
      dl->dataoff  = _startOfFunctionStarts;
      dl->datasize = _functionStartsSize;
      if (_swap)
        swapStruct(*dl);
      lc += sizeof(linkedit_data_command);
    }

    // Add LC_DATA_IN_CODE if requested.
    if (_file.generateDataInCodeLoadCommand) {
      linkedit_data_command* dl = reinterpret_cast<linkedit_data_command*>(lc);
      dl->cmd      = LC_DATA_IN_CODE;
      dl->cmdsize  = sizeof(linkedit_data_command);
      dl->dataoff  = _startOfDataInCode;
      dl->datasize = _dataInCodeSize;
      if (_swap)
        swapStruct(*dl);
      lc += sizeof(linkedit_data_command);
    }
  }
  return llvm::Error::success();
}

void MachOFileLayout::writeSectionContent() {
  for (const Section &s : _file.sections) {
    // Copy all section content to output buffer.
    if (isZeroFillSection(s.type))
      continue;
    if (s.content.empty())
      continue;
    uint32_t offset = _sectInfo[&s].fileOffset;
    uint8_t *p = &_buffer[offset];
    memcpy(p, &s.content[0], s.content.size());
    p += s.content.size();
  }
}

void MachOFileLayout::writeRelocations() {
  uint32_t relOffset = _startOfRelocations;
  for (Section sect : _file.sections) {
    for (Relocation r : sect.relocations) {
      any_relocation_info* rb = reinterpret_cast<any_relocation_info*>(
                                                           &_buffer[relOffset]);
      *rb = packRelocation(r, _swap, _bigEndianArch);
      relOffset += sizeof(any_relocation_info);
    }
  }
}

void MachOFileLayout::appendSymbols(const std::vector<Symbol> &symbols,
                                   uint32_t &symOffset, uint32_t &strOffset) {
  for (const Symbol &sym : symbols) {
    if (_is64) {
      nlist_64* nb = reinterpret_cast<nlist_64*>(&_buffer[symOffset]);
      nb->n_strx = strOffset - _startOfSymbolStrings;
      nb->n_type = sym.type | sym.scope;
      nb->n_sect = sym.sect;
      nb->n_desc = sym.desc;
      nb->n_value = sym.value;
      if (_swap)
        swapStruct(*nb);
      symOffset += sizeof(nlist_64);
    } else {
      nlist* nb = reinterpret_cast<nlist*>(&_buffer[symOffset]);
      nb->n_strx = strOffset - _startOfSymbolStrings;
      nb->n_type = sym.type | sym.scope;
      nb->n_sect = sym.sect;
      nb->n_desc = sym.desc;
      nb->n_value = sym.value;
      if (_swap)
        swapStruct(*nb);
      symOffset += sizeof(nlist);
    }
    memcpy(&_buffer[strOffset], sym.name.begin(), sym.name.size());
    strOffset += sym.name.size();
    _buffer[strOffset++] ='\0'; // Strings in table have nul terminator.
  }
}

void MachOFileLayout::writeFunctionStartsInfo() {
  if (!_functionStartsSize)
    return;
  memcpy(&_buffer[_startOfFunctionStarts], _file.functionStarts.data(),
         _functionStartsSize);
}

void MachOFileLayout::writeDataInCodeInfo() {
  uint32_t offset = _startOfDataInCode;
  for (const DataInCode &entry : _file.dataInCode) {
    data_in_code_entry *dst = reinterpret_cast<data_in_code_entry*>(
                                                             &_buffer[offset]);
    dst->offset = entry.offset;
    dst->length = entry.length;
    dst->kind   = entry.kind;
    if (_swap)
      swapStruct(*dst);
    offset += sizeof(data_in_code_entry);
  }
}

void MachOFileLayout::writeSymbolTable() {
  // Write symbol table and symbol strings in parallel.
  uint32_t symOffset = _startOfSymbols;
  uint32_t strOffset = _startOfSymbolStrings;
  // Reserve n_strx offset of zero to mean no name.
  _buffer[strOffset++] = ' ';
  _buffer[strOffset++] = '\0';
  appendSymbols(_file.stabsSymbols, symOffset, strOffset);
  appendSymbols(_file.localSymbols, symOffset, strOffset);
  appendSymbols(_file.globalSymbols, symOffset, strOffset);
  appendSymbols(_file.undefinedSymbols, symOffset, strOffset);
  // Write indirect symbol table array.
  uint32_t *indirects = reinterpret_cast<uint32_t*>
                                            (&_buffer[_startOfIndirectSymbols]);
  if (_file.fileType == llvm::MachO::MH_OBJECT) {
    // Object files have sections in same order as input normalized file.
    for (const Section &section : _file.sections) {
      for (uint32_t index : section.indirectSymbols) {
        if (_swap)
          *indirects++ = llvm::sys::getSwappedBytes(index);
        else
          *indirects++ = index;
      }
    }
  } else {
    // Final linked images must sort sections from normalized file.
    for (const Segment &seg : _file.segments) {
      SegExtraInfo &segInfo = _segInfo[&seg];
      for (const Section *section : segInfo.sections) {
        for (uint32_t index : section->indirectSymbols) {
          if (_swap)
            *indirects++ = llvm::sys::getSwappedBytes(index);
          else
            *indirects++ = index;
        }
      }
    }
  }
}

void MachOFileLayout::writeRebaseInfo() {
  memcpy(&_buffer[_startOfRebaseInfo], _rebaseInfo.bytes(), _rebaseInfo.size());
}

void MachOFileLayout::writeBindingInfo() {
  memcpy(&_buffer[_startOfBindingInfo],
                                    _bindingInfo.bytes(), _bindingInfo.size());
}

void MachOFileLayout::writeLazyBindingInfo() {
  memcpy(&_buffer[_startOfLazyBindingInfo],
                            _lazyBindingInfo.bytes(), _lazyBindingInfo.size());
}

void MachOFileLayout::writeExportInfo() {
  memcpy(&_buffer[_startOfExportTrie], _exportTrie.bytes(), _exportTrie.size());
}

void MachOFileLayout::buildLinkEditInfo() {
  buildRebaseInfo();
  buildBindInfo();
  buildLazyBindInfo();
  buildExportTrie();
  computeSymbolTableSizes();
  computeFunctionStartsSize();
  computeDataInCodeSize();
}

void MachOFileLayout::buildSectionRelocations() {

}

void MachOFileLayout::buildRebaseInfo() {
  // TODO: compress rebasing info.
  for (const RebaseLocation& entry : _file.rebasingInfo) {
    _rebaseInfo.append_byte(REBASE_OPCODE_SET_TYPE_IMM | entry.kind);
    _rebaseInfo.append_byte(REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB
                            | entry.segIndex);
    _rebaseInfo.append_uleb128(entry.segOffset);
    _rebaseInfo.append_uleb128(REBASE_OPCODE_DO_REBASE_IMM_TIMES | 1);
  }
  _rebaseInfo.append_byte(REBASE_OPCODE_DONE);
  _rebaseInfo.align(_is64 ? 8 : 4);
}

void MachOFileLayout::buildBindInfo() {
  // TODO: compress bind info.
  uint64_t lastAddend = 0;
  int lastOrdinal = 0x80000000;
  StringRef lastSymbolName;
  BindType lastType = (BindType)0;
  Hex32 lastSegOffset = ~0U;
  uint8_t lastSegIndex = (uint8_t)~0U;
  for (const BindLocation& entry : _file.bindingInfo) {
    if (entry.ordinal != lastOrdinal) {
      if (entry.ordinal <= 0)
        _bindingInfo.append_byte(BIND_OPCODE_SET_DYLIB_SPECIAL_IMM |
                                 (entry.ordinal & BIND_IMMEDIATE_MASK));
      else if (entry.ordinal <= BIND_IMMEDIATE_MASK)
        _bindingInfo.append_byte(BIND_OPCODE_SET_DYLIB_ORDINAL_IMM |
                                 entry.ordinal);
      else {
        _bindingInfo.append_byte(BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB);
        _bindingInfo.append_uleb128(entry.ordinal);
      }
      lastOrdinal = entry.ordinal;
    }

    if (lastSymbolName != entry.symbolName) {
      _bindingInfo.append_byte(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM);
      _bindingInfo.append_string(entry.symbolName);
      lastSymbolName = entry.symbolName;
    }

    if (lastType != entry.kind) {
      _bindingInfo.append_byte(BIND_OPCODE_SET_TYPE_IMM | entry.kind);
      lastType = entry.kind;
    }

    if (lastSegIndex != entry.segIndex || lastSegOffset != entry.segOffset) {
      _bindingInfo.append_byte(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB
                               | entry.segIndex);
      _bindingInfo.append_uleb128(entry.segOffset);
      lastSegIndex = entry.segIndex;
      lastSegOffset = entry.segOffset;
    }
    if (entry.addend != lastAddend) {
      _bindingInfo.append_byte(BIND_OPCODE_SET_ADDEND_SLEB);
      _bindingInfo.append_sleb128(entry.addend);
      lastAddend = entry.addend;
    }
    _bindingInfo.append_byte(BIND_OPCODE_DO_BIND);
  }
  _bindingInfo.append_byte(BIND_OPCODE_DONE);
  _bindingInfo.align(_is64 ? 8 : 4);
}

void MachOFileLayout::buildLazyBindInfo() {
  for (const BindLocation& entry : _file.lazyBindingInfo) {
    _lazyBindingInfo.append_byte(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB
                            | entry.segIndex);
    _lazyBindingInfo.append_uleb128(entry.segOffset);
    if (entry.ordinal <= 0)
      _lazyBindingInfo.append_byte(BIND_OPCODE_SET_DYLIB_SPECIAL_IMM |
                                   (entry.ordinal & BIND_IMMEDIATE_MASK));
    else if (entry.ordinal <= BIND_IMMEDIATE_MASK)
      _lazyBindingInfo.append_byte(BIND_OPCODE_SET_DYLIB_ORDINAL_IMM |
                                   entry.ordinal);
    else {
      _lazyBindingInfo.append_byte(BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB);
      _lazyBindingInfo.append_uleb128(entry.ordinal);
    }
    // FIXME: We need to | the opcode here with flags.
    _lazyBindingInfo.append_byte(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM);
    _lazyBindingInfo.append_string(entry.symbolName);
    _lazyBindingInfo.append_byte(BIND_OPCODE_DO_BIND);
    _lazyBindingInfo.append_byte(BIND_OPCODE_DONE);
  }
  _lazyBindingInfo.align(_is64 ? 8 : 4);
}

void TrieNode::addSymbol(const Export& entry,
                         BumpPtrAllocator &allocator,
                         std::vector<TrieNode*> &allNodes) {
  StringRef partialStr = entry.name.drop_front(_cummulativeString.size());
  for (TrieEdge &edge : _children) {
    StringRef edgeStr = edge._subString;
    if (partialStr.startswith(edgeStr)) {
      // Already have matching edge, go down that path.
      edge._child->addSymbol(entry, allocator, allNodes);
      return;
    }
    // See if string has commmon prefix with existing edge.
    for (int n=edgeStr.size()-1; n > 0; --n) {
      if (partialStr.substr(0, n).equals(edgeStr.substr(0, n))) {
        // Splice in new node:  was A -> C,  now A -> B -> C
        StringRef bNodeStr = edge._child->_cummulativeString;
        bNodeStr = bNodeStr.drop_back(edgeStr.size()-n).copy(allocator);
        auto *bNode = new (allocator) TrieNode(bNodeStr);
        allNodes.push_back(bNode);
        TrieNode* cNode = edge._child;
        StringRef abEdgeStr = edgeStr.substr(0,n).copy(allocator);
        StringRef bcEdgeStr = edgeStr.substr(n).copy(allocator);
        DEBUG_WITH_TYPE("trie-builder", llvm::dbgs()
                        << "splice in TrieNode('" << bNodeStr
                        << "') between edge '"
                        << abEdgeStr << "' and edge='"
                        << bcEdgeStr<< "'\n");
        TrieEdge& abEdge = edge;
        abEdge._subString = abEdgeStr;
        abEdge._child = bNode;
        auto *bcEdge = new (allocator) TrieEdge(bcEdgeStr, cNode);
        bNode->_children.insert(bNode->_children.end(), bcEdge);
        bNode->addSymbol(entry, allocator, allNodes);
        return;
      }
    }
  }
  if (entry.flags & EXPORT_SYMBOL_FLAGS_REEXPORT) {
    assert(entry.otherOffset != 0);
  }
  if (entry.flags & EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) {
    assert(entry.otherOffset != 0);
  }
  // No commonality with any existing child, make a new edge.
  auto *newNode = new (allocator) TrieNode(entry.name.copy(allocator));
  auto *newEdge = new (allocator) TrieEdge(partialStr, newNode);
  _children.insert(_children.end(), newEdge);
  DEBUG_WITH_TYPE("trie-builder", llvm::dbgs()
                   << "new TrieNode('" << entry.name << "') with edge '"
                   << partialStr << "' from node='"
                   << _cummulativeString << "'\n");
  newNode->_address = entry.offset;
  newNode->_flags = entry.flags | entry.kind;
  newNode->_other = entry.otherOffset;
  if ((entry.flags & EXPORT_SYMBOL_FLAGS_REEXPORT) && !entry.otherName.empty())
    newNode->_importedName = entry.otherName.copy(allocator);
  newNode->_hasExportInfo = true;
  allNodes.push_back(newNode);
}

void TrieNode::addOrderedNodes(const Export& entry,
                               std::vector<TrieNode*> &orderedNodes) {
  if (!_ordered) {
    orderedNodes.push_back(this);
    _ordered = true;
  }

  StringRef partialStr = entry.name.drop_front(_cummulativeString.size());
  for (TrieEdge &edge : _children) {
    StringRef edgeStr = edge._subString;
    if (partialStr.startswith(edgeStr)) {
      // Already have matching edge, go down that path.
      edge._child->addOrderedNodes(entry, orderedNodes);
      return;
    }
  }
}

bool TrieNode::updateOffset(uint32_t& offset) {
  uint32_t nodeSize = 1; // Length when no export info
  if (_hasExportInfo) {
    if (_flags & EXPORT_SYMBOL_FLAGS_REEXPORT) {
      nodeSize = llvm::getULEB128Size(_flags);
      nodeSize += llvm::getULEB128Size(_other); // Other contains ordinal.
      nodeSize += _importedName.size();
      ++nodeSize; // Trailing zero in imported name.
    } else {
      nodeSize = llvm::getULEB128Size(_flags) + llvm::getULEB128Size(_address);
      if (_flags & EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER)
        nodeSize += llvm::getULEB128Size(_other);
    }
    // Overall node size so far is uleb128 of export info + actual export info.
    nodeSize += llvm::getULEB128Size(nodeSize);
  }
  // Compute size of all child edges.
  ++nodeSize; // Byte for number of chidren.
  for (TrieEdge &edge : _children) {
    nodeSize += edge._subString.size() + 1 // String length.
              + llvm::getULEB128Size(edge._child->_trieOffset); // Offset len.
  }
  // On input, 'offset' is new prefered location for this node.
  bool result = (_trieOffset != offset);
  // Store new location in node object for use by parents.
  _trieOffset = offset;
  // Update offset for next iteration.
  offset += nodeSize;
  // Return true if _trieOffset was changed.
  return result;
}

void TrieNode::appendToByteBuffer(ByteBuffer &out) {
  if (_hasExportInfo) {
    if (_flags & EXPORT_SYMBOL_FLAGS_REEXPORT) {
      if (!_importedName.empty()) {
        // nodes with re-export info: size, flags, ordinal, import-name
        uint32_t nodeSize = llvm::getULEB128Size(_flags)
                          + llvm::getULEB128Size(_other)
                          + _importedName.size() + 1;
        assert(nodeSize < 256);
        out.append_byte(nodeSize);
        out.append_uleb128(_flags);
        out.append_uleb128(_other);
        out.append_string(_importedName);
      } else {
        // nodes without re-export info: size, flags, ordinal, empty-string
        uint32_t nodeSize = llvm::getULEB128Size(_flags)
                          + llvm::getULEB128Size(_other) + 1;
        assert(nodeSize < 256);
        out.append_byte(nodeSize);
        out.append_uleb128(_flags);
        out.append_uleb128(_other);
        out.append_byte(0);
      }
    } else if ( _flags & EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER ) {
      // Nodes with export info: size, flags, address, other
      uint32_t nodeSize = llvm::getULEB128Size(_flags)
                        + llvm::getULEB128Size(_address)
                        + llvm::getULEB128Size(_other);
      assert(nodeSize < 256);
      out.append_byte(nodeSize);
      out.append_uleb128(_flags);
      out.append_uleb128(_address);
      out.append_uleb128(_other);
    } else {
      // Nodes with export info: size, flags, address
      uint32_t nodeSize = llvm::getULEB128Size(_flags)
                        + llvm::getULEB128Size(_address);
      assert(nodeSize < 256);
      out.append_byte(nodeSize);
      out.append_uleb128(_flags);
      out.append_uleb128(_address);
    }
  } else {
    // Node with no export info.
    uint32_t nodeSize = 0;
    out.append_byte(nodeSize);
  }
  // Add number of children.
  assert(_children.size() < 256);
  out.append_byte(_children.size());
  // Append each child edge substring and node offset.
  for (TrieEdge &edge : _children) {
    out.append_string(edge._subString);
    out.append_uleb128(edge._child->_trieOffset);
  }
}

void MachOFileLayout::buildExportTrie() {
  if (_file.exportInfo.empty())
    return;

  // For all temporary strings and objects used building trie.
  BumpPtrAllocator allocator;

  // Build trie of all exported symbols.
  auto *rootNode = new (allocator) TrieNode(StringRef());
  std::vector<TrieNode*> allNodes;
  allNodes.reserve(_file.exportInfo.size()*2);
  allNodes.push_back(rootNode);
  for (const Export& entry : _file.exportInfo) {
    rootNode->addSymbol(entry, allocator, allNodes);
  }

  std::vector<TrieNode*> orderedNodes;
  orderedNodes.reserve(allNodes.size());

  for (const Export& entry : _file.exportInfo)
    rootNode->addOrderedNodes(entry, orderedNodes);

  // Assign each node in the vector an offset in the trie stream, iterating
  // until all uleb128 sizes have stabilized.
  bool more;
  do {
    uint32_t offset = 0;
    more = false;
    for (TrieNode* node : orderedNodes) {
      if (node->updateOffset(offset))
        more = true;
    }
  } while (more);

  // Serialize trie to ByteBuffer.
  for (TrieNode* node : orderedNodes) {
    node->appendToByteBuffer(_exportTrie);
  }
  _exportTrie.align(_is64 ? 8 : 4);
}

void MachOFileLayout::computeSymbolTableSizes() {
  // MachO symbol tables have three ranges: locals, globals, and undefines
  const size_t nlistSize = (_is64 ? sizeof(nlist_64) : sizeof(nlist));
  _symbolTableSize = nlistSize * (_file.stabsSymbols.size()
                                + _file.localSymbols.size()
                                + _file.globalSymbols.size()
                                + _file.undefinedSymbols.size());
  // Always reserve 1-byte for the empty string and 1-byte for its terminator.
  _symbolStringPoolSize = 2;
  for (const Symbol &sym : _file.stabsSymbols) {
    _symbolStringPoolSize += (sym.name.size()+1);
  }
  for (const Symbol &sym : _file.localSymbols) {
    _symbolStringPoolSize += (sym.name.size()+1);
  }
  for (const Symbol &sym : _file.globalSymbols) {
    _symbolStringPoolSize += (sym.name.size()+1);
  }
  for (const Symbol &sym : _file.undefinedSymbols) {
    _symbolStringPoolSize += (sym.name.size()+1);
  }
  _symbolTableLocalsStartIndex = 0;
  _symbolTableGlobalsStartIndex = _file.stabsSymbols.size() +
                                  _file.localSymbols.size();
  _symbolTableUndefinesStartIndex = _symbolTableGlobalsStartIndex
                                    + _file.globalSymbols.size();

  _indirectSymbolTableCount = 0;
  for (const Section &sect : _file.sections) {
    _indirectSymbolTableCount += sect.indirectSymbols.size();
  }
}

void MachOFileLayout::computeFunctionStartsSize() {
  _functionStartsSize = _file.functionStarts.size();
}

void MachOFileLayout::computeDataInCodeSize() {
  _dataInCodeSize = _file.dataInCode.size() * sizeof(data_in_code_entry);
}

void MachOFileLayout::writeLinkEditContent() {
  if (_file.fileType == llvm::MachO::MH_OBJECT) {
    writeRelocations();
    writeFunctionStartsInfo();
    writeDataInCodeInfo();
    writeSymbolTable();
  } else {
    writeRebaseInfo();
    writeBindingInfo();
    writeLazyBindingInfo();
    // TODO: add weak binding info
    writeExportInfo();
    writeFunctionStartsInfo();
    writeDataInCodeInfo();
    writeSymbolTable();
  }
}

llvm::Error MachOFileLayout::writeBinary(StringRef path) {
  // Check for pending error from constructor.
  if (_ec)
    return llvm::errorCodeToError(_ec);
  // Create FileOutputBuffer with calculated size.
  unsigned flags = 0;
  if (_file.fileType != llvm::MachO::MH_OBJECT)
    flags = llvm::FileOutputBuffer::F_executable;
  Expected<std::unique_ptr<llvm::FileOutputBuffer>> fobOrErr =
      llvm::FileOutputBuffer::create(path, size(), flags);
  if (Error E = fobOrErr.takeError())
    return E;
  std::unique_ptr<llvm::FileOutputBuffer> &fob = *fobOrErr;
  // Write content.
  _buffer = fob->getBufferStart();
  writeMachHeader();
  if (auto ec = writeLoadCommands())
    return ec;
  writeSectionContent();
  writeLinkEditContent();
  if (Error E = fob->commit())
    return E;

  return llvm::Error::success();
}

/// Takes in-memory normalized view and writes a mach-o object file.
llvm::Error writeBinary(const NormalizedFile &file, StringRef path) {
  MachOFileLayout layout(file);
  return layout.writeBinary(path);
}

} // namespace normalized
} // namespace mach_o
} // namespace lld
