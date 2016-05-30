/******************************************************************************
 *
 * Module Name: utobject - ACPI object create/delete/size/cache routines
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

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utobject")

/* Local prototypes */
static acpi_status
acpi_ut_get_simple_object_size(union acpi_operand_object *obj,
			       acpi_size *obj_length);

static acpi_status
acpi_ut_get_package_object_size(union acpi_operand_object *obj,
				acpi_size *obj_length);

static acpi_status
acpi_ut_get_element_length(u8 object_type,
			   union acpi_operand_object *source_object,
			   union acpi_generic_state *state, void *context);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_internal_object_dbg
 *
 * PARAMETERS:  module_name         - Source file name of caller
 *              line_number         - Line number of caller
 *              component_id        - Component type of caller
 *              type                - ACPI Type of the new object
 *
 * RETURN:      A new internal object, null on failure
 *
 * DESCRIPTION: Create and initialize a new internal object.
 *
 * NOTE:        We always allocate the worst-case object descriptor because
 *              these objects are cached, and we want them to be
 *              one-size-satisifies-any-request. This in itself may not be
 *              the most memory efficient, but the efficiency of the object
 *              cache should more than make up for this!
 *
 ******************************************************************************/

union acpi_operand_object *acpi_ut_create_internal_object_dbg(const char
							      *module_name,
							      u32 line_number,
							      u32 component_id,
							      acpi_object_type
							      type)
{
	union acpi_operand_object *object;
	union acpi_operand_object *second_object;

	ACPI_FUNCTION_TRACE_STR(ut_create_internal_object_dbg,
				acpi_ut_get_type_name(type));

	/* Allocate the raw object descriptor */

	object =
	    acpi_ut_allocate_object_desc_dbg(module_name, line_number,
					     component_id);
	if (!object) {
		return_PTR(NULL);
	}

	switch (type) {
	case ACPI_TYPE_REGION:
	case ACPI_TYPE_BUFFER_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:

		/* These types require a secondary object */

		second_object =
		    acpi_ut_allocate_object_desc_dbg(module_name, line_number,
						     component_id);
		if (!second_object) {
			acpi_ut_delete_object_desc(object);
			return_PTR(NULL);
		}

		second_object->common.type = ACPI_TYPE_LOCAL_EXTRA;
		second_object->common.reference_count = 1;

		/* Link the second object to the first */

		object->common.next_object = second_object;
		break;

	default:

		/* All others have no secondary object */
		break;
	}

	/* Save the object type in the object descriptor */

	object->common.type = (u8) type;

	/* Init the reference count */

	object->common.reference_count = 1;

	/* Any per-type initialization should go here */

	return_PTR(object);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_package_object
 *
 * PARAMETERS:  count               - Number of package elements
 *
 * RETURN:      Pointer to a new Package object, null on failure
 *
 * DESCRIPTION: Create a fully initialized package object
 *
 ******************************************************************************/

union acpi_operand_object *acpi_ut_create_package_object(u32 count)
{
	union acpi_operand_object *package_desc;
	union acpi_operand_object **package_elements;

	ACPI_FUNCTION_TRACE_U32(ut_create_package_object, count);

	/* Create a new Package object */

	package_desc = acpi_ut_create_internal_object(ACPI_TYPE_PACKAGE);
	if (!package_desc) {
		return_PTR(NULL);
	}

	/*
	 * Create the element array. Count+1 allows the array to be null
	 * terminated.
	 */
	package_elements = ACPI_ALLOCATE_ZEROED(((acpi_size)count +
						 1) * sizeof(void *));
	if (!package_elements) {
		ACPI_FREE(package_desc);
		return_PTR(NULL);
	}

	package_desc->package.count = count;
	package_desc->package.elements = package_elements;
	return_PTR(package_desc);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_integer_object
 *
 * PARAMETERS:  initial_value       - Initial value for the integer
 *
 * RETURN:      Pointer to a new Integer object, null on failure
 *
 * DESCRIPTION: Create an initialized integer object
 *
 ******************************************************************************/

union acpi_operand_object *acpi_ut_create_integer_object(u64 initial_value)
{
	union acpi_operand_object *integer_desc;

	ACPI_FUNCTION_TRACE(ut_create_integer_object);

	/* Create and initialize a new integer object */

	integer_desc = acpi_ut_create_internal_object(ACPI_TYPE_INTEGER);
	if (!integer_desc) {
		return_PTR(NULL);
	}

	integer_desc->integer.value = initial_value;
	return_PTR(integer_desc);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_buffer_object
 *
 * PARAMETERS:  buffer_size            - Size of buffer to be created
 *
 * RETURN:      Pointer to a new Buffer object, null on failure
 *
 * DESCRIPTION: Create a fully initialized buffer object
 *
 ******************************************************************************/

union acpi_operand_object *acpi_ut_create_buffer_object(acpi_size buffer_size)
{
	union acpi_operand_object *buffer_desc;
	u8 *buffer = NULL;

	ACPI_FUNCTION_TRACE_U32(ut_create_buffer_object, buffer_size);

	/* Create a new Buffer object */

	buffer_desc = acpi_ut_create_internal_object(ACPI_TYPE_BUFFER);
	if (!buffer_desc) {
		return_PTR(NULL);
	}

	/* Create an actual buffer only if size > 0 */

	if (buffer_size > 0) {

		/* Allocate the actual buffer */

		buffer = ACPI_ALLOCATE_ZEROED(buffer_size);
		if (!buffer) {
			ACPI_ERROR((AE_INFO, "Could not allocate size %u",
				    (u32)buffer_size));

			acpi_ut_remove_reference(buffer_desc);
			return_PTR(NULL);
		}
	}

	/* Complete buffer object initialization */

	buffer_desc->buffer.flags |= AOPOBJ_DATA_VALID;
	buffer_desc->buffer.pointer = buffer;
	buffer_desc->buffer.length = (u32) buffer_size;

	/* Return the new buffer descriptor */

	return_PTR(buffer_desc);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_string_object
 *
 * PARAMETERS:  string_size         - Size of string to be created. Does not
 *                                    include NULL terminator, this is added
 *                                    automatically.
 *
 * RETURN:      Pointer to a new String object
 *
 * DESCRIPTION: Create a fully initialized string object
 *
 ******************************************************************************/

union acpi_operand_object *acpi_ut_create_string_object(acpi_size string_size)
{
	union acpi_operand_object *string_desc;
	char *string;

	ACPI_FUNCTION_TRACE_U32(ut_create_string_object, string_size);

	/* Create a new String object */

	string_desc = acpi_ut_create_internal_object(ACPI_TYPE_STRING);
	if (!string_desc) {
		return_PTR(NULL);
	}

	/*
	 * Allocate the actual string buffer -- (Size + 1) for NULL terminator.
	 * NOTE: Zero-length strings are NULL terminated
	 */
	string = ACPI_ALLOCATE_ZEROED(string_size + 1);
	if (!string) {
		ACPI_ERROR((AE_INFO, "Could not allocate size %u",
			    (u32)string_size));

		acpi_ut_remove_reference(string_desc);
		return_PTR(NULL);
	}

	/* Complete string object initialization */

	string_desc->string.pointer = string;
	string_desc->string.length = (u32) string_size;

	/* Return the new string descriptor */

	return_PTR(string_desc);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_internal_object
 *
 * PARAMETERS:  object              - Object to be validated
 *
 * RETURN:      TRUE if object is valid, FALSE otherwise
 *
 * DESCRIPTION: Validate a pointer to be of type union acpi_operand_object
 *
 ******************************************************************************/

u8 acpi_ut_valid_internal_object(void *object)
{

	ACPI_FUNCTION_NAME(ut_valid_internal_object);

	/* Check for a null pointer */

	if (!object) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "**** Null Object Ptr\n"));
		return (FALSE);
	}

	/* Check the descriptor type field */

	switch (ACPI_GET_DESCRIPTOR_TYPE(object)) {
	case ACPI_DESC_TYPE_OPERAND:

		/* The object appears to be a valid union acpi_operand_object */

		return (TRUE);

	default:

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "%p is not an ACPI operand obj [%s]\n",
				  object, acpi_ut_get_descriptor_name(object)));
		break;
	}

	return (FALSE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_allocate_object_desc_dbg
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              component_id        - Caller's component ID (for error output)
 *
 * RETURN:      Pointer to newly allocated object descriptor. Null on error
 *
 * DESCRIPTION: Allocate a new object descriptor. Gracefully handle
 *              error conditions.
 *
 ******************************************************************************/

void *acpi_ut_allocate_object_desc_dbg(const char *module_name,
				       u32 line_number, u32 component_id)
{
	union acpi_operand_object *object;

	ACPI_FUNCTION_TRACE(ut_allocate_object_desc_dbg);

	object = acpi_os_acquire_object(acpi_gbl_operand_cache);
	if (!object) {
		ACPI_ERROR((module_name, line_number,
			    "Could not allocate an object descriptor"));

		return_PTR(NULL);
	}

	/* Mark the descriptor type */

	ACPI_SET_DESCRIPTOR_TYPE(object, ACPI_DESC_TYPE_OPERAND);

	ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS, "%p Size %X\n",
			  object, (u32) sizeof(union acpi_operand_object)));

	return_PTR(object);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_delete_object_desc
 *
 * PARAMETERS:  object          - An Acpi internal object to be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Free an ACPI object descriptor or add it to the object cache
 *
 ******************************************************************************/

void acpi_ut_delete_object_desc(union acpi_operand_object *object)
{
	ACPI_FUNCTION_TRACE_PTR(ut_delete_object_desc, object);

	/* Object must be of type union acpi_operand_object */

	if (ACPI_GET_DESCRIPTOR_TYPE(object) != ACPI_DESC_TYPE_OPERAND) {
		ACPI_ERROR((AE_INFO,
			    "%p is not an ACPI Operand object [%s]", object,
			    acpi_ut_get_descriptor_name(object)));
		return_VOID;
	}

	(void)acpi_os_release_object(acpi_gbl_operand_cache, object);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_simple_object_size
 *
 * PARAMETERS:  internal_object    - An ACPI operand object
 *              obj_length         - Where the length is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to determine the space required to
 *              contain a simple object for return to an external user.
 *
 *              The length includes the object structure plus any additional
 *              needed space.
 *
 ******************************************************************************/

static acpi_status
acpi_ut_get_simple_object_size(union acpi_operand_object *internal_object,
			       acpi_size *obj_length)
{
	acpi_size length;
	acpi_size size;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE_PTR(ut_get_simple_object_size, internal_object);

	/* Start with the length of the (external) Acpi object */

	length = sizeof(union acpi_object);

	/* A NULL object is allowed, can be a legal uninitialized package element */

	if (!internal_object) {
	/*
		 * Object is NULL, just return the length of union acpi_object
		 * (A NULL union acpi_object is an object of all zeroes.)
	 */
		*obj_length = ACPI_ROUND_UP_TO_NATIVE_WORD(length);
		return_ACPI_STATUS(AE_OK);
	}

	/* A Namespace Node should never appear here */

	if (ACPI_GET_DESCRIPTOR_TYPE(internal_object) == ACPI_DESC_TYPE_NAMED) {

		/* A namespace node should never get here */

		return_ACPI_STATUS(AE_AML_INTERNAL);
	}

	/*
	 * The final length depends on the object type
	 * Strings and Buffers are packed right up against the parent object and
	 * must be accessed bytewise or there may be alignment problems on
	 * certain processors
	 */
	switch (internal_object->common.type) {
	case ACPI_TYPE_STRING:

		length += (acpi_size)internal_object->string.length + 1;
		break;

	case ACPI_TYPE_BUFFER:

		length += (acpi_size)internal_object->buffer.length;
		break;

	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_POWER:

		/* No extra data for these types */

		break;

	case ACPI_TYPE_LOCAL_REFERENCE:

		switch (internal_object->reference.class) {
		case ACPI_REFCLASS_NAME:
			/*
			 * Get the actual length of the full pathname to this object.
			 * The reference will be converted to the pathname to the object
			 */
			size =
			    acpi_ns_get_pathname_length(internal_object->
							reference.node);
			if (!size) {
				return_ACPI_STATUS(AE_BAD_PARAMETER);
			}

			length += ACPI_ROUND_UP_TO_NATIVE_WORD(size);
			break;

		default:
			/*
			 * No other reference opcodes are supported.
			 * Notably, Locals and Args are not supported, but this may be
			 * required eventually.
			 */
			ACPI_ERROR((AE_INFO,
				    "Cannot convert to external object - "
				    "unsupported Reference Class [%s] 0x%X in object %p",
				    acpi_ut_get_reference_name(internal_object),
				    internal_object->reference.class,
				    internal_object));
			status = AE_TYPE;
			break;
		}
		break;

	default:

		ACPI_ERROR((AE_INFO, "Cannot convert to external object - "
			    "unsupported type [%s] 0x%X in object %p",
			    acpi_ut_get_object_type_name(internal_object),
			    internal_object->common.type, internal_object));
		status = AE_TYPE;
		break;
	}

	/*
	 * Account for the space required by the object rounded up to the next
	 * multiple of the machine word size. This keeps each object aligned
	 * on a machine word boundary. (preventing alignment faults on some
	 * machines.)
	 */
	*obj_length = ACPI_ROUND_UP_TO_NATIVE_WORD(length);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_element_length
 *
 * PARAMETERS:  acpi_pkg_callback
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the length of one package element.
 *
 ******************************************************************************/

static acpi_status
acpi_ut_get_element_length(u8 object_type,
			   union acpi_operand_object *source_object,
			   union acpi_generic_state *state, void *context)
{
	acpi_status status = AE_OK;
	struct acpi_pkg_info *info = (struct acpi_pkg_info *)context;
	acpi_size object_space;

	switch (object_type) {
	case ACPI_COPY_TYPE_SIMPLE:
		/*
		 * Simple object - just get the size (Null object/entry is handled
		 * here also) and sum it into the running package length
		 */
		status =
		    acpi_ut_get_simple_object_size(source_object,
						   &object_space);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		info->length += object_space;
		break;

	case ACPI_COPY_TYPE_PACKAGE:

		/* Package object - nothing much to do here, let the walk handle it */

		info->num_packages++;
		state->pkg.this_target_obj = NULL;
		break;

	default:

		/* No other types allowed */

		return (AE_BAD_PARAMETER);
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_package_object_size
 *
 * PARAMETERS:  internal_object     - An ACPI internal object
 *              obj_length          - Where the length is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to determine the space required to
 *              contain a package object for return to an external user.
 *
 *              This is moderately complex since a package contains other
 *              objects including packages.
 *
 ******************************************************************************/

static acpi_status
acpi_ut_get_package_object_size(union acpi_operand_object *internal_object,
				acpi_size *obj_length)
{
	acpi_status status;
	struct acpi_pkg_info info;

	ACPI_FUNCTION_TRACE_PTR(ut_get_package_object_size, internal_object);

	info.length = 0;
	info.object_space = 0;
	info.num_packages = 1;

	status =
	    acpi_ut_walk_package_tree(internal_object, NULL,
				      acpi_ut_get_element_length, &info);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * We have handled all of the objects in all levels of the package.
	 * just add the length of the package objects themselves.
	 * Round up to the next machine word.
	 */
	info.length +=
	    ACPI_ROUND_UP_TO_NATIVE_WORD(sizeof(union acpi_object)) *
	    (acpi_size)info.num_packages;

	/* Return the total package length */

	*obj_length = info.length;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_object_size
 *
 * PARAMETERS:  internal_object     - An ACPI internal object
 *              obj_length          - Where the length will be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to determine the space required to
 *              contain an object for return to an API user.
 *
 ******************************************************************************/

acpi_status
acpi_ut_get_object_size(union acpi_operand_object *internal_object,
			acpi_size *obj_length)
{
	acpi_status status;

	ACPI_FUNCTION_ENTRY();

	if ((ACPI_GET_DESCRIPTOR_TYPE(internal_object) ==
	     ACPI_DESC_TYPE_OPERAND) &&
	    (internal_object->common.type == ACPI_TYPE_PACKAGE)) {
		status =
		    acpi_ut_get_package_object_size(internal_object,
						    obj_length);
	} else {
		status =
		    acpi_ut_get_simple_object_size(internal_object, obj_length);
	}

	return (status);
}
