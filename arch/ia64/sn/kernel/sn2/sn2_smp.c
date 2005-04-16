/*
 * SN2 Platform specific SMP Support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/nodemask.h>

#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/tlb.h>
#include <asm/numa.h>
#include <asm/hw_irq.h>
#include <asm/current.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/addrs.h>
#include <asm/sn/shub_mmr.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/rw_mmr.h>

void sn2_ptc_deadlock_recovery(volatile unsigned long *, unsigned long data0, 
	volatile unsigned long *, unsigned long data1);

static  __cacheline_aligned DEFINE_SPINLOCK(sn2_global_ptc_lock);

static unsigned long sn2_ptc_deadlock_count;

static inline unsigned long wait_piowc(void)
{
	volatile unsigned long *piows, zeroval;
	unsigned long ws;

	piows = pda->pio_write_status_addr;
	zeroval = pda->pio_write_status_val;
	do {
		cpu_relax();
	} while (((ws = *piows) & SH_PIO_WRITE_STATUS_PENDING_WRITE_COUNT_MASK) != zeroval);
	return ws;
}

void sn_tlb_migrate_finish(struct mm_struct *mm)
{
	if (mm == current->mm)
		flush_tlb_mm(mm);
}

/**
 * sn2_global_tlb_purge - globally purge translation cache of virtual address range
 * @start: start of virtual address range
 * @end: end of virtual address range
 * @nbits: specifies number of bytes to purge per instruction (num = 1<<(nbits & 0xfc))
 *
 * Purges the translation caches of all processors of the given virtual address
 * range.
 *
 * Note:
 * 	- cpu_vm_mask is a bit mask that indicates which cpus have loaded the context.
 * 	- cpu_vm_mask is converted into a nodemask of the nodes containing the
 * 	  cpus in cpu_vm_mask.
 *	- if only one bit is set in cpu_vm_mask & it is the current cpu,
 *	  then only the local TLB needs to be flushed. This flushing can be done
 *	  using ptc.l. This is the common case & avoids the global spinlock.
 *	- if multiple cpus have loaded the context, then flushing has to be
 *	  done with ptc.g/MMRs under protection of the global ptc_lock.
 */

void
sn2_global_tlb_purge(unsigned long start, unsigned long end,
		     unsigned long nbits)
{
	int i, shub1, cnode, mynasid, cpu, lcpu = 0, nasid, flushed = 0;
	volatile unsigned long *ptc0, *ptc1;
	unsigned long flags = 0, data0 = 0, data1 = 0;
	struct mm_struct *mm = current->active_mm;
	short nasids[MAX_NUMNODES], nix;
	nodemask_t nodes_flushed;

	nodes_clear(nodes_flushed);
	i = 0;

	for_each_cpu_mask(cpu, mm->cpu_vm_mask) {
		cnode = cpu_to_node(cpu);
		node_set(cnode, nodes_flushed);
		lcpu = cpu;
		i++;
	}

	preempt_disable();

	if (likely(i == 1 && lcpu == smp_processor_id())) {
		do {
			ia64_ptcl(start, nbits << 2);
			start += (1UL << nbits);
		} while (start < end);
		ia64_srlz_i();
		preempt_enable();
		return;
	}

	if (atomic_read(&mm->mm_users) == 1) {
		flush_tlb_mm(mm);
		preempt_enable();
		return;
	}

	nix = 0;
	for_each_node_mask(cnode, nodes_flushed)
		nasids[nix++] = cnodeid_to_nasid(cnode);

	shub1 = is_shub1();
	if (shub1) {
		data0 = (1UL << SH1_PTC_0_A_SHFT) |
		    	(nbits << SH1_PTC_0_PS_SHFT) |
		    	((ia64_get_rr(start) >> 8) << SH1_PTC_0_RID_SHFT) |
		    	(1UL << SH1_PTC_0_START_SHFT);
		ptc0 = (long *)GLOBAL_MMR_PHYS_ADDR(0, SH1_PTC_0);
		ptc1 = (long *)GLOBAL_MMR_PHYS_ADDR(0, SH1_PTC_1);
	} else {
		data0 = (1UL << SH2_PTC_A_SHFT) |
			(nbits << SH2_PTC_PS_SHFT) |
		    	(1UL << SH2_PTC_START_SHFT);
		ptc0 = (long *)GLOBAL_MMR_PHYS_ADDR(0, SH2_PTC + 
			((ia64_get_rr(start) >> 8) << SH2_PTC_RID_SHFT) );
		ptc1 = NULL;
	}
	

	mynasid = get_nasid();

	spin_lock_irqsave(&sn2_global_ptc_lock, flags);

	do {
		if (shub1)
			data1 = start | (1UL << SH1_PTC_1_START_SHFT);
		else
			data0 = (data0 & ~SH2_PTC_ADDR_MASK) | (start & SH2_PTC_ADDR_MASK);
		for (i = 0; i < nix; i++) {
			nasid = nasids[i];
			if (unlikely(nasid == mynasid)) {
				ia64_ptcga(start, nbits << 2);
				ia64_srlz_i();
			} else {
				ptc0 = CHANGE_NASID(nasid, ptc0);
				if (ptc1)
					ptc1 = CHANGE_NASID(nasid, ptc1);
				pio_atomic_phys_write_mmrs(ptc0, data0, ptc1,
							   data1);
				flushed = 1;
			}
		}

		if (flushed
		    && (wait_piowc() &
			SH_PIO_WRITE_STATUS_WRITE_DEADLOCK_MASK)) {
			sn2_ptc_deadlock_recovery(ptc0, data0, ptc1, data1);
		}

		start += (1UL << nbits);

	} while (start < end);

	spin_unlock_irqrestore(&sn2_global_ptc_lock, flags);

	preempt_enable();
}

/*
 * sn2_ptc_deadlock_recovery
 *
 * Recover from PTC deadlocks conditions. Recovery requires stepping thru each 
 * TLB flush transaction.  The recovery sequence is somewhat tricky & is
 * coded in assembly language.
 */
void sn2_ptc_deadlock_recovery(volatile unsigned long *ptc0, unsigned long data0,
	volatile unsigned long *ptc1, unsigned long data1)
{
	extern void sn2_ptc_deadlock_recovery_core(volatile unsigned long *, unsigned long,
	        volatile unsigned long *, unsigned long, volatile unsigned long *, unsigned long);
	int cnode, mycnode, nasid;
	volatile unsigned long *piows;
	volatile unsigned long zeroval;

	sn2_ptc_deadlock_count++;

	piows = pda->pio_write_status_addr;
	zeroval = pda->pio_write_status_val;

	mycnode = numa_node_id();

	for_each_online_node(cnode) {
		if (is_headless_node(cnode) || cnode == mycnode)
			continue;
		nasid = cnodeid_to_nasid(cnode);
		ptc0 = CHANGE_NASID(nasid, ptc0);
		if (ptc1)
			ptc1 = CHANGE_NASID(nasid, ptc1);
		sn2_ptc_deadlock_recovery_core(ptc0, data0, ptc1, data1, piows, zeroval);
	}
}

/**
 * sn_send_IPI_phys - send an IPI to a Nasid and slice
 * @nasid: nasid to receive the interrupt (may be outside partition)
 * @physid: physical cpuid to receive the interrupt.
 * @vector: command to send
 * @delivery_mode: delivery mechanism
 *
 * Sends an IPI (interprocessor interrupt) to the processor specified by
 * @physid
 *
 * @delivery_mode can be one of the following
 *
 * %IA64_IPI_DM_INT - pend an interrupt
 * %IA64_IPI_DM_PMI - pend a PMI
 * %IA64_IPI_DM_NMI - pend an NMI
 * %IA64_IPI_DM_INIT - pend an INIT interrupt
 */
void sn_send_IPI_phys(int nasid, long physid, int vector, int delivery_mode)
{
	long val;
	unsigned long flags = 0;
	volatile long *p;

	p = (long *)GLOBAL_MMR_PHYS_ADDR(nasid, SH_IPI_INT);
	val = (1UL << SH_IPI_INT_SEND_SHFT) |
	    (physid << SH_IPI_INT_PID_SHFT) |
	    ((long)delivery_mode << SH_IPI_INT_TYPE_SHFT) |
	    ((long)vector << SH_IPI_INT_IDX_SHFT) |
	    (0x000feeUL << SH_IPI_INT_BASE_SHFT);

	mb();
	if (enable_shub_wars_1_1()) {
		spin_lock_irqsave(&sn2_global_ptc_lock, flags);
	}
	pio_phys_write_mmr(p, val);
	if (enable_shub_wars_1_1()) {
		wait_piowc();
		spin_unlock_irqrestore(&sn2_global_ptc_lock, flags);
	}

}

EXPORT_SYMBOL(sn_send_IPI_phys);

/**
 * sn2_send_IPI - send an IPI to a processor
 * @cpuid: target of the IPI
 * @vector: command to send
 * @delivery_mode: delivery mechanism
 * @redirect: redirect the IPI?
 *
 * Sends an IPI (InterProcessor Interrupt) to the processor specified by
 * @cpuid.  @vector specifies the command to send, while @delivery_mode can 
 * be one of the following
 *
 * %IA64_IPI_DM_INT - pend an interrupt
 * %IA64_IPI_DM_PMI - pend a PMI
 * %IA64_IPI_DM_NMI - pend an NMI
 * %IA64_IPI_DM_INIT - pend an INIT interrupt
 */
void sn2_send_IPI(int cpuid, int vector, int delivery_mode, int redirect)
{
	long physid;
	int nasid;

	physid = cpu_physical_id(cpuid);
	nasid = cpuid_to_nasid(cpuid);

	/* the following is used only when starting cpus at boot time */
	if (unlikely(nasid == -1))
		ia64_sn_get_sapic_info(physid, &nasid, NULL, NULL);

	sn_send_IPI_phys(nasid, physid, vector, delivery_mode);
}
