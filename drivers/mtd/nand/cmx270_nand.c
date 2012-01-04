/*
 *  linux/drivers/mtd/nand/cmx270-nand.c
 *
 *  Copyright (C) 2006 Compulab, Ltd.
 *  Mike Rapoport <mike@compulab.co.il>
 *
 *  Derived from drivers/mtd/nand/h1910.c
 *       Copyright (C) 2002 Marius Gröger (mag@sysgo.de)
 *       Copyright (c) 2001 Thomas Gleixner (gleixner@autronix.de)
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the NAND flash device found on the
 *   CM-X270 board.
 */

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <mach/pxa2xx-regs.h>

#define GPIO_NAND_CS	(11)
#define GPIO_NAND_RB	(89)

/* MTD structure for CM-X270 board */
static struct mtd_info *cmx270_nand_mtd;

/* remaped IO address of the device */
static void __iomem *cmx270_nand_io;

/*
 * Define static partitions for flash device
 */
static struct mtd_partition partition_info[] = {
	[0] = {
		.name	= "cmx270-0",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL
	}
};
#define NUM_PARTITIONS (ARRAY_SIZE(partition_info))

static u_char cmx270_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *this = mtd->priv;

	return (readl(this->IO_ADDR_R) >> 16);
}

static void cmx270_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i=0; i<len; i++)
		writel((*buf++ << 16), this->IO_ADDR_W);
}

static void cmx270_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i=0; i<len; i++)
		*buf++ = readl(this->IO_ADDR_R) >> 16;
}

static int cmx270_verify_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *this = mtd->priv;

	for (i=0; i<len; i++)
		if (buf[i] != (u_char)(readl(this->IO_ADDR_R) >> 16))
			return -EFAULT;

	return 0;
}

static inline void nand_cs_on(void)
{
	gpio_set_value(GPIO_NAND_CS, 0);
}

static void nand_cs_off(void)
{
	dsb();

	gpio_set_value(GPIO_NAND_CS, 1);
}

/*
 *	hardware specific access to control-lines
 */
static void cmx270_hwcontrol(struct mtd_info *mtd, int dat,
			     unsigned int ctrl)
{
	struct nand_chip* this = mtd->priv;
	unsigned int nandaddr = (unsigned int)this->IO_ADDR_W;

	dsb();

	if (ctrl & NAND_CTRL_CHANGE) {
		if ( ctrl & NAND_ALE )
			nandaddr |=  (1 << 3);
		else
			nandaddr &= ~(1 << 3);
		if ( ctrl & NAND_CLE )
			nandaddr |=  (1 << 2);
		else
			nandaddr &= ~(1 << 2);
		if ( ctrl & NAND_NCE )
			nand_cs_on();
		else
			nand_cs_off();
	}

	dsb();
	this->IO_ADDR_W = (void __iomem*)nandaddr;
	if (dat != NAND_CMD_NONE)
		writel((dat << 16), this->IO_ADDR_W);

	dsb();
}

/*
 *	read device ready pin
 */
static int cmx270_device_ready(struct mtd_info *mtd)
{
	dsb();

	return (gpio_get_value(GPIO_NAND_RB));
}

/*
 * Main initialization routine
 */
static int __init cmx270_init(void)
{
	struct nand_chip *this;
	int ret;

	if (!(machine_is_armcore() && cpu_is_pxa27x()))
		return -ENODEV;

	ret = gpio_request(GPIO_NAND_CS, "NAND CS");
	if (ret) {
		pr_warning("CM-X270: failed to request NAND CS gpio\n");
		return ret;
	}

	gpio_direction_output(GPIO_NAND_CS, 1);

	ret = gpio_request(GPIO_NAND_RB, "NAND R/B");
	if (ret) {
		pr_warning("CM-X270: failed to request NAND R/B gpio\n");
		goto err_gpio_request;
	}

	gpio_direction_input(GPIO_NAND_RB);

	/* Allocate memory for MTD device structure and private data */
	cmx270_nand_mtd = kzalloc(sizeof(struct mtd_info) +
				  sizeof(struct nand_chip),
				  GFP_KERNEL);
	if (!cmx270_nand_mtd) {
		pr_debug("Unable to allocate CM-X270 NAND MTD device structure.\n");
		ret = -ENOMEM;
		goto err_kzalloc;
	}

	cmx270_nand_io = ioremap(PXA_CS1_PHYS, 12);
	if (!cmx270_nand_io) {
		pr_debug("Unable to ioremap NAND device\n");
		ret = -EINVAL;
		goto err_ioremap;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *)(&cmx270_nand_mtd[1]);

	/* Link the private data with the MTD structure */
	cmx270_nand_mtd->owner = THIS_MODULE;
	cmx270_nand_mtd->priv = this;

	/* insert callbacks */
	this->IO_ADDR_R = cmx270_nand_io;
	this->IO_ADDR_W = cmx270_nand_io;
	this->cmd_ctrl = cmx270_hwcontrol;
	this->dev_ready = cmx270_device_ready;

	/* 15 us command delay time */
	this->chip_delay = 20;
	this->ecc.mode = NAND_ECC_SOFT;

	/* read/write functions */
	this->read_byte = cmx270_read_byte;
	this->read_buf = cmx270_read_buf;
	this->write_buf = cmx270_write_buf;
	this->verify_buf = cmx270_verify_buf;

	/* Scan to find existence of the device */
	if (nand_scan (cmx270_nand_mtd, 1)) {
		pr_notice("No NAND device\n");
		ret = -ENXIO;
		goto err_scan;
	}

	/* Register the partitions */
	ret = mtd_device_parse_register(cmx270_nand_mtd, NULL, 0,
					partition_info, NUM_PARTITIONS);
	if (ret)
		goto err_scan;

	/* Return happy */
	return 0;

err_scan:
	iounmap(cmx270_nand_io);
err_ioremap:
	kfree(cmx270_nand_mtd);
err_kzalloc:
	gpio_free(GPIO_NAND_RB);
err_gpio_request:
	gpio_free(GPIO_NAND_CS);

	return ret;

}
module_init(cmx270_init);

/*
 * Clean up routine
 */
static void __exit cmx270_cleanup(void)
{
	/* Release resources, unregister device */
	nand_release(cmx270_nand_mtd);

	gpio_free(GPIO_NAND_RB);
	gpio_free(GPIO_NAND_CS);

	iounmap(cmx270_nand_io);

	/* Free the MTD device structure */
	kfree (cmx270_nand_mtd);
}
module_exit(cmx270_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("NAND flash driver for Compulab CM-X270 Module");
