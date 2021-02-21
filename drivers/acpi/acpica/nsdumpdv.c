// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: nsdump - table dumping routines for debug
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>

/* TBD: This entire module is apparently obsolete and should be removed */

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsdumpdv")
#ifdef ACPI_OBSOLETE_FUNCTIONS
#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)
#include "acnamesp.h"
/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_dump_one_device
 *
 * PARAMETERS:  handle              - Node to be dumped
 *              level               - Nesting level of the handle
 *              context             - Passed into walk_namespace
 *              return_value        - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dump a single Node that represents a device
 *              This procedure is a user_function called by acpi_ns_walk_namespace.
 *
 ******************************************************************************/
static acpi_status
acpi_ns_dump_one_device(acpi_handle obj_handle,
			u32 level, void *context, void **return_value)
{
	struct acpi_buffer buffer;
	struct acpi_device_info *info;
	acpi_status status;
	u32 i;

	ACPI_FUNCTION_NAME(ns_dump_one_device);

	status =
	    acpi_ns_dump_one_object(obj_handle, level, context, return_value);

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
	status = acpi_get_object_info(obj_handle, &buffer);
	if (ACPI_SUCCESS(status)) {
		info = buffer.pointer;
		for (i = 0; i < level; i++) {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_TABLES, " "));
		}

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_TABLES,
				      "    HID: %s, ADR: %8.8X%8.8X\n",
				      info->hardware_id.value,
				      ACPI_FORMAT_UINT64(info->address)));
		ACPI_FREE(info);
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_dump_root_devices
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump all objects of type "device"
 *
 ******************************************************************************/

void acpi_ns_dump_root_devices(void)
{
	acpi_handle sys_bus_handle;
	acpi_status status;

	ACPI_FUNCTION_NAME(ns_dump_root_devices);

	/* Only dump the table if tracing is enabled */

	if (!(ACPI_LV_TABLES & acpi_dbg_level)) {
		return;
	}

	status = acpi_get_handle(NULL, METHOD_NAME__SB_, &sys_bus_handle);
	if (ACPI_FAILURE(status)) {
		return;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_TABLES,
			  "Display of all devices in the namespace:\n"));

	status = acpi_ns_walk_namespace(ACPI_TYPE_DEVICE, sys_bus_handle,
					ACPI_UINT32_MAX, ACPI_NS_WALK_NO_UNLOCK,
					acpi_ns_dump_one_device, NULL, NULL,
					NULL);
}

#endif
#endif
