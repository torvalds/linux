// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: uterror - Various internal error/warning output functions
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("uterror")

/*
 * This module contains internal error functions that may
 * be configured out.
 */
#if !defined (ACPI_NO_ERROR_MESSAGES)
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_predefined_warning
 *
 * PARAMETERS:  module_name     - Caller's module name (for error output)
 *              line_number     - Caller's line number (for error output)
 *              pathname        - Full pathname to the yesde
 *              yesde_flags      - From Namespace yesde for the method/object
 *              format          - Printf format string + additional args
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
			   u16 yesde_flags, const char *format, ...)
{
	va_list arg_list;

	/*
	 * Warning messages for this method/object will be disabled after the
	 * first time a validation fails or an object is successfully repaired.
	 */
	if (yesde_flags & ANOBJ_EVALUATED) {
		return;
	}

	acpi_os_printf(ACPI_MSG_WARNING "%s: ", pathname);

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
 *              pathname        - Full pathname to the yesde
 *              yesde_flags      - From Namespace yesde for the method/object
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
			char *pathname, u16 yesde_flags, const char *format, ...)
{
	va_list arg_list;

	/*
	 * Warning messages for this method/object will be disabled after the
	 * first time a validation fails or an object is successfully repaired.
	 */
	if (yesde_flags & ANOBJ_EVALUATED) {
		return;
	}

	acpi_os_printf(ACPI_MSG_INFO "%s: ", pathname);

	va_start(arg_list, format);
	acpi_os_vprintf(format, arg_list);
	ACPI_MSG_SUFFIX;
	va_end(arg_list);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_predefined_bios_error
 *
 * PARAMETERS:  module_name     - Caller's module name (for error output)
 *              line_number     - Caller's line number (for error output)
 *              pathname        - Full pathname to the yesde
 *              yesde_flags      - From Namespace yesde for the method/object
 *              format          - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: BIOS error message for predefined names. Messages
 *              are only emitted the first time a problem with a particular
 *              method/object is detected. This prevents a flood of
 *              messages for methods that are repeatedly evaluated.
 *
 ******************************************************************************/

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_predefined_bios_error(const char *module_name,
			      u32 line_number,
			      char *pathname,
			      u16 yesde_flags, const char *format, ...)
{
	va_list arg_list;

	/*
	 * Warning messages for this method/object will be disabled after the
	 * first time a validation fails or an object is successfully repaired.
	 */
	if (yesde_flags & ANOBJ_EVALUATED) {
		return;
	}

	acpi_os_printf(ACPI_MSG_BIOS_ERROR "%s: ", pathname);

	va_start(arg_list, format);
	acpi_os_vprintf(format, arg_list);
	ACPI_MSG_SUFFIX;
	va_end(arg_list);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_prefixed_namespace_error
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              prefix_scope        - Scope/Path that prefixes the internal path
 *              internal_path       - Name or path of the namespace yesde
 *              lookup_status       - Exception code from NS lookup
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message with the full pathname constructed this way:
 *
 *                  prefix_scope_yesde_full_path.externalized_internal_path
 *
 * NOTE:        10/2017: Treat the major ns_lookup errors as firmware errors
 *
 ******************************************************************************/

void
acpi_ut_prefixed_namespace_error(const char *module_name,
				 u32 line_number,
				 union acpi_generic_state *prefix_scope,
				 const char *internal_path,
				 acpi_status lookup_status)
{
	char *full_path;
	const char *message;

	/*
	 * Main cases:
	 * 1) Object creation, object must yest already exist
	 * 2) Object lookup, object must exist
	 */
	switch (lookup_status) {
	case AE_ALREADY_EXISTS:

		acpi_os_printf(ACPI_MSG_BIOS_ERROR);
		message = "Failure creating named object";
		break;

	case AE_NOT_FOUND:

		acpi_os_printf(ACPI_MSG_BIOS_ERROR);
		message = "Could yest resolve symbol";
		break;

	default:

		acpi_os_printf(ACPI_MSG_ERROR);
		message = "Failure resolving symbol";
		break;
	}

	/* Concatenate the prefix path and the internal path */

	full_path =
	    acpi_ns_build_prefixed_pathname(prefix_scope, internal_path);

	acpi_os_printf("%s [%s], %s", message,
		       full_path ? full_path : "Could yest get pathname",
		       acpi_format_exception(lookup_status));

	if (full_path) {
		ACPI_FREE(full_path);
	}

	ACPI_MSG_SUFFIX;
}

#ifdef __OBSOLETE_FUNCTION
/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_namespace_error
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              internal_name       - Name or path of the namespace yesde
 *              lookup_status       - Exception code from NS lookup
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message with the full pathname for the NS yesde.
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

		/* There is a yesn-ascii character in the name */

		ACPI_MOVE_32_TO_32(&bad_name,
				   ACPI_CAST_PTR(u32, internal_name));
		acpi_os_printf("[0x%.8X] (NON-ASCII)", bad_name);
	} else {
		/* Convert path to external format */

		status =
		    acpi_ns_externalize_name(ACPI_UINT32_MAX, internal_name,
					     NULL, &name);

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
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_method_error
 *
 * PARAMETERS:  module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              message             - Error message to use on failure
 *              prefix_yesde         - Prefix relative to the path
 *              path                - Path to the yesde (optional)
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
		     struct acpi_namespace_yesde *prefix_yesde,
		     const char *path, acpi_status method_status)
{
	acpi_status status;
	struct acpi_namespace_yesde *yesde = prefix_yesde;

	ACPI_MSG_REDIRECT_BEGIN;
	acpi_os_printf(ACPI_MSG_ERROR);

	if (path) {
		status = acpi_ns_get_yesde(prefix_yesde, path,
					  ACPI_NS_NO_UPSEARCH, &yesde);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf("[Could yest get yesde by pathname]");
		}
	}

	acpi_ns_print_yesde_pathname(yesde, message);
	acpi_os_printf(" due to previous error (%s)",
		       acpi_format_exception(method_status));

	ACPI_MSG_SUFFIX;
	ACPI_MSG_REDIRECT_END;
}

#endif				/* ACPI_NO_ERROR_MESSAGES */
