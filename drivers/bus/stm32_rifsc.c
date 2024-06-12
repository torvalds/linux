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
 * RIFSC offset register
 */
#define RIFSC_RISC_SECCFGR0		0x10
#define RIFSC_RISC_PRIVCFGR0		0x30
#define RIFSC_RISC_PER0_CIDCFGR		0x100
#define RIFSC_RISC_PER0_SEMCR		0x104
#define RIFSC_RISC_HWCFGR2		0xFEC

/*
 * SEMCR register
 */
#define SEMCR_MUTEX			BIT(0)

/*
 * HWCFGR2 register
 */
#define HWCFGR2_CONF1_MASK		GENMASK(15, 0)
#define HWCFGR2_CONF2_MASK		GENMASK(23, 16)
#define HWCFGR2_CONF3_MASK		GENMASK(31, 24)

/*
 * RIFSC miscellaneous
 */
#define RIFSC_RISC_CFEN_MASK		BIT(0)
#define RIFSC_RISC_SEM_EN_MASK		BIT(1)
#define RIFSC_RISC_SCID_MASK		GENMASK(6, 4)
#define RIFSC_RISC_SEML_SHIFT		16
#define RIFSC_RISC_SEMWL_MASK		GENMASK(23, 16)
#define RIFSC_RISC_PER_ID_MASK		GENMASK(31, 24)

#define RIFSC_RISC_PERx_CID_MASK	(RIFSC_RISC_CFEN_MASK | \
					 RIFSC_RISC_SEM_EN_MASK | \
					 RIFSC_RISC_SCID_MASK | \
					 RIFSC_RISC_SEMWL_MASK)

#define IDS_PER_RISC_SEC_PRIV_REGS	32

/* RIF miscellaneous */
/*
 * CIDCFGR register fields
 */
#define CIDCFGR_CFEN			BIT(0)
#define CIDCFGR_SEMEN			BIT(1)
#define CIDCFGR_SEMWL(x)		BIT(RIFSC_RISC_SEML_SHIFT + (x))

#define SEMWL_SHIFT			16

/* Compartiment IDs */
#define RIF_CID0			0x0
#define RIF_CID1			0x1

static bool stm32_rifsc_is_semaphore_available(void __iomem *addr)
{
	return !(readl(addr) & SEMCR_MUTEX);
}

static int stm32_rif_acquire_semaphore(struct stm32_firewall_controller *stm32_firewall_controller,
				       int id)
{
	void __iomem *addr = stm32_firewall_controller->mmio + RIFSC_RISC_PER0_SEMCR + 0x8 * id;

	writel(SEMCR_MUTEX, addr);

	/* Check that CID1 has the semaphore */
	if (stm32_rifsc_is_semaphore_available(addr) ||
	    FIELD_GET(RIFSC_RISC_SCID_MASK, readl(addr)) != RIF_CID1)
		return -EACCES;

	return 0;
}

static void stm32_rif_release_semaphore(struct stm32_firewall_controller *stm32_firewall_controller,
					int id)
{
	void __iomem *addr = stm32_firewall_controller->mmio + RIFSC_RISC_PER0_SEMCR + 0x8 * id;

	if (stm32_rifsc_is_semaphore_available(addr))
		return;

	writel(SEMCR_MUTEX, addr);

	/* Ok if another compartment takes the semaphore before the check */
	WARN_ON(!stm32_rifsc_is_semaphore_available(addr) &&
		FIELD_GET(RIFSC_RISC_SCID_MASK, readl(addr)) == RIF_CID1);
}

static int stm32_rifsc_grant_access(struct stm32_firewall_controller *ctrl, u32 firewall_id)
{
	struct stm32_firewall_controller *rifsc_controller = ctrl;
	u32 reg_offset, reg_id, sec_reg_value, cid_reg_value;
	int rc;

	if (firewall_id >= rifsc_controller->max_entries) {
		dev_err(rifsc_controller->dev, "Invalid sys bus ID %u", firewall_id);
		return -EINVAL;
	}

	/*
	 * RIFSC_RISC_PRIVCFGRx and RIFSC_RISC_SECCFGRx both handle configuration access for
	 * 32 peripherals. On the other hand, there is one _RIFSC_RISC_PERx_CIDCFGR register
	 * per peripheral
	 */
	reg_id = firewall_id / IDS_PER_RISC_SEC_PRIV_REGS;
	reg_offset = firewall_id % IDS_PER_RISC_SEC_PRIV_REGS;
	sec_reg_value = readl(rifsc_controller->mmio + RIFSC_RISC_SECCFGR0 + 0x4 * reg_id);
	cid_reg_value = readl(rifsc_controller->mmio + RIFSC_RISC_PER0_CIDCFGR + 0x8 * firewall_id);

	/* First check conditions for semaphore mode, which doesn't take into account static CID. */
	if ((cid_reg_value & CIDCFGR_SEMEN) && (cid_reg_value & CIDCFGR_CFEN)) {
		if (cid_reg_value & BIT(RIF_CID1 + SEMWL_SHIFT)) {
			/* Static CID is irrelevant if semaphore mode */
			goto skip_cid_check;
		} else {
			dev_dbg(rifsc_controller->dev,
				"Invalid bus semaphore configuration: index %d\n", firewall_id);
			return -EACCES;
		}
	}

	/*
	 * Skip CID check if CID filtering isn't enabled or filtering is enabled on CID0, which
	 * corresponds to whatever CID.
	 */
	if (!(cid_reg_value & CIDCFGR_CFEN) ||
	    FIELD_GET(RIFSC_RISC_SCID_MASK, cid_reg_value) == RIF_CID0)
		goto skip_cid_check;

	/* Coherency check with the CID configuration */
	if (FIELD_GET(RIFSC_RISC_SCID_MASK, cid_reg_value) != RIF_CID1) {
		dev_dbg(rifsc_controller->dev, "Invalid CID configuration for peripheral: %d\n",
			firewall_id);
		return -EACCES;
	}

skip_cid_check:
	/* Check security configuration */
	if (sec_reg_value & BIT(reg_offset)) {
		dev_dbg(rifsc_controller->dev,
			"Invalid security configuration for peripheral: %d\n", firewall_id);
		return -EACCES;
	}

	/*
	 * If the peripheral is in semaphore mode, take the semaphore so that
	 * the CID1 has the ownership.
	 */
	if ((cid_reg_value & CIDCFGR_SEMEN) && (cid_reg_value & CIDCFGR_CFEN)) {
		rc = stm32_rif_acquire_semaphore(rifsc_controller, firewall_id);
		if (rc) {
			dev_err(rifsc_controller->dev,
				"Couldn't acquire semaphore for peripheral: %d\n", firewall_id);
			return rc;
		}
	}

	return 0;
}

static void stm32_rifsc_release_access(struct stm32_firewall_controller *ctrl, u32 firewall_id)
{
	stm32_rif_release_semaphore(ctrl, firewall_id);
}

static int stm32_rifsc_probe(struct platform_device *pdev)
{
	struct stm32_firewall_controller *rifsc_controller;
	struct device_node *np = pdev->dev.of_node;
	u32 nb_risup, nb_rimu, nb_risal;
	struct resource *res;
	void __iomem *mmio;
	int rc;

	rifsc_controller = devm_kzalloc(&pdev->dev, sizeof(*rifsc_controller), GFP_KERNEL);
	if (!rifsc_controller)
		return -ENOMEM;

	mmio = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(mmio))
		return PTR_ERR(mmio);

	rifsc_controller->dev = &pdev->dev;
	rifsc_controller->mmio = mmio;
	rifsc_controller->name = dev_driver_string(rifsc_controller->dev);
	rifsc_controller->type = STM32_PERIPHERAL_FIREWALL | STM32_MEMORY_FIREWALL;
	rifsc_controller->grant_access = stm32_rifsc_grant_access;
	rifsc_controller->release_access = stm32_rifsc_release_access;

	/* Get number of RIFSC entries*/
	nb_risup = readl(rifsc_controller->mmio + RIFSC_RISC_HWCFGR2) & HWCFGR2_CONF1_MASK;
	nb_rimu = readl(rifsc_controller->mmio + RIFSC_RISC_HWCFGR2) & HWCFGR2_CONF2_MASK;
	nb_risal = readl(rifsc_controller->mmio + RIFSC_RISC_HWCFGR2) & HWCFGR2_CONF3_MASK;
	rifsc_controller->max_entries = nb_risup + nb_rimu + nb_risal;

	platform_set_drvdata(pdev, rifsc_controller);

	rc = stm32_firewall_controller_register(rifsc_controller);
	if (rc) {
		dev_err(rifsc_controller->dev, "Couldn't register as a firewall controller: %d",
			rc);
		return rc;
	}

	rc = stm32_firewall_populate_bus(rifsc_controller);
	if (rc) {
		dev_err(rifsc_controller->dev, "Couldn't populate RIFSC bus: %d",
			rc);
		return rc;
	}

	/* Populate all allowed nodes */
	return of_platform_populate(np, NULL, NULL, &pdev->dev);
}

static const struct of_device_id stm32_rifsc_of_match[] = {
	{ .compatible = "st,stm32mp25-rifsc" },
	{}
};
MODULE_DEVICE_TABLE(of, stm32_rifsc_of_match);

static struct platform_driver stm32_rifsc_driver = {
	.probe  = stm32_rifsc_probe,
	.driver = {
		.name = "stm32-rifsc",
		.of_match_table = stm32_rifsc_of_match,
	},
};
module_platform_driver(stm32_rifsc_driver);

MODULE_AUTHOR("Gatien Chevallier <gatien.chevallier@foss.st.com>");
MODULE_DESCRIPTION("STMicroelectronics RIFSC driver");
MODULE_LICENSE("GPL");
