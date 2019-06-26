/*
 * Driver for the Cirrus EP93xx matrix keypad controller.
 *
 * Copyright (c) 2008 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on the pxa27x matrix keypad controller by Rodolfo Giometti.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOTE:
 *
 * The 3-key reset is triggered by pressing the 3 keys in
 * Row 0, Columns 2, 4, and 7 at the same time.  This action can
 * be disabled by setting the EP93XX_KEYPAD_DISABLE_3_KEY flag.
 *
 * Normal operation for the matrix does not autorepeat the key press.
 * This action can be enabled by setting the EP93XX_KEYPAD_AUTOREPEAT
 * flag.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/input/matrix_keypad.h>
#include <linux/slab.h>
#include <linux/soc/cirrus/ep93xx.h>
#include <linux/platform_data/keypad-ep93xx.h>

/*
 * Keypad Interface Register offsets
 */
#define KEY_INIT		0x00	/* Key Scan Initialization register */
#define KEY_DIAG		0x04	/* Key Scan Diagnostic register */
#define KEY_REG			0x08	/* Key Value Capture register */

/* Key Scan Initialization Register bit defines */
#define KEY_INIT_DBNC_MASK	(0x00ff0000)
#define KEY_INIT_DBNC_SHIFT	(16)
#define KEY_INIT_DIS3KY		(1<<15)
#define KEY_INIT_DIAG		(1<<14)
#define KEY_INIT_BACK		(1<<13)
#define KEY_INIT_T2		(1<<12)
#define KEY_INIT_PRSCL_MASK	(0x000003ff)
#define KEY_INIT_PRSCL_SHIFT	(0)

/* Key Scan Diagnostic Register bit defines */
#define KEY_DIAG_MASK		(0x0000003f)
#define KEY_DIAG_SHIFT		(0)

/* Key Value Capture Register bit defines */
#define KEY_REG_K		(1<<15)
#define KEY_REG_INT		(1<<14)
#define KEY_REG_2KEYS		(1<<13)
#define KEY_REG_1KEY		(1<<12)
#define KEY_REG_KEY2_MASK	(0x00000fc0)
#define KEY_REG_KEY2_SHIFT	(6)
#define KEY_REG_KEY1_MASK	(0x0000003f)
#define KEY_REG_KEY1_SHIFT	(0)

#define EP93XX_MATRIX_SIZE	(EP93XX_MATRIX_ROWS * EP93XX_MATRIX_COLS)

struct ep93xx_keypad {
	struct ep93xx_keypad_platform_data *pdata;
	struct input_dev *input_dev;
	struct clk *clk;

	void __iomem *mmio_base;

	unsigned short keycodes[EP93XX_MATRIX_SIZE];

	int key1;
	int key2;

	int irq;

	bool enabled;
};

static irqreturn_t ep93xx_keypad_irq_handler(int irq, void *dev_id)
{
	struct ep93xx_keypad *keypad = dev_id;
	struct input_dev *input_dev = keypad->input_dev;
	unsigned int status;
	int keycode, key1, key2;

	status = __raw_readl(keypad->mmio_base + KEY_REG);

	keycode = (status & KEY_REG_KEY1_MASK) >> KEY_REG_KEY1_SHIFT;
	key1 = keypad->keycodes[keycode];

	keycode = (status & KEY_REG_KEY2_MASK) >> KEY_REG_KEY2_SHIFT;
	key2 = keypad->keycodes[keycode];

	if (status & KEY_REG_2KEYS) {
		if (keypad->key1 && key1 != keypad->key1 && key2 != keypad->key1)
			input_report_key(input_dev, keypad->key1, 0);

		if (keypad->key2 && key1 != keypad->key2 && key2 != keypad->key2)
			input_report_key(input_dev, keypad->key2, 0);

		input_report_key(input_dev, key1, 1);
		input_report_key(input_dev, key2, 1);

		keypad->key1 = key1;
		keypad->key2 = key2;

	} else if (status & KEY_REG_1KEY) {
		if (keypad->key1 && key1 != keypad->key1)
			input_report_key(input_dev, keypad->key1, 0);

		if (keypad->key2 && key1 != keypad->key2)
			input_report_key(input_dev, keypad->key2, 0);

		input_report_key(input_dev, key1, 1);

		keypad->key1 = key1;
		keypad->key2 = 0;

	} else {
		input_report_key(input_dev, keypad->key1, 0);
		input_report_key(input_dev, keypad->key2, 0);

		keypad->key1 = keypad->key2 = 0;
	}
	input_sync(input_dev);

	return IRQ_HANDLED;
}

static void ep93xx_keypad_config(struct ep93xx_keypad *keypad)
{
	struct ep93xx_keypad_platform_data *pdata = keypad->pdata;
	unsigned int val = 0;

	clk_set_rate(keypad->clk, pdata->clk_rate);

	if (pdata->flags & EP93XX_KEYPAD_DISABLE_3_KEY)
		val |= KEY_INIT_DIS3KY;
	if (pdata->flags & EP93XX_KEYPAD_DIAG_MODE)
		val |= KEY_INIT_DIAG;
	if (pdata->flags & EP93XX_KEYPAD_BACK_DRIVE)
		val |= KEY_INIT_BACK;
	if (pdata->flags & EP93XX_KEYPAD_TEST_MODE)
		val |= KEY_INIT_T2;

	val |= ((pdata->debounce << KEY_INIT_DBNC_SHIFT) & KEY_INIT_DBNC_MASK);

	val |= ((pdata->prescale << KEY_INIT_PRSCL_SHIFT) & KEY_INIT_PRSCL_MASK);

	__raw_writel(val, keypad->mmio_base + KEY_INIT);
}

static int ep93xx_keypad_open(struct input_dev *pdev)
{
	struct ep93xx_keypad *keypad = input_get_drvdata(pdev);

	if (!keypad->enabled) {
		ep93xx_keypad_config(keypad);
		clk_enable(keypad->clk);
		keypad->enabled = true;
	}

	return 0;
}

static void ep93xx_keypad_close(struct input_dev *pdev)
{
	struct ep93xx_keypad *keypad = input_get_drvdata(pdev);

	if (keypad->enabled) {
		clk_disable(keypad->clk);
		keypad->enabled = false;
	}
}


#ifdef CONFIG_PM_SLEEP
static int ep93xx_keypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ep93xx_keypad *keypad = platform_get_drvdata(pdev);
	struct input_dev *input_dev = keypad->input_dev;

	mutex_lock(&input_dev->mutex);

	if (keypad->enabled) {
		clk_disable(keypad->clk);
		keypad->enabled = false;
	}

	mutex_unlock(&input_dev->mutex);

	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(keypad->irq);

	return 0;
}

static int ep93xx_keypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ep93xx_keypad *keypad = platform_get_drvdata(pdev);
	struct input_dev *input_dev = keypad->input_dev;

	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(keypad->irq);

	mutex_lock(&input_dev->mutex);

	if (input_dev->users) {
		if (!keypad->enabled) {
			ep93xx_keypad_config(keypad);
			clk_enable(keypad->clk);
			keypad->enabled = true;
		}
	}

	mutex_unlock(&input_dev->mutex);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(ep93xx_keypad_pm_ops,
			 ep93xx_keypad_suspend, ep93xx_keypad_resume);

static int ep93xx_keypad_probe(struct platform_device *pdev)
{
	struct ep93xx_keypad *keypad;
	const struct matrix_keymap_data *keymap_data;
	struct input_dev *input_dev;
	struct resource *res;
	int err;

	keypad = kzalloc(sizeof(struct ep93xx_keypad), GFP_KERNEL);
	if (!keypad)
		return -ENOMEM;

	keypad->pdata = dev_get_platdata(&pdev->dev);
	if (!keypad->pdata) {
		err = -EINVAL;
		goto failed_free;
	}

	keymap_data = keypad->pdata->keymap_data;
	if (!keymap_data) {
		err = -EINVAL;
		goto failed_free;
	}

	keypad->irq = platform_get_irq(pdev, 0);
	if (!keypad->irq) {
		err = -ENXIO;
		goto failed_free;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENXIO;
		goto failed_free;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!res) {
		err = -EBUSY;
		goto failed_free;
	}

	keypad->mmio_base = ioremap(res->start, resource_size(res));
	if (keypad->mmio_base == NULL) {
		err = -ENXIO;
		goto failed_free_mem;
	}

	err = ep93xx_keypad_acquire_gpio(pdev);
	if (err)
		goto failed_free_io;

	keypad->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(keypad->clk)) {
		err = PTR_ERR(keypad->clk);
		goto failed_free_gpio;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		goto failed_put_clk;
	}

	keypad->input_dev = input_dev;

	input_dev->name = pdev->name;
	input_dev->id.bustype = BUS_HOST;
	input_dev->open = ep93xx_keypad_open;
	input_dev->close = ep93xx_keypad_close;
	input_dev->dev.parent = &pdev->dev;

	err = matrix_keypad_build_keymap(keymap_data, NULL,
					 EP93XX_MATRIX_ROWS, EP93XX_MATRIX_COLS,
					 keypad->keycodes, input_dev);
	if (err)
		goto failed_free_dev;

	if (keypad->pdata->flags & EP93XX_KEYPAD_AUTOREPEAT)
		__set_bit(EV_REP, input_dev->evbit);
	input_set_drvdata(input_dev, keypad);

	err = request_irq(keypad->irq, ep93xx_keypad_irq_handler,
			  0, pdev->name, keypad);
	if (err)
		goto failed_free_dev;

	err = input_register_device(input_dev);
	if (err)
		goto failed_free_irq;

	platform_set_drvdata(pdev, keypad);
	device_init_wakeup(&pdev->dev, 1);

	return 0;

failed_free_irq:
	free_irq(keypad->irq, keypad);
failed_free_dev:
	input_free_device(input_dev);
failed_put_clk:
	clk_put(keypad->clk);
failed_free_gpio:
	ep93xx_keypad_release_gpio(pdev);
failed_free_io:
	iounmap(keypad->mmio_base);
failed_free_mem:
	release_mem_region(res->start, resource_size(res));
failed_free:
	kfree(keypad);
	return err;
}

static int ep93xx_keypad_remove(struct platform_device *pdev)
{
	struct ep93xx_keypad *keypad = platform_get_drvdata(pdev);
	struct resource *res;

	free_irq(keypad->irq, keypad);

	if (keypad->enabled)
		clk_disable(keypad->clk);
	clk_put(keypad->clk);

	input_unregister_device(keypad->input_dev);

	ep93xx_keypad_release_gpio(pdev);

	iounmap(keypad->mmio_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	kfree(keypad);

	return 0;
}

static struct platform_driver ep93xx_keypad_driver = {
	.driver		= {
		.name	= "ep93xx-keypad",
		.pm	= &ep93xx_keypad_pm_ops,
	},
	.probe		= ep93xx_keypad_probe,
	.remove		= ep93xx_keypad_remove,
};
module_platform_driver(ep93xx_keypad_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_DESCRIPTION("EP93xx Matrix Keypad Controller");
MODULE_ALIAS("platform:ep93xx-keypad");
