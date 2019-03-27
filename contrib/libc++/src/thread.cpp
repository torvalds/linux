//===------------------------- thread.cpp----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "__config"
#ifndef _LIBCPP_HAS_NO_THREADS

#include "thread"
#include "exception"
#include "vector"
#include "future"
#include "limits"
#include <sys/types.h>

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
# include <sys/param.h>
# if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__APPLE__)
#   include <sys/sysctl.h>
# endif
#endif // defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__)) || defined(__CloudABI__) || defined(__Fuchsia__)
# include <unistd.h>
#endif // defined(__unix__) || (defined(__APPLE__) && defined(__MACH__)) || defined(__CloudABI__) || defined(__Fuchsia__)

#if defined(__NetBSD__)
#pragma weak pthread_create // Do not create libpthread dependency
#endif

#if defined(_LIBCPP_WIN32API)
#include <windows.h>
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

thread::~thread()
{
    if (!__libcpp_thread_isnull(&__t_))
        terminate();
}

void
thread::join()
{
    int ec = EINVAL;
    if (!__libcpp_thread_isnull(&__t_))
    {
        ec = __libcpp_thread_join(&__t_);
        if (ec == 0)
            __t_ = _LIBCPP_NULL_THREAD;
    }

    if (ec)
        __throw_system_error(ec, "thread::join failed");
}

void
thread::detach()
{
    int ec = EINVAL;
    if (!__libcpp_thread_isnull(&__t_))
    {
        ec = __libcpp_thread_detach(&__t_);
        if (ec == 0)
            __t_ = _LIBCPP_NULL_THREAD;
    }

    if (ec)
        __throw_system_error(ec, "thread::detach failed");
}

unsigned
thread::hardware_concurrency() _NOEXCEPT
{
#if defined(CTL_HW) && defined(HW_NCPU)
    unsigned n;
    int mib[2] = {CTL_HW, HW_NCPU};
    std::size_t s = sizeof(n);
    sysctl(mib, 2, &n, &s, 0, 0);
    return n;
#elif defined(_SC_NPROCESSORS_ONLN)
    long result = sysconf(_SC_NPROCESSORS_ONLN);
    // sysconf returns -1 if the name is invalid, the option does not exist or
    // does not have a definite limit.
    // if sysconf returns some other negative number, we have no idea
    // what is going on. Default to something safe.
    if (result < 0)
        return 0;
    return static_cast<unsigned>(result);
#elif defined(_LIBCPP_WIN32API)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
#else  // defined(CTL_HW) && defined(HW_NCPU)
    // TODO: grovel through /proc or check cpuid on x86 and similar
    // instructions on other architectures.
#   if defined(_LIBCPP_WARNING)
        _LIBCPP_WARNING("hardware_concurrency not yet implemented")
#   else
#       warning hardware_concurrency not yet implemented
#   endif
    return 0;  // Means not computable [thread.thread.static]
#endif  // defined(CTL_HW) && defined(HW_NCPU)
}

namespace this_thread
{

void
sleep_for(const chrono::nanoseconds& ns)
{
    if (ns > chrono::nanoseconds::zero())
    {
        __libcpp_thread_sleep_for(ns);
    }
}

}  // this_thread

__thread_specific_ptr<__thread_struct>&
__thread_local_data()
{
    static __thread_specific_ptr<__thread_struct> __p;
    return __p;
}

// __thread_struct_imp

template <class T>
class _LIBCPP_HIDDEN __hidden_allocator
{
public:
    typedef T  value_type;
    
    T* allocate(size_t __n)
        {return static_cast<T*>(::operator new(__n * sizeof(T)));}
    void deallocate(T* __p, size_t) {::operator delete(static_cast<void*>(__p));}

    size_t max_size() const {return size_t(~0) / sizeof(T);}
};

class _LIBCPP_HIDDEN __thread_struct_imp
{
    typedef vector<__assoc_sub_state*,
                          __hidden_allocator<__assoc_sub_state*> > _AsyncStates;
    typedef vector<pair<condition_variable*, mutex*>,
               __hidden_allocator<pair<condition_variable*, mutex*> > > _Notify;

    _AsyncStates async_states_;
    _Notify notify_;

    __thread_struct_imp(const __thread_struct_imp&);
    __thread_struct_imp& operator=(const __thread_struct_imp&);
public:
    __thread_struct_imp() {}
    ~__thread_struct_imp();

    void notify_all_at_thread_exit(condition_variable* cv, mutex* m);
    void __make_ready_at_thread_exit(__assoc_sub_state* __s);
};

__thread_struct_imp::~__thread_struct_imp()
{
    for (_Notify::iterator i = notify_.begin(), e = notify_.end();
            i != e; ++i)
    {
        i->second->unlock();
        i->first->notify_all();
    }
    for (_AsyncStates::iterator i = async_states_.begin(), e = async_states_.end();
            i != e; ++i)
    {
        (*i)->__make_ready();
        (*i)->__release_shared();
    }
}

void
__thread_struct_imp::notify_all_at_thread_exit(condition_variable* cv, mutex* m)
{
    notify_.push_back(pair<condition_variable*, mutex*>(cv, m));
}

void
__thread_struct_imp::__make_ready_at_thread_exit(__assoc_sub_state* __s)
{
    async_states_.push_back(__s);
    __s->__add_shared();
}

// __thread_struct

__thread_struct::__thread_struct()
    : __p_(new __thread_struct_imp)
{
}

__thread_struct::~__thread_struct()
{
    delete __p_;
}

void
__thread_struct::notify_all_at_thread_exit(condition_variable* cv, mutex* m)
{
    __p_->notify_all_at_thread_exit(cv, m);
}

void
__thread_struct::__make_ready_at_thread_exit(__assoc_sub_state* __s)
{
    __p_->__make_ready_at_thread_exit(__s);
}

_LIBCPP_END_NAMESPACE_STD

#endif // !_LIBCPP_HAS_NO_THREADS
