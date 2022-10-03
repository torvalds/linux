// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
//
// Samsung CPU Support

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include "map-base.h"
#include "cpu.h"

unsigned long samsung_cpu_id;

void __init s3c64xx_init_cpu(void)
{
	samsung_cpu_id = readl_relaxed(S3C_VA_SYS + 0x118);
	if (!samsung_cpu_id) {
		/*
		 * S3C6400 has the ID register in a different place,
		 * and needs a write before it can be read.
		 */
		writel_relaxed(0x0, S3C_VA_SYS + 0xA1C);
		samsung_cpu_id = readl_relaxed(S3C_VA_SYS + 0xA1C);
	}

	pr_info("Samsung CPU ID: 0x%08lx\n", samsung_cpu_id);
	pr_err("The platform is deprecated and scheduled for removal. Please reach to the maintainers of the platform and linux-samsung-soc@vger.kernel.org if you still use it.  Without such feedback, the platform will be removed after 2022.\n");
}
