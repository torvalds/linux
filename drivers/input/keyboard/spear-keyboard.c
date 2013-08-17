/*
 * SPEAr Keyboard Driver
 * Based on omap-keypad driver
 *
 * Copyright (C) 2010 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <plat/keyboard.h>

/* Keyboard Registers */
#define MODE_REG	0x00	/* 16 bit reg */
#define STATUS_REG	0x0C	/* 2 bit reg */
#define DATA_REG	0x10	/* 8 bit reg */
#define INTR_MASK	0x54

/* Register Values */
/*
 * pclk freq mask = (APB FEQ -1)= 82 MHZ.Programme bit 15-9 in mode
 * control register as 1010010(82MHZ)
 */
#define PCLK_FREQ_MSK	0xA400	/* 82 MHz */
#define START_SCAN	0x0100
#define SCAN_RATE_10	0x0000
#define SCAN_RATE_20	0x0004
#define SCAN_RATE_40	0x0008
#define SCAN_RATE_80	0x000C
#define MODE_KEYBOARD	0x0002
#define DATA_AVAIL	0x2

#define KEY_MASK	0xFF000000
#define KEY_VALUE	0x00FFFFFF
#define ROW_MASK	0xF0
#define COLUMN_MASK	0x0F
#define ROW_SHIFT	4
#define KEY_MATRIX_SHIFT	6

struct spear_kbd {
	struct input_dev *input;
	struct resource *res;
	void __iomem *io_base;
	struct clk *clk;
	unsigned int irq;
	unsigned int mode;
	unsigned short last_key;
	unsigned short keycodes[256];
};

static irqreturn_t spear_kbd_interrupt(int irq, void *dev_id)
{
	struct spear_kbd *kbd = dev_id;
	struct input_dev *input = kbd->input;
	unsigned int key;
	u8 sts, val;

	sts = readb(kbd->io_base + STATUS_REG);
	if (!(sts & DATA_AVAIL))
		return IRQ_NONE;

	if (kbd->last_key != KEY_RESERVED) {
		input_report_key(input, kbd->last_key, 0);
		kbd->last_key = KEY_RESERVED;
	}

	/* following reads active (row, col) pair */
	val = readb(kbd->io_base + DATA_REG);
	key = kbd->keycodes[val];

	input_event(input, EV_MSC, MSC_SCAN, val);
	input_report_key(input, key, 1);
	input_sync(input);

	kbd->last_key = key;

	/* clear interrupt */
	writeb(0, kbd->io_base + STATUS_REG);

	return IRQ_HANDLED;
}

static int spear_kbd_open(struct input_dev *dev)
{
	struct spear_kbd *kbd = input_get_drvdata(dev);
	int error;
	u16 val;

	kbd->last_key = KEY_RESERVED;

	error = clk_enable(kbd->clk);
	if (error)
		return error;

	/* program keyboard */
	val = SCAN_RATE_80 | MODE_KEYBOARD | PCLK_FREQ_MSK |
		(kbd->mode << KEY_MATRIX_SHIFT);
	writew(val, kbd->io_base + MODE_REG);
	writeb(1, kbd->io_base + STATUS_REG);

	/* start key scan */
	val = readw(kbd->io_base + MODE_REG);
	val |= START_SCAN;
	writew(val, kbd->io_base + MODE_REG);

	return 0;
}

static void spear_kbd_close(struct input_dev *dev)
{
	struct spear_kbd *kbd = input_get_drvdata(dev);
	u16 val;

	/* stop key scan */
	val = readw(kbd->io_base + MODE_REG);
	val &= ~START_SCAN;
	writew(val, kbd->io_base + MODE_REG);

	clk_disable(kbd->clk);

	kbd->last_key = KEY_RESERVED;
}

static int __devinit spear_kbd_probe(struct platform_device *pdev)
{
	const struct kbd_platform_data *pdata = pdev->dev.platform_data;
	const struct matrix_keymap_data *keymap;
	struct spear_kbd *kbd;
	struct input_dev *input_dev;
	struct resource *res;
	int irq;
	int error;

	if (!pdata) {
		dev_err(&pdev->dev, "Invalid platform data\n");
		return -EINVAL;
	}

	keymap = pdata->keymap;
	if (!keymap) {
		dev_err(&pdev->dev, "no keymap defined\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no keyboard resource defined\n");
		return -EBUSY;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "not able to get irq for the device\n");
		return irq;
	}

	kbd = kzalloc(sizeof(*kbd), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!kbd || !input_dev) {
		dev_err(&pdev->dev, "out of memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	kbd->input = input_dev;
	kbd->irq = irq;
	kbd->mode = pdata->mode;

	kbd->res = request_mem_region(res->start, resource_size(res),
				      pdev->name);
	if (!kbd->res) {
		dev_err(&pdev->dev, "keyboard region already claimed\n");
		error = -EBUSY;
		goto err_free_mem;
	}

	kbd->io_base = ioremap(res->start, resource_size(res));
	if (!kbd->io_base) {
		dev_err(&pdev->dev, "ioremap failed for kbd_region\n");
		error = -ENOMEM;
		goto err_release_mem_region;
	}

	kbd->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(kbd->clk)) {
		error = PTR_ERR(kbd->clk);
		goto err_iounmap;
	}

	input_dev->name = "Spear Keyboard";
	input_dev->phys = "keyboard/input0";
	input_dev->dev.parent = &pdev->dev;
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->open = spear_kbd_open;
	input_dev->close = spear_kbd_close;

	__set_bit(EV_KEY, input_dev->evbit);
	if (pdata->rep)
		__set_bit(EV_REP, input_dev->evbit);
	input_set_capability(input_dev, EV_MSC, MSC_SCAN);

	input_dev->keycode = kbd->keycodes;
	input_dev->keycodesize = sizeof(kbd->keycodes[0]);
	input_dev->keycodemax = ARRAY_SIZE(kbd->keycodes);

	matrix_keypad_build_keymap(keymap, ROW_SHIFT,
			input_dev->keycode, input_dev->keybit);

	input_set_drvdata(input_dev, kbd);

	error = request_irq(irq, spear_kbd_interrupt, 0, "keyboard", kbd);
	if (error) {
		dev_err(&pdev->dev, "request_irq fail\n");
		goto err_put_clk;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev, "Unable to register keyboard device\n");
		goto err_free_irq;
	}

	device_init_wakeup(&pdev->dev, 1);
	platform_set_drvdata(pdev, kbd);

	return 0;

err_free_irq:
	free_irq(kbd->irq, kbd);
err_put_clk:
	clk_put(kbd->clk);
err_iounmap:
	iounmap(kbd->io_base);
err_release_mem_region:
	release_mem_region(res->start, resource_size(res));
err_free_mem:
	input_free_device(input_dev);
	kfree(kbd);

	return error;
}

static int __devexit spear_kbd_remove(struct platform_device *pdev)
{
	struct spear_kbd *kbd = platform_get_drvdata(pdev);

	free_irq(kbd->irq, kbd);
	input_unregister_device(kbd->input);
	clk_put(kbd->clk);
	iounmap(kbd->io_base);
	release_mem_region(kbd->res->start, resource_size(kbd->res));
	kfree(kbd);

	device_init_wakeup(&pdev->dev, 1);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int spear_kbd_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spear_kbd *kbd = platform_get_drvdata(pdev);
	struct input_dev *input_dev = kbd->input;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		clk_enable(kbd->clk);

	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(kbd->irq);

	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int spear_kbd_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spear_kbd *kbd = platform_get_drvdata(pdev);
	struct input_dev *input_dev = kbd->input;

	mutex_lock(&input_dev->mutex);

	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(kbd->irq);

	if (input_dev->users)
		clk_enable(kbd->clk);

	mutex_unlock(&input_dev->mutex);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(spear_kbd_pm_ops, spear_kbd_suspend, spear_kbd_resume);

static struct platform_driver spear_kbd_driver = {
	.probe		= spear_kbd_probe,
	.remove		= __devexit_p(spear_kbd_remove),
	.driver		= {
		.name	= "keyboard",
		.owner	= THIS_MODULE,
		.pm	= &spear_kbd_pm_ops,
	},
};
module_platform_driver(spear_kbd_driver);

MODULE_AUTHOR("Rajeev Kumar");
MODULE_DESCRIPTION("SPEAr Keyboard Driver");
MODULE_LICENSE("GPL");
