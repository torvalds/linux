/******************************************************************************
 *
 * Name: hwsleep.c - ACPI Hardware Sleep/Wake Support Functions
 *
 *****************************************************************************/

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

#include <acpi/acpi.h>
#include "accommon.h"
#include <linux/tboot.h>
#include <linux/module.h>

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwsleep")

static unsigned int gts, bfs;
module_param(gts, uint, 0644);
module_param(bfs, uint, 0644);
MODULE_PARM_DESC(gts, "Enable evaluation of _GTS on suspend.");
MODULE_PARM_DESC(bfs, "Enable evaluation of _BFS on resume".);

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_execute_sleep_method
 *
 * PARAMETERS:  method_name         - Pathname of method to execute
 *              integer_argument    - Argument to pass to the method
 *
 * RETURN:      None
 *
 * DESCRIPTION: Execute a sleep/wake related method, with one integer argument
 *              and no return value.
 *
 ******************************************************************************/
void acpi_hw_execute_sleep_method(char *method_name, u32 integer_argument)
{
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status;

	ACPI_FUNCTION_TRACE(hw_execute_sleep_method);

	if (!ACPI_STRCMP(METHOD_NAME__GTS, method_name) && !gts)
		return_VOID;

	if (!ACPI_STRCMP(METHOD_NAME__BFS, method_name) && !bfs)
		return_VOID;

	/* One argument, integer_argument */

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = (u64)integer_argument;

	status = acpi_evaluate_object(NULL, method_name, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_EXCEPTION((AE_INFO, status, "While executing method %s",
				method_name));
	}

	return_VOID;
}

#if (!ACPI_REDUCED_HARDWARE)
/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_legacy_sleep
 *
 * PARAMETERS:  sleep_state         - Which sleep state to enter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter a system sleep state via the legacy FADT PM registers
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/

acpi_status acpi_hw_legacy_sleep(u8 sleep_state)
{
	struct acpi_bit_register_info *sleep_type_reg_info;
	struct acpi_bit_register_info *sleep_enable_reg_info;
	u32 pm1a_control;
	u32 pm1b_control;
	u32 in_value;
	acpi_status status;

	ACPI_FUNCTION_TRACE(hw_legacy_sleep);

	sleep_type_reg_info =
	    acpi_hw_get_bit_register_info(ACPI_BITREG_SLEEP_TYPE);
	sleep_enable_reg_info =
	    acpi_hw_get_bit_register_info(ACPI_BITREG_SLEEP_ENABLE);

	/* Clear wake status */

	status =
	    acpi_write_bit_register(ACPI_BITREG_WAKE_STATUS, ACPI_CLEAR_STATUS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Clear all fixed and general purpose status bits */

	status = acpi_hw_clear_acpi_status();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (sleep_state != ACPI_STATE_S5) {
		/*
		 * Disable BM arbitration. This feature is contained within an
		 * optional register (PM2 Control), so ignore a BAD_ADDRESS
		 * exception.
		 */
		status = acpi_write_bit_register(ACPI_BITREG_ARB_DISABLE, 1);
		if (ACPI_FAILURE(status) && (status != AE_BAD_ADDRESS)) {
			return_ACPI_STATUS(status);
		}
	}

	/*
	 * 1) Disable/Clear all GPEs
	 * 2) Enable all wakeup GPEs
	 */
	status = acpi_hw_disable_all_gpes();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}
	acpi_gbl_system_awake_and_running = FALSE;

	status = acpi_hw_enable_all_wakeup_gpes();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Execute the _GTS method (Going To Sleep) */

	acpi_hw_execute_sleep_method(METHOD_NAME__GTS, sleep_state);

	/* Get current value of PM1A control */

	status = acpi_hw_register_read(ACPI_REGISTER_PM1_CONTROL,
				       &pm1a_control);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}
	ACPI_DEBUG_PRINT((ACPI_DB_INIT,
			  "Entering sleep state [S%u]\n", sleep_state));

	/* Clear the SLP_EN and SLP_TYP fields */

	pm1a_control &= ~(sleep_type_reg_info->access_bit_mask |
			  sleep_enable_reg_info->access_bit_mask);
	pm1b_control = pm1a_control;

	/* Insert the SLP_TYP bits */

	pm1a_control |=
	    (acpi_gbl_sleep_type_a << sleep_type_reg_info->bit_position);
	pm1b_control |=
	    (acpi_gbl_sleep_type_b << sleep_type_reg_info->bit_position);

	/*
	 * We split the writes of SLP_TYP and SLP_EN to workaround
	 * poorly implemented hardware.
	 */

	/* Write #1: write the SLP_TYP data to the PM1 Control registers */

	status = acpi_hw_write_pm1_control(pm1a_control, pm1b_control);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Insert the sleep enable (SLP_EN) bit */

	pm1a_control |= sleep_enable_reg_info->access_bit_mask;
	pm1b_control |= sleep_enable_reg_info->access_bit_mask;

	/* Flush caches, as per ACPI specification */

	ACPI_FLUSH_CPU_CACHE();

	tboot_sleep(sleep_state, pm1a_control, pm1b_control);

	/* Write #2: Write both SLP_TYP + SLP_EN */

	status = acpi_hw_write_pm1_control(pm1a_control, pm1b_control);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (sleep_state > ACPI_STATE_S3) {
		/*
		 * We wanted to sleep > S3, but it didn't happen (by virtue of the
		 * fact that we are still executing!)
		 *
		 * Wait ten seconds, then try again. This is to get S4/S5 to work on
		 * all machines.
		 *
		 * We wait so long to allow chipsets that poll this reg very slowly
		 * to still read the right value. Ideally, this block would go
		 * away entirely.
		 */
		acpi_os_stall(10000000);

		status = acpi_hw_register_write(ACPI_REGISTER_PM1_CONTROL,
						sleep_enable_reg_info->
						access_bit_mask);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Wait for transition back to Working State */

	do {
		status =
		    acpi_read_bit_register(ACPI_BITREG_WAKE_STATUS, &in_value);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

	} while (!in_value);

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_legacy_wake_prep
 *
 * PARAMETERS:  sleep_state         - Which sleep state we just exited
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform the first state of OS-independent ACPI cleanup after a
 *              sleep.
 *              Called with interrupts ENABLED.
 *
 ******************************************************************************/

acpi_status acpi_hw_legacy_wake_prep(u8 sleep_state)
{
	acpi_status status;
	struct acpi_bit_register_info *sleep_type_reg_info;
	struct acpi_bit_register_info *sleep_enable_reg_info;
	u32 pm1a_control;
	u32 pm1b_control;

	ACPI_FUNCTION_TRACE(hw_legacy_wake_prep);

	/*
	 * Set SLP_TYPE and SLP_EN to state S0.
	 * This is unclear from the ACPI Spec, but it is required
	 * by some machines.
	 */
	status = acpi_get_sleep_type_data(ACPI_STATE_S0,
					  &acpi_gbl_sleep_type_a,
					  &acpi_gbl_sleep_type_b);
	if (ACPI_SUCCESS(status)) {
		sleep_type_reg_info =
		    acpi_hw_get_bit_register_info(ACPI_BITREG_SLEEP_TYPE);
		sleep_enable_reg_info =
		    acpi_hw_get_bit_register_info(ACPI_BITREG_SLEEP_ENABLE);

		/* Get current value of PM1A control */

		status = acpi_hw_register_read(ACPI_REGISTER_PM1_CONTROL,
					       &pm1a_control);
		if (ACPI_SUCCESS(status)) {

			/* Clear the SLP_EN and SLP_TYP fields */

			pm1a_control &= ~(sleep_type_reg_info->access_bit_mask |
					  sleep_enable_reg_info->
					  access_bit_mask);
			pm1b_control = pm1a_control;

			/* Insert the SLP_TYP bits */

			pm1a_control |= (acpi_gbl_sleep_type_a <<
					 sleep_type_reg_info->bit_position);
			pm1b_control |= (acpi_gbl_sleep_type_b <<
					 sleep_type_reg_info->bit_position);

			/* Write the control registers and ignore any errors */

			(void)acpi_hw_write_pm1_control(pm1a_control,
							pm1b_control);
		}
	}

	acpi_hw_execute_sleep_method(METHOD_NAME__BFS, sleep_state);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_legacy_wake
 *
 * PARAMETERS:  sleep_state         - Which sleep state we just exited
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform OS-independent ACPI cleanup after a sleep
 *              Called with interrupts ENABLED.
 *
 ******************************************************************************/

acpi_status acpi_hw_legacy_wake(u8 sleep_state)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(hw_legacy_wake);

	/* Ensure enter_sleep_state_prep -> enter_sleep_state ordering */

	acpi_gbl_sleep_type_a = ACPI_SLEEP_TYPE_INVALID;
	acpi_hw_execute_sleep_method(METHOD_NAME__SST, ACPI_SST_WAKING);

	/*
	 * GPEs must be enabled before _WAK is called as GPEs
	 * might get fired there
	 *
	 * Restore the GPEs:
	 * 1) Disable/Clear all GPEs
	 * 2) Enable all runtime GPEs
	 */
	status = acpi_hw_disable_all_gpes();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_enable_all_runtime_gpes();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Now we can execute _WAK, etc. Some machines require that the GPEs
	 * are enabled before the wake methods are executed.
	 */
	acpi_hw_execute_sleep_method(METHOD_NAME__WAK, sleep_state);

	/*
	 * Some BIOS code assumes that WAK_STS will be cleared on resume
	 * and use it to determine whether the system is rebooting or
	 * resuming. Clear WAK_STS for compatibility.
	 */
	acpi_write_bit_register(ACPI_BITREG_WAKE_STATUS, 1);
	acpi_gbl_system_awake_and_running = TRUE;

	/* Enable power button */

	(void)
	    acpi_write_bit_register(acpi_gbl_fixed_event_info
				    [ACPI_EVENT_POWER_BUTTON].
				    enable_register_id, ACPI_ENABLE_EVENT);

	(void)
	    acpi_write_bit_register(acpi_gbl_fixed_event_info
				    [ACPI_EVENT_POWER_BUTTON].
				    status_register_id, ACPI_CLEAR_STATUS);

	/*
	 * Enable BM arbitration. This feature is contained within an
	 * optional register (PM2 Control), so ignore a BAD_ADDRESS
	 * exception.
	 */
	status = acpi_write_bit_register(ACPI_BITREG_ARB_DISABLE, 0);
	if (ACPI_FAILURE(status) && (status != AE_BAD_ADDRESS)) {
		return_ACPI_STATUS(status);
	}

	acpi_hw_execute_sleep_method(METHOD_NAME__SST, ACPI_SST_WORKING);
	return_ACPI_STATUS(status);
}

#endif				/* !ACPI_REDUCED_HARDWARE */

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
	u8 sleep_type_value;
	u64 sleep_status;

	ACPI_FUNCTION_TRACE(hw_extended_sleep);

	/* Extended sleep registers must be valid */

	if (!acpi_gbl_FADT.sleep_control.address ||
	    !acpi_gbl_FADT.sleep_status.address) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Clear wake status (WAK_STS) */

	status = acpi_write(ACPI_X_WAKE_STATUS, &acpi_gbl_FADT.sleep_status);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	acpi_gbl_system_awake_and_running = FALSE;

	/* Execute the _GTS method (Going To Sleep) */

	acpi_hw_execute_sleep_method(METHOD_NAME__GTS, sleep_state);

	/* Flush caches, as per ACPI specification */

	ACPI_FLUSH_CPU_CACHE();

	/*
	 * Set the SLP_TYP and SLP_EN bits.
	 *
	 * Note: We only use the first value returned by the \_Sx method
	 * (acpi_gbl_sleep_type_a) - As per ACPI specification.
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_INIT,
			  "Entering sleep state [S%u]\n", sleep_state));

	sleep_type_value =
	    ((acpi_gbl_sleep_type_a << ACPI_X_SLEEP_TYPE_POSITION) &
	     ACPI_X_SLEEP_TYPE_MASK);

	status = acpi_write((sleep_type_value | ACPI_X_SLEEP_ENABLE),
			    &acpi_gbl_FADT.sleep_control);
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
	acpi_status status;
	u8 sleep_type_value;

	ACPI_FUNCTION_TRACE(hw_extended_wake_prep);

	status = acpi_get_sleep_type_data(ACPI_STATE_S0,
					  &acpi_gbl_sleep_type_a,
					  &acpi_gbl_sleep_type_b);
	if (ACPI_SUCCESS(status)) {
		sleep_type_value =
		    ((acpi_gbl_sleep_type_a << ACPI_X_SLEEP_TYPE_POSITION) &
		     ACPI_X_SLEEP_TYPE_MASK);

		(void)acpi_write((sleep_type_value | ACPI_X_SLEEP_ENABLE),
				 &acpi_gbl_FADT.sleep_control);
	}

	acpi_hw_execute_sleep_method(METHOD_NAME__BFS, sleep_state);
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

	acpi_hw_execute_sleep_method(METHOD_NAME__SST, ACPI_SST_WAKING);
	acpi_hw_execute_sleep_method(METHOD_NAME__WAK, sleep_state);

	/*
	 * Some BIOS code assumes that WAK_STS will be cleared on resume
	 * and use it to determine whether the system is rebooting or
	 * resuming. Clear WAK_STS for compatibility.
	 */
	(void)acpi_write(ACPI_X_WAKE_STATUS, &acpi_gbl_FADT.sleep_status);
	acpi_gbl_system_awake_and_running = TRUE;

	acpi_hw_execute_sleep_method(METHOD_NAME__SST, ACPI_SST_WORKING);
	return_ACPI_STATUS(AE_OK);
}
