/*
 * kmp_taskdeps.h
 */


//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


#ifndef KMP_TASKDEPS_H
#define KMP_TASKDEPS_H

#include "kmp.h"

#if OMP_40_ENABLED

#define KMP_ACQUIRE_DEPNODE(gtid, n) __kmp_acquire_lock(&(n)->dn.lock, (gtid))
#define KMP_RELEASE_DEPNODE(gtid, n) __kmp_release_lock(&(n)->dn.lock, (gtid))

static inline void __kmp_node_deref(kmp_info_t *thread, kmp_depnode_t *node) {
  if (!node)
    return;

  kmp_int32 n = KMP_ATOMIC_DEC(&node->dn.nrefs) - 1;
  if (n == 0) {
    KMP_ASSERT(node->dn.nrefs == 0);
#if USE_FAST_MEMORY
    __kmp_fast_free(thread, node);
#else
    __kmp_thread_free(thread, node);
#endif
  }
}

static inline void __kmp_depnode_list_free(kmp_info_t *thread,
                                           kmp_depnode_list *list) {
  kmp_depnode_list *next;

  for (; list; list = next) {
    next = list->next;

    __kmp_node_deref(thread, list->node);
#if USE_FAST_MEMORY
    __kmp_fast_free(thread, list);
#else
    __kmp_thread_free(thread, list);
#endif
  }
}

static inline void __kmp_dephash_free_entries(kmp_info_t *thread,
                                              kmp_dephash_t *h) {
  for (size_t i = 0; i < h->size; i++) {
    if (h->buckets[i]) {
      kmp_dephash_entry_t *next;
      for (kmp_dephash_entry_t *entry = h->buckets[i]; entry; entry = next) {
        next = entry->next_in_bucket;
        __kmp_depnode_list_free(thread, entry->last_ins);
        __kmp_depnode_list_free(thread, entry->last_mtxs);
        __kmp_node_deref(thread, entry->last_out);
        if (entry->mtx_lock) {
          __kmp_destroy_lock(entry->mtx_lock);
          __kmp_free(entry->mtx_lock);
        }
#if USE_FAST_MEMORY
        __kmp_fast_free(thread, entry);
#else
        __kmp_thread_free(thread, entry);
#endif
      }
      h->buckets[i] = 0;
    }
  }
}

static inline void __kmp_dephash_free(kmp_info_t *thread, kmp_dephash_t *h) {
  __kmp_dephash_free_entries(thread, h);
#if USE_FAST_MEMORY
  __kmp_fast_free(thread, h);
#else
  __kmp_thread_free(thread, h);
#endif
}

static inline void __kmp_release_deps(kmp_int32 gtid, kmp_taskdata_t *task) {
  kmp_info_t *thread = __kmp_threads[gtid];
  kmp_depnode_t *node = task->td_depnode;

  if (task->td_dephash) {
    KA_TRACE(
        40, ("__kmp_release_deps: T#%d freeing dependencies hash of task %p.\n",
             gtid, task));
    __kmp_dephash_free(thread, task->td_dephash);
    task->td_dephash = NULL;
  }

  if (!node)
    return;

  KA_TRACE(20, ("__kmp_release_deps: T#%d notifying successors of task %p.\n",
                gtid, task));

  KMP_ACQUIRE_DEPNODE(gtid, node);
  node->dn.task =
      NULL; // mark this task as finished, so no new dependencies are generated
  KMP_RELEASE_DEPNODE(gtid, node);

  kmp_depnode_list_t *next;
  for (kmp_depnode_list_t *p = node->dn.successors; p; p = next) {
    kmp_depnode_t *successor = p->node;
    kmp_int32 npredecessors = KMP_ATOMIC_DEC(&successor->dn.npredecessors) - 1;

    // successor task can be NULL for wait_depends or because deps are still
    // being processed
    if (npredecessors == 0) {
      KMP_MB();
      if (successor->dn.task) {
        KA_TRACE(20, ("__kmp_release_deps: T#%d successor %p of %p scheduled "
                      "for execution.\n",
                      gtid, successor->dn.task, task));
        __kmp_omp_task(gtid, successor->dn.task, false);
      }
    }

    next = p->next;
    __kmp_node_deref(thread, p->node);
#if USE_FAST_MEMORY
    __kmp_fast_free(thread, p);
#else
    __kmp_thread_free(thread, p);
#endif
  }

  __kmp_node_deref(thread, node);

  KA_TRACE(
      20,
      ("__kmp_release_deps: T#%d all successors of %p notified of completion\n",
       gtid, task));
}

#endif // OMP_40_ENABLED

#endif // KMP_TASKDEPS_H
