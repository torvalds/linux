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
#include <linux/memblock.h>

#include <asm/firmware.h>
#include <asm/lppaca.h>
#include <asm/paca.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/iseries/lpar_map.h>
#include <asm/iseries/hv_types.h>
#include <asm/kexec.h>

/* This symbol is provided by the linker - let it fill in the paca
 * field correctly */
extern unsigned long __toc_start;

#ifdef CONFIG_PPC_BOOK3S

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
		.page_ins = 0,
	},
};

#endif /* CONFIG_PPC_BOOK3S */

#ifdef CONFIG_PPC_STD_MMU_64

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

#endif /* CONFIG_PPC_STD_MMU_64 */

/* The Paca is an array with one entry per processor.  Each contains an
 * lppaca, which contains the information shared between the
 * hypervisor and Linux.
 * On systems with hardware multi-threading, there are two threads
 * per processor.  The Paca array must contain an entry for each thread.
 * The VPD Areas will give a max logical processors = 2 * max physical
 * processors.  The processor VPD array needs one entry per physical
 * processor (not thread).
 */
struct paca_struct *paca;
EXPORT_SYMBOL(paca);

struct paca_struct boot_paca;

void __init initialise_paca(struct paca_struct *new_paca, int cpu)
{
       /* The TOC register (GPR2) points 32kB into the TOC, so that 64kB
	* of the TOC can be addressed using a single machine instruction.
	*/
	unsigned long kernel_toc = (unsigned long)(&__toc_start) + 0x8000UL;

#ifdef CONFIG_PPC_BOOK3S
	new_paca->lppaca_ptr = &lppaca[cpu];
#else
	new_paca->kernel_pgd = swapper_pg_dir;
#endif
	new_paca->lock_token = 0x8000;
	new_paca->paca_index = cpu;
	new_paca->kernel_toc = kernel_toc;
	new_paca->kernelbase = (unsigned long) _stext;
	new_paca->kernel_msr = MSR_KERNEL;
	new_paca->hw_cpu_id = 0xffff;
	new_paca->kexec_state = KEXEC_STATE_NONE;
	new_paca->__current = &init_task;
#ifdef CONFIG_PPC_STD_MMU_64
	new_paca->slb_shadow_ptr = &slb_shadow[cpu];
#endif /* CONFIG_PPC_STD_MMU_64 */
}

/* Put the paca pointer into r13 and SPRG_PACA */
void setup_paca(struct paca_struct *new_paca)
{
	local_paca = new_paca;
	mtspr(SPRN_SPRG_PACA, local_paca);
#ifdef CONFIG_PPC_BOOK3E
	mtspr(SPRN_SPRG_TLB_EXFRAME, local_paca->extlb);
#endif
}

static int __initdata paca_size;

void __init allocate_pacas(void)
{
	int nr_cpus, cpu, limit;

	/*
	 * We can't take SLB misses on the paca, and we want to access them
	 * in real mode, so allocate them within the RMA and also within
	 * the first segment. On iSeries they must be within the area mapped
	 * by the HV, which is HvPagesToMap * HVPAGESIZE bytes.
	 */
	limit = min(0x10000000ULL, memblock.rmo_size);
	if (firmware_has_feature(FW_FEATURE_ISERIES))
		limit = min(limit, HvPagesToMap * HVPAGESIZE);

	nr_cpus = NR_CPUS;
	/* On iSeries we know we can never have more than 64 cpus */
	if (firmware_has_feature(FW_FEATURE_ISERIES))
		nr_cpus = min(64, nr_cpus);

	paca_size = PAGE_ALIGN(sizeof(struct paca_struct) * nr_cpus);

	paca = __va(memblock_alloc_base(paca_size, PAGE_SIZE, limit));
	memset(paca, 0, paca_size);

	printk(KERN_DEBUG "Allocated %u bytes for %d pacas at %p\n",
		paca_size, nr_cpus, paca);

	/* Can't use for_each_*_cpu, as they aren't functional yet */
	for (cpu = 0; cpu < nr_cpus; cpu++)
		initialise_paca(&paca[cpu], cpu);
}

void __init free_unused_pacas(void)
{
	int new_size;

	new_size = PAGE_ALIGN(sizeof(struct paca_struct) * num_possible_cpus());

	if (new_size >= paca_size)
		return;

	memblock_free(__pa(paca) + new_size, paca_size - new_size);

	printk(KERN_DEBUG "Freed %u bytes for unused pacas\n",
		paca_size - new_size);

	paca_size = new_size;
}
