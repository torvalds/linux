// SPDX-License-Identifier: GPL-2.0
/*
 * ip30-smp.c: SMP on IP30 architecture.
 * Based off of the original IP30 SMP code, with inspiration from ip27-smp.c
 * and smp-bmips.c.
 *
 * Copyright (C) 2005-2007 Stanislaw Skowronek <skylark@unaligned.org>
 *               2006-2007, 2014-2015 Joshua Kinard <linux@kumba.dev>
 *               2009 Johannes Dickgreber <tanzy@gmx.de>
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>

#include <asm/time.h>
#include <asm/sgi/heart.h>

#include "ip30-common.h"

#define MPCONF_MAGIC	0xbaddeed2
#define	MPCONF_ADDR	0xa800000000000600L
#define MPCONF_SIZE	0x80
#define MPCONF(x)	(MPCONF_ADDR + (x) * MPCONF_SIZE)

/* HEART can theoretically do 4 CPUs, but only 2 are physically possible */
#define MP_NCPU		2

struct mpconf {
	u32 magic;
	u32 prid;
	u32 physid;
	u32 virtid;
	u32 scachesz;
	u16 fanloads;
	u16 res;
	void *launch;
	void *rendezvous;
	u64 res2[3];
	void *stackaddr;
	void *lnch_parm;
	void *rndv_parm;
	u32 idleflag;
};

static void ip30_smp_send_ipi_single(int cpu, u32 action)
{
	int irq;

	switch (action) {
	case SMP_RESCHEDULE_YOURSELF:
		irq = HEART_L2_INT_RESCHED_CPU_0;
		break;
	case SMP_CALL_FUNCTION:
		irq = HEART_L2_INT_CALL_CPU_0;
		break;
	default:
		panic("IP30: Unknown action value in %s!\n", __func__);
	}

	irq += cpu;

	/* Poke the other CPU -- it's got mail! */
	heart_write(BIT_ULL(irq), &heart_regs->set_isr);
}

static void ip30_smp_send_ipi_mask(const struct cpumask *mask, u32 action)
{
	u32 i;

	for_each_cpu(i, mask)
		ip30_smp_send_ipi_single(i, action);
}

static void __init ip30_smp_setup(void)
{
	int i;
	int ncpu = 0;
	struct mpconf *mpc;

	init_cpu_possible(cpumask_of(0));

	/* Scan the MPCONF structure and enumerate available CPUs. */
	for (i = 0; i < MP_NCPU; i++) {
		mpc = (struct mpconf *)MPCONF(i);
		if (mpc->magic == MPCONF_MAGIC) {
			set_cpu_possible(i, true);
			__cpu_number_map[i] = ++ncpu;
			__cpu_logical_map[ncpu] = i;
			pr_info("IP30: Slot: %d, PrID: %.8x, PhyID: %d, VirtID: %d\n",
				i, mpc->prid, mpc->physid, mpc->virtid);
		}
	}
	pr_info("IP30: Detected %d CPU(s) present.\n", ncpu);

	/*
	 * Set the coherency algorithm to '5' (cacheable coherent
	 * exclusive on write).  This is needed on IP30 SMP, especially
	 * for R14000 CPUs, otherwise, instruction bus errors will
	 * occur upon reaching userland.
	 */
	change_c0_config(CONF_CM_CMASK, CONF_CM_CACHABLE_COW);
}

static void __init ip30_smp_prepare_cpus(unsigned int max_cpus)
{
	/* nothing to do here */
}

static int __init ip30_smp_boot_secondary(int cpu, struct task_struct *idle)
{
	struct mpconf *mpc = (struct mpconf *)MPCONF(cpu);

	/* Stack pointer (sp). */
	mpc->stackaddr = (void *)__KSTK_TOS(idle);

	/* Global pointer (gp). */
	mpc->lnch_parm = task_thread_info(idle);

	mb(); /* make sure stack and lparm are written */

	/* Boot CPUx. */
	mpc->launch = smp_bootstrap;

	/* CPUx now executes smp_bootstrap, then ip30_smp_finish */
	return 0;
}

static void __init ip30_smp_init_cpu(void)
{
	ip30_per_cpu_init();
}

static void __init ip30_smp_finish(void)
{
	enable_percpu_irq(get_c0_compare_int(), IRQ_TYPE_NONE);
	local_irq_enable();
}

struct plat_smp_ops __read_mostly ip30_smp_ops = {
	.send_ipi_single	= ip30_smp_send_ipi_single,
	.send_ipi_mask		= ip30_smp_send_ipi_mask,
	.smp_setup		= ip30_smp_setup,
	.prepare_cpus		= ip30_smp_prepare_cpus,
	.boot_secondary		= ip30_smp_boot_secondary,
	.init_secondary		= ip30_smp_init_cpu,
	.smp_finish		= ip30_smp_finish,
	.prepare_boot_cpu	= ip30_smp_init_cpu,
};
