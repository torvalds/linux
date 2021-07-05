/* SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB */
/* Copyright (c) 2016 - 2021 Intel Corporation */
#ifndef IRDMA_UDA_H
#define IRDMA_UDA_H

#define IRDMA_UDA_MAX_FSI_MGS	4096
#define IRDMA_UDA_MAX_PFS	16
#define IRDMA_UDA_MAX_VFS	128

struct irdma_sc_cqp;

struct irdma_ah_info {
	struct irdma_sc_vsi *vsi;
	u32 pd_idx;
	u32 dst_arpindex;
	u32 dest_ip_addr[4];
	u32 src_ip_addr[4];
	u32 flow_label;
	u32 ah_idx;
	u16 vlan_tag;
	u8 insert_vlan_tag;
	u8 tc_tos;
	u8 hop_ttl;
	u8 mac_addr[ETH_ALEN];
	bool ah_valid:1;
	bool ipv4_valid:1;
	bool do_lpbk:1;
};

struct irdma_sc_ah {
	struct irdma_sc_dev *dev;
	struct irdma_ah_info ah_info;
};

enum irdma_status_code irdma_sc_add_mcast_grp(struct irdma_mcast_grp_info *ctx,
					      struct irdma_mcast_grp_ctx_entry_info *mg);
enum irdma_status_code irdma_sc_del_mcast_grp(struct irdma_mcast_grp_info *ctx,
					      struct irdma_mcast_grp_ctx_entry_info *mg);
enum irdma_status_code irdma_sc_access_ah(struct irdma_sc_cqp *cqp, struct irdma_ah_info *info,
					  u32 op, u64 scratch);
enum irdma_status_code irdma_access_mcast_grp(struct irdma_sc_cqp *cqp,
					      struct irdma_mcast_grp_info *info,
					      u32 op, u64 scratch);

static inline void irdma_sc_init_ah(struct irdma_sc_dev *dev, struct irdma_sc_ah *ah)
{
	ah->dev = dev;
}

static inline enum irdma_status_code irdma_sc_create_ah(struct irdma_sc_cqp *cqp,
							struct irdma_ah_info *info,
							u64 scratch)
{
	return irdma_sc_access_ah(cqp, info, IRDMA_CQP_OP_CREATE_ADDR_HANDLE,
				  scratch);
}

static inline enum irdma_status_code irdma_sc_destroy_ah(struct irdma_sc_cqp *cqp,
							 struct irdma_ah_info *info,
							 u64 scratch)
{
	return irdma_sc_access_ah(cqp, info, IRDMA_CQP_OP_DESTROY_ADDR_HANDLE,
				  scratch);
}

static inline enum irdma_status_code irdma_sc_create_mcast_grp(struct irdma_sc_cqp *cqp,
							       struct irdma_mcast_grp_info *info,
							       u64 scratch)
{
	return irdma_access_mcast_grp(cqp, info, IRDMA_CQP_OP_CREATE_MCAST_GRP,
				      scratch);
}

static inline enum irdma_status_code irdma_sc_modify_mcast_grp(struct irdma_sc_cqp *cqp,
							       struct irdma_mcast_grp_info *info,
							       u64 scratch)
{
	return irdma_access_mcast_grp(cqp, info, IRDMA_CQP_OP_MODIFY_MCAST_GRP,
				      scratch);
}

static inline enum irdma_status_code irdma_sc_destroy_mcast_grp(struct irdma_sc_cqp *cqp,
								struct irdma_mcast_grp_info *info,
								u64 scratch)
{
	return irdma_access_mcast_grp(cqp, info, IRDMA_CQP_OP_DESTROY_MCAST_GRP,
				      scratch);
}
#endif /* IRDMA_UDA_H */
