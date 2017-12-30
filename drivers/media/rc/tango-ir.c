/*
 * Copyright (C) 2015 Mans Rullgard <mans@mansr.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <media/rc-core.h>

#define DRIVER_NAME "tango-ir"

#define IR_NEC_CTRL	0x00
#define IR_NEC_DATA	0x04
#define IR_CTRL		0x08
#define IR_RC5_CLK_DIV	0x0c
#define IR_RC5_DATA	0x10
#define IR_INT		0x14

#define NEC_TIME_BASE	560
#define RC5_TIME_BASE	1778

#define RC6_CTRL	0x00
#define RC6_CLKDIV	0x04
#define RC6_DATA0	0x08
#define RC6_DATA1	0x0c
#define RC6_DATA2	0x10
#define RC6_DATA3	0x14
#define RC6_DATA4	0x18

#define RC6_CARRIER	36000
#define RC6_TIME_BASE	16

#define NEC_CAP(n)	((n) << 24)
#define GPIO_SEL(n)	((n) << 16)
#define DISABLE_NEC	(BIT(4) | BIT(8))
#define ENABLE_RC5	(BIT(0) | BIT(9))
#define ENABLE_RC6	(BIT(0) | BIT(7))
#define ACK_IR_INT	(BIT(0) | BIT(1))
#define ACK_RC6_INT	(BIT(31))

#define NEC_ANY (RC_PROTO_BIT_NEC | RC_PROTO_BIT_NECX | RC_PROTO_BIT_NEC32)

struct tango_ir {
	void __iomem *rc5_base;
	void __iomem *rc6_base;
	struct rc_dev *rc;
	struct clk *clk;
};

static void tango_ir_handle_nec(struct tango_ir *ir)
{
	u32 v, code;
	enum rc_proto proto;

	v = readl_relaxed(ir->rc5_base + IR_NEC_DATA);
	if (!v) {
		rc_repeat(ir->rc);
		return;
	}

	code = ir_nec_bytes_to_scancode(v, v >> 8, v >> 16, v >> 24, &proto);
	rc_keydown(ir->rc, proto, code, 0);
}

static void tango_ir_handle_rc5(struct tango_ir *ir)
{
	u32 data, field, toggle, addr, cmd, code;

	data = readl_relaxed(ir->rc5_base + IR_RC5_DATA);
	if (data & BIT(31))
		return;

	field = data >> 12 & 1;
	toggle = data >> 11 & 1;
	addr = data >> 6 & 0x1f;
	cmd = (data & 0x3f) | (field ^ 1) << 6;

	code = RC_SCANCODE_RC5(addr, cmd);
	rc_keydown(ir->rc, RC_PROTO_RC5, code, toggle);
}

static void tango_ir_handle_rc6(struct tango_ir *ir)
{
	u32 data0, data1, toggle, mode, addr, cmd, code;

	data0 = readl_relaxed(ir->rc6_base + RC6_DATA0);
	data1 = readl_relaxed(ir->rc6_base + RC6_DATA1);

	mode = data0 >> 1 & 7;
	if (mode != 0)
		return;

	toggle = data0 & 1;
	addr = data0 >> 16;
	cmd = data1;

	code = RC_SCANCODE_RC6_0(addr, cmd);
	rc_keydown(ir->rc, RC_PROTO_RC6_0, code, toggle);
}

static irqreturn_t tango_ir_irq(int irq, void *dev_id)
{
	struct tango_ir *ir = dev_id;
	unsigned int rc5_stat;
	unsigned int rc6_stat;

	rc5_stat = readl_relaxed(ir->rc5_base + IR_INT);
	writel_relaxed(rc5_stat, ir->rc5_base + IR_INT);

	rc6_stat = readl_relaxed(ir->rc6_base + RC6_CTRL);
	writel_relaxed(rc6_stat, ir->rc6_base + RC6_CTRL);

	if (!(rc5_stat & 3) && !(rc6_stat & BIT(31)))
		return IRQ_NONE;

	if (rc5_stat & BIT(0))
		tango_ir_handle_rc5(ir);

	if (rc5_stat & BIT(1))
		tango_ir_handle_nec(ir);

	if (rc6_stat & BIT(31))
		tango_ir_handle_rc6(ir);

	return IRQ_HANDLED;
}

static int tango_change_protocol(struct rc_dev *dev, u64 *rc_type)
{
	struct tango_ir *ir = dev->priv;
	u32 rc5_ctrl = DISABLE_NEC;
	u32 rc6_ctrl = 0;

	if (*rc_type & NEC_ANY)
		rc5_ctrl = 0;

	if (*rc_type & RC_PROTO_BIT_RC5)
		rc5_ctrl |= ENABLE_RC5;

	if (*rc_type & RC_PROTO_BIT_RC6_0)
		rc6_ctrl = ENABLE_RC6;

	writel_relaxed(rc5_ctrl, ir->rc5_base + IR_CTRL);
	writel_relaxed(rc6_ctrl, ir->rc6_base + RC6_CTRL);

	return 0;
}

static int tango_ir_probe(struct platform_device *pdev)
{
	const char *map_name = RC_MAP_TANGO;
	struct device *dev = &pdev->dev;
	struct rc_dev *rc;
	struct tango_ir *ir;
	struct resource *rc5_res;
	struct resource *rc6_res;
	u64 clkrate, clkdiv;
	int irq, err;
	u32 val;

	rc5_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!rc5_res)
		return -EINVAL;

	rc6_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!rc6_res)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -EINVAL;

	ir = devm_kzalloc(dev, sizeof(*ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	ir->rc5_base = devm_ioremap_resource(dev, rc5_res);
	if (IS_ERR(ir->rc5_base))
		return PTR_ERR(ir->rc5_base);

	ir->rc6_base = devm_ioremap_resource(dev, rc6_res);
	if (IS_ERR(ir->rc6_base))
		return PTR_ERR(ir->rc6_base);

	ir->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ir->clk))
		return PTR_ERR(ir->clk);

	rc = devm_rc_allocate_device(dev, RC_DRIVER_SCANCODE);
	if (!rc)
		return -ENOMEM;

	of_property_read_string(dev->of_node, "linux,rc-map-name", &map_name);

	rc->device_name = DRIVER_NAME;
	rc->driver_name = DRIVER_NAME;
	rc->input_phys = DRIVER_NAME "/input0";
	rc->map_name = map_name;
	rc->allowed_protocols = NEC_ANY | RC_PROTO_BIT_RC5 | RC_PROTO_BIT_RC6_0;
	rc->change_protocol = tango_change_protocol;
	rc->priv = ir;
	ir->rc = rc;

	err = clk_prepare_enable(ir->clk);
	if (err)
		return err;

	clkrate = clk_get_rate(ir->clk);

	clkdiv = clkrate * NEC_TIME_BASE;
	do_div(clkdiv, 1000000);

	val = NEC_CAP(31) | GPIO_SEL(12) | clkdiv;
	writel_relaxed(val, ir->rc5_base + IR_NEC_CTRL);

	clkdiv = clkrate * RC5_TIME_BASE;
	do_div(clkdiv, 1000000);

	writel_relaxed(DISABLE_NEC, ir->rc5_base + IR_CTRL);
	writel_relaxed(clkdiv, ir->rc5_base + IR_RC5_CLK_DIV);
	writel_relaxed(ACK_IR_INT, ir->rc5_base + IR_INT);

	clkdiv = clkrate * RC6_TIME_BASE;
	do_div(clkdiv, RC6_CARRIER);

	writel_relaxed(ACK_RC6_INT, ir->rc6_base + RC6_CTRL);
	writel_relaxed((clkdiv >> 2) << 18 | clkdiv, ir->rc6_base + RC6_CLKDIV);

	err = devm_request_irq(dev, irq, tango_ir_irq, IRQF_SHARED,
			       dev_name(dev), ir);
	if (err)
		goto err_clk;

	err = devm_rc_register_device(dev, rc);
	if (err)
		goto err_clk;

	platform_set_drvdata(pdev, ir);
	return 0;

err_clk:
	clk_disable_unprepare(ir->clk);
	return err;
}

static int tango_ir_remove(struct platform_device *pdev)
{
	struct tango_ir *ir = platform_get_drvdata(pdev);

	clk_disable_unprepare(ir->clk);
	return 0;
}

static const struct of_device_id tango_ir_dt_ids[] = {
	{ .compatible = "sigma,smp8642-ir" },
	{ }
};
MODULE_DEVICE_TABLE(of, tango_ir_dt_ids);

static struct platform_driver tango_ir_driver = {
	.probe	= tango_ir_probe,
	.remove	= tango_ir_remove,
	.driver	= {
		.name		= DRIVER_NAME,
		.of_match_table	= tango_ir_dt_ids,
	},
};
module_platform_driver(tango_ir_driver);

MODULE_DESCRIPTION("SMP86xx IR decoder driver");
MODULE_AUTHOR("Mans Rullgard <mans@mansr.com>");
MODULE_LICENSE("GPL");
