/*******************************************************************************
 *
 * Module Name: dbconvert - debugger miscellaneous conversion routines
 *
 ******************************************************************************/

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
#include "acdebug.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbconvert")

#define DB_DEFAULT_PKG_ELEMENTS     33
/*******************************************************************************
 *
 * FUNCTION:    acpi_db_hex_char_to_value
 *
 * PARAMETERS:  hex_char            - Ascii Hex digit, 0-9|a-f|A-F
 *              return_value        - Where the converted value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a single hex character to a 4-bit number (0-16).
 *
 ******************************************************************************/
acpi_status acpi_db_hex_char_to_value(int hex_char, u8 *return_value)
{
	u8 value;

	/* Digit must be ascii [0-9a-fA-F] */

	if (!isxdigit(hex_char)) {
		return (AE_BAD_HEX_CONSTANT);
	}

	if (hex_char <= 0x39) {
		value = (u8)(hex_char - 0x30);
	} else {
		value = (u8)(toupper(hex_char) - 0x37);
	}

	*return_value = value;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_hex_byte_to_binary
 *
 * PARAMETERS:  hex_byte            - Double hex digit (0x00 - 0xFF) in format:
 *                                    hi_byte then lo_byte.
 *              return_value        - Where the converted value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert two hex characters to an 8 bit number (0 - 255).
 *
 ******************************************************************************/

static acpi_status acpi_db_hex_byte_to_binary(char *hex_byte, u8 *return_value)
{
	u8 local0;
	u8 local1;
	acpi_status status;

	/* High byte */

	status = acpi_db_hex_char_to_value(hex_byte[0], &local0);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Low byte */

	status = acpi_db_hex_char_to_value(hex_byte[1], &local1);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	*return_value = (u8)((local0 << 4) | local1);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_convert_to_buffer
 *
 * PARAMETERS:  string              - Input string to be converted
 *              object              - Where the buffer object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a string to a buffer object. String is treated a list
 *              of buffer elements, each separated by a space or comma.
 *
 ******************************************************************************/

static acpi_status
acpi_db_convert_to_buffer(char *string, union acpi_object *object)
{
	u32 i;
	u32 j;
	u32 length;
	u8 *buffer;
	acpi_status status;

	/* Generate the final buffer length */

	for (i = 0, length = 0; string[i];) {
		i += 2;
		length++;

		while (string[i] && ((string[i] == ',') || (string[i] == ' '))) {
			i++;
		}
	}

	buffer = ACPI_ALLOCATE(length);
	if (!buffer) {
		return (AE_NO_MEMORY);
	}

	/* Convert the command line bytes to the buffer */

	for (i = 0, j = 0; string[i];) {
		status = acpi_db_hex_byte_to_binary(&string[i], &buffer[j]);
		if (ACPI_FAILURE(status)) {
			ACPI_FREE(buffer);
			return (status);
		}

		j++;
		i += 2;
		while (string[i] && ((string[i] == ',') || (string[i] == ' '))) {
			i++;
		}
	}

	object->type = ACPI_TYPE_BUFFER;
	object->buffer.pointer = buffer;
	object->buffer.length = length;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_convert_to_package
 *
 * PARAMETERS:  string              - Input string to be converted
 *              object              - Where the package object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a string to a package object. Handles nested packages
 *              via recursion with acpi_db_convert_to_object.
 *
 ******************************************************************************/

acpi_status acpi_db_convert_to_package(char *string, union acpi_object * object)
{
	char *this;
	char *next;
	u32 i;
	acpi_object_type type;
	union acpi_object *elements;
	acpi_status status;

	elements =
	    ACPI_ALLOCATE_ZEROED(DB_DEFAULT_PKG_ELEMENTS *
				 sizeof(union acpi_object));

	this = string;
	for (i = 0; i < (DB_DEFAULT_PKG_ELEMENTS - 1); i++) {
		this = acpi_db_get_next_token(this, &next, &type);
		if (!this) {
			break;
		}

		/* Recursive call to convert each package element */

		status = acpi_db_convert_to_object(type, this, &elements[i]);
		if (ACPI_FAILURE(status)) {
			acpi_db_delete_objects(i + 1, elements);
			ACPI_FREE(elements);
			return (status);
		}

		this = next;
	}

	object->type = ACPI_TYPE_PACKAGE;
	object->package.count = i;
	object->package.elements = elements;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_convert_to_object
 *
 * PARAMETERS:  type                - Object type as determined by parser
 *              string              - Input string to be converted
 *              object              - Where the new object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a typed and tokenized string to an union acpi_object. Typing:
 *              1) String objects were surrounded by quotes.
 *              2) Buffer objects were surrounded by parentheses.
 *              3) Package objects were surrounded by brackets "[]".
 *              4) All standalone tokens are treated as integers.
 *
 ******************************************************************************/

acpi_status
acpi_db_convert_to_object(acpi_object_type type,
			  char *string, union acpi_object * object)
{
	acpi_status status = AE_OK;

	switch (type) {
	case ACPI_TYPE_STRING:

		object->type = ACPI_TYPE_STRING;
		object->string.pointer = string;
		object->string.length = (u32)strlen(string);
		break;

	case ACPI_TYPE_BUFFER:

		status = acpi_db_convert_to_buffer(string, object);
		break;

	case ACPI_TYPE_PACKAGE:

		status = acpi_db_convert_to_package(string, object);
		break;

	default:

		object->type = ACPI_TYPE_INTEGER;
		status = acpi_ut_strtoul64(string, 16, &object->integer.value);
		break;
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_encode_pld_buffer
 *
 * PARAMETERS:  pld_info            - _PLD buffer struct (Using local struct)
 *
 * RETURN:      Encode _PLD buffer suitable for return value from _PLD
 *
 * DESCRIPTION: Bit-packs a _PLD buffer struct. Used to test the _PLD macros
 *
 ******************************************************************************/

u8 *acpi_db_encode_pld_buffer(struct acpi_pld_info *pld_info)
{
	u32 *buffer;
	u32 dword;

	buffer = ACPI_ALLOCATE_ZEROED(ACPI_PLD_BUFFER_SIZE);
	if (!buffer) {
		return (NULL);
	}

	/* First 32 bits */

	dword = 0;
	ACPI_PLD_SET_REVISION(&dword, pld_info->revision);
	ACPI_PLD_SET_IGNORE_COLOR(&dword, pld_info->ignore_color);
	ACPI_PLD_SET_RED(&dword, pld_info->red);
	ACPI_PLD_SET_GREEN(&dword, pld_info->green);
	ACPI_PLD_SET_BLUE(&dword, pld_info->blue);
	ACPI_MOVE_32_TO_32(&buffer[0], &dword);

	/* Second 32 bits */

	dword = 0;
	ACPI_PLD_SET_WIDTH(&dword, pld_info->width);
	ACPI_PLD_SET_HEIGHT(&dword, pld_info->height);
	ACPI_MOVE_32_TO_32(&buffer[1], &dword);

	/* Third 32 bits */

	dword = 0;
	ACPI_PLD_SET_USER_VISIBLE(&dword, pld_info->user_visible);
	ACPI_PLD_SET_DOCK(&dword, pld_info->dock);
	ACPI_PLD_SET_LID(&dword, pld_info->lid);
	ACPI_PLD_SET_PANEL(&dword, pld_info->panel);
	ACPI_PLD_SET_VERTICAL(&dword, pld_info->vertical_position);
	ACPI_PLD_SET_HORIZONTAL(&dword, pld_info->horizontal_position);
	ACPI_PLD_SET_SHAPE(&dword, pld_info->shape);
	ACPI_PLD_SET_ORIENTATION(&dword, pld_info->group_orientation);
	ACPI_PLD_SET_TOKEN(&dword, pld_info->group_token);
	ACPI_PLD_SET_POSITION(&dword, pld_info->group_position);
	ACPI_PLD_SET_BAY(&dword, pld_info->bay);
	ACPI_MOVE_32_TO_32(&buffer[2], &dword);

	/* Fourth 32 bits */

	dword = 0;
	ACPI_PLD_SET_EJECTABLE(&dword, pld_info->ejectable);
	ACPI_PLD_SET_OSPM_EJECT(&dword, pld_info->ospm_eject_required);
	ACPI_PLD_SET_CABINET(&dword, pld_info->cabinet_number);
	ACPI_PLD_SET_CARD_CAGE(&dword, pld_info->card_cage_number);
	ACPI_PLD_SET_REFERENCE(&dword, pld_info->reference);
	ACPI_PLD_SET_ROTATION(&dword, pld_info->rotation);
	ACPI_PLD_SET_ORDER(&dword, pld_info->order);
	ACPI_MOVE_32_TO_32(&buffer[3], &dword);

	if (pld_info->revision >= 2) {

		/* Fifth 32 bits */

		dword = 0;
		ACPI_PLD_SET_VERT_OFFSET(&dword, pld_info->vertical_offset);
		ACPI_PLD_SET_HORIZ_OFFSET(&dword, pld_info->horizontal_offset);
		ACPI_MOVE_32_TO_32(&buffer[4], &dword);
	}

	return (ACPI_CAST_PTR(u8, buffer));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_dump_pld_buffer
 *
 * PARAMETERS:  obj_desc            - Object returned from _PLD method
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Dumps formatted contents of a _PLD return buffer.
 *
 ******************************************************************************/

#define ACPI_PLD_OUTPUT     "%20s : %-6X\n"

void acpi_db_dump_pld_buffer(union acpi_object *obj_desc)
{
	union acpi_object *buffer_desc;
	struct acpi_pld_info *pld_info;
	u8 *new_buffer;
	acpi_status status;

	/* Object must be of type Package with at least one Buffer element */

	if (obj_desc->type != ACPI_TYPE_PACKAGE) {
		return;
	}

	buffer_desc = &obj_desc->package.elements[0];
	if (buffer_desc->type != ACPI_TYPE_BUFFER) {
		return;
	}

	/* Convert _PLD buffer to local _PLD struct */

	status = acpi_decode_pld_buffer(buffer_desc->buffer.pointer,
					buffer_desc->buffer.length, &pld_info);
	if (ACPI_FAILURE(status)) {
		return;
	}

	/* Encode local _PLD struct back to a _PLD buffer */

	new_buffer = acpi_db_encode_pld_buffer(pld_info);
	if (!new_buffer) {
		return;
	}

	/* The two bit-packed buffers should match */

	if (memcmp(new_buffer, buffer_desc->buffer.pointer,
		   buffer_desc->buffer.length)) {
		acpi_os_printf
		    ("Converted _PLD buffer does not compare. New:\n");

		acpi_ut_dump_buffer(new_buffer,
				    buffer_desc->buffer.length, DB_BYTE_DISPLAY,
				    0);
	}

	/* First 32-bit dword */

	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Revision", pld_info->revision);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_IgnoreColor",
		       pld_info->ignore_color);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Red", pld_info->red);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Green", pld_info->green);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Blue", pld_info->blue);

	/* Second 32-bit dword */

	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Width", pld_info->width);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Height", pld_info->height);

	/* Third 32-bit dword */

	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_UserVisible",
		       pld_info->user_visible);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Dock", pld_info->dock);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Lid", pld_info->lid);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Panel", pld_info->panel);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_VerticalPosition",
		       pld_info->vertical_position);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_HorizontalPosition",
		       pld_info->horizontal_position);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Shape", pld_info->shape);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_GroupOrientation",
		       pld_info->group_orientation);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_GroupToken",
		       pld_info->group_token);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_GroupPosition",
		       pld_info->group_position);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Bay", pld_info->bay);

	/* Fourth 32-bit dword */

	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Ejectable", pld_info->ejectable);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_EjectRequired",
		       pld_info->ospm_eject_required);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_CabinetNumber",
		       pld_info->cabinet_number);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_CardCageNumber",
		       pld_info->card_cage_number);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Reference", pld_info->reference);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Rotation", pld_info->rotation);
	acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_Order", pld_info->order);

	/* Fifth 32-bit dword */

	if (buffer_desc->buffer.length > 16) {
		acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_VerticalOffset",
			       pld_info->vertical_offset);
		acpi_os_printf(ACPI_PLD_OUTPUT, "PLD_HorizontalOffset",
			       pld_info->horizontal_offset);
	}

	ACPI_FREE(pld_info);
	ACPI_FREE(new_buffer);
}
