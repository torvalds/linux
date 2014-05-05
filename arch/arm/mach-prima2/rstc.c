/*
 * reset controller for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/reset-controller.h>

#include <asm/system_misc.h>

#define SIRFSOC_RSTBIT_NUM	64

static void __iomem *sirfsoc_rstc_base;
static DEFINE_MUTEX(rstc_lock);

static int sirfsoc_reset_module(struct reset_controller_dev *rcdev,
					unsigned long sw_reset_idx)
{
	u32 reset_bit = sw_reset_idx;

	if (reset_bit >= SIRFSOC_RSTBIT_NUM)
		return -EINVAL;

	mutex_lock(&rstc_lock);

	if (of_device_is_compatible(rcdev->of_node, "sirf,prima2-rstc")) {
		/*
		 * Writing 1 to this bit resets corresponding block. Writing 0 to this
		 * bit de-asserts reset signal of the corresponding block.
		 * datasheet doesn't require explicit delay between the set and clear
		 * of reset bit. it could be shorter if tests pass.
		 */
		writel(readl(sirfsoc_rstc_base + (reset_bit / 32) * 4) | (1 << reset_bit),
			sirfsoc_rstc_base + (reset_bit / 32) * 4);
		msleep(10);
		writel(readl(sirfsoc_rstc_base + (reset_bit / 32) * 4) & ~(1 << reset_bit),
			sirfsoc_rstc_base + (reset_bit / 32) * 4);
	} else {
		/*
		 * For MARCO and POLO
		 * Writing 1 to SET register resets corresponding block. Writing 1 to CLEAR
		 * register de-asserts reset signal of the corresponding block.
		 * datasheet doesn't require explicit delay between the set and clear
		 * of reset bit. it could be shorter if tests pass.
		 */
		writel(1 << reset_bit, sirfsoc_rstc_base + (reset_bit / 32) * 8);
		msleep(10);
		writel(1 << reset_bit, sirfsoc_rstc_base + (reset_bit / 32) * 8 + 4);
	}

	mutex_unlock(&rstc_lock);

	return 0;
}

static struct reset_control_ops sirfsoc_rstc_ops = {
	.reset = sirfsoc_reset_module,
};

static struct reset_controller_dev sirfsoc_reset_controller = {
	.ops = &sirfsoc_rstc_ops,
	.nr_resets = SIRFSOC_RSTBIT_NUM,
};

#define SIRFSOC_SYS_RST_BIT  BIT(31)

static void sirfsoc_restart(enum reboot_mode mode, const char *cmd)
{
	writel(SIRFSOC_SYS_RST_BIT, sirfsoc_rstc_base);
}

static int sirfsoc_rstc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	sirfsoc_rstc_base = of_iomap(np, 0);
	if (!sirfsoc_rstc_base) {
		dev_err(&pdev->dev, "unable to map rstc cpu registers\n");
		return -ENOMEM;
	}

	sirfsoc_reset_controller.of_node = np;
	arm_pm_restart = sirfsoc_restart;

	if (IS_ENABLED(CONFIG_RESET_CONTROLLER))
		reset_controller_register(&sirfsoc_reset_controller);

	return 0;
}

static const struct of_device_id rstc_ids[]  = {
	{ .compatible = "sirf,prima2-rstc" },
	{ .compatible = "sirf,marco-rstc" },
	{},
};

static struct platform_driver sirfsoc_rstc_driver = {
	.probe		= sirfsoc_rstc_probe,
	.driver		= {
		.name	= "sirfsoc_rstc",
		.owner	= THIS_MODULE,
		.of_match_table = rstc_ids,
	},
};

static int __init sirfsoc_rstc_init(void)
{
	return platform_driver_register(&sirfsoc_rstc_driver);
}
subsys_initcall(sirfsoc_rstc_init);
