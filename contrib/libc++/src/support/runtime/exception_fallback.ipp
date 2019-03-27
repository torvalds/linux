// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <cstdio>

namespace std {

_LIBCPP_SAFE_STATIC static std::terminate_handler  __terminate_handler;
_LIBCPP_SAFE_STATIC static std::unexpected_handler __unexpected_handler;


// libcxxrt provides implementations of these functions itself.
unexpected_handler
set_unexpected(unexpected_handler func) _NOEXCEPT
{
  return __libcpp_atomic_exchange(&__unexpected_handler, func);
}

unexpected_handler
get_unexpected() _NOEXCEPT
{
  return __libcpp_atomic_load(&__unexpected_handler);

}

_LIBCPP_NORETURN
void unexpected()
{
    (*get_unexpected())();
    // unexpected handler should not return
    terminate();
}

terminate_handler
set_terminate(terminate_handler func) _NOEXCEPT
{
  return __libcpp_atomic_exchange(&__terminate_handler, func);
}

terminate_handler
get_terminate() _NOEXCEPT
{
  return __libcpp_atomic_load(&__terminate_handler);
}

#ifndef __EMSCRIPTEN__ // We provide this in JS
_LIBCPP_NORETURN
void
terminate() _NOEXCEPT
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
#endif // !__EMSCRIPTEN__

#if !defined(__EMSCRIPTEN__)
bool uncaught_exception() _NOEXCEPT { return uncaught_exceptions() > 0; }

int uncaught_exceptions() _NOEXCEPT
{
#warning uncaught_exception not yet implemented
  fprintf(stderr, "uncaught_exceptions not yet implemented\n");
  ::abort();
}
#endif // !__EMSCRIPTEN__


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

bad_cast::bad_cast() _NOEXCEPT
{
}

bad_typeid::bad_typeid() _NOEXCEPT
{
}

bad_cast::~bad_cast() _NOEXCEPT
{
}

const char*
bad_cast::what() const _NOEXCEPT
{
  return "std::bad_cast";
}

bad_typeid::~bad_typeid() _NOEXCEPT
{
}

const char*
bad_typeid::what() const _NOEXCEPT
{
  return "std::bad_typeid";
}

} // namespace std
