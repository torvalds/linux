//===-- ThreadSafeValue.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadSafeValue_h_
#define liblldb_ThreadSafeValue_h_


#include <mutex>

#include "lldb/lldb-defines.h"

namespace lldb_private {

template <class T> class ThreadSafeValue {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  ThreadSafeValue() : m_value(), m_mutex() {}

  ThreadSafeValue(const T &value) : m_value(value), m_mutex() {}

  ~ThreadSafeValue() {}

  T GetValue() const {
    T value;
    {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      value = m_value;
    }
    return value;
  }

  // Call this if you have already manually locked the mutex using the
  // GetMutex() accessor
  const T &GetValueNoLock() const { return m_value; }

  void SetValue(const T &value) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_value = value;
  }

  // Call this if you have already manually locked the mutex using the
  // GetMutex() accessor
  void SetValueNoLock(const T &value) { m_value = value; }

  std::recursive_mutex &GetMutex() { return m_mutex; }

private:
  T m_value;
  mutable std::recursive_mutex m_mutex;

  //------------------------------------------------------------------
  // For ThreadSafeValue only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(ThreadSafeValue);
};

} // namespace lldb_private
#endif // liblldb_ThreadSafeValue_h_
