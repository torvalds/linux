/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifndef _I40E_PROTOTYPE_H_
#define _I40E_PROTOTYPE_H_

#include "i40e_type.h"
#include "i40e_alloc.h"
#include "i40e_virtchnl.h"

/* Prototypes for shared code functions that are not in
 * the standard function pointer structures.  These are
 * mostly because they are needed even before the init
 * has happened and will assist in the early SW and FW
 * setup.
 */

/* adminq functions */
i40e_status i40e_init_adminq(struct i40e_hw *hw);
i40e_status i40e_shutdown_adminq(struct i40e_hw *hw);
void i40e_adminq_init_ring_data(struct i40e_hw *hw);
i40e_status i40e_clean_arq_element(struct i40e_hw *hw,
					     struct i40e_arq_event_info *e,
					     u16 *events_pending);
i40e_status i40e_asq_send_command(struct i40e_hw *hw,
				struct i40e_aq_desc *desc,
				void *buff, /* can be NULL */
				u16  buff_size,
				struct i40e_asq_cmd_details *cmd_details);

/* debug function for adminq */
void i40e_debug_aq(struct i40e_hw *hw, enum i40e_debug_mask mask,
		   void *desc, void *buffer, u16 buf_len);

void i40e_idle_aq(struct i40e_hw *hw);
bool i40e_check_asq_alive(struct i40e_hw *hw);
i40e_status i40e_aq_queue_shutdown(struct i40e_hw *hw, bool unloading);
const char *i40e_aq_str(struct i40e_hw *hw, enum i40e_admin_queue_err aq_err);
const char *i40e_stat_str(struct i40e_hw *hw, i40e_status stat_err);

i40e_status i40e_aq_get_rss_lut(struct i40e_hw *hw, u16 seid,
				bool pf_lut, u8 *lut, u16 lut_size);
i40e_status i40e_aq_set_rss_lut(struct i40e_hw *hw, u16 seid,
				bool pf_lut, u8 *lut, u16 lut_size);
i40e_status i40e_aq_get_rss_key(struct i40e_hw *hw,
				u16 seid,
				struct i40e_aqc_get_set_rss_key_data *key);
i40e_status i40e_aq_set_rss_key(struct i40e_hw *hw,
				u16 seid,
				struct i40e_aqc_get_set_rss_key_data *key);

u32 i40e_led_get(struct i40e_hw *hw);
void i40e_led_set(struct i40e_hw *hw, u32 mode, bool blink);
i40e_status i40e_led_set_phy(struct i40e_hw *hw, bool on,
			     u16 led_addr, u32 mode);
i40e_status i40e_led_get_phy(struct i40e_hw *hw, u16 *led_addr,
			     u16 *val);
i40e_status i40e_blink_phy_link_led(struct i40e_hw *hw,
				    u32 time, u32 interval);

/* admin send queue commands */

i40e_status i40e_aq_get_firmware_version(struct i40e_hw *hw,
				u16 *fw_major_version, u16 *fw_minor_version,
				u32 *fw_build,
				u16 *api_major_version, u16 *api_minor_version,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_debug_write_register(struct i40e_hw *hw,
					u32 reg_addr, u64 reg_val,
					struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_debug_read_register(struct i40e_hw *hw,
				u32  reg_addr, u64 *reg_val,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_set_phy_debug(struct i40e_hw *hw, u8 cmd_flags,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_set_default_vsi(struct i40e_hw *hw, u16 vsi_id,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_clear_default_vsi(struct i40e_hw *hw, u16 vsi_id,
				      struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_phy_capabilities(struct i40e_hw *hw,
			bool qualified_modules, bool report_init,
			struct i40e_aq_get_phy_abilities_resp *abilities,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_phy_config(struct i40e_hw *hw,
				struct i40e_aq_set_phy_config *config,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_set_fc(struct i40e_hw *hw, u8 *aq_failures,
				  bool atomic_reset);
i40e_status i40e_aq_set_phy_int_mask(struct i40e_hw *hw, u16 mask,
				     struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_clear_pxe_mode(struct i40e_hw *hw,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_set_link_restart_an(struct i40e_hw *hw,
					bool enable_link,
					struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_get_link_info(struct i40e_hw *hw,
				bool enable_lse, struct i40e_link_status *link,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_set_local_advt_reg(struct i40e_hw *hw,
				u64 advt_reg,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_send_driver_version(struct i40e_hw *hw,
				struct i40e_driver_version *dv,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_add_vsi(struct i40e_hw *hw,
				struct i40e_vsi_context *vsi_ctx,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_set_vsi_broadcast(struct i40e_hw *hw,
				u16 vsi_id, bool set_filter,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_set_vsi_unicast_promiscuous(struct i40e_hw *hw,
		u16 vsi_id, bool set, struct i40e_asq_cmd_details *cmd_details,
		bool rx_only_promisc);
i40e_status i40e_aq_set_vsi_multicast_promiscuous(struct i40e_hw *hw,
		u16 vsi_id, bool set, struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_vsi_mc_promisc_on_vlan(struct i40e_hw *hw,
							 u16 seid, bool enable,
							 u16 vid,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_vsi_uc_promisc_on_vlan(struct i40e_hw *hw,
							 u16 seid, bool enable,
							 u16 vid,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_set_vsi_vlan_promisc(struct i40e_hw *hw,
				u16 seid, bool enable,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_get_vsi_params(struct i40e_hw *hw,
				struct i40e_vsi_context *vsi_ctx,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_update_vsi_params(struct i40e_hw *hw,
				struct i40e_vsi_context *vsi_ctx,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_add_veb(struct i40e_hw *hw, u16 uplink_seid,
				u16 downlink_seid, u8 enabled_tc,
				bool default_port, u16 *pveb_seid,
				bool enable_stats,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_get_veb_parameters(struct i40e_hw *hw,
				u16 veb_seid, u16 *switch_id, bool *floating,
				u16 *statistic_index, u16 *vebs_used,
				u16 *vebs_free,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_add_macvlan(struct i40e_hw *hw, u16 vsi_id,
			struct i40e_aqc_add_macvlan_element_data *mv_list,
			u16 count, struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_remove_macvlan(struct i40e_hw *hw, u16 vsi_id,
			struct i40e_aqc_remove_macvlan_element_data *mv_list,
			u16 count, struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_add_mirrorrule(struct i40e_hw *hw, u16 sw_seid,
			u16 rule_type, u16 dest_vsi, u16 count, __le16 *mr_list,
			struct i40e_asq_cmd_details *cmd_details,
			u16 *rule_id, u16 *rules_used, u16 *rules_free);
i40e_status i40e_aq_delete_mirrorrule(struct i40e_hw *hw, u16 sw_seid,
			u16 rule_type, u16 rule_id, u16 count, __le16 *mr_list,
			struct i40e_asq_cmd_details *cmd_details,
			u16 *rules_used, u16 *rules_free);

i40e_status i40e_aq_send_msg_to_vf(struct i40e_hw *hw, u16 vfid,
				u32 v_opcode, u32 v_retval, u8 *msg, u16 msglen,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_get_switch_config(struct i40e_hw *hw,
				struct i40e_aqc_get_switch_config_resp *buf,
				u16 buf_size, u16 *start_seid,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_switch_config(struct i40e_hw *hw,
						u16 flags,
						u16 valid_flags,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_request_resource(struct i40e_hw *hw,
				enum i40e_aq_resources_ids resource,
				enum i40e_aq_resource_access_type access,
				u8 sdp_number, u64 *timeout,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_release_resource(struct i40e_hw *hw,
				enum i40e_aq_resources_ids resource,
				u8 sdp_number,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_read_nvm(struct i40e_hw *hw, u8 module_pointer,
				u32 offset, u16 length, void *data,
				bool last_command,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_erase_nvm(struct i40e_hw *hw, u8 module_pointer,
			      u32 offset, u16 length, bool last_command,
			      struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_discover_capabilities(struct i40e_hw *hw,
				void *buff, u16 buff_size, u16 *data_size,
				enum i40e_admin_queue_opc list_type_opc,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_update_nvm(struct i40e_hw *hw, u8 module_pointer,
				u32 offset, u16 length, void *data,
				bool last_command,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_get_lldp_mib(struct i40e_hw *hw, u8 bridge_type,
				u8 mib_type, void *buff, u16 buff_size,
				u16 *local_len, u16 *remote_len,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_cfg_lldp_mib_change_event(struct i40e_hw *hw,
				bool enable_update,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_stop_lldp(struct i40e_hw *hw, bool shutdown_agent,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_start_lldp(struct i40e_hw *hw,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_get_cee_dcb_config(struct i40e_hw *hw,
				       void *buff, u16 buff_size,
				       struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_add_udp_tunnel(struct i40e_hw *hw,
				u16 udp_port, u8 protocol_index,
				u8 *filter_index,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_del_udp_tunnel(struct i40e_hw *hw, u8 index,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_delete_element(struct i40e_hw *hw, u16 seid,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_mac_address_write(struct i40e_hw *hw,
				    u16 flags, u8 *mac_addr,
				    struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_config_vsi_bw_limit(struct i40e_hw *hw,
				u16 seid, u16 credit, u8 max_credit,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_dcb_updated(struct i40e_hw *hw,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_config_switch_comp_bw_limit(struct i40e_hw *hw,
				u16 seid, u16 credit, u8 max_bw,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_config_vsi_tc_bw(struct i40e_hw *hw, u16 seid,
			struct i40e_aqc_configure_vsi_tc_bw_data *bw_data,
			struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_config_switch_comp_ets(struct i40e_hw *hw,
		u16 seid,
		struct i40e_aqc_configure_switching_comp_ets_data *ets_data,
		enum i40e_admin_queue_opc opcode,
		struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_config_switch_comp_bw_config(struct i40e_hw *hw,
	u16 seid,
	struct i40e_aqc_configure_switching_comp_bw_config_data *bw_data,
	struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_query_vsi_bw_config(struct i40e_hw *hw,
			u16 seid,
			struct i40e_aqc_query_vsi_bw_config_resp *bw_data,
			struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_query_vsi_ets_sla_config(struct i40e_hw *hw,
			u16 seid,
			struct i40e_aqc_query_vsi_ets_sla_config_resp *bw_data,
			struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_query_switch_comp_ets_config(struct i40e_hw *hw,
		u16 seid,
		struct i40e_aqc_query_switching_comp_ets_config_resp *bw_data,
		struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_query_port_ets_config(struct i40e_hw *hw,
		u16 seid,
		struct i40e_aqc_query_port_ets_config_resp *bw_data,
		struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_query_switch_comp_bw_config(struct i40e_hw *hw,
		u16 seid,
		struct i40e_aqc_query_switching_comp_bw_config_resp *bw_data,
		struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_resume_port_tx(struct i40e_hw *hw,
				   struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_read_lldp_cfg(struct i40e_hw *hw,
			       struct i40e_lldp_variables *lldp_cfg);
/* i40e_common */
i40e_status i40e_init_shared_code(struct i40e_hw *hw);
i40e_status i40e_pf_reset(struct i40e_hw *hw);
void i40e_clear_hw(struct i40e_hw *hw);
void i40e_clear_pxe_mode(struct i40e_hw *hw);
i40e_status i40e_get_link_status(struct i40e_hw *hw, bool *link_up);
i40e_status i40e_update_link_info(struct i40e_hw *hw);
i40e_status i40e_get_mac_addr(struct i40e_hw *hw, u8 *mac_addr);
i40e_status i40e_read_bw_from_alt_ram(struct i40e_hw *hw,
				      u32 *max_bw, u32 *min_bw, bool *min_valid,
				      bool *max_valid);
i40e_status i40e_aq_configure_partition_bw(struct i40e_hw *hw,
			struct i40e_aqc_configure_partition_bw_data *bw_data,
			struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_get_port_mac_addr(struct i40e_hw *hw, u8 *mac_addr);
i40e_status i40e_read_pba_string(struct i40e_hw *hw, u8 *pba_num,
				 u32 pba_num_size);
i40e_status i40e_validate_mac_addr(u8 *mac_addr);
void i40e_pre_tx_queue_cfg(struct i40e_hw *hw, u32 queue, bool enable);
#ifdef I40E_FCOE
i40e_status i40e_get_san_mac_addr(struct i40e_hw *hw, u8 *mac_addr);
#endif
/* prototype for functions used for NVM access */
i40e_status i40e_init_nvm(struct i40e_hw *hw);
i40e_status i40e_acquire_nvm(struct i40e_hw *hw,
				      enum i40e_aq_resource_access_type access);
void i40e_release_nvm(struct i40e_hw *hw);
i40e_status i40e_read_nvm_word(struct i40e_hw *hw, u16 offset,
					 u16 *data);
i40e_status i40e_read_nvm_buffer(struct i40e_hw *hw, u16 offset,
					   u16 *words, u16 *data);
i40e_status i40e_update_nvm_checksum(struct i40e_hw *hw);
i40e_status i40e_validate_nvm_checksum(struct i40e_hw *hw,
						 u16 *checksum);
i40e_status i40e_nvmupd_command(struct i40e_hw *hw,
				struct i40e_nvm_access *cmd,
				u8 *bytes, int *);
void i40e_nvmupd_check_wait_event(struct i40e_hw *hw, u16 opcode);
void i40e_set_pci_config_data(struct i40e_hw *hw, u16 link_status);

extern struct i40e_rx_ptype_decoded i40e_ptype_lookup[];

static inline struct i40e_rx_ptype_decoded decode_rx_desc_ptype(u8 ptype)
{
	return i40e_ptype_lookup[ptype];
}

/* prototype for functions used for SW locks */

/* i40e_common for VF drivers*/
void i40e_vf_parse_hw_config(struct i40e_hw *hw,
			     struct i40e_virtchnl_vf_resource *msg);
i40e_status i40e_vf_reset(struct i40e_hw *hw);
i40e_status i40e_aq_send_msg_to_pf(struct i40e_hw *hw,
				enum i40e_virtchnl_ops v_opcode,
				i40e_status v_retval,
				u8 *msg, u16 msglen,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_set_filter_control(struct i40e_hw *hw,
				struct i40e_filter_control_settings *settings);
i40e_status i40e_aq_add_rem_control_packet_filter(struct i40e_hw *hw,
				u8 *mac_addr, u16 ethtype, u16 flags,
				u16 vsi_seid, u16 queue, bool is_add,
				struct i40e_control_filter_stats *stats,
				struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_debug_dump(struct i40e_hw *hw, u8 cluster_id,
			       u8 table_id, u32 start_index, u16 buff_size,
			       void *buff, u16 *ret_buff_size,
			       u8 *ret_next_table, u32 *ret_next_index,
			       struct i40e_asq_cmd_details *cmd_details);
void i40e_add_filter_to_drop_tx_flow_control_frames(struct i40e_hw *hw,
						    u16 vsi_seid);
i40e_status i40e_aq_rx_ctl_read_register(struct i40e_hw *hw,
				u32 reg_addr, u32 *reg_val,
				struct i40e_asq_cmd_details *cmd_details);
u32 i40e_read_rx_ctl(struct i40e_hw *hw, u32 reg_addr);
i40e_status i40e_aq_rx_ctl_write_register(struct i40e_hw *hw,
				u32 reg_addr, u32 reg_val,
				struct i40e_asq_cmd_details *cmd_details);
void i40e_write_rx_ctl(struct i40e_hw *hw, u32 reg_addr, u32 reg_val);
i40e_status i40e_read_phy_register_clause22(struct i40e_hw *hw,
					    u16 reg, u8 phy_addr, u16 *value);
i40e_status i40e_write_phy_register_clause22(struct i40e_hw *hw,
					     u16 reg, u8 phy_addr, u16 value);
i40e_status i40e_read_phy_register_clause45(struct i40e_hw *hw,
				u8 page, u16 reg, u8 phy_addr, u16 *value);
i40e_status i40e_write_phy_register_clause45(struct i40e_hw *hw,
				u8 page, u16 reg, u8 phy_addr, u16 value);
u8 i40e_get_phy_address(struct i40e_hw *hw, u8 dev_num);
i40e_status i40e_blink_phy_link_led(struct i40e_hw *hw,
				    u32 time, u32 interval);
#endif /* _I40E_PROTOTYPE_H_ */
