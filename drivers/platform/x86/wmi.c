// SPDX-License-Identifier: GPL-2.0-or-later
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
 *  WMI bus infrastructure by Andrew Lutomirski and Darren Hart:
 *    Copyright (C) 2015 Andrew Lutomirski
 *    Copyright (C) 2017 VMware, Inc. All Rights Reserved.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/build_bug.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/wmi.h>
#include <linux/fs.h>

MODULE_AUTHOR("Carlos Corbacho");
MODULE_DESCRIPTION("ACPI-WMI Mapping Driver");
MODULE_LICENSE("GPL");

static LIST_HEAD(wmi_block_list);

struct guid_block {
	guid_t guid;
	union {
		char object_id[2];
		struct {
			unsigned char notify_id;
			unsigned char reserved;
		};
	};
	u8 instance_count;
	u8 flags;
} __packed;
static_assert(sizeof(typeof_member(struct guid_block, guid)) == 16);
static_assert(sizeof(struct guid_block) == 20);
static_assert(__alignof__(struct guid_block) == 1);

enum {	/* wmi_block flags */
	WMI_READ_TAKES_NO_ARGS,
	WMI_GUID_DUPLICATED,
	WMI_NO_EVENT_DATA,
};

struct wmi_block {
	struct wmi_device dev;
	struct list_head list;
	struct guid_block gblock;
	struct acpi_device *acpi_device;
	struct rw_semaphore notify_lock;	/* Protects notify callback add/remove */
	wmi_notify_handler handler;
	void *handler_data;
	bool driver_ready;
	unsigned long flags;
};


/*
 * If the GUID data block is marked as expensive, we must enable and
 * explicitily disable data collection.
 */
#define ACPI_WMI_EXPENSIVE   BIT(0)
#define ACPI_WMI_METHOD      BIT(1)	/* GUID is a method */
#define ACPI_WMI_STRING      BIT(2)	/* GUID takes & returns a string */
#define ACPI_WMI_EVENT       BIT(3)	/* GUID is an event */

static const struct acpi_device_id wmi_device_ids[] = {
	{"PNP0C14", 0},
	{"pnp0c14", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, wmi_device_ids);

#define dev_to_wblock(__dev)	container_of_const(__dev, struct wmi_block, dev.dev)
#define dev_to_wdev(__dev)	container_of_const(__dev, struct wmi_device, dev)

/*
 * GUID parsing functions
 */

static bool guid_parse_and_compare(const char *string, const guid_t *guid)
{
	guid_t guid_input;

	if (guid_parse(string, &guid_input))
		return false;

	return guid_equal(&guid_input, guid);
}

static const void *find_guid_context(struct wmi_block *wblock,
				     struct wmi_driver *wdriver)
{
	const struct wmi_device_id *id;

	id = wdriver->id_table;
	if (!id)
		return NULL;

	while (*id->guid_string) {
		if (guid_parse_and_compare(id->guid_string, &wblock->gblock.guid))
			return id->context;
		id++;
	}
	return NULL;
}

static acpi_status wmi_method_enable(struct wmi_block *wblock, bool enable)
{
	struct guid_block *block;
	char method[5];
	acpi_status status;
	acpi_handle handle;

	block = &wblock->gblock;
	handle = wblock->acpi_device->handle;

	snprintf(method, 5, "WE%02X", block->notify_id);
	status = acpi_execute_simple_method(handle, method, enable);
	if (status == AE_NOT_FOUND)
		return AE_OK;

	return status;
}

#define WMI_ACPI_METHOD_NAME_SIZE 5

static inline void get_acpi_method_name(const struct wmi_block *wblock,
					const char method,
					char buffer[static WMI_ACPI_METHOD_NAME_SIZE])
{
	static_assert(ARRAY_SIZE(wblock->gblock.object_id) == 2);
	static_assert(WMI_ACPI_METHOD_NAME_SIZE >= 5);

	buffer[0] = 'W';
	buffer[1] = method;
	buffer[2] = wblock->gblock.object_id[0];
	buffer[3] = wblock->gblock.object_id[1];
	buffer[4] = '\0';
}

static inline acpi_object_type get_param_acpi_type(const struct wmi_block *wblock)
{
	if (wblock->gblock.flags & ACPI_WMI_STRING)
		return ACPI_TYPE_STRING;
	else
		return ACPI_TYPE_BUFFER;
}

static acpi_status get_event_data(const struct wmi_block *wblock, struct acpi_buffer *out)
{
	union acpi_object param = {
		.integer = {
			.type = ACPI_TYPE_INTEGER,
			.value = wblock->gblock.notify_id,
		}
	};
	struct acpi_object_list input = {
		.count = 1,
		.pointer = &param,
	};

	return acpi_evaluate_object(wblock->acpi_device->handle, "_WED", &input, out);
}

static int wmidev_match_guid(struct device *dev, const void *data)
{
	struct wmi_block *wblock = dev_to_wblock(dev);
	const guid_t *guid = data;

	/* Legacy GUID-based functions are restricted to only see
	 * a single WMI device for each GUID.
	 */
	if (test_bit(WMI_GUID_DUPLICATED, &wblock->flags))
		return 0;

	if (guid_equal(guid, &wblock->gblock.guid))
		return 1;

	return 0;
}

static int wmidev_match_notify_id(struct device *dev, const void *data)
{
	struct wmi_block *wblock = dev_to_wblock(dev);
	const u32 *notify_id = data;

	/* Legacy GUID-based functions are restricted to only see
	 * a single WMI device for each GUID.
	 */
	if (test_bit(WMI_GUID_DUPLICATED, &wblock->flags))
		return 0;

	if (wblock->gblock.flags & ACPI_WMI_EVENT && wblock->gblock.notify_id == *notify_id)
		return 1;

	return 0;
}

static const struct bus_type wmi_bus_type;

static struct wmi_device *wmi_find_device_by_guid(const char *guid_string)
{
	struct device *dev;
	guid_t guid;
	int ret;

	ret = guid_parse(guid_string, &guid);
	if (ret < 0)
		return ERR_PTR(ret);

	dev = bus_find_device(&wmi_bus_type, NULL, &guid, wmidev_match_guid);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return dev_to_wdev(dev);
}

static struct wmi_device *wmi_find_event_by_notify_id(const u32 notify_id)
{
	struct device *dev;

	dev = bus_find_device(&wmi_bus_type, NULL, &notify_id, wmidev_match_notify_id);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return to_wmi_device(dev);
}

static void wmi_device_put(struct wmi_device *wdev)
{
	put_device(&wdev->dev);
}

/*
 * Exported WMI functions
 */

/**
 * wmi_instance_count - Get number of WMI object instances
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 *
 * Get the number of WMI object instances.
 *
 * Returns: Number of WMI object instances or negative error code.
 */
int wmi_instance_count(const char *guid_string)
{
	struct wmi_device *wdev;
	int ret;

	wdev = wmi_find_device_by_guid(guid_string);
	if (IS_ERR(wdev))
		return PTR_ERR(wdev);

	ret = wmidev_instance_count(wdev);
	wmi_device_put(wdev);

	return ret;
}
EXPORT_SYMBOL_GPL(wmi_instance_count);

/**
 * wmidev_instance_count - Get number of WMI object instances
 * @wdev: A wmi bus device from a driver
 *
 * Get the number of WMI object instances.
 *
 * Returns: Number of WMI object instances.
 */
u8 wmidev_instance_count(struct wmi_device *wdev)
{
	struct wmi_block *wblock = container_of(wdev, struct wmi_block, dev);

	return wblock->gblock.instance_count;
}
EXPORT_SYMBOL_GPL(wmidev_instance_count);

/**
 * wmi_evaluate_method - Evaluate a WMI method (deprecated)
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @instance: Instance index
 * @method_id: Method ID to call
 * @in: Mandatory buffer containing input for the method call
 * @out: Empty buffer to return the method results
 *
 * Call an ACPI-WMI method, the caller must free @out.
 *
 * Return: acpi_status signaling success or error.
 */
acpi_status wmi_evaluate_method(const char *guid_string, u8 instance, u32 method_id,
				const struct acpi_buffer *in, struct acpi_buffer *out)
{
	struct wmi_device *wdev;
	acpi_status status;

	wdev = wmi_find_device_by_guid(guid_string);
	if (IS_ERR(wdev))
		return AE_ERROR;

	status = wmidev_evaluate_method(wdev, instance, method_id, in, out);

	wmi_device_put(wdev);

	return status;
}
EXPORT_SYMBOL_GPL(wmi_evaluate_method);

/**
 * wmidev_evaluate_method - Evaluate a WMI method
 * @wdev: A wmi bus device from a driver
 * @instance: Instance index
 * @method_id: Method ID to call
 * @in: Mandatory buffer containing input for the method call
 * @out: Empty buffer to return the method results
 *
 * Call an ACPI-WMI method, the caller must free @out.
 *
 * Return: acpi_status signaling success or error.
 */
acpi_status wmidev_evaluate_method(struct wmi_device *wdev, u8 instance, u32 method_id,
				   const struct acpi_buffer *in, struct acpi_buffer *out)
{
	struct guid_block *block;
	struct wmi_block *wblock;
	acpi_handle handle;
	struct acpi_object_list input;
	union acpi_object params[3];
	char method[WMI_ACPI_METHOD_NAME_SIZE];

	wblock = container_of(wdev, struct wmi_block, dev);
	block = &wblock->gblock;
	handle = wblock->acpi_device->handle;

	if (!in)
		return AE_BAD_DATA;

	if (!(block->flags & ACPI_WMI_METHOD))
		return AE_BAD_DATA;

	if (block->instance_count <= instance)
		return AE_BAD_PARAMETER;

	input.count = 3;
	input.pointer = params;

	params[0].type = ACPI_TYPE_INTEGER;
	params[0].integer.value = instance;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = method_id;
	params[2].type = get_param_acpi_type(wblock);
	params[2].buffer.length = in->length;
	params[2].buffer.pointer = in->pointer;

	get_acpi_method_name(wblock, 'M', method);

	return acpi_evaluate_object(handle, method, &input, out);
}
EXPORT_SYMBOL_GPL(wmidev_evaluate_method);

static acpi_status __query_block(struct wmi_block *wblock, u8 instance,
				 struct acpi_buffer *out)
{
	struct guid_block *block;
	acpi_handle handle;
	acpi_status status, wc_status = AE_ERROR;
	struct acpi_object_list input;
	union acpi_object wq_params[1];
	char wc_method[WMI_ACPI_METHOD_NAME_SIZE];
	char method[WMI_ACPI_METHOD_NAME_SIZE];

	if (!out)
		return AE_BAD_PARAMETER;

	block = &wblock->gblock;
	handle = wblock->acpi_device->handle;

	if (block->instance_count <= instance)
		return AE_BAD_PARAMETER;

	/* Check GUID is a data block */
	if (block->flags & (ACPI_WMI_EVENT | ACPI_WMI_METHOD))
		return AE_ERROR;

	input.count = 1;
	input.pointer = wq_params;
	wq_params[0].type = ACPI_TYPE_INTEGER;
	wq_params[0].integer.value = instance;

	if (instance == 0 && test_bit(WMI_READ_TAKES_NO_ARGS, &wblock->flags))
		input.count = 0;

	/*
	 * If ACPI_WMI_EXPENSIVE, call the relevant WCxx method first to
	 * enable collection.
	 */
	if (block->flags & ACPI_WMI_EXPENSIVE) {
		get_acpi_method_name(wblock, 'C', wc_method);

		/*
		 * Some GUIDs break the specification by declaring themselves
		 * expensive, but have no corresponding WCxx method. So we
		 * should not fail if this happens.
		 */
		wc_status = acpi_execute_simple_method(handle, wc_method, 1);
	}

	get_acpi_method_name(wblock, 'Q', method);
	status = acpi_evaluate_object(handle, method, &input, out);

	/*
	 * If ACPI_WMI_EXPENSIVE, call the relevant WCxx method, even if
	 * the WQxx method failed - we should disable collection anyway.
	 */
	if ((block->flags & ACPI_WMI_EXPENSIVE) && ACPI_SUCCESS(wc_status)) {
		/*
		 * Ignore whether this WCxx call succeeds or not since
		 * the previously executed WQxx method call might have
		 * succeeded, and returning the failing status code
		 * of this call would throw away the result of the WQxx
		 * call, potentially leaking memory.
		 */
		acpi_execute_simple_method(handle, wc_method, 0);
	}

	return status;
}

/**
 * wmi_query_block - Return contents of a WMI block (deprecated)
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @instance: Instance index
 * @out: Empty buffer to return the contents of the data block to
 *
 * Query a ACPI-WMI block, the caller must free @out.
 *
 * Return: ACPI object containing the content of the WMI block.
 */
acpi_status wmi_query_block(const char *guid_string, u8 instance,
			    struct acpi_buffer *out)
{
	struct wmi_block *wblock;
	struct wmi_device *wdev;
	acpi_status status;

	wdev = wmi_find_device_by_guid(guid_string);
	if (IS_ERR(wdev))
		return AE_ERROR;

	wblock = container_of(wdev, struct wmi_block, dev);
	status = __query_block(wblock, instance, out);

	wmi_device_put(wdev);

	return status;
}
EXPORT_SYMBOL_GPL(wmi_query_block);

/**
 * wmidev_block_query - Return contents of a WMI block
 * @wdev: A wmi bus device from a driver
 * @instance: Instance index
 *
 * Query an ACPI-WMI block, the caller must free the result.
 *
 * Return: ACPI object containing the content of the WMI block.
 */
union acpi_object *wmidev_block_query(struct wmi_device *wdev, u8 instance)
{
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	struct wmi_block *wblock = container_of(wdev, struct wmi_block, dev);

	if (ACPI_FAILURE(__query_block(wblock, instance, &out)))
		return NULL;

	return out.pointer;
}
EXPORT_SYMBOL_GPL(wmidev_block_query);

/**
 * wmi_set_block - Write to a WMI block (deprecated)
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @instance: Instance index
 * @in: Buffer containing new values for the data block
 *
 * Write the contents of the input buffer to an ACPI-WMI data block.
 *
 * Return: acpi_status signaling success or error.
 */
acpi_status wmi_set_block(const char *guid_string, u8 instance, const struct acpi_buffer *in)
{
	struct wmi_device *wdev;
	acpi_status status;

	wdev = wmi_find_device_by_guid(guid_string);
	if (IS_ERR(wdev))
		return AE_ERROR;

	status =  wmidev_block_set(wdev, instance, in);
	wmi_device_put(wdev);

	return status;
}
EXPORT_SYMBOL_GPL(wmi_set_block);

/**
 * wmidev_block_set - Write to a WMI block
 * @wdev: A wmi bus device from a driver
 * @instance: Instance index
 * @in: Buffer containing new values for the data block
 *
 * Write contents of the input buffer to an ACPI-WMI data block.
 *
 * Return: acpi_status signaling success or error.
 */
acpi_status wmidev_block_set(struct wmi_device *wdev, u8 instance, const struct acpi_buffer *in)
{
	struct wmi_block *wblock = container_of(wdev, struct wmi_block, dev);
	acpi_handle handle = wblock->acpi_device->handle;
	struct guid_block *block = &wblock->gblock;
	char method[WMI_ACPI_METHOD_NAME_SIZE];
	struct acpi_object_list input;
	union acpi_object params[2];

	if (!in)
		return AE_BAD_DATA;

	if (block->instance_count <= instance)
		return AE_BAD_PARAMETER;

	/* Check GUID is a data block */
	if (block->flags & (ACPI_WMI_EVENT | ACPI_WMI_METHOD))
		return AE_ERROR;

	input.count = 2;
	input.pointer = params;
	params[0].type = ACPI_TYPE_INTEGER;
	params[0].integer.value = instance;
	params[1].type = get_param_acpi_type(wblock);
	params[1].buffer.length = in->length;
	params[1].buffer.pointer = in->pointer;

	get_acpi_method_name(wblock, 'S', method);

	return acpi_evaluate_object(handle, method, &input, NULL);
}
EXPORT_SYMBOL_GPL(wmidev_block_set);

/**
 * wmi_install_notify_handler - Register handler for WMI events (deprecated)
 * @guid: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @handler: Function to handle notifications
 * @data: Data to be returned to handler when event is fired
 *
 * Register a handler for events sent to the ACPI-WMI mapper device.
 *
 * Return: acpi_status signaling success or error.
 */
acpi_status wmi_install_notify_handler(const char *guid,
				       wmi_notify_handler handler,
				       void *data)
{
	struct wmi_block *wblock;
	struct wmi_device *wdev;
	acpi_status status;

	wdev = wmi_find_device_by_guid(guid);
	if (IS_ERR(wdev))
		return AE_ERROR;

	wblock = container_of(wdev, struct wmi_block, dev);

	down_write(&wblock->notify_lock);
	if (wblock->handler) {
		status = AE_ALREADY_ACQUIRED;
	} else {
		wblock->handler = handler;
		wblock->handler_data = data;

		if (ACPI_FAILURE(wmi_method_enable(wblock, true)))
			dev_warn(&wblock->dev.dev, "Failed to enable device\n");

		status = AE_OK;
	}
	up_write(&wblock->notify_lock);

	wmi_device_put(wdev);

	return status;
}
EXPORT_SYMBOL_GPL(wmi_install_notify_handler);

/**
 * wmi_remove_notify_handler - Unregister handler for WMI events (deprecated)
 * @guid: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 *
 * Unregister handler for events sent to the ACPI-WMI mapper device.
 *
 * Return: acpi_status signaling success or error.
 */
acpi_status wmi_remove_notify_handler(const char *guid)
{
	struct wmi_block *wblock;
	struct wmi_device *wdev;
	acpi_status status;

	wdev = wmi_find_device_by_guid(guid);
	if (IS_ERR(wdev))
		return AE_ERROR;

	wblock = container_of(wdev, struct wmi_block, dev);

	down_write(&wblock->notify_lock);
	if (!wblock->handler) {
		status = AE_NULL_ENTRY;
	} else {
		if (ACPI_FAILURE(wmi_method_enable(wblock, false)))
			dev_warn(&wblock->dev.dev, "Failed to disable device\n");

		wblock->handler = NULL;
		wblock->handler_data = NULL;

		status = AE_OK;
	}
	up_write(&wblock->notify_lock);

	wmi_device_put(wdev);

	return status;
}
EXPORT_SYMBOL_GPL(wmi_remove_notify_handler);

/**
 * wmi_get_event_data - Get WMI data associated with an event (deprecated)
 *
 * @event: Event to find
 * @out: Buffer to hold event data
 *
 * Get extra data associated with an WMI event, the caller needs to free @out.
 *
 * Return: acpi_status signaling success or error.
 */
acpi_status wmi_get_event_data(u32 event, struct acpi_buffer *out)
{
	struct wmi_block *wblock;
	struct wmi_device *wdev;
	acpi_status status;

	wdev = wmi_find_event_by_notify_id(event);
	if (IS_ERR(wdev))
		return AE_NOT_FOUND;

	wblock = container_of(wdev, struct wmi_block, dev);
	status = get_event_data(wblock, out);

	wmi_device_put(wdev);

	return status;
}
EXPORT_SYMBOL_GPL(wmi_get_event_data);

/**
 * wmi_has_guid - Check if a GUID is available
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 *
 * Check if a given GUID is defined by _WDG.
 *
 * Return: True if GUID is available, false otherwise.
 */
bool wmi_has_guid(const char *guid_string)
{
	struct wmi_device *wdev;

	wdev = wmi_find_device_by_guid(guid_string);
	if (IS_ERR(wdev))
		return false;

	wmi_device_put(wdev);

	return true;
}
EXPORT_SYMBOL_GPL(wmi_has_guid);

/**
 * wmi_get_acpi_device_uid() - Get _UID name of ACPI device that defines GUID (deprecated)
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 *
 * Find the _UID of ACPI device associated with this WMI GUID.
 *
 * Return: The ACPI _UID field value or NULL if the WMI GUID was not found.
 */
char *wmi_get_acpi_device_uid(const char *guid_string)
{
	struct wmi_block *wblock;
	struct wmi_device *wdev;
	char *uid;

	wdev = wmi_find_device_by_guid(guid_string);
	if (IS_ERR(wdev))
		return NULL;

	wblock = container_of(wdev, struct wmi_block, dev);
	uid = acpi_device_uid(wblock->acpi_device);

	wmi_device_put(wdev);

	return uid;
}
EXPORT_SYMBOL_GPL(wmi_get_acpi_device_uid);

static inline struct wmi_driver *drv_to_wdrv(struct device_driver *drv)
{
	return container_of(drv, struct wmi_driver, driver);
}

/*
 * sysfs interface
 */
static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sysfs_emit(buf, "wmi:%pUL\n", &wblock->gblock.guid);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t guid_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sysfs_emit(buf, "%pUL\n", &wblock->gblock.guid);
}
static DEVICE_ATTR_RO(guid);

static ssize_t instance_count_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sysfs_emit(buf, "%d\n", (int)wblock->gblock.instance_count);
}
static DEVICE_ATTR_RO(instance_count);

static ssize_t expensive_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sysfs_emit(buf, "%d\n",
			  (wblock->gblock.flags & ACPI_WMI_EXPENSIVE) != 0);
}
static DEVICE_ATTR_RO(expensive);

static struct attribute *wmi_attrs[] = {
	&dev_attr_modalias.attr,
	&dev_attr_guid.attr,
	&dev_attr_instance_count.attr,
	&dev_attr_expensive.attr,
	NULL
};
ATTRIBUTE_GROUPS(wmi);

static ssize_t notify_id_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sysfs_emit(buf, "%02X\n", (unsigned int)wblock->gblock.notify_id);
}
static DEVICE_ATTR_RO(notify_id);

static struct attribute *wmi_event_attrs[] = {
	&dev_attr_notify_id.attr,
	NULL
};
ATTRIBUTE_GROUPS(wmi_event);

static ssize_t object_id_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sysfs_emit(buf, "%c%c\n", wblock->gblock.object_id[0],
			  wblock->gblock.object_id[1]);
}
static DEVICE_ATTR_RO(object_id);

static ssize_t setable_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct wmi_device *wdev = dev_to_wdev(dev);

	return sysfs_emit(buf, "%d\n", (int)wdev->setable);
}
static DEVICE_ATTR_RO(setable);

static struct attribute *wmi_data_attrs[] = {
	&dev_attr_object_id.attr,
	&dev_attr_setable.attr,
	NULL
};
ATTRIBUTE_GROUPS(wmi_data);

static struct attribute *wmi_method_attrs[] = {
	&dev_attr_object_id.attr,
	NULL
};
ATTRIBUTE_GROUPS(wmi_method);

static int wmi_dev_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct wmi_block *wblock = dev_to_wblock(dev);

	if (add_uevent_var(env, "MODALIAS=wmi:%pUL", &wblock->gblock.guid))
		return -ENOMEM;

	if (add_uevent_var(env, "WMI_GUID=%pUL", &wblock->gblock.guid))
		return -ENOMEM;

	return 0;
}

static void wmi_dev_release(struct device *dev)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	kfree(wblock);
}

static int wmi_dev_match(struct device *dev, struct device_driver *driver)
{
	struct wmi_driver *wmi_driver = drv_to_wdrv(driver);
	struct wmi_block *wblock = dev_to_wblock(dev);
	const struct wmi_device_id *id = wmi_driver->id_table;

	if (id == NULL)
		return 0;

	while (*id->guid_string) {
		if (guid_parse_and_compare(id->guid_string, &wblock->gblock.guid))
			return 1;

		id++;
	}

	return 0;
}

static int wmi_dev_probe(struct device *dev)
{
	struct wmi_block *wblock = dev_to_wblock(dev);
	struct wmi_driver *wdriver = drv_to_wdrv(dev->driver);
	int ret = 0;

	/* Some older WMI drivers will break if instantiated multiple times,
	 * so they are blocked from probing WMI devices with a duplicated GUID.
	 *
	 * New WMI drivers should support being instantiated multiple times.
	 */
	if (test_bit(WMI_GUID_DUPLICATED, &wblock->flags) && !wdriver->no_singleton) {
		dev_warn(dev, "Legacy driver %s cannot be instantiated multiple times\n",
			 dev->driver->name);

		return -ENODEV;
	}

	if (wdriver->notify) {
		if (test_bit(WMI_NO_EVENT_DATA, &wblock->flags) && !wdriver->no_notify_data)
			return -ENODEV;
	}

	if (ACPI_FAILURE(wmi_method_enable(wblock, true)))
		dev_warn(dev, "failed to enable device -- probing anyway\n");

	if (wdriver->probe) {
		ret = wdriver->probe(dev_to_wdev(dev),
				find_guid_context(wblock, wdriver));
		if (ret) {
			if (ACPI_FAILURE(wmi_method_enable(wblock, false)))
				dev_warn(dev, "Failed to disable device\n");

			return ret;
		}
	}

	down_write(&wblock->notify_lock);
	wblock->driver_ready = true;
	up_write(&wblock->notify_lock);

	return 0;
}

static void wmi_dev_remove(struct device *dev)
{
	struct wmi_block *wblock = dev_to_wblock(dev);
	struct wmi_driver *wdriver = drv_to_wdrv(dev->driver);

	down_write(&wblock->notify_lock);
	wblock->driver_ready = false;
	up_write(&wblock->notify_lock);

	if (wdriver->remove)
		wdriver->remove(dev_to_wdev(dev));

	if (ACPI_FAILURE(wmi_method_enable(wblock, false)))
		dev_warn(dev, "failed to disable device\n");
}

static struct class wmi_bus_class = {
	.name = "wmi_bus",
};

static const struct bus_type wmi_bus_type = {
	.name = "wmi",
	.dev_groups = wmi_groups,
	.match = wmi_dev_match,
	.uevent = wmi_dev_uevent,
	.probe = wmi_dev_probe,
	.remove = wmi_dev_remove,
};

static const struct device_type wmi_type_event = {
	.name = "event",
	.groups = wmi_event_groups,
	.release = wmi_dev_release,
};

static const struct device_type wmi_type_method = {
	.name = "method",
	.groups = wmi_method_groups,
	.release = wmi_dev_release,
};

static const struct device_type wmi_type_data = {
	.name = "data",
	.groups = wmi_data_groups,
	.release = wmi_dev_release,
};

/*
 * _WDG is a static list that is only parsed at startup,
 * so it's safe to count entries without extra protection.
 */
static int guid_count(const guid_t *guid)
{
	struct wmi_block *wblock;
	int count = 0;

	list_for_each_entry(wblock, &wmi_block_list, list) {
		if (guid_equal(&wblock->gblock.guid, guid))
			count++;
	}

	return count;
}

static int wmi_create_device(struct device *wmi_bus_dev,
			     struct wmi_block *wblock,
			     struct acpi_device *device)
{
	char method[WMI_ACPI_METHOD_NAME_SIZE];
	struct acpi_device_info *info;
	acpi_handle method_handle;
	acpi_status status;
	uint count;

	if (wblock->gblock.flags & ACPI_WMI_EVENT) {
		wblock->dev.dev.type = &wmi_type_event;
		goto out_init;
	}

	if (wblock->gblock.flags & ACPI_WMI_METHOD) {
		get_acpi_method_name(wblock, 'M', method);
		if (!acpi_has_method(device->handle, method)) {
			dev_warn(wmi_bus_dev,
				 FW_BUG "%s method block execution control method not found\n",
				 method);

			return -ENXIO;
		}

		wblock->dev.dev.type = &wmi_type_method;
		goto out_init;
	}

	/*
	 * Data Block Query Control Method (WQxx by convention) is
	 * required per the WMI documentation. If it is not present,
	 * we ignore this data block.
	 */
	get_acpi_method_name(wblock, 'Q', method);
	status = acpi_get_handle(device->handle, method, &method_handle);
	if (ACPI_FAILURE(status)) {
		dev_warn(wmi_bus_dev,
			 FW_BUG "%s data block query control method not found\n",
			 method);

		return -ENXIO;
	}

	status = acpi_get_object_info(method_handle, &info);
	if (ACPI_FAILURE(status))
		return -EIO;

	wblock->dev.dev.type = &wmi_type_data;

	/*
	 * The Microsoft documentation specifically states:
	 *
	 *   Data blocks registered with only a single instance
	 *   can ignore the parameter.
	 *
	 * ACPICA will get mad at us if we call the method with the wrong number
	 * of arguments, so check what our method expects.  (On some Dell
	 * laptops, WQxx may not be a method at all.)
	 */
	if (info->type != ACPI_TYPE_METHOD || info->param_count == 0)
		set_bit(WMI_READ_TAKES_NO_ARGS, &wblock->flags);

	kfree(info);

	get_acpi_method_name(wblock, 'S', method);
	if (acpi_has_method(device->handle, method))
		wblock->dev.setable = true;

 out_init:
	init_rwsem(&wblock->notify_lock);
	wblock->driver_ready = false;
	wblock->dev.dev.bus = &wmi_bus_type;
	wblock->dev.dev.parent = wmi_bus_dev;

	count = guid_count(&wblock->gblock.guid);
	if (count) {
		dev_set_name(&wblock->dev.dev, "%pUL-%d", &wblock->gblock.guid, count);
		set_bit(WMI_GUID_DUPLICATED, &wblock->flags);
	} else {
		dev_set_name(&wblock->dev.dev, "%pUL", &wblock->gblock.guid);
	}

	device_initialize(&wblock->dev.dev);

	return 0;
}

static int wmi_add_device(struct platform_device *pdev, struct wmi_device *wdev)
{
	struct device_link *link;

	/*
	 * Many aggregate WMI drivers do not use -EPROBE_DEFER when they
	 * are unable to find a WMI device during probe, instead they require
	 * all WMI devices associated with an platform device to become available
	 * at once. This device link thus prevents WMI drivers from probing until
	 * the associated platform device has finished probing (and has registered
	 * all discovered WMI devices).
	 */

	link = device_link_add(&wdev->dev, &pdev->dev, DL_FLAG_AUTOREMOVE_SUPPLIER);
	if (!link)
		return -EINVAL;

	return device_add(&wdev->dev);
}

/*
 * Parse the _WDG method for the GUID data blocks
 */
static int parse_wdg(struct device *wmi_bus_dev, struct platform_device *pdev)
{
	struct acpi_device *device = ACPI_COMPANION(&pdev->dev);
	struct acpi_buffer out = {ACPI_ALLOCATE_BUFFER, NULL};
	const struct guid_block *gblock;
	bool event_data_available;
	struct wmi_block *wblock;
	union acpi_object *obj;
	acpi_status status;
	u32 i, total;
	int retval;

	status = acpi_evaluate_object(device->handle, "_WDG", NULL, &out);
	if (ACPI_FAILURE(status))
		return -ENXIO;

	obj = out.pointer;
	if (!obj)
		return -ENXIO;

	if (obj->type != ACPI_TYPE_BUFFER) {
		kfree(obj);
		return -ENXIO;
	}

	event_data_available = acpi_has_method(device->handle, "_WED");
	gblock = (const struct guid_block *)obj->buffer.pointer;
	total = obj->buffer.length / sizeof(struct guid_block);

	for (i = 0; i < total; i++) {
		if (!gblock[i].instance_count) {
			dev_info(wmi_bus_dev, FW_INFO "%pUL has zero instances\n", &gblock[i].guid);
			continue;
		}

		wblock = kzalloc(sizeof(*wblock), GFP_KERNEL);
		if (!wblock)
			continue;

		wblock->acpi_device = device;
		wblock->gblock = gblock[i];
		if (gblock[i].flags & ACPI_WMI_EVENT && !event_data_available)
			set_bit(WMI_NO_EVENT_DATA, &wblock->flags);

		retval = wmi_create_device(wmi_bus_dev, wblock, device);
		if (retval) {
			kfree(wblock);
			continue;
		}

		list_add_tail(&wblock->list, &wmi_block_list);

		retval = wmi_add_device(pdev, &wblock->dev);
		if (retval) {
			dev_err(wmi_bus_dev, "failed to register %pUL\n",
				&wblock->gblock.guid);

			list_del(&wblock->list);
			put_device(&wblock->dev.dev);
		}
	}

	kfree(obj);

	return 0;
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
	int result = 0;
	u8 temp = 0;

	if ((address > 0xFF) || !value)
		return AE_BAD_PARAMETER;

	if (function != ACPI_READ && function != ACPI_WRITE)
		return AE_BAD_PARAMETER;

	if (bits != 8)
		return AE_BAD_PARAMETER;

	if (function == ACPI_READ) {
		result = ec_read(address, &temp);
		*value = temp;
	} else {
		temp = 0xff & *value;
		result = ec_write(address, temp);
	}

	switch (result) {
	case -EINVAL:
		return AE_BAD_PARAMETER;
	case -ENODEV:
		return AE_NOT_FOUND;
	case -ETIME:
		return AE_TIME;
	default:
		return AE_OK;
	}
}

static int wmi_get_notify_data(struct wmi_block *wblock, union acpi_object **obj)
{
	struct acpi_buffer data = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;

	if (test_bit(WMI_NO_EVENT_DATA, &wblock->flags)) {
		*obj = NULL;
		return 0;
	}

	status = get_event_data(wblock, &data);
	if (ACPI_FAILURE(status)) {
		dev_warn(&wblock->dev.dev, "Failed to get event data\n");
		return -EIO;
	}

	*obj = data.pointer;

	return 0;
}

static void wmi_notify_driver(struct wmi_block *wblock, union acpi_object *obj)
{
	struct wmi_driver *driver = drv_to_wdrv(wblock->dev.dev.driver);

	if (!obj && !driver->no_notify_data) {
		dev_warn(&wblock->dev.dev, "Event contains no event data\n");
		return;
	}

	if (driver->notify)
		driver->notify(&wblock->dev, obj);
}

static int wmi_notify_device(struct device *dev, void *data)
{
	struct wmi_block *wblock = dev_to_wblock(dev);
	union acpi_object *obj;
	u32 *event = data;
	int ret;

	if (!(wblock->gblock.flags & ACPI_WMI_EVENT && wblock->gblock.notify_id == *event))
		return 0;

	down_read(&wblock->notify_lock);
	/* The WMI driver notify handler conflicts with the legacy WMI handler.
	 * Because of this the WMI driver notify handler takes precedence.
	 */
	if (wblock->dev.dev.driver && wblock->driver_ready) {
		ret = wmi_get_notify_data(wblock, &obj);
		if (ret >= 0) {
			wmi_notify_driver(wblock, obj);
			kfree(obj);
		}
	} else {
		if (wblock->handler) {
			wblock->handler(*event, wblock->handler_data);
		} else {
			/* The ACPI WMI specification says that _WED should be
			 * evaluated every time an notification is received, even
			 * if no consumers are present.
			 *
			 * Some firmware implementations actually depend on this
			 * by using a queue for events which will fill up if the
			 * WMI driver core stops evaluating _WED due to missing
			 * WMI event consumers.
			 *
			 * Because of this we need this seemingly useless call to
			 * wmi_get_notify_data() which in turn evaluates _WED.
			 */
			ret = wmi_get_notify_data(wblock, &obj);
			if (ret >= 0)
				kfree(obj);
		}

	}
	up_read(&wblock->notify_lock);

	acpi_bus_generate_netlink_event("wmi", acpi_dev_name(wblock->acpi_device), *event, 0);

	return -EBUSY;
}

static void acpi_wmi_notify_handler(acpi_handle handle, u32 event, void *context)
{
	struct device *wmi_bus_dev = context;

	device_for_each_child(wmi_bus_dev, &event, wmi_notify_device);
}

static int wmi_remove_device(struct device *dev, void *data)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	list_del(&wblock->list);
	device_unregister(dev);

	return 0;
}

static void acpi_wmi_remove(struct platform_device *device)
{
	struct device *wmi_bus_device = dev_get_drvdata(&device->dev);

	device_for_each_child_reverse(wmi_bus_device, NULL, wmi_remove_device);
}

static void acpi_wmi_remove_notify_handler(void *data)
{
	struct acpi_device *acpi_device = data;

	acpi_remove_notify_handler(acpi_device->handle, ACPI_ALL_NOTIFY, acpi_wmi_notify_handler);
}

static void acpi_wmi_remove_address_space_handler(void *data)
{
	struct acpi_device *acpi_device = data;

	acpi_remove_address_space_handler(acpi_device->handle, ACPI_ADR_SPACE_EC,
					  &acpi_wmi_ec_space_handler);
}

static void acpi_wmi_remove_bus_device(void *data)
{
	struct device *wmi_bus_dev = data;

	device_unregister(wmi_bus_dev);
}

static int acpi_wmi_probe(struct platform_device *device)
{
	struct acpi_device *acpi_device;
	struct device *wmi_bus_dev;
	acpi_status status;
	int error;

	acpi_device = ACPI_COMPANION(&device->dev);
	if (!acpi_device) {
		dev_err(&device->dev, "ACPI companion is missing\n");
		return -ENODEV;
	}

	wmi_bus_dev = device_create(&wmi_bus_class, &device->dev, MKDEV(0, 0), NULL, "wmi_bus-%s",
				    dev_name(&device->dev));
	if (IS_ERR(wmi_bus_dev))
		return PTR_ERR(wmi_bus_dev);

	error = devm_add_action_or_reset(&device->dev, acpi_wmi_remove_bus_device, wmi_bus_dev);
	if (error < 0)
		return error;

	dev_set_drvdata(&device->dev, wmi_bus_dev);

	status = acpi_install_address_space_handler(acpi_device->handle,
						    ACPI_ADR_SPACE_EC,
						    &acpi_wmi_ec_space_handler,
						    NULL, NULL);
	if (ACPI_FAILURE(status)) {
		dev_err(&device->dev, "Error installing EC region handler\n");
		return -ENODEV;
	}
	error = devm_add_action_or_reset(&device->dev, acpi_wmi_remove_address_space_handler,
					 acpi_device);
	if (error < 0)
		return error;

	status = acpi_install_notify_handler(acpi_device->handle, ACPI_ALL_NOTIFY,
					     acpi_wmi_notify_handler, wmi_bus_dev);
	if (ACPI_FAILURE(status)) {
		dev_err(&device->dev, "Error installing notify handler\n");
		return -ENODEV;
	}
	error = devm_add_action_or_reset(&device->dev, acpi_wmi_remove_notify_handler,
					 acpi_device);
	if (error < 0)
		return error;

	error = parse_wdg(wmi_bus_dev, device);
	if (error) {
		dev_err(&device->dev, "Failed to parse _WDG method\n");
		return error;
	}

	return 0;
}

int __must_check __wmi_driver_register(struct wmi_driver *driver,
				       struct module *owner)
{
	driver->driver.owner = owner;
	driver->driver.bus = &wmi_bus_type;

	return driver_register(&driver->driver);
}
EXPORT_SYMBOL(__wmi_driver_register);

/**
 * wmi_driver_unregister() - Unregister a WMI driver
 * @driver: WMI driver to unregister
 *
 * Unregisters a WMI driver from the WMI bus.
 */
void wmi_driver_unregister(struct wmi_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL(wmi_driver_unregister);

static struct platform_driver acpi_wmi_driver = {
	.driver = {
		.name = "acpi-wmi",
		.acpi_match_table = wmi_device_ids,
	},
	.probe = acpi_wmi_probe,
	.remove_new = acpi_wmi_remove,
};

static int __init acpi_wmi_init(void)
{
	int error;

	if (acpi_disabled)
		return -ENODEV;

	error = class_register(&wmi_bus_class);
	if (error)
		return error;

	error = bus_register(&wmi_bus_type);
	if (error)
		goto err_unreg_class;

	error = platform_driver_register(&acpi_wmi_driver);
	if (error) {
		pr_err("Error loading mapper\n");
		goto err_unreg_bus;
	}

	return 0;

err_unreg_bus:
	bus_unregister(&wmi_bus_type);

err_unreg_class:
	class_unregister(&wmi_bus_class);

	return error;
}

static void __exit acpi_wmi_exit(void)
{
	platform_driver_unregister(&acpi_wmi_driver);
	bus_unregister(&wmi_bus_type);
	class_unregister(&wmi_bus_class);
}

subsys_initcall_sync(acpi_wmi_init);
module_exit(acpi_wmi_exit);
