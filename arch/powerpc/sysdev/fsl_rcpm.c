// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RCPM(Run Control/Power Management) support
 *
 * Copyright 2012-2015 Freescale Semiconductor Inc.
 *
 * Author: Chenhui Zhao <chenhui.zhao@freescale.com>
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/of_address.h>
#include <linux/export.h>

#include <asm/io.h>
#include <linux/fsl/guts.h>
#include <asm/cputhreads.h>
#include <asm/fsl_pm.h>
#include <asm/smp.h>

static struct ccsr_rcpm_v1 __iomem *rcpm_v1_regs;
static struct ccsr_rcpm_v2 __iomem *rcpm_v2_regs;
static unsigned int fsl_supported_pm_modes;

static void rcpm_v1_irq_mask(int cpu)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	setbits32(&rcpm_v1_regs->cpmimr, mask);
	setbits32(&rcpm_v1_regs->cpmcimr, mask);
	setbits32(&rcpm_v1_regs->cpmmcmr, mask);
	setbits32(&rcpm_v1_regs->cpmnmimr, mask);
}

static void rcpm_v2_irq_mask(int cpu)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	setbits32(&rcpm_v2_regs->tpmimr0, mask);
	setbits32(&rcpm_v2_regs->tpmcimr0, mask);
	setbits32(&rcpm_v2_regs->tpmmcmr0, mask);
	setbits32(&rcpm_v2_regs->tpmnmimr0, mask);
}

static void rcpm_v1_irq_unmask(int cpu)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	clrbits32(&rcpm_v1_regs->cpmimr, mask);
	clrbits32(&rcpm_v1_regs->cpmcimr, mask);
	clrbits32(&rcpm_v1_regs->cpmmcmr, mask);
	clrbits32(&rcpm_v1_regs->cpmnmimr, mask);
}

static void rcpm_v2_irq_unmask(int cpu)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	clrbits32(&rcpm_v2_regs->tpmimr0, mask);
	clrbits32(&rcpm_v2_regs->tpmcimr0, mask);
	clrbits32(&rcpm_v2_regs->tpmmcmr0, mask);
	clrbits32(&rcpm_v2_regs->tpmnmimr0, mask);
}

static void rcpm_v1_set_ip_power(bool enable, u32 mask)
{
	if (enable)
		setbits32(&rcpm_v1_regs->ippdexpcr, mask);
	else
		clrbits32(&rcpm_v1_regs->ippdexpcr, mask);
}

static void rcpm_v2_set_ip_power(bool enable, u32 mask)
{
	if (enable)
		setbits32(&rcpm_v2_regs->ippdexpcr[0], mask);
	else
		clrbits32(&rcpm_v2_regs->ippdexpcr[0], mask);
}

static void rcpm_v1_cpu_enter_state(int cpu, int state)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	switch (state) {
	case E500_PM_PH10:
		setbits32(&rcpm_v1_regs->cdozcr, mask);
		break;
	case E500_PM_PH15:
		setbits32(&rcpm_v1_regs->cnapcr, mask);
		break;
	default:
		pr_warn("Unknown cpu PM state (%d)\n", state);
		break;
	}
}

static void rcpm_v2_cpu_enter_state(int cpu, int state)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	u32 mask = 1 << cpu_core_index_of_thread(cpu);

	switch (state) {
	case E500_PM_PH10:
		/* one bit corresponds to one thread for PH10 of 6500 */
		setbits32(&rcpm_v2_regs->tph10setr0, 1 << hw_cpu);
		break;
	case E500_PM_PH15:
		setbits32(&rcpm_v2_regs->pcph15setr, mask);
		break;
	case E500_PM_PH20:
		setbits32(&rcpm_v2_regs->pcph20setr, mask);
		break;
	case E500_PM_PH30:
		setbits32(&rcpm_v2_regs->pcph30setr, mask);
		break;
	default:
		pr_warn("Unknown cpu PM state (%d)\n", state);
	}
}

static void rcpm_v1_cpu_die(int cpu)
{
	rcpm_v1_cpu_enter_state(cpu, E500_PM_PH15);
}

#ifdef CONFIG_PPC64
static void qoriq_disable_thread(int cpu)
{
	int thread = cpu_thread_in_core(cpu);

	book3e_stop_thread(thread);
}
#endif

static void rcpm_v2_cpu_die(int cpu)
{
#ifdef CONFIG_PPC64
	int primary;

	if (threads_per_core == 2) {
		primary = cpu_first_thread_sibling(cpu);
		if (cpu_is_offline(primary) && cpu_is_offline(primary + 1)) {
			/* if both threads are offline, put the cpu in PH20 */
			rcpm_v2_cpu_enter_state(cpu, E500_PM_PH20);
		} else {
			/* if only one thread is offline, disable the thread */
			qoriq_disable_thread(cpu);
		}
	}
#endif

	if (threads_per_core == 1)
		rcpm_v2_cpu_enter_state(cpu, E500_PM_PH20);
}

static void rcpm_v1_cpu_exit_state(int cpu, int state)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	unsigned int mask = 1 << hw_cpu;

	switch (state) {
	case E500_PM_PH10:
		clrbits32(&rcpm_v1_regs->cdozcr, mask);
		break;
	case E500_PM_PH15:
		clrbits32(&rcpm_v1_regs->cnapcr, mask);
		break;
	default:
		pr_warn("Unknown cpu PM state (%d)\n", state);
		break;
	}
}

static void rcpm_v1_cpu_up_prepare(int cpu)
{
	rcpm_v1_cpu_exit_state(cpu, E500_PM_PH15);
	rcpm_v1_irq_unmask(cpu);
}

static void rcpm_v2_cpu_exit_state(int cpu, int state)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);
	u32 mask = 1 << cpu_core_index_of_thread(cpu);

	switch (state) {
	case E500_PM_PH10:
		setbits32(&rcpm_v2_regs->tph10clrr0, 1 << hw_cpu);
		break;
	case E500_PM_PH15:
		setbits32(&rcpm_v2_regs->pcph15clrr, mask);
		break;
	case E500_PM_PH20:
		setbits32(&rcpm_v2_regs->pcph20clrr, mask);
		break;
	case E500_PM_PH30:
		setbits32(&rcpm_v2_regs->pcph30clrr, mask);
		break;
	default:
		pr_warn("Unknown cpu PM state (%d)\n", state);
	}
}

static void rcpm_v2_cpu_up_prepare(int cpu)
{
	rcpm_v2_cpu_exit_state(cpu, E500_PM_PH20);
	rcpm_v2_irq_unmask(cpu);
}

static int rcpm_v1_plat_enter_state(int state)
{
	u32 *pmcsr_reg = &rcpm_v1_regs->powmgtcsr;
	int ret = 0;
	int result;

	switch (state) {
	case PLAT_PM_SLEEP:
		setbits32(pmcsr_reg, RCPM_POWMGTCSR_SLP);

		/* Upon resume, wait for RCPM_POWMGTCSR_SLP bit to be clear. */
		result = spin_event_timeout(
		  !(in_be32(pmcsr_reg) & RCPM_POWMGTCSR_SLP), 10000, 10);
		if (!result) {
			pr_err("timeout waiting for SLP bit to be cleared\n");
			ret = -ETIMEDOUT;
		}
		break;
	default:
		pr_warn("Unknown platform PM state (%d)", state);
		ret = -EINVAL;
	}

	return ret;
}

static int rcpm_v2_plat_enter_state(int state)
{
	u32 *pmcsr_reg = &rcpm_v2_regs->powmgtcsr;
	int ret = 0;
	int result;

	switch (state) {
	case PLAT_PM_LPM20:
		/* clear previous LPM20 status */
		setbits32(pmcsr_reg, RCPM_POWMGTCSR_P_LPM20_ST);
		/* enter LPM20 status */
		setbits32(pmcsr_reg, RCPM_POWMGTCSR_LPM20_RQ);

		/* At this point, the device is in LPM20 status. */

		/* resume ... */
		result = spin_event_timeout(
		  !(in_be32(pmcsr_reg) & RCPM_POWMGTCSR_LPM20_ST), 10000, 10);
		if (!result) {
			pr_err("timeout waiting for LPM20 bit to be cleared\n");
			ret = -ETIMEDOUT;
		}
		break;
	default:
		pr_warn("Unknown platform PM state (%d)\n", state);
		ret = -EINVAL;
	}

	return ret;
}

static int rcpm_v1_plat_enter_sleep(void)
{
	return rcpm_v1_plat_enter_state(PLAT_PM_SLEEP);
}

static int rcpm_v2_plat_enter_sleep(void)
{
	return rcpm_v2_plat_enter_state(PLAT_PM_LPM20);
}

static void rcpm_common_freeze_time_base(u32 *tben_reg, int freeze)
{
	static u32 mask;

	if (freeze) {
		mask = in_be32(tben_reg);
		clrbits32(tben_reg, mask);
	} else {
		setbits32(tben_reg, mask);
	}

	/* read back to push the previous write */
	in_be32(tben_reg);
}

static void rcpm_v1_freeze_time_base(bool freeze)
{
	rcpm_common_freeze_time_base(&rcpm_v1_regs->ctbenr, freeze);
}

static void rcpm_v2_freeze_time_base(bool freeze)
{
	rcpm_common_freeze_time_base(&rcpm_v2_regs->pctbenr, freeze);
}

static unsigned int rcpm_get_pm_modes(void)
{
	return fsl_supported_pm_modes;
}

static const struct fsl_pm_ops qoriq_rcpm_v1_ops = {
	.irq_mask = rcpm_v1_irq_mask,
	.irq_unmask = rcpm_v1_irq_unmask,
	.cpu_enter_state = rcpm_v1_cpu_enter_state,
	.cpu_exit_state = rcpm_v1_cpu_exit_state,
	.cpu_up_prepare = rcpm_v1_cpu_up_prepare,
	.cpu_die = rcpm_v1_cpu_die,
	.plat_enter_sleep = rcpm_v1_plat_enter_sleep,
	.set_ip_power = rcpm_v1_set_ip_power,
	.freeze_time_base = rcpm_v1_freeze_time_base,
	.get_pm_modes = rcpm_get_pm_modes,
};

static const struct fsl_pm_ops qoriq_rcpm_v2_ops = {
	.irq_mask = rcpm_v2_irq_mask,
	.irq_unmask = rcpm_v2_irq_unmask,
	.cpu_enter_state = rcpm_v2_cpu_enter_state,
	.cpu_exit_state = rcpm_v2_cpu_exit_state,
	.cpu_up_prepare = rcpm_v2_cpu_up_prepare,
	.cpu_die = rcpm_v2_cpu_die,
	.plat_enter_sleep = rcpm_v2_plat_enter_sleep,
	.set_ip_power = rcpm_v2_set_ip_power,
	.freeze_time_base = rcpm_v2_freeze_time_base,
	.get_pm_modes = rcpm_get_pm_modes,
};

static const struct of_device_id rcpm_matches[] = {
	{
		.compatible = "fsl,qoriq-rcpm-1.0",
		.data = &qoriq_rcpm_v1_ops,
	},
	{
		.compatible = "fsl,qoriq-rcpm-2.0",
		.data = &qoriq_rcpm_v2_ops,
	},
	{
		.compatible = "fsl,qoriq-rcpm-2.1",
		.data = &qoriq_rcpm_v2_ops,
	},
	{},
};

int __init fsl_rcpm_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	void __iomem *base;

	np = of_find_matching_node_and_match(NULL, rcpm_matches, &match);
	if (!np)
		return 0;

	base = of_iomap(np, 0);
	of_node_put(np);
	if (!base) {
		pr_err("of_iomap() error.\n");
		return -ENOMEM;
	}

	rcpm_v1_regs = base;
	rcpm_v2_regs = base;

	/* support sleep by default */
	fsl_supported_pm_modes = FSL_PM_SLEEP;

	qoriq_pm_ops = match->data;

	return 0;
}
