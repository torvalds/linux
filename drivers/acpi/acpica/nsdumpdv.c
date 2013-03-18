/******************************************************************************
 *
 * Module Name: nsdump - table dumping routines for debug
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
	struct acpi_device_info *info;
	acpi_status status;
	u32 i;

	ACPI_FUNCTION_NAME(ns_dump_one_device);

	status =
	    acpi_ns_dump_one_object(obj_handle, level, context, return_value);

	status = acpi_get_object_info(obj_handle, &info);
	if (ACPI_SUCCESS(status)) {
		for (i = 0; i < level; i++) {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_TABLES, " "));
		}

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_TABLES,
				      "    HID: %s, ADR: %8.8X%8.8X, Status: %X\n",
				      info->hardware_id.string,
				      ACPI_FORMAT_UINT64(info->address),
				      info->current_status));
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
