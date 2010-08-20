
/******************************************************************************
 *
 * Name: hwsleep.c - ACPI Hardware Sleep/Wake Interface
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2010, Intel Corp.
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
#include "actables.h"
#include <linux/tboot.h>

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwsleep")

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_firmware_waking_vector
 *
 * PARAMETERS:  physical_address    - 32-bit physical address of ACPI real mode
 *                                    entry point.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Sets the 32-bit firmware_waking_vector field of the FACS
 *
 ******************************************************************************/
acpi_status
acpi_set_firmware_waking_vector(u32 physical_address)
{
	ACPI_FUNCTION_TRACE(acpi_set_firmware_waking_vector);


	/*
	 * According to the ACPI specification 2.0c and later, the 64-bit
	 * waking vector should be cleared and the 32-bit waking vector should
	 * be used, unless we want the wake-up code to be called by the BIOS in
	 * Protected Mode.  Some systems (for example HP dv5-1004nr) are known
	 * to fail to resume if the 64-bit vector is used.
	 */

	/* Set the 32-bit vector */

	acpi_gbl_FACS->firmware_waking_vector = physical_address;

	/* Clear the 64-bit vector if it exists */

	if ((acpi_gbl_FACS->length > 32) && (acpi_gbl_FACS->version >= 1)) {
		acpi_gbl_FACS->xfirmware_waking_vector = 0;
	}

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_set_firmware_waking_vector)

#if ACPI_MACHINE_WIDTH == 64
/*******************************************************************************
 *
 * FUNCTION:    acpi_set_firmware_waking_vector64
 *
 * PARAMETERS:  physical_address    - 64-bit physical address of ACPI protected
 *                                    mode entry point.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Sets the 64-bit X_firmware_waking_vector field of the FACS, if
 *              it exists in the table. This function is intended for use with
 *              64-bit host operating systems.
 *
 ******************************************************************************/
acpi_status
acpi_set_firmware_waking_vector64(u64 physical_address)
{
	ACPI_FUNCTION_TRACE(acpi_set_firmware_waking_vector64);


	/* Determine if the 64-bit vector actually exists */

	if ((acpi_gbl_FACS->length <= 32) || (acpi_gbl_FACS->version < 1)) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Clear 32-bit vector, set the 64-bit X_ vector */

	acpi_gbl_FACS->firmware_waking_vector = 0;
	acpi_gbl_FACS->xfirmware_waking_vector = physical_address;

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_set_firmware_waking_vector64)
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_enter_sleep_state_prep
 *
 * PARAMETERS:  sleep_state         - Which sleep state to enter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Prepare to enter a system sleep state (see ACPI 2.0 spec p 231)
 *              This function must execute with interrupts enabled.
 *              We break sleeping into 2 stages so that OSPM can handle
 *              various OS-specific tasks between the two steps.
 *
 ******************************************************************************/
acpi_status acpi_enter_sleep_state_prep(u8 sleep_state)
{
	acpi_status status;
	struct acpi_object_list arg_list;
	union acpi_object arg;

	ACPI_FUNCTION_TRACE(acpi_enter_sleep_state_prep);

	/* _PSW methods could be run here to enable wake-on keyboard, LAN, etc. */

	status = acpi_get_sleep_type_data(sleep_state,
					  &acpi_gbl_sleep_type_a,
					  &acpi_gbl_sleep_type_b);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Setup parameter object */

	arg_list.count = 1;
	arg_list.pointer = &arg;

	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = sleep_state;

	/* Run the _PTS method */

	status = acpi_evaluate_object(NULL, METHOD_NAME__PTS, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		return_ACPI_STATUS(status);
	}

	/* Setup the argument to _SST */

	switch (sleep_state) {
	case ACPI_STATE_S0:
		arg.integer.value = ACPI_SST_WORKING;
		break;

	case ACPI_STATE_S1:
	case ACPI_STATE_S2:
	case ACPI_STATE_S3:
		arg.integer.value = ACPI_SST_SLEEPING;
		break;

	case ACPI_STATE_S4:
		arg.integer.value = ACPI_SST_SLEEP_CONTEXT;
		break;

	default:
		arg.integer.value = ACPI_SST_INDICATOR_OFF;	/* Default is off */
		break;
	}

	/*
	 * Set the system indicators to show the desired sleep state.
	 * _SST is an optional method (return no error if not found)
	 */
	status = acpi_evaluate_object(NULL, METHOD_NAME__SST, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_EXCEPTION((AE_INFO, status,
				"While executing method _SST"));
	}

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_enter_sleep_state_prep)

static unsigned int gts, bfs;
module_param(gts, uint, 0644);
module_param(bfs, uint, 0644);
MODULE_PARM_DESC(gts, "Enable evaluation of _GTS on suspend.");
MODULE_PARM_DESC(bfs, "Enable evaluation of _BFS on resume".);

/*******************************************************************************
 *
 * FUNCTION:    acpi_enter_sleep_state
 *
 * PARAMETERS:  sleep_state         - Which sleep state to enter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter a system sleep state (see ACPI 2.0 spec p 231)
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/
acpi_status asmlinkage acpi_enter_sleep_state(u8 sleep_state)
{
	u32 pm1a_control;
	u32 pm1b_control;
	struct acpi_bit_register_info *sleep_type_reg_info;
	struct acpi_bit_register_info *sleep_enable_reg_info;
	u32 in_value;
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_enter_sleep_state);

	if ((acpi_gbl_sleep_type_a > ACPI_SLEEP_TYPE_MAX) ||
	    (acpi_gbl_sleep_type_b > ACPI_SLEEP_TYPE_MAX)) {
		ACPI_ERROR((AE_INFO, "Sleep values out of range: A=0x%X B=0x%X",
			    acpi_gbl_sleep_type_a, acpi_gbl_sleep_type_b));
		return_ACPI_STATUS(AE_AML_OPERAND_VALUE);
	}

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

	if (gts) {
		/* Execute the _GTS method */

		arg_list.count = 1;
		arg_list.pointer = &arg;
		arg.type = ACPI_TYPE_INTEGER;
		arg.integer.value = sleep_state;

		status = acpi_evaluate_object(NULL, METHOD_NAME__GTS, &arg_list, NULL);
		if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
			return_ACPI_STATUS(status);
		}
	}

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

	/* Wait until we enter sleep state */

	do {
		status = acpi_read_bit_register(ACPI_BITREG_WAKE_STATUS,
						    &in_value);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/* Spin until we wake */

	} while (!in_value);

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_enter_sleep_state)

/*******************************************************************************
 *
 * FUNCTION:    acpi_enter_sleep_state_s4bios
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform a S4 bios request.
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/
acpi_status asmlinkage acpi_enter_sleep_state_s4bios(void)
{
	u32 in_value;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_enter_sleep_state_s4bios);

	/* Clear the wake status bit (PM1) */

	status =
	    acpi_write_bit_register(ACPI_BITREG_WAKE_STATUS, ACPI_CLEAR_STATUS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_clear_acpi_status();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
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

	ACPI_FLUSH_CPU_CACHE();

	status = acpi_hw_write_port(acpi_gbl_FADT.smi_command,
				    (u32) acpi_gbl_FADT.S4bios_request, 8);

	do {
		acpi_os_stall(1000);
		status =
		    acpi_read_bit_register(ACPI_BITREG_WAKE_STATUS, &in_value);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	} while (!in_value);

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_enter_sleep_state_s4bios)

/*******************************************************************************
 *
 * FUNCTION:    acpi_leave_sleep_state_prep
 *
 * PARAMETERS:  sleep_state         - Which sleep state we are exiting
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform the first state of OS-independent ACPI cleanup after a
 *              sleep.
 *              Called with interrupts DISABLED.
 *
 ******************************************************************************/
acpi_status acpi_leave_sleep_state_prep(u8 sleep_state)
{
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status;
	struct acpi_bit_register_info *sleep_type_reg_info;
	struct acpi_bit_register_info *sleep_enable_reg_info;
	u32 pm1a_control;
	u32 pm1b_control;

	ACPI_FUNCTION_TRACE(acpi_leave_sleep_state_prep);

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

	if (bfs) {
		/* Execute the _BFS method */

		arg_list.count = 1;
		arg_list.pointer = &arg;
		arg.type = ACPI_TYPE_INTEGER;
		arg.integer.value = sleep_state;

		status = acpi_evaluate_object(NULL, METHOD_NAME__BFS, &arg_list, NULL);
		if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
			ACPI_EXCEPTION((AE_INFO, status, "During Method _BFS"));
		}
	}
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_leave_sleep_state
 *
 * PARAMETERS:  sleep_state         - Which sleep state we just exited
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform OS-independent ACPI cleanup after a sleep
 *              Called with interrupts ENABLED.
 *
 ******************************************************************************/
acpi_status acpi_leave_sleep_state(u8 sleep_state)
{
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_leave_sleep_state);

	/* Ensure enter_sleep_state_prep -> enter_sleep_state ordering */

	acpi_gbl_sleep_type_a = ACPI_SLEEP_TYPE_INVALID;

	/* Setup parameter object */

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;

	/* Ignore any errors from these methods */

	arg.integer.value = ACPI_SST_WAKING;
	status = acpi_evaluate_object(NULL, METHOD_NAME__SST, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_EXCEPTION((AE_INFO, status, "During Method _SST"));
	}

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

	arg.integer.value = sleep_state;
	status = acpi_evaluate_object(NULL, METHOD_NAME__WAK, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_EXCEPTION((AE_INFO, status, "During Method _WAK"));
	}
	/* TBD: _WAK "sometimes" returns stuff - do we want to look at it? */

	/*
	 * Some BIOSes assume that WAK_STS will be cleared on resume and use
	 * it to determine whether the system is rebooting or resuming. Clear
	 * it for compatibility.
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

	arg.integer.value = ACPI_SST_WORKING;
	status = acpi_evaluate_object(NULL, METHOD_NAME__SST, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_EXCEPTION((AE_INFO, status, "During Method _SST"));
	}

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_leave_sleep_state)
