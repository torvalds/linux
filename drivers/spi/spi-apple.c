// SPDX-License-Identifier: GPL-2.0
//
// Apple SoC SPI device driver
//
// Copyright The Asahi Linux Contributors
//
// Based on spi-sifive.c, Copyright 2018 SiFive, Inc.

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>

#define APPLE_SPI_CTRL			0x000
#define APPLE_SPI_CTRL_RUN		BIT(0)
#define APPLE_SPI_CTRL_TX_RESET		BIT(2)
#define APPLE_SPI_CTRL_RX_RESET		BIT(3)

#define APPLE_SPI_CFG			0x004
#define APPLE_SPI_CFG_CPHA		BIT(1)
#define APPLE_SPI_CFG_CPOL		BIT(2)
#define APPLE_SPI_CFG_MODE		GENMASK(6, 5)
#define APPLE_SPI_CFG_MODE_POLLED	0
#define APPLE_SPI_CFG_MODE_IRQ		1
#define APPLE_SPI_CFG_MODE_DMA		2
#define APPLE_SPI_CFG_IE_RXCOMPLETE	BIT(7)
#define APPLE_SPI_CFG_IE_TXRXTHRESH	BIT(8)
#define APPLE_SPI_CFG_LSB_FIRST		BIT(13)
#define APPLE_SPI_CFG_WORD_SIZE		GENMASK(16, 15)
#define APPLE_SPI_CFG_WORD_SIZE_8B	0
#define APPLE_SPI_CFG_WORD_SIZE_16B	1
#define APPLE_SPI_CFG_WORD_SIZE_32B	2
#define APPLE_SPI_CFG_FIFO_THRESH	GENMASK(18, 17)
#define APPLE_SPI_CFG_FIFO_THRESH_8B	0
#define APPLE_SPI_CFG_FIFO_THRESH_4B	1
#define APPLE_SPI_CFG_FIFO_THRESH_1B	2
#define APPLE_SPI_CFG_IE_TXCOMPLETE	BIT(21)

#define APPLE_SPI_STATUS		0x008
#define APPLE_SPI_STATUS_RXCOMPLETE	BIT(0)
#define APPLE_SPI_STATUS_TXRXTHRESH	BIT(1)
#define APPLE_SPI_STATUS_TXCOMPLETE	BIT(2)

#define APPLE_SPI_PIN			0x00c
#define APPLE_SPI_PIN_KEEP_MOSI		BIT(0)
#define APPLE_SPI_PIN_CS		BIT(1)

#define APPLE_SPI_TXDATA		0x010
#define APPLE_SPI_RXDATA		0x020
#define APPLE_SPI_CLKDIV		0x030
#define APPLE_SPI_CLKDIV_MAX		0x7ff
#define APPLE_SPI_RXCNT			0x034
#define APPLE_SPI_WORD_DELAY		0x038
#define APPLE_SPI_TXCNT			0x04c

#define APPLE_SPI_FIFOSTAT		0x10c
#define APPLE_SPI_FIFOSTAT_TXFULL	BIT(4)
#define APPLE_SPI_FIFOSTAT_LEVEL_TX	GENMASK(15, 8)
#define APPLE_SPI_FIFOSTAT_RXEMPTY	BIT(20)
#define APPLE_SPI_FIFOSTAT_LEVEL_RX	GENMASK(31, 24)

#define APPLE_SPI_IE_XFER		0x130
#define APPLE_SPI_IF_XFER		0x134
#define APPLE_SPI_XFER_RXCOMPLETE	BIT(0)
#define APPLE_SPI_XFER_TXCOMPLETE	BIT(1)

#define APPLE_SPI_IE_FIFO		0x138
#define APPLE_SPI_IF_FIFO		0x13c
#define APPLE_SPI_FIFO_RXTHRESH		BIT(4)
#define APPLE_SPI_FIFO_TXTHRESH		BIT(5)
#define APPLE_SPI_FIFO_RXFULL		BIT(8)
#define APPLE_SPI_FIFO_TXEMPTY		BIT(9)
#define APPLE_SPI_FIFO_RXUNDERRUN	BIT(16)
#define APPLE_SPI_FIFO_TXOVERFLOW	BIT(17)

#define APPLE_SPI_SHIFTCFG		0x150
#define APPLE_SPI_SHIFTCFG_CLK_ENABLE	BIT(0)
#define APPLE_SPI_SHIFTCFG_CS_ENABLE	BIT(1)
#define APPLE_SPI_SHIFTCFG_AND_CLK_DATA	BIT(8)
#define APPLE_SPI_SHIFTCFG_CS_AS_DATA	BIT(9)
#define APPLE_SPI_SHIFTCFG_TX_ENABLE	BIT(10)
#define APPLE_SPI_SHIFTCFG_RX_ENABLE	BIT(11)
#define APPLE_SPI_SHIFTCFG_BITS		GENMASK(21, 16)
#define APPLE_SPI_SHIFTCFG_OVERRIDE_CS	BIT(24)

#define APPLE_SPI_PINCFG		0x154
#define APPLE_SPI_PINCFG_KEEP_CLK	BIT(0)
#define APPLE_SPI_PINCFG_KEEP_CS	BIT(1)
#define APPLE_SPI_PINCFG_KEEP_MOSI	BIT(2)
#define APPLE_SPI_PINCFG_CLK_IDLE_VAL	BIT(8)
#define APPLE_SPI_PINCFG_CS_IDLE_VAL	BIT(9)
#define APPLE_SPI_PINCFG_MOSI_IDLE_VAL	BIT(10)

#define APPLE_SPI_DELAY_PRE		0x160
#define APPLE_SPI_DELAY_POST		0x168
#define APPLE_SPI_DELAY_ENABLE		BIT(0)
#define APPLE_SPI_DELAY_NO_INTERBYTE	BIT(1)
#define APPLE_SPI_DELAY_SET_SCK		BIT(4)
#define APPLE_SPI_DELAY_SET_MOSI	BIT(6)
#define APPLE_SPI_DELAY_SCK_VAL		BIT(8)
#define APPLE_SPI_DELAY_MOSI_VAL	BIT(12)

#define APPLE_SPI_FIFO_DEPTH		16

/*
 * The slowest refclock available is 24MHz, the highest divider is 0x7ff,
 * the largest word size is 32 bits, the FIFO depth is 16, the maximum
 * intra-word delay is 0xffff refclocks. So the maximum time a transfer
 * cycle can take is:
 *
 * (0x7ff * 32 + 0xffff) * 16 / 24e6 Hz ~= 87ms
 *
 * Double it and round it up to 200ms for good measure.
 */
#define APPLE_SPI_TIMEOUT_MS		200

struct apple_spi {
	void __iomem      *regs;        /* MMIO register address */
	struct clk        *clk;         /* bus clock */
	struct completion done;         /* wake-up from interrupt */
};

static inline void reg_write(struct apple_spi *spi, int offset, u32 value)
{
	writel_relaxed(value, spi->regs + offset);
}

static inline u32 reg_read(struct apple_spi *spi, int offset)
{
	return readl_relaxed(spi->regs + offset);
}

static inline void reg_mask(struct apple_spi *spi, int offset, u32 clear, u32 set)
{
	u32 val = reg_read(spi, offset);

	val &= ~clear;
	val |= set;
	reg_write(spi, offset, val);
}

static void apple_spi_init(struct apple_spi *spi)
{
	/* Set CS high (inactive) and disable override and auto-CS */
	reg_write(spi, APPLE_SPI_PIN, APPLE_SPI_PIN_CS);
	reg_mask(spi, APPLE_SPI_SHIFTCFG, APPLE_SPI_SHIFTCFG_OVERRIDE_CS, 0);
	reg_mask(spi, APPLE_SPI_PINCFG, APPLE_SPI_PINCFG_CS_IDLE_VAL, APPLE_SPI_PINCFG_KEEP_CS);

	/* Reset FIFOs */
	reg_write(spi, APPLE_SPI_CTRL, APPLE_SPI_CTRL_RX_RESET | APPLE_SPI_CTRL_TX_RESET);

	/* Configure defaults */
	reg_write(spi, APPLE_SPI_CFG,
		  FIELD_PREP(APPLE_SPI_CFG_FIFO_THRESH, APPLE_SPI_CFG_FIFO_THRESH_8B) |
		  FIELD_PREP(APPLE_SPI_CFG_MODE, APPLE_SPI_CFG_MODE_IRQ) |
		  FIELD_PREP(APPLE_SPI_CFG_WORD_SIZE, APPLE_SPI_CFG_WORD_SIZE_8B));

	/* Disable IRQs */
	reg_write(spi, APPLE_SPI_IE_FIFO, 0);
	reg_write(spi, APPLE_SPI_IE_XFER, 0);

	/* Disable delays */
	reg_write(spi, APPLE_SPI_DELAY_PRE, 0);
	reg_write(spi, APPLE_SPI_DELAY_POST, 0);
}

static int apple_spi_prepare_message(struct spi_controller *ctlr, struct spi_message *msg)
{
	struct apple_spi *spi = spi_controller_get_devdata(ctlr);
	struct spi_device *device = msg->spi;

	u32 cfg = ((device->mode & SPI_CPHA ? APPLE_SPI_CFG_CPHA : 0) |
		   (device->mode & SPI_CPOL ? APPLE_SPI_CFG_CPOL : 0) |
		   (device->mode & SPI_LSB_FIRST ? APPLE_SPI_CFG_LSB_FIRST : 0));

	/* Update core config */
	reg_mask(spi, APPLE_SPI_CFG,
		 APPLE_SPI_CFG_CPHA | APPLE_SPI_CFG_CPOL | APPLE_SPI_CFG_LSB_FIRST, cfg);

	return 0;
}

static void apple_spi_set_cs(struct spi_device *device, bool is_high)
{
	struct apple_spi *spi = spi_controller_get_devdata(device->controller);

	reg_mask(spi, APPLE_SPI_PIN, APPLE_SPI_PIN_CS, is_high ? APPLE_SPI_PIN_CS : 0);
}

static bool apple_spi_prep_transfer(struct apple_spi *spi, struct spi_transfer *t)
{
	u32 cr, fifo_threshold;

	/* Calculate and program the clock rate */
	cr = DIV_ROUND_UP(clk_get_rate(spi->clk), t->speed_hz);
	reg_write(spi, APPLE_SPI_CLKDIV, min_t(u32, cr, APPLE_SPI_CLKDIV_MAX));

	/* Update bits per word */
	reg_mask(spi, APPLE_SPI_SHIFTCFG, APPLE_SPI_SHIFTCFG_BITS,
		 FIELD_PREP(APPLE_SPI_SHIFTCFG_BITS, t->bits_per_word));

	/* We will want to poll if the time we need to wait is
	 * less than the context switching time.
	 * Let's call that threshold 5us. The operation will take:
	 *    bits_per_word * fifo_threshold / hz <= 5 * 10^-6
	 *    200000 * bits_per_word * fifo_threshold <= hz
	 */
	fifo_threshold = APPLE_SPI_FIFO_DEPTH / 2;
	return (200000 * t->bits_per_word * fifo_threshold) <= t->speed_hz;
}

static irqreturn_t apple_spi_irq(int irq, void *dev_id)
{
	struct apple_spi *spi = dev_id;
	u32 fifo = reg_read(spi, APPLE_SPI_IF_FIFO) & reg_read(spi, APPLE_SPI_IE_FIFO);
	u32 xfer = reg_read(spi, APPLE_SPI_IF_XFER) & reg_read(spi, APPLE_SPI_IE_XFER);

	if (fifo || xfer) {
		/* Disable interrupts until next transfer */
		reg_write(spi, APPLE_SPI_IE_XFER, 0);
		reg_write(spi, APPLE_SPI_IE_FIFO, 0);
		complete(&spi->done);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int apple_spi_wait(struct apple_spi *spi, u32 fifo_bit, u32 xfer_bit, int poll)
{
	int ret = 0;

	if (poll) {
		u32 fifo, xfer;
		unsigned long timeout = jiffies + APPLE_SPI_TIMEOUT_MS * HZ / 1000;

		do {
			fifo = reg_read(spi, APPLE_SPI_IF_FIFO);
			xfer = reg_read(spi, APPLE_SPI_IF_XFER);
			if (time_after(jiffies, timeout)) {
				ret = -ETIMEDOUT;
				break;
			}
		} while (!((fifo & fifo_bit) || (xfer & xfer_bit)));
	} else {
		reinit_completion(&spi->done);
		reg_write(spi, APPLE_SPI_IE_XFER, xfer_bit);
		reg_write(spi, APPLE_SPI_IE_FIFO, fifo_bit);

		if (!wait_for_completion_timeout(&spi->done,
						 msecs_to_jiffies(APPLE_SPI_TIMEOUT_MS)))
			ret = -ETIMEDOUT;

		reg_write(spi, APPLE_SPI_IE_XFER, 0);
		reg_write(spi, APPLE_SPI_IE_FIFO, 0);
	}

	return ret;
}

static void apple_spi_tx(struct apple_spi *spi, const void **tx_ptr, u32 *left,
			 unsigned int bytes_per_word)
{
	u32 inuse, words, wrote;

	if (!*tx_ptr)
		return;

	inuse = FIELD_GET(APPLE_SPI_FIFOSTAT_LEVEL_TX, reg_read(spi, APPLE_SPI_FIFOSTAT));
	words = wrote = min_t(u32, *left, APPLE_SPI_FIFO_DEPTH - inuse);

	if (!words)
		return;

	*left -= words;

	switch (bytes_per_word) {
	case 1: {
		const u8 *p = *tx_ptr;

		while (words--)
			reg_write(spi, APPLE_SPI_TXDATA, *p++);
		break;
	}
	case 2: {
		const u16 *p = *tx_ptr;

		while (words--)
			reg_write(spi, APPLE_SPI_TXDATA, *p++);
		break;
	}
	case 4: {
		const u32 *p = *tx_ptr;

		while (words--)
			reg_write(spi, APPLE_SPI_TXDATA, *p++);
		break;
	}
	default:
		WARN_ON(1);
	}

	*tx_ptr = ((u8 *)*tx_ptr) + bytes_per_word * wrote;
}

static void apple_spi_rx(struct apple_spi *spi, void **rx_ptr, u32 *left,
			 unsigned int bytes_per_word)
{
	u32 words, read;

	if (!*rx_ptr)
		return;

	words = read = FIELD_GET(APPLE_SPI_FIFOSTAT_LEVEL_RX, reg_read(spi, APPLE_SPI_FIFOSTAT));
	WARN_ON(words > *left);

	if (!words)
		return;

	*left -= min_t(u32, *left, words);

	switch (bytes_per_word) {
	case 1: {
		u8 *p = *rx_ptr;

		while (words--)
			*p++ = reg_read(spi, APPLE_SPI_RXDATA);
		break;
	}
	case 2: {
		u16 *p = *rx_ptr;

		while (words--)
			*p++ = reg_read(spi, APPLE_SPI_RXDATA);
		break;
	}
	case 4: {
		u32 *p = *rx_ptr;

		while (words--)
			*p++ = reg_read(spi, APPLE_SPI_RXDATA);
		break;
	}
	default:
		WARN_ON(1);
	}

	*rx_ptr = ((u8 *)*rx_ptr) + bytes_per_word * read;
}

static int apple_spi_transfer_one(struct spi_controller *ctlr, struct spi_device *device,
				  struct spi_transfer *t)
{
	struct apple_spi *spi = spi_controller_get_devdata(ctlr);
	bool poll = apple_spi_prep_transfer(spi, t);
	const void *tx_ptr = t->tx_buf;
	void *rx_ptr = t->rx_buf;
	unsigned int bytes_per_word;
	u32 words, remaining_tx, remaining_rx;
	u32 xfer_flags = 0;
	u32 fifo_flags;
	int retries = 100;
	int ret = 0;

	if (t->bits_per_word > 16)
		bytes_per_word = 4;
	else if (t->bits_per_word > 8)
		bytes_per_word = 2;
	else
		bytes_per_word = 1;

	words = t->len / bytes_per_word;
	remaining_tx = tx_ptr ? words : 0;
	remaining_rx = rx_ptr ? words : 0;

	/* Reset FIFOs */
	reg_write(spi, APPLE_SPI_CTRL, APPLE_SPI_CTRL_RX_RESET | APPLE_SPI_CTRL_TX_RESET);

	/* Clear IRQ flags */
	reg_write(spi, APPLE_SPI_IF_XFER, ~0);
	reg_write(spi, APPLE_SPI_IF_FIFO, ~0);

	/* Determine transfer completion flags we wait for */
	if (tx_ptr)
		xfer_flags |= APPLE_SPI_XFER_TXCOMPLETE;
	if (rx_ptr)
		xfer_flags |= APPLE_SPI_XFER_RXCOMPLETE;

	/* Set transfer length */
	reg_write(spi, APPLE_SPI_TXCNT, remaining_tx);
	reg_write(spi, APPLE_SPI_RXCNT, remaining_rx);

	/* Prime transmit FIFO */
	apple_spi_tx(spi, &tx_ptr, &remaining_tx, bytes_per_word);

	/* Start transfer */
	reg_write(spi, APPLE_SPI_CTRL, APPLE_SPI_CTRL_RUN);

	/* TX again since a few words get popped off immediately */
	apple_spi_tx(spi, &tx_ptr, &remaining_tx, bytes_per_word);

	while (xfer_flags) {
		fifo_flags = 0;

		if (remaining_tx)
			fifo_flags |= APPLE_SPI_FIFO_TXTHRESH;
		if (remaining_rx)
			fifo_flags |= APPLE_SPI_FIFO_RXTHRESH;

		/* Wait for anything to happen */
		ret = apple_spi_wait(spi, fifo_flags, xfer_flags, poll);
		if (ret) {
			dev_err(&ctlr->dev, "transfer timed out (remaining %d tx, %d rx)\n",
				remaining_tx, remaining_rx);
			goto err;
		}

		/* Stop waiting on transfer halves once they complete */
		xfer_flags &= ~reg_read(spi, APPLE_SPI_IF_XFER);

		/* Transmit and receive everything we can */
		apple_spi_tx(spi, &tx_ptr, &remaining_tx, bytes_per_word);
		apple_spi_rx(spi, &rx_ptr, &remaining_rx, bytes_per_word);
	}

	/*
	 * Sometimes the transfer completes before the last word is in the RX FIFO.
	 * Normally one retry is all it takes to get the last word out.
	 */
	while (remaining_rx && retries--)
		apple_spi_rx(spi, &rx_ptr, &remaining_rx, bytes_per_word);

	if (remaining_tx)
		dev_err(&ctlr->dev, "transfer completed with %d words left to transmit\n",
			remaining_tx);
	if (remaining_rx)
		dev_err(&ctlr->dev, "transfer completed with %d words left to receive\n",
			remaining_rx);

err:
	fifo_flags = reg_read(spi, APPLE_SPI_IF_FIFO);
	WARN_ON(fifo_flags & APPLE_SPI_FIFO_TXOVERFLOW);
	WARN_ON(fifo_flags & APPLE_SPI_FIFO_RXUNDERRUN);

	/* Stop transfer */
	reg_write(spi, APPLE_SPI_CTRL, 0);

	return ret;
}

static int apple_spi_probe(struct platform_device *pdev)
{
	struct apple_spi *spi;
	int ret, irq;
	struct spi_controller *ctlr;

	ctlr = devm_spi_alloc_host(&pdev->dev, sizeof(struct apple_spi));
	if (!ctlr)
		return -ENOMEM;

	spi = spi_controller_get_devdata(ctlr);
	init_completion(&spi->done);

	spi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spi->regs))
		return PTR_ERR(spi->regs);

	spi->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(spi->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(spi->clk),
				     "Unable to find or enable bus clock\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, apple_spi_irq, 0,
			       dev_name(&pdev->dev), spi);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Unable to bind to interrupt\n");

	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->bus_num = pdev->id;
	ctlr->num_chipselect = 1;
	ctlr->mode_bits = SPI_CPHA | SPI_CPOL | SPI_LSB_FIRST;
	ctlr->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 32);
	ctlr->prepare_message = apple_spi_prepare_message;
	ctlr->set_cs = apple_spi_set_cs;
	ctlr->transfer_one = apple_spi_transfer_one;
	ctlr->auto_runtime_pm = true;

	pm_runtime_set_active(&pdev->dev);
	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret < 0)
		return ret;

	apple_spi_init(spi);

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "devm_spi_register_controller failed\n");

	return 0;
}

static const struct of_device_id apple_spi_of_match[] = {
	{ .compatible = "apple,spi", },
	{}
};
MODULE_DEVICE_TABLE(of, apple_spi_of_match);

static struct platform_driver apple_spi_driver = {
	.probe = apple_spi_probe,
	.driver = {
		.name = "apple-spi",
		.of_match_table = apple_spi_of_match,
	},
};
module_platform_driver(apple_spi_driver);

MODULE_AUTHOR("Hector Martin <marcan@marcan.st>");
MODULE_DESCRIPTION("Apple SoC SPI driver");
MODULE_LICENSE("GPL");
