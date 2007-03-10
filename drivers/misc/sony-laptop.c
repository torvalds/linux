/*
 * ACPI Sony Notebook Control Driver (SNC)
 *
 * Copyright (C) 2004-2005 Stelian Pop <stelian@popies.net>
 * Copyright (C) 2007 Mattia Dongili <malattia@linux.it>
 *
 * Parts of this driver inspired from asus_acpi.c and ibm_acpi.c
 * which are copyrighted by their respective authors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <asm/uaccess.h>

#define ACPI_SNC_CLASS		"sony"
#define ACPI_SNC_HID		"SNY5001"
#define ACPI_SNC_DRIVER_NAME	"ACPI Sony Notebook Control Driver v0.4"

/* the device uses 1-based values, while the backlight subsystem uses
   0-based values */
#define SONY_MAX_BRIGHTNESS	8

#define LOG_PFX			KERN_WARNING "sony-laptop: "

MODULE_AUTHOR("Stelian Pop, Mattia Dongili");
MODULE_DESCRIPTION(ACPI_SNC_DRIVER_NAME);
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "set this to 1 (and RTFM) if you want to help "
		 "the development of this driver");

static ssize_t sony_acpi_show(struct device *, struct device_attribute *,
			      char *);
static ssize_t sony_acpi_store(struct device *, struct device_attribute *,
			       const char *, size_t);
static int boolean_validate(const int, const int);
static int brightness_default_validate(const int, const int);

#define SNC_VALIDATE_IN		0
#define SNC_VALIDATE_OUT	1

struct sony_acpi_value {
	char *name;		/* name of the entry */
	char **acpiget;		/* names of the ACPI get function */
	char **acpiset;		/* names of the ACPI set function */
	int (*validate)(const int, const int);	/* input/output validation */
	int value;		/* current setting */
	int valid;		/* Has ever been set */
	int debug;		/* active only in debug mode ? */
	struct device_attribute devattr;	/* sysfs atribute */
};

#define HANDLE_NAMES(_name, _values...) \
	static char *snc_##_name[] = { _values, NULL }

#define SONY_ACPI_VALUE(_name, _getters, _setters, _validate, _debug) \
	{ \
		.name		= __stringify(_name), \
		.acpiget	= _getters, \
		.acpiset	= _setters, \
		.validate	= _validate, \
		.debug		= _debug, \
		.devattr	= __ATTR(_name, 0, sony_acpi_show, sony_acpi_store), \
	}

#define SONY_ACPI_VALUE_NULL	{ .name = NULL }

HANDLE_NAMES(fnkey_get, "GHKE");

HANDLE_NAMES(brightness_def_get, "GPBR");
HANDLE_NAMES(brightness_def_set, "SPBR");

HANDLE_NAMES(cdpower_get, "GCDP");
HANDLE_NAMES(cdpower_set, "SCDP", "CDPW");

HANDLE_NAMES(audiopower_get, "GAZP");
HANDLE_NAMES(audiopower_set, "AZPW");

HANDLE_NAMES(lanpower_get, "GLNP");
HANDLE_NAMES(lanpower_set, "LNPW");

HANDLE_NAMES(PID_get, "GPID");

HANDLE_NAMES(CTR_get, "GCTR");
HANDLE_NAMES(CTR_set, "SCTR");

HANDLE_NAMES(PCR_get, "GPCR");
HANDLE_NAMES(PCR_set, "SPCR");

HANDLE_NAMES(CMI_get, "GCMI");
HANDLE_NAMES(CMI_set, "SCMI");

static struct sony_acpi_value sony_acpi_values[] = {
	SONY_ACPI_VALUE(brightness_default, snc_brightness_def_get,
			snc_brightness_def_set, brightness_default_validate, 0),
	SONY_ACPI_VALUE(fnkey, snc_fnkey_get, NULL, NULL, 0),
	SONY_ACPI_VALUE(cdpower, snc_cdpower_get, snc_cdpower_set, boolean_validate, 0),
	SONY_ACPI_VALUE(audiopower, snc_audiopower_get, snc_audiopower_set,
			boolean_validate, 0),
	SONY_ACPI_VALUE(lanpower, snc_lanpower_get, snc_lanpower_set,
			boolean_validate, 1),
	/* unknown methods */
	SONY_ACPI_VALUE(PID, snc_PID_get, NULL, NULL, 1),
	SONY_ACPI_VALUE(CTR, snc_CTR_get, snc_CTR_set, NULL, 1),
	SONY_ACPI_VALUE(PCR, snc_PCR_get, snc_PCR_set, NULL, 1),
	SONY_ACPI_VALUE(CMI, snc_CMI_get, snc_CMI_set, NULL, 1),
	SONY_ACPI_VALUE_NULL
};

static acpi_handle sony_acpi_handle;
static struct acpi_device *sony_acpi_acpi_device = NULL;

/*
 * acpi_evaluate_object wrappers
 */
static int acpi_callgetfunc(acpi_handle handle, char *name, int *result)
{
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, name, NULL, &output);
	if ((status == AE_OK) && (out_obj.type == ACPI_TYPE_INTEGER)) {
		*result = out_obj.integer.value;
		return 0;
	}

	printk(LOG_PFX "acpi_callreadfunc failed\n");

	return -1;
}

static int acpi_callsetfunc(acpi_handle handle, char *name, int value,
			    int *result)
{
	struct acpi_object_list params;
	union acpi_object in_obj;
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = value;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, name, &params, &output);
	if (status == AE_OK) {
		if (result != NULL) {
			if (out_obj.type != ACPI_TYPE_INTEGER) {
				printk(LOG_PFX "acpi_evaluate_object bad "
				       "return type\n");
				return -1;
			}
			*result = out_obj.integer.value;
		}
		return 0;
	}

	printk(LOG_PFX "acpi_evaluate_object failed\n");

	return -1;
}

/*
 * sony_acpi_values input/output validate functions
 */

/* brightness_default_validate:
 *
 * manipulate input output values to keep consistency with the
 * backlight framework for which brightness values are 0-based.
 */
static int brightness_default_validate(const int direction, const int value)
{
	switch (direction) {
		case SNC_VALIDATE_OUT:
			return value - 1;
		case SNC_VALIDATE_IN:
			if (value >= 0 && value < SONY_MAX_BRIGHTNESS)
				return value + 1;
	}
	return -EINVAL;
}

/* boolean_validate:
 *
 * on input validate boolean values 0/1, on output just pass the
 * received value.
 */
static int boolean_validate(const int direction, const int value)
{
	if (direction == SNC_VALIDATE_IN) {
		if (value != 0 && value != 1)
			return -EINVAL;
	}
	return value;
}

/*
 * Sysfs show/store common to all sony_acpi_values
 */
static ssize_t sony_acpi_show(struct device *dev, struct device_attribute *attr,
			      char *buffer)
{
	int value;
	struct sony_acpi_value *item =
	    container_of(attr, struct sony_acpi_value, devattr);

	if (!*item->acpiget)
		return -EIO;

	if (acpi_callgetfunc(sony_acpi_handle, *item->acpiget, &value) < 0)
		return -EIO;

	if (item->validate)
		value = item->validate(SNC_VALIDATE_OUT, value);

	return snprintf(buffer, PAGE_SIZE, "%d\n", value);
}

static ssize_t sony_acpi_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buffer, size_t count)
{
	int value;
	struct sony_acpi_value *item =
	    container_of(attr, struct sony_acpi_value, devattr);

	if (!item->acpiset)
		return -EIO;

	if (count > 31)
		return -EINVAL;

	value = simple_strtoul(buffer, NULL, 10);

	if (item->validate)
		value = item->validate(SNC_VALIDATE_IN, value);

	if (value < 0)
		return value;

	if (acpi_callsetfunc(sony_acpi_handle, *item->acpiset, value, NULL) < 0)
		return -EIO;
	item->value = value;
	item->valid = 1;
	return count;
}

/*
 * Platform device
 */
static struct platform_driver sncpf_driver = {
	.driver = {
		   .name = "sony-laptop",
		   .owner = THIS_MODULE,
		   }
};
static struct platform_device *sncpf_device;

static int sony_snc_pf_add(void)
{
	acpi_handle handle;
	struct sony_acpi_value *item;
	int ret = 0;

	ret = platform_driver_register(&sncpf_driver);
	if (ret)
		goto out;

	sncpf_device = platform_device_alloc("sony-laptop", -1);
	if (!sncpf_device) {
		ret = -ENOMEM;
		goto out_platform_registered;
	}

	ret = platform_device_add(sncpf_device);
	if (ret)
		goto out_platform_alloced;

	for (item = sony_acpi_values; item->name; ++item) {

		if (!debug && item->debug)
			continue;

		/* find the available acpiget as described in the DSDT */
		for (; item->acpiget && *item->acpiget; ++item->acpiget) {
			if (ACPI_SUCCESS(acpi_get_handle(sony_acpi_handle,
							 *item->acpiget,
							 &handle))) {
				if (debug)
					printk(LOG_PFX "Found %s getter: %s\n",
					       item->name, *item->acpiget);
				item->devattr.attr.mode |= S_IRUGO;
				break;
			}
		}

		/* find the available acpiset as described in the DSDT */
		for (; item->acpiset && *item->acpiset; ++item->acpiset) {
			if (ACPI_SUCCESS(acpi_get_handle(sony_acpi_handle,
							 *item->acpiset,
							 &handle))) {
				if (debug)
					printk(LOG_PFX "Found %s setter: %s\n",
					       item->name, *item->acpiset);
				item->devattr.attr.mode |= S_IWUSR;
				break;
			}
		}

		if (item->devattr.attr.mode != 0) {
			ret =
			    device_create_file(&sncpf_device->dev,
					       &item->devattr);
			if (ret)
				goto out_sysfs;
		}
	}

	return 0;

      out_sysfs:
	for (item = sony_acpi_values; item->name; ++item) {
		device_remove_file(&sncpf_device->dev, &item->devattr);
	}
	platform_device_del(sncpf_device);
      out_platform_alloced:
	platform_device_put(sncpf_device);
      out_platform_registered:
	platform_driver_unregister(&sncpf_driver);
      out:
	return ret;
}

static void sony_snc_pf_remove(void)
{
	struct sony_acpi_value *item;

	for (item = sony_acpi_values; item->name; ++item) {
		device_remove_file(&sncpf_device->dev, &item->devattr);
	}

	platform_device_del(sncpf_device);
	platform_device_put(sncpf_device);
	platform_driver_unregister(&sncpf_driver);
}

/*
 * Backlight device
 */
static int sony_backlight_update_status(struct backlight_device *bd)
{
	return acpi_callsetfunc(sony_acpi_handle, "SBRT",
				bd->props.brightness + 1, NULL);
}

static int sony_backlight_get_brightness(struct backlight_device *bd)
{
	int value;

	if (acpi_callgetfunc(sony_acpi_handle, "GBRT", &value))
		return 0;
	/* brightness levels are 1-based, while backlight ones are 0-based */
	return value - 1;
}

static struct backlight_device *sony_backlight_device;
static struct backlight_ops sony_backlight_ops = {
	.update_status = sony_backlight_update_status,
	.get_brightness = sony_backlight_get_brightness,
};

/*
 * ACPI callbacks
 */
static void sony_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	if (debug)
		printk(LOG_PFX "sony_acpi_notify, event: %d\n", event);
	acpi_bus_generate_event(sony_acpi_acpi_device, 1, event);
}

static acpi_status sony_walk_callback(acpi_handle handle, u32 level,
				      void *context, void **return_value)
{
	struct acpi_namespace_node *node;
	union acpi_operand_object *operand;

	node = (struct acpi_namespace_node *)handle;
	operand = (union acpi_operand_object *)node->object;

	printk(LOG_PFX "method: name: %4.4s, args %X\n", node->name.ascii,
	       (u32) operand->method.param_count);

	return AE_OK;
}

/*
 * ACPI device
 */
static int sony_acpi_resume(struct acpi_device *device)
{
	struct sony_acpi_value *item;

	for (item = sony_acpi_values; item->name; item++) {
		int ret;

		if (!item->valid)
			continue;
		ret = acpi_callsetfunc(sony_acpi_handle, *item->acpiset,
				       item->value, NULL);
		if (ret < 0) {
			printk("%s: %d\n", __FUNCTION__, ret);
			break;
		}
	}
	return 0;
}

static int sony_acpi_add(struct acpi_device *device)
{
	acpi_status status;
	int result = 0;
	acpi_handle handle;

	sony_acpi_acpi_device = device;

	sony_acpi_handle = device->handle;

	if (debug) {
		status = acpi_walk_namespace(ACPI_TYPE_METHOD, sony_acpi_handle,
					     1, sony_walk_callback, NULL, NULL);
		if (ACPI_FAILURE(status)) {
			printk(LOG_PFX "unable to walk acpi resources\n");
			result = -ENODEV;
			goto outwalk;
		}
	}

	status = acpi_install_notify_handler(sony_acpi_handle,
					     ACPI_DEVICE_NOTIFY,
					     sony_acpi_notify, NULL);
	if (ACPI_FAILURE(status)) {
		printk(LOG_PFX "unable to install notify handler\n");
		result = -ENODEV;
		goto outwalk;
	}

	if (ACPI_SUCCESS(acpi_get_handle(sony_acpi_handle, "GBRT", &handle))) {
		sony_backlight_device = backlight_device_register("sony", NULL,
								  NULL,
								  &sony_backlight_ops);

		if (IS_ERR(sony_backlight_device)) {
			printk(LOG_PFX "unable to register backlight device\n");
			sony_backlight_device = NULL;
		} else {
			sony_backlight_device->props.brightness =
			    sony_backlight_get_brightness
			    (sony_backlight_device);
			sony_backlight_device->props.max_brightness = 
			    SONY_MAX_BRIGHTNESS - 1;
		}

	}

	if (sony_snc_pf_add())
		goto outbacklight;

	printk(KERN_INFO ACPI_SNC_DRIVER_NAME " successfully installed\n");

	return 0;

      outbacklight:
	if (sony_backlight_device)
		backlight_device_unregister(sony_backlight_device);

	status = acpi_remove_notify_handler(sony_acpi_handle,
					    ACPI_DEVICE_NOTIFY,
					    sony_acpi_notify);
	if (ACPI_FAILURE(status))
		printk(LOG_PFX "unable to remove notify handler\n");
      outwalk:
	return result;
}

static int sony_acpi_remove(struct acpi_device *device, int type)
{
	acpi_status status;

	if (sony_backlight_device)
		backlight_device_unregister(sony_backlight_device);

	sony_acpi_acpi_device = NULL;

	status = acpi_remove_notify_handler(sony_acpi_handle,
					    ACPI_DEVICE_NOTIFY,
					    sony_acpi_notify);
	if (ACPI_FAILURE(status))
		printk(LOG_PFX "unable to remove notify handler\n");

	sony_snc_pf_remove();

	printk(KERN_INFO ACPI_SNC_DRIVER_NAME " successfully removed\n");

	return 0;
}

static struct acpi_driver sony_acpi_driver = {
	.name = ACPI_SNC_DRIVER_NAME,
	.class = ACPI_SNC_CLASS,
	.ids = ACPI_SNC_HID,
	.ops = {
		.add = sony_acpi_add,
		.remove = sony_acpi_remove,
		.resume = sony_acpi_resume,
		},
};

static int __init sony_acpi_init(void)
{
	return acpi_bus_register_driver(&sony_acpi_driver);
}

static void __exit sony_acpi_exit(void)
{
	acpi_bus_unregister_driver(&sony_acpi_driver);
}

module_init(sony_acpi_init);
module_exit(sony_acpi_exit);
