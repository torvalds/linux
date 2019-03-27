//===- MSFBuilder.h - MSF Directory & Metadata Builder ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_MSFBUILDER_H
#define LLVM_DEBUGINFO_MSF_MSFBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace llvm {
class FileBufferByteStream;
class WritableBinaryStream;
namespace msf {

class MSFBuilder {
public:
  /// Create a new `MSFBuilder`.
  ///
  /// \param BlockSize The internal block size used by the PDB file.  See
  /// isValidBlockSize() for a list of valid block sizes.
  ///
  /// \param MinBlockCount Causes the builder to reserve up front space for
  /// at least `MinBlockCount` blocks.  This is useful when using `MSFBuilder`
  /// to read an existing MSF that you want to write back out later.  The
  /// original MSF file's SuperBlock contains the exact number of blocks used
  /// by the file, so is a good hint as to how many blocks the new MSF file
  /// will contain.  Furthermore, it is actually necessary in this case.  To
  /// preserve stability of the file's layout, it is helpful to try to keep
  /// all streams mapped to their original block numbers.  To ensure that this
  /// is possible, space for all blocks must be allocated beforehand so that
  /// streams can be assigned to them.
  ///
  /// \param CanGrow If true, any operation which results in an attempt to
  /// locate a free block when all available blocks have been exhausted will
  /// allocate a new block, thereby growing the size of the final MSF file.
  /// When false, any such attempt will result in an error.  This is especially
  /// useful in testing scenarios when you know your test isn't going to do
  /// anything to increase the size of the file, so having an Error returned if
  /// it were to happen would catch a programming error
  ///
  /// \returns an llvm::Error representing whether the operation succeeded or
  /// failed.  Currently the only way this can fail is if an invalid block size
  /// is specified, or `MinBlockCount` does not leave enough room for the
  /// mandatory reserved blocks required by an MSF file.
  static Expected<MSFBuilder> create(BumpPtrAllocator &Allocator,
                                     uint32_t BlockSize,
                                     uint32_t MinBlockCount = 0,
                                     bool CanGrow = true);

  /// Request the block map to be at a specific block address.  This is useful
  /// when editing a MSF and you want the layout to be as stable as possible.
  Error setBlockMapAddr(uint32_t Addr);
  Error setDirectoryBlocksHint(ArrayRef<uint32_t> DirBlocks);
  void setFreePageMap(uint32_t Fpm);
  void setUnknown1(uint32_t Unk1);

  /// Add a stream to the MSF file with the given size, occupying the given
  /// list of blocks.  This is useful when reading a MSF file and you want a
  /// particular stream to occupy the original set of blocks.  If the given
  /// blocks are already allocated, or if the number of blocks specified is
  /// incorrect for the given stream size, this function will return an Error.
  Expected<uint32_t> addStream(uint32_t Size, ArrayRef<uint32_t> Blocks);

  /// Add a stream to the MSF file with the given size, occupying any available
  /// blocks that the builder decides to use.  This is useful when building a
  /// new PDB file from scratch and you don't care what blocks a stream occupies
  /// but you just want it to work.
  Expected<uint32_t> addStream(uint32_t Size);

  /// Update the size of an existing stream.  This will allocate or deallocate
  /// blocks as needed to match the requested size.  This can fail if `CanGrow`
  /// was set to false when initializing the `MSFBuilder`.
  Error setStreamSize(uint32_t Idx, uint32_t Size);

  /// Get the total number of streams in the MSF layout.  This should return 1
  /// for every call to `addStream`.
  uint32_t getNumStreams() const;

  /// Get the size of a stream by index.
  uint32_t getStreamSize(uint32_t StreamIdx) const;

  /// Get the list of blocks allocated to a particular stream.
  ArrayRef<uint32_t> getStreamBlocks(uint32_t StreamIdx) const;

  /// Get the total number of blocks that will be allocated to actual data in
  /// this MSF file.
  uint32_t getNumUsedBlocks() const;

  /// Get the total number of blocks that exist in the MSF file but are not
  /// allocated to any valid data.
  uint32_t getNumFreeBlocks() const;

  /// Get the total number of blocks in the MSF file.  In practice this is equal
  /// to `getNumUsedBlocks() + getNumFreeBlocks()`.
  uint32_t getTotalBlockCount() const;

  /// Check whether a particular block is allocated or free.
  bool isBlockFree(uint32_t Idx) const;

  /// Finalize the layout and build the headers and structures that describe the
  /// MSF layout and can be written directly to the MSF file.
  Expected<MSFLayout> generateLayout();

  /// Write the MSF layout to the underlying file.
  Expected<FileBufferByteStream> commit(StringRef Path, MSFLayout &Layout);

  BumpPtrAllocator &getAllocator() { return Allocator; }

private:
  MSFBuilder(uint32_t BlockSize, uint32_t MinBlockCount, bool CanGrow,
             BumpPtrAllocator &Allocator);

  Error allocateBlocks(uint32_t NumBlocks, MutableArrayRef<uint32_t> Blocks);
  uint32_t computeDirectoryByteSize() const;

  using BlockList = std::vector<uint32_t>;

  BumpPtrAllocator &Allocator;

  bool IsGrowable;
  uint32_t FreePageMap;
  uint32_t Unknown1 = 0;
  uint32_t BlockSize;
  uint32_t BlockMapAddr;
  BitVector FreeBlocks;
  std::vector<uint32_t> DirectoryBlocks;
  std::vector<std::pair<uint32_t, BlockList>> StreamData;
};

} // end namespace msf
} // end namespace llvm

#endif // LLVM_DEBUGINFO_MSF_MSFBUILDER_H
