/******************************************************************************
 *
 * Module Name: hwxface - Public ACPICA hardware interfaces
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
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

#define EXPORT_ACPI_INTERFACES

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
		 * For I/O space, write directly to the OSL. This bypasses the port
		 * validation mechanism, which may block a valid write to the reset
		 * register.
		 *
		 * NOTE:
		 * The ACPI spec requires the reset register width to be 8, so we
		 * hardcode it here and ignore the FADT value. This maintains
		 * compatibility with other ACPI implementations that have allowed
		 * BIOS code with bad register width values to go unnoticed.
		 */
		status = acpi_os_write_port((acpi_io_address)reset_reg->address,
					    acpi_gbl_FADT.reset_value,
					    ACPI_RESET_REGISTER_WIDTH);
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
 * PARAMETERS:  value               - Where the value is returned
 *              reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from either memory or IO space.
 *
 * LIMITATIONS: <These limitations also apply to acpi_write>
 *      bit_width must be exactly 8, 16, 32, or 64.
 *      space_ID must be system_memory or system_IO.
 *      bit_offset and access_width are currently ignored, as there has
 *          not been a need to implement these.
 *
 ******************************************************************************/
acpi_status acpi_read(u64 *return_value, struct acpi_generic_address *reg)
{
	acpi_status status;

	ACPI_FUNCTION_NAME(acpi_read);

	status = acpi_hw_read(return_value, reg);
	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_read)

/******************************************************************************
 *
 * FUNCTION:    acpi_write
 *
 * PARAMETERS:  value               - Value to be written
 *              reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to either memory or IO space.
 *
 ******************************************************************************/
acpi_status acpi_write(u64 value, struct acpi_generic_address *reg)
{
	acpi_status status;

	ACPI_FUNCTION_NAME(acpi_write);

	status = acpi_hw_write(value, reg);
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
 *              value           - Value to write to the register, in bit
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
 * RETURN:      Status
 *
 * DESCRIPTION: Obtain the SLP_TYPa and SLP_TYPb values for the requested
 *              sleep state via the appropriate \_Sx object.
 *
 *  The sleep state package returned from the corresponding \_Sx_ object
 *  must contain at least one integer.
 *
 *  March 2005:
 *  Added support for a package that contains two integers. This
 *  goes against the ACPI specification which defines this object as a
 *  package with one encoded DWORD integer. However, existing practice
 *  by many BIOS vendors is to return a package with 2 or more integer
 *  elements, at least one per sleep type (A/B).
 *
 *  January 2013:
 *  Therefore, we must be prepared to accept a package with either a
 *  single integer or multiple integers.
 *
 *  The single integer DWORD format is as follows:
 *      BYTE 0 - Value for the PM1A SLP_TYP register
 *      BYTE 1 - Value for the PM1B SLP_TYP register
 *      BYTE 2-3 - Reserved
 *
 *  The dual integer format is as follows:
 *      Integer 0 - Value for the PM1A SLP_TYP register
 *      Integer 1 - Value for the PM1A SLP_TYP register
 *
 ******************************************************************************/
acpi_status
acpi_get_sleep_type_data(u8 sleep_state, u8 *sleep_type_a, u8 *sleep_type_b)
{
	acpi_status status;
	struct acpi_evaluate_info *info;
	union acpi_operand_object **elements;

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

	/*
	 * Evaluate the \_Sx namespace object containing the register values
	 * for this state
	 */
	info->relative_pathname = acpi_gbl_sleep_state_names[sleep_state];

	status = acpi_ns_evaluate(info);
	if (ACPI_FAILURE(status)) {
		if (status == AE_NOT_FOUND) {

			/* The _Sx states are optional, ignore NOT_FOUND */

			goto final_cleanup;
		}

		goto warning_cleanup;
	}

	/* Must have a return object */

	if (!info->return_object) {
		ACPI_ERROR((AE_INFO, "No Sleep State object returned from [%s]",
			    info->relative_pathname));
		status = AE_AML_NO_RETURN_VALUE;
		goto warning_cleanup;
	}

	/* Return object must be of type Package */

	if (info->return_object->common.type != ACPI_TYPE_PACKAGE) {
		ACPI_ERROR((AE_INFO,
			    "Sleep State return object is not a Package"));
		status = AE_AML_OPERAND_TYPE;
		goto return_value_cleanup;
	}

	/*
	 * Any warnings about the package length or the object types have
	 * already been issued by the predefined name module -- there is no
	 * need to repeat them here.
	 */
	elements = info->return_object->package.elements;
	switch (info->return_object->package.count) {
	case 0:

		status = AE_AML_PACKAGE_LIMIT;
		break;

	case 1:

		if (elements[0]->common.type != ACPI_TYPE_INTEGER) {
			status = AE_AML_OPERAND_TYPE;
			break;
		}

		/* A valid _Sx_ package with one integer */

		*sleep_type_a = (u8)elements[0]->integer.value;
		*sleep_type_b = (u8)(elements[0]->integer.value >> 8);
		break;

	case 2:
	default:

		if ((elements[0]->common.type != ACPI_TYPE_INTEGER) ||
		    (elements[1]->common.type != ACPI_TYPE_INTEGER)) {
			status = AE_AML_OPERAND_TYPE;
			break;
		}

		/* A valid _Sx_ package with two integers */

		*sleep_type_a = (u8)elements[0]->integer.value;
		*sleep_type_b = (u8)elements[1]->integer.value;
		break;
	}

return_value_cleanup:
	acpi_ut_remove_reference(info->return_object);

warning_cleanup:
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"While evaluating Sleep State [%s]",
				info->relative_pathname));
	}

final_cleanup:
	ACPI_FREE(info);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_sleep_type_data)
