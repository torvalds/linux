// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: exdump - Interpreter debug output routines
 *
 * Copyright (C) 2000 - 2018, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exdump")

/*
 * The following routines are used for debug output only
 */
#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)
/* Local prototypes */
static void acpi_ex_out_string(const char *title, const char *value);

static void acpi_ex_out_pointer(const char *title, const void *value);

static void
acpi_ex_dump_object(union acpi_operand_object *obj_desc,
		    struct acpi_exdump_info *info);

static void acpi_ex_dump_reference_obj(union acpi_operand_object *obj_desc);

static void
acpi_ex_dump_package_obj(union acpi_operand_object *obj_desc,
			 u32 level, u32 index);

/*******************************************************************************
 *
 * Object Descriptor info tables
 *
 * Note: The first table entry must be an INIT opcode and must contain
 * the table length (number of table entries)
 *
 ******************************************************************************/

static struct acpi_exdump_info acpi_ex_dump_integer[2] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_integer), NULL},
	{ACPI_EXD_UINT64, ACPI_EXD_OFFSET(integer.value), "Value"}
};

static struct acpi_exdump_info acpi_ex_dump_string[4] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_string), NULL},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(string.length), "Length"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(string.pointer), "Pointer"},
	{ACPI_EXD_STRING, 0, NULL}
};

static struct acpi_exdump_info acpi_ex_dump_buffer[5] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_buffer), NULL},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(buffer.length), "Length"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(buffer.pointer), "Pointer"},
	{ACPI_EXD_NODE, ACPI_EXD_OFFSET(buffer.node), "Parent Node"},
	{ACPI_EXD_BUFFER, 0, NULL}
};

static struct acpi_exdump_info acpi_ex_dump_package[6] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_package), NULL},
	{ACPI_EXD_NODE, ACPI_EXD_OFFSET(package.node), "Parent Node"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(package.flags), "Flags"},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(package.count), "Element Count"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(package.elements), "Element List"},
	{ACPI_EXD_PACKAGE, 0, NULL}
};

static struct acpi_exdump_info acpi_ex_dump_device[4] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_device), NULL},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(device.notify_list[0]),
	 "System Notify"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(device.notify_list[1]),
	 "Device Notify"},
	{ACPI_EXD_HDLR_LIST, ACPI_EXD_OFFSET(device.handler), "Handler"}
};

static struct acpi_exdump_info acpi_ex_dump_event[2] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_event), NULL},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(event.os_semaphore), "OsSemaphore"}
};

static struct acpi_exdump_info acpi_ex_dump_method[9] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_method), NULL},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(method.info_flags), "Info Flags"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(method.param_count),
	 "Parameter Count"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(method.sync_level), "Sync Level"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(method.mutex), "Mutex"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(method.owner_id), "Owner Id"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(method.thread_count), "Thread Count"},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(method.aml_length), "Aml Length"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(method.aml_start), "Aml Start"}
};

static struct acpi_exdump_info acpi_ex_dump_mutex[6] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_mutex), NULL},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(mutex.sync_level), "Sync Level"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(mutex.original_sync_level),
	 "Original Sync Level"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(mutex.owner_thread), "Owner Thread"},
	{ACPI_EXD_UINT16, ACPI_EXD_OFFSET(mutex.acquisition_depth),
	 "Acquire Depth"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(mutex.os_mutex), "OsMutex"}
};

static struct acpi_exdump_info acpi_ex_dump_region[8] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_region), NULL},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(region.space_id), "Space Id"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(region.flags), "Flags"},
	{ACPI_EXD_NODE, ACPI_EXD_OFFSET(region.node), "Parent Node"},
	{ACPI_EXD_ADDRESS, ACPI_EXD_OFFSET(region.address), "Address"},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(region.length), "Length"},
	{ACPI_EXD_HDLR_LIST, ACPI_EXD_OFFSET(region.handler), "Handler"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(region.next), "Next"}
};

static struct acpi_exdump_info acpi_ex_dump_power[6] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_power), NULL},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(power_resource.system_level),
	 "System Level"},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(power_resource.resource_order),
	 "Resource Order"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(power_resource.notify_list[0]),
	 "System Notify"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(power_resource.notify_list[1]),
	 "Device Notify"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(power_resource.handler), "Handler"}
};

static struct acpi_exdump_info acpi_ex_dump_processor[7] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_processor), NULL},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(processor.proc_id), "Processor ID"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(processor.length), "Length"},
	{ACPI_EXD_ADDRESS, ACPI_EXD_OFFSET(processor.address), "Address"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(processor.notify_list[0]),
	 "System Notify"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(processor.notify_list[1]),
	 "Device Notify"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(processor.handler), "Handler"}
};

static struct acpi_exdump_info acpi_ex_dump_thermal[4] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_thermal), NULL},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(thermal_zone.notify_list[0]),
	 "System Notify"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(thermal_zone.notify_list[1]),
	 "Device Notify"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(thermal_zone.handler), "Handler"}
};

static struct acpi_exdump_info acpi_ex_dump_buffer_field[3] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_buffer_field), NULL},
	{ACPI_EXD_FIELD, 0, NULL},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(buffer_field.buffer_obj),
	 "Buffer Object"}
};

static struct acpi_exdump_info acpi_ex_dump_region_field[5] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_region_field), NULL},
	{ACPI_EXD_FIELD, 0, NULL},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(field.access_length), "AccessLength"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(field.region_obj), "Region Object"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(field.resource_buffer),
	 "ResourceBuffer"}
};

static struct acpi_exdump_info acpi_ex_dump_bank_field[5] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_bank_field), NULL},
	{ACPI_EXD_FIELD, 0, NULL},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(bank_field.value), "Value"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(bank_field.region_obj),
	 "Region Object"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(bank_field.bank_obj), "Bank Object"}
};

static struct acpi_exdump_info acpi_ex_dump_index_field[5] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_bank_field), NULL},
	{ACPI_EXD_FIELD, 0, NULL},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(index_field.value), "Value"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(index_field.index_obj),
	 "Index Object"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(index_field.data_obj), "Data Object"}
};

static struct acpi_exdump_info acpi_ex_dump_reference[9] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_reference), NULL},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(reference.class), "Class"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(reference.target_type), "Target Type"},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(reference.value), "Value"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(reference.object), "Object Desc"},
	{ACPI_EXD_NODE, ACPI_EXD_OFFSET(reference.node), "Node"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(reference.where), "Where"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(reference.index_pointer),
	 "Index Pointer"},
	{ACPI_EXD_REFERENCE, 0, NULL}
};

static struct acpi_exdump_info acpi_ex_dump_address_handler[6] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_address_handler),
	 NULL},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(address_space.space_id), "Space Id"},
	{ACPI_EXD_HDLR_LIST, ACPI_EXD_OFFSET(address_space.next), "Next"},
	{ACPI_EXD_RGN_LIST, ACPI_EXD_OFFSET(address_space.region_list),
	 "Region List"},
	{ACPI_EXD_NODE, ACPI_EXD_OFFSET(address_space.node), "Node"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(address_space.context), "Context"}
};

static struct acpi_exdump_info acpi_ex_dump_notify[7] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_notify), NULL},
	{ACPI_EXD_NODE, ACPI_EXD_OFFSET(notify.node), "Node"},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(notify.handler_type), "Handler Type"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(notify.handler), "Handler"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(notify.context), "Context"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(notify.next[0]),
	 "Next System Notify"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(notify.next[1]), "Next Device Notify"}
};

static struct acpi_exdump_info acpi_ex_dump_extra[6] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_extra), NULL},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(extra.method_REG), "_REG Method"},
	{ACPI_EXD_NODE, ACPI_EXD_OFFSET(extra.scope_node), "Scope Node"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(extra.region_context),
	 "Region Context"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(extra.aml_start), "Aml Start"},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(extra.aml_length), "Aml Length"}
};

static struct acpi_exdump_info acpi_ex_dump_data[3] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_data), NULL},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(data.handler), "Handler"},
	{ACPI_EXD_POINTER, ACPI_EXD_OFFSET(data.pointer), "Raw Data"}
};

/* Miscellaneous tables */

static struct acpi_exdump_info acpi_ex_dump_common[5] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_common), NULL},
	{ACPI_EXD_TYPE, 0, NULL},
	{ACPI_EXD_UINT16, ACPI_EXD_OFFSET(common.reference_count),
	 "Reference Count"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(common.flags), "Flags"},
	{ACPI_EXD_LIST, ACPI_EXD_OFFSET(common.next_object), "Object List"}
};

static struct acpi_exdump_info acpi_ex_dump_field_common[7] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_field_common), NULL},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(common_field.field_flags),
	 "Field Flags"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(common_field.access_byte_width),
	 "Access Byte Width"},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(common_field.bit_length),
	 "Bit Length"},
	{ACPI_EXD_UINT8, ACPI_EXD_OFFSET(common_field.start_field_bit_offset),
	 "Field Bit Offset"},
	{ACPI_EXD_UINT32, ACPI_EXD_OFFSET(common_field.base_byte_offset),
	 "Base Byte Offset"},
	{ACPI_EXD_NODE, ACPI_EXD_OFFSET(common_field.node), "Parent Node"}
};

static struct acpi_exdump_info acpi_ex_dump_node[7] = {
	{ACPI_EXD_INIT, ACPI_EXD_TABLE_SIZE(acpi_ex_dump_node), NULL},
	{ACPI_EXD_UINT8, ACPI_EXD_NSOFFSET(flags), "Flags"},
	{ACPI_EXD_UINT8, ACPI_EXD_NSOFFSET(owner_id), "Owner Id"},
	{ACPI_EXD_LIST, ACPI_EXD_NSOFFSET(object), "Object List"},
	{ACPI_EXD_NODE, ACPI_EXD_NSOFFSET(parent), "Parent"},
	{ACPI_EXD_NODE, ACPI_EXD_NSOFFSET(child), "Child"},
	{ACPI_EXD_NODE, ACPI_EXD_NSOFFSET(peer), "Peer"}
};

/* Dispatch table, indexed by object type */

static struct acpi_exdump_info *acpi_ex_dump_info[] = {
	NULL,
	acpi_ex_dump_integer,
	acpi_ex_dump_string,
	acpi_ex_dump_buffer,
	acpi_ex_dump_package,
	NULL,
	acpi_ex_dump_device,
	acpi_ex_dump_event,
	acpi_ex_dump_method,
	acpi_ex_dump_mutex,
	acpi_ex_dump_region,
	acpi_ex_dump_power,
	acpi_ex_dump_processor,
	acpi_ex_dump_thermal,
	acpi_ex_dump_buffer_field,
	NULL,
	NULL,
	acpi_ex_dump_region_field,
	acpi_ex_dump_bank_field,
	acpi_ex_dump_index_field,
	acpi_ex_dump_reference,
	NULL,
	NULL,
	acpi_ex_dump_notify,
	acpi_ex_dump_address_handler,
	NULL,
	NULL,
	NULL,
	acpi_ex_dump_extra,
	acpi_ex_dump_data
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_object
 *
 * PARAMETERS:  obj_desc            - Descriptor to dump
 *              info                - Info table corresponding to this object
 *                                    type
 *
 * RETURN:      None
 *
 * DESCRIPTION: Walk the info table for this object
 *
 ******************************************************************************/

static void
acpi_ex_dump_object(union acpi_operand_object *obj_desc,
		    struct acpi_exdump_info *info)
{
	u8 *target;
	const char *name;
	u8 count;
	union acpi_operand_object *start;
	union acpi_operand_object *data = NULL;
	union acpi_operand_object *next;
	struct acpi_namespace_node *node;

	if (!info) {
		acpi_os_printf
		    ("ExDumpObject: Display not implemented for object type %s\n",
		     acpi_ut_get_object_type_name(obj_desc));
		return;
	}

	/* First table entry must contain the table length (# of table entries) */

	count = info->offset;

	while (count) {
		if (!obj_desc) {
			return;
		}

		target = ACPI_ADD_PTR(u8, obj_desc, info->offset);
		name = info->name;

		switch (info->opcode) {
		case ACPI_EXD_INIT:

			break;

		case ACPI_EXD_TYPE:

			acpi_os_printf("%20s : %2.2X [%s]\n", "Type",
				       obj_desc->common.type,
				       acpi_ut_get_object_type_name(obj_desc));
			break;

		case ACPI_EXD_UINT8:

			acpi_os_printf("%20s : %2.2X\n", name, *target);
			break;

		case ACPI_EXD_UINT16:

			acpi_os_printf("%20s : %4.4X\n", name,
				       ACPI_GET16(target));
			break;

		case ACPI_EXD_UINT32:

			acpi_os_printf("%20s : %8.8X\n", name,
				       ACPI_GET32(target));
			break;

		case ACPI_EXD_UINT64:

			acpi_os_printf("%20s : %8.8X%8.8X\n", "Value",
				       ACPI_FORMAT_UINT64(ACPI_GET64(target)));
			break;

		case ACPI_EXD_POINTER:
		case ACPI_EXD_ADDRESS:

			acpi_ex_out_pointer(name,
					    *ACPI_CAST_PTR(void *, target));
			break;

		case ACPI_EXD_STRING:

			acpi_ut_print_string(obj_desc->string.pointer,
					     ACPI_UINT8_MAX);
			acpi_os_printf("\n");
			break;

		case ACPI_EXD_BUFFER:

			ACPI_DUMP_BUFFER(obj_desc->buffer.pointer,
					 obj_desc->buffer.length);
			break;

		case ACPI_EXD_PACKAGE:

			/* Dump the package contents */

			acpi_os_printf("\nPackage Contents:\n");
			acpi_ex_dump_package_obj(obj_desc, 0, 0);
			break;

		case ACPI_EXD_FIELD:

			acpi_ex_dump_object(obj_desc,
					    acpi_ex_dump_field_common);
			break;

		case ACPI_EXD_REFERENCE:

			acpi_ex_out_string("Class Name",
					   acpi_ut_get_reference_name
					   (obj_desc));
			acpi_ex_dump_reference_obj(obj_desc);
			break;

		case ACPI_EXD_LIST:

			start = *ACPI_CAST_PTR(void *, target);
			next = start;

			acpi_os_printf("%20s : %p ", name, next);
			if (next) {
				acpi_os_printf("%s (Type %2.2X)",
					       acpi_ut_get_object_type_name
					       (next), next->common.type);

				while (next->common.next_object) {
					if ((next->common.type ==
					     ACPI_TYPE_LOCAL_DATA) && !data) {
						data = next;
					}

					next = next->common.next_object;
					acpi_os_printf("->%p(%s %2.2X)", next,
						       acpi_ut_get_object_type_name
						       (next),
						       next->common.type);

					if ((next == start) || (next == data)) {
						acpi_os_printf
						    ("\n**** Error: Object list appears to be circular linked");
						break;
					}
				}
			} else {
				acpi_os_printf("- No attached objects");
			}

			acpi_os_printf("\n");
			break;

		case ACPI_EXD_HDLR_LIST:

			start = *ACPI_CAST_PTR(void *, target);
			next = start;

			acpi_os_printf("%20s : %p", name, next);
			if (next) {
				acpi_os_printf("(%s %2.2X)",
					       acpi_ut_get_object_type_name
					       (next),
					       next->address_space.space_id);

				while (next->address_space.next) {
					if ((next->common.type ==
					     ACPI_TYPE_LOCAL_DATA) && !data) {
						data = next;
					}

					next = next->address_space.next;
					acpi_os_printf("->%p(%s %2.2X)", next,
						       acpi_ut_get_object_type_name
						       (next),
						       next->address_space.
						       space_id);

					if ((next == start) || (next == data)) {
						acpi_os_printf
						    ("\n**** Error: Handler list appears to be circular linked");
						break;
					}
				}
			}

			acpi_os_printf("\n");
			break;

		case ACPI_EXD_RGN_LIST:

			start = *ACPI_CAST_PTR(void *, target);
			next = start;

			acpi_os_printf("%20s : %p", name, next);
			if (next) {
				acpi_os_printf("(%s %2.2X)",
					       acpi_ut_get_object_type_name
					       (next), next->common.type);

				while (next->region.next) {
					if ((next->common.type ==
					     ACPI_TYPE_LOCAL_DATA) && !data) {
						data = next;
					}

					next = next->region.next;
					acpi_os_printf("->%p(%s %2.2X)", next,
						       acpi_ut_get_object_type_name
						       (next),
						       next->common.type);

					if ((next == start) || (next == data)) {
						acpi_os_printf
						    ("\n**** Error: Region list appears to be circular linked");
						break;
					}
				}
			}

			acpi_os_printf("\n");
			break;

		case ACPI_EXD_NODE:

			node =
			    *ACPI_CAST_PTR(struct acpi_namespace_node *,
					   target);

			acpi_os_printf("%20s : %p", name, node);
			if (node) {
				acpi_os_printf(" [%4.4s]", node->name.ascii);
			}
			acpi_os_printf("\n");
			break;

		default:

			acpi_os_printf("**** Invalid table opcode [%X] ****\n",
				       info->opcode);
			return;
		}

		info++;
		count--;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_operand
 *
 * PARAMETERS:  *obj_desc       - Pointer to entry to be dumped
 *              depth           - Current nesting depth
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump an operand object
 *
 ******************************************************************************/

void acpi_ex_dump_operand(union acpi_operand_object *obj_desc, u32 depth)
{
	u32 length;
	u32 index;

	ACPI_FUNCTION_NAME(ex_dump_operand);

	/* Check if debug output enabled */

	if (!ACPI_IS_DEBUG_ENABLED(ACPI_LV_EXEC, _COMPONENT)) {
		return;
	}

	if (!obj_desc) {

		/* This could be a null element of a package */

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Null Object Descriptor\n"));
		return;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE(obj_desc) == ACPI_DESC_TYPE_NAMED) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "%p Namespace Node: ",
				  obj_desc));
		ACPI_DUMP_ENTRY(obj_desc, ACPI_LV_EXEC);
		return;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE(obj_desc) != ACPI_DESC_TYPE_OPERAND) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "%p is not a node or operand object: [%s]\n",
				  obj_desc,
				  acpi_ut_get_descriptor_name(obj_desc)));
		ACPI_DUMP_BUFFER(obj_desc, sizeof(union acpi_operand_object));
		return;
	}

	/* obj_desc is a valid object */

	if (depth > 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "%*s[%u] %p Refs=%u ",
				  depth, " ", depth, obj_desc,
				  obj_desc->common.reference_count));
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "%p Refs=%u ",
				  obj_desc, obj_desc->common.reference_count));
	}

	/* Decode object type */

	switch (obj_desc->common.type) {
	case ACPI_TYPE_LOCAL_REFERENCE:

		acpi_os_printf("Reference: [%s] ",
			       acpi_ut_get_reference_name(obj_desc));

		switch (obj_desc->reference.class) {
		case ACPI_REFCLASS_DEBUG:

			acpi_os_printf("\n");
			break;

		case ACPI_REFCLASS_INDEX:

			acpi_os_printf("%p\n", obj_desc->reference.object);
			break;

		case ACPI_REFCLASS_TABLE:

			acpi_os_printf("Table Index %X\n",
				       obj_desc->reference.value);
			break;

		case ACPI_REFCLASS_REFOF:

			acpi_os_printf("%p [%s]\n", obj_desc->reference.object,
				       acpi_ut_get_type_name(((union
							       acpi_operand_object
							       *)
							      obj_desc->
							      reference.
							      object)->common.
							     type));
			break;

		case ACPI_REFCLASS_NAME:

			acpi_ut_repair_name(obj_desc->reference.node->name.
					    ascii);
			acpi_os_printf("- [%4.4s] (Node %p)\n",
				       obj_desc->reference.node->name.ascii,
				       obj_desc->reference.node);
			break;

		case ACPI_REFCLASS_ARG:
		case ACPI_REFCLASS_LOCAL:

			acpi_os_printf("%X\n", obj_desc->reference.value);
			break;

		default:	/* Unknown reference class */

			acpi_os_printf("%2.2X\n", obj_desc->reference.class);
			break;
		}
		break;

	case ACPI_TYPE_BUFFER:

		acpi_os_printf("Buffer length %.2X @ %p\n",
			       obj_desc->buffer.length,
			       obj_desc->buffer.pointer);

		/* Debug only -- dump the buffer contents */

		if (obj_desc->buffer.pointer) {
			length = obj_desc->buffer.length;
			if (length > 128) {
				length = 128;
			}

			acpi_os_printf
			    ("Buffer Contents: (displaying length 0x%.2X)\n",
			     length);
			ACPI_DUMP_BUFFER(obj_desc->buffer.pointer, length);
		}
		break;

	case ACPI_TYPE_INTEGER:

		acpi_os_printf("Integer %8.8X%8.8X\n",
			       ACPI_FORMAT_UINT64(obj_desc->integer.value));
		break;

	case ACPI_TYPE_PACKAGE:

		acpi_os_printf("Package [Len %X] ElementArray %p\n",
			       obj_desc->package.count,
			       obj_desc->package.elements);

		/*
		 * If elements exist, package element pointer is valid,
		 * and debug_level exceeds 1, dump package's elements.
		 */
		if (obj_desc->package.count &&
		    obj_desc->package.elements && acpi_dbg_level > 1) {
			for (index = 0; index < obj_desc->package.count;
			     index++) {
				acpi_ex_dump_operand(obj_desc->package.
						     elements[index],
						     depth + 1);
			}
		}
		break;

	case ACPI_TYPE_REGION:

		acpi_os_printf("Region %s (%X)",
			       acpi_ut_get_region_name(obj_desc->region.
						       space_id),
			       obj_desc->region.space_id);

		/*
		 * If the address and length have not been evaluated,
		 * don't print them.
		 */
		if (!(obj_desc->region.flags & AOPOBJ_DATA_VALID)) {
			acpi_os_printf("\n");
		} else {
			acpi_os_printf(" base %8.8X%8.8X Length %X\n",
				       ACPI_FORMAT_UINT64(obj_desc->region.
							  address),
				       obj_desc->region.length);
		}
		break;

	case ACPI_TYPE_STRING:

		acpi_os_printf("String length %X @ %p ",
			       obj_desc->string.length,
			       obj_desc->string.pointer);

		acpi_ut_print_string(obj_desc->string.pointer, ACPI_UINT8_MAX);
		acpi_os_printf("\n");
		break;

	case ACPI_TYPE_LOCAL_BANK_FIELD:

		acpi_os_printf("BankField\n");
		break;

	case ACPI_TYPE_LOCAL_REGION_FIELD:

		acpi_os_printf
		    ("RegionField: Bits=%X AccWidth=%X Lock=%X Update=%X at "
		     "byte=%X bit=%X of below:\n", obj_desc->field.bit_length,
		     obj_desc->field.access_byte_width,
		     obj_desc->field.field_flags & AML_FIELD_LOCK_RULE_MASK,
		     obj_desc->field.field_flags & AML_FIELD_UPDATE_RULE_MASK,
		     obj_desc->field.base_byte_offset,
		     obj_desc->field.start_field_bit_offset);

		acpi_ex_dump_operand(obj_desc->field.region_obj, depth + 1);
		break;

	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		acpi_os_printf("IndexField\n");
		break;

	case ACPI_TYPE_BUFFER_FIELD:

		acpi_os_printf("BufferField: %X bits at byte %X bit %X of\n",
			       obj_desc->buffer_field.bit_length,
			       obj_desc->buffer_field.base_byte_offset,
			       obj_desc->buffer_field.start_field_bit_offset);

		if (!obj_desc->buffer_field.buffer_obj) {
			ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "*NULL*\n"));
		} else if ((obj_desc->buffer_field.buffer_obj)->common.type !=
			   ACPI_TYPE_BUFFER) {
			acpi_os_printf("*not a Buffer*\n");
		} else {
			acpi_ex_dump_operand(obj_desc->buffer_field.buffer_obj,
					     depth + 1);
		}
		break;

	case ACPI_TYPE_EVENT:

		acpi_os_printf("Event\n");
		break;

	case ACPI_TYPE_METHOD:

		acpi_os_printf("Method(%X) @ %p:%X\n",
			       obj_desc->method.param_count,
			       obj_desc->method.aml_start,
			       obj_desc->method.aml_length);
		break;

	case ACPI_TYPE_MUTEX:

		acpi_os_printf("Mutex\n");
		break;

	case ACPI_TYPE_DEVICE:

		acpi_os_printf("Device\n");
		break;

	case ACPI_TYPE_POWER:

		acpi_os_printf("Power\n");
		break;

	case ACPI_TYPE_PROCESSOR:

		acpi_os_printf("Processor\n");
		break;

	case ACPI_TYPE_THERMAL:

		acpi_os_printf("Thermal\n");
		break;

	default:

		/* Unknown Type */

		acpi_os_printf("Unknown Type %X\n", obj_desc->common.type);
		break;
	}

	return;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_operands
 *
 * PARAMETERS:  operands            - A list of Operand objects
 *		opcode_name	    - AML opcode name
 *		num_operands	    - Operand count for this opcode
 *
 * DESCRIPTION: Dump the operands associated with the opcode
 *
 ******************************************************************************/

void
acpi_ex_dump_operands(union acpi_operand_object **operands,
		      const char *opcode_name, u32 num_operands)
{
	ACPI_FUNCTION_TRACE(ex_dump_operands);

	if (!opcode_name) {
		opcode_name = "UNKNOWN";
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "**** Start operand dump for opcode [%s], %u operands\n",
			  opcode_name, num_operands));

	if (num_operands == 0) {
		num_operands = 1;
	}

	/* Dump the individual operands */

	while (num_operands) {
		acpi_ex_dump_operand(*operands, 0);
		operands++;
		num_operands--;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "**** End operand dump for [%s]\n", opcode_name));
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_out* functions
 *
 * PARAMETERS:  title               - Descriptive text
 *              value               - Value to be displayed
 *
 * DESCRIPTION: Object dump output formatting functions. These functions
 *              reduce the number of format strings required and keeps them
 *              all in one place for easy modification.
 *
 ******************************************************************************/

static void acpi_ex_out_string(const char *title, const char *value)
{
	acpi_os_printf("%20s : %s\n", title, value);
}

static void acpi_ex_out_pointer(const char *title, const void *value)
{
	acpi_os_printf("%20s : %p\n", title, value);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_namespace_node
 *
 * PARAMETERS:  node                - Descriptor to dump
 *              flags               - Force display if TRUE
 *
 * DESCRIPTION: Dumps the members of the given.Node
 *
 ******************************************************************************/

void acpi_ex_dump_namespace_node(struct acpi_namespace_node *node, u32 flags)
{

	ACPI_FUNCTION_ENTRY();

	if (!flags) {

		/* Check if debug output enabled */

		if (!ACPI_IS_DEBUG_ENABLED(ACPI_LV_OBJECTS, _COMPONENT)) {
			return;
		}
	}

	acpi_os_printf("%20s : %4.4s\n", "Name", acpi_ut_get_node_name(node));
	acpi_os_printf("%20s : %2.2X [%s]\n", "Type",
		       node->type, acpi_ut_get_type_name(node->type));

	acpi_ex_dump_object(ACPI_CAST_PTR(union acpi_operand_object, node),
			    acpi_ex_dump_node);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_reference_obj
 *
 * PARAMETERS:  object              - Descriptor to dump
 *
 * DESCRIPTION: Dumps a reference object
 *
 ******************************************************************************/

static void acpi_ex_dump_reference_obj(union acpi_operand_object *obj_desc)
{
	struct acpi_buffer ret_buf;
	acpi_status status;

	ret_buf.length = ACPI_ALLOCATE_LOCAL_BUFFER;

	if (obj_desc->reference.class == ACPI_REFCLASS_NAME) {
		acpi_os_printf(" %p ", obj_desc->reference.node);

		status = acpi_ns_handle_to_pathname(obj_desc->reference.node,
						    &ret_buf, TRUE);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf
			    (" Could not convert name to pathname: %s\n",
			     acpi_format_exception(status));
		} else {
			acpi_os_printf("%s: %s\n",
				       acpi_ut_get_type_name(obj_desc->
							     reference.node->
							     type),
				       (char *)ret_buf.pointer);
			ACPI_FREE(ret_buf.pointer);
		}
	} else if (obj_desc->reference.object) {
		if (ACPI_GET_DESCRIPTOR_TYPE(obj_desc) ==
		    ACPI_DESC_TYPE_OPERAND) {
			acpi_os_printf("%22s %p", "Target :",
				       obj_desc->reference.object);
			if (obj_desc->reference.class == ACPI_REFCLASS_TABLE) {
				acpi_os_printf(" Table Index: %X\n",
					       obj_desc->reference.value);
			} else {
				acpi_os_printf(" [%s]\n",
					       acpi_ut_get_type_name(((union
								       acpi_operand_object
								       *)
								      obj_desc->
								      reference.
								      object)->
								     common.
								     type));
			}
		} else {
			acpi_os_printf(" Target: %p\n",
				       obj_desc->reference.object);
		}
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_package_obj
 *
 * PARAMETERS:  obj_desc            - Descriptor to dump
 *              level               - Indentation Level
 *              index               - Package index for this object
 *
 * DESCRIPTION: Dumps the elements of the package
 *
 ******************************************************************************/

static void
acpi_ex_dump_package_obj(union acpi_operand_object *obj_desc,
			 u32 level, u32 index)
{
	u32 i;

	/* Indentation and index output */

	if (level > 0) {
		for (i = 0; i < level; i++) {
			acpi_os_printf(" ");
		}

		acpi_os_printf("[%.2d] ", index);
	}

	acpi_os_printf("%p ", obj_desc);

	/* Null package elements are allowed */

	if (!obj_desc) {
		acpi_os_printf("[Null Object]\n");
		return;
	}

	/* Packages may only contain a few object types */

	switch (obj_desc->common.type) {
	case ACPI_TYPE_INTEGER:

		acpi_os_printf("[Integer] = %8.8X%8.8X\n",
			       ACPI_FORMAT_UINT64(obj_desc->integer.value));
		break;

	case ACPI_TYPE_STRING:

		acpi_os_printf("[String] Value: ");
		acpi_ut_print_string(obj_desc->string.pointer, ACPI_UINT8_MAX);
		acpi_os_printf("\n");
		break;

	case ACPI_TYPE_BUFFER:

		acpi_os_printf("[Buffer] Length %.2X = ",
			       obj_desc->buffer.length);
		if (obj_desc->buffer.length) {
			acpi_ut_debug_dump_buffer(ACPI_CAST_PTR
						  (u8,
						   obj_desc->buffer.pointer),
						  obj_desc->buffer.length,
						  DB_DWORD_DISPLAY, _COMPONENT);
		} else {
			acpi_os_printf("\n");
		}
		break;

	case ACPI_TYPE_PACKAGE:

		acpi_os_printf("[Package] Contains %u Elements:\n",
			       obj_desc->package.count);

		for (i = 0; i < obj_desc->package.count; i++) {
			acpi_ex_dump_package_obj(obj_desc->package.elements[i],
						 level + 1, i);
		}
		break;

	case ACPI_TYPE_LOCAL_REFERENCE:

		acpi_os_printf("[Object Reference] Class [%s]",
			       acpi_ut_get_reference_name(obj_desc));
		acpi_ex_dump_reference_obj(obj_desc);
		break;

	default:

		acpi_os_printf("[%s] Type: %2.2X\n",
			       acpi_ut_get_type_name(obj_desc->common.type),
			       obj_desc->common.type);
		break;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_object_descriptor
 *
 * PARAMETERS:  obj_desc            - Descriptor to dump
 *              flags               - Force display if TRUE
 *
 * DESCRIPTION: Dumps the members of the object descriptor given.
 *
 ******************************************************************************/

void
acpi_ex_dump_object_descriptor(union acpi_operand_object *obj_desc, u32 flags)
{
	ACPI_FUNCTION_TRACE(ex_dump_object_descriptor);

	if (!obj_desc) {
		return_VOID;
	}

	if (!flags) {

		/* Check if debug output enabled */

		if (!ACPI_IS_DEBUG_ENABLED(ACPI_LV_OBJECTS, _COMPONENT)) {
			return_VOID;
		}
	}

	if (ACPI_GET_DESCRIPTOR_TYPE(obj_desc) == ACPI_DESC_TYPE_NAMED) {
		acpi_ex_dump_namespace_node((struct acpi_namespace_node *)
					    obj_desc, flags);

		obj_desc = ((struct acpi_namespace_node *)obj_desc)->object;
		if (!obj_desc) {
			return_VOID;
		}

		acpi_os_printf("\nAttached Object %p", obj_desc);
		if (ACPI_GET_DESCRIPTOR_TYPE(obj_desc) == ACPI_DESC_TYPE_NAMED) {
			acpi_os_printf(" - Namespace Node");
		}

		acpi_os_printf(":\n");
		goto dump_object;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE(obj_desc) != ACPI_DESC_TYPE_OPERAND) {
		acpi_os_printf("%p is not an ACPI operand object: [%s]\n",
			       obj_desc, acpi_ut_get_descriptor_name(obj_desc));
		return_VOID;
	}

	/* Validate the object type */

	if (obj_desc->common.type > ACPI_TYPE_LOCAL_MAX) {
		acpi_os_printf("Not a known object type: %2.2X\n",
			       obj_desc->common.type);
		return_VOID;
	}

dump_object:

	if (!obj_desc) {
		return_VOID;
	}

	/* Common Fields */

	acpi_ex_dump_object(obj_desc, acpi_ex_dump_common);

	/* Object-specific fields */

	acpi_ex_dump_object(obj_desc, acpi_ex_dump_info[obj_desc->common.type]);

	if (obj_desc->common.type == ACPI_TYPE_REGION) {
		obj_desc = obj_desc->common.next_object;
		if (obj_desc->common.type > ACPI_TYPE_LOCAL_MAX) {
			acpi_os_printf
			    ("Secondary object is not a known object type: %2.2X\n",
			     obj_desc->common.type);

			return_VOID;
		}

		acpi_os_printf("\nExtra attached Object (%p):\n", obj_desc);
		acpi_ex_dump_object(obj_desc,
				    acpi_ex_dump_info[obj_desc->common.type]);
	}

	return_VOID;
}

#endif
