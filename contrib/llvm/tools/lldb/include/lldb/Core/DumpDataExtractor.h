//===-- DumpDataExtractor.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_DUMPDATAEXTRACTOR_H
#define LLDB_CORE_DUMPDATAEXTRACTOR_H

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-types.h"

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class DataExtractor;
class ExecutionContextScope;
class Stream;

//------------------------------------------------------------------
/// Dumps \a item_count objects into the stream \a s.
///
/// Dumps \a item_count objects using \a item_format, each of which
/// are \a item_byte_size bytes long starting at offset \a offset
/// bytes into the contained data, into the stream \a s. \a
/// num_per_line objects will be dumped on each line before a new
/// line will be output. If \a base_addr is a valid address, then
/// each new line of output will be preceded by the address value
/// plus appropriate offset, and a colon and space. Bitfield values
/// can be dumped by calling this function multiple times with the
/// same start offset, format and size, yet differing \a
/// item_bit_size and \a item_bit_offset values.
///
/// @param[in] s
///     The stream to dump the output to. This value can not be nullptr.
///
/// @param[in] offset
///     The offset into the data at which to start dumping.
///
/// @param[in] item_format
///     The format to use when dumping each item.
///
/// @param[in] item_byte_size
///     The byte size of each item.
///
/// @param[in] item_count
///     The number of items to dump.
///
/// @param[in] num_per_line
///     The number of items to display on each line.
///
/// @param[in] base_addr
///     The base address that gets added to the offset displayed on
///     each line if the value is valid. Is \a base_addr is
///     LLDB_INVALID_ADDRESS then no address values will be prepended
///     to any lines.
///
/// @param[in] item_bit_size
///     If the value to display is a bitfield, this value should
///     be the number of bits that the bitfield item has within the
///     item's byte size value. This function will need to be called
///     multiple times with identical \a offset and \a item_byte_size
///     values in order to display multiple bitfield values that
///     exist within the same integer value. If the items being
///     displayed are not bitfields, this value should be zero.
///
/// @param[in] item_bit_offset
///     If the value to display is a bitfield, this value should
///     be the offset in bits, or shift right amount, that the
///     bitfield item occupies within the item's byte size value.
///     This function will need to be called multiple times with
///     identical \a offset and \a item_byte_size values in order
///     to display multiple bitfield values that exist within the
///     same integer value. If the items being displayed are not
///     bitfields, this value should be zero.
///
/// @return
///     The offset at which dumping ended.
//------------------------------------------------------------------
lldb::offset_t
DumpDataExtractor(const DataExtractor &DE, Stream *s, lldb::offset_t offset,
                  lldb::Format item_format, size_t item_byte_size,
                  size_t item_count, size_t num_per_line, uint64_t base_addr,
                  uint32_t item_bit_size, uint32_t item_bit_offset,
                  ExecutionContextScope *exe_scope = nullptr);

void DumpHexBytes(Stream *s, const void *src, size_t src_len,
                  uint32_t bytes_per_line, lldb::addr_t base_addr);
}

#endif
