/******************************************************************************
 *
 * Module Name: nsrepair - Repair for objects returned by predefined methods
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2013, Intel Corp.
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

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsrepair")

/*******************************************************************************
 *
 * This module attempts to repair or convert objects returned by the
 * predefined methods to an object type that is expected, as per the ACPI
 * specification. The need for this code is dictated by the many machines that
 * return incorrect types for the standard predefined methods. Performing these
 * conversions here, in one place, eliminates the need for individual ACPI
 * device drivers to do the same. Note: Most of these conversions are different
 * than the internal object conversion routines used for implicit object
 * conversion.
 *
 * The following conversions can be performed as necessary:
 *
 * Integer -> String
 * Integer -> Buffer
 * String  -> Integer
 * String  -> Buffer
 * Buffer  -> Integer
 * Buffer  -> String
 * Buffer  -> Package of Integers
 * Package -> Package of one Package
 * An incorrect standalone object is wrapped with required outer package
 *
 * Additional possible repairs:
 * Required package elements that are NULL replaced by Integer/String/Buffer
 *
 ******************************************************************************/
/* Local prototypes */
static acpi_status
acpi_ns_convert_to_integer(union acpi_operand_object *original_object,
			   union acpi_operand_object **return_object);

static acpi_status
acpi_ns_convert_to_string(union acpi_operand_object *original_object,
			  union acpi_operand_object **return_object);

static acpi_status
acpi_ns_convert_to_buffer(union acpi_operand_object *original_object,
			  union acpi_operand_object **return_object);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_object
 *
 * PARAMETERS:  data                - Pointer to validation data structure
 *              expected_btypes     - Object types expected
 *              package_index       - Index of object within parent package (if
 *                                    applicable - ACPI_NOT_PACKAGE_ELEMENT
 *                                    otherwise)
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if repair was successful.
 *
 * DESCRIPTION: Attempt to repair/convert a return object of a type that was
 *              not expected.
 *
 ******************************************************************************/

acpi_status
acpi_ns_repair_object(struct acpi_predefined_data *data,
		      u32 expected_btypes,
		      u32 package_index,
		      union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	union acpi_operand_object *new_object;
	acpi_status status;

	ACPI_FUNCTION_NAME(ns_repair_object);

	/*
	 * At this point, we know that the type of the returned object was not
	 * one of the expected types for this predefined name. Attempt to
	 * repair the object by converting it to one of the expected object
	 * types for this predefined name.
	 */
	if (expected_btypes & ACPI_RTYPE_INTEGER) {
		status = acpi_ns_convert_to_integer(return_object, &new_object);
		if (ACPI_SUCCESS(status)) {
			goto object_repaired;
		}
	}
	if (expected_btypes & ACPI_RTYPE_STRING) {
		status = acpi_ns_convert_to_string(return_object, &new_object);
		if (ACPI_SUCCESS(status)) {
			goto object_repaired;
		}
	}
	if (expected_btypes & ACPI_RTYPE_BUFFER) {
		status = acpi_ns_convert_to_buffer(return_object, &new_object);
		if (ACPI_SUCCESS(status)) {
			goto object_repaired;
		}
	}
	if (expected_btypes & ACPI_RTYPE_PACKAGE) {
		/*
		 * A package is expected. We will wrap the existing object with a
		 * new package object. It is often the case that if a variable-length
		 * package is required, but there is only a single object needed, the
		 * BIOS will return that object instead of wrapping it with a Package
		 * object. Note: after the wrapping, the package will be validated
		 * for correct contents (expected object type or types).
		 */
		status =
		    acpi_ns_wrap_with_package(data, return_object, &new_object);
		if (ACPI_SUCCESS(status)) {
			/*
			 * The original object just had its reference count
			 * incremented for being inserted into the new package.
			 */
			*return_object_ptr = new_object;	/* New Package object */
			data->flags |= ACPI_OBJECT_REPAIRED;
			return (AE_OK);
		}
	}

	/* We cannot repair this object */

	return (AE_AML_OPERAND_TYPE);

      object_repaired:

	/* Object was successfully repaired */

	if (package_index != ACPI_NOT_PACKAGE_ELEMENT) {
		/*
		 * The original object is a package element. We need to
		 * decrement the reference count of the original object,
		 * for removing it from the package.
		 *
		 * However, if the original object was just wrapped with a
		 * package object as part of the repair, we don't need to
		 * change the reference count.
		 */
		if (!(data->flags & ACPI_OBJECT_WRAPPED)) {
			new_object->common.reference_count =
			    return_object->common.reference_count;

			if (return_object->common.reference_count > 1) {
				return_object->common.reference_count--;
			}
		}

		ACPI_DEBUG_PRINT((ACPI_DB_REPAIR,
				  "%s: Converted %s to expected %s at Package index %u\n",
				  data->pathname,
				  acpi_ut_get_object_type_name(return_object),
				  acpi_ut_get_object_type_name(new_object),
				  package_index));
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_REPAIR,
				  "%s: Converted %s to expected %s\n",
				  data->pathname,
				  acpi_ut_get_object_type_name(return_object),
				  acpi_ut_get_object_type_name(new_object)));
	}

	/* Delete old object, install the new return object */

	acpi_ut_remove_reference(return_object);
	*return_object_ptr = new_object;
	data->flags |= ACPI_OBJECT_REPAIRED;
	return (AE_OK);
}

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

static acpi_status
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
					   ACPI_ANY_BASE, &value);
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
			value |=
			    ((u64) original_object->buffer.
			     pointer[i] << (i * 8));
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

static acpi_status
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
			status =
			    acpi_ex_convert_to_string(original_object,
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
		ACPI_MEMCPY(new_object->string.pointer,
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

static acpi_status
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

		new_object =
		    acpi_ut_create_buffer_object(original_object->string.
						 length);
		if (!new_object) {
			return (AE_NO_MEMORY);
		}

		ACPI_MEMCPY(new_object->buffer.pointer,
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
			*dword_buffer = (u32) (*elements)->integer.value;
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
 * FUNCTION:    acpi_ns_repair_null_element
 *
 * PARAMETERS:  data                - Pointer to validation data structure
 *              expected_btypes     - Object types expected
 *              package_index       - Index of object within parent package (if
 *                                    applicable - ACPI_NOT_PACKAGE_ELEMENT
 *                                    otherwise)
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if repair was successful.
 *
 * DESCRIPTION: Attempt to repair a NULL element of a returned Package object.
 *
 ******************************************************************************/

acpi_status
acpi_ns_repair_null_element(struct acpi_predefined_data *data,
			    u32 expected_btypes,
			    u32 package_index,
			    union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	union acpi_operand_object *new_object;

	ACPI_FUNCTION_NAME(ns_repair_null_element);

	/* No repair needed if return object is non-NULL */

	if (return_object) {
		return (AE_OK);
	}

	/*
	 * Attempt to repair a NULL element of a Package object. This applies to
	 * predefined names that return a fixed-length package and each element
	 * is required. It does not apply to variable-length packages where NULL
	 * elements are allowed, especially at the end of the package.
	 */
	if (expected_btypes & ACPI_RTYPE_INTEGER) {

		/* Need an integer - create a zero-value integer */

		new_object = acpi_ut_create_integer_object((u64)0);
	} else if (expected_btypes & ACPI_RTYPE_STRING) {

		/* Need a string - create a NULL string */

		new_object = acpi_ut_create_string_object(0);
	} else if (expected_btypes & ACPI_RTYPE_BUFFER) {

		/* Need a buffer - create a zero-length buffer */

		new_object = acpi_ut_create_buffer_object(0);
	} else {
		/* Error for all other expected types */

		return (AE_AML_OPERAND_TYPE);
	}

	if (!new_object) {
		return (AE_NO_MEMORY);
	}

	/* Set the reference count according to the parent Package object */

	new_object->common.reference_count =
	    data->parent_package->common.reference_count;

	ACPI_DEBUG_PRINT((ACPI_DB_REPAIR,
			  "%s: Converted NULL package element to expected %s at index %u\n",
			  data->pathname,
			  acpi_ut_get_object_type_name(new_object),
			  package_index));

	*return_object_ptr = new_object;
	data->flags |= ACPI_OBJECT_REPAIRED;
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_remove_null_elements
 *
 * PARAMETERS:  data                - Pointer to validation data structure
 *              package_type        - An acpi_return_package_types value
 *              obj_desc            - A Package object
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Remove all NULL package elements from packages that contain
 *              a variable number of sub-packages. For these types of
 *              packages, NULL elements can be safely removed.
 *
 *****************************************************************************/

void
acpi_ns_remove_null_elements(struct acpi_predefined_data *data,
			     u8 package_type,
			     union acpi_operand_object *obj_desc)
{
	union acpi_operand_object **source;
	union acpi_operand_object **dest;
	u32 count;
	u32 new_count;
	u32 i;

	ACPI_FUNCTION_NAME(ns_remove_null_elements);

	/*
	 * We can safely remove all NULL elements from these package types:
	 * PTYPE1_VAR packages contain a variable number of simple data types.
	 * PTYPE2 packages contain a variable number of sub-packages.
	 */
	switch (package_type) {
	case ACPI_PTYPE1_VAR:
	case ACPI_PTYPE2:
	case ACPI_PTYPE2_COUNT:
	case ACPI_PTYPE2_PKG_COUNT:
	case ACPI_PTYPE2_FIXED:
	case ACPI_PTYPE2_MIN:
	case ACPI_PTYPE2_REV_FIXED:
	case ACPI_PTYPE2_FIX_VAR:
		break;

	default:
	case ACPI_PTYPE1_FIXED:
	case ACPI_PTYPE1_OPTION:
		return;
	}

	count = obj_desc->package.count;
	new_count = count;

	source = obj_desc->package.elements;
	dest = source;

	/* Examine all elements of the package object, remove nulls */

	for (i = 0; i < count; i++) {
		if (!*source) {
			new_count--;
		} else {
			*dest = *source;
			dest++;
		}
		source++;
	}

	/* Update parent package if any null elements were removed */

	if (new_count < count) {
		ACPI_DEBUG_PRINT((ACPI_DB_REPAIR,
				  "%s: Found and removed %u NULL elements\n",
				  data->pathname, (count - new_count)));

		/* NULL terminate list and update the package count */

		*dest = NULL;
		obj_desc->package.count = new_count;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_wrap_with_package
 *
 * PARAMETERS:  data                - Pointer to validation data structure
 *              original_object     - Pointer to the object to repair.
 *              obj_desc_ptr        - The new package object is returned here
 *
 * RETURN:      Status, new object in *obj_desc_ptr
 *
 * DESCRIPTION: Repair a common problem with objects that are defined to
 *              return a variable-length Package of sub-objects. If there is
 *              only one sub-object, some BIOS code mistakenly simply declares
 *              the single object instead of a Package with one sub-object.
 *              This function attempts to repair this error by wrapping a
 *              Package object around the original object, creating the
 *              correct and expected Package with one sub-object.
 *
 *              Names that can be repaired in this manner include:
 *              _ALR, _CSD, _HPX, _MLS, _PLD, _PRT, _PSS, _TRT, _TSS,
 *              _BCL, _DOD, _FIX, _Sx
 *
 ******************************************************************************/

acpi_status
acpi_ns_wrap_with_package(struct acpi_predefined_data *data,
			  union acpi_operand_object *original_object,
			  union acpi_operand_object **obj_desc_ptr)
{
	union acpi_operand_object *pkg_obj_desc;

	ACPI_FUNCTION_NAME(ns_wrap_with_package);

	/*
	 * Create the new outer package and populate it. The new package will
	 * have a single element, the lone sub-object.
	 */
	pkg_obj_desc = acpi_ut_create_package_object(1);
	if (!pkg_obj_desc) {
		return (AE_NO_MEMORY);
	}

	pkg_obj_desc->package.elements[0] = original_object;

	ACPI_DEBUG_PRINT((ACPI_DB_REPAIR,
			  "%s: Wrapped %s with expected Package object\n",
			  data->pathname,
			  acpi_ut_get_object_type_name(original_object)));

	/* Return the new object in the object pointer */

	*obj_desc_ptr = pkg_obj_desc;
	data->flags |= ACPI_OBJECT_REPAIRED | ACPI_OBJECT_WRAPPED;
	return (AE_OK);
}
