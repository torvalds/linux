// SPDX-License-Identifier: GPL-2.0=or-later
/* Copyright (C) 2022 Hewlett-Packard Development Company, L.P. */

#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#define GXP_SPI0_MAX_CHIPSELECT	2
#define GXP_SPI_SLEEP_TIME	1
#define GXP_SPI_TIMEOUT (130 * 1000000 / GXP_SPI_SLEEP_TIME)

#define MANUAL_MODE		0
#define DIRECT_MODE		1
#define SPILDAT_LEN		256

#define OFFSET_SPIMCFG		0x0
#define OFFSET_SPIMCTRL		0x4
#define OFFSET_SPICMD		0x5
#define OFFSET_SPIDCNT		0x6
#define OFFSET_SPIADDR		0x8
#define OFFSET_SPIINTSTS	0xc

#define SPIMCTRL_START		0x01
#define SPIMCTRL_BUSY		0x02
#define SPIMCTRL_DIR		0x08

struct gxp_spi;

struct gxp_spi_chip {
	struct gxp_spi *spifi;
	u32 cs;
};

struct gxp_spi_data {
	u32 max_cs;
	u32 mode_bits;
};

struct gxp_spi {
	const struct gxp_spi_data *data;
	void __iomem *reg_base;
	void __iomem *dat_base;
	void __iomem *dir_base;
	struct device *dev;
	struct gxp_spi_chip chips[GXP_SPI0_MAX_CHIPSELECT];
};

static void gxp_spi_set_mode(struct gxp_spi *spifi, int mode)
{
	u8 value;
	void __iomem *reg_base = spifi->reg_base;

	value = readb(reg_base + OFFSET_SPIMCTRL);

	if (mode == MANUAL_MODE) {
		writeb(0x55, reg_base + OFFSET_SPICMD);
		writeb(0xaa, reg_base + OFFSET_SPICMD);
		value &= ~0x30;
	} else {
		value |= 0x30;
	}
	writeb(value, reg_base + OFFSET_SPIMCTRL);
}

static int gxp_spi_read_reg(struct gxp_spi_chip *chip, const struct spi_mem_op *op)
{
	int ret;
	struct gxp_spi *spifi = chip->spifi;
	void __iomem *reg_base = spifi->reg_base;
	u32 value;

	value = readl(reg_base + OFFSET_SPIMCFG);
	value &= ~(1 << 24);
	value |= (chip->cs << 24);
	value &= ~(0x07 << 16);
	value &= ~(0x1f << 19);
	writel(value, reg_base + OFFSET_SPIMCFG);

	writel(0, reg_base + OFFSET_SPIADDR);

	writeb(op->cmd.opcode, reg_base + OFFSET_SPICMD);

	writew(op->data.nbytes, reg_base + OFFSET_SPIDCNT);

	value = readb(reg_base + OFFSET_SPIMCTRL);
	value &= ~SPIMCTRL_DIR;
	value |= SPIMCTRL_START;

	writeb(value, reg_base + OFFSET_SPIMCTRL);

	ret = readb_poll_timeout(reg_base + OFFSET_SPIMCTRL, value,
				 !(value & SPIMCTRL_BUSY),
				 GXP_SPI_SLEEP_TIME, GXP_SPI_TIMEOUT);
	if (ret) {
		dev_warn(spifi->dev, "read reg busy time out\n");
		return ret;
	}

	memcpy_fromio(op->data.buf.in, spifi->dat_base, op->data.nbytes);
	return ret;
}

static int gxp_spi_write_reg(struct gxp_spi_chip *chip, const struct spi_mem_op *op)
{
	int ret;
	struct gxp_spi *spifi = chip->spifi;
	void __iomem *reg_base = spifi->reg_base;
	u32 value;

	value = readl(reg_base + OFFSET_SPIMCFG);
	value &= ~(1 << 24);
	value |= (chip->cs << 24);
	value &= ~(0x07 << 16);
	value &= ~(0x1f << 19);
	writel(value, reg_base + OFFSET_SPIMCFG);

	writel(0, reg_base + OFFSET_SPIADDR);

	writeb(op->cmd.opcode, reg_base + OFFSET_SPICMD);

	memcpy_toio(spifi->dat_base, op->data.buf.in, op->data.nbytes);

	writew(op->data.nbytes, reg_base + OFFSET_SPIDCNT);

	value = readb(reg_base + OFFSET_SPIMCTRL);
	value |= SPIMCTRL_DIR;
	value |= SPIMCTRL_START;

	writeb(value, reg_base + OFFSET_SPIMCTRL);

	ret = readb_poll_timeout(reg_base + OFFSET_SPIMCTRL, value,
				 !(value & SPIMCTRL_BUSY),
				 GXP_SPI_SLEEP_TIME, GXP_SPI_TIMEOUT);
	if (ret)
		dev_warn(spifi->dev, "write reg busy time out\n");

	return ret;
}

static ssize_t gxp_spi_read(struct gxp_spi_chip *chip, const struct spi_mem_op *op)
{
	struct gxp_spi *spifi = chip->spifi;
	u32 offset = op->addr.val;

	if (chip->cs == 0)
		offset += 0x4000000;

	memcpy_fromio(op->data.buf.in, spifi->dir_base + offset, op->data.nbytes);

	return 0;
}

static ssize_t gxp_spi_write(struct gxp_spi_chip *chip, const struct spi_mem_op *op)
{
	struct gxp_spi *spifi = chip->spifi;
	void __iomem *reg_base = spifi->reg_base;
	u32 write_len;
	u32 value;
	int ret;

	write_len = op->data.nbytes;
	if (write_len > SPILDAT_LEN)
		write_len = SPILDAT_LEN;

	value = readl(reg_base + OFFSET_SPIMCFG);
	value &= ~(1 << 24);
	value |= (chip->cs << 24);
	value &= ~(0x07 << 16);
	value |= (op->addr.nbytes << 16);
	value &= ~(0x1f << 19);
	writel(value, reg_base + OFFSET_SPIMCFG);

	writel(op->addr.val, reg_base + OFFSET_SPIADDR);

	writeb(op->cmd.opcode, reg_base + OFFSET_SPICMD);

	writew(write_len, reg_base + OFFSET_SPIDCNT);

	memcpy_toio(spifi->dat_base, op->data.buf.in, write_len);

	value = readb(reg_base + OFFSET_SPIMCTRL);
	value |= SPIMCTRL_DIR;
	value |= SPIMCTRL_START;

	writeb(value, reg_base + OFFSET_SPIMCTRL);

	ret = readb_poll_timeout(reg_base + OFFSET_SPIMCTRL, value,
				 !(value & SPIMCTRL_BUSY),
				 GXP_SPI_SLEEP_TIME, GXP_SPI_TIMEOUT);
	if (ret) {
		dev_warn(spifi->dev, "write busy time out\n");
		return ret;
	}

	return write_len;
}

static int do_gxp_exec_mem_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct gxp_spi *spifi = spi_controller_get_devdata(mem->spi->master);
	struct gxp_spi_chip *chip = &spifi->chips[mem->spi->chip_select];
	int ret;

	if (op->data.dir == SPI_MEM_DATA_IN) {
		if (!op->addr.nbytes)
			ret = gxp_spi_read_reg(chip, op);
		else
			ret = gxp_spi_read(chip, op);
	} else {
		if (!op->addr.nbytes)
			ret = gxp_spi_write_reg(chip, op);
		else
			ret = gxp_spi_write(chip, op);
	}

	return ret;
}

static int gxp_exec_mem_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	int ret;

	ret = do_gxp_exec_mem_op(mem, op);
	if (ret)
		dev_err(&mem->spi->dev, "operation failed: %d", ret);

	return ret;
}

static const struct spi_controller_mem_ops gxp_spi_mem_ops = {
	.exec_op = gxp_exec_mem_op,
};

static int gxp_spi_setup(struct spi_device *spi)
{
	struct gxp_spi *spifi = spi_controller_get_devdata(spi->master);
	unsigned int cs = spi->chip_select;
	struct gxp_spi_chip *chip = &spifi->chips[cs];

	chip->spifi = spifi;
	chip->cs = cs;

	gxp_spi_set_mode(spifi, MANUAL_MODE);

	return 0;
}

static int gxp_spifi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct gxp_spi_data *data;
	struct spi_controller *ctlr;
	struct gxp_spi *spifi;
	struct resource *res;
	int ret;

	data = of_device_get_match_data(&pdev->dev);

	ctlr = devm_spi_alloc_master(dev, sizeof(*spifi));
	if (!ctlr)
		return -ENOMEM;

	spifi = spi_controller_get_devdata(ctlr);

	platform_set_drvdata(pdev, spifi);
	spifi->data = data;
	spifi->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spifi->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spifi->reg_base))
		return PTR_ERR(spifi->reg_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	spifi->dat_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spifi->dat_base))
		return PTR_ERR(spifi->dat_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	spifi->dir_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spifi->dir_base))
		return PTR_ERR(spifi->dir_base);

	ctlr->mode_bits = data->mode_bits;
	ctlr->bus_num = pdev->id;
	ctlr->mem_ops = &gxp_spi_mem_ops;
	ctlr->setup = gxp_spi_setup;
	ctlr->num_chipselect = data->max_cs;
	ctlr->dev.of_node = dev->of_node;

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register spi controller\n");
	}

	return 0;
}

static const struct gxp_spi_data gxp_spifi_data = {
	.max_cs	= 2,
	.mode_bits = 0,
};

static const struct of_device_id gxp_spifi_match[] = {
	{.compatible = "hpe,gxp-spifi", .data = &gxp_spifi_data },
	{ /* null */ }
};
MODULE_DEVICE_TABLE(of, gxp_spifi_match);

static struct platform_driver gxp_spifi_driver = {
	.probe = gxp_spifi_probe,
	.driver = {
		.name = "gxp-spifi",
		.of_match_table = gxp_spifi_match,
	},
};
module_platform_driver(gxp_spifi_driver);

MODULE_DESCRIPTION("HPE GXP SPI Flash Interface driver");
MODULE_AUTHOR("Nick Hawkins <nick.hawkins@hpe.com>");
MODULE_LICENSE("GPL");
