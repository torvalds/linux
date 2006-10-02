
/*******************************************************************************
 *
 * Module Name: hwregs - Read/write access functions for the various ACPI
 *                       control and status registers.
 *
 ******************************************************************************/

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

#include <linux/module.h>

#include <acpi/acpi.h>
#include <acpi/acnamesp.h>
#include <acpi/acevents.h>

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwregs")

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_clear_acpi_status
 *
 * PARAMETERS:  Flags           - Lock the hardware or not
 *
 * RETURN:      none
 *
 * DESCRIPTION: Clears all fixed and general purpose status bits
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/
acpi_status acpi_hw_clear_acpi_status(u32 flags)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE("hw_clear_acpi_status");

	ACPI_DEBUG_PRINT((ACPI_DB_IO, "About to write %04X to %04X\n",
			  ACPI_BITMASK_ALL_FIXED_STATUS,
			  (u16) acpi_gbl_FADT->xpm1a_evt_blk.address));

	if (flags & ACPI_MTX_LOCK) {
		status = acpi_ut_acquire_mutex(ACPI_MTX_HARDWARE);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	status = acpi_hw_register_write(ACPI_MTX_DO_NOT_LOCK,
					ACPI_REGISTER_PM1_STATUS,
					ACPI_BITMASK_ALL_FIXED_STATUS);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/* Clear the fixed events */

	if (acpi_gbl_FADT->xpm1b_evt_blk.address) {
		status =
		    acpi_hw_low_level_write(16, ACPI_BITMASK_ALL_FIXED_STATUS,
					    &acpi_gbl_FADT->xpm1b_evt_blk);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}
	}

	/* Clear the GPE Bits in all GPE registers in all GPE blocks */

	status = acpi_ev_walk_gpe_list(acpi_hw_clear_gpe_block);

      unlock_and_exit:
	if (flags & ACPI_MTX_LOCK) {
		(void)acpi_ut_release_mutex(ACPI_MTX_HARDWARE);
	}
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_sleep_type_data
 *
 * PARAMETERS:  sleep_state         - Numeric sleep state
 *              *sleep_type_a        - Where SLP_TYPa is returned
 *              *sleep_type_b        - Where SLP_TYPb is returned
 *
 * RETURN:      Status - ACPI status
 *
 * DESCRIPTION: Obtain the SLP_TYPa and SLP_TYPb values for the requested sleep
 *              state.
 *
 ******************************************************************************/

acpi_status
acpi_get_sleep_type_data(u8 sleep_state, u8 * sleep_type_a, u8 * sleep_type_b)
{
	acpi_status status = AE_OK;
	struct acpi_parameter_info info;
	char *sleep_state_name;

	ACPI_FUNCTION_TRACE("acpi_get_sleep_type_data");

	/* Validate parameters */

	if ((sleep_state > ACPI_S_STATES_MAX) || !sleep_type_a || !sleep_type_b) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Evaluate the namespace object containing the values for this state */

	info.parameters = NULL;
	info.return_object = NULL;
	sleep_state_name =
	    ACPI_CAST_PTR(char, acpi_gbl_sleep_state_names[sleep_state]);

	status = acpi_ns_evaluate_by_name(sleep_state_name, &info);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "%s while evaluating sleep_state [%s]\n",
				  acpi_format_exception(status),
				  sleep_state_name));

		return_ACPI_STATUS(status);
	}

	/* Must have a return object */

	if (!info.return_object) {
		ACPI_ERROR((AE_INFO, "No Sleep State object returned from [%s]",
			    sleep_state_name));
		status = AE_NOT_EXIST;
	}

	/* It must be of type Package */

	else if (ACPI_GET_OBJECT_TYPE(info.return_object) != ACPI_TYPE_PACKAGE) {
		ACPI_ERROR((AE_INFO,
			    "Sleep State return object is not a Package"));
		status = AE_AML_OPERAND_TYPE;
	}

	/*
	 * The package must have at least two elements.  NOTE (March 2005): This
	 * goes against the current ACPI spec which defines this object as a
	 * package with one encoded DWORD element.  However, existing practice
	 * by BIOS vendors seems to be to have 2 or more elements, at least
	 * one per sleep type (A/B).
	 */
	else if (info.return_object->package.count < 2) {
		ACPI_ERROR((AE_INFO,
			    "Sleep State return package does not have at least two elements"));
		status = AE_AML_NO_OPERAND;
	}

	/* The first two elements must both be of type Integer */

	else if ((ACPI_GET_OBJECT_TYPE(info.return_object->package.elements[0])
		  != ACPI_TYPE_INTEGER) ||
		 (ACPI_GET_OBJECT_TYPE(info.return_object->package.elements[1])
		  != ACPI_TYPE_INTEGER)) {
		ACPI_ERROR((AE_INFO,
			    "Sleep State return package elements are not both Integers (%s, %s)",
			    acpi_ut_get_object_type_name(info.return_object->
							 package.elements[0]),
			    acpi_ut_get_object_type_name(info.return_object->
							 package.elements[1])));
		status = AE_AML_OPERAND_TYPE;
	} else {
		/* Valid _Sx_ package size, type, and value */

		*sleep_type_a = (u8)
		    (info.return_object->package.elements[0])->integer.value;
		*sleep_type_b = (u8)
		    (info.return_object->package.elements[1])->integer.value;
	}

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"While evaluating sleep_state [%s], bad Sleep object %p type %s",
				sleep_state_name, info.return_object,
				acpi_ut_get_object_type_name(info.
							     return_object)));
	}

	acpi_ut_remove_reference(info.return_object);
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_get_sleep_type_data);

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
		ACPI_ERROR((AE_INFO, "Invalid bit_register ID: %X",
			    register_id));
		return (NULL);
	}

	return (&acpi_gbl_bit_register_info[register_id]);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_register
 *
 * PARAMETERS:  register_id     - ID of ACPI bit_register to access
 *              return_value    - Value that was read from the register
 *              Flags           - Lock the hardware or not
 *
 * RETURN:      Status and the value read from specified Register.  Value
 *              returned is normalized to bit0 (is shifted all the way right)
 *
 * DESCRIPTION: ACPI bit_register read function.
 *
 ******************************************************************************/

acpi_status acpi_get_register(u32 register_id, u32 * return_value, u32 flags)
{
	u32 register_value = 0;
	struct acpi_bit_register_info *bit_reg_info;
	acpi_status status;

	ACPI_FUNCTION_TRACE("acpi_get_register");

	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info(register_id);
	if (!bit_reg_info) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (flags & ACPI_MTX_LOCK) {
		status = acpi_ut_acquire_mutex(ACPI_MTX_HARDWARE);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Read from the register */

	status = acpi_hw_register_read(ACPI_MTX_DO_NOT_LOCK,
				       bit_reg_info->parent_register,
				       &register_value);

	if (flags & ACPI_MTX_LOCK) {
		(void)acpi_ut_release_mutex(ACPI_MTX_HARDWARE);
	}

	if (ACPI_SUCCESS(status)) {

		/* Normalize the value that was read */

		register_value =
		    ((register_value & bit_reg_info->access_bit_mask)
		     >> bit_reg_info->bit_position);

		*return_value = register_value;

		ACPI_DEBUG_PRINT((ACPI_DB_IO, "Read value %8.8X register %X\n",
				  register_value,
				  bit_reg_info->parent_register));
	}

	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_get_register);

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_register
 *
 * PARAMETERS:  register_id     - ID of ACPI bit_register to access
 *              Value           - (only used on write) value to write to the
 *                                Register, NOT pre-normalized to the bit pos
 *              Flags           - Lock the hardware or not
 *
 * RETURN:      Status
 *
 * DESCRIPTION: ACPI Bit Register write function.
 *
 ******************************************************************************/

acpi_status acpi_set_register(u32 register_id, u32 value, u32 flags)
{
	u32 register_value = 0;
	struct acpi_bit_register_info *bit_reg_info;
	acpi_status status;

	ACPI_FUNCTION_TRACE_U32("acpi_set_register", register_id);

	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info(register_id);
	if (!bit_reg_info) {
		ACPI_ERROR((AE_INFO, "Bad ACPI HW register_id: %X",
			    register_id));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (flags & ACPI_MTX_LOCK) {
		status = acpi_ut_acquire_mutex(ACPI_MTX_HARDWARE);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Always do a register read first so we can insert the new bits  */

	status = acpi_hw_register_read(ACPI_MTX_DO_NOT_LOCK,
				       bit_reg_info->parent_register,
				       &register_value);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/*
	 * Decode the Register ID
	 * Register ID = [Register block ID] | [bit ID]
	 *
	 * Check bit ID to fine locate Register offset.
	 * Check Mask to determine Register offset, and then read-write.
	 */
	switch (bit_reg_info->parent_register) {
	case ACPI_REGISTER_PM1_STATUS:

		/*
		 * Status Registers are different from the rest.  Clear by
		 * writing 1, and writing 0 has no effect.  So, the only relevant
		 * information is the single bit we're interested in, all others should
		 * be written as 0 so they will be left unchanged.
		 */
		value = ACPI_REGISTER_PREPARE_BITS(value,
						   bit_reg_info->bit_position,
						   bit_reg_info->
						   access_bit_mask);
		if (value) {
			status = acpi_hw_register_write(ACPI_MTX_DO_NOT_LOCK,
							ACPI_REGISTER_PM1_STATUS,
							(u16) value);
			register_value = 0;
		}
		break;

	case ACPI_REGISTER_PM1_ENABLE:

		ACPI_REGISTER_INSERT_VALUE(register_value,
					   bit_reg_info->bit_position,
					   bit_reg_info->access_bit_mask,
					   value);

		status = acpi_hw_register_write(ACPI_MTX_DO_NOT_LOCK,
						ACPI_REGISTER_PM1_ENABLE,
						(u16) register_value);
		break;

	case ACPI_REGISTER_PM1_CONTROL:

		/*
		 * Write the PM1 Control register.
		 * Note that at this level, the fact that there are actually TWO
		 * registers (A and B - and B may not exist) is abstracted.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_IO, "PM1 control: Read %X\n",
				  register_value));

		ACPI_REGISTER_INSERT_VALUE(register_value,
					   bit_reg_info->bit_position,
					   bit_reg_info->access_bit_mask,
					   value);

		status = acpi_hw_register_write(ACPI_MTX_DO_NOT_LOCK,
						ACPI_REGISTER_PM1_CONTROL,
						(u16) register_value);
		break;

	case ACPI_REGISTER_PM2_CONTROL:

		status = acpi_hw_register_read(ACPI_MTX_DO_NOT_LOCK,
					       ACPI_REGISTER_PM2_CONTROL,
					       &register_value);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}

		ACPI_DEBUG_PRINT((ACPI_DB_IO,
				  "PM2 control: Read %X from %8.8X%8.8X\n",
				  register_value,
				  ACPI_FORMAT_UINT64(acpi_gbl_FADT->
						     xpm2_cnt_blk.address)));

		ACPI_REGISTER_INSERT_VALUE(register_value,
					   bit_reg_info->bit_position,
					   bit_reg_info->access_bit_mask,
					   value);

		ACPI_DEBUG_PRINT((ACPI_DB_IO,
				  "About to write %4.4X to %8.8X%8.8X\n",
				  register_value,
				  ACPI_FORMAT_UINT64(acpi_gbl_FADT->
						     xpm2_cnt_blk.address)));

		status = acpi_hw_register_write(ACPI_MTX_DO_NOT_LOCK,
						ACPI_REGISTER_PM2_CONTROL,
						(u8) (register_value));
		break;

	default:
		break;
	}

      unlock_and_exit:

	if (flags & ACPI_MTX_LOCK) {
		(void)acpi_ut_release_mutex(ACPI_MTX_HARDWARE);
	}

	/* Normalize the value that was read */

	ACPI_DEBUG_EXEC(register_value =
			((register_value & bit_reg_info->access_bit_mask) >>
			 bit_reg_info->bit_position));

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Set bits: %8.8X actual %8.8X register %X\n", value,
			  register_value, bit_reg_info->parent_register));
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_set_register);

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_register_read
 *
 * PARAMETERS:  use_lock            - Mutex hw access
 *              register_id         - register_iD + Offset
 *              return_value        - Where the register value is returned
 *
 * RETURN:      Status and the value read.
 *
 * DESCRIPTION: Acpi register read function.  Registers are read at the
 *              given offset.
 *
 ******************************************************************************/

acpi_status
acpi_hw_register_read(u8 use_lock, u32 register_id, u32 * return_value)
{
	u32 value1 = 0;
	u32 value2 = 0;
	acpi_status status;

	ACPI_FUNCTION_TRACE("hw_register_read");

	if (ACPI_MTX_LOCK == use_lock) {
		status = acpi_ut_acquire_mutex(ACPI_MTX_HARDWARE);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:	/* 16-bit access */

		status =
		    acpi_hw_low_level_read(16, &value1,
					   &acpi_gbl_FADT->xpm1a_evt_blk);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}

		/* PM1B is optional */

		status =
		    acpi_hw_low_level_read(16, &value2,
					   &acpi_gbl_FADT->xpm1b_evt_blk);
		value1 |= value2;
		break;

	case ACPI_REGISTER_PM1_ENABLE:	/* 16-bit access */

		status =
		    acpi_hw_low_level_read(16, &value1, &acpi_gbl_xpm1a_enable);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}

		/* PM1B is optional */

		status =
		    acpi_hw_low_level_read(16, &value2, &acpi_gbl_xpm1b_enable);
		value1 |= value2;
		break;

	case ACPI_REGISTER_PM1_CONTROL:	/* 16-bit access */

		status =
		    acpi_hw_low_level_read(16, &value1,
					   &acpi_gbl_FADT->xpm1a_cnt_blk);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}

		status =
		    acpi_hw_low_level_read(16, &value2,
					   &acpi_gbl_FADT->xpm1b_cnt_blk);
		value1 |= value2;
		break;

	case ACPI_REGISTER_PM2_CONTROL:	/* 8-bit access */

		status =
		    acpi_hw_low_level_read(8, &value1,
					   &acpi_gbl_FADT->xpm2_cnt_blk);
		break;

	case ACPI_REGISTER_PM_TIMER:	/* 32-bit access */

		status =
		    acpi_hw_low_level_read(32, &value1,
					   &acpi_gbl_FADT->xpm_tmr_blk);
		break;

	case ACPI_REGISTER_SMI_COMMAND_BLOCK:	/* 8-bit access */

		status = acpi_os_read_port(acpi_gbl_FADT->smi_cmd, &value1, 8);
		break;

	default:
		ACPI_ERROR((AE_INFO, "Unknown Register ID: %X", register_id));
		status = AE_BAD_PARAMETER;
		break;
	}

      unlock_and_exit:
	if (ACPI_MTX_LOCK == use_lock) {
		(void)acpi_ut_release_mutex(ACPI_MTX_HARDWARE);
	}

	if (ACPI_SUCCESS(status)) {
		*return_value = value1;
	}

	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_register_write
 *
 * PARAMETERS:  use_lock            - Mutex hw access
 *              register_id         - register_iD + Offset
 *              Value               - The value to write
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acpi register Write function.  Registers are written at the
 *              given offset.
 *
 ******************************************************************************/

acpi_status acpi_hw_register_write(u8 use_lock, u32 register_id, u32 value)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE("hw_register_write");

	if (ACPI_MTX_LOCK == use_lock) {
		status = acpi_ut_acquire_mutex(ACPI_MTX_HARDWARE);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:	/* 16-bit access */

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT->xpm1a_evt_blk);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}

		/* PM1B is optional */

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT->xpm1b_evt_blk);
		break;

	case ACPI_REGISTER_PM1_ENABLE:	/* 16-bit access */

		status =
		    acpi_hw_low_level_write(16, value, &acpi_gbl_xpm1a_enable);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}

		/* PM1B is optional */

		status =
		    acpi_hw_low_level_write(16, value, &acpi_gbl_xpm1b_enable);
		break;

	case ACPI_REGISTER_PM1_CONTROL:	/* 16-bit access */

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT->xpm1a_cnt_blk);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT->xpm1b_cnt_blk);
		break;

	case ACPI_REGISTER_PM1A_CONTROL:	/* 16-bit access */

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT->xpm1a_cnt_blk);
		break;

	case ACPI_REGISTER_PM1B_CONTROL:	/* 16-bit access */

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT->xpm1b_cnt_blk);
		break;

	case ACPI_REGISTER_PM2_CONTROL:	/* 8-bit access */

		status =
		    acpi_hw_low_level_write(8, value,
					    &acpi_gbl_FADT->xpm2_cnt_blk);
		break;

	case ACPI_REGISTER_PM_TIMER:	/* 32-bit access */

		status =
		    acpi_hw_low_level_write(32, value,
					    &acpi_gbl_FADT->xpm_tmr_blk);
		break;

	case ACPI_REGISTER_SMI_COMMAND_BLOCK:	/* 8-bit access */

		/* SMI_CMD is currently always in IO space */

		status = acpi_os_write_port(acpi_gbl_FADT->smi_cmd, value, 8);
		break;

	default:
		status = AE_BAD_PARAMETER;
		break;
	}

      unlock_and_exit:
	if (ACPI_MTX_LOCK == use_lock) {
		(void)acpi_ut_release_mutex(ACPI_MTX_HARDWARE);
	}

	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_low_level_read
 *
 * PARAMETERS:  Width               - 8, 16, or 32
 *              Value               - Where the value is returned
 *              Reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from either memory or IO space.
 *
 ******************************************************************************/

acpi_status
acpi_hw_low_level_read(u32 width, u32 * value, struct acpi_generic_address *reg)
{
	u64 address;
	acpi_status status;

	ACPI_FUNCTION_NAME("hw_low_level_read");

	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within. However, don't return an error
	 * because the PM1A/B code must not fail if B isn't present.
	 */
	if (!reg) {
		return (AE_OK);
	}

	/* Get a local copy of the address.  Handles possible alignment issues */

	ACPI_MOVE_64_TO_64(&address, &reg->address);
	if (!address) {
		return (AE_OK);
	}
	*value = 0;

	/*
	 * Two address spaces supported: Memory or IO.
	 * PCI_Config is not supported here because the GAS struct is insufficient
	 */
	switch (reg->address_space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		status = acpi_os_read_memory((acpi_physical_address) address,
					     value, width);
		break;

	case ACPI_ADR_SPACE_SYSTEM_IO:

		status = acpi_os_read_port((acpi_io_address) address,
					   value, width);
		break;

	default:
		ACPI_ERROR((AE_INFO,
			    "Unsupported address space: %X",
			    reg->address_space_id));
		return (AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Read:  %8.8X width %2d from %8.8X%8.8X (%s)\n",
			  *value, width,
			  ACPI_FORMAT_UINT64(address),
			  acpi_ut_get_region_name(reg->address_space_id)));

	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_low_level_write
 *
 * PARAMETERS:  Width               - 8, 16, or 32
 *              Value               - To be written
 *              Reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to either memory or IO space.
 *
 ******************************************************************************/

acpi_status
acpi_hw_low_level_write(u32 width, u32 value, struct acpi_generic_address * reg)
{
	u64 address;
	acpi_status status;

	ACPI_FUNCTION_NAME("hw_low_level_write");

	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within. However, don't return an error
	 * because the PM1A/B code must not fail if B isn't present.
	 */
	if (!reg) {
		return (AE_OK);
	}

	/* Get a local copy of the address.  Handles possible alignment issues */

	ACPI_MOVE_64_TO_64(&address, &reg->address);
	if (!address) {
		return (AE_OK);
	}

	/*
	 * Two address spaces supported: Memory or IO.
	 * PCI_Config is not supported here because the GAS struct is insufficient
	 */
	switch (reg->address_space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		status = acpi_os_write_memory((acpi_physical_address) address,
					      value, width);
		break;

	case ACPI_ADR_SPACE_SYSTEM_IO:

		status = acpi_os_write_port((acpi_io_address) address,
					    value, width);
		break;

	default:
		ACPI_ERROR((AE_INFO,
			    "Unsupported address space: %X",
			    reg->address_space_id));
		return (AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Wrote: %8.8X width %2d   to %8.8X%8.8X (%s)\n",
			  value, width,
			  ACPI_FORMAT_UINT64(address),
			  acpi_ut_get_region_name(reg->address_space_id)));

	return (status);
}
