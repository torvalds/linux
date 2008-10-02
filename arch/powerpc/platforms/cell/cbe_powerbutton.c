/*
 * driver for powerbutton on IBM cell blades
 *
 * (C) Copyright IBM Corp. 2005-2008
 *
 * Author: Christian Krafft <krafft@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/input.h>
#include <linux/platform_device.h>
#include <asm/pmi.h>
#include <asm/prom.h>

static struct input_dev *button_dev;
static struct platform_device *button_pdev;

static void cbe_powerbutton_handle_pmi(pmi_message_t pmi_msg)
{
	BUG_ON(pmi_msg.type != PMI_TYPE_POWER_BUTTON);

	input_report_key(button_dev, KEY_POWER, 1);
	input_sync(button_dev);
	input_report_key(button_dev, KEY_POWER, 0);
	input_sync(button_dev);
}

static struct pmi_handler cbe_pmi_handler = {
	.type			= PMI_TYPE_POWER_BUTTON,
	.handle_pmi_message	= cbe_powerbutton_handle_pmi,
};

static int __init cbe_powerbutton_init(void)
{
	int ret = 0;
	struct input_dev *dev;

	if (!machine_is_compatible("IBM,CBPLUS-1.0")) {
		printk(KERN_ERR "%s: Not a cell blade.\n", __func__);
		ret = -ENODEV;
		goto out;
	}

	dev = input_allocate_device();
	if (!dev) {
		ret = -ENOMEM;
		printk(KERN_ERR "%s: Not enough memory.\n", __func__);
		goto out;
	}

	set_bit(EV_KEY, dev->evbit);
	set_bit(KEY_POWER, dev->keybit);

	dev->name = "Power Button";
	dev->id.bustype = BUS_HOST;

	/* this makes the button look like an acpi power button
	 * no clue whether anyone relies on that though */
	dev->id.product = 0x02;
	dev->phys = "LNXPWRBN/button/input0";

	button_pdev = platform_device_register_simple("power_button", 0, NULL, 0);
	if (IS_ERR(button_pdev)) {
		ret = PTR_ERR(button_pdev);
		goto out_free_input;
	}

	dev->dev.parent = &button_pdev->dev;
	ret = input_register_device(dev);
	if (ret) {
		printk(KERN_ERR "%s: Failed to register device\n", __func__);
		goto out_free_pdev;
	}

	button_dev = dev;

	ret = pmi_register_handler(&cbe_pmi_handler);
	if (ret) {
		printk(KERN_ERR "%s: Failed to register with pmi.\n", __func__);
		goto out_free_pdev;
	}

	goto out;

out_free_pdev:
	platform_device_unregister(button_pdev);
out_free_input:
	input_free_device(dev);
out:
	return ret;
}

static void __exit cbe_powerbutton_exit(void)
{
	pmi_unregister_handler(&cbe_pmi_handler);
	platform_device_unregister(button_pdev);
	input_free_device(button_dev);
}

module_init(cbe_powerbutton_init);
module_exit(cbe_powerbutton_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Krafft <krafft@de.ibm.com>");
