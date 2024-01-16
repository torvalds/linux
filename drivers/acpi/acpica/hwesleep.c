// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Name: hwesleep.c - ACPI Hardware Sleep/Wake Support functions for the
 *                    extended FADT-V5 sleep registers.
 *
 * Copyright (C) 2000 - 2022, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwesleep")

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_execute_sleep_method
 *
 * PARAMETERS:  method_pathname     - Pathname of method to execute
 *              integer_argument    - Argument to pass to the method
 *
 * RETURN:      None
 *
 * DESCRIPTION: Execute a sleep/wake related method with one integer argument
 *              and no return value.
 *
 ******************************************************************************/
void acpi_hw_execute_sleep_method(char *method_pathname, u32 integer_argument)
{
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status;

	ACPI_FUNCTION_TRACE(hw_execute_sleep_method);

	/* One argument, integer_argument; No return value expected */

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = (u64)integer_argument;

	status = acpi_evaluate_object(NULL, method_pathname, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_EXCEPTION((AE_INFO, status, "While executing method %s",
				method_pathname));
	}

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_extended_sleep
 *
 * PARAMETERS:  sleep_state         - Which sleep state to enter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter a system sleep state via the extended FADT sleep
 *              registers (V5 FADT).
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/

acpi_status acpi_hw_extended_sleep(u8 sleep_state)
{
	acpi_status status;
	u8 sleep_control;
	u64 sleep_status;

	ACPI_FUNCTION_TRACE(hw_extended_sleep);

	/* Extended sleep registers must be valid */

	if (!acpi_gbl_FADT.sleep_control.address ||
	    !acpi_gbl_FADT.sleep_status.address) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Clear wake status (WAK_STS) */

	status = acpi_write((u64)ACPI_X_WAKE_STATUS,
			    &acpi_gbl_FADT.sleep_status);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	acpi_gbl_system_awake_and_running = FALSE;

	/*
	 * Set the SLP_TYP and SLP_EN bits.
	 *
	 * Note: We only use the first value returned by the \_Sx method
	 * (acpi_gbl_sleep_type_a) - As per ACPI specification.
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_INIT,
			  "Entering sleep state [S%u]\n", sleep_state));

	sleep_control = ((acpi_gbl_sleep_type_a << ACPI_X_SLEEP_TYPE_POSITION) &
			 ACPI_X_SLEEP_TYPE_MASK) | ACPI_X_SLEEP_ENABLE;

	/* Flush caches, as per ACPI specification */

	if (sleep_state < ACPI_STATE_S4) {
		ACPI_FLUSH_CPU_CACHE();
	}

	status = acpi_os_enter_sleep(sleep_state, sleep_control, 0);
	if (status == AE_CTRL_TERMINATE) {
		return_ACPI_STATUS(AE_OK);
	}
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_write((u64)sleep_control, &acpi_gbl_FADT.sleep_control);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Wait for transition back to Working State */

	do {
		status = acpi_read(&sleep_status, &acpi_gbl_FADT.sleep_status);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

	} while (!(((u8)sleep_status) & ACPI_X_WAKE_STATUS));

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_extended_wake_prep
 *
 * PARAMETERS:  sleep_state         - Which sleep state we just exited
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform first part of OS-independent ACPI cleanup after
 *              a sleep. Called with interrupts ENABLED.
 *
 ******************************************************************************/

acpi_status acpi_hw_extended_wake_prep(u8 sleep_state)
{
	u8 sleep_type_value;

	ACPI_FUNCTION_TRACE(hw_extended_wake_prep);

	if (acpi_gbl_sleep_type_a_s0 != ACPI_SLEEP_TYPE_INVALID) {
		sleep_type_value =
		    ((acpi_gbl_sleep_type_a_s0 << ACPI_X_SLEEP_TYPE_POSITION) &
		     ACPI_X_SLEEP_TYPE_MASK);

		(void)acpi_write((u64)(sleep_type_value | ACPI_X_SLEEP_ENABLE),
				 &acpi_gbl_FADT.sleep_control);
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_extended_wake
 *
 * PARAMETERS:  sleep_state         - Which sleep state we just exited
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform OS-independent ACPI cleanup after a sleep
 *              Called with interrupts ENABLED.
 *
 ******************************************************************************/

acpi_status acpi_hw_extended_wake(u8 sleep_state)
{
	ACPI_FUNCTION_TRACE(hw_extended_wake);

	/* Ensure enter_sleep_state_prep -> enter_sleep_state ordering */

	acpi_gbl_sleep_type_a = ACPI_SLEEP_TYPE_INVALID;

	/* Execute the wake methods */

	acpi_hw_execute_sleep_method(METHOD_PATHNAME__SST, ACPI_SST_WAKING);
	acpi_hw_execute_sleep_method(METHOD_PATHNAME__WAK, sleep_state);

	/*
	 * Some BIOS code assumes that WAK_STS will be cleared on resume
	 * and use it to determine whether the system is rebooting or
	 * resuming. Clear WAK_STS for compatibility.
	 */
	(void)acpi_write((u64)ACPI_X_WAKE_STATUS, &acpi_gbl_FADT.sleep_status);
	acpi_gbl_system_awake_and_running = TRUE;

	acpi_hw_execute_sleep_method(METHOD_PATHNAME__SST, ACPI_SST_WORKING);
	return_ACPI_STATUS(AE_OK);
}
