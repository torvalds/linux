//===-- ThreadSafeSTLVector.h ------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadSafeSTLVector_h_
#define liblldb_ThreadSafeSTLVector_h_

#include <mutex>
#include <vector>

#include "lldb/lldb-defines.h"

namespace lldb_private {

template <typename _Object> class ThreadSafeSTLVector {
public:
  typedef std::vector<_Object> collection;
  typedef typename collection::iterator iterator;
  typedef typename collection::const_iterator const_iterator;
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  ThreadSafeSTLVector() : m_collection(), m_mutex() {}

  ~ThreadSafeSTLVector() = default;

  bool IsEmpty() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_collection.empty();
  }

  void Clear() {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_collection.clear();
  }

  size_t GetCount() {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_collection.size();
  }

  void AppendObject(_Object &object) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_collection.push_back(object);
  }

  _Object GetObject(size_t index) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_collection.at(index);
  }

  void SetObject(size_t index, const _Object &object) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_collection.at(index) = object;
  }

  std::recursive_mutex &GetMutex() { return m_mutex; }

private:
  collection m_collection;
  mutable std::recursive_mutex m_mutex;

  //------------------------------------------------------------------
  // For ThreadSafeSTLVector only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(ThreadSafeSTLVector);
};

} // namespace lldb_private

#endif // liblldb_ThreadSafeSTLVector_h_
