/*******************************************************************************
 *
 * Module Name: utmisc - common utility procedures
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2005, R. Byron Moore
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
	 ACPI_MODULE_NAME    ("utmisc")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_print_string
 *
 * PARAMETERS:  String          - Null terminated ASCII string
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump an ASCII string with support for ACPI-defined escape
 *              sequences.
 *
 ******************************************************************************/

void
acpi_ut_print_string (
	char                            *string,
	u8                              max_length)
{
	u32                             i;


	if (!string) {
		acpi_os_printf ("<\"NULL STRING PTR\">");
		return;
	}

	acpi_os_printf ("\"");
	for (i = 0; string[i] && (i < max_length); i++) {
		/* Escape sequences */

		switch (string[i]) {
		case 0x07:
			acpi_os_printf ("\\a");      /* BELL */
			break;

		case 0x08:
			acpi_os_printf ("\\b");     /* BACKSPACE */
			break;

		case 0x0C:
			acpi_os_printf ("\\f");     /* FORMFEED */
			break;

		case 0x0A:
			acpi_os_printf ("\\n");     /* LINEFEED */
			break;

		case 0x0D:
			acpi_os_printf ("\\r");     /* CARRIAGE RETURN*/
			break;

		case 0x09:
			acpi_os_printf ("\\t");     /* HORIZONTAL TAB */
			break;

		case 0x0B:
			acpi_os_printf ("\\v");     /* VERTICAL TAB */
			break;

		case '\'':                      /* Single Quote */
		case '\"':                      /* Double Quote */
		case '\\':                      /* Backslash */
			acpi_os_printf ("\\%c", (int) string[i]);
			break;

		default:

			/* Check for printable character or hex escape */

			if (ACPI_IS_PRINT (string[i]))
			{
				/* This is a normal character */

				acpi_os_printf ("%c", (int) string[i]);
			}
			else
			{
				/* All others will be Hex escapes */

				acpi_os_printf ("\\x%2.2X", (s32) string[i]);
			}
			break;
		}
	}
	acpi_os_printf ("\"");

	if (i == max_length && string[i]) {
		acpi_os_printf ("...");
	}
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_dword_byte_swap
 *
 * PARAMETERS:  Value           - Value to be converted
 *
 * DESCRIPTION: Convert a 32-bit value to big-endian (swap the bytes)
 *
 ******************************************************************************/

u32
acpi_ut_dword_byte_swap (
	u32                             value)
{
	union {
		u32                         value;
		u8                          bytes[4];
	} out;

	union {
		u32                         value;
		u8                          bytes[4];
	} in;


	ACPI_FUNCTION_ENTRY ();


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

void
acpi_ut_set_integer_width (
	u8                              revision)
{

	if (revision <= 1) {
		acpi_gbl_integer_bit_width = 32;
		acpi_gbl_integer_nybble_width = 8;
		acpi_gbl_integer_byte_width = 4;
	}
	else {
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
 * PARAMETERS:  obj_handle          - Handle whose pathname will be displayed
 *              Path                - Additional path string to be appended.
 *                                      (NULL if no extra path)
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: Display full pathname of an object, DEBUG ONLY
 *
 ******************************************************************************/

void
acpi_ut_display_init_pathname (
	u8                              type,
	struct acpi_namespace_node      *obj_handle,
	char                            *path)
{
	acpi_status                     status;
	struct acpi_buffer              buffer;


	ACPI_FUNCTION_ENTRY ();


	/* Only print the path if the appropriate debug level is enabled */

	if (!(acpi_dbg_level & ACPI_LV_INIT_NAMES)) {
		return;
	}

	/* Get the full pathname to the node */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
	status = acpi_ns_handle_to_pathname (obj_handle, &buffer);
	if (ACPI_FAILURE (status)) {
		return;
	}

	/* Print what we're doing */

	switch (type) {
	case ACPI_TYPE_METHOD:
		acpi_os_printf ("Executing  ");
		break;

	default:
		acpi_os_printf ("Initializing ");
		break;
	}

	/* Print the object type and pathname */

	acpi_os_printf ("%-12s %s", acpi_ut_get_type_name (type), (char *) buffer.pointer);

	/* Extra path is used to append names like _STA, _INI, etc. */

	if (path) {
		acpi_os_printf (".%s", path);
	}
	acpi_os_printf ("\n");

	ACPI_MEM_FREE (buffer.pointer);
}
#endif


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_acpi_name
 *
 * PARAMETERS:  Character           - The character to be examined
 *
 * RETURN:      1 if Character may appear in a name, else 0
 *
 * DESCRIPTION: Check for a valid ACPI name.  Each character must be one of:
 *              1) Upper case alpha
 *              2) numeric
 *              3) underscore
 *
 ******************************************************************************/

u8
acpi_ut_valid_acpi_name (
	u32                             name)
{
	char                            *name_ptr = (char *) &name;
	char                            character;
	acpi_native_uint                i;


	ACPI_FUNCTION_ENTRY ();


	for (i = 0; i < ACPI_NAME_SIZE; i++) {
		character = *name_ptr;
		name_ptr++;

		if (!((character == '_') ||
			  (character >= 'A' && character <= 'Z') ||
			  (character >= '0' && character <= '9'))) {
			return (FALSE);
		}
	}

	return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_acpi_character
 *
 * PARAMETERS:  Character           - The character to be examined
 *
 * RETURN:      1 if Character may appear in a name, else 0
 *
 * DESCRIPTION: Check for a printable character
 *
 ******************************************************************************/

u8
acpi_ut_valid_acpi_character (
	char                            character)
{

	ACPI_FUNCTION_ENTRY ();

	return ((u8)   ((character == '_') ||
			   (character >= 'A' && character <= 'Z') ||
			   (character >= '0' && character <= '9')));
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
acpi_ut_strtoul64 (
	char                            *string,
	u32                             base,
	acpi_integer                    *ret_integer)
{
	u32                             this_digit = 0;
	acpi_integer                    return_value = 0;
	acpi_integer                    quotient;


	ACPI_FUNCTION_TRACE ("ut_stroul64");


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
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Skip over any white space in the buffer */

	while (ACPI_IS_SPACE (*string) || *string == '\t') {
		string++;
	}

	/*
	 * If the input parameter Base is zero, then we need to
	 * determine if it is decimal or hexadecimal:
	 */
	if (base == 0) {
		if ((*string == '0') &&
			(ACPI_TOLOWER (*(string + 1)) == 'x')) {
			base = 16;
			string += 2;
		}
		else {
			base = 10;
		}
	}

	/*
	 * For hexadecimal base, skip over the leading
	 * 0 or 0x, if they are present.
	 */
	if ((base == 16) &&
		(*string == '0') &&
		(ACPI_TOLOWER (*(string + 1)) == 'x')) {
		string += 2;
	}

	/* Any string left? */

	if (!(*string)) {
		goto error_exit;
	}

	/* Main loop: convert the string to a 64-bit integer */

	while (*string) {
		if (ACPI_IS_DIGIT (*string)) {
			/* Convert ASCII 0-9 to Decimal value */

			this_digit = ((u8) *string) - '0';
		}
		else {
			if (base == 10) {
				/* Digit is out of range */

				goto error_exit;
			}

			this_digit = (u8) ACPI_TOUPPER (*string);
			if (ACPI_IS_XDIGIT ((char) this_digit)) {
				/* Convert ASCII Hex char to value */

				this_digit = this_digit - 'A' + 10;
			}
			else {
				/*
				 * We allow non-hex chars, just stop now, same as end-of-string.
				 * See ACPI spec, string-to-integer conversion.
				 */
				break;
			}
		}

		/* Divide the digit into the correct position */

		(void) acpi_ut_short_divide ((ACPI_INTEGER_MAX - (acpi_integer) this_digit),
				 base, &quotient, NULL);
		if (return_value > quotient) {
			goto error_exit;
		}

		return_value *= base;
		return_value += this_digit;
		string++;
	}

	/* All done, normal exit */

	*ret_integer = return_value;
	return_ACPI_STATUS (AE_OK);


error_exit:
	/* Base was set/validated above */

	if (base == 10) {
		return_ACPI_STATUS (AE_BAD_DECIMAL_CONSTANT);
	}
	else {
		return_ACPI_STATUS (AE_BAD_HEX_CONSTANT);
	}
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_strupr
 *
 * PARAMETERS:  src_string      - The source string to convert to
 *
 * RETURN:      src_string
 *
 * DESCRIPTION: Convert string to uppercase
 *
 ******************************************************************************/
#ifdef ACPI_FUTURE_USAGE
char *
acpi_ut_strupr (
	char                            *src_string)
{
	char                            *string;


	ACPI_FUNCTION_ENTRY ();


	/* Walk entire string, uppercasing the letters */

	for (string = src_string; *string; ) {
		*string = (char) ACPI_TOUPPER (*string);
		string++;
	}

	return (src_string);
}
#endif  /*  ACPI_FUTURE_USAGE  */


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_mutex_initialize
 *
 * PARAMETERS:  None.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create the system mutex objects.
 *
 ******************************************************************************/

acpi_status
acpi_ut_mutex_initialize (
	void)
{
	u32                             i;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("ut_mutex_initialize");


	/*
	 * Create each of the predefined mutex objects
	 */
	for (i = 0; i < NUM_MUTEX; i++) {
		status = acpi_ut_create_mutex (i);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	status = acpi_os_create_lock (&acpi_gbl_gpe_lock);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_mutex_terminate
 *
 * PARAMETERS:  None.
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete all of the system mutex objects.
 *
 ******************************************************************************/

void
acpi_ut_mutex_terminate (
	void)
{
	u32                             i;


	ACPI_FUNCTION_TRACE ("ut_mutex_terminate");


	/*
	 * Delete each predefined mutex object
	 */
	for (i = 0; i < NUM_MUTEX; i++) {
		(void) acpi_ut_delete_mutex (i);
	}

	acpi_os_delete_lock (acpi_gbl_gpe_lock);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_mutex
 *
 * PARAMETERS:  mutex_iD        - ID of the mutex to be created
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a mutex object.
 *
 ******************************************************************************/

acpi_status
acpi_ut_create_mutex (
	acpi_mutex_handle               mutex_id)
{
	acpi_status                     status = AE_OK;


	ACPI_FUNCTION_TRACE_U32 ("ut_create_mutex", mutex_id);


	if (mutex_id > MAX_MUTEX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (!acpi_gbl_mutex_info[mutex_id].mutex) {
		status = acpi_os_create_semaphore (1, 1,
				  &acpi_gbl_mutex_info[mutex_id].mutex);
		acpi_gbl_mutex_info[mutex_id].owner_id = ACPI_MUTEX_NOT_ACQUIRED;
		acpi_gbl_mutex_info[mutex_id].use_count = 0;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_delete_mutex
 *
 * PARAMETERS:  mutex_iD        - ID of the mutex to be deleted
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete a mutex object.
 *
 ******************************************************************************/

acpi_status
acpi_ut_delete_mutex (
	acpi_mutex_handle               mutex_id)
{
	acpi_status                     status;


	ACPI_FUNCTION_TRACE_U32 ("ut_delete_mutex", mutex_id);


	if (mutex_id > MAX_MUTEX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	status = acpi_os_delete_semaphore (acpi_gbl_mutex_info[mutex_id].mutex);

	acpi_gbl_mutex_info[mutex_id].mutex = NULL;
	acpi_gbl_mutex_info[mutex_id].owner_id = ACPI_MUTEX_NOT_ACQUIRED;

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_acquire_mutex
 *
 * PARAMETERS:  mutex_iD        - ID of the mutex to be acquired
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire a mutex object.
 *
 ******************************************************************************/

acpi_status
acpi_ut_acquire_mutex (
	acpi_mutex_handle               mutex_id)
{
	acpi_status                     status;
	u32                             this_thread_id;


	ACPI_FUNCTION_NAME ("ut_acquire_mutex");


	if (mutex_id > MAX_MUTEX) {
		return (AE_BAD_PARAMETER);
	}

	this_thread_id = acpi_os_get_thread_id ();

#ifdef ACPI_MUTEX_DEBUG
	{
		u32                             i;
		/*
		 * Mutex debug code, for internal debugging only.
		 *
		 * Deadlock prevention.  Check if this thread owns any mutexes of value
		 * greater than or equal to this one.  If so, the thread has violated
		 * the mutex ordering rule.  This indicates a coding error somewhere in
		 * the ACPI subsystem code.
		 */
		for (i = mutex_id; i < MAX_MUTEX; i++) {
			if (acpi_gbl_mutex_info[i].owner_id == this_thread_id) {
				if (i == mutex_id) {
					ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
							"Mutex [%s] already acquired by this thread [%X]\n",
							acpi_ut_get_mutex_name (mutex_id), this_thread_id));

					return (AE_ALREADY_ACQUIRED);
				}

				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
						"Invalid acquire order: Thread %X owns [%s], wants [%s]\n",
						this_thread_id, acpi_ut_get_mutex_name (i),
						acpi_ut_get_mutex_name (mutex_id)));

				return (AE_ACQUIRE_DEADLOCK);
			}
		}
	}
#endif

	ACPI_DEBUG_PRINT ((ACPI_DB_MUTEX,
			 "Thread %X attempting to acquire Mutex [%s]\n",
			 this_thread_id, acpi_ut_get_mutex_name (mutex_id)));

	status = acpi_os_wait_semaphore (acpi_gbl_mutex_info[mutex_id].mutex,
			   1, ACPI_WAIT_FOREVER);
	if (ACPI_SUCCESS (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_MUTEX, "Thread %X acquired Mutex [%s]\n",
				 this_thread_id, acpi_ut_get_mutex_name (mutex_id)));

		acpi_gbl_mutex_info[mutex_id].use_count++;
		acpi_gbl_mutex_info[mutex_id].owner_id = this_thread_id;
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Thread %X could not acquire Mutex [%s] %s\n",
				 this_thread_id, acpi_ut_get_mutex_name (mutex_id),
				 acpi_format_exception (status)));
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_release_mutex
 *
 * PARAMETERS:  mutex_iD        - ID of the mutex to be released
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release a mutex object.
 *
 ******************************************************************************/

acpi_status
acpi_ut_release_mutex (
	acpi_mutex_handle               mutex_id)
{
	acpi_status                     status;
	u32                             i;
	u32                             this_thread_id;


	ACPI_FUNCTION_NAME ("ut_release_mutex");


	this_thread_id = acpi_os_get_thread_id ();
	ACPI_DEBUG_PRINT ((ACPI_DB_MUTEX,
		"Thread %X releasing Mutex [%s]\n", this_thread_id,
		acpi_ut_get_mutex_name (mutex_id)));

	if (mutex_id > MAX_MUTEX) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Mutex must be acquired in order to release it!
	 */
	if (acpi_gbl_mutex_info[mutex_id].owner_id == ACPI_MUTEX_NOT_ACQUIRED) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Mutex [%s] is not acquired, cannot release\n",
				acpi_ut_get_mutex_name (mutex_id)));

		return (AE_NOT_ACQUIRED);
	}

	/*
	 * Deadlock prevention.  Check if this thread owns any mutexes of value
	 * greater than this one.  If so, the thread has violated the mutex
	 * ordering rule.  This indicates a coding error somewhere in
	 * the ACPI subsystem code.
	 */
	for (i = mutex_id; i < MAX_MUTEX; i++) {
		if (acpi_gbl_mutex_info[i].owner_id == this_thread_id) {
			if (i == mutex_id) {
				continue;
			}

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"Invalid release order: owns [%s], releasing [%s]\n",
					acpi_ut_get_mutex_name (i), acpi_ut_get_mutex_name (mutex_id)));

			return (AE_RELEASE_DEADLOCK);
		}
	}

	/* Mark unlocked FIRST */

	acpi_gbl_mutex_info[mutex_id].owner_id = ACPI_MUTEX_NOT_ACQUIRED;

	status = acpi_os_signal_semaphore (acpi_gbl_mutex_info[mutex_id].mutex, 1);

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Thread %X could not release Mutex [%s] %s\n",
				 this_thread_id, acpi_ut_get_mutex_name (mutex_id),
				 acpi_format_exception (status)));
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_MUTEX, "Thread %X released Mutex [%s]\n",
				 this_thread_id, acpi_ut_get_mutex_name (mutex_id)));
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_update_state_and_push
 *
 * PARAMETERS:  *Object         - Object to be added to the new state
 *              Action          - Increment/Decrement
 *              state_list      - List the state will be added to
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create a new state and push it
 *
 ******************************************************************************/

acpi_status
acpi_ut_create_update_state_and_push (
	union acpi_operand_object       *object,
	u16                             action,
	union acpi_generic_state        **state_list)
{
	union acpi_generic_state         *state;


	ACPI_FUNCTION_ENTRY ();


	/* Ignore null objects; these are expected */

	if (!object) {
		return (AE_OK);
	}

	state = acpi_ut_create_update_state (object, action);
	if (!state) {
		return (AE_NO_MEMORY);
	}

	acpi_ut_push_generic_state (state_list, state);
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_pkg_state_and_push
 *
 * PARAMETERS:  *Object         - Object to be added to the new state
 *              Action          - Increment/Decrement
 *              state_list      - List the state will be added to
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create a new state and push it
 *
 ******************************************************************************/
#ifdef ACPI_FUTURE_USAGE
acpi_status
acpi_ut_create_pkg_state_and_push (
	void                            *internal_object,
	void                            *external_object,
	u16                             index,
	union acpi_generic_state        **state_list)
{
	union acpi_generic_state         *state;


	ACPI_FUNCTION_ENTRY ();


	state = acpi_ut_create_pkg_state (internal_object, external_object, index);
	if (!state) {
		return (AE_NO_MEMORY);
	}

	acpi_ut_push_generic_state (state_list, state);
	return (AE_OK);
}
#endif  /*  ACPI_FUTURE_USAGE  */

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_push_generic_state
 *
 * PARAMETERS:  list_head           - Head of the state stack
 *              State               - State object to push
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push a state object onto a state stack
 *
 ******************************************************************************/

void
acpi_ut_push_generic_state (
	union acpi_generic_state        **list_head,
	union acpi_generic_state        *state)
{
	ACPI_FUNCTION_TRACE ("ut_push_generic_state");


	/* Push the state object onto the front of the list (stack) */

	state->common.next = *list_head;
	*list_head = state;

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_pop_generic_state
 *
 * PARAMETERS:  list_head           - Head of the state stack
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop a state object from a state stack
 *
 ******************************************************************************/

union acpi_generic_state *
acpi_ut_pop_generic_state (
	union acpi_generic_state        **list_head)
{
	union acpi_generic_state        *state;


	ACPI_FUNCTION_TRACE ("ut_pop_generic_state");


	/* Remove the state object at the head of the list (stack) */

	state = *list_head;
	if (state) {
		/* Update the list head */

		*list_head = state->common.next;
	}

	return_PTR (state);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_generic_state
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a generic state object.  Attempt to obtain one from
 *              the global state cache;  If none available, create a new one.
 *
 ******************************************************************************/

union acpi_generic_state *
acpi_ut_create_generic_state (void)
{
	union acpi_generic_state        *state;


	ACPI_FUNCTION_ENTRY ();


	state = acpi_ut_acquire_from_cache (ACPI_MEM_LIST_STATE);

	/* Initialize */

	if (state) {
		state->common.data_type = ACPI_DESC_TYPE_STATE;
	}

	return (state);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_thread_state
 *
 * PARAMETERS:  None
 *
 * RETURN:      Thread State
 *
 * DESCRIPTION: Create a "Thread State" - a flavor of the generic state used
 *              to track per-thread info during method execution
 *
 ******************************************************************************/

struct acpi_thread_state *
acpi_ut_create_thread_state (
	void)
{
	union acpi_generic_state        *state;


	ACPI_FUNCTION_TRACE ("ut_create_thread_state");


	/* Create the generic state object */

	state = acpi_ut_create_generic_state ();
	if (!state) {
		return_PTR (NULL);
	}

	/* Init fields specific to the update struct */

	state->common.data_type = ACPI_DESC_TYPE_STATE_THREAD;
	state->thread.thread_id = acpi_os_get_thread_id ();

	return_PTR ((struct acpi_thread_state *) state);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_update_state
 *
 * PARAMETERS:  Object              - Initial Object to be installed in the
 *                                    state
 *              Action              - Update action to be performed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create an "Update State" - a flavor of the generic state used
 *              to update reference counts and delete complex objects such
 *              as packages.
 *
 ******************************************************************************/

union acpi_generic_state *
acpi_ut_create_update_state (
	union acpi_operand_object       *object,
	u16                             action)
{
	union acpi_generic_state        *state;


	ACPI_FUNCTION_TRACE_PTR ("ut_create_update_state", object);


	/* Create the generic state object */

	state = acpi_ut_create_generic_state ();
	if (!state) {
		return_PTR (NULL);
	}

	/* Init fields specific to the update struct */

	state->common.data_type = ACPI_DESC_TYPE_STATE_UPDATE;
	state->update.object = object;
	state->update.value  = action;

	return_PTR (state);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_pkg_state
 *
 * PARAMETERS:  Object              - Initial Object to be installed in the
 *                                    state
 *              Action              - Update action to be performed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a "Package State"
 *
 ******************************************************************************/

union acpi_generic_state *
acpi_ut_create_pkg_state (
	void                            *internal_object,
	void                            *external_object,
	u16                             index)
{
	union acpi_generic_state        *state;


	ACPI_FUNCTION_TRACE_PTR ("ut_create_pkg_state", internal_object);


	/* Create the generic state object */

	state = acpi_ut_create_generic_state ();
	if (!state) {
		return_PTR (NULL);
	}

	/* Init fields specific to the update struct */

	state->common.data_type = ACPI_DESC_TYPE_STATE_PACKAGE;
	state->pkg.source_object = (union acpi_operand_object *) internal_object;
	state->pkg.dest_object  = external_object;
	state->pkg.index        = index;
	state->pkg.num_packages = 1;

	return_PTR (state);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_control_state
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a "Control State" - a flavor of the generic state used
 *              to support nested IF/WHILE constructs in the AML.
 *
 ******************************************************************************/

union acpi_generic_state *
acpi_ut_create_control_state (
	void)
{
	union acpi_generic_state        *state;


	ACPI_FUNCTION_TRACE ("ut_create_control_state");


	/* Create the generic state object */

	state = acpi_ut_create_generic_state ();
	if (!state) {
		return_PTR (NULL);
	}

	/* Init fields specific to the control struct */

	state->common.data_type = ACPI_DESC_TYPE_STATE_CONTROL;
	state->common.state     = ACPI_CONTROL_CONDITIONAL_EXECUTING;

	return_PTR (state);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_delete_generic_state
 *
 * PARAMETERS:  State               - The state object to be deleted
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Put a state object back into the global state cache.  The object
 *              is not actually freed at this time.
 *
 ******************************************************************************/

void
acpi_ut_delete_generic_state (
	union acpi_generic_state        *state)
{
	ACPI_FUNCTION_TRACE ("ut_delete_generic_state");


	acpi_ut_release_to_cache (ACPI_MEM_LIST_STATE, state);
	return_VOID;
}


#ifdef ACPI_ENABLE_OBJECT_CACHE
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_delete_generic_state_cache
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Purge the global state object cache.  Used during subsystem
 *              termination.
 *
 ******************************************************************************/

void
acpi_ut_delete_generic_state_cache (
	void)
{
	ACPI_FUNCTION_TRACE ("ut_delete_generic_state_cache");


	acpi_ut_delete_generic_cache (ACPI_MEM_LIST_STATE);
	return_VOID;
}
#endif


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_walk_package_tree
 *
 * PARAMETERS:  obj_desc        - The Package object on which to resolve refs
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk through a package
 *
 ******************************************************************************/

acpi_status
acpi_ut_walk_package_tree (
	union acpi_operand_object       *source_object,
	void                            *target_object,
	acpi_pkg_callback               walk_callback,
	void                            *context)
{
	acpi_status                     status = AE_OK;
	union acpi_generic_state        *state_list = NULL;
	union acpi_generic_state        *state;
	u32                             this_index;
	union acpi_operand_object       *this_source_obj;


	ACPI_FUNCTION_TRACE ("ut_walk_package_tree");


	state = acpi_ut_create_pkg_state (source_object, target_object, 0);
	if (!state) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	while (state) {
		/* Get one element of the package */

		this_index    = state->pkg.index;
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
			(ACPI_GET_DESCRIPTOR_TYPE (this_source_obj) != ACPI_DESC_TYPE_OPERAND) ||
			(ACPI_GET_OBJECT_TYPE (this_source_obj) != ACPI_TYPE_PACKAGE)) {
			status = walk_callback (ACPI_COPY_TYPE_SIMPLE, this_source_obj,
					 state, context);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			state->pkg.index++;
			while (state->pkg.index >= state->pkg.source_object->package.count) {
				/*
				 * We've handled all of the objects at this level,  This means
				 * that we have just completed a package.  That package may
				 * have contained one or more packages itself.
				 *
				 * Delete this state and pop the previous state (package).
				 */
				acpi_ut_delete_generic_state (state);
				state = acpi_ut_pop_generic_state (&state_list);

				/* Finished when there are no more states */

				if (!state) {
					/*
					 * We have handled all of the objects in the top level
					 * package just add the length of the package objects
					 * and exit
					 */
					return_ACPI_STATUS (AE_OK);
				}

				/*
				 * Go back up a level and move the index past the just
				 * completed package object.
				 */
				state->pkg.index++;
			}
		}
		else {
			/* This is a subobject of type package */

			status = walk_callback (ACPI_COPY_TYPE_PACKAGE, this_source_obj,
					  state, context);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/*
			 * Push the current state and create a new one
			 * The callback above returned a new target package object.
			 */
			acpi_ut_push_generic_state (&state_list, state);
			state = acpi_ut_create_pkg_state (this_source_obj,
					   state->pkg.this_target_obj, 0);
			if (!state) {
				return_ACPI_STATUS (AE_NO_MEMORY);
			}
		}
	}

	/* We should never get here */

	return_ACPI_STATUS (AE_AML_INTERNAL);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_generate_checksum
 *
 * PARAMETERS:  Buffer          - Buffer to be scanned
 *              Length          - number of bytes to examine
 *
 * RETURN:      checksum
 *
 * DESCRIPTION: Generate a checksum on a raw buffer
 *
 ******************************************************************************/

u8
acpi_ut_generate_checksum (
	u8                              *buffer,
	u32                             length)
{
	u32                             i;
	signed char                     sum = 0;


	for (i = 0; i < length; i++) {
		sum = (signed char) (sum + buffer[i]);
	}

	return ((u8) (0 - sum));
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_resource_end_tag
 *
 * PARAMETERS:  obj_desc        - The resource template buffer object
 *
 * RETURN:      Pointer to the end tag
 *
 * DESCRIPTION: Find the END_TAG resource descriptor in a resource template
 *
 ******************************************************************************/


u8 *
acpi_ut_get_resource_end_tag (
	union acpi_operand_object       *obj_desc)
{
	u8                              buffer_byte;
	u8                              *buffer;
	u8                              *end_buffer;


	buffer    = obj_desc->buffer.pointer;
	end_buffer = buffer + obj_desc->buffer.length;

	while (buffer < end_buffer) {
		buffer_byte = *buffer;
		if (buffer_byte & ACPI_RDESC_TYPE_MASK) {
			/* Large Descriptor - Length is next 2 bytes */

			buffer += ((*(buffer+1) | (*(buffer+2) << 8)) + 3);
		}
		else {
			/* Small Descriptor.  End Tag will be found here */

			if ((buffer_byte & ACPI_RDESC_SMALL_MASK) == ACPI_RDESC_TYPE_END_TAG) {
				/* Found the end tag descriptor, all done. */

				return (buffer);
			}

			/* Length is in the header */

			buffer += ((buffer_byte & 0x07) + 1);
		}
	}

	/* End tag not found */

	return (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_report_error
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message
 *
 ******************************************************************************/

void
acpi_ut_report_error (
	char                            *module_name,
	u32                             line_number,
	u32                             component_id)
{


	acpi_os_printf ("%8s-%04d: *** Error: ", module_name, line_number);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_report_warning
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print warning message
 *
 ******************************************************************************/

void
acpi_ut_report_warning (
	char                            *module_name,
	u32                             line_number,
	u32                             component_id)
{

	acpi_os_printf ("%8s-%04d: *** Warning: ", module_name, line_number);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_report_info
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print information message
 *
 ******************************************************************************/

void
acpi_ut_report_info (
	char                            *module_name,
	u32                             line_number,
	u32                             component_id)
{

	acpi_os_printf ("%8s-%04d: *** Info: ", module_name, line_number);
}


