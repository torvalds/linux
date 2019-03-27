//===-- MappedHash.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_MappedHash_h_
#define liblldb_MappedHash_h_

#include <assert.h>
#include <stdint.h>

#include <algorithm>
#include <functional>
#include <map>
#include <vector>

#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Stream.h"
#include "llvm/Support/DJB.h"

class MappedHash {
public:
  enum HashFunctionType {
    eHashFunctionDJB = 0u // Daniel J Bernstein hash function that is also used
                          // by the ELF GNU_HASH sections
  };

  static uint32_t HashString(uint32_t hash_function, llvm::StringRef s) {
    switch (hash_function) {
    case MappedHash::eHashFunctionDJB:
      return llvm::djbHash(s);

    default:
      break;
    }
    llvm_unreachable("Invalid hash function index");
  }

  static const uint32_t HASH_MAGIC = 0x48415348u;
  static const uint32_t HASH_CIGAM = 0x48534148u;

  template <typename T> struct Header {
    typedef T HeaderData;

    uint32_t
        magic; // HASH_MAGIC or HASH_CIGAM magic value to allow endian detection
    uint16_t version;         // Version number
    uint16_t hash_function;   // The hash function enumeration that was used
    uint32_t bucket_count;    // The number of buckets in this hash table
    uint32_t hashes_count;    // The total number of unique hash values and hash
                              // data offsets in this table
    uint32_t header_data_len; // The size in bytes of the "header_data" template
                              // member below
    HeaderData header_data;   //

    Header()
        : magic(HASH_MAGIC), version(1), hash_function(eHashFunctionDJB),
          bucket_count(0), hashes_count(0), header_data_len(sizeof(T)),
          header_data() {}

    virtual ~Header() = default;

    size_t GetByteSize() const {
      return sizeof(magic) + sizeof(version) + sizeof(hash_function) +
             sizeof(bucket_count) + sizeof(hashes_count) +
             sizeof(header_data_len) + header_data_len;
    }

    virtual size_t GetByteSize(const HeaderData &header_data) = 0;

    void SetHeaderDataByteSize(uint32_t header_data_byte_size) {
      header_data_len = header_data_byte_size;
    }

    void Dump(lldb_private::Stream &s) {
      s.Printf("header.magic              = 0x%8.8x\n", magic);
      s.Printf("header.version            = 0x%4.4x\n", version);
      s.Printf("header.hash_function      = 0x%4.4x\n", hash_function);
      s.Printf("header.bucket_count       = 0x%8.8x %u\n", bucket_count,
               bucket_count);
      s.Printf("header.hashes_count       = 0x%8.8x %u\n", hashes_count,
               hashes_count);
      s.Printf("header.header_data_len    = 0x%8.8x %u\n", header_data_len,
               header_data_len);
    }

    virtual lldb::offset_t Read(lldb_private::DataExtractor &data,
                                lldb::offset_t offset) {
      if (data.ValidOffsetForDataOfSize(
              offset, sizeof(magic) + sizeof(version) + sizeof(hash_function) +
                          sizeof(bucket_count) + sizeof(hashes_count) +
                          sizeof(header_data_len))) {
        magic = data.GetU32(&offset);
        if (magic != HASH_MAGIC) {
          if (magic == HASH_CIGAM) {
            switch (data.GetByteOrder()) {
            case lldb::eByteOrderBig:
              data.SetByteOrder(lldb::eByteOrderLittle);
              break;
            case lldb::eByteOrderLittle:
              data.SetByteOrder(lldb::eByteOrderBig);
              break;
            default:
              return LLDB_INVALID_OFFSET;
            }
          } else {
            // Magic bytes didn't match
            version = 0;
            return LLDB_INVALID_OFFSET;
          }
        }

        version = data.GetU16(&offset);
        if (version != 1) {
          // Unsupported version
          return LLDB_INVALID_OFFSET;
        }
        hash_function = data.GetU16(&offset);
        if (hash_function == 4)
          hash_function = 0; // Deal with pre-release version of this table...
        bucket_count = data.GetU32(&offset);
        hashes_count = data.GetU32(&offset);
        header_data_len = data.GetU32(&offset);
        return offset;
      }
      return LLDB_INVALID_OFFSET;
    }
    //
    //        // Returns a buffer that contains a serialized version of this
    //        table
    //        // that must be freed with free().
    //        virtual void *
    //        Write (int fd);
  };

  // A class for reading and using a saved hash table from a block of data
  // in memory
  template <typename __KeyType, class __HeaderType, class __HashData>
  class MemoryTable {
  public:
    typedef __HeaderType HeaderType;
    typedef __KeyType KeyType;
    typedef __HashData HashData;

    enum Result {
      eResultKeyMatch = 0u, // The entry was found, key matched and "pair" was
                            // filled in successfully
      eResultKeyMismatch =
          1u, // Bucket hash data collision, but key didn't match
      eResultEndOfHashData = 2u, // The chain of items for this hash data in
                                 // this bucket is terminated, search no more
      eResultError = 3u          // Status parsing the hash data, abort
    };

    struct Pair {
      KeyType key;
      HashData value;
    };

    MemoryTable(lldb_private::DataExtractor &data)
        : m_header(), m_hash_indexes(nullptr), m_hash_values(nullptr),
          m_hash_offsets(nullptr) {
      lldb::offset_t offset = m_header.Read(data, 0);
      if (offset != LLDB_INVALID_OFFSET && IsValid()) {
        m_hash_indexes = (const uint32_t *)data.GetData(
            &offset, m_header.bucket_count * sizeof(uint32_t));
        m_hash_values = (const uint32_t *)data.GetData(
            &offset, m_header.hashes_count * sizeof(uint32_t));
        m_hash_offsets = (const uint32_t *)data.GetData(
            &offset, m_header.hashes_count * sizeof(uint32_t));
      }
    }

    virtual ~MemoryTable() = default;

    bool IsValid() const {
      return m_header.version == 1 &&
             m_header.hash_function == eHashFunctionDJB &&
             m_header.bucket_count > 0;
    }

    uint32_t GetHashIndex(uint32_t bucket_idx) const {
      uint32_t result = UINT32_MAX;
      if (m_hash_indexes && bucket_idx < m_header.bucket_count)
        memcpy(&result, m_hash_indexes + bucket_idx, sizeof(uint32_t));
      return result;
    }

    uint32_t GetHashValue(uint32_t hash_idx) const {
      uint32_t result = UINT32_MAX;
      if (m_hash_values && hash_idx < m_header.hashes_count)
        memcpy(&result, m_hash_values + hash_idx, sizeof(uint32_t));
      return result;
    }

    uint32_t GetHashDataOffset(uint32_t hash_idx) const {
      uint32_t result = UINT32_MAX;
      if (m_hash_offsets && hash_idx < m_header.hashes_count)
        memcpy(&result, m_hash_offsets + hash_idx, sizeof(uint32_t));
      return result;
    }

    bool Find(llvm::StringRef name, Pair &pair) const {
      if (name.empty())
        return false;

      if (IsValid()) {
        const uint32_t bucket_count = m_header.bucket_count;
        const uint32_t hash_count = m_header.hashes_count;
        const uint32_t hash_value =
            MappedHash::HashString(m_header.hash_function, name);
        const uint32_t bucket_idx = hash_value % bucket_count;
        uint32_t hash_idx = GetHashIndex(bucket_idx);
        if (hash_idx < hash_count) {
          for (; hash_idx < hash_count; ++hash_idx) {
            const uint32_t curr_hash_value = GetHashValue(hash_idx);
            if (curr_hash_value == hash_value) {
              lldb::offset_t hash_data_offset = GetHashDataOffset(hash_idx);
              while (hash_data_offset != UINT32_MAX) {
                const lldb::offset_t prev_hash_data_offset = hash_data_offset;
                Result hash_result =
                    GetHashDataForName(name, &hash_data_offset, pair);
                // Check the result of getting our hash data
                switch (hash_result) {
                case eResultKeyMatch:
                  return true;

                case eResultKeyMismatch:
                  if (prev_hash_data_offset == hash_data_offset)
                    return false;
                  break;

                case eResultEndOfHashData:
                  // The last HashData for this key has been reached, stop
                  // searching
                  return false;
                case eResultError:
                  // Status parsing the hash data, abort
                  return false;
                }
              }
            }
            if ((curr_hash_value % bucket_count) != bucket_idx)
              break;
          }
        }
      }
      return false;
    }

    // This method must be implemented in any subclasses. The KeyType is user
    // specified and must somehow result in a string value. For example, the
    // KeyType might be a string offset in a string table and subclasses can
    // store their string table as a member of the subclass and return a valie
    // "const char *" given a "key". The value could also be a C string
    // pointer, in which case just returning "key" will suffice.
    virtual const char *GetStringForKeyType(KeyType key) const = 0;

    virtual bool ReadHashData(uint32_t hash_data_offset,
                              HashData &hash_data) const = 0;

    // This method must be implemented in any subclasses and it must try to
    // read one "Pair" at the offset pointed to by the "hash_data_offset_ptr"
    // parameter. This offset should be updated as bytes are consumed and a
    // value "Result" enum should be returned. If the "name" matches the full
    // name for the "pair.key" (which must be filled in by this call), then the
    // HashData in the pair ("pair.value") should be extracted and filled in
    // and "eResultKeyMatch" should be returned. If "name" doesn't match this
    // string for the key, then "eResultKeyMismatch" should be returned and all
    // data for the current HashData must be consumed or skipped and the
    // "hash_data_offset_ptr" offset needs to be updated to point to the next
    // HashData. If the end of the HashData objects for a given hash value have
    // been reached, then "eResultEndOfHashData" should be returned. If
    // anything else goes wrong during parsing, return "eResultError" and the
    // corresponding "Find()" function will be canceled and return false.
    virtual Result GetHashDataForName(llvm::StringRef name,
                                      lldb::offset_t *hash_data_offset_ptr,
                                      Pair &pair) const = 0;

    const HeaderType &GetHeader() { return m_header; }

    void ForEach(
        std::function<bool(const HashData &hash_data)> const &callback) const {
      const size_t num_hash_offsets = m_header.hashes_count;
      for (size_t i = 0; i < num_hash_offsets; ++i) {
        uint32_t hash_data_offset = GetHashDataOffset(i);
        if (hash_data_offset != UINT32_MAX) {
          HashData hash_data;
          if (ReadHashData(hash_data_offset, hash_data)) {
            // If the callback returns false, then we are done and should stop
            if (callback(hash_data) == false)
              return;
          }
        }
      }
    }

  protected:
    // Implementation agnostic information
    HeaderType m_header;
    const uint32_t *m_hash_indexes;
    const uint32_t *m_hash_values;
    const uint32_t *m_hash_offsets;
  };
};

#endif // liblldb_MappedHash_h_
