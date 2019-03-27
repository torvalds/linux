//===------------------SharedCluster.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_SharedCluster_h_
#define utility_SharedCluster_h_

#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/SharingPtr.h"

#include "llvm/ADT/SmallPtrSet.h"

#include <mutex>

namespace lldb_private {

namespace imp {
template <typename T>
class shared_ptr_refcount : public lldb_private::imp::shared_count {
public:
  template <class Y>
  shared_ptr_refcount(Y *in) : shared_count(0), manager(in) {}

  shared_ptr_refcount() : shared_count(0) {}

  ~shared_ptr_refcount() override {}

  void on_zero_shared() override { manager->DecrementRefCount(); }

private:
  T *manager;
};

} // namespace imp

template <class T> class ClusterManager {
public:
  ClusterManager() : m_objects(), m_external_ref(0), m_mutex() {}

  ~ClusterManager() {
    for (typename llvm::SmallPtrSet<T *, 16>::iterator pos = m_objects.begin(),
                                                       end = m_objects.end();
         pos != end; ++pos) {
      T *object = *pos;
      delete object;
    }

    // Decrement refcount should have been called on this ClusterManager, and
    // it should have locked the mutex, now we will unlock it before we destroy
    // it...
    m_mutex.unlock();
  }

  void ManageObject(T *new_object) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_objects.insert(new_object);
  }

  typename lldb_private::SharingPtr<T> GetSharedPointer(T *desired_object) {
    {
      std::lock_guard<std::mutex> guard(m_mutex);
      m_external_ref++;
      if (0 == m_objects.count(desired_object)) {
        lldbassert(false && "object not found in shared cluster when expected");
        desired_object = nullptr;
      }
    }
    return typename lldb_private::SharingPtr<T>(
        desired_object, new imp::shared_ptr_refcount<ClusterManager>(this));
  }

private:
  void DecrementRefCount() {
    m_mutex.lock();
    m_external_ref--;
    if (m_external_ref == 0)
      delete this;
    else
      m_mutex.unlock();
  }

  friend class imp::shared_ptr_refcount<ClusterManager>;

  llvm::SmallPtrSet<T *, 16> m_objects;
  int m_external_ref;
  std::mutex m_mutex;
};

} // namespace lldb_private

#endif // utility_SharedCluster_h_
