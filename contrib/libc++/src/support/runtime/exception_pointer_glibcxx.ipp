// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// libsupc++ does not implement the dependent EH ABI and the functionality
// it uses to implement std::exception_ptr (which it declares as an alias of
// std::__exception_ptr::exception_ptr) is not directly exported to clients. So
// we have little choice but to hijack std::__exception_ptr::exception_ptr's
// (which fortunately has the same layout as our std::exception_ptr) copy
// constructor, assignment operator and destructor (which are part of its
// stable ABI), and its rethrow_exception(std::__exception_ptr::exception_ptr)
// function.

namespace std {

namespace __exception_ptr
{

struct exception_ptr
{
    void* __ptr_;

    exception_ptr(const exception_ptr&) _NOEXCEPT;
    exception_ptr& operator=(const exception_ptr&) _NOEXCEPT;
    ~exception_ptr() _NOEXCEPT;
};

}

_LIBCPP_NORETURN void rethrow_exception(__exception_ptr::exception_ptr);

exception_ptr::~exception_ptr() _NOEXCEPT
{
    reinterpret_cast<__exception_ptr::exception_ptr*>(this)->~exception_ptr();
}

exception_ptr::exception_ptr(const exception_ptr& other) _NOEXCEPT
    : __ptr_(other.__ptr_)
{
    new (reinterpret_cast<void*>(this)) __exception_ptr::exception_ptr(
        reinterpret_cast<const __exception_ptr::exception_ptr&>(other));
}

exception_ptr& exception_ptr::operator=(const exception_ptr& other) _NOEXCEPT
{
    *reinterpret_cast<__exception_ptr::exception_ptr*>(this) =
        reinterpret_cast<const __exception_ptr::exception_ptr&>(other);
    return *this;
}

nested_exception::nested_exception() _NOEXCEPT
    : __ptr_(current_exception())
{
}


_LIBCPP_NORETURN
void
nested_exception::rethrow_nested() const
{
    if (__ptr_ == nullptr)
        terminate();
    rethrow_exception(__ptr_);
}

_LIBCPP_NORETURN
void rethrow_exception(exception_ptr p)
{
    rethrow_exception(reinterpret_cast<__exception_ptr::exception_ptr&>(p));
}

} // namespace std
