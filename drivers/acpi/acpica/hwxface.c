
/******************************************************************************
 *
 * Module Name: hwxface - Public ACPICA hardware interfaces
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
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwxface")

/******************************************************************************
 *
 * FUNCTION:    acpi_reset
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Set reset register in memory or IO space. Note: Does not
 *              support reset register in PCI config space, this must be
 *              handled separately.
 *
 ******************************************************************************/
acpi_status acpi_reset(void)
{
	struct acpi_generic_address *reset_reg;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_reset);

	reset_reg = &acpi_gbl_FADT.reset_register;

	/* Check if the reset register is supported */

	if (!(acpi_gbl_FADT.flags & ACPI_FADT_RESET_REGISTER) ||
	    !reset_reg->address) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Write the reset value to the reset register */

	status = acpi_write(acpi_gbl_FADT.reset_value, reset_reg);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_reset)

/******************************************************************************
 *
 * FUNCTION:    acpi_read
 *
 * PARAMETERS:  Value               - Where the value is returned
 *              Reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from either memory or IO space.
 *
 ******************************************************************************/
acpi_status acpi_read(u32 *value, struct acpi_generic_address *reg)
{
	u32 width;
	u64 address;
	acpi_status status;

	ACPI_FUNCTION_NAME(acpi_read);

	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within. However, don't return an error
	 * because the PM1A/B code must not fail if B isn't present.
	 */
	if (!reg) {
		return (AE_OK);
	}

	/* Get a local copy of the address. Handles possible alignment issues */

	ACPI_MOVE_64_TO_64(&address, &reg->address);
	if (!address) {
		return (AE_OK);
	}

	/* Supported widths are 8/16/32 */

	width = reg->bit_width;
	if ((width != 8) && (width != 16) && (width != 32)) {
		return (AE_SUPPORT);
	}

	/* Initialize entire 32-bit return value to zero */

	*value = 0;

	/*
	 * Two address spaces supported: Memory or IO.
	 * PCI_Config is not supported here because the GAS struct is insufficient
	 */
	switch (reg->space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		status = acpi_os_read_memory((acpi_physical_address) address,
					     value, width);
		break;

	case ACPI_ADR_SPACE_SYSTEM_IO:

		status =
		    acpi_os_read_port((acpi_io_address) address, value, width);
		break;

	default:
		ACPI_ERROR((AE_INFO,
			    "Unsupported address space: %X", reg->space_id));
		return (AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Read:  %8.8X width %2d from %8.8X%8.8X (%s)\n",
			  *value, width, ACPI_FORMAT_UINT64(address),
			  acpi_ut_get_region_name(reg->space_id)));

	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_read)

/******************************************************************************
 *
 * FUNCTION:    acpi_write
 *
 * PARAMETERS:  Value               - To be written
 *              Reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to either memory or IO space.
 *
 ******************************************************************************/
acpi_status acpi_write(u32 value, struct acpi_generic_address *reg)
{
	u32 width;
	u64 address;
	acpi_status status;

	ACPI_FUNCTION_NAME(acpi_write);

	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within. However, don't return an error
	 * because the PM1A/B code must not fail if B isn't present.
	 */
	if (!reg) {
		return (AE_OK);
	}

	/* Get a local copy of the address. Handles possible alignment issues */

	ACPI_MOVE_64_TO_64(&address, &reg->address);
	if (!address) {
		return (AE_OK);
	}

	/* Supported widths are 8/16/32 */

	width = reg->bit_width;
	if ((width != 8) && (width != 16) && (width != 32)) {
		return (AE_SUPPORT);
	}

	/*
	 * Two address spaces supported: Memory or IO.
	 * PCI_Config is not supported here because the GAS struct is insufficient
	 */
	switch (reg->space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		status = acpi_os_write_memory((acpi_physical_address) address,
					      value, width);
		break;

	case ACPI_ADR_SPACE_SYSTEM_IO:

		status = acpi_os_write_port((acpi_io_address) address, value,
					    width);
		break;

	default:
		ACPI_ERROR((AE_INFO,
			    "Unsupported address space: %X", reg->space_id));
		return (AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Wrote: %8.8X width %2d   to %8.8X%8.8X (%s)\n",
			  value, width, ACPI_FORMAT_UINT64(address),
			  acpi_ut_get_region_name(reg->space_id)));

	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_write)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_register_unlocked
 *
 * PARAMETERS:  register_id     - ID of ACPI bit_register to access
 *              return_value    - Value that was read from the register
 *
 * RETURN:      Status and the value read from specified Register. Value
 *              returned is normalized to bit0 (is shifted all the way right)
 *
 * DESCRIPTION: ACPI bit_register read function. Does not acquire the HW lock.
 *
 ******************************************************************************/
acpi_status acpi_get_register_unlocked(u32 register_id, u32 *return_value)
{
	u32 register_value = 0;
	struct acpi_bit_register_info *bit_reg_info;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_get_register_unlocked);

	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info(register_id);
	if (!bit_reg_info) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Read from the register */

	status = acpi_hw_register_read(bit_reg_info->parent_register,
				       &register_value);

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

ACPI_EXPORT_SYMBOL(acpi_get_register_unlocked)

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_register
 *
 * PARAMETERS:  register_id     - ID of ACPI bit_register to access
 *              return_value    - Value that was read from the register
 *
 * RETURN:      Status and the value read from specified Register. Value
 *              returned is normalized to bit0 (is shifted all the way right)
 *
 * DESCRIPTION: ACPI bit_register read function.
 *
 ******************************************************************************/
acpi_status acpi_get_register(u32 register_id, u32 *return_value)
{
	acpi_status status;
	acpi_cpu_flags flags;

	flags = acpi_os_acquire_lock(acpi_gbl_hardware_lock);
	status = acpi_get_register_unlocked(register_id, return_value);
	acpi_os_release_lock(acpi_gbl_hardware_lock, flags);

	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_get_register)

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_register
 *
 * PARAMETERS:  register_id     - ID of ACPI bit_register to access
 *              Value           - (only used on write) value to write to the
 *                                Register, NOT pre-normalized to the bit pos
 *
 * RETURN:      Status
 *
 * DESCRIPTION: ACPI Bit Register write function.
 *
 ******************************************************************************/
acpi_status acpi_set_register(u32 register_id, u32 value)
{
	u32 register_value = 0;
	struct acpi_bit_register_info *bit_reg_info;
	acpi_status status;
	acpi_cpu_flags lock_flags;

	ACPI_FUNCTION_TRACE_U32(acpi_set_register, register_id);

	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info(register_id);
	if (!bit_reg_info) {
		ACPI_ERROR((AE_INFO, "Bad ACPI HW RegisterId: %X",
			    register_id));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	lock_flags = acpi_os_acquire_lock(acpi_gbl_hardware_lock);

	/* Always do a register read first so we can insert the new bits  */

	status = acpi_hw_register_read(bit_reg_info->parent_register,
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
		 * Status Registers are different from the rest. Clear by
		 * writing 1, and writing 0 has no effect. So, the only relevant
		 * information is the single bit we're interested in, all others should
		 * be written as 0 so they will be left unchanged.
		 */
		value = ACPI_REGISTER_PREPARE_BITS(value,
						   bit_reg_info->bit_position,
						   bit_reg_info->
						   access_bit_mask);
		if (value) {
			status =
			    acpi_hw_register_write(ACPI_REGISTER_PM1_STATUS,
						   (u16) value);
			register_value = 0;
		}
		break;

	case ACPI_REGISTER_PM1_ENABLE:

		ACPI_REGISTER_INSERT_VALUE(register_value,
					   bit_reg_info->bit_position,
					   bit_reg_info->access_bit_mask,
					   value);

		status = acpi_hw_register_write(ACPI_REGISTER_PM1_ENABLE,
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

		status = acpi_hw_register_write(ACPI_REGISTER_PM1_CONTROL,
						(u16) register_value);
		break;

	case ACPI_REGISTER_PM2_CONTROL:

		status = acpi_hw_register_read(ACPI_REGISTER_PM2_CONTROL,
					       &register_value);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}

		ACPI_DEBUG_PRINT((ACPI_DB_IO,
				  "PM2 control: Read %X from %8.8X%8.8X\n",
				  register_value,
				  ACPI_FORMAT_UINT64(acpi_gbl_FADT.
						     xpm2_control_block.
						     address)));

		ACPI_REGISTER_INSERT_VALUE(register_value,
					   bit_reg_info->bit_position,
					   bit_reg_info->access_bit_mask,
					   value);

		ACPI_DEBUG_PRINT((ACPI_DB_IO,
				  "About to write %4.4X to %8.8X%8.8X\n",
				  register_value,
				  ACPI_FORMAT_UINT64(acpi_gbl_FADT.
						     xpm2_control_block.
						     address)));

		status = acpi_hw_register_write(ACPI_REGISTER_PM2_CONTROL,
						(u8) (register_value));
		break;

	default:
		break;
	}

      unlock_and_exit:

	acpi_os_release_lock(acpi_gbl_hardware_lock, lock_flags);

	/* Normalize the value that was read */

	ACPI_DEBUG_EXEC(register_value =
			((register_value & bit_reg_info->access_bit_mask) >>
			 bit_reg_info->bit_position));

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Set bits: %8.8X actual %8.8X register %X\n", value,
			  register_value, bit_reg_info->parent_register));
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_set_register)

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
acpi_get_sleep_type_data(u8 sleep_state, u8 *sleep_type_a, u8 *sleep_type_b)
{
	acpi_status status = AE_OK;
	struct acpi_evaluate_info *info;

	ACPI_FUNCTION_TRACE(acpi_get_sleep_type_data);

	/* Validate parameters */

	if ((sleep_state > ACPI_S_STATES_MAX) || !sleep_type_a || !sleep_type_b) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Allocate the evaluation information block */

	info = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_evaluate_info));
	if (!info) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	info->pathname =
	    ACPI_CAST_PTR(char, acpi_gbl_sleep_state_names[sleep_state]);

	/* Evaluate the namespace object containing the values for this state */

	status = acpi_ns_evaluate(info);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "%s while evaluating SleepState [%s]\n",
				  acpi_format_exception(status),
				  info->pathname));

		goto cleanup;
	}

	/* Must have a return object */

	if (!info->return_object) {
		ACPI_ERROR((AE_INFO, "No Sleep State object returned from [%s]",
			    info->pathname));
		status = AE_NOT_EXIST;
	}

	/* It must be of type Package */

	else if (ACPI_GET_OBJECT_TYPE(info->return_object) != ACPI_TYPE_PACKAGE) {
		ACPI_ERROR((AE_INFO,
			    "Sleep State return object is not a Package"));
		status = AE_AML_OPERAND_TYPE;
	}

	/*
	 * The package must have at least two elements. NOTE (March 2005): This
	 * goes against the current ACPI spec which defines this object as a
	 * package with one encoded DWORD element. However, existing practice
	 * by BIOS vendors seems to be to have 2 or more elements, at least
	 * one per sleep type (A/B).
	 */
	else if (info->return_object->package.count < 2) {
		ACPI_ERROR((AE_INFO,
			    "Sleep State return package does not have at least two elements"));
		status = AE_AML_NO_OPERAND;
	}

	/* The first two elements must both be of type Integer */

	else if ((ACPI_GET_OBJECT_TYPE(info->return_object->package.elements[0])
		  != ACPI_TYPE_INTEGER) ||
		 (ACPI_GET_OBJECT_TYPE(info->return_object->package.elements[1])
		  != ACPI_TYPE_INTEGER)) {
		ACPI_ERROR((AE_INFO,
			    "Sleep State return package elements are not both Integers (%s, %s)",
			    acpi_ut_get_object_type_name(info->return_object->
							 package.elements[0]),
			    acpi_ut_get_object_type_name(info->return_object->
							 package.elements[1])));
		status = AE_AML_OPERAND_TYPE;
	} else {
		/* Valid _Sx_ package size, type, and value */

		*sleep_type_a = (u8)
		    (info->return_object->package.elements[0])->integer.value;
		*sleep_type_b = (u8)
		    (info->return_object->package.elements[1])->integer.value;
	}

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"While evaluating SleepState [%s], bad Sleep object %p type %s",
				info->pathname, info->return_object,
				acpi_ut_get_object_type_name(info->
							     return_object)));
	}

	acpi_ut_remove_reference(info->return_object);

      cleanup:
	ACPI_FREE(info);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_sleep_type_data)
