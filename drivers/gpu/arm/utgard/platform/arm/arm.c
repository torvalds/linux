/*
 * Copyright (C) 2010, 2012-2015 ARM Limited. All rights reserved.
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
#include "mali_executor.h"


static int mali_core_scaling_enable = 0;

void mali_gpu_utilization_callback(struct mali_gpu_utilization_data *data);
static u32 mali_read_phys(u32 phys_addr);
#if defined(CONFIG_ARCH_REALVIEW)
static void mali_write_phys(u32 phys_addr, u32 value);
#endif

#ifndef CONFIG_MALI_DT
static void mali_platform_device_release(struct device *device);

#if defined(CONFIG_ARCH_VEXPRESS)

#if defined(CONFIG_ARM64)
/* Juno + Mali-450 MP6 in V7 FPGA */
static struct resource mali_gpu_resources_m450_mp6[] = {
	MALI_GPU_RESOURCES_MALI450_MP6_PMU(0x6F040000, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200)
};

static struct resource mali_gpu_resources_m470_mp4[] = {
	MALI_GPU_RESOURCES_MALI470_MP4_PMU(0x6F040000, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200)
};

static struct resource mali_gpu_resources_m470_mp3[] = {
	MALI_GPU_RESOURCES_MALI470_MP3_PMU(0x6F040000, 200, 200, 200, 200, 200, 200, 200, 200, 200)
};

static struct resource mali_gpu_resources_m470_mp2[] = {
	MALI_GPU_RESOURCES_MALI470_MP2_PMU(0x6F040000, 200, 200, 200, 200, 200, 200, 200)
};

static struct resource mali_gpu_resources_m470_mp1[] = {
	MALI_GPU_RESOURCES_MALI470_MP1_PMU(0x6F040000, 200, 200, 200, 200, 200)
};

#else
static struct resource mali_gpu_resources_m450_mp8[] = {
	MALI_GPU_RESOURCES_MALI450_MP8_PMU(0xFC040000, -1, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 68)
};

static struct resource mali_gpu_resources_m450_mp6[] = {
	MALI_GPU_RESOURCES_MALI450_MP6_PMU(0xFC040000, -1, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 68)
};

static struct resource mali_gpu_resources_m450_mp4[] = {
	MALI_GPU_RESOURCES_MALI450_MP4_PMU(0xFC040000, -1, 70, 70, 70, 70, 70, 70, 70, 70, 70, 68)
};

static struct resource mali_gpu_resources_m470_mp4[] = {
	MALI_GPU_RESOURCES_MALI470_MP4_PMU(0xFC040000, -1, 70, 70, 70, 70, 70, 70, 70, 70, 70, 68)
};
#endif /* CONFIG_ARM64 */

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
#endif

static struct mali_gpu_device_data mali_gpu_data = {
#ifndef CONFIG_MALI_DT
	.pmu_switch_delay = 0xFF, /* do not have to be this high on FPGA, but it is good for testing to have a delay */
	.max_job_runtime = 60000, /* 60 seconds */
#if defined(CONFIG_ARCH_VEXPRESS)
	.shared_mem_size = 256 * 1024 * 1024, /* 256MB */
#endif
#endif

#if defined(CONFIG_ARCH_REALVIEW)
	.dedicated_mem_start = 0x80000000, /* Physical start address (use 0xD0000000 for old indirect setup) */
	.dedicated_mem_size = 0x10000000, /* 256MB */
#endif
#if defined(CONFIG_ARM64)
	/* Some framebuffer drivers get the framebuffer dynamically, such as through GEM,
	* in which the memory resource can't be predicted in advance.
	*/
	.fb_start = 0x0,
	.fb_size = 0xFFFFF000,
#else
	.fb_start = 0xe0000000,
	.fb_size = 0x01000000,
#endif
	.control_interval = 1000, /* 1000ms */
	.utilization_callback = mali_gpu_utilization_callback,
	.get_clock_info = NULL,
	.get_freq = NULL,
	.set_freq = NULL,
};

#ifndef CONFIG_MALI_DT
static struct platform_device mali_gpu_device = {
	.name = MALI_GPU_NAME_UTGARD,
	.id = 0,
	.dev.release = mali_platform_device_release,
	.dev.dma_mask = &mali_gpu_device.dev.coherent_dma_mask,
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

#if defined(CONFIG_ARM64)
	mali_gpu_device.dev.archdata.dma_ops = dma_ops;
	if ((mali_read_phys(0x6F000000) & 0x00600450) == 0x00600450) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-450 MP6 device\n"));
		num_pp_cores = 6;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m450_mp6);
		mali_gpu_device.resource = mali_gpu_resources_m450_mp6;
	} else if ((mali_read_phys(0x6F000000) & 0x00F00430) == 0x00400430) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-470 MP4 device\n"));
		num_pp_cores = 4;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m470_mp4);
		mali_gpu_device.resource = mali_gpu_resources_m470_mp4;
	} else if ((mali_read_phys(0x6F000000) & 0x00F00430) == 0x00300430) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-470 MP3 device\n"));
		num_pp_cores = 3;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m470_mp3);
		mali_gpu_device.resource = mali_gpu_resources_m470_mp3;
	} else if ((mali_read_phys(0x6F000000) & 0x00F00430) == 0x00200430) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-470 MP2 device\n"));
		num_pp_cores = 2;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m470_mp2);
		mali_gpu_device.resource = mali_gpu_resources_m470_mp2;
	} else if ((mali_read_phys(0x6F000000) & 0x00F00430) == 0x00100430) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-470 MP1 device\n"));
		num_pp_cores = 1;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m470_mp1);
		mali_gpu_device.resource = mali_gpu_resources_m470_mp1;
	}
#else
	if (mali_read_phys(0xFC000000) == 0x00000450) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-450 MP8 device\n"));
		num_pp_cores = 8;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m450_mp8);
		mali_gpu_device.resource = mali_gpu_resources_m450_mp8;
	} else if (mali_read_phys(0xFC000000) == 0x40600450) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-450 MP6 device\n"));
		num_pp_cores = 6;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m450_mp6);
		mali_gpu_device.resource = mali_gpu_resources_m450_mp6;
	} else if (mali_read_phys(0xFC000000) == 0x40400450) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-450 MP4 device\n"));
		num_pp_cores = 4;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m450_mp4);
		mali_gpu_device.resource = mali_gpu_resources_m450_mp4;
	} else if (mali_read_phys(0xFC000000) == 0xFFFFFFFF) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-470 MP4 device\n"));
		num_pp_cores = 4;
		mali_gpu_device.num_resources = ARRAY_SIZE(mali_gpu_resources_m470_mp4);
		mali_gpu_device.resource = mali_gpu_resources_m470_mp4;
	}
#endif /* CONFIG_ARM64 */

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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		pm_runtime_set_autosuspend_delay(&(mali_gpu_device.dev), 1000);
		pm_runtime_use_autosuspend(&(mali_gpu_device.dev));
#endif
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

#else /* CONFIG_MALI_DT */
int mali_platform_device_init(struct platform_device *device)
{
	int num_pp_cores = 0;
	int err = -1;
#if defined(CONFIG_ARCH_REALVIEW)
	u32 m400_gp_version;
#endif

	/* Detect present Mali GPU and connect the correct resources to the device */
#if defined(CONFIG_ARCH_VEXPRESS)

#if defined(CONFIG_ARM64)
	if ((mali_read_phys(0x6F000000) & 0x00600450) == 0x00600450) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-450 MP6 device\n"));
		num_pp_cores = 6;
	} else if ((mali_read_phys(0x6F000000) & 0x00F00430) == 0x00400430) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-470 MP4 device\n"));
		num_pp_cores = 4;
	} else if ((mali_read_phys(0x6F000000) & 0x00F00430) == 0x00300430) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-470 MP3 device\n"));
		num_pp_cores = 3;
	} else if ((mali_read_phys(0x6F000000) & 0x00F00430) == 0x00200430) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-470 MP2 device\n"));
		num_pp_cores = 2;
	} else if ((mali_read_phys(0x6F000000) & 0x00F00430) == 0x00100430) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-470 MP1 device\n"));
		num_pp_cores = 1;
	}
#else
	if (mali_read_phys(0xFC000000) == 0x00000450) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-450 MP8 device\n"));
		num_pp_cores = 8;
	} else if (mali_read_phys(0xFC000000) == 0x40400450) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-450 MP4 device\n"));
		num_pp_cores = 4;
	} else if (mali_read_phys(0xFC000000) == 0xFFFFFFFF) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-470 MP4 device\n"));
		num_pp_cores = 4;
	}
#endif

#elif defined(CONFIG_ARCH_REALVIEW)

	m400_gp_version = mali_read_phys(0xC000006C);
	if ((m400_gp_version & 0xFFFF0000) == 0x0C070000) {
		MALI_DEBUG_PRINT(4, ("Registering Mali-300 device\n"));
		num_pp_cores = 1;
		mali_write_phys(0xC0010020, 0xA); /* Enable direct memory mapping for FPGA */
	} else if ((m400_gp_version & 0xFFFF0000) == 0x0B070000) {
		u32 fpga_fw_version = mali_read_phys(0xC0010000);
		if (fpga_fw_version == 0x130C008F || fpga_fw_version == 0x110C008F) {
			/* Mali-400 MP1 r1p0 or r1p1 */
			MALI_DEBUG_PRINT(4, ("Registering Mali-400 MP1 device\n"));
			num_pp_cores = 1;
			mali_write_phys(0xC0010020, 0xA); /* Enable direct memory mapping for FPGA */
		} else if (fpga_fw_version == 0x130C000F) {
			/* Mali-400 MP2 r1p1 */
			MALI_DEBUG_PRINT(4, ("Registering Mali-400 MP2 device\n"));
			num_pp_cores = 2;
			mali_write_phys(0xC0010020, 0xA); /* Enable direct memory mapping for FPGA */
		}
	}
#endif

	/* After kernel 3.15 device tree will default set dev
	 * related parameters in of_platform_device_create_pdata.
	 * But kernel changes from version to version,
	 * For example 3.10 didn't include device->dev.dma_mask parameter setting,
	 * if we didn't include here will cause dma_mapping error,
	 * but in kernel 3.15 it include  device->dev.dma_mask parameter setting,
	 * so it's better to set must need paramter by DDK itself.
	 */
	if (!device->dev.dma_mask)
		device->dev.dma_mask = &device->dev.coherent_dma_mask;
	device->dev.archdata.dma_ops = dma_ops;

	err = platform_device_add_data(device, &mali_gpu_data, sizeof(mali_gpu_data));

	if (0 == err) {
#ifdef CONFIG_PM_RUNTIME
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		pm_runtime_set_autosuspend_delay(&(device->dev), 1000);
		pm_runtime_use_autosuspend(&(device->dev));
#endif
		pm_runtime_enable(&(device->dev));
#endif
		MALI_DEBUG_ASSERT(0 < num_pp_cores);
		mali_core_scaling_init(num_pp_cores);
	}

	return err;
}

int mali_platform_device_deinit(struct platform_device *device)
{
	MALI_IGNORE(device);

	MALI_DEBUG_PRINT(4, ("mali_platform_device_deinit() called\n"));

	mali_core_scaling_term();

#if defined(CONFIG_ARCH_REALVIEW)
	mali_write_phys(0xC0010020, 0x9); /* Restore default (legacy) memory mapping */
#endif

	return 0;
}

#endif /* CONFIG_MALI_DT */

static u32 mali_read_phys(u32 phys_addr)
{
	u32 phys_addr_page = phys_addr & 0xFFFFE000;
	u32 phys_offset    = phys_addr & 0x00001FFF;
	u32 map_size       = phys_offset + sizeof(u32);
	u32 ret = 0xDEADBEEF;
	void *mem_mapped = ioremap_nocache(phys_addr_page, map_size);
	if (NULL != mem_mapped) {
		ret = (u32)ioread32(((u8 *)mem_mapped) + phys_offset);
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
		iowrite32(value, ((u8 *)mem_mapped) + phys_offset);
		iounmap(mem_mapped);
	}
}
#endif

static int param_set_core_scaling(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);

	if (1 == mali_core_scaling_enable) {
		mali_core_scaling_sync(mali_executor_get_num_cores_enabled());
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
