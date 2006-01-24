/******************************************************************************
 *
 * Module Name: utdebug - Debug print routines
 *
 *****************************************************************************/

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

#include <linux/module.h>

#include <acpi/acpi.h>

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utdebug")

#ifdef ACPI_DEBUG_OUTPUT
static u32 acpi_gbl_prev_thread_id = 0xFFFFFFFF;
static char *acpi_gbl_fn_entry_str = "----Entry";
static char *acpi_gbl_fn_exit_str = "----Exit-";

/* Local prototypes */

static const char *acpi_ut_trim_function_name(const char *function_name);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_init_stack_ptr_trace
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Save the current CPU stack pointer at subsystem startup
 *
 ******************************************************************************/

void acpi_ut_init_stack_ptr_trace(void)
{
	u32 current_sp;

	acpi_gbl_entry_stack_pointer = ACPI_PTR_DIFF(&current_sp, NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_track_stack_ptr
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Save the current CPU stack pointer
 *
 ******************************************************************************/

void acpi_ut_track_stack_ptr(void)
{
	acpi_size current_sp;

	current_sp = ACPI_PTR_DIFF(&current_sp, NULL);

	if (current_sp < acpi_gbl_lowest_stack_pointer) {
		acpi_gbl_lowest_stack_pointer = current_sp;
	}

	if (acpi_gbl_nesting_level > acpi_gbl_deepest_nesting) {
		acpi_gbl_deepest_nesting = acpi_gbl_nesting_level;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_trim_function_name
 *
 * PARAMETERS:  function_name       - Ascii string containing a procedure name
 *
 * RETURN:      Updated pointer to the function name
 *
 * DESCRIPTION: Remove the "Acpi" prefix from the function name, if present.
 *              This allows compiler macros such as __FUNCTION__ to be used
 *              with no change to the debug output.
 *
 ******************************************************************************/

static const char *acpi_ut_trim_function_name(const char *function_name)
{

	/* All Function names are longer than 4 chars, check is safe */

	if (*(ACPI_CAST_PTR(u32, function_name)) == ACPI_PREFIX_MIXED) {
		/* This is the case where the original source has not been modified */

		return (function_name + 4);
	}

	if (*(ACPI_CAST_PTR(u32, function_name)) == ACPI_PREFIX_LOWER) {
		/* This is the case where the source has been 'linuxized' */

		return (function_name + 5);
	}

	return (function_name);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_debug_print
 *
 * PARAMETERS:  requested_debug_level - Requested debug print level
 *              line_number         - Caller's line number (for error output)
 *              function_name       - Caller's procedure name
 *              module_name         - Caller's module name
 *              component_id        - Caller's component ID
 *              Format              - Printf format field
 *              ...                 - Optional printf arguments
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message with prefix consisting of the module name,
 *              line number, and component ID.
 *
 ******************************************************************************/

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_debug_print(u32 requested_debug_level,
		    u32 line_number,
		    const char *function_name,
		    char *module_name, u32 component_id, char *format, ...)
{
	u32 thread_id;
	va_list args;

	/*
	 * Stay silent if the debug level or component ID is disabled
	 */
	if (!(requested_debug_level & acpi_dbg_level) ||
	    !(component_id & acpi_dbg_layer)) {
		return;
	}

	/*
	 * Thread tracking and context switch notification
	 */
	thread_id = acpi_os_get_thread_id();

	if (thread_id != acpi_gbl_prev_thread_id) {
		if (ACPI_LV_THREADS & acpi_dbg_level) {
			acpi_os_printf
			    ("\n**** Context Switch from TID %X to TID %X ****\n\n",
			     acpi_gbl_prev_thread_id, thread_id);
		}

		acpi_gbl_prev_thread_id = thread_id;
	}

	/*
	 * Display the module name, current line number, thread ID (if requested),
	 * current procedure nesting level, and the current procedure name
	 */
	acpi_os_printf("%8s-%04ld ", module_name, line_number);

	if (ACPI_LV_THREADS & acpi_dbg_level) {
		acpi_os_printf("[%04lX] ", thread_id);
	}

	acpi_os_printf("[%02ld] %-22.22s: ",
		       acpi_gbl_nesting_level,
		       acpi_ut_trim_function_name(function_name));

	va_start(args, format);
	acpi_os_vprintf(format, args);
}

EXPORT_SYMBOL(acpi_ut_debug_print);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_debug_print_raw
 *
 * PARAMETERS:  requested_debug_level - Requested debug print level
 *              line_number         - Caller's line number
 *              function_name       - Caller's procedure name
 *              module_name         - Caller's module name
 *              component_id        - Caller's component ID
 *              Format              - Printf format field
 *              ...                 - Optional printf arguments
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print message with no headers.  Has same interface as
 *              debug_print so that the same macros can be used.
 *
 ******************************************************************************/

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_debug_print_raw(u32 requested_debug_level,
			u32 line_number,
			const char *function_name,
			char *module_name, u32 component_id, char *format, ...)
{
	va_list args;

	if (!(requested_debug_level & acpi_dbg_level) ||
	    !(component_id & acpi_dbg_layer)) {
		return;
	}

	va_start(args, format);
	acpi_os_vprintf(format, args);
}

EXPORT_SYMBOL(acpi_ut_debug_print_raw);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_trace
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              function_name       - Caller's procedure name
 *              module_name         - Caller's module name
 *              component_id        - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level
 *
 ******************************************************************************/

void
acpi_ut_trace(u32 line_number,
	      const char *function_name, char *module_name, u32 component_id)
{

	acpi_gbl_nesting_level++;
	acpi_ut_track_stack_ptr();

	acpi_ut_debug_print(ACPI_LV_FUNCTIONS,
			    line_number, function_name, module_name,
			    component_id, "%s\n", acpi_gbl_fn_entry_str);
}

EXPORT_SYMBOL(acpi_ut_trace);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_trace_ptr
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              function_name       - Caller's procedure name
 *              module_name         - Caller's module name
 *              component_id        - Caller's component ID
 *              Pointer             - Pointer to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level
 *
 ******************************************************************************/

void
acpi_ut_trace_ptr(u32 line_number,
		  const char *function_name,
		  char *module_name, u32 component_id, void *pointer)
{
	acpi_gbl_nesting_level++;
	acpi_ut_track_stack_ptr();

	acpi_ut_debug_print(ACPI_LV_FUNCTIONS,
			    line_number, function_name, module_name,
			    component_id, "%s %p\n", acpi_gbl_fn_entry_str,
			    pointer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_trace_str
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              function_name       - Caller's procedure name
 *              module_name         - Caller's module name
 *              component_id        - Caller's component ID
 *              String              - Additional string to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level
 *
 ******************************************************************************/

void
acpi_ut_trace_str(u32 line_number,
		  const char *function_name,
		  char *module_name, u32 component_id, char *string)
{

	acpi_gbl_nesting_level++;
	acpi_ut_track_stack_ptr();

	acpi_ut_debug_print(ACPI_LV_FUNCTIONS,
			    line_number, function_name, module_name,
			    component_id, "%s %s\n", acpi_gbl_fn_entry_str,
			    string);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_trace_u32
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              function_name       - Caller's procedure name
 *              module_name         - Caller's module name
 *              component_id        - Caller's component ID
 *              Integer             - Integer to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level
 *
 ******************************************************************************/

void
acpi_ut_trace_u32(u32 line_number,
		  const char *function_name,
		  char *module_name, u32 component_id, u32 integer)
{

	acpi_gbl_nesting_level++;
	acpi_ut_track_stack_ptr();

	acpi_ut_debug_print(ACPI_LV_FUNCTIONS,
			    line_number, function_name, module_name,
			    component_id, "%s %08X\n", acpi_gbl_fn_entry_str,
			    integer);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_exit
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              function_name       - Caller's procedure name
 *              module_name         - Caller's module name
 *              component_id        - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level
 *
 ******************************************************************************/

void
acpi_ut_exit(u32 line_number,
	     const char *function_name, char *module_name, u32 component_id)
{

	acpi_ut_debug_print(ACPI_LV_FUNCTIONS,
			    line_number, function_name, module_name,
			    component_id, "%s\n", acpi_gbl_fn_exit_str);

	acpi_gbl_nesting_level--;
}

EXPORT_SYMBOL(acpi_ut_exit);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_status_exit
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              function_name       - Caller's procedure name
 *              module_name         - Caller's module name
 *              component_id        - Caller's component ID
 *              Status              - Exit status code
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level. Prints exit status also.
 *
 ******************************************************************************/

void
acpi_ut_status_exit(u32 line_number,
		    const char *function_name,
		    char *module_name, u32 component_id, acpi_status status)
{

	if (ACPI_SUCCESS(status)) {
		acpi_ut_debug_print(ACPI_LV_FUNCTIONS,
				    line_number, function_name, module_name,
				    component_id, "%s %s\n",
				    acpi_gbl_fn_exit_str,
				    acpi_format_exception(status));
	} else {
		acpi_ut_debug_print(ACPI_LV_FUNCTIONS,
				    line_number, function_name, module_name,
				    component_id, "%s ****Exception****: %s\n",
				    acpi_gbl_fn_exit_str,
				    acpi_format_exception(status));
	}

	acpi_gbl_nesting_level--;
}

EXPORT_SYMBOL(acpi_ut_status_exit);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_value_exit
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              function_name       - Caller's procedure name
 *              module_name         - Caller's module name
 *              component_id        - Caller's component ID
 *              Value               - Value to be printed with exit msg
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level. Prints exit value also.
 *
 ******************************************************************************/

void
acpi_ut_value_exit(u32 line_number,
		   const char *function_name,
		   char *module_name, u32 component_id, acpi_integer value)
{

	acpi_ut_debug_print(ACPI_LV_FUNCTIONS,
			    line_number, function_name, module_name,
			    component_id, "%s %8.8X%8.8X\n",
			    acpi_gbl_fn_exit_str, ACPI_FORMAT_UINT64(value));

	acpi_gbl_nesting_level--;
}

EXPORT_SYMBOL(acpi_ut_value_exit);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_ptr_exit
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              function_name       - Caller's procedure name
 *              module_name         - Caller's module name
 *              component_id        - Caller's component ID
 *              Ptr                 - Pointer to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level. Prints exit value also.
 *
 ******************************************************************************/

void
acpi_ut_ptr_exit(u32 line_number,
		 const char *function_name,
		 char *module_name, u32 component_id, u8 * ptr)
{

	acpi_ut_debug_print(ACPI_LV_FUNCTIONS,
			    line_number, function_name, module_name,
			    component_id, "%s %p\n", acpi_gbl_fn_exit_str, ptr);

	acpi_gbl_nesting_level--;
}

#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_dump_buffer
 *
 * PARAMETERS:  Buffer              - Buffer to dump
 *              Count               - Amount to dump, in bytes
 *              Display             - BYTE, WORD, DWORD, or QWORD display
 *              component_iD        - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generic dump buffer in both hex and ascii.
 *
 ******************************************************************************/

void acpi_ut_dump_buffer(u8 * buffer, u32 count, u32 display, u32 component_id)
{
	acpi_native_uint i = 0;
	acpi_native_uint j;
	u32 temp32;
	u8 buf_char;

	/* Only dump the buffer if tracing is enabled */

	if (!((ACPI_LV_TABLES & acpi_dbg_level) &&
	      (component_id & acpi_dbg_layer))) {
		return;
	}

	if ((count < 4) || (count & 0x01)) {
		display = DB_BYTE_DISPLAY;
	}

	/* Nasty little dump buffer routine! */

	while (i < count) {
		/* Print current offset */

		acpi_os_printf("%6.4X: ", (u32) i);

		/* Print 16 hex chars */

		for (j = 0; j < 16;) {
			if (i + j >= count) {
				/* Dump fill spaces */

				acpi_os_printf("%*s", ((display * 2) + 1), " ");
				j += (acpi_native_uint) display;
				continue;
			}

			switch (display) {
			default:	/* Default is BYTE display */

				acpi_os_printf("%02X ", buffer[i + j]);
				break;

			case DB_WORD_DISPLAY:

				ACPI_MOVE_16_TO_32(&temp32, &buffer[i + j]);
				acpi_os_printf("%04X ", temp32);
				break;

			case DB_DWORD_DISPLAY:

				ACPI_MOVE_32_TO_32(&temp32, &buffer[i + j]);
				acpi_os_printf("%08X ", temp32);
				break;

			case DB_QWORD_DISPLAY:

				ACPI_MOVE_32_TO_32(&temp32, &buffer[i + j]);
				acpi_os_printf("%08X", temp32);

				ACPI_MOVE_32_TO_32(&temp32, &buffer[i + j + 4]);
				acpi_os_printf("%08X ", temp32);
				break;
			}

			j += (acpi_native_uint) display;
		}

		/*
		 * Print the ASCII equivalent characters but watch out for the bad
		 * unprintable ones (printable chars are 0x20 through 0x7E)
		 */
		acpi_os_printf(" ");
		for (j = 0; j < 16; j++) {
			if (i + j >= count) {
				acpi_os_printf("\n");
				return;
			}

			buf_char = buffer[i + j];
			if (ACPI_IS_PRINT(buf_char)) {
				acpi_os_printf("%c", buf_char);
			} else {
				acpi_os_printf(".");
			}
		}

		/* Done with that line. */

		acpi_os_printf("\n");
		i += 16;
	}

	return;
}
