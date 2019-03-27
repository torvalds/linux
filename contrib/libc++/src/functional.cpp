//===----------------------- functional.cpp -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "functional"

_LIBCPP_BEGIN_NAMESPACE_STD

#ifdef _LIBCPP_ABI_BAD_FUNCTION_CALL_KEY_FUNCTION
bad_function_call::~bad_function_call() _NOEXCEPT
{
}

const char*
bad_function_call::what() const _NOEXCEPT
{
    return "std::bad_function_call";
}
#endif

_LIBCPP_END_NAMESPACE_STD
