// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Texas Instruments DDR3 ECC error correction and detection driver
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/edac.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/module.h>

#include "edac_module.h"

/* EMIF controller registers */
#define EMIF_SDRAM_CONFIG		0x008
#define EMIF_IRQ_STATUS			0x0ac
#define EMIF_IRQ_ENABLE_SET		0x0b4
#define EMIF_ECC_CTRL			0x110
#define EMIF_1B_ECC_ERR_CNT		0x130
#define EMIF_1B_ECC_ERR_THRSH		0x134
#define EMIF_1B_ECC_ERR_ADDR_LOG	0x13c
#define EMIF_2B_ECC_ERR_ADDR_LOG	0x140

/* Bit definitions for EMIF_SDRAM_CONFIG */
#define SDRAM_TYPE_SHIFT		29
#define SDRAM_TYPE_MASK			GENMASK(31, 29)
#define SDRAM_TYPE_DDR3			(3 << SDRAM_TYPE_SHIFT)
#define SDRAM_TYPE_DDR2			(2 << SDRAM_TYPE_SHIFT)
#define SDRAM_NARROW_MODE_MASK		GENMASK(15, 14)
#define SDRAM_K2_NARROW_MODE_SHIFT	12
#define SDRAM_K2_NARROW_MODE_MASK	GENMASK(13, 12)
#define SDRAM_ROWSIZE_SHIFT		7
#define SDRAM_ROWSIZE_MASK		GENMASK(9, 7)
#define SDRAM_IBANK_SHIFT		4
#define SDRAM_IBANK_MASK		GENMASK(6, 4)
#define SDRAM_K2_IBANK_SHIFT		5
#define SDRAM_K2_IBANK_MASK		GENMASK(6, 5)
#define SDRAM_K2_EBANK_SHIFT		3
#define SDRAM_K2_EBANK_MASK		BIT(SDRAM_K2_EBANK_SHIFT)
#define SDRAM_PAGESIZE_SHIFT		0
#define SDRAM_PAGESIZE_MASK		GENMASK(2, 0)
#define SDRAM_K2_PAGESIZE_SHIFT		0
#define SDRAM_K2_PAGESIZE_MASK		GENMASK(1, 0)

#define EMIF_1B_ECC_ERR_THRSH_SHIFT	24

/* IRQ bit definitions */
#define EMIF_1B_ECC_ERR			BIT(5)
#define EMIF_2B_ECC_ERR			BIT(4)
#define EMIF_WR_ECC_ERR			BIT(3)
#define EMIF_SYS_ERR			BIT(0)
/* Bit 31 enables ECC and 28 enables RMW */
#define ECC_ENABLED			(BIT(31) | BIT(28))

#define EDAC_MOD_NAME			"ti-emif-edac"

enum {
	EMIF_TYPE_DRA7,
	EMIF_TYPE_K2
};

struct ti_edac {
	void __iomem *reg;
};

static u32 ti_edac_readl(struct ti_edac *edac, u16 offset)
{
	return readl_relaxed(edac->reg + offset);
}

static void ti_edac_writel(struct ti_edac *edac, u32 val, u16 offset)
{
	writel_relaxed(val, edac->reg + offset);
}

static irqreturn_t ti_edac_isr(int irq, void *data)
{
	struct mem_ctl_info *mci = data;
	struct ti_edac *edac = mci->pvt_info;
	u32 irq_status;
	u32 err_addr;
	int err_count;

	irq_status = ti_edac_readl(edac, EMIF_IRQ_STATUS);

	if (irq_status & EMIF_1B_ECC_ERR) {
		err_addr = ti_edac_readl(edac, EMIF_1B_ECC_ERR_ADDR_LOG);
		err_count = ti_edac_readl(edac, EMIF_1B_ECC_ERR_CNT);
		ti_edac_writel(edac, err_count, EMIF_1B_ECC_ERR_CNT);
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, err_count,
				     err_addr >> PAGE_SHIFT,
				     err_addr & ~PAGE_MASK, -1, 0, 0, 0,
				     mci->ctl_name, "1B");
	}

	if (irq_status & EMIF_2B_ECC_ERR) {
		err_addr = ti_edac_readl(edac, EMIF_2B_ECC_ERR_ADDR_LOG);
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
				     err_addr >> PAGE_SHIFT,
				     err_addr & ~PAGE_MASK, -1, 0, 0, 0,
				     mci->ctl_name, "2B");
	}

	if (irq_status & EMIF_WR_ECC_ERR)
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
				     0, 0, -1, 0, 0, 0,
				     mci->ctl_name, "WR");

	ti_edac_writel(edac, irq_status, EMIF_IRQ_STATUS);

	return IRQ_HANDLED;
}

static void ti_edac_setup_dimm(struct mem_ctl_info *mci, u32 type)
{
	struct dimm_info *dimm;
	struct ti_edac *edac = mci->pvt_info;
	int bits;
	u32 val;
	u32 memsize;

	dimm = edac_get_dimm(mci, 0, 0, 0);

	val = ti_edac_readl(edac, EMIF_SDRAM_CONFIG);

	if (type == EMIF_TYPE_DRA7) {
		bits = ((val & SDRAM_PAGESIZE_MASK) >> SDRAM_PAGESIZE_SHIFT) + 8;
		bits += ((val & SDRAM_ROWSIZE_MASK) >> SDRAM_ROWSIZE_SHIFT) + 9;
		bits += (val & SDRAM_IBANK_MASK) >> SDRAM_IBANK_SHIFT;

		if (val & SDRAM_NARROW_MODE_MASK) {
			bits++;
			dimm->dtype = DEV_X16;
		} else {
			bits += 2;
			dimm->dtype = DEV_X32;
		}
	} else {
		bits = 16;
		bits += ((val & SDRAM_K2_PAGESIZE_MASK) >>
			SDRAM_K2_PAGESIZE_SHIFT) + 8;
		bits += (val & SDRAM_K2_IBANK_MASK) >> SDRAM_K2_IBANK_SHIFT;
		bits += (val & SDRAM_K2_EBANK_MASK) >> SDRAM_K2_EBANK_SHIFT;

		val = (val & SDRAM_K2_NARROW_MODE_MASK) >>
			SDRAM_K2_NARROW_MODE_SHIFT;
		switch (val) {
		case 0:
			bits += 3;
			dimm->dtype = DEV_X64;
			break;
		case 1:
			bits += 2;
			dimm->dtype = DEV_X32;
			break;
		case 2:
			bits++;
			dimm->dtype = DEV_X16;
			break;
		}
	}

	memsize = 1 << bits;

	dimm->nr_pages = memsize >> PAGE_SHIFT;
	dimm->grain = 4;
	if ((val & SDRAM_TYPE_MASK) == SDRAM_TYPE_DDR2)
		dimm->mtype = MEM_DDR2;
	else
		dimm->mtype = MEM_DDR3;

	val = ti_edac_readl(edac, EMIF_ECC_CTRL);
	if (val & ECC_ENABLED)
		dimm->edac_mode = EDAC_SECDED;
	else
		dimm->edac_mode = EDAC_NONE;
}

static const struct of_device_id ti_edac_of_match[] = {
	{ .compatible = "ti,emif-keystone", .data = (void *)EMIF_TYPE_K2 },
	{ .compatible = "ti,emif-dra7xx", .data = (void *)EMIF_TYPE_DRA7 },
	{},
};

static int _emif_get_id(struct device_node *node)
{
	struct device_node *np;
	const __be32 *addrp;
	u32 addr, my_addr;
	int my_id = 0;

	addrp = of_get_address(node, 0, NULL, NULL);
	my_addr = (u32)of_translate_address(node, addrp);

	for_each_matching_node(np, ti_edac_of_match) {
		if (np == node)
			continue;

		addrp = of_get_address(np, 0, NULL, NULL);
		addr = (u32)of_translate_address(np, addrp);

		edac_printk(KERN_INFO, EDAC_MOD_NAME,
			    "addr=%x, my_addr=%x\n",
			    addr, my_addr);

		if (addr < my_addr)
			my_id++;
	}

	return my_id;
}

static int ti_edac_probe(struct platform_device *pdev)
{
	int error_irq = 0, ret = -ENODEV;
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *reg;
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[1];
	const struct of_device_id *id;
	struct ti_edac *edac;
	int emif_id;

	id = of_match_device(ti_edac_of_match, &pdev->dev);
	if (!id)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(reg)) {
		edac_printk(KERN_ERR, EDAC_MOD_NAME,
			    "EMIF controller regs not defined\n");
		return PTR_ERR(reg);
	}

	layers[0].type = EDAC_MC_LAYER_ALL_MEM;
	layers[0].size = 1;

	/* Allocate ID number for our EMIF controller */
	emif_id = _emif_get_id(pdev->dev.of_node);
	if (emif_id < 0)
		return -EINVAL;

	mci = edac_mc_alloc(emif_id, 1, layers, sizeof(*edac));
	if (!mci)
		return -ENOMEM;

	mci->pdev = &pdev->dev;
	edac = mci->pvt_info;
	edac->reg = reg;
	platform_set_drvdata(pdev, mci);

	mci->mtype_cap = MEM_FLAG_DDR3 | MEM_FLAG_DDR2;
	mci->edac_ctl_cap = EDAC_FLAG_SECDED | EDAC_FLAG_NONE;
	mci->mod_name = EDAC_MOD_NAME;
	mci->ctl_name = id->compatible;
	mci->dev_name = dev_name(&pdev->dev);

	/* Setup memory layout */
	ti_edac_setup_dimm(mci, (u32)(id->data));

	/* add EMIF ECC error handler */
	error_irq = platform_get_irq(pdev, 0);
	if (error_irq < 0) {
		ret = error_irq;
		edac_printk(KERN_ERR, EDAC_MOD_NAME,
			    "EMIF irq number not defined.\n");
		goto err;
	}

	ret = devm_request_irq(dev, error_irq, ti_edac_isr, 0,
			       "emif-edac-irq", mci);
	if (ret) {
		edac_printk(KERN_ERR, EDAC_MOD_NAME,
			    "request_irq fail for EMIF EDAC irq\n");
		goto err;
	}

	ret = edac_mc_add_mc(mci);
	if (ret) {
		edac_printk(KERN_ERR, EDAC_MOD_NAME,
			    "Failed to register mci: %d.\n", ret);
		goto err;
	}

	/* Generate an interrupt with each 1b error */
	ti_edac_writel(edac, 1 << EMIF_1B_ECC_ERR_THRSH_SHIFT,
		       EMIF_1B_ECC_ERR_THRSH);

	/* Enable interrupts */
	ti_edac_writel(edac,
		       EMIF_1B_ECC_ERR | EMIF_2B_ECC_ERR | EMIF_WR_ECC_ERR,
		       EMIF_IRQ_ENABLE_SET);

	return 0;

err:
	edac_mc_free(mci);
	return ret;
}

static int ti_edac_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);

	return 0;
}

static struct platform_driver ti_edac_driver = {
	.probe = ti_edac_probe,
	.remove = ti_edac_remove,
	.driver = {
		   .name = EDAC_MOD_NAME,
		   .of_match_table = ti_edac_of_match,
	},
};

module_platform_driver(ti_edac_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("EDAC Driver for Texas Instruments DDR3 MC");
MODULE_LICENSE("GPL v2");
