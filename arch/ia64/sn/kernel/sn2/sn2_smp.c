/*
 * SN2 Platform specific SMP Support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2005 Silicon Graphics, Inc. All rights reserved.
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
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

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

DEFINE_PER_CPU(struct ptc_stats, ptcstats);
DECLARE_PER_CPU(struct ptc_stats, ptcstats);

static  __cacheline_aligned DEFINE_SPINLOCK(sn2_global_ptc_lock);

void sn2_ptc_deadlock_recovery(short *, short, int, volatile unsigned long *, unsigned long data0,
	volatile unsigned long *, unsigned long data1);

#ifdef DEBUG_PTC
/*
 * ptctest:
 *
 * 	xyz - 3 digit hex number:
 * 		x - Force PTC purges to use shub:
 * 			0 - no force
 * 			1 - force
 * 		y - interupt enable
 * 			0 - disable interrupts
 * 			1 - leave interuupts enabled
 * 		z - type of lock:
 * 			0 - global lock
 * 			1 - node local lock
 * 			2 - no lock
 *
 *   	Note: on shub1, only ptctest == 0 is supported. Don't try other values!
 */

static unsigned int sn2_ptctest = 0;

static int __init ptc_test(char *str)
{
	get_option(&str, &sn2_ptctest);
	return 1;
}
__setup("ptctest=", ptc_test);

static inline int ptc_lock(unsigned long *flagp)
{
	unsigned long opt = sn2_ptctest & 255;

	switch (opt) {
	case 0x00:
		spin_lock_irqsave(&sn2_global_ptc_lock, *flagp);
		break;
	case 0x01:
		spin_lock_irqsave(&sn_nodepda->ptc_lock, *flagp);
		break;
	case 0x02:
		local_irq_save(*flagp);
		break;
	case 0x10:
		spin_lock(&sn2_global_ptc_lock);
		break;
	case 0x11:
		spin_lock(&sn_nodepda->ptc_lock);
		break;
	case 0x12:
		break;
	default:
		BUG();
	}
	return opt;
}

static inline void ptc_unlock(unsigned long flags, int opt)
{
	switch (opt) {
	case 0x00:
		spin_unlock_irqrestore(&sn2_global_ptc_lock, flags);
		break;
	case 0x01:
		spin_unlock_irqrestore(&sn_nodepda->ptc_lock, flags);
		break;
	case 0x02:
		local_irq_restore(flags);
		break;
	case 0x10:
		spin_unlock(&sn2_global_ptc_lock);
		break;
	case 0x11:
		spin_unlock(&sn_nodepda->ptc_lock);
		break;
	case 0x12:
		break;
	default:
		BUG();
	}
}
#else

#define sn2_ptctest	0

static inline int ptc_lock(unsigned long *flagp)
{
	spin_lock_irqsave(&sn2_global_ptc_lock, *flagp);
	return 0;
}

static inline void ptc_unlock(unsigned long flags, int opt)
{
	spin_unlock_irqrestore(&sn2_global_ptc_lock, flags);
}
#endif

struct ptc_stats {
	unsigned long ptc_l;
	unsigned long change_rid;
	unsigned long shub_ptc_flushes;
	unsigned long nodes_flushed;
	unsigned long deadlocks;
	unsigned long lock_itc_clocks;
	unsigned long shub_itc_clocks;
	unsigned long shub_itc_clocks_max;
};

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
 * @mm: mm_struct containing virtual address range
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
 *	- if only one bit is set in cpu_vm_mask & it is the current cpu & the
 *	  process is purging its own virtual address range, then only the
 *	  local TLB needs to be flushed. This flushing can be done using
 *	  ptc.l. This is the common case & avoids the global spinlock.
 *	- if multiple cpus have loaded the context, then flushing has to be
 *	  done with ptc.g/MMRs under protection of the global ptc_lock.
 */

void
sn2_global_tlb_purge(struct mm_struct *mm, unsigned long start,
		     unsigned long end, unsigned long nbits)
{
	int i, opt, shub1, cnode, mynasid, cpu, lcpu = 0, nasid, flushed = 0;
	int mymm = (mm == current->active_mm && current->mm);
	volatile unsigned long *ptc0, *ptc1;
	unsigned long itc, itc2, flags, data0 = 0, data1 = 0, rr_value;
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

	if (i == 0)
		return;

	preempt_disable();

	if (likely(i == 1 && lcpu == smp_processor_id() && mymm)) {
		do {
			ia64_ptcl(start, nbits << 2);
			start += (1UL << nbits);
		} while (start < end);
		ia64_srlz_i();
		__get_cpu_var(ptcstats).ptc_l++;
		preempt_enable();
		return;
	}

	if (atomic_read(&mm->mm_users) == 1 && mymm) {
		flush_tlb_mm(mm);
		__get_cpu_var(ptcstats).change_rid++;
		preempt_enable();
		return;
	}

	itc = ia64_get_itc();
	nix = 0;
	for_each_node_mask(cnode, nodes_flushed)
		nasids[nix++] = cnodeid_to_nasid(cnode);

	rr_value = (mm->context << 3) | REGION_NUMBER(start);

	shub1 = is_shub1();
	if (shub1) {
		data0 = (1UL << SH1_PTC_0_A_SHFT) |
		    	(nbits << SH1_PTC_0_PS_SHFT) |
			(rr_value << SH1_PTC_0_RID_SHFT) |
		    	(1UL << SH1_PTC_0_START_SHFT);
		ptc0 = (long *)GLOBAL_MMR_PHYS_ADDR(0, SH1_PTC_0);
		ptc1 = (long *)GLOBAL_MMR_PHYS_ADDR(0, SH1_PTC_1);
	} else {
		data0 = (1UL << SH2_PTC_A_SHFT) |
			(nbits << SH2_PTC_PS_SHFT) |
		    	(1UL << SH2_PTC_START_SHFT);
		ptc0 = (long *)GLOBAL_MMR_PHYS_ADDR(0, SH2_PTC + 
			(rr_value << SH2_PTC_RID_SHFT));
		ptc1 = NULL;
	}
	

	mynasid = get_nasid();

	itc = ia64_get_itc();
	opt = ptc_lock(&flags);
	itc2 = ia64_get_itc();
	__get_cpu_var(ptcstats).lock_itc_clocks += itc2 - itc;
	__get_cpu_var(ptcstats).shub_ptc_flushes++;
	__get_cpu_var(ptcstats).nodes_flushed += nix;

	do {
		if (shub1)
			data1 = start | (1UL << SH1_PTC_1_START_SHFT);
		else
			data0 = (data0 & ~SH2_PTC_ADDR_MASK) | (start & SH2_PTC_ADDR_MASK);
		for (i = 0; i < nix; i++) {
			nasid = nasids[i];
			if ((!(sn2_ptctest & 3)) && unlikely(nasid == mynasid && mymm)) {
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
				(SH_PIO_WRITE_STATUS_WRITE_DEADLOCK_MASK))) {
			sn2_ptc_deadlock_recovery(nasids, nix, mynasid, ptc0, data0, ptc1, data1);
		}

		start += (1UL << nbits);

	} while (start < end);

	itc2 = ia64_get_itc() - itc2;
	__get_cpu_var(ptcstats).shub_itc_clocks += itc2;
	if (itc2 > __get_cpu_var(ptcstats).shub_itc_clocks_max)
		__get_cpu_var(ptcstats).shub_itc_clocks_max = itc2;

	ptc_unlock(flags, opt);

	preempt_enable();
}

/*
 * sn2_ptc_deadlock_recovery
 *
 * Recover from PTC deadlocks conditions. Recovery requires stepping thru each 
 * TLB flush transaction.  The recovery sequence is somewhat tricky & is
 * coded in assembly language.
 */
void sn2_ptc_deadlock_recovery(short *nasids, short nix, int mynasid, volatile unsigned long *ptc0, unsigned long data0,
	volatile unsigned long *ptc1, unsigned long data1)
{
	extern void sn2_ptc_deadlock_recovery_core(volatile unsigned long *, unsigned long,
	        volatile unsigned long *, unsigned long, volatile unsigned long *, unsigned long);
	short nasid, i;
	unsigned long *piows, zeroval;

	__get_cpu_var(ptcstats).deadlocks++;

	piows = (unsigned long *) pda->pio_write_status_addr;
	zeroval = pda->pio_write_status_val;

	for (i=0; i < nix; i++) {
		nasid = nasids[i];
		if (!(sn2_ptctest & 3) && nasid == mynasid)
			continue;
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

#ifdef CONFIG_PROC_FS

#define PTC_BASENAME	"sgi_sn/ptc_statistics"

static void *sn2_ptc_seq_start(struct seq_file *file, loff_t * offset)
{
	if (*offset < NR_CPUS)
		return offset;
	return NULL;
}

static void *sn2_ptc_seq_next(struct seq_file *file, void *data, loff_t * offset)
{
	(*offset)++;
	if (*offset < NR_CPUS)
		return offset;
	return NULL;
}

static void sn2_ptc_seq_stop(struct seq_file *file, void *data)
{
}

static int sn2_ptc_seq_show(struct seq_file *file, void *data)
{
	struct ptc_stats *stat;
	int cpu;

	cpu = *(loff_t *) data;

	if (!cpu) {
		seq_printf(file, "# ptc_l change_rid shub_ptc_flushes shub_nodes_flushed deadlocks lock_nsec shub_nsec shub_nsec_max\n");
		seq_printf(file, "# ptctest %d\n", sn2_ptctest);
	}

	if (cpu < NR_CPUS && cpu_online(cpu)) {
		stat = &per_cpu(ptcstats, cpu);
		seq_printf(file, "cpu %d %ld %ld %ld %ld %ld %ld %ld %ld\n", cpu, stat->ptc_l,
				stat->change_rid, stat->shub_ptc_flushes, stat->nodes_flushed,
				stat->deadlocks,
				1000 * stat->lock_itc_clocks / per_cpu(cpu_info, cpu).cyc_per_usec,
				1000 * stat->shub_itc_clocks / per_cpu(cpu_info, cpu).cyc_per_usec,
				1000 * stat->shub_itc_clocks_max / per_cpu(cpu_info, cpu).cyc_per_usec);
	}

	return 0;
}

static struct seq_operations sn2_ptc_seq_ops = {
	.start = sn2_ptc_seq_start,
	.next = sn2_ptc_seq_next,
	.stop = sn2_ptc_seq_stop,
	.show = sn2_ptc_seq_show
};

int sn2_ptc_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &sn2_ptc_seq_ops);
}

static struct file_operations proc_sn2_ptc_operations = {
	.open = sn2_ptc_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static struct proc_dir_entry *proc_sn2_ptc;

static int __init sn2_ptc_init(void)
{
	if (!ia64_platform_is("sn2"))
		return -ENOSYS;

	if (!(proc_sn2_ptc = create_proc_entry(PTC_BASENAME, 0444, NULL))) {
		printk(KERN_ERR "unable to create %s proc entry", PTC_BASENAME);
		return -EINVAL;
	}
	proc_sn2_ptc->proc_fops = &proc_sn2_ptc_operations;
	spin_lock_init(&sn2_global_ptc_lock);
	return 0;
}

static void __exit sn2_ptc_exit(void)
{
	remove_proc_entry(PTC_BASENAME, NULL);
}

module_init(sn2_ptc_init);
module_exit(sn2_ptc_exit);
#endif /* CONFIG_PROC_FS */

