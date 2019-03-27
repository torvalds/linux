// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>

_LIBCPP_CRT_FUNC void __cdecl __ExceptionPtrCreate(void*);
_LIBCPP_CRT_FUNC void __cdecl __ExceptionPtrDestroy(void*);
_LIBCPP_CRT_FUNC void __cdecl __ExceptionPtrCopy(void*, const void*);
_LIBCPP_CRT_FUNC void __cdecl __ExceptionPtrAssign(void*, const void*);
_LIBCPP_CRT_FUNC bool __cdecl __ExceptionPtrCompare(const void*, const void*);
_LIBCPP_CRT_FUNC bool __cdecl __ExceptionPtrToBool(const void*);
_LIBCPP_CRT_FUNC void __cdecl __ExceptionPtrSwap(void*, void*);
_LIBCPP_CRT_FUNC void __cdecl __ExceptionPtrCurrentException(void*);
[[noreturn]] _LIBCPP_CRT_FUNC void __cdecl __ExceptionPtrRethrow(const void*);
_LIBCPP_CRT_FUNC void __cdecl
__ExceptionPtrCopyException(void*, const void*, const void*);

namespace std {

exception_ptr::exception_ptr() _NOEXCEPT { __ExceptionPtrCreate(this); }
exception_ptr::exception_ptr(nullptr_t) _NOEXCEPT { __ExceptionPtrCreate(this); }

exception_ptr::exception_ptr(const exception_ptr& __other) _NOEXCEPT {
  __ExceptionPtrCopy(this, &__other);
}
exception_ptr& exception_ptr::operator=(const exception_ptr& __other) _NOEXCEPT {
  __ExceptionPtrAssign(this, &__other);
  return *this;
}

exception_ptr& exception_ptr::operator=(nullptr_t) _NOEXCEPT {
  exception_ptr dummy;
  __ExceptionPtrAssign(this, &dummy);
  return *this;
}

exception_ptr::~exception_ptr() _NOEXCEPT { __ExceptionPtrDestroy(this); }

exception_ptr::operator bool() const _NOEXCEPT {
  return __ExceptionPtrToBool(this);
}

bool operator==(const exception_ptr& __x, const exception_ptr& __y) _NOEXCEPT {
  return __ExceptionPtrCompare(&__x, &__y);
}


void swap(exception_ptr& lhs, exception_ptr& rhs) _NOEXCEPT {
  __ExceptionPtrSwap(&rhs, &lhs);
}

exception_ptr __copy_exception_ptr(void* __except, const void* __ptr) {
  exception_ptr __ret = nullptr;
  if (__ptr)
    __ExceptionPtrCopyException(&__ret, __except, __ptr);
  return __ret;
}

exception_ptr current_exception() _NOEXCEPT {
  exception_ptr __ret;
  __ExceptionPtrCurrentException(&__ret);
  return __ret;
}

_LIBCPP_NORETURN
void rethrow_exception(exception_ptr p) { __ExceptionPtrRethrow(&p); }

nested_exception::nested_exception() _NOEXCEPT : __ptr_(current_exception()) {}

nested_exception::~nested_exception() _NOEXCEPT {}

_LIBCPP_NORETURN
void nested_exception::rethrow_nested() const {
  if (__ptr_ == nullptr)
    terminate();
  rethrow_exception(__ptr_);
}

} // namespace std
