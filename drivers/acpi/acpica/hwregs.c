// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: hwregs - Read/write access functions for the various ACPI
 *                       control and status registers.
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acevents.h"

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwregs")

#if (!ACPI_REDUCED_HARDWARE)
/* Local Prototypes */
static u8
acpi_hw_get_access_bit_width(u64 address,
			     struct acpi_generic_address *reg,
			     u8 max_bit_width);

static acpi_status
acpi_hw_read_multiple(u32 *value,
		      struct acpi_generic_address *register_a,
		      struct acpi_generic_address *register_b);

static acpi_status
acpi_hw_write_multiple(u32 value,
		       struct acpi_generic_address *register_a,
		       struct acpi_generic_address *register_b);

#endif				/* !ACPI_REDUCED_HARDWARE */

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_get_access_bit_width
 *
 * PARAMETERS:  address             - GAS register address
 *              reg                 - GAS register structure
 *              max_bit_width       - Max bit_width supported (32 or 64)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Obtain optimal access bit width
 *
 ******************************************************************************/

static u8
acpi_hw_get_access_bit_width(u64 address,
			     struct acpi_generic_address *reg, u8 max_bit_width)
{
	u8 access_bit_width;

	/*
	 * GAS format "register", used by FADT:
	 *  1. Detected if bit_offset is 0 and bit_width is 8/16/32/64;
	 *  2. access_size field is ignored and bit_width field is used for
	 *     determining the boundary of the IO accesses.
	 * GAS format "region", used by APEI registers:
	 *  1. Detected if bit_offset is not 0 or bit_width is not 8/16/32/64;
	 *  2. access_size field is used for determining the boundary of the
	 *     IO accesses;
	 *  3. bit_offset/bit_width fields are used to describe the "region".
	 *
	 * Note: This algorithm assumes that the "Address" fields should always
	 *       contain aligned values.
	 */
	if (!reg->bit_offset && reg->bit_width &&
	    ACPI_IS_POWER_OF_TWO(reg->bit_width) &&
	    ACPI_IS_ALIGNED(reg->bit_width, 8)) {
		access_bit_width = reg->bit_width;
	} else if (reg->access_width) {
		access_bit_width = ACPI_ACCESS_BIT_WIDTH(reg->access_width);
	} else {
		access_bit_width =
		    ACPI_ROUND_UP_POWER_OF_TWO_8(reg->bit_offset +
						 reg->bit_width);
		if (access_bit_width <= 8) {
			access_bit_width = 8;
		} else {
			while (!ACPI_IS_ALIGNED(address, access_bit_width >> 3)) {
				access_bit_width >>= 1;
			}
		}
	}

	/* Maximum IO port access bit width is 32 */

	if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
		max_bit_width = 32;
	}

	/*
	 * Return access width according to the requested maximum access bit width,
	 * as the caller should know the format of the register and may enforce
	 * a 32-bit accesses.
	 */
	if (access_bit_width < max_bit_width) {
		return (access_bit_width);
	}
	return (max_bit_width);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_validate_register
 *
 * PARAMETERS:  reg                 - GAS register structure
 *              max_bit_width       - Max bit_width supported (32 or 64)
 *              address             - Pointer to where the gas->address
 *                                    is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Validate the contents of a GAS register. Checks the GAS
 *              pointer, Address, space_id, bit_width, and bit_offset.
 *
 ******************************************************************************/

acpi_status
acpi_hw_validate_register(struct acpi_generic_address *reg,
			  u8 max_bit_width, u64 *address)
{
	u8 bit_width;
	u8 access_width;

	/* Must have a valid pointer to a GAS structure */

	if (!reg) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Copy the target address. This handles possible alignment issues.
	 * Address must not be null. A null address also indicates an optional
	 * ACPI register that is not supported, so no error message.
	 */
	ACPI_MOVE_64_TO_64(address, &reg->address);
	if (!(*address)) {
		return (AE_BAD_ADDRESS);
	}

	/* Validate the space_ID */

	if ((reg->space_id != ACPI_ADR_SPACE_SYSTEM_MEMORY) &&
	    (reg->space_id != ACPI_ADR_SPACE_SYSTEM_IO)) {
		ACPI_ERROR((AE_INFO,
			    "Unsupported address space: 0x%X", reg->space_id));
		return (AE_SUPPORT);
	}

	/* Validate the access_width */

	if (reg->access_width > 4) {
		ACPI_ERROR((AE_INFO,
			    "Unsupported register access width: 0x%X",
			    reg->access_width));
		return (AE_SUPPORT);
	}

	/* Validate the bit_width, convert access_width into number of bits */

	access_width =
	    acpi_hw_get_access_bit_width(*address, reg, max_bit_width);
	bit_width =
	    ACPI_ROUND_UP(reg->bit_offset + reg->bit_width, access_width);
	if (max_bit_width < bit_width) {
		ACPI_WARNING((AE_INFO,
			      "Requested bit width 0x%X is smaller than register bit width 0x%X",
			      max_bit_width, bit_width));
		return (AE_SUPPORT);
	}

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_read
 *
 * PARAMETERS:  value               - Where the value is returned
 *              reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from either memory or IO space. This is a 64-bit max
 *              version of acpi_read.
 *
 * LIMITATIONS: <These limitations also apply to acpi_hw_write>
 *      space_ID must be system_memory or system_IO.
 *
 ******************************************************************************/

acpi_status acpi_hw_read(u64 *value, struct acpi_generic_address *reg)
{
	u64 address;
	u8 access_width;
	u32 bit_width;
	u8 bit_offset;
	u64 value64;
	u32 value32;
	u8 index;
	acpi_status status;

	ACPI_FUNCTION_NAME(hw_read);

	/* Validate contents of the GAS register */

	status = acpi_hw_validate_register(reg, 64, &address);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/*
	 * Initialize entire 64-bit return value to zero, convert access_width
	 * into number of bits based
	 */
	*value = 0;
	access_width = acpi_hw_get_access_bit_width(address, reg, 64);
	bit_width = reg->bit_offset + reg->bit_width;
	bit_offset = reg->bit_offset;

	/*
	 * Two address spaces supported: Memory or IO. PCI_Config is
	 * not supported here because the GAS structure is insufficient
	 */
	index = 0;
	while (bit_width) {
		if (bit_offset >= access_width) {
			value64 = 0;
			bit_offset -= access_width;
		} else {
			if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
				status =
				    acpi_os_read_memory((acpi_physical_address)
							address +
							index *
							ACPI_DIV_8
							(access_width),
							&value64, access_width);
			} else {	/* ACPI_ADR_SPACE_SYSTEM_IO, validated earlier */

				status = acpi_hw_read_port((acpi_io_address)
							   address +
							   index *
							   ACPI_DIV_8
							   (access_width),
							   &value32,
							   access_width);
				value64 = (u64)value32;
			}
		}

		/*
		 * Use offset style bit writes because "Index * AccessWidth" is
		 * ensured to be less than 64-bits by acpi_hw_validate_register().
		 */
		ACPI_SET_BITS(value, index * access_width,
			      ACPI_MASK_BITS_ABOVE_64(access_width), value64);

		bit_width -=
		    bit_width > access_width ? access_width : bit_width;
		index++;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Read:  %8.8X%8.8X width %2d from %8.8X%8.8X (%s)\n",
			  ACPI_FORMAT_UINT64(*value), access_width,
			  ACPI_FORMAT_UINT64(address),
			  acpi_ut_get_region_name(reg->space_id)));

	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_write
 *
 * PARAMETERS:  value               - Value to be written
 *              reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to either memory or IO space. This is a 64-bit max
 *              version of acpi_write.
 *
 ******************************************************************************/

acpi_status acpi_hw_write(u64 value, struct acpi_generic_address *reg)
{
	u64 address;
	u8 access_width;
	u32 bit_width;
	u8 bit_offset;
	u64 value64;
	u8 index;
	acpi_status status;

	ACPI_FUNCTION_NAME(hw_write);

	/* Validate contents of the GAS register */

	status = acpi_hw_validate_register(reg, 64, &address);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Convert access_width into number of bits based */

	access_width = acpi_hw_get_access_bit_width(address, reg, 64);
	bit_width = reg->bit_offset + reg->bit_width;
	bit_offset = reg->bit_offset;

	/*
	 * Two address spaces supported: Memory or IO. PCI_Config is
	 * not supported here because the GAS structure is insufficient
	 */
	index = 0;
	while (bit_width) {
		/*
		 * Use offset style bit reads because "Index * AccessWidth" is
		 * ensured to be less than 64-bits by acpi_hw_validate_register().
		 */
		value64 = ACPI_GET_BITS(&value, index * access_width,
					ACPI_MASK_BITS_ABOVE_64(access_width));

		if (bit_offset >= access_width) {
			bit_offset -= access_width;
		} else {
			if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
				status =
				    acpi_os_write_memory((acpi_physical_address)
							 address +
							 index *
							 ACPI_DIV_8
							 (access_width),
							 value64, access_width);
			} else {	/* ACPI_ADR_SPACE_SYSTEM_IO, validated earlier */

				status = acpi_hw_write_port((acpi_io_address)
							    address +
							    index *
							    ACPI_DIV_8
							    (access_width),
							    (u32)value64,
							    access_width);
			}
		}

		/*
		 * Index * access_width is ensured to be less than 32-bits by
		 * acpi_hw_validate_register().
		 */
		bit_width -=
		    bit_width > access_width ? access_width : bit_width;
		index++;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Wrote: %8.8X%8.8X width %2d   to %8.8X%8.8X (%s)\n",
			  ACPI_FORMAT_UINT64(value), access_width,
			  ACPI_FORMAT_UINT64(address),
			  acpi_ut_get_region_name(reg->space_id)));

	return (status);
}

#if (!ACPI_REDUCED_HARDWARE)
/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_clear_acpi_status
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clears all fixed and general purpose status bits
 *
 ******************************************************************************/

acpi_status acpi_hw_clear_acpi_status(void)
{
	acpi_status status;
	acpi_cpu_flags lock_flags = 0;

	ACPI_FUNCTION_TRACE(hw_clear_acpi_status);

	ACPI_DEBUG_PRINT((ACPI_DB_IO, "About to write %04X to %8.8X%8.8X\n",
			  ACPI_BITMASK_ALL_FIXED_STATUS,
			  ACPI_FORMAT_UINT64(acpi_gbl_xpm1a_status.address)));

	lock_flags = acpi_os_acquire_lock(acpi_gbl_hardware_lock);

	/* Clear the fixed events in PM1 A/B */

	status = acpi_hw_register_write(ACPI_REGISTER_PM1_STATUS,
					ACPI_BITMASK_ALL_FIXED_STATUS);

	acpi_os_release_lock(acpi_gbl_hardware_lock, lock_flags);

	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Clear the GPE Bits in all GPE registers in all GPE blocks */

	status = acpi_ev_walk_gpe_list(acpi_hw_clear_gpe_block, NULL);

exit:
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_get_bit_register_info
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
		ACPI_ERROR((AE_INFO, "Invalid BitRegister ID: 0x%X",
			    register_id));
		return (NULL);
	}

	return (&acpi_gbl_bit_register_info[register_id]);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_write_pm1_control
 *
 * PARAMETERS:  pm1a_control        - Value to be written to PM1A control
 *              pm1b_control        - Value to be written to PM1B control
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write the PM1 A/B control registers. These registers are
 *              different than than the PM1 A/B status and enable registers
 *              in that different values can be written to the A/B registers.
 *              Most notably, the SLP_TYP bits can be different, as per the
 *              values returned from the _Sx predefined methods.
 *
 ******************************************************************************/

acpi_status acpi_hw_write_pm1_control(u32 pm1a_control, u32 pm1b_control)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(hw_write_pm1_control);

	status =
	    acpi_hw_write(pm1a_control, &acpi_gbl_FADT.xpm1a_control_block);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (acpi_gbl_FADT.xpm1b_control_block.address) {
		status =
		    acpi_hw_write(pm1b_control,
				  &acpi_gbl_FADT.xpm1b_control_block);
	}
	return_ACPI_STATUS(status);
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
acpi_status acpi_hw_register_read(u32 register_id, u32 *return_value)
{
	u32 value = 0;
	u64 value64;
	acpi_status status;

	ACPI_FUNCTION_TRACE(hw_register_read);

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:	/* PM1 A/B: 16-bit access each */

		status = acpi_hw_read_multiple(&value,
					       &acpi_gbl_xpm1a_status,
					       &acpi_gbl_xpm1b_status);
		break;

	case ACPI_REGISTER_PM1_ENABLE:	/* PM1 A/B: 16-bit access each */

		status = acpi_hw_read_multiple(&value,
					       &acpi_gbl_xpm1a_enable,
					       &acpi_gbl_xpm1b_enable);
		break;

	case ACPI_REGISTER_PM1_CONTROL:	/* PM1 A/B: 16-bit access each */

		status = acpi_hw_read_multiple(&value,
					       &acpi_gbl_FADT.
					       xpm1a_control_block,
					       &acpi_gbl_FADT.
					       xpm1b_control_block);

		/*
		 * Zero the write-only bits. From the ACPI specification, "Hardware
		 * Write-Only Bits": "Upon reads to registers with write-only bits,
		 * software masks out all write-only bits."
		 */
		value &= ~ACPI_PM1_CONTROL_WRITEONLY_BITS;
		break;

	case ACPI_REGISTER_PM2_CONTROL:	/* 8-bit access */

		status =
		    acpi_hw_read(&value64, &acpi_gbl_FADT.xpm2_control_block);
		value = (u32)value64;
		break;

	case ACPI_REGISTER_PM_TIMER:	/* 32-bit access */

		status = acpi_hw_read(&value64, &acpi_gbl_FADT.xpm_timer_block);
		value = (u32)value64;
		break;

	case ACPI_REGISTER_SMI_COMMAND_BLOCK:	/* 8-bit access */

		status =
		    acpi_hw_read_port(acpi_gbl_FADT.smi_command, &value, 8);
		break;

	default:

		ACPI_ERROR((AE_INFO, "Unknown Register ID: 0x%X", register_id));
		status = AE_BAD_PARAMETER;
		break;
	}

	if (ACPI_SUCCESS(status)) {
		*return_value = (u32)value;
	}

	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_register_write
 *
 * PARAMETERS:  register_id         - ACPI Register ID
 *              value               - The value to write
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
	u64 read_value64;

	ACPI_FUNCTION_TRACE(hw_register_write);

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:	/* PM1 A/B: 16-bit access each */
		/*
		 * Handle the "ignored" bit in PM1 Status. According to the ACPI
		 * specification, ignored bits are to be preserved when writing.
		 * Normally, this would mean a read/modify/write sequence. However,
		 * preserving a bit in the status register is different. Writing a
		 * one clears the status, and writing a zero preserves the status.
		 * Therefore, we must always write zero to the ignored bit.
		 *
		 * This behavior is clarified in the ACPI 4.0 specification.
		 */
		value &= ~ACPI_PM1_STATUS_PRESERVED_BITS;

		status = acpi_hw_write_multiple(value,
						&acpi_gbl_xpm1a_status,
						&acpi_gbl_xpm1b_status);
		break;

	case ACPI_REGISTER_PM1_ENABLE:	/* PM1 A/B: 16-bit access each */

		status = acpi_hw_write_multiple(value,
						&acpi_gbl_xpm1a_enable,
						&acpi_gbl_xpm1b_enable);
		break;

	case ACPI_REGISTER_PM1_CONTROL:	/* PM1 A/B: 16-bit access each */
		/*
		 * Perform a read first to preserve certain bits (per ACPI spec)
		 * Note: This includes SCI_EN, we never want to change this bit
		 */
		status = acpi_hw_read_multiple(&read_value,
					       &acpi_gbl_FADT.
					       xpm1a_control_block,
					       &acpi_gbl_FADT.
					       xpm1b_control_block);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* Insert the bits to be preserved */

		ACPI_INSERT_BITS(value, ACPI_PM1_CONTROL_PRESERVED_BITS,
				 read_value);

		/* Now we can write the data */

		status = acpi_hw_write_multiple(value,
						&acpi_gbl_FADT.
						xpm1a_control_block,
						&acpi_gbl_FADT.
						xpm1b_control_block);
		break;

	case ACPI_REGISTER_PM2_CONTROL:	/* 8-bit access */
		/*
		 * For control registers, all reserved bits must be preserved,
		 * as per the ACPI spec.
		 */
		status =
		    acpi_hw_read(&read_value64,
				 &acpi_gbl_FADT.xpm2_control_block);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}
		read_value = (u32)read_value64;

		/* Insert the bits to be preserved */

		ACPI_INSERT_BITS(value, ACPI_PM2_CONTROL_PRESERVED_BITS,
				 read_value);

		status =
		    acpi_hw_write(value, &acpi_gbl_FADT.xpm2_control_block);
		break;

	case ACPI_REGISTER_PM_TIMER:	/* 32-bit access */

		status = acpi_hw_write(value, &acpi_gbl_FADT.xpm_timer_block);
		break;

	case ACPI_REGISTER_SMI_COMMAND_BLOCK:	/* 8-bit access */

		/* SMI_CMD is currently always in IO space */

		status =
		    acpi_hw_write_port(acpi_gbl_FADT.smi_command, value, 8);
		break;

	default:

		ACPI_ERROR((AE_INFO, "Unknown Register ID: 0x%X", register_id));
		status = AE_BAD_PARAMETER;
		break;
	}

exit:
	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_read_multiple
 *
 * PARAMETERS:  value               - Where the register value is returned
 *              register_a           - First ACPI register (required)
 *              register_b           - Second ACPI register (optional)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from the specified two-part ACPI register (such as PM1 A/B)
 *
 ******************************************************************************/

static acpi_status
acpi_hw_read_multiple(u32 *value,
		      struct acpi_generic_address *register_a,
		      struct acpi_generic_address *register_b)
{
	u32 value_a = 0;
	u32 value_b = 0;
	u64 value64;
	acpi_status status;

	/* The first register is always required */

	status = acpi_hw_read(&value64, register_a);
	if (ACPI_FAILURE(status)) {
		return (status);
	}
	value_a = (u32)value64;

	/* Second register is optional */

	if (register_b->address) {
		status = acpi_hw_read(&value64, register_b);
		if (ACPI_FAILURE(status)) {
			return (status);
		}
		value_b = (u32)value64;
	}

	/*
	 * OR the two return values together. No shifting or masking is necessary,
	 * because of how the PM1 registers are defined in the ACPI specification:
	 *
	 * "Although the bits can be split between the two register blocks (each
	 * register block has a unique pointer within the FADT), the bit positions
	 * are maintained. The register block with unimplemented bits (that is,
	 * those implemented in the other register block) always returns zeros,
	 * and writes have no side effects"
	 */
	*value = (value_a | value_b);
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_write_multiple
 *
 * PARAMETERS:  value               - The value to write
 *              register_a           - First ACPI register (required)
 *              register_b           - Second ACPI register (optional)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to the specified two-part ACPI register (such as PM1 A/B)
 *
 ******************************************************************************/

static acpi_status
acpi_hw_write_multiple(u32 value,
		       struct acpi_generic_address *register_a,
		       struct acpi_generic_address *register_b)
{
	acpi_status status;

	/* The first register is always required */

	status = acpi_hw_write(value, register_a);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/*
	 * Second register is optional
	 *
	 * No bit shifting or clearing is necessary, because of how the PM1
	 * registers are defined in the ACPI specification:
	 *
	 * "Although the bits can be split between the two register blocks (each
	 * register block has a unique pointer within the FADT), the bit positions
	 * are maintained. The register block with unimplemented bits (that is,
	 * those implemented in the other register block) always returns zeros,
	 * and writes have no side effects"
	 */
	if (register_b->address) {
		status = acpi_hw_write(value, register_b);
	}

	return (status);
}

#endif				/* !ACPI_REDUCED_HARDWARE */
