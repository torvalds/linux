// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI NOR driver for NXP SPI Flash Interface (SPIFI)
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * Based on Freescale QuadSPI driver:
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spi-nor.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

/* NXP SPIFI registers, bits and macros */
#define SPIFI_CTRL				0x000
#define  SPIFI_CTRL_TIMEOUT(timeout)		(timeout)
#define  SPIFI_CTRL_CSHIGH(cshigh)		((cshigh) << 16)
#define  SPIFI_CTRL_MODE3			BIT(23)
#define  SPIFI_CTRL_DUAL			BIT(28)
#define  SPIFI_CTRL_FBCLK			BIT(30)
#define SPIFI_CMD				0x004
#define  SPIFI_CMD_DATALEN(dlen)		((dlen) & 0x3fff)
#define  SPIFI_CMD_DOUT				BIT(15)
#define  SPIFI_CMD_INTLEN(ilen)			((ilen) << 16)
#define  SPIFI_CMD_FIELDFORM(field)		((field) << 19)
#define  SPIFI_CMD_FIELDFORM_ALL_SERIAL		SPIFI_CMD_FIELDFORM(0x0)
#define  SPIFI_CMD_FIELDFORM_QUAD_DUAL_DATA	SPIFI_CMD_FIELDFORM(0x1)
#define  SPIFI_CMD_FRAMEFORM(frame)		((frame) << 21)
#define  SPIFI_CMD_FRAMEFORM_OPCODE_ONLY	SPIFI_CMD_FRAMEFORM(0x1)
#define  SPIFI_CMD_OPCODE(op)			((op) << 24)
#define SPIFI_ADDR				0x008
#define SPIFI_IDATA				0x00c
#define SPIFI_CLIMIT				0x010
#define SPIFI_DATA				0x014
#define SPIFI_MCMD				0x018
#define SPIFI_STAT				0x01c
#define  SPIFI_STAT_MCINIT			BIT(0)
#define  SPIFI_STAT_CMD				BIT(1)
#define  SPIFI_STAT_RESET			BIT(4)

#define SPI_NOR_MAX_ID_LEN	6

struct nxp_spifi {
	struct device *dev;
	struct clk *clk_spifi;
	struct clk *clk_reg;
	void __iomem *io_base;
	void __iomem *flash_base;
	struct spi_nor nor;
	bool memory_mode;
	u32 mcmd;
};

static int nxp_spifi_wait_for_cmd(struct nxp_spifi *spifi)
{
	u8 stat;
	int ret;

	ret = readb_poll_timeout(spifi->io_base + SPIFI_STAT, stat,
				 !(stat & SPIFI_STAT_CMD), 10, 30);
	if (ret)
		dev_warn(spifi->dev, "command timed out\n");

	return ret;
}

static int nxp_spifi_reset(struct nxp_spifi *spifi)
{
	u8 stat;
	int ret;

	writel(SPIFI_STAT_RESET, spifi->io_base + SPIFI_STAT);
	ret = readb_poll_timeout(spifi->io_base + SPIFI_STAT, stat,
				 !(stat & SPIFI_STAT_RESET), 10, 30);
	if (ret)
		dev_warn(spifi->dev, "state reset timed out\n");

	return ret;
}

static int nxp_spifi_set_memory_mode_off(struct nxp_spifi *spifi)
{
	int ret;

	if (!spifi->memory_mode)
		return 0;

	ret = nxp_spifi_reset(spifi);
	if (ret)
		dev_err(spifi->dev, "unable to enter command mode\n");
	else
		spifi->memory_mode = false;

	return ret;
}

static int nxp_spifi_set_memory_mode_on(struct nxp_spifi *spifi)
{
	u8 stat;
	int ret;

	if (spifi->memory_mode)
		return 0;

	writel(spifi->mcmd, spifi->io_base + SPIFI_MCMD);
	ret = readb_poll_timeout(spifi->io_base + SPIFI_STAT, stat,
				 stat & SPIFI_STAT_MCINIT, 10, 30);
	if (ret)
		dev_err(spifi->dev, "unable to enter memory mode\n");
	else
		spifi->memory_mode = true;

	return ret;
}

static int nxp_spifi_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf,
			      size_t len)
{
	struct nxp_spifi *spifi = nor->priv;
	u32 cmd;
	int ret;

	ret = nxp_spifi_set_memory_mode_off(spifi);
	if (ret)
		return ret;

	cmd = SPIFI_CMD_DATALEN(len) |
	      SPIFI_CMD_OPCODE(opcode) |
	      SPIFI_CMD_FIELDFORM_ALL_SERIAL |
	      SPIFI_CMD_FRAMEFORM_OPCODE_ONLY;
	writel(cmd, spifi->io_base + SPIFI_CMD);

	while (len--)
		*buf++ = readb(spifi->io_base + SPIFI_DATA);

	return nxp_spifi_wait_for_cmd(spifi);
}

static int nxp_spifi_write_reg(struct spi_nor *nor, u8 opcode, const u8 *buf,
			       size_t len)
{
	struct nxp_spifi *spifi = nor->priv;
	u32 cmd;
	int ret;

	ret = nxp_spifi_set_memory_mode_off(spifi);
	if (ret)
		return ret;

	cmd = SPIFI_CMD_DOUT |
	      SPIFI_CMD_DATALEN(len) |
	      SPIFI_CMD_OPCODE(opcode) |
	      SPIFI_CMD_FIELDFORM_ALL_SERIAL |
	      SPIFI_CMD_FRAMEFORM_OPCODE_ONLY;
	writel(cmd, spifi->io_base + SPIFI_CMD);

	while (len--)
		writeb(*buf++, spifi->io_base + SPIFI_DATA);

	return nxp_spifi_wait_for_cmd(spifi);
}

static ssize_t nxp_spifi_read(struct spi_nor *nor, loff_t from, size_t len,
			      u_char *buf)
{
	struct nxp_spifi *spifi = nor->priv;
	int ret;

	ret = nxp_spifi_set_memory_mode_on(spifi);
	if (ret)
		return ret;

	memcpy_fromio(buf, spifi->flash_base + from, len);

	return len;
}

static ssize_t nxp_spifi_write(struct spi_nor *nor, loff_t to, size_t len,
			       const u_char *buf)
{
	struct nxp_spifi *spifi = nor->priv;
	u32 cmd;
	int ret;
	size_t i;

	ret = nxp_spifi_set_memory_mode_off(spifi);
	if (ret)
		return ret;

	writel(to, spifi->io_base + SPIFI_ADDR);

	cmd = SPIFI_CMD_DOUT |
	      SPIFI_CMD_DATALEN(len) |
	      SPIFI_CMD_FIELDFORM_ALL_SERIAL |
	      SPIFI_CMD_OPCODE(nor->program_opcode) |
	      SPIFI_CMD_FRAMEFORM(spifi->nor.addr_nbytes + 1);
	writel(cmd, spifi->io_base + SPIFI_CMD);

	for (i = 0; i < len; i++)
		writeb(buf[i], spifi->io_base + SPIFI_DATA);

	ret = nxp_spifi_wait_for_cmd(spifi);
	if (ret)
		return ret;

	return len;
}

static int nxp_spifi_erase(struct spi_nor *nor, loff_t offs)
{
	struct nxp_spifi *spifi = nor->priv;
	u32 cmd;
	int ret;

	ret = nxp_spifi_set_memory_mode_off(spifi);
	if (ret)
		return ret;

	writel(offs, spifi->io_base + SPIFI_ADDR);

	cmd = SPIFI_CMD_FIELDFORM_ALL_SERIAL |
	      SPIFI_CMD_OPCODE(nor->erase_opcode) |
	      SPIFI_CMD_FRAMEFORM(spifi->nor.addr_nbytes + 1);
	writel(cmd, spifi->io_base + SPIFI_CMD);

	return nxp_spifi_wait_for_cmd(spifi);
}

static int nxp_spifi_setup_memory_cmd(struct nxp_spifi *spifi)
{
	switch (spifi->nor.read_proto) {
	case SNOR_PROTO_1_1_1:
		spifi->mcmd = SPIFI_CMD_FIELDFORM_ALL_SERIAL;
		break;
	case SNOR_PROTO_1_1_2:
	case SNOR_PROTO_1_1_4:
		spifi->mcmd = SPIFI_CMD_FIELDFORM_QUAD_DUAL_DATA;
		break;
	default:
		dev_err(spifi->dev, "unsupported SPI read mode\n");
		return -EINVAL;
	}

	/* Memory mode supports address length between 1 and 4 */
	if (spifi->nor.addr_nbytes < 1 || spifi->nor.addr_nbytes > 4)
		return -EINVAL;

	spifi->mcmd |= SPIFI_CMD_OPCODE(spifi->nor.read_opcode) |
		       SPIFI_CMD_INTLEN(spifi->nor.read_dummy / 8) |
		       SPIFI_CMD_FRAMEFORM(spifi->nor.addr_nbytes + 1);

	return 0;
}

static void nxp_spifi_dummy_id_read(struct spi_nor *nor)
{
	u8 id[SPI_NOR_MAX_ID_LEN];
	nor->controller_ops->read_reg(nor, SPINOR_OP_RDID, id,
				      SPI_NOR_MAX_ID_LEN);
}

static const struct spi_nor_controller_ops nxp_spifi_controller_ops = {
	.read_reg  = nxp_spifi_read_reg,
	.write_reg = nxp_spifi_write_reg,
	.read  = nxp_spifi_read,
	.write = nxp_spifi_write,
	.erase = nxp_spifi_erase,
};

static int nxp_spifi_setup_flash(struct nxp_spifi *spifi,
				 struct device_node *np)
{
	struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ |
			SNOR_HWCAPS_READ_FAST |
			SNOR_HWCAPS_PP,
	};
	u32 ctrl, property;
	u16 mode = 0;
	int ret;

	if (!of_property_read_u32(np, "spi-rx-bus-width", &property)) {
		switch (property) {
		case 1:
			break;
		case 2:
			mode |= SPI_RX_DUAL;
			break;
		case 4:
			mode |= SPI_RX_QUAD;
			break;
		default:
			dev_err(spifi->dev, "unsupported rx-bus-width\n");
			return -EINVAL;
		}
	}

	if (of_property_read_bool(np, "spi-cpha"))
		mode |= SPI_CPHA;

	if (of_property_read_bool(np, "spi-cpol"))
		mode |= SPI_CPOL;

	/* Setup control register defaults */
	ctrl = SPIFI_CTRL_TIMEOUT(1000) |
	       SPIFI_CTRL_CSHIGH(15) |
	       SPIFI_CTRL_FBCLK;

	if (mode & SPI_RX_DUAL) {
		ctrl |= SPIFI_CTRL_DUAL;
		hwcaps.mask |= SNOR_HWCAPS_READ_1_1_2;
	} else if (mode & SPI_RX_QUAD) {
		ctrl &= ~SPIFI_CTRL_DUAL;
		hwcaps.mask |= SNOR_HWCAPS_READ_1_1_4;
	} else {
		ctrl |= SPIFI_CTRL_DUAL;
	}

	switch (mode & SPI_MODE_X_MASK) {
	case SPI_MODE_0:
		ctrl &= ~SPIFI_CTRL_MODE3;
		break;
	case SPI_MODE_3:
		ctrl |= SPIFI_CTRL_MODE3;
		break;
	default:
		dev_err(spifi->dev, "only mode 0 and 3 supported\n");
		return -EINVAL;
	}

	writel(ctrl, spifi->io_base + SPIFI_CTRL);

	spifi->nor.dev   = spifi->dev;
	spi_nor_set_flash_node(&spifi->nor, np);
	spifi->nor.priv  = spifi;
	spifi->nor.controller_ops = &nxp_spifi_controller_ops;

	/*
	 * The first read on a hard reset isn't reliable so do a
	 * dummy read of the id before calling spi_nor_scan().
	 * The reason for this problem is unknown.
	 *
	 * The official NXP spifilib uses more or less the same
	 * workaround that is applied here by reading the device
	 * id multiple times.
	 */
	nxp_spifi_dummy_id_read(&spifi->nor);

	ret = spi_nor_scan(&spifi->nor, NULL, &hwcaps);
	if (ret) {
		dev_err(spifi->dev, "device scan failed\n");
		return ret;
	}

	ret = nxp_spifi_setup_memory_cmd(spifi);
	if (ret) {
		dev_err(spifi->dev, "memory command setup failed\n");
		return ret;
	}

	ret = mtd_device_register(&spifi->nor.mtd, NULL, 0);
	if (ret) {
		dev_err(spifi->dev, "mtd device parse failed\n");
		return ret;
	}

	return 0;
}

static int nxp_spifi_probe(struct platform_device *pdev)
{
	struct device_node *flash_np;
	struct nxp_spifi *spifi;
	int ret;

	spifi = devm_kzalloc(&pdev->dev, sizeof(*spifi), GFP_KERNEL);
	if (!spifi)
		return -ENOMEM;

	spifi->io_base = devm_platform_ioremap_resource_byname(pdev, "spifi");
	if (IS_ERR(spifi->io_base))
		return PTR_ERR(spifi->io_base);

	spifi->flash_base = devm_platform_ioremap_resource_byname(pdev, "flash");
	if (IS_ERR(spifi->flash_base))
		return PTR_ERR(spifi->flash_base);

	spifi->clk_spifi = devm_clk_get(&pdev->dev, "spifi");
	if (IS_ERR(spifi->clk_spifi)) {
		dev_err(&pdev->dev, "spifi clock not found\n");
		return PTR_ERR(spifi->clk_spifi);
	}

	spifi->clk_reg = devm_clk_get(&pdev->dev, "reg");
	if (IS_ERR(spifi->clk_reg)) {
		dev_err(&pdev->dev, "reg clock not found\n");
		return PTR_ERR(spifi->clk_reg);
	}

	ret = clk_prepare_enable(spifi->clk_reg);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable reg clock\n");
		return ret;
	}

	ret = clk_prepare_enable(spifi->clk_spifi);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable spifi clock\n");
		goto dis_clk_reg;
	}

	spifi->dev = &pdev->dev;
	platform_set_drvdata(pdev, spifi);

	/* Initialize and reset device */
	nxp_spifi_reset(spifi);
	writel(0, spifi->io_base + SPIFI_IDATA);
	writel(0, spifi->io_base + SPIFI_MCMD);
	nxp_spifi_reset(spifi);

	flash_np = of_get_next_available_child(pdev->dev.of_node, NULL);
	if (!flash_np) {
		dev_err(&pdev->dev, "no SPI flash device to configure\n");
		ret = -ENODEV;
		goto dis_clks;
	}

	ret = nxp_spifi_setup_flash(spifi, flash_np);
	of_node_put(flash_np);
	if (ret) {
		dev_err(&pdev->dev, "unable to setup flash chip\n");
		goto dis_clks;
	}

	return 0;

dis_clks:
	clk_disable_unprepare(spifi->clk_spifi);
dis_clk_reg:
	clk_disable_unprepare(spifi->clk_reg);
	return ret;
}

static int nxp_spifi_remove(struct platform_device *pdev)
{
	struct nxp_spifi *spifi = platform_get_drvdata(pdev);

	mtd_device_unregister(&spifi->nor.mtd);
	clk_disable_unprepare(spifi->clk_spifi);
	clk_disable_unprepare(spifi->clk_reg);

	return 0;
}

static const struct of_device_id nxp_spifi_match[] = {
	{.compatible = "nxp,lpc1773-spifi"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nxp_spifi_match);

static struct platform_driver nxp_spifi_driver = {
	.probe	= nxp_spifi_probe,
	.remove	= nxp_spifi_remove,
	.driver	= {
		.name = "nxp-spifi",
		.of_match_table = nxp_spifi_match,
	},
};
module_platform_driver(nxp_spifi_driver);

MODULE_DESCRIPTION("NXP SPI Flash Interface driver");
MODULE_AUTHOR("Joachim Eastwood <manabian@gmail.com>");
MODULE_LICENSE("GPL v2");
