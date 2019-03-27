/*
 * kmp_taskq.cpp -- TASKQ support for OpenMP.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp.h"
#include "kmp_error.h"
#include "kmp_i18n.h"
#include "kmp_io.h"

#define MAX_MESSAGE 512

/* Taskq routines and global variables */

#define KMP_DEBUG_REF_CTS(x) KF_TRACE(1, x);

#define THREAD_ALLOC_FOR_TASKQ

static int in_parallel_context(kmp_team_t *team) {
  return !team->t.t_serialized;
}

static void __kmp_taskq_eo(int *gtid_ref, int *cid_ref, ident_t *loc_ref) {
  int gtid = *gtid_ref;
  int tid = __kmp_tid_from_gtid(gtid);
  kmp_uint32 my_token;
  kmpc_task_queue_t *taskq;
  kmp_taskq_t *tq = &__kmp_threads[gtid]->th.th_team->t.t_taskq;

  if (__kmp_env_consistency_check)
#if KMP_USE_DYNAMIC_LOCK
    __kmp_push_sync(gtid, ct_ordered_in_taskq, loc_ref, NULL, 0);
#else
    __kmp_push_sync(gtid, ct_ordered_in_taskq, loc_ref, NULL);
#endif

  if (!__kmp_threads[gtid]->th.th_team->t.t_serialized) {
    KMP_MB(); /* Flush all pending memory write invalidates.  */

    /* GEH - need check here under stats to make sure   */
    /*       inside task (curr_thunk[*tid_ref] != NULL) */

    my_token = tq->tq_curr_thunk[tid]->th_tasknum;

    taskq = tq->tq_curr_thunk[tid]->th.th_shareds->sv_queue;

    KMP_WAIT_YIELD(&taskq->tq_tasknum_serving, my_token, KMP_EQ, NULL);
    KMP_MB();
  }
}

static void __kmp_taskq_xo(int *gtid_ref, int *cid_ref, ident_t *loc_ref) {
  int gtid = *gtid_ref;
  int tid = __kmp_tid_from_gtid(gtid);
  kmp_uint32 my_token;
  kmp_taskq_t *tq = &__kmp_threads[gtid]->th.th_team->t.t_taskq;

  if (__kmp_env_consistency_check)
    __kmp_pop_sync(gtid, ct_ordered_in_taskq, loc_ref);

  if (!__kmp_threads[gtid]->th.th_team->t.t_serialized) {
    KMP_MB(); /* Flush all pending memory write invalidates.  */

    /* GEH - need check here under stats to make sure */
    /*       inside task (curr_thunk[tid] != NULL)    */

    my_token = tq->tq_curr_thunk[tid]->th_tasknum;

    KMP_MB(); /* Flush all pending memory write invalidates.  */

    tq->tq_curr_thunk[tid]->th.th_shareds->sv_queue->tq_tasknum_serving =
        my_token + 1;

    KMP_MB(); /* Flush all pending memory write invalidates.  */
  }
}

static void __kmp_taskq_check_ordered(kmp_int32 gtid, kmpc_thunk_t *thunk) {
  kmp_uint32 my_token;
  kmpc_task_queue_t *taskq;

  /* assume we are always called from an active parallel context */

  KMP_MB(); /* Flush all pending memory write invalidates.  */

  my_token = thunk->th_tasknum;

  taskq = thunk->th.th_shareds->sv_queue;

  if (taskq->tq_tasknum_serving <= my_token) {
    KMP_WAIT_YIELD(&taskq->tq_tasknum_serving, my_token, KMP_GE, NULL);
    KMP_MB();
    taskq->tq_tasknum_serving = my_token + 1;
    KMP_MB();
  }
}

#ifdef KMP_DEBUG

static void __kmp_dump_TQF(kmp_int32 flags) {
  if (flags & TQF_IS_ORDERED)
    __kmp_printf("ORDERED ");
  if (flags & TQF_IS_LASTPRIVATE)
    __kmp_printf("LAST_PRIV ");
  if (flags & TQF_IS_NOWAIT)
    __kmp_printf("NOWAIT ");
  if (flags & TQF_HEURISTICS)
    __kmp_printf("HEURIST ");
  if (flags & TQF_INTERFACE_RESERVED1)
    __kmp_printf("RESERV1 ");
  if (flags & TQF_INTERFACE_RESERVED2)
    __kmp_printf("RESERV2 ");
  if (flags & TQF_INTERFACE_RESERVED3)
    __kmp_printf("RESERV3 ");
  if (flags & TQF_INTERFACE_RESERVED4)
    __kmp_printf("RESERV4 ");
  if (flags & TQF_IS_LAST_TASK)
    __kmp_printf("LAST_TASK ");
  if (flags & TQF_TASKQ_TASK)
    __kmp_printf("TASKQ_TASK ");
  if (flags & TQF_RELEASE_WORKERS)
    __kmp_printf("RELEASE ");
  if (flags & TQF_ALL_TASKS_QUEUED)
    __kmp_printf("ALL_QUEUED ");
  if (flags & TQF_PARALLEL_CONTEXT)
    __kmp_printf("PARALLEL ");
  if (flags & TQF_DEALLOCATED)
    __kmp_printf("DEALLOC ");
  if (!(flags & (TQF_INTERNAL_FLAGS | TQF_INTERFACE_FLAGS)))
    __kmp_printf("(NONE)");
}

static void __kmp_dump_thunk(kmp_taskq_t *tq, kmpc_thunk_t *thunk,
                             kmp_int32 global_tid) {
  int i;
  int nproc = __kmp_threads[global_tid]->th.th_team->t.t_nproc;

  __kmp_printf("\tThunk at %p on (%d):  ", thunk, global_tid);

  if (thunk != NULL) {
    for (i = 0; i < nproc; i++) {
      if (tq->tq_curr_thunk[i] == thunk) {
        __kmp_printf("[%i] ", i);
      }
    }
    __kmp_printf("th_shareds=%p, ", thunk->th.th_shareds);
    __kmp_printf("th_task=%p, ", thunk->th_task);
    __kmp_printf("th_encl_thunk=%p, ", thunk->th_encl_thunk);
    __kmp_printf("th_status=%d, ", thunk->th_status);
    __kmp_printf("th_tasknum=%u, ", thunk->th_tasknum);
    __kmp_printf("th_flags=");
    __kmp_dump_TQF(thunk->th_flags);
  }

  __kmp_printf("\n");
}

static void __kmp_dump_thunk_stack(kmpc_thunk_t *thunk, kmp_int32 thread_num) {
  kmpc_thunk_t *th;

  __kmp_printf("    Thunk stack for T#%d:  ", thread_num);

  for (th = thunk; th != NULL; th = th->th_encl_thunk)
    __kmp_printf("%p ", th);

  __kmp_printf("\n");
}

static void __kmp_dump_task_queue(kmp_taskq_t *tq, kmpc_task_queue_t *queue,
                                  kmp_int32 global_tid) {
  int qs, count, i;
  kmpc_thunk_t *thunk;
  kmpc_task_queue_t *taskq;

  __kmp_printf("Task Queue at %p on (%d):\n", queue, global_tid);

  if (queue != NULL) {
    int in_parallel = queue->tq_flags & TQF_PARALLEL_CONTEXT;

    if (__kmp_env_consistency_check) {
      __kmp_printf("    tq_loc             : ");
    }
    if (in_parallel) {

      // if (queue->tq.tq_parent != 0)
      //__kmp_acquire_lock(& queue->tq.tq_parent->tq_link_lck, global_tid);

      //__kmp_acquire_lock(& queue->tq_link_lck, global_tid);

      // Make sure data structures are in consistent state before querying them
      // Seems to work without this for digital/alpha, needed for IBM/RS6000
      KMP_MB();

      __kmp_printf("    tq_parent          : %p\n", queue->tq.tq_parent);
      __kmp_printf("    tq_first_child     : %p\n", queue->tq_first_child);
      __kmp_printf("    tq_next_child      : %p\n", queue->tq_next_child);
      __kmp_printf("    tq_prev_child      : %p\n", queue->tq_prev_child);
      __kmp_printf("    tq_ref_count       : %d\n", queue->tq_ref_count);

      //__kmp_release_lock(& queue->tq_link_lck, global_tid);

      // if (queue->tq.tq_parent != 0)
      //__kmp_release_lock(& queue->tq.tq_parent->tq_link_lck, global_tid);

      //__kmp_acquire_lock(& queue->tq_free_thunks_lck, global_tid);
      //__kmp_acquire_lock(& queue->tq_queue_lck, global_tid);

      // Make sure data structures are in consistent state before querying them
      // Seems to work without this for digital/alpha, needed for IBM/RS6000
      KMP_MB();
    }

    __kmp_printf("    tq_shareds         : ");
    for (i = 0; i < ((queue == tq->tq_root) ? queue->tq_nproc : 1); i++)
      __kmp_printf("%p ", queue->tq_shareds[i].ai_data);
    __kmp_printf("\n");

    if (in_parallel) {
      __kmp_printf("    tq_tasknum_queuing : %u\n", queue->tq_tasknum_queuing);
      __kmp_printf("    tq_tasknum_serving : %u\n", queue->tq_tasknum_serving);
    }

    __kmp_printf("    tq_queue           : %p\n", queue->tq_queue);
    __kmp_printf("    tq_thunk_space     : %p\n", queue->tq_thunk_space);
    __kmp_printf("    tq_taskq_slot      : %p\n", queue->tq_taskq_slot);

    __kmp_printf("    tq_free_thunks     : ");
    for (thunk = queue->tq_free_thunks; thunk != NULL;
         thunk = thunk->th.th_next_free)
      __kmp_printf("%p ", thunk);
    __kmp_printf("\n");

    __kmp_printf("    tq_nslots          : %d\n", queue->tq_nslots);
    __kmp_printf("    tq_head            : %d\n", queue->tq_head);
    __kmp_printf("    tq_tail            : %d\n", queue->tq_tail);
    __kmp_printf("    tq_nfull           : %d\n", queue->tq_nfull);
    __kmp_printf("    tq_hiwat           : %d\n", queue->tq_hiwat);
    __kmp_printf("    tq_flags           : ");
    __kmp_dump_TQF(queue->tq_flags);
    __kmp_printf("\n");

    if (in_parallel) {
      __kmp_printf("    tq_th_thunks       : ");
      for (i = 0; i < queue->tq_nproc; i++) {
        __kmp_printf("%d ", queue->tq_th_thunks[i].ai_data);
      }
      __kmp_printf("\n");
    }

    __kmp_printf("\n");
    __kmp_printf("    Queue slots:\n");

    qs = queue->tq_tail;
    for (count = 0; count < queue->tq_nfull; ++count) {
      __kmp_printf("(%d)", qs);
      __kmp_dump_thunk(tq, queue->tq_queue[qs].qs_thunk, global_tid);
      qs = (qs + 1) % queue->tq_nslots;
    }

    __kmp_printf("\n");

    if (in_parallel) {
      if (queue->tq_taskq_slot != NULL) {
        __kmp_printf("    TaskQ slot:\n");
        __kmp_dump_thunk(tq, CCAST(kmpc_thunk_t *, queue->tq_taskq_slot),
                         global_tid);
        __kmp_printf("\n");
      }
      //__kmp_release_lock(& queue->tq_queue_lck, global_tid);
      //__kmp_release_lock(& queue->tq_free_thunks_lck, global_tid);
    }
  }

  __kmp_printf("    Taskq freelist: ");

  //__kmp_acquire_lock( & tq->tq_freelist_lck, global_tid );

  // Make sure data structures are in consistent state before querying them
  // Seems to work without this call for digital/alpha, needed for IBM/RS6000
  KMP_MB();

  for (taskq = tq->tq_freelist; taskq != NULL; taskq = taskq->tq.tq_next_free)
    __kmp_printf("%p ", taskq);

  //__kmp_release_lock( & tq->tq_freelist_lck, global_tid );

  __kmp_printf("\n\n");
}

static void __kmp_aux_dump_task_queue_tree(kmp_taskq_t *tq,
                                           kmpc_task_queue_t *curr_queue,
                                           kmp_int32 level,
                                           kmp_int32 global_tid) {
  int i, count, qs;
  int nproc = __kmp_threads[global_tid]->th.th_team->t.t_nproc;
  kmpc_task_queue_t *queue = curr_queue;

  if (curr_queue == NULL)
    return;

  __kmp_printf("    ");

  for (i = 0; i < level; i++)
    __kmp_printf("  ");

  __kmp_printf("%p", curr_queue);

  for (i = 0; i < nproc; i++) {
    if (tq->tq_curr_thunk[i] &&
        tq->tq_curr_thunk[i]->th.th_shareds->sv_queue == curr_queue) {
      __kmp_printf(" [%i]", i);
    }
  }

  __kmp_printf(":");

  //__kmp_acquire_lock(& curr_queue->tq_queue_lck, global_tid);

  // Make sure data structures are in consistent state before querying them
  // Seems to work without this call for digital/alpha, needed for IBM/RS6000
  KMP_MB();

  qs = curr_queue->tq_tail;

  for (count = 0; count < curr_queue->tq_nfull; ++count) {
    __kmp_printf("%p ", curr_queue->tq_queue[qs].qs_thunk);
    qs = (qs + 1) % curr_queue->tq_nslots;
  }

  //__kmp_release_lock(& curr_queue->tq_queue_lck, global_tid);

  __kmp_printf("\n");

  if (curr_queue->tq_first_child) {
    //__kmp_acquire_lock(& curr_queue->tq_link_lck, global_tid);

    // Make sure data structures are in consistent state before querying them
    // Seems to work without this call for digital/alpha, needed for IBM/RS6000
    KMP_MB();

    if (curr_queue->tq_first_child) {
      for (queue = CCAST(kmpc_task_queue_t *, curr_queue->tq_first_child);
           queue != NULL; queue = queue->tq_next_child) {
        __kmp_aux_dump_task_queue_tree(tq, queue, level + 1, global_tid);
      }
    }

    //__kmp_release_lock(& curr_queue->tq_link_lck, global_tid);
  }
}

static void __kmp_dump_task_queue_tree(kmp_taskq_t *tq,
                                       kmpc_task_queue_t *tqroot,
                                       kmp_int32 global_tid) {
  __kmp_printf("TaskQ Tree at root %p on (%d):\n", tqroot, global_tid);

  __kmp_aux_dump_task_queue_tree(tq, tqroot, 0, global_tid);

  __kmp_printf("\n");
}
#endif

/* New taskq storage routines that try to minimize overhead of mallocs but
   still provide cache line alignment. */
static void *__kmp_taskq_allocate(size_t size, kmp_int32 global_tid) {
  void *addr, *orig_addr;
  size_t bytes;

  KB_TRACE(5, ("__kmp_taskq_allocate: called size=%d, gtid=%d\n", (int)size,
               global_tid));

  bytes = sizeof(void *) + CACHE_LINE + size;

#ifdef THREAD_ALLOC_FOR_TASKQ
  orig_addr =
      (void *)__kmp_thread_malloc(__kmp_thread_from_gtid(global_tid), bytes);
#else
  KE_TRACE(10, ("%%%%%% MALLOC( %d )\n", bytes));
  orig_addr = (void *)KMP_INTERNAL_MALLOC(bytes);
#endif /* THREAD_ALLOC_FOR_TASKQ */

  if (orig_addr == 0)
    KMP_FATAL(OutOfHeapMemory);

  addr = orig_addr;

  if (((kmp_uintptr_t)addr & (CACHE_LINE - 1)) != 0) {
    KB_TRACE(50, ("__kmp_taskq_allocate:  adjust for cache alignment\n"));
    addr = (void *)(((kmp_uintptr_t)addr + CACHE_LINE) & ~(CACHE_LINE - 1));
  }

  (*(void **)addr) = orig_addr;

  KB_TRACE(10,
           ("__kmp_taskq_allocate:  allocate: %p, use: %p - %p, size: %d, "
            "gtid: %d\n",
            orig_addr, ((void **)addr) + 1,
            ((char *)(((void **)addr) + 1)) + size - 1, (int)size, global_tid));

  return (((void **)addr) + 1);
}

static void __kmpc_taskq_free(void *p, kmp_int32 global_tid) {
  KB_TRACE(5, ("__kmpc_taskq_free: called addr=%p, gtid=%d\n", p, global_tid));

  KB_TRACE(10, ("__kmpc_taskq_free:  freeing: %p, gtid: %d\n",
                (*(((void **)p) - 1)), global_tid));

#ifdef THREAD_ALLOC_FOR_TASKQ
  __kmp_thread_free(__kmp_thread_from_gtid(global_tid), *(((void **)p) - 1));
#else
  KMP_INTERNAL_FREE(*(((void **)p) - 1));
#endif /* THREAD_ALLOC_FOR_TASKQ */
}

/* Keep freed kmpc_task_queue_t on an internal freelist and recycle since
   they're of constant size. */

static kmpc_task_queue_t *
__kmp_alloc_taskq(kmp_taskq_t *tq, int in_parallel, kmp_int32 nslots,
                  kmp_int32 nthunks, kmp_int32 nshareds, kmp_int32 nproc,
                  size_t sizeof_thunk, size_t sizeof_shareds,
                  kmpc_thunk_t **new_taskq_thunk, kmp_int32 global_tid) {
  kmp_int32 i;
  size_t bytes;
  kmpc_task_queue_t *new_queue;
  kmpc_aligned_shared_vars_t *shared_var_array;
  char *shared_var_storage;
  char *pt; /* for doing byte-adjusted address computations */

  __kmp_acquire_lock(&tq->tq_freelist_lck, global_tid);

  // Make sure data structures are in consistent state before querying them
  // Seems to work without this call for digital/alpha, needed for IBM/RS6000
  KMP_MB();

  if (tq->tq_freelist) {
    new_queue = tq->tq_freelist;
    tq->tq_freelist = tq->tq_freelist->tq.tq_next_free;

    KMP_DEBUG_ASSERT(new_queue->tq_flags & TQF_DEALLOCATED);

    new_queue->tq_flags = 0;

    __kmp_release_lock(&tq->tq_freelist_lck, global_tid);
  } else {
    __kmp_release_lock(&tq->tq_freelist_lck, global_tid);

    new_queue = (kmpc_task_queue_t *)__kmp_taskq_allocate(
        sizeof(kmpc_task_queue_t), global_tid);
    new_queue->tq_flags = 0;
  }

  /*  space in the task queue for queue slots (allocate as one big chunk */
  /* of storage including new_taskq_task space)                          */

  sizeof_thunk +=
      (CACHE_LINE - (sizeof_thunk % CACHE_LINE)); /* pad to cache line size */
  pt = (char *)__kmp_taskq_allocate(nthunks * sizeof_thunk, global_tid);
  new_queue->tq_thunk_space = (kmpc_thunk_t *)pt;
  *new_taskq_thunk = (kmpc_thunk_t *)(pt + (nthunks - 1) * sizeof_thunk);

  /*  chain the allocated thunks into a freelist for this queue  */

  new_queue->tq_free_thunks = (kmpc_thunk_t *)pt;

  for (i = 0; i < (nthunks - 2); i++) {
    ((kmpc_thunk_t *)(pt + i * sizeof_thunk))->th.th_next_free =
        (kmpc_thunk_t *)(pt + (i + 1) * sizeof_thunk);
#ifdef KMP_DEBUG
    ((kmpc_thunk_t *)(pt + i * sizeof_thunk))->th_flags = TQF_DEALLOCATED;
#endif
  }

  ((kmpc_thunk_t *)(pt + (nthunks - 2) * sizeof_thunk))->th.th_next_free = NULL;
#ifdef KMP_DEBUG
  ((kmpc_thunk_t *)(pt + (nthunks - 2) * sizeof_thunk))->th_flags =
      TQF_DEALLOCATED;
#endif

  /* initialize the locks */

  if (in_parallel) {
    __kmp_init_lock(&new_queue->tq_link_lck);
    __kmp_init_lock(&new_queue->tq_free_thunks_lck);
    __kmp_init_lock(&new_queue->tq_queue_lck);
  }

  /* now allocate the slots */

  bytes = nslots * sizeof(kmpc_aligned_queue_slot_t);
  new_queue->tq_queue =
      (kmpc_aligned_queue_slot_t *)__kmp_taskq_allocate(bytes, global_tid);

  /*  space for array of pointers to shared variable structures */
  sizeof_shareds += sizeof(kmpc_task_queue_t *);
  sizeof_shareds +=
      (CACHE_LINE - (sizeof_shareds % CACHE_LINE)); /* pad to cache line size */

  bytes = nshareds * sizeof(kmpc_aligned_shared_vars_t);
  shared_var_array =
      (kmpc_aligned_shared_vars_t *)__kmp_taskq_allocate(bytes, global_tid);

  bytes = nshareds * sizeof_shareds;
  shared_var_storage = (char *)__kmp_taskq_allocate(bytes, global_tid);

  for (i = 0; i < nshareds; i++) {
    shared_var_array[i].ai_data =
        (kmpc_shared_vars_t *)(shared_var_storage + i * sizeof_shareds);
    shared_var_array[i].ai_data->sv_queue = new_queue;
  }
  new_queue->tq_shareds = shared_var_array;

  /* array for number of outstanding thunks per thread */

  if (in_parallel) {
    bytes = nproc * sizeof(kmpc_aligned_int32_t);
    new_queue->tq_th_thunks =
        (kmpc_aligned_int32_t *)__kmp_taskq_allocate(bytes, global_tid);
    new_queue->tq_nproc = nproc;

    for (i = 0; i < nproc; i++)
      new_queue->tq_th_thunks[i].ai_data = 0;
  }

  return new_queue;
}

static void __kmp_free_taskq(kmp_taskq_t *tq, kmpc_task_queue_t *p,
                             int in_parallel, kmp_int32 global_tid) {
  __kmpc_taskq_free(p->tq_thunk_space, global_tid);
  __kmpc_taskq_free(p->tq_queue, global_tid);

  /* free shared var structure storage */
  __kmpc_taskq_free(CCAST(kmpc_shared_vars_t *, p->tq_shareds[0].ai_data),
                    global_tid);
  /* free array of pointers to shared vars storage */
  __kmpc_taskq_free(p->tq_shareds, global_tid);

#ifdef KMP_DEBUG
  p->tq_first_child = NULL;
  p->tq_next_child = NULL;
  p->tq_prev_child = NULL;
  p->tq_ref_count = -10;
  p->tq_shareds = NULL;
  p->tq_tasknum_queuing = 0;
  p->tq_tasknum_serving = 0;
  p->tq_queue = NULL;
  p->tq_thunk_space = NULL;
  p->tq_taskq_slot = NULL;
  p->tq_free_thunks = NULL;
  p->tq_nslots = 0;
  p->tq_head = 0;
  p->tq_tail = 0;
  p->tq_nfull = 0;
  p->tq_hiwat = 0;

  if (in_parallel) {
    int i;

    for (i = 0; i < p->tq_nproc; i++)
      p->tq_th_thunks[i].ai_data = 0;
  }
  if (__kmp_env_consistency_check)
    p->tq_loc = NULL;
  KMP_DEBUG_ASSERT(p->tq_flags & TQF_DEALLOCATED);
  p->tq_flags = TQF_DEALLOCATED;
#endif /* KMP_DEBUG */

  if (in_parallel) {
    __kmpc_taskq_free(p->tq_th_thunks, global_tid);
    __kmp_destroy_lock(&p->tq_link_lck);
    __kmp_destroy_lock(&p->tq_queue_lck);
    __kmp_destroy_lock(&p->tq_free_thunks_lck);
  }
#ifdef KMP_DEBUG
  p->tq_th_thunks = NULL;
#endif /* KMP_DEBUG */

  // Make sure data structures are in consistent state before querying them
  // Seems to work without this call for digital/alpha, needed for IBM/RS6000
  KMP_MB();

  __kmp_acquire_lock(&tq->tq_freelist_lck, global_tid);
  p->tq.tq_next_free = tq->tq_freelist;

  tq->tq_freelist = p;
  __kmp_release_lock(&tq->tq_freelist_lck, global_tid);
}

/* Once a group of thunks has been allocated for use in a particular queue,
   these are managed via a per-queue freelist.
   We force a check that there's always a thunk free if we need one. */

static kmpc_thunk_t *__kmp_alloc_thunk(kmpc_task_queue_t *queue,
                                       int in_parallel, kmp_int32 global_tid) {
  kmpc_thunk_t *fl;

  if (in_parallel) {
    __kmp_acquire_lock(&queue->tq_free_thunks_lck, global_tid);
    // Make sure data structures are in consistent state before querying them
    // Seems to work without this call for digital/alpha, needed for IBM/RS6000
    KMP_MB();
  }

  fl = queue->tq_free_thunks;

  KMP_DEBUG_ASSERT(fl != NULL);

  queue->tq_free_thunks = fl->th.th_next_free;
  fl->th_flags = 0;

  if (in_parallel)
    __kmp_release_lock(&queue->tq_free_thunks_lck, global_tid);

  return fl;
}

static void __kmp_free_thunk(kmpc_task_queue_t *queue, kmpc_thunk_t *p,
                             int in_parallel, kmp_int32 global_tid) {
#ifdef KMP_DEBUG
  p->th_task = 0;
  p->th_encl_thunk = 0;
  p->th_status = 0;
  p->th_tasknum = 0;
/* Also could zero pointers to private vars */
#endif

  if (in_parallel) {
    __kmp_acquire_lock(&queue->tq_free_thunks_lck, global_tid);
    // Make sure data structures are in consistent state before querying them
    // Seems to work without this call for digital/alpha, needed for IBM/RS6000
    KMP_MB();
  }

  p->th.th_next_free = queue->tq_free_thunks;
  queue->tq_free_thunks = p;

#ifdef KMP_DEBUG
  p->th_flags = TQF_DEALLOCATED;
#endif

  if (in_parallel)
    __kmp_release_lock(&queue->tq_free_thunks_lck, global_tid);
}

/*  returns nonzero if the queue just became full after the enqueue  */
static kmp_int32 __kmp_enqueue_task(kmp_taskq_t *tq, kmp_int32 global_tid,
                                    kmpc_task_queue_t *queue,
                                    kmpc_thunk_t *thunk, int in_parallel) {
  kmp_int32 ret;

  /*  dkp: can we get around the lock in the TQF_RELEASE_WORKERS case (only the
   * master is executing then)  */
  if (in_parallel) {
    __kmp_acquire_lock(&queue->tq_queue_lck, global_tid);
    // Make sure data structures are in consistent state before querying them
    // Seems to work without this call for digital/alpha, needed for IBM/RS6000
    KMP_MB();
  }

  KMP_DEBUG_ASSERT(queue->tq_nfull < queue->tq_nslots); // check queue not full

  queue->tq_queue[(queue->tq_head)++].qs_thunk = thunk;

  if (queue->tq_head >= queue->tq_nslots)
    queue->tq_head = 0;

  (queue->tq_nfull)++;

  KMP_MB(); /* to assure that nfull is seen to increase before
               TQF_ALL_TASKS_QUEUED is set */

  ret = (in_parallel) ? (queue->tq_nfull == queue->tq_nslots) : FALSE;

  if (in_parallel) {
    /* don't need to wait until workers are released before unlocking */
    __kmp_release_lock(&queue->tq_queue_lck, global_tid);

    if (tq->tq_global_flags & TQF_RELEASE_WORKERS) {
      // If just creating the root queue, the worker threads are waiting at a
      // join barrier until now, when there's something in the queue for them to
      // do; release them now to do work. This should only be done when this is
      // the first task enqueued, so reset the flag here also.
      tq->tq_global_flags &= ~TQF_RELEASE_WORKERS; /* no lock needed, workers
                                                      are still in spin mode */
      // avoid releasing barrier twice if taskq_task switches threads
      KMP_MB();

      __kmpc_end_barrier_master(NULL, global_tid);
    }
  }

  return ret;
}

static kmpc_thunk_t *__kmp_dequeue_task(kmp_int32 global_tid,
                                        kmpc_task_queue_t *queue,
                                        int in_parallel) {
  kmpc_thunk_t *pt;
  int tid = __kmp_tid_from_gtid(global_tid);

  KMP_DEBUG_ASSERT(queue->tq_nfull > 0); /*  check queue not empty  */

  if (queue->tq.tq_parent != NULL && in_parallel) {
    int ct;
    __kmp_acquire_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
    ct = ++(queue->tq_ref_count);
    __kmp_release_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
    KMP_DEBUG_REF_CTS(
        ("line %d gtid %d: Q %p inc %d\n", __LINE__, global_tid, queue, ct));
  }

  pt = queue->tq_queue[(queue->tq_tail)++].qs_thunk;

  if (queue->tq_tail >= queue->tq_nslots)
    queue->tq_tail = 0;

  if (in_parallel) {
    queue->tq_th_thunks[tid].ai_data++;

    KMP_MB(); /* necessary so ai_data increment is propagated to other threads
                 immediately (digital) */

    KF_TRACE(200, ("__kmp_dequeue_task: T#%d(:%d) now has %d outstanding "
                   "thunks from queue %p\n",
                   global_tid, tid, queue->tq_th_thunks[tid].ai_data, queue));
  }

  (queue->tq_nfull)--;

#ifdef KMP_DEBUG
  KMP_MB();

  /* necessary so (queue->tq_nfull > 0) above succeeds after tq_nfull is
   * decremented */

  KMP_DEBUG_ASSERT(queue->tq_nfull >= 0);

  if (in_parallel) {
    KMP_DEBUG_ASSERT(queue->tq_th_thunks[tid].ai_data <=
                     __KMP_TASKQ_THUNKS_PER_TH);
  }
#endif

  return pt;
}

/* Find the next (non-null) task to dequeue and return it.
 * This is never called unless in_parallel=TRUE
 *
 * Here are the rules for deciding which queue to take the task from:
 * 1.  Walk up the task queue tree from the current queue's parent and look
 *      on the way up (for loop, below).
 * 2.  Do a depth-first search back down the tree from the root and
 *      look (find_task_in_descendant_queue()).
 *
 * Here are the rules for deciding which task to take from a queue
 * (__kmp_find_task_in_queue ()):
 * 1.  Never take the last task from a queue if TQF_IS_LASTPRIVATE; this task
 *     must be staged to make sure we execute the last one with
 *     TQF_IS_LAST_TASK at the end of task queue execution.
 * 2.  If the queue length is below some high water mark and the taskq task
 *     is enqueued, prefer running the taskq task.
 * 3.  Otherwise, take a (normal) task from the queue.
 *
 * If we do all this and return pt == NULL at the bottom of this routine,
 * this means there are no more tasks to execute (except possibly for
 * TQF_IS_LASTPRIVATE).
 */

static kmpc_thunk_t *__kmp_find_task_in_queue(kmp_int32 global_tid,
                                              kmpc_task_queue_t *queue) {
  kmpc_thunk_t *pt = NULL;
  int tid = __kmp_tid_from_gtid(global_tid);

  /* To prevent deadlock from tq_queue_lck if queue already deallocated */
  if (!(queue->tq_flags & TQF_DEALLOCATED)) {

    __kmp_acquire_lock(&queue->tq_queue_lck, global_tid);

    /* Check again to avoid race in __kmpc_end_taskq() */
    if (!(queue->tq_flags & TQF_DEALLOCATED)) {
      // Make sure data structures are in consistent state before querying them
      // Seems to work without this for digital/alpha, needed for IBM/RS6000
      KMP_MB();

      if ((queue->tq_taskq_slot != NULL) &&
          (queue->tq_nfull <= queue->tq_hiwat)) {
        /* if there's enough room in the queue and the dispatcher */
        /* (taskq task) is available, schedule more tasks         */
        pt = CCAST(kmpc_thunk_t *, queue->tq_taskq_slot);
        queue->tq_taskq_slot = NULL;
      } else if (queue->tq_nfull == 0 ||
                 queue->tq_th_thunks[tid].ai_data >=
                     __KMP_TASKQ_THUNKS_PER_TH) {
        /* do nothing if no thunks available or this thread can't */
        /* run any because it already is executing too many       */
        pt = NULL;
      } else if (queue->tq_nfull > 1) {
        /*  always safe to schedule a task even if TQF_IS_LASTPRIVATE  */

        pt = __kmp_dequeue_task(global_tid, queue, TRUE);
      } else if (!(queue->tq_flags & TQF_IS_LASTPRIVATE)) {
        // one thing in queue, always safe to schedule if !TQF_IS_LASTPRIVATE
        pt = __kmp_dequeue_task(global_tid, queue, TRUE);
      } else if (queue->tq_flags & TQF_IS_LAST_TASK) {
        /* TQF_IS_LASTPRIVATE, one thing in queue, kmpc_end_taskq_task()   */
        /* has been run so this is last task, run with TQF_IS_LAST_TASK so */
        /* instrumentation does copy-out.                                  */
        pt = __kmp_dequeue_task(global_tid, queue, TRUE);
        pt->th_flags |=
            TQF_IS_LAST_TASK; /* don't need test_then_or since already locked */
      }
    }

    /* GEH - What happens here if is lastprivate, but not last task? */
    __kmp_release_lock(&queue->tq_queue_lck, global_tid);
  }

  return pt;
}

/* Walk a tree of queues starting at queue's first child and return a non-NULL
   thunk if one can be scheduled. Must only be called when in_parallel=TRUE */

static kmpc_thunk_t *
__kmp_find_task_in_descendant_queue(kmp_int32 global_tid,
                                    kmpc_task_queue_t *curr_queue) {
  kmpc_thunk_t *pt = NULL;
  kmpc_task_queue_t *queue = curr_queue;

  if (curr_queue->tq_first_child != NULL) {
    __kmp_acquire_lock(&curr_queue->tq_link_lck, global_tid);
    // Make sure data structures are in consistent state before querying them
    // Seems to work without this call for digital/alpha, needed for IBM/RS6000
    KMP_MB();

    queue = CCAST(kmpc_task_queue_t *, curr_queue->tq_first_child);
    if (queue == NULL) {
      __kmp_release_lock(&curr_queue->tq_link_lck, global_tid);
      return NULL;
    }

    while (queue != NULL) {
      int ct;
      kmpc_task_queue_t *next;

      ct = ++(queue->tq_ref_count);
      __kmp_release_lock(&curr_queue->tq_link_lck, global_tid);
      KMP_DEBUG_REF_CTS(
          ("line %d gtid %d: Q %p inc %d\n", __LINE__, global_tid, queue, ct));

      pt = __kmp_find_task_in_queue(global_tid, queue);

      if (pt != NULL) {
        int ct;

        __kmp_acquire_lock(&curr_queue->tq_link_lck, global_tid);
        // Make sure data structures in consistent state before querying them
        // Seems to work without this for digital/alpha, needed for IBM/RS6000
        KMP_MB();

        ct = --(queue->tq_ref_count);
        KMP_DEBUG_REF_CTS(("line %d gtid %d: Q %p dec %d\n", __LINE__,
                           global_tid, queue, ct));
        KMP_DEBUG_ASSERT(queue->tq_ref_count >= 0);

        __kmp_release_lock(&curr_queue->tq_link_lck, global_tid);

        return pt;
      }

      /* although reference count stays active during descendant walk, shouldn't
         matter  since if children still exist, reference counts aren't being
         monitored anyway   */

      pt = __kmp_find_task_in_descendant_queue(global_tid, queue);

      if (pt != NULL) {
        int ct;

        __kmp_acquire_lock(&curr_queue->tq_link_lck, global_tid);
        // Make sure data structures in consistent state before querying them
        // Seems to work without this for digital/alpha, needed for IBM/RS6000
        KMP_MB();

        ct = --(queue->tq_ref_count);
        KMP_DEBUG_REF_CTS(("line %d gtid %d: Q %p dec %d\n", __LINE__,
                           global_tid, queue, ct));
        KMP_DEBUG_ASSERT(ct >= 0);

        __kmp_release_lock(&curr_queue->tq_link_lck, global_tid);

        return pt;
      }

      __kmp_acquire_lock(&curr_queue->tq_link_lck, global_tid);
      // Make sure data structures in consistent state before querying them
      // Seems to work without this for digital/alpha, needed for IBM/RS6000
      KMP_MB();

      next = queue->tq_next_child;

      ct = --(queue->tq_ref_count);
      KMP_DEBUG_REF_CTS(
          ("line %d gtid %d: Q %p dec %d\n", __LINE__, global_tid, queue, ct));
      KMP_DEBUG_ASSERT(ct >= 0);

      queue = next;
    }

    __kmp_release_lock(&curr_queue->tq_link_lck, global_tid);
  }

  return pt;
}

/* Walk up the taskq tree looking for a task to execute. If we get to the root,
   search the tree for a descendent queue task. Must only be called when
   in_parallel=TRUE */
static kmpc_thunk_t *
__kmp_find_task_in_ancestor_queue(kmp_taskq_t *tq, kmp_int32 global_tid,
                                  kmpc_task_queue_t *curr_queue) {
  kmpc_task_queue_t *queue;
  kmpc_thunk_t *pt;

  pt = NULL;

  if (curr_queue->tq.tq_parent != NULL) {
    queue = curr_queue->tq.tq_parent;

    while (queue != NULL) {
      if (queue->tq.tq_parent != NULL) {
        int ct;
        __kmp_acquire_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
        // Make sure data structures in consistent state before querying them
        // Seems to work without this for digital/alpha, needed for IBM/RS6000
        KMP_MB();

        ct = ++(queue->tq_ref_count);
        __kmp_release_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
        KMP_DEBUG_REF_CTS(("line %d gtid %d: Q %p inc %d\n", __LINE__,
                           global_tid, queue, ct));
      }

      pt = __kmp_find_task_in_queue(global_tid, queue);
      if (pt != NULL) {
        if (queue->tq.tq_parent != NULL) {
          int ct;
          __kmp_acquire_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
          // Make sure data structures in consistent state before querying them
          // Seems to work without this for digital/alpha, needed for IBM/RS6000
          KMP_MB();

          ct = --(queue->tq_ref_count);
          KMP_DEBUG_REF_CTS(("line %d gtid %d: Q %p dec %d\n", __LINE__,
                             global_tid, queue, ct));
          KMP_DEBUG_ASSERT(ct >= 0);

          __kmp_release_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
        }

        return pt;
      }

      if (queue->tq.tq_parent != NULL) {
        int ct;
        __kmp_acquire_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
        // Make sure data structures in consistent state before querying them
        // Seems to work without this for digital/alpha, needed for IBM/RS6000
        KMP_MB();

        ct = --(queue->tq_ref_count);
        KMP_DEBUG_REF_CTS(("line %d gtid %d: Q %p dec %d\n", __LINE__,
                           global_tid, queue, ct));
        KMP_DEBUG_ASSERT(ct >= 0);
      }
      queue = queue->tq.tq_parent;

      if (queue != NULL)
        __kmp_release_lock(&queue->tq_link_lck, global_tid);
    }
  }

  pt = __kmp_find_task_in_descendant_queue(global_tid, tq->tq_root);

  return pt;
}

static int __kmp_taskq_tasks_finished(kmpc_task_queue_t *queue) {
  int i;

  /* KMP_MB(); */ /* is this really necessary? */

  for (i = 0; i < queue->tq_nproc; i++) {
    if (queue->tq_th_thunks[i].ai_data != 0)
      return FALSE;
  }

  return TRUE;
}

static int __kmp_taskq_has_any_children(kmpc_task_queue_t *queue) {
  return (queue->tq_first_child != NULL);
}

static void __kmp_remove_queue_from_tree(kmp_taskq_t *tq, kmp_int32 global_tid,
                                         kmpc_task_queue_t *queue,
                                         int in_parallel) {
#ifdef KMP_DEBUG
  kmp_int32 i;
  kmpc_thunk_t *thunk;
#endif

  KF_TRACE(50,
           ("Before Deletion of TaskQ at %p on (%d):\n", queue, global_tid));
  KF_DUMP(50, __kmp_dump_task_queue(tq, queue, global_tid));

  /*  sub-queue in a recursion, not the root task queue  */
  KMP_DEBUG_ASSERT(queue->tq.tq_parent != NULL);

  if (in_parallel) {
    __kmp_acquire_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
    // Make sure data structures are in consistent state before querying them
    // Seems to work without this call for digital/alpha, needed for IBM/RS6000
    KMP_MB();
  }

  KMP_DEBUG_ASSERT(queue->tq_first_child == NULL);

  /*  unlink queue from its siblings if any at this level  */
  if (queue->tq_prev_child != NULL)
    queue->tq_prev_child->tq_next_child = queue->tq_next_child;
  if (queue->tq_next_child != NULL)
    queue->tq_next_child->tq_prev_child = queue->tq_prev_child;
  if (queue->tq.tq_parent->tq_first_child == queue)
    queue->tq.tq_parent->tq_first_child = queue->tq_next_child;

  queue->tq_prev_child = NULL;
  queue->tq_next_child = NULL;

  if (in_parallel) {
    KMP_DEBUG_REF_CTS(
        ("line %d gtid %d: Q %p waiting for ref_count of %d to reach 1\n",
         __LINE__, global_tid, queue, queue->tq_ref_count));

    /* wait until all other threads have stopped accessing this queue */
    while (queue->tq_ref_count > 1) {
      __kmp_release_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);

      KMP_WAIT_YIELD((volatile kmp_uint32 *)&queue->tq_ref_count, 1, KMP_LE,
                     NULL);

      __kmp_acquire_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
      // Make sure data structures are in consistent state before querying them
      // Seems to work without this for digital/alpha, needed for IBM/RS6000
      KMP_MB();
    }

    __kmp_release_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
  }

  KMP_DEBUG_REF_CTS(
      ("line %d gtid %d: Q %p freeing queue\n", __LINE__, global_tid, queue));

#ifdef KMP_DEBUG
  KMP_DEBUG_ASSERT(queue->tq_flags & TQF_ALL_TASKS_QUEUED);
  KMP_DEBUG_ASSERT(queue->tq_nfull == 0);

  for (i = 0; i < queue->tq_nproc; i++) {
    KMP_DEBUG_ASSERT(queue->tq_th_thunks[i].ai_data == 0);
  }

  i = 0;
  for (thunk = queue->tq_free_thunks; thunk != NULL;
       thunk = thunk->th.th_next_free)
    ++i;

  KMP_ASSERT(i ==
             queue->tq_nslots + (queue->tq_nproc * __KMP_TASKQ_THUNKS_PER_TH));
#endif

  /*  release storage for queue entry  */
  __kmp_free_taskq(tq, queue, TRUE, global_tid);

  KF_TRACE(50, ("After Deletion of TaskQ at %p on (%d):\n", queue, global_tid));
  KF_DUMP(50, __kmp_dump_task_queue_tree(tq, tq->tq_root, global_tid));
}

/* Starting from indicated queue, proceed downward through tree and remove all
   taskqs which are finished, but only go down to taskqs which have the "nowait"
   clause present.  Assume this is only called when in_parallel=TRUE. */

static void __kmp_find_and_remove_finished_child_taskq(
    kmp_taskq_t *tq, kmp_int32 global_tid, kmpc_task_queue_t *curr_queue) {
  kmpc_task_queue_t *queue = curr_queue;

  if (curr_queue->tq_first_child != NULL) {
    __kmp_acquire_lock(&curr_queue->tq_link_lck, global_tid);
    // Make sure data structures are in consistent state before querying them
    // Seems to work without this call for digital/alpha, needed for IBM/RS6000
    KMP_MB();

    queue = CCAST(kmpc_task_queue_t *, curr_queue->tq_first_child);
    if (queue != NULL) {
      __kmp_release_lock(&curr_queue->tq_link_lck, global_tid);
      return;
    }

    while (queue != NULL) {
      kmpc_task_queue_t *next;
      int ct = ++(queue->tq_ref_count);
      KMP_DEBUG_REF_CTS(
          ("line %d gtid %d: Q %p inc %d\n", __LINE__, global_tid, queue, ct));

      /* although reference count stays active during descendant walk, */
      /* shouldn't matter since if children still exist, reference     */
      /* counts aren't being monitored anyway                          */

      if (queue->tq_flags & TQF_IS_NOWAIT) {
        __kmp_find_and_remove_finished_child_taskq(tq, global_tid, queue);

        if ((queue->tq_flags & TQF_ALL_TASKS_QUEUED) &&
            (queue->tq_nfull == 0) && __kmp_taskq_tasks_finished(queue) &&
            !__kmp_taskq_has_any_children(queue)) {

          /* Only remove this if we have not already marked it for deallocation.
             This should prevent multiple threads from trying to free this. */

          if (__kmp_test_lock(&queue->tq_queue_lck, global_tid)) {
            if (!(queue->tq_flags & TQF_DEALLOCATED)) {
              queue->tq_flags |= TQF_DEALLOCATED;
              __kmp_release_lock(&queue->tq_queue_lck, global_tid);

              __kmp_remove_queue_from_tree(tq, global_tid, queue, TRUE);

              /* Can't do any more here since can't be sure where sibling queue
               * is so just exit this level */
              return;
            } else {
              __kmp_release_lock(&queue->tq_queue_lck, global_tid);
            }
          }
          /* otherwise, just fall through and decrement reference count */
        }
      }

      __kmp_acquire_lock(&curr_queue->tq_link_lck, global_tid);
      // Make sure data structures are in consistent state before querying them
      // Seems to work without this for digital/alpha, needed for IBM/RS6000
      KMP_MB();

      next = queue->tq_next_child;

      ct = --(queue->tq_ref_count);
      KMP_DEBUG_REF_CTS(
          ("line %d gtid %d: Q %p dec %d\n", __LINE__, global_tid, queue, ct));
      KMP_DEBUG_ASSERT(ct >= 0);

      queue = next;
    }

    __kmp_release_lock(&curr_queue->tq_link_lck, global_tid);
  }
}

/* Starting from indicated queue, proceed downward through tree and remove all
   taskq's assuming all are finished and assuming NO other threads are executing
   at this point. */
static void __kmp_remove_all_child_taskq(kmp_taskq_t *tq, kmp_int32 global_tid,
                                         kmpc_task_queue_t *queue) {
  kmpc_task_queue_t *next_child;

  queue = CCAST(kmpc_task_queue_t *, queue->tq_first_child);

  while (queue != NULL) {
    __kmp_remove_all_child_taskq(tq, global_tid, queue);

    next_child = queue->tq_next_child;
    queue->tq_flags |= TQF_DEALLOCATED;
    __kmp_remove_queue_from_tree(tq, global_tid, queue, FALSE);
    queue = next_child;
  }
}

static void __kmp_execute_task_from_queue(kmp_taskq_t *tq, ident_t *loc,
                                          kmp_int32 global_tid,
                                          kmpc_thunk_t *thunk,
                                          int in_parallel) {
  kmpc_task_queue_t *queue = thunk->th.th_shareds->sv_queue;
  kmp_int32 tid = __kmp_tid_from_gtid(global_tid);

  KF_TRACE(100, ("After dequeueing this Task on (%d):\n", global_tid));
  KF_DUMP(100, __kmp_dump_thunk(tq, thunk, global_tid));
  KF_TRACE(100, ("Task Queue: %p looks like this (%d):\n", queue, global_tid));
  KF_DUMP(100, __kmp_dump_task_queue(tq, queue, global_tid));

  /* For the taskq task, the curr_thunk pushes and pop pairs are set up as
   * follows:
   *
   * happens exactly once:
   * 1) __kmpc_taskq             : push (if returning thunk only)
   * 4) __kmpc_end_taskq_task    : pop
   *
   * optionally happens *each* time taskq task is dequeued/enqueued:
   * 2) __kmpc_taskq_task        : pop
   * 3) __kmp_execute_task_from_queue  : push
   *
   * execution ordering:  1,(2,3)*,4
   */

  if (!(thunk->th_flags & TQF_TASKQ_TASK)) {
    kmp_int32 index = (queue == tq->tq_root) ? tid : 0;
    thunk->th.th_shareds =
        CCAST(kmpc_shared_vars_t *, queue->tq_shareds[index].ai_data);

    if (__kmp_env_consistency_check) {
      __kmp_push_workshare(global_tid,
                           (queue->tq_flags & TQF_IS_ORDERED) ? ct_task_ordered
                                                              : ct_task,
                           queue->tq_loc);
    }
  } else {
    if (__kmp_env_consistency_check)
      __kmp_push_workshare(global_tid, ct_taskq, queue->tq_loc);
  }

  if (in_parallel) {
    thunk->th_encl_thunk = tq->tq_curr_thunk[tid];
    tq->tq_curr_thunk[tid] = thunk;

    KF_DUMP(200, __kmp_dump_thunk_stack(tq->tq_curr_thunk[tid], global_tid));
  }

  KF_TRACE(50, ("Begin Executing Thunk %p from queue %p on (%d)\n", thunk,
                queue, global_tid));
  thunk->th_task(global_tid, thunk);
  KF_TRACE(50, ("End Executing Thunk %p from queue %p on (%d)\n", thunk, queue,
                global_tid));

  if (!(thunk->th_flags & TQF_TASKQ_TASK)) {
    if (__kmp_env_consistency_check)
      __kmp_pop_workshare(global_tid,
                          (queue->tq_flags & TQF_IS_ORDERED) ? ct_task_ordered
                                                             : ct_task,
                          queue->tq_loc);

    if (in_parallel) {
      tq->tq_curr_thunk[tid] = thunk->th_encl_thunk;
      thunk->th_encl_thunk = NULL;
      KF_DUMP(200, __kmp_dump_thunk_stack(tq->tq_curr_thunk[tid], global_tid));
    }

    if ((thunk->th_flags & TQF_IS_ORDERED) && in_parallel) {
      __kmp_taskq_check_ordered(global_tid, thunk);
    }

    __kmp_free_thunk(queue, thunk, in_parallel, global_tid);

    KF_TRACE(100, ("T#%d After freeing thunk: %p, TaskQ looks like this:\n",
                   global_tid, thunk));
    KF_DUMP(100, __kmp_dump_task_queue(tq, queue, global_tid));

    if (in_parallel) {
      KMP_MB(); /* needed so thunk put on free list before outstanding thunk
                   count is decremented */

      KMP_DEBUG_ASSERT(queue->tq_th_thunks[tid].ai_data >= 1);

      KF_TRACE(
          200,
          ("__kmp_execute_task_from_queue: T#%d has %d thunks in queue %p\n",
           global_tid, queue->tq_th_thunks[tid].ai_data - 1, queue));

      queue->tq_th_thunks[tid].ai_data--;

      /* KMP_MB(); */ /* is MB really necessary ? */
    }

    if (queue->tq.tq_parent != NULL && in_parallel) {
      int ct;
      __kmp_acquire_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
      ct = --(queue->tq_ref_count);
      __kmp_release_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
      KMP_DEBUG_REF_CTS(
          ("line %d gtid %d: Q %p dec %d\n", __LINE__, global_tid, queue, ct));
      KMP_DEBUG_ASSERT(ct >= 0);
    }
  }
}

/* starts a taskq; creates and returns a thunk for the taskq_task        */
/* also, returns pointer to shared vars for this thread in "shareds" arg */
kmpc_thunk_t *__kmpc_taskq(ident_t *loc, kmp_int32 global_tid,
                           kmpc_task_t taskq_task, size_t sizeof_thunk,
                           size_t sizeof_shareds, kmp_int32 flags,
                           kmpc_shared_vars_t **shareds) {
  int in_parallel;
  kmp_int32 nslots, nthunks, nshareds, nproc;
  kmpc_task_queue_t *new_queue, *curr_queue;
  kmpc_thunk_t *new_taskq_thunk;
  kmp_info_t *th;
  kmp_team_t *team;
  kmp_taskq_t *tq;
  kmp_int32 tid;

  KE_TRACE(10, ("__kmpc_taskq called (%d)\n", global_tid));

  th = __kmp_threads[global_tid];
  team = th->th.th_team;
  tq = &team->t.t_taskq;
  nproc = team->t.t_nproc;
  tid = __kmp_tid_from_gtid(global_tid);

  /* find out whether this is a parallel taskq or serialized one. */
  in_parallel = in_parallel_context(team);

  if (!tq->tq_root) {
    if (in_parallel) {
      /* Vector ORDERED SECTION to taskq version */
      th->th.th_dispatch->th_deo_fcn = __kmp_taskq_eo;

      /* Vector ORDERED SECTION to taskq version */
      th->th.th_dispatch->th_dxo_fcn = __kmp_taskq_xo;
    }

    if (in_parallel) {
      // This shouldn't be a barrier region boundary, it will confuse the user.
      /* Need the boundary to be at the end taskq instead. */
      if (__kmp_barrier(bs_plain_barrier, global_tid, TRUE, 0, NULL, NULL)) {
        /* Creating the active root queue, and we are not the master thread. */
        /* The master thread below created the queue and tasks have been     */
        /* enqueued, and the master thread released this barrier.  This      */
        /* worker thread can now proceed and execute tasks.  See also the    */
        /* TQF_RELEASE_WORKERS which is used to handle this case.            */
        *shareds =
            CCAST(kmpc_shared_vars_t *, tq->tq_root->tq_shareds[tid].ai_data);
        KE_TRACE(10, ("__kmpc_taskq return (%d)\n", global_tid));

        return NULL;
      }
    }

    /* master thread only executes this code */
    if (tq->tq_curr_thunk_capacity < nproc) {
      if (tq->tq_curr_thunk)
        __kmp_free(tq->tq_curr_thunk);
      else {
        /* only need to do this once at outer level, i.e. when tq_curr_thunk is
         * still NULL */
        __kmp_init_lock(&tq->tq_freelist_lck);
      }

      tq->tq_curr_thunk =
          (kmpc_thunk_t **)__kmp_allocate(nproc * sizeof(kmpc_thunk_t *));
      tq->tq_curr_thunk_capacity = nproc;
    }

    if (in_parallel)
      tq->tq_global_flags = TQF_RELEASE_WORKERS;
  }

  /* dkp: in future, if flags & TQF_HEURISTICS, will choose nslots based */
  /*      on some heuristics (e.g., depth of queue nesting?).            */
  nslots = (in_parallel) ? (2 * nproc) : 1;

  /* There must be nproc * __KMP_TASKQ_THUNKS_PER_TH extra slots for pending */
  /* jobs being executed by other threads, and one extra for taskq slot */
  nthunks = (in_parallel) ? (nslots + (nproc * __KMP_TASKQ_THUNKS_PER_TH) + 1)
                          : nslots + 2;

  /* Only the root taskq gets a per-thread array of shareds.       */
  /* The rest of the taskq's only get one copy of the shared vars. */
  nshareds = (!tq->tq_root && in_parallel) ? nproc : 1;

  /*  create overall queue data structure and its components that require
   * allocation */
  new_queue = __kmp_alloc_taskq(tq, in_parallel, nslots, nthunks, nshareds,
                                nproc, sizeof_thunk, sizeof_shareds,
                                &new_taskq_thunk, global_tid);

  /*  rest of new_queue initializations  */
  new_queue->tq_flags = flags & TQF_INTERFACE_FLAGS;

  if (in_parallel) {
    new_queue->tq_tasknum_queuing = 0;
    new_queue->tq_tasknum_serving = 0;
    new_queue->tq_flags |= TQF_PARALLEL_CONTEXT;
  }

  new_queue->tq_taskq_slot = NULL;
  new_queue->tq_nslots = nslots;
  new_queue->tq_hiwat = HIGH_WATER_MARK(nslots);
  new_queue->tq_nfull = 0;
  new_queue->tq_head = 0;
  new_queue->tq_tail = 0;
  new_queue->tq_loc = loc;

  if ((new_queue->tq_flags & TQF_IS_ORDERED) && in_parallel) {
    /* prepare to serve the first-queued task's ORDERED directive */
    new_queue->tq_tasknum_serving = 1;

    /* Vector ORDERED SECTION to taskq version */
    th->th.th_dispatch->th_deo_fcn = __kmp_taskq_eo;

    /* Vector ORDERED SECTION to taskq version */
    th->th.th_dispatch->th_dxo_fcn = __kmp_taskq_xo;
  }

  /*  create a new thunk for the taskq_task in the new_queue  */
  *shareds = CCAST(kmpc_shared_vars_t *, new_queue->tq_shareds[0].ai_data);

  new_taskq_thunk->th.th_shareds = *shareds;
  new_taskq_thunk->th_task = taskq_task;
  new_taskq_thunk->th_flags = new_queue->tq_flags | TQF_TASKQ_TASK;
  new_taskq_thunk->th_status = 0;

  KMP_DEBUG_ASSERT(new_taskq_thunk->th_flags & TQF_TASKQ_TASK);

  // Make sure these inits complete before threads start using this queue
  /* KMP_MB(); */ // (necessary?)

  /* insert the new task queue into the tree, but only after all fields
   * initialized */

  if (in_parallel) {
    if (!tq->tq_root) {
      new_queue->tq.tq_parent = NULL;
      new_queue->tq_first_child = NULL;
      new_queue->tq_next_child = NULL;
      new_queue->tq_prev_child = NULL;
      new_queue->tq_ref_count = 1;
      tq->tq_root = new_queue;
    } else {
      curr_queue = tq->tq_curr_thunk[tid]->th.th_shareds->sv_queue;
      new_queue->tq.tq_parent = curr_queue;
      new_queue->tq_first_child = NULL;
      new_queue->tq_prev_child = NULL;
      new_queue->tq_ref_count =
          1; /* for this the thread that built the queue */

      KMP_DEBUG_REF_CTS(("line %d gtid %d: Q %p alloc %d\n", __LINE__,
                         global_tid, new_queue, new_queue->tq_ref_count));

      __kmp_acquire_lock(&curr_queue->tq_link_lck, global_tid);

      // Make sure data structures are in consistent state before querying them
      // Seems to work without this for digital/alpha, needed for IBM/RS6000
      KMP_MB();

      new_queue->tq_next_child =
          CCAST(struct kmpc_task_queue_t *, curr_queue->tq_first_child);

      if (curr_queue->tq_first_child != NULL)
        curr_queue->tq_first_child->tq_prev_child = new_queue;

      curr_queue->tq_first_child = new_queue;

      __kmp_release_lock(&curr_queue->tq_link_lck, global_tid);
    }

    /* set up thunk stack only after code that determines curr_queue above */
    new_taskq_thunk->th_encl_thunk = tq->tq_curr_thunk[tid];
    tq->tq_curr_thunk[tid] = new_taskq_thunk;

    KF_DUMP(200, __kmp_dump_thunk_stack(tq->tq_curr_thunk[tid], global_tid));
  } else {
    new_taskq_thunk->th_encl_thunk = 0;
    new_queue->tq.tq_parent = NULL;
    new_queue->tq_first_child = NULL;
    new_queue->tq_next_child = NULL;
    new_queue->tq_prev_child = NULL;
    new_queue->tq_ref_count = 1;
  }

#ifdef KMP_DEBUG
  KF_TRACE(150, ("Creating TaskQ Task on (%d):\n", global_tid));
  KF_DUMP(150, __kmp_dump_thunk(tq, new_taskq_thunk, global_tid));

  if (in_parallel) {
    KF_TRACE(25,
             ("After TaskQ at %p Creation on (%d):\n", new_queue, global_tid));
  } else {
    KF_TRACE(25, ("After Serial TaskQ at %p Creation on (%d):\n", new_queue,
                  global_tid));
  }

  KF_DUMP(25, __kmp_dump_task_queue(tq, new_queue, global_tid));

  if (in_parallel) {
    KF_DUMP(50, __kmp_dump_task_queue_tree(tq, tq->tq_root, global_tid));
  }
#endif /* KMP_DEBUG */

  if (__kmp_env_consistency_check)
    __kmp_push_workshare(global_tid, ct_taskq, new_queue->tq_loc);

  KE_TRACE(10, ("__kmpc_taskq return (%d)\n", global_tid));

  return new_taskq_thunk;
}

/*  ends a taskq; last thread out destroys the queue  */

void __kmpc_end_taskq(ident_t *loc, kmp_int32 global_tid,
                      kmpc_thunk_t *taskq_thunk) {
#ifdef KMP_DEBUG
  kmp_int32 i;
#endif
  kmp_taskq_t *tq;
  int in_parallel;
  kmp_info_t *th;
  kmp_int32 is_outermost;
  kmpc_task_queue_t *queue;
  kmpc_thunk_t *thunk;
  int nproc;

  KE_TRACE(10, ("__kmpc_end_taskq called (%d)\n", global_tid));

  tq = &__kmp_threads[global_tid]->th.th_team->t.t_taskq;
  nproc = __kmp_threads[global_tid]->th.th_team->t.t_nproc;

  /* For the outermost taskq only, all but one thread will have taskq_thunk ==
   * NULL */
  queue = (taskq_thunk == NULL) ? tq->tq_root
                                : taskq_thunk->th.th_shareds->sv_queue;

  KE_TRACE(50, ("__kmpc_end_taskq queue=%p (%d) \n", queue, global_tid));
  is_outermost = (queue == tq->tq_root);
  in_parallel = (queue->tq_flags & TQF_PARALLEL_CONTEXT);

  if (in_parallel) {
    kmp_uint32 spins;

    /* this is just a safeguard to release the waiting threads if */
    /* the outermost taskq never queues a task                    */

    if (is_outermost && (KMP_MASTER_GTID(global_tid))) {
      if (tq->tq_global_flags & TQF_RELEASE_WORKERS) {
        /* no lock needed, workers are still in spin mode */
        tq->tq_global_flags &= ~TQF_RELEASE_WORKERS;

        __kmp_end_split_barrier(bs_plain_barrier, global_tid);
      }
    }

    /* keep dequeueing work until all tasks are queued and dequeued */

    do {
      /* wait until something is available to dequeue */
      KMP_INIT_YIELD(spins);

      while ((queue->tq_nfull == 0) && (queue->tq_taskq_slot == NULL) &&
             (!__kmp_taskq_has_any_children(queue)) &&
             (!(queue->tq_flags & TQF_ALL_TASKS_QUEUED))) {
        KMP_YIELD_WHEN(TRUE, spins);
      }

      /* check to see if we can execute tasks in the queue */
      while (((queue->tq_nfull != 0) || (queue->tq_taskq_slot != NULL)) &&
             (thunk = __kmp_find_task_in_queue(global_tid, queue)) != NULL) {
        KF_TRACE(50, ("Found thunk: %p in primary queue %p (%d)\n", thunk,
                      queue, global_tid));
        __kmp_execute_task_from_queue(tq, loc, global_tid, thunk, in_parallel);
      }

      /* see if work found can be found in a descendant queue */
      if ((__kmp_taskq_has_any_children(queue)) &&
          (thunk = __kmp_find_task_in_descendant_queue(global_tid, queue)) !=
              NULL) {

        KF_TRACE(50,
                 ("Stole thunk: %p in descendant queue: %p while waiting in "
                  "queue: %p (%d)\n",
                  thunk, thunk->th.th_shareds->sv_queue, queue, global_tid));

        __kmp_execute_task_from_queue(tq, loc, global_tid, thunk, in_parallel);
      }

    } while ((!(queue->tq_flags & TQF_ALL_TASKS_QUEUED)) ||
             (queue->tq_nfull != 0));

    KF_TRACE(50, ("All tasks queued and dequeued in queue: %p (%d)\n", queue,
                  global_tid));

    /* wait while all tasks are not finished and more work found
       in descendant queues */

    while ((!__kmp_taskq_tasks_finished(queue)) &&
           (thunk = __kmp_find_task_in_descendant_queue(global_tid, queue)) !=
               NULL) {

      KF_TRACE(50, ("Stole thunk: %p in descendant queue: %p while waiting in "
                    "queue: %p (%d)\n",
                    thunk, thunk->th.th_shareds->sv_queue, queue, global_tid));

      __kmp_execute_task_from_queue(tq, loc, global_tid, thunk, in_parallel);
    }

    KF_TRACE(50, ("No work found in descendent queues or all work finished in "
                  "queue: %p (%d)\n",
                  queue, global_tid));

    if (!is_outermost) {
      /* need to return if NOWAIT present and not outermost taskq */

      if (queue->tq_flags & TQF_IS_NOWAIT) {
        __kmp_acquire_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);
        queue->tq_ref_count--;
        KMP_DEBUG_ASSERT(queue->tq_ref_count >= 0);
        __kmp_release_lock(&queue->tq.tq_parent->tq_link_lck, global_tid);

        KE_TRACE(
            10, ("__kmpc_end_taskq return for nowait case (%d)\n", global_tid));

        return;
      }

      __kmp_find_and_remove_finished_child_taskq(tq, global_tid, queue);

      /* WAIT until all tasks are finished and no child queues exist before
       * proceeding */
      KMP_INIT_YIELD(spins);

      while (!__kmp_taskq_tasks_finished(queue) ||
             __kmp_taskq_has_any_children(queue)) {
        thunk = __kmp_find_task_in_ancestor_queue(tq, global_tid, queue);

        if (thunk != NULL) {
          KF_TRACE(50,
                   ("Stole thunk: %p in ancestor queue: %p while waiting in "
                    "queue: %p (%d)\n",
                    thunk, thunk->th.th_shareds->sv_queue, queue, global_tid));
          __kmp_execute_task_from_queue(tq, loc, global_tid, thunk,
                                        in_parallel);
        }

        KMP_YIELD_WHEN(thunk == NULL, spins);

        __kmp_find_and_remove_finished_child_taskq(tq, global_tid, queue);
      }

      __kmp_acquire_lock(&queue->tq_queue_lck, global_tid);
      if (!(queue->tq_flags & TQF_DEALLOCATED)) {
        queue->tq_flags |= TQF_DEALLOCATED;
      }
      __kmp_release_lock(&queue->tq_queue_lck, global_tid);

      /* only the allocating thread can deallocate the queue */
      if (taskq_thunk != NULL) {
        __kmp_remove_queue_from_tree(tq, global_tid, queue, TRUE);
      }

      KE_TRACE(
          10,
          ("__kmpc_end_taskq return for non_outermost queue, wait case (%d)\n",
           global_tid));

      return;
    }

    // Outermost Queue: steal work from descendants until all tasks are finished

    KMP_INIT_YIELD(spins);

    while (!__kmp_taskq_tasks_finished(queue)) {
      thunk = __kmp_find_task_in_descendant_queue(global_tid, queue);

      if (thunk != NULL) {
        KF_TRACE(50,
                 ("Stole thunk: %p in descendant queue: %p while waiting in "
                  "queue: %p (%d)\n",
                  thunk, thunk->th.th_shareds->sv_queue, queue, global_tid));

        __kmp_execute_task_from_queue(tq, loc, global_tid, thunk, in_parallel);
      }

      KMP_YIELD_WHEN(thunk == NULL, spins);
    }

    /* Need this barrier to prevent destruction of queue before threads have all
     * executed above code */
    /* This may need to be done earlier when NOWAIT is implemented for the
     * outermost level */

    if (!__kmp_barrier(bs_plain_barrier, global_tid, TRUE, 0, NULL, NULL)) {
      /* the queue->tq_flags & TQF_IS_NOWAIT case is not yet handled here;   */
      /* for right now, everybody waits, and the master thread destroys the  */
      /* remaining queues.                                                   */

      __kmp_remove_all_child_taskq(tq, global_tid, queue);

      /* Now destroy the root queue */
      KF_TRACE(100, ("T#%d Before Deletion of top-level TaskQ at %p:\n",
                     global_tid, queue));
      KF_DUMP(100, __kmp_dump_task_queue(tq, queue, global_tid));

#ifdef KMP_DEBUG
      /*  the root queue entry  */
      KMP_DEBUG_ASSERT((queue->tq.tq_parent == NULL) &&
                       (queue->tq_next_child == NULL));

      /*  children must all be gone by now because of barrier above */
      KMP_DEBUG_ASSERT(queue->tq_first_child == NULL);

      for (i = 0; i < nproc; i++) {
        KMP_DEBUG_ASSERT(queue->tq_th_thunks[i].ai_data == 0);
      }

      for (i = 0, thunk = queue->tq_free_thunks; thunk != NULL;
           i++, thunk = thunk->th.th_next_free)
        ;

      KMP_DEBUG_ASSERT(i ==
                       queue->tq_nslots + (nproc * __KMP_TASKQ_THUNKS_PER_TH));

      for (i = 0; i < nproc; i++) {
        KMP_DEBUG_ASSERT(!tq->tq_curr_thunk[i]);
      }
#endif
      /*  unlink the root queue entry  */
      tq->tq_root = NULL;

      /*  release storage for root queue entry  */
      KF_TRACE(50, ("After Deletion of top-level TaskQ at %p on (%d):\n", queue,
                    global_tid));

      queue->tq_flags |= TQF_DEALLOCATED;
      __kmp_free_taskq(tq, queue, in_parallel, global_tid);

      KF_DUMP(50, __kmp_dump_task_queue_tree(tq, tq->tq_root, global_tid));

      /* release the workers now that the data structures are up to date */
      __kmp_end_split_barrier(bs_plain_barrier, global_tid);
    }

    th = __kmp_threads[global_tid];

    /* Reset ORDERED SECTION to parallel version */
    th->th.th_dispatch->th_deo_fcn = 0;

    /* Reset ORDERED SECTION to parallel version */
    th->th.th_dispatch->th_dxo_fcn = 0;
  } else {
    /* in serial execution context, dequeue the last task  */
    /* and execute it, if there were any tasks encountered */

    if (queue->tq_nfull > 0) {
      KMP_DEBUG_ASSERT(queue->tq_nfull == 1);

      thunk = __kmp_dequeue_task(global_tid, queue, in_parallel);

      if (queue->tq_flags & TQF_IS_LAST_TASK) {
        /* TQF_IS_LASTPRIVATE, one thing in queue, __kmpc_end_taskq_task() */
        /* has been run so this is last task, run with TQF_IS_LAST_TASK so */
        /* instrumentation does copy-out.                                  */

        /* no need for test_then_or call since already locked */
        thunk->th_flags |= TQF_IS_LAST_TASK;
      }

      KF_TRACE(50, ("T#%d found thunk: %p in serial queue: %p\n", global_tid,
                    thunk, queue));

      __kmp_execute_task_from_queue(tq, loc, global_tid, thunk, in_parallel);
    }

    // destroy the unattached serial queue now that there is no more work to do
    KF_TRACE(100, ("Before Deletion of Serialized TaskQ at %p on (%d):\n",
                   queue, global_tid));
    KF_DUMP(100, __kmp_dump_task_queue(tq, queue, global_tid));

#ifdef KMP_DEBUG
    i = 0;
    for (thunk = queue->tq_free_thunks; thunk != NULL;
         thunk = thunk->th.th_next_free)
      ++i;
    KMP_DEBUG_ASSERT(i == queue->tq_nslots + 1);
#endif
    /*  release storage for unattached serial queue  */
    KF_TRACE(50,
             ("Serialized TaskQ at %p deleted on (%d).\n", queue, global_tid));

    queue->tq_flags |= TQF_DEALLOCATED;
    __kmp_free_taskq(tq, queue, in_parallel, global_tid);
  }

  KE_TRACE(10, ("__kmpc_end_taskq return (%d)\n", global_tid));
}

/*  Enqueues a task for thunk previously created by __kmpc_task_buffer. */
/*  Returns nonzero if just filled up queue  */

kmp_int32 __kmpc_task(ident_t *loc, kmp_int32 global_tid, kmpc_thunk_t *thunk) {
  kmp_int32 ret;
  kmpc_task_queue_t *queue;
  int in_parallel;
  kmp_taskq_t *tq;

  KE_TRACE(10, ("__kmpc_task called (%d)\n", global_tid));

  KMP_DEBUG_ASSERT(!(thunk->th_flags &
                     TQF_TASKQ_TASK)); /*  thunk->th_task is a regular task  */

  tq = &__kmp_threads[global_tid]->th.th_team->t.t_taskq;
  queue = thunk->th.th_shareds->sv_queue;
  in_parallel = (queue->tq_flags & TQF_PARALLEL_CONTEXT);

  if (in_parallel && (thunk->th_flags & TQF_IS_ORDERED))
    thunk->th_tasknum = ++queue->tq_tasknum_queuing;

  /* For serial execution dequeue the preceding task and execute it, if one
   * exists */
  /* This cannot be the last task.  That one is handled in __kmpc_end_taskq */

  if (!in_parallel && queue->tq_nfull > 0) {
    kmpc_thunk_t *prev_thunk;

    KMP_DEBUG_ASSERT(queue->tq_nfull == 1);

    prev_thunk = __kmp_dequeue_task(global_tid, queue, in_parallel);

    KF_TRACE(50, ("T#%d found thunk: %p in serial queue: %p\n", global_tid,
                  prev_thunk, queue));

    __kmp_execute_task_from_queue(tq, loc, global_tid, prev_thunk, in_parallel);
  }

  /* The instrumentation sequence is:  __kmpc_task_buffer(), initialize private
     variables, __kmpc_task().  The __kmpc_task_buffer routine checks that the
     task queue is not full and allocates a thunk (which is then passed to
     __kmpc_task()).  So, the enqueue below should never fail due to a full
     queue. */

  KF_TRACE(100, ("After enqueueing this Task on (%d):\n", global_tid));
  KF_DUMP(100, __kmp_dump_thunk(tq, thunk, global_tid));

  ret = __kmp_enqueue_task(tq, global_tid, queue, thunk, in_parallel);

  KF_TRACE(100, ("Task Queue looks like this on (%d):\n", global_tid));
  KF_DUMP(100, __kmp_dump_task_queue(tq, queue, global_tid));

  KE_TRACE(10, ("__kmpc_task return (%d)\n", global_tid));

  return ret;
}

/*  enqueues a taskq_task for thunk previously created by __kmpc_taskq  */
/*  this should never be called unless in a parallel context            */

void __kmpc_taskq_task(ident_t *loc, kmp_int32 global_tid, kmpc_thunk_t *thunk,
                       kmp_int32 status) {
  kmpc_task_queue_t *queue;
  kmp_taskq_t *tq = &__kmp_threads[global_tid]->th.th_team->t.t_taskq;
  int tid = __kmp_tid_from_gtid(global_tid);

  KE_TRACE(10, ("__kmpc_taskq_task called (%d)\n", global_tid));
  KF_TRACE(100, ("TaskQ Task argument thunk on (%d):\n", global_tid));
  KF_DUMP(100, __kmp_dump_thunk(tq, thunk, global_tid));

  queue = thunk->th.th_shareds->sv_queue;

  if (__kmp_env_consistency_check)
    __kmp_pop_workshare(global_tid, ct_taskq, loc);

  /*  thunk->th_task is the taskq_task  */
  KMP_DEBUG_ASSERT(thunk->th_flags & TQF_TASKQ_TASK);

  /*  not supposed to call __kmpc_taskq_task if it's already enqueued  */
  KMP_DEBUG_ASSERT(queue->tq_taskq_slot == NULL);

  /* dequeue taskq thunk from curr_thunk stack */
  tq->tq_curr_thunk[tid] = thunk->th_encl_thunk;
  thunk->th_encl_thunk = NULL;

  KF_DUMP(200, __kmp_dump_thunk_stack(tq->tq_curr_thunk[tid], global_tid));

  thunk->th_status = status;

  // Flush thunk->th_status before taskq_task enqueued to avoid race condition
  KMP_MB();

  /* enqueue taskq_task in thunk into special slot in queue     */
  /* GEH - probably don't need to lock taskq slot since only one */
  /*       thread enqueues & already a lock set at dequeue point */

  queue->tq_taskq_slot = thunk;

  KE_TRACE(10, ("__kmpc_taskq_task return (%d)\n", global_tid));
}

/* ends a taskq_task; done generating tasks  */

void __kmpc_end_taskq_task(ident_t *loc, kmp_int32 global_tid,
                           kmpc_thunk_t *thunk) {
  kmp_taskq_t *tq;
  kmpc_task_queue_t *queue;
  int in_parallel;
  int tid;

  KE_TRACE(10, ("__kmpc_end_taskq_task called (%d)\n", global_tid));

  tq = &__kmp_threads[global_tid]->th.th_team->t.t_taskq;
  queue = thunk->th.th_shareds->sv_queue;
  in_parallel = (queue->tq_flags & TQF_PARALLEL_CONTEXT);
  tid = __kmp_tid_from_gtid(global_tid);

  if (__kmp_env_consistency_check)
    __kmp_pop_workshare(global_tid, ct_taskq, loc);

  if (in_parallel) {
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
    KMP_TEST_THEN_OR32(RCAST(volatile kmp_uint32 *, &queue->tq_flags),
                       TQF_ALL_TASKS_QUEUED);
#else
    {
      __kmp_acquire_lock(&queue->tq_queue_lck, global_tid);

      // Make sure data structures are in consistent state before querying them
      // Seems to work without this for digital/alpha, needed for IBM/RS6000
      KMP_MB();

      queue->tq_flags |= TQF_ALL_TASKS_QUEUED;
      __kmp_release_lock(&queue->tq_queue_lck, global_tid);
    }
#endif
  }

  if (thunk->th_flags & TQF_IS_LASTPRIVATE) {
    /* Normally, __kmp_find_task_in_queue() refuses to schedule the last task in
       the queue if TQF_IS_LASTPRIVATE so we can positively identify that last
       task and run it with its TQF_IS_LAST_TASK bit turned on in th_flags.
       When __kmpc_end_taskq_task() is called we are done generating all the
       tasks, so we know the last one in the queue is the lastprivate task.
       Mark the queue as having gotten to this state via tq_flags &
       TQF_IS_LAST_TASK; when that task actually executes mark it via th_flags &
       TQF_IS_LAST_TASK (this th_flags bit signals the instrumented code to do
       copy-outs after execution). */
    if (!in_parallel) {
      /* No synchronization needed for serial context */
      queue->tq_flags |= TQF_IS_LAST_TASK;
    } else {
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
      KMP_TEST_THEN_OR32(RCAST(volatile kmp_uint32 *, &queue->tq_flags),
                         TQF_IS_LAST_TASK);
#else
      {
        __kmp_acquire_lock(&queue->tq_queue_lck, global_tid);

        // Make sure data structures in consistent state before querying them
        // Seems to work without this for digital/alpha, needed for IBM/RS6000
        KMP_MB();

        queue->tq_flags |= TQF_IS_LAST_TASK;
        __kmp_release_lock(&queue->tq_queue_lck, global_tid);
      }
#endif
      /* to prevent race condition where last task is dequeued but */
      /* flag isn't visible yet (not sure about this)              */
      KMP_MB();
    }
  }

  /* dequeue taskq thunk from curr_thunk stack */
  if (in_parallel) {
    tq->tq_curr_thunk[tid] = thunk->th_encl_thunk;
    thunk->th_encl_thunk = NULL;

    KF_DUMP(200, __kmp_dump_thunk_stack(tq->tq_curr_thunk[tid], global_tid));
  }

  KE_TRACE(10, ("__kmpc_end_taskq_task return (%d)\n", global_tid));
}

/* returns thunk for a regular task based on taskq_thunk              */
/* (__kmpc_taskq_task does the analogous thing for a TQF_TASKQ_TASK)  */

kmpc_thunk_t *__kmpc_task_buffer(ident_t *loc, kmp_int32 global_tid,
                                 kmpc_thunk_t *taskq_thunk, kmpc_task_t task) {
  kmp_taskq_t *tq;
  kmpc_task_queue_t *queue;
  kmpc_thunk_t *new_thunk;
  int in_parallel;

  KE_TRACE(10, ("__kmpc_task_buffer called (%d)\n", global_tid));

  KMP_DEBUG_ASSERT(
      taskq_thunk->th_flags &
      TQF_TASKQ_TASK); /*  taskq_thunk->th_task is the taskq_task  */

  tq = &__kmp_threads[global_tid]->th.th_team->t.t_taskq;
  queue = taskq_thunk->th.th_shareds->sv_queue;
  in_parallel = (queue->tq_flags & TQF_PARALLEL_CONTEXT);

  /* The instrumentation sequence is:  __kmpc_task_buffer(), initialize private
     variables, __kmpc_task().  The __kmpc_task_buffer routine checks that the
     task queue is not full and allocates a thunk (which is then passed to
     __kmpc_task()).  So, we can pre-allocate a thunk here assuming it will be
     the next to be enqueued in __kmpc_task(). */

  new_thunk = __kmp_alloc_thunk(queue, in_parallel, global_tid);
  new_thunk->th.th_shareds =
      CCAST(kmpc_shared_vars_t *, queue->tq_shareds[0].ai_data);
  new_thunk->th_encl_thunk = NULL;
  new_thunk->th_task = task;

  /* GEH - shouldn't need to lock the read of tq_flags here */
  new_thunk->th_flags = queue->tq_flags & TQF_INTERFACE_FLAGS;

  new_thunk->th_status = 0;

  KMP_DEBUG_ASSERT(!(new_thunk->th_flags & TQF_TASKQ_TASK));

  KF_TRACE(100, ("Creating Regular Task on (%d):\n", global_tid));
  KF_DUMP(100, __kmp_dump_thunk(tq, new_thunk, global_tid));

  KE_TRACE(10, ("__kmpc_task_buffer return (%d)\n", global_tid));

  return new_thunk;
}
