/*
 * Texas Instruments TNETV107X Keypad Driver
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/input/matrix_keypad.h>
#include <linux/module.h>

#define BITS(x)			(BIT(x) - 1)

#define KEYPAD_ROWS		9
#define KEYPAD_COLS		9

#define DEBOUNCE_MIN		0x400ul
#define DEBOUNCE_MAX		0x3ffffffful

struct keypad_regs {
	u32	rev;
	u32	mode;
	u32	mask;
	u32	pol;
	u32	dclock;
	u32	rclock;
	u32	stable_cnt;
	u32	in_en;
	u32	out;
	u32	out_en;
	u32	in;
	u32	lock;
	u32	pres[3];
};

#define keypad_read(kp, reg)		__raw_readl(&(kp)->regs->reg)
#define keypad_write(kp, reg, val)	__raw_writel(val, &(kp)->regs->reg)

struct keypad_data {
	struct input_dev		*input_dev;
	struct resource			*res;
	struct keypad_regs __iomem	*regs;
	struct clk			*clk;
	struct device			*dev;
	spinlock_t			lock;
	u32				irq_press;
	u32				irq_release;
	int				rows, cols, row_shift;
	int				debounce_ms, active_low;
	u32				prev_keys[3];
	unsigned short			keycodes[];
};

static irqreturn_t keypad_irq(int irq, void *data)
{
	struct keypad_data *kp = data;
	int i, bit, val, row, col, code;
	unsigned long flags;
	u32 curr_keys[3];
	u32 change;

	spin_lock_irqsave(&kp->lock, flags);

	memset(curr_keys, 0, sizeof(curr_keys));
	if (irq == kp->irq_press)
		for (i = 0; i < 3; i++)
			curr_keys[i] = keypad_read(kp, pres[i]);

	for (i = 0; i < 3; i++) {
		change = curr_keys[i] ^ kp->prev_keys[i];

		while (change) {
			bit     = fls(change) - 1;
			change ^= BIT(bit);
			val     = curr_keys[i] & BIT(bit);
			bit    += i * 32;
			row     = bit / KEYPAD_COLS;
			col     = bit % KEYPAD_COLS;

			code = MATRIX_SCAN_CODE(row, col, kp->row_shift);
			input_event(kp->input_dev, EV_MSC, MSC_SCAN, code);
			input_report_key(kp->input_dev, kp->keycodes[code],
					 val);
		}
	}
	input_sync(kp->input_dev);
	memcpy(kp->prev_keys, curr_keys, sizeof(curr_keys));

	if (irq == kp->irq_press)
		keypad_write(kp, lock, 0); /* Allow hardware updates */

	spin_unlock_irqrestore(&kp->lock, flags);

	return IRQ_HANDLED;
}

static int keypad_start(struct input_dev *dev)
{
	struct keypad_data *kp = input_get_drvdata(dev);
	unsigned long mask, debounce, clk_rate_khz;
	unsigned long flags;

	clk_enable(kp->clk);
	clk_rate_khz = clk_get_rate(kp->clk) / 1000;

	spin_lock_irqsave(&kp->lock, flags);

	/* Initialize device registers */
	keypad_write(kp, mode, 0);

	mask  = BITS(kp->rows) << KEYPAD_COLS;
	mask |= BITS(kp->cols);
	keypad_write(kp, mask, ~mask);

	keypad_write(kp, pol, kp->active_low ? 0 : 0x3ffff);
	keypad_write(kp, stable_cnt, 3);

	debounce = kp->debounce_ms * clk_rate_khz;
	debounce = clamp(debounce, DEBOUNCE_MIN, DEBOUNCE_MAX);
	keypad_write(kp, dclock, debounce);
	keypad_write(kp, rclock, 4 * debounce);

	keypad_write(kp, in_en, 1);

	spin_unlock_irqrestore(&kp->lock, flags);

	return 0;
}

static void keypad_stop(struct input_dev *dev)
{
	struct keypad_data *kp = input_get_drvdata(dev);

	synchronize_irq(kp->irq_press);
	synchronize_irq(kp->irq_release);
	clk_disable(kp->clk);
}

static int __devinit keypad_probe(struct platform_device *pdev)
{
	const struct matrix_keypad_platform_data *pdata;
	const struct matrix_keymap_data *keymap_data;
	struct device *dev = &pdev->dev;
	struct keypad_data *kp;
	int error = 0, sz, row_shift;
	u32 rev = 0;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(dev, "cannot find device data\n");
		return -EINVAL;
	}

	keymap_data = pdata->keymap_data;
	if (!keymap_data) {
		dev_err(dev, "cannot find keymap data\n");
		return -EINVAL;
	}

	row_shift = get_count_order(pdata->num_col_gpios);
	sz  = offsetof(struct keypad_data, keycodes);
	sz += (pdata->num_row_gpios << row_shift) * sizeof(kp->keycodes[0]);
	kp = kzalloc(sz, GFP_KERNEL);
	if (!kp) {
		dev_err(dev, "cannot allocate device info\n");
		return -ENOMEM;
	}

	kp->dev  = dev;
	kp->rows = pdata->num_row_gpios;
	kp->cols = pdata->num_col_gpios;
	kp->row_shift = row_shift;
	platform_set_drvdata(pdev, kp);
	spin_lock_init(&kp->lock);

	kp->irq_press   = platform_get_irq_byname(pdev, "press");
	kp->irq_release = platform_get_irq_byname(pdev, "release");
	if (kp->irq_press < 0 || kp->irq_release < 0) {
		dev_err(dev, "cannot determine device interrupts\n");
		error = -ENODEV;
		goto error_res;
	}

	kp->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!kp->res) {
		dev_err(dev, "cannot determine register area\n");
		error = -ENODEV;
		goto error_res;
	}

	if (!request_mem_region(kp->res->start, resource_size(kp->res),
				pdev->name)) {
		dev_err(dev, "cannot claim register memory\n");
		kp->res = NULL;
		error = -EINVAL;
		goto error_res;
	}

	kp->regs = ioremap(kp->res->start, resource_size(kp->res));
	if (!kp->regs) {
		dev_err(dev, "cannot map register memory\n");
		error = -ENOMEM;
		goto error_map;
	}

	kp->clk = clk_get(dev, NULL);
	if (IS_ERR(kp->clk)) {
		dev_err(dev, "cannot claim device clock\n");
		error = PTR_ERR(kp->clk);
		goto error_clk;
	}

	error = request_threaded_irq(kp->irq_press, NULL, keypad_irq, 0,
				     dev_name(dev), kp);
	if (error < 0) {
		dev_err(kp->dev, "Could not allocate keypad press key irq\n");
		goto error_irq_press;
	}

	error = request_threaded_irq(kp->irq_release, NULL, keypad_irq, 0,
				     dev_name(dev), kp);
	if (error < 0) {
		dev_err(kp->dev, "Could not allocate keypad release key irq\n");
		goto error_irq_release;
	}

	kp->input_dev = input_allocate_device();
	if (!kp->input_dev) {
		dev_err(dev, "cannot allocate input device\n");
		error = -ENOMEM;
		goto error_input;
	}
	input_set_drvdata(kp->input_dev, kp);

	kp->input_dev->name	  = pdev->name;
	kp->input_dev->dev.parent = &pdev->dev;
	kp->input_dev->open	  = keypad_start;
	kp->input_dev->close	  = keypad_stop;
	kp->input_dev->evbit[0]	  = BIT_MASK(EV_KEY);
	if (!pdata->no_autorepeat)
		kp->input_dev->evbit[0] |= BIT_MASK(EV_REP);

	clk_enable(kp->clk);
	rev = keypad_read(kp, rev);
	kp->input_dev->id.bustype = BUS_HOST;
	kp->input_dev->id.product = ((rev >>  8) & 0x07);
	kp->input_dev->id.version = ((rev >> 16) & 0xfff);
	clk_disable(kp->clk);

	kp->input_dev->keycode     = kp->keycodes;
	kp->input_dev->keycodesize = sizeof(kp->keycodes[0]);
	kp->input_dev->keycodemax  = kp->rows << kp->row_shift;

	matrix_keypad_build_keymap(keymap_data, kp->row_shift, kp->keycodes,
				   kp->input_dev->keybit);

	input_set_capability(kp->input_dev, EV_MSC, MSC_SCAN);

	error = input_register_device(kp->input_dev);
	if (error < 0) {
		dev_err(dev, "Could not register input device\n");
		goto error_reg;
	}

	return 0;


error_reg:
	input_free_device(kp->input_dev);
error_input:
	free_irq(kp->irq_release, kp);
error_irq_release:
	free_irq(kp->irq_press, kp);
error_irq_press:
	clk_put(kp->clk);
error_clk:
	iounmap(kp->regs);
error_map:
	release_mem_region(kp->res->start, resource_size(kp->res));
error_res:
	platform_set_drvdata(pdev, NULL);
	kfree(kp);
	return error;
}

static int __devexit keypad_remove(struct platform_device *pdev)
{
	struct keypad_data *kp = platform_get_drvdata(pdev);

	free_irq(kp->irq_press, kp);
	free_irq(kp->irq_release, kp);
	input_unregister_device(kp->input_dev);
	clk_put(kp->clk);
	iounmap(kp->regs);
	release_mem_region(kp->res->start, resource_size(kp->res));
	platform_set_drvdata(pdev, NULL);
	kfree(kp);

	return 0;
}

static struct platform_driver keypad_driver = {
	.probe		= keypad_probe,
	.remove		= __devexit_p(keypad_remove),
	.driver.name	= "tnetv107x-keypad",
	.driver.owner	= THIS_MODULE,
};

static int __init keypad_init(void)
{
	return platform_driver_register(&keypad_driver);
}

static void __exit keypad_exit(void)
{
	platform_driver_unregister(&keypad_driver);
}

module_init(keypad_init);
module_exit(keypad_exit);

MODULE_AUTHOR("Cyril Chemparathy");
MODULE_DESCRIPTION("TNETV107X Keypad Driver");
MODULE_ALIAS("platform:tnetv107x-keypad");
MODULE_LICENSE("GPL");
