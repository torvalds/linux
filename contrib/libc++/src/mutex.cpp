//===------------------------- mutex.cpp ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "mutex"
#include "limits"
#include "system_error"
#include "include/atomic_support.h"
#include "__undef_macros"

_LIBCPP_BEGIN_NAMESPACE_STD
#ifndef _LIBCPP_HAS_NO_THREADS

const defer_lock_t  defer_lock = {};
const try_to_lock_t try_to_lock = {};
const adopt_lock_t  adopt_lock = {};

mutex::~mutex()
{
    __libcpp_mutex_destroy(&__m_);
}

void
mutex::lock()
{
    int ec = __libcpp_mutex_lock(&__m_);
    if (ec)
        __throw_system_error(ec, "mutex lock failed");
}

bool
mutex::try_lock() _NOEXCEPT
{
    return __libcpp_mutex_trylock(&__m_);
}

void
mutex::unlock() _NOEXCEPT
{
    int ec = __libcpp_mutex_unlock(&__m_);
    (void)ec;
    _LIBCPP_ASSERT(ec == 0, "call to mutex::unlock failed");
}

// recursive_mutex

recursive_mutex::recursive_mutex()
{
    int ec = __libcpp_recursive_mutex_init(&__m_);
    if (ec)
        __throw_system_error(ec, "recursive_mutex constructor failed");
}

recursive_mutex::~recursive_mutex()
{
    int e = __libcpp_recursive_mutex_destroy(&__m_);
    (void)e;
    _LIBCPP_ASSERT(e == 0, "call to ~recursive_mutex() failed");
}

void
recursive_mutex::lock()
{
    int ec = __libcpp_recursive_mutex_lock(&__m_);
    if (ec)
        __throw_system_error(ec, "recursive_mutex lock failed");
}

void
recursive_mutex::unlock() _NOEXCEPT
{
    int e = __libcpp_recursive_mutex_unlock(&__m_);
    (void)e;
    _LIBCPP_ASSERT(e == 0, "call to recursive_mutex::unlock() failed");
}

bool
recursive_mutex::try_lock() _NOEXCEPT
{
    return __libcpp_recursive_mutex_trylock(&__m_);
}

// timed_mutex

timed_mutex::timed_mutex()
    : __locked_(false)
{
}

timed_mutex::~timed_mutex()
{
    lock_guard<mutex> _(__m_);
}

void
timed_mutex::lock()
{
    unique_lock<mutex> lk(__m_);
    while (__locked_)
        __cv_.wait(lk);
    __locked_ = true;
}

bool
timed_mutex::try_lock() _NOEXCEPT
{
    unique_lock<mutex> lk(__m_, try_to_lock);
    if (lk.owns_lock() && !__locked_)
    {
        __locked_ = true;
        return true;
    }
    return false;
}

void
timed_mutex::unlock() _NOEXCEPT
{
    lock_guard<mutex> _(__m_);
    __locked_ = false;
    __cv_.notify_one();
}

// recursive_timed_mutex

recursive_timed_mutex::recursive_timed_mutex()
    : __count_(0),
      __id_(0)
{
}

recursive_timed_mutex::~recursive_timed_mutex()
{
    lock_guard<mutex> _(__m_);
}

void
recursive_timed_mutex::lock()
{
    __libcpp_thread_id id = __libcpp_thread_get_current_id();
    unique_lock<mutex> lk(__m_);
    if (__libcpp_thread_id_equal(id, __id_))
    {
        if (__count_ == numeric_limits<size_t>::max())
            __throw_system_error(EAGAIN, "recursive_timed_mutex lock limit reached");
        ++__count_;
        return;
    }
    while (__count_ != 0)
        __cv_.wait(lk);
    __count_ = 1;
    __id_ = id;
}

bool
recursive_timed_mutex::try_lock() _NOEXCEPT
{
    __libcpp_thread_id id = __libcpp_thread_get_current_id();
    unique_lock<mutex> lk(__m_, try_to_lock);
    if (lk.owns_lock() && (__count_ == 0 || __libcpp_thread_id_equal(id, __id_)))
    {
        if (__count_ == numeric_limits<size_t>::max())
            return false;
        ++__count_;
        __id_ = id;
        return true;
    }
    return false;
}

void
recursive_timed_mutex::unlock() _NOEXCEPT
{
    unique_lock<mutex> lk(__m_);
    if (--__count_ == 0)
    {
        __id_ = 0;
        lk.unlock();
        __cv_.notify_one();
    }
}

#endif // !_LIBCPP_HAS_NO_THREADS

// If dispatch_once_f ever handles C++ exceptions, and if one can get to it
// without illegal macros (unexpected macros not beginning with _UpperCase or
// __lowercase), and if it stops spinning waiting threads, then call_once should
// call into dispatch_once_f instead of here. Relevant radar this code needs to
// keep in sync with:  7741191.

#ifndef _LIBCPP_HAS_NO_THREADS
_LIBCPP_SAFE_STATIC static __libcpp_mutex_t mut = _LIBCPP_MUTEX_INITIALIZER;
_LIBCPP_SAFE_STATIC static __libcpp_condvar_t cv = _LIBCPP_CONDVAR_INITIALIZER;
#endif

void
__call_once(volatile unsigned long& flag, void* arg, void(*func)(void*))
{
#if defined(_LIBCPP_HAS_NO_THREADS)
    if (flag == 0)
    {
#ifndef _LIBCPP_NO_EXCEPTIONS
        try
        {
#endif  // _LIBCPP_NO_EXCEPTIONS
            flag = 1;
            func(arg);
            flag = ~0ul;
#ifndef _LIBCPP_NO_EXCEPTIONS
        }
        catch (...)
        {
            flag = 0ul;
            throw;
        }
#endif  // _LIBCPP_NO_EXCEPTIONS
    }
#else // !_LIBCPP_HAS_NO_THREADS
    __libcpp_mutex_lock(&mut);
    while (flag == 1)
        __libcpp_condvar_wait(&cv, &mut);
    if (flag == 0)
    {
#ifndef _LIBCPP_NO_EXCEPTIONS
        try
        {
#endif  // _LIBCPP_NO_EXCEPTIONS
            __libcpp_relaxed_store(&flag, 1ul);
            __libcpp_mutex_unlock(&mut);
            func(arg);
            __libcpp_mutex_lock(&mut);
            __libcpp_atomic_store(&flag, ~0ul, _AO_Release);
            __libcpp_mutex_unlock(&mut);
            __libcpp_condvar_broadcast(&cv);
#ifndef _LIBCPP_NO_EXCEPTIONS
        }
        catch (...)
        {
            __libcpp_mutex_lock(&mut);
            __libcpp_relaxed_store(&flag, 0ul);
            __libcpp_mutex_unlock(&mut);
            __libcpp_condvar_broadcast(&cv);
            throw;
        }
#endif  // _LIBCPP_NO_EXCEPTIONS
    }
    else
        __libcpp_mutex_unlock(&mut);
#endif // !_LIBCPP_HAS_NO_THREADS

}

_LIBCPP_END_NAMESPACE_STD
