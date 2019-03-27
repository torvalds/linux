//===- MSFBuilder.cpp -----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/MSF/MSFError.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::support;

static const uint32_t kSuperBlockBlock = 0;
static const uint32_t kFreePageMap0Block = 1;
static const uint32_t kFreePageMap1Block = 2;
static const uint32_t kNumReservedPages = 3;

static const uint32_t kDefaultFreePageMap = kFreePageMap1Block;
static const uint32_t kDefaultBlockMapAddr = kNumReservedPages;

MSFBuilder::MSFBuilder(uint32_t BlockSize, uint32_t MinBlockCount, bool CanGrow,
                       BumpPtrAllocator &Allocator)
    : Allocator(Allocator), IsGrowable(CanGrow),
      FreePageMap(kDefaultFreePageMap), BlockSize(BlockSize),
      BlockMapAddr(kDefaultBlockMapAddr), FreeBlocks(MinBlockCount, true) {
  FreeBlocks[kSuperBlockBlock] = false;
  FreeBlocks[kFreePageMap0Block] = false;
  FreeBlocks[kFreePageMap1Block] = false;
  FreeBlocks[BlockMapAddr] = false;
}

Expected<MSFBuilder> MSFBuilder::create(BumpPtrAllocator &Allocator,
                                        uint32_t BlockSize,
                                        uint32_t MinBlockCount, bool CanGrow) {
  if (!isValidBlockSize(BlockSize))
    return make_error<MSFError>(msf_error_code::invalid_format,
                                "The requested block size is unsupported");

  return MSFBuilder(BlockSize,
                    std::max(MinBlockCount, msf::getMinimumBlockCount()),
                    CanGrow, Allocator);
}

Error MSFBuilder::setBlockMapAddr(uint32_t Addr) {
  if (Addr == BlockMapAddr)
    return Error::success();

  if (Addr >= FreeBlocks.size()) {
    if (!IsGrowable)
      return make_error<MSFError>(msf_error_code::insufficient_buffer,
                                  "Cannot grow the number of blocks");
    FreeBlocks.resize(Addr + 1, true);
  }

  if (!isBlockFree(Addr))
    return make_error<MSFError>(
        msf_error_code::block_in_use,
        "Requested block map address is already in use");
  FreeBlocks[BlockMapAddr] = true;
  FreeBlocks[Addr] = false;
  BlockMapAddr = Addr;
  return Error::success();
}

void MSFBuilder::setFreePageMap(uint32_t Fpm) { FreePageMap = Fpm; }

void MSFBuilder::setUnknown1(uint32_t Unk1) { Unknown1 = Unk1; }

Error MSFBuilder::setDirectoryBlocksHint(ArrayRef<uint32_t> DirBlocks) {
  for (auto B : DirectoryBlocks)
    FreeBlocks[B] = true;
  for (auto B : DirBlocks) {
    if (!isBlockFree(B)) {
      return make_error<MSFError>(msf_error_code::unspecified,
                                  "Attempt to reuse an allocated block");
    }
    FreeBlocks[B] = false;
  }

  DirectoryBlocks = DirBlocks;
  return Error::success();
}

Error MSFBuilder::allocateBlocks(uint32_t NumBlocks,
                                 MutableArrayRef<uint32_t> Blocks) {
  if (NumBlocks == 0)
    return Error::success();

  uint32_t NumFreeBlocks = FreeBlocks.count();
  if (NumFreeBlocks < NumBlocks) {
    if (!IsGrowable)
      return make_error<MSFError>(msf_error_code::insufficient_buffer,
                                  "There are no free Blocks in the file");
    uint32_t AllocBlocks = NumBlocks - NumFreeBlocks;
    uint32_t OldBlockCount = FreeBlocks.size();
    uint32_t NewBlockCount = AllocBlocks + OldBlockCount;
    uint32_t NextFpmBlock = alignTo(OldBlockCount, BlockSize) + 1;
    FreeBlocks.resize(NewBlockCount, true);
    // If we crossed over an fpm page, we actually need to allocate 2 extra
    // blocks for each FPM group crossed and mark both blocks from the group as
    // used.  FPM blocks are marked as allocated regardless of whether or not
    // they ultimately describe the status of blocks in the file.  This means
    // that not only are extraneous blocks at the end of the main FPM marked as
    // allocated, but also blocks from the alternate FPM are always marked as
    // allocated.
    while (NextFpmBlock < NewBlockCount) {
      NewBlockCount += 2;
      FreeBlocks.resize(NewBlockCount, true);
      FreeBlocks.reset(NextFpmBlock, NextFpmBlock + 2);
      NextFpmBlock += BlockSize;
    }
  }

  int I = 0;
  int Block = FreeBlocks.find_first();
  do {
    assert(Block != -1 && "We ran out of Blocks!");

    uint32_t NextBlock = static_cast<uint32_t>(Block);
    Blocks[I++] = NextBlock;
    FreeBlocks.reset(NextBlock);
    Block = FreeBlocks.find_next(Block);
  } while (--NumBlocks > 0);
  return Error::success();
}

uint32_t MSFBuilder::getNumUsedBlocks() const {
  return getTotalBlockCount() - getNumFreeBlocks();
}

uint32_t MSFBuilder::getNumFreeBlocks() const { return FreeBlocks.count(); }

uint32_t MSFBuilder::getTotalBlockCount() const { return FreeBlocks.size(); }

bool MSFBuilder::isBlockFree(uint32_t Idx) const { return FreeBlocks[Idx]; }

Expected<uint32_t> MSFBuilder::addStream(uint32_t Size,
                                         ArrayRef<uint32_t> Blocks) {
  // Add a new stream mapped to the specified blocks.  Verify that the specified
  // blocks are both necessary and sufficient for holding the requested number
  // of bytes, and verify that all requested blocks are free.
  uint32_t ReqBlocks = bytesToBlocks(Size, BlockSize);
  if (ReqBlocks != Blocks.size())
    return make_error<MSFError>(
        msf_error_code::invalid_format,
        "Incorrect number of blocks for requested stream size");
  for (auto Block : Blocks) {
    if (Block >= FreeBlocks.size())
      FreeBlocks.resize(Block + 1, true);

    if (!FreeBlocks.test(Block))
      return make_error<MSFError>(
          msf_error_code::unspecified,
          "Attempt to re-use an already allocated block");
  }
  // Mark all the blocks occupied by the new stream as not free.
  for (auto Block : Blocks) {
    FreeBlocks.reset(Block);
  }
  StreamData.push_back(std::make_pair(Size, Blocks));
  return StreamData.size() - 1;
}

Expected<uint32_t> MSFBuilder::addStream(uint32_t Size) {
  uint32_t ReqBlocks = bytesToBlocks(Size, BlockSize);
  std::vector<uint32_t> NewBlocks;
  NewBlocks.resize(ReqBlocks);
  if (auto EC = allocateBlocks(ReqBlocks, NewBlocks))
    return std::move(EC);
  StreamData.push_back(std::make_pair(Size, NewBlocks));
  return StreamData.size() - 1;
}

Error MSFBuilder::setStreamSize(uint32_t Idx, uint32_t Size) {
  uint32_t OldSize = getStreamSize(Idx);
  if (OldSize == Size)
    return Error::success();

  uint32_t NewBlocks = bytesToBlocks(Size, BlockSize);
  uint32_t OldBlocks = bytesToBlocks(OldSize, BlockSize);

  if (NewBlocks > OldBlocks) {
    uint32_t AddedBlocks = NewBlocks - OldBlocks;
    // If we're growing, we have to allocate new Blocks.
    std::vector<uint32_t> AddedBlockList;
    AddedBlockList.resize(AddedBlocks);
    if (auto EC = allocateBlocks(AddedBlocks, AddedBlockList))
      return EC;
    auto &CurrentBlocks = StreamData[Idx].second;
    CurrentBlocks.insert(CurrentBlocks.end(), AddedBlockList.begin(),
                         AddedBlockList.end());
  } else if (OldBlocks > NewBlocks) {
    // For shrinking, free all the Blocks in the Block map, update the stream
    // data, then shrink the directory.
    uint32_t RemovedBlocks = OldBlocks - NewBlocks;
    auto CurrentBlocks = ArrayRef<uint32_t>(StreamData[Idx].second);
    auto RemovedBlockList = CurrentBlocks.drop_front(NewBlocks);
    for (auto P : RemovedBlockList)
      FreeBlocks[P] = true;
    StreamData[Idx].second = CurrentBlocks.drop_back(RemovedBlocks);
  }

  StreamData[Idx].first = Size;
  return Error::success();
}

uint32_t MSFBuilder::getNumStreams() const { return StreamData.size(); }

uint32_t MSFBuilder::getStreamSize(uint32_t StreamIdx) const {
  return StreamData[StreamIdx].first;
}

ArrayRef<uint32_t> MSFBuilder::getStreamBlocks(uint32_t StreamIdx) const {
  return StreamData[StreamIdx].second;
}

uint32_t MSFBuilder::computeDirectoryByteSize() const {
  // The directory has the following layout, where each item is a ulittle32_t:
  //    NumStreams
  //    StreamSizes[NumStreams]
  //    StreamBlocks[NumStreams][]
  uint32_t Size = sizeof(ulittle32_t);             // NumStreams
  Size += StreamData.size() * sizeof(ulittle32_t); // StreamSizes
  for (const auto &D : StreamData) {
    uint32_t ExpectedNumBlocks = bytesToBlocks(D.first, BlockSize);
    assert(ExpectedNumBlocks == D.second.size() &&
           "Unexpected number of blocks");
    Size += ExpectedNumBlocks * sizeof(ulittle32_t);
  }
  return Size;
}

Expected<MSFLayout> MSFBuilder::generateLayout() {
  SuperBlock *SB = Allocator.Allocate<SuperBlock>();
  MSFLayout L;
  L.SB = SB;

  std::memcpy(SB->MagicBytes, Magic, sizeof(Magic));
  SB->BlockMapAddr = BlockMapAddr;
  SB->BlockSize = BlockSize;
  SB->NumDirectoryBytes = computeDirectoryByteSize();
  SB->FreeBlockMapBlock = FreePageMap;
  SB->Unknown1 = Unknown1;

  uint32_t NumDirectoryBlocks = bytesToBlocks(SB->NumDirectoryBytes, BlockSize);
  if (NumDirectoryBlocks > DirectoryBlocks.size()) {
    // Our hint wasn't enough to satisfy the entire directory.  Allocate
    // remaining pages.
    std::vector<uint32_t> ExtraBlocks;
    uint32_t NumExtraBlocks = NumDirectoryBlocks - DirectoryBlocks.size();
    ExtraBlocks.resize(NumExtraBlocks);
    if (auto EC = allocateBlocks(NumExtraBlocks, ExtraBlocks))
      return std::move(EC);
    DirectoryBlocks.insert(DirectoryBlocks.end(), ExtraBlocks.begin(),
                           ExtraBlocks.end());
  } else if (NumDirectoryBlocks < DirectoryBlocks.size()) {
    uint32_t NumUnnecessaryBlocks = DirectoryBlocks.size() - NumDirectoryBlocks;
    for (auto B :
         ArrayRef<uint32_t>(DirectoryBlocks).drop_back(NumUnnecessaryBlocks))
      FreeBlocks[B] = true;
    DirectoryBlocks.resize(NumDirectoryBlocks);
  }

  // Don't set the number of blocks in the file until after allocating Blocks
  // for the directory, since the allocation might cause the file to need to
  // grow.
  SB->NumBlocks = FreeBlocks.size();

  ulittle32_t *DirBlocks = Allocator.Allocate<ulittle32_t>(NumDirectoryBlocks);
  std::uninitialized_copy_n(DirectoryBlocks.begin(), NumDirectoryBlocks,
                            DirBlocks);
  L.DirectoryBlocks = ArrayRef<ulittle32_t>(DirBlocks, NumDirectoryBlocks);

  // The stream sizes should be re-allocated as a stable pointer and the stream
  // map should have each of its entries allocated as a separate stable pointer.
  if (!StreamData.empty()) {
    ulittle32_t *Sizes = Allocator.Allocate<ulittle32_t>(StreamData.size());
    L.StreamSizes = ArrayRef<ulittle32_t>(Sizes, StreamData.size());
    L.StreamMap.resize(StreamData.size());
    for (uint32_t I = 0; I < StreamData.size(); ++I) {
      Sizes[I] = StreamData[I].first;
      ulittle32_t *BlockList =
          Allocator.Allocate<ulittle32_t>(StreamData[I].second.size());
      std::uninitialized_copy_n(StreamData[I].second.begin(),
                                StreamData[I].second.size(), BlockList);
      L.StreamMap[I] =
          ArrayRef<ulittle32_t>(BlockList, StreamData[I].second.size());
    }
  }

  L.FreePageMap = FreeBlocks;

  return L;
}

static void commitFpm(WritableBinaryStream &MsfBuffer, const MSFLayout &Layout,
                      BumpPtrAllocator &Allocator) {
  auto FpmStream =
      WritableMappedBlockStream::createFpmStream(Layout, MsfBuffer, Allocator);

  // We only need to create the alt fpm stream so that it gets initialized.
  WritableMappedBlockStream::createFpmStream(Layout, MsfBuffer, Allocator,
                                             true);

  uint32_t BI = 0;
  BinaryStreamWriter FpmWriter(*FpmStream);
  while (BI < Layout.SB->NumBlocks) {
    uint8_t ThisByte = 0;
    for (uint32_t I = 0; I < 8; ++I) {
      bool IsFree =
          (BI < Layout.SB->NumBlocks) ? Layout.FreePageMap.test(BI) : true;
      uint8_t Mask = uint8_t(IsFree) << I;
      ThisByte |= Mask;
      ++BI;
    }
    cantFail(FpmWriter.writeObject(ThisByte));
  }
  assert(FpmWriter.bytesRemaining() == 0);
}

Expected<FileBufferByteStream> MSFBuilder::commit(StringRef Path,
                                                  MSFLayout &Layout) {
  Expected<MSFLayout> L = generateLayout();
  if (!L)
    return L.takeError();

  Layout = std::move(*L);

  uint64_t FileSize = Layout.SB->BlockSize * Layout.SB->NumBlocks;
  auto OutFileOrError = FileOutputBuffer::create(Path, FileSize);
  if (auto EC = OutFileOrError.takeError())
    return std::move(EC);

  FileBufferByteStream Buffer(std::move(*OutFileOrError),
                              llvm::support::little);
  BinaryStreamWriter Writer(Buffer);

  if (auto EC = Writer.writeObject(*Layout.SB))
    return std::move(EC);

  commitFpm(Buffer, Layout, Allocator);

  uint32_t BlockMapOffset =
      msf::blockToOffset(Layout.SB->BlockMapAddr, Layout.SB->BlockSize);
  Writer.setOffset(BlockMapOffset);
  if (auto EC = Writer.writeArray(Layout.DirectoryBlocks))
    return std::move(EC);

  auto DirStream = WritableMappedBlockStream::createDirectoryStream(
      Layout, Buffer, Allocator);
  BinaryStreamWriter DW(*DirStream);
  if (auto EC = DW.writeInteger<uint32_t>(Layout.StreamSizes.size()))
    return std::move(EC);

  if (auto EC = DW.writeArray(Layout.StreamSizes))
    return std::move(EC);

  for (const auto &Blocks : Layout.StreamMap) {
    if (auto EC = DW.writeArray(Blocks))
      return std::move(EC);
  }

  return std::move(Buffer);
}
