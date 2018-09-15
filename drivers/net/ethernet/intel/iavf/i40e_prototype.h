/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

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

/* i40e_common for VF drivers*/
void i40e_vf_parse_hw_config(struct i40e_hw *hw,
			     struct virtchnl_vf_resource *msg);
i40e_status i40e_vf_reset(struct i40e_hw *hw);
i40e_status i40e_aq_send_msg_to_pf(struct i40e_hw *hw,
				   enum virtchnl_ops v_opcode,
				   i40e_status v_retval, u8 *msg, u16 msglen,
				   struct i40e_asq_cmd_details *cmd_details);
#endif /* _I40E_PROTOTYPE_H_ */
