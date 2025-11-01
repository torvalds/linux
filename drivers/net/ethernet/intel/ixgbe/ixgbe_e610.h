/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2024 Intel Corporation. */

#ifndef _IXGBE_E610_H_
#define _IXGBE_E610_H_

#include "ixgbe_type.h"

int ixgbe_aci_send_cmd(struct ixgbe_hw *hw, struct libie_aq_desc *desc,
		       void *buf, u16 buf_size);
bool ixgbe_aci_check_event_pending(struct ixgbe_hw *hw);
int ixgbe_aci_get_event(struct ixgbe_hw *hw, struct ixgbe_aci_event *e,
			bool *pending);
void ixgbe_fill_dflt_direct_cmd_desc(struct libie_aq_desc *desc, u16 opcode);
int ixgbe_acquire_res(struct ixgbe_hw *hw, enum libie_aq_res_id res,
		      enum libie_aq_res_access_type access, u32 timeout);
void ixgbe_release_res(struct ixgbe_hw *hw, enum libie_aq_res_id res);
int ixgbe_aci_list_caps(struct ixgbe_hw *hw, void *buf, u16 buf_size,
			u32 *cap_count, enum ixgbe_aci_opc opc);
int ixgbe_discover_dev_caps(struct ixgbe_hw *hw,
			    struct ixgbe_hw_dev_caps *dev_caps);
int ixgbe_discover_func_caps(struct ixgbe_hw *hw,
			     struct ixgbe_hw_func_caps *func_caps);
int ixgbe_get_caps(struct ixgbe_hw *hw);
int ixgbe_aci_disable_rxen(struct ixgbe_hw *hw);
int ixgbe_aci_get_phy_caps(struct ixgbe_hw *hw, bool qual_mods, u8 report_mode,
			   struct ixgbe_aci_cmd_get_phy_caps_data *pcaps);
void ixgbe_copy_phy_caps_to_cfg(struct ixgbe_aci_cmd_get_phy_caps_data *caps,
				struct ixgbe_aci_cmd_set_phy_cfg_data *cfg);
int ixgbe_aci_set_phy_cfg(struct ixgbe_hw *hw,
			  struct ixgbe_aci_cmd_set_phy_cfg_data *cfg);
int ixgbe_aci_set_link_restart_an(struct ixgbe_hw *hw, bool ena_link);
int ixgbe_update_link_info(struct ixgbe_hw *hw);
int ixgbe_get_link_status(struct ixgbe_hw *hw, bool *link_up);
int ixgbe_aci_get_link_info(struct ixgbe_hw *hw, bool ena_lse,
			    struct ixgbe_link_status *link);
int ixgbe_aci_set_event_mask(struct ixgbe_hw *hw, u8 port_num, u16 mask);
int ixgbe_configure_lse(struct ixgbe_hw *hw, bool activate, u16 mask);
int ixgbe_aci_set_port_id_led(struct ixgbe_hw *hw, bool orig_mode);
enum ixgbe_media_type ixgbe_get_media_type_e610(struct ixgbe_hw *hw);
int ixgbe_setup_link_e610(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			  bool autoneg_wait);
int ixgbe_check_link_e610(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
			  bool *link_up, bool link_up_wait_to_complete);
int ixgbe_get_link_capabilities_e610(struct ixgbe_hw *hw,
				     ixgbe_link_speed *speed,
				     bool *autoneg);
int ixgbe_cfg_phy_fc(struct ixgbe_hw *hw,
		     struct ixgbe_aci_cmd_set_phy_cfg_data *cfg,
		     enum ixgbe_fc_mode req_mode);
int ixgbe_setup_fc_e610(struct ixgbe_hw *hw);
void ixgbe_fc_autoneg_e610(struct ixgbe_hw *hw);
void ixgbe_disable_rx_e610(struct ixgbe_hw *hw);
int ixgbe_init_phy_ops_e610(struct ixgbe_hw *hw);
int ixgbe_identify_phy_e610(struct ixgbe_hw *hw);
int ixgbe_identify_module_e610(struct ixgbe_hw *hw);
int ixgbe_setup_phy_link_e610(struct ixgbe_hw *hw);
int ixgbe_set_phy_power_e610(struct ixgbe_hw *hw, bool on);
int ixgbe_enter_lplu_e610(struct ixgbe_hw *hw);
int ixgbe_init_eeprom_params_e610(struct ixgbe_hw *hw);
int ixgbe_aci_get_netlist_node(struct ixgbe_hw *hw,
			       struct ixgbe_aci_cmd_get_link_topo *cmd,
			       u8 *node_part_number, u16 *node_handle);
int ixgbe_acquire_nvm(struct ixgbe_hw *hw,
		      enum libie_aq_res_access_type access);
void ixgbe_release_nvm(struct ixgbe_hw *hw);
int ixgbe_aci_read_nvm(struct ixgbe_hw *hw, u16 module_typeid, u32 offset,
		       u16 length, void *data, bool last_command,
		       bool read_shadow_ram);
int ixgbe_nvm_validate_checksum(struct ixgbe_hw *hw);
int ixgbe_get_inactive_orom_ver(struct ixgbe_hw *hw,
				struct ixgbe_orom_info *orom);
int ixgbe_get_inactive_nvm_ver(struct ixgbe_hw *hw, struct ixgbe_nvm_info *nvm);
int ixgbe_get_inactive_netlist_ver(struct ixgbe_hw *hw,
				   struct ixgbe_netlist_info *netlist);
int ixgbe_read_sr_word_aci(struct ixgbe_hw  *hw, u16 offset, u16 *data);
int ixgbe_read_flat_nvm(struct ixgbe_hw  *hw, u32 offset, u32 *length,
			u8 *data, bool read_shadow_ram);
int ixgbe_read_sr_buf_aci(struct ixgbe_hw *hw, u16 offset, u16 *words,
			  u16 *data);
int ixgbe_read_ee_aci_e610(struct ixgbe_hw *hw, u16 offset, u16 *data);
int ixgbe_read_ee_aci_buffer_e610(struct ixgbe_hw *hw, u16 offset,
				  u16 words, u16 *data);
int ixgbe_validate_eeprom_checksum_e610(struct ixgbe_hw *hw, u16 *checksum_val);
int ixgbe_reset_hw_e610(struct ixgbe_hw *hw);
int ixgbe_get_flash_data(struct ixgbe_hw *hw);
int ixgbe_aci_nvm_update_empr(struct ixgbe_hw *hw);
int ixgbe_nvm_set_pkg_data(struct ixgbe_hw *hw, bool del_pkg_data_flag,
			   u8 *data, u16 length);
int ixgbe_nvm_pass_component_tbl(struct ixgbe_hw *hw, u8 *data, u16 length,
				 u8 transfer_flag, u8 *comp_response,
				 u8 *comp_response_code);
int ixgbe_aci_erase_nvm(struct ixgbe_hw *hw, u16 module_typeid);
int ixgbe_aci_update_nvm(struct ixgbe_hw *hw, u16 module_typeid,
			 u32 offset, u16 length, void *data,
			 bool last_command, u8 command_flags);
int ixgbe_nvm_write_activate(struct ixgbe_hw *hw, u16 cmd_flags,
			     u8 *response_flags);
int ixgbe_fwlog_init(struct ixgbe_hw *hw);
void ixgbe_fwlog_deinit(struct ixgbe_hw *hw);

#endif /* _IXGBE_E610_H_ */
