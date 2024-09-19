// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: rsdump - AML debugger support for resource structures.
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acresrc.h"

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rsdump")

/*
 * All functions in this module are used by the AML Debugger only
 */
/* Local prototypes */
static void acpi_rs_out_string(const char *title, const char *value);

static void acpi_rs_out_integer8(const char *title, u8 value);

static void acpi_rs_out_integer16(const char *title, u16 value);

static void acpi_rs_out_integer32(const char *title, u32 value);

static void acpi_rs_out_integer64(const char *title, u64 value);

static void acpi_rs_out_title(const char *title);

static void acpi_rs_dump_byte_list(u16 length, u8 *data);

static void acpi_rs_dump_word_list(u16 length, u16 *data);

static void acpi_rs_dump_dword_list(u8 length, u32 *data);

static void acpi_rs_dump_short_byte_list(u8 length, u8 *data);

static void
acpi_rs_dump_resource_source(struct acpi_resource_source *resource_source);

static void
acpi_rs_dump_resource_label(char *title,
			    struct acpi_resource_label *resource_label);

static void acpi_rs_dump_address_common(union acpi_resource_data *resource);

static void
acpi_rs_dump_descriptor(void *resource, struct acpi_rsdump_info *table);

#ifdef ACPI_DEBUGGER
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

	/* Check if debug output enabled */

	if (!ACPI_IS_DEBUG_ENABLED(ACPI_LV_RESOURCES, _COMPONENT)) {
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
		} else if (!resource_list->type) {
			ACPI_ERROR((AE_INFO, "Invalid Zero Resource Type"));
			return;
		}

		/* Sanity check the length. It must not be zero, or we loop forever */

		if (!resource_list->length) {
			acpi_os_printf
			    ("Invalid zero length descriptor in resource list\n");
			return;
		}

		/* Dump the resource descriptor */

		if (type == ACPI_RESOURCE_TYPE_SERIAL_BUS) {
			acpi_rs_dump_descriptor(&resource_list->data,
						acpi_gbl_dump_serial_bus_dispatch
						[resource_list->data.
						 common_serial_bus.type]);
		} else {
			acpi_rs_dump_descriptor(&resource_list->data,
						acpi_gbl_dump_resource_dispatch
						[type]);
		}

		/* Point to the next resource structure */

		resource_list = ACPI_NEXT_RESOURCE(resource_list);

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

void acpi_rs_dump_irq_list(u8 *route_table)
{
	struct acpi_pci_routing_table *prt_element;
	u8 count;

	ACPI_FUNCTION_ENTRY();

	/* Check if debug output enabled */

	if (!ACPI_IS_DEBUG_ENABLED(ACPI_LV_RESOURCES, _COMPONENT)) {
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
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_descriptor
 *
 * PARAMETERS:  resource            - Buffer containing the resource
 *              table               - Table entry to decode the resource
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump a resource descriptor based on a dump table entry.
 *
 ******************************************************************************/

static void
acpi_rs_dump_descriptor(void *resource, struct acpi_rsdump_info *table)
{
	u8 *target = NULL;
	u8 *previous_target;
	const char *name;
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

			if (table->pointer) {
				acpi_rs_out_string(name,
						   table->pointer[*target]);
			} else {
				acpi_rs_out_integer8(name, ACPI_GET8(target));
			}
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

			acpi_rs_out_string(name,
					   table->pointer[*target & 0x01]);
			break;

		case ACPI_RSD_2BITFLAG:

			acpi_rs_out_string(name,
					   table->pointer[*target & 0x03]);
			break;

		case ACPI_RSD_3BITFLAG:

			acpi_rs_out_string(name,
					   table->pointer[*target & 0x07]);
			break;

		case ACPI_RSD_6BITFLAG:

			acpi_rs_out_integer8(name, (ACPI_GET8(target) & 0x3F));
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

		case ACPI_RSD_SHORTLISTX:
			/*
			 * Short byte list (single line output) for GPIO vendor data
			 * Note: The list length is obtained from the previous table entry
			 */
			if (previous_target) {
				acpi_rs_out_title(name);
				acpi_rs_dump_short_byte_list(*previous_target,
							     *
							     (ACPI_CAST_INDIRECT_PTR
							      (u8, target)));
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

		case ACPI_RSD_WORDLIST:
			/*
			 * Word list for GPIO Pin Table
			 * Note: The list length is obtained from the previous table entry
			 */
			if (previous_target) {
				acpi_rs_dump_word_list(*previous_target,
						       *(ACPI_CAST_INDIRECT_PTR
							 (u16, target)));
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
			acpi_rs_dump_resource_source(ACPI_CAST_PTR
						     (struct
								   acpi_resource_source,
								   target));
			break;

		case ACPI_RSD_LABEL:
			/*
			 * resource_label
			 */
			acpi_rs_dump_resource_label("Resource Label",
						    ACPI_CAST_PTR(struct
								  acpi_resource_label,
								  target));
			break;

		case ACPI_RSD_SOURCE_LABEL:
			/*
			 * resource_source_label
			 */
			acpi_rs_dump_resource_label("Resource Source Label",
						    ACPI_CAST_PTR(struct
								  acpi_resource_label,
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
 * FUNCTION:    acpi_rs_dump_resource_label
 *
 * PARAMETERS:  title              - Title of the dumped resource field
 *              resource_label     - Pointer to a Resource Label struct
 *
 * RETURN:      None
 *
 * DESCRIPTION: Common routine for dumping the resource_label
 *
 ******************************************************************************/

static void
acpi_rs_dump_resource_label(char *title,
			    struct acpi_resource_label *resource_label)
{
	ACPI_FUNCTION_ENTRY();

	acpi_rs_out_string(title,
			   resource_label->string_ptr ?
			   resource_label->string_ptr : "[Not Specified]");
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump_address_common
 *
 * PARAMETERS:  resource        - Pointer to an internal resource descriptor
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
 * FUNCTION:    acpi_rs_out*
 *
 * PARAMETERS:  title       - Name of the resource field
 *              value       - Value of the resource field
 *
 * RETURN:      None
 *
 * DESCRIPTION: Miscellaneous helper functions to consistently format the
 *              output of the resource dump routines
 *
 ******************************************************************************/

static void acpi_rs_out_string(const char *title, const char *value)
{

	acpi_os_printf("%27s : %s", title, value);
	if (!*value) {
		acpi_os_printf("[NULL NAMESTRING]");
	}
	acpi_os_printf("\n");
}

static void acpi_rs_out_integer8(const char *title, u8 value)
{
	acpi_os_printf("%27s : %2.2X\n", title, value);
}

static void acpi_rs_out_integer16(const char *title, u16 value)
{

	acpi_os_printf("%27s : %4.4X\n", title, value);
}

static void acpi_rs_out_integer32(const char *title, u32 value)
{

	acpi_os_printf("%27s : %8.8X\n", title, value);
}

static void acpi_rs_out_integer64(const char *title, u64 value)
{

	acpi_os_printf("%27s : %8.8X%8.8X\n", title, ACPI_FORMAT_UINT64(value));
}

static void acpi_rs_out_title(const char *title)
{

	acpi_os_printf("%27s : ", title);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_dump*List
 *
 * PARAMETERS:  length      - Number of elements in the list
 *              data        - Start of the list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Miscellaneous functions to dump lists of raw data
 *
 ******************************************************************************/

static void acpi_rs_dump_byte_list(u16 length, u8 * data)
{
	u16 i;

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

static void acpi_rs_dump_word_list(u16 length, u16 *data)
{
	u16 i;

	for (i = 0; i < length; i++) {
		acpi_os_printf("%25s%2.2X : %4.4X\n", "Word", i, data[i]);
	}
}
