// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (c) 2008-2014 STMicroelectronics Limited
 *
 *  Author: Angus Clark <Angus.Clark@st.com>
 *          Patrice Chotard <patrice.chotard@st.com>
 *          Lee Jones <lee.jones@linaro.org>
 *
 *  SPI master mode controller driver, used in STMicroelectronics devices.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>

/* SSC registers */
#define SSC_BRG				0x000
#define SSC_TBUF			0x004
#define SSC_RBUF			0x008
#define SSC_CTL				0x00C
#define SSC_IEN				0x010
#define SSC_I2C				0x018

/* SSC Control */
#define SSC_CTL_DATA_WIDTH_9		0x8
#define SSC_CTL_DATA_WIDTH_MSK		0xf
#define SSC_CTL_BM			0xf
#define SSC_CTL_HB			BIT(4)
#define SSC_CTL_PH			BIT(5)
#define SSC_CTL_PO			BIT(6)
#define SSC_CTL_SR			BIT(7)
#define SSC_CTL_MS			BIT(8)
#define SSC_CTL_EN			BIT(9)
#define SSC_CTL_LPB			BIT(10)
#define SSC_CTL_EN_TX_FIFO		BIT(11)
#define SSC_CTL_EN_RX_FIFO		BIT(12)
#define SSC_CTL_EN_CLST_RX		BIT(13)

/* SSC Interrupt Enable */
#define SSC_IEN_TEEN			BIT(2)

#define FIFO_SIZE			8

struct spi_st {
	/* SSC SPI Controller */
	void __iomem		*base;
	struct clk		*clk;
	struct device		*dev;

	/* SSC SPI current transaction */
	const u8		*tx_ptr;
	u8			*rx_ptr;
	u16			bytes_per_word;
	unsigned int		words_remaining;
	unsigned int		baud;
	struct completion	done;
};

/* Load the TX FIFO */
static void ssc_write_tx_fifo(struct spi_st *spi_st)
{
	unsigned int count, i;
	uint32_t word = 0;

	if (spi_st->words_remaining > FIFO_SIZE)
		count = FIFO_SIZE;
	else
		count = spi_st->words_remaining;

	for (i = 0; i < count; i++) {
		if (spi_st->tx_ptr) {
			if (spi_st->bytes_per_word == 1) {
				word = *spi_st->tx_ptr++;
			} else {
				word = *spi_st->tx_ptr++;
				word = *spi_st->tx_ptr++ | (word << 8);
			}
		}
		writel_relaxed(word, spi_st->base + SSC_TBUF);
	}
}

/* Read the RX FIFO */
static void ssc_read_rx_fifo(struct spi_st *spi_st)
{
	unsigned int count, i;
	uint32_t word = 0;

	if (spi_st->words_remaining > FIFO_SIZE)
		count = FIFO_SIZE;
	else
		count = spi_st->words_remaining;

	for (i = 0; i < count; i++) {
		word = readl_relaxed(spi_st->base + SSC_RBUF);

		if (spi_st->rx_ptr) {
			if (spi_st->bytes_per_word == 1) {
				*spi_st->rx_ptr++ = (uint8_t)word;
			} else {
				*spi_st->rx_ptr++ = (word >> 8);
				*spi_st->rx_ptr++ = word & 0xff;
			}
		}
	}
	spi_st->words_remaining -= count;
}

static int spi_st_transfer_one(struct spi_master *master,
			       struct spi_device *spi, struct spi_transfer *t)
{
	struct spi_st *spi_st = spi_master_get_devdata(master);
	uint32_t ctl = 0;

	/* Setup transfer */
	spi_st->tx_ptr = t->tx_buf;
	spi_st->rx_ptr = t->rx_buf;

	if (spi->bits_per_word > 8) {
		/*
		 * Anything greater than 8 bits-per-word requires 2
		 * bytes-per-word in the RX/TX buffers
		 */
		spi_st->bytes_per_word = 2;
		spi_st->words_remaining = t->len / 2;

	} else if (spi->bits_per_word == 8 && !(t->len & 0x1)) {
		/*
		 * If transfer is even-length, and 8 bits-per-word, then
		 * implement as half-length 16 bits-per-word transfer
		 */
		spi_st->bytes_per_word = 2;
		spi_st->words_remaining = t->len / 2;

		/* Set SSC_CTL to 16 bits-per-word */
		ctl = readl_relaxed(spi_st->base + SSC_CTL);
		writel_relaxed((ctl | 0xf), spi_st->base + SSC_CTL);

		readl_relaxed(spi_st->base + SSC_RBUF);

	} else {
		spi_st->bytes_per_word = 1;
		spi_st->words_remaining = t->len;
	}

	reinit_completion(&spi_st->done);

	/* Start transfer by writing to the TX FIFO */
	ssc_write_tx_fifo(spi_st);
	writel_relaxed(SSC_IEN_TEEN, spi_st->base + SSC_IEN);

	/* Wait for transfer to complete */
	wait_for_completion(&spi_st->done);

	/* Restore SSC_CTL if necessary */
	if (ctl)
		writel_relaxed(ctl, spi_st->base + SSC_CTL);

	spi_finalize_current_transfer(spi->master);

	return t->len;
}

static void spi_st_cleanup(struct spi_device *spi)
{
	gpio_free(spi->cs_gpio);
}

/* the spi->mode bits understood by this driver: */
#define MODEBITS  (SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST | SPI_LOOP | SPI_CS_HIGH)
static int spi_st_setup(struct spi_device *spi)
{
	struct spi_st *spi_st = spi_master_get_devdata(spi->master);
	u32 spi_st_clk, sscbrg, var;
	u32 hz = spi->max_speed_hz;
	int cs = spi->cs_gpio;
	int ret;

	if (!hz)  {
		dev_err(&spi->dev, "max_speed_hz unspecified\n");
		return -EINVAL;
	}

	if (!gpio_is_valid(cs)) {
		dev_err(&spi->dev, "%d is not a valid gpio\n", cs);
		return -EINVAL;
	}

	ret = gpio_request(cs, dev_name(&spi->dev));
	if (ret) {
		dev_err(&spi->dev, "could not request gpio:%d\n", cs);
		return ret;
	}

	ret = gpio_direction_output(cs, spi->mode & SPI_CS_HIGH);
	if (ret)
		goto out_free_gpio;

	spi_st_clk = clk_get_rate(spi_st->clk);

	/* Set SSC_BRF */
	sscbrg = spi_st_clk / (2 * hz);
	if (sscbrg < 0x07 || sscbrg > BIT(16)) {
		dev_err(&spi->dev,
			"baudrate %d outside valid range %d\n", sscbrg, hz);
		ret = -EINVAL;
		goto out_free_gpio;
	}

	spi_st->baud = spi_st_clk / (2 * sscbrg);
	if (sscbrg == BIT(16)) /* 16-bit counter wraps */
		sscbrg = 0x0;

	writel_relaxed(sscbrg, spi_st->base + SSC_BRG);

	dev_dbg(&spi->dev,
		"setting baudrate:target= %u hz, actual= %u hz, sscbrg= %u\n",
		hz, spi_st->baud, sscbrg);

	/* Set SSC_CTL and enable SSC */
	var = readl_relaxed(spi_st->base + SSC_CTL);
	var |= SSC_CTL_MS;

	if (spi->mode & SPI_CPOL)
		var |= SSC_CTL_PO;
	else
		var &= ~SSC_CTL_PO;

	if (spi->mode & SPI_CPHA)
		var |= SSC_CTL_PH;
	else
		var &= ~SSC_CTL_PH;

	if ((spi->mode & SPI_LSB_FIRST) == 0)
		var |= SSC_CTL_HB;
	else
		var &= ~SSC_CTL_HB;

	if (spi->mode & SPI_LOOP)
		var |= SSC_CTL_LPB;
	else
		var &= ~SSC_CTL_LPB;

	var &= ~SSC_CTL_DATA_WIDTH_MSK;
	var |= (spi->bits_per_word - 1);

	var |= SSC_CTL_EN_TX_FIFO | SSC_CTL_EN_RX_FIFO;
	var |= SSC_CTL_EN;

	writel_relaxed(var, spi_st->base + SSC_CTL);

	/* Clear the status register */
	readl_relaxed(spi_st->base + SSC_RBUF);

	return 0;

out_free_gpio:
	gpio_free(cs);
	return ret;
}

/* Interrupt fired when TX shift register becomes empty */
static irqreturn_t spi_st_irq(int irq, void *dev_id)
{
	struct spi_st *spi_st = (struct spi_st *)dev_id;

	/* Read RX FIFO */
	ssc_read_rx_fifo(spi_st);

	/* Fill TX FIFO */
	if (spi_st->words_remaining) {
		ssc_write_tx_fifo(spi_st);
	} else {
		/* TX/RX complete */
		writel_relaxed(0x0, spi_st->base + SSC_IEN);
		/*
		 * read SSC_IEN to ensure that this bit is set
		 * before re-enabling interrupt
		 */
		readl(spi_st->base + SSC_IEN);
		complete(&spi_st->done);
	}

	return IRQ_HANDLED;
}

static int spi_st_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct spi_master *master;
	struct spi_st *spi_st;
	int irq, ret = 0;
	u32 var;

	master = spi_alloc_master(&pdev->dev, sizeof(*spi_st));
	if (!master)
		return -ENOMEM;

	master->dev.of_node		= np;
	master->mode_bits		= MODEBITS;
	master->setup			= spi_st_setup;
	master->cleanup			= spi_st_cleanup;
	master->transfer_one		= spi_st_transfer_one;
	master->bits_per_word_mask	= SPI_BPW_MASK(8) | SPI_BPW_MASK(16);
	master->auto_runtime_pm		= true;
	master->bus_num			= pdev->id;
	spi_st				= spi_master_get_devdata(master);

	spi_st->clk = devm_clk_get(&pdev->dev, "ssc");
	if (IS_ERR(spi_st->clk)) {
		dev_err(&pdev->dev, "Unable to request clock\n");
		ret = PTR_ERR(spi_st->clk);
		goto put_master;
	}

	ret = clk_prepare_enable(spi_st->clk);
	if (ret)
		goto put_master;

	init_completion(&spi_st->done);

	/* Get resources */
	spi_st->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spi_st->base)) {
		ret = PTR_ERR(spi_st->base);
		goto clk_disable;
	}

	/* Disable I2C and Reset SSC */
	writel_relaxed(0x0, spi_st->base + SSC_I2C);
	var = readw_relaxed(spi_st->base + SSC_CTL);
	var |= SSC_CTL_SR;
	writel_relaxed(var, spi_st->base + SSC_CTL);

	udelay(1);
	var = readl_relaxed(spi_st->base + SSC_CTL);
	var &= ~SSC_CTL_SR;
	writel_relaxed(var, spi_st->base + SSC_CTL);

	/* Set SSC into slave mode before reconfiguring PIO pins */
	var = readl_relaxed(spi_st->base + SSC_CTL);
	var &= ~SSC_CTL_MS;
	writel_relaxed(var, spi_st->base + SSC_CTL);

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		dev_err(&pdev->dev, "IRQ missing or invalid\n");
		ret = -EINVAL;
		goto clk_disable;
	}

	ret = devm_request_irq(&pdev->dev, irq, spi_st_irq, 0,
			       pdev->name, spi_st);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq %d\n", irq);
		goto clk_disable;
	}

	/* by default the device is on */
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	platform_set_drvdata(pdev, master);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register master\n");
		goto clk_disable;
	}

	return 0;

clk_disable:
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(spi_st->clk);
put_master:
	spi_master_put(master);
	return ret;
}

static int spi_st_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct spi_st *spi_st = spi_master_get_devdata(master);

	pm_runtime_disable(&pdev->dev);

	clk_disable_unprepare(spi_st->clk);

	pinctrl_pm_select_sleep_state(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int spi_st_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct spi_st *spi_st = spi_master_get_devdata(master);

	writel_relaxed(0, spi_st->base + SSC_IEN);
	pinctrl_pm_select_sleep_state(dev);

	clk_disable_unprepare(spi_st->clk);

	return 0;
}

static int spi_st_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct spi_st *spi_st = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(spi_st->clk);
	pinctrl_pm_select_default_state(dev);

	return ret;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int spi_st_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	int ret;

	ret = spi_master_suspend(master);
	if (ret)
		return ret;

	return pm_runtime_force_suspend(dev);
}

static int spi_st_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	int ret;

	ret = spi_master_resume(master);
	if (ret)
		return ret;

	return pm_runtime_force_resume(dev);
}
#endif

static const struct dev_pm_ops spi_st_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(spi_st_suspend, spi_st_resume)
	SET_RUNTIME_PM_OPS(spi_st_runtime_suspend, spi_st_runtime_resume, NULL)
};

static const struct of_device_id stm_spi_match[] = {
	{ .compatible = "st,comms-ssc4-spi", },
	{},
};
MODULE_DEVICE_TABLE(of, stm_spi_match);

static struct platform_driver spi_st_driver = {
	.driver = {
		.name = "spi-st",
		.pm = &spi_st_pm,
		.of_match_table = of_match_ptr(stm_spi_match),
	},
	.probe = spi_st_probe,
	.remove = spi_st_remove,
};
module_platform_driver(spi_st_driver);

MODULE_AUTHOR("Patrice Chotard <patrice.chotard@st.com>");
MODULE_DESCRIPTION("STM SSC SPI driver");
MODULE_LICENSE("GPL v2");
