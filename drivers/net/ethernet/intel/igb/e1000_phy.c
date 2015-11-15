/* Intel(R) Gigabit Ethernet Linux driver
 * Copyright(c) 2007-2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include <linux/if_ether.h>
#include <linux/delay.h>

#include "e1000_mac.h"
#include "e1000_phy.h"

static s32  igb_phy_setup_autoneg(struct e1000_hw *hw);
static void igb_phy_force_speed_duplex_setup(struct e1000_hw *hw,
					     u16 *phy_ctrl);
static s32  igb_wait_autoneg(struct e1000_hw *hw);
static s32  igb_set_master_slave_mode(struct e1000_hw *hw);

/* Cable length tables */
static const u16 e1000_m88_cable_length_table[] = {
	0, 50, 80, 110, 140, 140, E1000_CABLE_LENGTH_UNDEFINED };

static const u16 e1000_igp_2_cable_length_table[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 3, 5, 8, 11, 13, 16, 18, 21,
	0, 0, 0, 3, 6, 10, 13, 16, 19, 23, 26, 29, 32, 35, 38, 41,
	6, 10, 14, 18, 22, 26, 30, 33, 37, 41, 44, 48, 51, 54, 58, 61,
	21, 26, 31, 35, 40, 44, 49, 53, 57, 61, 65, 68, 72, 75, 79, 82,
	40, 45, 51, 56, 61, 66, 70, 75, 79, 83, 87, 91, 94, 98, 101, 104,
	60, 66, 72, 77, 82, 87, 92, 96, 100, 104, 108, 111, 114, 117, 119, 121,
	83, 89, 95, 100, 105, 109, 113, 116, 119, 122, 124,
	104, 109, 114, 118, 121, 124};

/**
 *  igb_check_reset_block - Check if PHY reset is blocked
 *  @hw: pointer to the HW structure
 *
 *  Read the PHY management control register and check whether a PHY reset
 *  is blocked.  If a reset is not blocked return 0, otherwise
 *  return E1000_BLK_PHY_RESET (12).
 **/
s32 igb_check_reset_block(struct e1000_hw *hw)
{
	u32 manc;

	manc = rd32(E1000_MANC);

	return (manc & E1000_MANC_BLK_PHY_RST_ON_IDE) ? E1000_BLK_PHY_RESET : 0;
}

/**
 *  igb_get_phy_id - Retrieve the PHY ID and revision
 *  @hw: pointer to the HW structure
 *
 *  Reads the PHY registers and stores the PHY ID and possibly the PHY
 *  revision in the hardware structure.
 **/
s32 igb_get_phy_id(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = 0;
	u16 phy_id;

	ret_val = phy->ops.read_reg(hw, PHY_ID1, &phy_id);
	if (ret_val)
		goto out;

	phy->id = (u32)(phy_id << 16);
	udelay(20);
	ret_val = phy->ops.read_reg(hw, PHY_ID2, &phy_id);
	if (ret_val)
		goto out;

	phy->id |= (u32)(phy_id & PHY_REVISION_MASK);
	phy->revision = (u32)(phy_id & ~PHY_REVISION_MASK);

out:
	return ret_val;
}

/**
 *  igb_phy_reset_dsp - Reset PHY DSP
 *  @hw: pointer to the HW structure
 *
 *  Reset the digital signal processor.
 **/
static s32 igb_phy_reset_dsp(struct e1000_hw *hw)
{
	s32 ret_val = 0;

	if (!(hw->phy.ops.write_reg))
		goto out;

	ret_val = hw->phy.ops.write_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xC1);
	if (ret_val)
		goto out;

	ret_val = hw->phy.ops.write_reg(hw, M88E1000_PHY_GEN_CONTROL, 0);

out:
	return ret_val;
}

/**
 *  igb_read_phy_reg_mdic - Read MDI control register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the MDI control regsiter in the PHY at offset and stores the
 *  information read to data.
 **/
s32 igb_read_phy_reg_mdic(struct e1000_hw *hw, u32 offset, u16 *data)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 i, mdic = 0;
	s32 ret_val = 0;

	if (offset > MAX_PHY_REG_ADDRESS) {
		hw_dbg("PHY Address %d is out of range\n", offset);
		ret_val = -E1000_ERR_PARAM;
		goto out;
	}

	/* Set up Op-code, Phy Address, and register offset in the MDI
	 * Control register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	mdic = ((offset << E1000_MDIC_REG_SHIFT) |
		(phy->addr << E1000_MDIC_PHY_SHIFT) |
		(E1000_MDIC_OP_READ));

	wr32(E1000_MDIC, mdic);

	/* Poll the ready bit to see if the MDI read completed
	 * Increasing the time out as testing showed failures with
	 * the lower time out
	 */
	for (i = 0; i < (E1000_GEN_POLL_TIMEOUT * 3); i++) {
		udelay(50);
		mdic = rd32(E1000_MDIC);
		if (mdic & E1000_MDIC_READY)
			break;
	}
	if (!(mdic & E1000_MDIC_READY)) {
		hw_dbg("MDI Read did not complete\n");
		ret_val = -E1000_ERR_PHY;
		goto out;
	}
	if (mdic & E1000_MDIC_ERROR) {
		hw_dbg("MDI Error\n");
		ret_val = -E1000_ERR_PHY;
		goto out;
	}
	*data = (u16) mdic;

out:
	return ret_val;
}

/**
 *  igb_write_phy_reg_mdic - Write MDI control register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write to register at offset
 *
 *  Writes data to MDI control register in the PHY at offset.
 **/
s32 igb_write_phy_reg_mdic(struct e1000_hw *hw, u32 offset, u16 data)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 i, mdic = 0;
	s32 ret_val = 0;

	if (offset > MAX_PHY_REG_ADDRESS) {
		hw_dbg("PHY Address %d is out of range\n", offset);
		ret_val = -E1000_ERR_PARAM;
		goto out;
	}

	/* Set up Op-code, Phy Address, and register offset in the MDI
	 * Control register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	mdic = (((u32)data) |
		(offset << E1000_MDIC_REG_SHIFT) |
		(phy->addr << E1000_MDIC_PHY_SHIFT) |
		(E1000_MDIC_OP_WRITE));

	wr32(E1000_MDIC, mdic);

	/* Poll the ready bit to see if the MDI read completed
	 * Increasing the time out as testing showed failures with
	 * the lower time out
	 */
	for (i = 0; i < (E1000_GEN_POLL_TIMEOUT * 3); i++) {
		udelay(50);
		mdic = rd32(E1000_MDIC);
		if (mdic & E1000_MDIC_READY)
			break;
	}
	if (!(mdic & E1000_MDIC_READY)) {
		hw_dbg("MDI Write did not complete\n");
		ret_val = -E1000_ERR_PHY;
		goto out;
	}
	if (mdic & E1000_MDIC_ERROR) {
		hw_dbg("MDI Error\n");
		ret_val = -E1000_ERR_PHY;
		goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_read_phy_reg_i2c - Read PHY register using i2c
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the PHY register at offset using the i2c interface and stores the
 *  retrieved information in data.
 **/
s32 igb_read_phy_reg_i2c(struct e1000_hw *hw, u32 offset, u16 *data)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 i, i2ccmd = 0;

	/* Set up Op-code, Phy Address, and register address in the I2CCMD
	 * register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	i2ccmd = ((offset << E1000_I2CCMD_REG_ADDR_SHIFT) |
		  (phy->addr << E1000_I2CCMD_PHY_ADDR_SHIFT) |
		  (E1000_I2CCMD_OPCODE_READ));

	wr32(E1000_I2CCMD, i2ccmd);

	/* Poll the ready bit to see if the I2C read completed */
	for (i = 0; i < E1000_I2CCMD_PHY_TIMEOUT; i++) {
		udelay(50);
		i2ccmd = rd32(E1000_I2CCMD);
		if (i2ccmd & E1000_I2CCMD_READY)
			break;
	}
	if (!(i2ccmd & E1000_I2CCMD_READY)) {
		hw_dbg("I2CCMD Read did not complete\n");
		return -E1000_ERR_PHY;
	}
	if (i2ccmd & E1000_I2CCMD_ERROR) {
		hw_dbg("I2CCMD Error bit set\n");
		return -E1000_ERR_PHY;
	}

	/* Need to byte-swap the 16-bit value. */
	*data = ((i2ccmd >> 8) & 0x00FF) | ((i2ccmd << 8) & 0xFF00);

	return 0;
}

/**
 *  igb_write_phy_reg_i2c - Write PHY register using i2c
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Writes the data to PHY register at the offset using the i2c interface.
 **/
s32 igb_write_phy_reg_i2c(struct e1000_hw *hw, u32 offset, u16 data)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 i, i2ccmd = 0;
	u16 phy_data_swapped;

	/* Prevent overwritting SFP I2C EEPROM which is at A0 address.*/
	if ((hw->phy.addr == 0) || (hw->phy.addr > 7)) {
		hw_dbg("PHY I2C Address %d is out of range.\n",
			  hw->phy.addr);
		return -E1000_ERR_CONFIG;
	}

	/* Swap the data bytes for the I2C interface */
	phy_data_swapped = ((data >> 8) & 0x00FF) | ((data << 8) & 0xFF00);

	/* Set up Op-code, Phy Address, and register address in the I2CCMD
	 * register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	i2ccmd = ((offset << E1000_I2CCMD_REG_ADDR_SHIFT) |
		  (phy->addr << E1000_I2CCMD_PHY_ADDR_SHIFT) |
		  E1000_I2CCMD_OPCODE_WRITE |
		  phy_data_swapped);

	wr32(E1000_I2CCMD, i2ccmd);

	/* Poll the ready bit to see if the I2C read completed */
	for (i = 0; i < E1000_I2CCMD_PHY_TIMEOUT; i++) {
		udelay(50);
		i2ccmd = rd32(E1000_I2CCMD);
		if (i2ccmd & E1000_I2CCMD_READY)
			break;
	}
	if (!(i2ccmd & E1000_I2CCMD_READY)) {
		hw_dbg("I2CCMD Write did not complete\n");
		return -E1000_ERR_PHY;
	}
	if (i2ccmd & E1000_I2CCMD_ERROR) {
		hw_dbg("I2CCMD Error bit set\n");
		return -E1000_ERR_PHY;
	}

	return 0;
}

/**
 *  igb_read_sfp_data_byte - Reads SFP module data.
 *  @hw: pointer to the HW structure
 *  @offset: byte location offset to be read
 *  @data: read data buffer pointer
 *
 *  Reads one byte from SFP module data stored
 *  in SFP resided EEPROM memory or SFP diagnostic area.
 *  Function should be called with
 *  E1000_I2CCMD_SFP_DATA_ADDR(<byte offset>) for SFP module database access
 *  E1000_I2CCMD_SFP_DIAG_ADDR(<byte offset>) for SFP diagnostics parameters
 *  access
 **/
s32 igb_read_sfp_data_byte(struct e1000_hw *hw, u16 offset, u8 *data)
{
	u32 i = 0;
	u32 i2ccmd = 0;
	u32 data_local = 0;

	if (offset > E1000_I2CCMD_SFP_DIAG_ADDR(255)) {
		hw_dbg("I2CCMD command address exceeds upper limit\n");
		return -E1000_ERR_PHY;
	}

	/* Set up Op-code, EEPROM Address,in the I2CCMD
	 * register. The MAC will take care of interfacing with the
	 * EEPROM to retrieve the desired data.
	 */
	i2ccmd = ((offset << E1000_I2CCMD_REG_ADDR_SHIFT) |
		  E1000_I2CCMD_OPCODE_READ);

	wr32(E1000_I2CCMD, i2ccmd);

	/* Poll the ready bit to see if the I2C read completed */
	for (i = 0; i < E1000_I2CCMD_PHY_TIMEOUT; i++) {
		udelay(50);
		data_local = rd32(E1000_I2CCMD);
		if (data_local & E1000_I2CCMD_READY)
			break;
	}
	if (!(data_local & E1000_I2CCMD_READY)) {
		hw_dbg("I2CCMD Read did not complete\n");
		return -E1000_ERR_PHY;
	}
	if (data_local & E1000_I2CCMD_ERROR) {
		hw_dbg("I2CCMD Error bit set\n");
		return -E1000_ERR_PHY;
	}
	*data = (u8) data_local & 0xFF;

	return 0;
}

/**
 *  igb_read_phy_reg_igp - Read igp PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Acquires semaphore, if necessary, then reads the PHY register at offset
 *  and storing the retrieved information in data.  Release any acquired
 *  semaphores before exiting.
 **/
s32 igb_read_phy_reg_igp(struct e1000_hw *hw, u32 offset, u16 *data)
{
	s32 ret_val = 0;

	if (!(hw->phy.ops.acquire))
		goto out;

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	if (offset > MAX_PHY_MULTI_PAGE_REG) {
		ret_val = igb_write_phy_reg_mdic(hw,
						 IGP01E1000_PHY_PAGE_SELECT,
						 (u16)offset);
		if (ret_val) {
			hw->phy.ops.release(hw);
			goto out;
		}
	}

	ret_val = igb_read_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS & offset,
					data);

	hw->phy.ops.release(hw);

out:
	return ret_val;
}

/**
 *  igb_write_phy_reg_igp - Write igp PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Acquires semaphore, if necessary, then writes the data to PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
s32 igb_write_phy_reg_igp(struct e1000_hw *hw, u32 offset, u16 data)
{
	s32 ret_val = 0;

	if (!(hw->phy.ops.acquire))
		goto out;

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	if (offset > MAX_PHY_MULTI_PAGE_REG) {
		ret_val = igb_write_phy_reg_mdic(hw,
						 IGP01E1000_PHY_PAGE_SELECT,
						 (u16)offset);
		if (ret_val) {
			hw->phy.ops.release(hw);
			goto out;
		}
	}

	ret_val = igb_write_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS & offset,
					 data);

	hw->phy.ops.release(hw);

out:
	return ret_val;
}

/**
 *  igb_copper_link_setup_82580 - Setup 82580 PHY for copper link
 *  @hw: pointer to the HW structure
 *
 *  Sets up Carrier-sense on Transmit and downshift values.
 **/
s32 igb_copper_link_setup_82580(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data;

	if (phy->reset_disable) {
		ret_val = 0;
		goto out;
	}

	if (phy->type == e1000_phy_82580) {
		ret_val = hw->phy.ops.reset(hw);
		if (ret_val) {
			hw_dbg("Error resetting the PHY.\n");
			goto out;
		}
	}

	/* Enable CRS on TX. This must be set for half-duplex operation. */
	ret_val = phy->ops.read_reg(hw, I82580_CFG_REG, &phy_data);
	if (ret_val)
		goto out;

	phy_data |= I82580_CFG_ASSERT_CRS_ON_TX;

	/* Enable downshift */
	phy_data |= I82580_CFG_ENABLE_DOWNSHIFT;

	ret_val = phy->ops.write_reg(hw, I82580_CFG_REG, phy_data);
	if (ret_val)
		goto out;

	/* Set MDI/MDIX mode */
	ret_val = phy->ops.read_reg(hw, I82580_PHY_CTRL_2, &phy_data);
	if (ret_val)
		goto out;
	phy_data &= ~I82580_PHY_CTRL2_MDIX_CFG_MASK;
	/* Options:
	 *   0 - Auto (default)
	 *   1 - MDI mode
	 *   2 - MDI-X mode
	 */
	switch (hw->phy.mdix) {
	case 1:
		break;
	case 2:
		phy_data |= I82580_PHY_CTRL2_MANUAL_MDIX;
		break;
	case 0:
	default:
		phy_data |= I82580_PHY_CTRL2_AUTO_MDI_MDIX;
		break;
	}
	ret_val = hw->phy.ops.write_reg(hw, I82580_PHY_CTRL_2, phy_data);

out:
	return ret_val;
}

/**
 *  igb_copper_link_setup_m88 - Setup m88 PHY's for copper link
 *  @hw: pointer to the HW structure
 *
 *  Sets up MDI/MDI-X and polarity for m88 PHY's.  If necessary, transmit clock
 *  and downshift values are set also.
 **/
s32 igb_copper_link_setup_m88(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data;

	if (phy->reset_disable) {
		ret_val = 0;
		goto out;
	}

	/* Enable CRS on TX. This must be set for half-duplex operation. */
	ret_val = phy->ops.read_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		goto out;

	phy_data |= M88E1000_PSCR_ASSERT_CRS_ON_TX;

	/* Options:
	 *   MDI/MDI-X = 0 (default)
	 *   0 - Auto for all speeds
	 *   1 - MDI mode
	 *   2 - MDI-X mode
	 *   3 - Auto for 1000Base-T only (MDI-X for 10/100Base-T modes)
	 */
	phy_data &= ~M88E1000_PSCR_AUTO_X_MODE;

	switch (phy->mdix) {
	case 1:
		phy_data |= M88E1000_PSCR_MDI_MANUAL_MODE;
		break;
	case 2:
		phy_data |= M88E1000_PSCR_MDIX_MANUAL_MODE;
		break;
	case 3:
		phy_data |= M88E1000_PSCR_AUTO_X_1000T;
		break;
	case 0:
	default:
		phy_data |= M88E1000_PSCR_AUTO_X_MODE;
		break;
	}

	/* Options:
	 *   disable_polarity_correction = 0 (default)
	 *       Automatic Correction for Reversed Cable Polarity
	 *   0 - Disabled
	 *   1 - Enabled
	 */
	phy_data &= ~M88E1000_PSCR_POLARITY_REVERSAL;
	if (phy->disable_polarity_correction == 1)
		phy_data |= M88E1000_PSCR_POLARITY_REVERSAL;

	ret_val = phy->ops.write_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
	if (ret_val)
		goto out;

	if (phy->revision < E1000_REVISION_4) {
		/* Force TX_CLK in the Extended PHY Specific Control Register
		 * to 25MHz clock.
		 */
		ret_val = phy->ops.read_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL,
					    &phy_data);
		if (ret_val)
			goto out;

		phy_data |= M88E1000_EPSCR_TX_CLK_25;

		if ((phy->revision == E1000_REVISION_2) &&
		    (phy->id == M88E1111_I_PHY_ID)) {
			/* 82573L PHY - set the downshift counter to 5x. */
			phy_data &= ~M88EC018_EPSCR_DOWNSHIFT_COUNTER_MASK;
			phy_data |= M88EC018_EPSCR_DOWNSHIFT_COUNTER_5X;
		} else {
			/* Configure Master and Slave downshift values */
			phy_data &= ~(M88E1000_EPSCR_MASTER_DOWNSHIFT_MASK |
				      M88E1000_EPSCR_SLAVE_DOWNSHIFT_MASK);
			phy_data |= (M88E1000_EPSCR_MASTER_DOWNSHIFT_1X |
				     M88E1000_EPSCR_SLAVE_DOWNSHIFT_1X);
		}
		ret_val = phy->ops.write_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL,
					     phy_data);
		if (ret_val)
			goto out;
	}

	/* Commit the changes. */
	ret_val = igb_phy_sw_reset(hw);
	if (ret_val) {
		hw_dbg("Error committing the PHY changes\n");
		goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_copper_link_setup_m88_gen2 - Setup m88 PHY's for copper link
 *  @hw: pointer to the HW structure
 *
 *  Sets up MDI/MDI-X and polarity for i347-AT4, m88e1322 and m88e1112 PHY's.
 *  Also enables and sets the downshift parameters.
 **/
s32 igb_copper_link_setup_m88_gen2(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data;

	if (phy->reset_disable)
		return 0;

	/* Enable CRS on Tx. This must be set for half-duplex operation. */
	ret_val = phy->ops.read_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	/* Options:
	 *   MDI/MDI-X = 0 (default)
	 *   0 - Auto for all speeds
	 *   1 - MDI mode
	 *   2 - MDI-X mode
	 *   3 - Auto for 1000Base-T only (MDI-X for 10/100Base-T modes)
	 */
	phy_data &= ~M88E1000_PSCR_AUTO_X_MODE;

	switch (phy->mdix) {
	case 1:
		phy_data |= M88E1000_PSCR_MDI_MANUAL_MODE;
		break;
	case 2:
		phy_data |= M88E1000_PSCR_MDIX_MANUAL_MODE;
		break;
	case 3:
		/* M88E1112 does not support this mode) */
		if (phy->id != M88E1112_E_PHY_ID) {
			phy_data |= M88E1000_PSCR_AUTO_X_1000T;
			break;
		}
	case 0:
	default:
		phy_data |= M88E1000_PSCR_AUTO_X_MODE;
		break;
	}

	/* Options:
	 *   disable_polarity_correction = 0 (default)
	 *       Automatic Correction for Reversed Cable Polarity
	 *   0 - Disabled
	 *   1 - Enabled
	 */
	phy_data &= ~M88E1000_PSCR_POLARITY_REVERSAL;
	if (phy->disable_polarity_correction == 1)
		phy_data |= M88E1000_PSCR_POLARITY_REVERSAL;

	/* Enable downshift and setting it to X6 */
	if (phy->id == M88E1543_E_PHY_ID) {
		phy_data &= ~I347AT4_PSCR_DOWNSHIFT_ENABLE;
		ret_val =
		    phy->ops.write_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
		if (ret_val)
			return ret_val;

		ret_val = igb_phy_sw_reset(hw);
		if (ret_val) {
			hw_dbg("Error committing the PHY changes\n");
			return ret_val;
		}
	}

	phy_data &= ~I347AT4_PSCR_DOWNSHIFT_MASK;
	phy_data |= I347AT4_PSCR_DOWNSHIFT_6X;
	phy_data |= I347AT4_PSCR_DOWNSHIFT_ENABLE;

	ret_val = phy->ops.write_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
	if (ret_val)
		return ret_val;

	/* Commit the changes. */
	ret_val = igb_phy_sw_reset(hw);
	if (ret_val) {
		hw_dbg("Error committing the PHY changes\n");
		return ret_val;
	}
	ret_val = igb_set_master_slave_mode(hw);
	if (ret_val)
		return ret_val;

	return 0;
}

/**
 *  igb_copper_link_setup_igp - Setup igp PHY's for copper link
 *  @hw: pointer to the HW structure
 *
 *  Sets up LPLU, MDI/MDI-X, polarity, Smartspeed and Master/Slave config for
 *  igp PHY's.
 **/
s32 igb_copper_link_setup_igp(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	if (phy->reset_disable) {
		ret_val = 0;
		goto out;
	}

	ret_val = phy->ops.reset(hw);
	if (ret_val) {
		hw_dbg("Error resetting the PHY.\n");
		goto out;
	}

	/* Wait 100ms for MAC to configure PHY from NVM settings, to avoid
	 * timeout issues when LFS is enabled.
	 */
	msleep(100);

	/* The NVM settings will configure LPLU in D3 for
	 * non-IGP1 PHYs.
	 */
	if (phy->type == e1000_phy_igp) {
		/* disable lplu d3 during driver init */
		if (phy->ops.set_d3_lplu_state)
			ret_val = phy->ops.set_d3_lplu_state(hw, false);
		if (ret_val) {
			hw_dbg("Error Disabling LPLU D3\n");
			goto out;
		}
	}

	/* disable lplu d0 during driver init */
	ret_val = phy->ops.set_d0_lplu_state(hw, false);
	if (ret_val) {
		hw_dbg("Error Disabling LPLU D0\n");
		goto out;
	}
	/* Configure mdi-mdix settings */
	ret_val = phy->ops.read_reg(hw, IGP01E1000_PHY_PORT_CTRL, &data);
	if (ret_val)
		goto out;

	data &= ~IGP01E1000_PSCR_AUTO_MDIX;

	switch (phy->mdix) {
	case 1:
		data &= ~IGP01E1000_PSCR_FORCE_MDI_MDIX;
		break;
	case 2:
		data |= IGP01E1000_PSCR_FORCE_MDI_MDIX;
		break;
	case 0:
	default:
		data |= IGP01E1000_PSCR_AUTO_MDIX;
		break;
	}
	ret_val = phy->ops.write_reg(hw, IGP01E1000_PHY_PORT_CTRL, data);
	if (ret_val)
		goto out;

	/* set auto-master slave resolution settings */
	if (hw->mac.autoneg) {
		/* when autonegotiation advertisement is only 1000Mbps then we
		 * should disable SmartSpeed and enable Auto MasterSlave
		 * resolution as hardware default.
		 */
		if (phy->autoneg_advertised == ADVERTISE_1000_FULL) {
			/* Disable SmartSpeed */
			ret_val = phy->ops.read_reg(hw,
						    IGP01E1000_PHY_PORT_CONFIG,
						    &data);
			if (ret_val)
				goto out;

			data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
						     IGP01E1000_PHY_PORT_CONFIG,
						     data);
			if (ret_val)
				goto out;

			/* Set auto Master/Slave resolution process */
			ret_val = phy->ops.read_reg(hw, PHY_1000T_CTRL, &data);
			if (ret_val)
				goto out;

			data &= ~CR_1000T_MS_ENABLE;
			ret_val = phy->ops.write_reg(hw, PHY_1000T_CTRL, data);
			if (ret_val)
				goto out;
		}

		ret_val = phy->ops.read_reg(hw, PHY_1000T_CTRL, &data);
		if (ret_val)
			goto out;

		/* load defaults for future use */
		phy->original_ms_type = (data & CR_1000T_MS_ENABLE) ?
			((data & CR_1000T_MS_VALUE) ?
			e1000_ms_force_master :
			e1000_ms_force_slave) :
			e1000_ms_auto;

		switch (phy->ms_type) {
		case e1000_ms_force_master:
			data |= (CR_1000T_MS_ENABLE | CR_1000T_MS_VALUE);
			break;
		case e1000_ms_force_slave:
			data |= CR_1000T_MS_ENABLE;
			data &= ~(CR_1000T_MS_VALUE);
			break;
		case e1000_ms_auto:
			data &= ~CR_1000T_MS_ENABLE;
		default:
			break;
		}
		ret_val = phy->ops.write_reg(hw, PHY_1000T_CTRL, data);
		if (ret_val)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_copper_link_autoneg - Setup/Enable autoneg for copper link
 *  @hw: pointer to the HW structure
 *
 *  Performs initial bounds checking on autoneg advertisement parameter, then
 *  configure to advertise the full capability.  Setup the PHY to autoneg
 *  and restart the negotiation process between the link partner.  If
 *  autoneg_wait_to_complete, then wait for autoneg to complete before exiting.
 **/
static s32 igb_copper_link_autoneg(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_ctrl;

	/* Perform some bounds checking on the autoneg advertisement
	 * parameter.
	 */
	phy->autoneg_advertised &= phy->autoneg_mask;

	/* If autoneg_advertised is zero, we assume it was not defaulted
	 * by the calling code so we set to advertise full capability.
	 */
	if (phy->autoneg_advertised == 0)
		phy->autoneg_advertised = phy->autoneg_mask;

	hw_dbg("Reconfiguring auto-neg advertisement params\n");
	ret_val = igb_phy_setup_autoneg(hw);
	if (ret_val) {
		hw_dbg("Error Setting up Auto-Negotiation\n");
		goto out;
	}
	hw_dbg("Restarting Auto-Neg\n");

	/* Restart auto-negotiation by setting the Auto Neg Enable bit and
	 * the Auto Neg Restart bit in the PHY control register.
	 */
	ret_val = phy->ops.read_reg(hw, PHY_CONTROL, &phy_ctrl);
	if (ret_val)
		goto out;

	phy_ctrl |= (MII_CR_AUTO_NEG_EN | MII_CR_RESTART_AUTO_NEG);
	ret_val = phy->ops.write_reg(hw, PHY_CONTROL, phy_ctrl);
	if (ret_val)
		goto out;

	/* Does the user want to wait for Auto-Neg to complete here, or
	 * check at a later time (for example, callback routine).
	 */
	if (phy->autoneg_wait_to_complete) {
		ret_val = igb_wait_autoneg(hw);
		if (ret_val) {
			hw_dbg("Error while waiting for autoneg to complete\n");
			goto out;
		}
	}

	hw->mac.get_link_status = true;

out:
	return ret_val;
}

/**
 *  igb_phy_setup_autoneg - Configure PHY for auto-negotiation
 *  @hw: pointer to the HW structure
 *
 *  Reads the MII auto-neg advertisement register and/or the 1000T control
 *  register and if the PHY is already setup for auto-negotiation, then
 *  return successful.  Otherwise, setup advertisement and flow control to
 *  the appropriate values for the wanted auto-negotiation.
 **/
static s32 igb_phy_setup_autoneg(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 mii_autoneg_adv_reg;
	u16 mii_1000t_ctrl_reg = 0;

	phy->autoneg_advertised &= phy->autoneg_mask;

	/* Read the MII Auto-Neg Advertisement Register (Address 4). */
	ret_val = phy->ops.read_reg(hw, PHY_AUTONEG_ADV, &mii_autoneg_adv_reg);
	if (ret_val)
		goto out;

	if (phy->autoneg_mask & ADVERTISE_1000_FULL) {
		/* Read the MII 1000Base-T Control Register (Address 9). */
		ret_val = phy->ops.read_reg(hw, PHY_1000T_CTRL,
					    &mii_1000t_ctrl_reg);
		if (ret_val)
			goto out;
	}

	/* Need to parse both autoneg_advertised and fc and set up
	 * the appropriate PHY registers.  First we will parse for
	 * autoneg_advertised software override.  Since we can advertise
	 * a plethora of combinations, we need to check each bit
	 * individually.
	 */

	/* First we clear all the 10/100 mb speed bits in the Auto-Neg
	 * Advertisement Register (Address 4) and the 1000 mb speed bits in
	 * the  1000Base-T Control Register (Address 9).
	 */
	mii_autoneg_adv_reg &= ~(NWAY_AR_100TX_FD_CAPS |
				 NWAY_AR_100TX_HD_CAPS |
				 NWAY_AR_10T_FD_CAPS   |
				 NWAY_AR_10T_HD_CAPS);
	mii_1000t_ctrl_reg &= ~(CR_1000T_HD_CAPS | CR_1000T_FD_CAPS);

	hw_dbg("autoneg_advertised %x\n", phy->autoneg_advertised);

	/* Do we want to advertise 10 Mb Half Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_10_HALF) {
		hw_dbg("Advertise 10mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_HD_CAPS;
	}

	/* Do we want to advertise 10 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_10_FULL) {
		hw_dbg("Advertise 10mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_FD_CAPS;
	}

	/* Do we want to advertise 100 Mb Half Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_100_HALF) {
		hw_dbg("Advertise 100mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_HD_CAPS;
	}

	/* Do we want to advertise 100 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_100_FULL) {
		hw_dbg("Advertise 100mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_FD_CAPS;
	}

	/* We do not allow the Phy to advertise 1000 Mb Half Duplex */
	if (phy->autoneg_advertised & ADVERTISE_1000_HALF)
		hw_dbg("Advertise 1000mb Half duplex request denied!\n");

	/* Do we want to advertise 1000 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_1000_FULL) {
		hw_dbg("Advertise 1000mb Full duplex\n");
		mii_1000t_ctrl_reg |= CR_1000T_FD_CAPS;
	}

	/* Check for a software override of the flow control settings, and
	 * setup the PHY advertisement registers accordingly.  If
	 * auto-negotiation is enabled, then software will have to set the
	 * "PAUSE" bits to the correct value in the Auto-Negotiation
	 * Advertisement Register (PHY_AUTONEG_ADV) and re-start auto-
	 * negotiation.
	 *
	 * The possible values of the "fc" parameter are:
	 *      0:  Flow control is completely disabled
	 *      1:  Rx flow control is enabled (we can receive pause frames
	 *          but not send pause frames).
	 *      2:  Tx flow control is enabled (we can send pause frames
	 *          but we do not support receiving pause frames).
	 *      3:  Both Rx and TX flow control (symmetric) are enabled.
	 *  other:  No software override.  The flow control configuration
	 *          in the EEPROM is used.
	 */
	switch (hw->fc.current_mode) {
	case e1000_fc_none:
		/* Flow control (RX & TX) is completely disabled by a
		 * software over-ride.
		 */
		mii_autoneg_adv_reg &= ~(NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case e1000_fc_rx_pause:
		/* RX Flow control is enabled, and TX Flow control is
		 * disabled, by a software over-ride.
		 *
		 * Since there really isn't a way to advertise that we are
		 * capable of RX Pause ONLY, we will advertise that we
		 * support both symmetric and asymmetric RX PAUSE.  Later
		 * (in e1000_config_fc_after_link_up) we will disable the
		 * hw's ability to send PAUSE frames.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case e1000_fc_tx_pause:
		/* TX Flow control is enabled, and RX Flow control is
		 * disabled, by a software over-ride.
		 */
		mii_autoneg_adv_reg |= NWAY_AR_ASM_DIR;
		mii_autoneg_adv_reg &= ~NWAY_AR_PAUSE;
		break;
	case e1000_fc_full:
		/* Flow control (both RX and TX) is enabled by a software
		 * over-ride.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	default:
		hw_dbg("Flow control param set incorrectly\n");
		ret_val = -E1000_ERR_CONFIG;
		goto out;
	}

	ret_val = phy->ops.write_reg(hw, PHY_AUTONEG_ADV, mii_autoneg_adv_reg);
	if (ret_val)
		goto out;

	hw_dbg("Auto-Neg Advertising %x\n", mii_autoneg_adv_reg);

	if (phy->autoneg_mask & ADVERTISE_1000_FULL) {
		ret_val = phy->ops.write_reg(hw,
					     PHY_1000T_CTRL,
					     mii_1000t_ctrl_reg);
		if (ret_val)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_setup_copper_link - Configure copper link settings
 *  @hw: pointer to the HW structure
 *
 *  Calls the appropriate function to configure the link for auto-neg or forced
 *  speed and duplex.  Then we check for link, once link is established calls
 *  to configure collision distance and flow control are called.  If link is
 *  not established, we return -E1000_ERR_PHY (-2).
 **/
s32 igb_setup_copper_link(struct e1000_hw *hw)
{
	s32 ret_val;
	bool link;

	if (hw->mac.autoneg) {
		/* Setup autoneg and flow control advertisement and perform
		 * autonegotiation.
		 */
		ret_val = igb_copper_link_autoneg(hw);
		if (ret_val)
			goto out;
	} else {
		/* PHY will be set to 10H, 10F, 100H or 100F
		 * depending on user settings.
		 */
		hw_dbg("Forcing Speed and Duplex\n");
		ret_val = hw->phy.ops.force_speed_duplex(hw);
		if (ret_val) {
			hw_dbg("Error Forcing Speed and Duplex\n");
			goto out;
		}
	}

	/* Check link status. Wait up to 100 microseconds for link to become
	 * valid.
	 */
	ret_val = igb_phy_has_link(hw, COPPER_LINK_UP_LIMIT, 10, &link);
	if (ret_val)
		goto out;

	if (link) {
		hw_dbg("Valid link established!!!\n");
		igb_config_collision_dist(hw);
		ret_val = igb_config_fc_after_link_up(hw);
	} else {
		hw_dbg("Unable to establish link!!!\n");
	}

out:
	return ret_val;
}

/**
 *  igb_phy_force_speed_duplex_igp - Force speed/duplex for igp PHY
 *  @hw: pointer to the HW structure
 *
 *  Calls the PHY setup function to force speed and duplex.  Clears the
 *  auto-crossover to force MDI manually.  Waits for link and returns
 *  successful if link up is successful, else -E1000_ERR_PHY (-2).
 **/
s32 igb_phy_force_speed_duplex_igp(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data;
	bool link;

	ret_val = phy->ops.read_reg(hw, PHY_CONTROL, &phy_data);
	if (ret_val)
		goto out;

	igb_phy_force_speed_duplex_setup(hw, &phy_data);

	ret_val = phy->ops.write_reg(hw, PHY_CONTROL, phy_data);
	if (ret_val)
		goto out;

	/* Clear Auto-Crossover to force MDI manually.  IGP requires MDI
	 * forced whenever speed and duplex are forced.
	 */
	ret_val = phy->ops.read_reg(hw, IGP01E1000_PHY_PORT_CTRL, &phy_data);
	if (ret_val)
		goto out;

	phy_data &= ~IGP01E1000_PSCR_AUTO_MDIX;
	phy_data &= ~IGP01E1000_PSCR_FORCE_MDI_MDIX;

	ret_val = phy->ops.write_reg(hw, IGP01E1000_PHY_PORT_CTRL, phy_data);
	if (ret_val)
		goto out;

	hw_dbg("IGP PSCR: %X\n", phy_data);

	udelay(1);

	if (phy->autoneg_wait_to_complete) {
		hw_dbg("Waiting for forced speed/duplex link on IGP phy.\n");

		ret_val = igb_phy_has_link(hw, PHY_FORCE_LIMIT, 10000, &link);
		if (ret_val)
			goto out;

		if (!link)
			hw_dbg("Link taking longer than expected.\n");

		/* Try once more */
		ret_val = igb_phy_has_link(hw, PHY_FORCE_LIMIT, 10000, &link);
		if (ret_val)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_phy_force_speed_duplex_m88 - Force speed/duplex for m88 PHY
 *  @hw: pointer to the HW structure
 *
 *  Calls the PHY setup function to force speed and duplex.  Clears the
 *  auto-crossover to force MDI manually.  Resets the PHY to commit the
 *  changes.  If time expires while waiting for link up, we reset the DSP.
 *  After reset, TX_CLK and CRS on TX must be set.  Return successful upon
 *  successful completion, else return corresponding error code.
 **/
s32 igb_phy_force_speed_duplex_m88(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data;
	bool link;

	/* I210 and I211 devices support Auto-Crossover in forced operation. */
	if (phy->type != e1000_phy_i210) {
		/* Clear Auto-Crossover to force MDI manually.  M88E1000
		 * requires MDI forced whenever speed and duplex are forced.
		 */
		ret_val = phy->ops.read_reg(hw, M88E1000_PHY_SPEC_CTRL,
					    &phy_data);
		if (ret_val)
			goto out;

		phy_data &= ~M88E1000_PSCR_AUTO_X_MODE;
		ret_val = phy->ops.write_reg(hw, M88E1000_PHY_SPEC_CTRL,
					     phy_data);
		if (ret_val)
			goto out;

		hw_dbg("M88E1000 PSCR: %X\n", phy_data);
	}

	ret_val = phy->ops.read_reg(hw, PHY_CONTROL, &phy_data);
	if (ret_val)
		goto out;

	igb_phy_force_speed_duplex_setup(hw, &phy_data);

	ret_val = phy->ops.write_reg(hw, PHY_CONTROL, phy_data);
	if (ret_val)
		goto out;

	/* Reset the phy to commit changes. */
	ret_val = igb_phy_sw_reset(hw);
	if (ret_val)
		goto out;

	if (phy->autoneg_wait_to_complete) {
		hw_dbg("Waiting for forced speed/duplex link on M88 phy.\n");

		ret_val = igb_phy_has_link(hw, PHY_FORCE_LIMIT, 100000, &link);
		if (ret_val)
			goto out;

		if (!link) {
			bool reset_dsp = true;

			switch (hw->phy.id) {
			case I347AT4_E_PHY_ID:
			case M88E1112_E_PHY_ID:
			case M88E1543_E_PHY_ID:
			case M88E1512_E_PHY_ID:
			case I210_I_PHY_ID:
				reset_dsp = false;
				break;
			default:
				if (hw->phy.type != e1000_phy_m88)
					reset_dsp = false;
				break;
			}
			if (!reset_dsp) {
				hw_dbg("Link taking longer than expected.\n");
			} else {
				/* We didn't get link.
				 * Reset the DSP and cross our fingers.
				 */
				ret_val = phy->ops.write_reg(hw,
						M88E1000_PHY_PAGE_SELECT,
						0x001d);
				if (ret_val)
					goto out;
				ret_val = igb_phy_reset_dsp(hw);
				if (ret_val)
					goto out;
			}
		}

		/* Try once more */
		ret_val = igb_phy_has_link(hw, PHY_FORCE_LIMIT,
					   100000, &link);
		if (ret_val)
			goto out;
	}

	if (hw->phy.type != e1000_phy_m88 ||
	    hw->phy.id == I347AT4_E_PHY_ID ||
	    hw->phy.id == M88E1112_E_PHY_ID ||
	    hw->phy.id == M88E1543_E_PHY_ID ||
	    hw->phy.id == M88E1512_E_PHY_ID ||
	    hw->phy.id == I210_I_PHY_ID)
		goto out;

	ret_val = phy->ops.read_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		goto out;

	/* Resetting the phy means we need to re-force TX_CLK in the
	 * Extended PHY Specific Control Register to 25MHz clock from
	 * the reset value of 2.5MHz.
	 */
	phy_data |= M88E1000_EPSCR_TX_CLK_25;
	ret_val = phy->ops.write_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL, phy_data);
	if (ret_val)
		goto out;

	/* In addition, we must re-enable CRS on Tx for both half and full
	 * duplex.
	 */
	ret_val = phy->ops.read_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		goto out;

	phy_data |= M88E1000_PSCR_ASSERT_CRS_ON_TX;
	ret_val = phy->ops.write_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);

out:
	return ret_val;
}

/**
 *  igb_phy_force_speed_duplex_setup - Configure forced PHY speed/duplex
 *  @hw: pointer to the HW structure
 *  @phy_ctrl: pointer to current value of PHY_CONTROL
 *
 *  Forces speed and duplex on the PHY by doing the following: disable flow
 *  control, force speed/duplex on the MAC, disable auto speed detection,
 *  disable auto-negotiation, configure duplex, configure speed, configure
 *  the collision distance, write configuration to CTRL register.  The
 *  caller must write to the PHY_CONTROL register for these settings to
 *  take affect.
 **/
static void igb_phy_force_speed_duplex_setup(struct e1000_hw *hw,
					     u16 *phy_ctrl)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 ctrl;

	/* Turn off flow control when forcing speed/duplex */
	hw->fc.current_mode = e1000_fc_none;

	/* Force speed/duplex on the mac */
	ctrl = rd32(E1000_CTRL);
	ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	ctrl &= ~E1000_CTRL_SPD_SEL;

	/* Disable Auto Speed Detection */
	ctrl &= ~E1000_CTRL_ASDE;

	/* Disable autoneg on the phy */
	*phy_ctrl &= ~MII_CR_AUTO_NEG_EN;

	/* Forcing Full or Half Duplex? */
	if (mac->forced_speed_duplex & E1000_ALL_HALF_DUPLEX) {
		ctrl &= ~E1000_CTRL_FD;
		*phy_ctrl &= ~MII_CR_FULL_DUPLEX;
		hw_dbg("Half Duplex\n");
	} else {
		ctrl |= E1000_CTRL_FD;
		*phy_ctrl |= MII_CR_FULL_DUPLEX;
		hw_dbg("Full Duplex\n");
	}

	/* Forcing 10mb or 100mb? */
	if (mac->forced_speed_duplex & E1000_ALL_100_SPEED) {
		ctrl |= E1000_CTRL_SPD_100;
		*phy_ctrl |= MII_CR_SPEED_100;
		*phy_ctrl &= ~(MII_CR_SPEED_1000 | MII_CR_SPEED_10);
		hw_dbg("Forcing 100mb\n");
	} else {
		ctrl &= ~(E1000_CTRL_SPD_1000 | E1000_CTRL_SPD_100);
		*phy_ctrl |= MII_CR_SPEED_10;
		*phy_ctrl &= ~(MII_CR_SPEED_1000 | MII_CR_SPEED_100);
		hw_dbg("Forcing 10mb\n");
	}

	igb_config_collision_dist(hw);

	wr32(E1000_CTRL, ctrl);
}

/**
 *  igb_set_d3_lplu_state - Sets low power link up state for D3
 *  @hw: pointer to the HW structure
 *  @active: boolean used to enable/disable lplu
 *
 *  Success returns 0, Failure returns 1
 *
 *  The low power link up (lplu) state is set to the power management level D3
 *  and SmartSpeed is disabled when active is true, else clear lplu for D3
 *  and enable Smartspeed.  LPLU and Smartspeed are mutually exclusive.  LPLU
 *  is used during Dx states where the power conservation is most important.
 *  During driver activity, SmartSpeed should be enabled so performance is
 *  maintained.
 **/
s32 igb_set_d3_lplu_state(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = 0;
	u16 data;

	if (!(hw->phy.ops.read_reg))
		goto out;

	ret_val = phy->ops.read_reg(hw, IGP02E1000_PHY_POWER_MGMT, &data);
	if (ret_val)
		goto out;

	if (!active) {
		data &= ~IGP02E1000_PM_D3_LPLU;
		ret_val = phy->ops.write_reg(hw, IGP02E1000_PHY_POWER_MGMT,
					     data);
		if (ret_val)
			goto out;
		/* LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on) {
			ret_val = phy->ops.read_reg(hw,
						    IGP01E1000_PHY_PORT_CONFIG,
						    &data);
			if (ret_val)
				goto out;

			data |= IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
						     IGP01E1000_PHY_PORT_CONFIG,
						     data);
			if (ret_val)
				goto out;
		} else if (phy->smart_speed == e1000_smart_speed_off) {
			ret_val = phy->ops.read_reg(hw,
						     IGP01E1000_PHY_PORT_CONFIG,
						     &data);
			if (ret_val)
				goto out;

			data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
						     IGP01E1000_PHY_PORT_CONFIG,
						     data);
			if (ret_val)
				goto out;
		}
	} else if ((phy->autoneg_advertised == E1000_ALL_SPEED_DUPLEX) ||
		   (phy->autoneg_advertised == E1000_ALL_NOT_GIG) ||
		   (phy->autoneg_advertised == E1000_ALL_10_SPEED)) {
		data |= IGP02E1000_PM_D3_LPLU;
		ret_val = phy->ops.write_reg(hw, IGP02E1000_PHY_POWER_MGMT,
					      data);
		if (ret_val)
			goto out;

		/* When LPLU is enabled, we should disable SmartSpeed */
		ret_val = phy->ops.read_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					    &data);
		if (ret_val)
			goto out;

		data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = phy->ops.write_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					     data);
	}

out:
	return ret_val;
}

/**
 *  igb_check_downshift - Checks whether a downshift in speed occurred
 *  @hw: pointer to the HW structure
 *
 *  Success returns 0, Failure returns 1
 *
 *  A downshift is detected by querying the PHY link health.
 **/
s32 igb_check_downshift(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data, offset, mask;

	switch (phy->type) {
	case e1000_phy_i210:
	case e1000_phy_m88:
	case e1000_phy_gg82563:
		offset	= M88E1000_PHY_SPEC_STATUS;
		mask	= M88E1000_PSSR_DOWNSHIFT;
		break;
	case e1000_phy_igp_2:
	case e1000_phy_igp:
	case e1000_phy_igp_3:
		offset	= IGP01E1000_PHY_LINK_HEALTH;
		mask	= IGP01E1000_PLHR_SS_DOWNGRADE;
		break;
	default:
		/* speed downshift not supported */
		phy->speed_downgraded = false;
		ret_val = 0;
		goto out;
	}

	ret_val = phy->ops.read_reg(hw, offset, &phy_data);

	if (!ret_val)
		phy->speed_downgraded = (phy_data & mask) ? true : false;

out:
	return ret_val;
}

/**
 *  igb_check_polarity_m88 - Checks the polarity.
 *  @hw: pointer to the HW structure
 *
 *  Success returns 0, Failure returns -E1000_ERR_PHY (-2)
 *
 *  Polarity is determined based on the PHY specific status register.
 **/
s32 igb_check_polarity_m88(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	ret_val = phy->ops.read_reg(hw, M88E1000_PHY_SPEC_STATUS, &data);

	if (!ret_val)
		phy->cable_polarity = (data & M88E1000_PSSR_REV_POLARITY)
				      ? e1000_rev_polarity_reversed
				      : e1000_rev_polarity_normal;

	return ret_val;
}

/**
 *  igb_check_polarity_igp - Checks the polarity.
 *  @hw: pointer to the HW structure
 *
 *  Success returns 0, Failure returns -E1000_ERR_PHY (-2)
 *
 *  Polarity is determined based on the PHY port status register, and the
 *  current speed (since there is no polarity at 100Mbps).
 **/
static s32 igb_check_polarity_igp(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data, offset, mask;

	/* Polarity is determined based on the speed of
	 * our connection.
	 */
	ret_val = phy->ops.read_reg(hw, IGP01E1000_PHY_PORT_STATUS, &data);
	if (ret_val)
		goto out;

	if ((data & IGP01E1000_PSSR_SPEED_MASK) ==
	    IGP01E1000_PSSR_SPEED_1000MBPS) {
		offset	= IGP01E1000_PHY_PCS_INIT_REG;
		mask	= IGP01E1000_PHY_POLARITY_MASK;
	} else {
		/* This really only applies to 10Mbps since
		 * there is no polarity for 100Mbps (always 0).
		 */
		offset	= IGP01E1000_PHY_PORT_STATUS;
		mask	= IGP01E1000_PSSR_POLARITY_REVERSED;
	}

	ret_val = phy->ops.read_reg(hw, offset, &data);

	if (!ret_val)
		phy->cable_polarity = (data & mask)
				      ? e1000_rev_polarity_reversed
				      : e1000_rev_polarity_normal;

out:
	return ret_val;
}

/**
 *  igb_wait_autoneg - Wait for auto-neg completion
 *  @hw: pointer to the HW structure
 *
 *  Waits for auto-negotiation to complete or for the auto-negotiation time
 *  limit to expire, which ever happens first.
 **/
static s32 igb_wait_autoneg(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	u16 i, phy_status;

	/* Break after autoneg completes or PHY_AUTO_NEG_LIMIT expires. */
	for (i = PHY_AUTO_NEG_LIMIT; i > 0; i--) {
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		if (phy_status & MII_SR_AUTONEG_COMPLETE)
			break;
		msleep(100);
	}

	/* PHY_AUTO_NEG_TIME expiration doesn't guarantee auto-negotiation
	 * has completed.
	 */
	return ret_val;
}

/**
 *  igb_phy_has_link - Polls PHY for link
 *  @hw: pointer to the HW structure
 *  @iterations: number of times to poll for link
 *  @usec_interval: delay between polling attempts
 *  @success: pointer to whether polling was successful or not
 *
 *  Polls the PHY status register for link, 'iterations' number of times.
 **/
s32 igb_phy_has_link(struct e1000_hw *hw, u32 iterations,
		     u32 usec_interval, bool *success)
{
	s32 ret_val = 0;
	u16 i, phy_status;

	for (i = 0; i < iterations; i++) {
		/* Some PHYs require the PHY_STATUS register to be read
		 * twice due to the link bit being sticky.  No harm doing
		 * it across the board.
		 */
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val && usec_interval > 0) {
			/* If the first read fails, another entity may have
			 * ownership of the resources, wait and try again to
			 * see if they have relinquished the resources yet.
			 */
			if (usec_interval >= 1000)
				mdelay(usec_interval/1000);
			else
				udelay(usec_interval);
		}
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		if (phy_status & MII_SR_LINK_STATUS)
			break;
		if (usec_interval >= 1000)
			mdelay(usec_interval/1000);
		else
			udelay(usec_interval);
	}

	*success = (i < iterations) ? true : false;

	return ret_val;
}

/**
 *  igb_get_cable_length_m88 - Determine cable length for m88 PHY
 *  @hw: pointer to the HW structure
 *
 *  Reads the PHY specific status register to retrieve the cable length
 *  information.  The cable length is determined by averaging the minimum and
 *  maximum values to get the "average" cable length.  The m88 PHY has four
 *  possible cable length values, which are:
 *	Register Value		Cable Length
 *	0			< 50 meters
 *	1			50 - 80 meters
 *	2			80 - 110 meters
 *	3			110 - 140 meters
 *	4			> 140 meters
 **/
s32 igb_get_cable_length_m88(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data, index;

	ret_val = phy->ops.read_reg(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
	if (ret_val)
		goto out;

	index = (phy_data & M88E1000_PSSR_CABLE_LENGTH) >>
		M88E1000_PSSR_CABLE_LENGTH_SHIFT;
	if (index >= ARRAY_SIZE(e1000_m88_cable_length_table) - 1) {
		ret_val = -E1000_ERR_PHY;
		goto out;
	}

	phy->min_cable_length = e1000_m88_cable_length_table[index];
	phy->max_cable_length = e1000_m88_cable_length_table[index + 1];

	phy->cable_length = (phy->min_cable_length + phy->max_cable_length) / 2;

out:
	return ret_val;
}

s32 igb_get_cable_length_m88_gen2(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data, phy_data2, index, default_page, is_cm;

	switch (hw->phy.id) {
	case I210_I_PHY_ID:
		/* Get cable length from PHY Cable Diagnostics Control Reg */
		ret_val = phy->ops.read_reg(hw, (0x7 << GS40G_PAGE_SHIFT) +
					    (I347AT4_PCDL + phy->addr),
					    &phy_data);
		if (ret_val)
			return ret_val;

		/* Check if the unit of cable length is meters or cm */
		ret_val = phy->ops.read_reg(hw, (0x7 << GS40G_PAGE_SHIFT) +
					    I347AT4_PCDC, &phy_data2);
		if (ret_val)
			return ret_val;

		is_cm = !(phy_data2 & I347AT4_PCDC_CABLE_LENGTH_UNIT);

		/* Populate the phy structure with cable length in meters */
		phy->min_cable_length = phy_data / (is_cm ? 100 : 1);
		phy->max_cable_length = phy_data / (is_cm ? 100 : 1);
		phy->cable_length = phy_data / (is_cm ? 100 : 1);
		break;
	case M88E1543_E_PHY_ID:
	case M88E1512_E_PHY_ID:
	case I347AT4_E_PHY_ID:
		/* Remember the original page select and set it to 7 */
		ret_val = phy->ops.read_reg(hw, I347AT4_PAGE_SELECT,
					    &default_page);
		if (ret_val)
			goto out;

		ret_val = phy->ops.write_reg(hw, I347AT4_PAGE_SELECT, 0x07);
		if (ret_val)
			goto out;

		/* Get cable length from PHY Cable Diagnostics Control Reg */
		ret_val = phy->ops.read_reg(hw, (I347AT4_PCDL + phy->addr),
					    &phy_data);
		if (ret_val)
			goto out;

		/* Check if the unit of cable length is meters or cm */
		ret_val = phy->ops.read_reg(hw, I347AT4_PCDC, &phy_data2);
		if (ret_val)
			goto out;

		is_cm = !(phy_data2 & I347AT4_PCDC_CABLE_LENGTH_UNIT);

		/* Populate the phy structure with cable length in meters */
		phy->min_cable_length = phy_data / (is_cm ? 100 : 1);
		phy->max_cable_length = phy_data / (is_cm ? 100 : 1);
		phy->cable_length = phy_data / (is_cm ? 100 : 1);

		/* Reset the page selec to its original value */
		ret_val = phy->ops.write_reg(hw, I347AT4_PAGE_SELECT,
					     default_page);
		if (ret_val)
			goto out;
		break;
	case M88E1112_E_PHY_ID:
		/* Remember the original page select and set it to 5 */
		ret_val = phy->ops.read_reg(hw, I347AT4_PAGE_SELECT,
					    &default_page);
		if (ret_val)
			goto out;

		ret_val = phy->ops.write_reg(hw, I347AT4_PAGE_SELECT, 0x05);
		if (ret_val)
			goto out;

		ret_val = phy->ops.read_reg(hw, M88E1112_VCT_DSP_DISTANCE,
					    &phy_data);
		if (ret_val)
			goto out;

		index = (phy_data & M88E1000_PSSR_CABLE_LENGTH) >>
			M88E1000_PSSR_CABLE_LENGTH_SHIFT;
		if (index >= ARRAY_SIZE(e1000_m88_cable_length_table) - 1) {
			ret_val = -E1000_ERR_PHY;
			goto out;
		}

		phy->min_cable_length = e1000_m88_cable_length_table[index];
		phy->max_cable_length = e1000_m88_cable_length_table[index + 1];

		phy->cable_length = (phy->min_cable_length +
				     phy->max_cable_length) / 2;

		/* Reset the page select to its original value */
		ret_val = phy->ops.write_reg(hw, I347AT4_PAGE_SELECT,
					     default_page);
		if (ret_val)
			goto out;

		break;
	default:
		ret_val = -E1000_ERR_PHY;
		goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_get_cable_length_igp_2 - Determine cable length for igp2 PHY
 *  @hw: pointer to the HW structure
 *
 *  The automatic gain control (agc) normalizes the amplitude of the
 *  received signal, adjusting for the attenuation produced by the
 *  cable.  By reading the AGC registers, which represent the
 *  combination of coarse and fine gain value, the value can be put
 *  into a lookup table to obtain the approximate cable length
 *  for each channel.
 **/
s32 igb_get_cable_length_igp_2(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = 0;
	u16 phy_data, i, agc_value = 0;
	u16 cur_agc_index, max_agc_index = 0;
	u16 min_agc_index = ARRAY_SIZE(e1000_igp_2_cable_length_table) - 1;
	static const u16 agc_reg_array[IGP02E1000_PHY_CHANNEL_NUM] = {
		IGP02E1000_PHY_AGC_A,
		IGP02E1000_PHY_AGC_B,
		IGP02E1000_PHY_AGC_C,
		IGP02E1000_PHY_AGC_D
	};

	/* Read the AGC registers for all channels */
	for (i = 0; i < IGP02E1000_PHY_CHANNEL_NUM; i++) {
		ret_val = phy->ops.read_reg(hw, agc_reg_array[i], &phy_data);
		if (ret_val)
			goto out;

		/* Getting bits 15:9, which represent the combination of
		 * coarse and fine gain values.  The result is a number
		 * that can be put into the lookup table to obtain the
		 * approximate cable length.
		 */
		cur_agc_index = (phy_data >> IGP02E1000_AGC_LENGTH_SHIFT) &
				IGP02E1000_AGC_LENGTH_MASK;

		/* Array index bound check. */
		if ((cur_agc_index >= ARRAY_SIZE(e1000_igp_2_cable_length_table)) ||
		    (cur_agc_index == 0)) {
			ret_val = -E1000_ERR_PHY;
			goto out;
		}

		/* Remove min & max AGC values from calculation. */
		if (e1000_igp_2_cable_length_table[min_agc_index] >
		    e1000_igp_2_cable_length_table[cur_agc_index])
			min_agc_index = cur_agc_index;
		if (e1000_igp_2_cable_length_table[max_agc_index] <
		    e1000_igp_2_cable_length_table[cur_agc_index])
			max_agc_index = cur_agc_index;

		agc_value += e1000_igp_2_cable_length_table[cur_agc_index];
	}

	agc_value -= (e1000_igp_2_cable_length_table[min_agc_index] +
		      e1000_igp_2_cable_length_table[max_agc_index]);
	agc_value /= (IGP02E1000_PHY_CHANNEL_NUM - 2);

	/* Calculate cable length with the error range of +/- 10 meters. */
	phy->min_cable_length = ((agc_value - IGP02E1000_AGC_RANGE) > 0) ?
				 (agc_value - IGP02E1000_AGC_RANGE) : 0;
	phy->max_cable_length = agc_value + IGP02E1000_AGC_RANGE;

	phy->cable_length = (phy->min_cable_length + phy->max_cable_length) / 2;

out:
	return ret_val;
}

/**
 *  igb_get_phy_info_m88 - Retrieve PHY information
 *  @hw: pointer to the HW structure
 *
 *  Valid for only copper links.  Read the PHY status register (sticky read)
 *  to verify that link is up.  Read the PHY special control register to
 *  determine the polarity and 10base-T extended distance.  Read the PHY
 *  special status register to determine MDI/MDIx and current speed.  If
 *  speed is 1000, then determine cable length, local and remote receiver.
 **/
s32 igb_get_phy_info_m88(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32  ret_val;
	u16 phy_data;
	bool link;

	if (phy->media_type != e1000_media_type_copper) {
		hw_dbg("Phy info is only valid for copper media\n");
		ret_val = -E1000_ERR_CONFIG;
		goto out;
	}

	ret_val = igb_phy_has_link(hw, 1, 0, &link);
	if (ret_val)
		goto out;

	if (!link) {
		hw_dbg("Phy info is only valid if link is up\n");
		ret_val = -E1000_ERR_CONFIG;
		goto out;
	}

	ret_val = phy->ops.read_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		goto out;

	phy->polarity_correction = (phy_data & M88E1000_PSCR_POLARITY_REVERSAL)
				   ? true : false;

	ret_val = igb_check_polarity_m88(hw);
	if (ret_val)
		goto out;

	ret_val = phy->ops.read_reg(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
	if (ret_val)
		goto out;

	phy->is_mdix = (phy_data & M88E1000_PSSR_MDIX) ? true : false;

	if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_1000MBS) {
		ret_val = phy->ops.get_cable_length(hw);
		if (ret_val)
			goto out;

		ret_val = phy->ops.read_reg(hw, PHY_1000T_STATUS, &phy_data);
		if (ret_val)
			goto out;

		phy->local_rx = (phy_data & SR_1000T_LOCAL_RX_STATUS)
				? e1000_1000t_rx_status_ok
				: e1000_1000t_rx_status_not_ok;

		phy->remote_rx = (phy_data & SR_1000T_REMOTE_RX_STATUS)
				 ? e1000_1000t_rx_status_ok
				 : e1000_1000t_rx_status_not_ok;
	} else {
		/* Set values to "undefined" */
		phy->cable_length = E1000_CABLE_LENGTH_UNDEFINED;
		phy->local_rx = e1000_1000t_rx_status_undefined;
		phy->remote_rx = e1000_1000t_rx_status_undefined;
	}

out:
	return ret_val;
}

/**
 *  igb_get_phy_info_igp - Retrieve igp PHY information
 *  @hw: pointer to the HW structure
 *
 *  Read PHY status to determine if link is up.  If link is up, then
 *  set/determine 10base-T extended distance and polarity correction.  Read
 *  PHY port status to determine MDI/MDIx and speed.  Based on the speed,
 *  determine on the cable length, local and remote receiver.
 **/
s32 igb_get_phy_info_igp(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;
	bool link;

	ret_val = igb_phy_has_link(hw, 1, 0, &link);
	if (ret_val)
		goto out;

	if (!link) {
		hw_dbg("Phy info is only valid if link is up\n");
		ret_val = -E1000_ERR_CONFIG;
		goto out;
	}

	phy->polarity_correction = true;

	ret_val = igb_check_polarity_igp(hw);
	if (ret_val)
		goto out;

	ret_val = phy->ops.read_reg(hw, IGP01E1000_PHY_PORT_STATUS, &data);
	if (ret_val)
		goto out;

	phy->is_mdix = (data & IGP01E1000_PSSR_MDIX) ? true : false;

	if ((data & IGP01E1000_PSSR_SPEED_MASK) ==
	    IGP01E1000_PSSR_SPEED_1000MBPS) {
		ret_val = phy->ops.get_cable_length(hw);
		if (ret_val)
			goto out;

		ret_val = phy->ops.read_reg(hw, PHY_1000T_STATUS, &data);
		if (ret_val)
			goto out;

		phy->local_rx = (data & SR_1000T_LOCAL_RX_STATUS)
				? e1000_1000t_rx_status_ok
				: e1000_1000t_rx_status_not_ok;

		phy->remote_rx = (data & SR_1000T_REMOTE_RX_STATUS)
				 ? e1000_1000t_rx_status_ok
				 : e1000_1000t_rx_status_not_ok;
	} else {
		phy->cable_length = E1000_CABLE_LENGTH_UNDEFINED;
		phy->local_rx = e1000_1000t_rx_status_undefined;
		phy->remote_rx = e1000_1000t_rx_status_undefined;
	}

out:
	return ret_val;
}

/**
 *  igb_phy_sw_reset - PHY software reset
 *  @hw: pointer to the HW structure
 *
 *  Does a software reset of the PHY by reading the PHY control register and
 *  setting/write the control register reset bit to the PHY.
 **/
s32 igb_phy_sw_reset(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	u16 phy_ctrl;

	if (!(hw->phy.ops.read_reg))
		goto out;

	ret_val = hw->phy.ops.read_reg(hw, PHY_CONTROL, &phy_ctrl);
	if (ret_val)
		goto out;

	phy_ctrl |= MII_CR_RESET;
	ret_val = hw->phy.ops.write_reg(hw, PHY_CONTROL, phy_ctrl);
	if (ret_val)
		goto out;

	udelay(1);

out:
	return ret_val;
}

/**
 *  igb_phy_hw_reset - PHY hardware reset
 *  @hw: pointer to the HW structure
 *
 *  Verify the reset block is not blocking us from resetting.  Acquire
 *  semaphore (if necessary) and read/set/write the device control reset
 *  bit in the PHY.  Wait the appropriate delay time for the device to
 *  reset and release the semaphore (if necessary).
 **/
s32 igb_phy_hw_reset(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32  ret_val;
	u32 ctrl;

	ret_val = igb_check_reset_block(hw);
	if (ret_val) {
		ret_val = 0;
		goto out;
	}

	ret_val = phy->ops.acquire(hw);
	if (ret_val)
		goto out;

	ctrl = rd32(E1000_CTRL);
	wr32(E1000_CTRL, ctrl | E1000_CTRL_PHY_RST);
	wrfl();

	udelay(phy->reset_delay_us);

	wr32(E1000_CTRL, ctrl);
	wrfl();

	udelay(150);

	phy->ops.release(hw);

	ret_val = phy->ops.get_cfg_done(hw);

out:
	return ret_val;
}

/**
 *  igb_phy_init_script_igp3 - Inits the IGP3 PHY
 *  @hw: pointer to the HW structure
 *
 *  Initializes a Intel Gigabit PHY3 when an EEPROM is not present.
 **/
s32 igb_phy_init_script_igp3(struct e1000_hw *hw)
{
	hw_dbg("Running IGP 3 PHY init script\n");

	/* PHY init IGP 3 */
	/* Enable rise/fall, 10-mode work in class-A */
	hw->phy.ops.write_reg(hw, 0x2F5B, 0x9018);
	/* Remove all caps from Replica path filter */
	hw->phy.ops.write_reg(hw, 0x2F52, 0x0000);
	/* Bias trimming for ADC, AFE and Driver (Default) */
	hw->phy.ops.write_reg(hw, 0x2FB1, 0x8B24);
	/* Increase Hybrid poly bias */
	hw->phy.ops.write_reg(hw, 0x2FB2, 0xF8F0);
	/* Add 4% to TX amplitude in Giga mode */
	hw->phy.ops.write_reg(hw, 0x2010, 0x10B0);
	/* Disable trimming (TTT) */
	hw->phy.ops.write_reg(hw, 0x2011, 0x0000);
	/* Poly DC correction to 94.6% + 2% for all channels */
	hw->phy.ops.write_reg(hw, 0x20DD, 0x249A);
	/* ABS DC correction to 95.9% */
	hw->phy.ops.write_reg(hw, 0x20DE, 0x00D3);
	/* BG temp curve trim */
	hw->phy.ops.write_reg(hw, 0x28B4, 0x04CE);
	/* Increasing ADC OPAMP stage 1 currents to max */
	hw->phy.ops.write_reg(hw, 0x2F70, 0x29E4);
	/* Force 1000 ( required for enabling PHY regs configuration) */
	hw->phy.ops.write_reg(hw, 0x0000, 0x0140);
	/* Set upd_freq to 6 */
	hw->phy.ops.write_reg(hw, 0x1F30, 0x1606);
	/* Disable NPDFE */
	hw->phy.ops.write_reg(hw, 0x1F31, 0xB814);
	/* Disable adaptive fixed FFE (Default) */
	hw->phy.ops.write_reg(hw, 0x1F35, 0x002A);
	/* Enable FFE hysteresis */
	hw->phy.ops.write_reg(hw, 0x1F3E, 0x0067);
	/* Fixed FFE for short cable lengths */
	hw->phy.ops.write_reg(hw, 0x1F54, 0x0065);
	/* Fixed FFE for medium cable lengths */
	hw->phy.ops.write_reg(hw, 0x1F55, 0x002A);
	/* Fixed FFE for long cable lengths */
	hw->phy.ops.write_reg(hw, 0x1F56, 0x002A);
	/* Enable Adaptive Clip Threshold */
	hw->phy.ops.write_reg(hw, 0x1F72, 0x3FB0);
	/* AHT reset limit to 1 */
	hw->phy.ops.write_reg(hw, 0x1F76, 0xC0FF);
	/* Set AHT master delay to 127 msec */
	hw->phy.ops.write_reg(hw, 0x1F77, 0x1DEC);
	/* Set scan bits for AHT */
	hw->phy.ops.write_reg(hw, 0x1F78, 0xF9EF);
	/* Set AHT Preset bits */
	hw->phy.ops.write_reg(hw, 0x1F79, 0x0210);
	/* Change integ_factor of channel A to 3 */
	hw->phy.ops.write_reg(hw, 0x1895, 0x0003);
	/* Change prop_factor of channels BCD to 8 */
	hw->phy.ops.write_reg(hw, 0x1796, 0x0008);
	/* Change cg_icount + enable integbp for channels BCD */
	hw->phy.ops.write_reg(hw, 0x1798, 0xD008);
	/* Change cg_icount + enable integbp + change prop_factor_master
	 * to 8 for channel A
	 */
	hw->phy.ops.write_reg(hw, 0x1898, 0xD918);
	/* Disable AHT in Slave mode on channel A */
	hw->phy.ops.write_reg(hw, 0x187A, 0x0800);
	/* Enable LPLU and disable AN to 1000 in non-D0a states,
	 * Enable SPD+B2B
	 */
	hw->phy.ops.write_reg(hw, 0x0019, 0x008D);
	/* Enable restart AN on an1000_dis change */
	hw->phy.ops.write_reg(hw, 0x001B, 0x2080);
	/* Enable wh_fifo read clock in 10/100 modes */
	hw->phy.ops.write_reg(hw, 0x0014, 0x0045);
	/* Restart AN, Speed selection is 1000 */
	hw->phy.ops.write_reg(hw, 0x0000, 0x1340);

	return 0;
}

/**
 *  igb_initialize_M88E1512_phy - Initialize M88E1512 PHY
 *  @hw: pointer to the HW structure
 *
 *  Initialize Marvel 1512 to work correctly with Avoton.
 **/
s32 igb_initialize_M88E1512_phy(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = 0;

	/* Switch to PHY page 0xFF. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0x00FF);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0x214B);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x2144);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0x0C28);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x2146);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0xB233);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x214D);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0xCC0C);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x2159);
	if (ret_val)
		goto out;

	/* Switch to PHY page 0xFB. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0x00FB);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_3, 0x000D);
	if (ret_val)
		goto out;

	/* Switch to PHY page 0x12. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0x12);
	if (ret_val)
		goto out;

	/* Change mode to SGMII-to-Copper */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_MODE, 0x8001);
	if (ret_val)
		goto out;

	/* Return the PHY to page 0. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0);
	if (ret_val)
		goto out;

	ret_val = igb_phy_sw_reset(hw);
	if (ret_val) {
		hw_dbg("Error committing the PHY changes\n");
		return ret_val;
	}

	/* msec_delay(1000); */
	usleep_range(1000, 2000);
out:
	return ret_val;
}

/**
 * igb_power_up_phy_copper - Restore copper link in case of PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, restore the link to previous settings.
 **/
void igb_power_up_phy_copper(struct e1000_hw *hw)
{
	u16 mii_reg = 0;

	/* The PHY will retain its settings across a power down/up cycle */
	hw->phy.ops.read_reg(hw, PHY_CONTROL, &mii_reg);
	mii_reg &= ~MII_CR_POWER_DOWN;
	hw->phy.ops.write_reg(hw, PHY_CONTROL, mii_reg);
}

/**
 * igb_power_down_phy_copper - Power down copper PHY
 * @hw: pointer to the HW structure
 *
 * Power down PHY to save power when interface is down and wake on lan
 * is not enabled.
 **/
void igb_power_down_phy_copper(struct e1000_hw *hw)
{
	u16 mii_reg = 0;

	/* The PHY will retain its settings across a power down/up cycle */
	hw->phy.ops.read_reg(hw, PHY_CONTROL, &mii_reg);
	mii_reg |= MII_CR_POWER_DOWN;
	hw->phy.ops.write_reg(hw, PHY_CONTROL, mii_reg);
	usleep_range(1000, 2000);
}

/**
 *  igb_check_polarity_82580 - Checks the polarity.
 *  @hw: pointer to the HW structure
 *
 *  Success returns 0, Failure returns -E1000_ERR_PHY (-2)
 *
 *  Polarity is determined based on the PHY specific status register.
 **/
static s32 igb_check_polarity_82580(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;


	ret_val = phy->ops.read_reg(hw, I82580_PHY_STATUS_2, &data);

	if (!ret_val)
		phy->cable_polarity = (data & I82580_PHY_STATUS2_REV_POLARITY)
				      ? e1000_rev_polarity_reversed
				      : e1000_rev_polarity_normal;

	return ret_val;
}

/**
 *  igb_phy_force_speed_duplex_82580 - Force speed/duplex for I82580 PHY
 *  @hw: pointer to the HW structure
 *
 *  Calls the PHY setup function to force speed and duplex.  Clears the
 *  auto-crossover to force MDI manually.  Waits for link and returns
 *  successful if link up is successful, else -E1000_ERR_PHY (-2).
 **/
s32 igb_phy_force_speed_duplex_82580(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data;
	bool link;

	ret_val = phy->ops.read_reg(hw, PHY_CONTROL, &phy_data);
	if (ret_val)
		goto out;

	igb_phy_force_speed_duplex_setup(hw, &phy_data);

	ret_val = phy->ops.write_reg(hw, PHY_CONTROL, phy_data);
	if (ret_val)
		goto out;

	/* Clear Auto-Crossover to force MDI manually.  82580 requires MDI
	 * forced whenever speed and duplex are forced.
	 */
	ret_val = phy->ops.read_reg(hw, I82580_PHY_CTRL_2, &phy_data);
	if (ret_val)
		goto out;

	phy_data &= ~I82580_PHY_CTRL2_MDIX_CFG_MASK;

	ret_val = phy->ops.write_reg(hw, I82580_PHY_CTRL_2, phy_data);
	if (ret_val)
		goto out;

	hw_dbg("I82580_PHY_CTRL_2: %X\n", phy_data);

	udelay(1);

	if (phy->autoneg_wait_to_complete) {
		hw_dbg("Waiting for forced speed/duplex link on 82580 phy\n");

		ret_val = igb_phy_has_link(hw, PHY_FORCE_LIMIT, 100000, &link);
		if (ret_val)
			goto out;

		if (!link)
			hw_dbg("Link taking longer than expected.\n");

		/* Try once more */
		ret_val = igb_phy_has_link(hw, PHY_FORCE_LIMIT, 100000, &link);
		if (ret_val)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_get_phy_info_82580 - Retrieve I82580 PHY information
 *  @hw: pointer to the HW structure
 *
 *  Read PHY status to determine if link is up.  If link is up, then
 *  set/determine 10base-T extended distance and polarity correction.  Read
 *  PHY port status to determine MDI/MDIx and speed.  Based on the speed,
 *  determine on the cable length, local and remote receiver.
 **/
s32 igb_get_phy_info_82580(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;
	bool link;

	ret_val = igb_phy_has_link(hw, 1, 0, &link);
	if (ret_val)
		goto out;

	if (!link) {
		hw_dbg("Phy info is only valid if link is up\n");
		ret_val = -E1000_ERR_CONFIG;
		goto out;
	}

	phy->polarity_correction = true;

	ret_val = igb_check_polarity_82580(hw);
	if (ret_val)
		goto out;

	ret_val = phy->ops.read_reg(hw, I82580_PHY_STATUS_2, &data);
	if (ret_val)
		goto out;

	phy->is_mdix = (data & I82580_PHY_STATUS2_MDIX) ? true : false;

	if ((data & I82580_PHY_STATUS2_SPEED_MASK) ==
	    I82580_PHY_STATUS2_SPEED_1000MBPS) {
		ret_val = hw->phy.ops.get_cable_length(hw);
		if (ret_val)
			goto out;

		ret_val = phy->ops.read_reg(hw, PHY_1000T_STATUS, &data);
		if (ret_val)
			goto out;

		phy->local_rx = (data & SR_1000T_LOCAL_RX_STATUS)
				? e1000_1000t_rx_status_ok
				: e1000_1000t_rx_status_not_ok;

		phy->remote_rx = (data & SR_1000T_REMOTE_RX_STATUS)
				 ? e1000_1000t_rx_status_ok
				 : e1000_1000t_rx_status_not_ok;
	} else {
		phy->cable_length = E1000_CABLE_LENGTH_UNDEFINED;
		phy->local_rx = e1000_1000t_rx_status_undefined;
		phy->remote_rx = e1000_1000t_rx_status_undefined;
	}

out:
	return ret_val;
}

/**
 *  igb_get_cable_length_82580 - Determine cable length for 82580 PHY
 *  @hw: pointer to the HW structure
 *
 * Reads the diagnostic status register and verifies result is valid before
 * placing it in the phy_cable_length field.
 **/
s32 igb_get_cable_length_82580(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data, length;

	ret_val = phy->ops.read_reg(hw, I82580_PHY_DIAG_STATUS, &phy_data);
	if (ret_val)
		goto out;

	length = (phy_data & I82580_DSTATUS_CABLE_LENGTH) >>
		 I82580_DSTATUS_CABLE_LENGTH_SHIFT;

	if (length == E1000_CABLE_LENGTH_UNDEFINED)
		ret_val = -E1000_ERR_PHY;

	phy->cable_length = length;

out:
	return ret_val;
}

/**
 *  igb_write_phy_reg_gs40g - Write GS40G PHY register
 *  @hw: pointer to the HW structure
 *  @offset: lower half is register offset to write to
 *     upper half is page to use.
 *  @data: data to write at register offset
 *
 *  Acquires semaphore, if necessary, then writes the data to PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
s32 igb_write_phy_reg_gs40g(struct e1000_hw *hw, u32 offset, u16 data)
{
	s32 ret_val;
	u16 page = offset >> GS40G_PAGE_SHIFT;

	offset = offset & GS40G_OFFSET_MASK;
	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	ret_val = igb_write_phy_reg_mdic(hw, GS40G_PAGE_SELECT, page);
	if (ret_val)
		goto release;
	ret_val = igb_write_phy_reg_mdic(hw, offset, data);

release:
	hw->phy.ops.release(hw);
	return ret_val;
}

/**
 *  igb_read_phy_reg_gs40g - Read GS40G  PHY register
 *  @hw: pointer to the HW structure
 *  @offset: lower half is register offset to read to
 *     upper half is page to use.
 *  @data: data to read at register offset
 *
 *  Acquires semaphore, if necessary, then reads the data in the PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
s32 igb_read_phy_reg_gs40g(struct e1000_hw *hw, u32 offset, u16 *data)
{
	s32 ret_val;
	u16 page = offset >> GS40G_PAGE_SHIFT;

	offset = offset & GS40G_OFFSET_MASK;
	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	ret_val = igb_write_phy_reg_mdic(hw, GS40G_PAGE_SELECT, page);
	if (ret_val)
		goto release;
	ret_val = igb_read_phy_reg_mdic(hw, offset, data);

release:
	hw->phy.ops.release(hw);
	return ret_val;
}

/**
 *  igb_set_master_slave_mode - Setup PHY for Master/slave mode
 *  @hw: pointer to the HW structure
 *
 *  Sets up Master/slave mode
 **/
static s32 igb_set_master_slave_mode(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 phy_data;

	/* Resolve Master/Slave mode */
	ret_val = hw->phy.ops.read_reg(hw, PHY_1000T_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	/* load defaults for future use */
	hw->phy.original_ms_type = (phy_data & CR_1000T_MS_ENABLE) ?
				   ((phy_data & CR_1000T_MS_VALUE) ?
				    e1000_ms_force_master :
				    e1000_ms_force_slave) : e1000_ms_auto;

	switch (hw->phy.ms_type) {
	case e1000_ms_force_master:
		phy_data |= (CR_1000T_MS_ENABLE | CR_1000T_MS_VALUE);
		break;
	case e1000_ms_force_slave:
		phy_data |= CR_1000T_MS_ENABLE;
		phy_data &= ~(CR_1000T_MS_VALUE);
		break;
	case e1000_ms_auto:
		phy_data &= ~CR_1000T_MS_ENABLE;
		/* fall-through */
	default:
		break;
	}

	return hw->phy.ops.write_reg(hw, PHY_1000T_CTRL, phy_data);
}
