// SPDX-License-Identifier: GPL-2.0+
// Loongson SPI Support
// Copyright (C) 2023 Loongson Technology Corporation Limited

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include "spi-loongson.h"

static inline void loongson_spi_write_reg(struct loongson_spi *spi, unsigned char reg,
					  unsigned char data)
{
	writeb(data, spi->base + reg);
}

static inline char loongson_spi_read_reg(struct loongson_spi *spi, unsigned char reg)
{
	return readb(spi->base + reg);
}

static void loongson_spi_set_cs(struct spi_device *spi, bool en)
{
	int cs;
	unsigned char mask = (BIT(4) | BIT(0)) << spi_get_chipselect(spi, 0);
	unsigned char val = en ? mask :  (BIT(0) << spi_get_chipselect(spi, 0));
	struct loongson_spi *loongson_spi = spi_controller_get_devdata(spi->controller);

	cs = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_SFCS_REG) & ~mask;
	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_SFCS_REG, val | cs);
}

static void loongson_spi_set_clk(struct loongson_spi *loongson_spi, unsigned int hz)
{
	unsigned char val;
	unsigned int div, div_tmp;
	static const char rdiv[12] = {0, 1, 4, 2, 3, 5, 6, 7, 8, 9, 10, 11};

	div = clamp_val(DIV_ROUND_UP_ULL(loongson_spi->clk_rate, hz), 2, 4096);
	div_tmp = rdiv[fls(div - 1)];
	loongson_spi->spcr = (div_tmp & GENMASK(1, 0)) >> 0;
	loongson_spi->sper = (div_tmp & GENMASK(3, 2)) >> 2;
	val = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_SPCR_REG);
	val &= ~GENMASK(1, 0);
	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_SPCR_REG, val |
			       loongson_spi->spcr);
	val = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_SPER_REG);
	val &= ~GENMASK(1, 0);
	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_SPER_REG, val |
			       loongson_spi->sper);
	loongson_spi->hz = hz;
}

static void loongson_spi_set_mode(struct loongson_spi *loongson_spi,
				  struct spi_device *spi)
{
	unsigned char val;

	val = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_SPCR_REG);
	val &= ~(LOONGSON_SPI_SPCR_CPOL | LOONGSON_SPI_SPCR_CPHA);
	if (spi->mode & SPI_CPOL)
		val |= LOONGSON_SPI_SPCR_CPOL;
	if (spi->mode & SPI_CPHA)
		val |= LOONGSON_SPI_SPCR_CPHA;

	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_SPCR_REG, val);
	loongson_spi->mode |= spi->mode;
}

static int loongson_spi_update_state(struct loongson_spi *loongson_spi,
				struct spi_device *spi, struct spi_transfer *t)
{
	if (t && loongson_spi->hz != t->speed_hz)
		loongson_spi_set_clk(loongson_spi, t->speed_hz);

	if ((spi->mode ^ loongson_spi->mode) & SPI_MODE_X_MASK)
		loongson_spi_set_mode(loongson_spi, spi);

	return 0;
}

static int loongson_spi_setup(struct spi_device *spi)
{
	struct loongson_spi *loongson_spi;

	loongson_spi = spi_controller_get_devdata(spi->controller);
	if (spi->bits_per_word % 8)
		return -EINVAL;

	if (spi_get_chipselect(spi, 0) >= spi->controller->num_chipselect)
		return -EINVAL;

	loongson_spi->hz = 0;
	loongson_spi_set_cs(spi, true);

	return 0;
}

static int loongson_spi_write_read_8bit(struct spi_device *spi, const u8 **tx_buf,
					u8 **rx_buf, unsigned int num)
{
	int ret;
	struct loongson_spi *loongson_spi = spi_controller_get_devdata(spi->controller);

	if (tx_buf && *tx_buf)
		loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_FIFO_REG, *((*tx_buf)++));
	else
		loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_FIFO_REG, 0);

	ret = readb_poll_timeout(loongson_spi->base + LOONGSON_SPI_SPSR_REG,
				 loongson_spi->spsr, (loongson_spi->spsr &
				 LOONGSON_SPI_SPSR_RFEMPTY) != LOONGSON_SPI_SPSR_RFEMPTY,
				 1, USEC_PER_MSEC);

	if (rx_buf && *rx_buf)
		*(*rx_buf)++ = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_FIFO_REG);
	else
		loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_FIFO_REG);

	return ret;
}

static int loongson_spi_write_read(struct spi_device *spi, struct spi_transfer *xfer)
{
	int ret;
	unsigned int count;
	const u8 *tx = xfer->tx_buf;
	u8 *rx = xfer->rx_buf;

	count = xfer->len;
	do {
		ret = loongson_spi_write_read_8bit(spi, &tx, &rx, count);
		if (ret)
			break;
	} while (--count);

	return ret;
}

static int loongson_spi_prepare_message(struct spi_controller *ctlr, struct spi_message *m)
{
	struct loongson_spi *loongson_spi = spi_controller_get_devdata(ctlr);

	loongson_spi->para = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_PARA_REG);
	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_PARA_REG, loongson_spi->para &
			       ~LOONGSON_SPI_PARA_MEM_EN);

	return 0;
}

static int loongson_spi_transfer_one(struct spi_controller *ctrl, struct spi_device *spi,
				     struct spi_transfer *xfer)
{
	struct loongson_spi *loongson_spi = spi_controller_get_devdata(spi->controller);

	loongson_spi_update_state(loongson_spi, spi, xfer);
	if (xfer->len)
		return loongson_spi_write_read(spi, xfer);

	return 0;
}

static int loongson_spi_unprepare_message(struct spi_controller *ctrl, struct spi_message *m)
{
	struct loongson_spi *loongson_spi = spi_controller_get_devdata(ctrl);

	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_PARA_REG, loongson_spi->para);

	return 0;
}

static void loongson_spi_reginit(struct loongson_spi *loongson_spi_dev)
{
	unsigned char val;

	val = loongson_spi_read_reg(loongson_spi_dev, LOONGSON_SPI_SPCR_REG);
	val &= ~LOONGSON_SPI_SPCR_SPE;
	loongson_spi_write_reg(loongson_spi_dev, LOONGSON_SPI_SPCR_REG, val);

	loongson_spi_write_reg(loongson_spi_dev, LOONGSON_SPI_SPSR_REG,
			       (LOONGSON_SPI_SPSR_SPIF | LOONGSON_SPI_SPSR_WCOL));

	val = loongson_spi_read_reg(loongson_spi_dev, LOONGSON_SPI_SPCR_REG);
	val |= LOONGSON_SPI_SPCR_SPE;
	loongson_spi_write_reg(loongson_spi_dev, LOONGSON_SPI_SPCR_REG, val);
}

int loongson_spi_init_controller(struct device *dev, void __iomem *regs)
{
	struct spi_controller *controller;
	struct loongson_spi *spi;
	struct clk *clk;

	controller = devm_spi_alloc_host(dev, sizeof(struct loongson_spi));
	if (controller == NULL)
		return -ENOMEM;

	controller->mode_bits = SPI_MODE_X_MASK | SPI_CS_HIGH;
	controller->setup = loongson_spi_setup;
	controller->prepare_message = loongson_spi_prepare_message;
	controller->transfer_one = loongson_spi_transfer_one;
	controller->unprepare_message = loongson_spi_unprepare_message;
	controller->set_cs = loongson_spi_set_cs;
	controller->num_chipselect = 4;
	device_set_node(&controller->dev, dev_fwnode(dev));
	dev_set_drvdata(dev, controller);

	spi = spi_controller_get_devdata(controller);
	spi->base = regs;
	spi->controller = controller;

	clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "unable to get clock\n");

	spi->clk_rate = clk_get_rate(clk);
	loongson_spi_reginit(spi);

	spi->mode = 0;

	return devm_spi_register_controller(dev, controller);
}
EXPORT_SYMBOL_NS_GPL(loongson_spi_init_controller, "SPI_LOONGSON_CORE");

static int __maybe_unused loongson_spi_suspend(struct device *dev)
{
	struct loongson_spi *loongson_spi;
	struct spi_controller *controller;

	controller = dev_get_drvdata(dev);
	spi_controller_suspend(controller);

	loongson_spi = spi_controller_get_devdata(controller);

	loongson_spi->spcr = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_SPCR_REG);
	loongson_spi->sper = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_SPER_REG);
	loongson_spi->spsr = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_SPSR_REG);
	loongson_spi->para = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_PARA_REG);
	loongson_spi->sfcs = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_SFCS_REG);
	loongson_spi->timi = loongson_spi_read_reg(loongson_spi, LOONGSON_SPI_TIMI_REG);

	return 0;
}

static int __maybe_unused loongson_spi_resume(struct device *dev)
{
	struct loongson_spi *loongson_spi;
	struct spi_controller *controller;

	controller = dev_get_drvdata(dev);
	loongson_spi = spi_controller_get_devdata(controller);

	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_SPCR_REG, loongson_spi->spcr);
	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_SPER_REG, loongson_spi->sper);
	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_SPSR_REG, loongson_spi->spsr);
	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_PARA_REG, loongson_spi->para);
	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_SFCS_REG, loongson_spi->sfcs);
	loongson_spi_write_reg(loongson_spi, LOONGSON_SPI_TIMI_REG, loongson_spi->timi);

	spi_controller_resume(controller);

	return 0;
}

const struct dev_pm_ops loongson_spi_dev_pm_ops = {
	.suspend = loongson_spi_suspend,
	.resume = loongson_spi_resume,
};
EXPORT_SYMBOL_NS_GPL(loongson_spi_dev_pm_ops, "SPI_LOONGSON_CORE");

MODULE_DESCRIPTION("Loongson SPI core driver");
MODULE_LICENSE("GPL");
