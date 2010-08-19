/*
 * Copyright (C) 2009 Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/slab.h>

#include <mach/spi.h>
#include <mach/edma.h>

#define SPI_NO_RESOURCE		((resource_size_t)-1)

#define SPI_MAX_CHIPSELECT	2

#define CS_DEFAULT	0xFF

#define SPI_BUFSIZ	(SMP_CACHE_BYTES + 1)
#define DAVINCI_DMA_DATA_TYPE_S8	0x01
#define DAVINCI_DMA_DATA_TYPE_S16	0x02
#define DAVINCI_DMA_DATA_TYPE_S32	0x04

#define SPIFMT_PHASE_MASK	BIT(16)
#define SPIFMT_POLARITY_MASK	BIT(17)
#define SPIFMT_DISTIMER_MASK	BIT(18)
#define SPIFMT_SHIFTDIR_MASK	BIT(20)
#define SPIFMT_WAITENA_MASK	BIT(21)
#define SPIFMT_PARITYENA_MASK	BIT(22)
#define SPIFMT_ODD_PARITY_MASK	BIT(23)
#define SPIFMT_WDELAY_MASK	0x3f000000u
#define SPIFMT_WDELAY_SHIFT	24
#define SPIFMT_PRESCALE_SHIFT	8


/* SPIPC0 */
#define SPIPC0_DIFUN_MASK	BIT(11)		/* MISO */
#define SPIPC0_DOFUN_MASK	BIT(10)		/* MOSI */
#define SPIPC0_CLKFUN_MASK	BIT(9)		/* CLK */
#define SPIPC0_SPIENA_MASK	BIT(8)		/* nREADY */

#define SPIINT_MASKALL		0x0101035F
#define SPI_INTLVL_1		0x000001FFu
#define SPI_INTLVL_0		0x00000000u

/* SPIDAT1 (upper 16 bit defines) */
#define SPIDAT1_CSHOLD_MASK	BIT(12)

/* SPIGCR1 */
#define SPIGCR1_CLKMOD_MASK	BIT(1)
#define SPIGCR1_MASTER_MASK     BIT(0)
#define SPIGCR1_LOOPBACK_MASK	BIT(16)
#define SPIGCR1_SPIENA_MASK	BIT(24)

/* SPIBUF */
#define SPIBUF_TXFULL_MASK	BIT(29)
#define SPIBUF_RXEMPTY_MASK	BIT(31)

/* Error Masks */
#define SPIFLG_DLEN_ERR_MASK		BIT(0)
#define SPIFLG_TIMEOUT_MASK		BIT(1)
#define SPIFLG_PARERR_MASK		BIT(2)
#define SPIFLG_DESYNC_MASK		BIT(3)
#define SPIFLG_BITERR_MASK		BIT(4)
#define SPIFLG_OVRRUN_MASK		BIT(6)
#define SPIFLG_RX_INTR_MASK		BIT(8)
#define SPIFLG_TX_INTR_MASK		BIT(9)
#define SPIFLG_BUF_INIT_ACTIVE_MASK	BIT(24)

#define SPIINT_BITERR_INTR	BIT(4)
#define SPIINT_OVRRUN_INTR	BIT(6)
#define SPIINT_RX_INTR		BIT(8)
#define SPIINT_TX_INTR		BIT(9)
#define SPIINT_DMA_REQ_EN	BIT(16)

#define SPI_T2CDELAY_SHIFT	16
#define SPI_C2TDELAY_SHIFT	24

/* SPI Controller registers */
#define SPIGCR0		0x00
#define SPIGCR1		0x04
#define SPIINT		0x08
#define SPILVL		0x0c
#define SPIFLG		0x10
#define SPIPC0		0x14
#define SPIDAT1		0x3c
#define SPIBUF		0x40
#define SPIDELAY	0x48
#define SPIDEF		0x4c
#define SPIFMT0		0x50

struct davinci_spi_slave {
	u32	cmd_to_write;
	u32	clk_ctrl_to_write;
	u32	bytes_per_word;
	u8	active_cs;
};

/* We have 2 DMA channels per CS, one for RX and one for TX */
struct davinci_spi_dma {
	int			dma_tx_channel;
	int			dma_rx_channel;
	int			dma_tx_sync_dev;
	int			dma_rx_sync_dev;
	enum dma_event_q	eventq;

	struct completion	dma_tx_completion;
	struct completion	dma_rx_completion;
};

/* SPI Controller driver's private data. */
struct davinci_spi {
	struct spi_bitbang	bitbang;
	struct clk		*clk;

	u8			version;
	resource_size_t		pbase;
	void __iomem		*base;
	size_t			region_size;
	u32			irq;
	struct completion	done;

	const void		*tx;
	void			*rx;
	u8			*tmp_buf;
	int			count;
	struct davinci_spi_dma	*dma_channels;
	struct davinci_spi_platform_data *pdata;

	void			(*get_rx)(u32 rx_data, struct davinci_spi *);
	u32			(*get_tx)(struct davinci_spi *);

	struct davinci_spi_slave slave[SPI_MAX_CHIPSELECT];
};

static struct davinci_spi_config davinci_spi_default_cfg;

static unsigned use_dma;

static void davinci_spi_rx_buf_u8(u32 data, struct davinci_spi *davinci_spi)
{
	u8 *rx = davinci_spi->rx;

	*rx++ = (u8)data;
	davinci_spi->rx = rx;
}

static void davinci_spi_rx_buf_u16(u32 data, struct davinci_spi *davinci_spi)
{
	u16 *rx = davinci_spi->rx;

	*rx++ = (u16)data;
	davinci_spi->rx = rx;
}

static u32 davinci_spi_tx_buf_u8(struct davinci_spi *davinci_spi)
{
	u32 data;
	const u8 *tx = davinci_spi->tx;

	data = *tx++;
	davinci_spi->tx = tx;
	return data;
}

static u32 davinci_spi_tx_buf_u16(struct davinci_spi *davinci_spi)
{
	u32 data;
	const u16 *tx = davinci_spi->tx;

	data = *tx++;
	davinci_spi->tx = tx;
	return data;
}

static inline void set_io_bits(void __iomem *addr, u32 bits)
{
	u32 v = ioread32(addr);

	v |= bits;
	iowrite32(v, addr);
}

static inline void clear_io_bits(void __iomem *addr, u32 bits)
{
	u32 v = ioread32(addr);

	v &= ~bits;
	iowrite32(v, addr);
}

static void davinci_spi_set_dma_req(const struct spi_device *spi, int enable)
{
	struct davinci_spi *davinci_spi = spi_master_get_devdata(spi->master);

	if (enable)
		set_io_bits(davinci_spi->base + SPIINT, SPIINT_DMA_REQ_EN);
	else
		clear_io_bits(davinci_spi->base + SPIINT, SPIINT_DMA_REQ_EN);
}

/*
 * Interface to control the chip select signal
 */
static void davinci_spi_chipselect(struct spi_device *spi, int value)
{
	struct davinci_spi *davinci_spi;
	struct davinci_spi_platform_data *pdata;
	u8 chip_sel = spi->chip_select;
	u16 spidat1_cfg = CS_DEFAULT;
	bool gpio_chipsel = false;

	davinci_spi = spi_master_get_devdata(spi->master);
	pdata = davinci_spi->pdata;

	if (pdata->chip_sel && chip_sel < pdata->num_chipselect &&
				pdata->chip_sel[chip_sel] != SPI_INTERN_CS)
		gpio_chipsel = true;

	/*
	 * Board specific chip select logic decides the polarity and cs
	 * line for the controller
	 */
	if (gpio_chipsel) {
		if (value == BITBANG_CS_ACTIVE)
			gpio_set_value(pdata->chip_sel[chip_sel], 0);
		else
			gpio_set_value(pdata->chip_sel[chip_sel], 1);
	} else {
		if (value == BITBANG_CS_ACTIVE) {
			spidat1_cfg |= SPIDAT1_CSHOLD_MASK;
			spidat1_cfg &= ~(0x1 << chip_sel);
		}

		iowrite16(spidat1_cfg, davinci_spi->base + SPIDAT1 + 2);
	}
}

/**
 * davinci_spi_get_prescale - Calculates the correct prescale value
 * @maxspeed_hz: the maximum rate the SPI clock can run at
 *
 * This function calculates the prescale value that generates a clock rate
 * less than or equal to the specified maximum.
 *
 * Returns: calculated prescale - 1 for easy programming into SPI registers
 * or negative error number if valid prescalar cannot be updated.
 */
static inline int davinci_spi_get_prescale(struct davinci_spi *davinci_spi,
							u32 max_speed_hz)
{
	int ret;

	ret = DIV_ROUND_UP(clk_get_rate(davinci_spi->clk), max_speed_hz);

	if (ret < 3 || ret > 256)
		return -EINVAL;

	return ret - 1;
}

/**
 * davinci_spi_setup_transfer - This functions will determine transfer method
 * @spi: spi device on which data transfer to be done
 * @t: spi transfer in which transfer info is filled
 *
 * This function determines data transfer method (8/16/32 bit transfer).
 * It will also set the SPI Clock Control register according to
 * SPI slave device freq.
 */
static int davinci_spi_setup_transfer(struct spi_device *spi,
		struct spi_transfer *t)
{

	struct davinci_spi *davinci_spi;
	struct davinci_spi_config *spicfg;
	u8 bits_per_word = 0;
	u32 hz = 0, spifmt = 0, prescale = 0;

	davinci_spi = spi_master_get_devdata(spi->master);
	spicfg = (struct davinci_spi_config *)spi->controller_data;
	if (!spicfg)
		spicfg = &davinci_spi_default_cfg;

	if (t) {
		bits_per_word = t->bits_per_word;
		hz = t->speed_hz;
	}

	/* if bits_per_word is not set then set it default */
	if (!bits_per_word)
		bits_per_word = spi->bits_per_word;

	/*
	 * Assign function pointer to appropriate transfer method
	 * 8bit, 16bit or 32bit transfer
	 */
	if (bits_per_word <= 8 && bits_per_word >= 2) {
		davinci_spi->get_rx = davinci_spi_rx_buf_u8;
		davinci_spi->get_tx = davinci_spi_tx_buf_u8;
		davinci_spi->slave[spi->chip_select].bytes_per_word = 1;
	} else if (bits_per_word <= 16 && bits_per_word >= 2) {
		davinci_spi->get_rx = davinci_spi_rx_buf_u16;
		davinci_spi->get_tx = davinci_spi_tx_buf_u16;
		davinci_spi->slave[spi->chip_select].bytes_per_word = 2;
	} else
		return -EINVAL;

	if (!hz)
		hz = spi->max_speed_hz;

	/* Set up SPIFMTn register, unique to this chipselect. */

	prescale = davinci_spi_get_prescale(davinci_spi, hz);
	if (prescale < 0)
		return prescale;

	spifmt = (prescale << SPIFMT_PRESCALE_SHIFT) | (bits_per_word & 0x1f);

	if (spi->mode & SPI_LSB_FIRST)
		spifmt |= SPIFMT_SHIFTDIR_MASK;

	if (spi->mode & SPI_CPOL)
		spifmt |= SPIFMT_POLARITY_MASK;

	if (!(spi->mode & SPI_CPHA))
		spifmt |= SPIFMT_PHASE_MASK;

	/*
	 * Version 1 hardware supports two basic SPI modes:
	 *  - Standard SPI mode uses 4 pins, with chipselect
	 *  - 3 pin SPI is a 4 pin variant without CS (SPI_NO_CS)
	 *	(distinct from SPI_3WIRE, with just one data wire;
	 *	or similar variants without MOSI or without MISO)
	 *
	 * Version 2 hardware supports an optional handshaking signal,
	 * so it can support two more modes:
	 *  - 5 pin SPI variant is standard SPI plus SPI_READY
	 *  - 4 pin with enable is (SPI_READY | SPI_NO_CS)
	 */

	if (davinci_spi->version == SPI_VERSION_2) {

		spifmt |= ((spicfg->wdelay << SPIFMT_WDELAY_SHIFT)
							& SPIFMT_WDELAY_MASK);

		if (spicfg->odd_parity)
			spifmt |= SPIFMT_ODD_PARITY_MASK;

		if (spicfg->parity_enable)
			spifmt |= SPIFMT_PARITYENA_MASK;

		if (spicfg->timer_disable)
			spifmt |= SPIFMT_DISTIMER_MASK;
		else
			iowrite32((spicfg->c2tdelay << SPI_C2TDELAY_SHIFT) |
				  (spicfg->t2cdelay << SPI_T2CDELAY_SHIFT),
				  davinci_spi->base + SPIDELAY);

		if (spi->mode & SPI_READY)
			spifmt |= SPIFMT_WAITENA_MASK;
	}

	iowrite32(spifmt, davinci_spi->base + SPIFMT0);

	return 0;
}

static void davinci_spi_dma_rx_callback(unsigned lch, u16 ch_status, void *data)
{
	struct spi_device *spi = (struct spi_device *)data;
	struct davinci_spi *davinci_spi;
	struct davinci_spi_dma *davinci_spi_dma;

	davinci_spi = spi_master_get_devdata(spi->master);
	davinci_spi_dma = &(davinci_spi->dma_channels[spi->chip_select]);

	if (ch_status == DMA_COMPLETE)
		edma_stop(davinci_spi_dma->dma_rx_channel);
	else
		edma_clean_channel(davinci_spi_dma->dma_rx_channel);

	complete(&davinci_spi_dma->dma_rx_completion);
	/* We must disable the DMA RX request */
	davinci_spi_set_dma_req(spi, 0);
}

static void davinci_spi_dma_tx_callback(unsigned lch, u16 ch_status, void *data)
{
	struct spi_device *spi = (struct spi_device *)data;
	struct davinci_spi *davinci_spi;
	struct davinci_spi_dma *davinci_spi_dma;

	davinci_spi = spi_master_get_devdata(spi->master);
	davinci_spi_dma = &(davinci_spi->dma_channels[spi->chip_select]);

	if (ch_status == DMA_COMPLETE)
		edma_stop(davinci_spi_dma->dma_tx_channel);
	else
		edma_clean_channel(davinci_spi_dma->dma_tx_channel);

	complete(&davinci_spi_dma->dma_tx_completion);
	/* We must disable the DMA TX request */
	davinci_spi_set_dma_req(spi, 0);
}

static int davinci_spi_request_dma(struct spi_device *spi)
{
	struct davinci_spi *davinci_spi;
	struct davinci_spi_dma *davinci_spi_dma;
	struct device *sdev;
	int r;

	davinci_spi = spi_master_get_devdata(spi->master);
	davinci_spi_dma = &davinci_spi->dma_channels[spi->chip_select];
	sdev = davinci_spi->bitbang.master->dev.parent;

	r = edma_alloc_channel(davinci_spi_dma->dma_rx_sync_dev,
				davinci_spi_dma_rx_callback, spi,
				davinci_spi_dma->eventq);
	if (r < 0) {
		dev_dbg(sdev, "Unable to request DMA channel for SPI RX\n");
		return -EAGAIN;
	}
	davinci_spi_dma->dma_rx_channel = r;
	r = edma_alloc_channel(davinci_spi_dma->dma_tx_sync_dev,
				davinci_spi_dma_tx_callback, spi,
				davinci_spi_dma->eventq);
	if (r < 0) {
		edma_free_channel(davinci_spi_dma->dma_rx_channel);
		davinci_spi_dma->dma_rx_channel = -1;
		dev_dbg(sdev, "Unable to request DMA channel for SPI TX\n");
		return -EAGAIN;
	}
	davinci_spi_dma->dma_tx_channel = r;

	return 0;
}

/**
 * davinci_spi_setup - This functions will set default transfer method
 * @spi: spi device on which data transfer to be done
 *
 * This functions sets the default transfer method.
 */
static int davinci_spi_setup(struct spi_device *spi)
{
	int retval;
	struct davinci_spi *davinci_spi;
	struct davinci_spi_dma *davinci_spi_dma;

	davinci_spi = spi_master_get_devdata(spi->master);

	/* if bits per word length is zero then set it default 8 */
	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	davinci_spi->slave[spi->chip_select].cmd_to_write = 0;

	if (use_dma && davinci_spi->dma_channels) {
		davinci_spi_dma = &davinci_spi->dma_channels[spi->chip_select];

		if ((davinci_spi_dma->dma_rx_channel == -1)
				|| (davinci_spi_dma->dma_tx_channel == -1)) {
			retval = davinci_spi_request_dma(spi);
			if (retval < 0)
				return retval;
		}
	}

	retval = davinci_spi_setup_transfer(spi, NULL);

	return retval;
}

static void davinci_spi_cleanup(struct spi_device *spi)
{
	struct davinci_spi *davinci_spi = spi_master_get_devdata(spi->master);
	struct davinci_spi_dma *davinci_spi_dma;

	davinci_spi_dma = &davinci_spi->dma_channels[spi->chip_select];

	if (use_dma && davinci_spi->dma_channels) {
		davinci_spi_dma = &davinci_spi->dma_channels[spi->chip_select];

		if ((davinci_spi_dma->dma_rx_channel != -1)
				&& (davinci_spi_dma->dma_tx_channel != -1)) {
			edma_free_channel(davinci_spi_dma->dma_tx_channel);
			edma_free_channel(davinci_spi_dma->dma_rx_channel);
		}
	}
}

static int davinci_spi_bufs_prep(struct spi_device *spi,
				 struct davinci_spi *davinci_spi)
{
	struct davinci_spi_platform_data *pdata;
	int op_mode = 0;

	/*
	 * REVISIT  unless devices disagree about SPI_LOOP or
	 * SPI_READY (SPI_NO_CS only allows one device!), this
	 * should not need to be done before each message...
	 * optimize for both flags staying cleared.
	 */

	op_mode = SPIPC0_DIFUN_MASK
		| SPIPC0_DOFUN_MASK
		| SPIPC0_CLKFUN_MASK;
	if (!(spi->mode & SPI_NO_CS)) {
		pdata = davinci_spi->pdata;
		if (!pdata->chip_sel ||
		     pdata->chip_sel[spi->chip_select] == SPI_INTERN_CS)
			op_mode |= 1 << spi->chip_select;
	}
	if (spi->mode & SPI_READY)
		op_mode |= SPIPC0_SPIENA_MASK;

	iowrite32(op_mode, davinci_spi->base + SPIPC0);

	if (spi->mode & SPI_LOOP)
		set_io_bits(davinci_spi->base + SPIGCR1,
				SPIGCR1_LOOPBACK_MASK);
	else
		clear_io_bits(davinci_spi->base + SPIGCR1,
				SPIGCR1_LOOPBACK_MASK);

	return 0;
}

static int davinci_spi_check_error(struct davinci_spi *davinci_spi,
				   int int_status)
{
	struct device *sdev = davinci_spi->bitbang.master->dev.parent;

	if (int_status & SPIFLG_TIMEOUT_MASK) {
		dev_dbg(sdev, "SPI Time-out Error\n");
		return -ETIMEDOUT;
	}
	if (int_status & SPIFLG_DESYNC_MASK) {
		dev_dbg(sdev, "SPI Desynchronization Error\n");
		return -EIO;
	}
	if (int_status & SPIFLG_BITERR_MASK) {
		dev_dbg(sdev, "SPI Bit error\n");
		return -EIO;
	}

	if (davinci_spi->version == SPI_VERSION_2) {
		if (int_status & SPIFLG_DLEN_ERR_MASK) {
			dev_dbg(sdev, "SPI Data Length Error\n");
			return -EIO;
		}
		if (int_status & SPIFLG_PARERR_MASK) {
			dev_dbg(sdev, "SPI Parity Error\n");
			return -EIO;
		}
		if (int_status & SPIFLG_OVRRUN_MASK) {
			dev_dbg(sdev, "SPI Data Overrun error\n");
			return -EIO;
		}
		if (int_status & SPIFLG_TX_INTR_MASK) {
			dev_dbg(sdev, "SPI TX intr bit set\n");
			return -EIO;
		}
		if (int_status & SPIFLG_BUF_INIT_ACTIVE_MASK) {
			dev_dbg(sdev, "SPI Buffer Init Active\n");
			return -EBUSY;
		}
	}

	return 0;
}

/**
 * davinci_spi_bufs - functions which will handle transfer data
 * @spi: spi device on which data transfer to be done
 * @t: spi transfer in which transfer info is filled
 *
 * This function will put data to be transferred into data register
 * of SPI controller and then wait until the completion will be marked
 * by the IRQ Handler.
 */
static int davinci_spi_bufs_pio(struct spi_device *spi, struct spi_transfer *t)
{
	struct davinci_spi *davinci_spi;
	int int_status, count, ret;
	u8 conv;
	u32 tx_data, data1_reg_val;
	u32 buf_val, flg_val;
	struct davinci_spi_platform_data *pdata;

	davinci_spi = spi_master_get_devdata(spi->master);
	pdata = davinci_spi->pdata;

	davinci_spi->tx = t->tx_buf;
	davinci_spi->rx = t->rx_buf;

	/* convert len to words based on bits_per_word */
	conv = davinci_spi->slave[spi->chip_select].bytes_per_word;
	davinci_spi->count = t->len / conv;

	data1_reg_val = ioread32(davinci_spi->base + SPIDAT1);

	INIT_COMPLETION(davinci_spi->done);

	ret = davinci_spi_bufs_prep(spi, davinci_spi);
	if (ret)
		return ret;

	/* Enable SPI */
	set_io_bits(davinci_spi->base + SPIGCR1, SPIGCR1_SPIENA_MASK);

	count = davinci_spi->count;

	/* Determine the command to execute READ or WRITE */
	if (t->tx_buf) {
		clear_io_bits(davinci_spi->base + SPIINT, SPIINT_MASKALL);

		while (1) {
			tx_data = davinci_spi->get_tx(davinci_spi);

			data1_reg_val &= ~(0xFFFF);
			data1_reg_val |= (0xFFFF & tx_data);

			buf_val = ioread32(davinci_spi->base + SPIBUF);
			if ((buf_val & SPIBUF_TXFULL_MASK) == 0) {
				iowrite32(data1_reg_val,
						davinci_spi->base + SPIDAT1);

				count--;
			}
			while (ioread32(davinci_spi->base + SPIBUF)
					& SPIBUF_RXEMPTY_MASK)
				cpu_relax();

			/* getting the returned byte */
			if (t->rx_buf) {
				buf_val = ioread32(davinci_spi->base + SPIBUF);
				davinci_spi->get_rx(buf_val, davinci_spi);
			}
			if (count <= 0)
				break;
		}
	} else {
		if (pdata->poll_mode) {
			while (1) {
				/* keeps the serial clock going */
				if ((ioread32(davinci_spi->base + SPIBUF)
						& SPIBUF_TXFULL_MASK) == 0)
					iowrite32(data1_reg_val,
						davinci_spi->base + SPIDAT1);

				while (ioread32(davinci_spi->base + SPIBUF) &
						SPIBUF_RXEMPTY_MASK)
					cpu_relax();

				flg_val = ioread32(davinci_spi->base + SPIFLG);
				buf_val = ioread32(davinci_spi->base + SPIBUF);

				davinci_spi->get_rx(buf_val, davinci_spi);

				count--;
				if (count <= 0)
					break;
			}
		} else {	/* Receive in Interrupt mode */
			int i;

			for (i = 0; i < davinci_spi->count; i++) {
				set_io_bits(davinci_spi->base + SPIINT,
						SPIINT_BITERR_INTR
						| SPIINT_OVRRUN_INTR
						| SPIINT_RX_INTR);

				iowrite32(data1_reg_val,
						davinci_spi->base + SPIDAT1);

				while (ioread32(davinci_spi->base + SPIINT) &
						SPIINT_RX_INTR)
					cpu_relax();
			}
			iowrite32((data1_reg_val & 0x0ffcffff),
					davinci_spi->base + SPIDAT1);
		}
	}

	/*
	 * Check for bit error, desync error,parity error,timeout error and
	 * receive overflow errors
	 */
	int_status = ioread32(davinci_spi->base + SPIFLG);

	ret = davinci_spi_check_error(davinci_spi, int_status);
	if (ret != 0)
		return ret;

	/* SPI Framework maintains the count only in bytes so convert back */
	davinci_spi->count *= conv;

	return t->len;
}

#define DAVINCI_DMA_DATA_TYPE_S8	0x01
#define DAVINCI_DMA_DATA_TYPE_S16	0x02
#define DAVINCI_DMA_DATA_TYPE_S32	0x04

static int davinci_spi_bufs_dma(struct spi_device *spi, struct spi_transfer *t)
{
	struct davinci_spi *davinci_spi;
	int int_status = 0;
	int count, temp_count;
	u8 conv = 1;
	u32 data1_reg_val;
	struct davinci_spi_dma *davinci_spi_dma;
	int word_len, data_type, ret;
	unsigned long tx_reg, rx_reg;
	struct device *sdev;

	davinci_spi = spi_master_get_devdata(spi->master);
	sdev = davinci_spi->bitbang.master->dev.parent;

	davinci_spi_dma = &davinci_spi->dma_channels[spi->chip_select];

	tx_reg = (unsigned long)davinci_spi->pbase + SPIDAT1;
	rx_reg = (unsigned long)davinci_spi->pbase + SPIBUF;

	davinci_spi->tx = t->tx_buf;
	davinci_spi->rx = t->rx_buf;

	/* convert len to words based on bits_per_word */
	conv = davinci_spi->slave[spi->chip_select].bytes_per_word;
	davinci_spi->count = t->len / conv;

	data1_reg_val = ioread32(davinci_spi->base + SPIDAT1);

	INIT_COMPLETION(davinci_spi->done);

	init_completion(&davinci_spi_dma->dma_rx_completion);
	init_completion(&davinci_spi_dma->dma_tx_completion);

	word_len = conv * 8;

	if (word_len <= 8)
		data_type = DAVINCI_DMA_DATA_TYPE_S8;
	else if (word_len <= 16)
		data_type = DAVINCI_DMA_DATA_TYPE_S16;
	else if (word_len <= 32)
		data_type = DAVINCI_DMA_DATA_TYPE_S32;
	else
		return -EINVAL;

	ret = davinci_spi_bufs_prep(spi, davinci_spi);
	if (ret)
		return ret;

	count = davinci_spi->count;	/* the number of elements */

	/* disable all interrupts for dma transfers */
	clear_io_bits(davinci_spi->base + SPIINT, SPIINT_MASKALL);
	/* Disable SPI to write configuration bits in SPIDAT */
	clear_io_bits(davinci_spi->base + SPIGCR1, SPIGCR1_SPIENA_MASK);
	/* Enable SPI */
	set_io_bits(davinci_spi->base + SPIGCR1, SPIGCR1_SPIENA_MASK);

	if (t->tx_buf) {
		t->tx_dma = dma_map_single(&spi->dev, (void *)t->tx_buf, count,
				DMA_TO_DEVICE);
		if (dma_mapping_error(&spi->dev, t->tx_dma)) {
			dev_dbg(sdev, "Unable to DMA map a %d bytes"
				" TX buffer\n", count);
			return -ENOMEM;
		}
		temp_count = count;
	} else {
		/* We need TX clocking for RX transaction */
		t->tx_dma = dma_map_single(&spi->dev,
				(void *)davinci_spi->tmp_buf, count + 1,
				DMA_TO_DEVICE);
		if (dma_mapping_error(&spi->dev, t->tx_dma)) {
			dev_dbg(sdev, "Unable to DMA map a %d bytes"
				" TX tmp buffer\n", count);
			return -ENOMEM;
		}
		temp_count = count + 1;
	}

	edma_set_transfer_params(davinci_spi_dma->dma_tx_channel,
					data_type, temp_count, 1, 0, ASYNC);
	edma_set_dest(davinci_spi_dma->dma_tx_channel, tx_reg, INCR, W8BIT);
	edma_set_src(davinci_spi_dma->dma_tx_channel, t->tx_dma, INCR, W8BIT);
	edma_set_src_index(davinci_spi_dma->dma_tx_channel, data_type, 0);
	edma_set_dest_index(davinci_spi_dma->dma_tx_channel, 0, 0);

	if (t->rx_buf) {
		/* initiate transaction */
		iowrite32(data1_reg_val, davinci_spi->base + SPIDAT1);

		t->rx_dma = dma_map_single(&spi->dev, (void *)t->rx_buf, count,
				DMA_FROM_DEVICE);
		if (dma_mapping_error(&spi->dev, t->rx_dma)) {
			dev_dbg(sdev, "Couldn't DMA map a %d bytes RX buffer\n",
					count);
			if (t->tx_buf != NULL)
				dma_unmap_single(NULL, t->tx_dma,
						 count, DMA_TO_DEVICE);
			return -ENOMEM;
		}
		edma_set_transfer_params(davinci_spi_dma->dma_rx_channel,
				data_type, count, 1, 0, ASYNC);
		edma_set_src(davinci_spi_dma->dma_rx_channel,
				rx_reg, INCR, W8BIT);
		edma_set_dest(davinci_spi_dma->dma_rx_channel,
				t->rx_dma, INCR, W8BIT);
		edma_set_src_index(davinci_spi_dma->dma_rx_channel, 0, 0);
		edma_set_dest_index(davinci_spi_dma->dma_rx_channel,
				data_type, 0);
	}

	if ((t->tx_buf) || (t->rx_buf))
		edma_start(davinci_spi_dma->dma_tx_channel);

	if (t->rx_buf)
		edma_start(davinci_spi_dma->dma_rx_channel);

	if ((t->rx_buf) || (t->tx_buf))
		davinci_spi_set_dma_req(spi, 1);

	if (t->tx_buf)
		wait_for_completion_interruptible(
				&davinci_spi_dma->dma_tx_completion);

	if (t->rx_buf)
		wait_for_completion_interruptible(
				&davinci_spi_dma->dma_rx_completion);

	dma_unmap_single(NULL, t->tx_dma, temp_count, DMA_TO_DEVICE);

	if (t->rx_buf)
		dma_unmap_single(NULL, t->rx_dma, count, DMA_FROM_DEVICE);

	/*
	 * Check for bit error, desync error,parity error,timeout error and
	 * receive overflow errors
	 */
	int_status = ioread32(davinci_spi->base + SPIFLG);

	ret = davinci_spi_check_error(davinci_spi, int_status);
	if (ret != 0)
		return ret;

	/* SPI Framework maintains the count only in bytes so convert back */
	davinci_spi->count *= conv;

	return t->len;
}

/**
 * davinci_spi_irq - IRQ handler for DaVinci SPI
 * @irq: IRQ number for this SPI Master
 * @context_data: structure for SPI Master controller davinci_spi
 */
static irqreturn_t davinci_spi_irq(s32 irq, void *context_data)
{
	struct davinci_spi *davinci_spi = context_data;
	u32 int_status, rx_data = 0;
	irqreturn_t ret = IRQ_NONE;

	int_status = ioread32(davinci_spi->base + SPIFLG);

	while ((int_status & SPIFLG_RX_INTR_MASK)) {
		if (likely(int_status & SPIFLG_RX_INTR_MASK)) {
			ret = IRQ_HANDLED;

			rx_data = ioread32(davinci_spi->base + SPIBUF);
			davinci_spi->get_rx(rx_data, davinci_spi);

			/* Disable Receive Interrupt */
			iowrite32(~(SPIINT_RX_INTR | SPIINT_TX_INTR),
					davinci_spi->base + SPIINT);
		} else
			(void)davinci_spi_check_error(davinci_spi, int_status);

		int_status = ioread32(davinci_spi->base + SPIFLG);
	}

	return ret;
}

/**
 * davinci_spi_probe - probe function for SPI Master Controller
 * @pdev: platform_device structure which contains plateform specific data
 */
static int davinci_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct davinci_spi *davinci_spi;
	struct davinci_spi_platform_data *pdata;
	struct resource *r, *mem;
	resource_size_t dma_rx_chan = SPI_NO_RESOURCE;
	resource_size_t	dma_tx_chan = SPI_NO_RESOURCE;
	resource_size_t	dma_eventq = SPI_NO_RESOURCE;
	int i = 0, ret = 0;

	pdata = pdev->dev.platform_data;
	if (pdata == NULL) {
		ret = -ENODEV;
		goto err;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(struct davinci_spi));
	if (master == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	dev_set_drvdata(&pdev->dev, master);

	davinci_spi = spi_master_get_devdata(master);
	if (davinci_spi == NULL) {
		ret = -ENOENT;
		goto free_master;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		ret = -ENOENT;
		goto free_master;
	}

	davinci_spi->pbase = r->start;
	davinci_spi->region_size = resource_size(r);
	davinci_spi->pdata = pdata;

	mem = request_mem_region(r->start, davinci_spi->region_size,
					pdev->name);
	if (mem == NULL) {
		ret = -EBUSY;
		goto free_master;
	}

	davinci_spi->base = ioremap(r->start, davinci_spi->region_size);
	if (davinci_spi->base == NULL) {
		ret = -ENOMEM;
		goto release_region;
	}

	davinci_spi->irq = platform_get_irq(pdev, 0);
	if (davinci_spi->irq <= 0) {
		ret = -EINVAL;
		goto unmap_io;
	}

	ret = request_irq(davinci_spi->irq, davinci_spi_irq, IRQF_DISABLED,
			  dev_name(&pdev->dev), davinci_spi);
	if (ret)
		goto unmap_io;

	/* Allocate tmp_buf for tx_buf */
	davinci_spi->tmp_buf = kzalloc(SPI_BUFSIZ, GFP_KERNEL);
	if (davinci_spi->tmp_buf == NULL) {
		ret = -ENOMEM;
		goto irq_free;
	}

	davinci_spi->bitbang.master = spi_master_get(master);
	if (davinci_spi->bitbang.master == NULL) {
		ret = -ENODEV;
		goto free_tmp_buf;
	}

	davinci_spi->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(davinci_spi->clk)) {
		ret = -ENODEV;
		goto put_master;
	}
	clk_enable(davinci_spi->clk);

	master->bus_num = pdev->id;
	master->num_chipselect = pdata->num_chipselect;
	master->setup = davinci_spi_setup;
	master->cleanup = davinci_spi_cleanup;

	davinci_spi->bitbang.chipselect = davinci_spi_chipselect;
	davinci_spi->bitbang.setup_transfer = davinci_spi_setup_transfer;

	davinci_spi->version = pdata->version;
	use_dma = pdata->use_dma;

	davinci_spi->bitbang.flags = SPI_NO_CS | SPI_LSB_FIRST | SPI_LOOP;
	if (davinci_spi->version == SPI_VERSION_2)
		davinci_spi->bitbang.flags |= SPI_READY;

	if (use_dma) {
		r = platform_get_resource(pdev, IORESOURCE_DMA, 0);
		if (r)
			dma_rx_chan = r->start;
		r = platform_get_resource(pdev, IORESOURCE_DMA, 1);
		if (r)
			dma_tx_chan = r->start;
		r = platform_get_resource(pdev, IORESOURCE_DMA, 2);
		if (r)
			dma_eventq = r->start;
	}

	if (!use_dma ||
	    dma_rx_chan == SPI_NO_RESOURCE ||
	    dma_tx_chan == SPI_NO_RESOURCE ||
	    dma_eventq	== SPI_NO_RESOURCE) {
		davinci_spi->bitbang.txrx_bufs = davinci_spi_bufs_pio;
		use_dma = 0;
	} else {
		davinci_spi->bitbang.txrx_bufs = davinci_spi_bufs_dma;
		davinci_spi->dma_channels = kzalloc(master->num_chipselect
				* sizeof(struct davinci_spi_dma), GFP_KERNEL);
		if (davinci_spi->dma_channels == NULL) {
			ret = -ENOMEM;
			goto free_clk;
		}

		for (i = 0; i < master->num_chipselect; i++) {
			davinci_spi->dma_channels[i].dma_rx_channel = -1;
			davinci_spi->dma_channels[i].dma_rx_sync_dev =
				dma_rx_chan;
			davinci_spi->dma_channels[i].dma_tx_channel = -1;
			davinci_spi->dma_channels[i].dma_tx_sync_dev =
				dma_tx_chan;
			davinci_spi->dma_channels[i].eventq = dma_eventq;
		}
		dev_info(&pdev->dev, "DaVinci SPI driver in EDMA mode\n"
				"Using RX channel = %d , TX channel = %d and "
				"event queue = %d", dma_rx_chan, dma_tx_chan,
				dma_eventq);
	}

	davinci_spi->get_rx = davinci_spi_rx_buf_u8;
	davinci_spi->get_tx = davinci_spi_tx_buf_u8;

	init_completion(&davinci_spi->done);

	/* Reset In/OUT SPI module */
	iowrite32(0, davinci_spi->base + SPIGCR0);
	udelay(100);
	iowrite32(1, davinci_spi->base + SPIGCR0);

	/* initialize chip selects */
	if (pdata->chip_sel) {
		for (i = 0; i < pdata->num_chipselect; i++) {
			if (pdata->chip_sel[i] != SPI_INTERN_CS)
				gpio_direction_output(pdata->chip_sel[i], 1);
		}
	}

	/* Clock internal */
	if (davinci_spi->pdata->clk_internal)
		set_io_bits(davinci_spi->base + SPIGCR1,
				SPIGCR1_CLKMOD_MASK);
	else
		clear_io_bits(davinci_spi->base + SPIGCR1,
				SPIGCR1_CLKMOD_MASK);

	iowrite32(CS_DEFAULT, davinci_spi->base + SPIDEF);

	/* master mode default */
	set_io_bits(davinci_spi->base + SPIGCR1, SPIGCR1_MASTER_MASK);

	if (davinci_spi->pdata->intr_level)
		iowrite32(SPI_INTLVL_1, davinci_spi->base + SPILVL);
	else
		iowrite32(SPI_INTLVL_0, davinci_spi->base + SPILVL);

	ret = spi_bitbang_start(&davinci_spi->bitbang);
	if (ret)
		goto free_clk;

	dev_info(&pdev->dev, "Controller at 0x%p\n", davinci_spi->base);

	if (!pdata->poll_mode)
		dev_info(&pdev->dev, "Operating in interrupt mode"
			" using IRQ %d\n", davinci_spi->irq);

	return ret;

free_clk:
	clk_disable(davinci_spi->clk);
	clk_put(davinci_spi->clk);
put_master:
	spi_master_put(master);
free_tmp_buf:
	kfree(davinci_spi->tmp_buf);
irq_free:
	free_irq(davinci_spi->irq, davinci_spi);
unmap_io:
	iounmap(davinci_spi->base);
release_region:
	release_mem_region(davinci_spi->pbase, davinci_spi->region_size);
free_master:
	kfree(master);
err:
	return ret;
}

/**
 * davinci_spi_remove - remove function for SPI Master Controller
 * @pdev: platform_device structure which contains plateform specific data
 *
 * This function will do the reverse action of davinci_spi_probe function
 * It will free the IRQ and SPI controller's memory region.
 * It will also call spi_bitbang_stop to destroy the work queue which was
 * created by spi_bitbang_start.
 */
static int __exit davinci_spi_remove(struct platform_device *pdev)
{
	struct davinci_spi *davinci_spi;
	struct spi_master *master;

	master = dev_get_drvdata(&pdev->dev);
	davinci_spi = spi_master_get_devdata(master);

	spi_bitbang_stop(&davinci_spi->bitbang);

	clk_disable(davinci_spi->clk);
	clk_put(davinci_spi->clk);
	spi_master_put(master);
	kfree(davinci_spi->tmp_buf);
	free_irq(davinci_spi->irq, davinci_spi);
	iounmap(davinci_spi->base);
	release_mem_region(davinci_spi->pbase, davinci_spi->region_size);

	return 0;
}

static struct platform_driver davinci_spi_driver = {
	.driver.name = "spi_davinci",
	.remove = __exit_p(davinci_spi_remove),
};

static int __init davinci_spi_init(void)
{
	return platform_driver_probe(&davinci_spi_driver, davinci_spi_probe);
}
module_init(davinci_spi_init);

static void __exit davinci_spi_exit(void)
{
	platform_driver_unregister(&davinci_spi_driver);
}
module_exit(davinci_spi_exit);

MODULE_DESCRIPTION("TI DaVinci SPI Master Controller Driver");
MODULE_LICENSE("GPL");
