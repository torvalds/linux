/*
 * Driver for Amlogic Meson IR remote receiver
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/bitfield.h>

#include <media/rc-core.h>

#define DRIVER_NAME		"meson-ir"

/* valid on all Meson platforms */
#define IR_DEC_LDR_ACTIVE	0x00
#define IR_DEC_LDR_IDLE		0x04
#define IR_DEC_LDR_REPEAT	0x08
#define IR_DEC_BIT_0		0x0c
#define IR_DEC_REG0		0x10
#define IR_DEC_FRAME		0x14
#define IR_DEC_STATUS		0x18
#define IR_DEC_REG1		0x1c
/* only available on Meson 8b and newer */
#define IR_DEC_REG2		0x20

#define REG0_RATE_MASK		GENMASK(11, 0)

#define DECODE_MODE_NEC		0x0
#define DECODE_MODE_RAW		0x2

/* Meson 6b uses REG1 to configure the mode */
#define REG1_MODE_MASK		GENMASK(8, 7)
#define REG1_MODE_SHIFT		7

/* Meson 8b / GXBB use REG2 to configure the mode */
#define REG2_MODE_MASK		GENMASK(3, 0)
#define REG2_MODE_SHIFT		0

#define REG1_TIME_IV_MASK	GENMASK(28, 16)

#define REG1_IRQSEL_MASK	GENMASK(3, 2)
#define REG1_IRQSEL_NEC_MODE	0
#define REG1_IRQSEL_RISE_FALL	1
#define REG1_IRQSEL_FALL	2
#define REG1_IRQSEL_RISE	3

#define REG1_RESET		BIT(0)
#define REG1_ENABLE		BIT(15)

#define STATUS_IR_DEC_IN	BIT(8)

#define MESON_TRATE		10	/* us */

struct meson_ir {
	void __iomem	*reg;
	struct rc_dev	*rc;
	spinlock_t	lock;
};

static void meson_ir_set_mask(struct meson_ir *ir, unsigned int reg,
			      u32 mask, u32 value)
{
	u32 data;

	data = readl(ir->reg + reg);
	data &= ~mask;
	data |= (value & mask);
	writel(data, ir->reg + reg);
}

static irqreturn_t meson_ir_irq(int irqno, void *dev_id)
{
	struct meson_ir *ir = dev_id;
	u32 duration, status;
	DEFINE_IR_RAW_EVENT(rawir);

	spin_lock(&ir->lock);

	duration = readl_relaxed(ir->reg + IR_DEC_REG1);
	duration = FIELD_GET(REG1_TIME_IV_MASK, duration);
	rawir.duration = US_TO_NS(duration * MESON_TRATE);

	status = readl_relaxed(ir->reg + IR_DEC_STATUS);
	rawir.pulse = !!(status & STATUS_IR_DEC_IN);

	ir_raw_event_store_with_timeout(ir->rc, &rawir);

	spin_unlock(&ir->lock);

	return IRQ_HANDLED;
}

static int meson_ir_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct resource *res;
	const char *map_name;
	struct meson_ir *ir;
	int irq, ret;

	ir = devm_kzalloc(dev, sizeof(struct meson_ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ir->reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(ir->reg)) {
		dev_err(dev, "failed to map registers\n");
		return PTR_ERR(ir->reg);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq resource\n");
		return irq;
	}

	ir->rc = devm_rc_allocate_device(dev, RC_DRIVER_IR_RAW);
	if (!ir->rc) {
		dev_err(dev, "failed to allocate rc device\n");
		return -ENOMEM;
	}

	ir->rc->priv = ir;
	ir->rc->device_name = DRIVER_NAME;
	ir->rc->input_phys = DRIVER_NAME "/input0";
	ir->rc->input_id.bustype = BUS_HOST;
	map_name = of_get_property(node, "linux,rc-map-name", NULL);
	ir->rc->map_name = map_name ? map_name : RC_MAP_EMPTY;
	ir->rc->allowed_protocols = RC_PROTO_BIT_ALL_IR_DECODER;
	ir->rc->rx_resolution = US_TO_NS(MESON_TRATE);
	ir->rc->min_timeout = 1;
	ir->rc->timeout = IR_DEFAULT_TIMEOUT;
	ir->rc->max_timeout = 10 * IR_DEFAULT_TIMEOUT;
	ir->rc->driver_name = DRIVER_NAME;

	spin_lock_init(&ir->lock);
	platform_set_drvdata(pdev, ir);

	ret = devm_rc_register_device(dev, ir->rc);
	if (ret) {
		dev_err(dev, "failed to register rc device\n");
		return ret;
	}

	ret = devm_request_irq(dev, irq, meson_ir_irq, 0, NULL, ir);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	/* Reset the decoder */
	meson_ir_set_mask(ir, IR_DEC_REG1, REG1_RESET, REG1_RESET);
	meson_ir_set_mask(ir, IR_DEC_REG1, REG1_RESET, 0);

	/* Set general operation mode (= raw/software decoding) */
	if (of_device_is_compatible(node, "amlogic,meson6-ir"))
		meson_ir_set_mask(ir, IR_DEC_REG1, REG1_MODE_MASK,
				  FIELD_PREP(REG1_MODE_MASK, DECODE_MODE_RAW));
	else
		meson_ir_set_mask(ir, IR_DEC_REG2, REG2_MODE_MASK,
				  FIELD_PREP(REG2_MODE_MASK, DECODE_MODE_RAW));

	/* Set rate */
	meson_ir_set_mask(ir, IR_DEC_REG0, REG0_RATE_MASK, MESON_TRATE - 1);
	/* IRQ on rising and falling edges */
	meson_ir_set_mask(ir, IR_DEC_REG1, REG1_IRQSEL_MASK,
			  FIELD_PREP(REG1_IRQSEL_MASK, REG1_IRQSEL_RISE_FALL));
	/* Enable the decoder */
	meson_ir_set_mask(ir, IR_DEC_REG1, REG1_ENABLE, REG1_ENABLE);

	dev_info(dev, "receiver initialized\n");

	return 0;
}

static int meson_ir_remove(struct platform_device *pdev)
{
	struct meson_ir *ir = platform_get_drvdata(pdev);
	unsigned long flags;

	/* Disable the decoder */
	spin_lock_irqsave(&ir->lock, flags);
	meson_ir_set_mask(ir, IR_DEC_REG1, REG1_ENABLE, 0);
	spin_unlock_irqrestore(&ir->lock, flags);

	return 0;
}

static void meson_ir_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct meson_ir *ir = platform_get_drvdata(pdev);
	unsigned long flags;

	spin_lock_irqsave(&ir->lock, flags);

	/*
	 * Set operation mode to NEC/hardware decoding to give
	 * bootloader a chance to power the system back on
	 */
	if (of_device_is_compatible(node, "amlogic,meson6-ir"))
		meson_ir_set_mask(ir, IR_DEC_REG1, REG1_MODE_MASK,
				  DECODE_MODE_NEC << REG1_MODE_SHIFT);
	else
		meson_ir_set_mask(ir, IR_DEC_REG2, REG2_MODE_MASK,
				  DECODE_MODE_NEC << REG2_MODE_SHIFT);

	/* Set rate to default value */
	meson_ir_set_mask(ir, IR_DEC_REG0, REG0_RATE_MASK, 0x13);

	spin_unlock_irqrestore(&ir->lock, flags);
}

static const struct of_device_id meson_ir_match[] = {
	{ .compatible = "amlogic,meson6-ir" },
	{ .compatible = "amlogic,meson8b-ir" },
	{ .compatible = "amlogic,meson-gxbb-ir" },
	{ },
};
MODULE_DEVICE_TABLE(of, meson_ir_match);

static struct platform_driver meson_ir_driver = {
	.probe		= meson_ir_probe,
	.remove		= meson_ir_remove,
	.shutdown	= meson_ir_shutdown,
	.driver = {
		.name		= DRIVER_NAME,
		.of_match_table	= meson_ir_match,
	},
};

module_platform_driver(meson_ir_driver);

MODULE_DESCRIPTION("Amlogic Meson IR remote receiver driver");
MODULE_AUTHOR("Beniamino Galvani <b.galvani@gmail.com>");
MODULE_LICENSE("GPL v2");
