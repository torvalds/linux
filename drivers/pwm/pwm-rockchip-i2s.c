// SPDX-License-Identifier: GPL-2.0

/*
 * PWM-I2S driver for Rockchip SoCs
 *
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

/* transmit operation control register */
#define I2S_TXCR_FBM_MSB		0
#define I2S_TXCR_FBM_LSB		BIT(11)
#define I2S_TXCR_IBM_NORMAL		0
#define I2S_TXCR_IBM_LSJM		BIT(9)
#define I2S_TXCR_IBM_RSJM		BIT(10)
#define I2S_TXCR_IBM_MASK		GENMASK(10, 9)
#define I2S_TXCR_VDW(x)			((x) - 1)
#define I2S_TXCR_VDW_MASK		GENMASK(4, 0)

/* clock generation register */
#define I2S_CKR_TSD(x)			((x) - 1)
#define I2S_CKR_TSD_MASK		GENMASK(7, 0)

/* DMA control register */
#define I2S_DMACR_TDE_DISABLE		0
#define I2S_DMACR_TDE_ENABLE		BIT(8)
#define I2S_DMACR_TDL(x)		(x)
#define I2S_DMACR_TDL_MASK		GENMASK(4, 0)

/* Transfer start register */
#define I2S_XFER_TXS_STOP		0
#define I2S_XFER_TXS_START		BIT(0)

/* clear SCLK domain logic register */
#define I2S_CLR_TXC			BIT(0)

/* Mclk div register */
#define I2S_CLKDIV_TXM(x)		((x) - 1)

/* I2S REGS */
#define I2S_TXCR			(0x0000)
#define I2S_RXCR			(0x0004)
#define I2S_CKR				(0x0008)
#define I2S_FIFOLR			(0x000c)
#define I2S_DMACR			(0x0010)
#define I2S_INTCR			(0x0014)
#define I2S_INTSR			(0x0018)
#define I2S_XFER			(0x001c)
#define I2S_CLR				(0x0020)
#define I2S_TXDR			(0x0024)
#define I2S_RXDR			(0x0028)
#define I2S_TDM_TXCR			(0x0030)
#define I2S_TDM_RXCR			(0x0034)
#define I2S_CLKDIV			(0x0038)

/* Hardware Param */
#define I2S_FORMAT_BITS			32
#define	I2S_CHANNEL_NUM			2
#define I2S_FRAME_BITS			(I2S_FORMAT_BITS * I2S_CHANNEL_NUM)
#define I2S_FRAME_BYTES			(I2S_FRAME_BITS / 8)
#define I2S_FIFO_WATERMARK_LEVEL	30

#define I2S_DMA_BUFFER_SIZE		256
#define I2S_DMA_BUFFER_FRAME_SIZE	(I2S_DMA_BUFFER_SIZE / I2S_FRAME_BYTES)

struct rockchip_i2s_pwm_dma {
	struct dma_chan         *chan_tx;
	dma_addr_t              tx_addr;
	char                    *tx_buff;
	dma_cookie_t            tx_cookie;
};

struct rockchip_i2s_pwm_chip {
	struct pwm_chip chip;
	struct clk *hclk;
	struct clk *mclk;
	void __iomem *base;
	struct rockchip_i2s_pwm_dma dma;
	const struct rockchip_i2s_pwm_data *data;

	struct pwm_state pwm_state;
};

struct rockchip_i2s_pwm_data {
	unsigned int reg_clkdiv;
	unsigned int bit_clkdiv;
	unsigned int mask_clkdiv;
};

static inline
struct rockchip_i2s_pwm_chip *to_rockchip_i2s_pwm_chip(struct pwm_chip *c)
{
	return container_of(c, struct rockchip_i2s_pwm_chip, chip);
}

static void rockchip_i2s_pwm_get_state(struct pwm_chip *chip,
				       struct pwm_device *pwm,
				       struct pwm_state *state)
{
	struct rockchip_i2s_pwm_chip *pc = to_rockchip_i2s_pwm_chip(chip);
	u32 ctrl;
	int ret;

	ret = clk_enable(pc->hclk);
	if (ret)
		return;

	memcpy(state, &pc->pwm_state, sizeof(struct pwm_state));

	ctrl = readl_relaxed(pc->base + I2S_XFER);
	if (ctrl & I2S_XFER_TXS_START)
		state->enabled = true;
	else
		state->enabled = false;

	clk_disable(pc->hclk);
}

static int rockchip_i2s_pwm_config(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   struct pwm_state *state)
{
	struct rockchip_i2s_pwm_chip *pc = to_rockchip_i2s_pwm_chip(chip);
	unsigned long div_bclk;
	unsigned long flags;
	u64 mclk_rate, period_div, duty, duty_div;
	unsigned int div_val;
	int ret, i;

	ret = clk_enable(pc->hclk);
	if (ret)
		return ret;
	/*
	 * Assume the time of a frame is a period of pwm, so a frame is the unit
	 * of the pwm, we have to config the buffer per frame.
	 */
	mclk_rate = clk_get_rate(pc->mclk);
	period_div = mclk_rate * state->period;
	div_bclk = DIV_ROUND_CLOSEST(period_div, I2S_FRAME_BITS * NSEC_PER_SEC);

	/*
	 * The duty pecent is equal to the bits percent at whole frame, as the
	 * time of a frame is a period.
	 */
	duty_div = DIV_ROUND_CLOSEST(I2S_FRAME_BITS * state->duty_cycle,
				     state->period);
	if (duty_div > 0)
		duty = GENMASK_ULL(duty_div - 1, 0);
	else
		duty = 0;

	if (state->polarity == PWM_POLARITY_INVERSED)
		duty = ~duty;

	local_irq_save(flags);

	div_val = readl_relaxed(pc->base + pc->data->reg_clkdiv);
	div_val &= ~pc->data->mask_clkdiv;
	writel_relaxed((I2S_CLKDIV_TXM(div_bclk) << pc->data->bit_clkdiv)
		       | div_val, pc->base + pc->data->reg_clkdiv);

	for (i = 0; i < I2S_DMA_BUFFER_FRAME_SIZE; i++)
		memcpy((u64 *)pc->dma.tx_buff + i, &duty, sizeof(u64));

	pc->pwm_state.period = state->period;
	pc->pwm_state.duty_cycle = state->duty_cycle;
	pc->pwm_state.polarity = state->polarity;

	local_irq_restore(flags);
	clk_disable(pc->hclk);

	return ret;
}

static int rockchip_i2s_pwm_enable(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   bool enable)
{
	struct rockchip_i2s_pwm_chip *pc = to_rockchip_i2s_pwm_chip(chip);
	struct rockchip_i2s_pwm_dma *dma = &pc->dma;
	struct dma_async_tx_descriptor *tx_desc;
	int ret, retry = 10;
	u32 val;

	if (enable) {
		ret = clk_enable(pc->hclk);
		if (ret)
			return ret;

		ret = clk_enable(pc->mclk);
		if (ret)
			goto err_mclk;

		tx_desc = dmaengine_prep_dma_cyclic(dma->chan_tx, dma->tx_addr,
						    I2S_DMA_BUFFER_SIZE,
						    I2S_DMA_BUFFER_SIZE,
						    DMA_MEM_TO_DEV,
						    DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!tx_desc) {
			dev_err(chip->dev, "Not able to get tx desc for DMA\n");
			ret = -EBUSY;
			goto out;
		}

		tx_desc->callback = NULL;
		tx_desc->callback_param = NULL;
		dma->tx_cookie = dmaengine_submit(tx_desc);
		ret = dma_submit_error(dma->tx_cookie);
		if (ret) {
			dev_err(chip->dev, "DMA submit failed\n");
			goto out;
		}

		dma_async_issue_pending(pc->dma.chan_tx);

		val = readl_relaxed(pc->base + I2S_DMACR);
		val &= ~I2S_DMACR_TDE_ENABLE;
		writel_relaxed(val | I2S_DMACR_TDE_ENABLE,
			       pc->base + I2S_DMACR);

		val = readl_relaxed(pc->base + I2S_XFER);
		val &= ~I2S_XFER_TXS_START;
		writel_relaxed(val | I2S_XFER_TXS_START, pc->base + I2S_XFER);
	} else {
		dmaengine_terminate_all(pc->dma.chan_tx);

		val = readl_relaxed(pc->base + I2S_DMACR);
		val &= ~I2S_DMACR_TDE_ENABLE;
		writel_relaxed(val | I2S_DMACR_TDE_DISABLE,
			       pc->base + I2S_DMACR);

		val = readl_relaxed(pc->base + I2S_XFER);
		val &= ~I2S_XFER_TXS_START;
		writel_relaxed(val | I2S_XFER_TXS_STOP, pc->base + I2S_XFER);

		usleep_range(100, 150);

		val = readl_relaxed(pc->base + I2S_CLR);
		val &= ~I2S_CLR_TXC;
		writel_relaxed(val | I2S_CLR_TXC, pc->base + I2S_CLR);

		/* Should wait for clear operation to finish */
		do {
			val = readl_relaxed(pc->base + I2S_CLR);
			if (val)
				break;
		} while (--retry);

		if (!retry)
			dev_warn(chip->dev, "fail to clear\n");

		clk_disable(pc->mclk);
		clk_disable(pc->hclk);
	}

	return 0;

out:
	clk_disable(pc->mclk);
err_mclk:
	clk_disable(pc->hclk);

	return ret;
}

static int rockchip_i2s_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
				  struct pwm_state *state)
{
	struct pwm_state curstate;
	bool enabled;
	int ret;

	pwm_get_state(pwm, &curstate);
	enabled = curstate.enabled;

	ret = rockchip_i2s_pwm_config(chip, pwm, state);
	if (ret)
		return ret;

	if (state->enabled != enabled) {
		ret = rockchip_i2s_pwm_enable(chip, pwm, state->enabled);
		if (ret)
			return ret;
	}

	rockchip_i2s_pwm_get_state(chip, pwm, state);

	return 0;
}

static const struct pwm_ops rockchip_i2s_pwm_ops = {
	.get_state = rockchip_i2s_pwm_get_state,
	.apply = rockchip_i2s_pwm_apply,
	.owner = THIS_MODULE,
};

static int rockchip_i2s_pwm_dma_request(struct rockchip_i2s_pwm_chip *pc,
					struct device *dev,
					dma_addr_t phy_addr)
{
	struct rockchip_i2s_pwm_dma *dma = &pc->dma;
	struct dma_slave_config dma_sconfig;
	int ret;

	dma->chan_tx = dma_request_slave_channel(dev, "tx");
	if (!dma->chan_tx) {
		dev_err(dev, "can't request DMA tx channel\n");
		return -ENODEV;
	}

	dma_sconfig.direction = DMA_MEM_TO_DEV;
	dma_sconfig.dst_addr = phy_addr;
	dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dma_sconfig.dst_maxburst = 2;

	ret = dmaengine_slave_config(dma->chan_tx, &dma_sconfig);
	if (ret < 0) {
		dev_err(dev, "can't configure tx channel\n");
		goto fail;
	}

	dma->tx_buff = dma_alloc_coherent(dev, I2S_DMA_BUFFER_SIZE,
					  &dma->tx_addr, GFP_KERNEL);
	if (!dma->tx_buff) {
		ret = -ENOMEM;
		goto fail;
	}

	return 0;
fail:
	dma_release_channel(dma->chan_tx);
	return ret;
}

static void rockchip_i2s_pwm_dma_release(struct rockchip_i2s_pwm_chip *pc)
{
	struct rockchip_i2s_pwm_dma *dma = &pc->dma;
	struct device *dev = pc->chip.dev;

	dma_free_coherent(dev, I2S_DMA_BUFFER_SIZE, dma->tx_buff, dma->tx_addr);
	dma_release_channel(dma->chan_tx);
}

static int rockchip_i2s_pwm_hw_params(struct rockchip_i2s_pwm_chip *pc)
{
	unsigned int val = 0;
	int ret;

	ret = clk_enable(pc->hclk);
	if (ret)
		return ret;

	/* Config tx format bits with 32, LSB, left justified. */
	val = readl_relaxed(pc->base + I2S_TXCR);
	val &= ~(I2S_TXCR_VDW_MASK | I2S_TXCR_IBM_MASK | I2S_TXCR_FBM_LSB);
	writel_relaxed(val | I2S_TXCR_VDW(I2S_FORMAT_BITS) | I2S_TXCR_IBM_LSJM
		       | I2S_TXCR_FBM_LSB, pc->base + I2S_TXCR);

	val = readl_relaxed(pc->base + I2S_CKR);
	val &= ~I2S_CKR_TSD_MASK;
	writel_relaxed(val | I2S_CKR_TSD(I2S_FRAME_BITS), pc->base + I2S_CKR);

	/* Config the tx fifo watermark level to 30. */
	val = readl_relaxed(pc->base + I2S_DMACR);
	val &= ~I2S_DMACR_TDL_MASK;
	writel_relaxed(val | I2S_DMACR_TDL(I2S_FIFO_WATERMARK_LEVEL),
		       pc->base + I2S_DMACR);

	clk_disable(pc->hclk);
	return 0;
}

static const struct rockchip_i2s_pwm_data i2s_pwm_data_v1 = {
	.reg_clkdiv = 0x8,
	.bit_clkdiv = 16,
	.mask_clkdiv = GENMASK(23, 16),
};

static const struct rockchip_i2s_pwm_data i2s_pwm_data_v2 = {
	.reg_clkdiv = 0x38,
	.bit_clkdiv = 0,
	.mask_clkdiv = GENMASK(7, 0),
};

static const struct of_device_id rockchip_i2s_pwm_match[] = {
	{ .compatible = "rockchip,i2s-pwm", .data = &i2s_pwm_data_v1 },
	{ .compatible = "rockchip,rk3308-i2s-pwm", .data = &i2s_pwm_data_v2 },
	{ /* sentinel */ },
};

static int rockchip_i2s_pwm_probe(struct platform_device *pdev)
{
	struct rockchip_i2s_pwm_chip *pc;
	const struct of_device_id *id;
	struct resource *res;
	int ret;

	id = of_match_device(rockchip_i2s_pwm_match, &pdev->dev);
	if (!id)
		return -EINVAL;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pc->base))
		return PTR_ERR(pc->base);

	pc->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(pc->hclk))
		return PTR_ERR(pc->hclk);

	pc->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(pc->mclk))
		return PTR_ERR(pc->mclk);

	ret = clk_prepare(pc->hclk);
	if (ret)
		return ret;

	ret = clk_prepare(pc->mclk);
	if (ret)
		goto err_hclk;

	pc->chip.dev = &pdev->dev;
	platform_set_drvdata(pdev, pc);

	ret = rockchip_i2s_pwm_hw_params(pc);
	if (ret)
		goto err_mclk;

	ret = rockchip_i2s_pwm_dma_request(pc, &pdev->dev,
					   res->start + I2S_TXDR);
	if (ret) {
		ret = -EPROBE_DEFER;
		goto err_mclk;
	}

	pc->data = id->data;
	pc->chip.ops = &rockchip_i2s_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = 1;
	pc->chip.of_xlate = of_pwm_xlate_with_flags;
	pc->chip.of_pwm_n_cells = 3;

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		rockchip_i2s_pwm_dma_release(pc);
		goto err_mclk;
	}

	return 0;

err_mclk:
	clk_unprepare(pc->mclk);
err_hclk:
	clk_unprepare(pc->hclk);

	return ret;
}

static int rockchip_i2s_pwm_remove(struct platform_device *pdev)
{
	struct rockchip_i2s_pwm_chip *pc = platform_get_drvdata(pdev);
	struct pwm_state curstate;

	pwm_get_state(pc->chip.pwms, &curstate);
	if (curstate.enabled)
		dmaengine_terminate_all(pc->dma.chan_tx);

	rockchip_i2s_pwm_dma_release(pc);

	clk_unprepare(pc->mclk);
	clk_unprepare(pc->hclk);

	return pwmchip_remove(&pc->chip);
}

static struct platform_driver rockchip_i2s_pwm_driver = {
	.driver = {
		.name = "rockchip-i2s-pwm",
		.of_match_table = rockchip_i2s_pwm_match,
	},
	.probe = rockchip_i2s_pwm_probe,
	.remove = rockchip_i2s_pwm_remove,
};
module_platform_driver(rockchip_i2s_pwm_driver);

MODULE_AUTHOR("David Wu <david.wu@rock-chip.com>");
MODULE_DESCRIPTION("ROCKCHIP I2S PWM driver");
MODULE_LICENSE("GPL v2");
