/*******************************************************************************
 *
 * Module Name: utmisc - common utility procedures
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
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utmisc")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_is_aml_table
 *
 * PARAMETERS:  Table               - An ACPI table
 *
 * RETURN:      TRUE if table contains executable AML; FALSE otherwise
 *
 * DESCRIPTION: Check ACPI Signature for a table that contains AML code.
 *              Currently, these are DSDT,SSDT,PSDT. All other table types are
 *              data tables that do not contain AML code.
 *
 ******************************************************************************/
u8 acpi_ut_is_aml_table(struct acpi_table_header *table)
{

	/* Ignore tables that contain AML */

	if (ACPI_COMPARE_NAME(table->signature, DSDT_SIG) ||
	    ACPI_COMPARE_NAME(table->signature, PSDT_SIG) ||
	    ACPI_COMPARE_NAME(table->signature, SSDT_SIG)) {
		return (TRUE);
	}

	return (FALSE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_allocate_owner_id
 *
 * PARAMETERS:  owner_id        - Where the new owner ID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate a table or method owner ID. The owner ID is used to
 *              track objects created by the table or method, to be deleted
 *              when the method exits or the table is unloaded.
 *
 ******************************************************************************/

acpi_status acpi_ut_allocate_owner_id(acpi_owner_id * owner_id)
{
	acpi_native_uint i;
	acpi_native_uint j;
	acpi_native_uint k;
	acpi_status status;

	ACPI_FUNCTION_TRACE("ut_allocate_owner_id");

	/* Guard against multiple allocations of ID to the same location */

	if (*owner_id) {
		ACPI_ERROR((AE_INFO, "Owner ID [%2.2X] already exists",
			    *owner_id));
		return_ACPI_STATUS(AE_ALREADY_EXISTS);
	}

	/* Mutex for the global ID mask */

	status = acpi_ut_acquire_mutex(ACPI_MTX_CACHES);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Find a free owner ID, cycle through all possible IDs on repeated
	 * allocations. (ACPI_NUM_OWNERID_MASKS + 1) because first index may have
	 * to be scanned twice.
	 */
	for (i = 0, j = acpi_gbl_last_owner_id_index;
	     i < (ACPI_NUM_OWNERID_MASKS + 1); i++, j++) {
		if (j >= ACPI_NUM_OWNERID_MASKS) {
			j = 0;	/* Wraparound to start of mask array */
		}

		for (k = acpi_gbl_next_owner_id_offset; k < 32; k++) {
			if (acpi_gbl_owner_id_mask[j] == ACPI_UINT32_MAX) {

				/* There are no free IDs in this mask */

				break;
			}

			if (!(acpi_gbl_owner_id_mask[j] & (1 << k))) {
				/*
				 * Found a free ID. The actual ID is the bit index plus one,
				 * making zero an invalid Owner ID. Save this as the last ID
				 * allocated and update the global ID mask.
				 */
				acpi_gbl_owner_id_mask[j] |= (1 << k);

				acpi_gbl_last_owner_id_index = (u8) j;
				acpi_gbl_next_owner_id_offset = (u8) (k + 1);

				/*
				 * Construct encoded ID from the index and bit position
				 *
				 * Note: Last [j].k (bit 255) is never used and is marked
				 * permanently allocated (prevents +1 overflow)
				 */
				*owner_id =
				    (acpi_owner_id) ((k + 1) + ACPI_MUL_32(j));

				ACPI_DEBUG_PRINT((ACPI_DB_VALUES,
						  "Allocated owner_id: %2.2X\n",
						  (unsigned int)*owner_id));
				goto exit;
			}
		}

		acpi_gbl_next_owner_id_offset = 0;
	}

	/*
	 * All owner_ids have been allocated. This typically should
	 * not happen since the IDs are reused after deallocation. The IDs are
	 * allocated upon table load (one per table) and method execution, and
	 * they are released when a table is unloaded or a method completes
	 * execution.
	 *
	 * If this error happens, there may be very deep nesting of invoked control
	 * methods, or there may be a bug where the IDs are not released.
	 */
	status = AE_OWNER_ID_LIMIT;
	ACPI_ERROR((AE_INFO,
		    "Could not allocate new owner_id (255 max), AE_OWNER_ID_LIMIT"));

      exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_CACHES);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_release_owner_id
 *
 * PARAMETERS:  owner_id_ptr        - Pointer to a previously allocated owner_iD
 *
 * RETURN:      None. No error is returned because we are either exiting a
 *              control method or unloading a table. Either way, we would
 *              ignore any error anyway.
 *
 * DESCRIPTION: Release a table or method owner ID.  Valid IDs are 1 - 255
 *
 ******************************************************************************/

void acpi_ut_release_owner_id(acpi_owner_id * owner_id_ptr)
{
	acpi_owner_id owner_id = *owner_id_ptr;
	acpi_status status;
	acpi_native_uint index;
	u32 bit;

	ACPI_FUNCTION_TRACE_U32("ut_release_owner_id", owner_id);

	/* Always clear the input owner_id (zero is an invalid ID) */

	*owner_id_ptr = 0;

	/* Zero is not a valid owner_iD */

	if (owner_id == 0) {
		ACPI_ERROR((AE_INFO, "Invalid owner_id: %2.2X", owner_id));
		return_VOID;
	}

	/* Mutex for the global ID mask */

	status = acpi_ut_acquire_mutex(ACPI_MTX_CACHES);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	/* Normalize the ID to zero */

	owner_id--;

	/* Decode ID to index/offset pair */

	index = ACPI_DIV_32(owner_id);
	bit = 1 << ACPI_MOD_32(owner_id);

	/* Free the owner ID only if it is valid */

	if (acpi_gbl_owner_id_mask[index] & bit) {
		acpi_gbl_owner_id_mask[index] ^= bit;
	} else {
		ACPI_ERROR((AE_INFO,
			    "Release of non-allocated owner_id: %2.2X",
			    owner_id + 1));
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_CACHES);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strupr (strupr)
 *
 * PARAMETERS:  src_string      - The source string to convert
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert string to uppercase
 *
 * NOTE: This is not a POSIX function, so it appears here, not in utclib.c
 *
 ******************************************************************************/

void acpi_ut_strupr(char *src_string)
{
	char *string;

	ACPI_FUNCTION_ENTRY();

	if (!src_string) {
		return;
	}

	/* Walk entire string, uppercasing the letters */

	for (string = src_string; *string; string++) {
		*string = (char)ACPI_TOUPPER(*string);
	}

	return;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_print_string
 *
 * PARAMETERS:  String          - Null terminated ASCII string
 *              max_length      - Maximum output length
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump an ASCII string with support for ACPI-defined escape
 *              sequences.
 *
 ******************************************************************************/

void acpi_ut_print_string(char *string, u8 max_length)
{
	u32 i;

	if (!string) {
		acpi_os_printf("<\"NULL STRING PTR\">");
		return;
	}

	acpi_os_printf("\"");
	for (i = 0; string[i] && (i < max_length); i++) {

		/* Escape sequences */

		switch (string[i]) {
		case 0x07:
			acpi_os_printf("\\a");	/* BELL */
			break;

		case 0x08:
			acpi_os_printf("\\b");	/* BACKSPACE */
			break;

		case 0x0C:
			acpi_os_printf("\\f");	/* FORMFEED */
			break;

		case 0x0A:
			acpi_os_printf("\\n");	/* LINEFEED */
			break;

		case 0x0D:
			acpi_os_printf("\\r");	/* CARRIAGE RETURN */
			break;

		case 0x09:
			acpi_os_printf("\\t");	/* HORIZONTAL TAB */
			break;

		case 0x0B:
			acpi_os_printf("\\v");	/* VERTICAL TAB */
			break;

		case '\'':	/* Single Quote */
		case '\"':	/* Double Quote */
		case '\\':	/* Backslash */
			acpi_os_printf("\\%c", (int)string[i]);
			break;

		default:

			/* Check for printable character or hex escape */

			if (ACPI_IS_PRINT(string[i])) {
				/* This is a normal character */

				acpi_os_printf("%c", (int)string[i]);
			} else {
				/* All others will be Hex escapes */

				acpi_os_printf("\\x%2.2X", (s32) string[i]);
			}
			break;
		}
	}
	acpi_os_printf("\"");

	if (i == max_length && string[i]) {
		acpi_os_printf("...");
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_dword_byte_swap
 *
 * PARAMETERS:  Value           - Value to be converted
 *
 * RETURN:      u32 integer with bytes swapped
 *
 * DESCRIPTION: Convert a 32-bit value to big-endian (swap the bytes)
 *
 ******************************************************************************/

u32 acpi_ut_dword_byte_swap(u32 value)
{
	union {
		u32 value;
		u8 bytes[4];
	} out;
	union {
		u32 value;
		u8 bytes[4];
	} in;

	ACPI_FUNCTION_ENTRY();

	in.value = value;

	out.bytes[0] = in.bytes[3];
	out.bytes[1] = in.bytes[2];
	out.bytes[2] = in.bytes[1];
	out.bytes[3] = in.bytes[0];

	return (out.value);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_set_integer_width
 *
 * PARAMETERS:  Revision            From DSDT header
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set the global integer bit width based upon the revision
 *              of the DSDT.  For Revision 1 and 0, Integers are 32 bits.
 *              For Revision 2 and above, Integers are 64 bits.  Yes, this
 *              makes a difference.
 *
 ******************************************************************************/

void acpi_ut_set_integer_width(u8 revision)
{

	if (revision <= 1) {
		acpi_gbl_integer_bit_width = 32;
		acpi_gbl_integer_nybble_width = 8;
		acpi_gbl_integer_byte_width = 4;
	} else {
		acpi_gbl_integer_bit_width = 64;
		acpi_gbl_integer_nybble_width = 16;
		acpi_gbl_integer_byte_width = 8;
	}
}

#ifdef ACPI_DEBUG_OUTPUT
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_display_init_pathname
 *
 * PARAMETERS:  Type                - Object type of the node
 *              obj_handle          - Handle whose pathname will be displayed
 *              Path                - Additional path string to be appended.
 *                                      (NULL if no extra path)
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: Display full pathname of an object, DEBUG ONLY
 *
 ******************************************************************************/

void
acpi_ut_display_init_pathname(u8 type,
			      struct acpi_namespace_node *obj_handle,
			      char *path)
{
	acpi_status status;
	struct acpi_buffer buffer;

	ACPI_FUNCTION_ENTRY();

	/* Only print the path if the appropriate debug level is enabled */

	if (!(acpi_dbg_level & ACPI_LV_INIT_NAMES)) {
		return;
	}

	/* Get the full pathname to the node */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
	status = acpi_ns_handle_to_pathname(obj_handle, &buffer);
	if (ACPI_FAILURE(status)) {
		return;
	}

	/* Print what we're doing */

	switch (type) {
	case ACPI_TYPE_METHOD:
		acpi_os_printf("Executing  ");
		break;

	default:
		acpi_os_printf("Initializing ");
		break;
	}

	/* Print the object type and pathname */

	acpi_os_printf("%-12s %s",
		       acpi_ut_get_type_name(type), (char *)buffer.pointer);

	/* Extra path is used to append names like _STA, _INI, etc. */

	if (path) {
		acpi_os_printf(".%s", path);
	}
	acpi_os_printf("\n");

	ACPI_FREE(buffer.pointer);
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_acpi_char
 *
 * PARAMETERS:  Char            - The character to be examined
 *
 * RETURN:      TRUE if the character is valid, FALSE otherwise
 *
 * DESCRIPTION: Check for a valid ACPI character. Must be one of:
 *              1) Upper case alpha
 *              2) numeric
 *              3) underscore
 *
 *              We allow a '!' as the last character because of the ASF! table
 *
 ******************************************************************************/

u8 acpi_ut_valid_acpi_char(char character, acpi_native_uint position)
{

	if (!((character >= 'A' && character <= 'Z') ||
	      (character >= '0' && character <= '9') || (character == '_'))) {

		/* Allow a '!' in the last position */

		if (character == '!' && position == 3) {
			return (TRUE);
		}

		return (FALSE);
	}

	return (TRUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_acpi_name
 *
 * PARAMETERS:  Name            - The name to be examined
 *
 * RETURN:      TRUE if the name is valid, FALSE otherwise
 *
 * DESCRIPTION: Check for a valid ACPI name.  Each character must be one of:
 *              1) Upper case alpha
 *              2) numeric
 *              3) underscore
 *
 ******************************************************************************/

u8 acpi_ut_valid_acpi_name(u32 name)
{
	acpi_native_uint i;

	ACPI_FUNCTION_ENTRY();

	for (i = 0; i < ACPI_NAME_SIZE; i++) {
		if (!acpi_ut_valid_acpi_char
		    ((ACPI_CAST_PTR(char, &name))[i], i)) {
			return (FALSE);
		}
	}

	return (TRUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_repair_name
 *
 * PARAMETERS:  Name            - The ACPI name to be repaired
 *
 * RETURN:      Repaired version of the name
 *
 * DESCRIPTION: Repair an ACPI name: Change invalid characters to '*' and
 *              return the new name.
 *
 ******************************************************************************/

acpi_name acpi_ut_repair_name(acpi_name name)
{
	char *name_ptr = ACPI_CAST_PTR(char, &name);
	char new_name[ACPI_NAME_SIZE];
	acpi_native_uint i;

	for (i = 0; i < ACPI_NAME_SIZE; i++) {
		new_name[i] = name_ptr[i];

		/*
		 * Replace a bad character with something printable, yet technically
		 * still invalid. This prevents any collisions with existing "good"
		 * names in the namespace.
		 */
		if (!acpi_ut_valid_acpi_char(name_ptr[i], i)) {
			new_name[i] = '*';
		}
	}

	return (*ACPI_CAST_PTR(u32, new_name));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strtoul64
 *
 * PARAMETERS:  String          - Null terminated string
 *              Base            - Radix of the string: 10, 16, or ACPI_ANY_BASE
 *              ret_integer     - Where the converted integer is returned
 *
 * RETURN:      Status and Converted value
 *
 * DESCRIPTION: Convert a string into an unsigned value.
 *              NOTE: Does not support Octal strings, not needed.
 *
 ******************************************************************************/

acpi_status
acpi_ut_strtoul64(char *string, u32 base, acpi_integer * ret_integer)
{
	u32 this_digit = 0;
	acpi_integer return_value = 0;
	acpi_integer quotient;

	ACPI_FUNCTION_TRACE("ut_stroul64");

	if ((!string) || !(*string)) {
		goto error_exit;
	}

	switch (base) {
	case ACPI_ANY_BASE:
	case 10:
	case 16:
		break;

	default:
		/* Invalid Base */
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Skip over any white space in the buffer */

	while (ACPI_IS_SPACE(*string) || *string == '\t') {
		string++;
	}

	/*
	 * If the input parameter Base is zero, then we need to
	 * determine if it is decimal or hexadecimal:
	 */
	if (base == 0) {
		if ((*string == '0') && (ACPI_TOLOWER(*(string + 1)) == 'x')) {
			base = 16;
			string += 2;
		} else {
			base = 10;
		}
	}

	/*
	 * For hexadecimal base, skip over the leading
	 * 0 or 0x, if they are present.
	 */
	if ((base == 16) &&
	    (*string == '0') && (ACPI_TOLOWER(*(string + 1)) == 'x')) {
		string += 2;
	}

	/* Any string left? */

	if (!(*string)) {
		goto error_exit;
	}

	/* Main loop: convert the string to a 64-bit integer */

	while (*string) {
		if (ACPI_IS_DIGIT(*string)) {

			/* Convert ASCII 0-9 to Decimal value */

			this_digit = ((u8) * string) - '0';
		} else {
			if (base == 10) {

				/* Digit is out of range */

				goto error_exit;
			}

			this_digit = (u8) ACPI_TOUPPER(*string);
			if (ACPI_IS_XDIGIT((char)this_digit)) {

				/* Convert ASCII Hex char to value */

				this_digit = this_digit - 'A' + 10;
			} else {
				/*
				 * We allow non-hex chars, just stop now, same as end-of-string.
				 * See ACPI spec, string-to-integer conversion.
				 */
				break;
			}
		}

		/* Divide the digit into the correct position */

		(void)
		    acpi_ut_short_divide((ACPI_INTEGER_MAX -
					  (acpi_integer) this_digit), base,
					 &quotient, NULL);
		if (return_value > quotient) {
			goto error_exit;
		}

		return_value *= base;
		return_value += this_digit;
		string++;
	}

	/* All done, normal exit */

	*ret_integer = return_value;
	return_ACPI_STATUS(AE_OK);

      error_exit:
	/* Base was set/validated above */

	if (base == 10) {
		return_ACPI_STATUS(AE_BAD_DECIMAL_CONSTANT);
	} else {
		return_ACPI_STATUS(AE_BAD_HEX_CONSTANT);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_update_state_and_push
 *
 * PARAMETERS:  Object          - Object to be added to the new state
 *              Action          - Increment/Decrement
 *              state_list      - List the state will be added to
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new state and push it
 *
 ******************************************************************************/

acpi_status
acpi_ut_create_update_state_and_push(union acpi_operand_object *object,
				     u16 action,
				     union acpi_generic_state **state_list)
{
	union acpi_generic_state *state;

	ACPI_FUNCTION_ENTRY();

	/* Ignore null objects; these are expected */

	if (!object) {
		return (AE_OK);
	}

	state = acpi_ut_create_update_state(object, action);
	if (!state) {
		return (AE_NO_MEMORY);
	}

	acpi_ut_push_generic_state(state_list, state);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_walk_package_tree
 *
 * PARAMETERS:  source_object       - The package to walk
 *              target_object       - Target object (if package is being copied)
 *              walk_callback       - Called once for each package element
 *              Context             - Passed to the callback function
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk through a package
 *
 ******************************************************************************/

acpi_status
acpi_ut_walk_package_tree(union acpi_operand_object * source_object,
			  void *target_object,
			  acpi_pkg_callback walk_callback, void *context)
{
	acpi_status status = AE_OK;
	union acpi_generic_state *state_list = NULL;
	union acpi_generic_state *state;
	u32 this_index;
	union acpi_operand_object *this_source_obj;

	ACPI_FUNCTION_TRACE("ut_walk_package_tree");

	state = acpi_ut_create_pkg_state(source_object, target_object, 0);
	if (!state) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	while (state) {

		/* Get one element of the package */

		this_index = state->pkg.index;
		this_source_obj = (union acpi_operand_object *)
		    state->pkg.source_object->package.elements[this_index];

		/*
		 * Check for:
		 * 1) An uninitialized package element.  It is completely
		 *    legal to declare a package and leave it uninitialized
		 * 2) Not an internal object - can be a namespace node instead
		 * 3) Any type other than a package.  Packages are handled in else
		 *    case below.
		 */
		if ((!this_source_obj) ||
		    (ACPI_GET_DESCRIPTOR_TYPE(this_source_obj) !=
		     ACPI_DESC_TYPE_OPERAND)
		    || (ACPI_GET_OBJECT_TYPE(this_source_obj) !=
			ACPI_TYPE_PACKAGE)) {
			status =
			    walk_callback(ACPI_COPY_TYPE_SIMPLE,
					  this_source_obj, state, context);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}

			state->pkg.index++;
			while (state->pkg.index >=
			       state->pkg.source_object->package.count) {
				/*
				 * We've handled all of the objects at this level,  This means
				 * that we have just completed a package.  That package may
				 * have contained one or more packages itself.
				 *
				 * Delete this state and pop the previous state (package).
				 */
				acpi_ut_delete_generic_state(state);
				state = acpi_ut_pop_generic_state(&state_list);

				/* Finished when there are no more states */

				if (!state) {
					/*
					 * We have handled all of the objects in the top level
					 * package just add the length of the package objects
					 * and exit
					 */
					return_ACPI_STATUS(AE_OK);
				}

				/*
				 * Go back up a level and move the index past the just
				 * completed package object.
				 */
				state->pkg.index++;
			}
		} else {
			/* This is a subobject of type package */

			status =
			    walk_callback(ACPI_COPY_TYPE_PACKAGE,
					  this_source_obj, state, context);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}

			/*
			 * Push the current state and create a new one
			 * The callback above returned a new target package object.
			 */
			acpi_ut_push_generic_state(&state_list, state);
			state = acpi_ut_create_pkg_state(this_source_obj,
							 state->pkg.
							 this_target_obj, 0);
			if (!state) {
				return_ACPI_STATUS(AE_NO_MEMORY);
			}
		}
	}

	/* We should never get here */

	return_ACPI_STATUS(AE_AML_INTERNAL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_error, acpi_ut_warning, acpi_ut_info
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              Format              - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print message with module/line/version info
 *
 ******************************************************************************/

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_error(char *module_name, u32 line_number, char *format, ...)
{
	va_list args;

	acpi_os_printf("ACPI Error (%s-%04d): ", module_name, line_number);

	va_start(args, format);
	acpi_os_vprintf(format, args);
	acpi_os_printf(" [%X]\n", ACPI_CA_VERSION);
}

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_exception(char *module_name,
		  u32 line_number, acpi_status status, char *format, ...)
{
	va_list args;

	acpi_os_printf("ACPI Exception (%s-%04d): %s, ", module_name,
		       line_number, acpi_format_exception(status));

	va_start(args, format);
	acpi_os_vprintf(format, args);
	acpi_os_printf(" [%X]\n", ACPI_CA_VERSION);
}

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_warning(char *module_name, u32 line_number, char *format, ...)
{
	va_list args;

	acpi_os_printf("ACPI Warning (%s-%04d): ", module_name, line_number);

	va_start(args, format);
	acpi_os_vprintf(format, args);
	acpi_os_printf(" [%X]\n", ACPI_CA_VERSION);
}

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_info(char *module_name, u32 line_number, char *format, ...)
{
	va_list args;

	acpi_os_printf("ACPI (%s-%04d): ", module_name, line_number);

	va_start(args, format);
	acpi_os_vprintf(format, args);
	acpi_os_printf(" [%X]\n", ACPI_CA_VERSION);
}
