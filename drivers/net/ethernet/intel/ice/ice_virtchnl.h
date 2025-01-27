/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022, Intel Corporation. */

#ifndef _ICE_VIRTCHNL_H_
#define _ICE_VIRTCHNL_H_

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/if_ether.h>
#include <linux/avf/virtchnl.h>
#include "ice_vf_lib.h"

/* Restrict number of MAC Addr and VLAN that non-trusted VF can programmed */
#define ICE_MAX_VLAN_PER_VF		8

#define ICE_DFLT_QUANTA 1024
#define ICE_MAX_QUANTA_SIZE 4096
#define ICE_MIN_QUANTA_SIZE 256

#define calc_quanta_desc(x)	\
	max_t(u16, 12, min_t(u16, 63, (((x) + 66) / 132) * 2 + 4))

/* MAC filters: 1 is reserved for the VF's default/perm_addr/LAA MAC, 1 for
 * broadcast, and 16 for additional unicast/multicast filters
 */
#define ICE_MAX_MACADDR_PER_VF		18
#define ICE_FLEX_DESC_RXDID_MAX_NUM	64

/* VFs only get a single VSI. For ice hardware, the VF does not need to know
 * its VSI index. However, the virtchnl interface requires a VSI number,
 * mainly due to legacy hardware.
 *
 * Since the VF doesn't need this information, report a static value to the VF
 * instead of leaking any information about the PF or hardware setup.
 */
#define ICE_VF_VSI_ID	1

struct ice_virtchnl_ops {
	int (*get_ver_msg)(struct ice_vf *vf, u8 *msg);
	int (*get_vf_res_msg)(struct ice_vf *vf, u8 *msg);
	void (*reset_vf)(struct ice_vf *vf);
	int (*add_mac_addr_msg)(struct ice_vf *vf, u8 *msg);
	int (*del_mac_addr_msg)(struct ice_vf *vf, u8 *msg);
	int (*cfg_qs_msg)(struct ice_vf *vf, u8 *msg);
	int (*ena_qs_msg)(struct ice_vf *vf, u8 *msg);
	int (*dis_qs_msg)(struct ice_vf *vf, u8 *msg);
	int (*request_qs_msg)(struct ice_vf *vf, u8 *msg);
	int (*cfg_irq_map_msg)(struct ice_vf *vf, u8 *msg);
	int (*config_rss_key)(struct ice_vf *vf, u8 *msg);
	int (*config_rss_lut)(struct ice_vf *vf, u8 *msg);
	int (*config_rss_hfunc)(struct ice_vf *vf, u8 *msg);
	int (*get_stats_msg)(struct ice_vf *vf, u8 *msg);
	int (*cfg_promiscuous_mode_msg)(struct ice_vf *vf, u8 *msg);
	int (*add_vlan_msg)(struct ice_vf *vf, u8 *msg);
	int (*remove_vlan_msg)(struct ice_vf *vf, u8 *msg);
	int (*query_rxdid)(struct ice_vf *vf);
	int (*get_rss_hena)(struct ice_vf *vf);
	int (*set_rss_hena_msg)(struct ice_vf *vf, u8 *msg);
	int (*ena_vlan_stripping)(struct ice_vf *vf);
	int (*dis_vlan_stripping)(struct ice_vf *vf);
	int (*handle_rss_cfg_msg)(struct ice_vf *vf, u8 *msg, bool add);
	int (*add_fdir_fltr_msg)(struct ice_vf *vf, u8 *msg);
	int (*del_fdir_fltr_msg)(struct ice_vf *vf, u8 *msg);
	int (*get_offload_vlan_v2_caps)(struct ice_vf *vf);
	int (*add_vlan_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*remove_vlan_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*ena_vlan_stripping_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*dis_vlan_stripping_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*ena_vlan_insertion_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*dis_vlan_insertion_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*get_qos_caps)(struct ice_vf *vf);
	int (*cfg_q_tc_map)(struct ice_vf *vf, u8 *msg);
	int (*cfg_q_bw)(struct ice_vf *vf, u8 *msg);
	int (*cfg_q_quanta)(struct ice_vf *vf, u8 *msg);
};

#ifdef CONFIG_PCI_IOV
void ice_virtchnl_set_dflt_ops(struct ice_vf *vf);
void ice_virtchnl_set_repr_ops(struct ice_vf *vf);
void ice_vc_notify_vf_link_state(struct ice_vf *vf);
void ice_vc_notify_link_state(struct ice_pf *pf);
void ice_vc_notify_reset(struct ice_pf *pf);
int
ice_vc_send_msg_to_vf(struct ice_vf *vf, u32 v_opcode,
		      enum virtchnl_status_code v_retval, u8 *msg, u16 msglen);
bool ice_vc_isvalid_vsi_id(struct ice_vf *vf, u16 vsi_id);
void ice_vc_process_vf_msg(struct ice_pf *pf, struct ice_rq_event_info *event,
			   struct ice_mbx_data *mbxdata);
#else /* CONFIG_PCI_IOV */
static inline void ice_virtchnl_set_dflt_ops(struct ice_vf *vf) { }
static inline void ice_virtchnl_set_repr_ops(struct ice_vf *vf) { }
static inline void ice_vc_notify_vf_link_state(struct ice_vf *vf) { }
static inline void ice_vc_notify_link_state(struct ice_pf *pf) { }
static inline void ice_vc_notify_reset(struct ice_pf *pf) { }

static inline int
ice_vc_send_msg_to_vf(struct ice_vf *vf, u32 v_opcode,
		      enum virtchnl_status_code v_retval, u8 *msg, u16 msglen)
{
	return -EOPNOTSUPP;
}

static inline bool ice_vc_isvalid_vsi_id(struct ice_vf *vf, u16 vsi_id)
{
	return false;
}

static inline void
ice_vc_process_vf_msg(struct ice_pf *pf, struct ice_rq_event_info *event,
		      struct ice_mbx_data *mbxdata)
{
}
#endif /* !CONFIG_PCI_IOV */

#endif /* _ICE_VIRTCHNL_H_ */
