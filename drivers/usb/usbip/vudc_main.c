// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2015 Karol Kosik <karo9@interia.eu>
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *               Krzysztof Opasiak <k.opasiak@samsung.com>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>

#include "vudc.h"

static unsigned int vudc_number = 1;

module_param_named(num, vudc_number, uint, S_IRUGO);
MODULE_PARM_DESC(num, "number of emulated controllers");

static struct platform_driver vudc_driver = {
	.probe		= vudc_probe,
	.remove		= vudc_remove,
	.driver		= {
		.name	= GADGET_NAME,
	},
};

static struct list_head vudc_devices = LIST_HEAD_INIT(vudc_devices);

static int __init init(void)
{
	int retval = -ENOMEM;
	int i;
	struct vudc_device *udc_dev = NULL, *udc_dev2 = NULL;

	if (usb_disabled())
		return -ENODEV;

	if (vudc_number < 1) {
		pr_err("Number of emulated UDC must be no less than 1");
		return -EINVAL;
	}

	retval = platform_driver_register(&vudc_driver);
	if (retval < 0)
		goto out;

	for (i = 0; i < vudc_number; i++) {
		udc_dev = alloc_vudc_device(i);
		if (!udc_dev) {
			retval = -ENOMEM;
			goto cleanup;
		}

		retval = platform_device_add(udc_dev->pdev);
		if (retval < 0) {
			put_vudc_device(udc_dev);
			goto cleanup;
		}

		list_add_tail(&udc_dev->dev_entry, &vudc_devices);
		if (!platform_get_drvdata(udc_dev->pdev)) {
			/*
			 * The udc was added successfully but its probe
			 * function failed for some reason.
			 */
			retval = -EINVAL;
			goto cleanup;
		}
	}
	goto out;

cleanup:
	list_for_each_entry_safe(udc_dev, udc_dev2, &vudc_devices, dev_entry) {
		list_del(&udc_dev->dev_entry);
		platform_device_del(udc_dev->pdev);
		put_vudc_device(udc_dev);
	}

	platform_driver_unregister(&vudc_driver);
out:
	return retval;
}
module_init(init);

static void __exit cleanup(void)
{
	struct vudc_device *udc_dev = NULL, *udc_dev2 = NULL;

	list_for_each_entry_safe(udc_dev, udc_dev2, &vudc_devices, dev_entry) {
		list_del(&udc_dev->dev_entry);
		platform_device_unregister(udc_dev->pdev);
		put_vudc_device(udc_dev);
	}
	platform_driver_unregister(&vudc_driver);
}
module_exit(cleanup);

MODULE_DESCRIPTION("USB over IP Device Controller");
MODULE_AUTHOR("Krzysztof Opasiak, Karol Kosik, Igor Kotrasinski");
MODULE_LICENSE("GPL");
