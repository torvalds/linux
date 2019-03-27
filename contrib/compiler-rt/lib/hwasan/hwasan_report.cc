//===-- hwasan_report.cc --------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
// Error reporting.
//===----------------------------------------------------------------------===//

#include "hwasan.h"
#include "hwasan_allocator.h"
#include "hwasan_mapping.h"
#include "hwasan_thread.h"
#include "hwasan_thread_list.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "sanitizer_common/sanitizer_report_decorator.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stacktrace_printer.h"
#include "sanitizer_common/sanitizer_symbolizer.h"

using namespace __sanitizer;

namespace __hwasan {

class ScopedReport {
 public:
  ScopedReport(bool fatal = false) : error_message_(1), fatal(fatal) {
    BlockingMutexLock lock(&error_message_lock_);
    error_message_ptr_ = fatal ? &error_message_ : nullptr;
  }

  ~ScopedReport() {
    BlockingMutexLock lock(&error_message_lock_);
    if (fatal) {
      SetAbortMessage(error_message_.data());
      Die();
    }
    error_message_ptr_ = nullptr;
  }

  static void MaybeAppendToErrorMessage(const char *msg) {
    BlockingMutexLock lock(&error_message_lock_);
    if (!error_message_ptr_)
      return;
    uptr len = internal_strlen(msg);
    uptr old_size = error_message_ptr_->size();
    error_message_ptr_->resize(old_size + len);
    // overwrite old trailing '\0', keep new trailing '\0' untouched.
    internal_memcpy(&(*error_message_ptr_)[old_size - 1], msg, len);
  }
 private:
  ScopedErrorReportLock error_report_lock_;
  InternalMmapVector<char> error_message_;
  bool fatal;

  static InternalMmapVector<char> *error_message_ptr_;
  static BlockingMutex error_message_lock_;
};

InternalMmapVector<char> *ScopedReport::error_message_ptr_;
BlockingMutex ScopedReport::error_message_lock_;

// If there is an active ScopedReport, append to its error message.
void AppendToErrorMessageBuffer(const char *buffer) {
  ScopedReport::MaybeAppendToErrorMessage(buffer);
}

static StackTrace GetStackTraceFromId(u32 id) {
  CHECK(id);
  StackTrace res = StackDepotGet(id);
  CHECK(res.trace);
  return res;
}

// A RAII object that holds a copy of the current thread stack ring buffer.
// The actual stack buffer may change while we are iterating over it (for
// example, Printf may call syslog() which can itself be built with hwasan).
class SavedStackAllocations {
 public:
  SavedStackAllocations(StackAllocationsRingBuffer *rb) {
    uptr size = rb->size() * sizeof(uptr);
    void *storage =
        MmapAlignedOrDieOnFatalError(size, size * 2, "saved stack allocations");
    new (&rb_) StackAllocationsRingBuffer(*rb, storage);
  }

  ~SavedStackAllocations() {
    StackAllocationsRingBuffer *rb = get();
    UnmapOrDie(rb->StartOfStorage(), rb->size() * sizeof(uptr));
  }

  StackAllocationsRingBuffer *get() {
    return (StackAllocationsRingBuffer *)&rb_;
  }

 private:
  uptr rb_;
};

class Decorator: public __sanitizer::SanitizerCommonDecorator {
 public:
  Decorator() : SanitizerCommonDecorator() { }
  const char *Access() { return Blue(); }
  const char *Allocation() const { return Magenta(); }
  const char *Origin() const { return Magenta(); }
  const char *Name() const { return Green(); }
  const char *Location() { return Green(); }
  const char *Thread() { return Green(); }
};

// Returns the index of the rb element that matches tagged_addr (plus one),
// or zero if found nothing.
uptr FindHeapAllocation(HeapAllocationsRingBuffer *rb,
                        uptr tagged_addr,
                        HeapAllocationRecord *har) {
  if (!rb) return 0;
  for (uptr i = 0, size = rb->size(); i < size; i++) {
    auto h = (*rb)[i];
    if (h.tagged_addr <= tagged_addr &&
        h.tagged_addr + h.requested_size > tagged_addr) {
      *har = h;
      return i + 1;
    }
  }
  return 0;
}

void PrintAddressDescription(
    uptr tagged_addr, uptr access_size,
    StackAllocationsRingBuffer *current_stack_allocations) {
  Decorator d;
  int num_descriptions_printed = 0;
  uptr untagged_addr = UntagAddr(tagged_addr);

  // Print some very basic information about the address, if it's a heap.
  HwasanChunkView chunk = FindHeapChunkByAddress(untagged_addr);
  if (uptr beg = chunk.Beg()) {
    uptr size = chunk.ActualSize();
    Printf("%s[%p,%p) is a %s %s heap chunk; "
           "size: %zd offset: %zd\n%s",
           d.Location(),
           beg, beg + size,
           chunk.FromSmallHeap() ? "small" : "large",
           chunk.IsAllocated() ? "allocated" : "unallocated",
           size, untagged_addr - beg,
           d.Default());
  }

  // Check if this looks like a heap buffer overflow by scanning
  // the shadow left and right and looking for the first adjacent
  // object with a different memory tag. If that tag matches addr_tag,
  // check the allocator if it has a live chunk there.
  tag_t addr_tag = GetTagFromPointer(tagged_addr);
  tag_t *tag_ptr = reinterpret_cast<tag_t*>(MemToShadow(untagged_addr));
  if (*tag_ptr != addr_tag) { // should be true usually.
    tag_t *left = tag_ptr, *right = tag_ptr;
    // scan left.
    for (int i = 0; i < 1000 && *left == *tag_ptr; i++, left--){}
    // scan right.
    for (int i = 0; i < 1000 && *right == *tag_ptr; i++, right++){}
    // Chose the object that has addr_tag and that is closer to addr.
    tag_t *candidate = nullptr;
    if (*right == addr_tag && *left == addr_tag)
      candidate = right - tag_ptr < tag_ptr - left ? right : left;
    else if (*right == addr_tag)
      candidate = right;
    else if (*left == addr_tag)
      candidate = left;

    if (candidate) {
      uptr mem = ShadowToMem(reinterpret_cast<uptr>(candidate));
      HwasanChunkView chunk = FindHeapChunkByAddress(mem);
      if (chunk.IsAllocated()) {
        Printf("%s", d.Location());
        Printf(
            "%p is located %zd bytes to the %s of %zd-byte region [%p,%p)\n",
            untagged_addr,
            candidate == left ? untagged_addr - chunk.End()
            : chunk.Beg() - untagged_addr,
            candidate == right ? "left" : "right", chunk.UsedSize(),
            chunk.Beg(), chunk.End());
        Printf("%s", d.Allocation());
        Printf("allocated here:\n");
        Printf("%s", d.Default());
        GetStackTraceFromId(chunk.GetAllocStackId()).Print();
        num_descriptions_printed++;
      }
    }
  }

  hwasanThreadList().VisitAllLiveThreads([&](Thread *t) {
    // Scan all threads' ring buffers to find if it's a heap-use-after-free.
    HeapAllocationRecord har;
    if (uptr D = FindHeapAllocation(t->heap_allocations(), tagged_addr, &har)) {
      Printf("%s", d.Location());
      Printf("%p is located %zd bytes inside of %zd-byte region [%p,%p)\n",
             untagged_addr, untagged_addr - UntagAddr(har.tagged_addr),
             har.requested_size, UntagAddr(har.tagged_addr),
             UntagAddr(har.tagged_addr) + har.requested_size);
      Printf("%s", d.Allocation());
      Printf("freed by thread T%zd here:\n", t->unique_id());
      Printf("%s", d.Default());
      GetStackTraceFromId(har.free_context_id).Print();

      Printf("%s", d.Allocation());
      Printf("previously allocated here:\n", t);
      Printf("%s", d.Default());
      GetStackTraceFromId(har.alloc_context_id).Print();

      // Print a developer note: the index of this heap object
      // in the thread's deallocation ring buffer.
      Printf("hwasan_dev_note_heap_rb_distance: %zd %zd\n", D,
             flags()->heap_history_size);

      t->Announce();
      num_descriptions_printed++;
    }

    // Very basic check for stack memory.
    if (t->AddrIsInStack(untagged_addr)) {
      Printf("%s", d.Location());
      Printf("Address %p is located in stack of thread T%zd\n", untagged_addr,
             t->unique_id());
      Printf("%s", d.Default());
      t->Announce();

      // Temporary report section, needs to be improved.
      Printf("Previously allocated frames:\n");
      auto *sa = (t == GetCurrentThread() && current_stack_allocations)
                     ? current_stack_allocations
                     : t->stack_allocations();
      uptr frames = Min((uptr)flags()->stack_history_size, sa->size());
      InternalScopedString frame_desc(GetPageSizeCached() * 2);
      for (uptr i = 0; i < frames; i++) {
        uptr record = (*sa)[i];
        if (!record)
          break;
        uptr sp = (record >> 48) << 4;
        uptr pc_mask = (1ULL << 48) - 1;
        uptr pc = record & pc_mask;
        if (SymbolizedStack *frame = Symbolizer::GetOrInit()->SymbolizePC(pc)) {
          frame_desc.append(" sp: 0x%zx pc: %p ", sp, pc);
          RenderFrame(&frame_desc, "in %f %s:%l\n", 0, frame->info,
                      common_flags()->symbolize_vs_style,
                      common_flags()->strip_path_prefix);
          frame->ClearAll();
          if (auto Descr = GetStackFrameDescr(pc))
            frame_desc.append("  %s\n", Descr);
        }
        Printf("%s", frame_desc.data());
        frame_desc.clear();
      }

      num_descriptions_printed++;
    }
  });

  // Print the remaining threads, as an extra information, 1 line per thread.
  hwasanThreadList().VisitAllLiveThreads([&](Thread *t) { t->Announce(); });

  if (!num_descriptions_printed)
    // We exhausted our possibilities. Bail out.
    Printf("HWAddressSanitizer can not describe address in more detail.\n");
}

void ReportStats() {}

static void PrintTagsAroundAddr(tag_t *tag_ptr) {
  Printf(
      "Memory tags around the buggy address (one tag corresponds to %zd "
      "bytes):\n", kShadowAlignment);

  const uptr row_len = 16;  // better be power of two.
  const uptr num_rows = 17;
  tag_t *center_row_beg = reinterpret_cast<tag_t *>(
      RoundDownTo(reinterpret_cast<uptr>(tag_ptr), row_len));
  tag_t *beg_row = center_row_beg - row_len * (num_rows / 2);
  tag_t *end_row = center_row_beg + row_len * (num_rows / 2);
  InternalScopedString s(GetPageSizeCached() * 8);
  for (tag_t *row = beg_row; row < end_row; row += row_len) {
    s.append("%s", row == center_row_beg ? "=>" : "  ");
    for (uptr i = 0; i < row_len; i++) {
      s.append("%s", row + i == tag_ptr ? "[" : " ");
      s.append("%02x", row[i]);
      s.append("%s", row + i == tag_ptr ? "]" : " ");
    }
    s.append("%s\n", row == center_row_beg ? "<=" : "  ");
  }
  Printf("%s", s.data());
}

void ReportInvalidFree(StackTrace *stack, uptr tagged_addr) {
  ScopedReport R(flags()->halt_on_error);

  uptr untagged_addr = UntagAddr(tagged_addr);
  tag_t ptr_tag = GetTagFromPointer(tagged_addr);
  tag_t *tag_ptr = reinterpret_cast<tag_t*>(MemToShadow(untagged_addr));
  tag_t mem_tag = *tag_ptr;
  Decorator d;
  Printf("%s", d.Error());
  uptr pc = stack->size ? stack->trace[0] : 0;
  const char *bug_type = "invalid-free";
  Report("ERROR: %s: %s on address %p at pc %p\n", SanitizerToolName, bug_type,
         untagged_addr, pc);
  Printf("%s", d.Access());
  Printf("tags: %02x/%02x (ptr/mem)\n", ptr_tag, mem_tag);
  Printf("%s", d.Default());

  stack->Print();

  PrintAddressDescription(tagged_addr, 0, nullptr);

  PrintTagsAroundAddr(tag_ptr);

  ReportErrorSummary(bug_type, stack);
}

void ReportTailOverwritten(StackTrace *stack, uptr tagged_addr, uptr orig_size,
                           uptr tail_size, const u8 *expected) {
  ScopedReport R(flags()->halt_on_error);
  Decorator d;
  uptr untagged_addr = UntagAddr(tagged_addr);
  Printf("%s", d.Error());
  const char *bug_type = "alocation-tail-overwritten";
  Report("ERROR: %s: %s; heap object [%p,%p) of size %zd\n", SanitizerToolName,
         bug_type, untagged_addr, untagged_addr + orig_size, orig_size);
  Printf("\n%s", d.Default());
  stack->Print();
  HwasanChunkView chunk = FindHeapChunkByAddress(untagged_addr);
  if (chunk.Beg()) {
    Printf("%s", d.Allocation());
    Printf("allocated here:\n");
    Printf("%s", d.Default());
    GetStackTraceFromId(chunk.GetAllocStackId()).Print();
  }

  InternalScopedString s(GetPageSizeCached() * 8);
  CHECK_GT(tail_size, 0U);
  CHECK_LT(tail_size, kShadowAlignment);
  u8 *tail = reinterpret_cast<u8*>(untagged_addr + orig_size);
  s.append("Tail contains: ");
  for (uptr i = 0; i < kShadowAlignment - tail_size; i++)
    s.append(".. ");
  for (uptr i = 0; i < tail_size; i++)
    s.append("%02x ", tail[i]);
  s.append("\n");
  s.append("Expected:      ");
  for (uptr i = 0; i < kShadowAlignment - tail_size; i++)
    s.append(".. ");
  for (uptr i = 0; i < tail_size; i++)
    s.append("%02x ", expected[i]);
  s.append("\n");
  s.append("               ");
  for (uptr i = 0; i < kShadowAlignment - tail_size; i++)
    s.append("   ");
  for (uptr i = 0; i < tail_size; i++)
    s.append("%s ", expected[i] != tail[i] ? "^^" : "  ");

  s.append("\nThis error occurs when a buffer overflow overwrites memory\n"
    "to the right of a heap object, but within the %zd-byte granule, e.g.\n"
    "   char *x = new char[20];\n"
    "   x[25] = 42;\n"
    "By default %s does not detect such bugs at the time of write,\n"
    "but can detect them at the time of free/delete.\n"
    "To disable this feature set HWASAN_OPTIONS=free_checks_tail_magic=0;\n"
    "To enable checking at the time of access, set "
    "HWASAN_OPTIONS=malloc_align_right to non-zero\n\n",
    kShadowAlignment, SanitizerToolName);
  Printf("%s", s.data());
  GetCurrentThread()->Announce();

  tag_t *tag_ptr = reinterpret_cast<tag_t*>(MemToShadow(untagged_addr));
  PrintTagsAroundAddr(tag_ptr);

  ReportErrorSummary(bug_type, stack);
}

void ReportTagMismatch(StackTrace *stack, uptr tagged_addr, uptr access_size,
                       bool is_store, bool fatal) {
  ScopedReport R(fatal);
  SavedStackAllocations current_stack_allocations(
      GetCurrentThread()->stack_allocations());

  Decorator d;
  Printf("%s", d.Error());
  uptr untagged_addr = UntagAddr(tagged_addr);
  // TODO: when possible, try to print heap-use-after-free, etc.
  const char *bug_type = "tag-mismatch";
  uptr pc = stack->size ? stack->trace[0] : 0;
  Report("ERROR: %s: %s on address %p at pc %p\n", SanitizerToolName, bug_type,
         untagged_addr, pc);

  Thread *t = GetCurrentThread();

  tag_t ptr_tag = GetTagFromPointer(tagged_addr);
  tag_t *tag_ptr = reinterpret_cast<tag_t*>(MemToShadow(untagged_addr));
  tag_t mem_tag = *tag_ptr;
  Printf("%s", d.Access());
  Printf("%s of size %zu at %p tags: %02x/%02x (ptr/mem) in thread T%zd\n",
         is_store ? "WRITE" : "READ", access_size, untagged_addr, ptr_tag,
         mem_tag, t->unique_id());
  Printf("%s", d.Default());

  stack->Print();

  PrintAddressDescription(tagged_addr, access_size,
                          current_stack_allocations.get());
  t->Announce();

  PrintTagsAroundAddr(tag_ptr);

  ReportErrorSummary(bug_type, stack);
}

}  // namespace __hwasan
