//===-- ConstString.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/ConstString.h"

#include "lldb/Utility/Stream.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/FormatProviders.h"
#include "llvm/Support/RWMutex.h"
#include "llvm/Support/Threading.h"

#include <algorithm>
#include <array>
#include <utility>

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

using namespace lldb_private;

class Pool {
public:
  typedef const char *StringPoolValueType;
  typedef llvm::StringMap<StringPoolValueType, llvm::BumpPtrAllocator>
      StringPool;
  typedef llvm::StringMapEntry<StringPoolValueType> StringPoolEntryType;

  static StringPoolEntryType &
  GetStringMapEntryFromKeyData(const char *keyData) {
    return StringPoolEntryType::GetStringMapEntryFromKeyData(keyData);
  }

  static size_t GetConstCStringLength(const char *ccstr) {
    if (ccstr != nullptr) {
      // Since the entry is read only, and we derive the entry entirely from
      // the pointer, we don't need the lock.
      const StringPoolEntryType &entry = GetStringMapEntryFromKeyData(ccstr);
      return entry.getKey().size();
    }
    return 0;
  }

  StringPoolValueType GetMangledCounterpart(const char *ccstr) const {
    if (ccstr != nullptr) {
      const uint8_t h = hash(llvm::StringRef(ccstr));
      llvm::sys::SmartScopedReader<false> rlock(m_string_pools[h].m_mutex);
      return GetStringMapEntryFromKeyData(ccstr).getValue();
    }
    return nullptr;
  }

  bool SetMangledCounterparts(const char *key_ccstr, const char *value_ccstr) {
    if (key_ccstr != nullptr && value_ccstr != nullptr) {
      {
        const uint8_t h = hash(llvm::StringRef(key_ccstr));
        llvm::sys::SmartScopedWriter<false> wlock(m_string_pools[h].m_mutex);
        GetStringMapEntryFromKeyData(key_ccstr).setValue(value_ccstr);
      }
      {
        const uint8_t h = hash(llvm::StringRef(value_ccstr));
        llvm::sys::SmartScopedWriter<false> wlock(m_string_pools[h].m_mutex);
        GetStringMapEntryFromKeyData(value_ccstr).setValue(key_ccstr);
      }
      return true;
    }
    return false;
  }

  const char *GetConstCString(const char *cstr) {
    if (cstr != nullptr)
      return GetConstCStringWithLength(cstr, strlen(cstr));
    return nullptr;
  }

  const char *GetConstCStringWithLength(const char *cstr, size_t cstr_len) {
    if (cstr != nullptr)
      return GetConstCStringWithStringRef(llvm::StringRef(cstr, cstr_len));
    return nullptr;
  }

  const char *GetConstCStringWithStringRef(const llvm::StringRef &string_ref) {
    if (string_ref.data()) {
      const uint8_t h = hash(string_ref);

      {
        llvm::sys::SmartScopedReader<false> rlock(m_string_pools[h].m_mutex);
        auto it = m_string_pools[h].m_string_map.find(string_ref);
        if (it != m_string_pools[h].m_string_map.end())
          return it->getKeyData();
      }

      llvm::sys::SmartScopedWriter<false> wlock(m_string_pools[h].m_mutex);
      StringPoolEntryType &entry =
          *m_string_pools[h]
               .m_string_map.insert(std::make_pair(string_ref, nullptr))
               .first;
      return entry.getKeyData();
    }
    return nullptr;
  }

  const char *
  GetConstCStringAndSetMangledCounterPart(llvm::StringRef demangled,
                                          const char *mangled_ccstr) {
    const char *demangled_ccstr = nullptr;

    {
      const uint8_t h = hash(demangled);
      llvm::sys::SmartScopedWriter<false> wlock(m_string_pools[h].m_mutex);

      // Make or update string pool entry with the mangled counterpart
      StringPool &map = m_string_pools[h].m_string_map;
      StringPoolEntryType &entry = *map.try_emplace(demangled).first;

      entry.second = mangled_ccstr;

      // Extract the const version of the demangled_cstr
      demangled_ccstr = entry.getKeyData();
    }

    {
      // Now assign the demangled const string as the counterpart of the
      // mangled const string...
      const uint8_t h = hash(llvm::StringRef(mangled_ccstr));
      llvm::sys::SmartScopedWriter<false> wlock(m_string_pools[h].m_mutex);
      GetStringMapEntryFromKeyData(mangled_ccstr).setValue(demangled_ccstr);
    }

    // Return the constant demangled C string
    return demangled_ccstr;
  }

  const char *GetConstTrimmedCStringWithLength(const char *cstr,
                                               size_t cstr_len) {
    if (cstr != nullptr) {
      const size_t trimmed_len = std::min<size_t>(strlen(cstr), cstr_len);
      return GetConstCStringWithLength(cstr, trimmed_len);
    }
    return nullptr;
  }

  //------------------------------------------------------------------
  // Return the size in bytes that this object and any items in its collection
  // of uniqued strings + data count values takes in memory.
  //------------------------------------------------------------------
  size_t MemorySize() const {
    size_t mem_size = sizeof(Pool);
    for (const auto &pool : m_string_pools) {
      llvm::sys::SmartScopedReader<false> rlock(pool.m_mutex);
      for (const auto &entry : pool.m_string_map)
        mem_size += sizeof(StringPoolEntryType) + entry.getKey().size();
    }
    return mem_size;
  }

protected:
  uint8_t hash(const llvm::StringRef &s) const {
    uint32_t h = llvm::djbHash(s);
    return ((h >> 24) ^ (h >> 16) ^ (h >> 8) ^ h) & 0xff;
  }

  struct PoolEntry {
    mutable llvm::sys::SmartRWMutex<false> m_mutex;
    StringPool m_string_map;
  };

  std::array<PoolEntry, 256> m_string_pools;
};

//----------------------------------------------------------------------
// Frameworks and dylibs aren't supposed to have global C++ initializers so we
// hide the string pool in a static function so that it will get initialized on
// the first call to this static function.
//
// Note, for now we make the string pool a pointer to the pool, because we
// can't guarantee that some objects won't get destroyed after the global
// destructor chain is run, and trying to make sure no destructors touch
// ConstStrings is difficult.  So we leak the pool instead.
//----------------------------------------------------------------------
static Pool &StringPool() {
  static llvm::once_flag g_pool_initialization_flag;
  static Pool *g_string_pool = nullptr;

  llvm::call_once(g_pool_initialization_flag,
                 []() { g_string_pool = new Pool(); });

  return *g_string_pool;
}

ConstString::ConstString(const char *cstr)
    : m_string(StringPool().GetConstCString(cstr)) {}

ConstString::ConstString(const char *cstr, size_t cstr_len)
    : m_string(StringPool().GetConstCStringWithLength(cstr, cstr_len)) {}

ConstString::ConstString(const llvm::StringRef &s)
    : m_string(StringPool().GetConstCStringWithLength(s.data(), s.size())) {}

bool ConstString::operator<(const ConstString &rhs) const {
  if (m_string == rhs.m_string)
    return false;

  llvm::StringRef lhs_string_ref(GetStringRef());
  llvm::StringRef rhs_string_ref(rhs.GetStringRef());

  // If both have valid C strings, then return the comparison
  if (lhs_string_ref.data() && rhs_string_ref.data())
    return lhs_string_ref < rhs_string_ref;

  // Else one of them was nullptr, so if LHS is nullptr then it is less than
  return lhs_string_ref.data() == nullptr;
}

Stream &lldb_private::operator<<(Stream &s, const ConstString &str) {
  const char *cstr = str.GetCString();
  if (cstr != nullptr)
    s << cstr;

  return s;
}

size_t ConstString::GetLength() const {
  return Pool::GetConstCStringLength(m_string);
}

bool ConstString::Equals(const ConstString &lhs, const ConstString &rhs,
                         const bool case_sensitive) {
  if (lhs.m_string == rhs.m_string)
    return true;

  // Since the pointers weren't equal, and identical ConstStrings always have
  // identical pointers, the result must be false for case sensitive equality
  // test.
  if (case_sensitive)
    return false;

  // perform case insensitive equality test
  llvm::StringRef lhs_string_ref(lhs.GetStringRef());
  llvm::StringRef rhs_string_ref(rhs.GetStringRef());
  return lhs_string_ref.equals_lower(rhs_string_ref);
}

int ConstString::Compare(const ConstString &lhs, const ConstString &rhs,
                         const bool case_sensitive) {
  // If the iterators are the same, this is the same string
  const char *lhs_cstr = lhs.m_string;
  const char *rhs_cstr = rhs.m_string;
  if (lhs_cstr == rhs_cstr)
    return 0;
  if (lhs_cstr && rhs_cstr) {
    llvm::StringRef lhs_string_ref(lhs.GetStringRef());
    llvm::StringRef rhs_string_ref(rhs.GetStringRef());

    if (case_sensitive) {
      return lhs_string_ref.compare(rhs_string_ref);
    } else {
      return lhs_string_ref.compare_lower(rhs_string_ref);
    }
  }

  if (lhs_cstr)
    return +1; // LHS isn't nullptr but RHS is
  else
    return -1; // LHS is nullptr but RHS isn't
}

void ConstString::Dump(Stream *s, const char *fail_value) const {
  if (s != nullptr) {
    const char *cstr = AsCString(fail_value);
    if (cstr != nullptr)
      s->PutCString(cstr);
  }
}

void ConstString::DumpDebug(Stream *s) const {
  const char *cstr = GetCString();
  size_t cstr_len = GetLength();
  // Only print the parens if we have a non-nullptr string
  const char *parens = cstr ? "\"" : "";
  s->Printf("%*p: ConstString, string = %s%s%s, length = %" PRIu64,
            static_cast<int>(sizeof(void *) * 2),
            static_cast<const void *>(this), parens, cstr, parens,
            static_cast<uint64_t>(cstr_len));
}

void ConstString::SetCString(const char *cstr) {
  m_string = StringPool().GetConstCString(cstr);
}

void ConstString::SetString(const llvm::StringRef &s) {
  m_string = StringPool().GetConstCStringWithLength(s.data(), s.size());
}

void ConstString::SetStringWithMangledCounterpart(llvm::StringRef demangled,
                                                   const ConstString &mangled) {
  m_string = StringPool().GetConstCStringAndSetMangledCounterPart(
      demangled, mangled.m_string);
}

bool ConstString::GetMangledCounterpart(ConstString &counterpart) const {
  counterpart.m_string = StringPool().GetMangledCounterpart(m_string);
  return (bool)counterpart;
}

void ConstString::SetCStringWithLength(const char *cstr, size_t cstr_len) {
  m_string = StringPool().GetConstCStringWithLength(cstr, cstr_len);
}

void ConstString::SetTrimmedCStringWithLength(const char *cstr,
                                              size_t cstr_len) {
  m_string = StringPool().GetConstTrimmedCStringWithLength(cstr, cstr_len);
}

size_t ConstString::StaticMemorySize() {
  // Get the size of the static string pool
  return StringPool().MemorySize();
}

void llvm::format_provider<ConstString>::format(const ConstString &CS,
                                                llvm::raw_ostream &OS,
                                                llvm::StringRef Options) {
  format_provider<StringRef>::format(CS.AsCString(), OS, Options);
}
