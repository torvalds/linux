/*
 * kmp_global.cpp -- KPTS global variables for runtime support library
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
#include "kmp_affinity.h"
#if KMP_USE_HIER_SCHED
#include "kmp_dispatch_hier.h"
#endif

kmp_key_t __kmp_gtid_threadprivate_key;

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
kmp_cpuinfo_t __kmp_cpuinfo = {0}; // Not initialized
#endif

#if KMP_STATS_ENABLED
#include "kmp_stats.h"
// lock for modifying the global __kmp_stats_list
kmp_tas_lock_t __kmp_stats_lock;

// global list of per thread stats, the head is a sentinel node which
// accumulates all stats produced before __kmp_create_worker is called.
kmp_stats_list *__kmp_stats_list;

// thread local pointer to stats node within list
KMP_THREAD_LOCAL kmp_stats_list *__kmp_stats_thread_ptr = NULL;

// gives reference tick for all events (considered the 0 tick)
tsc_tick_count __kmp_stats_start_time;
#endif

/* ----------------------------------------------------- */
/* INITIALIZATION VARIABLES */
/* they are syncronized to write during init, but read anytime */
volatile int __kmp_init_serial = FALSE;
volatile int __kmp_init_gtid = FALSE;
volatile int __kmp_init_common = FALSE;
volatile int __kmp_init_middle = FALSE;
volatile int __kmp_init_parallel = FALSE;
#if KMP_USE_MONITOR
volatile int __kmp_init_monitor =
    0; /* 1 - launched, 2 - actually started (Windows* OS only) */
#endif
volatile int __kmp_init_user_locks = FALSE;

/* list of address of allocated caches for commons */
kmp_cached_addr_t *__kmp_threadpriv_cache_list = NULL;

int __kmp_init_counter = 0;
int __kmp_root_counter = 0;
int __kmp_version = 0;

std::atomic<kmp_int32> __kmp_team_counter = ATOMIC_VAR_INIT(0);
std::atomic<kmp_int32> __kmp_task_counter = ATOMIC_VAR_INIT(0);

unsigned int __kmp_init_wait =
    KMP_DEFAULT_INIT_WAIT; /* initial number of spin-tests   */
unsigned int __kmp_next_wait =
    KMP_DEFAULT_NEXT_WAIT; /* susequent number of spin-tests */

size_t __kmp_stksize = KMP_DEFAULT_STKSIZE;
#if KMP_USE_MONITOR
size_t __kmp_monitor_stksize = 0; // auto adjust
#endif
size_t __kmp_stkoffset = KMP_DEFAULT_STKOFFSET;
int __kmp_stkpadding = KMP_MIN_STKPADDING;

size_t __kmp_malloc_pool_incr = KMP_DEFAULT_MALLOC_POOL_INCR;

// Barrier method defaults, settings, and strings.
// branch factor = 2^branch_bits (only relevant for tree & hyper barrier types)
kmp_uint32 __kmp_barrier_gather_bb_dflt = 2;
/* branch_factor = 4 */ /* hyper2: C78980 */
kmp_uint32 __kmp_barrier_release_bb_dflt = 2;
/* branch_factor = 4 */ /* hyper2: C78980 */

kmp_bar_pat_e __kmp_barrier_gather_pat_dflt = bp_hyper_bar;
/* hyper2: C78980 */
kmp_bar_pat_e __kmp_barrier_release_pat_dflt = bp_hyper_bar;
/* hyper2: C78980 */

kmp_uint32 __kmp_barrier_gather_branch_bits[bs_last_barrier] = {0};
kmp_uint32 __kmp_barrier_release_branch_bits[bs_last_barrier] = {0};
kmp_bar_pat_e __kmp_barrier_gather_pattern[bs_last_barrier] = {bp_linear_bar};
kmp_bar_pat_e __kmp_barrier_release_pattern[bs_last_barrier] = {bp_linear_bar};
char const *__kmp_barrier_branch_bit_env_name[bs_last_barrier] = {
    "KMP_PLAIN_BARRIER", "KMP_FORKJOIN_BARRIER"
#if KMP_FAST_REDUCTION_BARRIER
    ,
    "KMP_REDUCTION_BARRIER"
#endif // KMP_FAST_REDUCTION_BARRIER
};
char const *__kmp_barrier_pattern_env_name[bs_last_barrier] = {
    "KMP_PLAIN_BARRIER_PATTERN", "KMP_FORKJOIN_BARRIER_PATTERN"
#if KMP_FAST_REDUCTION_BARRIER
    ,
    "KMP_REDUCTION_BARRIER_PATTERN"
#endif // KMP_FAST_REDUCTION_BARRIER
};
char const *__kmp_barrier_type_name[bs_last_barrier] = {"plain", "forkjoin"
#if KMP_FAST_REDUCTION_BARRIER
                                                        ,
                                                        "reduction"
#endif // KMP_FAST_REDUCTION_BARRIER
};
char const *__kmp_barrier_pattern_name[bp_last_bar] = {"linear", "tree",
                                                       "hyper", "hierarchical"};

int __kmp_allThreadsSpecified = 0;
size_t __kmp_align_alloc = CACHE_LINE;

int __kmp_generate_warnings = kmp_warnings_low;
int __kmp_reserve_warn = 0;
int __kmp_xproc = 0;
int __kmp_avail_proc = 0;
size_t __kmp_sys_min_stksize = KMP_MIN_STKSIZE;
int __kmp_sys_max_nth = KMP_MAX_NTH;
int __kmp_max_nth = 0;
int __kmp_cg_max_nth = 0;
int __kmp_teams_max_nth = 0;
int __kmp_threads_capacity = 0;
int __kmp_dflt_team_nth = 0;
int __kmp_dflt_team_nth_ub = 0;
int __kmp_tp_capacity = 0;
int __kmp_tp_cached = 0;
int __kmp_dflt_nested = FALSE;
int __kmp_dispatch_num_buffers = KMP_DFLT_DISP_NUM_BUFF;
int __kmp_dflt_max_active_levels =
    KMP_MAX_ACTIVE_LEVELS_LIMIT; /* max_active_levels limit */
#if KMP_NESTED_HOT_TEAMS
int __kmp_hot_teams_mode = 0; /* 0 - free extra threads when reduced */
/* 1 - keep extra threads when reduced */
int __kmp_hot_teams_max_level = 1; /* nesting level of hot teams */
#endif
enum library_type __kmp_library = library_none;
enum sched_type __kmp_sched =
    kmp_sch_default; /* scheduling method for runtime scheduling */
enum sched_type __kmp_static =
    kmp_sch_static_greedy; /* default static scheduling method */
enum sched_type __kmp_guided =
    kmp_sch_guided_iterative_chunked; /* default guided scheduling method */
enum sched_type __kmp_auto =
    kmp_sch_guided_analytical_chunked; /* default auto scheduling method */
#if KMP_USE_HIER_SCHED
int __kmp_dispatch_hand_threading = 0;
int __kmp_hier_max_units[kmp_hier_layer_e::LAYER_LAST + 1];
int __kmp_hier_threads_per[kmp_hier_layer_e::LAYER_LAST + 1];
kmp_hier_sched_env_t __kmp_hier_scheds = {0, 0, NULL, NULL, NULL};
#endif
int __kmp_dflt_blocktime = KMP_DEFAULT_BLOCKTIME;
#if KMP_USE_MONITOR
int __kmp_monitor_wakeups = KMP_MIN_MONITOR_WAKEUPS;
int __kmp_bt_intervals = KMP_INTERVALS_FROM_BLOCKTIME(KMP_DEFAULT_BLOCKTIME,
                                                      KMP_MIN_MONITOR_WAKEUPS);
#endif
#ifdef KMP_ADJUST_BLOCKTIME
int __kmp_zero_bt = FALSE;
#endif /* KMP_ADJUST_BLOCKTIME */
#ifdef KMP_DFLT_NTH_CORES
int __kmp_ncores = 0;
#endif
int __kmp_chunk = 0;
int __kmp_abort_delay = 0;
#if KMP_OS_LINUX && defined(KMP_TDATA_GTID)
int __kmp_gtid_mode = 3; /* use __declspec(thread) TLS to store gtid */
int __kmp_adjust_gtid_mode = FALSE;
#elif KMP_OS_WINDOWS
int __kmp_gtid_mode = 2; /* use TLS functions to store gtid */
int __kmp_adjust_gtid_mode = FALSE;
#else
int __kmp_gtid_mode = 0; /* select method to get gtid based on #threads */
int __kmp_adjust_gtid_mode = TRUE;
#endif /* KMP_OS_LINUX && defined(KMP_TDATA_GTID) */
#ifdef KMP_TDATA_GTID
KMP_THREAD_LOCAL int __kmp_gtid = KMP_GTID_DNE;
#endif /* KMP_TDATA_GTID */
int __kmp_tls_gtid_min = INT_MAX;
int __kmp_foreign_tp = TRUE;
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
int __kmp_inherit_fp_control = TRUE;
kmp_int16 __kmp_init_x87_fpu_control_word = 0;
kmp_uint32 __kmp_init_mxcsr = 0;
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#ifdef USE_LOAD_BALANCE
double __kmp_load_balance_interval = 1.0;
#endif /* USE_LOAD_BALANCE */

kmp_nested_nthreads_t __kmp_nested_nth = {NULL, 0, 0};

#if KMP_USE_ADAPTIVE_LOCKS

kmp_adaptive_backoff_params_t __kmp_adaptive_backoff_params = {
    1, 1024}; // TODO: tune it!

#if KMP_DEBUG_ADAPTIVE_LOCKS
const char *__kmp_speculative_statsfile = "-";
#endif

#endif // KMP_USE_ADAPTIVE_LOCKS

#if OMP_40_ENABLED
int __kmp_display_env = FALSE;
int __kmp_display_env_verbose = FALSE;
int __kmp_omp_cancellation = FALSE;
#endif

/* map OMP 3.0 schedule types with our internal schedule types */
enum sched_type __kmp_sch_map[kmp_sched_upper - kmp_sched_lower_ext +
                              kmp_sched_upper_std - kmp_sched_lower - 2] = {
    kmp_sch_static_chunked, // ==> kmp_sched_static            = 1
    kmp_sch_dynamic_chunked, // ==> kmp_sched_dynamic           = 2
    kmp_sch_guided_chunked, // ==> kmp_sched_guided            = 3
    kmp_sch_auto, // ==> kmp_sched_auto              = 4
    kmp_sch_trapezoidal // ==> kmp_sched_trapezoidal       = 101
    // will likely not be used, introduced here just to debug the code
    // of public intel extension schedules
};

#if KMP_OS_LINUX
enum clock_function_type __kmp_clock_function;
int __kmp_clock_function_param;
#endif /* KMP_OS_LINUX */

#if KMP_MIC_SUPPORTED
enum mic_type __kmp_mic_type = non_mic;
#endif

#if KMP_AFFINITY_SUPPORTED

KMPAffinity *__kmp_affinity_dispatch = NULL;

#if KMP_USE_HWLOC
int __kmp_hwloc_error = FALSE;
hwloc_topology_t __kmp_hwloc_topology = NULL;
int __kmp_numa_detected = FALSE;
int __kmp_tile_depth = 0;
#endif

#if KMP_OS_WINDOWS
#if KMP_GROUP_AFFINITY
int __kmp_num_proc_groups = 1;
#endif /* KMP_GROUP_AFFINITY */
kmp_GetActiveProcessorCount_t __kmp_GetActiveProcessorCount = NULL;
kmp_GetActiveProcessorGroupCount_t __kmp_GetActiveProcessorGroupCount = NULL;
kmp_GetThreadGroupAffinity_t __kmp_GetThreadGroupAffinity = NULL;
kmp_SetThreadGroupAffinity_t __kmp_SetThreadGroupAffinity = NULL;
#endif /* KMP_OS_WINDOWS */

size_t __kmp_affin_mask_size = 0;
enum affinity_type __kmp_affinity_type = affinity_default;
enum affinity_gran __kmp_affinity_gran = affinity_gran_default;
int __kmp_affinity_gran_levels = -1;
int __kmp_affinity_dups = TRUE;
enum affinity_top_method __kmp_affinity_top_method =
    affinity_top_method_default;
int __kmp_affinity_compact = 0;
int __kmp_affinity_offset = 0;
int __kmp_affinity_verbose = FALSE;
int __kmp_affinity_warnings = TRUE;
int __kmp_affinity_respect_mask = affinity_respect_mask_default;
char *__kmp_affinity_proclist = NULL;
kmp_affin_mask_t *__kmp_affinity_masks = NULL;
unsigned __kmp_affinity_num_masks = 0;

char *__kmp_cpuinfo_file = NULL;

#endif /* KMP_AFFINITY_SUPPORTED */

#if OMP_40_ENABLED
kmp_nested_proc_bind_t __kmp_nested_proc_bind = {NULL, 0, 0};
int __kmp_affinity_num_places = 0;
#endif

#if OMP_50_ENABLED
int __kmp_display_affinity = FALSE;
char *__kmp_affinity_format = NULL;
#endif // OMP_50_ENABLED

kmp_hws_item_t __kmp_hws_socket = {0, 0};
kmp_hws_item_t __kmp_hws_node = {0, 0};
kmp_hws_item_t __kmp_hws_tile = {0, 0};
kmp_hws_item_t __kmp_hws_core = {0, 0};
kmp_hws_item_t __kmp_hws_proc = {0, 0};
int __kmp_hws_requested = 0;
int __kmp_hws_abs_flag = 0; // absolute or per-item number requested

#if OMP_40_ENABLED
kmp_int32 __kmp_default_device = 0;
#endif

kmp_tasking_mode_t __kmp_tasking_mode = tskm_task_teams;
#if OMP_45_ENABLED
kmp_int32 __kmp_max_task_priority = 0;
kmp_uint64 __kmp_taskloop_min_tasks = 0;
#endif

#if OMP_50_ENABLED
int __kmp_memkind_available = 0;
int __kmp_hbw_mem_available = 0;
const omp_allocator_t *OMP_NULL_ALLOCATOR = NULL;
const omp_allocator_t *omp_default_mem_alloc = (const omp_allocator_t *)1;
const omp_allocator_t *omp_large_cap_mem_alloc = (const omp_allocator_t *)2;
const omp_allocator_t *omp_const_mem_alloc = (const omp_allocator_t *)3;
const omp_allocator_t *omp_high_bw_mem_alloc = (const omp_allocator_t *)4;
const omp_allocator_t *omp_low_lat_mem_alloc = (const omp_allocator_t *)5;
const omp_allocator_t *omp_cgroup_mem_alloc = (const omp_allocator_t *)6;
const omp_allocator_t *omp_pteam_mem_alloc = (const omp_allocator_t *)7;
const omp_allocator_t *omp_thread_mem_alloc = (const omp_allocator_t *)8;
void *const *__kmp_def_allocator = omp_default_mem_alloc;
#endif

/* This check ensures that the compiler is passing the correct data type for the
   flags formal parameter of the function kmpc_omp_task_alloc(). If the type is
   not a 4-byte type, then give an error message about a non-positive length
   array pointing here.  If that happens, the kmp_tasking_flags_t structure must
   be redefined to have exactly 32 bits. */
KMP_BUILD_ASSERT(sizeof(kmp_tasking_flags_t) == 4);

int __kmp_task_stealing_constraint = 1; /* Constrain task stealing by default */

#ifdef DEBUG_SUSPEND
int __kmp_suspend_count = 0;
#endif

int __kmp_settings = FALSE;
int __kmp_duplicate_library_ok = 0;
#if USE_ITT_BUILD
int __kmp_forkjoin_frames = 1;
int __kmp_forkjoin_frames_mode = 3;
#endif
PACKED_REDUCTION_METHOD_T __kmp_force_reduction_method =
    reduction_method_not_defined;
int __kmp_determ_red = FALSE;

#ifdef KMP_DEBUG
int kmp_a_debug = 0;
int kmp_b_debug = 0;
int kmp_c_debug = 0;
int kmp_d_debug = 0;
int kmp_e_debug = 0;
int kmp_f_debug = 0;
int kmp_diag = 0;
#endif

/* For debug information logging using rotating buffer */
int __kmp_debug_buf =
    FALSE; /* TRUE means use buffer, FALSE means print to stderr */
int __kmp_debug_buf_lines =
    KMP_DEBUG_BUF_LINES_INIT; /* Lines of debug stored in buffer */
int __kmp_debug_buf_chars =
    KMP_DEBUG_BUF_CHARS_INIT; /* Characters allowed per line in buffer */
int __kmp_debug_buf_atomic =
    FALSE; /* TRUE means use atomic update of buffer entry pointer */

char *__kmp_debug_buffer = NULL; /* Debug buffer itself */
std::atomic<int> __kmp_debug_count =
    ATOMIC_VAR_INIT(0); /* number of lines printed in buffer so far */
int __kmp_debug_buf_warn_chars =
    0; /* Keep track of char increase recommended in warnings */
/* end rotating debug buffer */

#ifdef KMP_DEBUG
int __kmp_par_range; /* +1 => only go par for constructs in range */
/* -1 => only go par for constructs outside range */
char __kmp_par_range_routine[KMP_PAR_RANGE_ROUTINE_LEN] = {'\0'};
char __kmp_par_range_filename[KMP_PAR_RANGE_FILENAME_LEN] = {'\0'};
int __kmp_par_range_lb = 0;
int __kmp_par_range_ub = INT_MAX;
#endif /* KMP_DEBUG */

/* For printing out dynamic storage map for threads and teams */
int __kmp_storage_map =
    FALSE; /* True means print storage map for threads and teams */
int __kmp_storage_map_verbose =
    FALSE; /* True means storage map includes placement info */
int __kmp_storage_map_verbose_specified = FALSE;
/* Initialize the library data structures when we fork a child process, defaults
 * to TRUE */
int __kmp_need_register_atfork =
    TRUE; /* At initialization, call pthread_atfork to install fork handler */
int __kmp_need_register_atfork_specified = TRUE;

int __kmp_env_stksize = FALSE; /* KMP_STACKSIZE specified? */
int __kmp_env_blocktime = FALSE; /* KMP_BLOCKTIME specified? */
int __kmp_env_checks = FALSE; /* KMP_CHECKS specified?    */
int __kmp_env_consistency_check = FALSE; /* KMP_CONSISTENCY_CHECK specified? */

kmp_uint32 __kmp_yield_init = KMP_INIT_WAIT;
kmp_uint32 __kmp_yield_next = KMP_NEXT_WAIT;

#if KMP_USE_MONITOR
kmp_uint32 __kmp_yielding_on = 1;
#endif
#if KMP_OS_CNK
kmp_uint32 __kmp_yield_cycle = 0;
#else
kmp_uint32 __kmp_yield_cycle = 1; /* Yield-cycle is on by default */
#endif
kmp_int32 __kmp_yield_on_count =
    10; /* By default, yielding is on for 10 monitor periods. */
kmp_int32 __kmp_yield_off_count =
    1; /* By default, yielding is off for 1 monitor periods. */

/* ------------------------------------------------------ */
/* STATE mostly syncronized with global lock */
/* data written to rarely by masters, read often by workers */
/* TODO: None of this global padding stuff works consistently because the order
   of declaration is not necessarily correlated to storage order. To fix this,
   all the important globals must be put in a big structure instead. */
KMP_ALIGN_CACHE
kmp_info_t **__kmp_threads = NULL;
kmp_root_t **__kmp_root = NULL;

/* data read/written to often by masters */
KMP_ALIGN_CACHE
volatile int __kmp_nth = 0;
volatile int __kmp_all_nth = 0;
int __kmp_thread_pool_nth = 0;
volatile kmp_info_t *__kmp_thread_pool = NULL;
volatile kmp_team_t *__kmp_team_pool = NULL;

KMP_ALIGN_CACHE
std::atomic<int> __kmp_thread_pool_active_nth = ATOMIC_VAR_INIT(0);

/* -------------------------------------------------
 * GLOBAL/ROOT STATE */
KMP_ALIGN_CACHE
kmp_global_t __kmp_global = {{0}};

/* ----------------------------------------------- */
/* GLOBAL SYNCHRONIZATION LOCKS */
/* TODO verify the need for these locks and if they need to be global */

#if KMP_USE_INTERNODE_ALIGNMENT
/* Multinode systems have larger cache line granularity which can cause
 * false sharing if the alignment is not large enough for these locks */
KMP_ALIGN_CACHE_INTERNODE

KMP_BOOTSTRAP_LOCK_INIT(__kmp_initz_lock); /* Control initializations */
KMP_ALIGN_CACHE_INTERNODE
KMP_BOOTSTRAP_LOCK_INIT(__kmp_forkjoin_lock); /* control fork/join access */
KMP_ALIGN_CACHE_INTERNODE
KMP_BOOTSTRAP_LOCK_INIT(__kmp_exit_lock); /* exit() is not always thread-safe */
#if KMP_USE_MONITOR
/* control monitor thread creation */
KMP_ALIGN_CACHE_INTERNODE
KMP_BOOTSTRAP_LOCK_INIT(__kmp_monitor_lock);
#endif
/* used for the hack to allow threadprivate cache and __kmp_threads expansion
   to co-exist */
KMP_ALIGN_CACHE_INTERNODE
KMP_BOOTSTRAP_LOCK_INIT(__kmp_tp_cached_lock);

KMP_ALIGN_CACHE_INTERNODE
KMP_LOCK_INIT(__kmp_global_lock); /* Control OS/global access */
KMP_ALIGN_CACHE_INTERNODE
kmp_queuing_lock_t __kmp_dispatch_lock; /* Control dispatch access  */
KMP_ALIGN_CACHE_INTERNODE
KMP_LOCK_INIT(__kmp_debug_lock); /* Control I/O access for KMP_DEBUG */
#else
KMP_ALIGN_CACHE

KMP_BOOTSTRAP_LOCK_INIT(__kmp_initz_lock); /* Control initializations */
KMP_BOOTSTRAP_LOCK_INIT(__kmp_forkjoin_lock); /* control fork/join access */
KMP_BOOTSTRAP_LOCK_INIT(__kmp_exit_lock); /* exit() is not always thread-safe */
#if KMP_USE_MONITOR
/* control monitor thread creation */
KMP_BOOTSTRAP_LOCK_INIT(__kmp_monitor_lock);
#endif
/* used for the hack to allow threadprivate cache and __kmp_threads expansion
   to co-exist */
KMP_BOOTSTRAP_LOCK_INIT(__kmp_tp_cached_lock);

KMP_ALIGN(128)
KMP_LOCK_INIT(__kmp_global_lock); /* Control OS/global access */
KMP_ALIGN(128)
kmp_queuing_lock_t __kmp_dispatch_lock; /* Control dispatch access  */
KMP_ALIGN(128)
KMP_LOCK_INIT(__kmp_debug_lock); /* Control I/O access for KMP_DEBUG */
#endif

/* ----------------------------------------------- */

#if KMP_HANDLE_SIGNALS
/* Signal handling is disabled by default, because it confuses users: In case of
   sigsegv (or other trouble) in user code signal handler catches the signal,
   which then "appears" in the monitor thread (when the monitor executes raise()
   function). Users see signal in the monitor thread and blame OpenMP RTL.

   Grant said signal handling required on some older OSes (Irix?) supported by
   KAI, because bad applications hung but not aborted. Currently it is not a
   problem for Linux* OS, OS X* and Windows* OS.

   Grant: Found new hangs for EL4, EL5, and a Fedora Core machine.  So I'm
   putting the default back for now to see if that fixes hangs on those
   machines.

   2010-04013 Lev: It was a bug in Fortran RTL. Fortran RTL prints a kind of
   stack backtrace when program is aborting, but the code is not signal-safe.
   When multiple signals raised at the same time (which occurs in dynamic
   negative tests because all the worker threads detects the same error),
   Fortran RTL may hang. The bug finally fixed in Fortran RTL library provided
   by Steve R., and will be available soon. */
int __kmp_handle_signals = FALSE;
#endif

#ifdef DEBUG_SUSPEND
int get_suspend_count_(void) {
  int count = __kmp_suspend_count;
  __kmp_suspend_count = 0;
  return count;
}
void set_suspend_count_(int *value) { __kmp_suspend_count = *value; }
#endif

// Symbols for MS mutual detection.
int _You_must_link_with_exactly_one_OpenMP_library = 1;
int _You_must_link_with_Intel_OpenMP_library = 1;
#if KMP_OS_WINDOWS && (KMP_VERSION_MAJOR > 4)
int _You_must_link_with_Microsoft_OpenMP_library = 1;
#endif

#if OMP_50_ENABLED
kmp_target_offload_kind_t __kmp_target_offload = tgt_default;
#endif
// end of file //
