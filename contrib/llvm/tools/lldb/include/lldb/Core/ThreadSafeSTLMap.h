//===-- ThreadSafeSTLMap.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadSafeSTLMap_h_
#define liblldb_ThreadSafeSTLMap_h_

#include <map>
#include <mutex>

#include "lldb/lldb-defines.h"

namespace lldb_private {

template <typename _Key, typename _Tp> class ThreadSafeSTLMap {
public:
  typedef std::map<_Key, _Tp> collection;
  typedef typename collection::iterator iterator;
  typedef typename collection::const_iterator const_iterator;
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  ThreadSafeSTLMap() : m_collection(), m_mutex() {}

  ~ThreadSafeSTLMap() {}

  bool IsEmpty() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_collection.empty();
  }

  void Clear() {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_collection.clear();
  }

  size_t Erase(const _Key &key) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return EraseNoLock(key);
  }

  size_t EraseNoLock(const _Key &key) { return m_collection.erase(key); }

  bool GetValueForKey(const _Key &key, _Tp &value) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return GetValueForKeyNoLock(key, value);
  }

  // Call this if you have already manually locked the mutex using the
  // GetMutex() accessor
  bool GetValueForKeyNoLock(const _Key &key, _Tp &value) const {
    const_iterator pos = m_collection.find(key);
    if (pos != m_collection.end()) {
      value = pos->second;
      return true;
    }
    return false;
  }

  bool GetFirstKeyForValue(const _Tp &value, _Key &key) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return GetFirstKeyForValueNoLock(value, key);
  }

  bool GetFirstKeyForValueNoLock(const _Tp &value, _Key &key) const {
    const_iterator pos, end = m_collection.end();
    for (pos = m_collection.begin(); pos != end; ++pos) {
      if (pos->second == value) {
        key = pos->first;
        return true;
      }
    }
    return false;
  }

  bool LowerBound(const _Key &key, _Key &match_key, _Tp &match_value,
                  bool decrement_if_not_equal) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return LowerBoundNoLock(key, match_key, match_value,
                            decrement_if_not_equal);
  }

  bool LowerBoundNoLock(const _Key &key, _Key &match_key, _Tp &match_value,
                        bool decrement_if_not_equal) const {
    const_iterator pos = m_collection.lower_bound(key);
    if (pos != m_collection.end()) {
      match_key = pos->first;
      if (decrement_if_not_equal && key != match_key &&
          pos != m_collection.begin()) {
        --pos;
        match_key = pos->first;
      }
      match_value = pos->second;
      return true;
    }
    return false;
  }

  iterator lower_bound_unsafe(const _Key &key) {
    return m_collection.lower_bound(key);
  }

  void SetValueForKey(const _Key &key, const _Tp &value) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    SetValueForKeyNoLock(key, value);
  }

  // Call this if you have already manually locked the mutex using the
  // GetMutex() accessor
  void SetValueForKeyNoLock(const _Key &key, const _Tp &value) {
    m_collection[key] = value;
  }

  std::recursive_mutex &GetMutex() { return m_mutex; }

private:
  collection m_collection;
  mutable std::recursive_mutex m_mutex;

  //------------------------------------------------------------------
  // For ThreadSafeSTLMap only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(ThreadSafeSTLMap);
};

} // namespace lldb_private

#endif // liblldb_ThreadSafeSTLMap_h_
