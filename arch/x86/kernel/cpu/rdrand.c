/*
 * This file is part of the Linux kernel.
 *
 * Copyright (c) 2011, Intel Corporation
 * Authors: Fenghua Yu <fenghua.yu@intel.com>,
 *          H. Peter Anvin <hpa@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <asm/processor.h>
#include <asm/archrandom.h>
#include <asm/sections.h>

static int __init x86_rdrand_setup(char *s)
{
	setup_clear_cpu_cap(X86_FEATURE_RDRAND);
	setup_clear_cpu_cap(X86_FEATURE_RDSEED);
	return 1;
}
__setup("nordrand", x86_rdrand_setup);

/*
 * RDRAND has Built-In-Self-Test (BIST) that runs on every invocation.
 * Run the instruction a few times as a sanity check.
 * If it fails, it is simple to disable RDRAND here.
 */
#define SANITY_CHECK_LOOPS 8

#ifdef CONFIG_ARCH_RANDOM
void x86_init_rdrand(struct cpuinfo_x86 *c)
{
	unsigned long tmp;
	int i;

	if (!cpu_has(c, X86_FEATURE_RDRAND))
		return;

	for (i = 0; i < SANITY_CHECK_LOOPS; i++) {
		if (!rdrand_long(&tmp)) {
			clear_cpu_cap(c, X86_FEATURE_RDRAND);
			pr_warn_once("rdrand: disabled\n");
			return;
		}
	}
}
#endif
