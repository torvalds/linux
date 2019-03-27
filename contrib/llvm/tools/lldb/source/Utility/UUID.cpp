//===-- UUID.cpp ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/UUID.h"

#include "lldb/Utility/Stream.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Format.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

using namespace lldb_private;

// Whether to put a separator after count uuid bytes.
// For the first 16 bytes we follow the traditional UUID format. After that, we
// simply put a dash after every 6 bytes.
static inline bool separate(size_t count) {
  if (count >= 10)
    return (count - 10) % 6 == 0;

  switch (count) {
  case 4:
  case 6:
  case 8:
    return true;
  default:
    return false;
  }
}

std::string UUID::GetAsString(llvm::StringRef separator) const {
  std::string result;
  llvm::raw_string_ostream os(result);

  for (auto B : llvm::enumerate(GetBytes())) {
    if (separate(B.index()))
      os << separator;

    os << llvm::format_hex_no_prefix(B.value(), 2, true);
  }
  os.flush();

  return result;
}

void UUID::Dump(Stream *s) const { s->PutCString(GetAsString()); }

static inline int xdigit_to_int(char ch) {
  ch = tolower(ch);
  if (ch >= 'a' && ch <= 'f')
    return 10 + ch - 'a';
  return ch - '0';
}

llvm::StringRef
UUID::DecodeUUIDBytesFromString(llvm::StringRef p,
                                llvm::SmallVectorImpl<uint8_t> &uuid_bytes,
                                uint32_t num_uuid_bytes) {
  uuid_bytes.clear();
  while (!p.empty()) {
    if (isxdigit(p[0]) && isxdigit(p[1])) {
      int hi_nibble = xdigit_to_int(p[0]);
      int lo_nibble = xdigit_to_int(p[1]);
      // Translate the two hex nibble characters into a byte
      uuid_bytes.push_back((hi_nibble << 4) + lo_nibble);

      // Skip both hex digits
      p = p.drop_front(2);

      // Increment the byte that we are decoding within the UUID value and
      // break out if we are done
      if (uuid_bytes.size() == num_uuid_bytes)
        break;
    } else if (p.front() == '-') {
      // Skip dashes
      p = p.drop_front();
    } else {
      // UUID values can only consist of hex characters and '-' chars
      break;
    }
  }
  return p;
}

size_t UUID::SetFromStringRef(llvm::StringRef str, uint32_t num_uuid_bytes) {
  llvm::StringRef p = str;

  // Skip leading whitespace characters
  p = p.ltrim();

  llvm::SmallVector<uint8_t, 20> bytes;
  llvm::StringRef rest =
      UUID::DecodeUUIDBytesFromString(p, bytes, num_uuid_bytes);

  // If we successfully decoded a UUID, return the amount of characters that
  // were consumed
  if (bytes.size() == num_uuid_bytes) {
    *this = fromData(bytes);
    return str.size() - rest.size();
  }

  // Else return zero to indicate we were not able to parse a UUID value
  return 0;
}
