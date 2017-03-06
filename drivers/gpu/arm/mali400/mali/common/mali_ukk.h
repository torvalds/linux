/*
 * Copyright (C) 2010-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_ukk.h
 * Defines the kernel-side interface of the user-kernel interface
 */

#ifndef __MALI_UKK_H__
#define __MALI_UKK_H__

#include "mali_osk.h"
#include "mali_uk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup uddapi Unified Device Driver (UDD) APIs
 *
 * @{
 */

/**
 * @addtogroup u_k_api UDD User/Kernel Interface (U/K) APIs
 *
 * - The _mali_uk functions are an abstraction of the interface to the device
 * driver. On certain OSs, this would be implemented via the IOCTL interface.
 * On other OSs, it could be via extension of some Device Driver Class, or
 * direct function call for Bare metal/RTOSs.
 * - It is important to note that:
 *   -  The Device Driver has implemented the _mali_ukk set of functions
 *   -  The Base Driver calls the corresponding set of _mali_uku functions.
 * - What requires porting is solely the calling mechanism from User-side to
 * Kernel-side, and propagating back the results.
 * - Each U/K function is associated with a (group, number) pair from
 * \ref _mali_uk_functions to make it possible for a common function in the
 * Base Driver and Device Driver to route User/Kernel calls from/to the
 * correct _mali_uk function. For example, in an IOCTL system, the IOCTL number
 * would be formed based on the group and number assigned to the _mali_uk
 * function, as listed in \ref _mali_uk_functions. On the user-side, each
 * _mali_uku function would just make an IOCTL with the IOCTL-code being an
 * encoded form of the (group, number) pair. On the kernel-side, the Device
 * Driver's IOCTL handler decodes the IOCTL-code back into a (group, number)
 * pair, and uses this to determine which corresponding _mali_ukk should be
 * called.
 *   - Refer to \ref _mali_uk_functions for more information about this
 * (group, number) pairing.
 * - In a system where there is no distinction between user and kernel-side,
 * the U/K interface may be implemented as:@code
 * MALI_STATIC_INLINE _mali_osk_errcode_t _mali_uku_examplefunction( _mali_uk_examplefunction_s *args )
 * {
 *     return mali_ukk_examplefunction( args );
 * }
 * @endcode
 * - Therefore, all U/K calls behave \em as \em though they were direct
 * function calls (but the \b implementation \em need \em not be a direct
 * function calls)
 *
 * @note Naming the _mali_uk functions the same on both User and Kernel sides
 * on non-RTOS systems causes debugging issues when setting breakpoints. In
 * this case, it is not clear which function the breakpoint is put on.
 * Therefore the _mali_uk functions in user space are prefixed with \c _mali_uku
 * and in kernel space with \c _mali_ukk. The naming for the argument
 * structures is unaffected.
 *
 * - The _mali_uk functions are synchronous.
 * - Arguments to the _mali_uk functions are passed in a structure. The only
 * parameter passed to the _mali_uk functions is a pointer to this structure.
 * This first member of this structure, ctx, is a pointer to a context returned
 * by _mali_uku_open(). For example:@code
 * typedef struct
 * {
 *     void *ctx;
 *     u32 number_of_cores;
 * } _mali_uk_get_gp_number_of_cores_s;
 * @endcode
 *
 * - Each _mali_uk function has its own argument structure named after the
 *  function. The argument is distinguished by the _s suffix.
 * - The argument types are defined by the base driver and user-kernel
 *  interface.
 * - All _mali_uk functions return a standard \ref _mali_osk_errcode_t.
 * - Only arguments of type input or input/output need be initialized before
 * calling a _mali_uk function.
 * - Arguments of type output and input/output are only valid when the
 * _mali_uk function returns \ref _MALI_OSK_ERR_OK.
 * - The \c ctx member is always invalid after it has been used by a
 * _mali_uk function, except for the context management functions
 *
 *
 * \b Interface \b restrictions
 *
 * The requirements of the interface mean that an implementation of the
 * User-kernel interface may do no 'real' work. For example, the following are
 * illegal in the User-kernel implementation:
 * - Calling functions necessary for operation on all systems,  which would
 * not otherwise get called on RTOS systems.
 *     - For example, a  U/K interface that calls multiple _mali_ukk functions
 * during one particular U/K call. This could not be achieved by the same code
 * which uses direct function calls for the U/K interface.
 * -  Writing in values to the args members, when otherwise these members would
 * not hold a useful value for a direct function call U/K interface.
 *     - For example, U/K interface implementation that take NULL members in
 * their arguments structure from the user side, but those members are
 * replaced with non-NULL values in the kernel-side of the U/K interface
 * implementation. A scratch area for writing data is one such example. In this
 * case, a direct function call U/K interface would segfault, because no code
 * would be present to replace the NULL pointer with a meaningful pointer.
 *     - Note that we discourage the case where the U/K implementation changes
 * a NULL argument member to non-NULL, and then the Device Driver code (outside
 * of the U/K layer) re-checks this member for NULL, and corrects it when
 * necessary. Whilst such code works even on direct function call U/K
 * intefaces, it reduces the testing coverage of the Device Driver code. This
 * is because we have no way of testing the NULL == value path on an OS
 * implementation.
 *
 * A number of allowable examples exist where U/K interfaces do 'real' work:
 * - The 'pointer switching' technique for \ref _mali_ukk_get_system_info
 *     - In this case, without the pointer switching on direct function call
 * U/K interface, the Device Driver code still sees the same thing: a pointer
 * to which it can write memory. This is because such a system has no
 * distinction between a user and kernel pointer.
 * - Writing an OS-specific value into the ukk_private member for
 * _mali_ukk_mem_mmap().
 *     - In this case, this value is passed around by Device Driver code, but
 * its actual value is never checked. Device Driver code simply passes it from
 * the U/K layer to the OSK layer, where it can be acted upon. In this case,
 * \em some OS implementations of the U/K (_mali_ukk_mem_mmap()) and OSK
 * (_mali_osk_mem_mapregion_init()) functions will collaborate on the
 *  meaning of ukk_private member. On other OSs, it may be unused by both
 * U/K and OSK layers
 *     - Therefore, on error inside the U/K interface implementation itself,
 * it will be as though the _mali_ukk function itself had failed, and cleaned
 * up after itself.
 *     - Compare this to a direct function call U/K implementation, where all
 * error cleanup is handled by the _mali_ukk function itself. The direct
 * function call U/K interface implementation is automatically atomic.
 *
 * The last example highlights a consequence of all U/K interface
 * implementations: they must be atomic with respect to the Device Driver code.
 * And therefore, should Device Driver code succeed but the U/K implementation
 * fail afterwards (but before return to user-space), then the U/K
 * implementation must cause appropriate cleanup actions to preserve the
 * atomicity of the interface.
 *
 * @{
 */


/** @defgroup _mali_uk_context U/K Context management
 *
 * These functions allow for initialisation of the user-kernel interface once per process.
 *
 * Generally the context will store the OS specific object to communicate with the kernel device driver and further
 * state information required by the specific implementation. The context is shareable among all threads in the caller process.
 *
 * On IOCTL systems, this is likely to be a file descriptor as a result of opening the kernel device driver.
 *
 * On a bare-metal/RTOS system with no distinction between kernel and
 * user-space, the U/K interface simply calls the _mali_ukk variant of the
 * function by direct function call. In this case, the context returned is the
 * mali_session_data from _mali_ukk_open().
 *
 * The kernel side implementations of the U/K interface expect the first member of the argument structure to
 * be the context created by _mali_uku_open(). On some OS implementations, the meaning of this context
 * will be different between user-side and kernel-side. In which case, the kernel-side will need to replace this context
 * with the kernel-side equivalent, because user-side will not have access to kernel-side data. The context parameter
 * in the argument structure therefore has to be of type input/output.
 *
 * It should be noted that the caller cannot reuse the \c ctx member of U/K
 * argument structure after a U/K call, because it may be overwritten. Instead,
 * the context handle must always be stored  elsewhere, and copied into
 * the appropriate U/K argument structure for each user-side call to
 * the U/K interface. This is not usually a problem, since U/K argument
 * structures are usually placed on the stack.
 *
 * @{ */

/** @brief Begin a new Mali Device Driver session
 *
 * This is used to obtain a per-process context handle for all future U/K calls.
 *
 * @param context pointer to storage to return a (void*)context handle.
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_open(void **context);

/** @brief End a Mali Device Driver session
 *
 * This should be called when the process no longer requires use of the Mali Device Driver.
 *
 * The context handle must not be used after it has been closed.
 *
 * @param context pointer to a stored (void*)context handle.
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_close(void **context);

/** @} */ /* end group _mali_uk_context */


/** @addtogroup _mali_uk_core U/K Core
 *
 * The core functions provide the following functionality:
 * - verify that the user and kernel API are compatible
 * - retrieve information about the cores and memory banks in the system
 * - wait for the result of jobs started on a core
 *
 * @{ */

/** @brief Waits for a job notification.
 *
 * Sleeps until notified or a timeout occurs. Returns information about the notification.
 *
 * @param args see _mali_uk_wait_for_notification_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_wait_for_notification(_mali_uk_wait_for_notification_s *args);

/** @brief Post a notification to the notification queue of this application.
 *
 * @param args see _mali_uk_post_notification_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_post_notification(_mali_uk_post_notification_s *args);

/** @brief Verifies if the user and kernel side of this API are compatible.
 *
 * This function is obsolete, but kept to allow old, incompatible user space
 * clients to robustly detect the incompatibility.
 *
 * @param args see _mali_uk_get_api_version_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_get_api_version(_mali_uk_get_api_version_s *args);

/** @brief Verifies if the user and kernel side of this API are compatible.
 *
 * @param args see _mali_uk_get_api_version_v2_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_get_api_version_v2(_mali_uk_get_api_version_v2_s *args);

/** @brief Get the user space settings applicable for calling process.
 *
 * @param args see _mali_uk_get_user_settings_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_get_user_settings(_mali_uk_get_user_settings_s *args);

/** @brief Get a user space setting applicable for calling process.
 *
 * @param args see _mali_uk_get_user_setting_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_get_user_setting(_mali_uk_get_user_setting_s *args);

/* @brief Grant or deny high priority scheduling for this session.
 *
 * @param args see _mali_uk_request_high_priority_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_request_high_priority(_mali_uk_request_high_priority_s *args);

/** @brief Make process sleep if the pending big job in kernel  >= MALI_MAX_PENDING_BIG_JOB
 *
 */
_mali_osk_errcode_t _mali_ukk_pending_submit(_mali_uk_pending_submit_s *args);

/** @} */ /* end group _mali_uk_core */


/** @addtogroup _mali_uk_memory U/K Memory
 *
 * The memory functions provide functionality with and without a Mali-MMU present.
 *
 * For Mali-MMU based systems, the following functionality is provided:
 * - Initialize and terminate MALI virtual address space
 * - Allocate/deallocate physical memory to a MALI virtual address range and map into/unmap from the
 * current process address space
 * - Map/unmap external physical memory into the MALI virtual address range
 *
 * For Mali-nonMMU based systems:
 * - Allocate/deallocate MALI memory
 *
 * @{ */

/** @brief Map Mali Memory into the current user process
 *
 * Maps Mali memory into the current user process in a generic way.
 *
 * This function is to be used for Mali-MMU mode. The function is available in both Mali-MMU and Mali-nonMMU modes,
 * but should not be called by a user process in Mali-nonMMU mode.
 *
 * The implementation and operation of _mali_ukk_mem_mmap() is dependant on whether the driver is built for Mali-MMU
 * or Mali-nonMMU:
 * - In the nonMMU case, _mali_ukk_mem_mmap() requires a physical address to be specified. For this reason, an OS U/K
 * implementation should not allow this to be called from user-space. In any case, nonMMU implementations are
 * inherently insecure, and so the overall impact is minimal. Mali-MMU mode should be used if security is desired.
 * - In the MMU case, _mali_ukk_mem_mmap() the _mali_uk_mem_mmap_s::phys_addr
 * member is used for the \em Mali-virtual address desired for the mapping. The
 * implementation of _mali_ukk_mem_mmap() will allocate both the CPU-virtual
 * and CPU-physical addresses, and can cope with mapping a contiguous virtual
 * address range to a sequence of non-contiguous physical pages. In this case,
 * the CPU-physical addresses are not communicated back to the user-side, as
 * they are unnecsessary; the \em Mali-virtual address range must be used for
 * programming Mali structures.
 *
 * In the second (MMU) case, _mali_ukk_mem_mmap() handles management of
 * CPU-virtual and CPU-physical ranges, but the \em caller must manage the
 * \em Mali-virtual address range from the user-side.
 *
 * @note Mali-virtual address ranges are entirely separate between processes.
 * It is not possible for a process to accidentally corrupt another process'
 * \em Mali-virtual address space.
 *
 * @param args see _mali_uk_mem_mmap_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_mem_mmap(_mali_uk_mem_mmap_s *args);

/** @brief Unmap Mali Memory from the current user process
 *
 * Unmaps Mali memory from the current user process in a generic way. This only operates on Mali memory supplied
 * from _mali_ukk_mem_mmap().
 *
 * @param args see _mali_uk_mem_munmap_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_mem_munmap(_mali_uk_mem_munmap_s *args);

/** @brief Determine the buffer size necessary for an MMU page table dump.
 * @param args see _mali_uk_query_mmu_page_table_dump_size_s in mali_utgard_uk_types.h
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_query_mmu_page_table_dump_size(_mali_uk_query_mmu_page_table_dump_size_s *args);
/** @brief Dump MMU Page tables.
 * @param args see _mali_uk_dump_mmu_page_table_s in mali_utgard_uk_types.h
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_dump_mmu_page_table(_mali_uk_dump_mmu_page_table_s *args);

/** @brief Write user data to specified Mali memory without causing segfaults.
 * @param args see _mali_uk_mem_write_safe_s in mali_utgard_uk_types.h
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_mem_write_safe(_mali_uk_mem_write_safe_s *args);

/** @} */ /* end group _mali_uk_memory */


/** @addtogroup _mali_uk_pp U/K Fragment Processor
 *
 * The Fragment Processor (aka PP (Pixel Processor)) functions provide the following functionality:
 * - retrieving version of the fragment processors
 * - determine number of fragment processors
 * - starting a job on a fragment processor
 *
 * @{ */

/** @brief Issue a request to start a new job on a Fragment Processor.
 *
 * If the request fails args->status is set to _MALI_UK_START_JOB_NOT_STARTED_DO_REQUEUE and you can
 * try to start the job again.
 *
 * An existing job could be returned for requeueing if the new job has a higher priority than a previously started job
 * which the hardware hasn't actually started processing yet. In this case the new job will be started instead and the
 * existing one returned, otherwise the new job is started and the status field args->status is set to
 * _MALI_UK_START_JOB_STARTED.
 *
 * Job completion can be awaited with _mali_ukk_wait_for_notification().
 *
 * @param ctx user-kernel context (mali_session)
 * @param uargs see _mali_uk_pp_start_job_s in "mali_utgard_uk_types.h". Use _mali_osk_copy_from_user to retrieve data!
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_pp_start_job(void *ctx, _mali_uk_pp_start_job_s *uargs);

/**
 * @brief Issue a request to start new jobs on both Vertex Processor and Fragment Processor.
 *
 * @note Will call into @ref _mali_ukk_pp_start_job and @ref _mali_ukk_gp_start_job.
 *
 * @param ctx user-kernel context (mali_session)
 * @param uargs see _mali_uk_pp_and_gp_start_job_s in "mali_utgard_uk_types.h". Use _mali_osk_copy_from_user to retrieve data!
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_pp_and_gp_start_job(void *ctx, _mali_uk_pp_and_gp_start_job_s *uargs);

/** @brief Returns the number of Fragment Processors in the system
 *
 * @param args see _mali_uk_get_pp_number_of_cores_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_get_pp_number_of_cores(_mali_uk_get_pp_number_of_cores_s *args);

/** @brief Returns the version that all Fragment Processor cores are compatible with.
 *
 * This function may only be called when _mali_ukk_get_pp_number_of_cores() indicated at least one Fragment
 * Processor core is available.
 *
 * @param args see _mali_uk_get_pp_core_version_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_get_pp_core_version(_mali_uk_get_pp_core_version_s *args);

/** @brief Disable Write-back unit(s) on specified job
 *
 * @param args see _mali_uk_get_pp_core_version_s in "mali_utgard_uk_types.h"
 */
void _mali_ukk_pp_job_disable_wb(_mali_uk_pp_disable_wb_s *args);


/** @} */ /* end group _mali_uk_pp */


/** @addtogroup _mali_uk_gp U/K Vertex Processor
 *
 * The Vertex Processor (aka GP (Geometry Processor)) functions provide the following functionality:
 * - retrieving version of the Vertex Processors
 * - determine number of Vertex Processors available
 * - starting a job on a Vertex Processor
 *
 * @{ */

/** @brief Issue a request to start a new job on a Vertex Processor.
 *
 * If the request fails args->status is set to _MALI_UK_START_JOB_NOT_STARTED_DO_REQUEUE and you can
 * try to start the job again.
 *
 * An existing job could be returned for requeueing if the new job has a higher priority than a previously started job
 * which the hardware hasn't actually started processing yet. In this case the new job will be started and the
 * existing one returned, otherwise the new job is started and the status field args->status is set to
 * _MALI_UK_START_JOB_STARTED.
 *
 * Job completion can be awaited with _mali_ukk_wait_for_notification().
 *
 * @param ctx user-kernel context (mali_session)
 * @param uargs see _mali_uk_gp_start_job_s in "mali_utgard_uk_types.h". Use _mali_osk_copy_from_user to retrieve data!
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_gp_start_job(void *ctx, _mali_uk_gp_start_job_s *uargs);

/** @brief Returns the number of Vertex Processors in the system.
 *
 * @param args see _mali_uk_get_gp_number_of_cores_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_get_gp_number_of_cores(_mali_uk_get_gp_number_of_cores_s *args);

/** @brief Returns the version that all Vertex Processor cores are compatible with.
 *
 * This function may only be called when _mali_uk_get_gp_number_of_cores() indicated at least one Vertex
 * Processor core is available.
 *
 * @param args see _mali_uk_get_gp_core_version_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_get_gp_core_version(_mali_uk_get_gp_core_version_s *args);

/** @brief Resume or abort suspended Vertex Processor jobs.
 *
 * After receiving notification that a Vertex Processor job was suspended from
 * _mali_ukk_wait_for_notification() you can use this function to resume or abort the job.
 *
 * @param args see _mali_uk_gp_suspend_response_s in "mali_utgard_uk_types.h"
 * @return _MALI_OSK_ERR_OK on success, otherwise a suitable _mali_osk_errcode_t on failure.
 */
_mali_osk_errcode_t _mali_ukk_gp_suspend_response(_mali_uk_gp_suspend_response_s *args);

/** @} */ /* end group _mali_uk_gp */

#if defined(CONFIG_MALI400_PROFILING)
/** @addtogroup _mali_uk_profiling U/K Timeline profiling module
 * @{ */

/** @brief Add event to profiling buffer.
 *
 * @param args see _mali_uk_profiling_add_event_s in "mali_utgard_uk_types.h"
 */
_mali_osk_errcode_t _mali_ukk_profiling_add_event(_mali_uk_profiling_add_event_s *args);

/** @brief Get profiling stream fd.
 *
 * @param args see _mali_uk_profiling_stream_fd_get_s in "mali_utgard_uk_types.h"
 */
_mali_osk_errcode_t _mali_ukk_profiling_stream_fd_get(_mali_uk_profiling_stream_fd_get_s *args);

/** @brief Profiling control set.
 *
 * @param args see _mali_uk_profiling_control_set_s in "mali_utgard_uk_types.h"
 */
_mali_osk_errcode_t _mali_ukk_profiling_control_set(_mali_uk_profiling_control_set_s *args);

/** @} */ /* end group _mali_uk_profiling */
#endif

/** @addtogroup _mali_uk_vsync U/K VSYNC reporting module
 * @{ */

/** @brief Report events related to vsync.
 *
 * @note Events should be reported when starting to wait for vsync and when the
 * waiting is finished. This information can then be used in kernel space to
 * complement the GPU utilization metric.
 *
 * @param args see _mali_uk_vsync_event_report_s in "mali_utgard_uk_types.h"
 */
_mali_osk_errcode_t _mali_ukk_vsync_event_report(_mali_uk_vsync_event_report_s *args);

/** @} */ /* end group _mali_uk_vsync */

/** @addtogroup _mali_sw_counters_report U/K Software counter reporting
 * @{ */

/** @brief Report software counters.
 *
 * @param args see _mali_uk_sw_counters_report_s in "mali_uk_types.h"
 */
_mali_osk_errcode_t _mali_ukk_sw_counters_report(_mali_uk_sw_counters_report_s *args);

/** @} */ /* end group _mali_sw_counters_report */

/** @} */ /* end group u_k_api */

/** @} */ /* end group uddapi */

u32 _mali_ukk_report_memory_usage(void);

u32 _mali_ukk_report_total_memory_size(void);

u32 _mali_ukk_utilization_gp_pp(void);

u32 _mali_ukk_utilization_gp(void);

u32 _mali_ukk_utilization_pp(void);

#ifdef __cplusplus
}
#endif

#endif /* __MALI_UKK_H__ */
