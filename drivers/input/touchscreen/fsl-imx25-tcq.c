/*
 * Copyright (C) 2014-2015 Pengutronix, Markus Pargmann <mpa@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Based on driver from 2011:
 *   Juergen Beisert, Pengutronix <kernel@pengutronix.de>
 *
 * This is the driver for the imx25 TCQ (Touchscreen Conversion Queue)
 * connected to the imx25 ADC.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/imx25-tsadc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

static const char mx25_tcq_name[] = "mx25-tcq";

enum mx25_tcq_mode {
	MX25_TS_4WIRE,
};

struct mx25_tcq_priv {
	struct regmap *regs;
	struct regmap *core_regs;
	struct input_dev *idev;
	enum mx25_tcq_mode mode;
	unsigned int pen_threshold;
	unsigned int sample_count;
	unsigned int expected_samples;
	unsigned int pen_debounce;
	unsigned int settling_time;
	struct clk *clk;
	int irq;
	struct device *dev;
};

static struct regmap_config mx25_tcq_regconfig = {
	.fast_io = true,
	.max_register = 0x5c,
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static const struct of_device_id mx25_tcq_ids[] = {
	{ .compatible = "fsl,imx25-tcq", },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, mx25_tcq_ids);

#define TSC_4WIRE_PRE_INDEX 0
#define TSC_4WIRE_X_INDEX 1
#define TSC_4WIRE_Y_INDEX 2
#define TSC_4WIRE_POST_INDEX 3
#define TSC_4WIRE_LEAVE 4

#define MX25_TSC_DEF_THRESHOLD 80
#define TSC_MAX_SAMPLES 16

#define MX25_TSC_REPEAT_WAIT 14

enum mx25_adc_configurations {
	MX25_CFG_PRECHARGE = 0,
	MX25_CFG_TOUCH_DETECT,
	MX25_CFG_X_MEASUREMENT,
	MX25_CFG_Y_MEASUREMENT,
};

#define MX25_PRECHARGE_VALUE (\
			MX25_ADCQ_CFG_YPLL_OFF | \
			MX25_ADCQ_CFG_XNUR_OFF | \
			MX25_ADCQ_CFG_XPUL_HIGH | \
			MX25_ADCQ_CFG_REFP_INT | \
			MX25_ADCQ_CFG_IN_XP | \
			MX25_ADCQ_CFG_REFN_NGND2 | \
			MX25_ADCQ_CFG_IGS)

#define MX25_TOUCH_DETECT_VALUE (\
			MX25_ADCQ_CFG_YNLR | \
			MX25_ADCQ_CFG_YPLL_OFF | \
			MX25_ADCQ_CFG_XNUR_OFF | \
			MX25_ADCQ_CFG_XPUL_OFF | \
			MX25_ADCQ_CFG_REFP_INT | \
			MX25_ADCQ_CFG_IN_XP | \
			MX25_ADCQ_CFG_REFN_NGND2 | \
			MX25_ADCQ_CFG_PENIACK)

static void imx25_setup_queue_cfgs(struct mx25_tcq_priv *priv,
				   unsigned int settling_cnt)
{
	u32 precharge_cfg =
			MX25_PRECHARGE_VALUE |
			MX25_ADCQ_CFG_SETTLING_TIME(settling_cnt);
	u32 touch_detect_cfg =
			MX25_TOUCH_DETECT_VALUE |
			MX25_ADCQ_CFG_NOS(1) |
			MX25_ADCQ_CFG_SETTLING_TIME(settling_cnt);

	regmap_write(priv->core_regs, MX25_TSC_TICR, precharge_cfg);

	/* PRECHARGE */
	regmap_write(priv->regs, MX25_ADCQ_CFG(MX25_CFG_PRECHARGE),
		     precharge_cfg);

	/* TOUCH_DETECT */
	regmap_write(priv->regs, MX25_ADCQ_CFG(MX25_CFG_TOUCH_DETECT),
		     touch_detect_cfg);

	/* X Measurement */
	regmap_write(priv->regs, MX25_ADCQ_CFG(MX25_CFG_X_MEASUREMENT),
		     MX25_ADCQ_CFG_YPLL_OFF |
		     MX25_ADCQ_CFG_XNUR_LOW |
		     MX25_ADCQ_CFG_XPUL_HIGH |
		     MX25_ADCQ_CFG_REFP_XP |
		     MX25_ADCQ_CFG_IN_YP |
		     MX25_ADCQ_CFG_REFN_XN |
		     MX25_ADCQ_CFG_NOS(priv->sample_count) |
		     MX25_ADCQ_CFG_SETTLING_TIME(settling_cnt));

	/* Y Measurement */
	regmap_write(priv->regs, MX25_ADCQ_CFG(MX25_CFG_Y_MEASUREMENT),
		     MX25_ADCQ_CFG_YNLR |
		     MX25_ADCQ_CFG_YPLL_HIGH |
		     MX25_ADCQ_CFG_XNUR_OFF |
		     MX25_ADCQ_CFG_XPUL_OFF |
		     MX25_ADCQ_CFG_REFP_YP |
		     MX25_ADCQ_CFG_IN_XP |
		     MX25_ADCQ_CFG_REFN_YN |
		     MX25_ADCQ_CFG_NOS(priv->sample_count) |
		     MX25_ADCQ_CFG_SETTLING_TIME(settling_cnt));

	/* Enable the touch detection right now */
	regmap_write(priv->core_regs, MX25_TSC_TICR, touch_detect_cfg |
		     MX25_ADCQ_CFG_IGS);
}

static int imx25_setup_queue_4wire(struct mx25_tcq_priv *priv,
				   unsigned settling_cnt, int *items)
{
	imx25_setup_queue_cfgs(priv, settling_cnt);

	/* Setup the conversion queue */
	regmap_write(priv->regs, MX25_ADCQ_ITEM_7_0,
		     MX25_ADCQ_ITEM(0, MX25_CFG_PRECHARGE) |
		     MX25_ADCQ_ITEM(1, MX25_CFG_TOUCH_DETECT) |
		     MX25_ADCQ_ITEM(2, MX25_CFG_X_MEASUREMENT) |
		     MX25_ADCQ_ITEM(3, MX25_CFG_Y_MEASUREMENT) |
		     MX25_ADCQ_ITEM(4, MX25_CFG_PRECHARGE) |
		     MX25_ADCQ_ITEM(5, MX25_CFG_TOUCH_DETECT));

	/*
	 * We measure X/Y with 'sample_count' number of samples and execute a
	 * touch detection twice, with 1 sample each
	 */
	priv->expected_samples = priv->sample_count * 2 + 2;
	*items = 6;

	return 0;
}

static void mx25_tcq_disable_touch_irq(struct mx25_tcq_priv *priv)
{
	regmap_update_bits(priv->regs, MX25_ADCQ_CR, MX25_ADCQ_CR_PDMSK,
			   MX25_ADCQ_CR_PDMSK);
}

static void mx25_tcq_enable_touch_irq(struct mx25_tcq_priv *priv)
{
	regmap_update_bits(priv->regs, MX25_ADCQ_CR, MX25_ADCQ_CR_PDMSK, 0);
}

static void mx25_tcq_disable_fifo_irq(struct mx25_tcq_priv *priv)
{
	regmap_update_bits(priv->regs, MX25_ADCQ_MR, MX25_ADCQ_MR_FDRY_IRQ,
			   MX25_ADCQ_MR_FDRY_IRQ);
}

static void mx25_tcq_enable_fifo_irq(struct mx25_tcq_priv *priv)
{
	regmap_update_bits(priv->regs, MX25_ADCQ_MR, MX25_ADCQ_MR_FDRY_IRQ, 0);
}

static void mx25_tcq_force_queue_start(struct mx25_tcq_priv *priv)
{
	regmap_update_bits(priv->regs, MX25_ADCQ_CR,
			   MX25_ADCQ_CR_FQS,
			   MX25_ADCQ_CR_FQS);
}

static void mx25_tcq_force_queue_stop(struct mx25_tcq_priv *priv)
{
	regmap_update_bits(priv->regs, MX25_ADCQ_CR,
			   MX25_ADCQ_CR_FQS, 0);
}

static void mx25_tcq_fifo_reset(struct mx25_tcq_priv *priv)
{
	u32 tcqcr;

	regmap_read(priv->regs, MX25_ADCQ_CR, &tcqcr);
	regmap_update_bits(priv->regs, MX25_ADCQ_CR, MX25_ADCQ_CR_FRST,
			   MX25_ADCQ_CR_FRST);
	regmap_update_bits(priv->regs, MX25_ADCQ_CR, MX25_ADCQ_CR_FRST, 0);
	regmap_write(priv->regs, MX25_ADCQ_CR, tcqcr);
}

static void mx25_tcq_re_enable_touch_detection(struct mx25_tcq_priv *priv)
{
	/* stop the queue from looping */
	mx25_tcq_force_queue_stop(priv);

	/* for a clean touch detection, preload the X plane */
	regmap_write(priv->core_regs, MX25_TSC_TICR, MX25_PRECHARGE_VALUE);

	/* waste some time now to pre-load the X plate to high voltage */
	mx25_tcq_fifo_reset(priv);

	/* re-enable the detection right now */
	regmap_write(priv->core_regs, MX25_TSC_TICR,
		     MX25_TOUCH_DETECT_VALUE | MX25_ADCQ_CFG_IGS);

	regmap_update_bits(priv->regs, MX25_ADCQ_SR, MX25_ADCQ_SR_PD,
			   MX25_ADCQ_SR_PD);

	/* enable the pen down event to be a source for the interrupt */
	regmap_update_bits(priv->regs, MX25_ADCQ_MR, MX25_ADCQ_MR_PD_IRQ, 0);

	/* lets fire the next IRQ if someone touches the touchscreen */
	mx25_tcq_enable_touch_irq(priv);
}

static void mx25_tcq_create_event_for_4wire(struct mx25_tcq_priv *priv,
					    u32 *sample_buf,
					    unsigned int samples)
{
	unsigned int x_pos = 0;
	unsigned int y_pos = 0;
	unsigned int touch_pre = 0;
	unsigned int touch_post = 0;
	unsigned int i;

	for (i = 0; i < samples; i++) {
		unsigned int index = MX25_ADCQ_FIFO_ID(sample_buf[i]);
		unsigned int val = MX25_ADCQ_FIFO_DATA(sample_buf[i]);

		switch (index) {
		case 1:
			touch_pre = val;
			break;
		case 2:
			x_pos = val;
			break;
		case 3:
			y_pos = val;
			break;
		case 5:
			touch_post = val;
			break;
		default:
			dev_dbg(priv->dev, "Dropped samples because of invalid index %d\n",
				index);
			return;
		}
	}

	if (samples != 0) {
		/*
		 * only if both touch measures are below a threshold,
		 * the position is valid
		 */
		if (touch_pre < priv->pen_threshold &&
		    touch_post < priv->pen_threshold) {
			/* valid samples, generate a report */
			x_pos /= priv->sample_count;
			y_pos /= priv->sample_count;
			input_report_abs(priv->idev, ABS_X, x_pos);
			input_report_abs(priv->idev, ABS_Y, y_pos);
			input_report_key(priv->idev, BTN_TOUCH, 1);
			input_sync(priv->idev);

			/* get next sample */
			mx25_tcq_enable_fifo_irq(priv);
		} else if (touch_pre >= priv->pen_threshold &&
			   touch_post >= priv->pen_threshold) {
			/*
			 * if both samples are invalid,
			 * generate a release report
			 */
			input_report_key(priv->idev, BTN_TOUCH, 0);
			input_sync(priv->idev);
			mx25_tcq_re_enable_touch_detection(priv);
		} else {
			/*
			 * if only one of both touch measurements are
			 * below the threshold, still some bouncing
			 * happens. Take additional samples in this
			 * case to be sure
			 */
			mx25_tcq_enable_fifo_irq(priv);
		}
	}
}

static irqreturn_t mx25_tcq_irq_thread(int irq, void *dev_id)
{
	struct mx25_tcq_priv *priv = dev_id;
	u32 sample_buf[TSC_MAX_SAMPLES];
	unsigned int samples;
	u32 stats;
	unsigned int i;

	/*
	 * Check how many samples are available. We always have to read exactly
	 * sample_count samples from the fifo, or a multiple of sample_count.
	 * Otherwise we mixup samples into different touch events.
	 */
	regmap_read(priv->regs, MX25_ADCQ_SR, &stats);
	samples = MX25_ADCQ_SR_FDN(stats);
	samples -= samples % priv->sample_count;

	if (!samples)
		return IRQ_HANDLED;

	for (i = 0; i != samples; ++i)
		regmap_read(priv->regs, MX25_ADCQ_FIFO, &sample_buf[i]);

	mx25_tcq_create_event_for_4wire(priv, sample_buf, samples);

	return IRQ_HANDLED;
}

static irqreturn_t mx25_tcq_irq(int irq, void *dev_id)
{
	struct mx25_tcq_priv *priv = dev_id;
	u32 stat;
	int ret = IRQ_HANDLED;

	regmap_read(priv->regs, MX25_ADCQ_SR, &stat);

	if (stat & (MX25_ADCQ_SR_FRR | MX25_ADCQ_SR_FUR | MX25_ADCQ_SR_FOR))
		mx25_tcq_re_enable_touch_detection(priv);

	if (stat & MX25_ADCQ_SR_PD) {
		mx25_tcq_disable_touch_irq(priv);
		mx25_tcq_force_queue_start(priv);
		mx25_tcq_enable_fifo_irq(priv);
	}

	if (stat & MX25_ADCQ_SR_FDRY) {
		mx25_tcq_disable_fifo_irq(priv);
		ret = IRQ_WAKE_THREAD;
	}

	regmap_update_bits(priv->regs, MX25_ADCQ_SR, MX25_ADCQ_SR_FRR |
			   MX25_ADCQ_SR_FUR | MX25_ADCQ_SR_FOR |
			   MX25_ADCQ_SR_PD,
			   MX25_ADCQ_SR_FRR | MX25_ADCQ_SR_FUR |
			   MX25_ADCQ_SR_FOR | MX25_ADCQ_SR_PD);

	return ret;
}

/* configure the state machine for a 4-wire touchscreen */
static int mx25_tcq_init(struct mx25_tcq_priv *priv)
{
	u32 tgcr;
	unsigned int ipg_div;
	unsigned int adc_period;
	unsigned int debounce_cnt;
	unsigned int settling_cnt;
	int itemct;
	int error;

	regmap_read(priv->core_regs, MX25_TSC_TGCR, &tgcr);
	ipg_div = max_t(unsigned int, 4, MX25_TGCR_GET_ADCCLK(tgcr));
	adc_period = USEC_PER_SEC * ipg_div * 2 + 2;
	adc_period /= clk_get_rate(priv->clk) / 1000 + 1;
	debounce_cnt = DIV_ROUND_UP(priv->pen_debounce, adc_period * 8) - 1;
	settling_cnt = DIV_ROUND_UP(priv->settling_time, adc_period * 8) - 1;

	/* Reset */
	regmap_write(priv->regs, MX25_ADCQ_CR,
		     MX25_ADCQ_CR_QRST | MX25_ADCQ_CR_FRST);
	regmap_update_bits(priv->regs, MX25_ADCQ_CR,
			   MX25_ADCQ_CR_QRST | MX25_ADCQ_CR_FRST, 0);

	/* up to 128 * 8 ADC clocks are possible */
	if (debounce_cnt > 127)
		debounce_cnt = 127;

	/* up to 255 * 8 ADC clocks are possible */
	if (settling_cnt > 255)
		settling_cnt = 255;

	error = imx25_setup_queue_4wire(priv, settling_cnt, &itemct);
	if (error)
		return error;

	regmap_update_bits(priv->regs, MX25_ADCQ_CR,
			   MX25_ADCQ_CR_LITEMID_MASK | MX25_ADCQ_CR_WMRK_MASK,
			   MX25_ADCQ_CR_LITEMID(itemct - 1) |
			   MX25_ADCQ_CR_WMRK(priv->expected_samples - 1));

	/* setup debounce count */
	regmap_update_bits(priv->core_regs, MX25_TSC_TGCR,
			   MX25_TGCR_PDBTIME_MASK,
			   MX25_TGCR_PDBTIME(debounce_cnt));

	/* enable debounce */
	regmap_update_bits(priv->core_regs, MX25_TSC_TGCR, MX25_TGCR_PDBEN,
			   MX25_TGCR_PDBEN);
	regmap_update_bits(priv->core_regs, MX25_TSC_TGCR, MX25_TGCR_PDEN,
			   MX25_TGCR_PDEN);

	/* enable the engine on demand */
	regmap_update_bits(priv->regs, MX25_ADCQ_CR, MX25_ADCQ_CR_QSM_MASK,
			   MX25_ADCQ_CR_QSM_FQS);

	/* Enable repeat and repeat wait */
	regmap_update_bits(priv->regs, MX25_ADCQ_CR,
			   MX25_ADCQ_CR_RPT | MX25_ADCQ_CR_RWAIT_MASK,
			   MX25_ADCQ_CR_RPT |
			   MX25_ADCQ_CR_RWAIT(MX25_TSC_REPEAT_WAIT));

	return 0;
}

static int mx25_tcq_parse_dt(struct platform_device *pdev,
			     struct mx25_tcq_priv *priv)
{
	struct device_node *np = pdev->dev.of_node;
	u32 wires;
	int error;

	/* Setup defaults */
	priv->pen_threshold = 500;
	priv->sample_count = 3;
	priv->pen_debounce = 1000000;
	priv->settling_time = 250000;

	error = of_property_read_u32(np, "fsl,wires", &wires);
	if (error) {
		dev_err(&pdev->dev, "Failed to find fsl,wires properties\n");
		return error;
	}

	if (wires == 4) {
		priv->mode = MX25_TS_4WIRE;
	} else {
		dev_err(&pdev->dev, "%u-wire mode not supported\n", wires);
		return -EINVAL;
	}

	/* These are optional, we don't care about the return values */
	of_property_read_u32(np, "fsl,pen-threshold", &priv->pen_threshold);
	of_property_read_u32(np, "fsl,settling-time-ns", &priv->settling_time);
	of_property_read_u32(np, "fsl,pen-debounce-ns", &priv->pen_debounce);

	return 0;
}

static int mx25_tcq_open(struct input_dev *idev)
{
	struct device *dev = &idev->dev;
	struct mx25_tcq_priv *priv = dev_get_drvdata(dev);
	int error;

	error = clk_prepare_enable(priv->clk);
	if (error) {
		dev_err(dev, "Failed to enable ipg clock\n");
		return error;
	}

	error = mx25_tcq_init(priv);
	if (error) {
		dev_err(dev, "Failed to init tcq\n");
		clk_disable_unprepare(priv->clk);
		return error;
	}

	mx25_tcq_re_enable_touch_detection(priv);

	return 0;
}

static void mx25_tcq_close(struct input_dev *idev)
{
	struct mx25_tcq_priv *priv = input_get_drvdata(idev);

	mx25_tcq_force_queue_stop(priv);
	mx25_tcq_disable_touch_irq(priv);
	mx25_tcq_disable_fifo_irq(priv);
	clk_disable_unprepare(priv->clk);
}

static int mx25_tcq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct input_dev *idev;
	struct mx25_tcq_priv *priv;
	struct mx25_tsadc *tsadc = dev_get_drvdata(dev->parent);
	struct resource *res;
	void __iomem *mem;
	int error;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mem = devm_ioremap_resource(dev, res);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	error = mx25_tcq_parse_dt(pdev, priv);
	if (error)
		return error;

	priv->regs = devm_regmap_init_mmio(dev, mem, &mx25_tcq_regconfig);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return PTR_ERR(priv->regs);
	}

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq <= 0) {
		dev_err(dev, "Failed to get IRQ\n");
		return priv->irq;
	}

	idev = devm_input_allocate_device(dev);
	if (!idev) {
		dev_err(dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	idev->name = mx25_tcq_name;
	input_set_capability(idev, EV_KEY, BTN_TOUCH);
	input_set_abs_params(idev, ABS_X, 0, 0xfff, 0, 0);
	input_set_abs_params(idev, ABS_Y, 0, 0xfff, 0, 0);

	idev->id.bustype = BUS_HOST;
	idev->open = mx25_tcq_open;
	idev->close = mx25_tcq_close;

	priv->idev = idev;
	input_set_drvdata(idev, priv);

	priv->core_regs = tsadc->regs;
	if (!priv->core_regs)
		return -EINVAL;

	priv->clk = tsadc->clk;
	if (!priv->clk)
		return -EINVAL;

	platform_set_drvdata(pdev, priv);

	error = devm_request_threaded_irq(dev, priv->irq, mx25_tcq_irq,
					  mx25_tcq_irq_thread, 0, pdev->name,
					  priv);
	if (error) {
		dev_err(dev, "Failed requesting IRQ\n");
		return error;
	}

	error = input_register_device(idev);
	if (error) {
		dev_err(dev, "Failed to register input device\n");
		return error;
	}

	return 0;
}

static struct platform_driver mx25_tcq_driver = {
	.driver		= {
		.name	= "mx25-tcq",
		.of_match_table = mx25_tcq_ids,
	},
	.probe		= mx25_tcq_probe,
};
module_platform_driver(mx25_tcq_driver);

MODULE_DESCRIPTION("TS input driver for Freescale mx25");
MODULE_AUTHOR("Markus Pargmann <mpa@pengutronix.de>");
MODULE_LICENSE("GPL v2");
