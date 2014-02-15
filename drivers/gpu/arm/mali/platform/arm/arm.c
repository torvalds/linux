/*
 * Copyright (C) 2010, 2012-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for:
 * - Realview Versatile platforms with ARM11 Mpcore and virtex 5.
 * - Versatile Express platforms with ARM Cortex-A9 and virtex 6.
 */
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/pm.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <asm/io.h>
#include <linux/mali/mali_utgard.h>
#include "mali_kernel_common.h"
#include <linux/dma-mapping.h>
#include <linux/moduleparam.h>

#include "arm_core_scaling.h"
#include "mali_pp_scheduler.h"

static void mali_platform_device_release(struct device *device);
static u32 mali_read_phys(u32 phys_addr);
#if defined(CONFIG_ARCH_REALVIEW)
static void mali_write_phys(u32 phys_addr, u32 value);
#endif

static int mali_core_scaling_enable = 1;

void mali_gpu_utilization_callback(struct mali_gpu_utilization_data *data);

#if defined(CONFIG_ARCH_VEXPRESS)

static struct resource mali_gpu_resources_m450_mp8[] = {
	MALI_GPU_RESOURCES_MALI450_MP8_PMU(0xFC040000, -1, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 68)
};

#elif defined(CONFIG_ARCH_REALVIEW)

static struct resource mali_gpu_resources_m300[] = {
	MALI_GPU_RESOURCES_MALI300_PMU(0xC0000000, -1, -1, -1, -1)
};

static struct resource mali_gpu_resources_m400_mp1[] = {
	MALI_GPU_RESOURCES_MALI400_MP1_PMU(0xC0000000, -1, -1, -1, -1)
};

static struct resource mali_gpu_resources_m400_mp2[] = {
	MALI_GPU_RESOURCES_MALI400_MP2_PMU(0xC0000000, -1, -1, -1, -1, -1, -1)
};

#endif

static struct mali_gpu_device_data mali_gpu_data = {
#if defined(CONFIG_ARCH_VEXPRESS)
	.shared_mem_size =256 * 1024 * 1024, /* 256MB */
#elif defined(CONFIG_ARCH_REALVIEW)
	.dedicated_mem_start = 0x80000000, /* Physical start address (use 0xD0000000 for old indirect setup) */
	.dedicated_mem_size = 0x10000000, /* 256MB */
#endif
	.fb_start = 0xe0000000,
	.fb_size = 0x01000000,
	.max_job_runtime = 60000, /* 60 seconds */
	.utilization_interval = 1000, /* 1000ms */
	.utilization_callback = mali_gpu_utilization_callback,
	.pmu_switch_delay = 0xFF, /* do not have to be this high on FPGA, but it is good for testing to have a delay */
	.pmu_domain_config = {0x1, 0x2, 0x4, 0x4, 0x4, 0x8, 0x8, 0x8, 0x8, 0x1, 0x2, 0x8},
};

static struct platform_device mali_gpu_device = {
	.name = MALI_GPU_NAME_UTGARD,
	.id = 0,
	.dev.release = mali_platform_device_release,
	.dev.coherent_dma_mask = DMA_BIT_MASK(32),

	.dev.platform_data = &mali_gpu_data,
};

int mali_platform_device_register(void)
{
	int err = -1;
	int num_pp_cores = 0;
#if defined(CONFIG_ARCH_REALVIEW)
	u32 m400_gp_version;
#endif

	MALI_DEBUG_PRINT(4, ("mali_platform_device_register() called\n"));

	/* Detect present Mali GPU and connect the correct resources to the device */
#if defined(CONFIG_ARCH_VEXPRESS)

	if (mali_read_phys(0xFC020000) == 0x00010100) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-450 MP8 device\n"));
		num_pp_cores = 8;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m450_mp8);
		mali_gpu_device.resource = mali_gpu_resources_m450_mp8;
	}

#elif defined(CONFIG_ARCH_REALVIEW)

	m400_gp_version = mali_read_phys(0xC000006C);
	if ((m400_gp_version & 0xFFFF0000) == 0x0C070000) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-300 device\n"));
		num_pp_cores = 1;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m300);
		mali_gpu_device.resource = mali_gpu_resources_m300;
		mali_write_phys(0xC0010020, 0xA); /* Enable direct memory mapping for FPGA */
	} else if ((m400_gp_version & 0xFFFF0000) == 0x0B070000) {
		u32 fpga_fw_version = mali_read_phys(0xC0010000);
		if (fpga_fw_version == 0x130C008F || fpga_fw_version == 0x110C008F) {
			/* Mali-400 MP1 r1p0 or r1p1 */
			MALI_DEBUG_PRINT(4, ("Registering Mali-400 MP1 device\n"));
			num_pp_cores = 1;
			mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m400_mp1);
			mali_gpu_device.resource = mali_gpu_resources_m400_mp1;
			mali_write_phys(0xC0010020, 0xA); /* Enable direct memory mapping for FPGA */
		} else if (fpga_fw_version == 0x130C000F) {
			/* Mali-400 MP2 r1p1 */
			MALI_DEBUG_PRINT(4, ("Registering Mali-400 MP2 device\n"));
			num_pp_cores = 2;
			mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m400_mp2);
			mali_gpu_device.resource = mali_gpu_resources_m400_mp2;
			mali_write_phys(0xC0010020, 0xA); /* Enable direct memory mapping for FPGA */
		}
	}

#endif
	/* Register the platform device */
	err = platform_device_register(&mali_gpu_device);
	if (0 == err) {
#ifdef CONFIG_PM_RUNTIME
		pm_runtime_set_autosuspend_delay(&(mali_gpu_device.dev), 1000);
		pm_runtime_use_autosuspend(&(mali_gpu_device.dev));
		pm_runtime_enable(&(mali_gpu_device.dev));
#endif
		MALI_DEBUG_ASSERT(0 < num_pp_cores);
		mali_core_scaling_init(num_pp_cores);

		return 0;
	}

	return err;
}

void mali_platform_device_unregister(void)
{
	MALI_DEBUG_PRINT(4, ("mali_platform_device_unregister() called\n"));

	mali_core_scaling_term();
	platform_device_unregister(&mali_gpu_device);

	platform_device_put(&mali_gpu_device);

#if defined(CONFIG_ARCH_REALVIEW)
	mali_write_phys(0xC0010020, 0x9); /* Restore default (legacy) memory mapping */
#endif
}

static void mali_platform_device_release(struct device *device)
{
	MALI_DEBUG_PRINT(4, ("mali_platform_device_release() called\n"));
}

static u32 mali_read_phys(u32 phys_addr)
{
	u32 phys_addr_page = phys_addr & 0xFFFFE000;
	u32 phys_offset    = phys_addr & 0x00001FFF;
	u32 map_size       = phys_offset + sizeof(u32);
	u32 ret = 0xDEADBEEF;
	void *mem_mapped = ioremap_nocache(phys_addr_page, map_size);
	if (NULL != mem_mapped) {
		ret = (u32)ioread32(((u8*)mem_mapped) + phys_offset);
		iounmap(mem_mapped);
	}

	return ret;
}

#if defined(CONFIG_ARCH_REALVIEW)
static void mali_write_phys(u32 phys_addr, u32 value)
{
	u32 phys_addr_page = phys_addr & 0xFFFFE000;
	u32 phys_offset    = phys_addr & 0x00001FFF;
	u32 map_size       = phys_offset + sizeof(u32);
	void *mem_mapped = ioremap_nocache(phys_addr_page, map_size);
	if (NULL != mem_mapped) {
		iowrite32(value, ((u8*)mem_mapped) + phys_offset);
		iounmap(mem_mapped);
	}
}
#endif

static int param_set_core_scaling(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);

	if (1 == mali_core_scaling_enable) {
		mali_core_scaling_sync(mali_pp_scheduler_get_num_cores_enabled());
	}
	return ret;
}

static struct kernel_param_ops param_ops_core_scaling = {
	.set = param_set_core_scaling,
	.get = param_get_int,
};

module_param_cb(mali_core_scaling_enable, &param_ops_core_scaling, &mali_core_scaling_enable, 0644);
MODULE_PARM_DESC(mali_core_scaling_enable, "1 means to enable core scaling policy, 0 means to disable core scaling policy");

void mali_gpu_utilization_callback(struct mali_gpu_utilization_data *data)
{
	if (1 == mali_core_scaling_enable) {
		mali_core_scaling_update(data);
	}
}
