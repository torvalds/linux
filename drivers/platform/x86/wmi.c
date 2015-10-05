/*
 *  ACPI-WMI mapping driver
 *
 *  Copyright (C) 2007-2008 Carlos Corbacho <carlos@strangeworlds.co.uk>
 *
 *  GUID parsing code from ldm.c is:
 *   Copyright (C) 2001,2002 Richard Russon <ldm@flatcap.org>
 *   Copyright (c) 2001-2007 Anton Altaparmakov
 *   Copyright (C) 2001,2002 Jakob Kemi <jakob.kemi@telia.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <linux/module.h>

ACPI_MODULE_NAME("wmi");
MODULE_AUTHOR("Carlos Corbacho");
MODULE_DESCRIPTION("ACPI-WMI Mapping Driver");
MODULE_LICENSE("GPL");

#define ACPI_WMI_CLASS "wmi"

static LIST_HEAD(wmi_block_list);

struct guid_block {
	char guid[16];
	union {
		char object_id[2];
		struct {
			unsigned char notify_id;
			unsigned char reserved;
		};
	};
	u8 instance_count;
	u8 flags;
};

struct wmi_block {
	struct list_head list;
	struct guid_block gblock;
	acpi_handle handle;
	wmi_notify_handler handler;
	void *handler_data;
	struct device dev;
};


/*
 * If the GUID data block is marked as expensive, we must enable and
 * explicitily disable data collection.
 */
#define ACPI_WMI_EXPENSIVE   0x1
#define ACPI_WMI_METHOD      0x2	/* GUID is a method */
#define ACPI_WMI_STRING      0x4	/* GUID takes & returns a string */
#define ACPI_WMI_EVENT       0x8	/* GUID is an event */

static bool debug_event;
module_param(debug_event, bool, 0444);
MODULE_PARM_DESC(debug_event,
		 "Log WMI Events [0/1]");

static bool debug_dump_wdg;
module_param(debug_dump_wdg, bool, 0444);
MODULE_PARM_DESC(debug_dump_wdg,
		 "Dump available WMI interfaces [0/1]");

static int acpi_wmi_remove(struct acpi_device *device);
static int acpi_wmi_add(struct acpi_device *device);
static void acpi_wmi_notify(struct acpi_device *device, u32 event);

static const struct acpi_device_id wmi_device_ids[] = {
	{"PNP0C14", 0},
	{"pnp0c14", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, wmi_device_ids);

static struct acpi_driver acpi_wmi_driver = {
	.name = "wmi",
	.class = ACPI_WMI_CLASS,
	.ids = wmi_device_ids,
	.ops = {
		.add = acpi_wmi_add,
		.remove = acpi_wmi_remove,
		.notify = acpi_wmi_notify,
	},
};

/*
 * GUID parsing functions
 */

/**
 * wmi_parse_hexbyte - Convert a ASCII hex number to a byte
 * @src:  Pointer to at least 2 characters to convert.
 *
 * Convert a two character ASCII hex string to a number.
 *
 * Return:  0-255  Success, the byte was parsed correctly
 *          -1     Error, an invalid character was supplied
 */
static int wmi_parse_hexbyte(const u8 *src)
{
	int h;
	int value;

	/* high part */
	h = value = hex_to_bin(src[0]);
	if (value < 0)
		return -1;

	/* low part */
	value = hex_to_bin(src[1]);
	if (value >= 0)
		return (h << 4) | value;
	return -1;
}

/**
 * wmi_swap_bytes - Rearrange GUID bytes to match GUID binary
 * @src:   Memory block holding binary GUID (16 bytes)
 * @dest:  Memory block to hold byte swapped binary GUID (16 bytes)
 *
 * Byte swap a binary GUID to match it's real GUID value
 */
static void wmi_swap_bytes(u8 *src, u8 *dest)
{
	int i;

	for (i = 0; i <= 3; i++)
		memcpy(dest + i, src + (3 - i), 1);

	for (i = 0; i <= 1; i++)
		memcpy(dest + 4 + i, src + (5 - i), 1);

	for (i = 0; i <= 1; i++)
		memcpy(dest + 6 + i, src + (7 - i), 1);

	memcpy(dest + 8, src + 8, 8);
}

/**
 * wmi_parse_guid - Convert GUID from ASCII to binary
 * @src:   36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @dest:  Memory block to hold binary GUID (16 bytes)
 *
 * N.B. The GUID need not be NULL terminated.
 *
 * Return:  'true'   @dest contains binary GUID
 *          'false'  @dest contents are undefined
 */
static bool wmi_parse_guid(const u8 *src, u8 *dest)
{
	static const int size[] = { 4, 2, 2, 2, 6 };
	int i, j, v;

	if (src[8]  != '-' || src[13] != '-' ||
		src[18] != '-' || src[23] != '-')
		return false;

	for (j = 0; j < 5; j++, src++) {
		for (i = 0; i < size[j]; i++, src += 2, *dest++ = v) {
			v = wmi_parse_hexbyte(src);
			if (v < 0)
				return false;
		}
	}

	return true;
}

static bool find_guid(const char *guid_string, struct wmi_block **out)
{
	char tmp[16], guid_input[16];
	struct wmi_block *wblock;
	struct guid_block *block;
	struct list_head *p;

	wmi_parse_guid(guid_string, tmp);
	wmi_swap_bytes(tmp, guid_input);

	list_for_each(p, &wmi_block_list) {
		wblock = list_entry(p, struct wmi_block, list);
		block = &wblock->gblock;

		if (memcmp(block->guid, guid_input, 16) == 0) {
			if (out)
				*out = wblock;
			return true;
		}
	}
	return false;
}

static acpi_status wmi_method_enable(struct wmi_block *wblock, int enable)
{
	struct guid_block *block = NULL;
	char method[5];
	acpi_status status;
	acpi_handle handle;

	block = &wblock->gblock;
	handle = wblock->handle;

	snprintf(method, 5, "WE%02X", block->notify_id);
	status = acpi_execute_simple_method(handle, method, enable);

	if (status != AE_OK && status != AE_NOT_FOUND)
		return status;
	else
		return AE_OK;
}

/*
 * Exported WMI functions
 */
/**
 * wmi_evaluate_method - Evaluate a WMI method
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @instance: Instance index
 * @method_id: Method ID to call
 * &in: Buffer containing input for the method call
 * &out: Empty buffer to return the method results
 *
 * Call an ACPI-WMI method
 */
acpi_status wmi_evaluate_method(const char *guid_string, u8 instance,
u32 method_id, const struct acpi_buffer *in, struct acpi_buffer *out)
{
	struct guid_block *block = NULL;
	struct wmi_block *wblock = NULL;
	acpi_handle handle;
	acpi_status status;
	struct acpi_object_list input;
	union acpi_object params[3];
	char method[5] = "WM";

	if (!find_guid(guid_string, &wblock))
		return AE_ERROR;

	block = &wblock->gblock;
	handle = wblock->handle;

	if (!(block->flags & ACPI_WMI_METHOD))
		return AE_BAD_DATA;

	if (block->instance_count < instance)
		return AE_BAD_PARAMETER;

	input.count = 2;
	input.pointer = params;
	params[0].type = ACPI_TYPE_INTEGER;
	params[0].integer.value = instance;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = method_id;

	if (in) {
		input.count = 3;

		if (block->flags & ACPI_WMI_STRING) {
			params[2].type = ACPI_TYPE_STRING;
		} else {
			params[2].type = ACPI_TYPE_BUFFER;
		}
		params[2].buffer.length = in->length;
		params[2].buffer.pointer = in->pointer;
	}

	strncat(method, block->object_id, 2);

	status = acpi_evaluate_object(handle, method, &input, out);

	return status;
}
EXPORT_SYMBOL_GPL(wmi_evaluate_method);

/**
 * wmi_query_block - Return contents of a WMI block
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @instance: Instance index
 * &out: Empty buffer to return the contents of the data block to
 *
 * Return the contents of an ACPI-WMI data block to a buffer
 */
acpi_status wmi_query_block(const char *guid_string, u8 instance,
struct acpi_buffer *out)
{
	struct guid_block *block = NULL;
	struct wmi_block *wblock = NULL;
	acpi_handle handle;
	acpi_status status, wc_status = AE_ERROR;
	struct acpi_object_list input;
	union acpi_object wq_params[1];
	char method[5];
	char wc_method[5] = "WC";

	if (!guid_string || !out)
		return AE_BAD_PARAMETER;

	if (!find_guid(guid_string, &wblock))
		return AE_ERROR;

	block = &wblock->gblock;
	handle = wblock->handle;

	if (block->instance_count < instance)
		return AE_BAD_PARAMETER;

	/* Check GUID is a data block */
	if (block->flags & (ACPI_WMI_EVENT | ACPI_WMI_METHOD))
		return AE_ERROR;

	input.count = 1;
	input.pointer = wq_params;
	wq_params[0].type = ACPI_TYPE_INTEGER;
	wq_params[0].integer.value = instance;

	/*
	 * If ACPI_WMI_EXPENSIVE, call the relevant WCxx method first to
	 * enable collection.
	 */
	if (block->flags & ACPI_WMI_EXPENSIVE) {
		strncat(wc_method, block->object_id, 2);

		/*
		 * Some GUIDs break the specification by declaring themselves
		 * expensive, but have no corresponding WCxx method. So we
		 * should not fail if this happens.
		 */
		if (acpi_has_method(handle, wc_method))
			wc_status = acpi_execute_simple_method(handle,
								wc_method, 1);
	}

	strcpy(method, "WQ");
	strncat(method, block->object_id, 2);

	status = acpi_evaluate_object(handle, method, &input, out);

	/*
	 * If ACPI_WMI_EXPENSIVE, call the relevant WCxx method, even if
	 * the WQxx method failed - we should disable collection anyway.
	 */
	if ((block->flags & ACPI_WMI_EXPENSIVE) && ACPI_SUCCESS(wc_status)) {
		status = acpi_execute_simple_method(handle, wc_method, 0);
	}

	return status;
}
EXPORT_SYMBOL_GPL(wmi_query_block);

/**
 * wmi_set_block - Write to a WMI block
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @instance: Instance index
 * &in: Buffer containing new values for the data block
 *
 * Write the contents of the input buffer to an ACPI-WMI data block
 */
acpi_status wmi_set_block(const char *guid_string, u8 instance,
const struct acpi_buffer *in)
{
	struct guid_block *block = NULL;
	struct wmi_block *wblock = NULL;
	acpi_handle handle;
	struct acpi_object_list input;
	union acpi_object params[2];
	char method[5] = "WS";

	if (!guid_string || !in)
		return AE_BAD_DATA;

	if (!find_guid(guid_string, &wblock))
		return AE_ERROR;

	block = &wblock->gblock;
	handle = wblock->handle;

	if (block->instance_count < instance)
		return AE_BAD_PARAMETER;

	/* Check GUID is a data block */
	if (block->flags & (ACPI_WMI_EVENT | ACPI_WMI_METHOD))
		return AE_ERROR;

	input.count = 2;
	input.pointer = params;
	params[0].type = ACPI_TYPE_INTEGER;
	params[0].integer.value = instance;

	if (block->flags & ACPI_WMI_STRING) {
		params[1].type = ACPI_TYPE_STRING;
	} else {
		params[1].type = ACPI_TYPE_BUFFER;
	}
	params[1].buffer.length = in->length;
	params[1].buffer.pointer = in->pointer;

	strncat(method, block->object_id, 2);

	return acpi_evaluate_object(handle, method, &input, NULL);
}
EXPORT_SYMBOL_GPL(wmi_set_block);

static void wmi_dump_wdg(const struct guid_block *g)
{
	pr_info("%pUL:\n", g->guid);
	pr_info("\tobject_id: %c%c\n", g->object_id[0], g->object_id[1]);
	pr_info("\tnotify_id: %02X\n", g->notify_id);
	pr_info("\treserved: %02X\n", g->reserved);
	pr_info("\tinstance_count: %d\n", g->instance_count);
	pr_info("\tflags: %#x", g->flags);
	if (g->flags) {
		if (g->flags & ACPI_WMI_EXPENSIVE)
			pr_cont(" ACPI_WMI_EXPENSIVE");
		if (g->flags & ACPI_WMI_METHOD)
			pr_cont(" ACPI_WMI_METHOD");
		if (g->flags & ACPI_WMI_STRING)
			pr_cont(" ACPI_WMI_STRING");
		if (g->flags & ACPI_WMI_EVENT)
			pr_cont(" ACPI_WMI_EVENT");
	}
	pr_cont("\n");

}

static void wmi_notify_debug(u32 value, void *context)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_info("bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;

	if (!obj)
		return;

	pr_info("DEBUG Event ");
	switch(obj->type) {
	case ACPI_TYPE_BUFFER:
		pr_cont("BUFFER_TYPE - length %d\n", obj->buffer.length);
		break;
	case ACPI_TYPE_STRING:
		pr_cont("STRING_TYPE - %s\n", obj->string.pointer);
		break;
	case ACPI_TYPE_INTEGER:
		pr_cont("INTEGER_TYPE - %llu\n", obj->integer.value);
		break;
	case ACPI_TYPE_PACKAGE:
		pr_cont("PACKAGE_TYPE - %d elements\n", obj->package.count);
		break;
	default:
		pr_cont("object type 0x%X\n", obj->type);
	}
	kfree(obj);
}

/**
 * wmi_install_notify_handler - Register handler for WMI events
 * @handler: Function to handle notifications
 * @data: Data to be returned to handler when event is fired
 *
 * Register a handler for events sent to the ACPI-WMI mapper device.
 */
acpi_status wmi_install_notify_handler(const char *guid,
wmi_notify_handler handler, void *data)
{
	struct wmi_block *block;
	acpi_status status = AE_NOT_EXIST;
	char tmp[16], guid_input[16];
	struct list_head *p;

	if (!guid || !handler)
		return AE_BAD_PARAMETER;

	wmi_parse_guid(guid, tmp);
	wmi_swap_bytes(tmp, guid_input);

	list_for_each(p, &wmi_block_list) {
		acpi_status wmi_status;
		block = list_entry(p, struct wmi_block, list);

		if (memcmp(block->gblock.guid, guid_input, 16) == 0) {
			if (block->handler &&
			    block->handler != wmi_notify_debug)
				return AE_ALREADY_ACQUIRED;

			block->handler = handler;
			block->handler_data = data;

			wmi_status = wmi_method_enable(block, 1);
			if ((wmi_status != AE_OK) ||
			    ((wmi_status == AE_OK) && (status == AE_NOT_EXIST)))
				status = wmi_status;
		}
	}

	return status;
}
EXPORT_SYMBOL_GPL(wmi_install_notify_handler);

/**
 * wmi_uninstall_notify_handler - Unregister handler for WMI events
 *
 * Unregister handler for events sent to the ACPI-WMI mapper device.
 */
acpi_status wmi_remove_notify_handler(const char *guid)
{
	struct wmi_block *block;
	acpi_status status = AE_NOT_EXIST;
	char tmp[16], guid_input[16];
	struct list_head *p;

	if (!guid)
		return AE_BAD_PARAMETER;

	wmi_parse_guid(guid, tmp);
	wmi_swap_bytes(tmp, guid_input);

	list_for_each(p, &wmi_block_list) {
		acpi_status wmi_status;
		block = list_entry(p, struct wmi_block, list);

		if (memcmp(block->gblock.guid, guid_input, 16) == 0) {
			if (!block->handler ||
			    block->handler == wmi_notify_debug)
				return AE_NULL_ENTRY;

			if (debug_event) {
				block->handler = wmi_notify_debug;
				status = AE_OK;
			} else {
				wmi_status = wmi_method_enable(block, 0);
				block->handler = NULL;
				block->handler_data = NULL;
				if ((wmi_status != AE_OK) ||
				    ((wmi_status == AE_OK) &&
				     (status == AE_NOT_EXIST)))
					status = wmi_status;
			}
		}
	}

	return status;
}
EXPORT_SYMBOL_GPL(wmi_remove_notify_handler);

/**
 * wmi_get_event_data - Get WMI data associated with an event
 *
 * @event: Event to find
 * @out: Buffer to hold event data. out->pointer should be freed with kfree()
 *
 * Returns extra data associated with an event in WMI.
 */
acpi_status wmi_get_event_data(u32 event, struct acpi_buffer *out)
{
	struct acpi_object_list input;
	union acpi_object params[1];
	struct guid_block *gblock;
	struct wmi_block *wblock;
	struct list_head *p;

	input.count = 1;
	input.pointer = params;
	params[0].type = ACPI_TYPE_INTEGER;
	params[0].integer.value = event;

	list_for_each(p, &wmi_block_list) {
		wblock = list_entry(p, struct wmi_block, list);
		gblock = &wblock->gblock;

		if ((gblock->flags & ACPI_WMI_EVENT) &&
			(gblock->notify_id == event))
			return acpi_evaluate_object(wblock->handle, "_WED",
				&input, out);
	}

	return AE_NOT_FOUND;
}
EXPORT_SYMBOL_GPL(wmi_get_event_data);

/**
 * wmi_has_guid - Check if a GUID is available
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 *
 * Check if a given GUID is defined by _WDG
 */
bool wmi_has_guid(const char *guid_string)
{
	return find_guid(guid_string, NULL);
}
EXPORT_SYMBOL_GPL(wmi_has_guid);

/*
 * sysfs interface
 */
static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct wmi_block *wblock;

	wblock = dev_get_drvdata(dev);
	if (!wblock) {
		strcat(buf, "\n");
		return strlen(buf);
	}

	return sprintf(buf, "wmi:%pUL\n", wblock->gblock.guid);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *wmi_attrs[] = {
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(wmi);

static int wmi_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	char guid_string[37];

	struct wmi_block *wblock;

	if (add_uevent_var(env, "MODALIAS="))
		return -ENOMEM;

	wblock = dev_get_drvdata(dev);
	if (!wblock)
		return -ENOMEM;

	sprintf(guid_string, "%pUL", wblock->gblock.guid);

	strcpy(&env->buf[env->buflen - 1], "wmi:");
	memcpy(&env->buf[env->buflen - 1 + 4], guid_string, 36);
	env->buflen += 40;

	return 0;
}

static void wmi_dev_free(struct device *dev)
{
	struct wmi_block *wmi_block = container_of(dev, struct wmi_block, dev);

	kfree(wmi_block);
}

static struct class wmi_class = {
	.name = "wmi",
	.dev_release = wmi_dev_free,
	.dev_uevent = wmi_dev_uevent,
	.dev_groups = wmi_groups,
};

static int wmi_create_device(const struct guid_block *gblock,
			     struct wmi_block *wblock, acpi_handle handle)
{
	wblock->dev.class = &wmi_class;

	dev_set_name(&wblock->dev, "%pUL", gblock->guid);

	dev_set_drvdata(&wblock->dev, wblock);

	return device_register(&wblock->dev);
}

static void wmi_free_devices(void)
{
	struct wmi_block *wblock, *next;

	/* Delete devices for all the GUIDs */
	list_for_each_entry_safe(wblock, next, &wmi_block_list, list) {
		list_del(&wblock->list);
		if (wblock->dev.class)
			device_unregister(&wblock->dev);
		else
			kfree(wblock);
	}
}

static bool guid_already_parsed(const char *guid_string)
{
	struct wmi_block *wblock;

	list_for_each_entry(wblock, &wmi_block_list, list)
		if (memcmp(wblock->gblock.guid, guid_string, 16) == 0)
			return true;

	return false;
}

/*
 * Parse the _WDG method for the GUID data blocks
 */
static int parse_wdg(acpi_handle handle)
{
	struct acpi_buffer out = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obj;
	const struct guid_block *gblock;
	struct wmi_block *wblock;
	acpi_status status;
	int retval;
	u32 i, total;

	status = acpi_evaluate_object(handle, "_WDG", NULL, &out);
	if (ACPI_FAILURE(status))
		return -ENXIO;

	obj = (union acpi_object *) out.pointer;
	if (!obj)
		return -ENXIO;

	if (obj->type != ACPI_TYPE_BUFFER) {
		retval = -ENXIO;
		goto out_free_pointer;
	}

	gblock = (const struct guid_block *)obj->buffer.pointer;
	total = obj->buffer.length / sizeof(struct guid_block);

	for (i = 0; i < total; i++) {
		if (debug_dump_wdg)
			wmi_dump_wdg(&gblock[i]);

		wblock = kzalloc(sizeof(struct wmi_block), GFP_KERNEL);
		if (!wblock)
			return -ENOMEM;

		wblock->handle = handle;
		wblock->gblock = gblock[i];

		/*
		  Some WMI devices, like those for nVidia hooks, have a
		  duplicate GUID. It's not clear what we should do in this
		  case yet, so for now, we'll just ignore the duplicate
		  for device creation.
		*/
		if (!guid_already_parsed(gblock[i].guid)) {
			retval = wmi_create_device(&gblock[i], wblock, handle);
			if (retval) {
				wmi_free_devices();
				goto out_free_pointer;
			}
		}

		list_add_tail(&wblock->list, &wmi_block_list);

		if (debug_event) {
			wblock->handler = wmi_notify_debug;
			wmi_method_enable(wblock, 1);
		}
	}

	retval = 0;

out_free_pointer:
	kfree(out.pointer);

	return retval;
}

/*
 * WMI can have EmbeddedControl access regions. In which case, we just want to
 * hand these off to the EC driver.
 */
static acpi_status
acpi_wmi_ec_space_handler(u32 function, acpi_physical_address address,
		      u32 bits, u64 *value,
		      void *handler_context, void *region_context)
{
	int result = 0, i = 0;
	u8 temp = 0;

	if ((address > 0xFF) || !value)
		return AE_BAD_PARAMETER;

	if (function != ACPI_READ && function != ACPI_WRITE)
		return AE_BAD_PARAMETER;

	if (bits != 8)
		return AE_BAD_PARAMETER;

	if (function == ACPI_READ) {
		result = ec_read(address, &temp);
		(*value) |= ((u64)temp) << i;
	} else {
		temp = 0xff & ((*value) >> i);
		result = ec_write(address, temp);
	}

	switch (result) {
	case -EINVAL:
		return AE_BAD_PARAMETER;
		break;
	case -ENODEV:
		return AE_NOT_FOUND;
		break;
	case -ETIME:
		return AE_TIME;
		break;
	default:
		return AE_OK;
	}
}

static void acpi_wmi_notify(struct acpi_device *device, u32 event)
{
	struct guid_block *block;
	struct wmi_block *wblock;
	struct list_head *p;

	list_for_each(p, &wmi_block_list) {
		wblock = list_entry(p, struct wmi_block, list);
		block = &wblock->gblock;

		if ((block->flags & ACPI_WMI_EVENT) &&
			(block->notify_id == event)) {
			if (wblock->handler)
				wblock->handler(event, wblock->handler_data);
			if (debug_event) {
				pr_info("DEBUG Event GUID: %pUL\n",
					wblock->gblock.guid);
			}

			acpi_bus_generate_netlink_event(
				device->pnp.device_class, dev_name(&device->dev),
				event, 0);
			break;
		}
	}
}

static int acpi_wmi_remove(struct acpi_device *device)
{
	acpi_remove_address_space_handler(device->handle,
				ACPI_ADR_SPACE_EC, &acpi_wmi_ec_space_handler);
	wmi_free_devices();

	return 0;
}

static int acpi_wmi_add(struct acpi_device *device)
{
	acpi_status status;
	int error;

	status = acpi_install_address_space_handler(device->handle,
						    ACPI_ADR_SPACE_EC,
						    &acpi_wmi_ec_space_handler,
						    NULL, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err("Error installing EC region handler\n");
		return -ENODEV;
	}

	error = parse_wdg(device->handle);
	if (error) {
		acpi_remove_address_space_handler(device->handle,
						  ACPI_ADR_SPACE_EC,
						  &acpi_wmi_ec_space_handler);
		pr_err("Failed to parse WDG method\n");
		return error;
	}

	return 0;
}

static int __init acpi_wmi_init(void)
{
	int error;

	if (acpi_disabled)
		return -ENODEV;

	error = class_register(&wmi_class);
	if (error)
		return error;

	error = acpi_bus_register_driver(&acpi_wmi_driver);
	if (error) {
		pr_err("Error loading mapper\n");
		class_unregister(&wmi_class);
		return error;
	}

	pr_info("Mapper loaded\n");
	return 0;
}

static void __exit acpi_wmi_exit(void)
{
	acpi_bus_unregister_driver(&acpi_wmi_driver);
	class_unregister(&wmi_class);

	pr_info("Mapper unloaded\n");
}

subsys_initcall(acpi_wmi_init);
module_exit(acpi_wmi_exit);
