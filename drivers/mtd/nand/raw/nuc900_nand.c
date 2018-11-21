/*
 * Copyright © 2009 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>

#define REG_FMICSR   	0x00
#define REG_SMCSR    	0xa0
#define REG_SMISR    	0xac
#define REG_SMCMD    	0xb0
#define REG_SMADDR   	0xb4
#define REG_SMDATA   	0xb8

#define RESET_FMI	0x01
#define NAND_EN		0x08
#define READYBUSY	(0x01 << 18)

#define SWRST		0x01
#define PSIZE		(0x01 << 3)
#define DMARWEN		(0x03 << 1)
#define BUSWID		(0x01 << 4)
#define ECC4EN		(0x01 << 5)
#define WP		(0x01 << 24)
#define NANDCS		(0x01 << 25)
#define ENDADDR		(0x01 << 31)

#define read_data_reg(dev)		\
	__raw_readl((dev)->reg + REG_SMDATA)

#define write_data_reg(dev, val)	\
	__raw_writel((val), (dev)->reg + REG_SMDATA)

#define write_cmd_reg(dev, val)		\
	__raw_writel((val), (dev)->reg + REG_SMCMD)

#define write_addr_reg(dev, val)	\
	__raw_writel((val), (dev)->reg + REG_SMADDR)

struct nuc900_nand {
	struct nand_chip chip;
	void __iomem *reg;
	struct clk *clk;
	spinlock_t lock;
};

static inline struct nuc900_nand *mtd_to_nuc900(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct nuc900_nand, chip);
}

static const struct mtd_partition partitions[] = {
	{
	 .name = "NAND FS 0",
	 .offset = 0,
	 .size = 8 * 1024 * 1024
	},
	{
	 .name = "NAND FS 1",
	 .offset = MTDPART_OFS_APPEND,
	 .size = MTDPART_SIZ_FULL
	}
};

static unsigned char nuc900_nand_read_byte(struct mtd_info *mtd)
{
	unsigned char ret;
	struct nuc900_nand *nand = mtd_to_nuc900(mtd);

	ret = (unsigned char)read_data_reg(nand);

	return ret;
}

static void nuc900_nand_read_buf(struct mtd_info *mtd,
				 unsigned char *buf, int len)
{
	int i;
	struct nuc900_nand *nand = mtd_to_nuc900(mtd);

	for (i = 0; i < len; i++)
		buf[i] = (unsigned char)read_data_reg(nand);
}

static void nuc900_nand_write_buf(struct mtd_info *mtd,
				  const unsigned char *buf, int len)
{
	int i;
	struct nuc900_nand *nand = mtd_to_nuc900(mtd);

	for (i = 0; i < len; i++)
		write_data_reg(nand, buf[i]);
}

static int nuc900_check_rb(struct nuc900_nand *nand)
{
	unsigned int val;
	spin_lock(&nand->lock);
	val = __raw_readl(nand->reg + REG_SMISR);
	val &= READYBUSY;
	spin_unlock(&nand->lock);

	return val;
}

static int nuc900_nand_devready(struct mtd_info *mtd)
{
	struct nuc900_nand *nand = mtd_to_nuc900(mtd);
	int ready;

	ready = (nuc900_check_rb(nand)) ? 1 : 0;
	return ready;
}

static void nuc900_nand_command_lp(struct mtd_info *mtd, unsigned int command,
				   int column, int page_addr)
{
	register struct nand_chip *chip = mtd_to_nand(mtd);
	struct nuc900_nand *nand = mtd_to_nuc900(mtd);

	if (command == NAND_CMD_READOOB) {
		column += mtd->writesize;
		command = NAND_CMD_READ0;
	}

	write_cmd_reg(nand, command & 0xff);

	if (column != -1 || page_addr != -1) {

		if (column != -1) {
			if (chip->options & NAND_BUSWIDTH_16 &&
					!nand_opcode_8bits(command))
				column >>= 1;
			write_addr_reg(nand, column);
			write_addr_reg(nand, column >> 8 | ENDADDR);
		}
		if (page_addr != -1) {
			write_addr_reg(nand, page_addr);

			if (chip->options & NAND_ROW_ADDR_3) {
				write_addr_reg(nand, page_addr >> 8);
				write_addr_reg(nand, page_addr >> 16 | ENDADDR);
			} else {
				write_addr_reg(nand, page_addr >> 8 | ENDADDR);
			}
		}
	}

	switch (command) {
	case NAND_CMD_CACHEDPROG:
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_SEQIN:
	case NAND_CMD_RNDIN:
	case NAND_CMD_STATUS:
		return;

	case NAND_CMD_RESET:
		if (chip->dev_ready)
			break;
		udelay(chip->chip_delay);

		write_cmd_reg(nand, NAND_CMD_STATUS);
		write_cmd_reg(nand, command);

		while (!nuc900_check_rb(nand))
			;

		return;

	case NAND_CMD_RNDOUT:
		write_cmd_reg(nand, NAND_CMD_RNDOUTSTART);
		return;

	case NAND_CMD_READ0:

		write_cmd_reg(nand, NAND_CMD_READSTART);
	default:

		if (!chip->dev_ready) {
			udelay(chip->chip_delay);
			return;
		}
	}

	/* Apply this short delay always to ensure that we do wait tWB in
	 * any case on any machine. */
	ndelay(100);

	while (!chip->dev_ready(mtd))
		;
}


static void nuc900_nand_enable(struct nuc900_nand *nand)
{
	unsigned int val;
	spin_lock(&nand->lock);
	__raw_writel(RESET_FMI, (nand->reg + REG_FMICSR));

	val = __raw_readl(nand->reg + REG_FMICSR);

	if (!(val & NAND_EN))
		__raw_writel(val | NAND_EN, nand->reg + REG_FMICSR);

	val = __raw_readl(nand->reg + REG_SMCSR);

	val &= ~(SWRST|PSIZE|DMARWEN|BUSWID|ECC4EN|NANDCS);
	val |= WP;

	__raw_writel(val, nand->reg + REG_SMCSR);

	spin_unlock(&nand->lock);
}

static int nuc900_nand_probe(struct platform_device *pdev)
{
	struct nuc900_nand *nuc900_nand;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	struct resource *res;

	nuc900_nand = devm_kzalloc(&pdev->dev, sizeof(struct nuc900_nand),
				   GFP_KERNEL);
	if (!nuc900_nand)
		return -ENOMEM;
	chip = &(nuc900_nand->chip);
	mtd = nand_to_mtd(chip);

	mtd->dev.parent		= &pdev->dev;
	spin_lock_init(&nuc900_nand->lock);

	nuc900_nand->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(nuc900_nand->clk))
		return -ENOENT;
	clk_enable(nuc900_nand->clk);

	chip->cmdfunc		= nuc900_nand_command_lp;
	chip->dev_ready		= nuc900_nand_devready;
	chip->read_byte		= nuc900_nand_read_byte;
	chip->write_buf		= nuc900_nand_write_buf;
	chip->read_buf		= nuc900_nand_read_buf;
	chip->chip_delay	= 50;
	chip->options		= 0;
	chip->ecc.mode		= NAND_ECC_SOFT;
	chip->ecc.algo		= NAND_ECC_HAMMING;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nuc900_nand->reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nuc900_nand->reg))
		return PTR_ERR(nuc900_nand->reg);

	nuc900_nand_enable(nuc900_nand);

	if (nand_scan(mtd, 1))
		return -ENXIO;

	mtd_device_register(mtd, partitions, ARRAY_SIZE(partitions));

	platform_set_drvdata(pdev, nuc900_nand);

	return 0;
}

static int nuc900_nand_remove(struct platform_device *pdev)
{
	struct nuc900_nand *nuc900_nand = platform_get_drvdata(pdev);

	nand_release(nand_to_mtd(&nuc900_nand->chip));
	clk_disable(nuc900_nand->clk);

	return 0;
}

static struct platform_driver nuc900_nand_driver = {
	.probe		= nuc900_nand_probe,
	.remove		= nuc900_nand_remove,
	.driver		= {
		.name	= "nuc900-fmi",
	},
};

module_platform_driver(nuc900_nand_driver);

MODULE_AUTHOR("Wan ZongShun <mcuos.com@gmail.com>");
MODULE_DESCRIPTION("w90p910/NUC9xx nand driver!");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nuc900-fmi");
