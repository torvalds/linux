
/*******************************************************************************
 *
 * Module Name: hwregs - Read/write access functions for the various ACPI
 *                       control and status registers.
 *
 ******************************************************************************/

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
#include "accommon.h"
#include "acnamesp.h"
#include "acevents.h"

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwregs")

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_clear_acpi_status
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clears all fixed and general purpose status bits
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/
acpi_status acpi_hw_clear_acpi_status(void)
{
	acpi_status status;
	acpi_cpu_flags lock_flags = 0;

	ACPI_FUNCTION_TRACE(hw_clear_acpi_status);

	ACPI_DEBUG_PRINT((ACPI_DB_IO, "About to write %04X to %04X\n",
			  ACPI_BITMASK_ALL_FIXED_STATUS,
			  (u16) acpi_gbl_FADT.xpm1a_event_block.address));

	lock_flags = acpi_os_acquire_lock(acpi_gbl_hardware_lock);

	status = acpi_hw_register_write(ACPI_REGISTER_PM1_STATUS,
					ACPI_BITMASK_ALL_FIXED_STATUS);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/* Clear the fixed events */

	if (acpi_gbl_FADT.xpm1b_event_block.address) {
		status = acpi_write(ACPI_BITMASK_ALL_FIXED_STATUS,
				    &acpi_gbl_FADT.xpm1b_event_block);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}
	}

	/* Clear the GPE Bits in all GPE registers in all GPE blocks */

	status = acpi_ev_walk_gpe_list(acpi_hw_clear_gpe_block, NULL);

      unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_hardware_lock, lock_flags);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_get_register_bit_mask
 *
 * PARAMETERS:  register_id         - Index of ACPI Register to access
 *
 * RETURN:      The bitmask to be used when accessing the register
 *
 * DESCRIPTION: Map register_id into a register bitmask.
 *
 ******************************************************************************/

struct acpi_bit_register_info *acpi_hw_get_bit_register_info(u32 register_id)
{
	ACPI_FUNCTION_ENTRY();

	if (register_id > ACPI_BITREG_MAX) {
		ACPI_ERROR((AE_INFO, "Invalid BitRegister ID: %X",
			    register_id));
		return (NULL);
	}

	return (&acpi_gbl_bit_register_info[register_id]);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_register_read
 *
 * PARAMETERS:  register_id         - ACPI Register ID
 *              return_value        - Where the register value is returned
 *
 * RETURN:      Status and the value read.
 *
 * DESCRIPTION: Read from the specified ACPI register
 *
 ******************************************************************************/
acpi_status
acpi_hw_register_read(u32 register_id, u32 * return_value)
{
	u32 value1 = 0;
	u32 value2 = 0;
	acpi_status status;

	ACPI_FUNCTION_TRACE(hw_register_read);

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:	/* 16-bit access */

		status = acpi_read(&value1, &acpi_gbl_FADT.xpm1a_event_block);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* PM1B is optional */

		status = acpi_read(&value2, &acpi_gbl_FADT.xpm1b_event_block);
		value1 |= value2;
		break;

	case ACPI_REGISTER_PM1_ENABLE:	/* 16-bit access */

		status = acpi_read(&value1, &acpi_gbl_xpm1a_enable);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* PM1B is optional */

		status = acpi_read(&value2, &acpi_gbl_xpm1b_enable);
		value1 |= value2;
		break;

	case ACPI_REGISTER_PM1_CONTROL:	/* 16-bit access */

		status = acpi_read(&value1, &acpi_gbl_FADT.xpm1a_control_block);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		status = acpi_read(&value2, &acpi_gbl_FADT.xpm1b_control_block);
		value1 |= value2;
		break;

	case ACPI_REGISTER_PM2_CONTROL:	/* 8-bit access */

		status = acpi_read(&value1, &acpi_gbl_FADT.xpm2_control_block);
		break;

	case ACPI_REGISTER_PM_TIMER:	/* 32-bit access */

		status = acpi_read(&value1, &acpi_gbl_FADT.xpm_timer_block);
		break;

	case ACPI_REGISTER_SMI_COMMAND_BLOCK:	/* 8-bit access */

		status =
		    acpi_os_read_port(acpi_gbl_FADT.smi_command, &value1, 8);
		break;

	default:
		ACPI_ERROR((AE_INFO, "Unknown Register ID: %X", register_id));
		status = AE_BAD_PARAMETER;
		break;
	}

      exit:

	if (ACPI_SUCCESS(status)) {
		*return_value = value1;
	}

	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_register_write
 *
 * PARAMETERS:  register_id         - ACPI Register ID
 *              Value               - The value to write
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to the specified ACPI register
 *
 * NOTE: In accordance with the ACPI specification, this function automatically
 * preserves the value of the following bits, meaning that these bits cannot be
 * changed via this interface:
 *
 * PM1_CONTROL[0] = SCI_EN
 * PM1_CONTROL[9]
 * PM1_STATUS[11]
 *
 * ACPI References:
 * 1) Hardware Ignored Bits: When software writes to a register with ignored
 *      bit fields, it preserves the ignored bit fields
 * 2) SCI_EN: OSPM always preserves this bit position
 *
 ******************************************************************************/

acpi_status acpi_hw_register_write(u32 register_id, u32 value)
{
	acpi_status status;
	u32 read_value;

	ACPI_FUNCTION_TRACE(hw_register_write);

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:	/* 16-bit access */

		/* Perform a read first to preserve certain bits (per ACPI spec) */

		status = acpi_hw_register_read(ACPI_REGISTER_PM1_STATUS,
					       &read_value);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* Insert the bits to be preserved */

		ACPI_INSERT_BITS(value, ACPI_PM1_STATUS_PRESERVED_BITS,
				 read_value);

		/* Now we can write the data */

		status = acpi_write(value, &acpi_gbl_FADT.xpm1a_event_block);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* PM1B is optional */

		status = acpi_write(value, &acpi_gbl_FADT.xpm1b_event_block);
		break;

	case ACPI_REGISTER_PM1_ENABLE:	/* 16-bit access */

		status = acpi_write(value, &acpi_gbl_xpm1a_enable);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* PM1B is optional */

		status = acpi_write(value, &acpi_gbl_xpm1b_enable);
		break;

	case ACPI_REGISTER_PM1_CONTROL:	/* 16-bit access */

		/*
		 * Perform a read first to preserve certain bits (per ACPI spec)
		 */
		status = acpi_hw_register_read(ACPI_REGISTER_PM1_CONTROL,
					       &read_value);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* Insert the bits to be preserved */

		ACPI_INSERT_BITS(value, ACPI_PM1_CONTROL_PRESERVED_BITS,
				 read_value);

		/* Now we can write the data */

		status = acpi_write(value, &acpi_gbl_FADT.xpm1a_control_block);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		status = acpi_write(value, &acpi_gbl_FADT.xpm1b_control_block);
		break;

	case ACPI_REGISTER_PM1A_CONTROL:	/* 16-bit access */

		status = acpi_write(value, &acpi_gbl_FADT.xpm1a_control_block);
		break;

	case ACPI_REGISTER_PM1B_CONTROL:	/* 16-bit access */

		status = acpi_write(value, &acpi_gbl_FADT.xpm1b_control_block);
		break;

	case ACPI_REGISTER_PM2_CONTROL:	/* 8-bit access */

		status = acpi_write(value, &acpi_gbl_FADT.xpm2_control_block);
		break;

	case ACPI_REGISTER_PM_TIMER:	/* 32-bit access */

		status = acpi_write(value, &acpi_gbl_FADT.xpm_timer_block);
		break;

	case ACPI_REGISTER_SMI_COMMAND_BLOCK:	/* 8-bit access */

		/* SMI_CMD is currently always in IO space */

		status =
		    acpi_os_write_port(acpi_gbl_FADT.smi_command, value, 8);
		break;

	default:
		status = AE_BAD_PARAMETER;
		break;
	}

      exit:
	return_ACPI_STATUS(status);
}
