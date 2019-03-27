//===---------------------SharingPtr.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_SharingPtr_h_
#define utility_SharingPtr_h_

#include <memory>

// Microsoft Visual C++ currently does not enable std::atomic to work in CLR
// mode - as such we need to "hack around it" for MSVC++ builds only using
// Windows specific intrinsics instead of the C++11 atomic support
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <atomic>
#endif

#include <stddef.h>


//#define ENABLE_SP_LOGGING 1 // DON'T CHECK THIS LINE IN UNLESS COMMENTED OUT
#if defined(ENABLE_SP_LOGGING)

extern "C" void track_sp(void *sp_this, void *ptr, long count);

#endif

namespace lldb_private {

namespace imp {

class shared_count {
  shared_count(const shared_count &);
  shared_count &operator=(const shared_count &);

public:
  explicit shared_count(long refs = 0) : shared_owners_(refs) {}

  void add_shared();
  void release_shared();
  long use_count() const { return shared_owners_ + 1; }

protected:
#ifdef _MSC_VER
  long shared_owners_;
#else
  std::atomic<long> shared_owners_;
#endif
  virtual ~shared_count();

private:
  virtual void on_zero_shared() = 0;
};

template <class T> class shared_ptr_pointer : public shared_count {
  T data_;

public:
  shared_ptr_pointer(T p) : data_(p) {}

private:
  void on_zero_shared() override;

  // Outlaw copy constructor and assignment operator to keep effective C++
  // warnings down to a minimum
  shared_ptr_pointer(const shared_ptr_pointer &);
  shared_ptr_pointer &operator=(const shared_ptr_pointer &);
};

template <class T> void shared_ptr_pointer<T>::on_zero_shared() {
  delete data_;
}

template <class T> class shared_ptr_emplace : public shared_count {
  T data_;

public:
  shared_ptr_emplace() : data_() {}

  template <class A0> shared_ptr_emplace(A0 &a0) : data_(a0) {}

  template <class A0, class A1>
  shared_ptr_emplace(A0 &a0, A1 &a1) : data_(a0, a1) {}

  template <class A0, class A1, class A2>
  shared_ptr_emplace(A0 &a0, A1 &a1, A2 &a2) : data_(a0, a1, a2) {}

  template <class A0, class A1, class A2, class A3>
  shared_ptr_emplace(A0 &a0, A1 &a1, A2 &a2, A3 &a3) : data_(a0, a1, a2, a3) {}

  template <class A0, class A1, class A2, class A3, class A4>
  shared_ptr_emplace(A0 &a0, A1 &a1, A2 &a2, A3 &a3, A4 &a4)
      : data_(a0, a1, a2, a3, a4) {}

private:
  void on_zero_shared() override;

public:
  T *get() { return &data_; }
};

template <class T> void shared_ptr_emplace<T>::on_zero_shared() {}

} // namespace imp

template <class T> class SharingPtr {
public:
  typedef T element_type;

private:
  element_type *ptr_;
  imp::shared_count *cntrl_;

  struct nat {
    int for_bool_;
  };

public:
  SharingPtr();
  SharingPtr(std::nullptr_t);
  template <class Y> explicit SharingPtr(Y *p);
  template <class Y> explicit SharingPtr(Y *p, imp::shared_count *ctrl_block);
  template <class Y> SharingPtr(const SharingPtr<Y> &r, element_type *p);
  SharingPtr(const SharingPtr &r);
  template <class Y> SharingPtr(const SharingPtr<Y> &r);

  ~SharingPtr();

  SharingPtr &operator=(const SharingPtr &r);
  template <class Y> SharingPtr &operator=(const SharingPtr<Y> &r);

  void swap(SharingPtr &r);
  void reset();
  template <class Y> void reset(Y *p);
  void reset(std::nullptr_t);

  element_type *get() const { return ptr_; }
  element_type &operator*() const { return *ptr_; }
  element_type *operator->() const { return ptr_; }
  long use_count() const { return cntrl_ ? cntrl_->use_count() : 0; }
  bool unique() const { return use_count() == 1; }
  bool empty() const { return cntrl_ == nullptr; }
  operator nat *() const { return (nat *)get(); }

  static SharingPtr<T> make_shared();

  template <class A0> static SharingPtr<T> make_shared(A0 &);

  template <class A0, class A1> static SharingPtr<T> make_shared(A0 &, A1 &);

  template <class A0, class A1, class A2>
  static SharingPtr<T> make_shared(A0 &, A1 &, A2 &);

  template <class A0, class A1, class A2, class A3>
  static SharingPtr<T> make_shared(A0 &, A1 &, A2 &, A3 &);

  template <class A0, class A1, class A2, class A3, class A4>
  static SharingPtr<T> make_shared(A0 &, A1 &, A2 &, A3 &, A4 &);

private:
  template <class U> friend class SharingPtr;
};

template <class T>
inline SharingPtr<T>::SharingPtr() : ptr_(nullptr), cntrl_(nullptr) {}

template <class T>
inline SharingPtr<T>::SharingPtr(std::nullptr_t)
    : ptr_(nullptr), cntrl_(nullptr) {}

template <class T>
template <class Y>
SharingPtr<T>::SharingPtr(Y *p) : ptr_(p), cntrl_(nullptr) {
  std::unique_ptr<Y> hold(p);
  typedef imp::shared_ptr_pointer<Y *> _CntrlBlk;
  cntrl_ = new _CntrlBlk(p);
  hold.release();
}

template <class T>
template <class Y>
SharingPtr<T>::SharingPtr(Y *p, imp::shared_count *cntrl_block)
    : ptr_(p), cntrl_(cntrl_block) {}

template <class T>
template <class Y>
inline SharingPtr<T>::SharingPtr(const SharingPtr<Y> &r, element_type *p)
    : ptr_(p), cntrl_(r.cntrl_) {
  if (cntrl_)
    cntrl_->add_shared();
}

template <class T>
inline SharingPtr<T>::SharingPtr(const SharingPtr &r)
    : ptr_(r.ptr_), cntrl_(r.cntrl_) {
  if (cntrl_)
    cntrl_->add_shared();
}

template <class T>
template <class Y>
inline SharingPtr<T>::SharingPtr(const SharingPtr<Y> &r)
    : ptr_(r.ptr_), cntrl_(r.cntrl_) {
  if (cntrl_)
    cntrl_->add_shared();
}

template <class T> SharingPtr<T>::~SharingPtr() {
  if (cntrl_)
    cntrl_->release_shared();
}

template <class T>
inline SharingPtr<T> &SharingPtr<T>::operator=(const SharingPtr &r) {
  SharingPtr(r).swap(*this);
  return *this;
}

template <class T>
template <class Y>
inline SharingPtr<T> &SharingPtr<T>::operator=(const SharingPtr<Y> &r) {
  SharingPtr(r).swap(*this);
  return *this;
}

template <class T> inline void SharingPtr<T>::swap(SharingPtr &r) {
  std::swap(ptr_, r.ptr_);
  std::swap(cntrl_, r.cntrl_);
}

template <class T> inline void SharingPtr<T>::reset() {
  SharingPtr().swap(*this);
}

template <class T> inline void SharingPtr<T>::reset(std::nullptr_t p) {
  reset();
}

template <class T> template <class Y> inline void SharingPtr<T>::reset(Y *p) {
  SharingPtr(p).swap(*this);
}

template <class T> SharingPtr<T> SharingPtr<T>::make_shared() {
  typedef imp::shared_ptr_emplace<T> CntrlBlk;
  SharingPtr<T> r;
  r.cntrl_ = new CntrlBlk();
  r.ptr_ = static_cast<CntrlBlk *>(r.cntrl_)->get();
  return r;
}

template <class T>
template <class A0>
SharingPtr<T> SharingPtr<T>::make_shared(A0 &a0) {
  typedef imp::shared_ptr_emplace<T> CntrlBlk;
  SharingPtr<T> r;
  r.cntrl_ = new CntrlBlk(a0);
  r.ptr_ = static_cast<CntrlBlk *>(r.cntrl_)->get();
  return r;
}

template <class T>
template <class A0, class A1>
SharingPtr<T> SharingPtr<T>::make_shared(A0 &a0, A1 &a1) {
  typedef imp::shared_ptr_emplace<T> CntrlBlk;
  SharingPtr<T> r;
  r.cntrl_ = new CntrlBlk(a0, a1);
  r.ptr_ = static_cast<CntrlBlk *>(r.cntrl_)->get();
  return r;
}

template <class T>
template <class A0, class A1, class A2>
SharingPtr<T> SharingPtr<T>::make_shared(A0 &a0, A1 &a1, A2 &a2) {
  typedef imp::shared_ptr_emplace<T> CntrlBlk;
  SharingPtr<T> r;
  r.cntrl_ = new CntrlBlk(a0, a1, a2);
  r.ptr_ = static_cast<CntrlBlk *>(r.cntrl_)->get();
  return r;
}

template <class T>
template <class A0, class A1, class A2, class A3>
SharingPtr<T> SharingPtr<T>::make_shared(A0 &a0, A1 &a1, A2 &a2, A3 &a3) {
  typedef imp::shared_ptr_emplace<T> CntrlBlk;
  SharingPtr<T> r;
  r.cntrl_ = new CntrlBlk(a0, a1, a2, a3);
  r.ptr_ = static_cast<CntrlBlk *>(r.cntrl_)->get();
  return r;
}

template <class T>
template <class A0, class A1, class A2, class A3, class A4>
SharingPtr<T> SharingPtr<T>::make_shared(A0 &a0, A1 &a1, A2 &a2, A3 &a3,
                                         A4 &a4) {
  typedef imp::shared_ptr_emplace<T> CntrlBlk;
  SharingPtr<T> r;
  r.cntrl_ = new CntrlBlk(a0, a1, a2, a3, a4);
  r.ptr_ = static_cast<CntrlBlk *>(r.cntrl_)->get();
  return r;
}

template <class T> inline SharingPtr<T> make_shared() {
  return SharingPtr<T>::make_shared();
}

template <class T, class A0> inline SharingPtr<T> make_shared(A0 &a0) {
  return SharingPtr<T>::make_shared(a0);
}

template <class T, class A0, class A1>
inline SharingPtr<T> make_shared(A0 &a0, A1 &a1) {
  return SharingPtr<T>::make_shared(a0, a1);
}

template <class T, class A0, class A1, class A2>
inline SharingPtr<T> make_shared(A0 &a0, A1 &a1, A2 &a2) {
  return SharingPtr<T>::make_shared(a0, a1, a2);
}

template <class T, class A0, class A1, class A2, class A3>
inline SharingPtr<T> make_shared(A0 &a0, A1 &a1, A2 &a2, A3 &a3) {
  return SharingPtr<T>::make_shared(a0, a1, a2, a3);
}

template <class T, class A0, class A1, class A2, class A3, class A4>
inline SharingPtr<T> make_shared(A0 &a0, A1 &a1, A2 &a2, A3 &a3, A4 &a4) {
  return SharingPtr<T>::make_shared(a0, a1, a2, a3, a4);
}

template <class T, class U>
inline bool operator==(const SharingPtr<T> &__x, const SharingPtr<U> &__y) {
  return __x.get() == __y.get();
}

template <class T, class U>
inline bool operator!=(const SharingPtr<T> &__x, const SharingPtr<U> &__y) {
  return !(__x == __y);
}

template <class T, class U>
inline bool operator<(const SharingPtr<T> &__x, const SharingPtr<U> &__y) {
  return __x.get() < __y.get();
}

template <class T> inline void swap(SharingPtr<T> &__x, SharingPtr<T> &__y) {
  __x.swap(__y);
}

template <class T, class U>
inline SharingPtr<T> static_pointer_cast(const SharingPtr<U> &r) {
  return SharingPtr<T>(r, static_cast<T *>(r.get()));
}

template <class T, class U>
SharingPtr<T> const_pointer_cast(const SharingPtr<U> &r) {
  return SharingPtr<T>(r, const_cast<T *>(r.get()));
}

template <class T> class LoggingSharingPtr : public SharingPtr<T> {
  typedef SharingPtr<T> base;

public:
  typedef void (*Callback)(void *, const LoggingSharingPtr &, bool action);
  // action:  false means increment just happened
  //          true  means decrement is about to happen

  LoggingSharingPtr() : cb_(0), baton_(nullptr) {}

  LoggingSharingPtr(Callback cb, void *baton) : cb_(cb), baton_(baton) {
    if (cb_)
      cb_(baton_, *this, false);
  }

  template <class Y>
  LoggingSharingPtr(Y *p) : base(p), cb_(0), baton_(nullptr) {}

  template <class Y>
  LoggingSharingPtr(Y *p, Callback cb, void *baton)
      : base(p), cb_(cb), baton_(baton) {
    if (cb_)
      cb_(baton_, *this, false);
  }

  ~LoggingSharingPtr() {
    if (cb_)
      cb_(baton_, *this, true);
  }

  LoggingSharingPtr(const LoggingSharingPtr &p)
      : base(p), cb_(p.cb_), baton_(p.baton_) {
    if (cb_)
      cb_(baton_, *this, false);
  }

  LoggingSharingPtr &operator=(const LoggingSharingPtr &p) {
    if (cb_)
      cb_(baton_, *this, true);
    base::operator=(p);
    cb_ = p.cb_;
    baton_ = p.baton_;
    if (cb_)
      cb_(baton_, *this, false);
    return *this;
  }

  void reset() {
    if (cb_)
      cb_(baton_, *this, true);
    base::reset();
  }

  template <class Y> void reset(Y *p) {
    if (cb_)
      cb_(baton_, *this, true);
    base::reset(p);
    if (cb_)
      cb_(baton_, *this, false);
  }

  void SetCallback(Callback cb, void *baton) {
    cb_ = cb;
    baton_ = baton;
  }

  void ClearCallback() {
    cb_ = 0;
    baton_ = 0;
  }

private:
  Callback cb_;
  void *baton_;
};

template <class T> class IntrusiveSharingPtr;

template <class T> class ReferenceCountedBase {
public:
  explicit ReferenceCountedBase() : shared_owners_(-1) {}

  void add_shared();

  void release_shared();

  long use_count() const { return shared_owners_ + 1; }

protected:
  long shared_owners_;

  friend class IntrusiveSharingPtr<T>;

private:
  ReferenceCountedBase(const ReferenceCountedBase &);
  ReferenceCountedBase &operator=(const ReferenceCountedBase &);
};

template <class T> void lldb_private::ReferenceCountedBase<T>::add_shared() {
#ifdef _MSC_VER
  _InterlockedIncrement(&shared_owners_);
#else
  ++shared_owners_;
#endif
}

template <class T>
void lldb_private::ReferenceCountedBase<T>::release_shared() {
#ifdef _MSC_VER
  if (_InterlockedDecrement(&shared_owners_) == -1)
#else
  if (--shared_owners_ == -1)
#endif
    delete static_cast<T *>(this);
}

template <class T>
class ReferenceCountedBaseVirtual : public imp::shared_count {
public:
  explicit ReferenceCountedBaseVirtual() : imp::shared_count(-1) {}

  ~ReferenceCountedBaseVirtual() override = default;

  void on_zero_shared() override;
};

template <class T> void ReferenceCountedBaseVirtual<T>::on_zero_shared() {}

template <typename T> class IntrusiveSharingPtr {
public:
  typedef T element_type;

  explicit IntrusiveSharingPtr() : ptr_(0) {}

  explicit IntrusiveSharingPtr(T *ptr) : ptr_(ptr) { add_shared(); }

  IntrusiveSharingPtr(const IntrusiveSharingPtr &rhs) : ptr_(rhs.ptr_) {
    add_shared();
  }

  template <class X>
  IntrusiveSharingPtr(const IntrusiveSharingPtr<X> &rhs) : ptr_(rhs.get()) {
    add_shared();
  }

  IntrusiveSharingPtr &operator=(const IntrusiveSharingPtr &rhs) {
    reset(rhs.get());
    return *this;
  }

  template <class X>
  IntrusiveSharingPtr &operator=(const IntrusiveSharingPtr<X> &rhs) {
    reset(rhs.get());
    return *this;
  }

  IntrusiveSharingPtr &operator=(T *ptr) {
    reset(ptr);
    return *this;
  }

  ~IntrusiveSharingPtr() {
    release_shared();
    ptr_ = nullptr;
  }

  T &operator*() const { return *ptr_; }

  T *operator->() const { return ptr_; }

  T *get() const { return ptr_; }

  explicit operator bool() const { return ptr_ != 0; }

  void swap(IntrusiveSharingPtr &rhs) {
    std::swap(ptr_, rhs.ptr_);
#if defined(ENABLE_SP_LOGGING)
    track_sp(this, ptr_, use_count());
    track_sp(&rhs, rhs.ptr_, rhs.use_count());
#endif
  }

  void reset(T *ptr = nullptr) { IntrusiveSharingPtr(ptr).swap(*this); }

  long use_count() const {
    if (ptr_)
      return ptr_->use_count();
    return 0;
  }

  bool unique() const { return use_count() == 1; }

private:
  element_type *ptr_;

  void add_shared() {
    if (ptr_) {
      ptr_->add_shared();
#if defined(ENABLE_SP_LOGGING)
      track_sp(this, ptr_, ptr_->use_count());
#endif
    }
  }
  void release_shared() {
    if (ptr_) {
#if defined(ENABLE_SP_LOGGING)
      track_sp(this, nullptr, ptr_->use_count() - 1);
#endif
      ptr_->release_shared();
    }
  }
};

template <class T, class U>
inline bool operator==(const IntrusiveSharingPtr<T> &lhs,
                       const IntrusiveSharingPtr<U> &rhs) {
  return lhs.get() == rhs.get();
}

template <class T, class U>
inline bool operator!=(const IntrusiveSharingPtr<T> &lhs,
                       const IntrusiveSharingPtr<U> &rhs) {
  return lhs.get() != rhs.get();
}

template <class T, class U>
inline bool operator==(const IntrusiveSharingPtr<T> &lhs, U *rhs) {
  return lhs.get() == rhs;
}

template <class T, class U>
inline bool operator!=(const IntrusiveSharingPtr<T> &lhs, U *rhs) {
  return lhs.get() != rhs;
}

template <class T, class U>
inline bool operator==(T *lhs, const IntrusiveSharingPtr<U> &rhs) {
  return lhs == rhs.get();
}

template <class T, class U>
inline bool operator!=(T *lhs, const IntrusiveSharingPtr<U> &rhs) {
  return lhs != rhs.get();
}

} // namespace lldb_private

#endif // utility_SharingPtr_h_
