/*
 * Driver for Mediatek IR Receiver Controller
 *
 * Copyright (C) 2017 Sean Wang <sean.wang@mediatek.com>
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

#define MTK_IR_DEV KBUILD_MODNAME

/* Register to enable PWM and IR */
#define MTK_CONFIG_HIGH_REG       0x0c
/* Enable IR pulse width detection */
#define MTK_PWM_EN		  BIT(13)
/* Enable IR hardware function */
#define MTK_IR_EN		  BIT(0)

/* Register to setting sample period */
#define MTK_CONFIG_LOW_REG        0x10
/* Field to set sample period */
#define CHK_PERIOD		  DIV_ROUND_CLOSEST(MTK_IR_SAMPLE,  \
						    MTK_IR_CLK_PERIOD)
#define MTK_CHK_PERIOD            (((CHK_PERIOD) << 8) & (GENMASK(20, 8)))
#define MTK_CHK_PERIOD_MASK	  (GENMASK(20, 8))

/* Register to clear state of state machine */
#define MTK_IRCLR_REG             0x20
/* Bit to restart IR receiving */
#define MTK_IRCLR		  BIT(0)

/* Register containing pulse width data */
#define MTK_CHKDATA_REG(i)        (0x88 + 4 * (i))
#define MTK_WIDTH_MASK		  (GENMASK(7, 0))

/* Register to enable IR interrupt */
#define MTK_IRINT_EN_REG          0xcc
/* Bit to enable interrupt */
#define MTK_IRINT_EN		  BIT(0)

/* Register to ack IR interrupt */
#define MTK_IRINT_CLR_REG         0xd0
/* Bit to clear interrupt status */
#define MTK_IRINT_CLR		  BIT(0)

/* Maximum count of samples */
#define MTK_MAX_SAMPLES		  0xff
/* Indicate the end of IR message */
#define MTK_IR_END(v, p)	  ((v) == MTK_MAX_SAMPLES && (p) == 0)
/* Number of registers to record the pulse width */
#define MTK_CHKDATA_SZ		  17
/* Source clock frequency */
#define MTK_IR_BASE_CLK		  273000000
/* Frequency after IR internal divider */
#define MTK_IR_CLK_FREQ		  (MTK_IR_BASE_CLK / 4)
/* Period for MTK_IR_CLK in ns*/
#define MTK_IR_CLK_PERIOD	  DIV_ROUND_CLOSEST(1000000000ul,  \
						    MTK_IR_CLK_FREQ)
/* Sample period in ns */
#define MTK_IR_SAMPLE		  (MTK_IR_CLK_PERIOD * 0xc00)

/*
 * struct mtk_ir -	This is the main datasructure for holding the state
 *			of the driver
 * @dev:		The device pointer
 * @rc:			The rc instrance
 * @irq:		The IRQ that we are using
 * @base:		The mapped register i/o base
 * @clk:		The clock that we are using
 */
struct mtk_ir {
	struct device	*dev;
	struct rc_dev	*rc;
	void __iomem	*base;
	int		irq;
	struct clk	*clk;
};

static void mtk_w32_mask(struct mtk_ir *ir, u32 val, u32 mask, unsigned int reg)
{
	u32 tmp;

	tmp = __raw_readl(ir->base + reg);
	tmp = (tmp & ~mask) | val;
	__raw_writel(tmp, ir->base + reg);
}

static void mtk_w32(struct mtk_ir *ir, u32 val, unsigned int reg)
{
	__raw_writel(val, ir->base + reg);
}

static u32 mtk_r32(struct mtk_ir *ir, unsigned int reg)
{
	return __raw_readl(ir->base + reg);
}

static inline void mtk_irq_disable(struct mtk_ir *ir, u32 mask)
{
	u32 val;

	val = mtk_r32(ir, MTK_IRINT_EN_REG);
	mtk_w32(ir, val & ~mask, MTK_IRINT_EN_REG);
}

static inline void mtk_irq_enable(struct mtk_ir *ir, u32 mask)
{
	u32 val;

	val = mtk_r32(ir, MTK_IRINT_EN_REG);
	mtk_w32(ir, val | mask, MTK_IRINT_EN_REG);
}

static irqreturn_t mtk_ir_irq(int irqno, void *dev_id)
{
	struct mtk_ir *ir = dev_id;
	u8  wid = 0;
	u32 i, j, val;
	DEFINE_IR_RAW_EVENT(rawir);

	/*
	 * Reset decoder state machine explicitly is required
	 * because 1) the longest duration for space MTK IR hardware
	 * could record is not safely long. e.g  12ms if rx resolution
	 * is 46us by default. There is still the risk to satisfying
	 * every decoder to reset themselves through long enough
	 * trailing spaces and 2) the IRQ handler guarantees that
	 * start of IR message is always contained in and starting
	 * from register MTK_CHKDATA_REG(0).
	 */
	ir_raw_event_reset(ir->rc);

	/* First message must be pulse */
	rawir.pulse = false;

	/* Handle all pulse and space IR controller captures */
	for (i = 0 ; i < MTK_CHKDATA_SZ ; i++) {
		val = mtk_r32(ir, MTK_CHKDATA_REG(i));
		dev_dbg(ir->dev, "@reg%d=0x%08x\n", i, val);

		for (j = 0 ; j < 4 ; j++) {
			wid = (val & (MTK_WIDTH_MASK << j * 8)) >> j * 8;
			rawir.pulse = !rawir.pulse;
			rawir.duration = wid * (MTK_IR_SAMPLE + 1);
			ir_raw_event_store_with_filter(ir->rc, &rawir);
		}
	}

	/*
	 * The maximum number of edges the IR controller can
	 * hold is MTK_CHKDATA_SZ * 4. So if received IR messages
	 * is over the limit, the last incomplete IR message would
	 * be appended trailing space and still would be sent into
	 * ir-rc-raw to decode. That helps it is possible that it
	 * has enough information to decode a scancode even if the
	 * trailing end of the message is missing.
	 */
	if (!MTK_IR_END(wid, rawir.pulse)) {
		rawir.pulse = false;
		rawir.duration = MTK_MAX_SAMPLES * (MTK_IR_SAMPLE + 1);
		ir_raw_event_store_with_filter(ir->rc, &rawir);
	}

	ir_raw_event_handle(ir->rc);

	/*
	 * Restart controller for the next receive that would
	 * clear up all CHKDATA registers
	 */
	mtk_w32_mask(ir, 0x1, MTK_IRCLR, MTK_IRCLR_REG);

	/* Clear interrupt status */
	mtk_w32_mask(ir, 0x1, MTK_IRINT_CLR, MTK_IRINT_CLR_REG);

	return IRQ_HANDLED;
}

static int mtk_ir_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dn = dev->of_node;
	struct resource *res;
	struct mtk_ir *ir;
	u32 val;
	int ret = 0;
	const char *map_name;

	ir = devm_kzalloc(dev, sizeof(struct mtk_ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	ir->dev = dev;

	if (!of_device_is_compatible(dn, "mediatek,mt7623-cir"))
		return -ENODEV;

	ir->clk = devm_clk_get(dev, "clk");
	if (IS_ERR(ir->clk)) {
		dev_err(dev, "failed to get a ir clock.\n");
		return PTR_ERR(ir->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ir->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ir->base)) {
		dev_err(dev, "failed to map registers\n");
		return PTR_ERR(ir->base);
	}

	ir->rc = devm_rc_allocate_device(dev, RC_DRIVER_IR_RAW);
	if (!ir->rc) {
		dev_err(dev, "failed to allocate device\n");
		return -ENOMEM;
	}

	ir->rc->priv = ir;
	ir->rc->input_name = MTK_IR_DEV;
	ir->rc->input_phys = MTK_IR_DEV "/input0";
	ir->rc->input_id.bustype = BUS_HOST;
	ir->rc->input_id.vendor = 0x0001;
	ir->rc->input_id.product = 0x0001;
	ir->rc->input_id.version = 0x0001;
	map_name = of_get_property(dn, "linux,rc-map-name", NULL);
	ir->rc->map_name = map_name ?: RC_MAP_EMPTY;
	ir->rc->dev.parent = dev;
	ir->rc->driver_name = MTK_IR_DEV;
	ir->rc->allowed_protocols = RC_BIT_ALL;
	ir->rc->rx_resolution = MTK_IR_SAMPLE;
	ir->rc->timeout = MTK_MAX_SAMPLES * (MTK_IR_SAMPLE + 1);

	ret = devm_rc_register_device(dev, ir->rc);
	if (ret) {
		dev_err(dev, "failed to register rc device\n");
		return ret;
	}

	platform_set_drvdata(pdev, ir);

	ir->irq = platform_get_irq(pdev, 0);
	if (ir->irq < 0) {
		dev_err(dev, "no irq resource\n");
		return -ENODEV;
	}

	/*
	 * Enable interrupt after proper hardware
	 * setup and IRQ handler registration
	 */
	if (clk_prepare_enable(ir->clk)) {
		dev_err(dev, "try to enable ir_clk failed\n");
		ret = -EINVAL;
		goto exit_clkdisable_clk;
	}

	mtk_irq_disable(ir, MTK_IRINT_EN);

	ret = devm_request_irq(dev, ir->irq, mtk_ir_irq, 0, MTK_IR_DEV, ir);
	if (ret) {
		dev_err(dev, "failed request irq\n");
		goto exit_clkdisable_clk;
	}

	/* Enable IR and PWM */
	val = mtk_r32(ir, MTK_CONFIG_HIGH_REG);
	val |= MTK_PWM_EN | MTK_IR_EN;
	mtk_w32(ir, val, MTK_CONFIG_HIGH_REG);

	/* Setting sample period */
	mtk_w32_mask(ir, MTK_CHK_PERIOD, MTK_CHK_PERIOD_MASK,
		     MTK_CONFIG_LOW_REG);

	mtk_irq_enable(ir, MTK_IRINT_EN);

	dev_info(dev, "Initialized MT7623 IR driver, sample period = %luus\n",
		 DIV_ROUND_CLOSEST(MTK_IR_SAMPLE, 1000));

	return 0;

exit_clkdisable_clk:
	clk_disable_unprepare(ir->clk);

	return ret;
}

static int mtk_ir_remove(struct platform_device *pdev)
{
	struct mtk_ir *ir = platform_get_drvdata(pdev);

	/*
	 * Avoid contention between remove handler and
	 * IRQ handler so that disabling IR interrupt and
	 * waiting for pending IRQ handler to complete
	 */
	mtk_irq_disable(ir, MTK_IRINT_EN);
	synchronize_irq(ir->irq);

	clk_disable_unprepare(ir->clk);

	return 0;
}

static const struct of_device_id mtk_ir_match[] = {
	{ .compatible = "mediatek,mt7623-cir" },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_ir_match);

static struct platform_driver mtk_ir_driver = {
	.probe          = mtk_ir_probe,
	.remove         = mtk_ir_remove,
	.driver = {
		.name = MTK_IR_DEV,
		.of_match_table = mtk_ir_match,
	},
};

module_platform_driver(mtk_ir_driver);

MODULE_DESCRIPTION("Mediatek IR Receiver Controller Driver");
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_LICENSE("GPL");
