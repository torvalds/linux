//==- llvm/ADT/IntrusiveRefCntPtr.h - Smart Refcounting Pointer --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the RefCountedBase, ThreadSafeRefCountedBase, and
// IntrusiveRefCntPtr classes.
//
// IntrusiveRefCntPtr is a smart pointer to an object which maintains a
// reference count.  (ThreadSafe)RefCountedBase is a mixin class that adds a
// refcount member variable and methods for updating the refcount.  An object
// that inherits from (ThreadSafe)RefCountedBase deletes itself when its
// refcount hits zero.
//
// For example:
//
//   class MyClass : public RefCountedBase<MyClass> {};
//
//   void foo() {
//     // Constructing an IntrusiveRefCntPtr increases the pointee's refcount by
//     // 1 (from 0 in this case).
//     IntrusiveRefCntPtr<MyClass> Ptr1(new MyClass());
//
//     // Copying an IntrusiveRefCntPtr increases the pointee's refcount by 1.
//     IntrusiveRefCntPtr<MyClass> Ptr2(Ptr1);
//
//     // Constructing an IntrusiveRefCntPtr has no effect on the object's
//     // refcount.  After a move, the moved-from pointer is null.
//     IntrusiveRefCntPtr<MyClass> Ptr3(std::move(Ptr1));
//     assert(Ptr1 == nullptr);
//
//     // Clearing an IntrusiveRefCntPtr decreases the pointee's refcount by 1.
//     Ptr2.reset();
//
//     // The object deletes itself when we return from the function, because
//     // Ptr3's destructor decrements its refcount to 0.
//   }
//
// You can use IntrusiveRefCntPtr with isa<T>(), dyn_cast<T>(), etc.:
//
//   IntrusiveRefCntPtr<MyClass> Ptr(new MyClass());
//   OtherClass *Other = dyn_cast<OtherClass>(Ptr);  // Ptr.get() not required
//
// IntrusiveRefCntPtr works with any class that
//
//  - inherits from (ThreadSafe)RefCountedBase,
//  - has Retain() and Release() methods, or
//  - specializes IntrusiveRefCntPtrInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_INTRUSIVEREFCNTPTR_H
#define LLVM_ADT_INTRUSIVEREFCNTPTR_H

#include <atomic>
#include <cassert>
#include <cstddef>

namespace llvm {

/// A CRTP mixin class that adds reference counting to a type.
///
/// The lifetime of an object which inherits from RefCountedBase is managed by
/// calls to Release() and Retain(), which increment and decrement the object's
/// refcount, respectively.  When a Release() call decrements the refcount to 0,
/// the object deletes itself.
template <class Derived> class RefCountedBase {
  mutable unsigned RefCount = 0;

public:
  RefCountedBase() = default;
  RefCountedBase(const RefCountedBase &) {}

  void Retain() const { ++RefCount; }

  void Release() const {
    assert(RefCount > 0 && "Reference count is already zero.");
    if (--RefCount == 0)
      delete static_cast<const Derived *>(this);
  }
};

/// A thread-safe version of \c RefCountedBase.
template <class Derived> class ThreadSafeRefCountedBase {
  mutable std::atomic<int> RefCount;

protected:
  ThreadSafeRefCountedBase() : RefCount(0) {}

public:
  void Retain() const { RefCount.fetch_add(1, std::memory_order_relaxed); }

  void Release() const {
    int NewRefCount = RefCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
    assert(NewRefCount >= 0 && "Reference count was already zero.");
    if (NewRefCount == 0)
      delete static_cast<const Derived *>(this);
  }
};

/// Class you can specialize to provide custom retain/release functionality for
/// a type.
///
/// Usually specializing this class is not necessary, as IntrusiveRefCntPtr
/// works with any type which defines Retain() and Release() functions -- you
/// can define those functions yourself if RefCountedBase doesn't work for you.
///
/// One case when you might want to specialize this type is if you have
///  - Foo.h defines type Foo and includes Bar.h, and
///  - Bar.h uses IntrusiveRefCntPtr<Foo> in inline functions.
///
/// Because Foo.h includes Bar.h, Bar.h can't include Foo.h in order to pull in
/// the declaration of Foo.  Without the declaration of Foo, normally Bar.h
/// wouldn't be able to use IntrusiveRefCntPtr<Foo>, which wants to call
/// T::Retain and T::Release.
///
/// To resolve this, Bar.h could include a third header, FooFwd.h, which
/// forward-declares Foo and specializes IntrusiveRefCntPtrInfo<Foo>.  Then
/// Bar.h could use IntrusiveRefCntPtr<Foo>, although it still couldn't call any
/// functions on Foo itself, because Foo would be an incomplete type.
template <typename T> struct IntrusiveRefCntPtrInfo {
  static void retain(T *obj) { obj->Retain(); }
  static void release(T *obj) { obj->Release(); }
};

/// A smart pointer to a reference-counted object that inherits from
/// RefCountedBase or ThreadSafeRefCountedBase.
///
/// This class increments its pointee's reference count when it is created, and
/// decrements its refcount when it's destroyed (or is changed to point to a
/// different object).
template <typename T> class IntrusiveRefCntPtr {
  T *Obj = nullptr;

public:
  using element_type = T;

  explicit IntrusiveRefCntPtr() = default;
  IntrusiveRefCntPtr(T *obj) : Obj(obj) { retain(); }
  IntrusiveRefCntPtr(const IntrusiveRefCntPtr &S) : Obj(S.Obj) { retain(); }
  IntrusiveRefCntPtr(IntrusiveRefCntPtr &&S) : Obj(S.Obj) { S.Obj = nullptr; }

  template <class X>
  IntrusiveRefCntPtr(IntrusiveRefCntPtr<X> &&S) : Obj(S.get()) {
    S.Obj = nullptr;
  }

  template <class X>
  IntrusiveRefCntPtr(const IntrusiveRefCntPtr<X> &S) : Obj(S.get()) {
    retain();
  }

  ~IntrusiveRefCntPtr() { release(); }

  IntrusiveRefCntPtr &operator=(IntrusiveRefCntPtr S) {
    swap(S);
    return *this;
  }

  T &operator*() const { return *Obj; }
  T *operator->() const { return Obj; }
  T *get() const { return Obj; }
  explicit operator bool() const { return Obj; }

  void swap(IntrusiveRefCntPtr &other) {
    T *tmp = other.Obj;
    other.Obj = Obj;
    Obj = tmp;
  }

  void reset() {
    release();
    Obj = nullptr;
  }

  void resetWithoutRelease() { Obj = nullptr; }

private:
  void retain() {
    if (Obj)
      IntrusiveRefCntPtrInfo<T>::retain(Obj);
  }

  void release() {
    if (Obj)
      IntrusiveRefCntPtrInfo<T>::release(Obj);
  }

  template <typename X> friend class IntrusiveRefCntPtr;
};

template <class T, class U>
inline bool operator==(const IntrusiveRefCntPtr<T> &A,
                       const IntrusiveRefCntPtr<U> &B) {
  return A.get() == B.get();
}

template <class T, class U>
inline bool operator!=(const IntrusiveRefCntPtr<T> &A,
                       const IntrusiveRefCntPtr<U> &B) {
  return A.get() != B.get();
}

template <class T, class U>
inline bool operator==(const IntrusiveRefCntPtr<T> &A, U *B) {
  return A.get() == B;
}

template <class T, class U>
inline bool operator!=(const IntrusiveRefCntPtr<T> &A, U *B) {
  return A.get() != B;
}

template <class T, class U>
inline bool operator==(T *A, const IntrusiveRefCntPtr<U> &B) {
  return A == B.get();
}

template <class T, class U>
inline bool operator!=(T *A, const IntrusiveRefCntPtr<U> &B) {
  return A != B.get();
}

template <class T>
bool operator==(std::nullptr_t A, const IntrusiveRefCntPtr<T> &B) {
  return !B;
}

template <class T>
bool operator==(const IntrusiveRefCntPtr<T> &A, std::nullptr_t B) {
  return B == A;
}

template <class T>
bool operator!=(std::nullptr_t A, const IntrusiveRefCntPtr<T> &B) {
  return !(A == B);
}

template <class T>
bool operator!=(const IntrusiveRefCntPtr<T> &A, std::nullptr_t B) {
  return !(A == B);
}

// Make IntrusiveRefCntPtr work with dyn_cast, isa, and the other idioms from
// Casting.h.
template <typename From> struct simplify_type;

template <class T> struct simplify_type<IntrusiveRefCntPtr<T>> {
  using SimpleType = T *;

  static SimpleType getSimplifiedValue(IntrusiveRefCntPtr<T> &Val) {
    return Val.get();
  }
};

template <class T> struct simplify_type<const IntrusiveRefCntPtr<T>> {
  using SimpleType = /*const*/ T *;

  static SimpleType getSimplifiedValue(const IntrusiveRefCntPtr<T> &Val) {
    return Val.get();
  }
};

} // end namespace llvm

#endif // LLVM_ADT_INTRUSIVEREFCNTPTR_H
