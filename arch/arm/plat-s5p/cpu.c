/* linux/arch/arm/plat-s5p/cpu.c
 *
 * Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P CPU Support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

#include <plat/cpu.h>
#include <plat/s5p6440.h>
#include <plat/s5p6450.h>
#include <plat/s5pc100.h>
#include <plat/s5pv210.h>
#include <plat/exynos4.h>

/* table of supported CPUs */

static const char name_s5p6440[] = "S5P6440";
static const char name_s5p6450[] = "S5P6450";
static const char name_s5pc100[] = "S5PC100";
static const char name_s5pv210[] = "S5PV210/S5PC110";
static const char name_exynos4210[] = "EXYNOS4210";
static const char name_exynos4212[] = "EXYNOS4212";
static const char name_exynos4412[] = "EXYNOS4412";

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode		= S5P6440_CPU_ID,
		.idmask		= S5P64XX_CPU_MASK,
		.map_io		= s5p6440_map_io,
		.init_clocks	= s5p6440_init_clocks,
		.init_uarts	= s5p6440_init_uarts,
		.init		= s5p64x0_init,
		.name		= name_s5p6440,
	}, {
		.idcode		= S5P6450_CPU_ID,
		.idmask		= S5P64XX_CPU_MASK,
		.map_io		= s5p6450_map_io,
		.init_clocks	= s5p6450_init_clocks,
		.init_uarts	= s5p6450_init_uarts,
		.init		= s5p64x0_init,
		.name		= name_s5p6450,
	}, {
		.idcode		= S5PC100_CPU_ID,
		.idmask		= S5PC100_CPU_MASK,
		.map_io		= s5pc100_map_io,
		.init_clocks	= s5pc100_init_clocks,
		.init_uarts	= s5pc100_init_uarts,
		.init		= s5pc100_init,
		.name		= name_s5pc100,
	}, {
		.idcode		= S5PV210_CPU_ID,
		.idmask		= S5PV210_CPU_MASK,
		.map_io		= s5pv210_map_io,
		.init_clocks	= s5pv210_init_clocks,
		.init_uarts	= s5pv210_init_uarts,
		.init		= s5pv210_init,
		.name		= name_s5pv210,
	}, {
		.idcode		= EXYNOS4210_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init_clocks	= exynos4_init_clocks,
		.init_uarts	= exynos4_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos4210,
	}, {
		.idcode		= EXYNOS4212_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init_clocks	= exynos4_init_clocks,
		.init_uarts	= exynos4_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos4212,
	}, {
		.idcode		= EXYNOS4412_CPU_ID,
		.idmask		= EXYNOS4_CPU_MASK,
		.map_io		= exynos4_map_io,
		.init_clocks	= exynos4_init_clocks,
		.init_uarts	= exynos4_init_uarts,
		.init		= exynos_init,
		.name		= name_exynos4412,
	},
};

/* minimal IO mapping */

static struct map_desc s5p_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_CHIPID,
		.pfn		= __phys_to_pfn(S5P_PA_CHIPID),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_SYS,
		.pfn		= __phys_to_pfn(S5P_PA_SYSCON),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_TIMER,
		.pfn		= __phys_to_pfn(S5P_PA_TIMER),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_WATCHDOG,
		.pfn		= __phys_to_pfn(S3C_PA_WDT),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SROMC,
		.pfn		= __phys_to_pfn(S5P_PA_SROMC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

/* read cpu identification code */

void __init s5p_init_io(struct map_desc *mach_desc,
			int size, void __iomem *cpuid_addr)
{
	/* initialize the io descriptors we need for initialization */
	iotable_init(s5p_iodesc, ARRAY_SIZE(s5p_iodesc));
	if (mach_desc)
		iotable_init(mach_desc, size);

	/* detect cpu id and rev. */
	s5p_init_cpu(cpuid_addr);

	s3c_init_cpu(samsung_cpu_id, cpu_ids, ARRAY_SIZE(cpu_ids));
}
