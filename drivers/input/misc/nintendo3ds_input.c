/*
 *  nintendo3ds_input.c
 *
 *  Copyright (C) 2015 Sergi Granell
 *  Copyright (C) 2017 Paul LaMendola
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/of.h>
#include <asm/io.h>

#include <mach/platform.h>

/***** Buttons *****/

/* We poll keys - msecs */
#define POLL_INTERVAL_DEFAULT	20

#define BUTTON_A      (1 << 0)
#define BUTTON_B      (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START  (1 << 3)
#define BUTTON_RIGHT  (1 << 4)
#define BUTTON_LEFT   (1 << 5)
#define BUTTON_UP     (1 << 6)
#define BUTTON_DOWN   (1 << 7)
#define BUTTON_R1     (1 << 8)
#define BUTTON_L1     (1 << 9)
#define BUTTON_X      (1 << 10)
#define BUTTON_Y      (1 << 11)

#define BUTTON_HELD(b, m) (~(b) & (m))
#define BUTTON_PRESSED(b, o, m) ((~(b) & (o)) & (m))
#define BUTTON_CHANGED(b, o, m) (((b) ^ (o)) & (m))

struct nintendo3ds_input_dev {
	struct input_polled_dev *pdev;
	void __iomem *hid_input;
	unsigned int old_buttons;
};

struct button_map_t {
	int inbutton;
	int outbutton;
};

static const struct button_map_t button_map[] = {
	{BUTTON_A, BTN_LEFT},
	{BUTTON_X, BTN_MIDDLE},
	{BUTTON_Y, BTN_RIGHT},
	{BUTTON_L1, KEY_BACKSPACE},
	{BUTTON_R1, KEY_SPACE},
	{BUTTON_START, KEY_ENTER},
	{BUTTON_UP, KEY_UP},
	{BUTTON_DOWN, KEY_DOWN},
	{BUTTON_LEFT, KEY_LEFT},
	{BUTTON_RIGHT, KEY_RIGHT}

	/* BUTTON_B -> CTRL+C */
};

#define CHECK_BUTTON(inbutton, outbutton) \
	if (BUTTON_CHANGED(buttons, old_buttons, inbutton)) \
		input_report_key(idev, outbutton, BUTTON_HELD(buttons, inbutton));

static void nintendo3ds_input_poll(struct input_polled_dev *pdev)
{
	struct nintendo3ds_input_dev *n3ds_input_dev = pdev->private;
	struct input_dev *idev = pdev->input;
	unsigned int buttons;
	unsigned int old_buttons;
	int i;

	buttons = ioread32(n3ds_input_dev->hid_input);

	old_buttons = n3ds_input_dev->old_buttons;

	for(i = 0; i < sizeof(button_map) / sizeof(struct button_map_t); i++)
		CHECK_BUTTON(button_map[i].inbutton, button_map[i].outbutton)

	/* CTRL+C */
	if (BUTTON_CHANGED(buttons, old_buttons, BUTTON_B)) {
		input_report_key(idev, KEY_LEFTCTRL, BUTTON_HELD(buttons, BUTTON_B));
		input_report_key(idev, KEY_C, BUTTON_HELD(buttons, BUTTON_B));
	}

	if(buttons != n3ds_input_dev->old_buttons)
		input_sync(idev);

	n3ds_input_dev->old_buttons = buttons;
}

static int nintendo3ds_input_probe(struct platform_device *plat_dev)
{
	int i;
	int error;
	struct nintendo3ds_input_dev *n3ds_input_dev;
	struct input_polled_dev *pdev;
	struct input_dev *idev;
	void *hid_input;

	n3ds_input_dev = kzalloc(sizeof(*n3ds_input_dev), GFP_KERNEL);
	if (!n3ds_input_dev) {
		error = -ENOMEM;
		goto err_alloc_n3ds_input_dev;
	}

	/* Try to map HID_input */
	if (request_mem_region(NINTENDO3DS_REG_HID, NINTENDO3DS_REG_HID_SIZE, "N3DS_HID_INPUT")) {
		hid_input = ioremap_nocache(NINTENDO3DS_REG_HID, NINTENDO3DS_REG_HID_SIZE);

		printk("HID_INPUT mapped to: %p - %p\n", hid_input,
			hid_input + NINTENDO3DS_REG_HID_SIZE);
	} else {
		printk("HID_INPUT region not available.\n");
		error = -ENOMEM;
		goto err_hidmem;
	}

	pdev = input_allocate_polled_device();
	if (!pdev) {
		printk(KERN_ERR "nintendo3ds_input.c: Not enough memory\n");
		error = -ENOMEM;
		goto err_alloc_pdev;
	}

	pdev->poll = nintendo3ds_input_poll;
	pdev->poll_interval = POLL_INTERVAL_DEFAULT;
	pdev->private = n3ds_input_dev;

	idev = pdev->input;
	idev->name = "Nintendo 3DS input";
	idev->phys = "nintendo3ds/input0";
	idev->id.bustype = BUS_HOST;
	idev->dev.parent = &plat_dev->dev;

	set_bit(EV_KEY, idev->evbit);
	for(i = 0; i < sizeof(button_map) / sizeof(struct button_map_t); i++)
		set_bit(button_map[i].outbutton, idev->keybit);

	/* CTRL+C */
	set_bit(KEY_LEFTCTRL, idev->keybit);
	set_bit(KEY_C, idev->keybit);

	input_set_capability(idev, EV_MSC, MSC_SCAN);

	n3ds_input_dev->pdev = pdev;
	n3ds_input_dev->hid_input = hid_input;

	error = input_register_polled_device(pdev);
	if (error) {
		printk(KERN_ERR "nintendo3ds_input.c: Failed to register device\n");
		goto err_free_dev;
	}

	return 0;

err_free_dev:
	input_free_polled_device(pdev);
err_alloc_pdev:
	iounmap(hid_input);
	release_mem_region(NINTENDO3DS_REG_HID, NINTENDO3DS_REG_HID_SIZE);
err_hidmem:
	kfree(n3ds_input_dev);
err_alloc_n3ds_input_dev:
	return error;
}

static int nintendo3ds_input_remove(struct platform_device *plat_pdev)
{
	struct nintendo3ds_input_dev *dev = platform_get_drvdata(plat_pdev);

	input_unregister_polled_device(dev->pdev);
	input_free_polled_device(dev->pdev);

	iounmap(dev->hid_input);
	release_mem_region(NINTENDO3DS_REG_HID, NINTENDO3DS_REG_HID_SIZE);

	kfree(dev);

	return 0;
}

static const struct of_device_id nintendo3ds_input_of_match[] = {
	{ .compatible = "nintendo3ds-input", },
	{},
};
MODULE_DEVICE_TABLE(of, nintendo3ds_input_of_match);

static struct platform_driver nintendo3ds_input_driver = {
	.probe	= nintendo3ds_input_probe,
	.remove	= nintendo3ds_input_remove,
	.driver	= {
		.name = "nintendo3ds-input",
		.owner = THIS_MODULE,
		.of_match_table = nintendo3ds_input_of_match,
	},
};

static int __init nintendo3ds_input_init_driver(void)
{
	return platform_driver_register(&nintendo3ds_input_driver);
}

static void __exit nintendo3ds_input_exit_driver(void)
{
	platform_driver_unregister(&nintendo3ds_input_driver);
}

module_init(nintendo3ds_input_init_driver);
module_exit(nintendo3ds_input_exit_driver);

MODULE_DESCRIPTION("Nintendo 3DS input driver");
MODULE_AUTHOR("Sergi Granell, <xerpi.g.12@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nintendo3ds-input");

