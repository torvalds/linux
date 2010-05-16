/*******************************************************************************
 *
 * Module Name: rsmem24 - Memory resource descriptors
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2010, Intel Corp.
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
ACPI_MODULE_NAME("rsmemory")

/*******************************************************************************
 *
 * acpi_rs_convert_memory24
 *
 ******************************************************************************/
struct acpi_rsconvert_info acpi_rs_convert_memory24[4] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_MEMORY24,
	 ACPI_RS_SIZE(struct acpi_resource_memory24),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_memory24)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_MEMORY24,
	 sizeof(struct aml_resource_memory24),
	 0},

	/* Read/Write bit */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.memory24.write_protect),
	 AML_OFFSET(memory24.flags),
	 0},
	/*
	 * These fields are contiguous in both the source and destination:
	 * Minimum Base Address
	 * Maximum Base Address
	 * Address Base Alignment
	 * Range Length
	 */
	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.memory24.minimum),
	 AML_OFFSET(memory24.minimum),
	 4}
};

/*******************************************************************************
 *
 * acpi_rs_convert_memory32
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_memory32[4] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_MEMORY32,
	 ACPI_RS_SIZE(struct acpi_resource_memory32),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_memory32)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_MEMORY32,
	 sizeof(struct aml_resource_memory32),
	 0},

	/* Read/Write bit */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.memory32.write_protect),
	 AML_OFFSET(memory32.flags),
	 0},
	/*
	 * These fields are contiguous in both the source and destination:
	 * Minimum Base Address
	 * Maximum Base Address
	 * Address Base Alignment
	 * Range Length
	 */
	{ACPI_RSC_MOVE32, ACPI_RS_OFFSET(data.memory32.minimum),
	 AML_OFFSET(memory32.minimum),
	 4}
};

/*******************************************************************************
 *
 * acpi_rs_convert_fixed_memory32
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_fixed_memory32[4] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_FIXED_MEMORY32,
	 ACPI_RS_SIZE(struct acpi_resource_fixed_memory32),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_fixed_memory32)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_FIXED_MEMORY32,
	 sizeof(struct aml_resource_fixed_memory32),
	 0},

	/* Read/Write bit */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.fixed_memory32.write_protect),
	 AML_OFFSET(fixed_memory32.flags),
	 0},
	/*
	 * These fields are contiguous in both the source and destination:
	 * Base Address
	 * Range Length
	 */
	{ACPI_RSC_MOVE32, ACPI_RS_OFFSET(data.fixed_memory32.address),
	 AML_OFFSET(fixed_memory32.address),
	 2}
};

/*******************************************************************************
 *
 * acpi_rs_get_vendor_small
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_get_vendor_small[3] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_VENDOR,
	 ACPI_RS_SIZE(struct acpi_resource_vendor),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_get_vendor_small)},

	/* Length of the vendor data (byte count) */

	{ACPI_RSC_COUNT16, ACPI_RS_OFFSET(data.vendor.byte_length),
	 0,
	 sizeof(u8)}
	,

	/* Vendor data */

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.vendor.byte_data[0]),
	 sizeof(struct aml_resource_small_header),
	 0}
};

/*******************************************************************************
 *
 * acpi_rs_get_vendor_large
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_get_vendor_large[3] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_VENDOR,
	 ACPI_RS_SIZE(struct acpi_resource_vendor),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_get_vendor_large)},

	/* Length of the vendor data (byte count) */

	{ACPI_RSC_COUNT16, ACPI_RS_OFFSET(data.vendor.byte_length),
	 0,
	 sizeof(u8)}
	,

	/* Vendor data */

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.vendor.byte_data[0]),
	 sizeof(struct aml_resource_large_header),
	 0}
};

/*******************************************************************************
 *
 * acpi_rs_set_vendor
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_set_vendor[7] = {
	/* Default is a small vendor descriptor */

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_VENDOR_SMALL,
	 sizeof(struct aml_resource_small_header),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_set_vendor)},

	/* Get the length and copy the data */

	{ACPI_RSC_COUNT16, ACPI_RS_OFFSET(data.vendor.byte_length),
	 0,
	 0},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.vendor.byte_data[0]),
	 sizeof(struct aml_resource_small_header),
	 0},

	/*
	 * All done if the Vendor byte length is 7 or less, meaning that it will
	 * fit within a small descriptor
	 */
	{ACPI_RSC_EXIT_LE, 0, 0, 7},

	/* Must create a large vendor descriptor */

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_VENDOR_LARGE,
	 sizeof(struct aml_resource_large_header),
	 0},

	{ACPI_RSC_COUNT16, ACPI_RS_OFFSET(data.vendor.byte_length),
	 0,
	 0},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.vendor.byte_data[0]),
	 sizeof(struct aml_resource_large_header),
	 0}
};
