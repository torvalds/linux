// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Andy Fleming <afleming@freescale.com>
 * 	   Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2006-2008, 2011-2012, 2015 Freescale Semiconductor Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/sched/hotplug.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/kexec.h>
#include <linux/highmem.h>
#include <linux/cpu.h>
#include <linux/fsl/guts.h>
#include <linux/pgtable.h>

#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/mpic.h>
#include <asm/cacheflush.h>
#include <asm/dbell.h>
#include <asm/code-patching.h>
#include <asm/cputhreads.h>
#include <asm/fsl_pm.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/mpic.h>
#include "smp.h"

struct epapr_spin_table {
	u32	addr_h;
	u32	addr_l;
	u32	r3_h;
	u32	r3_l;
	u32	reserved;
	u32	pir;
};

#ifdef CONFIG_HOTPLUG_CPU
static u64 timebase;
static int tb_req;
static int tb_valid;

static void mpc85xx_give_timebase(void)
{
	unsigned long flags;

	local_irq_save(flags);
	hard_irq_disable();

	while (!tb_req)
		barrier();
	tb_req = 0;

	qoriq_pm_ops->freeze_time_base(true);
#ifdef CONFIG_PPC64
	/*
	 * e5500/e6500 have a workaround for erratum A-006958 in place
	 * that will reread the timebase until TBL is non-zero.
	 * That would be a bad thing when the timebase is frozen.
	 *
	 * Thus, we read it manually, and instead of checking that
	 * TBL is non-zero, we ensure that TB does not change.  We don't
	 * do that for the main mftb implementation, because it requires
	 * a scratch register
	 */
	{
		u64 prev;

		asm volatile("mfspr %0, %1" : "=r" (timebase) :
			     "i" (SPRN_TBRL));

		do {
			prev = timebase;
			asm volatile("mfspr %0, %1" : "=r" (timebase) :
				     "i" (SPRN_TBRL));
		} while (prev != timebase);
	}
#else
	timebase = get_tb();
#endif
	mb();
	tb_valid = 1;

	while (tb_valid)
		barrier();

	qoriq_pm_ops->freeze_time_base(false);

	local_irq_restore(flags);
}

static void mpc85xx_take_timebase(void)
{
	unsigned long flags;

	local_irq_save(flags);
	hard_irq_disable();

	tb_req = 1;
	while (!tb_valid)
		barrier();

	set_tb(timebase >> 32, timebase & 0xffffffff);
	isync();
	tb_valid = 0;

	local_irq_restore(flags);
}

static void smp_85xx_cpu_offline_self(void)
{
	unsigned int cpu = smp_processor_id();

	local_irq_disable();
	hard_irq_disable();
	/* mask all irqs to prevent cpu wakeup */
	qoriq_pm_ops->irq_mask(cpu);

	idle_task_exit();

	mtspr(SPRN_TCR, 0);
	mtspr(SPRN_TSR, mfspr(SPRN_TSR));

	generic_set_cpu_dead(cpu);

	cur_cpu_spec->cpu_down_flush();

	qoriq_pm_ops->cpu_die(cpu);

	while (1)
		;
}

static void qoriq_cpu_kill(unsigned int cpu)
{
	int i;

	for (i = 0; i < 500; i++) {
		if (is_cpu_dead(cpu)) {
#ifdef CONFIG_PPC64
			paca_ptrs[cpu]->cpu_start = 0;
#endif
			return;
		}
		msleep(20);
	}
	pr_err("CPU%d didn't die...\n", cpu);
}
#endif

/*
 * To keep it compatible with old boot program which uses
 * cache-inhibit spin table, we need to flush the cache
 * before accessing spin table to invalidate any staled data.
 * We also need to flush the cache after writing to spin
 * table to push data out.
 */
static inline void flush_spin_table(void *spin_table)
{
	flush_dcache_range((ulong)spin_table,
		(ulong)spin_table + sizeof(struct epapr_spin_table));
}

static inline u32 read_spin_table_addr_l(void *spin_table)
{
	flush_dcache_range((ulong)spin_table,
		(ulong)spin_table + sizeof(struct epapr_spin_table));
	return in_be32(&((struct epapr_spin_table *)spin_table)->addr_l);
}

#ifdef CONFIG_PPC64
static void wake_hw_thread(void *info)
{
	void fsl_secondary_thread_init(void);
	unsigned long inia;
	int cpu = *(const int *)info;

	inia = *(unsigned long *)fsl_secondary_thread_init;
	book3e_start_thread(cpu_thread_in_core(cpu), inia);
}
#endif

static int smp_85xx_start_cpu(int cpu)
{
	int ret = 0;
	struct device_node *np;
	const u64 *cpu_rel_addr;
	unsigned long flags;
	int ioremappable;
	int hw_cpu = get_hard_smp_processor_id(cpu);
	struct epapr_spin_table __iomem *spin_table;

	np = of_get_cpu_node(cpu, NULL);
	cpu_rel_addr = of_get_property(np, "cpu-release-addr", NULL);
	if (!cpu_rel_addr) {
		pr_err("No cpu-release-addr for cpu %d\n", cpu);
		return -ENOENT;
	}

	/*
	 * A secondary core could be in a spinloop in the bootpage
	 * (0xfffff000), somewhere in highmem, or somewhere in lowmem.
	 * The bootpage and highmem can be accessed via ioremap(), but
	 * we need to directly access the spinloop if its in lowmem.
	 */
	ioremappable = *cpu_rel_addr > virt_to_phys(high_memory);

	/* Map the spin table */
	if (ioremappable)
		spin_table = ioremap_coherent(*cpu_rel_addr,
					      sizeof(struct epapr_spin_table));
	else
		spin_table = phys_to_virt(*cpu_rel_addr);

	local_irq_save(flags);
	hard_irq_disable();

	if (qoriq_pm_ops)
		qoriq_pm_ops->cpu_up_prepare(cpu);

	/* if cpu is not spinning, reset it */
	if (read_spin_table_addr_l(spin_table) != 1) {
		/*
		 * We don't set the BPTR register here since it already points
		 * to the boot page properly.
		 */
		mpic_reset_core(cpu);

		/*
		 * wait until core is ready...
		 * We need to invalidate the stale data, in case the boot
		 * loader uses a cache-inhibited spin table.
		 */
		if (!spin_event_timeout(
				read_spin_table_addr_l(spin_table) == 1,
				10000, 100)) {
			pr_err("timeout waiting for cpu %d to reset\n",
				hw_cpu);
			ret = -EAGAIN;
			goto err;
		}
	}

	flush_spin_table(spin_table);
	out_be32(&spin_table->pir, hw_cpu);
#ifdef CONFIG_PPC64
	out_be64((u64 *)(&spin_table->addr_h),
		__pa(ppc_function_entry(generic_secondary_smp_init)));
#else
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	/*
	 * We need also to write addr_h to spin table for systems
	 * in which their physical memory start address was configured
	 * to above 4G, otherwise the secondary core can not get
	 * correct entry to start from.
	 */
	out_be32(&spin_table->addr_h, __pa(__early_start) >> 32);
#endif
	out_be32(&spin_table->addr_l, __pa(__early_start));
#endif
	flush_spin_table(spin_table);
err:
	local_irq_restore(flags);

	if (ioremappable)
		iounmap(spin_table);

	return ret;
}

static int smp_85xx_kick_cpu(int nr)
{
	int ret = 0;
#ifdef CONFIG_PPC64
	int primary = nr;
#endif

	WARN_ON(nr < 0 || nr >= num_possible_cpus());

	pr_debug("kick CPU #%d\n", nr);

#ifdef CONFIG_PPC64
	if (threads_per_core == 2) {
		if (WARN_ON_ONCE(!cpu_has_feature(CPU_FTR_SMT)))
			return -ENOENT;

		booting_thread_hwid = cpu_thread_in_core(nr);
		primary = cpu_first_thread_sibling(nr);

		if (qoriq_pm_ops)
			qoriq_pm_ops->cpu_up_prepare(nr);

		/*
		 * If either thread in the core is online, use it to start
		 * the other.
		 */
		if (cpu_online(primary)) {
			smp_call_function_single(primary,
					wake_hw_thread, &nr, 1);
			goto done;
		} else if (cpu_online(primary + 1)) {
			smp_call_function_single(primary + 1,
					wake_hw_thread, &nr, 1);
			goto done;
		}

		/*
		 * If getting here, it means both threads in the core are
		 * offline. So start the primary thread, then it will start
		 * the thread specified in booting_thread_hwid, the one
		 * corresponding to nr.
		 */

	} else if (threads_per_core == 1) {
		/*
		 * If one core has only one thread, set booting_thread_hwid to
		 * an invalid value.
		 */
		booting_thread_hwid = INVALID_THREAD_HWID;

	} else if (threads_per_core > 2) {
		pr_err("Do not support more than 2 threads per CPU.");
		return -EINVAL;
	}

	ret = smp_85xx_start_cpu(primary);
	if (ret)
		return ret;

done:
	paca_ptrs[nr]->cpu_start = 1;
	generic_set_cpu_up(nr);

	return ret;
#else
	ret = smp_85xx_start_cpu(nr);
	if (ret)
		return ret;

	generic_set_cpu_up(nr);

	return ret;
#endif
}

struct smp_ops_t smp_85xx_ops = {
	.cause_nmi_ipi = NULL,
	.kick_cpu = smp_85xx_kick_cpu,
	.cpu_bootable = smp_generic_cpu_bootable,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable	= generic_cpu_disable,
	.cpu_die	= generic_cpu_die,
#endif
#if defined(CONFIG_KEXEC_CORE) && !defined(CONFIG_PPC64)
	.give_timebase	= smp_generic_give_timebase,
	.take_timebase	= smp_generic_take_timebase,
#endif
};

#ifdef CONFIG_KEXEC_CORE
#ifdef CONFIG_PPC32
atomic_t kexec_down_cpus = ATOMIC_INIT(0);

void mpc85xx_smp_kexec_cpu_down(int crash_shutdown, int secondary)
{
	local_irq_disable();

	if (secondary) {
		cur_cpu_spec->cpu_down_flush();
		atomic_inc(&kexec_down_cpus);
		/* loop forever */
		while (1);
	}
}

static void mpc85xx_smp_kexec_down(void *arg)
{
	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(0,1);
}
#else
void mpc85xx_smp_kexec_cpu_down(int crash_shutdown, int secondary)
{
	int cpu = smp_processor_id();
	int sibling = cpu_last_thread_sibling(cpu);
	bool notified = false;
	int disable_cpu;
	int disable_threadbit = 0;
	long start = mftb();
	long now;

	local_irq_disable();
	hard_irq_disable();
	mpic_teardown_this_cpu(secondary);

	if (cpu == crashing_cpu && cpu_thread_in_core(cpu) != 0) {
		/*
		 * We enter the crash kernel on whatever cpu crashed,
		 * even if it's a secondary thread.  If that's the case,
		 * disable the corresponding primary thread.
		 */
		disable_threadbit = 1;
		disable_cpu = cpu_first_thread_sibling(cpu);
	} else if (sibling != crashing_cpu &&
		   cpu_thread_in_core(cpu) == 0 &&
		   cpu_thread_in_core(sibling) != 0) {
		disable_threadbit = 2;
		disable_cpu = sibling;
	}

	if (disable_threadbit) {
		while (paca_ptrs[disable_cpu]->kexec_state < KEXEC_STATE_REAL_MODE) {
			barrier();
			now = mftb();
			if (!notified && now - start > 1000000) {
				pr_info("%s/%d: waiting for cpu %d to enter KEXEC_STATE_REAL_MODE (%d)\n",
					__func__, smp_processor_id(),
					disable_cpu,
					paca_ptrs[disable_cpu]->kexec_state);
				notified = true;
			}
		}

		if (notified) {
			pr_info("%s: cpu %d done waiting\n",
				__func__, disable_cpu);
		}

		mtspr(SPRN_TENC, disable_threadbit);
		while (mfspr(SPRN_TENSR) & disable_threadbit)
			cpu_relax();
	}
}
#endif

static void mpc85xx_smp_machine_kexec(struct kimage *image)
{
#ifdef CONFIG_PPC32
	int timeout = INT_MAX;
	int i, num_cpus = num_present_cpus();

	if (image->type == KEXEC_TYPE_DEFAULT)
		smp_call_function(mpc85xx_smp_kexec_down, NULL, 0);

	while ( (atomic_read(&kexec_down_cpus) != (num_cpus - 1)) &&
		( timeout > 0 ) )
	{
		timeout--;
	}

	if ( !timeout )
		printk(KERN_ERR "Unable to bring down secondary cpu(s)");

	for_each_online_cpu(i)
	{
		if ( i == smp_processor_id() ) continue;
		mpic_reset_core(i);
	}
#endif

	default_machine_kexec(image);
}
#endif /* CONFIG_KEXEC_CORE */

static void smp_85xx_setup_cpu(int cpu_nr)
{
	mpic_setup_this_cpu();
}

void __init mpc85xx_smp_init(void)
{
	struct device_node *np;


	np = of_find_node_by_type(NULL, "open-pic");
	if (np) {
		smp_85xx_ops.probe = smp_mpic_probe;
		smp_85xx_ops.setup_cpu = smp_85xx_setup_cpu;
		smp_85xx_ops.message_pass = smp_mpic_message_pass;
	} else
		smp_85xx_ops.setup_cpu = NULL;

	if (cpu_has_feature(CPU_FTR_DBELL)) {
		/*
		 * If left NULL, .message_pass defaults to
		 * smp_muxed_ipi_message_pass
		 */
		smp_85xx_ops.message_pass = NULL;
		smp_85xx_ops.cause_ipi = doorbell_global_ipi;
		smp_85xx_ops.probe = NULL;
	}

#ifdef CONFIG_HOTPLUG_CPU
#ifdef CONFIG_FSL_CORENET_RCPM
	fsl_rcpm_init();
#endif

#ifdef CONFIG_FSL_PMC
	mpc85xx_setup_pmc();
#endif
	if (qoriq_pm_ops) {
		smp_85xx_ops.give_timebase = mpc85xx_give_timebase;
		smp_85xx_ops.take_timebase = mpc85xx_take_timebase;
		smp_85xx_ops.cpu_offline_self = smp_85xx_cpu_offline_self;
		smp_85xx_ops.cpu_die = qoriq_cpu_kill;
	}
#endif
	smp_ops = &smp_85xx_ops;

#ifdef CONFIG_KEXEC_CORE
	ppc_md.kexec_cpu_down = mpc85xx_smp_kexec_cpu_down;
	ppc_md.machine_kexec = mpc85xx_smp_machine_kexec;
#endif
}
