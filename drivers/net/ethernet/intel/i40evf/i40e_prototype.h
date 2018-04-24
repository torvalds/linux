/* SPDX-License-Identifier: GPL-2.0 */
/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
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
#include <linux/avf/virtchnl.h>

/* Prototypes for shared code functions that are not in
 * the standard function pointer structures.  These are
 * mostly because they are needed even before the init
 * has happened and will assist in the early SW and FW
 * setup.
 */

/* adminq functions */
i40e_status i40evf_init_adminq(struct i40e_hw *hw);
i40e_status i40evf_shutdown_adminq(struct i40e_hw *hw);
void i40e_adminq_init_ring_data(struct i40e_hw *hw);
i40e_status i40evf_clean_arq_element(struct i40e_hw *hw,
					     struct i40e_arq_event_info *e,
					     u16 *events_pending);
i40e_status i40evf_asq_send_command(struct i40e_hw *hw,
				struct i40e_aq_desc *desc,
				void *buff, /* can be NULL */
				u16  buff_size,
				struct i40e_asq_cmd_details *cmd_details);
bool i40evf_asq_done(struct i40e_hw *hw);

/* debug function for adminq */
void i40evf_debug_aq(struct i40e_hw *hw, enum i40e_debug_mask mask,
		     void *desc, void *buffer, u16 buf_len);

void i40e_idle_aq(struct i40e_hw *hw);
void i40evf_resume_aq(struct i40e_hw *hw);
bool i40evf_check_asq_alive(struct i40e_hw *hw);
i40e_status i40evf_aq_queue_shutdown(struct i40e_hw *hw, bool unloading);
const char *i40evf_aq_str(struct i40e_hw *hw, enum i40e_admin_queue_err aq_err);
const char *i40evf_stat_str(struct i40e_hw *hw, i40e_status stat_err);

i40e_status i40evf_aq_get_rss_lut(struct i40e_hw *hw, u16 seid,
				  bool pf_lut, u8 *lut, u16 lut_size);
i40e_status i40evf_aq_set_rss_lut(struct i40e_hw *hw, u16 seid,
				  bool pf_lut, u8 *lut, u16 lut_size);
i40e_status i40evf_aq_get_rss_key(struct i40e_hw *hw,
				  u16 seid,
				  struct i40e_aqc_get_set_rss_key_data *key);
i40e_status i40evf_aq_set_rss_key(struct i40e_hw *hw,
				  u16 seid,
				  struct i40e_aqc_get_set_rss_key_data *key);

i40e_status i40e_set_mac_type(struct i40e_hw *hw);

extern struct i40e_rx_ptype_decoded i40evf_ptype_lookup[];

static inline struct i40e_rx_ptype_decoded decode_rx_desc_ptype(u8 ptype)
{
	return i40evf_ptype_lookup[ptype];
}

/* prototype for functions used for SW locks */

/* i40e_common for VF drivers*/
void i40e_vf_parse_hw_config(struct i40e_hw *hw,
			     struct virtchnl_vf_resource *msg);
i40e_status i40e_vf_reset(struct i40e_hw *hw);
i40e_status i40e_aq_send_msg_to_pf(struct i40e_hw *hw,
				enum virtchnl_ops v_opcode,
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
void i40e_add_filter_to_drop_tx_flow_control_frames(struct i40e_hw *hw,
						    u16 vsi_seid);
i40e_status i40evf_aq_rx_ctl_read_register(struct i40e_hw *hw,
				u32 reg_addr, u32 *reg_val,
				struct i40e_asq_cmd_details *cmd_details);
u32 i40evf_read_rx_ctl(struct i40e_hw *hw, u32 reg_addr);
i40e_status i40evf_aq_rx_ctl_write_register(struct i40e_hw *hw,
				u32 reg_addr, u32 reg_val,
				struct i40e_asq_cmd_details *cmd_details);
void i40evf_write_rx_ctl(struct i40e_hw *hw, u32 reg_addr, u32 reg_val);
i40e_status i40e_aq_set_phy_register(struct i40e_hw *hw,
				     u8 phy_select, u8 dev_addr,
				     u32 reg_addr, u32 reg_val,
				     struct i40e_asq_cmd_details *cmd_details);
i40e_status i40e_aq_get_phy_register(struct i40e_hw *hw,
				     u8 phy_select, u8 dev_addr,
				     u32 reg_addr, u32 *reg_val,
				     struct i40e_asq_cmd_details *cmd_details);

i40e_status i40e_read_phy_register(struct i40e_hw *hw, u8 page,
				   u16 reg, u8 phy_addr, u16 *value);
i40e_status i40e_write_phy_register(struct i40e_hw *hw, u8 page,
				    u16 reg, u8 phy_addr, u16 value);
i40e_status i40e_read_phy_register(struct i40e_hw *hw, u8 page, u16 reg,
				   u8 phy_addr, u16 *value);
i40e_status i40e_write_phy_register(struct i40e_hw *hw, u8 page, u16 reg,
				    u8 phy_addr, u16 value);
u8 i40e_get_phy_address(struct i40e_hw *hw, u8 dev_num);
i40e_status i40e_blink_phy_link_led(struct i40e_hw *hw,
				    u32 time, u32 interval);
i40e_status i40evf_aq_write_ddp(struct i40e_hw *hw, void *buff,
				u16 buff_size, u32 track_id,
				u32 *error_offset, u32 *error_info,
				struct i40e_asq_cmd_details *
				cmd_details);
i40e_status i40evf_aq_get_ddp_list(struct i40e_hw *hw, void *buff,
				   u16 buff_size, u8 flags,
				   struct i40e_asq_cmd_details *
				   cmd_details);
struct i40e_generic_seg_header *
i40evf_find_segment_in_package(u32 segment_type,
			       struct i40e_package_header *pkg_header);
enum i40e_status_code
i40evf_write_profile(struct i40e_hw *hw, struct i40e_profile_segment *i40e_seg,
		     u32 track_id);
enum i40e_status_code
i40evf_add_pinfo_to_list(struct i40e_hw *hw,
			 struct i40e_profile_segment *profile,
			 u8 *profile_info_sec, u32 track_id);
#endif /* _I40E_PROTOTYPE_H_ */
