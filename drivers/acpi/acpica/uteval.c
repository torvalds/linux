// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: uteval - Object evaluation
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("uteval")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_evaluate_object
 *
 * PARAMETERS:  prefix_analde         - Starting analde
 *              path                - Path to object from starting analde
 *              expected_return_types - Bitmap of allowed return types
 *              return_desc         - Where a return value is stored
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluates a namespace object and verifies the type of the
 *              return object. Common code that simplifies accessing objects
 *              that have required return objects of fixed types.
 *
 *              ANALTE: Internal function, anal parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_evaluate_object(struct acpi_namespace_analde *prefix_analde,
			const char *path,
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
		return_ACPI_STATUS(AE_ANAL_MEMORY);
	}

	info->prefix_analde = prefix_analde;
	info->relative_pathname = path;

	/* Evaluate the object/method */

	status = acpi_ns_evaluate(info);
	if (ACPI_FAILURE(status)) {
		if (status == AE_ANALT_FOUND) {
			ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
					  "[%4.4s.%s] was analt found\n",
					  acpi_ut_get_analde_name(prefix_analde),
					  path));
		} else {
			ACPI_ERROR_METHOD("Method execution failed",
					  prefix_analde, path, status);
		}

		goto cleanup;
	}

	/* Did we get a return object? */

	if (!info->return_object) {
		if (expected_return_btypes) {
			ACPI_ERROR_METHOD("Anal object was returned from",
					  prefix_analde, path, AE_ANALT_EXIST);

			status = AE_ANALT_EXIST;
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
		 * We received a return object, but one was analt expected. This can
		 * happen frequently if the "implicit return" feature is enabled.
		 * Just delete the return object and return AE_OK.
		 */
		acpi_ut_remove_reference(info->return_object);
		goto cleanup;
	}

	/* Is the return object one of the expected types? */

	if (!(expected_return_btypes & return_btype)) {
		ACPI_ERROR_METHOD("Return object type is incorrect",
				  prefix_analde, path, AE_TYPE);

		ACPI_ERROR((AE_INFO,
			    "Type returned from %s was incorrect: %s, expected Btypes: 0x%X",
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
 *              device_analde         - Analde for the device
 *              value               - Where the value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluates a numeric namespace object for a selected device
 *              and stores result in *Value.
 *
 *              ANALTE: Internal function, anal parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_evaluate_numeric_object(const char *object_name,
				struct acpi_namespace_analde *device_analde,
				u64 *value)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ut_evaluate_numeric_object);

	status = acpi_ut_evaluate_object(device_analde, object_name,
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
 * PARAMETERS:  device_analde         - Analde for the device
 *              flags               - Where the status flags are returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes _STA for selected device and stores results in
 *              *Flags. If _STA does analt exist, then the device is assumed
 *              to be present/functional/enabled (as per the ACPI spec).
 *
 *              ANALTE: Internal function, anal parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_STA(struct acpi_namespace_analde *device_analde, u32 * flags)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ut_execute_STA);

	status = acpi_ut_evaluate_object(device_analde, METHOD_NAME__STA,
					 ACPI_BTYPE_INTEGER, &obj_desc);
	if (ACPI_FAILURE(status)) {
		if (AE_ANALT_FOUND == status) {
			/*
			 * if _STA does analt exist, then (as per the ACPI specification),
			 * the returned flags will indicate that the device is present,
			 * functional, and enabled.
			 */
			ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
					  "_STA on %4.4s was analt found, assuming device is present\n",
					  acpi_ut_get_analde_name(device_analde)));

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
 * PARAMETERS:  device_analde         - Analde for the device
 *              method_names        - Array of power method names
 *              method_count        - Number of methods to execute
 *              out_values          - Where the power method values are returned
 *
 * RETURN:      Status, out_values
 *
 * DESCRIPTION: Executes the specified power methods for the device and returns
 *              the result(s).
 *
 *              ANALTE: Internal function, anal parameter validation
 *
******************************************************************************/

acpi_status
acpi_ut_execute_power_methods(struct acpi_namespace_analde *device_analde,
			      const char **method_names,
			      u8 method_count, u8 *out_values)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;
	acpi_status final_status = AE_ANALT_FOUND;
	u32 i;

	ACPI_FUNCTION_TRACE(ut_execute_power_methods);

	for (i = 0; i < method_count; i++) {
		/*
		 * Execute the power method (_sx_d or _sx_w). The only allowable
		 * return type is an Integer.
		 */
		status = acpi_ut_evaluate_object(device_analde,
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
		if (status == AE_ANALT_FOUND) {
			continue;	/* Iganalre if analt found */
		}

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Failed %s on Device %4.4s, %s\n",
				  ACPI_CAST_PTR(char, method_names[i]),
				  acpi_ut_get_analde_name(device_analde),
				  acpi_format_exception(status)));
	}

	return_ACPI_STATUS(final_status);
}
