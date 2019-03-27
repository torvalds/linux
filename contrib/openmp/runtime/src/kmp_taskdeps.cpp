/*
 * kmp_taskdeps.cpp
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

//#define KMP_SUPPORT_GRAPH_OUTPUT 1

#include "kmp.h"
#include "kmp_io.h"
#include "kmp_wait_release.h"
#include "kmp_taskdeps.h"
#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

#if OMP_40_ENABLED

// TODO: Improve memory allocation? keep a list of pre-allocated structures?
// allocate in blocks? re-use list finished list entries?
// TODO: don't use atomic ref counters for stack-allocated nodes.
// TODO: find an alternate to atomic refs for heap-allocated nodes?
// TODO: Finish graph output support
// TODO: kmp_lock_t seems a tad to big (and heavy weight) for this. Check other
// runtime locks
// TODO: Any ITT support needed?

#ifdef KMP_SUPPORT_GRAPH_OUTPUT
static std::atomic<kmp_int32> kmp_node_id_seed = ATOMIC_VAR_INIT(0);
#endif

static void __kmp_init_node(kmp_depnode_t *node) {
  node->dn.successors = NULL;
  node->dn.task = NULL; // will point to the rigth task
  // once dependences have been processed
  for (int i = 0; i < MAX_MTX_DEPS; ++i)
    node->dn.mtx_locks[i] = NULL;
  node->dn.mtx_num_locks = 0;
  __kmp_init_lock(&node->dn.lock);
  KMP_ATOMIC_ST_RLX(&node->dn.nrefs, 1); // init creates the first reference
#ifdef KMP_SUPPORT_GRAPH_OUTPUT
  node->dn.id = KMP_ATOMIC_INC(&kmp_node_id_seed);
#endif
}

static inline kmp_depnode_t *__kmp_node_ref(kmp_depnode_t *node) {
  KMP_ATOMIC_INC(&node->dn.nrefs);
  return node;
}

enum { KMP_DEPHASH_OTHER_SIZE = 97, KMP_DEPHASH_MASTER_SIZE = 997 };

static inline kmp_int32 __kmp_dephash_hash(kmp_intptr_t addr, size_t hsize) {
  // TODO alternate to try: set = (((Addr64)(addrUsefulBits * 9.618)) %
  // m_num_sets );
  return ((addr >> 6) ^ (addr >> 2)) % hsize;
}

static kmp_dephash_t *__kmp_dephash_create(kmp_info_t *thread,
                                           kmp_taskdata_t *current_task) {
  kmp_dephash_t *h;

  size_t h_size;

  if (current_task->td_flags.tasktype == TASK_IMPLICIT)
    h_size = KMP_DEPHASH_MASTER_SIZE;
  else
    h_size = KMP_DEPHASH_OTHER_SIZE;

  kmp_int32 size =
      h_size * sizeof(kmp_dephash_entry_t *) + sizeof(kmp_dephash_t);

#if USE_FAST_MEMORY
  h = (kmp_dephash_t *)__kmp_fast_allocate(thread, size);
#else
  h = (kmp_dephash_t *)__kmp_thread_malloc(thread, size);
#endif
  h->size = h_size;

#ifdef KMP_DEBUG
  h->nelements = 0;
  h->nconflicts = 0;
#endif
  h->buckets = (kmp_dephash_entry **)(h + 1);

  for (size_t i = 0; i < h_size; i++)
    h->buckets[i] = 0;

  return h;
}

#define ENTRY_LAST_INS 0
#define ENTRY_LAST_MTXS 1

static kmp_dephash_entry *
__kmp_dephash_find(kmp_info_t *thread, kmp_dephash_t *h, kmp_intptr_t addr) {
  kmp_int32 bucket = __kmp_dephash_hash(addr, h->size);

  kmp_dephash_entry_t *entry;
  for (entry = h->buckets[bucket]; entry; entry = entry->next_in_bucket)
    if (entry->addr == addr)
      break;

  if (entry == NULL) {
// create entry. This is only done by one thread so no locking required
#if USE_FAST_MEMORY
    entry = (kmp_dephash_entry_t *)__kmp_fast_allocate(
        thread, sizeof(kmp_dephash_entry_t));
#else
    entry = (kmp_dephash_entry_t *)__kmp_thread_malloc(
        thread, sizeof(kmp_dephash_entry_t));
#endif
    entry->addr = addr;
    entry->last_out = NULL;
    entry->last_ins = NULL;
    entry->last_mtxs = NULL;
    entry->last_flag = ENTRY_LAST_INS;
    entry->mtx_lock = NULL;
    entry->next_in_bucket = h->buckets[bucket];
    h->buckets[bucket] = entry;
#ifdef KMP_DEBUG
    h->nelements++;
    if (entry->next_in_bucket)
      h->nconflicts++;
#endif
  }
  return entry;
}

static kmp_depnode_list_t *__kmp_add_node(kmp_info_t *thread,
                                          kmp_depnode_list_t *list,
                                          kmp_depnode_t *node) {
  kmp_depnode_list_t *new_head;

#if USE_FAST_MEMORY
  new_head = (kmp_depnode_list_t *)__kmp_fast_allocate(
      thread, sizeof(kmp_depnode_list_t));
#else
  new_head = (kmp_depnode_list_t *)__kmp_thread_malloc(
      thread, sizeof(kmp_depnode_list_t));
#endif

  new_head->node = __kmp_node_ref(node);
  new_head->next = list;

  return new_head;
}

static inline void __kmp_track_dependence(kmp_depnode_t *source,
                                          kmp_depnode_t *sink,
                                          kmp_task_t *sink_task) {
#ifdef KMP_SUPPORT_GRAPH_OUTPUT
  kmp_taskdata_t *task_source = KMP_TASK_TO_TASKDATA(source->dn.task);
  // do not use sink->dn.task as that is only filled after the dependencies
  // are already processed!
  kmp_taskdata_t *task_sink = KMP_TASK_TO_TASKDATA(sink_task);

  __kmp_printf("%d(%s) -> %d(%s)\n", source->dn.id,
               task_source->td_ident->psource, sink->dn.id,
               task_sink->td_ident->psource);
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
  /* OMPT tracks dependences between task (a=source, b=sink) in which
     task a blocks the execution of b through the ompt_new_dependence_callback
     */
  if (ompt_enabled.ompt_callback_task_dependence) {
    kmp_taskdata_t *task_source = KMP_TASK_TO_TASKDATA(source->dn.task);
    kmp_taskdata_t *task_sink = KMP_TASK_TO_TASKDATA(sink_task);

    ompt_callbacks.ompt_callback(ompt_callback_task_dependence)(
        &(task_source->ompt_task_info.task_data),
        &(task_sink->ompt_task_info.task_data));
  }
#endif /* OMPT_SUPPORT && OMPT_OPTIONAL */
}

static inline kmp_int32
__kmp_depnode_link_successor(kmp_int32 gtid, kmp_info_t *thread,
                             kmp_task_t *task, kmp_depnode_t *node,
                             kmp_depnode_list_t *plist) {
  if (!plist)
    return 0;
  kmp_int32 npredecessors = 0;
  // link node as successor of list elements
  for (kmp_depnode_list_t *p = plist; p; p = p->next) {
    kmp_depnode_t *dep = p->node;
    if (dep->dn.task) {
      KMP_ACQUIRE_DEPNODE(gtid, dep);
      if (dep->dn.task) {
        __kmp_track_dependence(dep, node, task);
        dep->dn.successors = __kmp_add_node(thread, dep->dn.successors, node);
        KA_TRACE(40, ("__kmp_process_deps: T#%d adding dependence from %p to "
                      "%p\n",
                      gtid, KMP_TASK_TO_TASKDATA(dep->dn.task),
                      KMP_TASK_TO_TASKDATA(task)));
        npredecessors++;
      }
      KMP_RELEASE_DEPNODE(gtid, dep);
    }
  }
  return npredecessors;
}

static inline kmp_int32 __kmp_depnode_link_successor(kmp_int32 gtid,
                                                     kmp_info_t *thread,
                                                     kmp_task_t *task,
                                                     kmp_depnode_t *source,
                                                     kmp_depnode_t *sink) {
  if (!sink)
    return 0;
  kmp_int32 npredecessors = 0;
  if (sink->dn.task) {
    // synchronously add source to sink' list of successors
    KMP_ACQUIRE_DEPNODE(gtid, sink);
    if (sink->dn.task) {
      __kmp_track_dependence(sink, source, task);
      sink->dn.successors = __kmp_add_node(thread, sink->dn.successors, source);
      KA_TRACE(40, ("__kmp_process_deps: T#%d adding dependence from %p to "
                    "%p\n",
                    gtid, KMP_TASK_TO_TASKDATA(sink->dn.task),
                    KMP_TASK_TO_TASKDATA(task)));
      npredecessors++;
    }
    KMP_RELEASE_DEPNODE(gtid, sink);
  }
  return npredecessors;
}

template <bool filter>
static inline kmp_int32
__kmp_process_deps(kmp_int32 gtid, kmp_depnode_t *node, kmp_dephash_t *hash,
                   bool dep_barrier, kmp_int32 ndeps,
                   kmp_depend_info_t *dep_list, kmp_task_t *task) {
  KA_TRACE(30, ("__kmp_process_deps<%d>: T#%d processing %d dependencies : "
                "dep_barrier = %d\n",
                filter, gtid, ndeps, dep_barrier));

  kmp_info_t *thread = __kmp_threads[gtid];
  kmp_int32 npredecessors = 0;
  for (kmp_int32 i = 0; i < ndeps; i++) {
    const kmp_depend_info_t *dep = &dep_list[i];

    if (filter && dep->base_addr == 0)
      continue; // skip filtered entries

    kmp_dephash_entry_t *info =
        __kmp_dephash_find(thread, hash, dep->base_addr);
    kmp_depnode_t *last_out = info->last_out;
    kmp_depnode_list_t *last_ins = info->last_ins;
    kmp_depnode_list_t *last_mtxs = info->last_mtxs;

    if (dep->flags.out) { // out --> clean lists of ins and mtxs if any
      if (last_ins || last_mtxs) {
        if (info->last_flag == ENTRY_LAST_INS) { // INS were last
          npredecessors +=
              __kmp_depnode_link_successor(gtid, thread, task, node, last_ins);
        } else { // MTXS were last
          npredecessors +=
              __kmp_depnode_link_successor(gtid, thread, task, node, last_mtxs);
        }
        __kmp_depnode_list_free(thread, last_ins);
        __kmp_depnode_list_free(thread, last_mtxs);
        info->last_ins = NULL;
        info->last_mtxs = NULL;
      } else {
        npredecessors +=
            __kmp_depnode_link_successor(gtid, thread, task, node, last_out);
      }
      __kmp_node_deref(thread, last_out);
      if (dep_barrier) {
        // if this is a sync point in the serial sequence, then the previous
        // outputs are guaranteed to be completed after the execution of this
        // task so the previous output nodes can be cleared.
        info->last_out = NULL;
      } else {
        info->last_out = __kmp_node_ref(node);
      }
    } else if (dep->flags.in) {
      // in --> link node to either last_out or last_mtxs, clean earlier deps
      if (last_mtxs) {
        npredecessors +=
            __kmp_depnode_link_successor(gtid, thread, task, node, last_mtxs);
        __kmp_node_deref(thread, last_out);
        info->last_out = NULL;
        if (info->last_flag == ENTRY_LAST_MTXS && last_ins) { // MTXS were last
          // clean old INS before creating new list
          __kmp_depnode_list_free(thread, last_ins);
          info->last_ins = NULL;
        }
      } else {
        // link node as successor of the last_out if any
        npredecessors +=
            __kmp_depnode_link_successor(gtid, thread, task, node, last_out);
      }
      info->last_flag = ENTRY_LAST_INS;
      info->last_ins = __kmp_add_node(thread, info->last_ins, node);
    } else {
      KMP_DEBUG_ASSERT(dep->flags.mtx == 1);
      // mtx --> link node to either last_out or last_ins, clean earlier deps
      if (last_ins) {
        npredecessors +=
            __kmp_depnode_link_successor(gtid, thread, task, node, last_ins);
        __kmp_node_deref(thread, last_out);
        info->last_out = NULL;
        if (info->last_flag == ENTRY_LAST_INS && last_mtxs) { // INS were last
          // clean old MTXS before creating new list
          __kmp_depnode_list_free(thread, last_mtxs);
          info->last_mtxs = NULL;
        }
      } else {
        // link node as successor of the last_out if any
        npredecessors +=
            __kmp_depnode_link_successor(gtid, thread, task, node, last_out);
      }
      info->last_flag = ENTRY_LAST_MTXS;
      info->last_mtxs = __kmp_add_node(thread, info->last_mtxs, node);
      if (info->mtx_lock == NULL) {
        info->mtx_lock = (kmp_lock_t *)__kmp_allocate(sizeof(kmp_lock_t));
        __kmp_init_lock(info->mtx_lock);
      }
      KMP_DEBUG_ASSERT(node->dn.mtx_num_locks < MAX_MTX_DEPS);
      kmp_int32 m;
      // Save lock in node's array
      for (m = 0; m < MAX_MTX_DEPS; ++m) {
        // sort pointers in decreasing order to avoid potential livelock
        if (node->dn.mtx_locks[m] < info->mtx_lock) {
          KMP_DEBUG_ASSERT(node->dn.mtx_locks[node->dn.mtx_num_locks] == NULL);
          for (int n = node->dn.mtx_num_locks; n > m; --n) {
            // shift right all lesser non-NULL pointers
            KMP_DEBUG_ASSERT(node->dn.mtx_locks[n - 1] != NULL);
            node->dn.mtx_locks[n] = node->dn.mtx_locks[n - 1];
          }
          node->dn.mtx_locks[m] = info->mtx_lock;
          break;
        }
      }
      KMP_DEBUG_ASSERT(m < MAX_MTX_DEPS); // must break from loop
      node->dn.mtx_num_locks++;
    }
  }
  KA_TRACE(30, ("__kmp_process_deps<%d>: T#%d found %d predecessors\n", filter,
                gtid, npredecessors));
  return npredecessors;
}

#define NO_DEP_BARRIER (false)
#define DEP_BARRIER (true)

// returns true if the task has any outstanding dependence
static bool __kmp_check_deps(kmp_int32 gtid, kmp_depnode_t *node,
                             kmp_task_t *task, kmp_dephash_t *hash,
                             bool dep_barrier, kmp_int32 ndeps,
                             kmp_depend_info_t *dep_list,
                             kmp_int32 ndeps_noalias,
                             kmp_depend_info_t *noalias_dep_list) {
  int i, n_mtxs = 0;
#if KMP_DEBUG
  kmp_taskdata_t *taskdata = KMP_TASK_TO_TASKDATA(task);
#endif
  KA_TRACE(20, ("__kmp_check_deps: T#%d checking dependencies for task %p : %d "
                "possibly aliased dependencies, %d non-aliased depedencies : "
                "dep_barrier=%d .\n",
                gtid, taskdata, ndeps, ndeps_noalias, dep_barrier));

  // Filter deps in dep_list
  // TODO: Different algorithm for large dep_list ( > 10 ? )
  for (i = 0; i < ndeps; i++) {
    if (dep_list[i].base_addr != 0) {
      for (int j = i + 1; j < ndeps; j++) {
        if (dep_list[i].base_addr == dep_list[j].base_addr) {
          dep_list[i].flags.in |= dep_list[j].flags.in;
          dep_list[i].flags.out |=
              (dep_list[j].flags.out ||
               (dep_list[i].flags.in && dep_list[j].flags.mtx) ||
               (dep_list[i].flags.mtx && dep_list[j].flags.in));
          dep_list[i].flags.mtx =
              dep_list[i].flags.mtx | dep_list[j].flags.mtx &&
              !dep_list[i].flags.out;
          dep_list[j].base_addr = 0; // Mark j element as void
        }
      }
      if (dep_list[i].flags.mtx) {
        // limit number of mtx deps to MAX_MTX_DEPS per node
        if (n_mtxs < MAX_MTX_DEPS && task != NULL) {
          ++n_mtxs;
        } else {
          dep_list[i].flags.in = 1; // downgrade mutexinoutset to inout
          dep_list[i].flags.out = 1;
          dep_list[i].flags.mtx = 0;
        }
      }
    }
  }

  // doesn't need to be atomic as no other thread is going to be accessing this
  // node just yet.
  // npredecessors is set -1 to ensure that none of the releasing tasks queues
  // this task before we have finished processing all the dependencies
  node->dn.npredecessors = -1;

  // used to pack all npredecessors additions into a single atomic operation at
  // the end
  int npredecessors;

  npredecessors = __kmp_process_deps<true>(gtid, node, hash, dep_barrier, ndeps,
                                           dep_list, task);
  npredecessors += __kmp_process_deps<false>(
      gtid, node, hash, dep_barrier, ndeps_noalias, noalias_dep_list, task);

  node->dn.task = task;
  KMP_MB();

  // Account for our initial fake value
  npredecessors++;

  // Update predecessors and obtain current value to check if there are still
  // any outstandig dependences (some tasks may have finished while we processed
  // the dependences)
  npredecessors =
      node->dn.npredecessors.fetch_add(npredecessors) + npredecessors;

  KA_TRACE(20, ("__kmp_check_deps: T#%d found %d predecessors for task %p \n",
                gtid, npredecessors, taskdata));

  // beyond this point the task could be queued (and executed) by a releasing
  // task...
  return npredecessors > 0 ? true : false;
}

/*!
@ingroup TASKING
@param loc_ref location of the original task directive
@param gtid Global Thread ID of encountering thread
@param new_task task thunk allocated by __kmp_omp_task_alloc() for the ''new
task''
@param ndeps Number of depend items with possible aliasing
@param dep_list List of depend items with possible aliasing
@param ndeps_noalias Number of depend items with no aliasing
@param noalias_dep_list List of depend items with no aliasing

@return Returns either TASK_CURRENT_NOT_QUEUED if the current task was not
suspendend and queued, or TASK_CURRENT_QUEUED if it was suspended and queued

Schedule a non-thread-switchable task with dependences for execution
*/
kmp_int32 __kmpc_omp_task_with_deps(ident_t *loc_ref, kmp_int32 gtid,
                                    kmp_task_t *new_task, kmp_int32 ndeps,
                                    kmp_depend_info_t *dep_list,
                                    kmp_int32 ndeps_noalias,
                                    kmp_depend_info_t *noalias_dep_list) {

  kmp_taskdata_t *new_taskdata = KMP_TASK_TO_TASKDATA(new_task);
  KA_TRACE(10, ("__kmpc_omp_task_with_deps(enter): T#%d loc=%p task=%p\n", gtid,
                loc_ref, new_taskdata));

  kmp_info_t *thread = __kmp_threads[gtid];
  kmp_taskdata_t *current_task = thread->th.th_current_task;

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    OMPT_STORE_RETURN_ADDRESS(gtid);
    if (!current_task->ompt_task_info.frame.enter_frame.ptr)
      current_task->ompt_task_info.frame.enter_frame.ptr =
          OMPT_GET_FRAME_ADDRESS(0);
    if (ompt_enabled.ompt_callback_task_create) {
      ompt_data_t task_data = ompt_data_none;
      ompt_callbacks.ompt_callback(ompt_callback_task_create)(
          current_task ? &(current_task->ompt_task_info.task_data) : &task_data,
          current_task ? &(current_task->ompt_task_info.frame) : NULL,
          &(new_taskdata->ompt_task_info.task_data),
          ompt_task_explicit | TASK_TYPE_DETAILS_FORMAT(new_taskdata), 1,
          OMPT_LOAD_RETURN_ADDRESS(gtid));
    }

    new_taskdata->ompt_task_info.frame.enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
  }

#if OMPT_OPTIONAL
  /* OMPT grab all dependences if requested by the tool */
  if (ndeps + ndeps_noalias > 0 &&
      ompt_enabled.ompt_callback_dependences) {
    kmp_int32 i;

    new_taskdata->ompt_task_info.ndeps = ndeps + ndeps_noalias;
    new_taskdata->ompt_task_info.deps =
        (ompt_dependence_t *)KMP_OMPT_DEPS_ALLOC(
            thread, (ndeps + ndeps_noalias) * sizeof(ompt_dependence_t));

    KMP_ASSERT(new_taskdata->ompt_task_info.deps != NULL);

    for (i = 0; i < ndeps; i++) {
      new_taskdata->ompt_task_info.deps[i].variable.ptr =
          (void *)dep_list[i].base_addr;
      if (dep_list[i].flags.in && dep_list[i].flags.out)
        new_taskdata->ompt_task_info.deps[i].dependence_type =
            ompt_dependence_type_inout;
      else if (dep_list[i].flags.out)
        new_taskdata->ompt_task_info.deps[i].dependence_type =
            ompt_dependence_type_out;
      else if (dep_list[i].flags.in)
        new_taskdata->ompt_task_info.deps[i].dependence_type =
            ompt_dependence_type_in;
    }
    for (i = 0; i < ndeps_noalias; i++) {
      new_taskdata->ompt_task_info.deps[ndeps + i].variable.ptr =
          (void *)noalias_dep_list[i].base_addr;
      if (noalias_dep_list[i].flags.in && noalias_dep_list[i].flags.out)
        new_taskdata->ompt_task_info.deps[ndeps + i].dependence_type =
            ompt_dependence_type_inout;
      else if (noalias_dep_list[i].flags.out)
        new_taskdata->ompt_task_info.deps[ndeps + i].dependence_type =
            ompt_dependence_type_out;
      else if (noalias_dep_list[i].flags.in)
        new_taskdata->ompt_task_info.deps[ndeps + i].dependence_type =
            ompt_dependence_type_in;
    }
    ompt_callbacks.ompt_callback(ompt_callback_dependences)(
        &(new_taskdata->ompt_task_info.task_data),
        new_taskdata->ompt_task_info.deps, new_taskdata->ompt_task_info.ndeps);
    /* We can now free the allocated memory for the dependencies */
    /* For OMPD we might want to delay the free until task_end */
    KMP_OMPT_DEPS_FREE(thread, new_taskdata->ompt_task_info.deps);
    new_taskdata->ompt_task_info.deps = NULL;
    new_taskdata->ompt_task_info.ndeps = 0;
  }
#endif /* OMPT_OPTIONAL */
#endif /* OMPT_SUPPORT */

  bool serial = current_task->td_flags.team_serial ||
                current_task->td_flags.tasking_ser ||
                current_task->td_flags.final;
#if OMP_45_ENABLED
  kmp_task_team_t *task_team = thread->th.th_task_team;
  serial = serial && !(task_team && task_team->tt.tt_found_proxy_tasks);
#endif

  if (!serial && (ndeps > 0 || ndeps_noalias > 0)) {
    /* if no dependencies have been tracked yet, create the dependence hash */
    if (current_task->td_dephash == NULL)
      current_task->td_dephash = __kmp_dephash_create(thread, current_task);

#if USE_FAST_MEMORY
    kmp_depnode_t *node =
        (kmp_depnode_t *)__kmp_fast_allocate(thread, sizeof(kmp_depnode_t));
#else
    kmp_depnode_t *node =
        (kmp_depnode_t *)__kmp_thread_malloc(thread, sizeof(kmp_depnode_t));
#endif

    __kmp_init_node(node);
    new_taskdata->td_depnode = node;

    if (__kmp_check_deps(gtid, node, new_task, current_task->td_dephash,
                         NO_DEP_BARRIER, ndeps, dep_list, ndeps_noalias,
                         noalias_dep_list)) {
      KA_TRACE(10, ("__kmpc_omp_task_with_deps(exit): T#%d task had blocking "
                    "dependencies: "
                    "loc=%p task=%p, return: TASK_CURRENT_NOT_QUEUED\n",
                    gtid, loc_ref, new_taskdata));
#if OMPT_SUPPORT
      if (ompt_enabled.enabled) {
        current_task->ompt_task_info.frame.enter_frame = ompt_data_none;
      }
#endif
      return TASK_CURRENT_NOT_QUEUED;
    }
  } else {
    KA_TRACE(10, ("__kmpc_omp_task_with_deps(exit): T#%d ignored dependencies "
                  "for task (serialized)"
                  "loc=%p task=%p\n",
                  gtid, loc_ref, new_taskdata));
  }

  KA_TRACE(10, ("__kmpc_omp_task_with_deps(exit): T#%d task had no blocking "
                "dependencies : "
                "loc=%p task=%p, transferring to __kmp_omp_task\n",
                gtid, loc_ref, new_taskdata));

  kmp_int32 ret = __kmp_omp_task(gtid, new_task, true);
#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    current_task->ompt_task_info.frame.enter_frame = ompt_data_none;
  }
#endif
  return ret;
}

/*!
@ingroup TASKING
@param loc_ref location of the original task directive
@param gtid Global Thread ID of encountering thread
@param ndeps Number of depend items with possible aliasing
@param dep_list List of depend items with possible aliasing
@param ndeps_noalias Number of depend items with no aliasing
@param noalias_dep_list List of depend items with no aliasing

Blocks the current task until all specifies dependencies have been fulfilled.
*/
void __kmpc_omp_wait_deps(ident_t *loc_ref, kmp_int32 gtid, kmp_int32 ndeps,
                          kmp_depend_info_t *dep_list, kmp_int32 ndeps_noalias,
                          kmp_depend_info_t *noalias_dep_list) {
  KA_TRACE(10, ("__kmpc_omp_wait_deps(enter): T#%d loc=%p\n", gtid, loc_ref));

  if (ndeps == 0 && ndeps_noalias == 0) {
    KA_TRACE(10, ("__kmpc_omp_wait_deps(exit): T#%d has no dependencies to "
                  "wait upon : loc=%p\n",
                  gtid, loc_ref));
    return;
  }

  kmp_info_t *thread = __kmp_threads[gtid];
  kmp_taskdata_t *current_task = thread->th.th_current_task;

  // We can return immediately as:
  // - dependences are not computed in serial teams (except with proxy tasks)
  // - if the dephash is not yet created it means we have nothing to wait for
  bool ignore = current_task->td_flags.team_serial ||
                current_task->td_flags.tasking_ser ||
                current_task->td_flags.final;
#if OMP_45_ENABLED
  ignore = ignore && thread->th.th_task_team != NULL &&
           thread->th.th_task_team->tt.tt_found_proxy_tasks == FALSE;
#endif
  ignore = ignore || current_task->td_dephash == NULL;

  if (ignore) {
    KA_TRACE(10, ("__kmpc_omp_wait_deps(exit): T#%d has no blocking "
                  "dependencies : loc=%p\n",
                  gtid, loc_ref));
    return;
  }

  kmp_depnode_t node = {0};
  __kmp_init_node(&node);

  if (!__kmp_check_deps(gtid, &node, NULL, current_task->td_dephash,
                        DEP_BARRIER, ndeps, dep_list, ndeps_noalias,
                        noalias_dep_list)) {
    KA_TRACE(10, ("__kmpc_omp_wait_deps(exit): T#%d has no blocking "
                  "dependencies : loc=%p\n",
                  gtid, loc_ref));
    return;
  }

  int thread_finished = FALSE;
  kmp_flag_32 flag((std::atomic<kmp_uint32> *)&node.dn.npredecessors, 0U);
  while (node.dn.npredecessors > 0) {
    flag.execute_tasks(thread, gtid, FALSE,
                       &thread_finished USE_ITT_BUILD_ARG(NULL),
                       __kmp_task_stealing_constraint);
  }

  KA_TRACE(10, ("__kmpc_omp_wait_deps(exit): T#%d finished waiting : loc=%p\n",
                gtid, loc_ref));
}

#endif /* OMP_40_ENABLED */
