/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2014 Intel Corporation.

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
#include "ixgbe.h"

u16 ixgbe_get_pcie_msix_count_generic(struct ixgbe_hw *hw);
s32 ixgbe_init_ops_generic(struct ixgbe_hw *hw);
s32 ixgbe_init_hw_generic(struct ixgbe_hw *hw);
s32 ixgbe_start_hw_generic(struct ixgbe_hw *hw);
s32 ixgbe_start_hw_gen2(struct ixgbe_hw *hw);
s32 ixgbe_clear_hw_cntrs_generic(struct ixgbe_hw *hw);
s32 ixgbe_read_pba_string_generic(struct ixgbe_hw *hw, u8 *pba_num,
				  u32 pba_num_size);
s32 ixgbe_get_mac_addr_generic(struct ixgbe_hw *hw, u8 *mac_addr);
enum ixgbe_bus_width ixgbe_convert_bus_width(u16 link_status);
enum ixgbe_bus_speed ixgbe_convert_bus_speed(u16 link_status);
s32 ixgbe_get_bus_info_generic(struct ixgbe_hw *hw);
void ixgbe_set_lan_id_multi_port_pcie(struct ixgbe_hw *hw);
s32 ixgbe_stop_adapter_generic(struct ixgbe_hw *hw);

s32 ixgbe_led_on_generic(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_led_off_generic(struct ixgbe_hw *hw, u32 index);

s32 ixgbe_init_eeprom_params_generic(struct ixgbe_hw *hw);
s32 ixgbe_write_eeprom_generic(struct ixgbe_hw *hw, u16 offset, u16 data);
s32 ixgbe_write_eeprom_buffer_bit_bang_generic(struct ixgbe_hw *hw, u16 offset,
					       u16 words, u16 *data);
s32 ixgbe_read_eerd_generic(struct ixgbe_hw *hw, u16 offset, u16 *data);
s32 ixgbe_read_eerd_buffer_generic(struct ixgbe_hw *hw, u16 offset,
				   u16 words, u16 *data);
s32 ixgbe_write_eewr_generic(struct ixgbe_hw *hw, u16 offset, u16 data);
s32 ixgbe_write_eewr_buffer_generic(struct ixgbe_hw *hw, u16 offset,
				    u16 words, u16 *data);
s32 ixgbe_read_eeprom_bit_bang_generic(struct ixgbe_hw *hw, u16 offset,
				       u16 *data);
s32 ixgbe_read_eeprom_buffer_bit_bang_generic(struct ixgbe_hw *hw, u16 offset,
					      u16 words, u16 *data);
u16 ixgbe_calc_eeprom_checksum_generic(struct ixgbe_hw *hw);
s32 ixgbe_validate_eeprom_checksum_generic(struct ixgbe_hw *hw,
					   u16 *checksum_val);
s32 ixgbe_update_eeprom_checksum_generic(struct ixgbe_hw *hw);

s32 ixgbe_set_rar_generic(struct ixgbe_hw *hw, u32 index, u8 *addr, u32 vmdq,
			  u32 enable_addr);
s32 ixgbe_clear_rar_generic(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_init_rx_addrs_generic(struct ixgbe_hw *hw);
s32 ixgbe_update_mc_addr_list_generic(struct ixgbe_hw *hw,
				      struct net_device *netdev);
s32 ixgbe_enable_mc_generic(struct ixgbe_hw *hw);
s32 ixgbe_disable_mc_generic(struct ixgbe_hw *hw);
s32 ixgbe_disable_rx_buff_generic(struct ixgbe_hw *hw);
s32 ixgbe_enable_rx_buff_generic(struct ixgbe_hw *hw);
s32 ixgbe_enable_rx_dma_generic(struct ixgbe_hw *hw, u32 regval);
s32 ixgbe_fc_enable_generic(struct ixgbe_hw *hw);
bool ixgbe_device_supports_autoneg_fc(struct ixgbe_hw *hw);
void ixgbe_fc_autoneg(struct ixgbe_hw *hw);

s32 ixgbe_acquire_swfw_sync(struct ixgbe_hw *hw, u32 mask);
void ixgbe_release_swfw_sync(struct ixgbe_hw *hw, u32 mask);
s32 ixgbe_get_san_mac_addr_generic(struct ixgbe_hw *hw, u8 *san_mac_addr);
s32 ixgbe_set_vmdq_generic(struct ixgbe_hw *hw, u32 rar, u32 vmdq);
s32 ixgbe_set_vmdq_san_mac_generic(struct ixgbe_hw *hw, u32 vmdq);
s32 ixgbe_clear_vmdq_generic(struct ixgbe_hw *hw, u32 rar, u32 vmdq);
s32 ixgbe_init_uta_tables_generic(struct ixgbe_hw *hw);
s32 ixgbe_set_vfta_generic(struct ixgbe_hw *hw, u32 vlan,
			   u32 vind, bool vlan_on);
s32 ixgbe_clear_vfta_generic(struct ixgbe_hw *hw);
s32 ixgbe_check_mac_link_generic(struct ixgbe_hw *hw,
				 ixgbe_link_speed *speed,
				 bool *link_up, bool link_up_wait_to_complete);
s32 ixgbe_get_wwn_prefix_generic(struct ixgbe_hw *hw, u16 *wwnn_prefix,
				 u16 *wwpn_prefix);

s32 prot_autoc_read_generic(struct ixgbe_hw *hw, bool *, u32 *reg_val);
s32 prot_autoc_write_generic(struct ixgbe_hw *hw, u32 reg_val, bool locked);

s32 ixgbe_blink_led_start_generic(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_blink_led_stop_generic(struct ixgbe_hw *hw, u32 index);
void ixgbe_set_mac_anti_spoofing(struct ixgbe_hw *hw, bool enable, int pf);
void ixgbe_set_vlan_anti_spoofing(struct ixgbe_hw *hw, bool enable, int vf);
s32 ixgbe_get_device_caps_generic(struct ixgbe_hw *hw, u16 *device_caps);
s32 ixgbe_set_fw_drv_ver_generic(struct ixgbe_hw *hw, u8 maj, u8 min,
				 u8 build, u8 ver);
void ixgbe_clear_tx_pending(struct ixgbe_hw *hw);
bool ixgbe_mng_enabled(struct ixgbe_hw *hw);

void ixgbe_set_rxpba_generic(struct ixgbe_hw *hw, int num_pb,
			     u32 headroom, int strategy);

#define IXGBE_I2C_THERMAL_SENSOR_ADDR	0xF8
#define IXGBE_EMC_INTERNAL_DATA		0x00
#define IXGBE_EMC_INTERNAL_THERM_LIMIT	0x20
#define IXGBE_EMC_DIODE1_DATA		0x01
#define IXGBE_EMC_DIODE1_THERM_LIMIT	0x19
#define IXGBE_EMC_DIODE2_DATA		0x23
#define IXGBE_EMC_DIODE2_THERM_LIMIT	0x1A
#define IXGBE_EMC_DIODE3_DATA		0x2A
#define IXGBE_EMC_DIODE3_THERM_LIMIT	0x30

s32 ixgbe_get_thermal_sensor_data_generic(struct ixgbe_hw *hw);
s32 ixgbe_init_thermal_sensor_thresh_generic(struct ixgbe_hw *hw);

#define IXGBE_FAILED_READ_REG 0xffffffffU
#define IXGBE_FAILED_READ_CFG_DWORD 0xffffffffU
#define IXGBE_FAILED_READ_CFG_WORD 0xffffU

u16 ixgbe_read_pci_cfg_word(struct ixgbe_hw *hw, u32 reg);
void ixgbe_write_pci_cfg_word(struct ixgbe_hw *hw, u32 reg, u16 value);

static inline bool ixgbe_removed(void __iomem *addr)
{
	return unlikely(!addr);
}

static inline void ixgbe_write_reg(struct ixgbe_hw *hw, u32 reg, u32 value)
{
	u8 __iomem *reg_addr = ACCESS_ONCE(hw->hw_addr);

	if (ixgbe_removed(reg_addr))
		return;
	writel(value, reg_addr + reg);
}
#define IXGBE_WRITE_REG(a, reg, value) ixgbe_write_reg((a), (reg), (value))

#ifndef writeq
#define writeq writeq
static inline void writeq(u64 val, void __iomem *addr)
{
	writel((u32)val, addr);
	writel((u32)(val >> 32), addr + 4);
}
#endif

static inline void ixgbe_write_reg64(struct ixgbe_hw *hw, u32 reg, u64 value)
{
	u8 __iomem *reg_addr = ACCESS_ONCE(hw->hw_addr);

	if (ixgbe_removed(reg_addr))
		return;
	writeq(value, reg_addr + reg);
}
#define IXGBE_WRITE_REG64(a, reg, value) ixgbe_write_reg64((a), (reg), (value))

u32 ixgbe_read_reg(struct ixgbe_hw *hw, u32 reg);
#define IXGBE_READ_REG(a, reg) ixgbe_read_reg((a), (reg))

#define IXGBE_WRITE_REG_ARRAY(a, reg, offset, value) \
		ixgbe_write_reg((a), (reg) + ((offset) << 2), (value))

#define IXGBE_READ_REG_ARRAY(a, reg, offset) \
		ixgbe_read_reg((a), (reg) + ((offset) << 2))

#define IXGBE_WRITE_FLUSH(a) ixgbe_read_reg((a), IXGBE_STATUS)

#define ixgbe_hw_to_netdev(hw) (((struct ixgbe_adapter *)(hw)->back)->netdev)

#define hw_dbg(hw, format, arg...) \
	netdev_dbg(ixgbe_hw_to_netdev(hw), format, ## arg)
#define hw_err(hw, format, arg...) \
	netdev_err(ixgbe_hw_to_netdev(hw), format, ## arg)
#define e_dev_info(format, arg...) \
	dev_info(&adapter->pdev->dev, format, ## arg)
#define e_dev_warn(format, arg...) \
	dev_warn(&adapter->pdev->dev, format, ## arg)
#define e_dev_err(format, arg...) \
	dev_err(&adapter->pdev->dev, format, ## arg)
#define e_dev_notice(format, arg...) \
	dev_notice(&adapter->pdev->dev, format, ## arg)
#define e_info(msglvl, format, arg...) \
	netif_info(adapter, msglvl, adapter->netdev, format, ## arg)
#define e_err(msglvl, format, arg...) \
	netif_err(adapter, msglvl, adapter->netdev, format, ## arg)
#define e_warn(msglvl, format, arg...) \
	netif_warn(adapter, msglvl, adapter->netdev, format, ## arg)
#define e_crit(msglvl, format, arg...) \
	netif_crit(adapter, msglvl, adapter->netdev, format, ## arg)
#endif /* IXGBE_COMMON */
