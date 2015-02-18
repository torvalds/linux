/******************************************************************************
 *
 * Module Name: utpredef - support functions for predefined names
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
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
#include "acpredef.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utpredef")

/*
 * Names for the types that can be returned by the predefined objects.
 * Used for warning messages. Must be in the same order as the ACPI_RTYPEs
 */
static const char *ut_rtype_names[] = {
	"/Integer",
	"/String",
	"/Buffer",
	"/Package",
	"/Reference",
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_next_predefined_method
 *
 * PARAMETERS:  this_name           - Entry in the predefined method/name table
 *
 * RETURN:      Pointer to next entry in predefined table.
 *
 * DESCRIPTION: Get the next entry in the predefine method table. Handles the
 *              cases where a package info entry follows a method name that
 *              returns a package.
 *
 ******************************************************************************/

const union acpi_predefined_info *acpi_ut_get_next_predefined_method(const union
								     acpi_predefined_info
								     *this_name)
{

	/*
	 * Skip next entry in the table if this name returns a Package
	 * (next entry contains the package info)
	 */
	if ((this_name->info.expected_btypes & ACPI_RTYPE_PACKAGE) &&
	    (this_name->info.expected_btypes != ACPI_RTYPE_ALL)) {
		this_name++;
	}

	this_name++;
	return (this_name);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_match_predefined_method
 *
 * PARAMETERS:  name                - Name to find
 *
 * RETURN:      Pointer to entry in predefined table. NULL indicates not found.
 *
 * DESCRIPTION: Check an object name against the predefined object list.
 *
 ******************************************************************************/

const union acpi_predefined_info *acpi_ut_match_predefined_method(char *name)
{
	const union acpi_predefined_info *this_name;

	/* Quick check for a predefined name, first character must be underscore */

	if (name[0] != '_') {
		return (NULL);
	}

	/* Search info table for a predefined method/object name */

	this_name = acpi_gbl_predefined_methods;
	while (this_name->info.name[0]) {
		if (ACPI_COMPARE_NAME(name, this_name->info.name)) {
			return (this_name);
		}

		this_name = acpi_ut_get_next_predefined_method(this_name);
	}

	return (NULL);		/* Not found */
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_expected_return_types
 *
 * PARAMETERS:  buffer              - Where the formatted string is returned
 *              expected_Btypes     - Bitfield of expected data types
 *
 * RETURN:      Formatted string in Buffer.
 *
 * DESCRIPTION: Format the expected object types into a printable string.
 *
 ******************************************************************************/

void acpi_ut_get_expected_return_types(char *buffer, u32 expected_btypes)
{
	u32 this_rtype;
	u32 i;
	u32 j;

	if (!expected_btypes) {
		ACPI_STRCPY(buffer, "NONE");
		return;
	}

	j = 1;
	buffer[0] = 0;
	this_rtype = ACPI_RTYPE_INTEGER;

	for (i = 0; i < ACPI_NUM_RTYPES; i++) {

		/* If one of the expected types, concatenate the name of this type */

		if (expected_btypes & this_rtype) {
			ACPI_STRCAT(buffer, &ut_rtype_names[i][j]);
			j = 0;	/* Use name separator from now on */
		}

		this_rtype <<= 1;	/* Next Rtype */
	}
}

/*******************************************************************************
 *
 * The remaining functions are used by iASL and acpi_help only
 *
 ******************************************************************************/

#if (defined ACPI_ASL_COMPILER || defined ACPI_HELP_APP)
#include <stdio.h>
#include <string.h>

/* Local prototypes */

static u32 acpi_ut_get_argument_types(char *buffer, u16 argument_types);

/* Types that can be returned externally by a predefined name */

static const char *ut_external_type_names[] =	/* Indexed by ACPI_TYPE_* */
{
	", UNSUPPORTED-TYPE",
	", Integer",
	", String",
	", Buffer",
	", Package"
};

/* Bit widths for resource descriptor predefined names */

static const char *ut_resource_type_names[] = {
	"/1",
	"/2",
	"/3",
	"/8",
	"/16",
	"/32",
	"/64",
	"/variable",
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_match_resource_name
 *
 * PARAMETERS:  name                - Name to find
 *
 * RETURN:      Pointer to entry in the resource table. NULL indicates not
 *              found.
 *
 * DESCRIPTION: Check an object name against the predefined resource
 *              descriptor object list.
 *
 ******************************************************************************/

const union acpi_predefined_info *acpi_ut_match_resource_name(char *name)
{
	const union acpi_predefined_info *this_name;

	/* Quick check for a predefined name, first character must be underscore */

	if (name[0] != '_') {
		return (NULL);
	}

	/* Search info table for a predefined method/object name */

	this_name = acpi_gbl_resource_names;
	while (this_name->info.name[0]) {
		if (ACPI_COMPARE_NAME(name, this_name->info.name)) {
			return (this_name);
		}

		this_name++;
	}

	return (NULL);		/* Not found */
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_display_predefined_method
 *
 * PARAMETERS:  buffer              - Scratch buffer for this function
 *              this_name           - Entry in the predefined method/name table
 *              multi_line          - TRUE if output should be on >1 line
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display information about a predefined method. Number and
 *              type of the input arguments, and expected type(s) for the
 *              return value, if any.
 *
 ******************************************************************************/

void
acpi_ut_display_predefined_method(char *buffer,
				  const union acpi_predefined_info *this_name,
				  u8 multi_line)
{
	u32 arg_count;

	/*
	 * Get the argument count and the string buffer
	 * containing all argument types
	 */
	arg_count = acpi_ut_get_argument_types(buffer,
					       this_name->info.argument_list);

	if (multi_line) {
		printf("      ");
	}

	printf("%4.4s    Requires %s%u argument%s",
	       this_name->info.name,
	       (this_name->info.argument_list & ARG_COUNT_IS_MINIMUM) ?
	       "(at least) " : "", arg_count, arg_count != 1 ? "s" : "");

	/* Display the types for any arguments */

	if (arg_count > 0) {
		printf(" (%s)", buffer);
	}

	if (multi_line) {
		printf("\n    ");
	}

	/* Get the return value type(s) allowed */

	if (this_name->info.expected_btypes) {
		acpi_ut_get_expected_return_types(buffer,
						  this_name->info.
						  expected_btypes);
		printf("  Return value types: %s\n", buffer);
	} else {
		printf("  No return value\n");
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_argument_types
 *
 * PARAMETERS:  buffer              - Where to return the formatted types
 *              argument_types      - Types field for this method
 *
 * RETURN:      count - the number of arguments required for this method
 *
 * DESCRIPTION: Format the required data types for this method (Integer,
 *              String, Buffer, or Package) and return the required argument
 *              count.
 *
 ******************************************************************************/

static u32 acpi_ut_get_argument_types(char *buffer, u16 argument_types)
{
	u16 this_argument_type;
	u16 sub_index;
	u16 arg_count;
	u32 i;

	*buffer = 0;
	sub_index = 2;

	/* First field in the types list is the count of args to follow */

	arg_count = METHOD_GET_ARG_COUNT(argument_types);
	if (arg_count > METHOD_PREDEF_ARGS_MAX) {
		printf("**** Invalid argument count (%u) "
		       "in predefined info structure\n", arg_count);
		return (arg_count);
	}

	/* Get each argument from the list, convert to ascii, store to buffer */

	for (i = 0; i < arg_count; i++) {
		this_argument_type = METHOD_GET_NEXT_TYPE(argument_types);

		if (!this_argument_type
		    || (this_argument_type > METHOD_MAX_ARG_TYPE)) {
			printf("**** Invalid argument type (%u) "
			       "in predefined info structure\n",
			       this_argument_type);
			return (arg_count);
		}

		strcat(buffer,
		       ut_external_type_names[this_argument_type] + sub_index);
		sub_index = 0;
	}

	return (arg_count);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_resource_bit_width
 *
 * PARAMETERS:  buffer              - Where the formatted string is returned
 *              types               - Bitfield of expected data types
 *
 * RETURN:      Count of return types. Formatted string in Buffer.
 *
 * DESCRIPTION: Format the resource bit widths into a printable string.
 *
 ******************************************************************************/

u32 acpi_ut_get_resource_bit_width(char *buffer, u16 types)
{
	u32 i;
	u16 sub_index;
	u32 found;

	*buffer = 0;
	sub_index = 1;
	found = 0;

	for (i = 0; i < NUM_RESOURCE_WIDTHS; i++) {
		if (types & 1) {
			strcat(buffer, &(ut_resource_type_names[i][sub_index]));
			sub_index = 0;
			found++;
		}

		types >>= 1;
	}

	return (found);
}
#endif
