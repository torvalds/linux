/*******************************************************************************
 *
 * Module Name: utxferror - Various error/warning output functions
 *
 ******************************************************************************/

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

#include <linux/export.h>
#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utxferror")

/*
 * This module is used for the in-kernel ACPICA as well as the ACPICA
 * tools/applications.
 *
 * For the iASL compiler case, the output is redirected to stderr so that
 * any of the various ACPI errors and warnings do not appear in the output
 * files, for either the compiler or disassembler portions of the tool.
 */
#ifdef ACPI_ASL_COMPILER
#include <stdio.h>
extern FILE *acpi_gbl_output_file;

#define ACPI_MSG_REDIRECT_BEGIN \
	FILE                            *output_file = acpi_gbl_output_file; \
	acpi_os_redirect_output (stderr);

#define ACPI_MSG_REDIRECT_END \
	acpi_os_redirect_output (output_file);

#else
/*
 * non-iASL case - no redirection, nothing to do
 */
#define ACPI_MSG_REDIRECT_BEGIN
#define ACPI_MSG_REDIRECT_END
#endif
/*
 * Common message prefixes
 */
#define ACPI_MSG_ERROR          "ACPI Error: "
#define ACPI_MSG_EXCEPTION      "ACPI Exception: "
#define ACPI_MSG_WARNING        "ACPI Warning: "
#define ACPI_MSG_INFO           "ACPI: "
#define ACPI_MSG_BIOS_ERROR     "ACPI BIOS Bug: Error: "
#define ACPI_MSG_BIOS_WARNING   "ACPI BIOS Bug: Warning: "
/*
 * Common message suffix
 */
#define ACPI_MSG_SUFFIX \
	acpi_os_printf (" (%8.8X/%s-%u)\n", ACPI_CA_VERSION, module_name, line_number)
/*******************************************************************************
 *
 * FUNCTION:    acpi_error
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              format              - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print "ACPI Error" message with module/line/version info
 *
 ******************************************************************************/
void ACPI_INTERNAL_VAR_XFACE
acpi_error(const char *module_name, u32 line_number, const char *format, ...)
{
	va_list arg_list;

	ACPI_MSG_REDIRECT_BEGIN;
	acpi_os_printf(ACPI_MSG_ERROR);

	va_start(arg_list, format);
	acpi_os_vprintf(format, arg_list);
	ACPI_MSG_SUFFIX;
	va_end(arg_list);

	ACPI_MSG_REDIRECT_END;
}

ACPI_EXPORT_SYMBOL(acpi_error)

/*******************************************************************************
 *
 * FUNCTION:    acpi_exception
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              status              - Status to be formatted
 *              format              - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print "ACPI Exception" message with module/line/version info
 *              and decoded acpi_status.
 *
 ******************************************************************************/
void ACPI_INTERNAL_VAR_XFACE
acpi_exception(const char *module_name,
	       u32 line_number, acpi_status status, const char *format, ...)
{
	va_list arg_list;

	ACPI_MSG_REDIRECT_BEGIN;
	acpi_os_printf(ACPI_MSG_EXCEPTION "%s, ",
		       acpi_format_exception(status));

	va_start(arg_list, format);
	acpi_os_vprintf(format, arg_list);
	ACPI_MSG_SUFFIX;
	va_end(arg_list);

	ACPI_MSG_REDIRECT_END;
}

ACPI_EXPORT_SYMBOL(acpi_exception)

/*******************************************************************************
 *
 * FUNCTION:    acpi_warning
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              format              - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print "ACPI Warning" message with module/line/version info
 *
 ******************************************************************************/
void ACPI_INTERNAL_VAR_XFACE
acpi_warning(const char *module_name, u32 line_number, const char *format, ...)
{
	va_list arg_list;

	ACPI_MSG_REDIRECT_BEGIN;
	acpi_os_printf(ACPI_MSG_WARNING);

	va_start(arg_list, format);
	acpi_os_vprintf(format, arg_list);
	ACPI_MSG_SUFFIX;
	va_end(arg_list);

	ACPI_MSG_REDIRECT_END;
}

ACPI_EXPORT_SYMBOL(acpi_warning)

/*******************************************************************************
 *
 * FUNCTION:    acpi_info
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              format              - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print generic "ACPI:" information message. There is no
 *              module/line/version info in order to keep the message simple.
 *
 * TBD: module_name and line_number args are not needed, should be removed.
 *
 ******************************************************************************/
void ACPI_INTERNAL_VAR_XFACE
acpi_info(const char *module_name, u32 line_number, const char *format, ...)
{
	va_list arg_list;

	ACPI_MSG_REDIRECT_BEGIN;
	acpi_os_printf(ACPI_MSG_INFO);

	va_start(arg_list, format);
	acpi_os_vprintf(format, arg_list);
	acpi_os_printf("\n");
	va_end(arg_list);

	ACPI_MSG_REDIRECT_END;
}

ACPI_EXPORT_SYMBOL(acpi_info)

/*******************************************************************************
 *
 * FUNCTION:    acpi_bios_error
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              format              - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print "ACPI Firmware Error" message with module/line/version
 *              info
 *
 ******************************************************************************/
void ACPI_INTERNAL_VAR_XFACE
acpi_bios_error(const char *module_name,
		u32 line_number, const char *format, ...)
{
	va_list arg_list;

	ACPI_MSG_REDIRECT_BEGIN;
	acpi_os_printf(ACPI_MSG_BIOS_ERROR);

	va_start(arg_list, format);
	acpi_os_vprintf(format, arg_list);
	ACPI_MSG_SUFFIX;
	va_end(arg_list);

	ACPI_MSG_REDIRECT_END;
}

ACPI_EXPORT_SYMBOL(acpi_bios_error)

/*******************************************************************************
 *
 * FUNCTION:    acpi_bios_warning
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              format              - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print "ACPI Firmware Warning" message with module/line/version
 *              info
 *
 ******************************************************************************/
void ACPI_INTERNAL_VAR_XFACE
acpi_bios_warning(const char *module_name,
		  u32 line_number, const char *format, ...)
{
	va_list arg_list;

	ACPI_MSG_REDIRECT_BEGIN;
	acpi_os_printf(ACPI_MSG_BIOS_WARNING);

	va_start(arg_list, format);
	acpi_os_vprintf(format, arg_list);
	ACPI_MSG_SUFFIX;
	va_end(arg_list);

	ACPI_MSG_REDIRECT_END;
}

ACPI_EXPORT_SYMBOL(acpi_bios_warning)

/*
 * The remainder of this module contains internal error functions that may
 * be configured out.
 */
#if !defined (ACPI_NO_ERROR_MESSAGES) && !defined (ACPI_BIN_APP)
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_predefined_warning
 *
 * PARAMETERS:  module_name     - Caller's module name (for error output)
 *              line_number     - Caller's line number (for error output)
 *              Pathname        - Full pathname to the node
 *              node_flags      - From Namespace node for the method/object
 *              Format          - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Warnings for the predefined validation module. Messages are
 *              only emitted the first time a problem with a particular
 *              method/object is detected. This prevents a flood of error
 *              messages for methods that are repeatedly evaluated.
 *
 ******************************************************************************/
void ACPI_INTERNAL_VAR_XFACE
acpi_ut_predefined_warning(const char *module_name,
			   u32 line_number,
			   char *pathname,
			   u8 node_flags, const char *format, ...)
{
	va_list arg_list;

	/*
	 * Warning messages for this method/object will be disabled after the
	 * first time a validation fails or an object is successfully repaired.
	 */
	if (node_flags & ANOBJ_EVALUATED) {
		return;
	}

	acpi_os_printf(ACPI_MSG_WARNING "For %s: ", pathname);

	va_start(arg_list, format);
	acpi_os_vprintf(format, arg_list);
	ACPI_MSG_SUFFIX;
	va_end(arg_list);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_predefined_info
 *
 * PARAMETERS:  module_name     - Caller's module name (for error output)
 *              line_number     - Caller's line number (for error output)
 *              pathname        - Full pathname to the node
 *              node_flags      - From Namespace node for the method/object
 *              format          - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Info messages for the predefined validation module. Messages
 *              are only emitted the first time a problem with a particular
 *              method/object is detected. This prevents a flood of
 *              messages for methods that are repeatedly evaluated.
 *
 ******************************************************************************/

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_predefined_info(const char *module_name,
			u32 line_number,
			char *pathname, u8 node_flags, const char *format, ...)
{
	va_list arg_list;

	/*
	 * Warning messages for this method/object will be disabled after the
	 * first time a validation fails or an object is successfully repaired.
	 */
	if (node_flags & ANOBJ_EVALUATED) {
		return;
	}

	acpi_os_printf(ACPI_MSG_INFO "For %s: ", pathname);

	va_start(arg_list, format);
	acpi_os_vprintf(format, arg_list);
	ACPI_MSG_SUFFIX;
	va_end(arg_list);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_namespace_error
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              internal_name       - Name or path of the namespace node
 *              lookup_status       - Exception code from NS lookup
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message with the full pathname for the NS node.
 *
 ******************************************************************************/

void
acpi_ut_namespace_error(const char *module_name,
			u32 line_number,
			const char *internal_name, acpi_status lookup_status)
{
	acpi_status status;
	u32 bad_name;
	char *name = NULL;

	ACPI_MSG_REDIRECT_BEGIN;
	acpi_os_printf(ACPI_MSG_ERROR);

	if (lookup_status == AE_BAD_CHARACTER) {

		/* There is a non-ascii character in the name */

		ACPI_MOVE_32_TO_32(&bad_name,
				   ACPI_CAST_PTR(u32, internal_name));
		acpi_os_printf("[0x%.8X] (NON-ASCII)", bad_name);
	} else {
		/* Convert path to external format */

		status = acpi_ns_externalize_name(ACPI_UINT32_MAX,
						  internal_name, NULL, &name);

		/* Print target name */

		if (ACPI_SUCCESS(status)) {
			acpi_os_printf("[%s]", name);
		} else {
			acpi_os_printf("[COULD NOT EXTERNALIZE NAME]");
		}

		if (name) {
			ACPI_FREE(name);
		}
	}

	acpi_os_printf(" Namespace lookup failure, %s",
		       acpi_format_exception(lookup_status));

	ACPI_MSG_SUFFIX;
	ACPI_MSG_REDIRECT_END;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_method_error
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              message             - Error message to use on failure
 *              prefix_node         - Prefix relative to the path
 *              path                - Path to the node (optional)
 *              method_status       - Execution status
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message with the full pathname for the method.
 *
 ******************************************************************************/

void
acpi_ut_method_error(const char *module_name,
		     u32 line_number,
		     const char *message,
		     struct acpi_namespace_node *prefix_node,
		     const char *path, acpi_status method_status)
{
	acpi_status status;
	struct acpi_namespace_node *node = prefix_node;

	ACPI_MSG_REDIRECT_BEGIN;
	acpi_os_printf(ACPI_MSG_ERROR);

	if (path) {
		status =
		    acpi_ns_get_node(prefix_node, path, ACPI_NS_NO_UPSEARCH,
				     &node);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf("[Could not get node by pathname]");
		}
	}

	acpi_ns_print_node_pathname(node, message);
	acpi_os_printf(", %s", acpi_format_exception(method_status));

	ACPI_MSG_SUFFIX;
	ACPI_MSG_REDIRECT_END;
}

#endif				/* ACPI_NO_ERROR_MESSAGES */
