// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: dswload2 - Dispatcher second pass namespace load callbacks
 *
 * Copyright (C) 2000 - 2022, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acparser.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "acevents.h"
#ifdef ACPI_EXEC_APP
#include "aecommon.h"
#endif

#define _COMPONENT          ACPI_DISPATCHER
ACPI_MODULE_NAME("dswload2")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_load2_begin_op
 *
 * PARAMETERS:  walk_state      - Current state of the parse tree walk
 *              out_op          - Where to return op if a new one is created
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback used during the loading of ACPI tables.
 *
 ******************************************************************************/
acpi_status
acpi_ds_load2_begin_op(struct acpi_walk_state *walk_state,
		       union acpi_parse_object **out_op)
{
	union acpi_parse_object *op;
	struct acpi_namespace_node *node;
	acpi_status status;
	acpi_object_type object_type;
	char *buffer_ptr;
	u32 flags;

	ACPI_FUNCTION_TRACE(ds_load2_begin_op);

	op = walk_state->op;
	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op,
			  walk_state));

	if (op) {
		if ((walk_state->control_state) &&
		    (walk_state->control_state->common.state ==
		     ACPI_CONTROL_CONDITIONAL_EXECUTING)) {

			/* We are executing a while loop outside of a method */

			status = acpi_ds_exec_begin_op(walk_state, out_op);
			return_ACPI_STATUS(status);
		}

		/* We only care about Namespace opcodes here */

		if ((!(walk_state->op_info->flags & AML_NSOPCODE) &&
		     (walk_state->opcode != AML_INT_NAMEPATH_OP)) ||
		    (!(walk_state->op_info->flags & AML_NAMED))) {
			return_ACPI_STATUS(AE_OK);
		}

		/* Get the name we are going to enter or lookup in the namespace */

		if (walk_state->opcode == AML_INT_NAMEPATH_OP) {

			/* For Namepath op, get the path string */

			buffer_ptr = op->common.value.string;
			if (!buffer_ptr) {

				/* No name, just exit */

				return_ACPI_STATUS(AE_OK);
			}
		} else {
			/* Get name from the op */

			buffer_ptr = ACPI_CAST_PTR(char, &op->named.name);
		}
	} else {
		/* Get the namestring from the raw AML */

		buffer_ptr =
		    acpi_ps_get_next_namestring(&walk_state->parser_state);
	}

	/* Map the opcode into an internal object type */

	object_type = walk_state->op_info->object_type;

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "State=%p Op=%p Type=%X\n", walk_state, op,
			  object_type));

	switch (walk_state->opcode) {
	case AML_FIELD_OP:
	case AML_BANK_FIELD_OP:
	case AML_INDEX_FIELD_OP:

		node = NULL;
		status = AE_OK;
		break;

	case AML_INT_NAMEPATH_OP:
		/*
		 * The name_path is an object reference to an existing object.
		 * Don't enter the name into the namespace, but look it up
		 * for use later.
		 */
		status =
		    acpi_ns_lookup(walk_state->scope_info, buffer_ptr,
				   object_type, ACPI_IMODE_EXECUTE,
				   ACPI_NS_SEARCH_PARENT, walk_state, &(node));
		break;

	case AML_SCOPE_OP:

		/* Special case for Scope(\) -> refers to the Root node */

		if (op && (op->named.node == acpi_gbl_root_node)) {
			node = op->named.node;

			status =
			    acpi_ds_scope_stack_push(node, object_type,
						     walk_state);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		} else {
			/*
			 * The Path is an object reference to an existing object.
			 * Don't enter the name into the namespace, but look it up
			 * for use later.
			 */
			status =
			    acpi_ns_lookup(walk_state->scope_info, buffer_ptr,
					   object_type, ACPI_IMODE_EXECUTE,
					   ACPI_NS_SEARCH_PARENT, walk_state,
					   &(node));
			if (ACPI_FAILURE(status)) {
#ifdef ACPI_ASL_COMPILER
				if (status == AE_NOT_FOUND) {
					status = AE_OK;
				} else {
					ACPI_ERROR_NAMESPACE(walk_state->
							     scope_info,
							     buffer_ptr,
							     status);
				}
#else
				ACPI_ERROR_NAMESPACE(walk_state->scope_info,
						     buffer_ptr, status);
#endif
				return_ACPI_STATUS(status);
			}
		}

		/*
		 * We must check to make sure that the target is
		 * one of the opcodes that actually opens a scope
		 */
		switch (node->type) {
		case ACPI_TYPE_ANY:
		case ACPI_TYPE_LOCAL_SCOPE:	/* Scope */
		case ACPI_TYPE_DEVICE:
		case ACPI_TYPE_POWER:
		case ACPI_TYPE_PROCESSOR:
		case ACPI_TYPE_THERMAL:

			/* These are acceptable types */
			break;

		case ACPI_TYPE_INTEGER:
		case ACPI_TYPE_STRING:
		case ACPI_TYPE_BUFFER:

			/*
			 * These types we will allow, but we will change the type.
			 * This enables some existing code of the form:
			 *
			 *  Name (DEB, 0)
			 *  Scope (DEB) { ... }
			 */
			ACPI_WARNING((AE_INFO,
				      "Type override - [%4.4s] had invalid type (%s) "
				      "for Scope operator, changed to type ANY",
				      acpi_ut_get_node_name(node),
				      acpi_ut_get_type_name(node->type)));

			node->type = ACPI_TYPE_ANY;
			walk_state->scope_info->common.value = ACPI_TYPE_ANY;
			break;

		case ACPI_TYPE_METHOD:

			/*
			 * Allow scope change to root during execution of module-level
			 * code. Root is typed METHOD during this time.
			 */
			if ((node == acpi_gbl_root_node) &&
			    (walk_state->
			     parse_flags & ACPI_PARSE_MODULE_LEVEL)) {
				break;
			}

			ACPI_FALLTHROUGH;

		default:

			/* All other types are an error */

			ACPI_ERROR((AE_INFO,
				    "Invalid type (%s) for target of "
				    "Scope operator [%4.4s] (Cannot override)",
				    acpi_ut_get_type_name(node->type),
				    acpi_ut_get_node_name(node)));

			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}
		break;

	default:

		/* All other opcodes */

		if (op && op->common.node) {

			/* This op/node was previously entered into the namespace */

			node = op->common.node;

			if (acpi_ns_opens_scope(object_type)) {
				status =
				    acpi_ds_scope_stack_push(node, object_type,
							     walk_state);
				if (ACPI_FAILURE(status)) {
					return_ACPI_STATUS(status);
				}
			}

			return_ACPI_STATUS(AE_OK);
		}

		/*
		 * Enter the named type into the internal namespace. We enter the name
		 * as we go downward in the parse tree. Any necessary subobjects that
		 * involve arguments to the opcode must be created as we go back up the
		 * parse tree later.
		 *
		 * Note: Name may already exist if we are executing a deferred opcode.
		 */
		if (walk_state->deferred_node) {

			/* This name is already in the namespace, get the node */

			node = walk_state->deferred_node;
			status = AE_OK;
			break;
		}

		flags = ACPI_NS_NO_UPSEARCH;
		if (walk_state->pass_number == ACPI_IMODE_EXECUTE) {

			/* Execution mode, node cannot already exist, node is temporary */

			flags |= ACPI_NS_ERROR_IF_FOUND;

			if (!
			    (walk_state->
			     parse_flags & ACPI_PARSE_MODULE_LEVEL)) {
				flags |= ACPI_NS_TEMPORARY;
			}
		}
#ifdef ACPI_ASL_COMPILER

		/*
		 * Do not open a scope for AML_EXTERNAL_OP
		 * acpi_ns_lookup can open a new scope based on the object type
		 * of this op. AML_EXTERNAL_OP is a declaration rather than a
		 * definition. In the case that this external is a method object,
		 * acpi_ns_lookup will open a new scope. However, an AML_EXTERNAL_OP
		 * associated with the ACPI_TYPE_METHOD is a declaration, rather than
		 * a definition. Flags is set to avoid opening a scope for any
		 * AML_EXTERNAL_OP.
		 */
		if (walk_state->opcode == AML_EXTERNAL_OP) {
			flags |= ACPI_NS_DONT_OPEN_SCOPE;
		}
#endif

		/*
		 * For name creation opcodes, the full namepath prefix must
		 * exist, except for the final (new) nameseg.
		 */
		if (walk_state->op_info->flags & AML_NAMED) {
			flags |= ACPI_NS_PREFIX_MUST_EXIST;
		}

		/* Add new entry or lookup existing entry */

		status =
		    acpi_ns_lookup(walk_state->scope_info, buffer_ptr,
				   object_type, ACPI_IMODE_LOAD_PASS2, flags,
				   walk_state, &node);

		if (ACPI_SUCCESS(status) && (flags & ACPI_NS_TEMPORARY)) {
			ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
					  "***New Node [%4.4s] %p is temporary\n",
					  acpi_ut_get_node_name(node), node));
		}
		break;
	}

	if (ACPI_FAILURE(status)) {
		ACPI_ERROR_NAMESPACE(walk_state->scope_info,
				     buffer_ptr, status);
		return_ACPI_STATUS(status);
	}

	if (!op) {

		/* Create a new op */

		op = acpi_ps_alloc_op(walk_state->opcode, walk_state->aml);
		if (!op) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Initialize the new op */

		if (node) {
			op->named.name = node->name.integer;
		}
		*out_op = op;
	}

	/*
	 * Put the Node in the "op" object that the parser uses, so we
	 * can get it again quickly when this scope is closed
	 */
	op->common.node = node;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_load2_end_op
 *
 * PARAMETERS:  walk_state      - Current state of the parse tree walk
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback used during the loading of the namespace,
 *              both control methods and everything else.
 *
 ******************************************************************************/

acpi_status acpi_ds_load2_end_op(struct acpi_walk_state *walk_state)
{
	union acpi_parse_object *op;
	acpi_status status = AE_OK;
	acpi_object_type object_type;
	struct acpi_namespace_node *node;
	union acpi_parse_object *arg;
	struct acpi_namespace_node *new_node;
	u32 i;
	u8 region_space;
#ifdef ACPI_EXEC_APP
	union acpi_operand_object *obj_desc;
	char *namepath;
#endif

	ACPI_FUNCTION_TRACE(ds_load2_end_op);

	op = walk_state->op;
	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH, "Opcode [%s] Op %p State %p\n",
			  walk_state->op_info->name, op, walk_state));

	/* Check if opcode had an associated namespace object */

	if (!(walk_state->op_info->flags & AML_NSOBJECT)) {
		return_ACPI_STATUS(AE_OK);
	}

	if (op->common.aml_opcode == AML_SCOPE_OP) {
		ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
				  "Ending scope Op=%p State=%p\n", op,
				  walk_state));
	}

	object_type = walk_state->op_info->object_type;

	/*
	 * Get the Node/name from the earlier lookup
	 * (It was saved in the *op structure)
	 */
	node = op->common.node;

	/*
	 * Put the Node on the object stack (Contains the ACPI Name of
	 * this object)
	 */
	walk_state->operands[0] = (void *)node;
	walk_state->num_operands = 1;

	/* Pop the scope stack */

	if (acpi_ns_opens_scope(object_type) &&
	    (op->common.aml_opcode != AML_INT_METHODCALL_OP)) {
		ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
				  "(%s) Popping scope for Op %p\n",
				  acpi_ut_get_type_name(object_type), op));

		status = acpi_ds_scope_stack_pop(walk_state);
		if (ACPI_FAILURE(status)) {
			goto cleanup;
		}
	}

	/*
	 * Named operations are as follows:
	 *
	 * AML_ALIAS
	 * AML_BANKFIELD
	 * AML_CREATEBITFIELD
	 * AML_CREATEBYTEFIELD
	 * AML_CREATEDWORDFIELD
	 * AML_CREATEFIELD
	 * AML_CREATEQWORDFIELD
	 * AML_CREATEWORDFIELD
	 * AML_DATA_REGION
	 * AML_DEVICE
	 * AML_EVENT
	 * AML_FIELD
	 * AML_INDEXFIELD
	 * AML_METHOD
	 * AML_METHODCALL
	 * AML_MUTEX
	 * AML_NAME
	 * AML_NAMEDFIELD
	 * AML_OPREGION
	 * AML_POWERRES
	 * AML_PROCESSOR
	 * AML_SCOPE
	 * AML_THERMALZONE
	 */

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "Create-Load [%s] State=%p Op=%p NamedObj=%p\n",
			  acpi_ps_get_opcode_name(op->common.aml_opcode),
			  walk_state, op, node));

	/* Decode the opcode */

	arg = op->common.value.arg;

	switch (walk_state->op_info->type) {

	case AML_TYPE_CREATE_FIELD:
		/*
		 * Create the field object, but the field buffer and index must
		 * be evaluated later during the execution phase
		 */
		status = acpi_ds_create_buffer_field(op, walk_state);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"CreateBufferField failure"));
			goto cleanup;
			}
		break;

	case AML_TYPE_NAMED_FIELD:
		/*
		 * If we are executing a method, initialize the field
		 */
		if (walk_state->method_node) {
			status = acpi_ds_init_field_objects(op, walk_state);
		}

		switch (op->common.aml_opcode) {
		case AML_INDEX_FIELD_OP:

			status =
			    acpi_ds_create_index_field(op,
						       (acpi_handle)arg->common.
						       node, walk_state);
			break;

		case AML_BANK_FIELD_OP:

			status =
			    acpi_ds_create_bank_field(op, arg->common.node,
						      walk_state);
			break;

		case AML_FIELD_OP:

			status =
			    acpi_ds_create_field(op, arg->common.node,
						 walk_state);
			break;

		default:

			/* All NAMED_FIELD opcodes must be handled above */
			break;
		}
		break;

	case AML_TYPE_NAMED_SIMPLE:

		status = acpi_ds_create_operands(walk_state, arg);
		if (ACPI_FAILURE(status)) {
			goto cleanup;
		}

		switch (op->common.aml_opcode) {
		case AML_PROCESSOR_OP:

			status = acpi_ex_create_processor(walk_state);
			break;

		case AML_POWER_RESOURCE_OP:

			status = acpi_ex_create_power_resource(walk_state);
			break;

		case AML_MUTEX_OP:

			status = acpi_ex_create_mutex(walk_state);
			break;

		case AML_EVENT_OP:

			status = acpi_ex_create_event(walk_state);
			break;

		case AML_ALIAS_OP:

			status = acpi_ex_create_alias(walk_state);
			break;

		default:

			/* Unknown opcode */

			status = AE_OK;
			goto cleanup;
		}

		/* Delete operands */

		for (i = 1; i < walk_state->num_operands; i++) {
			acpi_ut_remove_reference(walk_state->operands[i]);
			walk_state->operands[i] = NULL;
		}

		break;

	case AML_TYPE_NAMED_COMPLEX:

		switch (op->common.aml_opcode) {
		case AML_REGION_OP:
		case AML_DATA_REGION_OP:

			if (op->common.aml_opcode == AML_REGION_OP) {
				region_space = (acpi_adr_space_type)
				    ((op->common.value.arg)->common.value.
				     integer);
			} else {
				region_space = ACPI_ADR_SPACE_DATA_TABLE;
			}

			/*
			 * The op_region is not fully parsed at this time. The only valid
			 * argument is the space_id. (We must save the address of the
			 * AML of the address and length operands)
			 *
			 * If we have a valid region, initialize it. The namespace is
			 * unlocked at this point.
			 *
			 * Need to unlock interpreter if it is locked (if we are running
			 * a control method), in order to allow _REG methods to be run
			 * during acpi_ev_initialize_region.
			 */
			if (walk_state->method_node) {
				/*
				 * Executing a method: initialize the region and unlock
				 * the interpreter
				 */
				status = acpi_ex_create_region(op->named.data,
							       op->named.length,
							       region_space,
							       walk_state);
				if (ACPI_FAILURE(status)) {
					return_ACPI_STATUS(status);
				}
			}

			status =
			    acpi_ev_initialize_region
			    (acpi_ns_get_attached_object(node));
			break;

		case AML_NAME_OP:

			status = acpi_ds_create_node(walk_state, node, op);
			if (ACPI_FAILURE(status)) {
				goto cleanup;
			}
#ifdef ACPI_EXEC_APP
			/*
			 * acpi_exec support for namespace initialization file (initialize
			 * Name opcodes in this code.)
			 */
			namepath = acpi_ns_get_external_pathname(node);
			status = ae_lookup_init_file_entry(namepath, &obj_desc);
			if (ACPI_SUCCESS(status)) {

				/* Detach any existing object, attach new object */

				if (node->object) {
					acpi_ns_detach_object(node);
				}
				acpi_ns_attach_object(node, obj_desc,
						      obj_desc->common.type);
			}
			ACPI_FREE(namepath);
			status = AE_OK;
#endif
			break;

		case AML_METHOD_OP:
			/*
			 * method_op pkg_length name_string method_flags term_list
			 *
			 * Note: We must create the method node/object pair as soon as we
			 * see the method declaration. This allows later pass1 parsing
			 * of invocations of the method (need to know the number of
			 * arguments.)
			 */
			ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
					  "LOADING-Method: State=%p Op=%p NamedObj=%p\n",
					  walk_state, op, op->named.node));

			if (!acpi_ns_get_attached_object(op->named.node)) {
				walk_state->operands[0] =
				    ACPI_CAST_PTR(void, op->named.node);
				walk_state->num_operands = 1;

				status =
				    acpi_ds_create_operands(walk_state,
							    op->common.value.
							    arg);
				if (ACPI_SUCCESS(status)) {
					status =
					    acpi_ex_create_method(op->named.
								  data,
								  op->named.
								  length,
								  walk_state);
				}

				walk_state->operands[0] = NULL;
				walk_state->num_operands = 0;

				if (ACPI_FAILURE(status)) {
					return_ACPI_STATUS(status);
				}
			}
			break;

		default:

			/* All NAMED_COMPLEX opcodes must be handled above */
			break;
		}
		break;

	case AML_CLASS_INTERNAL:

		/* case AML_INT_NAMEPATH_OP: */
		break;

	case AML_CLASS_METHOD_CALL:

		ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
				  "RESOLVING-MethodCall: State=%p Op=%p NamedObj=%p\n",
				  walk_state, op, node));

		/*
		 * Lookup the method name and save the Node
		 */
		status =
		    acpi_ns_lookup(walk_state->scope_info,
				   arg->common.value.string, ACPI_TYPE_ANY,
				   ACPI_IMODE_LOAD_PASS2,
				   ACPI_NS_SEARCH_PARENT |
				   ACPI_NS_DONT_OPEN_SCOPE, walk_state,
				   &(new_node));
		if (ACPI_SUCCESS(status)) {
			/*
			 * Make sure that what we found is indeed a method
			 * We didn't search for a method on purpose, to see if the name
			 * would resolve
			 */
			if (new_node->type != ACPI_TYPE_METHOD) {
				status = AE_AML_OPERAND_TYPE;
			}

			/* We could put the returned object (Node) on the object stack for
			 * later, but for now, we will put it in the "op" object that the
			 * parser uses, so we can get it again at the end of this scope
			 */
			op->common.node = new_node;
		} else {
			ACPI_ERROR_NAMESPACE(walk_state->scope_info,
					     arg->common.value.string, status);
		}
		break;

	default:

		break;
	}

cleanup:

	/* Remove the Node pushed at the very beginning */

	walk_state->operands[0] = NULL;
	walk_state->num_operands = 0;
	return_ACPI_STATUS(status);
}
