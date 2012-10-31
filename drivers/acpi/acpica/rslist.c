/*******************************************************************************
 *
 * Module Name: rslist - Linked list utilities
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
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
#include "acresrc.h"

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rslist")

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_convert_aml_to_resources
 *
 * PARAMETERS:  acpi_walk_aml_callback
 *              resource_ptr            - Pointer to the buffer that will
 *                                        contain the output structures
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an AML resource to an internal representation of the
 *              resource that is aligned and easier to access.
 *
 ******************************************************************************/
acpi_status
acpi_rs_convert_aml_to_resources(u8 * aml,
				 u32 length,
				 u32 offset, u8 resource_index, void **context)
{
	struct acpi_resource **resource_ptr =
	    ACPI_CAST_INDIRECT_PTR(struct acpi_resource, context);
	struct acpi_resource *resource;
	union aml_resource *aml_resource;
	struct acpi_rsconvert_info *conversion_table;
	acpi_status status;

	ACPI_FUNCTION_TRACE(rs_convert_aml_to_resources);

	/*
	 * Check that the input buffer and all subsequent pointers into it
	 * are aligned on a native word boundary. Most important on IA64
	 */
	resource = *resource_ptr;
	if (ACPI_IS_MISALIGNED(resource)) {
		ACPI_WARNING((AE_INFO,
			      "Misaligned resource pointer %p", resource));
	}

	/* Get the appropriate conversion info table */

	aml_resource = ACPI_CAST_PTR(union aml_resource, aml);
	if (acpi_ut_get_resource_type(aml) == ACPI_RESOURCE_NAME_SERIAL_BUS) {
		if (aml_resource->common_serial_bus.type >
		    AML_RESOURCE_MAX_SERIALBUSTYPE) {
			conversion_table = NULL;
		} else {
			/* This is an I2C, SPI, or UART serial_bus descriptor */

			conversion_table =
			    acpi_gbl_convert_resource_serial_bus_dispatch
			    [aml_resource->common_serial_bus.type];
		}
	} else {
		conversion_table =
		    acpi_gbl_get_resource_dispatch[resource_index];
	}

	if (!conversion_table) {
		ACPI_ERROR((AE_INFO,
			    "Invalid/unsupported resource descriptor: Type 0x%2.2X",
			    resource_index));
		return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
	}

	/* Convert the AML byte stream resource to a local resource struct */

	status =
	    acpi_rs_convert_aml_to_resource(resource, aml_resource,
					    conversion_table);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Could not convert AML resource (Type 0x%X)",
				*aml));
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_RESOURCES,
			  "Type %.2X, AmlLength %.2X InternalLength %.2X\n",
			  acpi_ut_get_resource_type(aml), length,
			  resource->length));

	/* Point to the next structure in the output buffer */

	*resource_ptr = ACPI_NEXT_RESOURCE(resource);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_convert_resources_to_aml
 *
 * PARAMETERS:  resource            - Pointer to the resource linked list
 *              aml_size_needed     - Calculated size of the byte stream
 *                                    needed from calling acpi_rs_get_aml_length()
 *                                    The size of the output_buffer is
 *                                    guaranteed to be >= aml_size_needed
 *              output_buffer       - Pointer to the buffer that will
 *                                    contain the byte stream
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes the resource linked list and parses it, creating a
 *              byte stream of resources in the caller's output buffer
 *
 ******************************************************************************/

acpi_status
acpi_rs_convert_resources_to_aml(struct acpi_resource *resource,
				 acpi_size aml_size_needed, u8 * output_buffer)
{
	u8 *aml = output_buffer;
	u8 *end_aml = output_buffer + aml_size_needed;
	struct acpi_rsconvert_info *conversion_table;
	acpi_status status;

	ACPI_FUNCTION_TRACE(rs_convert_resources_to_aml);

	/* Walk the resource descriptor list, convert each descriptor */

	while (aml < end_aml) {

		/* Validate the (internal) Resource Type */

		if (resource->type > ACPI_RESOURCE_TYPE_MAX) {
			ACPI_ERROR((AE_INFO,
				    "Invalid descriptor type (0x%X) in resource list",
				    resource->type));
			return_ACPI_STATUS(AE_BAD_DATA);
		}

		/* Perform the conversion */

		if (resource->type == ACPI_RESOURCE_TYPE_SERIAL_BUS) {
			if (resource->data.common_serial_bus.type >
			    AML_RESOURCE_MAX_SERIALBUSTYPE) {
				conversion_table = NULL;
			} else {
				/* This is an I2C, SPI, or UART serial_bus descriptor */

				conversion_table =
				    acpi_gbl_convert_resource_serial_bus_dispatch
				    [resource->data.common_serial_bus.type];
			}
		} else {
			conversion_table =
			    acpi_gbl_set_resource_dispatch[resource->type];
		}

		if (!conversion_table) {
			ACPI_ERROR((AE_INFO,
				    "Invalid/unsupported resource descriptor: Type 0x%2.2X",
				    resource->type));
			return_ACPI_STATUS(AE_AML_INVALID_RESOURCE_TYPE);
		}

		status = acpi_rs_convert_resource_to_aml(resource,
						         ACPI_CAST_PTR(union
								       aml_resource,
								       aml),
							 conversion_table);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Could not convert resource (type 0x%X) to AML",
					resource->type));
			return_ACPI_STATUS(status);
		}

		/* Perform final sanity check on the new AML resource descriptor */

		status =
		    acpi_ut_validate_resource(ACPI_CAST_PTR
					      (union aml_resource, aml), NULL);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/* Check for end-of-list, normal exit */

		if (resource->type == ACPI_RESOURCE_TYPE_END_TAG) {

			/* An End Tag indicates the end of the input Resource Template */

			return_ACPI_STATUS(AE_OK);
		}

		/*
		 * Extract the total length of the new descriptor and set the
		 * Aml to point to the next (output) resource descriptor
		 */
		aml += acpi_ut_get_descriptor_length(aml);

		/* Point to the next input resource descriptor */

		resource = ACPI_NEXT_RESOURCE(resource);
	}

	/* Completed buffer, but did not find an end_tag resource descriptor */

	return_ACPI_STATUS(AE_AML_NO_RESOURCE_END_TAG);
}
