/******************************************************************************
 *
 * Module Name: exdump - Interpreter debug output routines
 *
 *****************************************************************************/

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
#include <acpi/acinterp.h>
#include <acpi/amlcode.h>
#include <acpi/acnamesp.h>
#include <acpi/acparser.h>

#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exdump")


/*
 * The following routines are used for debug output only
 */
#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)

/*****************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_operand
 *
 * PARAMETERS:  *obj_desc         - Pointer to entry to be dumped
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump an operand object
 *
 ****************************************************************************/

void
acpi_ex_dump_operand (
	union acpi_operand_object       *obj_desc,
	u32                             depth)
{
	u32                             length;
	u32                             index;


	ACPI_FUNCTION_NAME ("ex_dump_operand")


	if (!((ACPI_LV_EXEC & acpi_dbg_level) && (_COMPONENT & acpi_dbg_layer))) {
		return;
	}

	if (!obj_desc) {
		/*
		 * This could be a null element of a package
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Null Object Descriptor\n"));
		return;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE (obj_desc) == ACPI_DESC_TYPE_NAMED) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "%p is a NS Node: ", obj_desc));
		ACPI_DUMP_ENTRY (obj_desc, ACPI_LV_EXEC);
		return;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE (obj_desc) != ACPI_DESC_TYPE_OPERAND) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
			"%p is not a node or operand object: [%s]\n",
			obj_desc, acpi_ut_get_descriptor_name (obj_desc)));
		ACPI_DUMP_BUFFER (obj_desc, sizeof (union acpi_operand_object));
		return;
	}

	/* obj_desc is a valid object */

	if (depth > 0) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "%*s[%u] %p ",
			depth, " ", depth, obj_desc));
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "%p ", obj_desc));
	}

	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_LOCAL_REFERENCE:

		switch (obj_desc->reference.opcode) {
		case AML_DEBUG_OP:

			acpi_os_printf ("Reference: Debug\n");
			break;


		case AML_NAME_OP:

			ACPI_DUMP_PATHNAME (obj_desc->reference.object,
				"Reference: Name: ", ACPI_LV_INFO, _COMPONENT);
			ACPI_DUMP_ENTRY (obj_desc->reference.object, ACPI_LV_INFO);
			break;


		case AML_INDEX_OP:

			acpi_os_printf ("Reference: Index %p\n",
				obj_desc->reference.object);
			break;


		case AML_REF_OF_OP:

			acpi_os_printf ("Reference: (ref_of) %p\n",
				obj_desc->reference.object);
			break;


		case AML_ARG_OP:

			acpi_os_printf ("Reference: Arg%d",
				obj_desc->reference.offset);

			if (ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_INTEGER) {
				/* Value is an Integer */

				acpi_os_printf (" value is [%8.8X%8.8x]",
					ACPI_FORMAT_UINT64 (obj_desc->integer.value));
			}

			acpi_os_printf ("\n");
			break;


		case AML_LOCAL_OP:

			acpi_os_printf ("Reference: Local%d",
				obj_desc->reference.offset);

			if (ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_INTEGER) {

				/* Value is an Integer */

				acpi_os_printf (" value is [%8.8X%8.8x]",
					ACPI_FORMAT_UINT64 (obj_desc->integer.value));
			}

			acpi_os_printf ("\n");
			break;


		case AML_INT_NAMEPATH_OP:

			acpi_os_printf ("Reference.Node->Name %X\n",
				obj_desc->reference.node->name.integer);
			break;


		default:

			/* Unknown opcode */

			acpi_os_printf ("Unknown Reference opcode=%X\n",
				obj_desc->reference.opcode);
			break;

		}
		break;


	case ACPI_TYPE_BUFFER:

		acpi_os_printf ("Buffer len %X @ %p \n",
			obj_desc->buffer.length, obj_desc->buffer.pointer);

		length = obj_desc->buffer.length;
		if (length > 64) {
			length = 64;
		}

		/* Debug only -- dump the buffer contents */

		if (obj_desc->buffer.pointer) {
			acpi_os_printf ("Buffer Contents: ");

			for (index = 0; index < length; index++) {
				acpi_os_printf (" %02x", obj_desc->buffer.pointer[index]);
			}
			acpi_os_printf ("\n");
		}
		break;


	case ACPI_TYPE_INTEGER:

		acpi_os_printf ("Integer %8.8X%8.8X\n",
			ACPI_FORMAT_UINT64 (obj_desc->integer.value));
		break;


	case ACPI_TYPE_PACKAGE:

		acpi_os_printf ("Package [Len %X] element_array %p\n",
			obj_desc->package.count, obj_desc->package.elements);

		/*
		 * If elements exist, package element pointer is valid,
		 * and debug_level exceeds 1, dump package's elements.
		 */
		if (obj_desc->package.count &&
			obj_desc->package.elements &&
			acpi_dbg_level > 1) {
			for (index = 0; index < obj_desc->package.count; index++) {
				acpi_ex_dump_operand (obj_desc->package.elements[index], depth+1);
			}
		}
		break;


	case ACPI_TYPE_REGION:

		acpi_os_printf ("Region %s (%X)",
			acpi_ut_get_region_name (obj_desc->region.space_id),
			obj_desc->region.space_id);

		/*
		 * If the address and length have not been evaluated,
		 * don't print them.
		 */
		if (!(obj_desc->region.flags & AOPOBJ_DATA_VALID)) {
			acpi_os_printf ("\n");
		}
		else {
			acpi_os_printf (" base %8.8X%8.8X Length %X\n",
				ACPI_FORMAT_UINT64 (obj_desc->region.address),
				obj_desc->region.length);
		}
		break;


	case ACPI_TYPE_STRING:

		acpi_os_printf ("String length %X @ %p ",
			obj_desc->string.length, obj_desc->string.pointer);
		acpi_ut_print_string (obj_desc->string.pointer, ACPI_UINT8_MAX);
		acpi_os_printf ("\n");
		break;


	case ACPI_TYPE_LOCAL_BANK_FIELD:

		acpi_os_printf ("bank_field\n");
		break;


	case ACPI_TYPE_LOCAL_REGION_FIELD:

		acpi_os_printf (
			"region_field: Bits=%X acc_width=%X Lock=%X Update=%X at byte=%X bit=%X of below:\n",
			obj_desc->field.bit_length, obj_desc->field.access_byte_width,
			obj_desc->field.field_flags & AML_FIELD_LOCK_RULE_MASK,
			obj_desc->field.field_flags & AML_FIELD_UPDATE_RULE_MASK,
			obj_desc->field.base_byte_offset, obj_desc->field.start_field_bit_offset);
		acpi_ex_dump_operand (obj_desc->field.region_obj, depth+1);
		break;


	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		acpi_os_printf ("index_field\n");
		break;


	case ACPI_TYPE_BUFFER_FIELD:

		acpi_os_printf (
			"buffer_field: %X bits at byte %X bit %X of \n",
			obj_desc->buffer_field.bit_length, obj_desc->buffer_field.base_byte_offset,
			obj_desc->buffer_field.start_field_bit_offset);

		if (!obj_desc->buffer_field.buffer_obj) {
			ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "*NULL* \n"));
		}
		else if (ACPI_GET_OBJECT_TYPE (obj_desc->buffer_field.buffer_obj) != ACPI_TYPE_BUFFER) {
			acpi_os_printf ("*not a Buffer* \n");
		}
		else {
			acpi_ex_dump_operand (obj_desc->buffer_field.buffer_obj, depth+1);
		}
		break;


	case ACPI_TYPE_EVENT:

		acpi_os_printf ("Event\n");
		break;


	case ACPI_TYPE_METHOD:

		acpi_os_printf (
			"Method(%X) @ %p:%X\n",
			obj_desc->method.param_count,
			obj_desc->method.aml_start, obj_desc->method.aml_length);
		break;


	case ACPI_TYPE_MUTEX:

		acpi_os_printf ("Mutex\n");
		break;


	case ACPI_TYPE_DEVICE:

		acpi_os_printf ("Device\n");
		break;


	case ACPI_TYPE_POWER:

		acpi_os_printf ("Power\n");
		break;


	case ACPI_TYPE_PROCESSOR:

		acpi_os_printf ("Processor\n");
		break;


	case ACPI_TYPE_THERMAL:

		acpi_os_printf ("Thermal\n");
		break;


	default:
		/* Unknown Type */

		acpi_os_printf ("Unknown Type %X\n", ACPI_GET_OBJECT_TYPE (obj_desc));
		break;
	}

	return;
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_operands
 *
 * PARAMETERS:  Operands            - Operand list
 *              interpreter_mode    - Load or Exec
 *              Ident               - Identification
 *              num_levels          - # of stack entries to dump above line
 *              Note                - Output notation
 *              module_name         - Caller's module name
 *              line_number         - Caller's invocation line number
 *
 * DESCRIPTION: Dump the object stack
 *
 ****************************************************************************/

void
acpi_ex_dump_operands (
	union acpi_operand_object       **operands,
	acpi_interpreter_mode           interpreter_mode,
	char                            *ident,
	u32                             num_levels,
	char                            *note,
	char                            *module_name,
	u32                             line_number)
{
	acpi_native_uint                i;


	ACPI_FUNCTION_NAME ("ex_dump_operands");


	if (!ident) {
		ident = "?";
	}

	if (!note) {
		note = "?";
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"************* Operand Stack Contents (Opcode [%s], %d Operands)\n",
		ident, num_levels));

	if (num_levels == 0) {
		num_levels = 1;
	}

	/* Dump the operand stack starting at the top */

	for (i = 0; num_levels > 0; i--, num_levels--) {
		acpi_ex_dump_operand (operands[i], 0);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"************* Stack dump from %s(%d), %s\n",
		module_name, line_number, note));
	return;
}


#ifdef ACPI_FUTURE_USAGE

/*****************************************************************************
 *
 * FUNCTION:    acpi_ex_out*
 *
 * PARAMETERS:  Title               - Descriptive text
 *              Value               - Value to be displayed
 *
 * DESCRIPTION: Object dump output formatting functions.  These functions
 *              reduce the number of format strings required and keeps them
 *              all in one place for easy modification.
 *
 ****************************************************************************/

void
acpi_ex_out_string (
	char                            *title,
	char                            *value)
{
	acpi_os_printf ("%20s : %s\n", title, value);
}

void
acpi_ex_out_pointer (
	char                            *title,
	void                            *value)
{
	acpi_os_printf ("%20s : %p\n", title, value);
}

void
acpi_ex_out_integer (
	char                            *title,
	u32                             value)
{
	acpi_os_printf ("%20s : %X\n", title, value);
}

void
acpi_ex_out_address (
	char                            *title,
	acpi_physical_address           value)
{

#if ACPI_MACHINE_WIDTH == 16
	acpi_os_printf ("%20s : %p\n", title, value);
#else
	acpi_os_printf ("%20s : %8.8X%8.8X\n", title, ACPI_FORMAT_UINT64 (value));
#endif
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_node
 *
 * PARAMETERS:  *Node               - Descriptor to dump
 *              Flags               - Force display
 *
 * DESCRIPTION: Dumps the members of the given.Node
 *
 ****************************************************************************/

void
acpi_ex_dump_node (
	struct acpi_namespace_node      *node,
	u32                             flags)
{

	ACPI_FUNCTION_ENTRY ();


	if (!flags) {
		if (!((ACPI_LV_OBJECTS & acpi_dbg_level) && (_COMPONENT & acpi_dbg_layer))) {
			return;
		}
	}

	acpi_os_printf ("%20s : %4.4s\n",     "Name", acpi_ut_get_node_name (node));
	acpi_ex_out_string ("Type",           acpi_ut_get_type_name (node->type));
	acpi_ex_out_integer ("Flags",         node->flags);
	acpi_ex_out_integer ("Owner Id",      node->owner_id);
	acpi_ex_out_integer ("Reference Count", node->reference_count);
	acpi_ex_out_pointer ("Attached Object", acpi_ns_get_attached_object (node));
	acpi_ex_out_pointer ("child_list",    node->child);
	acpi_ex_out_pointer ("next_peer",     node->peer);
	acpi_ex_out_pointer ("Parent",        acpi_ns_get_parent_node (node));
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ex_dump_object_descriptor
 *
 * PARAMETERS:  *Object             - Descriptor to dump
 *              Flags               - Force display
 *
 * DESCRIPTION: Dumps the members of the object descriptor given.
 *
 ****************************************************************************/

void
acpi_ex_dump_object_descriptor (
	union acpi_operand_object       *obj_desc,
	u32                             flags)
{
	u32                             i;


	ACPI_FUNCTION_TRACE ("ex_dump_object_descriptor");


	if (!flags) {
		if (!((ACPI_LV_OBJECTS & acpi_dbg_level) && (_COMPONENT & acpi_dbg_layer))) {
			return_VOID;
		}
	}

	if (ACPI_GET_DESCRIPTOR_TYPE (obj_desc) == ACPI_DESC_TYPE_NAMED) {
		acpi_ex_dump_node ((struct acpi_namespace_node *) obj_desc, flags);
		acpi_os_printf ("\nAttached Object (%p):\n",
			((struct acpi_namespace_node *) obj_desc)->object);
		acpi_ex_dump_object_descriptor (
			((struct acpi_namespace_node *) obj_desc)->object, flags);
		return_VOID;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE (obj_desc) != ACPI_DESC_TYPE_OPERAND) {
		acpi_os_printf (
			"ex_dump_object_descriptor: %p is not an ACPI operand object: [%s]\n",
			obj_desc, acpi_ut_get_descriptor_name (obj_desc));
		return_VOID;
	}

	/* Common Fields */

	acpi_ex_out_string ("Type",             acpi_ut_get_object_type_name (obj_desc));
	acpi_ex_out_integer ("Reference Count", obj_desc->common.reference_count);
	acpi_ex_out_integer ("Flags",           obj_desc->common.flags);

	/* Object-specific Fields */

	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_INTEGER:

		acpi_os_printf ("%20s : %8.8X%8.8X\n", "Value",
				ACPI_FORMAT_UINT64 (obj_desc->integer.value));
		break;


	case ACPI_TYPE_STRING:

		acpi_ex_out_integer ("Length",      obj_desc->string.length);

		acpi_os_printf ("%20s : %p ", "Pointer", obj_desc->string.pointer);
		acpi_ut_print_string (obj_desc->string.pointer, ACPI_UINT8_MAX);
		acpi_os_printf ("\n");
		break;


	case ACPI_TYPE_BUFFER:

		acpi_ex_out_integer ("Length",      obj_desc->buffer.length);
		acpi_ex_out_pointer ("Pointer",     obj_desc->buffer.pointer);
		ACPI_DUMP_BUFFER (obj_desc->buffer.pointer, obj_desc->buffer.length);
		break;


	case ACPI_TYPE_PACKAGE:

		acpi_ex_out_integer ("Flags",       obj_desc->package.flags);
		acpi_ex_out_integer ("Count",       obj_desc->package.count);
		acpi_ex_out_pointer ("Elements",    obj_desc->package.elements);

		/* Dump the package contents */

		if (obj_desc->package.count > 0) {
			acpi_os_printf ("\nPackage Contents:\n");
			for (i = 0; i < obj_desc->package.count; i++) {
				acpi_os_printf ("[%.3d] %p", i, obj_desc->package.elements[i]);
				if (obj_desc->package.elements[i]) {
					acpi_os_printf (" %s",
						acpi_ut_get_object_type_name (obj_desc->package.elements[i]));
				}
				acpi_os_printf ("\n");
			}
		}
		break;


	case ACPI_TYPE_DEVICE:

		acpi_ex_out_pointer ("Handler",     obj_desc->device.handler);
		acpi_ex_out_pointer ("system_notify", obj_desc->device.system_notify);
		acpi_ex_out_pointer ("device_notify", obj_desc->device.device_notify);
		break;


	case ACPI_TYPE_EVENT:

		acpi_ex_out_pointer ("Semaphore",   obj_desc->event.semaphore);
		break;


	case ACPI_TYPE_METHOD:

		acpi_ex_out_integer ("param_count", obj_desc->method.param_count);
		acpi_ex_out_integer ("Concurrency", obj_desc->method.concurrency);
		acpi_ex_out_pointer ("Semaphore",   obj_desc->method.semaphore);
		acpi_ex_out_integer ("owning_id",   obj_desc->method.owning_id);
		acpi_ex_out_integer ("aml_length",  obj_desc->method.aml_length);
		acpi_ex_out_pointer ("aml_start",   obj_desc->method.aml_start);
		break;


	case ACPI_TYPE_MUTEX:

		acpi_ex_out_integer ("sync_level",  obj_desc->mutex.sync_level);
		acpi_ex_out_pointer ("owner_thread", obj_desc->mutex.owner_thread);
		acpi_ex_out_integer ("acquire_depth", obj_desc->mutex.acquisition_depth);
		acpi_ex_out_pointer ("Semaphore",   obj_desc->mutex.semaphore);
		break;


	case ACPI_TYPE_REGION:

		acpi_ex_out_integer ("space_id",    obj_desc->region.space_id);
		acpi_ex_out_integer ("Flags",       obj_desc->region.flags);
		acpi_ex_out_address ("Address",     obj_desc->region.address);
		acpi_ex_out_integer ("Length",      obj_desc->region.length);
		acpi_ex_out_pointer ("Handler",     obj_desc->region.handler);
		acpi_ex_out_pointer ("Next",        obj_desc->region.next);
		break;


	case ACPI_TYPE_POWER:

		acpi_ex_out_integer ("system_level", obj_desc->power_resource.system_level);
		acpi_ex_out_integer ("resource_order", obj_desc->power_resource.resource_order);
		acpi_ex_out_pointer ("system_notify", obj_desc->power_resource.system_notify);
		acpi_ex_out_pointer ("device_notify", obj_desc->power_resource.device_notify);
		break;


	case ACPI_TYPE_PROCESSOR:

		acpi_ex_out_integer ("Processor ID", obj_desc->processor.proc_id);
		acpi_ex_out_integer ("Length",      obj_desc->processor.length);
		acpi_ex_out_address ("Address",     (acpi_physical_address) obj_desc->processor.address);
		acpi_ex_out_pointer ("system_notify", obj_desc->processor.system_notify);
		acpi_ex_out_pointer ("device_notify", obj_desc->processor.device_notify);
		acpi_ex_out_pointer ("Handler",     obj_desc->processor.handler);
		break;


	case ACPI_TYPE_THERMAL:

		acpi_ex_out_pointer ("system_notify", obj_desc->thermal_zone.system_notify);
		acpi_ex_out_pointer ("device_notify", obj_desc->thermal_zone.device_notify);
		acpi_ex_out_pointer ("Handler",     obj_desc->thermal_zone.handler);
		break;


	case ACPI_TYPE_BUFFER_FIELD:
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		acpi_ex_out_integer ("field_flags", obj_desc->common_field.field_flags);
		acpi_ex_out_integer ("access_byte_width",obj_desc->common_field.access_byte_width);
		acpi_ex_out_integer ("bit_length",  obj_desc->common_field.bit_length);
		acpi_ex_out_integer ("fld_bit_offset", obj_desc->common_field.start_field_bit_offset);
		acpi_ex_out_integer ("base_byte_offset", obj_desc->common_field.base_byte_offset);
		acpi_ex_out_pointer ("parent_node", obj_desc->common_field.node);

		switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
		case ACPI_TYPE_BUFFER_FIELD:
			acpi_ex_out_pointer ("buffer_obj", obj_desc->buffer_field.buffer_obj);
			break;

		case ACPI_TYPE_LOCAL_REGION_FIELD:
			acpi_ex_out_pointer ("region_obj", obj_desc->field.region_obj);
			break;

		case ACPI_TYPE_LOCAL_BANK_FIELD:
			acpi_ex_out_integer ("Value",   obj_desc->bank_field.value);
			acpi_ex_out_pointer ("region_obj", obj_desc->bank_field.region_obj);
			acpi_ex_out_pointer ("bank_obj", obj_desc->bank_field.bank_obj);
			break;

		case ACPI_TYPE_LOCAL_INDEX_FIELD:
			acpi_ex_out_integer ("Value",   obj_desc->index_field.value);
			acpi_ex_out_pointer ("Index",   obj_desc->index_field.index_obj);
			acpi_ex_out_pointer ("Data",    obj_desc->index_field.data_obj);
			break;

		default:
			/* All object types covered above */
			break;
		}
		break;


	case ACPI_TYPE_LOCAL_REFERENCE:

		acpi_ex_out_integer ("target_type", obj_desc->reference.target_type);
		acpi_ex_out_string ("Opcode",       (acpi_ps_get_opcode_info (obj_desc->reference.opcode))->name);
		acpi_ex_out_integer ("Offset",      obj_desc->reference.offset);
		acpi_ex_out_pointer ("obj_desc",    obj_desc->reference.object);
		acpi_ex_out_pointer ("Node",        obj_desc->reference.node);
		acpi_ex_out_pointer ("Where",       obj_desc->reference.where);
		break;


	case ACPI_TYPE_LOCAL_ADDRESS_HANDLER:

		acpi_ex_out_integer ("space_id",    obj_desc->address_space.space_id);
		acpi_ex_out_pointer ("Next",        obj_desc->address_space.next);
		acpi_ex_out_pointer ("region_list", obj_desc->address_space.region_list);
		acpi_ex_out_pointer ("Node",        obj_desc->address_space.node);
		acpi_ex_out_pointer ("Context",     obj_desc->address_space.context);
		break;


	case ACPI_TYPE_LOCAL_NOTIFY:

		acpi_ex_out_pointer ("Node",        obj_desc->notify.node);
		acpi_ex_out_pointer ("Context",     obj_desc->notify.context);
		break;


	case ACPI_TYPE_LOCAL_ALIAS:
	case ACPI_TYPE_LOCAL_METHOD_ALIAS:
	case ACPI_TYPE_LOCAL_EXTRA:
	case ACPI_TYPE_LOCAL_DATA:
	default:

		acpi_os_printf (
			"ex_dump_object_descriptor: Display not implemented for object type %s\n",
			acpi_ut_get_object_type_name (obj_desc));
		break;
	}

	return_VOID;
}

#endif  /*  ACPI_FUTURE_USAGE  */

#endif

