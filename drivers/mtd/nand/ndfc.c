/*
 *  drivers/mtd/ndfc.c
 *
 *  Overview:
 *   Platform independend driver for NDFC (NanD Flash Controller)
 *   integrated into EP440 cores
 *
 *  Author: Thomas Gleixner
 *
 *  Copyright 2006 IBM
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 */
#include <linux/module.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/ndfc.h>
#include <linux/mtd/mtd.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/ibm44x.h>

struct ndfc_nand_mtd {
	struct mtd_info			mtd;
	struct nand_chip		chip;
	struct platform_nand_chip	*pl_chip;
};

static struct ndfc_nand_mtd ndfc_mtd[NDFC_MAX_BANKS];

struct ndfc_controller {
	void __iomem		*ndfcbase;
	struct nand_hw_control	ndfc_control;
	atomic_t		childs_active;
};

static struct ndfc_controller ndfc_ctrl;

static void ndfc_select_chip(struct mtd_info *mtd, int chip)
{
	uint32_t ccr;
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	struct nand_chip *nandchip = mtd->priv;
	struct ndfc_nand_mtd *nandmtd = nandchip->priv;
	struct platform_nand_chip *pchip = nandmtd->pl_chip;

	ccr = __raw_readl(ndfc->ndfcbase + NDFC_CCR);
	if (chip >= 0) {
		ccr &= ~NDFC_CCR_BS_MASK;
		ccr |= NDFC_CCR_BS(chip + pchip->chip_offset);
	} else
		ccr |= NDFC_CCR_RESET_CE;
	writel(ccr, ndfc->ndfcbase + NDFC_CCR);
}

static void ndfc_hwcontrol(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writel(cmd & 0xFF, ndfc->ndfcbase + NDFC_CMD);
	else
		writel(cmd & 0xFF, ndfc->ndfcbase + NDFC_ALE);
}

static int ndfc_ready(struct mtd_info *mtd)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;

	return __raw_readl(ndfc->ndfcbase + NDFC_STAT) & NDFC_STAT_IS_READY;
}

static void ndfc_enable_hwecc(struct mtd_info *mtd, int mode)
{
	uint32_t ccr;
	struct ndfc_controller *ndfc = &ndfc_ctrl;

	ccr = __raw_readl(ndfc->ndfcbase + NDFC_CCR);
	ccr |= NDFC_CCR_RESET_ECC;
	__raw_writel(ccr, ndfc->ndfcbase + NDFC_CCR);
	wmb();
}

static int ndfc_calculate_ecc(struct mtd_info *mtd,
			      const u_char *dat, u_char *ecc_code)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	uint32_t ecc;
	uint8_t *p = (uint8_t *)&ecc;

	wmb();
	ecc = __raw_readl(ndfc->ndfcbase + NDFC_ECC);
	ecc_code[0] = p[1];
	ecc_code[1] = p[2];
	ecc_code[2] = p[3];

	return 0;
}

/*
 * Speedups for buffer read/write/verify
 *
 * NDFC allows 32bit read/write of data. So we can speed up the buffer
 * functions. No further checking, as nand_base will always read/write
 * page aligned.
 */
static void ndfc_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	uint32_t *p = (uint32_t *) buf;

	for(;len > 0; len -= 4)
		*p++ = __raw_readl(ndfc->ndfcbase + NDFC_DATA);
}

static void ndfc_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	uint32_t *p = (uint32_t *) buf;

	for(;len > 0; len -= 4)
		__raw_writel(*p++, ndfc->ndfcbase + NDFC_DATA);
}

static int ndfc_verify_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	uint32_t *p = (uint32_t *) buf;

	for(;len > 0; len -= 4)
		if (*p++ != __raw_readl(ndfc->ndfcbase + NDFC_DATA))
			return -EFAULT;
	return 0;
}

/*
 * Initialize chip structure
 */
static void ndfc_chip_init(struct ndfc_nand_mtd *mtd)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	struct nand_chip *chip = &mtd->chip;

	chip->IO_ADDR_R = ndfc->ndfcbase + NDFC_DATA;
	chip->IO_ADDR_W = ndfc->ndfcbase + NDFC_DATA;
	chip->cmd_ctrl = ndfc_hwcontrol;
	chip->dev_ready = ndfc_ready;
	chip->select_chip = ndfc_select_chip;
	chip->chip_delay = 50;
	chip->priv = mtd;
	chip->options = mtd->pl_chip->options;
	chip->controller = &ndfc->ndfc_control;
	chip->read_buf = ndfc_read_buf;
	chip->write_buf = ndfc_write_buf;
	chip->verify_buf = ndfc_verify_buf;
	chip->ecc.correct = nand_correct_data;
	chip->ecc.hwctl = ndfc_enable_hwecc;
	chip->ecc.calculate = ndfc_calculate_ecc;
	chip->ecc.mode = NAND_ECC_HW;
	chip->ecc.size = 256;
	chip->ecc.bytes = 3;
	chip->ecclayout = mtd->pl_chip->ecclayout;
	mtd->mtd.priv = chip;
	mtd->mtd.owner = THIS_MODULE;
}

static int ndfc_chip_probe(struct platform_device *pdev)
{
	struct platform_nand_chip *nc = pdev->dev.platform_data;
	struct ndfc_chip_settings *settings = nc->priv;
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	struct ndfc_nand_mtd *nandmtd;

	if (nc->chip_offset >= NDFC_MAX_BANKS || nc->nr_chips > NDFC_MAX_BANKS)
		return -EINVAL;

	/* Set the bank settings */
	__raw_writel(settings->bank_settings,
		     ndfc->ndfcbase + NDFC_BCFG0 + (nc->chip_offset << 2));

	nandmtd = &ndfc_mtd[pdev->id];
	if (nandmtd->pl_chip)
		return -EBUSY;

	nandmtd->pl_chip = nc;
	ndfc_chip_init(nandmtd);

	/* Scan for chips */
	if (nand_scan(&nandmtd->mtd, nc->nr_chips)) {
		nandmtd->pl_chip = NULL;
		return -ENODEV;
	}

#ifdef CONFIG_MTD_PARTITIONS
	printk("Number of partitions %d\n", nc->nr_partitions);
	if (nc->nr_partitions) {
		/* Add the full device, so complete dumps can be made */
		add_mtd_device(&nandmtd->mtd);
		add_mtd_partitions(&nandmtd->mtd, nc->partitions,
				   nc->nr_partitions);

	} else
#else
		add_mtd_device(&nandmtd->mtd);
#endif

	atomic_inc(&ndfc->childs_active);
	return 0;
}

static int ndfc_chip_remove(struct platform_device *pdev)
{
	return 0;
}

static int ndfc_nand_probe(struct platform_device *pdev)
{
	struct platform_nand_ctrl *nc = pdev->dev.platform_data;
	struct ndfc_controller_settings *settings = nc->priv;
	struct resource *res = pdev->resource;
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	unsigned long long phys = settings->ndfc_erpn | res->start;

	ndfc->ndfcbase = ioremap64(phys, res->end - res->start + 1);
	if (!ndfc->ndfcbase) {
		printk(KERN_ERR "NDFC: ioremap failed\n");
		return -EIO;
	}

	__raw_writel(settings->ccr_settings, ndfc->ndfcbase + NDFC_CCR);

	spin_lock_init(&ndfc->ndfc_control.lock);
	init_waitqueue_head(&ndfc->ndfc_control.wq);

	platform_set_drvdata(pdev, ndfc);

	printk("NDFC NAND Driver initialized. Chip-Rev: 0x%08x\n",
	       __raw_readl(ndfc->ndfcbase + NDFC_REVID));

	return 0;
}

static int ndfc_nand_remove(struct platform_device *pdev)
{
	struct ndfc_controller *ndfc = platform_get_drvdata(pdev);

	if (atomic_read(&ndfc->childs_active))
		return -EBUSY;

	if (ndfc) {
		platform_set_drvdata(pdev, NULL);
		iounmap(ndfc_ctrl.ndfcbase);
		ndfc_ctrl.ndfcbase = NULL;
	}
	return 0;
}

/* driver device registration */

static struct platform_driver ndfc_chip_driver = {
	.probe		= ndfc_chip_probe,
	.remove		= ndfc_chip_remove,
	.driver		= {
		.name	= "ndfc-chip",
		.owner	= THIS_MODULE,
	},
};

static struct platform_driver ndfc_nand_driver = {
	.probe		= ndfc_nand_probe,
	.remove		= ndfc_nand_remove,
	.driver		= {
		.name	= "ndfc-nand",
		.owner	= THIS_MODULE,
	},
};

static int __init ndfc_nand_init(void)
{
	int ret;

	spin_lock_init(&ndfc_ctrl.ndfc_control.lock);
	init_waitqueue_head(&ndfc_ctrl.ndfc_control.wq);

	ret = platform_driver_register(&ndfc_nand_driver);
	if (!ret)
		ret = platform_driver_register(&ndfc_chip_driver);
	return ret;
}

static void __exit ndfc_nand_exit(void)
{
	platform_driver_unregister(&ndfc_chip_driver);
	platform_driver_unregister(&ndfc_nand_driver);
}

module_init(ndfc_nand_init);
module_exit(ndfc_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Gleixner <tglx@linutronix.de>");
MODULE_DESCRIPTION("Platform driver for NDFC");
