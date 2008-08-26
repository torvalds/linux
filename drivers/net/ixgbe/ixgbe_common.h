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

#ifndef _IXGBE_COMMON_H_
#define _IXGBE_COMMON_H_

#include "ixgbe_type.h"

s32 ixgbe_init_hw(struct ixgbe_hw *hw);
s32 ixgbe_start_hw(struct ixgbe_hw *hw);
s32 ixgbe_get_mac_addr(struct ixgbe_hw *hw, u8 *mac_addr);
s32 ixgbe_stop_adapter(struct ixgbe_hw *hw);
s32 ixgbe_read_part_num(struct ixgbe_hw *hw, u32 *part_num);

s32 ixgbe_led_on(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_led_off(struct ixgbe_hw *hw, u32 index);

s32 ixgbe_init_eeprom(struct ixgbe_hw *hw);
s32 ixgbe_read_eeprom(struct ixgbe_hw *hw, u16 offset, u16 *data);
s32 ixgbe_validate_eeprom_checksum(struct ixgbe_hw *hw, u16 *checksum_val);

s32 ixgbe_set_rar(struct ixgbe_hw *hw, u32 index, u8 *addr, u32 vind,
		  u32 enable_addr);
s32 ixgbe_update_mc_addr_list(struct ixgbe_hw *hw, u8 *mc_addr_list,
			      u32 mc_addr_count, ixgbe_mc_addr_itr next);
s32 ixgbe_update_uc_addr_list(struct ixgbe_hw *hw, u8 *uc_addr_list,
			      u32 mc_addr_count, ixgbe_mc_addr_itr next);
s32 ixgbe_set_vfta(struct ixgbe_hw *hw, u32 vlan, u32 vind, bool vlan_on);
s32 ixgbe_validate_mac_addr(u8 *mac_addr);

s32 ixgbe_setup_fc(struct ixgbe_hw *hw, s32 packtetbuf_num);

s32 ixgbe_acquire_swfw_sync(struct ixgbe_hw *hw, u16 mask);
void ixgbe_release_swfw_sync(struct ixgbe_hw *hw, u16 mask);
s32 ixgbe_disable_pcie_master(struct ixgbe_hw *hw);

s32 ixgbe_read_analog_reg8(struct ixgbe_hw *hw, u32 reg, u8 *val);
s32 ixgbe_write_analog_reg8(struct ixgbe_hw *hw, u32 reg, u8 val);

#define IXGBE_WRITE_REG(a, reg, value) writel((value), ((a)->hw_addr + (reg)))

#define IXGBE_READ_REG(a, reg) readl((a)->hw_addr + (reg))

#define IXGBE_WRITE_REG_ARRAY(a, reg, offset, value) (\
    writel((value), ((a)->hw_addr + (reg) + ((offset) << 2))))

#define IXGBE_READ_REG_ARRAY(a, reg, offset) (\
    readl((a)->hw_addr + (reg) + ((offset) << 2)))

#define IXGBE_WRITE_FLUSH(a) IXGBE_READ_REG(a, IXGBE_STATUS)

#ifdef DEBUG
#define hw_dbg(hw, format, arg...) \
printk(KERN_DEBUG, "%s: " format, ixgbe_get_hw_dev_name(hw), ##arg);
#else
static inline int __attribute__ ((format (printf, 2, 3)))
hw_dbg(struct ixgbe_hw *hw, const char *format, ...)
{
	return 0;
}
#endif

#endif /* IXGBE_COMMON */
