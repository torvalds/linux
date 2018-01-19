/*
 * Copyright (C) ST-Ericsson SA 2010-2013
 * Author: Rickard Andersson <rickard.andersson@stericsson.com> for
 *         ST-Ericsson.
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org> for Linaro.
 * Author: Ulf Hansson <ulf.hansson@linaro.org> for Linaro.
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 */

#include <linux/kernel.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/platform_data/arm-ux500-pm.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "db8500-regs.h"

/* ARM WFI Standby signal register */
#define PRCM_ARM_WFI_STANDBY    (prcmu_base + 0x130)
#define PRCM_ARM_WFI_STANDBY_WFI0		0x08
#define PRCM_ARM_WFI_STANDBY_WFI1		0x10
#define PRCM_IOCR		(prcmu_base + 0x310)
#define PRCM_IOCR_IOFORCE			0x1

/* Dual A9 core interrupt management unit registers */
#define PRCM_A9_MASK_REQ	(prcmu_base + 0x328)
#define PRCM_A9_MASK_REQ_PRCM_A9_MASK_REQ	0x1

#define PRCM_A9_MASK_ACK	(prcmu_base + 0x32c)
#define PRCM_ARMITMSK31TO0	(prcmu_base + 0x11c)
#define PRCM_ARMITMSK63TO32	(prcmu_base + 0x120)
#define PRCM_ARMITMSK95TO64	(prcmu_base + 0x124)
#define PRCM_ARMITMSK127TO96	(prcmu_base + 0x128)
#define PRCM_POWER_STATE_VAL	(prcmu_base + 0x25C)
#define PRCM_ARMITVAL31TO0	(prcmu_base + 0x260)
#define PRCM_ARMITVAL63TO32	(prcmu_base + 0x264)
#define PRCM_ARMITVAL95TO64	(prcmu_base + 0x268)
#define PRCM_ARMITVAL127TO96	(prcmu_base + 0x26C)

static void __iomem *prcmu_base;
static void __iomem *dist_base;

/* This function decouple the gic from the prcmu */
int prcmu_gic_decouple(void)
{
	u32 val = readl(PRCM_A9_MASK_REQ);

	/* Set bit 0 register value to 1 */
	writel(val | PRCM_A9_MASK_REQ_PRCM_A9_MASK_REQ,
	       PRCM_A9_MASK_REQ);

	/* Make sure the register is updated */
	readl(PRCM_A9_MASK_REQ);

	/* Wait a few cycles for the gic mask completion */
	udelay(1);

	return 0;
}

/* This function recouple the gic with the prcmu */
int prcmu_gic_recouple(void)
{
	u32 val = readl(PRCM_A9_MASK_REQ);

	/* Set bit 0 register value to 0 */
	writel(val & ~PRCM_A9_MASK_REQ_PRCM_A9_MASK_REQ, PRCM_A9_MASK_REQ);

	return 0;
}

#define PRCMU_GIC_NUMBER_REGS 5

/*
 * This function checks if there are pending irq on the gic. It only
 * makes sense if the gic has been decoupled before with the
 * db8500_prcmu_gic_decouple function. Disabling an interrupt only
 * disables the forwarding of the interrupt to any CPU interface. It
 * does not prevent the interrupt from changing state, for example
 * becoming pending, or active and pending if it is already
 * active. Hence, we have to check the interrupt is pending *and* is
 * active.
 */
bool prcmu_gic_pending_irq(void)
{
	u32 pr; /* Pending register */
	u32 er; /* Enable register */
	int i;

	/* 5 registers. STI & PPI not skipped */
	for (i = 0; i < PRCMU_GIC_NUMBER_REGS; i++) {

		pr = readl_relaxed(dist_base + GIC_DIST_PENDING_SET + i * 4);
		er = readl_relaxed(dist_base + GIC_DIST_ENABLE_SET + i * 4);

		if (pr & er)
			return true; /* There is a pending interrupt */
	}

	return false;
}

/*
 * This function checks if there are pending interrupt on the
 * prcmu which has been delegated to monitor the irqs with the
 * db8500_prcmu_copy_gic_settings function.
 */
bool prcmu_pending_irq(void)
{
	u32 it, im;
	int i;

	for (i = 0; i < PRCMU_GIC_NUMBER_REGS - 1; i++) {
		it = readl(PRCM_ARMITVAL31TO0 + i * 4);
		im = readl(PRCM_ARMITMSK31TO0 + i * 4);
		if (it & im)
			return true; /* There is a pending interrupt */
	}

	return false;
}

/*
 * This function checks if the specified cpu is in in WFI. It's usage
 * makes sense only if the gic is decoupled with the db8500_prcmu_gic_decouple
 * function. Of course passing smp_processor_id() to this function will
 * always return false...
 */
bool prcmu_is_cpu_in_wfi(int cpu)
{
	return readl(PRCM_ARM_WFI_STANDBY) &
		(cpu ? PRCM_ARM_WFI_STANDBY_WFI1 : PRCM_ARM_WFI_STANDBY_WFI0);
}

/*
 * This function copies the gic SPI settings to the prcmu in order to
 * monitor them and abort/finish the retention/off sequence or state.
 */
int prcmu_copy_gic_settings(void)
{
	u32 er; /* Enable register */
	int i;

	/* We skip the STI and PPI */
	for (i = 0; i < PRCMU_GIC_NUMBER_REGS - 1; i++) {
		er = readl_relaxed(dist_base +
				   GIC_DIST_ENABLE_SET + (i + 1) * 4);
		writel(er, PRCM_ARMITMSK31TO0 + i * 4);
	}

	return 0;
}

#ifdef CONFIG_SUSPEND
static int ux500_suspend_enter(suspend_state_t state)
{
	cpu_do_idle();
	return 0;
}

static int ux500_suspend_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM || state == PM_SUSPEND_STANDBY;
}

static const struct platform_suspend_ops ux500_suspend_ops = {
	.enter	      = ux500_suspend_enter,
	.valid	      = ux500_suspend_valid,
};
#define UX500_SUSPEND_OPS	(&ux500_suspend_ops)
#else
#define UX500_SUSPEND_OPS	NULL
#endif

void __init ux500_pm_init(u32 phy_base, u32 size)
{
	struct device_node *np;

	prcmu_base = ioremap(phy_base, size);
	if (!prcmu_base) {
		pr_err("could not remap PRCMU for PM functions\n");
		return;
	}
	np = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-gic");
	dist_base = of_iomap(np, 0);
	of_node_put(np);
	if (!dist_base) {
		pr_err("could not remap GIC dist base for PM functions\n");
		return;
	}

	/*
	 * On watchdog reboot the GIC is in some cases decoupled.
	 * This will make sure that the GIC is correctly configured.
	 */
	prcmu_gic_recouple();

	/* Set up ux500 suspend callbacks. */
	suspend_set_ops(UX500_SUSPEND_OPS);
}
