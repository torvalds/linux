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

#include <asm/io.h>
#include <asm/irq.h>

#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>

#define GPIO_NAND_CS	(11)
#define GPIO_NAND_RB	(89)

/* This macro needed to ensure in-order operation of GPIO and local
 * bus. Without both asm command and dummy uncached read there're
 * states when NAND access is broken. I've looked for such macro(s) in
 * include/asm-arm but found nothing approptiate.
 * dmac_clean_range is close, but is makes cache invalidation
 * unnecessary here and it cannot be used in module
 */
#define DRAIN_WB() \
	do { \
		unsigned char dummy; \
		asm volatile ("mcr p15, 0, r0, c7, c10, 4":::"r0"); \
		dummy=*((unsigned char*)UNCACHED_ADDR); \
	} while(0)

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

const char *part_probes[] = { "cmdlinepart", NULL };

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
	GPCR(GPIO_NAND_CS) = GPIO_bit(GPIO_NAND_CS);
}

static void nand_cs_off(void)
{
	DRAIN_WB();

	GPSR(GPIO_NAND_CS) = GPIO_bit(GPIO_NAND_CS);
}

/*
 *	hardware specific access to control-lines
 */
static void cmx270_hwcontrol(struct mtd_info *mtd, int dat,
			     unsigned int ctrl)
{
	struct nand_chip* this = mtd->priv;
	unsigned int nandaddr = (unsigned int)this->IO_ADDR_W;

	DRAIN_WB();

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

	DRAIN_WB();
	this->IO_ADDR_W = (void __iomem*)nandaddr;
	if (dat != NAND_CMD_NONE)
		writel((dat << 16), this->IO_ADDR_W);

	DRAIN_WB();
}

/*
 *	read device ready pin
 */
static int cmx270_device_ready(struct mtd_info *mtd)
{
	DRAIN_WB();

	return (GPLR(GPIO_NAND_RB) & GPIO_bit(GPIO_NAND_RB));
}

/*
 * Main initialization routine
 */
static int cmx270_init(void)
{
	struct nand_chip *this;
	const char *part_type;
	struct mtd_partition *mtd_parts;
	int mtd_parts_nb = 0;
	int ret;

	/* Allocate memory for MTD device structure and private data */
	cmx270_nand_mtd = kzalloc(sizeof(struct mtd_info) +
				  sizeof(struct nand_chip),
				  GFP_KERNEL);
	if (!cmx270_nand_mtd) {
		printk("Unable to allocate CM-X270 NAND MTD device structure.\n");
		return -ENOMEM;
	}

	cmx270_nand_io = ioremap(PXA_CS1_PHYS, 12);
	if (!cmx270_nand_io) {
		printk("Unable to ioremap NAND device\n");
		ret = -EINVAL;
		goto err1;
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
		printk(KERN_NOTICE "No NAND device\n");
		ret = -ENXIO;
		goto err2;
	}

#ifdef CONFIG_MTD_CMDLINE_PARTS
	mtd_parts_nb = parse_mtd_partitions(cmx270_nand_mtd, part_probes,
					    &mtd_parts, 0);
	if (mtd_parts_nb > 0)
		part_type = "command line";
	else
		mtd_parts_nb = 0;
#endif
	if (!mtd_parts_nb) {
		mtd_parts = partition_info;
		mtd_parts_nb = NUM_PARTITIONS;
		part_type = "static";
	}

	/* Register the partitions */
	printk(KERN_NOTICE "Using %s partition definition\n", part_type);
	ret = add_mtd_partitions(cmx270_nand_mtd, mtd_parts, mtd_parts_nb);
	if (ret)
		goto err2;

	/* Return happy */
	return 0;

err2:
	iounmap(cmx270_nand_io);
err1:
	kfree(cmx270_nand_mtd);

	return ret;

}
module_init(cmx270_init);

/*
 * Clean up routine
 */
static void cmx270_cleanup(void)
{
	/* Release resources, unregister device */
	nand_release(cmx270_nand_mtd);

	iounmap(cmx270_nand_io);

	/* Free the MTD device structure */
	kfree (cmx270_nand_mtd);
}
module_exit(cmx270_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("NAND flash driver for Compulab CM-X270 Module");
