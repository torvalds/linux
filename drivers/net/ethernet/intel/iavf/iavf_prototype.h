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
enum iavf_status iavf_init_adminq(struct iavf_hw *hw);
enum iavf_status iavf_shutdown_adminq(struct iavf_hw *hw);
void iavf_adminq_init_ring_data(struct iavf_hw *hw);
enum iavf_status iavf_clean_arq_element(struct iavf_hw *hw,
					struct iavf_arq_event_info *e,
					u16 *events_pending);
enum iavf_status iavf_asq_send_command(struct iavf_hw *hw,
				       struct iavf_aq_desc *desc,
				       void *buff, /* can be NULL */
				       u16 buff_size,
				       struct iavf_asq_cmd_details *cmd_details);
bool iavf_asq_done(struct iavf_hw *hw);

/* debug function for adminq */
void iavf_debug_aq(struct iavf_hw *hw, enum iavf_debug_mask mask,
		   void *desc, void *buffer, u16 buf_len);

void iavf_idle_aq(struct iavf_hw *hw);
void iavf_resume_aq(struct iavf_hw *hw);
bool iavf_check_asq_alive(struct iavf_hw *hw);
enum iavf_status iavf_aq_queue_shutdown(struct iavf_hw *hw, bool unloading);
const char *iavf_aq_str(struct iavf_hw *hw, enum iavf_admin_queue_err aq_err);
const char *iavf_stat_str(struct iavf_hw *hw, enum iavf_status stat_err);

enum iavf_status iavf_aq_set_rss_lut(struct iavf_hw *hw, u16 seid,
				     bool pf_lut, u8 *lut, u16 lut_size);
enum iavf_status iavf_aq_set_rss_key(struct iavf_hw *hw, u16 seid,
				     struct iavf_aqc_get_set_rss_key_data *key);

void iavf_vf_parse_hw_config(struct iavf_hw *hw,
			     struct virtchnl_vf_resource *msg);
enum iavf_status iavf_aq_send_msg_to_pf(struct iavf_hw *hw,
					enum virtchnl_ops v_opcode,
					enum iavf_status v_retval,
					u8 *msg, u16 msglen,
					struct iavf_asq_cmd_details *cmd_details);
#endif /* _IAVF_PROTOTYPE_H_ */
