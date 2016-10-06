/******************************************************************************
 *
 * Module Name: nsconvert - Object conversions for objects returned by
 *                          predefined methods
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "acinterp.h"
#include "acpredef.h"
#include "amlresrc.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsconvert")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_convert_to_integer
 *
 * PARAMETERS:  original_object     - Object to be converted
 *              return_object       - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful.
 *
 * DESCRIPTION: Attempt to convert a String/Buffer object to an Integer.
 *
 ******************************************************************************/
acpi_status
acpi_ns_convert_to_integer(union acpi_operand_object *original_object,
			   union acpi_operand_object **return_object)
{
	union acpi_operand_object *new_object;
	acpi_status status;
	u64 value = 0;
	u32 i;

	switch (original_object->common.type) {
	case ACPI_TYPE_STRING:

		/* String-to-Integer conversion */

		status = acpi_ut_strtoul64(original_object->string.pointer,
					   acpi_gbl_integer_byte_width, &value);
		if (ACPI_FAILURE(status)) {
			return (status);
		}
		break;

	case ACPI_TYPE_BUFFER:

		/* Buffer-to-Integer conversion. Max buffer size is 64 bits. */

		if (original_object->buffer.length > 8) {
			return (AE_AML_OPERAND_TYPE);
		}

		/* Extract each buffer byte to create the integer */

		for (i = 0; i < original_object->buffer.length; i++) {
			value |= ((u64)
				  original_object->buffer.pointer[i] << (i *
									 8));
		}
		break;

	default:

		return (AE_AML_OPERAND_TYPE);
	}

	new_object = acpi_ut_create_integer_object(value);
	if (!new_object) {
		return (AE_NO_MEMORY);
	}

	*return_object = new_object;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_convert_to_string
 *
 * PARAMETERS:  original_object     - Object to be converted
 *              return_object       - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful.
 *
 * DESCRIPTION: Attempt to convert a Integer/Buffer object to a String.
 *
 ******************************************************************************/

acpi_status
acpi_ns_convert_to_string(union acpi_operand_object *original_object,
			  union acpi_operand_object **return_object)
{
	union acpi_operand_object *new_object;
	acpi_size length;
	acpi_status status;

	switch (original_object->common.type) {
	case ACPI_TYPE_INTEGER:
		/*
		 * Integer-to-String conversion. Commonly, convert
		 * an integer of value 0 to a NULL string. The last element of
		 * _BIF and _BIX packages occasionally need this fix.
		 */
		if (original_object->integer.value == 0) {

			/* Allocate a new NULL string object */

			new_object = acpi_ut_create_string_object(0);
			if (!new_object) {
				return (AE_NO_MEMORY);
			}
		} else {
			status = acpi_ex_convert_to_string(original_object,
							   &new_object,
							   ACPI_IMPLICIT_CONVERT_HEX);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
		}
		break;

	case ACPI_TYPE_BUFFER:
		/*
		 * Buffer-to-String conversion. Use a to_string
		 * conversion, no transform performed on the buffer data. The best
		 * example of this is the _BIF method, where the string data from
		 * the battery is often (incorrectly) returned as buffer object(s).
		 */
		length = 0;
		while ((length < original_object->buffer.length) &&
		       (original_object->buffer.pointer[length])) {
			length++;
		}

		/* Allocate a new string object */

		new_object = acpi_ut_create_string_object(length);
		if (!new_object) {
			return (AE_NO_MEMORY);
		}

		/*
		 * Copy the raw buffer data with no transform. String is already NULL
		 * terminated at Length+1.
		 */
		memcpy(new_object->string.pointer,
		       original_object->buffer.pointer, length);
		break;

	default:

		return (AE_AML_OPERAND_TYPE);
	}

	*return_object = new_object;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_convert_to_buffer
 *
 * PARAMETERS:  original_object     - Object to be converted
 *              return_object       - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful.
 *
 * DESCRIPTION: Attempt to convert a Integer/String/Package object to a Buffer.
 *
 ******************************************************************************/

acpi_status
acpi_ns_convert_to_buffer(union acpi_operand_object *original_object,
			  union acpi_operand_object **return_object)
{
	union acpi_operand_object *new_object;
	acpi_status status;
	union acpi_operand_object **elements;
	u32 *dword_buffer;
	u32 count;
	u32 i;

	switch (original_object->common.type) {
	case ACPI_TYPE_INTEGER:
		/*
		 * Integer-to-Buffer conversion.
		 * Convert the Integer to a packed-byte buffer. _MAT and other
		 * objects need this sometimes, if a read has been performed on a
		 * Field object that is less than or equal to the global integer
		 * size (32 or 64 bits).
		 */
		status =
		    acpi_ex_convert_to_buffer(original_object, &new_object);
		if (ACPI_FAILURE(status)) {
			return (status);
		}
		break;

	case ACPI_TYPE_STRING:

		/* String-to-Buffer conversion. Simple data copy */

		new_object = acpi_ut_create_buffer_object
		    (original_object->string.length);
		if (!new_object) {
			return (AE_NO_MEMORY);
		}

		memcpy(new_object->buffer.pointer,
		       original_object->string.pointer,
		       original_object->string.length);
		break;

	case ACPI_TYPE_PACKAGE:
		/*
		 * This case is often seen for predefined names that must return a
		 * Buffer object with multiple DWORD integers within. For example,
		 * _FDE and _GTM. The Package can be converted to a Buffer.
		 */

		/* All elements of the Package must be integers */

		elements = original_object->package.elements;
		count = original_object->package.count;

		for (i = 0; i < count; i++) {
			if ((!*elements) ||
			    ((*elements)->common.type != ACPI_TYPE_INTEGER)) {
				return (AE_AML_OPERAND_TYPE);
			}
			elements++;
		}

		/* Create the new buffer object to replace the Package */

		new_object = acpi_ut_create_buffer_object(ACPI_MUL_4(count));
		if (!new_object) {
			return (AE_NO_MEMORY);
		}

		/* Copy the package elements (integers) to the buffer as DWORDs */

		elements = original_object->package.elements;
		dword_buffer = ACPI_CAST_PTR(u32, new_object->buffer.pointer);

		for (i = 0; i < count; i++) {
			*dword_buffer = (u32)(*elements)->integer.value;
			dword_buffer++;
			elements++;
		}
		break;

	default:

		return (AE_AML_OPERAND_TYPE);
	}

	*return_object = new_object;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_convert_to_unicode
 *
 * PARAMETERS:  scope               - Namespace node for the method/object
 *              original_object     - ASCII String Object to be converted
 *              return_object       - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful.
 *
 * DESCRIPTION: Attempt to convert a String object to a Unicode string Buffer.
 *
 ******************************************************************************/

acpi_status
acpi_ns_convert_to_unicode(struct acpi_namespace_node *scope,
			   union acpi_operand_object *original_object,
			   union acpi_operand_object **return_object)
{
	union acpi_operand_object *new_object;
	char *ascii_string;
	u16 *unicode_buffer;
	u32 unicode_length;
	u32 i;

	if (!original_object) {
		return (AE_OK);
	}

	/* If a Buffer was returned, it must be at least two bytes long */

	if (original_object->common.type == ACPI_TYPE_BUFFER) {
		if (original_object->buffer.length < 2) {
			return (AE_AML_OPERAND_VALUE);
		}

		*return_object = NULL;
		return (AE_OK);
	}

	/*
	 * The original object is an ASCII string. Convert this string to
	 * a unicode buffer.
	 */
	ascii_string = original_object->string.pointer;
	unicode_length = (original_object->string.length * 2) + 2;

	/* Create a new buffer object for the Unicode data */

	new_object = acpi_ut_create_buffer_object(unicode_length);
	if (!new_object) {
		return (AE_NO_MEMORY);
	}

	unicode_buffer = ACPI_CAST_PTR(u16, new_object->buffer.pointer);

	/* Convert ASCII to Unicode */

	for (i = 0; i < original_object->string.length; i++) {
		unicode_buffer[i] = (u16)ascii_string[i];
	}

	*return_object = new_object;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_convert_to_resource
 *
 * PARAMETERS:  scope               - Namespace node for the method/object
 *              original_object     - Object to be converted
 *              return_object       - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful
 *
 * DESCRIPTION: Attempt to convert a Integer object to a resource_template
 *              Buffer.
 *
 ******************************************************************************/

acpi_status
acpi_ns_convert_to_resource(struct acpi_namespace_node *scope,
			    union acpi_operand_object *original_object,
			    union acpi_operand_object **return_object)
{
	union acpi_operand_object *new_object;
	u8 *buffer;

	/*
	 * We can fix the following cases for an expected resource template:
	 * 1. No return value (interpreter slack mode is disabled)
	 * 2. A "Return (Zero)" statement
	 * 3. A "Return empty buffer" statement
	 *
	 * We will return a buffer containing a single end_tag
	 * resource descriptor.
	 */
	if (original_object) {
		switch (original_object->common.type) {
		case ACPI_TYPE_INTEGER:

			/* We can only repair an Integer==0 */

			if (original_object->integer.value) {
				return (AE_AML_OPERAND_TYPE);
			}
			break;

		case ACPI_TYPE_BUFFER:

			if (original_object->buffer.length) {

				/* Additional checks can be added in the future */

				*return_object = NULL;
				return (AE_OK);
			}
			break;

		case ACPI_TYPE_STRING:
		default:

			return (AE_AML_OPERAND_TYPE);
		}
	}

	/* Create the new buffer object for the resource descriptor */

	new_object = acpi_ut_create_buffer_object(2);
	if (!new_object) {
		return (AE_NO_MEMORY);
	}

	buffer = ACPI_CAST_PTR(u8, new_object->buffer.pointer);

	/* Initialize the Buffer with a single end_tag descriptor */

	buffer[0] = (ACPI_RESOURCE_NAME_END_TAG | ASL_RDESC_END_TAG_SIZE);
	buffer[1] = 0x00;

	*return_object = new_object;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_convert_to_reference
 *
 * PARAMETERS:  scope               - Namespace node for the method/object
 *              original_object     - Object to be converted
 *              return_object       - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful
 *
 * DESCRIPTION: Attempt to convert a Integer object to a object_reference.
 *              Buffer.
 *
 ******************************************************************************/

acpi_status
acpi_ns_convert_to_reference(struct acpi_namespace_node *scope,
			     union acpi_operand_object *original_object,
			     union acpi_operand_object **return_object)
{
	union acpi_operand_object *new_object = NULL;
	acpi_status status;
	struct acpi_namespace_node *node;
	union acpi_generic_state scope_info;
	char *name;

	ACPI_FUNCTION_NAME(ns_convert_to_reference);

	/* Convert path into internal presentation */

	status =
	    acpi_ns_internalize_name(original_object->string.pointer, &name);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Find the namespace node */

	scope_info.scope.node =
	    ACPI_CAST_PTR(struct acpi_namespace_node, scope);
	status =
	    acpi_ns_lookup(&scope_info, name, ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
			   ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE,
			   NULL, &node);
	if (ACPI_FAILURE(status)) {

		/* Check if we are resolving a named reference within a package */

		ACPI_ERROR_NAMESPACE(original_object->string.pointer, status);
		goto error_exit;
	}

	/* Create and init a new internal ACPI object */

	new_object = acpi_ut_create_internal_object(ACPI_TYPE_LOCAL_REFERENCE);
	if (!new_object) {
		status = AE_NO_MEMORY;
		goto error_exit;
	}
	new_object->reference.node = node;
	new_object->reference.object = node->object;
	new_object->reference.class = ACPI_REFCLASS_NAME;

	/*
	 * Increase reference of the object if needed (the object is likely a
	 * null for device nodes).
	 */
	acpi_ut_add_reference(node->object);

error_exit:
	ACPI_FREE(name);
	*return_object = new_object;
	return (AE_OK);
}
