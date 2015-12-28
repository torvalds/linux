/*
 * Copyright (C) 2010-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 * Class Path Exception
 * Linking this library statically or dynamically with other modules is making a combined work based on this library. 
 * Thus, the terms and conditions of the GNU General Public License cover the whole combination.
 * As a special exception, the copyright holders of this library give you permission to link this library with independent modules 
 * to produce an executable, regardless of the license terms of these independent modules, and to copy and distribute the resulting 
 * executable under terms of your choice, provided that you also meet, for each linked independent module, the terms and conditions 
 * of the license of that module. An independent module is a module which is not derived from or based on this library. If you modify 
 * this library, you may extend this exception to your version of the library, but you are not obligated to do so. 
 * If you do not wish to do so, delete this exception statement from your version.
 */

/**
 * @file mali_uk_types.h
 * Defines the types and constants used in the user-kernel interface
 */

#ifndef __MALI_UTGARD_UK_TYPES_H__
#define __MALI_UTGARD_UK_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Iteration functions depend on these values being consecutive. */
#define MALI_UK_TIMELINE_GP   0
#define MALI_UK_TIMELINE_PP   1
#define MALI_UK_TIMELINE_SOFT 2
#define MALI_UK_TIMELINE_MAX  3

#define MALI_UK_BIG_VARYING_SIZE  (1024*1024*2)

typedef struct {
	u32 points[MALI_UK_TIMELINE_MAX];
	s32 sync_fd;
} _mali_uk_fence_t;

/**
 * @addtogroup uddapi Unified Device Driver (UDD) APIs
 *
 * @{
 */

/**
 * @addtogroup u_k_api UDD User/Kernel Interface (U/K) APIs
 *
 * @{
 */

/** @defgroup _mali_uk_core U/K Core
 * @{ */

/** Definition of subsystem numbers, to assist in creating a unique identifier
 * for each U/K call.
 *
 * @see _mali_uk_functions */
typedef enum {
	_MALI_UK_CORE_SUBSYSTEM,      /**< Core Group of U/K calls */
	_MALI_UK_MEMORY_SUBSYSTEM,    /**< Memory Group of U/K calls */
	_MALI_UK_PP_SUBSYSTEM,        /**< Fragment Processor Group of U/K calls */
	_MALI_UK_GP_SUBSYSTEM,        /**< Vertex Processor Group of U/K calls */
	_MALI_UK_PROFILING_SUBSYSTEM, /**< Profiling Group of U/K calls */
	_MALI_UK_VSYNC_SUBSYSTEM,     /**< VSYNC Group of U/K calls */
} _mali_uk_subsystem_t;

/** Within a function group each function has its unique sequence number
 * to assist in creating a unique identifier for each U/K call.
 *
 * An ordered pair of numbers selected from
 * ( \ref _mali_uk_subsystem_t,\ref  _mali_uk_functions) will uniquely identify the
 * U/K call across all groups of functions, and all functions. */
typedef enum {
	/** Core functions */

	_MALI_UK_OPEN                    = 0, /**< _mali_ukk_open() */
	_MALI_UK_CLOSE,                       /**< _mali_ukk_close() */
	_MALI_UK_WAIT_FOR_NOTIFICATION,       /**< _mali_ukk_wait_for_notification() */
	_MALI_UK_GET_API_VERSION,             /**< _mali_ukk_get_api_version() */
	_MALI_UK_POST_NOTIFICATION,           /**< _mali_ukk_post_notification() */
	_MALI_UK_GET_USER_SETTING,            /**< _mali_ukk_get_user_setting() *//**< [out] */
	_MALI_UK_GET_USER_SETTINGS,           /**< _mali_ukk_get_user_settings() *//**< [out] */
	_MALI_UK_REQUEST_HIGH_PRIORITY,       /**< _mali_ukk_request_high_priority() */
	_MALI_UK_TIMELINE_GET_LATEST_POINT,   /**< _mali_ukk_timeline_get_latest_point() */
	_MALI_UK_TIMELINE_WAIT,               /**< _mali_ukk_timeline_wait() */
	_MALI_UK_TIMELINE_CREATE_SYNC_FENCE,  /**< _mali_ukk_timeline_create_sync_fence() */
	_MALI_UK_SOFT_JOB_START,              /**< _mali_ukk_soft_job_start() */
	_MALI_UK_SOFT_JOB_SIGNAL,             /**< _mali_ukk_soft_job_signal() */
	_MALI_UK_PENDING_SUBMIT,             /**< _mali_ukk_pending_submit() */

	/** Memory functions */

	_MALI_UK_ALLOC_MEM                = 0,   /**< _mali_ukk_alloc_mem() */
	_MALI_UK_FREE_MEM,                       /**< _mali_ukk_free_mem() */
	_MALI_UK_BIND_MEM,                       /**< _mali_ukk_mem_bind() */
	_MALI_UK_UNBIND_MEM,                     /**< _mali_ukk_mem_unbind() */
	_MALI_UK_COW_MEM,                        /**< _mali_ukk_mem_cow() */
	_MALI_UK_COW_MODIFY_RANGE,               /**< _mali_ukk_mem_cow_modify_range() */
	_MALI_UK_RESIZE_MEM,                     /**<._mali_ukk_mem_resize() */
	_MALI_UK_QUERY_MMU_PAGE_TABLE_DUMP_SIZE, /**< _mali_ukk_mem_get_mmu_page_table_dump_size() */
	_MALI_UK_DUMP_MMU_PAGE_TABLE,            /**< _mali_ukk_mem_dump_mmu_page_table() */
	_MALI_UK_DMA_BUF_GET_SIZE,               /**< _mali_ukk_dma_buf_get_size() */
	_MALI_UK_MEM_WRITE_SAFE,                 /**< _mali_uku_mem_write_safe() */

	/** Common functions for each core */

	_MALI_UK_START_JOB           = 0,     /**< Start a Fragment/Vertex Processor Job on a core */
	_MALI_UK_GET_NUMBER_OF_CORES,         /**< Get the number of Fragment/Vertex Processor cores */
	_MALI_UK_GET_CORE_VERSION,            /**< Get the Fragment/Vertex Processor version compatible with all cores */

	/** Fragment Processor Functions  */

	_MALI_UK_PP_START_JOB            = _MALI_UK_START_JOB,            /**< _mali_ukk_pp_start_job() */
	_MALI_UK_GET_PP_NUMBER_OF_CORES  = _MALI_UK_GET_NUMBER_OF_CORES,  /**< _mali_ukk_get_pp_number_of_cores() */
	_MALI_UK_GET_PP_CORE_VERSION     = _MALI_UK_GET_CORE_VERSION,     /**< _mali_ukk_get_pp_core_version() */
	_MALI_UK_PP_DISABLE_WB,                                           /**< _mali_ukk_pp_job_disable_wb() */
	_MALI_UK_PP_AND_GP_START_JOB,                                     /**< _mali_ukk_pp_and_gp_start_job() */

	/** Vertex Processor Functions  */

	_MALI_UK_GP_START_JOB            = _MALI_UK_START_JOB,            /**< _mali_ukk_gp_start_job() */
	_MALI_UK_GET_GP_NUMBER_OF_CORES  = _MALI_UK_GET_NUMBER_OF_CORES,  /**< _mali_ukk_get_gp_number_of_cores() */
	_MALI_UK_GET_GP_CORE_VERSION     = _MALI_UK_GET_CORE_VERSION,     /**< _mali_ukk_get_gp_core_version() */
	_MALI_UK_GP_SUSPEND_RESPONSE,                                     /**< _mali_ukk_gp_suspend_response() */

	/** Profiling functions */

	_MALI_UK_PROFILING_ADD_EVENT     = 0, /**< __mali_uku_profiling_add_event() */
	_MALI_UK_PROFILING_REPORT_SW_COUNTERS,/**< __mali_uku_profiling_report_sw_counters() */
	_MALI_UK_PROFILING_MEMORY_USAGE_GET,  /**< __mali_uku_profiling_memory_usage_get() */
	_MALI_UK_PROFILING_STREAM_FD_GET, /** < __mali_uku_profiling_stream_fd_get() */
	_MALI_UK_PROFILING_CONTROL_SET, /** < __mali_uku_profiling_control_set() */

	/** VSYNC reporting fuctions */
	_MALI_UK_VSYNC_EVENT_REPORT      = 0, /**< _mali_ukk_vsync_event_report() */
} _mali_uk_functions;

/** @defgroup _mali_uk_getsysteminfo U/K Get System Info
 * @{ */

/**
 * Type definition for the core version number.
 * Used when returning the version number read from a core
 *
 * Its format is that of the 32-bit Version register for a particular core.
 * Refer to the "Mali200 and MaliGP2 3D Graphics Processor Technical Reference
 * Manual", ARM DDI 0415C, for more information.
 */
typedef u32 _mali_core_version;

/** @} */ /* end group _mali_uk_core */


/** @defgroup _mali_uk_gp U/K Vertex Processor
 * @{ */

/** @defgroup _mali_uk_gp_suspend_response_s Vertex Processor Suspend Response
 * @{ */

/** @brief Arguments for _mali_ukk_gp_suspend_response()
 *
 * When _mali_wait_for_notification() receives notification that a
 * Vertex Processor job was suspended, you need to send a response to indicate
 * what needs to happen with this job. You can either abort or resume the job.
 *
 * - set @c code to indicate response code. This is either @c _MALIGP_JOB_ABORT or
 * @c _MALIGP_JOB_RESUME_WITH_NEW_HEAP to indicate you will provide a new heap
 * for the job that will resolve the out of memory condition for the job.
 * - copy the @c cookie value from the @c _mali_uk_gp_job_suspended_s notification;
 * this is an identifier for the suspended job
 * - set @c arguments[0] and @c arguments[1] to zero if you abort the job. If
 * you resume it, @c argument[0] should specify the Mali start address for the new
 * heap and @c argument[1] the Mali end address of the heap.
 * - pass in the user-kernel context @c ctx that was returned from _mali_ukk_open()
 *
 */
typedef enum _maligp_job_suspended_response_code {
	_MALIGP_JOB_ABORT,                  /**< Abort the Vertex Processor job */
	_MALIGP_JOB_RESUME_WITH_NEW_HEAP    /**< Resume the Vertex Processor job with a new heap */
} _maligp_job_suspended_response_code;

typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u32 cookie;                     /**< [in] cookie from the _mali_uk_gp_job_suspended_s notification */
	_maligp_job_suspended_response_code code; /**< [in] abort or resume response code, see \ref _maligp_job_suspended_response_code */
	u32 arguments[2];               /**< [in] 0 when aborting a job. When resuming a job, the Mali start and end address for a new heap to resume the job with */
} _mali_uk_gp_suspend_response_s;

/** @} */ /* end group _mali_uk_gp_suspend_response_s */

/** @defgroup _mali_uk_gpstartjob_s Vertex Processor Start Job
 * @{ */

/** @brief Status indicating the result of the execution of a Vertex or Fragment processor job  */
typedef enum {
	_MALI_UK_JOB_STATUS_END_SUCCESS         = 1 << (16 + 0),
	_MALI_UK_JOB_STATUS_END_OOM             = 1 << (16 + 1),
	_MALI_UK_JOB_STATUS_END_ABORT           = 1 << (16 + 2),
	_MALI_UK_JOB_STATUS_END_TIMEOUT_SW      = 1 << (16 + 3),
	_MALI_UK_JOB_STATUS_END_HANG            = 1 << (16 + 4),
	_MALI_UK_JOB_STATUS_END_SEG_FAULT       = 1 << (16 + 5),
	_MALI_UK_JOB_STATUS_END_ILLEGAL_JOB     = 1 << (16 + 6),
	_MALI_UK_JOB_STATUS_END_UNKNOWN_ERR     = 1 << (16 + 7),
	_MALI_UK_JOB_STATUS_END_SHUTDOWN        = 1 << (16 + 8),
	_MALI_UK_JOB_STATUS_END_SYSTEM_UNUSABLE = 1 << (16 + 9)
} _mali_uk_job_status;

#define MALIGP2_NUM_REGS_FRAME (6)

/** @brief Arguments for _mali_ukk_gp_start_job()
 *
 * To start a Vertex Processor job
 * - associate the request with a reference to a @c mali_gp_job_info by setting
 * user_job_ptr to the address of the @c mali_gp_job_info of the job.
 * - set @c priority to the priority of the @c mali_gp_job_info
 * - specify a timeout for the job by setting @c watchdog_msecs to the number of
 * milliseconds the job is allowed to run. Specifying a value of 0 selects the
 * default timeout in use by the device driver.
 * - copy the frame registers from the @c mali_gp_job_info into @c frame_registers.
 * - set the @c perf_counter_flag, @c perf_counter_src0 and @c perf_counter_src1 to zero
 * for a non-instrumented build. For an instrumented build you can use up
 * to two performance counters. Set the corresponding bit in @c perf_counter_flag
 * to enable them. @c perf_counter_src0 and @c perf_counter_src1 specify
 * the source of what needs to get counted (e.g. number of vertex loader
 * cache hits). For source id values, see ARM DDI0415A, Table 3-60.
 * - pass in the user-kernel context @c ctx that was returned from _mali_ukk_open()
 *
 * When @c _mali_ukk_gp_start_job() returns @c _MALI_OSK_ERR_OK, status contains the
 * result of the request (see \ref _mali_uk_start_job_status). If the job could
 * not get started (@c _MALI_UK_START_JOB_NOT_STARTED_DO_REQUEUE) it should be
 * tried again.
 *
 * After the job has started, @c _mali_wait_for_notification() will be notified
 * that the job finished or got suspended. It may get suspended due to
 * resource shortage. If it finished (see _mali_ukk_wait_for_notification())
 * the notification will contain a @c _mali_uk_gp_job_finished_s result. If
 * it got suspended the notification will contain a @c _mali_uk_gp_job_suspended_s
 * result.
 *
 * The @c _mali_uk_gp_job_finished_s contains the job status (see \ref _mali_uk_job_status),
 * the number of milliseconds the job took to render, and values of core registers
 * when the job finished (irq status, performance counters, renderer list
 * address). A job has finished succesfully when its status is
 * @c _MALI_UK_JOB_STATUS_FINISHED. If the hardware detected a timeout while rendering
 * the job, or software detected the job is taking more than watchdog_msecs to
 * complete, the status will indicate @c _MALI_UK_JOB_STATUS_HANG.
 * If the hardware detected a bus error while accessing memory associated with the
 * job, status will indicate @c _MALI_UK_JOB_STATUS_SEG_FAULT.
 * status will indicate @c _MALI_UK_JOB_STATUS_NOT_STARTED if the driver had to
 * stop the job but the job didn't start on the hardware yet, e.g. when the
 * driver shutdown.
 *
 * In case the job got suspended, @c _mali_uk_gp_job_suspended_s contains
 * the @c user_job_ptr identifier used to start the job with, the @c reason
 * why the job stalled (see \ref _maligp_job_suspended_reason) and a @c cookie
 * to identify the core on which the job stalled.  This @c cookie will be needed
 * when responding to this nofication by means of _mali_ukk_gp_suspend_response().
 * (see _mali_ukk_gp_suspend_response()). The response is either to abort or
 * resume the job. If the job got suspended due to an out of memory condition
 * you may be able to resolve this by providing more memory and resuming the job.
 *
 */
typedef struct {
	u64 ctx;                          /**< [in,out] user-kernel context (trashed on output) */
	u64 user_job_ptr;                   /**< [in] identifier for the job in user space, a @c mali_gp_job_info* */
	u32 priority;                       /**< [in] job priority. A lower number means higher priority */
	u32 frame_registers[MALIGP2_NUM_REGS_FRAME]; /**< [in] core specific registers associated with this job */
	u32 heap_grow_size;     /** <[in] the grow size of the plbu heap when out of memory */
	u32 perf_counter_flag;              /**< [in] bitmask indicating which performance counters to enable, see \ref _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE and related macro definitions */
	u32 perf_counter_src0;              /**< [in] source id for performance counter 0 (see ARM DDI0415A, Table 3-60) */
	u32 perf_counter_src1;              /**< [in] source id for performance counter 1 (see ARM DDI0415A, Table 3-60) */
	u32 frame_builder_id;               /**< [in] id of the originating frame builder */
	u32 flush_id;                       /**< [in] flush id within the originating frame builder */
	_mali_uk_fence_t fence;             /**< [in] fence this job must wait on */
	u64 timeline_point_ptr;            /**< [in,out] pointer to u32: location where point on gp timeline for this job will be written */
	u32 varying_memsize;            /** < [in] size of varying memory to use deffer bind*/
	u32 varying_alloc_num;
	u64 varying_alloc_list;         /** < [in] memory hanlde list of varying buffer to use deffer bind */
} _mali_uk_gp_start_job_s;

#define _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE (1<<0) /**< Enable performance counter SRC0 for a job */
#define _MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE (1<<1) /**< Enable performance counter SRC1 for a job */
#define _MALI_PERFORMANCE_COUNTER_FLAG_HEATMAP_ENABLE (1<<2) /**< Enable per tile (aka heatmap) generation with for a job (using the enabled counter sources) */

/** @} */ /* end group _mali_uk_gpstartjob_s */

typedef struct {
	u64 user_job_ptr;               /**< [out] identifier for the job in user space */
	_mali_uk_job_status status;     /**< [out] status of finished job */
	u32 heap_current_addr;          /**< [out] value of the GP PLB PL heap start address register */
	u32 perf_counter0;              /**< [out] value of performance counter 0 (see ARM DDI0415A) */
	u32 perf_counter1;              /**< [out] value of performance counter 1 (see ARM DDI0415A) */
	u32 pending_big_job_num;
} _mali_uk_gp_job_finished_s;

typedef struct {
	u64 user_job_ptr;                    /**< [out] identifier for the job in user space */
	u32 cookie;                          /**< [out] identifier for the core in kernel space on which the job stalled */
	u32 heap_added_size;
} _mali_uk_gp_job_suspended_s;

/** @} */ /* end group _mali_uk_gp */


/** @defgroup _mali_uk_pp U/K Fragment Processor
 * @{ */

#define _MALI_PP_MAX_SUB_JOBS 8

#define _MALI_PP_MAX_FRAME_REGISTERS ((0x058/4)+1)

#define _MALI_PP_MAX_WB_REGISTERS ((0x02C/4)+1)

#define _MALI_DLBU_MAX_REGISTERS 4

/** Flag for _mali_uk_pp_start_job_s */
#define _MALI_PP_JOB_FLAG_NO_NOTIFICATION (1<<0)
#define _MALI_PP_JOB_FLAG_IS_WINDOW_SURFACE (1<<1)

/** @defgroup _mali_uk_ppstartjob_s Fragment Processor Start Job
 * @{ */

/** @brief Arguments for _mali_ukk_pp_start_job()
 *
 * To start a Fragment Processor job
 * - associate the request with a reference to a mali_pp_job by setting
 * @c user_job_ptr to the address of the @c mali_pp_job of the job.
 * - set @c priority to the priority of the mali_pp_job
 * - specify a timeout for the job by setting @c watchdog_msecs to the number of
 * milliseconds the job is allowed to run. Specifying a value of 0 selects the
 * default timeout in use by the device driver.
 * - copy the frame registers from the @c mali_pp_job into @c frame_registers.
 * For MALI200 you also need to copy the write back 0,1 and 2 registers.
 * - set the @c perf_counter_flag, @c perf_counter_src0 and @c perf_counter_src1 to zero
 * for a non-instrumented build. For an instrumented build you can use up
 * to two performance counters. Set the corresponding bit in @c perf_counter_flag
 * to enable them. @c perf_counter_src0 and @c perf_counter_src1 specify
 * the source of what needs to get counted (e.g. number of vertex loader
 * cache hits). For source id values, see ARM DDI0415A, Table 3-60.
 * - pass in the user-kernel context in @c ctx that was returned from _mali_ukk_open()
 *
 * When _mali_ukk_pp_start_job() returns @c _MALI_OSK_ERR_OK, @c status contains the
 * result of the request (see \ref _mali_uk_start_job_status). If the job could
 * not get started (@c _MALI_UK_START_JOB_NOT_STARTED_DO_REQUEUE) it should be
 * tried again.
 *
 * After the job has started, _mali_wait_for_notification() will be notified
 * when the job finished. The notification will contain a
 * @c _mali_uk_pp_job_finished_s result. It contains the @c user_job_ptr
 * identifier used to start the job with, the job @c status (see \ref _mali_uk_job_status),
 * the number of milliseconds the job took to render, and values of core registers
 * when the job finished (irq status, performance counters, renderer list
 * address). A job has finished succesfully when its status is
 * @c _MALI_UK_JOB_STATUS_FINISHED. If the hardware detected a timeout while rendering
 * the job, or software detected the job is taking more than @c watchdog_msecs to
 * complete, the status will indicate @c _MALI_UK_JOB_STATUS_HANG.
 * If the hardware detected a bus error while accessing memory associated with the
 * job, status will indicate @c _MALI_UK_JOB_STATUS_SEG_FAULT.
 * status will indicate @c _MALI_UK_JOB_STATUS_NOT_STARTED if the driver had to
 * stop the job but the job didn't start on the hardware yet, e.g. when the
 * driver shutdown.
 *
 */
typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u64 user_job_ptr;               /**< [in] identifier for the job in user space */
	u32 priority;                   /**< [in] job priority. A lower number means higher priority */
	u32 frame_registers[_MALI_PP_MAX_FRAME_REGISTERS];         /**< [in] core specific registers associated with first sub job, see ARM DDI0415A */
	u32 frame_registers_addr_frame[_MALI_PP_MAX_SUB_JOBS - 1]; /**< [in] ADDR_FRAME registers for sub job 1-7 */
	u32 frame_registers_addr_stack[_MALI_PP_MAX_SUB_JOBS - 1]; /**< [in] ADDR_STACK registers for sub job 1-7 */
	u32 wb0_registers[_MALI_PP_MAX_WB_REGISTERS];
	u32 wb1_registers[_MALI_PP_MAX_WB_REGISTERS];
	u32 wb2_registers[_MALI_PP_MAX_WB_REGISTERS];
	u32 dlbu_registers[_MALI_DLBU_MAX_REGISTERS]; /**< [in] Dynamic load balancing unit registers */
	u32 num_cores;                      /**< [in] Number of cores to set up (valid range: 1-8(M450) or 4(M400)) */
	u32 perf_counter_flag;              /**< [in] bitmask indicating which performance counters to enable, see \ref _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE and related macro definitions */
	u32 perf_counter_src0;              /**< [in] source id for performance counter 0 (see ARM DDI0415A, Table 3-60) */
	u32 perf_counter_src1;              /**< [in] source id for performance counter 1 (see ARM DDI0415A, Table 3-60) */
	u32 frame_builder_id;               /**< [in] id of the originating frame builder */
	u32 flush_id;                       /**< [in] flush id within the originating frame builder */
	u32 flags;                          /**< [in] See _MALI_PP_JOB_FLAG_* for a list of avaiable flags */
	u32 tilesx;                         /**< [in] number of tiles in the x direction (needed for heatmap generation */
	u32 tilesy;                         /**< [in] number of tiles in y direction (needed for reading the heatmap memory) */
	u32 heatmap_mem;                    /**< [in] memory address to store counter values per tile (aka heatmap) */
	u32 num_memory_cookies;             /**< [in] number of memory cookies attached to job */
	u64 memory_cookies;               /**< [in] pointer to array of u32 memory cookies attached to job */
	_mali_uk_fence_t fence;             /**< [in] fence this job must wait on */
	u64 timeline_point_ptr;           /**< [in,out] pointer to location of u32 where point on pp timeline for this job will be written */
} _mali_uk_pp_start_job_s;

typedef struct {
	u64 ctx;       /**< [in,out] user-kernel context (trashed on output) */
	u64 gp_args;   /**< [in,out] GP uk arguments (see _mali_uk_gp_start_job_s) */
	u64 pp_args;   /**< [in,out] PP uk arguments (see _mali_uk_pp_start_job_s) */
} _mali_uk_pp_and_gp_start_job_s;

/** @} */ /* end group _mali_uk_ppstartjob_s */

typedef struct {
	u64 user_job_ptr;                          /**< [out] identifier for the job in user space */
	_mali_uk_job_status status;                /**< [out] status of finished job */
	u32 perf_counter0[_MALI_PP_MAX_SUB_JOBS];  /**< [out] value of perfomance counter 0 (see ARM DDI0415A), one for each sub job */
	u32 perf_counter1[_MALI_PP_MAX_SUB_JOBS];  /**< [out] value of perfomance counter 1 (see ARM DDI0415A), one for each sub job */
	u32 perf_counter_src0;
	u32 perf_counter_src1;
} _mali_uk_pp_job_finished_s;

typedef struct {
	u32 number_of_enabled_cores;               /**< [out] the new number of enabled cores */
} _mali_uk_pp_num_cores_changed_s;



/**
 * Flags to indicate write-back units
 */
typedef enum {
	_MALI_UK_PP_JOB_WB0 = 1,
	_MALI_UK_PP_JOB_WB1 = 2,
	_MALI_UK_PP_JOB_WB2 = 4,
} _mali_uk_pp_job_wbx_flag;

typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u32 fb_id;                      /**< [in] Frame builder ID of job to disable WB units for */
	u32 wb0_memory;
	u32 wb1_memory;
	u32 wb2_memory;
} _mali_uk_pp_disable_wb_s;


/** @} */ /* end group _mali_uk_pp */

/** @defgroup _mali_uk_soft_job U/K Soft Job
 * @{ */

typedef struct {
	u64 ctx;                            /**< [in,out] user-kernel context (trashed on output) */
	u64 user_job;                       /**< [in] identifier for the job in user space */
	u64 job_id_ptr;                     /**< [in,out] pointer to location of u32 where job id will be written */
	_mali_uk_fence_t fence;             /**< [in] fence this job must wait on */
	u32 point;                          /**< [out] point on soft timeline for this job */
	u32 type;                           /**< [in] type of soft job */
} _mali_uk_soft_job_start_s;

typedef struct {
	u64 user_job;                       /**< [out] identifier for the job in user space */
} _mali_uk_soft_job_activated_s;

typedef struct {
	u64 ctx;                          /**< [in,out] user-kernel context (trashed on output) */
	u32 job_id;                         /**< [in] id for soft job */
} _mali_uk_soft_job_signal_s;

/** @} */ /* end group _mali_uk_soft_job */

typedef struct {
	u32 counter_id;
	u32 key;
	int enable;
} _mali_uk_annotate_profiling_mem_counter_s;

typedef struct {
	u32 sampling_rate;
	int enable;
} _mali_uk_annotate_profiling_enable_s;


/** @addtogroup _mali_uk_core U/K Core
 * @{ */

/** @defgroup _mali_uk_waitfornotification_s Wait For Notification
 * @{ */

/** @brief Notification type encodings
 *
 * Each Notification type is an ordered pair of (subsystem,id), and is unique.
 *
 * The encoding of subsystem,id into a 32-bit word is:
 * encoding = (( subsystem << _MALI_NOTIFICATION_SUBSYSTEM_SHIFT ) & _MALI_NOTIFICATION_SUBSYSTEM_MASK)
 *            | (( id <<  _MALI_NOTIFICATION_ID_SHIFT ) & _MALI_NOTIFICATION_ID_MASK)
 *
 * @see _mali_uk_wait_for_notification_s
 */
typedef enum {
	/** core notifications */

	_MALI_NOTIFICATION_CORE_SHUTDOWN_IN_PROGRESS = (_MALI_UK_CORE_SUBSYSTEM << 16) | 0x20,
	_MALI_NOTIFICATION_APPLICATION_QUIT = (_MALI_UK_CORE_SUBSYSTEM << 16) | 0x40,
	_MALI_NOTIFICATION_SETTINGS_CHANGED = (_MALI_UK_CORE_SUBSYSTEM << 16) | 0x80,
	_MALI_NOTIFICATION_SOFT_ACTIVATED = (_MALI_UK_CORE_SUBSYSTEM << 16) | 0x100,

	/** Fragment Processor notifications */

	_MALI_NOTIFICATION_PP_FINISHED = (_MALI_UK_PP_SUBSYSTEM << 16) | 0x10,
	_MALI_NOTIFICATION_PP_NUM_CORE_CHANGE = (_MALI_UK_PP_SUBSYSTEM << 16) | 0x20,

	/** Vertex Processor notifications */

	_MALI_NOTIFICATION_GP_FINISHED = (_MALI_UK_GP_SUBSYSTEM << 16) | 0x10,
	_MALI_NOTIFICATION_GP_STALLED = (_MALI_UK_GP_SUBSYSTEM << 16) | 0x20,

	/** Profiling notifications */
	_MALI_NOTIFICATION_ANNOTATE_PROFILING_MEM_COUNTER = (_MALI_UK_PROFILING_SUBSYSTEM << 16) | 0x10,
	_MALI_NOTIFICATION_ANNOTATE_PROFILING_ENABLE = (_MALI_UK_PROFILING_SUBSYSTEM << 16) | 0x20,
} _mali_uk_notification_type;

/** to assist in splitting up 32-bit notification value in subsystem and id value */
#define _MALI_NOTIFICATION_SUBSYSTEM_MASK 0xFFFF0000
#define _MALI_NOTIFICATION_SUBSYSTEM_SHIFT 16
#define _MALI_NOTIFICATION_ID_MASK 0x0000FFFF
#define _MALI_NOTIFICATION_ID_SHIFT 0


/** @brief Enumeration of possible settings which match mali_setting_t in user space
 *
 *
 */
typedef enum {
	_MALI_UK_USER_SETTING_SW_EVENTS_ENABLE = 0,
	_MALI_UK_USER_SETTING_COLORBUFFER_CAPTURE_ENABLED,
	_MALI_UK_USER_SETTING_DEPTHBUFFER_CAPTURE_ENABLED,
	_MALI_UK_USER_SETTING_STENCILBUFFER_CAPTURE_ENABLED,
	_MALI_UK_USER_SETTING_PER_TILE_COUNTERS_CAPTURE_ENABLED,
	_MALI_UK_USER_SETTING_BUFFER_CAPTURE_COMPOSITOR,
	_MALI_UK_USER_SETTING_BUFFER_CAPTURE_WINDOW,
	_MALI_UK_USER_SETTING_BUFFER_CAPTURE_OTHER,
	_MALI_UK_USER_SETTING_BUFFER_CAPTURE_N_FRAMES,
	_MALI_UK_USER_SETTING_BUFFER_CAPTURE_RESIZE_FACTOR,
	_MALI_UK_USER_SETTING_SW_COUNTER_ENABLED,
	_MALI_UK_USER_SETTING_MAX,
} _mali_uk_user_setting_t;

/* See mali_user_settings_db.c */
extern const char *_mali_uk_user_setting_descriptions[];
#define _MALI_UK_USER_SETTING_DESCRIPTIONS \
	{                                           \
		"sw_events_enable",                 \
		"colorbuffer_capture_enable",       \
		"depthbuffer_capture_enable",       \
		"stencilbuffer_capture_enable",     \
		"per_tile_counters_enable",         \
		"buffer_capture_compositor",        \
		"buffer_capture_window",            \
		"buffer_capture_other",             \
		"buffer_capture_n_frames",          \
		"buffer_capture_resize_factor",     \
		"sw_counters_enable",               \
	};

/** @brief struct to hold the value to a particular setting as seen in the kernel space
 */
typedef struct {
	_mali_uk_user_setting_t setting;
	u32 value;
} _mali_uk_settings_changed_s;

/** @brief Arguments for _mali_ukk_wait_for_notification()
 *
 * On successful return from _mali_ukk_wait_for_notification(), the members of
 * this structure will indicate the reason for notification.
 *
 * Specifically, the source of the notification can be identified by the
 * subsystem and id fields of the mali_uk_notification_type in the code.type
 * member. The type member is encoded in a way to divide up the types into a
 * subsystem field, and a per-subsystem ID field. See
 * _mali_uk_notification_type for more information.
 *
 * Interpreting the data union member depends on the notification type:
 *
 * - type == _MALI_NOTIFICATION_CORE_SHUTDOWN_IN_PROGRESS
 *     - The kernel side is shutting down. No further
 * _mali_uk_wait_for_notification() calls should be made.
 *     - In this case, the value of the data union member is undefined.
 *     - This is used to indicate to the user space client that it should close
 * the connection to the Mali Device Driver.
 * - type == _MALI_NOTIFICATION_PP_FINISHED
 *    - The notification data is of type _mali_uk_pp_job_finished_s. It contains the user_job_ptr
 * identifier used to start the job with, the job status, the number of milliseconds the job took to render,
 * and values of core registers when the job finished (irq status, performance counters, renderer list
 * address).
 *    - A job has finished succesfully when its status member is _MALI_UK_JOB_STATUS_FINISHED.
 *    - If the hardware detected a timeout while rendering the job, or software detected the job is
 * taking more than watchdog_msecs (see _mali_ukk_pp_start_job()) to complete, the status member will
 * indicate _MALI_UK_JOB_STATUS_HANG.
 *    - If the hardware detected a bus error while accessing memory associated with the job, status will
 * indicate _MALI_UK_JOB_STATUS_SEG_FAULT.
 *    - Status will indicate MALI_UK_JOB_STATUS_NOT_STARTED if the driver had to stop the job but the job
 * didn't start the hardware yet, e.g. when the driver closes.
 * - type == _MALI_NOTIFICATION_GP_FINISHED
 *     - The notification data is of type _mali_uk_gp_job_finished_s. The notification is similar to that of
 * type == _MALI_NOTIFICATION_PP_FINISHED, except that several other GP core register values are returned.
 * The status values have the same meaning for type == _MALI_NOTIFICATION_PP_FINISHED.
 * - type == _MALI_NOTIFICATION_GP_STALLED
 *     - The nofication data is of type _mali_uk_gp_job_suspended_s. It contains the user_job_ptr
 * identifier used to start the job with, the reason why the job stalled and a cookie to identify the core on
 * which the job stalled.
 *     - The reason member of gp_job_suspended is set to _MALIGP_JOB_SUSPENDED_OUT_OF_MEMORY
 * when the polygon list builder unit has run out of memory.
 */
typedef struct {
	u64 ctx;                       /**< [in,out] user-kernel context (trashed on output) */
	_mali_uk_notification_type type; /**< [out] Type of notification available */
	union {
		_mali_uk_gp_job_suspended_s gp_job_suspended;/**< [out] Notification data for _MALI_NOTIFICATION_GP_STALLED notification type */
		_mali_uk_gp_job_finished_s  gp_job_finished; /**< [out] Notification data for _MALI_NOTIFICATION_GP_FINISHED notification type */
		_mali_uk_pp_job_finished_s  pp_job_finished; /**< [out] Notification data for _MALI_NOTIFICATION_PP_FINISHED notification type */
		_mali_uk_settings_changed_s setting_changed;/**< [out] Notification data for _MALI_NOTIFICAATION_SETTINGS_CHANGED notification type */
		_mali_uk_soft_job_activated_s soft_job_activated; /**< [out] Notification data for _MALI_NOTIFICATION_SOFT_ACTIVATED notification type */
		_mali_uk_annotate_profiling_mem_counter_s profiling_mem_counter;
		_mali_uk_annotate_profiling_enable_s profiling_enable;
	} data;
} _mali_uk_wait_for_notification_s;

/** @brief Arguments for _mali_ukk_post_notification()
 *
 * Posts the specified notification to the notification queue for this application.
 * This is used to send a quit message to the callback thread.
 */
typedef struct {
	u64 ctx;                       /**< [in,out] user-kernel context (trashed on output) */
	_mali_uk_notification_type type; /**< [in] Type of notification to post */
} _mali_uk_post_notification_s;

/** @} */ /* end group _mali_uk_waitfornotification_s */

/** @defgroup _mali_uk_getapiversion_s Get API Version
 * @{ */

/** helpers for Device Driver API version handling */

/** @brief Encode a version ID from a 16-bit input
 *
 * @note the input is assumed to be 16 bits. It must not exceed 16 bits. */
#define _MAKE_VERSION_ID(x) (((x) << 16UL) | (x))

/** @brief Check whether a 32-bit value is likely to be Device Driver API
 * version ID. */
#define _IS_VERSION_ID(x) (((x) & 0xFFFF) == (((x) >> 16UL) & 0xFFFF))

/** @brief Decode a 16-bit version number from a 32-bit Device Driver API version
 * ID */
#define _GET_VERSION(x) (((x) >> 16UL) & 0xFFFF)

/** @brief Determine whether two 32-bit encoded version IDs match */
#define _IS_API_MATCH(x, y) (IS_VERSION_ID((x)) && IS_VERSION_ID((y)) && (GET_VERSION((x)) == GET_VERSION((y))))

/**
 * API version define.
 * Indicates the version of the kernel API
 * The version is a 16bit integer incremented on each API change.
 * The 16bit integer is stored twice in a 32bit integer
 * For example, for version 1 the value would be 0x00010001
 */
#define _MALI_API_VERSION 800
#define _MALI_UK_API_VERSION _MAKE_VERSION_ID(_MALI_API_VERSION)

/**
 * The API version is a 16-bit integer stored in both the lower and upper 16-bits
 * of a 32-bit value. The 16-bit API version value is incremented on each API
 * change. Version 1 would be 0x00010001. Used in _mali_uk_get_api_version_s.
 */
typedef u32 _mali_uk_api_version;

/** @brief Arguments for _mali_uk_get_api_version()
 *
 * The user-side interface version must be written into the version member,
 * encoded using _MAKE_VERSION_ID(). It will be compared to the API version of
 * the kernel-side interface.
 *
 * On successful return, the version member will be the API version of the
 * kernel-side interface. _MALI_UK_API_VERSION macro defines the current version
 * of the API.
 *
 * The compatible member must be checked to see if the version of the user-side
 * interface is compatible with the kernel-side interface, since future versions
 * of the interface may be backwards compatible.
 */
typedef struct {
	u32 ctx;                        /**< [in,out] user-kernel context (trashed on output) */
	_mali_uk_api_version version;   /**< [in,out] API version of user-side interface. */
	int compatible;                 /**< [out] @c 1 when @version is compatible, @c 0 otherwise */
} _mali_uk_get_api_version_s;

/** @brief Arguments for _mali_uk_get_api_version_v2()
 *
 * The user-side interface version must be written into the version member,
 * encoded using _MAKE_VERSION_ID(). It will be compared to the API version of
 * the kernel-side interface.
 *
 * On successful return, the version member will be the API version of the
 * kernel-side interface. _MALI_UK_API_VERSION macro defines the current version
 * of the API.
 *
 * The compatible member must be checked to see if the version of the user-side
 * interface is compatible with the kernel-side interface, since future versions
 * of the interface may be backwards compatible.
 */
typedef struct {
	u64 ctx;                        /**< [in,out] user-kernel context (trashed on output) */
	_mali_uk_api_version version;   /**< [in,out] API version of user-side interface. */
	int compatible;                 /**< [out] @c 1 when @version is compatible, @c 0 otherwise */
} _mali_uk_get_api_version_v2_s;

/** @} */ /* end group _mali_uk_getapiversion_s */

/** @defgroup _mali_uk_get_user_settings_s Get user space settings */

/** @brief struct to keep the matching values of the user space settings within certain context
 *
 * Each member of the settings array corresponds to a matching setting in the user space and its value is the value
 * of that particular setting.
 *
 * All settings are given reference to the context pointed to by the ctx pointer.
 *
 */
typedef struct {
	u64 ctx;                       /**< [in,out] user-kernel context (trashed on output) */
	u32 settings[_MALI_UK_USER_SETTING_MAX]; /**< [out] The values for all settings */
} _mali_uk_get_user_settings_s;

/** @brief struct to hold the value of a particular setting from the user space within a given context
 */
typedef struct {
	u64 ctx;                       /**< [in,out] user-kernel context (trashed on output) */
	_mali_uk_user_setting_t setting; /**< [in] setting to get */
	u32 value;                       /**< [out] value of setting */
} _mali_uk_get_user_setting_s;

/** @brief Arguments for _mali_ukk_request_high_priority() */
typedef struct {
	u64 ctx;                       /**< [in,out] user-kernel context (trashed on output) */
} _mali_uk_request_high_priority_s;

/** @brief Arguments for _mali_ukk_pending_submit() */
typedef struct {
	u64 ctx;                       /**< [in,out] user-kernel context (trashed on output) */
} _mali_uk_pending_submit_s;

/** @} */ /* end group _mali_uk_core */


/** @defgroup _mali_uk_memory U/K Memory
 * @{ */

#define _MALI_MEMORY_ALLOCATE_RESIZEABLE  (1<<4) /* BUFFER can trim dow/grow*/
#define _MALI_MEMORY_ALLOCATE_NO_BIND_GPU (1<<5) /*Not map to GPU when allocate, must call bind later*/
#define _MALI_MEMORY_ALLOCATE_SWAPPABLE   (1<<6) /* Allocate swappale memory. */
#define _MALI_MEMORY_ALLOCATE_DEFER_BIND (1<<7) /*Not map to GPU when allocate, must call bind later*/


typedef struct {
	u64 ctx;                                          /**< [in,out] user-kernel context (trashed on output) */
	u32 gpu_vaddr;                                    /**< [in] GPU virtual address */
	u32 vsize;                                        /**< [in] vitrual size of the allocation */
	u32 psize;                                        /**< [in] physical size of the allocation */
	u32 flags;
	u64 backend_handle;                               /**< [out] backend handle */
	struct {
		/* buffer types*/
		/* CPU read/write info*/
	} buffer_info;
} _mali_uk_alloc_mem_s;


typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u32 gpu_vaddr;                /**< [in] use as handle to free allocation */
	u32 free_pages_nr;      /** < [out] record the number of free pages */
} _mali_uk_free_mem_s;


#define _MALI_MEMORY_BIND_BACKEND_UMP             (1<<8)
#define _MALI_MEMORY_BIND_BACKEND_DMA_BUF         (1<<9)
#define _MALI_MEMORY_BIND_BACKEND_MALI_MEMORY     (1<<10)
#define _MALI_MEMORY_BIND_BACKEND_EXTERNAL_MEMORY (1<<11)
#define _MALI_MEMORY_BIND_BACKEND_EXT_COW         (1<<12)
#define _MALI_MEMORY_BIND_BACKEND_HAVE_ALLOCATION (1<<13)


#define _MALI_MEMORY_BIND_BACKEND_MASK (_MALI_MEMORY_BIND_BACKEND_UMP| \
					_MALI_MEMORY_BIND_BACKEND_DMA_BUF |\
					_MALI_MEMORY_BIND_BACKEND_MALI_MEMORY |\
					_MALI_MEMORY_BIND_BACKEND_EXTERNAL_MEMORY |\
					_MALI_MEMORY_BIND_BACKEND_EXT_COW |\
					_MALI_MEMORY_BIND_BACKEND_HAVE_ALLOCATION)


#define _MALI_MEMORY_GPU_READ_ALLOCATE            (1<<16)


typedef struct {
	u64 ctx;                                        /**< [in,out] user-kernel context (trashed on output) */
	u32 vaddr;                                      /**< [in] mali address to map the physical memory to */
	u32 size;                                       /**< [in] size */
	u32 flags;                                      /**< [in] see_MALI_MEMORY_BIND_BACKEND_* */
	u32 padding;                                    /** padding for 32/64 struct alignment */
	union {
		struct {
			u32 secure_id;                  /**< [in] secure id */
			u32 rights;                     /**< [in] rights necessary for accessing memory */
			u32 flags;                      /**< [in] flags, see \ref _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE */
		} bind_ump;
		struct {
			u32 mem_fd;                     /**< [in] Memory descriptor */
			u32 rights;                     /**< [in] rights necessary for accessing memory */
			u32 flags;                      /**< [in] flags, see \ref _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE */
		} bind_dma_buf;
		struct {
			/**/
		} bind_mali_memory;
		struct {
			u32 phys_addr;                  /**< [in] physical address */
			u32 rights;                     /**< [in] rights necessary for accessing memory */
			u32 flags;                      /**< [in] flags, see \ref _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE */
		} bind_ext_memory;
	} mem_union;
} _mali_uk_bind_mem_s;

typedef struct {
	u64 ctx;                                        /**< [in,out] user-kernel context (trashed on output) */
	u32 flags;                                      /**< [in] see_MALI_MEMORY_BIND_BACKEND_* */
	u32 vaddr;                                      /**<  [in] identifier for mapped memory object in kernel space  */
} _mali_uk_unbind_mem_s;

typedef struct {
	u64 ctx;                                        /**< [in,out] user-kernel context (trashed on output) */
	u32 target_handle;                              /**< [in] handle of allocation need to do COW */
	u32 target_offset;                              /**< [in] offset in target allocation to do COW(for support COW  a memory allocated from memory_bank, PAGE_SIZE align)*/
	u32 target_size;                                /**< [in] size of target allocation to do COW (for support memory bank, PAGE_SIZE align)(in byte) */
	u32 range_start;                                /**< [in] re allocate range start offset, offset from the start of allocation (PAGE_SIZE align)*/
	u32 range_size;                                 /**< [in] re allocate size (PAGE_SIZE align)*/
	u32 vaddr;                                      /**< [in] mali address for the new allocaiton */
	u32 backend_handle;                             /**< [out] backend handle */
	u32 flags;
} _mali_uk_cow_mem_s;

typedef struct {
	u64 ctx;                                        /**< [in,out] user-kernel context (trashed on output) */
	u32 range_start;                                /**< [in] re allocate range start offset, offset from the start of allocation */
	u32 size;                                       /**< [in] re allocate size*/
	u32 vaddr;                                      /**< [in] mali address for the new allocaiton */
	s32 change_pages_nr;                            /**< [out] record the page number change for cow operation */
} _mali_uk_cow_modify_range_s;


typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u32 mem_fd;                     /**< [in] Memory descriptor */
	u32 size;                       /**< [out] size */
} _mali_uk_dma_buf_get_size_s;

/** Flag for _mali_uk_map_external_mem_s, _mali_uk_attach_ump_mem_s and _mali_uk_attach_dma_buf_s */
#define _MALI_MAP_EXTERNAL_MAP_GUARD_PAGE (1<<0)


typedef struct {
	u64 ctx;                                /**< [in,out] user-kernel context (trashed on output) */
	u64 vaddr;                              /* the buffer to do resize*/
	u32 psize;                              /* wanted physical size of this memory */
} _mali_uk_mem_resize_s;

/**
 * @brief Arguments for _mali_uk[uk]_mem_write_safe()
 */
typedef struct {
	u64 ctx;  /**< [in,out] user-kernel context (trashed on output) */
	u64 src;  /**< [in] Pointer to source data */
	u64 dest; /**< [in] Destination Mali buffer */
	u32 size;   /**< [in,out] Number of bytes to write/copy on input, number of bytes actually written/copied on output */
} _mali_uk_mem_write_safe_s;

typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u32 size;                       /**< [out] size of MMU page table information (registers + page tables) */
} _mali_uk_query_mmu_page_table_dump_size_s;

typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u32 size;                       /**< [in] size of buffer to receive mmu page table information */
	u64 buffer;                   /**< [in,out] buffer to receive mmu page table information */
	u32 register_writes_size;       /**< [out] size of MMU register dump */
	u64 register_writes;           /**< [out] pointer within buffer where MMU register dump is stored */
	u32 page_table_dump_size;       /**< [out] size of MMU page table dump */
	u64 page_table_dump;           /**< [out] pointer within buffer where MMU page table dump is stored */
} _mali_uk_dump_mmu_page_table_s;

/** @} */ /* end group _mali_uk_memory */


/** @addtogroup _mali_uk_pp U/K Fragment Processor
 * @{ */

/** @brief Arguments for _mali_ukk_get_pp_number_of_cores()
 *
 * - pass in the user-kernel context @c ctx that was returned from _mali_ukk_open()
 * - Upon successful return from _mali_ukk_get_pp_number_of_cores(), @c number_of_cores
 * will contain the number of Fragment Processor cores in the system.
 */
typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u32 number_of_total_cores;      /**< [out] Total number of Fragment Processor cores in the system */
	u32 number_of_enabled_cores;    /**< [out] Number of enabled Fragment Processor cores */
} _mali_uk_get_pp_number_of_cores_s;

/** @brief Arguments for _mali_ukk_get_pp_core_version()
 *
 * - pass in the user-kernel context @c ctx that was returned from _mali_ukk_open()
 * - Upon successful return from _mali_ukk_get_pp_core_version(), @c version contains
 * the version that all Fragment Processor cores are compatible with.
 */
typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	_mali_core_version version;     /**< [out] version returned from core, see \ref _mali_core_version  */
	u32 padding;
} _mali_uk_get_pp_core_version_s;

/** @} */ /* end group _mali_uk_pp */


/** @addtogroup _mali_uk_gp U/K Vertex Processor
 * @{ */

/** @brief Arguments for _mali_ukk_get_gp_number_of_cores()
 *
 * - pass in the user-kernel context @c ctx that was returned from _mali_ukk_open()
 * - Upon successful return from _mali_ukk_get_gp_number_of_cores(), @c number_of_cores
 * will contain the number of Vertex Processor cores in the system.
 */
typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u32 number_of_cores;            /**< [out] number of Vertex Processor cores in the system */
} _mali_uk_get_gp_number_of_cores_s;

/** @brief Arguments for _mali_ukk_get_gp_core_version()
 *
 * - pass in the user-kernel context @c ctx that was returned from _mali_ukk_open()
 * - Upon successful return from _mali_ukk_get_gp_core_version(), @c version contains
 * the version that all Vertex Processor cores are compatible with.
 */
typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	_mali_core_version version;     /**< [out] version returned from core, see \ref _mali_core_version */
} _mali_uk_get_gp_core_version_s;

/** @} */ /* end group _mali_uk_gp */

typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u32 event_id;                   /**< [in] event id to register (see  enum mali_profiling_events for values) */
	u32 data[5];                    /**< [in] event specific data */
} _mali_uk_profiling_add_event_s;

typedef struct {
	u64 ctx;                     /**< [in,out] user-kernel context (trashed on output) */
	u32 memory_usage;              /**< [out] total memory usage */
	u32 vaddr;                                      /**< [in] mali address for the cow allocaiton */
	s32 change_pages_nr;            /**< [out] record the page number change for cow operation */
} _mali_uk_profiling_memory_usage_get_s;


/** @addtogroup _mali_uk_memory U/K Memory
 * @{ */

/** @brief Arguments to _mali_ukk_mem_mmap()
 *
 * Use of the phys_addr member depends on whether the driver is compiled for
 * Mali-MMU or nonMMU:
 * - in the nonMMU case, this is the physical address of the memory as seen by
 * the CPU (which may be a constant offset from that used by Mali)
 * - in the MMU case, this is the Mali Virtual base address of the memory to
 * allocate, and the particular physical pages used to back the memory are
 * entirely determined by _mali_ukk_mem_mmap(). The details of the physical pages
 * are not reported to user-space for security reasons.
 *
 * The cookie member must be stored for use later when freeing the memory by
 * calling _mali_ukk_mem_munmap(). In the Mali-MMU case, the cookie is secure.
 *
 * The ukk_private word must be set to zero when calling from user-space. On
 * Kernel-side, the  OS implementation of the U/K interface can use it to
 * communicate data to the OS implementation of the OSK layer. In particular,
 * _mali_ukk_get_big_block() directly calls _mali_ukk_mem_mmap directly, and
 * will communicate its own ukk_private word through the ukk_private member
 * here. The common code itself will not inspect or modify the ukk_private
 * word, and so it may be safely used for whatever purposes necessary to
 * integrate Mali Memory handling into the OS.
 *
 * The uku_private member is currently reserved for use by the user-side
 * implementation of the U/K interface. Its value must be zero.
 */
typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	void *mapping;                  /**< [out] Returns user-space virtual address for the mapping */
	u32 size;                       /**< [in] Size of the requested mapping */
	u32 phys_addr;                  /**< [in] Physical address - could be offset, depending on caller+callee convention */
	mali_bool writeable;
} _mali_uk_mem_mmap_s;

/** @brief Arguments to _mali_ukk_mem_munmap()
 *
 * The cookie and mapping members must be that returned from the same previous
 * call to _mali_ukk_mem_mmap(). The size member must correspond to cookie
 * and mapping - that is, it must be the value originally supplied to a call to
 * _mali_ukk_mem_mmap that returned the values of mapping and cookie.
 *
 * An error will be returned if an attempt is made to unmap only part of the
 * originally obtained range, or to unmap more than was originally obtained.
 */
typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	void *mapping;                  /**< [in] The mapping returned from mmap call */
	u32 size;                       /**< [in] The size passed to mmap call */
} _mali_uk_mem_munmap_s;
/** @} */ /* end group _mali_uk_memory */

/** @defgroup _mali_uk_vsync U/K VSYNC Wait Reporting Module
 * @{ */

/** @brief VSYNC events
 *
 * These events are reported when DDK starts to wait for vsync and when the
 * vsync has occured and the DDK can continue on the next frame.
 */
typedef enum _mali_uk_vsync_event {
	_MALI_UK_VSYNC_EVENT_BEGIN_WAIT = 0,
	_MALI_UK_VSYNC_EVENT_END_WAIT
} _mali_uk_vsync_event;

/** @brief Arguments to _mali_ukk_vsync_event()
 *
 */
typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	_mali_uk_vsync_event event;     /**< [in] VSYNCH event type */
} _mali_uk_vsync_event_report_s;

/** @} */ /* end group _mali_uk_vsync */

/** @defgroup _mali_uk_sw_counters_report U/K Software Counter Reporting
 * @{ */

/** @brief Software counter values
 *
 * Values recorded for each of the software counters during a single renderpass.
 */
typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u64 counters;                  /**< [in] The array of u32 counter values */
	u32 num_counters;              /**< [in] The number of elements in counters array */
} _mali_uk_sw_counters_report_s;

/** @} */ /* end group _mali_uk_sw_counters_report */

/** @defgroup _mali_uk_timeline U/K Mali Timeline
 * @{ */

typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	u32 timeline;                   /**< [in] timeline id */
	u32 point;                      /**< [out] latest point on timeline */
} _mali_uk_timeline_get_latest_point_s;

typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	_mali_uk_fence_t fence;         /**< [in] fence */
	u32 timeout;                    /**< [in] timeout (0 for no wait, -1 for blocking) */
	u32 status;                     /**< [out] status of fence (1 if signaled, 0 if timeout) */
} _mali_uk_timeline_wait_s;

typedef struct {
	u64 ctx;                      /**< [in,out] user-kernel context (trashed on output) */
	_mali_uk_fence_t fence;         /**< [in] mali fence to create linux sync fence from */
	s32 sync_fd;                    /**< [out] file descriptor for new linux sync fence */
} _mali_uk_timeline_create_sync_fence_s;

/** @} */ /* end group _mali_uk_timeline */

/** @} */ /* end group u_k_api */

/** @} */ /* end group uddapi */

typedef struct {
	u64 ctx;                 /**< [in,out] user-kernel context (trashed on output) */
	s32 stream_fd;   /**< [in] The profiling kernel base stream fd handle */
} _mali_uk_profiling_stream_fd_get_s;

typedef struct {
	u64 ctx;        /**< [in,out] user-kernel context (trashed on output) */
	u64 control_packet_data; /**< [in] the control packet data for control settings */
	u32 control_packet_size;  /**< [in] The control packet size */
	u64 response_packet_data; /** < [out] The response packet data */
	u32 response_packet_size; /** < [in,out] The response packet data */
} _mali_uk_profiling_control_set_s;

#ifdef __cplusplus
}
#endif

#endif /* __MALI_UTGARD_UK_TYPES_H__ */
