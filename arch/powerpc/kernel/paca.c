/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/threads.h>
#include <linux/module.h>

#include <asm/lppaca.h>
#include <asm/paca.h>

/* This symbol is provided by the linker - let it fill in the paca
 * field correctly */
extern unsigned long __toc_start;

/*
 * The structure which the hypervisor knows about - this structure
 * should not cross a page boundary.  The vpa_init/register_vpa call
 * is now known to fail if the lppaca structure crosses a page
 * boundary.  The lppaca is also used on legacy iSeries and POWER5
 * pSeries boxes.  The lppaca is 640 bytes long, and cannot readily
 * change since the hypervisor knows its layout, so a 1kB alignment
 * will suffice to ensure that it doesn't cross a page boundary.
 */
struct lppaca lppaca[] = {
	[0 ... (NR_CPUS-1)] = {
		.desc = 0xd397d781,	/* "LpPa" */
		.size = sizeof(struct lppaca),
		.dyn_proc_status = 2,
		.decr_val = 0x00ff0000,
		.fpregs_in_use = 1,
		.end_of_quantum = 0xfffffffffffffffful,
		.slb_count = 64,
		.vmxregs_in_use = 0,
	},
};

/*
 * 3 persistent SLBs are registered here.  The buffer will be zero
 * initially, hence will all be invaild until we actually write them.
 */
struct slb_shadow slb_shadow[] __cacheline_aligned = {
	[0 ... (NR_CPUS-1)] = {
		.persistent = SLB_NUM_BOLTED,
		.buffer_length = sizeof(struct slb_shadow),
	},
};

/* The Paca is an array with one entry per processor.  Each contains an
 * lppaca, which contains the information shared between the
 * hypervisor and Linux.
 * On systems with hardware multi-threading, there are two threads
 * per processor.  The Paca array must contain an entry for each thread.
 * The VPD Areas will give a max logical processors = 2 * max physical
 * processors.  The processor VPD array needs one entry per physical
 * processor (not thread).
 */
struct paca_struct paca[NR_CPUS];
EXPORT_SYMBOL(paca);

void __init initialise_pacas(void)
{
	int cpu;

	/* The TOC register (GPR2) points 32kB into the TOC, so that 64kB
	 * of the TOC can be addressed using a single machine instruction.
	 */
	unsigned long kernel_toc = (unsigned long)(&__toc_start) + 0x8000UL;

	/* Can't use for_each_*_cpu, as they aren't functional yet */
	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		struct paca_struct *new_paca = &paca[cpu];

		new_paca->lppaca_ptr = &lppaca[cpu];
		new_paca->lock_token = 0x8000;
		new_paca->paca_index = cpu;
		new_paca->kernel_toc = kernel_toc;
		new_paca->kernelbase = KERNELBASE;
		new_paca->kernel_msr = MSR_KERNEL;
		new_paca->hw_cpu_id = 0xffff;
		new_paca->slb_shadow_ptr = &slb_shadow[cpu];
		new_paca->__current = &init_task;

	}
}
