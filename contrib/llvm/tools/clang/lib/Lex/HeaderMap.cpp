//===--- HeaderMap.cpp - A file that acts like dir of symlinks ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the HeaderMap interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/HeaderMap.h"
#include "clang/Lex/HeaderMapTypes.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/FileManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/Support/Debug.h"
#include <cstring>
#include <memory>
using namespace clang;

/// HashHMapKey - This is the 'well known' hash function required by the file
/// format, used to look up keys in the hash table.  The hash table uses simple
/// linear probing based on this function.
static inline unsigned HashHMapKey(StringRef Str) {
  unsigned Result = 0;
  const char *S = Str.begin(), *End = Str.end();

  for (; S != End; S++)
    Result += toLowercase(*S) * 13;
  return Result;
}



//===----------------------------------------------------------------------===//
// Verification and Construction
//===----------------------------------------------------------------------===//

/// HeaderMap::Create - This attempts to load the specified file as a header
/// map.  If it doesn't look like a HeaderMap, it gives up and returns null.
/// If it looks like a HeaderMap but is obviously corrupted, it puts a reason
/// into the string error argument and returns null.
std::unique_ptr<HeaderMap> HeaderMap::Create(const FileEntry *FE,
                                             FileManager &FM) {
  // If the file is too small to be a header map, ignore it.
  unsigned FileSize = FE->getSize();
  if (FileSize <= sizeof(HMapHeader)) return nullptr;

  auto FileBuffer = FM.getBufferForFile(FE);
  if (!FileBuffer || !*FileBuffer)
    return nullptr;
  bool NeedsByteSwap;
  if (!checkHeader(**FileBuffer, NeedsByteSwap))
    return nullptr;
  return std::unique_ptr<HeaderMap>(new HeaderMap(std::move(*FileBuffer), NeedsByteSwap));
}

bool HeaderMapImpl::checkHeader(const llvm::MemoryBuffer &File,
                                bool &NeedsByteSwap) {
  if (File.getBufferSize() <= sizeof(HMapHeader))
    return false;
  const char *FileStart = File.getBufferStart();

  // We know the file is at least as big as the header, check it now.
  const HMapHeader *Header = reinterpret_cast<const HMapHeader*>(FileStart);

  // Sniff it to see if it's a headermap by checking the magic number and
  // version.
  if (Header->Magic == HMAP_HeaderMagicNumber &&
      Header->Version == HMAP_HeaderVersion)
    NeedsByteSwap = false;
  else if (Header->Magic == llvm::ByteSwap_32(HMAP_HeaderMagicNumber) &&
           Header->Version == llvm::ByteSwap_16(HMAP_HeaderVersion))
    NeedsByteSwap = true;  // Mixed endianness headermap.
  else
    return false;  // Not a header map.

  if (Header->Reserved != 0)
    return false;

  // Check the number of buckets.  It should be a power of two, and there
  // should be enough space in the file for all of them.
  uint32_t NumBuckets = NeedsByteSwap
                            ? llvm::sys::getSwappedBytes(Header->NumBuckets)
                            : Header->NumBuckets;
  if (!llvm::isPowerOf2_32(NumBuckets))
    return false;
  if (File.getBufferSize() <
      sizeof(HMapHeader) + sizeof(HMapBucket) * NumBuckets)
    return false;

  // Okay, everything looks good.
  return true;
}

//===----------------------------------------------------------------------===//
//  Utility Methods
//===----------------------------------------------------------------------===//


/// getFileName - Return the filename of the headermap.
StringRef HeaderMapImpl::getFileName() const {
  return FileBuffer->getBufferIdentifier();
}

unsigned HeaderMapImpl::getEndianAdjustedWord(unsigned X) const {
  if (!NeedsBSwap) return X;
  return llvm::ByteSwap_32(X);
}

/// getHeader - Return a reference to the file header, in unbyte-swapped form.
/// This method cannot fail.
const HMapHeader &HeaderMapImpl::getHeader() const {
  // We know the file is at least as big as the header.  Return it.
  return *reinterpret_cast<const HMapHeader*>(FileBuffer->getBufferStart());
}

/// getBucket - Return the specified hash table bucket from the header map,
/// bswap'ing its fields as appropriate.  If the bucket number is not valid,
/// this return a bucket with an empty key (0).
HMapBucket HeaderMapImpl::getBucket(unsigned BucketNo) const {
  assert(FileBuffer->getBufferSize() >=
             sizeof(HMapHeader) + sizeof(HMapBucket) * BucketNo &&
         "Expected bucket to be in range");

  HMapBucket Result;
  Result.Key = HMAP_EmptyBucketKey;

  const HMapBucket *BucketArray =
    reinterpret_cast<const HMapBucket*>(FileBuffer->getBufferStart() +
                                        sizeof(HMapHeader));
  const HMapBucket *BucketPtr = BucketArray+BucketNo;

  // Load the values, bswapping as needed.
  Result.Key    = getEndianAdjustedWord(BucketPtr->Key);
  Result.Prefix = getEndianAdjustedWord(BucketPtr->Prefix);
  Result.Suffix = getEndianAdjustedWord(BucketPtr->Suffix);
  return Result;
}

Optional<StringRef> HeaderMapImpl::getString(unsigned StrTabIdx) const {
  // Add the start of the string table to the idx.
  StrTabIdx += getEndianAdjustedWord(getHeader().StringsOffset);

  // Check for invalid index.
  if (StrTabIdx >= FileBuffer->getBufferSize())
    return None;

  const char *Data = FileBuffer->getBufferStart() + StrTabIdx;
  unsigned MaxLen = FileBuffer->getBufferSize() - StrTabIdx;
  unsigned Len = strnlen(Data, MaxLen);

  // Check whether the buffer is null-terminated.
  if (Len == MaxLen && Data[Len - 1])
    return None;

  return StringRef(Data, Len);
}

//===----------------------------------------------------------------------===//
// The Main Drivers
//===----------------------------------------------------------------------===//

/// dump - Print the contents of this headermap to stderr.
LLVM_DUMP_METHOD void HeaderMapImpl::dump() const {
  const HMapHeader &Hdr = getHeader();
  unsigned NumBuckets = getEndianAdjustedWord(Hdr.NumBuckets);

  llvm::dbgs() << "Header Map " << getFileName() << ":\n  " << NumBuckets
               << ", " << getEndianAdjustedWord(Hdr.NumEntries) << "\n";

  auto getStringOrInvalid = [this](unsigned Id) -> StringRef {
    if (Optional<StringRef> S = getString(Id))
      return *S;
    return "<invalid>";
  };

  for (unsigned i = 0; i != NumBuckets; ++i) {
    HMapBucket B = getBucket(i);
    if (B.Key == HMAP_EmptyBucketKey) continue;

    StringRef Key = getStringOrInvalid(B.Key);
    StringRef Prefix = getStringOrInvalid(B.Prefix);
    StringRef Suffix = getStringOrInvalid(B.Suffix);
    llvm::dbgs() << "  " << i << ". " << Key << " -> '" << Prefix << "' '"
                 << Suffix << "'\n";
  }
}

/// LookupFile - Check to see if the specified relative filename is located in
/// this HeaderMap.  If so, open it and return its FileEntry.
const FileEntry *HeaderMap::LookupFile(
    StringRef Filename, FileManager &FM) const {

  SmallString<1024> Path;
  StringRef Dest = HeaderMapImpl::lookupFilename(Filename, Path);
  if (Dest.empty())
    return nullptr;

  return FM.getFile(Dest);
}

StringRef HeaderMapImpl::lookupFilename(StringRef Filename,
                                        SmallVectorImpl<char> &DestPath) const {
  const HMapHeader &Hdr = getHeader();
  unsigned NumBuckets = getEndianAdjustedWord(Hdr.NumBuckets);

  // Don't probe infinitely.  This should be checked before constructing.
  assert(llvm::isPowerOf2_32(NumBuckets) && "Expected power of 2");

  // Linearly probe the hash table.
  for (unsigned Bucket = HashHMapKey(Filename);; ++Bucket) {
    HMapBucket B = getBucket(Bucket & (NumBuckets-1));
    if (B.Key == HMAP_EmptyBucketKey) return StringRef(); // Hash miss.

    // See if the key matches.  If not, probe on.
    Optional<StringRef> Key = getString(B.Key);
    if (LLVM_UNLIKELY(!Key))
      continue;
    if (!Filename.equals_lower(*Key))
      continue;

    // If so, we have a match in the hash table.  Construct the destination
    // path.
    Optional<StringRef> Prefix = getString(B.Prefix);
    Optional<StringRef> Suffix = getString(B.Suffix);

    DestPath.clear();
    if (LLVM_LIKELY(Prefix && Suffix)) {
      DestPath.append(Prefix->begin(), Prefix->end());
      DestPath.append(Suffix->begin(), Suffix->end());
    }
    return StringRef(DestPath.begin(), DestPath.size());
  }
}
