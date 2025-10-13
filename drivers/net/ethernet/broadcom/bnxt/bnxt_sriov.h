/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2018 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_SRIOV_H
#define BNXT_SRIOV_H

#define BNXT_FWD_RESP_SIZE_ERR(n)					\
	((offsetof(struct hwrm_fwd_resp_input, encap_resp) + n) >	\
	 sizeof(struct hwrm_fwd_resp_input))

#define BNXT_EXEC_FWD_RESP_SIZE_ERR(n)					\
	((offsetof(struct hwrm_exec_fwd_resp_input, encap_request) + n) >\
	 offsetof(struct hwrm_exec_fwd_resp_input, encap_resp_target_id))

#define BNXT_REJ_FWD_RESP_SIZE_ERR(n)					\
	((offsetof(struct hwrm_reject_fwd_resp_input, encap_request) + n) >\
	 offsetof(struct hwrm_reject_fwd_resp_input, encap_resp_target_id))

#define BNXT_VF_MIN_RSS_CTX	1
#define BNXT_VF_MAX_RSS_CTX	1
#define BNXT_VF_MIN_L2_CTX	1
#define BNXT_VF_MAX_L2_CTX	4

int bnxt_get_vf_config(struct net_device *, int, struct ifla_vf_info *);
int bnxt_set_vf_mac(struct net_device *, int, u8 *);
int bnxt_set_vf_vlan(struct net_device *, int, u16, u8, __be16);
int bnxt_set_vf_bw(struct net_device *, int, int, int);
int bnxt_set_vf_link_state(struct net_device *, int, int);
int bnxt_set_vf_spoofchk(struct net_device *, int, bool);
bool bnxt_is_trusted_vf(struct bnxt *bp, struct bnxt_vf_info *vf);
int bnxt_set_vf_trust(struct net_device *dev, int vf_id, bool trust);
int bnxt_sriov_configure(struct pci_dev *pdev, int num_vfs);
int bnxt_cfg_hw_sriov(struct bnxt *bp, int *num_vfs, bool reset);
void __bnxt_sriov_disable(struct bnxt *bp);
void bnxt_hwrm_exec_fwd_req(struct bnxt *);
void bnxt_update_vf_mac(struct bnxt *);
int bnxt_approve_mac(struct bnxt *, const u8 *, bool);
#endif
