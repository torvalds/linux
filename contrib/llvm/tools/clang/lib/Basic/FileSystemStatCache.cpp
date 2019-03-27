//===- FileSystemStatCache.cpp - Caching for 'stat' calls -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the FileSystemStatCache interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/FileSystemStatCache.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <utility>

using namespace clang;

void FileSystemStatCache::anchor() {}

static void copyStatusToFileData(const llvm::vfs::Status &Status,
                                 FileData &Data) {
  Data.Name = Status.getName();
  Data.Size = Status.getSize();
  Data.ModTime = llvm::sys::toTimeT(Status.getLastModificationTime());
  Data.UniqueID = Status.getUniqueID();
  Data.IsDirectory = Status.isDirectory();
  Data.IsNamedPipe = Status.getType() == llvm::sys::fs::file_type::fifo_file;
  Data.InPCH = false;
  Data.IsVFSMapped = Status.IsVFSMapped;
}

/// FileSystemStatCache::get - Get the 'stat' information for the specified
/// path, using the cache to accelerate it if possible.  This returns true if
/// the path does not exist or false if it exists.
///
/// If isFile is true, then this lookup should only return success for files
/// (not directories).  If it is false this lookup should only return
/// success for directories (not files).  On a successful file lookup, the
/// implementation can optionally fill in FileDescriptor with a valid
/// descriptor and the client guarantees that it will close it.
bool FileSystemStatCache::get(StringRef Path, FileData &Data, bool isFile,
                              std::unique_ptr<llvm::vfs::File> *F,
                              FileSystemStatCache *Cache,
                              llvm::vfs::FileSystem &FS) {
  LookupResult R;
  bool isForDir = !isFile;

  // If we have a cache, use it to resolve the stat query.
  if (Cache)
    R = Cache->getStat(Path, Data, isFile, F, FS);
  else if (isForDir || !F) {
    // If this is a directory or a file descriptor is not needed and we have
    // no cache, just go to the file system.
    llvm::ErrorOr<llvm::vfs::Status> Status = FS.status(Path);
    if (!Status) {
      R = CacheMissing;
    } else {
      R = CacheExists;
      copyStatusToFileData(*Status, Data);
    }
  } else {
    // Otherwise, we have to go to the filesystem.  We can always just use
    // 'stat' here, but (for files) the client is asking whether the file exists
    // because it wants to turn around and *open* it.  It is more efficient to
    // do "open+fstat" on success than it is to do "stat+open".
    //
    // Because of this, check to see if the file exists with 'open'.  If the
    // open succeeds, use fstat to get the stat info.
    auto OwnedFile = FS.openFileForRead(Path);

    if (!OwnedFile) {
      // If the open fails, our "stat" fails.
      R = CacheMissing;
    } else {
      // Otherwise, the open succeeded.  Do an fstat to get the information
      // about the file.  We'll end up returning the open file descriptor to the
      // client to do what they please with it.
      llvm::ErrorOr<llvm::vfs::Status> Status = (*OwnedFile)->status();
      if (Status) {
        R = CacheExists;
        copyStatusToFileData(*Status, Data);
        *F = std::move(*OwnedFile);
      } else {
        // fstat rarely fails.  If it does, claim the initial open didn't
        // succeed.
        R = CacheMissing;
        *F = nullptr;
      }
    }
  }

  // If the path doesn't exist, return failure.
  if (R == CacheMissing) return true;

  // If the path exists, make sure that its "directoryness" matches the clients
  // demands.
  if (Data.IsDirectory != isForDir) {
    // If not, close the file if opened.
    if (F)
      *F = nullptr;

    return true;
  }

  return false;
}

MemorizeStatCalls::LookupResult
MemorizeStatCalls::getStat(StringRef Path, FileData &Data, bool isFile,
                           std::unique_ptr<llvm::vfs::File> *F,
                           llvm::vfs::FileSystem &FS) {
  if (get(Path, Data, isFile, F, nullptr, FS)) {
    // Do not cache failed stats, it is easy to construct common inconsistent
    // situations if we do, and they are not important for PCH performance
    // (which currently only needs the stats to construct the initial
    // FileManager entries).
    return CacheMissing;
  }

  // Cache file 'stat' results and directories with absolutely paths.
  if (!Data.IsDirectory || llvm::sys::path::is_absolute(Path))
    StatCalls[Path] = Data;

  return CacheExists;
}
