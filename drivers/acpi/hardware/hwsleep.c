
/******************************************************************************
 *
 * Name: hwsleep.c - ACPI Hardware Sleep/Wake Interface
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
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
#include <acpi/actables.h>

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwsleep")

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_firmware_waking_vector
 *
 * PARAMETERS:  physical_address    - Physical address of ACPI real mode
 *                                    entry point.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Access function for the firmware_waking_vector field in FACS
 *
 ******************************************************************************/
acpi_status
acpi_set_firmware_waking_vector(acpi_physical_address physical_address)
{
	struct acpi_table_facs *facs;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_set_firmware_waking_vector);

	/* Get the FACS */

	status = acpi_get_table_by_index(ACPI_TABLE_INDEX_FACS,
					 ACPI_CAST_INDIRECT_PTR(struct
								acpi_table_header,
								&facs));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Set the vector */

	if ((facs->length < 32) || (!(facs->xfirmware_waking_vector))) {
		/*
		 * ACPI 1.0 FACS or short table or optional X_ field is zero
		 */
		facs->firmware_waking_vector = (u32) physical_address;
	} else {
		/*
		 * ACPI 2.0 FACS with valid X_ field
		 */
		facs->xfirmware_waking_vector = physical_address;
	}

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_set_firmware_waking_vector)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_firmware_waking_vector
 *
 * PARAMETERS:  *physical_address   - Where the contents of
 *                                    the firmware_waking_vector field of
 *                                    the FACS will be returned.
 *
 * RETURN:      Status, vector
 *
 * DESCRIPTION: Access function for the firmware_waking_vector field in FACS
 *
 ******************************************************************************/
#ifdef ACPI_FUTURE_USAGE
acpi_status
acpi_get_firmware_waking_vector(acpi_physical_address * physical_address)
{
	struct acpi_table_facs *facs;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_get_firmware_waking_vector);

	if (!physical_address) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Get the FACS */

	status = acpi_get_table_by_index(ACPI_TABLE_INDEX_FACS,
					 ACPI_CAST_INDIRECT_PTR(struct
								acpi_table_header,
								&facs));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Get the vector */

	if ((facs->length < 32) || (!(facs->xfirmware_waking_vector))) {
		/*
		 * ACPI 1.0 FACS or short table or optional X_ field is zero
		 */
		*physical_address =
		    (acpi_physical_address) facs->firmware_waking_vector;
	} else {
		/*
		 * ACPI 2.0 FACS with valid X_ field
		 */
		*physical_address =
		    (acpi_physical_address) facs->xfirmware_waking_vector;
	}

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_get_firmware_waking_vector)
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

	/*
	 * _PSW methods could be run here to enable wake-on keyboard, LAN, etc.
	 */
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

	/* Set the system indicators to show the desired sleep state. */

	status = acpi_evaluate_object(NULL, METHOD_NAME__SST, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_EXCEPTION((AE_INFO, status,
				"While executing method _SST"));
	}

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_enter_sleep_state_prep)

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
	u32 PM1Acontrol;
	u32 PM1Bcontrol;
	struct acpi_bit_register_info *sleep_type_reg_info;
	struct acpi_bit_register_info *sleep_enable_reg_info;
	u32 in_value;
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_enter_sleep_state);

	if ((acpi_gbl_sleep_type_a > ACPI_SLEEP_TYPE_MAX) ||
	    (acpi_gbl_sleep_type_b > ACPI_SLEEP_TYPE_MAX)) {
		ACPI_ERROR((AE_INFO, "Sleep values out of range: A=%X B=%X",
			    acpi_gbl_sleep_type_a, acpi_gbl_sleep_type_b));
		return_ACPI_STATUS(AE_AML_OPERAND_VALUE);
	}

	sleep_type_reg_info =
	    acpi_hw_get_bit_register_info(ACPI_BITREG_SLEEP_TYPE_A);
	sleep_enable_reg_info =
	    acpi_hw_get_bit_register_info(ACPI_BITREG_SLEEP_ENABLE);

	/* Clear wake status */

	status = acpi_set_register(ACPI_BITREG_WAKE_STATUS, 1);
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

	/* Execute the _GTS method */

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = sleep_state;

	status = acpi_evaluate_object(NULL, METHOD_NAME__GTS, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		return_ACPI_STATUS(status);
	}

	/* Get current value of PM1A control */

	status = acpi_hw_register_read(ACPI_REGISTER_PM1_CONTROL, &PM1Acontrol);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}
	ACPI_DEBUG_PRINT((ACPI_DB_INIT,
			  "Entering sleep state [S%d]\n", sleep_state));

	/* Clear SLP_EN and SLP_TYP fields */

	PM1Acontrol &= ~(sleep_type_reg_info->access_bit_mask |
			 sleep_enable_reg_info->access_bit_mask);
	PM1Bcontrol = PM1Acontrol;

	/* Insert SLP_TYP bits */

	PM1Acontrol |=
	    (acpi_gbl_sleep_type_a << sleep_type_reg_info->bit_position);
	PM1Bcontrol |=
	    (acpi_gbl_sleep_type_b << sleep_type_reg_info->bit_position);

	/*
	 * We split the writes of SLP_TYP and SLP_EN to workaround
	 * poorly implemented hardware.
	 */

	/* Write #1: fill in SLP_TYP data */

	status = acpi_hw_register_write(ACPI_REGISTER_PM1A_CONTROL,
					PM1Acontrol);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_register_write(ACPI_REGISTER_PM1B_CONTROL,
					PM1Bcontrol);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Insert SLP_ENABLE bit */

	PM1Acontrol |= sleep_enable_reg_info->access_bit_mask;
	PM1Bcontrol |= sleep_enable_reg_info->access_bit_mask;

	/* Write #2: SLP_TYP + SLP_EN */

	ACPI_FLUSH_CPU_CACHE();

	status = acpi_hw_register_write(ACPI_REGISTER_PM1A_CONTROL,
					PM1Acontrol);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_register_write(ACPI_REGISTER_PM1B_CONTROL,
					PM1Bcontrol);
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
		 * We wait so long to allow chipsets that poll this reg very slowly to
		 * still read the right value. Ideally, this block would go
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
		status = acpi_get_register_unlocked(ACPI_BITREG_WAKE_STATUS,
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

	status = acpi_set_register(ACPI_BITREG_WAKE_STATUS, 1);
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

	status = acpi_os_write_port(acpi_gbl_FADT.smi_command,
				    (u32) acpi_gbl_FADT.S4bios_request, 8);

	do {
		acpi_os_stall(1000);
		status = acpi_get_register(ACPI_BITREG_WAKE_STATUS, &in_value);
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
	u32 PM1Acontrol;
	u32 PM1Bcontrol;

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
		    acpi_hw_get_bit_register_info(ACPI_BITREG_SLEEP_TYPE_A);
		sleep_enable_reg_info =
		    acpi_hw_get_bit_register_info(ACPI_BITREG_SLEEP_ENABLE);

		/* Get current value of PM1A control */

		status = acpi_hw_register_read(ACPI_REGISTER_PM1_CONTROL,
					       &PM1Acontrol);
		if (ACPI_SUCCESS(status)) {

			/* Clear SLP_EN and SLP_TYP fields */

			PM1Acontrol &= ~(sleep_type_reg_info->access_bit_mask |
					 sleep_enable_reg_info->
					 access_bit_mask);
			PM1Bcontrol = PM1Acontrol;

			/* Insert SLP_TYP bits */

			PM1Acontrol |=
			    (acpi_gbl_sleep_type_a << sleep_type_reg_info->
			     bit_position);
			PM1Bcontrol |=
			    (acpi_gbl_sleep_type_b << sleep_type_reg_info->
			     bit_position);

			/* Just ignore any errors */

			(void)acpi_hw_register_write(ACPI_REGISTER_PM1A_CONTROL,
						     PM1Acontrol);
			(void)acpi_hw_register_write(ACPI_REGISTER_PM1B_CONTROL,
						     PM1Bcontrol);
		}
	}

	/* Execute the _BFS method */

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = sleep_state;

	status = acpi_evaluate_object(NULL, METHOD_NAME__BFS, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_EXCEPTION((AE_INFO, status, "During Method _BFS"));
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

	acpi_gbl_system_awake_and_running = TRUE;

	/* Enable power button */

	(void)
	    acpi_set_register(acpi_gbl_fixed_event_info
			      [ACPI_EVENT_POWER_BUTTON].enable_register_id, 1);

	(void)
	    acpi_set_register(acpi_gbl_fixed_event_info
			      [ACPI_EVENT_POWER_BUTTON].status_register_id, 1);

	arg.integer.value = ACPI_SST_WORKING;
	status = acpi_evaluate_object(NULL, METHOD_NAME__SST, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		ACPI_EXCEPTION((AE_INFO, status, "During Method _SST"));
	}

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_leave_sleep_state)
