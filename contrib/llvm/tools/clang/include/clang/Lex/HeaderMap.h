//===--- HeaderMap.h - A file that acts like dir of symlinks ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the HeaderMap interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_HEADERMAP_H
#define LLVM_CLANG_LEX_HEADERMAP_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace clang {

class FileEntry;
class FileManager;
struct HMapBucket;
struct HMapHeader;

/// Implementation for \a HeaderMap that doesn't depend on \a FileManager.
class HeaderMapImpl {
  std::unique_ptr<const llvm::MemoryBuffer> FileBuffer;
  bool NeedsBSwap;

public:
  HeaderMapImpl(std::unique_ptr<const llvm::MemoryBuffer> File, bool NeedsBSwap)
      : FileBuffer(std::move(File)), NeedsBSwap(NeedsBSwap) {}

  // Check for a valid header and extract the byte swap.
  static bool checkHeader(const llvm::MemoryBuffer &File, bool &NeedsByteSwap);

  /// If the specified relative filename is located in this HeaderMap return
  /// the filename it is mapped to, otherwise return an empty StringRef.
  StringRef lookupFilename(StringRef Filename,
                           SmallVectorImpl<char> &DestPath) const;

  /// Return the filename of the headermap.
  StringRef getFileName() const;

  /// Print the contents of this headermap to stderr.
  void dump() const;

private:
  unsigned getEndianAdjustedWord(unsigned X) const;
  const HMapHeader &getHeader() const;
  HMapBucket getBucket(unsigned BucketNo) const;

  /// Look up the specified string in the string table.  If the string index is
  /// not valid, return None.
  Optional<StringRef> getString(unsigned StrTabIdx) const;
};

/// This class represents an Apple concept known as a 'header map'.  To the
/// \#include file resolution process, it basically acts like a directory of
/// symlinks to files.  Its advantages are that it is dense and more efficient
/// to create and process than a directory of symlinks.
class HeaderMap : private HeaderMapImpl {
  HeaderMap(std::unique_ptr<const llvm::MemoryBuffer> File, bool BSwap)
      : HeaderMapImpl(std::move(File), BSwap) {}

public:
  /// This attempts to load the specified file as a header map.  If it doesn't
  /// look like a HeaderMap, it gives up and returns null.
  static std::unique_ptr<HeaderMap> Create(const FileEntry *FE,
                                           FileManager &FM);

  /// Check to see if the specified relative filename is located in this
  /// HeaderMap.  If so, open it and return its FileEntry.  If RawPath is not
  /// NULL and the file is found, RawPath will be set to the raw path at which
  /// the file was found in the file system. For example, for a search path
  /// ".." and a filename "../file.h" this would be "../../file.h".
  const FileEntry *LookupFile(StringRef Filename, FileManager &FM) const;

  using HeaderMapImpl::lookupFilename;
  using HeaderMapImpl::getFileName;
  using HeaderMapImpl::dump;
};

} // end namespace clang.

#endif
