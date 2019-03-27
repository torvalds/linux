//===-- tsan_symbolize.cc -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//

#include "tsan_symbolize.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_symbolizer.h"
#include "tsan_flags.h"
#include "tsan_report.h"
#include "tsan_rtl.h"

namespace __tsan {

void EnterSymbolizer() {
  ThreadState *thr = cur_thread();
  CHECK(!thr->in_symbolizer);
  thr->in_symbolizer = true;
  thr->ignore_interceptors++;
}

void ExitSymbolizer() {
  ThreadState *thr = cur_thread();
  CHECK(thr->in_symbolizer);
  thr->in_symbolizer = false;
  thr->ignore_interceptors--;
}

// Legacy API.
// May be overriden by JIT/JAVA/etc,
// whatever produces PCs marked with kExternalPCBit.
SANITIZER_WEAK_DEFAULT_IMPL
bool __tsan_symbolize_external(uptr pc, char *func_buf, uptr func_siz,
                               char *file_buf, uptr file_siz, int *line,
                               int *col) {
  return false;
}

// New API: call __tsan_symbolize_external_ex only when it exists.
// Once old clients are gone, provide dummy implementation.
SANITIZER_WEAK_DEFAULT_IMPL
void __tsan_symbolize_external_ex(uptr pc,
                                  void (*add_frame)(void *, const char *,
                                                    const char *, int, int),
                                  void *ctx) {}

struct SymbolizedStackBuilder {
  SymbolizedStack *head;
  SymbolizedStack *tail;
  uptr addr;
};

static void AddFrame(void *ctx, const char *function_name, const char *file,
                     int line, int column) {
  SymbolizedStackBuilder *ssb = (struct SymbolizedStackBuilder *)ctx;
  if (ssb->tail) {
    ssb->tail->next = SymbolizedStack::New(ssb->addr);
    ssb->tail = ssb->tail->next;
  } else {
    ssb->head = ssb->tail = SymbolizedStack::New(ssb->addr);
  }
  AddressInfo *info = &ssb->tail->info;
  if (function_name) {
    info->function = internal_strdup(function_name);
  }
  if (file) {
    info->file = internal_strdup(file);
  }
  info->line = line;
  info->column = column;
}

SymbolizedStack *SymbolizeCode(uptr addr) {
  // Check if PC comes from non-native land.
  if (addr & kExternalPCBit) {
    SymbolizedStackBuilder ssb = {nullptr, nullptr, addr};
    __tsan_symbolize_external_ex(addr, AddFrame, &ssb);
    if (ssb.head)
      return ssb.head;
    // Legacy code: remove along with the declaration above
    // once all clients using this API are gone.
    // Declare static to not consume too much stack space.
    // We symbolize reports in a single thread, so this is fine.
    static char func_buf[1024];
    static char file_buf[1024];
    int line, col;
    SymbolizedStack *frame = SymbolizedStack::New(addr);
    if (__tsan_symbolize_external(addr, func_buf, sizeof(func_buf), file_buf,
                                  sizeof(file_buf), &line, &col)) {
      frame->info.function = internal_strdup(func_buf);
      frame->info.file = internal_strdup(file_buf);
      frame->info.line = line;
      frame->info.column = col;
    }
    return frame;
  }
  return Symbolizer::GetOrInit()->SymbolizePC(addr);
}

ReportLocation *SymbolizeData(uptr addr) {
  DataInfo info;
  if (!Symbolizer::GetOrInit()->SymbolizeData(addr, &info))
    return 0;
  ReportLocation *ent = ReportLocation::New(ReportLocationGlobal);
  internal_memcpy(&ent->global, &info, sizeof(info));
  return ent;
}

void SymbolizeFlush() {
  Symbolizer::GetOrInit()->Flush();
}

}  // namespace __tsan
