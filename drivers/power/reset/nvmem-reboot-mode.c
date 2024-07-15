// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) Vaisala Oyj. All rights reserved.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>
#include <linux/reboot-mode.h>

struct nvmem_reboot_mode {
	struct reboot_mode_driver reboot;
	struct nvmem_cell *cell;
};

static int nvmem_reboot_mode_write(struct reboot_mode_driver *reboot,
				    unsigned int magic)
{
	int ret;
	struct nvmem_reboot_mode *nvmem_rbm;

	nvmem_rbm = container_of(reboot, struct nvmem_reboot_mode, reboot);

	ret = nvmem_cell_write(nvmem_rbm->cell, &magic, sizeof(magic));
	if (ret < 0)
		dev_err(reboot->dev, "update reboot mode bits failed\n");

	return ret;
}

static int nvmem_reboot_mode_probe(struct platform_device *pdev)
{
	int ret;
	struct nvmem_reboot_mode *nvmem_rbm;

	nvmem_rbm = devm_kzalloc(&pdev->dev, sizeof(*nvmem_rbm), GFP_KERNEL);
	if (!nvmem_rbm)
		return -ENOMEM;

	nvmem_rbm->reboot.dev = &pdev->dev;
	nvmem_rbm->reboot.write = nvmem_reboot_mode_write;

	nvmem_rbm->cell = devm_nvmem_cell_get(&pdev->dev, "reboot-mode");
	if (IS_ERR(nvmem_rbm->cell)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(nvmem_rbm->cell),
				     "failed to get the nvmem cell reboot-mode\n");
	}

	ret = devm_reboot_mode_register(&pdev->dev, &nvmem_rbm->reboot);
	if (ret)
		dev_err(&pdev->dev, "can't register reboot mode\n");

	return ret;
}

static const struct of_device_id nvmem_reboot_mode_of_match[] = {
	{ .compatible = "nvmem-reboot-mode" },
	{}
};
MODULE_DEVICE_TABLE(of, nvmem_reboot_mode_of_match);

static struct platform_driver nvmem_reboot_mode_driver = {
	.probe = nvmem_reboot_mode_probe,
	.driver = {
		.name = "nvmem-reboot-mode",
		.of_match_table = nvmem_reboot_mode_of_match,
	},
};
module_platform_driver(nvmem_reboot_mode_driver);

MODULE_AUTHOR("Nandor Han <nandor.han@vaisala.com>");
MODULE_DESCRIPTION("NVMEM reboot mode driver");
MODULE_LICENSE("GPL");
