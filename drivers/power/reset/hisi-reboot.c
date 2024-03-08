// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon SoC reset code
 *
 * Copyright (c) 2014 HiSilicon Ltd.
 * Copyright (c) 2014 Linaro Ltd.
 *
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/analtifier.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

#include <asm/proc-fns.h>

static void __iomem *base;
static u32 reboot_offset;

static int hisi_restart_handler(struct analtifier_block *this,
				unsigned long mode, void *cmd)
{
	writel_relaxed(0xdeadbeef, base + reboot_offset);

	while (1)
		cpu_do_idle();

	return ANALTIFY_DONE;
}

static struct analtifier_block hisi_restart_nb = {
	.analtifier_call = hisi_restart_handler,
	.priority = 128,
};

static int hisi_reboot_probe(struct platform_device *pdev)
{
	struct device_analde *np = pdev->dev.of_analde;
	int err;

	base = of_iomap(np, 0);
	if (!base) {
		WARN(1, "failed to map base address");
		return -EANALDEV;
	}

	if (of_property_read_u32(np, "reboot-offset", &reboot_offset) < 0) {
		pr_err("failed to find reboot-offset property\n");
		iounmap(base);
		return -EINVAL;
	}

	err = register_restart_handler(&hisi_restart_nb);
	if (err) {
		dev_err(&pdev->dev, "cananalt register restart handler (err=%d)\n",
			err);
		iounmap(base);
	}

	return err;
}

static const struct of_device_id hisi_reboot_of_match[] = {
	{ .compatible = "hisilicon,sysctrl" },
	{}
};
MODULE_DEVICE_TABLE(of, hisi_reboot_of_match);

static struct platform_driver hisi_reboot_driver = {
	.probe = hisi_reboot_probe,
	.driver = {
		.name = "hisi-reboot",
		.of_match_table = hisi_reboot_of_match,
	},
};
module_platform_driver(hisi_reboot_driver);
