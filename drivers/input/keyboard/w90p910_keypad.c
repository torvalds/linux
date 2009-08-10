/*
 * Copyright (c) 2008-2009 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>

#include <mach/w90p910_keypad.h>

/* Keypad Interface Control Registers */
#define KPI_CONF		0x00
#define KPI_3KCONF		0x04
#define KPI_LPCONF		0x08
#define KPI_STATUS		0x0C

#define IS1KEY			(0x01 << 16)
#define INTTR			(0x01 << 21)
#define KEY0R			(0x0f << 3)
#define KEY0C			0x07
#define DEBOUNCE_BIT		0x08
#define KSIZE0			(0x01 << 16)
#define KSIZE1			(0x01 << 17)
#define KPSEL			(0x01 << 19)
#define ENKP			(0x01 << 18)

#define KGET_RAW(n)		(((n) & KEY0R) >> 3)
#define KGET_COLUMN(n)		((n) & KEY0C)

#define MAX_MATRIX_KEY_NUM	(8 * 8)

struct w90p910_keypad {
	struct w90p910_keypad_platform_data *pdata;
	struct clk *clk;
	struct input_dev *input_dev;
	void __iomem *mmio_base;
	int irq;
	unsigned int matrix_keycodes[MAX_MATRIX_KEY_NUM];
};

static void w90p910_keypad_build_keycode(struct w90p910_keypad *keypad)
{
	struct w90p910_keypad_platform_data *pdata = keypad->pdata;
	struct input_dev *input_dev = keypad->input_dev;
	unsigned int *key;
	int i;

	key = &pdata->matrix_key_map[0];
	for (i = 0; i < pdata->matrix_key_map_size; i++, key++) {
		int row = KEY_ROW(*key);
		int col = KEY_COL(*key);
		int code = KEY_VAL(*key);

		keypad->matrix_keycodes[(row << 3) + col] = code;
		__set_bit(code, input_dev->keybit);
	}
}

static inline unsigned int lookup_matrix_keycode(
		struct w90p910_keypad *keypad, int row, int col)
{
	return keypad->matrix_keycodes[(row << 3) + col];
}

static void w90p910_keypad_scan_matrix(struct w90p910_keypad *keypad,
							unsigned int status)
{
	unsigned int row, col, val;

	row = KGET_RAW(status);
	col = KGET_COLUMN(status);

	val = lookup_matrix_keycode(keypad, row, col);

	input_report_key(keypad->input_dev, val, 1);

	input_sync(keypad->input_dev);

	input_report_key(keypad->input_dev, val, 0);

	input_sync(keypad->input_dev);
}

static irqreturn_t w90p910_keypad_irq_handler(int irq, void *dev_id)
{
	struct w90p910_keypad *keypad = dev_id;
	unsigned int  kstatus, val;

	kstatus = __raw_readl(keypad->mmio_base + KPI_STATUS);

	val = INTTR | IS1KEY;

	if (kstatus & val)
		w90p910_keypad_scan_matrix(keypad, kstatus);

	return IRQ_HANDLED;
}

static int w90p910_keypad_open(struct input_dev *dev)
{
	struct w90p910_keypad *keypad = input_get_drvdata(dev);
	struct w90p910_keypad_platform_data *pdata = keypad->pdata;
	unsigned int val, config;

	/* Enable unit clock */
	clk_enable(keypad->clk);

	val = __raw_readl(keypad->mmio_base + KPI_CONF);
	val |= (KPSEL | ENKP);
	val &= ~(KSIZE0 | KSIZE1);

	config = pdata->prescale | (pdata->debounce << DEBOUNCE_BIT);

	val |= config;

	__raw_writel(val, keypad->mmio_base + KPI_CONF);

	return 0;
}

static void w90p910_keypad_close(struct input_dev *dev)
{
	struct w90p910_keypad *keypad = input_get_drvdata(dev);

	/* Disable clock unit */
	clk_disable(keypad->clk);
}

static int __devinit w90p910_keypad_probe(struct platform_device *pdev)
{
	struct w90p910_keypad *keypad;
	struct input_dev *input_dev;
	struct resource *res;
	int irq, error;

	keypad = kzalloc(sizeof(struct w90p910_keypad), GFP_KERNEL);
	if (keypad == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	keypad->pdata = pdev->dev.platform_data;
	if (keypad->pdata == NULL) {
		dev_err(&pdev->dev, "no platform data defined\n");
		error = -EINVAL;
		goto failed_free;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get keypad irq\n");
		error = -ENXIO;
		goto failed_free;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get I/O memory\n");
		error = -ENXIO;
		goto failed_free;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to request I/O memory\n");
		error = -EBUSY;
		goto failed_free;
	}

	keypad->mmio_base = ioremap(res->start, resource_size(res));
	if (keypad->mmio_base == NULL) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		error = -ENXIO;
		goto failed_free_mem;
	}

	keypad->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(keypad->clk)) {
		dev_err(&pdev->dev, "failed to get keypad clock\n");
		error = PTR_ERR(keypad->clk);
		goto failed_free_io;
	}

	/* Create and register the input driver. */
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		error = -ENOMEM;
		goto failed_put_clk;
	}

	/* set multi-function pin for w90p910 kpi. */
	mfp_set_groupi(&pdev->dev);

	input_dev->name = pdev->name;
	input_dev->id.bustype = BUS_HOST;
	input_dev->open = w90p910_keypad_open;
	input_dev->close = w90p910_keypad_close;
	input_dev->dev.parent = &pdev->dev;
	input_dev->keycode = keypad->matrix_keycodes;
	input_dev->keycodesize = sizeof(keypad->matrix_keycodes[0]);
	input_dev->keycodemax = ARRAY_SIZE(keypad->matrix_keycodes);

	keypad->input_dev = input_dev;
	input_set_drvdata(input_dev, keypad);

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	w90p910_keypad_build_keycode(keypad);
	platform_set_drvdata(pdev, keypad);

	error = request_irq(irq, w90p910_keypad_irq_handler, IRQF_DISABLED,
			    pdev->name, keypad);
	if (error) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		goto failed_free_dev;
	}

	keypad->irq = irq;

	/* Register the input device */
	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto failed_free_irq;
	}

	return 0;

failed_free_irq:
	free_irq(irq, pdev);
	platform_set_drvdata(pdev, NULL);
failed_free_dev:
	input_free_device(input_dev);
failed_put_clk:
	clk_put(keypad->clk);
failed_free_io:
	iounmap(keypad->mmio_base);
failed_free_mem:
	release_mem_region(res->start, resource_size(res));
failed_free:
	kfree(keypad);
	return error;
}

static int __devexit w90p910_keypad_remove(struct platform_device *pdev)
{
	struct w90p910_keypad *keypad = platform_get_drvdata(pdev);
	struct resource *res;

	free_irq(keypad->irq, pdev);

	clk_put(keypad->clk);

	input_unregister_device(keypad->input_dev);

	iounmap(keypad->mmio_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	platform_set_drvdata(pdev, NULL);
	kfree(keypad);
	return 0;
}

static struct platform_driver w90p910_keypad_driver = {
	.probe		= w90p910_keypad_probe,
	.remove		= __devexit_p(w90p910_keypad_remove),
	.driver		= {
		.name	= "w90p910-keypad",
		.owner	= THIS_MODULE,
	},
};

static int __init w90p910_keypad_init(void)
{
	return platform_driver_register(&w90p910_keypad_driver);
}

static void __exit w90p910_keypad_exit(void)
{
	platform_driver_unregister(&w90p910_keypad_driver);
}

module_init(w90p910_keypad_init);
module_exit(w90p910_keypad_exit);

MODULE_AUTHOR("Wan ZongShun <mcuos.com@gmail.com>");
MODULE_DESCRIPTION("w90p910 keypad driver!");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:w90p910-keypad");
