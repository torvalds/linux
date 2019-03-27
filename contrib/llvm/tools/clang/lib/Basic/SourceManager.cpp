//===- SourceManager.cpp - Track and cache source files -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the SourceManager interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManagerInternals.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Capacity.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace SrcMgr;
using llvm::MemoryBuffer;

//===----------------------------------------------------------------------===//
// SourceManager Helper Classes
//===----------------------------------------------------------------------===//

ContentCache::~ContentCache() {
  if (shouldFreeBuffer())
    delete Buffer.getPointer();
}

/// getSizeBytesMapped - Returns the number of bytes actually mapped for this
/// ContentCache. This can be 0 if the MemBuffer was not actually expanded.
unsigned ContentCache::getSizeBytesMapped() const {
  return Buffer.getPointer() ? Buffer.getPointer()->getBufferSize() : 0;
}

/// Returns the kind of memory used to back the memory buffer for
/// this content cache.  This is used for performance analysis.
llvm::MemoryBuffer::BufferKind ContentCache::getMemoryBufferKind() const {
  assert(Buffer.getPointer());

  // Should be unreachable, but keep for sanity.
  if (!Buffer.getPointer())
    return llvm::MemoryBuffer::MemoryBuffer_Malloc;

  llvm::MemoryBuffer *buf = Buffer.getPointer();
  return buf->getBufferKind();
}

/// getSize - Returns the size of the content encapsulated by this ContentCache.
///  This can be the size of the source file or the size of an arbitrary
///  scratch buffer.  If the ContentCache encapsulates a source file, that
///  file is not lazily brought in from disk to satisfy this query.
unsigned ContentCache::getSize() const {
  return Buffer.getPointer() ? (unsigned) Buffer.getPointer()->getBufferSize()
                             : (unsigned) ContentsEntry->getSize();
}

void ContentCache::replaceBuffer(llvm::MemoryBuffer *B, bool DoNotFree) {
  if (B && B == Buffer.getPointer()) {
    assert(0 && "Replacing with the same buffer");
    Buffer.setInt(DoNotFree? DoNotFreeFlag : 0);
    return;
  }

  if (shouldFreeBuffer())
    delete Buffer.getPointer();
  Buffer.setPointer(B);
  Buffer.setInt((B && DoNotFree) ? DoNotFreeFlag : 0);
}

llvm::MemoryBuffer *ContentCache::getBuffer(DiagnosticsEngine &Diag,
                                            const SourceManager &SM,
                                            SourceLocation Loc,
                                            bool *Invalid) const {
  // Lazily create the Buffer for ContentCaches that wrap files.  If we already
  // computed it, just return what we have.
  if (Buffer.getPointer() || !ContentsEntry) {
    if (Invalid)
      *Invalid = isBufferInvalid();

    return Buffer.getPointer();
  }

  bool isVolatile = SM.userFilesAreVolatile() && !IsSystemFile;
  auto BufferOrError =
      SM.getFileManager().getBufferForFile(ContentsEntry, isVolatile);

  // If we were unable to open the file, then we are in an inconsistent
  // situation where the content cache referenced a file which no longer
  // exists. Most likely, we were using a stat cache with an invalid entry but
  // the file could also have been removed during processing. Since we can't
  // really deal with this situation, just create an empty buffer.
  //
  // FIXME: This is definitely not ideal, but our immediate clients can't
  // currently handle returning a null entry here. Ideally we should detect
  // that we are in an inconsistent situation and error out as quickly as
  // possible.
  if (!BufferOrError) {
    StringRef FillStr("<<<MISSING SOURCE FILE>>>\n");
    auto BackupBuffer = llvm::WritableMemoryBuffer::getNewUninitMemBuffer(
        ContentsEntry->getSize(), "<invalid>");
    char *Ptr = BackupBuffer->getBufferStart();
    for (unsigned i = 0, e = ContentsEntry->getSize(); i != e; ++i)
      Ptr[i] = FillStr[i % FillStr.size()];
    Buffer.setPointer(BackupBuffer.release());

    if (Diag.isDiagnosticInFlight())
      Diag.SetDelayedDiagnostic(diag::err_cannot_open_file,
                                ContentsEntry->getName(),
                                BufferOrError.getError().message());
    else
      Diag.Report(Loc, diag::err_cannot_open_file)
          << ContentsEntry->getName() << BufferOrError.getError().message();

    Buffer.setInt(Buffer.getInt() | InvalidFlag);

    if (Invalid) *Invalid = true;
    return Buffer.getPointer();
  }

  Buffer.setPointer(BufferOrError->release());

  // Check that the file's size is the same as in the file entry (which may
  // have come from a stat cache).
  if (getRawBuffer()->getBufferSize() != (size_t)ContentsEntry->getSize()) {
    if (Diag.isDiagnosticInFlight())
      Diag.SetDelayedDiagnostic(diag::err_file_modified,
                                ContentsEntry->getName());
    else
      Diag.Report(Loc, diag::err_file_modified)
        << ContentsEntry->getName();

    Buffer.setInt(Buffer.getInt() | InvalidFlag);
    if (Invalid) *Invalid = true;
    return Buffer.getPointer();
  }

  // If the buffer is valid, check to see if it has a UTF Byte Order Mark
  // (BOM).  We only support UTF-8 with and without a BOM right now.  See
  // http://en.wikipedia.org/wiki/Byte_order_mark for more information.
  StringRef BufStr = Buffer.getPointer()->getBuffer();
  const char *InvalidBOM = llvm::StringSwitch<const char *>(BufStr)
    .StartsWith("\xFE\xFF", "UTF-16 (BE)")
    .StartsWith("\xFF\xFE", "UTF-16 (LE)")
    .StartsWith(llvm::StringLiteral::withInnerNUL("\x00\x00\xFE\xFF"),
                                                  "UTF-32 (BE)")
    .StartsWith(llvm::StringLiteral::withInnerNUL("\xFF\xFE\x00\x00"),
                                                  "UTF-32 (LE)")
    .StartsWith("\x2B\x2F\x76", "UTF-7")
    .StartsWith("\xF7\x64\x4C", "UTF-1")
    .StartsWith("\xDD\x73\x66\x73", "UTF-EBCDIC")
    .StartsWith("\x0E\xFE\xFF", "SDSU")
    .StartsWith("\xFB\xEE\x28", "BOCU-1")
    .StartsWith("\x84\x31\x95\x33", "GB-18030")
    .Default(nullptr);

  if (InvalidBOM) {
    Diag.Report(Loc, diag::err_unsupported_bom)
      << InvalidBOM << ContentsEntry->getName();
    Buffer.setInt(Buffer.getInt() | InvalidFlag);
  }

  if (Invalid)
    *Invalid = isBufferInvalid();

  return Buffer.getPointer();
}

unsigned LineTableInfo::getLineTableFilenameID(StringRef Name) {
  auto IterBool = FilenameIDs.try_emplace(Name, FilenamesByID.size());
  if (IterBool.second)
    FilenamesByID.push_back(&*IterBool.first);
  return IterBool.first->second;
}

/// Add a line note to the line table that indicates that there is a \#line or
/// GNU line marker at the specified FID/Offset location which changes the
/// presumed location to LineNo/FilenameID. If EntryExit is 0, then this doesn't
/// change the presumed \#include stack.  If it is 1, this is a file entry, if
/// it is 2 then this is a file exit. FileKind specifies whether this is a
/// system header or extern C system header.
void LineTableInfo::AddLineNote(FileID FID, unsigned Offset, unsigned LineNo,
                                int FilenameID, unsigned EntryExit,
                                SrcMgr::CharacteristicKind FileKind) {
  std::vector<LineEntry> &Entries = LineEntries[FID];

  // An unspecified FilenameID means use the last filename if available, or the
  // main source file otherwise.
  if (FilenameID == -1 && !Entries.empty())
    FilenameID = Entries.back().FilenameID;

  assert((Entries.empty() || Entries.back().FileOffset < Offset) &&
         "Adding line entries out of order!");

  unsigned IncludeOffset = 0;
  if (EntryExit == 0) {  // No #include stack change.
    IncludeOffset = Entries.empty() ? 0 : Entries.back().IncludeOffset;
  } else if (EntryExit == 1) {
    IncludeOffset = Offset-1;
  } else if (EntryExit == 2) {
    assert(!Entries.empty() && Entries.back().IncludeOffset &&
       "PPDirectives should have caught case when popping empty include stack");

    // Get the include loc of the last entries' include loc as our include loc.
    IncludeOffset = 0;
    if (const LineEntry *PrevEntry =
          FindNearestLineEntry(FID, Entries.back().IncludeOffset))
      IncludeOffset = PrevEntry->IncludeOffset;
  }

  Entries.push_back(LineEntry::get(Offset, LineNo, FilenameID, FileKind,
                                   IncludeOffset));
}

/// FindNearestLineEntry - Find the line entry nearest to FID that is before
/// it.  If there is no line entry before Offset in FID, return null.
const LineEntry *LineTableInfo::FindNearestLineEntry(FileID FID,
                                                     unsigned Offset) {
  const std::vector<LineEntry> &Entries = LineEntries[FID];
  assert(!Entries.empty() && "No #line entries for this FID after all!");

  // It is very common for the query to be after the last #line, check this
  // first.
  if (Entries.back().FileOffset <= Offset)
    return &Entries.back();

  // Do a binary search to find the maximal element that is still before Offset.
  std::vector<LineEntry>::const_iterator I =
    std::upper_bound(Entries.begin(), Entries.end(), Offset);
  if (I == Entries.begin()) return nullptr;
  return &*--I;
}

/// Add a new line entry that has already been encoded into
/// the internal representation of the line table.
void LineTableInfo::AddEntry(FileID FID,
                             const std::vector<LineEntry> &Entries) {
  LineEntries[FID] = Entries;
}

/// getLineTableFilenameID - Return the uniqued ID for the specified filename.
unsigned SourceManager::getLineTableFilenameID(StringRef Name) {
  return getLineTable().getLineTableFilenameID(Name);
}

/// AddLineNote - Add a line note to the line table for the FileID and offset
/// specified by Loc.  If FilenameID is -1, it is considered to be
/// unspecified.
void SourceManager::AddLineNote(SourceLocation Loc, unsigned LineNo,
                                int FilenameID, bool IsFileEntry,
                                bool IsFileExit,
                                SrcMgr::CharacteristicKind FileKind) {
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);

  bool Invalid = false;
  const SLocEntry &Entry = getSLocEntry(LocInfo.first, &Invalid);
  if (!Entry.isFile() || Invalid)
    return;

  const SrcMgr::FileInfo &FileInfo = Entry.getFile();

  // Remember that this file has #line directives now if it doesn't already.
  const_cast<SrcMgr::FileInfo&>(FileInfo).setHasLineDirectives();

  (void) getLineTable();

  unsigned EntryExit = 0;
  if (IsFileEntry)
    EntryExit = 1;
  else if (IsFileExit)
    EntryExit = 2;

  LineTable->AddLineNote(LocInfo.first, LocInfo.second, LineNo, FilenameID,
                         EntryExit, FileKind);
}

LineTableInfo &SourceManager::getLineTable() {
  if (!LineTable)
    LineTable = new LineTableInfo();
  return *LineTable;
}

//===----------------------------------------------------------------------===//
// Private 'Create' methods.
//===----------------------------------------------------------------------===//

SourceManager::SourceManager(DiagnosticsEngine &Diag, FileManager &FileMgr,
                             bool UserFilesAreVolatile)
  : Diag(Diag), FileMgr(FileMgr), UserFilesAreVolatile(UserFilesAreVolatile) {
  clearIDTables();
  Diag.setSourceManager(this);
}

SourceManager::~SourceManager() {
  delete LineTable;

  // Delete FileEntry objects corresponding to content caches.  Since the actual
  // content cache objects are bump pointer allocated, we just have to run the
  // dtors, but we call the deallocate method for completeness.
  for (unsigned i = 0, e = MemBufferInfos.size(); i != e; ++i) {
    if (MemBufferInfos[i]) {
      MemBufferInfos[i]->~ContentCache();
      ContentCacheAlloc.Deallocate(MemBufferInfos[i]);
    }
  }
  for (llvm::DenseMap<const FileEntry*, SrcMgr::ContentCache*>::iterator
       I = FileInfos.begin(), E = FileInfos.end(); I != E; ++I) {
    if (I->second) {
      I->second->~ContentCache();
      ContentCacheAlloc.Deallocate(I->second);
    }
  }
}

void SourceManager::clearIDTables() {
  MainFileID = FileID();
  LocalSLocEntryTable.clear();
  LoadedSLocEntryTable.clear();
  SLocEntryLoaded.clear();
  LastLineNoFileIDQuery = FileID();
  LastLineNoContentCache = nullptr;
  LastFileIDLookup = FileID();

  if (LineTable)
    LineTable->clear();

  // Use up FileID #0 as an invalid expansion.
  NextLocalOffset = 0;
  CurrentLoadedOffset = MaxLoadedOffset;
  createExpansionLoc(SourceLocation(), SourceLocation(), SourceLocation(), 1);
}

void SourceManager::initializeForReplay(const SourceManager &Old) {
  assert(MainFileID.isInvalid() && "expected uninitialized SourceManager");

  auto CloneContentCache = [&](const ContentCache *Cache) -> ContentCache * {
    auto *Clone = new (ContentCacheAlloc.Allocate<ContentCache>()) ContentCache;
    Clone->OrigEntry = Cache->OrigEntry;
    Clone->ContentsEntry = Cache->ContentsEntry;
    Clone->BufferOverridden = Cache->BufferOverridden;
    Clone->IsSystemFile = Cache->IsSystemFile;
    Clone->IsTransient = Cache->IsTransient;
    Clone->replaceBuffer(Cache->getRawBuffer(), /*DoNotFree*/true);
    return Clone;
  };

  // Ensure all SLocEntries are loaded from the external source.
  for (unsigned I = 0, N = Old.LoadedSLocEntryTable.size(); I != N; ++I)
    if (!Old.SLocEntryLoaded[I])
      Old.loadSLocEntry(I, nullptr);

  // Inherit any content cache data from the old source manager.
  for (auto &FileInfo : Old.FileInfos) {
    SrcMgr::ContentCache *&Slot = FileInfos[FileInfo.first];
    if (Slot)
      continue;
    Slot = CloneContentCache(FileInfo.second);
  }
}

/// getOrCreateContentCache - Create or return a cached ContentCache for the
/// specified file.
const ContentCache *
SourceManager::getOrCreateContentCache(const FileEntry *FileEnt,
                                       bool isSystemFile) {
  assert(FileEnt && "Didn't specify a file entry to use?");

  // Do we already have information about this file?
  ContentCache *&Entry = FileInfos[FileEnt];
  if (Entry) return Entry;

  // Nope, create a new Cache entry.
  Entry = ContentCacheAlloc.Allocate<ContentCache>();

  if (OverriddenFilesInfo) {
    // If the file contents are overridden with contents from another file,
    // pass that file to ContentCache.
    llvm::DenseMap<const FileEntry *, const FileEntry *>::iterator
        overI = OverriddenFilesInfo->OverriddenFiles.find(FileEnt);
    if (overI == OverriddenFilesInfo->OverriddenFiles.end())
      new (Entry) ContentCache(FileEnt);
    else
      new (Entry) ContentCache(OverridenFilesKeepOriginalName ? FileEnt
                                                              : overI->second,
                               overI->second);
  } else {
    new (Entry) ContentCache(FileEnt);
  }

  Entry->IsSystemFile = isSystemFile;
  Entry->IsTransient = FilesAreTransient;

  return Entry;
}

/// Create a new ContentCache for the specified memory buffer.
/// This does no caching.
const ContentCache *
SourceManager::createMemBufferContentCache(llvm::MemoryBuffer *Buffer,
                                           bool DoNotFree) {
  // Add a new ContentCache to the MemBufferInfos list and return it.
  ContentCache *Entry = ContentCacheAlloc.Allocate<ContentCache>();
  new (Entry) ContentCache();
  MemBufferInfos.push_back(Entry);
  Entry->replaceBuffer(Buffer, DoNotFree);
  return Entry;
}

const SrcMgr::SLocEntry &SourceManager::loadSLocEntry(unsigned Index,
                                                      bool *Invalid) const {
  assert(!SLocEntryLoaded[Index]);
  if (ExternalSLocEntries->ReadSLocEntry(-(static_cast<int>(Index) + 2))) {
    if (Invalid)
      *Invalid = true;
    // If the file of the SLocEntry changed we could still have loaded it.
    if (!SLocEntryLoaded[Index]) {
      // Try to recover; create a SLocEntry so the rest of clang can handle it.
      LoadedSLocEntryTable[Index] = SLocEntry::get(0,
                                 FileInfo::get(SourceLocation(),
                                               getFakeContentCacheForRecovery(),
                                               SrcMgr::C_User));
    }
  }

  return LoadedSLocEntryTable[Index];
}

std::pair<int, unsigned>
SourceManager::AllocateLoadedSLocEntries(unsigned NumSLocEntries,
                                         unsigned TotalSize) {
  assert(ExternalSLocEntries && "Don't have an external sloc source");
  // Make sure we're not about to run out of source locations.
  if (CurrentLoadedOffset - TotalSize < NextLocalOffset)
    return std::make_pair(0, 0);
  LoadedSLocEntryTable.resize(LoadedSLocEntryTable.size() + NumSLocEntries);
  SLocEntryLoaded.resize(LoadedSLocEntryTable.size());
  CurrentLoadedOffset -= TotalSize;
  int ID = LoadedSLocEntryTable.size();
  return std::make_pair(-ID - 1, CurrentLoadedOffset);
}

/// As part of recovering from missing or changed content, produce a
/// fake, non-empty buffer.
llvm::MemoryBuffer *SourceManager::getFakeBufferForRecovery() const {
  if (!FakeBufferForRecovery)
    FakeBufferForRecovery =
        llvm::MemoryBuffer::getMemBuffer("<<<INVALID BUFFER>>");

  return FakeBufferForRecovery.get();
}

/// As part of recovering from missing or changed content, produce a
/// fake content cache.
const SrcMgr::ContentCache *
SourceManager::getFakeContentCacheForRecovery() const {
  if (!FakeContentCacheForRecovery) {
    FakeContentCacheForRecovery = llvm::make_unique<SrcMgr::ContentCache>();
    FakeContentCacheForRecovery->replaceBuffer(getFakeBufferForRecovery(),
                                               /*DoNotFree=*/true);
  }
  return FakeContentCacheForRecovery.get();
}

/// Returns the previous in-order FileID or an invalid FileID if there
/// is no previous one.
FileID SourceManager::getPreviousFileID(FileID FID) const {
  if (FID.isInvalid())
    return FileID();

  int ID = FID.ID;
  if (ID == -1)
    return FileID();

  if (ID > 0) {
    if (ID-1 == 0)
      return FileID();
  } else if (unsigned(-(ID-1) - 2) >= LoadedSLocEntryTable.size()) {
    return FileID();
  }

  return FileID::get(ID-1);
}

/// Returns the next in-order FileID or an invalid FileID if there is
/// no next one.
FileID SourceManager::getNextFileID(FileID FID) const {
  if (FID.isInvalid())
    return FileID();

  int ID = FID.ID;
  if (ID > 0) {
    if (unsigned(ID+1) >= local_sloc_entry_size())
      return FileID();
  } else if (ID+1 >= -1) {
    return FileID();
  }

  return FileID::get(ID+1);
}

//===----------------------------------------------------------------------===//
// Methods to create new FileID's and macro expansions.
//===----------------------------------------------------------------------===//

/// createFileID - Create a new FileID for the specified ContentCache and
/// include position.  This works regardless of whether the ContentCache
/// corresponds to a file or some other input source.
FileID SourceManager::createFileID(const ContentCache *File,
                                   SourceLocation IncludePos,
                                   SrcMgr::CharacteristicKind FileCharacter,
                                   int LoadedID, unsigned LoadedOffset) {
  if (LoadedID < 0) {
    assert(LoadedID != -1 && "Loading sentinel FileID");
    unsigned Index = unsigned(-LoadedID) - 2;
    assert(Index < LoadedSLocEntryTable.size() && "FileID out of range");
    assert(!SLocEntryLoaded[Index] && "FileID already loaded");
    LoadedSLocEntryTable[Index] = SLocEntry::get(LoadedOffset,
        FileInfo::get(IncludePos, File, FileCharacter));
    SLocEntryLoaded[Index] = true;
    return FileID::get(LoadedID);
  }
  LocalSLocEntryTable.push_back(SLocEntry::get(NextLocalOffset,
                                               FileInfo::get(IncludePos, File,
                                                             FileCharacter)));
  unsigned FileSize = File->getSize();
  assert(NextLocalOffset + FileSize + 1 > NextLocalOffset &&
         NextLocalOffset + FileSize + 1 <= CurrentLoadedOffset &&
         "Ran out of source locations!");
  // We do a +1 here because we want a SourceLocation that means "the end of the
  // file", e.g. for the "no newline at the end of the file" diagnostic.
  NextLocalOffset += FileSize + 1;

  // Set LastFileIDLookup to the newly created file.  The next getFileID call is
  // almost guaranteed to be from that file.
  FileID FID = FileID::get(LocalSLocEntryTable.size()-1);
  return LastFileIDLookup = FID;
}

SourceLocation
SourceManager::createMacroArgExpansionLoc(SourceLocation SpellingLoc,
                                          SourceLocation ExpansionLoc,
                                          unsigned TokLength) {
  ExpansionInfo Info = ExpansionInfo::createForMacroArg(SpellingLoc,
                                                        ExpansionLoc);
  return createExpansionLocImpl(Info, TokLength);
}

SourceLocation
SourceManager::createExpansionLoc(SourceLocation SpellingLoc,
                                  SourceLocation ExpansionLocStart,
                                  SourceLocation ExpansionLocEnd,
                                  unsigned TokLength,
                                  bool ExpansionIsTokenRange,
                                  int LoadedID,
                                  unsigned LoadedOffset) {
  ExpansionInfo Info = ExpansionInfo::create(
      SpellingLoc, ExpansionLocStart, ExpansionLocEnd, ExpansionIsTokenRange);
  return createExpansionLocImpl(Info, TokLength, LoadedID, LoadedOffset);
}

SourceLocation SourceManager::createTokenSplitLoc(SourceLocation Spelling,
                                                  SourceLocation TokenStart,
                                                  SourceLocation TokenEnd) {
  assert(getFileID(TokenStart) == getFileID(TokenEnd) &&
         "token spans multiple files");
  return createExpansionLocImpl(
      ExpansionInfo::createForTokenSplit(Spelling, TokenStart, TokenEnd),
      TokenEnd.getOffset() - TokenStart.getOffset());
}

SourceLocation
SourceManager::createExpansionLocImpl(const ExpansionInfo &Info,
                                      unsigned TokLength,
                                      int LoadedID,
                                      unsigned LoadedOffset) {
  if (LoadedID < 0) {
    assert(LoadedID != -1 && "Loading sentinel FileID");
    unsigned Index = unsigned(-LoadedID) - 2;
    assert(Index < LoadedSLocEntryTable.size() && "FileID out of range");
    assert(!SLocEntryLoaded[Index] && "FileID already loaded");
    LoadedSLocEntryTable[Index] = SLocEntry::get(LoadedOffset, Info);
    SLocEntryLoaded[Index] = true;
    return SourceLocation::getMacroLoc(LoadedOffset);
  }
  LocalSLocEntryTable.push_back(SLocEntry::get(NextLocalOffset, Info));
  assert(NextLocalOffset + TokLength + 1 > NextLocalOffset &&
         NextLocalOffset + TokLength + 1 <= CurrentLoadedOffset &&
         "Ran out of source locations!");
  // See createFileID for that +1.
  NextLocalOffset += TokLength + 1;
  return SourceLocation::getMacroLoc(NextLocalOffset - (TokLength + 1));
}

llvm::MemoryBuffer *SourceManager::getMemoryBufferForFile(const FileEntry *File,
                                                          bool *Invalid) {
  const SrcMgr::ContentCache *IR = getOrCreateContentCache(File);
  assert(IR && "getOrCreateContentCache() cannot return NULL");
  return IR->getBuffer(Diag, *this, SourceLocation(), Invalid);
}

void SourceManager::overrideFileContents(const FileEntry *SourceFile,
                                         llvm::MemoryBuffer *Buffer,
                                         bool DoNotFree) {
  const SrcMgr::ContentCache *IR = getOrCreateContentCache(SourceFile);
  assert(IR && "getOrCreateContentCache() cannot return NULL");

  const_cast<SrcMgr::ContentCache *>(IR)->replaceBuffer(Buffer, DoNotFree);
  const_cast<SrcMgr::ContentCache *>(IR)->BufferOverridden = true;

  getOverriddenFilesInfo().OverriddenFilesWithBuffer.insert(SourceFile);
}

void SourceManager::overrideFileContents(const FileEntry *SourceFile,
                                         const FileEntry *NewFile) {
  assert(SourceFile->getSize() == NewFile->getSize() &&
         "Different sizes, use the FileManager to create a virtual file with "
         "the correct size");
  assert(FileInfos.count(SourceFile) == 0 &&
         "This function should be called at the initialization stage, before "
         "any parsing occurs.");
  getOverriddenFilesInfo().OverriddenFiles[SourceFile] = NewFile;
}

void SourceManager::disableFileContentsOverride(const FileEntry *File) {
  if (!isFileOverridden(File))
    return;

  const SrcMgr::ContentCache *IR = getOrCreateContentCache(File);
  const_cast<SrcMgr::ContentCache *>(IR)->replaceBuffer(nullptr);
  const_cast<SrcMgr::ContentCache *>(IR)->ContentsEntry = IR->OrigEntry;

  assert(OverriddenFilesInfo);
  OverriddenFilesInfo->OverriddenFiles.erase(File);
  OverriddenFilesInfo->OverriddenFilesWithBuffer.erase(File);
}

void SourceManager::setFileIsTransient(const FileEntry *File) {
  const SrcMgr::ContentCache *CC = getOrCreateContentCache(File);
  const_cast<SrcMgr::ContentCache *>(CC)->IsTransient = true;
}

StringRef SourceManager::getBufferData(FileID FID, bool *Invalid) const {
  bool MyInvalid = false;
  const SLocEntry &SLoc = getSLocEntry(FID, &MyInvalid);
  if (!SLoc.isFile() || MyInvalid) {
    if (Invalid)
      *Invalid = true;
    return "<<<<<INVALID SOURCE LOCATION>>>>>";
  }

  llvm::MemoryBuffer *Buf = SLoc.getFile().getContentCache()->getBuffer(
      Diag, *this, SourceLocation(), &MyInvalid);
  if (Invalid)
    *Invalid = MyInvalid;

  if (MyInvalid)
    return "<<<<<INVALID SOURCE LOCATION>>>>>";

  return Buf->getBuffer();
}

//===----------------------------------------------------------------------===//
// SourceLocation manipulation methods.
//===----------------------------------------------------------------------===//

/// Return the FileID for a SourceLocation.
///
/// This is the cache-miss path of getFileID. Not as hot as that function, but
/// still very important. It is responsible for finding the entry in the
/// SLocEntry tables that contains the specified location.
FileID SourceManager::getFileIDSlow(unsigned SLocOffset) const {
  if (!SLocOffset)
    return FileID::get(0);

  // Now it is time to search for the correct file. See where the SLocOffset
  // sits in the global view and consult local or loaded buffers for it.
  if (SLocOffset < NextLocalOffset)
    return getFileIDLocal(SLocOffset);
  return getFileIDLoaded(SLocOffset);
}

/// Return the FileID for a SourceLocation with a low offset.
///
/// This function knows that the SourceLocation is in a local buffer, not a
/// loaded one.
FileID SourceManager::getFileIDLocal(unsigned SLocOffset) const {
  assert(SLocOffset < NextLocalOffset && "Bad function choice");

  // After the first and second level caches, I see two common sorts of
  // behavior: 1) a lot of searched FileID's are "near" the cached file
  // location or are "near" the cached expansion location. 2) others are just
  // completely random and may be a very long way away.
  //
  // To handle this, we do a linear search for up to 8 steps to catch #1 quickly
  // then we fall back to a less cache efficient, but more scalable, binary
  // search to find the location.

  // See if this is near the file point - worst case we start scanning from the
  // most newly created FileID.
  const SrcMgr::SLocEntry *I;

  if (LastFileIDLookup.ID < 0 ||
      LocalSLocEntryTable[LastFileIDLookup.ID].getOffset() < SLocOffset) {
    // Neither loc prunes our search.
    I = LocalSLocEntryTable.end();
  } else {
    // Perhaps it is near the file point.
    I = LocalSLocEntryTable.begin()+LastFileIDLookup.ID;
  }

  // Find the FileID that contains this.  "I" is an iterator that points to a
  // FileID whose offset is known to be larger than SLocOffset.
  unsigned NumProbes = 0;
  while (true) {
    --I;
    if (I->getOffset() <= SLocOffset) {
      FileID Res = FileID::get(int(I - LocalSLocEntryTable.begin()));

      // If this isn't an expansion, remember it.  We have good locality across
      // FileID lookups.
      if (!I->isExpansion())
        LastFileIDLookup = Res;
      NumLinearScans += NumProbes+1;
      return Res;
    }
    if (++NumProbes == 8)
      break;
  }

  // Convert "I" back into an index.  We know that it is an entry whose index is
  // larger than the offset we are looking for.
  unsigned GreaterIndex = I - LocalSLocEntryTable.begin();
  // LessIndex - This is the lower bound of the range that we're searching.
  // We know that the offset corresponding to the FileID is is less than
  // SLocOffset.
  unsigned LessIndex = 0;
  NumProbes = 0;
  while (true) {
    bool Invalid = false;
    unsigned MiddleIndex = (GreaterIndex-LessIndex)/2+LessIndex;
    unsigned MidOffset = getLocalSLocEntry(MiddleIndex, &Invalid).getOffset();
    if (Invalid)
      return FileID::get(0);

    ++NumProbes;

    // If the offset of the midpoint is too large, chop the high side of the
    // range to the midpoint.
    if (MidOffset > SLocOffset) {
      GreaterIndex = MiddleIndex;
      continue;
    }

    // If the middle index contains the value, succeed and return.
    // FIXME: This could be made faster by using a function that's aware of
    // being in the local area.
    if (isOffsetInFileID(FileID::get(MiddleIndex), SLocOffset)) {
      FileID Res = FileID::get(MiddleIndex);

      // If this isn't a macro expansion, remember it.  We have good locality
      // across FileID lookups.
      if (!LocalSLocEntryTable[MiddleIndex].isExpansion())
        LastFileIDLookup = Res;
      NumBinaryProbes += NumProbes;
      return Res;
    }

    // Otherwise, move the low-side up to the middle index.
    LessIndex = MiddleIndex;
  }
}

/// Return the FileID for a SourceLocation with a high offset.
///
/// This function knows that the SourceLocation is in a loaded buffer, not a
/// local one.
FileID SourceManager::getFileIDLoaded(unsigned SLocOffset) const {
  // Sanity checking, otherwise a bug may lead to hanging in release build.
  if (SLocOffset < CurrentLoadedOffset) {
    assert(0 && "Invalid SLocOffset or bad function choice");
    return FileID();
  }

  // Essentially the same as the local case, but the loaded array is sorted
  // in the other direction.

  // First do a linear scan from the last lookup position, if possible.
  unsigned I;
  int LastID = LastFileIDLookup.ID;
  if (LastID >= 0 || getLoadedSLocEntryByID(LastID).getOffset() < SLocOffset)
    I = 0;
  else
    I = (-LastID - 2) + 1;

  unsigned NumProbes;
  for (NumProbes = 0; NumProbes < 8; ++NumProbes, ++I) {
    // Make sure the entry is loaded!
    const SrcMgr::SLocEntry &E = getLoadedSLocEntry(I);
    if (E.getOffset() <= SLocOffset) {
      FileID Res = FileID::get(-int(I) - 2);

      if (!E.isExpansion())
        LastFileIDLookup = Res;
      NumLinearScans += NumProbes + 1;
      return Res;
    }
  }

  // Linear scan failed. Do the binary search. Note the reverse sorting of the
  // table: GreaterIndex is the one where the offset is greater, which is
  // actually a lower index!
  unsigned GreaterIndex = I;
  unsigned LessIndex = LoadedSLocEntryTable.size();
  NumProbes = 0;
  while (true) {
    ++NumProbes;
    unsigned MiddleIndex = (LessIndex - GreaterIndex) / 2 + GreaterIndex;
    const SrcMgr::SLocEntry &E = getLoadedSLocEntry(MiddleIndex);
    if (E.getOffset() == 0)
      return FileID(); // invalid entry.

    ++NumProbes;

    if (E.getOffset() > SLocOffset) {
      // Sanity checking, otherwise a bug may lead to hanging in release build.
      if (GreaterIndex == MiddleIndex) {
        assert(0 && "binary search missed the entry");
        return FileID();
      }
      GreaterIndex = MiddleIndex;
      continue;
    }

    if (isOffsetInFileID(FileID::get(-int(MiddleIndex) - 2), SLocOffset)) {
      FileID Res = FileID::get(-int(MiddleIndex) - 2);
      if (!E.isExpansion())
        LastFileIDLookup = Res;
      NumBinaryProbes += NumProbes;
      return Res;
    }

    // Sanity checking, otherwise a bug may lead to hanging in release build.
    if (LessIndex == MiddleIndex) {
      assert(0 && "binary search missed the entry");
      return FileID();
    }
    LessIndex = MiddleIndex;
  }
}

SourceLocation SourceManager::
getExpansionLocSlowCase(SourceLocation Loc) const {
  do {
    // Note: If Loc indicates an offset into a token that came from a macro
    // expansion (e.g. the 5th character of the token) we do not want to add
    // this offset when going to the expansion location.  The expansion
    // location is the macro invocation, which the offset has nothing to do
    // with.  This is unlike when we get the spelling loc, because the offset
    // directly correspond to the token whose spelling we're inspecting.
    Loc = getSLocEntry(getFileID(Loc)).getExpansion().getExpansionLocStart();
  } while (!Loc.isFileID());

  return Loc;
}

SourceLocation SourceManager::getSpellingLocSlowCase(SourceLocation Loc) const {
  do {
    std::pair<FileID, unsigned> LocInfo = getDecomposedLoc(Loc);
    Loc = getSLocEntry(LocInfo.first).getExpansion().getSpellingLoc();
    Loc = Loc.getLocWithOffset(LocInfo.second);
  } while (!Loc.isFileID());
  return Loc;
}

SourceLocation SourceManager::getFileLocSlowCase(SourceLocation Loc) const {
  do {
    if (isMacroArgExpansion(Loc))
      Loc = getImmediateSpellingLoc(Loc);
    else
      Loc = getImmediateExpansionRange(Loc).getBegin();
  } while (!Loc.isFileID());
  return Loc;
}


std::pair<FileID, unsigned>
SourceManager::getDecomposedExpansionLocSlowCase(
                                             const SrcMgr::SLocEntry *E) const {
  // If this is an expansion record, walk through all the expansion points.
  FileID FID;
  SourceLocation Loc;
  unsigned Offset;
  do {
    Loc = E->getExpansion().getExpansionLocStart();

    FID = getFileID(Loc);
    E = &getSLocEntry(FID);
    Offset = Loc.getOffset()-E->getOffset();
  } while (!Loc.isFileID());

  return std::make_pair(FID, Offset);
}

std::pair<FileID, unsigned>
SourceManager::getDecomposedSpellingLocSlowCase(const SrcMgr::SLocEntry *E,
                                                unsigned Offset) const {
  // If this is an expansion record, walk through all the expansion points.
  FileID FID;
  SourceLocation Loc;
  do {
    Loc = E->getExpansion().getSpellingLoc();
    Loc = Loc.getLocWithOffset(Offset);

    FID = getFileID(Loc);
    E = &getSLocEntry(FID);
    Offset = Loc.getOffset()-E->getOffset();
  } while (!Loc.isFileID());

  return std::make_pair(FID, Offset);
}

/// getImmediateSpellingLoc - Given a SourceLocation object, return the
/// spelling location referenced by the ID.  This is the first level down
/// towards the place where the characters that make up the lexed token can be
/// found.  This should not generally be used by clients.
SourceLocation SourceManager::getImmediateSpellingLoc(SourceLocation Loc) const{
  if (Loc.isFileID()) return Loc;
  std::pair<FileID, unsigned> LocInfo = getDecomposedLoc(Loc);
  Loc = getSLocEntry(LocInfo.first).getExpansion().getSpellingLoc();
  return Loc.getLocWithOffset(LocInfo.second);
}

/// getImmediateExpansionRange - Loc is required to be an expansion location.
/// Return the start/end of the expansion information.
CharSourceRange
SourceManager::getImmediateExpansionRange(SourceLocation Loc) const {
  assert(Loc.isMacroID() && "Not a macro expansion loc!");
  const ExpansionInfo &Expansion = getSLocEntry(getFileID(Loc)).getExpansion();
  return Expansion.getExpansionLocRange();
}

SourceLocation SourceManager::getTopMacroCallerLoc(SourceLocation Loc) const {
  while (isMacroArgExpansion(Loc))
    Loc = getImmediateSpellingLoc(Loc);
  return Loc;
}

/// getExpansionRange - Given a SourceLocation object, return the range of
/// tokens covered by the expansion in the ultimate file.
CharSourceRange SourceManager::getExpansionRange(SourceLocation Loc) const {
  if (Loc.isFileID())
    return CharSourceRange(SourceRange(Loc, Loc), true);

  CharSourceRange Res = getImmediateExpansionRange(Loc);

  // Fully resolve the start and end locations to their ultimate expansion
  // points.
  while (!Res.getBegin().isFileID())
    Res.setBegin(getImmediateExpansionRange(Res.getBegin()).getBegin());
  while (!Res.getEnd().isFileID()) {
    CharSourceRange EndRange = getImmediateExpansionRange(Res.getEnd());
    Res.setEnd(EndRange.getEnd());
    Res.setTokenRange(EndRange.isTokenRange());
  }
  return Res;
}

bool SourceManager::isMacroArgExpansion(SourceLocation Loc,
                                        SourceLocation *StartLoc) const {
  if (!Loc.isMacroID()) return false;

  FileID FID = getFileID(Loc);
  const SrcMgr::ExpansionInfo &Expansion = getSLocEntry(FID).getExpansion();
  if (!Expansion.isMacroArgExpansion()) return false;

  if (StartLoc)
    *StartLoc = Expansion.getExpansionLocStart();
  return true;
}

bool SourceManager::isMacroBodyExpansion(SourceLocation Loc) const {
  if (!Loc.isMacroID()) return false;

  FileID FID = getFileID(Loc);
  const SrcMgr::ExpansionInfo &Expansion = getSLocEntry(FID).getExpansion();
  return Expansion.isMacroBodyExpansion();
}

bool SourceManager::isAtStartOfImmediateMacroExpansion(SourceLocation Loc,
                                             SourceLocation *MacroBegin) const {
  assert(Loc.isValid() && Loc.isMacroID() && "Expected a valid macro loc");

  std::pair<FileID, unsigned> DecompLoc = getDecomposedLoc(Loc);
  if (DecompLoc.second > 0)
    return false; // Does not point at the start of expansion range.

  bool Invalid = false;
  const SrcMgr::ExpansionInfo &ExpInfo =
      getSLocEntry(DecompLoc.first, &Invalid).getExpansion();
  if (Invalid)
    return false;
  SourceLocation ExpLoc = ExpInfo.getExpansionLocStart();

  if (ExpInfo.isMacroArgExpansion()) {
    // For macro argument expansions, check if the previous FileID is part of
    // the same argument expansion, in which case this Loc is not at the
    // beginning of the expansion.
    FileID PrevFID = getPreviousFileID(DecompLoc.first);
    if (!PrevFID.isInvalid()) {
      const SrcMgr::SLocEntry &PrevEntry = getSLocEntry(PrevFID, &Invalid);
      if (Invalid)
        return false;
      if (PrevEntry.isExpansion() &&
          PrevEntry.getExpansion().getExpansionLocStart() == ExpLoc)
        return false;
    }
  }

  if (MacroBegin)
    *MacroBegin = ExpLoc;
  return true;
}

bool SourceManager::isAtEndOfImmediateMacroExpansion(SourceLocation Loc,
                                               SourceLocation *MacroEnd) const {
  assert(Loc.isValid() && Loc.isMacroID() && "Expected a valid macro loc");

  FileID FID = getFileID(Loc);
  SourceLocation NextLoc = Loc.getLocWithOffset(1);
  if (isInFileID(NextLoc, FID))
    return false; // Does not point at the end of expansion range.

  bool Invalid = false;
  const SrcMgr::ExpansionInfo &ExpInfo =
      getSLocEntry(FID, &Invalid).getExpansion();
  if (Invalid)
    return false;

  if (ExpInfo.isMacroArgExpansion()) {
    // For macro argument expansions, check if the next FileID is part of the
    // same argument expansion, in which case this Loc is not at the end of the
    // expansion.
    FileID NextFID = getNextFileID(FID);
    if (!NextFID.isInvalid()) {
      const SrcMgr::SLocEntry &NextEntry = getSLocEntry(NextFID, &Invalid);
      if (Invalid)
        return false;
      if (NextEntry.isExpansion() &&
          NextEntry.getExpansion().getExpansionLocStart() ==
              ExpInfo.getExpansionLocStart())
        return false;
    }
  }

  if (MacroEnd)
    *MacroEnd = ExpInfo.getExpansionLocEnd();
  return true;
}

//===----------------------------------------------------------------------===//
// Queries about the code at a SourceLocation.
//===----------------------------------------------------------------------===//

/// getCharacterData - Return a pointer to the start of the specified location
/// in the appropriate MemoryBuffer.
const char *SourceManager::getCharacterData(SourceLocation SL,
                                            bool *Invalid) const {
  // Note that this is a hot function in the getSpelling() path, which is
  // heavily used by -E mode.
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(SL);

  // Note that calling 'getBuffer()' may lazily page in a source file.
  bool CharDataInvalid = false;
  const SLocEntry &Entry = getSLocEntry(LocInfo.first, &CharDataInvalid);
  if (CharDataInvalid || !Entry.isFile()) {
    if (Invalid)
      *Invalid = true;

    return "<<<<INVALID BUFFER>>>>";
  }
  llvm::MemoryBuffer *Buffer = Entry.getFile().getContentCache()->getBuffer(
      Diag, *this, SourceLocation(), &CharDataInvalid);
  if (Invalid)
    *Invalid = CharDataInvalid;
  return Buffer->getBufferStart() + (CharDataInvalid? 0 : LocInfo.second);
}

/// getColumnNumber - Return the column # for the specified file position.
/// this is significantly cheaper to compute than the line number.
unsigned SourceManager::getColumnNumber(FileID FID, unsigned FilePos,
                                        bool *Invalid) const {
  bool MyInvalid = false;
  llvm::MemoryBuffer *MemBuf = getBuffer(FID, &MyInvalid);
  if (Invalid)
    *Invalid = MyInvalid;

  if (MyInvalid)
    return 1;

  // It is okay to request a position just past the end of the buffer.
  if (FilePos > MemBuf->getBufferSize()) {
    if (Invalid)
      *Invalid = true;
    return 1;
  }

  const char *Buf = MemBuf->getBufferStart();
  // See if we just calculated the line number for this FilePos and can use
  // that to lookup the start of the line instead of searching for it.
  if (LastLineNoFileIDQuery == FID &&
      LastLineNoContentCache->SourceLineCache != nullptr &&
      LastLineNoResult < LastLineNoContentCache->NumLines) {
    unsigned *SourceLineCache = LastLineNoContentCache->SourceLineCache;
    unsigned LineStart = SourceLineCache[LastLineNoResult - 1];
    unsigned LineEnd = SourceLineCache[LastLineNoResult];
    if (FilePos >= LineStart && FilePos < LineEnd) {
      // LineEnd is the LineStart of the next line.
      // A line ends with separator LF or CR+LF on Windows.
      // FilePos might point to the last separator,
      // but we need a column number at most 1 + the last column.
      if (FilePos + 1 == LineEnd && FilePos > LineStart) {
        if (Buf[FilePos - 1] == '\r' || Buf[FilePos - 1] == '\n')
          --FilePos;
      }
      return FilePos - LineStart + 1;
    }
  }

  unsigned LineStart = FilePos;
  while (LineStart && Buf[LineStart-1] != '\n' && Buf[LineStart-1] != '\r')
    --LineStart;
  return FilePos-LineStart+1;
}

// isInvalid - Return the result of calling loc.isInvalid(), and
// if Invalid is not null, set its value to same.
template<typename LocType>
static bool isInvalid(LocType Loc, bool *Invalid) {
  bool MyInvalid = Loc.isInvalid();
  if (Invalid)
    *Invalid = MyInvalid;
  return MyInvalid;
}

unsigned SourceManager::getSpellingColumnNumber(SourceLocation Loc,
                                                bool *Invalid) const {
  if (isInvalid(Loc, Invalid)) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(Loc);
  return getColumnNumber(LocInfo.first, LocInfo.second, Invalid);
}

unsigned SourceManager::getExpansionColumnNumber(SourceLocation Loc,
                                                 bool *Invalid) const {
  if (isInvalid(Loc, Invalid)) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);
  return getColumnNumber(LocInfo.first, LocInfo.second, Invalid);
}

unsigned SourceManager::getPresumedColumnNumber(SourceLocation Loc,
                                                bool *Invalid) const {
  PresumedLoc PLoc = getPresumedLoc(Loc);
  if (isInvalid(PLoc, Invalid)) return 0;
  return PLoc.getColumn();
}

#ifdef __SSE2__
#include <emmintrin.h>
#endif

static LLVM_ATTRIBUTE_NOINLINE void
ComputeLineNumbers(DiagnosticsEngine &Diag, ContentCache *FI,
                   llvm::BumpPtrAllocator &Alloc,
                   const SourceManager &SM, bool &Invalid);
static void ComputeLineNumbers(DiagnosticsEngine &Diag, ContentCache *FI,
                               llvm::BumpPtrAllocator &Alloc,
                               const SourceManager &SM, bool &Invalid) {
  // Note that calling 'getBuffer()' may lazily page in the file.
  MemoryBuffer *Buffer = FI->getBuffer(Diag, SM, SourceLocation(), &Invalid);
  if (Invalid)
    return;

  // Find the file offsets of all of the *physical* source lines.  This does
  // not look at trigraphs, escaped newlines, or anything else tricky.
  SmallVector<unsigned, 256> LineOffsets;

  // Line #1 starts at char 0.
  LineOffsets.push_back(0);

  const unsigned char *Buf = (const unsigned char *)Buffer->getBufferStart();
  const unsigned char *End = (const unsigned char *)Buffer->getBufferEnd();
  unsigned I = 0;
  while (true) {
    // Skip over the contents of the line.
    while (Buf[I] != '\n' && Buf[I] != '\r' && Buf[I] != '\0')
      ++I;

    if (Buf[I] == '\n' || Buf[I] == '\r') {
      // If this is \r\n, skip both characters.
      if (Buf[I] == '\r' && Buf[I+1] == '\n')
        ++I;
      ++I;
      LineOffsets.push_back(I);
    } else {
      // Otherwise, this is a NUL. If end of file, exit.
      if (Buf+I == End) break;
      ++I;
    }
  }

  // Copy the offsets into the FileInfo structure.
  FI->NumLines = LineOffsets.size();
  FI->SourceLineCache = Alloc.Allocate<unsigned>(LineOffsets.size());
  std::copy(LineOffsets.begin(), LineOffsets.end(), FI->SourceLineCache);
}

/// getLineNumber - Given a SourceLocation, return the spelling line number
/// for the position indicated.  This requires building and caching a table of
/// line offsets for the MemoryBuffer, so this is not cheap: use only when
/// about to emit a diagnostic.
unsigned SourceManager::getLineNumber(FileID FID, unsigned FilePos,
                                      bool *Invalid) const {
  if (FID.isInvalid()) {
    if (Invalid)
      *Invalid = true;
    return 1;
  }

  ContentCache *Content;
  if (LastLineNoFileIDQuery == FID)
    Content = LastLineNoContentCache;
  else {
    bool MyInvalid = false;
    const SLocEntry &Entry = getSLocEntry(FID, &MyInvalid);
    if (MyInvalid || !Entry.isFile()) {
      if (Invalid)
        *Invalid = true;
      return 1;
    }

    Content = const_cast<ContentCache*>(Entry.getFile().getContentCache());
  }

  // If this is the first use of line information for this buffer, compute the
  /// SourceLineCache for it on demand.
  if (!Content->SourceLineCache) {
    bool MyInvalid = false;
    ComputeLineNumbers(Diag, Content, ContentCacheAlloc, *this, MyInvalid);
    if (Invalid)
      *Invalid = MyInvalid;
    if (MyInvalid)
      return 1;
  } else if (Invalid)
    *Invalid = false;

  // Okay, we know we have a line number table.  Do a binary search to find the
  // line number that this character position lands on.
  unsigned *SourceLineCache = Content->SourceLineCache;
  unsigned *SourceLineCacheStart = SourceLineCache;
  unsigned *SourceLineCacheEnd = SourceLineCache + Content->NumLines;

  unsigned QueriedFilePos = FilePos+1;

  // FIXME: I would like to be convinced that this code is worth being as
  // complicated as it is, binary search isn't that slow.
  //
  // If it is worth being optimized, then in my opinion it could be more
  // performant, simpler, and more obviously correct by just "galloping" outward
  // from the queried file position. In fact, this could be incorporated into a
  // generic algorithm such as lower_bound_with_hint.
  //
  // If someone gives me a test case where this matters, and I will do it! - DWD

  // If the previous query was to the same file, we know both the file pos from
  // that query and the line number returned.  This allows us to narrow the
  // search space from the entire file to something near the match.
  if (LastLineNoFileIDQuery == FID) {
    if (QueriedFilePos >= LastLineNoFilePos) {
      // FIXME: Potential overflow?
      SourceLineCache = SourceLineCache+LastLineNoResult-1;

      // The query is likely to be nearby the previous one.  Here we check to
      // see if it is within 5, 10 or 20 lines.  It can be far away in cases
      // where big comment blocks and vertical whitespace eat up lines but
      // contribute no tokens.
      if (SourceLineCache+5 < SourceLineCacheEnd) {
        if (SourceLineCache[5] > QueriedFilePos)
          SourceLineCacheEnd = SourceLineCache+5;
        else if (SourceLineCache+10 < SourceLineCacheEnd) {
          if (SourceLineCache[10] > QueriedFilePos)
            SourceLineCacheEnd = SourceLineCache+10;
          else if (SourceLineCache+20 < SourceLineCacheEnd) {
            if (SourceLineCache[20] > QueriedFilePos)
              SourceLineCacheEnd = SourceLineCache+20;
          }
        }
      }
    } else {
      if (LastLineNoResult < Content->NumLines)
        SourceLineCacheEnd = SourceLineCache+LastLineNoResult+1;
    }
  }

  unsigned *Pos
    = std::lower_bound(SourceLineCache, SourceLineCacheEnd, QueriedFilePos);
  unsigned LineNo = Pos-SourceLineCacheStart;

  LastLineNoFileIDQuery = FID;
  LastLineNoContentCache = Content;
  LastLineNoFilePos = QueriedFilePos;
  LastLineNoResult = LineNo;
  return LineNo;
}

unsigned SourceManager::getSpellingLineNumber(SourceLocation Loc,
                                              bool *Invalid) const {
  if (isInvalid(Loc, Invalid)) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(Loc);
  return getLineNumber(LocInfo.first, LocInfo.second);
}
unsigned SourceManager::getExpansionLineNumber(SourceLocation Loc,
                                               bool *Invalid) const {
  if (isInvalid(Loc, Invalid)) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);
  return getLineNumber(LocInfo.first, LocInfo.second);
}
unsigned SourceManager::getPresumedLineNumber(SourceLocation Loc,
                                              bool *Invalid) const {
  PresumedLoc PLoc = getPresumedLoc(Loc);
  if (isInvalid(PLoc, Invalid)) return 0;
  return PLoc.getLine();
}

/// getFileCharacteristic - return the file characteristic of the specified
/// source location, indicating whether this is a normal file, a system
/// header, or an "implicit extern C" system header.
///
/// This state can be modified with flags on GNU linemarker directives like:
///   # 4 "foo.h" 3
/// which changes all source locations in the current file after that to be
/// considered to be from a system header.
SrcMgr::CharacteristicKind
SourceManager::getFileCharacteristic(SourceLocation Loc) const {
  assert(Loc.isValid() && "Can't get file characteristic of invalid loc!");
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);
  bool Invalid = false;
  const SLocEntry &SEntry = getSLocEntry(LocInfo.first, &Invalid);
  if (Invalid || !SEntry.isFile())
    return C_User;

  const SrcMgr::FileInfo &FI = SEntry.getFile();

  // If there are no #line directives in this file, just return the whole-file
  // state.
  if (!FI.hasLineDirectives())
    return FI.getFileCharacteristic();

  assert(LineTable && "Can't have linetable entries without a LineTable!");
  // See if there is a #line directive before the location.
  const LineEntry *Entry =
    LineTable->FindNearestLineEntry(LocInfo.first, LocInfo.second);

  // If this is before the first line marker, use the file characteristic.
  if (!Entry)
    return FI.getFileCharacteristic();

  return Entry->FileKind;
}

/// Return the filename or buffer identifier of the buffer the location is in.
/// Note that this name does not respect \#line directives.  Use getPresumedLoc
/// for normal clients.
StringRef SourceManager::getBufferName(SourceLocation Loc,
                                       bool *Invalid) const {
  if (isInvalid(Loc, Invalid)) return "<invalid loc>";

  return getBuffer(getFileID(Loc), Invalid)->getBufferIdentifier();
}

/// getPresumedLoc - This method returns the "presumed" location of a
/// SourceLocation specifies.  A "presumed location" can be modified by \#line
/// or GNU line marker directives.  This provides a view on the data that a
/// user should see in diagnostics, for example.
///
/// Note that a presumed location is always given as the expansion point of an
/// expansion location, not at the spelling location.
PresumedLoc SourceManager::getPresumedLoc(SourceLocation Loc,
                                          bool UseLineDirectives) const {
  if (Loc.isInvalid()) return PresumedLoc();

  // Presumed locations are always for expansion points.
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);

  bool Invalid = false;
  const SLocEntry &Entry = getSLocEntry(LocInfo.first, &Invalid);
  if (Invalid || !Entry.isFile())
    return PresumedLoc();

  const SrcMgr::FileInfo &FI = Entry.getFile();
  const SrcMgr::ContentCache *C = FI.getContentCache();

  // To get the source name, first consult the FileEntry (if one exists)
  // before the MemBuffer as this will avoid unnecessarily paging in the
  // MemBuffer.
  StringRef Filename;
  if (C->OrigEntry)
    Filename = C->OrigEntry->getName();
  else
    Filename = C->getBuffer(Diag, *this)->getBufferIdentifier();

  unsigned LineNo = getLineNumber(LocInfo.first, LocInfo.second, &Invalid);
  if (Invalid)
    return PresumedLoc();
  unsigned ColNo  = getColumnNumber(LocInfo.first, LocInfo.second, &Invalid);
  if (Invalid)
    return PresumedLoc();

  SourceLocation IncludeLoc = FI.getIncludeLoc();

  // If we have #line directives in this file, update and overwrite the physical
  // location info if appropriate.
  if (UseLineDirectives && FI.hasLineDirectives()) {
    assert(LineTable && "Can't have linetable entries without a LineTable!");
    // See if there is a #line directive before this.  If so, get it.
    if (const LineEntry *Entry =
          LineTable->FindNearestLineEntry(LocInfo.first, LocInfo.second)) {
      // If the LineEntry indicates a filename, use it.
      if (Entry->FilenameID != -1)
        Filename = LineTable->getFilename(Entry->FilenameID);

      // Use the line number specified by the LineEntry.  This line number may
      // be multiple lines down from the line entry.  Add the difference in
      // physical line numbers from the query point and the line marker to the
      // total.
      unsigned MarkerLineNo = getLineNumber(LocInfo.first, Entry->FileOffset);
      LineNo = Entry->LineNo + (LineNo-MarkerLineNo-1);

      // Note that column numbers are not molested by line markers.

      // Handle virtual #include manipulation.
      if (Entry->IncludeOffset) {
        IncludeLoc = getLocForStartOfFile(LocInfo.first);
        IncludeLoc = IncludeLoc.getLocWithOffset(Entry->IncludeOffset);
      }
    }
  }

  return PresumedLoc(Filename.data(), LineNo, ColNo, IncludeLoc);
}

/// Returns whether the PresumedLoc for a given SourceLocation is
/// in the main file.
///
/// This computes the "presumed" location for a SourceLocation, then checks
/// whether it came from a file other than the main file. This is different
/// from isWrittenInMainFile() because it takes line marker directives into
/// account.
bool SourceManager::isInMainFile(SourceLocation Loc) const {
  if (Loc.isInvalid()) return false;

  // Presumed locations are always for expansion points.
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);

  bool Invalid = false;
  const SLocEntry &Entry = getSLocEntry(LocInfo.first, &Invalid);
  if (Invalid || !Entry.isFile())
    return false;

  const SrcMgr::FileInfo &FI = Entry.getFile();

  // Check if there is a line directive for this location.
  if (FI.hasLineDirectives())
    if (const LineEntry *Entry =
            LineTable->FindNearestLineEntry(LocInfo.first, LocInfo.second))
      if (Entry->IncludeOffset)
        return false;

  return FI.getIncludeLoc().isInvalid();
}

/// The size of the SLocEntry that \p FID represents.
unsigned SourceManager::getFileIDSize(FileID FID) const {
  bool Invalid = false;
  const SrcMgr::SLocEntry &Entry = getSLocEntry(FID, &Invalid);
  if (Invalid)
    return 0;

  int ID = FID.ID;
  unsigned NextOffset;
  if ((ID > 0 && unsigned(ID+1) == local_sloc_entry_size()))
    NextOffset = getNextLocalOffset();
  else if (ID+1 == -1)
    NextOffset = MaxLoadedOffset;
  else
    NextOffset = getSLocEntry(FileID::get(ID+1)).getOffset();

  return NextOffset - Entry.getOffset() - 1;
}

//===----------------------------------------------------------------------===//
// Other miscellaneous methods.
//===----------------------------------------------------------------------===//

/// Retrieve the inode for the given file entry, if possible.
///
/// This routine involves a system call, and therefore should only be used
/// in non-performance-critical code.
static Optional<llvm::sys::fs::UniqueID>
getActualFileUID(const FileEntry *File) {
  if (!File)
    return None;

  llvm::sys::fs::UniqueID ID;
  if (llvm::sys::fs::getUniqueID(File->getName(), ID))
    return None;

  return ID;
}

/// Get the source location for the given file:line:col triplet.
///
/// If the source file is included multiple times, the source location will
/// be based upon an arbitrary inclusion.
SourceLocation SourceManager::translateFileLineCol(const FileEntry *SourceFile,
                                                  unsigned Line,
                                                  unsigned Col) const {
  assert(SourceFile && "Null source file!");
  assert(Line && Col && "Line and column should start from 1!");

  FileID FirstFID = translateFile(SourceFile);
  return translateLineCol(FirstFID, Line, Col);
}

/// Get the FileID for the given file.
///
/// If the source file is included multiple times, the FileID will be the
/// first inclusion.
FileID SourceManager::translateFile(const FileEntry *SourceFile) const {
  assert(SourceFile && "Null source file!");

  // Find the first file ID that corresponds to the given file.
  FileID FirstFID;

  // First, check the main file ID, since it is common to look for a
  // location in the main file.
  Optional<llvm::sys::fs::UniqueID> SourceFileUID;
  Optional<StringRef> SourceFileName;
  if (MainFileID.isValid()) {
    bool Invalid = false;
    const SLocEntry &MainSLoc = getSLocEntry(MainFileID, &Invalid);
    if (Invalid)
      return FileID();

    if (MainSLoc.isFile()) {
      const ContentCache *MainContentCache
        = MainSLoc.getFile().getContentCache();
      if (!MainContentCache) {
        // Can't do anything
      } else if (MainContentCache->OrigEntry == SourceFile) {
        FirstFID = MainFileID;
      } else {
        // Fall back: check whether we have the same base name and inode
        // as the main file.
        const FileEntry *MainFile = MainContentCache->OrigEntry;
        SourceFileName = llvm::sys::path::filename(SourceFile->getName());
        if (*SourceFileName == llvm::sys::path::filename(MainFile->getName())) {
          SourceFileUID = getActualFileUID(SourceFile);
          if (SourceFileUID) {
            if (Optional<llvm::sys::fs::UniqueID> MainFileUID =
                    getActualFileUID(MainFile)) {
              if (*SourceFileUID == *MainFileUID) {
                FirstFID = MainFileID;
                SourceFile = MainFile;
              }
            }
          }
        }
      }
    }
  }

  if (FirstFID.isInvalid()) {
    // The location we're looking for isn't in the main file; look
    // through all of the local source locations.
    for (unsigned I = 0, N = local_sloc_entry_size(); I != N; ++I) {
      bool Invalid = false;
      const SLocEntry &SLoc = getLocalSLocEntry(I, &Invalid);
      if (Invalid)
        return FileID();

      if (SLoc.isFile() &&
          SLoc.getFile().getContentCache() &&
          SLoc.getFile().getContentCache()->OrigEntry == SourceFile) {
        FirstFID = FileID::get(I);
        break;
      }
    }
    // If that still didn't help, try the modules.
    if (FirstFID.isInvalid()) {
      for (unsigned I = 0, N = loaded_sloc_entry_size(); I != N; ++I) {
        const SLocEntry &SLoc = getLoadedSLocEntry(I);
        if (SLoc.isFile() &&
            SLoc.getFile().getContentCache() &&
            SLoc.getFile().getContentCache()->OrigEntry == SourceFile) {
          FirstFID = FileID::get(-int(I) - 2);
          break;
        }
      }
    }
  }

  // If we haven't found what we want yet, try again, but this time stat()
  // each of the files in case the files have changed since we originally
  // parsed the file.
  if (FirstFID.isInvalid() &&
      (SourceFileName ||
       (SourceFileName = llvm::sys::path::filename(SourceFile->getName()))) &&
      (SourceFileUID || (SourceFileUID = getActualFileUID(SourceFile)))) {
    bool Invalid = false;
    for (unsigned I = 0, N = local_sloc_entry_size(); I != N; ++I) {
      FileID IFileID;
      IFileID.ID = I;
      const SLocEntry &SLoc = getSLocEntry(IFileID, &Invalid);
      if (Invalid)
        return FileID();

      if (SLoc.isFile()) {
        const ContentCache *FileContentCache
          = SLoc.getFile().getContentCache();
        const FileEntry *Entry = FileContentCache ? FileContentCache->OrigEntry
                                                  : nullptr;
        if (Entry &&
            *SourceFileName == llvm::sys::path::filename(Entry->getName())) {
          if (Optional<llvm::sys::fs::UniqueID> EntryUID =
                  getActualFileUID(Entry)) {
            if (*SourceFileUID == *EntryUID) {
              FirstFID = FileID::get(I);
              SourceFile = Entry;
              break;
            }
          }
        }
      }
    }
  }

  (void) SourceFile;
  return FirstFID;
}

/// Get the source location in \arg FID for the given line:col.
/// Returns null location if \arg FID is not a file SLocEntry.
SourceLocation SourceManager::translateLineCol(FileID FID,
                                               unsigned Line,
                                               unsigned Col) const {
  // Lines are used as a one-based index into a zero-based array. This assert
  // checks for possible buffer underruns.
  assert(Line && Col && "Line and column should start from 1!");

  if (FID.isInvalid())
    return SourceLocation();

  bool Invalid = false;
  const SLocEntry &Entry = getSLocEntry(FID, &Invalid);
  if (Invalid)
    return SourceLocation();

  if (!Entry.isFile())
    return SourceLocation();

  SourceLocation FileLoc = SourceLocation::getFileLoc(Entry.getOffset());

  if (Line == 1 && Col == 1)
    return FileLoc;

  ContentCache *Content
    = const_cast<ContentCache *>(Entry.getFile().getContentCache());
  if (!Content)
    return SourceLocation();

  // If this is the first use of line information for this buffer, compute the
  // SourceLineCache for it on demand.
  if (!Content->SourceLineCache) {
    bool MyInvalid = false;
    ComputeLineNumbers(Diag, Content, ContentCacheAlloc, *this, MyInvalid);
    if (MyInvalid)
      return SourceLocation();
  }

  if (Line > Content->NumLines) {
    unsigned Size = Content->getBuffer(Diag, *this)->getBufferSize();
    if (Size > 0)
      --Size;
    return FileLoc.getLocWithOffset(Size);
  }

  llvm::MemoryBuffer *Buffer = Content->getBuffer(Diag, *this);
  unsigned FilePos = Content->SourceLineCache[Line - 1];
  const char *Buf = Buffer->getBufferStart() + FilePos;
  unsigned BufLength = Buffer->getBufferSize() - FilePos;
  if (BufLength == 0)
    return FileLoc.getLocWithOffset(FilePos);

  unsigned i = 0;

  // Check that the given column is valid.
  while (i < BufLength-1 && i < Col-1 && Buf[i] != '\n' && Buf[i] != '\r')
    ++i;
  return FileLoc.getLocWithOffset(FilePos + i);
}

/// Compute a map of macro argument chunks to their expanded source
/// location. Chunks that are not part of a macro argument will map to an
/// invalid source location. e.g. if a file contains one macro argument at
/// offset 100 with length 10, this is how the map will be formed:
///     0   -> SourceLocation()
///     100 -> Expanded macro arg location
///     110 -> SourceLocation()
void SourceManager::computeMacroArgsCache(MacroArgsMap &MacroArgsCache,
                                          FileID FID) const {
  assert(FID.isValid());

  // Initially no macro argument chunk is present.
  MacroArgsCache.insert(std::make_pair(0, SourceLocation()));

  int ID = FID.ID;
  while (true) {
    ++ID;
    // Stop if there are no more FileIDs to check.
    if (ID > 0) {
      if (unsigned(ID) >= local_sloc_entry_size())
        return;
    } else if (ID == -1) {
      return;
    }

    bool Invalid = false;
    const SrcMgr::SLocEntry &Entry = getSLocEntryByID(ID, &Invalid);
    if (Invalid)
      return;
    if (Entry.isFile()) {
      SourceLocation IncludeLoc = Entry.getFile().getIncludeLoc();
      if (IncludeLoc.isInvalid())
        continue;
      if (!isInFileID(IncludeLoc, FID))
        return; // No more files/macros that may be "contained" in this file.

      // Skip the files/macros of the #include'd file, we only care about macros
      // that lexed macro arguments from our file.
      if (Entry.getFile().NumCreatedFIDs)
        ID += Entry.getFile().NumCreatedFIDs - 1/*because of next ++ID*/;
      continue;
    }

    const ExpansionInfo &ExpInfo = Entry.getExpansion();

    if (ExpInfo.getExpansionLocStart().isFileID()) {
      if (!isInFileID(ExpInfo.getExpansionLocStart(), FID))
        return; // No more files/macros that may be "contained" in this file.
    }

    if (!ExpInfo.isMacroArgExpansion())
      continue;

    associateFileChunkWithMacroArgExp(MacroArgsCache, FID,
                                 ExpInfo.getSpellingLoc(),
                                 SourceLocation::getMacroLoc(Entry.getOffset()),
                                 getFileIDSize(FileID::get(ID)));
  }
}

void SourceManager::associateFileChunkWithMacroArgExp(
                                         MacroArgsMap &MacroArgsCache,
                                         FileID FID,
                                         SourceLocation SpellLoc,
                                         SourceLocation ExpansionLoc,
                                         unsigned ExpansionLength) const {
  if (!SpellLoc.isFileID()) {
    unsigned SpellBeginOffs = SpellLoc.getOffset();
    unsigned SpellEndOffs = SpellBeginOffs + ExpansionLength;

    // The spelling range for this macro argument expansion can span multiple
    // consecutive FileID entries. Go through each entry contained in the
    // spelling range and if one is itself a macro argument expansion, recurse
    // and associate the file chunk that it represents.

    FileID SpellFID; // Current FileID in the spelling range.
    unsigned SpellRelativeOffs;
    std::tie(SpellFID, SpellRelativeOffs) = getDecomposedLoc(SpellLoc);
    while (true) {
      const SLocEntry &Entry = getSLocEntry(SpellFID);
      unsigned SpellFIDBeginOffs = Entry.getOffset();
      unsigned SpellFIDSize = getFileIDSize(SpellFID);
      unsigned SpellFIDEndOffs = SpellFIDBeginOffs + SpellFIDSize;
      const ExpansionInfo &Info = Entry.getExpansion();
      if (Info.isMacroArgExpansion()) {
        unsigned CurrSpellLength;
        if (SpellFIDEndOffs < SpellEndOffs)
          CurrSpellLength = SpellFIDSize - SpellRelativeOffs;
        else
          CurrSpellLength = ExpansionLength;
        associateFileChunkWithMacroArgExp(MacroArgsCache, FID,
                      Info.getSpellingLoc().getLocWithOffset(SpellRelativeOffs),
                      ExpansionLoc, CurrSpellLength);
      }

      if (SpellFIDEndOffs >= SpellEndOffs)
        return; // we covered all FileID entries in the spelling range.

      // Move to the next FileID entry in the spelling range.
      unsigned advance = SpellFIDSize - SpellRelativeOffs + 1;
      ExpansionLoc = ExpansionLoc.getLocWithOffset(advance);
      ExpansionLength -= advance;
      ++SpellFID.ID;
      SpellRelativeOffs = 0;
    }
  }

  assert(SpellLoc.isFileID());

  unsigned BeginOffs;
  if (!isInFileID(SpellLoc, FID, &BeginOffs))
    return;

  unsigned EndOffs = BeginOffs + ExpansionLength;

  // Add a new chunk for this macro argument. A previous macro argument chunk
  // may have been lexed again, so e.g. if the map is
  //     0   -> SourceLocation()
  //     100 -> Expanded loc #1
  //     110 -> SourceLocation()
  // and we found a new macro FileID that lexed from offset 105 with length 3,
  // the new map will be:
  //     0   -> SourceLocation()
  //     100 -> Expanded loc #1
  //     105 -> Expanded loc #2
  //     108 -> Expanded loc #1
  //     110 -> SourceLocation()
  //
  // Since re-lexed macro chunks will always be the same size or less of
  // previous chunks, we only need to find where the ending of the new macro
  // chunk is mapped to and update the map with new begin/end mappings.

  MacroArgsMap::iterator I = MacroArgsCache.upper_bound(EndOffs);
  --I;
  SourceLocation EndOffsMappedLoc = I->second;
  MacroArgsCache[BeginOffs] = ExpansionLoc;
  MacroArgsCache[EndOffs] = EndOffsMappedLoc;
}

/// If \arg Loc points inside a function macro argument, the returned
/// location will be the macro location in which the argument was expanded.
/// If a macro argument is used multiple times, the expanded location will
/// be at the first expansion of the argument.
/// e.g.
///   MY_MACRO(foo);
///             ^
/// Passing a file location pointing at 'foo', will yield a macro location
/// where 'foo' was expanded into.
SourceLocation
SourceManager::getMacroArgExpandedLocation(SourceLocation Loc) const {
  if (Loc.isInvalid() || !Loc.isFileID())
    return Loc;

  FileID FID;
  unsigned Offset;
  std::tie(FID, Offset) = getDecomposedLoc(Loc);
  if (FID.isInvalid())
    return Loc;

  std::unique_ptr<MacroArgsMap> &MacroArgsCache = MacroArgsCacheMap[FID];
  if (!MacroArgsCache) {
    MacroArgsCache = llvm::make_unique<MacroArgsMap>();
    computeMacroArgsCache(*MacroArgsCache, FID);
  }

  assert(!MacroArgsCache->empty());
  MacroArgsMap::iterator I = MacroArgsCache->upper_bound(Offset);
  --I;

  unsigned MacroArgBeginOffs = I->first;
  SourceLocation MacroArgExpandedLoc = I->second;
  if (MacroArgExpandedLoc.isValid())
    return MacroArgExpandedLoc.getLocWithOffset(Offset - MacroArgBeginOffs);

  return Loc;
}

std::pair<FileID, unsigned>
SourceManager::getDecomposedIncludedLoc(FileID FID) const {
  if (FID.isInvalid())
    return std::make_pair(FileID(), 0);

  // Uses IncludedLocMap to retrieve/cache the decomposed loc.

  using DecompTy = std::pair<FileID, unsigned>;
  auto InsertOp = IncludedLocMap.try_emplace(FID);
  DecompTy &DecompLoc = InsertOp.first->second;
  if (!InsertOp.second)
    return DecompLoc; // already in map.

  SourceLocation UpperLoc;
  bool Invalid = false;
  const SrcMgr::SLocEntry &Entry = getSLocEntry(FID, &Invalid);
  if (!Invalid) {
    if (Entry.isExpansion())
      UpperLoc = Entry.getExpansion().getExpansionLocStart();
    else
      UpperLoc = Entry.getFile().getIncludeLoc();
  }

  if (UpperLoc.isValid())
    DecompLoc = getDecomposedLoc(UpperLoc);

  return DecompLoc;
}

/// Given a decomposed source location, move it up the include/expansion stack
/// to the parent source location.  If this is possible, return the decomposed
/// version of the parent in Loc and return false.  If Loc is the top-level
/// entry, return true and don't modify it.
static bool MoveUpIncludeHierarchy(std::pair<FileID, unsigned> &Loc,
                                   const SourceManager &SM) {
  std::pair<FileID, unsigned> UpperLoc = SM.getDecomposedIncludedLoc(Loc.first);
  if (UpperLoc.first.isInvalid())
    return true; // We reached the top.

  Loc = UpperLoc;
  return false;
}

/// Return the cache entry for comparing the given file IDs
/// for isBeforeInTranslationUnit.
InBeforeInTUCacheEntry &SourceManager::getInBeforeInTUCache(FileID LFID,
                                                            FileID RFID) const {
  // This is a magic number for limiting the cache size.  It was experimentally
  // derived from a small Objective-C project (where the cache filled
  // out to ~250 items).  We can make it larger if necessary.
  enum { MagicCacheSize = 300 };
  IsBeforeInTUCacheKey Key(LFID, RFID);

  // If the cache size isn't too large, do a lookup and if necessary default
  // construct an entry.  We can then return it to the caller for direct
  // use.  When they update the value, the cache will get automatically
  // updated as well.
  if (IBTUCache.size() < MagicCacheSize)
    return IBTUCache[Key];

  // Otherwise, do a lookup that will not construct a new value.
  InBeforeInTUCache::iterator I = IBTUCache.find(Key);
  if (I != IBTUCache.end())
    return I->second;

  // Fall back to the overflow value.
  return IBTUCacheOverflow;
}

/// Determines the order of 2 source locations in the translation unit.
///
/// \returns true if LHS source location comes before RHS, false otherwise.
bool SourceManager::isBeforeInTranslationUnit(SourceLocation LHS,
                                              SourceLocation RHS) const {
  assert(LHS.isValid() && RHS.isValid() && "Passed invalid source location!");
  if (LHS == RHS)
    return false;

  std::pair<FileID, unsigned> LOffs = getDecomposedLoc(LHS);
  std::pair<FileID, unsigned> ROffs = getDecomposedLoc(RHS);

  // getDecomposedLoc may have failed to return a valid FileID because, e.g. it
  // is a serialized one referring to a file that was removed after we loaded
  // the PCH.
  if (LOffs.first.isInvalid() || ROffs.first.isInvalid())
    return LOffs.first.isInvalid() && !ROffs.first.isInvalid();

  std::pair<bool, bool> InSameTU = isInTheSameTranslationUnit(LOffs, ROffs);
  if (InSameTU.first)
    return InSameTU.second;

  // If we arrived here, the location is either in a built-ins buffer or
  // associated with global inline asm. PR5662 and PR22576 are examples.

  StringRef LB = getBuffer(LOffs.first)->getBufferIdentifier();
  StringRef RB = getBuffer(ROffs.first)->getBufferIdentifier();
  bool LIsBuiltins = LB == "<built-in>";
  bool RIsBuiltins = RB == "<built-in>";
  // Sort built-in before non-built-in.
  if (LIsBuiltins || RIsBuiltins) {
    if (LIsBuiltins != RIsBuiltins)
      return LIsBuiltins;
    // Both are in built-in buffers, but from different files. We just claim that
    // lower IDs come first.
    return LOffs.first < ROffs.first;
  }
  bool LIsAsm = LB == "<inline asm>";
  bool RIsAsm = RB == "<inline asm>";
  // Sort assembler after built-ins, but before the rest.
  if (LIsAsm || RIsAsm) {
    if (LIsAsm != RIsAsm)
      return RIsAsm;
    assert(LOffs.first == ROffs.first);
    return false;
  }
  bool LIsScratch = LB == "<scratch space>";
  bool RIsScratch = RB == "<scratch space>";
  // Sort scratch after inline asm, but before the rest.
  if (LIsScratch || RIsScratch) {
    if (LIsScratch != RIsScratch)
      return LIsScratch;
    return LOffs.second < ROffs.second;
  }
  llvm_unreachable("Unsortable locations found");
}

std::pair<bool, bool> SourceManager::isInTheSameTranslationUnit(
    std::pair<FileID, unsigned> &LOffs,
    std::pair<FileID, unsigned> &ROffs) const {
  // If the source locations are in the same file, just compare offsets.
  if (LOffs.first == ROffs.first)
    return std::make_pair(true, LOffs.second < ROffs.second);

  // If we are comparing a source location with multiple locations in the same
  // file, we get a big win by caching the result.
  InBeforeInTUCacheEntry &IsBeforeInTUCache =
    getInBeforeInTUCache(LOffs.first, ROffs.first);

  // If we are comparing a source location with multiple locations in the same
  // file, we get a big win by caching the result.
  if (IsBeforeInTUCache.isCacheValid(LOffs.first, ROffs.first))
    return std::make_pair(
        true, IsBeforeInTUCache.getCachedResult(LOffs.second, ROffs.second));

  // Okay, we missed in the cache, start updating the cache for this query.
  IsBeforeInTUCache.setQueryFIDs(LOffs.first, ROffs.first,
                          /*isLFIDBeforeRFID=*/LOffs.first.ID < ROffs.first.ID);

  // We need to find the common ancestor. The only way of doing this is to
  // build the complete include chain for one and then walking up the chain
  // of the other looking for a match.
  // We use a map from FileID to Offset to store the chain. Easier than writing
  // a custom set hash info that only depends on the first part of a pair.
  using LocSet = llvm::SmallDenseMap<FileID, unsigned, 16>;
  LocSet LChain;
  do {
    LChain.insert(LOffs);
    // We catch the case where LOffs is in a file included by ROffs and
    // quit early. The other way round unfortunately remains suboptimal.
  } while (LOffs.first != ROffs.first && !MoveUpIncludeHierarchy(LOffs, *this));
  LocSet::iterator I;
  while((I = LChain.find(ROffs.first)) == LChain.end()) {
    if (MoveUpIncludeHierarchy(ROffs, *this))
      break; // Met at topmost file.
  }
  if (I != LChain.end())
    LOffs = *I;

  // If we exited because we found a nearest common ancestor, compare the
  // locations within the common file and cache them.
  if (LOffs.first == ROffs.first) {
    IsBeforeInTUCache.setCommonLoc(LOffs.first, LOffs.second, ROffs.second);
    return std::make_pair(
        true, IsBeforeInTUCache.getCachedResult(LOffs.second, ROffs.second));
  }
  // Clear the lookup cache, it depends on a common location.
  IsBeforeInTUCache.clear();
  return std::make_pair(false, false);
}

void SourceManager::PrintStats() const {
  llvm::errs() << "\n*** Source Manager Stats:\n";
  llvm::errs() << FileInfos.size() << " files mapped, " << MemBufferInfos.size()
               << " mem buffers mapped.\n";
  llvm::errs() << LocalSLocEntryTable.size() << " local SLocEntry's allocated ("
               << llvm::capacity_in_bytes(LocalSLocEntryTable)
               << " bytes of capacity), "
               << NextLocalOffset << "B of Sloc address space used.\n";
  llvm::errs() << LoadedSLocEntryTable.size()
               << " loaded SLocEntries allocated, "
               << MaxLoadedOffset - CurrentLoadedOffset
               << "B of Sloc address space used.\n";

  unsigned NumLineNumsComputed = 0;
  unsigned NumFileBytesMapped = 0;
  for (fileinfo_iterator I = fileinfo_begin(), E = fileinfo_end(); I != E; ++I){
    NumLineNumsComputed += I->second->SourceLineCache != nullptr;
    NumFileBytesMapped  += I->second->getSizeBytesMapped();
  }
  unsigned NumMacroArgsComputed = MacroArgsCacheMap.size();

  llvm::errs() << NumFileBytesMapped << " bytes of files mapped, "
               << NumLineNumsComputed << " files with line #'s computed, "
               << NumMacroArgsComputed << " files with macro args computed.\n";
  llvm::errs() << "FileID scans: " << NumLinearScans << " linear, "
               << NumBinaryProbes << " binary.\n";
}

LLVM_DUMP_METHOD void SourceManager::dump() const {
  llvm::raw_ostream &out = llvm::errs();

  auto DumpSLocEntry = [&](int ID, const SrcMgr::SLocEntry &Entry,
                           llvm::Optional<unsigned> NextStart) {
    out << "SLocEntry <FileID " << ID << "> " << (Entry.isFile() ? "file" : "expansion")
        << " <SourceLocation " << Entry.getOffset() << ":";
    if (NextStart)
      out << *NextStart << ">\n";
    else
      out << "???\?>\n";
    if (Entry.isFile()) {
      auto &FI = Entry.getFile();
      if (FI.NumCreatedFIDs)
        out << "  covers <FileID " << ID << ":" << int(ID + FI.NumCreatedFIDs)
            << ">\n";
      if (FI.getIncludeLoc().isValid())
        out << "  included from " << FI.getIncludeLoc().getOffset() << "\n";
      if (auto *CC = FI.getContentCache()) {
        out << "  for " << (CC->OrigEntry ? CC->OrigEntry->getName() : "<none>")
            << "\n";
        if (CC->BufferOverridden)
          out << "  contents overridden\n";
        if (CC->ContentsEntry != CC->OrigEntry) {
          out << "  contents from "
              << (CC->ContentsEntry ? CC->ContentsEntry->getName() : "<none>")
              << "\n";
        }
      }
    } else {
      auto &EI = Entry.getExpansion();
      out << "  spelling from " << EI.getSpellingLoc().getOffset() << "\n";
      out << "  macro " << (EI.isMacroArgExpansion() ? "arg" : "body")
          << " range <" << EI.getExpansionLocStart().getOffset() << ":"
          << EI.getExpansionLocEnd().getOffset() << ">\n";
    }
  };

  // Dump local SLocEntries.
  for (unsigned ID = 0, NumIDs = LocalSLocEntryTable.size(); ID != NumIDs; ++ID) {
    DumpSLocEntry(ID, LocalSLocEntryTable[ID],
                  ID == NumIDs - 1 ? NextLocalOffset
                                   : LocalSLocEntryTable[ID + 1].getOffset());
  }
  // Dump loaded SLocEntries.
  llvm::Optional<unsigned> NextStart;
  for (unsigned Index = 0; Index != LoadedSLocEntryTable.size(); ++Index) {
    int ID = -(int)Index - 2;
    if (SLocEntryLoaded[Index]) {
      DumpSLocEntry(ID, LoadedSLocEntryTable[Index], NextStart);
      NextStart = LoadedSLocEntryTable[Index].getOffset();
    } else {
      NextStart = None;
    }
  }
}

ExternalSLocEntrySource::~ExternalSLocEntrySource() = default;

/// Return the amount of memory used by memory buffers, breaking down
/// by heap-backed versus mmap'ed memory.
SourceManager::MemoryBufferSizes SourceManager::getMemoryBufferSizes() const {
  size_t malloc_bytes = 0;
  size_t mmap_bytes = 0;

  for (unsigned i = 0, e = MemBufferInfos.size(); i != e; ++i)
    if (size_t sized_mapped = MemBufferInfos[i]->getSizeBytesMapped())
      switch (MemBufferInfos[i]->getMemoryBufferKind()) {
        case llvm::MemoryBuffer::MemoryBuffer_MMap:
          mmap_bytes += sized_mapped;
          break;
        case llvm::MemoryBuffer::MemoryBuffer_Malloc:
          malloc_bytes += sized_mapped;
          break;
      }

  return MemoryBufferSizes(malloc_bytes, mmap_bytes);
}

size_t SourceManager::getDataStructureSizes() const {
  size_t size = llvm::capacity_in_bytes(MemBufferInfos)
    + llvm::capacity_in_bytes(LocalSLocEntryTable)
    + llvm::capacity_in_bytes(LoadedSLocEntryTable)
    + llvm::capacity_in_bytes(SLocEntryLoaded)
    + llvm::capacity_in_bytes(FileInfos);

  if (OverriddenFilesInfo)
    size += llvm::capacity_in_bytes(OverriddenFilesInfo->OverriddenFiles);

  return size;
}

SourceManagerForFile::SourceManagerForFile(StringRef FileName,
                                           StringRef Content) {
  // This is referenced by `FileMgr` and will be released by `FileMgr` when it
  // is deleted.
  IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
      new llvm::vfs::InMemoryFileSystem);
  InMemoryFileSystem->addFile(
      FileName, 0,
      llvm::MemoryBuffer::getMemBuffer(Content, FileName,
                                       /*RequiresNullTerminator=*/false));
  // This is passed to `SM` as reference, so the pointer has to be referenced
  // in `Environment` so that `FileMgr` can out-live this function scope.
  FileMgr =
      llvm::make_unique<FileManager>(FileSystemOptions(), InMemoryFileSystem);
  // This is passed to `SM` as reference, so the pointer has to be referenced
  // by `Environment` due to the same reason above.
  Diagnostics = llvm::make_unique<DiagnosticsEngine>(
      IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs),
      new DiagnosticOptions);
  SourceMgr = llvm::make_unique<SourceManager>(*Diagnostics, *FileMgr);
  FileID ID = SourceMgr->createFileID(FileMgr->getFile(FileName),
                                      SourceLocation(), clang::SrcMgr::C_User);
  assert(ID.isValid());
  SourceMgr->setMainFileID(ID);
}
