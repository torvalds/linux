
/******************************************************************************
 *
 * Module Name: hwxface - Public ACPICA hardware interfaces
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

#include <linux/export.h>
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

	if (reset_reg->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
		/*
		 * For I/O space, write directly to the OSL. This
		 * bypasses the port validation mechanism, which may
		 * block a valid write to the reset register. Spec
		 * section 4.7.3.6 requires register width to be 8.
		 */
		status =
		    acpi_os_write_port((acpi_io_address) reset_reg->address,
				       acpi_gbl_FADT.reset_value, 8);
	} else {
		/* Write the reset value to the reset register */

		status = acpi_hw_write(acpi_gbl_FADT.reset_value, reset_reg);
	}

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
 * LIMITATIONS: <These limitations also apply to acpi_write>
 *      bit_width must be exactly 8, 16, 32, or 64.
 *      space_iD must be system_memory or system_iO.
 *      bit_offset and access_width are currently ignored, as there has
 *          not been a need to implement these.
 *
 ******************************************************************************/
acpi_status acpi_read(u64 *return_value, struct acpi_generic_address *reg)
{
	u32 value;
	u32 width;
	u64 address;
	acpi_status status;

	ACPI_FUNCTION_NAME(acpi_read);

	if (!return_value) {
		return (AE_BAD_PARAMETER);
	}

	/* Validate contents of the GAS register. Allow 64-bit transfers */

	status = acpi_hw_validate_register(reg, 64, &address);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Initialize entire 64-bit return value to zero */

	*return_value = 0;
	value = 0;

	/*
	 * Two address spaces supported: Memory or IO. PCI_Config is
	 * not supported here because the GAS structure is insufficient
	 */
	if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
		status = acpi_os_read_memory((acpi_physical_address)
					     address, return_value,
					     reg->bit_width);
		if (ACPI_FAILURE(status)) {
			return (status);
		}
	} else {		/* ACPI_ADR_SPACE_SYSTEM_IO, validated earlier */

		width = reg->bit_width;
		if (width == 64) {
			width = 32;	/* Break into two 32-bit transfers */
		}

		status = acpi_hw_read_port((acpi_io_address)
					   address, &value, width);
		if (ACPI_FAILURE(status)) {
			return (status);
		}
		*return_value = value;

		if (reg->bit_width == 64) {

			/* Read the top 32 bits */

			status = acpi_hw_read_port((acpi_io_address)
						   (address + 4), &value, 32);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
			*return_value |= ((u64)value << 32);
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Read:  %8.8X%8.8X width %2d from %8.8X%8.8X (%s)\n",
			  ACPI_FORMAT_UINT64(*return_value), reg->bit_width,
			  ACPI_FORMAT_UINT64(address),
			  acpi_ut_get_region_name(reg->space_id)));

	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_read)

/******************************************************************************
 *
 * FUNCTION:    acpi_write
 *
 * PARAMETERS:  Value               - Value to be written
 *              Reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to either memory or IO space.
 *
 ******************************************************************************/
acpi_status acpi_write(u64 value, struct acpi_generic_address *reg)
{
	u32 width;
	u64 address;
	acpi_status status;

	ACPI_FUNCTION_NAME(acpi_write);

	/* Validate contents of the GAS register. Allow 64-bit transfers */

	status = acpi_hw_validate_register(reg, 64, &address);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/*
	 * Two address spaces supported: Memory or IO. PCI_Config is
	 * not supported here because the GAS structure is insufficient
	 */
	if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
		status = acpi_os_write_memory((acpi_physical_address)
					      address, value, reg->bit_width);
		if (ACPI_FAILURE(status)) {
			return (status);
		}
	} else {		/* ACPI_ADR_SPACE_SYSTEM_IO, validated earlier */

		width = reg->bit_width;
		if (width == 64) {
			width = 32;	/* Break into two 32-bit transfers */
		}

		status = acpi_hw_write_port((acpi_io_address)
					    address, ACPI_LODWORD(value),
					    width);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		if (reg->bit_width == 64) {
			status = acpi_hw_write_port((acpi_io_address)
						    (address + 4),
						    ACPI_HIDWORD(value), 32);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Wrote: %8.8X%8.8X width %2d   to %8.8X%8.8X (%s)\n",
			  ACPI_FORMAT_UINT64(value), reg->bit_width,
			  ACPI_FORMAT_UINT64(address),
			  acpi_ut_get_region_name(reg->space_id)));

	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_write)

#if (!ACPI_REDUCED_HARDWARE)
/*******************************************************************************
 *
 * FUNCTION:    acpi_read_bit_register
 *
 * PARAMETERS:  register_id     - ID of ACPI Bit Register to access
 *              return_value    - Value that was read from the register,
 *                                normalized to bit position zero.
 *
 * RETURN:      Status and the value read from the specified Register. Value
 *              returned is normalized to bit0 (is shifted all the way right)
 *
 * DESCRIPTION: ACPI bit_register read function. Does not acquire the HW lock.
 *
 * SUPPORTS:    Bit fields in PM1 Status, PM1 Enable, PM1 Control, and
 *              PM2 Control.
 *
 * Note: The hardware lock is not required when reading the ACPI bit registers
 *       since almost all of them are single bit and it does not matter that
 *       the parent hardware register can be split across two physical
 *       registers. The only multi-bit field is SLP_TYP in the PM1 control
 *       register, but this field does not cross an 8-bit boundary (nor does
 *       it make much sense to actually read this field.)
 *
 ******************************************************************************/
acpi_status acpi_read_bit_register(u32 register_id, u32 *return_value)
{
	struct acpi_bit_register_info *bit_reg_info;
	u32 register_value;
	u32 value;
	acpi_status status;

	ACPI_FUNCTION_TRACE_U32(acpi_read_bit_register, register_id);

	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info(register_id);
	if (!bit_reg_info) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Read the entire parent register */

	status = acpi_hw_register_read(bit_reg_info->parent_register,
				       &register_value);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Normalize the value that was read, mask off other bits */

	value = ((register_value & bit_reg_info->access_bit_mask)
		 >> bit_reg_info->bit_position);

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "BitReg %X, ParentReg %X, Actual %8.8X, ReturnValue %8.8X\n",
			  register_id, bit_reg_info->parent_register,
			  register_value, value));

	*return_value = value;
	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_read_bit_register)

/*******************************************************************************
 *
 * FUNCTION:    acpi_write_bit_register
 *
 * PARAMETERS:  register_id     - ID of ACPI Bit Register to access
 *              Value           - Value to write to the register, in bit
 *                                position zero. The bit is automatically
 *                                shifted to the correct position.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: ACPI Bit Register write function. Acquires the hardware lock
 *              since most operations require a read/modify/write sequence.
 *
 * SUPPORTS:    Bit fields in PM1 Status, PM1 Enable, PM1 Control, and
 *              PM2 Control.
 *
 * Note that at this level, the fact that there may be actually two
 * hardware registers (A and B - and B may not exist) is abstracted.
 *
 ******************************************************************************/
acpi_status acpi_write_bit_register(u32 register_id, u32 value)
{
	struct acpi_bit_register_info *bit_reg_info;
	acpi_cpu_flags lock_flags;
	u32 register_value;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE_U32(acpi_write_bit_register, register_id);

	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info(register_id);
	if (!bit_reg_info) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	lock_flags = acpi_os_acquire_lock(acpi_gbl_hardware_lock);

	/*
	 * At this point, we know that the parent register is one of the
	 * following: PM1 Status, PM1 Enable, PM1 Control, or PM2 Control
	 */
	if (bit_reg_info->parent_register != ACPI_REGISTER_PM1_STATUS) {
		/*
		 * 1) Case for PM1 Enable, PM1 Control, and PM2 Control
		 *
		 * Perform a register read to preserve the bits that we are not
		 * interested in
		 */
		status = acpi_hw_register_read(bit_reg_info->parent_register,
					       &register_value);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}

		/*
		 * Insert the input bit into the value that was just read
		 * and write the register
		 */
		ACPI_REGISTER_INSERT_VALUE(register_value,
					   bit_reg_info->bit_position,
					   bit_reg_info->access_bit_mask,
					   value);

		status = acpi_hw_register_write(bit_reg_info->parent_register,
						register_value);
	} else {
		/*
		 * 2) Case for PM1 Status
		 *
		 * The Status register is different from the rest. Clear an event
		 * by writing 1, writing 0 has no effect. So, the only relevant
		 * information is the single bit we're interested in, all others
		 * should be written as 0 so they will be left unchanged.
		 */
		register_value = ACPI_REGISTER_PREPARE_BITS(value,
							    bit_reg_info->
							    bit_position,
							    bit_reg_info->
							    access_bit_mask);

		/* No need to write the register if value is all zeros */

		if (register_value) {
			status =
			    acpi_hw_register_write(ACPI_REGISTER_PM1_STATUS,
						   register_value);
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "BitReg %X, ParentReg %X, Value %8.8X, Actual %8.8X\n",
			  register_id, bit_reg_info->parent_register, value,
			  register_value));

unlock_and_exit:

	acpi_os_release_lock(acpi_gbl_hardware_lock, lock_flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_write_bit_register)
#endif				/* !ACPI_REDUCED_HARDWARE */
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

	else if (info->return_object->common.type != ACPI_TYPE_PACKAGE) {
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

	else if (((info->return_object->package.elements[0])->common.type
		  != ACPI_TYPE_INTEGER) ||
		 ((info->return_object->package.elements[1])->common.type
		  != ACPI_TYPE_INTEGER)) {
		ACPI_ERROR((AE_INFO,
			    "Sleep State return package elements are not both Integers "
			    "(%s, %s)",
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
