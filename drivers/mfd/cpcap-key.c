/*
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/spi/cpcap.h>
#include <linux/spi/cpcap-regbits.h>



struct cpcap_key_data {
	struct input_dev *input_dev;
	struct cpcap_device *cpcap;
};

static int __init cpcap_key_probe(struct platform_device *pdev)
{
	int err;
	struct cpcap_key_data *key;

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "no platform_data\n");
		return -EINVAL;
	}

	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	key->cpcap = pdev->dev.platform_data;

	key->input_dev = input_allocate_device();
	if (key->input_dev == NULL) {
		dev_err(&pdev->dev, "can't allocate input device\n");
		err = -ENOMEM;
		goto err0;
	}

	set_bit(EV_KEY, key->input_dev->evbit);
	set_bit(KEY_MEDIA, key->input_dev->keybit);
	set_bit(KEY_END, key->input_dev->keybit);

	key->input_dev->name = "cpcap-key";

	err = input_register_device(key->input_dev);
	if (err < 0) {
		dev_err(&pdev->dev, "could not register input device.\n");
		goto err1;
	}

	platform_set_drvdata(pdev, key);
	cpcap_set_keydata(key->cpcap, key);

	dev_info(&pdev->dev, "CPCAP key device probed\n");

	return 0;

err1:
	input_free_device(key->input_dev);
err0:
	kfree(key);
	return err;
}

static int __exit cpcap_key_remove(struct platform_device *pdev)
{
	struct cpcap_key_data *key = platform_get_drvdata(pdev);

	input_unregister_device(key->input_dev);
	input_free_device(key->input_dev);
	kfree(key);

	return 0;
}

void cpcap_broadcast_key_event(struct cpcap_device *cpcap,
			       unsigned int code, int value)
{
	struct cpcap_key_data *key = cpcap_get_keydata(cpcap);

	if (key && key->input_dev)
		input_report_key(key->input_dev, code, value);
}
EXPORT_SYMBOL(cpcap_broadcast_key_event);

static struct platform_driver cpcap_key_driver = {
	.probe		= cpcap_key_probe,
	.remove		= __exit_p(cpcap_key_remove),
	.driver		= {
		.name	= "cpcap_key",
		.owner	= THIS_MODULE,
	},
};

static int __init cpcap_key_init(void)
{
	return platform_driver_register(&cpcap_key_driver);
}
module_init(cpcap_key_init);

static void __exit cpcap_key_exit(void)
{
	platform_driver_unregister(&cpcap_key_driver);
}
module_exit(cpcap_key_exit);

MODULE_ALIAS("platform:cpcap_key");
MODULE_DESCRIPTION("CPCAP KEY driver");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
