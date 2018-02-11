/*
 * Copyright (C) 2017 Sanechips Technology Co., Ltd.
 * Copyright 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <media/rc-core.h>

#define DRIVER_NAME		"zx-irdec"

#define ZX_IR_ENABLE		0x04
#define ZX_IREN			BIT(0)
#define ZX_IR_CTRL		0x08
#define ZX_DEGL_MASK		GENMASK(21, 20)
#define ZX_DEGL_VALUE(x)	(((x) << 20) & ZX_DEGL_MASK)
#define ZX_WDBEGIN_MASK		GENMASK(18, 8)
#define ZX_WDBEGIN_VALUE(x)	(((x) << 8) & ZX_WDBEGIN_MASK)
#define ZX_IR_INTEN		0x10
#define ZX_IR_INTSTCLR		0x14
#define ZX_IR_CODE		0x30
#define ZX_IR_CNUM		0x34
#define ZX_NECRPT		BIT(16)

struct zx_irdec {
	void __iomem *base;
	struct rc_dev *rcd;
};

static void zx_irdec_set_mask(struct zx_irdec *irdec, unsigned int reg,
			      u32 mask, u32 value)
{
	u32 data;

	data = readl(irdec->base + reg);
	data &= ~mask;
	data |= value & mask;
	writel(data, irdec->base + reg);
}

static irqreturn_t zx_irdec_irq(int irq, void *dev_id)
{
	struct zx_irdec *irdec = dev_id;
	u8 address, not_address;
	u8 command, not_command;
	u32 rawcode, scancode;
	enum rc_proto rc_proto;

	/* Clear interrupt */
	writel(1, irdec->base + ZX_IR_INTSTCLR);

	/* Check repeat frame */
	if (readl(irdec->base + ZX_IR_CNUM) & ZX_NECRPT) {
		rc_repeat(irdec->rcd);
		goto done;
	}

	rawcode = readl(irdec->base + ZX_IR_CODE);
	not_command = (rawcode >> 24) & 0xff;
	command = (rawcode >> 16) & 0xff;
	not_address = (rawcode >> 8) & 0xff;
	address = rawcode & 0xff;

	scancode = ir_nec_bytes_to_scancode(address, not_address,
					    command, not_command,
					    &rc_proto);
	rc_keydown(irdec->rcd, rc_proto, scancode, 0);

done:
	return IRQ_HANDLED;
}

static int zx_irdec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zx_irdec *irdec;
	struct resource *res;
	struct rc_dev *rcd;
	int irq;
	int ret;

	irdec = devm_kzalloc(dev, sizeof(*irdec), GFP_KERNEL);
	if (!irdec)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irdec->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(irdec->base))
		return PTR_ERR(irdec->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	rcd = devm_rc_allocate_device(dev, RC_DRIVER_SCANCODE);
	if (!rcd) {
		dev_err(dev, "failed to allocate rc device\n");
		return -ENOMEM;
	}

	irdec->rcd = rcd;

	rcd->priv = irdec;
	rcd->input_phys = DRIVER_NAME "/input0";
	rcd->input_id.bustype = BUS_HOST;
	rcd->map_name = RC_MAP_ZX_IRDEC;
	rcd->allowed_protocols = RC_PROTO_BIT_NEC | RC_PROTO_BIT_NECX |
							RC_PROTO_BIT_NEC32;
	rcd->driver_name = DRIVER_NAME;
	rcd->device_name = DRIVER_NAME;

	platform_set_drvdata(pdev, irdec);

	ret = devm_rc_register_device(dev, rcd);
	if (ret) {
		dev_err(dev, "failed to register rc device\n");
		return ret;
	}

	ret = devm_request_irq(dev, irq, zx_irdec_irq, 0, NULL, irdec);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	/*
	 * Initialize deglitch level and watchdog counter beginner as
	 * recommended by vendor BSP code.
	 */
	zx_irdec_set_mask(irdec, ZX_IR_CTRL, ZX_DEGL_MASK, ZX_DEGL_VALUE(0));
	zx_irdec_set_mask(irdec, ZX_IR_CTRL, ZX_WDBEGIN_MASK,
			  ZX_WDBEGIN_VALUE(0x21c));

	/* Enable interrupt */
	writel(1, irdec->base + ZX_IR_INTEN);

	/* Enable the decoder */
	zx_irdec_set_mask(irdec, ZX_IR_ENABLE, ZX_IREN, ZX_IREN);

	return 0;
}

static int zx_irdec_remove(struct platform_device *pdev)
{
	struct zx_irdec *irdec = platform_get_drvdata(pdev);

	/* Disable the decoder */
	zx_irdec_set_mask(irdec, ZX_IR_ENABLE, ZX_IREN, 0);

	/* Disable interrupt */
	writel(0, irdec->base + ZX_IR_INTEN);

	return 0;
}

static const struct of_device_id zx_irdec_match[] = {
	{ .compatible = "zte,zx296718-irdec" },
	{ },
};
MODULE_DEVICE_TABLE(of, zx_irdec_match);

static struct platform_driver zx_irdec_driver = {
	.probe = zx_irdec_probe,
	.remove = zx_irdec_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table	= zx_irdec_match,
	},
};
module_platform_driver(zx_irdec_driver);

MODULE_DESCRIPTION("ZTE ZX IR remote control driver");
MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_LICENSE("GPL v2");
