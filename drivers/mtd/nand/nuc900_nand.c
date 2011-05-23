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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
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
	struct mtd_info mtd;
	struct nand_chip chip;
	void __iomem *reg;
	struct clk *clk;
	spinlock_t lock;
};

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
	struct nuc900_nand *nand;

	nand = container_of(mtd, struct nuc900_nand, mtd);

	ret = (unsigned char)read_data_reg(nand);

	return ret;
}

static void nuc900_nand_read_buf(struct mtd_info *mtd,
				 unsigned char *buf, int len)
{
	int i;
	struct nuc900_nand *nand;

	nand = container_of(mtd, struct nuc900_nand, mtd);

	for (i = 0; i < len; i++)
		buf[i] = (unsigned char)read_data_reg(nand);
}

static void nuc900_nand_write_buf(struct mtd_info *mtd,
				  const unsigned char *buf, int len)
{
	int i;
	struct nuc900_nand *nand;

	nand = container_of(mtd, struct nuc900_nand, mtd);

	for (i = 0; i < len; i++)
		write_data_reg(nand, buf[i]);
}

static int nuc900_verify_buf(struct mtd_info *mtd,
			     const unsigned char *buf, int len)
{
	int i;
	struct nuc900_nand *nand;

	nand = container_of(mtd, struct nuc900_nand, mtd);

	for (i = 0; i < len; i++) {
		if (buf[i] != (unsigned char)read_data_reg(nand))
			return -EFAULT;
	}

	return 0;
}

static int nuc900_check_rb(struct nuc900_nand *nand)
{
	unsigned int val;
	spin_lock(&nand->lock);
	val = __raw_readl(REG_SMISR);
	val &= READYBUSY;
	spin_unlock(&nand->lock);

	return val;
}

static int nuc900_nand_devready(struct mtd_info *mtd)
{
	struct nuc900_nand *nand;
	int ready;

	nand = container_of(mtd, struct nuc900_nand, mtd);

	ready = (nuc900_check_rb(nand)) ? 1 : 0;
	return ready;
}

static void nuc900_nand_command_lp(struct mtd_info *mtd, unsigned int command,
				   int column, int page_addr)
{
	register struct nand_chip *chip = mtd->priv;
	struct nuc900_nand *nand;

	nand = container_of(mtd, struct nuc900_nand, mtd);

	if (command == NAND_CMD_READOOB) {
		column += mtd->writesize;
		command = NAND_CMD_READ0;
	}

	write_cmd_reg(nand, command & 0xff);

	if (column != -1 || page_addr != -1) {

		if (column != -1) {
			if (chip->options & NAND_BUSWIDTH_16)
				column >>= 1;
			write_addr_reg(nand, column);
			write_addr_reg(nand, column >> 8 | ENDADDR);
		}
		if (page_addr != -1) {
			write_addr_reg(nand, page_addr);

			if (chip->chipsize > (128 << 20)) {
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
	case NAND_CMD_DEPLETE1:
		return;

	case NAND_CMD_STATUS_ERROR:
	case NAND_CMD_STATUS_ERROR0:
	case NAND_CMD_STATUS_ERROR1:
	case NAND_CMD_STATUS_ERROR2:
	case NAND_CMD_STATUS_ERROR3:
		udelay(chip->chip_delay);
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
		__raw_writel(val | NAND_EN, REG_FMICSR);

	val = __raw_readl(nand->reg + REG_SMCSR);

	val &= ~(SWRST|PSIZE|DMARWEN|BUSWID|ECC4EN|NANDCS);
	val |= WP;

	__raw_writel(val, nand->reg + REG_SMCSR);

	spin_unlock(&nand->lock);
}

static int __devinit nuc900_nand_probe(struct platform_device *pdev)
{
	struct nuc900_nand *nuc900_nand;
	struct nand_chip *chip;
	int retval;
	struct resource *res;

	retval = 0;

	nuc900_nand = kzalloc(sizeof(struct nuc900_nand), GFP_KERNEL);
	if (!nuc900_nand)
		return -ENOMEM;
	chip = &(nuc900_nand->chip);

	nuc900_nand->mtd.priv	= chip;
	nuc900_nand->mtd.owner	= THIS_MODULE;
	spin_lock_init(&nuc900_nand->lock);

	nuc900_nand->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(nuc900_nand->clk)) {
		retval = -ENOENT;
		goto fail1;
	}
	clk_enable(nuc900_nand->clk);

	chip->cmdfunc		= nuc900_nand_command_lp;
	chip->dev_ready		= nuc900_nand_devready;
	chip->read_byte		= nuc900_nand_read_byte;
	chip->write_buf		= nuc900_nand_write_buf;
	chip->read_buf		= nuc900_nand_read_buf;
	chip->verify_buf	= nuc900_verify_buf;
	chip->chip_delay	= 50;
	chip->options		= 0;
	chip->ecc.mode		= NAND_ECC_SOFT;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		retval = -ENXIO;
		goto fail1;
	}

	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		retval = -EBUSY;
		goto fail1;
	}

	nuc900_nand->reg = ioremap(res->start, resource_size(res));
	if (!nuc900_nand->reg) {
		retval = -ENOMEM;
		goto fail2;
	}

	nuc900_nand_enable(nuc900_nand);

	if (nand_scan(&(nuc900_nand->mtd), 1)) {
		retval = -ENXIO;
		goto fail3;
	}

	mtd_device_register(&(nuc900_nand->mtd), partitions,
			    ARRAY_SIZE(partitions));

	platform_set_drvdata(pdev, nuc900_nand);

	return retval;

fail3:	iounmap(nuc900_nand->reg);
fail2:	release_mem_region(res->start, resource_size(res));
fail1:	kfree(nuc900_nand);
	return retval;
}

static int __devexit nuc900_nand_remove(struct platform_device *pdev)
{
	struct nuc900_nand *nuc900_nand = platform_get_drvdata(pdev);
	struct resource *res;

	iounmap(nuc900_nand->reg);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	clk_disable(nuc900_nand->clk);
	clk_put(nuc900_nand->clk);

	kfree(nuc900_nand);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver nuc900_nand_driver = {
	.probe		= nuc900_nand_probe,
	.remove		= __devexit_p(nuc900_nand_remove),
	.driver		= {
		.name	= "nuc900-fmi",
		.owner	= THIS_MODULE,
	},
};

static int __init nuc900_nand_init(void)
{
	return platform_driver_register(&nuc900_nand_driver);
}

static void __exit nuc900_nand_exit(void)
{
	platform_driver_unregister(&nuc900_nand_driver);
}

module_init(nuc900_nand_init);
module_exit(nuc900_nand_exit);

MODULE_AUTHOR("Wan ZongShun <mcuos.com@gmail.com>");
MODULE_DESCRIPTION("w90p910/NUC9xx nand driver!");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nuc900-fmi");
