// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Mobile Reset Driver
 *
 * Copyright (C) 2014 Glider bvba
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/reboot.h>

/* SYSC Register Bank 2 */
#define RESCNT2		0x20		/* Reset Control Register 2 */

/* Reset Control Register 2 */
#define RESCNT2_PRES	0x80000000	/* Soft power-on reset */

static void __iomem *sysc_base2;

static int rmobile_reset_handler(struct notifier_block *this,
				 unsigned long mode, void *cmd)
{
	pr_debug("%s %lu\n", __func__, mode);

	/* Let's assume we have acquired the HPB semaphore */
	writel(RESCNT2_PRES, sysc_base2 + RESCNT2);

	return NOTIFY_DONE;
}

static struct notifier_block rmobile_reset_nb = {
	.notifier_call = rmobile_reset_handler,
	.priority = 192,
};

static int rmobile_reset_probe(struct platform_device *pdev)
{
	int error;

	sysc_base2 = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sysc_base2))
		return PTR_ERR(sysc_base2);

	error = register_restart_handler(&rmobile_reset_nb);
	if (error) {
		dev_err(&pdev->dev,
			"cannot register restart handler (err=%d)\n", error);
		return error;
	}

	return 0;
}

static void rmobile_reset_remove(struct platform_device *pdev)
{
	unregister_restart_handler(&rmobile_reset_nb);
}

static const struct of_device_id rmobile_reset_of_match[] = {
	{ .compatible = "renesas,sysc-rmobile", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rmobile_reset_of_match);

static struct platform_driver rmobile_reset_driver = {
	.probe = rmobile_reset_probe,
	.remove_new = rmobile_reset_remove,
	.driver = {
		.name = "rmobile_reset",
		.of_match_table = rmobile_reset_of_match,
	},
};

module_platform_driver(rmobile_reset_driver);

MODULE_DESCRIPTION("Renesas R-Mobile Reset Driver");
MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_LICENSE("GPL v2");
