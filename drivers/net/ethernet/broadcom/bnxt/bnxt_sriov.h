/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_SRIOV_H
#define BNXT_SRIOV_H

int bnxt_get_vf_config(struct net_device *, int, struct ifla_vf_info *);
int bnxt_set_vf_mac(struct net_device *, int, u8 *);
int bnxt_set_vf_vlan(struct net_device *, int, u16, u8);
int bnxt_set_vf_bw(struct net_device *, int, int, int);
int bnxt_set_vf_link_state(struct net_device *, int, int);
int bnxt_set_vf_spoofchk(struct net_device *, int, bool);
int bnxt_sriov_configure(struct pci_dev *pdev, int num_vfs);
void bnxt_sriov_disable(struct bnxt *);
void bnxt_hwrm_exec_fwd_req(struct bnxt *);
void bnxt_update_vf_mac(struct bnxt *);
#endif
