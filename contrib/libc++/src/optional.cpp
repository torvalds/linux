//===------------------------ optional.cpp --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "optional"

namespace std
{

bad_optional_access::~bad_optional_access() _NOEXCEPT = default;

const char* bad_optional_access::what() const _NOEXCEPT {
  return "bad_optional_access";
  }

} // std


#include <experimental/__config>

//  Preserve std::experimental::bad_optional_access for ABI compatibility
//  Even though it no longer exists in a header file
_LIBCPP_BEGIN_NAMESPACE_EXPERIMENTAL

class _LIBCPP_EXCEPTION_ABI _LIBCPP_AVAILABILITY_BAD_OPTIONAL_ACCESS bad_optional_access
    : public std::logic_error
{
public:
	bad_optional_access() : std::logic_error("Bad optional Access") {}

//	Get the key function ~bad_optional_access() into the dylib
    virtual ~bad_optional_access() _NOEXCEPT;
};

bad_optional_access::~bad_optional_access() _NOEXCEPT = default;

_LIBCPP_END_NAMESPACE_EXPERIMENTAL
