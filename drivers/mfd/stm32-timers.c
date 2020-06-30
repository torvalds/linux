// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2016
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com>
 */

#include <linux/bitfield.h>
#include <linux/mfd/stm32-timers.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/reset.h>

#define STM32_TIMERS_MAX_REGISTERS	0x3fc

/* DIER register DMA enable bits */
static const u32 stm32_timers_dier_dmaen[STM32_TIMERS_MAX_DMAS] = {
	TIM_DIER_CC1DE,
	TIM_DIER_CC2DE,
	TIM_DIER_CC3DE,
	TIM_DIER_CC4DE,
	TIM_DIER_UIE,
	TIM_DIER_TDE,
	TIM_DIER_COMDE
};

static void stm32_timers_dma_done(void *p)
{
	struct stm32_timers_dma *dma = p;
	struct dma_tx_state state;
	enum dma_status status;

	status = dmaengine_tx_status(dma->chan, dma->chan->cookie, &state);
	if (status == DMA_COMPLETE)
		complete(&dma->completion);
}

/**
 * stm32_timers_dma_burst_read - Read from timers registers using DMA.
 *
 * Read from STM32 timers registers using DMA on a single event.
 * @dev: reference to stm32_timers MFD device
 * @buf: DMA'able destination buffer
 * @id: stm32_timers_dmas event identifier (ch[1..4], up, trig or com)
 * @reg: registers start offset for DMA to read from (like CCRx for capture)
 * @num_reg: number of registers to read upon each DMA request, starting @reg.
 * @bursts: number of bursts to read (e.g. like two for pwm period capture)
 * @tmo_ms: timeout (milliseconds)
 */
int stm32_timers_dma_burst_read(struct device *dev, u32 *buf,
				enum stm32_timers_dmas id, u32 reg,
				unsigned int num_reg, unsigned int bursts,
				unsigned long tmo_ms)
{
	struct stm32_timers *ddata = dev_get_drvdata(dev);
	unsigned long timeout = msecs_to_jiffies(tmo_ms);
	struct regmap *regmap = ddata->regmap;
	struct stm32_timers_dma *dma = &ddata->dma;
	size_t len = num_reg * bursts * sizeof(u32);
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config config;
	dma_cookie_t cookie;
	dma_addr_t dma_buf;
	u32 dbl, dba;
	long err;
	int ret;

	/* Sanity check */
	if (id < STM32_TIMERS_DMA_CH1 || id >= STM32_TIMERS_MAX_DMAS)
		return -EINVAL;

	if (!num_reg || !bursts || reg > STM32_TIMERS_MAX_REGISTERS ||
	    (reg + num_reg * sizeof(u32)) > STM32_TIMERS_MAX_REGISTERS)
		return -EINVAL;

	if (!dma->chans[id])
		return -ENODEV;
	mutex_lock(&dma->lock);

	/* Select DMA channel in use */
	dma->chan = dma->chans[id];
	dma_buf = dma_map_single(dev, buf, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dma_buf)) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* Prepare DMA read from timer registers, using DMA burst mode */
	memset(&config, 0, sizeof(config));
	config.src_addr = (dma_addr_t)dma->phys_base + TIM_DMAR;
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	ret = dmaengine_slave_config(dma->chan, &config);
	if (ret)
		goto unmap;

	desc = dmaengine_prep_slave_single(dma->chan, dma_buf, len,
					   DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);
	if (!desc) {
		ret = -EBUSY;
		goto unmap;
	}

	desc->callback = stm32_timers_dma_done;
	desc->callback_param = dma;
	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret)
		goto dma_term;

	reinit_completion(&dma->completion);
	dma_async_issue_pending(dma->chan);

	/* Setup and enable timer DMA burst mode */
	dbl = FIELD_PREP(TIM_DCR_DBL, bursts - 1);
	dba = FIELD_PREP(TIM_DCR_DBA, reg >> 2);
	ret = regmap_write(regmap, TIM_DCR, dbl | dba);
	if (ret)
		goto dma_term;

	/* Clear pending flags before enabling DMA request */
	ret = regmap_write(regmap, TIM_SR, 0);
	if (ret)
		goto dcr_clr;

	ret = regmap_update_bits(regmap, TIM_DIER, stm32_timers_dier_dmaen[id],
				 stm32_timers_dier_dmaen[id]);
	if (ret)
		goto dcr_clr;

	err = wait_for_completion_interruptible_timeout(&dma->completion,
							timeout);
	if (err == 0)
		ret = -ETIMEDOUT;
	else if (err < 0)
		ret = err;

	regmap_update_bits(regmap, TIM_DIER, stm32_timers_dier_dmaen[id], 0);
	regmap_write(regmap, TIM_SR, 0);
dcr_clr:
	regmap_write(regmap, TIM_DCR, 0);
dma_term:
	dmaengine_terminate_all(dma->chan);
unmap:
	dma_unmap_single(dev, dma_buf, len, DMA_FROM_DEVICE);
unlock:
	dma->chan = NULL;
	mutex_unlock(&dma->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(stm32_timers_dma_burst_read);

static const struct regmap_config stm32_timers_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = sizeof(u32),
	.max_register = STM32_TIMERS_MAX_REGISTERS,
};

static void stm32_timers_get_arr_size(struct stm32_timers *ddata)
{
	/*
	 * Only the available bits will be written so when readback
	 * we get the maximum value of auto reload register
	 */
	regmap_write(ddata->regmap, TIM_ARR, ~0L);
	regmap_read(ddata->regmap, TIM_ARR, &ddata->max_arr);
	regmap_write(ddata->regmap, TIM_ARR, 0x0);
}

static int stm32_timers_dma_probe(struct device *dev,
				   struct stm32_timers *ddata)
{
	int i;
	int ret = 0;
	char name[4];

	init_completion(&ddata->dma.completion);
	mutex_init(&ddata->dma.lock);

	/* Optional DMA support: get valid DMA channel(s) or NULL */
	for (i = STM32_TIMERS_DMA_CH1; i <= STM32_TIMERS_DMA_CH4; i++) {
		snprintf(name, ARRAY_SIZE(name), "ch%1d", i + 1);
		ddata->dma.chans[i] = dma_request_chan(dev, name);
	}
	ddata->dma.chans[STM32_TIMERS_DMA_UP] = dma_request_chan(dev, "up");
	ddata->dma.chans[STM32_TIMERS_DMA_TRIG] = dma_request_chan(dev, "trig");
	ddata->dma.chans[STM32_TIMERS_DMA_COM] = dma_request_chan(dev, "com");

	for (i = STM32_TIMERS_DMA_CH1; i < STM32_TIMERS_MAX_DMAS; i++) {
		if (IS_ERR(ddata->dma.chans[i])) {
			/* Save the first error code to return */
			if (PTR_ERR(ddata->dma.chans[i]) != -ENODEV && !ret)
				ret = PTR_ERR(ddata->dma.chans[i]);

			ddata->dma.chans[i] = NULL;
		}
	}

	return ret;
}

static void stm32_timers_dma_remove(struct device *dev,
				    struct stm32_timers *ddata)
{
	int i;

	for (i = STM32_TIMERS_DMA_CH1; i < STM32_TIMERS_MAX_DMAS; i++)
		if (ddata->dma.chans[i])
			dma_release_channel(ddata->dma.chans[i]);
}

static int stm32_timers_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm32_timers *ddata;
	struct resource *res;
	void __iomem *mmio;
	int ret;

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(mmio))
		return PTR_ERR(mmio);

	/* Timer physical addr for DMA */
	ddata->dma.phys_base = res->start;

	ddata->regmap = devm_regmap_init_mmio_clk(dev, "int", mmio,
						  &stm32_timers_regmap_cfg);
	if (IS_ERR(ddata->regmap))
		return PTR_ERR(ddata->regmap);

	ddata->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ddata->clk))
		return PTR_ERR(ddata->clk);

	stm32_timers_get_arr_size(ddata);

	ret = stm32_timers_dma_probe(dev, ddata);
	if (ret) {
		stm32_timers_dma_remove(dev, ddata);
		return ret;
	}

	platform_set_drvdata(pdev, ddata);

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret)
		stm32_timers_dma_remove(dev, ddata);

	return ret;
}

static int stm32_timers_remove(struct platform_device *pdev)
{
	struct stm32_timers *ddata = platform_get_drvdata(pdev);

	/*
	 * Don't use devm_ here: enfore of_platform_depopulate() happens before
	 * DMA are released, to avoid race on DMA.
	 */
	of_platform_depopulate(&pdev->dev);
	stm32_timers_dma_remove(&pdev->dev, ddata);

	return 0;
}

static const struct of_device_id stm32_timers_of_match[] = {
	{ .compatible = "st,stm32-timers", },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, stm32_timers_of_match);

static struct platform_driver stm32_timers_driver = {
	.probe = stm32_timers_probe,
	.remove = stm32_timers_remove,
	.driver	= {
		.name = "stm32-timers",
		.of_match_table = stm32_timers_of_match,
	},
};
module_platform_driver(stm32_timers_driver);

MODULE_DESCRIPTION("STMicroelectronics STM32 Timers");
MODULE_LICENSE("GPL v2");
