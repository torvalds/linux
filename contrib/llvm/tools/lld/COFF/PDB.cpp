//===- PDB.cpp ------------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PDB.h"
#include "Chunks.h"
#include "Config.h"
#include "Driver.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Writer.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Timer.h"
#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/CodeView/GlobalTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/MergingTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolRecordHelpers.h"
#include "llvm/DebugInfo/CodeView/SymbolSerializer.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeDumpVisitor.h"
#include "llvm/DebugInfo/CodeView/TypeIndexDiscovery.h"
#include "llvm/DebugInfo/CodeView/TypeStreamMerger.h"
#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/PDB/GenericError.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptorBuilder.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/GSIStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PDBFileBuilder.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTableBuilder.h"
#include "llvm/DebugInfo/PDB/Native/TpiHashing.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStreamBuilder.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/CVDebugRecord.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JamCRC.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"
#include <memory>

using namespace lld;
using namespace lld::coff;
using namespace llvm;
using namespace llvm::codeview;

using llvm::object::coff_section;

static ExitOnError ExitOnErr;

static Timer TotalPdbLinkTimer("PDB Emission (Cumulative)", Timer::root());

static Timer AddObjectsTimer("Add Objects", TotalPdbLinkTimer);
static Timer TypeMergingTimer("Type Merging", AddObjectsTimer);
static Timer SymbolMergingTimer("Symbol Merging", AddObjectsTimer);
static Timer GlobalsLayoutTimer("Globals Stream Layout", TotalPdbLinkTimer);
static Timer TpiStreamLayoutTimer("TPI Stream Layout", TotalPdbLinkTimer);
static Timer DiskCommitTimer("Commit to Disk", TotalPdbLinkTimer);

namespace {
/// Map from type index and item index in a type server PDB to the
/// corresponding index in the destination PDB.
struct CVIndexMap {
  SmallVector<TypeIndex, 0> TPIMap;
  SmallVector<TypeIndex, 0> IPIMap;
  bool IsTypeServerMap = false;
  bool IsPrecompiledTypeMap = false;
};

class DebugSHandler;

class PDBLinker {
  friend DebugSHandler;

public:
  PDBLinker(SymbolTable *Symtab)
      : Alloc(), Symtab(Symtab), Builder(Alloc), TypeTable(Alloc),
        IDTable(Alloc), GlobalTypeTable(Alloc), GlobalIDTable(Alloc) {
    // This isn't strictly necessary, but link.exe usually puts an empty string
    // as the first "valid" string in the string table, so we do the same in
    // order to maintain as much byte-for-byte compatibility as possible.
    PDBStrTab.insert("");
  }

  /// Emit the basic PDB structure: initial streams, headers, etc.
  void initialize(llvm::codeview::DebugInfo *BuildId);

  /// Add natvis files specified on the command line.
  void addNatvisFiles();

  /// Link CodeView from each object file in the symbol table into the PDB.
  void addObjectsToPDB();

  /// Link CodeView from a single object file into the target (output) PDB.
  /// When a precompiled headers object is linked, its TPI map might be provided
  /// externally.
  void addObjFile(ObjFile *File, CVIndexMap *ExternIndexMap = nullptr);

  /// Produce a mapping from the type and item indices used in the object
  /// file to those in the destination PDB.
  ///
  /// If the object file uses a type server PDB (compiled with /Zi), merge TPI
  /// and IPI from the type server PDB and return a map for it. Each unique type
  /// server PDB is merged at most once, so this may return an existing index
  /// mapping.
  ///
  /// If the object does not use a type server PDB (compiled with /Z7), we merge
  /// all the type and item records from the .debug$S stream and fill in the
  /// caller-provided ObjectIndexMap.
  Expected<const CVIndexMap &> mergeDebugT(ObjFile *File,
                                           CVIndexMap *ObjectIndexMap);

  /// Reads and makes available a PDB.
  Expected<const CVIndexMap &> maybeMergeTypeServerPDB(ObjFile *File,
                                                       const CVType &FirstType);

  /// Merges a precompiled headers TPI map into the current TPI map. The
  /// precompiled headers object will also be loaded and remapped in the
  /// process.
  Expected<const CVIndexMap &>
  mergeInPrecompHeaderObj(ObjFile *File, const CVType &FirstType,
                          CVIndexMap *ObjectIndexMap);

  /// Reads and makes available a precompiled headers object.
  ///
  /// This is a requirement for objects compiled with cl.exe /Yu. In that
  /// case, the referenced object (which was compiled with /Yc) has to be loaded
  /// first. This is mainly because the current object's TPI stream has external
  /// references to the precompiled headers object.
  ///
  /// If the precompiled headers object was already loaded, this function will
  /// simply return its (remapped) TPI map.
  Expected<const CVIndexMap &> aquirePrecompObj(ObjFile *File,
                                                PrecompRecord Precomp);

  /// Adds a precompiled headers object signature -> TPI mapping.
  std::pair<CVIndexMap &, bool /*already there*/>
  registerPrecompiledHeaders(uint32_t Signature);

  void mergeSymbolRecords(ObjFile *File, const CVIndexMap &IndexMap,
                          std::vector<ulittle32_t *> &StringTableRefs,
                          BinaryStreamRef SymData);

  /// Add the section map and section contributions to the PDB.
  void addSections(ArrayRef<OutputSection *> OutputSections,
                   ArrayRef<uint8_t> SectionTable);

  /// Get the type table or the global type table if /DEBUG:GHASH is enabled.
  TypeCollection &getTypeTable() {
    if (Config->DebugGHashes)
      return GlobalTypeTable;
    return TypeTable;
  }

  /// Get the ID table or the global ID table if /DEBUG:GHASH is enabled.
  TypeCollection &getIDTable() {
    if (Config->DebugGHashes)
      return GlobalIDTable;
    return IDTable;
  }

  /// Write the PDB to disk and store the Guid generated for it in *Guid.
  void commit(codeview::GUID *Guid);

private:
  BumpPtrAllocator Alloc;

  SymbolTable *Symtab;

  pdb::PDBFileBuilder Builder;

  /// Type records that will go into the PDB TPI stream.
  MergingTypeTableBuilder TypeTable;

  /// Item records that will go into the PDB IPI stream.
  MergingTypeTableBuilder IDTable;

  /// Type records that will go into the PDB TPI stream (for /DEBUG:GHASH)
  GlobalTypeTableBuilder GlobalTypeTable;

  /// Item records that will go into the PDB IPI stream (for /DEBUG:GHASH)
  GlobalTypeTableBuilder GlobalIDTable;

  /// PDBs use a single global string table for filenames in the file checksum
  /// table.
  DebugStringTableSubsection PDBStrTab;

  llvm::SmallString<128> NativePath;

  /// A list of other PDBs which are loaded during the linking process and which
  /// we need to keep around since the linking operation may reference pointers
  /// inside of these PDBs.
  llvm::SmallVector<std::unique_ptr<pdb::NativeSession>, 2> LoadedPDBs;

  std::vector<pdb::SecMapEntry> SectionMap;

  /// Type index mappings of type server PDBs that we've loaded so far.
  std::map<codeview::GUID, CVIndexMap> TypeServerIndexMappings;

  /// Type index mappings of precompiled objects type map that we've loaded so
  /// far.
  std::map<uint32_t, CVIndexMap> PrecompTypeIndexMappings;

  /// List of TypeServer PDBs which cannot be loaded.
  /// Cached to prevent repeated load attempts.
  std::map<codeview::GUID, std::string> MissingTypeServerPDBs;
};

class DebugSHandler {
  PDBLinker &Linker;

  /// The object file whose .debug$S sections we're processing.
  ObjFile &File;

  /// The result of merging type indices.
  const CVIndexMap &IndexMap;

  /// The DEBUG_S_STRINGTABLE subsection.  These strings are referred to by
  /// index from other records in the .debug$S section.  All of these strings
  /// need to be added to the global PDB string table, and all references to
  /// these strings need to have their indices re-written to refer to the
  /// global PDB string table.
  DebugStringTableSubsectionRef CVStrTab;

  /// The DEBUG_S_FILECHKSMS subsection.  As above, these are referred to
  /// by other records in the .debug$S section and need to be merged into the
  /// PDB.
  DebugChecksumsSubsectionRef Checksums;

  /// The DEBUG_S_FRAMEDATA subsection(s).  There can be more than one of
  /// these and they need not appear in any specific order.  However, they
  /// contain string table references which need to be re-written, so we
  /// collect them all here and re-write them after all subsections have been
  /// discovered and processed.
  std::vector<DebugFrameDataSubsectionRef> NewFpoFrames;

  /// Pointers to raw memory that we determine have string table references
  /// that need to be re-written.  We first process all .debug$S subsections
  /// to ensure that we can handle subsections written in any order, building
  /// up this list as we go.  At the end, we use the string table (which must
  /// have been discovered by now else it is an error) to re-write these
  /// references.
  std::vector<ulittle32_t *> StringTableReferences;

public:
  DebugSHandler(PDBLinker &Linker, ObjFile &File, const CVIndexMap &IndexMap)
      : Linker(Linker), File(File), IndexMap(IndexMap) {}

  void handleDebugS(lld::coff::SectionChunk &DebugS);
  void finish();
};
}

// Visual Studio's debugger requires absolute paths in various places in the
// PDB to work without additional configuration:
// https://docs.microsoft.com/en-us/visualstudio/debugger/debug-source-files-common-properties-solution-property-pages-dialog-box
static void pdbMakeAbsolute(SmallVectorImpl<char> &FileName) {
  // The default behavior is to produce paths that are valid within the context
  // of the machine that you perform the link on.  If the linker is running on
  // a POSIX system, we will output absolute POSIX paths.  If the linker is
  // running on a Windows system, we will output absolute Windows paths.  If the
  // user desires any other kind of behavior, they should explicitly pass
  // /pdbsourcepath, in which case we will treat the exact string the user
  // passed in as the gospel and not normalize, canonicalize it.
  if (sys::path::is_absolute(FileName, sys::path::Style::windows) ||
      sys::path::is_absolute(FileName, sys::path::Style::posix))
    return;

  // It's not absolute in any path syntax.  Relative paths necessarily refer to
  // the local file system, so we can make it native without ending up with a
  // nonsensical path.
  if (Config->PDBSourcePath.empty()) {
    sys::path::native(FileName);
    sys::fs::make_absolute(FileName);
    return;
  }

  // Try to guess whether /PDBSOURCEPATH is a unix path or a windows path.
  // Since PDB's are more of a Windows thing, we make this conservative and only
  // decide that it's a unix path if we're fairly certain.  Specifically, if
  // it starts with a forward slash.
  SmallString<128> AbsoluteFileName = Config->PDBSourcePath;
  sys::path::Style GuessedStyle = AbsoluteFileName.startswith("/")
                                      ? sys::path::Style::posix
                                      : sys::path::Style::windows;
  sys::path::append(AbsoluteFileName, GuessedStyle, FileName);
  sys::path::native(AbsoluteFileName, GuessedStyle);
  sys::path::remove_dots(AbsoluteFileName, true, GuessedStyle);

  FileName = std::move(AbsoluteFileName);
}

static SectionChunk *findByName(ArrayRef<SectionChunk *> Sections,
                                StringRef Name) {
  for (SectionChunk *C : Sections)
    if (C->getSectionName() == Name)
      return C;
  return nullptr;
}

static ArrayRef<uint8_t> consumeDebugMagic(ArrayRef<uint8_t> Data,
                                           StringRef SecName) {
  // First 4 bytes are section magic.
  if (Data.size() < 4)
    fatal(SecName + " too short");
  if (support::endian::read32le(Data.data()) != COFF::DEBUG_SECTION_MAGIC)
    fatal(SecName + " has an invalid magic");
  return Data.slice(4);
}

static ArrayRef<uint8_t> getDebugSection(ObjFile *File, StringRef SecName) {
  if (SectionChunk *Sec = findByName(File->getDebugChunks(), SecName))
    return consumeDebugMagic(Sec->getContents(), SecName);
  return {};
}

// A COFF .debug$H section is currently a clang extension.  This function checks
// if a .debug$H section is in a format that we expect / understand, so that we
// can ignore any sections which are coincidentally also named .debug$H but do
// not contain a format we recognize.
static bool canUseDebugH(ArrayRef<uint8_t> DebugH) {
  if (DebugH.size() < sizeof(object::debug_h_header))
    return false;
  auto *Header =
      reinterpret_cast<const object::debug_h_header *>(DebugH.data());
  DebugH = DebugH.drop_front(sizeof(object::debug_h_header));
  return Header->Magic == COFF::DEBUG_HASHES_SECTION_MAGIC &&
         Header->Version == 0 &&
         Header->HashAlgorithm == uint16_t(GlobalTypeHashAlg::SHA1_8) &&
         (DebugH.size() % 8 == 0);
}

static Optional<ArrayRef<uint8_t>> getDebugH(ObjFile *File) {
  SectionChunk *Sec = findByName(File->getDebugChunks(), ".debug$H");
  if (!Sec)
    return llvm::None;
  ArrayRef<uint8_t> Contents = Sec->getContents();
  if (!canUseDebugH(Contents))
    return None;
  return Contents;
}

static ArrayRef<GloballyHashedType>
getHashesFromDebugH(ArrayRef<uint8_t> DebugH) {
  assert(canUseDebugH(DebugH));

  DebugH = DebugH.drop_front(sizeof(object::debug_h_header));
  uint32_t Count = DebugH.size() / sizeof(GloballyHashedType);
  return {reinterpret_cast<const GloballyHashedType *>(DebugH.data()), Count};
}

static void addTypeInfo(pdb::TpiStreamBuilder &TpiBuilder,
                        TypeCollection &TypeTable) {
  // Start the TPI or IPI stream header.
  TpiBuilder.setVersionHeader(pdb::PdbTpiV80);

  // Flatten the in memory type table and hash each type.
  TypeTable.ForEachRecord([&](TypeIndex TI, const CVType &Type) {
    auto Hash = pdb::hashTypeRecord(Type);
    if (auto E = Hash.takeError())
      fatal("type hashing error");
    TpiBuilder.addTypeRecord(Type.RecordData, *Hash);
  });
}

// OBJs usually start their symbol stream with a S_OBJNAME record. This record
// also contains the signature/key of the current PCH session. The signature
// must be same for all objects which depend on the precompiled object.
// Recompiling the precompiled headers will generate a new PCH key and thus
// invalidate all the dependent objects.
static uint32_t extractPCHSignature(ObjFile *File) {
  auto DbgIt = find_if(File->getDebugChunks(), [](SectionChunk *C) {
    return C->getSectionName() == ".debug$S";
  });
  if (!DbgIt)
    return 0;

  ArrayRef<uint8_t> Contents =
      consumeDebugMagic((*DbgIt)->getContents(), ".debug$S");
  DebugSubsectionArray Subsections;
  BinaryStreamReader Reader(Contents, support::little);
  ExitOnErr(Reader.readArray(Subsections, Contents.size()));

  for (const DebugSubsectionRecord &SS : Subsections) {
    if (SS.kind() != DebugSubsectionKind::Symbols)
      continue;

    // If it's there, the S_OBJNAME record shall come first in the stream.
    Expected<CVSymbol> Sym = readSymbolFromStream(SS.getRecordData(), 0);
    if (!Sym) {
      consumeError(Sym.takeError());
      continue;
    }
    if (auto ObjName = SymbolDeserializer::deserializeAs<ObjNameSym>(Sym.get()))
      return ObjName->Signature;
  }
  return 0;
}

Expected<const CVIndexMap &>
PDBLinker::mergeDebugT(ObjFile *File, CVIndexMap *ObjectIndexMap) {
  ScopedTimer T(TypeMergingTimer);

  bool IsPrecompiledHeader = false;

  ArrayRef<uint8_t> Data = getDebugSection(File, ".debug$T");
  if (Data.empty()) {
    // Try again, Microsoft precompiled headers use .debug$P instead of
    // .debug$T
    Data = getDebugSection(File, ".debug$P");
    IsPrecompiledHeader = true;
  }
  if (Data.empty())
    return *ObjectIndexMap; // no debug info

  // Precompiled headers objects need to save the index map for further
  // reference by other objects which use the precompiled headers.
  if (IsPrecompiledHeader) {
    uint32_t PCHSignature = extractPCHSignature(File);
    if (PCHSignature == 0)
      fatal("No signature found for the precompiled headers OBJ (" +
            File->getName() + ")");

    // When a precompiled headers object comes first on the command-line, we
    // update the mapping here. Otherwise, if an object referencing the
    // precompiled headers object comes first, the mapping is created in
    // aquirePrecompObj(), thus we would skip this block.
    if (!ObjectIndexMap->IsPrecompiledTypeMap) {
      auto R = registerPrecompiledHeaders(PCHSignature);
      if (R.second)
        fatal(
            "A precompiled headers OBJ with the same signature was already "
            "provided! (" +
            File->getName() + ")");

      ObjectIndexMap = &R.first;
    }
  }

  BinaryByteStream Stream(Data, support::little);
  CVTypeArray Types;
  BinaryStreamReader Reader(Stream);
  if (auto EC = Reader.readArray(Types, Reader.getLength()))
    fatal("Reader::readArray failed: " + toString(std::move(EC)));

  auto FirstType = Types.begin();
  if (FirstType == Types.end())
    return *ObjectIndexMap;

  if (FirstType->kind() == LF_TYPESERVER2) {
    // Look through type servers. If we've already seen this type server,
    // don't merge any type information.
    return maybeMergeTypeServerPDB(File, *FirstType);
  } else if (FirstType->kind() == LF_PRECOMP) {
    // This object was compiled with /Yu, so process the corresponding
    // precompiled headers object (/Yc) first. Some type indices in the current
    // object are referencing data in the precompiled headers object, so we need
    // both to be loaded.
    auto E = mergeInPrecompHeaderObj(File, *FirstType, ObjectIndexMap);
    if (!E)
      return E.takeError();

    // Drop LF_PRECOMP record from the input stream, as it needs to be replaced
    // with the precompiled headers object type stream.
    // Note that we can't just call Types.drop_front(), as we explicitly want to
    // rebase the stream.
    Types.setUnderlyingStream(
        Types.getUnderlyingStream().drop_front(FirstType->RecordData.size()));
  }

  // Fill in the temporary, caller-provided ObjectIndexMap.
  if (Config->DebugGHashes) {
    ArrayRef<GloballyHashedType> Hashes;
    std::vector<GloballyHashedType> OwnedHashes;
    if (Optional<ArrayRef<uint8_t>> DebugH = getDebugH(File))
      Hashes = getHashesFromDebugH(*DebugH);
    else {
      OwnedHashes = GloballyHashedType::hashTypes(Types);
      Hashes = OwnedHashes;
    }

    if (auto Err = mergeTypeAndIdRecords(GlobalIDTable, GlobalTypeTable,
                                         ObjectIndexMap->TPIMap, Types, Hashes,
                                         File->PCHSignature))
      fatal("codeview::mergeTypeAndIdRecords failed: " +
            toString(std::move(Err)));
  } else {
    if (auto Err =
            mergeTypeAndIdRecords(IDTable, TypeTable, ObjectIndexMap->TPIMap,
                                  Types, File->PCHSignature))
      fatal("codeview::mergeTypeAndIdRecords failed: " +
            toString(std::move(Err)));
  }
  return *ObjectIndexMap;
}

static Expected<std::unique_ptr<pdb::NativeSession>>
tryToLoadPDB(const codeview::GUID &GuidFromObj, StringRef TSPath) {
  // Ensure the file exists before anything else. We want to return ENOENT,
  // "file not found", even if the path points to a removable device (in which
  // case the return message would be EAGAIN, "resource unavailable try again")
  if (!llvm::sys::fs::exists(TSPath))
    return errorCodeToError(std::error_code(ENOENT, std::generic_category()));

  ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr = MemoryBuffer::getFile(
      TSPath, /*FileSize=*/-1, /*RequiresNullTerminator=*/false);
  if (!MBOrErr)
    return errorCodeToError(MBOrErr.getError());

  std::unique_ptr<pdb::IPDBSession> ThisSession;
  if (auto EC = pdb::NativeSession::createFromPdb(
          MemoryBuffer::getMemBuffer(Driver->takeBuffer(std::move(*MBOrErr)),
                                     /*RequiresNullTerminator=*/false),
          ThisSession))
    return std::move(EC);

  std::unique_ptr<pdb::NativeSession> NS(
      static_cast<pdb::NativeSession *>(ThisSession.release()));
  pdb::PDBFile &File = NS->getPDBFile();
  auto ExpectedInfo = File.getPDBInfoStream();
  // All PDB Files should have an Info stream.
  if (!ExpectedInfo)
    return ExpectedInfo.takeError();

  // Just because a file with a matching name was found and it was an actual
  // PDB file doesn't mean it matches.  For it to match the InfoStream's GUID
  // must match the GUID specified in the TypeServer2 record.
  if (ExpectedInfo->getGuid() != GuidFromObj)
    return make_error<pdb::PDBError>(pdb::pdb_error_code::signature_out_of_date);

  return std::move(NS);
}

Expected<const CVIndexMap &>
PDBLinker::maybeMergeTypeServerPDB(ObjFile *File, const CVType &FirstType) {
  TypeServer2Record TS;
  if (auto EC =
          TypeDeserializer::deserializeAs(const_cast<CVType &>(FirstType), TS))
    fatal("error reading record: " + toString(std::move(EC)));

  const codeview::GUID &TSId = TS.getGuid();
  StringRef TSPath = TS.getName();

  // First, check if the PDB has previously failed to load.
  auto PrevErr = MissingTypeServerPDBs.find(TSId);
  if (PrevErr != MissingTypeServerPDBs.end())
    return createFileError(
        TSPath,
        make_error<StringError>(PrevErr->second, inconvertibleErrorCode()));

  // Second, check if we already loaded a PDB with this GUID. Return the type
  // index mapping if we have it.
  auto Insertion = TypeServerIndexMappings.insert({TSId, CVIndexMap()});
  CVIndexMap &IndexMap = Insertion.first->second;
  if (!Insertion.second)
    return IndexMap;

  // Mark this map as a type server map.
  IndexMap.IsTypeServerMap = true;

  // Check for a PDB at:
  // 1. The given file path
  // 2. Next to the object file or archive file
  auto ExpectedSession = handleExpected(
      tryToLoadPDB(TSId, TSPath),
      [&]() {
        StringRef LocalPath =
            !File->ParentName.empty() ? File->ParentName : File->getName();
        SmallString<128> Path = sys::path::parent_path(LocalPath);
        // Currently, type server PDBs are only created by cl, which only runs
        // on Windows, so we can assume type server paths are Windows style.
        sys::path::append(
            Path, sys::path::filename(TSPath, sys::path::Style::windows));
        return tryToLoadPDB(TSId, Path);
      },
      [&](std::unique_ptr<ECError> EC) -> Error {
        auto SysErr = EC->convertToErrorCode();
        // Only re-try loading if the previous error was "No such file or
        // directory"
        if (SysErr.category() == std::generic_category() &&
            SysErr.value() == ENOENT)
          return Error::success();
        return Error(std::move(EC));
      });

  if (auto E = ExpectedSession.takeError()) {
    TypeServerIndexMappings.erase(TSId);

    // Flatten the error to a string, for later display, if the error occurs
    // again on the same PDB.
    std::string ErrMsg;
    raw_string_ostream S(ErrMsg);
    S << E;
    MissingTypeServerPDBs.emplace(TSId, S.str());

    return createFileError(TSPath, std::move(E));
  }

  pdb::NativeSession *Session = ExpectedSession->get();

  // Keep a strong reference to this PDB, so that it's safe to hold pointers
  // into the file.
  LoadedPDBs.push_back(std::move(*ExpectedSession));

  auto ExpectedTpi = Session->getPDBFile().getPDBTpiStream();
  if (auto E = ExpectedTpi.takeError())
    fatal("Type server does not have TPI stream: " + toString(std::move(E)));
  auto ExpectedIpi = Session->getPDBFile().getPDBIpiStream();
  if (auto E = ExpectedIpi.takeError())
    fatal("Type server does not have TPI stream: " + toString(std::move(E)));

  if (Config->DebugGHashes) {
    // PDBs do not actually store global hashes, so when merging a type server
    // PDB we have to synthesize global hashes.  To do this, we first synthesize
    // global hashes for the TPI stream, since it is independent, then we
    // synthesize hashes for the IPI stream, using the hashes for the TPI stream
    // as inputs.
    auto TpiHashes = GloballyHashedType::hashTypes(ExpectedTpi->typeArray());
    auto IpiHashes =
        GloballyHashedType::hashIds(ExpectedIpi->typeArray(), TpiHashes);

    Optional<uint32_t> EndPrecomp;
    // Merge TPI first, because the IPI stream will reference type indices.
    if (auto Err = mergeTypeRecords(GlobalTypeTable, IndexMap.TPIMap,
                                    ExpectedTpi->typeArray(), TpiHashes, EndPrecomp))
      fatal("codeview::mergeTypeRecords failed: " + toString(std::move(Err)));

    // Merge IPI.
    if (auto Err =
            mergeIdRecords(GlobalIDTable, IndexMap.TPIMap, IndexMap.IPIMap,
                           ExpectedIpi->typeArray(), IpiHashes))
      fatal("codeview::mergeIdRecords failed: " + toString(std::move(Err)));
  } else {
    // Merge TPI first, because the IPI stream will reference type indices.
    if (auto Err = mergeTypeRecords(TypeTable, IndexMap.TPIMap,
                                    ExpectedTpi->typeArray()))
      fatal("codeview::mergeTypeRecords failed: " + toString(std::move(Err)));

    // Merge IPI.
    if (auto Err = mergeIdRecords(IDTable, IndexMap.TPIMap, IndexMap.IPIMap,
                                  ExpectedIpi->typeArray()))
      fatal("codeview::mergeIdRecords failed: " + toString(std::move(Err)));
  }

  return IndexMap;
}

Expected<const CVIndexMap &>
PDBLinker::mergeInPrecompHeaderObj(ObjFile *File, const CVType &FirstType,
                                   CVIndexMap *ObjectIndexMap) {
  PrecompRecord Precomp;
  if (auto EC = TypeDeserializer::deserializeAs(const_cast<CVType &>(FirstType),
                                                Precomp))
    fatal("error reading record: " + toString(std::move(EC)));

  auto E = aquirePrecompObj(File, Precomp);
  if (!E)
    return E.takeError();

  const CVIndexMap &PrecompIndexMap = *E;
  assert(PrecompIndexMap.IsPrecompiledTypeMap);

  if (PrecompIndexMap.TPIMap.empty())
    return PrecompIndexMap;

  assert(Precomp.getStartTypeIndex() == TypeIndex::FirstNonSimpleIndex);
  assert(Precomp.getTypesCount() <= PrecompIndexMap.TPIMap.size());
  // Use the previously remapped index map from the precompiled headers.
  ObjectIndexMap->TPIMap.append(PrecompIndexMap.TPIMap.begin(),
                                PrecompIndexMap.TPIMap.begin() +
                                    Precomp.getTypesCount());
  return *ObjectIndexMap;
}

static bool equals_path(StringRef path1, StringRef path2) {
#if defined(_WIN32)
  return path1.equals_lower(path2);
#else
  return path1.equals(path2);
#endif
}

// Find by name an OBJ provided on the command line
static ObjFile *findObjByName(StringRef FileNameOnly) {
  SmallString<128> CurrentPath;

  for (ObjFile *F : ObjFile::Instances) {
    StringRef CurrentFileName = sys::path::filename(F->getName());

    // Compare based solely on the file name (link.exe behavior)
    if (equals_path(CurrentFileName, FileNameOnly))
      return F;
  }
  return nullptr;
}

std::pair<CVIndexMap &, bool /*already there*/>
PDBLinker::registerPrecompiledHeaders(uint32_t Signature) {
  auto Insertion = PrecompTypeIndexMappings.insert({Signature, CVIndexMap()});
  CVIndexMap &IndexMap = Insertion.first->second;
  if (!Insertion.second)
    return {IndexMap, true};
  // Mark this map as a precompiled types map.
  IndexMap.IsPrecompiledTypeMap = true;
  return {IndexMap, false};
}

Expected<const CVIndexMap &>
PDBLinker::aquirePrecompObj(ObjFile *File, PrecompRecord Precomp) {
  // First, check if we already loaded the precompiled headers object with this
  // signature. Return the type index mapping if we've already seen it.
  auto R = registerPrecompiledHeaders(Precomp.getSignature());
  if (R.second)
    return R.first;

  CVIndexMap &IndexMap = R.first;

  // Cross-compile warning: given that Clang doesn't generate LF_PRECOMP
  // records, we assume the OBJ comes from a Windows build of cl.exe. Thusly,
  // the paths embedded in the OBJs are in the Windows format.
  SmallString<128> PrecompFileName = sys::path::filename(
      Precomp.getPrecompFilePath(), sys::path::Style::windows);

  // link.exe requires that a precompiled headers object must always be provided
  // on the command-line, even if that's not necessary.
  auto PrecompFile = findObjByName(PrecompFileName);
  if (!PrecompFile)
    return createFileError(
        PrecompFileName.str(),
        make_error<pdb::PDBError>(pdb::pdb_error_code::external_cmdline_ref));

  addObjFile(PrecompFile, &IndexMap);

  if (!PrecompFile->PCHSignature)
    fatal(PrecompFile->getName() + " is not a precompiled headers object");

  if (Precomp.getSignature() != PrecompFile->PCHSignature.getValueOr(0))
    return createFileError(
        Precomp.getPrecompFilePath().str(),
        make_error<pdb::PDBError>(pdb::pdb_error_code::signature_out_of_date));

  return IndexMap;
}

static bool remapTypeIndex(TypeIndex &TI, ArrayRef<TypeIndex> TypeIndexMap) {
  if (TI.isSimple())
    return true;
  if (TI.toArrayIndex() >= TypeIndexMap.size())
    return false;
  TI = TypeIndexMap[TI.toArrayIndex()];
  return true;
}

static void remapTypesInSymbolRecord(ObjFile *File, SymbolKind SymKind,
                                     MutableArrayRef<uint8_t> RecordBytes,
                                     const CVIndexMap &IndexMap,
                                     ArrayRef<TiReference> TypeRefs) {
  MutableArrayRef<uint8_t> Contents =
      RecordBytes.drop_front(sizeof(RecordPrefix));
  for (const TiReference &Ref : TypeRefs) {
    unsigned ByteSize = Ref.Count * sizeof(TypeIndex);
    if (Contents.size() < Ref.Offset + ByteSize)
      fatal("symbol record too short");

    // This can be an item index or a type index. Choose the appropriate map.
    ArrayRef<TypeIndex> TypeOrItemMap = IndexMap.TPIMap;
    bool IsItemIndex = Ref.Kind == TiRefKind::IndexRef;
    if (IsItemIndex && IndexMap.IsTypeServerMap)
      TypeOrItemMap = IndexMap.IPIMap;

    MutableArrayRef<TypeIndex> TIs(
        reinterpret_cast<TypeIndex *>(Contents.data() + Ref.Offset), Ref.Count);
    for (TypeIndex &TI : TIs) {
      if (!remapTypeIndex(TI, TypeOrItemMap)) {
        log("ignoring symbol record of kind 0x" + utohexstr(SymKind) + " in " +
            File->getName() + " with bad " + (IsItemIndex ? "item" : "type") +
            " index 0x" + utohexstr(TI.getIndex()));
        TI = TypeIndex(SimpleTypeKind::NotTranslated);
        continue;
      }
    }
  }
}

static void
recordStringTableReferenceAtOffset(MutableArrayRef<uint8_t> Contents,
                                   uint32_t Offset,
                                   std::vector<ulittle32_t *> &StrTableRefs) {
  Contents =
      Contents.drop_front(Offset).take_front(sizeof(support::ulittle32_t));
  ulittle32_t *Index = reinterpret_cast<ulittle32_t *>(Contents.data());
  StrTableRefs.push_back(Index);
}

static void
recordStringTableReferences(SymbolKind Kind, MutableArrayRef<uint8_t> Contents,
                            std::vector<ulittle32_t *> &StrTableRefs) {
  // For now we only handle S_FILESTATIC, but we may need the same logic for
  // S_DEFRANGE and S_DEFRANGE_SUBFIELD.  However, I cannot seem to generate any
  // PDBs that contain these types of records, so because of the uncertainty
  // they are omitted here until we can prove that it's necessary.
  switch (Kind) {
  case SymbolKind::S_FILESTATIC:
    // FileStaticSym::ModFileOffset
    recordStringTableReferenceAtOffset(Contents, 8, StrTableRefs);
    break;
  case SymbolKind::S_DEFRANGE:
  case SymbolKind::S_DEFRANGE_SUBFIELD:
    log("Not fixing up string table reference in S_DEFRANGE / "
        "S_DEFRANGE_SUBFIELD record");
    break;
  default:
    break;
  }
}

static SymbolKind symbolKind(ArrayRef<uint8_t> RecordData) {
  const RecordPrefix *Prefix =
      reinterpret_cast<const RecordPrefix *>(RecordData.data());
  return static_cast<SymbolKind>(uint16_t(Prefix->RecordKind));
}

/// MSVC translates S_PROC_ID_END to S_END, and S_[LG]PROC32_ID to S_[LG]PROC32
static void translateIdSymbols(MutableArrayRef<uint8_t> &RecordData,
                               TypeCollection &IDTable) {
  RecordPrefix *Prefix = reinterpret_cast<RecordPrefix *>(RecordData.data());

  SymbolKind Kind = symbolKind(RecordData);

  if (Kind == SymbolKind::S_PROC_ID_END) {
    Prefix->RecordKind = SymbolKind::S_END;
    return;
  }

  // In an object file, GPROC32_ID has an embedded reference which refers to the
  // single object file type index namespace.  This has already been translated
  // to the PDB file's ID stream index space, but we need to convert this to a
  // symbol that refers to the type stream index space.  So we remap again from
  // ID index space to type index space.
  if (Kind == SymbolKind::S_GPROC32_ID || Kind == SymbolKind::S_LPROC32_ID) {
    SmallVector<TiReference, 1> Refs;
    auto Content = RecordData.drop_front(sizeof(RecordPrefix));
    CVSymbol Sym(Kind, RecordData);
    discoverTypeIndicesInSymbol(Sym, Refs);
    assert(Refs.size() == 1);
    assert(Refs.front().Count == 1);

    TypeIndex *TI =
        reinterpret_cast<TypeIndex *>(Content.data() + Refs[0].Offset);
    // `TI` is the index of a FuncIdRecord or MemberFuncIdRecord which lives in
    // the IPI stream, whose `FunctionType` member refers to the TPI stream.
    // Note that LF_FUNC_ID and LF_MEMFUNC_ID have the same record layout, and
    // in both cases we just need the second type index.
    if (!TI->isSimple() && !TI->isNoneType()) {
      CVType FuncIdData = IDTable.getType(*TI);
      SmallVector<TypeIndex, 2> Indices;
      discoverTypeIndices(FuncIdData, Indices);
      assert(Indices.size() == 2);
      *TI = Indices[1];
    }

    Kind = (Kind == SymbolKind::S_GPROC32_ID) ? SymbolKind::S_GPROC32
                                              : SymbolKind::S_LPROC32;
    Prefix->RecordKind = uint16_t(Kind);
  }
}

/// Copy the symbol record. In a PDB, symbol records must be 4 byte aligned.
/// The object file may not be aligned.
static MutableArrayRef<uint8_t>
copyAndAlignSymbol(const CVSymbol &Sym, MutableArrayRef<uint8_t> &AlignedMem) {
  size_t Size = alignTo(Sym.length(), alignOf(CodeViewContainer::Pdb));
  assert(Size >= 4 && "record too short");
  assert(Size <= MaxRecordLength && "record too long");
  assert(AlignedMem.size() >= Size && "didn't preallocate enough");

  // Copy the symbol record and zero out any padding bytes.
  MutableArrayRef<uint8_t> NewData = AlignedMem.take_front(Size);
  AlignedMem = AlignedMem.drop_front(Size);
  memcpy(NewData.data(), Sym.data().data(), Sym.length());
  memset(NewData.data() + Sym.length(), 0, Size - Sym.length());

  // Update the record prefix length. It should point to the beginning of the
  // next record.
  auto *Prefix = reinterpret_cast<RecordPrefix *>(NewData.data());
  Prefix->RecordLen = Size - 2;
  return NewData;
}

struct ScopeRecord {
  ulittle32_t PtrParent;
  ulittle32_t PtrEnd;
};

struct SymbolScope {
  ScopeRecord *OpeningRecord;
  uint32_t ScopeOffset;
};

static void scopeStackOpen(SmallVectorImpl<SymbolScope> &Stack,
                           uint32_t CurOffset, CVSymbol &Sym) {
  assert(symbolOpensScope(Sym.kind()));
  SymbolScope S;
  S.ScopeOffset = CurOffset;
  S.OpeningRecord = const_cast<ScopeRecord *>(
      reinterpret_cast<const ScopeRecord *>(Sym.content().data()));
  S.OpeningRecord->PtrParent = Stack.empty() ? 0 : Stack.back().ScopeOffset;
  Stack.push_back(S);
}

static void scopeStackClose(SmallVectorImpl<SymbolScope> &Stack,
                            uint32_t CurOffset, ObjFile *File) {
  if (Stack.empty()) {
    warn("symbol scopes are not balanced in " + File->getName());
    return;
  }
  SymbolScope S = Stack.pop_back_val();
  S.OpeningRecord->PtrEnd = CurOffset;
}

static bool symbolGoesInModuleStream(const CVSymbol &Sym, bool IsGlobalScope) {
  switch (Sym.kind()) {
  case SymbolKind::S_GDATA32:
  case SymbolKind::S_CONSTANT:
  // We really should not be seeing S_PROCREF and S_LPROCREF in the first place
  // since they are synthesized by the linker in response to S_GPROC32 and
  // S_LPROC32, but if we do see them, don't put them in the module stream I
  // guess.
  case SymbolKind::S_PROCREF:
  case SymbolKind::S_LPROCREF:
    return false;
  // S_UDT records go in the module stream if it is not a global S_UDT.
  case SymbolKind::S_UDT:
    return !IsGlobalScope;
  // S_GDATA32 does not go in the module stream, but S_LDATA32 does.
  case SymbolKind::S_LDATA32:
  default:
    return true;
  }
}

static bool symbolGoesInGlobalsStream(const CVSymbol &Sym, bool IsGlobalScope) {
  switch (Sym.kind()) {
  case SymbolKind::S_CONSTANT:
  case SymbolKind::S_GDATA32:
  // S_LDATA32 goes in both the module stream and the globals stream.
  case SymbolKind::S_LDATA32:
  case SymbolKind::S_GPROC32:
  case SymbolKind::S_LPROC32:
  // We really should not be seeing S_PROCREF and S_LPROCREF in the first place
  // since they are synthesized by the linker in response to S_GPROC32 and
  // S_LPROC32, but if we do see them, copy them straight through.
  case SymbolKind::S_PROCREF:
  case SymbolKind::S_LPROCREF:
    return true;
  // S_UDT records go in the globals stream if it is a global S_UDT.
  case SymbolKind::S_UDT:
    return IsGlobalScope;
  default:
    return false;
  }
}

static void addGlobalSymbol(pdb::GSIStreamBuilder &Builder, uint16_t ModIndex,
                            unsigned SymOffset, const CVSymbol &Sym) {
  switch (Sym.kind()) {
  case SymbolKind::S_CONSTANT:
  case SymbolKind::S_UDT:
  case SymbolKind::S_GDATA32:
  case SymbolKind::S_LDATA32:
  case SymbolKind::S_PROCREF:
  case SymbolKind::S_LPROCREF:
    Builder.addGlobalSymbol(Sym);
    break;
  case SymbolKind::S_GPROC32:
  case SymbolKind::S_LPROC32: {
    SymbolRecordKind K = SymbolRecordKind::ProcRefSym;
    if (Sym.kind() == SymbolKind::S_LPROC32)
      K = SymbolRecordKind::LocalProcRef;
    ProcRefSym PS(K);
    PS.Module = ModIndex;
    // For some reason, MSVC seems to add one to this value.
    ++PS.Module;
    PS.Name = getSymbolName(Sym);
    PS.SumName = 0;
    PS.SymOffset = SymOffset;
    Builder.addGlobalSymbol(PS);
    break;
  }
  default:
    llvm_unreachable("Invalid symbol kind!");
  }
}

void PDBLinker::mergeSymbolRecords(ObjFile *File, const CVIndexMap &IndexMap,
                                   std::vector<ulittle32_t *> &StringTableRefs,
                                   BinaryStreamRef SymData) {
  ArrayRef<uint8_t> SymsBuffer;
  cantFail(SymData.readBytes(0, SymData.getLength(), SymsBuffer));
  SmallVector<SymbolScope, 4> Scopes;

  // Iterate every symbol to check if any need to be realigned, and if so, how
  // much space we need to allocate for them.
  bool NeedsRealignment = false;
  unsigned TotalRealignedSize = 0;
  auto EC = forEachCodeViewRecord<CVSymbol>(
      SymsBuffer, [&](CVSymbol Sym) -> llvm::Error {
        unsigned RealignedSize =
            alignTo(Sym.length(), alignOf(CodeViewContainer::Pdb));
        NeedsRealignment |= RealignedSize != Sym.length();
        TotalRealignedSize += RealignedSize;
        return Error::success();
      });

  // If any of the symbol record lengths was corrupt, ignore them all, warn
  // about it, and move on.
  if (EC) {
    warn("corrupt symbol records in " + File->getName());
    consumeError(std::move(EC));
    return;
  }

  // If any symbol needed realignment, allocate enough contiguous memory for
  // them all. Typically symbol subsections are small enough that this will not
  // cause fragmentation.
  MutableArrayRef<uint8_t> AlignedSymbolMem;
  if (NeedsRealignment) {
    void *AlignedData =
        Alloc.Allocate(TotalRealignedSize, alignOf(CodeViewContainer::Pdb));
    AlignedSymbolMem = makeMutableArrayRef(
        reinterpret_cast<uint8_t *>(AlignedData), TotalRealignedSize);
  }

  // Iterate again, this time doing the real work.
  unsigned CurSymOffset = File->ModuleDBI->getNextSymbolOffset();
  ArrayRef<uint8_t> BulkSymbols;
  cantFail(forEachCodeViewRecord<CVSymbol>(
      SymsBuffer, [&](CVSymbol Sym) -> llvm::Error {
        // Align the record if required.
        MutableArrayRef<uint8_t> RecordBytes;
        if (NeedsRealignment) {
          RecordBytes = copyAndAlignSymbol(Sym, AlignedSymbolMem);
          Sym = CVSymbol(Sym.kind(), RecordBytes);
        } else {
          // Otherwise, we can actually mutate the symbol directly, since we
          // copied it to apply relocations.
          RecordBytes = makeMutableArrayRef(
              const_cast<uint8_t *>(Sym.data().data()), Sym.length());
        }

        // Discover type index references in the record. Skip it if we don't
        // know where they are.
        SmallVector<TiReference, 32> TypeRefs;
        if (!discoverTypeIndicesInSymbol(Sym, TypeRefs)) {
          log("ignoring unknown symbol record with kind 0x" +
              utohexstr(Sym.kind()));
          return Error::success();
        }

        // Re-map all the type index references.
        remapTypesInSymbolRecord(File, Sym.kind(), RecordBytes, IndexMap,
                                 TypeRefs);

        // An object file may have S_xxx_ID symbols, but these get converted to
        // "real" symbols in a PDB.
        translateIdSymbols(RecordBytes, getIDTable());
        Sym = CVSymbol(symbolKind(RecordBytes), RecordBytes);

        // If this record refers to an offset in the object file's string table,
        // add that item to the global PDB string table and re-write the index.
        recordStringTableReferences(Sym.kind(), RecordBytes, StringTableRefs);

        // Fill in "Parent" and "End" fields by maintaining a stack of scopes.
        if (symbolOpensScope(Sym.kind()))
          scopeStackOpen(Scopes, CurSymOffset, Sym);
        else if (symbolEndsScope(Sym.kind()))
          scopeStackClose(Scopes, CurSymOffset, File);

        // Add the symbol to the globals stream if necessary.  Do this before
        // adding the symbol to the module since we may need to get the next
        // symbol offset, and writing to the module's symbol stream will update
        // that offset.
        if (symbolGoesInGlobalsStream(Sym, Scopes.empty()))
          addGlobalSymbol(Builder.getGsiBuilder(),
                          File->ModuleDBI->getModuleIndex(), CurSymOffset, Sym);

        if (symbolGoesInModuleStream(Sym, Scopes.empty())) {
          // Add symbols to the module in bulk. If this symbol is contiguous
          // with the previous run of symbols to add, combine the ranges. If
          // not, close the previous range of symbols and start a new one.
          if (Sym.data().data() == BulkSymbols.end()) {
            BulkSymbols = makeArrayRef(BulkSymbols.data(),
                                       BulkSymbols.size() + Sym.length());
          } else {
            File->ModuleDBI->addSymbolsInBulk(BulkSymbols);
            BulkSymbols = RecordBytes;
          }
          CurSymOffset += Sym.length();
        }
        return Error::success();
      }));

  // Add any remaining symbols we've accumulated.
  File->ModuleDBI->addSymbolsInBulk(BulkSymbols);
}

// Allocate memory for a .debug$S / .debug$F section and relocate it.
static ArrayRef<uint8_t> relocateDebugChunk(BumpPtrAllocator &Alloc,
                                            SectionChunk &DebugChunk) {
  uint8_t *Buffer = Alloc.Allocate<uint8_t>(DebugChunk.getSize());
  assert(DebugChunk.OutputSectionOff == 0 &&
         "debug sections should not be in output sections");
  DebugChunk.readRelocTargets();
  DebugChunk.writeTo(Buffer);
  return makeArrayRef(Buffer, DebugChunk.getSize());
}

static pdb::SectionContrib createSectionContrib(const Chunk *C, uint32_t Modi) {
  OutputSection *OS = C->getOutputSection();
  pdb::SectionContrib SC;
  memset(&SC, 0, sizeof(SC));
  SC.ISect = OS->SectionIndex;
  SC.Off = C->getRVA() - OS->getRVA();
  SC.Size = C->getSize();
  if (auto *SecChunk = dyn_cast<SectionChunk>(C)) {
    SC.Characteristics = SecChunk->Header->Characteristics;
    SC.Imod = SecChunk->File->ModuleDBI->getModuleIndex();
    ArrayRef<uint8_t> Contents = SecChunk->getContents();
    JamCRC CRC(0);
    ArrayRef<char> CharContents = makeArrayRef(
        reinterpret_cast<const char *>(Contents.data()), Contents.size());
    CRC.update(CharContents);
    SC.DataCrc = CRC.getCRC();
  } else {
    SC.Characteristics = OS->Header.Characteristics;
    // FIXME: When we start creating DBI for import libraries, use those here.
    SC.Imod = Modi;
  }
  SC.RelocCrc = 0; // FIXME

  return SC;
}

static uint32_t
translateStringTableIndex(uint32_t ObjIndex,
                          const DebugStringTableSubsectionRef &ObjStrTable,
                          DebugStringTableSubsection &PdbStrTable) {
  auto ExpectedString = ObjStrTable.getString(ObjIndex);
  if (!ExpectedString) {
    warn("Invalid string table reference");
    consumeError(ExpectedString.takeError());
    return 0;
  }

  return PdbStrTable.insert(*ExpectedString);
}

void DebugSHandler::handleDebugS(lld::coff::SectionChunk &DebugS) {
  DebugSubsectionArray Subsections;

  ArrayRef<uint8_t> RelocatedDebugContents = consumeDebugMagic(
      relocateDebugChunk(Linker.Alloc, DebugS), DebugS.getSectionName());

  BinaryStreamReader Reader(RelocatedDebugContents, support::little);
  ExitOnErr(Reader.readArray(Subsections, RelocatedDebugContents.size()));

  for (const DebugSubsectionRecord &SS : Subsections) {
    switch (SS.kind()) {
    case DebugSubsectionKind::StringTable: {
      assert(!CVStrTab.valid() &&
             "Encountered multiple string table subsections!");
      ExitOnErr(CVStrTab.initialize(SS.getRecordData()));
      break;
    }
    case DebugSubsectionKind::FileChecksums:
      assert(!Checksums.valid() &&
             "Encountered multiple checksum subsections!");
      ExitOnErr(Checksums.initialize(SS.getRecordData()));
      break;
    case DebugSubsectionKind::Lines:
      // We can add the relocated line table directly to the PDB without
      // modification because the file checksum offsets will stay the same.
      File.ModuleDBI->addDebugSubsection(SS);
      break;
    case DebugSubsectionKind::FrameData: {
      // We need to re-write string table indices here, so save off all
      // frame data subsections until we've processed the entire list of
      // subsections so that we can be sure we have the string table.
      DebugFrameDataSubsectionRef FDS;
      ExitOnErr(FDS.initialize(SS.getRecordData()));
      NewFpoFrames.push_back(std::move(FDS));
      break;
    }
    case DebugSubsectionKind::Symbols: {
      Linker.mergeSymbolRecords(&File, IndexMap, StringTableReferences,
                                SS.getRecordData());
      break;
    }
    default:
      // FIXME: Process the rest of the subsections.
      break;
    }
  }
}

void DebugSHandler::finish() {
  pdb::DbiStreamBuilder &DbiBuilder = Linker.Builder.getDbiBuilder();

  // We should have seen all debug subsections across the entire object file now
  // which means that if a StringTable subsection and Checksums subsection were
  // present, now is the time to handle them.
  if (!CVStrTab.valid()) {
    if (Checksums.valid())
      fatal(".debug$S sections with a checksums subsection must also contain a "
            "string table subsection");

    if (!StringTableReferences.empty())
      warn("No StringTable subsection was encountered, but there are string "
           "table references");
    return;
  }

  // Rewrite string table indices in the Fpo Data and symbol records to refer to
  // the global PDB string table instead of the object file string table.
  for (DebugFrameDataSubsectionRef &FDS : NewFpoFrames) {
    const ulittle32_t *Reloc = FDS.getRelocPtr();
    for (codeview::FrameData FD : FDS) {
      FD.RvaStart += *Reloc;
      FD.FrameFunc =
          translateStringTableIndex(FD.FrameFunc, CVStrTab, Linker.PDBStrTab);
      DbiBuilder.addNewFpoData(FD);
    }
  }

  for (ulittle32_t *Ref : StringTableReferences)
    *Ref = translateStringTableIndex(*Ref, CVStrTab, Linker.PDBStrTab);

  // Make a new file checksum table that refers to offsets in the PDB-wide
  // string table. Generally the string table subsection appears after the
  // checksum table, so we have to do this after looping over all the
  // subsections.
  auto NewChecksums = make_unique<DebugChecksumsSubsection>(Linker.PDBStrTab);
  for (FileChecksumEntry &FC : Checksums) {
    SmallString<128> FileName =
        ExitOnErr(CVStrTab.getString(FC.FileNameOffset));
    pdbMakeAbsolute(FileName);
    ExitOnErr(Linker.Builder.getDbiBuilder().addModuleSourceFile(
        *File.ModuleDBI, FileName));
    NewChecksums->addChecksum(FileName, FC.Kind, FC.Checksum);
  }
  File.ModuleDBI->addDebugSubsection(std::move(NewChecksums));
}

void PDBLinker::addObjFile(ObjFile *File, CVIndexMap *ExternIndexMap) {
  if (File->wasProcessedForPDB())
    return;
  // Add a module descriptor for every object file. We need to put an absolute
  // path to the object into the PDB. If this is a plain object, we make its
  // path absolute. If it's an object in an archive, we make the archive path
  // absolute.
  bool InArchive = !File->ParentName.empty();
  SmallString<128> Path = InArchive ? File->ParentName : File->getName();
  pdbMakeAbsolute(Path);
  StringRef Name = InArchive ? File->getName() : StringRef(Path);

  pdb::DbiStreamBuilder &DbiBuilder = Builder.getDbiBuilder();
  File->ModuleDBI = &ExitOnErr(DbiBuilder.addModuleInfo(Name));
  File->ModuleDBI->setObjFileName(Path);

  auto Chunks = File->getChunks();
  uint32_t Modi = File->ModuleDBI->getModuleIndex();
  for (Chunk *C : Chunks) {
    auto *SecChunk = dyn_cast<SectionChunk>(C);
    if (!SecChunk || !SecChunk->Live)
      continue;
    pdb::SectionContrib SC = createSectionContrib(SecChunk, Modi);
    File->ModuleDBI->setFirstSectionContrib(SC);
    break;
  }

  // Before we can process symbol substreams from .debug$S, we need to process
  // type information, file checksums, and the string table.  Add type info to
  // the PDB first, so that we can get the map from object file type and item
  // indices to PDB type and item indices.
  CVIndexMap ObjectIndexMap;
  auto IndexMapResult =
      mergeDebugT(File, ExternIndexMap ? ExternIndexMap : &ObjectIndexMap);

  // If the .debug$T sections fail to merge, assume there is no debug info.
  if (!IndexMapResult) {
    if (!Config->WarnDebugInfoUnusable) {
      consumeError(IndexMapResult.takeError());
      return;
    }
    StringRef FileName = sys::path::filename(Path);
    warn("Cannot use debug info for '" + FileName + "' [LNK4099]\n" +
         ">>> failed to load reference " +
         StringRef(toString(IndexMapResult.takeError())));
    return;
  }

  ScopedTimer T(SymbolMergingTimer);

  DebugSHandler DSH(*this, *File, *IndexMapResult);
  // Now do all live .debug$S and .debug$F sections.
  for (SectionChunk *DebugChunk : File->getDebugChunks()) {
    if (!DebugChunk->Live || DebugChunk->getSize() == 0)
      continue;

    if (DebugChunk->getSectionName() == ".debug$S") {
      DSH.handleDebugS(*DebugChunk);
      continue;
    }

    if (DebugChunk->getSectionName() == ".debug$F") {
      ArrayRef<uint8_t> RelocatedDebugContents =
          relocateDebugChunk(Alloc, *DebugChunk);

      FixedStreamArray<object::FpoData> FpoRecords;
      BinaryStreamReader Reader(RelocatedDebugContents, support::little);
      uint32_t Count = RelocatedDebugContents.size() / sizeof(object::FpoData);
      ExitOnErr(Reader.readArray(FpoRecords, Count));

      // These are already relocated and don't refer to the string table, so we
      // can just copy it.
      for (const object::FpoData &FD : FpoRecords)
        DbiBuilder.addOldFpoData(FD);
      continue;
    }
  }

  // Do any post-processing now that all .debug$S sections have been processed.
  DSH.finish();
}

static PublicSym32 createPublic(Defined *Def) {
  PublicSym32 Pub(SymbolKind::S_PUB32);
  Pub.Name = Def->getName();
  if (auto *D = dyn_cast<DefinedCOFF>(Def)) {
    if (D->getCOFFSymbol().isFunctionDefinition())
      Pub.Flags = PublicSymFlags::Function;
  } else if (isa<DefinedImportThunk>(Def)) {
    Pub.Flags = PublicSymFlags::Function;
  }

  OutputSection *OS = Def->getChunk()->getOutputSection();
  assert(OS && "all publics should be in final image");
  Pub.Offset = Def->getRVA() - OS->getRVA();
  Pub.Segment = OS->SectionIndex;
  return Pub;
}

// Add all object files to the PDB. Merge .debug$T sections into IpiData and
// TpiData.
void PDBLinker::addObjectsToPDB() {
  ScopedTimer T1(AddObjectsTimer);
  for (ObjFile *File : ObjFile::Instances)
    addObjFile(File);

  Builder.getStringTableBuilder().setStrings(PDBStrTab);
  T1.stop();

  // Construct TPI and IPI stream contents.
  ScopedTimer T2(TpiStreamLayoutTimer);
  addTypeInfo(Builder.getTpiBuilder(), getTypeTable());
  addTypeInfo(Builder.getIpiBuilder(), getIDTable());
  T2.stop();

  ScopedTimer T3(GlobalsLayoutTimer);
  // Compute the public and global symbols.
  auto &GsiBuilder = Builder.getGsiBuilder();
  std::vector<PublicSym32> Publics;
  Symtab->forEachSymbol([&Publics](Symbol *S) {
    // Only emit defined, live symbols that have a chunk.
    auto *Def = dyn_cast<Defined>(S);
    if (Def && Def->isLive() && Def->getChunk())
      Publics.push_back(createPublic(Def));
  });

  if (!Publics.empty()) {
    // Sort the public symbols and add them to the stream.
    sort(parallel::par, Publics.begin(), Publics.end(),
         [](const PublicSym32 &L, const PublicSym32 &R) {
           return L.Name < R.Name;
         });
    for (const PublicSym32 &Pub : Publics)
      GsiBuilder.addPublicSymbol(Pub);
  }
}

void PDBLinker::addNatvisFiles() {
  for (StringRef File : Config->NatvisFiles) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> DataOrErr =
        MemoryBuffer::getFile(File);
    if (!DataOrErr) {
      warn("Cannot open input file: " + File);
      continue;
    }
    Builder.addInjectedSource(File, std::move(*DataOrErr));
  }
}

static codeview::CPUType toCodeViewMachine(COFF::MachineTypes Machine) {
  switch (Machine) {
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return codeview::CPUType::X64;
  case COFF::IMAGE_FILE_MACHINE_ARM:
    return codeview::CPUType::ARM7;
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    return codeview::CPUType::ARM64;
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    return codeview::CPUType::ARMNT;
  case COFF::IMAGE_FILE_MACHINE_I386:
    return codeview::CPUType::Intel80386;
  default:
    llvm_unreachable("Unsupported CPU Type");
  }
}

// Mimic MSVC which surrounds arguments containing whitespace with quotes.
// Double double-quotes are handled, so that the resulting string can be
// executed again on the cmd-line.
static std::string quote(ArrayRef<StringRef> Args) {
  std::string R;
  R.reserve(256);
  for (StringRef A : Args) {
    if (!R.empty())
      R.push_back(' ');
    bool HasWS = A.find(' ') != StringRef::npos;
    bool HasQ = A.find('"') != StringRef::npos;
    if (HasWS || HasQ)
      R.push_back('"');
    if (HasQ) {
      SmallVector<StringRef, 4> S;
      A.split(S, '"');
      R.append(join(S, "\"\""));
    } else {
      R.append(A);
    }
    if (HasWS || HasQ)
      R.push_back('"');
  }
  return R;
}

static void addCommonLinkerModuleSymbols(StringRef Path,
                                         pdb::DbiModuleDescriptorBuilder &Mod,
                                         BumpPtrAllocator &Allocator) {
  ObjNameSym ONS(SymbolRecordKind::ObjNameSym);
  Compile3Sym CS(SymbolRecordKind::Compile3Sym);
  EnvBlockSym EBS(SymbolRecordKind::EnvBlockSym);

  ONS.Name = "* Linker *";
  ONS.Signature = 0;

  CS.Machine = toCodeViewMachine(Config->Machine);
  // Interestingly, if we set the string to 0.0.0.0, then when trying to view
  // local variables WinDbg emits an error that private symbols are not present.
  // By setting this to a valid MSVC linker version string, local variables are
  // displayed properly.   As such, even though it is not representative of
  // LLVM's version information, we need this for compatibility.
  CS.Flags = CompileSym3Flags::None;
  CS.VersionBackendBuild = 25019;
  CS.VersionBackendMajor = 14;
  CS.VersionBackendMinor = 10;
  CS.VersionBackendQFE = 0;

  // MSVC also sets the frontend to 0.0.0.0 since this is specifically for the
  // linker module (which is by definition a backend), so we don't need to do
  // anything here.  Also, it seems we can use "LLVM Linker" for the linker name
  // without any problems.  Only the backend version has to be hardcoded to a
  // magic number.
  CS.VersionFrontendBuild = 0;
  CS.VersionFrontendMajor = 0;
  CS.VersionFrontendMinor = 0;
  CS.VersionFrontendQFE = 0;
  CS.Version = "LLVM Linker";
  CS.setLanguage(SourceLanguage::Link);

  ArrayRef<StringRef> Args = makeArrayRef(Config->Argv).drop_front();
  std::string ArgStr = quote(Args);
  EBS.Fields.push_back("cwd");
  SmallString<64> cwd;
  if (Config->PDBSourcePath.empty())
    sys::fs::current_path(cwd);
  else
    cwd = Config->PDBSourcePath;
  EBS.Fields.push_back(cwd);
  EBS.Fields.push_back("exe");
  SmallString<64> exe = Config->Argv[0];
  pdbMakeAbsolute(exe);
  EBS.Fields.push_back(exe);
  EBS.Fields.push_back("pdb");
  EBS.Fields.push_back(Path);
  EBS.Fields.push_back("cmd");
  EBS.Fields.push_back(ArgStr);
  Mod.addSymbol(codeview::SymbolSerializer::writeOneSymbol(
      ONS, Allocator, CodeViewContainer::Pdb));
  Mod.addSymbol(codeview::SymbolSerializer::writeOneSymbol(
      CS, Allocator, CodeViewContainer::Pdb));
  Mod.addSymbol(codeview::SymbolSerializer::writeOneSymbol(
      EBS, Allocator, CodeViewContainer::Pdb));
}

static void addLinkerModuleSectionSymbol(pdb::DbiModuleDescriptorBuilder &Mod,
                                         OutputSection &OS,
                                         BumpPtrAllocator &Allocator) {
  SectionSym Sym(SymbolRecordKind::SectionSym);
  Sym.Alignment = 12; // 2^12 = 4KB
  Sym.Characteristics = OS.Header.Characteristics;
  Sym.Length = OS.getVirtualSize();
  Sym.Name = OS.Name;
  Sym.Rva = OS.getRVA();
  Sym.SectionNumber = OS.SectionIndex;
  Mod.addSymbol(codeview::SymbolSerializer::writeOneSymbol(
      Sym, Allocator, CodeViewContainer::Pdb));
}

// Creates a PDB file.
void coff::createPDB(SymbolTable *Symtab,
                     ArrayRef<OutputSection *> OutputSections,
                     ArrayRef<uint8_t> SectionTable,
                     llvm::codeview::DebugInfo *BuildId) {
  ScopedTimer T1(TotalPdbLinkTimer);
  PDBLinker PDB(Symtab);

  PDB.initialize(BuildId);
  PDB.addObjectsToPDB();
  PDB.addSections(OutputSections, SectionTable);
  PDB.addNatvisFiles();

  ScopedTimer T2(DiskCommitTimer);
  codeview::GUID Guid;
  PDB.commit(&Guid);
  memcpy(&BuildId->PDB70.Signature, &Guid, 16);
}

void PDBLinker::initialize(llvm::codeview::DebugInfo *BuildId) {
  ExitOnErr(Builder.initialize(4096)); // 4096 is blocksize

  BuildId->Signature.CVSignature = OMF::Signature::PDB70;
  // Signature is set to a hash of the PDB contents when the PDB is done.
  memset(BuildId->PDB70.Signature, 0, 16);
  BuildId->PDB70.Age = 1;

  // Create streams in MSF for predefined streams, namely
  // PDB, TPI, DBI and IPI.
  for (int I = 0; I < (int)pdb::kSpecialStreamCount; ++I)
    ExitOnErr(Builder.getMsfBuilder().addStream(0));

  // Add an Info stream.
  auto &InfoBuilder = Builder.getInfoBuilder();
  InfoBuilder.setVersion(pdb::PdbRaw_ImplVer::PdbImplVC70);
  InfoBuilder.setHashPDBContentsToGUID(true);

  // Add an empty DBI stream.
  pdb::DbiStreamBuilder &DbiBuilder = Builder.getDbiBuilder();
  DbiBuilder.setAge(BuildId->PDB70.Age);
  DbiBuilder.setVersionHeader(pdb::PdbDbiV70);
  DbiBuilder.setMachineType(Config->Machine);
  // Technically we are not link.exe 14.11, but there are known cases where
  // debugging tools on Windows expect Microsoft-specific version numbers or
  // they fail to work at all.  Since we know we produce PDBs that are
  // compatible with LINK 14.11, we set that version number here.
  DbiBuilder.setBuildNumber(14, 11);
}

void PDBLinker::addSections(ArrayRef<OutputSection *> OutputSections,
                            ArrayRef<uint8_t> SectionTable) {
  // It's not entirely clear what this is, but the * Linker * module uses it.
  pdb::DbiStreamBuilder &DbiBuilder = Builder.getDbiBuilder();
  NativePath = Config->PDBPath;
  pdbMakeAbsolute(NativePath);
  uint32_t PdbFilePathNI = DbiBuilder.addECName(NativePath);
  auto &LinkerModule = ExitOnErr(DbiBuilder.addModuleInfo("* Linker *"));
  LinkerModule.setPdbFilePathNI(PdbFilePathNI);
  addCommonLinkerModuleSymbols(NativePath, LinkerModule, Alloc);

  // Add section contributions. They must be ordered by ascending RVA.
  for (OutputSection *OS : OutputSections) {
    addLinkerModuleSectionSymbol(LinkerModule, *OS, Alloc);
    for (Chunk *C : OS->Chunks) {
      pdb::SectionContrib SC =
          createSectionContrib(C, LinkerModule.getModuleIndex());
      Builder.getDbiBuilder().addSectionContrib(SC);
    }
  }

  // Add Section Map stream.
  ArrayRef<object::coff_section> Sections = {
      (const object::coff_section *)SectionTable.data(),
      SectionTable.size() / sizeof(object::coff_section)};
  SectionMap = pdb::DbiStreamBuilder::createSectionMap(Sections);
  DbiBuilder.setSectionMap(SectionMap);

  // Add COFF section header stream.
  ExitOnErr(
      DbiBuilder.addDbgStream(pdb::DbgHeaderType::SectionHdr, SectionTable));
}

void PDBLinker::commit(codeview::GUID *Guid) {
  // Write to a file.
  ExitOnErr(Builder.commit(Config->PDBPath, Guid));
}

static Expected<StringRef>
getFileName(const DebugStringTableSubsectionRef &Strings,
            const DebugChecksumsSubsectionRef &Checksums, uint32_t FileID) {
  auto Iter = Checksums.getArray().at(FileID);
  if (Iter == Checksums.getArray().end())
    return make_error<CodeViewError>(cv_error_code::no_records);
  uint32_t Offset = Iter->FileNameOffset;
  return Strings.getString(Offset);
}

static uint32_t getSecrelReloc() {
  switch (Config->Machine) {
  case AMD64:
    return COFF::IMAGE_REL_AMD64_SECREL;
  case I386:
    return COFF::IMAGE_REL_I386_SECREL;
  case ARMNT:
    return COFF::IMAGE_REL_ARM_SECREL;
  case ARM64:
    return COFF::IMAGE_REL_ARM64_SECREL;
  default:
    llvm_unreachable("unknown machine type");
  }
}

// Try to find a line table for the given offset Addr into the given chunk C.
// If a line table was found, the line table, the string and checksum tables
// that are used to interpret the line table, and the offset of Addr in the line
// table are stored in the output arguments. Returns whether a line table was
// found.
static bool findLineTable(const SectionChunk *C, uint32_t Addr,
                          DebugStringTableSubsectionRef &CVStrTab,
                          DebugChecksumsSubsectionRef &Checksums,
                          DebugLinesSubsectionRef &Lines,
                          uint32_t &OffsetInLinetable) {
  ExitOnError ExitOnErr;
  uint32_t SecrelReloc = getSecrelReloc();

  for (SectionChunk *DbgC : C->File->getDebugChunks()) {
    if (DbgC->getSectionName() != ".debug$S")
      continue;

    // Build a mapping of SECREL relocations in DbgC that refer to C.
    DenseMap<uint32_t, uint32_t> Secrels;
    for (const coff_relocation &R : DbgC->Relocs) {
      if (R.Type != SecrelReloc)
        continue;

      if (auto *S = dyn_cast_or_null<DefinedRegular>(
              C->File->getSymbols()[R.SymbolTableIndex]))
        if (S->getChunk() == C)
          Secrels[R.VirtualAddress] = S->getValue();
    }

    ArrayRef<uint8_t> Contents =
        consumeDebugMagic(DbgC->getContents(), ".debug$S");
    DebugSubsectionArray Subsections;
    BinaryStreamReader Reader(Contents, support::little);
    ExitOnErr(Reader.readArray(Subsections, Contents.size()));

    for (const DebugSubsectionRecord &SS : Subsections) {
      switch (SS.kind()) {
      case DebugSubsectionKind::StringTable: {
        assert(!CVStrTab.valid() &&
               "Encountered multiple string table subsections!");
        ExitOnErr(CVStrTab.initialize(SS.getRecordData()));
        break;
      }
      case DebugSubsectionKind::FileChecksums:
        assert(!Checksums.valid() &&
               "Encountered multiple checksum subsections!");
        ExitOnErr(Checksums.initialize(SS.getRecordData()));
        break;
      case DebugSubsectionKind::Lines: {
        ArrayRef<uint8_t> Bytes;
        auto Ref = SS.getRecordData();
        ExitOnErr(Ref.readLongestContiguousChunk(0, Bytes));
        size_t OffsetInDbgC = Bytes.data() - DbgC->getContents().data();

        // Check whether this line table refers to C.
        auto I = Secrels.find(OffsetInDbgC);
        if (I == Secrels.end())
          break;

        // Check whether this line table covers Addr in C.
        DebugLinesSubsectionRef LinesTmp;
        ExitOnErr(LinesTmp.initialize(BinaryStreamReader(Ref)));
        uint32_t OffsetInC = I->second + LinesTmp.header()->RelocOffset;
        if (Addr < OffsetInC || Addr >= OffsetInC + LinesTmp.header()->CodeSize)
          break;

        assert(!Lines.header() &&
               "Encountered multiple line tables for function!");
        ExitOnErr(Lines.initialize(BinaryStreamReader(Ref)));
        OffsetInLinetable = Addr - OffsetInC;
        break;
      }
      default:
        break;
      }

      if (CVStrTab.valid() && Checksums.valid() && Lines.header())
        return true;
    }
  }

  return false;
}

// Use CodeView line tables to resolve a file and line number for the given
// offset into the given chunk and return them, or {"", 0} if a line table was
// not found.
std::pair<StringRef, uint32_t> coff::getFileLine(const SectionChunk *C,
                                                 uint32_t Addr) {
  ExitOnError ExitOnErr;

  DebugStringTableSubsectionRef CVStrTab;
  DebugChecksumsSubsectionRef Checksums;
  DebugLinesSubsectionRef Lines;
  uint32_t OffsetInLinetable;

  if (!findLineTable(C, Addr, CVStrTab, Checksums, Lines, OffsetInLinetable))
    return {"", 0};

  Optional<uint32_t> NameIndex;
  Optional<uint32_t> LineNumber;
  for (LineColumnEntry &Entry : Lines) {
    for (const LineNumberEntry &LN : Entry.LineNumbers) {
      LineInfo LI(LN.Flags);
      if (LN.Offset > OffsetInLinetable) {
        if (!NameIndex) {
          NameIndex = Entry.NameIndex;
          LineNumber = LI.getStartLine();
        }
        StringRef Filename =
            ExitOnErr(getFileName(CVStrTab, Checksums, *NameIndex));
        return {Filename, *LineNumber};
      }
      NameIndex = Entry.NameIndex;
      LineNumber = LI.getStartLine();
    }
  }
  if (!NameIndex)
    return {"", 0};
  StringRef Filename = ExitOnErr(getFileName(CVStrTab, Checksums, *NameIndex));
  return {Filename, *LineNumber};
}
