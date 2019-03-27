//===---------------------- system_error.cpp ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "__config"

#include "system_error"

#include "include/config_elast.h"
#include "cerrno"
#include "cstring"
#include "cstdio"
#include "cstdlib"
#include "string"
#include "string.h"
#include "__debug"

#if defined(__ANDROID__)
#include <android/api-level.h>
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// class error_category

#if defined(_LIBCPP_DEPRECATED_ABI_LEGACY_LIBRARY_DEFINITIONS_FOR_INLINE_FUNCTIONS)
error_category::error_category() _NOEXCEPT
{
}
#endif

error_category::~error_category() _NOEXCEPT
{
}

error_condition
error_category::default_error_condition(int ev) const _NOEXCEPT
{
    return error_condition(ev, *this);
}

bool
error_category::equivalent(int code, const error_condition& condition) const _NOEXCEPT
{
    return default_error_condition(code) == condition;
}

bool
error_category::equivalent(const error_code& code, int condition) const _NOEXCEPT
{
    return *this == code.category() && code.value() == condition;
}

#if !defined(_LIBCPP_HAS_NO_THREADS)
namespace {

//  GLIBC also uses 1024 as the maximum buffer size internally.
constexpr size_t strerror_buff_size = 1024;

string do_strerror_r(int ev);

#if defined(_LIBCPP_MSVCRT_LIKE)
string do_strerror_r(int ev) {
  char buffer[strerror_buff_size];
  if (::strerror_s(buffer, strerror_buff_size, ev) == 0)
    return string(buffer);
  std::snprintf(buffer, strerror_buff_size, "unknown error %d", ev);
  return string(buffer);
}
#else

// Only one of the two following functions will be used, depending on
// the return type of strerror_r:

// For the GNU variant, a char* return value:
__attribute__((unused)) const char *
handle_strerror_r_return(char *strerror_return, char *buffer) {
  // GNU always returns a string pointer in its return value. The
  // string might point to either the input buffer, or a static
  // buffer, but we don't care which.
  return strerror_return;
}

// For the POSIX variant: an int return value.
__attribute__((unused)) const char *
handle_strerror_r_return(int strerror_return, char *buffer) {
  // The POSIX variant either:
  // - fills in the provided buffer and returns 0
  // - returns a positive error value, or
  // - returns -1 and fills in errno with an error value.
  if (strerror_return == 0)
    return buffer;

  // Only handle EINVAL. Other errors abort.
  int new_errno = strerror_return == -1 ? errno : strerror_return;
  if (new_errno == EINVAL)
    return "";

  _LIBCPP_ASSERT(new_errno == ERANGE, "unexpected error from ::strerror_r");
  // FIXME maybe? 'strerror_buff_size' is likely to exceed the
  // maximum error size so ERANGE shouldn't be returned.
  std::abort();
}

// This function handles both GNU and POSIX variants, dispatching to
// one of the two above functions.
string do_strerror_r(int ev) {
    char buffer[strerror_buff_size];
    // Preserve errno around the call. (The C++ standard requires that
    // system_error functions not modify errno).
    const int old_errno = errno;
    const char *error_message = handle_strerror_r_return(
        ::strerror_r(ev, buffer, strerror_buff_size), buffer);
    // If we didn't get any message, print one now.
    if (!error_message[0]) {
      std::snprintf(buffer, strerror_buff_size, "Unknown error %d", ev);
      error_message = buffer;
    }
    errno = old_errno;
    return string(error_message);
}
#endif
} // end namespace
#endif

string
__do_message::message(int ev) const
{
#if defined(_LIBCPP_HAS_NO_THREADS)
    return string(::strerror(ev));
#else
    return do_strerror_r(ev);
#endif
}

class _LIBCPP_HIDDEN __generic_error_category
    : public __do_message
{
public:
    virtual const char* name() const _NOEXCEPT;
    virtual string message(int ev) const;
};

const char*
__generic_error_category::name() const _NOEXCEPT
{
    return "generic";
}

string
__generic_error_category::message(int ev) const
{
#ifdef _LIBCPP_ELAST
    if (ev > _LIBCPP_ELAST)
      return string("unspecified generic_category error");
#endif  // _LIBCPP_ELAST
    return __do_message::message(ev);
}

const error_category&
generic_category() _NOEXCEPT
{
    static __generic_error_category s;
    return s;
}

class _LIBCPP_HIDDEN __system_error_category
    : public __do_message
{
public:
    virtual const char* name() const _NOEXCEPT;
    virtual string message(int ev) const;
    virtual error_condition default_error_condition(int ev) const _NOEXCEPT;
};

const char*
__system_error_category::name() const _NOEXCEPT
{
    return "system";
}

string
__system_error_category::message(int ev) const
{
#ifdef _LIBCPP_ELAST
    if (ev > _LIBCPP_ELAST)
      return string("unspecified system_category error");
#endif  // _LIBCPP_ELAST
    return __do_message::message(ev);
}

error_condition
__system_error_category::default_error_condition(int ev) const _NOEXCEPT
{
#ifdef _LIBCPP_ELAST
    if (ev > _LIBCPP_ELAST)
      return error_condition(ev, system_category());
#endif  // _LIBCPP_ELAST
    return error_condition(ev, generic_category());
}

const error_category&
system_category() _NOEXCEPT
{
    static __system_error_category s;
    return s;
}

// error_condition

string
error_condition::message() const
{
    return __cat_->message(__val_);
}

// error_code

string
error_code::message() const
{
    return __cat_->message(__val_);
}

// system_error

string
system_error::__init(const error_code& ec, string what_arg)
{
    if (ec)
    {
        if (!what_arg.empty())
            what_arg += ": ";
        what_arg += ec.message();
    }
    return what_arg;
}

system_error::system_error(error_code ec, const string& what_arg)
    : runtime_error(__init(ec, what_arg)),
      __ec_(ec)
{
}

system_error::system_error(error_code ec, const char* what_arg)
    : runtime_error(__init(ec, what_arg)),
      __ec_(ec)
{
}

system_error::system_error(error_code ec)
    : runtime_error(__init(ec, "")),
      __ec_(ec)
{
}

system_error::system_error(int ev, const error_category& ecat, const string& what_arg)
    : runtime_error(__init(error_code(ev, ecat), what_arg)),
      __ec_(error_code(ev, ecat))
{
}

system_error::system_error(int ev, const error_category& ecat, const char* what_arg)
    : runtime_error(__init(error_code(ev, ecat), what_arg)),
      __ec_(error_code(ev, ecat))
{
}

system_error::system_error(int ev, const error_category& ecat)
    : runtime_error(__init(error_code(ev, ecat), "")),
      __ec_(error_code(ev, ecat))
{
}

system_error::~system_error() _NOEXCEPT
{
}

void
__throw_system_error(int ev, const char* what_arg)
{
#ifndef _LIBCPP_NO_EXCEPTIONS
    throw system_error(error_code(ev, system_category()), what_arg);
#else
    (void)ev;
    (void)what_arg;
    _VSTD::abort();
#endif
}

_LIBCPP_END_NAMESPACE_STD
