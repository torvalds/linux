/******************************************************************************
 *
 * Module Name: exnames - interpreter/scanner name load/execute
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
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
#include "acinterp.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exnames")

/* Local prototypes */
static char *acpi_ex_allocate_name_string(u32 prefix_count, u32 num_name_segs);

static acpi_status acpi_ex_name_segment(u8 **in_aml_address, char *name_string);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_allocate_name_string
 *
 * PARAMETERS:  prefix_count        - Count of parent levels. Special cases:
 *                                    (-1)==root,  0==none
 *              num_name_segs       - count of 4-character name segments
 *
 * RETURN:      A pointer to the allocated string segment. This segment must
 *              be deleted by the caller.
 *
 * DESCRIPTION: Allocate a buffer for a name string. Ensure allocated name
 *              string is long enough, and set up prefix if any.
 *
 ******************************************************************************/

static char *acpi_ex_allocate_name_string(u32 prefix_count, u32 num_name_segs)
{
	char *temp_ptr;
	char *name_string;
	u32 size_needed;

	ACPI_FUNCTION_TRACE(ex_allocate_name_string);

	/*
	 * Allow room for all \ and ^ prefixes, all segments and a multi_name_prefix.
	 * Also, one byte for the null terminator.
	 * This may actually be somewhat longer than needed.
	 */
	if (prefix_count == ACPI_UINT32_MAX) {

		/* Special case for root */

		size_needed = 1 + (ACPI_NAME_SIZE * num_name_segs) + 2 + 1;
	} else {
		size_needed =
		    prefix_count + (ACPI_NAME_SIZE * num_name_segs) + 2 + 1;
	}

	/*
	 * Allocate a buffer for the name.
	 * This buffer must be deleted by the caller!
	 */
	name_string = ACPI_ALLOCATE(size_needed);
	if (!name_string) {
		ACPI_ERROR((AE_INFO,
			    "Could not allocate size %u", size_needed));
		return_PTR(NULL);
	}

	temp_ptr = name_string;

	/* Set up Root or Parent prefixes if needed */

	if (prefix_count == ACPI_UINT32_MAX) {
		*temp_ptr++ = AML_ROOT_PREFIX;
	} else {
		while (prefix_count--) {
			*temp_ptr++ = AML_PARENT_PREFIX;
		}
	}

	/* Set up Dual or Multi prefixes if needed */

	if (num_name_segs > 2) {

		/* Set up multi prefixes   */

		*temp_ptr++ = AML_MULTI_NAME_PREFIX_OP;
		*temp_ptr++ = (char)num_name_segs;
	} else if (2 == num_name_segs) {

		/* Set up dual prefixes */

		*temp_ptr++ = AML_DUAL_NAME_PREFIX;
	}

	/*
	 * Terminate string following prefixes. acpi_ex_name_segment() will
	 * append the segment(s)
	 */
	*temp_ptr = 0;

	return_PTR(name_string);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_name_segment
 *
 * PARAMETERS:  in_aml_address  - Pointer to the name in the AML code
 *              name_string     - Where to return the name. The name is appended
 *                                to any existing string to form a namepath
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Extract an ACPI name (4 bytes) from the AML byte stream
 *
 ******************************************************************************/

static acpi_status acpi_ex_name_segment(u8 ** in_aml_address, char *name_string)
{
	char *aml_address = (void *)*in_aml_address;
	acpi_status status = AE_OK;
	u32 index;
	char char_buf[5];

	ACPI_FUNCTION_TRACE(ex_name_segment);

	/*
	 * If first character is a digit, then we know that we aren't looking at a
	 * valid name segment
	 */
	char_buf[0] = *aml_address;

	if ('0' <= char_buf[0] && char_buf[0] <= '9') {
		ACPI_ERROR((AE_INFO, "Invalid leading digit: %c", char_buf[0]));
		return_ACPI_STATUS(AE_CTRL_PENDING);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_LOAD, "Bytes from stream:\n"));

	for (index = 0;
	     (index < ACPI_NAME_SIZE)
	     && (acpi_ut_valid_acpi_char(*aml_address, 0)); index++) {
		char_buf[index] = *aml_address++;
		ACPI_DEBUG_PRINT((ACPI_DB_LOAD, "%c\n", char_buf[index]));
	}

	/* Valid name segment  */

	if (index == 4) {

		/* Found 4 valid characters */

		char_buf[4] = '\0';

		if (name_string) {
			ACPI_STRCAT(name_string, char_buf);
			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "Appended to - %s\n", name_string));
		} else {
			ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
					  "No Name string - %s\n", char_buf));
		}
	} else if (index == 0) {
		/*
		 * First character was not a valid name character,
		 * so we are looking at something other than a name.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Leading character is not alpha: %02Xh (not a name)\n",
				  char_buf[0]));
		status = AE_CTRL_PENDING;
	} else {
		/*
		 * Segment started with one or more valid characters, but fewer than
		 * the required 4
		 */
		status = AE_AML_BAD_NAME;
		ACPI_ERROR((AE_INFO,
			    "Bad character 0x%02x in name, at %p",
			    *aml_address, aml_address));
	}

	*in_aml_address = ACPI_CAST_PTR(u8, aml_address);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_get_name_string
 *
 * PARAMETERS:  data_type           - Object type to be associated with this
 *                                    name
 *              in_aml_address      - Pointer to the namestring in the AML code
 *              out_name_string     - Where the namestring is returned
 *              out_name_length     - Length of the returned string
 *
 * RETURN:      Status, namestring and length
 *
 * DESCRIPTION: Extract a full namepath from the AML byte stream,
 *              including any prefixes.
 *
 ******************************************************************************/

acpi_status
acpi_ex_get_name_string(acpi_object_type data_type,
			u8 * in_aml_address,
			char **out_name_string, u32 * out_name_length)
{
	acpi_status status = AE_OK;
	u8 *aml_address = in_aml_address;
	char *name_string = NULL;
	u32 num_segments;
	u32 prefix_count = 0;
	u8 has_prefix = FALSE;

	ACPI_FUNCTION_TRACE_PTR(ex_get_name_string, aml_address);

	if (ACPI_TYPE_LOCAL_REGION_FIELD == data_type ||
	    ACPI_TYPE_LOCAL_BANK_FIELD == data_type ||
	    ACPI_TYPE_LOCAL_INDEX_FIELD == data_type) {

		/* Disallow prefixes for types associated with field_unit names */

		name_string = acpi_ex_allocate_name_string(0, 1);
		if (!name_string) {
			status = AE_NO_MEMORY;
		} else {
			status =
			    acpi_ex_name_segment(&aml_address, name_string);
		}
	} else {
		/*
		 * data_type is not a field name.
		 * Examine first character of name for root or parent prefix operators
		 */
		switch (*aml_address) {
		case AML_ROOT_PREFIX:

			ACPI_DEBUG_PRINT((ACPI_DB_LOAD,
					  "RootPrefix(\\) at %p\n",
					  aml_address));

			/*
			 * Remember that we have a root_prefix --
			 * see comment in acpi_ex_allocate_name_string()
			 */
			aml_address++;
			prefix_count = ACPI_UINT32_MAX;
			has_prefix = TRUE;
			break;

		case AML_PARENT_PREFIX:

			/* Increment past possibly multiple parent prefixes */

			do {
				ACPI_DEBUG_PRINT((ACPI_DB_LOAD,
						  "ParentPrefix (^) at %p\n",
						  aml_address));

				aml_address++;
				prefix_count++;

			} while (*aml_address == AML_PARENT_PREFIX);

			has_prefix = TRUE;
			break;

		default:

			/* Not a prefix character */

			break;
		}

		/* Examine first character of name for name segment prefix operator */

		switch (*aml_address) {
		case AML_DUAL_NAME_PREFIX:

			ACPI_DEBUG_PRINT((ACPI_DB_LOAD,
					  "DualNamePrefix at %p\n",
					  aml_address));

			aml_address++;
			name_string =
			    acpi_ex_allocate_name_string(prefix_count, 2);
			if (!name_string) {
				status = AE_NO_MEMORY;
				break;
			}

			/* Indicate that we processed a prefix */

			has_prefix = TRUE;

			status =
			    acpi_ex_name_segment(&aml_address, name_string);
			if (ACPI_SUCCESS(status)) {
				status =
				    acpi_ex_name_segment(&aml_address,
							 name_string);
			}
			break;

		case AML_MULTI_NAME_PREFIX_OP:

			ACPI_DEBUG_PRINT((ACPI_DB_LOAD,
					  "MultiNamePrefix at %p\n",
					  aml_address));

			/* Fetch count of segments remaining in name path */

			aml_address++;
			num_segments = *aml_address;

			name_string =
			    acpi_ex_allocate_name_string(prefix_count,
							 num_segments);
			if (!name_string) {
				status = AE_NO_MEMORY;
				break;
			}

			/* Indicate that we processed a prefix */

			aml_address++;
			has_prefix = TRUE;

			while (num_segments &&
			       (status =
				acpi_ex_name_segment(&aml_address,
						     name_string)) == AE_OK) {
				num_segments--;
			}

			break;

		case 0:

			/* null_name valid as of 8-12-98 ASL/AML Grammar Update */

			if (prefix_count == ACPI_UINT32_MAX) {
				ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
						  "NameSeg is \"\\\" followed by NULL\n"));
			}

			/* Consume the NULL byte */

			aml_address++;
			name_string =
			    acpi_ex_allocate_name_string(prefix_count, 0);
			if (!name_string) {
				status = AE_NO_MEMORY;
				break;
			}

			break;

		default:

			/* Name segment string */

			name_string =
			    acpi_ex_allocate_name_string(prefix_count, 1);
			if (!name_string) {
				status = AE_NO_MEMORY;
				break;
			}

			status =
			    acpi_ex_name_segment(&aml_address, name_string);
			break;
		}
	}

	if (AE_CTRL_PENDING == status && has_prefix) {

		/* Ran out of segments after processing a prefix */

		ACPI_ERROR((AE_INFO, "Malformed Name at %p", name_string));
		status = AE_AML_BAD_NAME;
	}

	if (ACPI_FAILURE(status)) {
		if (name_string) {
			ACPI_FREE(name_string);
		}
		return_ACPI_STATUS(status);
	}

	*out_name_string = name_string;
	*out_name_length = (u32) (aml_address - in_aml_address);

	return_ACPI_STATUS(status);
}
