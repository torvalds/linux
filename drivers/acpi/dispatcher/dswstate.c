/******************************************************************************
 *
 * Module Name: dswstate - Dispatcher parse tree walk management routines
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2007, R. Byron Moore
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
#include <acpi/acparser.h>
#include <acpi/acdispat.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_DISPATCHER
ACPI_MODULE_NAME("dswstate")

/* Local prototypes */
#ifdef ACPI_OBSOLETE_FUNCTIONS
acpi_status
acpi_ds_result_insert(void *object,
		      u32 index, struct acpi_walk_state *walk_state);

acpi_status acpi_ds_obj_stack_delete_all(struct acpi_walk_state *walk_state);

acpi_status
acpi_ds_obj_stack_pop_object(union acpi_operand_object **object,
			     struct acpi_walk_state *walk_state);

void *acpi_ds_obj_stack_get_value(u32 index,
				  struct acpi_walk_state *walk_state);
#endif

#ifdef ACPI_FUTURE_USAGE
/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_result_remove
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              Index               - Where to extract the object
 *              walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop an object off the bottom of this walk's result stack.  In
 *              other words, this is a FIFO.
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_remove(union acpi_operand_object **object,
		      u32 index, struct acpi_walk_state *walk_state)
{
	union acpi_generic_state *state;

	ACPI_FUNCTION_NAME(ds_result_remove);

	state = walk_state->results;
	if (!state) {
		ACPI_ERROR((AE_INFO, "No result object pushed! State=%p",
			    walk_state));
		return (AE_NOT_EXIST);
	}

	if (index >= ACPI_OBJ_MAX_OPERAND) {
		ACPI_ERROR((AE_INFO,
			    "Index out of range: %X State=%p Num=%X",
			    index, walk_state, state->results.num_results));
	}

	/* Check for a valid result object */

	if (!state->results.obj_desc[index]) {
		ACPI_ERROR((AE_INFO,
			    "Null operand! State=%p #Ops=%X, Index=%X",
			    walk_state, state->results.num_results, index));
		return (AE_AML_NO_RETURN_VALUE);
	}

	/* Remove the object */

	state->results.num_results--;

	*object = state->results.obj_desc[index];
	state->results.obj_desc[index] = NULL;

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "Obj=%p [%s] Index=%X State=%p Num=%X\n",
			  *object,
			  (*object) ? acpi_ut_get_object_type_name(*object) :
			  "NULL", index, walk_state,
			  state->results.num_results));

	return (AE_OK);
}
#endif				/*  ACPI_FUTURE_USAGE  */

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_result_pop
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop an object off the bottom of this walk's result stack.  In
 *              other words, this is a FIFO.
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_pop(union acpi_operand_object ** object,
		   struct acpi_walk_state * walk_state)
{
	acpi_native_uint index;
	union acpi_generic_state *state;

	ACPI_FUNCTION_NAME(ds_result_pop);

	state = walk_state->results;
	if (!state) {
		return (AE_OK);
	}

	if (!state->results.num_results) {
		ACPI_ERROR((AE_INFO, "Result stack is empty! State=%p",
			    walk_state));
		return (AE_AML_NO_RETURN_VALUE);
	}

	/* Remove top element */

	state->results.num_results--;

	for (index = ACPI_OBJ_NUM_OPERANDS; index; index--) {

		/* Check for a valid result object */

		if (state->results.obj_desc[index - 1]) {
			*object = state->results.obj_desc[index - 1];
			state->results.obj_desc[index - 1] = NULL;

			ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
					  "Obj=%p [%s] Index=%X State=%p Num=%X\n",
					  *object,
					  (*object) ?
					  acpi_ut_get_object_type_name(*object)
					  : "NULL", (u32) index - 1, walk_state,
					  state->results.num_results));

			return (AE_OK);
		}
	}

	ACPI_ERROR((AE_INFO, "No result objects! State=%p", walk_state));
	return (AE_AML_NO_RETURN_VALUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_result_pop_from_bottom
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop an object off the bottom of this walk's result stack.  In
 *              other words, this is a FIFO.
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_pop_from_bottom(union acpi_operand_object ** object,
			       struct acpi_walk_state * walk_state)
{
	acpi_native_uint index;
	union acpi_generic_state *state;

	ACPI_FUNCTION_NAME(ds_result_pop_from_bottom);

	state = walk_state->results;
	if (!state) {
		ACPI_ERROR((AE_INFO,
			    "No result object pushed! State=%p", walk_state));
		return (AE_NOT_EXIST);
	}

	if (!state->results.num_results) {
		ACPI_ERROR((AE_INFO, "No result objects! State=%p",
			    walk_state));
		return (AE_AML_NO_RETURN_VALUE);
	}

	/* Remove Bottom element */

	*object = state->results.obj_desc[0];

	/* Push entire stack down one element */

	for (index = 0; index < state->results.num_results; index++) {
		state->results.obj_desc[index] =
		    state->results.obj_desc[index + 1];
	}

	state->results.num_results--;

	/* Check for a valid result object */

	if (!*object) {
		ACPI_ERROR((AE_INFO,
			    "Null operand! State=%p #Ops=%X Index=%X",
			    walk_state, state->results.num_results,
			    (u32) index));
		return (AE_AML_NO_RETURN_VALUE);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Obj=%p [%s] Results=%p State=%p\n",
			  *object,
			  (*object) ? acpi_ut_get_object_type_name(*object) :
			  "NULL", state, walk_state));

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_result_push
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push an object onto the current result stack
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_push(union acpi_operand_object * object,
		    struct acpi_walk_state * walk_state)
{
	union acpi_generic_state *state;

	ACPI_FUNCTION_NAME(ds_result_push);

	state = walk_state->results;
	if (!state) {
		ACPI_ERROR((AE_INFO, "No result stack frame during push"));
		return (AE_AML_INTERNAL);
	}

	if (state->results.num_results == ACPI_OBJ_NUM_OPERANDS) {
		ACPI_ERROR((AE_INFO,
			    "Result stack overflow: Obj=%p State=%p Num=%X",
			    object, walk_state, state->results.num_results));
		return (AE_STACK_OVERFLOW);
	}

	if (!object) {
		ACPI_ERROR((AE_INFO,
			    "Null Object! Obj=%p State=%p Num=%X",
			    object, walk_state, state->results.num_results));
		return (AE_BAD_PARAMETER);
	}

	state->results.obj_desc[state->results.num_results] = object;
	state->results.num_results++;

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Obj=%p [%s] State=%p Num=%X Cur=%X\n",
			  object,
			  object ?
			  acpi_ut_get_object_type_name((union
							acpi_operand_object *)
						       object) : "NULL",
			  walk_state, state->results.num_results,
			  walk_state->current_result));

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_result_stack_push
 *
 * PARAMETERS:  walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push an object onto the walk_state result stack.
 *
 ******************************************************************************/

acpi_status acpi_ds_result_stack_push(struct acpi_walk_state * walk_state)
{
	union acpi_generic_state *state;

	ACPI_FUNCTION_NAME(ds_result_stack_push);

	state = acpi_ut_create_generic_state();
	if (!state) {
		return (AE_NO_MEMORY);
	}

	state->common.descriptor_type = ACPI_DESC_TYPE_STATE_RESULT;
	acpi_ut_push_generic_state(&walk_state->results, state);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Results=%p State=%p\n",
			  state, walk_state));

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_result_stack_pop
 *
 * PARAMETERS:  walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop an object off of the walk_state result stack.
 *
 ******************************************************************************/

acpi_status acpi_ds_result_stack_pop(struct acpi_walk_state * walk_state)
{
	union acpi_generic_state *state;

	ACPI_FUNCTION_NAME(ds_result_stack_pop);

	/* Check for stack underflow */

	if (walk_state->results == NULL) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Underflow - State=%p\n",
				  walk_state));
		return (AE_AML_NO_OPERAND);
	}

	state = acpi_ut_pop_generic_state(&walk_state->results);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "Result=%p RemainingResults=%X State=%p\n",
			  state, state->results.num_results, walk_state));

	acpi_ut_delete_generic_state(state);

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_obj_stack_push
 *
 * PARAMETERS:  Object              - Object to push
 *              walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push an object onto this walk's object/operand stack
 *
 ******************************************************************************/

acpi_status
acpi_ds_obj_stack_push(void *object, struct acpi_walk_state * walk_state)
{
	ACPI_FUNCTION_NAME(ds_obj_stack_push);

	/* Check for stack overflow */

	if (walk_state->num_operands >= ACPI_OBJ_NUM_OPERANDS) {
		ACPI_ERROR((AE_INFO,
			    "Object stack overflow! Obj=%p State=%p #Ops=%X",
			    object, walk_state, walk_state->num_operands));
		return (AE_STACK_OVERFLOW);
	}

	/* Put the object onto the stack */

	walk_state->operands[walk_state->num_operands] = object;
	walk_state->num_operands++;

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Obj=%p [%s] State=%p #Ops=%X\n",
			  object,
			  acpi_ut_get_object_type_name((union
							acpi_operand_object *)
						       object), walk_state,
			  walk_state->num_operands));

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_obj_stack_pop
 *
 * PARAMETERS:  pop_count           - Number of objects/entries to pop
 *              walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop this walk's object stack.  Objects on the stack are NOT
 *              deleted by this routine.
 *
 ******************************************************************************/

acpi_status
acpi_ds_obj_stack_pop(u32 pop_count, struct acpi_walk_state * walk_state)
{
	u32 i;

	ACPI_FUNCTION_NAME(ds_obj_stack_pop);

	for (i = 0; i < pop_count; i++) {

		/* Check for stack underflow */

		if (walk_state->num_operands == 0) {
			ACPI_ERROR((AE_INFO,
				    "Object stack underflow! Count=%X State=%p #Ops=%X",
				    pop_count, walk_state,
				    walk_state->num_operands));
			return (AE_STACK_UNDERFLOW);
		}

		/* Just set the stack entry to null */

		walk_state->num_operands--;
		walk_state->operands[walk_state->num_operands] = NULL;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Count=%X State=%p #Ops=%X\n",
			  pop_count, walk_state, walk_state->num_operands));

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_obj_stack_pop_and_delete
 *
 * PARAMETERS:  pop_count           - Number of objects/entries to pop
 *              walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop this walk's object stack and delete each object that is
 *              popped off.
 *
 ******************************************************************************/

acpi_status
acpi_ds_obj_stack_pop_and_delete(u32 pop_count,
				 struct acpi_walk_state * walk_state)
{
	u32 i;
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_NAME(ds_obj_stack_pop_and_delete);

	for (i = 0; i < pop_count; i++) {

		/* Check for stack underflow */

		if (walk_state->num_operands == 0) {
			ACPI_ERROR((AE_INFO,
				    "Object stack underflow! Count=%X State=%p #Ops=%X",
				    pop_count, walk_state,
				    walk_state->num_operands));
			return (AE_STACK_UNDERFLOW);
		}

		/* Pop the stack and delete an object if present in this stack entry */

		walk_state->num_operands--;
		obj_desc = walk_state->operands[walk_state->num_operands];
		if (obj_desc) {
			acpi_ut_remove_reference(walk_state->
						 operands[walk_state->
							  num_operands]);
			walk_state->operands[walk_state->num_operands] = NULL;
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Count=%X State=%p #Ops=%X\n",
			  pop_count, walk_state, walk_state->num_operands));

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_get_current_walk_state
 *
 * PARAMETERS:  Thread          - Get current active state for this Thread
 *
 * RETURN:      Pointer to the current walk state
 *
 * DESCRIPTION: Get the walk state that is at the head of the list (the "current"
 *              walk state.)
 *
 ******************************************************************************/

struct acpi_walk_state *acpi_ds_get_current_walk_state(struct acpi_thread_state
						       *thread)
{
	ACPI_FUNCTION_NAME(ds_get_current_walk_state);

	if (!thread) {
		return (NULL);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_PARSE, "Current WalkState %p\n",
			  thread->walk_state_list));

	return (thread->walk_state_list);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_push_walk_state
 *
 * PARAMETERS:  walk_state      - State to push
 *              Thread          - Thread state object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Place the Thread state at the head of the state list.
 *
 ******************************************************************************/

void
acpi_ds_push_walk_state(struct acpi_walk_state *walk_state,
			struct acpi_thread_state *thread)
{
	ACPI_FUNCTION_TRACE(ds_push_walk_state);

	walk_state->next = thread->walk_state_list;
	thread->walk_state_list = walk_state;

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_pop_walk_state
 *
 * PARAMETERS:  Thread      - Current thread state
 *
 * RETURN:      A walk_state object popped from the thread's stack
 *
 * DESCRIPTION: Remove and return the walkstate object that is at the head of
 *              the walk stack for the given walk list.  NULL indicates that
 *              the list is empty.
 *
 ******************************************************************************/

struct acpi_walk_state *acpi_ds_pop_walk_state(struct acpi_thread_state *thread)
{
	struct acpi_walk_state *walk_state;

	ACPI_FUNCTION_TRACE(ds_pop_walk_state);

	walk_state = thread->walk_state_list;

	if (walk_state) {

		/* Next walk state becomes the current walk state */

		thread->walk_state_list = walk_state->next;

		/*
		 * Don't clear the NEXT field, this serves as an indicator
		 * that there is a parent WALK STATE
		 * Do Not: walk_state->Next = NULL;
		 */
	}

	return_PTR(walk_state);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_create_walk_state
 *
 * PARAMETERS:  owner_id        - ID for object creation
 *              Origin          - Starting point for this walk
 *              method_desc     - Method object
 *              Thread          - Current thread state
 *
 * RETURN:      Pointer to the new walk state.
 *
 * DESCRIPTION: Allocate and initialize a new walk state.  The current walk
 *              state is set to this new state.
 *
 ******************************************************************************/

struct acpi_walk_state *acpi_ds_create_walk_state(acpi_owner_id owner_id, union acpi_parse_object
						  *origin, union acpi_operand_object
						  *method_desc, struct acpi_thread_state
						  *thread)
{
	struct acpi_walk_state *walk_state;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ds_create_walk_state);

	walk_state = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_walk_state));
	if (!walk_state) {
		return_PTR(NULL);
	}

	walk_state->descriptor_type = ACPI_DESC_TYPE_WALK;
	walk_state->method_desc = method_desc;
	walk_state->owner_id = owner_id;
	walk_state->origin = origin;
	walk_state->thread = thread;

	walk_state->parser_state.start_op = origin;

	/* Init the method args/local */

#if (!defined (ACPI_NO_METHOD_EXECUTION) && !defined (ACPI_CONSTANT_EVAL_ONLY))
	acpi_ds_method_data_init(walk_state);
#endif

	/* Create an initial result stack entry */

	status = acpi_ds_result_stack_push(walk_state);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(walk_state);
		return_PTR(NULL);
	}

	/* Put the new state at the head of the walk list */

	if (thread) {
		acpi_ds_push_walk_state(walk_state, thread);
	}

	return_PTR(walk_state);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_init_aml_walk
 *
 * PARAMETERS:  walk_state      - New state to be initialized
 *              Op              - Current parse op
 *              method_node     - Control method NS node, if any
 *              aml_start       - Start of AML
 *              aml_length      - Length of AML
 *              Info            - Method info block (params, etc.)
 *              pass_number     - 1, 2, or 3
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize a walk state for a pass 1 or 2 parse tree walk
 *
 ******************************************************************************/

acpi_status
acpi_ds_init_aml_walk(struct acpi_walk_state *walk_state,
		      union acpi_parse_object *op,
		      struct acpi_namespace_node *method_node,
		      u8 * aml_start,
		      u32 aml_length,
		      struct acpi_evaluate_info *info, u8 pass_number)
{
	acpi_status status;
	struct acpi_parse_state *parser_state = &walk_state->parser_state;
	union acpi_parse_object *extra_op;

	ACPI_FUNCTION_TRACE(ds_init_aml_walk);

	walk_state->parser_state.aml =
	    walk_state->parser_state.aml_start = aml_start;
	walk_state->parser_state.aml_end =
	    walk_state->parser_state.pkg_end = aml_start + aml_length;

	/* The next_op of the next_walk will be the beginning of the method */

	walk_state->next_op = NULL;
	walk_state->pass_number = pass_number;

	if (info) {
		if (info->parameter_type == ACPI_PARAM_GPE) {
			walk_state->gpe_event_info =
			    ACPI_CAST_PTR(struct acpi_gpe_event_info,
					  info->parameters);
		} else {
			walk_state->params = info->parameters;
			walk_state->caller_return_desc = &info->return_object;
		}
	}

	status = acpi_ps_init_scope(&walk_state->parser_state, op);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (method_node) {
		walk_state->parser_state.start_node = method_node;
		walk_state->walk_type = ACPI_WALK_METHOD;
		walk_state->method_node = method_node;
		walk_state->method_desc =
		    acpi_ns_get_attached_object(method_node);

		/* Push start scope on scope stack and make it current  */

		status =
		    acpi_ds_scope_stack_push(method_node, ACPI_TYPE_METHOD,
					     walk_state);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/* Init the method arguments */

		status = acpi_ds_method_data_init_args(walk_state->params,
						       ACPI_METHOD_NUM_ARGS,
						       walk_state);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	} else {
		/*
		 * Setup the current scope.
		 * Find a Named Op that has a namespace node associated with it.
		 * search upwards from this Op.  Current scope is the first
		 * Op with a namespace node.
		 */
		extra_op = parser_state->start_op;
		while (extra_op && !extra_op->common.node) {
			extra_op = extra_op->common.parent;
		}

		if (!extra_op) {
			parser_state->start_node = NULL;
		} else {
			parser_state->start_node = extra_op->common.node;
		}

		if (parser_state->start_node) {

			/* Push start scope on scope stack and make it current  */

			status =
			    acpi_ds_scope_stack_push(parser_state->start_node,
						     parser_state->start_node->
						     type, walk_state);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}
	}

	status = acpi_ds_init_callbacks(walk_state, pass_number);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_delete_walk_state
 *
 * PARAMETERS:  walk_state      - State to delete
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete a walk state including all internal data structures
 *
 ******************************************************************************/

void acpi_ds_delete_walk_state(struct acpi_walk_state *walk_state)
{
	union acpi_generic_state *state;

	ACPI_FUNCTION_TRACE_PTR(ds_delete_walk_state, walk_state);

	if (!walk_state) {
		return;
	}

	if (walk_state->descriptor_type != ACPI_DESC_TYPE_WALK) {
		ACPI_ERROR((AE_INFO, "%p is not a valid walk state",
			    walk_state));
		return;
	}

	/* There should not be any open scopes */

	if (walk_state->parser_state.scope) {
		ACPI_ERROR((AE_INFO, "%p walk still has a scope list",
			    walk_state));
		acpi_ps_cleanup_scope(&walk_state->parser_state);
	}

	/* Always must free any linked control states */

	while (walk_state->control_state) {
		state = walk_state->control_state;
		walk_state->control_state = state->common.next;

		acpi_ut_delete_generic_state(state);
	}

	/* Always must free any linked parse states */

	while (walk_state->scope_info) {
		state = walk_state->scope_info;
		walk_state->scope_info = state->common.next;

		acpi_ut_delete_generic_state(state);
	}

	/* Always must free any stacked result states */

	while (walk_state->results) {
		state = walk_state->results;
		walk_state->results = state->common.next;

		acpi_ut_delete_generic_state(state);
	}

	ACPI_FREE(walk_state);
	return_VOID;
}

#ifdef ACPI_OBSOLETE_FUNCTIONS
/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_result_insert
 *
 * PARAMETERS:  Object              - Object to push
 *              Index               - Where to insert the object
 *              walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Insert an object onto this walk's result stack
 *
 ******************************************************************************/

acpi_status
acpi_ds_result_insert(void *object,
		      u32 index, struct acpi_walk_state *walk_state)
{
	union acpi_generic_state *state;

	ACPI_FUNCTION_NAME(ds_result_insert);

	state = walk_state->results;
	if (!state) {
		ACPI_ERROR((AE_INFO, "No result object pushed! State=%p",
			    walk_state));
		return (AE_NOT_EXIST);
	}

	if (index >= ACPI_OBJ_NUM_OPERANDS) {
		ACPI_ERROR((AE_INFO,
			    "Index out of range: %X Obj=%p State=%p Num=%X",
			    index, object, walk_state,
			    state->results.num_results));
		return (AE_BAD_PARAMETER);
	}

	if (!object) {
		ACPI_ERROR((AE_INFO,
			    "Null Object! Index=%X Obj=%p State=%p Num=%X",
			    index, object, walk_state,
			    state->results.num_results));
		return (AE_BAD_PARAMETER);
	}

	state->results.obj_desc[index] = object;
	state->results.num_results++;

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "Obj=%p [%s] State=%p Num=%X Cur=%X\n",
			  object,
			  object ?
			  acpi_ut_get_object_type_name((union
							acpi_operand_object *)
						       object) : "NULL",
			  walk_state, state->results.num_results,
			  walk_state->current_result));

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_obj_stack_delete_all
 *
 * PARAMETERS:  walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear the object stack by deleting all objects that are on it.
 *              Should be used with great care, if at all!
 *
 ******************************************************************************/

acpi_status acpi_ds_obj_stack_delete_all(struct acpi_walk_state * walk_state)
{
	u32 i;

	ACPI_FUNCTION_TRACE_PTR(ds_obj_stack_delete_all, walk_state);

	/* The stack size is configurable, but fixed */

	for (i = 0; i < ACPI_OBJ_NUM_OPERANDS; i++) {
		if (walk_state->operands[i]) {
			acpi_ut_remove_reference(walk_state->operands[i]);
			walk_state->operands[i] = NULL;
		}
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_obj_stack_pop_object
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              walk_state          - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop this walk's object stack.  Objects on the stack are NOT
 *              deleted by this routine.
 *
 ******************************************************************************/

acpi_status
acpi_ds_obj_stack_pop_object(union acpi_operand_object **object,
			     struct acpi_walk_state *walk_state)
{
	ACPI_FUNCTION_NAME(ds_obj_stack_pop_object);

	/* Check for stack underflow */

	if (walk_state->num_operands == 0) {
		ACPI_ERROR((AE_INFO,
			    "Missing operand/stack empty! State=%p #Ops=%X",
			    walk_state, walk_state->num_operands));
		*object = NULL;
		return (AE_AML_NO_OPERAND);
	}

	/* Pop the stack */

	walk_state->num_operands--;

	/* Check for a valid operand */

	if (!walk_state->operands[walk_state->num_operands]) {
		ACPI_ERROR((AE_INFO,
			    "Null operand! State=%p #Ops=%X",
			    walk_state, walk_state->num_operands));
		*object = NULL;
		return (AE_AML_NO_OPERAND);
	}

	/* Get operand and set stack entry to null */

	*object = walk_state->operands[walk_state->num_operands];
	walk_state->operands[walk_state->num_operands] = NULL;

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Obj=%p [%s] State=%p #Ops=%X\n",
			  *object, acpi_ut_get_object_type_name(*object),
			  walk_state, walk_state->num_operands));

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_obj_stack_get_value
 *
 * PARAMETERS:  Index               - Stack index whose value is desired.  Based
 *                                    on the top of the stack (index=0 == top)
 *              walk_state          - Current Walk state
 *
 * RETURN:      Pointer to the requested operand
 *
 * DESCRIPTION: Retrieve an object from this walk's operand stack.  Index must
 *              be within the range of the current stack pointer.
 *
 ******************************************************************************/

void *acpi_ds_obj_stack_get_value(u32 index, struct acpi_walk_state *walk_state)
{

	ACPI_FUNCTION_TRACE_PTR(ds_obj_stack_get_value, walk_state);

	/* Can't do it if the stack is empty */

	if (walk_state->num_operands == 0) {
		return_PTR(NULL);
	}

	/* or if the index is past the top of the stack */

	if (index > (walk_state->num_operands - (u32) 1)) {
		return_PTR(NULL);
	}

	return_PTR(walk_state->
		   operands[(acpi_native_uint) (walk_state->num_operands - 1) -
			    index]);
}
#endif
