/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2007 Intel Corporation.

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

#ifndef _IXGBE_PHY_H_
#define _IXGBE_PHY_H_

#include "ixgbe_type.h"

s32 ixgbe_setup_phy_link(struct ixgbe_hw *hw);
s32 ixgbe_check_phy_link(struct ixgbe_hw *hw, u32 *speed, bool *link_up);
s32 ixgbe_setup_phy_link_speed(struct ixgbe_hw *hw, u32 speed, bool autoneg,
			       bool autoneg_wait_to_complete);
s32 ixgbe_identify_phy(struct ixgbe_hw *hw);
s32 ixgbe_reset_phy(struct ixgbe_hw *hw);
s32 ixgbe_read_phy_reg(struct ixgbe_hw *hw, u32 reg_addr,
			       u32 device_type, u16 *phy_data);

/* PHY specific */
s32 ixgbe_setup_tnx_phy_link(struct ixgbe_hw *hw);
s32 ixgbe_check_tnx_phy_link(struct ixgbe_hw *hw, u32 *speed, bool *link_up);
s32 ixgbe_setup_tnx_phy_link_speed(struct ixgbe_hw *hw, u32 speed, bool autoneg,
				  bool autoneg_wait_to_complete);

#endif /* _IXGBE_PHY_H_ */
