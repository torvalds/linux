/*
 * Driver for Allwinner sunXi IR controller
 *
 * Copyright (C) 2014 Alexsey Shestacov <wingrime@linux-sunxi.org>
 * Copyright (C) 2014 Alexander Bersenev <bay@hackerdom.ru>
 *
 * Based on sun5i-ir.c:
 * Copyright (C) 2007-2012 Daniel Wang
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/reset.h>
#include <media/rc-core.h>

#define SUNXI_IR_DEV "sunxi-ir"

/* Registers */
/* IR Control */
#define SUNXI_IR_CTL_REG      0x00
/* Global Enable */
#define REG_CTL_GEN			BIT(0)
/* RX block enable */
#define REG_CTL_RXEN			BIT(1)
/* CIR mode */
#define REG_CTL_MD			(BIT(4) | BIT(5))

/* Rx Config */
#define SUNXI_IR_RXCTL_REG    0x10
/* Pulse Polarity Invert flag */
#define REG_RXCTL_RPPI			BIT(2)

/* Rx Data */
#define SUNXI_IR_RXFIFO_REG   0x20

/* Rx Interrupt Enable */
#define SUNXI_IR_RXINT_REG    0x2C
/* Rx FIFO Overflow */
#define REG_RXINT_ROI_EN		BIT(0)
/* Rx Packet End */
#define REG_RXINT_RPEI_EN		BIT(1)
/* Rx FIFO Data Available */
#define REG_RXINT_RAI_EN		BIT(4)

/* Rx FIFO available byte level */
#define REG_RXINT_RAL(val)    ((val) << 8)

/* Rx Interrupt Status */
#define SUNXI_IR_RXSTA_REG    0x30
/* RX FIFO Get Available Counter */
#define REG_RXSTA_GET_AC(val) (((val) >> 8) & (ir->fifo_size * 2 - 1))
/* Clear all interrupt status value */
#define REG_RXSTA_CLEARALL    0xff

/* IR Sample Config */
#define SUNXI_IR_CIR_REG      0x34
/* CIR_REG register noise threshold */
#define REG_CIR_NTHR(val)    (((val) << 2) & (GENMASK(7, 2)))
/* CIR_REG register idle threshold */
#define REG_CIR_ITHR(val)    (((val) << 8) & (GENMASK(15, 8)))

/* Required frequency for IR0 or IR1 clock in CIR mode (default) */
#define SUNXI_IR_BASE_CLK     8000000
/* Noise threshold in samples  */
#define SUNXI_IR_RXNOISE      1
/* Idle Threshold in samples */
#define SUNXI_IR_RXIDLE       20
/* Time after which device stops sending data in ms */
#define SUNXI_IR_TIMEOUT      120

struct sunxi_ir {
	spinlock_t      ir_lock;
	struct rc_dev   *rc;
	void __iomem    *base;
	int             irq;
	int		fifo_size;
	struct clk      *clk;
	struct clk      *apb_clk;
	struct reset_control *rst;
	const char      *map_name;
};

static irqreturn_t sunxi_ir_irq(int irqno, void *dev_id)
{
	unsigned long status;
	unsigned char dt;
	unsigned int cnt, rc;
	struct sunxi_ir *ir = dev_id;
	DEFINE_IR_RAW_EVENT(rawir);

	spin_lock(&ir->ir_lock);

	status = readl(ir->base + SUNXI_IR_RXSTA_REG);

	/* clean all pending statuses */
	writel(status | REG_RXSTA_CLEARALL, ir->base + SUNXI_IR_RXSTA_REG);

	if (status & (REG_RXINT_RAI_EN | REG_RXINT_RPEI_EN)) {
		/* How many messages in fifo */
		rc  = REG_RXSTA_GET_AC(status);
		/* Sanity check */
		rc = rc > ir->fifo_size ? ir->fifo_size : rc;
		/* If we have data */
		for (cnt = 0; cnt < rc; cnt++) {
			/* for each bit in fifo */
			dt = readb(ir->base + SUNXI_IR_RXFIFO_REG);
			rawir.pulse = (dt & 0x80) != 0;
			rawir.duration = ((dt & 0x7f) + 1) *
					 ir->rc->rx_resolution;
			ir_raw_event_store_with_filter(ir->rc, &rawir);
		}
	}

	if (status & REG_RXINT_ROI_EN) {
		ir_raw_event_reset(ir->rc);
	} else if (status & REG_RXINT_RPEI_EN) {
		ir_raw_event_set_idle(ir->rc, true);
		ir_raw_event_handle(ir->rc);
	} else {
		ir_raw_event_handle(ir->rc);
	}

	spin_unlock(&ir->ir_lock);

	return IRQ_HANDLED;
}

static int sunxi_ir_probe(struct platform_device *pdev)
{
	int ret = 0;
	unsigned long tmp = 0;

	struct device *dev = &pdev->dev;
	struct device_node *dn = dev->of_node;
	struct resource *res;
	struct sunxi_ir *ir;
	u32 b_clk_freq = SUNXI_IR_BASE_CLK;

	ir = devm_kzalloc(dev, sizeof(struct sunxi_ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	spin_lock_init(&ir->ir_lock);

	if (of_device_is_compatible(dn, "allwinner,sun5i-a13-ir"))
		ir->fifo_size = 64;
	else
		ir->fifo_size = 16;

	/* Clock */
	ir->apb_clk = devm_clk_get(dev, "apb");
	if (IS_ERR(ir->apb_clk)) {
		dev_err(dev, "failed to get a apb clock.\n");
		return PTR_ERR(ir->apb_clk);
	}
	ir->clk = devm_clk_get(dev, "ir");
	if (IS_ERR(ir->clk)) {
		dev_err(dev, "failed to get a ir clock.\n");
		return PTR_ERR(ir->clk);
	}

	/* Base clock frequency (optional) */
	of_property_read_u32(dn, "clock-frequency", &b_clk_freq);

	/* Reset (optional) */
	ir->rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(ir->rst))
		return PTR_ERR(ir->rst);
	ret = reset_control_deassert(ir->rst);
	if (ret)
		return ret;

	ret = clk_set_rate(ir->clk, b_clk_freq);
	if (ret) {
		dev_err(dev, "set ir base clock failed!\n");
		goto exit_reset_assert;
	}
	dev_dbg(dev, "set base clock frequency to %d Hz.\n", b_clk_freq);

	if (clk_prepare_enable(ir->apb_clk)) {
		dev_err(dev, "try to enable apb_ir_clk failed\n");
		ret = -EINVAL;
		goto exit_reset_assert;
	}

	if (clk_prepare_enable(ir->clk)) {
		dev_err(dev, "try to enable ir_clk failed\n");
		ret = -EINVAL;
		goto exit_clkdisable_apb_clk;
	}

	/* IO */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ir->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ir->base)) {
		dev_err(dev, "failed to map registers\n");
		ret = PTR_ERR(ir->base);
		goto exit_clkdisable_clk;
	}

	ir->rc = rc_allocate_device(RC_DRIVER_IR_RAW);
	if (!ir->rc) {
		dev_err(dev, "failed to allocate device\n");
		ret = -ENOMEM;
		goto exit_clkdisable_clk;
	}

	ir->rc->priv = ir;
	ir->rc->device_name = SUNXI_IR_DEV;
	ir->rc->input_phys = "sunxi-ir/input0";
	ir->rc->input_id.bustype = BUS_HOST;
	ir->rc->input_id.vendor = 0x0001;
	ir->rc->input_id.product = 0x0001;
	ir->rc->input_id.version = 0x0100;
	ir->map_name = of_get_property(dn, "linux,rc-map-name", NULL);
	ir->rc->map_name = ir->map_name ?: RC_MAP_EMPTY;
	ir->rc->dev.parent = dev;
	ir->rc->allowed_protocols = RC_PROTO_BIT_ALL_IR_DECODER;
	/* Frequency after IR internal divider with sample period in ns */
	ir->rc->rx_resolution = (1000000000ul / (b_clk_freq / 64));
	ir->rc->timeout = MS_TO_NS(SUNXI_IR_TIMEOUT);
	ir->rc->driver_name = SUNXI_IR_DEV;

	ret = rc_register_device(ir->rc);
	if (ret) {
		dev_err(dev, "failed to register rc device\n");
		goto exit_free_dev;
	}

	platform_set_drvdata(pdev, ir);

	/* IRQ */
	ir->irq = platform_get_irq(pdev, 0);
	if (ir->irq < 0) {
		dev_err(dev, "no irq resource\n");
		ret = ir->irq;
		goto exit_free_dev;
	}

	ret = devm_request_irq(dev, ir->irq, sunxi_ir_irq, 0, SUNXI_IR_DEV, ir);
	if (ret) {
		dev_err(dev, "failed request irq\n");
		goto exit_free_dev;
	}

	/* Enable CIR Mode */
	writel(REG_CTL_MD, ir->base+SUNXI_IR_CTL_REG);

	/* Set noise threshold and idle threshold */
	writel(REG_CIR_NTHR(SUNXI_IR_RXNOISE)|REG_CIR_ITHR(SUNXI_IR_RXIDLE),
	       ir->base + SUNXI_IR_CIR_REG);

	/* Invert Input Signal */
	writel(REG_RXCTL_RPPI, ir->base + SUNXI_IR_RXCTL_REG);

	/* Clear All Rx Interrupt Status */
	writel(REG_RXSTA_CLEARALL, ir->base + SUNXI_IR_RXSTA_REG);

	/*
	 * Enable IRQ on overflow, packet end, FIFO available with trigger
	 * level
	 */
	writel(REG_RXINT_ROI_EN | REG_RXINT_RPEI_EN |
	       REG_RXINT_RAI_EN | REG_RXINT_RAL(ir->fifo_size / 2 - 1),
	       ir->base + SUNXI_IR_RXINT_REG);

	/* Enable IR Module */
	tmp = readl(ir->base + SUNXI_IR_CTL_REG);
	writel(tmp | REG_CTL_GEN | REG_CTL_RXEN, ir->base + SUNXI_IR_CTL_REG);

	dev_info(dev, "initialized sunXi IR driver\n");
	return 0;

exit_free_dev:
	rc_free_device(ir->rc);
exit_clkdisable_clk:
	clk_disable_unprepare(ir->clk);
exit_clkdisable_apb_clk:
	clk_disable_unprepare(ir->apb_clk);
exit_reset_assert:
	reset_control_assert(ir->rst);

	return ret;
}

static int sunxi_ir_remove(struct platform_device *pdev)
{
	unsigned long flags;
	struct sunxi_ir *ir = platform_get_drvdata(pdev);

	clk_disable_unprepare(ir->clk);
	clk_disable_unprepare(ir->apb_clk);
	reset_control_assert(ir->rst);

	spin_lock_irqsave(&ir->ir_lock, flags);
	/* disable IR IRQ */
	writel(0, ir->base + SUNXI_IR_RXINT_REG);
	/* clear All Rx Interrupt Status */
	writel(REG_RXSTA_CLEARALL, ir->base + SUNXI_IR_RXSTA_REG);
	/* disable IR */
	writel(0, ir->base + SUNXI_IR_CTL_REG);
	spin_unlock_irqrestore(&ir->ir_lock, flags);

	rc_unregister_device(ir->rc);
	return 0;
}

static const struct of_device_id sunxi_ir_match[] = {
	{ .compatible = "allwinner,sun4i-a10-ir", },
	{ .compatible = "allwinner,sun5i-a13-ir", },
	{},
};
MODULE_DEVICE_TABLE(of, sunxi_ir_match);

static struct platform_driver sunxi_ir_driver = {
	.probe          = sunxi_ir_probe,
	.remove         = sunxi_ir_remove,
	.driver = {
		.name = SUNXI_IR_DEV,
		.of_match_table = sunxi_ir_match,
	},
};

module_platform_driver(sunxi_ir_driver);

MODULE_DESCRIPTION("Allwinner sunXi IR controller driver");
MODULE_AUTHOR("Alexsey Shestacov <wingrime@linux-sunxi.org>");
MODULE_LICENSE("GPL");
