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



#include <linux/ioport.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_defs.h>
#include <kbase/src/linux/mali_kbase_config_linux.h>
#ifdef CONFIG_UMP
#include <linux/ump-common.h>
#endif /* CONFIG_UMP */

#include "mali_kbase_cpu_vexpress.h"

/* Versatile Express (VE) configuration defaults shared between config_attributes[]
 * and config_attributes_hw_issue_8408[]. Settings are not shared for
 * JS_HARD_STOP_TICKS_SS and JS_RESET_TICKS_SS.
 */
#define KBASE_VE_MEMORY_PER_PROCESS_LIMIT       512 * 1024 * 1024UL /* 512MB */
#define KBASE_VE_MEMORY_OS_SHARED_MAX           768 * 1024 * 1024UL /* 768MB */
#define KBASE_VE_MEMORY_OS_SHARED_PERF_GPU      KBASE_MEM_PERF_SLOW
#define KBASE_VE_GPU_FREQ_KHZ_MAX               5000
#define KBASE_VE_GPU_FREQ_KHZ_MIN               5000
#ifdef CONFIG_UMP
#define KBASE_VE_UMP_DEVICE                     UMP_DEVICE_Z_SHIFT
#endif /* CONFIG_UMP */

#define KBASE_VE_JS_SCHEDULING_TICK_NS_DEBUG    15000000u   /* 15ms, an agressive tick for testing purposes. This will reduce performance significantly */
#define KBASE_VE_JS_SOFT_STOP_TICKS_DEBUG       1           /* between 15ms and 30ms before soft-stop a job */
#define KBASE_VE_JS_HARD_STOP_TICKS_SS_DEBUG    333         /* 5s before hard-stop */
#define KBASE_VE_JS_HARD_STOP_TICKS_SS_8401_DEBUG 2000      /* 30s before hard-stop, for a certain GLES2 test at 128x128 (bound by combined vertex+tiler job) - for issue 8401 */
#define KBASE_VE_JS_HARD_STOP_TICKS_NSS_DEBUG   100000      /* 1500s (25mins) before NSS hard-stop */
#define KBASE_VE_JS_RESET_TICKS_SS_DEBUG        500         /* 45s before resetting GPU, for a certain GLES2 test at 128x128 (bound by combined vertex+tiler job) */
#define KBASE_VE_JS_RESET_TICKS_SS_8401_DEBUG   3000        /* 7.5s before resetting GPU - for issue 8401 */
#define KBASE_VE_JS_RESET_TICKS_NSS_DEBUG       100166      /* 1502s before resetting GPU */

#define KBASE_VE_JS_SCHEDULING_TICK_NS          2500000000u /* 2.5s */
#define KBASE_VE_JS_SOFT_STOP_TICKS             1           /* 2.5s before soft-stop a job */
#define KBASE_VE_JS_HARD_STOP_TICKS_SS          2           /* 5s before hard-stop */
#define KBASE_VE_JS_HARD_STOP_TICKS_SS_8401     12          /* 30s before hard-stop, for a certain GLES2 test at 128x128 (bound by combined vertex+tiler job) - for issue 8401 */
#define KBASE_VE_JS_HARD_STOP_TICKS_NSS         600         /* 1500s before NSS hard-stop */
#define KBASE_VE_JS_RESET_TICKS_SS              3           /* 7.5s before resetting GPU */
#define KBASE_VE_JS_RESET_TICKS_SS_8401         18          /* 45s before resetting GPU, for a certain GLES2 test at 128x128 (bound by combined vertex+tiler job) - for issue 8401 */
#define KBASE_VE_JS_RESET_TICKS_NSS             601         /* 1502s before resetting GPU */

#define KBASE_VE_JS_RESET_TIMEOUT_MS            3000        /* 3s before cancelling stuck jobs */
#define KBASE_VE_JS_CTX_TIMESLICE_NS            1000000     /* 1ms - an agressive timeslice for testing purposes (causes lots of scheduling out for >4 ctxs) */
#define KBASE_VE_SECURE_BUT_LOSS_OF_PERFORMANCE	(uintptr_t)MALI_FALSE /* By default we prefer performance over security on r0p0-15dev0 and KBASE_CONFIG_ATTR_ earlier */
#define KBASE_VE_POWER_MANAGEMENT_CALLBACKS     (uintptr_t)&pm_callbacks
#define KBASE_VE_MEMORY_RESOURCE_ZBT            (uintptr_t)&lt_zbt
#define KBASE_VE_MEMORY_RESOURCE_DDR            (uintptr_t)&lt_ddr
#define KBASE_VE_CPU_SPEED_FUNC                 (uintptr_t)&kbase_get_vexpress_cpu_clock_speed

/* Set this to 1 to enable dedicated memory banks */
#define T6F1_ZBT_DDR_ENABLED 0
#define HARD_RESET_AT_POWER_OFF 0

static kbase_io_resources io_resources =
{
	.job_irq_number   = 68,
	.mmu_irq_number   = 69,
	.gpu_irq_number   = 70,
	.io_memory_region =
	{
		.start = 0xFC010000,
		.end   = 0xFC010000 + (4096 * 5) - 1
	}
};

#if T6F1_ZBT_DDR_ENABLED

static kbase_attribute lt_zbt_attrs[] =
{
	{
		KBASE_MEM_ATTR_PERF_CPU,
		KBASE_MEM_PERF_SLOW
	},
	{
		KBASE_MEM_ATTR_END,
		0
	}
};

static kbase_memory_resource lt_zbt =
{
	.base = 0xFD000000,
	.size = 16 * 1024 * 1024UL /* 16MB */,
	.attributes = lt_zbt_attrs,
	.name = "T604 ZBT memory"
};


static kbase_attribute lt_ddr_attrs[] =
{
	{
		KBASE_MEM_ATTR_PERF_CPU,
		KBASE_MEM_PERF_SLOW
	},
	{
		KBASE_MEM_ATTR_END,
		0
	}
};

static kbase_memory_resource lt_ddr =
{
	.base = 0xE0000000,
	.size = 256 * 1024 * 1024UL /* 256MB */,
	.attributes = lt_ddr_attrs,
	.name = "T604 DDR memory"
};

#endif /* T6F1_ZBT_DDR_ENABLED */

static int pm_callback_power_on(kbase_device *kbdev)
{
	/* Nothing is needed on VExpress, but we may have destroyed GPU state (if the below HARD_RESET code is active) */
	return 1;
}

static void pm_callback_power_off(kbase_device *kbdev)
{
#if HARD_RESET_AT_POWER_OFF
	/* Cause a GPU hard reset to test whether we have actually idled the GPU
	 * and that we properly reconfigure the GPU on power up.
	 * Usually this would be dangerous, but if the GPU is working correctly it should
	 * be completely safe as the GPU should not be active at this point.
	 * However this is disabled normally because it will most likely interfere with
	 * bus logging etc.
	 */
	KBASE_TRACE_ADD( kbdev, CORE_GPU_HARD_RESET, NULL, NULL, 0u, 0 );
	kbase_os_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_HARD_RESET);
#endif
}

static kbase_pm_callback_conf pm_callbacks =
{
	.power_on_callback = pm_callback_power_on,
	.power_off_callback = pm_callback_power_off
};

/* Please keep table config_attributes in sync with config_attributes_hw_issue_8408 */
static kbase_attribute config_attributes[] =
{
	{
		KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT,
		KBASE_VE_MEMORY_PER_PROCESS_LIMIT
	},
#ifdef CONFIG_UMP
	{
		KBASE_CONFIG_ATTR_UMP_DEVICE,
		KBASE_VE_UMP_DEVICE
	},
#endif /* CONFIG_UMP */
	{
		KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_MAX,
		KBASE_VE_MEMORY_OS_SHARED_MAX
	},

	{
		KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_PERF_GPU,
		KBASE_VE_MEMORY_OS_SHARED_PERF_GPU
	},

#if T6F1_ZBT_DDR_ENABLED
	{
		KBASE_CONFIG_ATTR_MEMORY_RESOURCE,
		KBASE_VE_MEMORY_RESOURCE_ZBT
	},

	{
		KBASE_CONFIG_ATTR_MEMORY_RESOURCE,
		KBASE_VE_MEMORY_RESOURCE_DDR
	},
#endif /* T6F1_ZBT_DDR_ENABLED */

	{
		KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX,
		KBASE_VE_GPU_FREQ_KHZ_MAX
	},

	{
		KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN,
		KBASE_VE_GPU_FREQ_KHZ_MIN
	},

#ifdef CONFIG_MALI_DEBUG
/* Use more aggressive scheduling timeouts in debug builds for testing purposes */
	{
		KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS,
		KBASE_VE_JS_SCHEDULING_TICK_NS_DEBUG
	},

	{
		KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS,
		KBASE_VE_JS_SOFT_STOP_TICKS_DEBUG
	},

	{
		KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS,
		KBASE_VE_JS_HARD_STOP_TICKS_SS_DEBUG
	},

	{
		KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS,
		KBASE_VE_JS_HARD_STOP_TICKS_NSS_DEBUG
	},

	{
		KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS,
		KBASE_VE_JS_RESET_TICKS_SS_DEBUG
	},

	{
		KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS,
		KBASE_VE_JS_RESET_TICKS_NSS_DEBUG
	},
#else /* CONFIG_MALI_DEBUG */
/* In release builds same as the defaults but scaled for 5MHz FPGA */
	{
		KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS,
		KBASE_VE_JS_SCHEDULING_TICK_NS
	},

	{
		KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS,
		KBASE_VE_JS_SOFT_STOP_TICKS
	},

	{
		KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS,
		KBASE_VE_JS_HARD_STOP_TICKS_SS
	},

	{
		KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS,
		KBASE_VE_JS_HARD_STOP_TICKS_NSS
	},

	{
		KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS,
		KBASE_VE_JS_RESET_TICKS_SS
	},

	{
		KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS,
		KBASE_VE_JS_RESET_TICKS_NSS
	},
#endif /* CONFIG_MALI_DEBUG */
	{
		KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS,
		KBASE_VE_JS_RESET_TIMEOUT_MS
	},

	{
		KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS,
		KBASE_VE_JS_CTX_TIMESLICE_NS
	},

	{
		KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS,
		KBASE_VE_POWER_MANAGEMENT_CALLBACKS
	},

	{
		KBASE_CONFIG_ATTR_CPU_SPEED_FUNC,
		KBASE_VE_CPU_SPEED_FUNC
	},

	{
		KBASE_CONFIG_ATTR_SECURE_BUT_LOSS_OF_PERFORMANCE,
		KBASE_VE_SECURE_BUT_LOSS_OF_PERFORMANCE
	},

	{
		KBASE_CONFIG_ATTR_GPU_IRQ_THROTTLE_TIME_US,
		20
	},

	{
		KBASE_CONFIG_ATTR_END,
		0
	}
};

/* as config_attributes array above except with different settings for
 * JS_HARD_STOP_TICKS_SS, JS_RESET_TICKS_SS that
 * are needed for BASE_HW_ISSUE_8408.
 */
kbase_attribute config_attributes_hw_issue_8408[] =
{
	{
		KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT,
		KBASE_VE_MEMORY_PER_PROCESS_LIMIT
	},
#ifdef CONFIG_UMP
	{
		KBASE_CONFIG_ATTR_UMP_DEVICE,
		KBASE_VE_UMP_DEVICE
	},
#endif /* CONFIG_UMP */
	{
		KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_MAX,
		KBASE_VE_MEMORY_OS_SHARED_MAX
	},

	{
		KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_PERF_GPU,
		KBASE_VE_MEMORY_OS_SHARED_PERF_GPU
	},

#if T6F1_ZBT_DDR_ENABLED
	{
		KBASE_CONFIG_ATTR_MEMORY_RESOURCE,
		KBASE_VE_MEMORY_RESOURCE_ZBT
	},

	{
		KBASE_CONFIG_ATTR_MEMORY_RESOURCE,
		KBASE_VE_MEMORY_RESOURCE_DDR
	},
#endif /* T6F1_ZBT_DDR_ENABLED */

	{
		KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX,
		KBASE_VE_GPU_FREQ_KHZ_MAX
	},

	{
		KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN,
		KBASE_VE_GPU_FREQ_KHZ_MIN
	},

#ifdef CONFIG_MALI_DEBUG
/* Use more aggressive scheduling timeouts in debug builds for testing purposes */
	{
		KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS,
		KBASE_VE_JS_SCHEDULING_TICK_NS_DEBUG
	},

	{
		KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS,
		KBASE_VE_JS_SOFT_STOP_TICKS_DEBUG
	},

	{
		KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS,
		KBASE_VE_JS_HARD_STOP_TICKS_SS_8401_DEBUG
	},

	{
		KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS,
		KBASE_VE_JS_HARD_STOP_TICKS_NSS_DEBUG
	},

	{
		KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS,
		KBASE_VE_JS_RESET_TICKS_SS_8401_DEBUG
	},

	{
		KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS,
		KBASE_VE_JS_RESET_TICKS_NSS_DEBUG
	},
#else /* CONFIG_MALI_DEBUG */
/* In release builds same as the defaults but scaled for 5MHz FPGA */
	{
		KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS,
		KBASE_VE_JS_SCHEDULING_TICK_NS
	},

	{
		KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS,
		KBASE_VE_JS_SOFT_STOP_TICKS
	},

	{
		KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS,
		KBASE_VE_JS_HARD_STOP_TICKS_SS_8401
	},

	{
		KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS,
		KBASE_VE_JS_HARD_STOP_TICKS_NSS
	},

	{
		KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS,
		KBASE_VE_JS_RESET_TICKS_SS_8401
	},

	{
		KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS,
		KBASE_VE_JS_RESET_TICKS_NSS
	},
#endif /* CONFIG_MALI_DEBUG */
	{
		KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS,
		KBASE_VE_JS_RESET_TIMEOUT_MS
	},

	{
		KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS,
		KBASE_VE_JS_CTX_TIMESLICE_NS
	},

	{
		KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS,
		KBASE_VE_POWER_MANAGEMENT_CALLBACKS
	},

	{
		KBASE_CONFIG_ATTR_CPU_SPEED_FUNC,
		KBASE_VE_CPU_SPEED_FUNC
	},

	{
		KBASE_CONFIG_ATTR_SECURE_BUT_LOSS_OF_PERFORMANCE,
		KBASE_VE_SECURE_BUT_LOSS_OF_PERFORMANCE
	},

	{
		KBASE_CONFIG_ATTR_END,
		0
	}
};

kbase_platform_config platform_config =
{
		.attributes                = config_attributes,
		.io_resources              = &io_resources
};

