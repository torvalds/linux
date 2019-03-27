//===-- StringExtractor.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_StringExtractor_h_
#define utility_StringExtractor_h_

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <stddef.h>
#include <stdint.h>
#include <string>

class StringExtractor {
public:
  enum { BigEndian = 0, LittleEndian = 1 };
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  StringExtractor();
  StringExtractor(llvm::StringRef packet_str);
  StringExtractor(const char *packet_cstr);
  StringExtractor(const StringExtractor &rhs);
  virtual ~StringExtractor();

  //------------------------------------------------------------------
  // Operators
  //------------------------------------------------------------------
  const StringExtractor &operator=(const StringExtractor &rhs);

  void Reset(llvm::StringRef str) {
    m_packet = str;
    m_index = 0;
  }

  // Returns true if the file position is still valid for the data contained in
  // this string extractor object.
  bool IsGood() const { return m_index != UINT64_MAX; }

  uint64_t GetFilePos() const { return m_index; }

  void SetFilePos(uint32_t idx) { m_index = idx; }

  void Clear() {
    m_packet.clear();
    m_index = 0;
  }

  void SkipSpaces();

  std::string &GetStringRef() { return m_packet; }

  const std::string &GetStringRef() const { return m_packet; }

  bool Empty() { return m_packet.empty(); }

  size_t GetBytesLeft() {
    if (m_index < m_packet.size())
      return m_packet.size() - m_index;
    return 0;
  }

  char GetChar(char fail_value = '\0');

  char PeekChar(char fail_value = '\0') {
    const char *cstr = Peek();
    if (cstr)
      return cstr[0];
    return fail_value;
  }

  int DecodeHexU8();

  uint8_t GetHexU8(uint8_t fail_value = 0, bool set_eof_on_fail = true);

  bool GetHexU8Ex(uint8_t &ch, bool set_eof_on_fail = true);

  bool GetNameColonValue(llvm::StringRef &name, llvm::StringRef &value);

  int32_t GetS32(int32_t fail_value, int base = 0);

  uint32_t GetU32(uint32_t fail_value, int base = 0);

  int64_t GetS64(int64_t fail_value, int base = 0);

  uint64_t GetU64(uint64_t fail_value, int base = 0);

  uint32_t GetHexMaxU32(bool little_endian, uint32_t fail_value);

  uint64_t GetHexMaxU64(bool little_endian, uint64_t fail_value);

  size_t GetHexBytes(llvm::MutableArrayRef<uint8_t> dest,
                     uint8_t fail_fill_value);

  size_t GetHexBytesAvail(llvm::MutableArrayRef<uint8_t> dest);

  uint64_t GetHexWithFixedSize(uint32_t byte_size, bool little_endian,
                               uint64_t fail_value);

  size_t GetHexByteString(std::string &str);

  size_t GetHexByteStringFixedLength(std::string &str, uint32_t nibble_length);

  size_t GetHexByteStringTerminatedBy(std::string &str, char terminator);

  bool ConsumeFront(const llvm::StringRef &str);

  const char *Peek() {
    if (m_index < m_packet.size())
      return m_packet.c_str() + m_index;
    return nullptr;
  }

protected:
  bool fail() {
    m_index = UINT64_MAX;
    return false;
  }
  //------------------------------------------------------------------
  // For StringExtractor only
  //------------------------------------------------------------------
  std::string m_packet; // The string in which to extract data.
  uint64_t m_index;     // When extracting data from a packet, this index
                        // will march along as things get extracted. If set to
                        // UINT64_MAX the end of the packet data was reached
                        // when decoding information
};

#endif // utility_StringExtractor_h_
