/*
 *  acpi_utils.c - ACPI Utility Functions ($Revision: 10 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_BUS_COMPONENT
ACPI_MODULE_NAME("utils");

/* --------------------------------------------------------------------------
                            Object Evaluation Helpers
   -------------------------------------------------------------------------- */
static void
acpi_util_eval_error(acpi_handle h, acpi_string p, acpi_status s)
{
#ifdef ACPI_DEBUG_OUTPUT
	char prefix[80] = {'\0'};
	struct acpi_buffer buffer = {sizeof(prefix), prefix};
	acpi_get_name(h, ACPI_FULL_PATHNAME, &buffer);
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Evaluate [%s.%s]: %s\n",
		(char *) prefix, p, acpi_format_exception(s)));
#else
	return;
#endif
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
		printk(KERN_WARNING PREFIX "Invalid package argument\n");
		return AE_BAD_PARAMETER;
	}

	if (!format || !format->pointer || (format->length < 1)) {
		printk(KERN_WARNING PREFIX "Invalid format argument\n");
		return AE_BAD_PARAMETER;
	}

	if (!buffer) {
		printk(KERN_WARNING PREFIX "Invalid buffer argument\n");
		return AE_BAD_PARAMETER;
	}

	format_count = (format->length / sizeof(char)) - 1;
	if (format_count > package->package.count) {
		printk(KERN_WARNING PREFIX "Format specifies more objects [%d]"
			      " than exist in package [%d].\n",
			      format_count, package->package.count);
		return AE_BAD_DATA;
	}

	format_string = format->pointer;

	/*
	 * Calculate size_required.
	 */
	for (i = 0; i < format_count; i++) {

		union acpi_object *element = &(package->package.elements[i]);

		if (!element) {
			return AE_BAD_DATA;
		}

		switch (element->type) {

		case ACPI_TYPE_INTEGER:
			switch (format_string[i]) {
			case 'N':
				size_required += sizeof(acpi_integer);
				tail_offset += sizeof(acpi_integer);
				break;
			case 'S':
				size_required +=
				    sizeof(char *) + sizeof(acpi_integer) +
				    sizeof(char);
				tail_offset += sizeof(char *);
				break;
			default:
				printk(KERN_WARNING PREFIX "Invalid package element"
					      " [%d]: got number, expecing"
					      " [%c]\n",
					      i, format_string[i]);
				return AE_BAD_DATA;
				break;
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
				    sizeof(u8 *) +
				    (element->buffer.length * sizeof(u8));
				tail_offset += sizeof(u8 *);
				break;
			default:
				printk(KERN_WARNING PREFIX "Invalid package element"
					      " [%d] got string/buffer,"
					      " expecing [%c]\n",
					      i, format_string[i]);
				return AE_BAD_DATA;
				break;
			}
			break;

		case ACPI_TYPE_PACKAGE:
		default:
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Found unsupported element at index=%d\n",
					  i));
			/* TBD: handle nested packages... */
			return AE_SUPPORT;
			break;
		}
	}

	/*
	 * Validate output buffer.
	 */
	if (buffer->length < size_required) {
		buffer->length = size_required;
		return AE_BUFFER_OVERFLOW;
	} else if (buffer->length != size_required || !buffer->pointer) {
		return AE_BAD_PARAMETER;
	}

	head = buffer->pointer;
	tail = buffer->pointer + tail_offset;

	/*
	 * Extract package data.
	 */
	for (i = 0; i < format_count; i++) {

		u8 **pointer = NULL;
		union acpi_object *element = &(package->package.elements[i]);

		if (!element) {
			return AE_BAD_DATA;
		}

		switch (element->type) {

		case ACPI_TYPE_INTEGER:
			switch (format_string[i]) {
			case 'N':
				*((acpi_integer *) head) =
				    element->integer.value;
				head += sizeof(acpi_integer);
				break;
			case 'S':
				pointer = (u8 **) head;
				*pointer = tail;
				*((acpi_integer *) tail) =
				    element->integer.value;
				head += sizeof(acpi_integer *);
				tail += sizeof(acpi_integer);
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
				tail += element->buffer.length * sizeof(u8);
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

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Return value [%llu]\n", *data));

	return AE_OK;
}

EXPORT_SYMBOL(acpi_evaluate_integer);

#if 0
acpi_status
acpi_evaluate_string(acpi_handle handle,
		     acpi_string pathname,
		     acpi_object_list * arguments, acpi_string * data)
{
	acpi_status status = AE_OK;
	acpi_object *element = NULL;
	acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };


	if (!data)
		return AE_BAD_PARAMETER;

	status = acpi_evaluate_object(handle, pathname, arguments, &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_util_eval_error(handle, pathname, status);
		return status;
	}

	element = (acpi_object *) buffer.pointer;

	if ((element->type != ACPI_TYPE_STRING)
	    || (element->type != ACPI_TYPE_BUFFER)
	    || !element->string.length) {
		acpi_util_eval_error(handle, pathname, AE_BAD_DATA);
		return AE_BAD_DATA;
	}

	*data = kzalloc(element->string.length + 1, GFP_KERNEL);
	if (!data) {
		printk(KERN_ERR PREFIX "Memory allocation\n");
		return -ENOMEM;
	}

	memcpy(*data, element->string.pointer, element->string.length);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Return value [%s]\n", *data));

	kfree(buffer.pointer);

	return AE_OK;
}
#endif

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
		printk(KERN_ERR PREFIX "No return object (len %X ptr %p)\n",
			    (unsigned)buffer.length, package);
		status = AE_BAD_DATA;
		acpi_util_eval_error(handle, pathname, status);
		goto end;
	}
	if (package->type != ACPI_TYPE_PACKAGE) {
		printk(KERN_ERR PREFIX "Expecting a [Package], found type %X\n",
			    package->type);
		status = AE_BAD_DATA;
		acpi_util_eval_error(handle, pathname, status);
		goto end;
	}
	if (!package->package.count) {
		printk(KERN_ERR PREFIX "[Package] has zero elements (%p)\n",
			    package);
		status = AE_BAD_DATA;
		acpi_util_eval_error(handle, pathname, status);
		goto end;
	}

	if (package->package.count > ACPI_MAX_HANDLES) {
		return AE_NO_MEMORY;
	}
	list->count = package->package.count;

	/* Extract package data. */

	for (i = 0; i < list->count; i++) {

		element = &(package->package.elements[i]);

		if (element->type != ACPI_TYPE_LOCAL_REFERENCE) {
			status = AE_BAD_DATA;
			printk(KERN_ERR PREFIX
				    "Expecting a [Reference] package element, found type %X\n",
				    element->type);
			acpi_util_eval_error(handle, pathname, status);
			break;
		}

		if (!element->reference.handle) {
			printk(KERN_WARNING PREFIX "Invalid reference in"
			       " package %s\n", pathname);
			status = AE_NULL_ENTRY;
			break;
		}
		/* Get the  acpi_handle. */

		list->handles[i] = element->reference.handle;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found reference [%p]\n",
				  list->handles[i]));
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
