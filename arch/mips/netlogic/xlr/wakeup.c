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
#include <linux/delay.h>
#include <linux/threads.h>

#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/string.h>

#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/common.h>
#include <asm/netlogic/mips-extns.h>

#include <asm/netlogic/xlr/iomap.h>
#include <asm/netlogic/xlr/pic.h>

int __cpuinit xlr_wakeup_secondary_cpus(void)
{
	struct nlm_soc_info *nodep;
	unsigned int i, j, boot_cpu;

	/*
	 *  In case of RMI boot, hit with NMI to get the cores
	 *  from bootloader to linux code.
	 */
	nodep = nlm_get_node(0);
	boot_cpu = hard_smp_processor_id();
	nlm_set_nmi_handler(nlm_rmiboot_preboot);
	for (i = 0; i < NR_CPUS; i++) {
		if (i == boot_cpu || !cpumask_test_cpu(i, &nlm_cpumask))
			continue;
		nlm_pic_send_ipi(nodep->picbase, i, 1, 1); /* send NMI */
	}

	/* Fill up the coremask early */
	nodep->coremask = 1;
	for (i = 1; i < NLM_CORES_PER_NODE; i++) {
		for (j = 1000000; j > 0; j--) {
			if (nlm_cpu_ready[i * NLM_THREADS_PER_CORE])
				break;
			udelay(10);
		}
		if (j != 0)
			nodep->coremask |= (1u << i);
		else
			pr_err("Failed to wakeup core %d\n", i);
	}

	return 0;
}
