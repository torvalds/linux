//===------------------------ stdexcept.cpp -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "stdexcept"
#include "new"
#include "string"
#include "system_error"
#include "include/refstring.h"

/* For _LIBCPPABI_VERSION */
#if !defined(_LIBCPP_BUILDING_HAS_NO_ABI_LIBRARY) && \
    (defined(LIBCXX_BUILDING_LIBCXXABI) || defined(__APPLE__) || defined(LIBCXXRT))
#include <cxxabi.h>
#endif

static_assert(sizeof(std::__libcpp_refstring) == sizeof(const char *), "");


namespace std  // purposefully not using versioning namespace
{

logic_error::logic_error(const string& msg) : __imp_(msg.c_str())
{
}

logic_error::logic_error(const char* msg) : __imp_(msg)
{
}

logic_error::logic_error(const logic_error& le) _NOEXCEPT : __imp_(le.__imp_)
{
}

logic_error&
logic_error::operator=(const logic_error& le) _NOEXCEPT
{
    __imp_ = le.__imp_;
    return *this;
}

#if !defined(_LIBCPPABI_VERSION) && !defined(LIBSTDCXX)

logic_error::~logic_error() _NOEXCEPT
{
}

const char*
logic_error::what() const _NOEXCEPT
{
    return __imp_.c_str();
}

#endif

runtime_error::runtime_error(const string& msg) : __imp_(msg.c_str())
{
}

runtime_error::runtime_error(const char* msg) : __imp_(msg)
{
}

runtime_error::runtime_error(const runtime_error& le) _NOEXCEPT
  : __imp_(le.__imp_)
{
}

runtime_error&
runtime_error::operator=(const runtime_error& le) _NOEXCEPT
{
    __imp_ = le.__imp_;
    return *this;
}

#if !defined(_LIBCPPABI_VERSION) && !defined(LIBSTDCXX)

runtime_error::~runtime_error() _NOEXCEPT
{
}

const char*
runtime_error::what() const _NOEXCEPT
{
    return __imp_.c_str();
}

domain_error::~domain_error() _NOEXCEPT {}
invalid_argument::~invalid_argument() _NOEXCEPT {}
length_error::~length_error() _NOEXCEPT {}
out_of_range::~out_of_range() _NOEXCEPT {}

range_error::~range_error() _NOEXCEPT {}
overflow_error::~overflow_error() _NOEXCEPT {}
underflow_error::~underflow_error() _NOEXCEPT {}

#endif

}  // std
