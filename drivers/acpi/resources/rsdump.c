/*******************************************************************************
 *
 * Module Name: rsdump - Functions to display the resource structures.
 *
 ******************************************************************************/

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
#include <acpi/acresrc.h>

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

static void acpi_rs_dump_byte_list(u32 length, u8 * data);

static void acpi_rs_dump_dword_list(u32 length, u32 * data);

static void acpi_rs_dump_short_byte_list(u32 length, u32 * data);

static void
acpi_rs_dump_resource_source(struct acpi_resource_source *resource_source);

static void acpi_rs_dump_address_common(union acpi_resource_data *resource);

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
	acpi_os_printf("%27s : %s\n", title, value);
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

static void acpi_rs_dump_byte_list(u32 length, u8 * data)
{
	u32 i;

	for (i = 0; i < length; i++) {
		acpi_os_printf("%25s%2.2X : %2.2X\n", "Byte", i, data[i]);
	}
}

static void acpi_rs_dump_dword_list(u32 length, u32 * data)
{
	u32 i;

	for (i = 0; i < length; i++) {
		acpi_os_printf("%25s%2.2X : %8.8X\n", "Dword", i, data[i]);
	}
}

static void acpi_rs_dump_short_byte_list(u32 length, u32 * data)
{
	u32 i;

	for (i = 0; i < length; i++) {
		acpi_os_printf("%X ", data[i]);
	}
	acpi_os_printf("\n");
}

static void acpi_rs_dump_memory_attribute(u32 read_write_attribute)
{

	acpi_rs_out_string("Read/Write Attribute",
			   ACPI_READ_WRITE_MEMORY == read_write_attribute ?
			   "Read/Write" : "Read-Only");
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

	acpi_rs_out_integer8("Resource Source Index",
			     (u8) resource_source->index);

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

		acpi_rs_out_string("Resource Type", "Memory Range");

		acpi_rs_out_title("Type-Specific Flags");

		switch (resource->address.attribute.memory.cache_attribute) {
		case ACPI_NON_CACHEABLE_MEMORY:
			acpi_os_printf("Noncacheable memory\n");
			break;

		case ACPI_CACHABLE_MEMORY:
			acpi_os_printf("Cacheable memory\n");
			break;

		case ACPI_WRITE_COMBINING_MEMORY:
			acpi_os_printf("Write-combining memory\n");
			break;

		case ACPI_PREFETCHABLE_MEMORY:
			acpi_os_printf("Prefetchable memory\n");
			break;

		default:
			acpi_os_printf("Invalid cache attribute\n");
			break;
		}

		acpi_rs_dump_memory_attribute(resource->address.attribute.
					      memory.read_write_attribute);
		break;

	case ACPI_IO_RANGE:

		acpi_rs_out_string("Resource Type", "I/O Range");

		acpi_rs_out_title("Type-Specific Flags");

		switch (resource->address.attribute.io.range_attribute) {
		case ACPI_NON_ISA_ONLY_RANGES:
			acpi_os_printf("Non-ISA I/O Addresses\n");
			break;

		case ACPI_ISA_ONLY_RANGES:
			acpi_os_printf("ISA I/O Addresses\n");
			break;

		case ACPI_ENTIRE_RANGE:
			acpi_os_printf("ISA and non-ISA I/O Addresses\n");
			break;

		default:
			acpi_os_printf("Invalid range attribute\n");
			break;
		}

		acpi_rs_out_string("Translation Attribute",
				   ACPI_SPARSE_TRANSLATION ==
				   resource->address.attribute.io.
				   translation_attribute ? "Sparse Translation"
				   : "Dense Translation");
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

	acpi_rs_out_string("Resource",
			   ACPI_CONSUMER ==
			   resource->address.
			   producer_consumer ? "Consumer" : "Producer");

	acpi_rs_out_string("Decode",
			   ACPI_SUB_DECODE == resource->address.decode ?
			   "Subtractive" : "Positive");

	acpi_rs_out_string("Min Address",
			   ACPI_ADDRESS_FIXED ==
			   resource->address.
			   min_address_fixed ? "Fixed" : "Not Fixed");

	acpi_rs_out_string("Max Address",
			   ACPI_ADDRESS_FIXED ==
			   resource->address.
			   max_address_fixed ? "Fixed" : "Not Fixed");
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

	ACPI_FUNCTION_ENTRY();

	if (!(acpi_dbg_level & ACPI_LV_RESOURCES)
	    || !(_COMPONENT & acpi_dbg_layer)) {
		return;
	}

	/* Dump all resource descriptors in the list */

	while (resource_list) {
		acpi_os_printf("\n[%02X] ", count);

		/* Validate Type before dispatch */

		if (resource_list->type > ACPI_RESOURCE_TYPE_MAX) {
			acpi_os_printf
			    ("Invalid descriptor type (%X) in resource list\n",
			     resource_list->type);
			return;
		}

		/* Dump the resource descriptor */

		acpi_gbl_dump_resource_dispatch[resource_list->
						type] (&resource_list->data);

		/* Exit on end tag */

		if (resource_list->type == ACPI_RESOURCE_TYPE_END_TAG) {
			return;
		}

		/* Get the next resource structure */

		resource_list =
		    ACPI_PTR_ADD(struct acpi_resource, resource_list,
				 resource_list->length);
		count++;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_irq
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_irq(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("IRQ Resource\n");

	acpi_rs_out_string("Triggering",
			   ACPI_LEVEL_SENSITIVE ==
			   resource->irq.triggering ? "Level" : "Edge");

	acpi_rs_out_string("Active",
			   ACPI_ACTIVE_LOW ==
			   resource->irq.polarity ? "Low" : "High");

	acpi_rs_out_string("Sharing",
			   ACPI_SHARED ==
			   resource->irq.sharable ? "Shared" : "Exclusive");

	acpi_rs_out_integer8("Interrupt Count",
			     (u8) resource->irq.interrupt_count);

	acpi_rs_out_title("Interrupt List");
	acpi_rs_dump_short_byte_list(resource->irq.interrupt_count,
				     resource->irq.interrupts);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_dma
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_dma(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("DMA Resource\n");

	acpi_rs_out_title("DMA Type");
	switch (resource->dma.type) {
	case ACPI_COMPATIBILITY:
		acpi_os_printf("Compatibility mode\n");
		break;

	case ACPI_TYPE_A:
		acpi_os_printf("Type A\n");
		break;

	case ACPI_TYPE_B:
		acpi_os_printf("Type B\n");
		break;

	case ACPI_TYPE_F:
		acpi_os_printf("Type F\n");
		break;

	default:
		acpi_os_printf("**** Invalid DMA type\n");
		break;
	}

	acpi_rs_out_string("Bus Master",
			   ACPI_BUS_MASTER ==
			   resource->dma.bus_master ? "Yes" : "No");

	acpi_rs_out_title("Transfer Type");
	switch (resource->dma.transfer) {
	case ACPI_TRANSFER_8:
		acpi_os_printf("8-bit transfers only\n");
		break;

	case ACPI_TRANSFER_8_16:
		acpi_os_printf("8-bit and 16-bit transfers\n");
		break;

	case ACPI_TRANSFER_16:
		acpi_os_printf("16-bit transfers only\n");
		break;

	default:
		acpi_os_printf("**** Invalid transfer preference\n");
		break;
	}

	acpi_rs_out_integer8("DMA Channel Count",
			     (u8) resource->dma.channel_count);

	acpi_rs_out_title("Channel List");
	acpi_rs_dump_short_byte_list(resource->dma.channel_count,
				     resource->dma.channels);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_start_dpf
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_start_dpf(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("Start Dependent Functions Resource\n");

	acpi_rs_out_title("Compatibility Priority");
	switch (resource->start_dpf.compatibility_priority) {
	case ACPI_GOOD_CONFIGURATION:
		acpi_os_printf("Good configuration\n");
		break;

	case ACPI_ACCEPTABLE_CONFIGURATION:
		acpi_os_printf("Acceptable configuration\n");
		break;

	case ACPI_SUB_OPTIMAL_CONFIGURATION:
		acpi_os_printf("Sub-optimal configuration\n");
		break;

	default:
		acpi_os_printf("**** Invalid compatibility priority\n");
		break;
	}

	acpi_rs_out_title("Performance/Robustness");
	switch (resource->start_dpf.performance_robustness) {
	case ACPI_GOOD_CONFIGURATION:
		acpi_os_printf("Good configuration\n");
		break;

	case ACPI_ACCEPTABLE_CONFIGURATION:
		acpi_os_printf("Acceptable configuration\n");
		break;

	case ACPI_SUB_OPTIMAL_CONFIGURATION:
		acpi_os_printf("Sub-optimal configuration\n");
		break;

	default:
		acpi_os_printf
		    ("**** Invalid performance robustness preference\n");
		break;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_io
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_io(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("I/O Resource\n");

	acpi_rs_out_string("Decode",
			   ACPI_DECODE_16 ==
			   resource->io.io_decode ? "16-bit" : "10-bit");

	acpi_rs_out_integer32("Address Minimum", resource->io.minimum);

	acpi_rs_out_integer32("Address Maximum", resource->io.maximum);

	acpi_rs_out_integer32("Alignment", resource->io.alignment);

	acpi_rs_out_integer32("Address Length", resource->io.address_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_fixed_io
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_fixed_io(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("Fixed I/O Resource\n");

	acpi_rs_out_integer32("Address", resource->fixed_io.address);

	acpi_rs_out_integer32("Address Length",
			      resource->fixed_io.address_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_vendor
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_vendor(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("Vendor Specific Resource\n");

	acpi_rs_out_integer16("Length", (u16) resource->vendor.byte_length);

	acpi_rs_dump_byte_list(resource->vendor.byte_length,
			       resource->vendor.byte_data);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_memory24
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_memory24(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("24-Bit Memory Range Resource\n");

	acpi_rs_dump_memory_attribute(resource->memory24.read_write_attribute);

	acpi_rs_out_integer16("Address Minimum",
			      (u16) resource->memory24.minimum);

	acpi_rs_out_integer16("Address Maximum",
			      (u16) resource->memory24.maximum);

	acpi_rs_out_integer16("Alignment", (u16) resource->memory24.alignment);

	acpi_rs_out_integer16("Address Length",
			      (u16) resource->memory24.address_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_memory32
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_memory32(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("32-Bit Memory Range Resource\n");

	acpi_rs_dump_memory_attribute(resource->memory32.read_write_attribute);

	acpi_rs_out_integer32("Address Minimum", resource->memory32.minimum);

	acpi_rs_out_integer32("Address Maximum", resource->memory32.maximum);

	acpi_rs_out_integer32("Alignment", resource->memory32.alignment);

	acpi_rs_out_integer32("Address Length",
			      resource->memory32.address_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_fixed_memory32
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_fixed_memory32(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("32-Bit Fixed Location Memory Range Resource\n");

	acpi_rs_dump_memory_attribute(resource->fixed_memory32.
				      read_write_attribute);

	acpi_rs_out_integer32("Address", resource->fixed_memory32.address);

	acpi_rs_out_integer32("Address Length",
			      resource->fixed_memory32.address_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_address16
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_address16(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("16-Bit WORD Address Space Resource\n");

	acpi_rs_dump_address_common(resource);

	acpi_rs_out_integer16("Granularity",
			      (u16) resource->address16.granularity);

	acpi_rs_out_integer16("Address Minimum",
			      (u16) resource->address16.minimum);

	acpi_rs_out_integer16("Address Maximum",
			      (u16) resource->address16.maximum);

	acpi_rs_out_integer16("Translation Offset",
			      (u16) resource->address16.translation_offset);

	acpi_rs_out_integer16("Address Length",
			      (u16) resource->address16.address_length);

	acpi_rs_dump_resource_source(&resource->address16.resource_source);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_address32
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_address32(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("32-Bit DWORD Address Space Resource\n");

	acpi_rs_dump_address_common(resource);

	acpi_rs_out_integer32("Granularity", resource->address32.granularity);

	acpi_rs_out_integer32("Address Minimum", resource->address32.minimum);

	acpi_rs_out_integer32("Address Maximum", resource->address32.maximum);

	acpi_rs_out_integer32("Translation Offset",
			      resource->address32.translation_offset);

	acpi_rs_out_integer32("Address Length",
			      resource->address32.address_length);

	acpi_rs_dump_resource_source(&resource->address32.resource_source);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_address64
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_address64(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("64-Bit QWORD Address Space Resource\n");

	acpi_rs_dump_address_common(resource);

	acpi_rs_out_integer64("Granularity", resource->address64.granularity);

	acpi_rs_out_integer64("Address Minimum", resource->address64.minimum);

	acpi_rs_out_integer64("Address Maximum", resource->address64.maximum);

	acpi_rs_out_integer64("Translation Offset",
			      resource->address64.translation_offset);

	acpi_rs_out_integer64("Address Length",
			      resource->address64.address_length);

	acpi_rs_dump_resource_source(&resource->address64.resource_source);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_ext_address64
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_ext_address64(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("64-Bit Extended Address Space Resource\n");

	acpi_rs_dump_address_common(resource);

	acpi_rs_out_integer64("Granularity",
			      resource->ext_address64.granularity);

	acpi_rs_out_integer64("Address Minimum",
			      resource->ext_address64.minimum);

	acpi_rs_out_integer64("Address Maximum",
			      resource->ext_address64.maximum);

	acpi_rs_out_integer64("Translation Offset",
			      resource->ext_address64.translation_offset);

	acpi_rs_out_integer64("Address Length",
			      resource->ext_address64.address_length);

	acpi_rs_out_integer64("Type-Specific Attribute",
			      resource->ext_address64.type_specific_attributes);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_ext_irq
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_ext_irq(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("Extended IRQ Resource\n");

	acpi_rs_out_string("Resource",
			   ACPI_CONSUMER ==
			   resource->extended_irq.
			   producer_consumer ? "Consumer" : "Producer");

	acpi_rs_out_string("Triggering",
			   ACPI_LEVEL_SENSITIVE ==
			   resource->extended_irq.
			   triggering ? "Level" : "Edge");

	acpi_rs_out_string("Active",
			   ACPI_ACTIVE_LOW == resource->extended_irq.polarity ?
			   "Low" : "High");

	acpi_rs_out_string("Sharing",
			   ACPI_SHARED == resource->extended_irq.sharable ?
			   "Shared" : "Exclusive");

	acpi_rs_dump_resource_source(&resource->extended_irq.resource_source);

	acpi_rs_out_integer8("Interrupts",
			     (u8) resource->extended_irq.interrupt_count);

	acpi_rs_dump_dword_list(resource->extended_irq.interrupt_count,
				resource->extended_irq.interrupts);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_generic_reg
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the field names and values of the resource descriptor
 *
 ******************************************************************************/

void acpi_rs_dump_generic_reg(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("Generic Register Resource\n");

	acpi_rs_out_integer8("Space ID", (u8) resource->generic_reg.space_id);

	acpi_rs_out_integer8("Bit Width", (u8) resource->generic_reg.bit_width);

	acpi_rs_out_integer8("Bit Offset",
			     (u8) resource->generic_reg.bit_offset);

	acpi_rs_out_integer8("Access Size",
			     (u8) resource->generic_reg.access_size);

	acpi_rs_out_integer64("Address", resource->generic_reg.address);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_end_dpf
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print type, no data.
 *
 ******************************************************************************/

void acpi_rs_dump_end_dpf(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("end_dependent_functions Resource\n");
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_end_tag
 *
 * PARAMETERS:  Resource        - Pointer to an internal resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print type, no data.
 *
 ******************************************************************************/

void acpi_rs_dump_end_tag(union acpi_resource_data *resource)
{
	ACPI_FUNCTION_ENTRY();

	acpi_os_printf("end_tag Resource\n");
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
	u8 *buffer = route_table;
	u8 count = 0;
	struct acpi_pci_routing_table *prt_element;

	ACPI_FUNCTION_ENTRY();

	if (!(acpi_dbg_level & ACPI_LV_RESOURCES)
	    || !(_COMPONENT & acpi_dbg_layer)) {
		return;
	}

	prt_element = ACPI_CAST_PTR(struct acpi_pci_routing_table, buffer);

	/* Dump all table elements, Exit on null length element */

	while (prt_element->length) {
		acpi_os_printf("\n[%02X] PCI IRQ Routing Table Package\n",
			       count);

		acpi_rs_out_integer64("Address", prt_element->address);

		acpi_rs_out_integer32("Pin", prt_element->pin);
		acpi_rs_out_string("Source", prt_element->source);
		acpi_rs_out_integer32("Source Index",
				      prt_element->source_index);

		buffer += prt_element->length;
		prt_element =
		    ACPI_CAST_PTR(struct acpi_pci_routing_table, buffer);
		count++;
	}
}

#endif
