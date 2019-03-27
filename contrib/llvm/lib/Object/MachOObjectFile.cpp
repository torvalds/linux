//===- MachOObjectFile.cpp - Mach-O object file binding -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachOObjectFile class, which binds the MachOObject
// class to the generic ObjectFile wrapper.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <system_error>

using namespace llvm;
using namespace object;

namespace {

  struct section_base {
    char sectname[16];
    char segname[16];
  };

} // end anonymous namespace

static Error malformedError(const Twine &Msg) {
  return make_error<GenericBinaryError>("truncated or malformed object (" +
                                            Msg + ")",
                                        object_error::parse_failed);
}

// FIXME: Replace all uses of this function with getStructOrErr.
template <typename T>
static T getStruct(const MachOObjectFile &O, const char *P) {
  // Don't read before the beginning or past the end of the file
  if (P < O.getData().begin() || P + sizeof(T) > O.getData().end())
    report_fatal_error("Malformed MachO file.");

  T Cmd;
  memcpy(&Cmd, P, sizeof(T));
  if (O.isLittleEndian() != sys::IsLittleEndianHost)
    MachO::swapStruct(Cmd);
  return Cmd;
}

template <typename T>
static Expected<T> getStructOrErr(const MachOObjectFile &O, const char *P) {
  // Don't read before the beginning or past the end of the file
  if (P < O.getData().begin() || P + sizeof(T) > O.getData().end())
    return malformedError("Structure read out-of-range");

  T Cmd;
  memcpy(&Cmd, P, sizeof(T));
  if (O.isLittleEndian() != sys::IsLittleEndianHost)
    MachO::swapStruct(Cmd);
  return Cmd;
}

static const char *
getSectionPtr(const MachOObjectFile &O, MachOObjectFile::LoadCommandInfo L,
              unsigned Sec) {
  uintptr_t CommandAddr = reinterpret_cast<uintptr_t>(L.Ptr);

  bool Is64 = O.is64Bit();
  unsigned SegmentLoadSize = Is64 ? sizeof(MachO::segment_command_64) :
                                    sizeof(MachO::segment_command);
  unsigned SectionSize = Is64 ? sizeof(MachO::section_64) :
                                sizeof(MachO::section);

  uintptr_t SectionAddr = CommandAddr + SegmentLoadSize + Sec * SectionSize;
  return reinterpret_cast<const char*>(SectionAddr);
}

static const char *getPtr(const MachOObjectFile &O, size_t Offset) {
  assert(Offset <= O.getData().size());
  return O.getData().data() + Offset;
}

static MachO::nlist_base
getSymbolTableEntryBase(const MachOObjectFile &O, DataRefImpl DRI) {
  const char *P = reinterpret_cast<const char *>(DRI.p);
  return getStruct<MachO::nlist_base>(O, P);
}

static StringRef parseSegmentOrSectionName(const char *P) {
  if (P[15] == 0)
    // Null terminated.
    return P;
  // Not null terminated, so this is a 16 char string.
  return StringRef(P, 16);
}

static unsigned getCPUType(const MachOObjectFile &O) {
  return O.getHeader().cputype;
}

static uint32_t
getPlainRelocationAddress(const MachO::any_relocation_info &RE) {
  return RE.r_word0;
}

static unsigned
getScatteredRelocationAddress(const MachO::any_relocation_info &RE) {
  return RE.r_word0 & 0xffffff;
}

static bool getPlainRelocationPCRel(const MachOObjectFile &O,
                                    const MachO::any_relocation_info &RE) {
  if (O.isLittleEndian())
    return (RE.r_word1 >> 24) & 1;
  return (RE.r_word1 >> 7) & 1;
}

static bool
getScatteredRelocationPCRel(const MachO::any_relocation_info &RE) {
  return (RE.r_word0 >> 30) & 1;
}

static unsigned getPlainRelocationLength(const MachOObjectFile &O,
                                         const MachO::any_relocation_info &RE) {
  if (O.isLittleEndian())
    return (RE.r_word1 >> 25) & 3;
  return (RE.r_word1 >> 5) & 3;
}

static unsigned
getScatteredRelocationLength(const MachO::any_relocation_info &RE) {
  return (RE.r_word0 >> 28) & 3;
}

static unsigned getPlainRelocationType(const MachOObjectFile &O,
                                       const MachO::any_relocation_info &RE) {
  if (O.isLittleEndian())
    return RE.r_word1 >> 28;
  return RE.r_word1 & 0xf;
}

static uint32_t getSectionFlags(const MachOObjectFile &O,
                                DataRefImpl Sec) {
  if (O.is64Bit()) {
    MachO::section_64 Sect = O.getSection64(Sec);
    return Sect.flags;
  }
  MachO::section Sect = O.getSection(Sec);
  return Sect.flags;
}

static Expected<MachOObjectFile::LoadCommandInfo>
getLoadCommandInfo(const MachOObjectFile &Obj, const char *Ptr,
                   uint32_t LoadCommandIndex) {
  if (auto CmdOrErr = getStructOrErr<MachO::load_command>(Obj, Ptr)) {
    if (CmdOrErr->cmdsize + Ptr > Obj.getData().end())
      return malformedError("load command " + Twine(LoadCommandIndex) +
                            " extends past end of file");
    if (CmdOrErr->cmdsize < 8)
      return malformedError("load command " + Twine(LoadCommandIndex) +
                            " with size less than 8 bytes");
    return MachOObjectFile::LoadCommandInfo({Ptr, *CmdOrErr});
  } else
    return CmdOrErr.takeError();
}

static Expected<MachOObjectFile::LoadCommandInfo>
getFirstLoadCommandInfo(const MachOObjectFile &Obj) {
  unsigned HeaderSize = Obj.is64Bit() ? sizeof(MachO::mach_header_64)
                                      : sizeof(MachO::mach_header);
  if (sizeof(MachO::load_command) > Obj.getHeader().sizeofcmds)
    return malformedError("load command 0 extends past the end all load "
                          "commands in the file");
  return getLoadCommandInfo(Obj, getPtr(Obj, HeaderSize), 0);
}

static Expected<MachOObjectFile::LoadCommandInfo>
getNextLoadCommandInfo(const MachOObjectFile &Obj, uint32_t LoadCommandIndex,
                       const MachOObjectFile::LoadCommandInfo &L) {
  unsigned HeaderSize = Obj.is64Bit() ? sizeof(MachO::mach_header_64)
                                      : sizeof(MachO::mach_header);
  if (L.Ptr + L.C.cmdsize + sizeof(MachO::load_command) >
      Obj.getData().data() + HeaderSize + Obj.getHeader().sizeofcmds)
    return malformedError("load command " + Twine(LoadCommandIndex + 1) +
                          " extends past the end all load commands in the file");
  return getLoadCommandInfo(Obj, L.Ptr + L.C.cmdsize, LoadCommandIndex + 1);
}

template <typename T>
static void parseHeader(const MachOObjectFile &Obj, T &Header,
                        Error &Err) {
  if (sizeof(T) > Obj.getData().size()) {
    Err = malformedError("the mach header extends past the end of the "
                         "file");
    return;
  }
  if (auto HeaderOrErr = getStructOrErr<T>(Obj, getPtr(Obj, 0)))
    Header = *HeaderOrErr;
  else
    Err = HeaderOrErr.takeError();
}

// This is used to check for overlapping of Mach-O elements.
struct MachOElement {
  uint64_t Offset;
  uint64_t Size;
  const char *Name;
};

static Error checkOverlappingElement(std::list<MachOElement> &Elements,
                                     uint64_t Offset, uint64_t Size,
                                     const char *Name) {
  if (Size == 0)
    return Error::success();

  for (auto it=Elements.begin() ; it != Elements.end(); ++it) {
    auto E = *it;
    if ((Offset >= E.Offset && Offset < E.Offset + E.Size) ||
        (Offset + Size > E.Offset && Offset + Size < E.Offset + E.Size) ||
        (Offset <= E.Offset && Offset + Size >= E.Offset + E.Size))
      return malformedError(Twine(Name) + " at offset " + Twine(Offset) +
                            " with a size of " + Twine(Size) + ", overlaps " +
                            E.Name + " at offset " + Twine(E.Offset) + " with "
                            "a size of " + Twine(E.Size));
    auto nt = it;
    nt++;
    if (nt != Elements.end()) {
      auto N = *nt;
      if (Offset + Size <= N.Offset) {
        Elements.insert(nt, {Offset, Size, Name});
        return Error::success();
      }
    }
  }
  Elements.push_back({Offset, Size, Name});
  return Error::success();
}

// Parses LC_SEGMENT or LC_SEGMENT_64 load command, adds addresses of all
// sections to \param Sections, and optionally sets
// \param IsPageZeroSegment to true.
template <typename Segment, typename Section>
static Error parseSegmentLoadCommand(
    const MachOObjectFile &Obj, const MachOObjectFile::LoadCommandInfo &Load,
    SmallVectorImpl<const char *> &Sections, bool &IsPageZeroSegment,
    uint32_t LoadCommandIndex, const char *CmdName, uint64_t SizeOfHeaders,
    std::list<MachOElement> &Elements) {
  const unsigned SegmentLoadSize = sizeof(Segment);
  if (Load.C.cmdsize < SegmentLoadSize)
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " " + CmdName + " cmdsize too small");
  if (auto SegOrErr = getStructOrErr<Segment>(Obj, Load.Ptr)) {
    Segment S = SegOrErr.get();
    const unsigned SectionSize = sizeof(Section);
    uint64_t FileSize = Obj.getData().size();
    if (S.nsects > std::numeric_limits<uint32_t>::max() / SectionSize ||
        S.nsects * SectionSize > Load.C.cmdsize - SegmentLoadSize)
      return malformedError("load command " + Twine(LoadCommandIndex) +
                            " inconsistent cmdsize in " + CmdName +
                            " for the number of sections");
    for (unsigned J = 0; J < S.nsects; ++J) {
      const char *Sec = getSectionPtr(Obj, Load, J);
      Sections.push_back(Sec);
      Section s = getStruct<Section>(Obj, Sec);
      if (Obj.getHeader().filetype != MachO::MH_DYLIB_STUB &&
          Obj.getHeader().filetype != MachO::MH_DSYM &&
          s.flags != MachO::S_ZEROFILL &&
          s.flags != MachO::S_THREAD_LOCAL_ZEROFILL &&
          s.offset > FileSize)
        return malformedError("offset field of section " + Twine(J) + " in " +
                              CmdName + " command " + Twine(LoadCommandIndex) +
                              " extends past the end of the file");
      if (Obj.getHeader().filetype != MachO::MH_DYLIB_STUB &&
          Obj.getHeader().filetype != MachO::MH_DSYM &&
          s.flags != MachO::S_ZEROFILL &&
          s.flags != MachO::S_THREAD_LOCAL_ZEROFILL && S.fileoff == 0 &&
          s.offset < SizeOfHeaders && s.size != 0)
        return malformedError("offset field of section " + Twine(J) + " in " +
                              CmdName + " command " + Twine(LoadCommandIndex) +
                              " not past the headers of the file");
      uint64_t BigSize = s.offset;
      BigSize += s.size;
      if (Obj.getHeader().filetype != MachO::MH_DYLIB_STUB &&
          Obj.getHeader().filetype != MachO::MH_DSYM &&
          s.flags != MachO::S_ZEROFILL &&
          s.flags != MachO::S_THREAD_LOCAL_ZEROFILL &&
          BigSize > FileSize)
        return malformedError("offset field plus size field of section " +
                              Twine(J) + " in " + CmdName + " command " +
                              Twine(LoadCommandIndex) +
                              " extends past the end of the file");
      if (Obj.getHeader().filetype != MachO::MH_DYLIB_STUB &&
          Obj.getHeader().filetype != MachO::MH_DSYM &&
          s.flags != MachO::S_ZEROFILL &&
          s.flags != MachO::S_THREAD_LOCAL_ZEROFILL &&
          s.size > S.filesize)
        return malformedError("size field of section " +
                              Twine(J) + " in " + CmdName + " command " +
                              Twine(LoadCommandIndex) +
                              " greater than the segment");
      if (Obj.getHeader().filetype != MachO::MH_DYLIB_STUB &&
          Obj.getHeader().filetype != MachO::MH_DSYM && s.size != 0 &&
          s.addr < S.vmaddr)
        return malformedError("addr field of section " + Twine(J) + " in " +
                              CmdName + " command " + Twine(LoadCommandIndex) +
                              " less than the segment's vmaddr");
      BigSize = s.addr;
      BigSize += s.size;
      uint64_t BigEnd = S.vmaddr;
      BigEnd += S.vmsize;
      if (S.vmsize != 0 && s.size != 0 && BigSize > BigEnd)
        return malformedError("addr field plus size of section " + Twine(J) +
                              " in " + CmdName + " command " +
                              Twine(LoadCommandIndex) +
                              " greater than than "
                              "the segment's vmaddr plus vmsize");
      if (Obj.getHeader().filetype != MachO::MH_DYLIB_STUB &&
          Obj.getHeader().filetype != MachO::MH_DSYM &&
          s.flags != MachO::S_ZEROFILL &&
          s.flags != MachO::S_THREAD_LOCAL_ZEROFILL)
        if (Error Err = checkOverlappingElement(Elements, s.offset, s.size,
                                                "section contents"))
          return Err;
      if (s.reloff > FileSize)
        return malformedError("reloff field of section " + Twine(J) + " in " +
                              CmdName + " command " + Twine(LoadCommandIndex) +
                              " extends past the end of the file");
      BigSize = s.nreloc;
      BigSize *= sizeof(struct MachO::relocation_info);
      BigSize += s.reloff;
      if (BigSize > FileSize)
        return malformedError("reloff field plus nreloc field times sizeof("
                              "struct relocation_info) of section " +
                              Twine(J) + " in " + CmdName + " command " +
                              Twine(LoadCommandIndex) +
                              " extends past the end of the file");
      if (Error Err = checkOverlappingElement(Elements, s.reloff, s.nreloc *
                                              sizeof(struct
                                              MachO::relocation_info),
                                              "section relocation entries"))
        return Err;
    }
    if (S.fileoff > FileSize)
      return malformedError("load command " + Twine(LoadCommandIndex) +
                            " fileoff field in " + CmdName +
                            " extends past the end of the file");
    uint64_t BigSize = S.fileoff;
    BigSize += S.filesize;
    if (BigSize > FileSize)
      return malformedError("load command " + Twine(LoadCommandIndex) +
                            " fileoff field plus filesize field in " +
                            CmdName + " extends past the end of the file");
    if (S.vmsize != 0 && S.filesize > S.vmsize)
      return malformedError("load command " + Twine(LoadCommandIndex) +
                            " filesize field in " + CmdName +
                            " greater than vmsize field");
    IsPageZeroSegment |= StringRef("__PAGEZERO").equals(S.segname);
  } else
    return SegOrErr.takeError();

  return Error::success();
}

static Error checkSymtabCommand(const MachOObjectFile &Obj,
                                const MachOObjectFile::LoadCommandInfo &Load,
                                uint32_t LoadCommandIndex,
                                const char **SymtabLoadCmd,
                                std::list<MachOElement> &Elements) {
  if (Load.C.cmdsize < sizeof(MachO::symtab_command))
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_SYMTAB cmdsize too small");
  if (*SymtabLoadCmd != nullptr)
    return malformedError("more than one LC_SYMTAB command");
  MachO::symtab_command Symtab =
    getStruct<MachO::symtab_command>(Obj, Load.Ptr);
  if (Symtab.cmdsize != sizeof(MachO::symtab_command))
    return malformedError("LC_SYMTAB command " + Twine(LoadCommandIndex) +
                          " has incorrect cmdsize");
  uint64_t FileSize = Obj.getData().size();
  if (Symtab.symoff > FileSize)
    return malformedError("symoff field of LC_SYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end "
                          "of the file");
  uint64_t SymtabSize = Symtab.nsyms;
  const char *struct_nlist_name;
  if (Obj.is64Bit()) {
    SymtabSize *= sizeof(MachO::nlist_64);
    struct_nlist_name = "struct nlist_64";
  } else {
    SymtabSize *= sizeof(MachO::nlist);
    struct_nlist_name = "struct nlist";
  }
  uint64_t BigSize = SymtabSize;
  BigSize += Symtab.symoff;
  if (BigSize > FileSize)
    return malformedError("symoff field plus nsyms field times sizeof(" +
                          Twine(struct_nlist_name) + ") of LC_SYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end "
                          "of the file");
  if (Error Err = checkOverlappingElement(Elements, Symtab.symoff, SymtabSize,
                                          "symbol table"))
    return Err;
  if (Symtab.stroff > FileSize)
    return malformedError("stroff field of LC_SYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end "
                          "of the file");
  BigSize = Symtab.stroff;
  BigSize += Symtab.strsize;
  if (BigSize > FileSize)
    return malformedError("stroff field plus strsize field of LC_SYMTAB "
                          "command " + Twine(LoadCommandIndex) + " extends "
                          "past the end of the file");
  if (Error Err = checkOverlappingElement(Elements, Symtab.stroff,
                                          Symtab.strsize, "string table"))
    return Err;
  *SymtabLoadCmd = Load.Ptr;
  return Error::success();
}

static Error checkDysymtabCommand(const MachOObjectFile &Obj,
                                  const MachOObjectFile::LoadCommandInfo &Load,
                                  uint32_t LoadCommandIndex,
                                  const char **DysymtabLoadCmd,
                                  std::list<MachOElement> &Elements) {
  if (Load.C.cmdsize < sizeof(MachO::dysymtab_command))
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_DYSYMTAB cmdsize too small");
  if (*DysymtabLoadCmd != nullptr)
    return malformedError("more than one LC_DYSYMTAB command");
  MachO::dysymtab_command Dysymtab =
    getStruct<MachO::dysymtab_command>(Obj, Load.Ptr);
  if (Dysymtab.cmdsize != sizeof(MachO::dysymtab_command))
    return malformedError("LC_DYSYMTAB command " + Twine(LoadCommandIndex) +
                          " has incorrect cmdsize");
  uint64_t FileSize = Obj.getData().size();
  if (Dysymtab.tocoff > FileSize)
    return malformedError("tocoff field of LC_DYSYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  uint64_t BigSize = Dysymtab.ntoc;
  BigSize *= sizeof(MachO::dylib_table_of_contents);
  BigSize += Dysymtab.tocoff;
  if (BigSize > FileSize)
    return malformedError("tocoff field plus ntoc field times sizeof(struct "
                          "dylib_table_of_contents) of LC_DYSYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, Dysymtab.tocoff,
                                          Dysymtab.ntoc * sizeof(struct
                                          MachO::dylib_table_of_contents),
                                          "table of contents"))
    return Err;
  if (Dysymtab.modtaboff > FileSize)
    return malformedError("modtaboff field of LC_DYSYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  BigSize = Dysymtab.nmodtab;
  const char *struct_dylib_module_name;
  uint64_t sizeof_modtab;
  if (Obj.is64Bit()) {
    sizeof_modtab = sizeof(MachO::dylib_module_64);
    struct_dylib_module_name = "struct dylib_module_64";
  } else {
    sizeof_modtab = sizeof(MachO::dylib_module);
    struct_dylib_module_name = "struct dylib_module";
  }
  BigSize *= sizeof_modtab;
  BigSize += Dysymtab.modtaboff;
  if (BigSize > FileSize)
    return malformedError("modtaboff field plus nmodtab field times sizeof(" +
                          Twine(struct_dylib_module_name) + ") of LC_DYSYMTAB "
                          "command " + Twine(LoadCommandIndex) + " extends "
                          "past the end of the file");
  if (Error Err = checkOverlappingElement(Elements, Dysymtab.modtaboff,
                                          Dysymtab.nmodtab * sizeof_modtab,
                                          "module table"))
    return Err;
  if (Dysymtab.extrefsymoff > FileSize)
    return malformedError("extrefsymoff field of LC_DYSYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  BigSize = Dysymtab.nextrefsyms;
  BigSize *= sizeof(MachO::dylib_reference);
  BigSize += Dysymtab.extrefsymoff;
  if (BigSize > FileSize)
    return malformedError("extrefsymoff field plus nextrefsyms field times "
                          "sizeof(struct dylib_reference) of LC_DYSYMTAB "
                          "command " + Twine(LoadCommandIndex) + " extends "
                          "past the end of the file");
  if (Error Err = checkOverlappingElement(Elements, Dysymtab.extrefsymoff,
                                          Dysymtab.nextrefsyms *
                                              sizeof(MachO::dylib_reference),
                                          "reference table"))
    return Err;
  if (Dysymtab.indirectsymoff > FileSize)
    return malformedError("indirectsymoff field of LC_DYSYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  BigSize = Dysymtab.nindirectsyms;
  BigSize *= sizeof(uint32_t);
  BigSize += Dysymtab.indirectsymoff;
  if (BigSize > FileSize)
    return malformedError("indirectsymoff field plus nindirectsyms field times "
                          "sizeof(uint32_t) of LC_DYSYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, Dysymtab.indirectsymoff,
                                          Dysymtab.nindirectsyms *
                                          sizeof(uint32_t),
                                          "indirect table"))
    return Err;
  if (Dysymtab.extreloff > FileSize)
    return malformedError("extreloff field of LC_DYSYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  BigSize = Dysymtab.nextrel;
  BigSize *= sizeof(MachO::relocation_info);
  BigSize += Dysymtab.extreloff;
  if (BigSize > FileSize)
    return malformedError("extreloff field plus nextrel field times sizeof"
                          "(struct relocation_info) of LC_DYSYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, Dysymtab.extreloff,
                                          Dysymtab.nextrel *
                                              sizeof(MachO::relocation_info),
                                          "external relocation table"))
    return Err;
  if (Dysymtab.locreloff > FileSize)
    return malformedError("locreloff field of LC_DYSYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  BigSize = Dysymtab.nlocrel;
  BigSize *= sizeof(MachO::relocation_info);
  BigSize += Dysymtab.locreloff;
  if (BigSize > FileSize)
    return malformedError("locreloff field plus nlocrel field times sizeof"
                          "(struct relocation_info) of LC_DYSYMTAB command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, Dysymtab.locreloff,
                                          Dysymtab.nlocrel *
                                              sizeof(MachO::relocation_info),
                                          "local relocation table"))
    return Err;
  *DysymtabLoadCmd = Load.Ptr;
  return Error::success();
}

static Error checkLinkeditDataCommand(const MachOObjectFile &Obj,
                                 const MachOObjectFile::LoadCommandInfo &Load,
                                 uint32_t LoadCommandIndex,
                                 const char **LoadCmd, const char *CmdName,
                                 std::list<MachOElement> &Elements,
                                 const char *ElementName) {
  if (Load.C.cmdsize < sizeof(MachO::linkedit_data_command))
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " cmdsize too small");
  if (*LoadCmd != nullptr)
    return malformedError("more than one " + Twine(CmdName) + " command");
  MachO::linkedit_data_command LinkData =
    getStruct<MachO::linkedit_data_command>(Obj, Load.Ptr);
  if (LinkData.cmdsize != sizeof(MachO::linkedit_data_command))
    return malformedError(Twine(CmdName) + " command " +
                          Twine(LoadCommandIndex) + " has incorrect cmdsize");
  uint64_t FileSize = Obj.getData().size();
  if (LinkData.dataoff > FileSize)
    return malformedError("dataoff field of " + Twine(CmdName) + " command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  uint64_t BigSize = LinkData.dataoff;
  BigSize += LinkData.datasize;
  if (BigSize > FileSize)
    return malformedError("dataoff field plus datasize field of " +
                          Twine(CmdName) + " command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, LinkData.dataoff,
                                          LinkData.datasize, ElementName))
    return Err;
  *LoadCmd = Load.Ptr;
  return Error::success();
}

static Error checkDyldInfoCommand(const MachOObjectFile &Obj,
                                  const MachOObjectFile::LoadCommandInfo &Load,
                                  uint32_t LoadCommandIndex,
                                  const char **LoadCmd, const char *CmdName,
                                  std::list<MachOElement> &Elements) {
  if (Load.C.cmdsize < sizeof(MachO::dyld_info_command))
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " cmdsize too small");
  if (*LoadCmd != nullptr)
    return malformedError("more than one LC_DYLD_INFO and or LC_DYLD_INFO_ONLY "
                          "command");
  MachO::dyld_info_command DyldInfo =
    getStruct<MachO::dyld_info_command>(Obj, Load.Ptr);
  if (DyldInfo.cmdsize != sizeof(MachO::dyld_info_command))
    return malformedError(Twine(CmdName) + " command " +
                          Twine(LoadCommandIndex) + " has incorrect cmdsize");
  uint64_t FileSize = Obj.getData().size();
  if (DyldInfo.rebase_off > FileSize)
    return malformedError("rebase_off field of " + Twine(CmdName) +
                          " command " + Twine(LoadCommandIndex) + " extends "
                          "past the end of the file");
  uint64_t BigSize = DyldInfo.rebase_off;
  BigSize += DyldInfo.rebase_size;
  if (BigSize > FileSize)
    return malformedError("rebase_off field plus rebase_size field of " +
                          Twine(CmdName) + " command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, DyldInfo.rebase_off,
                                          DyldInfo.rebase_size,
                                          "dyld rebase info"))
    return Err;
  if (DyldInfo.bind_off > FileSize)
    return malformedError("bind_off field of " + Twine(CmdName) +
                          " command " + Twine(LoadCommandIndex) + " extends "
                          "past the end of the file");
  BigSize = DyldInfo.bind_off;
  BigSize += DyldInfo.bind_size;
  if (BigSize > FileSize)
    return malformedError("bind_off field plus bind_size field of " +
                          Twine(CmdName) + " command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, DyldInfo.bind_off,
                                          DyldInfo.bind_size,
                                          "dyld bind info"))
    return Err;
  if (DyldInfo.weak_bind_off > FileSize)
    return malformedError("weak_bind_off field of " + Twine(CmdName) +
                          " command " + Twine(LoadCommandIndex) + " extends "
                          "past the end of the file");
  BigSize = DyldInfo.weak_bind_off;
  BigSize += DyldInfo.weak_bind_size;
  if (BigSize > FileSize)
    return malformedError("weak_bind_off field plus weak_bind_size field of " +
                          Twine(CmdName) + " command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, DyldInfo.weak_bind_off,
                                          DyldInfo.weak_bind_size,
                                          "dyld weak bind info"))
    return Err;
  if (DyldInfo.lazy_bind_off > FileSize)
    return malformedError("lazy_bind_off field of " + Twine(CmdName) +
                          " command " + Twine(LoadCommandIndex) + " extends "
                          "past the end of the file");
  BigSize = DyldInfo.lazy_bind_off;
  BigSize += DyldInfo.lazy_bind_size;
  if (BigSize > FileSize)
    return malformedError("lazy_bind_off field plus lazy_bind_size field of " +
                          Twine(CmdName) + " command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, DyldInfo.lazy_bind_off,
                                          DyldInfo.lazy_bind_size,
                                          "dyld lazy bind info"))
    return Err;
  if (DyldInfo.export_off > FileSize)
    return malformedError("export_off field of " + Twine(CmdName) +
                          " command " + Twine(LoadCommandIndex) + " extends "
                          "past the end of the file");
  BigSize = DyldInfo.export_off;
  BigSize += DyldInfo.export_size;
  if (BigSize > FileSize)
    return malformedError("export_off field plus export_size field of " +
                          Twine(CmdName) + " command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, DyldInfo.export_off,
                                          DyldInfo.export_size,
                                          "dyld export info"))
    return Err;
  *LoadCmd = Load.Ptr;
  return Error::success();
}

static Error checkDylibCommand(const MachOObjectFile &Obj,
                               const MachOObjectFile::LoadCommandInfo &Load,
                               uint32_t LoadCommandIndex, const char *CmdName) {
  if (Load.C.cmdsize < sizeof(MachO::dylib_command))
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " cmdsize too small");
  MachO::dylib_command D = getStruct<MachO::dylib_command>(Obj, Load.Ptr);
  if (D.dylib.name < sizeof(MachO::dylib_command))
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " name.offset field too small, not past "
                          "the end of the dylib_command struct");
  if (D.dylib.name >= D.cmdsize)
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " name.offset field extends past the end "
                          "of the load command");
  // Make sure there is a null between the starting offset of the name and
  // the end of the load command.
  uint32_t i;
  const char *P = (const char *)Load.Ptr;
  for (i = D.dylib.name; i < D.cmdsize; i++)
    if (P[i] == '\0')
      break;
  if (i >= D.cmdsize)
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " library name extends past the end of the "
                          "load command");
  return Error::success();
}

static Error checkDylibIdCommand(const MachOObjectFile &Obj,
                                 const MachOObjectFile::LoadCommandInfo &Load,
                                 uint32_t LoadCommandIndex,
                                 const char **LoadCmd) {
  if (Error Err = checkDylibCommand(Obj, Load, LoadCommandIndex,
                                     "LC_ID_DYLIB"))
    return Err;
  if (*LoadCmd != nullptr)
    return malformedError("more than one LC_ID_DYLIB command");
  if (Obj.getHeader().filetype != MachO::MH_DYLIB &&
      Obj.getHeader().filetype != MachO::MH_DYLIB_STUB)
    return malformedError("LC_ID_DYLIB load command in non-dynamic library "
                          "file type");
  *LoadCmd = Load.Ptr;
  return Error::success();
}

static Error checkDyldCommand(const MachOObjectFile &Obj,
                              const MachOObjectFile::LoadCommandInfo &Load,
                              uint32_t LoadCommandIndex, const char *CmdName) {
  if (Load.C.cmdsize < sizeof(MachO::dylinker_command))
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " cmdsize too small");
  MachO::dylinker_command D = getStruct<MachO::dylinker_command>(Obj, Load.Ptr);
  if (D.name < sizeof(MachO::dylinker_command))
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " name.offset field too small, not past "
                          "the end of the dylinker_command struct");
  if (D.name >= D.cmdsize)
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " name.offset field extends past the end "
                          "of the load command");
  // Make sure there is a null between the starting offset of the name and
  // the end of the load command.
  uint32_t i;
  const char *P = (const char *)Load.Ptr;
  for (i = D.name; i < D.cmdsize; i++)
    if (P[i] == '\0')
      break;
  if (i >= D.cmdsize)
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " dyld name extends past the end of the "
                          "load command");
  return Error::success();
}

static Error checkVersCommand(const MachOObjectFile &Obj,
                              const MachOObjectFile::LoadCommandInfo &Load,
                              uint32_t LoadCommandIndex,
                              const char **LoadCmd, const char *CmdName) {
  if (Load.C.cmdsize != sizeof(MachO::version_min_command))
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " has incorrect cmdsize");
  if (*LoadCmd != nullptr)
    return malformedError("more than one LC_VERSION_MIN_MACOSX, "
                          "LC_VERSION_MIN_IPHONEOS, LC_VERSION_MIN_TVOS or "
                          "LC_VERSION_MIN_WATCHOS command");
  *LoadCmd = Load.Ptr;
  return Error::success();
}

static Error checkNoteCommand(const MachOObjectFile &Obj,
                              const MachOObjectFile::LoadCommandInfo &Load,
                              uint32_t LoadCommandIndex,
                              std::list<MachOElement> &Elements) {
  if (Load.C.cmdsize != sizeof(MachO::note_command))
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_NOTE has incorrect cmdsize");
  MachO::note_command Nt = getStruct<MachO::note_command>(Obj, Load.Ptr);
  uint64_t FileSize = Obj.getData().size();
  if (Nt.offset > FileSize)
    return malformedError("offset field of LC_NOTE command " +
                          Twine(LoadCommandIndex) + " extends "
                          "past the end of the file");
  uint64_t BigSize = Nt.offset;
  BigSize += Nt.size;
  if (BigSize > FileSize)
    return malformedError("size field plus offset field of LC_NOTE command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, Nt.offset, Nt.size,
                                          "LC_NOTE data"))
    return Err;
  return Error::success();
}

static Error
parseBuildVersionCommand(const MachOObjectFile &Obj,
                         const MachOObjectFile::LoadCommandInfo &Load,
                         SmallVectorImpl<const char*> &BuildTools,
                         uint32_t LoadCommandIndex) {
  MachO::build_version_command BVC =
      getStruct<MachO::build_version_command>(Obj, Load.Ptr);
  if (Load.C.cmdsize !=
      sizeof(MachO::build_version_command) +
          BVC.ntools * sizeof(MachO::build_tool_version))
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_BUILD_VERSION_COMMAND has incorrect cmdsize");

  auto Start = Load.Ptr + sizeof(MachO::build_version_command);
  BuildTools.resize(BVC.ntools);
  for (unsigned i = 0; i < BVC.ntools; ++i)
    BuildTools[i] = Start + i * sizeof(MachO::build_tool_version);

  return Error::success();
}

static Error checkRpathCommand(const MachOObjectFile &Obj,
                               const MachOObjectFile::LoadCommandInfo &Load,
                               uint32_t LoadCommandIndex) {
  if (Load.C.cmdsize < sizeof(MachO::rpath_command))
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_RPATH cmdsize too small");
  MachO::rpath_command R = getStruct<MachO::rpath_command>(Obj, Load.Ptr);
  if (R.path < sizeof(MachO::rpath_command))
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_RPATH path.offset field too small, not past "
                          "the end of the rpath_command struct");
  if (R.path >= R.cmdsize)
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_RPATH path.offset field extends past the end "
                          "of the load command");
  // Make sure there is a null between the starting offset of the path and
  // the end of the load command.
  uint32_t i;
  const char *P = (const char *)Load.Ptr;
  for (i = R.path; i < R.cmdsize; i++)
    if (P[i] == '\0')
      break;
  if (i >= R.cmdsize)
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_RPATH library name extends past the end of the "
                          "load command");
  return Error::success();
}

static Error checkEncryptCommand(const MachOObjectFile &Obj,
                                 const MachOObjectFile::LoadCommandInfo &Load,
                                 uint32_t LoadCommandIndex,
                                 uint64_t cryptoff, uint64_t cryptsize,
                                 const char **LoadCmd, const char *CmdName) {
  if (*LoadCmd != nullptr)
    return malformedError("more than one LC_ENCRYPTION_INFO and or "
                          "LC_ENCRYPTION_INFO_64 command");
  uint64_t FileSize = Obj.getData().size();
  if (cryptoff > FileSize)
    return malformedError("cryptoff field of " + Twine(CmdName) +
                          " command " + Twine(LoadCommandIndex) + " extends "
                          "past the end of the file");
  uint64_t BigSize = cryptoff;
  BigSize += cryptsize;
  if (BigSize > FileSize)
    return malformedError("cryptoff field plus cryptsize field of " +
                          Twine(CmdName) + " command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  *LoadCmd = Load.Ptr;
  return Error::success();
}

static Error checkLinkerOptCommand(const MachOObjectFile &Obj,
                                   const MachOObjectFile::LoadCommandInfo &Load,
                                   uint32_t LoadCommandIndex) {
  if (Load.C.cmdsize < sizeof(MachO::linker_option_command))
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_LINKER_OPTION cmdsize too small");
  MachO::linker_option_command L =
    getStruct<MachO::linker_option_command>(Obj, Load.Ptr);
  // Make sure the count of strings is correct.
  const char *string = (const char *)Load.Ptr +
                       sizeof(struct MachO::linker_option_command);
  uint32_t left = L.cmdsize - sizeof(struct MachO::linker_option_command);
  uint32_t i = 0;
  while (left > 0) {
    while (*string == '\0' && left > 0) {
      string++;
      left--;
    }
    if (left > 0) {
      i++;
      uint32_t NullPos = StringRef(string, left).find('\0');
      uint32_t len = std::min(NullPos, left) + 1;
      string += len;
      left -= len;
    }
  }
  if (L.count != i)
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_LINKER_OPTION string count " + Twine(L.count) +
                          " does not match number of strings");
  return Error::success();
}

static Error checkSubCommand(const MachOObjectFile &Obj,
                             const MachOObjectFile::LoadCommandInfo &Load,
                             uint32_t LoadCommandIndex, const char *CmdName,
                             size_t SizeOfCmd, const char *CmdStructName,
                             uint32_t PathOffset, const char *PathFieldName) {
  if (PathOffset < SizeOfCmd)
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " " + PathFieldName + ".offset field too "
                          "small, not past the end of the " + CmdStructName);
  if (PathOffset >= Load.C.cmdsize)
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " " + PathFieldName + ".offset field "
                          "extends past the end of the load command");
  // Make sure there is a null between the starting offset of the path and
  // the end of the load command.
  uint32_t i;
  const char *P = (const char *)Load.Ptr;
  for (i = PathOffset; i < Load.C.cmdsize; i++)
    if (P[i] == '\0')
      break;
  if (i >= Load.C.cmdsize)
    return malformedError("load command " + Twine(LoadCommandIndex) + " " +
                          CmdName + " " + PathFieldName + " name extends past "
                          "the end of the load command");
  return Error::success();
}

static Error checkThreadCommand(const MachOObjectFile &Obj,
                                const MachOObjectFile::LoadCommandInfo &Load,
                                uint32_t LoadCommandIndex,
                                const char *CmdName) {
  if (Load.C.cmdsize < sizeof(MachO::thread_command))
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          CmdName + " cmdsize too small");
  MachO::thread_command T =
    getStruct<MachO::thread_command>(Obj, Load.Ptr);
  const char *state = Load.Ptr + sizeof(MachO::thread_command);
  const char *end = Load.Ptr + T.cmdsize;
  uint32_t nflavor = 0;
  uint32_t cputype = getCPUType(Obj);
  while (state < end) {
    if(state + sizeof(uint32_t) > end)
      return malformedError("load command " + Twine(LoadCommandIndex) +
                            "flavor in " + CmdName + " extends past end of "
                            "command");
    uint32_t flavor;
    memcpy(&flavor, state, sizeof(uint32_t));
    if (Obj.isLittleEndian() != sys::IsLittleEndianHost)
      sys::swapByteOrder(flavor);
    state += sizeof(uint32_t);

    if(state + sizeof(uint32_t) > end)
      return malformedError("load command " + Twine(LoadCommandIndex) +
                            " count in " + CmdName + " extends past end of "
                            "command");
    uint32_t count;
    memcpy(&count, state, sizeof(uint32_t));
    if (Obj.isLittleEndian() != sys::IsLittleEndianHost)
      sys::swapByteOrder(count);
    state += sizeof(uint32_t);

    if (cputype == MachO::CPU_TYPE_I386) {
      if (flavor == MachO::x86_THREAD_STATE32) {
        if (count != MachO::x86_THREAD_STATE32_COUNT)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " count not x86_THREAD_STATE32_COUNT for "
                                "flavor number " + Twine(nflavor) + " which is "
                                "a x86_THREAD_STATE32 flavor in " + CmdName +
                                " command");
        if (state + sizeof(MachO::x86_thread_state32_t) > end)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " x86_THREAD_STATE32 extends past end of "
                                "command in " + CmdName + " command");
        state += sizeof(MachO::x86_thread_state32_t);
      } else {
        return malformedError("load command " + Twine(LoadCommandIndex) +
                              " unknown flavor (" + Twine(flavor) + ") for "
                              "flavor number " + Twine(nflavor) + " in " +
                              CmdName + " command");
      }
    } else if (cputype == MachO::CPU_TYPE_X86_64) {
      if (flavor == MachO::x86_THREAD_STATE) {
        if (count != MachO::x86_THREAD_STATE_COUNT)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " count not x86_THREAD_STATE_COUNT for "
                                "flavor number " + Twine(nflavor) + " which is "
                                "a x86_THREAD_STATE flavor in " + CmdName +
                                " command");
        if (state + sizeof(MachO::x86_thread_state_t) > end)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " x86_THREAD_STATE extends past end of "
                                "command in " + CmdName + " command");
        state += sizeof(MachO::x86_thread_state_t);
      } else if (flavor == MachO::x86_FLOAT_STATE) {
        if (count != MachO::x86_FLOAT_STATE_COUNT)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " count not x86_FLOAT_STATE_COUNT for "
                                "flavor number " + Twine(nflavor) + " which is "
                                "a x86_FLOAT_STATE flavor in " + CmdName +
                                " command");
        if (state + sizeof(MachO::x86_float_state_t) > end)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " x86_FLOAT_STATE extends past end of "
                                "command in " + CmdName + " command");
        state += sizeof(MachO::x86_float_state_t);
      } else if (flavor == MachO::x86_EXCEPTION_STATE) {
        if (count != MachO::x86_EXCEPTION_STATE_COUNT)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " count not x86_EXCEPTION_STATE_COUNT for "
                                "flavor number " + Twine(nflavor) + " which is "
                                "a x86_EXCEPTION_STATE flavor in " + CmdName +
                                " command");
        if (state + sizeof(MachO::x86_exception_state_t) > end)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " x86_EXCEPTION_STATE extends past end of "
                                "command in " + CmdName + " command");
        state += sizeof(MachO::x86_exception_state_t);
      } else if (flavor == MachO::x86_THREAD_STATE64) {
        if (count != MachO::x86_THREAD_STATE64_COUNT)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " count not x86_THREAD_STATE64_COUNT for "
                                "flavor number " + Twine(nflavor) + " which is "
                                "a x86_THREAD_STATE64 flavor in " + CmdName +
                                " command");
        if (state + sizeof(MachO::x86_thread_state64_t) > end)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " x86_THREAD_STATE64 extends past end of "
                                "command in " + CmdName + " command");
        state += sizeof(MachO::x86_thread_state64_t);
      } else if (flavor == MachO::x86_EXCEPTION_STATE64) {
        if (count != MachO::x86_EXCEPTION_STATE64_COUNT)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " count not x86_EXCEPTION_STATE64_COUNT for "
                                "flavor number " + Twine(nflavor) + " which is "
                                "a x86_EXCEPTION_STATE64 flavor in " + CmdName +
                                " command");
        if (state + sizeof(MachO::x86_exception_state64_t) > end)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " x86_EXCEPTION_STATE64 extends past end of "
                                "command in " + CmdName + " command");
        state += sizeof(MachO::x86_exception_state64_t);
      } else {
        return malformedError("load command " + Twine(LoadCommandIndex) +
                              " unknown flavor (" + Twine(flavor) + ") for "
                              "flavor number " + Twine(nflavor) + " in " +
                              CmdName + " command");
      }
    } else if (cputype == MachO::CPU_TYPE_ARM) {
      if (flavor == MachO::ARM_THREAD_STATE) {
        if (count != MachO::ARM_THREAD_STATE_COUNT)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " count not ARM_THREAD_STATE_COUNT for "
                                "flavor number " + Twine(nflavor) + " which is "
                                "a ARM_THREAD_STATE flavor in " + CmdName +
                                " command");
        if (state + sizeof(MachO::arm_thread_state32_t) > end)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " ARM_THREAD_STATE extends past end of "
                                "command in " + CmdName + " command");
        state += sizeof(MachO::arm_thread_state32_t);
      } else {
        return malformedError("load command " + Twine(LoadCommandIndex) +
                              " unknown flavor (" + Twine(flavor) + ") for "
                              "flavor number " + Twine(nflavor) + " in " +
                              CmdName + " command");
      }
    } else if (cputype == MachO::CPU_TYPE_ARM64) {
      if (flavor == MachO::ARM_THREAD_STATE64) {
        if (count != MachO::ARM_THREAD_STATE64_COUNT)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " count not ARM_THREAD_STATE64_COUNT for "
                                "flavor number " + Twine(nflavor) + " which is "
                                "a ARM_THREAD_STATE64 flavor in " + CmdName +
                                " command");
        if (state + sizeof(MachO::arm_thread_state64_t) > end)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " ARM_THREAD_STATE64 extends past end of "
                                "command in " + CmdName + " command");
        state += sizeof(MachO::arm_thread_state64_t);
      } else {
        return malformedError("load command " + Twine(LoadCommandIndex) +
                              " unknown flavor (" + Twine(flavor) + ") for "
                              "flavor number " + Twine(nflavor) + " in " +
                              CmdName + " command");
      }
    } else if (cputype == MachO::CPU_TYPE_POWERPC) {
      if (flavor == MachO::PPC_THREAD_STATE) {
        if (count != MachO::PPC_THREAD_STATE_COUNT)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " count not PPC_THREAD_STATE_COUNT for "
                                "flavor number " + Twine(nflavor) + " which is "
                                "a PPC_THREAD_STATE flavor in " + CmdName +
                                " command");
        if (state + sizeof(MachO::ppc_thread_state32_t) > end)
          return malformedError("load command " + Twine(LoadCommandIndex) +
                                " PPC_THREAD_STATE extends past end of "
                                "command in " + CmdName + " command");
        state += sizeof(MachO::ppc_thread_state32_t);
      } else {
        return malformedError("load command " + Twine(LoadCommandIndex) +
                              " unknown flavor (" + Twine(flavor) + ") for "
                              "flavor number " + Twine(nflavor) + " in " +
                              CmdName + " command");
      }
    } else {
      return malformedError("unknown cputype (" + Twine(cputype) + ") load "
                            "command " + Twine(LoadCommandIndex) + " for " +
                            CmdName + " command can't be checked");
    }
    nflavor++;
  }
  return Error::success();
}

static Error checkTwoLevelHintsCommand(const MachOObjectFile &Obj,
                                       const MachOObjectFile::LoadCommandInfo
                                         &Load,
                                       uint32_t LoadCommandIndex,
                                       const char **LoadCmd,
                                       std::list<MachOElement> &Elements) {
  if (Load.C.cmdsize != sizeof(MachO::twolevel_hints_command))
    return malformedError("load command " + Twine(LoadCommandIndex) +
                          " LC_TWOLEVEL_HINTS has incorrect cmdsize");
  if (*LoadCmd != nullptr)
    return malformedError("more than one LC_TWOLEVEL_HINTS command");
  MachO::twolevel_hints_command Hints =
    getStruct<MachO::twolevel_hints_command>(Obj, Load.Ptr);
  uint64_t FileSize = Obj.getData().size();
  if (Hints.offset > FileSize)
    return malformedError("offset field of LC_TWOLEVEL_HINTS command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  uint64_t BigSize = Hints.nhints;
  BigSize *= sizeof(MachO::twolevel_hint);
  BigSize += Hints.offset;
  if (BigSize > FileSize)
    return malformedError("offset field plus nhints times sizeof(struct "
                          "twolevel_hint) field of LC_TWOLEVEL_HINTS command " +
                          Twine(LoadCommandIndex) + " extends past the end of "
                          "the file");
  if (Error Err = checkOverlappingElement(Elements, Hints.offset, Hints.nhints *
                                          sizeof(MachO::twolevel_hint),
                                          "two level hints"))
    return Err;
  *LoadCmd = Load.Ptr;
  return Error::success();
}

// Returns true if the libObject code does not support the load command and its
// contents.  The cmd value it is treated as an unknown load command but with
// an error message that says the cmd value is obsolete.
static bool isLoadCommandObsolete(uint32_t cmd) {
  if (cmd == MachO::LC_SYMSEG ||
      cmd == MachO::LC_LOADFVMLIB ||
      cmd == MachO::LC_IDFVMLIB ||
      cmd == MachO::LC_IDENT ||
      cmd == MachO::LC_FVMFILE ||
      cmd == MachO::LC_PREPAGE ||
      cmd == MachO::LC_PREBOUND_DYLIB ||
      cmd == MachO::LC_TWOLEVEL_HINTS ||
      cmd == MachO::LC_PREBIND_CKSUM)
    return true;
  return false;
}

Expected<std::unique_ptr<MachOObjectFile>>
MachOObjectFile::create(MemoryBufferRef Object, bool IsLittleEndian,
                        bool Is64Bits, uint32_t UniversalCputype,
                        uint32_t UniversalIndex) {
  Error Err = Error::success();
  std::unique_ptr<MachOObjectFile> Obj(
      new MachOObjectFile(std::move(Object), IsLittleEndian,
                          Is64Bits, Err, UniversalCputype,
                          UniversalIndex));
  if (Err)
    return std::move(Err);
  return std::move(Obj);
}

MachOObjectFile::MachOObjectFile(MemoryBufferRef Object, bool IsLittleEndian,
                                 bool Is64bits, Error &Err,
                                 uint32_t UniversalCputype,
                                 uint32_t UniversalIndex)
    : ObjectFile(getMachOType(IsLittleEndian, Is64bits), Object) {
  ErrorAsOutParameter ErrAsOutParam(&Err);
  uint64_t SizeOfHeaders;
  uint32_t cputype;
  if (is64Bit()) {
    parseHeader(*this, Header64, Err);
    SizeOfHeaders = sizeof(MachO::mach_header_64);
    cputype = Header64.cputype;
  } else {
    parseHeader(*this, Header, Err);
    SizeOfHeaders = sizeof(MachO::mach_header);
    cputype = Header.cputype;
  }
  if (Err)
    return;
  SizeOfHeaders += getHeader().sizeofcmds;
  if (getData().data() + SizeOfHeaders > getData().end()) {
    Err = malformedError("load commands extend past the end of the file");
    return;
  }
  if (UniversalCputype != 0 && cputype != UniversalCputype) {
    Err = malformedError("universal header architecture: " +
                         Twine(UniversalIndex) + "'s cputype does not match "
                         "object file's mach header");
    return;
  }
  std::list<MachOElement> Elements;
  Elements.push_back({0, SizeOfHeaders, "Mach-O headers"});

  uint32_t LoadCommandCount = getHeader().ncmds;
  LoadCommandInfo Load;
  if (LoadCommandCount != 0) {
    if (auto LoadOrErr = getFirstLoadCommandInfo(*this))
      Load = *LoadOrErr;
    else {
      Err = LoadOrErr.takeError();
      return;
    }
  }

  const char *DyldIdLoadCmd = nullptr;
  const char *FuncStartsLoadCmd = nullptr;
  const char *SplitInfoLoadCmd = nullptr;
  const char *CodeSignDrsLoadCmd = nullptr;
  const char *CodeSignLoadCmd = nullptr;
  const char *VersLoadCmd = nullptr;
  const char *SourceLoadCmd = nullptr;
  const char *EntryPointLoadCmd = nullptr;
  const char *EncryptLoadCmd = nullptr;
  const char *RoutinesLoadCmd = nullptr;
  const char *UnixThreadLoadCmd = nullptr;
  const char *TwoLevelHintsLoadCmd = nullptr;
  for (unsigned I = 0; I < LoadCommandCount; ++I) {
    if (is64Bit()) {
      if (Load.C.cmdsize % 8 != 0) {
        // We have a hack here to allow 64-bit Mach-O core files to have
        // LC_THREAD commands that are only a multiple of 4 and not 8 to be
        // allowed since the macOS kernel produces them.
        if (getHeader().filetype != MachO::MH_CORE ||
            Load.C.cmd != MachO::LC_THREAD || Load.C.cmdsize % 4) {
          Err = malformedError("load command " + Twine(I) + " cmdsize not a "
                               "multiple of 8");
          return;
        }
      }
    } else {
      if (Load.C.cmdsize % 4 != 0) {
        Err = malformedError("load command " + Twine(I) + " cmdsize not a "
                             "multiple of 4");
        return;
      }
    }
    LoadCommands.push_back(Load);
    if (Load.C.cmd == MachO::LC_SYMTAB) {
      if ((Err = checkSymtabCommand(*this, Load, I, &SymtabLoadCmd, Elements)))
        return;
    } else if (Load.C.cmd == MachO::LC_DYSYMTAB) {
      if ((Err = checkDysymtabCommand(*this, Load, I, &DysymtabLoadCmd,
                                      Elements)))
        return;
    } else if (Load.C.cmd == MachO::LC_DATA_IN_CODE) {
      if ((Err = checkLinkeditDataCommand(*this, Load, I, &DataInCodeLoadCmd,
                                          "LC_DATA_IN_CODE", Elements,
                                          "data in code info")))
        return;
    } else if (Load.C.cmd == MachO::LC_LINKER_OPTIMIZATION_HINT) {
      if ((Err = checkLinkeditDataCommand(*this, Load, I, &LinkOptHintsLoadCmd,
                                          "LC_LINKER_OPTIMIZATION_HINT",
                                          Elements, "linker optimization "
                                          "hints")))
        return;
    } else if (Load.C.cmd == MachO::LC_FUNCTION_STARTS) {
      if ((Err = checkLinkeditDataCommand(*this, Load, I, &FuncStartsLoadCmd,
                                          "LC_FUNCTION_STARTS", Elements,
                                          "function starts data")))
        return;
    } else if (Load.C.cmd == MachO::LC_SEGMENT_SPLIT_INFO) {
      if ((Err = checkLinkeditDataCommand(*this, Load, I, &SplitInfoLoadCmd,
                                          "LC_SEGMENT_SPLIT_INFO", Elements,
                                          "split info data")))
        return;
    } else if (Load.C.cmd == MachO::LC_DYLIB_CODE_SIGN_DRS) {
      if ((Err = checkLinkeditDataCommand(*this, Load, I, &CodeSignDrsLoadCmd,
                                          "LC_DYLIB_CODE_SIGN_DRS", Elements,
                                          "code signing RDs data")))
        return;
    } else if (Load.C.cmd == MachO::LC_CODE_SIGNATURE) {
      if ((Err = checkLinkeditDataCommand(*this, Load, I, &CodeSignLoadCmd,
                                          "LC_CODE_SIGNATURE", Elements,
                                          "code signature data")))
        return;
    } else if (Load.C.cmd == MachO::LC_DYLD_INFO) {
      if ((Err = checkDyldInfoCommand(*this, Load, I, &DyldInfoLoadCmd,
                                      "LC_DYLD_INFO", Elements)))
        return;
    } else if (Load.C.cmd == MachO::LC_DYLD_INFO_ONLY) {
      if ((Err = checkDyldInfoCommand(*this, Load, I, &DyldInfoLoadCmd,
                                      "LC_DYLD_INFO_ONLY", Elements)))
        return;
    } else if (Load.C.cmd == MachO::LC_UUID) {
      if (Load.C.cmdsize != sizeof(MachO::uuid_command)) {
        Err = malformedError("LC_UUID command " + Twine(I) + " has incorrect "
                             "cmdsize");
        return;
      }
      if (UuidLoadCmd) {
        Err = malformedError("more than one LC_UUID command");
        return;
      }
      UuidLoadCmd = Load.Ptr;
    } else if (Load.C.cmd == MachO::LC_SEGMENT_64) {
      if ((Err = parseSegmentLoadCommand<MachO::segment_command_64,
                                         MachO::section_64>(
                   *this, Load, Sections, HasPageZeroSegment, I,
                   "LC_SEGMENT_64", SizeOfHeaders, Elements)))
        return;
    } else if (Load.C.cmd == MachO::LC_SEGMENT) {
      if ((Err = parseSegmentLoadCommand<MachO::segment_command,
                                         MachO::section>(
                   *this, Load, Sections, HasPageZeroSegment, I,
                   "LC_SEGMENT", SizeOfHeaders, Elements)))
        return;
    } else if (Load.C.cmd == MachO::LC_ID_DYLIB) {
      if ((Err = checkDylibIdCommand(*this, Load, I, &DyldIdLoadCmd)))
        return;
    } else if (Load.C.cmd == MachO::LC_LOAD_DYLIB) {
      if ((Err = checkDylibCommand(*this, Load, I, "LC_LOAD_DYLIB")))
        return;
      Libraries.push_back(Load.Ptr);
    } else if (Load.C.cmd == MachO::LC_LOAD_WEAK_DYLIB) {
      if ((Err = checkDylibCommand(*this, Load, I, "LC_LOAD_WEAK_DYLIB")))
        return;
      Libraries.push_back(Load.Ptr);
    } else if (Load.C.cmd == MachO::LC_LAZY_LOAD_DYLIB) {
      if ((Err = checkDylibCommand(*this, Load, I, "LC_LAZY_LOAD_DYLIB")))
        return;
      Libraries.push_back(Load.Ptr);
    } else if (Load.C.cmd == MachO::LC_REEXPORT_DYLIB) {
      if ((Err = checkDylibCommand(*this, Load, I, "LC_REEXPORT_DYLIB")))
        return;
      Libraries.push_back(Load.Ptr);
    } else if (Load.C.cmd == MachO::LC_LOAD_UPWARD_DYLIB) {
      if ((Err = checkDylibCommand(*this, Load, I, "LC_LOAD_UPWARD_DYLIB")))
        return;
      Libraries.push_back(Load.Ptr);
    } else if (Load.C.cmd == MachO::LC_ID_DYLINKER) {
      if ((Err = checkDyldCommand(*this, Load, I, "LC_ID_DYLINKER")))
        return;
    } else if (Load.C.cmd == MachO::LC_LOAD_DYLINKER) {
      if ((Err = checkDyldCommand(*this, Load, I, "LC_LOAD_DYLINKER")))
        return;
    } else if (Load.C.cmd == MachO::LC_DYLD_ENVIRONMENT) {
      if ((Err = checkDyldCommand(*this, Load, I, "LC_DYLD_ENVIRONMENT")))
        return;
    } else if (Load.C.cmd == MachO::LC_VERSION_MIN_MACOSX) {
      if ((Err = checkVersCommand(*this, Load, I, &VersLoadCmd,
                                  "LC_VERSION_MIN_MACOSX")))
        return;
    } else if (Load.C.cmd == MachO::LC_VERSION_MIN_IPHONEOS) {
      if ((Err = checkVersCommand(*this, Load, I, &VersLoadCmd,
                                  "LC_VERSION_MIN_IPHONEOS")))
        return;
    } else if (Load.C.cmd == MachO::LC_VERSION_MIN_TVOS) {
      if ((Err = checkVersCommand(*this, Load, I, &VersLoadCmd,
                                  "LC_VERSION_MIN_TVOS")))
        return;
    } else if (Load.C.cmd == MachO::LC_VERSION_MIN_WATCHOS) {
      if ((Err = checkVersCommand(*this, Load, I, &VersLoadCmd,
                                  "LC_VERSION_MIN_WATCHOS")))
        return;
    } else if (Load.C.cmd == MachO::LC_NOTE) {
      if ((Err = checkNoteCommand(*this, Load, I, Elements)))
        return;
    } else if (Load.C.cmd == MachO::LC_BUILD_VERSION) {
      if ((Err = parseBuildVersionCommand(*this, Load, BuildTools, I)))
        return;
    } else if (Load.C.cmd == MachO::LC_RPATH) {
      if ((Err = checkRpathCommand(*this, Load, I)))
        return;
    } else if (Load.C.cmd == MachO::LC_SOURCE_VERSION) {
      if (Load.C.cmdsize != sizeof(MachO::source_version_command)) {
        Err = malformedError("LC_SOURCE_VERSION command " + Twine(I) +
                             " has incorrect cmdsize");
        return;
      }
      if (SourceLoadCmd) {
        Err = malformedError("more than one LC_SOURCE_VERSION command");
        return;
      }
      SourceLoadCmd = Load.Ptr;
    } else if (Load.C.cmd == MachO::LC_MAIN) {
      if (Load.C.cmdsize != sizeof(MachO::entry_point_command)) {
        Err = malformedError("LC_MAIN command " + Twine(I) +
                             " has incorrect cmdsize");
        return;
      }
      if (EntryPointLoadCmd) {
        Err = malformedError("more than one LC_MAIN command");
        return;
      }
      EntryPointLoadCmd = Load.Ptr;
    } else if (Load.C.cmd == MachO::LC_ENCRYPTION_INFO) {
      if (Load.C.cmdsize != sizeof(MachO::encryption_info_command)) {
        Err = malformedError("LC_ENCRYPTION_INFO command " + Twine(I) +
                             " has incorrect cmdsize");
        return;
      }
      MachO::encryption_info_command E =
        getStruct<MachO::encryption_info_command>(*this, Load.Ptr);
      if ((Err = checkEncryptCommand(*this, Load, I, E.cryptoff, E.cryptsize,
                                     &EncryptLoadCmd, "LC_ENCRYPTION_INFO")))
        return;
    } else if (Load.C.cmd == MachO::LC_ENCRYPTION_INFO_64) {
      if (Load.C.cmdsize != sizeof(MachO::encryption_info_command_64)) {
        Err = malformedError("LC_ENCRYPTION_INFO_64 command " + Twine(I) +
                             " has incorrect cmdsize");
        return;
      }
      MachO::encryption_info_command_64 E =
        getStruct<MachO::encryption_info_command_64>(*this, Load.Ptr);
      if ((Err = checkEncryptCommand(*this, Load, I, E.cryptoff, E.cryptsize,
                                     &EncryptLoadCmd, "LC_ENCRYPTION_INFO_64")))
        return;
    } else if (Load.C.cmd == MachO::LC_LINKER_OPTION) {
      if ((Err = checkLinkerOptCommand(*this, Load, I)))
        return;
    } else if (Load.C.cmd == MachO::LC_SUB_FRAMEWORK) {
      if (Load.C.cmdsize < sizeof(MachO::sub_framework_command)) {
        Err =  malformedError("load command " + Twine(I) +
                              " LC_SUB_FRAMEWORK cmdsize too small");
        return;
      }
      MachO::sub_framework_command S =
        getStruct<MachO::sub_framework_command>(*this, Load.Ptr);
      if ((Err = checkSubCommand(*this, Load, I, "LC_SUB_FRAMEWORK",
                                 sizeof(MachO::sub_framework_command),
                                 "sub_framework_command", S.umbrella,
                                 "umbrella")))
        return;
    } else if (Load.C.cmd == MachO::LC_SUB_UMBRELLA) {
      if (Load.C.cmdsize < sizeof(MachO::sub_umbrella_command)) {
        Err =  malformedError("load command " + Twine(I) +
                              " LC_SUB_UMBRELLA cmdsize too small");
        return;
      }
      MachO::sub_umbrella_command S =
        getStruct<MachO::sub_umbrella_command>(*this, Load.Ptr);
      if ((Err = checkSubCommand(*this, Load, I, "LC_SUB_UMBRELLA",
                                 sizeof(MachO::sub_umbrella_command),
                                 "sub_umbrella_command", S.sub_umbrella,
                                 "sub_umbrella")))
        return;
    } else if (Load.C.cmd == MachO::LC_SUB_LIBRARY) {
      if (Load.C.cmdsize < sizeof(MachO::sub_library_command)) {
        Err =  malformedError("load command " + Twine(I) +
                              " LC_SUB_LIBRARY cmdsize too small");
        return;
      }
      MachO::sub_library_command S =
        getStruct<MachO::sub_library_command>(*this, Load.Ptr);
      if ((Err = checkSubCommand(*this, Load, I, "LC_SUB_LIBRARY",
                                 sizeof(MachO::sub_library_command),
                                 "sub_library_command", S.sub_library,
                                 "sub_library")))
        return;
    } else if (Load.C.cmd == MachO::LC_SUB_CLIENT) {
      if (Load.C.cmdsize < sizeof(MachO::sub_client_command)) {
        Err =  malformedError("load command " + Twine(I) +
                              " LC_SUB_CLIENT cmdsize too small");
        return;
      }
      MachO::sub_client_command S =
        getStruct<MachO::sub_client_command>(*this, Load.Ptr);
      if ((Err = checkSubCommand(*this, Load, I, "LC_SUB_CLIENT",
                                 sizeof(MachO::sub_client_command),
                                 "sub_client_command", S.client, "client")))
        return;
    } else if (Load.C.cmd == MachO::LC_ROUTINES) {
      if (Load.C.cmdsize != sizeof(MachO::routines_command)) {
        Err = malformedError("LC_ROUTINES command " + Twine(I) +
                             " has incorrect cmdsize");
        return;
      }
      if (RoutinesLoadCmd) {
        Err = malformedError("more than one LC_ROUTINES and or LC_ROUTINES_64 "
                             "command");
        return;
      }
      RoutinesLoadCmd = Load.Ptr;
    } else if (Load.C.cmd == MachO::LC_ROUTINES_64) {
      if (Load.C.cmdsize != sizeof(MachO::routines_command_64)) {
        Err = malformedError("LC_ROUTINES_64 command " + Twine(I) +
                             " has incorrect cmdsize");
        return;
      }
      if (RoutinesLoadCmd) {
        Err = malformedError("more than one LC_ROUTINES_64 and or LC_ROUTINES "
                             "command");
        return;
      }
      RoutinesLoadCmd = Load.Ptr;
    } else if (Load.C.cmd == MachO::LC_UNIXTHREAD) {
      if ((Err = checkThreadCommand(*this, Load, I, "LC_UNIXTHREAD")))
        return;
      if (UnixThreadLoadCmd) {
        Err = malformedError("more than one LC_UNIXTHREAD command");
        return;
      }
      UnixThreadLoadCmd = Load.Ptr;
    } else if (Load.C.cmd == MachO::LC_THREAD) {
      if ((Err = checkThreadCommand(*this, Load, I, "LC_THREAD")))
        return;
    // Note: LC_TWOLEVEL_HINTS is really obsolete and is not supported.
    } else if (Load.C.cmd == MachO::LC_TWOLEVEL_HINTS) {
       if ((Err = checkTwoLevelHintsCommand(*this, Load, I,
                                            &TwoLevelHintsLoadCmd, Elements)))
         return;
    } else if (isLoadCommandObsolete(Load.C.cmd)) {
      Err = malformedError("load command " + Twine(I) + " for cmd value of: " +
                           Twine(Load.C.cmd) + " is obsolete and not "
                           "supported");
      return;
    }
    // TODO: generate a error for unknown load commands by default.  But still
    // need work out an approach to allow or not allow unknown values like this
    // as an option for some uses like lldb.
    if (I < LoadCommandCount - 1) {
      if (auto LoadOrErr = getNextLoadCommandInfo(*this, I, Load))
        Load = *LoadOrErr;
      else {
        Err = LoadOrErr.takeError();
        return;
      }
    }
  }
  if (!SymtabLoadCmd) {
    if (DysymtabLoadCmd) {
      Err = malformedError("contains LC_DYSYMTAB load command without a "
                           "LC_SYMTAB load command");
      return;
    }
  } else if (DysymtabLoadCmd) {
    MachO::symtab_command Symtab =
      getStruct<MachO::symtab_command>(*this, SymtabLoadCmd);
    MachO::dysymtab_command Dysymtab =
      getStruct<MachO::dysymtab_command>(*this, DysymtabLoadCmd);
    if (Dysymtab.nlocalsym != 0 && Dysymtab.ilocalsym > Symtab.nsyms) {
      Err = malformedError("ilocalsym in LC_DYSYMTAB load command "
                           "extends past the end of the symbol table");
      return;
    }
    uint64_t BigSize = Dysymtab.ilocalsym;
    BigSize += Dysymtab.nlocalsym;
    if (Dysymtab.nlocalsym != 0 && BigSize > Symtab.nsyms) {
      Err = malformedError("ilocalsym plus nlocalsym in LC_DYSYMTAB load "
                           "command extends past the end of the symbol table");
      return;
    }
    if (Dysymtab.nextdefsym != 0 && Dysymtab.iextdefsym > Symtab.nsyms) {
      Err = malformedError("iextdefsym in LC_DYSYMTAB load command "
                           "extends past the end of the symbol table");
      return;
    }
    BigSize = Dysymtab.iextdefsym;
    BigSize += Dysymtab.nextdefsym;
    if (Dysymtab.nextdefsym != 0 && BigSize > Symtab.nsyms) {
      Err = malformedError("iextdefsym plus nextdefsym in LC_DYSYMTAB "
                           "load command extends past the end of the symbol "
                           "table");
      return;
    }
    if (Dysymtab.nundefsym != 0 && Dysymtab.iundefsym > Symtab.nsyms) {
      Err = malformedError("iundefsym in LC_DYSYMTAB load command "
                           "extends past the end of the symbol table");
      return;
    }
    BigSize = Dysymtab.iundefsym;
    BigSize += Dysymtab.nundefsym;
    if (Dysymtab.nundefsym != 0 && BigSize > Symtab.nsyms) {
      Err = malformedError("iundefsym plus nundefsym in LC_DYSYMTAB load "
                           " command extends past the end of the symbol table");
      return;
    }
  }
  if ((getHeader().filetype == MachO::MH_DYLIB ||
       getHeader().filetype == MachO::MH_DYLIB_STUB) &&
       DyldIdLoadCmd == nullptr) {
    Err = malformedError("no LC_ID_DYLIB load command in dynamic library "
                         "filetype");
    return;
  }
  assert(LoadCommands.size() == LoadCommandCount);

  Err = Error::success();
}

Error MachOObjectFile::checkSymbolTable() const {
  uint32_t Flags = 0;
  if (is64Bit()) {
    MachO::mach_header_64 H_64 = MachOObjectFile::getHeader64();
    Flags = H_64.flags;
  } else {
    MachO::mach_header H = MachOObjectFile::getHeader();
    Flags = H.flags;
  }
  uint8_t NType = 0;
  uint8_t NSect = 0;
  uint16_t NDesc = 0;
  uint32_t NStrx = 0;
  uint64_t NValue = 0;
  uint32_t SymbolIndex = 0;
  MachO::symtab_command S = getSymtabLoadCommand();
  for (const SymbolRef &Symbol : symbols()) {
    DataRefImpl SymDRI = Symbol.getRawDataRefImpl();
    if (is64Bit()) {
      MachO::nlist_64 STE_64 = getSymbol64TableEntry(SymDRI);
      NType = STE_64.n_type;
      NSect = STE_64.n_sect;
      NDesc = STE_64.n_desc;
      NStrx = STE_64.n_strx;
      NValue = STE_64.n_value;
    } else {
      MachO::nlist STE = getSymbolTableEntry(SymDRI);
      NType = STE.n_type;
      NType = STE.n_type;
      NSect = STE.n_sect;
      NDesc = STE.n_desc;
      NStrx = STE.n_strx;
      NValue = STE.n_value;
    }
    if ((NType & MachO::N_STAB) == 0 &&
        (NType & MachO::N_TYPE) == MachO::N_SECT) {
      if (NSect == 0 || NSect > Sections.size())
        return malformedError("bad section index: " + Twine((int)NSect) +
                              " for symbol at index " + Twine(SymbolIndex));
    }
    if ((NType & MachO::N_STAB) == 0 &&
        (NType & MachO::N_TYPE) == MachO::N_INDR) {
      if (NValue >= S.strsize)
        return malformedError("bad n_value: " + Twine((int)NValue) + " past "
                              "the end of string table, for N_INDR symbol at "
                              "index " + Twine(SymbolIndex));
    }
    if ((Flags & MachO::MH_TWOLEVEL) == MachO::MH_TWOLEVEL &&
        (((NType & MachO::N_TYPE) == MachO::N_UNDF && NValue == 0) ||
         (NType & MachO::N_TYPE) == MachO::N_PBUD)) {
      uint32_t LibraryOrdinal = MachO::GET_LIBRARY_ORDINAL(NDesc);
      if (LibraryOrdinal != 0 &&
          LibraryOrdinal != MachO::EXECUTABLE_ORDINAL &&
          LibraryOrdinal != MachO::DYNAMIC_LOOKUP_ORDINAL &&
          LibraryOrdinal - 1 >= Libraries.size() ) {
        return malformedError("bad library ordinal: " + Twine(LibraryOrdinal) +
                            " for symbol at index " + Twine(SymbolIndex));
      }
    }
    if (NStrx >= S.strsize)
      return malformedError("bad string table index: " + Twine((int)NStrx) +
                            " past the end of string table, for symbol at "
                            "index " + Twine(SymbolIndex));
    SymbolIndex++;
  }
  return Error::success();
}

void MachOObjectFile::moveSymbolNext(DataRefImpl &Symb) const {
  unsigned SymbolTableEntrySize = is64Bit() ?
    sizeof(MachO::nlist_64) :
    sizeof(MachO::nlist);
  Symb.p += SymbolTableEntrySize;
}

Expected<StringRef> MachOObjectFile::getSymbolName(DataRefImpl Symb) const {
  StringRef StringTable = getStringTableData();
  MachO::nlist_base Entry = getSymbolTableEntryBase(*this, Symb);
  if (Entry.n_strx == 0)
    // A n_strx value of 0 indicates that no name is associated with a
    // particular symbol table entry.
    return StringRef();
  const char *Start = &StringTable.data()[Entry.n_strx];
  if (Start < getData().begin() || Start >= getData().end()) {
    return malformedError("bad string index: " + Twine(Entry.n_strx) +
                          " for symbol at index " + Twine(getSymbolIndex(Symb)));
  }
  return StringRef(Start);
}

unsigned MachOObjectFile::getSectionType(SectionRef Sec) const {
  DataRefImpl DRI = Sec.getRawDataRefImpl();
  uint32_t Flags = getSectionFlags(*this, DRI);
  return Flags & MachO::SECTION_TYPE;
}

uint64_t MachOObjectFile::getNValue(DataRefImpl Sym) const {
  if (is64Bit()) {
    MachO::nlist_64 Entry = getSymbol64TableEntry(Sym);
    return Entry.n_value;
  }
  MachO::nlist Entry = getSymbolTableEntry(Sym);
  return Entry.n_value;
}

// getIndirectName() returns the name of the alias'ed symbol who's string table
// index is in the n_value field.
std::error_code MachOObjectFile::getIndirectName(DataRefImpl Symb,
                                                 StringRef &Res) const {
  StringRef StringTable = getStringTableData();
  MachO::nlist_base Entry = getSymbolTableEntryBase(*this, Symb);
  if ((Entry.n_type & MachO::N_TYPE) != MachO::N_INDR)
    return object_error::parse_failed;
  uint64_t NValue = getNValue(Symb);
  if (NValue >= StringTable.size())
    return object_error::parse_failed;
  const char *Start = &StringTable.data()[NValue];
  Res = StringRef(Start);
  return std::error_code();
}

uint64_t MachOObjectFile::getSymbolValueImpl(DataRefImpl Sym) const {
  return getNValue(Sym);
}

Expected<uint64_t> MachOObjectFile::getSymbolAddress(DataRefImpl Sym) const {
  return getSymbolValue(Sym);
}

uint32_t MachOObjectFile::getSymbolAlignment(DataRefImpl DRI) const {
  uint32_t flags = getSymbolFlags(DRI);
  if (flags & SymbolRef::SF_Common) {
    MachO::nlist_base Entry = getSymbolTableEntryBase(*this, DRI);
    return 1 << MachO::GET_COMM_ALIGN(Entry.n_desc);
  }
  return 0;
}

uint64_t MachOObjectFile::getCommonSymbolSizeImpl(DataRefImpl DRI) const {
  return getNValue(DRI);
}

Expected<SymbolRef::Type>
MachOObjectFile::getSymbolType(DataRefImpl Symb) const {
  MachO::nlist_base Entry = getSymbolTableEntryBase(*this, Symb);
  uint8_t n_type = Entry.n_type;

  // If this is a STAB debugging symbol, we can do nothing more.
  if (n_type & MachO::N_STAB)
    return SymbolRef::ST_Debug;

  switch (n_type & MachO::N_TYPE) {
    case MachO::N_UNDF :
      return SymbolRef::ST_Unknown;
    case MachO::N_SECT :
      Expected<section_iterator> SecOrError = getSymbolSection(Symb);
      if (!SecOrError)
        return SecOrError.takeError();
      section_iterator Sec = *SecOrError;
      if (Sec->isData() || Sec->isBSS())
        return SymbolRef::ST_Data;
      return SymbolRef::ST_Function;
  }
  return SymbolRef::ST_Other;
}

uint32_t MachOObjectFile::getSymbolFlags(DataRefImpl DRI) const {
  MachO::nlist_base Entry = getSymbolTableEntryBase(*this, DRI);

  uint8_t MachOType = Entry.n_type;
  uint16_t MachOFlags = Entry.n_desc;

  uint32_t Result = SymbolRef::SF_None;

  if ((MachOType & MachO::N_TYPE) == MachO::N_INDR)
    Result |= SymbolRef::SF_Indirect;

  if (MachOType & MachO::N_STAB)
    Result |= SymbolRef::SF_FormatSpecific;

  if (MachOType & MachO::N_EXT) {
    Result |= SymbolRef::SF_Global;
    if ((MachOType & MachO::N_TYPE) == MachO::N_UNDF) {
      if (getNValue(DRI))
        Result |= SymbolRef::SF_Common;
      else
        Result |= SymbolRef::SF_Undefined;
    }

    if (!(MachOType & MachO::N_PEXT))
      Result |= SymbolRef::SF_Exported;
  }

  if (MachOFlags & (MachO::N_WEAK_REF | MachO::N_WEAK_DEF))
    Result |= SymbolRef::SF_Weak;

  if (MachOFlags & (MachO::N_ARM_THUMB_DEF))
    Result |= SymbolRef::SF_Thumb;

  if ((MachOType & MachO::N_TYPE) == MachO::N_ABS)
    Result |= SymbolRef::SF_Absolute;

  return Result;
}

Expected<section_iterator>
MachOObjectFile::getSymbolSection(DataRefImpl Symb) const {
  MachO::nlist_base Entry = getSymbolTableEntryBase(*this, Symb);
  uint8_t index = Entry.n_sect;

  if (index == 0)
    return section_end();
  DataRefImpl DRI;
  DRI.d.a = index - 1;
  if (DRI.d.a >= Sections.size()){
    return malformedError("bad section index: " + Twine((int)index) +
                          " for symbol at index " + Twine(getSymbolIndex(Symb)));
  }
  return section_iterator(SectionRef(DRI, this));
}

unsigned MachOObjectFile::getSymbolSectionID(SymbolRef Sym) const {
  MachO::nlist_base Entry =
      getSymbolTableEntryBase(*this, Sym.getRawDataRefImpl());
  return Entry.n_sect - 1;
}

void MachOObjectFile::moveSectionNext(DataRefImpl &Sec) const {
  Sec.d.a++;
}

std::error_code MachOObjectFile::getSectionName(DataRefImpl Sec,
                                                StringRef &Result) const {
  ArrayRef<char> Raw = getSectionRawName(Sec);
  Result = parseSegmentOrSectionName(Raw.data());
  return std::error_code();
}

uint64_t MachOObjectFile::getSectionAddress(DataRefImpl Sec) const {
  if (is64Bit())
    return getSection64(Sec).addr;
  return getSection(Sec).addr;
}

uint64_t MachOObjectFile::getSectionIndex(DataRefImpl Sec) const {
  return Sec.d.a;
}

uint64_t MachOObjectFile::getSectionSize(DataRefImpl Sec) const {
  // In the case if a malformed Mach-O file where the section offset is past
  // the end of the file or some part of the section size is past the end of
  // the file return a size of zero or a size that covers the rest of the file
  // but does not extend past the end of the file.
  uint32_t SectOffset, SectType;
  uint64_t SectSize;

  if (is64Bit()) {
    MachO::section_64 Sect = getSection64(Sec);
    SectOffset = Sect.offset;
    SectSize = Sect.size;
    SectType = Sect.flags & MachO::SECTION_TYPE;
  } else {
    MachO::section Sect = getSection(Sec);
    SectOffset = Sect.offset;
    SectSize = Sect.size;
    SectType = Sect.flags & MachO::SECTION_TYPE;
  }
  if (SectType == MachO::S_ZEROFILL || SectType == MachO::S_GB_ZEROFILL)
    return SectSize;
  uint64_t FileSize = getData().size();
  if (SectOffset > FileSize)
    return 0;
  if (FileSize - SectOffset < SectSize)
    return FileSize - SectOffset;
  return SectSize;
}

std::error_code MachOObjectFile::getSectionContents(DataRefImpl Sec,
                                                    StringRef &Res) const {
  uint32_t Offset;
  uint64_t Size;

  if (is64Bit()) {
    MachO::section_64 Sect = getSection64(Sec);
    Offset = Sect.offset;
    Size = Sect.size;
  } else {
    MachO::section Sect = getSection(Sec);
    Offset = Sect.offset;
    Size = Sect.size;
  }

  Res = this->getData().substr(Offset, Size);
  return std::error_code();
}

uint64_t MachOObjectFile::getSectionAlignment(DataRefImpl Sec) const {
  uint32_t Align;
  if (is64Bit()) {
    MachO::section_64 Sect = getSection64(Sec);
    Align = Sect.align;
  } else {
    MachO::section Sect = getSection(Sec);
    Align = Sect.align;
  }

  return uint64_t(1) << Align;
}

Expected<SectionRef> MachOObjectFile::getSection(unsigned SectionIndex) const {
  if (SectionIndex < 1 || SectionIndex > Sections.size())
    return malformedError("bad section index: " + Twine((int)SectionIndex));

  DataRefImpl DRI;
  DRI.d.a = SectionIndex - 1;
  return SectionRef(DRI, this);
}

Expected<SectionRef> MachOObjectFile::getSection(StringRef SectionName) const {
  StringRef SecName;
  for (const SectionRef &Section : sections()) {
    if (std::error_code E = Section.getName(SecName))
      return errorCodeToError(E);
    if (SecName == SectionName) {
      return Section;
    }
  }
  return errorCodeToError(object_error::parse_failed);
}

bool MachOObjectFile::isSectionCompressed(DataRefImpl Sec) const {
  return false;
}

bool MachOObjectFile::isSectionText(DataRefImpl Sec) const {
  uint32_t Flags = getSectionFlags(*this, Sec);
  return Flags & MachO::S_ATTR_PURE_INSTRUCTIONS;
}

bool MachOObjectFile::isSectionData(DataRefImpl Sec) const {
  uint32_t Flags = getSectionFlags(*this, Sec);
  unsigned SectionType = Flags & MachO::SECTION_TYPE;
  return !(Flags & MachO::S_ATTR_PURE_INSTRUCTIONS) &&
         !(SectionType == MachO::S_ZEROFILL ||
           SectionType == MachO::S_GB_ZEROFILL);
}

bool MachOObjectFile::isSectionBSS(DataRefImpl Sec) const {
  uint32_t Flags = getSectionFlags(*this, Sec);
  unsigned SectionType = Flags & MachO::SECTION_TYPE;
  return !(Flags & MachO::S_ATTR_PURE_INSTRUCTIONS) &&
         (SectionType == MachO::S_ZEROFILL ||
          SectionType == MachO::S_GB_ZEROFILL);
}

unsigned MachOObjectFile::getSectionID(SectionRef Sec) const {
  return Sec.getRawDataRefImpl().d.a;
}

bool MachOObjectFile::isSectionVirtual(DataRefImpl Sec) const {
  uint32_t Flags = getSectionFlags(*this, Sec);
  unsigned SectionType = Flags & MachO::SECTION_TYPE;
  return SectionType == MachO::S_ZEROFILL ||
         SectionType == MachO::S_GB_ZEROFILL;
}

bool MachOObjectFile::isSectionBitcode(DataRefImpl Sec) const {
  StringRef SegmentName = getSectionFinalSegmentName(Sec);
  StringRef SectName;
  if (!getSectionName(Sec, SectName))
    return (SegmentName == "__LLVM" && SectName == "__bitcode");
  return false;
}

bool MachOObjectFile::isSectionStripped(DataRefImpl Sec) const {
  if (is64Bit())
    return getSection64(Sec).offset == 0;
  return getSection(Sec).offset == 0;
}

relocation_iterator MachOObjectFile::section_rel_begin(DataRefImpl Sec) const {
  DataRefImpl Ret;
  Ret.d.a = Sec.d.a;
  Ret.d.b = 0;
  return relocation_iterator(RelocationRef(Ret, this));
}

relocation_iterator
MachOObjectFile::section_rel_end(DataRefImpl Sec) const {
  uint32_t Num;
  if (is64Bit()) {
    MachO::section_64 Sect = getSection64(Sec);
    Num = Sect.nreloc;
  } else {
    MachO::section Sect = getSection(Sec);
    Num = Sect.nreloc;
  }

  DataRefImpl Ret;
  Ret.d.a = Sec.d.a;
  Ret.d.b = Num;
  return relocation_iterator(RelocationRef(Ret, this));
}

relocation_iterator MachOObjectFile::extrel_begin() const {
  DataRefImpl Ret;
  // for DYSYMTAB symbols, Ret.d.a == 0 for external relocations
  Ret.d.a = 0; // Would normally be a section index.
  Ret.d.b = 0; // Index into the external relocations
  return relocation_iterator(RelocationRef(Ret, this));
}

relocation_iterator MachOObjectFile::extrel_end() const {
  MachO::dysymtab_command DysymtabLoadCmd = getDysymtabLoadCommand();
  DataRefImpl Ret;
  // for DYSYMTAB symbols, Ret.d.a == 0 for external relocations
  Ret.d.a = 0; // Would normally be a section index.
  Ret.d.b = DysymtabLoadCmd.nextrel; // Index into the external relocations
  return relocation_iterator(RelocationRef(Ret, this));
}

relocation_iterator MachOObjectFile::locrel_begin() const {
  DataRefImpl Ret;
  // for DYSYMTAB symbols, Ret.d.a == 1 for local relocations
  Ret.d.a = 1; // Would normally be a section index.
  Ret.d.b = 0; // Index into the local relocations
  return relocation_iterator(RelocationRef(Ret, this));
}

relocation_iterator MachOObjectFile::locrel_end() const {
  MachO::dysymtab_command DysymtabLoadCmd = getDysymtabLoadCommand();
  DataRefImpl Ret;
  // for DYSYMTAB symbols, Ret.d.a == 1 for local relocations
  Ret.d.a = 1; // Would normally be a section index.
  Ret.d.b = DysymtabLoadCmd.nlocrel; // Index into the local relocations
  return relocation_iterator(RelocationRef(Ret, this));
}

void MachOObjectFile::moveRelocationNext(DataRefImpl &Rel) const {
  ++Rel.d.b;
}

uint64_t MachOObjectFile::getRelocationOffset(DataRefImpl Rel) const {
  assert((getHeader().filetype == MachO::MH_OBJECT ||
          getHeader().filetype == MachO::MH_KEXT_BUNDLE) &&
         "Only implemented for MH_OBJECT && MH_KEXT_BUNDLE");
  MachO::any_relocation_info RE = getRelocation(Rel);
  return getAnyRelocationAddress(RE);
}

symbol_iterator
MachOObjectFile::getRelocationSymbol(DataRefImpl Rel) const {
  MachO::any_relocation_info RE = getRelocation(Rel);
  if (isRelocationScattered(RE))
    return symbol_end();

  uint32_t SymbolIdx = getPlainRelocationSymbolNum(RE);
  bool isExtern = getPlainRelocationExternal(RE);
  if (!isExtern)
    return symbol_end();

  MachO::symtab_command S = getSymtabLoadCommand();
  unsigned SymbolTableEntrySize = is64Bit() ?
    sizeof(MachO::nlist_64) :
    sizeof(MachO::nlist);
  uint64_t Offset = S.symoff + SymbolIdx * SymbolTableEntrySize;
  DataRefImpl Sym;
  Sym.p = reinterpret_cast<uintptr_t>(getPtr(*this, Offset));
  return symbol_iterator(SymbolRef(Sym, this));
}

section_iterator
MachOObjectFile::getRelocationSection(DataRefImpl Rel) const {
  return section_iterator(getAnyRelocationSection(getRelocation(Rel)));
}

uint64_t MachOObjectFile::getRelocationType(DataRefImpl Rel) const {
  MachO::any_relocation_info RE = getRelocation(Rel);
  return getAnyRelocationType(RE);
}

void MachOObjectFile::getRelocationTypeName(
    DataRefImpl Rel, SmallVectorImpl<char> &Result) const {
  StringRef res;
  uint64_t RType = getRelocationType(Rel);

  unsigned Arch = this->getArch();

  switch (Arch) {
    case Triple::x86: {
      static const char *const Table[] =  {
        "GENERIC_RELOC_VANILLA",
        "GENERIC_RELOC_PAIR",
        "GENERIC_RELOC_SECTDIFF",
        "GENERIC_RELOC_PB_LA_PTR",
        "GENERIC_RELOC_LOCAL_SECTDIFF",
        "GENERIC_RELOC_TLV" };

      if (RType > 5)
        res = "Unknown";
      else
        res = Table[RType];
      break;
    }
    case Triple::x86_64: {
      static const char *const Table[] =  {
        "X86_64_RELOC_UNSIGNED",
        "X86_64_RELOC_SIGNED",
        "X86_64_RELOC_BRANCH",
        "X86_64_RELOC_GOT_LOAD",
        "X86_64_RELOC_GOT",
        "X86_64_RELOC_SUBTRACTOR",
        "X86_64_RELOC_SIGNED_1",
        "X86_64_RELOC_SIGNED_2",
        "X86_64_RELOC_SIGNED_4",
        "X86_64_RELOC_TLV" };

      if (RType > 9)
        res = "Unknown";
      else
        res = Table[RType];
      break;
    }
    case Triple::arm: {
      static const char *const Table[] =  {
        "ARM_RELOC_VANILLA",
        "ARM_RELOC_PAIR",
        "ARM_RELOC_SECTDIFF",
        "ARM_RELOC_LOCAL_SECTDIFF",
        "ARM_RELOC_PB_LA_PTR",
        "ARM_RELOC_BR24",
        "ARM_THUMB_RELOC_BR22",
        "ARM_THUMB_32BIT_BRANCH",
        "ARM_RELOC_HALF",
        "ARM_RELOC_HALF_SECTDIFF" };

      if (RType > 9)
        res = "Unknown";
      else
        res = Table[RType];
      break;
    }
    case Triple::aarch64: {
      static const char *const Table[] = {
        "ARM64_RELOC_UNSIGNED",           "ARM64_RELOC_SUBTRACTOR",
        "ARM64_RELOC_BRANCH26",           "ARM64_RELOC_PAGE21",
        "ARM64_RELOC_PAGEOFF12",          "ARM64_RELOC_GOT_LOAD_PAGE21",
        "ARM64_RELOC_GOT_LOAD_PAGEOFF12", "ARM64_RELOC_POINTER_TO_GOT",
        "ARM64_RELOC_TLVP_LOAD_PAGE21",   "ARM64_RELOC_TLVP_LOAD_PAGEOFF12",
        "ARM64_RELOC_ADDEND"
      };

      if (RType >= array_lengthof(Table))
        res = "Unknown";
      else
        res = Table[RType];
      break;
    }
    case Triple::ppc: {
      static const char *const Table[] =  {
        "PPC_RELOC_VANILLA",
        "PPC_RELOC_PAIR",
        "PPC_RELOC_BR14",
        "PPC_RELOC_BR24",
        "PPC_RELOC_HI16",
        "PPC_RELOC_LO16",
        "PPC_RELOC_HA16",
        "PPC_RELOC_LO14",
        "PPC_RELOC_SECTDIFF",
        "PPC_RELOC_PB_LA_PTR",
        "PPC_RELOC_HI16_SECTDIFF",
        "PPC_RELOC_LO16_SECTDIFF",
        "PPC_RELOC_HA16_SECTDIFF",
        "PPC_RELOC_JBSR",
        "PPC_RELOC_LO14_SECTDIFF",
        "PPC_RELOC_LOCAL_SECTDIFF" };

      if (RType > 15)
        res = "Unknown";
      else
        res = Table[RType];
      break;
    }
    case Triple::UnknownArch:
      res = "Unknown";
      break;
  }
  Result.append(res.begin(), res.end());
}

uint8_t MachOObjectFile::getRelocationLength(DataRefImpl Rel) const {
  MachO::any_relocation_info RE = getRelocation(Rel);
  return getAnyRelocationLength(RE);
}

//
// guessLibraryShortName() is passed a name of a dynamic library and returns a
// guess on what the short name is.  Then name is returned as a substring of the
// StringRef Name passed in.  The name of the dynamic library is recognized as
// a framework if it has one of the two following forms:
//      Foo.framework/Versions/A/Foo
//      Foo.framework/Foo
// Where A and Foo can be any string.  And may contain a trailing suffix
// starting with an underbar.  If the Name is recognized as a framework then
// isFramework is set to true else it is set to false.  If the Name has a
// suffix then Suffix is set to the substring in Name that contains the suffix
// else it is set to a NULL StringRef.
//
// The Name of the dynamic library is recognized as a library name if it has
// one of the two following forms:
//      libFoo.A.dylib
//      libFoo.dylib
// The library may have a suffix trailing the name Foo of the form:
//      libFoo_profile.A.dylib
//      libFoo_profile.dylib
//
// The Name of the dynamic library is also recognized as a library name if it
// has the following form:
//      Foo.qtx
//
// If the Name of the dynamic library is none of the forms above then a NULL
// StringRef is returned.
//
StringRef MachOObjectFile::guessLibraryShortName(StringRef Name,
                                                 bool &isFramework,
                                                 StringRef &Suffix) {
  StringRef Foo, F, DotFramework, V, Dylib, Lib, Dot, Qtx;
  size_t a, b, c, d, Idx;

  isFramework = false;
  Suffix = StringRef();

  // Pull off the last component and make Foo point to it
  a = Name.rfind('/');
  if (a == Name.npos || a == 0)
    goto guess_library;
  Foo = Name.slice(a+1, Name.npos);

  // Look for a suffix starting with a '_'
  Idx = Foo.rfind('_');
  if (Idx != Foo.npos && Foo.size() >= 2) {
    Suffix = Foo.slice(Idx, Foo.npos);
    Foo = Foo.slice(0, Idx);
  }

  // First look for the form Foo.framework/Foo
  b = Name.rfind('/', a);
  if (b == Name.npos)
    Idx = 0;
  else
    Idx = b+1;
  F = Name.slice(Idx, Idx + Foo.size());
  DotFramework = Name.slice(Idx + Foo.size(),
                            Idx + Foo.size() + sizeof(".framework/")-1);
  if (F == Foo && DotFramework == ".framework/") {
    isFramework = true;
    return Foo;
  }

  // Next look for the form Foo.framework/Versions/A/Foo
  if (b == Name.npos)
    goto guess_library;
  c =  Name.rfind('/', b);
  if (c == Name.npos || c == 0)
    goto guess_library;
  V = Name.slice(c+1, Name.npos);
  if (!V.startswith("Versions/"))
    goto guess_library;
  d =  Name.rfind('/', c);
  if (d == Name.npos)
    Idx = 0;
  else
    Idx = d+1;
  F = Name.slice(Idx, Idx + Foo.size());
  DotFramework = Name.slice(Idx + Foo.size(),
                            Idx + Foo.size() + sizeof(".framework/")-1);
  if (F == Foo && DotFramework == ".framework/") {
    isFramework = true;
    return Foo;
  }

guess_library:
  // pull off the suffix after the "." and make a point to it
  a = Name.rfind('.');
  if (a == Name.npos || a == 0)
    return StringRef();
  Dylib = Name.slice(a, Name.npos);
  if (Dylib != ".dylib")
    goto guess_qtx;

  // First pull off the version letter for the form Foo.A.dylib if any.
  if (a >= 3) {
    Dot = Name.slice(a-2, a-1);
    if (Dot == ".")
      a = a - 2;
  }

  b = Name.rfind('/', a);
  if (b == Name.npos)
    b = 0;
  else
    b = b+1;
  // ignore any suffix after an underbar like Foo_profile.A.dylib
  Idx = Name.find('_', b);
  if (Idx != Name.npos && Idx != b) {
    Lib = Name.slice(b, Idx);
    Suffix = Name.slice(Idx, a);
  }
  else
    Lib = Name.slice(b, a);
  // There are incorrect library names of the form:
  // libATS.A_profile.dylib so check for these.
  if (Lib.size() >= 3) {
    Dot = Lib.slice(Lib.size()-2, Lib.size()-1);
    if (Dot == ".")
      Lib = Lib.slice(0, Lib.size()-2);
  }
  return Lib;

guess_qtx:
  Qtx = Name.slice(a, Name.npos);
  if (Qtx != ".qtx")
    return StringRef();
  b = Name.rfind('/', a);
  if (b == Name.npos)
    Lib = Name.slice(0, a);
  else
    Lib = Name.slice(b+1, a);
  // There are library names of the form: QT.A.qtx so check for these.
  if (Lib.size() >= 3) {
    Dot = Lib.slice(Lib.size()-2, Lib.size()-1);
    if (Dot == ".")
      Lib = Lib.slice(0, Lib.size()-2);
  }
  return Lib;
}

// getLibraryShortNameByIndex() is used to get the short name of the library
// for an undefined symbol in a linked Mach-O binary that was linked with the
// normal two-level namespace default (that is MH_TWOLEVEL in the header).
// It is passed the index (0 - based) of the library as translated from
// GET_LIBRARY_ORDINAL (1 - based).
std::error_code MachOObjectFile::getLibraryShortNameByIndex(unsigned Index,
                                                         StringRef &Res) const {
  if (Index >= Libraries.size())
    return object_error::parse_failed;

  // If the cache of LibrariesShortNames is not built up do that first for
  // all the Libraries.
  if (LibrariesShortNames.size() == 0) {
    for (unsigned i = 0; i < Libraries.size(); i++) {
      MachO::dylib_command D =
        getStruct<MachO::dylib_command>(*this, Libraries[i]);
      if (D.dylib.name >= D.cmdsize)
        return object_error::parse_failed;
      const char *P = (const char *)(Libraries[i]) + D.dylib.name;
      StringRef Name = StringRef(P);
      if (D.dylib.name+Name.size() >= D.cmdsize)
        return object_error::parse_failed;
      StringRef Suffix;
      bool isFramework;
      StringRef shortName = guessLibraryShortName(Name, isFramework, Suffix);
      if (shortName.empty())
        LibrariesShortNames.push_back(Name);
      else
        LibrariesShortNames.push_back(shortName);
    }
  }

  Res = LibrariesShortNames[Index];
  return std::error_code();
}

uint32_t MachOObjectFile::getLibraryCount() const {
  return Libraries.size();
}

section_iterator
MachOObjectFile::getRelocationRelocatedSection(relocation_iterator Rel) const {
  DataRefImpl Sec;
  Sec.d.a = Rel->getRawDataRefImpl().d.a;
  return section_iterator(SectionRef(Sec, this));
}

basic_symbol_iterator MachOObjectFile::symbol_begin() const {
  DataRefImpl DRI;
  MachO::symtab_command Symtab = getSymtabLoadCommand();
  if (!SymtabLoadCmd || Symtab.nsyms == 0)
    return basic_symbol_iterator(SymbolRef(DRI, this));

  return getSymbolByIndex(0);
}

basic_symbol_iterator MachOObjectFile::symbol_end() const {
  DataRefImpl DRI;
  MachO::symtab_command Symtab = getSymtabLoadCommand();
  if (!SymtabLoadCmd || Symtab.nsyms == 0)
    return basic_symbol_iterator(SymbolRef(DRI, this));

  unsigned SymbolTableEntrySize = is64Bit() ?
    sizeof(MachO::nlist_64) :
    sizeof(MachO::nlist);
  unsigned Offset = Symtab.symoff +
    Symtab.nsyms * SymbolTableEntrySize;
  DRI.p = reinterpret_cast<uintptr_t>(getPtr(*this, Offset));
  return basic_symbol_iterator(SymbolRef(DRI, this));
}

symbol_iterator MachOObjectFile::getSymbolByIndex(unsigned Index) const {
  MachO::symtab_command Symtab = getSymtabLoadCommand();
  if (!SymtabLoadCmd || Index >= Symtab.nsyms)
    report_fatal_error("Requested symbol index is out of range.");
  unsigned SymbolTableEntrySize =
    is64Bit() ? sizeof(MachO::nlist_64) : sizeof(MachO::nlist);
  DataRefImpl DRI;
  DRI.p = reinterpret_cast<uintptr_t>(getPtr(*this, Symtab.symoff));
  DRI.p += Index * SymbolTableEntrySize;
  return basic_symbol_iterator(SymbolRef(DRI, this));
}

uint64_t MachOObjectFile::getSymbolIndex(DataRefImpl Symb) const {
  MachO::symtab_command Symtab = getSymtabLoadCommand();
  if (!SymtabLoadCmd)
    report_fatal_error("getSymbolIndex() called with no symbol table symbol");
  unsigned SymbolTableEntrySize =
    is64Bit() ? sizeof(MachO::nlist_64) : sizeof(MachO::nlist);
  DataRefImpl DRIstart;
  DRIstart.p = reinterpret_cast<uintptr_t>(getPtr(*this, Symtab.symoff));
  uint64_t Index = (Symb.p - DRIstart.p) / SymbolTableEntrySize;
  return Index;
}

section_iterator MachOObjectFile::section_begin() const {
  DataRefImpl DRI;
  return section_iterator(SectionRef(DRI, this));
}

section_iterator MachOObjectFile::section_end() const {
  DataRefImpl DRI;
  DRI.d.a = Sections.size();
  return section_iterator(SectionRef(DRI, this));
}

uint8_t MachOObjectFile::getBytesInAddress() const {
  return is64Bit() ? 8 : 4;
}

StringRef MachOObjectFile::getFileFormatName() const {
  unsigned CPUType = getCPUType(*this);
  if (!is64Bit()) {
    switch (CPUType) {
    case MachO::CPU_TYPE_I386:
      return "Mach-O 32-bit i386";
    case MachO::CPU_TYPE_ARM:
      return "Mach-O arm";
    case MachO::CPU_TYPE_POWERPC:
      return "Mach-O 32-bit ppc";
    default:
      return "Mach-O 32-bit unknown";
    }
  }

  switch (CPUType) {
  case MachO::CPU_TYPE_X86_64:
    return "Mach-O 64-bit x86-64";
  case MachO::CPU_TYPE_ARM64:
    return "Mach-O arm64";
  case MachO::CPU_TYPE_POWERPC64:
    return "Mach-O 64-bit ppc64";
  default:
    return "Mach-O 64-bit unknown";
  }
}

Triple::ArchType MachOObjectFile::getArch(uint32_t CPUType) {
  switch (CPUType) {
  case MachO::CPU_TYPE_I386:
    return Triple::x86;
  case MachO::CPU_TYPE_X86_64:
    return Triple::x86_64;
  case MachO::CPU_TYPE_ARM:
    return Triple::arm;
  case MachO::CPU_TYPE_ARM64:
    return Triple::aarch64;
  case MachO::CPU_TYPE_POWERPC:
    return Triple::ppc;
  case MachO::CPU_TYPE_POWERPC64:
    return Triple::ppc64;
  default:
    return Triple::UnknownArch;
  }
}

Triple MachOObjectFile::getArchTriple(uint32_t CPUType, uint32_t CPUSubType,
                                      const char **McpuDefault,
                                      const char **ArchFlag) {
  if (McpuDefault)
    *McpuDefault = nullptr;
  if (ArchFlag)
    *ArchFlag = nullptr;

  switch (CPUType) {
  case MachO::CPU_TYPE_I386:
    switch (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) {
    case MachO::CPU_SUBTYPE_I386_ALL:
      if (ArchFlag)
        *ArchFlag = "i386";
      return Triple("i386-apple-darwin");
    default:
      return Triple();
    }
  case MachO::CPU_TYPE_X86_64:
    switch (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) {
    case MachO::CPU_SUBTYPE_X86_64_ALL:
      if (ArchFlag)
        *ArchFlag = "x86_64";
      return Triple("x86_64-apple-darwin");
    case MachO::CPU_SUBTYPE_X86_64_H:
      if (ArchFlag)
        *ArchFlag = "x86_64h";
      return Triple("x86_64h-apple-darwin");
    default:
      return Triple();
    }
  case MachO::CPU_TYPE_ARM:
    switch (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) {
    case MachO::CPU_SUBTYPE_ARM_V4T:
      if (ArchFlag)
        *ArchFlag = "armv4t";
      return Triple("armv4t-apple-darwin");
    case MachO::CPU_SUBTYPE_ARM_V5TEJ:
      if (ArchFlag)
        *ArchFlag = "armv5e";
      return Triple("armv5e-apple-darwin");
    case MachO::CPU_SUBTYPE_ARM_XSCALE:
      if (ArchFlag)
        *ArchFlag = "xscale";
      return Triple("xscale-apple-darwin");
    case MachO::CPU_SUBTYPE_ARM_V6:
      if (ArchFlag)
        *ArchFlag = "armv6";
      return Triple("armv6-apple-darwin");
    case MachO::CPU_SUBTYPE_ARM_V6M:
      if (McpuDefault)
        *McpuDefault = "cortex-m0";
      if (ArchFlag)
        *ArchFlag = "armv6m";
      return Triple("armv6m-apple-darwin");
    case MachO::CPU_SUBTYPE_ARM_V7:
      if (ArchFlag)
        *ArchFlag = "armv7";
      return Triple("armv7-apple-darwin");
    case MachO::CPU_SUBTYPE_ARM_V7EM:
      if (McpuDefault)
        *McpuDefault = "cortex-m4";
      if (ArchFlag)
        *ArchFlag = "armv7em";
      return Triple("thumbv7em-apple-darwin");
    case MachO::CPU_SUBTYPE_ARM_V7K:
      if (McpuDefault)
        *McpuDefault = "cortex-a7";
      if (ArchFlag)
        *ArchFlag = "armv7k";
      return Triple("armv7k-apple-darwin");
    case MachO::CPU_SUBTYPE_ARM_V7M:
      if (McpuDefault)
        *McpuDefault = "cortex-m3";
      if (ArchFlag)
        *ArchFlag = "armv7m";
      return Triple("thumbv7m-apple-darwin");
    case MachO::CPU_SUBTYPE_ARM_V7S:
      if (McpuDefault)
        *McpuDefault = "cortex-a7";
      if (ArchFlag)
        *ArchFlag = "armv7s";
      return Triple("armv7s-apple-darwin");
    default:
      return Triple();
    }
  case MachO::CPU_TYPE_ARM64:
    switch (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) {
    case MachO::CPU_SUBTYPE_ARM64_ALL:
      if (McpuDefault)
        *McpuDefault = "cyclone";
      if (ArchFlag)
        *ArchFlag = "arm64";
      return Triple("arm64-apple-darwin");
    default:
      return Triple();
    }
  case MachO::CPU_TYPE_POWERPC:
    switch (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) {
    case MachO::CPU_SUBTYPE_POWERPC_ALL:
      if (ArchFlag)
        *ArchFlag = "ppc";
      return Triple("ppc-apple-darwin");
    default:
      return Triple();
    }
  case MachO::CPU_TYPE_POWERPC64:
    switch (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) {
    case MachO::CPU_SUBTYPE_POWERPC_ALL:
      if (ArchFlag)
        *ArchFlag = "ppc64";
      return Triple("ppc64-apple-darwin");
    default:
      return Triple();
    }
  default:
    return Triple();
  }
}

Triple MachOObjectFile::getHostArch() {
  return Triple(sys::getDefaultTargetTriple());
}

bool MachOObjectFile::isValidArch(StringRef ArchFlag) {
  return StringSwitch<bool>(ArchFlag)
      .Case("i386", true)
      .Case("x86_64", true)
      .Case("x86_64h", true)
      .Case("armv4t", true)
      .Case("arm", true)
      .Case("armv5e", true)
      .Case("armv6", true)
      .Case("armv6m", true)
      .Case("armv7", true)
      .Case("armv7em", true)
      .Case("armv7k", true)
      .Case("armv7m", true)
      .Case("armv7s", true)
      .Case("arm64", true)
      .Case("ppc", true)
      .Case("ppc64", true)
      .Default(false);
}

Triple::ArchType MachOObjectFile::getArch() const {
  return getArch(getCPUType(*this));
}

Triple MachOObjectFile::getArchTriple(const char **McpuDefault) const {
  return getArchTriple(Header.cputype, Header.cpusubtype, McpuDefault);
}

relocation_iterator MachOObjectFile::section_rel_begin(unsigned Index) const {
  DataRefImpl DRI;
  DRI.d.a = Index;
  return section_rel_begin(DRI);
}

relocation_iterator MachOObjectFile::section_rel_end(unsigned Index) const {
  DataRefImpl DRI;
  DRI.d.a = Index;
  return section_rel_end(DRI);
}

dice_iterator MachOObjectFile::begin_dices() const {
  DataRefImpl DRI;
  if (!DataInCodeLoadCmd)
    return dice_iterator(DiceRef(DRI, this));

  MachO::linkedit_data_command DicLC = getDataInCodeLoadCommand();
  DRI.p = reinterpret_cast<uintptr_t>(getPtr(*this, DicLC.dataoff));
  return dice_iterator(DiceRef(DRI, this));
}

dice_iterator MachOObjectFile::end_dices() const {
  DataRefImpl DRI;
  if (!DataInCodeLoadCmd)
    return dice_iterator(DiceRef(DRI, this));

  MachO::linkedit_data_command DicLC = getDataInCodeLoadCommand();
  unsigned Offset = DicLC.dataoff + DicLC.datasize;
  DRI.p = reinterpret_cast<uintptr_t>(getPtr(*this, Offset));
  return dice_iterator(DiceRef(DRI, this));
}

ExportEntry::ExportEntry(Error *E, const MachOObjectFile *O,
                         ArrayRef<uint8_t> T) : E(E), O(O), Trie(T) {}

void ExportEntry::moveToFirst() {
  ErrorAsOutParameter ErrAsOutParam(E);
  pushNode(0);
  if (*E)
    return;
  pushDownUntilBottom();
}

void ExportEntry::moveToEnd() {
  Stack.clear();
  Done = true;
}

bool ExportEntry::operator==(const ExportEntry &Other) const {
  // Common case, one at end, other iterating from begin.
  if (Done || Other.Done)
    return (Done == Other.Done);
  // Not equal if different stack sizes.
  if (Stack.size() != Other.Stack.size())
    return false;
  // Not equal if different cumulative strings.
  if (!CumulativeString.equals(Other.CumulativeString))
    return false;
  // Equal if all nodes in both stacks match.
  for (unsigned i=0; i < Stack.size(); ++i) {
    if (Stack[i].Start != Other.Stack[i].Start)
      return false;
  }
  return true;
}

uint64_t ExportEntry::readULEB128(const uint8_t *&Ptr, const char **error) {
  unsigned Count;
  uint64_t Result = decodeULEB128(Ptr, &Count, Trie.end(), error);
  Ptr += Count;
  if (Ptr > Trie.end())
    Ptr = Trie.end();
  return Result;
}

StringRef ExportEntry::name() const {
  return CumulativeString;
}

uint64_t ExportEntry::flags() const {
  return Stack.back().Flags;
}

uint64_t ExportEntry::address() const {
  return Stack.back().Address;
}

uint64_t ExportEntry::other() const {
  return Stack.back().Other;
}

StringRef ExportEntry::otherName() const {
  const char* ImportName = Stack.back().ImportName;
  if (ImportName)
    return StringRef(ImportName);
  return StringRef();
}

uint32_t ExportEntry::nodeOffset() const {
  return Stack.back().Start - Trie.begin();
}

ExportEntry::NodeState::NodeState(const uint8_t *Ptr)
    : Start(Ptr), Current(Ptr) {}

void ExportEntry::pushNode(uint64_t offset) {
  ErrorAsOutParameter ErrAsOutParam(E);
  const uint8_t *Ptr = Trie.begin() + offset;
  NodeState State(Ptr);
  const char *error;
  uint64_t ExportInfoSize = readULEB128(State.Current, &error);
  if (error) {
    *E = malformedError("export info size " + Twine(error) +
                        " in export trie data at node: 0x" +
                        Twine::utohexstr(offset));
    moveToEnd();
    return;
  }
  State.IsExportNode = (ExportInfoSize != 0);
  const uint8_t* Children = State.Current + ExportInfoSize;
  if (Children > Trie.end()) {
    *E = malformedError(
        "export info size: 0x" + Twine::utohexstr(ExportInfoSize) +
        " in export trie data at node: 0x" + Twine::utohexstr(offset) +
        " too big and extends past end of trie data");
    moveToEnd();
    return;
  }
  if (State.IsExportNode) {
    const uint8_t *ExportStart = State.Current;
    State.Flags = readULEB128(State.Current, &error);
    if (error) {
      *E = malformedError("flags " + Twine(error) +
                          " in export trie data at node: 0x" +
                          Twine::utohexstr(offset));
      moveToEnd();
      return;
    }
    uint64_t Kind = State.Flags & MachO::EXPORT_SYMBOL_FLAGS_KIND_MASK;
    if (State.Flags != 0 &&
        (Kind != MachO::EXPORT_SYMBOL_FLAGS_KIND_REGULAR &&
         Kind != MachO::EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE &&
         Kind != MachO::EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL)) {
      *E = malformedError(
          "unsupported exported symbol kind: " + Twine((int)Kind) +
          " in flags: 0x" + Twine::utohexstr(State.Flags) +
          " in export trie data at node: 0x" + Twine::utohexstr(offset));
      moveToEnd();
      return;
    }
    if (State.Flags & MachO::EXPORT_SYMBOL_FLAGS_REEXPORT) {
      State.Address = 0;
      State.Other = readULEB128(State.Current, &error); // dylib ordinal
      if (error) {
        *E = malformedError("dylib ordinal of re-export " + Twine(error) +
                            " in export trie data at node: 0x" +
                            Twine::utohexstr(offset));
        moveToEnd();
        return;
      }
      if (O != nullptr) {
        if (State.Other > O->getLibraryCount()) {
          *E = malformedError(
              "bad library ordinal: " + Twine((int)State.Other) + " (max " +
              Twine((int)O->getLibraryCount()) +
              ") in export trie data at node: 0x" + Twine::utohexstr(offset));
          moveToEnd();
          return;
        }
      }
      State.ImportName = reinterpret_cast<const char*>(State.Current);
      if (*State.ImportName == '\0') {
        State.Current++;
      } else {
        const uint8_t *End = State.Current + 1;
        if (End >= Trie.end()) {
          *E = malformedError("import name of re-export in export trie data at "
                              "node: 0x" +
                              Twine::utohexstr(offset) +
                              " starts past end of trie data");
          moveToEnd();
          return;
        }
        while(*End != '\0' && End < Trie.end())
          End++;
        if (*End != '\0') {
          *E = malformedError("import name of re-export in export trie data at "
                              "node: 0x" +
                              Twine::utohexstr(offset) +
                              " extends past end of trie data");
          moveToEnd();
          return;
        }
        State.Current = End + 1;
      }
    } else {
      State.Address = readULEB128(State.Current, &error);
      if (error) {
        *E = malformedError("address " + Twine(error) +
                            " in export trie data at node: 0x" +
                            Twine::utohexstr(offset));
        moveToEnd();
        return;
      }
      if (State.Flags & MachO::EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) {
        State.Other = readULEB128(State.Current, &error);
        if (error) {
          *E = malformedError("resolver of stub and resolver " + Twine(error) +
                              " in export trie data at node: 0x" +
                              Twine::utohexstr(offset));
          moveToEnd();
          return;
        }
      }
    }
    if(ExportStart + ExportInfoSize != State.Current) {
      *E = malformedError(
          "inconsistant export info size: 0x" +
          Twine::utohexstr(ExportInfoSize) + " where actual size was: 0x" +
          Twine::utohexstr(State.Current - ExportStart) +
          " in export trie data at node: 0x" + Twine::utohexstr(offset));
      moveToEnd();
      return;
    }
  }
  State.ChildCount = *Children;
  if (State.ChildCount != 0 && Children + 1 >= Trie.end()) {
    *E = malformedError("byte for count of childern in export trie data at "
                        "node: 0x" +
                        Twine::utohexstr(offset) +
                        " extends past end of trie data");
    moveToEnd();
    return;
  }
  State.Current = Children + 1;
  State.NextChildIndex = 0;
  State.ParentStringLength = CumulativeString.size();
  Stack.push_back(State);
}

void ExportEntry::pushDownUntilBottom() {
  ErrorAsOutParameter ErrAsOutParam(E);
  const char *error;
  while (Stack.back().NextChildIndex < Stack.back().ChildCount) {
    NodeState &Top = Stack.back();
    CumulativeString.resize(Top.ParentStringLength);
    for (;*Top.Current != 0 && Top.Current < Trie.end(); Top.Current++) {
      char C = *Top.Current;
      CumulativeString.push_back(C);
    }
    if (Top.Current >= Trie.end()) {
      *E = malformedError("edge sub-string in export trie data at node: 0x" +
                          Twine::utohexstr(Top.Start - Trie.begin()) +
                          " for child #" + Twine((int)Top.NextChildIndex) +
                          " extends past end of trie data");
      moveToEnd();
      return;
    }
    Top.Current += 1;
    uint64_t childNodeIndex = readULEB128(Top.Current, &error);
    if (error) {
      *E = malformedError("child node offset " + Twine(error) +
                          " in export trie data at node: 0x" +
                          Twine::utohexstr(Top.Start - Trie.begin()));
      moveToEnd();
      return;
    }
    for (const NodeState &node : nodes()) {
      if (node.Start == Trie.begin() + childNodeIndex){
        *E = malformedError("loop in childern in export trie data at node: 0x" +
                            Twine::utohexstr(Top.Start - Trie.begin()) +
                            " back to node: 0x" +
                            Twine::utohexstr(childNodeIndex));
        moveToEnd();
        return;
      }
    }
    Top.NextChildIndex += 1;
    pushNode(childNodeIndex);
    if (*E)
      return;
  }
  if (!Stack.back().IsExportNode) {
    *E = malformedError("node is not an export node in export trie data at "
                        "node: 0x" +
                        Twine::utohexstr(Stack.back().Start - Trie.begin()));
    moveToEnd();
    return;
  }
}

// We have a trie data structure and need a way to walk it that is compatible
// with the C++ iterator model. The solution is a non-recursive depth first
// traversal where the iterator contains a stack of parent nodes along with a
// string that is the accumulation of all edge strings along the parent chain
// to this point.
//
// There is one "export" node for each exported symbol.  But because some
// symbols may be a prefix of another symbol (e.g. _dup and _dup2), an export
// node may have child nodes too.
//
// The algorithm for moveNext() is to keep moving down the leftmost unvisited
// child until hitting a node with no children (which is an export node or
// else the trie is malformed). On the way down, each node is pushed on the
// stack ivar.  If there is no more ways down, it pops up one and tries to go
// down a sibling path until a childless node is reached.
void ExportEntry::moveNext() {
  assert(!Stack.empty() && "ExportEntry::moveNext() with empty node stack");
  if (!Stack.back().IsExportNode) {
    *E = malformedError("node is not an export node in export trie data at "
                        "node: 0x" +
                        Twine::utohexstr(Stack.back().Start - Trie.begin()));
    moveToEnd();
    return;
  }

  Stack.pop_back();
  while (!Stack.empty()) {
    NodeState &Top = Stack.back();
    if (Top.NextChildIndex < Top.ChildCount) {
      pushDownUntilBottom();
      // Now at the next export node.
      return;
    } else {
      if (Top.IsExportNode) {
        // This node has no children but is itself an export node.
        CumulativeString.resize(Top.ParentStringLength);
        return;
      }
      Stack.pop_back();
    }
  }
  Done = true;
}

iterator_range<export_iterator>
MachOObjectFile::exports(Error &E, ArrayRef<uint8_t> Trie,
                         const MachOObjectFile *O) {
  ExportEntry Start(&E, O, Trie);
  if (Trie.empty())
    Start.moveToEnd();
  else
    Start.moveToFirst();

  ExportEntry Finish(&E, O, Trie);
  Finish.moveToEnd();

  return make_range(export_iterator(Start), export_iterator(Finish));
}

iterator_range<export_iterator> MachOObjectFile::exports(Error &Err) const {
  return exports(Err, getDyldInfoExportsTrie(), this);
}

MachORebaseEntry::MachORebaseEntry(Error *E, const MachOObjectFile *O,
                                   ArrayRef<uint8_t> Bytes, bool is64Bit)
    : E(E), O(O), Opcodes(Bytes), Ptr(Bytes.begin()),
      PointerSize(is64Bit ? 8 : 4) {}

void MachORebaseEntry::moveToFirst() {
  Ptr = Opcodes.begin();
  moveNext();
}

void MachORebaseEntry::moveToEnd() {
  Ptr = Opcodes.end();
  RemainingLoopCount = 0;
  Done = true;
}

void MachORebaseEntry::moveNext() {
  ErrorAsOutParameter ErrAsOutParam(E);
  // If in the middle of some loop, move to next rebasing in loop.
  SegmentOffset += AdvanceAmount;
  if (RemainingLoopCount) {
    --RemainingLoopCount;
    return;
  }
  // REBASE_OPCODE_DONE is only used for padding if we are not aligned to
  // pointer size. Therefore it is possible to reach the end without ever having
  // seen REBASE_OPCODE_DONE.
  if (Ptr == Opcodes.end()) {
    Done = true;
    return;
  }
  bool More = true;
  while (More) {
    // Parse next opcode and set up next loop.
    const uint8_t *OpcodeStart = Ptr;
    uint8_t Byte = *Ptr++;
    uint8_t ImmValue = Byte & MachO::REBASE_IMMEDIATE_MASK;
    uint8_t Opcode = Byte & MachO::REBASE_OPCODE_MASK;
    uint32_t Count, Skip;
    const char *error = nullptr;
    switch (Opcode) {
    case MachO::REBASE_OPCODE_DONE:
      More = false;
      Done = true;
      moveToEnd();
      DEBUG_WITH_TYPE("mach-o-rebase", dbgs() << "REBASE_OPCODE_DONE\n");
      break;
    case MachO::REBASE_OPCODE_SET_TYPE_IMM:
      RebaseType = ImmValue;
      if (RebaseType > MachO::REBASE_TYPE_TEXT_PCREL32) {
        *E = malformedError("for REBASE_OPCODE_SET_TYPE_IMM bad bind type: " +
                            Twine((int)RebaseType) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-rebase",
          dbgs() << "REBASE_OPCODE_SET_TYPE_IMM: "
                 << "RebaseType=" << (int) RebaseType << "\n");
      break;
    case MachO::REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
      SegmentIndex = ImmValue;
      SegmentOffset = readULEB128(&error);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      error = O->RebaseEntryCheckSegAndOffset(SegmentIndex, SegmentOffset,
                                              true);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-rebase",
          dbgs() << "REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB: "
                 << "SegmentIndex=" << SegmentIndex << ", "
                 << format("SegmentOffset=0x%06X", SegmentOffset)
                 << "\n");
      break;
    case MachO::REBASE_OPCODE_ADD_ADDR_ULEB:
      SegmentOffset += readULEB128(&error);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_ADD_ADDR_ULEB " + Twine(error) +
                            " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      error = O->RebaseEntryCheckSegAndOffset(SegmentIndex, SegmentOffset,
                                              true);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_ADD_ADDR_ULEB " + Twine(error) +
                            " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE("mach-o-rebase",
                      dbgs() << "REBASE_OPCODE_ADD_ADDR_ULEB: "
                             << format("SegmentOffset=0x%06X",
                                       SegmentOffset) << "\n");
      break;
    case MachO::REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
      error = O->RebaseEntryCheckSegAndOffset(SegmentIndex, SegmentOffset,
                                              true);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_ADD_ADDR_IMM_SCALED " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      SegmentOffset += ImmValue * PointerSize;
      error = O->RebaseEntryCheckSegAndOffset(SegmentIndex, SegmentOffset,
                                              false);
      if (error) {
        *E =
            malformedError("for REBASE_OPCODE_ADD_ADDR_IMM_SCALED "
                           " (after adding immediate times the pointer size) " +
                           Twine(error) + " for opcode at: 0x" +
                           Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE("mach-o-rebase",
                      dbgs() << "REBASE_OPCODE_ADD_ADDR_IMM_SCALED: "
                             << format("SegmentOffset=0x%06X",
                                       SegmentOffset) << "\n");
      break;
    case MachO::REBASE_OPCODE_DO_REBASE_IMM_TIMES:
      error = O->RebaseEntryCheckSegAndOffset(SegmentIndex, SegmentOffset,
                                              true);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_IMM_TIMES " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      AdvanceAmount = PointerSize;
      Skip = 0;
      Count = ImmValue;
      if (ImmValue != 0)
        RemainingLoopCount = ImmValue - 1;
      else
        RemainingLoopCount = 0;
      error = O->RebaseEntryCheckCountAndSkip(Count, Skip, PointerSize,
                                              SegmentIndex, SegmentOffset);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_IMM_TIMES " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-rebase",
          dbgs() << "REBASE_OPCODE_DO_REBASE_IMM_TIMES: "
                 << format("SegmentOffset=0x%06X", SegmentOffset)
                 << ", AdvanceAmount=" << AdvanceAmount
                 << ", RemainingLoopCount=" << RemainingLoopCount
                 << "\n");
      return;
    case MachO::REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
      error = O->RebaseEntryCheckSegAndOffset(SegmentIndex, SegmentOffset,
                                              true);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_ULEB_TIMES " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      AdvanceAmount = PointerSize;
      Skip = 0;
      Count = readULEB128(&error);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_ULEB_TIMES " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (Count != 0)
        RemainingLoopCount = Count - 1;
      else
        RemainingLoopCount = 0;
      error = O->RebaseEntryCheckCountAndSkip(Count, Skip, PointerSize,
                                              SegmentIndex, SegmentOffset);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_ULEB_TIMES " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-rebase",
          dbgs() << "REBASE_OPCODE_DO_REBASE_ULEB_TIMES: "
                 << format("SegmentOffset=0x%06X", SegmentOffset)
                 << ", AdvanceAmount=" << AdvanceAmount
                 << ", RemainingLoopCount=" << RemainingLoopCount
                 << "\n");
      return;
    case MachO::REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
      error = O->RebaseEntryCheckSegAndOffset(SegmentIndex, SegmentOffset,
                                              true);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      Skip = readULEB128(&error);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      AdvanceAmount = Skip + PointerSize;
      Count = 1;
      RemainingLoopCount = 0;
      error = O->RebaseEntryCheckCountAndSkip(Count, Skip, PointerSize,
                                              SegmentIndex, SegmentOffset);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-rebase",
          dbgs() << "REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB: "
                 << format("SegmentOffset=0x%06X", SegmentOffset)
                 << ", AdvanceAmount=" << AdvanceAmount
                 << ", RemainingLoopCount=" << RemainingLoopCount
                 << "\n");
      return;
    case MachO::REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB:
      error = O->RebaseEntryCheckSegAndOffset(SegmentIndex, SegmentOffset,
                                              true);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_"
                            "ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      Count = readULEB128(&error);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_"
                            "ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (Count != 0)
        RemainingLoopCount = Count - 1;
      else
        RemainingLoopCount = 0;
      Skip = readULEB128(&error);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_"
                            "ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      AdvanceAmount = Skip + PointerSize;

      error = O->RebaseEntryCheckCountAndSkip(Count, Skip, PointerSize,
                                              SegmentIndex, SegmentOffset);
      if (error) {
        *E = malformedError("for REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_"
                            "ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-rebase",
          dbgs() << "REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB: "
                 << format("SegmentOffset=0x%06X", SegmentOffset)
                 << ", AdvanceAmount=" << AdvanceAmount
                 << ", RemainingLoopCount=" << RemainingLoopCount
                 << "\n");
      return;
    default:
      *E = malformedError("bad rebase info (bad opcode value 0x" +
                          Twine::utohexstr(Opcode) + " for opcode at: 0x" +
                          Twine::utohexstr(OpcodeStart - Opcodes.begin()));
      moveToEnd();
      return;
    }
  }
}

uint64_t MachORebaseEntry::readULEB128(const char **error) {
  unsigned Count;
  uint64_t Result = decodeULEB128(Ptr, &Count, Opcodes.end(), error);
  Ptr += Count;
  if (Ptr > Opcodes.end())
    Ptr = Opcodes.end();
  return Result;
}

int32_t MachORebaseEntry::segmentIndex() const { return SegmentIndex; }

uint64_t MachORebaseEntry::segmentOffset() const { return SegmentOffset; }

StringRef MachORebaseEntry::typeName() const {
  switch (RebaseType) {
  case MachO::REBASE_TYPE_POINTER:
    return "pointer";
  case MachO::REBASE_TYPE_TEXT_ABSOLUTE32:
    return "text abs32";
  case MachO::REBASE_TYPE_TEXT_PCREL32:
    return "text rel32";
  }
  return "unknown";
}

// For use with the SegIndex of a checked Mach-O Rebase entry
// to get the segment name.
StringRef MachORebaseEntry::segmentName() const {
  return O->BindRebaseSegmentName(SegmentIndex);
}

// For use with a SegIndex,SegOffset pair from a checked Mach-O Rebase entry
// to get the section name.
StringRef MachORebaseEntry::sectionName() const {
  return O->BindRebaseSectionName(SegmentIndex, SegmentOffset);
}

// For use with a SegIndex,SegOffset pair from a checked Mach-O Rebase entry
// to get the address.
uint64_t MachORebaseEntry::address() const {
  return O->BindRebaseAddress(SegmentIndex, SegmentOffset);
}

bool MachORebaseEntry::operator==(const MachORebaseEntry &Other) const {
#ifdef EXPENSIVE_CHECKS
  assert(Opcodes == Other.Opcodes && "compare iterators of different files");
#else
  assert(Opcodes.data() == Other.Opcodes.data() && "compare iterators of different files");
#endif
  return (Ptr == Other.Ptr) &&
         (RemainingLoopCount == Other.RemainingLoopCount) &&
         (Done == Other.Done);
}

iterator_range<rebase_iterator>
MachOObjectFile::rebaseTable(Error &Err, MachOObjectFile *O,
                             ArrayRef<uint8_t> Opcodes, bool is64) {
  if (O->BindRebaseSectionTable == nullptr)
    O->BindRebaseSectionTable = llvm::make_unique<BindRebaseSegInfo>(O);
  MachORebaseEntry Start(&Err, O, Opcodes, is64);
  Start.moveToFirst();

  MachORebaseEntry Finish(&Err, O, Opcodes, is64);
  Finish.moveToEnd();

  return make_range(rebase_iterator(Start), rebase_iterator(Finish));
}

iterator_range<rebase_iterator> MachOObjectFile::rebaseTable(Error &Err) {
  return rebaseTable(Err, this, getDyldInfoRebaseOpcodes(), is64Bit());
}

MachOBindEntry::MachOBindEntry(Error *E, const MachOObjectFile *O,
                               ArrayRef<uint8_t> Bytes, bool is64Bit, Kind BK)
    : E(E), O(O), Opcodes(Bytes), Ptr(Bytes.begin()),
      PointerSize(is64Bit ? 8 : 4), TableKind(BK) {}

void MachOBindEntry::moveToFirst() {
  Ptr = Opcodes.begin();
  moveNext();
}

void MachOBindEntry::moveToEnd() {
  Ptr = Opcodes.end();
  RemainingLoopCount = 0;
  Done = true;
}

void MachOBindEntry::moveNext() {
  ErrorAsOutParameter ErrAsOutParam(E);
  // If in the middle of some loop, move to next binding in loop.
  SegmentOffset += AdvanceAmount;
  if (RemainingLoopCount) {
    --RemainingLoopCount;
    return;
  }
  // BIND_OPCODE_DONE is only used for padding if we are not aligned to
  // pointer size. Therefore it is possible to reach the end without ever having
  // seen BIND_OPCODE_DONE.
  if (Ptr == Opcodes.end()) {
    Done = true;
    return;
  }
  bool More = true;
  while (More) {
    // Parse next opcode and set up next loop.
    const uint8_t *OpcodeStart = Ptr;
    uint8_t Byte = *Ptr++;
    uint8_t ImmValue = Byte & MachO::BIND_IMMEDIATE_MASK;
    uint8_t Opcode = Byte & MachO::BIND_OPCODE_MASK;
    int8_t SignExtended;
    const uint8_t *SymStart;
    uint32_t Count, Skip;
    const char *error = nullptr;
    switch (Opcode) {
    case MachO::BIND_OPCODE_DONE:
      if (TableKind == Kind::Lazy) {
        // Lazying bindings have a DONE opcode between entries.  Need to ignore
        // it to advance to next entry.  But need not if this is last entry.
        bool NotLastEntry = false;
        for (const uint8_t *P = Ptr; P < Opcodes.end(); ++P) {
          if (*P) {
            NotLastEntry = true;
          }
        }
        if (NotLastEntry)
          break;
      }
      More = false;
      moveToEnd();
      DEBUG_WITH_TYPE("mach-o-bind", dbgs() << "BIND_OPCODE_DONE\n");
      break;
    case MachO::BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
      if (TableKind == Kind::Weak) {
        *E = malformedError("BIND_OPCODE_SET_DYLIB_ORDINAL_IMM not allowed in "
                            "weak bind table for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      Ordinal = ImmValue;
      LibraryOrdinalSet = true;
      if (ImmValue > O->getLibraryCount()) {
        *E = malformedError("for BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB bad "
                            "library ordinal: " +
                            Twine((int)ImmValue) + " (max " +
                            Twine((int)O->getLibraryCount()) +
                            ") for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-bind",
          dbgs() << "BIND_OPCODE_SET_DYLIB_ORDINAL_IMM: "
                 << "Ordinal=" << Ordinal << "\n");
      break;
    case MachO::BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
      if (TableKind == Kind::Weak) {
        *E = malformedError("BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB not allowed in "
                            "weak bind table for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      Ordinal = readULEB128(&error);
      LibraryOrdinalSet = true;
      if (error) {
        *E = malformedError("for BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (Ordinal > (int)O->getLibraryCount()) {
        *E = malformedError("for BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB bad "
                            "library ordinal: " +
                            Twine((int)Ordinal) + " (max " +
                            Twine((int)O->getLibraryCount()) +
                            ") for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-bind",
          dbgs() << "BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB: "
                 << "Ordinal=" << Ordinal << "\n");
      break;
    case MachO::BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
      if (TableKind == Kind::Weak) {
        *E = malformedError("BIND_OPCODE_SET_DYLIB_SPECIAL_IMM not allowed in "
                            "weak bind table for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (ImmValue) {
        SignExtended = MachO::BIND_OPCODE_MASK | ImmValue;
        Ordinal = SignExtended;
        if (Ordinal < MachO::BIND_SPECIAL_DYLIB_FLAT_LOOKUP) {
          *E = malformedError("for BIND_OPCODE_SET_DYLIB_SPECIAL_IMM unknown "
                              "special ordinal: " +
                              Twine((int)Ordinal) + " for opcode at: 0x" +
                              Twine::utohexstr(OpcodeStart - Opcodes.begin()));
          moveToEnd();
          return;
        }
      } else
        Ordinal = 0;
      LibraryOrdinalSet = true;
      DEBUG_WITH_TYPE(
          "mach-o-bind",
          dbgs() << "BIND_OPCODE_SET_DYLIB_SPECIAL_IMM: "
                 << "Ordinal=" << Ordinal << "\n");
      break;
    case MachO::BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
      Flags = ImmValue;
      SymStart = Ptr;
      while (*Ptr && (Ptr < Opcodes.end())) {
        ++Ptr;
      }
      if (Ptr == Opcodes.end()) {
        *E = malformedError(
            "for BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM "
            "symbol name extends past opcodes for opcode at: 0x" +
            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      SymbolName = StringRef(reinterpret_cast<const char*>(SymStart),
                             Ptr-SymStart);
      ++Ptr;
      DEBUG_WITH_TYPE(
          "mach-o-bind",
          dbgs() << "BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM: "
                 << "SymbolName=" << SymbolName << "\n");
      if (TableKind == Kind::Weak) {
        if (ImmValue & MachO::BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION)
          return;
      }
      break;
    case MachO::BIND_OPCODE_SET_TYPE_IMM:
      BindType = ImmValue;
      if (ImmValue > MachO::BIND_TYPE_TEXT_PCREL32) {
        *E = malformedError("for BIND_OPCODE_SET_TYPE_IMM bad bind type: " +
                            Twine((int)ImmValue) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-bind",
          dbgs() << "BIND_OPCODE_SET_TYPE_IMM: "
                 << "BindType=" << (int)BindType << "\n");
      break;
    case MachO::BIND_OPCODE_SET_ADDEND_SLEB:
      Addend = readSLEB128(&error);
      if (error) {
        *E = malformedError("for BIND_OPCODE_SET_ADDEND_SLEB " + Twine(error) +
                            " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-bind",
          dbgs() << "BIND_OPCODE_SET_ADDEND_SLEB: "
                 << "Addend=" << Addend << "\n");
      break;
    case MachO::BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
      SegmentIndex = ImmValue;
      SegmentOffset = readULEB128(&error);
      if (error) {
        *E = malformedError("for BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      error = O->BindEntryCheckSegAndOffset(SegmentIndex, SegmentOffset, true);
      if (error) {
        *E = malformedError("for BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-bind",
          dbgs() << "BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB: "
                 << "SegmentIndex=" << SegmentIndex << ", "
                 << format("SegmentOffset=0x%06X", SegmentOffset)
                 << "\n");
      break;
    case MachO::BIND_OPCODE_ADD_ADDR_ULEB:
      SegmentOffset += readULEB128(&error);
      if (error) {
        *E = malformedError("for BIND_OPCODE_ADD_ADDR_ULEB " + Twine(error) +
                            " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      error = O->BindEntryCheckSegAndOffset(SegmentIndex, SegmentOffset, true);
      if (error) {
        *E = malformedError("for BIND_OPCODE_ADD_ADDR_ULEB " + Twine(error) +
                            " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE("mach-o-bind",
                      dbgs() << "BIND_OPCODE_ADD_ADDR_ULEB: "
                             << format("SegmentOffset=0x%06X",
                                       SegmentOffset) << "\n");
      break;
    case MachO::BIND_OPCODE_DO_BIND:
      AdvanceAmount = PointerSize;
      RemainingLoopCount = 0;
      error = O->BindEntryCheckSegAndOffset(SegmentIndex, SegmentOffset, true);
      if (error) {
        *E = malformedError("for BIND_OPCODE_DO_BIND " + Twine(error) +
                            " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (SymbolName == StringRef()) {
        *E = malformedError(
            "for BIND_OPCODE_DO_BIND missing preceding "
            "BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM for opcode at: 0x" +
            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (!LibraryOrdinalSet && TableKind != Kind::Weak) {
        *E =
            malformedError("for BIND_OPCODE_DO_BIND missing preceding "
                           "BIND_OPCODE_SET_DYLIB_ORDINAL_* for opcode at: 0x" +
                           Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE("mach-o-bind",
                      dbgs() << "BIND_OPCODE_DO_BIND: "
                             << format("SegmentOffset=0x%06X",
                                       SegmentOffset) << "\n");
      return;
     case MachO::BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
      if (TableKind == Kind::Lazy) {
        *E = malformedError("BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB not allowed in "
                            "lazy bind table for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      error = O->BindEntryCheckSegAndOffset(SegmentIndex, SegmentOffset, true);
      if (error) {
        *E = malformedError("for BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (SymbolName == StringRef()) {
        *E = malformedError(
            "for BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB missing "
            "preceding BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM for opcode "
            "at: 0x" +
            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (!LibraryOrdinalSet && TableKind != Kind::Weak) {
        *E = malformedError(
            "for BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB missing "
            "preceding BIND_OPCODE_SET_DYLIB_ORDINAL_* for opcode at: 0x" +
            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      AdvanceAmount = readULEB128(&error) + PointerSize;
      if (error) {
        *E = malformedError("for BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      // Note, this is not really an error until the next bind but make no sense
      // for a BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB to not be followed by another
      // bind operation.
      error = O->BindEntryCheckSegAndOffset(SegmentIndex, SegmentOffset +
                                            AdvanceAmount, false);
      if (error) {
        *E = malformedError("for BIND_OPCODE_ADD_ADDR_ULEB (after adding "
                            "ULEB) " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      RemainingLoopCount = 0;
      DEBUG_WITH_TYPE(
          "mach-o-bind",
          dbgs() << "BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB: "
                 << format("SegmentOffset=0x%06X", SegmentOffset)
                 << ", AdvanceAmount=" << AdvanceAmount
                 << ", RemainingLoopCount=" << RemainingLoopCount
                 << "\n");
      return;
    case MachO::BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
      if (TableKind == Kind::Lazy) {
        *E = malformedError("BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED not "
                            "allowed in lazy bind table for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      error = O->BindEntryCheckSegAndOffset(SegmentIndex, SegmentOffset, true);
      if (error) {
        *E = malformedError("for BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (SymbolName == StringRef()) {
        *E = malformedError(
            "for BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED "
            "missing preceding BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM for "
            "opcode at: 0x" +
            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (!LibraryOrdinalSet && TableKind != Kind::Weak) {
        *E = malformedError(
            "for BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED "
            "missing preceding BIND_OPCODE_SET_DYLIB_ORDINAL_* for opcode "
            "at: 0x" +
            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      AdvanceAmount = ImmValue * PointerSize + PointerSize;
      RemainingLoopCount = 0;
      error = O->BindEntryCheckSegAndOffset(SegmentIndex, SegmentOffset +
                                            AdvanceAmount, false);
      if (error) {
        *E =
            malformedError("for BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED "
                           " (after adding immediate times the pointer size) " +
                           Twine(error) + " for opcode at: 0x" +
                           Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE("mach-o-bind",
                      dbgs()
                      << "BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED: "
                      << format("SegmentOffset=0x%06X", SegmentOffset) << "\n");
      return;
    case MachO::BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
      if (TableKind == Kind::Lazy) {
        *E = malformedError("BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB not "
                            "allowed in lazy bind table for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      Count = readULEB128(&error);
      if (Count != 0)
        RemainingLoopCount = Count - 1;
      else
        RemainingLoopCount = 0;
      if (error) {
        *E = malformedError("for BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB "
                            " (count value) " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      Skip = readULEB128(&error);
      AdvanceAmount = Skip + PointerSize;
      if (error) {
        *E = malformedError("for BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB "
                            " (skip value) " +
                            Twine(error) + " for opcode at: 0x" +
                            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      error = O->BindEntryCheckSegAndOffset(SegmentIndex, SegmentOffset, true);
      if (error) {
        *E =
            malformedError("for BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB " +
                           Twine(error) + " for opcode at: 0x" +
                           Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (SymbolName == StringRef()) {
        *E = malformedError(
            "for BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB "
            "missing preceding BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM for "
            "opcode at: 0x" +
            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      if (!LibraryOrdinalSet && TableKind != Kind::Weak) {
        *E = malformedError(
            "for BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB "
            "missing preceding BIND_OPCODE_SET_DYLIB_ORDINAL_* for opcode "
            "at: 0x" +
            Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      error = O->BindEntryCheckCountAndSkip(Count, Skip, PointerSize,
                                            SegmentIndex, SegmentOffset);
      if (error) {
        *E =
            malformedError("for BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB " +
                           Twine(error) + " for opcode at: 0x" +
                           Twine::utohexstr(OpcodeStart - Opcodes.begin()));
        moveToEnd();
        return;
      }
      DEBUG_WITH_TYPE(
          "mach-o-bind",
          dbgs() << "BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB: "
                 << format("SegmentOffset=0x%06X", SegmentOffset)
                 << ", AdvanceAmount=" << AdvanceAmount
                 << ", RemainingLoopCount=" << RemainingLoopCount
                 << "\n");
      return;
    default:
      *E = malformedError("bad bind info (bad opcode value 0x" +
                          Twine::utohexstr(Opcode) + " for opcode at: 0x" +
                          Twine::utohexstr(OpcodeStart - Opcodes.begin()));
      moveToEnd();
      return;
    }
  }
}

uint64_t MachOBindEntry::readULEB128(const char **error) {
  unsigned Count;
  uint64_t Result = decodeULEB128(Ptr, &Count, Opcodes.end(), error);
  Ptr += Count;
  if (Ptr > Opcodes.end())
    Ptr = Opcodes.end();
  return Result;
}

int64_t MachOBindEntry::readSLEB128(const char **error) {
  unsigned Count;
  int64_t Result = decodeSLEB128(Ptr, &Count, Opcodes.end(), error);
  Ptr += Count;
  if (Ptr > Opcodes.end())
    Ptr = Opcodes.end();
  return Result;
}

int32_t MachOBindEntry::segmentIndex() const { return SegmentIndex; }

uint64_t MachOBindEntry::segmentOffset() const { return SegmentOffset; }

StringRef MachOBindEntry::typeName() const {
  switch (BindType) {
  case MachO::BIND_TYPE_POINTER:
    return "pointer";
  case MachO::BIND_TYPE_TEXT_ABSOLUTE32:
    return "text abs32";
  case MachO::BIND_TYPE_TEXT_PCREL32:
    return "text rel32";
  }
  return "unknown";
}

StringRef MachOBindEntry::symbolName() const { return SymbolName; }

int64_t MachOBindEntry::addend() const { return Addend; }

uint32_t MachOBindEntry::flags() const { return Flags; }

int MachOBindEntry::ordinal() const { return Ordinal; }

// For use with the SegIndex of a checked Mach-O Bind entry
// to get the segment name.
StringRef MachOBindEntry::segmentName() const {
  return O->BindRebaseSegmentName(SegmentIndex);
}

// For use with a SegIndex,SegOffset pair from a checked Mach-O Bind entry
// to get the section name.
StringRef MachOBindEntry::sectionName() const {
  return O->BindRebaseSectionName(SegmentIndex, SegmentOffset);
}

// For use with a SegIndex,SegOffset pair from a checked Mach-O Bind entry
// to get the address.
uint64_t MachOBindEntry::address() const {
  return O->BindRebaseAddress(SegmentIndex, SegmentOffset);
}

bool MachOBindEntry::operator==(const MachOBindEntry &Other) const {
#ifdef EXPENSIVE_CHECKS
  assert(Opcodes == Other.Opcodes && "compare iterators of different files");
#else
  assert(Opcodes.data() == Other.Opcodes.data() && "compare iterators of different files");
#endif
  return (Ptr == Other.Ptr) &&
         (RemainingLoopCount == Other.RemainingLoopCount) &&
         (Done == Other.Done);
}

// Build table of sections so SegIndex/SegOffset pairs can be translated.
BindRebaseSegInfo::BindRebaseSegInfo(const object::MachOObjectFile *Obj) {
  uint32_t CurSegIndex = Obj->hasPageZeroSegment() ? 1 : 0;
  StringRef CurSegName;
  uint64_t CurSegAddress;
  for (const SectionRef &Section : Obj->sections()) {
    SectionInfo Info;
    Section.getName(Info.SectionName);
    Info.Address = Section.getAddress();
    Info.Size = Section.getSize();
    Info.SegmentName =
        Obj->getSectionFinalSegmentName(Section.getRawDataRefImpl());
    if (!Info.SegmentName.equals(CurSegName)) {
      ++CurSegIndex;
      CurSegName = Info.SegmentName;
      CurSegAddress = Info.Address;
    }
    Info.SegmentIndex = CurSegIndex - 1;
    Info.OffsetInSegment = Info.Address - CurSegAddress;
    Info.SegmentStartAddress = CurSegAddress;
    Sections.push_back(Info);
  }
  MaxSegIndex = CurSegIndex;
}

// For use with a SegIndex,SegOffset pair in MachOBindEntry::moveNext() to
// validate a MachOBindEntry or MachORebaseEntry.
const char * BindRebaseSegInfo::checkSegAndOffset(int32_t SegIndex,
                                                  uint64_t SegOffset,
                                                  bool endInvalid) {
  if (SegIndex == -1)
    return "missing preceding *_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB";
  if (SegIndex >= MaxSegIndex)
    return "bad segIndex (too large)";
  for (const SectionInfo &SI : Sections) {
    if (SI.SegmentIndex != SegIndex)
      continue;
    if (SI.OffsetInSegment > SegOffset)
      continue;
    if (SegOffset > (SI.OffsetInSegment + SI.Size))
      continue;
    if (endInvalid && SegOffset >= (SI.OffsetInSegment + SI.Size))
      continue;
    return nullptr;
  }
  return "bad segOffset, too large";
}

// For use in MachOBindEntry::moveNext() to validate a MachOBindEntry for
// the BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB opcode and for use in
// MachORebaseEntry::moveNext() to validate a MachORebaseEntry for
// REBASE_OPCODE_DO_*_TIMES* opcodes.  The SegIndex and SegOffset must have
// been already checked.
const char * BindRebaseSegInfo::checkCountAndSkip(uint32_t Count, uint32_t Skip,
                                                  uint8_t PointerSize,
                                                  int32_t SegIndex,
                                                  uint64_t SegOffset) {
  const SectionInfo &SI = findSection(SegIndex, SegOffset);
  uint64_t addr = SI.SegmentStartAddress + SegOffset;
  if (addr >= SI.Address + SI.Size)
    return "bad segOffset, too large";
  uint64_t i = 0;
  if (Count > 1)
    i = (Skip + PointerSize) * (Count - 1);
  else if (Count == 1)
    i = Skip + PointerSize;
  if (addr + i >= SI.Address + SI.Size) {
    // For rebase opcodes they can step from one section to another.
    uint64_t TrailingSegOffset = (addr + i) - SI.SegmentStartAddress;
    const char *error = checkSegAndOffset(SegIndex, TrailingSegOffset, false);
    if (error)
      return "bad count and skip, too large";
  }
  return nullptr;
}

// For use with the SegIndex of a checked Mach-O Bind or Rebase entry
// to get the segment name.
StringRef BindRebaseSegInfo::segmentName(int32_t SegIndex) {
  for (const SectionInfo &SI : Sections) {
    if (SI.SegmentIndex == SegIndex)
      return SI.SegmentName;
  }
  llvm_unreachable("invalid SegIndex");
}

// For use with a SegIndex,SegOffset pair from a checked Mach-O Bind or Rebase
// to get the SectionInfo.
const BindRebaseSegInfo::SectionInfo &BindRebaseSegInfo::findSection(
                                     int32_t SegIndex, uint64_t SegOffset) {
  for (const SectionInfo &SI : Sections) {
    if (SI.SegmentIndex != SegIndex)
      continue;
    if (SI.OffsetInSegment > SegOffset)
      continue;
    if (SegOffset >= (SI.OffsetInSegment + SI.Size))
      continue;
    return SI;
  }
  llvm_unreachable("SegIndex and SegOffset not in any section");
}

// For use with a SegIndex,SegOffset pair from a checked Mach-O Bind or Rebase
// entry to get the section name.
StringRef BindRebaseSegInfo::sectionName(int32_t SegIndex,
                                         uint64_t SegOffset) {
  return findSection(SegIndex, SegOffset).SectionName;
}

// For use with a SegIndex,SegOffset pair from a checked Mach-O Bind or Rebase
// entry to get the address.
uint64_t BindRebaseSegInfo::address(uint32_t SegIndex, uint64_t OffsetInSeg) {
  const SectionInfo &SI = findSection(SegIndex, OffsetInSeg);
  return SI.SegmentStartAddress + OffsetInSeg;
}

iterator_range<bind_iterator>
MachOObjectFile::bindTable(Error &Err, MachOObjectFile *O,
                           ArrayRef<uint8_t> Opcodes, bool is64,
                           MachOBindEntry::Kind BKind) {
  if (O->BindRebaseSectionTable == nullptr)
    O->BindRebaseSectionTable = llvm::make_unique<BindRebaseSegInfo>(O);
  MachOBindEntry Start(&Err, O, Opcodes, is64, BKind);
  Start.moveToFirst();

  MachOBindEntry Finish(&Err, O, Opcodes, is64, BKind);
  Finish.moveToEnd();

  return make_range(bind_iterator(Start), bind_iterator(Finish));
}

iterator_range<bind_iterator> MachOObjectFile::bindTable(Error &Err) {
  return bindTable(Err, this, getDyldInfoBindOpcodes(), is64Bit(),
                   MachOBindEntry::Kind::Regular);
}

iterator_range<bind_iterator> MachOObjectFile::lazyBindTable(Error &Err) {
  return bindTable(Err, this, getDyldInfoLazyBindOpcodes(), is64Bit(),
                   MachOBindEntry::Kind::Lazy);
}

iterator_range<bind_iterator> MachOObjectFile::weakBindTable(Error &Err) {
  return bindTable(Err, this, getDyldInfoWeakBindOpcodes(), is64Bit(),
                   MachOBindEntry::Kind::Weak);
}

MachOObjectFile::load_command_iterator
MachOObjectFile::begin_load_commands() const {
  return LoadCommands.begin();
}

MachOObjectFile::load_command_iterator
MachOObjectFile::end_load_commands() const {
  return LoadCommands.end();
}

iterator_range<MachOObjectFile::load_command_iterator>
MachOObjectFile::load_commands() const {
  return make_range(begin_load_commands(), end_load_commands());
}

StringRef
MachOObjectFile::getSectionFinalSegmentName(DataRefImpl Sec) const {
  ArrayRef<char> Raw = getSectionRawFinalSegmentName(Sec);
  return parseSegmentOrSectionName(Raw.data());
}

ArrayRef<char>
MachOObjectFile::getSectionRawName(DataRefImpl Sec) const {
  assert(Sec.d.a < Sections.size() && "Should have detected this earlier");
  const section_base *Base =
    reinterpret_cast<const section_base *>(Sections[Sec.d.a]);
  return makeArrayRef(Base->sectname);
}

ArrayRef<char>
MachOObjectFile::getSectionRawFinalSegmentName(DataRefImpl Sec) const {
  assert(Sec.d.a < Sections.size() && "Should have detected this earlier");
  const section_base *Base =
    reinterpret_cast<const section_base *>(Sections[Sec.d.a]);
  return makeArrayRef(Base->segname);
}

bool
MachOObjectFile::isRelocationScattered(const MachO::any_relocation_info &RE)
  const {
  if (getCPUType(*this) == MachO::CPU_TYPE_X86_64)
    return false;
  return getPlainRelocationAddress(RE) & MachO::R_SCATTERED;
}

unsigned MachOObjectFile::getPlainRelocationSymbolNum(
    const MachO::any_relocation_info &RE) const {
  if (isLittleEndian())
    return RE.r_word1 & 0xffffff;
  return RE.r_word1 >> 8;
}

bool MachOObjectFile::getPlainRelocationExternal(
    const MachO::any_relocation_info &RE) const {
  if (isLittleEndian())
    return (RE.r_word1 >> 27) & 1;
  return (RE.r_word1 >> 4) & 1;
}

bool MachOObjectFile::getScatteredRelocationScattered(
    const MachO::any_relocation_info &RE) const {
  return RE.r_word0 >> 31;
}

uint32_t MachOObjectFile::getScatteredRelocationValue(
    const MachO::any_relocation_info &RE) const {
  return RE.r_word1;
}

uint32_t MachOObjectFile::getScatteredRelocationType(
    const MachO::any_relocation_info &RE) const {
  return (RE.r_word0 >> 24) & 0xf;
}

unsigned MachOObjectFile::getAnyRelocationAddress(
    const MachO::any_relocation_info &RE) const {
  if (isRelocationScattered(RE))
    return getScatteredRelocationAddress(RE);
  return getPlainRelocationAddress(RE);
}

unsigned MachOObjectFile::getAnyRelocationPCRel(
    const MachO::any_relocation_info &RE) const {
  if (isRelocationScattered(RE))
    return getScatteredRelocationPCRel(RE);
  return getPlainRelocationPCRel(*this, RE);
}

unsigned MachOObjectFile::getAnyRelocationLength(
    const MachO::any_relocation_info &RE) const {
  if (isRelocationScattered(RE))
    return getScatteredRelocationLength(RE);
  return getPlainRelocationLength(*this, RE);
}

unsigned
MachOObjectFile::getAnyRelocationType(
                                   const MachO::any_relocation_info &RE) const {
  if (isRelocationScattered(RE))
    return getScatteredRelocationType(RE);
  return getPlainRelocationType(*this, RE);
}

SectionRef
MachOObjectFile::getAnyRelocationSection(
                                   const MachO::any_relocation_info &RE) const {
  if (isRelocationScattered(RE) || getPlainRelocationExternal(RE))
    return *section_end();
  unsigned SecNum = getPlainRelocationSymbolNum(RE);
  if (SecNum == MachO::R_ABS || SecNum > Sections.size())
    return *section_end();
  DataRefImpl DRI;
  DRI.d.a = SecNum - 1;
  return SectionRef(DRI, this);
}

MachO::section MachOObjectFile::getSection(DataRefImpl DRI) const {
  assert(DRI.d.a < Sections.size() && "Should have detected this earlier");
  return getStruct<MachO::section>(*this, Sections[DRI.d.a]);
}

MachO::section_64 MachOObjectFile::getSection64(DataRefImpl DRI) const {
  assert(DRI.d.a < Sections.size() && "Should have detected this earlier");
  return getStruct<MachO::section_64>(*this, Sections[DRI.d.a]);
}

MachO::section MachOObjectFile::getSection(const LoadCommandInfo &L,
                                           unsigned Index) const {
  const char *Sec = getSectionPtr(*this, L, Index);
  return getStruct<MachO::section>(*this, Sec);
}

MachO::section_64 MachOObjectFile::getSection64(const LoadCommandInfo &L,
                                                unsigned Index) const {
  const char *Sec = getSectionPtr(*this, L, Index);
  return getStruct<MachO::section_64>(*this, Sec);
}

MachO::nlist
MachOObjectFile::getSymbolTableEntry(DataRefImpl DRI) const {
  const char *P = reinterpret_cast<const char *>(DRI.p);
  return getStruct<MachO::nlist>(*this, P);
}

MachO::nlist_64
MachOObjectFile::getSymbol64TableEntry(DataRefImpl DRI) const {
  const char *P = reinterpret_cast<const char *>(DRI.p);
  return getStruct<MachO::nlist_64>(*this, P);
}

MachO::linkedit_data_command
MachOObjectFile::getLinkeditDataLoadCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::linkedit_data_command>(*this, L.Ptr);
}

MachO::segment_command
MachOObjectFile::getSegmentLoadCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::segment_command>(*this, L.Ptr);
}

MachO::segment_command_64
MachOObjectFile::getSegment64LoadCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::segment_command_64>(*this, L.Ptr);
}

MachO::linker_option_command
MachOObjectFile::getLinkerOptionLoadCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::linker_option_command>(*this, L.Ptr);
}

MachO::version_min_command
MachOObjectFile::getVersionMinLoadCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::version_min_command>(*this, L.Ptr);
}

MachO::note_command
MachOObjectFile::getNoteLoadCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::note_command>(*this, L.Ptr);
}

MachO::build_version_command
MachOObjectFile::getBuildVersionLoadCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::build_version_command>(*this, L.Ptr);
}

MachO::build_tool_version
MachOObjectFile::getBuildToolVersion(unsigned index) const {
  return getStruct<MachO::build_tool_version>(*this, BuildTools[index]);
}

MachO::dylib_command
MachOObjectFile::getDylibIDLoadCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::dylib_command>(*this, L.Ptr);
}

MachO::dyld_info_command
MachOObjectFile::getDyldInfoLoadCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::dyld_info_command>(*this, L.Ptr);
}

MachO::dylinker_command
MachOObjectFile::getDylinkerCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::dylinker_command>(*this, L.Ptr);
}

MachO::uuid_command
MachOObjectFile::getUuidCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::uuid_command>(*this, L.Ptr);
}

MachO::rpath_command
MachOObjectFile::getRpathCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::rpath_command>(*this, L.Ptr);
}

MachO::source_version_command
MachOObjectFile::getSourceVersionCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::source_version_command>(*this, L.Ptr);
}

MachO::entry_point_command
MachOObjectFile::getEntryPointCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::entry_point_command>(*this, L.Ptr);
}

MachO::encryption_info_command
MachOObjectFile::getEncryptionInfoCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::encryption_info_command>(*this, L.Ptr);
}

MachO::encryption_info_command_64
MachOObjectFile::getEncryptionInfoCommand64(const LoadCommandInfo &L) const {
  return getStruct<MachO::encryption_info_command_64>(*this, L.Ptr);
}

MachO::sub_framework_command
MachOObjectFile::getSubFrameworkCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::sub_framework_command>(*this, L.Ptr);
}

MachO::sub_umbrella_command
MachOObjectFile::getSubUmbrellaCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::sub_umbrella_command>(*this, L.Ptr);
}

MachO::sub_library_command
MachOObjectFile::getSubLibraryCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::sub_library_command>(*this, L.Ptr);
}

MachO::sub_client_command
MachOObjectFile::getSubClientCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::sub_client_command>(*this, L.Ptr);
}

MachO::routines_command
MachOObjectFile::getRoutinesCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::routines_command>(*this, L.Ptr);
}

MachO::routines_command_64
MachOObjectFile::getRoutinesCommand64(const LoadCommandInfo &L) const {
  return getStruct<MachO::routines_command_64>(*this, L.Ptr);
}

MachO::thread_command
MachOObjectFile::getThreadCommand(const LoadCommandInfo &L) const {
  return getStruct<MachO::thread_command>(*this, L.Ptr);
}

MachO::any_relocation_info
MachOObjectFile::getRelocation(DataRefImpl Rel) const {
  uint32_t Offset;
  if (getHeader().filetype == MachO::MH_OBJECT) {
    DataRefImpl Sec;
    Sec.d.a = Rel.d.a;
    if (is64Bit()) {
      MachO::section_64 Sect = getSection64(Sec);
      Offset = Sect.reloff;
    } else {
      MachO::section Sect = getSection(Sec);
      Offset = Sect.reloff;
    }
  } else {
    MachO::dysymtab_command DysymtabLoadCmd = getDysymtabLoadCommand();
    if (Rel.d.a == 0)
      Offset = DysymtabLoadCmd.extreloff; // Offset to the external relocations
    else
      Offset = DysymtabLoadCmd.locreloff; // Offset to the local relocations
  }

  auto P = reinterpret_cast<const MachO::any_relocation_info *>(
      getPtr(*this, Offset)) + Rel.d.b;
  return getStruct<MachO::any_relocation_info>(
      *this, reinterpret_cast<const char *>(P));
}

MachO::data_in_code_entry
MachOObjectFile::getDice(DataRefImpl Rel) const {
  const char *P = reinterpret_cast<const char *>(Rel.p);
  return getStruct<MachO::data_in_code_entry>(*this, P);
}

const MachO::mach_header &MachOObjectFile::getHeader() const {
  return Header;
}

const MachO::mach_header_64 &MachOObjectFile::getHeader64() const {
  assert(is64Bit());
  return Header64;
}

uint32_t MachOObjectFile::getIndirectSymbolTableEntry(
                                             const MachO::dysymtab_command &DLC,
                                             unsigned Index) const {
  uint64_t Offset = DLC.indirectsymoff + Index * sizeof(uint32_t);
  return getStruct<uint32_t>(*this, getPtr(*this, Offset));
}

MachO::data_in_code_entry
MachOObjectFile::getDataInCodeTableEntry(uint32_t DataOffset,
                                         unsigned Index) const {
  uint64_t Offset = DataOffset + Index * sizeof(MachO::data_in_code_entry);
  return getStruct<MachO::data_in_code_entry>(*this, getPtr(*this, Offset));
}

MachO::symtab_command MachOObjectFile::getSymtabLoadCommand() const {
  if (SymtabLoadCmd)
    return getStruct<MachO::symtab_command>(*this, SymtabLoadCmd);

  // If there is no SymtabLoadCmd return a load command with zero'ed fields.
  MachO::symtab_command Cmd;
  Cmd.cmd = MachO::LC_SYMTAB;
  Cmd.cmdsize = sizeof(MachO::symtab_command);
  Cmd.symoff = 0;
  Cmd.nsyms = 0;
  Cmd.stroff = 0;
  Cmd.strsize = 0;
  return Cmd;
}

MachO::dysymtab_command MachOObjectFile::getDysymtabLoadCommand() const {
  if (DysymtabLoadCmd)
    return getStruct<MachO::dysymtab_command>(*this, DysymtabLoadCmd);

  // If there is no DysymtabLoadCmd return a load command with zero'ed fields.
  MachO::dysymtab_command Cmd;
  Cmd.cmd = MachO::LC_DYSYMTAB;
  Cmd.cmdsize = sizeof(MachO::dysymtab_command);
  Cmd.ilocalsym = 0;
  Cmd.nlocalsym = 0;
  Cmd.iextdefsym = 0;
  Cmd.nextdefsym = 0;
  Cmd.iundefsym = 0;
  Cmd.nundefsym = 0;
  Cmd.tocoff = 0;
  Cmd.ntoc = 0;
  Cmd.modtaboff = 0;
  Cmd.nmodtab = 0;
  Cmd.extrefsymoff = 0;
  Cmd.nextrefsyms = 0;
  Cmd.indirectsymoff = 0;
  Cmd.nindirectsyms = 0;
  Cmd.extreloff = 0;
  Cmd.nextrel = 0;
  Cmd.locreloff = 0;
  Cmd.nlocrel = 0;
  return Cmd;
}

MachO::linkedit_data_command
MachOObjectFile::getDataInCodeLoadCommand() const {
  if (DataInCodeLoadCmd)
    return getStruct<MachO::linkedit_data_command>(*this, DataInCodeLoadCmd);

  // If there is no DataInCodeLoadCmd return a load command with zero'ed fields.
  MachO::linkedit_data_command Cmd;
  Cmd.cmd = MachO::LC_DATA_IN_CODE;
  Cmd.cmdsize = sizeof(MachO::linkedit_data_command);
  Cmd.dataoff = 0;
  Cmd.datasize = 0;
  return Cmd;
}

MachO::linkedit_data_command
MachOObjectFile::getLinkOptHintsLoadCommand() const {
  if (LinkOptHintsLoadCmd)
    return getStruct<MachO::linkedit_data_command>(*this, LinkOptHintsLoadCmd);

  // If there is no LinkOptHintsLoadCmd return a load command with zero'ed
  // fields.
  MachO::linkedit_data_command Cmd;
  Cmd.cmd = MachO::LC_LINKER_OPTIMIZATION_HINT;
  Cmd.cmdsize = sizeof(MachO::linkedit_data_command);
  Cmd.dataoff = 0;
  Cmd.datasize = 0;
  return Cmd;
}

ArrayRef<uint8_t> MachOObjectFile::getDyldInfoRebaseOpcodes() const {
  if (!DyldInfoLoadCmd)
    return None;

  MachO::dyld_info_command DyldInfo =
      getStruct<MachO::dyld_info_command>(*this, DyldInfoLoadCmd);
  const uint8_t *Ptr =
      reinterpret_cast<const uint8_t *>(getPtr(*this, DyldInfo.rebase_off));
  return makeArrayRef(Ptr, DyldInfo.rebase_size);
}

ArrayRef<uint8_t> MachOObjectFile::getDyldInfoBindOpcodes() const {
  if (!DyldInfoLoadCmd)
    return None;

  MachO::dyld_info_command DyldInfo =
      getStruct<MachO::dyld_info_command>(*this, DyldInfoLoadCmd);
  const uint8_t *Ptr =
      reinterpret_cast<const uint8_t *>(getPtr(*this, DyldInfo.bind_off));
  return makeArrayRef(Ptr, DyldInfo.bind_size);
}

ArrayRef<uint8_t> MachOObjectFile::getDyldInfoWeakBindOpcodes() const {
  if (!DyldInfoLoadCmd)
    return None;

  MachO::dyld_info_command DyldInfo =
      getStruct<MachO::dyld_info_command>(*this, DyldInfoLoadCmd);
  const uint8_t *Ptr =
      reinterpret_cast<const uint8_t *>(getPtr(*this, DyldInfo.weak_bind_off));
  return makeArrayRef(Ptr, DyldInfo.weak_bind_size);
}

ArrayRef<uint8_t> MachOObjectFile::getDyldInfoLazyBindOpcodes() const {
  if (!DyldInfoLoadCmd)
    return None;

  MachO::dyld_info_command DyldInfo =
      getStruct<MachO::dyld_info_command>(*this, DyldInfoLoadCmd);
  const uint8_t *Ptr =
      reinterpret_cast<const uint8_t *>(getPtr(*this, DyldInfo.lazy_bind_off));
  return makeArrayRef(Ptr, DyldInfo.lazy_bind_size);
}

ArrayRef<uint8_t> MachOObjectFile::getDyldInfoExportsTrie() const {
  if (!DyldInfoLoadCmd)
    return None;

  MachO::dyld_info_command DyldInfo =
      getStruct<MachO::dyld_info_command>(*this, DyldInfoLoadCmd);
  const uint8_t *Ptr =
      reinterpret_cast<const uint8_t *>(getPtr(*this, DyldInfo.export_off));
  return makeArrayRef(Ptr, DyldInfo.export_size);
}

ArrayRef<uint8_t> MachOObjectFile::getUuid() const {
  if (!UuidLoadCmd)
    return None;
  // Returning a pointer is fine as uuid doesn't need endian swapping.
  const char *Ptr = UuidLoadCmd + offsetof(MachO::uuid_command, uuid);
  return makeArrayRef(reinterpret_cast<const uint8_t *>(Ptr), 16);
}

StringRef MachOObjectFile::getStringTableData() const {
  MachO::symtab_command S = getSymtabLoadCommand();
  return getData().substr(S.stroff, S.strsize);
}

bool MachOObjectFile::is64Bit() const {
  return getType() == getMachOType(false, true) ||
    getType() == getMachOType(true, true);
}

void MachOObjectFile::ReadULEB128s(uint64_t Index,
                                   SmallVectorImpl<uint64_t> &Out) const {
  DataExtractor extractor(ObjectFile::getData(), true, 0);

  uint32_t offset = Index;
  uint64_t data = 0;
  while (uint64_t delta = extractor.getULEB128(&offset)) {
    data += delta;
    Out.push_back(data);
  }
}

bool MachOObjectFile::isRelocatableObject() const {
  return getHeader().filetype == MachO::MH_OBJECT;
}

Expected<std::unique_ptr<MachOObjectFile>>
ObjectFile::createMachOObjectFile(MemoryBufferRef Buffer,
                                  uint32_t UniversalCputype,
                                  uint32_t UniversalIndex) {
  StringRef Magic = Buffer.getBuffer().slice(0, 4);
  if (Magic == "\xFE\xED\xFA\xCE")
    return MachOObjectFile::create(Buffer, false, false,
                                   UniversalCputype, UniversalIndex);
  if (Magic == "\xCE\xFA\xED\xFE")
    return MachOObjectFile::create(Buffer, true, false,
                                   UniversalCputype, UniversalIndex);
  if (Magic == "\xFE\xED\xFA\xCF")
    return MachOObjectFile::create(Buffer, false, true,
                                   UniversalCputype, UniversalIndex);
  if (Magic == "\xCF\xFA\xED\xFE")
    return MachOObjectFile::create(Buffer, true, true,
                                   UniversalCputype, UniversalIndex);
  return make_error<GenericBinaryError>("Unrecognized MachO magic number",
                                        object_error::invalid_file_type);
}

StringRef MachOObjectFile::mapDebugSectionName(StringRef Name) const {
  return StringSwitch<StringRef>(Name)
      .Case("debug_str_offs", "debug_str_offsets")
      .Default(Name);
}
