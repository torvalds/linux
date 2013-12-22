/******************************************************************************
 *
 * Module Name: utids - support for device Ids - HID, UID, CID
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
#include "acinterp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utids")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_execute_HID
 *
 * PARAMETERS:  device_node         - Node for the device
 *              return_id           - Where the string HID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _HID control method that returns the hardware
 *              ID of the device. The HID is either an 32-bit encoded EISAID
 *              Integer or a String. A string is always returned. An EISAID
 *              is converted to a string.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/
acpi_status
acpi_ut_execute_HID(struct acpi_namespace_node *device_node,
		    struct acpi_pnp_device_id **return_id)
{
	union acpi_operand_object *obj_desc;
	struct acpi_pnp_device_id *hid;
	u32 length;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ut_execute_HID);

	status = acpi_ut_evaluate_object(device_node, METHOD_NAME__HID,
					 ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING,
					 &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Get the size of the String to be returned, includes null terminator */

	if (obj_desc->common.type == ACPI_TYPE_INTEGER) {
		length = ACPI_EISAID_STRING_SIZE;
	} else {
		length = obj_desc->string.length + 1;
	}

	/* Allocate a buffer for the HID */

	hid =
	    ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_pnp_device_id) +
				 (acpi_size) length);
	if (!hid) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Area for the string starts after PNP_DEVICE_ID struct */

	hid->string =
	    ACPI_ADD_PTR(char, hid, sizeof(struct acpi_pnp_device_id));

	/* Convert EISAID to a string or simply copy existing string */

	if (obj_desc->common.type == ACPI_TYPE_INTEGER) {
		acpi_ex_eisa_id_to_string(hid->string, obj_desc->integer.value);
	} else {
		ACPI_STRCPY(hid->string, obj_desc->string.pointer);
	}

	hid->length = length;
	*return_id = hid;

cleanup:

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_execute_SUB
 *
 * PARAMETERS:  device_node         - Node for the device
 *              return_id           - Where the _SUB is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _SUB control method that returns the subsystem
 *              ID of the device. The _SUB value is always a string containing
 *              either a valid PNP or ACPI ID.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_SUB(struct acpi_namespace_node *device_node,
		    struct acpi_pnp_device_id **return_id)
{
	union acpi_operand_object *obj_desc;
	struct acpi_pnp_device_id *sub;
	u32 length;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ut_execute_SUB);

	status = acpi_ut_evaluate_object(device_node, METHOD_NAME__SUB,
					 ACPI_BTYPE_STRING, &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Get the size of the String to be returned, includes null terminator */

	length = obj_desc->string.length + 1;

	/* Allocate a buffer for the SUB */

	sub =
	    ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_pnp_device_id) +
				 (acpi_size) length);
	if (!sub) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Area for the string starts after PNP_DEVICE_ID struct */

	sub->string =
	    ACPI_ADD_PTR(char, sub, sizeof(struct acpi_pnp_device_id));

	/* Simply copy existing string */

	ACPI_STRCPY(sub->string, obj_desc->string.pointer);
	sub->length = length;
	*return_id = sub;

cleanup:

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_execute_UID
 *
 * PARAMETERS:  device_node         - Node for the device
 *              return_id           - Where the string UID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _UID control method that returns the unique
 *              ID of the device. The UID is either a 64-bit Integer (NOT an
 *              EISAID) or a string. Always returns a string. A 64-bit integer
 *              is converted to a decimal string.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_UID(struct acpi_namespace_node *device_node,
		    struct acpi_pnp_device_id **return_id)
{
	union acpi_operand_object *obj_desc;
	struct acpi_pnp_device_id *uid;
	u32 length;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ut_execute_UID);

	status = acpi_ut_evaluate_object(device_node, METHOD_NAME__UID,
					 ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING,
					 &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Get the size of the String to be returned, includes null terminator */

	if (obj_desc->common.type == ACPI_TYPE_INTEGER) {
		length = ACPI_MAX64_DECIMAL_DIGITS + 1;
	} else {
		length = obj_desc->string.length + 1;
	}

	/* Allocate a buffer for the UID */

	uid =
	    ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_pnp_device_id) +
				 (acpi_size) length);
	if (!uid) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Area for the string starts after PNP_DEVICE_ID struct */

	uid->string =
	    ACPI_ADD_PTR(char, uid, sizeof(struct acpi_pnp_device_id));

	/* Convert an Integer to string, or just copy an existing string */

	if (obj_desc->common.type == ACPI_TYPE_INTEGER) {
		acpi_ex_integer_to_string(uid->string, obj_desc->integer.value);
	} else {
		ACPI_STRCPY(uid->string, obj_desc->string.pointer);
	}

	uid->length = length;
	*return_id = uid;

cleanup:

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_execute_CID
 *
 * PARAMETERS:  device_node         - Node for the device
 *              return_cid_list     - Where the CID list is returned
 *
 * RETURN:      Status, list of CID strings
 *
 * DESCRIPTION: Executes the _CID control method that returns one or more
 *              compatible hardware IDs for the device.
 *
 *              NOTE: Internal function, no parameter validation
 *
 * A _CID method can return either a single compatible ID or a package of
 * compatible IDs. Each compatible ID can be one of the following:
 * 1) Integer (32 bit compressed EISA ID) or
 * 2) String (PCI ID format, e.g. "PCI\VEN_vvvv&DEV_dddd&SUBSYS_ssssssss")
 *
 * The Integer CIDs are converted to string format by this function.
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_CID(struct acpi_namespace_node *device_node,
		    struct acpi_pnp_device_id_list **return_cid_list)
{
	union acpi_operand_object **cid_objects;
	union acpi_operand_object *obj_desc;
	struct acpi_pnp_device_id_list *cid_list;
	char *next_id_string;
	u32 string_area_size;
	u32 length;
	u32 cid_list_size;
	acpi_status status;
	u32 count;
	u32 i;

	ACPI_FUNCTION_TRACE(ut_execute_CID);

	/* Evaluate the _CID method for this device */

	status = acpi_ut_evaluate_object(device_node, METHOD_NAME__CID,
					 ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING
					 | ACPI_BTYPE_PACKAGE, &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Get the count and size of the returned _CIDs. _CID can return either
	 * a Package of Integers/Strings or a single Integer or String.
	 * Note: This section also validates that all CID elements are of the
	 * correct type (Integer or String).
	 */
	if (obj_desc->common.type == ACPI_TYPE_PACKAGE) {
		count = obj_desc->package.count;
		cid_objects = obj_desc->package.elements;
	} else {		/* Single Integer or String CID */

		count = 1;
		cid_objects = &obj_desc;
	}

	string_area_size = 0;
	for (i = 0; i < count; i++) {

		/* String lengths include null terminator */

		switch (cid_objects[i]->common.type) {
		case ACPI_TYPE_INTEGER:

			string_area_size += ACPI_EISAID_STRING_SIZE;
			break;

		case ACPI_TYPE_STRING:

			string_area_size += cid_objects[i]->string.length + 1;
			break;

		default:

			status = AE_TYPE;
			goto cleanup;
		}
	}

	/*
	 * Now that we know the length of the CIDs, allocate return buffer:
	 * 1) Size of the base structure +
	 * 2) Size of the CID PNP_DEVICE_ID array +
	 * 3) Size of the actual CID strings
	 */
	cid_list_size = sizeof(struct acpi_pnp_device_id_list) +
	    ((count - 1) * sizeof(struct acpi_pnp_device_id)) +
	    string_area_size;

	cid_list = ACPI_ALLOCATE_ZEROED(cid_list_size);
	if (!cid_list) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Area for CID strings starts after the CID PNP_DEVICE_ID array */

	next_id_string = ACPI_CAST_PTR(char, cid_list->ids) +
	    ((acpi_size) count * sizeof(struct acpi_pnp_device_id));

	/* Copy/convert the CIDs to the return buffer */

	for (i = 0; i < count; i++) {
		if (cid_objects[i]->common.type == ACPI_TYPE_INTEGER) {

			/* Convert the Integer (EISAID) CID to a string */

			acpi_ex_eisa_id_to_string(next_id_string,
						  cid_objects[i]->integer.
						  value);
			length = ACPI_EISAID_STRING_SIZE;
		} else {	/* ACPI_TYPE_STRING */

			/* Copy the String CID from the returned object */

			ACPI_STRCPY(next_id_string,
				    cid_objects[i]->string.pointer);
			length = cid_objects[i]->string.length + 1;
		}

		cid_list->ids[i].string = next_id_string;
		cid_list->ids[i].length = length;
		next_id_string += length;
	}

	/* Finish the CID list */

	cid_list->count = count;
	cid_list->list_size = cid_list_size;
	*return_cid_list = cid_list;

cleanup:

	/* On exit, we must delete the _CID return object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}
