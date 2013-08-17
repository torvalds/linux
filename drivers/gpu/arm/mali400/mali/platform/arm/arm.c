/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
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

static void mali_platform_device_release(struct device *device);
static u32 mali_read_phys(u32 phys_addr);
#if defined(CONFIG_ARCH_REALVIEW)
static void mali_write_phys(u32 phys_addr, u32 value);
#endif

#if defined(CONFIG_ARCH_VEXPRESS)

static struct resource mali_gpu_resources_m450_mp8[] =
{
	MALI_GPU_RESOURCES_MALI450_MP8_PMU(0xFC040000, -1, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 68)
};

#elif defined(CONFIG_ARCH_REALVIEW)

static struct resource mali_gpu_resources_m200[] =
{
	MALI_GPU_RESOURCES_MALI200(0xC0000000, -1, -1, -1)
};

static struct resource mali_gpu_resources_m300[] =
{
	MALI_GPU_RESOURCES_MALI300_PMU(0xC0000000, -1, -1, -1, -1)
};

static struct resource mali_gpu_resources_m400_mp1[] =
{
	MALI_GPU_RESOURCES_MALI400_MP1_PMU(0xC0000000, -1, -1, -1, -1)
};

static struct resource mali_gpu_resources_m400_mp2[] =
{
	MALI_GPU_RESOURCES_MALI400_MP2_PMU(0xC0000000, -1, -1, -1, -1, -1, -1)
};

#endif

static struct platform_device mali_gpu_device =
{
	.name = MALI_GPU_NAME_UTGARD,
	.id = 0,
	.dev.release = mali_platform_device_release,
};

static struct mali_gpu_device_data mali_gpu_data =
{
#if defined(CONFIG_ARCH_VEXPRESS)
	.shared_mem_size =256 * 1024 * 1024, /* 256MB */
#elif defined(CONFIG_ARCH_REALVIEW)
	.dedicated_mem_start = 0x80000000, /* Physical start address (use 0xD0000000 for old indirect setup) */
	.dedicated_mem_size = 0x10000000, /* 256MB */
#endif
	.fb_start = 0xe0000000,
	.fb_size = 0x01000000,
};

int mali_platform_device_register(void)
{
	int err = -1;
#if defined(CONFIG_ARCH_REALVIEW)
	u32 m400_gp_version;
#endif

	MALI_DEBUG_PRINT(4, ("mali_platform_device_register() called\n"));

	/* Detect present Mali GPU and connect the correct resources to the device */
#if defined(CONFIG_ARCH_VEXPRESS)

	if (mali_read_phys(0xFC020000) == 0x00010100)
	{
		MALI_DEBUG_PRINT(4, ("Registering Mali-450 MP8 device\n"));
		err = platform_device_add_resources(&mali_gpu_device, mali_gpu_resources_m450_mp8, sizeof(mali_gpu_resources_m450_mp8) / sizeof(mali_gpu_resources_m450_mp8[0]));
	}

#elif defined(CONFIG_ARCH_REALVIEW)

	m400_gp_version = mali_read_phys(0xC000006C);
	if (m400_gp_version == 0x00000000 && (mali_read_phys(0xC000206c) & 0xFFFF0000) == 0x0A070000)
	{
		MALI_DEBUG_PRINT(4, ("Registering Mali-200 device\n"));
		err = platform_device_add_resources(&mali_gpu_device, mali_gpu_resources_m200, sizeof(mali_gpu_resources_m200) / sizeof(mali_gpu_resources_m200[0]));
		mali_write_phys(0xC0010020, 0xA); /* Enable direct memory mapping for FPGA */
	}
	else if ((m400_gp_version & 0xFFFF0000) == 0x0C070000)
	{
		MALI_DEBUG_PRINT(4, ("Registering Mali-300 device\n"));
		err = platform_device_add_resources(&mali_gpu_device, mali_gpu_resources_m300, sizeof(mali_gpu_resources_m300) / sizeof(mali_gpu_resources_m300[0]));
		mali_write_phys(0xC0010020, 0xA); /* Enable direct memory mapping for FPGA */
	}
	else if ((m400_gp_version & 0xFFFF0000) == 0x0B070000)
	{
		u32 fpga_fw_version = mali_read_phys(0xC0010000);
		if (fpga_fw_version == 0x130C008F || fpga_fw_version == 0x110C008F)
		{
			/* Mali-400 MP1 r1p0 or r1p1 */
			MALI_DEBUG_PRINT(4, ("Registering Mali-400 MP1 device\n"));
			err = platform_device_add_resources(&mali_gpu_device, mali_gpu_resources_m400_mp1, sizeof(mali_gpu_resources_m400_mp1) / sizeof(mali_gpu_resources_m400_mp1[0]));
			mali_write_phys(0xC0010020, 0xA); /* Enable direct memory mapping for FPGA */
		}
		else if (fpga_fw_version == 0x130C000F)
		{
			/* Mali-400 MP2 r1p1 */
			MALI_DEBUG_PRINT(4, ("Registering Mali-400 MP2 device\n"));
			err = platform_device_add_resources(&mali_gpu_device, mali_gpu_resources_m400_mp2, sizeof(mali_gpu_resources_m400_mp2) / sizeof(mali_gpu_resources_m400_mp2[0]));
			mali_write_phys(0xC0010020, 0xA); /* Enable direct memory mapping for FPGA */
		}
	}

#endif

	if (0 == err)
	{
		err = platform_device_add_data(&mali_gpu_device, &mali_gpu_data, sizeof(mali_gpu_data));
		if (0 == err)
		{
			/* Register the platform device */
			err = platform_device_register(&mali_gpu_device);
			if (0 == err)
			{
#ifdef CONFIG_PM_RUNTIME
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
				pm_runtime_set_autosuspend_delay(&(mali_gpu_device.dev), 1000);
				pm_runtime_use_autosuspend(&(mali_gpu_device.dev));
#endif
				pm_runtime_enable(&(mali_gpu_device.dev));
#endif

				return 0;
			}
		}

		platform_device_unregister(&mali_gpu_device);
	}

	return err;
}

void mali_platform_device_unregister(void)
{
	MALI_DEBUG_PRINT(4, ("mali_platform_device_unregister() called\n"));

	platform_device_unregister(&mali_gpu_device);

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
	if (NULL != mem_mapped)
	{
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
	if (NULL != mem_mapped)
	{
		iowrite32(value, ((u8*)mem_mapped) + phys_offset);
		iounmap(mem_mapped);
	}
}
#endif
