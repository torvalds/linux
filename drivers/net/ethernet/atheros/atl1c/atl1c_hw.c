/*
 * Copyright(c) 2007 Atheros Corporation. All rights reserved.
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/crc32.h>

#include "atl1c.h"

/*
 * check_eeprom_exist
 * return 1 if eeprom exist
 */
int atl1c_check_eeprom_exist(struct atl1c_hw *hw)
{
	u32 data;

	AT_READ_REG(hw, REG_TWSI_DEBUG, &data);
	if (data & TWSI_DEBUG_DEV_EXIST)
		return 1;

	AT_READ_REG(hw, REG_MASTER_CTRL, &data);
	if (data & MASTER_CTRL_OTP_SEL)
		return 1;
	return 0;
}

void atl1c_hw_set_mac_addr(struct atl1c_hw *hw, u8 *mac_addr)
{
	u32 value;
	/*
	 * 00-0B-6A-F6-00-DC
	 * 0:  6AF600DC 1: 000B
	 * low dword
	 */
	value = mac_addr[2] << 24 |
		mac_addr[3] << 16 |
		mac_addr[4] << 8  |
		mac_addr[5];
	AT_WRITE_REG_ARRAY(hw, REG_MAC_STA_ADDR, 0, value);
	/* hight dword */
	value = mac_addr[0] << 8 |
		mac_addr[1];
	AT_WRITE_REG_ARRAY(hw, REG_MAC_STA_ADDR, 1, value);
}

/* read mac address from hardware register */
static bool atl1c_read_current_addr(struct atl1c_hw *hw, u8 *eth_addr)
{
	u32 addr[2];

	AT_READ_REG(hw, REG_MAC_STA_ADDR, &addr[0]);
	AT_READ_REG(hw, REG_MAC_STA_ADDR + 4, &addr[1]);

	*(u32 *) &eth_addr[2] = htonl(addr[0]);
	*(u16 *) &eth_addr[0] = htons((u16)addr[1]);

	return is_valid_ether_addr(eth_addr);
}

/*
 * atl1c_get_permanent_address
 * return 0 if get valid mac address,
 */
static int atl1c_get_permanent_address(struct atl1c_hw *hw)
{
	u32 i;
	u32 otp_ctrl_data;
	u32 twsi_ctrl_data;
	u16 phy_data;
	bool raise_vol = false;

	/* MAC-address from BIOS is the 1st priority */
	if (atl1c_read_current_addr(hw, hw->perm_mac_addr))
		return 0;

	/* init */
	AT_READ_REG(hw, REG_OTP_CTRL, &otp_ctrl_data);
	if (atl1c_check_eeprom_exist(hw)) {
		if (hw->nic_type == athr_l1c || hw->nic_type == athr_l2c) {
			/* Enable OTP CLK */
			if (!(otp_ctrl_data & OTP_CTRL_CLK_EN)) {
				otp_ctrl_data |= OTP_CTRL_CLK_EN;
				AT_WRITE_REG(hw, REG_OTP_CTRL, otp_ctrl_data);
				AT_WRITE_FLUSH(hw);
				msleep(1);
			}
		}
		/* raise voltage temporally for l2cb */
		if (hw->nic_type == athr_l2c_b || hw->nic_type == athr_l2c_b2) {
			atl1c_read_phy_dbg(hw, MIIDBG_ANACTRL, &phy_data);
			phy_data &= ~ANACTRL_HB_EN;
			atl1c_write_phy_dbg(hw, MIIDBG_ANACTRL, phy_data);
			atl1c_read_phy_dbg(hw, MIIDBG_VOLT_CTRL, &phy_data);
			phy_data |= VOLT_CTRL_SWLOWEST;
			atl1c_write_phy_dbg(hw, MIIDBG_VOLT_CTRL, phy_data);
			udelay(20);
			raise_vol = true;
		}

		AT_READ_REG(hw, REG_TWSI_CTRL, &twsi_ctrl_data);
		twsi_ctrl_data |= TWSI_CTRL_SW_LDSTART;
		AT_WRITE_REG(hw, REG_TWSI_CTRL, twsi_ctrl_data);
		for (i = 0; i < AT_TWSI_EEPROM_TIMEOUT; i++) {
			msleep(10);
			AT_READ_REG(hw, REG_TWSI_CTRL, &twsi_ctrl_data);
			if ((twsi_ctrl_data & TWSI_CTRL_SW_LDSTART) == 0)
				break;
		}
		if (i >= AT_TWSI_EEPROM_TIMEOUT)
			return -1;
	}
	/* Disable OTP_CLK */
	if ((hw->nic_type == athr_l1c || hw->nic_type == athr_l2c)) {
		otp_ctrl_data &= ~OTP_CTRL_CLK_EN;
		AT_WRITE_REG(hw, REG_OTP_CTRL, otp_ctrl_data);
		msleep(1);
	}
	if (raise_vol) {
		atl1c_read_phy_dbg(hw, MIIDBG_ANACTRL, &phy_data);
		phy_data |= ANACTRL_HB_EN;
		atl1c_write_phy_dbg(hw, MIIDBG_ANACTRL, phy_data);
		atl1c_read_phy_dbg(hw, MIIDBG_VOLT_CTRL, &phy_data);
		phy_data &= ~VOLT_CTRL_SWLOWEST;
		atl1c_write_phy_dbg(hw, MIIDBG_VOLT_CTRL, phy_data);
		udelay(20);
	}

	if (atl1c_read_current_addr(hw, hw->perm_mac_addr))
		return 0;

	return -1;
}

bool atl1c_read_eeprom(struct atl1c_hw *hw, u32 offset, u32 *p_value)
{
	int i;
	int ret = false;
	u32 otp_ctrl_data;
	u32 control;
	u32 data;

	if (offset & 3)
		return ret; /* address do not align */

	AT_READ_REG(hw, REG_OTP_CTRL, &otp_ctrl_data);
	if (!(otp_ctrl_data & OTP_CTRL_CLK_EN))
		AT_WRITE_REG(hw, REG_OTP_CTRL,
				(otp_ctrl_data | OTP_CTRL_CLK_EN));

	AT_WRITE_REG(hw, REG_EEPROM_DATA_LO, 0);
	control = (offset & EEPROM_CTRL_ADDR_MASK) << EEPROM_CTRL_ADDR_SHIFT;
	AT_WRITE_REG(hw, REG_EEPROM_CTRL, control);

	for (i = 0; i < 10; i++) {
		udelay(100);
		AT_READ_REG(hw, REG_EEPROM_CTRL, &control);
		if (control & EEPROM_CTRL_RW)
			break;
	}
	if (control & EEPROM_CTRL_RW) {
		AT_READ_REG(hw, REG_EEPROM_CTRL, &data);
		AT_READ_REG(hw, REG_EEPROM_DATA_LO, p_value);
		data = data & 0xFFFF;
		*p_value = swab32((data << 16) | (*p_value >> 16));
		ret = true;
	}
	if (!(otp_ctrl_data & OTP_CTRL_CLK_EN))
		AT_WRITE_REG(hw, REG_OTP_CTRL, otp_ctrl_data);

	return ret;
}
/*
 * Reads the adapter's MAC address from the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 */
int atl1c_read_mac_addr(struct atl1c_hw *hw)
{
	int err = 0;

	err = atl1c_get_permanent_address(hw);
	if (err)
		random_ether_addr(hw->perm_mac_addr);

	memcpy(hw->mac_addr, hw->perm_mac_addr, sizeof(hw->perm_mac_addr));
	return err;
}

/*
 * atl1c_hash_mc_addr
 *  purpose
 *      set hash value for a multicast address
 *      hash calcu processing :
 *          1. calcu 32bit CRC for multicast address
 *          2. reverse crc with MSB to LSB
 */
u32 atl1c_hash_mc_addr(struct atl1c_hw *hw, u8 *mc_addr)
{
	u32 crc32;
	u32 value = 0;
	int i;

	crc32 = ether_crc_le(6, mc_addr);
	for (i = 0; i < 32; i++)
		value |= (((crc32 >> i) & 1) << (31 - i));

	return value;
}

/*
 * Sets the bit in the multicast table corresponding to the hash value.
 * hw - Struct containing variables accessed by shared code
 * hash_value - Multicast address hash value
 */
void atl1c_hash_set(struct atl1c_hw *hw, u32 hash_value)
{
	u32 hash_bit, hash_reg;
	u32 mta;

	/*
	 * The HASH Table  is a register array of 2 32-bit registers.
	 * It is treated like an array of 64 bits.  We want to set
	 * bit BitArray[hash_value]. So we figure out what register
	 * the bit is in, read it, OR in the new bit, then write
	 * back the new value.  The register is determined by the
	 * upper bit of the hash value and the bit within that
	 * register are determined by the lower 5 bits of the value.
	 */
	hash_reg = (hash_value >> 31) & 0x1;
	hash_bit = (hash_value >> 26) & 0x1F;

	mta = AT_READ_REG_ARRAY(hw, REG_RX_HASH_TABLE, hash_reg);

	mta |= (1 << hash_bit);

	AT_WRITE_REG_ARRAY(hw, REG_RX_HASH_TABLE, hash_reg, mta);
}

/*
 * wait mdio module be idle
 * return true: idle
 *        false: still busy
 */
bool atl1c_wait_mdio_idle(struct atl1c_hw *hw)
{
	u32 val;
	int i;

	for (i = 0; i < MDIO_MAX_AC_TO; i++) {
		AT_READ_REG(hw, REG_MDIO_CTRL, &val);
		if (!(val & (MDIO_CTRL_BUSY | MDIO_CTRL_START)))
			break;
		udelay(10);
	}

	return i != MDIO_MAX_AC_TO;
}

void atl1c_stop_phy_polling(struct atl1c_hw *hw)
{
	if (!(hw->ctrl_flags & ATL1C_FPGA_VERSION))
		return;

	AT_WRITE_REG(hw, REG_MDIO_CTRL, 0);
	atl1c_wait_mdio_idle(hw);
}

void atl1c_start_phy_polling(struct atl1c_hw *hw, u16 clk_sel)
{
	u32 val;

	if (!(hw->ctrl_flags & ATL1C_FPGA_VERSION))
		return;

	val = MDIO_CTRL_SPRES_PRMBL |
		FIELDX(MDIO_CTRL_CLK_SEL, clk_sel) |
		FIELDX(MDIO_CTRL_REG, 1) |
		MDIO_CTRL_START |
		MDIO_CTRL_OP_READ;
	AT_WRITE_REG(hw, REG_MDIO_CTRL, val);
	atl1c_wait_mdio_idle(hw);
	val |= MDIO_CTRL_AP_EN;
	val &= ~MDIO_CTRL_START;
	AT_WRITE_REG(hw, REG_MDIO_CTRL, val);
	udelay(30);
}


/*
 * atl1c_read_phy_core
 * core funtion to read register in PHY via MDIO control regsiter.
 * ext: extension register (see IEEE 802.3)
 * dev: device address (see IEEE 802.3 DEVAD, PRTAD is fixed to 0)
 * reg: reg to read
 */
int atl1c_read_phy_core(struct atl1c_hw *hw, bool ext, u8 dev,
			u16 reg, u16 *phy_data)
{
	u32 val;
	u16 clk_sel = MDIO_CTRL_CLK_25_4;

	atl1c_stop_phy_polling(hw);

	*phy_data = 0;

	/* only l2c_b2 & l1d_2 could use slow clock */
	if ((hw->nic_type == athr_l2c_b2 || hw->nic_type == athr_l1d_2) &&
		hw->hibernate)
		clk_sel = MDIO_CTRL_CLK_25_128;
	if (ext) {
		val = FIELDX(MDIO_EXTN_DEVAD, dev) | FIELDX(MDIO_EXTN_REG, reg);
		AT_WRITE_REG(hw, REG_MDIO_EXTN, val);
		val = MDIO_CTRL_SPRES_PRMBL |
			FIELDX(MDIO_CTRL_CLK_SEL, clk_sel) |
			MDIO_CTRL_START |
			MDIO_CTRL_MODE_EXT |
			MDIO_CTRL_OP_READ;
	} else {
		val = MDIO_CTRL_SPRES_PRMBL |
			FIELDX(MDIO_CTRL_CLK_SEL, clk_sel) |
			FIELDX(MDIO_CTRL_REG, reg) |
			MDIO_CTRL_START |
			MDIO_CTRL_OP_READ;
	}
	AT_WRITE_REG(hw, REG_MDIO_CTRL, val);

	if (!atl1c_wait_mdio_idle(hw))
		return -1;

	AT_READ_REG(hw, REG_MDIO_CTRL, &val);
	*phy_data = (u16)FIELD_GETX(val, MDIO_CTRL_DATA);

	atl1c_start_phy_polling(hw, clk_sel);

	return 0;
}

/*
 * atl1c_write_phy_core
 * core funtion to write to register in PHY via MDIO control regsiter.
 * ext: extension register (see IEEE 802.3)
 * dev: device address (see IEEE 802.3 DEVAD, PRTAD is fixed to 0)
 * reg: reg to write
 */
int atl1c_write_phy_core(struct atl1c_hw *hw, bool ext, u8 dev,
			u16 reg, u16 phy_data)
{
	u32 val;
	u16 clk_sel = MDIO_CTRL_CLK_25_4;

	atl1c_stop_phy_polling(hw);


	/* only l2c_b2 & l1d_2 could use slow clock */
	if ((hw->nic_type == athr_l2c_b2 || hw->nic_type == athr_l1d_2) &&
		hw->hibernate)
		clk_sel = MDIO_CTRL_CLK_25_128;

	if (ext) {
		val = FIELDX(MDIO_EXTN_DEVAD, dev) | FIELDX(MDIO_EXTN_REG, reg);
		AT_WRITE_REG(hw, REG_MDIO_EXTN, val);
		val = MDIO_CTRL_SPRES_PRMBL |
			FIELDX(MDIO_CTRL_CLK_SEL, clk_sel) |
			FIELDX(MDIO_CTRL_DATA, phy_data) |
			MDIO_CTRL_START |
			MDIO_CTRL_MODE_EXT;
	} else {
		val = MDIO_CTRL_SPRES_PRMBL |
			FIELDX(MDIO_CTRL_CLK_SEL, clk_sel) |
			FIELDX(MDIO_CTRL_DATA, phy_data) |
			FIELDX(MDIO_CTRL_REG, reg) |
			MDIO_CTRL_START;
	}
	AT_WRITE_REG(hw, REG_MDIO_CTRL, val);

	if (!atl1c_wait_mdio_idle(hw))
		return -1;

	atl1c_start_phy_polling(hw, clk_sel);

	return 0;
}

/*
 * Reads the value from a PHY register
 * hw - Struct containing variables accessed by shared code
 * reg_addr - address of the PHY register to read
 */
int atl1c_read_phy_reg(struct atl1c_hw *hw, u16 reg_addr, u16 *phy_data)
{
	return atl1c_read_phy_core(hw, false, 0, reg_addr, phy_data);
}

/*
 * Writes a value to a PHY register
 * hw - Struct containing variables accessed by shared code
 * reg_addr - address of the PHY register to write
 * data - data to write to the PHY
 */
int atl1c_write_phy_reg(struct atl1c_hw *hw, u32 reg_addr, u16 phy_data)
{
	return atl1c_write_phy_core(hw, false, 0, reg_addr, phy_data);
}

/* read from PHY extension register */
int atl1c_read_phy_ext(struct atl1c_hw *hw, u8 dev_addr,
			u16 reg_addr, u16 *phy_data)
{
	return atl1c_read_phy_core(hw, true, dev_addr, reg_addr, phy_data);
}

/* write to PHY extension register */
int atl1c_write_phy_ext(struct atl1c_hw *hw, u8 dev_addr,
			u16 reg_addr, u16 phy_data)
{
	return atl1c_write_phy_core(hw, true, dev_addr, reg_addr, phy_data);
}

int atl1c_read_phy_dbg(struct atl1c_hw *hw, u16 reg_addr, u16 *phy_data)
{
	int err;

	err = atl1c_write_phy_reg(hw, MII_DBG_ADDR, reg_addr);
	if (unlikely(err))
		return err;
	else
		err = atl1c_read_phy_reg(hw, MII_DBG_DATA, phy_data);

	return err;
}

int atl1c_write_phy_dbg(struct atl1c_hw *hw, u16 reg_addr, u16 phy_data)
{
	int err;

	err = atl1c_write_phy_reg(hw, MII_DBG_ADDR, reg_addr);
	if (unlikely(err))
		return err;
	else
		err = atl1c_write_phy_reg(hw, MII_DBG_DATA, phy_data);

	return err;
}

/*
 * Configures PHY autoneg and flow control advertisement settings
 *
 * hw - Struct containing variables accessed by shared code
 */
static int atl1c_phy_setup_adv(struct atl1c_hw *hw)
{
	u16 mii_adv_data = ADVERTISE_DEFAULT_CAP & ~ADVERTISE_ALL;
	u16 mii_giga_ctrl_data = GIGA_CR_1000T_DEFAULT_CAP &
				~GIGA_CR_1000T_SPEED_MASK;

	if (hw->autoneg_advertised & ADVERTISED_10baseT_Half)
		mii_adv_data |= ADVERTISE_10HALF;
	if (hw->autoneg_advertised & ADVERTISED_10baseT_Full)
		mii_adv_data |= ADVERTISE_10FULL;
	if (hw->autoneg_advertised & ADVERTISED_100baseT_Half)
		mii_adv_data |= ADVERTISE_100HALF;
	if (hw->autoneg_advertised & ADVERTISED_100baseT_Full)
		mii_adv_data |= ADVERTISE_100FULL;

	if (hw->autoneg_advertised & ADVERTISED_Autoneg)
		mii_adv_data |= ADVERTISE_10HALF  | ADVERTISE_10FULL |
				ADVERTISE_100HALF | ADVERTISE_100FULL;

	if (hw->link_cap_flags & ATL1C_LINK_CAP_1000M) {
		if (hw->autoneg_advertised & ADVERTISED_1000baseT_Half)
			mii_giga_ctrl_data |= ADVERTISE_1000HALF;
		if (hw->autoneg_advertised & ADVERTISED_1000baseT_Full)
			mii_giga_ctrl_data |= ADVERTISE_1000FULL;
		if (hw->autoneg_advertised & ADVERTISED_Autoneg)
			mii_giga_ctrl_data |= ADVERTISE_1000HALF |
					ADVERTISE_1000FULL;
	}

	if (atl1c_write_phy_reg(hw, MII_ADVERTISE, mii_adv_data) != 0 ||
	    atl1c_write_phy_reg(hw, MII_CTRL1000, mii_giga_ctrl_data) != 0)
		return -1;
	return 0;
}

void atl1c_phy_disable(struct atl1c_hw *hw)
{
	atl1c_power_saving(hw, 0);
}


int atl1c_phy_reset(struct atl1c_hw *hw)
{
	struct atl1c_adapter *adapter = hw->adapter;
	struct pci_dev *pdev = adapter->pdev;
	u16 phy_data;
	u32 phy_ctrl_data, lpi_ctrl;
	int err;

	/* reset PHY core */
	AT_READ_REG(hw, REG_GPHY_CTRL, &phy_ctrl_data);
	phy_ctrl_data &= ~(GPHY_CTRL_EXT_RESET | GPHY_CTRL_PHY_IDDQ |
		GPHY_CTRL_GATE_25M_EN | GPHY_CTRL_PWDOWN_HW | GPHY_CTRL_CLS);
	phy_ctrl_data |= GPHY_CTRL_SEL_ANA_RST;
	if (!(hw->ctrl_flags & ATL1C_HIB_DISABLE))
		phy_ctrl_data |= (GPHY_CTRL_HIB_EN | GPHY_CTRL_HIB_PULSE);
	else
		phy_ctrl_data &= ~(GPHY_CTRL_HIB_EN | GPHY_CTRL_HIB_PULSE);
	AT_WRITE_REG(hw, REG_GPHY_CTRL, phy_ctrl_data);
	AT_WRITE_FLUSH(hw);
	udelay(10);
	AT_WRITE_REG(hw, REG_GPHY_CTRL, phy_ctrl_data | GPHY_CTRL_EXT_RESET);
	AT_WRITE_FLUSH(hw);
	udelay(10 * GPHY_CTRL_EXT_RST_TO);	/* delay 800us */

	/* switch clock */
	if (hw->nic_type == athr_l2c_b) {
		atl1c_read_phy_dbg(hw, MIIDBG_CFGLPSPD, &phy_data);
		atl1c_write_phy_dbg(hw, MIIDBG_CFGLPSPD,
			phy_data & ~CFGLPSPD_RSTCNT_CLK125SW);
	}

	/* tx-half amplitude issue fix */
	if (hw->nic_type == athr_l2c_b || hw->nic_type == athr_l2c_b2) {
		atl1c_read_phy_dbg(hw, MIIDBG_CABLE1TH_DET, &phy_data);
		phy_data |= CABLE1TH_DET_EN;
		atl1c_write_phy_dbg(hw, MIIDBG_CABLE1TH_DET, phy_data);
	}

	/* clear bit3 of dbgport 3B to lower voltage */
	if (!(hw->ctrl_flags & ATL1C_HIB_DISABLE)) {
		if (hw->nic_type == athr_l2c_b || hw->nic_type == athr_l2c_b2) {
			atl1c_read_phy_dbg(hw, MIIDBG_VOLT_CTRL, &phy_data);
			phy_data &= ~VOLT_CTRL_SWLOWEST;
			atl1c_write_phy_dbg(hw, MIIDBG_VOLT_CTRL, phy_data);
		}
		/* power saving config */
		phy_data =
			hw->nic_type == athr_l1d || hw->nic_type == athr_l1d_2 ?
			L1D_LEGCYPS_DEF : L1C_LEGCYPS_DEF;
		atl1c_write_phy_dbg(hw, MIIDBG_LEGCYPS, phy_data);
		/* hib */
		atl1c_write_phy_dbg(hw, MIIDBG_SYSMODCTRL,
			SYSMODCTRL_IECHOADJ_DEF);
	} else {
		/* disable pws */
		atl1c_read_phy_dbg(hw, MIIDBG_LEGCYPS, &phy_data);
		atl1c_write_phy_dbg(hw, MIIDBG_LEGCYPS,
			phy_data & ~LEGCYPS_EN);
		/* disable hibernate */
		atl1c_read_phy_dbg(hw, MIIDBG_HIBNEG, &phy_data);
		atl1c_write_phy_dbg(hw, MIIDBG_HIBNEG,
			phy_data & HIBNEG_PSHIB_EN);
	}
	/* disable AZ(EEE) by default */
	if (hw->nic_type == athr_l1d || hw->nic_type == athr_l1d_2 ||
	    hw->nic_type == athr_l2c_b2) {
		AT_READ_REG(hw, REG_LPI_CTRL, &lpi_ctrl);
		AT_WRITE_REG(hw, REG_LPI_CTRL, lpi_ctrl & ~LPI_CTRL_EN);
		atl1c_write_phy_ext(hw, MIIEXT_ANEG, MIIEXT_LOCAL_EEEADV, 0);
		atl1c_write_phy_ext(hw, MIIEXT_PCS, MIIEXT_CLDCTRL3,
			L2CB_CLDCTRL3);
	}

	/* other debug port to set */
	atl1c_write_phy_dbg(hw, MIIDBG_ANACTRL, ANACTRL_DEF);
	atl1c_write_phy_dbg(hw, MIIDBG_SRDSYSMOD, SRDSYSMOD_DEF);
	atl1c_write_phy_dbg(hw, MIIDBG_TST10BTCFG, TST10BTCFG_DEF);
	/* UNH-IOL test issue, set bit7 */
	atl1c_write_phy_dbg(hw, MIIDBG_TST100BTCFG,
		TST100BTCFG_DEF | TST100BTCFG_LITCH_EN);

	/* set phy interrupt mask */
	phy_data = IER_LINK_UP | IER_LINK_DOWN;
	err = atl1c_write_phy_reg(hw, MII_IER, phy_data);
	if (err) {
		if (netif_msg_hw(adapter))
			dev_err(&pdev->dev,
				"Error enable PHY linkChange Interrupt\n");
		return err;
	}
	return 0;
}

int atl1c_phy_init(struct atl1c_hw *hw)
{
	struct atl1c_adapter *adapter = (struct atl1c_adapter *)hw->adapter;
	struct pci_dev *pdev = adapter->pdev;
	int ret_val;
	u16 mii_bmcr_data = BMCR_RESET;

	if ((atl1c_read_phy_reg(hw, MII_PHYSID1, &hw->phy_id1) != 0) ||
		(atl1c_read_phy_reg(hw, MII_PHYSID2, &hw->phy_id2) != 0)) {
		dev_err(&pdev->dev, "Error get phy ID\n");
		return -1;
	}
	switch (hw->media_type) {
	case MEDIA_TYPE_AUTO_SENSOR:
		ret_val = atl1c_phy_setup_adv(hw);
		if (ret_val) {
			if (netif_msg_link(adapter))
				dev_err(&pdev->dev,
					"Error Setting up Auto-Negotiation\n");
			return ret_val;
		}
		mii_bmcr_data |= BMCR_ANENABLE | BMCR_ANRESTART;
		break;
	case MEDIA_TYPE_100M_FULL:
		mii_bmcr_data |= BMCR_SPEED100 | BMCR_FULLDPLX;
		break;
	case MEDIA_TYPE_100M_HALF:
		mii_bmcr_data |= BMCR_SPEED100;
		break;
	case MEDIA_TYPE_10M_FULL:
		mii_bmcr_data |= BMCR_FULLDPLX;
		break;
	case MEDIA_TYPE_10M_HALF:
		break;
	default:
		if (netif_msg_link(adapter))
			dev_err(&pdev->dev, "Wrong Media type %d\n",
				hw->media_type);
		return -1;
		break;
	}

	ret_val = atl1c_write_phy_reg(hw, MII_BMCR, mii_bmcr_data);
	if (ret_val)
		return ret_val;
	hw->phy_configured = true;

	return 0;
}

/*
 * Detects the current speed and duplex settings of the hardware.
 *
 * hw - Struct containing variables accessed by shared code
 * speed - Speed of the connection
 * duplex - Duplex setting of the connection
 */
int atl1c_get_speed_and_duplex(struct atl1c_hw *hw, u16 *speed, u16 *duplex)
{
	int err;
	u16 phy_data;

	/* Read   PHY Specific Status Register (17) */
	err = atl1c_read_phy_reg(hw, MII_GIGA_PSSR, &phy_data);
	if (err)
		return err;

	if (!(phy_data & GIGA_PSSR_SPD_DPLX_RESOLVED))
		return -1;

	switch (phy_data & GIGA_PSSR_SPEED) {
	case GIGA_PSSR_1000MBS:
		*speed = SPEED_1000;
		break;
	case GIGA_PSSR_100MBS:
		*speed = SPEED_100;
		break;
	case  GIGA_PSSR_10MBS:
		*speed = SPEED_10;
		break;
	default:
		return -1;
		break;
	}

	if (phy_data & GIGA_PSSR_DPLX)
		*duplex = FULL_DUPLEX;
	else
		*duplex = HALF_DUPLEX;

	return 0;
}

/* select one link mode to get lower power consumption */
int atl1c_phy_to_ps_link(struct atl1c_hw *hw)
{
	struct atl1c_adapter *adapter = (struct atl1c_adapter *)hw->adapter;
	struct pci_dev *pdev = adapter->pdev;
	int ret = 0;
	u16 autoneg_advertised = ADVERTISED_10baseT_Half;
	u16 save_autoneg_advertised;
	u16 phy_data;
	u16 mii_lpa_data;
	u16 speed = SPEED_0;
	u16 duplex = FULL_DUPLEX;
	int i;

	atl1c_read_phy_reg(hw, MII_BMSR, &phy_data);
	atl1c_read_phy_reg(hw, MII_BMSR, &phy_data);
	if (phy_data & BMSR_LSTATUS) {
		atl1c_read_phy_reg(hw, MII_LPA, &mii_lpa_data);
		if (mii_lpa_data & LPA_10FULL)
			autoneg_advertised = ADVERTISED_10baseT_Full;
		else if (mii_lpa_data & LPA_10HALF)
			autoneg_advertised = ADVERTISED_10baseT_Half;
		else if (mii_lpa_data & LPA_100HALF)
			autoneg_advertised = ADVERTISED_100baseT_Half;
		else if (mii_lpa_data & LPA_100FULL)
			autoneg_advertised = ADVERTISED_100baseT_Full;

		save_autoneg_advertised = hw->autoneg_advertised;
		hw->phy_configured = false;
		hw->autoneg_advertised = autoneg_advertised;
		if (atl1c_restart_autoneg(hw) != 0) {
			dev_dbg(&pdev->dev, "phy autoneg failed\n");
			ret = -1;
		}
		hw->autoneg_advertised = save_autoneg_advertised;

		if (mii_lpa_data) {
			for (i = 0; i < AT_SUSPEND_LINK_TIMEOUT; i++) {
				mdelay(100);
				atl1c_read_phy_reg(hw, MII_BMSR, &phy_data);
				atl1c_read_phy_reg(hw, MII_BMSR, &phy_data);
				if (phy_data & BMSR_LSTATUS) {
					if (atl1c_get_speed_and_duplex(hw, &speed,
									&duplex) != 0)
						dev_dbg(&pdev->dev,
							"get speed and duplex failed\n");
					break;
				}
			}
		}
	} else {
		speed = SPEED_10;
		duplex = HALF_DUPLEX;
	}
	adapter->link_speed = speed;
	adapter->link_duplex = duplex;

	return ret;
}

int atl1c_restart_autoneg(struct atl1c_hw *hw)
{
	int err = 0;
	u16 mii_bmcr_data = BMCR_RESET;

	err = atl1c_phy_setup_adv(hw);
	if (err)
		return err;
	mii_bmcr_data |= BMCR_ANENABLE | BMCR_ANRESTART;

	return atl1c_write_phy_reg(hw, MII_BMCR, mii_bmcr_data);
}

int atl1c_power_saving(struct atl1c_hw *hw, u32 wufc)
{
	struct atl1c_adapter *adapter = (struct atl1c_adapter *)hw->adapter;
	struct pci_dev *pdev = adapter->pdev;
	u32 master_ctrl, mac_ctrl, phy_ctrl;
	u32 wol_ctrl, speed;
	u16 phy_data;

	wol_ctrl = 0;
	speed = adapter->link_speed == SPEED_1000 ?
		MAC_CTRL_SPEED_1000 : MAC_CTRL_SPEED_10_100;

	AT_READ_REG(hw, REG_MASTER_CTRL, &master_ctrl);
	AT_READ_REG(hw, REG_MAC_CTRL, &mac_ctrl);
	AT_READ_REG(hw, REG_GPHY_CTRL, &phy_ctrl);

	master_ctrl &= ~MASTER_CTRL_CLK_SEL_DIS;
	mac_ctrl = FIELD_SETX(mac_ctrl, MAC_CTRL_SPEED, speed);
	mac_ctrl &= ~(MAC_CTRL_DUPLX | MAC_CTRL_RX_EN | MAC_CTRL_TX_EN);
	if (adapter->link_duplex == FULL_DUPLEX)
		mac_ctrl |= MAC_CTRL_DUPLX;
	phy_ctrl &= ~(GPHY_CTRL_EXT_RESET | GPHY_CTRL_CLS);
	phy_ctrl |= GPHY_CTRL_SEL_ANA_RST | GPHY_CTRL_HIB_PULSE |
		GPHY_CTRL_HIB_EN;
	if (!wufc) { /* without WoL */
		master_ctrl |= MASTER_CTRL_CLK_SEL_DIS;
		phy_ctrl |= GPHY_CTRL_PHY_IDDQ | GPHY_CTRL_PWDOWN_HW;
		AT_WRITE_REG(hw, REG_MASTER_CTRL, master_ctrl);
		AT_WRITE_REG(hw, REG_MAC_CTRL, mac_ctrl);
		AT_WRITE_REG(hw, REG_GPHY_CTRL, phy_ctrl);
		AT_WRITE_REG(hw, REG_WOL_CTRL, 0);
		hw->phy_configured = false; /* re-init PHY when resume */
		return 0;
	}
	phy_ctrl |= GPHY_CTRL_EXT_RESET;
	if (wufc & AT_WUFC_MAG) {
		mac_ctrl |= MAC_CTRL_RX_EN | MAC_CTRL_BC_EN;
		wol_ctrl |= WOL_MAGIC_EN | WOL_MAGIC_PME_EN;
		if (hw->nic_type == athr_l2c_b && hw->revision_id == L2CB_V11)
			wol_ctrl |= WOL_PATTERN_EN | WOL_PATTERN_PME_EN;
	}
	if (wufc & AT_WUFC_LNKC) {
		wol_ctrl |= WOL_LINK_CHG_EN | WOL_LINK_CHG_PME_EN;
		if (atl1c_write_phy_reg(hw, MII_IER, IER_LINK_UP) != 0) {
			dev_dbg(&pdev->dev, "%s: write phy MII_IER faild.\n",
				atl1c_driver_name);
		}
	}
	/* clear PHY interrupt */
	atl1c_read_phy_reg(hw, MII_ISR, &phy_data);

	dev_dbg(&pdev->dev, "%s: suspend MAC=%x,MASTER=%x,PHY=0x%x,WOL=%x\n",
		atl1c_driver_name, mac_ctrl, master_ctrl, phy_ctrl, wol_ctrl);
	AT_WRITE_REG(hw, REG_MASTER_CTRL, master_ctrl);
	AT_WRITE_REG(hw, REG_MAC_CTRL, mac_ctrl);
	AT_WRITE_REG(hw, REG_GPHY_CTRL, phy_ctrl);
	AT_WRITE_REG(hw, REG_WOL_CTRL, wol_ctrl);

	return 0;
}


/* configure phy after Link change Event */
void atl1c_post_phy_linkchg(struct atl1c_hw *hw, u16 link_speed)
{
	u16 phy_val;
	bool adj_thresh = false;

	if (hw->nic_type == athr_l2c_b || hw->nic_type == athr_l2c_b2 ||
	    hw->nic_type == athr_l1d || hw->nic_type == athr_l1d_2)
		adj_thresh = true;

	if (link_speed != SPEED_0) { /* link up */
		/* az with brcm, half-amp */
		if (hw->nic_type == athr_l1d_2) {
			atl1c_read_phy_ext(hw, MIIEXT_PCS, MIIEXT_CLDCTRL6,
				&phy_val);
			phy_val = FIELD_GETX(phy_val, CLDCTRL6_CAB_LEN);
			phy_val = phy_val > CLDCTRL6_CAB_LEN_SHORT ?
				AZ_ANADECT_LONG : AZ_ANADECT_DEF;
			atl1c_write_phy_dbg(hw, MIIDBG_AZ_ANADECT, phy_val);
		}
		/* threshold adjust */
		if (adj_thresh && link_speed == SPEED_100 && hw->msi_lnkpatch) {
			atl1c_write_phy_dbg(hw, MIIDBG_MSE16DB, L1D_MSE16DB_UP);
			atl1c_write_phy_dbg(hw, MIIDBG_SYSMODCTRL,
				L1D_SYSMODCTRL_IECHOADJ_DEF);
		}
	} else { /* link down */
		if (adj_thresh && hw->msi_lnkpatch) {
			atl1c_write_phy_dbg(hw, MIIDBG_SYSMODCTRL,
				SYSMODCTRL_IECHOADJ_DEF);
			atl1c_write_phy_dbg(hw, MIIDBG_MSE16DB,
				L1D_MSE16DB_DOWN);
		}
	}
}
