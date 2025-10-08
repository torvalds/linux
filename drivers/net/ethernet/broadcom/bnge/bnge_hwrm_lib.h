/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_HWRM_LIB_H_
#define _BNGE_HWRM_LIB_H_

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

#endif /* _BNGE_HWRM_LIB_H_ */
