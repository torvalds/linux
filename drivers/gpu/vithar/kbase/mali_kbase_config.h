/*
 *
 * (C) COPYRIGHT 2010, 2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_config.h
 * Configuration API and Attributes for KBase
 */

#ifndef _KBASE_CONFIG_H_
#define _KBASE_CONFIG_H_

#include <malisw/mali_stdtypes.h>

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_kbase_api
 * @{
 */

/**
 * @addtogroup kbase_config Configuration API and Attributes
 * @{
 */

#if MALI_CUSTOMER_RELEASE == 0
/* This flag is set for internal builds so we can run tests without credentials. */
#define KBASE_HWCNT_DUMP_BYPASS_ROOT 1
#else
#define KBASE_HWCNT_DUMP_BYPASS_ROOT 0
#endif

/**
 * Relative memory performance indicators. Enum elements should always be defined in slowest to fastest order.
 */
typedef enum kbase_memory_performance
{
	KBASE_MEM_PERF_SLOW,
	KBASE_MEM_PERF_NORMAL,
	KBASE_MEM_PERF_FAST,

	KBASE_MEM_PERF_MAX_VALUE = KBASE_MEM_PERF_FAST
} kbase_memory_performance;

/**
 * Device wide configuration
 */
enum
{
	/**
	 * Invalid attribute ID (reserve 0).
	 *
	 * Attached value: Ignored
	 * Default value: NA
	 * */
	KBASE_CONFIG_ATTR_INVALID,

	/**
	 * Memory resource object.
	 * Multiple resources can be listed.
	 * The resources will be used in the order listed
	 * in the configuration attribute list if they have no other
	 * preferred order based on the memory resource property list
	 * (see ::kbase_memory_attribute).
	 *
	 * Attached value: Pointer to a kbase_memory_resource object.
	 * Default value: No resources
	 * */

	KBASE_CONFIG_ATTR_MEMORY_RESOURCE,
	/**
	 * Maximum of memory which can be allocated from the OS
	 * to be used by the GPU (shared memory).
	 *
	 * Attached value: number in bytes
	 * Default value: Limited by available memory
	 */
	KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_MAX,

	/**
	 * Relative performance for the GPU to access
	 * OS shared memory.
	 *
	 * Attached value: ::kbase_memory_performance member
	 * Default value: ::KBASE_MEM_PERF_NORMAL
	 */
	KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_PERF_GPU,

	/**
	 * Limit (in bytes) the amount of memory a single process
	 * can allocate across all memory banks (including OS shared memory)
	 * for use by the GPU.
	 *
	 * Attached value: number in bytes
	 * Default value: Limited by available memory
	 */
	KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT,

	/**
	 * UMP device mapping.
	 * Which UMP device this GPU should be mapped to.
	 *
	 * Attached value: UMP_DEVICE_<device>_SHIFT
	 * Default value: UMP_DEVICE_W_SHIFT
	 */
	KBASE_CONFIG_ATTR_UMP_DEVICE,

	/**
	 * Maximum frequency GPU will be clocked at. Given in kHz.
	 * This must be specified as there is no default value.
	 * 
	 * Attached value: number in kHz
	 * Default value: NA
	 */
	KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX,

	/**
	 * Minimum frequency GPU will be clocked at. Given in kHz.
	 * This must be specified as there is no default value.
	 * 
	 * Attached value: number in kHz
	 * Default value: NA
	 */
	KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN,

	/**
	 * Irq throttle. It is the minimum desired time in between two
	 * consecutive gpu interrupts (given in 'us'). The irq throttle
	 * gpu register will be configured after this, taking into
	 * account the configured max frequency.
	 * 
	 * Attached value: number in micro seconds
	 * Default value: see DEFAULT_IRQ_THROTTLE_TIME_US
	 */
	KBASE_CONFIG_ATTR_GPU_IRQ_THROTTLE_TIME_US,

	/*** Begin Job Scheduling Configs ***/
	/**
	 * Job Scheduler scheduling tick granuality. This is in nanoseconds to
	 * allow HR timer support.
	 *
	 * On each scheduling tick, the scheduler may decide to:
	 * -# soft stop a job (the job will be re-run later, and other jobs will
	 * be able to run on the GPU now). This effectively controls the
	 * 'timeslice' given to a job.
	 * -# hard stop a job (to kill a job if it has spent too long on the GPU
	 * and didn't soft-stop).
	 *
	 * The numbers of ticks for these events are controlled by:
	 * - @ref KBASE_CONIFG_ATTR_JS_SOFT_STOP_TICKS
	 * - @ref KBASE_CONIFG_ATTR_JS_HARD_STOP_TICKS_SS
	 * - @ref KBASE_CONIFG_ATTR_JS_HARD_STOP_TICKS_NSS
	 *
	 * A soft-stopped job will later be resumed, allowing it to use more GPU
	 * time <em>in total</em> than that defined by any of the above. However,
	 * the scheduling policy attempts to limit the amount of \em uninterrupted
	 * time spent on the GPU using the above values (that is, the 'timeslice'
	 * of a job)
	 *
	 * This value is supported by the following scheduling policies:
	 * - The Completely Fair Share (CFS) policy
	 *
	 * Attached value: unsigned 32-bit kbasep_js_device_data::scheduling_tick_ns.
	 * The value might be rounded down to lower precision. Must be non-zero
	 * after rounding.<br>
	 * Default value: @ref DEFAULT_JS_SCHEDULING_TICK_NS
	 *
	 * @note this value is allowed to be greater than
	 * @ref KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS. This allows jobs to run on (much)
	 * longer than the job-timeslice, but once this happens, the context gets
	 * scheduled in (much) less frequently than others that stay within the
	 * ctx-timeslice.
	 */
	KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS,

	/**
	 * Job Scheduler minimum number of scheduling ticks before jobs are soft-stopped.
	 *
	 * This defines the amount of time a job is allowed to stay on the GPU,
	 * before it is soft-stopped to allow other jobs to run.
	 *
	 * That is, this defines the 'timeslice' of the job. It is separate from the
	 * timeslice of the context that contains the job (see
	 * @ref KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS).
	 *
	 * This value is supported by the following scheduling policies:
	 * - The Completely Fair Share (CFS) policy
	 *
	 * Attached value: unsigned 32-bit kbasep_js_device_data::soft_stop_ticks<br>
	 * Default value: @ref DEFAULT_JS_SOFT_STOP_TICKS
	 *
	 * @note a value of zero means "the quickest time to soft-stop a job",
	 * which is somewhere between instant and one tick later.
	 *
	 * @note this value is allowed to be greater than
	 * @ref KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS or
	 * @ref KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS. This effectively disables
	 * soft-stop, and just uses hard-stop instead. In this case, this value
	 * should be much greater than any of the hard stop values (to avoid
	 * soft-stop-after-hard-stop)
	 *
	 * @see KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS
	 */
	KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS,

	/**
	 * Job Scheduler minimum number of scheduling ticks before Soft-Stoppable
	 * (BASE_JD_REQ_NSS bit \b clear) jobs are hard-stopped.
	 *
	 * This defines the amount of time a Soft-Stoppable job is allowed to spend
	 * on the GPU before it is killed. Such jobs won't be resumed if killed.
	 *
	 * This value is supported by the following scheduling policies:
	 * - The Completely Fair Share (CFS) policy
	 *
	 * Attached value: unsigned 32-bit kbasep_js_device_data::hard_stop_ticks_ss<br>
	 * Default value: @ref DEFAULT_JS_HARD_STOP_TICKS_SS
	 *
	 * @note a value of zero means "the quickest time to hard-stop a job",
	 * which is somewhere between instant and one tick later.
	 *
	 * @see KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS
	 */
	KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS,

	/**
	 * Job Scheduler minimum number of scheduling ticks before Non-Soft-Stoppable
	 * (BASE_JD_REQ_NSS bit \b set) jobs are hard-stopped.
	 *
	 * This defines the amount of time a Non-Soft-Stoppable job is allowed to spend
	 * on the GPU before it is killed. Such jobs won't be resumed if killed.
	 *
	 * This value is supported by the following scheduling policies:
	 * - The Completely Fair Share (CFS) policy
	 *
	 * Attached value: unsigned 32-bit kbasep_js_device_data::hard_stop_ticks_nss<br>
	 * Default value: @ref DEFAULT_JS_HARD_STOP_TICKS_NSS
	 *
	 * @note a value of zero means "the quickest time to hard-stop a job",
	 * which is somewhere between instant and one tick later.
	 *
	 * @see KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS
	 */
	KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS,

	/**
	 * Job Scheduler timeslice that a context is scheduled in for, in nanoseconds.
	 *
	 * When a context has used up this amount of time across its jobs, it is
	 * scheduled out to let another run.
	 *
	 * @note the resolution is nanoseconds (ns) here, because that's the format
	 * often used by the OS.
	 *
	 * This value controls affects the actual time defined by the following
	 * config values:
	 * - @ref KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_INIT_SLICES
	 * - @ref KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_MIN_SLICES
	 *
	 * This value is supported by the following scheduling policies:
	 * - The Completely Fair Share (CFS) policy
	 *
	 * Attached value: unsigned 32-bit kbasep_js_device_data::ctx_timeslice_ns.
	 * The value might be rounded down to lower precision.<br>
	 * Default value: @ref DEFAULT_JS_CTX_TIMESLICE_NS
	 *
	 * @note a value of zero models a "Round Robin" scheduling policy, and
	 * disables @ref KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_INIT_SLICES
	 * (initially causing LIFO scheduling) and
	 * @ref KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_MIN_SLICES (allowing
	 * not-run-often contexts to get scheduled in quickly, but to only use
	 * a single timeslice when they get scheduled in).
	 */
	KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS,

	/**
	 * Job Scheduler initial runtime of a context for the CFS Policy, in time-slices.
	 *
	 * This value is relative to that of the least-run context, and defines
	 * where in the CFS queue a new context is added. A value of 1 means 'after
	 * the least-run context has used its timeslice'. Therefore, when all
	 * contexts consistently use the same amount of time, a value of 1 models a
	 * FIFO. A value of 0 would model a LIFO.
	 *
	 * The value is represented in "numbers of time slices". Multiply this
	 * value by that defined in @ref KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS to get
	 * the time value for this in nanoseconds.
	 *
	 * Attached value: unsigned 32-bit kbasep_js_device_data::cfs_ctx_runtime_init_slices<br>
	 * Default value: @ref DEFAULT_JS_CFS_CTX_RUNTIME_INIT_SLICES
	 */
	KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_INIT_SLICES,

	/**
	 * Job Scheduler minimum runtime value of a context for CFS, in time_slices
	 * relative to that of the least-run context.
	 *
	 * This is a measure of how much preferrential treatment is given to a
	 * context that is not run very often.
	 *
	 * Specficially, this value defines how many timeslices such a context is
	 * (initially) allowed to use at once. Such contexts (e.g. 'interactive'
	 * processes) will appear near the front of the CFS queue, and can initially
	 * use more time than contexts that run continuously (e.g. 'batch'
	 * processes).
	 *
	 * This limit \b prevents a "stored-up timeslices" DoS attack, where a ctx
	 * not run for a long time attacks the system by using a very large initial
	 * number of timeslices when it finally does run.
	 *
	 * Attached value: unsigned 32-bit kbasep_js_device_data::cfs_ctx_runtime_min_slices<br>
	 * Default value: @ref DEFAULT_JS_CFS_CTX_RUNTIME_MIN_SLICES
	 *
	 * @note A value of zero allows not-run-often contexts to get scheduled in
	 * quickly, but to only use a single timeslice when they get scheduled in.
	 */
	KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_MIN_SLICES,

	/**
	 * Job Scheduler minimum number of scheduling ticks before Soft-Stoppable
	 * (BASE_JD_REQ_NSS bit \b clear) jobs cause the GPU to be reset.
	 *
	 * This defines the amount of time a Soft-Stoppable job is allowed to spend
	 * on the GPU before it is assumed that the GPU has hung and needs to be reset.
	 * The assumes that the job has been hard-stopped already and so the presence of
	 * a job that has remained on the GPU for so long indicates that the GPU has in some
	 * way hung.
	 *
	 * This value is supported by the following scheduling policies:
	 * - The Completely Fair Share (CFS) policy
	 *
	 * Attached value: unsigned 32-bit kbasep_js_device_data::gpu_reset_ticks_nss<br>
	 * Default value: @ref DEFAULT_JS_RESET_TICKS_SS
	 *
	 * @see KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS
	 */
	KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS,

	/**
	 * Job Scheduler minimum number of scheduling ticks before Non-Soft-Stoppable
	 * (BASE_JD_REQ_NSS bit \b set) jobs cause the GPU to be reset.
	 *
	 * This defines the amount of time a Non-Soft-Stoppable job is allowed to spend
	 * on the GPU before it is assumed that the GPU has hung and needs to be reset.
	 * The assumes that the job has been hard-stopped already and so the presence of
	 * a job that has remained on the GPU for so long indicates that the GPU has in some
	 * way hung.
	 *
	 * This value is supported by the following scheduling policies:
	 * - The Completely Fair Share (CFS) policy
	 *
	 * Attached value: unsigned 32-bit kbasep_js_device_data::gpu_reset_ticks_nss<br>
	 * Default value: @ref DEFAULT_JS_RESET_TICKS_NSS
	 *
	 * @see KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS
	 */
	KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS,

	/**
	 * Number of milliseconds given for other jobs on the GPU to be 
	 * soft-stopped when the GPU needs to be reset.
	 *
	 * Attached value: number in milliseconds
	 * Default value: @ref DEFAULT_JS_RESET_TIMEOUT_MS
	 */
	KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS,

	/*** End Job Scheduling Configs ***/

	/**
	 * End of attribute list indicator.
	 * The configuration loader will stop processing any more elements
	 * when it encounters this attribute.
	 *
	 * Attached value: Ignored
	 * Default value: NA
	 */
	KBASE_CONFIG_ATTR_END = 0x1FFFUL
};

enum
{
	/**
	 * Invalid attribute ID (reserve 0).
	 *
	 * Attached value: Ignored
	 * Default value: NA
	 */
	KBASE_MEM_ATTR_INVALID,

	/**
	 * Relative performance for the CPU to access
	 * the memory resource.
	 *
	 * Attached value: ::kbase_memory_performance member
	 * Default value: ::KBASE_MEM_PERF_NORMAL
	 */
	KBASE_MEM_ATTR_PERF_CPU,

	/**
	 * Relative performance for the GPU to access
	 * the memory resource.
	 *
	 * Attached value: ::kbase_memory_performance member
	 * Default value: ::KBASE_MEM_PERF_NORMAL
	 */
	KBASE_MEM_ATTR_PERF_GPU,

	/**
	 * End of attribute list indicator.
	 * The memory resource loader will stop processing any more
	 * elements when it encounters this attribute.
	 *
	 * Attached value: Ignored
	 * Default value: NA
	 */
	KBASE_MEM_ATTR_END = 0x1FFFUL
};


/*
 * @brief specifies a single attribute
 *
 * Attribute is identified by attr field. Data is either integer or a pointer to attribute-specific structure.
 */
typedef struct kbase_attribute
{
	int id;
	uintptr_t data;
} kbase_attribute;

/*
 * @brief Specifies dedicated memory bank
 *
 * Specifies base, size and attributes of a memory bank
 */
typedef struct kbase_memory_resource
{
	u64 base;
	u64 size;
	struct kbase_attribute * attributes;
	const char * name;
} kbase_memory_resource;

#if !MALI_LICENSE_IS_GPL || (defined(MALI_FAKE_PLATFORM_DEVICE) && MALI_FAKE_PLATFORM_DEVICE)
/*
 * @brief Specifies start and end of I/O memory region.
 */
typedef struct kbase_io_memory_region
{
	u64       start;
	u64       end;
} kbase_io_memory_region;

/*
 * @brief Specifies I/O related resources like IRQs and memory region for I/O operations.
 */
typedef struct kbase_io_resources
{
	u32                      job_irq_number;
	u32                      mmu_irq_number;
	u32                      gpu_irq_number;
	kbase_io_memory_region   io_memory_region;
} kbase_io_resources;

typedef struct kbase_platform_config
{
	const kbase_attribute *attributes;
	const kbase_io_resources *io_resources;
	u32 midgard_type;
} kbase_platform_config;

#endif /* !MALI_LICENSE_IS_GPL || (defined(MALI_FAKE_PLATFORM_DEVICE) && MALI_FAKE_PLATFORM_DEVICE) */
/**
 * @brief Return character string associated with the given midgard type.
 *
 * @param[in]  midgard_type - ID of midgard type
  *
 * @return  Pointer to NULL-terminated character array associated with the given midgard type
 */
const char *kbasep_midgard_type_to_string(u32 midgard_type);

/**
 * @brief Gets the count of attributes in array
 *
 * Function gets the count of attributes in array. Note that end of list indicator is also included.
 *
 * @param[in]  attributes     Array of attributes
  *
 * @return  Number of attributes in the array including end of list indicator.
 */
int kbasep_get_config_attribute_count(const kbase_attribute *attributes);

/**
 * @brief Gets the count of attributes with specified id
 *
 * Function gets the count of attributes with specified id in the given attribute array
 *
 * @param[in]  attributes     Array of attributes
 * @param[in]  attibute_id    Id of attributes to count
  *
 * @return  Number of attributes in the array that have specified id
 */
int kbasep_get_config_attribute_count_by_id(const kbase_attribute *attributes, int attribute_id);

/**
 * @brief Gets the next config attribute with the specified ID from the array of attributes.
 *
 * Function gets the next attribute with specified attribute id within specified array. If no such attribute is found,
 * NULL is returned.
 *
 * @param[in]  attributes     Array of attributes in which lookup is performed
 * @param[in]  attribute_id   ID of attribute
 *
 * @return  Pointer to the first attribute matching id or NULL if none is found.
 */
const kbase_attribute *kbasep_get_next_attribute(const kbase_attribute *attributes, int attribute_id);

/**
 * @brief Gets the value of a single config attribute.
 *
 * Function gets the value of attribute specified as parameter. If no such attribute is found in the array of
 * attributes, default value is used.
 *
 * @param[in]  attributes     Array of attributes in which lookup is performed
 * @param[in]  attribute_id   ID of attribute
 *
 * @return Value of attribute with the given id
 */
uintptr_t kbasep_get_config_value(const kbase_attribute *attributes, int attribute_id);

/**
 * @brief Obtain memory performance values from kbase_memory_resource structure.
 *
 * Function gets cpu and gpu memory performance values from memory resource structure and puts them in the variables
 * provided as parameters. If the performance of memory bank is not in resource attributes, default value is used.
 *
 * @param[in]  resource         Structure containing information about memory bank to use
 * @param[out] cpu_performance  Pointer to variable which will hold CPU performance value
 * @param[out] gpu_performance  Pointer to variable which will hold GPU performance value
 */
void kbasep_get_memory_performance(const kbase_memory_resource *resource,
				kbase_memory_performance *cpu_performance, kbase_memory_performance *gpu_performance);

/**
 * @brief Validates configuration attributes
 *
 * Function checks validity of given configuration attributes. It will fail on any attribute with unknown id, attribute
 * with invalid value or attribute list that is not correctly terminated. It will also fail if
 * KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN or KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX are not specified.
 *
 * @param[in]  attributes  Array of attributes to validate
 *
 * @return   MALI_TRUE if no errors have been found in the config. MALI_FALSE otherwise.
 */
mali_bool kbasep_validate_configuration_attributes(const kbase_attribute *attributes);

#if !MALI_LICENSE_IS_GPL || (defined(MALI_FAKE_PLATFORM_DEVICE) && MALI_FAKE_PLATFORM_DEVICE)
/**
 * @brief Gets the pointer to platform config.
 *
 * @return Pointer to the platform config
 */
kbase_platform_config *kbasep_get_platform_config(void);
#endif /* !MALI_LICENSE_IS_GPL || (defined(MALI_FAKE_PLATFORM_DEVICE) && MALI_FAKE_PLATFORM_DEVICE) */

/** @} */ /* end group kbase_config */
/** @} */ /* end group base_kbase_api */
/** @} */ /* end group base_api */

#endif /* _KBASE_CONFIG_H_ */
