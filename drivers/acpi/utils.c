// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  acpi_utils.c - ACPI Utility Functions ($Revision: 10 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#define pr_fmt(fmt) "ACPI: utils: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/hardirq.h>
#include <linux/acpi.h>
#include <linux/dynamic_debug.h>

#include "internal.h"
#include "sleep.h"

/* --------------------------------------------------------------------------
                            Object Evaluation Helpers
   -------------------------------------------------------------------------- */
static void acpi_util_eval_error(acpi_handle h, acpi_string p, acpi_status s)
{
	acpi_handle_debug(h, "Evaluate [%s]: %s\n", p, acpi_format_exception(s));
}

acpi_status
acpi_extract_package(union acpi_object *package,
		     struct acpi_buffer *format, struct acpi_buffer *buffer)
{
	u32 size_required = 0;
	u32 tail_offset = 0;
	char *format_string = NULL;
	u32 format_count = 0;
	u32 i = 0;
	u8 *head = NULL;
	u8 *tail = NULL;


	if (!package || (package->type != ACPI_TYPE_PACKAGE)
	    || (package->package.count < 1)) {
		pr_debug("Invalid package argument\n");
		return AE_BAD_PARAMETER;
	}

	if (!format || !format->pointer || (format->length < 1)) {
		pr_debug("Invalid format argument\n");
		return AE_BAD_PARAMETER;
	}

	if (!buffer) {
		pr_debug("Invalid buffer argument\n");
		return AE_BAD_PARAMETER;
	}

	format_count = (format->length / sizeof(char)) - 1;
	if (format_count > package->package.count) {
		pr_debug("Format specifies more objects [%d] than present [%d]\n",
			 format_count, package->package.count);
		return AE_BAD_DATA;
	}

	format_string = format->pointer;

	/*
	 * Calculate size_required.
	 */
	for (i = 0; i < format_count; i++) {

		union acpi_object *element = &(package->package.elements[i]);

		switch (element->type) {

		case ACPI_TYPE_INTEGER:
			switch (format_string[i]) {
			case 'N':
				size_required += sizeof(u64);
				tail_offset += sizeof(u64);
				break;
			case 'S':
				size_required +=
				    sizeof(char *) + sizeof(u64) +
				    sizeof(char);
				tail_offset += sizeof(char *);
				break;
			default:
				pr_debug("Invalid package element [%d]: got number, expected [%c]\n",
					 i, format_string[i]);
				return AE_BAD_DATA;
			}
			break;

		case ACPI_TYPE_STRING:
		case ACPI_TYPE_BUFFER:
			switch (format_string[i]) {
			case 'S':
				size_required +=
				    sizeof(char *) +
				    (element->string.length * sizeof(char)) +
				    sizeof(char);
				tail_offset += sizeof(char *);
				break;
			case 'B':
				size_required +=
				    sizeof(u8 *) + element->buffer.length;
				tail_offset += sizeof(u8 *);
				break;
			default:
				pr_debug("Invalid package element [%d] got string/buffer, expected [%c]\n",
					 i, format_string[i]);
				return AE_BAD_DATA;
			}
			break;
		case ACPI_TYPE_LOCAL_REFERENCE:
			switch (format_string[i]) {
			case 'R':
				size_required += sizeof(void *);
				tail_offset += sizeof(void *);
				break;
			default:
				pr_debug("Invalid package element [%d] got reference, expected [%c]\n",
					 i, format_string[i]);
				return AE_BAD_DATA;
			}
			break;

		case ACPI_TYPE_PACKAGE:
		default:
			pr_debug("Unsupported element at index=%d\n", i);
			/* TBD: handle nested packages... */
			return AE_SUPPORT;
		}
	}

	/*
	 * Validate output buffer.
	 */
	if (buffer->length == ACPI_ALLOCATE_BUFFER) {
		buffer->pointer = ACPI_ALLOCATE_ZEROED(size_required);
		if (!buffer->pointer)
			return AE_NO_MEMORY;
		buffer->length = size_required;
	} else {
		if (buffer->length < size_required) {
			buffer->length = size_required;
			return AE_BUFFER_OVERFLOW;
		} else if (buffer->length != size_required ||
			   !buffer->pointer) {
			return AE_BAD_PARAMETER;
		}
	}

	head = buffer->pointer;
	tail = buffer->pointer + tail_offset;

	/*
	 * Extract package data.
	 */
	for (i = 0; i < format_count; i++) {

		u8 **pointer = NULL;
		union acpi_object *element = &(package->package.elements[i]);

		switch (element->type) {

		case ACPI_TYPE_INTEGER:
			switch (format_string[i]) {
			case 'N':
				*((u64 *) head) =
				    element->integer.value;
				head += sizeof(u64);
				break;
			case 'S':
				pointer = (u8 **) head;
				*pointer = tail;
				*((u64 *) tail) =
				    element->integer.value;
				head += sizeof(u64 *);
				tail += sizeof(u64);
				/* NULL terminate string */
				*tail = (char)0;
				tail += sizeof(char);
				break;
			default:
				/* Should never get here */
				break;
			}
			break;

		case ACPI_TYPE_STRING:
		case ACPI_TYPE_BUFFER:
			switch (format_string[i]) {
			case 'S':
				pointer = (u8 **) head;
				*pointer = tail;
				memcpy(tail, element->string.pointer,
				       element->string.length);
				head += sizeof(char *);
				tail += element->string.length * sizeof(char);
				/* NULL terminate string */
				*tail = (char)0;
				tail += sizeof(char);
				break;
			case 'B':
				pointer = (u8 **) head;
				*pointer = tail;
				memcpy(tail, element->buffer.pointer,
				       element->buffer.length);
				head += sizeof(u8 *);
				tail += element->buffer.length;
				break;
			default:
				/* Should never get here */
				break;
			}
			break;
		case ACPI_TYPE_LOCAL_REFERENCE:
			switch (format_string[i]) {
			case 'R':
				*(void **)head =
				    (void *)element->reference.handle;
				head += sizeof(void *);
				break;
			default:
				/* Should never get here */
				break;
			}
			break;
		case ACPI_TYPE_PACKAGE:
			/* TBD: handle nested packages... */
		default:
			/* Should never get here */
			break;
		}
	}

	return AE_OK;
}

EXPORT_SYMBOL(acpi_extract_package);

acpi_status
acpi_evaluate_integer(acpi_handle handle,
		      acpi_string pathname,
		      struct acpi_object_list *arguments, unsigned long long *data)
{
	acpi_status status = AE_OK;
	union acpi_object element;
	struct acpi_buffer buffer = { 0, NULL };

	if (!data)
		return AE_BAD_PARAMETER;

	buffer.length = sizeof(union acpi_object);
	buffer.pointer = &element;
	status = acpi_evaluate_object(handle, pathname, arguments, &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_util_eval_error(handle, pathname, status);
		return status;
	}

	if (element.type != ACPI_TYPE_INTEGER) {
		acpi_util_eval_error(handle, pathname, AE_BAD_DATA);
		return AE_BAD_DATA;
	}

	*data = element.integer.value;

	acpi_handle_debug(handle, "Return value [%llu]\n", *data);

	return AE_OK;
}

EXPORT_SYMBOL(acpi_evaluate_integer);

int acpi_get_local_address(acpi_handle handle, u32 *addr)
{
	unsigned long long adr;
	acpi_status status;

	status = acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL, &adr);
	if (ACPI_FAILURE(status))
		return -ENODATA;

	*addr = (u32)adr;
	return 0;
}
EXPORT_SYMBOL(acpi_get_local_address);

#define ACPI_MAX_SUB_BUF_SIZE	9

const char *acpi_get_subsystem_id(acpi_handle handle)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	const char *sub;
	size_t len;

	status = acpi_evaluate_object(handle, METHOD_NAME__SUB, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_handle_debug(handle, "Reading ACPI _SUB failed: %#x\n", status);
		return ERR_PTR(-ENODATA);
	}

	obj = buffer.pointer;
	if (obj->type == ACPI_TYPE_STRING) {
		len = strlen(obj->string.pointer);
		if (len < ACPI_MAX_SUB_BUF_SIZE && len > 0) {
			sub = kstrdup(obj->string.pointer, GFP_KERNEL);
			if (!sub)
				sub = ERR_PTR(-ENOMEM);
		} else {
			acpi_handle_err(handle, "ACPI _SUB Length %zu is Invalid\n", len);
			sub = ERR_PTR(-ENODATA);
		}
	} else {
		acpi_handle_warn(handle, "Warning ACPI _SUB did not return a string\n");
		sub = ERR_PTR(-ENODATA);
	}

	acpi_os_free(buffer.pointer);

	return sub;
}
EXPORT_SYMBOL_GPL(acpi_get_subsystem_id);

acpi_status
acpi_evaluate_reference(acpi_handle handle,
			acpi_string pathname,
			struct acpi_object_list *arguments,
			struct acpi_handle_list *list)
{
	acpi_status status = AE_OK;
	union acpi_object *package = NULL;
	union acpi_object *element = NULL;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	u32 i = 0;


	if (!list) {
		return AE_BAD_PARAMETER;
	}

	/* Evaluate object. */

	status = acpi_evaluate_object(handle, pathname, arguments, &buffer);
	if (ACPI_FAILURE(status))
		goto end;

	package = buffer.pointer;

	if ((buffer.length == 0) || !package) {
		status = AE_BAD_DATA;
		acpi_util_eval_error(handle, pathname, status);
		goto end;
	}
	if (package->type != ACPI_TYPE_PACKAGE) {
		status = AE_BAD_DATA;
		acpi_util_eval_error(handle, pathname, status);
		goto end;
	}
	if (!package->package.count) {
		status = AE_BAD_DATA;
		acpi_util_eval_error(handle, pathname, status);
		goto end;
	}

	if (package->package.count > ACPI_MAX_HANDLES) {
		kfree(package);
		return AE_NO_MEMORY;
	}
	list->count = package->package.count;

	/* Extract package data. */

	for (i = 0; i < list->count; i++) {

		element = &(package->package.elements[i]);

		if (element->type != ACPI_TYPE_LOCAL_REFERENCE) {
			status = AE_BAD_DATA;
			acpi_util_eval_error(handle, pathname, status);
			break;
		}

		if (!element->reference.handle) {
			status = AE_NULL_ENTRY;
			acpi_util_eval_error(handle, pathname, status);
			break;
		}
		/* Get the  acpi_handle. */

		list->handles[i] = element->reference.handle;
		acpi_handle_debug(list->handles[i], "Found in reference list\n");
	}

      end:
	if (ACPI_FAILURE(status)) {
		list->count = 0;
		//kfree(list->handles);
	}

	kfree(buffer.pointer);

	return status;
}

EXPORT_SYMBOL(acpi_evaluate_reference);

acpi_status
acpi_get_physical_device_location(acpi_handle handle, struct acpi_pld_info **pld)
{
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *output;

	status = acpi_evaluate_object(handle, "_PLD", NULL, &buffer);

	if (ACPI_FAILURE(status))
		return status;

	output = buffer.pointer;

	if (!output || output->type != ACPI_TYPE_PACKAGE
	    || !output->package.count
	    || output->package.elements[0].type != ACPI_TYPE_BUFFER
	    || output->package.elements[0].buffer.length < ACPI_PLD_REV1_BUFFER_SIZE) {
		status = AE_TYPE;
		goto out;
	}

	status = acpi_decode_pld_buffer(
			output->package.elements[0].buffer.pointer,
			output->package.elements[0].buffer.length,
			pld);

out:
	kfree(buffer.pointer);
	return status;
}
EXPORT_SYMBOL(acpi_get_physical_device_location);

/**
 * acpi_evaluate_ost: Evaluate _OST for hotplug operations
 * @handle: ACPI device handle
 * @source_event: source event code
 * @status_code: status code
 * @status_buf: optional detailed information (NULL if none)
 *
 * Evaluate _OST for hotplug operations. All ACPI hotplug handlers
 * must call this function when evaluating _OST for hotplug operations.
 * When the platform does not support _OST, this function has no effect.
 */
acpi_status
acpi_evaluate_ost(acpi_handle handle, u32 source_event, u32 status_code,
		  struct acpi_buffer *status_buf)
{
	union acpi_object params[3] = {
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_BUFFER,}
	};
	struct acpi_object_list arg_list = {3, params};

	params[0].integer.value = source_event;
	params[1].integer.value = status_code;
	if (status_buf != NULL) {
		params[2].buffer.pointer = status_buf->pointer;
		params[2].buffer.length = status_buf->length;
	} else {
		params[2].buffer.pointer = NULL;
		params[2].buffer.length = 0;
	}

	return acpi_evaluate_object(handle, "_OST", &arg_list, NULL);
}
EXPORT_SYMBOL(acpi_evaluate_ost);

/**
 * acpi_handle_path: Return the object path of handle
 * @handle: ACPI device handle
 *
 * Caller must free the returned buffer
 */
static char *acpi_handle_path(acpi_handle handle)
{
	struct acpi_buffer buffer = {
		.length = ACPI_ALLOCATE_BUFFER,
		.pointer = NULL
	};

	if (in_interrupt() ||
	    acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer) != AE_OK)
		return NULL;
	return buffer.pointer;
}

/**
 * acpi_handle_printk: Print message with ACPI prefix and object path
 * @level: log level
 * @handle: ACPI device handle
 * @fmt: format string
 *
 * This function is called through acpi_handle_<level> macros and prints
 * a message with ACPI prefix and object path.  This function acquires
 * the global namespace mutex to obtain an object path.  In interrupt
 * context, it shows the object path as <n/a>.
 */
void
acpi_handle_printk(const char *level, acpi_handle handle, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	const char *path;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	path = acpi_handle_path(handle);
	printk("%sACPI: %s: %pV", level, path ? path : "<n/a>" , &vaf);

	va_end(args);
	kfree(path);
}
EXPORT_SYMBOL(acpi_handle_printk);

#if defined(CONFIG_DYNAMIC_DEBUG)
/**
 * __acpi_handle_debug: pr_debug with ACPI prefix and object path
 * @descriptor: Dynamic Debug descriptor
 * @handle: ACPI device handle
 * @fmt: format string
 *
 * This function is called through acpi_handle_debug macro and debug
 * prints a message with ACPI prefix and object path. This function
 * acquires the global namespace mutex to obtain an object path.  In
 * interrupt context, it shows the object path as <n/a>.
 */
void
__acpi_handle_debug(struct _ddebug *descriptor, acpi_handle handle,
		    const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	const char *path;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	path = acpi_handle_path(handle);
	__dynamic_pr_debug(descriptor, "ACPI: %s: %pV", path ? path : "<n/a>", &vaf);

	va_end(args);
	kfree(path);
}
EXPORT_SYMBOL(__acpi_handle_debug);
#endif

/**
 * acpi_evaluation_failure_warn - Log evaluation failure warning.
 * @handle: Parent object handle.
 * @name: Name of the object whose evaluation has failed.
 * @status: Status value returned by the failing object evaluation.
 */
void acpi_evaluation_failure_warn(acpi_handle handle, const char *name,
				  acpi_status status)
{
	acpi_handle_warn(handle, "%s evaluation failed: %s\n", name,
			 acpi_format_exception(status));
}
EXPORT_SYMBOL_GPL(acpi_evaluation_failure_warn);

/**
 * acpi_has_method: Check whether @handle has a method named @name
 * @handle: ACPI device handle
 * @name: name of object or method
 *
 * Check whether @handle has a method named @name.
 */
bool acpi_has_method(acpi_handle handle, char *name)
{
	acpi_handle tmp;

	return ACPI_SUCCESS(acpi_get_handle(handle, name, &tmp));
}
EXPORT_SYMBOL(acpi_has_method);

acpi_status acpi_execute_simple_method(acpi_handle handle, char *method,
				       u64 arg)
{
	union acpi_object obj = { .type = ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { .count = 1, .pointer = &obj, };

	obj.integer.value = arg;

	return acpi_evaluate_object(handle, method, &arg_list, NULL);
}
EXPORT_SYMBOL(acpi_execute_simple_method);

/**
 * acpi_evaluate_ej0: Evaluate _EJ0 method for hotplug operations
 * @handle: ACPI device handle
 *
 * Evaluate device's _EJ0 method for hotplug operations.
 */
acpi_status acpi_evaluate_ej0(acpi_handle handle)
{
	acpi_status status;

	status = acpi_execute_simple_method(handle, "_EJ0", 1);
	if (status == AE_NOT_FOUND)
		acpi_handle_warn(handle, "No _EJ0 support for device\n");
	else if (ACPI_FAILURE(status))
		acpi_handle_warn(handle, "Eject failed (0x%x)\n", status);

	return status;
}

/**
 * acpi_evaluate_lck: Evaluate _LCK method to lock/unlock device
 * @handle: ACPI device handle
 * @lock: lock device if non-zero, otherwise unlock device
 *
 * Evaluate device's _LCK method if present to lock/unlock device
 */
acpi_status acpi_evaluate_lck(acpi_handle handle, int lock)
{
	acpi_status status;

	status = acpi_execute_simple_method(handle, "_LCK", !!lock);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		if (lock)
			acpi_handle_warn(handle,
				"Locking device failed (0x%x)\n", status);
		else
			acpi_handle_warn(handle,
				"Unlocking device failed (0x%x)\n", status);
	}

	return status;
}

/**
 * acpi_evaluate_reg: Evaluate _REG method to register OpRegion presence
 * @handle: ACPI device handle
 * @space_id: ACPI address space id to register OpRegion presence for
 * @function: Parameter to pass to _REG one of ACPI_REG_CONNECT or
 *            ACPI_REG_DISCONNECT
 *
 * Evaluate device's _REG method to register OpRegion presence.
 */
acpi_status acpi_evaluate_reg(acpi_handle handle, u8 space_id, u32 function)
{
	struct acpi_object_list arg_list;
	union acpi_object params[2];

	params[0].type = ACPI_TYPE_INTEGER;
	params[0].integer.value = space_id;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = function;
	arg_list.count = 2;
	arg_list.pointer = params;

	return acpi_evaluate_object(handle, "_REG", &arg_list, NULL);
}
EXPORT_SYMBOL(acpi_evaluate_reg);

/**
 * acpi_evaluate_dsm - evaluate device's _DSM method
 * @handle: ACPI device handle
 * @guid: GUID of requested functions, should be 16 bytes
 * @rev: revision number of requested function
 * @func: requested function number
 * @argv4: the function specific parameter
 *
 * Evaluate device's _DSM method with specified GUID, revision id and
 * function number. Caller needs to free the returned object.
 *
 * Though ACPI defines the fourth parameter for _DSM should be a package,
 * some old BIOSes do expect a buffer or an integer etc.
 */
union acpi_object *
acpi_evaluate_dsm(acpi_handle handle, const guid_t *guid, u64 rev, u64 func,
		  union acpi_object *argv4)
{
	acpi_status ret;
	struct acpi_buffer buf = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object params[4];
	struct acpi_object_list input = {
		.count = 4,
		.pointer = params,
	};

	params[0].type = ACPI_TYPE_BUFFER;
	params[0].buffer.length = 16;
	params[0].buffer.pointer = (u8 *)guid;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = rev;
	params[2].type = ACPI_TYPE_INTEGER;
	params[2].integer.value = func;
	if (argv4) {
		params[3] = *argv4;
	} else {
		params[3].type = ACPI_TYPE_PACKAGE;
		params[3].package.count = 0;
		params[3].package.elements = NULL;
	}

	ret = acpi_evaluate_object(handle, "_DSM", &input, &buf);
	if (ACPI_SUCCESS(ret))
		return (union acpi_object *)buf.pointer;

	if (ret != AE_NOT_FOUND)
		acpi_handle_warn(handle,
				 "failed to evaluate _DSM %pUb (0x%x)\n", guid, ret);

	return NULL;
}
EXPORT_SYMBOL(acpi_evaluate_dsm);

/**
 * acpi_check_dsm - check if _DSM method supports requested functions.
 * @handle: ACPI device handle
 * @guid: GUID of requested functions, should be 16 bytes at least
 * @rev: revision number of requested functions
 * @funcs: bitmap of requested functions
 *
 * Evaluate device's _DSM method to check whether it supports requested
 * functions. Currently only support 64 functions at maximum, should be
 * enough for now.
 */
bool acpi_check_dsm(acpi_handle handle, const guid_t *guid, u64 rev, u64 funcs)
{
	int i;
	u64 mask = 0;
	union acpi_object *obj;

	if (funcs == 0)
		return false;

	obj = acpi_evaluate_dsm(handle, guid, rev, 0, NULL);
	if (!obj)
		return false;

	/* For compatibility, old BIOSes may return an integer */
	if (obj->type == ACPI_TYPE_INTEGER)
		mask = obj->integer.value;
	else if (obj->type == ACPI_TYPE_BUFFER)
		for (i = 0; i < obj->buffer.length && i < 8; i++)
			mask |= (((u64)obj->buffer.pointer[i]) << (i * 8));
	ACPI_FREE(obj);

	/*
	 * Bit 0 indicates whether there's support for any functions other than
	 * function 0 for the specified GUID and revision.
	 */
	if ((mask & 0x1) && (mask & funcs) == funcs)
		return true;

	return false;
}
EXPORT_SYMBOL(acpi_check_dsm);

/**
 * acpi_dev_hid_uid_match - Match device by supplied HID and UID
 * @adev: ACPI device to match.
 * @hid2: Hardware ID of the device.
 * @uid2: Unique ID of the device, pass NULL to not check _UID.
 *
 * Matches HID and UID in @adev with given @hid2 and @uid2.
 * Returns true if matches.
 */
bool acpi_dev_hid_uid_match(struct acpi_device *adev,
			    const char *hid2, const char *uid2)
{
	const char *hid1 = acpi_device_hid(adev);
	const char *uid1 = acpi_device_uid(adev);

	if (strcmp(hid1, hid2))
		return false;

	if (!uid2)
		return true;

	return uid1 && !strcmp(uid1, uid2);
}
EXPORT_SYMBOL(acpi_dev_hid_uid_match);

/**
 * acpi_dev_found - Detect presence of a given ACPI device in the namespace.
 * @hid: Hardware ID of the device.
 *
 * Return %true if the device was present at the moment of invocation.
 * Note that if the device is pluggable, it may since have disappeared.
 *
 * For this function to work, acpi_bus_scan() must have been executed
 * which happens in the subsys_initcall() subsection. Hence, do not
 * call from a subsys_initcall() or earlier (use acpi_get_devices()
 * instead). Calling from module_init() is fine (which is synonymous
 * with device_initcall()).
 */
bool acpi_dev_found(const char *hid)
{
	struct acpi_device_bus_id *acpi_device_bus_id;
	bool found = false;

	mutex_lock(&acpi_device_lock);
	list_for_each_entry(acpi_device_bus_id, &acpi_bus_id_list, node)
		if (!strcmp(acpi_device_bus_id->bus_id, hid)) {
			found = true;
			break;
		}
	mutex_unlock(&acpi_device_lock);

	return found;
}
EXPORT_SYMBOL(acpi_dev_found);

struct acpi_dev_match_info {
	struct acpi_device_id hid[2];
	const char *uid;
	s64 hrv;
};

static int acpi_dev_match_cb(struct device *dev, const void *data)
{
	struct acpi_device *adev = to_acpi_device(dev);
	const struct acpi_dev_match_info *match = data;
	unsigned long long hrv;
	acpi_status status;

	if (acpi_match_device_ids(adev, match->hid))
		return 0;

	if (match->uid && (!adev->pnp.unique_id ||
	    strcmp(adev->pnp.unique_id, match->uid)))
		return 0;

	if (match->hrv == -1)
		return 1;

	status = acpi_evaluate_integer(adev->handle, "_HRV", NULL, &hrv);
	if (ACPI_FAILURE(status))
		return 0;

	return hrv == match->hrv;
}

/**
 * acpi_dev_present - Detect that a given ACPI device is present
 * @hid: Hardware ID of the device.
 * @uid: Unique ID of the device, pass NULL to not check _UID
 * @hrv: Hardware Revision of the device, pass -1 to not check _HRV
 *
 * Return %true if a matching device was present at the moment of invocation.
 * Note that if the device is pluggable, it may since have disappeared.
 *
 * Note that unlike acpi_dev_found() this function checks the status
 * of the device. So for devices which are present in the DSDT, but
 * which are disabled (their _STA callback returns 0) this function
 * will return false.
 *
 * For this function to work, acpi_bus_scan() must have been executed
 * which happens in the subsys_initcall() subsection. Hence, do not
 * call from a subsys_initcall() or earlier (use acpi_get_devices()
 * instead). Calling from module_init() is fine (which is synonymous
 * with device_initcall()).
 */
bool acpi_dev_present(const char *hid, const char *uid, s64 hrv)
{
	struct acpi_dev_match_info match = {};
	struct device *dev;

	strlcpy(match.hid[0].id, hid, sizeof(match.hid[0].id));
	match.uid = uid;
	match.hrv = hrv;

	dev = bus_find_device(&acpi_bus_type, NULL, &match, acpi_dev_match_cb);
	put_device(dev);
	return !!dev;
}
EXPORT_SYMBOL(acpi_dev_present);

/**
 * acpi_dev_get_next_match_dev - Return the next match of ACPI device
 * @adev: Pointer to the previous ACPI device matching this @hid, @uid and @hrv
 * @hid: Hardware ID of the device.
 * @uid: Unique ID of the device, pass NULL to not check _UID
 * @hrv: Hardware Revision of the device, pass -1 to not check _HRV
 *
 * Return the next match of ACPI device if another matching device was present
 * at the moment of invocation, or NULL otherwise.
 *
 * The caller is responsible for invoking acpi_dev_put() on the returned device.
 * On the other hand the function invokes  acpi_dev_put() on the given @adev
 * assuming that its reference counter had been increased beforehand.
 *
 * See additional information in acpi_dev_present() as well.
 */
struct acpi_device *
acpi_dev_get_next_match_dev(struct acpi_device *adev, const char *hid, const char *uid, s64 hrv)
{
	struct device *start = adev ? &adev->dev : NULL;
	struct acpi_dev_match_info match = {};
	struct device *dev;

	strlcpy(match.hid[0].id, hid, sizeof(match.hid[0].id));
	match.uid = uid;
	match.hrv = hrv;

	dev = bus_find_device(&acpi_bus_type, start, &match, acpi_dev_match_cb);
	acpi_dev_put(adev);
	return dev ? to_acpi_device(dev) : NULL;
}
EXPORT_SYMBOL(acpi_dev_get_next_match_dev);

/**
 * acpi_dev_get_first_match_dev - Return the first match of ACPI device
 * @hid: Hardware ID of the device.
 * @uid: Unique ID of the device, pass NULL to not check _UID
 * @hrv: Hardware Revision of the device, pass -1 to not check _HRV
 *
 * Return the first match of ACPI device if a matching device was present
 * at the moment of invocation, or NULL otherwise.
 *
 * The caller is responsible for invoking acpi_dev_put() on the returned device.
 *
 * See additional information in acpi_dev_present() as well.
 */
struct acpi_device *
acpi_dev_get_first_match_dev(const char *hid, const char *uid, s64 hrv)
{
	return acpi_dev_get_next_match_dev(NULL, hid, uid, hrv);
}
EXPORT_SYMBOL(acpi_dev_get_first_match_dev);

/**
 * acpi_reduced_hardware - Return if this is an ACPI-reduced-hw machine
 *
 * Return true when running on an ACPI-reduced-hw machine, false otherwise.
 */
bool acpi_reduced_hardware(void)
{
	return acpi_gbl_reduced_hardware;
}
EXPORT_SYMBOL_GPL(acpi_reduced_hardware);

/*
 * acpi_backlight= handling, this is done here rather then in video_detect.c
 * because __setup cannot be used in modules.
 */
char acpi_video_backlight_string[16];
EXPORT_SYMBOL(acpi_video_backlight_string);

static int __init acpi_backlight(char *str)
{
	strlcpy(acpi_video_backlight_string, str,
		sizeof(acpi_video_backlight_string));
	return 1;
}
__setup("acpi_backlight=", acpi_backlight);

/**
 * acpi_match_platform_list - Check if the system matches with a given list
 * @plat: pointer to acpi_platform_list table terminated by a NULL entry
 *
 * Return the matched index if the system is found in the platform list.
 * Otherwise, return a negative error code.
 */
int acpi_match_platform_list(const struct acpi_platform_list *plat)
{
	struct acpi_table_header hdr;
	int idx = 0;

	if (acpi_disabled)
		return -ENODEV;

	for (; plat->oem_id[0]; plat++, idx++) {
		if (ACPI_FAILURE(acpi_get_table_header(plat->table, 0, &hdr)))
			continue;

		if (strncmp(plat->oem_id, hdr.oem_id, ACPI_OEM_ID_SIZE))
			continue;

		if (strncmp(plat->oem_table_id, hdr.oem_table_id, ACPI_OEM_TABLE_ID_SIZE))
			continue;

		if ((plat->pred == all_versions) ||
		    (plat->pred == less_than_or_equal && hdr.oem_revision <= plat->oem_revision) ||
		    (plat->pred == greater_than_or_equal && hdr.oem_revision >= plat->oem_revision) ||
		    (plat->pred == equal && hdr.oem_revision == plat->oem_revision))
			return idx;
	}

	return -ENODEV;
}
EXPORT_SYMBOL(acpi_match_platform_list);
