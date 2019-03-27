//===-- CFUtils.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 3/5/07.
//
//===----------------------------------------------------------------------===//

#ifndef __CFUtils_h__
#define __CFUtils_h__

#include <CoreFoundation/CoreFoundation.h>

#ifdef __cplusplus

//----------------------------------------------------------------------
// Templatized CF helper class that can own any CF pointer and will
// call CFRelease() on any valid pointer it owns unless that pointer is
// explicitly released using the release() member function.
//----------------------------------------------------------------------
template <class T> class CFReleaser {
public:
  // Type names for the avlue
  typedef T element_type;

  // Constructors and destructors
  CFReleaser(T ptr = NULL) : _ptr(ptr) {}
  CFReleaser(const CFReleaser &copy) : _ptr(copy.get()) {
    if (get())
      ::CFRetain(get());
  }
  virtual ~CFReleaser() { reset(); }

  // Assignments
  CFReleaser &operator=(const CFReleaser<T> &copy) {
    if (copy != *this) {
      // Replace our owned pointer with the new one
      reset(copy.get());
      // Retain the current pointer that we own
      if (get())
        ::CFRetain(get());
    }
  }
  // Get the address of the contained type
  T *ptr_address() { return &_ptr; }

  // Access the pointer itself
  const T get() const { return _ptr; }
  T get() { return _ptr; }

  // Set a new value for the pointer and CFRelease our old
  // value if we had a valid one.
  void reset(T ptr = NULL) {
    if (ptr != _ptr) {
      if (_ptr != NULL)
        ::CFRelease(_ptr);
      _ptr = ptr;
    }
  }

  // Release ownership without calling CFRelease
  T release() {
    T tmp = _ptr;
    _ptr = NULL;
    return tmp;
  }

private:
  element_type _ptr;
};

#endif // #ifdef __cplusplus
#endif // #ifndef __CFUtils_h__
