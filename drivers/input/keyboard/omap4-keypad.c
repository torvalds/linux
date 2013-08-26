/*
 * OMAP4 Keypad Driver
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * Author: Abraham Arce <x0066660@ti.com>
 * Initial Code: Syed Rafiuddin <rafiuddin.syed@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include <linux/platform_data/omap4-keypad.h>

/* OMAP4 registers */
#define OMAP4_KBD_REVISION		0x00
#define OMAP4_KBD_SYSCONFIG		0x10
#define OMAP4_KBD_SYSSTATUS		0x14
#define OMAP4_KBD_IRQSTATUS		0x18
#define OMAP4_KBD_IRQENABLE		0x1C
#define OMAP4_KBD_WAKEUPENABLE		0x20
#define OMAP4_KBD_PENDING		0x24
#define OMAP4_KBD_CTRL			0x28
#define OMAP4_KBD_DEBOUNCINGTIME	0x2C
#define OMAP4_KBD_LONGKEYTIME		0x30
#define OMAP4_KBD_TIMEOUT		0x34
#define OMAP4_KBD_STATEMACHINE		0x38
#define OMAP4_KBD_ROWINPUTS		0x3C
#define OMAP4_KBD_COLUMNOUTPUTS		0x40
#define OMAP4_KBD_FULLCODE31_0		0x44
#define OMAP4_KBD_FULLCODE63_32		0x48

/* OMAP4 bit definitions */
#define OMAP4_DEF_IRQENABLE_EVENTEN	BIT(0)
#define OMAP4_DEF_IRQENABLE_LONGKEY	BIT(1)
#define OMAP4_DEF_WUP_EVENT_ENA		BIT(0)
#define OMAP4_DEF_WUP_LONG_KEY_ENA	BIT(1)
#define OMAP4_DEF_CTRL_NOSOFTMODE	BIT(1)
#define OMAP4_DEF_CTRL_PTV_SHIFT	2

/* OMAP4 values */
#define OMAP4_VAL_IRQDISABLE		0x0
#define OMAP4_VAL_DEBOUNCINGTIME	0x7
#define OMAP4_VAL_PVT			0x7

enum {
	KBD_REVISION_OMAP4 = 0,
	KBD_REVISION_OMAP5,
};

struct omap4_keypad {
	struct input_dev *input;

	void __iomem *base;
	unsigned int irq;

	unsigned int rows;
	unsigned int cols;
	u32 reg_offset;
	u32 irqreg_offset;
	unsigned int row_shift;
	bool no_autorepeat;
	unsigned char key_state[8];
	unsigned short *keymap;
};

static int kbd_readl(struct omap4_keypad *keypad_data, u32 offset)
{
	return __raw_readl(keypad_data->base +
				keypad_data->reg_offset + offset);
}

static void kbd_writel(struct omap4_keypad *keypad_data, u32 offset, u32 value)
{
	__raw_writel(value,
		     keypad_data->base + keypad_data->reg_offset + offset);
}

static int kbd_read_irqreg(struct omap4_keypad *keypad_data, u32 offset)
{
	return __raw_readl(keypad_data->base +
				keypad_data->irqreg_offset + offset);
}

static void kbd_write_irqreg(struct omap4_keypad *keypad_data,
			     u32 offset, u32 value)
{
	__raw_writel(value,
		     keypad_data->base + keypad_data->irqreg_offset + offset);
}


/* Interrupt handlers */
static irqreturn_t omap4_keypad_irq_handler(int irq, void *dev_id)
{
	struct omap4_keypad *keypad_data = dev_id;

	if (kbd_read_irqreg(keypad_data, OMAP4_KBD_IRQSTATUS)) {
		/* Disable interrupts */
		kbd_write_irqreg(keypad_data, OMAP4_KBD_IRQENABLE,
				 OMAP4_VAL_IRQDISABLE);
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static irqreturn_t omap4_keypad_irq_thread_fn(int irq, void *dev_id)
{
	struct omap4_keypad *keypad_data = dev_id;
	struct input_dev *input_dev = keypad_data->input;
	unsigned char key_state[ARRAY_SIZE(keypad_data->key_state)];
	unsigned int col, row, code, changed;
	u32 *new_state = (u32 *) key_state;

	*new_state = kbd_readl(keypad_data, OMAP4_KBD_FULLCODE31_0);
	*(new_state + 1) = kbd_readl(keypad_data, OMAP4_KBD_FULLCODE63_32);

	for (row = 0; row < keypad_data->rows; row++) {
		changed = key_state[row] ^ keypad_data->key_state[row];
		if (!changed)
			continue;

		for (col = 0; col < keypad_data->cols; col++) {
			if (changed & (1 << col)) {
				code = MATRIX_SCAN_CODE(row, col,
						keypad_data->row_shift);
				input_event(input_dev, EV_MSC, MSC_SCAN, code);
				input_report_key(input_dev,
						 keypad_data->keymap[code],
						 key_state[row] & (1 << col));
			}
		}
	}

	input_sync(input_dev);

	memcpy(keypad_data->key_state, key_state,
		sizeof(keypad_data->key_state));

	/* clear pending interrupts */
	kbd_write_irqreg(keypad_data, OMAP4_KBD_IRQSTATUS,
			 kbd_read_irqreg(keypad_data, OMAP4_KBD_IRQSTATUS));

	/* enable interrupts */
	kbd_write_irqreg(keypad_data, OMAP4_KBD_IRQENABLE,
		OMAP4_DEF_IRQENABLE_EVENTEN |
				OMAP4_DEF_IRQENABLE_LONGKEY);

	return IRQ_HANDLED;
}

static int omap4_keypad_open(struct input_dev *input)
{
	struct omap4_keypad *keypad_data = input_get_drvdata(input);

	pm_runtime_get_sync(input->dev.parent);

	disable_irq(keypad_data->irq);

	kbd_writel(keypad_data, OMAP4_KBD_CTRL,
			OMAP4_DEF_CTRL_NOSOFTMODE |
			(OMAP4_VAL_PVT << OMAP4_DEF_CTRL_PTV_SHIFT));
	kbd_writel(keypad_data, OMAP4_KBD_DEBOUNCINGTIME,
			OMAP4_VAL_DEBOUNCINGTIME);
	/* clear pending interrupts */
	kbd_write_irqreg(keypad_data, OMAP4_KBD_IRQSTATUS,
			 kbd_read_irqreg(keypad_data, OMAP4_KBD_IRQSTATUS));
	kbd_write_irqreg(keypad_data, OMAP4_KBD_IRQENABLE,
			OMAP4_DEF_IRQENABLE_EVENTEN |
				OMAP4_DEF_IRQENABLE_LONGKEY);
	kbd_writel(keypad_data, OMAP4_KBD_WAKEUPENABLE,
			OMAP4_DEF_WUP_EVENT_ENA | OMAP4_DEF_WUP_LONG_KEY_ENA);

	enable_irq(keypad_data->irq);

	return 0;
}

static void omap4_keypad_close(struct input_dev *input)
{
	struct omap4_keypad *keypad_data = input_get_drvdata(input);

	disable_irq(keypad_data->irq);

	/* Disable interrupts */
	kbd_write_irqreg(keypad_data, OMAP4_KBD_IRQENABLE,
			 OMAP4_VAL_IRQDISABLE);

	/* clear pending interrupts */
	kbd_write_irqreg(keypad_data, OMAP4_KBD_IRQSTATUS,
			 kbd_read_irqreg(keypad_data, OMAP4_KBD_IRQSTATUS));

	enable_irq(keypad_data->irq);

	pm_runtime_put_sync(input->dev.parent);
}

#ifdef CONFIG_OF
static int omap4_keypad_parse_dt(struct device *dev,
				 struct omap4_keypad *keypad_data)
{
	struct device_node *np = dev->of_node;
	int err;

	err = matrix_keypad_parse_of_params(dev, &keypad_data->rows,
					    &keypad_data->cols);
	if (err)
		return err;

	if (of_get_property(np, "linux,input-no-autorepeat", NULL))
		keypad_data->no_autorepeat = true;

	return 0;
}
#else
static inline int omap4_keypad_parse_dt(struct device *dev,
					struct omap4_keypad *keypad_data)
{
	return -ENOSYS;
}
#endif

static int omap4_keypad_probe(struct platform_device *pdev)
{
	const struct omap4_keypad_platform_data *pdata =
				dev_get_platdata(&pdev->dev);
	const struct matrix_keymap_data *keymap_data =
				pdata ? pdata->keymap_data : NULL;
	struct omap4_keypad *keypad_data;
	struct input_dev *input_dev;
	struct resource *res;
	unsigned int max_keys;
	int rev;
	int irq;
	int error;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no base address specified\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "no keyboard irq assigned\n");
		return -EINVAL;
	}

	keypad_data = kzalloc(sizeof(struct omap4_keypad), GFP_KERNEL);
	if (!keypad_data) {
		dev_err(&pdev->dev, "keypad_data memory allocation failed\n");
		return -ENOMEM;
	}

	keypad_data->irq = irq;

	if (pdata) {
		keypad_data->rows = pdata->rows;
		keypad_data->cols = pdata->cols;
	} else {
		error = omap4_keypad_parse_dt(&pdev->dev, keypad_data);
		if (error)
			return error;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!res) {
		dev_err(&pdev->dev, "can't request mem region\n");
		error = -EBUSY;
		goto err_free_keypad;
	}

	keypad_data->base = ioremap(res->start, resource_size(res));
	if (!keypad_data->base) {
		dev_err(&pdev->dev, "can't ioremap mem resource\n");
		error = -ENOMEM;
		goto err_release_mem;
	}


	/*
	 * Enable clocks for the keypad module so that we can read
	 * revision register.
	 */
	pm_runtime_enable(&pdev->dev);
	error = pm_runtime_get_sync(&pdev->dev);
	if (error) {
		dev_err(&pdev->dev, "pm_runtime_get_sync() failed\n");
		goto err_unmap;
	}
	rev = __raw_readl(keypad_data->base + OMAP4_KBD_REVISION);
	rev &= 0x03 << 30;
	rev >>= 30;
	switch (rev) {
	case KBD_REVISION_OMAP4:
		keypad_data->reg_offset = 0x00;
		keypad_data->irqreg_offset = 0x00;
		break;
	case KBD_REVISION_OMAP5:
		keypad_data->reg_offset = 0x10;
		keypad_data->irqreg_offset = 0x0c;
		break;
	default:
		dev_err(&pdev->dev,
			"Keypad reports unsupported revision %d", rev);
		error = -EINVAL;
		goto err_pm_put_sync;
	}

	/* input device allocation */
	keypad_data->input = input_dev = input_allocate_device();
	if (!input_dev) {
		error = -ENOMEM;
		goto err_pm_put_sync;
	}

	input_dev->name = pdev->name;
	input_dev->dev.parent = &pdev->dev;
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0001;

	input_dev->open = omap4_keypad_open;
	input_dev->close = omap4_keypad_close;

	input_set_capability(input_dev, EV_MSC, MSC_SCAN);
	if (!keypad_data->no_autorepeat)
		__set_bit(EV_REP, input_dev->evbit);

	input_set_drvdata(input_dev, keypad_data);

	keypad_data->row_shift = get_count_order(keypad_data->cols);
	max_keys = keypad_data->rows << keypad_data->row_shift;
	keypad_data->keymap = kzalloc(max_keys * sizeof(keypad_data->keymap[0]),
				      GFP_KERNEL);
	if (!keypad_data->keymap) {
		dev_err(&pdev->dev, "Not enough memory for keymap\n");
		error = -ENOMEM;
		goto err_free_input;
	}

	error = matrix_keypad_build_keymap(keymap_data, NULL,
					   keypad_data->rows, keypad_data->cols,
					   keypad_data->keymap, input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to build keymap\n");
		goto err_free_keymap;
	}

	error = request_threaded_irq(keypad_data->irq, omap4_keypad_irq_handler,
				     omap4_keypad_irq_thread_fn,
				     IRQF_TRIGGER_RISING,
				     "omap4-keypad", keypad_data);
	if (error) {
		dev_err(&pdev->dev, "failed to register interrupt\n");
		goto err_free_input;
	}

	pm_runtime_put_sync(&pdev->dev);

	error = input_register_device(keypad_data->input);
	if (error < 0) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto err_pm_disable;
	}

	platform_set_drvdata(pdev, keypad_data);
	return 0;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);
	free_irq(keypad_data->irq, keypad_data);
err_free_keymap:
	kfree(keypad_data->keymap);
err_free_input:
	input_free_device(input_dev);
err_pm_put_sync:
	pm_runtime_put_sync(&pdev->dev);
err_unmap:
	iounmap(keypad_data->base);
err_release_mem:
	release_mem_region(res->start, resource_size(res));
err_free_keypad:
	kfree(keypad_data);
	return error;
}

static int omap4_keypad_remove(struct platform_device *pdev)
{
	struct omap4_keypad *keypad_data = platform_get_drvdata(pdev);
	struct resource *res;

	free_irq(keypad_data->irq, keypad_data);

	pm_runtime_disable(&pdev->dev);

	input_unregister_device(keypad_data->input);

	iounmap(keypad_data->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	kfree(keypad_data->keymap);
	kfree(keypad_data);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id omap_keypad_dt_match[] = {
	{ .compatible = "ti,omap4-keypad" },
	{},
};
MODULE_DEVICE_TABLE(of, omap_keypad_dt_match);
#endif

static struct platform_driver omap4_keypad_driver = {
	.probe		= omap4_keypad_probe,
	.remove		= omap4_keypad_remove,
	.driver		= {
		.name	= "omap4-keypad",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(omap_keypad_dt_match),
	},
};
module_platform_driver(omap4_keypad_driver);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("OMAP4 Keypad Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:omap4-keypad");
