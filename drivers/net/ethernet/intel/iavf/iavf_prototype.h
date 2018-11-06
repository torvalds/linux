/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _IAVF_PROTOTYPE_H_
#define _IAVF_PROTOTYPE_H_

#include "iavf_type.h"
#include "iavf_alloc.h"
#include <linux/avf/virtchnl.h>

/* Prototypes for shared code functions that are not in
 * the standard function pointer structures.  These are
 * mostly because they are needed even before the init
 * has happened and will assist in the early SW and FW
 * setup.
 */

/* adminq functions */
iavf_status iavf_init_adminq(struct iavf_hw *hw);
iavf_status iavf_shutdown_adminq(struct iavf_hw *hw);
void i40e_adminq_init_ring_data(struct iavf_hw *hw);
iavf_status iavf_clean_arq_element(struct iavf_hw *hw,
				   struct i40e_arq_event_info *e,
				   u16 *events_pending);
iavf_status iavf_asq_send_command(struct iavf_hw *hw, struct i40e_aq_desc *desc,
				  void *buff, /* can be NULL */
				  u16 buff_size,
				  struct i40e_asq_cmd_details *cmd_details);
bool iavf_asq_done(struct iavf_hw *hw);

/* debug function for adminq */
void iavf_debug_aq(struct iavf_hw *hw, enum iavf_debug_mask mask,
		   void *desc, void *buffer, u16 buf_len);

void i40e_idle_aq(struct iavf_hw *hw);
void iavf_resume_aq(struct iavf_hw *hw);
bool iavf_check_asq_alive(struct iavf_hw *hw);
iavf_status iavf_aq_queue_shutdown(struct iavf_hw *hw, bool unloading);
const char *iavf_aq_str(struct iavf_hw *hw, enum i40e_admin_queue_err aq_err);
const char *iavf_stat_str(struct iavf_hw *hw, iavf_status stat_err);

iavf_status iavf_aq_get_rss_lut(struct iavf_hw *hw, u16 seid,
				bool pf_lut, u8 *lut, u16 lut_size);
iavf_status iavf_aq_set_rss_lut(struct iavf_hw *hw, u16 seid,
				bool pf_lut, u8 *lut, u16 lut_size);
iavf_status iavf_aq_get_rss_key(struct iavf_hw *hw, u16 seid,
				struct i40e_aqc_get_set_rss_key_data *key);
iavf_status iavf_aq_set_rss_key(struct iavf_hw *hw, u16 seid,
				struct i40e_aqc_get_set_rss_key_data *key);

iavf_status iavf_set_mac_type(struct iavf_hw *hw);

extern struct iavf_rx_ptype_decoded iavf_ptype_lookup[];

static inline struct iavf_rx_ptype_decoded decode_rx_desc_ptype(u8 ptype)
{
	return iavf_ptype_lookup[ptype];
}

void iavf_vf_parse_hw_config(struct iavf_hw *hw,
			     struct virtchnl_vf_resource *msg);
iavf_status iavf_vf_reset(struct iavf_hw *hw);
iavf_status iavf_aq_send_msg_to_pf(struct iavf_hw *hw,
				   enum virtchnl_ops v_opcode,
				   iavf_status v_retval, u8 *msg, u16 msglen,
				   struct i40e_asq_cmd_details *cmd_details);
#endif /* _IAVF_PROTOTYPE_H_ */
