/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _ICE_VIRTCHNL_FDIR_H_
#define _ICE_VIRTCHNL_FDIR_H_

struct ice_vf;
struct ice_pf;
struct ice_vsi;

enum ice_fdir_ctx_stat {
	ICE_FDIR_CTX_READY,
	ICE_FDIR_CTX_IRQ,
	ICE_FDIR_CTX_TIMEOUT,
};

struct ice_vf_fdir_ctx {
	struct timer_list rx_tmr;
	enum virtchnl_ops v_opcode;
	enum ice_fdir_ctx_stat stat;
	union ice_32b_rx_flex_desc rx_desc;
#define ICE_VF_FDIR_CTX_VALID		BIT(0)
	u32 flags;

	void *conf;
};

/* VF FDIR information structure */
struct ice_vf_fdir {
	u16 fdir_fltr_cnt[ICE_FLTR_PTYPE_MAX][ICE_FD_HW_SEG_MAX];
	int prof_entry_cnt[ICE_FLTR_PTYPE_MAX][ICE_FD_HW_SEG_MAX];
	u16 fdir_fltr_cnt_total;
	struct ice_fd_hw_prof **fdir_prof;

	struct idr fdir_rule_idr;
	struct list_head fdir_rule_list;

	spinlock_t ctx_lock; /* protects FDIR context info */
	struct ice_vf_fdir_ctx ctx_irq;
	struct ice_vf_fdir_ctx ctx_done;
};

#ifdef CONFIG_PCI_IOV
int ice_vc_add_fdir_fltr(struct ice_vf *vf, u8 *msg);
int ice_vc_del_fdir_fltr(struct ice_vf *vf, u8 *msg);
void ice_vf_fdir_init(struct ice_vf *vf);
void ice_vf_fdir_exit(struct ice_vf *vf);
void
ice_vc_fdir_irq_handler(struct ice_vsi *ctrl_vsi,
			union ice_32b_rx_flex_desc *rx_desc);
void ice_flush_fdir_ctx(struct ice_pf *pf);
#else
static inline void
ice_vc_fdir_irq_handler(struct ice_vsi *ctrl_vsi, union ice_32b_rx_flex_desc *rx_desc) { }
static inline void ice_flush_fdir_ctx(struct ice_pf *pf) { }
#endif /* CONFIG_PCI_IOV */
#endif /* _ICE_VIRTCHNL_FDIR_H_ */
