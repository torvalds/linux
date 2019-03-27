//===---------------------SharingPtr.cpp ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/SharingPtr.h"

#if defined(ENABLE_SP_LOGGING)

// If ENABLE_SP_LOGGING is defined, then log all shared pointer assignments and
// allow them to be queried using a pointer by a call to:
#include <assert.h>
#include <execinfo.h>

#include "llvm/ADT/STLExtras.h"

#include <map>
#include <mutex>
#include <vector>

class Backtrace {
public:
  Backtrace();

  ~Backtrace();

  void GetFrames();

  void Dump() const;

private:
  void *m_sp_this;
  std::vector<void *> m_frames;
};

Backtrace::Backtrace() : m_frames() {}

Backtrace::~Backtrace() {}

void Backtrace::GetFrames() {
  void *frames[1024];
  const int count = ::backtrace(frames, llvm::array_lengthof(frames));
  if (count > 2)
    m_frames.assign(frames + 2, frames + (count - 2));
}

void Backtrace::Dump() const {
  if (!m_frames.empty())
    ::backtrace_symbols_fd(m_frames.data(), m_frames.size(), STDOUT_FILENO);
  write(STDOUT_FILENO, "\n\n", 2);
}

extern "C" void track_sp(void *sp_this, void *ptr, long use_count) {
  typedef std::pair<void *, Backtrace> PtrBacktracePair;
  typedef std::map<void *, PtrBacktracePair> PtrToBacktraceMap;
  static std::mutex g_mutex;
  std::lock_guard<std::mutex> guard(g_mutex);
  static PtrToBacktraceMap g_map;

  if (sp_this) {
    printf("sp(%p) -> %p %lu\n", sp_this, ptr, use_count);

    if (ptr) {
      Backtrace bt;
      bt.GetFrames();
      g_map[sp_this] = std::make_pair(ptr, bt);
    } else {
      g_map.erase(sp_this);
    }
  } else {
    if (ptr)
      printf("Searching for shared pointers that are tracking %p: ", ptr);
    else
      printf("Dump all live shared pointres: ");

    uint32_t matches = 0;
    PtrToBacktraceMap::iterator pos, end = g_map.end();
    for (pos = g_map.begin(); pos != end; ++pos) {
      if (ptr == NULL || pos->second.first == ptr) {
        ++matches;
        printf("\nsp(%p): %p\n", pos->first, pos->second.first);
        pos->second.second.Dump();
      }
    }
    if (matches == 0) {
      printf("none.\n");
    }
  }
}
// Put dump_sp_refs in the lldb namespace to it gets through our exports lists
// filter in the LLDB.framework or lldb.so
namespace lldb {

void dump_sp_refs(void *ptr) {
  // Use a specially crafted call to "track_sp" which will dump info on all
  // live shared pointers that reference "ptr"
  track_sp(NULL, ptr, 0);
}
}

#endif

namespace lldb_private {

namespace imp {

shared_count::~shared_count() {}

void shared_count::add_shared() {
#ifdef _MSC_VER
  _InterlockedIncrement(&shared_owners_);
#else
  ++shared_owners_;
#endif
}

void shared_count::release_shared() {
#ifdef _MSC_VER
  if (_InterlockedDecrement(&shared_owners_) == -1)
#else
  if (--shared_owners_ == -1)
#endif
  {
    on_zero_shared();
    delete this;
  }
}

} // imp

} // namespace lldb
