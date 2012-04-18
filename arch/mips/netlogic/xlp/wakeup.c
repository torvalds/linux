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

static void xlp_enable_secondary_cores(void)
{
	uint32_t core, value, coremask, syscoremask;
	int count;

	/* read cores in reset from SYS block */
	syscoremask = nlm_read_sys_reg(nlm_sys_base, SYS_CPU_RESET);

	/* update user specified */
	nlm_coremask = nlm_coremask & (syscoremask | 1);

	for (core = 1; core < 8; core++) {
		coremask = 1 << core;
		if ((nlm_coremask & coremask) == 0)
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
		count = 100000;
		do {
			value = nlm_read_sys_reg(nlm_sys_base,
			    SYS_CPU_NONCOHERENT_MODE);
		} while ((value & coremask) != 0 && count-- > 0);

		if (count == 0)
			pr_err("Failed to enable core %d\n", core);
	}
}

void xlp_wakeup_secondary_cpus(void)
{
	/*
	 * In case of u-boot, the secondaries are in reset
	 * first wakeup core 0 threads
	 */
	xlp_boot_core0_siblings();

	/* now get other cores out of reset */
	xlp_enable_secondary_cores();
}
