/*
 *  quickstart.c - ACPI Direct App Launch driver
 *
 *
 *  Copyright (C) 2007-2010 Angelo Arrifano <miknix@gmail.com>
 *
 *  Information gathered from disassebled dsdt and from here:
 *  <http://www.microsoft.com/whdc/system/platform/firmware/DirAppLaunch.mspx>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define QUICKSTART_VERSION "1.03"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <acpi/acpi_drivers.h>
#include <linux/platform_device.h>
#include <linux/input.h>

MODULE_AUTHOR("Angelo Arrifano");
MODULE_DESCRIPTION("ACPI Direct App Launch driver");
MODULE_LICENSE("GPL");

#define QUICKSTART_ACPI_DEVICE_NAME   "quickstart"
#define QUICKSTART_ACPI_CLASS         "quickstart"
#define QUICKSTART_ACPI_HID           "PNP0C32"

#define QUICKSTART_PF_DRIVER_NAME     "quickstart"
#define QUICKSTART_PF_DEVICE_NAME     "quickstart"
#define QUICKSTART_PF_DEVATTR_NAME    "pressed_button"

#define QUICKSTART_MAX_BTN_NAME_LEN   16

/* There will be two events:
	 * 0x02 - A hot button was pressed while device was off/sleeping.
	 * 0x80 - A hot button was pressed while device was up. */
#define QUICKSTART_EVENT_WAKE         0x02
#define QUICKSTART_EVENT_RUNTIME      0x80

struct quickstart_btn {
	char *name;
	unsigned int id;
	struct quickstart_btn *next;
};

static struct quickstart_driver_data {
	struct quickstart_btn *btn_lst;
	struct quickstart_btn *pressed;
} quickstart_data;

/* ACPI driver Structs */
struct quickstart_acpi {
	struct acpi_device *device;
	struct quickstart_btn *btn;
};
static int quickstart_acpi_add(struct acpi_device *device);
static int quickstart_acpi_remove(struct acpi_device *device, int type);
static const struct acpi_device_id  quickstart_device_ids[] = {
	{QUICKSTART_ACPI_HID, 0},
	{"", 0},
};

static struct acpi_driver quickstart_acpi_driver = {
	.name = "quickstart",
	.class = QUICKSTART_ACPI_CLASS,
	.ids = quickstart_device_ids,
	.ops = {
			.add = quickstart_acpi_add,
			.remove = quickstart_acpi_remove,
		},
};

/* Input device structs */
struct input_dev *quickstart_input;

/* Platform driver structs */
static ssize_t buttons_show(struct device *dev,
					struct device_attribute *attr,
					char *buf);
static ssize_t pressed_button_show(struct device *dev,
					struct device_attribute *attr,
					char *buf);
static ssize_t pressed_button_store(struct device *dev,
					struct device_attribute *attr,
					 const char *buf,
					 size_t count);
static DEVICE_ATTR(pressed_button, 0666, pressed_button_show,
					 pressed_button_store);
static DEVICE_ATTR(buttons, 0444, buttons_show, NULL);
static struct platform_device *pf_device;
static struct platform_driver pf_driver = {
	.driver = {
		.name = QUICKSTART_PF_DRIVER_NAME,
		.owner = THIS_MODULE,
	}
};

/*
 * Platform driver functions
 */
static ssize_t buttons_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int count = 0;
	struct quickstart_btn *ptr = quickstart_data.btn_lst;

	if (!ptr)
		return snprintf(buf, PAGE_SIZE, "none");

	while (ptr && (count < PAGE_SIZE)) {
		if (ptr->name) {
			count += snprintf(buf + count,
					PAGE_SIZE - count,
					"%d\t%s\n", ptr->id, ptr->name);
		}
		ptr = ptr->next;
	}

	return count;
}

static ssize_t pressed_button_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n",
			(quickstart_data.pressed ?
			 quickstart_data.pressed->name : "none"));
}


static ssize_t pressed_button_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	if (count < 2)
		return -EINVAL;

	if (strncasecmp(buf, "none", 4) != 0)
		return -EINVAL;

	quickstart_data.pressed = NULL;
	return count;
}

/* Hotstart Helper functions */
static int quickstart_btnlst_add(struct quickstart_btn **data)
{
	struct quickstart_btn **ptr = &quickstart_data.btn_lst;

	while (*ptr)
		ptr = &((*ptr)->next);

	*ptr = kzalloc(sizeof(struct quickstart_btn), GFP_KERNEL);
	if (!*ptr) {
		*data = NULL;
		return -ENOMEM;
	}
	*data = *ptr;

	return 0;
}

static void quickstart_btnlst_del(struct quickstart_btn *data)
{
	struct quickstart_btn **ptr = &quickstart_data.btn_lst;

	if (!data)
		return;

	while (*ptr) {
		if (*ptr == data) {
			*ptr = (*ptr)->next;
			kfree(data);
			return;
		}
		ptr = &((*ptr)->next);
	}

	return;
}

static void quickstart_btnlst_free(void)
{
	struct quickstart_btn *ptr = quickstart_data.btn_lst;
	struct quickstart_btn *lptr = NULL;

	while (ptr) {
		lptr = ptr;
		ptr = ptr->next;
		kfree(lptr->name);
		kfree(lptr);
	}

	return;
}

/* ACPI Driver functions */
static void quickstart_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	struct quickstart_acpi *quickstart = data;

	if (!quickstart)
		return;

	if (event == QUICKSTART_EVENT_WAKE)
		quickstart_data.pressed = quickstart->btn;
	else if (event == QUICKSTART_EVENT_RUNTIME) {
		input_report_key(quickstart_input, quickstart->btn->id, 1);
		input_sync(quickstart_input);
		input_report_key(quickstart_input, quickstart->btn->id, 0);
		input_sync(quickstart_input);
	}
	return;
}

static void quickstart_acpi_ghid(struct quickstart_acpi *quickstart)
{
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	uint32_t usageid = 0;

	if (!quickstart)
		return;

	/* This returns a buffer telling the button usage ID,
	 * and triggers pending notify events (The ones before booting). */
	status = acpi_evaluate_object(quickstart->device->handle,
					"GHID", NULL, &buffer);
	if (ACPI_FAILURE(status) || !buffer.pointer) {
		printk(KERN_ERR "quickstart: %s GHID method failed.\n",
		       quickstart->btn->name);
		return;
	}

	if (buffer.length < 8)
		return;

	/* <<The GHID method can return a BYTE, WORD, or DWORD.
	 * The value must be encoded in little-endian byte
	 * order (least significant byte first).>> */
	usageid = *((uint32_t *)(buffer.pointer + (buffer.length - 8)));
	quickstart->btn->id = usageid;

	kfree(buffer.pointer);
}

static int quickstart_acpi_config(struct quickstart_acpi *quickstart, char *bid)
{
	int len = strlen(bid);
	int ret;

	/* Add button to list */
	ret = quickstart_btnlst_add(&quickstart->btn);
	if (ret)
		return ret;

	quickstart->btn->name = kzalloc(len + 1, GFP_KERNEL);
	if (!quickstart->btn->name) {
		quickstart_btnlst_free();
		return -ENOMEM;
	}
	strcpy(quickstart->btn->name, bid);

	return 0;
}

static int quickstart_acpi_add(struct acpi_device *device)
{
	int ret = 0;
	acpi_status status = AE_OK;
	struct quickstart_acpi *quickstart = NULL;

	if (!device)
		return -EINVAL;

	quickstart = kzalloc(sizeof(struct quickstart_acpi), GFP_KERNEL);
	if (!quickstart)
		return -ENOMEM;

	quickstart->device = device;
	strcpy(acpi_device_name(device), QUICKSTART_ACPI_DEVICE_NAME);
	strcpy(acpi_device_class(device), QUICKSTART_ACPI_CLASS);
	device->driver_data = quickstart;

	/* Add button to list and initialize some stuff */
	ret = quickstart_acpi_config(quickstart, acpi_device_bid(device));
	if (ret)
		goto fail_config;

	status = acpi_install_notify_handler(device->handle,
						ACPI_ALL_NOTIFY,
						quickstart_acpi_notify,
						quickstart);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR "quickstart: Notify handler install error\n");
		ret = -ENODEV;
		goto fail_installnotify;
	}

	quickstart_acpi_ghid(quickstart);

	return 0;

fail_installnotify:
	quickstart_btnlst_del(quickstart->btn);

fail_config:

	kfree(quickstart);

	return ret;
}

static int quickstart_acpi_remove(struct acpi_device *device, int type)
{
	acpi_status status = 0;
	struct quickstart_acpi *quickstart = NULL;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	quickstart = acpi_driver_data(device);

	status = acpi_remove_notify_handler(device->handle,
						 ACPI_ALL_NOTIFY,
					    quickstart_acpi_notify);
	if (ACPI_FAILURE(status))
		printk(KERN_ERR "quickstart: Error removing notify handler\n");


	kfree(quickstart);

	return 0;
}

/* Module functions */

static void quickstart_exit(void)
{
	input_unregister_device(quickstart_input);

	device_remove_file(&pf_device->dev, &dev_attr_pressed_button);
	device_remove_file(&pf_device->dev, &dev_attr_buttons);

	platform_device_unregister(pf_device);

	platform_driver_unregister(&pf_driver);

	acpi_bus_unregister_driver(&quickstart_acpi_driver);

	quickstart_btnlst_free();

	return;
}

static int __init quickstart_init_input(void)
{
	struct quickstart_btn **ptr = &quickstart_data.btn_lst;
	int count;
	int ret;

	quickstart_input = input_allocate_device();

	if (!quickstart_input)
		return -ENOMEM;

	quickstart_input->name = "Quickstart ACPI Buttons";
	quickstart_input->id.bustype = BUS_HOST;

	while (*ptr) {
		count++;
		set_bit(EV_KEY, quickstart_input->evbit);
		set_bit((*ptr)->id, quickstart_input->keybit);
		ptr = &((*ptr)->next);
	}

	ret = input_register_device(quickstart_input);
	if (ret) {
		input_free_device(quickstart_input);
		return ret;
	}

	return 0;
}

static int __init quickstart_init(void)
{
	int ret;

	/* ACPI Check */
	if (acpi_disabled)
		return -ENODEV;

	/* ACPI driver register */
	ret = acpi_bus_register_driver(&quickstart_acpi_driver);
	if (ret)
		return ret;

	/* If existing bus with no devices */
	if (!quickstart_data.btn_lst) {
		ret = -ENODEV;
		goto fail_pfdrv_reg;
	}

	/* Platform driver register */
	ret = platform_driver_register(&pf_driver);
	if (ret)
		goto fail_pfdrv_reg;

	/* Platform device register */
	pf_device = platform_device_alloc(QUICKSTART_PF_DEVICE_NAME, -1);
	if (!pf_device) {
		ret = -ENOMEM;
		goto fail_pfdev_alloc;
	}
	ret = platform_device_add(pf_device);
	if (ret)
		goto fail_pfdev_add;

	/* Create device sysfs file */
	ret = device_create_file(&pf_device->dev, &dev_attr_pressed_button);
	if (ret)
		goto fail_dev_file;

	ret = device_create_file(&pf_device->dev, &dev_attr_buttons);
	if (ret)
		goto fail_dev_file2;


	/* Input device */
	ret = quickstart_init_input();
	if (ret)
		goto fail_input;

	printk(KERN_INFO "quickstart: ACPI Direct App Launch ver %s\n",
						QUICKSTART_VERSION);

	return 0;
fail_input:
	device_remove_file(&pf_device->dev, &dev_attr_buttons);

fail_dev_file2:
	device_remove_file(&pf_device->dev, &dev_attr_pressed_button);

fail_dev_file:
	platform_device_del(pf_device);

fail_pfdev_add:
	platform_device_put(pf_device);

fail_pfdev_alloc:
	platform_driver_unregister(&pf_driver);

fail_pfdrv_reg:
	acpi_bus_unregister_driver(&quickstart_acpi_driver);

	return ret;
}

module_init(quickstart_init);
module_exit(quickstart_exit);
