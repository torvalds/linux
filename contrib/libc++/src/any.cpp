//===---------------------------- any.cpp ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "any"

namespace std {
const char* bad_any_cast::what() const _NOEXCEPT {
    return "bad any cast";
}
}


#include <experimental/__config>

//  Preserve std::experimental::any_bad_cast for ABI compatibility
//  Even though it no longer exists in a header file
_LIBCPP_BEGIN_NAMESPACE_LFTS

class _LIBCPP_EXCEPTION_ABI _LIBCPP_AVAILABILITY_BAD_ANY_CAST bad_any_cast : public bad_cast
{
public:
    virtual const char* what() const _NOEXCEPT;
};

const char* bad_any_cast::what() const _NOEXCEPT {
    return "bad any cast";
}

_LIBCPP_END_NAMESPACE_LFTS
