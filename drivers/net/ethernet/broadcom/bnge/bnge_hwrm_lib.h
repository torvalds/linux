/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_HWRM_LIB_H_
#define _BNGE_HWRM_LIB_H_

#define BNGE_PLC_EN_JUMBO_THRES_VALID		\
	VNIC_PLCMODES_CFG_REQ_ENABLES_JUMBO_THRESH_VALID
#define BNGE_PLC_EN_HDS_THRES_VALID		\
	VNIC_PLCMODES_CFG_REQ_ENABLES_HDS_THRESHOLD_VALID
#define BNGE_VNIC_CFG_ROCE_DUAL_MODE		\
	VNIC_CFG_REQ_FLAGS_ROCE_DUAL_VNIC_MODE

int bnge_hwrm_ver_get(struct bnge_dev *bd);
int bnge_hwrm_func_reset(struct bnge_dev *bd);
int bnge_hwrm_fw_set_time(struct bnge_dev *bd);
int bnge_hwrm_func_drv_rgtr(struct bnge_dev *bd);
int bnge_hwrm_func_drv_unrgtr(struct bnge_dev *bd);
int bnge_hwrm_vnic_qcaps(struct bnge_dev *bd);
int bnge_hwrm_nvm_dev_info(struct bnge_dev *bd,
			   struct hwrm_nvm_get_dev_info_output *nvm_dev_info);
int bnge_hwrm_func_backing_store(struct bnge_dev *bd,
				 struct bnge_ctx_mem_type *ctxm,
				 bool last);
int bnge_hwrm_func_backing_store_qcaps(struct bnge_dev *bd);
int bnge_hwrm_reserve_rings(struct bnge_dev *bd,
			    struct bnge_hw_rings *hwr);
int bnge_hwrm_func_qcaps(struct bnge_dev *bd);
int bnge_hwrm_vnic_qcaps(struct bnge_dev *bd);
int bnge_hwrm_func_qcfg(struct bnge_dev *bd);
int bnge_hwrm_func_resc_qcaps(struct bnge_dev *bd);
int bnge_hwrm_queue_qportcfg(struct bnge_dev *bd);

int bnge_hwrm_vnic_set_hds(struct bnge_net *bn, struct bnge_vnic_info *vnic);
int bnge_hwrm_vnic_ctx_alloc(struct bnge_dev *bd,
			     struct bnge_vnic_info *vnic, u16 ctx_idx);
int bnge_hwrm_vnic_set_rss(struct bnge_net *bn,
			   struct bnge_vnic_info *vnic, bool set_rss);
int bnge_hwrm_vnic_cfg(struct bnge_net *bn, struct bnge_vnic_info *vnic);
void bnge_hwrm_update_rss_hash_cfg(struct bnge_net *bn);
int bnge_hwrm_vnic_alloc(struct bnge_dev *bd, struct bnge_vnic_info *vnic,
			 unsigned int nr_rings);
void bnge_hwrm_vnic_free_one(struct bnge_dev *bd, struct bnge_vnic_info *vnic);
void bnge_hwrm_vnic_ctx_free_one(struct bnge_dev *bd,
				 struct bnge_vnic_info *vnic, u16 ctx_idx);
int bnge_hwrm_l2_filter_free(struct bnge_dev *bd, struct bnge_l2_filter *fltr);
int bnge_hwrm_l2_filter_alloc(struct bnge_dev *bd, struct bnge_l2_filter *fltr);
int bnge_hwrm_cfa_l2_set_rx_mask(struct bnge_dev *bd,
				 struct bnge_vnic_info *vnic);
void bnge_hwrm_stat_ctx_free(struct bnge_net *bn);
int bnge_hwrm_stat_ctx_alloc(struct bnge_net *bn);
int hwrm_ring_free_send_msg(struct bnge_net *bn, struct bnge_ring_struct *ring,
			    u32 ring_type, int cmpl_ring_id);
int hwrm_ring_alloc_send_msg(struct bnge_net *bn,
			     struct bnge_ring_struct *ring,
			     u32 ring_type, u32 map_index);
int bnge_hwrm_set_async_event_cr(struct bnge_dev *bd, int idx);
#endif /* _BNGE_HWRM_LIB_H_ */
