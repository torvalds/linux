//===- InputFile.cpp ------------------------------------------ *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "InputFile.h"

#include "FormatUtil.h"
#include "LinePrinter.h"

#include "llvm/BinaryFormat/Magic.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTable.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::object;
using namespace llvm::pdb;

InputFile::InputFile() {}
InputFile::~InputFile() {}

static Expected<ModuleDebugStreamRef>
getModuleDebugStream(PDBFile &File, StringRef &ModuleName, uint32_t Index) {
  ExitOnError Err("Unexpected error: ");

  auto &Dbi = Err(File.getPDBDbiStream());
  const auto &Modules = Dbi.modules();
  if (Index >= Modules.getModuleCount())
    return make_error<RawError>(raw_error_code::index_out_of_bounds,
                                "Invalid module index");

  auto Modi = Modules.getModuleDescriptor(Index);

  ModuleName = Modi.getModuleName();

  uint16_t ModiStream = Modi.getModuleStreamIndex();
  if (ModiStream == kInvalidStreamIndex)
    return make_error<RawError>(raw_error_code::no_stream,
                                "Module stream not present");

  auto ModStreamData = File.createIndexedStream(ModiStream);

  ModuleDebugStreamRef ModS(Modi, std::move(ModStreamData));
  if (auto EC = ModS.reload())
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "Invalid module stream");

  return std::move(ModS);
}

static inline bool isCodeViewDebugSubsection(object::SectionRef Section,
                                             StringRef Name,
                                             BinaryStreamReader &Reader) {
  StringRef SectionName, Contents;
  if (Section.getName(SectionName))
    return false;

  if (SectionName != Name)
    return false;

  if (Section.getContents(Contents))
    return false;

  Reader = BinaryStreamReader(Contents, support::little);
  uint32_t Magic;
  if (Reader.bytesRemaining() < sizeof(uint32_t))
    return false;
  cantFail(Reader.readInteger(Magic));
  if (Magic != COFF::DEBUG_SECTION_MAGIC)
    return false;
  return true;
}

static inline bool isDebugSSection(object::SectionRef Section,
                                   DebugSubsectionArray &Subsections) {
  BinaryStreamReader Reader;
  if (!isCodeViewDebugSubsection(Section, ".debug$S", Reader))
    return false;

  cantFail(Reader.readArray(Subsections, Reader.bytesRemaining()));
  return true;
}

static bool isDebugTSection(SectionRef Section, CVTypeArray &Types) {
  BinaryStreamReader Reader;
  if (!isCodeViewDebugSubsection(Section, ".debug$T", Reader) &&
      !isCodeViewDebugSubsection(Section, ".debug$P", Reader))
    return false;
  cantFail(Reader.readArray(Types, Reader.bytesRemaining()));
  return true;
}

static std::string formatChecksumKind(FileChecksumKind Kind) {
  switch (Kind) {
    RETURN_CASE(FileChecksumKind, None, "None");
    RETURN_CASE(FileChecksumKind, MD5, "MD5");
    RETURN_CASE(FileChecksumKind, SHA1, "SHA-1");
    RETURN_CASE(FileChecksumKind, SHA256, "SHA-256");
  }
  return formatUnknownEnum(Kind);
}

template <typename... Args>
static void formatInternal(LinePrinter &Printer, bool Append, Args &&... args) {
  if (Append)
    Printer.format(std::forward<Args>(args)...);
  else
    Printer.formatLine(std::forward<Args>(args)...);
}

SymbolGroup::SymbolGroup(InputFile *File, uint32_t GroupIndex) : File(File) {
  if (!File)
    return;

  if (File->isPdb())
    initializeForPdb(GroupIndex);
  else {
    Name = ".debug$S";
    uint32_t I = 0;
    for (const auto &S : File->obj().sections()) {
      DebugSubsectionArray SS;
      if (!isDebugSSection(S, SS))
        continue;

      if (!SC.hasChecksums() || !SC.hasStrings())
        SC.initialize(SS);

      if (I == GroupIndex)
        Subsections = SS;

      if (SC.hasChecksums() && SC.hasStrings())
        break;
    }
    rebuildChecksumMap();
  }
}

StringRef SymbolGroup::name() const { return Name; }

void SymbolGroup::updateDebugS(const codeview::DebugSubsectionArray &SS) {
  Subsections = SS;
}

void SymbolGroup::updatePdbModi(uint32_t Modi) { initializeForPdb(Modi); }

void SymbolGroup::initializeForPdb(uint32_t Modi) {
  assert(File && File->isPdb());

  // PDB always uses the same string table, but each module has its own
  // checksums.  So we only set the strings if they're not already set.
  if (!SC.hasStrings()) {
    auto StringTable = File->pdb().getStringTable();
    if (StringTable)
      SC.setStrings(StringTable->getStringTable());
    else
      consumeError(StringTable.takeError());
  }

  SC.resetChecksums();
  auto MDS = getModuleDebugStream(File->pdb(), Name, Modi);
  if (!MDS) {
    consumeError(MDS.takeError());
    return;
  }

  DebugStream = std::make_shared<ModuleDebugStreamRef>(std::move(*MDS));
  Subsections = DebugStream->getSubsectionsArray();
  SC.initialize(Subsections);
  rebuildChecksumMap();
}

void SymbolGroup::rebuildChecksumMap() {
  if (!SC.hasChecksums())
    return;

  for (const auto &Entry : SC.checksums()) {
    auto S = SC.strings().getString(Entry.FileNameOffset);
    if (!S)
      continue;
    ChecksumsByFile[*S] = Entry;
  }
}

const ModuleDebugStreamRef &SymbolGroup::getPdbModuleStream() const {
  assert(File && File->isPdb() && DebugStream);
  return *DebugStream;
}

Expected<StringRef> SymbolGroup::getNameFromStringTable(uint32_t Offset) const {
  return SC.strings().getString(Offset);
}

void SymbolGroup::formatFromFileName(LinePrinter &Printer, StringRef File,
                                     bool Append) const {
  auto FC = ChecksumsByFile.find(File);
  if (FC == ChecksumsByFile.end()) {
    formatInternal(Printer, Append, "- (no checksum) {0}", File);
    return;
  }

  formatInternal(Printer, Append, "- ({0}: {1}) {2}",
                 formatChecksumKind(FC->getValue().Kind),
                 toHex(FC->getValue().Checksum), File);
}

void SymbolGroup::formatFromChecksumsOffset(LinePrinter &Printer,
                                            uint32_t Offset,
                                            bool Append) const {
  if (!SC.hasChecksums()) {
    formatInternal(Printer, Append, "(unknown file name offset {0})", Offset);
    return;
  }

  auto Iter = SC.checksums().getArray().at(Offset);
  if (Iter == SC.checksums().getArray().end()) {
    formatInternal(Printer, Append, "(unknown file name offset {0})", Offset);
    return;
  }

  uint32_t FO = Iter->FileNameOffset;
  auto ExpectedFile = getNameFromStringTable(FO);
  if (!ExpectedFile) {
    formatInternal(Printer, Append, "(unknown file name offset {0})", Offset);
    consumeError(ExpectedFile.takeError());
    return;
  }
  if (Iter->Kind == FileChecksumKind::None) {
    formatInternal(Printer, Append, "{0} (no checksum)", *ExpectedFile);
  } else {
    formatInternal(Printer, Append, "{0} ({1}: {2})", *ExpectedFile,
                   formatChecksumKind(Iter->Kind), toHex(Iter->Checksum));
  }
}

Expected<InputFile> InputFile::open(StringRef Path, bool AllowUnknownFile) {
  InputFile IF;
  if (!llvm::sys::fs::exists(Path))
    return make_error<StringError>(formatv("File {0} not found", Path),
                                   inconvertibleErrorCode());

  file_magic Magic;
  if (auto EC = identify_magic(Path, Magic))
    return make_error<StringError>(
        formatv("Unable to identify file type for file {0}", Path), EC);

  if (Magic == file_magic::coff_object) {
    Expected<OwningBinary<Binary>> BinaryOrErr = createBinary(Path);
    if (!BinaryOrErr)
      return BinaryOrErr.takeError();

    IF.CoffObject = std::move(*BinaryOrErr);
    IF.PdbOrObj = llvm::cast<COFFObjectFile>(IF.CoffObject.getBinary());
    return std::move(IF);
  }

  if (Magic == file_magic::pdb) {
    std::unique_ptr<IPDBSession> Session;
    if (auto Err = loadDataForPDB(PDB_ReaderType::Native, Path, Session))
      return std::move(Err);

    IF.PdbSession.reset(static_cast<NativeSession *>(Session.release()));
    IF.PdbOrObj = &IF.PdbSession->getPDBFile();

    return std::move(IF);
  }

  if (!AllowUnknownFile)
    return make_error<StringError>(
        formatv("File {0} is not a supported file type", Path),
        inconvertibleErrorCode());

  auto Result = MemoryBuffer::getFile(Path, -1LL, false);
  if (!Result)
    return make_error<StringError>(
        formatv("File {0} could not be opened", Path), Result.getError());

  IF.UnknownFile = std::move(*Result);
  IF.PdbOrObj = IF.UnknownFile.get();
  return std::move(IF);
}

PDBFile &InputFile::pdb() {
  assert(isPdb());
  return *PdbOrObj.get<PDBFile *>();
}

const PDBFile &InputFile::pdb() const {
  assert(isPdb());
  return *PdbOrObj.get<PDBFile *>();
}

object::COFFObjectFile &InputFile::obj() {
  assert(isObj());
  return *PdbOrObj.get<object::COFFObjectFile *>();
}

const object::COFFObjectFile &InputFile::obj() const {
  assert(isObj());
  return *PdbOrObj.get<object::COFFObjectFile *>();
}

MemoryBuffer &InputFile::unknown() {
  assert(isUnknown());
  return *PdbOrObj.get<MemoryBuffer *>();
}

const MemoryBuffer &InputFile::unknown() const {
  assert(isUnknown());
  return *PdbOrObj.get<MemoryBuffer *>();
}

StringRef InputFile::getFilePath() const {
  if (isPdb())
    return pdb().getFilePath();
  if (isObj())
    return obj().getFileName();
  assert(isUnknown());
  return unknown().getBufferIdentifier();
}

bool InputFile::hasTypes() const {
  if (isPdb())
    return pdb().hasPDBTpiStream();

  for (const auto &Section : obj().sections()) {
    CVTypeArray Types;
    if (isDebugTSection(Section, Types))
      return true;
  }
  return false;
}

bool InputFile::hasIds() const {
  if (isObj())
    return false;
  return pdb().hasPDBIpiStream();
}

bool InputFile::isPdb() const { return PdbOrObj.is<PDBFile *>(); }

bool InputFile::isObj() const {
  return PdbOrObj.is<object::COFFObjectFile *>();
}

bool InputFile::isUnknown() const { return PdbOrObj.is<MemoryBuffer *>(); }

codeview::LazyRandomTypeCollection &
InputFile::getOrCreateTypeCollection(TypeCollectionKind Kind) {
  if (Types && Kind == kTypes)
    return *Types;
  if (Ids && Kind == kIds)
    return *Ids;

  if (Kind == kIds) {
    assert(isPdb() && pdb().hasPDBIpiStream());
  }

  // If the collection was already initialized, we should have just returned it
  // in step 1.
  if (isPdb()) {
    TypeCollectionPtr &Collection = (Kind == kIds) ? Ids : Types;
    auto &Stream = cantFail((Kind == kIds) ? pdb().getPDBIpiStream()
                                           : pdb().getPDBTpiStream());

    auto &Array = Stream.typeArray();
    uint32_t Count = Stream.getNumTypeRecords();
    auto Offsets = Stream.getTypeIndexOffsets();
    Collection =
        llvm::make_unique<LazyRandomTypeCollection>(Array, Count, Offsets);
    return *Collection;
  }

  assert(isObj());
  assert(Kind == kTypes);
  assert(!Types);

  for (const auto &Section : obj().sections()) {
    CVTypeArray Records;
    if (!isDebugTSection(Section, Records))
      continue;

    Types = llvm::make_unique<LazyRandomTypeCollection>(Records, 100);
    return *Types;
  }

  Types = llvm::make_unique<LazyRandomTypeCollection>(100);
  return *Types;
}

codeview::LazyRandomTypeCollection &InputFile::types() {
  return getOrCreateTypeCollection(kTypes);
}

codeview::LazyRandomTypeCollection &InputFile::ids() {
  // Object files have only one type stream that contains both types and ids.
  // Similarly, some PDBs don't contain an IPI stream, and for those both types
  // and IDs are in the same stream.
  if (isObj() || !pdb().hasPDBIpiStream())
    return types();

  return getOrCreateTypeCollection(kIds);
}

iterator_range<SymbolGroupIterator> InputFile::symbol_groups() {
  return make_range<SymbolGroupIterator>(symbol_groups_begin(),
                                         symbol_groups_end());
}

SymbolGroupIterator InputFile::symbol_groups_begin() {
  return SymbolGroupIterator(*this);
}

SymbolGroupIterator InputFile::symbol_groups_end() {
  return SymbolGroupIterator();
}

SymbolGroupIterator::SymbolGroupIterator() : Value(nullptr) {}

SymbolGroupIterator::SymbolGroupIterator(InputFile &File) : Value(&File) {
  if (File.isObj()) {
    SectionIter = File.obj().section_begin();
    scanToNextDebugS();
  }
}

bool SymbolGroupIterator::operator==(const SymbolGroupIterator &R) const {
  bool E = isEnd();
  bool RE = R.isEnd();
  if (E || RE)
    return E == RE;

  if (Value.File != R.Value.File)
    return false;
  return Index == R.Index;
}

const SymbolGroup &SymbolGroupIterator::operator*() const {
  assert(!isEnd());
  return Value;
}
SymbolGroup &SymbolGroupIterator::operator*() {
  assert(!isEnd());
  return Value;
}

SymbolGroupIterator &SymbolGroupIterator::operator++() {
  assert(Value.File && !isEnd());
  ++Index;
  if (isEnd())
    return *this;

  if (Value.File->isPdb()) {
    Value.updatePdbModi(Index);
    return *this;
  }

  scanToNextDebugS();
  return *this;
}

void SymbolGroupIterator::scanToNextDebugS() {
  assert(SectionIter.hasValue());
  auto End = Value.File->obj().section_end();
  auto &Iter = *SectionIter;
  assert(!isEnd());

  while (++Iter != End) {
    DebugSubsectionArray SS;
    SectionRef SR = *Iter;
    if (!isDebugSSection(SR, SS))
      continue;

    Value.updateDebugS(SS);
    return;
  }
}

bool SymbolGroupIterator::isEnd() const {
  if (!Value.File)
    return true;
  if (Value.File->isPdb()) {
    auto &Dbi = cantFail(Value.File->pdb().getPDBDbiStream());
    uint32_t Count = Dbi.modules().getModuleCount();
    assert(Index <= Count);
    return Index == Count;
  }

  assert(SectionIter.hasValue());
  return *SectionIter == Value.File->obj().section_end();
}
