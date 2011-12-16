/*
 * MPC83xx suspend support
 *
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Copyright (c) 2006-2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/pm.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/suspend.h>
#include <linux/fsl_devices.h>
#include <linux/of_platform.h>
#include <linux/export.h>

#include <asm/reg.h>
#include <asm/io.h>
#include <asm/time.h>
#include <asm/mpc6xx.h>

#include <sysdev/fsl_soc.h>

#define PMCCR1_NEXT_STATE       0x0C /* Next state for power management */
#define PMCCR1_NEXT_STATE_SHIFT 2
#define PMCCR1_CURR_STATE       0x03 /* Current state for power management*/
#define IMMR_SYSCR_OFFSET       0x100
#define IMMR_RCW_OFFSET         0x900
#define RCW_PCI_HOST            0x80000000

void mpc83xx_enter_deep_sleep(phys_addr_t immrbase);

struct mpc83xx_pmc {
	u32 config;
#define PMCCR_DLPEN 2 /* DDR SDRAM low power enable */
#define PMCCR_SLPEN 1 /* System low power enable */

	u32 event;
	u32 mask;
/* All but PMCI are deep-sleep only */
#define PMCER_GPIO   0x100
#define PMCER_PCI    0x080
#define PMCER_USB    0x040
#define PMCER_ETSEC1 0x020
#define PMCER_ETSEC2 0x010
#define PMCER_TIMER  0x008
#define PMCER_INT1   0x004
#define PMCER_INT2   0x002
#define PMCER_PMCI   0x001
#define PMCER_ALL    0x1FF

	/* deep-sleep only */
	u32 config1;
#define PMCCR1_USE_STATE  0x80000000
#define PMCCR1_PME_EN     0x00000080
#define PMCCR1_ASSERT_PME 0x00000040
#define PMCCR1_POWER_OFF  0x00000020

	/* deep-sleep only */
	u32 config2;
};

struct mpc83xx_rcw {
	u32 rcwlr;
	u32 rcwhr;
};

struct mpc83xx_clock {
	u32 spmr;
	u32 occr;
	u32 sccr;
};

struct mpc83xx_syscr {
	__be32 sgprl;
	__be32 sgprh;
	__be32 spridr;
	__be32 :32;
	__be32 spcr;
	__be32 sicrl;
	__be32 sicrh;
};

struct mpc83xx_saved {
	u32 sicrl;
	u32 sicrh;
	u32 sccr;
};

struct pmc_type {
	int has_deep_sleep;
};

static struct platform_device *pmc_dev;
static int has_deep_sleep, deep_sleeping;
static int pmc_irq;
static struct mpc83xx_pmc __iomem *pmc_regs;
static struct mpc83xx_clock __iomem *clock_regs;
static struct mpc83xx_syscr __iomem *syscr_regs;
static struct mpc83xx_saved saved_regs;
static int is_pci_agent, wake_from_pci;
static phys_addr_t immrbase;
static int pci_pm_state;
static DECLARE_WAIT_QUEUE_HEAD(agent_wq);

int fsl_deep_sleep(void)
{
	return deep_sleeping;
}
EXPORT_SYMBOL(fsl_deep_sleep);

static int mpc83xx_change_state(void)
{
	u32 curr_state;
	u32 reg_cfg1 = in_be32(&pmc_regs->config1);

	if (is_pci_agent) {
		pci_pm_state = (reg_cfg1 & PMCCR1_NEXT_STATE) >>
		               PMCCR1_NEXT_STATE_SHIFT;
		curr_state = reg_cfg1 & PMCCR1_CURR_STATE;

		if (curr_state != pci_pm_state) {
			reg_cfg1 &= ~PMCCR1_CURR_STATE;
			reg_cfg1 |= pci_pm_state;
			out_be32(&pmc_regs->config1, reg_cfg1);

			wake_up(&agent_wq);
			return 1;
		}
	}

	return 0;
}

static irqreturn_t pmc_irq_handler(int irq, void *dev_id)
{
	u32 event = in_be32(&pmc_regs->event);
	int ret = IRQ_NONE;

	if (mpc83xx_change_state())
		ret = IRQ_HANDLED;

	if (event) {
		out_be32(&pmc_regs->event, event);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static void mpc83xx_suspend_restore_regs(void)
{
	out_be32(&syscr_regs->sicrl, saved_regs.sicrl);
	out_be32(&syscr_regs->sicrh, saved_regs.sicrh);
	out_be32(&clock_regs->sccr, saved_regs.sccr);
}

static void mpc83xx_suspend_save_regs(void)
{
	saved_regs.sicrl = in_be32(&syscr_regs->sicrl);
	saved_regs.sicrh = in_be32(&syscr_regs->sicrh);
	saved_regs.sccr = in_be32(&clock_regs->sccr);
}

static int mpc83xx_suspend_enter(suspend_state_t state)
{
	int ret = -EAGAIN;

	/* Don't go to sleep if there's a race where pci_pm_state changes
	 * between the agent thread checking it and the PM code disabling
	 * interrupts.
	 */
	if (wake_from_pci) {
		if (pci_pm_state != (deep_sleeping ? 3 : 2))
			goto out;

		out_be32(&pmc_regs->config1,
		         in_be32(&pmc_regs->config1) | PMCCR1_PME_EN);
	}

	/* Put the system into low-power mode and the RAM
	 * into self-refresh mode once the core goes to
	 * sleep.
	 */

	out_be32(&pmc_regs->config, PMCCR_SLPEN | PMCCR_DLPEN);

	/* If it has deep sleep (i.e. it's an 831x or compatible),
	 * disable power to the core upon entering sleep mode.  This will
	 * require going through the boot firmware upon a wakeup event.
	 */

	if (deep_sleeping) {
		mpc83xx_suspend_save_regs();

		out_be32(&pmc_regs->mask, PMCER_ALL);

		out_be32(&pmc_regs->config1,
		         in_be32(&pmc_regs->config1) | PMCCR1_POWER_OFF);

		enable_kernel_fp();

		mpc83xx_enter_deep_sleep(immrbase);

		out_be32(&pmc_regs->config1,
		         in_be32(&pmc_regs->config1) & ~PMCCR1_POWER_OFF);

		out_be32(&pmc_regs->mask, PMCER_PMCI);

		mpc83xx_suspend_restore_regs();
	} else {
		out_be32(&pmc_regs->mask, PMCER_PMCI);

		mpc6xx_enter_standby();
	}

	ret = 0;

out:
	out_be32(&pmc_regs->config1,
	         in_be32(&pmc_regs->config1) & ~PMCCR1_PME_EN);

	return ret;
}

static void mpc83xx_suspend_end(void)
{
	deep_sleeping = 0;
}

static int mpc83xx_suspend_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_STANDBY || state == PM_SUSPEND_MEM;
}

static int mpc83xx_suspend_begin(suspend_state_t state)
{
	switch (state) {
		case PM_SUSPEND_STANDBY:
			deep_sleeping = 0;
			return 0;

		case PM_SUSPEND_MEM:
			if (has_deep_sleep)
				deep_sleeping = 1;

			return 0;

		default:
			return -EINVAL;
	}
}

static int agent_thread_fn(void *data)
{
	while (1) {
		wait_event_interruptible(agent_wq, pci_pm_state >= 2);
		try_to_freeze();

		if (signal_pending(current) || pci_pm_state < 2)
			continue;

		/* With a preemptible kernel (or SMP), this could race with
		 * a userspace-driven suspend request.  It's probably best
		 * to avoid mixing the two with such a configuration (or
		 * else fix it by adding a mutex to state_store that we can
		 * synchronize with).
		 */

		wake_from_pci = 1;

		pm_suspend(pci_pm_state == 3 ? PM_SUSPEND_MEM :
		                               PM_SUSPEND_STANDBY);

		wake_from_pci = 0;
	}

	return 0;
}

static void mpc83xx_set_agent(void)
{
	out_be32(&pmc_regs->config1, PMCCR1_USE_STATE);
	out_be32(&pmc_regs->mask, PMCER_PMCI);

	kthread_run(agent_thread_fn, NULL, "PCI power mgt");
}

static int mpc83xx_is_pci_agent(void)
{
	struct mpc83xx_rcw __iomem *rcw_regs;
	int ret;

	rcw_regs = ioremap(get_immrbase() + IMMR_RCW_OFFSET,
	                   sizeof(struct mpc83xx_rcw));

	if (!rcw_regs)
		return -ENOMEM;

	ret = !(in_be32(&rcw_regs->rcwhr) & RCW_PCI_HOST);

	iounmap(rcw_regs);
	return ret;
}

static const struct platform_suspend_ops mpc83xx_suspend_ops = {
	.valid = mpc83xx_suspend_valid,
	.begin = mpc83xx_suspend_begin,
	.enter = mpc83xx_suspend_enter,
	.end = mpc83xx_suspend_end,
};

static struct of_device_id pmc_match[];
static int pmc_probe(struct platform_device *ofdev)
{
	const struct of_device_id *match;
	struct device_node *np = ofdev->dev.of_node;
	struct resource res;
	struct pmc_type *type;
	int ret = 0;

	match = of_match_device(pmc_match, &ofdev->dev);
	if (!match)
		return -EINVAL;

	type = match->data;

	if (!of_device_is_available(np))
		return -ENODEV;

	has_deep_sleep = type->has_deep_sleep;
	immrbase = get_immrbase();
	pmc_dev = ofdev;

	is_pci_agent = mpc83xx_is_pci_agent();
	if (is_pci_agent < 0)
		return is_pci_agent;

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return -ENODEV;

	pmc_irq = irq_of_parse_and_map(np, 0);
	if (pmc_irq != NO_IRQ) {
		ret = request_irq(pmc_irq, pmc_irq_handler, IRQF_SHARED,
		                  "pmc", ofdev);

		if (ret)
			return -EBUSY;
	}

	pmc_regs = ioremap(res.start, sizeof(struct mpc83xx_pmc));

	if (!pmc_regs) {
		ret = -ENOMEM;
		goto out;
	}

	ret = of_address_to_resource(np, 1, &res);
	if (ret) {
		ret = -ENODEV;
		goto out_pmc;
	}

	clock_regs = ioremap(res.start, sizeof(struct mpc83xx_pmc));

	if (!clock_regs) {
		ret = -ENOMEM;
		goto out_pmc;
	}

	if (has_deep_sleep) {
		syscr_regs = ioremap(immrbase + IMMR_SYSCR_OFFSET,
				     sizeof(*syscr_regs));
		if (!syscr_regs) {
			ret = -ENOMEM;
			goto out_syscr;
		}
	}

	if (is_pci_agent)
		mpc83xx_set_agent();

	suspend_set_ops(&mpc83xx_suspend_ops);
	return 0;

out_syscr:
	iounmap(clock_regs);
out_pmc:
	iounmap(pmc_regs);
out:
	if (pmc_irq != NO_IRQ)
		free_irq(pmc_irq, ofdev);

	return ret;
}

static int pmc_remove(struct platform_device *ofdev)
{
	return -EPERM;
};

static struct pmc_type pmc_types[] = {
	{
		.has_deep_sleep = 1,
	},
	{
		.has_deep_sleep = 0,
	}
};

static struct of_device_id pmc_match[] = {
	{
		.compatible = "fsl,mpc8313-pmc",
		.data = &pmc_types[0],
	},
	{
		.compatible = "fsl,mpc8349-pmc",
		.data = &pmc_types[1],
	},
	{}
};

static struct platform_driver pmc_driver = {
	.driver = {
		.name = "mpc83xx-pmc",
		.owner = THIS_MODULE,
		.of_match_table = pmc_match,
	},
	.probe = pmc_probe,
	.remove = pmc_remove
};

static int pmc_init(void)
{
	return platform_driver_register(&pmc_driver);
}

module_init(pmc_init);
