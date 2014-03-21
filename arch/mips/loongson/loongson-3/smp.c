/*
 * Copyright (C) 2010, 2011, 2012, Lemote, Inc.
 * Author: Chen Huacai, chenhc@lemote.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <asm/processor.h>
#include <asm/time.h>
#include <asm/clock.h>
#include <asm/tlbflush.h>
#include <loongson.h>

#include "smp.h"

/* read a 32bit value from ipi register */
#define loongson3_ipi_read32(addr) readl(addr)
/* read a 64bit value from ipi register */
#define loongson3_ipi_read64(addr) readq(addr)
/* write a 32bit value to ipi register */
#define loongson3_ipi_write32(action, addr)	\
	do {					\
		writel(action, addr);		\
		__wbflush();			\
	} while (0)
/* write a 64bit value to ipi register */
#define loongson3_ipi_write64(action, addr)	\
	do {					\
		writeq(action, addr);		\
		__wbflush();			\
	} while (0)

static void *ipi_set0_regs[] = {
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE0_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE1_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE2_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE3_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE0_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE1_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE2_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE3_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE0_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE1_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE2_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE3_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE0_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE1_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE2_OFFSET + SET0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE3_OFFSET + SET0),
};

static void *ipi_clear0_regs[] = {
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE0_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE1_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE2_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE3_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE0_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE1_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE2_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE3_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE0_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE1_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE2_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE3_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE0_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE1_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE2_OFFSET + CLEAR0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE3_OFFSET + CLEAR0),
};

static void *ipi_status0_regs[] = {
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE0_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE1_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE2_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE3_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE0_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE1_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE2_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE3_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE0_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE1_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE2_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE3_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE0_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE1_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE2_OFFSET + STATUS0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE3_OFFSET + STATUS0),
};

static void *ipi_en0_regs[] = {
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE0_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE1_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE2_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE3_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE0_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE1_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE2_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE3_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE0_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE1_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE2_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE3_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE0_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE1_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE2_OFFSET + EN0),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE3_OFFSET + EN0),
};

static void *ipi_mailbox_buf[] = {
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE0_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE1_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE2_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP0_BASE + SMP_CORE3_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE0_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE1_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE2_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP1_BASE + SMP_CORE3_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE0_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE1_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE2_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP2_BASE + SMP_CORE3_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE0_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE1_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE2_OFFSET + BUF),
	(void *)(SMP_CORE_GROUP3_BASE + SMP_CORE3_OFFSET + BUF),
};

/*
 * Simple enough, just poke the appropriate ipi register
 */
static void loongson3_send_ipi_single(int cpu, unsigned int action)
{
	loongson3_ipi_write32((u32)action, ipi_set0_regs[cpu]);
}

static void
loongson3_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int i;

	for_each_cpu(i, mask)
		loongson3_ipi_write32((u32)action, ipi_set0_regs[i]);
}

void loongson3_ipi_interrupt(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned int action;

	/* Load the ipi register to figure out what we're supposed to do */
	action = loongson3_ipi_read32(ipi_status0_regs[cpu]);

	/* Clear the ipi register to clear the interrupt */
	loongson3_ipi_write32((u32)action, ipi_clear0_regs[cpu]);

	if (action & SMP_RESCHEDULE_YOURSELF)
		scheduler_ipi();

	if (action & SMP_CALL_FUNCTION)
		smp_call_function_interrupt();
}

/*
 * SMP init and finish on secondary CPUs
 */
static void loongson3_init_secondary(void)
{
	int i;
	unsigned int imask = STATUSF_IP7 | STATUSF_IP6 |
			     STATUSF_IP3 | STATUSF_IP2;

	/* Set interrupt mask, but don't enable */
	change_c0_status(ST0_IM, imask);

	for (i = 0; i < loongson_sysconf.nr_cpus; i++)
		loongson3_ipi_write32(0xffffffff, ipi_en0_regs[i]);
}

static void loongson3_smp_finish(void)
{
	write_c0_compare(read_c0_count() + mips_hpt_frequency/HZ);
	local_irq_enable();
	loongson3_ipi_write64(0,
			(void *)(ipi_mailbox_buf[smp_processor_id()]+0x0));
	pr_info("CPU#%d finished, CP0_ST=%x\n",
			smp_processor_id(), read_c0_status());
}

static void __init loongson3_smp_setup(void)
{
	int i, num;

	init_cpu_possible(cpu_none_mask);
	set_cpu_possible(0, true);

	__cpu_number_map[0] = 0;
	__cpu_logical_map[0] = 0;

	/* For unified kernel, NR_CPUS is the maximum possible value,
	 * loongson_sysconf.nr_cpus is the really present value */
	for (i = 1, num = 0; i < loongson_sysconf.nr_cpus; i++) {
		set_cpu_possible(i, true);
		__cpu_number_map[i] = ++num;
		__cpu_logical_map[num] = i;
	}
	pr_info("Detected %i available secondary CPU(s)\n", num);
}

static void __init loongson3_prepare_cpus(unsigned int max_cpus)
{
}

/*
 * Setup the PC, SP, and GP of a secondary processor and start it runing!
 */
static void loongson3_boot_secondary(int cpu, struct task_struct *idle)
{
	unsigned long startargs[4];

	pr_info("Booting CPU#%d...\n", cpu);

	/* startargs[] are initial PC, SP and GP for secondary CPU */
	startargs[0] = (unsigned long)&smp_bootstrap;
	startargs[1] = (unsigned long)__KSTK_TOS(idle);
	startargs[2] = (unsigned long)task_thread_info(idle);
	startargs[3] = 0;

	pr_debug("CPU#%d, func_pc=%lx, sp=%lx, gp=%lx\n",
			cpu, startargs[0], startargs[1], startargs[2]);

	loongson3_ipi_write64(startargs[3], (void *)(ipi_mailbox_buf[cpu]+0x18));
	loongson3_ipi_write64(startargs[2], (void *)(ipi_mailbox_buf[cpu]+0x10));
	loongson3_ipi_write64(startargs[1], (void *)(ipi_mailbox_buf[cpu]+0x8));
	loongson3_ipi_write64(startargs[0], (void *)(ipi_mailbox_buf[cpu]+0x0));
}

/*
 * Final cleanup after all secondaries booted
 */
static void __init loongson3_cpus_done(void)
{
}

struct plat_smp_ops loongson3_smp_ops = {
	.send_ipi_single = loongson3_send_ipi_single,
	.send_ipi_mask = loongson3_send_ipi_mask,
	.init_secondary = loongson3_init_secondary,
	.smp_finish = loongson3_smp_finish,
	.cpus_done = loongson3_cpus_done,
	.boot_secondary = loongson3_boot_secondary,
	.smp_setup = loongson3_smp_setup,
	.prepare_cpus = loongson3_prepare_cpus,
};
