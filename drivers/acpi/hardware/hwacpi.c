
/******************************************************************************
 *
 * Module Name: hwacpi - ACPI Hardware Initialization/Mode Interface
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

#include <acpi/acpi.h>

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwacpi")

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize and validate the various ACPI registers defined in
 *              the FADT.
 *
 ******************************************************************************/
acpi_status acpi_hw_initialize(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(hw_initialize);

	/* We must have the ACPI tables by the time we get here */

	if (!acpi_gbl_FADT) {
		ACPI_ERROR((AE_INFO, "No FADT is present"));
		return_ACPI_STATUS(AE_NO_ACPI_TABLES);
	}

	/* Sanity check the FADT for valid values */

	status = acpi_ut_validate_fadt();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	return_ACPI_STATUS(AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_set_mode
 *
 * PARAMETERS:  Mode            - SYS_MODE_ACPI or SYS_MODE_LEGACY
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transitions the system into the requested mode.
 *
 ******************************************************************************/

acpi_status acpi_hw_set_mode(u32 mode)
{

	acpi_status status;
	u32 retry;

	ACPI_FUNCTION_TRACE(hw_set_mode);

	/*
	 * ACPI 2.0 clarified that if SMI_CMD in FADT is zero,
	 * system does not support mode transition.
	 */
	if (!acpi_gbl_FADT->smi_cmd) {
		ACPI_ERROR((AE_INFO,
			    "No SMI_CMD in FADT, mode transition failed"));
		return_ACPI_STATUS(AE_NO_HARDWARE_RESPONSE);
	}

	/*
	 * ACPI 2.0 clarified the meaning of ACPI_ENABLE and ACPI_DISABLE
	 * in FADT: If it is zero, enabling or disabling is not supported.
	 * As old systems may have used zero for mode transition,
	 * we make sure both the numbers are zero to determine these
	 * transitions are not supported.
	 */
	if (!acpi_gbl_FADT->acpi_enable && !acpi_gbl_FADT->acpi_disable) {
		ACPI_ERROR((AE_INFO,
			    "No ACPI mode transition supported in this system (enable/disable both zero)"));
		return_ACPI_STATUS(AE_OK);
	}

	switch (mode) {
	case ACPI_SYS_MODE_ACPI:

		/* BIOS should have disabled ALL fixed and GP events */

		status = acpi_os_write_port(acpi_gbl_FADT->smi_cmd,
					    (u32) acpi_gbl_FADT->acpi_enable,
					    8);
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Attempting to enable ACPI mode\n"));
		break;

	case ACPI_SYS_MODE_LEGACY:

		/*
		 * BIOS should clear all fixed status bits and restore fixed event
		 * enable bits to default
		 */
		status = acpi_os_write_port(acpi_gbl_FADT->smi_cmd,
					    (u32) acpi_gbl_FADT->acpi_disable,
					    8);
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Attempting to enable Legacy (non-ACPI) mode\n"));
		break;

	default:
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Could not write ACPI mode change"));
		return_ACPI_STATUS(status);
	}

	/*
	 * Some hardware takes a LONG time to switch modes. Give them 3 sec to
	 * do so, but allow faster systems to proceed more quickly.
	 */
	retry = 3000;
	while (retry) {
		if (acpi_hw_get_mode() == mode) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Mode %X successfully enabled\n",
					  mode));
			return_ACPI_STATUS(AE_OK);
		}
		acpi_os_stall(1000);
		retry--;
	}

	ACPI_ERROR((AE_INFO, "Hardware did not change modes"));
	return_ACPI_STATUS(AE_NO_HARDWARE_RESPONSE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_get_mode
 *
 * PARAMETERS:  none
 *
 * RETURN:      SYS_MODE_ACPI or SYS_MODE_LEGACY
 *
 * DESCRIPTION: Return current operating state of system.  Determined by
 *              querying the SCI_EN bit.
 *
 ******************************************************************************/

u32 acpi_hw_get_mode(void)
{
	acpi_status status;
	u32 value;

	ACPI_FUNCTION_TRACE(hw_get_mode);

	/*
	 * ACPI 2.0 clarified that if SMI_CMD in FADT is zero,
	 * system does not support mode transition.
	 */
	if (!acpi_gbl_FADT->smi_cmd) {
		return_UINT32(ACPI_SYS_MODE_ACPI);
	}

	status =
	    acpi_get_register(ACPI_BITREG_SCI_ENABLE, &value, ACPI_MTX_LOCK);
	if (ACPI_FAILURE(status)) {
		return_UINT32(ACPI_SYS_MODE_LEGACY);
	}

	if (value) {
		return_UINT32(ACPI_SYS_MODE_ACPI);
	} else {
		return_UINT32(ACPI_SYS_MODE_LEGACY);
	}
}
