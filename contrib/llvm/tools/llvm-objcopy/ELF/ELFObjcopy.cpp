//===- ELFObjcopy.cpp -----------------------------------------------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ELFObjcopy.h"
#include "Buffer.h"
#include "CopyConfig.h"
#include "Object.h"
#include "llvm-objcopy.h"

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/Error.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace llvm {
namespace objcopy {
namespace elf {

using namespace object;
using namespace ELF;
using SectionPred = std::function<bool(const SectionBase &Sec)>;

static bool isDebugSection(const SectionBase &Sec) {
  return StringRef(Sec.Name).startswith(".debug") ||
         StringRef(Sec.Name).startswith(".zdebug") || Sec.Name == ".gdb_index";
}

static bool isDWOSection(const SectionBase &Sec) {
  return StringRef(Sec.Name).endswith(".dwo");
}

static bool onlyKeepDWOPred(const Object &Obj, const SectionBase &Sec) {
  // We can't remove the section header string table.
  if (&Sec == Obj.SectionNames)
    return false;
  // Short of keeping the string table we want to keep everything that is a DWO
  // section and remove everything else.
  return !isDWOSection(Sec);
}

static ElfType getOutputElfType(const Binary &Bin) {
  // Infer output ELF type from the input ELF object
  if (isa<ELFObjectFile<ELF32LE>>(Bin))
    return ELFT_ELF32LE;
  if (isa<ELFObjectFile<ELF64LE>>(Bin))
    return ELFT_ELF64LE;
  if (isa<ELFObjectFile<ELF32BE>>(Bin))
    return ELFT_ELF32BE;
  if (isa<ELFObjectFile<ELF64BE>>(Bin))
    return ELFT_ELF64BE;
  llvm_unreachable("Invalid ELFType");
}

static ElfType getOutputElfType(const MachineInfo &MI) {
  // Infer output ELF type from the binary arch specified
  if (MI.Is64Bit)
    return MI.IsLittleEndian ? ELFT_ELF64LE : ELFT_ELF64BE;
  else
    return MI.IsLittleEndian ? ELFT_ELF32LE : ELFT_ELF32BE;
}

static std::unique_ptr<Writer> createWriter(const CopyConfig &Config,
                                            Object &Obj, Buffer &Buf,
                                            ElfType OutputElfType) {
  if (Config.OutputFormat == "binary") {
    return llvm::make_unique<BinaryWriter>(Obj, Buf);
  }
  // Depending on the initial ELFT and OutputFormat we need a different Writer.
  switch (OutputElfType) {
  case ELFT_ELF32LE:
    return llvm::make_unique<ELFWriter<ELF32LE>>(Obj, Buf,
                                                 !Config.StripSections);
  case ELFT_ELF64LE:
    return llvm::make_unique<ELFWriter<ELF64LE>>(Obj, Buf,
                                                 !Config.StripSections);
  case ELFT_ELF32BE:
    return llvm::make_unique<ELFWriter<ELF32BE>>(Obj, Buf,
                                                 !Config.StripSections);
  case ELFT_ELF64BE:
    return llvm::make_unique<ELFWriter<ELF64BE>>(Obj, Buf,
                                                 !Config.StripSections);
  }
  llvm_unreachable("Invalid output format");
}

template <class ELFT>
static Expected<ArrayRef<uint8_t>>
findBuildID(const object::ELFFile<ELFT> &In) {
  for (const auto &Phdr : unwrapOrError(In.program_headers())) {
    if (Phdr.p_type != PT_NOTE)
      continue;
    Error Err = Error::success();
    for (const auto &Note : In.notes(Phdr, Err))
      if (Note.getType() == NT_GNU_BUILD_ID && Note.getName() == ELF_NOTE_GNU)
        return Note.getDesc();
    if (Err)
      return std::move(Err);
  }
  return createStringError(llvm::errc::invalid_argument,
                           "Could not find build ID.");
}

static Expected<ArrayRef<uint8_t>>
findBuildID(const object::ELFObjectFileBase &In) {
  if (auto *O = dyn_cast<ELFObjectFile<ELF32LE>>(&In))
    return findBuildID(*O->getELFFile());
  else if (auto *O = dyn_cast<ELFObjectFile<ELF64LE>>(&In))
    return findBuildID(*O->getELFFile());
  else if (auto *O = dyn_cast<ELFObjectFile<ELF32BE>>(&In))
    return findBuildID(*O->getELFFile());
  else if (auto *O = dyn_cast<ELFObjectFile<ELF64BE>>(&In))
    return findBuildID(*O->getELFFile());

  llvm_unreachable("Bad file format");
}

static void linkToBuildIdDir(const CopyConfig &Config, StringRef ToLink,
                             StringRef Suffix, ArrayRef<uint8_t> BuildIdBytes) {
  SmallString<128> Path = Config.BuildIdLinkDir;
  sys::path::append(Path, llvm::toHex(BuildIdBytes[0], /*LowerCase*/ true));
  if (auto EC = sys::fs::create_directories(Path))
    error("cannot create build ID link directory " + Path + ": " +
          EC.message());

  sys::path::append(Path,
                    llvm::toHex(BuildIdBytes.slice(1), /*LowerCase*/ true));
  Path += Suffix;
  if (auto EC = sys::fs::create_hard_link(ToLink, Path)) {
    // Hard linking failed, try to remove the file first if it exists.
    if (sys::fs::exists(Path))
      sys::fs::remove(Path);
    EC = sys::fs::create_hard_link(ToLink, Path);
    if (EC)
      error("cannot link " + ToLink + " to " + Path + ": " + EC.message());
  }
}

static void splitDWOToFile(const CopyConfig &Config, const Reader &Reader,
                           StringRef File, ElfType OutputElfType) {
  auto DWOFile = Reader.create();
  DWOFile->removeSections(
      [&](const SectionBase &Sec) { return onlyKeepDWOPred(*DWOFile, Sec); });
  if (Config.OutputArch)
    DWOFile->Machine = Config.OutputArch.getValue().EMachine;
  FileBuffer FB(File);
  auto Writer = createWriter(Config, *DWOFile, FB, OutputElfType);
  Writer->finalize();
  Writer->write();
}

static Error dumpSectionToFile(StringRef SecName, StringRef Filename,
                               Object &Obj) {
  for (auto &Sec : Obj.sections()) {
    if (Sec.Name == SecName) {
      if (Sec.OriginalData.empty())
        return make_error<StringError>("Can't dump section \"" + SecName +
                                           "\": it has no contents",
                                       object_error::parse_failed);
      Expected<std::unique_ptr<FileOutputBuffer>> BufferOrErr =
          FileOutputBuffer::create(Filename, Sec.OriginalData.size());
      if (!BufferOrErr)
        return BufferOrErr.takeError();
      std::unique_ptr<FileOutputBuffer> Buf = std::move(*BufferOrErr);
      std::copy(Sec.OriginalData.begin(), Sec.OriginalData.end(),
                Buf->getBufferStart());
      if (Error E = Buf->commit())
        return E;
      return Error::success();
    }
  }
  return make_error<StringError>("Section not found",
                                 object_error::parse_failed);
}

static bool isCompressed(const SectionBase &Section) {
  const char *Magic = "ZLIB";
  return StringRef(Section.Name).startswith(".zdebug") ||
         (Section.OriginalData.size() > strlen(Magic) &&
          !strncmp(reinterpret_cast<const char *>(Section.OriginalData.data()),
                   Magic, strlen(Magic))) ||
         (Section.Flags & ELF::SHF_COMPRESSED);
}

static bool isCompressable(const SectionBase &Section) {
  return !isCompressed(Section) && isDebugSection(Section) &&
         Section.Name != ".gdb_index";
}

static void replaceDebugSections(
    const CopyConfig &Config, Object &Obj, SectionPred &RemovePred,
    function_ref<bool(const SectionBase &)> shouldReplace,
    function_ref<SectionBase *(const SectionBase *)> addSection) {
  SmallVector<SectionBase *, 13> ToReplace;
  SmallVector<RelocationSection *, 13> RelocationSections;
  for (auto &Sec : Obj.sections()) {
    if (RelocationSection *R = dyn_cast<RelocationSection>(&Sec)) {
      if (shouldReplace(*R->getSection()))
        RelocationSections.push_back(R);
      continue;
    }

    if (shouldReplace(Sec))
      ToReplace.push_back(&Sec);
  }

  for (SectionBase *S : ToReplace) {
    SectionBase *NewSection = addSection(S);

    for (RelocationSection *RS : RelocationSections) {
      if (RS->getSection() == S)
        RS->setSection(NewSection);
    }
  }

  RemovePred = [shouldReplace, RemovePred](const SectionBase &Sec) {
    return shouldReplace(Sec) || RemovePred(Sec);
  };
}

// This function handles the high level operations of GNU objcopy including
// handling command line options. It's important to outline certain properties
// we expect to hold of the command line operations. Any operation that "keeps"
// should keep regardless of a remove. Additionally any removal should respect
// any previous removals. Lastly whether or not something is removed shouldn't
// depend a) on the order the options occur in or b) on some opaque priority
// system. The only priority is that keeps/copies overrule removes.
static void handleArgs(const CopyConfig &Config, Object &Obj,
                       const Reader &Reader, ElfType OutputElfType) {

  if (!Config.SplitDWO.empty()) {
    splitDWOToFile(Config, Reader, Config.SplitDWO, OutputElfType);
  }
  if (Config.OutputArch)
    Obj.Machine = Config.OutputArch.getValue().EMachine;

  // TODO: update or remove symbols only if there is an option that affects
  // them.
  if (Obj.SymbolTable) {
    Obj.SymbolTable->updateSymbols([&](Symbol &Sym) {
      if (!Sym.isCommon() &&
          ((Config.LocalizeHidden &&
            (Sym.Visibility == STV_HIDDEN || Sym.Visibility == STV_INTERNAL)) ||
           is_contained(Config.SymbolsToLocalize, Sym.Name)))
        Sym.Binding = STB_LOCAL;

      // Note: these two globalize flags have very similar names but different
      // meanings:
      //
      // --globalize-symbol: promote a symbol to global
      // --keep-global-symbol: all symbols except for these should be made local
      //
      // If --globalize-symbol is specified for a given symbol, it will be
      // global in the output file even if it is not included via
      // --keep-global-symbol. Because of that, make sure to check
      // --globalize-symbol second.
      if (!Config.SymbolsToKeepGlobal.empty() &&
          !is_contained(Config.SymbolsToKeepGlobal, Sym.Name) &&
          Sym.getShndx() != SHN_UNDEF)
        Sym.Binding = STB_LOCAL;

      if (is_contained(Config.SymbolsToGlobalize, Sym.Name) &&
          Sym.getShndx() != SHN_UNDEF)
        Sym.Binding = STB_GLOBAL;

      if (is_contained(Config.SymbolsToWeaken, Sym.Name) &&
          Sym.Binding == STB_GLOBAL)
        Sym.Binding = STB_WEAK;

      if (Config.Weaken && Sym.Binding == STB_GLOBAL &&
          Sym.getShndx() != SHN_UNDEF)
        Sym.Binding = STB_WEAK;

      const auto I = Config.SymbolsToRename.find(Sym.Name);
      if (I != Config.SymbolsToRename.end())
        Sym.Name = I->getValue();

      if (!Config.SymbolsPrefix.empty() && Sym.Type != STT_SECTION)
        Sym.Name = (Config.SymbolsPrefix + Sym.Name).str();
    });

    // The purpose of this loop is to mark symbols referenced by sections
    // (like GroupSection or RelocationSection). This way, we know which
    // symbols are still 'needed' and which are not.
    if (Config.StripUnneeded) {
      for (auto &Section : Obj.sections())
        Section.markSymbols();
    }

    Obj.removeSymbols([&](const Symbol &Sym) {
      if (is_contained(Config.SymbolsToKeep, Sym.Name) ||
          (Config.KeepFileSymbols && Sym.Type == STT_FILE))
        return false;

      if (Config.DiscardAll && Sym.Binding == STB_LOCAL &&
          Sym.getShndx() != SHN_UNDEF && Sym.Type != STT_FILE &&
          Sym.Type != STT_SECTION)
        return true;

      if (Config.StripAll || Config.StripAllGNU)
        return true;

      if (is_contained(Config.SymbolsToRemove, Sym.Name))
        return true;

      if (Config.StripUnneeded && !Sym.Referenced &&
          (Sym.Binding == STB_LOCAL || Sym.getShndx() == SHN_UNDEF) &&
          Sym.Type != STT_FILE && Sym.Type != STT_SECTION)
        return true;

      return false;
    });
  }

  SectionPred RemovePred = [](const SectionBase &) { return false; };

  // Removes:
  if (!Config.ToRemove.empty()) {
    RemovePred = [&Config](const SectionBase &Sec) {
      return is_contained(Config.ToRemove, Sec.Name);
    };
  }

  if (Config.StripDWO || !Config.SplitDWO.empty())
    RemovePred = [RemovePred](const SectionBase &Sec) {
      return isDWOSection(Sec) || RemovePred(Sec);
    };

  if (Config.ExtractDWO)
    RemovePred = [RemovePred, &Obj](const SectionBase &Sec) {
      return onlyKeepDWOPred(Obj, Sec) || RemovePred(Sec);
    };

  if (Config.StripAllGNU)
    RemovePred = [RemovePred, &Obj](const SectionBase &Sec) {
      if (RemovePred(Sec))
        return true;
      if ((Sec.Flags & SHF_ALLOC) != 0)
        return false;
      if (&Sec == Obj.SectionNames)
        return false;
      switch (Sec.Type) {
      case SHT_SYMTAB:
      case SHT_REL:
      case SHT_RELA:
      case SHT_STRTAB:
        return true;
      }
      return isDebugSection(Sec);
    };

  if (Config.StripSections) {
    RemovePred = [RemovePred](const SectionBase &Sec) {
      return RemovePred(Sec) || (Sec.Flags & SHF_ALLOC) == 0;
    };
  }

  if (Config.StripDebug) {
    RemovePred = [RemovePred](const SectionBase &Sec) {
      return RemovePred(Sec) || isDebugSection(Sec);
    };
  }

  if (Config.StripNonAlloc)
    RemovePred = [RemovePred, &Obj](const SectionBase &Sec) {
      if (RemovePred(Sec))
        return true;
      if (&Sec == Obj.SectionNames)
        return false;
      return (Sec.Flags & SHF_ALLOC) == 0;
    };

  if (Config.StripAll)
    RemovePred = [RemovePred, &Obj](const SectionBase &Sec) {
      if (RemovePred(Sec))
        return true;
      if (&Sec == Obj.SectionNames)
        return false;
      if (StringRef(Sec.Name).startswith(".gnu.warning"))
        return false;
      return (Sec.Flags & SHF_ALLOC) == 0;
    };

  // Explicit copies:
  if (!Config.OnlySection.empty()) {
    RemovePred = [&Config, RemovePred, &Obj](const SectionBase &Sec) {
      // Explicitly keep these sections regardless of previous removes.
      if (is_contained(Config.OnlySection, Sec.Name))
        return false;

      // Allow all implicit removes.
      if (RemovePred(Sec))
        return true;

      // Keep special sections.
      if (Obj.SectionNames == &Sec)
        return false;
      if (Obj.SymbolTable == &Sec ||
          (Obj.SymbolTable && Obj.SymbolTable->getStrTab() == &Sec))
        return false;

      // Remove everything else.
      return true;
    };
  }

  if (!Config.KeepSection.empty()) {
    RemovePred = [&Config, RemovePred](const SectionBase &Sec) {
      // Explicitly keep these sections regardless of previous removes.
      if (is_contained(Config.KeepSection, Sec.Name))
        return false;
      // Otherwise defer to RemovePred.
      return RemovePred(Sec);
    };
  }

  // This has to be the last predicate assignment.
  // If the option --keep-symbol has been specified
  // and at least one of those symbols is present
  // (equivalently, the updated symbol table is not empty)
  // the symbol table and the string table should not be removed.
  if ((!Config.SymbolsToKeep.empty() || Config.KeepFileSymbols) &&
      Obj.SymbolTable && !Obj.SymbolTable->empty()) {
    RemovePred = [&Obj, RemovePred](const SectionBase &Sec) {
      if (&Sec == Obj.SymbolTable || &Sec == Obj.SymbolTable->getStrTab())
        return false;
      return RemovePred(Sec);
    };
  }

  if (Config.CompressionType != DebugCompressionType::None)
    replaceDebugSections(Config, Obj, RemovePred, isCompressable,
                         [&Config, &Obj](const SectionBase *S) {
                           return &Obj.addSection<CompressedSection>(
                               *S, Config.CompressionType);
                         });
  else if (Config.DecompressDebugSections)
    replaceDebugSections(
        Config, Obj, RemovePred,
        [](const SectionBase &S) { return isa<CompressedSection>(&S); },
        [&Obj](const SectionBase *S) {
          auto CS = cast<CompressedSection>(S);
          return &Obj.addSection<DecompressedSection>(*CS);
        });

  Obj.removeSections(RemovePred);

  if (!Config.SectionsToRename.empty()) {
    for (auto &Sec : Obj.sections()) {
      const auto Iter = Config.SectionsToRename.find(Sec.Name);
      if (Iter != Config.SectionsToRename.end()) {
        const SectionRename &SR = Iter->second;
        Sec.Name = SR.NewName;
        if (SR.NewFlags.hasValue()) {
          // Preserve some flags which should not be dropped when setting flags.
          // Also, preserve anything OS/processor dependant.
          const uint64_t PreserveMask = ELF::SHF_COMPRESSED | ELF::SHF_EXCLUDE |
                                        ELF::SHF_GROUP | ELF::SHF_LINK_ORDER |
                                        ELF::SHF_MASKOS | ELF::SHF_MASKPROC |
                                        ELF::SHF_TLS | ELF::SHF_INFO_LINK;
          Sec.Flags = (Sec.Flags & PreserveMask) |
                      (SR.NewFlags.getValue() & ~PreserveMask);
        }
      }
    }
  }

  if (!Config.AddSection.empty()) {
    for (const auto &Flag : Config.AddSection) {
      std::pair<StringRef, StringRef> SecPair = Flag.split("=");
      StringRef SecName = SecPair.first;
      StringRef File = SecPair.second;
      ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
          MemoryBuffer::getFile(File);
      if (!BufOrErr)
        reportError(File, BufOrErr.getError());
      std::unique_ptr<MemoryBuffer> Buf = std::move(*BufOrErr);
      ArrayRef<uint8_t> Data(
          reinterpret_cast<const uint8_t *>(Buf->getBufferStart()),
          Buf->getBufferSize());
      OwnedDataSection &NewSection =
          Obj.addSection<OwnedDataSection>(SecName, Data);
      if (SecName.startswith(".note") && SecName != ".note.GNU-stack")
        NewSection.Type = SHT_NOTE;
    }
  }

  if (!Config.DumpSection.empty()) {
    for (const auto &Flag : Config.DumpSection) {
      std::pair<StringRef, StringRef> SecPair = Flag.split("=");
      StringRef SecName = SecPair.first;
      StringRef File = SecPair.second;
      if (Error E = dumpSectionToFile(SecName, File, Obj))
        reportError(Config.InputFilename, std::move(E));
    }
  }

  if (!Config.AddGnuDebugLink.empty())
    Obj.addSection<GnuDebugLinkSection>(Config.AddGnuDebugLink);
}

void executeObjcopyOnRawBinary(const CopyConfig &Config, MemoryBuffer &In,
                               Buffer &Out) {
  BinaryReader Reader(Config.BinaryArch, &In);
  std::unique_ptr<Object> Obj = Reader.create();

  // Prefer OutputArch (-O<format>) if set, otherwise fallback to BinaryArch
  // (-B<arch>).
  const ElfType OutputElfType = getOutputElfType(
      Config.OutputArch ? Config.OutputArch.getValue() : Config.BinaryArch);
  handleArgs(Config, *Obj, Reader, OutputElfType);
  std::unique_ptr<Writer> Writer =
      createWriter(Config, *Obj, Out, OutputElfType);
  Writer->finalize();
  Writer->write();
}

void executeObjcopyOnBinary(const CopyConfig &Config,
                            object::ELFObjectFileBase &In, Buffer &Out) {
  ELFReader Reader(&In);
  std::unique_ptr<Object> Obj = Reader.create();
  // Prefer OutputArch (-O<format>) if set, otherwise infer it from the input.
  const ElfType OutputElfType =
      Config.OutputArch ? getOutputElfType(Config.OutputArch.getValue())
                        : getOutputElfType(In);
  ArrayRef<uint8_t> BuildIdBytes;

  if (!Config.BuildIdLinkDir.empty()) {
    BuildIdBytes = unwrapOrError(findBuildID(In));
    if (BuildIdBytes.size() < 2)
      error("build ID in file '" + Config.InputFilename +
            "' is smaller than two bytes");
  }

  if (!Config.BuildIdLinkDir.empty() && Config.BuildIdLinkInput) {
    linkToBuildIdDir(Config, Config.InputFilename,
                     Config.BuildIdLinkInput.getValue(), BuildIdBytes);
  }
  handleArgs(Config, *Obj, Reader, OutputElfType);
  std::unique_ptr<Writer> Writer =
      createWriter(Config, *Obj, Out, OutputElfType);
  Writer->finalize();
  Writer->write();
  if (!Config.BuildIdLinkDir.empty() && Config.BuildIdLinkOutput) {
    linkToBuildIdDir(Config, Config.OutputFilename,
                     Config.BuildIdLinkOutput.getValue(), BuildIdBytes);
  }
}

} // end namespace elf
} // end namespace objcopy
} // end namespace llvm
