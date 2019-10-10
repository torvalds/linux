// SPDX-License-Identifier: GPL-2.0-only
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
 * Christophe de Dinechin <ddd@hp.com>: Avoid ptc.e on memory allocation
 * Copyright (C) 2007 Intel Corp
 *	Fenghua Yu <fenghua.yu@intel.com>
 *	Add multiple ptc.g/ptc.ga instruction support in global tlb purge.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/slab.h>

#include <asm/delay.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/pal.h>
#include <asm/tlbflush.h>
#include <asm/dma.h>
#include <asm/processor.h>
#include <asm/sal.h>
#include <asm/tlb.h>

static struct {
	u64 mask;		/* mask of supported purge page-sizes */
	unsigned long max_bits;	/* log2 of largest supported purge page-size */
} purge;

struct ia64_ctx ia64_ctx = {
	.lock =	__SPIN_LOCK_UNLOCKED(ia64_ctx.lock),
	.next =	1,
	.max_ctx = ~0U
};

DEFINE_PER_CPU(u8, ia64_need_tlb_flush);
DEFINE_PER_CPU(u8, ia64_tr_num);  /*Number of TR slots in current processor*/
DEFINE_PER_CPU(u8, ia64_tr_used); /*Max Slot number used by kernel*/

struct ia64_tr_entry *ia64_idtrs[NR_CPUS];

/*
 * Initializes the ia64_ctx.bitmap array based on max_ctx+1.
 * Called after cpu_init() has setup ia64_ctx.max_ctx based on
 * maximum RID that is supported by boot CPU.
 */
void __init
mmu_context_init (void)
{
	ia64_ctx.bitmap = memblock_alloc((ia64_ctx.max_ctx + 1) >> 3,
					 SMP_CACHE_BYTES);
	if (!ia64_ctx.bitmap)
		panic("%s: Failed to allocate %u bytes\n", __func__,
		      (ia64_ctx.max_ctx + 1) >> 3);
	ia64_ctx.flushmap = memblock_alloc((ia64_ctx.max_ctx + 1) >> 3,
					   SMP_CACHE_BYTES);
	if (!ia64_ctx.flushmap)
		panic("%s: Failed to allocate %u bytes\n", __func__,
		      (ia64_ctx.max_ctx + 1) >> 3);
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

/*
 * Implement "spinaphores" ... like counting semaphores, but they
 * spin instead of sleeping.  If there are ever any other users for
 * this primitive it can be moved up to a spinaphore.h header.
 */
struct spinaphore {
	unsigned long	ticket;
	unsigned long	serve;
};

static inline void spinaphore_init(struct spinaphore *ss, int val)
{
	ss->ticket = 0;
	ss->serve = val;
}

static inline void down_spin(struct spinaphore *ss)
{
	unsigned long t = ia64_fetchadd(1, &ss->ticket, acq), serve;

	if (time_before(t, ss->serve))
		return;

	ia64_invala();

	for (;;) {
		asm volatile ("ld8.c.nc %0=[%1]" : "=r"(serve) : "r"(&ss->serve) : "memory");
		if (time_before(t, serve))
			return;
		cpu_relax();
	}
}

static inline void up_spin(struct spinaphore *ss)
{
	ia64_fetchadd(1, &ss->serve, rel);
}

static struct spinaphore ptcg_sem;
static u16 nptcg = 1;
static int need_ptcg_sem = 1;
static int toolatetochangeptcgsem = 0;

/*
 * Kernel parameter "nptcg=" overrides max number of concurrent global TLB
 * purges which is reported from either PAL or SAL PALO.
 *
 * We don't have sanity checking for nptcg value. It's the user's responsibility
 * for valid nptcg value on the platform. Otherwise, kernel may hang in some
 * cases.
 */
static int __init
set_nptcg(char *str)
{
	int value = 0;

	get_option(&str, &value);
	setup_ptcg_sem(value, NPTCG_FROM_KERNEL_PARAMETER);

	return 1;
}

__setup("nptcg=", set_nptcg);

/*
 * Maximum number of simultaneous ptc.g purges in the system can
 * be defined by PAL_VM_SUMMARY (in which case we should take
 * the smallest value for any cpu in the system) or by the PAL
 * override table (in which case we should ignore the value from
 * PAL_VM_SUMMARY).
 *
 * Kernel parameter "nptcg=" overrides maximum number of simultanesous ptc.g
 * purges defined in either PAL_VM_SUMMARY or PAL override table. In this case,
 * we should ignore the value from either PAL_VM_SUMMARY or PAL override table.
 *
 * Complicating the logic here is the fact that num_possible_cpus()
 * isn't fully setup until we start bringing cpus online.
 */
void
setup_ptcg_sem(int max_purges, int nptcg_from)
{
	static int kp_override;
	static int palo_override;
	static int firstcpu = 1;

	if (toolatetochangeptcgsem) {
		if (nptcg_from == NPTCG_FROM_PAL && max_purges == 0)
			BUG_ON(1 < nptcg);
		else
			BUG_ON(max_purges < nptcg);
		return;
	}

	if (nptcg_from == NPTCG_FROM_KERNEL_PARAMETER) {
		kp_override = 1;
		nptcg = max_purges;
		goto resetsema;
	}
	if (kp_override) {
		need_ptcg_sem = num_possible_cpus() > nptcg;
		return;
	}

	if (nptcg_from == NPTCG_FROM_PALO) {
		palo_override = 1;

		/* In PALO max_purges == 0 really means it! */
		if (max_purges == 0)
			panic("Whoa! Platform does not support global TLB purges.\n");
		nptcg = max_purges;
		if (nptcg == PALO_MAX_TLB_PURGES) {
			need_ptcg_sem = 0;
			return;
		}
		goto resetsema;
	}
	if (palo_override) {
		if (nptcg != PALO_MAX_TLB_PURGES)
			need_ptcg_sem = (num_possible_cpus() > nptcg);
		return;
	}

	/* In PAL_VM_SUMMARY max_purges == 0 actually means 1 */
	if (max_purges == 0) max_purges = 1;

	if (firstcpu) {
		nptcg = max_purges;
		firstcpu = 0;
	}
	if (max_purges < nptcg)
		nptcg = max_purges;
	if (nptcg == PAL_MAX_PURGES) {
		need_ptcg_sem = 0;
		return;
	} else
		need_ptcg_sem = (num_possible_cpus() > nptcg);

resetsema:
	spinaphore_init(&ptcg_sem, max_purges);
}

#ifdef CONFIG_SMP
static void
ia64_global_tlb_purge (struct mm_struct *mm, unsigned long start,
		       unsigned long end, unsigned long nbits)
{
	struct mm_struct *active_mm = current->active_mm;

	toolatetochangeptcgsem = 1;

	if (mm != active_mm) {
		/* Restore region IDs for mm */
		if (mm && active_mm) {
			activate_context(mm);
		} else {
			flush_tlb_all();
			return;
		}
	}

	if (need_ptcg_sem)
		down_spin(&ptcg_sem);

	do {
		/*
		 * Flush ALAT entries also.
		 */
		ia64_ptcga(start, (nbits << 2));
		ia64_srlz_i();
		start += (1UL << nbits);
	} while (start < end);

	if (need_ptcg_sem)
		up_spin(&ptcg_sem);

        if (mm != active_mm) {
                activate_context(active_mm);
        }
}
#endif /* CONFIG_SMP */

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

static void
__flush_tlb_range (struct vm_area_struct *vma, unsigned long start,
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
	if (mm != current->active_mm || cpumask_weight(mm_cpumask(mm)) != 1) {
		ia64_global_tlb_purge(mm, start, end, nbits);
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

void flush_tlb_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	if (unlikely(end - start >= 1024*1024*1024*1024UL
			|| REGION_NUMBER(start) != REGION_NUMBER(end - 1))) {
		/*
		 * If we flush more than a tera-byte or across regions, we're
		 * probably better off just flushing the entire TLB(s).  This
		 * should be very rare and is not worth optimizing for.
		 */
		flush_tlb_all();
	} else {
		/* flush the address range from the tlb */
		__flush_tlb_range(vma, start, end);
		/* flush the virt. page-table area mapping the addr range */
		__flush_tlb_range(vma, ia64_thash(start), ia64_thash(end));
	}
}
EXPORT_SYMBOL(flush_tlb_range);

void ia64_tlb_init(void)
{
	ia64_ptce_info_t uninitialized_var(ptce_info); /* GCC be quiet */
	u64 tr_pgbits;
	long status;
	pal_vm_info_1_u_t vm_info_1;
	pal_vm_info_2_u_t vm_info_2;
	int cpu = smp_processor_id();

	if ((status = ia64_pal_vm_page_size(&tr_pgbits, &purge.mask)) != 0) {
		printk(KERN_ERR "PAL_VM_PAGE_SIZE failed with status=%ld; "
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
	status = ia64_pal_vm_summary(&vm_info_1, &vm_info_2);

	if (status) {
		printk(KERN_ERR "ia64_pal_vm_summary=%ld\n", status);
		per_cpu(ia64_tr_num, cpu) = 8;
		return;
	}
	per_cpu(ia64_tr_num, cpu) = vm_info_1.pal_vm_info_1_s.max_itr_entry+1;
	if (per_cpu(ia64_tr_num, cpu) >
				(vm_info_1.pal_vm_info_1_s.max_dtr_entry+1))
		per_cpu(ia64_tr_num, cpu) =
				vm_info_1.pal_vm_info_1_s.max_dtr_entry+1;
	if (per_cpu(ia64_tr_num, cpu) > IA64_TR_ALLOC_MAX) {
		static int justonce = 1;
		per_cpu(ia64_tr_num, cpu) = IA64_TR_ALLOC_MAX;
		if (justonce) {
			justonce = 0;
			printk(KERN_DEBUG "TR register number exceeds "
			       "IA64_TR_ALLOC_MAX!\n");
		}
	}
}

/*
 * is_tr_overlap
 *
 * Check overlap with inserted TRs.
 */
static int is_tr_overlap(struct ia64_tr_entry *p, u64 va, u64 log_size)
{
	u64 tr_log_size;
	u64 tr_end;
	u64 va_rr = ia64_get_rr(va);
	u64 va_rid = RR_TO_RID(va_rr);
	u64 va_end = va + (1<<log_size) - 1;

	if (va_rid != RR_TO_RID(p->rr))
		return 0;
	tr_log_size = (p->itir & 0xff) >> 2;
	tr_end = p->ifa + (1<<tr_log_size) - 1;

	if (va > tr_end || p->ifa > va_end)
		return 0;
	return 1;

}

/*
 * ia64_insert_tr in virtual mode. Allocate a TR slot
 *
 * target_mask : 0x1 : itr, 0x2 : dtr, 0x3 : idtr
 *
 * va 	: virtual address.
 * pte 	: pte entries inserted.
 * log_size: range to be covered.
 *
 * Return value:  <0 :  error No.
 *
 *		  >=0 : slot number allocated for TR.
 * Must be called with preemption disabled.
 */
int ia64_itr_entry(u64 target_mask, u64 va, u64 pte, u64 log_size)
{
	int i, r;
	unsigned long psr;
	struct ia64_tr_entry *p;
	int cpu = smp_processor_id();

	if (!ia64_idtrs[cpu]) {
		ia64_idtrs[cpu] = kmalloc_array(2 * IA64_TR_ALLOC_MAX,
						sizeof(struct ia64_tr_entry),
						GFP_KERNEL);
		if (!ia64_idtrs[cpu])
			return -ENOMEM;
	}
	r = -EINVAL;
	/*Check overlap with existing TR entries*/
	if (target_mask & 0x1) {
		p = ia64_idtrs[cpu];
		for (i = IA64_TR_ALLOC_BASE; i <= per_cpu(ia64_tr_used, cpu);
								i++, p++) {
			if (p->pte & 0x1)
				if (is_tr_overlap(p, va, log_size)) {
					printk(KERN_DEBUG "Overlapped Entry"
						"Inserted for TR Register!!\n");
					goto out;
			}
		}
	}
	if (target_mask & 0x2) {
		p = ia64_idtrs[cpu] + IA64_TR_ALLOC_MAX;
		for (i = IA64_TR_ALLOC_BASE; i <= per_cpu(ia64_tr_used, cpu);
								i++, p++) {
			if (p->pte & 0x1)
				if (is_tr_overlap(p, va, log_size)) {
					printk(KERN_DEBUG "Overlapped Entry"
						"Inserted for TR Register!!\n");
					goto out;
				}
		}
	}

	for (i = IA64_TR_ALLOC_BASE; i < per_cpu(ia64_tr_num, cpu); i++) {
		switch (target_mask & 0x3) {
		case 1:
			if (!((ia64_idtrs[cpu] + i)->pte & 0x1))
				goto found;
			continue;
		case 2:
			if (!((ia64_idtrs[cpu] + IA64_TR_ALLOC_MAX + i)->pte & 0x1))
				goto found;
			continue;
		case 3:
			if (!((ia64_idtrs[cpu] + i)->pte & 0x1) &&
			    !((ia64_idtrs[cpu] + IA64_TR_ALLOC_MAX + i)->pte & 0x1))
				goto found;
			continue;
		default:
			r = -EINVAL;
			goto out;
		}
	}
found:
	if (i >= per_cpu(ia64_tr_num, cpu))
		return -EBUSY;

	/*Record tr info for mca hander use!*/
	if (i > per_cpu(ia64_tr_used, cpu))
		per_cpu(ia64_tr_used, cpu) = i;

	psr = ia64_clear_ic();
	if (target_mask & 0x1) {
		ia64_itr(0x1, i, va, pte, log_size);
		ia64_srlz_i();
		p = ia64_idtrs[cpu] + i;
		p->ifa = va;
		p->pte = pte;
		p->itir = log_size << 2;
		p->rr = ia64_get_rr(va);
	}
	if (target_mask & 0x2) {
		ia64_itr(0x2, i, va, pte, log_size);
		ia64_srlz_i();
		p = ia64_idtrs[cpu] + IA64_TR_ALLOC_MAX + i;
		p->ifa = va;
		p->pte = pte;
		p->itir = log_size << 2;
		p->rr = ia64_get_rr(va);
	}
	ia64_set_psr(psr);
	r = i;
out:
	return r;
}
EXPORT_SYMBOL_GPL(ia64_itr_entry);

/*
 * ia64_purge_tr
 *
 * target_mask: 0x1: purge itr, 0x2 : purge dtr, 0x3 purge idtr.
 * slot: slot number to be freed.
 *
 * Must be called with preemption disabled.
 */
void ia64_ptr_entry(u64 target_mask, int slot)
{
	int cpu = smp_processor_id();
	int i;
	struct ia64_tr_entry *p;

	if (slot < IA64_TR_ALLOC_BASE || slot >= per_cpu(ia64_tr_num, cpu))
		return;

	if (target_mask & 0x1) {
		p = ia64_idtrs[cpu] + slot;
		if ((p->pte&0x1) && is_tr_overlap(p, p->ifa, p->itir>>2)) {
			p->pte = 0;
			ia64_ptr(0x1, p->ifa, p->itir>>2);
			ia64_srlz_i();
		}
	}

	if (target_mask & 0x2) {
		p = ia64_idtrs[cpu] + IA64_TR_ALLOC_MAX + slot;
		if ((p->pte & 0x1) && is_tr_overlap(p, p->ifa, p->itir>>2)) {
			p->pte = 0;
			ia64_ptr(0x2, p->ifa, p->itir>>2);
			ia64_srlz_i();
		}
	}

	for (i = per_cpu(ia64_tr_used, cpu); i >= IA64_TR_ALLOC_BASE; i--) {
		if (((ia64_idtrs[cpu] + i)->pte & 0x1) ||
		    ((ia64_idtrs[cpu] + IA64_TR_ALLOC_MAX + i)->pte & 0x1))
			break;
	}
	per_cpu(ia64_tr_used, cpu) = i;
}
EXPORT_SYMBOL_GPL(ia64_ptr_entry);
