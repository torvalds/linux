// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Sean Young <sean@mess.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <media/rc-core.h>

#define DRIVER_NAME	"gpio-ir-tx"
#define DEVICE_NAME	"GPIO IR Bit Banging Transmitter"

struct gpio_ir {
	struct gpio_desc *gpio;
	unsigned int carrier;
	unsigned int duty_cycle;
};

static const struct of_device_id gpio_ir_tx_of_match[] = {
	{ .compatible = "gpio-ir-tx", },
	{ },
};
MODULE_DEVICE_TABLE(of, gpio_ir_tx_of_match);

static int gpio_ir_tx_set_duty_cycle(struct rc_dev *dev, u32 duty_cycle)
{
	struct gpio_ir *gpio_ir = dev->priv;

	gpio_ir->duty_cycle = duty_cycle;

	return 0;
}

static int gpio_ir_tx_set_carrier(struct rc_dev *dev, u32 carrier)
{
	struct gpio_ir *gpio_ir = dev->priv;

	if (carrier > 500000)
		return -EINVAL;

	gpio_ir->carrier = carrier;

	return 0;
}

static void delay_until(ktime_t until)
{
	/*
	 * delta should never exceed 0.5 seconds (IR_MAX_DURATION) and on
	 * m68k ndelay(s64) does not compile; so use s32 rather than s64.
	 */
	s32 delta;

	while (true) {
		delta = ktime_us_delta(until, ktime_get());
		if (delta <= 0)
			return;

		/* udelay more than 1ms may not work */
		if (delta >= 1000) {
			mdelay(delta / 1000);
			continue;
		}

		udelay(delta);
		break;
	}
}

static void gpio_ir_tx_unmodulated(struct gpio_ir *gpio_ir, uint *txbuf,
				   uint count)
{
	ktime_t edge;
	int i;

	local_irq_disable();

	edge = ktime_get();

	for (i = 0; i < count; i++) {
		gpiod_set_value(gpio_ir->gpio, !(i % 2));

		edge = ktime_add_us(edge, txbuf[i]);
		delay_until(edge);
	}

	gpiod_set_value(gpio_ir->gpio, 0);
}

static void gpio_ir_tx_modulated(struct gpio_ir *gpio_ir, uint *txbuf,
				 uint count)
{
	ktime_t edge;
	/*
	 * delta should never exceed 0.5 seconds (IR_MAX_DURATION) and on
	 * m68k ndelay(s64) does not compile; so use s32 rather than s64.
	 */
	s32 delta;
	int i;
	unsigned int pulse, space;

	/* Ensure the dividend fits into 32 bit */
	pulse = DIV_ROUND_CLOSEST(gpio_ir->duty_cycle * (NSEC_PER_SEC / 100),
				  gpio_ir->carrier);
	space = DIV_ROUND_CLOSEST((100 - gpio_ir->duty_cycle) *
				  (NSEC_PER_SEC / 100), gpio_ir->carrier);

	local_irq_disable();

	edge = ktime_get();

	for (i = 0; i < count; i++) {
		if (i % 2) {
			// space
			edge = ktime_add_us(edge, txbuf[i]);
			delay_until(edge);
		} else {
			// pulse
			ktime_t last = ktime_add_us(edge, txbuf[i]);

			while (ktime_before(ktime_get(), last)) {
				gpiod_set_value(gpio_ir->gpio, 1);
				edge = ktime_add_ns(edge, pulse);
				delta = ktime_to_ns(ktime_sub(edge,
							      ktime_get()));
				if (delta > 0)
					ndelay(delta);
				gpiod_set_value(gpio_ir->gpio, 0);
				edge = ktime_add_ns(edge, space);
				delta = ktime_to_ns(ktime_sub(edge,
							      ktime_get()));
				if (delta > 0)
					ndelay(delta);
			}

			edge = last;
		}
	}
}

static int gpio_ir_tx(struct rc_dev *dev, unsigned int *txbuf,
		      unsigned int count)
{
	struct gpio_ir *gpio_ir = dev->priv;
	unsigned long flags;

	local_irq_save(flags);
	if (gpio_ir->carrier)
		gpio_ir_tx_modulated(gpio_ir, txbuf, count);
	else
		gpio_ir_tx_unmodulated(gpio_ir, txbuf, count);
	local_irq_restore(flags);

	return count;
}

static int gpio_ir_tx_probe(struct platform_device *pdev)
{
	struct gpio_ir *gpio_ir;
	struct rc_dev *rcdev;
	int rc;

	gpio_ir = devm_kmalloc(&pdev->dev, sizeof(*gpio_ir), GFP_KERNEL);
	if (!gpio_ir)
		return -ENOMEM;

	rcdev = devm_rc_allocate_device(&pdev->dev, RC_DRIVER_IR_RAW_TX);
	if (!rcdev)
		return -ENOMEM;

	gpio_ir->gpio = devm_gpiod_get(&pdev->dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(gpio_ir->gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(gpio_ir->gpio),
				     "Failed to get gpio\n");

	rcdev->priv = gpio_ir;
	rcdev->driver_name = DRIVER_NAME;
	rcdev->device_name = DEVICE_NAME;
	rcdev->tx_ir = gpio_ir_tx;
	rcdev->s_tx_duty_cycle = gpio_ir_tx_set_duty_cycle;
	rcdev->s_tx_carrier = gpio_ir_tx_set_carrier;

	gpio_ir->carrier = 38000;
	gpio_ir->duty_cycle = 50;

	rc = devm_rc_register_device(&pdev->dev, rcdev);
	if (rc < 0)
		dev_err(&pdev->dev, "failed to register rc device\n");

	return rc;
}

static struct platform_driver gpio_ir_tx_driver = {
	.probe	= gpio_ir_tx_probe,
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = gpio_ir_tx_of_match,
	},
};
module_platform_driver(gpio_ir_tx_driver);

MODULE_DESCRIPTION("GPIO IR Bit Banging Transmitter");
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_LICENSE("GPL");
