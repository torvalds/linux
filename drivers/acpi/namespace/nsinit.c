/******************************************************************************
 *
 * Module Name: nsinit - namespace initialization
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2005, R. Byron Moore
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
#include <acpi/acdispat.h>
#include <acpi/acinterp.h>

#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsinit")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_initialize_objects
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the entire namespace and perform any necessary
 *              initialization on the objects found therein
 *
 ******************************************************************************/

acpi_status
acpi_ns_initialize_objects (
	void)
{
	acpi_status                     status;
	struct acpi_init_walk_info      info;


	ACPI_FUNCTION_TRACE ("ns_initialize_objects");


	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"**** Starting initialization of namespace objects ****\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
		"Completing Region/Field/Buffer/Package initialization:"));

	/* Set all init info to zero */

	ACPI_MEMSET (&info, 0, sizeof (struct acpi_init_walk_info));

	/* Walk entire namespace from the supplied root */

	status = acpi_walk_namespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
			  ACPI_UINT32_MAX, acpi_ns_init_one_object,
			  &info, NULL);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "walk_namespace failed! %s\n",
			acpi_format_exception (status)));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
		"\nInitialized %hd/%hd Regions %hd/%hd Fields %hd/%hd Buffers %hd/%hd Packages (%hd nodes)\n",
		info.op_region_init, info.op_region_count,
		info.field_init,    info.field_count,
		info.buffer_init,   info.buffer_count,
		info.package_init,  info.package_count, info.object_count));

	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"%hd Control Methods found\n", info.method_count));
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"%hd Op Regions found\n", info.op_region_count));

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_initialize_devices
 *
 * PARAMETERS:  None
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: Walk the entire namespace and initialize all ACPI devices.
 *              This means running _INI on all present devices.
 *
 *              Note: We install PCI config space handler on region access,
 *              not here.
 *
 ******************************************************************************/

acpi_status
acpi_ns_initialize_devices (
	void)
{
	acpi_status                     status;
	struct acpi_device_walk_info    info;


	ACPI_FUNCTION_TRACE ("ns_initialize_devices");


	/* Init counters */

	info.device_count = 0;
	info.num_STA = 0;
	info.num_INI = 0;

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
		"Executing all Device _STA and_INI methods:"));

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Walk namespace for all objects */

	status = acpi_ns_walk_namespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
			  ACPI_UINT32_MAX, TRUE, acpi_ns_init_one_device, &info, NULL);

	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "walk_namespace failed! %s\n",
			acpi_format_exception (status)));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
		"\n%hd Devices found containing: %hd _STA, %hd _INI methods\n",
		info.device_count, info.num_STA, info.num_INI));

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_init_one_object
 *
 * PARAMETERS:  obj_handle      - Node
 *              Level           - Current nesting level
 *              Context         - Points to a init info struct
 *              return_value    - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Callback from acpi_walk_namespace. Invoked for every object
 *              within the  namespace.
 *
 *              Currently, the only objects that require initialization are:
 *              1) Methods
 *              2) Op Regions
 *
 ******************************************************************************/

acpi_status
acpi_ns_init_one_object (
	acpi_handle                     obj_handle,
	u32                             level,
	void                            *context,
	void                            **return_value)
{
	acpi_object_type                type;
	acpi_status                     status;
	struct acpi_init_walk_info      *info = (struct acpi_init_walk_info *) context;
	struct acpi_namespace_node      *node = (struct acpi_namespace_node *) obj_handle;
	union acpi_operand_object       *obj_desc;


	ACPI_FUNCTION_NAME ("ns_init_one_object");


	info->object_count++;

	/* And even then, we are only interested in a few object types */

	type = acpi_ns_get_type (obj_handle);
	obj_desc = acpi_ns_get_attached_object (node);
	if (!obj_desc) {
		return (AE_OK);
	}

	/* Increment counters for object types we are looking for */

	switch (type) {
	case ACPI_TYPE_REGION:
		info->op_region_count++;
		break;

	case ACPI_TYPE_BUFFER_FIELD:
		info->field_count++;
		break;

	case ACPI_TYPE_BUFFER:
		info->buffer_count++;
		break;

	case ACPI_TYPE_PACKAGE:
		info->package_count++;
		break;

	default:

		/* No init required, just exit now */
		return (AE_OK);
	}

	/*
	 * If the object is already initialized, nothing else to do
	 */
	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return (AE_OK);
	}

	/*
	 * Must lock the interpreter before executing AML code
	 */
	status = acpi_ex_enter_interpreter ();
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/*
	 * Each of these types can contain executable AML code within the
	 * declaration.
	 */
	switch (type) {
	case ACPI_TYPE_REGION:

		info->op_region_init++;
		status = acpi_ds_get_region_arguments (obj_desc);
		break;

	case ACPI_TYPE_BUFFER_FIELD:

		info->field_init++;
		status = acpi_ds_get_buffer_field_arguments (obj_desc);
		break;

	case ACPI_TYPE_BUFFER:

		info->buffer_init++;
		status = acpi_ds_get_buffer_arguments (obj_desc);
		break;

	case ACPI_TYPE_PACKAGE:

		info->package_init++;
		status = acpi_ds_get_package_arguments (obj_desc);
		break;

	default:
		/* No other types can get here */
		break;
	}

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ERROR, "\n"));
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not execute arguments for [%4.4s] (%s), %s\n",
				acpi_ut_get_node_name (node), acpi_ut_get_type_name (type),
				acpi_format_exception (status)));
	}

	/*
	 * Print a dot for each object unless we are going to print the entire
	 * pathname
	 */
	if (!(acpi_dbg_level & ACPI_LV_INIT_NAMES)) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT, "."));
	}

	/*
	 * We ignore errors from above, and always return OK, since we don't want
	 * to abort the walk on any single error.
	 */
	acpi_ex_exit_interpreter ();
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_init_one_device
 *
 * PARAMETERS:  acpi_walk_callback
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: This is called once per device soon after ACPI is enabled
 *              to initialize each device. It determines if the device is
 *              present, and if so, calls _INI.
 *
 ******************************************************************************/

acpi_status
acpi_ns_init_one_device (
	acpi_handle                     obj_handle,
	u32                             nesting_level,
	void                            *context,
	void                            **return_value)
{
	struct acpi_device_walk_info   *info = (struct acpi_device_walk_info *) context;
	struct acpi_parameter_info      pinfo;
	u32                             flags;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("ns_init_one_device");


	pinfo.parameters = NULL;
	pinfo.parameter_type = ACPI_PARAM_ARGS;

	pinfo.node = acpi_ns_map_handle_to_node (obj_handle);
	if (!pinfo.node) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * We will run _STA/_INI on Devices, Processors and thermal_zones only
	 */
	if ((pinfo.node->type != ACPI_TYPE_DEVICE)      &&
		(pinfo.node->type != ACPI_TYPE_PROCESSOR)   &&
		(pinfo.node->type != ACPI_TYPE_THERMAL)) {
		return_ACPI_STATUS (AE_OK);
	}

	if ((acpi_dbg_level <= ACPI_LV_ALL_EXCEPTIONS) &&
		(!(acpi_dbg_level & ACPI_LV_INFO))) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT, "."));
	}

	info->device_count++;

	/*
	 * Run _STA to determine if we can run _INI on the device.
	 */
	ACPI_DEBUG_EXEC (acpi_ut_display_init_pathname (ACPI_TYPE_METHOD, pinfo.node, "_STA"));
	status = acpi_ut_execute_STA (pinfo.node, &flags);

	if (ACPI_FAILURE (status)) {
		if (pinfo.node->type == ACPI_TYPE_DEVICE) {
			/* Ignore error and move on to next device */

			return_ACPI_STATUS (AE_OK);
		}

		/* _STA is not required for Processor or thermal_zone objects */
	}
	else {
		info->num_STA++;

		if (!(flags & 0x01)) {
			/* Don't look at children of a not present device */

			return_ACPI_STATUS(AE_CTRL_DEPTH);
		}
	}

	/*
	 * The device is present. Run _INI.
	 */
	ACPI_DEBUG_EXEC (acpi_ut_display_init_pathname (ACPI_TYPE_METHOD, pinfo.node, "_INI"));
	status = acpi_ns_evaluate_relative ("_INI", &pinfo);
	if (ACPI_FAILURE (status)) {
		/* No _INI (AE_NOT_FOUND) means device requires no initialization */

		if (status != AE_NOT_FOUND) {
			/* Ignore error and move on to next device */

#ifdef ACPI_DEBUG_OUTPUT
			char                *scope_name = acpi_ns_get_external_pathname (pinfo.node);

			ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "%s._INI failed: %s\n",
					scope_name, acpi_format_exception (status)));

			ACPI_MEM_FREE (scope_name);
#endif
		}

		status = AE_OK;
	}
	else {
		/* Delete any return object (especially if implicit_return is enabled) */

		if (pinfo.return_object) {
			acpi_ut_remove_reference (pinfo.return_object);
		}

		/* Count of successful INIs */

		info->num_INI++;
	}

	if (acpi_gbl_init_handler) {
		/* External initialization handler is present, call it */

		status = acpi_gbl_init_handler (pinfo.node, ACPI_INIT_DEVICE_INI);
	}

	return_ACPI_STATUS (status);
}
