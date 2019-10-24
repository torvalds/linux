// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: nseval - Object evaluation, includes control method execution
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acparser.h"
#include "acinterp.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nseval")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_evaluate
 *
 * PARAMETERS:  info            - Evaluation info block, contains these fields
 *                                and more:
 *                  prefix_node     - Prefix or Method/Object Node to execute
 *                  relative_path   - Name of method to execute, If NULL, the
 *                                    Node is the object to execute
 *                  parameters      - List of parameters to pass to the method,
 *                                    terminated by NULL. Params itself may be
 *                                    NULL if no parameters are being passed.
 *                  parameter_type  - Type of Parameter list
 *                  return_object   - Where to put method's return value (if
 *                                    any). If NULL, no value is returned.
 *                  flags           - ACPI_IGNORE_RETURN_VALUE to delete return
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method or return the current value of an
 *              ACPI namespace object.
 *
 * MUTEX:       Locks interpreter
 *
 ******************************************************************************/
acpi_status acpi_ns_evaluate(struct acpi_evaluate_info *info)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ns_evaluate);

	if (!info) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (!info->node) {
		/*
		 * Get the actual namespace node for the target object if we
		 * need to. Handles these cases:
		 *
		 * 1) Null node, valid pathname from root (absolute path)
		 * 2) Node and valid pathname (path relative to Node)
		 * 3) Node, Null pathname
		 */
		status =
		    acpi_ns_get_node(info->prefix_node, info->relative_pathname,
				     ACPI_NS_NO_UPSEARCH, &info->node);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/*
	 * For a method alias, we must grab the actual method node so that
	 * proper scoping context will be established before execution.
	 */
	if (acpi_ns_get_type(info->node) == ACPI_TYPE_LOCAL_METHOD_ALIAS) {
		info->node =
		    ACPI_CAST_PTR(struct acpi_namespace_node,
				  info->node->object);
	}

	/* Complete the info block initialization */

	info->return_object = NULL;
	info->node_flags = info->node->flags;
	info->obj_desc = acpi_ns_get_attached_object(info->node);

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES, "%s [%p] Value %p\n",
			  info->relative_pathname, info->node,
			  acpi_ns_get_attached_object(info->node)));

	/* Get info if we have a predefined name (_HID, etc.) */

	info->predefined =
	    acpi_ut_match_predefined_method(info->node->name.ascii);

	/* Get the full pathname to the object, for use in warning messages */

	info->full_pathname = acpi_ns_get_normalized_pathname(info->node, TRUE);
	if (!info->full_pathname) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Optional object evaluation log */

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_EVALUATION,
			      "%-26s:  %s (%s)\n", "   Enter evaluation",
			      &info->full_pathname[1],
			      acpi_ut_get_type_name(info->node->type)));

	/* Count the number of arguments being passed in */

	info->param_count = 0;
	if (info->parameters) {
		while (info->parameters[info->param_count]) {
			info->param_count++;
		}

		/* Warn on impossible argument count */

		if (info->param_count > ACPI_METHOD_NUM_ARGS) {
			ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
					      ACPI_WARN_ALWAYS,
					      "Excess arguments (%u) - using only %u",
					      info->param_count,
					      ACPI_METHOD_NUM_ARGS));

			info->param_count = ACPI_METHOD_NUM_ARGS;
		}
	}

	/*
	 * For predefined names: Check that the declared argument count
	 * matches the ACPI spec -- otherwise this is a BIOS error.
	 */
	acpi_ns_check_acpi_compliance(info->full_pathname, info->node,
				      info->predefined);

	/*
	 * For all names: Check that the incoming argument count for
	 * this method/object matches the actual ASL/AML definition.
	 */
	acpi_ns_check_argument_count(info->full_pathname, info->node,
				     info->param_count, info->predefined);

	/* For predefined names: Typecheck all incoming arguments */

	acpi_ns_check_argument_types(info);

	/*
	 * Three major evaluation cases:
	 *
	 * 1) Object types that cannot be evaluated by definition
	 * 2) The object is a control method -- execute it
	 * 3) The object is not a method -- just return it's current value
	 */
	switch (acpi_ns_get_type(info->node)) {
	case ACPI_TYPE_ANY:
	case ACPI_TYPE_DEVICE:
	case ACPI_TYPE_EVENT:
	case ACPI_TYPE_MUTEX:
	case ACPI_TYPE_REGION:
	case ACPI_TYPE_THERMAL:
	case ACPI_TYPE_LOCAL_SCOPE:
		/*
		 * 1) Disallow evaluation of these object types. For these,
		 *    object evaluation is undefined.
		 */
		ACPI_ERROR((AE_INFO,
			    "%s: This object type [%s] "
			    "never contains data and cannot be evaluated",
			    info->full_pathname,
			    acpi_ut_get_type_name(info->node->type)));

		status = AE_TYPE;
		goto cleanup;

	case ACPI_TYPE_METHOD:
		/*
		 * 2) Object is a control method - execute it
		 */

		/* Verify that there is a method object associated with this node */

		if (!info->obj_desc) {
			ACPI_ERROR((AE_INFO,
				    "%s: Method has no attached sub-object",
				    info->full_pathname));
			status = AE_NULL_OBJECT;
			goto cleanup;
		}

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "**** Execute method [%s] at AML address %p length %X\n",
				  info->full_pathname,
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
		break;

	default:
		/*
		 * 3) All other non-method objects -- get the current object value
		 */

		/*
		 * Some objects require additional resolution steps (e.g., the Node
		 * may be a field that must be read, etc.) -- we can't just grab
		 * the object out of the node.
		 *
		 * Use resolve_node_to_value() to get the associated value.
		 *
		 * NOTE: we can get away with passing in NULL for a walk state because
		 * the Node is guaranteed to not be a reference to either a method
		 * local or a method argument (because this interface is never called
		 * from a running method.)
		 *
		 * Even though we do not directly invoke the interpreter for object
		 * resolution, we must lock it because we could access an op_region.
		 * The op_region access code assumes that the interpreter is locked.
		 */
		acpi_ex_enter_interpreter();

		/* TBD: resolve_node_to_value has a strange interface, fix */

		info->return_object =
		    ACPI_CAST_PTR(union acpi_operand_object, info->node);

		status =
		    acpi_ex_resolve_node_to_value(ACPI_CAST_INDIRECT_PTR
						  (struct acpi_namespace_node,
						   &info->return_object), NULL);
		acpi_ex_exit_interpreter();

		if (ACPI_FAILURE(status)) {
			info->return_object = NULL;
			goto cleanup;
		}

		ACPI_DEBUG_PRINT((ACPI_DB_NAMES, "Returned object %p [%s]\n",
				  info->return_object,
				  acpi_ut_get_object_type_name(info->
							       return_object)));

		status = AE_CTRL_RETURN_VALUE;	/* Always has a "return value" */
		break;
	}

	/*
	 * For predefined names, check the return value against the ACPI
	 * specification. Some incorrect return value types are repaired.
	 */
	(void)acpi_ns_check_return_value(info->node, info, info->param_count,
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
	} else if (ACPI_FAILURE(status)) {

		/* If return_object exists, delete it */

		if (info->return_object) {
			acpi_ut_remove_reference(info->return_object);
			info->return_object = NULL;
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
			  "*** Completed evaluation of object %s ***\n",
			  info->relative_pathname));

cleanup:
	/* Optional object evaluation log */

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_EVALUATION,
			      "%-26s:  %s\n", "   Exit evaluation",
			      &info->full_pathname[1]));

	/*
	 * Namespace was unlocked by the handling acpi_ns* function, so we
	 * just free the pathname and return
	 */
	ACPI_FREE(info->full_pathname);
	info->full_pathname = NULL;
	return_ACPI_STATUS(status);
}
