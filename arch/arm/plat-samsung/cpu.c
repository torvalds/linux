/* linux/arch/arm/plat-samsung/cpu.c
 *
 * Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung CPU Support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/system.h>

#include <mach/map.h>
#include <plat/cpu.h>

unsigned long samsung_cpu_id;
static unsigned int samsung_cpu_rev;

unsigned int samsung_rev(void)
{
	return samsung_cpu_rev;
}
EXPORT_SYMBOL(samsung_rev);

void __init s3c24xx_init_cpu(void)
{
	/* nothing here yet */

	samsung_cpu_rev = 0;
}

void __init s3c64xx_init_cpu(void)
{
	samsung_cpu_id = __raw_readl(S3C_VA_SYS + 0x118);
	if (!samsung_cpu_id) {
		/*
		 * S3C6400 has the ID register in a different place,
		 * and needs a write before it can be read.
		 */
		__raw_writel(0x0, S3C_VA_SYS + 0xA1C);
		samsung_cpu_id = __raw_readl(S3C_VA_SYS + 0xA1C);
	}

	samsung_cpu_rev = 0;
}

void __init s5p_init_cpu(void __iomem *cpuid_addr)
{
	samsung_cpu_id = __raw_readl(cpuid_addr);
	samsung_cpu_rev = samsung_cpu_id & 0xFF;
}
