/******************************************************************************
 *
 * Module Name: nsrepair - Repair for objects returned by predefined methods
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2009, Intel Corp.
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
#include "acpredef.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsrepair")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_object
 *
 * PARAMETERS:  Data                - Pointer to validation data structure
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
	acpi_size length;

	switch (return_object->common.type) {
	case ACPI_TYPE_BUFFER:

		/* Does the method/object legally return a string? */

		if (!(expected_btypes & ACPI_RTYPE_STRING)) {
			return (AE_AML_OPERAND_TYPE);
		}

		/*
		 * Have a Buffer, expected a String, convert. Use a to_string
		 * conversion, no transform performed on the buffer data. The best
		 * example of this is the _BIF method, where the string data from
		 * the battery is often (incorrectly) returned as buffer object(s).
		 */
		length = 0;
		while ((length < return_object->buffer.length) &&
		       (return_object->buffer.pointer[length])) {
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
			    return_object->buffer.pointer, length);

		/*
		 * If the original object is a package element, we need to:
		 * 1. Set the reference count of the new object to match the
		 *    reference count of the old object.
		 * 2. Decrement the reference count of the original object.
		 */
		if (package_index != ACPI_NOT_PACKAGE_ELEMENT) {
			new_object->common.reference_count =
			    return_object->common.reference_count;

			if (return_object->common.reference_count > 1) {
				return_object->common.reference_count--;
			}

			ACPI_WARN_PREDEFINED((AE_INFO, data->pathname,
					      data->node_flags,
					      "Converted Buffer to expected String at index %u",
					      package_index));
		} else {
			ACPI_WARN_PREDEFINED((AE_INFO, data->pathname,
					      data->node_flags,
					      "Converted Buffer to expected String"));
		}

		/* Delete old object, install the new return object */

		acpi_ut_remove_reference(return_object);
		*return_object_ptr = new_object;
		data->flags |= ACPI_OBJECT_REPAIRED;
		return (AE_OK);

	default:
		break;
	}

	return (AE_AML_OPERAND_TYPE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_package_list
 *
 * PARAMETERS:  Data                - Pointer to validation data structure
 *              obj_desc_ptr        - Pointer to the object to repair. The new
 *                                    package object is returned here,
 *                                    overwriting the old object.
 *
 * RETURN:      Status, new object in *obj_desc_ptr
 *
 * DESCRIPTION: Repair a common problem with objects that are defined to return
 *              a variable-length Package of Packages. If the variable-length
 *              is one, some BIOS code mistakenly simply declares a single
 *              Package instead of a Package with one sub-Package. This
 *              function attempts to repair this error by wrapping a Package
 *              object around the original Package, creating the correct
 *              Package with one sub-Package.
 *
 *              Names that can be repaired in this manner include:
 *              _ALR, _CSD, _HPX, _MLS, _PRT, _PSS, _TRT, TSS
 *
 ******************************************************************************/

acpi_status
acpi_ns_repair_package_list(struct acpi_predefined_data *data,
			    union acpi_operand_object **obj_desc_ptr)
{
	union acpi_operand_object *pkg_obj_desc;

	/*
	 * Create the new outer package and populate it. The new package will
	 * have a single element, the lone subpackage.
	 */
	pkg_obj_desc = acpi_ut_create_package_object(1);
	if (!pkg_obj_desc) {
		return (AE_NO_MEMORY);
	}

	pkg_obj_desc->package.elements[0] = *obj_desc_ptr;

	/* Return the new object in the object pointer */

	*obj_desc_ptr = pkg_obj_desc;
	data->flags |= ACPI_OBJECT_REPAIRED;

	ACPI_WARN_PREDEFINED((AE_INFO, data->pathname, data->node_flags,
			      "Incorrectly formed Package, attempting repair"));

	return (AE_OK);
}
