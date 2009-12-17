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
#include "accommon.h"
#include "acparser.h"
#include "acinterp.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nseval")

/* Local prototypes */
static void
acpi_ns_exec_module_code(union acpi_operand_object *method_obj,
			 struct acpi_evaluate_info *info);

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
	info->param_count = 0;

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

		/* Count the number of arguments being passed to the method */

		if (info->parameters) {
			while (info->parameters[info->param_count]) {
				if (info->param_count > ACPI_METHOD_MAX_ARG) {
					return_ACPI_STATUS(AE_LIMIT);
				}
				info->param_count++;
			}
		}


		ACPI_DUMP_PATHNAME(info->resolved_node, "ACPI: Execute Method",
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

	/*
	 * Check input argument count against the ASL-defined count for a method.
	 * Also check predefined names: argument count and return value against
	 * the ACPI specification. Some incorrect return value types are repaired.
	 */
	(void)acpi_ns_check_predefined_names(node, info->param_count,
		status, &info->return_object);

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

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_exec_module_code_list
 *
 * PARAMETERS:  None
 *
 * RETURN:      None. Exceptions during method execution are ignored, since
 *              we cannot abort a table load.
 *
 * DESCRIPTION: Execute all elements of the global module-level code list.
 *              Each element is executed as a single control method.
 *
 ******************************************************************************/

void acpi_ns_exec_module_code_list(void)
{
	union acpi_operand_object *prev;
	union acpi_operand_object *next;
	struct acpi_evaluate_info *info;
	u32 method_count = 0;

	ACPI_FUNCTION_TRACE(ns_exec_module_code_list);

	/* Exit now if the list is empty */

	next = acpi_gbl_module_code_list;
	if (!next) {
		return_VOID;
	}

	/* Allocate the evaluation information block */

	info = ACPI_ALLOCATE(sizeof(struct acpi_evaluate_info));
	if (!info) {
		return_VOID;
	}

	/* Walk the list, executing each "method" */

	while (next) {
		prev = next;
		next = next->method.mutex;

		/* Clear the link field and execute the method */

		prev->method.mutex = NULL;
		acpi_ns_exec_module_code(prev, info);
		method_count++;

		/* Delete the (temporary) method object */

		acpi_ut_remove_reference(prev);
	}

	ACPI_INFO((AE_INFO,
		   "Executed %u blocks of module-level executable AML code",
		   method_count));

	ACPI_FREE(info);
	acpi_gbl_module_code_list = NULL;
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_exec_module_code
 *
 * PARAMETERS:  method_obj          - Object container for the module-level code
 *              Info                - Info block for method evaluation
 *
 * RETURN:      None. Exceptions during method execution are ignored, since
 *              we cannot abort a table load.
 *
 * DESCRIPTION: Execute a control method containing a block of module-level
 *              executable AML code. The control method is temporarily
 *              installed to the root node, then evaluated.
 *
 ******************************************************************************/

static void
acpi_ns_exec_module_code(union acpi_operand_object *method_obj,
			 struct acpi_evaluate_info *info)
{
	union acpi_operand_object *parent_obj;
	struct acpi_namespace_node *parent_node;
	acpi_object_type type;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ns_exec_module_code);

	/*
	 * Get the parent node. We cheat by using the next_object field
	 * of the method object descriptor.
	 */
	parent_node = ACPI_CAST_PTR(struct acpi_namespace_node,
				    method_obj->method.next_object);
	type = acpi_ns_get_type(parent_node);

	/*
	 * Get the region handler and save it in the method object. We may need
	 * this if an operation region declaration causes a _REG method to be run.
	 *
	 * We can't do this in acpi_ps_link_module_code because
	 * acpi_gbl_root_node->Object is NULL at PASS1.
	 */
	if ((type == ACPI_TYPE_DEVICE) && parent_node->object) {
		method_obj->method.extra.handler =
		    parent_node->object->device.handler;
	}

	/* Must clear next_object (acpi_ns_attach_object needs the field) */

	method_obj->method.next_object = NULL;

	/* Initialize the evaluation information block */

	ACPI_MEMSET(info, 0, sizeof(struct acpi_evaluate_info));
	info->prefix_node = parent_node;

	/*
	 * Get the currently attached parent object. Add a reference, because the
	 * ref count will be decreased when the method object is installed to
	 * the parent node.
	 */
	parent_obj = acpi_ns_get_attached_object(parent_node);
	if (parent_obj) {
		acpi_ut_add_reference(parent_obj);
	}

	/* Install the method (module-level code) in the parent node */

	status = acpi_ns_attach_object(parent_node, method_obj,
				       ACPI_TYPE_METHOD);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Execute the parent node as a control method */

	status = acpi_ns_evaluate(info);

	ACPI_DEBUG_PRINT((ACPI_DB_INIT, "Executed module-level code at %p\n",
			  method_obj->method.aml_start));

	/* Delete a possible implicit return value (in slack mode) */

	if (info->return_object) {
		acpi_ut_remove_reference(info->return_object);
	}

	/* Detach the temporary method object */

	acpi_ns_detach_object(parent_node);

	/* Restore the original parent object */

	if (parent_obj) {
		status = acpi_ns_attach_object(parent_node, parent_obj, type);
	} else {
		parent_node->type = (u8)type;
	}

      exit:
	if (parent_obj) {
		acpi_ut_remove_reference(parent_obj);
	}
	return_VOID;
}
