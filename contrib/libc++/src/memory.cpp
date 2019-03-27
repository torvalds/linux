//===------------------------ memory.cpp ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "memory"
#ifndef _LIBCPP_HAS_NO_THREADS
#include "mutex"
#include "thread"
#endif
#include "include/atomic_support.h"

_LIBCPP_BEGIN_NAMESPACE_STD

const allocator_arg_t allocator_arg = allocator_arg_t();

bad_weak_ptr::~bad_weak_ptr() _NOEXCEPT {}

const char*
bad_weak_ptr::what() const _NOEXCEPT
{
    return "bad_weak_ptr";
}

__shared_count::~__shared_count()
{
}

__shared_weak_count::~__shared_weak_count()
{
}

#if defined(_LIBCPP_DEPRECATED_ABI_LEGACY_LIBRARY_DEFINITIONS_FOR_INLINE_FUNCTIONS)
void
__shared_count::__add_shared() _NOEXCEPT
{
    __libcpp_atomic_refcount_increment(__shared_owners_);
}

bool
__shared_count::__release_shared() _NOEXCEPT
{
    if (__libcpp_atomic_refcount_decrement(__shared_owners_) == -1)
    {
        __on_zero_shared();
        return true;
    }
    return false;
}

void
__shared_weak_count::__add_shared() _NOEXCEPT
{
    __shared_count::__add_shared();
}

void
__shared_weak_count::__add_weak() _NOEXCEPT
{
    __libcpp_atomic_refcount_increment(__shared_weak_owners_);
}

void
__shared_weak_count::__release_shared() _NOEXCEPT
{
    if (__shared_count::__release_shared())
        __release_weak();
}

#endif // _LIBCPP_DEPRECATED_ABI_LEGACY_LIBRARY_DEFINITIONS_FOR_INLINE_FUNCTIONS

void
__shared_weak_count::__release_weak() _NOEXCEPT
{
    // NOTE: The acquire load here is an optimization of the very
    // common case where a shared pointer is being destructed while
    // having no other contended references.
    //
    // BENEFIT: We avoid expensive atomic stores like XADD and STREX
    // in a common case.  Those instructions are slow and do nasty
    // things to caches.
    //
    // IS THIS SAFE?  Yes.  During weak destruction, if we see that we
    // are the last reference, we know that no-one else is accessing
    // us. If someone were accessing us, then they would be doing so
    // while the last shared / weak_ptr was being destructed, and
    // that's undefined anyway.
    //
    // If we see anything other than a 0, then we have possible
    // contention, and need to use an atomicrmw primitive.
    // The same arguments don't apply for increment, where it is legal
    // (though inadvisable) to share shared_ptr references between
    // threads, and have them all get copied at once.  The argument
    // also doesn't apply for __release_shared, because an outstanding
    // weak_ptr::lock() could read / modify the shared count.
    if (__libcpp_atomic_load(&__shared_weak_owners_, _AO_Acquire) == 0)
    {
        // no need to do this store, because we are about
        // to destroy everything.
        //__libcpp_atomic_store(&__shared_weak_owners_, -1, _AO_Release);
        __on_zero_shared_weak();
    }
    else if (__libcpp_atomic_refcount_decrement(__shared_weak_owners_) == -1)
        __on_zero_shared_weak();
}

__shared_weak_count*
__shared_weak_count::lock() _NOEXCEPT
{
    long object_owners = __libcpp_atomic_load(&__shared_owners_);
    while (object_owners != -1)
    {
        if (__libcpp_atomic_compare_exchange(&__shared_owners_,
                                             &object_owners,
                                             object_owners+1))
            return this;
    }
    return nullptr;
}

#if !defined(_LIBCPP_NO_RTTI) || !defined(_LIBCPP_BUILD_STATIC)

const void*
__shared_weak_count::__get_deleter(const type_info&) const _NOEXCEPT
{
    return nullptr;
}

#endif  // _LIBCPP_NO_RTTI

#if !defined(_LIBCPP_HAS_NO_ATOMIC_HEADER)

_LIBCPP_SAFE_STATIC static const std::size_t __sp_mut_count = 16;
_LIBCPP_SAFE_STATIC static __libcpp_mutex_t mut_back[__sp_mut_count] =
{
    _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER,
    _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER,
    _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER,
    _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER, _LIBCPP_MUTEX_INITIALIZER
};

_LIBCPP_CONSTEXPR __sp_mut::__sp_mut(void* p) _NOEXCEPT
   : __lx(p)
{
}

void
__sp_mut::lock() _NOEXCEPT
{
    auto m = static_cast<__libcpp_mutex_t*>(__lx);
    unsigned count = 0;
    while (!__libcpp_mutex_trylock(m))
    {
        if (++count > 16)
        {
            __libcpp_mutex_lock(m);
            break;
        }
        this_thread::yield();
    }
}

void
__sp_mut::unlock() _NOEXCEPT
{
    __libcpp_mutex_unlock(static_cast<__libcpp_mutex_t*>(__lx));
}

__sp_mut&
__get_sp_mut(const void* p)
{
    static __sp_mut muts[__sp_mut_count]
    {
        &mut_back[ 0], &mut_back[ 1], &mut_back[ 2], &mut_back[ 3],
        &mut_back[ 4], &mut_back[ 5], &mut_back[ 6], &mut_back[ 7],
        &mut_back[ 8], &mut_back[ 9], &mut_back[10], &mut_back[11],
        &mut_back[12], &mut_back[13], &mut_back[14], &mut_back[15]
    };
    return muts[hash<const void*>()(p) & (__sp_mut_count-1)];
}

#endif // !defined(_LIBCPP_HAS_NO_ATOMIC_HEADER)

void
declare_reachable(void*)
{
}

void
declare_no_pointers(char*, size_t)
{
}

void
undeclare_no_pointers(char*, size_t)
{
}

#if !defined(_LIBCPP_ABI_POINTER_SAFETY_ENUM_TYPE)
pointer_safety get_pointer_safety() _NOEXCEPT
{
    return pointer_safety::relaxed;
}
#endif

void*
__undeclare_reachable(void* p)
{
    return p;
}

void*
align(size_t alignment, size_t size, void*& ptr, size_t& space)
{
    void* r = nullptr;
    if (size <= space)
    {
        char* p1 = static_cast<char*>(ptr);
        char* p2 = reinterpret_cast<char*>(reinterpret_cast<size_t>(p1 + (alignment - 1)) & -alignment);
        size_t d = static_cast<size_t>(p2 - p1);
        if (d <= space - size)
        {
            r = p2;
            ptr = r;
            space -= d;
        }
    }
    return r;
}

_LIBCPP_END_NAMESPACE_STD
