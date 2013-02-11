/******************************************************************************
 *
 * Module Name: nspredef - Validation of ACPI predefined methods and objects
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2013, Intel Corp.
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

#define ACPI_CREATE_PREDEFINED_TABLE

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "acpredef.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nspredef")

/*******************************************************************************
 *
 * This module validates predefined ACPI objects that appear in the namespace,
 * at the time they are evaluated (via acpi_evaluate_object). The purpose of this
 * validation is to detect problems with BIOS-exposed predefined ACPI objects
 * before the results are returned to the ACPI-related drivers.
 *
 * There are several areas that are validated:
 *
 *  1) The number of input arguments as defined by the method/object in the
 *      ASL is validated against the ACPI specification.
 *  2) The type of the return object (if any) is validated against the ACPI
 *      specification.
 *  3) For returned package objects, the count of package elements is
 *      validated, as well as the type of each package element. Nested
 *      packages are supported.
 *
 * For any problems found, a warning message is issued.
 *
 ******************************************************************************/
/* Local prototypes */
static acpi_status
acpi_ns_check_reference(struct acpi_predefined_data *data,
			union acpi_operand_object *return_object);

static void acpi_ns_get_expected_types(char *buffer, u32 expected_btypes);

/*
 * Names for the types that can be returned by the predefined objects.
 * Used for warning messages. Must be in the same order as the ACPI_RTYPEs
 */
static const char *acpi_rtype_names[] = {
	"/Integer",
	"/String",
	"/Buffer",
	"/Package",
	"/Reference",
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_predefined_names
 *
 * PARAMETERS:  node            - Namespace node for the method/object
 *              user_param_count - Number of parameters actually passed
 *              return_status   - Status from the object evaluation
 *              return_object_ptr - Pointer to the object returned from the
 *                                evaluation of a method or object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check an ACPI name for a match in the predefined name list.
 *
 ******************************************************************************/

acpi_status
acpi_ns_check_predefined_names(struct acpi_namespace_node *node,
			       u32 user_param_count,
			       acpi_status return_status,
			       union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	acpi_status status = AE_OK;
	const union acpi_predefined_info *predefined;
	char *pathname;
	struct acpi_predefined_data *data;

	/* Match the name for this method/object against the predefined list */

	predefined = acpi_ns_check_for_predefined_name(node);

	/* Get the full pathname to the object, for use in warning messages */

	pathname = acpi_ns_get_external_pathname(node);
	if (!pathname) {
		return (AE_OK);	/* Could not get pathname, ignore */
	}

	/*
	 * Check that the parameter count for this method matches the ASL
	 * definition. For predefined names, ensure that both the caller and
	 * the method itself are in accordance with the ACPI specification.
	 */
	acpi_ns_check_parameter_count(pathname, node, user_param_count,
				      predefined);

	/* If not a predefined name, we cannot validate the return object */

	if (!predefined) {
		goto cleanup;
	}

	/*
	 * If the method failed or did not actually return an object, we cannot
	 * validate the return object
	 */
	if ((return_status != AE_OK) && (return_status != AE_CTRL_RETURN_VALUE)) {
		goto cleanup;
	}

	/*
	 * If there is no return value, check if we require a return value for
	 * this predefined name. Either one return value is expected, or none,
	 * for both methods and other objects.
	 *
	 * Exit now if there is no return object. Warning if one was expected.
	 */
	if (!return_object) {
		if ((predefined->info.expected_btypes) &&
		    (!(predefined->info.expected_btypes & ACPI_RTYPE_NONE))) {
			ACPI_WARN_PREDEFINED((AE_INFO, pathname,
					      ACPI_WARN_ALWAYS,
					      "Missing expected return value"));

			status = AE_AML_NO_RETURN_VALUE;
		}
		goto cleanup;
	}

	/*
	 * Return value validation and possible repair.
	 *
	 * 1) Don't perform return value validation/repair if this feature
	 * has been disabled via a global option.
	 *
	 * 2) We have a return value, but if one wasn't expected, just exit,
	 * this is not a problem. For example, if the "Implicit Return"
	 * feature is enabled, methods will always return a value.
	 *
	 * 3) If the return value can be of any type, then we cannot perform
	 * any validation, just exit.
	 */
	if (acpi_gbl_disable_auto_repair ||
	    (!predefined->info.expected_btypes) ||
	    (predefined->info.expected_btypes == ACPI_RTYPE_ALL)) {
		goto cleanup;
	}

	/* Create the parameter data block for object validation */

	data = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_predefined_data));
	if (!data) {
		goto cleanup;
	}
	data->predefined = predefined;
	data->node = node;
	data->node_flags = node->flags;
	data->pathname = pathname;

	/*
	 * Check that the type of the main return object is what is expected
	 * for this predefined name
	 */
	status = acpi_ns_check_object_type(data, return_object_ptr,
					   predefined->info.expected_btypes,
					   ACPI_NOT_PACKAGE_ELEMENT);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/*
	 * For returned Package objects, check the type of all sub-objects.
	 * Note: Package may have been newly created by call above.
	 */
	if ((*return_object_ptr)->common.type == ACPI_TYPE_PACKAGE) {
		data->parent_package = *return_object_ptr;
		status = acpi_ns_check_package(data, return_object_ptr);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}
	}

	/*
	 * The return object was OK, or it was successfully repaired above.
	 * Now make some additional checks such as verifying that package
	 * objects are sorted correctly (if required) or buffer objects have
	 * the correct data width (bytes vs. dwords). These repairs are
	 * performed on a per-name basis, i.e., the code is specific to
	 * particular predefined names.
	 */
	status = acpi_ns_complex_repairs(data, node, status, return_object_ptr);

exit:
	/*
	 * If the object validation failed or if we successfully repaired one
	 * or more objects, mark the parent node to suppress further warning
	 * messages during the next evaluation of the same method/object.
	 */
	if (ACPI_FAILURE(status) || (data->flags & ACPI_OBJECT_REPAIRED)) {
		node->flags |= ANOBJ_EVALUATED;
	}
	ACPI_FREE(data);

cleanup:
	ACPI_FREE(pathname);
	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_parameter_count
 *
 * PARAMETERS:  pathname        - Full pathname to the node (for error msgs)
 *              node            - Namespace node for the method/object
 *              user_param_count - Number of args passed in by the caller
 *              predefined      - Pointer to entry in predefined name table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check that the declared (in ASL/AML) parameter count for a
 *              predefined name is what is expected (i.e., what is defined in
 *              the ACPI specification for this predefined name.)
 *
 ******************************************************************************/

void
acpi_ns_check_parameter_count(char *pathname,
			      struct acpi_namespace_node *node,
			      u32 user_param_count,
			      const union acpi_predefined_info *predefined)
{
	u32 param_count;
	u32 required_params_current;
	u32 required_params_old;

	/* Methods have 0-7 parameters. All other types have zero. */

	param_count = 0;
	if (node->type == ACPI_TYPE_METHOD) {
		param_count = node->object->method.param_count;
	}

	if (!predefined) {
		/*
		 * Check the parameter count for non-predefined methods/objects.
		 *
		 * Warning if too few or too many arguments have been passed by the
		 * caller. An incorrect number of arguments may not cause the method
		 * to fail. However, the method will fail if there are too few
		 * arguments and the method attempts to use one of the missing ones.
		 */
		if (user_param_count < param_count) {
			ACPI_WARN_PREDEFINED((AE_INFO, pathname,
					      ACPI_WARN_ALWAYS,
					      "Insufficient arguments - needs %u, found %u",
					      param_count, user_param_count));
		} else if (user_param_count > param_count) {
			ACPI_WARN_PREDEFINED((AE_INFO, pathname,
					      ACPI_WARN_ALWAYS,
					      "Excess arguments - needs %u, found %u",
					      param_count, user_param_count));
		}
		return;
	}

	/*
	 * Validate the user-supplied parameter count.
	 * Allow two different legal argument counts (_SCP, etc.)
	 */
	required_params_current = predefined->info.param_count & 0x0F;
	required_params_old = predefined->info.param_count >> 4;

	if (user_param_count != ACPI_UINT32_MAX) {
		if ((user_param_count != required_params_current) &&
		    (user_param_count != required_params_old)) {
			ACPI_WARN_PREDEFINED((AE_INFO, pathname,
					      ACPI_WARN_ALWAYS,
					      "Parameter count mismatch - "
					      "caller passed %u, ACPI requires %u",
					      user_param_count,
					      required_params_current));
		}
	}

	/*
	 * Check that the ASL-defined parameter count is what is expected for
	 * this predefined name (parameter count as defined by the ACPI
	 * specification)
	 */
	if ((param_count != required_params_current) &&
	    (param_count != required_params_old)) {
		ACPI_WARN_PREDEFINED((AE_INFO, pathname, node->flags,
				      "Parameter count mismatch - ASL declared %u, ACPI requires %u",
				      param_count, required_params_current));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_for_predefined_name
 *
 * PARAMETERS:  node            - Namespace node for the method/object
 *
 * RETURN:      Pointer to entry in predefined table. NULL indicates not found.
 *
 * DESCRIPTION: Check an object name against the predefined object list.
 *
 ******************************************************************************/

const union acpi_predefined_info *acpi_ns_check_for_predefined_name(struct
								    acpi_namespace_node
								    *node)
{
	const union acpi_predefined_info *this_name;

	/* Quick check for a predefined name, first character must be underscore */

	if (node->name.ascii[0] != '_') {
		return (NULL);
	}

	/* Search info table for a predefined method/object name */

	this_name = predefined_names;
	while (this_name->info.name[0]) {
		if (ACPI_COMPARE_NAME(node->name.ascii, this_name->info.name)) {
			return (this_name);
		}

		/*
		 * Skip next entry in the table if this name returns a Package
		 * (next entry contains the package info)
		 */
		if (this_name->info.expected_btypes & ACPI_RTYPE_PACKAGE) {
			this_name++;
		}

		this_name++;
	}

	return (NULL);		/* Not found */
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_object_type
 *
 * PARAMETERS:  data            - Pointer to validation data structure
 *              return_object_ptr - Pointer to the object returned from the
 *                                evaluation of a method or object
 *              expected_btypes - Bitmap of expected return type(s)
 *              package_index   - Index of object within parent package (if
 *                                applicable - ACPI_NOT_PACKAGE_ELEMENT
 *                                otherwise)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check the type of the return object against the expected object
 *              type(s). Use of Btype allows multiple expected object types.
 *
 ******************************************************************************/

acpi_status
acpi_ns_check_object_type(struct acpi_predefined_data *data,
			  union acpi_operand_object **return_object_ptr,
			  u32 expected_btypes, u32 package_index)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	acpi_status status = AE_OK;
	u32 return_btype;
	char type_buffer[48];	/* Room for 5 types */

	/*
	 * If we get a NULL return_object here, it is a NULL package element.
	 * Since all extraneous NULL package elements were removed earlier by a
	 * call to acpi_ns_remove_null_elements, this is an unexpected NULL element.
	 * We will attempt to repair it.
	 */
	if (!return_object) {
		status = acpi_ns_repair_null_element(data, expected_btypes,
						     package_index,
						     return_object_ptr);
		if (ACPI_SUCCESS(status)) {
			return (AE_OK);	/* Repair was successful */
		}
		goto type_error_exit;
	}

	/* A Namespace node should not get here, but make sure */

	if (ACPI_GET_DESCRIPTOR_TYPE(return_object) == ACPI_DESC_TYPE_NAMED) {
		ACPI_WARN_PREDEFINED((AE_INFO, data->pathname, data->node_flags,
				      "Invalid return type - Found a Namespace node [%4.4s] type %s",
				      return_object->node.name.ascii,
				      acpi_ut_get_type_name(return_object->node.
							    type)));
		return (AE_AML_OPERAND_TYPE);
	}

	/*
	 * Convert the object type (ACPI_TYPE_xxx) to a bitmapped object type.
	 * The bitmapped type allows multiple possible return types.
	 *
	 * Note, the cases below must handle all of the possible types returned
	 * from all of the predefined names (including elements of returned
	 * packages)
	 */
	switch (return_object->common.type) {
	case ACPI_TYPE_INTEGER:
		return_btype = ACPI_RTYPE_INTEGER;
		break;

	case ACPI_TYPE_BUFFER:
		return_btype = ACPI_RTYPE_BUFFER;
		break;

	case ACPI_TYPE_STRING:
		return_btype = ACPI_RTYPE_STRING;
		break;

	case ACPI_TYPE_PACKAGE:
		return_btype = ACPI_RTYPE_PACKAGE;
		break;

	case ACPI_TYPE_LOCAL_REFERENCE:
		return_btype = ACPI_RTYPE_REFERENCE;
		break;

	default:
		/* Not one of the supported objects, must be incorrect */

		goto type_error_exit;
	}

	/* Is the object one of the expected types? */

	if (return_btype & expected_btypes) {

		/* For reference objects, check that the reference type is correct */

		if (return_object->common.type == ACPI_TYPE_LOCAL_REFERENCE) {
			status = acpi_ns_check_reference(data, return_object);
		}

		return (status);
	}

	/* Type mismatch -- attempt repair of the returned object */

	status = acpi_ns_repair_object(data, expected_btypes,
				       package_index, return_object_ptr);
	if (ACPI_SUCCESS(status)) {
		return (AE_OK);	/* Repair was successful */
	}

      type_error_exit:

	/* Create a string with all expected types for this predefined object */

	acpi_ns_get_expected_types(type_buffer, expected_btypes);

	if (package_index == ACPI_NOT_PACKAGE_ELEMENT) {
		ACPI_WARN_PREDEFINED((AE_INFO, data->pathname, data->node_flags,
				      "Return type mismatch - found %s, expected %s",
				      acpi_ut_get_object_type_name
				      (return_object), type_buffer));
	} else {
		ACPI_WARN_PREDEFINED((AE_INFO, data->pathname, data->node_flags,
				      "Return Package type mismatch at index %u - "
				      "found %s, expected %s", package_index,
				      acpi_ut_get_object_type_name
				      (return_object), type_buffer));
	}

	return (AE_AML_OPERAND_TYPE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_reference
 *
 * PARAMETERS:  data            - Pointer to validation data structure
 *              return_object   - Object returned from the evaluation of a
 *                                method or object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check a returned reference object for the correct reference
 *              type. The only reference type that can be returned from a
 *              predefined method is a named reference. All others are invalid.
 *
 ******************************************************************************/

static acpi_status
acpi_ns_check_reference(struct acpi_predefined_data *data,
			union acpi_operand_object *return_object)
{

	/*
	 * Check the reference object for the correct reference type (opcode).
	 * The only type of reference that can be converted to an union acpi_object is
	 * a reference to a named object (reference class: NAME)
	 */
	if (return_object->reference.class == ACPI_REFCLASS_NAME) {
		return (AE_OK);
	}

	ACPI_WARN_PREDEFINED((AE_INFO, data->pathname, data->node_flags,
			      "Return type mismatch - unexpected reference object type [%s] %2.2X",
			      acpi_ut_get_reference_name(return_object),
			      return_object->reference.class));

	return (AE_AML_OPERAND_TYPE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_expected_types
 *
 * PARAMETERS:  buffer          - Pointer to where the string is returned
 *              expected_btypes - Bitmap of expected return type(s)
 *
 * RETURN:      Buffer is populated with type names.
 *
 * DESCRIPTION: Translate the expected types bitmap into a string of ascii
 *              names of expected types, for use in warning messages.
 *
 ******************************************************************************/

static void acpi_ns_get_expected_types(char *buffer, u32 expected_btypes)
{
	u32 this_rtype;
	u32 i;
	u32 j;

	j = 1;
	buffer[0] = 0;
	this_rtype = ACPI_RTYPE_INTEGER;

	for (i = 0; i < ACPI_NUM_RTYPES; i++) {

		/* If one of the expected types, concatenate the name of this type */

		if (expected_btypes & this_rtype) {
			ACPI_STRCAT(buffer, &acpi_rtype_names[i][j]);
			j = 0;	/* Use name separator from now on */
		}
		this_rtype <<= 1;	/* Next Rtype */
	}
}
