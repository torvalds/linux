// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Samsung keypad driver
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 * Author: Donghwa Lee <dh09.lee@samsung.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/input/samsung-keypad.h>

#define SAMSUNG_KEYIFCON			0x00
#define SAMSUNG_KEYIFSTSCLR			0x04
#define SAMSUNG_KEYIFCOL			0x08
#define SAMSUNG_KEYIFROW			0x0c
#define SAMSUNG_KEYIFFC				0x10

/* SAMSUNG_KEYIFCON */
#define SAMSUNG_KEYIFCON_INT_F_EN		BIT(0)
#define SAMSUNG_KEYIFCON_INT_R_EN		BIT(1)
#define SAMSUNG_KEYIFCON_DF_EN			BIT(2)
#define SAMSUNG_KEYIFCON_FC_EN			BIT(3)
#define SAMSUNG_KEYIFCON_WAKEUPEN		BIT(4)

/* SAMSUNG_KEYIFSTSCLR */
#define SAMSUNG_KEYIFSTSCLR_P_INT_MASK		(0xff << 0)
#define SAMSUNG_KEYIFSTSCLR_R_INT_MASK		(0xff << 8)
#define SAMSUNG_KEYIFSTSCLR_R_INT_OFFSET	8
#define S5PV210_KEYIFSTSCLR_P_INT_MASK		(0x3fff << 0)
#define S5PV210_KEYIFSTSCLR_R_INT_MASK		(0x3fff << 16)
#define S5PV210_KEYIFSTSCLR_R_INT_OFFSET	16

/* SAMSUNG_KEYIFCOL */
#define SAMSUNG_KEYIFCOL_MASK			0xff

/* SAMSUNG_KEYIFROW */
#define SAMSUNG_KEYIFROW_MASK			(0xff << 0)
#define S5PV210_KEYIFROW_MASK			(0x3fff << 0)

/* SAMSUNG_KEYIFFC */
#define SAMSUNG_KEYIFFC_MASK			(0x3ff << 0)

struct samsung_chip_info {
	unsigned int column_shift;
};

struct samsung_keypad {
	const struct samsung_chip_info *chip;
	struct input_dev *input_dev;
	struct platform_device *pdev;
	struct clk *clk;
	void __iomem *base;
	wait_queue_head_t wait;
	bool stopped;
	bool wake_enabled;
	int irq;
	unsigned int row_shift;
	unsigned int rows;
	unsigned int cols;
	unsigned int row_state[SAMSUNG_MAX_COLS];
	unsigned short keycodes[];
};

static void samsung_keypad_scan(struct samsung_keypad *keypad,
				unsigned int *row_state)
{
	unsigned int col;
	unsigned int val;

	for (col = 0; col < keypad->cols; col++) {
		val = SAMSUNG_KEYIFCOL_MASK & ~BIT(col);
		val <<= keypad->chip->column_shift;

		writel(val, keypad->base + SAMSUNG_KEYIFCOL);
		mdelay(1);

		val = readl(keypad->base + SAMSUNG_KEYIFROW);
		row_state[col] = ~val & GENMASK(keypad->rows - 1, 0);
	}

	/* KEYIFCOL reg clear */
	writel(0, keypad->base + SAMSUNG_KEYIFCOL);
}

static bool samsung_keypad_report(struct samsung_keypad *keypad,
				  unsigned int *row_state)
{
	struct input_dev *input_dev = keypad->input_dev;
	unsigned int changed;
	unsigned int pressed;
	unsigned int key_down = 0;
	unsigned int val;
	unsigned int col, row;

	for (col = 0; col < keypad->cols; col++) {
		changed = row_state[col] ^ keypad->row_state[col];
		key_down |= row_state[col];
		if (!changed)
			continue;

		for (row = 0; row < keypad->rows; row++) {
			if (!(changed & BIT(row)))
				continue;

			pressed = row_state[col] & BIT(row);

			dev_dbg(&keypad->input_dev->dev,
				"key %s, row: %d, col: %d\n",
				pressed ? "pressed" : "released", row, col);

			val = MATRIX_SCAN_CODE(row, col, keypad->row_shift);

			input_event(input_dev, EV_MSC, MSC_SCAN, val);
			input_report_key(input_dev,
					keypad->keycodes[val], pressed);
		}
		input_sync(keypad->input_dev);
	}

	memcpy(keypad->row_state, row_state, sizeof(keypad->row_state));

	return key_down;
}

static irqreturn_t samsung_keypad_irq(int irq, void *dev_id)
{
	struct samsung_keypad *keypad = dev_id;
	unsigned int row_state[SAMSUNG_MAX_COLS];
	bool key_down;

	pm_runtime_get_sync(&keypad->pdev->dev);

	do {
		readl(keypad->base + SAMSUNG_KEYIFSTSCLR);
		/* Clear interrupt. */
		writel(~0x0, keypad->base + SAMSUNG_KEYIFSTSCLR);

		samsung_keypad_scan(keypad, row_state);

		key_down = samsung_keypad_report(keypad, row_state);
		if (key_down)
			wait_event_timeout(keypad->wait, keypad->stopped,
					   msecs_to_jiffies(50));

	} while (key_down && !keypad->stopped);

	pm_runtime_put(&keypad->pdev->dev);

	return IRQ_HANDLED;
}

static void samsung_keypad_start(struct samsung_keypad *keypad)
{
	unsigned int val;

	pm_runtime_get_sync(&keypad->pdev->dev);

	/* Tell IRQ thread that it may poll the device. */
	keypad->stopped = false;

	clk_enable(keypad->clk);

	/* Enable interrupt bits. */
	val = readl(keypad->base + SAMSUNG_KEYIFCON);
	val |= SAMSUNG_KEYIFCON_INT_F_EN | SAMSUNG_KEYIFCON_INT_R_EN;
	writel(val, keypad->base + SAMSUNG_KEYIFCON);

	/* KEYIFCOL reg clear. */
	writel(0, keypad->base + SAMSUNG_KEYIFCOL);

	pm_runtime_put(&keypad->pdev->dev);
}

static void samsung_keypad_stop(struct samsung_keypad *keypad)
{
	unsigned int val;

	pm_runtime_get_sync(&keypad->pdev->dev);

	/* Signal IRQ thread to stop polling and disable the handler. */
	keypad->stopped = true;
	wake_up(&keypad->wait);
	disable_irq(keypad->irq);

	/* Clear interrupt. */
	writel(~0x0, keypad->base + SAMSUNG_KEYIFSTSCLR);

	/* Disable interrupt bits. */
	val = readl(keypad->base + SAMSUNG_KEYIFCON);
	val &= ~(SAMSUNG_KEYIFCON_INT_F_EN | SAMSUNG_KEYIFCON_INT_R_EN);
	writel(val, keypad->base + SAMSUNG_KEYIFCON);

	clk_disable(keypad->clk);

	/*
	 * Now that chip should not generate interrupts we can safely
	 * re-enable the handler.
	 */
	enable_irq(keypad->irq);

	pm_runtime_put(&keypad->pdev->dev);
}

static int samsung_keypad_open(struct input_dev *input_dev)
{
	struct samsung_keypad *keypad = input_get_drvdata(input_dev);

	samsung_keypad_start(keypad);

	return 0;
}

static void samsung_keypad_close(struct input_dev *input_dev)
{
	struct samsung_keypad *keypad = input_get_drvdata(input_dev);

	samsung_keypad_stop(keypad);
}

#ifdef CONFIG_OF
static struct samsung_keypad_platdata *
samsung_keypad_parse_dt(struct device *dev)
{
	struct samsung_keypad_platdata *pdata;
	struct matrix_keymap_data *keymap_data;
	uint32_t *keymap, num_rows = 0, num_cols = 0;
	struct device_node *np = dev->of_node, *key_np;
	unsigned int key_count;

	if (!np) {
		dev_err(dev, "missing device tree data\n");
		return ERR_PTR(-EINVAL);
	}

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "could not allocate memory for platform data\n");
		return ERR_PTR(-ENOMEM);
	}

	of_property_read_u32(np, "samsung,keypad-num-rows", &num_rows);
	of_property_read_u32(np, "samsung,keypad-num-columns", &num_cols);
	if (!num_rows || !num_cols) {
		dev_err(dev, "number of keypad rows/columns not specified\n");
		return ERR_PTR(-EINVAL);
	}
	pdata->rows = num_rows;
	pdata->cols = num_cols;

	keymap_data = devm_kzalloc(dev, sizeof(*keymap_data), GFP_KERNEL);
	if (!keymap_data) {
		dev_err(dev, "could not allocate memory for keymap data\n");
		return ERR_PTR(-ENOMEM);
	}
	pdata->keymap_data = keymap_data;

	key_count = of_get_child_count(np);
	keymap_data->keymap_size = key_count;
	keymap = devm_kcalloc(dev, key_count, sizeof(uint32_t), GFP_KERNEL);
	if (!keymap) {
		dev_err(dev, "could not allocate memory for keymap\n");
		return ERR_PTR(-ENOMEM);
	}
	keymap_data->keymap = keymap;

	for_each_child_of_node(np, key_np) {
		u32 row, col, key_code;
		of_property_read_u32(key_np, "keypad,row", &row);
		of_property_read_u32(key_np, "keypad,column", &col);
		of_property_read_u32(key_np, "linux,code", &key_code);
		*keymap++ = KEY(row, col, key_code);
	}

	pdata->no_autorepeat = of_property_read_bool(np, "linux,input-no-autorepeat");

	pdata->wakeup = of_property_read_bool(np, "wakeup-source") ||
			/* legacy name */
			of_property_read_bool(np, "linux,input-wakeup");


	return pdata;
}
#else
static struct samsung_keypad_platdata *
samsung_keypad_parse_dt(struct device *dev)
{
	dev_err(dev, "no platform data defined\n");

	return ERR_PTR(-EINVAL);
}
#endif

static int samsung_keypad_probe(struct platform_device *pdev)
{
	const struct samsung_keypad_platdata *pdata;
	const struct matrix_keymap_data *keymap_data;
	const struct platform_device_id *id;
	struct samsung_keypad *keypad;
	struct resource *res;
	struct input_dev *input_dev;
	unsigned int row_shift;
	int error;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		pdata = samsung_keypad_parse_dt(&pdev->dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	keymap_data = pdata->keymap_data;
	if (!keymap_data) {
		dev_err(&pdev->dev, "no keymap data defined\n");
		return -EINVAL;
	}

	if (!pdata->rows || pdata->rows > SAMSUNG_MAX_ROWS)
		return -EINVAL;

	if (!pdata->cols || pdata->cols > SAMSUNG_MAX_COLS)
		return -EINVAL;

	/* initialize the gpio */
	if (pdata->cfg_gpio)
		pdata->cfg_gpio(pdata->rows, pdata->cols);

	row_shift = get_count_order(pdata->cols);

	keypad = devm_kzalloc(&pdev->dev,
			      struct_size(keypad, keycodes,
					  pdata->rows << row_shift),
			      GFP_KERNEL);
	if (!keypad)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	keypad->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!keypad->base)
		return -EBUSY;

	keypad->clk = devm_clk_get_prepared(&pdev->dev, "keypad");
	if (IS_ERR(keypad->clk)) {
		dev_err(&pdev->dev, "failed to get keypad clk\n");
		return PTR_ERR(keypad->clk);
	}

	keypad->input_dev = input_dev;
	keypad->pdev = pdev;
	keypad->row_shift = row_shift;
	keypad->rows = pdata->rows;
	keypad->cols = pdata->cols;
	keypad->stopped = true;
	init_waitqueue_head(&keypad->wait);

	keypad->chip = device_get_match_data(&pdev->dev);
	if (!keypad->chip) {
		id = platform_get_device_id(pdev);
		if (id)
			keypad->chip = (const void *)id->driver_data;
	}

	if (!keypad->chip) {
		dev_err(&pdev->dev, "Unable to determine chip type");
		return -EINVAL;
	}

	input_dev->name = pdev->name;
	input_dev->id.bustype = BUS_HOST;

	input_dev->open = samsung_keypad_open;
	input_dev->close = samsung_keypad_close;

	error = matrix_keypad_build_keymap(keymap_data, NULL,
					   pdata->rows, pdata->cols,
					   keypad->keycodes, input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to build keymap\n");
		return error;
	}

	input_set_capability(input_dev, EV_MSC, MSC_SCAN);
	if (!pdata->no_autorepeat)
		__set_bit(EV_REP, input_dev->evbit);

	input_set_drvdata(input_dev, keypad);

	keypad->irq = platform_get_irq(pdev, 0);
	if (keypad->irq < 0) {
		error = keypad->irq;
		return error;
	}

	error = devm_request_threaded_irq(&pdev->dev, keypad->irq, NULL,
					  samsung_keypad_irq, IRQF_ONESHOT,
					  dev_name(&pdev->dev), keypad);
	if (error) {
		dev_err(&pdev->dev, "failed to register keypad interrupt\n");
		return error;
	}

	device_init_wakeup(&pdev->dev, pdata->wakeup);
	platform_set_drvdata(pdev, keypad);

	error = devm_pm_runtime_enable(&pdev->dev);
	if (error)
		return error;

	error = input_register_device(keypad->input_dev);
	if (error)
		return error;

	if (pdev->dev.of_node) {
		devm_kfree(&pdev->dev, (void *)pdata->keymap_data->keymap);
		devm_kfree(&pdev->dev, (void *)pdata->keymap_data);
		devm_kfree(&pdev->dev, (void *)pdata);
	}
	return 0;
}

static int samsung_keypad_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct samsung_keypad *keypad = platform_get_drvdata(pdev);
	unsigned int val;
	int error;

	if (keypad->stopped)
		return 0;

	/* This may fail on some SoCs due to lack of controller support */
	error = enable_irq_wake(keypad->irq);
	if (!error)
		keypad->wake_enabled = true;

	val = readl(keypad->base + SAMSUNG_KEYIFCON);
	val |= SAMSUNG_KEYIFCON_WAKEUPEN;
	writel(val, keypad->base + SAMSUNG_KEYIFCON);

	clk_disable(keypad->clk);

	return 0;
}

static int samsung_keypad_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct samsung_keypad *keypad = platform_get_drvdata(pdev);
	unsigned int val;

	if (keypad->stopped)
		return 0;

	clk_enable(keypad->clk);

	val = readl(keypad->base + SAMSUNG_KEYIFCON);
	val &= ~SAMSUNG_KEYIFCON_WAKEUPEN;
	writel(val, keypad->base + SAMSUNG_KEYIFCON);

	if (keypad->wake_enabled)
		disable_irq_wake(keypad->irq);

	return 0;
}

static void samsung_keypad_toggle_wakeup(struct samsung_keypad *keypad,
					 bool enable)
{
	unsigned int val;

	clk_enable(keypad->clk);

	val = readl(keypad->base + SAMSUNG_KEYIFCON);
	if (enable) {
		val |= SAMSUNG_KEYIFCON_WAKEUPEN;
		if (device_may_wakeup(&keypad->pdev->dev))
			enable_irq_wake(keypad->irq);
	} else {
		val &= ~SAMSUNG_KEYIFCON_WAKEUPEN;
		if (device_may_wakeup(&keypad->pdev->dev))
			disable_irq_wake(keypad->irq);
	}
	writel(val, keypad->base + SAMSUNG_KEYIFCON);

	clk_disable(keypad->clk);
}

static int samsung_keypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct samsung_keypad *keypad = platform_get_drvdata(pdev);
	struct input_dev *input_dev = keypad->input_dev;

	guard(mutex)(&input_dev->mutex);

	if (input_device_enabled(input_dev))
		samsung_keypad_stop(keypad);

	samsung_keypad_toggle_wakeup(keypad, true);

	return 0;
}

static int samsung_keypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct samsung_keypad *keypad = platform_get_drvdata(pdev);
	struct input_dev *input_dev = keypad->input_dev;

	guard(mutex)(&input_dev->mutex);

	samsung_keypad_toggle_wakeup(keypad, false);

	if (input_device_enabled(input_dev))
		samsung_keypad_start(keypad);

	return 0;
}

static const struct dev_pm_ops samsung_keypad_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(samsung_keypad_suspend, samsung_keypad_resume)
	RUNTIME_PM_OPS(samsung_keypad_runtime_suspend,
		       samsung_keypad_runtime_resume, NULL)
};

static const struct samsung_chip_info samsung_s3c6410_chip_info = {
	.column_shift = 0,
};

static const struct samsung_chip_info samsung_s5pv210_chip_info = {
	.column_shift = 8,
};

#ifdef CONFIG_OF
static const struct of_device_id samsung_keypad_dt_match[] = {
	{
		.compatible = "samsung,s3c6410-keypad",
		.data = &samsung_s3c6410_chip_info,
	}, {
		.compatible = "samsung,s5pv210-keypad",
		.data = &samsung_s5pv210_chip_info,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, samsung_keypad_dt_match);
#endif

static const struct platform_device_id samsung_keypad_driver_ids[] = {
	{
		.name		= "samsung-keypad",
		.driver_data	= (kernel_ulong_t)&samsung_s3c6410_chip_info,
	}, {
		.name		= "s5pv210-keypad",
		.driver_data	= (kernel_ulong_t)&samsung_s5pv210_chip_info,
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, samsung_keypad_driver_ids);

static struct platform_driver samsung_keypad_driver = {
	.probe		= samsung_keypad_probe,
	.driver		= {
		.name	= "samsung-keypad",
		.of_match_table = of_match_ptr(samsung_keypad_dt_match),
		.pm	= pm_ptr(&samsung_keypad_pm_ops),
	},
	.id_table	= samsung_keypad_driver_ids,
};
module_platform_driver(samsung_keypad_driver);

MODULE_DESCRIPTION("Samsung keypad driver");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_AUTHOR("Donghwa Lee <dh09.lee@samsung.com>");
MODULE_LICENSE("GPL");
