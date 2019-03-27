#include "hwasan_thread_list.h"

namespace __hwasan {
static ALIGNED(16) char thread_list_placeholder[sizeof(HwasanThreadList)];
static HwasanThreadList *hwasan_thread_list;

HwasanThreadList &hwasanThreadList() { return *hwasan_thread_list; }

void InitThreadList(uptr storage, uptr size) {
  CHECK(hwasan_thread_list == nullptr);
  hwasan_thread_list =
      new (thread_list_placeholder) HwasanThreadList(storage, size);
}

} // namespace
