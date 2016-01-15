/*******************************************************************************
 *
 * Module Name: dbdisply - debug display commands
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
#include "amlcode.h"
#include "acdispat.h"
#include "acnamesp.h"
#include "acparser.h"
#include "acinterp.h"
#include "acevents.h"
#include "acdebug.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbdisply")

/* Local prototypes */
static void acpi_db_dump_parser_descriptor(union acpi_parse_object *op);

static void *acpi_db_get_pointer(void *target);

static acpi_status
acpi_db_display_non_root_handlers(acpi_handle obj_handle,
				  u32 nesting_level,
				  void *context, void **return_value);

/*
 * System handler information.
 * Used for Handlers command, in acpi_db_display_handlers.
 */
#define ACPI_PREDEFINED_PREFIX          "%25s (%.2X) : "
#define ACPI_HANDLER_NAME_STRING               "%30s : "
#define ACPI_HANDLER_PRESENT_STRING                    "%-9s (%p)\n"
#define ACPI_HANDLER_PRESENT_STRING2                   "%-9s (%p)"
#define ACPI_HANDLER_NOT_PRESENT_STRING                "%-9s\n"

/* All predefined Address Space IDs */

static acpi_adr_space_type acpi_gbl_space_id_list[] = {
	ACPI_ADR_SPACE_SYSTEM_MEMORY,
	ACPI_ADR_SPACE_SYSTEM_IO,
	ACPI_ADR_SPACE_PCI_CONFIG,
	ACPI_ADR_SPACE_EC,
	ACPI_ADR_SPACE_SMBUS,
	ACPI_ADR_SPACE_CMOS,
	ACPI_ADR_SPACE_PCI_BAR_TARGET,
	ACPI_ADR_SPACE_IPMI,
	ACPI_ADR_SPACE_GPIO,
	ACPI_ADR_SPACE_GSBUS,
	ACPI_ADR_SPACE_DATA_TABLE,
	ACPI_ADR_SPACE_FIXED_HARDWARE
};

/* Global handler information */

typedef struct acpi_handler_info {
	void *handler;
	char *name;

} acpi_handler_info;

static struct acpi_handler_info acpi_gbl_handler_list[] = {
	{&acpi_gbl_global_notify[0].handler, "System Notifications"},
	{&acpi_gbl_global_notify[1].handler, "Device Notifications"},
	{&acpi_gbl_table_handler, "ACPI Table Events"},
	{&acpi_gbl_exception_handler, "Control Method Exceptions"},
	{&acpi_gbl_interface_handler, "OSI Invocations"}
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_get_pointer
 *
 * PARAMETERS:  target          - Pointer to string to be converted
 *
 * RETURN:      Converted pointer
 *
 * DESCRIPTION: Convert an ascii pointer value to a real value
 *
 ******************************************************************************/

static void *acpi_db_get_pointer(void *target)
{
	void *obj_ptr;
	acpi_size address;

	address = strtoul(target, NULL, 16);
	obj_ptr = ACPI_TO_POINTER(address);
	return (obj_ptr);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_dump_parser_descriptor
 *
 * PARAMETERS:  op              - A parser Op descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display a formatted parser object
 *
 ******************************************************************************/

static void acpi_db_dump_parser_descriptor(union acpi_parse_object *op)
{
	const struct acpi_opcode_info *info;

	info = acpi_ps_get_opcode_info(op->common.aml_opcode);

	acpi_os_printf("Parser Op Descriptor:\n");
	acpi_os_printf("%20.20s : %4.4X\n", "Opcode", op->common.aml_opcode);

	ACPI_DEBUG_ONLY_MEMBERS(acpi_os_printf("%20.20s : %s\n", "Opcode Name",
					       info->name));

	acpi_os_printf("%20.20s : %p\n", "Value/ArgList", op->common.value.arg);
	acpi_os_printf("%20.20s : %p\n", "Parent", op->common.parent);
	acpi_os_printf("%20.20s : %p\n", "NextOp", op->common.next);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_decode_and_display_object
 *
 * PARAMETERS:  target          - String with object to be displayed. Names
 *                                and hex pointers are supported.
 *              output_type     - Byte, Word, Dword, or Qword (B|W|D|Q)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display a formatted ACPI object
 *
 ******************************************************************************/

void acpi_db_decode_and_display_object(char *target, char *output_type)
{
	void *obj_ptr;
	struct acpi_namespace_node *node;
	union acpi_operand_object *obj_desc;
	u32 display = DB_BYTE_DISPLAY;
	char buffer[80];
	struct acpi_buffer ret_buf;
	acpi_status status;
	u32 size;

	if (!target) {
		return;
	}

	/* Decode the output type */

	if (output_type) {
		acpi_ut_strupr(output_type);
		if (output_type[0] == 'W') {
			display = DB_WORD_DISPLAY;
		} else if (output_type[0] == 'D') {
			display = DB_DWORD_DISPLAY;
		} else if (output_type[0] == 'Q') {
			display = DB_QWORD_DISPLAY;
		}
	}

	ret_buf.length = sizeof(buffer);
	ret_buf.pointer = buffer;

	/* Differentiate between a number and a name */

	if ((target[0] >= 0x30) && (target[0] <= 0x39)) {
		obj_ptr = acpi_db_get_pointer(target);
		if (!acpi_os_readable(obj_ptr, 16)) {
			acpi_os_printf
			    ("Address %p is invalid in this address space\n",
			     obj_ptr);
			return;
		}

		/* Decode the object type */

		switch (ACPI_GET_DESCRIPTOR_TYPE(obj_ptr)) {
		case ACPI_DESC_TYPE_NAMED:

			/* This is a namespace Node */

			if (!acpi_os_readable
			    (obj_ptr, sizeof(struct acpi_namespace_node))) {
				acpi_os_printf
				    ("Cannot read entire Named object at address %p\n",
				     obj_ptr);
				return;
			}

			node = obj_ptr;
			goto dump_node;

		case ACPI_DESC_TYPE_OPERAND:

			/* This is a ACPI OPERAND OBJECT */

			if (!acpi_os_readable
			    (obj_ptr, sizeof(union acpi_operand_object))) {
				acpi_os_printf
				    ("Cannot read entire ACPI object at address %p\n",
				     obj_ptr);
				return;
			}

			acpi_ut_debug_dump_buffer(obj_ptr,
						  sizeof(union
							 acpi_operand_object),
						  display, ACPI_UINT32_MAX);
			acpi_ex_dump_object_descriptor(obj_ptr, 1);
			break;

		case ACPI_DESC_TYPE_PARSER:

			/* This is a Parser Op object */

			if (!acpi_os_readable
			    (obj_ptr, sizeof(union acpi_parse_object))) {
				acpi_os_printf
				    ("Cannot read entire Parser object at address %p\n",
				     obj_ptr);
				return;
			}

			acpi_ut_debug_dump_buffer(obj_ptr,
						  sizeof(union
							 acpi_parse_object),
						  display, ACPI_UINT32_MAX);
			acpi_db_dump_parser_descriptor((union acpi_parse_object
							*)obj_ptr);
			break;

		default:

			/* Is not a recognizeable object */

			acpi_os_printf
			    ("Not a known ACPI internal object, descriptor type %2.2X\n",
			     ACPI_GET_DESCRIPTOR_TYPE(obj_ptr));

			size = 16;
			if (acpi_os_readable(obj_ptr, 64)) {
				size = 64;
			}

			/* Just dump some memory */

			acpi_ut_debug_dump_buffer(obj_ptr, size, display,
						  ACPI_UINT32_MAX);
			break;
		}

		return;
	}

	/* The parameter is a name string that must be resolved to a Named obj */

	node = acpi_db_local_ns_lookup(target);
	if (!node) {
		return;
	}

dump_node:
	/* Now dump the NS node */

	status = acpi_get_name(node, ACPI_FULL_PATHNAME_NO_TRAILING, &ret_buf);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could not convert name to pathname\n");
	}

	else {
		acpi_os_printf("Object (%p) Pathname: %s\n",
			       node, (char *)ret_buf.pointer);
	}

	if (!acpi_os_readable(node, sizeof(struct acpi_namespace_node))) {
		acpi_os_printf("Invalid Named object at address %p\n", node);
		return;
	}

	acpi_ut_debug_dump_buffer((void *)node,
				  sizeof(struct acpi_namespace_node), display,
				  ACPI_UINT32_MAX);
	acpi_ex_dump_namespace_node(node, 1);

	obj_desc = acpi_ns_get_attached_object(node);
	if (obj_desc) {
		acpi_os_printf("\nAttached Object (%p):\n", obj_desc);
		if (!acpi_os_readable
		    (obj_desc, sizeof(union acpi_operand_object))) {
			acpi_os_printf
			    ("Invalid internal ACPI Object at address %p\n",
			     obj_desc);
			return;
		}

		acpi_ut_debug_dump_buffer((void *)obj_desc,
					  sizeof(union acpi_operand_object),
					  display, ACPI_UINT32_MAX);
		acpi_ex_dump_object_descriptor(obj_desc, 1);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_method_info
 *
 * PARAMETERS:  start_op        - Root of the control method parse tree
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display information about the current method
 *
 ******************************************************************************/

void acpi_db_display_method_info(union acpi_parse_object *start_op)
{
	struct acpi_walk_state *walk_state;
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node;
	union acpi_parse_object *root_op;
	union acpi_parse_object *op;
	const struct acpi_opcode_info *op_info;
	u32 num_ops = 0;
	u32 num_operands = 0;
	u32 num_operators = 0;
	u32 num_remaining_ops = 0;
	u32 num_remaining_operands = 0;
	u32 num_remaining_operators = 0;
	u8 count_remaining = FALSE;

	walk_state = acpi_ds_get_current_walk_state(acpi_gbl_current_walk_list);
	if (!walk_state) {
		acpi_os_printf("There is no method currently executing\n");
		return;
	}

	obj_desc = walk_state->method_desc;
	node = walk_state->method_node;

	acpi_os_printf("Currently executing control method is [%4.4s]\n",
		       acpi_ut_get_node_name(node));
	acpi_os_printf("%X Arguments, SyncLevel = %X\n",
		       (u32)obj_desc->method.param_count,
		       (u32)obj_desc->method.sync_level);

	root_op = start_op;
	while (root_op->common.parent) {
		root_op = root_op->common.parent;
	}

	op = root_op;

	while (op) {
		if (op == start_op) {
			count_remaining = TRUE;
		}

		num_ops++;
		if (count_remaining) {
			num_remaining_ops++;
		}

		/* Decode the opcode */

		op_info = acpi_ps_get_opcode_info(op->common.aml_opcode);
		switch (op_info->class) {
		case AML_CLASS_ARGUMENT:

			if (count_remaining) {
				num_remaining_operands++;
			}

			num_operands++;
			break;

		case AML_CLASS_UNKNOWN:

			/* Bad opcode or ASCII character */

			continue;

		default:

			if (count_remaining) {
				num_remaining_operators++;
			}

			num_operators++;
			break;
		}

		op = acpi_ps_get_depth_next(start_op, op);
	}

	acpi_os_printf
	    ("Method contains:       %X AML Opcodes - %X Operators, %X Operands\n",
	     num_ops, num_operators, num_operands);

	acpi_os_printf
	    ("Remaining to execute:  %X AML Opcodes - %X Operators, %X Operands\n",
	     num_remaining_ops, num_remaining_operators,
	     num_remaining_operands);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_locals
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display all locals for the currently running control method
 *
 ******************************************************************************/

void acpi_db_display_locals(void)
{
	struct acpi_walk_state *walk_state;

	walk_state = acpi_ds_get_current_walk_state(acpi_gbl_current_walk_list);
	if (!walk_state) {
		acpi_os_printf("There is no method currently executing\n");
		return;
	}

	acpi_db_decode_locals(walk_state);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_arguments
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display all arguments for the currently running control method
 *
 ******************************************************************************/

void acpi_db_display_arguments(void)
{
	struct acpi_walk_state *walk_state;

	walk_state = acpi_ds_get_current_walk_state(acpi_gbl_current_walk_list);
	if (!walk_state) {
		acpi_os_printf("There is no method currently executing\n");
		return;
	}

	acpi_db_decode_arguments(walk_state);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_results
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display current contents of a method result stack
 *
 ******************************************************************************/

void acpi_db_display_results(void)
{
	u32 i;
	struct acpi_walk_state *walk_state;
	union acpi_operand_object *obj_desc;
	u32 result_count = 0;
	struct acpi_namespace_node *node;
	union acpi_generic_state *frame;
	u32 index;		/* Index onto current frame */

	walk_state = acpi_ds_get_current_walk_state(acpi_gbl_current_walk_list);
	if (!walk_state) {
		acpi_os_printf("There is no method currently executing\n");
		return;
	}

	obj_desc = walk_state->method_desc;
	node = walk_state->method_node;

	if (walk_state->results) {
		result_count = walk_state->result_count;
	}

	acpi_os_printf("Method [%4.4s] has %X stacked result objects\n",
		       acpi_ut_get_node_name(node), result_count);

	/* From the top element of result stack */

	frame = walk_state->results;
	index = (result_count - 1) % ACPI_RESULTS_FRAME_OBJ_NUM;

	for (i = 0; i < result_count; i++) {
		obj_desc = frame->results.obj_desc[index];
		acpi_os_printf("Result%u: ", i);
		acpi_db_display_internal_object(obj_desc, walk_state);

		if (index == 0) {
			frame = frame->results.next;
			index = ACPI_RESULTS_FRAME_OBJ_NUM;
		}

		index--;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_calling_tree
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display current calling tree of nested control methods
 *
 ******************************************************************************/

void acpi_db_display_calling_tree(void)
{
	struct acpi_walk_state *walk_state;
	struct acpi_namespace_node *node;

	walk_state = acpi_ds_get_current_walk_state(acpi_gbl_current_walk_list);
	if (!walk_state) {
		acpi_os_printf("There is no method currently executing\n");
		return;
	}

	node = walk_state->method_node;
	acpi_os_printf("Current Control Method Call Tree\n");

	while (walk_state) {
		node = walk_state->method_node;
		acpi_os_printf("  [%4.4s]\n", acpi_ut_get_node_name(node));

		walk_state = walk_state->next;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_object_type
 *
 * PARAMETERS:  object_arg      - User entered NS node handle
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display type of an arbitrary NS node
 *
 ******************************************************************************/

void acpi_db_display_object_type(char *object_arg)
{
	acpi_size arg;
	acpi_handle handle;
	struct acpi_device_info *info;
	acpi_status status;
	u32 i;

	arg = strtoul(object_arg, NULL, 16);
	handle = ACPI_TO_POINTER(arg);

	status = acpi_get_object_info(handle, &info);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could not get object info, %s\n",
			       acpi_format_exception(status));
		return;
	}

	acpi_os_printf("ADR: %8.8X%8.8X, STA: %8.8X, Flags: %X\n",
		       ACPI_FORMAT_UINT64(info->address),
		       info->current_status, info->flags);

	acpi_os_printf("S1D-%2.2X S2D-%2.2X S3D-%2.2X S4D-%2.2X\n",
		       info->highest_dstates[0], info->highest_dstates[1],
		       info->highest_dstates[2], info->highest_dstates[3]);

	acpi_os_printf("S0W-%2.2X S1W-%2.2X S2W-%2.2X S3W-%2.2X S4W-%2.2X\n",
		       info->lowest_dstates[0], info->lowest_dstates[1],
		       info->lowest_dstates[2], info->lowest_dstates[3],
		       info->lowest_dstates[4]);

	if (info->valid & ACPI_VALID_HID) {
		acpi_os_printf("HID: %s\n", info->hardware_id.string);
	}

	if (info->valid & ACPI_VALID_UID) {
		acpi_os_printf("UID: %s\n", info->unique_id.string);
	}

	if (info->valid & ACPI_VALID_CID) {
		for (i = 0; i < info->compatible_id_list.count; i++) {
			acpi_os_printf("CID %u: %s\n", i,
				       info->compatible_id_list.ids[i].string);
		}
	}

	ACPI_FREE(info);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_result_object
 *
 * PARAMETERS:  obj_desc        - Object to be displayed
 *              walk_state      - Current walk state
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display the result of an AML opcode
 *
 * Note: Curently only displays the result object if we are single stepping.
 * However, this output may be useful in other contexts and could be enabled
 * to do so if needed.
 *
 ******************************************************************************/

void
acpi_db_display_result_object(union acpi_operand_object *obj_desc,
			      struct acpi_walk_state *walk_state)
{

#ifndef ACPI_APPLICATION
	if (acpi_gbl_db_thread_id != acpi_os_get_thread_id()) {
		return;
	}
#endif

	/* Only display if single stepping */

	if (!acpi_gbl_cm_single_step) {
		return;
	}

	acpi_os_printf("ResultObj: ");
	acpi_db_display_internal_object(obj_desc, walk_state);
	acpi_os_printf("\n");
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_argument_object
 *
 * PARAMETERS:  obj_desc        - Object to be displayed
 *              walk_state      - Current walk state
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display the result of an AML opcode
 *
 ******************************************************************************/

void
acpi_db_display_argument_object(union acpi_operand_object *obj_desc,
				struct acpi_walk_state *walk_state)
{

#ifndef ACPI_APPLICATION
	if (acpi_gbl_db_thread_id != acpi_os_get_thread_id()) {
		return;
	}
#endif

	if (!acpi_gbl_cm_single_step) {
		return;
	}

	acpi_os_printf("ArgObj:  ");
	acpi_db_display_internal_object(obj_desc, walk_state);
}

#if (!ACPI_REDUCED_HARDWARE)
/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_gpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display the current GPE structures
 *
 ******************************************************************************/

void acpi_db_display_gpes(void)
{
	struct acpi_gpe_block_info *gpe_block;
	struct acpi_gpe_xrupt_info *gpe_xrupt_info;
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_gpe_register_info *gpe_register_info;
	char *gpe_type;
	struct acpi_gpe_notify_info *notify;
	u32 gpe_index;
	u32 block = 0;
	u32 i;
	u32 j;
	u32 count;
	char buffer[80];
	struct acpi_buffer ret_buf;
	acpi_status status;

	ret_buf.length = sizeof(buffer);
	ret_buf.pointer = buffer;

	block = 0;

	/* Walk the GPE lists */

	gpe_xrupt_info = acpi_gbl_gpe_xrupt_list_head;
	while (gpe_xrupt_info) {
		gpe_block = gpe_xrupt_info->gpe_block_list_head;
		while (gpe_block) {
			status = acpi_get_name(gpe_block->node,
					       ACPI_FULL_PATHNAME_NO_TRAILING,
					       &ret_buf);
			if (ACPI_FAILURE(status)) {
				acpi_os_printf
				    ("Could not convert name to pathname\n");
			}

			if (gpe_block->node == acpi_gbl_fadt_gpe_device) {
				gpe_type = "FADT-defined GPE block";
			} else {
				gpe_type = "GPE Block Device";
			}

			acpi_os_printf
			    ("\nBlock %u - Info %p  DeviceNode %p [%s] - %s\n",
			     block, gpe_block, gpe_block->node, buffer,
			     gpe_type);

			acpi_os_printf("    Registers:    %u (%u GPEs)\n",
				       gpe_block->register_count,
				       gpe_block->gpe_count);

			acpi_os_printf
			    ("    GPE range:    0x%X to 0x%X on interrupt %u\n",
			     gpe_block->block_base_number,
			     gpe_block->block_base_number +
			     (gpe_block->gpe_count - 1),
			     gpe_xrupt_info->interrupt_number);

			acpi_os_printf
			    ("    RegisterInfo: %p  Status %8.8X%8.8X Enable %8.8X%8.8X\n",
			     gpe_block->register_info,
			     ACPI_FORMAT_UINT64(gpe_block->register_info->
						status_address.address),
			     ACPI_FORMAT_UINT64(gpe_block->register_info->
						enable_address.address));

			acpi_os_printf("  EventInfo:    %p\n",
				       gpe_block->event_info);

			/* Examine each GPE Register within the block */

			for (i = 0; i < gpe_block->register_count; i++) {
				gpe_register_info =
				    &gpe_block->register_info[i];

				acpi_os_printf("    Reg %u: (GPE %.2X-%.2X)  "
					       "RunEnable %2.2X WakeEnable %2.2X"
					       " Status %8.8X%8.8X Enable %8.8X%8.8X\n",
					       i,
					       gpe_register_info->
					       base_gpe_number,
					       gpe_register_info->
					       base_gpe_number +
					       (ACPI_GPE_REGISTER_WIDTH - 1),
					       gpe_register_info->
					       enable_for_run,
					       gpe_register_info->
					       enable_for_wake,
					       ACPI_FORMAT_UINT64
					       (gpe_register_info->
						status_address.address),
					       ACPI_FORMAT_UINT64
					       (gpe_register_info->
						enable_address.address));

				/* Now look at the individual GPEs in this byte register */

				for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++) {
					gpe_index =
					    (i * ACPI_GPE_REGISTER_WIDTH) + j;
					gpe_event_info =
					    &gpe_block->event_info[gpe_index];

					if (ACPI_GPE_DISPATCH_TYPE
					    (gpe_event_info->flags) ==
					    ACPI_GPE_DISPATCH_NONE) {

						/* This GPE is not used (no method or handler), ignore it */

						continue;
					}

					acpi_os_printf
					    ("        GPE %.2X: %p  RunRefs %2.2X Flags %2.2X (",
					     gpe_block->block_base_number +
					     gpe_index, gpe_event_info,
					     gpe_event_info->runtime_count,
					     gpe_event_info->flags);

					/* Decode the flags byte */

					if (gpe_event_info->
					    flags & ACPI_GPE_LEVEL_TRIGGERED) {
						acpi_os_printf("Level, ");
					} else {
						acpi_os_printf("Edge, ");
					}

					if (gpe_event_info->
					    flags & ACPI_GPE_CAN_WAKE) {
						acpi_os_printf("CanWake, ");
					} else {
						acpi_os_printf("RunOnly, ");
					}

					switch (ACPI_GPE_DISPATCH_TYPE
						(gpe_event_info->flags)) {
					case ACPI_GPE_DISPATCH_NONE:

						acpi_os_printf("NotUsed");
						break;

					case ACPI_GPE_DISPATCH_METHOD:

						acpi_os_printf("Method");
						break;

					case ACPI_GPE_DISPATCH_HANDLER:

						acpi_os_printf("Handler");
						break;

					case ACPI_GPE_DISPATCH_NOTIFY:

						count = 0;
						notify =
						    gpe_event_info->dispatch.
						    notify_list;
						while (notify) {
							count++;
							notify = notify->next;
						}

						acpi_os_printf
						    ("Implicit Notify on %u devices",
						     count);
						break;

					case ACPI_GPE_DISPATCH_RAW_HANDLER:

						acpi_os_printf("RawHandler");
						break;

					default:

						acpi_os_printf("UNKNOWN: %X",
							       ACPI_GPE_DISPATCH_TYPE
							       (gpe_event_info->
								flags));
						break;
					}

					acpi_os_printf(")\n");
				}
			}

			block++;
			gpe_block = gpe_block->next;
		}

		gpe_xrupt_info = gpe_xrupt_info->next;
	}
}
#endif				/* !ACPI_REDUCED_HARDWARE */

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_handlers
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display the currently installed global handlers
 *
 ******************************************************************************/

void acpi_db_display_handlers(void)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *handler_obj;
	acpi_adr_space_type space_id;
	u32 i;

	/* Operation region handlers */

	acpi_os_printf("\nOperation Region Handlers at the namespace root:\n");

	obj_desc = acpi_ns_get_attached_object(acpi_gbl_root_node);
	if (obj_desc) {
		for (i = 0; i < ACPI_ARRAY_LENGTH(acpi_gbl_space_id_list); i++) {
			space_id = acpi_gbl_space_id_list[i];

			acpi_os_printf(ACPI_PREDEFINED_PREFIX,
				       acpi_ut_get_region_name((u8)space_id),
				       space_id);

			handler_obj =
			    acpi_ev_find_region_handler(space_id,
							obj_desc->common_notify.
							handler);
			if (handler_obj) {
				acpi_os_printf(ACPI_HANDLER_PRESENT_STRING,
					       (handler_obj->address_space.
						handler_flags &
						ACPI_ADDR_HANDLER_DEFAULT_INSTALLED)
					       ? "Default" : "User",
					       handler_obj->address_space.
					       handler);

				goto found_handler;
			}

			/* There is no handler for this space_id */

			acpi_os_printf("None\n");

found_handler:		;
		}

		/* Find all handlers for user-defined space_IDs */

		handler_obj = obj_desc->common_notify.handler;
		while (handler_obj) {
			if (handler_obj->address_space.space_id >=
			    ACPI_USER_REGION_BEGIN) {
				acpi_os_printf(ACPI_PREDEFINED_PREFIX,
					       "User-defined ID",
					       handler_obj->address_space.
					       space_id);
				acpi_os_printf(ACPI_HANDLER_PRESENT_STRING,
					       (handler_obj->address_space.
						handler_flags &
						ACPI_ADDR_HANDLER_DEFAULT_INSTALLED)
					       ? "Default" : "User",
					       handler_obj->address_space.
					       handler);
			}

			handler_obj = handler_obj->address_space.next;
		}
	}
#if (!ACPI_REDUCED_HARDWARE)

	/* Fixed event handlers */

	acpi_os_printf("\nFixed Event Handlers:\n");

	for (i = 0; i < ACPI_NUM_FIXED_EVENTS; i++) {
		acpi_os_printf(ACPI_PREDEFINED_PREFIX,
			       acpi_ut_get_event_name(i), i);
		if (acpi_gbl_fixed_event_handlers[i].handler) {
			acpi_os_printf(ACPI_HANDLER_PRESENT_STRING, "User",
				       acpi_gbl_fixed_event_handlers[i].
				       handler);
		} else {
			acpi_os_printf(ACPI_HANDLER_NOT_PRESENT_STRING, "None");
		}
	}

#endif				/* !ACPI_REDUCED_HARDWARE */

	/* Miscellaneous global handlers */

	acpi_os_printf("\nMiscellaneous Global Handlers:\n");

	for (i = 0; i < ACPI_ARRAY_LENGTH(acpi_gbl_handler_list); i++) {
		acpi_os_printf(ACPI_HANDLER_NAME_STRING,
			       acpi_gbl_handler_list[i].name);

		if (acpi_gbl_handler_list[i].handler) {
			acpi_os_printf(ACPI_HANDLER_PRESENT_STRING, "User",
				       acpi_gbl_handler_list[i].handler);
		} else {
			acpi_os_printf(ACPI_HANDLER_NOT_PRESENT_STRING, "None");
		}
	}

	/* Other handlers that are installed throughout the namespace */

	acpi_os_printf("\nOperation Region Handlers for specific devices:\n");

	(void)acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				  ACPI_UINT32_MAX,
				  acpi_db_display_non_root_handlers, NULL, NULL,
				  NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_non_root_handlers
 *
 * PARAMETERS:  acpi_walk_callback
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display information about all handlers installed for a
 *              device object.
 *
 ******************************************************************************/

static acpi_status
acpi_db_display_non_root_handlers(acpi_handle obj_handle,
				  u32 nesting_level,
				  void *context, void **return_value)
{
	struct acpi_namespace_node *node =
	    ACPI_CAST_PTR(struct acpi_namespace_node, obj_handle);
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *handler_obj;
	char *pathname;

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {
		return (AE_OK);
	}

	pathname = acpi_ns_get_normalized_pathname(node, TRUE);
	if (!pathname) {
		return (AE_OK);
	}

	/* Display all handlers associated with this device */

	handler_obj = obj_desc->common_notify.handler;
	while (handler_obj) {
		acpi_os_printf(ACPI_PREDEFINED_PREFIX,
			       acpi_ut_get_region_name((u8)handler_obj->
						       address_space.space_id),
			       handler_obj->address_space.space_id);

		acpi_os_printf(ACPI_HANDLER_PRESENT_STRING2,
			       (handler_obj->address_space.handler_flags &
				ACPI_ADDR_HANDLER_DEFAULT_INSTALLED) ? "Default"
			       : "User", handler_obj->address_space.handler);

		acpi_os_printf(" Device Name: %s (%p)\n", pathname, node);

		handler_obj = handler_obj->address_space.next;
	}

	ACPI_FREE(pathname);
	return (AE_OK);
}
