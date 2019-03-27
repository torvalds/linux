//===------------------------- future.cpp ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "__config"

#ifndef _LIBCPP_HAS_NO_THREADS

#include "future"
#include "string"

_LIBCPP_BEGIN_NAMESPACE_STD

class _LIBCPP_HIDDEN __future_error_category
    : public __do_message
{
public:
    virtual const char* name() const _NOEXCEPT;
    virtual string message(int ev) const;
};

const char*
__future_error_category::name() const _NOEXCEPT
{
    return "future";
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch"
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
#endif

string
__future_error_category::message(int ev) const
{
    switch (static_cast<future_errc>(ev))
    {
    case future_errc(0):  // For backwards compatibility with C++11 (LWG 2056)
    case future_errc::broken_promise:
        return string("The associated promise has been destructed prior "
                      "to the associated state becoming ready.");
    case future_errc::future_already_retrieved:
        return string("The future has already been retrieved from "
                      "the promise or packaged_task.");
    case future_errc::promise_already_satisfied:
        return string("The state of the promise has already been set.");
    case future_errc::no_state:
        return string("Operation not permitted on an object without "
                      "an associated state.");
    }
    return string("unspecified future_errc value\n");
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#endif

const error_category&
future_category() _NOEXCEPT
{
    static __future_error_category __f;
    return __f;
}

future_error::future_error(error_code __ec)
    : logic_error(__ec.message()),
      __ec_(__ec)
{
}

future_error::~future_error() _NOEXCEPT
{
}

void
__assoc_sub_state::__on_zero_shared() _NOEXCEPT
{
    delete this;
}

void
__assoc_sub_state::set_value()
{
    unique_lock<mutex> __lk(__mut_);
    if (__has_value())
        __throw_future_error(future_errc::promise_already_satisfied);
    __state_ |= __constructed | ready;
    __cv_.notify_all();
}

void
__assoc_sub_state::set_value_at_thread_exit()
{
    unique_lock<mutex> __lk(__mut_);
    if (__has_value())
        __throw_future_error(future_errc::promise_already_satisfied);
    __state_ |= __constructed;
    __thread_local_data()->__make_ready_at_thread_exit(this);
}

void
__assoc_sub_state::set_exception(exception_ptr __p)
{
    unique_lock<mutex> __lk(__mut_);
    if (__has_value())
        __throw_future_error(future_errc::promise_already_satisfied);
    __exception_ = __p;
    __state_ |= ready;
    __cv_.notify_all();
}

void
__assoc_sub_state::set_exception_at_thread_exit(exception_ptr __p)
{
    unique_lock<mutex> __lk(__mut_);
    if (__has_value())
        __throw_future_error(future_errc::promise_already_satisfied);
    __exception_ = __p;
    __thread_local_data()->__make_ready_at_thread_exit(this);
}

void
__assoc_sub_state::__make_ready()
{
    unique_lock<mutex> __lk(__mut_);
    __state_ |= ready;
    __cv_.notify_all();
}

void
__assoc_sub_state::copy()
{
    unique_lock<mutex> __lk(__mut_);
    __sub_wait(__lk);
    if (__exception_ != nullptr)
        rethrow_exception(__exception_);
}

void
__assoc_sub_state::wait()
{
    unique_lock<mutex> __lk(__mut_);
    __sub_wait(__lk);
}

void
__assoc_sub_state::__sub_wait(unique_lock<mutex>& __lk)
{
    if (!__is_ready())
    {
        if (__state_ & static_cast<unsigned>(deferred))
        {
            __state_ &= ~static_cast<unsigned>(deferred);
            __lk.unlock();
            __execute();
        }
        else
            while (!__is_ready())
                __cv_.wait(__lk);
    }
}

void
__assoc_sub_state::__execute()
{
    __throw_future_error(future_errc::no_state);
}

future<void>::future(__assoc_sub_state* __state)
    : __state_(__state)
{
    __state_->__attach_future();
}

future<void>::~future()
{
    if (__state_)
        __state_->__release_shared();
}

void
future<void>::get()
{
    unique_ptr<__shared_count, __release_shared_count> __(__state_);
    __assoc_sub_state* __s = __state_;
    __state_ = nullptr;
    __s->copy();
}

promise<void>::promise()
    : __state_(new __assoc_sub_state)
{
}

promise<void>::~promise()
{
    if (__state_)
    {
#ifndef _LIBCPP_NO_EXCEPTIONS
        if (!__state_->__has_value() && __state_->use_count() > 1)
            __state_->set_exception(make_exception_ptr(
                      future_error(make_error_code(future_errc::broken_promise))
                                                      ));
#endif // _LIBCPP_NO_EXCEPTIONS
        __state_->__release_shared();
    }
}

future<void>
promise<void>::get_future()
{
    if (__state_ == nullptr)
        __throw_future_error(future_errc::no_state);
    return future<void>(__state_);
}

void
promise<void>::set_value()
{
    if (__state_ == nullptr)
        __throw_future_error(future_errc::no_state);
    __state_->set_value();
}

void
promise<void>::set_exception(exception_ptr __p)
{
    if (__state_ == nullptr)
        __throw_future_error(future_errc::no_state);
    __state_->set_exception(__p);
}

void
promise<void>::set_value_at_thread_exit()
{
    if (__state_ == nullptr)
        __throw_future_error(future_errc::no_state);
    __state_->set_value_at_thread_exit();
}

void
promise<void>::set_exception_at_thread_exit(exception_ptr __p)
{
    if (__state_ == nullptr)
        __throw_future_error(future_errc::no_state);
    __state_->set_exception_at_thread_exit(__p);
}

shared_future<void>::~shared_future()
{
    if (__state_)
        __state_->__release_shared();
}

shared_future<void>&
shared_future<void>::operator=(const shared_future& __rhs)
{
    if (__rhs.__state_)
        __rhs.__state_->__add_shared();
    if (__state_)
        __state_->__release_shared();
    __state_ = __rhs.__state_;
    return *this;
}

_LIBCPP_END_NAMESPACE_STD

#endif // !_LIBCPP_HAS_NO_THREADS
