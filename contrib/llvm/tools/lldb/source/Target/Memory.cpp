//===-- Memory.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Memory.h"
#include <inttypes.h>
#include "lldb/Core/RangeMap.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// MemoryCache constructor
//----------------------------------------------------------------------
MemoryCache::MemoryCache(Process &process)
    : m_mutex(), m_L1_cache(), m_L2_cache(), m_invalid_ranges(),
      m_process(process),
      m_L2_cache_line_byte_size(process.GetMemoryCacheLineSize()) {}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
MemoryCache::~MemoryCache() {}

void MemoryCache::Clear(bool clear_invalid_ranges) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_L1_cache.clear();
  m_L2_cache.clear();
  if (clear_invalid_ranges)
    m_invalid_ranges.Clear();
  m_L2_cache_line_byte_size = m_process.GetMemoryCacheLineSize();
}

void MemoryCache::AddL1CacheData(lldb::addr_t addr, const void *src,
                                 size_t src_len) {
  AddL1CacheData(
      addr, DataBufferSP(new DataBufferHeap(DataBufferHeap(src, src_len))));
}

void MemoryCache::AddL1CacheData(lldb::addr_t addr,
                                 const DataBufferSP &data_buffer_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_L1_cache[addr] = data_buffer_sp;
}

void MemoryCache::Flush(addr_t addr, size_t size) {
  if (size == 0)
    return;

  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  // Erase any blocks from the L1 cache that intersect with the flush range
  if (!m_L1_cache.empty()) {
    AddrRange flush_range(addr, size);
    BlockMap::iterator pos = m_L1_cache.upper_bound(addr);
    if (pos != m_L1_cache.begin()) {
      --pos;
    }
    while (pos != m_L1_cache.end()) {
      AddrRange chunk_range(pos->first, pos->second->GetByteSize());
      if (!chunk_range.DoesIntersect(flush_range))
        break;
      pos = m_L1_cache.erase(pos);
    }
  }

  if (!m_L2_cache.empty()) {
    const uint32_t cache_line_byte_size = m_L2_cache_line_byte_size;
    const addr_t end_addr = (addr + size - 1);
    const addr_t first_cache_line_addr = addr - (addr % cache_line_byte_size);
    const addr_t last_cache_line_addr =
        end_addr - (end_addr % cache_line_byte_size);
    // Watch for overflow where size will cause us to go off the end of the
    // 64 bit address space
    uint32_t num_cache_lines;
    if (last_cache_line_addr >= first_cache_line_addr)
      num_cache_lines = ((last_cache_line_addr - first_cache_line_addr) /
                         cache_line_byte_size) +
                        1;
    else
      num_cache_lines =
          (UINT64_MAX - first_cache_line_addr + 1) / cache_line_byte_size;

    uint32_t cache_idx = 0;
    for (addr_t curr_addr = first_cache_line_addr; cache_idx < num_cache_lines;
         curr_addr += cache_line_byte_size, ++cache_idx) {
      BlockMap::iterator pos = m_L2_cache.find(curr_addr);
      if (pos != m_L2_cache.end())
        m_L2_cache.erase(pos);
    }
  }
}

void MemoryCache::AddInvalidRange(lldb::addr_t base_addr,
                                  lldb::addr_t byte_size) {
  if (byte_size > 0) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    InvalidRanges::Entry range(base_addr, byte_size);
    m_invalid_ranges.Append(range);
    m_invalid_ranges.Sort();
  }
}

bool MemoryCache::RemoveInvalidRange(lldb::addr_t base_addr,
                                     lldb::addr_t byte_size) {
  if (byte_size > 0) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    const uint32_t idx = m_invalid_ranges.FindEntryIndexThatContains(base_addr);
    if (idx != UINT32_MAX) {
      const InvalidRanges::Entry *entry = m_invalid_ranges.GetEntryAtIndex(idx);
      if (entry->GetRangeBase() == base_addr &&
          entry->GetByteSize() == byte_size)
        return m_invalid_ranges.RemoveEntrtAtIndex(idx);
    }
  }
  return false;
}

size_t MemoryCache::Read(addr_t addr, void *dst, size_t dst_len,
                         Status &error) {
  size_t bytes_left = dst_len;

  // Check the L1 cache for a range that contain the entire memory read. If we
  // find a range in the L1 cache that does, we use it. Else we fall back to
  // reading memory in m_L2_cache_line_byte_size byte sized chunks. The L1
  // cache contains chunks of memory that are not required to be
  // m_L2_cache_line_byte_size bytes in size, so we don't try anything tricky
  // when reading from them (no partial reads from the L1 cache).

  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (!m_L1_cache.empty()) {
    AddrRange read_range(addr, dst_len);
    BlockMap::iterator pos = m_L1_cache.upper_bound(addr);
    if (pos != m_L1_cache.begin()) {
      --pos;
    }
    AddrRange chunk_range(pos->first, pos->second->GetByteSize());
    if (chunk_range.Contains(read_range)) {
      memcpy(dst, pos->second->GetBytes() + addr - chunk_range.GetRangeBase(),
             dst_len);
      return dst_len;
    }
  }

  // If this memory read request is larger than the cache line size, then we
  // (1) try to read as much of it at once as possible, and (2) don't add the
  // data to the memory cache.  We don't want to split a big read up into more
  // separate reads than necessary, and with a large memory read request, it is
  // unlikely that the caller function will ask for the next
  // 4 bytes after the large memory read - so there's little benefit to saving
  // it in the cache.
  if (dst && dst_len > m_L2_cache_line_byte_size) {
    size_t bytes_read =
        m_process.ReadMemoryFromInferior(addr, dst, dst_len, error);
    // Add this non block sized range to the L1 cache if we actually read
    // anything
    if (bytes_read > 0)
      AddL1CacheData(addr, dst, bytes_read);
    return bytes_read;
  }

  if (dst && bytes_left > 0) {
    const uint32_t cache_line_byte_size = m_L2_cache_line_byte_size;
    uint8_t *dst_buf = (uint8_t *)dst;
    addr_t curr_addr = addr - (addr % cache_line_byte_size);
    addr_t cache_offset = addr - curr_addr;

    while (bytes_left > 0) {
      if (m_invalid_ranges.FindEntryThatContains(curr_addr)) {
        error.SetErrorStringWithFormat("memory read failed for 0x%" PRIx64,
                                       curr_addr);
        return dst_len - bytes_left;
      }

      BlockMap::const_iterator pos = m_L2_cache.find(curr_addr);
      BlockMap::const_iterator end = m_L2_cache.end();

      if (pos != end) {
        size_t curr_read_size = cache_line_byte_size - cache_offset;
        if (curr_read_size > bytes_left)
          curr_read_size = bytes_left;

        memcpy(dst_buf + dst_len - bytes_left,
               pos->second->GetBytes() + cache_offset, curr_read_size);

        bytes_left -= curr_read_size;
        curr_addr += curr_read_size + cache_offset;
        cache_offset = 0;

        if (bytes_left > 0) {
          // Get sequential cache page hits
          for (++pos; (pos != end) && (bytes_left > 0); ++pos) {
            assert((curr_addr % cache_line_byte_size) == 0);

            if (pos->first != curr_addr)
              break;

            curr_read_size = pos->second->GetByteSize();
            if (curr_read_size > bytes_left)
              curr_read_size = bytes_left;

            memcpy(dst_buf + dst_len - bytes_left, pos->second->GetBytes(),
                   curr_read_size);

            bytes_left -= curr_read_size;
            curr_addr += curr_read_size;

            // We have a cache page that succeeded to read some bytes but not
            // an entire page. If this happens, we must cap off how much data
            // we are able to read...
            if (pos->second->GetByteSize() != cache_line_byte_size)
              return dst_len - bytes_left;
          }
        }
      }

      // We need to read from the process

      if (bytes_left > 0) {
        assert((curr_addr % cache_line_byte_size) == 0);
        std::unique_ptr<DataBufferHeap> data_buffer_heap_ap(
            new DataBufferHeap(cache_line_byte_size, 0));
        size_t process_bytes_read = m_process.ReadMemoryFromInferior(
            curr_addr, data_buffer_heap_ap->GetBytes(),
            data_buffer_heap_ap->GetByteSize(), error);
        if (process_bytes_read == 0)
          return dst_len - bytes_left;

        if (process_bytes_read != cache_line_byte_size)
          data_buffer_heap_ap->SetByteSize(process_bytes_read);
        m_L2_cache[curr_addr] = DataBufferSP(data_buffer_heap_ap.release());
        // We have read data and put it into the cache, continue through the
        // loop again to get the data out of the cache...
      }
    }
  }

  return dst_len - bytes_left;
}

AllocatedBlock::AllocatedBlock(lldb::addr_t addr, uint32_t byte_size,
                               uint32_t permissions, uint32_t chunk_size)
    : m_range(addr, byte_size), m_permissions(permissions),
      m_chunk_size(chunk_size)
{
  // The entire address range is free to start with.
  m_free_blocks.Append(m_range);
  assert(byte_size > chunk_size);
}

AllocatedBlock::~AllocatedBlock() {}

lldb::addr_t AllocatedBlock::ReserveBlock(uint32_t size) {
  // We must return something valid for zero bytes.
  if (size == 0)
    size = 1;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  
  const size_t free_count = m_free_blocks.GetSize();
  for (size_t i=0; i<free_count; ++i)
  {
    auto &free_block = m_free_blocks.GetEntryRef(i);
    const lldb::addr_t range_size = free_block.GetByteSize();
    if (range_size >= size)
    {
      // We found a free block that is big enough for our data. Figure out how
      // many chunks we will need and calculate the resulting block size we
      // will reserve.
      addr_t addr = free_block.GetRangeBase();
      size_t num_chunks = CalculateChunksNeededForSize(size);
      lldb::addr_t block_size = num_chunks * m_chunk_size;
      lldb::addr_t bytes_left = range_size - block_size;
      if (bytes_left == 0)
      {
        // The newly allocated block will take all of the bytes in this
        // available block, so we can just add it to the allocated ranges and
        // remove the range from the free ranges.
        m_reserved_blocks.Insert(free_block, false);
        m_free_blocks.RemoveEntryAtIndex(i);
      }
      else
      {
        // Make the new allocated range and add it to the allocated ranges.
        Range<lldb::addr_t, uint32_t> reserved_block(free_block);
        reserved_block.SetByteSize(block_size);
        // Insert the reserved range and don't combine it with other blocks in
        // the reserved blocks list.
        m_reserved_blocks.Insert(reserved_block, false);
        // Adjust the free range in place since we won't change the sorted
        // ordering of the m_free_blocks list.
        free_block.SetRangeBase(reserved_block.GetRangeEnd());
        free_block.SetByteSize(bytes_left);
      }
      LLDB_LOGV(log, "({0}) (size = {1} ({1:x})) => {2:x}", this, size, addr);
      return addr;
    }
  }

  LLDB_LOGV(log, "({0}) (size = {1} ({1:x})) => {2:x}", this, size,
            LLDB_INVALID_ADDRESS);
  return LLDB_INVALID_ADDRESS;
}

bool AllocatedBlock::FreeBlock(addr_t addr) {
  bool success = false;
  auto entry_idx = m_reserved_blocks.FindEntryIndexThatContains(addr);
  if (entry_idx != UINT32_MAX)
  {
    m_free_blocks.Insert(m_reserved_blocks.GetEntryRef(entry_idx), true);
    m_reserved_blocks.RemoveEntryAtIndex(entry_idx);
    success = true;
  }
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  LLDB_LOGV(log, "({0}) (addr = {1:x}) => {2}", this, addr, success);
  return success;
}

AllocatedMemoryCache::AllocatedMemoryCache(Process &process)
    : m_process(process), m_mutex(), m_memory_map() {}

AllocatedMemoryCache::~AllocatedMemoryCache() {}

void AllocatedMemoryCache::Clear() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_process.IsAlive()) {
    PermissionsToBlockMap::iterator pos, end = m_memory_map.end();
    for (pos = m_memory_map.begin(); pos != end; ++pos)
      m_process.DoDeallocateMemory(pos->second->GetBaseAddress());
  }
  m_memory_map.clear();
}

AllocatedMemoryCache::AllocatedBlockSP
AllocatedMemoryCache::AllocatePage(uint32_t byte_size, uint32_t permissions,
                                   uint32_t chunk_size, Status &error) {
  AllocatedBlockSP block_sp;
  const size_t page_size = 4096;
  const size_t num_pages = (byte_size + page_size - 1) / page_size;
  const size_t page_byte_size = num_pages * page_size;

  addr_t addr = m_process.DoAllocateMemory(page_byte_size, permissions, error);

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log) {
    log->Printf("Process::DoAllocateMemory (byte_size = 0x%8.8" PRIx32
                ", permissions = %s) => 0x%16.16" PRIx64,
                (uint32_t)page_byte_size, GetPermissionsAsCString(permissions),
                (uint64_t)addr);
  }

  if (addr != LLDB_INVALID_ADDRESS) {
    block_sp.reset(
        new AllocatedBlock(addr, page_byte_size, permissions, chunk_size));
    m_memory_map.insert(std::make_pair(permissions, block_sp));
  }
  return block_sp;
}

lldb::addr_t AllocatedMemoryCache::AllocateMemory(size_t byte_size,
                                                  uint32_t permissions,
                                                  Status &error) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  addr_t addr = LLDB_INVALID_ADDRESS;
  std::pair<PermissionsToBlockMap::iterator, PermissionsToBlockMap::iterator>
      range = m_memory_map.equal_range(permissions);

  for (PermissionsToBlockMap::iterator pos = range.first; pos != range.second;
       ++pos) {
    addr = (*pos).second->ReserveBlock(byte_size);
    if (addr != LLDB_INVALID_ADDRESS)
      break;
  }

  if (addr == LLDB_INVALID_ADDRESS) {
    AllocatedBlockSP block_sp(AllocatePage(byte_size, permissions, 16, error));

    if (block_sp)
      addr = block_sp->ReserveBlock(byte_size);
  }
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf(
        "AllocatedMemoryCache::AllocateMemory (byte_size = 0x%8.8" PRIx32
        ", permissions = %s) => 0x%16.16" PRIx64,
        (uint32_t)byte_size, GetPermissionsAsCString(permissions),
        (uint64_t)addr);
  return addr;
}

bool AllocatedMemoryCache::DeallocateMemory(lldb::addr_t addr) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  PermissionsToBlockMap::iterator pos, end = m_memory_map.end();
  bool success = false;
  for (pos = m_memory_map.begin(); pos != end; ++pos) {
    if (pos->second->Contains(addr)) {
      success = pos->second->FreeBlock(addr);
      break;
    }
  }
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf("AllocatedMemoryCache::DeallocateMemory (addr = 0x%16.16" PRIx64
                ") => %i",
                (uint64_t)addr, success);
  return success;
}
