/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Bayi Cheng <bayi.cheng@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spi-nor.h>

#define MTK_NOR_CMD_REG			0x00
#define MTK_NOR_CNT_REG			0x04
#define MTK_NOR_RDSR_REG		0x08
#define MTK_NOR_RDATA_REG		0x0c
#define MTK_NOR_RADR0_REG		0x10
#define MTK_NOR_RADR1_REG		0x14
#define MTK_NOR_RADR2_REG		0x18
#define MTK_NOR_WDATA_REG		0x1c
#define MTK_NOR_PRGDATA0_REG		0x20
#define MTK_NOR_PRGDATA1_REG		0x24
#define MTK_NOR_PRGDATA2_REG		0x28
#define MTK_NOR_PRGDATA3_REG		0x2c
#define MTK_NOR_PRGDATA4_REG		0x30
#define MTK_NOR_PRGDATA5_REG		0x34
#define MTK_NOR_SHREG0_REG		0x38
#define MTK_NOR_SHREG1_REG		0x3c
#define MTK_NOR_SHREG2_REG		0x40
#define MTK_NOR_SHREG3_REG		0x44
#define MTK_NOR_SHREG4_REG		0x48
#define MTK_NOR_SHREG5_REG		0x4c
#define MTK_NOR_SHREG6_REG		0x50
#define MTK_NOR_SHREG7_REG		0x54
#define MTK_NOR_SHREG8_REG		0x58
#define MTK_NOR_SHREG9_REG		0x5c
#define MTK_NOR_CFG1_REG		0x60
#define MTK_NOR_CFG2_REG		0x64
#define MTK_NOR_CFG3_REG		0x68
#define MTK_NOR_STATUS0_REG		0x70
#define MTK_NOR_STATUS1_REG		0x74
#define MTK_NOR_STATUS2_REG		0x78
#define MTK_NOR_STATUS3_REG		0x7c
#define MTK_NOR_FLHCFG_REG		0x84
#define MTK_NOR_TIME_REG		0x94
#define MTK_NOR_PP_DATA_REG		0x98
#define MTK_NOR_PREBUF_STUS_REG		0x9c
#define MTK_NOR_DELSEL0_REG		0xa0
#define MTK_NOR_DELSEL1_REG		0xa4
#define MTK_NOR_INTRSTUS_REG		0xa8
#define MTK_NOR_INTREN_REG		0xac
#define MTK_NOR_CHKSUM_CTL_REG		0xb8
#define MTK_NOR_CHKSUM_REG		0xbc
#define MTK_NOR_CMD2_REG		0xc0
#define MTK_NOR_WRPROT_REG		0xc4
#define MTK_NOR_RADR3_REG		0xc8
#define MTK_NOR_DUAL_REG		0xcc
#define MTK_NOR_DELSEL2_REG		0xd0
#define MTK_NOR_DELSEL3_REG		0xd4
#define MTK_NOR_DELSEL4_REG		0xd8

/* commands for mtk nor controller */
#define MTK_NOR_READ_CMD		0x0
#define MTK_NOR_RDSR_CMD		0x2
#define MTK_NOR_PRG_CMD			0x4
#define MTK_NOR_WR_CMD			0x10
#define MTK_NOR_PIO_WR_CMD		0x90
#define MTK_NOR_WRSR_CMD		0x20
#define MTK_NOR_PIO_READ_CMD		0x81
#define MTK_NOR_WR_BUF_ENABLE		0x1
#define MTK_NOR_WR_BUF_DISABLE		0x0
#define MTK_NOR_ENABLE_SF_CMD		0x30
#define MTK_NOR_DUAD_ADDR_EN		0x8
#define MTK_NOR_QUAD_READ_EN		0x4
#define MTK_NOR_DUAL_ADDR_EN		0x2
#define MTK_NOR_DUAL_READ_EN		0x1
#define MTK_NOR_DUAL_DISABLE		0x0
#define MTK_NOR_FAST_READ		0x1

#define SFLASH_WRBUF_SIZE		128

/* Can shift up to 48 bits (6 bytes) of TX/RX */
#define MTK_NOR_MAX_RX_TX_SHIFT		6
/* can shift up to 56 bits (7 bytes) transfer by MTK_NOR_PRG_CMD */
#define MTK_NOR_MAX_SHIFT		7
/* nor controller 4-byte address mode enable bit */
#define MTK_NOR_4B_ADDR_EN		BIT(4)

/* Helpers for accessing the program data / shift data registers */
#define MTK_NOR_PRG_REG(n)		(MTK_NOR_PRGDATA0_REG + 4 * (n))
#define MTK_NOR_SHREG(n)		(MTK_NOR_SHREG0_REG + 4 * (n))

struct mtk_nor {
	struct spi_nor nor;
	struct device *dev;
	void __iomem *base;	/* nor flash base address */
	struct clk *spi_clk;
	struct clk *nor_clk;
};

static void mtk_nor_set_read_mode(struct mtk_nor *mtk_nor)
{
	struct spi_nor *nor = &mtk_nor->nor;

	switch (nor->read_proto) {
	case SNOR_PROTO_1_1_1:
		writeb(nor->read_opcode, mtk_nor->base +
		       MTK_NOR_PRGDATA3_REG);
		writeb(MTK_NOR_FAST_READ, mtk_nor->base +
		       MTK_NOR_CFG1_REG);
		break;
	case SNOR_PROTO_1_1_2:
		writeb(nor->read_opcode, mtk_nor->base +
		       MTK_NOR_PRGDATA3_REG);
		writeb(MTK_NOR_DUAL_READ_EN, mtk_nor->base +
		       MTK_NOR_DUAL_REG);
		break;
	case SNOR_PROTO_1_1_4:
		writeb(nor->read_opcode, mtk_nor->base +
		       MTK_NOR_PRGDATA4_REG);
		writeb(MTK_NOR_QUAD_READ_EN, mtk_nor->base +
		       MTK_NOR_DUAL_REG);
		break;
	default:
		writeb(MTK_NOR_DUAL_DISABLE, mtk_nor->base +
		       MTK_NOR_DUAL_REG);
		break;
	}
}

static int mtk_nor_execute_cmd(struct mtk_nor *mtk_nor, u8 cmdval)
{
	int reg;
	u8 val = cmdval & 0x1f;

	writeb(cmdval, mtk_nor->base + MTK_NOR_CMD_REG);
	return readl_poll_timeout(mtk_nor->base + MTK_NOR_CMD_REG, reg,
				  !(reg & val), 100, 10000);
}

static int mtk_nor_do_tx_rx(struct mtk_nor *mtk_nor, u8 op,
			    u8 *tx, int txlen, u8 *rx, int rxlen)
{
	int len = 1 + txlen + rxlen;
	int i, ret, idx;

	if (len > MTK_NOR_MAX_SHIFT)
		return -EINVAL;

	writeb(len * 8, mtk_nor->base + MTK_NOR_CNT_REG);

	/* start at PRGDATA5, go down to PRGDATA0 */
	idx = MTK_NOR_MAX_RX_TX_SHIFT - 1;

	/* opcode */
	writeb(op, mtk_nor->base + MTK_NOR_PRG_REG(idx));
	idx--;

	/* program TX data */
	for (i = 0; i < txlen; i++, idx--)
		writeb(tx[i], mtk_nor->base + MTK_NOR_PRG_REG(idx));

	/* clear out rest of TX registers */
	while (idx >= 0) {
		writeb(0, mtk_nor->base + MTK_NOR_PRG_REG(idx));
		idx--;
	}

	ret = mtk_nor_execute_cmd(mtk_nor, MTK_NOR_PRG_CMD);
	if (ret)
		return ret;

	/* restart at first RX byte */
	idx = rxlen - 1;

	/* read out RX data */
	for (i = 0; i < rxlen; i++, idx--)
		rx[i] = readb(mtk_nor->base + MTK_NOR_SHREG(idx));

	return 0;
}

/* Do a WRSR (Write Status Register) command */
static int mtk_nor_wr_sr(struct mtk_nor *mtk_nor, u8 sr)
{
	writeb(sr, mtk_nor->base + MTK_NOR_PRGDATA5_REG);
	writeb(8, mtk_nor->base + MTK_NOR_CNT_REG);
	return mtk_nor_execute_cmd(mtk_nor, MTK_NOR_WRSR_CMD);
}

static int mtk_nor_write_buffer_enable(struct mtk_nor *mtk_nor)
{
	u8 reg;

	/* the bit0 of MTK_NOR_CFG2_REG is pre-fetch buffer
	 * 0: pre-fetch buffer use for read
	 * 1: pre-fetch buffer use for page program
	 */
	writel(MTK_NOR_WR_BUF_ENABLE, mtk_nor->base + MTK_NOR_CFG2_REG);
	return readb_poll_timeout(mtk_nor->base + MTK_NOR_CFG2_REG, reg,
				  0x01 == (reg & 0x01), 100, 10000);
}

static int mtk_nor_write_buffer_disable(struct mtk_nor *mtk_nor)
{
	u8 reg;

	writel(MTK_NOR_WR_BUF_DISABLE, mtk_nor->base + MTK_NOR_CFG2_REG);
	return readb_poll_timeout(mtk_nor->base + MTK_NOR_CFG2_REG, reg,
				  MTK_NOR_WR_BUF_DISABLE == (reg & 0x1), 100,
				  10000);
}

static void mtk_nor_set_addr_width(struct mtk_nor *mtk_nor)
{
	u8 val;
	struct spi_nor *nor = &mtk_nor->nor;

	val = readb(mtk_nor->base + MTK_NOR_DUAL_REG);

	switch (nor->addr_width) {
	case 3:
		val &= ~MTK_NOR_4B_ADDR_EN;
		break;
	case 4:
		val |= MTK_NOR_4B_ADDR_EN;
		break;
	default:
		dev_warn(mtk_nor->dev, "Unexpected address width %u.\n",
			 nor->addr_width);
		break;
	}

	writeb(val, mtk_nor->base + MTK_NOR_DUAL_REG);
}

static void mtk_nor_set_addr(struct mtk_nor *mtk_nor, u32 addr)
{
	int i;

	mtk_nor_set_addr_width(mtk_nor);

	for (i = 0; i < 3; i++) {
		writeb(addr & 0xff, mtk_nor->base + MTK_NOR_RADR0_REG + i * 4);
		addr >>= 8;
	}
	/* Last register is non-contiguous */
	writeb(addr & 0xff, mtk_nor->base + MTK_NOR_RADR3_REG);
}

static ssize_t mtk_nor_read(struct spi_nor *nor, loff_t from, size_t length,
			    u_char *buffer)
{
	int i, ret;
	int addr = (int)from;
	u8 *buf = (u8 *)buffer;
	struct mtk_nor *mtk_nor = nor->priv;

	/* set mode for fast read mode ,dual mode or quad mode */
	mtk_nor_set_read_mode(mtk_nor);
	mtk_nor_set_addr(mtk_nor, addr);

	for (i = 0; i < length; i++) {
		ret = mtk_nor_execute_cmd(mtk_nor, MTK_NOR_PIO_READ_CMD);
		if (ret < 0)
			return ret;
		buf[i] = readb(mtk_nor->base + MTK_NOR_RDATA_REG);
	}
	return length;
}

static int mtk_nor_write_single_byte(struct mtk_nor *mtk_nor,
				     int addr, int length, u8 *data)
{
	int i, ret;

	mtk_nor_set_addr(mtk_nor, addr);

	for (i = 0; i < length; i++) {
		writeb(*data++, mtk_nor->base + MTK_NOR_WDATA_REG);
		ret = mtk_nor_execute_cmd(mtk_nor, MTK_NOR_PIO_WR_CMD);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int mtk_nor_write_buffer(struct mtk_nor *mtk_nor, int addr,
				const u8 *buf)
{
	int i, bufidx, data;

	mtk_nor_set_addr(mtk_nor, addr);

	bufidx = 0;
	for (i = 0; i < SFLASH_WRBUF_SIZE; i += 4) {
		data = buf[bufidx + 3]<<24 | buf[bufidx + 2]<<16 |
		       buf[bufidx + 1]<<8 | buf[bufidx];
		bufidx += 4;
		writel(data, mtk_nor->base + MTK_NOR_PP_DATA_REG);
	}
	return mtk_nor_execute_cmd(mtk_nor, MTK_NOR_WR_CMD);
}

static ssize_t mtk_nor_write(struct spi_nor *nor, loff_t to, size_t len,
			     const u_char *buf)
{
	int ret;
	struct mtk_nor *mtk_nor = nor->priv;
	size_t i;

	ret = mtk_nor_write_buffer_enable(mtk_nor);
	if (ret < 0) {
		dev_warn(mtk_nor->dev, "write buffer enable failed!\n");
		return ret;
	}

	for (i = 0; i + SFLASH_WRBUF_SIZE <= len; i += SFLASH_WRBUF_SIZE) {
		ret = mtk_nor_write_buffer(mtk_nor, to, buf);
		if (ret < 0) {
			dev_err(mtk_nor->dev, "write buffer failed!\n");
			return ret;
		}
		to += SFLASH_WRBUF_SIZE;
		buf += SFLASH_WRBUF_SIZE;
	}
	ret = mtk_nor_write_buffer_disable(mtk_nor);
	if (ret < 0) {
		dev_warn(mtk_nor->dev, "write buffer disable failed!\n");
		return ret;
	}

	if (i < len) {
		ret = mtk_nor_write_single_byte(mtk_nor, to,
						(int)(len - i), (u8 *)buf);
		if (ret < 0) {
			dev_err(mtk_nor->dev, "write single byte failed!\n");
			return ret;
		}
	}

	return len;
}

static int mtk_nor_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	int ret;
	struct mtk_nor *mtk_nor = nor->priv;

	switch (opcode) {
	case SPINOR_OP_RDSR:
		ret = mtk_nor_execute_cmd(mtk_nor, MTK_NOR_RDSR_CMD);
		if (ret < 0)
			return ret;
		if (len == 1)
			*buf = readb(mtk_nor->base + MTK_NOR_RDSR_REG);
		else
			dev_err(mtk_nor->dev, "len should be 1 for read status!\n");
		break;
	default:
		ret = mtk_nor_do_tx_rx(mtk_nor, opcode, NULL, 0, buf, len);
		break;
	}
	return ret;
}

static int mtk_nor_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf,
			     int len)
{
	int ret;
	struct mtk_nor *mtk_nor = nor->priv;

	switch (opcode) {
	case SPINOR_OP_WRSR:
		/* We only handle 1 byte */
		ret = mtk_nor_wr_sr(mtk_nor, *buf);
		break;
	default:
		ret = mtk_nor_do_tx_rx(mtk_nor, opcode, buf, len, NULL, 0);
		if (ret)
			dev_warn(mtk_nor->dev, "write reg failure!\n");
		break;
	}
	return ret;
}

static void mtk_nor_disable_clk(struct mtk_nor *mtk_nor)
{
	clk_disable_unprepare(mtk_nor->spi_clk);
	clk_disable_unprepare(mtk_nor->nor_clk);
}

static int mtk_nor_enable_clk(struct mtk_nor *mtk_nor)
{
	int ret;

	ret = clk_prepare_enable(mtk_nor->spi_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(mtk_nor->nor_clk);
	if (ret) {
		clk_disable_unprepare(mtk_nor->spi_clk);
		return ret;
	}

	return 0;
}

static int mtk_nor_init(struct mtk_nor *mtk_nor,
			struct device_node *flash_node)
{
	const struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ |
			SNOR_HWCAPS_READ_FAST |
			SNOR_HWCAPS_READ_1_1_2 |
			SNOR_HWCAPS_PP,
	};
	int ret;
	struct spi_nor *nor;

	/* initialize controller to accept commands */
	writel(MTK_NOR_ENABLE_SF_CMD, mtk_nor->base + MTK_NOR_WRPROT_REG);

	nor = &mtk_nor->nor;
	nor->dev = mtk_nor->dev;
	nor->priv = mtk_nor;
	spi_nor_set_flash_node(nor, flash_node);

	/* fill the hooks to spi nor */
	nor->read = mtk_nor_read;
	nor->read_reg = mtk_nor_read_reg;
	nor->write = mtk_nor_write;
	nor->write_reg = mtk_nor_write_reg;
	nor->mtd.name = "mtk_nor";
	/* initialized with NULL */
	ret = spi_nor_scan(nor, NULL, &hwcaps);
	if (ret)
		return ret;

	return mtd_device_register(&nor->mtd, NULL, 0);
}

static int mtk_nor_drv_probe(struct platform_device *pdev)
{
	struct device_node *flash_np;
	struct resource *res;
	int ret;
	struct mtk_nor *mtk_nor;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "No DT found\n");
		return -EINVAL;
	}

	mtk_nor = devm_kzalloc(&pdev->dev, sizeof(*mtk_nor), GFP_KERNEL);
	if (!mtk_nor)
		return -ENOMEM;
	platform_set_drvdata(pdev, mtk_nor);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mtk_nor->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mtk_nor->base))
		return PTR_ERR(mtk_nor->base);

	mtk_nor->spi_clk = devm_clk_get(&pdev->dev, "spi");
	if (IS_ERR(mtk_nor->spi_clk))
		return PTR_ERR(mtk_nor->spi_clk);

	mtk_nor->nor_clk = devm_clk_get(&pdev->dev, "sf");
	if (IS_ERR(mtk_nor->nor_clk))
		return PTR_ERR(mtk_nor->nor_clk);

	mtk_nor->dev = &pdev->dev;

	ret = mtk_nor_enable_clk(mtk_nor);
	if (ret)
		return ret;

	/* only support one attached flash */
	flash_np = of_get_next_available_child(pdev->dev.of_node, NULL);
	if (!flash_np) {
		dev_err(&pdev->dev, "no SPI flash device to configure\n");
		ret = -ENODEV;
		goto nor_free;
	}
	ret = mtk_nor_init(mtk_nor, flash_np);

nor_free:
	if (ret)
		mtk_nor_disable_clk(mtk_nor);

	return ret;
}

static int mtk_nor_drv_remove(struct platform_device *pdev)
{
	struct mtk_nor *mtk_nor = platform_get_drvdata(pdev);

	mtk_nor_disable_clk(mtk_nor);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk_nor_suspend(struct device *dev)
{
	struct mtk_nor *mtk_nor = dev_get_drvdata(dev);

	mtk_nor_disable_clk(mtk_nor);

	return 0;
}

static int mtk_nor_resume(struct device *dev)
{
	struct mtk_nor *mtk_nor = dev_get_drvdata(dev);

	return mtk_nor_enable_clk(mtk_nor);
}

static const struct dev_pm_ops mtk_nor_dev_pm_ops = {
	.suspend = mtk_nor_suspend,
	.resume = mtk_nor_resume,
};

#define MTK_NOR_DEV_PM_OPS	(&mtk_nor_dev_pm_ops)
#else
#define MTK_NOR_DEV_PM_OPS	NULL
#endif

static const struct of_device_id mtk_nor_of_ids[] = {
	{ .compatible = "mediatek,mt8173-nor"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_nor_of_ids);

static struct platform_driver mtk_nor_driver = {
	.probe = mtk_nor_drv_probe,
	.remove = mtk_nor_drv_remove,
	.driver = {
		.name = "mtk-nor",
		.pm = MTK_NOR_DEV_PM_OPS,
		.of_match_table = mtk_nor_of_ids,
	},
};

module_platform_driver(mtk_nor_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek SPI NOR Flash Driver");
