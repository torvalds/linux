/**
 * @file op_model_mpcore.c
 * MPCORE Event Monitor Driver
 * @remark Copyright 2004 ARM SMP Development Team
 * @remark Copyright 2000-2004 Deepak Saxena <dsaxena@mvista.com>
 * @remark Copyright 2000-2004 MontaVista Software Inc
 * @remark Copyright 2004 Dave Jiang <dave.jiang@intel.com>
 * @remark Copyright 2004 Intel Corporation
 * @remark Copyright 2004 Zwane Mwaikambo <zwane@arm.linux.org.uk>
 * @remark Copyright 2004 Oprofile Authors
 *
 * @remark Read the file COPYING
 *
 * @author Zwane Mwaikambo
 *
 *  Counters:
 *    0: PMN0 on CPU0, per-cpu configurable event counter
 *    1: PMN1 on CPU0, per-cpu configurable event counter
 *    2: CCNT on CPU0
 *    3: PMN0 on CPU1
 *    4: PMN1 on CPU1
 *    5: CCNT on CPU1
 *    6: PMN0 on CPU1
 *    7: PMN1 on CPU1
 *    8: CCNT on CPU1
 *    9: PMN0 on CPU1
 *   10: PMN1 on CPU1
 *   11: CCNT on CPU1
 *   12-19: configurable SCU event counters
 */

/* #define DEBUG */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/oprofile.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <mach/hardware.h>
#include <asm/system.h>

#include "op_counter.h"
#include "op_arm_model.h"
#include "op_model_arm11_core.h"
#include "op_model_mpcore.h"

/*
 * MPCore SCU event monitor support
 */
#define SCU_EVENTMONITORS_VA_BASE __io_address(REALVIEW_EB11MP_SCU_BASE + 0x10)

/*
 * Bitmask of used SCU counters
 */
static unsigned int scu_em_used;

/*
 * 2 helper fns take a counter number from 0-7 (not the userspace-visible counter number)
 */
static inline void scu_reset_counter(struct eventmonitor __iomem *emc, unsigned int n)
{
	writel(-(u32)counter_config[SCU_COUNTER(n)].count, &emc->MC[n]);
}

static inline void scu_set_event(struct eventmonitor __iomem *emc, unsigned int n, u32 event)
{
	event &= 0xff;
	writeb(event, &emc->MCEB[n]);
}

/*
 * SCU counters' IRQ handler (one IRQ per counter => 2 IRQs per CPU)
 */
static irqreturn_t scu_em_interrupt(int irq, void *arg)
{
	struct eventmonitor __iomem *emc = SCU_EVENTMONITORS_VA_BASE;
	unsigned int cnt;

	cnt = irq - IRQ_EB11MP_PMU_SCU0;
	oprofile_add_sample(get_irq_regs(), SCU_COUNTER(cnt));
	scu_reset_counter(emc, cnt);

	/* Clear overflow flag for this counter */
	writel(1 << (cnt + 16), &emc->PMCR);

	return IRQ_HANDLED;
}

/* Configure just the SCU counters that the user has requested */
static void scu_setup(void)
{
	struct eventmonitor __iomem *emc = SCU_EVENTMONITORS_VA_BASE;
	unsigned int i;

	scu_em_used = 0;

	for (i = 0; i < NUM_SCU_COUNTERS; i++) {
		if (counter_config[SCU_COUNTER(i)].enabled &&
		    counter_config[SCU_COUNTER(i)].event) {
			scu_set_event(emc, i, 0); /* disable counter for now */
			scu_em_used |= 1 << i;
		}
	}
}

static int scu_start(void)
{
	struct eventmonitor __iomem *emc = SCU_EVENTMONITORS_VA_BASE;
	unsigned int temp, i;
	unsigned long event;
	int ret = 0;

	/*
	 * request the SCU counter interrupts that we need
	 */
	for (i = 0; i < NUM_SCU_COUNTERS; i++) {
		if (scu_em_used & (1 << i)) {
			ret = request_irq(IRQ_EB11MP_PMU_SCU0 + i, scu_em_interrupt, IRQF_DISABLED, "SCU PMU", NULL);
			if (ret) {
				printk(KERN_ERR "oprofile: unable to request IRQ%u for SCU Event Monitor\n",
				       IRQ_EB11MP_PMU_SCU0 + i);
				goto err_free_scu;
			}
		}
	}

	/*
	 * clear overflow and enable interrupt for all used counters
	 */
	temp = readl(&emc->PMCR);
	for (i = 0; i < NUM_SCU_COUNTERS; i++) {
		if (scu_em_used & (1 << i)) {
			scu_reset_counter(emc, i);
			event = counter_config[SCU_COUNTER(i)].event;
			scu_set_event(emc, i, event);

			/* clear overflow/interrupt */
			temp |= 1 << (i + 16);
			/* enable interrupt*/
			temp |= 1 << (i + 8);
		}
	}

	/* Enable all 8 counters */
	temp |= PMCR_E;
	writel(temp, &emc->PMCR);

	return 0;

 err_free_scu:
	while (i--)
		free_irq(IRQ_EB11MP_PMU_SCU0 + i, NULL);
	return ret;
}

static void scu_stop(void)
{
	struct eventmonitor __iomem *emc = SCU_EVENTMONITORS_VA_BASE;
	unsigned int temp, i;

	/* Disable counter interrupts */
	/* Don't disable all 8 counters (with the E bit) as they may be in use */
	temp = readl(&emc->PMCR);
	for (i = 0; i < NUM_SCU_COUNTERS; i++) {
		if (scu_em_used & (1 << i))
			temp &= ~(1 << (i + 8));
	}
	writel(temp, &emc->PMCR);

	/* Free counter interrupts and reset counters */
	for (i = 0; i < NUM_SCU_COUNTERS; i++) {
		if (scu_em_used & (1 << i)) {
			scu_reset_counter(emc, i);
			free_irq(IRQ_EB11MP_PMU_SCU0 + i, NULL);
		}
	}
}

struct em_function_data {
	int (*fn)(void);
	int ret;
};

static void em_func(void *data)
{
	struct em_function_data *d = data;
	int ret = d->fn();
	if (ret)
		d->ret = ret;
}

static int em_call_function(int (*fn)(void))
{
	struct em_function_data data;

	data.fn = fn;
	data.ret = 0;

	preempt_disable();
	smp_call_function(em_func, &data, 1);
	em_func(&data);
	preempt_enable();

	return data.ret;
}

/*
 * Glue to stick the individual ARM11 PMUs and the SCU
 * into the oprofile framework.
 */
static int em_setup_ctrs(void)
{
	int ret;

	/* Configure CPU counters by cross-calling to the other CPUs */
	ret = em_call_function(arm11_setup_pmu);
	if (ret == 0)
		scu_setup();

	return 0;
}

static int arm11_irqs[] = {
	[0]	= IRQ_EB11MP_PMU_CPU0,
	[1]	= IRQ_EB11MP_PMU_CPU1,
	[2]	= IRQ_EB11MP_PMU_CPU2,
	[3]	= IRQ_EB11MP_PMU_CPU3
};

static int em_start(void)
{
	int ret;

	ret = arm11_request_interrupts(arm11_irqs, ARRAY_SIZE(arm11_irqs));
	if (ret == 0) {
		em_call_function(arm11_start_pmu);

		ret = scu_start();
		if (ret)
			arm11_release_interrupts(arm11_irqs, ARRAY_SIZE(arm11_irqs));
	}
	return ret;
}

static void em_stop(void)
{
	em_call_function(arm11_stop_pmu);
	arm11_release_interrupts(arm11_irqs, ARRAY_SIZE(arm11_irqs));
	scu_stop();
}

/*
 * Why isn't there a function to route an IRQ to a specific CPU in
 * genirq?
 */
static void em_route_irq(int irq, unsigned int cpu)
{
	struct irq_desc *desc = irq_desc + irq;
	const struct cpumask *mask = cpumask_of(cpu);

	spin_lock_irq(&desc->lock);
	cpumask_copy(desc->affinity, mask);
	desc->chip->set_affinity(irq, mask);
	spin_unlock_irq(&desc->lock);
}

static int em_setup(void)
{
	/*
	 * Send SCU PMU interrupts to the "owner" CPU.
	 */
	em_route_irq(IRQ_EB11MP_PMU_SCU0, 0);
	em_route_irq(IRQ_EB11MP_PMU_SCU1, 0);
	em_route_irq(IRQ_EB11MP_PMU_SCU2, 1);
	em_route_irq(IRQ_EB11MP_PMU_SCU3, 1);
	em_route_irq(IRQ_EB11MP_PMU_SCU4, 2);
	em_route_irq(IRQ_EB11MP_PMU_SCU5, 2);
	em_route_irq(IRQ_EB11MP_PMU_SCU6, 3);
	em_route_irq(IRQ_EB11MP_PMU_SCU7, 3);

	/*
	 * Send CP15 PMU interrupts to the owner CPU.
	 */
	em_route_irq(IRQ_EB11MP_PMU_CPU0, 0);
	em_route_irq(IRQ_EB11MP_PMU_CPU1, 1);
	em_route_irq(IRQ_EB11MP_PMU_CPU2, 2);
	em_route_irq(IRQ_EB11MP_PMU_CPU3, 3);

	return 0;
}

struct op_arm_model_spec op_mpcore_spec = {
	.init		= em_setup,
	.num_counters	= MPCORE_NUM_COUNTERS,
	.setup_ctrs	= em_setup_ctrs,
	.start		= em_start,
	.stop		= em_stop,
	.name		= "arm/mpcore",
};
