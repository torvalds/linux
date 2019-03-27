//===-- sanitizer_stacktrace_libcdep.cc -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_stacktrace.h"
#include "sanitizer_stacktrace_printer.h"
#include "sanitizer_symbolizer.h"

namespace __sanitizer {

void StackTrace::Print() const {
  if (trace == nullptr || size == 0) {
    Printf("    <empty stack>\n\n");
    return;
  }
  InternalScopedString frame_desc(GetPageSizeCached() * 2);
  InternalScopedString dedup_token(GetPageSizeCached());
  int dedup_frames = common_flags()->dedup_token_length;
  uptr frame_num = 0;
  for (uptr i = 0; i < size && trace[i]; i++) {
    // PCs in stack traces are actually the return addresses, that is,
    // addresses of the next instructions after the call.
    uptr pc = GetPreviousInstructionPc(trace[i]);
    SymbolizedStack *frames = Symbolizer::GetOrInit()->SymbolizePC(pc);
    CHECK(frames);
    for (SymbolizedStack *cur = frames; cur; cur = cur->next) {
      frame_desc.clear();
      RenderFrame(&frame_desc, common_flags()->stack_trace_format, frame_num++,
                  cur->info, common_flags()->symbolize_vs_style,
                  common_flags()->strip_path_prefix);
      Printf("%s\n", frame_desc.data());
      if (dedup_frames-- > 0) {
        if (dedup_token.length())
          dedup_token.append("--");
        if (cur->info.function != nullptr)
          dedup_token.append(cur->info.function);
      }
    }
    frames->ClearAll();
  }
  // Always print a trailing empty line after stack trace.
  Printf("\n");
  if (dedup_token.length())
    Printf("DEDUP_TOKEN: %s\n", dedup_token.data());
}

void BufferedStackTrace::Unwind(u32 max_depth, uptr pc, uptr bp, void *context,
                                uptr stack_top, uptr stack_bottom,
                                bool request_fast_unwind) {
  top_frame_bp = (max_depth > 0) ? bp : 0;
  // Avoid doing any work for small max_depth.
  if (max_depth == 0) {
    size = 0;
    return;
  }
  if (max_depth == 1) {
    size = 1;
    trace_buffer[0] = pc;
    return;
  }
  if (!WillUseFastUnwind(request_fast_unwind)) {
#if SANITIZER_CAN_SLOW_UNWIND
    if (context)
      SlowUnwindStackWithContext(pc, context, max_depth);
    else
      SlowUnwindStack(pc, max_depth);
#else
    UNREACHABLE("slow unwind requested but not available");
#endif
  } else {
    FastUnwindStack(pc, bp, stack_top, stack_bottom, max_depth);
  }
}

static int GetModuleAndOffsetForPc(uptr pc, char *module_name,
                                   uptr module_name_len, uptr *pc_offset) {
  const char *found_module_name = nullptr;
  bool ok = Symbolizer::GetOrInit()->GetModuleNameAndOffsetForPC(
      pc, &found_module_name, pc_offset);

  if (!ok) return false;

  if (module_name && module_name_len) {
    internal_strncpy(module_name, found_module_name, module_name_len);
    module_name[module_name_len - 1] = '\x00';
  }
  return true;
}

}  // namespace __sanitizer
using namespace __sanitizer;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_symbolize_pc(uptr pc, const char *fmt, char *out_buf,
                              uptr out_buf_size) {
  if (!out_buf_size) return;
  pc = StackTrace::GetPreviousInstructionPc(pc);
  SymbolizedStack *frame = Symbolizer::GetOrInit()->SymbolizePC(pc);
  if (!frame) {
    internal_strncpy(out_buf, "<can't symbolize>", out_buf_size);
    out_buf[out_buf_size - 1] = 0;
    return;
  }
  InternalScopedString frame_desc(GetPageSizeCached());
  uptr frame_num = 0;
  // Reserve one byte for the final 0.
  char *out_end = out_buf + out_buf_size - 1;
  for (SymbolizedStack *cur = frame; cur && out_buf < out_end;
       cur = cur->next) {
    frame_desc.clear();
    RenderFrame(&frame_desc, fmt, frame_num++, cur->info,
                common_flags()->symbolize_vs_style,
                common_flags()->strip_path_prefix);
    if (!frame_desc.length())
      continue;
    // Reserve one byte for the terminating 0.
    uptr n = out_end - out_buf - 1;
    internal_strncpy(out_buf, frame_desc.data(), n);
    out_buf += __sanitizer::Min<uptr>(n, frame_desc.length());
    *out_buf++ = 0;
  }
  CHECK(out_buf <= out_end);
  *out_buf = 0;
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_symbolize_global(uptr data_addr, const char *fmt,
                                  char *out_buf, uptr out_buf_size) {
  if (!out_buf_size) return;
  out_buf[0] = 0;
  DataInfo DI;
  if (!Symbolizer::GetOrInit()->SymbolizeData(data_addr, &DI)) return;
  InternalScopedString data_desc(GetPageSizeCached());
  RenderData(&data_desc, fmt, &DI, common_flags()->strip_path_prefix);
  internal_strncpy(out_buf, data_desc.data(), out_buf_size);
  out_buf[out_buf_size - 1] = 0;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __sanitizer_get_module_and_offset_for_pc( // NOLINT
    uptr pc, char *module_name, uptr module_name_len, uptr *pc_offset) {
  return __sanitizer::GetModuleAndOffsetForPc(pc, module_name, module_name_len,
                                              pc_offset);
}
}  // extern "C"
