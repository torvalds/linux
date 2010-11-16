/*******************************************************************************

  Intel PRO/10GbE Linux driver
  Copyright(c) 1999 - 2008 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ixgb_hw.h"
#include "ixgb_ee.h"
/* Local prototypes */
static u16 ixgb_shift_in_bits(struct ixgb_hw *hw);

static void ixgb_shift_out_bits(struct ixgb_hw *hw,
				u16 data,
				u16 count);
static void ixgb_standby_eeprom(struct ixgb_hw *hw);

static bool ixgb_wait_eeprom_command(struct ixgb_hw *hw);

static void ixgb_cleanup_eeprom(struct ixgb_hw *hw);

/******************************************************************************
 * Raises the EEPROM's clock input.
 *
 * hw - Struct containing variables accessed by shared code
 * eecd_reg - EECD's current value
 *****************************************************************************/
static void
ixgb_raise_clock(struct ixgb_hw *hw,
		  u32 *eecd_reg)
{
	/* Raise the clock input to the EEPROM (by setting the SK bit), and then
	 *  wait 50 microseconds.
	 */
	*eecd_reg = *eecd_reg | IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, *eecd_reg);
	udelay(50);
}

/******************************************************************************
 * Lowers the EEPROM's clock input.
 *
 * hw - Struct containing variables accessed by shared code
 * eecd_reg - EECD's current value
 *****************************************************************************/
static void
ixgb_lower_clock(struct ixgb_hw *hw,
		  u32 *eecd_reg)
{
	/* Lower the clock input to the EEPROM (by clearing the SK bit), and then
	 * wait 50 microseconds.
	 */
	*eecd_reg = *eecd_reg & ~IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, *eecd_reg);
	udelay(50);
}

/******************************************************************************
 * Shift data bits out to the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * data - data to send to the EEPROM
 * count - number of bits to shift out
 *****************************************************************************/
static void
ixgb_shift_out_bits(struct ixgb_hw *hw,
					 u16 data,
					 u16 count)
{
	u32 eecd_reg;
	u32 mask;

	/* We need to shift "count" bits out to the EEPROM. So, value in the
	 * "data" parameter will be shifted out to the EEPROM one bit at a time.
	 * In order to do this, "data" must be broken down into bits.
	 */
	mask = 0x01 << (count - 1);
	eecd_reg = IXGB_READ_REG(hw, EECD);
	eecd_reg &= ~(IXGB_EECD_DO | IXGB_EECD_DI);
	do {
		/* A "1" is shifted out to the EEPROM by setting bit "DI" to a "1",
		 * and then raising and then lowering the clock (the SK bit controls
		 * the clock input to the EEPROM).  A "0" is shifted out to the EEPROM
		 * by setting "DI" to "0" and then raising and then lowering the clock.
		 */
		eecd_reg &= ~IXGB_EECD_DI;

		if (data & mask)
			eecd_reg |= IXGB_EECD_DI;

		IXGB_WRITE_REG(hw, EECD, eecd_reg);

		udelay(50);

		ixgb_raise_clock(hw, &eecd_reg);
		ixgb_lower_clock(hw, &eecd_reg);

		mask = mask >> 1;

	} while (mask);

	/* We leave the "DI" bit set to "0" when we leave this routine. */
	eecd_reg &= ~IXGB_EECD_DI;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
}

/******************************************************************************
 * Shift data bits in from the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static u16
ixgb_shift_in_bits(struct ixgb_hw *hw)
{
	u32 eecd_reg;
	u32 i;
	u16 data;

	/* In order to read a register from the EEPROM, we need to shift 16 bits
	 * in from the EEPROM. Bits are "shifted in" by raising the clock input to
	 * the EEPROM (setting the SK bit), and then reading the value of the "DO"
	 * bit.  During this "shifting in" process the "DI" bit should always be
	 * clear..
	 */

	eecd_reg = IXGB_READ_REG(hw, EECD);

	eecd_reg &= ~(IXGB_EECD_DO | IXGB_EECD_DI);
	data = 0;

	for (i = 0; i < 16; i++) {
		data = data << 1;
		ixgb_raise_clock(hw, &eecd_reg);

		eecd_reg = IXGB_READ_REG(hw, EECD);

		eecd_reg &= ~(IXGB_EECD_DI);
		if (eecd_reg & IXGB_EECD_DO)
			data |= 1;

		ixgb_lower_clock(hw, &eecd_reg);
	}

	return data;
}

/******************************************************************************
 * Prepares EEPROM for access
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Lowers EEPROM clock. Clears input pin. Sets the chip select pin. This
 * function should be called before issuing a command to the EEPROM.
 *****************************************************************************/
static void
ixgb_setup_eeprom(struct ixgb_hw *hw)
{
	u32 eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	/*  Clear SK and DI  */
	eecd_reg &= ~(IXGB_EECD_SK | IXGB_EECD_DI);
	IXGB_WRITE_REG(hw, EECD, eecd_reg);

	/*  Set CS  */
	eecd_reg |= IXGB_EECD_CS;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
}

/******************************************************************************
 * Returns EEPROM to a "standby" state
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
ixgb_standby_eeprom(struct ixgb_hw *hw)
{
	u32 eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	/*  Deselect EEPROM  */
	eecd_reg &= ~(IXGB_EECD_CS | IXGB_EECD_SK);
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);

	/*  Clock high  */
	eecd_reg |= IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);

	/*  Select EEPROM  */
	eecd_reg |= IXGB_EECD_CS;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);

	/*  Clock low  */
	eecd_reg &= ~IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);
}

/******************************************************************************
 * Raises then lowers the EEPROM's clock pin
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
ixgb_clock_eeprom(struct ixgb_hw *hw)
{
	u32 eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	/*  Rising edge of clock  */
	eecd_reg |= IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);

	/*  Falling edge of clock  */
	eecd_reg &= ~IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);
}

/******************************************************************************
 * Terminates a command by lowering the EEPROM's chip select pin
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
ixgb_cleanup_eeprom(struct ixgb_hw *hw)
{
	u32 eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	eecd_reg &= ~(IXGB_EECD_CS | IXGB_EECD_DI);

	IXGB_WRITE_REG(hw, EECD, eecd_reg);

	ixgb_clock_eeprom(hw);
}

/******************************************************************************
 * Waits for the EEPROM to finish the current command.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * The command is done when the EEPROM's data out pin goes high.
 *
 * Returns:
 *      true: EEPROM data pin is high before timeout.
 *      false:  Time expired.
 *****************************************************************************/
static bool
ixgb_wait_eeprom_command(struct ixgb_hw *hw)
{
	u32 eecd_reg;
	u32 i;

	/* Toggle the CS line.  This in effect tells to EEPROM to actually execute
	 * the command in question.
	 */
	ixgb_standby_eeprom(hw);

	/* Now read DO repeatedly until is high (equal to '1').  The EEPROM will
	 * signal that the command has been completed by raising the DO signal.
	 * If DO does not go high in 10 milliseconds, then error out.
	 */
	for (i = 0; i < 200; i++) {
		eecd_reg = IXGB_READ_REG(hw, EECD);

		if (eecd_reg & IXGB_EECD_DO)
			return true;

		udelay(50);
	}
	ASSERT(0);
	return false;
}

/******************************************************************************
 * Verifies that the EEPROM has a valid checksum
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Reads the first 64 16 bit words of the EEPROM and sums the values read.
 * If the sum of the 64 16 bit words is 0xBABA, the EEPROM's checksum is
 * valid.
 *
 * Returns:
 *  true: Checksum is valid
 *  false: Checksum is not valid.
 *****************************************************************************/
bool
ixgb_validate_eeprom_checksum(struct ixgb_hw *hw)
{
	u16 checksum = 0;
	u16 i;

	for (i = 0; i < (EEPROM_CHECKSUM_REG + 1); i++)
		checksum += ixgb_read_eeprom(hw, i);

	if (checksum == (u16) EEPROM_SUM)
		return true;
	else
		return false;
}

/******************************************************************************
 * Calculates the EEPROM checksum and writes it to the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Sums the first 63 16 bit words of the EEPROM. Subtracts the sum from 0xBABA.
 * Writes the difference to word offset 63 of the EEPROM.
 *****************************************************************************/
void
ixgb_update_eeprom_checksum(struct ixgb_hw *hw)
{
	u16 checksum = 0;
	u16 i;

	for (i = 0; i < EEPROM_CHECKSUM_REG; i++)
		checksum += ixgb_read_eeprom(hw, i);

	checksum = (u16) EEPROM_SUM - checksum;

	ixgb_write_eeprom(hw, EEPROM_CHECKSUM_REG, checksum);
}

/******************************************************************************
 * Writes a 16 bit word to a given offset in the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * reg - offset within the EEPROM to be written to
 * data - 16 bit word to be written to the EEPROM
 *
 * If ixgb_update_eeprom_checksum is not called after this function, the
 * EEPROM will most likely contain an invalid checksum.
 *
 *****************************************************************************/
void
ixgb_write_eeprom(struct ixgb_hw *hw, u16 offset, u16 data)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	/* Prepare the EEPROM for writing */
	ixgb_setup_eeprom(hw);

	/*  Send the 9-bit EWEN (write enable) command to the EEPROM (5-bit opcode
	 *  plus 4-bit dummy).  This puts the EEPROM into write/erase mode.
	 */
	ixgb_shift_out_bits(hw, EEPROM_EWEN_OPCODE, 5);
	ixgb_shift_out_bits(hw, 0, 4);

	/*  Prepare the EEPROM  */
	ixgb_standby_eeprom(hw);

	/*  Send the Write command (3-bit opcode + 6-bit addr)  */
	ixgb_shift_out_bits(hw, EEPROM_WRITE_OPCODE, 3);
	ixgb_shift_out_bits(hw, offset, 6);

	/*  Send the data  */
	ixgb_shift_out_bits(hw, data, 16);

	ixgb_wait_eeprom_command(hw);

	/*  Recover from write  */
	ixgb_standby_eeprom(hw);

	/* Send the 9-bit EWDS (write disable) command to the EEPROM (5-bit
	 * opcode plus 4-bit dummy).  This takes the EEPROM out of write/erase
	 * mode.
	 */
	ixgb_shift_out_bits(hw, EEPROM_EWDS_OPCODE, 5);
	ixgb_shift_out_bits(hw, 0, 4);

	/*  Done with writing  */
	ixgb_cleanup_eeprom(hw);

	/* clear the init_ctrl_reg_1 to signify that the cache is invalidated */
	ee_map->init_ctrl_reg_1 = cpu_to_le16(EEPROM_ICW1_SIGNATURE_CLEAR);
}

/******************************************************************************
 * Reads a 16 bit word from the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of 16 bit word in the EEPROM to read
 *
 * Returns:
 *  The 16-bit value read from the eeprom
 *****************************************************************************/
u16
ixgb_read_eeprom(struct ixgb_hw *hw,
		  u16 offset)
{
	u16 data;

	/*  Prepare the EEPROM for reading  */
	ixgb_setup_eeprom(hw);

	/*  Send the READ command (opcode + addr)  */
	ixgb_shift_out_bits(hw, EEPROM_READ_OPCODE, 3);
	/*
	 * We have a 64 word EEPROM, there are 6 address bits
	 */
	ixgb_shift_out_bits(hw, offset, 6);

	/*  Read the data  */
	data = ixgb_shift_in_bits(hw);

	/*  End this read operation  */
	ixgb_standby_eeprom(hw);

	return data;
}

/******************************************************************************
 * Reads eeprom and stores data in shared structure.
 * Validates eeprom checksum and eeprom signature.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *      true: if eeprom read is successful
 *      false: otherwise.
 *****************************************************************************/
bool
ixgb_get_eeprom_data(struct ixgb_hw *hw)
{
	u16 i;
	u16 checksum = 0;
	struct ixgb_ee_map_type *ee_map;

	ENTER();

	ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	pr_debug("Reading eeprom data\n");
	for (i = 0; i < IXGB_EEPROM_SIZE ; i++) {
		u16 ee_data;
		ee_data = ixgb_read_eeprom(hw, i);
		checksum += ee_data;
		hw->eeprom[i] = cpu_to_le16(ee_data);
	}

	if (checksum != (u16) EEPROM_SUM) {
		pr_debug("Checksum invalid\n");
		/* clear the init_ctrl_reg_1 to signify that the cache is
		 * invalidated */
		ee_map->init_ctrl_reg_1 = cpu_to_le16(EEPROM_ICW1_SIGNATURE_CLEAR);
		return false;
	}

	if ((ee_map->init_ctrl_reg_1 & cpu_to_le16(EEPROM_ICW1_SIGNATURE_MASK))
		 != cpu_to_le16(EEPROM_ICW1_SIGNATURE_VALID)) {
		pr_debug("Signature invalid\n");
		return false;
	}

	return true;
}

/******************************************************************************
 * Local function to check if the eeprom signature is good
 * If the eeprom signature is good, calls ixgb)get_eeprom_data.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *      true: eeprom signature was good and the eeprom read was successful
 *      false: otherwise.
 ******************************************************************************/
static bool
ixgb_check_and_get_eeprom_data (struct ixgb_hw* hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if ((ee_map->init_ctrl_reg_1 & cpu_to_le16(EEPROM_ICW1_SIGNATURE_MASK))
	    == cpu_to_le16(EEPROM_ICW1_SIGNATURE_VALID)) {
		return true;
	} else {
		return ixgb_get_eeprom_data(hw);
	}
}

/******************************************************************************
 * return a word from the eeprom
 *
 * hw - Struct containing variables accessed by shared code
 * index - Offset of eeprom word
 *
 * Returns:
 *          Word at indexed offset in eeprom, if valid, 0 otherwise.
 ******************************************************************************/
__le16
ixgb_get_eeprom_word(struct ixgb_hw *hw, u16 index)
{

	if ((index < IXGB_EEPROM_SIZE) &&
		(ixgb_check_and_get_eeprom_data(hw) == true)) {
	   return hw->eeprom[index];
	}

	return 0;
}

/******************************************************************************
 * return the mac address from EEPROM
 *
 * hw       - Struct containing variables accessed by shared code
 * mac_addr - Ethernet Address if EEPROM contents are valid, 0 otherwise
 *
 * Returns: None.
 ******************************************************************************/
void
ixgb_get_ee_mac_addr(struct ixgb_hw *hw,
			u8 *mac_addr)
{
	int i;
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	ENTER();

	if (ixgb_check_and_get_eeprom_data(hw) == true) {
		for (i = 0; i < IXGB_ETH_LENGTH_OF_ADDRESS; i++) {
			mac_addr[i] = ee_map->mac_addr[i];
		}
		pr_debug("eeprom mac address = %pM\n", mac_addr);
	}
}


/******************************************************************************
 * return the Printed Board Assembly number from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          PBA number if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
u32
ixgb_get_ee_pba_number(struct ixgb_hw *hw)
{
	if (ixgb_check_and_get_eeprom_data(hw) == true)
		return le16_to_cpu(hw->eeprom[EEPROM_PBA_1_2_REG])
			| (le16_to_cpu(hw->eeprom[EEPROM_PBA_3_4_REG])<<16);

	return 0;
}


/******************************************************************************
 * return the Device Id from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          Device Id if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
u16
ixgb_get_ee_device_id(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if (ixgb_check_and_get_eeprom_data(hw) == true)
		return le16_to_cpu(ee_map->device_id);

	return 0;
}

