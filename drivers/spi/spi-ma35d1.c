// SPDX-License-Identifier: GPL-2.0-or-later
//
// Nuvoton MA35D1 SPI controller driver
//
// Copyright (c) 2026 Nuvoton Technology Corp.
// Author: Chi-Wen Weng <cwweng@nuvoton.com>

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/sizes.h>
#include <linux/spi/spi.h>
#include <linux/time.h>
#include <linux/unaligned.h>

/* Register offset definitions */
#define NUVOTON_SPI_CTL_OFFSET			0x00
#define NUVOTON_SPI_CLKDIV_OFFSET		0x04
#define NUVOTON_SPI_SSCTL_OFFSET		0x08
#define NUVOTON_SPI_PDMACTL_OFFSET		0x0c
#define NUVOTON_SPI_FIFOCTL_OFFSET		0x10
#define NUVOTON_SPI_STATUS_OFFSET		0x14
#define NUVOTON_SPI_TX_OFFSET			0x20
#define NUVOTON_SPI_RX_OFFSET			0x30

/* SPI Control Register bit masks */
#define NUVOTON_SPI_CTL_DATDIR_MASK		BIT(20)
#define NUVOTON_SPI_CTL_REORDER_MASK		BIT(19)
#define NUVOTON_SPI_CTL_SLAVE_MASK		BIT(18)
#define NUVOTON_SPI_CTL_UNITIEN_MASK		BIT(17)
#define NUVOTON_SPI_CTL_RXONLY_MASK		BIT(15)
#define NUVOTON_SPI_CTL_HALFDPX_MASK		BIT(14)
#define NUVOTON_SPI_CTL_LSB_MASK		BIT(13)
#define NUVOTON_SPI_CTL_DWIDTH_MASK		GENMASK(12, 8)
#define NUVOTON_SPI_CTL_SUSPITV_MASK		GENMASK(7, 4)
#define NUVOTON_SPI_CTL_CLKPOL_MASK		BIT(3)
#define NUVOTON_SPI_CTL_TXNEG_MASK		BIT(2)
#define NUVOTON_SPI_CTL_RXNEG_MASK		BIT(1)
#define NUVOTON_SPI_CTL_SPIEN_MASK		BIT(0)

/* SPI Clock Divider Register bit masks */
#define NUVOTON_SPI_CLKDIV_MASK			GENMASK(8, 0)

/* SPI Slave Select Control Register bit masks */
#define NUVOTON_SPI_SSCTL_SS1_MASK		BIT(1)
#define NUVOTON_SPI_SSCTL_SS0_MASK		BIT(0)
#define NUVOTON_SPI_SSCTL_SSACTPOL_MASK		BIT(2)
#define NUVOTON_SPI_SSCTL_AUTOSS_MASK		BIT(3)
#define NUVOTON_SPI_SSCTL_SLV3WIRE_MASK		BIT(4)
#define NUVOTON_SPI_SSCTL_SLVBEIEN_MASK		BIT(8)
#define NUVOTON_SPI_SSCTL_SLVURIEN_MASK		BIT(9)
#define NUVOTON_SPI_SSCTL_SSACTIEN_MASK		BIT(12)
#define NUVOTON_SPI_SSCTL_SSINAIEN_MASK		BIT(13)

/* SPI PDMA Control Register bit masks */
#define NUVOTON_SPI_PDMACTL_TXPDMAEN_MASK	BIT(0)
#define NUVOTON_SPI_PDMACTL_RXPDMAEN_MASK	BIT(1)

/* SPI FIFO Control Register bit masks */
#define NUVOTON_SPI_FIFOCTL_SLVBERX_MASK	BIT(10)
#define NUVOTON_SPI_FIFOCTL_TXUFIEN_MASK	BIT(7)
#define NUVOTON_SPI_FIFOCTL_TXUFPOL_MASK	BIT(6)
#define NUVOTON_SPI_FIFOCTL_RXOVIEN_MASK	BIT(5)
#define NUVOTON_SPI_FIFOCTL_RXTOIEN_MASK	BIT(4)
#define NUVOTON_SPI_FIFOCTL_TXTHIEN_MASK	BIT(3)
#define NUVOTON_SPI_FIFOCTL_RXTHIEN_MASK	BIT(2)
#define NUVOTON_SPI_FIFOCTL_TXRST_MASK		BIT(1)
#define NUVOTON_SPI_FIFOCTL_RXRST_MASK		BIT(0)

/* SPI Status Register bit masks */
#define NUVOTON_SPI_STATUS_TXRXRST_MASK		BIT(23)
#define NUVOTON_SPI_STATUS_TXFULL_MASK		BIT(17)
#define NUVOTON_SPI_STATUS_SPIENSTS_MASK	BIT(15)
#define NUVOTON_SPI_STATUS_RXEMPTY_MASK		BIT(8)
#define NUVOTON_SPI_STATUS_BUSY_MASK		BIT(0)

#define NUVOTON_SPI_MAX_NATIVE_CS		2
#define NUVOTON_SPI_DEFAULT_NUM_CS		2
#define NUVOTON_SPI_DEFAULT_BPW			8
#define NUVOTON_SPI_MAX_SPEED_HZ		100000000U
#define NUVOTON_SPI_MIN_DIVISOR			2U
#define NUVOTON_SPI_MAX_DIVISOR			512U
#define NUVOTON_SPI_RESET_CYCLES			5U

/* Bound PIO operations to avoid long polling loops. */
#define NUVOTON_SPI_MAX_TRANSFER_SIZE		SZ_4K
#define NUVOTON_SPI_TIMEOUT_US			10000

struct nuvoton_spi {
	void __iomem *regs;
	struct device *dev;
	unsigned long clk_rate;
};

static u32 nuvoton_spi_read(struct nuvoton_spi *nspi, u32 reg)
{
	return readl(nspi->regs + reg);
}

static void nuvoton_spi_write(struct nuvoton_spi *nspi, u32 val, u32 reg)
{
	writel(val, nspi->regs + reg);
}

static void nuvoton_spi_update_bits(struct nuvoton_spi *nspi, u32 reg,
				    u32 mask, u32 val)
{
	u32 tmp;

	tmp = nuvoton_spi_read(nspi, reg);
	tmp &= ~mask;
	tmp |= val & mask;
	nuvoton_spi_write(nspi, tmp, reg);
}

static int nuvoton_spi_disable(struct nuvoton_spi *nspi)
{
	u32 ctl;
	u32 val;

	ctl = nuvoton_spi_read(nspi, NUVOTON_SPI_CTL_OFFSET);
	if (!(ctl & NUVOTON_SPI_CTL_SPIEN_MASK))
		return 0;

	nuvoton_spi_write(nspi, ctl & ~NUVOTON_SPI_CTL_SPIEN_MASK,
			  NUVOTON_SPI_CTL_OFFSET);

	return readl_poll_timeout_atomic(nspi->regs + NUVOTON_SPI_STATUS_OFFSET,
					 val,
					 !(val & NUVOTON_SPI_STATUS_SPIENSTS_MASK),
					 1, NUVOTON_SPI_TIMEOUT_US);
}

static int nuvoton_spi_enable(struct nuvoton_spi *nspi)
{
	u32 ctl;
	u32 val;

	ctl = nuvoton_spi_read(nspi, NUVOTON_SPI_CTL_OFFSET);
	if (ctl & NUVOTON_SPI_CTL_SPIEN_MASK)
		return 0;

	nuvoton_spi_write(nspi, ctl | NUVOTON_SPI_CTL_SPIEN_MASK,
			  NUVOTON_SPI_CTL_OFFSET);

	return readl_poll_timeout_atomic(nspi->regs + NUVOTON_SPI_STATUS_OFFSET,
					 val,
					 val & NUVOTON_SPI_STATUS_SPIENSTS_MASK,
					 1, NUVOTON_SPI_TIMEOUT_US);
}

static int nuvoton_spi_wait_ready(struct nuvoton_spi *nspi)
{
	u32 val;

	return readl_poll_timeout(nspi->regs + NUVOTON_SPI_STATUS_OFFSET, val,
				  !(val & NUVOTON_SPI_STATUS_BUSY_MASK),
				  0, NUVOTON_SPI_TIMEOUT_US);
}

static int nuvoton_spi_reset_fifo(struct nuvoton_spi *nspi)
{
	u32 val;

	nuvoton_spi_update_bits(nspi, NUVOTON_SPI_FIFOCTL_OFFSET,
				NUVOTON_SPI_FIFOCTL_TXRST_MASK |
				NUVOTON_SPI_FIFOCTL_RXRST_MASK,
				NUVOTON_SPI_FIFOCTL_TXRST_MASK |
				NUVOTON_SPI_FIFOCTL_RXRST_MASK);

	/*
	 * Give the controller a short time to latch the FIFO reset request
	 * before polling the reset status bit.
	 */
	udelay(1);

	return readl_poll_timeout(nspi->regs + NUVOTON_SPI_STATUS_OFFSET, val,
				  !(val & NUVOTON_SPI_STATUS_TXRXRST_MASK),
				  1, NUVOTON_SPI_TIMEOUT_US);
}

static int nuvoton_spi_wait_tx_not_full(struct nuvoton_spi *nspi)
{
	u32 val;

	return readl_poll_timeout(nspi->regs + NUVOTON_SPI_STATUS_OFFSET, val,
				  !(val & NUVOTON_SPI_STATUS_TXFULL_MASK),
				  0, NUVOTON_SPI_TIMEOUT_US);
}

static int nuvoton_spi_wait_rx_not_empty(struct nuvoton_spi *nspi)
{
	u32 val;

	return readl_poll_timeout(nspi->regs + NUVOTON_SPI_STATUS_OFFSET, val,
				  !(val & NUVOTON_SPI_STATUS_RXEMPTY_MASK),
				  0, NUVOTON_SPI_TIMEOUT_US);
}

static int nuvoton_spi_calc_divisor(unsigned long clk_rate, u32 speed_hz,
				    unsigned int *divisor)
{
	unsigned int div;

	div = DIV_ROUND_UP(clk_rate, speed_hz);
	if (div < NUVOTON_SPI_MIN_DIVISOR)
		div = NUVOTON_SPI_MIN_DIVISOR;

	/*
	 * CLKDIV only accepts odd register values, corresponding to even
	 * clock divisors. Round up so the generated clock never exceeds
	 * the requested frequency.
	 */
	if (div & 1)
		div++;

	if (div > NUVOTON_SPI_MAX_DIVISOR)
		return -EINVAL;

	*divisor = div;

	return 0;
}

static int nuvoton_spi_configure_mode(struct nuvoton_spi *nspi,
				      struct spi_device *spi)
{
	u32 mask = NUVOTON_SPI_CTL_TXNEG_MASK |
		   NUVOTON_SPI_CTL_RXNEG_MASK |
		   NUVOTON_SPI_CTL_CLKPOL_MASK |
		   NUVOTON_SPI_CTL_LSB_MASK;
	u32 ctl = 0;
	u32 cur_ctl;
	u32 mode;
	int ret;

	mode = spi->mode & SPI_MODE_X_MASK;
	if (mode == SPI_MODE_0 || mode == SPI_MODE_3)
		ctl |= NUVOTON_SPI_CTL_TXNEG_MASK;
	else
		ctl |= NUVOTON_SPI_CTL_RXNEG_MASK;

	if (spi->mode & SPI_CPOL)
		ctl |= NUVOTON_SPI_CTL_CLKPOL_MASK;

	if (spi->mode & SPI_LSB_FIRST)
		ctl |= NUVOTON_SPI_CTL_LSB_MASK;

	cur_ctl = nuvoton_spi_read(nspi, NUVOTON_SPI_CTL_OFFSET);
	if ((cur_ctl & mask) == ctl)
		return 0;

	ret = nuvoton_spi_disable(nspi);
	if (ret)
		return ret;

	nuvoton_spi_update_bits(nspi, NUVOTON_SPI_CTL_OFFSET, mask, ctl);

	return 0;
}

static int nuvoton_spi_configure_transfer(struct nuvoton_spi *nspi,
					  struct spi_transfer *xfer)
{
	u32 ctl_mask = NUVOTON_SPI_CTL_DWIDTH_MASK;
	u32 cur_clkdiv;
	u32 cur_ctl;
	u32 clkdiv;
	u32 dwidth;
	unsigned int divisor;
	int ret;

	ret = nuvoton_spi_calc_divisor(nspi->clk_rate, xfer->speed_hz,
				       &divisor);
	if (ret) {
		dev_err(nspi->dev, "unsupported SPI clock %u Hz\n",
			xfer->speed_hz);
		return ret;
	}

	clkdiv = FIELD_PREP(NUVOTON_SPI_CLKDIV_MASK, divisor - 1);
	dwidth = xfer->bits_per_word == 32 ? 0 : xfer->bits_per_word;
	dwidth = FIELD_PREP(NUVOTON_SPI_CTL_DWIDTH_MASK, dwidth);

	xfer->effective_speed_hz = nspi->clk_rate / divisor;

	cur_ctl = nuvoton_spi_read(nspi, NUVOTON_SPI_CTL_OFFSET);
	cur_clkdiv = nuvoton_spi_read(nspi, NUVOTON_SPI_CLKDIV_OFFSET);

	if ((cur_ctl & ctl_mask) == dwidth &&
	    (cur_clkdiv & NUVOTON_SPI_CLKDIV_MASK) == clkdiv)
		return 0;

	ret = nuvoton_spi_disable(nspi);
	if (ret)
		return ret;

	if ((cur_ctl & ctl_mask) != dwidth)
		nuvoton_spi_update_bits(nspi, NUVOTON_SPI_CTL_OFFSET,
					ctl_mask, dwidth);

	if ((cur_clkdiv & NUVOTON_SPI_CLKDIV_MASK) != clkdiv)
		nuvoton_spi_write(nspi, clkdiv, NUVOTON_SPI_CLKDIV_OFFSET);

	return 0;
}

static u32 nuvoton_spi_get_tx_word(const void *txbuf, unsigned int offset,
				   unsigned int bytes_per_word)
{
	if (!txbuf)
		return 0;

	switch (bytes_per_word) {
	case 1:
		return ((const u8 *)txbuf)[offset];
	case 2:
		return get_unaligned((const u16 *)((const u8 *)txbuf + offset));
	case 4:
		return get_unaligned((const u32 *)((const u8 *)txbuf + offset));
	default:
		return 0;
	}
}

static void nuvoton_spi_put_rx_word(void *rxbuf, unsigned int offset,
				    unsigned int bytes_per_word, u32 val)
{
	if (!rxbuf)
		return;

	switch (bytes_per_word) {
	case 1:
		((u8 *)rxbuf)[offset] = val;
		break;
	case 2:
		put_unaligned((u16)val, (u16 *)((u8 *)rxbuf + offset));
		break;
	case 4:
		put_unaligned(val, (u32 *)((u8 *)rxbuf + offset));
		break;
	}
}

static int nuvoton_spi_txrx(struct nuvoton_spi *nspi,
			    struct spi_transfer *xfer)
{
	unsigned int bytes_per_word;
	unsigned int offset;
	u32 val;
	int ret;

	bytes_per_word = spi_bpw_to_bytes(xfer->bits_per_word);

	/*
	 * Use conservative word-by-word PIO. Each transmitted word produces
	 * one receive FIFO entry, so always drain RX, including TX-only
	 * transfers. RX-only transfers send zero-filled dummy words.
	 */
	for (offset = 0; offset < xfer->len; offset += bytes_per_word) {
		ret = nuvoton_spi_wait_tx_not_full(nspi);
		if (ret) {
			dev_err(nspi->dev, "TX FIFO full timeout\n");
			return ret;
		}

		val = nuvoton_spi_get_tx_word(xfer->tx_buf, offset,
					      bytes_per_word);
		nuvoton_spi_write(nspi, val, NUVOTON_SPI_TX_OFFSET);

		ret = nuvoton_spi_wait_rx_not_empty(nspi);
		if (ret) {
			dev_err(nspi->dev, "RX FIFO empty timeout\n");
			return ret;
		}

		val = nuvoton_spi_read(nspi, NUVOTON_SPI_RX_OFFSET);
		nuvoton_spi_put_rx_word(xfer->rx_buf, offset, bytes_per_word,
					val);

		spi_delay_exec(&xfer->word_delay, xfer);
	}

	ret = nuvoton_spi_wait_ready(nspi);
	if (ret)
		dev_err(nspi->dev, "controller busy timeout\n");

	return ret;
}

static void nuvoton_spi_set_cs_level(struct nuvoton_spi *nspi,
				     unsigned int cs, bool assert)
{
	u32 mask;

	switch (cs) {
	case 0:
		mask = NUVOTON_SPI_SSCTL_SS0_MASK;
		break;
	case 1:
		mask = NUVOTON_SPI_SSCTL_SS1_MASK;
		break;
	default:
		dev_warn(nspi->dev, "invalid chip select %u\n", cs);
		return;
	}

	nuvoton_spi_update_bits(nspi, NUVOTON_SPI_SSCTL_OFFSET,
				mask, assert ? mask : 0);
}

static int nuvoton_spi_setup(struct spi_device *spi)
{
	unsigned int cs = spi_get_chipselect(spi, 0);

	if (spi_get_csgpiod(spi, 0))
		return 0;

	if (cs >= NUVOTON_SPI_MAX_NATIVE_CS) {
		dev_err(&spi->dev, "invalid native chip select %u\n", cs);
		return -EINVAL;
	}

	if (spi->mode & SPI_CS_HIGH) {
		dev_err(&spi->dev,
			"active-high native chip select is not supported\n");
		return -EINVAL;
	}

	return 0;
}

static void nuvoton_spi_set_cs(struct spi_device *spi, bool level)
{
	struct nuvoton_spi *nspi = spi_controller_get_devdata(spi->controller);
	int ret;

	/*
	 * SSCTL may only be changed while SPIEN is clear. Clearing SPIEN
	 * retains the CLK and MOSI output levels.
	 */
	ret = nuvoton_spi_disable(nspi);
	if (ret) {
		dev_err(nspi->dev, "failed to disable controller for chip select\n");
		return;
	}

	/*
	 * The SPI core passes the physical CS level to ->set_cs(). This
	 * initial driver only supports active-low native chip selects.
	 */
	nuvoton_spi_set_cs_level(nspi, spi_get_chipselect(spi, 0), !level);
}

static int nuvoton_spi_prepare_message(struct spi_controller *ctlr,
				       struct spi_message *message)
{
	struct nuvoton_spi *nspi = spi_controller_get_devdata(ctlr);
	int ret;

	ret = nuvoton_spi_configure_mode(nspi, message->spi);
	if (ret)
		dev_err(nspi->dev, "failed to configure SPI mode\n");

	return ret;
}

static int nuvoton_spi_transfer_one(struct spi_controller *ctlr,
				    struct spi_device *spi,
				    struct spi_transfer *xfer)
{
	struct nuvoton_spi *nspi = spi_controller_get_devdata(ctlr);
	int ret;

	ret = nuvoton_spi_configure_transfer(nspi, xfer);
	if (ret)
		return ret;

	ret = nuvoton_spi_enable(nspi);
	if (ret) {
		dev_err(nspi->dev, "failed to enable controller\n");
		return ret;
	}

	return nuvoton_spi_txrx(nspi, xfer);
}

static void nuvoton_spi_handle_err(struct spi_controller *ctlr,
				   struct spi_message *message)
{
	struct nuvoton_spi *nspi = spi_controller_get_devdata(ctlr);
	int ret;

	ret = nuvoton_spi_disable(nspi);
	if (ret) {
		dev_err(nspi->dev,
			"failed to disable controller during recovery\n");
		return;
	}

	ret = nuvoton_spi_reset_fifo(nspi);
	if (ret)
		dev_err(nspi->dev, "failed to reset FIFO during recovery\n");
}

static size_t nuvoton_spi_max_transfer_size(struct spi_device *spi)
{
	return NUVOTON_SPI_MAX_TRANSFER_SIZE;
}

static int nuvoton_spi_hw_init(struct nuvoton_spi *nspi)
{
	u32 ctl_mask;
	u32 fifo_mask;
	u32 ssctl_mask;
	int ret;

	ret = nuvoton_spi_disable(nspi);
	if (ret) {
		dev_err(nspi->dev, "failed to disable controller\n");
		return ret;
	}

	ctl_mask = NUVOTON_SPI_CTL_DATDIR_MASK |
		   NUVOTON_SPI_CTL_REORDER_MASK |
		   NUVOTON_SPI_CTL_SLAVE_MASK |
		   NUVOTON_SPI_CTL_UNITIEN_MASK |
		   NUVOTON_SPI_CTL_RXONLY_MASK |
		   NUVOTON_SPI_CTL_HALFDPX_MASK |
		   NUVOTON_SPI_CTL_LSB_MASK |
		   NUVOTON_SPI_CTL_DWIDTH_MASK |
		   NUVOTON_SPI_CTL_SUSPITV_MASK |
		   NUVOTON_SPI_CTL_CLKPOL_MASK |
		   NUVOTON_SPI_CTL_TXNEG_MASK |
		   NUVOTON_SPI_CTL_RXNEG_MASK;

	nuvoton_spi_update_bits(nspi, NUVOTON_SPI_CTL_OFFSET, ctl_mask,
				NUVOTON_SPI_CTL_TXNEG_MASK |
				FIELD_PREP(NUVOTON_SPI_CTL_DWIDTH_MASK,
					   NUVOTON_SPI_DEFAULT_BPW));

	ssctl_mask = NUVOTON_SPI_SSCTL_SS0_MASK |
		     NUVOTON_SPI_SSCTL_SS1_MASK |
		     NUVOTON_SPI_SSCTL_SSACTPOL_MASK |
		     NUVOTON_SPI_SSCTL_AUTOSS_MASK |
		     NUVOTON_SPI_SSCTL_SLV3WIRE_MASK |
		     NUVOTON_SPI_SSCTL_SLVBEIEN_MASK |
		     NUVOTON_SPI_SSCTL_SLVURIEN_MASK |
		     NUVOTON_SPI_SSCTL_SSACTIEN_MASK |
		     NUVOTON_SPI_SSCTL_SSINAIEN_MASK;

	nuvoton_spi_update_bits(nspi, NUVOTON_SPI_SSCTL_OFFSET,
				ssctl_mask, 0);

	nuvoton_spi_update_bits(nspi, NUVOTON_SPI_PDMACTL_OFFSET,
				NUVOTON_SPI_PDMACTL_TXPDMAEN_MASK |
				NUVOTON_SPI_PDMACTL_RXPDMAEN_MASK, 0);

	fifo_mask = NUVOTON_SPI_FIFOCTL_SLVBERX_MASK |
		    NUVOTON_SPI_FIFOCTL_TXUFIEN_MASK |
		    NUVOTON_SPI_FIFOCTL_TXUFPOL_MASK |
		    NUVOTON_SPI_FIFOCTL_RXOVIEN_MASK |
		    NUVOTON_SPI_FIFOCTL_RXTOIEN_MASK |
		    NUVOTON_SPI_FIFOCTL_TXTHIEN_MASK |
		    NUVOTON_SPI_FIFOCTL_RXTHIEN_MASK;

	nuvoton_spi_update_bits(nspi, NUVOTON_SPI_FIFOCTL_OFFSET, fifo_mask, 0);

	ret = nuvoton_spi_reset_fifo(nspi);
	if (ret)
		dev_err(nspi->dev, "FIFO reset timed out\n");

	return ret;
}

static int nuvoton_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *ctlr;
	struct nuvoton_spi *nspi;
	struct reset_control *rst;
	struct clk *clk;
	unsigned int max_divisor;
	u32 num_cs = NUVOTON_SPI_DEFAULT_NUM_CS;
	int ret;

	if (device_property_read_bool(dev, "spi-slave"))
		return dev_err_probe(dev, -EOPNOTSUPP,
				     "target mode is not supported\n");

	ctlr = devm_spi_alloc_host(dev, sizeof(*nspi));
	if (!ctlr)
		return -ENOMEM;

	platform_set_drvdata(pdev, ctlr);

	nspi = spi_controller_get_devdata(ctlr);
	nspi->dev = dev;

	nspi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(nspi->regs))
		return PTR_ERR(nspi->regs);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk),
				     "failed to get and enable clock\n");

	nspi->clk_rate = clk_get_rate(clk);
	if (!nspi->clk_rate)
		return dev_err_probe(dev, -EINVAL, "invalid clock rate\n");

	rst = devm_reset_control_get_exclusive_deasserted(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst),
				     "failed to get and deassert reset\n");

	/*
	 * The hardware requires at least five peripheral clock cycles after
	 * reset deassertion before programming controller registers.
	 */
	udelay(DIV_ROUND_UP_ULL((u64)NUVOTON_SPI_RESET_CYCLES *
				USEC_PER_SEC, nspi->clk_rate));

	ret = device_property_read_u32(dev, "num-cs", &num_cs);
	if (ret && ret != -EINVAL)
		return dev_err_probe(dev, ret, "failed to read num-cs\n");

	if (!num_cs || num_cs > NUVOTON_SPI_MAX_NATIVE_CS)
		return dev_err_probe(dev, -EINVAL, "invalid num-cs %u\n",
				     num_cs);

	ctlr->num_chipselect = num_cs;
	ctlr->max_native_cs = NUVOTON_SPI_MAX_NATIVE_CS;
	ctlr->use_gpio_descriptors = true;
	ctlr->max_transfer_size = nuvoton_spi_max_transfer_size;
	ctlr->setup = nuvoton_spi_setup;
	ctlr->set_cs = nuvoton_spi_set_cs;
	ctlr->prepare_message = nuvoton_spi_prepare_message;
	ctlr->transfer_one = nuvoton_spi_transfer_one;
	ctlr->handle_err = nuvoton_spi_handle_err;
	ctlr->bits_per_word_mask = SPI_BPW_RANGE_MASK(8, 32);
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;
	ctlr->min_speed_hz = DIV_ROUND_UP(nspi->clk_rate,
					  NUVOTON_SPI_MAX_DIVISOR);

	ret = nuvoton_spi_calc_divisor(nspi->clk_rate,
				       NUVOTON_SPI_MAX_SPEED_HZ, &max_divisor);
	if (ret)
		return dev_err_probe(dev, ret,
				     "clock rate does not support SPI transfers\n");

	ctlr->max_speed_hz = nspi->clk_rate / max_divisor;

	ret = nuvoton_spi_hw_init(nspi);
	if (ret)
		return ret;

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register SPI controller\n");

	return 0;
}

static int nuvoton_spi_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);

	return spi_controller_suspend(ctlr);
}

static int nuvoton_spi_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);

	/*
	 * The MA35D1 SPI registers are retained across power-down and deep
	 * sleep, so the controller state does not need to be restored here.
	 */
	return spi_controller_resume(ctlr);
}

static DEFINE_SIMPLE_DEV_PM_OPS(nuvoton_spi_pm_ops, nuvoton_spi_suspend,
				nuvoton_spi_resume);

static const struct of_device_id nuvoton_spi_of_match[] = {
	{ .compatible = "nuvoton,ma35d1-spi" },
	{ }
};
MODULE_DEVICE_TABLE(of, nuvoton_spi_of_match);

static struct platform_driver nuvoton_spi_driver = {
	.driver = {
		.name = "ma35d1-spi",
		.of_match_table = nuvoton_spi_of_match,
		.pm = pm_sleep_ptr(&nuvoton_spi_pm_ops),
	},
	.probe = nuvoton_spi_probe,
};
module_platform_driver(nuvoton_spi_driver);

MODULE_DESCRIPTION("Nuvoton MA35D1 SPI controller driver");
MODULE_AUTHOR("Chi-Wen Weng <cwweng@nuvoton.com>");
MODULE_LICENSE("GPL");
