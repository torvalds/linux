/*
 * Driver for Broadcom BCM2835 auxiliary SPI Controllers
 *
 * the driver does not rely on the native chipselects at all
 * but only uses the gpio type chipselects
 *
 * Based on: spi-bcm2835.c
 *
 * Copyright (C) 2015 Martin Sperl
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
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>

/*
 * spi register defines
 *
 * note there is garbage in the "official" documentation,
 * so some data is taken from the file:
 *   brcm_usrlib/dag/vmcsx/vcinclude/bcm2708_chip/aux_io.h
 * inside of:
 *   http://www.broadcom.com/docs/support/videocore/Brcm_Android_ICS_Graphics_Stack.tar.gz
 */

/* SPI register offsets */
#define BCM2835_AUX_SPI_CNTL0	0x00
#define BCM2835_AUX_SPI_CNTL1	0x04
#define BCM2835_AUX_SPI_STAT	0x08
#define BCM2835_AUX_SPI_PEEK	0x0C
#define BCM2835_AUX_SPI_IO	0x20
#define BCM2835_AUX_SPI_TXHOLD	0x30

/* Bitfields in CNTL0 */
#define BCM2835_AUX_SPI_CNTL0_SPEED	0xFFF00000
#define BCM2835_AUX_SPI_CNTL0_SPEED_MAX	0xFFF
#define BCM2835_AUX_SPI_CNTL0_SPEED_SHIFT	20
#define BCM2835_AUX_SPI_CNTL0_CS	0x000E0000
#define BCM2835_AUX_SPI_CNTL0_POSTINPUT	0x00010000
#define BCM2835_AUX_SPI_CNTL0_VAR_CS	0x00008000
#define BCM2835_AUX_SPI_CNTL0_VAR_WIDTH	0x00004000
#define BCM2835_AUX_SPI_CNTL0_DOUTHOLD	0x00003000
#define BCM2835_AUX_SPI_CNTL0_ENABLE	0x00000800
#define BCM2835_AUX_SPI_CNTL0_IN_RISING	0x00000400
#define BCM2835_AUX_SPI_CNTL0_CLEARFIFO	0x00000200
#define BCM2835_AUX_SPI_CNTL0_OUT_RISING	0x00000100
#define BCM2835_AUX_SPI_CNTL0_CPOL	0x00000080
#define BCM2835_AUX_SPI_CNTL0_MSBF_OUT	0x00000040
#define BCM2835_AUX_SPI_CNTL0_SHIFTLEN	0x0000003F

/* Bitfields in CNTL1 */
#define BCM2835_AUX_SPI_CNTL1_CSHIGH	0x00000700
#define BCM2835_AUX_SPI_CNTL1_TXEMPTY	0x00000080
#define BCM2835_AUX_SPI_CNTL1_IDLE	0x00000040
#define BCM2835_AUX_SPI_CNTL1_MSBF_IN	0x00000002
#define BCM2835_AUX_SPI_CNTL1_KEEP_IN	0x00000001

/* Bitfields in STAT */
#define BCM2835_AUX_SPI_STAT_TX_LVL	0xFF000000
#define BCM2835_AUX_SPI_STAT_RX_LVL	0x00FF0000
#define BCM2835_AUX_SPI_STAT_TX_FULL	0x00000400
#define BCM2835_AUX_SPI_STAT_TX_EMPTY	0x00000200
#define BCM2835_AUX_SPI_STAT_RX_FULL	0x00000100
#define BCM2835_AUX_SPI_STAT_RX_EMPTY	0x00000080
#define BCM2835_AUX_SPI_STAT_BUSY	0x00000040
#define BCM2835_AUX_SPI_STAT_BITCOUNT	0x0000003F

/* timeout values */
#define BCM2835_AUX_SPI_POLLING_LIMIT_US	30
#define BCM2835_AUX_SPI_POLLING_JIFFIES		2

struct bcm2835aux_spi {
	void __iomem *regs;
	struct clk *clk;
	int irq;
	u32 cntl[2];
	const u8 *tx_buf;
	u8 *rx_buf;
	int tx_len;
	int rx_len;
	int pending;
};

static inline u32 bcm2835aux_rd(struct bcm2835aux_spi *bs, unsigned reg)
{
	return readl(bs->regs + reg);
}

static inline void bcm2835aux_wr(struct bcm2835aux_spi *bs, unsigned reg,
				 u32 val)
{
	writel(val, bs->regs + reg);
}

static inline void bcm2835aux_rd_fifo(struct bcm2835aux_spi *bs)
{
	u32 data;
	int count = min(bs->rx_len, 3);

	data = bcm2835aux_rd(bs, BCM2835_AUX_SPI_IO);
	if (bs->rx_buf) {
		switch (count) {
		case 4:
			*bs->rx_buf++ = (data >> 24) & 0xff;
			/* fallthrough */
		case 3:
			*bs->rx_buf++ = (data >> 16) & 0xff;
			/* fallthrough */
		case 2:
			*bs->rx_buf++ = (data >> 8) & 0xff;
			/* fallthrough */
		case 1:
			*bs->rx_buf++ = (data >> 0) & 0xff;
			/* fallthrough - no default */
		}
	}
	bs->rx_len -= count;
	bs->pending -= count;
}

static inline void bcm2835aux_wr_fifo(struct bcm2835aux_spi *bs)
{
	u32 data;
	u8 byte;
	int count;
	int i;

	/* gather up to 3 bytes to write to the FIFO */
	count = min(bs->tx_len, 3);
	data = 0;
	for (i = 0; i < count; i++) {
		byte = bs->tx_buf ? *bs->tx_buf++ : 0;
		data |= byte << (8 * (2 - i));
	}

	/* and set the variable bit-length */
	data |= (count * 8) << 24;

	/* and decrement length */
	bs->tx_len -= count;
	bs->pending += count;

	/* write to the correct TX-register */
	if (bs->tx_len)
		bcm2835aux_wr(bs, BCM2835_AUX_SPI_TXHOLD, data);
	else
		bcm2835aux_wr(bs, BCM2835_AUX_SPI_IO, data);
}

static void bcm2835aux_spi_reset_hw(struct bcm2835aux_spi *bs)
{
	/* disable spi clearing fifo and interrupts */
	bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL1, 0);
	bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL0,
		      BCM2835_AUX_SPI_CNTL0_CLEARFIFO);
}

static void bcm2835aux_spi_transfer_helper(struct bcm2835aux_spi *bs)
{
	u32 stat = bcm2835aux_rd(bs, BCM2835_AUX_SPI_STAT);

	/* check if we have data to read */
	for (; bs->rx_len && (stat & BCM2835_AUX_SPI_STAT_RX_LVL);
	     stat = bcm2835aux_rd(bs, BCM2835_AUX_SPI_STAT))
		bcm2835aux_rd_fifo(bs);

	/* check if we have data to write */
	while (bs->tx_len &&
	       (bs->pending < 12) &&
	       (!(bcm2835aux_rd(bs, BCM2835_AUX_SPI_STAT) &
		  BCM2835_AUX_SPI_STAT_TX_FULL))) {
		bcm2835aux_wr_fifo(bs);
	}
}

static irqreturn_t bcm2835aux_spi_interrupt(int irq, void *dev_id)
{
	struct spi_master *master = dev_id;
	struct bcm2835aux_spi *bs = spi_master_get_devdata(master);

	/* IRQ may be shared, so return if our interrupts are disabled */
	if (!(bcm2835aux_rd(bs, BCM2835_AUX_SPI_CNTL1) &
	      (BCM2835_AUX_SPI_CNTL1_TXEMPTY | BCM2835_AUX_SPI_CNTL1_IDLE)))
		return IRQ_NONE;

	/* do common fifo handling */
	bcm2835aux_spi_transfer_helper(bs);

	if (!bs->tx_len) {
		/* disable tx fifo empty interrupt */
		bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL1, bs->cntl[1] |
			BCM2835_AUX_SPI_CNTL1_IDLE);
	}

	/* and if rx_len is 0 then disable interrupts and wake up completion */
	if (!bs->rx_len) {
		bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL1, bs->cntl[1]);
		complete(&master->xfer_completion);
	}

	return IRQ_HANDLED;
}

static int __bcm2835aux_spi_transfer_one_irq(struct spi_master *master,
					     struct spi_device *spi,
					     struct spi_transfer *tfr)
{
	struct bcm2835aux_spi *bs = spi_master_get_devdata(master);

	/* enable interrupts */
	bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL1, bs->cntl[1] |
		BCM2835_AUX_SPI_CNTL1_TXEMPTY |
		BCM2835_AUX_SPI_CNTL1_IDLE);

	/* and wait for finish... */
	return 1;
}

static int bcm2835aux_spi_transfer_one_irq(struct spi_master *master,
					   struct spi_device *spi,
					   struct spi_transfer *tfr)
{
	struct bcm2835aux_spi *bs = spi_master_get_devdata(master);

	/* fill in registers and fifos before enabling interrupts */
	bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL1, bs->cntl[1]);
	bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL0, bs->cntl[0]);

	/* fill in tx fifo with data before enabling interrupts */
	while ((bs->tx_len) &&
	       (bs->pending < 12) &&
	       (!(bcm2835aux_rd(bs, BCM2835_AUX_SPI_STAT) &
		  BCM2835_AUX_SPI_STAT_TX_FULL))) {
		bcm2835aux_wr_fifo(bs);
	}

	/* now run the interrupt mode */
	return __bcm2835aux_spi_transfer_one_irq(master, spi, tfr);
}

static int bcm2835aux_spi_transfer_one_poll(struct spi_master *master,
					    struct spi_device *spi,
					struct spi_transfer *tfr)
{
	struct bcm2835aux_spi *bs = spi_master_get_devdata(master);
	unsigned long timeout;

	/* configure spi */
	bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL1, bs->cntl[1]);
	bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL0, bs->cntl[0]);

	/* set the timeout */
	timeout = jiffies + BCM2835_AUX_SPI_POLLING_JIFFIES;

	/* loop until finished the transfer */
	while (bs->rx_len) {

		/* do common fifo handling */
		bcm2835aux_spi_transfer_helper(bs);

		/* there is still data pending to read check the timeout */
		if (bs->rx_len && time_after(jiffies, timeout)) {
			dev_dbg_ratelimited(&spi->dev,
					    "timeout period reached: jiffies: %lu remaining tx/rx: %d/%d - falling back to interrupt mode\n",
					    jiffies - timeout,
					    bs->tx_len, bs->rx_len);
			/* forward to interrupt handler */
			return __bcm2835aux_spi_transfer_one_irq(master,
							       spi, tfr);
		}
	}

	/* and return without waiting for completion */
	return 0;
}

static int bcm2835aux_spi_transfer_one(struct spi_master *master,
				       struct spi_device *spi,
				       struct spi_transfer *tfr)
{
	struct bcm2835aux_spi *bs = spi_master_get_devdata(master);
	unsigned long spi_hz, clk_hz, speed;
	unsigned long spi_used_hz;

	/* calculate the registers to handle
	 *
	 * note that we use the variable data mode, which
	 * is not optimal for longer transfers as we waste registers
	 * resulting (potentially) in more interrupts when transferring
	 * more than 12 bytes
	 */

	/* set clock */
	spi_hz = tfr->speed_hz;
	clk_hz = clk_get_rate(bs->clk);

	if (spi_hz >= clk_hz / 2) {
		speed = 0;
	} else if (spi_hz) {
		speed = DIV_ROUND_UP(clk_hz, 2 * spi_hz) - 1;
		if (speed >  BCM2835_AUX_SPI_CNTL0_SPEED_MAX)
			speed = BCM2835_AUX_SPI_CNTL0_SPEED_MAX;
	} else { /* the slowest we can go */
		speed = BCM2835_AUX_SPI_CNTL0_SPEED_MAX;
	}
	/* mask out old speed from previous spi_transfer */
	bs->cntl[0] &= ~(BCM2835_AUX_SPI_CNTL0_SPEED);
	/* set the new speed */
	bs->cntl[0] |= speed << BCM2835_AUX_SPI_CNTL0_SPEED_SHIFT;

	spi_used_hz = clk_hz / (2 * (speed + 1));

	/* set transmit buffers and length */
	bs->tx_buf = tfr->tx_buf;
	bs->rx_buf = tfr->rx_buf;
	bs->tx_len = tfr->len;
	bs->rx_len = tfr->len;
	bs->pending = 0;

	/* Calculate the estimated time in us the transfer runs.  Note that
	 * there are are 2 idle clocks cycles after each chunk getting
	 * transferred - in our case the chunk size is 3 bytes, so we
	 * approximate this by 9 cycles/byte.  This is used to find the number
	 * of Hz per byte per polling limit.  E.g., we can transfer 1 byte in
	 * 30 µs per 300,000 Hz of bus clock.
	 */
#define HZ_PER_BYTE ((9 * 1000000) / BCM2835_AUX_SPI_POLLING_LIMIT_US)
	/* run in polling mode for short transfers */
	if (tfr->len < spi_used_hz / HZ_PER_BYTE)
		return bcm2835aux_spi_transfer_one_poll(master, spi, tfr);

	/* run in interrupt mode for all others */
	return bcm2835aux_spi_transfer_one_irq(master, spi, tfr);
#undef HZ_PER_BYTE
}

static int bcm2835aux_spi_prepare_message(struct spi_master *master,
					  struct spi_message *msg)
{
	struct spi_device *spi = msg->spi;
	struct bcm2835aux_spi *bs = spi_master_get_devdata(master);

	bs->cntl[0] = BCM2835_AUX_SPI_CNTL0_ENABLE |
		      BCM2835_AUX_SPI_CNTL0_VAR_WIDTH |
		      BCM2835_AUX_SPI_CNTL0_MSBF_OUT;
	bs->cntl[1] = BCM2835_AUX_SPI_CNTL1_MSBF_IN;

	/* handle all the modes */
	if (spi->mode & SPI_CPOL) {
		bs->cntl[0] |= BCM2835_AUX_SPI_CNTL0_CPOL;
		bs->cntl[0] |= BCM2835_AUX_SPI_CNTL0_OUT_RISING;
	} else {
		bs->cntl[0] |= BCM2835_AUX_SPI_CNTL0_IN_RISING;
	}
	bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL1, bs->cntl[1]);
	bcm2835aux_wr(bs, BCM2835_AUX_SPI_CNTL0, bs->cntl[0]);

	return 0;
}

static int bcm2835aux_spi_unprepare_message(struct spi_master *master,
					    struct spi_message *msg)
{
	struct bcm2835aux_spi *bs = spi_master_get_devdata(master);

	bcm2835aux_spi_reset_hw(bs);

	return 0;
}

static void bcm2835aux_spi_handle_err(struct spi_master *master,
				      struct spi_message *msg)
{
	struct bcm2835aux_spi *bs = spi_master_get_devdata(master);

	bcm2835aux_spi_reset_hw(bs);
}

static int bcm2835aux_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct bcm2835aux_spi *bs;
	struct resource *res;
	unsigned long clk_hz;
	int err;

	master = spi_alloc_master(&pdev->dev, sizeof(*bs));
	if (!master) {
		dev_err(&pdev->dev, "spi_alloc_master() failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, master);
	master->mode_bits = (SPI_CPOL | SPI_CS_HIGH | SPI_NO_CS);
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	/* even though the driver never officially supported native CS
	 * allow a single native CS for legacy DT support purposes when
	 * no cs-gpio is configured.
	 * Known limitations for native cs are:
	 * * multiple chip-selects: cs0-cs2 are all simultaniously asserted
	 *     whenever there is a transfer -  this even includes SPI_NO_CS
	 * * SPI_CS_HIGH: is ignores - cs are always asserted low
	 * * cs_change: cs is deasserted after each spi_transfer
	 * * cs_delay_usec: cs is always deasserted one SCK cycle after
	 *     a spi_transfer
	 */
	master->num_chipselect = 1;
	master->transfer_one = bcm2835aux_spi_transfer_one;
	master->handle_err = bcm2835aux_spi_handle_err;
	master->prepare_message = bcm2835aux_spi_prepare_message;
	master->unprepare_message = bcm2835aux_spi_unprepare_message;
	master->dev.of_node = pdev->dev.of_node;

	bs = spi_master_get_devdata(master);

	/* the main area */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bs->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(bs->regs)) {
		err = PTR_ERR(bs->regs);
		goto out_master_put;
	}

	bs->clk = devm_clk_get(&pdev->dev, NULL);
	if ((!bs->clk) || (IS_ERR(bs->clk))) {
		err = PTR_ERR(bs->clk);
		dev_err(&pdev->dev, "could not get clk: %d\n", err);
		goto out_master_put;
	}

	bs->irq = platform_get_irq(pdev, 0);
	if (bs->irq <= 0) {
		dev_err(&pdev->dev, "could not get IRQ: %d\n", bs->irq);
		err = bs->irq ? bs->irq : -ENODEV;
		goto out_master_put;
	}

	/* this also enables the HW block */
	err = clk_prepare_enable(bs->clk);
	if (err) {
		dev_err(&pdev->dev, "could not prepare clock: %d\n", err);
		goto out_master_put;
	}

	/* just checking if the clock returns a sane value */
	clk_hz = clk_get_rate(bs->clk);
	if (!clk_hz) {
		dev_err(&pdev->dev, "clock returns 0 Hz\n");
		err = -ENODEV;
		goto out_clk_disable;
	}

	/* reset SPI-HW block */
	bcm2835aux_spi_reset_hw(bs);

	err = devm_request_irq(&pdev->dev, bs->irq,
			       bcm2835aux_spi_interrupt,
			       IRQF_SHARED,
			       dev_name(&pdev->dev), master);
	if (err) {
		dev_err(&pdev->dev, "could not request IRQ: %d\n", err);
		goto out_clk_disable;
	}

	err = devm_spi_register_master(&pdev->dev, master);
	if (err) {
		dev_err(&pdev->dev, "could not register SPI master: %d\n", err);
		goto out_clk_disable;
	}

	return 0;

out_clk_disable:
	clk_disable_unprepare(bs->clk);
out_master_put:
	spi_master_put(master);
	return err;
}

static int bcm2835aux_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct bcm2835aux_spi *bs = spi_master_get_devdata(master);

	bcm2835aux_spi_reset_hw(bs);

	/* disable the HW block by releasing the clock */
	clk_disable_unprepare(bs->clk);

	return 0;
}

static const struct of_device_id bcm2835aux_spi_match[] = {
	{ .compatible = "brcm,bcm2835-aux-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, bcm2835aux_spi_match);

static struct platform_driver bcm2835aux_spi_driver = {
	.driver		= {
		.name		= "spi-bcm2835aux",
		.of_match_table	= bcm2835aux_spi_match,
	},
	.probe		= bcm2835aux_spi_probe,
	.remove		= bcm2835aux_spi_remove,
};
module_platform_driver(bcm2835aux_spi_driver);

MODULE_DESCRIPTION("SPI controller driver for Broadcom BCM2835 aux");
MODULE_AUTHOR("Martin Sperl <kernel@martin.sperl.org>");
MODULE_LICENSE("GPL v2");
