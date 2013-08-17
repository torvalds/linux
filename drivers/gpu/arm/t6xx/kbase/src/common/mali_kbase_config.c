/*
 *
 * (C) COPYRIGHT 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_defs.h>
#include <kbase/src/common/mali_kbase_cpuprops.h>
#include <osk/mali_osk.h>
#ifdef CONFIG_UMP
#include <linux/ump-common.h>
#endif /* CONFIG_UMP */

/* Specifies how many attributes are permitted in the config (excluding terminating attribute).
 * This is used in validation function so we can detect if configuration is properly terminated. This value can be
 * changed if we need to introduce more attributes or many memory regions need to be defined */
#define ATTRIBUTE_COUNT_MAX 32

/* right now we allow only 2 memory attributes (excluding termination attribute) */
#define MEMORY_ATTRIBUTE_COUNT_MAX 2

/* Limits for gpu frequency configuration parameters. These will use for config validation. */
#define MAX_GPU_ALLOWED_FREQ_KHZ 1000000
#define MIN_GPU_ALLOWED_FREQ_KHZ 1

/* Default irq throttle time. This is the default desired minimum time in between two consecutive
 * interrupts from the gpu. The irq throttle gpu register is set after this value. */
#define DEFAULT_IRQ_THROTTLE_TIME_US 20

/*** Begin Scheduling defaults ***/

/**
 * Default scheduling tick granuality, in nanoseconds
 */
#define DEFAULT_JS_SCHEDULING_TICK_NS 100000000u /* 100ms */

/**
 * Default minimum number of scheduling ticks before jobs are soft-stopped.
 *
 * This defines the time-slice for a job (which may be different from that of a context)
 */
#define DEFAULT_JS_SOFT_STOP_TICKS 1 /* Between 0.1 and 0.2s before soft-stop */

/**
 * Default minimum number of scheduling ticks before Soft-Stoppable
 * (BASE_JD_REQ_NSS bit clear) jobs are hard-stopped
 */
#define DEFAULT_JS_HARD_STOP_TICKS_SS_HW_ISSUE_8408 12 /* 1.2s before hard-stop, for a certain GLES2 test at 128x128 (bound by combined vertex+tiler job) */
#define DEFAULT_JS_HARD_STOP_TICKS_SS 2 /* Between 0.2 and 0.3s before hard-stop */

/**
 * Default minimum number of scheduling ticks before Non-Soft-Stoppable
 * (BASE_JD_REQ_NSS bit set) jobs are hard-stopped
 */
#define DEFAULT_JS_HARD_STOP_TICKS_NSS 600 /* 60s @ 100ms tick */

/**
 * Default minimum number of scheduling ticks before the GPU is reset
 * to clear a "stuck" Soft-Stoppable job
 */
#define DEFAULT_JS_RESET_TICKS_SS_HW_ISSUE_8408 18 /* 1.8s before resetting GPU, for a certain GLES2 test at 128x128 (bound by combined vertex+tiler job) */
#define DEFAULT_JS_RESET_TICKS_SS 3 /* 0.3-0.4s before GPU is reset */

/**
 * Default minimum number of scheduling ticks before the GPU is reset
 * to clear a "stuck" Non-Soft-Stoppable job
 */
#define DEFAULT_JS_RESET_TICKS_NSS 601 /* 60.1s @ 100ms tick */

/**
 * Number of milliseconds given for other jobs on the GPU to be
 * soft-stopped when the GPU needs to be reset.
 */
#define DEFAULT_JS_RESET_TIMEOUT_MS 3000

/**
 * Default timeslice that a context is scheduled in for, in nanoseconds.
 *
 * When a context has used up this amount of time across its jobs, it is
 * scheduled out to let another run.
 *
 * @note the resolution is nanoseconds (ns) here, because that's the format
 * often used by the OS.
 */
#define DEFAULT_JS_CTX_TIMESLICE_NS 50000000 /* 0.05s - at 20fps a ctx does at least 1 frame before being scheduled out. At 40fps, 2 frames, etc */

/**
 * Default initial runtime of a context for CFS, in ticks.
 *
 * This value is relative to that of the least-run context, and defines where
 * in the CFS queue a new context is added.
 */
#define DEFAULT_JS_CFS_CTX_RUNTIME_INIT_SLICES 1

/**
 * Default minimum runtime value of a context for CFS, in ticks.
 *
 * This value is relative to that of the least-run context. This prevents
 * "stored-up timeslices" DoS attacks.
 */
#define DEFAULT_JS_CFS_CTX_RUNTIME_MIN_SLICES 2

/**
 * Default setting for whether to prefer security or performance.
 *
 * Currently affects only r0p0-15dev0 HW and earlier.
 */
#define DEFAULT_SECURE_BUT_LOSS_OF_PERFORMANCE MALI_FALSE

/*** End Scheduling defaults ***/

/**
 * Default value for KBASE_CONFIG_ATTR_CPU_SPEED_FUNC.
 * Points to @ref kbase_cpuprops_get_default_clock_speed.
 */
#define DEFAULT_CPU_SPEED_FUNC ((uintptr_t)kbase_cpuprops_get_default_clock_speed)

#ifdef CONFIG_MALI_PLATFORM_FAKE

extern kbase_platform_config platform_config;
kbase_platform_config *kbasep_get_platform_config(void)
{
	return &platform_config;
}
#endif /* CONFIG_MALI_PLATFORM_FAKE */

int kbasep_get_config_attribute_count(const kbase_attribute *attributes)
{
	int count = 1;

	OSK_ASSERT(attributes != NULL);

	while (attributes->id != KBASE_CONFIG_ATTR_END)
	{
		attributes++;
		count++;
	}

	return count;
}

int kbasep_get_config_attribute_count_by_id(const kbase_attribute *attributes, int attribute_id)
{
	int count = 0;
	OSK_ASSERT(attributes != NULL);

	while (attributes->id != KBASE_CONFIG_ATTR_END)
	{
		if (attributes->id == attribute_id)
		{
			count++;
		}
		attributes++;
	}

	return count;
}

const kbase_attribute *kbasep_get_next_attribute(const kbase_attribute *attributes, int attribute_id)
{
	OSK_ASSERT(attributes != NULL);

	while (attributes->id != KBASE_CONFIG_ATTR_END)
	{
		if (attributes->id == attribute_id)
		{
			return attributes;
		}
		attributes++;
	}
	return NULL;
}
KBASE_EXPORT_TEST_API(kbasep_get_next_attribute)

uintptr_t kbasep_get_config_value(struct kbase_device *kbdev, const kbase_attribute *attributes, int attribute_id)
{
	const kbase_attribute *attr;

	OSK_ASSERT(attributes != NULL);

	attr = kbasep_get_next_attribute(attributes, attribute_id);
	if (attr != NULL)
	{
		return attr->data;
	}

	/* default values */
	switch (attribute_id)
	{
		case KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT:
			return (uintptr_t)-1;
#ifdef CONFIG_UMP
		case KBASE_CONFIG_ATTR_UMP_DEVICE:
			return UMP_DEVICE_W_SHIFT;
#endif /* CONFIG_UMP */
		case KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_MAX:
			return (uintptr_t)-1;
		case KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_PERF_GPU:
			return KBASE_MEM_PERF_NORMAL;
		case KBASE_CONFIG_ATTR_GPU_IRQ_THROTTLE_TIME_US:
			return DEFAULT_IRQ_THROTTLE_TIME_US;
		/* Begin scheduling defaults */
		case KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS:
			return     DEFAULT_JS_SCHEDULING_TICK_NS;
		case KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS:
			return     DEFAULT_JS_SOFT_STOP_TICKS;
		case KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS:
			if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408))
			{
				return DEFAULT_JS_HARD_STOP_TICKS_SS_HW_ISSUE_8408;
			}
			else
			{
				return DEFAULT_JS_HARD_STOP_TICKS_SS;
			}
		case KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS:
			return     DEFAULT_JS_HARD_STOP_TICKS_NSS;
		case KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS:
			return     DEFAULT_JS_CTX_TIMESLICE_NS;
		case KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_INIT_SLICES:
			return     DEFAULT_JS_CFS_CTX_RUNTIME_INIT_SLICES;
		case KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_MIN_SLICES:
			return     DEFAULT_JS_CFS_CTX_RUNTIME_MIN_SLICES;
		case KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS:
			if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408))
			{
				return DEFAULT_JS_RESET_TICKS_SS_HW_ISSUE_8408;
			}
			else
			{
				return DEFAULT_JS_RESET_TICKS_SS;
			}
		case KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS:
			return     DEFAULT_JS_RESET_TICKS_NSS;
		case KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS:
			return     DEFAULT_JS_RESET_TIMEOUT_MS;
		/* End scheduling defaults */
		case KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS:
			return 0;
		case KBASE_CONFIG_ATTR_PLATFORM_FUNCS:
			return 0;
		case  KBASE_CONFIG_ATTR_SECURE_BUT_LOSS_OF_PERFORMANCE:
			return DEFAULT_SECURE_BUT_LOSS_OF_PERFORMANCE;
		case KBASE_CONFIG_ATTR_CPU_SPEED_FUNC:
			return DEFAULT_CPU_SPEED_FUNC;
		case KBASE_CONFIG_ATTR_GPU_SPEED_FUNC:
			return 0;
		default:
			OSK_PRINT_ERROR(OSK_BASE_CORE,
			    "kbasep_get_config_value. Cannot get value of attribute with id=%d and no default value defined",
			    attribute_id);
			return 0;
	}
}
KBASE_EXPORT_TEST_API(kbasep_get_config_value)

mali_bool kbasep_platform_device_init(kbase_device *kbdev)
{
	kbase_platform_funcs_conf *platform_funcs;

	if (OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
	{
		return MALI_FALSE;
	}

	platform_funcs = (kbase_platform_funcs_conf *) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_PLATFORM_FUNCS);
	if(platform_funcs)
	{
		if(platform_funcs->platform_init_func)
		{
			return platform_funcs->platform_init_func(kbdev);
		}
	}
	return MALI_TRUE;
}

void kbasep_platform_device_term(kbase_device *kbdev)
{
	kbase_platform_funcs_conf *platform_funcs;

	platform_funcs = (kbase_platform_funcs_conf *) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_PLATFORM_FUNCS);
	if(platform_funcs)
	{
		if(platform_funcs->platform_term_func)
		{
			platform_funcs->platform_term_func(kbdev);
		}
	}
}

void kbasep_get_memory_performance(const kbase_memory_resource *resource, kbase_memory_performance *cpu_performance,
		kbase_memory_performance *gpu_performance)
{
	kbase_attribute *attributes;

	OSK_ASSERT(resource != NULL);
	OSK_ASSERT(cpu_performance != NULL );
	OSK_ASSERT(gpu_performance != NULL);

	attributes = resource->attributes;
	*cpu_performance = *gpu_performance = KBASE_MEM_PERF_NORMAL; /* default performance */

	if (attributes == NULL)
	{
		return;
	}

	while (attributes->id != KBASE_CONFIG_ATTR_END)
	{
		if (attributes->id == KBASE_MEM_ATTR_PERF_GPU)
		{
			*gpu_performance = (kbase_memory_performance) attributes->data;
		}
		else if (attributes->id == KBASE_MEM_ATTR_PERF_CPU)
		{
			*cpu_performance = (kbase_memory_performance) attributes->data;
		}
		attributes++;
	}
}

#ifdef CONFIG_UMP
static mali_bool kbasep_validate_ump_device(int ump_device)
{
	mali_bool valid;

	if (OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
	{
		return MALI_FALSE;
	}

	switch (ump_device)
	{
		case UMP_DEVICE_W_SHIFT:
		case UMP_DEVICE_X_SHIFT:
		case UMP_DEVICE_Y_SHIFT:
		case UMP_DEVICE_Z_SHIFT:
			valid = MALI_TRUE;
			break;
		default:
			valid = MALI_FALSE;
			break;
	}
	return valid;
}
#endif /* CONFIG_UMP */

static mali_bool kbasep_validate_memory_performance(kbase_memory_performance performance)
{
	if (OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
	{
		return MALI_FALSE;
	}
	return performance <= KBASE_MEM_PERF_MAX_VALUE;
}

static mali_bool kbasep_validate_memory_resource(const kbase_memory_resource *memory_resource)
{
	OSK_ASSERT(memory_resource != NULL);

	if (OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
	{
		return MALI_FALSE;
	}

	if (memory_resource->name == NULL)
	{
		OSK_PRINT_WARN(OSK_BASE_CORE, "Unnamed memory region found");
		return MALI_FALSE;
	}

	if (memory_resource->base & ((1 << PAGE_SHIFT) - 1))
	{
		OSK_PRINT_WARN(OSK_BASE_CORE, "Base address of \"%s\" memory region is not page aligned", memory_resource->name);
		return MALI_FALSE;
	}

	if (memory_resource->size & ((1 << PAGE_SHIFT) - 1))
	{
		OSK_PRINT_WARN(OSK_BASE_CORE, "Size of \"%s\" memory region is not a multiple of page size", memory_resource->name);
		return MALI_FALSE;
	}

	if (memory_resource->attributes != NULL) /* we allow NULL attribute list */
	{
		int i;

		for (i = 0; memory_resource->attributes[i].id != KBASE_MEM_ATTR_END; i++)
		{
			if (i >= MEMORY_ATTRIBUTE_COUNT_MAX)
			{
				OSK_PRINT_WARN(OSK_BASE_CORE, "More than MEMORY_ATTRIBUTE_COUNT_MAX=%d configuration attributes defined. Is memory attribute list properly terminated?",
						MEMORY_ATTRIBUTE_COUNT_MAX);
				return MALI_FALSE;
			}
			switch(memory_resource->attributes[i].id)
			{
				case KBASE_MEM_ATTR_PERF_CPU:
					if (MALI_TRUE != kbasep_validate_memory_performance(
							(kbase_memory_performance)memory_resource->attributes[i].data))
					{
						OSK_PRINT_WARN(OSK_BASE_CORE, "CPU performance of \"%s\" region is invalid: %d",
								memory_resource->name, (kbase_memory_performance)memory_resource->attributes[i].data);
						return MALI_FALSE;
					}
					break;

				case KBASE_MEM_ATTR_PERF_GPU:
					if (MALI_TRUE != kbasep_validate_memory_performance(
											(kbase_memory_performance)memory_resource->attributes[i].data))
					{
						OSK_PRINT_WARN(OSK_BASE_CORE, "GPU performance of \"%s\" region is invalid: %d",
								memory_resource->name, (kbase_memory_performance)memory_resource->attributes[i].data);
							return MALI_FALSE;
					}
					break;
				default:
					OSK_PRINT_WARN(OSK_BASE_CORE, "Invalid memory attribute found in \"%s\" memory region: %d",
							memory_resource->name, memory_resource->attributes[i].id);
					return MALI_FALSE;
			}
		}
	}

	return MALI_TRUE;
}


static mali_bool kbasep_validate_gpu_clock_freq(kbase_device *kbdev, const kbase_attribute *attributes)
{
	uintptr_t freq_min = kbasep_get_config_value(kbdev, attributes, KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN);
	uintptr_t freq_max = kbasep_get_config_value(kbdev, attributes, KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX);

	if (OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
	{
		return MALI_FALSE;
	}

	if ((freq_min > MAX_GPU_ALLOWED_FREQ_KHZ) ||
		(freq_min < MIN_GPU_ALLOWED_FREQ_KHZ) ||
		(freq_max > MAX_GPU_ALLOWED_FREQ_KHZ) ||
		(freq_max < MIN_GPU_ALLOWED_FREQ_KHZ) ||
		(freq_min > freq_max))
	{
		OSK_PRINT_WARN(OSK_BASE_CORE, "Invalid GPU frequencies found in configuration: min=%ldkHz, max=%ldkHz.", freq_min, freq_max);
		return MALI_FALSE;
	}
	
	return MALI_TRUE;
}

static mali_bool kbasep_validate_pm_callback(const kbase_pm_callback_conf *callbacks)
{
	if (OSK_SIMULATE_FAILURE(OSK_BASE_PM))
	{
		return MALI_FALSE;
	}

	if (callbacks == NULL)
	{
		/* Having no callbacks is valid */
		return MALI_TRUE;
	}

	if ((callbacks->power_off_callback != NULL && callbacks->power_on_callback == NULL) ||
	    (callbacks->power_off_callback == NULL && callbacks->power_on_callback != NULL))
	{
		OSK_PRINT_WARN(OSK_BASE_CORE, "Invalid power management callbacks: Only one of power_off_callback and power_on_callback was specified");
		return MALI_FALSE;
	}
	return MALI_TRUE;
}

static mali_bool kbasep_validate_cpu_speed_func(kbase_cpuprops_clock_speed_function fcn)
{
	if (OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
	{
		return MALI_FALSE;
	}
	return fcn != NULL;
}

mali_bool kbasep_validate_configuration_attributes(kbase_device *kbdev, const kbase_attribute *attributes)
{
	int i;
	mali_bool had_gpu_freq_min = MALI_FALSE, had_gpu_freq_max = MALI_FALSE;

	OSK_ASSERT(attributes);

	if (OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
	{
		return MALI_FALSE;
	}

	for (i = 0; attributes[i].id != KBASE_CONFIG_ATTR_END; i++)
	{
		if (i >= ATTRIBUTE_COUNT_MAX)
		{
			OSK_PRINT_WARN(OSK_BASE_CORE, "More than ATTRIBUTE_COUNT_MAX=%d configuration attributes defined. Is attribute list properly terminated?",
					ATTRIBUTE_COUNT_MAX);
			return MALI_FALSE;
		}

		switch (attributes[i].id)
		{
			case KBASE_CONFIG_ATTR_MEMORY_RESOURCE:
				if (MALI_FALSE == kbasep_validate_memory_resource((kbase_memory_resource *)attributes[i].data))
				{
					OSK_PRINT_WARN(OSK_BASE_CORE, "Invalid memory region found in configuration");
					return MALI_FALSE;
				}
				break;

			case KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_MAX:
				/* Some shared memory is required for GPU page tables, see MIDBASE-1534 */
				if ( 0 == attributes[i].data )
				{
					OSK_PRINT_WARN(OSK_BASE_CORE, "Maximum OS Shared Memory Maximum is set to 0 which is not supported");
					return MALI_FALSE;
				}
				break;

			case KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_PERF_GPU:
				if (MALI_FALSE == kbasep_validate_memory_performance((kbase_memory_performance)attributes[i].data))
				{
					OSK_PRINT_WARN(OSK_BASE_CORE, "Shared OS memory GPU performance attribute has invalid value: %d",
							(kbase_memory_performance)attributes[i].data);
					return MALI_FALSE;
				}
				break;

			case KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT:
				/* any value is allowed */
				break;
#ifdef CONFIG_UMP
			case KBASE_CONFIG_ATTR_UMP_DEVICE:
				if (MALI_FALSE == kbasep_validate_ump_device(attributes[i].data))
				{
					OSK_PRINT_WARN(OSK_BASE_CORE, "Unknown UMP device found in configuration: %d",
							(int)attributes[i].data);
					return MALI_FALSE;
				}
				break;
#endif /* CONFIG_UMP */

			case KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN:
				had_gpu_freq_min = MALI_TRUE;
				if (MALI_FALSE == kbasep_validate_gpu_clock_freq(kbdev, attributes))
				{
					/* Warning message handled by kbasep_validate_gpu_clock_freq() */
					return MALI_FALSE;
				}
				break;

			case KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX:
				had_gpu_freq_max = MALI_TRUE;
				if (MALI_FALSE == kbasep_validate_gpu_clock_freq(kbdev, attributes))
				{
					/* Warning message handled by kbasep_validate_gpu_clock_freq() */
					return MALI_FALSE;
				}
				break;

				/* Only non-zero unsigned 32-bit values accepted */
			case KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS:
				#if CSTD_CPU_64BIT
						if ( attributes[i].data == 0u || (u64)attributes[i].data > (u64)U32_MAX )
				#else
						if ( attributes[i].data == 0u )
				#endif
						{
							OSK_PRINT_WARN(OSK_BASE_CORE, "Invalid Job Scheduling Configuration attribute for "
										   "KBASE_CONFIG_ATTR_JS_SCHEDULING_TICKS_NS: %d",
										   (int)attributes[i].data);
							return MALI_FALSE;
						}
				break;

				/* All these Job Scheduling attributes are FALLTHROUGH: only unsigned 32-bit values accepted */
			case KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS:
			case KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS:
			case KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS:
			case KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS:
			case KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS:
			case KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS:
			case KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS:
			case KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_INIT_SLICES:
			case KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_MIN_SLICES:
				#if CSTD_CPU_64BIT
					if ( (u64)attributes[i].data > (u64)U32_MAX )
					{
						OSK_PRINT_WARN(OSK_BASE_CORE, "Job Scheduling Configuration attribute exceeds 32-bits: "
									   "id==%d val==%d",
									   attributes[i].id, (int)attributes[i].data);
						return MALI_FALSE;
					}
				#endif
				break;

			case KBASE_CONFIG_ATTR_GPU_IRQ_THROTTLE_TIME_US:
				#if CSTD_CPU_64BIT
					if ( (u64)attributes[i].data > (u64)U32_MAX )
					{
						OSK_PRINT_WARN(OSK_BASE_CORE, "IRQ throttle time attribute exceeds 32-bits: "
						               "id==%d val==%d",
						               attributes[i].id, (int)attributes[i].data);
						return MALI_FALSE;
					}
				#endif
				break;

			case KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS:
				if (MALI_FALSE == kbasep_validate_pm_callback((kbase_pm_callback_conf*)attributes[i].data))
				{
					/* Warning message handled by kbasep_validate_pm_callback() */
					return MALI_FALSE;
				}
				break;

			case KBASE_CONFIG_ATTR_SECURE_BUT_LOSS_OF_PERFORMANCE:
				if ( attributes[i].data != MALI_TRUE && attributes[i].data != MALI_FALSE  )
				{
					OSK_PRINT_WARN(OSK_BASE_CORE,
								   "Value for KBASE_CONFIG_ATTR_SECURE_BUT_LOSS_OF_PERFORMANCE was not "
								   "MALI_TRUE or MALI_FALSE: %u",
								   (unsigned int)attributes[i].data);
					return MALI_FALSE;
				}
				break;

			case KBASE_CONFIG_ATTR_CPU_SPEED_FUNC:
				if (MALI_FALSE == kbasep_validate_cpu_speed_func((kbase_cpuprops_clock_speed_function)attributes[i].data))
				{
					OSK_PRINT_WARN(OSK_BASE_CORE, "Invalid function pointer in KBASE_CONFIG_ATTR_CPU_SPEED_FUNC");
					return MALI_FALSE;
				}
				break;

			case KBASE_CONFIG_ATTR_GPU_SPEED_FUNC:
				if (0 == attributes[i].data)
				{
					OSK_PRINT_WARN(OSK_BASE_CORE, "Invalid function pointer in KBASE_CONFIG_ATTR_GPU_SPEED_FUNC");
					return MALI_FALSE;
				}
				break;

			case KBASE_CONFIG_ATTR_PLATFORM_FUNCS:
				/* any value is allowed */
				break;

			default:
				OSK_PRINT_WARN(OSK_BASE_CORE, "Invalid attribute found in configuration: %d", attributes[i].id);
				return MALI_FALSE;
		}
	}

	if(!had_gpu_freq_min)
	{
		OSK_PRINT_WARN(OSK_BASE_CORE, "Configuration does not include mandatory attribute KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN");
		return MALI_FALSE;
	}

	if(!had_gpu_freq_max)
	{
		OSK_PRINT_WARN(OSK_BASE_CORE, "Configuration does not include mandatory attribute KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX");
		return MALI_FALSE;
	}

	return MALI_TRUE;
}

