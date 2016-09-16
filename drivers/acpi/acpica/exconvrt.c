/******************************************************************************
 *
 * Module Name: exconvrt - Object conversion routines
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
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
ACPI_MODULE_NAME("exconvrt")

/* Local prototypes */
static u32
acpi_ex_convert_to_ascii(u64 integer, u16 base, u8 *string, u8 max_length);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_convert_to_integer
 *
 * PARAMETERS:  obj_desc        - Object to be converted. Must be an
 *                                Integer, Buffer, or String
 *              result_desc     - Where the new Integer object is returned
 *              flags           - Used for string conversion
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an ACPI Object to an integer.
 *
 ******************************************************************************/

acpi_status
acpi_ex_convert_to_integer(union acpi_operand_object *obj_desc,
			   union acpi_operand_object **result_desc, u32 flags)
{
	union acpi_operand_object *return_desc;
	u8 *pointer;
	u64 result;
	u32 i;
	u32 count;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ex_convert_to_integer, obj_desc);

	switch (obj_desc->common.type) {
	case ACPI_TYPE_INTEGER:

		/* No conversion necessary */

		*result_desc = obj_desc;
		return_ACPI_STATUS(AE_OK);

	case ACPI_TYPE_BUFFER:
	case ACPI_TYPE_STRING:

		/* Note: Takes advantage of common buffer/string fields */

		pointer = obj_desc->buffer.pointer;
		count = obj_desc->buffer.length;
		break;

	default:

		return_ACPI_STATUS(AE_TYPE);
	}

	/*
	 * Convert the buffer/string to an integer. Note that both buffers and
	 * strings are treated as raw data - we don't convert ascii to hex for
	 * strings.
	 *
	 * There are two terminating conditions for the loop:
	 * 1) The size of an integer has been reached, or
	 * 2) The end of the buffer or string has been reached
	 */
	result = 0;

	/* String conversion is different than Buffer conversion */

	switch (obj_desc->common.type) {
	case ACPI_TYPE_STRING:
		/*
		 * Convert string to an integer - for most cases, the string must be
		 * hexadecimal as per the ACPI specification. The only exception (as
		 * of ACPI 3.0) is that the to_integer() operator allows both decimal
		 * and hexadecimal strings (hex prefixed with "0x").
		 */
		status = acpi_ut_strtoul64(ACPI_CAST_PTR(char, pointer),
					   (acpi_gbl_integer_byte_width |
					    flags), &result);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
		break;

	case ACPI_TYPE_BUFFER:

		/* Check for zero-length buffer */

		if (!count) {
			return_ACPI_STATUS(AE_AML_BUFFER_LIMIT);
		}

		/* Transfer no more than an integer's worth of data */

		if (count > acpi_gbl_integer_byte_width) {
			count = acpi_gbl_integer_byte_width;
		}

		/*
		 * Convert buffer to an integer - we simply grab enough raw data
		 * from the buffer to fill an integer
		 */
		for (i = 0; i < count; i++) {
			/*
			 * Get next byte and shift it into the Result.
			 * Little endian is used, meaning that the first byte of the buffer
			 * is the LSB of the integer
			 */
			result |= (((u64) pointer[i]) << (i * 8));
		}
		break;

	default:

		/* No other types can get here */

		break;
	}

	/* Create a new integer */

	return_desc = acpi_ut_create_integer_object(result);
	if (!return_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Converted value: %8.8X%8.8X\n",
			  ACPI_FORMAT_UINT64(result)));

	/* Save the Result */

	(void)acpi_ex_truncate_for32bit_table(return_desc);
	*result_desc = return_desc;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_convert_to_buffer
 *
 * PARAMETERS:  obj_desc        - Object to be converted. Must be an
 *                                Integer, Buffer, or String
 *              result_desc     - Where the new buffer object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an ACPI Object to a Buffer
 *
 ******************************************************************************/

acpi_status
acpi_ex_convert_to_buffer(union acpi_operand_object *obj_desc,
			  union acpi_operand_object **result_desc)
{
	union acpi_operand_object *return_desc;
	u8 *new_buf;

	ACPI_FUNCTION_TRACE_PTR(ex_convert_to_buffer, obj_desc);

	switch (obj_desc->common.type) {
	case ACPI_TYPE_BUFFER:

		/* No conversion necessary */

		*result_desc = obj_desc;
		return_ACPI_STATUS(AE_OK);

	case ACPI_TYPE_INTEGER:
		/*
		 * Create a new Buffer object.
		 * Need enough space for one integer
		 */
		return_desc =
		    acpi_ut_create_buffer_object(acpi_gbl_integer_byte_width);
		if (!return_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Copy the integer to the buffer, LSB first */

		new_buf = return_desc->buffer.pointer;
		memcpy(new_buf, &obj_desc->integer.value,
		       acpi_gbl_integer_byte_width);
		break;

	case ACPI_TYPE_STRING:
		/*
		 * Create a new Buffer object
		 * Size will be the string length
		 *
		 * NOTE: Add one to the string length to include the null terminator.
		 * The ACPI spec is unclear on this subject, but there is existing
		 * ASL/AML code that depends on the null being transferred to the new
		 * buffer.
		 */
		return_desc = acpi_ut_create_buffer_object((acpi_size)
							   obj_desc->string.
							   length + 1);
		if (!return_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Copy the string to the buffer */

		new_buf = return_desc->buffer.pointer;
		strncpy((char *)new_buf, (char *)obj_desc->string.pointer,
			obj_desc->string.length);
		break;

	default:

		return_ACPI_STATUS(AE_TYPE);
	}

	/* Mark buffer initialized */

	return_desc->common.flags |= AOPOBJ_DATA_VALID;
	*result_desc = return_desc;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_convert_to_ascii
 *
 * PARAMETERS:  integer         - Value to be converted
 *              base            - ACPI_STRING_DECIMAL or ACPI_STRING_HEX
 *              string          - Where the string is returned
 *              data_width      - Size of data item to be converted, in bytes
 *
 * RETURN:      Actual string length
 *
 * DESCRIPTION: Convert an ACPI Integer to a hex or decimal string
 *
 ******************************************************************************/

static u32
acpi_ex_convert_to_ascii(u64 integer, u16 base, u8 *string, u8 data_width)
{
	u64 digit;
	u32 i;
	u32 j;
	u32 k = 0;
	u32 hex_length;
	u32 decimal_length;
	u32 remainder;
	u8 supress_zeros;

	ACPI_FUNCTION_ENTRY();

	switch (base) {
	case 10:

		/* Setup max length for the decimal number */

		switch (data_width) {
		case 1:

			decimal_length = ACPI_MAX8_DECIMAL_DIGITS;
			break;

		case 4:

			decimal_length = ACPI_MAX32_DECIMAL_DIGITS;
			break;

		case 8:
		default:

			decimal_length = ACPI_MAX64_DECIMAL_DIGITS;
			break;
		}

		supress_zeros = TRUE;	/* No leading zeros */
		remainder = 0;

		for (i = decimal_length; i > 0; i--) {

			/* Divide by nth factor of 10 */

			digit = integer;
			for (j = 0; j < i; j++) {
				(void)acpi_ut_short_divide(digit, 10, &digit,
							   &remainder);
			}

			/* Handle leading zeros */

			if (remainder != 0) {
				supress_zeros = FALSE;
			}

			if (!supress_zeros) {
				string[k] = (u8) (ACPI_ASCII_ZERO + remainder);
				k++;
			}
		}
		break;

	case 16:

		/* hex_length: 2 ascii hex chars per data byte */

		hex_length = ACPI_MUL_2(data_width);
		for (i = 0, j = (hex_length - 1); i < hex_length; i++, j--) {

			/* Get one hex digit, most significant digits first */

			string[k] = (u8)
			    acpi_ut_hex_to_ascii_char(integer, ACPI_MUL_4(j));
			k++;
		}
		break;

	default:
		return (0);
	}

	/*
	 * Since leading zeros are suppressed, we must check for the case where
	 * the integer equals 0
	 *
	 * Finally, null terminate the string and return the length
	 */
	if (!k) {
		string[0] = ACPI_ASCII_ZERO;
		k = 1;
	}

	string[k] = 0;
	return ((u32) k);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_convert_to_string
 *
 * PARAMETERS:  obj_desc        - Object to be converted. Must be an
 *                                Integer, Buffer, or String
 *              result_desc     - Where the string object is returned
 *              type            - String flags (base and conversion type)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an ACPI Object to a string
 *
 ******************************************************************************/

acpi_status
acpi_ex_convert_to_string(union acpi_operand_object * obj_desc,
			  union acpi_operand_object ** result_desc, u32 type)
{
	union acpi_operand_object *return_desc;
	u8 *new_buf;
	u32 i;
	u32 string_length = 0;
	u16 base = 16;
	u8 separator = ',';

	ACPI_FUNCTION_TRACE_PTR(ex_convert_to_string, obj_desc);

	switch (obj_desc->common.type) {
	case ACPI_TYPE_STRING:

		/* No conversion necessary */

		*result_desc = obj_desc;
		return_ACPI_STATUS(AE_OK);

	case ACPI_TYPE_INTEGER:

		switch (type) {
		case ACPI_EXPLICIT_CONVERT_DECIMAL:

			/* Make room for maximum decimal number */

			string_length = ACPI_MAX_DECIMAL_DIGITS;
			base = 10;
			break;

		default:

			/* Two hex string characters for each integer byte */

			string_length = ACPI_MUL_2(acpi_gbl_integer_byte_width);
			break;
		}

		/*
		 * Create a new String
		 * Need enough space for one ASCII integer (plus null terminator)
		 */
		return_desc =
		    acpi_ut_create_string_object((acpi_size)string_length);
		if (!return_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		new_buf = return_desc->buffer.pointer;

		/* Convert integer to string */

		string_length =
		    acpi_ex_convert_to_ascii(obj_desc->integer.value, base,
					     new_buf,
					     acpi_gbl_integer_byte_width);

		/* Null terminate at the correct place */

		return_desc->string.length = string_length;
		new_buf[string_length] = 0;
		break;

	case ACPI_TYPE_BUFFER:

		/* Setup string length, base, and separator */

		switch (type) {
		case ACPI_EXPLICIT_CONVERT_DECIMAL:	/* Used by to_decimal_string */
			/*
			 * From ACPI: "If Data is a buffer, it is converted to a string of
			 * decimal values separated by commas."
			 */
			base = 10;

			/*
			 * Calculate the final string length. Individual string values
			 * are variable length (include separator for each)
			 */
			for (i = 0; i < obj_desc->buffer.length; i++) {
				if (obj_desc->buffer.pointer[i] >= 100) {
					string_length += 4;
				} else if (obj_desc->buffer.pointer[i] >= 10) {
					string_length += 3;
				} else {
					string_length += 2;
				}
			}
			break;

		case ACPI_IMPLICIT_CONVERT_HEX:
			/*
			 * From the ACPI spec:
			 *"The entire contents of the buffer are converted to a string of
			 * two-character hexadecimal numbers, each separated by a space."
			 */
			separator = ' ';
			string_length = (obj_desc->buffer.length * 3);
			break;

		case ACPI_EXPLICIT_CONVERT_HEX:	/* Used by to_hex_string */
			/*
			 * From ACPI: "If Data is a buffer, it is converted to a string of
			 * hexadecimal values separated by commas."
			 */
			string_length = (obj_desc->buffer.length * 3);
			break;

		default:
			return_ACPI_STATUS(AE_BAD_PARAMETER);
		}

		/*
		 * Create a new string object and string buffer
		 * (-1 because of extra separator included in string_length from above)
		 * Allow creation of zero-length strings from zero-length buffers.
		 */
		if (string_length) {
			string_length--;
		}

		return_desc =
		    acpi_ut_create_string_object((acpi_size)string_length);
		if (!return_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		new_buf = return_desc->buffer.pointer;

		/*
		 * Convert buffer bytes to hex or decimal values
		 * (separated by commas or spaces)
		 */
		for (i = 0; i < obj_desc->buffer.length; i++) {
			new_buf += acpi_ex_convert_to_ascii((u64) obj_desc->
							    buffer.pointer[i],
							    base, new_buf, 1);
			*new_buf++ = separator;	/* each separated by a comma or space */
		}

		/*
		 * Null terminate the string
		 * (overwrites final comma/space from above)
		 */
		if (obj_desc->buffer.length) {
			new_buf--;
		}
		*new_buf = 0;
		break;

	default:

		return_ACPI_STATUS(AE_TYPE);
	}

	*result_desc = return_desc;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_convert_to_target_type
 *
 * PARAMETERS:  destination_type    - Current type of the destination
 *              source_desc         - Source object to be converted.
 *              result_desc         - Where the converted object is returned
 *              walk_state          - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Implements "implicit conversion" rules for storing an object.
 *
 ******************************************************************************/

acpi_status
acpi_ex_convert_to_target_type(acpi_object_type destination_type,
			       union acpi_operand_object *source_desc,
			       union acpi_operand_object **result_desc,
			       struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(ex_convert_to_target_type);

	/* Default behavior */

	*result_desc = source_desc;

	/*
	 * If required by the target,
	 * perform implicit conversion on the source before we store it.
	 */
	switch (GET_CURRENT_ARG_TYPE(walk_state->op_info->runtime_args)) {
	case ARGI_SIMPLE_TARGET:
	case ARGI_FIXED_TARGET:
	case ARGI_INTEGER_REF:	/* Handles Increment, Decrement cases */

		switch (destination_type) {
		case ACPI_TYPE_LOCAL_REGION_FIELD:
			/*
			 * Named field can always handle conversions
			 */
			break;

		default:

			/* No conversion allowed for these types */

			if (destination_type != source_desc->common.type) {
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "Explicit operator, will store (%s) over existing type (%s)\n",
						  acpi_ut_get_object_type_name
						  (source_desc),
						  acpi_ut_get_type_name
						  (destination_type)));
				status = AE_TYPE;
			}
		}
		break;

	case ARGI_TARGETREF:
	case ARGI_STORE_TARGET:

		switch (destination_type) {
		case ACPI_TYPE_INTEGER:
		case ACPI_TYPE_BUFFER_FIELD:
		case ACPI_TYPE_LOCAL_BANK_FIELD:
		case ACPI_TYPE_LOCAL_INDEX_FIELD:
			/*
			 * These types require an Integer operand. We can convert
			 * a Buffer or a String to an Integer if necessary.
			 */
			status =
			    acpi_ex_convert_to_integer(source_desc, result_desc,
						       ACPI_STRTOUL_BASE16);
			break;

		case ACPI_TYPE_STRING:
			/*
			 * The operand must be a String. We can convert an
			 * Integer or Buffer if necessary
			 */
			status =
			    acpi_ex_convert_to_string(source_desc, result_desc,
						      ACPI_IMPLICIT_CONVERT_HEX);
			break;

		case ACPI_TYPE_BUFFER:
			/*
			 * The operand must be a Buffer. We can convert an
			 * Integer or String if necessary
			 */
			status =
			    acpi_ex_convert_to_buffer(source_desc, result_desc);
			break;

		default:

			ACPI_ERROR((AE_INFO,
				    "Bad destination type during conversion: 0x%X",
				    destination_type));
			status = AE_AML_INTERNAL;
			break;
		}
		break;

	case ARGI_REFERENCE:
		/*
		 * create_xxxx_field cases - we are storing the field object into the name
		 */
		break;

	default:

		ACPI_ERROR((AE_INFO,
			    "Unknown Target type ID 0x%X AmlOpcode 0x%X DestType %s",
			    GET_CURRENT_ARG_TYPE(walk_state->op_info->
						 runtime_args),
			    walk_state->opcode,
			    acpi_ut_get_type_name(destination_type)));
		status = AE_AML_INTERNAL;
	}

	/*
	 * Source-to-Target conversion semantics:
	 *
	 * If conversion to the target type cannot be performed, then simply
	 * overwrite the target with the new object and type.
	 */
	if (status == AE_TYPE) {
		status = AE_OK;
	}

	return_ACPI_STATUS(status);
}
