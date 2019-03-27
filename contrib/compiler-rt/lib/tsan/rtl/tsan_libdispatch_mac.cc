//===-- tsan_libdispatch_mac.cc -------------------------------------------===//
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
// Mac-specific libdispatch (GCD) support.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_MAC

#include "sanitizer_common/sanitizer_common.h"
#include "interception/interception.h"
#include "tsan_interceptors.h"
#include "tsan_platform.h"
#include "tsan_rtl.h"

#include <Block.h>
#include <dispatch/dispatch.h>
#include <pthread.h>

// DISPATCH_NOESCAPE is not defined prior to XCode 8.
#ifndef DISPATCH_NOESCAPE
#define DISPATCH_NOESCAPE
#endif

typedef long long_t;  // NOLINT

namespace __tsan {

typedef struct {
  dispatch_queue_t queue;
  void *orig_context;
  dispatch_function_t orig_work;
  bool free_context_in_callback;
  bool submitted_synchronously;
  bool is_barrier_block;
  uptr non_queue_sync_object;
} tsan_block_context_t;

// The offsets of different fields of the dispatch_queue_t structure, exported
// by libdispatch.dylib.
extern "C" struct dispatch_queue_offsets_s {
  const uint16_t dqo_version;
  const uint16_t dqo_label;
  const uint16_t dqo_label_size;
  const uint16_t dqo_flags;
  const uint16_t dqo_flags_size;
  const uint16_t dqo_serialnum;
  const uint16_t dqo_serialnum_size;
  const uint16_t dqo_width;
  const uint16_t dqo_width_size;
  const uint16_t dqo_running;
  const uint16_t dqo_running_size;
  const uint16_t dqo_suspend_cnt;
  const uint16_t dqo_suspend_cnt_size;
  const uint16_t dqo_target_queue;
  const uint16_t dqo_target_queue_size;
  const uint16_t dqo_priority;
  const uint16_t dqo_priority_size;
} dispatch_queue_offsets;

static bool IsQueueSerial(dispatch_queue_t q) {
  CHECK_EQ(dispatch_queue_offsets.dqo_width_size, 2);
  uptr width = *(uint16_t *)(((uptr)q) + dispatch_queue_offsets.dqo_width);
  CHECK_NE(width, 0);
  return width == 1;
}

static dispatch_queue_t GetTargetQueueFromQueue(dispatch_queue_t q) {
  CHECK_EQ(dispatch_queue_offsets.dqo_target_queue_size, 8);
  dispatch_queue_t tq = *(
      dispatch_queue_t *)(((uptr)q) + dispatch_queue_offsets.dqo_target_queue);
  return tq;
}

static dispatch_queue_t GetTargetQueueFromSource(dispatch_source_t source) {
  dispatch_queue_t tq = GetTargetQueueFromQueue((dispatch_queue_t)source);
  CHECK_NE(tq, 0);
  return tq;
}

static tsan_block_context_t *AllocContext(ThreadState *thr, uptr pc,
                                          dispatch_queue_t queue,
                                          void *orig_context,
                                          dispatch_function_t orig_work) {
  tsan_block_context_t *new_context =
      (tsan_block_context_t *)user_alloc_internal(thr, pc,
                                                  sizeof(tsan_block_context_t));
  new_context->queue = queue;
  new_context->orig_context = orig_context;
  new_context->orig_work = orig_work;
  new_context->free_context_in_callback = true;
  new_context->submitted_synchronously = false;
  new_context->is_barrier_block = false;
  new_context->non_queue_sync_object = 0;
  return new_context;
}

#define GET_QUEUE_SYNC_VARS(context, q)                                  \
  bool is_queue_serial = q && IsQueueSerial(q);                          \
  uptr sync_ptr = (uptr)q ?: context->non_queue_sync_object;             \
  uptr serial_sync = (uptr)sync_ptr;                                     \
  uptr concurrent_sync = sync_ptr ? ((uptr)sync_ptr) + sizeof(uptr) : 0; \
  bool serial_task = context->is_barrier_block || is_queue_serial

static void dispatch_sync_pre_execute(ThreadState *thr, uptr pc,
                                      tsan_block_context_t *context) {
  uptr submit_sync = (uptr)context;
  Acquire(thr, pc, submit_sync);

  dispatch_queue_t q = context->queue;
  do {
    GET_QUEUE_SYNC_VARS(context, q);
    if (serial_sync) Acquire(thr, pc, serial_sync);
    if (serial_task && concurrent_sync) Acquire(thr, pc, concurrent_sync);

    if (q) q = GetTargetQueueFromQueue(q);
  } while (q);
}

static void dispatch_sync_post_execute(ThreadState *thr, uptr pc,
                                       tsan_block_context_t *context) {
  uptr submit_sync = (uptr)context;
  if (context->submitted_synchronously) Release(thr, pc, submit_sync);

  dispatch_queue_t q = context->queue;
  do {
    GET_QUEUE_SYNC_VARS(context, q);
    if (serial_task && serial_sync) Release(thr, pc, serial_sync);
    if (!serial_task && concurrent_sync) Release(thr, pc, concurrent_sync);

    if (q) q = GetTargetQueueFromQueue(q);
  } while (q);
}

static void dispatch_callback_wrap(void *param) {
  SCOPED_INTERCEPTOR_RAW(dispatch_callback_wrap);
  tsan_block_context_t *context = (tsan_block_context_t *)param;

  dispatch_sync_pre_execute(thr, pc, context);

  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();
  context->orig_work(context->orig_context);
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();

  dispatch_sync_post_execute(thr, pc, context);

  if (context->free_context_in_callback) user_free(thr, pc, context);
}

static void invoke_block(void *param) {
  dispatch_block_t block = (dispatch_block_t)param;
  block();
}

static void invoke_and_release_block(void *param) {
  dispatch_block_t block = (dispatch_block_t)param;
  block();
  Block_release(block);
}

#define DISPATCH_INTERCEPT_B(name, barrier)                                  \
  TSAN_INTERCEPTOR(void, name, dispatch_queue_t q, dispatch_block_t block) { \
    SCOPED_TSAN_INTERCEPTOR(name, q, block);                                 \
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();                           \
    dispatch_block_t heap_block = Block_copy(block);                         \
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();                             \
    tsan_block_context_t *new_context =                                      \
        AllocContext(thr, pc, q, heap_block, &invoke_and_release_block);     \
    new_context->is_barrier_block = barrier;                                 \
    Release(thr, pc, (uptr)new_context);                                     \
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();                           \
    REAL(name##_f)(q, new_context, dispatch_callback_wrap);                  \
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();                             \
  }

#define DISPATCH_INTERCEPT_SYNC_B(name, barrier)                             \
  TSAN_INTERCEPTOR(void, name, dispatch_queue_t q,                           \
                   DISPATCH_NOESCAPE dispatch_block_t block) {               \
    SCOPED_TSAN_INTERCEPTOR(name, q, block);                                 \
    tsan_block_context_t new_context = {                                     \
        q, block, &invoke_block, false, true, barrier, 0};                   \
    Release(thr, pc, (uptr)&new_context);                                    \
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();                           \
    REAL(name##_f)(q, &new_context, dispatch_callback_wrap);                 \
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();                             \
    Acquire(thr, pc, (uptr)&new_context);                                    \
  }

#define DISPATCH_INTERCEPT_F(name, barrier)                       \
  TSAN_INTERCEPTOR(void, name, dispatch_queue_t q, void *context, \
                   dispatch_function_t work) {                    \
    SCOPED_TSAN_INTERCEPTOR(name, q, context, work);              \
    tsan_block_context_t *new_context =                           \
        AllocContext(thr, pc, q, context, work);                  \
    new_context->is_barrier_block = barrier;                      \
    Release(thr, pc, (uptr)new_context);                          \
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();                \
    REAL(name)(q, new_context, dispatch_callback_wrap);           \
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();                  \
  }

#define DISPATCH_INTERCEPT_SYNC_F(name, barrier)                              \
  TSAN_INTERCEPTOR(void, name, dispatch_queue_t q, void *context,             \
                   dispatch_function_t work) {                                \
    SCOPED_TSAN_INTERCEPTOR(name, q, context, work);                          \
    tsan_block_context_t new_context = {                                      \
        q, context, work, false, true, barrier, 0};                           \
    Release(thr, pc, (uptr)&new_context);                                     \
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();                            \
    REAL(name)(q, &new_context, dispatch_callback_wrap);                      \
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();                              \
    Acquire(thr, pc, (uptr)&new_context);                                     \
  }

// We wrap dispatch_async, dispatch_sync and friends where we allocate a new
// context, which is used to synchronize (we release the context before
// submitting, and the callback acquires it before executing the original
// callback).
DISPATCH_INTERCEPT_B(dispatch_async, false)
DISPATCH_INTERCEPT_B(dispatch_barrier_async, true)
DISPATCH_INTERCEPT_F(dispatch_async_f, false)
DISPATCH_INTERCEPT_F(dispatch_barrier_async_f, true)
DISPATCH_INTERCEPT_SYNC_B(dispatch_sync, false)
DISPATCH_INTERCEPT_SYNC_B(dispatch_barrier_sync, true)
DISPATCH_INTERCEPT_SYNC_F(dispatch_sync_f, false)
DISPATCH_INTERCEPT_SYNC_F(dispatch_barrier_sync_f, true)

TSAN_INTERCEPTOR(void, dispatch_after, dispatch_time_t when,
                 dispatch_queue_t queue, dispatch_block_t block) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_after, when, queue, block);
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();
  dispatch_block_t heap_block = Block_copy(block);
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();
  tsan_block_context_t *new_context =
      AllocContext(thr, pc, queue, heap_block, &invoke_and_release_block);
  Release(thr, pc, (uptr)new_context);
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();
  REAL(dispatch_after_f)(when, queue, new_context, dispatch_callback_wrap);
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();
}

TSAN_INTERCEPTOR(void, dispatch_after_f, dispatch_time_t when,
                 dispatch_queue_t queue, void *context,
                 dispatch_function_t work) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_after_f, when, queue, context, work);
  WRAP(dispatch_after)(when, queue, ^(void) {
    work(context);
  });
}

// GCD's dispatch_once implementation has a fast path that contains a racy read
// and it's inlined into user's code. Furthermore, this fast path doesn't
// establish a proper happens-before relations between the initialization and
// code following the call to dispatch_once. We could deal with this in
// instrumented code, but there's not much we can do about it in system
// libraries. Let's disable the fast path (by never storing the value ~0 to
// predicate), so the interceptor is always called, and let's add proper release
// and acquire semantics. Since TSan does not see its own atomic stores, the
// race on predicate won't be reported - the only accesses to it that TSan sees
// are the loads on the fast path. Loads don't race. Secondly, dispatch_once is
// both a macro and a real function, we want to intercept the function, so we
// need to undefine the macro.
#undef dispatch_once
TSAN_INTERCEPTOR(void, dispatch_once, dispatch_once_t *predicate,
                 DISPATCH_NOESCAPE dispatch_block_t block) {
  SCOPED_INTERCEPTOR_RAW(dispatch_once, predicate, block);
  atomic_uint32_t *a = reinterpret_cast<atomic_uint32_t *>(predicate);
  u32 v = atomic_load(a, memory_order_acquire);
  if (v == 0 &&
      atomic_compare_exchange_strong(a, &v, 1, memory_order_relaxed)) {
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();
    block();
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();
    Release(thr, pc, (uptr)a);
    atomic_store(a, 2, memory_order_release);
  } else {
    while (v != 2) {
      internal_sched_yield();
      v = atomic_load(a, memory_order_acquire);
    }
    Acquire(thr, pc, (uptr)a);
  }
}

#undef dispatch_once_f
TSAN_INTERCEPTOR(void, dispatch_once_f, dispatch_once_t *predicate,
                 void *context, dispatch_function_t function) {
  SCOPED_INTERCEPTOR_RAW(dispatch_once_f, predicate, context, function);
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();
  WRAP(dispatch_once)(predicate, ^(void) {
    function(context);
  });
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();
}

TSAN_INTERCEPTOR(long_t, dispatch_semaphore_signal,
                 dispatch_semaphore_t dsema) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_semaphore_signal, dsema);
  Release(thr, pc, (uptr)dsema);
  return REAL(dispatch_semaphore_signal)(dsema);
}

TSAN_INTERCEPTOR(long_t, dispatch_semaphore_wait, dispatch_semaphore_t dsema,
                 dispatch_time_t timeout) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_semaphore_wait, dsema, timeout);
  long_t result = REAL(dispatch_semaphore_wait)(dsema, timeout);
  if (result == 0) Acquire(thr, pc, (uptr)dsema);
  return result;
}

TSAN_INTERCEPTOR(long_t, dispatch_group_wait, dispatch_group_t group,
                 dispatch_time_t timeout) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_group_wait, group, timeout);
  long_t result = REAL(dispatch_group_wait)(group, timeout);
  if (result == 0) Acquire(thr, pc, (uptr)group);
  return result;
}

TSAN_INTERCEPTOR(void, dispatch_group_leave, dispatch_group_t group) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_group_leave, group);
  // Acquired in the group noticifaction callback in dispatch_group_notify[_f].
  Release(thr, pc, (uptr)group);
  REAL(dispatch_group_leave)(group);
}

TSAN_INTERCEPTOR(void, dispatch_group_async, dispatch_group_t group,
                 dispatch_queue_t queue, dispatch_block_t block) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_group_async, group, queue, block);
  dispatch_retain(group);
  dispatch_group_enter(group);
  __block dispatch_block_t block_copy = (dispatch_block_t)_Block_copy(block);
  WRAP(dispatch_async)(queue, ^(void) {
    block_copy();
    _Block_release(block_copy);
    WRAP(dispatch_group_leave)(group);
    dispatch_release(group);
  });
}

TSAN_INTERCEPTOR(void, dispatch_group_async_f, dispatch_group_t group,
                 dispatch_queue_t queue, void *context,
                 dispatch_function_t work) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_group_async_f, group, queue, context, work);
  dispatch_retain(group);
  dispatch_group_enter(group);
  WRAP(dispatch_async)(queue, ^(void) {
    work(context);
    WRAP(dispatch_group_leave)(group);
    dispatch_release(group);
  });
}

TSAN_INTERCEPTOR(void, dispatch_group_notify, dispatch_group_t group,
                 dispatch_queue_t q, dispatch_block_t block) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_group_notify, group, q, block);

  // To make sure the group is still available in the callback (otherwise
  // it can be already destroyed).  Will be released in the callback.
  dispatch_retain(group);

  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();
  dispatch_block_t heap_block = Block_copy(^(void) {
    {
      SCOPED_INTERCEPTOR_RAW(dispatch_read_callback);
      // Released when leaving the group (dispatch_group_leave).
      Acquire(thr, pc, (uptr)group);
    }
    dispatch_release(group);
    block();
  });
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();
  tsan_block_context_t *new_context =
      AllocContext(thr, pc, q, heap_block, &invoke_and_release_block);
  new_context->is_barrier_block = true;
  Release(thr, pc, (uptr)new_context);
  REAL(dispatch_group_notify_f)(group, q, new_context, dispatch_callback_wrap);
}

TSAN_INTERCEPTOR(void, dispatch_group_notify_f, dispatch_group_t group,
                 dispatch_queue_t q, void *context, dispatch_function_t work) {
  WRAP(dispatch_group_notify)(group, q, ^(void) { work(context); });
}

TSAN_INTERCEPTOR(void, dispatch_source_set_event_handler,
                 dispatch_source_t source, dispatch_block_t handler) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_source_set_event_handler, source, handler);
  if (handler == nullptr)
    return REAL(dispatch_source_set_event_handler)(source, nullptr);
  dispatch_queue_t q = GetTargetQueueFromSource(source);
  __block tsan_block_context_t new_context = {
      q, handler, &invoke_block, false, false, false, 0 };
  dispatch_block_t new_handler = Block_copy(^(void) {
    new_context.orig_context = handler;  // To explicitly capture "handler".
    dispatch_callback_wrap(&new_context);
  });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  REAL(dispatch_source_set_event_handler)(source, new_handler);
  Block_release(new_handler);
}

TSAN_INTERCEPTOR(void, dispatch_source_set_event_handler_f,
                 dispatch_source_t source, dispatch_function_t handler) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_source_set_event_handler_f, source, handler);
  if (handler == nullptr)
    return REAL(dispatch_source_set_event_handler)(source, nullptr);
  dispatch_block_t block = ^(void) {
    handler(dispatch_get_context(source));
  };
  WRAP(dispatch_source_set_event_handler)(source, block);
}

TSAN_INTERCEPTOR(void, dispatch_source_set_cancel_handler,
                 dispatch_source_t source, dispatch_block_t handler) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_source_set_cancel_handler, source, handler);
  if (handler == nullptr)
    return REAL(dispatch_source_set_cancel_handler)(source, nullptr);
  dispatch_queue_t q = GetTargetQueueFromSource(source);
  __block tsan_block_context_t new_context = {
      q, handler, &invoke_block, false, false, false, 0};
  dispatch_block_t new_handler = Block_copy(^(void) {
    new_context.orig_context = handler;  // To explicitly capture "handler".
    dispatch_callback_wrap(&new_context);
  });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  REAL(dispatch_source_set_cancel_handler)(source, new_handler);
  Block_release(new_handler);
}

TSAN_INTERCEPTOR(void, dispatch_source_set_cancel_handler_f,
                 dispatch_source_t source, dispatch_function_t handler) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_source_set_cancel_handler_f, source,
                          handler);
  if (handler == nullptr)
    return REAL(dispatch_source_set_cancel_handler)(source, nullptr);
  dispatch_block_t block = ^(void) {
    handler(dispatch_get_context(source));
  };
  WRAP(dispatch_source_set_cancel_handler)(source, block);
}

TSAN_INTERCEPTOR(void, dispatch_source_set_registration_handler,
                 dispatch_source_t source, dispatch_block_t handler) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_source_set_registration_handler, source,
                          handler);
  if (handler == nullptr)
    return REAL(dispatch_source_set_registration_handler)(source, nullptr);
  dispatch_queue_t q = GetTargetQueueFromSource(source);
  __block tsan_block_context_t new_context = {
      q, handler, &invoke_block, false, false, false, 0};
  dispatch_block_t new_handler = Block_copy(^(void) {
    new_context.orig_context = handler;  // To explicitly capture "handler".
    dispatch_callback_wrap(&new_context);
  });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  REAL(dispatch_source_set_registration_handler)(source, new_handler);
  Block_release(new_handler);
}

TSAN_INTERCEPTOR(void, dispatch_source_set_registration_handler_f,
                 dispatch_source_t source, dispatch_function_t handler) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_source_set_registration_handler_f, source,
                          handler);
  if (handler == nullptr)
    return REAL(dispatch_source_set_registration_handler)(source, nullptr);
  dispatch_block_t block = ^(void) {
    handler(dispatch_get_context(source));
  };
  WRAP(dispatch_source_set_registration_handler)(source, block);
}

TSAN_INTERCEPTOR(void, dispatch_apply, size_t iterations,
                 dispatch_queue_t queue,
                 DISPATCH_NOESCAPE void (^block)(size_t)) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_apply, iterations, queue, block);

  void *parent_to_child_sync = nullptr;
  uptr parent_to_child_sync_uptr = (uptr)&parent_to_child_sync;
  void *child_to_parent_sync = nullptr;
  uptr child_to_parent_sync_uptr = (uptr)&child_to_parent_sync;

  Release(thr, pc, parent_to_child_sync_uptr);
  void (^new_block)(size_t) = ^(size_t iteration) {
    SCOPED_INTERCEPTOR_RAW(dispatch_apply);
    Acquire(thr, pc, parent_to_child_sync_uptr);
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();
    block(iteration);
    SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();
    Release(thr, pc, child_to_parent_sync_uptr);
  };
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();
  REAL(dispatch_apply)(iterations, queue, new_block);
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();
  Acquire(thr, pc, child_to_parent_sync_uptr);
}

TSAN_INTERCEPTOR(void, dispatch_apply_f, size_t iterations,
                 dispatch_queue_t queue, void *context,
                 void (*work)(void *, size_t)) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_apply_f, iterations, queue, context, work);
  void (^new_block)(size_t) = ^(size_t iteration) {
    work(context, iteration);
  };
  WRAP(dispatch_apply)(iterations, queue, new_block);
}

DECLARE_REAL_AND_INTERCEPTOR(void, free, void *ptr)
DECLARE_REAL_AND_INTERCEPTOR(int, munmap, void *addr, long_t sz)

TSAN_INTERCEPTOR(dispatch_data_t, dispatch_data_create, const void *buffer,
                 size_t size, dispatch_queue_t q, dispatch_block_t destructor) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_data_create, buffer, size, q, destructor);
  if ((q == nullptr) || (destructor == DISPATCH_DATA_DESTRUCTOR_DEFAULT))
    return REAL(dispatch_data_create)(buffer, size, q, destructor);

  if (destructor == DISPATCH_DATA_DESTRUCTOR_FREE)
    destructor = ^(void) { WRAP(free)((void *)(uintptr_t)buffer); };
  else if (destructor == DISPATCH_DATA_DESTRUCTOR_MUNMAP)
    destructor = ^(void) { WRAP(munmap)((void *)(uintptr_t)buffer, size); };

  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START();
  dispatch_block_t heap_block = Block_copy(destructor);
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END();
  tsan_block_context_t *new_context =
      AllocContext(thr, pc, q, heap_block, &invoke_and_release_block);
  uptr submit_sync = (uptr)new_context;
  Release(thr, pc, submit_sync);
  return REAL(dispatch_data_create)(buffer, size, q, ^(void) {
    dispatch_callback_wrap(new_context);
  });
}

typedef void (^fd_handler_t)(dispatch_data_t data, int error);
typedef void (^cleanup_handler_t)(int error);

TSAN_INTERCEPTOR(void, dispatch_read, dispatch_fd_t fd, size_t length,
                 dispatch_queue_t q, fd_handler_t h) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_read, fd, length, q, h);
  __block tsan_block_context_t new_context = {
      q, nullptr, &invoke_block, false, false, false, 0};
  fd_handler_t new_h = Block_copy(^(dispatch_data_t data, int error) {
    new_context.orig_context = ^(void) {
      h(data, error);
    };
    dispatch_callback_wrap(&new_context);
  });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  REAL(dispatch_read)(fd, length, q, new_h);
  Block_release(new_h);
}

TSAN_INTERCEPTOR(void, dispatch_write, dispatch_fd_t fd, dispatch_data_t data,
                 dispatch_queue_t q, fd_handler_t h) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_write, fd, data, q, h);
  __block tsan_block_context_t new_context = {
      q, nullptr, &invoke_block, false, false, false, 0};
  fd_handler_t new_h = Block_copy(^(dispatch_data_t data, int error) {
    new_context.orig_context = ^(void) {
      h(data, error);
    };
    dispatch_callback_wrap(&new_context);
  });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  REAL(dispatch_write)(fd, data, q, new_h);
  Block_release(new_h);
}

TSAN_INTERCEPTOR(void, dispatch_io_read, dispatch_io_t channel, off_t offset,
                 size_t length, dispatch_queue_t q, dispatch_io_handler_t h) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_io_read, channel, offset, length, q, h);
  __block tsan_block_context_t new_context = {
      q, nullptr, &invoke_block, false, false, false, 0};
  dispatch_io_handler_t new_h =
      Block_copy(^(bool done, dispatch_data_t data, int error) {
        new_context.orig_context = ^(void) {
          h(done, data, error);
        };
        dispatch_callback_wrap(&new_context);
      });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  REAL(dispatch_io_read)(channel, offset, length, q, new_h);
  Block_release(new_h);
}

TSAN_INTERCEPTOR(void, dispatch_io_write, dispatch_io_t channel, off_t offset,
                 dispatch_data_t data, dispatch_queue_t q,
                 dispatch_io_handler_t h) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_io_write, channel, offset, data, q, h);
  __block tsan_block_context_t new_context = {
      q, nullptr, &invoke_block, false, false, false, 0};
  dispatch_io_handler_t new_h =
      Block_copy(^(bool done, dispatch_data_t data, int error) {
        new_context.orig_context = ^(void) {
          h(done, data, error);
        };
        dispatch_callback_wrap(&new_context);
      });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  REAL(dispatch_io_write)(channel, offset, data, q, new_h);
  Block_release(new_h);
}

TSAN_INTERCEPTOR(void, dispatch_io_barrier, dispatch_io_t channel,
                 dispatch_block_t barrier) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_io_barrier, channel, barrier);
  __block tsan_block_context_t new_context = {
      nullptr, nullptr, &invoke_block, false, false, false, 0};
  new_context.non_queue_sync_object = (uptr)channel;
  new_context.is_barrier_block = true;
  dispatch_block_t new_block = Block_copy(^(void) {
    new_context.orig_context = ^(void) {
      barrier();
    };
    dispatch_callback_wrap(&new_context);
  });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  REAL(dispatch_io_barrier)(channel, new_block);
  Block_release(new_block);
}

TSAN_INTERCEPTOR(dispatch_io_t, dispatch_io_create, dispatch_io_type_t type,
                 dispatch_fd_t fd, dispatch_queue_t q, cleanup_handler_t h) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_io_create, type, fd, q, h);
  __block dispatch_io_t new_channel = nullptr;
  __block tsan_block_context_t new_context = {
      q, nullptr, &invoke_block, false, false, false, 0};
  cleanup_handler_t new_h = Block_copy(^(int error) {
    {
      SCOPED_INTERCEPTOR_RAW(dispatch_io_create_callback);
      Acquire(thr, pc, (uptr)new_channel);  // Release() in dispatch_io_close.
    }
    new_context.orig_context = ^(void) {
      h(error);
    };
    dispatch_callback_wrap(&new_context);
  });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  new_channel = REAL(dispatch_io_create)(type, fd, q, new_h);
  Block_release(new_h);
  return new_channel;
}

TSAN_INTERCEPTOR(dispatch_io_t, dispatch_io_create_with_path,
                 dispatch_io_type_t type, const char *path, int oflag,
                 mode_t mode, dispatch_queue_t q, cleanup_handler_t h) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_io_create_with_path, type, path, oflag, mode,
                          q, h);
  __block dispatch_io_t new_channel = nullptr;
  __block tsan_block_context_t new_context = {
      q, nullptr, &invoke_block, false, false, false, 0};
  cleanup_handler_t new_h = Block_copy(^(int error) {
    {
      SCOPED_INTERCEPTOR_RAW(dispatch_io_create_callback);
      Acquire(thr, pc, (uptr)new_channel);  // Release() in dispatch_io_close.
    }
    new_context.orig_context = ^(void) {
      h(error);
    };
    dispatch_callback_wrap(&new_context);
  });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  new_channel =
      REAL(dispatch_io_create_with_path)(type, path, oflag, mode, q, new_h);
  Block_release(new_h);
  return new_channel;
}

TSAN_INTERCEPTOR(dispatch_io_t, dispatch_io_create_with_io,
                 dispatch_io_type_t type, dispatch_io_t io, dispatch_queue_t q,
                 cleanup_handler_t h) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_io_create_with_io, type, io, q, h);
  __block dispatch_io_t new_channel = nullptr;
  __block tsan_block_context_t new_context = {
      q, nullptr, &invoke_block, false, false, false, 0};
  cleanup_handler_t new_h = Block_copy(^(int error) {
    {
      SCOPED_INTERCEPTOR_RAW(dispatch_io_create_callback);
      Acquire(thr, pc, (uptr)new_channel);  // Release() in dispatch_io_close.
    }
    new_context.orig_context = ^(void) {
      h(error);
    };
    dispatch_callback_wrap(&new_context);
  });
  uptr submit_sync = (uptr)&new_context;
  Release(thr, pc, submit_sync);
  new_channel = REAL(dispatch_io_create_with_io)(type, io, q, new_h);
  Block_release(new_h);
  return new_channel;
}

TSAN_INTERCEPTOR(void, dispatch_io_close, dispatch_io_t channel,
                 dispatch_io_close_flags_t flags) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_io_close, channel, flags);
  Release(thr, pc, (uptr)channel);  // Acquire() in dispatch_io_create[_*].
  return REAL(dispatch_io_close)(channel, flags);
}

// Resuming a suspended queue needs to synchronize with all subsequent
// executions of blocks in that queue.
TSAN_INTERCEPTOR(void, dispatch_resume, dispatch_object_t o) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_resume, o);
  Release(thr, pc, (uptr)o);  // Synchronizes with the Acquire() on serial_sync
                              // in dispatch_sync_pre_execute
  return REAL(dispatch_resume)(o);
}

}  // namespace __tsan

#endif  // SANITIZER_MAC
