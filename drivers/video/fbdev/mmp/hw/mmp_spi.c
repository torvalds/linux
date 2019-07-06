// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/drivers/video/mmp/hw/mmp_spi.c
 * using the spi in LCD controler for commands send
 *
 * Copyright (C) 2012 Marvell Technology Group Ltd.
 * Authors:  Guoqing Li <ligq@marvell.com>
 *          Lisa Du <cldu@marvell.com>
 *          Zhou Zhu <zzhu3@marvell.com>
 */
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include "mmp_ctrl.h"

/**
 * spi_write - write command to the SPI port
 * @data: can be 8/16/32-bit, MSB justified data to write.
 * @len:  data length.
 *
 * Wait bus transfer complete IRQ.
 * The caller is expected to perform the necessary locking.
 *
 * Returns:
 *   %-ETIMEDOUT	timeout occurred
 *   0			success
 */
static inline int lcd_spi_write(struct spi_device *spi, u32 data)
{
	int timeout = 100000, isr, ret = 0;
	u32 tmp;
	void *reg_base =
		*(void **)spi_master_get_devdata(spi->master);

	/* clear ISR */
	writel_relaxed(~SPI_IRQ_MASK, reg_base + SPU_IRQ_ISR);

	switch (spi->bits_per_word) {
	case 8:
		writel_relaxed((u8)data, reg_base + LCD_SPU_SPI_TXDATA);
		break;
	case 16:
		writel_relaxed((u16)data, reg_base + LCD_SPU_SPI_TXDATA);
		break;
	case 32:
		writel_relaxed((u32)data, reg_base + LCD_SPU_SPI_TXDATA);
		break;
	default:
		dev_err(&spi->dev, "Wrong spi bit length\n");
	}

	/* SPI start to send command */
	tmp = readl_relaxed(reg_base + LCD_SPU_SPI_CTRL);
	tmp &= ~CFG_SPI_START_MASK;
	tmp |= CFG_SPI_START(1);
	writel(tmp, reg_base + LCD_SPU_SPI_CTRL);

	isr = readl_relaxed(reg_base + SPU_IRQ_ISR);
	while (!(isr & SPI_IRQ_ENA_MASK)) {
		udelay(100);
		isr = readl_relaxed(reg_base + SPU_IRQ_ISR);
		if (!--timeout) {
			ret = -ETIMEDOUT;
			dev_err(&spi->dev, "spi cmd send time out\n");
			break;
		}
	}

	tmp = readl_relaxed(reg_base + LCD_SPU_SPI_CTRL);
	tmp &= ~CFG_SPI_START_MASK;
	tmp |= CFG_SPI_START(0);
	writel_relaxed(tmp, reg_base + LCD_SPU_SPI_CTRL);

	writel_relaxed(~SPI_IRQ_MASK, reg_base + SPU_IRQ_ISR);

	return ret;
}

static int lcd_spi_setup(struct spi_device *spi)
{
	void *reg_base =
		*(void **)spi_master_get_devdata(spi->master);
	u32 tmp;

	tmp = CFG_SCLKCNT(16) |
		CFG_TXBITS(spi->bits_per_word) |
		CFG_SPI_SEL(1) | CFG_SPI_ENA(1) |
		CFG_SPI_3W4WB(1);
	writel(tmp, reg_base + LCD_SPU_SPI_CTRL);

	/*
	 * After set mode it need a time to pull up the spi singals,
	 * or it would cause the wrong waveform when send spi command,
	 * especially on pxa910h
	 */
	tmp = readl_relaxed(reg_base + SPU_IOPAD_CONTROL);
	if ((tmp & CFG_IOPADMODE_MASK) != IOPAD_DUMB18SPI)
		writel_relaxed(IOPAD_DUMB18SPI |
			(tmp & ~CFG_IOPADMODE_MASK),
			reg_base + SPU_IOPAD_CONTROL);
	udelay(20);
	return 0;
}

static int lcd_spi_one_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct spi_transfer *t;
	int i;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		switch (spi->bits_per_word) {
		case 8:
			for (i = 0; i < t->len; i++)
				lcd_spi_write(spi, ((u8 *)t->tx_buf)[i]);
			break;
		case 16:
			for (i = 0; i < t->len/2; i++)
				lcd_spi_write(spi, ((u16 *)t->tx_buf)[i]);
			break;
		case 32:
			for (i = 0; i < t->len/4; i++)
				lcd_spi_write(spi, ((u32 *)t->tx_buf)[i]);
			break;
		default:
			dev_err(&spi->dev, "Wrong spi bit length\n");
		}
	}

	m->status = 0;
	if (m->complete)
		m->complete(m->context);
	return 0;
}

int lcd_spi_register(struct mmphw_ctrl *ctrl)
{
	struct spi_master *master;
	void **p_regbase;
	int err;

	master = spi_alloc_master(ctrl->dev, sizeof(void *));
	if (!master) {
		dev_err(ctrl->dev, "unable to allocate SPI master\n");
		return -ENOMEM;
	}
	p_regbase = spi_master_get_devdata(master);
	*p_regbase = ctrl->reg_base;

	/* set bus num to 5 to avoid conflict with other spi hosts */
	master->bus_num = 5;
	master->num_chipselect = 1;
	master->setup = lcd_spi_setup;
	master->transfer = lcd_spi_one_transfer;

	err = spi_register_master(master);
	if (err < 0) {
		dev_err(ctrl->dev, "unable to register SPI master\n");
		spi_master_put(master);
		return err;
	}

	dev_info(&master->dev, "registered\n");

	return 0;
}
