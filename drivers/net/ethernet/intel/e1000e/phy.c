/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2013 Intel Corporation.

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

#include "e1000.h"

static s32 e1000_wait_autoneg(struct e1000_hw *hw);
static s32 e1000_access_phy_wakeup_reg_bm(struct e1000_hw *hw, u32 offset,
					  u16 *data, bool read, bool page_set);
static u32 e1000_get_phy_addr_for_hv_page(u32 page);
static s32 e1000_access_phy_debug_regs_hv(struct e1000_hw *hw, u32 offset,
					  u16 *data, bool read);

/* Cable length tables */
static const u16 e1000_m88_cable_length_table[] = {
	0, 50, 80, 110, 140, 140, E1000_CABLE_LENGTH_UNDEFINED };
#define M88E1000_CABLE_LENGTH_TABLE_SIZE \
		ARRAY_SIZE(e1000_m88_cable_length_table)

static const u16 e1000_igp_2_cable_length_table[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 3, 5, 8, 11, 13, 16, 18, 21, 0, 0, 0, 3,
	6, 10, 13, 16, 19, 23, 26, 29, 32, 35, 38, 41, 6, 10, 14, 18, 22,
	26, 30, 33, 37, 41, 44, 48, 51, 54, 58, 61, 21, 26, 31, 35, 40,
	44, 49, 53, 57, 61, 65, 68, 72, 75, 79, 82, 40, 45, 51, 56, 61,
	66, 70, 75, 79, 83, 87, 91, 94, 98, 101, 104, 60, 66, 72, 77, 82,
	87, 92, 96, 100, 104, 108, 111, 114, 117, 119, 121, 83, 89, 95,
	100, 105, 109, 113, 116, 119, 122, 124, 104, 109, 114, 118, 121,
	124};
#define IGP02E1000_CABLE_LENGTH_TABLE_SIZE \
		ARRAY_SIZE(e1000_igp_2_cable_length_table)

#define BM_PHY_REG_PAGE(offset) \
	((u16)(((offset) >> PHY_PAGE_SHIFT) & 0xFFFF))
#define BM_PHY_REG_NUM(offset) \
	((u16)(((offset) & MAX_PHY_REG_ADDRESS) |\
	 (((offset) >> (PHY_UPPER_SHIFT - PHY_PAGE_SHIFT)) &\
		~MAX_PHY_REG_ADDRESS)))

#define HV_INTC_FC_PAGE_START             768
#define I82578_ADDR_REG                   29
#define I82577_ADDR_REG                   16
#define I82577_CFG_REG                    22
#define I82577_CFG_ASSERT_CRS_ON_TX       (1 << 15)
#define I82577_CFG_ENABLE_DOWNSHIFT       (3 << 10) /* auto downshift 100/10 */
#define I82577_CTRL_REG                   23

/* 82577 specific PHY registers */
#define I82577_PHY_CTRL_2            18
#define I82577_PHY_STATUS_2          26
#define I82577_PHY_DIAG_STATUS       31

/* I82577 PHY Status 2 */
#define I82577_PHY_STATUS2_REV_POLARITY   0x0400
#define I82577_PHY_STATUS2_MDIX           0x0800
#define I82577_PHY_STATUS2_SPEED_MASK     0x0300
#define I82577_PHY_STATUS2_SPEED_1000MBPS 0x0200

/* I82577 PHY Control 2 */
#define I82577_PHY_CTRL2_MANUAL_MDIX      0x0200
#define I82577_PHY_CTRL2_AUTO_MDI_MDIX    0x0400
#define I82577_PHY_CTRL2_MDIX_CFG_MASK    0x0600

/* I82577 PHY Diagnostics Status */
#define I82577_DSTATUS_CABLE_LENGTH       0x03FC
#define I82577_DSTATUS_CABLE_LENGTH_SHIFT 2

/* BM PHY Copper Specific Control 1 */
#define BM_CS_CTRL1                       16

#define HV_MUX_DATA_CTRL               PHY_REG(776, 16)
#define HV_MUX_DATA_CTRL_GEN_TO_MAC    0x0400
#define HV_MUX_DATA_CTRL_FORCE_SPEED   0x0004

/**
 *  e1000e_check_reset_block_generic - Check if PHY reset is blocked
 *  @hw: pointer to the HW structure
 *
 *  Read the PHY management control register and check whether a PHY reset
 *  is blocked.  If a reset is not blocked return 0, otherwise
 *  return E1000_BLK_PHY_RESET (12).
 **/
s32 e1000e_check_reset_block_generic(struct e1000_hw *hw)
{
	u32 manc;

	manc = er32(MANC);

	return (manc & E1000_MANC_BLK_PHY_RST_ON_IDE) ?
	       E1000_BLK_PHY_RESET : 0;
}

/**
 *  e1000e_get_phy_id - Retrieve the PHY ID and revision
 *  @hw: pointer to the HW structure
 *
 *  Reads the PHY registers and stores the PHY ID and possibly the PHY
 *  revision in the hardware structure.
 **/
s32 e1000e_get_phy_id(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = 0;
	u16 phy_id;
	u16 retry_count = 0;

	if (!phy->ops.read_reg)
		return 0;

	while (retry_count < 2) {
		ret_val = e1e_rphy(hw, PHY_ID1, &phy_id);
		if (ret_val)
			return ret_val;

		phy->id = (u32)(phy_id << 16);
		udelay(20);
		ret_val = e1e_rphy(hw, PHY_ID2, &phy_id);
		if (ret_val)
			return ret_val;

		phy->id |= (u32)(phy_id & PHY_REVISION_MASK);
		phy->revision = (u32)(phy_id & ~PHY_REVISION_MASK);

		if (phy->id != 0 && phy->id != PHY_REVISION_MASK)
			return 0;

		retry_count++;
	}

	return 0;
}

/**
 *  e1000e_phy_reset_dsp - Reset PHY DSP
 *  @hw: pointer to the HW structure
 *
 *  Reset the digital signal processor.
 **/
s32 e1000e_phy_reset_dsp(struct e1000_hw *hw)
{
	s32 ret_val;

	ret_val = e1e_wphy(hw, M88E1000_PHY_GEN_CONTROL, 0xC1);
	if (ret_val)
		return ret_val;

	return e1e_wphy(hw, M88E1000_PHY_GEN_CONTROL, 0);
}

/**
 *  e1000e_read_phy_reg_mdic - Read MDI control register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the MDI control register in the PHY at offset and stores the
 *  information read to data.
 **/
s32 e1000e_read_phy_reg_mdic(struct e1000_hw *hw, u32 offset, u16 *data)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 i, mdic = 0;

	if (offset > MAX_PHY_REG_ADDRESS) {
		e_dbg("PHY Address %d is out of range\n", offset);
		return -E1000_ERR_PARAM;
	}

	/* Set up Op-code, Phy Address, and register offset in the MDI
	 * Control register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	mdic = ((offset << E1000_MDIC_REG_SHIFT) |
		(phy->addr << E1000_MDIC_PHY_SHIFT) |
		(E1000_MDIC_OP_READ));

	ew32(MDIC, mdic);

	/* Poll the ready bit to see if the MDI read completed
	 * Increasing the time out as testing showed failures with
	 * the lower time out
	 */
	for (i = 0; i < (E1000_GEN_POLL_TIMEOUT * 3); i++) {
		udelay(50);
		mdic = er32(MDIC);
		if (mdic & E1000_MDIC_READY)
			break;
	}
	if (!(mdic & E1000_MDIC_READY)) {
		e_dbg("MDI Read did not complete\n");
		return -E1000_ERR_PHY;
	}
	if (mdic & E1000_MDIC_ERROR) {
		e_dbg("MDI Error\n");
		return -E1000_ERR_PHY;
	}
	*data = (u16) mdic;

	/* Allow some time after each MDIC transaction to avoid
	 * reading duplicate data in the next MDIC transaction.
	 */
	if (hw->mac.type == e1000_pch2lan)
		udelay(100);

	return 0;
}

/**
 *  e1000e_write_phy_reg_mdic - Write MDI control register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write to register at offset
 *
 *  Writes data to MDI control register in the PHY at offset.
 **/
s32 e1000e_write_phy_reg_mdic(struct e1000_hw *hw, u32 offset, u16 data)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 i, mdic = 0;

	if (offset > MAX_PHY_REG_ADDRESS) {
		e_dbg("PHY Address %d is out of range\n", offset);
		return -E1000_ERR_PARAM;
	}

	/* Set up Op-code, Phy Address, and register offset in the MDI
	 * Control register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	mdic = (((u32)data) |
		(offset << E1000_MDIC_REG_SHIFT) |
		(phy->addr << E1000_MDIC_PHY_SHIFT) |
		(E1000_MDIC_OP_WRITE));

	ew32(MDIC, mdic);

	/* Poll the ready bit to see if the MDI read completed
	 * Increasing the time out as testing showed failures with
	 * the lower time out
	 */
	for (i = 0; i < (E1000_GEN_POLL_TIMEOUT * 3); i++) {
		udelay(50);
		mdic = er32(MDIC);
		if (mdic & E1000_MDIC_READY)
			break;
	}
	if (!(mdic & E1000_MDIC_READY)) {
		e_dbg("MDI Write did not complete\n");
		return -E1000_ERR_PHY;
	}
	if (mdic & E1000_MDIC_ERROR) {
		e_dbg("MDI Error\n");
		return -E1000_ERR_PHY;
	}

	/* Allow some time after each MDIC transaction to avoid
	 * reading duplicate data in the next MDIC transaction.
	 */
	if (hw->mac.type == e1000_pch2lan)
		udelay(100);

	return 0;
}

/**
 *  e1000e_read_phy_reg_m88 - Read m88 PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Acquires semaphore, if necessary, then reads the PHY register at offset
 *  and storing the retrieved information in data.  Release any acquired
 *  semaphores before exiting.
 **/
s32 e1000e_read_phy_reg_m88(struct e1000_hw *hw, u32 offset, u16 *data)
{
	s32 ret_val;

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	ret_val = e1000e_read_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS & offset,
					   data);

	hw->phy.ops.release(hw);

	return ret_val;
}

/**
 *  e1000e_write_phy_reg_m88 - Write m88 PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Acquires semaphore, if necessary, then writes the data to PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
s32 e1000e_write_phy_reg_m88(struct e1000_hw *hw, u32 offset, u16 data)
{
	s32 ret_val;

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	ret_val = e1000e_write_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS & offset,
					    data);

	hw->phy.ops.release(hw);

	return ret_val;
}

/**
 *  e1000_set_page_igp - Set page as on IGP-like PHY(s)
 *  @hw: pointer to the HW structure
 *  @page: page to set (shifted left when necessary)
 *
 *  Sets PHY page required for PHY register access.  Assumes semaphore is
 *  already acquired.  Note, this function sets phy.addr to 1 so the caller
 *  must set it appropriately (if necessary) after this function returns.
 **/
s32 e1000_set_page_igp(struct e1000_hw *hw, u16 page)
{
	e_dbg("Setting page 0x%x\n", page);

	hw->phy.addr = 1;

	return e1000e_write_phy_reg_mdic(hw, IGP01E1000_PHY_PAGE_SELECT, page);
}

/**
 *  __e1000e_read_phy_reg_igp - Read igp PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *  @locked: semaphore has already been acquired or not
 *
 *  Acquires semaphore, if necessary, then reads the PHY register at offset
 *  and stores the retrieved information in data.  Release any acquired
 *  semaphores before exiting.
 **/
static s32 __e1000e_read_phy_reg_igp(struct e1000_hw *hw, u32 offset, u16 *data,
                                    bool locked)
{
	s32 ret_val = 0;

	if (!locked) {
		if (!hw->phy.ops.acquire)
			return 0;

		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
	}

	if (offset > MAX_PHY_MULTI_PAGE_REG)
		ret_val = e1000e_write_phy_reg_mdic(hw,
						    IGP01E1000_PHY_PAGE_SELECT,
						    (u16)offset);
	if (!ret_val)
		ret_val = e1000e_read_phy_reg_mdic(hw,
						   MAX_PHY_REG_ADDRESS & offset,
						   data);
	if (!locked)
		hw->phy.ops.release(hw);

	return ret_val;
}

/**
 *  e1000e_read_phy_reg_igp - Read igp PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Acquires semaphore then reads the PHY register at offset and stores the
 *  retrieved information in data.
 *  Release the acquired semaphore before exiting.
 **/
s32 e1000e_read_phy_reg_igp(struct e1000_hw *hw, u32 offset, u16 *data)
{
	return __e1000e_read_phy_reg_igp(hw, offset, data, false);
}

/**
 *  e1000e_read_phy_reg_igp_locked - Read igp PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the PHY register at offset and stores the retrieved information
 *  in data.  Assumes semaphore already acquired.
 **/
s32 e1000e_read_phy_reg_igp_locked(struct e1000_hw *hw, u32 offset, u16 *data)
{
	return __e1000e_read_phy_reg_igp(hw, offset, data, true);
}

/**
 *  e1000e_write_phy_reg_igp - Write igp PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *  @locked: semaphore has already been acquired or not
 *
 *  Acquires semaphore, if necessary, then writes the data to PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
static s32 __e1000e_write_phy_reg_igp(struct e1000_hw *hw, u32 offset, u16 data,
                                     bool locked)
{
	s32 ret_val = 0;

	if (!locked) {
		if (!hw->phy.ops.acquire)
			return 0;

		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
	}

	if (offset > MAX_PHY_MULTI_PAGE_REG)
		ret_val = e1000e_write_phy_reg_mdic(hw,
						    IGP01E1000_PHY_PAGE_SELECT,
						    (u16)offset);
	if (!ret_val)
		ret_val = e1000e_write_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS &
							offset,
						    data);
	if (!locked)
		hw->phy.ops.release(hw);

	return ret_val;
}

/**
 *  e1000e_write_phy_reg_igp - Write igp PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Acquires semaphore then writes the data to PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
s32 e1000e_write_phy_reg_igp(struct e1000_hw *hw, u32 offset, u16 data)
{
	return __e1000e_write_phy_reg_igp(hw, offset, data, false);
}

/**
 *  e1000e_write_phy_reg_igp_locked - Write igp PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Writes the data to PHY register at the offset.
 *  Assumes semaphore already acquired.
 **/
s32 e1000e_write_phy_reg_igp_locked(struct e1000_hw *hw, u32 offset, u16 data)
{
	return __e1000e_write_phy_reg_igp(hw, offset, data, true);
}

/**
 *  __e1000_read_kmrn_reg - Read kumeran register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *  @locked: semaphore has already been acquired or not
 *
 *  Acquires semaphore, if necessary.  Then reads the PHY register at offset
 *  using the kumeran interface.  The information retrieved is stored in data.
 *  Release any acquired semaphores before exiting.
 **/
static s32 __e1000_read_kmrn_reg(struct e1000_hw *hw, u32 offset, u16 *data,
                                 bool locked)
{
	u32 kmrnctrlsta;

	if (!locked) {
		s32 ret_val = 0;

		if (!hw->phy.ops.acquire)
			return 0;

		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
	}

	kmrnctrlsta = ((offset << E1000_KMRNCTRLSTA_OFFSET_SHIFT) &
		       E1000_KMRNCTRLSTA_OFFSET) | E1000_KMRNCTRLSTA_REN;
	ew32(KMRNCTRLSTA, kmrnctrlsta);
	e1e_flush();

	udelay(2);

	kmrnctrlsta = er32(KMRNCTRLSTA);
	*data = (u16)kmrnctrlsta;

	if (!locked)
		hw->phy.ops.release(hw);

	return 0;
}

/**
 *  e1000e_read_kmrn_reg -  Read kumeran register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Acquires semaphore then reads the PHY register at offset using the
 *  kumeran interface.  The information retrieved is stored in data.
 *  Release the acquired semaphore before exiting.
 **/
s32 e1000e_read_kmrn_reg(struct e1000_hw *hw, u32 offset, u16 *data)
{
	return __e1000_read_kmrn_reg(hw, offset, data, false);
}

/**
 *  e1000e_read_kmrn_reg_locked -  Read kumeran register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the PHY register at offset using the kumeran interface.  The
 *  information retrieved is stored in data.
 *  Assumes semaphore already acquired.
 **/
s32 e1000e_read_kmrn_reg_locked(struct e1000_hw *hw, u32 offset, u16 *data)
{
	return __e1000_read_kmrn_reg(hw, offset, data, true);
}

/**
 *  __e1000_write_kmrn_reg - Write kumeran register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *  @locked: semaphore has already been acquired or not
 *
 *  Acquires semaphore, if necessary.  Then write the data to PHY register
 *  at the offset using the kumeran interface.  Release any acquired semaphores
 *  before exiting.
 **/
static s32 __e1000_write_kmrn_reg(struct e1000_hw *hw, u32 offset, u16 data,
                                  bool locked)
{
	u32 kmrnctrlsta;

	if (!locked) {
		s32 ret_val = 0;

		if (!hw->phy.ops.acquire)
			return 0;

		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
	}

	kmrnctrlsta = ((offset << E1000_KMRNCTRLSTA_OFFSET_SHIFT) &
		       E1000_KMRNCTRLSTA_OFFSET) | data;
	ew32(KMRNCTRLSTA, kmrnctrlsta);
	e1e_flush();

	udelay(2);

	if (!locked)
		hw->phy.ops.release(hw);

	return 0;
}

/**
 *  e1000e_write_kmrn_reg -  Write kumeran register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Acquires semaphore then writes the data to the PHY register at the offset
 *  using the kumeran interface.  Release the acquired semaphore before exiting.
 **/
s32 e1000e_write_kmrn_reg(struct e1000_hw *hw, u32 offset, u16 data)
{
	return __e1000_write_kmrn_reg(hw, offset, data, false);
}

/**
 *  e1000e_write_kmrn_reg_locked -  Write kumeran register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Write the data to PHY register at the offset using the kumeran interface.
 *  Assumes semaphore already acquired.
 **/
s32 e1000e_write_kmrn_reg_locked(struct e1000_hw *hw, u32 offset, u16 data)
{
	return __e1000_write_kmrn_reg(hw, offset, data, true);
}

/**
 *  e1000_set_master_slave_mode - Setup PHY for Master/slave mode
 *  @hw: pointer to the HW structure
 *
 *  Sets up Master/slave mode
 **/
static s32 e1000_set_master_slave_mode(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 phy_data;

	/* Resolve Master/Slave mode */
	ret_val = e1e_rphy(hw, PHY_1000T_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	/* load defaults for future use */
	hw->phy.original_ms_type = (phy_data & CR_1000T_MS_ENABLE) ?
	    ((phy_data & CR_1000T_MS_VALUE) ?
	     e1000_ms_force_master : e1000_ms_force_slave) : e1000_ms_auto;

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

	return e1e_wphy(hw, PHY_1000T_CTRL, phy_data);
}

/**
 *  e1000_copper_link_setup_82577 - Setup 82577 PHY for copper link
 *  @hw: pointer to the HW structure
 *
 *  Sets up Carrier-sense on Transmit and downshift values.
 **/
s32 e1000_copper_link_setup_82577(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 phy_data;

	/* Enable CRS on Tx. This must be set for half-duplex operation. */
	ret_val = e1e_rphy(hw, I82577_CFG_REG, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data |= I82577_CFG_ASSERT_CRS_ON_TX;

	/* Enable downshift */
	phy_data |= I82577_CFG_ENABLE_DOWNSHIFT;

	ret_val = e1e_wphy(hw, I82577_CFG_REG, phy_data);
	if (ret_val)
		return ret_val;

	/* Set MDI/MDIX mode */
	ret_val = e1e_rphy(hw, I82577_PHY_CTRL_2, &phy_data);
	if (ret_val)
		return ret_val;
	phy_data &= ~I82577_PHY_CTRL2_MDIX_CFG_MASK;
	/* Options:
	 *   0 - Auto (default)
	 *   1 - MDI mode
	 *   2 - MDI-X mode
	 */
	switch (hw->phy.mdix) {
	case 1:
		break;
	case 2:
		phy_data |= I82577_PHY_CTRL2_MANUAL_MDIX;
		break;
	case 0:
	default:
		phy_data |= I82577_PHY_CTRL2_AUTO_MDI_MDIX;
		break;
	}
	ret_val = e1e_wphy(hw, I82577_PHY_CTRL_2, phy_data);
	if (ret_val)
		return ret_val;

	return e1000_set_master_slave_mode(hw);
}

/**
 *  e1000e_copper_link_setup_m88 - Setup m88 PHY's for copper link
 *  @hw: pointer to the HW structure
 *
 *  Sets up MDI/MDI-X and polarity for m88 PHY's.  If necessary, transmit clock
 *  and downshift values are set also.
 **/
s32 e1000e_copper_link_setup_m88(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data;

	/* Enable CRS on Tx. This must be set for half-duplex operation. */
	ret_val = e1e_rphy(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	/* For BM PHY this bit is downshift enable */
	if (phy->type != e1000_phy_bm)
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
	if (phy->disable_polarity_correction)
		phy_data |= M88E1000_PSCR_POLARITY_REVERSAL;

	/* Enable downshift on BM (disabled by default) */
	if (phy->type == e1000_phy_bm) {
		/* For 82574/82583, first disable then enable downshift */
		if (phy->id == BME1000_E_PHY_ID_R2) {
			phy_data &= ~BME1000_PSCR_ENABLE_DOWNSHIFT;
			ret_val = e1e_wphy(hw, M88E1000_PHY_SPEC_CTRL,
					   phy_data);
			if (ret_val)
				return ret_val;
			/* Commit the changes. */
			ret_val = phy->ops.commit(hw);
			if (ret_val) {
				e_dbg("Error committing the PHY changes\n");
				return ret_val;
			}
		}

		phy_data |= BME1000_PSCR_ENABLE_DOWNSHIFT;
	}

	ret_val = e1e_wphy(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
	if (ret_val)
		return ret_val;

	if ((phy->type == e1000_phy_m88) &&
	    (phy->revision < E1000_REVISION_4) &&
	    (phy->id != BME1000_E_PHY_ID_R2)) {
		/* Force TX_CLK in the Extended PHY Specific Control Register
		 * to 25MHz clock.
		 */
		ret_val = e1e_rphy(hw, M88E1000_EXT_PHY_SPEC_CTRL, &phy_data);
		if (ret_val)
			return ret_val;

		phy_data |= M88E1000_EPSCR_TX_CLK_25;

		if ((phy->revision == 2) &&
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
		ret_val = e1e_wphy(hw, M88E1000_EXT_PHY_SPEC_CTRL, phy_data);
		if (ret_val)
			return ret_val;
	}

	if ((phy->type == e1000_phy_bm) && (phy->id == BME1000_E_PHY_ID_R2)) {
		/* Set PHY page 0, register 29 to 0x0003 */
		ret_val = e1e_wphy(hw, 29, 0x0003);
		if (ret_val)
			return ret_val;

		/* Set PHY page 0, register 30 to 0x0000 */
		ret_val = e1e_wphy(hw, 30, 0x0000);
		if (ret_val)
			return ret_val;
	}

	/* Commit the changes. */
	if (phy->ops.commit) {
		ret_val = phy->ops.commit(hw);
		if (ret_val) {
			e_dbg("Error committing the PHY changes\n");
			return ret_val;
		}
	}

	if (phy->type == e1000_phy_82578) {
		ret_val = e1e_rphy(hw, M88E1000_EXT_PHY_SPEC_CTRL, &phy_data);
		if (ret_val)
			return ret_val;

		/* 82578 PHY - set the downshift count to 1x. */
		phy_data |= I82578_EPSCR_DOWNSHIFT_ENABLE;
		phy_data &= ~I82578_EPSCR_DOWNSHIFT_COUNTER_MASK;
		ret_val = e1e_wphy(hw, M88E1000_EXT_PHY_SPEC_CTRL, phy_data);
		if (ret_val)
			return ret_val;
	}

	return 0;
}

/**
 *  e1000e_copper_link_setup_igp - Setup igp PHY's for copper link
 *  @hw: pointer to the HW structure
 *
 *  Sets up LPLU, MDI/MDI-X, polarity, Smartspeed and Master/Slave config for
 *  igp PHY's.
 **/
s32 e1000e_copper_link_setup_igp(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	ret_val = e1000_phy_hw_reset(hw);
	if (ret_val) {
		e_dbg("Error resetting the PHY.\n");
		return ret_val;
	}

	/* Wait 100ms for MAC to configure PHY from NVM settings, to avoid
	 * timeout issues when LFS is enabled.
	 */
	msleep(100);

	/* disable lplu d0 during driver init */
	if (hw->phy.ops.set_d0_lplu_state) {
		ret_val = hw->phy.ops.set_d0_lplu_state(hw, false);
		if (ret_val) {
			e_dbg("Error Disabling LPLU D0\n");
			return ret_val;
		}
	}
	/* Configure mdi-mdix settings */
	ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_CTRL, &data);
	if (ret_val)
		return ret_val;

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
	ret_val = e1e_wphy(hw, IGP01E1000_PHY_PORT_CTRL, data);
	if (ret_val)
		return ret_val;

	/* set auto-master slave resolution settings */
	if (hw->mac.autoneg) {
		/* when autonegotiation advertisement is only 1000Mbps then we
		 * should disable SmartSpeed and enable Auto MasterSlave
		 * resolution as hardware default.
		 */
		if (phy->autoneg_advertised == ADVERTISE_1000_FULL) {
			/* Disable SmartSpeed */
			ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_CONFIG,
					   &data);
			if (ret_val)
				return ret_val;

			data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = e1e_wphy(hw, IGP01E1000_PHY_PORT_CONFIG,
					   data);
			if (ret_val)
				return ret_val;

			/* Set auto Master/Slave resolution process */
			ret_val = e1e_rphy(hw, PHY_1000T_CTRL, &data);
			if (ret_val)
				return ret_val;

			data &= ~CR_1000T_MS_ENABLE;
			ret_val = e1e_wphy(hw, PHY_1000T_CTRL, data);
			if (ret_val)
				return ret_val;
		}

		ret_val = e1000_set_master_slave_mode(hw);
	}

	return ret_val;
}

/**
 *  e1000_phy_setup_autoneg - Configure PHY for auto-negotiation
 *  @hw: pointer to the HW structure
 *
 *  Reads the MII auto-neg advertisement register and/or the 1000T control
 *  register and if the PHY is already setup for auto-negotiation, then
 *  return successful.  Otherwise, setup advertisement and flow control to
 *  the appropriate values for the wanted auto-negotiation.
 **/
static s32 e1000_phy_setup_autoneg(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 mii_autoneg_adv_reg;
	u16 mii_1000t_ctrl_reg = 0;

	phy->autoneg_advertised &= phy->autoneg_mask;

	/* Read the MII Auto-Neg Advertisement Register (Address 4). */
	ret_val = e1e_rphy(hw, PHY_AUTONEG_ADV, &mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	if (phy->autoneg_mask & ADVERTISE_1000_FULL) {
		/* Read the MII 1000Base-T Control Register (Address 9). */
		ret_val = e1e_rphy(hw, PHY_1000T_CTRL, &mii_1000t_ctrl_reg);
		if (ret_val)
			return ret_val;
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

	e_dbg("autoneg_advertised %x\n", phy->autoneg_advertised);

	/* Do we want to advertise 10 Mb Half Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_10_HALF) {
		e_dbg("Advertise 10mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_HD_CAPS;
	}

	/* Do we want to advertise 10 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_10_FULL) {
		e_dbg("Advertise 10mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_FD_CAPS;
	}

	/* Do we want to advertise 100 Mb Half Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_100_HALF) {
		e_dbg("Advertise 100mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_HD_CAPS;
	}

	/* Do we want to advertise 100 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_100_FULL) {
		e_dbg("Advertise 100mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_FD_CAPS;
	}

	/* We do not allow the Phy to advertise 1000 Mb Half Duplex */
	if (phy->autoneg_advertised & ADVERTISE_1000_HALF)
		e_dbg("Advertise 1000mb Half duplex request denied!\n");

	/* Do we want to advertise 1000 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_1000_FULL) {
		e_dbg("Advertise 1000mb Full duplex\n");
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
	 *      3:  Both Rx and Tx flow control (symmetric) are enabled.
	 *  other:  No software override.  The flow control configuration
	 *          in the EEPROM is used.
	 */
	switch (hw->fc.current_mode) {
	case e1000_fc_none:
		/* Flow control (Rx & Tx) is completely disabled by a
		 * software over-ride.
		 */
		mii_autoneg_adv_reg &= ~(NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case e1000_fc_rx_pause:
		/* Rx Flow control is enabled, and Tx Flow control is
		 * disabled, by a software over-ride.
		 *
		 * Since there really isn't a way to advertise that we are
		 * capable of Rx Pause ONLY, we will advertise that we
		 * support both symmetric and asymmetric Rx PAUSE.  Later
		 * (in e1000e_config_fc_after_link_up) we will disable the
		 * hw's ability to send PAUSE frames.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case e1000_fc_tx_pause:
		/* Tx Flow control is enabled, and Rx Flow control is
		 * disabled, by a software over-ride.
		 */
		mii_autoneg_adv_reg |= NWAY_AR_ASM_DIR;
		mii_autoneg_adv_reg &= ~NWAY_AR_PAUSE;
		break;
	case e1000_fc_full:
		/* Flow control (both Rx and Tx) is enabled by a software
		 * over-ride.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	default:
		e_dbg("Flow control param set incorrectly\n");
		return -E1000_ERR_CONFIG;
	}

	ret_val = e1e_wphy(hw, PHY_AUTONEG_ADV, mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	e_dbg("Auto-Neg Advertising %x\n", mii_autoneg_adv_reg);

	if (phy->autoneg_mask & ADVERTISE_1000_FULL)
		ret_val = e1e_wphy(hw, PHY_1000T_CTRL, mii_1000t_ctrl_reg);

	return ret_val;
}

/**
 *  e1000_copper_link_autoneg - Setup/Enable autoneg for copper link
 *  @hw: pointer to the HW structure
 *
 *  Performs initial bounds checking on autoneg advertisement parameter, then
 *  configure to advertise the full capability.  Setup the PHY to autoneg
 *  and restart the negotiation process between the link partner.  If
 *  autoneg_wait_to_complete, then wait for autoneg to complete before exiting.
 **/
static s32 e1000_copper_link_autoneg(struct e1000_hw *hw)
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
	if (!phy->autoneg_advertised)
		phy->autoneg_advertised = phy->autoneg_mask;

	e_dbg("Reconfiguring auto-neg advertisement params\n");
	ret_val = e1000_phy_setup_autoneg(hw);
	if (ret_val) {
		e_dbg("Error Setting up Auto-Negotiation\n");
		return ret_val;
	}
	e_dbg("Restarting Auto-Neg\n");

	/* Restart auto-negotiation by setting the Auto Neg Enable bit and
	 * the Auto Neg Restart bit in the PHY control register.
	 */
	ret_val = e1e_rphy(hw, PHY_CONTROL, &phy_ctrl);
	if (ret_val)
		return ret_val;

	phy_ctrl |= (MII_CR_AUTO_NEG_EN | MII_CR_RESTART_AUTO_NEG);
	ret_val = e1e_wphy(hw, PHY_CONTROL, phy_ctrl);
	if (ret_val)
		return ret_val;

	/* Does the user want to wait for Auto-Neg to complete here, or
	 * check at a later time (for example, callback routine).
	 */
	if (phy->autoneg_wait_to_complete) {
		ret_val = e1000_wait_autoneg(hw);
		if (ret_val) {
			e_dbg("Error while waiting for autoneg to complete\n");
			return ret_val;
		}
	}

	hw->mac.get_link_status = true;

	return ret_val;
}

/**
 *  e1000e_setup_copper_link - Configure copper link settings
 *  @hw: pointer to the HW structure
 *
 *  Calls the appropriate function to configure the link for auto-neg or forced
 *  speed and duplex.  Then we check for link, once link is established calls
 *  to configure collision distance and flow control are called.  If link is
 *  not established, we return -E1000_ERR_PHY (-2).
 **/
s32 e1000e_setup_copper_link(struct e1000_hw *hw)
{
	s32 ret_val;
	bool link;

	if (hw->mac.autoneg) {
		/* Setup autoneg and flow control advertisement and perform
		 * autonegotiation.
		 */
		ret_val = e1000_copper_link_autoneg(hw);
		if (ret_val)
			return ret_val;
	} else {
		/* PHY will be set to 10H, 10F, 100H or 100F
		 * depending on user settings.
		 */
		e_dbg("Forcing Speed and Duplex\n");
		ret_val = hw->phy.ops.force_speed_duplex(hw);
		if (ret_val) {
			e_dbg("Error Forcing Speed and Duplex\n");
			return ret_val;
		}
	}

	/* Check link status. Wait up to 100 microseconds for link to become
	 * valid.
	 */
	ret_val = e1000e_phy_has_link_generic(hw, COPPER_LINK_UP_LIMIT, 10,
					      &link);
	if (ret_val)
		return ret_val;

	if (link) {
		e_dbg("Valid link established!!!\n");
		hw->mac.ops.config_collision_dist(hw);
		ret_val = e1000e_config_fc_after_link_up(hw);
	} else {
		e_dbg("Unable to establish link!!!\n");
	}

	return ret_val;
}

/**
 *  e1000e_phy_force_speed_duplex_igp - Force speed/duplex for igp PHY
 *  @hw: pointer to the HW structure
 *
 *  Calls the PHY setup function to force speed and duplex.  Clears the
 *  auto-crossover to force MDI manually.  Waits for link and returns
 *  successful if link up is successful, else -E1000_ERR_PHY (-2).
 **/
s32 e1000e_phy_force_speed_duplex_igp(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data;
	bool link;

	ret_val = e1e_rphy(hw, PHY_CONTROL, &phy_data);
	if (ret_val)
		return ret_val;

	e1000e_phy_force_speed_duplex_setup(hw, &phy_data);

	ret_val = e1e_wphy(hw, PHY_CONTROL, phy_data);
	if (ret_val)
		return ret_val;

	/* Clear Auto-Crossover to force MDI manually.  IGP requires MDI
	 * forced whenever speed and duplex are forced.
	 */
	ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data &= ~IGP01E1000_PSCR_AUTO_MDIX;
	phy_data &= ~IGP01E1000_PSCR_FORCE_MDI_MDIX;

	ret_val = e1e_wphy(hw, IGP01E1000_PHY_PORT_CTRL, phy_data);
	if (ret_val)
		return ret_val;

	e_dbg("IGP PSCR: %X\n", phy_data);

	udelay(1);

	if (phy->autoneg_wait_to_complete) {
		e_dbg("Waiting for forced speed/duplex link on IGP phy.\n");

		ret_val = e1000e_phy_has_link_generic(hw, PHY_FORCE_LIMIT,
						      100000, &link);
		if (ret_val)
			return ret_val;

		if (!link)
			e_dbg("Link taking longer than expected.\n");

		/* Try once more */
		ret_val = e1000e_phy_has_link_generic(hw, PHY_FORCE_LIMIT,
						      100000, &link);
	}

	return ret_val;
}

/**
 *  e1000e_phy_force_speed_duplex_m88 - Force speed/duplex for m88 PHY
 *  @hw: pointer to the HW structure
 *
 *  Calls the PHY setup function to force speed and duplex.  Clears the
 *  auto-crossover to force MDI manually.  Resets the PHY to commit the
 *  changes.  If time expires while waiting for link up, we reset the DSP.
 *  After reset, TX_CLK and CRS on Tx must be set.  Return successful upon
 *  successful completion, else return corresponding error code.
 **/
s32 e1000e_phy_force_speed_duplex_m88(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data;
	bool link;

	/* Clear Auto-Crossover to force MDI manually.  M88E1000 requires MDI
	 * forced whenever speed and duplex are forced.
	 */
	ret_val = e1e_rphy(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data &= ~M88E1000_PSCR_AUTO_X_MODE;
	ret_val = e1e_wphy(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
	if (ret_val)
		return ret_val;

	e_dbg("M88E1000 PSCR: %X\n", phy_data);

	ret_val = e1e_rphy(hw, PHY_CONTROL, &phy_data);
	if (ret_val)
		return ret_val;

	e1000e_phy_force_speed_duplex_setup(hw, &phy_data);

	ret_val = e1e_wphy(hw, PHY_CONTROL, phy_data);
	if (ret_val)
		return ret_val;

	/* Reset the phy to commit changes. */
	if (hw->phy.ops.commit) {
		ret_val = hw->phy.ops.commit(hw);
		if (ret_val)
			return ret_val;
	}

	if (phy->autoneg_wait_to_complete) {
		e_dbg("Waiting for forced speed/duplex link on M88 phy.\n");

		ret_val = e1000e_phy_has_link_generic(hw, PHY_FORCE_LIMIT,
						     100000, &link);
		if (ret_val)
			return ret_val;

		if (!link) {
			if (hw->phy.type != e1000_phy_m88) {
				e_dbg("Link taking longer than expected.\n");
			} else {
				/* We didn't get link.
				 * Reset the DSP and cross our fingers.
				 */
				ret_val = e1e_wphy(hw, M88E1000_PHY_PAGE_SELECT,
						   0x001d);
				if (ret_val)
					return ret_val;
				ret_val = e1000e_phy_reset_dsp(hw);
				if (ret_val)
					return ret_val;
			}
		}

		/* Try once more */
		ret_val = e1000e_phy_has_link_generic(hw, PHY_FORCE_LIMIT,
						     100000, &link);
		if (ret_val)
			return ret_val;
	}

	if (hw->phy.type != e1000_phy_m88)
		return 0;

	ret_val = e1e_rphy(hw, M88E1000_EXT_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	/* Resetting the phy means we need to re-force TX_CLK in the
	 * Extended PHY Specific Control Register to 25MHz clock from
	 * the reset value of 2.5MHz.
	 */
	phy_data |= M88E1000_EPSCR_TX_CLK_25;
	ret_val = e1e_wphy(hw, M88E1000_EXT_PHY_SPEC_CTRL, phy_data);
	if (ret_val)
		return ret_val;

	/* In addition, we must re-enable CRS on Tx for both half and full
	 * duplex.
	 */
	ret_val = e1e_rphy(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data |= M88E1000_PSCR_ASSERT_CRS_ON_TX;
	ret_val = e1e_wphy(hw, M88E1000_PHY_SPEC_CTRL, phy_data);

	return ret_val;
}

/**
 *  e1000_phy_force_speed_duplex_ife - Force PHY speed & duplex
 *  @hw: pointer to the HW structure
 *
 *  Forces the speed and duplex settings of the PHY.
 *  This is a function pointer entry point only called by
 *  PHY setup routines.
 **/
s32 e1000_phy_force_speed_duplex_ife(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;
	bool link;

	ret_val = e1e_rphy(hw, PHY_CONTROL, &data);
	if (ret_val)
		return ret_val;

	e1000e_phy_force_speed_duplex_setup(hw, &data);

	ret_val = e1e_wphy(hw, PHY_CONTROL, data);
	if (ret_val)
		return ret_val;

	/* Disable MDI-X support for 10/100 */
	ret_val = e1e_rphy(hw, IFE_PHY_MDIX_CONTROL, &data);
	if (ret_val)
		return ret_val;

	data &= ~IFE_PMC_AUTO_MDIX;
	data &= ~IFE_PMC_FORCE_MDIX;

	ret_val = e1e_wphy(hw, IFE_PHY_MDIX_CONTROL, data);
	if (ret_val)
		return ret_val;

	e_dbg("IFE PMC: %X\n", data);

	udelay(1);

	if (phy->autoneg_wait_to_complete) {
		e_dbg("Waiting for forced speed/duplex link on IFE phy.\n");

		ret_val = e1000e_phy_has_link_generic(hw, PHY_FORCE_LIMIT,
						      100000, &link);
		if (ret_val)
			return ret_val;

		if (!link)
			e_dbg("Link taking longer than expected.\n");

		/* Try once more */
		ret_val = e1000e_phy_has_link_generic(hw, PHY_FORCE_LIMIT,
						      100000, &link);
		if (ret_val)
			return ret_val;
	}

	return 0;
}

/**
 *  e1000e_phy_force_speed_duplex_setup - Configure forced PHY speed/duplex
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
void e1000e_phy_force_speed_duplex_setup(struct e1000_hw *hw, u16 *phy_ctrl)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 ctrl;

	/* Turn off flow control when forcing speed/duplex */
	hw->fc.current_mode = e1000_fc_none;

	/* Force speed/duplex on the mac */
	ctrl = er32(CTRL);
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
		e_dbg("Half Duplex\n");
	} else {
		ctrl |= E1000_CTRL_FD;
		*phy_ctrl |= MII_CR_FULL_DUPLEX;
		e_dbg("Full Duplex\n");
	}

	/* Forcing 10mb or 100mb? */
	if (mac->forced_speed_duplex & E1000_ALL_100_SPEED) {
		ctrl |= E1000_CTRL_SPD_100;
		*phy_ctrl |= MII_CR_SPEED_100;
		*phy_ctrl &= ~(MII_CR_SPEED_1000 | MII_CR_SPEED_10);
		e_dbg("Forcing 100mb\n");
	} else {
		ctrl &= ~(E1000_CTRL_SPD_1000 | E1000_CTRL_SPD_100);
		*phy_ctrl |= MII_CR_SPEED_10;
		*phy_ctrl &= ~(MII_CR_SPEED_1000 | MII_CR_SPEED_100);
		e_dbg("Forcing 10mb\n");
	}

	hw->mac.ops.config_collision_dist(hw);

	ew32(CTRL, ctrl);
}

/**
 *  e1000e_set_d3_lplu_state - Sets low power link up state for D3
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
s32 e1000e_set_d3_lplu_state(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	ret_val = e1e_rphy(hw, IGP02E1000_PHY_POWER_MGMT, &data);
	if (ret_val)
		return ret_val;

	if (!active) {
		data &= ~IGP02E1000_PM_D3_LPLU;
		ret_val = e1e_wphy(hw, IGP02E1000_PHY_POWER_MGMT, data);
		if (ret_val)
			return ret_val;
		/* LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on) {
			ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_CONFIG,
					   &data);
			if (ret_val)
				return ret_val;

			data |= IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = e1e_wphy(hw, IGP01E1000_PHY_PORT_CONFIG,
					   data);
			if (ret_val)
				return ret_val;
		} else if (phy->smart_speed == e1000_smart_speed_off) {
			ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_CONFIG,
					   &data);
			if (ret_val)
				return ret_val;

			data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = e1e_wphy(hw, IGP01E1000_PHY_PORT_CONFIG,
					   data);
			if (ret_val)
				return ret_val;
		}
	} else if ((phy->autoneg_advertised == E1000_ALL_SPEED_DUPLEX) ||
		   (phy->autoneg_advertised == E1000_ALL_NOT_GIG) ||
		   (phy->autoneg_advertised == E1000_ALL_10_SPEED)) {
		data |= IGP02E1000_PM_D3_LPLU;
		ret_val = e1e_wphy(hw, IGP02E1000_PHY_POWER_MGMT, data);
		if (ret_val)
			return ret_val;

		/* When LPLU is enabled, we should disable SmartSpeed */
		ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_CONFIG, &data);
		if (ret_val)
			return ret_val;

		data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = e1e_wphy(hw, IGP01E1000_PHY_PORT_CONFIG, data);
	}

	return ret_val;
}

/**
 *  e1000e_check_downshift - Checks whether a downshift in speed occurred
 *  @hw: pointer to the HW structure
 *
 *  Success returns 0, Failure returns 1
 *
 *  A downshift is detected by querying the PHY link health.
 **/
s32 e1000e_check_downshift(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data, offset, mask;

	switch (phy->type) {
	case e1000_phy_m88:
	case e1000_phy_gg82563:
	case e1000_phy_bm:
	case e1000_phy_82578:
		offset	= M88E1000_PHY_SPEC_STATUS;
		mask	= M88E1000_PSSR_DOWNSHIFT;
		break;
	case e1000_phy_igp_2:
	case e1000_phy_igp_3:
		offset	= IGP01E1000_PHY_LINK_HEALTH;
		mask	= IGP01E1000_PLHR_SS_DOWNGRADE;
		break;
	default:
		/* speed downshift not supported */
		phy->speed_downgraded = false;
		return 0;
	}

	ret_val = e1e_rphy(hw, offset, &phy_data);

	if (!ret_val)
		phy->speed_downgraded = !!(phy_data & mask);

	return ret_val;
}

/**
 *  e1000_check_polarity_m88 - Checks the polarity.
 *  @hw: pointer to the HW structure
 *
 *  Success returns 0, Failure returns -E1000_ERR_PHY (-2)
 *
 *  Polarity is determined based on the PHY specific status register.
 **/
s32 e1000_check_polarity_m88(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	ret_val = e1e_rphy(hw, M88E1000_PHY_SPEC_STATUS, &data);

	if (!ret_val)
		phy->cable_polarity = (data & M88E1000_PSSR_REV_POLARITY)
				      ? e1000_rev_polarity_reversed
				      : e1000_rev_polarity_normal;

	return ret_val;
}

/**
 *  e1000_check_polarity_igp - Checks the polarity.
 *  @hw: pointer to the HW structure
 *
 *  Success returns 0, Failure returns -E1000_ERR_PHY (-2)
 *
 *  Polarity is determined based on the PHY port status register, and the
 *  current speed (since there is no polarity at 100Mbps).
 **/
s32 e1000_check_polarity_igp(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data, offset, mask;

	/* Polarity is determined based on the speed of
	 * our connection.
	 */
	ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_STATUS, &data);
	if (ret_val)
		return ret_val;

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

	ret_val = e1e_rphy(hw, offset, &data);

	if (!ret_val)
		phy->cable_polarity = (data & mask)
				      ? e1000_rev_polarity_reversed
				      : e1000_rev_polarity_normal;

	return ret_val;
}

/**
 *  e1000_check_polarity_ife - Check cable polarity for IFE PHY
 *  @hw: pointer to the HW structure
 *
 *  Polarity is determined on the polarity reversal feature being enabled.
 **/
s32 e1000_check_polarity_ife(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data, offset, mask;

	/* Polarity is determined based on the reversal feature being enabled.
	 */
	if (phy->polarity_correction) {
		offset = IFE_PHY_EXTENDED_STATUS_CONTROL;
		mask = IFE_PESC_POLARITY_REVERSED;
	} else {
		offset = IFE_PHY_SPECIAL_CONTROL;
		mask = IFE_PSC_FORCE_POLARITY;
	}

	ret_val = e1e_rphy(hw, offset, &phy_data);

	if (!ret_val)
		phy->cable_polarity = (phy_data & mask)
		                       ? e1000_rev_polarity_reversed
		                       : e1000_rev_polarity_normal;

	return ret_val;
}

/**
 *  e1000_wait_autoneg - Wait for auto-neg completion
 *  @hw: pointer to the HW structure
 *
 *  Waits for auto-negotiation to complete or for the auto-negotiation time
 *  limit to expire, which ever happens first.
 **/
static s32 e1000_wait_autoneg(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	u16 i, phy_status;

	/* Break after autoneg completes or PHY_AUTO_NEG_LIMIT expires. */
	for (i = PHY_AUTO_NEG_LIMIT; i > 0; i--) {
		ret_val = e1e_rphy(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		ret_val = e1e_rphy(hw, PHY_STATUS, &phy_status);
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
 *  e1000e_phy_has_link_generic - Polls PHY for link
 *  @hw: pointer to the HW structure
 *  @iterations: number of times to poll for link
 *  @usec_interval: delay between polling attempts
 *  @success: pointer to whether polling was successful or not
 *
 *  Polls the PHY status register for link, 'iterations' number of times.
 **/
s32 e1000e_phy_has_link_generic(struct e1000_hw *hw, u32 iterations,
			       u32 usec_interval, bool *success)
{
	s32 ret_val = 0;
	u16 i, phy_status;

	for (i = 0; i < iterations; i++) {
		/* Some PHYs require the PHY_STATUS register to be read
		 * twice due to the link bit being sticky.  No harm doing
		 * it across the board.
		 */
		ret_val = e1e_rphy(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			/* If the first read fails, another entity may have
			 * ownership of the resources, wait and try again to
			 * see if they have relinquished the resources yet.
			 */
			udelay(usec_interval);
		ret_val = e1e_rphy(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		if (phy_status & MII_SR_LINK_STATUS)
			break;
		if (usec_interval >= 1000)
			mdelay(usec_interval/1000);
		else
			udelay(usec_interval);
	}

	*success = (i < iterations);

	return ret_val;
}

/**
 *  e1000e_get_cable_length_m88 - Determine cable length for m88 PHY
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
s32 e1000e_get_cable_length_m88(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data, index;

	ret_val = e1e_rphy(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
	if (ret_val)
		return ret_val;

	index = (phy_data & M88E1000_PSSR_CABLE_LENGTH) >>
	        M88E1000_PSSR_CABLE_LENGTH_SHIFT;

	if (index >= M88E1000_CABLE_LENGTH_TABLE_SIZE - 1)
		return -E1000_ERR_PHY;

	phy->min_cable_length = e1000_m88_cable_length_table[index];
	phy->max_cable_length = e1000_m88_cable_length_table[index + 1];

	phy->cable_length = (phy->min_cable_length + phy->max_cable_length) / 2;

	return 0;
}

/**
 *  e1000e_get_cable_length_igp_2 - Determine cable length for igp2 PHY
 *  @hw: pointer to the HW structure
 *
 *  The automatic gain control (agc) normalizes the amplitude of the
 *  received signal, adjusting for the attenuation produced by the
 *  cable.  By reading the AGC registers, which represent the
 *  combination of coarse and fine gain value, the value can be put
 *  into a lookup table to obtain the approximate cable length
 *  for each channel.
 **/
s32 e1000e_get_cable_length_igp_2(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data, i, agc_value = 0;
	u16 cur_agc_index, max_agc_index = 0;
	u16 min_agc_index = IGP02E1000_CABLE_LENGTH_TABLE_SIZE - 1;
	static const u16 agc_reg_array[IGP02E1000_PHY_CHANNEL_NUM] = {
	       IGP02E1000_PHY_AGC_A,
	       IGP02E1000_PHY_AGC_B,
	       IGP02E1000_PHY_AGC_C,
	       IGP02E1000_PHY_AGC_D
	};

	/* Read the AGC registers for all channels */
	for (i = 0; i < IGP02E1000_PHY_CHANNEL_NUM; i++) {
		ret_val = e1e_rphy(hw, agc_reg_array[i], &phy_data);
		if (ret_val)
			return ret_val;

		/* Getting bits 15:9, which represent the combination of
		 * coarse and fine gain values.  The result is a number
		 * that can be put into the lookup table to obtain the
		 * approximate cable length.
		 */
		cur_agc_index = (phy_data >> IGP02E1000_AGC_LENGTH_SHIFT) &
				IGP02E1000_AGC_LENGTH_MASK;

		/* Array index bound check. */
		if ((cur_agc_index >= IGP02E1000_CABLE_LENGTH_TABLE_SIZE) ||
		    (cur_agc_index == 0))
			return -E1000_ERR_PHY;

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

	return 0;
}

/**
 *  e1000e_get_phy_info_m88 - Retrieve PHY information
 *  @hw: pointer to the HW structure
 *
 *  Valid for only copper links.  Read the PHY status register (sticky read)
 *  to verify that link is up.  Read the PHY special control register to
 *  determine the polarity and 10base-T extended distance.  Read the PHY
 *  special status register to determine MDI/MDIx and current speed.  If
 *  speed is 1000, then determine cable length, local and remote receiver.
 **/
s32 e1000e_get_phy_info_m88(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32  ret_val;
	u16 phy_data;
	bool link;

	if (phy->media_type != e1000_media_type_copper) {
		e_dbg("Phy info is only valid for copper media\n");
		return -E1000_ERR_CONFIG;
	}

	ret_val = e1000e_phy_has_link_generic(hw, 1, 0, &link);
	if (ret_val)
		return ret_val;

	if (!link) {
		e_dbg("Phy info is only valid if link is up\n");
		return -E1000_ERR_CONFIG;
	}

	ret_val = e1e_rphy(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	phy->polarity_correction = !!(phy_data &
				      M88E1000_PSCR_POLARITY_REVERSAL);

	ret_val = e1000_check_polarity_m88(hw);
	if (ret_val)
		return ret_val;

	ret_val = e1e_rphy(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
	if (ret_val)
		return ret_val;

	phy->is_mdix = !!(phy_data & M88E1000_PSSR_MDIX);

	if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_1000MBS) {
		ret_val = hw->phy.ops.get_cable_length(hw);
		if (ret_val)
			return ret_val;

		ret_val = e1e_rphy(hw, PHY_1000T_STATUS, &phy_data);
		if (ret_val)
			return ret_val;

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

	return ret_val;
}

/**
 *  e1000e_get_phy_info_igp - Retrieve igp PHY information
 *  @hw: pointer to the HW structure
 *
 *  Read PHY status to determine if link is up.  If link is up, then
 *  set/determine 10base-T extended distance and polarity correction.  Read
 *  PHY port status to determine MDI/MDIx and speed.  Based on the speed,
 *  determine on the cable length, local and remote receiver.
 **/
s32 e1000e_get_phy_info_igp(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;
	bool link;

	ret_val = e1000e_phy_has_link_generic(hw, 1, 0, &link);
	if (ret_val)
		return ret_val;

	if (!link) {
		e_dbg("Phy info is only valid if link is up\n");
		return -E1000_ERR_CONFIG;
	}

	phy->polarity_correction = true;

	ret_val = e1000_check_polarity_igp(hw);
	if (ret_val)
		return ret_val;

	ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_STATUS, &data);
	if (ret_val)
		return ret_val;

	phy->is_mdix = !!(data & IGP01E1000_PSSR_MDIX);

	if ((data & IGP01E1000_PSSR_SPEED_MASK) ==
	    IGP01E1000_PSSR_SPEED_1000MBPS) {
		ret_val = phy->ops.get_cable_length(hw);
		if (ret_val)
			return ret_val;

		ret_val = e1e_rphy(hw, PHY_1000T_STATUS, &data);
		if (ret_val)
			return ret_val;

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

	return ret_val;
}

/**
 *  e1000_get_phy_info_ife - Retrieves various IFE PHY states
 *  @hw: pointer to the HW structure
 *
 *  Populates "phy" structure with various feature states.
 **/
s32 e1000_get_phy_info_ife(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;
	bool link;

	ret_val = e1000e_phy_has_link_generic(hw, 1, 0, &link);
	if (ret_val)
		return ret_val;

	if (!link) {
		e_dbg("Phy info is only valid if link is up\n");
		return -E1000_ERR_CONFIG;
	}

	ret_val = e1e_rphy(hw, IFE_PHY_SPECIAL_CONTROL, &data);
	if (ret_val)
		return ret_val;
	phy->polarity_correction = !(data & IFE_PSC_AUTO_POLARITY_DISABLE);

	if (phy->polarity_correction) {
		ret_val = e1000_check_polarity_ife(hw);
		if (ret_val)
			return ret_val;
	} else {
		/* Polarity is forced */
		phy->cable_polarity = (data & IFE_PSC_FORCE_POLARITY)
		                      ? e1000_rev_polarity_reversed
		                      : e1000_rev_polarity_normal;
	}

	ret_val = e1e_rphy(hw, IFE_PHY_MDIX_CONTROL, &data);
	if (ret_val)
		return ret_val;

	phy->is_mdix = !!(data & IFE_PMC_MDIX_STATUS);

	/* The following parameters are undefined for 10/100 operation. */
	phy->cable_length = E1000_CABLE_LENGTH_UNDEFINED;
	phy->local_rx = e1000_1000t_rx_status_undefined;
	phy->remote_rx = e1000_1000t_rx_status_undefined;

	return 0;
}

/**
 *  e1000e_phy_sw_reset - PHY software reset
 *  @hw: pointer to the HW structure
 *
 *  Does a software reset of the PHY by reading the PHY control register and
 *  setting/write the control register reset bit to the PHY.
 **/
s32 e1000e_phy_sw_reset(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 phy_ctrl;

	ret_val = e1e_rphy(hw, PHY_CONTROL, &phy_ctrl);
	if (ret_val)
		return ret_val;

	phy_ctrl |= MII_CR_RESET;
	ret_val = e1e_wphy(hw, PHY_CONTROL, phy_ctrl);
	if (ret_val)
		return ret_val;

	udelay(1);

	return ret_val;
}

/**
 *  e1000e_phy_hw_reset_generic - PHY hardware reset
 *  @hw: pointer to the HW structure
 *
 *  Verify the reset block is not blocking us from resetting.  Acquire
 *  semaphore (if necessary) and read/set/write the device control reset
 *  bit in the PHY.  Wait the appropriate delay time for the device to
 *  reset and release the semaphore (if necessary).
 **/
s32 e1000e_phy_hw_reset_generic(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u32 ctrl;

	if (phy->ops.check_reset_block) {
		ret_val = phy->ops.check_reset_block(hw);
		if (ret_val)
			return 0;
	}

	ret_val = phy->ops.acquire(hw);
	if (ret_val)
		return ret_val;

	ctrl = er32(CTRL);
	ew32(CTRL, ctrl | E1000_CTRL_PHY_RST);
	e1e_flush();

	udelay(phy->reset_delay_us);

	ew32(CTRL, ctrl);
	e1e_flush();

	udelay(150);

	phy->ops.release(hw);

	return phy->ops.get_cfg_done(hw);
}

/**
 *  e1000e_get_cfg_done_generic - Generic configuration done
 *  @hw: pointer to the HW structure
 *
 *  Generic function to wait 10 milli-seconds for configuration to complete
 *  and return success.
 **/
s32 e1000e_get_cfg_done_generic(struct e1000_hw *hw)
{
	mdelay(10);

	return 0;
}

/**
 *  e1000e_phy_init_script_igp3 - Inits the IGP3 PHY
 *  @hw: pointer to the HW structure
 *
 *  Initializes a Intel Gigabit PHY3 when an EEPROM is not present.
 **/
s32 e1000e_phy_init_script_igp3(struct e1000_hw *hw)
{
	e_dbg("Running IGP 3 PHY init script\n");

	/* PHY init IGP 3 */
	/* Enable rise/fall, 10-mode work in class-A */
	e1e_wphy(hw, 0x2F5B, 0x9018);
	/* Remove all caps from Replica path filter */
	e1e_wphy(hw, 0x2F52, 0x0000);
	/* Bias trimming for ADC, AFE and Driver (Default) */
	e1e_wphy(hw, 0x2FB1, 0x8B24);
	/* Increase Hybrid poly bias */
	e1e_wphy(hw, 0x2FB2, 0xF8F0);
	/* Add 4% to Tx amplitude in Gig mode */
	e1e_wphy(hw, 0x2010, 0x10B0);
	/* Disable trimming (TTT) */
	e1e_wphy(hw, 0x2011, 0x0000);
	/* Poly DC correction to 94.6% + 2% for all channels */
	e1e_wphy(hw, 0x20DD, 0x249A);
	/* ABS DC correction to 95.9% */
	e1e_wphy(hw, 0x20DE, 0x00D3);
	/* BG temp curve trim */
	e1e_wphy(hw, 0x28B4, 0x04CE);
	/* Increasing ADC OPAMP stage 1 currents to max */
	e1e_wphy(hw, 0x2F70, 0x29E4);
	/* Force 1000 ( required for enabling PHY regs configuration) */
	e1e_wphy(hw, 0x0000, 0x0140);
	/* Set upd_freq to 6 */
	e1e_wphy(hw, 0x1F30, 0x1606);
	/* Disable NPDFE */
	e1e_wphy(hw, 0x1F31, 0xB814);
	/* Disable adaptive fixed FFE (Default) */
	e1e_wphy(hw, 0x1F35, 0x002A);
	/* Enable FFE hysteresis */
	e1e_wphy(hw, 0x1F3E, 0x0067);
	/* Fixed FFE for short cable lengths */
	e1e_wphy(hw, 0x1F54, 0x0065);
	/* Fixed FFE for medium cable lengths */
	e1e_wphy(hw, 0x1F55, 0x002A);
	/* Fixed FFE for long cable lengths */
	e1e_wphy(hw, 0x1F56, 0x002A);
	/* Enable Adaptive Clip Threshold */
	e1e_wphy(hw, 0x1F72, 0x3FB0);
	/* AHT reset limit to 1 */
	e1e_wphy(hw, 0x1F76, 0xC0FF);
	/* Set AHT master delay to 127 msec */
	e1e_wphy(hw, 0x1F77, 0x1DEC);
	/* Set scan bits for AHT */
	e1e_wphy(hw, 0x1F78, 0xF9EF);
	/* Set AHT Preset bits */
	e1e_wphy(hw, 0x1F79, 0x0210);
	/* Change integ_factor of channel A to 3 */
	e1e_wphy(hw, 0x1895, 0x0003);
	/* Change prop_factor of channels BCD to 8 */
	e1e_wphy(hw, 0x1796, 0x0008);
	/* Change cg_icount + enable integbp for channels BCD */
	e1e_wphy(hw, 0x1798, 0xD008);
	/* Change cg_icount + enable integbp + change prop_factor_master
	 * to 8 for channel A
	 */
	e1e_wphy(hw, 0x1898, 0xD918);
	/* Disable AHT in Slave mode on channel A */
	e1e_wphy(hw, 0x187A, 0x0800);
	/* Enable LPLU and disable AN to 1000 in non-D0a states,
	 * Enable SPD+B2B
	 */
	e1e_wphy(hw, 0x0019, 0x008D);
	/* Enable restart AN on an1000_dis change */
	e1e_wphy(hw, 0x001B, 0x2080);
	/* Enable wh_fifo read clock in 10/100 modes */
	e1e_wphy(hw, 0x0014, 0x0045);
	/* Restart AN, Speed selection is 1000 */
	e1e_wphy(hw, 0x0000, 0x1340);

	return 0;
}

/**
 *  e1000e_get_phy_type_from_id - Get PHY type from id
 *  @phy_id: phy_id read from the phy
 *
 *  Returns the phy type from the id.
 **/
enum e1000_phy_type e1000e_get_phy_type_from_id(u32 phy_id)
{
	enum e1000_phy_type phy_type = e1000_phy_unknown;

	switch (phy_id) {
	case M88E1000_I_PHY_ID:
	case M88E1000_E_PHY_ID:
	case M88E1111_I_PHY_ID:
	case M88E1011_I_PHY_ID:
		phy_type = e1000_phy_m88;
		break;
	case IGP01E1000_I_PHY_ID: /* IGP 1 & 2 share this */
		phy_type = e1000_phy_igp_2;
		break;
	case GG82563_E_PHY_ID:
		phy_type = e1000_phy_gg82563;
		break;
	case IGP03E1000_E_PHY_ID:
		phy_type = e1000_phy_igp_3;
		break;
	case IFE_E_PHY_ID:
	case IFE_PLUS_E_PHY_ID:
	case IFE_C_E_PHY_ID:
		phy_type = e1000_phy_ife;
		break;
	case BME1000_E_PHY_ID:
	case BME1000_E_PHY_ID_R2:
		phy_type = e1000_phy_bm;
		break;
	case I82578_E_PHY_ID:
		phy_type = e1000_phy_82578;
		break;
	case I82577_E_PHY_ID:
		phy_type = e1000_phy_82577;
		break;
	case I82579_E_PHY_ID:
		phy_type = e1000_phy_82579;
		break;
	case I217_E_PHY_ID:
		phy_type = e1000_phy_i217;
		break;
	default:
		phy_type = e1000_phy_unknown;
		break;
	}
	return phy_type;
}

/**
 *  e1000e_determine_phy_address - Determines PHY address.
 *  @hw: pointer to the HW structure
 *
 *  This uses a trial and error method to loop through possible PHY
 *  addresses. It tests each by reading the PHY ID registers and
 *  checking for a match.
 **/
s32 e1000e_determine_phy_address(struct e1000_hw *hw)
{
	u32 phy_addr = 0;
	u32 i;
	enum e1000_phy_type phy_type = e1000_phy_unknown;

	hw->phy.id = phy_type;

	for (phy_addr = 0; phy_addr < E1000_MAX_PHY_ADDR; phy_addr++) {
		hw->phy.addr = phy_addr;
		i = 0;

		do {
			e1000e_get_phy_id(hw);
			phy_type = e1000e_get_phy_type_from_id(hw->phy.id);

			/* If phy_type is valid, break - we found our
			 * PHY address
			 */
			if (phy_type  != e1000_phy_unknown)
				return 0;

			usleep_range(1000, 2000);
			i++;
		} while (i < 10);
	}

	return -E1000_ERR_PHY_TYPE;
}

/**
 *  e1000_get_phy_addr_for_bm_page - Retrieve PHY page address
 *  @page: page to access
 *
 *  Returns the phy address for the page requested.
 **/
static u32 e1000_get_phy_addr_for_bm_page(u32 page, u32 reg)
{
	u32 phy_addr = 2;

	if ((page >= 768) || (page == 0 && reg == 25) || (reg == 31))
		phy_addr = 1;

	return phy_addr;
}

/**
 *  e1000e_write_phy_reg_bm - Write BM PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Acquires semaphore, if necessary, then writes the data to PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
s32 e1000e_write_phy_reg_bm(struct e1000_hw *hw, u32 offset, u16 data)
{
	s32 ret_val;
	u32 page = offset >> IGP_PAGE_SHIFT;

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	/* Page 800 works differently than the rest so it has its own func */
	if (page == BM_WUC_PAGE) {
		ret_val = e1000_access_phy_wakeup_reg_bm(hw, offset, &data,
							 false, false);
		goto release;
	}

	hw->phy.addr = e1000_get_phy_addr_for_bm_page(page, offset);

	if (offset > MAX_PHY_MULTI_PAGE_REG) {
		u32 page_shift, page_select;

		/* Page select is register 31 for phy address 1 and 22 for
		 * phy address 2 and 3. Page select is shifted only for
		 * phy address 1.
		 */
		if (hw->phy.addr == 1) {
			page_shift = IGP_PAGE_SHIFT;
			page_select = IGP01E1000_PHY_PAGE_SELECT;
		} else {
			page_shift = 0;
			page_select = BM_PHY_PAGE_SELECT;
		}

		/* Page is shifted left, PHY expects (page x 32) */
		ret_val = e1000e_write_phy_reg_mdic(hw, page_select,
		                                    (page << page_shift));
		if (ret_val)
			goto release;
	}

	ret_val = e1000e_write_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS & offset,
	                                    data);

release:
	hw->phy.ops.release(hw);
	return ret_val;
}

/**
 *  e1000e_read_phy_reg_bm - Read BM PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Acquires semaphore, if necessary, then reads the PHY register at offset
 *  and storing the retrieved information in data.  Release any acquired
 *  semaphores before exiting.
 **/
s32 e1000e_read_phy_reg_bm(struct e1000_hw *hw, u32 offset, u16 *data)
{
	s32 ret_val;
	u32 page = offset >> IGP_PAGE_SHIFT;

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	/* Page 800 works differently than the rest so it has its own func */
	if (page == BM_WUC_PAGE) {
		ret_val = e1000_access_phy_wakeup_reg_bm(hw, offset, data,
							 true, false);
		goto release;
	}

	hw->phy.addr = e1000_get_phy_addr_for_bm_page(page, offset);

	if (offset > MAX_PHY_MULTI_PAGE_REG) {
		u32 page_shift, page_select;

		/* Page select is register 31 for phy address 1 and 22 for
		 * phy address 2 and 3. Page select is shifted only for
		 * phy address 1.
		 */
		if (hw->phy.addr == 1) {
			page_shift = IGP_PAGE_SHIFT;
			page_select = IGP01E1000_PHY_PAGE_SELECT;
		} else {
			page_shift = 0;
			page_select = BM_PHY_PAGE_SELECT;
		}

		/* Page is shifted left, PHY expects (page x 32) */
		ret_val = e1000e_write_phy_reg_mdic(hw, page_select,
		                                    (page << page_shift));
		if (ret_val)
			goto release;
	}

	ret_val = e1000e_read_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS & offset,
	                                   data);
release:
	hw->phy.ops.release(hw);
	return ret_val;
}

/**
 *  e1000e_read_phy_reg_bm2 - Read BM PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Acquires semaphore, if necessary, then reads the PHY register at offset
 *  and storing the retrieved information in data.  Release any acquired
 *  semaphores before exiting.
 **/
s32 e1000e_read_phy_reg_bm2(struct e1000_hw *hw, u32 offset, u16 *data)
{
	s32 ret_val;
	u16 page = (u16)(offset >> IGP_PAGE_SHIFT);

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	/* Page 800 works differently than the rest so it has its own func */
	if (page == BM_WUC_PAGE) {
		ret_val = e1000_access_phy_wakeup_reg_bm(hw, offset, data,
							 true, false);
		goto release;
	}

	hw->phy.addr = 1;

	if (offset > MAX_PHY_MULTI_PAGE_REG) {

		/* Page is shifted left, PHY expects (page x 32) */
		ret_val = e1000e_write_phy_reg_mdic(hw, BM_PHY_PAGE_SELECT,
						    page);

		if (ret_val)
			goto release;
	}

	ret_val = e1000e_read_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS & offset,
					   data);
release:
	hw->phy.ops.release(hw);
	return ret_val;
}

/**
 *  e1000e_write_phy_reg_bm2 - Write BM PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Acquires semaphore, if necessary, then writes the data to PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
s32 e1000e_write_phy_reg_bm2(struct e1000_hw *hw, u32 offset, u16 data)
{
	s32 ret_val;
	u16 page = (u16)(offset >> IGP_PAGE_SHIFT);

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	/* Page 800 works differently than the rest so it has its own func */
	if (page == BM_WUC_PAGE) {
		ret_val = e1000_access_phy_wakeup_reg_bm(hw, offset, &data,
							 false, false);
		goto release;
	}

	hw->phy.addr = 1;

	if (offset > MAX_PHY_MULTI_PAGE_REG) {
		/* Page is shifted left, PHY expects (page x 32) */
		ret_val = e1000e_write_phy_reg_mdic(hw, BM_PHY_PAGE_SELECT,
						    page);

		if (ret_val)
			goto release;
	}

	ret_val = e1000e_write_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS & offset,
					    data);

release:
	hw->phy.ops.release(hw);
	return ret_val;
}

/**
 *  e1000_enable_phy_wakeup_reg_access_bm - enable access to BM wakeup registers
 *  @hw: pointer to the HW structure
 *  @phy_reg: pointer to store original contents of BM_WUC_ENABLE_REG
 *
 *  Assumes semaphore already acquired and phy_reg points to a valid memory
 *  address to store contents of the BM_WUC_ENABLE_REG register.
 **/
s32 e1000_enable_phy_wakeup_reg_access_bm(struct e1000_hw *hw, u16 *phy_reg)
{
	s32 ret_val;
	u16 temp;

	/* All page select, port ctrl and wakeup registers use phy address 1 */
	hw->phy.addr = 1;

	/* Select Port Control Registers page */
	ret_val = e1000_set_page_igp(hw, (BM_PORT_CTRL_PAGE << IGP_PAGE_SHIFT));
	if (ret_val) {
		e_dbg("Could not set Port Control page\n");
		return ret_val;
	}

	ret_val = e1000e_read_phy_reg_mdic(hw, BM_WUC_ENABLE_REG, phy_reg);
	if (ret_val) {
		e_dbg("Could not read PHY register %d.%d\n",
		      BM_PORT_CTRL_PAGE, BM_WUC_ENABLE_REG);
		return ret_val;
	}

	/* Enable both PHY wakeup mode and Wakeup register page writes.
	 * Prevent a power state change by disabling ME and Host PHY wakeup.
	 */
	temp = *phy_reg;
	temp |= BM_WUC_ENABLE_BIT;
	temp &= ~(BM_WUC_ME_WU_BIT | BM_WUC_HOST_WU_BIT);

	ret_val = e1000e_write_phy_reg_mdic(hw, BM_WUC_ENABLE_REG, temp);
	if (ret_val) {
		e_dbg("Could not write PHY register %d.%d\n",
		      BM_PORT_CTRL_PAGE, BM_WUC_ENABLE_REG);
		return ret_val;
	}

	/* Select Host Wakeup Registers page - caller now able to write
	 * registers on the Wakeup registers page
	 */
	return e1000_set_page_igp(hw, (BM_WUC_PAGE << IGP_PAGE_SHIFT));
}

/**
 *  e1000_disable_phy_wakeup_reg_access_bm - disable access to BM wakeup regs
 *  @hw: pointer to the HW structure
 *  @phy_reg: pointer to original contents of BM_WUC_ENABLE_REG
 *
 *  Restore BM_WUC_ENABLE_REG to its original value.
 *
 *  Assumes semaphore already acquired and *phy_reg is the contents of the
 *  BM_WUC_ENABLE_REG before register(s) on BM_WUC_PAGE were accessed by
 *  caller.
 **/
s32 e1000_disable_phy_wakeup_reg_access_bm(struct e1000_hw *hw, u16 *phy_reg)
{
	s32 ret_val;

	/* Select Port Control Registers page */
	ret_val = e1000_set_page_igp(hw, (BM_PORT_CTRL_PAGE << IGP_PAGE_SHIFT));
	if (ret_val) {
		e_dbg("Could not set Port Control page\n");
		return ret_val;
	}

	/* Restore 769.17 to its original value */
	ret_val = e1000e_write_phy_reg_mdic(hw, BM_WUC_ENABLE_REG, *phy_reg);
	if (ret_val)
		e_dbg("Could not restore PHY register %d.%d\n",
		      BM_PORT_CTRL_PAGE, BM_WUC_ENABLE_REG);

	return ret_val;
}

/**
 *  e1000_access_phy_wakeup_reg_bm - Read/write BM PHY wakeup register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read or written
 *  @data: pointer to the data to read or write
 *  @read: determines if operation is read or write
 *  @page_set: BM_WUC_PAGE already set and access enabled
 *
 *  Read the PHY register at offset and store the retrieved information in
 *  data, or write data to PHY register at offset.  Note the procedure to
 *  access the PHY wakeup registers is different than reading the other PHY
 *  registers. It works as such:
 *  1) Set 769.17.2 (page 769, register 17, bit 2) = 1
 *  2) Set page to 800 for host (801 if we were manageability)
 *  3) Write the address using the address opcode (0x11)
 *  4) Read or write the data using the data opcode (0x12)
 *  5) Restore 769.17.2 to its original value
 *
 *  Steps 1 and 2 are done by e1000_enable_phy_wakeup_reg_access_bm() and
 *  step 5 is done by e1000_disable_phy_wakeup_reg_access_bm().
 *
 *  Assumes semaphore is already acquired.  When page_set==true, assumes
 *  the PHY page is set to BM_WUC_PAGE (i.e. a function in the call stack
 *  is responsible for calls to e1000_[enable|disable]_phy_wakeup_reg_bm()).
 **/
static s32 e1000_access_phy_wakeup_reg_bm(struct e1000_hw *hw, u32 offset,
					  u16 *data, bool read, bool page_set)
{
	s32 ret_val;
	u16 reg = BM_PHY_REG_NUM(offset);
	u16 page = BM_PHY_REG_PAGE(offset);
	u16 phy_reg = 0;

	/* Gig must be disabled for MDIO accesses to Host Wakeup reg page */
	if ((hw->mac.type == e1000_pchlan) &&
	    (!(er32(PHY_CTRL) & E1000_PHY_CTRL_GBE_DISABLE)))
		e_dbg("Attempting to access page %d while gig enabled.\n",
		      page);

	if (!page_set) {
		/* Enable access to PHY wakeup registers */
		ret_val = e1000_enable_phy_wakeup_reg_access_bm(hw, &phy_reg);
		if (ret_val) {
			e_dbg("Could not enable PHY wakeup reg access\n");
			return ret_val;
		}
	}

	e_dbg("Accessing PHY page %d reg 0x%x\n", page, reg);

	/* Write the Wakeup register page offset value using opcode 0x11 */
	ret_val = e1000e_write_phy_reg_mdic(hw, BM_WUC_ADDRESS_OPCODE, reg);
	if (ret_val) {
		e_dbg("Could not write address opcode to page %d\n", page);
		return ret_val;
	}

	if (read) {
		/* Read the Wakeup register page value using opcode 0x12 */
		ret_val = e1000e_read_phy_reg_mdic(hw, BM_WUC_DATA_OPCODE,
		                                   data);
	} else {
		/* Write the Wakeup register page value using opcode 0x12 */
		ret_val = e1000e_write_phy_reg_mdic(hw, BM_WUC_DATA_OPCODE,
						    *data);
	}

	if (ret_val) {
		e_dbg("Could not access PHY reg %d.%d\n", page, reg);
		return ret_val;
	}

	if (!page_set)
		ret_val = e1000_disable_phy_wakeup_reg_access_bm(hw, &phy_reg);

	return ret_val;
}

/**
 * e1000_power_up_phy_copper - Restore copper link in case of PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, restore the link to previous
 * settings.
 **/
void e1000_power_up_phy_copper(struct e1000_hw *hw)
{
	u16 mii_reg = 0;

	/* The PHY will retain its settings across a power down/up cycle */
	e1e_rphy(hw, PHY_CONTROL, &mii_reg);
	mii_reg &= ~MII_CR_POWER_DOWN;
	e1e_wphy(hw, PHY_CONTROL, mii_reg);
}

/**
 * e1000_power_down_phy_copper - Restore copper link in case of PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, restore the link to previous
 * settings.
 **/
void e1000_power_down_phy_copper(struct e1000_hw *hw)
{
	u16 mii_reg = 0;

	/* The PHY will retain its settings across a power down/up cycle */
	e1e_rphy(hw, PHY_CONTROL, &mii_reg);
	mii_reg |= MII_CR_POWER_DOWN;
	e1e_wphy(hw, PHY_CONTROL, mii_reg);
	usleep_range(1000, 2000);
}

/**
 *  __e1000_read_phy_reg_hv -  Read HV PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *  @locked: semaphore has already been acquired or not
 *
 *  Acquires semaphore, if necessary, then reads the PHY register at offset
 *  and stores the retrieved information in data.  Release any acquired
 *  semaphore before exiting.
 **/
static s32 __e1000_read_phy_reg_hv(struct e1000_hw *hw, u32 offset, u16 *data,
				   bool locked, bool page_set)
{
	s32 ret_val;
	u16 page = BM_PHY_REG_PAGE(offset);
	u16 reg = BM_PHY_REG_NUM(offset);
	u32 phy_addr = hw->phy.addr = e1000_get_phy_addr_for_hv_page(page);

	if (!locked) {
		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
	}

	/* Page 800 works differently than the rest so it has its own func */
	if (page == BM_WUC_PAGE) {
		ret_val = e1000_access_phy_wakeup_reg_bm(hw, offset, data,
							 true, page_set);
		goto out;
	}

	if (page > 0 && page < HV_INTC_FC_PAGE_START) {
		ret_val = e1000_access_phy_debug_regs_hv(hw, offset,
		                                         data, true);
		goto out;
	}

	if (!page_set) {
		if (page == HV_INTC_FC_PAGE_START)
			page = 0;

		if (reg > MAX_PHY_MULTI_PAGE_REG) {
			/* Page is shifted left, PHY expects (page x 32) */
			ret_val = e1000_set_page_igp(hw,
						     (page << IGP_PAGE_SHIFT));

			hw->phy.addr = phy_addr;

			if (ret_val)
				goto out;
		}
	}

	e_dbg("reading PHY page %d (or 0x%x shifted) reg 0x%x\n", page,
	      page << IGP_PAGE_SHIFT, reg);

	ret_val = e1000e_read_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS & reg,
	                                  data);
out:
	if (!locked)
		hw->phy.ops.release(hw);

	return ret_val;
}

/**
 *  e1000_read_phy_reg_hv -  Read HV PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Acquires semaphore then reads the PHY register at offset and stores
 *  the retrieved information in data.  Release the acquired semaphore
 *  before exiting.
 **/
s32 e1000_read_phy_reg_hv(struct e1000_hw *hw, u32 offset, u16 *data)
{
	return __e1000_read_phy_reg_hv(hw, offset, data, false, false);
}

/**
 *  e1000_read_phy_reg_hv_locked -  Read HV PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the PHY register at offset and stores the retrieved information
 *  in data.  Assumes semaphore already acquired.
 **/
s32 e1000_read_phy_reg_hv_locked(struct e1000_hw *hw, u32 offset, u16 *data)
{
	return __e1000_read_phy_reg_hv(hw, offset, data, true, false);
}

/**
 *  e1000_read_phy_reg_page_hv - Read HV PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Reads the PHY register at offset and stores the retrieved information
 *  in data.  Assumes semaphore already acquired and page already set.
 **/
s32 e1000_read_phy_reg_page_hv(struct e1000_hw *hw, u32 offset, u16 *data)
{
	return __e1000_read_phy_reg_hv(hw, offset, data, true, true);
}

/**
 *  __e1000_write_phy_reg_hv - Write HV PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *  @locked: semaphore has already been acquired or not
 *
 *  Acquires semaphore, if necessary, then writes the data to PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
static s32 __e1000_write_phy_reg_hv(struct e1000_hw *hw, u32 offset, u16 data,
				    bool locked, bool page_set)
{
	s32 ret_val;
	u16 page = BM_PHY_REG_PAGE(offset);
	u16 reg = BM_PHY_REG_NUM(offset);
	u32 phy_addr = hw->phy.addr = e1000_get_phy_addr_for_hv_page(page);

	if (!locked) {
		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
	}

	/* Page 800 works differently than the rest so it has its own func */
	if (page == BM_WUC_PAGE) {
		ret_val = e1000_access_phy_wakeup_reg_bm(hw, offset, &data,
							 false, page_set);
		goto out;
	}

	if (page > 0 && page < HV_INTC_FC_PAGE_START) {
		ret_val = e1000_access_phy_debug_regs_hv(hw, offset,
		                                         &data, false);
		goto out;
	}

	if (!page_set) {
		if (page == HV_INTC_FC_PAGE_START)
			page = 0;

		/* Workaround MDIO accesses being disabled after entering IEEE
		 * Power Down (when bit 11 of the PHY Control register is set)
		 */
		if ((hw->phy.type == e1000_phy_82578) &&
		    (hw->phy.revision >= 1) &&
		    (hw->phy.addr == 2) &&
		    !(MAX_PHY_REG_ADDRESS & reg) && (data & (1 << 11))) {
			u16 data2 = 0x7EFF;
			ret_val = e1000_access_phy_debug_regs_hv(hw,
								 (1 << 6) | 0x3,
								 &data2, false);
			if (ret_val)
				goto out;
		}

		if (reg > MAX_PHY_MULTI_PAGE_REG) {
			/* Page is shifted left, PHY expects (page x 32) */
			ret_val = e1000_set_page_igp(hw,
						     (page << IGP_PAGE_SHIFT));

			hw->phy.addr = phy_addr;

			if (ret_val)
				goto out;
		}
	}

	e_dbg("writing PHY page %d (or 0x%x shifted) reg 0x%x\n", page,
	      page << IGP_PAGE_SHIFT, reg);

	ret_val = e1000e_write_phy_reg_mdic(hw, MAX_PHY_REG_ADDRESS & reg,
	                                  data);

out:
	if (!locked)
		hw->phy.ops.release(hw);

	return ret_val;
}

/**
 *  e1000_write_phy_reg_hv - Write HV PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Acquires semaphore then writes the data to PHY register at the offset.
 *  Release the acquired semaphores before exiting.
 **/
s32 e1000_write_phy_reg_hv(struct e1000_hw *hw, u32 offset, u16 data)
{
	return __e1000_write_phy_reg_hv(hw, offset, data, false, false);
}

/**
 *  e1000_write_phy_reg_hv_locked - Write HV PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Writes the data to PHY register at the offset.  Assumes semaphore
 *  already acquired.
 **/
s32 e1000_write_phy_reg_hv_locked(struct e1000_hw *hw, u32 offset, u16 data)
{
	return __e1000_write_phy_reg_hv(hw, offset, data, true, false);
}

/**
 *  e1000_write_phy_reg_page_hv - Write HV PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Writes the data to PHY register at the offset.  Assumes semaphore
 *  already acquired and page already set.
 **/
s32 e1000_write_phy_reg_page_hv(struct e1000_hw *hw, u32 offset, u16 data)
{
	return __e1000_write_phy_reg_hv(hw, offset, data, true, true);
}

/**
 *  e1000_get_phy_addr_for_hv_page - Get PHY address based on page
 *  @page: page to be accessed
 **/
static u32 e1000_get_phy_addr_for_hv_page(u32 page)
{
	u32 phy_addr = 2;

	if (page >= HV_INTC_FC_PAGE_START)
		phy_addr = 1;

	return phy_addr;
}

/**
 *  e1000_access_phy_debug_regs_hv - Read HV PHY vendor specific high registers
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read or written
 *  @data: pointer to the data to be read or written
 *  @read: determines if operation is read or write
 *
 *  Reads the PHY register at offset and stores the retreived information
 *  in data.  Assumes semaphore already acquired.  Note that the procedure
 *  to access these regs uses the address port and data port to read/write.
 *  These accesses done with PHY address 2 and without using pages.
 **/
static s32 e1000_access_phy_debug_regs_hv(struct e1000_hw *hw, u32 offset,
                                          u16 *data, bool read)
{
	s32 ret_val;
	u32 addr_reg;
	u32 data_reg;

	/* This takes care of the difference with desktop vs mobile phy */
	addr_reg = (hw->phy.type == e1000_phy_82578) ?
	           I82578_ADDR_REG : I82577_ADDR_REG;
	data_reg = addr_reg + 1;

	/* All operations in this function are phy address 2 */
	hw->phy.addr = 2;

	/* masking with 0x3F to remove the page from offset */
	ret_val = e1000e_write_phy_reg_mdic(hw, addr_reg, (u16)offset & 0x3F);
	if (ret_val) {
		e_dbg("Could not write the Address Offset port register\n");
		return ret_val;
	}

	/* Read or write the data value next */
	if (read)
		ret_val = e1000e_read_phy_reg_mdic(hw, data_reg, data);
	else
		ret_val = e1000e_write_phy_reg_mdic(hw, data_reg, *data);

	if (ret_val)
		e_dbg("Could not access the Data port register\n");

	return ret_val;
}

/**
 *  e1000_link_stall_workaround_hv - Si workaround
 *  @hw: pointer to the HW structure
 *
 *  This function works around a Si bug where the link partner can get
 *  a link up indication before the PHY does.  If small packets are sent
 *  by the link partner they can be placed in the packet buffer without
 *  being properly accounted for by the PHY and will stall preventing
 *  further packets from being received.  The workaround is to clear the
 *  packet buffer after the PHY detects link up.
 **/
s32 e1000_link_stall_workaround_hv(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	u16 data;

	if (hw->phy.type != e1000_phy_82578)
		return 0;

	/* Do not apply workaround if in PHY loopback bit 14 set */
	e1e_rphy(hw, PHY_CONTROL, &data);
	if (data & PHY_CONTROL_LB)
		return 0;

	/* check if link is up and at 1Gbps */
	ret_val = e1e_rphy(hw, BM_CS_STATUS, &data);
	if (ret_val)
		return ret_val;

	data &= BM_CS_STATUS_LINK_UP | BM_CS_STATUS_RESOLVED |
		BM_CS_STATUS_SPEED_MASK;

	if (data != (BM_CS_STATUS_LINK_UP | BM_CS_STATUS_RESOLVED |
		     BM_CS_STATUS_SPEED_1000))
		return 0;

	msleep(200);

	/* flush the packets in the fifo buffer */
	ret_val = e1e_wphy(hw, HV_MUX_DATA_CTRL, HV_MUX_DATA_CTRL_GEN_TO_MAC |
			   HV_MUX_DATA_CTRL_FORCE_SPEED);
	if (ret_val)
		return ret_val;

	return e1e_wphy(hw, HV_MUX_DATA_CTRL, HV_MUX_DATA_CTRL_GEN_TO_MAC);
}

/**
 *  e1000_check_polarity_82577 - Checks the polarity.
 *  @hw: pointer to the HW structure
 *
 *  Success returns 0, Failure returns -E1000_ERR_PHY (-2)
 *
 *  Polarity is determined based on the PHY specific status register.
 **/
s32 e1000_check_polarity_82577(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	ret_val = e1e_rphy(hw, I82577_PHY_STATUS_2, &data);

	if (!ret_val)
		phy->cable_polarity = (data & I82577_PHY_STATUS2_REV_POLARITY)
		                      ? e1000_rev_polarity_reversed
		                      : e1000_rev_polarity_normal;

	return ret_val;
}

/**
 *  e1000_phy_force_speed_duplex_82577 - Force speed/duplex for I82577 PHY
 *  @hw: pointer to the HW structure
 *
 *  Calls the PHY setup function to force speed and duplex.
 **/
s32 e1000_phy_force_speed_duplex_82577(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data;
	bool link;

	ret_val = e1e_rphy(hw, PHY_CONTROL, &phy_data);
	if (ret_val)
		return ret_val;

	e1000e_phy_force_speed_duplex_setup(hw, &phy_data);

	ret_val = e1e_wphy(hw, PHY_CONTROL, phy_data);
	if (ret_val)
		return ret_val;

	udelay(1);

	if (phy->autoneg_wait_to_complete) {
		e_dbg("Waiting for forced speed/duplex link on 82577 phy\n");

		ret_val = e1000e_phy_has_link_generic(hw, PHY_FORCE_LIMIT,
						      100000, &link);
		if (ret_val)
			return ret_val;

		if (!link)
			e_dbg("Link taking longer than expected.\n");

		/* Try once more */
		ret_val = e1000e_phy_has_link_generic(hw, PHY_FORCE_LIMIT,
						      100000, &link);
	}

	return ret_val;
}

/**
 *  e1000_get_phy_info_82577 - Retrieve I82577 PHY information
 *  @hw: pointer to the HW structure
 *
 *  Read PHY status to determine if link is up.  If link is up, then
 *  set/determine 10base-T extended distance and polarity correction.  Read
 *  PHY port status to determine MDI/MDIx and speed.  Based on the speed,
 *  determine on the cable length, local and remote receiver.
 **/
s32 e1000_get_phy_info_82577(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;
	bool link;

	ret_val = e1000e_phy_has_link_generic(hw, 1, 0, &link);
	if (ret_val)
		return ret_val;

	if (!link) {
		e_dbg("Phy info is only valid if link is up\n");
		return -E1000_ERR_CONFIG;
	}

	phy->polarity_correction = true;

	ret_val = e1000_check_polarity_82577(hw);
	if (ret_val)
		return ret_val;

	ret_val = e1e_rphy(hw, I82577_PHY_STATUS_2, &data);
	if (ret_val)
		return ret_val;

	phy->is_mdix = !!(data & I82577_PHY_STATUS2_MDIX);

	if ((data & I82577_PHY_STATUS2_SPEED_MASK) ==
	    I82577_PHY_STATUS2_SPEED_1000MBPS) {
		ret_val = hw->phy.ops.get_cable_length(hw);
		if (ret_val)
			return ret_val;

		ret_val = e1e_rphy(hw, PHY_1000T_STATUS, &data);
		if (ret_val)
			return ret_val;

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

	return 0;
}

/**
 *  e1000_get_cable_length_82577 - Determine cable length for 82577 PHY
 *  @hw: pointer to the HW structure
 *
 * Reads the diagnostic status register and verifies result is valid before
 * placing it in the phy_cable_length field.
 **/
s32 e1000_get_cable_length_82577(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_data, length;

	ret_val = e1e_rphy(hw, I82577_PHY_DIAG_STATUS, &phy_data);
	if (ret_val)
		return ret_val;

	length = (phy_data & I82577_DSTATUS_CABLE_LENGTH) >>
	         I82577_DSTATUS_CABLE_LENGTH_SHIFT;

	if (length == E1000_CABLE_LENGTH_UNDEFINED)
		return -E1000_ERR_PHY;

	phy->cable_length = length;

	return 0;
}
