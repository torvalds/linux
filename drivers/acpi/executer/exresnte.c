
/******************************************************************************
 *
 * Module Name: exresnte - AML Interpreter object resolution
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
#include <acpi/acdispat.h>
#include <acpi/acinterp.h>
#include <acpi/acnamesp.h>
#include <acpi/acparser.h>
#include <acpi/amlcode.h>

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exresnte")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_resolve_node_to_value
 *
 * PARAMETERS:  object_ptr      - Pointer to a location that contains
 *                                a pointer to a NS node, and will receive a
 *                                pointer to the resolved object.
 *              walk_state      - Current state.  Valid only if executing AML
 *                                code.  NULL if simply resolving an object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Resolve a Namespace node to a valued object
 *
 * Note: for some of the data types, the pointer attached to the Node
 * can be either a pointer to an actual internal object or a pointer into the
 * AML stream itself.  These types are currently:
 *
 *      ACPI_TYPE_INTEGER
 *      ACPI_TYPE_STRING
 *      ACPI_TYPE_BUFFER
 *      ACPI_TYPE_MUTEX
 *      ACPI_TYPE_PACKAGE
 *
 ******************************************************************************/
acpi_status
acpi_ex_resolve_node_to_value(struct acpi_namespace_node **object_ptr,
			      struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *source_desc;
	union acpi_operand_object *obj_desc = NULL;
	struct acpi_namespace_node *node;
	acpi_object_type entry_type;

	ACPI_FUNCTION_TRACE("ex_resolve_node_to_value");

	/*
	 * The stack pointer points to a struct acpi_namespace_node (Node).  Get the
	 * object that is attached to the Node.
	 */
	node = *object_ptr;
	source_desc = acpi_ns_get_attached_object(node);
	entry_type = acpi_ns_get_type((acpi_handle) node);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Entry=%p source_desc=%p [%s]\n",
			  node, source_desc,
			  acpi_ut_get_type_name(entry_type)));

	if ((entry_type == ACPI_TYPE_LOCAL_ALIAS) ||
	    (entry_type == ACPI_TYPE_LOCAL_METHOD_ALIAS)) {
		/* There is always exactly one level of indirection */

		node = ACPI_CAST_PTR(struct acpi_namespace_node, node->object);
		source_desc = acpi_ns_get_attached_object(node);
		entry_type = acpi_ns_get_type((acpi_handle) node);
		*object_ptr = node;
	}

	/*
	 * Several object types require no further processing:
	 * 1) Devices rarely have an attached object, return the Node
	 * 2) Method locals and arguments have a pseudo-Node
	 */
	if (entry_type == ACPI_TYPE_DEVICE ||
	    (node->flags & (ANOBJ_METHOD_ARG | ANOBJ_METHOD_LOCAL))) {
		return_ACPI_STATUS(AE_OK);
	}

	if (!source_desc) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "No object attached to node %p\n", node));
		return_ACPI_STATUS(AE_AML_NO_OPERAND);
	}

	/*
	 * Action is based on the type of the Node, which indicates the type
	 * of the attached object or pointer
	 */
	switch (entry_type) {
	case ACPI_TYPE_PACKAGE:

		if (ACPI_GET_OBJECT_TYPE(source_desc) != ACPI_TYPE_PACKAGE) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Object not a Package, type %s\n",
					  acpi_ut_get_object_type_name
					  (source_desc)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		status = acpi_ds_get_package_arguments(source_desc);
		if (ACPI_SUCCESS(status)) {
			/* Return an additional reference to the object */

			obj_desc = source_desc;
			acpi_ut_add_reference(obj_desc);
		}
		break;

	case ACPI_TYPE_BUFFER:

		if (ACPI_GET_OBJECT_TYPE(source_desc) != ACPI_TYPE_BUFFER) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Object not a Buffer, type %s\n",
					  acpi_ut_get_object_type_name
					  (source_desc)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		status = acpi_ds_get_buffer_arguments(source_desc);
		if (ACPI_SUCCESS(status)) {
			/* Return an additional reference to the object */

			obj_desc = source_desc;
			acpi_ut_add_reference(obj_desc);
		}
		break;

	case ACPI_TYPE_STRING:

		if (ACPI_GET_OBJECT_TYPE(source_desc) != ACPI_TYPE_STRING) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Object not a String, type %s\n",
					  acpi_ut_get_object_type_name
					  (source_desc)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/* Return an additional reference to the object */

		obj_desc = source_desc;
		acpi_ut_add_reference(obj_desc);
		break;

	case ACPI_TYPE_INTEGER:

		if (ACPI_GET_OBJECT_TYPE(source_desc) != ACPI_TYPE_INTEGER) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Object not a Integer, type %s\n",
					  acpi_ut_get_object_type_name
					  (source_desc)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/* Return an additional reference to the object */

		obj_desc = source_desc;
		acpi_ut_add_reference(obj_desc);
		break;

	case ACPI_TYPE_BUFFER_FIELD:
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "field_read Node=%p source_desc=%p Type=%X\n",
				  node, source_desc, entry_type));

		status =
		    acpi_ex_read_data_from_field(walk_state, source_desc,
						 &obj_desc);
		break;

		/* For these objects, just return the object attached to the Node */

	case ACPI_TYPE_MUTEX:
	case ACPI_TYPE_METHOD:
	case ACPI_TYPE_POWER:
	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_THERMAL:
	case ACPI_TYPE_EVENT:
	case ACPI_TYPE_REGION:

		/* Return an additional reference to the object */

		obj_desc = source_desc;
		acpi_ut_add_reference(obj_desc);
		break;

		/* TYPE_ANY is untyped, and thus there is no object associated with it */

	case ACPI_TYPE_ANY:

		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Untyped entry %p, no attached object!\n",
				  node));

		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);	/* Cannot be AE_TYPE */

	case ACPI_TYPE_LOCAL_REFERENCE:

		switch (source_desc->reference.opcode) {
		case AML_LOAD_OP:

			/* This is a ddb_handle */
			/* Return an additional reference to the object */

			obj_desc = source_desc;
			acpi_ut_add_reference(obj_desc);
			break;

		default:
			/* No named references are allowed here */

			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Unsupported Reference opcode %X (%s)\n",
					  source_desc->reference.opcode,
					  acpi_ps_get_opcode_name(source_desc->
								  reference.
								  opcode)));

			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}
		break;

	default:

		/* Default case is for unknown types */

		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Node %p - Unknown object type %X\n",
				  node, entry_type));

		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);

	}			/* switch (entry_type) */

	/* Return the object descriptor */

	*object_ptr = (void *)obj_desc;
	return_ACPI_STATUS(status);
}
