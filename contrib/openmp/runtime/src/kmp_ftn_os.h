/*
 * kmp_ftn_os.h -- KPTS Fortran defines header file.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_FTN_OS_H
#define KMP_FTN_OS_H

// KMP_FNT_ENTRIES may be one of: KMP_FTN_PLAIN, KMP_FTN_UPPER, KMP_FTN_APPEND,
// KMP_FTN_UAPPEND.

/* -------------------------- External definitions ------------------------ */

#if KMP_FTN_ENTRIES == KMP_FTN_PLAIN

#define FTN_SET_STACKSIZE kmp_set_stacksize
#define FTN_SET_STACKSIZE_S kmp_set_stacksize_s
#define FTN_GET_STACKSIZE kmp_get_stacksize
#define FTN_GET_STACKSIZE_S kmp_get_stacksize_s
#define FTN_SET_BLOCKTIME kmp_set_blocktime
#define FTN_GET_BLOCKTIME kmp_get_blocktime
#define FTN_SET_LIBRARY_SERIAL kmp_set_library_serial
#define FTN_SET_LIBRARY_TURNAROUND kmp_set_library_turnaround
#define FTN_SET_LIBRARY_THROUGHPUT kmp_set_library_throughput
#define FTN_SET_LIBRARY kmp_set_library
#define FTN_GET_LIBRARY kmp_get_library
#define FTN_SET_DEFAULTS kmp_set_defaults
#define FTN_SET_DISP_NUM_BUFFERS kmp_set_disp_num_buffers
#define FTN_SET_AFFINITY kmp_set_affinity
#define FTN_GET_AFFINITY kmp_get_affinity
#define FTN_GET_AFFINITY_MAX_PROC kmp_get_affinity_max_proc
#define FTN_CREATE_AFFINITY_MASK kmp_create_affinity_mask
#define FTN_DESTROY_AFFINITY_MASK kmp_destroy_affinity_mask
#define FTN_SET_AFFINITY_MASK_PROC kmp_set_affinity_mask_proc
#define FTN_UNSET_AFFINITY_MASK_PROC kmp_unset_affinity_mask_proc
#define FTN_GET_AFFINITY_MASK_PROC kmp_get_affinity_mask_proc

#define FTN_MALLOC kmp_malloc
#define FTN_ALIGNED_MALLOC kmp_aligned_malloc
#define FTN_CALLOC kmp_calloc
#define FTN_REALLOC kmp_realloc
#define FTN_KFREE kmp_free

#define FTN_GET_NUM_KNOWN_THREADS kmp_get_num_known_threads

#define FTN_SET_NUM_THREADS omp_set_num_threads
#define FTN_GET_NUM_THREADS omp_get_num_threads
#define FTN_GET_MAX_THREADS omp_get_max_threads
#define FTN_GET_THREAD_NUM omp_get_thread_num
#define FTN_GET_NUM_PROCS omp_get_num_procs
#define FTN_SET_DYNAMIC omp_set_dynamic
#define FTN_GET_DYNAMIC omp_get_dynamic
#define FTN_SET_NESTED omp_set_nested
#define FTN_GET_NESTED omp_get_nested
#define FTN_IN_PARALLEL omp_in_parallel
#define FTN_GET_THREAD_LIMIT omp_get_thread_limit
#define FTN_SET_SCHEDULE omp_set_schedule
#define FTN_GET_SCHEDULE omp_get_schedule
#define FTN_SET_MAX_ACTIVE_LEVELS omp_set_max_active_levels
#define FTN_GET_MAX_ACTIVE_LEVELS omp_get_max_active_levels
#define FTN_GET_ACTIVE_LEVEL omp_get_active_level
#define FTN_GET_LEVEL omp_get_level
#define FTN_GET_ANCESTOR_THREAD_NUM omp_get_ancestor_thread_num
#define FTN_GET_TEAM_SIZE omp_get_team_size
#define FTN_IN_FINAL omp_in_final
//  #define FTN_SET_PROC_BIND                    omp_set_proc_bind
#define FTN_GET_PROC_BIND omp_get_proc_bind
//  #define FTN_CURR_PROC_BIND                   omp_curr_proc_bind
#if OMP_40_ENABLED
#define FTN_GET_NUM_TEAMS omp_get_num_teams
#define FTN_GET_TEAM_NUM omp_get_team_num
#endif
#define FTN_INIT_LOCK omp_init_lock
#if KMP_USE_DYNAMIC_LOCK
#define FTN_INIT_LOCK_WITH_HINT omp_init_lock_with_hint
#define FTN_INIT_NEST_LOCK_WITH_HINT omp_init_nest_lock_with_hint
#endif
#define FTN_DESTROY_LOCK omp_destroy_lock
#define FTN_SET_LOCK omp_set_lock
#define FTN_UNSET_LOCK omp_unset_lock
#define FTN_TEST_LOCK omp_test_lock
#define FTN_INIT_NEST_LOCK omp_init_nest_lock
#define FTN_DESTROY_NEST_LOCK omp_destroy_nest_lock
#define FTN_SET_NEST_LOCK omp_set_nest_lock
#define FTN_UNSET_NEST_LOCK omp_unset_nest_lock
#define FTN_TEST_NEST_LOCK omp_test_nest_lock

#define FTN_SET_WARNINGS_ON kmp_set_warnings_on
#define FTN_SET_WARNINGS_OFF kmp_set_warnings_off

#define FTN_GET_WTIME omp_get_wtime
#define FTN_GET_WTICK omp_get_wtick

#if OMP_40_ENABLED
#define FTN_GET_NUM_DEVICES omp_get_num_devices
#define FTN_GET_DEFAULT_DEVICE omp_get_default_device
#define FTN_SET_DEFAULT_DEVICE omp_set_default_device
#define FTN_IS_INITIAL_DEVICE omp_is_initial_device
#endif

#if OMP_40_ENABLED
#define FTN_GET_CANCELLATION omp_get_cancellation
#define FTN_GET_CANCELLATION_STATUS kmp_get_cancellation_status
#endif

#if OMP_45_ENABLED
#define FTN_GET_MAX_TASK_PRIORITY omp_get_max_task_priority
#define FTN_GET_NUM_PLACES omp_get_num_places
#define FTN_GET_PLACE_NUM_PROCS omp_get_place_num_procs
#define FTN_GET_PLACE_PROC_IDS omp_get_place_proc_ids
#define FTN_GET_PLACE_NUM omp_get_place_num
#define FTN_GET_PARTITION_NUM_PLACES omp_get_partition_num_places
#define FTN_GET_PARTITION_PLACE_NUMS omp_get_partition_place_nums
#define FTN_GET_INITIAL_DEVICE omp_get_initial_device
#ifdef KMP_STUB
#define FTN_TARGET_ALLOC omp_target_alloc
#define FTN_TARGET_FREE omp_target_free
#define FTN_TARGET_IS_PRESENT omp_target_is_present
#define FTN_TARGET_MEMCPY omp_target_memcpy
#define FTN_TARGET_MEMCPY_RECT omp_target_memcpy_rect
#define FTN_TARGET_ASSOCIATE_PTR omp_target_associate_ptr
#define FTN_TARGET_DISASSOCIATE_PTR omp_target_disassociate_ptr
#endif
#endif

#if OMP_50_ENABLED
#define FTN_CONTROL_TOOL omp_control_tool
#define FTN_SET_DEFAULT_ALLOCATOR omp_set_default_allocator
#define FTN_GET_DEFAULT_ALLOCATOR omp_get_default_allocator
#define FTN_ALLOC omp_alloc
#define FTN_FREE omp_free
#define FTN_GET_DEVICE_NUM omp_get_device_num
#define FTN_SET_AFFINITY_FORMAT omp_set_affinity_format
#define FTN_GET_AFFINITY_FORMAT omp_get_affinity_format
#define FTN_DISPLAY_AFFINITY omp_display_affinity
#define FTN_CAPTURE_AFFINITY omp_capture_affinity
#endif

#endif /* KMP_FTN_PLAIN */

/* ------------------------------------------------------------------------ */

#if KMP_FTN_ENTRIES == KMP_FTN_APPEND

#define FTN_SET_STACKSIZE kmp_set_stacksize_
#define FTN_SET_STACKSIZE_S kmp_set_stacksize_s_
#define FTN_GET_STACKSIZE kmp_get_stacksize_
#define FTN_GET_STACKSIZE_S kmp_get_stacksize_s_
#define FTN_SET_BLOCKTIME kmp_set_blocktime_
#define FTN_GET_BLOCKTIME kmp_get_blocktime_
#define FTN_SET_LIBRARY_SERIAL kmp_set_library_serial_
#define FTN_SET_LIBRARY_TURNAROUND kmp_set_library_turnaround_
#define FTN_SET_LIBRARY_THROUGHPUT kmp_set_library_throughput_
#define FTN_SET_LIBRARY kmp_set_library_
#define FTN_GET_LIBRARY kmp_get_library_
#define FTN_SET_DEFAULTS kmp_set_defaults_
#define FTN_SET_DISP_NUM_BUFFERS kmp_set_disp_num_buffers_
#define FTN_SET_AFFINITY kmp_set_affinity_
#define FTN_GET_AFFINITY kmp_get_affinity_
#define FTN_GET_AFFINITY_MAX_PROC kmp_get_affinity_max_proc_
#define FTN_CREATE_AFFINITY_MASK kmp_create_affinity_mask_
#define FTN_DESTROY_AFFINITY_MASK kmp_destroy_affinity_mask_
#define FTN_SET_AFFINITY_MASK_PROC kmp_set_affinity_mask_proc_
#define FTN_UNSET_AFFINITY_MASK_PROC kmp_unset_affinity_mask_proc_
#define FTN_GET_AFFINITY_MASK_PROC kmp_get_affinity_mask_proc_

#define FTN_MALLOC kmp_malloc_
#define FTN_ALIGNED_MALLOC kmp_aligned_malloc_
#define FTN_CALLOC kmp_calloc_
#define FTN_REALLOC kmp_realloc_
#define FTN_KFREE kmp_free_

#define FTN_GET_NUM_KNOWN_THREADS kmp_get_num_known_threads_

#define FTN_SET_NUM_THREADS omp_set_num_threads_
#define FTN_GET_NUM_THREADS omp_get_num_threads_
#define FTN_GET_MAX_THREADS omp_get_max_threads_
#define FTN_GET_THREAD_NUM omp_get_thread_num_
#define FTN_GET_NUM_PROCS omp_get_num_procs_
#define FTN_SET_DYNAMIC omp_set_dynamic_
#define FTN_GET_DYNAMIC omp_get_dynamic_
#define FTN_SET_NESTED omp_set_nested_
#define FTN_GET_NESTED omp_get_nested_
#define FTN_IN_PARALLEL omp_in_parallel_
#define FTN_GET_THREAD_LIMIT omp_get_thread_limit_
#define FTN_SET_SCHEDULE omp_set_schedule_
#define FTN_GET_SCHEDULE omp_get_schedule_
#define FTN_SET_MAX_ACTIVE_LEVELS omp_set_max_active_levels_
#define FTN_GET_MAX_ACTIVE_LEVELS omp_get_max_active_levels_
#define FTN_GET_ACTIVE_LEVEL omp_get_active_level_
#define FTN_GET_LEVEL omp_get_level_
#define FTN_GET_ANCESTOR_THREAD_NUM omp_get_ancestor_thread_num_
#define FTN_GET_TEAM_SIZE omp_get_team_size_
#define FTN_IN_FINAL omp_in_final_
//  #define FTN_SET_PROC_BIND                    omp_set_proc_bind_
#define FTN_GET_PROC_BIND omp_get_proc_bind_
//  #define FTN_CURR_PROC_BIND                   omp_curr_proc_bind_
#if OMP_40_ENABLED
#define FTN_GET_NUM_TEAMS omp_get_num_teams_
#define FTN_GET_TEAM_NUM omp_get_team_num_
#endif
#define FTN_INIT_LOCK omp_init_lock_
#if KMP_USE_DYNAMIC_LOCK
#define FTN_INIT_LOCK_WITH_HINT omp_init_lock_with_hint_
#define FTN_INIT_NEST_LOCK_WITH_HINT omp_init_nest_lock_with_hint_
#endif
#define FTN_DESTROY_LOCK omp_destroy_lock_
#define FTN_SET_LOCK omp_set_lock_
#define FTN_UNSET_LOCK omp_unset_lock_
#define FTN_TEST_LOCK omp_test_lock_
#define FTN_INIT_NEST_LOCK omp_init_nest_lock_
#define FTN_DESTROY_NEST_LOCK omp_destroy_nest_lock_
#define FTN_SET_NEST_LOCK omp_set_nest_lock_
#define FTN_UNSET_NEST_LOCK omp_unset_nest_lock_
#define FTN_TEST_NEST_LOCK omp_test_nest_lock_

#define FTN_SET_WARNINGS_ON kmp_set_warnings_on_
#define FTN_SET_WARNINGS_OFF kmp_set_warnings_off_

#define FTN_GET_WTIME omp_get_wtime_
#define FTN_GET_WTICK omp_get_wtick_

#if OMP_40_ENABLED
#define FTN_GET_NUM_DEVICES omp_get_num_devices_
#define FTN_GET_DEFAULT_DEVICE omp_get_default_device_
#define FTN_SET_DEFAULT_DEVICE omp_set_default_device_
#define FTN_IS_INITIAL_DEVICE omp_is_initial_device_
#endif

#if OMP_40_ENABLED
#define FTN_GET_CANCELLATION omp_get_cancellation_
#define FTN_GET_CANCELLATION_STATUS kmp_get_cancellation_status_
#endif

#if OMP_45_ENABLED
#define FTN_GET_MAX_TASK_PRIORITY omp_get_max_task_priority_
#define FTN_GET_NUM_PLACES omp_get_num_places_
#define FTN_GET_PLACE_NUM_PROCS omp_get_place_num_procs_
#define FTN_GET_PLACE_PROC_IDS omp_get_place_proc_ids_
#define FTN_GET_PLACE_NUM omp_get_place_num_
#define FTN_GET_PARTITION_NUM_PLACES omp_get_partition_num_places_
#define FTN_GET_PARTITION_PLACE_NUMS omp_get_partition_place_nums_
#define FTN_GET_INITIAL_DEVICE omp_get_initial_device_
#ifdef KMP_STUB
#define FTN_TARGET_ALLOC omp_target_alloc_
#define FTN_TARGET_FREE omp_target_free_
#define FTN_TARGET_IS_PRESENT omp_target_is_present_
#define FTN_TARGET_MEMCPY omp_target_memcpy_
#define FTN_TARGET_MEMCPY_RECT omp_target_memcpy_rect_
#define FTN_TARGET_ASSOCIATE_PTR omp_target_associate_ptr_
#define FTN_TARGET_DISASSOCIATE_PTR omp_target_disassociate_ptr_
#endif
#endif

#if OMP_50_ENABLED
#define FTN_CONTROL_TOOL omp_control_tool_
#define FTN_SET_DEFAULT_ALLOCATOR omp_set_default_allocator_
#define FTN_GET_DEFAULT_ALLOCATOR omp_get_default_allocator_
#define FTN_ALLOC omp_alloc_
#define FTN_FREE omp_free_
#define FTN_GET_DEVICE_NUM omp_get_device_num_
#define FTN_SET_AFFINITY_FORMAT omp_set_affinity_format_
#define FTN_GET_AFFINITY_FORMAT omp_get_affinity_format_
#define FTN_DISPLAY_AFFINITY omp_display_affinity_
#define FTN_CAPTURE_AFFINITY omp_capture_affinity_
#endif

#endif /* KMP_FTN_APPEND */

/* ------------------------------------------------------------------------ */

#if KMP_FTN_ENTRIES == KMP_FTN_UPPER

#define FTN_SET_STACKSIZE KMP_SET_STACKSIZE
#define FTN_SET_STACKSIZE_S KMP_SET_STACKSIZE_S
#define FTN_GET_STACKSIZE KMP_GET_STACKSIZE
#define FTN_GET_STACKSIZE_S KMP_GET_STACKSIZE_S
#define FTN_SET_BLOCKTIME KMP_SET_BLOCKTIME
#define FTN_GET_BLOCKTIME KMP_GET_BLOCKTIME
#define FTN_SET_LIBRARY_SERIAL KMP_SET_LIBRARY_SERIAL
#define FTN_SET_LIBRARY_TURNAROUND KMP_SET_LIBRARY_TURNAROUND
#define FTN_SET_LIBRARY_THROUGHPUT KMP_SET_LIBRARY_THROUGHPUT
#define FTN_SET_LIBRARY KMP_SET_LIBRARY
#define FTN_GET_LIBRARY KMP_GET_LIBRARY
#define FTN_SET_DEFAULTS KMP_SET_DEFAULTS
#define FTN_SET_DISP_NUM_BUFFERS KMP_SET_DISP_NUM_BUFFERS
#define FTN_SET_AFFINITY KMP_SET_AFFINITY
#define FTN_GET_AFFINITY KMP_GET_AFFINITY
#define FTN_GET_AFFINITY_MAX_PROC KMP_GET_AFFINITY_MAX_PROC
#define FTN_CREATE_AFFINITY_MASK KMP_CREATE_AFFINITY_MASK
#define FTN_DESTROY_AFFINITY_MASK KMP_DESTROY_AFFINITY_MASK
#define FTN_SET_AFFINITY_MASK_PROC KMP_SET_AFFINITY_MASK_PROC
#define FTN_UNSET_AFFINITY_MASK_PROC KMP_UNSET_AFFINITY_MASK_PROC
#define FTN_GET_AFFINITY_MASK_PROC KMP_GET_AFFINITY_MASK_PROC

#define FTN_MALLOC KMP_MALLOC
#define FTN_ALIGNED_MALLOC KMP_ALIGNED_MALLOC
#define FTN_CALLOC KMP_CALLOC
#define FTN_REALLOC KMP_REALLOC
#define FTN_KFREE KMP_FREE

#define FTN_GET_NUM_KNOWN_THREADS KMP_GET_NUM_KNOWN_THREADS

#define FTN_SET_NUM_THREADS OMP_SET_NUM_THREADS
#define FTN_GET_NUM_THREADS OMP_GET_NUM_THREADS
#define FTN_GET_MAX_THREADS OMP_GET_MAX_THREADS
#define FTN_GET_THREAD_NUM OMP_GET_THREAD_NUM
#define FTN_GET_NUM_PROCS OMP_GET_NUM_PROCS
#define FTN_SET_DYNAMIC OMP_SET_DYNAMIC
#define FTN_GET_DYNAMIC OMP_GET_DYNAMIC
#define FTN_SET_NESTED OMP_SET_NESTED
#define FTN_GET_NESTED OMP_GET_NESTED
#define FTN_IN_PARALLEL OMP_IN_PARALLEL
#define FTN_GET_THREAD_LIMIT OMP_GET_THREAD_LIMIT
#define FTN_SET_SCHEDULE OMP_SET_SCHEDULE
#define FTN_GET_SCHEDULE OMP_GET_SCHEDULE
#define FTN_SET_MAX_ACTIVE_LEVELS OMP_SET_MAX_ACTIVE_LEVELS
#define FTN_GET_MAX_ACTIVE_LEVELS OMP_GET_MAX_ACTIVE_LEVELS
#define FTN_GET_ACTIVE_LEVEL OMP_GET_ACTIVE_LEVEL
#define FTN_GET_LEVEL OMP_GET_LEVEL
#define FTN_GET_ANCESTOR_THREAD_NUM OMP_GET_ANCESTOR_THREAD_NUM
#define FTN_GET_TEAM_SIZE OMP_GET_TEAM_SIZE
#define FTN_IN_FINAL OMP_IN_FINAL
//  #define FTN_SET_PROC_BIND                    OMP_SET_PROC_BIND
#define FTN_GET_PROC_BIND OMP_GET_PROC_BIND
//  #define FTN_CURR_PROC_BIND                   OMP_CURR_PROC_BIND
#if OMP_40_ENABLED
#define FTN_GET_NUM_TEAMS OMP_GET_NUM_TEAMS
#define FTN_GET_TEAM_NUM OMP_GET_TEAM_NUM
#endif
#define FTN_INIT_LOCK OMP_INIT_LOCK
#if KMP_USE_DYNAMIC_LOCK
#define FTN_INIT_LOCK_WITH_HINT OMP_INIT_LOCK_WITH_HINT
#define FTN_INIT_NEST_LOCK_WITH_HINT OMP_INIT_NEST_LOCK_WITH_HINT
#endif
#define FTN_DESTROY_LOCK OMP_DESTROY_LOCK
#define FTN_SET_LOCK OMP_SET_LOCK
#define FTN_UNSET_LOCK OMP_UNSET_LOCK
#define FTN_TEST_LOCK OMP_TEST_LOCK
#define FTN_INIT_NEST_LOCK OMP_INIT_NEST_LOCK
#define FTN_DESTROY_NEST_LOCK OMP_DESTROY_NEST_LOCK
#define FTN_SET_NEST_LOCK OMP_SET_NEST_LOCK
#define FTN_UNSET_NEST_LOCK OMP_UNSET_NEST_LOCK
#define FTN_TEST_NEST_LOCK OMP_TEST_NEST_LOCK

#define FTN_SET_WARNINGS_ON KMP_SET_WARNINGS_ON
#define FTN_SET_WARNINGS_OFF KMP_SET_WARNINGS_OFF

#define FTN_GET_WTIME OMP_GET_WTIME
#define FTN_GET_WTICK OMP_GET_WTICK

#if OMP_40_ENABLED
#define FTN_GET_NUM_DEVICES OMP_GET_NUM_DEVICES
#define FTN_GET_DEFAULT_DEVICE OMP_GET_DEFAULT_DEVICE
#define FTN_SET_DEFAULT_DEVICE OMP_SET_DEFAULT_DEVICE
#define FTN_IS_INITIAL_DEVICE OMP_IS_INITIAL_DEVICE
#endif

#if OMP_40_ENABLED
#define FTN_GET_CANCELLATION OMP_GET_CANCELLATION
#define FTN_GET_CANCELLATION_STATUS KMP_GET_CANCELLATION_STATUS
#endif

#if OMP_45_ENABLED
#define FTN_GET_MAX_TASK_PRIORITY OMP_GET_MAX_TASK_PRIORITY
#define FTN_GET_NUM_PLACES OMP_GET_NUM_PLACES
#define FTN_GET_PLACE_NUM_PROCS OMP_GET_PLACE_NUM_PROCS
#define FTN_GET_PLACE_PROC_IDS OMP_GET_PLACE_PROC_IDS
#define FTN_GET_PLACE_NUM OMP_GET_PLACE_NUM
#define FTN_GET_PARTITION_NUM_PLACES OMP_GET_PARTITION_NUM_PLACES
#define FTN_GET_PARTITION_PLACE_NUMS OMP_GET_PARTITION_PLACE_NUMS
#define FTN_GET_INITIAL_DEVICE OMP_GET_INITIAL_DEVICE
#ifdef KMP_STUB
#define FTN_TARGET_ALLOC OMP_TARGET_ALLOC
#define FTN_TARGET_FREE OMP_TARGET_FREE
#define FTN_TARGET_IS_PRESENT OMP_TARGET_IS_PRESENT
#define FTN_TARGET_MEMCPY OMP_TARGET_MEMCPY
#define FTN_TARGET_MEMCPY_RECT OMP_TARGET_MEMCPY_RECT
#define FTN_TARGET_ASSOCIATE_PTR OMP_TARGET_ASSOCIATE_PTR
#define FTN_TARGET_DISASSOCIATE_PTR OMP_TARGET_DISASSOCIATE_PTR
#endif
#endif

#if OMP_50_ENABLED
#define FTN_CONTROL_TOOL OMP_CONTROL_TOOL
#define FTN_SET_DEFAULT_ALLOCATOR OMP_SET_DEFAULT_ALLOCATOR
#define FTN_GET_DEFAULT_ALLOCATOR OMP_GET_DEFAULT_ALLOCATOR
#define FTN_ALLOC OMP_ALLOC
#define FTN_FREE OMP_FREE
#define FTN_GET_DEVICE_NUM OMP_GET_DEVICE_NUM
#define FTN_SET_AFFINITY_FORMAT OMP_SET_AFFINITY_FORMAT
#define FTN_GET_AFFINITY_FORMAT OMP_GET_AFFINITY_FORMAT
#define FTN_DISPLAY_AFFINITY OMP_DISPLAY_AFFINITY
#define FTN_CAPTURE_AFFINITY OMP_CAPTURE_AFFINITY
#endif

#endif /* KMP_FTN_UPPER */

/* ------------------------------------------------------------------------ */

#if KMP_FTN_ENTRIES == KMP_FTN_UAPPEND

#define FTN_SET_STACKSIZE KMP_SET_STACKSIZE_
#define FTN_SET_STACKSIZE_S KMP_SET_STACKSIZE_S_
#define FTN_GET_STACKSIZE KMP_GET_STACKSIZE_
#define FTN_GET_STACKSIZE_S KMP_GET_STACKSIZE_S_
#define FTN_SET_BLOCKTIME KMP_SET_BLOCKTIME_
#define FTN_GET_BLOCKTIME KMP_GET_BLOCKTIME_
#define FTN_SET_LIBRARY_SERIAL KMP_SET_LIBRARY_SERIAL_
#define FTN_SET_LIBRARY_TURNAROUND KMP_SET_LIBRARY_TURNAROUND_
#define FTN_SET_LIBRARY_THROUGHPUT KMP_SET_LIBRARY_THROUGHPUT_
#define FTN_SET_LIBRARY KMP_SET_LIBRARY_
#define FTN_GET_LIBRARY KMP_GET_LIBRARY_
#define FTN_SET_DEFAULTS KMP_SET_DEFAULTS_
#define FTN_SET_DISP_NUM_BUFFERS KMP_SET_DISP_NUM_BUFFERS_
#define FTN_SET_AFFINITY KMP_SET_AFFINITY_
#define FTN_GET_AFFINITY KMP_GET_AFFINITY_
#define FTN_GET_AFFINITY_MAX_PROC KMP_GET_AFFINITY_MAX_PROC_
#define FTN_CREATE_AFFINITY_MASK KMP_CREATE_AFFINITY_MASK_
#define FTN_DESTROY_AFFINITY_MASK KMP_DESTROY_AFFINITY_MASK_
#define FTN_SET_AFFINITY_MASK_PROC KMP_SET_AFFINITY_MASK_PROC_
#define FTN_UNSET_AFFINITY_MASK_PROC KMP_UNSET_AFFINITY_MASK_PROC_
#define FTN_GET_AFFINITY_MASK_PROC KMP_GET_AFFINITY_MASK_PROC_

#define FTN_MALLOC KMP_MALLOC_
#define FTN_ALIGNED_MALLOC KMP_ALIGNED_MALLOC_
#define FTN_CALLOC KMP_CALLOC_
#define FTN_REALLOC KMP_REALLOC_
#define FTN_KFREE KMP_FREE_

#define FTN_GET_NUM_KNOWN_THREADS KMP_GET_NUM_KNOWN_THREADS_

#define FTN_SET_NUM_THREADS OMP_SET_NUM_THREADS_
#define FTN_GET_NUM_THREADS OMP_GET_NUM_THREADS_
#define FTN_GET_MAX_THREADS OMP_GET_MAX_THREADS_
#define FTN_GET_THREAD_NUM OMP_GET_THREAD_NUM_
#define FTN_GET_NUM_PROCS OMP_GET_NUM_PROCS_
#define FTN_SET_DYNAMIC OMP_SET_DYNAMIC_
#define FTN_GET_DYNAMIC OMP_GET_DYNAMIC_
#define FTN_SET_NESTED OMP_SET_NESTED_
#define FTN_GET_NESTED OMP_GET_NESTED_
#define FTN_IN_PARALLEL OMP_IN_PARALLEL_
#define FTN_GET_THREAD_LIMIT OMP_GET_THREAD_LIMIT_
#define FTN_SET_SCHEDULE OMP_SET_SCHEDULE_
#define FTN_GET_SCHEDULE OMP_GET_SCHEDULE_
#define FTN_SET_MAX_ACTIVE_LEVELS OMP_SET_MAX_ACTIVE_LEVELS_
#define FTN_GET_MAX_ACTIVE_LEVELS OMP_GET_MAX_ACTIVE_LEVELS_
#define FTN_GET_ACTIVE_LEVEL OMP_GET_ACTIVE_LEVEL_
#define FTN_GET_LEVEL OMP_GET_LEVEL_
#define FTN_GET_ANCESTOR_THREAD_NUM OMP_GET_ANCESTOR_THREAD_NUM_
#define FTN_GET_TEAM_SIZE OMP_GET_TEAM_SIZE_
#define FTN_IN_FINAL OMP_IN_FINAL_
//  #define FTN_SET_PROC_BIND                    OMP_SET_PROC_BIND_
#define FTN_GET_PROC_BIND OMP_GET_PROC_BIND_
//  #define FTN_CURR_PROC_BIND                   OMP_CURR_PROC_BIND_
#if OMP_40_ENABLED
#define FTN_GET_NUM_TEAMS OMP_GET_NUM_TEAMS_
#define FTN_GET_TEAM_NUM OMP_GET_TEAM_NUM_
#endif
#define FTN_INIT_LOCK OMP_INIT_LOCK_
#if KMP_USE_DYNAMIC_LOCK
#define FTN_INIT_LOCK_WITH_HINT OMP_INIT_LOCK_WITH_HINT_
#define FTN_INIT_NEST_LOCK_WITH_HINT OMP_INIT_NEST_LOCK_WITH_HINT_
#endif
#define FTN_DESTROY_LOCK OMP_DESTROY_LOCK_
#define FTN_SET_LOCK OMP_SET_LOCK_
#define FTN_UNSET_LOCK OMP_UNSET_LOCK_
#define FTN_TEST_LOCK OMP_TEST_LOCK_
#define FTN_INIT_NEST_LOCK OMP_INIT_NEST_LOCK_
#define FTN_DESTROY_NEST_LOCK OMP_DESTROY_NEST_LOCK_
#define FTN_SET_NEST_LOCK OMP_SET_NEST_LOCK_
#define FTN_UNSET_NEST_LOCK OMP_UNSET_NEST_LOCK_
#define FTN_TEST_NEST_LOCK OMP_TEST_NEST_LOCK_

#define FTN_SET_WARNINGS_ON KMP_SET_WARNINGS_ON_
#define FTN_SET_WARNINGS_OFF KMP_SET_WARNINGS_OFF_

#define FTN_GET_WTIME OMP_GET_WTIME_
#define FTN_GET_WTICK OMP_GET_WTICK_

#if OMP_40_ENABLED
#define FTN_GET_NUM_DEVICES OMP_GET_NUM_DEVICES_
#define FTN_GET_DEFAULT_DEVICE OMP_GET_DEFAULT_DEVICE_
#define FTN_SET_DEFAULT_DEVICE OMP_SET_DEFAULT_DEVICE_
#define FTN_IS_INITIAL_DEVICE OMP_IS_INITIAL_DEVICE_
#endif

#if OMP_40_ENABLED
#define FTN_GET_CANCELLATION OMP_GET_CANCELLATION_
#define FTN_GET_CANCELLATION_STATUS KMP_GET_CANCELLATION_STATUS_
#endif

#if OMP_45_ENABLED
#define FTN_GET_MAX_TASK_PRIORITY OMP_GET_MAX_TASK_PRIORITY_
#define FTN_GET_NUM_PLACES OMP_GET_NUM_PLACES_
#define FTN_GET_PLACE_NUM_PROCS OMP_GET_PLACE_NUM_PROCS_
#define FTN_GET_PLACE_PROC_IDS OMP_GET_PLACE_PROC_IDS_
#define FTN_GET_PLACE_NUM OMP_GET_PLACE_NUM_
#define FTN_GET_PARTITION_NUM_PLACES OMP_GET_PARTITION_NUM_PLACES_
#define FTN_GET_PARTITION_PLACE_NUMS OMP_GET_PARTITION_PLACE_NUMS_
#define FTN_GET_INITIAL_DEVICE OMP_GET_INITIAL_DEVICE_
#ifdef KMP_STUB
#define FTN_TARGET_ALLOC OMP_TARGET_ALLOC_
#define FTN_TARGET_FREE OMP_TARGET_FREE_
#define FTN_TARGET_IS_PRESENT OMP_TARGET_IS_PRESENT_
#define FTN_TARGET_MEMCPY OMP_TARGET_MEMCPY_
#define FTN_TARGET_MEMCPY_RECT OMP_TARGET_MEMCPY_RECT_
#define FTN_TARGET_ASSOCIATE_PTR OMP_TARGET_ASSOCIATE_PTR_
#define FTN_TARGET_DISASSOCIATE_PTR OMP_TARGET_DISASSOCIATE_PTR_
#endif
#endif

#if OMP_50_ENABLED
#define FTN_CONTROL_TOOL OMP_CONTROL_TOOL_
#define FTN_SET_DEFAULT_ALLOCATOR OMP_SET_DEFAULT_ALLOCATOR_
#define FTN_GET_DEFAULT_ALLOCATOR OMP_GET_DEFAULT_ALLOCATOR_
#define FTN_ALLOC OMP_ALLOC_
#define FTN_FREE OMP_FREE_
#define FTN_GET_DEVICE_NUM OMP_GET_DEVICE_NUM_
#define FTN_SET_AFFINITY_FORMAT OMP_SET_AFFINITY_FORMAT_
#define FTN_GET_AFFINITY_FORMAT OMP_GET_AFFINITY_FORMAT_
#define FTN_DISPLAY_AFFINITY OMP_DISPLAY_AFFINITY_
#define FTN_CAPTURE_AFFINITY OMP_CAPTURE_AFFINITY_
#endif

#endif /* KMP_FTN_UAPPEND */

/* -------------------------- GOMP API NAMES ------------------------ */
// All GOMP_1.0 symbols
#define KMP_API_NAME_GOMP_ATOMIC_END GOMP_atomic_end
#define KMP_API_NAME_GOMP_ATOMIC_START GOMP_atomic_start
#define KMP_API_NAME_GOMP_BARRIER GOMP_barrier
#define KMP_API_NAME_GOMP_CRITICAL_END GOMP_critical_end
#define KMP_API_NAME_GOMP_CRITICAL_NAME_END GOMP_critical_name_end
#define KMP_API_NAME_GOMP_CRITICAL_NAME_START GOMP_critical_name_start
#define KMP_API_NAME_GOMP_CRITICAL_START GOMP_critical_start
#define KMP_API_NAME_GOMP_LOOP_DYNAMIC_NEXT GOMP_loop_dynamic_next
#define KMP_API_NAME_GOMP_LOOP_DYNAMIC_START GOMP_loop_dynamic_start
#define KMP_API_NAME_GOMP_LOOP_END GOMP_loop_end
#define KMP_API_NAME_GOMP_LOOP_END_NOWAIT GOMP_loop_end_nowait
#define KMP_API_NAME_GOMP_LOOP_GUIDED_NEXT GOMP_loop_guided_next
#define KMP_API_NAME_GOMP_LOOP_GUIDED_START GOMP_loop_guided_start
#define KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_NEXT                            \
  GOMP_loop_ordered_dynamic_next
#define KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_START                           \
  GOMP_loop_ordered_dynamic_start
#define KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_NEXT GOMP_loop_ordered_guided_next
#define KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_START                            \
  GOMP_loop_ordered_guided_start
#define KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_NEXT                            \
  GOMP_loop_ordered_runtime_next
#define KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_START                           \
  GOMP_loop_ordered_runtime_start
#define KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_NEXT GOMP_loop_ordered_static_next
#define KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_START                            \
  GOMP_loop_ordered_static_start
#define KMP_API_NAME_GOMP_LOOP_RUNTIME_NEXT GOMP_loop_runtime_next
#define KMP_API_NAME_GOMP_LOOP_RUNTIME_START GOMP_loop_runtime_start
#define KMP_API_NAME_GOMP_LOOP_STATIC_NEXT GOMP_loop_static_next
#define KMP_API_NAME_GOMP_LOOP_STATIC_START GOMP_loop_static_start
#define KMP_API_NAME_GOMP_ORDERED_END GOMP_ordered_end
#define KMP_API_NAME_GOMP_ORDERED_START GOMP_ordered_start
#define KMP_API_NAME_GOMP_PARALLEL_END GOMP_parallel_end
#define KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC_START                          \
  GOMP_parallel_loop_dynamic_start
#define KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED_START                           \
  GOMP_parallel_loop_guided_start
#define KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME_START                          \
  GOMP_parallel_loop_runtime_start
#define KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC_START                           \
  GOMP_parallel_loop_static_start
#define KMP_API_NAME_GOMP_PARALLEL_SECTIONS_START GOMP_parallel_sections_start
#define KMP_API_NAME_GOMP_PARALLEL_START GOMP_parallel_start
#define KMP_API_NAME_GOMP_SECTIONS_END GOMP_sections_end
#define KMP_API_NAME_GOMP_SECTIONS_END_NOWAIT GOMP_sections_end_nowait
#define KMP_API_NAME_GOMP_SECTIONS_NEXT GOMP_sections_next
#define KMP_API_NAME_GOMP_SECTIONS_START GOMP_sections_start
#define KMP_API_NAME_GOMP_SINGLE_COPY_END GOMP_single_copy_end
#define KMP_API_NAME_GOMP_SINGLE_COPY_START GOMP_single_copy_start
#define KMP_API_NAME_GOMP_SINGLE_START GOMP_single_start

// All GOMP_2.0 symbols
#define KMP_API_NAME_GOMP_TASK GOMP_task
#define KMP_API_NAME_GOMP_TASKWAIT GOMP_taskwait
#define KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_NEXT GOMP_loop_ull_dynamic_next
#define KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_START GOMP_loop_ull_dynamic_start
#define KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_NEXT GOMP_loop_ull_guided_next
#define KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_START GOMP_loop_ull_guided_start
#define KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_NEXT                        \
  GOMP_loop_ull_ordered_dynamic_next
#define KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_START                       \
  GOMP_loop_ull_ordered_dynamic_start
#define KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_NEXT                         \
  GOMP_loop_ull_ordered_guided_next
#define KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_START                        \
  GOMP_loop_ull_ordered_guided_start
#define KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_NEXT                        \
  GOMP_loop_ull_ordered_runtime_next
#define KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_START                       \
  GOMP_loop_ull_ordered_runtime_start
#define KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_NEXT                         \
  GOMP_loop_ull_ordered_static_next
#define KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_START                        \
  GOMP_loop_ull_ordered_static_start
#define KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_NEXT GOMP_loop_ull_runtime_next
#define KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_START GOMP_loop_ull_runtime_start
#define KMP_API_NAME_GOMP_LOOP_ULL_STATIC_NEXT GOMP_loop_ull_static_next
#define KMP_API_NAME_GOMP_LOOP_ULL_STATIC_START GOMP_loop_ull_static_start

// All GOMP_3.0 symbols
#define KMP_API_NAME_GOMP_TASKYIELD GOMP_taskyield

// All GOMP_4.0 symbols
// TODO: As of 2013-10-14, none of the GOMP_4.0 functions are implemented in
// libomp
#define KMP_API_NAME_GOMP_BARRIER_CANCEL GOMP_barrier_cancel
#define KMP_API_NAME_GOMP_CANCEL GOMP_cancel
#define KMP_API_NAME_GOMP_CANCELLATION_POINT GOMP_cancellation_point
#define KMP_API_NAME_GOMP_LOOP_END_CANCEL GOMP_loop_end_cancel
#define KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC GOMP_parallel_loop_dynamic
#define KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED GOMP_parallel_loop_guided
#define KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME GOMP_parallel_loop_runtime
#define KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC GOMP_parallel_loop_static
#define KMP_API_NAME_GOMP_PARALLEL_SECTIONS GOMP_parallel_sections
#define KMP_API_NAME_GOMP_PARALLEL GOMP_parallel
#define KMP_API_NAME_GOMP_SECTIONS_END_CANCEL GOMP_sections_end_cancel
#define KMP_API_NAME_GOMP_TASKGROUP_START GOMP_taskgroup_start
#define KMP_API_NAME_GOMP_TASKGROUP_END GOMP_taskgroup_end
/* Target functions should be taken care of by liboffload */
#define KMP_API_NAME_GOMP_TARGET GOMP_target
#define KMP_API_NAME_GOMP_TARGET_DATA GOMP_target_data
#define KMP_API_NAME_GOMP_TARGET_END_DATA GOMP_target_end_data
#define KMP_API_NAME_GOMP_TARGET_UPDATE GOMP_target_update
#define KMP_API_NAME_GOMP_TEAMS GOMP_teams

// All GOMP_4.5 symbols
#define KMP_API_NAME_GOMP_TASKLOOP GOMP_taskloop
#define KMP_API_NAME_GOMP_TASKLOOP_ULL GOMP_taskloop_ull
#define KMP_API_NAME_GOMP_DOACROSS_POST GOMP_doacross_post
#define KMP_API_NAME_GOMP_DOACROSS_WAIT GOMP_doacross_wait
#define KMP_API_NAME_GOMP_LOOP_DOACROSS_STATIC_START                           \
  GOMP_loop_doacross_static_start
#define KMP_API_NAME_GOMP_LOOP_DOACROSS_DYNAMIC_START                          \
  GOMP_loop_doacross_dynamic_start
#define KMP_API_NAME_GOMP_LOOP_DOACROSS_GUIDED_START                           \
  GOMP_loop_doacross_guided_start
#define KMP_API_NAME_GOMP_LOOP_DOACROSS_RUNTIME_START                          \
  GOMP_loop_doacross_runtime_start
#define KMP_API_NAME_GOMP_DOACROSS_ULL_POST GOMP_doacross_ull_post
#define KMP_API_NAME_GOMP_DOACROSS_ULL_WAIT GOMP_doacross_ull_wait
#define KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_STATIC_START                       \
  GOMP_loop_ull_doacross_static_start
#define KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_DYNAMIC_START                      \
  GOMP_loop_ull_doacross_dynamic_start
#define KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_GUIDED_START                       \
  GOMP_loop_ull_doacross_guided_start
#define KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_RUNTIME_START                      \
  GOMP_loop_ull_doacross_runtime_start

#endif /* KMP_FTN_OS_H */
