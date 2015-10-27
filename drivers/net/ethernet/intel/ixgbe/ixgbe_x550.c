/*******************************************************************************
 *
 *  Intel 10 Gigabit PCI Express Linux driver
 *  Copyright(c) 1999 - 2015 Intel Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 *  Contact Information:
 *  Linux NICS <linux.nics@intel.com>
 *  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 *  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/
#include "ixgbe_x540.h"
#include "ixgbe_type.h"
#include "ixgbe_common.h"
#include "ixgbe_phy.h"

static s32 ixgbe_setup_kr_speed_x550em(struct ixgbe_hw *, ixgbe_link_speed);

static s32 ixgbe_get_invariants_X550_x(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;

	/* Start with X540 invariants, since so simular */
	ixgbe_get_invariants_X540(hw);

	if (mac->ops.get_media_type(hw) != ixgbe_media_type_copper)
		phy->ops.set_phy_power = NULL;

	return 0;
}

/** ixgbe_setup_mux_ctl - Setup ESDP register for I2C mux control
 *  @hw: pointer to hardware structure
 **/
static void ixgbe_setup_mux_ctl(struct ixgbe_hw *hw)
{
	u32 esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);

	if (hw->bus.lan_id) {
		esdp &= ~(IXGBE_ESDP_SDP1_NATIVE | IXGBE_ESDP_SDP1);
		esdp |= IXGBE_ESDP_SDP1_DIR;
	}
	esdp &= ~(IXGBE_ESDP_SDP0_NATIVE | IXGBE_ESDP_SDP0_DIR);
	IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 * ixgbe_read_cs4227 - Read CS4227 register
 * @hw: pointer to hardware structure
 * @reg: register number to write
 * @value: pointer to receive value read
 *
 * Returns status code
 */
static s32 ixgbe_read_cs4227(struct ixgbe_hw *hw, u16 reg, u16 *value)
{
	return hw->phy.ops.read_i2c_combined_unlocked(hw, IXGBE_CS4227, reg,
						      value);
}

/**
 * ixgbe_write_cs4227 - Write CS4227 register
 * @hw: pointer to hardware structure
 * @reg: register number to write
 * @value: value to write to register
 *
 * Returns status code
 */
static s32 ixgbe_write_cs4227(struct ixgbe_hw *hw, u16 reg, u16 value)
{
	return hw->phy.ops.write_i2c_combined_unlocked(hw, IXGBE_CS4227, reg,
						       value);
}

/**
 * ixgbe_read_pe - Read register from port expander
 * @hw: pointer to hardware structure
 * @reg: register number to read
 * @value: pointer to receive read value
 *
 * Returns status code
 */
static s32 ixgbe_read_pe(struct ixgbe_hw *hw, u8 reg, u8 *value)
{
	s32 status;

	status = ixgbe_read_i2c_byte_generic_unlocked(hw, reg, IXGBE_PE, value);
	if (status)
		hw_err(hw, "port expander access failed with %d\n", status);
	return status;
}

/**
 * ixgbe_write_pe - Write register to port expander
 * @hw: pointer to hardware structure
 * @reg: register number to write
 * @value: value to write
 *
 * Returns status code
 */
static s32 ixgbe_write_pe(struct ixgbe_hw *hw, u8 reg, u8 value)
{
	s32 status;

	status = ixgbe_write_i2c_byte_generic_unlocked(hw, reg, IXGBE_PE,
						       value);
	if (status)
		hw_err(hw, "port expander access failed with %d\n", status);
	return status;
}

/**
 * ixgbe_reset_cs4227 - Reset CS4227 using port expander
 * @hw: pointer to hardware structure
 *
 * This function assumes that the caller has acquired the proper semaphore.
 * Returns error code
 */
static s32 ixgbe_reset_cs4227(struct ixgbe_hw *hw)
{
	s32 status;
	u32 retry;
	u16 value;
	u8 reg;

	/* Trigger hard reset. */
	status = ixgbe_read_pe(hw, IXGBE_PE_OUTPUT, &reg);
	if (status)
		return status;
	reg |= IXGBE_PE_BIT1;
	status = ixgbe_write_pe(hw, IXGBE_PE_OUTPUT, reg);
	if (status)
		return status;

	status = ixgbe_read_pe(hw, IXGBE_PE_CONFIG, &reg);
	if (status)
		return status;
	reg &= ~IXGBE_PE_BIT1;
	status = ixgbe_write_pe(hw, IXGBE_PE_CONFIG, reg);
	if (status)
		return status;

	status = ixgbe_read_pe(hw, IXGBE_PE_OUTPUT, &reg);
	if (status)
		return status;
	reg &= ~IXGBE_PE_BIT1;
	status = ixgbe_write_pe(hw, IXGBE_PE_OUTPUT, reg);
	if (status)
		return status;

	usleep_range(IXGBE_CS4227_RESET_HOLD, IXGBE_CS4227_RESET_HOLD + 100);

	status = ixgbe_read_pe(hw, IXGBE_PE_OUTPUT, &reg);
	if (status)
		return status;
	reg |= IXGBE_PE_BIT1;
	status = ixgbe_write_pe(hw, IXGBE_PE_OUTPUT, reg);
	if (status)
		return status;

	/* Wait for the reset to complete. */
	msleep(IXGBE_CS4227_RESET_DELAY);
	for (retry = 0; retry < IXGBE_CS4227_RETRIES; retry++) {
		status = ixgbe_read_cs4227(hw, IXGBE_CS4227_EFUSE_STATUS,
					   &value);
		if (!status && value == IXGBE_CS4227_EEPROM_LOAD_OK)
			break;
		msleep(IXGBE_CS4227_CHECK_DELAY);
	}
	if (retry == IXGBE_CS4227_RETRIES) {
		hw_err(hw, "CS4227 reset did not complete\n");
		return IXGBE_ERR_PHY;
	}

	status = ixgbe_read_cs4227(hw, IXGBE_CS4227_EEPROM_STATUS, &value);
	if (status || !(value & IXGBE_CS4227_EEPROM_LOAD_OK)) {
		hw_err(hw, "CS4227 EEPROM did not load successfully\n");
		return IXGBE_ERR_PHY;
	}

	return 0;
}

/**
 * ixgbe_check_cs4227 - Check CS4227 and reset as needed
 * @hw: pointer to hardware structure
 */
static void ixgbe_check_cs4227(struct ixgbe_hw *hw)
{
	u32 swfw_mask = hw->phy.phy_semaphore_mask;
	s32 status;
	u16 value;
	u8 retry;

	for (retry = 0; retry < IXGBE_CS4227_RETRIES; retry++) {
		status = hw->mac.ops.acquire_swfw_sync(hw, swfw_mask);
		if (status) {
			hw_err(hw, "semaphore failed with %d\n", status);
			msleep(IXGBE_CS4227_CHECK_DELAY);
			continue;
		}

		/* Get status of reset flow. */
		status = ixgbe_read_cs4227(hw, IXGBE_CS4227_SCRATCH, &value);
		if (!status && value == IXGBE_CS4227_RESET_COMPLETE)
			goto out;

		if (status || value != IXGBE_CS4227_RESET_PENDING)
			break;

		/* Reset is pending. Wait and check again. */
		hw->mac.ops.release_swfw_sync(hw, swfw_mask);
		msleep(IXGBE_CS4227_CHECK_DELAY);
	}
	/* If still pending, assume other instance failed. */
	if (retry == IXGBE_CS4227_RETRIES) {
		status = hw->mac.ops.acquire_swfw_sync(hw, swfw_mask);
		if (status) {
			hw_err(hw, "semaphore failed with %d\n", status);
			return;
		}
	}

	/* Reset the CS4227. */
	status = ixgbe_reset_cs4227(hw);
	if (status) {
		hw_err(hw, "CS4227 reset failed: %d", status);
		goto out;
	}

	/* Reset takes so long, temporarily release semaphore in case the
	 * other driver instance is waiting for the reset indication.
	 */
	ixgbe_write_cs4227(hw, IXGBE_CS4227_SCRATCH,
			   IXGBE_CS4227_RESET_PENDING);
	hw->mac.ops.release_swfw_sync(hw, swfw_mask);
	usleep_range(10000, 12000);
	status = hw->mac.ops.acquire_swfw_sync(hw, swfw_mask);
	if (status) {
		hw_err(hw, "semaphore failed with %d", status);
		return;
	}

	/* Record completion for next time. */
	status = ixgbe_write_cs4227(hw, IXGBE_CS4227_SCRATCH,
				    IXGBE_CS4227_RESET_COMPLETE);

out:
	hw->mac.ops.release_swfw_sync(hw, swfw_mask);
	msleep(hw->eeprom.semaphore_delay);
}

/** ixgbe_identify_phy_x550em - Get PHY type based on device id
 *  @hw: pointer to hardware structure
 *
 *  Returns error code
 */
static s32 ixgbe_identify_phy_x550em(struct ixgbe_hw *hw)
{
	switch (hw->device_id) {
	case IXGBE_DEV_ID_X550EM_X_SFP:
		/* set up for CS4227 usage */
		hw->phy.phy_semaphore_mask = IXGBE_GSSR_SHARED_I2C_SM;
		ixgbe_setup_mux_ctl(hw);
		ixgbe_check_cs4227(hw);
		return ixgbe_identify_module_generic(hw);
	case IXGBE_DEV_ID_X550EM_X_KX4:
		hw->phy.type = ixgbe_phy_x550em_kx4;
		break;
	case IXGBE_DEV_ID_X550EM_X_KR:
		hw->phy.type = ixgbe_phy_x550em_kr;
		break;
	case IXGBE_DEV_ID_X550EM_X_1G_T:
	case IXGBE_DEV_ID_X550EM_X_10G_T:
		return ixgbe_identify_phy_generic(hw);
	default:
		break;
	}
	return 0;
}

static s32 ixgbe_read_phy_reg_x550em(struct ixgbe_hw *hw, u32 reg_addr,
				     u32 device_type, u16 *phy_data)
{
	return IXGBE_NOT_IMPLEMENTED;
}

static s32 ixgbe_write_phy_reg_x550em(struct ixgbe_hw *hw, u32 reg_addr,
				      u32 device_type, u16 phy_data)
{
	return IXGBE_NOT_IMPLEMENTED;
}

/** ixgbe_init_eeprom_params_X550 - Initialize EEPROM params
 *  @hw: pointer to hardware structure
 *
 *  Initializes the EEPROM parameters ixgbe_eeprom_info within the
 *  ixgbe_hw struct in order to set up EEPROM access.
 **/
static s32 ixgbe_init_eeprom_params_X550(struct ixgbe_hw *hw)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	u32 eec;
	u16 eeprom_size;

	if (eeprom->type == ixgbe_eeprom_uninitialized) {
		eeprom->semaphore_delay = 10;
		eeprom->type = ixgbe_flash;

		eec = IXGBE_READ_REG(hw, IXGBE_EEC(hw));
		eeprom_size = (u16)((eec & IXGBE_EEC_SIZE) >>
				    IXGBE_EEC_SIZE_SHIFT);
		eeprom->word_size = 1 << (eeprom_size +
					  IXGBE_EEPROM_WORD_SIZE_SHIFT);

		hw_dbg(hw, "Eeprom params: type = %d, size = %d\n",
		       eeprom->type, eeprom->word_size);
	}

	return 0;
}

/**
 * ixgbe_iosf_wait - Wait for IOSF command completion
 * @hw: pointer to hardware structure
 * @ctrl: pointer to location to receive final IOSF control value
 *
 * Return: failing status on timeout
 *
 * Note: ctrl can be NULL if the IOSF control register value is not needed
 */
static s32 ixgbe_iosf_wait(struct ixgbe_hw *hw, u32 *ctrl)
{
	u32 i, command;

	/* Check every 10 usec to see if the address cycle completed.
	 * The SB IOSF BUSY bit will clear when the operation is
	 * complete.
	 */
	for (i = 0; i < IXGBE_MDIO_COMMAND_TIMEOUT; i++) {
		command = IXGBE_READ_REG(hw, IXGBE_SB_IOSF_INDIRECT_CTRL);
		if (!(command & IXGBE_SB_IOSF_CTRL_BUSY))
			break;
		usleep_range(10, 20);
	}
	if (ctrl)
		*ctrl = command;
	if (i == IXGBE_MDIO_COMMAND_TIMEOUT) {
		hw_dbg(hw, "IOSF wait timed out\n");
		return IXGBE_ERR_PHY;
	}

	return 0;
}

/** ixgbe_read_iosf_sb_reg_x550 - Writes a value to specified register of the
 *  IOSF device
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: 3 bit device type
 *  @phy_data: Pointer to read data from the register
 **/
static s32 ixgbe_read_iosf_sb_reg_x550(struct ixgbe_hw *hw, u32 reg_addr,
				       u32 device_type, u32 *data)
{
	u32 gssr = IXGBE_GSSR_PHY1_SM | IXGBE_GSSR_PHY0_SM;
	u32 command, error;
	s32 ret;

	ret = hw->mac.ops.acquire_swfw_sync(hw, gssr);
	if (ret)
		return ret;

	ret = ixgbe_iosf_wait(hw, NULL);
	if (ret)
		goto out;

	command = ((reg_addr << IXGBE_SB_IOSF_CTRL_ADDR_SHIFT) |
		   (device_type << IXGBE_SB_IOSF_CTRL_TARGET_SELECT_SHIFT));

	/* Write IOSF control register */
	IXGBE_WRITE_REG(hw, IXGBE_SB_IOSF_INDIRECT_CTRL, command);

	ret = ixgbe_iosf_wait(hw, &command);

	if ((command & IXGBE_SB_IOSF_CTRL_RESP_STAT_MASK) != 0) {
		error = (command & IXGBE_SB_IOSF_CTRL_CMPL_ERR_MASK) >>
			 IXGBE_SB_IOSF_CTRL_CMPL_ERR_SHIFT;
		hw_dbg(hw, "Failed to read, error %x\n", error);
		return IXGBE_ERR_PHY;
	}

	if (!ret)
		*data = IXGBE_READ_REG(hw, IXGBE_SB_IOSF_INDIRECT_DATA);

out:
	hw->mac.ops.release_swfw_sync(hw, gssr);
	return ret;
}

/** ixgbe_read_ee_hostif_data_X550 - Read EEPROM word using a host interface
 *  command assuming that the semaphore is already obtained.
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM using the hostif.
 **/
static s32 ixgbe_read_ee_hostif_data_X550(struct ixgbe_hw *hw, u16 offset,
					  u16 *data)
{
	s32 status;
	struct ixgbe_hic_read_shadow_ram buffer;

	buffer.hdr.req.cmd = FW_READ_SHADOW_RAM_CMD;
	buffer.hdr.req.buf_lenh = 0;
	buffer.hdr.req.buf_lenl = FW_READ_SHADOW_RAM_LEN;
	buffer.hdr.req.checksum = FW_DEFAULT_CHECKSUM;

	/* convert offset from words to bytes */
	buffer.address = cpu_to_be32(offset * 2);
	/* one word */
	buffer.length = cpu_to_be16(sizeof(u16));

	status = ixgbe_host_interface_command(hw, (u32 *)&buffer,
					      sizeof(buffer),
					      IXGBE_HI_COMMAND_TIMEOUT, false);
	if (status)
		return status;

	*data = (u16)IXGBE_READ_REG_ARRAY(hw, IXGBE_FLEX_MNG,
					  FW_NVM_DATA_OFFSET);

	return 0;
}

/** ixgbe_read_ee_hostif_buffer_X550- Read EEPROM word(s) using hostif
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @words: number of words
 *  @data: word(s) read from the EEPROM
 *
 *  Reads a 16 bit word(s) from the EEPROM using the hostif.
 **/
static s32 ixgbe_read_ee_hostif_buffer_X550(struct ixgbe_hw *hw,
					    u16 offset, u16 words, u16 *data)
{
	struct ixgbe_hic_read_shadow_ram buffer;
	u32 current_word = 0;
	u16 words_to_read;
	s32 status;
	u32 i;

	/* Take semaphore for the entire operation. */
	status = hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	if (status) {
		hw_dbg(hw, "EEPROM read buffer - semaphore failed\n");
		return status;
	}

	while (words) {
		if (words > FW_MAX_READ_BUFFER_SIZE / 2)
			words_to_read = FW_MAX_READ_BUFFER_SIZE / 2;
		else
			words_to_read = words;

		buffer.hdr.req.cmd = FW_READ_SHADOW_RAM_CMD;
		buffer.hdr.req.buf_lenh = 0;
		buffer.hdr.req.buf_lenl = FW_READ_SHADOW_RAM_LEN;
		buffer.hdr.req.checksum = FW_DEFAULT_CHECKSUM;

		/* convert offset from words to bytes */
		buffer.address = cpu_to_be32((offset + current_word) * 2);
		buffer.length = cpu_to_be16(words_to_read * 2);

		status = ixgbe_host_interface_command(hw, (u32 *)&buffer,
						      sizeof(buffer),
						      IXGBE_HI_COMMAND_TIMEOUT,
						      false);
		if (status) {
			hw_dbg(hw, "Host interface command failed\n");
			goto out;
		}

		for (i = 0; i < words_to_read; i++) {
			u32 reg = IXGBE_FLEX_MNG + (FW_NVM_DATA_OFFSET << 2) +
				  2 * i;
			u32 value = IXGBE_READ_REG(hw, reg);

			data[current_word] = (u16)(value & 0xffff);
			current_word++;
			i++;
			if (i < words_to_read) {
				value >>= 16;
				data[current_word] = (u16)(value & 0xffff);
				current_word++;
			}
		}
		words -= words_to_read;
	}

out:
	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	return status;
}

/** ixgbe_checksum_ptr_x550 - Checksum one pointer region
 *  @hw: pointer to hardware structure
 *  @ptr: pointer offset in eeprom
 *  @size: size of section pointed by ptr, if 0 first word will be used as size
 *  @csum: address of checksum to update
 *
 *  Returns error status for any failure
 **/
static s32 ixgbe_checksum_ptr_x550(struct ixgbe_hw *hw, u16 ptr,
				   u16 size, u16 *csum, u16 *buffer,
				   u32 buffer_size)
{
	u16 buf[256];
	s32 status;
	u16 length, bufsz, i, start;
	u16 *local_buffer;

	bufsz = sizeof(buf) / sizeof(buf[0]);

	/* Read a chunk at the pointer location */
	if (!buffer) {
		status = ixgbe_read_ee_hostif_buffer_X550(hw, ptr, bufsz, buf);
		if (status) {
			hw_dbg(hw, "Failed to read EEPROM image\n");
			return status;
		}
		local_buffer = buf;
	} else {
		if (buffer_size < ptr)
			return  IXGBE_ERR_PARAM;
		local_buffer = &buffer[ptr];
	}

	if (size) {
		start = 0;
		length = size;
	} else {
		start = 1;
		length = local_buffer[0];

		/* Skip pointer section if length is invalid. */
		if (length == 0xFFFF || length == 0 ||
		    (ptr + length) >= hw->eeprom.word_size)
			return 0;
	}

	if (buffer && ((u32)start + (u32)length > buffer_size))
		return IXGBE_ERR_PARAM;

	for (i = start; length; i++, length--) {
		if (i == bufsz && !buffer) {
			ptr += bufsz;
			i = 0;
			if (length < bufsz)
				bufsz = length;

			/* Read a chunk at the pointer location */
			status = ixgbe_read_ee_hostif_buffer_X550(hw, ptr,
								  bufsz, buf);
			if (status) {
				hw_dbg(hw, "Failed to read EEPROM image\n");
				return status;
			}
		}
		*csum += local_buffer[i];
	}
	return 0;
}

/** ixgbe_calc_checksum_X550 - Calculates and returns the checksum
 *  @hw: pointer to hardware structure
 *  @buffer: pointer to buffer containing calculated checksum
 *  @buffer_size: size of buffer
 *
 *  Returns a negative error code on error, or the 16-bit checksum
 **/
static s32 ixgbe_calc_checksum_X550(struct ixgbe_hw *hw, u16 *buffer,
				    u32 buffer_size)
{
	u16 eeprom_ptrs[IXGBE_EEPROM_LAST_WORD + 1];
	u16 *local_buffer;
	s32 status;
	u16 checksum = 0;
	u16 pointer, i, size;

	hw->eeprom.ops.init_params(hw);

	if (!buffer) {
		/* Read pointer area */
		status = ixgbe_read_ee_hostif_buffer_X550(hw, 0,
						IXGBE_EEPROM_LAST_WORD + 1,
						eeprom_ptrs);
		if (status) {
			hw_dbg(hw, "Failed to read EEPROM image\n");
			return status;
		}
		local_buffer = eeprom_ptrs;
	} else {
		if (buffer_size < IXGBE_EEPROM_LAST_WORD)
			return IXGBE_ERR_PARAM;
		local_buffer = buffer;
	}

	/* For X550 hardware include 0x0-0x41 in the checksum, skip the
	 * checksum word itself
	 */
	for (i = 0; i <= IXGBE_EEPROM_LAST_WORD; i++)
		if (i != IXGBE_EEPROM_CHECKSUM)
			checksum += local_buffer[i];

	/* Include all data from pointers 0x3, 0x6-0xE.  This excludes the
	 * FW, PHY module, and PCIe Expansion/Option ROM pointers.
	 */
	for (i = IXGBE_PCIE_ANALOG_PTR_X550; i < IXGBE_FW_PTR; i++) {
		if (i == IXGBE_PHY_PTR || i == IXGBE_OPTION_ROM_PTR)
			continue;

		pointer = local_buffer[i];

		/* Skip pointer section if the pointer is invalid. */
		if (pointer == 0xFFFF || pointer == 0 ||
		    pointer >= hw->eeprom.word_size)
			continue;

		switch (i) {
		case IXGBE_PCIE_GENERAL_PTR:
			size = IXGBE_IXGBE_PCIE_GENERAL_SIZE;
			break;
		case IXGBE_PCIE_CONFIG0_PTR:
		case IXGBE_PCIE_CONFIG1_PTR:
			size = IXGBE_PCIE_CONFIG_SIZE;
			break;
		default:
			size = 0;
			break;
		}

		status = ixgbe_checksum_ptr_x550(hw, pointer, size, &checksum,
						 buffer, buffer_size);
		if (status)
			return status;
	}

	checksum = (u16)IXGBE_EEPROM_SUM - checksum;

	return (s32)checksum;
}

/** ixgbe_calc_eeprom_checksum_X550 - Calculates and returns the checksum
 *  @hw: pointer to hardware structure
 *
 *  Returns a negative error code on error, or the 16-bit checksum
 **/
static s32 ixgbe_calc_eeprom_checksum_X550(struct ixgbe_hw *hw)
{
	return ixgbe_calc_checksum_X550(hw, NULL, 0);
}

/** ixgbe_read_ee_hostif_X550 - Read EEPROM word using a host interface command
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @data: word read from the EEPROM
 *
 *   Reads a 16 bit word from the EEPROM using the hostif.
 **/
static s32 ixgbe_read_ee_hostif_X550(struct ixgbe_hw *hw, u16 offset, u16 *data)
{
	s32 status = 0;

	if (hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM) == 0) {
		status = ixgbe_read_ee_hostif_data_X550(hw, offset, data);
		hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	} else {
		status = IXGBE_ERR_SWFW_SYNC;
	}

	return status;
}

/** ixgbe_validate_eeprom_checksum_X550 - Validate EEPROM checksum
 *  @hw: pointer to hardware structure
 *  @checksum_val: calculated checksum
 *
 *  Performs checksum calculation and validates the EEPROM checksum.  If the
 *  caller does not need checksum_val, the value can be NULL.
 **/
static s32 ixgbe_validate_eeprom_checksum_X550(struct ixgbe_hw *hw,
					       u16 *checksum_val)
{
	s32 status;
	u16 checksum;
	u16 read_checksum = 0;

	/* Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = hw->eeprom.ops.read(hw, 0, &checksum);
	if (status) {
		hw_dbg(hw, "EEPROM read failed\n");
		return status;
	}

	status = hw->eeprom.ops.calc_checksum(hw);
	if (status < 0)
		return status;

	checksum = (u16)(status & 0xffff);

	status = ixgbe_read_ee_hostif_X550(hw, IXGBE_EEPROM_CHECKSUM,
					   &read_checksum);
	if (status)
		return status;

	/* Verify read checksum from EEPROM is the same as
	 * calculated checksum
	 */
	if (read_checksum != checksum) {
		status = IXGBE_ERR_EEPROM_CHECKSUM;
		hw_dbg(hw, "Invalid EEPROM checksum");
	}

	/* If the user cares, return the calculated checksum */
	if (checksum_val)
		*checksum_val = checksum;

	return status;
}

/** ixgbe_write_ee_hostif_X550 - Write EEPROM word using hostif
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to write
 *  @data: word write to the EEPROM
 *
 *  Write a 16 bit word to the EEPROM using the hostif.
 **/
static s32 ixgbe_write_ee_hostif_data_X550(struct ixgbe_hw *hw, u16 offset,
					   u16 data)
{
	s32 status;
	struct ixgbe_hic_write_shadow_ram buffer;

	buffer.hdr.req.cmd = FW_WRITE_SHADOW_RAM_CMD;
	buffer.hdr.req.buf_lenh = 0;
	buffer.hdr.req.buf_lenl = FW_WRITE_SHADOW_RAM_LEN;
	buffer.hdr.req.checksum = FW_DEFAULT_CHECKSUM;

	/* one word */
	buffer.length = cpu_to_be16(sizeof(u16));
	buffer.data = data;
	buffer.address = cpu_to_be32(offset * 2);

	status = ixgbe_host_interface_command(hw, (u32 *)&buffer,
					      sizeof(buffer),
					      IXGBE_HI_COMMAND_TIMEOUT, false);
	return status;
}

/** ixgbe_write_ee_hostif_X550 - Write EEPROM word using hostif
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to write
 *  @data: word write to the EEPROM
 *
 *  Write a 16 bit word to the EEPROM using the hostif.
 **/
static s32 ixgbe_write_ee_hostif_X550(struct ixgbe_hw *hw, u16 offset, u16 data)
{
	s32 status = 0;

	if (hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM) == 0) {
		status = ixgbe_write_ee_hostif_data_X550(hw, offset, data);
		hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	} else {
		hw_dbg(hw, "write ee hostif failed to get semaphore");
		status = IXGBE_ERR_SWFW_SYNC;
	}

	return status;
}

/** ixgbe_update_flash_X550 - Instruct HW to copy EEPROM to Flash device
 *  @hw: pointer to hardware structure
 *
 *  Issue a shadow RAM dump to FW to copy EEPROM from shadow RAM to the flash.
 **/
static s32 ixgbe_update_flash_X550(struct ixgbe_hw *hw)
{
	s32 status = 0;
	union ixgbe_hic_hdr2 buffer;

	buffer.req.cmd = FW_SHADOW_RAM_DUMP_CMD;
	buffer.req.buf_lenh = 0;
	buffer.req.buf_lenl = FW_SHADOW_RAM_DUMP_LEN;
	buffer.req.checksum = FW_DEFAULT_CHECKSUM;

	status = ixgbe_host_interface_command(hw, (u32 *)&buffer,
					      sizeof(buffer),
					      IXGBE_HI_COMMAND_TIMEOUT, false);
	return status;
}

/**
 * ixgbe_get_bus_info_X550em - Set PCI bus info
 * @hw: pointer to hardware structure
 *
 * Sets bus link width and speed to unknown because X550em is
 * not a PCI device.
 **/
static s32 ixgbe_get_bus_info_X550em(struct ixgbe_hw *hw)
{
	hw->bus.type  = ixgbe_bus_type_internal;
	hw->bus.width = ixgbe_bus_width_unknown;
	hw->bus.speed = ixgbe_bus_speed_unknown;

	hw->mac.ops.set_lan_id(hw);

	return 0;
}

/** ixgbe_disable_rx_x550 - Disable RX unit
 *
 *  Enables the Rx DMA unit for x550
 **/
static void ixgbe_disable_rx_x550(struct ixgbe_hw *hw)
{
	u32 rxctrl, pfdtxgswc;
	s32 status;
	struct ixgbe_hic_disable_rxen fw_cmd;

	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	if (rxctrl & IXGBE_RXCTRL_RXEN) {
		pfdtxgswc = IXGBE_READ_REG(hw, IXGBE_PFDTXGSWC);
		if (pfdtxgswc & IXGBE_PFDTXGSWC_VT_LBEN) {
			pfdtxgswc &= ~IXGBE_PFDTXGSWC_VT_LBEN;
			IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, pfdtxgswc);
			hw->mac.set_lben = true;
		} else {
			hw->mac.set_lben = false;
		}

		fw_cmd.hdr.cmd = FW_DISABLE_RXEN_CMD;
		fw_cmd.hdr.buf_len = FW_DISABLE_RXEN_LEN;
		fw_cmd.hdr.checksum = FW_DEFAULT_CHECKSUM;
		fw_cmd.port_number = (u8)hw->bus.lan_id;

		status = ixgbe_host_interface_command(hw, (u32 *)&fw_cmd,
					sizeof(struct ixgbe_hic_disable_rxen),
					IXGBE_HI_COMMAND_TIMEOUT, true);

		/* If we fail - disable RX using register write */
		if (status) {
			rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
			if (rxctrl & IXGBE_RXCTRL_RXEN) {
				rxctrl &= ~IXGBE_RXCTRL_RXEN;
				IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxctrl);
			}
		}
	}
}

/** ixgbe_update_eeprom_checksum_X550 - Updates the EEPROM checksum and flash
 *  @hw: pointer to hardware structure
 *
 *  After writing EEPROM to shadow RAM using EEWR register, software calculates
 *  checksum and updates the EEPROM and instructs the hardware to update
 *  the flash.
 **/
static s32 ixgbe_update_eeprom_checksum_X550(struct ixgbe_hw *hw)
{
	s32 status;
	u16 checksum = 0;

	/* Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = ixgbe_read_ee_hostif_X550(hw, 0, &checksum);
	if (status) {
		hw_dbg(hw, "EEPROM read failed\n");
		return status;
	}

	status = ixgbe_calc_eeprom_checksum_X550(hw);
	if (status < 0)
		return status;

	checksum = (u16)(status & 0xffff);

	status = ixgbe_write_ee_hostif_X550(hw, IXGBE_EEPROM_CHECKSUM,
					    checksum);
	if (status)
		return status;

	status = ixgbe_update_flash_X550(hw);

	return status;
}

/** ixgbe_write_ee_hostif_buffer_X550 - Write EEPROM word(s) using hostif
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to write
 *  @words: number of words
 *  @data: word(s) write to the EEPROM
 *
 *
 *  Write a 16 bit word(s) to the EEPROM using the hostif.
 **/
static s32 ixgbe_write_ee_hostif_buffer_X550(struct ixgbe_hw *hw,
					     u16 offset, u16 words,
					     u16 *data)
{
	s32 status = 0;
	u32 i = 0;

	/* Take semaphore for the entire operation. */
	status = hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	if (status) {
		hw_dbg(hw, "EEPROM write buffer - semaphore failed\n");
		return status;
	}

	for (i = 0; i < words; i++) {
		status = ixgbe_write_ee_hostif_data_X550(hw, offset + i,
							 data[i]);
		if (status) {
			hw_dbg(hw, "Eeprom buffered write failed\n");
			break;
		}
	}

	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);

	return status;
}

/** ixgbe_write_iosf_sb_reg_x550 - Writes a value to specified register of the
 *  IOSF device
 *
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: 3 bit device type
 *  @data: Data to write to the register
 **/
static s32 ixgbe_write_iosf_sb_reg_x550(struct ixgbe_hw *hw, u32 reg_addr,
					u32 device_type, u32 data)
{
	u32 gssr = IXGBE_GSSR_PHY1_SM | IXGBE_GSSR_PHY0_SM;
	u32 command, error;
	s32 ret;

	ret = hw->mac.ops.acquire_swfw_sync(hw, gssr);
	if (ret)
		return ret;

	ret = ixgbe_iosf_wait(hw, NULL);
	if (ret)
		goto out;

	command = ((reg_addr << IXGBE_SB_IOSF_CTRL_ADDR_SHIFT) |
		   (device_type << IXGBE_SB_IOSF_CTRL_TARGET_SELECT_SHIFT));

	/* Write IOSF control register */
	IXGBE_WRITE_REG(hw, IXGBE_SB_IOSF_INDIRECT_CTRL, command);

	/* Write IOSF data register */
	IXGBE_WRITE_REG(hw, IXGBE_SB_IOSF_INDIRECT_DATA, data);

	ret = ixgbe_iosf_wait(hw, &command);

	if ((command & IXGBE_SB_IOSF_CTRL_RESP_STAT_MASK) != 0) {
		error = (command & IXGBE_SB_IOSF_CTRL_CMPL_ERR_MASK) >>
			 IXGBE_SB_IOSF_CTRL_CMPL_ERR_SHIFT;
		hw_dbg(hw, "Failed to write, error %x\n", error);
		return IXGBE_ERR_PHY;
	}

out:
	hw->mac.ops.release_swfw_sync(hw, gssr);
	return ret;
}

/** ixgbe_setup_ixfi_x550em - Configure the KR PHY for iXFI mode.
 *  @hw: pointer to hardware structure
 *  @speed: the link speed to force
 *
 *  Configures the integrated KR PHY to use iXFI mode. Used to connect an
 *  internal and external PHY at a specific speed, without autonegotiation.
 **/
static s32 ixgbe_setup_ixfi_x550em(struct ixgbe_hw *hw, ixgbe_link_speed *speed)
{
	s32 status;
	u32 reg_val;

	/* Disable AN and force speed to 10G Serial. */
	status = ixgbe_read_iosf_sb_reg_x550(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status)
		return status;

	reg_val &= ~IXGBE_KRM_LINK_CTRL_1_TETH_AN_ENABLE;
	reg_val &= ~IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_MASK;

	/* Select forced link speed for internal PHY. */
	switch (*speed) {
	case IXGBE_LINK_SPEED_10GB_FULL:
		reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_10G;
		break;
	case IXGBE_LINK_SPEED_1GB_FULL:
		reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_1G;
		break;
	default:
		/* Other link speeds are not supported by internal KR PHY. */
		return IXGBE_ERR_LINK_SETUP;
	}

	status = ixgbe_write_iosf_sb_reg_x550(hw,
				IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status)
		return status;

	/* Disable training protocol FSM. */
	status = ixgbe_read_iosf_sb_reg_x550(hw,
				IXGBE_KRM_RX_TRN_LINKUP_CTRL(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status)
		return status;

	reg_val |= IXGBE_KRM_RX_TRN_LINKUP_CTRL_CONV_WO_PROTOCOL;
	status = ixgbe_write_iosf_sb_reg_x550(hw,
				IXGBE_KRM_RX_TRN_LINKUP_CTRL(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status)
		return status;

	/* Disable Flex from training TXFFE. */
	status = ixgbe_read_iosf_sb_reg_x550(hw,
				IXGBE_KRM_DSP_TXFFE_STATE_4(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status)
		return status;

	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_C0_EN;
	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_CP1_CN1_EN;
	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_CO_ADAPT_EN;
	status = ixgbe_write_iosf_sb_reg_x550(hw,
				IXGBE_KRM_DSP_TXFFE_STATE_4(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status)
		return status;

	status = ixgbe_read_iosf_sb_reg_x550(hw,
				IXGBE_KRM_DSP_TXFFE_STATE_5(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status)
		return status;

	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_C0_EN;
	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_CP1_CN1_EN;
	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_CO_ADAPT_EN;
	status = ixgbe_write_iosf_sb_reg_x550(hw,
				IXGBE_KRM_DSP_TXFFE_STATE_5(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status)
		return status;

	/* Enable override for coefficients. */
	status = ixgbe_read_iosf_sb_reg_x550(hw,
				IXGBE_KRM_TX_COEFF_CTRL_1(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status)
		return status;

	reg_val |= IXGBE_KRM_TX_COEFF_CTRL_1_OVRRD_EN;
	reg_val |= IXGBE_KRM_TX_COEFF_CTRL_1_CZERO_EN;
	reg_val |= IXGBE_KRM_TX_COEFF_CTRL_1_CPLUS1_OVRRD_EN;
	reg_val |= IXGBE_KRM_TX_COEFF_CTRL_1_CMINUS1_OVRRD_EN;
	status = ixgbe_write_iosf_sb_reg_x550(hw,
				IXGBE_KRM_TX_COEFF_CTRL_1(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status)
		return status;

	/* Toggle port SW reset by AN reset. */
	status = ixgbe_read_iosf_sb_reg_x550(hw,
				IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status)
		return status;

	reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_RESTART;
	status = ixgbe_write_iosf_sb_reg_x550(hw,
				IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);

	return status;
}

/**
 *  ixgbe_supported_sfp_modules_X550em - Check if SFP module type is supported
 *  @hw: pointer to hardware structure
 *  @linear: true if SFP module is linear
 */
static s32 ixgbe_supported_sfp_modules_X550em(struct ixgbe_hw *hw, bool *linear)
{
	switch (hw->phy.sfp_type) {
	case ixgbe_sfp_type_not_present:
		return IXGBE_ERR_SFP_NOT_PRESENT;
	case ixgbe_sfp_type_da_cu_core0:
	case ixgbe_sfp_type_da_cu_core1:
		*linear = true;
		break;
	case ixgbe_sfp_type_srlr_core0:
	case ixgbe_sfp_type_srlr_core1:
	case ixgbe_sfp_type_da_act_lmt_core0:
	case ixgbe_sfp_type_da_act_lmt_core1:
	case ixgbe_sfp_type_1g_sx_core0:
	case ixgbe_sfp_type_1g_sx_core1:
	case ixgbe_sfp_type_1g_lx_core0:
	case ixgbe_sfp_type_1g_lx_core1:
		*linear = false;
		break;
	case ixgbe_sfp_type_unknown:
	case ixgbe_sfp_type_1g_cu_core0:
	case ixgbe_sfp_type_1g_cu_core1:
	default:
		return IXGBE_ERR_SFP_NOT_SUPPORTED;
	}

	return 0;
}

/**
 *  ixgbe_setup_mac_link_sfp_x550em - Configure the KR PHY for SFP.
 *  @hw: pointer to hardware structure
 *
 *  Configures the extern PHY and the integrated KR PHY for SFP support.
 */
static s32
ixgbe_setup_mac_link_sfp_x550em(struct ixgbe_hw *hw,
				ixgbe_link_speed speed,
				__always_unused bool autoneg_wait_to_complete)
{
	s32 status;
	u16 slice, value;
	bool setup_linear = false;

	/* Check if SFP module is supported and linear */
	status = ixgbe_supported_sfp_modules_X550em(hw, &setup_linear);

	/* If no SFP module present, then return success. Return success since
	 * there is no reason to configure CS4227 and SFP not present error is
	 * not accepted in the setup MAC link flow.
	 */
	if (status == IXGBE_ERR_SFP_NOT_PRESENT)
		return 0;

	if (status)
		return status;

	if (!(hw->phy.nw_mng_if_sel & IXGBE_NW_MNG_IF_SEL_INT_PHY_MODE)) {
		/* Configure CS4227 LINE side to 10G SR. */
		slice = IXGBE_CS4227_LINE_SPARE22_MSB + (hw->bus.lan_id << 12);
		value = IXGBE_CS4227_SPEED_10G;
		status = ixgbe_write_i2c_combined_generic(hw, IXGBE_CS4227,
							  slice, value);
		if (status)
			goto i2c_err;

		slice = IXGBE_CS4227_LINE_SPARE24_LSB + (hw->bus.lan_id << 12);
		value = (IXGBE_CS4227_EDC_MODE_SR << 1) | 1;
		status = ixgbe_write_i2c_combined_generic(hw, IXGBE_CS4227,
							  slice, value);
		if (status)
			goto i2c_err;

		/* Configure CS4227 for HOST connection rate then type. */
		slice = IXGBE_CS4227_HOST_SPARE22_MSB + (hw->bus.lan_id << 12);
		value = speed & IXGBE_LINK_SPEED_10GB_FULL ?
			IXGBE_CS4227_SPEED_10G : IXGBE_CS4227_SPEED_1G;
		status = ixgbe_write_i2c_combined_generic(hw, IXGBE_CS4227,
							  slice, value);
		if (status)
			goto i2c_err;

		slice = IXGBE_CS4227_HOST_SPARE24_LSB + (hw->bus.lan_id << 12);
		if (setup_linear)
			value = (IXGBE_CS4227_EDC_MODE_CX1 << 1) | 1;
		else
			value = (IXGBE_CS4227_EDC_MODE_SR << 1) | 1;
		status = ixgbe_write_i2c_combined_generic(hw, IXGBE_CS4227,
							  slice, value);
		if (status)
			goto i2c_err;

		/* Setup XFI internal link. */
		status = ixgbe_setup_ixfi_x550em(hw, &speed);
		if (status) {
			hw_dbg(hw, "setup_ixfi failed with %d\n", status);
			return status;
		}
	} else {
		/* Configure internal PHY for KR/KX. */
		status = ixgbe_setup_kr_speed_x550em(hw, speed);
		if (status) {
			hw_dbg(hw, "setup_kr_speed failed with %d\n", status);
			return status;
		}

		/* Configure CS4227 LINE side to proper mode. */
		slice = IXGBE_CS4227_LINE_SPARE24_LSB + (hw->bus.lan_id << 12);
		if (setup_linear)
			value = (IXGBE_CS4227_EDC_MODE_CX1 << 1) | 1;
		else
			value = (IXGBE_CS4227_EDC_MODE_SR << 1) | 1;
		status = ixgbe_write_i2c_combined_generic(hw, IXGBE_CS4227,
							  slice, value);
		if (status)
			goto i2c_err;
	}

	return 0;

i2c_err:
	hw_dbg(hw, "combined i2c access failed with %d\n", status);
	return status;
}

/**
 * ixgbe_setup_mac_link_t_X550em - Sets the auto advertised link speed
 * @hw: pointer to hardware structure
 * @speed: new link speed
 * @autoneg_wait_to_complete: true when waiting for completion is needed
 *
 * Setup internal/external PHY link speed based on link speed, then set
 * external PHY auto advertised link speed.
 *
 * Returns error status for any failure
 **/
static s32 ixgbe_setup_mac_link_t_X550em(struct ixgbe_hw *hw,
					 ixgbe_link_speed speed,
					 bool autoneg_wait)
{
	s32 status;
	ixgbe_link_speed force_speed;

	/* Setup internal/external PHY link speed to iXFI (10G), unless
	 * only 1G is auto advertised then setup KX link.
	 */
	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		force_speed = IXGBE_LINK_SPEED_10GB_FULL;
	else
		force_speed = IXGBE_LINK_SPEED_1GB_FULL;

	/* If internal link mode is XFI, then setup XFI internal link. */
	if (!(hw->phy.nw_mng_if_sel & IXGBE_NW_MNG_IF_SEL_INT_PHY_MODE)) {
		status = ixgbe_setup_ixfi_x550em(hw, &force_speed);

		if (status)
			return status;
	}

	return hw->phy.ops.setup_link_speed(hw, speed, autoneg_wait);
}

/** ixgbe_check_link_t_X550em - Determine link and speed status
  * @hw: pointer to hardware structure
  * @speed: pointer to link speed
  * @link_up: true when link is up
  * @link_up_wait_to_complete: bool used to wait for link up or not
  *
  * Check that both the MAC and X557 external PHY have link.
  **/
static s32 ixgbe_check_link_t_X550em(struct ixgbe_hw *hw,
				     ixgbe_link_speed *speed,
				     bool *link_up,
				     bool link_up_wait_to_complete)
{
	u32 status;
	u16 autoneg_status;

	if (hw->mac.ops.get_media_type(hw) != ixgbe_media_type_copper)
		return IXGBE_ERR_CONFIG;

	status = ixgbe_check_mac_link_generic(hw, speed, link_up,
					      link_up_wait_to_complete);

	/* If check link fails or MAC link is not up, then return */
	if (status || !(*link_up))
		return status;

	 /* MAC link is up, so check external PHY link.
	  * Read this twice back to back to indicate current status.
	  */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_STATUS,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      &autoneg_status);
	if (status)
		return status;

	/* If external PHY link is not up, then indicate link not up */
	if (!(autoneg_status & IXGBE_MDIO_AUTO_NEG_LINK_STATUS))
		*link_up = false;

	return 0;
}

/** ixgbe_init_mac_link_ops_X550em - init mac link function pointers
 *  @hw: pointer to hardware structure
 **/
static void ixgbe_init_mac_link_ops_X550em(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;

	switch (mac->ops.get_media_type(hw)) {
	case ixgbe_media_type_fiber:
		/* CS4227 does not support autoneg, so disable the laser control
		 * functions for SFP+ fiber
		 */
		mac->ops.disable_tx_laser = NULL;
		mac->ops.enable_tx_laser = NULL;
		mac->ops.flap_tx_laser = NULL;
		mac->ops.setup_link = ixgbe_setup_mac_link_multispeed_fiber;
		mac->ops.setup_mac_link = ixgbe_setup_mac_link_sfp_x550em;
		mac->ops.set_rate_select_speed =
					ixgbe_set_soft_rate_select_speed;
		break;
	case ixgbe_media_type_copper:
		mac->ops.setup_link = ixgbe_setup_mac_link_t_X550em;
		mac->ops.check_link = ixgbe_check_link_t_X550em;
		break;
	default:
		break;
	}
}

/** ixgbe_setup_sfp_modules_X550em - Setup SFP module
 * @hw: pointer to hardware structure
 */
static s32 ixgbe_setup_sfp_modules_X550em(struct ixgbe_hw *hw)
{
	s32 status;
	bool linear;

	/* Check if SFP module is supported */
	status = ixgbe_supported_sfp_modules_X550em(hw, &linear);
	if (status)
		return status;

	ixgbe_init_mac_link_ops_X550em(hw);
	hw->phy.ops.reset = NULL;

	return 0;
}

/** ixgbe_get_link_capabilities_x550em - Determines link capabilities
 * @hw: pointer to hardware structure
 * @speed: pointer to link speed
 * @autoneg: true when autoneg or autotry is enabled
 **/
static s32 ixgbe_get_link_capabilities_X550em(struct ixgbe_hw *hw,
					      ixgbe_link_speed *speed,
					      bool *autoneg)
{
	/* SFP */
	if (hw->phy.media_type == ixgbe_media_type_fiber) {
		/* CS4227 SFP must not enable auto-negotiation */
		*autoneg = false;

		if (hw->phy.sfp_type == ixgbe_sfp_type_1g_sx_core0 ||
		    hw->phy.sfp_type == ixgbe_sfp_type_1g_sx_core1) {
			*speed = IXGBE_LINK_SPEED_1GB_FULL;
			return 0;
		}

		/* Link capabilities are based on SFP */
		if (hw->phy.multispeed_fiber)
			*speed = IXGBE_LINK_SPEED_10GB_FULL |
				 IXGBE_LINK_SPEED_1GB_FULL;
		else
			*speed = IXGBE_LINK_SPEED_10GB_FULL;
	} else {
		*speed = IXGBE_LINK_SPEED_10GB_FULL |
			 IXGBE_LINK_SPEED_1GB_FULL;
		*autoneg = true;
	}
	return 0;
}

/**
 * ixgbe_get_lasi_ext_t_x550em - Determime external Base T PHY interrupt cause
 * @hw: pointer to hardware structure
 * @lsc: pointer to boolean flag which indicates whether external Base T
 *	 PHY interrupt is lsc
 *
 * Determime if external Base T PHY interrupt cause is high temperature
 * failure alarm or link status change.
 *
 * Return IXGBE_ERR_OVERTEMP if interrupt is high temperature
 * failure alarm, else return PHY access status.
 **/
static s32 ixgbe_get_lasi_ext_t_x550em(struct ixgbe_hw *hw, bool *lsc)
{
	u32 status;
	u16 reg;

	*lsc = false;

	/* Vendor alarm triggered */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_CHIP_STD_INT_FLAG,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);

	if (status || !(reg & IXGBE_MDIO_GLOBAL_VEN_ALM_INT_EN))
		return status;

	/* Vendor Auto-Neg alarm triggered or Global alarm 1 triggered */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_INT_CHIP_VEN_FLAG,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);

	if (status || !(reg & (IXGBE_MDIO_GLOBAL_AN_VEN_ALM_INT_EN |
				IXGBE_MDIO_GLOBAL_ALARM_1_INT)))
		return status;

	/* Global alarm triggered */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_ALARM_1,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);

	if (status)
		return status;

	/* If high temperature failure, then return over temp error and exit */
	if (reg & IXGBE_MDIO_GLOBAL_ALM_1_HI_TMP_FAIL) {
		/* power down the PHY in case the PHY FW didn't already */
		ixgbe_set_copper_phy_power(hw, false);
		return IXGBE_ERR_OVERTEMP;
	}
	if (reg & IXGBE_MDIO_GLOBAL_ALM_1_DEV_FAULT) {
		/*  device fault alarm triggered */
		status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_FAULT_MSG,
					  IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
					  &reg);
		if (status)
			return status;

		/* if device fault was due to high temp alarm handle and exit */
		if (reg == IXGBE_MDIO_GLOBAL_FAULT_MSG_HI_TMP) {
			/* power down the PHY in case the PHY FW didn't */
			ixgbe_set_copper_phy_power(hw, false);
			return IXGBE_ERR_OVERTEMP;
		}
	}

	/* Vendor alarm 2 triggered */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_CHIP_STD_INT_FLAG,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &reg);

	if (status || !(reg & IXGBE_MDIO_GLOBAL_STD_ALM2_INT))
		return status;

	/* link connect/disconnect event occurred */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_VENDOR_TX_ALARM2,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &reg);

	if (status)
		return status;

	/* Indicate LSC */
	if (reg & IXGBE_MDIO_AUTO_NEG_VEN_LSC)
		*lsc = true;

	return 0;
}

/**
 * ixgbe_enable_lasi_ext_t_x550em - Enable external Base T PHY interrupts
 * @hw: pointer to hardware structure
 *
 * Enable link status change and temperature failure alarm for the external
 * Base T PHY
 *
 * Returns PHY access status
 **/
static s32 ixgbe_enable_lasi_ext_t_x550em(struct ixgbe_hw *hw)
{
	u32 status;
	u16 reg;
	bool lsc;

	/* Clear interrupt flags */
	status = ixgbe_get_lasi_ext_t_x550em(hw, &lsc);

	/* Enable link status change alarm */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_PMA_TX_VEN_LASI_INT_MASK,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &reg);
	if (status)
		return status;

	reg |= IXGBE_MDIO_PMA_TX_VEN_LASI_INT_EN;

	status = hw->phy.ops.write_reg(hw, IXGBE_MDIO_PMA_TX_VEN_LASI_INT_MASK,
				       IXGBE_MDIO_AUTO_NEG_DEV_TYPE, reg);
	if (status)
		return status;

	/* Enable high temperature failure and global fault alarms */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_INT_MASK,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);
	if (status)
		return status;

	reg |= (IXGBE_MDIO_GLOBAL_INT_HI_TEMP_EN |
		IXGBE_MDIO_GLOBAL_INT_DEV_FAULT_EN);

	status = hw->phy.ops.write_reg(hw, IXGBE_MDIO_GLOBAL_INT_MASK,
				       IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				       reg);
	if (status)
		return status;

	/* Enable vendor Auto-Neg alarm and Global Interrupt Mask 1 alarm */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_INT_CHIP_VEN_MASK,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);
	if (status)
		return status;

	reg |= (IXGBE_MDIO_GLOBAL_AN_VEN_ALM_INT_EN |
		IXGBE_MDIO_GLOBAL_ALARM_1_INT);

	status = hw->phy.ops.write_reg(hw, IXGBE_MDIO_GLOBAL_INT_CHIP_VEN_MASK,
				       IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				       reg);
	if (status)
		return status;

	/* Enable chip-wide vendor alarm */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_INT_CHIP_STD_MASK,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);
	if (status)
		return status;

	reg |= IXGBE_MDIO_GLOBAL_VEN_ALM_INT_EN;

	status = hw->phy.ops.write_reg(hw, IXGBE_MDIO_GLOBAL_INT_CHIP_STD_MASK,
				       IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				       reg);

	return status;
}

/**
 * ixgbe_handle_lasi_ext_t_x550em - Handle external Base T PHY interrupt
 * @hw: pointer to hardware structure
 *
 * Handle external Base T PHY interrupt. If high temperature
 * failure alarm then return error, else if link status change
 * then setup internal/external PHY link
 *
 * Return IXGBE_ERR_OVERTEMP if interrupt is high temperature
 * failure alarm, else return PHY access status.
 **/
static s32 ixgbe_handle_lasi_ext_t_x550em(struct ixgbe_hw *hw)
{
	struct ixgbe_phy_info *phy = &hw->phy;
	bool lsc;
	u32 status;

	status = ixgbe_get_lasi_ext_t_x550em(hw, &lsc);
	if (status)
		return status;

	if (lsc && phy->ops.setup_internal_link)
		return phy->ops.setup_internal_link(hw);

	return 0;
}

/**
 * ixgbe_setup_kr_speed_x550em - Configure the KR PHY for link speed.
 * @hw: pointer to hardware structure
 * @speed: link speed
 *
 * Configures the integrated KR PHY.
 **/
static s32 ixgbe_setup_kr_speed_x550em(struct ixgbe_hw *hw,
				       ixgbe_link_speed speed)
{
	s32 status;
	u32 reg_val;

	status = ixgbe_read_iosf_sb_reg_x550(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status)
		return status;

	reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_ENABLE;
	reg_val &= ~(IXGBE_KRM_LINK_CTRL_1_TETH_AN_FEC_REQ |
		     IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_FEC);
	reg_val &= ~(IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_KR |
		     IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_KX);

	/* Advertise 10G support. */
	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_KR;

	/* Advertise 1G support. */
	if (speed & IXGBE_LINK_SPEED_1GB_FULL)
		reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_KX;

	/* Restart auto-negotiation. */
	reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_RESTART;
	status = ixgbe_write_iosf_sb_reg_x550(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);

	return status;
}

/** ixgbe_setup_kx4_x550em - Configure the KX4 PHY.
 *  @hw: pointer to hardware structure
 *
 *   Configures the integrated KX4 PHY.
 **/
static s32 ixgbe_setup_kx4_x550em(struct ixgbe_hw *hw)
{
	s32 status;
	u32 reg_val;

	status = ixgbe_read_iosf_sb_reg_x550(hw, IXGBE_KX4_LINK_CNTL_1,
					     IXGBE_SB_IOSF_TARGET_KX4_PCS0 +
					     hw->bus.lan_id, &reg_val);
	if (status)
		return status;

	reg_val &= ~(IXGBE_KX4_LINK_CNTL_1_TETH_AN_CAP_KX4 |
		     IXGBE_KX4_LINK_CNTL_1_TETH_AN_CAP_KX);

	reg_val |= IXGBE_KX4_LINK_CNTL_1_TETH_AN_ENABLE;

	/* Advertise 10G support. */
	if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_10GB_FULL)
		reg_val |= IXGBE_KX4_LINK_CNTL_1_TETH_AN_CAP_KX4;

	/* Advertise 1G support. */
	if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_1GB_FULL)
		reg_val |= IXGBE_KX4_LINK_CNTL_1_TETH_AN_CAP_KX;

	/* Restart auto-negotiation. */
	reg_val |= IXGBE_KX4_LINK_CNTL_1_TETH_AN_RESTART;
	status = ixgbe_write_iosf_sb_reg_x550(hw, IXGBE_KX4_LINK_CNTL_1,
					      IXGBE_SB_IOSF_TARGET_KX4_PCS0 +
					      hw->bus.lan_id, reg_val);

	return status;
}

/**  ixgbe_setup_kr_x550em - Configure the KR PHY.
 *   @hw: pointer to hardware structure
 *
 *   Configures the integrated KR PHY.
 **/
static s32 ixgbe_setup_kr_x550em(struct ixgbe_hw *hw)
{
	return ixgbe_setup_kr_speed_x550em(hw, hw->phy.autoneg_advertised);
}

/** ixgbe_ext_phy_t_x550em_get_link - Get ext phy link status
 *  @hw: address of hardware structure
 *  @link_up: address of boolean to indicate link status
 *
 *  Returns error code if unable to get link status.
 **/
static s32 ixgbe_ext_phy_t_x550em_get_link(struct ixgbe_hw *hw, bool *link_up)
{
	u32 ret;
	u16 autoneg_status;

	*link_up = false;

	/* read this twice back to back to indicate current status */
	ret = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_STATUS,
				   IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				   &autoneg_status);
	if (ret)
		return ret;

	ret = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_STATUS,
				   IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				   &autoneg_status);
	if (ret)
		return ret;

	*link_up = !!(autoneg_status & IXGBE_MDIO_AUTO_NEG_LINK_STATUS);

	return 0;
}

/** ixgbe_setup_internal_phy_t_x550em - Configure KR PHY to X557 link
 *  @hw: point to hardware structure
 *
 *  Configures the link between the integrated KR PHY and the external X557 PHY
 *  The driver will call this function when it gets a link status change
 *  interrupt from the X557 PHY. This function configures the link speed
 *  between the PHYs to match the link speed of the BASE-T link.
 *
 * A return of a non-zero value indicates an error, and the base driver should
 * not report link up.
 **/
static s32 ixgbe_setup_internal_phy_t_x550em(struct ixgbe_hw *hw)
{
	ixgbe_link_speed force_speed;
	bool link_up;
	u32 status;
	u16 speed;

	if (hw->mac.ops.get_media_type(hw) != ixgbe_media_type_copper)
		return IXGBE_ERR_CONFIG;

	if (hw->phy.nw_mng_if_sel & IXGBE_NW_MNG_IF_SEL_INT_PHY_MODE) {
		speed = IXGBE_LINK_SPEED_10GB_FULL |
			IXGBE_LINK_SPEED_1GB_FULL;
		return ixgbe_setup_kr_speed_x550em(hw, speed);
	}

	/* If link is not up, then there is no setup necessary so return  */
	status = ixgbe_ext_phy_t_x550em_get_link(hw, &link_up);
	if (status)
		return status;

	if (!link_up)
		return 0;

	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_VENDOR_STAT,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      &speed);
	if (status)
		return status;

	/* If link is not still up, then no setup is necessary so return */
	status = ixgbe_ext_phy_t_x550em_get_link(hw, &link_up);
	if (status)
		return status;

	if (!link_up)
		return 0;

	/* clear everything but the speed and duplex bits */
	speed &= IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_MASK;

	switch (speed) {
	case IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_10GB_FULL:
		force_speed = IXGBE_LINK_SPEED_10GB_FULL;
		break;
	case IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_1GB_FULL:
		force_speed = IXGBE_LINK_SPEED_1GB_FULL;
		break;
	default:
		/* Internal PHY does not support anything else */
		return IXGBE_ERR_INVALID_LINK_SETTINGS;
	}

	return ixgbe_setup_ixfi_x550em(hw, &force_speed);
}

/** ixgbe_reset_phy_t_X550em - Performs X557 PHY reset and enables LASI
 *  @hw: pointer to hardware structure
 **/
static s32 ixgbe_reset_phy_t_X550em(struct ixgbe_hw *hw)
{
	s32 status;

	status = ixgbe_reset_phy_generic(hw);

	if (status)
		return status;

	/* Configure Link Status Alarm and Temperature Threshold interrupts */
	return ixgbe_enable_lasi_ext_t_x550em(hw);
}

/** ixgbe_get_lcd_x550em - Determine lowest common denominator
 *  @hw: pointer to hardware structure
 *  @lcd_speed: pointer to lowest common link speed
 *
 *  Determine lowest common link speed with link partner.
 **/
static s32 ixgbe_get_lcd_t_x550em(struct ixgbe_hw *hw,
				  ixgbe_link_speed *lcd_speed)
{
	u16 an_lp_status;
	s32 status;
	u16 word = hw->eeprom.ctrl_word_3;

	*lcd_speed = IXGBE_LINK_SPEED_UNKNOWN;

	status = hw->phy.ops.read_reg(hw, IXGBE_AUTO_NEG_LP_STATUS,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      &an_lp_status);
	if (status)
		return status;

	/* If link partner advertised 1G, return 1G */
	if (an_lp_status & IXGBE_AUTO_NEG_LP_1000BASE_CAP) {
		*lcd_speed = IXGBE_LINK_SPEED_1GB_FULL;
		return status;
	}

	/* If 10G disabled for LPLU via NVM D10GMP, then return no valid LCD */
	if ((hw->bus.lan_id && (word & NVM_INIT_CTRL_3_D10GMP_PORT1)) ||
	    (word & NVM_INIT_CTRL_3_D10GMP_PORT0))
		return status;

	/* Link partner not capable of lower speeds, return 10G */
	*lcd_speed = IXGBE_LINK_SPEED_10GB_FULL;
	return status;
}

/** ixgbe_enter_lplu_x550em - Transition to low power states
 *  @hw: pointer to hardware structure
 *
 *  Configures Low Power Link Up on transition to low power states
 *  (from D0 to non-D0). Link is required to enter LPLU so avoid resetting
 *  the X557 PHY immediately prior to entering LPLU.
 **/
static s32 ixgbe_enter_lplu_t_x550em(struct ixgbe_hw *hw)
{
	u16 an_10g_cntl_reg, autoneg_reg, speed;
	s32 status;
	ixgbe_link_speed lcd_speed;
	u32 save_autoneg;
	bool link_up;

	/* SW LPLU not required on later HW revisions. */
	if (IXGBE_FUSES0_REV1 & IXGBE_READ_REG(hw, IXGBE_FUSES0_GROUP(0)))
		return 0;

	/* If blocked by MNG FW, then don't restart AN */
	if (ixgbe_check_reset_blocked(hw))
		return 0;

	status = ixgbe_ext_phy_t_x550em_get_link(hw, &link_up);
	if (status)
		return status;

	status = hw->eeprom.ops.read(hw, NVM_INIT_CTRL_3,
				     &hw->eeprom.ctrl_word_3);
	if (status)
		return status;

	/* If link is down, LPLU disabled in NVM, WoL disabled, or
	 * manageability disabled, then force link down by entering
	 * low power mode.
	 */
	if (!link_up || !(hw->eeprom.ctrl_word_3 & NVM_INIT_CTRL_3_LPLU) ||
	    !(hw->wol_enabled || ixgbe_mng_present(hw)))
		return ixgbe_set_copper_phy_power(hw, false);

	/* Determine LCD */
	status = ixgbe_get_lcd_t_x550em(hw, &lcd_speed);
	if (status)
		return status;

	/* If no valid LCD link speed, then force link down and exit. */
	if (lcd_speed == IXGBE_LINK_SPEED_UNKNOWN)
		return ixgbe_set_copper_phy_power(hw, false);

	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_VENDOR_STAT,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      &speed);
	if (status)
		return status;

	/* If no link now, speed is invalid so take link down */
	status = ixgbe_ext_phy_t_x550em_get_link(hw, &link_up);
	if (status)
		return ixgbe_set_copper_phy_power(hw, false);

	/* clear everything but the speed bits */
	speed &= IXGBE_MDIO_AUTO_NEG_VEN_STAT_SPEED_MASK;

	/* If current speed is already LCD, then exit. */
	if (((speed == IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_1GB) &&
	     (lcd_speed == IXGBE_LINK_SPEED_1GB_FULL)) ||
	    ((speed == IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_10GB) &&
	     (lcd_speed == IXGBE_LINK_SPEED_10GB_FULL)))
		return status;

	/* Clear AN completed indication */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_VENDOR_TX_ALARM,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      &autoneg_reg);
	if (status)
		return status;

	status = hw->phy.ops.read_reg(hw, IXGBE_MII_10GBASE_T_AUTONEG_CTRL_REG,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      &an_10g_cntl_reg);
	if (status)
		return status;

	status = hw->phy.ops.read_reg(hw,
				      IXGBE_MII_AUTONEG_VENDOR_PROVISION_1_REG,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      &autoneg_reg);
	if (status)
		return status;

	save_autoneg = hw->phy.autoneg_advertised;

	/* Setup link at least common link speed */
	status = hw->mac.ops.setup_link(hw, lcd_speed, false);

	/* restore autoneg from before setting lplu speed */
	hw->phy.autoneg_advertised = save_autoneg;

	return status;
}

/** ixgbe_init_phy_ops_X550em - PHY/SFP specific init
 *  @hw: pointer to hardware structure
 *
 *  Initialize any function pointers that were not able to be
 *  set during init_shared_code because the PHY/SFP type was
 *  not known.  Perform the SFP init if necessary.
 **/
static s32 ixgbe_init_phy_ops_X550em(struct ixgbe_hw *hw)
{
	struct ixgbe_phy_info *phy = &hw->phy;
	s32 ret_val;

	hw->mac.ops.set_lan_id(hw);

	if (hw->mac.ops.get_media_type(hw) == ixgbe_media_type_fiber) {
		phy->phy_semaphore_mask = IXGBE_GSSR_SHARED_I2C_SM;
		ixgbe_setup_mux_ctl(hw);

		/* Save NW management interface connected on board. This is used
		 * to determine internal PHY mode.
		 */
		phy->nw_mng_if_sel = IXGBE_READ_REG(hw, IXGBE_NW_MNG_IF_SEL);
	}

	/* Identify the PHY or SFP module */
	ret_val = phy->ops.identify(hw);

	/* Setup function pointers based on detected hardware */
	ixgbe_init_mac_link_ops_X550em(hw);
	if (phy->sfp_type != ixgbe_sfp_type_unknown)
		phy->ops.reset = NULL;

	/* Set functions pointers based on phy type */
	switch (hw->phy.type) {
	case ixgbe_phy_x550em_kx4:
		phy->ops.setup_link = ixgbe_setup_kx4_x550em;
		phy->ops.read_reg = ixgbe_read_phy_reg_x550em;
		phy->ops.write_reg = ixgbe_write_phy_reg_x550em;
		break;
	case ixgbe_phy_x550em_kr:
		phy->ops.setup_link = ixgbe_setup_kr_x550em;
		phy->ops.read_reg = ixgbe_read_phy_reg_x550em;
		phy->ops.write_reg = ixgbe_write_phy_reg_x550em;
		break;
	case ixgbe_phy_x550em_ext_t:
		/* Save NW management interface connected on board. This is used
		 * to determine internal PHY mode
		 */
		phy->nw_mng_if_sel = IXGBE_READ_REG(hw, IXGBE_NW_MNG_IF_SEL);

		/* If internal link mode is XFI, then setup iXFI internal link,
		 * else setup KR now.
		 */
		phy->ops.setup_internal_link =
					      ixgbe_setup_internal_phy_t_x550em;

		/* setup SW LPLU only for first revision */
		if (!(IXGBE_FUSES0_REV1 & IXGBE_READ_REG(hw,
							IXGBE_FUSES0_GROUP(0))))
			phy->ops.enter_lplu = ixgbe_enter_lplu_t_x550em;

		phy->ops.handle_lasi = ixgbe_handle_lasi_ext_t_x550em;
		phy->ops.reset = ixgbe_reset_phy_t_X550em;
		break;
	default:
		break;
	}

	return ret_val;
}

/** ixgbe_get_media_type_X550em - Get media type
 *  @hw: pointer to hardware structure
 *
 *  Returns the media type (fiber, copper, backplane)
 *
 */
static enum ixgbe_media_type ixgbe_get_media_type_X550em(struct ixgbe_hw *hw)
{
	enum ixgbe_media_type media_type;

	/* Detect if there is a copper PHY attached. */
	switch (hw->device_id) {
	case IXGBE_DEV_ID_X550EM_X_KR:
	case IXGBE_DEV_ID_X550EM_X_KX4:
		media_type = ixgbe_media_type_backplane;
		break;
	case IXGBE_DEV_ID_X550EM_X_SFP:
		media_type = ixgbe_media_type_fiber;
		break;
	case IXGBE_DEV_ID_X550EM_X_1G_T:
	case IXGBE_DEV_ID_X550EM_X_10G_T:
		 media_type = ixgbe_media_type_copper;
		break;
	default:
		media_type = ixgbe_media_type_unknown;
		break;
	}
	return media_type;
}

/** ixgbe_init_ext_t_x550em - Start (unstall) the external Base T PHY.
 ** @hw: pointer to hardware structure
 **/
static s32 ixgbe_init_ext_t_x550em(struct ixgbe_hw *hw)
{
	s32 status;
	u16 reg;

	status = hw->phy.ops.read_reg(hw,
				      IXGBE_MDIO_TX_VENDOR_ALARMS_3,
				      IXGBE_MDIO_PMA_PMD_DEV_TYPE,
				      &reg);
	if (status)
		return status;

	/* If PHY FW reset completed bit is set then this is the first
	 * SW instance after a power on so the PHY FW must be un-stalled.
	 */
	if (reg & IXGBE_MDIO_TX_VENDOR_ALARMS_3_RST_MASK) {
		status = hw->phy.ops.read_reg(hw,
					IXGBE_MDIO_GLOBAL_RES_PR_10,
					IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
					&reg);
		if (status)
			return status;

		reg &= ~IXGBE_MDIO_POWER_UP_STALL;

		status = hw->phy.ops.write_reg(hw,
					IXGBE_MDIO_GLOBAL_RES_PR_10,
					IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
					reg);
		if (status)
			return status;
	}

	return status;
}

/**  ixgbe_reset_hw_X550em - Perform hardware reset
 **  @hw: pointer to hardware structure
 **
 **  Resets the hardware by resetting the transmit and receive units, masks
 **  and clears all interrupts, perform a PHY reset, and perform a link (MAC)
 **  reset.
 **/
static s32 ixgbe_reset_hw_X550em(struct ixgbe_hw *hw)
{
	ixgbe_link_speed link_speed;
	s32 status;
	u32 ctrl = 0;
	u32 i;
	u32 hlreg0;
	bool link_up = false;

	/* Call adapter stop to disable Tx/Rx and clear interrupts */
	status = hw->mac.ops.stop_adapter(hw);
	if (status)
		return status;

	/* flush pending Tx transactions */
	ixgbe_clear_tx_pending(hw);

	/* PHY ops must be identified and initialized prior to reset */

	/* Identify PHY and related function pointers */
	status = hw->phy.ops.init(hw);

	/* start the external PHY */
	if (hw->phy.type == ixgbe_phy_x550em_ext_t) {
		status = ixgbe_init_ext_t_x550em(hw);
		if (status)
			return status;
	}

	/* Setup SFP module if there is one present. */
	if (hw->phy.sfp_setup_needed) {
		status = hw->mac.ops.setup_sfp(hw);
		hw->phy.sfp_setup_needed = false;
	}

	/* Reset PHY */
	if (!hw->phy.reset_disable && hw->phy.ops.reset)
		hw->phy.ops.reset(hw);

mac_reset_top:
	/* Issue global reset to the MAC.  Needs to be SW reset if link is up.
	 * If link reset is used when link is up, it might reset the PHY when
	 * mng is using it.  If link is down or the flag to force full link
	 * reset is set, then perform link reset.
	 */
	ctrl = IXGBE_CTRL_LNK_RST;

	if (!hw->force_full_reset) {
		hw->mac.ops.check_link(hw, &link_speed, &link_up, false);
		if (link_up)
			ctrl = IXGBE_CTRL_RST;
	}

	ctrl |= IXGBE_READ_REG(hw, IXGBE_CTRL);
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, ctrl);
	IXGBE_WRITE_FLUSH(hw);
	usleep_range(1000, 1200);

	/* Poll for reset bit to self-clear meaning reset is complete */
	for (i = 0; i < 10; i++) {
		ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
		if (!(ctrl & IXGBE_CTRL_RST_MASK))
			break;
		udelay(1);
	}

	if (ctrl & IXGBE_CTRL_RST_MASK) {
		status = IXGBE_ERR_RESET_FAILED;
		hw_dbg(hw, "Reset polling failed to complete.\n");
	}

	msleep(50);

	/* Double resets are required for recovery from certain error
	 * clear the multicast table.  Also reset num_rar_entries to 128,
	 * since we modify this value when programming the SAN MAC address.
	 */
	if (hw->mac.flags & IXGBE_FLAGS_DOUBLE_RESET_REQUIRED) {
		hw->mac.flags &= ~IXGBE_FLAGS_DOUBLE_RESET_REQUIRED;
		goto mac_reset_top;
	}

	/* Store the permanent mac address */
	hw->mac.ops.get_mac_addr(hw, hw->mac.perm_addr);

	/* Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to 128,
	 * since we modify this value when programming the SAN MAC address.
	 */
	hw->mac.num_rar_entries = 128;
	hw->mac.ops.init_rx_addrs(hw);

	if (hw->device_id == IXGBE_DEV_ID_X550EM_X_10G_T) {
		hlreg0 = IXGBE_READ_REG(hw, IXGBE_HLREG0);
		hlreg0 &= ~IXGBE_HLREG0_MDCSPD;
		IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg0);
	}

	if (hw->device_id == IXGBE_DEV_ID_X550EM_X_SFP)
		ixgbe_setup_mux_ctl(hw);

	return status;
}

/** ixgbe_set_ethertype_anti_spoofing_X550 - Enable/Disable Ethertype
 *	anti-spoofing
 *  @hw:  pointer to hardware structure
 *  @enable: enable or disable switch for Ethertype anti-spoofing
 *  @vf: Virtual Function pool - VF Pool to set for Ethertype anti-spoofing
 **/
static void ixgbe_set_ethertype_anti_spoofing_X550(struct ixgbe_hw *hw,
						   bool enable, int vf)
{
	int vf_target_reg = vf >> 3;
	int vf_target_shift = vf % 8 + IXGBE_SPOOF_ETHERTYPEAS_SHIFT;
	u32 pfvfspoof;

	pfvfspoof = IXGBE_READ_REG(hw, IXGBE_PFVFSPOOF(vf_target_reg));
	if (enable)
		pfvfspoof |= (1 << vf_target_shift);
	else
		pfvfspoof &= ~(1 << vf_target_shift);

	IXGBE_WRITE_REG(hw, IXGBE_PFVFSPOOF(vf_target_reg), pfvfspoof);
}

/** ixgbe_set_source_address_pruning_X550 - Enable/Disbale src address pruning
 *  @hw: pointer to hardware structure
 *  @enable: enable or disable source address pruning
 *  @pool: Rx pool to set source address pruning for
 **/
static void ixgbe_set_source_address_pruning_X550(struct ixgbe_hw *hw,
						  bool enable,
						  unsigned int pool)
{
	u64 pfflp;

	/* max rx pool is 63 */
	if (pool > 63)
		return;

	pfflp = (u64)IXGBE_READ_REG(hw, IXGBE_PFFLPL);
	pfflp |= (u64)IXGBE_READ_REG(hw, IXGBE_PFFLPH) << 32;

	if (enable)
		pfflp |= (1ULL << pool);
	else
		pfflp &= ~(1ULL << pool);

	IXGBE_WRITE_REG(hw, IXGBE_PFFLPL, (u32)pfflp);
	IXGBE_WRITE_REG(hw, IXGBE_PFFLPH, (u32)(pfflp >> 32));
}

/**
 * ixgbe_set_mux - Set mux for port 1 access with CS4227
 * @hw: pointer to hardware structure
 * @state: set mux if 1, clear if 0
 */
static void ixgbe_set_mux(struct ixgbe_hw *hw, u8 state)
{
	u32 esdp;

	if (!hw->bus.lan_id)
		return;
	esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
	if (state)
		esdp |= IXGBE_ESDP_SDP1;
	else
		esdp &= ~IXGBE_ESDP_SDP1;
	IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 * ixgbe_acquire_swfw_sync_X550em - Acquire SWFW semaphore
 * @hw: pointer to hardware structure
 * @mask: Mask to specify which semaphore to acquire
 *
 * Acquires the SWFW semaphore and sets the I2C MUX
 */
static s32 ixgbe_acquire_swfw_sync_X550em(struct ixgbe_hw *hw, u32 mask)
{
	s32 status;

	status = ixgbe_acquire_swfw_sync_X540(hw, mask);
	if (status)
		return status;

	if (mask & IXGBE_GSSR_I2C_MASK)
		ixgbe_set_mux(hw, 1);

	return 0;
}

/**
 * ixgbe_release_swfw_sync_X550em - Release SWFW semaphore
 * @hw: pointer to hardware structure
 * @mask: Mask to specify which semaphore to release
 *
 * Releases the SWFW semaphore and sets the I2C MUX
 */
static void ixgbe_release_swfw_sync_X550em(struct ixgbe_hw *hw, u32 mask)
{
	if (mask & IXGBE_GSSR_I2C_MASK)
		ixgbe_set_mux(hw, 0);

	ixgbe_release_swfw_sync_X540(hw, mask);
}

#define X550_COMMON_MAC \
	.init_hw			= &ixgbe_init_hw_generic, \
	.start_hw			= &ixgbe_start_hw_X540, \
	.clear_hw_cntrs			= &ixgbe_clear_hw_cntrs_generic, \
	.enable_rx_dma			= &ixgbe_enable_rx_dma_generic, \
	.get_mac_addr			= &ixgbe_get_mac_addr_generic, \
	.get_device_caps		= &ixgbe_get_device_caps_generic, \
	.stop_adapter			= &ixgbe_stop_adapter_generic, \
	.set_lan_id			= &ixgbe_set_lan_id_multi_port_pcie, \
	.read_analog_reg8		= NULL, \
	.write_analog_reg8		= NULL, \
	.set_rxpba			= &ixgbe_set_rxpba_generic, \
	.check_link			= &ixgbe_check_mac_link_generic, \
	.led_on				= &ixgbe_led_on_generic, \
	.led_off			= &ixgbe_led_off_generic, \
	.blink_led_start		= &ixgbe_blink_led_start_X540, \
	.blink_led_stop			= &ixgbe_blink_led_stop_X540, \
	.set_rar			= &ixgbe_set_rar_generic, \
	.clear_rar			= &ixgbe_clear_rar_generic, \
	.set_vmdq			= &ixgbe_set_vmdq_generic, \
	.set_vmdq_san_mac		= &ixgbe_set_vmdq_san_mac_generic, \
	.clear_vmdq			= &ixgbe_clear_vmdq_generic, \
	.init_rx_addrs			= &ixgbe_init_rx_addrs_generic, \
	.update_mc_addr_list		= &ixgbe_update_mc_addr_list_generic, \
	.enable_mc			= &ixgbe_enable_mc_generic, \
	.disable_mc			= &ixgbe_disable_mc_generic, \
	.clear_vfta			= &ixgbe_clear_vfta_generic, \
	.set_vfta			= &ixgbe_set_vfta_generic, \
	.fc_enable			= &ixgbe_fc_enable_generic, \
	.set_fw_drv_ver			= &ixgbe_set_fw_drv_ver_generic, \
	.init_uta_tables		= &ixgbe_init_uta_tables_generic, \
	.set_mac_anti_spoofing		= &ixgbe_set_mac_anti_spoofing, \
	.set_vlan_anti_spoofing		= &ixgbe_set_vlan_anti_spoofing, \
	.set_source_address_pruning	= \
				&ixgbe_set_source_address_pruning_X550, \
	.set_ethertype_anti_spoofing	= \
				&ixgbe_set_ethertype_anti_spoofing_X550, \
	.disable_rx_buff		= &ixgbe_disable_rx_buff_generic, \
	.enable_rx_buff			= &ixgbe_enable_rx_buff_generic, \
	.get_thermal_sensor_data	= NULL, \
	.init_thermal_sensor_thresh	= NULL, \
	.prot_autoc_read		= &prot_autoc_read_generic, \
	.prot_autoc_write		= &prot_autoc_write_generic, \
	.enable_rx			= &ixgbe_enable_rx_generic, \
	.disable_rx			= &ixgbe_disable_rx_x550, \

static struct ixgbe_mac_operations mac_ops_X550 = {
	X550_COMMON_MAC
	.reset_hw		= &ixgbe_reset_hw_X540,
	.get_media_type		= &ixgbe_get_media_type_X540,
	.get_san_mac_addr	= &ixgbe_get_san_mac_addr_generic,
	.get_wwn_prefix		= &ixgbe_get_wwn_prefix_generic,
	.setup_link		= &ixgbe_setup_mac_link_X540,
	.get_link_capabilities	= &ixgbe_get_copper_link_capabilities_generic,
	.get_bus_info		= &ixgbe_get_bus_info_generic,
	.setup_sfp		= NULL,
	.acquire_swfw_sync	= &ixgbe_acquire_swfw_sync_X540,
	.release_swfw_sync	= &ixgbe_release_swfw_sync_X540,
};

static struct ixgbe_mac_operations mac_ops_X550EM_x = {
	X550_COMMON_MAC
	.reset_hw		= &ixgbe_reset_hw_X550em,
	.get_media_type		= &ixgbe_get_media_type_X550em,
	.get_san_mac_addr	= NULL,
	.get_wwn_prefix		= NULL,
	.setup_link		= NULL, /* defined later */
	.get_link_capabilities	= &ixgbe_get_link_capabilities_X550em,
	.get_bus_info		= &ixgbe_get_bus_info_X550em,
	.setup_sfp		= ixgbe_setup_sfp_modules_X550em,
	.acquire_swfw_sync	= &ixgbe_acquire_swfw_sync_X550em,
	.release_swfw_sync	= &ixgbe_release_swfw_sync_X550em,
};

#define X550_COMMON_EEP \
	.read			= &ixgbe_read_ee_hostif_X550, \
	.read_buffer		= &ixgbe_read_ee_hostif_buffer_X550, \
	.write			= &ixgbe_write_ee_hostif_X550, \
	.write_buffer		= &ixgbe_write_ee_hostif_buffer_X550, \
	.validate_checksum	= &ixgbe_validate_eeprom_checksum_X550, \
	.update_checksum	= &ixgbe_update_eeprom_checksum_X550, \
	.calc_checksum		= &ixgbe_calc_eeprom_checksum_X550, \

static struct ixgbe_eeprom_operations eeprom_ops_X550 = {
	X550_COMMON_EEP
	.init_params		= &ixgbe_init_eeprom_params_X550,
};

static struct ixgbe_eeprom_operations eeprom_ops_X550EM_x = {
	X550_COMMON_EEP
	.init_params		= &ixgbe_init_eeprom_params_X540,
};

#define X550_COMMON_PHY	\
	.identify_sfp		= &ixgbe_identify_module_generic, \
	.reset			= NULL, \
	.setup_link_speed	= &ixgbe_setup_phy_link_speed_generic, \
	.read_i2c_byte		= &ixgbe_read_i2c_byte_generic, \
	.write_i2c_byte		= &ixgbe_write_i2c_byte_generic, \
	.read_i2c_sff8472	= &ixgbe_read_i2c_sff8472_generic, \
	.read_i2c_eeprom	= &ixgbe_read_i2c_eeprom_generic, \
	.write_i2c_eeprom	= &ixgbe_write_i2c_eeprom_generic, \
	.read_reg		= &ixgbe_read_phy_reg_generic, \
	.write_reg		= &ixgbe_write_phy_reg_generic, \
	.setup_link		= &ixgbe_setup_phy_link_generic, \
	.set_phy_power		= NULL, \
	.check_overtemp		= &ixgbe_tn_check_overtemp, \
	.get_firmware_version	= &ixgbe_get_phy_firmware_version_generic,

static struct ixgbe_phy_operations phy_ops_X550 = {
	X550_COMMON_PHY
	.init			= NULL,
	.identify		= &ixgbe_identify_phy_generic,
};

static struct ixgbe_phy_operations phy_ops_X550EM_x = {
	X550_COMMON_PHY
	.init			= &ixgbe_init_phy_ops_X550em,
	.identify		= &ixgbe_identify_phy_x550em,
	.read_i2c_combined	= &ixgbe_read_i2c_combined_generic,
	.write_i2c_combined	= &ixgbe_write_i2c_combined_generic,
	.read_i2c_combined_unlocked = &ixgbe_read_i2c_combined_generic_unlocked,
	.write_i2c_combined_unlocked =
				     &ixgbe_write_i2c_combined_generic_unlocked,
};

static const u32 ixgbe_mvals_X550[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(X550)
};

static const u32 ixgbe_mvals_X550EM_x[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(X550EM_x)
};

struct ixgbe_info ixgbe_X550_info = {
	.mac			= ixgbe_mac_X550,
	.get_invariants		= &ixgbe_get_invariants_X540,
	.mac_ops		= &mac_ops_X550,
	.eeprom_ops		= &eeprom_ops_X550,
	.phy_ops		= &phy_ops_X550,
	.mbx_ops		= &mbx_ops_generic,
	.mvals			= ixgbe_mvals_X550,
};

struct ixgbe_info ixgbe_X550EM_x_info = {
	.mac			= ixgbe_mac_X550EM_x,
	.get_invariants		= &ixgbe_get_invariants_X550_x,
	.mac_ops		= &mac_ops_X550EM_x,
	.eeprom_ops		= &eeprom_ops_X550EM_x,
	.phy_ops		= &phy_ops_X550EM_x,
	.mbx_ops		= &mbx_ops_generic,
	.mvals			= ixgbe_mvals_X550EM_x,
};
