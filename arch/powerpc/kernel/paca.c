/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/smp.h>
#include <linux/export.h>
#include <linux/memblock.h>

#include <asm/lppaca.h>
#include <asm/paca.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/kexec.h>

/* This symbol is provided by the linker - let it fill in the paca
 * field correctly */
extern unsigned long __toc_start;

#ifdef CONFIG_PPC_BOOK3S

/*
 * The structure which the hypervisor knows about - this structure
 * should not cross a page boundary.  The vpa_init/register_vpa call
 * is now known to fail if the lppaca structure crosses a page
 * boundary.  The lppaca is also used on POWER5 pSeries boxes.
 * The lppaca is 640 bytes long, and cannot readily
 * change since the hypervisor knows its layout, so a 1kB alignment
 * will suffice to ensure that it doesn't cross a page boundary.
 */
struct lppaca lppaca[] = {
	[0 ... (NR_LPPACAS-1)] = {
		.desc = 0xd397d781,	/* "LpPa" */
		.size = sizeof(struct lppaca),
		.fpregs_in_use = 1,
		.slb_count = 64,
		.vmxregs_in_use = 0,
		.page_ins = 0,
	},
};

static struct lppaca *extra_lppacas;
static long __initdata lppaca_size;

static void allocate_lppacas(int nr_cpus, unsigned long limit)
{
	if (nr_cpus <= NR_LPPACAS)
		return;

	lppaca_size = PAGE_ALIGN(sizeof(struct lppaca) *
				 (nr_cpus - NR_LPPACAS));
	extra_lppacas = __va(memblock_alloc_base(lppaca_size,
						 PAGE_SIZE, limit));
}

static struct lppaca *new_lppaca(int cpu)
{
	struct lppaca *lp;

	if (cpu < NR_LPPACAS)
		return &lppaca[cpu];

	lp = extra_lppacas + (cpu - NR_LPPACAS);
	*lp = lppaca[0];

	return lp;
}

static void free_lppacas(void)
{
	long new_size = 0, nr;

	if (!lppaca_size)
		return;
	nr = num_possible_cpus() - NR_LPPACAS;
	if (nr > 0)
		new_size = PAGE_ALIGN(nr * sizeof(struct lppaca));
	if (new_size >= lppaca_size)
		return;

	memblock_free(__pa(extra_lppacas) + new_size, lppaca_size - new_size);
	lppaca_size = new_size;
}

#else

static inline void allocate_lppacas(int nr_cpus, unsigned long limit) { }
static inline void free_lppacas(void) { }

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
	new_paca->lppaca_ptr = new_lppaca(cpu);
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
	new_paca->data_offset = 0xfeeeeeeeeeeeeeeeULL;
#ifdef CONFIG_PPC_STD_MMU_64
	new_paca->slb_shadow_ptr = &slb_shadow[cpu];
#endif /* CONFIG_PPC_STD_MMU_64 */
}

/* Put the paca pointer into r13 and SPRG_PACA */
void setup_paca(struct paca_struct *new_paca)
{
	/* Setup r13 */
	local_paca = new_paca;

#ifdef CONFIG_PPC_BOOK3E
	/* On Book3E, initialize the TLB miss exception frames */
	mtspr(SPRN_SPRG_TLB_EXFRAME, local_paca->extlb);
#else
	/* In HV mode, we setup both HPACA and PACA to avoid problems
	 * if we do a GET_PACA() before the feature fixups have been
	 * applied
	 */
	if (cpu_has_feature(CPU_FTR_HVMODE))
		mtspr(SPRN_SPRG_HPACA, local_paca);
#endif
	mtspr(SPRN_SPRG_PACA, local_paca);

}

static int __initdata paca_size;

void __init allocate_pacas(void)
{
	int cpu, limit;

	/*
	 * We can't take SLB misses on the paca, and we want to access them
	 * in real mode, so allocate them within the RMA and also within
	 * the first segment.
	 */
	limit = min(0x10000000ULL, ppc64_rma_size);

	paca_size = PAGE_ALIGN(sizeof(struct paca_struct) * nr_cpu_ids);

	paca = __va(memblock_alloc_base(paca_size, PAGE_SIZE, limit));
	memset(paca, 0, paca_size);

	printk(KERN_DEBUG "Allocated %u bytes for %d pacas at %p\n",
		paca_size, nr_cpu_ids, paca);

	allocate_lppacas(nr_cpu_ids, limit);

	/* Can't use for_each_*_cpu, as they aren't functional yet */
	for (cpu = 0; cpu < nr_cpu_ids; cpu++)
		initialise_paca(&paca[cpu], cpu);
}

void __init free_unused_pacas(void)
{
	int new_size;

	new_size = PAGE_ALIGN(sizeof(struct paca_struct) * nr_cpu_ids);

	if (new_size >= paca_size)
		return;

	memblock_free(__pa(paca) + new_size, paca_size - new_size);

	printk(KERN_DEBUG "Freed %u bytes for unused pacas\n",
		paca_size - new_size);

	paca_size = new_size;

	free_lppacas();
}
