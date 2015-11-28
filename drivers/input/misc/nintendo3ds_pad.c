/*
 *  nintendo3ds_pad.c
 *
 *  Copyright (C) 2015 Sergi Granell
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

/* We poll keys - msecs */
#define POLL_INTERVAL_DEFAULT	50

#define HID_PAD_PA   (0x10146000)
#define HID_PAD_SIZE (4)

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

#define BUTTON_PRESSED(b, m) (~(b) & (m))
#define BUTTON_CHANGED(b, o, m) ((b ^ o) & (m))

static const struct {
	unsigned int button;
	unsigned int code;
} button_map[] = {
	{BUTTON_A,	KEY_L},
	{BUTTON_B,	KEY_S},
	{BUTTON_Y,	KEY_BACKSPACE},
	{BUTTON_X,	KEY_SPACE},
	{BUTTON_START,	KEY_ENTER},
	{BUTTON_UP,	KEY_UP},
	{BUTTON_DOWN,	KEY_DOWN},
	{BUTTON_RIGHT,	KEY_RIGHT},
	{BUTTON_LEFT,	KEY_LEFT},
	{BUTTON_L1,	KEY_HOME},
	{BUTTON_R1,	KEY_END}
};

struct nintendo3ds_pad_dev {
	struct input_polled_dev *pdev;
	void __iomem *hid_pad;
	unsigned int old_buttons;
};

static void nintendo3ds_pad_poll(struct input_polled_dev *pdev)
{
	int i;
	unsigned int buttons;
	struct nintendo3ds_pad_dev *n3ds_pad_dev = pdev->private;
	struct input_dev *idev = pdev->input;

	buttons = ioread32(n3ds_pad_dev->hid_pad);

	if (buttons ^ n3ds_pad_dev->old_buttons) {
		for (i = 0; i < sizeof(button_map)/sizeof(*button_map); i++) {
			if (BUTTON_CHANGED(buttons, n3ds_pad_dev->old_buttons,
				button_map[i].button)) {
				input_report_key(idev, button_map[i].code,
					BUTTON_PRESSED(buttons, button_map[i].button));
			}
		}
		input_sync(idev);
	}

	n3ds_pad_dev->old_buttons = buttons;
}

static int nintendo3ds_pad_probe(struct platform_device *plat_dev)
{
	int i, error;
	struct nintendo3ds_pad_dev *n3ds_pad_dev;
	struct input_polled_dev *pdev;
	struct input_dev *idev;
	void *hid_pad;

	n3ds_pad_dev = kzalloc(sizeof(*n3ds_pad_dev), GFP_KERNEL);
	if (!n3ds_pad_dev) {
		error = -ENOMEM;
		goto err_alloc_n3ds_pad_dev;
	}

	/* Try to map HID_PAD */
	if (request_mem_region(HID_PAD_PA, HID_PAD_SIZE, "N3DS_HID_PAD")) {
		hid_pad = ioremap_nocache(HID_PAD_PA, HID_PAD_SIZE);

		printk("HID_PAD mapped to: %p - %p\n", hid_pad,
			hid_pad + HID_PAD_SIZE);
	} else {
		printk("HID_PAD region not available.\n");
		error = -ENOMEM;
		goto err_iomem;
	}

	pdev = input_allocate_polled_device();
	if (!pdev) {
		printk(KERN_ERR "nintendo3ds_pad.c: Not enough memory\n");
		error = -ENOMEM;
		goto err_alloc_pdev;
	}

	pdev->poll = nintendo3ds_pad_poll;
	pdev->poll_interval = POLL_INTERVAL_DEFAULT;
	pdev->private = n3ds_pad_dev;

	idev = pdev->input;
	idev->name = "Nintendo 3DS pad";
	idev->phys = "nintendo3ds/input0";
	idev->id.bustype = BUS_HOST;
	idev->dev.parent = &plat_dev->dev;

	idev->evbit[0] |= BIT(EV_KEY);

	for (i = 0; i < sizeof(button_map)/sizeof(*button_map); i++) {
		set_bit(button_map[i].code, idev->keybit);
	}

	input_set_capability(idev, EV_MSC, MSC_SCAN);

	n3ds_pad_dev->pdev = pdev;
	n3ds_pad_dev->hid_pad = hid_pad;
	n3ds_pad_dev->old_buttons = 0xFFF;

	error = input_register_polled_device(pdev);
	if (error) {
		printk(KERN_ERR "nintendo3ds_pad.c: Failed to register device\n");
		goto err_free_dev;
	}

	return 0;

err_free_dev:
	input_free_polled_device(pdev);
err_alloc_pdev:
	iounmap(hid_pad);
	release_mem_region(HID_PAD_PA, HID_PAD_SIZE);
err_iomem:
	kfree(n3ds_pad_dev);
err_alloc_n3ds_pad_dev:
	return error;
}

static int nintendo3ds_pad_remove(struct platform_device *plat_pdev)
{
	struct nintendo3ds_pad_dev *dev = platform_get_drvdata(plat_pdev);

	input_unregister_polled_device(dev->pdev);
	input_free_polled_device(dev->pdev);

	iounmap(dev->hid_pad);
	release_mem_region(HID_PAD_PA, HID_PAD_SIZE);

	kfree(dev);

	return 0;
}

static const struct of_device_id nintendo3ds_pad_of_match[] = {
	{ .compatible = "arm,nintendo3ds_pad", },
	{},
};
MODULE_DEVICE_TABLE(of, nintendo3ds_pad_of_match);

static struct platform_driver nintendo3ds_pad_driver = {
	.probe	= nintendo3ds_pad_probe,
	.remove	= nintendo3ds_pad_remove,
	.driver	= {
		.name = "nintendo3ds_pad",
		.owner = THIS_MODULE,
		.of_match_table = nintendo3ds_pad_of_match,
	},
};

static int __init nintendo3ds_pad_init_driver(void)
{
	return platform_driver_register(&nintendo3ds_pad_driver);
}

static void __exit nintendo3ds_pad_exit_driver(void)
{
	platform_driver_unregister(&nintendo3ds_pad_driver);
}

module_init(nintendo3ds_pad_init_driver);
module_exit(nintendo3ds_pad_exit_driver);

MODULE_DESCRIPTION("Nintendo 3DS pad driver");
MODULE_AUTHOR("Sergi Granell, <xerpi.g.12@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nintendo3ds_pad");
