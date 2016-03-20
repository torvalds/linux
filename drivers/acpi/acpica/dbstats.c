/*******************************************************************************
 *
 * Module Name: dbstats - Generation and display of ACPI table statistics
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
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
#include "acdebug.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbstats")

/* Local prototypes */
static void acpi_db_count_namespace_objects(void);

static void acpi_db_enumerate_object(union acpi_operand_object *obj_desc);

static acpi_status
acpi_db_classify_one_object(acpi_handle obj_handle,
			    u32 nesting_level,
			    void *context, void **return_value);

#if defined ACPI_DBG_TRACK_ALLOCATIONS || defined ACPI_USE_LOCAL_CACHE
static void acpi_db_list_info(struct acpi_memory_list *list);
#endif

/*
 * Statistics subcommands
 */
static struct acpi_db_argument_info acpi_db_stat_types[] = {
	{"ALLOCATIONS"},
	{"OBJECTS"},
	{"MEMORY"},
	{"MISC"},
	{"TABLES"},
	{"SIZES"},
	{"STACK"},
	{NULL}			/* Must be null terminated */
};

#define CMD_STAT_ALLOCATIONS     0
#define CMD_STAT_OBJECTS         1
#define CMD_STAT_MEMORY          2
#define CMD_STAT_MISC            3
#define CMD_STAT_TABLES          4
#define CMD_STAT_SIZES           5
#define CMD_STAT_STACK           6

#if defined ACPI_DBG_TRACK_ALLOCATIONS || defined ACPI_USE_LOCAL_CACHE
/*******************************************************************************
 *
 * FUNCTION:    acpi_db_list_info
 *
 * PARAMETERS:  list            - Memory list/cache to be displayed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display information about the input memory list or cache.
 *
 ******************************************************************************/

static void acpi_db_list_info(struct acpi_memory_list *list)
{
#ifdef ACPI_DBG_TRACK_ALLOCATIONS
	u32 outstanding;
#endif

	acpi_os_printf("\n%s\n", list->list_name);

	/* max_depth > 0 indicates a cache object */

	if (list->max_depth > 0) {
		acpi_os_printf
		    ("    Cache: [Depth    MaxD Avail  Size]                "
		     "%8.2X %8.2X %8.2X %8.2X\n", list->current_depth,
		     list->max_depth, list->max_depth - list->current_depth,
		     (list->current_depth * list->object_size));
	}
#ifdef ACPI_DBG_TRACK_ALLOCATIONS
	if (list->max_depth > 0) {
		acpi_os_printf
		    ("    Cache: [Requests Hits Misses ObjSize]             "
		     "%8.2X %8.2X %8.2X %8.2X\n", list->requests, list->hits,
		     list->requests - list->hits, list->object_size);
	}

	outstanding = acpi_db_get_cache_info(list);

	if (list->object_size) {
		acpi_os_printf
		    ("    Mem:   [Alloc    Free Max    CurSize Outstanding] "
		     "%8.2X %8.2X %8.2X %8.2X %8.2X\n", list->total_allocated,
		     list->total_freed, list->max_occupied,
		     outstanding * list->object_size, outstanding);
	} else {
		acpi_os_printf
		    ("    Mem:   [Alloc Free Max CurSize Outstanding Total] "
		     "%8.2X %8.2X %8.2X %8.2X %8.2X %8.2X\n",
		     list->total_allocated, list->total_freed,
		     list->max_occupied, list->current_total_size, outstanding,
		     list->total_size);
	}
#endif
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_enumerate_object
 *
 * PARAMETERS:  obj_desc            - Object to be counted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add this object to the global counts, by object type.
 *              Limited recursion handles subobjects and packages, and this
 *              is probably acceptable within the AML debugger only.
 *
 ******************************************************************************/

static void acpi_db_enumerate_object(union acpi_operand_object *obj_desc)
{
	u32 i;

	if (!obj_desc) {
		return;
	}

	/* Enumerate this object first */

	acpi_gbl_num_objects++;

	if (obj_desc->common.type > ACPI_TYPE_NS_NODE_MAX) {
		acpi_gbl_obj_type_count_misc++;
	} else {
		acpi_gbl_obj_type_count[obj_desc->common.type]++;
	}

	/* Count the sub-objects */

	switch (obj_desc->common.type) {
	case ACPI_TYPE_PACKAGE:

		for (i = 0; i < obj_desc->package.count; i++) {
			acpi_db_enumerate_object(obj_desc->package.elements[i]);
		}
		break;

	case ACPI_TYPE_DEVICE:

		acpi_db_enumerate_object(obj_desc->device.notify_list[0]);
		acpi_db_enumerate_object(obj_desc->device.notify_list[1]);
		acpi_db_enumerate_object(obj_desc->device.handler);
		break;

	case ACPI_TYPE_BUFFER_FIELD:

		if (acpi_ns_get_secondary_object(obj_desc)) {
			acpi_gbl_obj_type_count[ACPI_TYPE_BUFFER_FIELD]++;
		}
		break;

	case ACPI_TYPE_REGION:

		acpi_gbl_obj_type_count[ACPI_TYPE_LOCAL_REGION_FIELD]++;
		acpi_db_enumerate_object(obj_desc->region.handler);
		break;

	case ACPI_TYPE_POWER:

		acpi_db_enumerate_object(obj_desc->power_resource.
					 notify_list[0]);
		acpi_db_enumerate_object(obj_desc->power_resource.
					 notify_list[1]);
		break;

	case ACPI_TYPE_PROCESSOR:

		acpi_db_enumerate_object(obj_desc->processor.notify_list[0]);
		acpi_db_enumerate_object(obj_desc->processor.notify_list[1]);
		acpi_db_enumerate_object(obj_desc->processor.handler);
		break;

	case ACPI_TYPE_THERMAL:

		acpi_db_enumerate_object(obj_desc->thermal_zone.notify_list[0]);
		acpi_db_enumerate_object(obj_desc->thermal_zone.notify_list[1]);
		acpi_db_enumerate_object(obj_desc->thermal_zone.handler);
		break;

	default:

		break;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_classify_one_object
 *
 * PARAMETERS:  Callback for walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enumerate both the object descriptor (including subobjects) and
 *              the parent namespace node.
 *
 ******************************************************************************/

static acpi_status
acpi_db_classify_one_object(acpi_handle obj_handle,
			    u32 nesting_level,
			    void *context, void **return_value)
{
	struct acpi_namespace_node *node;
	union acpi_operand_object *obj_desc;
	u32 type;

	acpi_gbl_num_nodes++;

	node = (struct acpi_namespace_node *)obj_handle;
	obj_desc = acpi_ns_get_attached_object(node);

	acpi_db_enumerate_object(obj_desc);

	type = node->type;
	if (type > ACPI_TYPE_NS_NODE_MAX) {
		acpi_gbl_node_type_count_misc++;
	} else {
		acpi_gbl_node_type_count[type]++;
	}

	return (AE_OK);

#ifdef ACPI_FUTURE_IMPLEMENTATION

	/* TBD: These need to be counted during the initial parsing phase */

	if (acpi_ps_is_named_op(op->opcode)) {
		num_nodes++;
	}

	if (is_method) {
		num_method_elements++;
	}

	num_grammar_elements++;
	op = acpi_ps_get_depth_next(root, op);

	size_of_parse_tree = (num_grammar_elements - num_method_elements) *
	    (u32)sizeof(union acpi_parse_object);
	size_of_method_trees =
	    num_method_elements * (u32)sizeof(union acpi_parse_object);
	size_of_node_entries =
	    num_nodes * (u32)sizeof(struct acpi_namespace_node);
	size_of_acpi_objects =
	    num_nodes * (u32)sizeof(union acpi_operand_object);
#endif
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_count_namespace_objects
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Count and classify the entire namespace, including all
 *              namespace nodes and attached objects.
 *
 ******************************************************************************/

static void acpi_db_count_namespace_objects(void)
{
	u32 i;

	acpi_gbl_num_nodes = 0;
	acpi_gbl_num_objects = 0;

	acpi_gbl_obj_type_count_misc = 0;
	for (i = 0; i < (ACPI_TYPE_NS_NODE_MAX - 1); i++) {
		acpi_gbl_obj_type_count[i] = 0;
		acpi_gbl_node_type_count[i] = 0;
	}

	(void)acpi_ns_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX, FALSE,
				     acpi_db_classify_one_object, NULL, NULL,
				     NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_statistics
 *
 * PARAMETERS:  type_arg        - Subcommand
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display various statistics
 *
 ******************************************************************************/

acpi_status acpi_db_display_statistics(char *type_arg)
{
	u32 i;
	u32 temp;

	acpi_ut_strupr(type_arg);
	temp = acpi_db_match_argument(type_arg, acpi_db_stat_types);
	if (temp == ACPI_TYPE_NOT_FOUND) {
		acpi_os_printf("Invalid or unsupported argument\n");
		return (AE_OK);
	}

	switch (temp) {
	case CMD_STAT_ALLOCATIONS:

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
		acpi_ut_dump_allocation_info();
#endif
		break;

	case CMD_STAT_TABLES:

		acpi_os_printf("ACPI Table Information (not implemented):\n\n");
		break;

	case CMD_STAT_OBJECTS:

		acpi_db_count_namespace_objects();

		acpi_os_printf
		    ("\nObjects defined in the current namespace:\n\n");

		acpi_os_printf("%16.16s %10.10s %10.10s\n",
			       "ACPI_TYPE", "NODES", "OBJECTS");

		for (i = 0; i < ACPI_TYPE_NS_NODE_MAX; i++) {
			acpi_os_printf("%16.16s % 10ld% 10ld\n",
				       acpi_ut_get_type_name(i),
				       acpi_gbl_node_type_count[i],
				       acpi_gbl_obj_type_count[i]);
		}

		acpi_os_printf("%16.16s % 10ld% 10ld\n", "Misc/Unknown",
			       acpi_gbl_node_type_count_misc,
			       acpi_gbl_obj_type_count_misc);

		acpi_os_printf("%16.16s % 10ld% 10ld\n", "TOTALS:",
			       acpi_gbl_num_nodes, acpi_gbl_num_objects);
		break;

	case CMD_STAT_MEMORY:

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
		acpi_os_printf
		    ("\n----Object Statistics (all in hex)---------\n");

		acpi_db_list_info(acpi_gbl_global_list);
		acpi_db_list_info(acpi_gbl_ns_node_list);
#endif

#ifdef ACPI_USE_LOCAL_CACHE
		acpi_os_printf
		    ("\n----Cache Statistics (all in hex)---------\n");
		acpi_db_list_info(acpi_gbl_operand_cache);
		acpi_db_list_info(acpi_gbl_ps_node_cache);
		acpi_db_list_info(acpi_gbl_ps_node_ext_cache);
		acpi_db_list_info(acpi_gbl_state_cache);
#endif

		break;

	case CMD_STAT_MISC:

		acpi_os_printf("\nMiscellaneous Statistics:\n\n");
		acpi_os_printf("Calls to AcpiPsFind:.. ........% 7ld\n",
			       acpi_gbl_ps_find_count);
		acpi_os_printf("Calls to AcpiNsLookup:..........% 7ld\n",
			       acpi_gbl_ns_lookup_count);

		acpi_os_printf("\n");

		acpi_os_printf("Mutex usage:\n\n");
		for (i = 0; i < ACPI_NUM_MUTEX; i++) {
			acpi_os_printf("%-28s:     % 7ld\n",
				       acpi_ut_get_mutex_name(i),
				       acpi_gbl_mutex_info[i].use_count);
		}
		break;

	case CMD_STAT_SIZES:

		acpi_os_printf("\nInternal object sizes:\n\n");

		acpi_os_printf("Common         %3d\n",
			       sizeof(struct acpi_object_common));
		acpi_os_printf("Number         %3d\n",
			       sizeof(struct acpi_object_integer));
		acpi_os_printf("String         %3d\n",
			       sizeof(struct acpi_object_string));
		acpi_os_printf("Buffer         %3d\n",
			       sizeof(struct acpi_object_buffer));
		acpi_os_printf("Package        %3d\n",
			       sizeof(struct acpi_object_package));
		acpi_os_printf("BufferField    %3d\n",
			       sizeof(struct acpi_object_buffer_field));
		acpi_os_printf("Device         %3d\n",
			       sizeof(struct acpi_object_device));
		acpi_os_printf("Event          %3d\n",
			       sizeof(struct acpi_object_event));
		acpi_os_printf("Method         %3d\n",
			       sizeof(struct acpi_object_method));
		acpi_os_printf("Mutex          %3d\n",
			       sizeof(struct acpi_object_mutex));
		acpi_os_printf("Region         %3d\n",
			       sizeof(struct acpi_object_region));
		acpi_os_printf("PowerResource  %3d\n",
			       sizeof(struct acpi_object_power_resource));
		acpi_os_printf("Processor      %3d\n",
			       sizeof(struct acpi_object_processor));
		acpi_os_printf("ThermalZone    %3d\n",
			       sizeof(struct acpi_object_thermal_zone));
		acpi_os_printf("RegionField    %3d\n",
			       sizeof(struct acpi_object_region_field));
		acpi_os_printf("BankField      %3d\n",
			       sizeof(struct acpi_object_bank_field));
		acpi_os_printf("IndexField     %3d\n",
			       sizeof(struct acpi_object_index_field));
		acpi_os_printf("Reference      %3d\n",
			       sizeof(struct acpi_object_reference));
		acpi_os_printf("Notify         %3d\n",
			       sizeof(struct acpi_object_notify_handler));
		acpi_os_printf("AddressSpace   %3d\n",
			       sizeof(struct acpi_object_addr_handler));
		acpi_os_printf("Extra          %3d\n",
			       sizeof(struct acpi_object_extra));
		acpi_os_printf("Data           %3d\n",
			       sizeof(struct acpi_object_data));

		acpi_os_printf("\n");

		acpi_os_printf("ParseObject    %3d\n",
			       sizeof(struct acpi_parse_obj_common));
		acpi_os_printf("ParseObjectNamed %3d\n",
			       sizeof(struct acpi_parse_obj_named));
		acpi_os_printf("ParseObjectAsl %3d\n",
			       sizeof(struct acpi_parse_obj_asl));
		acpi_os_printf("OperandObject  %3d\n",
			       sizeof(union acpi_operand_object));
		acpi_os_printf("NamespaceNode  %3d\n",
			       sizeof(struct acpi_namespace_node));
		acpi_os_printf("AcpiObject     %3d\n",
			       sizeof(union acpi_object));

		acpi_os_printf("\n");

		acpi_os_printf("Generic State  %3d\n",
			       sizeof(union acpi_generic_state));
		acpi_os_printf("Common State   %3d\n",
			       sizeof(struct acpi_common_state));
		acpi_os_printf("Control State  %3d\n",
			       sizeof(struct acpi_control_state));
		acpi_os_printf("Update State   %3d\n",
			       sizeof(struct acpi_update_state));
		acpi_os_printf("Scope State    %3d\n",
			       sizeof(struct acpi_scope_state));
		acpi_os_printf("Parse Scope    %3d\n",
			       sizeof(struct acpi_pscope_state));
		acpi_os_printf("Package State  %3d\n",
			       sizeof(struct acpi_pkg_state));
		acpi_os_printf("Thread State   %3d\n",
			       sizeof(struct acpi_thread_state));
		acpi_os_printf("Result Values  %3d\n",
			       sizeof(struct acpi_result_values));
		acpi_os_printf("Notify Info    %3d\n",
			       sizeof(struct acpi_notify_info));
		break;

	case CMD_STAT_STACK:
#if defined(ACPI_DEBUG_OUTPUT)

		temp =
		    (u32)ACPI_PTR_DIFF(acpi_gbl_entry_stack_pointer,
				       acpi_gbl_lowest_stack_pointer);

		acpi_os_printf("\nSubsystem Stack Usage:\n\n");
		acpi_os_printf("Entry Stack Pointer        %p\n",
			       acpi_gbl_entry_stack_pointer);
		acpi_os_printf("Lowest Stack Pointer       %p\n",
			       acpi_gbl_lowest_stack_pointer);
		acpi_os_printf("Stack Use                  %X (%u)\n", temp,
			       temp);
		acpi_os_printf("Deepest Procedure Nesting  %u\n",
			       acpi_gbl_deepest_nesting);
#endif
		break;

	default:

		break;
	}

	acpi_os_printf("\n");
	return (AE_OK);
}
