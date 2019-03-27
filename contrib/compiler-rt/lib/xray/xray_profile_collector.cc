//===-- xray_profile_collector.cc ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// This implements the interface for the profileCollectorService.
//
//===----------------------------------------------------------------------===//
#include "xray_profile_collector.h"
#include "sanitizer_common/sanitizer_common.h"
#include "xray_allocator.h"
#include "xray_defs.h"
#include "xray_profiling_flags.h"
#include "xray_segmented_array.h"
#include <memory>
#include <pthread.h>
#include <utility>

namespace __xray {
namespace profileCollectorService {

namespace {

SpinMutex GlobalMutex;
struct ThreadTrie {
  tid_t TId;
  typename std::aligned_storage<sizeof(FunctionCallTrie)>::type TrieStorage;
};

struct ProfileBuffer {
  void *Data;
  size_t Size;
};

// Current version of the profile format.
constexpr u64 XRayProfilingVersion = 0x20180424;

// Identifier for XRay profiling files 'xrayprof' in hex.
constexpr u64 XRayMagicBytes = 0x7872617970726f66;

struct XRayProfilingFileHeader {
  const u64 MagicBytes = XRayMagicBytes;
  const u64 Version = XRayProfilingVersion;
  u64 Timestamp = 0; // System time in nanoseconds.
  u64 PID = 0;       // Process ID.
};

struct BlockHeader {
  u32 BlockSize;
  u32 BlockNum;
  u64 ThreadId;
};

struct ThreadData {
  BufferQueue *BQ;
  FunctionCallTrie::Allocators::Buffers Buffers;
  FunctionCallTrie::Allocators Allocators;
  FunctionCallTrie FCT;
  tid_t TId;
};

using ThreadDataArray = Array<ThreadData>;
using ThreadDataAllocator = ThreadDataArray::AllocatorType;

// We use a separate buffer queue for the backing store for the allocator used
// by the ThreadData array. This lets us host the buffers, allocators, and tries
// associated with a thread by moving the data into the array instead of
// attempting to copy the data to a separately backed set of tries.
static typename std::aligned_storage<
    sizeof(BufferQueue), alignof(BufferQueue)>::type BufferQueueStorage;
static BufferQueue *BQ = nullptr;
static BufferQueue::Buffer Buffer;
static typename std::aligned_storage<sizeof(ThreadDataAllocator),
                                     alignof(ThreadDataAllocator)>::type
    ThreadDataAllocatorStorage;
static typename std::aligned_storage<sizeof(ThreadDataArray),
                                     alignof(ThreadDataArray)>::type
    ThreadDataArrayStorage;

static ThreadDataAllocator *TDAllocator = nullptr;
static ThreadDataArray *TDArray = nullptr;

using ProfileBufferArray = Array<ProfileBuffer>;
using ProfileBufferArrayAllocator = typename ProfileBufferArray::AllocatorType;

// These need to be global aligned storage to avoid dynamic initialization. We
// need these to be aligned to allow us to placement new objects into the
// storage, and have pointers to those objects be appropriately aligned.
static typename std::aligned_storage<sizeof(ProfileBufferArray)>::type
    ProfileBuffersStorage;
static typename std::aligned_storage<sizeof(ProfileBufferArrayAllocator)>::type
    ProfileBufferArrayAllocatorStorage;

static ProfileBufferArrayAllocator *ProfileBuffersAllocator = nullptr;
static ProfileBufferArray *ProfileBuffers = nullptr;

// Use a global flag to determine whether the collector implementation has been
// initialized.
static atomic_uint8_t CollectorInitialized{0};

} // namespace

void post(BufferQueue *Q, FunctionCallTrie &&T,
          FunctionCallTrie::Allocators &&A,
          FunctionCallTrie::Allocators::Buffers &&B,
          tid_t TId) XRAY_NEVER_INSTRUMENT {
  DCHECK_NE(Q, nullptr);

  // Bail out early if the collector has not been initialized.
  if (!atomic_load(&CollectorInitialized, memory_order_acquire)) {
    T.~FunctionCallTrie();
    A.~Allocators();
    Q->releaseBuffer(B.NodeBuffer);
    Q->releaseBuffer(B.RootsBuffer);
    Q->releaseBuffer(B.ShadowStackBuffer);
    Q->releaseBuffer(B.NodeIdPairBuffer);
    B.~Buffers();
    return;
  }

  {
    SpinMutexLock Lock(&GlobalMutex);
    DCHECK_NE(TDAllocator, nullptr);
    DCHECK_NE(TDArray, nullptr);

    if (TDArray->AppendEmplace(Q, std::move(B), std::move(A), std::move(T),
                               TId) == nullptr) {
      // If we fail to add the data to the array, we should destroy the objects
      // handed us.
      T.~FunctionCallTrie();
      A.~Allocators();
      Q->releaseBuffer(B.NodeBuffer);
      Q->releaseBuffer(B.RootsBuffer);
      Q->releaseBuffer(B.ShadowStackBuffer);
      Q->releaseBuffer(B.NodeIdPairBuffer);
      B.~Buffers();
    }
  }
}

// A PathArray represents the function id's representing a stack trace. In this
// context a path is almost always represented from the leaf function in a call
// stack to a root of the call trie.
using PathArray = Array<int32_t>;

struct ProfileRecord {
  using PathAllocator = typename PathArray::AllocatorType;

  // The Path in this record is the function id's from the leaf to the root of
  // the function call stack as represented from a FunctionCallTrie.
  PathArray Path;
  const FunctionCallTrie::Node *Node;
};

namespace {

using ProfileRecordArray = Array<ProfileRecord>;

// Walk a depth-first traversal of each root of the FunctionCallTrie to generate
// the path(s) and the data associated with the path.
static void
populateRecords(ProfileRecordArray &PRs, ProfileRecord::PathAllocator &PA,
                const FunctionCallTrie &Trie) XRAY_NEVER_INSTRUMENT {
  using StackArray = Array<const FunctionCallTrie::Node *>;
  using StackAllocator = typename StackArray::AllocatorType;
  StackAllocator StackAlloc(profilingFlags()->stack_allocator_max);
  StackArray DFSStack(StackAlloc);
  for (const auto *R : Trie.getRoots()) {
    DFSStack.Append(R);
    while (!DFSStack.empty()) {
      auto *Node = DFSStack.back();
      DFSStack.trim(1);
      if (Node == nullptr)
        continue;
      auto Record = PRs.AppendEmplace(PathArray{PA}, Node);
      if (Record == nullptr)
        return;
      DCHECK_NE(Record, nullptr);

      // Traverse the Node's parents and as we're doing so, get the FIds in
      // the order they appear.
      for (auto N = Node; N != nullptr; N = N->Parent)
        Record->Path.Append(N->FId);
      DCHECK(!Record->Path.empty());

      for (const auto C : Node->Callees)
        DFSStack.Append(C.NodePtr);
    }
  }
}

static void serializeRecords(ProfileBuffer *Buffer, const BlockHeader &Header,
                             const ProfileRecordArray &ProfileRecords)
    XRAY_NEVER_INSTRUMENT {
  auto NextPtr = static_cast<uint8_t *>(
                     internal_memcpy(Buffer->Data, &Header, sizeof(Header))) +
                 sizeof(Header);
  for (const auto &Record : ProfileRecords) {
    // List of IDs follow:
    for (const auto FId : Record.Path)
      NextPtr =
          static_cast<uint8_t *>(internal_memcpy(NextPtr, &FId, sizeof(FId))) +
          sizeof(FId);

    // Add the sentinel here.
    constexpr int32_t SentinelFId = 0;
    NextPtr = static_cast<uint8_t *>(
                  internal_memset(NextPtr, SentinelFId, sizeof(SentinelFId))) +
              sizeof(SentinelFId);

    // Add the node data here.
    NextPtr =
        static_cast<uint8_t *>(internal_memcpy(
            NextPtr, &Record.Node->CallCount, sizeof(Record.Node->CallCount))) +
        sizeof(Record.Node->CallCount);
    NextPtr = static_cast<uint8_t *>(
                  internal_memcpy(NextPtr, &Record.Node->CumulativeLocalTime,
                                  sizeof(Record.Node->CumulativeLocalTime))) +
              sizeof(Record.Node->CumulativeLocalTime);
  }

  DCHECK_EQ(NextPtr - static_cast<uint8_t *>(Buffer->Data), Buffer->Size);
}

} // namespace

void serialize() XRAY_NEVER_INSTRUMENT {
  if (!atomic_load(&CollectorInitialized, memory_order_acquire))
    return;

  SpinMutexLock Lock(&GlobalMutex);

  // Clear out the global ProfileBuffers, if it's not empty.
  for (auto &B : *ProfileBuffers)
    deallocateBuffer(reinterpret_cast<unsigned char *>(B.Data), B.Size);
  ProfileBuffers->trim(ProfileBuffers->size());

  DCHECK_NE(TDArray, nullptr);
  if (TDArray->empty())
    return;

  // Then repopulate the global ProfileBuffers.
  u32 I = 0;
  auto MaxSize = profilingFlags()->global_allocator_max;
  auto ProfileArena = allocateBuffer(MaxSize);
  if (ProfileArena == nullptr)
    return;

  auto ProfileArenaCleanup = at_scope_exit(
      [&]() XRAY_NEVER_INSTRUMENT { deallocateBuffer(ProfileArena, MaxSize); });

  auto PathArena = allocateBuffer(profilingFlags()->global_allocator_max);
  if (PathArena == nullptr)
    return;

  auto PathArenaCleanup = at_scope_exit(
      [&]() XRAY_NEVER_INSTRUMENT { deallocateBuffer(PathArena, MaxSize); });

  for (const auto &ThreadTrie : *TDArray) {
    using ProfileRecordAllocator = typename ProfileRecordArray::AllocatorType;
    ProfileRecordAllocator PRAlloc(ProfileArena,
                                   profilingFlags()->global_allocator_max);
    ProfileRecord::PathAllocator PathAlloc(
        PathArena, profilingFlags()->global_allocator_max);
    ProfileRecordArray ProfileRecords(PRAlloc);

    // First, we want to compute the amount of space we're going to need. We'll
    // use a local allocator and an __xray::Array<...> to store the intermediary
    // data, then compute the size as we're going along. Then we'll allocate the
    // contiguous space to contain the thread buffer data.
    if (ThreadTrie.FCT.getRoots().empty())
      continue;

    populateRecords(ProfileRecords, PathAlloc, ThreadTrie.FCT);
    DCHECK(!ThreadTrie.FCT.getRoots().empty());
    DCHECK(!ProfileRecords.empty());

    // Go through each record, to compute the sizes.
    //
    // header size = block size (4 bytes)
    //   + block number (4 bytes)
    //   + thread id (8 bytes)
    // record size = path ids (4 bytes * number of ids + sentinel 4 bytes)
    //   + call count (8 bytes)
    //   + local time (8 bytes)
    //   + end of record (8 bytes)
    u32 CumulativeSizes = 0;
    for (const auto &Record : ProfileRecords)
      CumulativeSizes += 20 + (4 * Record.Path.size());

    BlockHeader Header{16 + CumulativeSizes, I++, ThreadTrie.TId};
    auto B = ProfileBuffers->Append({});
    B->Size = sizeof(Header) + CumulativeSizes;
    B->Data = allocateBuffer(B->Size);
    DCHECK_NE(B->Data, nullptr);
    serializeRecords(B, Header, ProfileRecords);
  }
}

void reset() XRAY_NEVER_INSTRUMENT {
  atomic_store(&CollectorInitialized, 0, memory_order_release);
  SpinMutexLock Lock(&GlobalMutex);

  if (ProfileBuffers != nullptr) {
    // Clear out the profile buffers that have been serialized.
    for (auto &B : *ProfileBuffers)
      deallocateBuffer(reinterpret_cast<uint8_t *>(B.Data), B.Size);
    ProfileBuffers->trim(ProfileBuffers->size());
    ProfileBuffers = nullptr;
  }

  if (TDArray != nullptr) {
    // Release the resources as required.
    for (auto &TD : *TDArray) {
      TD.BQ->releaseBuffer(TD.Buffers.NodeBuffer);
      TD.BQ->releaseBuffer(TD.Buffers.RootsBuffer);
      TD.BQ->releaseBuffer(TD.Buffers.ShadowStackBuffer);
      TD.BQ->releaseBuffer(TD.Buffers.NodeIdPairBuffer);
    }
    // We don't bother destroying the array here because we've already
    // potentially freed the backing store for the array. Instead we're going to
    // reset the pointer to nullptr, and re-use the storage later instead
    // (placement-new'ing into the storage as-is).
    TDArray = nullptr;
  }

  if (TDAllocator != nullptr) {
    TDAllocator->~Allocator();
    TDAllocator = nullptr;
  }

  if (Buffer.Data != nullptr) {
    BQ->releaseBuffer(Buffer);
  }

  if (BQ == nullptr) {
    bool Success = false;
    new (&BufferQueueStorage)
        BufferQueue(profilingFlags()->global_allocator_max, 1, Success);
    if (!Success)
      return;
    BQ = reinterpret_cast<BufferQueue *>(&BufferQueueStorage);
  } else {
    BQ->finalize();

    if (BQ->init(profilingFlags()->global_allocator_max, 1) !=
        BufferQueue::ErrorCode::Ok)
      return;
  }

  if (BQ->getBuffer(Buffer) != BufferQueue::ErrorCode::Ok)
    return;

  new (&ProfileBufferArrayAllocatorStorage)
      ProfileBufferArrayAllocator(profilingFlags()->global_allocator_max);
  ProfileBuffersAllocator = reinterpret_cast<ProfileBufferArrayAllocator *>(
      &ProfileBufferArrayAllocatorStorage);

  new (&ProfileBuffersStorage) ProfileBufferArray(*ProfileBuffersAllocator);
  ProfileBuffers =
      reinterpret_cast<ProfileBufferArray *>(&ProfileBuffersStorage);

  new (&ThreadDataAllocatorStorage)
      ThreadDataAllocator(Buffer.Data, Buffer.Size);
  TDAllocator =
      reinterpret_cast<ThreadDataAllocator *>(&ThreadDataAllocatorStorage);
  new (&ThreadDataArrayStorage) ThreadDataArray(*TDAllocator);
  TDArray = reinterpret_cast<ThreadDataArray *>(&ThreadDataArrayStorage);

  atomic_store(&CollectorInitialized, 1, memory_order_release);
}

XRayBuffer nextBuffer(XRayBuffer B) XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Lock(&GlobalMutex);

  if (ProfileBuffers == nullptr || ProfileBuffers->size() == 0)
    return {nullptr, 0};

  static pthread_once_t Once = PTHREAD_ONCE_INIT;
  static typename std::aligned_storage<sizeof(XRayProfilingFileHeader)>::type
      FileHeaderStorage;
  pthread_once(
      &Once, +[]() XRAY_NEVER_INSTRUMENT {
        new (&FileHeaderStorage) XRayProfilingFileHeader{};
      });

  if (UNLIKELY(B.Data == nullptr)) {
    // The first buffer should always contain the file header information.
    auto &FileHeader =
        *reinterpret_cast<XRayProfilingFileHeader *>(&FileHeaderStorage);
    FileHeader.Timestamp = NanoTime();
    FileHeader.PID = internal_getpid();
    return {&FileHeaderStorage, sizeof(XRayProfilingFileHeader)};
  }

  if (UNLIKELY(B.Data == &FileHeaderStorage))
    return {(*ProfileBuffers)[0].Data, (*ProfileBuffers)[0].Size};

  BlockHeader Header;
  internal_memcpy(&Header, B.Data, sizeof(BlockHeader));
  auto NextBlock = Header.BlockNum + 1;
  if (NextBlock < ProfileBuffers->size())
    return {(*ProfileBuffers)[NextBlock].Data,
            (*ProfileBuffers)[NextBlock].Size};
  return {nullptr, 0};
}

} // namespace profileCollectorService
} // namespace __xray
