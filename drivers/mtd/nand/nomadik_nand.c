/*
 *  drivers/mtd/nand/nomadik_nand.c
 *
 *  Overview:
 *  	Driver for on-board NAND flash on Nomadik Platforms
 *
 * Copyright © 2007 STMicroelectronics Pvt. Ltd.
 * Author: Sachin Verma <sachin.verma@st.com>
 *
 * Copyright © 2009 Alessandro Rubini
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <mach/nand.h>
#include <mach/fsmc.h>

#include <mtd/mtd-abi.h>

struct nomadik_nand_host {
	struct mtd_info		mtd;
	struct nand_chip	nand;
	void __iomem *data_va;
	void __iomem *cmd_va;
	void __iomem *addr_va;
	struct nand_bbt_descr *bbt_desc;
};

static struct nand_ecclayout nomadik_ecc_layout = {
	.eccbytes = 3 * 4,
	.eccpos = { /* each subpage has 16 bytes: pos 2,3,4 hosts ECC */
		0x02, 0x03, 0x04,
		0x12, 0x13, 0x14,
		0x22, 0x23, 0x24,
		0x32, 0x33, 0x34},
	/* let's keep bytes 5,6,7 for us, just in case we change ECC algo */
	.oobfree = { {0x08, 0x08}, {0x18, 0x08}, {0x28, 0x08}, {0x38, 0x08} },
};

static void nomadik_ecc_control(struct mtd_info *mtd, int mode)
{
	/* No need to enable hw ecc, it's on by default */
}

static void nomadik_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *nand = mtd->priv;
	struct nomadik_nand_host *host = nand->priv;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, host->cmd_va);
	else
		writeb(cmd, host->addr_va);
}

static int nomadik_nand_probe(struct platform_device *pdev)
{
	struct nomadik_nand_platform_data *pdata = pdev->dev.platform_data;
	struct nomadik_nand_host *host;
	struct mtd_info *mtd;
	struct nand_chip *nand;
	struct resource *res;
	int ret = 0;

	/* Allocate memory for the device structure (and zero it) */
	host = kzalloc(sizeof(struct nomadik_nand_host), GFP_KERNEL);
	if (!host) {
		dev_err(&pdev->dev, "Failed to allocate device structure.\n");
		return -ENOMEM;
	}

	/* Call the client's init function, if any */
	if (pdata->init)
		ret = pdata->init();
	if (ret < 0) {
		dev_err(&pdev->dev, "Init function failed\n");
		goto err;
	}

	/* ioremap three regions */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "nand_addr");
	if (!res) {
		ret = -EIO;
		goto err_unmap;
	}
	host->addr_va = ioremap(res->start, resource_size(res));

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "nand_data");
	if (!res) {
		ret = -EIO;
		goto err_unmap;
	}
	host->data_va = ioremap(res->start, resource_size(res));

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "nand_cmd");
	if (!res) {
		ret = -EIO;
		goto err_unmap;
	}
	host->cmd_va = ioremap(res->start, resource_size(res));

	if (!host->addr_va || !host->data_va || !host->cmd_va) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	/* Link all private pointers */
	mtd = &host->mtd;
	nand = &host->nand;
	mtd->priv = nand;
	nand->priv = host;

	host->mtd.owner = THIS_MODULE;
	nand->IO_ADDR_R = host->data_va;
	nand->IO_ADDR_W = host->data_va;
	nand->cmd_ctrl = nomadik_cmd_ctrl;

	/*
	 * This stanza declares ECC_HW but uses soft routines. It's because
	 * HW claims to make the calculation but not the correction. However,
	 * I haven't managed to get the desired data out of it until now.
	 */
	nand->ecc.mode = NAND_ECC_SOFT;
	nand->ecc.layout = &nomadik_ecc_layout;
	nand->ecc.hwctl = nomadik_ecc_control;
	nand->ecc.size = 512;
	nand->ecc.bytes = 3;

	nand->options = pdata->options;

	/*
	 * Scan to find existence of the device
	 */
	if (nand_scan(&host->mtd, 1)) {
		ret = -ENXIO;
		goto err_unmap;
	}

#ifdef CONFIG_MTD_PARTITIONS
	add_mtd_partitions(&host->mtd, pdata->parts, pdata->nparts);
#else
	pr_info("Registering %s as whole device\n", mtd->name);
	add_mtd_device(mtd);
#endif

	platform_set_drvdata(pdev, host);
	return 0;

 err_unmap:
	if (host->cmd_va)
		iounmap(host->cmd_va);
	if (host->data_va)
		iounmap(host->data_va);
	if (host->addr_va)
		iounmap(host->addr_va);
 err:
	kfree(host);
	return ret;
}

/*
 * Clean up routine
 */
static int nomadik_nand_remove(struct platform_device *pdev)
{
	struct nomadik_nand_host *host = platform_get_drvdata(pdev);
	struct nomadik_nand_platform_data *pdata = pdev->dev.platform_data;

	if (pdata->exit)
		pdata->exit();

	if (host) {
		iounmap(host->cmd_va);
		iounmap(host->data_va);
		iounmap(host->addr_va);
		kfree(host);
	}
	return 0;
}

static int nomadik_nand_suspend(struct device *dev)
{
	struct nomadik_nand_host *host = dev_get_drvdata(dev);
	int ret = 0;
	if (host)
		ret = host->mtd.suspend(&host->mtd);
	return ret;
}

static int nomadik_nand_resume(struct device *dev)
{
	struct nomadik_nand_host *host = dev_get_drvdata(dev);
	if (host)
		host->mtd.resume(&host->mtd);
	return 0;
}

static const struct dev_pm_ops nomadik_nand_pm_ops = {
	.suspend = nomadik_nand_suspend,
	.resume = nomadik_nand_resume,
};

static struct platform_driver nomadik_nand_driver = {
	.probe = nomadik_nand_probe,
	.remove = nomadik_nand_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "nomadik_nand",
		.pm = &nomadik_nand_pm_ops,
	},
};

static int __init nand_nomadik_init(void)
{
	pr_info("Nomadik NAND driver\n");
	return platform_driver_register(&nomadik_nand_driver);
}

static void __exit nand_nomadik_exit(void)
{
	platform_driver_unregister(&nomadik_nand_driver);
}

module_init(nand_nomadik_init);
module_exit(nand_nomadik_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ST Microelectronics (sachin.verma@st.com)");
MODULE_DESCRIPTION("NAND driver for Nomadik Platform");
