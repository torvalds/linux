// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2017-2018 Socionext Inc.
//   Author: Masahiro Yamada <yamada.masahiro@socionext.com>

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "tmio_mmc.h"

#define   UNIPHIER_SD_CLK_CTL_DIV1024		BIT(16)
#define   UNIPHIER_SD_CLK_CTL_DIV1		BIT(10)
#define   UNIPHIER_SD_CLKCTL_OFFEN		BIT(9)  // auto SDCLK stop
#define UNIPHIER_SD_CC_EXT_MODE		0x1b0
#define   UNIPHIER_SD_CC_EXT_MODE_DMA		BIT(1)
#define UNIPHIER_SD_HOST_MODE		0x1c8
#define UNIPHIER_SD_VOLT		0x1e4
#define   UNIPHIER_SD_VOLT_MASK			GENMASK(1, 0)
#define   UNIPHIER_SD_VOLT_OFF			0
#define   UNIPHIER_SD_VOLT_330			1	// 3.3V signal
#define   UNIPHIER_SD_VOLT_180			2	// 1.8V signal
#define UNIPHIER_SD_DMA_MODE		0x410
#define   UNIPHIER_SD_DMA_MODE_DIR_MASK		GENMASK(17, 16)
#define   UNIPHIER_SD_DMA_MODE_DIR_TO_DEV	0
#define   UNIPHIER_SD_DMA_MODE_DIR_FROM_DEV	1
#define   UNIPHIER_SD_DMA_MODE_WIDTH_MASK	GENMASK(5, 4)
#define   UNIPHIER_SD_DMA_MODE_WIDTH_8		0
#define   UNIPHIER_SD_DMA_MODE_WIDTH_16		1
#define   UNIPHIER_SD_DMA_MODE_WIDTH_32		2
#define   UNIPHIER_SD_DMA_MODE_WIDTH_64		3
#define   UNIPHIER_SD_DMA_MODE_ADDR_INC		BIT(0)	// 1: inc, 0: fixed
#define UNIPHIER_SD_DMA_CTL		0x414
#define   UNIPHIER_SD_DMA_CTL_START	BIT(0)	// start DMA (auto cleared)
#define UNIPHIER_SD_DMA_RST		0x418
#define   UNIPHIER_SD_DMA_RST_CH1	BIT(9)
#define   UNIPHIER_SD_DMA_RST_CH0	BIT(8)
#define UNIPHIER_SD_DMA_ADDR_L		0x440
#define UNIPHIER_SD_DMA_ADDR_H		0x444

/*
 * IP is extended to support various features: built-in DMA engine,
 * 1/1024 divisor, etc.
 */
#define UNIPHIER_SD_CAP_EXTENDED_IP		BIT(0)
/* RX channel of the built-in DMA controller is broken (Pro5) */
#define UNIPHIER_SD_CAP_BROKEN_DMA_RX		BIT(1)

struct uniphier_sd_priv {
	struct tmio_mmc_data tmio_data;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinstate_default;
	struct pinctrl_state *pinstate_uhs;
	struct clk *clk;
	struct reset_control *rst;
	struct reset_control *rst_br;
	struct reset_control *rst_hw;
	struct dma_chan *chan;
	enum dma_data_direction dma_dir;
	unsigned long clk_rate;
	unsigned long caps;
};

static void *uniphier_sd_priv(struct tmio_mmc_host *host)
{
	return container_of(host->pdata, struct uniphier_sd_priv, tmio_data);
}

static void uniphier_sd_dma_endisable(struct tmio_mmc_host *host, int enable)
{
	sd_ctrl_write16(host, CTL_DMA_ENABLE, enable ? DMA_ENABLE_DMASDRW : 0);
}

/* external DMA engine */
static void uniphier_sd_external_dma_issue(unsigned long arg)
{
	struct tmio_mmc_host *host = (void *)arg;
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);

	uniphier_sd_dma_endisable(host, 1);
	dma_async_issue_pending(priv->chan);
}

static void uniphier_sd_external_dma_callback(void *param,
					const struct dmaengine_result *result)
{
	struct tmio_mmc_host *host = param;
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	unsigned long flags;

	dma_unmap_sg(mmc_dev(host->mmc), host->sg_ptr, host->sg_len,
		     priv->dma_dir);

	spin_lock_irqsave(&host->lock, flags);

	if (result->result == DMA_TRANS_NOERROR) {
		/*
		 * When the external DMA engine is enabled, strangely enough,
		 * the DATAEND flag can be asserted even if the DMA engine has
		 * not been kicked yet.  Enable the TMIO_STAT_DATAEND irq only
		 * after we make sure the DMA engine finishes the transfer,
		 * hence, in this callback.
		 */
		tmio_mmc_enable_mmc_irqs(host, TMIO_STAT_DATAEND);
	} else {
		host->data->error = -ETIMEDOUT;
		tmio_mmc_do_data_irq(host);
	}

	spin_unlock_irqrestore(&host->lock, flags);
}

static void uniphier_sd_external_dma_start(struct tmio_mmc_host *host,
					   struct mmc_data *data)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	enum dma_transfer_direction dma_tx_dir;
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
	int sg_len;

	if (!priv->chan)
		goto force_pio;

	if (data->flags & MMC_DATA_READ) {
		priv->dma_dir = DMA_FROM_DEVICE;
		dma_tx_dir = DMA_DEV_TO_MEM;
	} else {
		priv->dma_dir = DMA_TO_DEVICE;
		dma_tx_dir = DMA_MEM_TO_DEV;
	}

	sg_len = dma_map_sg(mmc_dev(host->mmc), host->sg_ptr, host->sg_len,
			    priv->dma_dir);
	if (sg_len == 0)
		goto force_pio;

	desc = dmaengine_prep_slave_sg(priv->chan, host->sg_ptr, sg_len,
				       dma_tx_dir, DMA_CTRL_ACK);
	if (!desc)
		goto unmap_sg;

	desc->callback_result = uniphier_sd_external_dma_callback;
	desc->callback_param = host;

	cookie = dmaengine_submit(desc);
	if (cookie < 0)
		goto unmap_sg;

	host->dma_on = true;

	return;

unmap_sg:
	dma_unmap_sg(mmc_dev(host->mmc), host->sg_ptr, host->sg_len,
		     priv->dma_dir);
force_pio:
	uniphier_sd_dma_endisable(host, 0);
}

static void uniphier_sd_external_dma_enable(struct tmio_mmc_host *host,
					    bool enable)
{
}

static void uniphier_sd_external_dma_request(struct tmio_mmc_host *host,
					     struct tmio_mmc_data *pdata)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	struct dma_chan *chan;

	chan = dma_request_chan(mmc_dev(host->mmc), "rx-tx");
	if (IS_ERR(chan)) {
		dev_warn(mmc_dev(host->mmc),
			 "failed to request DMA channel. falling back to PIO\n");
		return;	/* just use PIO even for -EPROBE_DEFER */
	}

	/* this driver uses a single channel for both RX an TX */
	priv->chan = chan;
	host->chan_rx = chan;
	host->chan_tx = chan;

	tasklet_init(&host->dma_issue, uniphier_sd_external_dma_issue,
		     (unsigned long)host);
}

static void uniphier_sd_external_dma_release(struct tmio_mmc_host *host)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);

	if (priv->chan)
		dma_release_channel(priv->chan);
}

static void uniphier_sd_external_dma_abort(struct tmio_mmc_host *host)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);

	uniphier_sd_dma_endisable(host, 0);

	if (priv->chan)
		dmaengine_terminate_sync(priv->chan);
}

static void uniphier_sd_external_dma_dataend(struct tmio_mmc_host *host)
{
	uniphier_sd_dma_endisable(host, 0);

	tmio_mmc_do_data_irq(host);
}

static const struct tmio_mmc_dma_ops uniphier_sd_external_dma_ops = {
	.start = uniphier_sd_external_dma_start,
	.enable = uniphier_sd_external_dma_enable,
	.request = uniphier_sd_external_dma_request,
	.release = uniphier_sd_external_dma_release,
	.abort = uniphier_sd_external_dma_abort,
	.dataend = uniphier_sd_external_dma_dataend,
};

static void uniphier_sd_internal_dma_issue(unsigned long arg)
{
	struct tmio_mmc_host *host = (void *)arg;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	tmio_mmc_enable_mmc_irqs(host, TMIO_STAT_DATAEND);
	spin_unlock_irqrestore(&host->lock, flags);

	uniphier_sd_dma_endisable(host, 1);
	writel(UNIPHIER_SD_DMA_CTL_START, host->ctl + UNIPHIER_SD_DMA_CTL);
}

static void uniphier_sd_internal_dma_start(struct tmio_mmc_host *host,
					   struct mmc_data *data)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	struct scatterlist *sg = host->sg_ptr;
	dma_addr_t dma_addr;
	unsigned int dma_mode_dir;
	u32 dma_mode;
	int sg_len;

	if ((data->flags & MMC_DATA_READ) && !host->chan_rx)
		goto force_pio;

	if (WARN_ON(host->sg_len != 1))
		goto force_pio;

	if (!IS_ALIGNED(sg->offset, 8))
		goto force_pio;

	if (data->flags & MMC_DATA_READ) {
		priv->dma_dir = DMA_FROM_DEVICE;
		dma_mode_dir = UNIPHIER_SD_DMA_MODE_DIR_FROM_DEV;
	} else {
		priv->dma_dir = DMA_TO_DEVICE;
		dma_mode_dir = UNIPHIER_SD_DMA_MODE_DIR_TO_DEV;
	}

	sg_len = dma_map_sg(mmc_dev(host->mmc), sg, 1, priv->dma_dir);
	if (sg_len == 0)
		goto force_pio;

	dma_mode = FIELD_PREP(UNIPHIER_SD_DMA_MODE_DIR_MASK, dma_mode_dir);
	dma_mode |= FIELD_PREP(UNIPHIER_SD_DMA_MODE_WIDTH_MASK,
			       UNIPHIER_SD_DMA_MODE_WIDTH_64);
	dma_mode |= UNIPHIER_SD_DMA_MODE_ADDR_INC;

	writel(dma_mode, host->ctl + UNIPHIER_SD_DMA_MODE);

	dma_addr = sg_dma_address(data->sg);
	writel(lower_32_bits(dma_addr), host->ctl + UNIPHIER_SD_DMA_ADDR_L);
	writel(upper_32_bits(dma_addr), host->ctl + UNIPHIER_SD_DMA_ADDR_H);

	host->dma_on = true;

	return;
force_pio:
	uniphier_sd_dma_endisable(host, 0);
}

static void uniphier_sd_internal_dma_enable(struct tmio_mmc_host *host,
					    bool enable)
{
}

static void uniphier_sd_internal_dma_request(struct tmio_mmc_host *host,
					     struct tmio_mmc_data *pdata)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);

	/*
	 * Due to a hardware bug, Pro5 cannot use DMA for RX.
	 * We can still use DMA for TX, but PIO for RX.
	 */
	if (!(priv->caps & UNIPHIER_SD_CAP_BROKEN_DMA_RX))
		host->chan_rx = (void *)0xdeadbeaf;

	host->chan_tx = (void *)0xdeadbeaf;

	tasklet_init(&host->dma_issue, uniphier_sd_internal_dma_issue,
		     (unsigned long)host);
}

static void uniphier_sd_internal_dma_release(struct tmio_mmc_host *host)
{
	/* Each value is set to zero to assume "disabling" each DMA */
	host->chan_rx = NULL;
	host->chan_tx = NULL;
}

static void uniphier_sd_internal_dma_abort(struct tmio_mmc_host *host)
{
	u32 tmp;

	uniphier_sd_dma_endisable(host, 0);

	tmp = readl(host->ctl + UNIPHIER_SD_DMA_RST);
	tmp &= ~(UNIPHIER_SD_DMA_RST_CH1 | UNIPHIER_SD_DMA_RST_CH0);
	writel(tmp, host->ctl + UNIPHIER_SD_DMA_RST);

	tmp |= UNIPHIER_SD_DMA_RST_CH1 | UNIPHIER_SD_DMA_RST_CH0;
	writel(tmp, host->ctl + UNIPHIER_SD_DMA_RST);
}

static void uniphier_sd_internal_dma_dataend(struct tmio_mmc_host *host)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);

	uniphier_sd_dma_endisable(host, 0);
	dma_unmap_sg(mmc_dev(host->mmc), host->sg_ptr, 1, priv->dma_dir);

	tmio_mmc_do_data_irq(host);
}

static const struct tmio_mmc_dma_ops uniphier_sd_internal_dma_ops = {
	.start = uniphier_sd_internal_dma_start,
	.enable = uniphier_sd_internal_dma_enable,
	.request = uniphier_sd_internal_dma_request,
	.release = uniphier_sd_internal_dma_release,
	.abort = uniphier_sd_internal_dma_abort,
	.dataend = uniphier_sd_internal_dma_dataend,
};

static int uniphier_sd_clk_enable(struct tmio_mmc_host *host)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	struct mmc_host *mmc = host->mmc;
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	ret = clk_set_rate(priv->clk, ULONG_MAX);
	if (ret)
		goto disable_clk;

	priv->clk_rate = clk_get_rate(priv->clk);

	/* If max-frequency property is set, use it. */
	if (!mmc->f_max)
		mmc->f_max = priv->clk_rate;

	/*
	 * 1/512 is the finest divisor in the original IP.  Newer versions
	 * also supports 1/1024 divisor. (UniPhier-specific extension)
	 */
	if (priv->caps & UNIPHIER_SD_CAP_EXTENDED_IP)
		mmc->f_min = priv->clk_rate / 1024;
	else
		mmc->f_min = priv->clk_rate / 512;

	ret = reset_control_deassert(priv->rst);
	if (ret)
		goto disable_clk;

	ret = reset_control_deassert(priv->rst_br);
	if (ret)
		goto assert_rst;

	return 0;

assert_rst:
	reset_control_assert(priv->rst);
disable_clk:
	clk_disable_unprepare(priv->clk);

	return ret;
}

static void uniphier_sd_clk_disable(struct tmio_mmc_host *host)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);

	reset_control_assert(priv->rst_br);
	reset_control_assert(priv->rst);
	clk_disable_unprepare(priv->clk);
}

static void uniphier_sd_hw_reset(struct tmio_mmc_host *host)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);

	reset_control_assert(priv->rst_hw);
	/* For eMMC, minimum is 1us but give it 9us for good measure */
	udelay(9);
	reset_control_deassert(priv->rst_hw);
	/* For eMMC, minimum is 200us but give it 300us for good measure */
	usleep_range(300, 1000);
}

static void uniphier_sd_set_clock(struct tmio_mmc_host *host,
				  unsigned int clock)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	unsigned long divisor;
	u32 tmp;

	tmp = readl(host->ctl + (CTL_SD_CARD_CLK_CTL << 1));

	/* stop the clock before changing its rate to avoid a glitch signal */
	tmp &= ~CLK_CTL_SCLKEN;
	writel(tmp, host->ctl + (CTL_SD_CARD_CLK_CTL << 1));

	if (clock == 0)
		return;

	tmp &= ~UNIPHIER_SD_CLK_CTL_DIV1024;
	tmp &= ~UNIPHIER_SD_CLK_CTL_DIV1;
	tmp &= ~CLK_CTL_DIV_MASK;

	divisor = priv->clk_rate / clock;

	/*
	 * In the original IP, bit[7:0] represents the divisor.
	 * bit7 set: 1/512, ... bit0 set:1/4, all bits clear: 1/2
	 *
	 * The IP does not define a way to achieve 1/1.  For UniPhier variants,
	 * bit10 is used for 1/1.  Newer versions of UniPhier variants use
	 * bit16 for 1/1024.
	 */
	if (divisor <= 1)
		tmp |= UNIPHIER_SD_CLK_CTL_DIV1;
	else if (priv->caps & UNIPHIER_SD_CAP_EXTENDED_IP && divisor > 512)
		tmp |= UNIPHIER_SD_CLK_CTL_DIV1024;
	else
		tmp |= roundup_pow_of_two(divisor) >> 2;

	writel(tmp, host->ctl + (CTL_SD_CARD_CLK_CTL << 1));

	tmp |= CLK_CTL_SCLKEN;
	writel(tmp, host->ctl + (CTL_SD_CARD_CLK_CTL << 1));
}

static void uniphier_sd_host_init(struct tmio_mmc_host *host)
{
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	u32 val;

	/*
	 * Connected to 32bit AXI.
	 * This register holds settings for SoC-specific internal bus
	 * connection.  What is worse, the register spec was changed,
	 * breaking the backward compatibility.  Write an appropriate
	 * value depending on a flag associated with a compatible string.
	 */
	if (priv->caps & UNIPHIER_SD_CAP_EXTENDED_IP)
		val = 0x00000101;
	else
		val = 0x00000000;

	writel(val, host->ctl + UNIPHIER_SD_HOST_MODE);

	val = 0;
	/*
	 * If supported, the controller can automatically
	 * enable/disable the clock line to the card.
	 */
	if (priv->caps & UNIPHIER_SD_CAP_EXTENDED_IP)
		val |= UNIPHIER_SD_CLKCTL_OFFEN;

	writel(val, host->ctl + (CTL_SD_CARD_CLK_CTL << 1));
}

static int uniphier_sd_start_signal_voltage_switch(struct mmc_host *mmc,
						   struct mmc_ios *ios)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct uniphier_sd_priv *priv = uniphier_sd_priv(host);
	struct pinctrl_state *pinstate;
	u32 val, tmp;

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		val = UNIPHIER_SD_VOLT_330;
		pinstate = priv->pinstate_default;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		val = UNIPHIER_SD_VOLT_180;
		pinstate = priv->pinstate_uhs;
		break;
	default:
		return -ENOTSUPP;
	}

	tmp = readl(host->ctl + UNIPHIER_SD_VOLT);
	tmp &= ~UNIPHIER_SD_VOLT_MASK;
	tmp |= FIELD_PREP(UNIPHIER_SD_VOLT_MASK, val);
	writel(tmp, host->ctl + UNIPHIER_SD_VOLT);

	pinctrl_select_state(priv->pinctrl, pinstate);

	return 0;
}

static int uniphier_sd_uhs_init(struct tmio_mmc_host *host,
				struct uniphier_sd_priv *priv)
{
	priv->pinctrl = devm_pinctrl_get(mmc_dev(host->mmc));
	if (IS_ERR(priv->pinctrl))
		return PTR_ERR(priv->pinctrl);

	priv->pinstate_default = pinctrl_lookup_state(priv->pinctrl,
						      PINCTRL_STATE_DEFAULT);
	if (IS_ERR(priv->pinstate_default))
		return PTR_ERR(priv->pinstate_default);

	priv->pinstate_uhs = pinctrl_lookup_state(priv->pinctrl, "uhs");
	if (IS_ERR(priv->pinstate_uhs))
		return PTR_ERR(priv->pinstate_uhs);

	host->ops.start_signal_voltage_switch =
					uniphier_sd_start_signal_voltage_switch;

	return 0;
}

static int uniphier_sd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_sd_priv *priv;
	struct tmio_mmc_data *tmio_data;
	struct tmio_mmc_host *host;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->caps = (unsigned long)of_device_get_match_data(dev);

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(priv->clk);
	}

	priv->rst = devm_reset_control_get_shared(dev, "host");
	if (IS_ERR(priv->rst)) {
		dev_err(dev, "failed to get host reset\n");
		return PTR_ERR(priv->rst);
	}

	/* old version has one more reset */
	if (!(priv->caps & UNIPHIER_SD_CAP_EXTENDED_IP)) {
		priv->rst_br = devm_reset_control_get_shared(dev, "bridge");
		if (IS_ERR(priv->rst_br)) {
			dev_err(dev, "failed to get bridge reset\n");
			return PTR_ERR(priv->rst_br);
		}
	}

	tmio_data = &priv->tmio_data;
	tmio_data->flags |= TMIO_MMC_32BIT_DATA_PORT;

	host = tmio_mmc_host_alloc(pdev, tmio_data);
	if (IS_ERR(host))
		return PTR_ERR(host);

	if (host->mmc->caps & MMC_CAP_HW_RESET) {
		priv->rst_hw = devm_reset_control_get_exclusive(dev, "hw");
		if (IS_ERR(priv->rst_hw)) {
			dev_err(dev, "failed to get hw reset\n");
			ret = PTR_ERR(priv->rst_hw);
			goto free_host;
		}
		host->hw_reset = uniphier_sd_hw_reset;
	}

	if (host->mmc->caps & MMC_CAP_UHS) {
		ret = uniphier_sd_uhs_init(host, priv);
		if (ret) {
			dev_warn(dev,
				 "failed to setup UHS (error %d).  Disabling UHS.",
				 ret);
			host->mmc->caps &= ~MMC_CAP_UHS;
		}
	}

	ret = devm_request_irq(dev, irq, tmio_mmc_irq, IRQF_SHARED,
			       dev_name(dev), host);
	if (ret)
		goto free_host;

	if (priv->caps & UNIPHIER_SD_CAP_EXTENDED_IP)
		host->dma_ops = &uniphier_sd_internal_dma_ops;
	else
		host->dma_ops = &uniphier_sd_external_dma_ops;

	host->bus_shift = 1;
	host->clk_enable = uniphier_sd_clk_enable;
	host->clk_disable = uniphier_sd_clk_disable;
	host->set_clock = uniphier_sd_set_clock;

	ret = uniphier_sd_clk_enable(host);
	if (ret)
		goto free_host;

	uniphier_sd_host_init(host);

	tmio_data->ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34;
	if (host->mmc->caps & MMC_CAP_UHS)
		tmio_data->ocr_mask |= MMC_VDD_165_195;

	tmio_data->max_segs = 1;
	tmio_data->max_blk_count = U16_MAX;

	ret = tmio_mmc_host_probe(host);
	if (ret)
		goto free_host;

	return 0;

free_host:
	tmio_mmc_host_free(host);

	return ret;
}

static int uniphier_sd_remove(struct platform_device *pdev)
{
	struct tmio_mmc_host *host = platform_get_drvdata(pdev);

	tmio_mmc_host_remove(host);
	uniphier_sd_clk_disable(host);

	return 0;
}

static const struct of_device_id uniphier_sd_match[] = {
	{
		.compatible = "socionext,uniphier-sd-v2.91",
	},
	{
		.compatible = "socionext,uniphier-sd-v3.1",
		.data = (void *)(UNIPHIER_SD_CAP_EXTENDED_IP |
				 UNIPHIER_SD_CAP_BROKEN_DMA_RX),
	},
	{
		.compatible = "socionext,uniphier-sd-v3.1.1",
		.data = (void *)UNIPHIER_SD_CAP_EXTENDED_IP,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_sd_match);

static struct platform_driver uniphier_sd_driver = {
	.probe = uniphier_sd_probe,
	.remove = uniphier_sd_remove,
	.driver = {
		.name = "uniphier-sd",
		.of_match_table = uniphier_sd_match,
	},
};
module_platform_driver(uniphier_sd_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier SD/eMMC host controller driver");
MODULE_LICENSE("GPL v2");
