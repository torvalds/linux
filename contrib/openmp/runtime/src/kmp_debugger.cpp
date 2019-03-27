#include "kmp_config.h"

#if USE_DEBUGGER
/*
 * kmp_debugger.cpp -- debugger support.
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
#include "kmp_lock.h"
#include "kmp_omp.h"
#include "kmp_str.h"

// NOTE: All variable names are known to the debugger, do not change!

#ifdef __cplusplus
extern "C" {
extern kmp_omp_struct_info_t __kmp_omp_debug_struct_info;
} // extern "C"
#endif // __cplusplus

int __kmp_debugging = FALSE; // Boolean whether currently debugging OpenMP RTL.

#define offset_and_size_of(structure, field)                                   \
  { offsetof(structure, field), sizeof(((structure *)NULL)->field) }

#define offset_and_size_not_available                                          \
  { -1, -1 }

#define addr_and_size_of(var)                                                  \
  { (kmp_uint64)(&var), sizeof(var) }

#define nthr_buffer_size 1024
static kmp_int32 kmp_omp_nthr_info_buffer[nthr_buffer_size] = {
    nthr_buffer_size * sizeof(kmp_int32)};

/* TODO: Check punctuation for various platforms here */
static char func_microtask[] = "__kmp_invoke_microtask";
static char func_fork[] = "__kmpc_fork_call";
static char func_fork_teams[] = "__kmpc_fork_teams";

// Various info about runtime structures: addresses, field offsets, sizes, etc.
kmp_omp_struct_info_t __kmp_omp_debug_struct_info = {

    /* Change this only if you make a fundamental data structure change here */
    KMP_OMP_VERSION,

    /* sanity check.  Only should be checked if versions are identical
     * This is also used for backward compatibility to get the runtime
     * structure size if it the runtime is older than the interface */
    sizeof(kmp_omp_struct_info_t),

    /* OpenMP RTL version info. */
    addr_and_size_of(__kmp_version_major),
    addr_and_size_of(__kmp_version_minor),
    addr_and_size_of(__kmp_version_build),
    addr_and_size_of(__kmp_openmp_version),
    {(kmp_uint64)(__kmp_copyright) + KMP_VERSION_MAGIC_LEN,
     0}, // Skip magic prefix.

    /* Various globals. */
    addr_and_size_of(__kmp_threads),
    addr_and_size_of(__kmp_root),
    addr_and_size_of(__kmp_threads_capacity),
#if KMP_USE_MONITOR
    addr_and_size_of(__kmp_monitor),
#endif
#if !KMP_USE_DYNAMIC_LOCK
    addr_and_size_of(__kmp_user_lock_table),
#endif
    addr_and_size_of(func_microtask),
    addr_and_size_of(func_fork),
    addr_and_size_of(func_fork_teams),
    addr_and_size_of(__kmp_team_counter),
    addr_and_size_of(__kmp_task_counter),
    addr_and_size_of(kmp_omp_nthr_info_buffer),
    sizeof(void *),
    OMP_LOCK_T_SIZE < sizeof(void *),
    bs_last_barrier,
    INITIAL_TASK_DEQUE_SIZE,

    // thread structure information
    sizeof(kmp_base_info_t),
    offset_and_size_of(kmp_base_info_t, th_info),
    offset_and_size_of(kmp_base_info_t, th_team),
    offset_and_size_of(kmp_base_info_t, th_root),
    offset_and_size_of(kmp_base_info_t, th_serial_team),
    offset_and_size_of(kmp_base_info_t, th_ident),
    offset_and_size_of(kmp_base_info_t, th_spin_here),
    offset_and_size_of(kmp_base_info_t, th_next_waiting),
    offset_and_size_of(kmp_base_info_t, th_task_team),
    offset_and_size_of(kmp_base_info_t, th_current_task),
    offset_and_size_of(kmp_base_info_t, th_task_state),
    offset_and_size_of(kmp_base_info_t, th_bar),
    offset_and_size_of(kmp_bstate_t, b_worker_arrived),

#if OMP_40_ENABLED
    // teams information
    offset_and_size_of(kmp_base_info_t, th_teams_microtask),
    offset_and_size_of(kmp_base_info_t, th_teams_level),
    offset_and_size_of(kmp_teams_size_t, nteams),
    offset_and_size_of(kmp_teams_size_t, nth),
#endif

    // kmp_desc structure (for info field above)
    sizeof(kmp_desc_base_t),
    offset_and_size_of(kmp_desc_base_t, ds_tid),
    offset_and_size_of(kmp_desc_base_t, ds_gtid),
// On Windows* OS, ds_thread contains a thread /handle/, which is not usable,
// while thread /id/ is in ds_thread_id.
#if KMP_OS_WINDOWS
    offset_and_size_of(kmp_desc_base_t, ds_thread_id),
#else
    offset_and_size_of(kmp_desc_base_t, ds_thread),
#endif

    // team structure information
    sizeof(kmp_base_team_t),
    offset_and_size_of(kmp_base_team_t, t_master_tid),
    offset_and_size_of(kmp_base_team_t, t_ident),
    offset_and_size_of(kmp_base_team_t, t_parent),
    offset_and_size_of(kmp_base_team_t, t_nproc),
    offset_and_size_of(kmp_base_team_t, t_threads),
    offset_and_size_of(kmp_base_team_t, t_serialized),
    offset_and_size_of(kmp_base_team_t, t_id),
    offset_and_size_of(kmp_base_team_t, t_pkfn),
    offset_and_size_of(kmp_base_team_t, t_task_team),
    offset_and_size_of(kmp_base_team_t, t_implicit_task_taskdata),
#if OMP_40_ENABLED
    offset_and_size_of(kmp_base_team_t, t_cancel_request),
#endif
    offset_and_size_of(kmp_base_team_t, t_bar),
    offset_and_size_of(kmp_balign_team_t, b_master_arrived),
    offset_and_size_of(kmp_balign_team_t, b_team_arrived),

    // root structure information
    sizeof(kmp_base_root_t),
    offset_and_size_of(kmp_base_root_t, r_root_team),
    offset_and_size_of(kmp_base_root_t, r_hot_team),
    offset_and_size_of(kmp_base_root_t, r_uber_thread),
    offset_and_size_not_available,

    // ident structure information
    sizeof(ident_t),
    offset_and_size_of(ident_t, psource),
    offset_and_size_of(ident_t, flags),

    // lock structure information
    sizeof(kmp_base_queuing_lock_t),
    offset_and_size_of(kmp_base_queuing_lock_t, initialized),
    offset_and_size_of(kmp_base_queuing_lock_t, location),
    offset_and_size_of(kmp_base_queuing_lock_t, tail_id),
    offset_and_size_of(kmp_base_queuing_lock_t, head_id),
    offset_and_size_of(kmp_base_queuing_lock_t, next_ticket),
    offset_and_size_of(kmp_base_queuing_lock_t, now_serving),
    offset_and_size_of(kmp_base_queuing_lock_t, owner_id),
    offset_and_size_of(kmp_base_queuing_lock_t, depth_locked),
    offset_and_size_of(kmp_base_queuing_lock_t, flags),

#if !KMP_USE_DYNAMIC_LOCK
    /* Lock table. */
    sizeof(kmp_lock_table_t),
    offset_and_size_of(kmp_lock_table_t, used),
    offset_and_size_of(kmp_lock_table_t, allocated),
    offset_and_size_of(kmp_lock_table_t, table),
#endif

    // Task team structure information.
    sizeof(kmp_base_task_team_t),
    offset_and_size_of(kmp_base_task_team_t, tt_threads_data),
    offset_and_size_of(kmp_base_task_team_t, tt_found_tasks),
    offset_and_size_of(kmp_base_task_team_t, tt_nproc),
    offset_and_size_of(kmp_base_task_team_t, tt_unfinished_threads),
    offset_and_size_of(kmp_base_task_team_t, tt_active),

    // task_data_t.
    sizeof(kmp_taskdata_t),
    offset_and_size_of(kmp_taskdata_t, td_task_id),
    offset_and_size_of(kmp_taskdata_t, td_flags),
    offset_and_size_of(kmp_taskdata_t, td_team),
    offset_and_size_of(kmp_taskdata_t, td_parent),
    offset_and_size_of(kmp_taskdata_t, td_level),
    offset_and_size_of(kmp_taskdata_t, td_ident),
    offset_and_size_of(kmp_taskdata_t, td_allocated_child_tasks),
    offset_and_size_of(kmp_taskdata_t, td_incomplete_child_tasks),

    offset_and_size_of(kmp_taskdata_t, td_taskwait_ident),
    offset_and_size_of(kmp_taskdata_t, td_taskwait_counter),
    offset_and_size_of(kmp_taskdata_t, td_taskwait_thread),

#if OMP_40_ENABLED
    offset_and_size_of(kmp_taskdata_t, td_taskgroup),
    offset_and_size_of(kmp_taskgroup_t, count),
    offset_and_size_of(kmp_taskgroup_t, cancel_request),

    offset_and_size_of(kmp_taskdata_t, td_depnode),
    offset_and_size_of(kmp_depnode_list_t, node),
    offset_and_size_of(kmp_depnode_list_t, next),
    offset_and_size_of(kmp_base_depnode_t, successors),
    offset_and_size_of(kmp_base_depnode_t, task),
    offset_and_size_of(kmp_base_depnode_t, npredecessors),
    offset_and_size_of(kmp_base_depnode_t, nrefs),
#endif
    offset_and_size_of(kmp_task_t, routine),

    // thread_data_t.
    sizeof(kmp_thread_data_t),
    offset_and_size_of(kmp_base_thread_data_t, td_deque),
    offset_and_size_of(kmp_base_thread_data_t, td_deque_size),
    offset_and_size_of(kmp_base_thread_data_t, td_deque_head),
    offset_and_size_of(kmp_base_thread_data_t, td_deque_tail),
    offset_and_size_of(kmp_base_thread_data_t, td_deque_ntasks),
    offset_and_size_of(kmp_base_thread_data_t, td_deque_last_stolen),

    // The last field.
    KMP_OMP_VERSION,

}; // __kmp_omp_debug_struct_info

#undef offset_and_size_of
#undef addr_and_size_of

/* Intel compiler on IA-32 architecture issues a warning "conversion
  from "unsigned long long" to "char *" may lose significant bits"
  when 64-bit value is assigned to 32-bit pointer. Use this function
  to suppress the warning. */
static inline void *__kmp_convert_to_ptr(kmp_uint64 addr) {
#if KMP_COMPILER_ICC
#pragma warning(push)
#pragma warning(disable : 810) // conversion from "unsigned long long" to "char
// *" may lose significant bits
#pragma warning(disable : 1195) // conversion from integer to smaller pointer
#endif // KMP_COMPILER_ICC
  return (void *)addr;
#if KMP_COMPILER_ICC
#pragma warning(pop)
#endif // KMP_COMPILER_ICC
} // __kmp_convert_to_ptr

static int kmp_location_match(kmp_str_loc_t *loc, kmp_omp_nthr_item_t *item) {

  int file_match = 0;
  int func_match = 0;
  int line_match = 0;

  char *file = (char *)__kmp_convert_to_ptr(item->file);
  char *func = (char *)__kmp_convert_to_ptr(item->func);
  file_match = __kmp_str_fname_match(&loc->fname, file);
  func_match =
      item->func == 0 // If item->func is NULL, it allows any func name.
      || strcmp(func, "*") == 0 ||
      (loc->func != NULL && strcmp(loc->func, func) == 0);
  line_match =
      item->begin <= loc->line &&
      (item->end <= 0 ||
       loc->line <= item->end); // if item->end <= 0, it means "end of file".

  return (file_match && func_match && line_match);

} // kmp_location_match

int __kmp_omp_num_threads(ident_t const *ident) {

  int num_threads = 0;

  kmp_omp_nthr_info_t *info = (kmp_omp_nthr_info_t *)__kmp_convert_to_ptr(
      __kmp_omp_debug_struct_info.nthr_info.addr);
  if (info->num > 0 && info->array != 0) {
    kmp_omp_nthr_item_t *items =
        (kmp_omp_nthr_item_t *)__kmp_convert_to_ptr(info->array);
    kmp_str_loc_t loc = __kmp_str_loc_init(ident->psource, 1);
    int i;
    for (i = 0; i < info->num; ++i) {
      if (kmp_location_match(&loc, &items[i])) {
        num_threads = items[i].num_threads;
      }
    }
    __kmp_str_loc_free(&loc);
  }

  return num_threads;
  ;

} // __kmp_omp_num_threads
#endif /* USE_DEBUGGER */
