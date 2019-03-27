//===- lib/ReaderWriter/MachO/MachONormalizedFileBinaryReader.cpp ---------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

///
/// \file For mach-o object files, this implementation converts from
/// mach-o on-disk binary format to in-memory normalized mach-o.
///
///                 +---------------+
///                 | binary mach-o |
///                 +---------------+
///                        |
///                        |
///                        v
///                  +------------+
///                  | normalized |
///                  +------------+

#include "ArchHandler.h"
#include "MachONormalizedFile.h"
#include "MachONormalizedFileBinaryUtils.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/Error.h"
#include "lld/Core/SharedLibraryFile.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <system_error>

using namespace llvm::MachO;
using llvm::object::ExportEntry;
using llvm::file_magic;
using llvm::object::MachOObjectFile;

namespace lld {
namespace mach_o {
namespace normalized {

// Utility to call a lambda expression on each load command.
static llvm::Error forEachLoadCommand(
    StringRef lcRange, unsigned lcCount, bool isBig, bool is64,
    std::function<bool(uint32_t cmd, uint32_t size, const char *lc)> func) {
  const char* p = lcRange.begin();
  for (unsigned i=0; i < lcCount; ++i) {
    const load_command *lc = reinterpret_cast<const load_command*>(p);
    load_command lcCopy;
    const load_command *slc = lc;
    if (isBig != llvm::sys::IsBigEndianHost) {
      memcpy(&lcCopy, lc, sizeof(load_command));
      swapStruct(lcCopy);
      slc = &lcCopy;
    }
    if ( (p + slc->cmdsize) > lcRange.end() )
      return llvm::make_error<GenericError>("Load command exceeds range");

    if (func(slc->cmd, slc->cmdsize, p))
      return llvm::Error::success();

    p += slc->cmdsize;
  }

  return llvm::Error::success();
}

static std::error_code appendRelocations(Relocations &relocs, StringRef buffer,
                                         bool bigEndian,
                                         uint32_t reloff, uint32_t nreloc) {
  if ((reloff + nreloc*8) > buffer.size())
    return make_error_code(llvm::errc::executable_format_error);
  const any_relocation_info* relocsArray =
            reinterpret_cast<const any_relocation_info*>(buffer.begin()+reloff);

  for(uint32_t i=0; i < nreloc; ++i) {
    relocs.push_back(unpackRelocation(relocsArray[i], bigEndian));
  }
  return std::error_code();
}

static std::error_code
appendIndirectSymbols(IndirectSymbols &isyms, StringRef buffer, bool isBig,
                      uint32_t istOffset, uint32_t istCount,
                      uint32_t startIndex, uint32_t count) {
  if ((istOffset + istCount*4) > buffer.size())
    return make_error_code(llvm::errc::executable_format_error);
  if (startIndex+count  > istCount)
    return make_error_code(llvm::errc::executable_format_error);
  const uint8_t *indirectSymbolArray = (const uint8_t *)buffer.data();

  for(uint32_t i=0; i < count; ++i) {
    isyms.push_back(read32(
        indirectSymbolArray + (startIndex + i) * sizeof(uint32_t), isBig));
  }
  return std::error_code();
}


template <typename T> static T readBigEndian(T t) {
  if (llvm::sys::IsLittleEndianHost)
    llvm::sys::swapByteOrder(t);
  return t;
}


static bool isMachOHeader(const mach_header *mh, bool &is64, bool &isBig) {
  switch (read32(&mh->magic, false)) {
  case llvm::MachO::MH_MAGIC:
    is64 = false;
    isBig = false;
    return true;
  case llvm::MachO::MH_MAGIC_64:
    is64 = true;
    isBig = false;
    return true;
  case llvm::MachO::MH_CIGAM:
    is64 = false;
    isBig = true;
    return true;
  case llvm::MachO::MH_CIGAM_64:
    is64 = true;
    isBig = true;
    return true;
  default:
    return false;
  }
}


bool isThinObjectFile(StringRef path, MachOLinkingContext::Arch &arch) {
  // Try opening and mapping file at path.
  ErrorOr<std::unique_ptr<MemoryBuffer>> b = MemoryBuffer::getFileOrSTDIN(path);
  if (b.getError())
    return false;

  // If file length < 32 it is too small to be mach-o object file.
  StringRef fileBuffer = b->get()->getBuffer();
  if (fileBuffer.size() < 32)
    return false;

  // If file buffer does not start with MH_MAGIC (and variants), not obj file.
  const mach_header *mh = reinterpret_cast<const mach_header *>(
                                                            fileBuffer.begin());
  bool is64, isBig;
  if (!isMachOHeader(mh, is64, isBig))
    return false;

  // If not MH_OBJECT, not object file.
  if (read32(&mh->filetype, isBig) != MH_OBJECT)
    return false;

  // Lookup up arch from cpu/subtype pair.
  arch = MachOLinkingContext::archFromCpuType(
      read32(&mh->cputype, isBig),
      read32(&mh->cpusubtype, isBig));
  return true;
}

bool sliceFromFatFile(MemoryBufferRef mb, MachOLinkingContext::Arch arch,
                      uint32_t &offset, uint32_t &size) {
  const char *start = mb.getBufferStart();
  const llvm::MachO::fat_header *fh =
      reinterpret_cast<const llvm::MachO::fat_header *>(start);
  if (readBigEndian(fh->magic) != llvm::MachO::FAT_MAGIC)
    return false;
  uint32_t nfat_arch = readBigEndian(fh->nfat_arch);
  const fat_arch *fstart =
      reinterpret_cast<const fat_arch *>(start + sizeof(fat_header));
  const fat_arch *fend =
      reinterpret_cast<const fat_arch *>(start + sizeof(fat_header) +
                                         sizeof(fat_arch) * nfat_arch);
  const uint32_t reqCpuType = MachOLinkingContext::cpuTypeFromArch(arch);
  const uint32_t reqCpuSubtype = MachOLinkingContext::cpuSubtypeFromArch(arch);
  for (const fat_arch *fa = fstart; fa < fend; ++fa) {
    if ((readBigEndian(fa->cputype) == reqCpuType) &&
        (readBigEndian(fa->cpusubtype) == reqCpuSubtype)) {
      offset = readBigEndian(fa->offset);
      size = readBigEndian(fa->size);
      if ((offset + size) > mb.getBufferSize())
        return false;
      return true;
    }
  }
  return false;
}

/// Reads a mach-o file and produces an in-memory normalized view.
llvm::Expected<std::unique_ptr<NormalizedFile>>
readBinary(std::unique_ptr<MemoryBuffer> &mb,
           const MachOLinkingContext::Arch arch) {
  // Make empty NormalizedFile.
  std::unique_ptr<NormalizedFile> f(new NormalizedFile());

  const char *start = mb->getBufferStart();
  size_t objSize = mb->getBufferSize();
  const mach_header *mh = reinterpret_cast<const mach_header *>(start);

  uint32_t sliceOffset;
  uint32_t sliceSize;
  if (sliceFromFatFile(mb->getMemBufferRef(), arch, sliceOffset, sliceSize)) {
    start = &start[sliceOffset];
    objSize = sliceSize;
    mh = reinterpret_cast<const mach_header *>(start);
  }

  // Determine endianness and pointer size for mach-o file.
  bool is64, isBig;
  if (!isMachOHeader(mh, is64, isBig))
    return llvm::make_error<GenericError>("File is not a mach-o");

  // Endian swap header, if needed.
  mach_header headerCopy;
  const mach_header *smh = mh;
  if (isBig != llvm::sys::IsBigEndianHost) {
    memcpy(&headerCopy, mh, sizeof(mach_header));
    swapStruct(headerCopy);
    smh = &headerCopy;
  }

  // Validate head and load commands fit in buffer.
  const uint32_t lcCount = smh->ncmds;
  const char *lcStart =
      start + (is64 ? sizeof(mach_header_64) : sizeof(mach_header));
  StringRef lcRange(lcStart, smh->sizeofcmds);
  if (lcRange.end() > (start + objSize))
    return llvm::make_error<GenericError>("Load commands exceed file size");

  // Get architecture from mach_header.
  f->arch = MachOLinkingContext::archFromCpuType(smh->cputype, smh->cpusubtype);
  if (f->arch != arch) {
    return llvm::make_error<GenericError>(
                                  Twine("file is wrong architecture. Expected "
                                  "(" + MachOLinkingContext::nameFromArch(arch)
                                  + ") found ("
                                  + MachOLinkingContext::nameFromArch(f->arch)
                                  + ")" ));
  }
  // Copy file type and flags
  f->fileType = HeaderFileType(smh->filetype);
  f->flags = smh->flags;


  // Pre-scan load commands looking for indirect symbol table.
  uint32_t indirectSymbolTableOffset = 0;
  uint32_t indirectSymbolTableCount = 0;
  auto ec = forEachLoadCommand(lcRange, lcCount, isBig, is64,
                               [&](uint32_t cmd, uint32_t size,
                                   const char *lc) -> bool {
    if (cmd == LC_DYSYMTAB) {
      const dysymtab_command *d = reinterpret_cast<const dysymtab_command*>(lc);
      indirectSymbolTableOffset = read32(&d->indirectsymoff, isBig);
      indirectSymbolTableCount = read32(&d->nindirectsyms, isBig);
      return true;
    }
    return false;
  });
  if (ec)
    return std::move(ec);

  // Walk load commands looking for segments/sections and the symbol table.
  const data_in_code_entry *dataInCode = nullptr;
  const dyld_info_command *dyldInfo = nullptr;
  uint32_t dataInCodeSize = 0;
  ec = forEachLoadCommand(lcRange, lcCount, isBig, is64,
                    [&] (uint32_t cmd, uint32_t size, const char* lc) -> bool {
    switch(cmd) {
    case LC_SEGMENT_64:
      if (is64) {
        const segment_command_64 *seg =
                              reinterpret_cast<const segment_command_64*>(lc);
        const unsigned sectionCount = read32(&seg->nsects, isBig);
        const section_64 *sects = reinterpret_cast<const section_64*>
                                  (lc + sizeof(segment_command_64));
        const unsigned lcSize = sizeof(segment_command_64)
                                              + sectionCount*sizeof(section_64);
        // Verify sections don't extend beyond end of segment load command.
        if (lcSize > size)
          return true;
        for (unsigned i=0; i < sectionCount; ++i) {
          const section_64 *sect = &sects[i];
          Section section;
          section.segmentName = getString16(sect->segname);
          section.sectionName = getString16(sect->sectname);
          section.type = (SectionType)(read32(&sect->flags, isBig) &
                                       SECTION_TYPE);
          section.attributes  = read32(&sect->flags, isBig) & SECTION_ATTRIBUTES;
          section.alignment   = 1 << read32(&sect->align, isBig);
          section.address     = read64(&sect->addr, isBig);
          const uint8_t *content =
            (const uint8_t *)start + read32(&sect->offset, isBig);
          size_t contentSize = read64(&sect->size, isBig);
          // Note: this assign() is copying the content bytes.  Ideally,
          // we can use a custom allocator for vector to avoid the copy.
          section.content = llvm::makeArrayRef(content, contentSize);
          appendRelocations(section.relocations, mb->getBuffer(), isBig,
                            read32(&sect->reloff, isBig),
                            read32(&sect->nreloc, isBig));
          if (section.type == S_NON_LAZY_SYMBOL_POINTERS) {
            appendIndirectSymbols(section.indirectSymbols, mb->getBuffer(),
                                  isBig,
                                  indirectSymbolTableOffset,
                                  indirectSymbolTableCount,
                                  read32(&sect->reserved1, isBig),
                                  contentSize/4);
          }
          f->sections.push_back(section);
        }
      }
      break;
    case LC_SEGMENT:
      if (!is64) {
        const segment_command *seg =
                              reinterpret_cast<const segment_command*>(lc);
        const unsigned sectionCount = read32(&seg->nsects, isBig);
        const section *sects = reinterpret_cast<const section*>
                                  (lc + sizeof(segment_command));
        const unsigned lcSize = sizeof(segment_command)
                                              + sectionCount*sizeof(section);
        // Verify sections don't extend beyond end of segment load command.
        if (lcSize > size)
          return true;
        for (unsigned i=0; i < sectionCount; ++i) {
          const section *sect = &sects[i];
          Section section;
          section.segmentName = getString16(sect->segname);
          section.sectionName = getString16(sect->sectname);
          section.type = (SectionType)(read32(&sect->flags, isBig) &
                                       SECTION_TYPE);
          section.attributes =
              read32((const uint8_t *)&sect->flags, isBig) & SECTION_ATTRIBUTES;
          section.alignment   = 1 << read32(&sect->align, isBig);
          section.address     = read32(&sect->addr, isBig);
          const uint8_t *content =
            (const uint8_t *)start + read32(&sect->offset, isBig);
          size_t contentSize = read32(&sect->size, isBig);
          // Note: this assign() is copying the content bytes.  Ideally,
          // we can use a custom allocator for vector to avoid the copy.
          section.content = llvm::makeArrayRef(content, contentSize);
          appendRelocations(section.relocations, mb->getBuffer(), isBig,
                            read32(&sect->reloff, isBig),
                            read32(&sect->nreloc, isBig));
          if (section.type == S_NON_LAZY_SYMBOL_POINTERS) {
            appendIndirectSymbols(
                section.indirectSymbols, mb->getBuffer(), isBig,
                indirectSymbolTableOffset, indirectSymbolTableCount,
                read32(&sect->reserved1, isBig), contentSize / 4);
          }
          f->sections.push_back(section);
        }
      }
      break;
    case LC_SYMTAB: {
      const symtab_command *st = reinterpret_cast<const symtab_command*>(lc);
      const char *strings = start + read32(&st->stroff, isBig);
      const uint32_t strSize = read32(&st->strsize, isBig);
      // Validate string pool and symbol table all in buffer.
      if (read32((const uint8_t *)&st->stroff, isBig) +
              read32((const uint8_t *)&st->strsize, isBig) >
          objSize)
        return true;
      if (is64) {
        const uint32_t symOffset = read32(&st->symoff, isBig);
        const uint32_t symCount = read32(&st->nsyms, isBig);
        if ( symOffset+(symCount*sizeof(nlist_64)) > objSize)
          return true;
        const nlist_64 *symbols =
            reinterpret_cast<const nlist_64 *>(start + symOffset);
        // Convert each nlist_64 to a lld::mach_o::normalized::Symbol.
        for(uint32_t i=0; i < symCount; ++i) {
          nlist_64 tempSym;
          memcpy(&tempSym, &symbols[i], sizeof(nlist_64));
          const nlist_64 *sin = &tempSym;
          if (isBig != llvm::sys::IsBigEndianHost)
            swapStruct(tempSym);
          Symbol sout;
          if (sin->n_strx > strSize)
            return true;
          sout.name  = &strings[sin->n_strx];
          sout.type = static_cast<NListType>(sin->n_type & (N_STAB|N_TYPE));
          sout.scope = (sin->n_type & (N_PEXT|N_EXT));
          sout.sect  = sin->n_sect;
          sout.desc  = sin->n_desc;
          sout.value = sin->n_value;
          if (sin->n_type & N_STAB)
            f->stabsSymbols.push_back(sout);
          else if (sout.type == N_UNDF)
            f->undefinedSymbols.push_back(sout);
          else if (sin->n_type & N_EXT)
            f->globalSymbols.push_back(sout);
          else
            f->localSymbols.push_back(sout);
        }
      } else {
        const uint32_t symOffset = read32(&st->symoff, isBig);
        const uint32_t symCount = read32(&st->nsyms, isBig);
        if ( symOffset+(symCount*sizeof(nlist)) > objSize)
          return true;
        const nlist *symbols =
            reinterpret_cast<const nlist *>(start + symOffset);
        // Convert each nlist to a lld::mach_o::normalized::Symbol.
        for(uint32_t i=0; i < symCount; ++i) {
          const nlist *sin = &symbols[i];
          nlist tempSym;
          if (isBig != llvm::sys::IsBigEndianHost) {
            tempSym = *sin; swapStruct(tempSym); sin = &tempSym;
          }
          Symbol sout;
          if (sin->n_strx > strSize)
            return true;
          sout.name  = &strings[sin->n_strx];
          sout.type  = (NListType)(sin->n_type & N_TYPE);
          sout.scope = (sin->n_type & (N_PEXT|N_EXT));
          sout.sect  = sin->n_sect;
          sout.desc  = sin->n_desc;
          sout.value = sin->n_value;
          if (sout.type == N_UNDF)
            f->undefinedSymbols.push_back(sout);
          else if (sout.scope == (SymbolScope)N_EXT)
            f->globalSymbols.push_back(sout);
          else if (sin->n_type & N_STAB)
            f->stabsSymbols.push_back(sout);
          else
            f->localSymbols.push_back(sout);
        }
      }
      }
      break;
    case LC_ID_DYLIB: {
      const dylib_command *dl = reinterpret_cast<const dylib_command*>(lc);
      f->installName = lc + read32(&dl->dylib.name, isBig);
      f->currentVersion = read32(&dl->dylib.current_version, isBig);
      f->compatVersion = read32(&dl->dylib.compatibility_version, isBig);
      }
      break;
    case LC_DATA_IN_CODE: {
      const linkedit_data_command *ldc =
                            reinterpret_cast<const linkedit_data_command*>(lc);
      dataInCode = reinterpret_cast<const data_in_code_entry *>(
          start + read32(&ldc->dataoff, isBig));
      dataInCodeSize = read32(&ldc->datasize, isBig);
      }
      break;
    case LC_LOAD_DYLIB:
    case LC_LOAD_WEAK_DYLIB:
    case LC_REEXPORT_DYLIB:
    case LC_LOAD_UPWARD_DYLIB: {
      const dylib_command *dl = reinterpret_cast<const dylib_command*>(lc);
      DependentDylib entry;
      entry.path = lc + read32(&dl->dylib.name, isBig);
      entry.kind = LoadCommandType(cmd);
      entry.compatVersion = read32(&dl->dylib.compatibility_version, isBig);
      entry.currentVersion = read32(&dl->dylib.current_version, isBig);
      f->dependentDylibs.push_back(entry);
     }
      break;
    case LC_RPATH: {
      const rpath_command *rpc = reinterpret_cast<const rpath_command *>(lc);
      f->rpaths.push_back(lc + read32(&rpc->path, isBig));
     }
      break;
    case LC_DYLD_INFO:
    case LC_DYLD_INFO_ONLY:
      dyldInfo = reinterpret_cast<const dyld_info_command*>(lc);
      break;
    case LC_VERSION_MIN_MACOSX:
    case LC_VERSION_MIN_IPHONEOS:
    case LC_VERSION_MIN_WATCHOS:
    case LC_VERSION_MIN_TVOS:
      // If we are emitting an object file, then we may take the load command
      // kind from these commands and pass it on to the output
      // file.
      f->minOSVersionKind = (LoadCommandType)cmd;
      break;
    }
    return false;
  });
  if (ec)
    return std::move(ec);

  if (dataInCode) {
    // Convert on-disk data_in_code_entry array to DataInCode vector.
    for (unsigned i=0; i < dataInCodeSize/sizeof(data_in_code_entry); ++i) {
      DataInCode entry;
      entry.offset = read32(&dataInCode[i].offset, isBig);
      entry.length = read16(&dataInCode[i].length, isBig);
      entry.kind =
          (DataRegionType)read16((const uint8_t *)&dataInCode[i].kind, isBig);
      f->dataInCode.push_back(entry);
    }
  }

  if (dyldInfo) {
    // If any exports, extract and add to normalized exportInfo vector.
    if (dyldInfo->export_size) {
      const uint8_t *trieStart = reinterpret_cast<const uint8_t *>(
          start + read32(&dyldInfo->export_off, isBig));
      ArrayRef<uint8_t> trie(trieStart, read32(&dyldInfo->export_size, isBig));
      Error Err = Error::success();
      for (const ExportEntry &trieExport : MachOObjectFile::exports(Err, trie)) {
        Export normExport;
        normExport.name = trieExport.name().copy(f->ownedAllocations);
        normExport.offset = trieExport.address();
        normExport.kind = ExportSymbolKind(trieExport.flags() & EXPORT_SYMBOL_FLAGS_KIND_MASK);
        normExport.flags = trieExport.flags() & ~EXPORT_SYMBOL_FLAGS_KIND_MASK;
        normExport.otherOffset = trieExport.other();
        if (!trieExport.otherName().empty())
          normExport.otherName = trieExport.otherName().copy(f->ownedAllocations);
        f->exportInfo.push_back(normExport);
      }
      if (Err)
        return std::move(Err);
    }
  }

  return std::move(f);
}

class MachOObjectReader : public Reader {
public:
  MachOObjectReader(MachOLinkingContext &ctx) : _ctx(ctx) {}

  bool canParse(file_magic magic, MemoryBufferRef mb) const override {
    return (magic == file_magic::macho_object && mb.getBufferSize() > 32);
  }

  ErrorOr<std::unique_ptr<File>>
  loadFile(std::unique_ptr<MemoryBuffer> mb,
           const Registry &registry) const override {
    std::unique_ptr<File> ret =
      llvm::make_unique<MachOFile>(std::move(mb), &_ctx);
    return std::move(ret);
  }

private:
  MachOLinkingContext &_ctx;
};

class MachODylibReader : public Reader {
public:
  MachODylibReader(MachOLinkingContext &ctx) : _ctx(ctx) {}

  bool canParse(file_magic magic, MemoryBufferRef mb) const override {
    switch (magic) {
    case file_magic::macho_dynamically_linked_shared_lib:
    case file_magic::macho_dynamically_linked_shared_lib_stub:
      return mb.getBufferSize() > 32;
    default:
      return false;
    }
  }

  ErrorOr<std::unique_ptr<File>>
  loadFile(std::unique_ptr<MemoryBuffer> mb,
           const Registry &registry) const override {
    std::unique_ptr<File> ret =
        llvm::make_unique<MachODylibFile>(std::move(mb), &_ctx);
    return std::move(ret);
  }

private:
  MachOLinkingContext &_ctx;
};

} // namespace normalized
} // namespace mach_o

void Registry::addSupportMachOObjects(MachOLinkingContext &ctx) {
  MachOLinkingContext::Arch arch = ctx.arch();
  add(std::unique_ptr<Reader>(new mach_o::normalized::MachOObjectReader(ctx)));
  add(std::unique_ptr<Reader>(new mach_o::normalized::MachODylibReader(ctx)));
  addKindTable(Reference::KindNamespace::mach_o, ctx.archHandler().kindArch(),
               ctx.archHandler().kindStrings());
  add(std::unique_ptr<YamlIOTaggedDocumentHandler>(
                           new mach_o::MachOYamlIOTaggedDocumentHandler(arch)));
}


} // namespace lld
