// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: rsio - IO and DMA resource descriptors
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acresrc.h"

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rsio")

/*******************************************************************************
 *
 * acpi_rs_convert_io
 *
 ******************************************************************************/
struct acpi_rsconvert_info acpi_rs_convert_io[5] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_IO,
	 ACPI_RS_SIZE(struct acpi_resource_io),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_io)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_IO,
	 sizeof(struct aml_resource_io),
	 0},

	/* Decode flag */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.io.io_decode),
	 AML_OFFSET(io.flags),
	 0},
	/*
	 * These fields are contiguous in both the source and destination:
	 * Address Alignment
	 * Length
	 * Minimum Base Address
	 * Maximum Base Address
	 */
	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.io.alignment),
	 AML_OFFSET(io.alignment),
	 2},

	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.io.minimum),
	 AML_OFFSET(io.minimum),
	 2}
};

/*******************************************************************************
 *
 * acpi_rs_convert_fixed_io
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_fixed_io[4] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_FIXED_IO,
	 ACPI_RS_SIZE(struct acpi_resource_fixed_io),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_fixed_io)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_FIXED_IO,
	 sizeof(struct aml_resource_fixed_io),
	 0},
	/*
	 * These fields are contiguous in both the source and destination:
	 * Base Address
	 * Length
	 */
	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.fixed_io.address_length),
	 AML_OFFSET(fixed_io.address_length),
	 1},

	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.fixed_io.address),
	 AML_OFFSET(fixed_io.address),
	 1}
};

/*******************************************************************************
 *
 * acpi_rs_convert_generic_reg
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_generic_reg[4] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_GENERIC_REGISTER,
	 ACPI_RS_SIZE(struct acpi_resource_generic_register),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_generic_reg)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_GENERIC_REGISTER,
	 sizeof(struct aml_resource_generic_register),
	 0},
	/*
	 * These fields are contiguous in both the source and destination:
	 * Address Space ID
	 * Register Bit Width
	 * Register Bit Offset
	 * Access Size
	 */
	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.generic_reg.space_id),
	 AML_OFFSET(generic_reg.address_space_id),
	 4},

	/* Get the Register Address */

	{ACPI_RSC_MOVE64, ACPI_RS_OFFSET(data.generic_reg.address),
	 AML_OFFSET(generic_reg.address),
	 1}
};

/*******************************************************************************
 *
 * acpi_rs_convert_end_dpf
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_end_dpf[2] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_END_DEPENDENT,
	 ACPI_RS_SIZE_MIN,
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_end_dpf)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_END_DEPENDENT,
	 sizeof(struct aml_resource_end_dependent),
	 0}
};

/*******************************************************************************
 *
 * acpi_rs_convert_end_tag
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_end_tag[2] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_END_TAG,
	 ACPI_RS_SIZE_MIN,
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_end_tag)},

	/*
	 * Note: The checksum field is set to zero, meaning that the resource
	 * data is treated as if the checksum operation succeeded.
	 * (ACPI Spec 1.0b Section 6.4.2.8)
	 */
	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_END_TAG,
	 sizeof(struct aml_resource_end_tag),
	 0}
};

/*******************************************************************************
 *
 * acpi_rs_get_start_dpf
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_get_start_dpf[6] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_START_DEPENDENT,
	 ACPI_RS_SIZE(struct acpi_resource_start_dependent),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_get_start_dpf)},

	/* Defaults for Compatibility and Performance priorities */

	{ACPI_RSC_SET8, ACPI_RS_OFFSET(data.start_dpf.compatibility_priority),
	 ACPI_ACCEPTABLE_CONFIGURATION,
	 2},

	/* Get the descriptor length (0 or 1 for Start Dpf descriptor) */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.start_dpf.descriptor_length),
	 AML_OFFSET(start_dpf.descriptor_type),
	 0},

	/* All done if there is no flag byte present in the descriptor */

	{ACPI_RSC_EXIT_NE, ACPI_RSC_COMPARE_AML_LENGTH, 0, 1},

	/* Flag byte is present, get the flags */

	{ACPI_RSC_2BITFLAG,
	 ACPI_RS_OFFSET(data.start_dpf.compatibility_priority),
	 AML_OFFSET(start_dpf.flags),
	 0},

	{ACPI_RSC_2BITFLAG,
	 ACPI_RS_OFFSET(data.start_dpf.performance_robustness),
	 AML_OFFSET(start_dpf.flags),
	 2}
};

/*******************************************************************************
 *
 * acpi_rs_set_start_dpf
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_set_start_dpf[10] = {
	/* Start with a default descriptor of length 1 */

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_START_DEPENDENT,
	 sizeof(struct aml_resource_start_dependent),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_set_start_dpf)},

	/* Set the default flag values */

	{ACPI_RSC_2BITFLAG,
	 ACPI_RS_OFFSET(data.start_dpf.compatibility_priority),
	 AML_OFFSET(start_dpf.flags),
	 0},

	{ACPI_RSC_2BITFLAG,
	 ACPI_RS_OFFSET(data.start_dpf.performance_robustness),
	 AML_OFFSET(start_dpf.flags),
	 2},
	/*
	 * All done if the output descriptor length is required to be 1
	 * (i.e., optimization to 0 bytes cannot be attempted)
	 */
	{ACPI_RSC_EXIT_EQ, ACPI_RSC_COMPARE_VALUE,
	 ACPI_RS_OFFSET(data.start_dpf.descriptor_length),
	 1},

	/* Set length to 0 bytes (no flags byte) */

	{ACPI_RSC_LENGTH, 0, 0,
	 sizeof(struct aml_resource_start_dependent_noprio)},

	/*
	 * All done if the output descriptor length is required to be 0.
	 *
	 * TBD: Perhaps we should check for error if input flags are not
	 * compatible with a 0-byte descriptor.
	 */
	{ACPI_RSC_EXIT_EQ, ACPI_RSC_COMPARE_VALUE,
	 ACPI_RS_OFFSET(data.start_dpf.descriptor_length),
	 0},

	/* Reset length to 1 byte (descriptor with flags byte) */

	{ACPI_RSC_LENGTH, 0, 0, sizeof(struct aml_resource_start_dependent)},

	/*
	 * All done if flags byte is necessary -- if either priority value
	 * is not ACPI_ACCEPTABLE_CONFIGURATION
	 */
	{ACPI_RSC_EXIT_NE, ACPI_RSC_COMPARE_VALUE,
	 ACPI_RS_OFFSET(data.start_dpf.compatibility_priority),
	 ACPI_ACCEPTABLE_CONFIGURATION},

	{ACPI_RSC_EXIT_NE, ACPI_RSC_COMPARE_VALUE,
	 ACPI_RS_OFFSET(data.start_dpf.performance_robustness),
	 ACPI_ACCEPTABLE_CONFIGURATION},

	/* Flag byte is not necessary */

	{ACPI_RSC_LENGTH, 0, 0,
	 sizeof(struct aml_resource_start_dependent_noprio)}
};
