/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2012 Intel Corporation.

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
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include <linux/if_ether.h>
#include <linux/delay.h>

#include "e1000_mac.h"
#include "e1000_nvm.h"

/**
 *  igb_raise_eec_clk - Raise EEPROM clock
 *  @hw: pointer to the HW structure
 *  @eecd: pointer to the EEPROM
 *
 *  Enable/Raise the EEPROM clock bit.
 **/
static void igb_raise_eec_clk(struct e1000_hw *hw, u32 *eecd)
{
	*eecd = *eecd | E1000_EECD_SK;
	wr32(E1000_EECD, *eecd);
	wrfl();
	udelay(hw->nvm.delay_usec);
}

/**
 *  igb_lower_eec_clk - Lower EEPROM clock
 *  @hw: pointer to the HW structure
 *  @eecd: pointer to the EEPROM
 *
 *  Clear/Lower the EEPROM clock bit.
 **/
static void igb_lower_eec_clk(struct e1000_hw *hw, u32 *eecd)
{
	*eecd = *eecd & ~E1000_EECD_SK;
	wr32(E1000_EECD, *eecd);
	wrfl();
	udelay(hw->nvm.delay_usec);
}

/**
 *  igb_shift_out_eec_bits - Shift data bits our to the EEPROM
 *  @hw: pointer to the HW structure
 *  @data: data to send to the EEPROM
 *  @count: number of bits to shift out
 *
 *  We need to shift 'count' bits out to the EEPROM.  So, the value in the
 *  "data" parameter will be shifted out to the EEPROM one bit at a time.
 *  In order to do this, "data" must be broken down into bits.
 **/
static void igb_shift_out_eec_bits(struct e1000_hw *hw, u16 data, u16 count)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 eecd = rd32(E1000_EECD);
	u32 mask;

	mask = 0x01 << (count - 1);
	if (nvm->type == e1000_nvm_eeprom_spi)
		eecd |= E1000_EECD_DO;

	do {
		eecd &= ~E1000_EECD_DI;

		if (data & mask)
			eecd |= E1000_EECD_DI;

		wr32(E1000_EECD, eecd);
		wrfl();

		udelay(nvm->delay_usec);

		igb_raise_eec_clk(hw, &eecd);
		igb_lower_eec_clk(hw, &eecd);

		mask >>= 1;
	} while (mask);

	eecd &= ~E1000_EECD_DI;
	wr32(E1000_EECD, eecd);
}

/**
 *  igb_shift_in_eec_bits - Shift data bits in from the EEPROM
 *  @hw: pointer to the HW structure
 *  @count: number of bits to shift in
 *
 *  In order to read a register from the EEPROM, we need to shift 'count' bits
 *  in from the EEPROM.  Bits are "shifted in" by raising the clock input to
 *  the EEPROM (setting the SK bit), and then reading the value of the data out
 *  "DO" bit.  During this "shifting in" process the data in "DI" bit should
 *  always be clear.
 **/
static u16 igb_shift_in_eec_bits(struct e1000_hw *hw, u16 count)
{
	u32 eecd;
	u32 i;
	u16 data;

	eecd = rd32(E1000_EECD);

	eecd &= ~(E1000_EECD_DO | E1000_EECD_DI);
	data = 0;

	for (i = 0; i < count; i++) {
		data <<= 1;
		igb_raise_eec_clk(hw, &eecd);

		eecd = rd32(E1000_EECD);

		eecd &= ~E1000_EECD_DI;
		if (eecd & E1000_EECD_DO)
			data |= 1;

		igb_lower_eec_clk(hw, &eecd);
	}

	return data;
}

/**
 *  igb_poll_eerd_eewr_done - Poll for EEPROM read/write completion
 *  @hw: pointer to the HW structure
 *  @ee_reg: EEPROM flag for polling
 *
 *  Polls the EEPROM status bit for either read or write completion based
 *  upon the value of 'ee_reg'.
 **/
static s32 igb_poll_eerd_eewr_done(struct e1000_hw *hw, int ee_reg)
{
	u32 attempts = 100000;
	u32 i, reg = 0;
	s32 ret_val = -E1000_ERR_NVM;

	for (i = 0; i < attempts; i++) {
		if (ee_reg == E1000_NVM_POLL_READ)
			reg = rd32(E1000_EERD);
		else
			reg = rd32(E1000_EEWR);

		if (reg & E1000_NVM_RW_REG_DONE) {
			ret_val = 0;
			break;
		}

		udelay(5);
	}

	return ret_val;
}

/**
 *  igb_acquire_nvm - Generic request for access to EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Set the EEPROM access request bit and wait for EEPROM access grant bit.
 *  Return successful if access grant bit set, else clear the request for
 *  EEPROM access and return -E1000_ERR_NVM (-1).
 **/
s32 igb_acquire_nvm(struct e1000_hw *hw)
{
	u32 eecd = rd32(E1000_EECD);
	s32 timeout = E1000_NVM_GRANT_ATTEMPTS;
	s32 ret_val = 0;


	wr32(E1000_EECD, eecd | E1000_EECD_REQ);
	eecd = rd32(E1000_EECD);

	while (timeout) {
		if (eecd & E1000_EECD_GNT)
			break;
		udelay(5);
		eecd = rd32(E1000_EECD);
		timeout--;
	}

	if (!timeout) {
		eecd &= ~E1000_EECD_REQ;
		wr32(E1000_EECD, eecd);
		hw_dbg("Could not acquire NVM grant\n");
		ret_val = -E1000_ERR_NVM;
	}

	return ret_val;
}

/**
 *  igb_standby_nvm - Return EEPROM to standby state
 *  @hw: pointer to the HW structure
 *
 *  Return the EEPROM to a standby state.
 **/
static void igb_standby_nvm(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 eecd = rd32(E1000_EECD);

	if (nvm->type == e1000_nvm_eeprom_spi) {
		/* Toggle CS to flush commands */
		eecd |= E1000_EECD_CS;
		wr32(E1000_EECD, eecd);
		wrfl();
		udelay(nvm->delay_usec);
		eecd &= ~E1000_EECD_CS;
		wr32(E1000_EECD, eecd);
		wrfl();
		udelay(nvm->delay_usec);
	}
}

/**
 *  e1000_stop_nvm - Terminate EEPROM command
 *  @hw: pointer to the HW structure
 *
 *  Terminates the current command by inverting the EEPROM's chip select pin.
 **/
static void e1000_stop_nvm(struct e1000_hw *hw)
{
	u32 eecd;

	eecd = rd32(E1000_EECD);
	if (hw->nvm.type == e1000_nvm_eeprom_spi) {
		/* Pull CS high */
		eecd |= E1000_EECD_CS;
		igb_lower_eec_clk(hw, &eecd);
	}
}

/**
 *  igb_release_nvm - Release exclusive access to EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Stop any current commands to the EEPROM and clear the EEPROM request bit.
 **/
void igb_release_nvm(struct e1000_hw *hw)
{
	u32 eecd;

	e1000_stop_nvm(hw);

	eecd = rd32(E1000_EECD);
	eecd &= ~E1000_EECD_REQ;
	wr32(E1000_EECD, eecd);
}

/**
 *  igb_ready_nvm_eeprom - Prepares EEPROM for read/write
 *  @hw: pointer to the HW structure
 *
 *  Setups the EEPROM for reading and writing.
 **/
static s32 igb_ready_nvm_eeprom(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 eecd = rd32(E1000_EECD);
	s32 ret_val = 0;
	u16 timeout = 0;
	u8 spi_stat_reg;


	if (nvm->type == e1000_nvm_eeprom_spi) {
		/* Clear SK and CS */
		eecd &= ~(E1000_EECD_CS | E1000_EECD_SK);
		wr32(E1000_EECD, eecd);
		wrfl();
		udelay(1);
		timeout = NVM_MAX_RETRY_SPI;

		/*
		 * Read "Status Register" repeatedly until the LSB is cleared.
		 * The EEPROM will signal that the command has been completed
		 * by clearing bit 0 of the internal status register.  If it's
		 * not cleared within 'timeout', then error out.
		 */
		while (timeout) {
			igb_shift_out_eec_bits(hw, NVM_RDSR_OPCODE_SPI,
						 hw->nvm.opcode_bits);
			spi_stat_reg = (u8)igb_shift_in_eec_bits(hw, 8);
			if (!(spi_stat_reg & NVM_STATUS_RDY_SPI))
				break;

			udelay(5);
			igb_standby_nvm(hw);
			timeout--;
		}

		if (!timeout) {
			hw_dbg("SPI NVM Status error\n");
			ret_val = -E1000_ERR_NVM;
			goto out;
		}
	}

out:
	return ret_val;
}

/**
 *  igb_read_nvm_spi - Read EEPROM's using SPI
 *  @hw: pointer to the HW structure
 *  @offset: offset of word in the EEPROM to read
 *  @words: number of words to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM.
 **/
s32 igb_read_nvm_spi(struct e1000_hw *hw, u16 offset, u16 words, u16 *data)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 i = 0;
	s32 ret_val;
	u16 word_in;
	u8 read_opcode = NVM_READ_OPCODE_SPI;

	/*
	 * A check for invalid values:  offset too large, too many words,
	 * and not enough words.
	 */
	if ((offset >= nvm->word_size) || (words > (nvm->word_size - offset)) ||
	    (words == 0)) {
		hw_dbg("nvm parameter(s) out of bounds\n");
		ret_val = -E1000_ERR_NVM;
		goto out;
	}

	ret_val = nvm->ops.acquire(hw);
	if (ret_val)
		goto out;

	ret_val = igb_ready_nvm_eeprom(hw);
	if (ret_val)
		goto release;

	igb_standby_nvm(hw);

	if ((nvm->address_bits == 8) && (offset >= 128))
		read_opcode |= NVM_A8_OPCODE_SPI;

	/* Send the READ command (opcode + addr) */
	igb_shift_out_eec_bits(hw, read_opcode, nvm->opcode_bits);
	igb_shift_out_eec_bits(hw, (u16)(offset*2), nvm->address_bits);

	/*
	 * Read the data.  SPI NVMs increment the address with each byte
	 * read and will roll over if reading beyond the end.  This allows
	 * us to read the whole NVM from any offset
	 */
	for (i = 0; i < words; i++) {
		word_in = igb_shift_in_eec_bits(hw, 16);
		data[i] = (word_in >> 8) | (word_in << 8);
	}

release:
	nvm->ops.release(hw);

out:
	return ret_val;
}

/**
 *  igb_read_nvm_eerd - Reads EEPROM using EERD register
 *  @hw: pointer to the HW structure
 *  @offset: offset of word in the EEPROM to read
 *  @words: number of words to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM using the EERD register.
 **/
s32 igb_read_nvm_eerd(struct e1000_hw *hw, u16 offset, u16 words, u16 *data)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 i, eerd = 0;
	s32 ret_val = 0;

	/*
	 * A check for invalid values:  offset too large, too many words,
	 * and not enough words.
	 */
	if ((offset >= nvm->word_size) || (words > (nvm->word_size - offset)) ||
	    (words == 0)) {
		hw_dbg("nvm parameter(s) out of bounds\n");
		ret_val = -E1000_ERR_NVM;
		goto out;
	}

	for (i = 0; i < words; i++) {
		eerd = ((offset+i) << E1000_NVM_RW_ADDR_SHIFT) +
		       E1000_NVM_RW_REG_START;

		wr32(E1000_EERD, eerd);
		ret_val = igb_poll_eerd_eewr_done(hw, E1000_NVM_POLL_READ);
		if (ret_val)
			break;

		data[i] = (rd32(E1000_EERD) >>
			E1000_NVM_RW_REG_DATA);
	}

out:
	return ret_val;
}

/**
 *  igb_write_nvm_spi - Write to EEPROM using SPI
 *  @hw: pointer to the HW structure
 *  @offset: offset within the EEPROM to be written to
 *  @words: number of words to write
 *  @data: 16 bit word(s) to be written to the EEPROM
 *
 *  Writes data to EEPROM at offset using SPI interface.
 *
 *  If e1000_update_nvm_checksum is not called after this function , the
 *  EEPROM will most likley contain an invalid checksum.
 **/
s32 igb_write_nvm_spi(struct e1000_hw *hw, u16 offset, u16 words, u16 *data)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	s32 ret_val;
	u16 widx = 0;

	/*
	 * A check for invalid values:  offset too large, too many words,
	 * and not enough words.
	 */
	if ((offset >= nvm->word_size) || (words > (nvm->word_size - offset)) ||
	    (words == 0)) {
		hw_dbg("nvm parameter(s) out of bounds\n");
		ret_val = -E1000_ERR_NVM;
		goto out;
	}

	ret_val = hw->nvm.ops.acquire(hw);
	if (ret_val)
		goto out;

	msleep(10);

	while (widx < words) {
		u8 write_opcode = NVM_WRITE_OPCODE_SPI;

		ret_val = igb_ready_nvm_eeprom(hw);
		if (ret_val)
			goto release;

		igb_standby_nvm(hw);

		/* Send the WRITE ENABLE command (8 bit opcode) */
		igb_shift_out_eec_bits(hw, NVM_WREN_OPCODE_SPI,
					 nvm->opcode_bits);

		igb_standby_nvm(hw);

		/*
		 * Some SPI eeproms use the 8th address bit embedded in the
		 * opcode
		 */
		if ((nvm->address_bits == 8) && (offset >= 128))
			write_opcode |= NVM_A8_OPCODE_SPI;

		/* Send the Write command (8-bit opcode + addr) */
		igb_shift_out_eec_bits(hw, write_opcode, nvm->opcode_bits);
		igb_shift_out_eec_bits(hw, (u16)((offset + widx) * 2),
					 nvm->address_bits);

		/* Loop to allow for up to whole page write of eeprom */
		while (widx < words) {
			u16 word_out = data[widx];
			word_out = (word_out >> 8) | (word_out << 8);
			igb_shift_out_eec_bits(hw, word_out, 16);
			widx++;

			if ((((offset + widx) * 2) % nvm->page_size) == 0) {
				igb_standby_nvm(hw);
				break;
			}
		}
	}

	msleep(10);
release:
	hw->nvm.ops.release(hw);

out:
	return ret_val;
}

/**
 *  igb_read_part_string - Read device part number
 *  @hw: pointer to the HW structure
 *  @part_num: pointer to device part number
 *  @part_num_size: size of part number buffer
 *
 *  Reads the product board assembly (PBA) number from the EEPROM and stores
 *  the value in part_num.
 **/
s32 igb_read_part_string(struct e1000_hw *hw, u8 *part_num, u32 part_num_size)
{
	s32 ret_val;
	u16 nvm_data;
	u16 pointer;
	u16 offset;
	u16 length;

	if (part_num == NULL) {
		hw_dbg("PBA string buffer was null\n");
		ret_val = E1000_ERR_INVALID_ARGUMENT;
		goto out;
	}

	ret_val = hw->nvm.ops.read(hw, NVM_PBA_OFFSET_0, 1, &nvm_data);
	if (ret_val) {
		hw_dbg("NVM Read Error\n");
		goto out;
	}

	ret_val = hw->nvm.ops.read(hw, NVM_PBA_OFFSET_1, 1, &pointer);
	if (ret_val) {
		hw_dbg("NVM Read Error\n");
		goto out;
	}

	/*
	 * if nvm_data is not ptr guard the PBA must be in legacy format which
	 * means pointer is actually our second data word for the PBA number
	 * and we can decode it into an ascii string
	 */
	if (nvm_data != NVM_PBA_PTR_GUARD) {
		hw_dbg("NVM PBA number is not stored as string\n");

		/* we will need 11 characters to store the PBA */
		if (part_num_size < 11) {
			hw_dbg("PBA string buffer too small\n");
			return E1000_ERR_NO_SPACE;
		}

		/* extract hex string from data and pointer */
		part_num[0] = (nvm_data >> 12) & 0xF;
		part_num[1] = (nvm_data >> 8) & 0xF;
		part_num[2] = (nvm_data >> 4) & 0xF;
		part_num[3] = nvm_data & 0xF;
		part_num[4] = (pointer >> 12) & 0xF;
		part_num[5] = (pointer >> 8) & 0xF;
		part_num[6] = '-';
		part_num[7] = 0;
		part_num[8] = (pointer >> 4) & 0xF;
		part_num[9] = pointer & 0xF;

		/* put a null character on the end of our string */
		part_num[10] = '\0';

		/* switch all the data but the '-' to hex char */
		for (offset = 0; offset < 10; offset++) {
			if (part_num[offset] < 0xA)
				part_num[offset] += '0';
			else if (part_num[offset] < 0x10)
				part_num[offset] += 'A' - 0xA;
		}

		goto out;
	}

	ret_val = hw->nvm.ops.read(hw, pointer, 1, &length);
	if (ret_val) {
		hw_dbg("NVM Read Error\n");
		goto out;
	}

	if (length == 0xFFFF || length == 0) {
		hw_dbg("NVM PBA number section invalid length\n");
		ret_val = E1000_ERR_NVM_PBA_SECTION;
		goto out;
	}
	/* check if part_num buffer is big enough */
	if (part_num_size < (((u32)length * 2) - 1)) {
		hw_dbg("PBA string buffer too small\n");
		ret_val = E1000_ERR_NO_SPACE;
		goto out;
	}

	/* trim pba length from start of string */
	pointer++;
	length--;

	for (offset = 0; offset < length; offset++) {
		ret_val = hw->nvm.ops.read(hw, pointer + offset, 1, &nvm_data);
		if (ret_val) {
			hw_dbg("NVM Read Error\n");
			goto out;
		}
		part_num[offset * 2] = (u8)(nvm_data >> 8);
		part_num[(offset * 2) + 1] = (u8)(nvm_data & 0xFF);
	}
	part_num[offset * 2] = '\0';

out:
	return ret_val;
}

/**
 *  igb_read_mac_addr - Read device MAC address
 *  @hw: pointer to the HW structure
 *
 *  Reads the device MAC address from the EEPROM and stores the value.
 *  Since devices with two ports use the same EEPROM, we increment the
 *  last bit in the MAC address for the second port.
 **/
s32 igb_read_mac_addr(struct e1000_hw *hw)
{
	u32 rar_high;
	u32 rar_low;
	u16 i;

	rar_high = rd32(E1000_RAH(0));
	rar_low = rd32(E1000_RAL(0));

	for (i = 0; i < E1000_RAL_MAC_ADDR_LEN; i++)
		hw->mac.perm_addr[i] = (u8)(rar_low >> (i*8));

	for (i = 0; i < E1000_RAH_MAC_ADDR_LEN; i++)
		hw->mac.perm_addr[i+4] = (u8)(rar_high >> (i*8));

	for (i = 0; i < ETH_ALEN; i++)
		hw->mac.addr[i] = hw->mac.perm_addr[i];

	return 0;
}

/**
 *  igb_validate_nvm_checksum - Validate EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Calculates the EEPROM checksum by reading/adding each word of the EEPROM
 *  and then verifies that the sum of the EEPROM is equal to 0xBABA.
 **/
s32 igb_validate_nvm_checksum(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	u16 checksum = 0;
	u16 i, nvm_data;

	for (i = 0; i < (NVM_CHECKSUM_REG + 1); i++) {
		ret_val = hw->nvm.ops.read(hw, i, 1, &nvm_data);
		if (ret_val) {
			hw_dbg("NVM Read Error\n");
			goto out;
		}
		checksum += nvm_data;
	}

	if (checksum != (u16) NVM_SUM) {
		hw_dbg("NVM Checksum Invalid\n");
		ret_val = -E1000_ERR_NVM;
		goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_update_nvm_checksum - Update EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Updates the EEPROM checksum by reading/adding each word of the EEPROM
 *  up to the checksum.  Then calculates the EEPROM checksum and writes the
 *  value to the EEPROM.
 **/
s32 igb_update_nvm_checksum(struct e1000_hw *hw)
{
	s32  ret_val;
	u16 checksum = 0;
	u16 i, nvm_data;

	for (i = 0; i < NVM_CHECKSUM_REG; i++) {
		ret_val = hw->nvm.ops.read(hw, i, 1, &nvm_data);
		if (ret_val) {
			hw_dbg("NVM Read Error while updating checksum.\n");
			goto out;
		}
		checksum += nvm_data;
	}
	checksum = (u16) NVM_SUM - checksum;
	ret_val = hw->nvm.ops.write(hw, NVM_CHECKSUM_REG, 1, &checksum);
	if (ret_val)
		hw_dbg("NVM Write Error while updating checksum.\n");

out:
	return ret_val;
}

/**
 *  igb_get_fw_version - Get firmware version information
 *  @hw: pointer to the HW structure
 *  @fw_vers: pointer to output structure
 *
 *  unsupported MAC types will return all 0 version structure
 **/
void igb_get_fw_version(struct e1000_hw *hw, struct e1000_fw_version *fw_vers)
{
	u16 eeprom_verh, eeprom_verl, comb_verh, comb_verl, comb_offset;
	u16 fw_version;

	memset(fw_vers, 0, sizeof(struct e1000_fw_version));

	switch (hw->mac.type) {
	case e1000_i211:
		return;
	case e1000_82575:
	case e1000_82576:
	case e1000_82580:
	case e1000_i350:
	case e1000_i210:
		break;
	default:
		return;
	}
	/* basic eeprom version numbers */
	hw->nvm.ops.read(hw, NVM_VERSION, 1, &fw_version);
	fw_vers->eep_major = (fw_version & NVM_MAJOR_MASK) >> NVM_MAJOR_SHIFT;
	fw_vers->eep_minor = (fw_version & NVM_MINOR_MASK);

	/* etrack id */
	hw->nvm.ops.read(hw, NVM_ETRACK_WORD, 1, &eeprom_verl);
	hw->nvm.ops.read(hw, (NVM_ETRACK_WORD + 1), 1, &eeprom_verh);
	fw_vers->etrack_id = (eeprom_verh << NVM_ETRACK_SHIFT) | eeprom_verl;

	switch (hw->mac.type) {
	case e1000_i210:
	case e1000_i350:
		/* find combo image version */
		hw->nvm.ops.read(hw, NVM_COMB_VER_PTR, 1, &comb_offset);
		if ((comb_offset != 0x0) && (comb_offset != NVM_VER_INVALID)) {

			hw->nvm.ops.read(hw, (NVM_COMB_VER_OFF + comb_offset
					 + 1), 1, &comb_verh);
			hw->nvm.ops.read(hw, (NVM_COMB_VER_OFF + comb_offset),
					 1, &comb_verl);

			/* get Option Rom version if it exists and is valid */
			if ((comb_verh && comb_verl) &&
			    ((comb_verh != NVM_VER_INVALID) &&
			     (comb_verl != NVM_VER_INVALID))) {

				fw_vers->or_valid = true;
				fw_vers->or_major =
					comb_verl >> NVM_COMB_VER_SHFT;
				fw_vers->or_build =
					((comb_verl << NVM_COMB_VER_SHFT)
					| (comb_verh >> NVM_COMB_VER_SHFT));
				fw_vers->or_patch =
					comb_verh & NVM_COMB_VER_MASK;
			}
		}
		break;
	default:
		break;
	}
	return;
}
