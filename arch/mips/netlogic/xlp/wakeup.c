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
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/pic.h>
#include <asm/netlogic/xlp-hal/sys.h>

static int xlp_wakeup_core(uint64_t sysbase, int node, int core)
{
	uint32_t coremask, value;
	int count, resetreg;

	coremask = (1 << core);

	/* Enable CPU clock in case of 8xx/3xx */
	if (!cpu_is_xlpii()) {
		value = nlm_read_sys_reg(sysbase, SYS_CORE_DFS_DIS_CTRL);
		value &= ~coremask;
		nlm_write_sys_reg(sysbase, SYS_CORE_DFS_DIS_CTRL, value);
	}

	/* On 9XX, mark coherent first */
	if (cpu_is_xlp9xx()) {
		value = nlm_read_sys_reg(sysbase, SYS_9XX_CPU_NONCOHERENT_MODE);
		value &= ~coremask;
		nlm_write_sys_reg(sysbase, SYS_9XX_CPU_NONCOHERENT_MODE, value);
	}

	/* Remove CPU Reset */
	resetreg = cpu_is_xlp9xx() ? SYS_9XX_CPU_RESET : SYS_CPU_RESET;
	value = nlm_read_sys_reg(sysbase, resetreg);
	value &= ~coremask;
	nlm_write_sys_reg(sysbase, resetreg, value);

	/* We are done on 9XX */
	if (cpu_is_xlp9xx())
		return 1;

	/* Poll for CPU to mark itself coherent on other type of XLP */
	count = 100000;
	do {
		value = nlm_read_sys_reg(sysbase, SYS_CPU_NONCOHERENT_MODE);
	} while ((value & coremask) != 0 && --count > 0);

	return count != 0;
}

static int wait_for_cpus(int cpu, int bootcpu)
{
	volatile uint32_t *cpu_ready = nlm_get_boot_data(BOOT_CPU_READY);
	int i, count, notready;

	count = 0x800000;
	do {
		notready = nlm_threads_per_core;
		for (i = 0; i < nlm_threads_per_core; i++)
			if (cpu_ready[cpu + i] || cpu == bootcpu)
				--notready;
	} while (notready != 0 && --count > 0);

	return count != 0;
}

static void xlp_enable_secondary_cores(const cpumask_t *wakeup_mask)
{
	struct nlm_soc_info *nodep;
	uint64_t syspcibase, fusebase;
	uint32_t syscoremask, mask, fusemask;
	int core, n, cpu;

	for (n = 0; n < NLM_NR_NODES; n++) {
		if (n != 0) {
			/* check if node exists and is online */
			if (cpu_is_xlp9xx()) {
				int b = xlp9xx_get_socbus(n);
				pr_info("Node %d SoC PCI bus %d.\n", n, b);
				if (b == 0)
					break;
			} else {
				syspcibase = nlm_get_sys_pcibase(n);
				if (nlm_read_reg(syspcibase, 0) == 0xffffffff)
					break;
			}
			nlm_node_init(n);
		}

		/* read cores in reset from SYS */
		nodep = nlm_get_node(n);

		if (cpu_is_xlp9xx()) {
			fusebase = nlm_get_fuse_regbase(n);
			fusemask = nlm_read_reg(fusebase, FUSE_9XX_DEVCFG6);
			switch (read_c0_prid() & PRID_IMP_MASK) {
			case PRID_IMP_NETLOGIC_XLP5XX:
				mask = 0xff;
				break;
			case PRID_IMP_NETLOGIC_XLP9XX:
			default:
				mask = 0xfffff;
				break;
			}
		} else {
			fusemask = nlm_read_sys_reg(nodep->sysbase,
						SYS_EFUSE_DEVICE_CFG_STATUS0);
			switch (read_c0_prid() & PRID_IMP_MASK) {
			case PRID_IMP_NETLOGIC_XLP3XX:
				mask = 0xf;
				break;
			case PRID_IMP_NETLOGIC_XLP2XX:
				mask = 0x3;
				break;
			case PRID_IMP_NETLOGIC_XLP8XX:
			default:
				mask = 0xff;
				break;
			}
		}

		/*
		 * Fused out cores are set in the fusemask, and the remaining
		 * cores are renumbered to range 0 .. nactive-1
		 */
		syscoremask = (1 << hweight32(~fusemask & mask)) - 1;

		pr_info("Node %d - SYS/FUSE coremask %x\n", n, syscoremask);
		for (core = 0; core < nlm_cores_per_node(); core++) {
			/* we will be on node 0 core 0 */
			if (n == 0 && core == 0)
				continue;

			/* see if the core exists */
			if ((syscoremask & (1 << core)) == 0)
				continue;

			/* see if at least the first hw thread is enabled */
			cpu = (n * nlm_cores_per_node() + core)
						* NLM_THREADS_PER_CORE;
			if (!cpumask_test_cpu(cpu, wakeup_mask))
				continue;

			/* wake up the core */
			if (!xlp_wakeup_core(nodep->sysbase, n, core))
				continue;

			/* core is up */
			nodep->coremask |= 1u << core;

			/* spin until the hw threads sets their ready */
			if (!wait_for_cpus(cpu, 0))
				pr_err("Node %d : timeout core %d\n", n, core);
		}
	}
}

void xlp_wakeup_secondary_cpus()
{
	/*
	 * In case of u-boot, the secondaries are in reset
	 * first wakeup core 0 threads
	 */
	xlp_boot_core0_siblings();
	if (!wait_for_cpus(0, 0))
		pr_err("Node 0 : timeout core 0\n");

	/* now get other cores out of reset */
	xlp_enable_secondary_cores(&nlm_cpumask);
}
