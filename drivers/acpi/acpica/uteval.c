/******************************************************************************
 *
 * Module Name: uteval - Object evaluation
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
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
ACPI_MODULE_NAME("uteval")

/*
 * Strings supported by the _OSI predefined (internal) method.
 *
 * March 2009: Removed "Linux" as this host no longer wants to respond true
 * for this string. Basically, the only safe OS strings are windows-related
 * and in many or most cases represent the only test path within the
 * BIOS-provided ASL code.
 *
 * The second element of each entry is used to track the newest version of
 * Windows that the BIOS has requested.
 */
static struct acpi_interface_info acpi_interfaces_supported[] = {
	/* Operating System Vendor Strings */

	{"Windows 2000", ACPI_OSI_WIN_2000},	/* Windows 2000 */
	{"Windows 2001", ACPI_OSI_WIN_XP},	/* Windows XP */
	{"Windows 2001 SP1", ACPI_OSI_WIN_XP_SP1},	/* Windows XP SP1 */
	{"Windows 2001.1", ACPI_OSI_WINSRV_2003},	/* Windows Server 2003 */
	{"Windows 2001 SP2", ACPI_OSI_WIN_XP_SP2},	/* Windows XP SP2 */
	{"Windows 2001.1 SP1", ACPI_OSI_WINSRV_2003_SP1},	/* Windows Server 2003 SP1 - Added 03/2006 */
	{"Windows 2006", ACPI_OSI_WIN_VISTA},	/* Windows Vista - Added 03/2006 */
	{"Windows 2006.1", ACPI_OSI_WINSRV_2008},	/* Windows Server 2008 - Added 09/2009 */
	{"Windows 2006 SP1", ACPI_OSI_WIN_VISTA_SP1},	/* Windows Vista SP1 - Added 09/2009 */
	{"Windows 2009", ACPI_OSI_WIN_7},	/* Windows 7 and Server 2008 R2 - Added 09/2009 */

	/* Feature Group Strings */

	{"Extended Address Space Descriptor", 0}

	/*
	 * All "optional" feature group strings (features that are implemented
	 * by the host) should be implemented in the host version of
	 * acpi_os_validate_interface and should not be added here.
	 */
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_osi_implementation
 *
 * PARAMETERS:  walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Implementation of the _OSI predefined control method
 *
 ******************************************************************************/

acpi_status acpi_ut_osi_implementation(struct acpi_walk_state *walk_state)
{
	acpi_status status;
	union acpi_operand_object *string_desc;
	union acpi_operand_object *return_desc;
	u32 return_value;
	u32 i;

	ACPI_FUNCTION_TRACE(ut_osi_implementation);

	/* Validate the string input argument */

	string_desc = walk_state->arguments[0].object;
	if (!string_desc || (string_desc->common.type != ACPI_TYPE_STRING)) {
		return_ACPI_STATUS(AE_TYPE);
	}

	/* Create a return object */

	return_desc = acpi_ut_create_internal_object(ACPI_TYPE_INTEGER);
	if (!return_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Default return value is 0, NOT SUPPORTED */

	return_value = 0;

	/* Compare input string to static table of supported interfaces */

	for (i = 0; i < ACPI_ARRAY_LENGTH(acpi_interfaces_supported); i++) {
		if (!ACPI_STRCMP(string_desc->string.pointer,
				 acpi_interfaces_supported[i].name)) {
			/*
			 * The interface is supported.
			 * Update the osi_data if necessary. We keep track of the latest
			 * version of Windows that has been requested by the BIOS.
			 */
			if (acpi_interfaces_supported[i].value >
			    acpi_gbl_osi_data) {
				acpi_gbl_osi_data =
				    acpi_interfaces_supported[i].value;
			}

			return_value = ACPI_UINT32_MAX;
			goto exit;
		}
	}

	/*
	 * Did not match the string in the static table, call the host OSL to
	 * check for a match with one of the optional strings (such as
	 * "Module Device", "3.0 Thermal Model", etc.)
	 */
	status = acpi_os_validate_interface(string_desc->string.pointer);
	if (ACPI_SUCCESS(status)) {

		/* The interface is supported */

		return_value = ACPI_UINT32_MAX;
	}

exit:
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO,
		"ACPI: BIOS _OSI(%s) is %ssupported\n",
		string_desc->string.pointer, return_value == 0 ? "not " : ""));

	/* Complete the return value */

	return_desc->integer.value = return_value;
	walk_state->return_desc = return_desc;
	return_ACPI_STATUS (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_osi_invalidate
 *
 * PARAMETERS:  interface_string
 *
 * RETURN:      Status
 *
 * DESCRIPTION: invalidate string in pre-defiend _OSI string list
 *
 ******************************************************************************/

acpi_status acpi_osi_invalidate(char *interface)
{
	int i;

	for (i = 0; i < ACPI_ARRAY_LENGTH(acpi_interfaces_supported); i++) {
		if (!ACPI_STRCMP(interface, acpi_interfaces_supported[i].name)) {
			*acpi_interfaces_supported[i].name = '\0';
			return AE_OK;
		}
	}
	return AE_NOT_FOUND;
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
 *              return object. Common code that simplifies accessing objects
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
	struct acpi_evaluate_info *info;
	acpi_status status;
	u32 return_btype;

	ACPI_FUNCTION_TRACE(ut_evaluate_object);

	/* Allocate the evaluation information block */

	info = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_evaluate_info));
	if (!info) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	info->prefix_node = prefix_node;
	info->pathname = path;

	/* Evaluate the object/method */

	status = acpi_ns_evaluate(info);
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

		goto cleanup;
	}

	/* Did we get a return object? */

	if (!info->return_object) {
		if (expected_return_btypes) {
			ACPI_ERROR_METHOD("No object was returned from",
					  prefix_node, path, AE_NOT_EXIST);

			status = AE_NOT_EXIST;
		}

		goto cleanup;
	}

	/* Map the return object type to the bitmapped type */

	switch ((info->return_object)->common.type) {
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
		 * We received a return object, but one was not expected. This can
		 * happen frequently if the "implicit return" feature is enabled.
		 * Just delete the return object and return AE_OK.
		 */
		acpi_ut_remove_reference(info->return_object);
		goto cleanup;
	}

	/* Is the return object one of the expected types? */

	if (!(expected_return_btypes & return_btype)) {
		ACPI_ERROR_METHOD("Return object type is incorrect",
				  prefix_node, path, AE_TYPE);

		ACPI_ERROR((AE_INFO,
			    "Type returned from %s was incorrect: %s, expected Btypes: %X",
			    path,
			    acpi_ut_get_object_type_name(info->return_object),
			    expected_return_btypes));

		/* On error exit, we must delete the return object */

		acpi_ut_remove_reference(info->return_object);
		status = AE_TYPE;
		goto cleanup;
	}

	/* Object type is OK, return it */

	*return_desc = info->return_object;

      cleanup:
	ACPI_FREE(info);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_evaluate_numeric_object
 *
 * PARAMETERS:  object_name         - Object name to be evaluated
 *              device_node         - Node for the device
 *              Value               - Where the value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluates a numeric namespace object for a selected device
 *              and stores result in *Value.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_evaluate_numeric_object(char *object_name,
				struct acpi_namespace_node *device_node,
				acpi_integer *value)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ut_evaluate_numeric_object);

	status = acpi_ut_evaluate_object(device_node, object_name,
					 ACPI_BTYPE_INTEGER, &obj_desc);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Get the returned Integer */

	*value = obj_desc->integer.value;

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

	ACPI_FUNCTION_TRACE(ut_execute_STA);

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
 * FUNCTION:    acpi_ut_execute_power_methods
 *
 * PARAMETERS:  device_node         - Node for the device
 *              method_names        - Array of power method names
 *              method_count        - Number of methods to execute
 *              out_values          - Where the power method values are returned
 *
 * RETURN:      Status, out_values
 *
 * DESCRIPTION: Executes the specified power methods for the device and returns
 *              the result(s).
 *
 *              NOTE: Internal function, no parameter validation
 *
******************************************************************************/

acpi_status
acpi_ut_execute_power_methods(struct acpi_namespace_node *device_node,
			      const char **method_names,
			      u8 method_count, u8 *out_values)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;
	acpi_status final_status = AE_NOT_FOUND;
	u32 i;

	ACPI_FUNCTION_TRACE(ut_execute_power_methods);

	for (i = 0; i < method_count; i++) {
		/*
		 * Execute the power method (_sx_d or _sx_w). The only allowable
		 * return type is an Integer.
		 */
		status = acpi_ut_evaluate_object(device_node,
						 ACPI_CAST_PTR(char,
							       method_names[i]),
						 ACPI_BTYPE_INTEGER, &obj_desc);
		if (ACPI_SUCCESS(status)) {
			out_values[i] = (u8)obj_desc->integer.value;

			/* Delete the return object */

			acpi_ut_remove_reference(obj_desc);
			final_status = AE_OK;	/* At least one value is valid */
			continue;
		}

		out_values[i] = ACPI_UINT8_MAX;
		if (status == AE_NOT_FOUND) {
			continue;	/* Ignore if not found */
		}

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Failed %s on Device %4.4s, %s\n",
				  ACPI_CAST_PTR(char, method_names[i]),
				  acpi_ut_get_node_name(device_node),
				  acpi_format_exception(status)));
	}

	return_ACPI_STATUS(final_status);
}
