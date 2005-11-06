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
ACPI_MODULE_NAME("acpi_utils")

/* --------------------------------------------------------------------------
                            Object Evaluation Helpers
   -------------------------------------------------------------------------- */
#ifdef ACPI_DEBUG_OUTPUT
#define acpi_util_eval_error(h,p,s) {\
	char prefix[80] = {'\0'};\
	struct acpi_buffer buffer = {sizeof(prefix), prefix};\
	acpi_get_name(h, ACPI_FULL_PATHNAME, &buffer);\
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Evaluate [%s.%s]: %s\n",\
		(char *) prefix, p, acpi_format_exception(s))); }
#else
#define acpi_util_eval_error(h,p,s)
#endif
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

	ACPI_FUNCTION_TRACE("acpi_extract_package");

	if (!package || (package->type != ACPI_TYPE_PACKAGE)
	    || (package->package.count < 1)) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN,
				  "Invalid 'package' argument\n"));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (!format || !format->pointer || (format->length < 1)) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid 'format' argument\n"));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (!buffer) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid 'buffer' argument\n"));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	format_count = (format->length / sizeof(char)) - 1;
	if (format_count > package->package.count) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN,
				  "Format specifies more objects [%d] than exist in package [%d].",
				  format_count, package->package.count));
		return_ACPI_STATUS(AE_BAD_DATA);
	}

	format_string = (char *)format->pointer;

	/*
	 * Calculate size_required.
	 */
	for (i = 0; i < format_count; i++) {

		union acpi_object *element = &(package->package.elements[i]);

		if (!element) {
			return_ACPI_STATUS(AE_BAD_DATA);
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
				ACPI_DEBUG_PRINT((ACPI_DB_WARN,
						  "Invalid package element [%d]: got number, expecing [%c].\n",
						  i, format_string[i]));
				return_ACPI_STATUS(AE_BAD_DATA);
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
				ACPI_DEBUG_PRINT((ACPI_DB_WARN,
						  "Invalid package element [%d] got string/buffer, expecing [%c].\n",
						  i, format_string[i]));
				return_ACPI_STATUS(AE_BAD_DATA);
				break;
			}
			break;

		case ACPI_TYPE_PACKAGE:
		default:
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Found unsupported element at index=%d\n",
					  i));
			/* TBD: handle nested packages... */
			return_ACPI_STATUS(AE_SUPPORT);
			break;
		}
	}

	/*
	 * Validate output buffer.
	 */
	if (buffer->length < size_required) {
		buffer->length = size_required;
		return_ACPI_STATUS(AE_BUFFER_OVERFLOW);
	} else if (buffer->length != size_required || !buffer->pointer) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
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
			return_ACPI_STATUS(AE_BAD_DATA);
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

	return_ACPI_STATUS(AE_OK);
}

EXPORT_SYMBOL(acpi_extract_package);

acpi_status
acpi_evaluate_integer(acpi_handle handle,
		      acpi_string pathname,
		      struct acpi_object_list *arguments, unsigned long *data)
{
	acpi_status status = AE_OK;
	union acpi_object *element;
	struct acpi_buffer buffer = { 0, NULL };

	ACPI_FUNCTION_TRACE("acpi_evaluate_integer");

	if (!data)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	element = kmalloc(sizeof(union acpi_object), GFP_KERNEL);
	if (!element)
		return_ACPI_STATUS(AE_NO_MEMORY);

	memset(element, 0, sizeof(union acpi_object));
	buffer.length = sizeof(union acpi_object);
	buffer.pointer = element;
	status = acpi_evaluate_object(handle, pathname, arguments, &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_util_eval_error(handle, pathname, status);
		return_ACPI_STATUS(status);
	}

	if (element->type != ACPI_TYPE_INTEGER) {
		acpi_util_eval_error(handle, pathname, AE_BAD_DATA);
		return_ACPI_STATUS(AE_BAD_DATA);
	}

	*data = element->integer.value;
	kfree(element);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Return value [%lu]\n", *data));

	return_ACPI_STATUS(AE_OK);
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

	ACPI_FUNCTION_TRACE("acpi_evaluate_string");

	if (!data)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	status = acpi_evaluate_object(handle, pathname, arguments, &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_util_eval_error(handle, pathname, status);
		return_ACPI_STATUS(status);
	}

	element = (acpi_object *) buffer.pointer;

	if ((element->type != ACPI_TYPE_STRING)
	    || (element->type != ACPI_TYPE_BUFFER)
	    || !element->string.length) {
		acpi_util_eval_error(handle, pathname, AE_BAD_DATA);
		return_ACPI_STATUS(AE_BAD_DATA);
	}

	*data = kmalloc(element->string.length + 1, GFP_KERNEL);
	if (!data) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Memory allocation error\n"));
		return_VALUE(-ENOMEM);
	}
	memset(*data, 0, element->string.length + 1);

	memcpy(*data, element->string.pointer, element->string.length);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Return value [%s]\n", *data));

	acpi_os_free(buffer.pointer);

	return_ACPI_STATUS(AE_OK);
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

	ACPI_FUNCTION_TRACE("acpi_evaluate_reference");

	if (!list) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Evaluate object. */

	status = acpi_evaluate_object(handle, pathname, arguments, &buffer);
	if (ACPI_FAILURE(status))
		goto end;

	package = (union acpi_object *)buffer.pointer;

	if ((buffer.length == 0) || !package) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "No return object (len %X ptr %p)\n",
				  (unsigned)buffer.length, package));
		status = AE_BAD_DATA;
		acpi_util_eval_error(handle, pathname, status);
		goto end;
	}
	if (package->type != ACPI_TYPE_PACKAGE) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Expecting a [Package], found type %X\n",
				  package->type));
		status = AE_BAD_DATA;
		acpi_util_eval_error(handle, pathname, status);
		goto end;
	}
	if (!package->package.count) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "[Package] has zero elements (%p)\n",
				  package));
		status = AE_BAD_DATA;
		acpi_util_eval_error(handle, pathname, status);
		goto end;
	}

	if (package->package.count > ACPI_MAX_HANDLES) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}
	list->count = package->package.count;

	/* Extract package data. */

	for (i = 0; i < list->count; i++) {

		element = &(package->package.elements[i]);

		if (element->type != ACPI_TYPE_ANY) {
			status = AE_BAD_DATA;
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Expecting a [Reference] package element, found type %X\n",
					  element->type));
			acpi_util_eval_error(handle, pathname, status);
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

	acpi_os_free(buffer.pointer);

	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_evaluate_reference);
