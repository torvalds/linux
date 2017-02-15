/******************************************************************************
 *
 * Module Name: nsarguments - Validation of args for ACPI predefined methods
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
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
#include "acnamesp.h"
#include "acpredef.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsarguments")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_argument_types
 *
 * PARAMETERS:  info            - Method execution information block
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check the incoming argument count and all argument types
 *              against the argument type list for a predefined name.
 *
 ******************************************************************************/
void acpi_ns_check_argument_types(struct acpi_evaluate_info *info)
{
	u16 arg_type_list;
	u8 arg_count;
	u8 arg_type;
	u8 user_arg_type;
	u32 i;

	/* If not a predefined name, cannot typecheck args */

	if (!info->predefined) {
		return;
	}

	arg_type_list = info->predefined->info.argument_list;
	arg_count = METHOD_GET_ARG_COUNT(arg_type_list);

	/* Typecheck all arguments */

	for (i = 0; ((i < arg_count) && (i < info->param_count)); i++) {
		arg_type = METHOD_GET_NEXT_TYPE(arg_type_list);
		user_arg_type = info->parameters[i]->common.type;

		if (user_arg_type != arg_type) {
			ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
					      ACPI_WARN_ALWAYS,
					      "Argument #%u type mismatch - "
					      "Found [%s], ACPI requires [%s]",
					      (i + 1),
					      acpi_ut_get_type_name
					      (user_arg_type),
					      acpi_ut_get_type_name(arg_type)));
		}
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_acpi_compliance
 *
 * PARAMETERS:  pathname        - Full pathname to the node (for error msgs)
 *              node            - Namespace node for the method/object
 *              predefined      - Pointer to entry in predefined name table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check that the declared parameter count (in ASL/AML) for a
 *              predefined name is what is expected (matches what is defined in
 *              the ACPI specification for this predefined name.)
 *
 ******************************************************************************/

void
acpi_ns_check_acpi_compliance(char *pathname,
			      struct acpi_namespace_node *node,
			      const union acpi_predefined_info *predefined)
{
	u32 aml_param_count;
	u32 required_param_count;

	if (!predefined) {
		return;
	}

	/* Get the ACPI-required arg count from the predefined info table */

	required_param_count =
	    METHOD_GET_ARG_COUNT(predefined->info.argument_list);

	/*
	 * If this object is not a control method, we can check if the ACPI
	 * spec requires that it be a method.
	 */
	if (node->type != ACPI_TYPE_METHOD) {
		if (required_param_count > 0) {

			/* Object requires args, must be implemented as a method */

			ACPI_BIOS_ERROR_PREDEFINED((AE_INFO, pathname,
						    ACPI_WARN_ALWAYS,
						    "Object (%s) must be a control method with %u arguments",
						    acpi_ut_get_type_name(node->
									  type),
						    required_param_count));
		} else if (!required_param_count
			   && !predefined->info.expected_btypes) {

			/* Object requires no args and no return value, must be a method */

			ACPI_BIOS_ERROR_PREDEFINED((AE_INFO, pathname,
						    ACPI_WARN_ALWAYS,
						    "Object (%s) must be a control method "
						    "with no arguments and no return value",
						    acpi_ut_get_type_name(node->
									  type)));
		}

		return;
	}

	/*
	 * This is a control method.
	 * Check that the ASL/AML-defined parameter count for this method
	 * matches the ACPI-required parameter count
	 *
	 * Some methods are allowed to have a "minimum" number of args (_SCP)
	 * because their definition in ACPI has changed over time.
	 *
	 * Note: These are BIOS errors in the declaration of the object
	 */
	aml_param_count = node->object->method.param_count;

	if (aml_param_count < required_param_count) {
		ACPI_BIOS_ERROR_PREDEFINED((AE_INFO, pathname, ACPI_WARN_ALWAYS,
					    "Insufficient arguments - "
					    "ASL declared %u, ACPI requires %u",
					    aml_param_count,
					    required_param_count));
	} else if ((aml_param_count > required_param_count)
		   && !(predefined->info.
			argument_list & ARG_COUNT_IS_MINIMUM)) {
		ACPI_BIOS_ERROR_PREDEFINED((AE_INFO, pathname, ACPI_WARN_ALWAYS,
					    "Excess arguments - "
					    "ASL declared %u, ACPI requires %u",
					    aml_param_count,
					    required_param_count));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_argument_count
 *
 * PARAMETERS:  pathname        - Full pathname to the node (for error msgs)
 *              node            - Namespace node for the method/object
 *              user_param_count - Number of args passed in by the caller
 *              predefined      - Pointer to entry in predefined name table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check that incoming argument count matches the declared
 *              parameter count (in the ASL/AML) for an object.
 *
 ******************************************************************************/

void
acpi_ns_check_argument_count(char *pathname,
			     struct acpi_namespace_node *node,
			     u32 user_param_count,
			     const union acpi_predefined_info *predefined)
{
	u32 aml_param_count;
	u32 required_param_count;

	if (!predefined) {
		/*
		 * Not a predefined name. Check the incoming user argument count
		 * against the count that is specified in the method/object.
		 */
		if (node->type != ACPI_TYPE_METHOD) {
			if (user_param_count) {
				ACPI_INFO_PREDEFINED((AE_INFO, pathname,
						      ACPI_WARN_ALWAYS,
						      "%u arguments were passed to a non-method ACPI object (%s)",
						      user_param_count,
						      acpi_ut_get_type_name
						      (node->type)));
			}

			return;
		}

		/*
		 * This is a control method. Check the parameter count.
		 * We can only check the incoming argument count against the
		 * argument count declared for the method in the ASL/AML.
		 *
		 * Emit a message if too few or too many arguments have been passed
		 * by the caller.
		 *
		 * Note: Too many arguments will not cause the method to
		 * fail. However, the method will fail if there are too few
		 * arguments and the method attempts to use one of the missing ones.
		 */
		aml_param_count = node->object->method.param_count;

		if (user_param_count < aml_param_count) {
			ACPI_WARN_PREDEFINED((AE_INFO, pathname,
					      ACPI_WARN_ALWAYS,
					      "Insufficient arguments - "
					      "Caller passed %u, method requires %u",
					      user_param_count,
					      aml_param_count));
		} else if (user_param_count > aml_param_count) {
			ACPI_INFO_PREDEFINED((AE_INFO, pathname,
					      ACPI_WARN_ALWAYS,
					      "Excess arguments - "
					      "Caller passed %u, method requires %u",
					      user_param_count,
					      aml_param_count));
		}

		return;
	}

	/*
	 * This is a predefined name. Validate the user-supplied parameter
	 * count against the ACPI specification. We don't validate against
	 * the method itself because what is important here is that the
	 * caller is in conformance with the spec. (The arg count for the
	 * method was checked against the ACPI spec earlier.)
	 *
	 * Some methods are allowed to have a "minimum" number of args (_SCP)
	 * because their definition in ACPI has changed over time.
	 */
	required_param_count =
	    METHOD_GET_ARG_COUNT(predefined->info.argument_list);

	if (user_param_count < required_param_count) {
		ACPI_WARN_PREDEFINED((AE_INFO, pathname, ACPI_WARN_ALWAYS,
				      "Insufficient arguments - "
				      "Caller passed %u, ACPI requires %u",
				      user_param_count, required_param_count));
	} else if ((user_param_count > required_param_count) &&
		   !(predefined->info.argument_list & ARG_COUNT_IS_MINIMUM)) {
		ACPI_INFO_PREDEFINED((AE_INFO, pathname, ACPI_WARN_ALWAYS,
				      "Excess arguments - "
				      "Caller passed %u, ACPI requires %u",
				      user_param_count, required_param_count));
	}
}
