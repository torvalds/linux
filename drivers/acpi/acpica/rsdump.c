/*******************************************************************************
 *
 * Module Name: rsdump - Functions to display the resource structures.
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2011, Intel Corp.
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
ACPI_MODULE_NAME("rsdump")
#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)
/* Local prototypes */
static void acpi_rs_out_string(char *title, char *value);

static void acpi_rs_out_integer8(char *title, u8 value);

static void acpi_rs_out_integer16(char *title, u16 value);

static void acpi_rs_out_integer32(char *title, u32 value);

static void acpi_rs_out_integer64(char *title, u64 value);

static void acpi_rs_out_title(char *title);

static void acpi_rs_dump_byte_list(u16 length, u8 * data);

static void acpi_rs_dump_dword_list(u8 length, u32 * data);

static void acpi_rs_dump_short_byte_list(u8 length, u8 * data);

static void
acpi_rs_dump_resource_source(struct acpi_resource_source *resource_source);

static void acpi_rs_dump_address_common(union acpi_resource_data *resource);

static void
acpi_rs_dump_descriptor(void *resource, struct acpi_rsdump_info *table);

#define ACPI_RSD_OFFSET(f)          (u8) ACPI_OFFSET (union acpi_resource_data,f)
#define ACPI_PRT_OFFSET(f)          (u8) ACPI_OFFSET (struct acpi_pci_routing_table,f)
#define ACPI_RSD_TABLE_SIZE(name)   (sizeof(name) / sizeof (struct acpi_rsdump_info))

/*******************************************************************************
 *
 * Resource Descriptor info tables
 *
 * Note: The first table entry must be a Title or Literal and must contain
 * the table length (number of table entries)
 *
 ******************************************************************************/

struct acpi_rsdump_info acpi_rs_dump_irq[7] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_irq), "IRQ", NULL},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(irq.descriptor_length),
	 "Descriptor Length", NULL},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(irq.triggering), "Triggering",
	 acpi_gbl_he_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(irq.polarity), "Polarity",
	 acpi_gbl_ll_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(irq.sharable), "Sharing",
	 acpi_gbl_shr_decode},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(irq.interrupt_count),
	 "Interrupt Count", NULL},
	{ACPI_RSD_SHORTLIST, ACPI_RSD_OFFSET(irq.interrupts[0]),
	 "Interrupt List", NULL}
};

struct acpi_rsdump_info acpi_rs_dump_dma[6] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_dma), "DMA", NULL},
	{ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET(dma.type), "Speed",
	 acpi_gbl_typ_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(dma.bus_master), "Mastering",
	 acpi_gbl_bm_decode},
	{ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET(dma.transfer), "Transfer Type",
	 acpi_gbl_siz_decode},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(dma.channel_count), "Channel Count",
	 NULL},
	{ACPI_RSD_SHORTLIST, ACPI_RSD_OFFSET(dma.channels[0]), "Channel List",
	 NULL}
};

struct acpi_rsdump_info acpi_rs_dump_start_dpf[4] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_start_dpf),
	 "Start-Dependent-Functions", NULL},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(start_dpf.descriptor_length),
	 "Descriptor Length", NULL},
	{ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET(start_dpf.compatibility_priority),
	 "Compatibility Priority", acpi_gbl_config_decode},
	{ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET(start_dpf.performance_robustness),
	 "Performance/Robustness", acpi_gbl_config_decode}
};

struct acpi_rsdump_info acpi_rs_dump_end_dpf[1] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_end_dpf),
	 "End-Dependent-Functions", NULL}
};

struct acpi_rsdump_info acpi_rs_dump_io[6] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_io), "I/O", NULL},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(io.io_decode), "Address Decoding",
	 acpi_gbl_io_decode},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(io.minimum), "Address Minimum", NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(io.maximum), "Address Maximum", NULL},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(io.alignment), "Alignment", NULL},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(io.address_length), "Address Length",
	 NULL}
};

struct acpi_rsdump_info acpi_rs_dump_fixed_io[3] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_fixed_io),
	 "Fixed I/O", NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(fixed_io.address), "Address", NULL},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(fixed_io.address_length),
	 "Address Length", NULL}
};

struct acpi_rsdump_info acpi_rs_dump_vendor[3] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_vendor),
	 "Vendor Specific", NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(vendor.byte_length), "Length", NULL},
	{ACPI_RSD_LONGLIST, ACPI_RSD_OFFSET(vendor.byte_data[0]), "Vendor Data",
	 NULL}
};

struct acpi_rsdump_info acpi_rs_dump_end_tag[1] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_end_tag), "EndTag",
	 NULL}
};

struct acpi_rsdump_info acpi_rs_dump_memory24[6] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_memory24),
	 "24-Bit Memory Range", NULL},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(memory24.write_protect),
	 "Write Protect", acpi_gbl_rw_decode},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(memory24.minimum), "Address Minimum",
	 NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(memory24.maximum), "Address Maximum",
	 NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(memory24.alignment), "Alignment",
	 NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(memory24.address_length),
	 "Address Length", NULL}
};

struct acpi_rsdump_info acpi_rs_dump_memory32[6] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_memory32),
	 "32-Bit Memory Range", NULL},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(memory32.write_protect),
	 "Write Protect", acpi_gbl_rw_decode},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(memory32.minimum), "Address Minimum",
	 NULL},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(memory32.maximum), "Address Maximum",
	 NULL},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(memory32.alignment), "Alignment",
	 NULL},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(memory32.address_length),
	 "Address Length", NULL}
};

struct acpi_rsdump_info acpi_rs_dump_fixed_memory32[4] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_fixed_memory32),
	 "32-Bit Fixed Memory Range", NULL},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(fixed_memory32.write_protect),
	 "Write Protect", acpi_gbl_rw_decode},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(fixed_memory32.address), "Address",
	 NULL},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(fixed_memory32.address_length),
	 "Address Length", NULL}
};

struct acpi_rsdump_info acpi_rs_dump_address16[8] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_address16),
	 "16-Bit WORD Address Space", NULL},
	{ACPI_RSD_ADDRESS, 0, NULL, NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(address16.granularity), "Granularity",
	 NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(address16.minimum), "Address Minimum",
	 NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(address16.maximum), "Address Maximum",
	 NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(address16.translation_offset),
	 "Translation Offset", NULL},
	{ACPI_RSD_UINT16, ACPI_RSD_OFFSET(address16.address_length),
	 "Address Length", NULL},
	{ACPI_RSD_SOURCE, ACPI_RSD_OFFSET(address16.resource_source), NULL, NULL}
};

struct acpi_rsdump_info acpi_rs_dump_address32[8] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_address32),
	 "32-Bit DWORD Address Space", NULL},
	{ACPI_RSD_ADDRESS, 0, NULL, NULL},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(address32.granularity), "Granularity",
	 NULL},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(address32.minimum), "Address Minimum",
	 NULL},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(address32.maximum), "Address Maximum",
	 NULL},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(address32.translation_offset),
	 "Translation Offset", NULL},
	{ACPI_RSD_UINT32, ACPI_RSD_OFFSET(address32.address_length),
	 "Address Length", NULL},
	{ACPI_RSD_SOURCE, ACPI_RSD_OFFSET(address32.resource_source), NULL, NULL}
};

struct acpi_rsdump_info acpi_rs_dump_address64[8] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_address64),
	 "64-Bit QWORD Address Space", NULL},
	{ACPI_RSD_ADDRESS, 0, NULL, NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(address64.granularity), "Granularity",
	 NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(address64.minimum), "Address Minimum",
	 NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(address64.maximum), "Address Maximum",
	 NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(address64.translation_offset),
	 "Translation Offset", NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(address64.address_length),
	 "Address Length", NULL},
	{ACPI_RSD_SOURCE, ACPI_RSD_OFFSET(address64.resource_source), NULL, NULL}
};

struct acpi_rsdump_info acpi_rs_dump_ext_address64[8] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_ext_address64),
	 "64-Bit Extended Address Space", NULL},
	{ACPI_RSD_ADDRESS, 0, NULL, NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(ext_address64.granularity),
	 "Granularity", NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(ext_address64.minimum),
	 "Address Minimum", NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(ext_address64.maximum),
	 "Address Maximum", NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(ext_address64.translation_offset),
	 "Translation Offset", NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(ext_address64.address_length),
	 "Address Length", NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(ext_address64.type_specific),
	 "Type-Specific Attribute", NULL}
};

struct acpi_rsdump_info acpi_rs_dump_ext_irq[8] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_ext_irq),
	 "Extended IRQ", NULL},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(extended_irq.producer_consumer),
	 "Type", acpi_gbl_consume_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(extended_irq.triggering),
	 "Triggering", acpi_gbl_he_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(extended_irq.polarity), "Polarity",
	 acpi_gbl_ll_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(extended_irq.sharable), "Sharing",
	 acpi_gbl_shr_decode},
	{ACPI_RSD_SOURCE, ACPI_RSD_OFFSET(extended_irq.resource_source), NULL,
	 NULL},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(extended_irq.interrupt_count),
	 "Interrupt Count", NULL},
	{ACPI_RSD_DWORDLIST, ACPI_RSD_OFFSET(extended_irq.interrupts[0]),
	 "Interrupt List", NULL}
};

struct acpi_rsdump_info acpi_rs_dump_generic_reg[6] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_generic_reg),
	 "Generic Register", NULL},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(generic_reg.space_id), "Space ID",
	 NULL},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(generic_reg.bit_width), "Bit Width",
	 NULL},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(generic_reg.bit_offset), "Bit Offset",
	 NULL},
	{ACPI_RSD_UINT8, ACPI_RSD_OFFSET(generic_reg.access_size),
	 "Access Size", NULL},
	{ACPI_RSD_UINT64, ACPI_RSD_OFFSET(generic_reg.address), "Address", NULL}
};

/*
 * Tables used for common address descriptor flag fields
 */
static struct acpi_rsdump_info acpi_rs_dump_general_flags[5] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_general_flags), NULL,
	 NULL},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(address.producer_consumer),
	 "Consumer/Producer", acpi_gbl_consume_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(address.decode), "Address Decode",
	 acpi_gbl_dec_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(address.min_address_fixed),
	 "Min Relocatability", acpi_gbl_min_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(address.max_address_fixed),
	 "Max Relocatability", acpi_gbl_max_decode}
};

static struct acpi_rsdump_info acpi_rs_dump_memory_flags[5] = {
	{ACPI_RSD_LITERAL, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_memory_flags),
	 "Resource Type", (void *)"Memory Range"},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(address.info.mem.write_protect),
	 "Write Protect", acpi_gbl_rw_decode},
	{ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET(address.info.mem.caching),
	 "Caching", acpi_gbl_mem_decode},
	{ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET(address.info.mem.range_type),
	 "Range Type", acpi_gbl_mtp_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(address.info.mem.translation),
	 "Translation", acpi_gbl_ttp_decode}
};

static struct acpi_rsdump_info acpi_rs_dump_io_flags[4] = {
	{ACPI_RSD_LITERAL, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_io_flags),
	 "Resource Type", (void *)"I/O Range"},
	{ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET(address.info.io.range_type),
	 "Range Type", acpi_gbl_rng_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(address.info.io.translation),
	 "Translation", acpi_gbl_ttp_decode},
	{ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET(address.info.io.translation_type),
	 "Translation Type", acpi_gbl_trs_decode}
};

/*
 * Table used to dump _PRT contents
 */
static struct acpi_rsdump_info acpi_rs_dump_prt[5] = {
	{ACPI_RSD_TITLE, ACPI_RSD_TABLE_SIZE(acpi_rs_dump_prt), NULL, NULL},
	{ACPI_RSD_UINT64, ACPI_PRT_OFFSET(address), "Address", NULL},
	{ACPI_RSD_UINT32, ACPI_PRT_OFFSET(pin), "Pin", NULL},
	{ACPI_RSD_STRING, ACPI_PRT_OFFSET(source[0]), "Source", NULL},
	{ACPI_RSD_UINT32, ACPI_PRT_OFFSET(source_index), "Source Index", NULL}
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_descriptor
 *
 * PARAMETERS:  Resource
 *
 * RETURN:      None
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

static void
acpi_rs_dump_descriptor(void *resource, struct acpi_rsdump_info *table)
{
	u8 *target = NULL;
	u8 *previous_target;
	char *name;
	u8 count;

	/* First table entry must contain the table length (# of table entries) */

	count = table->offset;

	while (count) {
		previous_target = target;
		target = ACPI_ADD_PTR(u8, resource, table->offset);
		name = table->name;

		switch (table->opcode) {
		case ACPI_RSD_TITLE:
			/*
			 * Optional resource title
			 */
			if (table->name) {
				acpi_os_printf("%s Resource\n", name);
			}
			break;

			/* Strings */

		case ACPI_RSD_LITERAL:
			acpi_rs_out_string(name,
					   ACPI_CAST_PTR(char, table->pointer));
			break;

		case ACPI_RSD_STRING:
			acpi_rs_out_string(name, ACPI_CAST_PTR(char, target));
			break;

			/* Data items, 8/16/32/64 bit */

		case ACPI_RSD_UINT8:
			acpi_rs_out_integer8(name, ACPI_GET8(target));
			break;

		case ACPI_RSD_UINT16:
			acpi_rs_out_integer16(name, ACPI_GET16(target));
			break;

		case ACPI_RSD_UINT32:
			acpi_rs_out_integer32(name, ACPI_GET32(target));
			break;

		case ACPI_RSD_UINT64:
			acpi_rs_out_integer64(name, ACPI_GET64(target));
			break;

			/* Flags: 1-bit and 2-bit flags supported */

		case ACPI_RSD_1BITFLAG:
			acpi_rs_out_string(name, ACPI_CAST_PTR(char,
							       table->
							       pointer[*target &
								       0x01]));
			break;

		case ACPI_RSD_2BITFLAG:
			acpi_rs_out_string(name, ACPI_CAST_PTR(char,
							       table->
							       pointer[*target &
								       0x03]));
			break;

		case ACPI_RSD_SHORTLIST:
			/*
			 * Short byte list (single line output) for DMA and IRQ resources
			 * Note: The list length is obtained from the previous table entry
			 */
			if (previous_target) {
				acpi_rs_out_title(name);
				acpi_rs_dump_short_byte_list(*previous_target,
							     target);
			}
			break;

		case ACPI_RSD_LONGLIST:
			/*
			 * Long byte list for Vendor resource data
			 * Note: The list length is obtained from the previous table entry
			 */
			if (previous_target) {
				acpi_rs_dump_byte_list(ACPI_GET16
						       (previous_target),
						       target);
			}
			break;

		case ACPI_RSD_DWORDLIST:
			/*
			 * Dword list for Extended Interrupt resources
			 * Note: The list length is obtained from the previous table entry
			 */
			if (previous_target) {
				acpi_rs_dump_dword_list(*previous_target,
							ACPI_CAST_PTR(u32,
								      target));
			}
			break;

		case ACPI_RSD_ADDRESS:
			/*
			 * Common flags for all Address resources
			 */
			acpi_rs_dump_address_common(ACPI_CAST_PTR
						    (union acpi_resource_data,
						     target));
			break;

		case ACPI_RSD_SOURCE:
			/*
			 * Optional resource_source for Address resources
			 */
			acpi_rs_dump_resource_source(ACPI_CAST_PTR(struct
								   acpi_resource_source,
								   target));
			break;

		default:
			acpi_os_printf("**** Invalid table opcode [%X] ****\n",
				       table->opcode);
			return;
		}

		table++;
		count--;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_resource_source
 *
 * PARAMETERS:  resource_source     - Pointer to a Resource Source struct
 *
 * RETURN:      None
 *
 * DESCRIPTION: Common routine for dumping the optional resource_source and the
 *              corresponding resource_source_index.
 *
 ******************************************************************************/

static void
acpi_rs_dump_resource_source(struct acpi_resource_source *resource_source)
{
	ACPI_FUNCTION_ENTRY();

	if (resource_source->index == 0xFF) {
		return;
	}

	acpi_rs_out_integer8("Resource Source Index", resource_source->index);

	acpi_rs_out_string("Resource Source",
			   resource_source->string_ptr ?
			   resource_source->string_ptr : "[Not Specified]");
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_address_common
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the fields that are common to all Address resource
 *              descriptors
 *
 ******************************************************************************/

static void acpi_rs_dump_address_common(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	/* Decode the type-specific flags */

	switch (resource->address.resource_type) {
	case ACPI_MEMORY_RANGE:

		acpi_rs_dump_descriptor(resource, acpi_rs_dump_memory_flags);
		break;

	case ACPI_IO_RANGE:

		acpi_rs_dump_descriptor(resource, acpi_rs_dump_io_flags);
		break;

	case ACPI_BUS_NUMBER_RANGE:

		acpi_rs_out_string("Resource Type", "Bus Number Range");
		break;

	default:

		acpi_rs_out_integer8("Resource Type",
				     (u8) resource->address.resource_type);
		break;
	}

	/* Decode the general flags */

	acpi_rs_dump_descriptor(resource, acpi_rs_dump_general_flags);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_resource_list
 *
 * PARAMETERS:  resource_list       - Pointer to a resource descriptor list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dispatches the structure to the correct dump routine.
 *
 ******************************************************************************/

void acpi_rs_dump_resource_list(struct acpi_resource *resource_list)
{
	u32 count = 0;
	u32 type;

	ACPI_FUNCTION_ENTRY();

	if (!(acpi_dbg_level & ACPI_LV_RESOURCES)
	    || !(_COMPONENT & acpi_dbg_layer)) {
		return;
	}

	/* Walk list and dump all resource descriptors (END_TAG terminates) */

	do {
		acpi_os_printf("\n[%02X] ", count);
		count++;

		/* Validate Type before dispatch */

		type = resource_list->type;
		if (type > ACPI_RESOURCE_TYPE_MAX) {
			acpi_os_printf
			    ("Invalid descriptor type (%X) in resource list\n",
			     resource_list->type);
			return;
		}

		/* Dump the resource descriptor */

		acpi_rs_dump_descriptor(&resource_list->data,
					acpi_gbl_dump_resource_dispatch[type]);

		/* Point to the next resource structure */

		resource_list =
		    ACPI_ADD_PTR(struct acpi_resource, resource_list,
				 resource_list->length);

		/* Exit when END_TAG descriptor is reached */

	} while (type != ACPI_RESOURCE_TYPE_END_TAG);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_irq_list
 *
 * PARAMETERS:  route_table     - Pointer to the routing table to dump.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print IRQ routing table
 *
 ******************************************************************************/

void acpi_rs_dump_irq_list(u8 * route_table)
{
	struct acpi_pci_routing_table *prt_element;
	u8 count;

	ACPI_FUNCTION_ENTRY();

	if (!(acpi_dbg_level & ACPI_LV_RESOURCES)
	    || !(_COMPONENT & acpi_dbg_layer)) {
		return;
	}

	prt_element = ACPI_CAST_PTR(struct acpi_pci_routing_table, route_table);

	/* Dump all table elements, Exit on zero length element */

	for (count = 0; prt_element->length; count++) {
		acpi_os_printf("\n[%02X] PCI IRQ Routing Table Package\n",
			       count);
		acpi_rs_dump_descriptor(prt_element, acpi_rs_dump_prt);

		prt_element = ACPI_ADD_PTR(struct acpi_pci_routing_table,
					   prt_element, prt_element->length);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_out*
 *
 * PARAMETERS:  Title       - Name of the resource field
 *              Value       - Value of the resource field
 *
 * RETURN:      None
 *
 * DESCRIPTION: Miscellaneous helper functions to consistently format the
 *              output of the resource dump routines
 *
 ******************************************************************************/

static void acpi_rs_out_string(char *title, char *value)
{
	acpi_os_printf("%27s : %s", title, value);
	if (!*value) {
		acpi_os_printf("[NULL NAMESTRING]");
	}
	acpi_os_printf("\n");
}

static void acpi_rs_out_integer8(char *title, u8 value)
{
	acpi_os_printf("%27s : %2.2X\n", title, value);
}

static void acpi_rs_out_integer16(char *title, u16 value)
{
	acpi_os_printf("%27s : %4.4X\n", title, value);
}

static void acpi_rs_out_integer32(char *title, u32 value)
{
	acpi_os_printf("%27s : %8.8X\n", title, value);
}

static void acpi_rs_out_integer64(char *title, u64 value)
{
	acpi_os_printf("%27s : %8.8X%8.8X\n", title, ACPI_FORMAT_UINT64(value));
}

static void acpi_rs_out_title(char *title)
{
	acpi_os_printf("%27s : ", title);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump*List
 *
 * PARAMETERS:  Length      - Number of elements in the list
 *              Data        - Start of the list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Miscellaneous functions to dump lists of raw data
 *
 ******************************************************************************/

static void acpi_rs_dump_byte_list(u16 length, u8 * data)
{
	u8 i;

	for (i = 0; i < length; i++) {
		acpi_os_printf("%25s%2.2X : %2.2X\n", "Byte", i, data[i]);
	}
}

static void acpi_rs_dump_short_byte_list(u8 length, u8 * data)
{
	u8 i;

	for (i = 0; i < length; i++) {
		acpi_os_printf("%X ", data[i]);
	}
	acpi_os_printf("\n");
}

static void acpi_rs_dump_dword_list(u8 length, u32 * data)
{
	u8 i;

	for (i = 0; i < length; i++) {
		acpi_os_printf("%25s%2.2X : %8.8X\n", "Dword", i, data[i]);
	}
}

#endif
