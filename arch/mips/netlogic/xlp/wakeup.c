/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/threads.h>

#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/string.h>

#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/common.h>
#include <asm/netlogic/mips-extns.h>

#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/pic.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/sys.h>

unsigned long secondary_entry;
uint32_t nlm_coremask;
unsigned int nlm_threads_per_core;
unsigned int nlm_threadmode;

static void nlm_enable_secondary_cores(unsigned int cores_bitmap)
{
	uint32_t core, value, coremask;

	for (core = 1; core < 8; core++) {
		coremask = 1 << core;
		if ((cores_bitmap & coremask) == 0)
			continue;

		/* Enable CPU clock */
		value = nlm_read_sys_reg(nlm_sys_base, SYS_CORE_DFS_DIS_CTRL);
		value &= ~coremask;
		nlm_write_sys_reg(nlm_sys_base, SYS_CORE_DFS_DIS_CTRL, value);

		/* Remove CPU Reset */
		value = nlm_read_sys_reg(nlm_sys_base, SYS_CPU_RESET);
		value &= ~coremask;
		nlm_write_sys_reg(nlm_sys_base, SYS_CPU_RESET, value);

		/* Poll for CPU to mark itself coherent */
		do {
			value = nlm_read_sys_reg(nlm_sys_base,
			    SYS_CPU_NONCOHERENT_MODE);
		} while ((value & coremask) != 0);
	}
}


static void nlm_parse_cpumask(u32 cpu_mask)
{
	uint32_t core0_thr_mask, core_thr_mask;
	int i;

	core0_thr_mask = cpu_mask & 0xf;
	switch (core0_thr_mask) {
	case 1:
		nlm_threads_per_core = 1;
		nlm_threadmode = 0;
		break;
	case 3:
		nlm_threads_per_core = 2;
		nlm_threadmode = 2;
		break;
	case 0xf:
		nlm_threads_per_core = 4;
		nlm_threadmode = 3;
		break;
	default:
		goto unsupp;
	}

	/* Verify other cores CPU masks */
	nlm_coremask = 1;
	for (i = 1; i < 8; i++) {
		core_thr_mask = (cpu_mask >> (i * 4)) & 0xf;
		if (core_thr_mask) {
			if (core_thr_mask != core0_thr_mask)
				goto unsupp;
			nlm_coremask |= 1 << i;
		}
	}
	return;

unsupp:
	panic("Unsupported CPU mask %x\n", cpu_mask);
}

int __cpuinit nlm_wakeup_secondary_cpus(u32 wakeup_mask)
{
	unsigned long reset_vec;
	unsigned int *reset_data;

	/* Update reset entry point with CPU init code */
	reset_vec = CKSEG1ADDR(RESET_VEC_PHYS);
	memcpy((void *)reset_vec, (void *)nlm_reset_entry,
			(nlm_reset_entry_end - nlm_reset_entry));

	/* verify the mask and setup core config variables */
	nlm_parse_cpumask(wakeup_mask);

	/* Setup CPU init parameters */
	reset_data = (unsigned int *)CKSEG1ADDR(RESET_DATA_PHYS);
	reset_data[BOOT_THREAD_MODE] = nlm_threadmode;

	/* first wakeup core 0 siblings */
	nlm_boot_core0_siblings();

	/* enable the reset of the cores */
	nlm_enable_secondary_cores(nlm_coremask);
	return 0;
}
