/*******************************************************************************
 *
 * Module Name: nseval - Object evaluation interfaces -- includes control
 *                       method lookup and execution.
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
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
#include <acpi/acinterp.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nseval")

/* Local prototypes */
static acpi_status
acpi_ns_execute_control_method(struct acpi_parameter_info *info);

static acpi_status acpi_ns_get_object_value(struct acpi_parameter_info *info);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_evaluate_relative
 *
 * PARAMETERS:  Pathname        - Name of method to execute, If NULL, the
 *                                handle is the object to execute
 *              Info            - Method info block, contains:
 *                  return_object   - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *                  Params          - List of parameters to pass to the method,
 *                                    terminated by NULL.  Params itself may be
 *                                    NULL if no parameters are being passed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluate the object or find and execute the requested method
 *
 * MUTEX:       Locks Namespace
 *
 ******************************************************************************/

acpi_status
acpi_ns_evaluate_relative(char *pathname, struct acpi_parameter_info *info)
{
	acpi_status status;
	struct acpi_namespace_node *node = NULL;
	union acpi_generic_state *scope_info;
	char *internal_path = NULL;

	ACPI_FUNCTION_TRACE("ns_evaluate_relative");

	/*
	 * Must have a valid object handle
	 */
	if (!info || !info->node) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Build an internal name string for the method */

	status = acpi_ns_internalize_name(pathname, &internal_path);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	scope_info = acpi_ut_create_generic_state();
	if (!scope_info) {
		goto cleanup1;
	}

	/* Get the prefix handle and Node */

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	info->node = acpi_ns_map_handle_to_node(info->node);
	if (!info->node) {
		(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
		status = AE_BAD_PARAMETER;
		goto cleanup;
	}

	/* Lookup the name in the namespace */

	scope_info->scope.node = info->node;
	status = acpi_ns_lookup(scope_info, internal_path, ACPI_TYPE_ANY,
				ACPI_IMODE_EXECUTE, ACPI_NS_NO_UPSEARCH, NULL,
				&node);

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_NAMES, "Object [%s] not found [%s]\n",
				  pathname, acpi_format_exception(status)));
		goto cleanup;
	}

	/*
	 * Now that we have a handle to the object, we can attempt to evaluate it.
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_NAMES, "%s [%p] Value %p\n",
			  pathname, node, acpi_ns_get_attached_object(node)));

	info->node = node;
	status = acpi_ns_evaluate_by_handle(info);

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
			  "*** Completed eval of object %s ***\n", pathname));

      cleanup:
	acpi_ut_delete_generic_state(scope_info);

      cleanup1:
	ACPI_FREE(internal_path);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_evaluate_by_name
 *
 * PARAMETERS:  Pathname        - Fully qualified pathname to the object
 *              Info                - Method info block, contains:
 *                  return_object   - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *                  Params          - List of parameters to pass to the method,
 *                                    terminated by NULL.  Params itself may be
 *                                    NULL if no parameters are being passed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluate the object or rind and execute the requested method
 *              passing the given parameters
 *
 * MUTEX:       Locks Namespace
 *
 ******************************************************************************/

acpi_status
acpi_ns_evaluate_by_name(char *pathname, struct acpi_parameter_info *info)
{
	acpi_status status;
	char *internal_path = NULL;

	ACPI_FUNCTION_TRACE("ns_evaluate_by_name");

	/* Build an internal name string for the method */

	status = acpi_ns_internalize_name(pathname, &internal_path);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Lookup the name in the namespace */

	status = acpi_ns_lookup(NULL, internal_path, ACPI_TYPE_ANY,
				ACPI_IMODE_EXECUTE, ACPI_NS_NO_UPSEARCH, NULL,
				&info->node);

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
				  "Object at [%s] was not found, status=%.4X\n",
				  pathname, status));
		goto cleanup;
	}

	/*
	 * Now that we have a handle to the object, we can attempt to evaluate it.
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_NAMES, "%s [%p] Value %p\n",
			  pathname, info->node,
			  acpi_ns_get_attached_object(info->node)));

	status = acpi_ns_evaluate_by_handle(info);

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
			  "*** Completed eval of object %s ***\n", pathname));

      cleanup:

	/* Cleanup */

	if (internal_path) {
		ACPI_FREE(internal_path);
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_evaluate_by_handle
 *
 * PARAMETERS:  Info            - Method info block, contains:
 *                  Node            - Method/Object Node to execute
 *                  Parameters      - List of parameters to pass to the method,
 *                                    terminated by NULL. Params itself may be
 *                                    NULL if no parameters are being passed.
 *                  return_object   - Where to put method's return value (if
 *                                    any). If NULL, no value is returned.
 *                  parameter_type  - Type of Parameter list
 *                  return_object   - Where to put method's return value (if
 *                                    any). If NULL, no value is returned.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluate object or execute the requested method passing the
 *              given parameters
 *
 * MUTEX:       Locks Namespace
 *
 ******************************************************************************/

acpi_status acpi_ns_evaluate_by_handle(struct acpi_parameter_info *info)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE("ns_evaluate_by_handle");

	/* Check if namespace has been initialized */

	if (!acpi_gbl_root_node) {
		return_ACPI_STATUS(AE_NO_NAMESPACE);
	}

	/* Parameter Validation */

	if (!info) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Initialize the return value to an invalid object */

	info->return_object = NULL;

	/* Get the prefix handle and Node */

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	info->node = acpi_ns_map_handle_to_node(info->node);
	if (!info->node) {
		(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * For a method alias, we must grab the actual method node so that proper
	 * scoping context will be established before execution.
	 */
	if (acpi_ns_get_type(info->node) == ACPI_TYPE_LOCAL_METHOD_ALIAS) {
		info->node =
		    ACPI_CAST_PTR(struct acpi_namespace_node,
				  info->node->object);
	}

	/*
	 * Two major cases here:
	 * 1) The object is an actual control method -- execute it.
	 * 2) The object is not a method -- just return it's current value
	 *
	 * In both cases, the namespace is unlocked by the acpi_ns* procedure
	 */
	if (acpi_ns_get_type(info->node) == ACPI_TYPE_METHOD) {
		/*
		 * Case 1) We have an actual control method to execute
		 */
		status = acpi_ns_execute_control_method(info);
	} else {
		/*
		 * Case 2) Object is NOT a method, just return its current value
		 */
		status = acpi_ns_get_object_value(info);
	}

	/*
	 * Check if there is a return value on the stack that must be dealt with
	 */
	if (status == AE_CTRL_RETURN_VALUE) {

		/* Map AE_CTRL_RETURN_VALUE to AE_OK, we are done with it */

		status = AE_OK;
	}

	/*
	 * Namespace was unlocked by the handling acpi_ns* function, so we
	 * just return
	 */
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_execute_control_method
 *
 * PARAMETERS:  Info            - Method info block, contains:
 *                  Node            - Method Node to execute
 *                  obj_desc        - Method object
 *                  Parameters      - List of parameters to pass to the method,
 *                                    terminated by NULL. Params itself may be
 *                                    NULL if no parameters are being passed.
 *                  return_object   - Where to put method's return value (if
 *                                    any). If NULL, no value is returned.
 *                  parameter_type  - Type of Parameter list
 *                  return_object   - Where to put method's return value (if
 *                                    any). If NULL, no value is returned.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute the requested method passing the given parameters
 *
 * MUTEX:       Assumes namespace is locked
 *
 ******************************************************************************/

static acpi_status
acpi_ns_execute_control_method(struct acpi_parameter_info *info)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE("ns_execute_control_method");

	/* Verify that there is a method associated with this object */

	info->obj_desc = acpi_ns_get_attached_object(info->node);
	if (!info->obj_desc) {
		ACPI_ERROR((AE_INFO, "No attached method object"));

		(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
		return_ACPI_STATUS(AE_NULL_OBJECT);
	}

	ACPI_DUMP_PATHNAME(info->node, "Execute Method:",
			   ACPI_LV_INFO, _COMPONENT);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Method at AML address %p Length %X\n",
			  info->obj_desc->method.aml_start + 1,
			  info->obj_desc->method.aml_length - 1));

	/*
	 * Unlock the namespace before execution.  This allows namespace access
	 * via the external Acpi* interfaces while a method is being executed.
	 * However, any namespace deletion must acquire both the namespace and
	 * interpreter locks to ensure that no thread is using the portion of the
	 * namespace that is being deleted.
	 */
	status = acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Execute the method via the interpreter.  The interpreter is locked
	 * here before calling into the AML parser
	 */
	status = acpi_ex_enter_interpreter();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_ps_execute_method(info);
	acpi_ex_exit_interpreter();

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_object_value
 *
 * PARAMETERS:  Info            - Method info block, contains:
 *                  Node            - Object's NS node
 *                  return_object   - Where to put object value (if
 *                                    any). If NULL, no value is returned.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the current value of the object
 *
 * MUTEX:       Assumes namespace is locked, leaves namespace unlocked
 *
 ******************************************************************************/

static acpi_status acpi_ns_get_object_value(struct acpi_parameter_info *info)
{
	acpi_status status = AE_OK;
	struct acpi_namespace_node *resolved_node = info->node;

	ACPI_FUNCTION_TRACE("ns_get_object_value");

	/*
	 * Objects require additional resolution steps (e.g., the Node may be a
	 * field that must be read, etc.) -- we can't just grab the object out of
	 * the node.
	 */

	/*
	 * Use resolve_node_to_value() to get the associated value. This call always
	 * deletes obj_desc (allocated above).
	 *
	 * NOTE: we can get away with passing in NULL for a walk state because
	 * obj_desc is guaranteed to not be a reference to either a method local or
	 * a method argument (because this interface can only be called from the
	 * acpi_evaluate external interface, never called from a running method.)
	 *
	 * Even though we do not directly invoke the interpreter for this, we must
	 * enter it because we could access an opregion. The opregion access code
	 * assumes that the interpreter is locked.
	 *
	 * We must release the namespace lock before entering the intepreter.
	 */
	status = acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_ex_enter_interpreter();
	if (ACPI_SUCCESS(status)) {
		status = acpi_ex_resolve_node_to_value(&resolved_node, NULL);
		/*
		 * If acpi_ex_resolve_node_to_value() succeeded, the return value was placed
		 * in resolved_node.
		 */
		acpi_ex_exit_interpreter();

		if (ACPI_SUCCESS(status)) {
			status = AE_CTRL_RETURN_VALUE;
			info->return_object = ACPI_CAST_PTR
			    (union acpi_operand_object, resolved_node);
			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "Returning object %p [%s]\n",
					  info->return_object,
					  acpi_ut_get_object_type_name(info->
								       return_object)));
		}
	}

	/* Namespace is unlocked */

	return_ACPI_STATUS(status);
}
