//===-- asan_errors.cc ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan implementation for error structures.
//===----------------------------------------------------------------------===//

#include "asan_errors.h"
#include "asan_descriptions.h"
#include "asan_mapping.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

namespace __asan {

static void OnStackUnwind(const SignalContext &sig,
                          const void *callback_context,
                          BufferedStackTrace *stack) {
  bool fast = common_flags()->fast_unwind_on_fatal;
#if SANITIZER_FREEBSD || SANITIZER_NETBSD
  // On FreeBSD the slow unwinding that leverages _Unwind_Backtrace()
  // yields the call stack of the signal's handler and not of the code
  // that raised the signal (as it does on Linux).
  fast = true;
#endif
  // Tests and maybe some users expect that scariness is going to be printed
  // just before the stack. As only asan has scariness score we have no
  // corresponding code in the sanitizer_common and we use this callback to
  // print it.
  static_cast<const ScarinessScoreBase *>(callback_context)->Print();
  GetStackTrace(stack, kStackTraceMax, sig.pc, sig.bp, sig.context, fast);
}

void ErrorDeadlySignal::Print() {
  ReportDeadlySignal(signal, tid, &OnStackUnwind, &scariness);
}

void ErrorDoubleFree::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: attempting %s on %p in thread %s:\n",
      scariness.GetDescription(), addr_description.addr,
      AsanThreadIdAndName(tid).c_str());
  Printf("%s", d.Default());
  scariness.Print();
  GET_STACK_TRACE_FATAL(second_free_stack->trace[0],
                        second_free_stack->top_frame_bp);
  stack.Print();
  addr_description.Print();
  ReportErrorSummary(scariness.GetDescription(), &stack);
}

void ErrorNewDeleteTypeMismatch::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: %s on %p in thread %s:\n",
      scariness.GetDescription(), addr_description.addr,
      AsanThreadIdAndName(tid).c_str());
  Printf("%s  object passed to delete has wrong type:\n", d.Default());
  if (delete_size != 0) {
    Printf(
        "  size of the allocated type:   %zd bytes;\n"
        "  size of the deallocated type: %zd bytes.\n",
        addr_description.chunk_access.chunk_size, delete_size);
  }
  const uptr user_alignment =
      addr_description.chunk_access.user_requested_alignment;
  if (delete_alignment != user_alignment) {
    char user_alignment_str[32];
    char delete_alignment_str[32];
    internal_snprintf(user_alignment_str, sizeof(user_alignment_str),
                      "%zd bytes", user_alignment);
    internal_snprintf(delete_alignment_str, sizeof(delete_alignment_str),
                      "%zd bytes", delete_alignment);
    static const char *kDefaultAlignment = "default-aligned";
    Printf(
        "  alignment of the allocated type:   %s;\n"
        "  alignment of the deallocated type: %s.\n",
        user_alignment > 0 ? user_alignment_str : kDefaultAlignment,
        delete_alignment > 0 ? delete_alignment_str : kDefaultAlignment);
  }
  CHECK_GT(free_stack->size, 0);
  scariness.Print();
  GET_STACK_TRACE_FATAL(free_stack->trace[0], free_stack->top_frame_bp);
  stack.Print();
  addr_description.Print();
  ReportErrorSummary(scariness.GetDescription(), &stack);
  Report(
      "HINT: if you don't care about these errors you may set "
      "ASAN_OPTIONS=new_delete_type_mismatch=0\n");
}

void ErrorFreeNotMalloced::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: attempting free on address "
      "which was not malloc()-ed: %p in thread %s\n",
      addr_description.Address(), AsanThreadIdAndName(tid).c_str());
  Printf("%s", d.Default());
  CHECK_GT(free_stack->size, 0);
  scariness.Print();
  GET_STACK_TRACE_FATAL(free_stack->trace[0], free_stack->top_frame_bp);
  stack.Print();
  addr_description.Print();
  ReportErrorSummary(scariness.GetDescription(), &stack);
}

void ErrorAllocTypeMismatch::Print() {
  static const char *alloc_names[] = {"INVALID", "malloc", "operator new",
                                      "operator new []"};
  static const char *dealloc_names[] = {"INVALID", "free", "operator delete",
                                        "operator delete []"};
  CHECK_NE(alloc_type, dealloc_type);
  Decorator d;
  Printf("%s", d.Error());
  Report("ERROR: AddressSanitizer: %s (%s vs %s) on %p\n",
         scariness.GetDescription(), alloc_names[alloc_type],
         dealloc_names[dealloc_type], addr_description.Address());
  Printf("%s", d.Default());
  CHECK_GT(dealloc_stack->size, 0);
  scariness.Print();
  GET_STACK_TRACE_FATAL(dealloc_stack->trace[0], dealloc_stack->top_frame_bp);
  stack.Print();
  addr_description.Print();
  ReportErrorSummary(scariness.GetDescription(), &stack);
  Report(
      "HINT: if you don't care about these errors you may set "
      "ASAN_OPTIONS=alloc_dealloc_mismatch=0\n");
}

void ErrorMallocUsableSizeNotOwned::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: attempting to call malloc_usable_size() for "
      "pointer which is not owned: %p\n",
      addr_description.Address());
  Printf("%s", d.Default());
  stack->Print();
  addr_description.Print();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorSanitizerGetAllocatedSizeNotOwned::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: attempting to call "
      "__sanitizer_get_allocated_size() for pointer which is not owned: %p\n",
      addr_description.Address());
  Printf("%s", d.Default());
  stack->Print();
  addr_description.Print();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorCallocOverflow::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: calloc parameters overflow: count * size "
      "(%zd * %zd) cannot be represented in type size_t (thread %s)\n",
      count, size, AsanThreadIdAndName(tid).c_str());
  Printf("%s", d.Default());
  stack->Print();
  PrintHintAllocatorCannotReturnNull();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorPvallocOverflow::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: pvalloc parameters overflow: size 0x%zx "
      "rounded up to system page size 0x%zx cannot be represented in type "
      "size_t (thread %s)\n",
      size, GetPageSizeCached(), AsanThreadIdAndName(tid).c_str());
  Printf("%s", d.Default());
  stack->Print();
  PrintHintAllocatorCannotReturnNull();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorInvalidAllocationAlignment::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: invalid allocation alignment: %zd, "
      "alignment must be a power of two (thread %s)\n",
      alignment, AsanThreadIdAndName(tid).c_str());
  Printf("%s", d.Default());
  stack->Print();
  PrintHintAllocatorCannotReturnNull();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorInvalidAlignedAllocAlignment::Print() {
  Decorator d;
  Printf("%s", d.Error());
#if SANITIZER_POSIX
  Report("ERROR: AddressSanitizer: invalid alignment requested in "
         "aligned_alloc: %zd, alignment must be a power of two and the "
         "requested size 0x%zx must be a multiple of alignment "
         "(thread %s)\n", alignment, size, AsanThreadIdAndName(tid).c_str());
#else
  Report("ERROR: AddressSanitizer: invalid alignment requested in "
         "aligned_alloc: %zd, the requested size 0x%zx must be a multiple of "
         "alignment (thread %s)\n", alignment, size,
         AsanThreadIdAndName(tid).c_str());
#endif
  Printf("%s", d.Default());
  stack->Print();
  PrintHintAllocatorCannotReturnNull();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorInvalidPosixMemalignAlignment::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: invalid alignment requested in posix_memalign: "
      "%zd, alignment must be a power of two and a multiple of sizeof(void*) "
      "== %zd (thread %s)\n",
      alignment, sizeof(void*), AsanThreadIdAndName(tid).c_str());  // NOLINT
  Printf("%s", d.Default());
  stack->Print();
  PrintHintAllocatorCannotReturnNull();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorAllocationSizeTooBig::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: requested allocation size 0x%zx (0x%zx after "
      "adjustments for alignment, red zones etc.) exceeds maximum supported "
      "size of 0x%zx (thread %s)\n",
      user_size, total_size, max_size, AsanThreadIdAndName(tid).c_str());
  Printf("%s", d.Default());
  stack->Print();
  PrintHintAllocatorCannotReturnNull();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorRssLimitExceeded::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: specified RSS limit exceeded, currently set to "
      "soft_rss_limit_mb=%zd\n", common_flags()->soft_rss_limit_mb);
  Printf("%s", d.Default());
  stack->Print();
  PrintHintAllocatorCannotReturnNull();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorOutOfMemory::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: allocator is out of memory trying to allocate "
      "0x%zx bytes\n", requested_size);
  Printf("%s", d.Default());
  stack->Print();
  PrintHintAllocatorCannotReturnNull();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorStringFunctionMemoryRangesOverlap::Print() {
  Decorator d;
  char bug_type[100];
  internal_snprintf(bug_type, sizeof(bug_type), "%s-param-overlap", function);
  Printf("%s", d.Error());
  Report(
      "ERROR: AddressSanitizer: %s: memory ranges [%p,%p) and [%p, %p) "
      "overlap\n",
      bug_type, addr1_description.Address(),
      addr1_description.Address() + length1, addr2_description.Address(),
      addr2_description.Address() + length2);
  Printf("%s", d.Default());
  scariness.Print();
  stack->Print();
  addr1_description.Print();
  addr2_description.Print();
  ReportErrorSummary(bug_type, stack);
}

void ErrorStringFunctionSizeOverflow::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report("ERROR: AddressSanitizer: %s: (size=%zd)\n",
         scariness.GetDescription(), size);
  Printf("%s", d.Default());
  scariness.Print();
  stack->Print();
  addr_description.Print();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorBadParamsToAnnotateContiguousContainer::Print() {
  Report(
      "ERROR: AddressSanitizer: bad parameters to "
      "__sanitizer_annotate_contiguous_container:\n"
      "      beg     : %p\n"
      "      end     : %p\n"
      "      old_mid : %p\n"
      "      new_mid : %p\n",
      beg, end, old_mid, new_mid);
  uptr granularity = SHADOW_GRANULARITY;
  if (!IsAligned(beg, granularity))
    Report("ERROR: beg is not aligned by %d\n", granularity);
  stack->Print();
  ReportErrorSummary(scariness.GetDescription(), stack);
}

void ErrorODRViolation::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report("ERROR: AddressSanitizer: %s (%p):\n", scariness.GetDescription(),
         global1.beg);
  Printf("%s", d.Default());
  InternalScopedString g1_loc(256), g2_loc(256);
  PrintGlobalLocation(&g1_loc, global1);
  PrintGlobalLocation(&g2_loc, global2);
  Printf("  [1] size=%zd '%s' %s\n", global1.size,
         MaybeDemangleGlobalName(global1.name), g1_loc.data());
  Printf("  [2] size=%zd '%s' %s\n", global2.size,
         MaybeDemangleGlobalName(global2.name), g2_loc.data());
  if (stack_id1 && stack_id2) {
    Printf("These globals were registered at these points:\n");
    Printf("  [1]:\n");
    StackDepotGet(stack_id1).Print();
    Printf("  [2]:\n");
    StackDepotGet(stack_id2).Print();
  }
  Report(
      "HINT: if you don't care about these errors you may set "
      "ASAN_OPTIONS=detect_odr_violation=0\n");
  InternalScopedString error_msg(256);
  error_msg.append("%s: global '%s' at %s", scariness.GetDescription(),
                   MaybeDemangleGlobalName(global1.name), g1_loc.data());
  ReportErrorSummary(error_msg.data());
}

void ErrorInvalidPointerPair::Print() {
  Decorator d;
  Printf("%s", d.Error());
  Report("ERROR: AddressSanitizer: %s: %p %p\n", scariness.GetDescription(),
         addr1_description.Address(), addr2_description.Address());
  Printf("%s", d.Default());
  GET_STACK_TRACE_FATAL(pc, bp);
  stack.Print();
  addr1_description.Print();
  addr2_description.Print();
  ReportErrorSummary(scariness.GetDescription(), &stack);
}

static bool AdjacentShadowValuesAreFullyPoisoned(u8 *s) {
  return s[-1] > 127 && s[1] > 127;
}

ErrorGeneric::ErrorGeneric(u32 tid, uptr pc_, uptr bp_, uptr sp_, uptr addr,
                           bool is_write_, uptr access_size_)
    : ErrorBase(tid),
      addr_description(addr, access_size_, /*shouldLockThreadRegistry=*/false),
      pc(pc_),
      bp(bp_),
      sp(sp_),
      access_size(access_size_),
      is_write(is_write_),
      shadow_val(0) {
  scariness.Clear();
  if (access_size) {
    if (access_size <= 9) {
      char desr[] = "?-byte";
      desr[0] = '0' + access_size;
      scariness.Scare(access_size + access_size / 2, desr);
    } else if (access_size >= 10) {
      scariness.Scare(15, "multi-byte");
    }
    is_write ? scariness.Scare(20, "write") : scariness.Scare(1, "read");

    // Determine the error type.
    bug_descr = "unknown-crash";
    if (AddrIsInMem(addr)) {
      u8 *shadow_addr = (u8 *)MemToShadow(addr);
      // If we are accessing 16 bytes, look at the second shadow byte.
      if (*shadow_addr == 0 && access_size > SHADOW_GRANULARITY) shadow_addr++;
      // If we are in the partial right redzone, look at the next shadow byte.
      if (*shadow_addr > 0 && *shadow_addr < 128) shadow_addr++;
      bool far_from_bounds = false;
      shadow_val = *shadow_addr;
      int bug_type_score = 0;
      // For use-after-frees reads are almost as bad as writes.
      int read_after_free_bonus = 0;
      switch (shadow_val) {
        case kAsanHeapLeftRedzoneMagic:
        case kAsanArrayCookieMagic:
          bug_descr = "heap-buffer-overflow";
          bug_type_score = 10;
          far_from_bounds = AdjacentShadowValuesAreFullyPoisoned(shadow_addr);
          break;
        case kAsanHeapFreeMagic:
          bug_descr = "heap-use-after-free";
          bug_type_score = 20;
          if (!is_write) read_after_free_bonus = 18;
          break;
        case kAsanStackLeftRedzoneMagic:
          bug_descr = "stack-buffer-underflow";
          bug_type_score = 25;
          far_from_bounds = AdjacentShadowValuesAreFullyPoisoned(shadow_addr);
          break;
        case kAsanInitializationOrderMagic:
          bug_descr = "initialization-order-fiasco";
          bug_type_score = 1;
          break;
        case kAsanStackMidRedzoneMagic:
        case kAsanStackRightRedzoneMagic:
          bug_descr = "stack-buffer-overflow";
          bug_type_score = 25;
          far_from_bounds = AdjacentShadowValuesAreFullyPoisoned(shadow_addr);
          break;
        case kAsanStackAfterReturnMagic:
          bug_descr = "stack-use-after-return";
          bug_type_score = 30;
          if (!is_write) read_after_free_bonus = 18;
          break;
        case kAsanUserPoisonedMemoryMagic:
          bug_descr = "use-after-poison";
          bug_type_score = 20;
          break;
        case kAsanContiguousContainerOOBMagic:
          bug_descr = "container-overflow";
          bug_type_score = 10;
          break;
        case kAsanStackUseAfterScopeMagic:
          bug_descr = "stack-use-after-scope";
          bug_type_score = 10;
          break;
        case kAsanGlobalRedzoneMagic:
          bug_descr = "global-buffer-overflow";
          bug_type_score = 10;
          far_from_bounds = AdjacentShadowValuesAreFullyPoisoned(shadow_addr);
          break;
        case kAsanIntraObjectRedzone:
          bug_descr = "intra-object-overflow";
          bug_type_score = 10;
          break;
        case kAsanAllocaLeftMagic:
        case kAsanAllocaRightMagic:
          bug_descr = "dynamic-stack-buffer-overflow";
          bug_type_score = 25;
          far_from_bounds = AdjacentShadowValuesAreFullyPoisoned(shadow_addr);
          break;
      }
      scariness.Scare(bug_type_score + read_after_free_bonus, bug_descr);
      if (far_from_bounds) scariness.Scare(10, "far-from-bounds");
    }
  }
}

static void PrintContainerOverflowHint() {
  Printf("HINT: if you don't care about these errors you may set "
         "ASAN_OPTIONS=detect_container_overflow=0.\n"
         "If you suspect a false positive see also: "
         "https://github.com/google/sanitizers/wiki/"
         "AddressSanitizerContainerOverflow.\n");
}

static void PrintShadowByte(InternalScopedString *str, const char *before,
    u8 byte, const char *after = "\n") {
  PrintMemoryByte(str, before, byte, /*in_shadow*/true, after);
}

static void PrintLegend(InternalScopedString *str) {
  str->append(
      "Shadow byte legend (one shadow byte represents %d "
      "application bytes):\n",
      (int)SHADOW_GRANULARITY);
  PrintShadowByte(str, "  Addressable:           ", 0);
  str->append("  Partially addressable: ");
  for (u8 i = 1; i < SHADOW_GRANULARITY; i++) PrintShadowByte(str, "", i, " ");
  str->append("\n");
  PrintShadowByte(str, "  Heap left redzone:       ",
                  kAsanHeapLeftRedzoneMagic);
  PrintShadowByte(str, "  Freed heap region:       ", kAsanHeapFreeMagic);
  PrintShadowByte(str, "  Stack left redzone:      ",
                  kAsanStackLeftRedzoneMagic);
  PrintShadowByte(str, "  Stack mid redzone:       ",
                  kAsanStackMidRedzoneMagic);
  PrintShadowByte(str, "  Stack right redzone:     ",
                  kAsanStackRightRedzoneMagic);
  PrintShadowByte(str, "  Stack after return:      ",
                  kAsanStackAfterReturnMagic);
  PrintShadowByte(str, "  Stack use after scope:   ",
                  kAsanStackUseAfterScopeMagic);
  PrintShadowByte(str, "  Global redzone:          ", kAsanGlobalRedzoneMagic);
  PrintShadowByte(str, "  Global init order:       ",
                  kAsanInitializationOrderMagic);
  PrintShadowByte(str, "  Poisoned by user:        ",
                  kAsanUserPoisonedMemoryMagic);
  PrintShadowByte(str, "  Container overflow:      ",
                  kAsanContiguousContainerOOBMagic);
  PrintShadowByte(str, "  Array cookie:            ",
                  kAsanArrayCookieMagic);
  PrintShadowByte(str, "  Intra object redzone:    ",
                  kAsanIntraObjectRedzone);
  PrintShadowByte(str, "  ASan internal:           ", kAsanInternalHeapMagic);
  PrintShadowByte(str, "  Left alloca redzone:     ", kAsanAllocaLeftMagic);
  PrintShadowByte(str, "  Right alloca redzone:    ", kAsanAllocaRightMagic);
  PrintShadowByte(str, "  Shadow gap:              ", kAsanShadowGap);
}

static void PrintShadowBytes(InternalScopedString *str, const char *before,
                             u8 *bytes, u8 *guilty, uptr n) {
  Decorator d;
  if (before) str->append("%s%p:", before, bytes);
  for (uptr i = 0; i < n; i++) {
    u8 *p = bytes + i;
    const char *before =
        p == guilty ? "[" : (p - 1 == guilty && i != 0) ? "" : " ";
    const char *after = p == guilty ? "]" : "";
    PrintShadowByte(str, before, *p, after);
  }
  str->append("\n");
}

static void PrintShadowMemoryForAddress(uptr addr) {
  if (!AddrIsInMem(addr)) return;
  uptr shadow_addr = MemToShadow(addr);
  const uptr n_bytes_per_row = 16;
  uptr aligned_shadow = shadow_addr & ~(n_bytes_per_row - 1);
  InternalScopedString str(4096 * 8);
  str.append("Shadow bytes around the buggy address:\n");
  for (int i = -5; i <= 5; i++) {
    uptr row_shadow_addr = aligned_shadow + i * n_bytes_per_row;
    // Skip rows that would be outside the shadow range. This can happen when
    // the user address is near the bottom, top, or shadow gap of the address
    // space.
    if (!AddrIsInShadow(row_shadow_addr)) continue;
    const char *prefix = (i == 0) ? "=>" : "  ";
    PrintShadowBytes(&str, prefix, (u8 *)row_shadow_addr, (u8 *)shadow_addr,
                     n_bytes_per_row);
  }
  if (flags()->print_legend) PrintLegend(&str);
  Printf("%s", str.data());
}

void ErrorGeneric::Print() {
  Decorator d;
  Printf("%s", d.Error());
  uptr addr = addr_description.Address();
  Report("ERROR: AddressSanitizer: %s on address %p at pc %p bp %p sp %p\n",
         bug_descr, (void *)addr, pc, bp, sp);
  Printf("%s", d.Default());

  Printf("%s%s of size %zu at %p thread %s%s\n", d.Access(),
         access_size ? (is_write ? "WRITE" : "READ") : "ACCESS", access_size,
         (void *)addr, AsanThreadIdAndName(tid).c_str(), d.Default());

  scariness.Print();
  GET_STACK_TRACE_FATAL(pc, bp);
  stack.Print();

  // Pass bug_descr because we have a special case for
  // initialization-order-fiasco
  addr_description.Print(bug_descr);
  if (shadow_val == kAsanContiguousContainerOOBMagic)
    PrintContainerOverflowHint();
  ReportErrorSummary(bug_descr, &stack);
  PrintShadowMemoryForAddress(addr);
}

}  // namespace __asan
