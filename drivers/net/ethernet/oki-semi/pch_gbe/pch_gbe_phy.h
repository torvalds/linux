/*
 * Copyright (C) 1999 - 2010 Intel Corporation.
 * Copyright (C) 2010 OKI SEMICONDUCTOR Co., LTD.
 *
 * This code was derived from the Intel e1000e Linux driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */
#ifndef _PCH_GBE_PHY_H_
#define _PCH_GBE_PHY_H_

#define PCH_GBE_PHY_REGS_LEN		32
#define	PCH_GBE_PHY_RESET_DELAY_US	10
#define PCH_GBE_MAC_IFOP_RGMII

s32 pch_gbe_phy_get_id(struct pch_gbe_hw *hw);
s32 pch_gbe_phy_read_reg_miic(struct pch_gbe_hw *hw, u32 offset, u16 *data);
s32 pch_gbe_phy_write_reg_miic(struct pch_gbe_hw *hw, u32 offset, u16 data);
void pch_gbe_phy_sw_reset(struct pch_gbe_hw *hw);
void pch_gbe_phy_hw_reset(struct pch_gbe_hw *hw);
void pch_gbe_phy_power_up(struct pch_gbe_hw *hw);
void pch_gbe_phy_power_down(struct pch_gbe_hw *hw);
void pch_gbe_phy_set_rgmii(struct pch_gbe_hw *hw);
void pch_gbe_phy_init_setting(struct pch_gbe_hw *hw);
int pch_gbe_phy_disable_hibernate(struct pch_gbe_hw *hw);

#endif /* _PCH_GBE_PHY_H_ */
