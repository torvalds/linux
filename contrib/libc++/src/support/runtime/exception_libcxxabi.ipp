// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPPABI_VERSION
#error this header can only be used with libc++abi
#endif

namespace std {

bool uncaught_exception() _NOEXCEPT { return uncaught_exceptions() > 0; }

int uncaught_exceptions() _NOEXCEPT
{
# if _LIBCPPABI_VERSION > 1001
    return __cxa_uncaught_exceptions();
# else
    return __cxa_uncaught_exception() ? 1 : 0;
# endif
}

} // namespace std
