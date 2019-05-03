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
#include <linux/sched/task.h>
#include <linux/numa.h>

#include <asm/lppaca.h>
#include <asm/paca.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/kexec.h>

#include "setup.h"

#ifndef CONFIG_SMP
#define boot_cpuid 0
#endif

static void *__init alloc_paca_data(unsigned long size, unsigned long align,
				unsigned long limit, int cpu)
{
	void *ptr;
	int nid;

	/*
	 * boot_cpuid paca is allocated very early before cpu_to_node is up.
	 * Set bottom-up mode, because the boot CPU should be on node-0,
	 * which will put its paca in the right place.
	 */
	if (cpu == boot_cpuid) {
		nid = NUMA_NO_NODE;
		memblock_set_bottom_up(true);
	} else {
		nid = early_cpu_to_node(cpu);
	}

	ptr = memblock_alloc_try_nid(size, align, MEMBLOCK_LOW_LIMIT,
				     limit, nid);
	if (!ptr)
		panic("cannot allocate paca data");

	if (cpu == boot_cpuid)
		memblock_set_bottom_up(false);

	return ptr;
}

#ifdef CONFIG_PPC_PSERIES

/*
 * See asm/lppaca.h for more detail.
 *
 * lppaca structures must must be 1kB in size, L1 cache line aligned,
 * and not cross 4kB boundary. A 1kB size and 1kB alignment will satisfy
 * these requirements.
 */
static inline void init_lppaca(struct lppaca *lppaca)
{
	BUILD_BUG_ON(sizeof(struct lppaca) != 640);

	*lppaca = (struct lppaca) {
		.desc = cpu_to_be32(0xd397d781),	/* "LpPa" */
		.size = cpu_to_be16(0x400),
		.fpregs_in_use = 1,
		.slb_count = cpu_to_be16(64),
		.vmxregs_in_use = 0,
		.page_ins = 0, };
};

static struct lppaca * __init new_lppaca(int cpu, unsigned long limit)
{
	struct lppaca *lp;
	size_t size = 0x400;

	BUILD_BUG_ON(size < sizeof(struct lppaca));

	if (early_cpu_has_feature(CPU_FTR_HVMODE))
		return NULL;

	lp = alloc_paca_data(size, 0x400, limit, cpu);
	init_lppaca(lp);

	return lp;
}
#endif /* CONFIG_PPC_BOOK3S */

#ifdef CONFIG_PPC_BOOK3S_64

/*
 * 3 persistent SLBs are allocated here.  The buffer will be zero
 * initially, hence will all be invaild until we actually write them.
 *
 * If you make the number of persistent SLB entries dynamic, please also
 * update PR KVM to flush and restore them accordingly.
 */
static struct slb_shadow * __init new_slb_shadow(int cpu, unsigned long limit)
{
	struct slb_shadow *s;

	if (cpu != boot_cpuid) {
		/*
		 * Boot CPU comes here before early_radix_enabled
		 * is parsed (e.g., for disable_radix). So allocate
		 * always and this will be fixed up in free_unused_pacas.
		 */
		if (early_radix_enabled())
			return NULL;
	}

	s = alloc_paca_data(sizeof(*s), L1_CACHE_BYTES, limit, cpu);

	s->persistent = cpu_to_be32(SLB_NUM_BOLTED);
	s->buffer_length = cpu_to_be32(sizeof(*s));

	return s;
}

#endif /* CONFIG_PPC_BOOK3S_64 */

/* The Paca is an array with one entry per processor.  Each contains an
 * lppaca, which contains the information shared between the
 * hypervisor and Linux.
 * On systems with hardware multi-threading, there are two threads
 * per processor.  The Paca array must contain an entry for each thread.
 * The VPD Areas will give a max logical processors = 2 * max physical
 * processors.  The processor VPD array needs one entry per physical
 * processor (not thread).
 */
struct paca_struct **paca_ptrs __read_mostly;
EXPORT_SYMBOL(paca_ptrs);

void __init initialise_paca(struct paca_struct *new_paca, int cpu)
{
#ifdef CONFIG_PPC_PSERIES
	new_paca->lppaca_ptr = NULL;
#endif
#ifdef CONFIG_PPC_BOOK3E
	new_paca->kernel_pgd = swapper_pg_dir;
#endif
	new_paca->lock_token = 0x8000;
	new_paca->paca_index = cpu;
	new_paca->kernel_toc = kernel_toc_addr();
	new_paca->kernelbase = (unsigned long) _stext;
	/* Only set MSR:IR/DR when MMU is initialized */
	new_paca->kernel_msr = MSR_KERNEL & ~(MSR_IR | MSR_DR);
	new_paca->hw_cpu_id = 0xffff;
	new_paca->kexec_state = KEXEC_STATE_NONE;
	new_paca->__current = &init_task;
	new_paca->data_offset = 0xfeeeeeeeeeeeeeeeULL;
#ifdef CONFIG_PPC_BOOK3S_64
	new_paca->slb_shadow_ptr = NULL;
#endif

#ifdef CONFIG_PPC_BOOK3E
	/* For now -- if we have threads this will be adjusted later */
	new_paca->tcd_ptr = &new_paca->tcd;
#endif
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
	if (early_cpu_has_feature(CPU_FTR_HVMODE))
		mtspr(SPRN_SPRG_HPACA, local_paca);
#endif
	mtspr(SPRN_SPRG_PACA, local_paca);

}

static int __initdata paca_nr_cpu_ids;
static int __initdata paca_ptrs_size;
static int __initdata paca_struct_size;

void __init allocate_paca_ptrs(void)
{
	paca_nr_cpu_ids = nr_cpu_ids;

	paca_ptrs_size = sizeof(struct paca_struct *) * nr_cpu_ids;
	paca_ptrs = memblock_alloc_raw(paca_ptrs_size, SMP_CACHE_BYTES);
	if (!paca_ptrs)
		panic("Failed to allocate %d bytes for paca pointers\n",
		      paca_ptrs_size);

	memset(paca_ptrs, 0x88, paca_ptrs_size);
}

void __init allocate_paca(int cpu)
{
	u64 limit;
	struct paca_struct *paca;

	BUG_ON(cpu >= paca_nr_cpu_ids);

#ifdef CONFIG_PPC_BOOK3S_64
	/*
	 * We access pacas in real mode, and cannot take SLB faults
	 * on them when in virtual mode, so allocate them accordingly.
	 */
	limit = min(ppc64_bolted_size(), ppc64_rma_size);
#else
	limit = ppc64_rma_size;
#endif

	paca = alloc_paca_data(sizeof(struct paca_struct), L1_CACHE_BYTES,
				limit, cpu);
	paca_ptrs[cpu] = paca;

	initialise_paca(paca, cpu);
#ifdef CONFIG_PPC_PSERIES
	paca->lppaca_ptr = new_lppaca(cpu, limit);
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	paca->slb_shadow_ptr = new_slb_shadow(cpu, limit);
#endif
	paca_struct_size += sizeof(struct paca_struct);
}

void __init free_unused_pacas(void)
{
	int new_ptrs_size;

	new_ptrs_size = sizeof(struct paca_struct *) * nr_cpu_ids;
	if (new_ptrs_size < paca_ptrs_size)
		memblock_free(__pa(paca_ptrs) + new_ptrs_size,
					paca_ptrs_size - new_ptrs_size);

	paca_nr_cpu_ids = nr_cpu_ids;
	paca_ptrs_size = new_ptrs_size;

#ifdef CONFIG_PPC_BOOK3S_64
	if (early_radix_enabled()) {
		/* Ugly fixup, see new_slb_shadow() */
		memblock_free(__pa(paca_ptrs[boot_cpuid]->slb_shadow_ptr),
				sizeof(struct slb_shadow));
		paca_ptrs[boot_cpuid]->slb_shadow_ptr = NULL;
	}
#endif

	printk(KERN_DEBUG "Allocated %u bytes for %u pacas\n",
			paca_ptrs_size + paca_struct_size, nr_cpu_ids);
}

void copy_mm_to_paca(struct mm_struct *mm)
{
#ifdef CONFIG_PPC_BOOK3S
	mm_context_t *context = &mm->context;

	get_paca()->mm_ctx_id = context->id;
#ifdef CONFIG_PPC_MM_SLICES
	VM_BUG_ON(!mm->context.slb_addr_limit);
	get_paca()->mm_ctx_slb_addr_limit = mm->context.slb_addr_limit;
	memcpy(&get_paca()->mm_ctx_low_slices_psize,
	       &context->low_slices_psize, sizeof(context->low_slices_psize));
	memcpy(&get_paca()->mm_ctx_high_slices_psize,
	       &context->high_slices_psize, TASK_SLICE_ARRAY_SZ(mm));
#else /* CONFIG_PPC_MM_SLICES */
	get_paca()->mm_ctx_user_psize = context->user_psize;
	get_paca()->mm_ctx_sllp = context->sllp;
#endif
#else /* !CONFIG_PPC_BOOK3S */
	return;
#endif
}
