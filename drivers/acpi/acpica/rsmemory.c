// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: rsmem24 - Memory resource descriptors
 *
 ******************************************************************************/

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
	 sizeof(u8)},

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
	 sizeof(u8)},

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
