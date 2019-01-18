// SPDX-License-Identifier: GPL-2.0
/*
 * Interrupt support for Cirrus Logic Madera codecs
 *
 * Copyright (C) 2015-2018 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/irqchip/irq-madera.h>
#include <linux/mfd/madera/core.h>
#include <linux/mfd/madera/pdata.h>
#include <linux/mfd/madera/registers.h>

#define MADERA_IRQ(_irq, _reg)					\
	[MADERA_IRQ_ ## _irq] = {				\
		.reg_offset = (_reg) - MADERA_IRQ1_STATUS_2,	\
		.mask = MADERA_ ## _irq ## _EINT1		\
	}

/* Mappings are the same for all Madera codecs */
static const struct regmap_irq madera_irqs[MADERA_NUM_IRQ] = {
	MADERA_IRQ(FLL1_LOCK,		MADERA_IRQ1_STATUS_2),
	MADERA_IRQ(FLL2_LOCK,		MADERA_IRQ1_STATUS_2),
	MADERA_IRQ(FLL3_LOCK,		MADERA_IRQ1_STATUS_2),
	MADERA_IRQ(FLLAO_LOCK,		MADERA_IRQ1_STATUS_2),

	MADERA_IRQ(MICDET1,		MADERA_IRQ1_STATUS_6),
	MADERA_IRQ(MICDET2,		MADERA_IRQ1_STATUS_6),
	MADERA_IRQ(HPDET,		MADERA_IRQ1_STATUS_6),

	MADERA_IRQ(MICD_CLAMP_RISE,	MADERA_IRQ1_STATUS_7),
	MADERA_IRQ(MICD_CLAMP_FALL,	MADERA_IRQ1_STATUS_7),
	MADERA_IRQ(JD1_RISE,		MADERA_IRQ1_STATUS_7),
	MADERA_IRQ(JD1_FALL,		MADERA_IRQ1_STATUS_7),

	MADERA_IRQ(ASRC2_IN1_LOCK,	MADERA_IRQ1_STATUS_9),
	MADERA_IRQ(ASRC2_IN2_LOCK,	MADERA_IRQ1_STATUS_9),
	MADERA_IRQ(ASRC1_IN1_LOCK,	MADERA_IRQ1_STATUS_9),
	MADERA_IRQ(ASRC1_IN2_LOCK,	MADERA_IRQ1_STATUS_9),
	MADERA_IRQ(DRC2_SIG_DET,	MADERA_IRQ1_STATUS_9),
	MADERA_IRQ(DRC1_SIG_DET,	MADERA_IRQ1_STATUS_9),

	MADERA_IRQ(DSP_IRQ1,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ2,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ3,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ4,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ5,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ6,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ7,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ8,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ9,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ10,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ11,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ12,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ13,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ14,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ15,		MADERA_IRQ1_STATUS_11),
	MADERA_IRQ(DSP_IRQ16,		MADERA_IRQ1_STATUS_11),

	MADERA_IRQ(HP3R_SC,		MADERA_IRQ1_STATUS_12),
	MADERA_IRQ(HP3L_SC,		MADERA_IRQ1_STATUS_12),
	MADERA_IRQ(HP2R_SC,		MADERA_IRQ1_STATUS_12),
	MADERA_IRQ(HP2L_SC,		MADERA_IRQ1_STATUS_12),
	MADERA_IRQ(HP1R_SC,		MADERA_IRQ1_STATUS_12),
	MADERA_IRQ(HP1L_SC,		MADERA_IRQ1_STATUS_12),

	MADERA_IRQ(SPK_OVERHEAT_WARN,	MADERA_IRQ1_STATUS_15),
	MADERA_IRQ(SPK_OVERHEAT,	MADERA_IRQ1_STATUS_15),

	MADERA_IRQ(DSP1_BUS_ERR,	MADERA_IRQ1_STATUS_33),
	MADERA_IRQ(DSP2_BUS_ERR,	MADERA_IRQ1_STATUS_33),
	MADERA_IRQ(DSP3_BUS_ERR,	MADERA_IRQ1_STATUS_33),
	MADERA_IRQ(DSP4_BUS_ERR,	MADERA_IRQ1_STATUS_33),
	MADERA_IRQ(DSP5_BUS_ERR,	MADERA_IRQ1_STATUS_33),
	MADERA_IRQ(DSP6_BUS_ERR,	MADERA_IRQ1_STATUS_33),
	MADERA_IRQ(DSP7_BUS_ERR,	MADERA_IRQ1_STATUS_33),
};

static const struct regmap_irq_chip madera_irq_chip = {
	.name		= "madera IRQ",
	.status_base	= MADERA_IRQ1_STATUS_2,
	.mask_base	= MADERA_IRQ1_MASK_2,
	.ack_base	= MADERA_IRQ1_STATUS_2,
	.runtime_pm	= true,
	.num_regs	= 32,
	.irqs		= madera_irqs,
	.num_irqs	= ARRAY_SIZE(madera_irqs),
};

#ifdef CONFIG_PM_SLEEP
static int madera_suspend(struct device *dev)
{
	struct madera *madera = dev_get_drvdata(dev->parent);

	dev_dbg(madera->irq_dev, "Suspend, disabling IRQ\n");

	/*
	 * A runtime resume would be needed to access the chip interrupt
	 * controller but runtime pm doesn't function during suspend.
	 * Temporarily disable interrupts until we reach suspend_noirq state.
	 */
	disable_irq(madera->irq);

	return 0;
}

static int madera_suspend_noirq(struct device *dev)
{
	struct madera *madera = dev_get_drvdata(dev->parent);

	dev_dbg(madera->irq_dev, "No IRQ suspend, reenabling IRQ\n");

	/* Re-enable interrupts to service wakeup interrupts from the chip */
	enable_irq(madera->irq);

	return 0;
}

static int madera_resume_noirq(struct device *dev)
{
	struct madera *madera = dev_get_drvdata(dev->parent);

	dev_dbg(madera->irq_dev, "No IRQ resume, disabling IRQ\n");

	/*
	 * We can't handle interrupts until runtime pm is available again.
	 * Disable them temporarily.
	 */
	disable_irq(madera->irq);

	return 0;
}

static int madera_resume(struct device *dev)
{
	struct madera *madera = dev_get_drvdata(dev->parent);

	dev_dbg(madera->irq_dev, "Resume, reenabling IRQ\n");

	/* Interrupts can now be handled */
	enable_irq(madera->irq);

	return 0;
}
#endif

static const struct dev_pm_ops madera_irq_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(madera_suspend, madera_resume)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(madera_suspend_noirq,
				      madera_resume_noirq)
};

static int madera_irq_probe(struct platform_device *pdev)
{
	struct madera *madera = dev_get_drvdata(pdev->dev.parent);
	struct irq_data *irq_data;
	unsigned int irq_flags = 0;
	int ret;

	dev_dbg(&pdev->dev, "probe\n");

	/*
	 * Read the flags from the interrupt controller if not specified
	 * by pdata
	 */
	irq_flags = madera->pdata.irq_flags;
	if (!irq_flags) {
		irq_data = irq_get_irq_data(madera->irq);
		if (!irq_data) {
			dev_err(&pdev->dev, "Invalid IRQ: %d\n", madera->irq);
			return -EINVAL;
		}

		irq_flags = irqd_get_trigger_type(irq_data);

		/* Codec defaults to trigger low, use this if no flags given */
		if (irq_flags == IRQ_TYPE_NONE)
			irq_flags = IRQF_TRIGGER_LOW;
	}

	if (irq_flags & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) {
		dev_err(&pdev->dev, "Host interrupt not level-triggered\n");
		return -EINVAL;
	}

	/*
	 * The silicon always starts at active-low, check if we need to
	 * switch to active-high.
	 */
	if (irq_flags & IRQF_TRIGGER_HIGH) {
		ret = regmap_update_bits(madera->regmap, MADERA_IRQ1_CTRL,
					 MADERA_IRQ_POL_MASK, 0);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to set IRQ polarity: %d\n", ret);
			return ret;
		}
	}

	/*
	 * NOTE: regmap registers this against the OF node of the parent of
	 * the regmap - that is, against the mfd driver
	 */
	ret = regmap_add_irq_chip(madera->regmap, madera->irq, IRQF_ONESHOT, 0,
				  &madera_irq_chip, &madera->irq_data);
	if (ret) {
		dev_err(&pdev->dev, "add_irq_chip failed: %d\n", ret);
		return ret;
	}

	/* Save dev in parent MFD struct so it is accessible to siblings */
	madera->irq_dev = &pdev->dev;

	return 0;
}

static int madera_irq_remove(struct platform_device *pdev)
{
	struct madera *madera = dev_get_drvdata(pdev->dev.parent);

	/*
	 * The IRQ is disabled by the parent MFD driver before
	 * it starts cleaning up all child drivers
	 */
	madera->irq_dev = NULL;
	regmap_del_irq_chip(madera->irq, madera->irq_data);

	return 0;
}

static struct platform_driver madera_irq_driver = {
	.probe	= &madera_irq_probe,
	.remove = &madera_irq_remove,
	.driver = {
		.name	= "madera-irq",
		.pm	= &madera_irq_pm_ops,
	}
};
module_platform_driver(madera_irq_driver);

MODULE_SOFTDEP("pre: madera");
MODULE_DESCRIPTION("Madera IRQ driver");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
