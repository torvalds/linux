//===-- tsan_external.cc --------------------------------------------------===//
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
#include "tsan_rtl.h"
#include "tsan_interceptors.h"

namespace __tsan {

#define CALLERPC ((uptr)__builtin_return_address(0))

struct TagData {
  const char *object_type;
  const char *header;
};

static TagData registered_tags[kExternalTagMax] = {
  {},
  {"Swift variable", "Swift access race"},
};
static atomic_uint32_t used_tags{kExternalTagFirstUserAvailable};  // NOLINT.
static TagData *GetTagData(uptr tag) {
  // Invalid/corrupted tag?  Better return NULL and let the caller deal with it.
  if (tag >= atomic_load(&used_tags, memory_order_relaxed)) return nullptr;
  return &registered_tags[tag];
}

const char *GetObjectTypeFromTag(uptr tag) {
  TagData *tag_data = GetTagData(tag);
  return tag_data ? tag_data->object_type : nullptr;
}

const char *GetReportHeaderFromTag(uptr tag) {
  TagData *tag_data = GetTagData(tag);
  return tag_data ? tag_data->header : nullptr;
}

void InsertShadowStackFrameForTag(ThreadState *thr, uptr tag) {
  FuncEntry(thr, (uptr)&registered_tags[tag]);
}

uptr TagFromShadowStackFrame(uptr pc) {
  uptr tag_count = atomic_load(&used_tags, memory_order_relaxed);
  void *pc_ptr = (void *)pc;
  if (pc_ptr < GetTagData(0) || pc_ptr > GetTagData(tag_count - 1))
    return 0;
  return (TagData *)pc_ptr - GetTagData(0);
}

#if !SANITIZER_GO

typedef void(*AccessFunc)(ThreadState *, uptr, uptr, int);
void ExternalAccess(void *addr, void *caller_pc, void *tag, AccessFunc access) {
  CHECK_LT(tag, atomic_load(&used_tags, memory_order_relaxed));
  ThreadState *thr = cur_thread();
  if (caller_pc) FuncEntry(thr, (uptr)caller_pc);
  InsertShadowStackFrameForTag(thr, (uptr)tag);
  bool in_ignored_lib;
  if (!caller_pc || !libignore()->IsIgnored((uptr)caller_pc, &in_ignored_lib)) {
    access(thr, CALLERPC, (uptr)addr, kSizeLog1);
  }
  FuncExit(thr);
  if (caller_pc) FuncExit(thr);
}

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void *__tsan_external_register_tag(const char *object_type) {
  uptr new_tag = atomic_fetch_add(&used_tags, 1, memory_order_relaxed);
  CHECK_LT(new_tag, kExternalTagMax);
  GetTagData(new_tag)->object_type = internal_strdup(object_type);
  char header[127] = {0};
  internal_snprintf(header, sizeof(header), "race on %s", object_type);
  GetTagData(new_tag)->header = internal_strdup(header);
  return (void *)new_tag;
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_external_register_header(void *tag, const char *header) {
  CHECK_GE((uptr)tag, kExternalTagFirstUserAvailable);
  CHECK_LT((uptr)tag, kExternalTagMax);
  atomic_uintptr_t *header_ptr =
      (atomic_uintptr_t *)&GetTagData((uptr)tag)->header;
  header = internal_strdup(header);
  char *old_header =
      (char *)atomic_exchange(header_ptr, (uptr)header, memory_order_seq_cst);
  if (old_header) internal_free(old_header);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_external_assign_tag(void *addr, void *tag) {
  CHECK_LT(tag, atomic_load(&used_tags, memory_order_relaxed));
  Allocator *a = allocator();
  MBlock *b = nullptr;
  if (a->PointerIsMine((void *)addr)) {
    void *block_begin = a->GetBlockBegin((void *)addr);
    if (block_begin) b = ctx->metamap.GetBlock((uptr)block_begin);
  }
  if (b) {
    b->tag = (uptr)tag;
  }
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_external_read(void *addr, void *caller_pc, void *tag) {
  ExternalAccess(addr, caller_pc, tag, MemoryRead);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_external_write(void *addr, void *caller_pc, void *tag) {
  ExternalAccess(addr, caller_pc, tag, MemoryWrite);
}
}  // extern "C"

#endif  // !SANITIZER_GO

}  // namespace __tsan
