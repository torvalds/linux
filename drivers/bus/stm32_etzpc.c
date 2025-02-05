// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023, STMicroelectronics - All Rights Reserved
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include "stm32_firewall.h"

/*
 * ETZPC registers
 */
#define ETZPC_DECPROT			0x10
#define ETZPC_HWCFGR			0x3F0

/*
 * HWCFGR register
 */
#define ETZPC_HWCFGR_NUM_TZMA		GENMASK(7, 0)
#define ETZPC_HWCFGR_NUM_PER_SEC	GENMASK(15, 8)
#define ETZPC_HWCFGR_NUM_AHB_SEC	GENMASK(23, 16)
#define ETZPC_HWCFGR_CHUNKS1N4		GENMASK(31, 24)

/*
 * ETZPC miscellaneous
 */
#define ETZPC_PROT_MASK			GENMASK(1, 0)
#define ETZPC_PROT_A7NS			0x3
#define ETZPC_DECPROT_SHIFT		1

#define IDS_PER_DECPROT_REGS		16

static int stm32_etzpc_grant_access(struct stm32_firewall_controller *ctrl, u32 firewall_id)
{
	u32 offset, reg_offset, sec_val;

	if (firewall_id >= ctrl->max_entries) {
		dev_err(ctrl->dev, "Invalid sys bus ID %u", firewall_id);
		return -EINVAL;
	}

	/* Check access configuration, 16 peripherals per register */
	reg_offset = ETZPC_DECPROT + 0x4 * (firewall_id / IDS_PER_DECPROT_REGS);
	offset = (firewall_id % IDS_PER_DECPROT_REGS) << ETZPC_DECPROT_SHIFT;

	/* Verify peripheral is non-secure and attributed to cortex A7 */
	sec_val = (readl(ctrl->mmio + reg_offset) >> offset) & ETZPC_PROT_MASK;
	if (sec_val != ETZPC_PROT_A7NS) {
		dev_dbg(ctrl->dev, "Invalid bus configuration: reg_offset %#x, value %d\n",
			reg_offset, sec_val);
		return -EACCES;
	}

	return 0;
}

static void stm32_etzpc_release_access(struct stm32_firewall_controller *ctrl __maybe_unused,
				       u32 firewall_id __maybe_unused)
{
}

static int stm32_etzpc_probe(struct platform_device *pdev)
{
	struct stm32_firewall_controller *etzpc_controller;
	struct device_node *np = pdev->dev.of_node;
	u32 nb_per, nb_master;
	struct resource *res;
	void __iomem *mmio;
	int rc;

	etzpc_controller = devm_kzalloc(&pdev->dev, sizeof(*etzpc_controller), GFP_KERNEL);
	if (!etzpc_controller)
		return -ENOMEM;

	mmio = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(mmio))
		return PTR_ERR(mmio);

	etzpc_controller->dev = &pdev->dev;
	etzpc_controller->mmio = mmio;
	etzpc_controller->name = dev_driver_string(etzpc_controller->dev);
	etzpc_controller->type = STM32_PERIPHERAL_FIREWALL | STM32_MEMORY_FIREWALL;
	etzpc_controller->grant_access = stm32_etzpc_grant_access;
	etzpc_controller->release_access = stm32_etzpc_release_access;

	/* Get number of etzpc entries*/
	nb_per = FIELD_GET(ETZPC_HWCFGR_NUM_PER_SEC,
			   readl(etzpc_controller->mmio + ETZPC_HWCFGR));
	nb_master = FIELD_GET(ETZPC_HWCFGR_NUM_AHB_SEC,
			      readl(etzpc_controller->mmio + ETZPC_HWCFGR));
	etzpc_controller->max_entries = nb_per + nb_master;

	platform_set_drvdata(pdev, etzpc_controller);

	rc = stm32_firewall_controller_register(etzpc_controller);
	if (rc) {
		dev_err(etzpc_controller->dev, "Couldn't register as a firewall controller: %d",
			rc);
		return rc;
	}

	rc = stm32_firewall_populate_bus(etzpc_controller);
	if (rc) {
		dev_err(etzpc_controller->dev, "Couldn't populate ETZPC bus: %d",
			rc);
		return rc;
	}

	/* Populate all allowed nodes */
	return of_platform_populate(np, NULL, NULL, &pdev->dev);
}

static const struct of_device_id stm32_etzpc_of_match[] = {
	{ .compatible = "st,stm32-etzpc" },
	{}
};
MODULE_DEVICE_TABLE(of, stm32_etzpc_of_match);

static struct platform_driver stm32_etzpc_driver = {
	.probe  = stm32_etzpc_probe,
	.driver = {
		.name = "stm32-etzpc",
		.of_match_table = stm32_etzpc_of_match,
	},
};
module_platform_driver(stm32_etzpc_driver);

MODULE_AUTHOR("Gatien Chevallier <gatien.chevallier@foss.st.com>");
MODULE_DESCRIPTION("STMicroelectronics ETZPC driver");
MODULE_LICENSE("GPL");
