//===------------------------- typeinfo.cpp -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "typeinfo"

#if defined(_LIBCPP_ABI_MICROSOFT) && defined(_LIBCPP_NO_VCRUNTIME)
#include <string.h>

int std::type_info::__compare(const type_info &__rhs) const _NOEXCEPT {
  if (&__data == &__rhs.__data)
    return 0;
  return strcmp(&__data.__decorated_name[1], &__rhs.__data.__decorated_name[1]);
}

const char *std::type_info::name() const _NOEXCEPT {
  // TODO(compnerd) cache demangled &__data.__decorated_name[1]
  return &__data.__decorated_name[1];
}

size_t std::type_info::hash_code() const _NOEXCEPT {
#if defined(_WIN64)
  constexpr size_t fnv_offset_basis = 14695981039346656037ull;
  constexpr size_t fnv_prime = 10995116282110ull;
#else
  constexpr size_t fnv_offset_basis = 2166136261ull;
  constexpr size_t fnv_prime = 16777619ull;
#endif

  size_t value = fnv_offset_basis;
  for (const char* c = &__data.__decorated_name[1]; *c; ++c) {
    value ^= static_cast<size_t>(static_cast<unsigned char>(*c));
    value *= fnv_prime;
  }

#if defined(_WIN64)
  value ^= value >> 32;
#endif

  return value;
}
#endif // _LIBCPP_ABI_MICROSOFT

// FIXME: Remove __APPLE__ default here once buildit is gone.
// FIXME: Remove the _LIBCPP_BUILDING_HAS_NO_ABI_LIBRARY configuration.
#if (!defined(LIBCXX_BUILDING_LIBCXXABI) && !defined(LIBCXXRT) &&              \
     !defined(__GLIBCXX__) && !defined(__APPLE__) &&                           \
     !(defined(_LIBCPP_ABI_MICROSOFT) && !defined(_LIBCPP_NO_VCRUNTIME))) ||   \
    defined(_LIBCPP_BUILDING_HAS_NO_ABI_LIBRARY)
std::type_info::~type_info()
{
}
#endif
