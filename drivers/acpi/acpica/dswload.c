// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: dswload - Dispatcher first pass namespace load callbacks
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acparser.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"
#ifdef ACPI_ASL_COMPILER
#include "acdisasm.h"
#endif

#define _COMPONENT          ACPI_DISPATCHER
ACPI_MODULE_NAME("dswload")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_init_callbacks
 *
 * PARAMETERS:  walk_state      - Current state of the parse tree walk
 *              pass_number     - 1, 2, or 3
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Init walk state callbacks
 *
 ******************************************************************************/
acpi_status
acpi_ds_init_callbacks(struct acpi_walk_state *walk_state, u32 pass_number)
{

	switch (pass_number) {
	case 0:

		/* Parse only - caller will setup callbacks */

		walk_state->parse_flags = ACPI_PARSE_LOAD_PASS1 |
		    ACPI_PARSE_DELETE_TREE | ACPI_PARSE_DISASSEMBLE;
		walk_state->descending_callback = NULL;
		walk_state->ascending_callback = NULL;
		break;

	case 1:

		/* Load pass 1 */

		walk_state->parse_flags = ACPI_PARSE_LOAD_PASS1 |
		    ACPI_PARSE_DELETE_TREE;
		walk_state->descending_callback = acpi_ds_load1_begin_op;
		walk_state->ascending_callback = acpi_ds_load1_end_op;
		break;

	case 2:

		/* Load pass 2 */

		walk_state->parse_flags = ACPI_PARSE_LOAD_PASS1 |
		    ACPI_PARSE_DELETE_TREE;
		walk_state->descending_callback = acpi_ds_load2_begin_op;
		walk_state->ascending_callback = acpi_ds_load2_end_op;
		break;

	case 3:

		/* Execution pass */

		walk_state->parse_flags |= ACPI_PARSE_EXECUTE |
		    ACPI_PARSE_DELETE_TREE;
		walk_state->descending_callback = acpi_ds_exec_begin_op;
		walk_state->ascending_callback = acpi_ds_exec_end_op;
		break;

	default:

		return (AE_BAD_PARAMETER);
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_load1_begin_op
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
acpi_ds_load1_begin_op(struct acpi_walk_state *walk_state,
		       union acpi_parse_object **out_op)
{
	union acpi_parse_object *op;
	struct acpi_namespace_node *node;
	acpi_status status;
	acpi_object_type object_type;
	char *path;
	u32 flags;

	ACPI_FUNCTION_TRACE_PTR(ds_load1_begin_op, walk_state->op);

	op = walk_state->op;
	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op,
			  walk_state));

	/* We are only interested in opcodes that have an associated name */

	if (op) {
		if (!(walk_state->op_info->flags & AML_NAMED)) {
			*out_op = op;
			return_ACPI_STATUS(AE_OK);
		}

		/* Check if this object has already been installed in the namespace */

		if (op->common.node) {
			*out_op = op;
			return_ACPI_STATUS(AE_OK);
		}
	}

	path = acpi_ps_get_next_namestring(&walk_state->parser_state);

	/* Map the raw opcode into an internal object type */

	object_type = walk_state->op_info->object_type;

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "State=%p Op=%p [%s]\n", walk_state, op,
			  acpi_ut_get_type_name(object_type)));

	switch (walk_state->opcode) {
	case AML_SCOPE_OP:
		/*
		 * The target name of the Scope() operator must exist at this point so
		 * that we can actually open the scope to enter new names underneath it.
		 * Allow search-to-root for single namesegs.
		 */
		status =
		    acpi_ns_lookup(walk_state->scope_info, path, object_type,
				   ACPI_IMODE_EXECUTE, ACPI_NS_SEARCH_PARENT,
				   walk_state, &(node));
#ifdef ACPI_ASL_COMPILER
		if (status == AE_NOT_FOUND) {
			/*
			 * Table disassembly:
			 * Target of Scope() not found. Generate an External for it, and
			 * insert the name into the namespace.
			 */
			acpi_dm_add_op_to_external_list(op, path,
							ACPI_TYPE_DEVICE, 0, 0);
			status =
			    acpi_ns_lookup(walk_state->scope_info, path,
					   object_type, ACPI_IMODE_LOAD_PASS1,
					   ACPI_NS_SEARCH_PARENT, walk_state,
					   &node);
		}
#endif
		if (ACPI_FAILURE(status)) {
			ACPI_ERROR_NAMESPACE(walk_state->scope_info, path,
					     status);
			return_ACPI_STATUS(status);
		}

		/*
		 * Check to make sure that the target is
		 * one of the opcodes that actually opens a scope
		 */
		switch (node->type) {
		case ACPI_TYPE_ANY:
		case ACPI_TYPE_LOCAL_SCOPE:	/* Scope  */
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
			 *
			 * Note: silently change the type here. On the second pass,
			 * we will report a warning
			 */
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Type override - [%4.4s] had invalid type (%s) "
					  "for Scope operator, changed to type ANY\n",
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
		/*
		 * For all other named opcodes, we will enter the name into
		 * the namespace.
		 *
		 * Setup the search flags.
		 * Since we are entering a name into the namespace, we do not want to
		 * enable the search-to-root upsearch.
		 *
		 * There are only two conditions where it is acceptable that the name
		 * already exists:
		 *    1) the Scope() operator can reopen a scoping object that was
		 *       previously defined (Scope, Method, Device, etc.)
		 *    2) Whenever we are parsing a deferred opcode (op_region, Buffer,
		 *       buffer_field, or Package), the name of the object is already
		 *       in the namespace.
		 */
		if (walk_state->deferred_node) {

			/* This name is already in the namespace, get the node */

			node = walk_state->deferred_node;
			status = AE_OK;
			break;
		}

		/*
		 * If we are executing a method, do not create any namespace objects
		 * during the load phase, only during execution.
		 */
		if (walk_state->method_node) {
			node = NULL;
			status = AE_OK;
			break;
		}

		flags = ACPI_NS_NO_UPSEARCH;
		if ((walk_state->opcode != AML_SCOPE_OP) &&
		    (!(walk_state->parse_flags & ACPI_PARSE_DEFERRED_OP))) {
			if (walk_state->namespace_override) {
				flags |= ACPI_NS_OVERRIDE_IF_FOUND;
				ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
						  "[%s] Override allowed\n",
						  acpi_ut_get_type_name
						  (object_type)));
			} else {
				flags |= ACPI_NS_ERROR_IF_FOUND;
				ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
						  "[%s] Cannot already exist\n",
						  acpi_ut_get_type_name
						  (object_type)));
			}
		} else {
			ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
					  "[%s] Both Find or Create allowed\n",
					  acpi_ut_get_type_name(object_type)));
		}

		/*
		 * Enter the named type into the internal namespace. We enter the name
		 * as we go downward in the parse tree. Any necessary subobjects that
		 * involve arguments to the opcode must be created as we go back up the
		 * parse tree later.
		 */
		status =
		    acpi_ns_lookup(walk_state->scope_info, path, object_type,
				   ACPI_IMODE_LOAD_PASS1, flags, walk_state,
				   &node);
		if (ACPI_FAILURE(status)) {
			if (status == AE_ALREADY_EXISTS) {

				/* The name already exists in this scope */

				if (node->flags & ANOBJ_IS_EXTERNAL) {
					/*
					 * Allow one create on an object or segment that was
					 * previously declared External
					 */
					node->flags &= ~ANOBJ_IS_EXTERNAL;
					node->type = (u8) object_type;

					/* Just retyped a node, probably will need to open a scope */

					if (acpi_ns_opens_scope(object_type)) {
						status =
						    acpi_ds_scope_stack_push
						    (node, object_type,
						     walk_state);
						if (ACPI_FAILURE(status)) {
							return_ACPI_STATUS
							    (status);
						}
					}

					status = AE_OK;
				}
			}

			if (ACPI_FAILURE(status)) {
				ACPI_ERROR_NAMESPACE(walk_state->scope_info,
						     path, status);
				return_ACPI_STATUS(status);
			}
		}
		break;
	}

	/* Common exit */

	if (!op) {

		/* Create a new op */

		op = acpi_ps_alloc_op(walk_state->opcode, walk_state->aml);
		if (!op) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}
	}

	/* Initialize the op */

#ifdef ACPI_CONSTANT_EVAL_ONLY
	op->named.path = path;
#endif

	if (node) {
		/*
		 * Put the Node in the "op" object that the parser uses, so we
		 * can get it again quickly when this scope is closed
		 */
		op->common.node = node;
		op->named.name = node->name.integer;
	}

	acpi_ps_append_arg(acpi_ps_get_parent_scope(&walk_state->parser_state),
			   op);
	*out_op = op;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_load1_end_op
 *
 * PARAMETERS:  walk_state      - Current state of the parse tree walk
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback used during the loading of the namespace,
 *              both control methods and everything else.
 *
 ******************************************************************************/

acpi_status acpi_ds_load1_end_op(struct acpi_walk_state *walk_state)
{
	union acpi_parse_object *op;
	acpi_object_type object_type;
	acpi_status status = AE_OK;
#ifdef ACPI_ASL_COMPILER
	u8 param_count;
#endif

	ACPI_FUNCTION_TRACE(ds_load1_end_op);

	op = walk_state->op;
	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH, "Op=%p State=%p\n", op,
			  walk_state));

	/*
	 * Disassembler: handle create field operators here.
	 *
	 * create_buffer_field is a deferred op that is typically processed in load
	 * pass 2. However, disassembly of control method contents walk the parse
	 * tree with ACPI_PARSE_LOAD_PASS1 and AML_CREATE operators are processed
	 * in a later walk. This is a problem when there is a control method that
	 * has the same name as the AML_CREATE object. In this case, any use of the
	 * name segment will be detected as a method call rather than a reference
	 * to a buffer field.
	 *
	 * This earlier creation during disassembly solves this issue by inserting
	 * the named object in the ACPI namespace so that references to this name
	 * would be a name string rather than a method call.
	 */
	if ((walk_state->parse_flags & ACPI_PARSE_DISASSEMBLE) &&
	    (walk_state->op_info->flags & AML_CREATE)) {
		status = acpi_ds_create_buffer_field(op, walk_state);
		return_ACPI_STATUS(status);
	}

	/* We are only interested in opcodes that have an associated name */

	if (!(walk_state->op_info->flags & (AML_NAMED | AML_FIELD))) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Get the object type to determine if we should pop the scope */

	object_type = walk_state->op_info->object_type;

	if (walk_state->op_info->flags & AML_FIELD) {
		/*
		 * If we are executing a method, do not create any namespace objects
		 * during the load phase, only during execution.
		 */
		if (!walk_state->method_node) {
			if (walk_state->opcode == AML_FIELD_OP ||
			    walk_state->opcode == AML_BANK_FIELD_OP ||
			    walk_state->opcode == AML_INDEX_FIELD_OP) {
				status =
				    acpi_ds_init_field_objects(op, walk_state);
			}
		}
		return_ACPI_STATUS(status);
	}

	/*
	 * If we are executing a method, do not create any namespace objects
	 * during the load phase, only during execution.
	 */
	if (!walk_state->method_node) {
		if (op->common.aml_opcode == AML_REGION_OP) {
			status =
			    acpi_ex_create_region(op->named.data,
						  op->named.length,
						  (acpi_adr_space_type)
						  ((op->common.value.arg)->
						   common.value.integer),
						  walk_state);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		} else if (op->common.aml_opcode == AML_DATA_REGION_OP) {
			status =
			    acpi_ex_create_region(op->named.data,
						  op->named.length,
						  ACPI_ADR_SPACE_DATA_TABLE,
						  walk_state);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}
	}

	if (op->common.aml_opcode == AML_NAME_OP) {

		/* For Name opcode, get the object type from the argument */

		if (op->common.value.arg) {
			object_type = (acpi_ps_get_opcode_info((op->common.
								value.arg)->
							       common.
							       aml_opcode))->
			    object_type;

			/* Set node type if we have a namespace node */

			if (op->common.node) {
				op->common.node->type = (u8) object_type;
			}
		}
	}
#ifdef ACPI_ASL_COMPILER
	/*
	 * For external opcode, get the object type from the argument and
	 * get the parameter count from the argument's next.
	 */
	if (acpi_gbl_disasm_flag &&
	    op->common.node && op->common.aml_opcode == AML_EXTERNAL_OP) {
		/*
		 * Note, if this external is not a method
		 * Op->Common.Value.Arg->Common.Next->Common.Value.Integer == 0
		 * Therefore, param_count will be 0.
		 */
		param_count =
		    (u8)op->common.value.arg->common.next->common.value.integer;
		object_type = (u8)op->common.value.arg->common.value.integer;
		op->common.node->flags |= ANOBJ_IS_EXTERNAL;
		op->common.node->type = (u8)object_type;

		acpi_dm_create_subobject_for_external((u8)object_type,
						      &op->common.node,
						      param_count);

		/*
		 * Add the external to the external list because we may be
		 * emitting code based off of the items within the external list.
		 */
		acpi_dm_add_op_to_external_list(op, op->named.path,
						(u8)object_type, param_count,
						ACPI_EXT_ORIGIN_FROM_OPCODE |
						ACPI_EXT_RESOLVED_REFERENCE);
	}
#endif

	/*
	 * If we are executing a method, do not create any namespace objects
	 * during the load phase, only during execution.
	 */
	if (!walk_state->method_node) {
		if (op->common.aml_opcode == AML_METHOD_OP) {
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
		}
	}

	/* Pop the scope stack (only if loading a table) */

	if (!walk_state->method_node &&
	    op->common.aml_opcode != AML_EXTERNAL_OP &&
	    acpi_ns_opens_scope(object_type)) {
		ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
				  "(%s): Popping scope for Op %p\n",
				  acpi_ut_get_type_name(object_type), op));

		status = acpi_ds_scope_stack_pop(walk_state);
	}

	return_ACPI_STATUS(status);
}
