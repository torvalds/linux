/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2009 Intel Corporation.

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

#ifndef _E1000_PHY_H_
#define _E1000_PHY_H_

enum e1000_ms_type {
	e1000_ms_hw_default = 0,
	e1000_ms_force_master,
	e1000_ms_force_slave,
	e1000_ms_auto
};

enum e1000_smart_speed {
	e1000_smart_speed_default = 0,
	e1000_smart_speed_on,
	e1000_smart_speed_off
};

s32  igb_check_downshift(struct e1000_hw *hw);
s32  igb_check_reset_block(struct e1000_hw *hw);
s32  igb_copper_link_setup_igp(struct e1000_hw *hw);
s32  igb_copper_link_setup_m88(struct e1000_hw *hw);
s32  igb_phy_force_speed_duplex_igp(struct e1000_hw *hw);
s32  igb_phy_force_speed_duplex_m88(struct e1000_hw *hw);
s32  igb_get_cable_length_m88(struct e1000_hw *hw);
s32  igb_get_cable_length_igp_2(struct e1000_hw *hw);
s32  igb_get_phy_id(struct e1000_hw *hw);
s32  igb_get_phy_info_igp(struct e1000_hw *hw);
s32  igb_get_phy_info_m88(struct e1000_hw *hw);
s32  igb_phy_sw_reset(struct e1000_hw *hw);
s32  igb_phy_hw_reset(struct e1000_hw *hw);
s32  igb_read_phy_reg_igp(struct e1000_hw *hw, u32 offset, u16 *data);
s32  igb_set_d3_lplu_state(struct e1000_hw *hw, bool active);
s32  igb_setup_copper_link(struct e1000_hw *hw);
s32  igb_write_phy_reg_igp(struct e1000_hw *hw, u32 offset, u16 data);
s32  igb_phy_has_link(struct e1000_hw *hw, u32 iterations,
				u32 usec_interval, bool *success);
s32  igb_phy_init_script_igp3(struct e1000_hw *hw);
s32  igb_read_phy_reg_mdic(struct e1000_hw *hw, u32 offset, u16 *data);
s32  igb_write_phy_reg_mdic(struct e1000_hw *hw, u32 offset, u16 data);
s32  igb_read_phy_reg_i2c(struct e1000_hw *hw, u32 offset, u16 *data);
s32  igb_write_phy_reg_i2c(struct e1000_hw *hw, u32 offset, u16 data);
s32  igb_copper_link_setup_82580(struct e1000_hw *hw);
s32  igb_get_phy_info_82580(struct e1000_hw *hw);
s32  igb_phy_force_speed_duplex_82580(struct e1000_hw *hw);
s32  igb_get_cable_length_82580(struct e1000_hw *hw);

/* IGP01E1000 Specific Registers */
#define IGP01E1000_PHY_PORT_CONFIG        0x10 /* Port Config */
#define IGP01E1000_PHY_PORT_STATUS        0x11 /* Status */
#define IGP01E1000_PHY_PORT_CTRL          0x12 /* Control */
#define IGP01E1000_PHY_LINK_HEALTH        0x13 /* PHY Link Health */
#define IGP02E1000_PHY_POWER_MGMT         0x19 /* Power Management */
#define IGP01E1000_PHY_PAGE_SELECT        0x1F /* Page Select */
#define IGP01E1000_PHY_PCS_INIT_REG       0x00B4
#define IGP01E1000_PHY_POLARITY_MASK      0x0078
#define IGP01E1000_PSCR_AUTO_MDIX         0x1000
#define IGP01E1000_PSCR_FORCE_MDI_MDIX    0x2000 /* 0=MDI, 1=MDIX */
#define IGP01E1000_PSCFR_SMART_SPEED      0x0080

#define I82580_ADDR_REG                   16
#define I82580_CFG_REG                    22
#define I82580_CFG_ASSERT_CRS_ON_TX       (1 << 15)
#define I82580_CFG_ENABLE_DOWNSHIFT       (3 << 10) /* auto downshift 100/10 */
#define I82580_CTRL_REG                   23
#define I82580_CTRL_DOWNSHIFT_MASK        (7 << 10)

/* 82580 specific PHY registers */
#define I82580_PHY_CTRL_2            18
#define I82580_PHY_LBK_CTRL          19
#define I82580_PHY_STATUS_2          26
#define I82580_PHY_DIAG_STATUS       31

/* I82580 PHY Status 2 */
#define I82580_PHY_STATUS2_REV_POLARITY   0x0400
#define I82580_PHY_STATUS2_MDIX           0x0800
#define I82580_PHY_STATUS2_SPEED_MASK     0x0300
#define I82580_PHY_STATUS2_SPEED_1000MBPS 0x0200
#define I82580_PHY_STATUS2_SPEED_100MBPS  0x0100

/* I82580 PHY Control 2 */
#define I82580_PHY_CTRL2_AUTO_MDIX        0x0400
#define I82580_PHY_CTRL2_FORCE_MDI_MDIX   0x0200

/* I82580 PHY Diagnostics Status */
#define I82580_DSTATUS_CABLE_LENGTH       0x03FC
#define I82580_DSTATUS_CABLE_LENGTH_SHIFT 2
/* Enable flexible speed on link-up */
#define IGP02E1000_PM_D0_LPLU             0x0002 /* For D0a states */
#define IGP02E1000_PM_D3_LPLU             0x0004 /* For all other states */
#define IGP01E1000_PLHR_SS_DOWNGRADE      0x8000
#define IGP01E1000_PSSR_POLARITY_REVERSED 0x0002
#define IGP01E1000_PSSR_MDIX              0x0800
#define IGP01E1000_PSSR_SPEED_MASK        0xC000
#define IGP01E1000_PSSR_SPEED_1000MBPS    0xC000
#define IGP02E1000_PHY_CHANNEL_NUM        4
#define IGP02E1000_PHY_AGC_A              0x11B1
#define IGP02E1000_PHY_AGC_B              0x12B1
#define IGP02E1000_PHY_AGC_C              0x14B1
#define IGP02E1000_PHY_AGC_D              0x18B1
#define IGP02E1000_AGC_LENGTH_SHIFT       9   /* Course - 15:13, Fine - 12:9 */
#define IGP02E1000_AGC_LENGTH_MASK        0x7F
#define IGP02E1000_AGC_RANGE              15

#define E1000_CABLE_LENGTH_UNDEFINED      0xFF

#endif
