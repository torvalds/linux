/******************************************************************************
 *
 * Module Name: uteval - Object evaluation
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
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
#include <acpi/acnamesp.h>
#include <acpi/acinterp.h>

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("uteval")

/* Local prototypes */
static void
acpi_ut_copy_id_string(char *destination, char *source, acpi_size max_length);

static acpi_status
acpi_ut_translate_one_cid(union acpi_operand_object *obj_desc,
			  struct acpi_compatible_id *one_cid);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_osi_implementation
 *
 * PARAMETERS:  walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Implementation of _OSI predefined control method
 *              Supported = _OSI (String)
 *
 ******************************************************************************/

acpi_status acpi_ut_osi_implementation(struct acpi_walk_state *walk_state)
{
	union acpi_operand_object *string_desc;
	union acpi_operand_object *return_desc;
	acpi_native_uint i;

	ACPI_FUNCTION_TRACE("ut_osi_implementation");

	/* Validate the string input argument */

	string_desc = walk_state->arguments[0].object;
	if (!string_desc || (string_desc->common.type != ACPI_TYPE_STRING)) {
		return_ACPI_STATUS(AE_TYPE);
	}

	/* Create a return object (Default value = 0) */

	return_desc = acpi_ut_create_internal_object(ACPI_TYPE_INTEGER);
	if (!return_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Compare input string to table of supported strings */

	for (i = 0; i < ACPI_NUM_OSI_STRINGS; i++) {
		if (!ACPI_STRCMP(string_desc->string.pointer,
				 ACPI_CAST_PTR(char,
					       acpi_gbl_valid_osi_strings[i])))
		{

			/* This string is supported */

			return_desc->integer.value = 0xFFFFFFFF;
			break;
		}
	}

	walk_state->return_desc = return_desc;
	return_ACPI_STATUS(AE_CTRL_TERMINATE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_evaluate_object
 *
 * PARAMETERS:  prefix_node         - Starting node
 *              Path                - Path to object from starting node
 *              expected_return_types - Bitmap of allowed return types
 *              return_desc         - Where a return value is stored
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluates a namespace object and verifies the type of the
 *              return object.  Common code that simplifies accessing objects
 *              that have required return objects of fixed types.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_evaluate_object(struct acpi_namespace_node *prefix_node,
			char *path,
			u32 expected_return_btypes,
			union acpi_operand_object **return_desc)
{
	struct acpi_parameter_info info;
	acpi_status status;
	u32 return_btype;

	ACPI_FUNCTION_TRACE("ut_evaluate_object");

	info.node = prefix_node;
	info.parameters = NULL;
	info.parameter_type = ACPI_PARAM_ARGS;

	/* Evaluate the object/method */

	status = acpi_ns_evaluate_relative(path, &info);
	if (ACPI_FAILURE(status)) {
		if (status == AE_NOT_FOUND) {
			ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
					  "[%4.4s.%s] was not found\n",
					  acpi_ut_get_node_name(prefix_node),
					  path));
		} else {
			ACPI_ERROR_METHOD("Method execution failed",
					  prefix_node, path, status);
		}

		return_ACPI_STATUS(status);
	}

	/* Did we get a return object? */

	if (!info.return_object) {
		if (expected_return_btypes) {
			ACPI_ERROR_METHOD("No object was returned from",
					  prefix_node, path, AE_NOT_EXIST);

			return_ACPI_STATUS(AE_NOT_EXIST);
		}

		return_ACPI_STATUS(AE_OK);
	}

	/* Map the return object type to the bitmapped type */

	switch (ACPI_GET_OBJECT_TYPE(info.return_object)) {
	case ACPI_TYPE_INTEGER:
		return_btype = ACPI_BTYPE_INTEGER;
		break;

	case ACPI_TYPE_BUFFER:
		return_btype = ACPI_BTYPE_BUFFER;
		break;

	case ACPI_TYPE_STRING:
		return_btype = ACPI_BTYPE_STRING;
		break;

	case ACPI_TYPE_PACKAGE:
		return_btype = ACPI_BTYPE_PACKAGE;
		break;

	default:
		return_btype = 0;
		break;
	}

	if ((acpi_gbl_enable_interpreter_slack) && (!expected_return_btypes)) {
		/*
		 * We received a return object, but one was not expected.  This can
		 * happen frequently if the "implicit return" feature is enabled.
		 * Just delete the return object and return AE_OK.
		 */
		acpi_ut_remove_reference(info.return_object);
		return_ACPI_STATUS(AE_OK);
	}

	/* Is the return object one of the expected types? */

	if (!(expected_return_btypes & return_btype)) {
		ACPI_ERROR_METHOD("Return object type is incorrect",
				  prefix_node, path, AE_TYPE);

		ACPI_ERROR((AE_INFO,
			    "Type returned from %s was incorrect: %s, expected Btypes: %X",
			    path,
			    acpi_ut_get_object_type_name(info.return_object),
			    expected_return_btypes));

		/* On error exit, we must delete the return object */

		acpi_ut_remove_reference(info.return_object);
		return_ACPI_STATUS(AE_TYPE);
	}

	/* Object type is OK, return it */

	*return_desc = info.return_object;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_evaluate_numeric_object
 *
 * PARAMETERS:  object_name         - Object name to be evaluated
 *              device_node         - Node for the device
 *              Address             - Where the value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluates a numeric namespace object for a selected device
 *              and stores result in *Address.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_evaluate_numeric_object(char *object_name,
				struct acpi_namespace_node *device_node,
				acpi_integer * address)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE("ut_evaluate_numeric_object");

	status = acpi_ut_evaluate_object(device_node, object_name,
					 ACPI_BTYPE_INTEGER, &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Get the returned Integer */

	*address = obj_desc->integer.value;

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_copy_id_string
 *
 * PARAMETERS:  Destination         - Where to copy the string
 *              Source              - Source string
 *              max_length          - Length of the destination buffer
 *
 * RETURN:      None
 *
 * DESCRIPTION: Copies an ID string for the _HID, _CID, and _UID methods.
 *              Performs removal of a leading asterisk if present -- workaround
 *              for a known issue on a bunch of machines.
 *
 ******************************************************************************/

static void
acpi_ut_copy_id_string(char *destination, char *source, acpi_size max_length)
{

	/*
	 * Workaround for ID strings that have a leading asterisk. This construct
	 * is not allowed by the ACPI specification  (ID strings must be
	 * alphanumeric), but enough existing machines have this embedded in their
	 * ID strings that the following code is useful.
	 */
	if (*source == '*') {
		source++;
	}

	/* Do the actual copy */

	ACPI_STRNCPY(destination, source, max_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_execute_HID
 *
 * PARAMETERS:  device_node         - Node for the device
 *              Hid                 - Where the HID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _HID control method that returns the hardware
 *              ID of the device.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_HID(struct acpi_namespace_node *device_node,
		    struct acpi_device_id *hid)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE("ut_execute_HID");

	status = acpi_ut_evaluate_object(device_node, METHOD_NAME__HID,
					 ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING,
					 &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (ACPI_GET_OBJECT_TYPE(obj_desc) == ACPI_TYPE_INTEGER) {

		/* Convert the Numeric HID to string */

		acpi_ex_eisa_id_to_string((u32) obj_desc->integer.value,
					  hid->value);
	} else {
		/* Copy the String HID from the returned object */

		acpi_ut_copy_id_string(hid->value, obj_desc->string.pointer,
				       sizeof(hid->value));
	}

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_translate_one_cid
 *
 * PARAMETERS:  obj_desc            - _CID object, must be integer or string
 *              one_cid             - Where the CID string is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return a numeric or string _CID value as a string.
 *              (Compatible ID)
 *
 *              NOTE:  Assumes a maximum _CID string length of
 *                     ACPI_MAX_CID_LENGTH.
 *
 ******************************************************************************/

static acpi_status
acpi_ut_translate_one_cid(union acpi_operand_object *obj_desc,
			  struct acpi_compatible_id *one_cid)
{

	switch (ACPI_GET_OBJECT_TYPE(obj_desc)) {
	case ACPI_TYPE_INTEGER:

		/* Convert the Numeric CID to string */

		acpi_ex_eisa_id_to_string((u32) obj_desc->integer.value,
					  one_cid->value);
		return (AE_OK);

	case ACPI_TYPE_STRING:

		if (obj_desc->string.length > ACPI_MAX_CID_LENGTH) {
			return (AE_AML_STRING_LIMIT);
		}

		/* Copy the String CID from the returned object */

		acpi_ut_copy_id_string(one_cid->value, obj_desc->string.pointer,
				       ACPI_MAX_CID_LENGTH);
		return (AE_OK);

	default:

		return (AE_TYPE);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_execute_CID
 *
 * PARAMETERS:  device_node         - Node for the device
 *              return_cid_list     - Where the CID list is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _CID control method that returns one or more
 *              compatible hardware IDs for the device.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_CID(struct acpi_namespace_node * device_node,
		    struct acpi_compatible_id_list ** return_cid_list)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;
	u32 count;
	u32 size;
	struct acpi_compatible_id_list *cid_list;
	acpi_native_uint i;

	ACPI_FUNCTION_TRACE("ut_execute_CID");

	/* Evaluate the _CID method for this device */

	status = acpi_ut_evaluate_object(device_node, METHOD_NAME__CID,
					 ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING
					 | ACPI_BTYPE_PACKAGE, &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Get the number of _CIDs returned */

	count = 1;
	if (ACPI_GET_OBJECT_TYPE(obj_desc) == ACPI_TYPE_PACKAGE) {
		count = obj_desc->package.count;
	}

	/* Allocate a worst-case buffer for the _CIDs */

	size = (((count - 1) * sizeof(struct acpi_compatible_id)) +
		sizeof(struct acpi_compatible_id_list));

	cid_list = ACPI_MEM_CALLOCATE((acpi_size) size);
	if (!cid_list) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Init CID list */

	cid_list->count = count;
	cid_list->size = size;

	/*
	 *  A _CID can return either a single compatible ID or a package of
	 *  compatible IDs.  Each compatible ID can be one of the following:
	 *  1) Integer (32 bit compressed EISA ID) or
	 *  2) String (PCI ID format, e.g. "PCI\VEN_vvvv&DEV_dddd&SUBSYS_ssssssss")
	 */

	/* The _CID object can be either a single CID or a package (list) of CIDs */

	if (ACPI_GET_OBJECT_TYPE(obj_desc) == ACPI_TYPE_PACKAGE) {

		/* Translate each package element */

		for (i = 0; i < count; i++) {
			status =
			    acpi_ut_translate_one_cid(obj_desc->package.
						      elements[i],
						      &cid_list->id[i]);
			if (ACPI_FAILURE(status)) {
				break;
			}
		}
	} else {
		/* Only one CID, translate to a string */

		status = acpi_ut_translate_one_cid(obj_desc, cid_list->id);
	}

	/* Cleanup on error */

	if (ACPI_FAILURE(status)) {
		ACPI_MEM_FREE(cid_list);
	} else {
		*return_cid_list = cid_list;
	}

	/* On exit, we must delete the _CID return object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_execute_UID
 *
 * PARAMETERS:  device_node         - Node for the device
 *              Uid                 - Where the UID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _UID control method that returns the hardware
 *              ID of the device.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_UID(struct acpi_namespace_node *device_node,
		    struct acpi_device_id *uid)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE("ut_execute_UID");

	status = acpi_ut_evaluate_object(device_node, METHOD_NAME__UID,
					 ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING,
					 &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (ACPI_GET_OBJECT_TYPE(obj_desc) == ACPI_TYPE_INTEGER) {

		/* Convert the Numeric UID to string */

		acpi_ex_unsigned_integer_to_string(obj_desc->integer.value,
						   uid->value);
	} else {
		/* Copy the String UID from the returned object */

		acpi_ut_copy_id_string(uid->value, obj_desc->string.pointer,
				       sizeof(uid->value));
	}

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_execute_STA
 *
 * PARAMETERS:  device_node         - Node for the device
 *              Flags               - Where the status flags are returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes _STA for selected device and stores results in
 *              *Flags.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_STA(struct acpi_namespace_node *device_node, u32 * flags)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE("ut_execute_STA");

	status = acpi_ut_evaluate_object(device_node, METHOD_NAME__STA,
					 ACPI_BTYPE_INTEGER, &obj_desc);
	if (ACPI_FAILURE(status)) {
		if (AE_NOT_FOUND == status) {
			ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
					  "_STA on %4.4s was not found, assuming device is present\n",
					  acpi_ut_get_node_name(device_node)));

			*flags = ACPI_UINT32_MAX;
			status = AE_OK;
		}

		return_ACPI_STATUS(status);
	}

	/* Extract the status flags */

	*flags = (u32) obj_desc->integer.value;

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_execute_Sxds
 *
 * PARAMETERS:  device_node         - Node for the device
 *              Flags               - Where the status flags are returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes _STA for selected device and stores results in
 *              *Flags.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_sxds(struct acpi_namespace_node *device_node, u8 * highest)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;
	u32 i;

	ACPI_FUNCTION_TRACE("ut_execute_Sxds");

	for (i = 0; i < 4; i++) {
		highest[i] = 0xFF;
		status = acpi_ut_evaluate_object(device_node,
						 ACPI_CAST_PTR(char,
							       acpi_gbl_highest_dstate_names
							       [i]),
						 ACPI_BTYPE_INTEGER, &obj_desc);
		if (ACPI_FAILURE(status)) {
			if (status != AE_NOT_FOUND) {
				ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
						  "%s on Device %4.4s, %s\n",
						  ACPI_CAST_PTR(char,
								acpi_gbl_highest_dstate_names
								[i]),
						  acpi_ut_get_node_name
						  (device_node),
						  acpi_format_exception
						  (status)));

				return_ACPI_STATUS(status);
			}
		} else {
			/* Extract the Dstate value */

			highest[i] = (u8) obj_desc->integer.value;

			/* Delete the return object */

			acpi_ut_remove_reference(obj_desc);
		}
	}

	return_ACPI_STATUS(AE_OK);
}
