/*
 * TLB support routines.
 *
 * Copyright (C) 1998-2001, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 08/02/00 A. Mallick <asit.k.mallick@intel.com>
 *		Modified RID allocation for SMP
 *          Goutham Rao <goutham.rao@intel.com>
 *              IPI based ptc implementation and A-step IPI implementation.
 * Rohit Seth <rohit.seth@intel.com>
 * Ken Chen <kenneth.w.chen@intel.com>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/bootmem.h>

#include <asm/delay.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/pal.h>
#include <asm/tlbflush.h>
#include <asm/dma.h>

static struct {
	unsigned long mask;	/* mask of supported purge page-sizes */
	unsigned long max_bits;	/* log2 of largest supported purge page-size */
} purge;

struct ia64_ctx ia64_ctx = {
	.lock =	__SPIN_LOCK_UNLOCKED(ia64_ctx.lock),
	.next =	1,
	.max_ctx = ~0U
};

DEFINE_PER_CPU(u8, ia64_need_tlb_flush);

/*
 * Initializes the ia64_ctx.bitmap array based on max_ctx+1.
 * Called after cpu_init() has setup ia64_ctx.max_ctx based on
 * maximum RID that is supported by boot CPU.
 */
void __init
mmu_context_init (void)
{
	ia64_ctx.bitmap = alloc_bootmem((ia64_ctx.max_ctx+1)>>3);
	ia64_ctx.flushmap = alloc_bootmem((ia64_ctx.max_ctx+1)>>3);
}

/*
 * Acquire the ia64_ctx.lock before calling this function!
 */
void
wrap_mmu_context (struct mm_struct *mm)
{
	int i, cpu;
	unsigned long flush_bit;

	for (i=0; i <= ia64_ctx.max_ctx / BITS_PER_LONG; i++) {
		flush_bit = xchg(&ia64_ctx.flushmap[i], 0);
		ia64_ctx.bitmap[i] ^= flush_bit;
	}
 
	/* use offset at 300 to skip daemons */
	ia64_ctx.next = find_next_zero_bit(ia64_ctx.bitmap,
				ia64_ctx.max_ctx, 300);
	ia64_ctx.limit = find_next_bit(ia64_ctx.bitmap,
				ia64_ctx.max_ctx, ia64_ctx.next);

	/*
	 * can't call flush_tlb_all() here because of race condition
	 * with O(1) scheduler [EF]
	 */
	cpu = get_cpu(); /* prevent preemption/migration */
	for_each_online_cpu(i)
		if (i != cpu)
			per_cpu(ia64_need_tlb_flush, i) = 1;
	put_cpu();
	local_flush_tlb_all();
}

void
ia64_global_tlb_purge (struct mm_struct *mm, unsigned long start,
		       unsigned long end, unsigned long nbits)
{
	static DEFINE_SPINLOCK(ptcg_lock);

	if (mm != current->active_mm || !current->mm) {
		flush_tlb_all();
		return;
	}

	/* HW requires global serialization of ptc.ga.  */
	spin_lock(&ptcg_lock);
	{
		do {
			/*
			 * Flush ALAT entries also.
			 */
			ia64_ptcga(start, (nbits<<2));
			ia64_srlz_i();
			start += (1UL << nbits);
		} while (start < end);
	}
	spin_unlock(&ptcg_lock);
}

void
local_flush_tlb_all (void)
{
	unsigned long i, j, flags, count0, count1, stride0, stride1, addr;

	addr    = local_cpu_data->ptce_base;
	count0  = local_cpu_data->ptce_count[0];
	count1  = local_cpu_data->ptce_count[1];
	stride0 = local_cpu_data->ptce_stride[0];
	stride1 = local_cpu_data->ptce_stride[1];

	local_irq_save(flags);
	for (i = 0; i < count0; ++i) {
		for (j = 0; j < count1; ++j) {
			ia64_ptce(addr);
			addr += stride1;
		}
		addr += stride0;
	}
	local_irq_restore(flags);
	ia64_srlz_i();			/* srlz.i implies srlz.d */
}

void
flush_tlb_range (struct vm_area_struct *vma, unsigned long start,
		 unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long size = end - start;
	unsigned long nbits;

#ifndef CONFIG_SMP
	if (mm != current->active_mm) {
		mm->context = 0;
		return;
	}
#endif

	nbits = ia64_fls(size + 0xfff);
	while (unlikely (((1UL << nbits) & purge.mask) == 0) &&
			(nbits < purge.max_bits))
		++nbits;
	if (nbits > purge.max_bits)
		nbits = purge.max_bits;
	start &= ~((1UL << nbits) - 1);

	preempt_disable();
#ifdef CONFIG_SMP
	if (mm != current->active_mm || cpus_weight(mm->cpu_vm_mask) != 1) {
		platform_global_tlb_purge(mm, start, end, nbits);
		preempt_enable();
		return;
	}
#endif
	do {
		ia64_ptcl(start, (nbits<<2));
		start += (1UL << nbits);
	} while (start < end);
	preempt_enable();
	ia64_srlz_i();			/* srlz.i implies srlz.d */
}
EXPORT_SYMBOL(flush_tlb_range);

void __devinit
ia64_tlb_init (void)
{
	ia64_ptce_info_t ptce_info;
	unsigned long tr_pgbits;
	long status;

	if ((status = ia64_pal_vm_page_size(&tr_pgbits, &purge.mask)) != 0) {
		printk(KERN_ERR "PAL_VM_PAGE_SIZE failed with status=%ld;"
		       "defaulting to architected purge page-sizes.\n", status);
		purge.mask = 0x115557000UL;
	}
	purge.max_bits = ia64_fls(purge.mask);

	ia64_get_ptce(&ptce_info);
	local_cpu_data->ptce_base = ptce_info.base;
	local_cpu_data->ptce_count[0] = ptce_info.count[0];
	local_cpu_data->ptce_count[1] = ptce_info.count[1];
	local_cpu_data->ptce_stride[0] = ptce_info.stride[0];
	local_cpu_data->ptce_stride[1] = ptce_info.stride[1];

	local_flush_tlb_all();	/* nuke left overs from bootstrapping... */
}
