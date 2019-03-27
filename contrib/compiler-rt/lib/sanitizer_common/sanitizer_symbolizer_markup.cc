//===-- sanitizer_symbolizer_markup.cc ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries.
//
// Implementation of offline markup symbolizer.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_SYMBOLIZER_MARKUP

#if SANITIZER_FUCHSIA
#include "sanitizer_symbolizer_fuchsia.h"
#elif SANITIZER_RTEMS
#include "sanitizer_symbolizer_rtems.h"
#endif
#include "sanitizer_stacktrace.h"
#include "sanitizer_symbolizer.h"

#include <limits.h>
#include <unwind.h>

namespace __sanitizer {

// This generic support for offline symbolizing is based on the
// Fuchsia port.  We don't do any actual symbolization per se.
// Instead, we emit text containing raw addresses and raw linkage
// symbol names, embedded in Fuchsia's symbolization markup format.
// Fuchsia's logging infrastructure emits enough information about
// process memory layout that a post-processing filter can do the
// symbolization and pretty-print the markup.  See the spec at:
// https://fuchsia.googlesource.com/zircon/+/master/docs/symbolizer_markup.md

// This is used by UBSan for type names, and by ASan for global variable names.
// It's expected to return a static buffer that will be reused on each call.
const char *Symbolizer::Demangle(const char *name) {
  static char buffer[kFormatDemangleMax];
  internal_snprintf(buffer, sizeof(buffer), kFormatDemangle, name);
  return buffer;
}

// This is used mostly for suppression matching.  Making it work
// would enable "interceptor_via_lib" suppressions.  It's also used
// once in UBSan to say "in module ..." in a message that also
// includes an address in the module, so post-processing can already
// pretty-print that so as to indicate the module.
bool Symbolizer::GetModuleNameAndOffsetForPC(uptr pc, const char **module_name,
                                             uptr *module_address) {
  return false;
}

// This is used in some places for suppression checking, which we
// don't really support for Fuchsia.  It's also used in UBSan to
// identify a PC location to a function name, so we always fill in
// the function member with a string containing markup around the PC
// value.
// TODO(mcgrathr): Under SANITIZER_GO, it's currently used by TSan
// to render stack frames, but that should be changed to use
// RenderStackFrame.
SymbolizedStack *Symbolizer::SymbolizePC(uptr addr) {
  SymbolizedStack *s = SymbolizedStack::New(addr);
  char buffer[kFormatFunctionMax];
  internal_snprintf(buffer, sizeof(buffer), kFormatFunction, addr);
  s->info.function = internal_strdup(buffer);
  return s;
}

// Always claim we succeeded, so that RenderDataInfo will be called.
bool Symbolizer::SymbolizeData(uptr addr, DataInfo *info) {
  info->Clear();
  info->start = addr;
  return true;
}

// We ignore the format argument to __sanitizer_symbolize_global.
void RenderData(InternalScopedString *buffer, const char *format,
                const DataInfo *DI, const char *strip_path_prefix) {
  buffer->append(kFormatData, DI->start);
}

// We don't support the stack_trace_format flag at all.
void RenderFrame(InternalScopedString *buffer, const char *format, int frame_no,
                 const AddressInfo &info, bool vs_style,
                 const char *strip_path_prefix, const char *strip_func_prefix) {
  buffer->append(kFormatFrame, frame_no, info.address);
}

Symbolizer *Symbolizer::PlatformInit() {
  return new (symbolizer_allocator_) Symbolizer({});
}

void Symbolizer::LateInitialize() { Symbolizer::GetOrInit(); }

void StartReportDeadlySignal() {}
void ReportDeadlySignal(const SignalContext &sig, u32 tid,
                        UnwindSignalStackCallbackType unwind,
                        const void *unwind_context) {}

#if SANITIZER_CAN_SLOW_UNWIND
struct UnwindTraceArg {
  BufferedStackTrace *stack;
  u32 max_depth;
};

_Unwind_Reason_Code Unwind_Trace(struct _Unwind_Context *ctx, void *param) {
  UnwindTraceArg *arg = static_cast<UnwindTraceArg *>(param);
  CHECK_LT(arg->stack->size, arg->max_depth);
  uptr pc = _Unwind_GetIP(ctx);
  if (pc < PAGE_SIZE) return _URC_NORMAL_STOP;
  arg->stack->trace_buffer[arg->stack->size++] = pc;
  return (arg->stack->size == arg->max_depth ? _URC_NORMAL_STOP
                                             : _URC_NO_REASON);
}

void BufferedStackTrace::SlowUnwindStack(uptr pc, u32 max_depth) {
  CHECK_GE(max_depth, 2);
  size = 0;
  UnwindTraceArg arg = {this, Min(max_depth + 1, kStackTraceMax)};
  _Unwind_Backtrace(Unwind_Trace, &arg);
  CHECK_GT(size, 0);
  // We need to pop a few frames so that pc is on top.
  uptr to_pop = LocatePcInTrace(pc);
  // trace_buffer[0] belongs to the current function so we always pop it,
  // unless there is only 1 frame in the stack trace (1 frame is always better
  // than 0!).
  PopStackFrames(Min(to_pop, static_cast<uptr>(1)));
  trace_buffer[0] = pc;
}

void BufferedStackTrace::SlowUnwindStackWithContext(uptr pc, void *context,
                                                    u32 max_depth) {
  CHECK_NE(context, nullptr);
  UNREACHABLE("signal context doesn't exist");
}
#endif  // SANITIZER_CAN_SLOW_UNWIND

}  // namespace __sanitizer

#endif  // SANITIZER_SYMBOLIZER_MARKUP
