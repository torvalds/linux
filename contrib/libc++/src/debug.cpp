//===-------------------------- debug.cpp ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "__config"
#include "__debug"
#include "functional"
#include "algorithm"
#include "string"
#include "cstdio"
#include "__hash_table"
#include "mutex"

_LIBCPP_BEGIN_NAMESPACE_STD

static std::string make_what_str(__libcpp_debug_info const& info) {
  string msg = info.__file_;
  msg += ":" + to_string(info.__line_) + ": _LIBCPP_ASSERT '";
  msg += info.__pred_;
  msg += "' failed. ";
  msg += info.__msg_;
  return msg;
}

_LIBCPP_SAFE_STATIC __libcpp_debug_function_type
    __libcpp_debug_function = __libcpp_abort_debug_function;

bool __libcpp_set_debug_function(__libcpp_debug_function_type __func) {
  __libcpp_debug_function = __func;
  return true;
}

_LIBCPP_NORETURN void __libcpp_abort_debug_function(__libcpp_debug_info const& info) {
  std::fprintf(stderr, "%s\n", make_what_str(info).c_str());
  std::abort();
}

_LIBCPP_NORETURN void __libcpp_throw_debug_function(__libcpp_debug_info const& info) {
#ifndef _LIBCPP_NO_EXCEPTIONS
  throw __libcpp_debug_exception(info);
#else
  __libcpp_abort_debug_function(info);
#endif
}

struct __libcpp_debug_exception::__libcpp_debug_exception_imp {
  __libcpp_debug_info __info_;
  std::string __what_str_;
};

__libcpp_debug_exception::__libcpp_debug_exception() _NOEXCEPT
    : __imp_(nullptr) {
}

__libcpp_debug_exception::__libcpp_debug_exception(
    __libcpp_debug_info const& info) : __imp_(new __libcpp_debug_exception_imp)
{
  __imp_->__info_ = info;
  __imp_->__what_str_ = make_what_str(info);
}
__libcpp_debug_exception::__libcpp_debug_exception(
    __libcpp_debug_exception const& other) : __imp_(nullptr) {
  if (other.__imp_)
    __imp_ = new __libcpp_debug_exception_imp(*other.__imp_);
}

__libcpp_debug_exception::~__libcpp_debug_exception() _NOEXCEPT {
  if (__imp_)
    delete __imp_;
}

const char* __libcpp_debug_exception::what() const _NOEXCEPT {
  if (__imp_)
    return __imp_->__what_str_.c_str();
  return "__libcpp_debug_exception";
}

_LIBCPP_FUNC_VIS
__libcpp_db*
__get_db()
{
    static __libcpp_db db;
    return &db;
}

_LIBCPP_FUNC_VIS
const __libcpp_db*
__get_const_db()
{
    return __get_db();
}

namespace
{

#ifndef _LIBCPP_HAS_NO_THREADS
typedef mutex mutex_type;
typedef lock_guard<mutex_type> WLock;
typedef lock_guard<mutex_type> RLock;

mutex_type&
mut()
{
    static mutex_type m;
    return m;
}
#endif // !_LIBCPP_HAS_NO_THREADS

}  // unnamed namespace

__i_node::~__i_node()
{
    if (__next_)
    {
        __next_->~__i_node();
        free(__next_);
    }
}

__c_node::~__c_node()
{
    free(beg_);
    if (__next_)
    {
        __next_->~__c_node();
        free(__next_);
    }
}

__libcpp_db::__libcpp_db()
    : __cbeg_(nullptr),
      __cend_(nullptr),
      __csz_(0),
      __ibeg_(nullptr),
      __iend_(nullptr),
      __isz_(0)
{
}

__libcpp_db::~__libcpp_db()
{
    if (__cbeg_)
    {
        for (__c_node** p = __cbeg_; p != __cend_; ++p)
        {
            if (*p != nullptr)
            {
                (*p)->~__c_node();
                free(*p);
            }
        }
        free(__cbeg_);
    }
    if (__ibeg_)
    {
        for (__i_node** p = __ibeg_; p != __iend_; ++p)
        {
            if (*p != nullptr)
            {
                (*p)->~__i_node();
                free(*p);
            }
        }
        free(__ibeg_);
    }
}

void*
__libcpp_db::__find_c_from_i(void* __i) const
{
#ifndef _LIBCPP_HAS_NO_THREADS
    RLock _(mut());
#endif
    __i_node* i = __find_iterator(__i);
    _LIBCPP_ASSERT(i != nullptr, "iterator not found in debug database.");
    return i->__c_ != nullptr ? i->__c_->__c_ : nullptr;
}

void
__libcpp_db::__insert_ic(void* __i, const void* __c)
{
#ifndef _LIBCPP_HAS_NO_THREADS
    WLock _(mut());
#endif
    if (__cbeg_ == __cend_)
        return;
    size_t hc = hash<const void*>()(__c) % static_cast<size_t>(__cend_ - __cbeg_);
    __c_node* c = __cbeg_[hc];
    if (c == nullptr)
        return;
    while (c->__c_ != __c)
    {
        c = c->__next_;
        if (c == nullptr)
            return;
    }
    __i_node* i = __insert_iterator(__i);
    c->__add(i);
    i->__c_ = c;
}

__c_node*
__libcpp_db::__insert_c(void* __c)
{
#ifndef _LIBCPP_HAS_NO_THREADS
    WLock _(mut());
#endif
    if (__csz_ + 1 > static_cast<size_t>(__cend_ - __cbeg_))
    {
        size_t nc = __next_prime(2*static_cast<size_t>(__cend_ - __cbeg_) + 1);
        __c_node** cbeg = static_cast<__c_node**>(calloc(nc, sizeof(void*)));
        if (cbeg == nullptr)
            __throw_bad_alloc();

        for (__c_node** p = __cbeg_; p != __cend_; ++p)
        {
            __c_node* q = *p;
            while (q != nullptr)
            {
                size_t h = hash<void*>()(q->__c_) % nc;
                __c_node* r = q->__next_;
                q->__next_ = cbeg[h];
                cbeg[h] = q;
                q = r;
            }
        }
        free(__cbeg_);
        __cbeg_ = cbeg;
        __cend_ = __cbeg_ + nc;
    }
    size_t hc = hash<void*>()(__c) % static_cast<size_t>(__cend_ - __cbeg_);
    __c_node* p = __cbeg_[hc];
    __c_node* r = __cbeg_[hc] =
      static_cast<__c_node*>(malloc(sizeof(__c_node)));
    if (__cbeg_[hc] == nullptr)
        __throw_bad_alloc();

    r->__c_ = __c;
    r->__next_ = p;
    ++__csz_;
    return r;
}

void
__libcpp_db::__erase_i(void* __i)
{
#ifndef _LIBCPP_HAS_NO_THREADS
    WLock _(mut());
#endif
    if (__ibeg_ != __iend_)
    {
        size_t hi = hash<void*>()(__i) % static_cast<size_t>(__iend_ - __ibeg_);
        __i_node* p = __ibeg_[hi];
        if (p != nullptr)
        {
            __i_node* q = nullptr;
            while (p->__i_ != __i)
            {
                q = p;
                p = p->__next_;
                if (p == nullptr)
                    return;
            }
            if (q == nullptr)
                __ibeg_[hi] = p->__next_;
            else
                q->__next_ = p->__next_;
            __c_node* c = p->__c_;
            --__isz_;
            if (c != nullptr)
                c->__remove(p);
            free(p);
        }
    }
}

void
__libcpp_db::__invalidate_all(void* __c)
{
#ifndef _LIBCPP_HAS_NO_THREADS
    WLock _(mut());
#endif
    if (__cend_ != __cbeg_)
    {
        size_t hc = hash<void*>()(__c) % static_cast<size_t>(__cend_ - __cbeg_);
        __c_node* p = __cbeg_[hc];
        if (p == nullptr)
            return;
        while (p->__c_ != __c)
        {
            p = p->__next_;
            if (p == nullptr)
                return;
        }
        while (p->end_ != p->beg_)
        {
            --p->end_;
            (*p->end_)->__c_ = nullptr;
        }
    }
}

__c_node*
__libcpp_db::__find_c_and_lock(void* __c) const
{
#ifndef _LIBCPP_HAS_NO_THREADS
    mut().lock();
#endif
    if (__cend_ == __cbeg_)
    {
#ifndef _LIBCPP_HAS_NO_THREADS
        mut().unlock();
#endif
        return nullptr;
    }
    size_t hc = hash<void*>()(__c) % static_cast<size_t>(__cend_ - __cbeg_);
    __c_node* p = __cbeg_[hc];
    if (p == nullptr)
    {
#ifndef _LIBCPP_HAS_NO_THREADS
        mut().unlock();
#endif
        return nullptr;
    }
    while (p->__c_ != __c)
    {
        p = p->__next_;
        if (p == nullptr)
        {
#ifndef _LIBCPP_HAS_NO_THREADS
            mut().unlock();
#endif
            return nullptr;
        }
    }
    return p;
}

__c_node*
__libcpp_db::__find_c(void* __c) const
{
    size_t hc = hash<void*>()(__c) % static_cast<size_t>(__cend_ - __cbeg_);
    __c_node* p = __cbeg_[hc];
    _LIBCPP_ASSERT(p != nullptr, "debug mode internal logic error __find_c A");
    while (p->__c_ != __c)
    {
        p = p->__next_;
        _LIBCPP_ASSERT(p != nullptr, "debug mode internal logic error __find_c B");
    }
    return p;
}

void
__libcpp_db::unlock() const
{
#ifndef _LIBCPP_HAS_NO_THREADS
    mut().unlock();
#endif
}

void
__libcpp_db::__erase_c(void* __c)
{
#ifndef _LIBCPP_HAS_NO_THREADS
    WLock _(mut());
#endif
    if (__cend_ != __cbeg_)
    {
        size_t hc = hash<void*>()(__c) % static_cast<size_t>(__cend_ - __cbeg_);
        __c_node* p = __cbeg_[hc];
        if (p == nullptr)
            return;
        __c_node* q = nullptr;
        _LIBCPP_ASSERT(p != nullptr, "debug mode internal logic error __erase_c A");
        while (p->__c_ != __c)
        {
            q = p;
            p = p->__next_;
            if (p == nullptr)
                return;
            _LIBCPP_ASSERT(p != nullptr, "debug mode internal logic error __erase_c B");
        }
        if (q == nullptr)
            __cbeg_[hc] = p->__next_;
        else
            q->__next_ = p->__next_;
        while (p->end_ != p->beg_)
        {
            --p->end_;
            (*p->end_)->__c_ = nullptr;
        }
        free(p->beg_);
        free(p);
        --__csz_;
    }
}

void
__libcpp_db::__iterator_copy(void* __i, const void* __i0)
{
#ifndef _LIBCPP_HAS_NO_THREADS
    WLock _(mut());
#endif
    __i_node* i = __find_iterator(__i);
    __i_node* i0 = __find_iterator(__i0);
    __c_node* c0 = i0 != nullptr ? i0->__c_ : nullptr;
    if (i == nullptr && i0 != nullptr)
        i = __insert_iterator(__i);
    __c_node* c = i != nullptr ? i->__c_ : nullptr;
    if (c != c0)
    {
        if (c != nullptr)
            c->__remove(i);
        if (i != nullptr)
        {
            i->__c_ = nullptr;
            if (c0 != nullptr)
            {
                i->__c_ = c0;
                i->__c_->__add(i);
            }
        }
    }
}

bool
__libcpp_db::__dereferenceable(const void* __i) const
{
#ifndef _LIBCPP_HAS_NO_THREADS
    RLock _(mut());
#endif
    __i_node* i = __find_iterator(__i);
    return i != nullptr && i->__c_ != nullptr && i->__c_->__dereferenceable(__i);
}

bool
__libcpp_db::__decrementable(const void* __i) const
{
#ifndef _LIBCPP_HAS_NO_THREADS
    RLock _(mut());
#endif
    __i_node* i = __find_iterator(__i);
    return i != nullptr && i->__c_ != nullptr && i->__c_->__decrementable(__i);
}

bool
__libcpp_db::__addable(const void* __i, ptrdiff_t __n) const
{
#ifndef _LIBCPP_HAS_NO_THREADS
    RLock _(mut());
#endif
    __i_node* i = __find_iterator(__i);
    return i != nullptr && i->__c_ != nullptr && i->__c_->__addable(__i, __n);
}

bool
__libcpp_db::__subscriptable(const void* __i, ptrdiff_t __n) const
{
#ifndef _LIBCPP_HAS_NO_THREADS
    RLock _(mut());
#endif
    __i_node* i = __find_iterator(__i);
    return i != nullptr && i->__c_ != nullptr && i->__c_->__subscriptable(__i, __n);
}

bool
__libcpp_db::__less_than_comparable(const void* __i, const void* __j) const
{
#ifndef _LIBCPP_HAS_NO_THREADS
    RLock _(mut());
#endif
    __i_node* i = __find_iterator(__i);
    __i_node* j = __find_iterator(__j);
    __c_node* ci = i != nullptr ? i->__c_ : nullptr;
    __c_node* cj = j != nullptr ? j->__c_ : nullptr;
    return ci != nullptr && ci == cj;
}

void
__libcpp_db::swap(void* c1, void* c2)
{
#ifndef _LIBCPP_HAS_NO_THREADS
    WLock _(mut());
#endif
    size_t hc = hash<void*>()(c1) % static_cast<size_t>(__cend_ - __cbeg_);
    __c_node* p1 = __cbeg_[hc];
    _LIBCPP_ASSERT(p1 != nullptr, "debug mode internal logic error swap A");
    while (p1->__c_ != c1)
    {
        p1 = p1->__next_;
        _LIBCPP_ASSERT(p1 != nullptr, "debug mode internal logic error swap B");
    }
    hc = hash<void*>()(c2) % static_cast<size_t>(__cend_ - __cbeg_);
    __c_node* p2 = __cbeg_[hc];
    _LIBCPP_ASSERT(p2 != nullptr, "debug mode internal logic error swap C");
    while (p2->__c_ != c2)
    {
        p2 = p2->__next_;
        _LIBCPP_ASSERT(p2 != nullptr, "debug mode internal logic error swap D");
    }
    std::swap(p1->beg_, p2->beg_);
    std::swap(p1->end_, p2->end_);
    std::swap(p1->cap_, p2->cap_);
    for (__i_node** p = p1->beg_; p != p1->end_; ++p)
        (*p)->__c_ = p1;
    for (__i_node** p = p2->beg_; p != p2->end_; ++p)
        (*p)->__c_ = p2;
}

void
__libcpp_db::__insert_i(void* __i)
{
#ifndef _LIBCPP_HAS_NO_THREADS
    WLock _(mut());
#endif
    __insert_iterator(__i);
}

void
__c_node::__add(__i_node* i)
{
    if (end_ == cap_)
    {
        size_t nc = 2*static_cast<size_t>(cap_ - beg_);
        if (nc == 0)
            nc = 1;
        __i_node** beg =
           static_cast<__i_node**>(malloc(nc * sizeof(__i_node*)));
        if (beg == nullptr)
            __throw_bad_alloc();

        if (nc > 1)
            memcpy(beg, beg_, nc/2*sizeof(__i_node*));
        free(beg_);
        beg_ = beg;
        end_ = beg_ + nc/2;
        cap_ = beg_ + nc;
    }
    *end_++ = i;
}

// private api

_LIBCPP_HIDDEN
__i_node*
__libcpp_db::__insert_iterator(void* __i)
{
    if (__isz_ + 1 > static_cast<size_t>(__iend_ - __ibeg_))
    {
        size_t nc = __next_prime(2*static_cast<size_t>(__iend_ - __ibeg_) + 1);
        __i_node** ibeg = static_cast<__i_node**>(calloc(nc, sizeof(void*)));
        if (ibeg == nullptr)
            __throw_bad_alloc();

        for (__i_node** p = __ibeg_; p != __iend_; ++p)
        {
            __i_node* q = *p;
            while (q != nullptr)
            {
                size_t h = hash<void*>()(q->__i_) % nc;
                __i_node* r = q->__next_;
                q->__next_ = ibeg[h];
                ibeg[h] = q;
                q = r;
            }
        }
        free(__ibeg_);
        __ibeg_ = ibeg;
        __iend_ = __ibeg_ + nc;
    }
    size_t hi = hash<void*>()(__i) % static_cast<size_t>(__iend_ - __ibeg_);
    __i_node* p = __ibeg_[hi];
    __i_node* r = __ibeg_[hi] =
      static_cast<__i_node*>(malloc(sizeof(__i_node)));
    if (r == nullptr)
        __throw_bad_alloc();

    ::new(r) __i_node(__i, p, nullptr);
    ++__isz_;
    return r;
}

_LIBCPP_HIDDEN
__i_node*
__libcpp_db::__find_iterator(const void* __i) const
{
    __i_node* r = nullptr;
    if (__ibeg_ != __iend_)
    {
        size_t h = hash<const void*>()(__i) % static_cast<size_t>(__iend_ - __ibeg_);
        for (__i_node* nd = __ibeg_[h]; nd != nullptr; nd = nd->__next_)
        {
            if (nd->__i_ == __i)
            {
                r = nd;
                break;
            }
        }
    }
    return r;
}

_LIBCPP_HIDDEN
void
__c_node::__remove(__i_node* p)
{
    __i_node** r = find(beg_, end_, p);
    _LIBCPP_ASSERT(r != end_, "debug mode internal logic error __c_node::__remove");
    if (--end_ != r)
        memmove(r, r+1, static_cast<size_t>(end_ - r)*sizeof(__i_node*));
}

_LIBCPP_END_NAMESPACE_STD
