//===------------------------ variant.cpp ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "variant"

namespace std {

const char* bad_variant_access::what() const noexcept {
  return "bad_variant_access";
}

}  // namespace std
