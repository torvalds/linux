/*
 * Copyright (C) 2003-2015 Broadcom Corporation
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 (GPL v2)
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/interrupt.h>

/* SPI Configuration Register */
#define XLP_SPI_CONFIG			0x00
#define XLP_SPI_CPHA			BIT(0)
#define XLP_SPI_CPOL			BIT(1)
#define XLP_SPI_CS_POL			BIT(2)
#define XLP_SPI_TXMISO_EN		BIT(3)
#define XLP_SPI_TXMOSI_EN		BIT(4)
#define XLP_SPI_RXMISO_EN		BIT(5)
#define XLP_SPI_CS_LSBFE		BIT(10)
#define XLP_SPI_RXCAP_EN		BIT(11)

/* SPI Frequency Divider Register */
#define XLP_SPI_FDIV			0x04

/* SPI Command Register */
#define XLP_SPI_CMD			0x08
#define XLP_SPI_CMD_IDLE_MASK		0x0
#define XLP_SPI_CMD_TX_MASK		0x1
#define XLP_SPI_CMD_RX_MASK		0x2
#define XLP_SPI_CMD_TXRX_MASK		0x3
#define XLP_SPI_CMD_CONT		BIT(4)
#define XLP_SPI_XFR_BITCNT_SHIFT	16

/* SPI Status Register */
#define XLP_SPI_STATUS			0x0c
#define XLP_SPI_XFR_PENDING		BIT(0)
#define XLP_SPI_XFR_DONE		BIT(1)
#define XLP_SPI_TX_INT			BIT(2)
#define XLP_SPI_RX_INT			BIT(3)
#define XLP_SPI_TX_UF			BIT(4)
#define XLP_SPI_RX_OF			BIT(5)
#define XLP_SPI_STAT_MASK		0x3f

/* SPI Interrupt Enable Register */
#define XLP_SPI_INTR_EN			0x10
#define XLP_SPI_INTR_DONE		BIT(0)
#define XLP_SPI_INTR_TXTH		BIT(1)
#define XLP_SPI_INTR_RXTH		BIT(2)
#define XLP_SPI_INTR_TXUF		BIT(3)
#define XLP_SPI_INTR_RXOF		BIT(4)

/* SPI FIFO Threshold Register */
#define XLP_SPI_FIFO_THRESH		0x14

/* SPI FIFO Word Count Register */
#define XLP_SPI_FIFO_WCNT		0x18
#define XLP_SPI_RXFIFO_WCNT_MASK	0xf
#define XLP_SPI_TXFIFO_WCNT_MASK	0xf0
#define XLP_SPI_TXFIFO_WCNT_SHIFT	4

/* SPI Transmit Data FIFO Register */
#define XLP_SPI_TXDATA_FIFO		0x1c

/* SPI Receive Data FIFO Register */
#define XLP_SPI_RXDATA_FIFO		0x20

/* SPI System Control Register */
#define XLP_SPI_SYSCTRL			0x100
#define XLP_SPI_SYS_RESET		BIT(0)
#define XLP_SPI_SYS_CLKDIS		BIT(1)
#define XLP_SPI_SYS_PMEN		BIT(8)

#define SPI_CS_OFFSET			0x40
#define XLP_SPI_TXRXTH			0x80
#define XLP_SPI_FIFO_SIZE		8
#define XLP_SPI_MAX_CS			4
#define XLP_SPI_DEFAULT_FREQ		133333333
#define XLP_SPI_FDIV_MIN		4
#define XLP_SPI_FDIV_MAX		65535
/*
 * SPI can transfer only 28 bytes properly at a time. So split the
 * transfer into 28 bytes size.
 */
#define XLP_SPI_XFER_SIZE		28

struct xlp_spi_priv {
	struct device		dev;		/* device structure */
	void __iomem		*base;		/* spi registers base address */
	const u8		*tx_buf;	/* tx data buffer */
	u8			*rx_buf;	/* rx data buffer */
	int			tx_len;		/* tx xfer length */
	int			rx_len;		/* rx xfer length */
	int			txerrors;	/* TXFIFO underflow count */
	int			rxerrors;	/* RXFIFO overflow count */
	int			cs;		/* slave device chip select */
	u32			spi_clk;	/* spi clock frequency */
	bool			cmd_cont;	/* cs active */
	struct completion	done;		/* completion notification */
};

static inline u32 xlp_spi_reg_read(struct xlp_spi_priv *priv,
				int cs, int regoff)
{
	return readl(priv->base + regoff + cs * SPI_CS_OFFSET);
}

static inline void xlp_spi_reg_write(struct xlp_spi_priv *priv, int cs,
				int regoff, u32 val)
{
	writel(val, priv->base + regoff + cs * SPI_CS_OFFSET);
}

static inline void xlp_spi_sysctl_write(struct xlp_spi_priv *priv,
				int regoff, u32 val)
{
	writel(val, priv->base + regoff);
}

/*
 * Setup global SPI_SYSCTRL register for all SPI channels.
 */
static void xlp_spi_sysctl_setup(struct xlp_spi_priv *xspi)
{
	int cs;

	for (cs = 0; cs < XLP_SPI_MAX_CS; cs++)
		xlp_spi_sysctl_write(xspi, XLP_SPI_SYSCTRL,
				XLP_SPI_SYS_RESET << cs);
	xlp_spi_sysctl_write(xspi, XLP_SPI_SYSCTRL, XLP_SPI_SYS_PMEN);
}

static int xlp_spi_setup(struct spi_device *spi)
{
	struct xlp_spi_priv *xspi;
	u32 fdiv, cfg;
	int cs;

	xspi = spi_master_get_devdata(spi->master);
	cs = spi->chip_select;
	/*
	 * The value of fdiv must be between 4 and 65535.
	 */
	fdiv = DIV_ROUND_UP(xspi->spi_clk, spi->max_speed_hz);
	if (fdiv > XLP_SPI_FDIV_MAX)
		fdiv = XLP_SPI_FDIV_MAX;
	else if (fdiv < XLP_SPI_FDIV_MIN)
		fdiv = XLP_SPI_FDIV_MIN;

	xlp_spi_reg_write(xspi, cs, XLP_SPI_FDIV, fdiv);
	xlp_spi_reg_write(xspi, cs, XLP_SPI_FIFO_THRESH, XLP_SPI_TXRXTH);
	cfg = xlp_spi_reg_read(xspi, cs, XLP_SPI_CONFIG);
	if (spi->mode & SPI_CPHA)
		cfg |= XLP_SPI_CPHA;
	else
		cfg &= ~XLP_SPI_CPHA;
	if (spi->mode & SPI_CPOL)
		cfg |= XLP_SPI_CPOL;
	else
		cfg &= ~XLP_SPI_CPOL;
	if (!(spi->mode & SPI_CS_HIGH))
		cfg |= XLP_SPI_CS_POL;
	else
		cfg &= ~XLP_SPI_CS_POL;
	if (spi->mode & SPI_LSB_FIRST)
		cfg |= XLP_SPI_CS_LSBFE;
	else
		cfg &= ~XLP_SPI_CS_LSBFE;

	cfg |= XLP_SPI_TXMOSI_EN | XLP_SPI_RXMISO_EN;
	if (fdiv == 4)
		cfg |= XLP_SPI_RXCAP_EN;
	xlp_spi_reg_write(xspi, cs, XLP_SPI_CONFIG, cfg);

	return 0;
}

static void xlp_spi_read_rxfifo(struct xlp_spi_priv *xspi)
{
	u32 rx_data, rxfifo_cnt;
	int i, j, nbytes;

	rxfifo_cnt = xlp_spi_reg_read(xspi, xspi->cs, XLP_SPI_FIFO_WCNT);
	rxfifo_cnt &= XLP_SPI_RXFIFO_WCNT_MASK;
	while (rxfifo_cnt) {
		rx_data = xlp_spi_reg_read(xspi, xspi->cs, XLP_SPI_RXDATA_FIFO);
		j = 0;
		nbytes = min(xspi->rx_len, 4);
		for (i = nbytes - 1; i >= 0; i--, j++)
			xspi->rx_buf[i] = (rx_data >> (j * 8)) & 0xff;

		xspi->rx_len -= nbytes;
		xspi->rx_buf += nbytes;
		rxfifo_cnt--;
	}
}

static void xlp_spi_fill_txfifo(struct xlp_spi_priv *xspi)
{
	u32 tx_data, txfifo_cnt;
	int i, j, nbytes;

	txfifo_cnt = xlp_spi_reg_read(xspi, xspi->cs, XLP_SPI_FIFO_WCNT);
	txfifo_cnt &= XLP_SPI_TXFIFO_WCNT_MASK;
	txfifo_cnt >>= XLP_SPI_TXFIFO_WCNT_SHIFT;
	while (xspi->tx_len && (txfifo_cnt < XLP_SPI_FIFO_SIZE)) {
		j = 0;
		tx_data = 0;
		nbytes = min(xspi->tx_len, 4);
		for (i = nbytes - 1; i >= 0; i--, j++)
			tx_data |= xspi->tx_buf[i] << (j * 8);

		xlp_spi_reg_write(xspi, xspi->cs, XLP_SPI_TXDATA_FIFO, tx_data);
		xspi->tx_len -= nbytes;
		xspi->tx_buf += nbytes;
		txfifo_cnt++;
	}
}

static irqreturn_t xlp_spi_interrupt(int irq, void *dev_id)
{
	struct xlp_spi_priv *xspi = dev_id;
	u32 stat;

	stat = xlp_spi_reg_read(xspi, xspi->cs, XLP_SPI_STATUS) &
		XLP_SPI_STAT_MASK;
	if (!stat)
		return IRQ_NONE;

	if (stat & XLP_SPI_TX_INT) {
		if (xspi->tx_len)
			xlp_spi_fill_txfifo(xspi);
		if (stat & XLP_SPI_TX_UF)
			xspi->txerrors++;
	}

	if (stat & XLP_SPI_RX_INT) {
		if (xspi->rx_len)
			xlp_spi_read_rxfifo(xspi);
		if (stat & XLP_SPI_RX_OF)
			xspi->rxerrors++;
	}

	/* write status back to clear interrupts */
	xlp_spi_reg_write(xspi, xspi->cs, XLP_SPI_STATUS, stat);
	if (stat & XLP_SPI_XFR_DONE)
		complete(&xspi->done);

	return IRQ_HANDLED;
}

static void xlp_spi_send_cmd(struct xlp_spi_priv *xspi, int xfer_len,
			int cmd_cont)
{
	u32 cmd = 0;

	if (xspi->tx_buf)
		cmd |= XLP_SPI_CMD_TX_MASK;
	if (xspi->rx_buf)
		cmd |= XLP_SPI_CMD_RX_MASK;
	if (cmd_cont)
		cmd |= XLP_SPI_CMD_CONT;
	cmd |= ((xfer_len * 8 - 1) << XLP_SPI_XFR_BITCNT_SHIFT);
	xlp_spi_reg_write(xspi, xspi->cs, XLP_SPI_CMD, cmd);
}

static int xlp_spi_xfer_block(struct  xlp_spi_priv *xs,
		const unsigned char *tx_buf,
		unsigned char *rx_buf, int xfer_len, int cmd_cont)
{
	int timeout;
	u32 intr_mask = 0;

	xs->tx_buf = tx_buf;
	xs->rx_buf = rx_buf;
	xs->tx_len = (xs->tx_buf == NULL) ? 0 : xfer_len;
	xs->rx_len = (xs->rx_buf == NULL) ? 0 : xfer_len;
	xs->txerrors = xs->rxerrors = 0;

	/* fill TXDATA_FIFO, then send the CMD */
	if (xs->tx_len)
		xlp_spi_fill_txfifo(xs);

	xlp_spi_send_cmd(xs, xfer_len, cmd_cont);

	/*
	 * We are getting some spurious tx interrupts, so avoid enabling
	 * tx interrupts when only rx is in process.
	 * Enable all the interrupts in tx case.
	 */
	if (xs->tx_len)
		intr_mask |= XLP_SPI_INTR_TXTH | XLP_SPI_INTR_TXUF |
				XLP_SPI_INTR_RXTH | XLP_SPI_INTR_RXOF;
	else
		intr_mask |= XLP_SPI_INTR_RXTH | XLP_SPI_INTR_RXOF;

	intr_mask |= XLP_SPI_INTR_DONE;
	xlp_spi_reg_write(xs, xs->cs, XLP_SPI_INTR_EN, intr_mask);

	timeout = wait_for_completion_timeout(&xs->done,
				msecs_to_jiffies(1000));
	/* Disable interrupts */
	xlp_spi_reg_write(xs, xs->cs, XLP_SPI_INTR_EN, 0x0);
	if (!timeout) {
		dev_err(&xs->dev, "xfer timedout!\n");
		goto out;
	}
	if (xs->txerrors || xs->rxerrors)
		dev_err(&xs->dev, "Over/Underflow rx %d tx %d xfer %d!\n",
				xs->rxerrors, xs->txerrors, xfer_len);

	return xfer_len;
out:
	return -ETIMEDOUT;
}

static int xlp_spi_txrx_bufs(struct xlp_spi_priv *xs, struct spi_transfer *t)
{
	int bytesleft, sz;
	unsigned char *rx_buf;
	const unsigned char *tx_buf;

	tx_buf = t->tx_buf;
	rx_buf = t->rx_buf;
	bytesleft = t->len;
	while (bytesleft) {
		if (bytesleft > XLP_SPI_XFER_SIZE)
			sz = xlp_spi_xfer_block(xs, tx_buf, rx_buf,
					XLP_SPI_XFER_SIZE, 1);
		else
			sz = xlp_spi_xfer_block(xs, tx_buf, rx_buf,
					bytesleft, xs->cmd_cont);
		if (sz < 0)
			return sz;
		bytesleft -= sz;
		if (tx_buf)
			tx_buf += sz;
		if (rx_buf)
			rx_buf += sz;
	}
	return bytesleft;
}

static int xlp_spi_transfer_one(struct spi_master *master,
					struct spi_device *spi,
					struct spi_transfer *t)
{
	struct xlp_spi_priv *xspi = spi_master_get_devdata(master);
	int ret = 0;

	xspi->cs = spi->chip_select;
	xspi->dev = spi->dev;

	if (spi_transfer_is_last(master, t))
		xspi->cmd_cont = 0;
	else
		xspi->cmd_cont = 1;

	if (xlp_spi_txrx_bufs(xspi, t))
		ret = -EIO;

	spi_finalize_current_transfer(master);
	return ret;
}

static int xlp_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct xlp_spi_priv *xspi;
	struct resource *res;
	struct clk *clk;
	int irq, err;

	xspi = devm_kzalloc(&pdev->dev, sizeof(*xspi), GFP_KERNEL);
	if (!xspi)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xspi->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xspi->base))
		return PTR_ERR(xspi->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no IRQ resource found\n");
		return -EINVAL;
	}
	err = devm_request_irq(&pdev->dev, irq, xlp_spi_interrupt, 0,
			pdev->name, xspi);
	if (err) {
		dev_err(&pdev->dev, "unable to request irq %d\n", irq);
		return err;
	}

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "could not get spi clock\n");
		return PTR_ERR(clk);
	}

	xspi->spi_clk = clk_get_rate(clk);

	master = spi_alloc_master(&pdev->dev, 0);
	if (!master) {
		dev_err(&pdev->dev, "could not alloc master\n");
		return -ENOMEM;
	}

	master->bus_num = 0;
	master->num_chipselect = XLP_SPI_MAX_CS;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->setup = xlp_spi_setup;
	master->transfer_one = xlp_spi_transfer_one;
	master->dev.of_node = pdev->dev.of_node;

	init_completion(&xspi->done);
	spi_master_set_devdata(master, xspi);
	xlp_spi_sysctl_setup(xspi);

	/* register spi controller */
	err = devm_spi_register_master(&pdev->dev, master);
	if (err) {
		dev_err(&pdev->dev, "spi register master failed!\n");
		spi_master_put(master);
		return err;
	}

	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id xlp_spi_acpi_match[] = {
	{ "BRCM900D", 0 },
	{ "CAV900D",  0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, xlp_spi_acpi_match);
#endif

static const struct of_device_id xlp_spi_dt_id[] = {
	{ .compatible = "netlogic,xlp832-spi" },
	{ },
};
MODULE_DEVICE_TABLE(of, xlp_spi_dt_id);

static struct platform_driver xlp_spi_driver = {
	.probe	= xlp_spi_probe,
	.driver = {
		.name	= "xlp-spi",
		.of_match_table = xlp_spi_dt_id,
		.acpi_match_table = ACPI_PTR(xlp_spi_acpi_match),
	},
};
module_platform_driver(xlp_spi_driver);

MODULE_AUTHOR("Kamlakant Patel <kamlakant.patel@broadcom.com>");
MODULE_DESCRIPTION("Netlogic XLP SPI controller driver");
MODULE_LICENSE("GPL v2");
