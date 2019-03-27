// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_ABI_MICROSOFT
#error this header can only be used when targeting the MSVC ABI
#endif

#include <stdio.h>
#include <stdlib.h>

extern "C" {
typedef void (__cdecl* terminate_handler)();
_LIBCPP_CRT_FUNC terminate_handler __cdecl set_terminate(
    terminate_handler _NewTerminateHandler) throw();
_LIBCPP_CRT_FUNC terminate_handler __cdecl _get_terminate();

typedef void (__cdecl* unexpected_handler)();
unexpected_handler __cdecl set_unexpected(
    unexpected_handler _NewUnexpectedHandler) throw();
unexpected_handler __cdecl _get_unexpected();

int __cdecl __uncaught_exceptions();
}

namespace std {

unexpected_handler
set_unexpected(unexpected_handler func) _NOEXCEPT {
  return ::set_unexpected(func);
}

unexpected_handler get_unexpected() _NOEXCEPT {
  return ::_get_unexpected();
}

_LIBCPP_NORETURN
void unexpected() {
    (*get_unexpected())();
    // unexpected handler should not return
    terminate();
}

terminate_handler set_terminate(terminate_handler func) _NOEXCEPT {
  return ::set_terminate(func);
}

terminate_handler get_terminate() _NOEXCEPT {
  return ::_get_terminate();
}

_LIBCPP_NORETURN
void terminate() _NOEXCEPT
{
#ifndef _LIBCPP_NO_EXCEPTIONS
    try
    {
#endif  // _LIBCPP_NO_EXCEPTIONS
        (*get_terminate())();
        // handler should not return
        fprintf(stderr, "terminate_handler unexpectedly returned\n");
        ::abort();
#ifndef _LIBCPP_NO_EXCEPTIONS
    }
    catch (...)
    {
        // handler should not throw exception
        fprintf(stderr, "terminate_handler unexpectedly threw an exception\n");
        ::abort();
    }
#endif  // _LIBCPP_NO_EXCEPTIONS
}

bool uncaught_exception() _NOEXCEPT { return uncaught_exceptions() > 0; }

int uncaught_exceptions() _NOEXCEPT {
    return __uncaught_exceptions();
}

#if defined(_LIBCPP_NO_VCRUNTIME)
bad_cast::bad_cast() _NOEXCEPT
{
}

bad_cast::~bad_cast() _NOEXCEPT
{
}

const char *
bad_cast::what() const _NOEXCEPT
{
  return "std::bad_cast";
}

bad_typeid::bad_typeid() _NOEXCEPT
{
}

bad_typeid::~bad_typeid() _NOEXCEPT
{
}

const char *
bad_typeid::what() const _NOEXCEPT
{
  return "std::bad_typeid";
}

exception::~exception() _NOEXCEPT
{
}

const char* exception::what() const _NOEXCEPT
{
  return "std::exception";
}


bad_exception::~bad_exception() _NOEXCEPT
{
}

const char* bad_exception::what() const _NOEXCEPT
{
  return "std::bad_exception";
}


bad_alloc::bad_alloc() _NOEXCEPT
{
}

bad_alloc::~bad_alloc() _NOEXCEPT
{
}

const char*
bad_alloc::what() const _NOEXCEPT
{
    return "std::bad_alloc";
}

bad_array_new_length::bad_array_new_length() _NOEXCEPT
{
}

bad_array_new_length::~bad_array_new_length() _NOEXCEPT
{
}

const char*
bad_array_new_length::what() const _NOEXCEPT
{
    return "bad_array_new_length";
}
#endif // _LIBCPP_NO_VCRUNTIME

} // namespace std
