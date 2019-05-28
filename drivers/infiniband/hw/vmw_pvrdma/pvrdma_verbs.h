/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __PVRDMA_VERBS_H__
#define __PVRDMA_VERBS_H__

#include <linux/types.h>

union pvrdma_gid {
	u8	raw[16];
	struct {
		__be64	subnet_prefix;
		__be64	interface_id;
	} global;
};

enum pvrdma_link_layer {
	PVRDMA_LINK_LAYER_UNSPECIFIED,
	PVRDMA_LINK_LAYER_INFINIBAND,
	PVRDMA_LINK_LAYER_ETHERNET,
};

enum pvrdma_mtu {
	PVRDMA_MTU_256  = 1,
	PVRDMA_MTU_512  = 2,
	PVRDMA_MTU_1024 = 3,
	PVRDMA_MTU_2048 = 4,
	PVRDMA_MTU_4096 = 5,
};

static inline int pvrdma_mtu_enum_to_int(enum pvrdma_mtu mtu)
{
	switch (mtu) {
	case PVRDMA_MTU_256:	return  256;
	case PVRDMA_MTU_512:	return  512;
	case PVRDMA_MTU_1024:	return 1024;
	case PVRDMA_MTU_2048:	return 2048;
	case PVRDMA_MTU_4096:	return 4096;
	default:		return   -1;
	}
}

static inline enum pvrdma_mtu pvrdma_mtu_int_to_enum(int mtu)
{
	switch (mtu) {
	case 256:	return PVRDMA_MTU_256;
	case 512:	return PVRDMA_MTU_512;
	case 1024:	return PVRDMA_MTU_1024;
	case 2048:	return PVRDMA_MTU_2048;
	case 4096:
	default:	return PVRDMA_MTU_4096;
	}
}

enum pvrdma_port_state {
	PVRDMA_PORT_NOP			= 0,
	PVRDMA_PORT_DOWN		= 1,
	PVRDMA_PORT_INIT		= 2,
	PVRDMA_PORT_ARMED		= 3,
	PVRDMA_PORT_ACTIVE		= 4,
	PVRDMA_PORT_ACTIVE_DEFER	= 5,
};

enum pvrdma_port_cap_flags {
	PVRDMA_PORT_SM				= 1 <<  1,
	PVRDMA_PORT_NOTICE_SUP			= 1 <<  2,
	PVRDMA_PORT_TRAP_SUP			= 1 <<  3,
	PVRDMA_PORT_OPT_IPD_SUP			= 1 <<  4,
	PVRDMA_PORT_AUTO_MIGR_SUP		= 1 <<  5,
	PVRDMA_PORT_SL_MAP_SUP			= 1 <<  6,
	PVRDMA_PORT_MKEY_NVRAM			= 1 <<  7,
	PVRDMA_PORT_PKEY_NVRAM			= 1 <<  8,
	PVRDMA_PORT_LED_INFO_SUP		= 1 <<  9,
	PVRDMA_PORT_SM_DISABLED			= 1 << 10,
	PVRDMA_PORT_SYS_IMAGE_GUID_SUP		= 1 << 11,
	PVRDMA_PORT_PKEY_SW_EXT_PORT_TRAP_SUP	= 1 << 12,
	PVRDMA_PORT_EXTENDED_SPEEDS_SUP		= 1 << 14,
	PVRDMA_PORT_CM_SUP			= 1 << 16,
	PVRDMA_PORT_SNMP_TUNNEL_SUP		= 1 << 17,
	PVRDMA_PORT_REINIT_SUP			= 1 << 18,
	PVRDMA_PORT_DEVICE_MGMT_SUP		= 1 << 19,
	PVRDMA_PORT_VENDOR_CLASS_SUP		= 1 << 20,
	PVRDMA_PORT_DR_NOTICE_SUP		= 1 << 21,
	PVRDMA_PORT_CAP_MASK_NOTICE_SUP		= 1 << 22,
	PVRDMA_PORT_BOOT_MGMT_SUP		= 1 << 23,
	PVRDMA_PORT_LINK_LATENCY_SUP		= 1 << 24,
	PVRDMA_PORT_CLIENT_REG_SUP		= 1 << 25,
	PVRDMA_PORT_IP_BASED_GIDS		= 1 << 26,
	PVRDMA_PORT_CAP_FLAGS_MAX		= PVRDMA_PORT_IP_BASED_GIDS,
};

enum pvrdma_port_width {
	PVRDMA_WIDTH_1X		= 1,
	PVRDMA_WIDTH_4X		= 2,
	PVRDMA_WIDTH_8X		= 4,
	PVRDMA_WIDTH_12X	= 8,
};

static inline int pvrdma_width_enum_to_int(enum pvrdma_port_width width)
{
	switch (width) {
	case PVRDMA_WIDTH_1X:	return  1;
	case PVRDMA_WIDTH_4X:	return  4;
	case PVRDMA_WIDTH_8X:	return  8;
	case PVRDMA_WIDTH_12X:	return 12;
	default:		return -1;
	}
}

enum pvrdma_port_speed {
	PVRDMA_SPEED_SDR	= 1,
	PVRDMA_SPEED_DDR	= 2,
	PVRDMA_SPEED_QDR	= 4,
	PVRDMA_SPEED_FDR10	= 8,
	PVRDMA_SPEED_FDR	= 16,
	PVRDMA_SPEED_EDR	= 32,
};

struct pvrdma_port_attr {
	enum pvrdma_port_state	state;
	enum pvrdma_mtu		max_mtu;
	enum pvrdma_mtu		active_mtu;
	u32			gid_tbl_len;
	u32			port_cap_flags;
	u32			max_msg_sz;
	u32			bad_pkey_cntr;
	u32			qkey_viol_cntr;
	u16			pkey_tbl_len;
	u16			lid;
	u16			sm_lid;
	u8			lmc;
	u8			max_vl_num;
	u8			sm_sl;
	u8			subnet_timeout;
	u8			init_type_reply;
	u8			active_width;
	u8			active_speed;
	u8			phys_state;
	u8			reserved[2];
};

struct pvrdma_global_route {
	union pvrdma_gid	dgid;
	u32			flow_label;
	u8			sgid_index;
	u8			hop_limit;
	u8			traffic_class;
	u8			reserved;
};

struct pvrdma_grh {
	__be32			version_tclass_flow;
	__be16			paylen;
	u8			next_hdr;
	u8			hop_limit;
	union pvrdma_gid	sgid;
	union pvrdma_gid	dgid;
};

enum pvrdma_ah_flags {
	PVRDMA_AH_GRH = 1,
};

enum pvrdma_rate {
	PVRDMA_RATE_PORT_CURRENT	= 0,
	PVRDMA_RATE_2_5_GBPS		= 2,
	PVRDMA_RATE_5_GBPS		= 5,
	PVRDMA_RATE_10_GBPS		= 3,
	PVRDMA_RATE_20_GBPS		= 6,
	PVRDMA_RATE_30_GBPS		= 4,
	PVRDMA_RATE_40_GBPS		= 7,
	PVRDMA_RATE_60_GBPS		= 8,
	PVRDMA_RATE_80_GBPS		= 9,
	PVRDMA_RATE_120_GBPS		= 10,
	PVRDMA_RATE_14_GBPS		= 11,
	PVRDMA_RATE_56_GBPS		= 12,
	PVRDMA_RATE_112_GBPS		= 13,
	PVRDMA_RATE_168_GBPS		= 14,
	PVRDMA_RATE_25_GBPS		= 15,
	PVRDMA_RATE_100_GBPS		= 16,
	PVRDMA_RATE_200_GBPS		= 17,
	PVRDMA_RATE_300_GBPS		= 18,
};

struct pvrdma_ah_attr {
	struct pvrdma_global_route	grh;
	u16				dlid;
	u16				vlan_id;
	u8				sl;
	u8				src_path_bits;
	u8				static_rate;
	u8				ah_flags;
	u8				port_num;
	u8				dmac[6];
	u8				reserved;
};

enum pvrdma_cq_notify_flags {
	PVRDMA_CQ_SOLICITED		= 1 << 0,
	PVRDMA_CQ_NEXT_COMP		= 1 << 1,
	PVRDMA_CQ_SOLICITED_MASK	= PVRDMA_CQ_SOLICITED |
					  PVRDMA_CQ_NEXT_COMP,
	PVRDMA_CQ_REPORT_MISSED_EVENTS	= 1 << 2,
};

struct pvrdma_qp_cap {
	u32	max_send_wr;
	u32	max_recv_wr;
	u32	max_send_sge;
	u32	max_recv_sge;
	u32	max_inline_data;
	u32	reserved;
};

enum pvrdma_sig_type {
	PVRDMA_SIGNAL_ALL_WR,
	PVRDMA_SIGNAL_REQ_WR,
};

enum pvrdma_qp_type {
	PVRDMA_QPT_SMI,
	PVRDMA_QPT_GSI,
	PVRDMA_QPT_RC,
	PVRDMA_QPT_UC,
	PVRDMA_QPT_UD,
	PVRDMA_QPT_RAW_IPV6,
	PVRDMA_QPT_RAW_ETHERTYPE,
	PVRDMA_QPT_RAW_PACKET = 8,
	PVRDMA_QPT_XRC_INI = 9,
	PVRDMA_QPT_XRC_TGT,
	PVRDMA_QPT_MAX,
};

enum pvrdma_qp_create_flags {
	PVRDMA_QP_CREATE_IPOPVRDMA_UD_LSO		= 1 << 0,
	PVRDMA_QP_CREATE_BLOCK_MULTICAST_LOOPBACK	= 1 << 1,
};

enum pvrdma_qp_attr_mask {
	PVRDMA_QP_STATE			= 1 << 0,
	PVRDMA_QP_CUR_STATE		= 1 << 1,
	PVRDMA_QP_EN_SQD_ASYNC_NOTIFY	= 1 << 2,
	PVRDMA_QP_ACCESS_FLAGS		= 1 << 3,
	PVRDMA_QP_PKEY_INDEX		= 1 << 4,
	PVRDMA_QP_PORT			= 1 << 5,
	PVRDMA_QP_QKEY			= 1 << 6,
	PVRDMA_QP_AV			= 1 << 7,
	PVRDMA_QP_PATH_MTU		= 1 << 8,
	PVRDMA_QP_TIMEOUT		= 1 << 9,
	PVRDMA_QP_RETRY_CNT		= 1 << 10,
	PVRDMA_QP_RNR_RETRY		= 1 << 11,
	PVRDMA_QP_RQ_PSN		= 1 << 12,
	PVRDMA_QP_MAX_QP_RD_ATOMIC	= 1 << 13,
	PVRDMA_QP_ALT_PATH		= 1 << 14,
	PVRDMA_QP_MIN_RNR_TIMER		= 1 << 15,
	PVRDMA_QP_SQ_PSN		= 1 << 16,
	PVRDMA_QP_MAX_DEST_RD_ATOMIC	= 1 << 17,
	PVRDMA_QP_PATH_MIG_STATE	= 1 << 18,
	PVRDMA_QP_CAP			= 1 << 19,
	PVRDMA_QP_DEST_QPN		= 1 << 20,
	PVRDMA_QP_ATTR_MASK_MAX		= PVRDMA_QP_DEST_QPN,
};

enum pvrdma_qp_state {
	PVRDMA_QPS_RESET,
	PVRDMA_QPS_INIT,
	PVRDMA_QPS_RTR,
	PVRDMA_QPS_RTS,
	PVRDMA_QPS_SQD,
	PVRDMA_QPS_SQE,
	PVRDMA_QPS_ERR,
};

enum pvrdma_mig_state {
	PVRDMA_MIG_MIGRATED,
	PVRDMA_MIG_REARM,
	PVRDMA_MIG_ARMED,
};

enum pvrdma_mw_type {
	PVRDMA_MW_TYPE_1 = 1,
	PVRDMA_MW_TYPE_2 = 2,
};

struct pvrdma_srq_attr {
	u32			max_wr;
	u32			max_sge;
	u32			srq_limit;
	u32			reserved;
};

struct pvrdma_qp_attr {
	enum pvrdma_qp_state	qp_state;
	enum pvrdma_qp_state	cur_qp_state;
	enum pvrdma_mtu		path_mtu;
	enum pvrdma_mig_state	path_mig_state;
	u32			qkey;
	u32			rq_psn;
	u32			sq_psn;
	u32			dest_qp_num;
	u32			qp_access_flags;
	u16			pkey_index;
	u16			alt_pkey_index;
	u8			en_sqd_async_notify;
	u8			sq_draining;
	u8			max_rd_atomic;
	u8			max_dest_rd_atomic;
	u8			min_rnr_timer;
	u8			port_num;
	u8			timeout;
	u8			retry_cnt;
	u8			rnr_retry;
	u8			alt_port_num;
	u8			alt_timeout;
	u8			reserved[5];
	struct pvrdma_qp_cap	cap;
	struct pvrdma_ah_attr	ah_attr;
	struct pvrdma_ah_attr	alt_ah_attr;
};

enum pvrdma_send_flags {
	PVRDMA_SEND_FENCE	= 1 << 0,
	PVRDMA_SEND_SIGNALED	= 1 << 1,
	PVRDMA_SEND_SOLICITED	= 1 << 2,
	PVRDMA_SEND_INLINE	= 1 << 3,
	PVRDMA_SEND_IP_CSUM	= 1 << 4,
	PVRDMA_SEND_FLAGS_MAX	= PVRDMA_SEND_IP_CSUM,
};

enum pvrdma_access_flags {
	PVRDMA_ACCESS_LOCAL_WRITE	= 1 << 0,
	PVRDMA_ACCESS_REMOTE_WRITE	= 1 << 1,
	PVRDMA_ACCESS_REMOTE_READ	= 1 << 2,
	PVRDMA_ACCESS_REMOTE_ATOMIC	= 1 << 3,
	PVRDMA_ACCESS_MW_BIND		= 1 << 4,
	PVRDMA_ZERO_BASED		= 1 << 5,
	PVRDMA_ACCESS_ON_DEMAND		= 1 << 6,
	PVRDMA_ACCESS_FLAGS_MAX		= PVRDMA_ACCESS_ON_DEMAND,
};

int pvrdma_query_device(struct ib_device *ibdev,
			struct ib_device_attr *props,
			struct ib_udata *udata);
int pvrdma_query_port(struct ib_device *ibdev, u8 port,
		      struct ib_port_attr *props);
int pvrdma_query_gid(struct ib_device *ibdev, u8 port,
		     int index, union ib_gid *gid);
int pvrdma_query_pkey(struct ib_device *ibdev, u8 port,
		      u16 index, u16 *pkey);
enum rdma_link_layer pvrdma_port_link_layer(struct ib_device *ibdev,
					    u8 port);
int pvrdma_modify_device(struct ib_device *ibdev, int mask,
			 struct ib_device_modify *props);
int pvrdma_modify_port(struct ib_device *ibdev, u8 port,
		       int mask, struct ib_port_modify *props);
int pvrdma_mmap(struct ib_ucontext *context, struct vm_area_struct *vma);
int pvrdma_alloc_ucontext(struct ib_ucontext *uctx, struct ib_udata *udata);
void pvrdma_dealloc_ucontext(struct ib_ucontext *context);
int pvrdma_alloc_pd(struct ib_pd *pd, struct ib_udata *udata);
void pvrdma_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
struct ib_mr *pvrdma_get_dma_mr(struct ib_pd *pd, int acc);
struct ib_mr *pvrdma_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				 u64 virt_addr, int access_flags,
				 struct ib_udata *udata);
int pvrdma_dereg_mr(struct ib_mr *mr, struct ib_udata *udata);
struct ib_mr *pvrdma_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
			      u32 max_num_sg, struct ib_udata *udata);
int pvrdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
		     int sg_nents, unsigned int *sg_offset);
struct ib_cq *pvrdma_create_cq(struct ib_device *ibdev,
			       const struct ib_cq_init_attr *attr,
			       struct ib_udata *udata);
void pvrdma_destroy_cq(struct ib_cq *cq, struct ib_udata *udata);
int pvrdma_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc);
int pvrdma_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);
int pvrdma_create_ah(struct ib_ah *ah, struct rdma_ah_attr *ah_attr, u32 flags,
		     struct ib_udata *udata);
void pvrdma_destroy_ah(struct ib_ah *ah, u32 flags);

int pvrdma_create_srq(struct ib_srq *srq, struct ib_srq_init_attr *init_attr,
		      struct ib_udata *udata);
int pvrdma_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		      enum ib_srq_attr_mask attr_mask, struct ib_udata *udata);
int pvrdma_query_srq(struct ib_srq *srq, struct ib_srq_attr *srq_attr);
void pvrdma_destroy_srq(struct ib_srq *srq, struct ib_udata *udata);

struct ib_qp *pvrdma_create_qp(struct ib_pd *pd,
			       struct ib_qp_init_attr *init_attr,
			       struct ib_udata *udata);
int pvrdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		     int attr_mask, struct ib_udata *udata);
int pvrdma_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		    int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr);
int pvrdma_destroy_qp(struct ib_qp *qp, struct ib_udata *udata);
int pvrdma_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		     const struct ib_send_wr **bad_wr);
int pvrdma_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		     const struct ib_recv_wr **bad_wr);

#endif /* __PVRDMA_VERBS_H__ */
