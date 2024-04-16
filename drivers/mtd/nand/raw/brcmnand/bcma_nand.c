// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2021 Broadcom
 */
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_driver_chipcommon.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "brcmnand.h"

struct brcmnand_bcma_soc {
	struct brcmnand_soc soc;
	struct bcma_drv_cc *cc;
};

static inline bool brcmnand_bcma_needs_swapping(u32 offset)
{
	switch (offset) {
	case BCMA_CC_NAND_SPARE_RD0:
	case BCMA_CC_NAND_SPARE_RD4:
	case BCMA_CC_NAND_SPARE_RD8:
	case BCMA_CC_NAND_SPARE_RD12:
	case BCMA_CC_NAND_SPARE_WR0:
	case BCMA_CC_NAND_SPARE_WR4:
	case BCMA_CC_NAND_SPARE_WR8:
	case BCMA_CC_NAND_SPARE_WR12:
	case BCMA_CC_NAND_DEVID:
	case BCMA_CC_NAND_DEVID_X:
	case BCMA_CC_NAND_SPARE_RD16:
	case BCMA_CC_NAND_SPARE_RD20:
	case BCMA_CC_NAND_SPARE_RD24:
	case BCMA_CC_NAND_SPARE_RD28:
		return true;
	}

	return false;
}

static inline struct brcmnand_bcma_soc *to_bcma_soc(struct brcmnand_soc *soc)
{
	return container_of(soc, struct brcmnand_bcma_soc, soc);
}

static u32 brcmnand_bcma_read_reg(struct brcmnand_soc *soc, u32 offset)
{
	struct brcmnand_bcma_soc *sc = to_bcma_soc(soc);
	u32 val;

	/* Offset into the NAND block and deal with the flash cache separately */
	if (offset == BRCMNAND_NON_MMIO_FC_ADDR)
		offset = BCMA_CC_NAND_CACHE_DATA;
	else
		offset += BCMA_CC_NAND_REVISION;

	val = bcma_cc_read32(sc->cc, offset);

	/* Swap if necessary */
	if (brcmnand_bcma_needs_swapping(offset))
		val = be32_to_cpu((__force __be32)val);
	return val;
}

static void brcmnand_bcma_write_reg(struct brcmnand_soc *soc, u32 val,
				    u32 offset)
{
	struct brcmnand_bcma_soc *sc = to_bcma_soc(soc);

	/* Offset into the NAND block */
	if (offset == BRCMNAND_NON_MMIO_FC_ADDR)
		offset = BCMA_CC_NAND_CACHE_DATA;
	else
		offset += BCMA_CC_NAND_REVISION;

	/* Swap if necessary */
	if (brcmnand_bcma_needs_swapping(offset))
		val = (__force u32)cpu_to_be32(val);

	bcma_cc_write32(sc->cc, offset, val);
}

static struct brcmnand_io_ops brcmnand_bcma_io_ops = {
	.read_reg	= brcmnand_bcma_read_reg,
	.write_reg	= brcmnand_bcma_write_reg,
};

static void brcmnand_bcma_prepare_data_bus(struct brcmnand_soc *soc, bool prepare,
					   bool is_param)
{
	struct brcmnand_bcma_soc *sc = to_bcma_soc(soc);

	/* Reset the cache address to ensure we are already accessing the
	 * beginning of a sub-page.
	 */
	bcma_cc_write32(sc->cc, BCMA_CC_NAND_CACHE_ADDR, 0);
}

static int brcmnand_bcma_nand_probe(struct platform_device *pdev)
{
	struct bcma_nflash *nflash = dev_get_platdata(&pdev->dev);
	struct brcmnand_bcma_soc *soc;

	soc = devm_kzalloc(&pdev->dev, sizeof(*soc), GFP_KERNEL);
	if (!soc)
		return -ENOMEM;

	soc->cc = container_of(nflash, struct bcma_drv_cc, nflash);
	soc->soc.prepare_data_bus = brcmnand_bcma_prepare_data_bus;
	soc->soc.ops = &brcmnand_bcma_io_ops;

	if (soc->cc->core->bus->chipinfo.id == BCMA_CHIP_ID_BCM4706) {
		dev_err(&pdev->dev, "Use bcm47xxnflash for 4706!\n");
		return -ENODEV;
	}

	return brcmnand_probe(pdev, &soc->soc);
}

static struct platform_driver brcmnand_bcma_nand_driver = {
	.probe			= brcmnand_bcma_nand_probe,
	.remove			= brcmnand_remove,
	.driver = {
		.name		= "bcma_brcmnand",
		.pm		= &brcmnand_pm_ops,
	}
};
module_platform_driver(brcmnand_bcma_nand_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("NAND controller driver glue for BCMA chips");
