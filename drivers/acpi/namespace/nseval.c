/*******************************************************************************
 *
 * Module Name: nseval - Object evaluation, includes control method execution
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
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

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_evaluate
 *
 * PARAMETERS:  Info            - Evaluation info block, contains:
 *                  prefix_node     - Prefix or Method/Object Node to execute
 *                  Pathname        - Name of method to execute, If NULL, the
 *                                    Node is the object to execute
 *                  Parameters      - List of parameters to pass to the method,
 *                                    terminated by NULL. Params itself may be
 *                                    NULL if no parameters are being passed.
 *                  return_object   - Where to put method's return value (if
 *                                    any). If NULL, no value is returned.
 *                  parameter_type  - Type of Parameter list
 *                  return_object   - Where to put method's return value (if
 *                                    any). If NULL, no value is returned.
 *                  Flags           - ACPI_IGNORE_RETURN_VALUE to delete return
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method or return the current value of an
 *              ACPI namespace object.
 *
 * MUTEX:       Locks interpreter
 *
 ******************************************************************************/
acpi_status acpi_ns_evaluate(struct acpi_evaluate_info * info)
{
	acpi_status status;
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE(ns_evaluate);

	if (!info) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Initialize the return value to an invalid object */

	info->return_object = NULL;

	/*
	 * Get the actual namespace node for the target object. Handles these cases:
	 *
	 * 1) Null node, Pathname (absolute path)
	 * 2) Node, Pathname (path relative to Node)
	 * 3) Node, Null Pathname
	 */
	status = acpi_ns_get_node(info->prefix_node, info->pathname,
				  ACPI_NS_NO_UPSEARCH, &info->resolved_node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * For a method alias, we must grab the actual method node so that proper
	 * scoping context will be established before execution.
	 */
	if (acpi_ns_get_type(info->resolved_node) ==
	    ACPI_TYPE_LOCAL_METHOD_ALIAS) {
		info->resolved_node =
		    ACPI_CAST_PTR(struct acpi_namespace_node,
				  info->resolved_node->object);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES, "%s [%p] Value %p\n", info->pathname,
			  info->resolved_node,
			  acpi_ns_get_attached_object(info->resolved_node)));

	node = info->resolved_node;

	/*
	 * Two major cases here:
	 *
	 * 1) The object is a control method -- execute it
	 * 2) The object is not a method -- just return it's current value
	 */
	if (acpi_ns_get_type(info->resolved_node) == ACPI_TYPE_METHOD) {
		/*
		 * 1) Object is a control method - execute it
		 */

		/* Verify that there is a method object associated with this node */

		info->obj_desc =
		    acpi_ns_get_attached_object(info->resolved_node);
		if (!info->obj_desc) {
			ACPI_ERROR((AE_INFO,
				    "Control method has no attached sub-object"));
			return_ACPI_STATUS(AE_NULL_OBJECT);
		}

		/*
		 * Calculate the number of arguments being passed to the method
		 */

		info->param_count = 0;
		if (info->parameters) {
			while (info->parameters[info->param_count])
				info->param_count++;
		}

		/*
		 * Warning if too few or too many arguments have been passed by the
		 * caller. We don't want to abort here with an error because an
		 * incorrect number of arguments may not cause the method to fail.
		 * However, the method will fail if there are too few arguments passed
		 * and the method attempts to use one of the missing ones.
		 */

		if (info->param_count < info->obj_desc->method.param_count) {
			ACPI_WARNING((AE_INFO,
				    "Insufficient arguments - "
				    "method [%4.4s] needs %d, found %d",
				    acpi_ut_get_node_name(info->resolved_node),
				    info->obj_desc->method.param_count,
				    info->param_count));
		} else if (info->param_count >
				info->obj_desc->method.param_count) {
			ACPI_WARNING((AE_INFO,
				      "Excess arguments - "
				      "method [%4.4s] needs %d, found %d",
				      acpi_ut_get_node_name(info->
							    resolved_node),
				      info->obj_desc->method.param_count,
				      info->param_count));
		}

		ACPI_DUMP_PATHNAME(info->resolved_node, "Execute Method:",
				   ACPI_LV_INFO, _COMPONENT);

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Method at AML address %p Length %X\n",
				  info->obj_desc->method.aml_start + 1,
				  info->obj_desc->method.aml_length - 1));

		/*
		 * Any namespace deletion must acquire both the namespace and
		 * interpreter locks to ensure that no thread is using the portion of
		 * the namespace that is being deleted.
		 *
		 * Execute the method via the interpreter. The interpreter is locked
		 * here before calling into the AML parser
		 */
		acpi_ex_enter_interpreter();
		status = acpi_ps_execute_method(info);
		acpi_ex_exit_interpreter();
	} else {
		/*
		 * 2) Object is not a method, return its current value
		 *
		 * Disallow certain object types. For these, "evaluation" is undefined.
		 */
		switch (info->resolved_node->type) {
		case ACPI_TYPE_DEVICE:
		case ACPI_TYPE_EVENT:
		case ACPI_TYPE_MUTEX:
		case ACPI_TYPE_REGION:
		case ACPI_TYPE_THERMAL:
		case ACPI_TYPE_LOCAL_SCOPE:

			ACPI_ERROR((AE_INFO,
				    "[%4.4s] Evaluation of object type [%s] is not supported",
				    info->resolved_node->name.ascii,
				    acpi_ut_get_type_name(info->resolved_node->
							  type)));

			return_ACPI_STATUS(AE_TYPE);

		default:
			break;
		}

		/*
		 * Objects require additional resolution steps (e.g., the Node may be
		 * a field that must be read, etc.) -- we can't just grab the object
		 * out of the node.
		 *
		 * Use resolve_node_to_value() to get the associated value.
		 *
		 * NOTE: we can get away with passing in NULL for a walk state because
		 * resolved_node is guaranteed to not be a reference to either a method
		 * local or a method argument (because this interface is never called
		 * from a running method.)
		 *
		 * Even though we do not directly invoke the interpreter for object
		 * resolution, we must lock it because we could access an opregion.
		 * The opregion access code assumes that the interpreter is locked.
		 */
		acpi_ex_enter_interpreter();

		/* Function has a strange interface */

		status =
		    acpi_ex_resolve_node_to_value(&info->resolved_node, NULL);
		acpi_ex_exit_interpreter();

		/*
		 * If acpi_ex_resolve_node_to_value() succeeded, the return value was placed
		 * in resolved_node.
		 */
		if (ACPI_SUCCESS(status)) {
			status = AE_CTRL_RETURN_VALUE;
			info->return_object =
			    ACPI_CAST_PTR(union acpi_operand_object,
					  info->resolved_node);

			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "Returning object %p [%s]\n",
					  info->return_object,
					  acpi_ut_get_object_type_name(info->
								       return_object)));
		}
	}

	/* Validation of return values for ACPI-predefined methods and objects */

	if ((status == AE_OK) || (status == AE_CTRL_RETURN_VALUE)) {
		/*
		 * If this is the first evaluation, check the return value. This
		 * ensures that any warnings will only be emitted during the very
		 * first evaluation of the object.
		 */
		if (!(node->flags & ANOBJ_EVALUATED)) {
			/*
			 * Check for a predefined ACPI name. If found, validate the
			 * returned object.
			 *
			 * Note: Ignore return status for now, emit warnings if there are
			 * problems with the returned object. May change later to abort
			 * the method on invalid return object.
			 */
			(void)acpi_ns_check_predefined_names(node,
							     info->
							     return_object);
		}

		/* Mark the node as having been evaluated */

		node->flags |= ANOBJ_EVALUATED;
	}

	/* Check if there is a return value that must be dealt with */

	if (status == AE_CTRL_RETURN_VALUE) {

		/* If caller does not want the return value, delete it */

		if (info->flags & ACPI_IGNORE_RETURN_VALUE) {
			acpi_ut_remove_reference(info->return_object);
			info->return_object = NULL;
		}

		/* Map AE_CTRL_RETURN_VALUE to AE_OK, we are done with it */

		status = AE_OK;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
			  "*** Completed evaluation of object %s ***\n",
			  info->pathname));

	/*
	 * Namespace was unlocked by the handling acpi_ns* function, so we
	 * just return
	 */
	return_ACPI_STATUS(status);
}
