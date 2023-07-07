// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Renesas RZ/V2M Clocked Serial Interface (CSI) driver
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/count_zeros.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>

/* Registers */
#define CSI_MODE		0x00	/* CSI mode control */
#define CSI_CLKSEL		0x04	/* CSI clock select */
#define CSI_CNT			0x08	/* CSI control */
#define CSI_INT			0x0C	/* CSI interrupt status */
#define CSI_IFIFOL		0x10	/* CSI receive FIFO level display */
#define CSI_OFIFOL		0x14	/* CSI transmit FIFO level display */
#define CSI_IFIFO		0x18	/* CSI receive window */
#define CSI_OFIFO		0x1C	/* CSI transmit window */
#define CSI_FIFOTRG		0x20	/* CSI FIFO trigger level */

/* CSI_MODE */
#define CSI_MODE_CSIE		BIT(7)
#define CSI_MODE_TRMD		BIT(6)
#define CSI_MODE_CCL		BIT(5)
#define CSI_MODE_DIR		BIT(4)
#define CSI_MODE_CSOT		BIT(0)

#define CSI_MODE_SETUP		0x00000040

/* CSI_CLKSEL */
#define CSI_CLKSEL_CKP		BIT(17)
#define CSI_CLKSEL_DAP		BIT(16)
#define CSI_CLKSEL_SLAVE	BIT(15)
#define CSI_CLKSEL_CKS		GENMASK(14, 1)

/* CSI_CNT */
#define CSI_CNT_CSIRST		BIT(28)
#define CSI_CNT_R_TRGEN		BIT(19)
#define CSI_CNT_UNDER_E		BIT(13)
#define CSI_CNT_OVERF_E		BIT(12)
#define CSI_CNT_TREND_E		BIT(9)
#define CSI_CNT_CSIEND_E	BIT(8)
#define CSI_CNT_T_TRGR_E	BIT(4)
#define CSI_CNT_R_TRGR_E	BIT(0)

/* CSI_INT */
#define CSI_INT_UNDER		BIT(13)
#define CSI_INT_OVERF		BIT(12)
#define CSI_INT_TREND		BIT(9)
#define CSI_INT_CSIEND		BIT(8)
#define CSI_INT_T_TRGR		BIT(4)
#define CSI_INT_R_TRGR		BIT(0)

/* CSI_FIFOTRG */
#define CSI_FIFOTRG_R_TRG       GENMASK(2, 0)

#define CSI_FIFO_SIZE_BYTES	32
#define CSI_FIFO_HALF_SIZE	16
#define CSI_EN_DIS_TIMEOUT_US	100
#define CSI_CKS_MAX		0x3FFF

#define UNDERRUN_ERROR		BIT(0)
#define OVERFLOW_ERROR		BIT(1)
#define TX_TIMEOUT_ERROR	BIT(2)
#define RX_TIMEOUT_ERROR	BIT(3)

#define CSI_MAX_SPI_SCKO	8000000

struct rzv2m_csi_priv {
	void __iomem *base;
	struct clk *csiclk;
	struct clk *pclk;
	struct device *dev;
	struct spi_controller *controller;
	const u8 *txbuf;
	u8 *rxbuf;
	int buffer_len;
	int bytes_sent;
	int bytes_received;
	int bytes_to_transfer;
	int words_to_transfer;
	unsigned char bytes_per_word;
	wait_queue_head_t wait;
	u8 errors;
	u32 status;
};

static const unsigned char x_trg[] = {
	0, 1, 1, 2, 2, 2, 2, 3,
	3, 3, 3, 3, 3, 3, 3, 4,
	4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 5
};

static const unsigned char x_trg_words[] = {
	1,  2,  2,  4,  4,  4,  4,  8,
	8,  8,  8,  8,  8,  8,  8,  16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 32
};

static void rzv2m_csi_reg_write_bit(const struct rzv2m_csi_priv *csi,
				    int reg_offs, int bit_mask, u32 value)
{
	int nr_zeros;
	u32 tmp;

	nr_zeros = count_trailing_zeros(bit_mask);
	value <<= nr_zeros;

	tmp = (readl(csi->base + reg_offs) & ~bit_mask) | value;
	writel(tmp, csi->base + reg_offs);
}

static int rzv2m_csi_sw_reset(struct rzv2m_csi_priv *csi, int assert)
{
	u32 reg;

	rzv2m_csi_reg_write_bit(csi, CSI_CNT, CSI_CNT_CSIRST, assert);

	if (assert) {
		return readl_poll_timeout(csi->base + CSI_MODE, reg,
					  !(reg & CSI_MODE_CSOT), 0,
					  CSI_EN_DIS_TIMEOUT_US);
	}

	return 0;
}

static int rzv2m_csi_start_stop_operation(const struct rzv2m_csi_priv *csi,
					  int enable, bool wait)
{
	u32 reg;

	rzv2m_csi_reg_write_bit(csi, CSI_MODE, CSI_MODE_CSIE, enable);

	if (!enable && wait)
		return readl_poll_timeout(csi->base + CSI_MODE, reg,
					  !(reg & CSI_MODE_CSOT), 0,
					  CSI_EN_DIS_TIMEOUT_US);

	return 0;
}

static int rzv2m_csi_fill_txfifo(struct rzv2m_csi_priv *csi)
{
	int i;

	if (readl(csi->base + CSI_OFIFOL))
		return -EIO;

	if (csi->bytes_per_word == 2) {
		u16 *buf = (u16 *)csi->txbuf;

		for (i = 0; i < csi->words_to_transfer; i++)
			writel(buf[i], csi->base + CSI_OFIFO);
	} else {
		u8 *buf = (u8 *)csi->txbuf;

		for (i = 0; i < csi->words_to_transfer; i++)
			writel(buf[i], csi->base + CSI_OFIFO);
	}

	csi->txbuf += csi->bytes_to_transfer;
	csi->bytes_sent += csi->bytes_to_transfer;

	return 0;
}

static int rzv2m_csi_read_rxfifo(struct rzv2m_csi_priv *csi)
{
	int i;

	if (readl(csi->base + CSI_IFIFOL) != csi->bytes_to_transfer)
		return -EIO;

	if (csi->bytes_per_word == 2) {
		u16 *buf = (u16 *)csi->rxbuf;

		for (i = 0; i < csi->words_to_transfer; i++)
			buf[i] = (u16)readl(csi->base + CSI_IFIFO);
	} else {
		u8 *buf = (u8 *)csi->rxbuf;

		for (i = 0; i < csi->words_to_transfer; i++)
			buf[i] = (u8)readl(csi->base + CSI_IFIFO);
	}

	csi->rxbuf += csi->bytes_to_transfer;
	csi->bytes_received += csi->bytes_to_transfer;

	return 0;
}

static inline void rzv2m_csi_calc_current_transfer(struct rzv2m_csi_priv *csi)
{
	int bytes_transferred = max_t(int, csi->bytes_received, csi->bytes_sent);
	int bytes_remaining = csi->buffer_len - bytes_transferred;
	int to_transfer;

	if (csi->txbuf)
		/*
		 * Leaving a little bit of headroom in the FIFOs makes it very
		 * hard to raise an overflow error (which is only possible
		 * when IP transmits and receives at the same time).
		 */
		to_transfer = min_t(int, CSI_FIFO_HALF_SIZE, bytes_remaining);
	else
		to_transfer = min_t(int, CSI_FIFO_SIZE_BYTES, bytes_remaining);

	if (csi->bytes_per_word == 2)
		to_transfer >>= 1;

	/*
	 * We can only choose a trigger level from a predefined set of values.
	 * This will pick a value that is the greatest possible integer that's
	 * less than or equal to the number of bytes we need to transfer.
	 * This may result in multiple smaller transfers.
	 */
	csi->words_to_transfer = x_trg_words[to_transfer - 1];

	if (csi->bytes_per_word == 2)
		csi->bytes_to_transfer = csi->words_to_transfer << 1;
	else
		csi->bytes_to_transfer = csi->words_to_transfer;
}

static inline void rzv2m_csi_set_rx_fifo_trigger_level(struct rzv2m_csi_priv *csi)
{
	rzv2m_csi_reg_write_bit(csi, CSI_FIFOTRG, CSI_FIFOTRG_R_TRG,
				x_trg[csi->words_to_transfer - 1]);
}

static inline void rzv2m_csi_enable_rx_trigger(struct rzv2m_csi_priv *csi,
					       bool enable)
{
	rzv2m_csi_reg_write_bit(csi, CSI_CNT, CSI_CNT_R_TRGEN, enable);
}

static void rzv2m_csi_disable_irqs(const struct rzv2m_csi_priv *csi,
				   u32 enable_bits)
{
	u32 cnt = readl(csi->base + CSI_CNT);

	writel(cnt & ~enable_bits, csi->base + CSI_CNT);
}

static void rzv2m_csi_disable_all_irqs(struct rzv2m_csi_priv *csi)
{
	rzv2m_csi_disable_irqs(csi, CSI_CNT_R_TRGR_E | CSI_CNT_T_TRGR_E |
			       CSI_CNT_CSIEND_E | CSI_CNT_TREND_E |
			       CSI_CNT_OVERF_E | CSI_CNT_UNDER_E);
}

static inline void rzv2m_csi_clear_irqs(struct rzv2m_csi_priv *csi, u32 irqs)
{
	writel(irqs, csi->base + CSI_INT);
}

static void rzv2m_csi_clear_all_irqs(struct rzv2m_csi_priv *csi)
{
	rzv2m_csi_clear_irqs(csi, CSI_INT_UNDER | CSI_INT_OVERF |
			     CSI_INT_TREND | CSI_INT_CSIEND |  CSI_INT_T_TRGR |
			     CSI_INT_R_TRGR);
}

static void rzv2m_csi_enable_irqs(struct rzv2m_csi_priv *csi, u32 enable_bits)
{
	u32 cnt = readl(csi->base + CSI_CNT);

	writel(cnt | enable_bits, csi->base + CSI_CNT);
}

static int rzv2m_csi_wait_for_interrupt(struct rzv2m_csi_priv *csi,
					u32 wait_mask, u32 enable_bits)
{
	int ret;

	rzv2m_csi_enable_irqs(csi, enable_bits);

	ret = wait_event_timeout(csi->wait,
				 ((csi->status & wait_mask) == wait_mask) ||
				 csi->errors, HZ);

	rzv2m_csi_disable_irqs(csi, enable_bits);

	if (csi->errors)
		return -EIO;

	if (!ret)
		return -ETIMEDOUT;

	return 0;
}

static int rzv2m_csi_wait_for_tx_empty(struct rzv2m_csi_priv *csi)
{
	int ret;

	if (readl(csi->base + CSI_OFIFOL) == 0)
		return 0;

	ret = rzv2m_csi_wait_for_interrupt(csi, CSI_INT_TREND, CSI_CNT_TREND_E);

	if (ret == -ETIMEDOUT)
		csi->errors |= TX_TIMEOUT_ERROR;

	return ret;
}

static inline int rzv2m_csi_wait_for_rx_ready(struct rzv2m_csi_priv *csi)
{
	int ret;

	if (readl(csi->base + CSI_IFIFOL) == csi->bytes_to_transfer)
		return 0;

	ret = rzv2m_csi_wait_for_interrupt(csi, CSI_INT_R_TRGR,
					   CSI_CNT_R_TRGR_E);

	if (ret == -ETIMEDOUT)
		csi->errors |= RX_TIMEOUT_ERROR;

	return ret;
}

static irqreturn_t rzv2m_csi_irq_handler(int irq, void *data)
{
	struct rzv2m_csi_priv *csi = (struct rzv2m_csi_priv *)data;

	csi->status = readl(csi->base + CSI_INT);
	rzv2m_csi_disable_irqs(csi, csi->status);

	if (csi->status & CSI_INT_OVERF)
		csi->errors |= OVERFLOW_ERROR;
	if (csi->status & CSI_INT_UNDER)
		csi->errors |= UNDERRUN_ERROR;

	wake_up(&csi->wait);

	return IRQ_HANDLED;
}

static void rzv2m_csi_setup_clock(struct rzv2m_csi_priv *csi, u32 spi_hz)
{
	unsigned long csiclk_rate = clk_get_rate(csi->csiclk);
	unsigned long pclk_rate = clk_get_rate(csi->pclk);
	unsigned long csiclk_rate_limit = pclk_rate >> 1;
	u32 cks;

	/*
	 * There is a restriction on the frequency of CSICLK, it has to be <=
	 * PCLK / 2.
	 */
	if (csiclk_rate > csiclk_rate_limit) {
		clk_set_rate(csi->csiclk, csiclk_rate >> 1);
		csiclk_rate = clk_get_rate(csi->csiclk);
	} else if ((csiclk_rate << 1) <= csiclk_rate_limit) {
		clk_set_rate(csi->csiclk, csiclk_rate << 1);
		csiclk_rate = clk_get_rate(csi->csiclk);
	}

	spi_hz = spi_hz > CSI_MAX_SPI_SCKO ? CSI_MAX_SPI_SCKO : spi_hz;

	cks = DIV_ROUND_UP(csiclk_rate, spi_hz << 1);
	if (cks > CSI_CKS_MAX)
		cks = CSI_CKS_MAX;

	dev_dbg(csi->dev, "SPI clk rate is %ldHz\n", csiclk_rate / (cks << 1));

	rzv2m_csi_reg_write_bit(csi, CSI_CLKSEL, CSI_CLKSEL_CKS, cks);
}

static void rzv2m_csi_setup_operating_mode(struct rzv2m_csi_priv *csi,
					   struct spi_transfer *t)
{
	if (t->rx_buf && !t->tx_buf)
		/* Reception-only mode */
		rzv2m_csi_reg_write_bit(csi, CSI_MODE, CSI_MODE_TRMD, 0);
	else
		/* Send and receive mode */
		rzv2m_csi_reg_write_bit(csi, CSI_MODE, CSI_MODE_TRMD, 1);

	csi->bytes_per_word = t->bits_per_word / 8;
	rzv2m_csi_reg_write_bit(csi, CSI_MODE, CSI_MODE_CCL,
				csi->bytes_per_word == 2);
}

static int rzv2m_csi_setup(struct spi_device *spi)
{
	struct rzv2m_csi_priv *csi = spi_controller_get_devdata(spi->controller);
	int ret;

	rzv2m_csi_sw_reset(csi, 0);

	writel(CSI_MODE_SETUP, csi->base + CSI_MODE);

	/* Setup clock polarity and phase timing */
	rzv2m_csi_reg_write_bit(csi, CSI_CLKSEL, CSI_CLKSEL_CKP,
				!(spi->mode & SPI_CPOL));
	rzv2m_csi_reg_write_bit(csi, CSI_CLKSEL, CSI_CLKSEL_DAP,
				!(spi->mode & SPI_CPHA));

	/* Setup serial data order */
	rzv2m_csi_reg_write_bit(csi, CSI_MODE, CSI_MODE_DIR,
				!!(spi->mode & SPI_LSB_FIRST));

	/* Set the operation mode as master */
	rzv2m_csi_reg_write_bit(csi, CSI_CLKSEL, CSI_CLKSEL_SLAVE, 0);

	/* Give the IP a SW reset */
	ret = rzv2m_csi_sw_reset(csi, 1);
	if (ret)
		return ret;
	rzv2m_csi_sw_reset(csi, 0);

	/*
	 * We need to enable the communication so that the clock will settle
	 * for the right polarity before enabling the CS.
	 */
	rzv2m_csi_start_stop_operation(csi, 1, false);
	udelay(10);
	rzv2m_csi_start_stop_operation(csi, 0, false);

	return 0;
}

static int rzv2m_csi_pio_transfer(struct rzv2m_csi_priv *csi)
{
	bool tx_completed = csi->txbuf ? false : true;
	bool rx_completed = csi->rxbuf ? false : true;
	int ret = 0;

	/* Make sure the TX FIFO is empty */
	writel(0, csi->base + CSI_OFIFOL);

	csi->bytes_sent = 0;
	csi->bytes_received = 0;
	csi->errors = 0;

	rzv2m_csi_disable_all_irqs(csi);
	rzv2m_csi_clear_all_irqs(csi);
	rzv2m_csi_enable_rx_trigger(csi, true);

	while (!tx_completed || !rx_completed) {
		/*
		 * Decide how many words we are going to transfer during
		 * this cycle (for both TX and RX), then set the RX FIFO trigger
		 * level accordingly. No need to set a trigger level for the
		 * TX FIFO, as this IP comes with an interrupt that fires when
		 * the TX FIFO is empty.
		 */
		rzv2m_csi_calc_current_transfer(csi);
		rzv2m_csi_set_rx_fifo_trigger_level(csi);

		rzv2m_csi_enable_irqs(csi, CSI_INT_OVERF | CSI_INT_UNDER);

		/* Make sure the RX FIFO is empty */
		writel(0, csi->base + CSI_IFIFOL);

		writel(readl(csi->base + CSI_INT), csi->base + CSI_INT);
		csi->status = 0;

		rzv2m_csi_start_stop_operation(csi, 1, false);

		/* TX */
		if (csi->txbuf) {
			ret = rzv2m_csi_fill_txfifo(csi);
			if (ret)
				break;

			ret = rzv2m_csi_wait_for_tx_empty(csi);
			if (ret)
				break;

			if (csi->bytes_sent == csi->buffer_len)
				tx_completed = true;
		}

		/*
		 * Make sure the RX FIFO contains the desired number of words.
		 * We then either flush its content, or we copy it onto
		 * csi->rxbuf.
		 */
		ret = rzv2m_csi_wait_for_rx_ready(csi);
		if (ret)
			break;

		/* RX */
		if (csi->rxbuf) {
			rzv2m_csi_start_stop_operation(csi, 0, false);

			ret = rzv2m_csi_read_rxfifo(csi);
			if (ret)
				break;

			if (csi->bytes_received == csi->buffer_len)
				rx_completed = true;
		}

		ret = rzv2m_csi_start_stop_operation(csi, 0, true);
		if (ret)
			goto pio_quit;

		if (csi->errors) {
			ret = -EIO;
			goto pio_quit;
		}
	}

	rzv2m_csi_start_stop_operation(csi, 0, true);

pio_quit:
	rzv2m_csi_disable_all_irqs(csi);
	rzv2m_csi_enable_rx_trigger(csi, false);
	rzv2m_csi_clear_all_irqs(csi);

	return ret;
}

static int rzv2m_csi_transfer_one(struct spi_controller *controller,
				  struct spi_device *spi,
				  struct spi_transfer *transfer)
{
	struct rzv2m_csi_priv *csi = spi_controller_get_devdata(controller);
	struct device *dev = csi->dev;
	int ret;

	csi->txbuf = transfer->tx_buf;
	csi->rxbuf = transfer->rx_buf;
	csi->buffer_len = transfer->len;

	rzv2m_csi_setup_operating_mode(csi, transfer);

	rzv2m_csi_setup_clock(csi, transfer->speed_hz);

	ret = rzv2m_csi_pio_transfer(csi);
	if (ret) {
		if (csi->errors & UNDERRUN_ERROR)
			dev_err(dev, "Underrun error\n");
		if (csi->errors & OVERFLOW_ERROR)
			dev_err(dev, "Overflow error\n");
		if (csi->errors & TX_TIMEOUT_ERROR)
			dev_err(dev, "TX timeout error\n");
		if (csi->errors & RX_TIMEOUT_ERROR)
			dev_err(dev, "RX timeout error\n");
	}

	return ret;
}

static int rzv2m_csi_probe(struct platform_device *pdev)
{
	struct spi_controller *controller;
	struct device *dev = &pdev->dev;
	struct rzv2m_csi_priv *csi;
	struct reset_control *rstc;
	int irq;
	int ret;

	controller = devm_spi_alloc_master(dev, sizeof(*csi));
	if (!controller)
		return -ENOMEM;

	csi = spi_controller_get_devdata(controller);
	platform_set_drvdata(pdev, csi);

	csi->dev = dev;
	csi->controller = controller;

	csi->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi->base))
		return PTR_ERR(csi->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	csi->csiclk = devm_clk_get(dev, "csiclk");
	if (IS_ERR(csi->csiclk))
		return dev_err_probe(dev, PTR_ERR(csi->csiclk),
				     "could not get csiclk\n");

	csi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(csi->pclk))
		return dev_err_probe(dev, PTR_ERR(csi->pclk),
				     "could not get pclk\n");

	rstc = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(rstc))
		return dev_err_probe(dev, PTR_ERR(rstc), "Missing reset ctrl\n");

	init_waitqueue_head(&csi->wait);

	controller->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;
	controller->dev.of_node = pdev->dev.of_node;
	controller->bits_per_word_mask = SPI_BPW_MASK(16) | SPI_BPW_MASK(8);
	controller->setup = rzv2m_csi_setup;
	controller->transfer_one = rzv2m_csi_transfer_one;
	controller->use_gpio_descriptors = true;

	ret = devm_request_irq(dev, irq, rzv2m_csi_irq_handler, 0,
			       dev_name(dev), csi);
	if (ret)
		return dev_err_probe(dev, ret, "cannot request IRQ\n");

	/*
	 * The reset also affects other HW that is not under the control
	 * of Linux. Therefore, all we can do is make sure the reset is
	 * deasserted.
	 */
	reset_control_deassert(rstc);

	/* Make sure the IP is in SW reset state */
	ret = rzv2m_csi_sw_reset(csi, 1);
	if (ret)
		return ret;

	ret = clk_prepare_enable(csi->csiclk);
	if (ret)
		return dev_err_probe(dev, ret, "could not enable csiclk\n");

	ret = spi_register_controller(controller);
	if (ret) {
		clk_disable_unprepare(csi->csiclk);
		return dev_err_probe(dev, ret, "register controller failed\n");
	}

	return 0;
}

static void rzv2m_csi_remove(struct platform_device *pdev)
{
	struct rzv2m_csi_priv *csi = platform_get_drvdata(pdev);

	spi_unregister_controller(csi->controller);
	rzv2m_csi_sw_reset(csi, 1);
	clk_disable_unprepare(csi->csiclk);
}

static const struct of_device_id rzv2m_csi_match[] = {
	{ .compatible = "renesas,rzv2m-csi" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzv2m_csi_match);

static struct platform_driver rzv2m_csi_drv = {
	.probe = rzv2m_csi_probe,
	.remove_new = rzv2m_csi_remove,
	.driver = {
		.name = "rzv2m_csi",
		.of_match_table = rzv2m_csi_match,
	},
};
module_platform_driver(rzv2m_csi_drv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fabrizio Castro <castro.fabrizio.jz@renesas.com>");
MODULE_DESCRIPTION("Clocked Serial Interface Driver");
