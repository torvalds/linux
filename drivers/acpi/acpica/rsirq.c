// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: rsirq - IRQ resource descriptors
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acresrc.h"

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rsirq")

/*******************************************************************************
 *
 * acpi_rs_get_irq
 *
 ******************************************************************************/
struct acpi_rsconvert_info acpi_rs_get_irq[9] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_IRQ,
	 ACPI_RS_SIZE(struct acpi_resource_irq),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_get_irq)},

	/* Get the IRQ mask (bytes 1:2) */

	{ACPI_RSC_BITMASK16, ACPI_RS_OFFSET(data.irq.interrupts[0]),
	 AML_OFFSET(irq.irq_mask),
	 ACPI_RS_OFFSET(data.irq.interrupt_count)},

	/* Set default flags (others are zero) */

	{ACPI_RSC_SET8, ACPI_RS_OFFSET(data.irq.triggering),
	 ACPI_EDGE_SENSITIVE,
	 1},

	/* Get the descriptor length (2 or 3 for IRQ descriptor) */

	{ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET(data.irq.descriptor_length),
	 AML_OFFSET(irq.descriptor_type),
	 0},

	/* All done if no flag byte present in descriptor */

	{ACPI_RSC_EXIT_NE, ACPI_RSC_COMPARE_AML_LENGTH, 0, 3},

	/* Get flags: Triggering[0], Polarity[3], Sharing[4], Wake[5] */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.irq.triggering),
	 AML_OFFSET(irq.flags),
	 0},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.irq.polarity),
	 AML_OFFSET(irq.flags),
	 3},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.irq.sharable),
	 AML_OFFSET(irq.flags),
	 4},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.irq.wake_capable),
	 AML_OFFSET(irq.flags),
	 5}
};

/*******************************************************************************
 *
 * acpi_rs_set_irq
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_set_irq[14] = {
	/* Start with a default descriptor of length 3 */

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_IRQ,
	 sizeof(struct aml_resource_irq),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_set_irq)},

	/* Convert interrupt list to 16-bit IRQ bitmask */

	{ACPI_RSC_BITMASK16, ACPI_RS_OFFSET(data.irq.interrupts[0]),
	 AML_OFFSET(irq.irq_mask),
	 ACPI_RS_OFFSET(data.irq.interrupt_count)},

	/* Set flags: Triggering[0], Polarity[3], Sharing[4], Wake[5] */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.irq.triggering),
	 AML_OFFSET(irq.flags),
	 0},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.irq.polarity),
	 AML_OFFSET(irq.flags),
	 3},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.irq.sharable),
	 AML_OFFSET(irq.flags),
	 4},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.irq.wake_capable),
	 AML_OFFSET(irq.flags),
	 5},

	/*
	 * All done if the output descriptor length is required to be 3
	 * (i.e., optimization to 2 bytes cannot be attempted)
	 */
	{ACPI_RSC_EXIT_EQ, ACPI_RSC_COMPARE_VALUE,
	 ACPI_RS_OFFSET(data.irq.descriptor_length),
	 3},

	/* Set length to 2 bytes (no flags byte) */

	{ACPI_RSC_LENGTH, 0, 0, sizeof(struct aml_resource_irq_noflags)},

	/*
	 * All done if the output descriptor length is required to be 2.
	 *
	 * TBD: Perhaps we should check for error if input flags are not
	 * compatible with a 2-byte descriptor.
	 */
	{ACPI_RSC_EXIT_EQ, ACPI_RSC_COMPARE_VALUE,
	 ACPI_RS_OFFSET(data.irq.descriptor_length),
	 2},

	/* Reset length to 3 bytes (descriptor with flags byte) */

	{ACPI_RSC_LENGTH, 0, 0, sizeof(struct aml_resource_irq)},

	/*
	 * Check if the flags byte is necessary. Not needed if the flags are:
	 * ACPI_EDGE_SENSITIVE, ACPI_ACTIVE_HIGH, ACPI_EXCLUSIVE
	 */
	{ACPI_RSC_EXIT_NE, ACPI_RSC_COMPARE_VALUE,
	 ACPI_RS_OFFSET(data.irq.triggering),
	 ACPI_EDGE_SENSITIVE},

	{ACPI_RSC_EXIT_NE, ACPI_RSC_COMPARE_VALUE,
	 ACPI_RS_OFFSET(data.irq.polarity),
	 ACPI_ACTIVE_HIGH},

	{ACPI_RSC_EXIT_NE, ACPI_RSC_COMPARE_VALUE,
	 ACPI_RS_OFFSET(data.irq.sharable),
	 ACPI_EXCLUSIVE},

	/* We can optimize to a 2-byte irq_no_flags() descriptor */

	{ACPI_RSC_LENGTH, 0, 0, sizeof(struct aml_resource_irq_noflags)}
};

/*******************************************************************************
 *
 * acpi_rs_convert_ext_irq
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_ext_irq[10] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_EXTENDED_IRQ,
	 ACPI_RS_SIZE(struct acpi_resource_extended_irq),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_ext_irq)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_EXTENDED_IRQ,
	 sizeof(struct aml_resource_extended_irq),
	 0},

	/*
	 * Flags: Producer/Consumer[0], Triggering[1], Polarity[2],
	 *        Sharing[3], Wake[4]
	 */
	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.extended_irq.producer_consumer),
	 AML_OFFSET(extended_irq.flags),
	 0},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.extended_irq.triggering),
	 AML_OFFSET(extended_irq.flags),
	 1},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.extended_irq.polarity),
	 AML_OFFSET(extended_irq.flags),
	 2},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.extended_irq.sharable),
	 AML_OFFSET(extended_irq.flags),
	 3},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.extended_irq.wake_capable),
	 AML_OFFSET(extended_irq.flags),
	 4},

	/* IRQ Table length (Byte4) */

	{ACPI_RSC_COUNT, ACPI_RS_OFFSET(data.extended_irq.interrupt_count),
	 AML_OFFSET(extended_irq.interrupt_count),
	 sizeof(u32)},

	/* Copy every IRQ in the table, each is 32 bits */

	{ACPI_RSC_MOVE32, ACPI_RS_OFFSET(data.extended_irq.interrupts[0]),
	 AML_OFFSET(extended_irq.interrupts[0]),
	 0},

	/* Optional resource_source (Index and String) */

	{ACPI_RSC_SOURCEX, ACPI_RS_OFFSET(data.extended_irq.resource_source),
	 ACPI_RS_OFFSET(data.extended_irq.interrupts[0]),
	 sizeof(struct aml_resource_extended_irq)}
};

/*******************************************************************************
 *
 * acpi_rs_convert_dma
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_dma[6] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_DMA,
	 ACPI_RS_SIZE(struct acpi_resource_dma),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_dma)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_DMA,
	 sizeof(struct aml_resource_dma),
	 0},

	/* Flags: transfer preference, bus mastering, channel speed */

	{ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET(data.dma.transfer),
	 AML_OFFSET(dma.flags),
	 0},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.dma.bus_master),
	 AML_OFFSET(dma.flags),
	 2},

	{ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET(data.dma.type),
	 AML_OFFSET(dma.flags),
	 5},

	/* DMA channel mask bits */

	{ACPI_RSC_BITMASK, ACPI_RS_OFFSET(data.dma.channels[0]),
	 AML_OFFSET(dma.dma_channel_mask),
	 ACPI_RS_OFFSET(data.dma.channel_count)}
};

/*******************************************************************************
 *
 * acpi_rs_convert_fixed_dma
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_fixed_dma[4] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_FIXED_DMA,
	 ACPI_RS_SIZE(struct acpi_resource_fixed_dma),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_fixed_dma)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_FIXED_DMA,
	 sizeof(struct aml_resource_fixed_dma),
	 0},

	/*
	 * These fields are contiguous in both the source and destination:
	 * request_lines
	 * Channels
	 */
	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.fixed_dma.request_lines),
	 AML_OFFSET(fixed_dma.request_lines),
	 2},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.fixed_dma.width),
	 AML_OFFSET(fixed_dma.width),
	 1},
};
