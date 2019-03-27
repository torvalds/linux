// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

namespace std {

_LIBCPP_SAFE_STATIC static std::new_handler __new_handler;

new_handler
set_new_handler(new_handler handler) _NOEXCEPT
{
    return __libcpp_atomic_exchange(&__new_handler, handler);
}

new_handler
get_new_handler() _NOEXCEPT
{
    return __libcpp_atomic_load(&__new_handler);
}

} // namespace std
