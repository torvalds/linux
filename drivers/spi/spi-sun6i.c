// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012 - 2014 Allwinner Tech
 * Pan Nan <pannan@allwinnertech.com>
 *
 * Copyright (C) 2014 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <linux/spi/spi.h>

#define SUN6I_FIFO_DEPTH		128
#define SUN8I_FIFO_DEPTH		64

#define SUN6I_GBL_CTL_REG		0x04
#define SUN6I_GBL_CTL_BUS_ENABLE		BIT(0)
#define SUN6I_GBL_CTL_MASTER			BIT(1)
#define SUN6I_GBL_CTL_TP			BIT(7)
#define SUN6I_GBL_CTL_RST			BIT(31)

#define SUN6I_TFR_CTL_REG		0x08
#define SUN6I_TFR_CTL_CPHA			BIT(0)
#define SUN6I_TFR_CTL_CPOL			BIT(1)
#define SUN6I_TFR_CTL_SPOL			BIT(2)
#define SUN6I_TFR_CTL_CS_MASK			0x30
#define SUN6I_TFR_CTL_CS(cs)			(((cs) << 4) & SUN6I_TFR_CTL_CS_MASK)
#define SUN6I_TFR_CTL_CS_MANUAL			BIT(6)
#define SUN6I_TFR_CTL_CS_LEVEL			BIT(7)
#define SUN6I_TFR_CTL_DHB			BIT(8)
#define SUN6I_TFR_CTL_FBS			BIT(12)
#define SUN6I_TFR_CTL_XCH			BIT(31)

#define SUN6I_INT_CTL_REG		0x10
#define SUN6I_INT_CTL_RF_RDY			BIT(0)
#define SUN6I_INT_CTL_TF_ERQ			BIT(4)
#define SUN6I_INT_CTL_RF_OVF			BIT(8)
#define SUN6I_INT_CTL_TC			BIT(12)

#define SUN6I_INT_STA_REG		0x14

#define SUN6I_FIFO_CTL_REG		0x18
#define SUN6I_FIFO_CTL_RF_RDY_TRIG_LEVEL_MASK	0xff
#define SUN6I_FIFO_CTL_RF_RDY_TRIG_LEVEL_BITS	0
#define SUN6I_FIFO_CTL_RF_RST			BIT(15)
#define SUN6I_FIFO_CTL_TF_ERQ_TRIG_LEVEL_MASK	0xff
#define SUN6I_FIFO_CTL_TF_ERQ_TRIG_LEVEL_BITS	16
#define SUN6I_FIFO_CTL_TF_RST			BIT(31)

#define SUN6I_FIFO_STA_REG		0x1c
#define SUN6I_FIFO_STA_RF_CNT_MASK		0x7f
#define SUN6I_FIFO_STA_RF_CNT_BITS		0
#define SUN6I_FIFO_STA_TF_CNT_MASK		0x7f
#define SUN6I_FIFO_STA_TF_CNT_BITS		16

#define SUN6I_CLK_CTL_REG		0x24
#define SUN6I_CLK_CTL_CDR2_MASK			0xff
#define SUN6I_CLK_CTL_CDR2(div)			(((div) & SUN6I_CLK_CTL_CDR2_MASK) << 0)
#define SUN6I_CLK_CTL_CDR1_MASK			0xf
#define SUN6I_CLK_CTL_CDR1(div)			(((div) & SUN6I_CLK_CTL_CDR1_MASK) << 8)
#define SUN6I_CLK_CTL_DRS			BIT(12)

#define SUN6I_MAX_XFER_SIZE		0xffffff

#define SUN6I_BURST_CNT_REG		0x30
#define SUN6I_BURST_CNT(cnt)			((cnt) & SUN6I_MAX_XFER_SIZE)

#define SUN6I_XMIT_CNT_REG		0x34
#define SUN6I_XMIT_CNT(cnt)			((cnt) & SUN6I_MAX_XFER_SIZE)

#define SUN6I_BURST_CTL_CNT_REG		0x38
#define SUN6I_BURST_CTL_CNT_STC(cnt)		((cnt) & SUN6I_MAX_XFER_SIZE)

#define SUN6I_TXDATA_REG		0x200
#define SUN6I_RXDATA_REG		0x300

struct sun6i_spi {
	struct spi_master	*master;
	void __iomem		*base_addr;
	struct clk		*hclk;
	struct clk		*mclk;
	struct reset_control	*rstc;

	struct completion	done;

	const u8		*tx_buf;
	u8			*rx_buf;
	int			len;
	unsigned long		fifo_depth;
};

static inline u32 sun6i_spi_read(struct sun6i_spi *sspi, u32 reg)
{
	return readl(sspi->base_addr + reg);
}

static inline void sun6i_spi_write(struct sun6i_spi *sspi, u32 reg, u32 value)
{
	writel(value, sspi->base_addr + reg);
}

static inline u32 sun6i_spi_get_tx_fifo_count(struct sun6i_spi *sspi)
{
	u32 reg = sun6i_spi_read(sspi, SUN6I_FIFO_STA_REG);

	reg >>= SUN6I_FIFO_STA_TF_CNT_BITS;

	return reg & SUN6I_FIFO_STA_TF_CNT_MASK;
}

static inline void sun6i_spi_enable_interrupt(struct sun6i_spi *sspi, u32 mask)
{
	u32 reg = sun6i_spi_read(sspi, SUN6I_INT_CTL_REG);

	reg |= mask;
	sun6i_spi_write(sspi, SUN6I_INT_CTL_REG, reg);
}

static inline void sun6i_spi_disable_interrupt(struct sun6i_spi *sspi, u32 mask)
{
	u32 reg = sun6i_spi_read(sspi, SUN6I_INT_CTL_REG);

	reg &= ~mask;
	sun6i_spi_write(sspi, SUN6I_INT_CTL_REG, reg);
}

static inline void sun6i_spi_drain_fifo(struct sun6i_spi *sspi, int len)
{
	u32 reg, cnt;
	u8 byte;

	/* See how much data is available */
	reg = sun6i_spi_read(sspi, SUN6I_FIFO_STA_REG);
	reg &= SUN6I_FIFO_STA_RF_CNT_MASK;
	cnt = reg >> SUN6I_FIFO_STA_RF_CNT_BITS;

	if (len > cnt)
		len = cnt;

	while (len--) {
		byte = readb(sspi->base_addr + SUN6I_RXDATA_REG);
		if (sspi->rx_buf)
			*sspi->rx_buf++ = byte;
	}
}

static inline void sun6i_spi_fill_fifo(struct sun6i_spi *sspi, int len)
{
	u32 cnt;
	u8 byte;

	/* See how much data we can fit */
	cnt = sspi->fifo_depth - sun6i_spi_get_tx_fifo_count(sspi);

	len = min3(len, (int)cnt, sspi->len);

	while (len--) {
		byte = sspi->tx_buf ? *sspi->tx_buf++ : 0;
		writeb(byte, sspi->base_addr + SUN6I_TXDATA_REG);
		sspi->len--;
	}
}

static void sun6i_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct sun6i_spi *sspi = spi_master_get_devdata(spi->master);
	u32 reg;

	reg = sun6i_spi_read(sspi, SUN6I_TFR_CTL_REG);
	reg &= ~SUN6I_TFR_CTL_CS_MASK;
	reg |= SUN6I_TFR_CTL_CS(spi->chip_select);

	if (enable)
		reg |= SUN6I_TFR_CTL_CS_LEVEL;
	else
		reg &= ~SUN6I_TFR_CTL_CS_LEVEL;

	sun6i_spi_write(sspi, SUN6I_TFR_CTL_REG, reg);
}

static size_t sun6i_spi_max_transfer_size(struct spi_device *spi)
{
	return SUN6I_MAX_XFER_SIZE - 1;
}

static int sun6i_spi_transfer_one(struct spi_master *master,
				  struct spi_device *spi,
				  struct spi_transfer *tfr)
{
	struct sun6i_spi *sspi = spi_master_get_devdata(master);
	unsigned int mclk_rate, div, timeout;
	unsigned int start, end, tx_time;
	unsigned int trig_level;
	unsigned int tx_len = 0;
	int ret = 0;
	u32 reg;

	if (tfr->len > SUN6I_MAX_XFER_SIZE)
		return -EINVAL;

	reinit_completion(&sspi->done);
	sspi->tx_buf = tfr->tx_buf;
	sspi->rx_buf = tfr->rx_buf;
	sspi->len = tfr->len;

	/* Clear pending interrupts */
	sun6i_spi_write(sspi, SUN6I_INT_STA_REG, ~0);

	/* Reset FIFO */
	sun6i_spi_write(sspi, SUN6I_FIFO_CTL_REG,
			SUN6I_FIFO_CTL_RF_RST | SUN6I_FIFO_CTL_TF_RST);

	/*
	 * Setup FIFO interrupt trigger level
	 * Here we choose 3/4 of the full fifo depth, as it's the hardcoded
	 * value used in old generation of Allwinner SPI controller.
	 * (See spi-sun4i.c)
	 */
	trig_level = sspi->fifo_depth / 4 * 3;
	sun6i_spi_write(sspi, SUN6I_FIFO_CTL_REG,
			(trig_level << SUN6I_FIFO_CTL_RF_RDY_TRIG_LEVEL_BITS) |
			(trig_level << SUN6I_FIFO_CTL_TF_ERQ_TRIG_LEVEL_BITS));

	/*
	 * Setup the transfer control register: Chip Select,
	 * polarities, etc.
	 */
	reg = sun6i_spi_read(sspi, SUN6I_TFR_CTL_REG);

	if (spi->mode & SPI_CPOL)
		reg |= SUN6I_TFR_CTL_CPOL;
	else
		reg &= ~SUN6I_TFR_CTL_CPOL;

	if (spi->mode & SPI_CPHA)
		reg |= SUN6I_TFR_CTL_CPHA;
	else
		reg &= ~SUN6I_TFR_CTL_CPHA;

	if (spi->mode & SPI_LSB_FIRST)
		reg |= SUN6I_TFR_CTL_FBS;
	else
		reg &= ~SUN6I_TFR_CTL_FBS;

	/*
	 * If it's a TX only transfer, we don't want to fill the RX
	 * FIFO with bogus data
	 */
	if (sspi->rx_buf)
		reg &= ~SUN6I_TFR_CTL_DHB;
	else
		reg |= SUN6I_TFR_CTL_DHB;

	/* We want to control the chip select manually */
	reg |= SUN6I_TFR_CTL_CS_MANUAL;

	sun6i_spi_write(sspi, SUN6I_TFR_CTL_REG, reg);

	/* Ensure that we have a parent clock fast enough */
	mclk_rate = clk_get_rate(sspi->mclk);
	if (mclk_rate < (2 * tfr->speed_hz)) {
		clk_set_rate(sspi->mclk, 2 * tfr->speed_hz);
		mclk_rate = clk_get_rate(sspi->mclk);
	}

	/*
	 * Setup clock divider.
	 *
	 * We have two choices there. Either we can use the clock
	 * divide rate 1, which is calculated thanks to this formula:
	 * SPI_CLK = MOD_CLK / (2 ^ cdr)
	 * Or we can use CDR2, which is calculated with the formula:
	 * SPI_CLK = MOD_CLK / (2 * (cdr + 1))
	 * Wether we use the former or the latter is set through the
	 * DRS bit.
	 *
	 * First try CDR2, and if we can't reach the expected
	 * frequency, fall back to CDR1.
	 */
	div = mclk_rate / (2 * tfr->speed_hz);
	if (div <= (SUN6I_CLK_CTL_CDR2_MASK + 1)) {
		if (div > 0)
			div--;

		reg = SUN6I_CLK_CTL_CDR2(div) | SUN6I_CLK_CTL_DRS;
	} else {
		div = ilog2(mclk_rate) - ilog2(tfr->speed_hz);
		reg = SUN6I_CLK_CTL_CDR1(div);
	}

	sun6i_spi_write(sspi, SUN6I_CLK_CTL_REG, reg);

	/* Setup the transfer now... */
	if (sspi->tx_buf)
		tx_len = tfr->len;

	/* Setup the counters */
	sun6i_spi_write(sspi, SUN6I_BURST_CNT_REG, SUN6I_BURST_CNT(tfr->len));
	sun6i_spi_write(sspi, SUN6I_XMIT_CNT_REG, SUN6I_XMIT_CNT(tx_len));
	sun6i_spi_write(sspi, SUN6I_BURST_CTL_CNT_REG,
			SUN6I_BURST_CTL_CNT_STC(tx_len));

	/* Fill the TX FIFO */
	sun6i_spi_fill_fifo(sspi, sspi->fifo_depth);

	/* Enable the interrupts */
	sun6i_spi_write(sspi, SUN6I_INT_CTL_REG, SUN6I_INT_CTL_TC);
	sun6i_spi_enable_interrupt(sspi, SUN6I_INT_CTL_TC |
					 SUN6I_INT_CTL_RF_RDY);
	if (tx_len > sspi->fifo_depth)
		sun6i_spi_enable_interrupt(sspi, SUN6I_INT_CTL_TF_ERQ);

	/* Start the transfer */
	reg = sun6i_spi_read(sspi, SUN6I_TFR_CTL_REG);
	sun6i_spi_write(sspi, SUN6I_TFR_CTL_REG, reg | SUN6I_TFR_CTL_XCH);

	tx_time = max(tfr->len * 8 * 2 / (tfr->speed_hz / 1000), 100U);
	start = jiffies;
	timeout = wait_for_completion_timeout(&sspi->done,
					      msecs_to_jiffies(tx_time));
	end = jiffies;
	if (!timeout) {
		dev_warn(&master->dev,
			 "%s: timeout transferring %u bytes@%iHz for %i(%i)ms",
			 dev_name(&spi->dev), tfr->len, tfr->speed_hz,
			 jiffies_to_msecs(end - start), tx_time);
		ret = -ETIMEDOUT;
		goto out;
	}

out:
	sun6i_spi_write(sspi, SUN6I_INT_CTL_REG, 0);

	return ret;
}

static irqreturn_t sun6i_spi_handler(int irq, void *dev_id)
{
	struct sun6i_spi *sspi = dev_id;
	u32 status = sun6i_spi_read(sspi, SUN6I_INT_STA_REG);

	/* Transfer complete */
	if (status & SUN6I_INT_CTL_TC) {
		sun6i_spi_write(sspi, SUN6I_INT_STA_REG, SUN6I_INT_CTL_TC);
		sun6i_spi_drain_fifo(sspi, sspi->fifo_depth);
		complete(&sspi->done);
		return IRQ_HANDLED;
	}

	/* Receive FIFO 3/4 full */
	if (status & SUN6I_INT_CTL_RF_RDY) {
		sun6i_spi_drain_fifo(sspi, SUN6I_FIFO_DEPTH);
		/* Only clear the interrupt _after_ draining the FIFO */
		sun6i_spi_write(sspi, SUN6I_INT_STA_REG, SUN6I_INT_CTL_RF_RDY);
		return IRQ_HANDLED;
	}

	/* Transmit FIFO 3/4 empty */
	if (status & SUN6I_INT_CTL_TF_ERQ) {
		sun6i_spi_fill_fifo(sspi, SUN6I_FIFO_DEPTH);

		if (!sspi->len)
			/* nothing left to transmit */
			sun6i_spi_disable_interrupt(sspi, SUN6I_INT_CTL_TF_ERQ);

		/* Only clear the interrupt _after_ re-seeding the FIFO */
		sun6i_spi_write(sspi, SUN6I_INT_STA_REG, SUN6I_INT_CTL_TF_ERQ);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int sun6i_spi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct sun6i_spi *sspi = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(sspi->hclk);
	if (ret) {
		dev_err(dev, "Couldn't enable AHB clock\n");
		goto out;
	}

	ret = clk_prepare_enable(sspi->mclk);
	if (ret) {
		dev_err(dev, "Couldn't enable module clock\n");
		goto err;
	}

	ret = reset_control_deassert(sspi->rstc);
	if (ret) {
		dev_err(dev, "Couldn't deassert the device from reset\n");
		goto err2;
	}

	sun6i_spi_write(sspi, SUN6I_GBL_CTL_REG,
			SUN6I_GBL_CTL_BUS_ENABLE | SUN6I_GBL_CTL_MASTER | SUN6I_GBL_CTL_TP);

	return 0;

err2:
	clk_disable_unprepare(sspi->mclk);
err:
	clk_disable_unprepare(sspi->hclk);
out:
	return ret;
}

static int sun6i_spi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct sun6i_spi *sspi = spi_master_get_devdata(master);

	reset_control_assert(sspi->rstc);
	clk_disable_unprepare(sspi->mclk);
	clk_disable_unprepare(sspi->hclk);

	return 0;
}

static int sun6i_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct sun6i_spi *sspi;
	int ret = 0, irq;

	master = spi_alloc_master(&pdev->dev, sizeof(struct sun6i_spi));
	if (!master) {
		dev_err(&pdev->dev, "Unable to allocate SPI Master\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, master);
	sspi = spi_master_get_devdata(master);

	sspi->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sspi->base_addr)) {
		ret = PTR_ERR(sspi->base_addr);
		goto err_free_master;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENXIO;
		goto err_free_master;
	}

	ret = devm_request_irq(&pdev->dev, irq, sun6i_spi_handler,
			       0, "sun6i-spi", sspi);
	if (ret) {
		dev_err(&pdev->dev, "Cannot request IRQ\n");
		goto err_free_master;
	}

	sspi->master = master;
	sspi->fifo_depth = (unsigned long)of_device_get_match_data(&pdev->dev);

	master->max_speed_hz = 100 * 1000 * 1000;
	master->min_speed_hz = 3 * 1000;
	master->use_gpio_descriptors = true;
	master->set_cs = sun6i_spi_set_cs;
	master->transfer_one = sun6i_spi_transfer_one;
	master->num_chipselect = 4;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->dev.of_node = pdev->dev.of_node;
	master->auto_runtime_pm = true;
	master->max_transfer_size = sun6i_spi_max_transfer_size;

	sspi->hclk = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(sspi->hclk)) {
		dev_err(&pdev->dev, "Unable to acquire AHB clock\n");
		ret = PTR_ERR(sspi->hclk);
		goto err_free_master;
	}

	sspi->mclk = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(sspi->mclk)) {
		dev_err(&pdev->dev, "Unable to acquire module clock\n");
		ret = PTR_ERR(sspi->mclk);
		goto err_free_master;
	}

	init_completion(&sspi->done);

	sspi->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(sspi->rstc)) {
		dev_err(&pdev->dev, "Couldn't get reset controller\n");
		ret = PTR_ERR(sspi->rstc);
		goto err_free_master;
	}

	/*
	 * This wake-up/shutdown pattern is to be able to have the
	 * device woken up, even if runtime_pm is disabled
	 */
	ret = sun6i_spi_runtime_resume(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't resume the device\n");
		goto err_free_master;
	}

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_idle(&pdev->dev);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret) {
		dev_err(&pdev->dev, "cannot register SPI master\n");
		goto err_pm_disable;
	}

	return 0;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);
	sun6i_spi_runtime_suspend(&pdev->dev);
err_free_master:
	spi_master_put(master);
	return ret;
}

static int sun6i_spi_remove(struct platform_device *pdev)
{
	pm_runtime_force_suspend(&pdev->dev);

	return 0;
}

static const struct of_device_id sun6i_spi_match[] = {
	{ .compatible = "allwinner,sun6i-a31-spi", .data = (void *)SUN6I_FIFO_DEPTH },
	{ .compatible = "allwinner,sun8i-h3-spi",  .data = (void *)SUN8I_FIFO_DEPTH },
	{}
};
MODULE_DEVICE_TABLE(of, sun6i_spi_match);

static const struct dev_pm_ops sun6i_spi_pm_ops = {
	.runtime_resume		= sun6i_spi_runtime_resume,
	.runtime_suspend	= sun6i_spi_runtime_suspend,
};

static struct platform_driver sun6i_spi_driver = {
	.probe	= sun6i_spi_probe,
	.remove	= sun6i_spi_remove,
	.driver	= {
		.name		= "sun6i-spi",
		.of_match_table	= sun6i_spi_match,
		.pm		= &sun6i_spi_pm_ops,
	},
};
module_platform_driver(sun6i_spi_driver);

MODULE_AUTHOR("Pan Nan <pannan@allwinnertech.com>");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A31 SPI controller driver");
MODULE_LICENSE("GPL");
