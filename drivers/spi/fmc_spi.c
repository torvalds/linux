// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fmc_spi.c - FMC SPI driver for the Aspeed SoC
 *
 * Copyright (C) ASPEED Technology Inc.
 * Ryan Chen <ryan_chen@aspeedtech.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>

#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

/******************************************************************************/
/* AST_SPI_CONFIG 0x00 : SPI00 CE Type Setting Register */
#define AST_G5_SPI_CONF_CE1_WEN		(0x1 << 17)
#define AST_G5_SPI_CONF_CE0_WEN		(0x1 << 16)

#define SPI_CONF_CE0_WEN		(0x1)

/* Register offsets */
#define FMC_SPI_CONFIG			0x00
#define FMC_SPI_CTRL			0x04
#define FMC_SPI_DMA_STS			0x08

#define FMC_SPI_CE0_CTRL		0x10
#define FMC_SPI_CE1_CTRL		0x14
#define FMC_SPI_CE2_CTRL		0x18

#define FMC_SPI_ADDR_DECODE_REG	0x30

#define AST_SPI_DMA_CTRL		0x80
#define AST_SPI_DMA_FLASH_BASE	0x84
#define AST_SPI_DMA_DRAM_BASE	0x88
#define AST_SPI_DMA_LENGTH	0x8c

/* AST_FMC_CONFIG 0x00 : FMC00 CE Type Setting Register */
#define FMC_CONF_LAGACY_DIS	(0x1 << 31)
#define FMC_CONF_CE2_WEN		(0x1 << 18)
#define FMC_CONF_CE1_WEN		(0x1 << 17)
#define FMC_CONF_CE0_WEN		(0x1 << 16)
#define FMC_CONF_CE2_SPI		(0x2 << 4)
#define FMC_CONF_CE1_SPI		(0x2 << 2)
#define FMC_CONF_CE0_SPI		(0x2)


/* FMC_SPI_CTRL	: 0x04 : FMC04 CE Control Register */
#define FMC_CTRL_CE1_4BYTE_MODE	(0x1 << 1)
#define FMC_CTRL_CE0_4BYTE_MODE	(0x1)

/* FMC_SPI_DMA_STS	: 0x08 : FMC08 Interrupt Control and Status Register */
#define FMC_STS_DMA_READY		0x0800
#define FMC_STS_DMA_CLEAR		0x0800

/* FMC_CE0_CTRL	for SPI 0x10, 0x14, 0x18, 0x1c, 0x20 */
#define SPI_IO_MODE_MASK		(3 << 28)
#define SPI_SINGLE_BIT			(0 << 28)
#define SPI_DUAL_MODE			(0x2 << 28)
#define SPI_DUAL_IO_MODE		(0x3 << 28)
#define SPI_QUAD_MODE			(0x4 << 28)
#define SPI_QUAD_IO_MODE		(0x5 << 28)

#define SPI_CE_WIDTH(x)			(x << 24)
#define SPI_CMD_DATA_MASK		(0xff << 16)
#define SPI_CMD_DATA(x)			(x << 16)
#define SPI_DUMMY_CMD			(1 << 15)
#define SPI_DUMMY_HIGH			(1 << 14)
//#define SPI_CLK_DIV				(1 << 13)		?? TODO ask....
//#define SPI_ADDR_CYCLE			(1 << 13)		?? TODO ask....
#define SPI_CMD_MERGE_DIS		(1 << 12)
#define SPI_CLK_DIV(x)			(x << 8)
#define SPI_CLK_DIV_MASK		(0xf << 8)

#define SPI_DUMMY_LOW_MASK		(0x3 << 6)
#define SPI_DUMMY_LOW(x)		((x) << 6)
#define SPI_LSB_FIRST_CTRL		(1 << 5)
#define SPI_CPOL_1				(1 << 4)
#define SPI_DUAL_DATA			(1 << 3)
#define SPI_CE_INACTIVE			(1 << 2)
#define SPI_CMD_MODE_MASK		(0x3)
#define SPI_CMD_NORMAL_READ_MODE	0
#define SPI_CMD_READ_CMD_MODE		1
#define SPI_CMD_WRITE_CMD_MODE		2
#define SPI_CMD_USER_MODE			3


/* AST_SPI_DMA_CTRL				0x80 */
#define FMC_DMA_ENABLE		(0x1)

#define G6_SEGMENT_ADDR_START(reg)		(reg & 0xffff)
#define G6_SEGMENT_ADDR_END(reg)		((reg >> 16) & 0xffff)
#define G6_SEGMENT_ADDR_VALUE(start, end)					\
	((((start) >> 16) & 0xffff) | (((end) - 0x100000) & 0xffff0000))
/******************************************************************************/
static int ast2600_set_spi_segment_addr(u32 *reg, u32 start, u32 end);

/******************************************************************************/
struct fmc_spi_host {
	void __iomem		*base;
	void __iomem		*ctrl_reg;
	u32		buff[5];
	struct spi_master *master;
	struct spi_device *spi_dev;
	struct device *dev;
	u32					ahb_clk;
	spinlock_t			lock;
};

struct aspeed_spi_info {
	int (*set_segment)(u32 *reg, u32 start, u32 end);
};

/*
 * Keeping default setting. If it is modified before,
 * its segment address should be re-configured.
 */
struct aspeed_spi_info ast2500_spi_info = {
	.set_segment = NULL,
};

struct aspeed_spi_info ast2600_spi_info = {
	.set_segment = ast2600_set_spi_segment_addr,
};

/******************************************************************************/

static int ast2600_set_spi_segment_addr(u32 *reg, u32 start, u32 end)
{
	int ret = 0;
	u32 segment_val;

	segment_val = G6_SEGMENT_ADDR_VALUE(start, end + 1);

	/* for ast2600, the start and end decode address should not be the same.*/
	if (G6_SEGMENT_ADDR_START(segment_val) == G6_SEGMENT_ADDR_END(segment_val))
		return -EINVAL;

	writel(segment_val, reg);

	return ret;
}

static u32 ast_spi_calculate_divisor(struct fmc_spi_host *host, u32 max_speed_hz)
{
	//[0] ->15 : HCLK , HCLK/16
	u8 SPI_DIV[16] = {16, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 0};
	u32 i;
	u32 spi_cdvr = 0;

	for (i = 1; i < 17; i++) {
		if (max_speed_hz >= (host->ahb_clk / i)) {
			spi_cdvr = SPI_DIV[i - 1];
			break;
		}
	}

	//printk("hclk is %d, divisor is %d, target :%d , cal speed %d\n", host->ahb_clk, spi_cdvr, spi->max_speed_hz, hclk/i);
	return spi_cdvr;
}

/* the spi->mode bits understood by this driver: */
#define MODEBITS (SPI_MODE_0 | SPI_RX_DUAL | SPI_TX_DUAL | SPI_RX_QUAD | SPI_TX_QUAD)

static int fmc_spi_setup(struct spi_device *spi)
{
	struct fmc_spi_host *host = (struct fmc_spi_host *)spi_master_get_devdata(spi->master);
	unsigned int bits = spi->bits_per_word;
	u32 fmc_config = 0;
	u32 spi_ctrl = 0;
	u32 divisor;

	fmc_config = readl(host->base);

	dev_dbg(host->dev, "%s cs: %d, spi->mode %d\n", __func__, spi->chip_select, spi->mode);
	dev_dbg(host->dev, "%s cs: %d, spi->mode %d spi->max_speed_hz %d , spi->bits_per_word %d\n",
		__func__, spi->chip_select, spi->mode, spi->max_speed_hz, spi->bits_per_word);

	switch (spi->chip_select) {
	case 0:
		fmc_config |= FMC_CONF_CE0_WEN | FMC_CONF_CE0_SPI;
		host->ctrl_reg = host->base + FMC_SPI_CE0_CTRL;
		break;
	case 1:
		fmc_config |= FMC_CONF_CE1_WEN | FMC_CONF_CE1_SPI;
		host->ctrl_reg = host->base + FMC_SPI_CE1_CTRL;
		break;
	case 2:
		fmc_config |= FMC_CONF_CE2_WEN | FMC_CONF_CE2_SPI;
		host->ctrl_reg = host->base + FMC_SPI_CE2_CTRL;
		break;
	default:
		dev_dbg(&spi->dev,
				"setup: invalid chipselect %u (%u defined)\n",
				spi->chip_select, spi->master->num_chipselect);
		return -EINVAL;
	}
	writel(fmc_config, host->base);


	if (bits == 0)
		bits = 8;

	if (bits < 8 || bits > 16) {
		dev_err(&spi->dev,
			"setup: invalid bits_per_word %u (8 to 16)\n",
			bits);
		return -EINVAL;
	}

	if (spi->mode & ~MODEBITS) {
		dev_err(&spi->dev, "setup: unsupported mode bits %x\n",
			spi->mode & ~MODEBITS);
		return -EINVAL;
	}

	/* see notes above re chipselect */
	if ((spi->chip_select == 0) && (spi->mode & SPI_CS_HIGH)) {
		dev_dbg(&spi->dev, "setup: can't be active-high\n");
		return -EINVAL;
	}

	/*
	 * Pre-new_1 chips start out at half the peripheral
	 * bus speed.
	 */

	if (spi->max_speed_hz) {
		/* Set the SPI slaves select and characteristic control register */
		divisor = ast_spi_calculate_divisor(host, spi->max_speed_hz);
	} else {
		/* speed zero means "as slow as possible" */
		divisor = 15;
	}

	if ((spi->mode & SPI_RX_DUAL) != 0 && (spi->mode & SPI_TX_DUAL) != 0)
		spi_ctrl |= SPI_DUAL_IO_MODE;
	else if ((spi->mode & SPI_RX_QUAD) != 0 && (spi->mode & SPI_TX_QUAD) != 0)
		spi_ctrl |= SPI_QUAD_IO_MODE;

	spi_ctrl &= ~SPI_CLK_DIV_MASK;
	/* printk("set div %x\n",divisor); */
	/* TODO MASK first */
	spi_ctrl |= SPI_CLK_DIV(divisor);

	/* only support mode 0 (CPOL=0, CPHA=0) and cannot support mode 1 ~ mode 3 */

#if 0
	if (SPI_CPHA & spi->mode)
		cpha = SPI_CPHA_1;
	else
		cpha = SPI_CPHA_0;
#endif
	/* ISSUE : ast spi ctrl couldn't use mode 3, so fix mode 0 */
	spi_ctrl &= ~SPI_CPOL_1;


	if (SPI_LSB_FIRST & spi->mode)
		spi_ctrl |= SPI_LSB_FIRST_CTRL;
	else
		spi_ctrl &= ~SPI_LSB_FIRST_CTRL;


	/* Configure SPI controller */
	writel(spi_ctrl, host->ctrl_reg);

	//printk("ctrl  %x, ", spi_ctrl);
	return 0;
}

static int fmc_spi_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct fmc_spi_host *host = (struct fmc_spi_host *)spi_master_get_devdata(spi->master);
	struct spi_transfer *xfer;
	const u8 *tx_buf;
	u8 *rx_buf;
	unsigned long flags;
	u32 *ctrl_reg;
	int i = 0;
	int j = 0;

	/* dev_dbg(host->dev, "xfer %s\n", dev_name(&spi->dev)); */
	dev_dbg(host->dev, "xfer spi->chip_select %d\n", spi->chip_select);
	host->spi_dev = spi;
	spin_lock_irqsave(&host->lock, flags);

	ctrl_reg = (u32 *)(host->base + FMC_SPI_CE0_CTRL +
						host->spi_dev->chip_select * 4);
	/* start user-mode (standard SPI) */
	writel(readl(ctrl_reg) | SPI_CMD_USER_MODE | SPI_CE_INACTIVE, ctrl_reg);
	writel(readl(ctrl_reg) & (~SPI_CE_INACTIVE), ctrl_reg);

	msg->actual_length = 0;
	msg->status = 0;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		dev_dbg(host->dev,
				"xfer[%d] %p: width %d, len %u, tx %p/%08x, rx %p/%08x\n",
				j, xfer,
				xfer->bits_per_word, xfer->len,
				xfer->tx_buf, xfer->tx_dma,
				xfer->rx_buf, xfer->rx_dma);

		tx_buf = xfer->tx_buf;
		rx_buf = xfer->rx_buf;

		if (tx_buf != 0) {
#if 0
			pr_info("tx : ");
			if (xfer->len > 10) {
				for (i = 0; i < 10; i++)
					pr_info("%x ", tx_buf[i]);
			} else {
				for (i = 0; i < xfer->len; i++)
					pr_info("%x ", tx_buf[i]);
			}
			pr_info("\n");
#endif
			for (i = 0; i < xfer->len; i++)
				writeb(tx_buf[i], (void *)host->buff[host->spi_dev->chip_select]);
		}
		/* Issue need clarify */
		udelay(1);
		if (rx_buf != 0) {
			for (i = 0; i < xfer->len; i++)
				rx_buf[i] = readb((void *)host->buff[host->spi_dev->chip_select]);
#if 0
			pr_info("rx : ");
			if (xfer->len > 10) {
				for (i = 0; i < 10; i++)
					pr_info(" %x", rx_buf[i]);
			} else {
				for (i = 0; i < xfer->len; i++)
					pr_info(" %x", rx_buf[i]);
			}
			pr_info("\n");
#endif
		}

		dev_dbg(host->dev, "old msg->actual_length %d , +len %d\n",
			msg->actual_length, xfer->len);
		msg->actual_length += xfer->len;
		dev_dbg(host->dev, "new msg->actual_length %d\n",
			msg->actual_length);
		j++;

	}

	/* end of user-mode (standard SPI) */
	writel(readl(ctrl_reg) | SPI_CE_INACTIVE, ctrl_reg);
	writel(readl(ctrl_reg) & (~(SPI_CMD_USER_MODE | SPI_CE_INACTIVE)), ctrl_reg);
	msg->status = 0;

	msg->complete(msg->context);

	/* spin_unlock(&host->lock); */
	spin_unlock_irqrestore(&host->lock, flags);

	return 0;
}

static void fmc_spi_cleanup(struct spi_device *spi)
{
	struct fmc_spi_host *host = spi_master_get_devdata(spi->master);
	unsigned long flags;

	dev_dbg(host->dev, "%s\n", __func__);

	spin_lock_irqsave(&host->lock, flags);

	spin_unlock_irqrestore(&host->lock, flags);
}


static const struct of_device_id fmc_spi_of_match[] = {
	{ .compatible = "aspeed,fmc-spi", .data = &ast2500_spi_info},
	{ .compatible = "aspeed,ast2600-fmc-spi", .data = &ast2600_spi_info},
	{ },
};

static int fmc_spi_probe(struct platform_device *pdev)
{
	struct resource	*res;
	struct fmc_spi_host *host;
	struct spi_master *master;
	struct clk *clk;
	const struct of_device_id *match;
	const struct aspeed_spi_info *spi_info;
	int cs_num = 0;
	int err = 0;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	master = spi_alloc_master(&pdev->dev, sizeof(struct fmc_spi_host));
	if (master == NULL) {
		dev_err(&pdev->dev, "No memory for spi_master\n");
		err = -ENOMEM;
		goto err_nomem;
	}

	/* the spi->mode bits understood by this driver: */
	master->bits_per_word_mask = SPI_BPW_MASK(8);

	master->mode_bits = SPI_MODE_0 | SPI_RX_DUAL | SPI_TX_DUAL |
			    SPI_RX_QUAD | SPI_TX_QUAD;
	//master->bits_per_word_mask = SPI_BPW_RANGE_MASK(8, 16);
	master->dev.of_node = pdev->dev.of_node;
	master->bus_num = pdev->id;
	// master->num_chipselect = master->dev.of_node ? 0 : 4;
	platform_set_drvdata(pdev, master);

	host = spi_master_get_devdata(master);
	memset(host, 0, sizeof(struct fmc_spi_host));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot get IORESOURCE_MEM 0\n");
		err = -ENXIO;
		goto err_no_io_res;
	}

	host->base = devm_ioremap_resource(&pdev->dev, res);
	if (!host->base) {
		dev_err(&pdev->dev, "cannot remap register\n");
		err = -EIO;
		goto err_no_io_res;
	}

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "no clock defined\n");
		return -ENODEV;
	}
	host->ahb_clk = clk_get_rate(clk);

	dev_dbg(&pdev->dev, "remap phy %x, virt %x hclk : %d\n",
		(u32)res->start, (u32)host->base, host->ahb_clk);

	host->master = spi_master_get(master);

	match = of_match_device(fmc_spi_of_match, &pdev->dev);
	if (!match || !match->data)
		return -ENODEV;
	spi_info = match->data;

	if (of_property_read_u16(pdev->dev.of_node, "number_of_chip_select",
		&host->master->num_chipselect))
		goto err_register;

	for (cs_num = 0; cs_num < host->master->num_chipselect; cs_num++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, cs_num + 1);
		if (!res) {
			dev_err(&pdev->dev, "cannot get IORESOURCE_IO 0.\n");
			return -ENXIO;
		}

		/* set address decode range */
		if (spi_info != NULL && spi_info->set_segment != NULL) {
			err = spi_info->set_segment(host->base + FMC_SPI_ADDR_DECODE_REG + cs_num * 4,
								(u32)res->start, (u32)res->end);
			if (err) {
				dev_err(&pdev->dev, "fail to set decode range.\n");
				goto err_no_io_res;
			}
		}

		host->buff[cs_num] = (u32)devm_ioremap_resource(&pdev->dev, res);
		if (!host->buff[cs_num]) {
			dev_err(&pdev->dev, "cannot remap buffer\n");
			err = -EIO;
			goto err_no_io_res;
		}

		dev_dbg(&pdev->dev, "remap io phy %x, virt %x\n",
			(u32)res->start, (u32)host->buff[cs_num]);
	}

	host->master->bus_num = pdev->id;
	host->dev = &pdev->dev;

	/* Setup the state for bitbang driver */
	host->master->setup = fmc_spi_setup;
	host->master->transfer = fmc_spi_transfer;
	host->master->cleanup = fmc_spi_cleanup;

	platform_set_drvdata(pdev, host);

	/* Register our spi controller */
	err = devm_spi_register_master(&pdev->dev, host->master);
	if (err) {
		dev_err(&pdev->dev, "failed to register SPI master\n");
		goto err_register;
	}

	dev_dbg(&pdev->dev, "fmc_spi : driver load\n");

	return 0;

err_register:
	spi_master_put(host->master);
	iounmap(host->base);
	for (cs_num = 0; cs_num < host->master->num_chipselect; cs_num++)
		iounmap((void *)host->buff[cs_num]);

err_no_io_res:
	kfree(master);
	kfree(host);

err_nomem:
	return err;

}

static int fmc_spi_remove(struct platform_device *pdev)
{
	struct resource *res0;
	struct fmc_spi_host *host = platform_get_drvdata(pdev);

	dev_dbg(host->dev, "%s\n", __func__);

	if (!host)
		return -1;

	res0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res0->start, res0->end - res0->start + 1);
	iounmap(host->base);
	iounmap(host->buff);

	platform_set_drvdata(pdev, NULL);
	spi_unregister_master(host->master);
	spi_master_put(host->master);
	return 0;
}

static struct platform_driver fmc_spi_driver = {
	.probe = fmc_spi_probe,
	.remove = fmc_spi_remove,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table = fmc_spi_of_match,
	},
};

module_platform_driver(fmc_spi_driver);

MODULE_DESCRIPTION("FMC SPI Driver");
MODULE_AUTHOR("Ryan Chen");
MODULE_LICENSE("GPL");

