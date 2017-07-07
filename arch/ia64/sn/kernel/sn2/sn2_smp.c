/*
 * SN2 Platform specific SMP Support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2006 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/sched.h>
#include <linux/mm_types.h>
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
#include <asm/sn/sn_feature_sets.h>

DEFINE_PER_CPU(struct ptc_stats, ptcstats);
DECLARE_PER_CPU(struct ptc_stats, ptcstats);

static  __cacheline_aligned DEFINE_SPINLOCK(sn2_global_ptc_lock);

/* 0 = old algorithm (no IPI flushes), 1 = ipi deadlock flush, 2 = ipi instead of SHUB ptc, >2 = always ipi */
static int sn2_flush_opt = 0;

extern unsigned long
sn2_ptc_deadlock_recovery_core(volatile unsigned long *, unsigned long,
			       volatile unsigned long *, unsigned long,
			       volatile unsigned long *, unsigned long);
void
sn2_ptc_deadlock_recovery(nodemask_t, short, short, int,
			  volatile unsigned long *, unsigned long,
			  volatile unsigned long *, unsigned long);

/*
 * Note: some is the following is captured here to make degugging easier
 * (the macros make more sense if you see the debug patch - not posted)
 */
#define sn2_ptctest	0
#define local_node_uses_ptc_ga(sh1)	((sh1) ? 1 : 0)
#define max_active_pio(sh1)		((sh1) ? 32 : 7)
#define reset_max_active_on_deadlock()	1
#define PTC_LOCK(sh1)			((sh1) ? &sn2_global_ptc_lock : &sn_nodepda->ptc_lock)

struct ptc_stats {
	unsigned long ptc_l;
	unsigned long change_rid;
	unsigned long shub_ptc_flushes;
	unsigned long nodes_flushed;
	unsigned long deadlocks;
	unsigned long deadlocks2;
	unsigned long lock_itc_clocks;
	unsigned long shub_itc_clocks;
	unsigned long shub_itc_clocks_max;
	unsigned long shub_ptc_flushes_not_my_mm;
	unsigned long shub_ipi_flushes;
	unsigned long shub_ipi_flushes_itc_clocks;
};

#define sn2_ptctest	0

static inline unsigned long wait_piowc(void)
{
	volatile unsigned long *piows;
	unsigned long zeroval, ws;

	piows = pda->pio_write_status_addr;
	zeroval = pda->pio_write_status_val;
	do {
		cpu_relax();
	} while (((ws = *piows) & SH_PIO_WRITE_STATUS_PENDING_WRITE_COUNT_MASK) != zeroval);
	return (ws & SH_PIO_WRITE_STATUS_WRITE_DEADLOCK_MASK) != 0;
}

/**
 * sn_migrate - SN-specific task migration actions
 * @task: Task being migrated to new CPU
 *
 * SN2 PIO writes from separate CPUs are not guaranteed to arrive in order.
 * Context switching user threads which have memory-mapped MMIO may cause
 * PIOs to issue from separate CPUs, thus the PIO writes must be drained
 * from the previous CPU's Shub before execution resumes on the new CPU.
 */
void sn_migrate(struct task_struct *task)
{
	pda_t *last_pda = pdacpu(task_thread_info(task)->last_cpu);
	volatile unsigned long *adr = last_pda->pio_write_status_addr;
	unsigned long val = last_pda->pio_write_status_val;

	/* Drain PIO writes from old CPU's Shub */
	while (unlikely((*adr & SH_PIO_WRITE_STATUS_PENDING_WRITE_COUNT_MASK)
			!= val))
		cpu_relax();
}

void sn_tlb_migrate_finish(struct mm_struct *mm)
{
	/* flush_tlb_mm is inefficient if more than 1 users of mm */
	if (mm == current->mm && mm && atomic_read(&mm->mm_users) == 1)
		flush_tlb_mm(mm);
}

static void
sn2_ipi_flush_all_tlb(struct mm_struct *mm)
{
	unsigned long itc;

	itc = ia64_get_itc();
	smp_flush_tlb_cpumask(*mm_cpumask(mm));
	itc = ia64_get_itc() - itc;
	__this_cpu_add(ptcstats.shub_ipi_flushes_itc_clocks, itc);
	__this_cpu_inc(ptcstats.shub_ipi_flushes);
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
	int i, ibegin, shub1, cnode, mynasid, cpu, lcpu = 0, nasid;
	int mymm = (mm == current->active_mm && mm == current->mm);
	int use_cpu_ptcga;
	volatile unsigned long *ptc0, *ptc1;
	unsigned long itc, itc2, flags, data0 = 0, data1 = 0, rr_value, old_rr = 0;
	short nix;
	nodemask_t nodes_flushed;
	int active, max_active, deadlock, flush_opt = sn2_flush_opt;

	if (flush_opt > 2) {
		sn2_ipi_flush_all_tlb(mm);
		return;
	}

	nodes_clear(nodes_flushed);
	i = 0;

	for_each_cpu(cpu, mm_cpumask(mm)) {
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
		__this_cpu_inc(ptcstats.ptc_l);
		preempt_enable();
		return;
	}

	if (atomic_read(&mm->mm_users) == 1 && mymm) {
		flush_tlb_mm(mm);
		__this_cpu_inc(ptcstats.change_rid);
		preempt_enable();
		return;
	}

	if (flush_opt == 2) {
		sn2_ipi_flush_all_tlb(mm);
		preempt_enable();
		return;
	}

	itc = ia64_get_itc();
	nix = nodes_weight(nodes_flushed);

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
	use_cpu_ptcga = local_node_uses_ptc_ga(shub1);
	max_active = max_active_pio(shub1);

	itc = ia64_get_itc();
	spin_lock_irqsave(PTC_LOCK(shub1), flags);
	itc2 = ia64_get_itc();

	__this_cpu_add(ptcstats.lock_itc_clocks, itc2 - itc);
	__this_cpu_inc(ptcstats.shub_ptc_flushes);
	__this_cpu_add(ptcstats.nodes_flushed, nix);
	if (!mymm)
		 __this_cpu_inc(ptcstats.shub_ptc_flushes_not_my_mm);

	if (use_cpu_ptcga && !mymm) {
		old_rr = ia64_get_rr(start);
		ia64_set_rr(start, (old_rr & 0xff) | (rr_value << 8));
		ia64_srlz_d();
	}

	wait_piowc();
	do {
		if (shub1)
			data1 = start | (1UL << SH1_PTC_1_START_SHFT);
		else
			data0 = (data0 & ~SH2_PTC_ADDR_MASK) | (start & SH2_PTC_ADDR_MASK);
		deadlock = 0;
		active = 0;
		ibegin = 0;
		i = 0;
		for_each_node_mask(cnode, nodes_flushed) {
			nasid = cnodeid_to_nasid(cnode);
			if (use_cpu_ptcga && unlikely(nasid == mynasid)) {
				ia64_ptcga(start, nbits << 2);
				ia64_srlz_i();
			} else {
				ptc0 = CHANGE_NASID(nasid, ptc0);
				if (ptc1)
					ptc1 = CHANGE_NASID(nasid, ptc1);
				pio_atomic_phys_write_mmrs(ptc0, data0, ptc1, data1);
				active++;
			}
			if (active >= max_active || i == (nix - 1)) {
				if ((deadlock = wait_piowc())) {
					if (flush_opt == 1)
						goto done;
					sn2_ptc_deadlock_recovery(nodes_flushed, ibegin, i, mynasid, ptc0, data0, ptc1, data1);
					if (reset_max_active_on_deadlock())
						max_active = 1;
				}
				active = 0;
				ibegin = i + 1;
			}
			i++;
		}
		start += (1UL << nbits);
	} while (start < end);

done:
	itc2 = ia64_get_itc() - itc2;
	__this_cpu_add(ptcstats.shub_itc_clocks, itc2);
	if (itc2 > __this_cpu_read(ptcstats.shub_itc_clocks_max))
		__this_cpu_write(ptcstats.shub_itc_clocks_max, itc2);

	if (old_rr) {
		ia64_set_rr(start, old_rr);
		ia64_srlz_d();
	}

	spin_unlock_irqrestore(PTC_LOCK(shub1), flags);

	if (flush_opt == 1 && deadlock) {
		__this_cpu_inc(ptcstats.deadlocks);
		sn2_ipi_flush_all_tlb(mm);
	}

	preempt_enable();
}

/*
 * sn2_ptc_deadlock_recovery
 *
 * Recover from PTC deadlocks conditions. Recovery requires stepping thru each 
 * TLB flush transaction.  The recovery sequence is somewhat tricky & is
 * coded in assembly language.
 */

void
sn2_ptc_deadlock_recovery(nodemask_t nodes, short ib, short ie, int mynasid,
			  volatile unsigned long *ptc0, unsigned long data0,
			  volatile unsigned long *ptc1, unsigned long data1)
{
	short nasid, i;
	int cnode;
	unsigned long *piows, zeroval, n;

	__this_cpu_inc(ptcstats.deadlocks);

	piows = (unsigned long *) pda->pio_write_status_addr;
	zeroval = pda->pio_write_status_val;

	i = 0;
	for_each_node_mask(cnode, nodes) {
		if (i < ib)
			goto next;

		if (i > ie)
			break;

		nasid = cnodeid_to_nasid(cnode);
		if (local_node_uses_ptc_ga(is_shub1()) && nasid == mynasid)
			goto next;

		ptc0 = CHANGE_NASID(nasid, ptc0);
		if (ptc1)
			ptc1 = CHANGE_NASID(nasid, ptc1);

		n = sn2_ptc_deadlock_recovery_core(ptc0, data0, ptc1, data1, piows, zeroval);
		__this_cpu_add(ptcstats.deadlocks2, n);
next:
		i++;
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

#ifdef CONFIG_HOTPLUG_CPU
/**
 * sn_cpu_disable_allowed - Determine if a CPU can be disabled.
 * @cpu - CPU that is requested to be disabled.
 *
 * CPU disable is only allowed on SHub2 systems running with a PROM
 * that supports CPU disable. It is not permitted to disable the boot processor.
 */
bool sn_cpu_disable_allowed(int cpu)
{
	if (is_shub2() && sn_prom_feature_available(PRF_CPU_DISABLE_SUPPORT)) {
		if (cpu != 0)
			return true;
		else
			printk(KERN_WARNING
			      "Disabling the boot processor is not allowed.\n");

	} else
		printk(KERN_WARNING
		       "CPU disable is not supported on this system.\n");

	return false;
}
#endif /* CONFIG_HOTPLUG_CPU */

#ifdef CONFIG_PROC_FS

#define PTC_BASENAME	"sgi_sn/ptc_statistics"

static void *sn2_ptc_seq_start(struct seq_file *file, loff_t * offset)
{
	if (*offset < nr_cpu_ids)
		return offset;
	return NULL;
}

static void *sn2_ptc_seq_next(struct seq_file *file, void *data, loff_t * offset)
{
	(*offset)++;
	if (*offset < nr_cpu_ids)
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
		seq_printf(file,
			   "# cpu ptc_l newrid ptc_flushes nodes_flushed deadlocks lock_nsec shub_nsec shub_nsec_max not_my_mm deadlock2 ipi_fluches ipi_nsec\n");
		seq_printf(file, "# ptctest %d, flushopt %d\n", sn2_ptctest, sn2_flush_opt);
	}

	if (cpu < nr_cpu_ids && cpu_online(cpu)) {
		stat = &per_cpu(ptcstats, cpu);
		seq_printf(file, "cpu %d %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld\n", cpu, stat->ptc_l,
				stat->change_rid, stat->shub_ptc_flushes, stat->nodes_flushed,
				stat->deadlocks,
				1000 * stat->lock_itc_clocks / per_cpu(ia64_cpu_info, cpu).cyc_per_usec,
				1000 * stat->shub_itc_clocks / per_cpu(ia64_cpu_info, cpu).cyc_per_usec,
				1000 * stat->shub_itc_clocks_max / per_cpu(ia64_cpu_info, cpu).cyc_per_usec,
				stat->shub_ptc_flushes_not_my_mm,
				stat->deadlocks2,
				stat->shub_ipi_flushes,
				1000 * stat->shub_ipi_flushes_itc_clocks / per_cpu(ia64_cpu_info, cpu).cyc_per_usec);
	}
	return 0;
}

static ssize_t sn2_ptc_proc_write(struct file *file, const char __user *user, size_t count, loff_t *data)
{
	int cpu;
	char optstr[64];

	if (count == 0 || count > sizeof(optstr))
		return -EINVAL;
	if (copy_from_user(optstr, user, count))
		return -EFAULT;
	optstr[count - 1] = '\0';
	sn2_flush_opt = simple_strtoul(optstr, NULL, 0);

	for_each_online_cpu(cpu)
		memset(&per_cpu(ptcstats, cpu), 0, sizeof(struct ptc_stats));

	return count;
}

static const struct seq_operations sn2_ptc_seq_ops = {
	.start = sn2_ptc_seq_start,
	.next = sn2_ptc_seq_next,
	.stop = sn2_ptc_seq_stop,
	.show = sn2_ptc_seq_show
};

static int sn2_ptc_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &sn2_ptc_seq_ops);
}

static const struct file_operations proc_sn2_ptc_operations = {
	.open = sn2_ptc_proc_open,
	.read = seq_read,
	.write = sn2_ptc_proc_write,
	.llseek = seq_lseek,
	.release = seq_release,
};

static struct proc_dir_entry *proc_sn2_ptc;

static int __init sn2_ptc_init(void)
{
	if (!ia64_platform_is("sn2"))
		return 0;

	proc_sn2_ptc = proc_create(PTC_BASENAME, 0444,
				   NULL, &proc_sn2_ptc_operations);
	if (!proc_sn2_ptc) {
		printk(KERN_ERR "unable to create %s proc entry", PTC_BASENAME);
		return -EINVAL;
	}
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

