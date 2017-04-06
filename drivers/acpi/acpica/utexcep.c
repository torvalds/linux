/*******************************************************************************
 *
 * Module Name: utexcep - Exception code support
 *
 ******************************************************************************/

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

#define EXPORT_ACPI_INTERFACES

#define ACPI_DEFINE_EXCEPTION_TABLE
#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utexcep")

/*******************************************************************************
 *
 * FUNCTION:    acpi_format_exception
 *
 * PARAMETERS:  status              - The acpi_status code to be formatted
 *
 * RETURN:      A string containing the exception text. A valid pointer is
 *              always returned.
 *
 * DESCRIPTION: This function translates an ACPI exception into an ASCII
 *              string. Returns "unknown status" string for invalid codes.
 *
 ******************************************************************************/
const char *acpi_format_exception(acpi_status status)
{
	const struct acpi_exception_info *exception;

	ACPI_FUNCTION_ENTRY();

	exception = acpi_ut_validate_exception(status);
	if (!exception) {

		/* Exception code was not recognized */

		ACPI_ERROR((AE_INFO,
			    "Unknown exception code: 0x%8.8X", status));

		return ("UNKNOWN_STATUS_CODE");
	}

	return (exception->name);
}

ACPI_EXPORT_SYMBOL(acpi_format_exception)

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_validate_exception
 *
 * PARAMETERS:  status              - The acpi_status code to be formatted
 *
 * RETURN:      A string containing the exception text. NULL if exception is
 *              not valid.
 *
 * DESCRIPTION: This function validates and translates an ACPI exception into
 *              an ASCII string.
 *
 ******************************************************************************/
const struct acpi_exception_info *acpi_ut_validate_exception(acpi_status status)
{
	u32 sub_status;
	const struct acpi_exception_info *exception = NULL;

	ACPI_FUNCTION_ENTRY();

	/*
	 * Status is composed of two parts, a "type" and an actual code
	 */
	sub_status = (status & ~AE_CODE_MASK);

	switch (status & AE_CODE_MASK) {
	case AE_CODE_ENVIRONMENTAL:

		if (sub_status <= AE_CODE_ENV_MAX) {
			exception = &acpi_gbl_exception_names_env[sub_status];
		}
		break;

	case AE_CODE_PROGRAMMER:

		if (sub_status <= AE_CODE_PGM_MAX) {
			exception = &acpi_gbl_exception_names_pgm[sub_status];
		}
		break;

	case AE_CODE_ACPI_TABLES:

		if (sub_status <= AE_CODE_TBL_MAX) {
			exception = &acpi_gbl_exception_names_tbl[sub_status];
		}
		break;

	case AE_CODE_AML:

		if (sub_status <= AE_CODE_AML_MAX) {
			exception = &acpi_gbl_exception_names_aml[sub_status];
		}
		break;

	case AE_CODE_CONTROL:

		if (sub_status <= AE_CODE_CTRL_MAX) {
			exception = &acpi_gbl_exception_names_ctrl[sub_status];
		}
		break;

	default:

		break;
	}

	if (!exception || !exception->name) {
		return (NULL);
	}

	return (exception);
}
