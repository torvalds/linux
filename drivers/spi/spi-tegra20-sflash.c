// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI driver for Nvidia's Tegra20 Serial Flash Controller.
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>

#define SPI_COMMAND				0x000
#define SPI_GO					BIT(30)
#define SPI_M_S					BIT(28)
#define SPI_ACTIVE_SCLK_MASK			(0x3 << 26)
#define SPI_ACTIVE_SCLK_DRIVE_LOW		(0 << 26)
#define SPI_ACTIVE_SCLK_DRIVE_HIGH		(1 << 26)
#define SPI_ACTIVE_SCLK_PULL_LOW		(2 << 26)
#define SPI_ACTIVE_SCLK_PULL_HIGH		(3 << 26)

#define SPI_CK_SDA_FALLING			(1 << 21)
#define SPI_CK_SDA_RISING			(0 << 21)
#define SPI_CK_SDA_MASK				(1 << 21)
#define SPI_ACTIVE_SDA				(0x3 << 18)
#define SPI_ACTIVE_SDA_DRIVE_LOW		(0 << 18)
#define SPI_ACTIVE_SDA_DRIVE_HIGH		(1 << 18)
#define SPI_ACTIVE_SDA_PULL_LOW			(2 << 18)
#define SPI_ACTIVE_SDA_PULL_HIGH		(3 << 18)

#define SPI_CS_POL_INVERT			BIT(16)
#define SPI_TX_EN				BIT(15)
#define SPI_RX_EN				BIT(14)
#define SPI_CS_VAL_HIGH				BIT(13)
#define SPI_CS_VAL_LOW				0x0
#define SPI_CS_SW				BIT(12)
#define SPI_CS_HW				0x0
#define SPI_CS_DELAY_MASK			(7 << 9)
#define SPI_CS3_EN				BIT(8)
#define SPI_CS2_EN				BIT(7)
#define SPI_CS1_EN				BIT(6)
#define SPI_CS0_EN				BIT(5)

#define SPI_CS_MASK			(SPI_CS3_EN | SPI_CS2_EN |	\
					SPI_CS1_EN | SPI_CS0_EN)
#define SPI_BIT_LENGTH(x)		(((x) & 0x1f) << 0)

#define SPI_MODES			(SPI_ACTIVE_SCLK_MASK | SPI_CK_SDA_MASK)

#define SPI_STATUS			0x004
#define SPI_BSY				BIT(31)
#define SPI_RDY				BIT(30)
#define SPI_TXF_FLUSH			BIT(29)
#define SPI_RXF_FLUSH			BIT(28)
#define SPI_RX_UNF			BIT(27)
#define SPI_TX_OVF			BIT(26)
#define SPI_RXF_EMPTY			BIT(25)
#define SPI_RXF_FULL			BIT(24)
#define SPI_TXF_EMPTY			BIT(23)
#define SPI_TXF_FULL			BIT(22)
#define SPI_BLK_CNT(count)		(((count) & 0xffff) + 1)

#define SPI_FIFO_ERROR			(SPI_RX_UNF | SPI_TX_OVF)
#define SPI_FIFO_EMPTY			(SPI_TX_EMPTY | SPI_RX_EMPTY)

#define SPI_RX_CMP			0x8
#define SPI_DMA_CTL			0x0C
#define SPI_DMA_EN			BIT(31)
#define SPI_IE_RXC			BIT(27)
#define SPI_IE_TXC			BIT(26)
#define SPI_PACKED			BIT(20)
#define SPI_RX_TRIG_MASK		(0x3 << 18)
#define SPI_RX_TRIG_1W			(0x0 << 18)
#define SPI_RX_TRIG_4W			(0x1 << 18)
#define SPI_TX_TRIG_MASK		(0x3 << 16)
#define SPI_TX_TRIG_1W			(0x0 << 16)
#define SPI_TX_TRIG_4W			(0x1 << 16)
#define SPI_DMA_BLK_COUNT(count)	(((count) - 1) & 0xFFFF)

#define SPI_TX_FIFO			0x10
#define SPI_RX_FIFO			0x20

#define DATA_DIR_TX			(1 << 0)
#define DATA_DIR_RX			(1 << 1)

#define MAX_CHIP_SELECT			4
#define SPI_FIFO_DEPTH			4
#define SPI_DMA_TIMEOUT               (msecs_to_jiffies(1000))

struct tegra_sflash_data {
	struct device				*dev;
	struct spi_master			*master;
	spinlock_t				lock;

	struct clk				*clk;
	struct reset_control			*rst;
	void __iomem				*base;
	unsigned				irq;
	u32					cur_speed;

	struct spi_device			*cur_spi;
	unsigned				cur_pos;
	unsigned				cur_len;
	unsigned				bytes_per_word;
	unsigned				cur_direction;
	unsigned				curr_xfer_words;

	unsigned				cur_rx_pos;
	unsigned				cur_tx_pos;

	u32					tx_status;
	u32					rx_status;
	u32					status_reg;

	u32					def_command_reg;
	u32					command_reg;
	u32					dma_control_reg;

	struct completion			xfer_completion;
	struct spi_transfer			*curr_xfer;
};

static int tegra_sflash_runtime_suspend(struct device *dev);
static int tegra_sflash_runtime_resume(struct device *dev);

static inline u32 tegra_sflash_readl(struct tegra_sflash_data *tsd,
		unsigned long reg)
{
	return readl(tsd->base + reg);
}

static inline void tegra_sflash_writel(struct tegra_sflash_data *tsd,
		u32 val, unsigned long reg)
{
	writel(val, tsd->base + reg);
}

static void tegra_sflash_clear_status(struct tegra_sflash_data *tsd)
{
	/* Write 1 to clear status register */
	tegra_sflash_writel(tsd, SPI_RDY | SPI_FIFO_ERROR, SPI_STATUS);
}

static unsigned tegra_sflash_calculate_curr_xfer_param(
	struct spi_device *spi, struct tegra_sflash_data *tsd,
	struct spi_transfer *t)
{
	unsigned remain_len = t->len - tsd->cur_pos;
	unsigned max_word;

	tsd->bytes_per_word = DIV_ROUND_UP(t->bits_per_word, 8);
	max_word = remain_len / tsd->bytes_per_word;
	if (max_word > SPI_FIFO_DEPTH)
		max_word = SPI_FIFO_DEPTH;
	tsd->curr_xfer_words = max_word;
	return max_word;
}

static unsigned tegra_sflash_fill_tx_fifo_from_client_txbuf(
	struct tegra_sflash_data *tsd, struct spi_transfer *t)
{
	unsigned nbytes;
	u32 status;
	unsigned max_n_32bit = tsd->curr_xfer_words;
	u8 *tx_buf = (u8 *)t->tx_buf + tsd->cur_tx_pos;

	if (max_n_32bit > SPI_FIFO_DEPTH)
		max_n_32bit = SPI_FIFO_DEPTH;
	nbytes = max_n_32bit * tsd->bytes_per_word;

	status = tegra_sflash_readl(tsd, SPI_STATUS);
	while (!(status & SPI_TXF_FULL)) {
		int i;
		u32 x = 0;

		for (i = 0; nbytes && (i < tsd->bytes_per_word);
							i++, nbytes--)
			x |= (u32)(*tx_buf++) << (i * 8);
		tegra_sflash_writel(tsd, x, SPI_TX_FIFO);
		if (!nbytes)
			break;

		status = tegra_sflash_readl(tsd, SPI_STATUS);
	}
	tsd->cur_tx_pos += max_n_32bit * tsd->bytes_per_word;
	return max_n_32bit;
}

static int tegra_sflash_read_rx_fifo_to_client_rxbuf(
		struct tegra_sflash_data *tsd, struct spi_transfer *t)
{
	u32 status;
	unsigned int read_words = 0;
	u8 *rx_buf = (u8 *)t->rx_buf + tsd->cur_rx_pos;

	status = tegra_sflash_readl(tsd, SPI_STATUS);
	while (!(status & SPI_RXF_EMPTY)) {
		int i;
		u32 x = tegra_sflash_readl(tsd, SPI_RX_FIFO);

		for (i = 0; (i < tsd->bytes_per_word); i++)
			*rx_buf++ = (x >> (i*8)) & 0xFF;
		read_words++;
		status = tegra_sflash_readl(tsd, SPI_STATUS);
	}
	tsd->cur_rx_pos += read_words * tsd->bytes_per_word;
	return 0;
}

static int tegra_sflash_start_cpu_based_transfer(
		struct tegra_sflash_data *tsd, struct spi_transfer *t)
{
	u32 val = 0;
	unsigned cur_words;

	if (tsd->cur_direction & DATA_DIR_TX)
		val |= SPI_IE_TXC;

	if (tsd->cur_direction & DATA_DIR_RX)
		val |= SPI_IE_RXC;

	tegra_sflash_writel(tsd, val, SPI_DMA_CTL);
	tsd->dma_control_reg = val;

	if (tsd->cur_direction & DATA_DIR_TX)
		cur_words = tegra_sflash_fill_tx_fifo_from_client_txbuf(tsd, t);
	else
		cur_words = tsd->curr_xfer_words;
	val |= SPI_DMA_BLK_COUNT(cur_words);
	tegra_sflash_writel(tsd, val, SPI_DMA_CTL);
	tsd->dma_control_reg = val;
	val |= SPI_DMA_EN;
	tegra_sflash_writel(tsd, val, SPI_DMA_CTL);
	return 0;
}

static int tegra_sflash_start_transfer_one(struct spi_device *spi,
		struct spi_transfer *t, bool is_first_of_msg,
		bool is_single_xfer)
{
	struct tegra_sflash_data *tsd = spi_master_get_devdata(spi->master);
	u32 speed;
	u32 command;

	speed = t->speed_hz;
	if (speed != tsd->cur_speed) {
		clk_set_rate(tsd->clk, speed);
		tsd->cur_speed = speed;
	}

	tsd->cur_spi = spi;
	tsd->cur_pos = 0;
	tsd->cur_rx_pos = 0;
	tsd->cur_tx_pos = 0;
	tsd->curr_xfer = t;
	tegra_sflash_calculate_curr_xfer_param(spi, tsd, t);
	if (is_first_of_msg) {
		command = tsd->def_command_reg;
		command |= SPI_BIT_LENGTH(t->bits_per_word - 1);
		command |= SPI_CS_VAL_HIGH;

		command &= ~SPI_MODES;
		if (spi->mode & SPI_CPHA)
			command |= SPI_CK_SDA_FALLING;

		if (spi->mode & SPI_CPOL)
			command |= SPI_ACTIVE_SCLK_DRIVE_HIGH;
		else
			command |= SPI_ACTIVE_SCLK_DRIVE_LOW;
		command |= SPI_CS0_EN << spi->chip_select;
	} else {
		command = tsd->command_reg;
		command &= ~SPI_BIT_LENGTH(~0);
		command |= SPI_BIT_LENGTH(t->bits_per_word - 1);
		command &= ~(SPI_RX_EN | SPI_TX_EN);
	}

	tsd->cur_direction = 0;
	if (t->rx_buf) {
		command |= SPI_RX_EN;
		tsd->cur_direction |= DATA_DIR_RX;
	}
	if (t->tx_buf) {
		command |= SPI_TX_EN;
		tsd->cur_direction |= DATA_DIR_TX;
	}
	tegra_sflash_writel(tsd, command, SPI_COMMAND);
	tsd->command_reg = command;

	return tegra_sflash_start_cpu_based_transfer(tsd, t);
}

static int tegra_sflash_transfer_one_message(struct spi_master *master,
			struct spi_message *msg)
{
	bool is_first_msg = true;
	int single_xfer;
	struct tegra_sflash_data *tsd = spi_master_get_devdata(master);
	struct spi_transfer *xfer;
	struct spi_device *spi = msg->spi;
	int ret;

	msg->status = 0;
	msg->actual_length = 0;
	single_xfer = list_is_singular(&msg->transfers);
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		reinit_completion(&tsd->xfer_completion);
		ret = tegra_sflash_start_transfer_one(spi, xfer,
					is_first_msg, single_xfer);
		if (ret < 0) {
			dev_err(tsd->dev,
				"spi can not start transfer, err %d\n", ret);
			goto exit;
		}
		is_first_msg = false;
		ret = wait_for_completion_timeout(&tsd->xfer_completion,
						SPI_DMA_TIMEOUT);
		if (WARN_ON(ret == 0)) {
			dev_err(tsd->dev,
				"spi transfer timeout, err %d\n", ret);
			ret = -EIO;
			goto exit;
		}

		if (tsd->tx_status ||  tsd->rx_status) {
			dev_err(tsd->dev, "Error in Transfer\n");
			ret = -EIO;
			goto exit;
		}
		msg->actual_length += xfer->len;
		if (xfer->cs_change && xfer->delay.value) {
			tegra_sflash_writel(tsd, tsd->def_command_reg,
					SPI_COMMAND);
			spi_transfer_delay_exec(xfer);
		}
	}
	ret = 0;
exit:
	tegra_sflash_writel(tsd, tsd->def_command_reg, SPI_COMMAND);
	msg->status = ret;
	spi_finalize_current_message(master);
	return ret;
}

static irqreturn_t handle_cpu_based_xfer(struct tegra_sflash_data *tsd)
{
	struct spi_transfer *t = tsd->curr_xfer;

	spin_lock(&tsd->lock);
	if (tsd->tx_status || tsd->rx_status || (tsd->status_reg & SPI_BSY)) {
		dev_err(tsd->dev,
			"CpuXfer ERROR bit set 0x%x\n", tsd->status_reg);
		dev_err(tsd->dev,
			"CpuXfer 0x%08x:0x%08x\n", tsd->command_reg,
				tsd->dma_control_reg);
		reset_control_assert(tsd->rst);
		udelay(2);
		reset_control_deassert(tsd->rst);
		complete(&tsd->xfer_completion);
		goto exit;
	}

	if (tsd->cur_direction & DATA_DIR_RX)
		tegra_sflash_read_rx_fifo_to_client_rxbuf(tsd, t);

	if (tsd->cur_direction & DATA_DIR_TX)
		tsd->cur_pos = tsd->cur_tx_pos;
	else
		tsd->cur_pos = tsd->cur_rx_pos;

	if (tsd->cur_pos == t->len) {
		complete(&tsd->xfer_completion);
		goto exit;
	}

	tegra_sflash_calculate_curr_xfer_param(tsd->cur_spi, tsd, t);
	tegra_sflash_start_cpu_based_transfer(tsd, t);
exit:
	spin_unlock(&tsd->lock);
	return IRQ_HANDLED;
}

static irqreturn_t tegra_sflash_isr(int irq, void *context_data)
{
	struct tegra_sflash_data *tsd = context_data;

	tsd->status_reg = tegra_sflash_readl(tsd, SPI_STATUS);
	if (tsd->cur_direction & DATA_DIR_TX)
		tsd->tx_status = tsd->status_reg & SPI_TX_OVF;

	if (tsd->cur_direction & DATA_DIR_RX)
		tsd->rx_status = tsd->status_reg & SPI_RX_UNF;
	tegra_sflash_clear_status(tsd);

	return handle_cpu_based_xfer(tsd);
}

static const struct of_device_id tegra_sflash_of_match[] = {
	{ .compatible = "nvidia,tegra20-sflash", },
	{}
};
MODULE_DEVICE_TABLE(of, tegra_sflash_of_match);

static int tegra_sflash_probe(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct tegra_sflash_data	*tsd;
	int ret;
	const struct of_device_id *match;

	match = of_match_device(tegra_sflash_of_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Error: No device match found\n");
		return -ENODEV;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(*tsd));
	if (!master) {
		dev_err(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA;
	master->transfer_one_message = tegra_sflash_transfer_one_message;
	master->auto_runtime_pm = true;
	master->num_chipselect = MAX_CHIP_SELECT;

	platform_set_drvdata(pdev, master);
	tsd = spi_master_get_devdata(master);
	tsd->master = master;
	tsd->dev = &pdev->dev;
	spin_lock_init(&tsd->lock);

	if (of_property_read_u32(tsd->dev->of_node, "spi-max-frequency",
				 &master->max_speed_hz))
		master->max_speed_hz = 25000000; /* 25MHz */

	tsd->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tsd->base)) {
		ret = PTR_ERR(tsd->base);
		goto exit_free_master;
	}

	tsd->irq = platform_get_irq(pdev, 0);
	ret = request_irq(tsd->irq, tegra_sflash_isr, 0,
			dev_name(&pdev->dev), tsd);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register ISR for IRQ %d\n",
					tsd->irq);
		goto exit_free_master;
	}

	tsd->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tsd->clk)) {
		dev_err(&pdev->dev, "can not get clock\n");
		ret = PTR_ERR(tsd->clk);
		goto exit_free_irq;
	}

	tsd->rst = devm_reset_control_get_exclusive(&pdev->dev, "spi");
	if (IS_ERR(tsd->rst)) {
		dev_err(&pdev->dev, "can not get reset\n");
		ret = PTR_ERR(tsd->rst);
		goto exit_free_irq;
	}

	init_completion(&tsd->xfer_completion);
	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = tegra_sflash_runtime_resume(&pdev->dev);
		if (ret)
			goto exit_pm_disable;
	}

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm runtime get failed, e = %d\n", ret);
		pm_runtime_put_noidle(&pdev->dev);
		goto exit_pm_disable;
	}

	/* Reset controller */
	reset_control_assert(tsd->rst);
	udelay(2);
	reset_control_deassert(tsd->rst);

	tsd->def_command_reg  = SPI_M_S | SPI_CS_SW;
	tegra_sflash_writel(tsd, tsd->def_command_reg, SPI_COMMAND);
	pm_runtime_put(&pdev->dev);

	master->dev.of_node = pdev->dev.of_node;
	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret < 0) {
		dev_err(&pdev->dev, "can not register to master err %d\n", ret);
		goto exit_pm_disable;
	}
	return ret;

exit_pm_disable:
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_sflash_runtime_suspend(&pdev->dev);
exit_free_irq:
	free_irq(tsd->irq, tsd);
exit_free_master:
	spi_master_put(master);
	return ret;
}

static int tegra_sflash_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct tegra_sflash_data	*tsd = spi_master_get_devdata(master);

	free_irq(tsd->irq, tsd);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_sflash_runtime_suspend(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_sflash_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);

	return spi_master_suspend(master);
}

static int tegra_sflash_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_sflash_data *tsd = spi_master_get_devdata(master);
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		dev_err(dev, "pm runtime failed, e = %d\n", ret);
		return ret;
	}
	tegra_sflash_writel(tsd, tsd->command_reg, SPI_COMMAND);
	pm_runtime_put(dev);

	return spi_master_resume(master);
}
#endif

static int tegra_sflash_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_sflash_data *tsd = spi_master_get_devdata(master);

	/* Flush all write which are in PPSB queue by reading back */
	tegra_sflash_readl(tsd, SPI_COMMAND);

	clk_disable_unprepare(tsd->clk);
	return 0;
}

static int tegra_sflash_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_sflash_data *tsd = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(tsd->clk);
	if (ret < 0) {
		dev_err(tsd->dev, "clk_prepare failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static const struct dev_pm_ops slink_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_sflash_runtime_suspend,
		tegra_sflash_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_sflash_suspend, tegra_sflash_resume)
};
static struct platform_driver tegra_sflash_driver = {
	.driver = {
		.name		= "spi-tegra-sflash",
		.pm		= &slink_pm_ops,
		.of_match_table	= tegra_sflash_of_match,
	},
	.probe =	tegra_sflash_probe,
	.remove =	tegra_sflash_remove,
};
module_platform_driver(tegra_sflash_driver);

MODULE_ALIAS("platform:spi-tegra-sflash");
MODULE_DESCRIPTION("NVIDIA Tegra20 Serial Flash Controller Driver");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
