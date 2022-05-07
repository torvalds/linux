// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Name: hwxfsleep.c - ACPI Hardware Sleep/Wake External Interfaces
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

#define EXPORT_ACPI_INTERFACES

#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwxfsleep")

/* Local prototypes */
#if (!ACPI_REDUCED_HARDWARE)
static acpi_status
acpi_hw_set_firmware_waking_vector(struct acpi_table_facs *facs,
				   acpi_physical_address physical_address,
				   acpi_physical_address physical_address64);
#endif

/*
 * These functions are removed for the ACPI_REDUCED_HARDWARE case:
 *      acpi_set_firmware_waking_vector
 *      acpi_enter_sleep_state_s4bios
 */

#if (!ACPI_REDUCED_HARDWARE)
/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_set_firmware_waking_vector
 *
 * PARAMETERS:  facs                - Pointer to FACS table
 *              physical_address    - 32-bit physical address of ACPI real mode
 *                                    entry point
 *              physical_address64  - 64-bit physical address of ACPI protected
 *                                    mode entry point
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Sets the firmware_waking_vector fields of the FACS
 *
 ******************************************************************************/

static acpi_status
acpi_hw_set_firmware_waking_vector(struct acpi_table_facs *facs,
				   acpi_physical_address physical_address,
				   acpi_physical_address physical_address64)
{
	ACPI_FUNCTION_TRACE(acpi_hw_set_firmware_waking_vector);


	/*
	 * According to the ACPI specification 2.0c and later, the 64-bit
	 * waking vector should be cleared and the 32-bit waking vector should
	 * be used, unless we want the wake-up code to be called by the BIOS in
	 * Protected Mode.  Some systems (for example HP dv5-1004nr) are known
	 * to fail to resume if the 64-bit vector is used.
	 */

	/* Set the 32-bit vector */

	facs->firmware_waking_vector = (u32)physical_address;

	if (facs->length > 32) {
		if (facs->version >= 1) {

			/* Set the 64-bit vector */

			facs->xfirmware_waking_vector = physical_address64;
		} else {
			/* Clear the 64-bit vector if it exists */

			facs->xfirmware_waking_vector = 0;
		}
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_firmware_waking_vector
 *
 * PARAMETERS:  physical_address    - 32-bit physical address of ACPI real mode
 *                                    entry point
 *              physical_address64  - 64-bit physical address of ACPI protected
 *                                    mode entry point
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Sets the firmware_waking_vector fields of the FACS
 *
 ******************************************************************************/

acpi_status
acpi_set_firmware_waking_vector(acpi_physical_address physical_address,
				acpi_physical_address physical_address64)
{

	ACPI_FUNCTION_TRACE(acpi_set_firmware_waking_vector);

	if (acpi_gbl_FACS) {
		(void)acpi_hw_set_firmware_waking_vector(acpi_gbl_FACS,
							 physical_address,
							 physical_address64);
	}

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_set_firmware_waking_vector)

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
acpi_status acpi_enter_sleep_state_s4bios(void)
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
	 * 1) Disable all GPEs
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
				    (u32)acpi_gbl_FADT.s4_bios_request, 8);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	do {
		acpi_os_stall(ACPI_USEC_PER_MSEC);
		status =
		    acpi_read_bit_register(ACPI_BITREG_WAKE_STATUS, &in_value);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

	} while (!in_value);

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_enter_sleep_state_s4bios)
#endif				/* !ACPI_REDUCED_HARDWARE */

/*******************************************************************************
 *
 * FUNCTION:    acpi_enter_sleep_state_prep
 *
 * PARAMETERS:  sleep_state         - Which sleep state to enter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Prepare to enter a system sleep state.
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
	u32 sst_value;

	ACPI_FUNCTION_TRACE(acpi_enter_sleep_state_prep);

	status = acpi_get_sleep_type_data(sleep_state,
					  &acpi_gbl_sleep_type_a,
					  &acpi_gbl_sleep_type_b);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Execute the _PTS method (Prepare To Sleep) */

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = sleep_state;

	status =
	    acpi_evaluate_object(NULL, METHOD_PATHNAME__PTS, &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		return_ACPI_STATUS(status);
	}

	/* Setup the argument to the _SST method (System STatus) */

	switch (sleep_state) {
	case ACPI_STATE_S0:

		sst_value = ACPI_SST_WORKING;
		break;

	case ACPI_STATE_S1:
	case ACPI_STATE_S2:
	case ACPI_STATE_S3:

		sst_value = ACPI_SST_SLEEPING;
		break;

	case ACPI_STATE_S4:

		sst_value = ACPI_SST_SLEEP_CONTEXT;
		break;

	default:

		sst_value = ACPI_SST_INDICATOR_OFF;	/* Default is off */
		break;
	}

	/*
	 * Set the system indicators to show the desired sleep state.
	 * _SST is an optional method (return no error if not found)
	 */
	acpi_hw_execute_sleep_method(METHOD_PATHNAME__SST, sst_value);
	return_ACPI_STATUS(AE_OK);
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
 * DESCRIPTION: Enter a system sleep state
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/
acpi_status acpi_enter_sleep_state(u8 sleep_state)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_enter_sleep_state);

	if ((acpi_gbl_sleep_type_a > ACPI_SLEEP_TYPE_MAX) ||
	    (acpi_gbl_sleep_type_b > ACPI_SLEEP_TYPE_MAX)) {
		ACPI_ERROR((AE_INFO, "Sleep values out of range: A=0x%X B=0x%X",
			    acpi_gbl_sleep_type_a, acpi_gbl_sleep_type_b));
		return_ACPI_STATUS(AE_AML_OPERAND_VALUE);
	}

#if !ACPI_REDUCED_HARDWARE
	if (!acpi_gbl_reduced_hardware)
		status = acpi_hw_legacy_sleep(sleep_state);
	else
#endif
		status = acpi_hw_extended_sleep(sleep_state);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_enter_sleep_state)

/*******************************************************************************
 *
 * FUNCTION:    acpi_leave_sleep_state_prep
 *
 * PARAMETERS:  sleep_state         - Which sleep state we are exiting
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform the first state of OS-independent ACPI cleanup after a
 *              sleep. Called with interrupts DISABLED.
 *              We break wake/resume into 2 stages so that OSPM can handle
 *              various OS-specific tasks between the two steps.
 *
 ******************************************************************************/
acpi_status acpi_leave_sleep_state_prep(u8 sleep_state)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_leave_sleep_state_prep);

#if !ACPI_REDUCED_HARDWARE
	if (!acpi_gbl_reduced_hardware)
		status = acpi_hw_legacy_wake_prep(sleep_state);
	else
#endif
		status = acpi_hw_extended_wake_prep(sleep_state);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_leave_sleep_state_prep)

/*******************************************************************************
 *
 * FUNCTION:    acpi_leave_sleep_state
 *
 * PARAMETERS:  sleep_state         - Which sleep state we are exiting
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform OS-independent ACPI cleanup after a sleep
 *              Called with interrupts ENABLED.
 *
 ******************************************************************************/
acpi_status acpi_leave_sleep_state(u8 sleep_state)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_leave_sleep_state);

#if !ACPI_REDUCED_HARDWARE
	if (!acpi_gbl_reduced_hardware)
		status = acpi_hw_legacy_wake(sleep_state);
	else
#endif
		status = acpi_hw_extended_wake(sleep_state);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_leave_sleep_state)
