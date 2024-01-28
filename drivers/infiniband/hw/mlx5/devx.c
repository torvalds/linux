// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#include <rdma/uverbs_types.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/mlx5_user_ioctl_cmds.h>
#include <rdma/mlx5_user_ioctl_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/uverbs_std_types.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/fs.h>
#include "mlx5_ib.h"
#include "devx.h"
#include "qp.h"
#include <linux/xarray.h>

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

static void dispatch_event_fd(struct list_head *fd_list, const void *data);

enum devx_obj_flags {
	DEVX_OBJ_FLAGS_INDIRECT_MKEY = 1 << 0,
	DEVX_OBJ_FLAGS_DCT = 1 << 1,
	DEVX_OBJ_FLAGS_CQ = 1 << 2,
};

struct devx_async_data {
	struct mlx5_ib_dev *mdev;
	struct list_head list;
	struct devx_async_cmd_event_file *ev_file;
	struct mlx5_async_work cb_work;
	u16 cmd_out_len;
	/* must be last field in this structure */
	struct mlx5_ib_uapi_devx_async_cmd_hdr hdr;
};

struct devx_async_event_data {
	struct list_head list; /* headed in ev_file->event_list */
	struct mlx5_ib_uapi_devx_async_event_hdr hdr;
};

/* first level XA value data structure */
struct devx_event {
	struct xarray object_ids; /* second XA level, Key = object id */
	struct list_head unaffiliated_list;
};

/* second level XA value data structure */
struct devx_obj_event {
	struct rcu_head rcu;
	struct list_head obj_sub_list;
};

struct devx_event_subscription {
	struct list_head file_list; /* headed in ev_file->
				     * subscribed_events_list
				     */
	struct list_head xa_list; /* headed in devx_event->unaffiliated_list or
				   * devx_obj_event->obj_sub_list
				   */
	struct list_head obj_list; /* headed in devx_object */
	struct list_head event_list; /* headed in ev_file->event_list or in
				      * temp list via subscription
				      */

	u8 is_cleaned:1;
	u32 xa_key_level1;
	u32 xa_key_level2;
	struct rcu_head	rcu;
	u64 cookie;
	struct devx_async_event_file *ev_file;
	struct eventfd_ctx *eventfd;
};

struct devx_async_event_file {
	struct ib_uobject uobj;
	/* Head of events that are subscribed to this FD */
	struct list_head subscribed_events_list;
	spinlock_t lock;
	wait_queue_head_t poll_wait;
	struct list_head event_list;
	struct mlx5_ib_dev *dev;
	u8 omit_data:1;
	u8 is_overflow_err:1;
	u8 is_destroyed:1;
};

struct devx_umem {
	struct mlx5_core_dev		*mdev;
	struct ib_umem			*umem;
	u32				dinlen;
	u32				dinbox[MLX5_ST_SZ_DW(destroy_umem_in)];
};

struct devx_umem_reg_cmd {
	void				*in;
	u32				inlen;
	u32				out[MLX5_ST_SZ_DW(create_umem_out)];
};

static struct mlx5_ib_ucontext *
devx_ufile2uctx(const struct uverbs_attr_bundle *attrs)
{
	return to_mucontext(ib_uverbs_get_ucontext(attrs));
}

int mlx5_ib_devx_create(struct mlx5_ib_dev *dev, bool is_user)
{
	u32 in[MLX5_ST_SZ_DW(create_uctx_in)] = {};
	u32 out[MLX5_ST_SZ_DW(create_uctx_out)] = {};
	void *uctx;
	int err;
	u16 uid;
	u32 cap = 0;

	/* 0 means not supported */
	if (!MLX5_CAP_GEN(dev->mdev, log_max_uctx))
		return -EINVAL;

	uctx = MLX5_ADDR_OF(create_uctx_in, in, uctx);
	if (is_user && capable(CAP_NET_RAW) &&
	    (MLX5_CAP_GEN(dev->mdev, uctx_cap) & MLX5_UCTX_CAP_RAW_TX))
		cap |= MLX5_UCTX_CAP_RAW_TX;
	if (is_user && capable(CAP_SYS_RAWIO) &&
	    (MLX5_CAP_GEN(dev->mdev, uctx_cap) &
	     MLX5_UCTX_CAP_INTERNAL_DEV_RES))
		cap |= MLX5_UCTX_CAP_INTERNAL_DEV_RES;

	MLX5_SET(create_uctx_in, in, opcode, MLX5_CMD_OP_CREATE_UCTX);
	MLX5_SET(uctx, uctx, cap, cap);

	err = mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	uid = MLX5_GET(create_uctx_out, out, uid);
	return uid;
}

void mlx5_ib_devx_destroy(struct mlx5_ib_dev *dev, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(destroy_uctx_in)] = {};
	u32 out[MLX5_ST_SZ_DW(destroy_uctx_out)] = {};

	MLX5_SET(destroy_uctx_in, in, opcode, MLX5_CMD_OP_DESTROY_UCTX);
	MLX5_SET(destroy_uctx_in, in, uid, uid);

	mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
}

static bool is_legacy_unaffiliated_event_num(u16 event_num)
{
	switch (event_num) {
	case MLX5_EVENT_TYPE_PORT_CHANGE:
		return true;
	default:
		return false;
	}
}

static bool is_legacy_obj_event_num(u16 event_num)
{
	switch (event_num) {
	case MLX5_EVENT_TYPE_PATH_MIG:
	case MLX5_EVENT_TYPE_COMM_EST:
	case MLX5_EVENT_TYPE_SQ_DRAINED:
	case MLX5_EVENT_TYPE_SRQ_LAST_WQE:
	case MLX5_EVENT_TYPE_SRQ_RQ_LIMIT:
	case MLX5_EVENT_TYPE_CQ_ERROR:
	case MLX5_EVENT_TYPE_WQ_CATAS_ERROR:
	case MLX5_EVENT_TYPE_PATH_MIG_FAILED:
	case MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
	case MLX5_EVENT_TYPE_WQ_ACCESS_ERROR:
	case MLX5_EVENT_TYPE_SRQ_CATAS_ERROR:
	case MLX5_EVENT_TYPE_DCT_DRAINED:
	case MLX5_EVENT_TYPE_COMP:
	case MLX5_EVENT_TYPE_DCT_KEY_VIOLATION:
	case MLX5_EVENT_TYPE_XRQ_ERROR:
		return true;
	default:
		return false;
	}
}

static u16 get_legacy_obj_type(u16 opcode)
{
	switch (opcode) {
	case MLX5_CMD_OP_CREATE_RQ:
		return MLX5_EVENT_QUEUE_TYPE_RQ;
	case MLX5_CMD_OP_CREATE_QP:
		return MLX5_EVENT_QUEUE_TYPE_QP;
	case MLX5_CMD_OP_CREATE_SQ:
		return MLX5_EVENT_QUEUE_TYPE_SQ;
	case MLX5_CMD_OP_CREATE_DCT:
		return MLX5_EVENT_QUEUE_TYPE_DCT;
	default:
		return 0;
	}
}

static u16 get_dec_obj_type(struct devx_obj *obj, u16 event_num)
{
	u16 opcode;

	opcode = (obj->obj_id >> 32) & 0xffff;

	if (is_legacy_obj_event_num(event_num))
		return get_legacy_obj_type(opcode);

	switch (opcode) {
	case MLX5_CMD_OP_CREATE_GENERAL_OBJECT:
		return (obj->obj_id >> 48);
	case MLX5_CMD_OP_CREATE_RQ:
		return MLX5_OBJ_TYPE_RQ;
	case MLX5_CMD_OP_CREATE_QP:
		return MLX5_OBJ_TYPE_QP;
	case MLX5_CMD_OP_CREATE_SQ:
		return MLX5_OBJ_TYPE_SQ;
	case MLX5_CMD_OP_CREATE_DCT:
		return MLX5_OBJ_TYPE_DCT;
	case MLX5_CMD_OP_CREATE_TIR:
		return MLX5_OBJ_TYPE_TIR;
	case MLX5_CMD_OP_CREATE_TIS:
		return MLX5_OBJ_TYPE_TIS;
	case MLX5_CMD_OP_CREATE_PSV:
		return MLX5_OBJ_TYPE_PSV;
	case MLX5_OBJ_TYPE_MKEY:
		return MLX5_OBJ_TYPE_MKEY;
	case MLX5_CMD_OP_CREATE_RMP:
		return MLX5_OBJ_TYPE_RMP;
	case MLX5_CMD_OP_CREATE_XRC_SRQ:
		return MLX5_OBJ_TYPE_XRC_SRQ;
	case MLX5_CMD_OP_CREATE_XRQ:
		return MLX5_OBJ_TYPE_XRQ;
	case MLX5_CMD_OP_CREATE_RQT:
		return MLX5_OBJ_TYPE_RQT;
	case MLX5_CMD_OP_ALLOC_FLOW_COUNTER:
		return MLX5_OBJ_TYPE_FLOW_COUNTER;
	case MLX5_CMD_OP_CREATE_CQ:
		return MLX5_OBJ_TYPE_CQ;
	default:
		return 0;
	}
}

static u16 get_event_obj_type(unsigned long event_type, struct mlx5_eqe *eqe)
{
	switch (event_type) {
	case MLX5_EVENT_TYPE_WQ_CATAS_ERROR:
	case MLX5_EVENT_TYPE_WQ_ACCESS_ERROR:
	case MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
	case MLX5_EVENT_TYPE_SRQ_LAST_WQE:
	case MLX5_EVENT_TYPE_PATH_MIG:
	case MLX5_EVENT_TYPE_PATH_MIG_FAILED:
	case MLX5_EVENT_TYPE_COMM_EST:
	case MLX5_EVENT_TYPE_SQ_DRAINED:
	case MLX5_EVENT_TYPE_SRQ_RQ_LIMIT:
	case MLX5_EVENT_TYPE_SRQ_CATAS_ERROR:
		return eqe->data.qp_srq.type;
	case MLX5_EVENT_TYPE_CQ_ERROR:
	case MLX5_EVENT_TYPE_XRQ_ERROR:
		return 0;
	case MLX5_EVENT_TYPE_DCT_DRAINED:
	case MLX5_EVENT_TYPE_DCT_KEY_VIOLATION:
		return MLX5_EVENT_QUEUE_TYPE_DCT;
	default:
		return MLX5_GET(affiliated_event_header, &eqe->data, obj_type);
	}
}

static u32 get_dec_obj_id(u64 obj_id)
{
	return (obj_id & 0xffffffff);
}

/*
 * As the obj_id in the firmware is not globally unique the object type
 * must be considered upon checking for a valid object id.
 * For that the opcode of the creator command is encoded as part of the obj_id.
 */
static u64 get_enc_obj_id(u32 opcode, u32 obj_id)
{
	return ((u64)opcode << 32) | obj_id;
}

static u32 devx_get_created_obj_id(const void *in, const void *out, u16 opcode)
{
	switch (opcode) {
	case MLX5_CMD_OP_CREATE_GENERAL_OBJECT:
		return MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
	case MLX5_CMD_OP_CREATE_UMEM:
		return MLX5_GET(create_umem_out, out, umem_id);
	case MLX5_CMD_OP_CREATE_MKEY:
		return MLX5_GET(create_mkey_out, out, mkey_index);
	case MLX5_CMD_OP_CREATE_CQ:
		return MLX5_GET(create_cq_out, out, cqn);
	case MLX5_CMD_OP_ALLOC_PD:
		return MLX5_GET(alloc_pd_out, out, pd);
	case MLX5_CMD_OP_ALLOC_TRANSPORT_DOMAIN:
		return MLX5_GET(alloc_transport_domain_out, out,
				transport_domain);
	case MLX5_CMD_OP_CREATE_RMP:
		return MLX5_GET(create_rmp_out, out, rmpn);
	case MLX5_CMD_OP_CREATE_SQ:
		return MLX5_GET(create_sq_out, out, sqn);
	case MLX5_CMD_OP_CREATE_RQ:
		return MLX5_GET(create_rq_out, out, rqn);
	case MLX5_CMD_OP_CREATE_RQT:
		return MLX5_GET(create_rqt_out, out, rqtn);
	case MLX5_CMD_OP_CREATE_TIR:
		return MLX5_GET(create_tir_out, out, tirn);
	case MLX5_CMD_OP_CREATE_TIS:
		return MLX5_GET(create_tis_out, out, tisn);
	case MLX5_CMD_OP_ALLOC_Q_COUNTER:
		return MLX5_GET(alloc_q_counter_out, out, counter_set_id);
	case MLX5_CMD_OP_CREATE_FLOW_TABLE:
		return MLX5_GET(create_flow_table_out, out, table_id);
	case MLX5_CMD_OP_CREATE_FLOW_GROUP:
		return MLX5_GET(create_flow_group_out, out, group_id);
	case MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY:
		return MLX5_GET(set_fte_in, in, flow_index);
	case MLX5_CMD_OP_ALLOC_FLOW_COUNTER:
		return MLX5_GET(alloc_flow_counter_out, out, flow_counter_id);
	case MLX5_CMD_OP_ALLOC_PACKET_REFORMAT_CONTEXT:
		return MLX5_GET(alloc_packet_reformat_context_out, out,
				packet_reformat_id);
	case MLX5_CMD_OP_ALLOC_MODIFY_HEADER_CONTEXT:
		return MLX5_GET(alloc_modify_header_context_out, out,
				modify_header_id);
	case MLX5_CMD_OP_CREATE_SCHEDULING_ELEMENT:
		return MLX5_GET(create_scheduling_element_out, out,
				scheduling_element_id);
	case MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT:
		return MLX5_GET(add_vxlan_udp_dport_in, in, vxlan_udp_port);
	case MLX5_CMD_OP_SET_L2_TABLE_ENTRY:
		return MLX5_GET(set_l2_table_entry_in, in, table_index);
	case MLX5_CMD_OP_CREATE_QP:
		return MLX5_GET(create_qp_out, out, qpn);
	case MLX5_CMD_OP_CREATE_SRQ:
		return MLX5_GET(create_srq_out, out, srqn);
	case MLX5_CMD_OP_CREATE_XRC_SRQ:
		return MLX5_GET(create_xrc_srq_out, out, xrc_srqn);
	case MLX5_CMD_OP_CREATE_DCT:
		return MLX5_GET(create_dct_out, out, dctn);
	case MLX5_CMD_OP_CREATE_XRQ:
		return MLX5_GET(create_xrq_out, out, xrqn);
	case MLX5_CMD_OP_ATTACH_TO_MCG:
		return MLX5_GET(attach_to_mcg_in, in, qpn);
	case MLX5_CMD_OP_ALLOC_XRCD:
		return MLX5_GET(alloc_xrcd_out, out, xrcd);
	case MLX5_CMD_OP_CREATE_PSV:
		return MLX5_GET(create_psv_out, out, psv0_index);
	default:
		/* The entry must match to one of the devx_is_obj_create_cmd */
		WARN_ON(true);
		return 0;
	}
}

static u64 devx_get_obj_id(const void *in)
{
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);
	u64 obj_id;

	switch (opcode) {
	case MLX5_CMD_OP_MODIFY_GENERAL_OBJECT:
	case MLX5_CMD_OP_QUERY_GENERAL_OBJECT:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_GENERAL_OBJECT |
					MLX5_GET(general_obj_in_cmd_hdr, in,
						 obj_type) << 16,
					MLX5_GET(general_obj_in_cmd_hdr, in,
						 obj_id));
		break;
	case MLX5_CMD_OP_QUERY_MKEY:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_MKEY,
					MLX5_GET(query_mkey_in, in,
						 mkey_index));
		break;
	case MLX5_CMD_OP_QUERY_CQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_CQ,
					MLX5_GET(query_cq_in, in, cqn));
		break;
	case MLX5_CMD_OP_MODIFY_CQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_CQ,
					MLX5_GET(modify_cq_in, in, cqn));
		break;
	case MLX5_CMD_OP_QUERY_SQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_SQ,
					MLX5_GET(query_sq_in, in, sqn));
		break;
	case MLX5_CMD_OP_MODIFY_SQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_SQ,
					MLX5_GET(modify_sq_in, in, sqn));
		break;
	case MLX5_CMD_OP_QUERY_RQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_RQ,
					MLX5_GET(query_rq_in, in, rqn));
		break;
	case MLX5_CMD_OP_MODIFY_RQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_RQ,
					MLX5_GET(modify_rq_in, in, rqn));
		break;
	case MLX5_CMD_OP_QUERY_RMP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_RMP,
					MLX5_GET(query_rmp_in, in, rmpn));
		break;
	case MLX5_CMD_OP_MODIFY_RMP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_RMP,
					MLX5_GET(modify_rmp_in, in, rmpn));
		break;
	case MLX5_CMD_OP_QUERY_RQT:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_RQT,
					MLX5_GET(query_rqt_in, in, rqtn));
		break;
	case MLX5_CMD_OP_MODIFY_RQT:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_RQT,
					MLX5_GET(modify_rqt_in, in, rqtn));
		break;
	case MLX5_CMD_OP_QUERY_TIR:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_TIR,
					MLX5_GET(query_tir_in, in, tirn));
		break;
	case MLX5_CMD_OP_MODIFY_TIR:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_TIR,
					MLX5_GET(modify_tir_in, in, tirn));
		break;
	case MLX5_CMD_OP_QUERY_TIS:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_TIS,
					MLX5_GET(query_tis_in, in, tisn));
		break;
	case MLX5_CMD_OP_MODIFY_TIS:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_TIS,
					MLX5_GET(modify_tis_in, in, tisn));
		break;
	case MLX5_CMD_OP_QUERY_FLOW_TABLE:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_FLOW_TABLE,
					MLX5_GET(query_flow_table_in, in,
						 table_id));
		break;
	case MLX5_CMD_OP_MODIFY_FLOW_TABLE:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_FLOW_TABLE,
					MLX5_GET(modify_flow_table_in, in,
						 table_id));
		break;
	case MLX5_CMD_OP_QUERY_FLOW_GROUP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_FLOW_GROUP,
					MLX5_GET(query_flow_group_in, in,
						 group_id));
		break;
	case MLX5_CMD_OP_QUERY_FLOW_TABLE_ENTRY:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY,
					MLX5_GET(query_fte_in, in,
						 flow_index));
		break;
	case MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY,
					MLX5_GET(set_fte_in, in, flow_index));
		break;
	case MLX5_CMD_OP_QUERY_Q_COUNTER:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_ALLOC_Q_COUNTER,
					MLX5_GET(query_q_counter_in, in,
						 counter_set_id));
		break;
	case MLX5_CMD_OP_QUERY_FLOW_COUNTER:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_ALLOC_FLOW_COUNTER,
					MLX5_GET(query_flow_counter_in, in,
						 flow_counter_id));
		break;
	case MLX5_CMD_OP_QUERY_MODIFY_HEADER_CONTEXT:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_ALLOC_MODIFY_HEADER_CONTEXT,
					MLX5_GET(query_modify_header_context_in,
						 in, modify_header_id));
		break;
	case MLX5_CMD_OP_QUERY_SCHEDULING_ELEMENT:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_SCHEDULING_ELEMENT,
					MLX5_GET(query_scheduling_element_in,
						 in, scheduling_element_id));
		break;
	case MLX5_CMD_OP_MODIFY_SCHEDULING_ELEMENT:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_SCHEDULING_ELEMENT,
					MLX5_GET(modify_scheduling_element_in,
						 in, scheduling_element_id));
		break;
	case MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT,
					MLX5_GET(add_vxlan_udp_dport_in, in,
						 vxlan_udp_port));
		break;
	case MLX5_CMD_OP_QUERY_L2_TABLE_ENTRY:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_SET_L2_TABLE_ENTRY,
					MLX5_GET(query_l2_table_entry_in, in,
						 table_index));
		break;
	case MLX5_CMD_OP_SET_L2_TABLE_ENTRY:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_SET_L2_TABLE_ENTRY,
					MLX5_GET(set_l2_table_entry_in, in,
						 table_index));
		break;
	case MLX5_CMD_OP_QUERY_QP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_QP,
					MLX5_GET(query_qp_in, in, qpn));
		break;
	case MLX5_CMD_OP_RST2INIT_QP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_QP,
					MLX5_GET(rst2init_qp_in, in, qpn));
		break;
	case MLX5_CMD_OP_INIT2INIT_QP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_QP,
					MLX5_GET(init2init_qp_in, in, qpn));
		break;
	case MLX5_CMD_OP_INIT2RTR_QP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_QP,
					MLX5_GET(init2rtr_qp_in, in, qpn));
		break;
	case MLX5_CMD_OP_RTR2RTS_QP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_QP,
					MLX5_GET(rtr2rts_qp_in, in, qpn));
		break;
	case MLX5_CMD_OP_RTS2RTS_QP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_QP,
					MLX5_GET(rts2rts_qp_in, in, qpn));
		break;
	case MLX5_CMD_OP_SQERR2RTS_QP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_QP,
					MLX5_GET(sqerr2rts_qp_in, in, qpn));
		break;
	case MLX5_CMD_OP_2ERR_QP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_QP,
					MLX5_GET(qp_2err_in, in, qpn));
		break;
	case MLX5_CMD_OP_2RST_QP:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_QP,
					MLX5_GET(qp_2rst_in, in, qpn));
		break;
	case MLX5_CMD_OP_QUERY_DCT:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_DCT,
					MLX5_GET(query_dct_in, in, dctn));
		break;
	case MLX5_CMD_OP_QUERY_XRQ:
	case MLX5_CMD_OP_QUERY_XRQ_DC_PARAMS_ENTRY:
	case MLX5_CMD_OP_QUERY_XRQ_ERROR_PARAMS:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_XRQ,
					MLX5_GET(query_xrq_in, in, xrqn));
		break;
	case MLX5_CMD_OP_QUERY_XRC_SRQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_XRC_SRQ,
					MLX5_GET(query_xrc_srq_in, in,
						 xrc_srqn));
		break;
	case MLX5_CMD_OP_ARM_XRC_SRQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_XRC_SRQ,
					MLX5_GET(arm_xrc_srq_in, in, xrc_srqn));
		break;
	case MLX5_CMD_OP_QUERY_SRQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_SRQ,
					MLX5_GET(query_srq_in, in, srqn));
		break;
	case MLX5_CMD_OP_ARM_RQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_RQ,
					MLX5_GET(arm_rq_in, in, srq_number));
		break;
	case MLX5_CMD_OP_ARM_DCT_FOR_KEY_VIOLATION:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_DCT,
					MLX5_GET(drain_dct_in, in, dctn));
		break;
	case MLX5_CMD_OP_ARM_XRQ:
	case MLX5_CMD_OP_SET_XRQ_DC_PARAMS_ENTRY:
	case MLX5_CMD_OP_RELEASE_XRQ_ERROR:
	case MLX5_CMD_OP_MODIFY_XRQ:
		obj_id = get_enc_obj_id(MLX5_CMD_OP_CREATE_XRQ,
					MLX5_GET(arm_xrq_in, in, xrqn));
		break;
	case MLX5_CMD_OP_QUERY_PACKET_REFORMAT_CONTEXT:
		obj_id = get_enc_obj_id
				(MLX5_CMD_OP_ALLOC_PACKET_REFORMAT_CONTEXT,
				 MLX5_GET(query_packet_reformat_context_in,
					  in, packet_reformat_id));
		break;
	default:
		obj_id = 0;
	}

	return obj_id;
}

static bool devx_is_valid_obj_id(struct uverbs_attr_bundle *attrs,
				 struct ib_uobject *uobj, const void *in)
{
	struct mlx5_ib_dev *dev = mlx5_udata_to_mdev(&attrs->driver_udata);
	u64 obj_id = devx_get_obj_id(in);

	if (!obj_id)
		return false;

	switch (uobj_get_object_id(uobj)) {
	case UVERBS_OBJECT_CQ:
		return get_enc_obj_id(MLX5_CMD_OP_CREATE_CQ,
				      to_mcq(uobj->object)->mcq.cqn) ==
				      obj_id;

	case UVERBS_OBJECT_SRQ:
	{
		struct mlx5_core_srq *srq = &(to_msrq(uobj->object)->msrq);
		u16 opcode;

		switch (srq->common.res) {
		case MLX5_RES_XSRQ:
			opcode = MLX5_CMD_OP_CREATE_XRC_SRQ;
			break;
		case MLX5_RES_XRQ:
			opcode = MLX5_CMD_OP_CREATE_XRQ;
			break;
		default:
			if (!dev->mdev->issi)
				opcode = MLX5_CMD_OP_CREATE_SRQ;
			else
				opcode = MLX5_CMD_OP_CREATE_RMP;
		}

		return get_enc_obj_id(opcode,
				      to_msrq(uobj->object)->msrq.srqn) ==
				      obj_id;
	}

	case UVERBS_OBJECT_QP:
	{
		struct mlx5_ib_qp *qp = to_mqp(uobj->object);

		if (qp->type == IB_QPT_RAW_PACKET ||
		    (qp->flags & IB_QP_CREATE_SOURCE_QPN)) {
			struct mlx5_ib_raw_packet_qp *raw_packet_qp =
							 &qp->raw_packet_qp;
			struct mlx5_ib_rq *rq = &raw_packet_qp->rq;
			struct mlx5_ib_sq *sq = &raw_packet_qp->sq;

			return (get_enc_obj_id(MLX5_CMD_OP_CREATE_RQ,
					       rq->base.mqp.qpn) == obj_id ||
				get_enc_obj_id(MLX5_CMD_OP_CREATE_SQ,
					       sq->base.mqp.qpn) == obj_id ||
				get_enc_obj_id(MLX5_CMD_OP_CREATE_TIR,
					       rq->tirn) == obj_id ||
				get_enc_obj_id(MLX5_CMD_OP_CREATE_TIS,
					       sq->tisn) == obj_id);
		}

		if (qp->type == MLX5_IB_QPT_DCT)
			return get_enc_obj_id(MLX5_CMD_OP_CREATE_DCT,
					      qp->dct.mdct.mqp.qpn) == obj_id;
		return get_enc_obj_id(MLX5_CMD_OP_CREATE_QP,
				      qp->ibqp.qp_num) == obj_id;
	}

	case UVERBS_OBJECT_WQ:
		return get_enc_obj_id(MLX5_CMD_OP_CREATE_RQ,
				      to_mrwq(uobj->object)->core_qp.qpn) ==
				      obj_id;

	case UVERBS_OBJECT_RWQ_IND_TBL:
		return get_enc_obj_id(MLX5_CMD_OP_CREATE_RQT,
				      to_mrwq_ind_table(uobj->object)->rqtn) ==
				      obj_id;

	case MLX5_IB_OBJECT_DEVX_OBJ:
	{
		u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);
		struct devx_obj *devx_uobj = uobj->object;

		if (opcode == MLX5_CMD_OP_QUERY_FLOW_COUNTER &&
		    devx_uobj->flow_counter_bulk_size) {
			u64 end;

			end = devx_uobj->obj_id +
				devx_uobj->flow_counter_bulk_size;
			return devx_uobj->obj_id <= obj_id && end > obj_id;
		}

		return devx_uobj->obj_id == obj_id;
	}

	default:
		return false;
	}
}

static void devx_set_umem_valid(const void *in)
{
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);

	switch (opcode) {
	case MLX5_CMD_OP_CREATE_MKEY:
		MLX5_SET(create_mkey_in, in, mkey_umem_valid, 1);
		break;
	case MLX5_CMD_OP_CREATE_CQ:
	{
		void *cqc;

		MLX5_SET(create_cq_in, in, cq_umem_valid, 1);
		cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);
		MLX5_SET(cqc, cqc, dbr_umem_valid, 1);
		break;
	}
	case MLX5_CMD_OP_CREATE_QP:
	{
		void *qpc;

		qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);
		MLX5_SET(qpc, qpc, dbr_umem_valid, 1);
		MLX5_SET(create_qp_in, in, wq_umem_valid, 1);
		break;
	}

	case MLX5_CMD_OP_CREATE_RQ:
	{
		void *rqc, *wq;

		rqc = MLX5_ADDR_OF(create_rq_in, in, ctx);
		wq  = MLX5_ADDR_OF(rqc, rqc, wq);
		MLX5_SET(wq, wq, dbr_umem_valid, 1);
		MLX5_SET(wq, wq, wq_umem_valid, 1);
		break;
	}

	case MLX5_CMD_OP_CREATE_SQ:
	{
		void *sqc, *wq;

		sqc = MLX5_ADDR_OF(create_sq_in, in, ctx);
		wq = MLX5_ADDR_OF(sqc, sqc, wq);
		MLX5_SET(wq, wq, dbr_umem_valid, 1);
		MLX5_SET(wq, wq, wq_umem_valid, 1);
		break;
	}

	case MLX5_CMD_OP_MODIFY_CQ:
		MLX5_SET(modify_cq_in, in, cq_umem_valid, 1);
		break;

	case MLX5_CMD_OP_CREATE_RMP:
	{
		void *rmpc, *wq;

		rmpc = MLX5_ADDR_OF(create_rmp_in, in, ctx);
		wq = MLX5_ADDR_OF(rmpc, rmpc, wq);
		MLX5_SET(wq, wq, dbr_umem_valid, 1);
		MLX5_SET(wq, wq, wq_umem_valid, 1);
		break;
	}

	case MLX5_CMD_OP_CREATE_XRQ:
	{
		void *xrqc, *wq;

		xrqc = MLX5_ADDR_OF(create_xrq_in, in, xrq_context);
		wq = MLX5_ADDR_OF(xrqc, xrqc, wq);
		MLX5_SET(wq, wq, dbr_umem_valid, 1);
		MLX5_SET(wq, wq, wq_umem_valid, 1);
		break;
	}

	case MLX5_CMD_OP_CREATE_XRC_SRQ:
	{
		void *xrc_srqc;

		MLX5_SET(create_xrc_srq_in, in, xrc_srq_umem_valid, 1);
		xrc_srqc = MLX5_ADDR_OF(create_xrc_srq_in, in,
					xrc_srq_context_entry);
		MLX5_SET(xrc_srqc, xrc_srqc, dbr_umem_valid, 1);
		break;
	}

	default:
		return;
	}
}

static bool devx_is_obj_create_cmd(const void *in, u16 *opcode)
{
	*opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);

	switch (*opcode) {
	case MLX5_CMD_OP_CREATE_GENERAL_OBJECT:
	case MLX5_CMD_OP_CREATE_MKEY:
	case MLX5_CMD_OP_CREATE_CQ:
	case MLX5_CMD_OP_ALLOC_PD:
	case MLX5_CMD_OP_ALLOC_TRANSPORT_DOMAIN:
	case MLX5_CMD_OP_CREATE_RMP:
	case MLX5_CMD_OP_CREATE_SQ:
	case MLX5_CMD_OP_CREATE_RQ:
	case MLX5_CMD_OP_CREATE_RQT:
	case MLX5_CMD_OP_CREATE_TIR:
	case MLX5_CMD_OP_CREATE_TIS:
	case MLX5_CMD_OP_ALLOC_Q_COUNTER:
	case MLX5_CMD_OP_CREATE_FLOW_TABLE:
	case MLX5_CMD_OP_CREATE_FLOW_GROUP:
	case MLX5_CMD_OP_ALLOC_FLOW_COUNTER:
	case MLX5_CMD_OP_ALLOC_PACKET_REFORMAT_CONTEXT:
	case MLX5_CMD_OP_ALLOC_MODIFY_HEADER_CONTEXT:
	case MLX5_CMD_OP_CREATE_SCHEDULING_ELEMENT:
	case MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT:
	case MLX5_CMD_OP_SET_L2_TABLE_ENTRY:
	case MLX5_CMD_OP_CREATE_QP:
	case MLX5_CMD_OP_CREATE_SRQ:
	case MLX5_CMD_OP_CREATE_XRC_SRQ:
	case MLX5_CMD_OP_CREATE_DCT:
	case MLX5_CMD_OP_CREATE_XRQ:
	case MLX5_CMD_OP_ATTACH_TO_MCG:
	case MLX5_CMD_OP_ALLOC_XRCD:
		return true;
	case MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY:
	{
		u16 op_mod = MLX5_GET(set_fte_in, in, op_mod);
		if (op_mod == 0)
			return true;
		return false;
	}
	case MLX5_CMD_OP_CREATE_PSV:
	{
		u8 num_psv = MLX5_GET(create_psv_in, in, num_psv);

		if (num_psv == 1)
			return true;
		return false;
	}
	default:
		return false;
	}
}

static bool devx_is_obj_modify_cmd(const void *in)
{
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);

	switch (opcode) {
	case MLX5_CMD_OP_MODIFY_GENERAL_OBJECT:
	case MLX5_CMD_OP_MODIFY_CQ:
	case MLX5_CMD_OP_MODIFY_RMP:
	case MLX5_CMD_OP_MODIFY_SQ:
	case MLX5_CMD_OP_MODIFY_RQ:
	case MLX5_CMD_OP_MODIFY_RQT:
	case MLX5_CMD_OP_MODIFY_TIR:
	case MLX5_CMD_OP_MODIFY_TIS:
	case MLX5_CMD_OP_MODIFY_FLOW_TABLE:
	case MLX5_CMD_OP_MODIFY_SCHEDULING_ELEMENT:
	case MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT:
	case MLX5_CMD_OP_SET_L2_TABLE_ENTRY:
	case MLX5_CMD_OP_RST2INIT_QP:
	case MLX5_CMD_OP_INIT2RTR_QP:
	case MLX5_CMD_OP_INIT2INIT_QP:
	case MLX5_CMD_OP_RTR2RTS_QP:
	case MLX5_CMD_OP_RTS2RTS_QP:
	case MLX5_CMD_OP_SQERR2RTS_QP:
	case MLX5_CMD_OP_2ERR_QP:
	case MLX5_CMD_OP_2RST_QP:
	case MLX5_CMD_OP_ARM_XRC_SRQ:
	case MLX5_CMD_OP_ARM_RQ:
	case MLX5_CMD_OP_ARM_DCT_FOR_KEY_VIOLATION:
	case MLX5_CMD_OP_ARM_XRQ:
	case MLX5_CMD_OP_SET_XRQ_DC_PARAMS_ENTRY:
	case MLX5_CMD_OP_RELEASE_XRQ_ERROR:
	case MLX5_CMD_OP_MODIFY_XRQ:
		return true;
	case MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY:
	{
		u16 op_mod = MLX5_GET(set_fte_in, in, op_mod);

		if (op_mod == 1)
			return true;
		return false;
	}
	default:
		return false;
	}
}

static bool devx_is_obj_query_cmd(const void *in)
{
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);

	switch (opcode) {
	case MLX5_CMD_OP_QUERY_GENERAL_OBJECT:
	case MLX5_CMD_OP_QUERY_MKEY:
	case MLX5_CMD_OP_QUERY_CQ:
	case MLX5_CMD_OP_QUERY_RMP:
	case MLX5_CMD_OP_QUERY_SQ:
	case MLX5_CMD_OP_QUERY_RQ:
	case MLX5_CMD_OP_QUERY_RQT:
	case MLX5_CMD_OP_QUERY_TIR:
	case MLX5_CMD_OP_QUERY_TIS:
	case MLX5_CMD_OP_QUERY_Q_COUNTER:
	case MLX5_CMD_OP_QUERY_FLOW_TABLE:
	case MLX5_CMD_OP_QUERY_FLOW_GROUP:
	case MLX5_CMD_OP_QUERY_FLOW_TABLE_ENTRY:
	case MLX5_CMD_OP_QUERY_FLOW_COUNTER:
	case MLX5_CMD_OP_QUERY_MODIFY_HEADER_CONTEXT:
	case MLX5_CMD_OP_QUERY_SCHEDULING_ELEMENT:
	case MLX5_CMD_OP_QUERY_L2_TABLE_ENTRY:
	case MLX5_CMD_OP_QUERY_QP:
	case MLX5_CMD_OP_QUERY_SRQ:
	case MLX5_CMD_OP_QUERY_XRC_SRQ:
	case MLX5_CMD_OP_QUERY_DCT:
	case MLX5_CMD_OP_QUERY_XRQ:
	case MLX5_CMD_OP_QUERY_XRQ_DC_PARAMS_ENTRY:
	case MLX5_CMD_OP_QUERY_XRQ_ERROR_PARAMS:
	case MLX5_CMD_OP_QUERY_PACKET_REFORMAT_CONTEXT:
		return true;
	default:
		return false;
	}
}

static bool devx_is_whitelist_cmd(void *in)
{
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);

	switch (opcode) {
	case MLX5_CMD_OP_QUERY_HCA_CAP:
	case MLX5_CMD_OP_QUERY_HCA_VPORT_CONTEXT:
	case MLX5_CMD_OP_QUERY_ESW_VPORT_CONTEXT:
	case MLX5_CMD_OP_QUERY_ESW_FUNCTIONS:
		return true;
	default:
		return false;
	}
}

static int devx_get_uid(struct mlx5_ib_ucontext *c, void *cmd_in)
{
	if (devx_is_whitelist_cmd(cmd_in)) {
		struct mlx5_ib_dev *dev;

		if (c->devx_uid)
			return c->devx_uid;

		dev = to_mdev(c->ibucontext.device);
		if (dev->devx_whitelist_uid)
			return dev->devx_whitelist_uid;

		return -EOPNOTSUPP;
	}

	if (!c->devx_uid)
		return -EINVAL;

	return c->devx_uid;
}

static bool devx_is_general_cmd(void *in, struct mlx5_ib_dev *dev)
{
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);

	/* Pass all cmds for vhca_tunnel as general, tracking is done in FW */
	if ((MLX5_CAP_GEN_64(dev->mdev, vhca_tunnel_commands) &&
	     MLX5_GET(general_obj_in_cmd_hdr, in, vhca_tunnel_id)) ||
	    (opcode >= MLX5_CMD_OP_GENERAL_START &&
	     opcode < MLX5_CMD_OP_GENERAL_END))
		return true;

	switch (opcode) {
	case MLX5_CMD_OP_QUERY_HCA_CAP:
	case MLX5_CMD_OP_QUERY_HCA_VPORT_CONTEXT:
	case MLX5_CMD_OP_QUERY_ESW_VPORT_CONTEXT:
	case MLX5_CMD_OP_QUERY_VPORT_STATE:
	case MLX5_CMD_OP_QUERY_ADAPTER:
	case MLX5_CMD_OP_QUERY_ISSI:
	case MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT:
	case MLX5_CMD_OP_QUERY_ROCE_ADDRESS:
	case MLX5_CMD_OP_QUERY_VNIC_ENV:
	case MLX5_CMD_OP_QUERY_VPORT_COUNTER:
	case MLX5_CMD_OP_GET_DROPPED_PACKET_LOG:
	case MLX5_CMD_OP_NOP:
	case MLX5_CMD_OP_QUERY_CONG_STATUS:
	case MLX5_CMD_OP_QUERY_CONG_PARAMS:
	case MLX5_CMD_OP_QUERY_CONG_STATISTICS:
	case MLX5_CMD_OP_QUERY_LAG:
	case MLX5_CMD_OP_QUERY_ESW_FUNCTIONS:
		return true;
	default:
		return false;
	}
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_QUERY_EQN)(
	struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_ucontext *c;
	struct mlx5_ib_dev *dev;
	int user_vector;
	int dev_eqn;
	int err;

	if (uverbs_copy_from(&user_vector, attrs,
			     MLX5_IB_ATTR_DEVX_QUERY_EQN_USER_VEC))
		return -EFAULT;

	c = devx_ufile2uctx(attrs);
	if (IS_ERR(c))
		return PTR_ERR(c);
	dev = to_mdev(c->ibucontext.device);

	err = mlx5_comp_eqn_get(dev->mdev, user_vector, &dev_eqn);
	if (err < 0)
		return err;

	if (uverbs_copy_to(attrs, MLX5_IB_ATTR_DEVX_QUERY_EQN_DEV_EQN,
			   &dev_eqn, sizeof(dev_eqn)))
		return -EFAULT;

	return 0;
}

/*
 *Security note:
 * The hardware protection mechanism works like this: Each device object that
 * is subject to UAR doorbells (QP/SQ/CQ) gets a UAR ID (called uar_page in
 * the device specification manual) upon its creation. Then upon doorbell,
 * hardware fetches the object context for which the doorbell was rang, and
 * validates that the UAR through which the DB was rang matches the UAR ID
 * of the object.
 * If no match the doorbell is silently ignored by the hardware. Of course,
 * the user cannot ring a doorbell on a UAR that was not mapped to it.
 * Now in devx, as the devx kernel does not manipulate the QP/SQ/CQ command
 * mailboxes (except tagging them with UID), we expose to the user its UAR
 * ID, so it can embed it in these objects in the expected specification
 * format. So the only thing the user can do is hurt itself by creating a
 * QP/SQ/CQ with a UAR ID other than his, and then in this case other users
 * may ring a doorbell on its objects.
 * The consequence of that will be that another user can schedule a QP/SQ
 * of the buggy user for execution (just insert it to the hardware schedule
 * queue or arm its CQ for event generation), no further harm is expected.
 */
static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_QUERY_UAR)(
	struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_ucontext *c;
	struct mlx5_ib_dev *dev;
	u32 user_idx;
	s32 dev_idx;

	c = devx_ufile2uctx(attrs);
	if (IS_ERR(c))
		return PTR_ERR(c);
	dev = to_mdev(c->ibucontext.device);

	if (uverbs_copy_from(&user_idx, attrs,
			     MLX5_IB_ATTR_DEVX_QUERY_UAR_USER_IDX))
		return -EFAULT;

	dev_idx = bfregn_to_uar_index(dev, &c->bfregi, user_idx, true);
	if (dev_idx < 0)
		return dev_idx;

	if (uverbs_copy_to(attrs, MLX5_IB_ATTR_DEVX_QUERY_UAR_DEV_IDX,
			   &dev_idx, sizeof(dev_idx)))
		return -EFAULT;

	return 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_OTHER)(
	struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_ucontext *c;
	struct mlx5_ib_dev *dev;
	void *cmd_in = uverbs_attr_get_alloced_ptr(
		attrs, MLX5_IB_ATTR_DEVX_OTHER_CMD_IN);
	int cmd_out_len = uverbs_attr_get_len(attrs,
					MLX5_IB_ATTR_DEVX_OTHER_CMD_OUT);
	void *cmd_out;
	int err, err2;
	int uid;

	c = devx_ufile2uctx(attrs);
	if (IS_ERR(c))
		return PTR_ERR(c);
	dev = to_mdev(c->ibucontext.device);

	uid = devx_get_uid(c, cmd_in);
	if (uid < 0)
		return uid;

	/* Only white list of some general HCA commands are allowed for this method. */
	if (!devx_is_general_cmd(cmd_in, dev))
		return -EINVAL;

	cmd_out = uverbs_zalloc(attrs, cmd_out_len);
	if (IS_ERR(cmd_out))
		return PTR_ERR(cmd_out);

	MLX5_SET(general_obj_in_cmd_hdr, cmd_in, uid, uid);
	err = mlx5_cmd_do(dev->mdev, cmd_in,
			  uverbs_attr_get_len(attrs, MLX5_IB_ATTR_DEVX_OTHER_CMD_IN),
			  cmd_out, cmd_out_len);
	if (err && err != -EREMOTEIO)
		return err;

	err2 = uverbs_copy_to(attrs, MLX5_IB_ATTR_DEVX_OTHER_CMD_OUT, cmd_out,
			      cmd_out_len);

	return err2 ?: err;
}

static void devx_obj_build_destroy_cmd(void *in, void *out, void *din,
				       u32 *dinlen,
				       u32 *obj_id)
{
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);
	u16 uid = MLX5_GET(general_obj_in_cmd_hdr, in, uid);

	*obj_id = devx_get_created_obj_id(in, out, opcode);
	*dinlen = MLX5_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	MLX5_SET(general_obj_in_cmd_hdr, din, uid, uid);

	switch (opcode) {
	case MLX5_CMD_OP_CREATE_GENERAL_OBJECT:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
		MLX5_SET(general_obj_in_cmd_hdr, din, obj_id, *obj_id);
		MLX5_SET(general_obj_in_cmd_hdr, din, obj_type,
			 MLX5_GET(general_obj_in_cmd_hdr, in, obj_type));
		break;

	case MLX5_CMD_OP_CREATE_UMEM:
		MLX5_SET(destroy_umem_in, din, opcode,
			 MLX5_CMD_OP_DESTROY_UMEM);
		MLX5_SET(destroy_umem_in, din, umem_id, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_MKEY:
		MLX5_SET(destroy_mkey_in, din, opcode,
			 MLX5_CMD_OP_DESTROY_MKEY);
		MLX5_SET(destroy_mkey_in, din, mkey_index, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_CQ:
		MLX5_SET(destroy_cq_in, din, opcode, MLX5_CMD_OP_DESTROY_CQ);
		MLX5_SET(destroy_cq_in, din, cqn, *obj_id);
		break;
	case MLX5_CMD_OP_ALLOC_PD:
		MLX5_SET(dealloc_pd_in, din, opcode, MLX5_CMD_OP_DEALLOC_PD);
		MLX5_SET(dealloc_pd_in, din, pd, *obj_id);
		break;
	case MLX5_CMD_OP_ALLOC_TRANSPORT_DOMAIN:
		MLX5_SET(dealloc_transport_domain_in, din, opcode,
			 MLX5_CMD_OP_DEALLOC_TRANSPORT_DOMAIN);
		MLX5_SET(dealloc_transport_domain_in, din, transport_domain,
			 *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_RMP:
		MLX5_SET(destroy_rmp_in, din, opcode, MLX5_CMD_OP_DESTROY_RMP);
		MLX5_SET(destroy_rmp_in, din, rmpn, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_SQ:
		MLX5_SET(destroy_sq_in, din, opcode, MLX5_CMD_OP_DESTROY_SQ);
		MLX5_SET(destroy_sq_in, din, sqn, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_RQ:
		MLX5_SET(destroy_rq_in, din, opcode, MLX5_CMD_OP_DESTROY_RQ);
		MLX5_SET(destroy_rq_in, din, rqn, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_RQT:
		MLX5_SET(destroy_rqt_in, din, opcode, MLX5_CMD_OP_DESTROY_RQT);
		MLX5_SET(destroy_rqt_in, din, rqtn, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_TIR:
		MLX5_SET(destroy_tir_in, din, opcode, MLX5_CMD_OP_DESTROY_TIR);
		MLX5_SET(destroy_tir_in, din, tirn, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_TIS:
		MLX5_SET(destroy_tis_in, din, opcode, MLX5_CMD_OP_DESTROY_TIS);
		MLX5_SET(destroy_tis_in, din, tisn, *obj_id);
		break;
	case MLX5_CMD_OP_ALLOC_Q_COUNTER:
		MLX5_SET(dealloc_q_counter_in, din, opcode,
			 MLX5_CMD_OP_DEALLOC_Q_COUNTER);
		MLX5_SET(dealloc_q_counter_in, din, counter_set_id, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_FLOW_TABLE:
		*dinlen = MLX5_ST_SZ_BYTES(destroy_flow_table_in);
		MLX5_SET(destroy_flow_table_in, din, other_vport,
			 MLX5_GET(create_flow_table_in,  in, other_vport));
		MLX5_SET(destroy_flow_table_in, din, vport_number,
			 MLX5_GET(create_flow_table_in,  in, vport_number));
		MLX5_SET(destroy_flow_table_in, din, table_type,
			 MLX5_GET(create_flow_table_in,  in, table_type));
		MLX5_SET(destroy_flow_table_in, din, table_id, *obj_id);
		MLX5_SET(destroy_flow_table_in, din, opcode,
			 MLX5_CMD_OP_DESTROY_FLOW_TABLE);
		break;
	case MLX5_CMD_OP_CREATE_FLOW_GROUP:
		*dinlen = MLX5_ST_SZ_BYTES(destroy_flow_group_in);
		MLX5_SET(destroy_flow_group_in, din, other_vport,
			 MLX5_GET(create_flow_group_in, in, other_vport));
		MLX5_SET(destroy_flow_group_in, din, vport_number,
			 MLX5_GET(create_flow_group_in, in, vport_number));
		MLX5_SET(destroy_flow_group_in, din, table_type,
			 MLX5_GET(create_flow_group_in, in, table_type));
		MLX5_SET(destroy_flow_group_in, din, table_id,
			 MLX5_GET(create_flow_group_in, in, table_id));
		MLX5_SET(destroy_flow_group_in, din, group_id, *obj_id);
		MLX5_SET(destroy_flow_group_in, din, opcode,
			 MLX5_CMD_OP_DESTROY_FLOW_GROUP);
		break;
	case MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY:
		*dinlen = MLX5_ST_SZ_BYTES(delete_fte_in);
		MLX5_SET(delete_fte_in, din, other_vport,
			 MLX5_GET(set_fte_in,  in, other_vport));
		MLX5_SET(delete_fte_in, din, vport_number,
			 MLX5_GET(set_fte_in, in, vport_number));
		MLX5_SET(delete_fte_in, din, table_type,
			 MLX5_GET(set_fte_in, in, table_type));
		MLX5_SET(delete_fte_in, din, table_id,
			 MLX5_GET(set_fte_in, in, table_id));
		MLX5_SET(delete_fte_in, din, flow_index, *obj_id);
		MLX5_SET(delete_fte_in, din, opcode,
			 MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY);
		break;
	case MLX5_CMD_OP_ALLOC_FLOW_COUNTER:
		MLX5_SET(dealloc_flow_counter_in, din, opcode,
			 MLX5_CMD_OP_DEALLOC_FLOW_COUNTER);
		MLX5_SET(dealloc_flow_counter_in, din, flow_counter_id,
			 *obj_id);
		break;
	case MLX5_CMD_OP_ALLOC_PACKET_REFORMAT_CONTEXT:
		MLX5_SET(dealloc_packet_reformat_context_in, din, opcode,
			 MLX5_CMD_OP_DEALLOC_PACKET_REFORMAT_CONTEXT);
		MLX5_SET(dealloc_packet_reformat_context_in, din,
			 packet_reformat_id, *obj_id);
		break;
	case MLX5_CMD_OP_ALLOC_MODIFY_HEADER_CONTEXT:
		MLX5_SET(dealloc_modify_header_context_in, din, opcode,
			 MLX5_CMD_OP_DEALLOC_MODIFY_HEADER_CONTEXT);
		MLX5_SET(dealloc_modify_header_context_in, din,
			 modify_header_id, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_SCHEDULING_ELEMENT:
		*dinlen = MLX5_ST_SZ_BYTES(destroy_scheduling_element_in);
		MLX5_SET(destroy_scheduling_element_in, din,
			 scheduling_hierarchy,
			 MLX5_GET(create_scheduling_element_in, in,
				  scheduling_hierarchy));
		MLX5_SET(destroy_scheduling_element_in, din,
			 scheduling_element_id, *obj_id);
		MLX5_SET(destroy_scheduling_element_in, din, opcode,
			 MLX5_CMD_OP_DESTROY_SCHEDULING_ELEMENT);
		break;
	case MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT:
		*dinlen = MLX5_ST_SZ_BYTES(delete_vxlan_udp_dport_in);
		MLX5_SET(delete_vxlan_udp_dport_in, din, vxlan_udp_port, *obj_id);
		MLX5_SET(delete_vxlan_udp_dport_in, din, opcode,
			 MLX5_CMD_OP_DELETE_VXLAN_UDP_DPORT);
		break;
	case MLX5_CMD_OP_SET_L2_TABLE_ENTRY:
		*dinlen = MLX5_ST_SZ_BYTES(delete_l2_table_entry_in);
		MLX5_SET(delete_l2_table_entry_in, din, table_index, *obj_id);
		MLX5_SET(delete_l2_table_entry_in, din, opcode,
			 MLX5_CMD_OP_DELETE_L2_TABLE_ENTRY);
		break;
	case MLX5_CMD_OP_CREATE_QP:
		MLX5_SET(destroy_qp_in, din, opcode, MLX5_CMD_OP_DESTROY_QP);
		MLX5_SET(destroy_qp_in, din, qpn, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_SRQ:
		MLX5_SET(destroy_srq_in, din, opcode, MLX5_CMD_OP_DESTROY_SRQ);
		MLX5_SET(destroy_srq_in, din, srqn, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_XRC_SRQ:
		MLX5_SET(destroy_xrc_srq_in, din, opcode,
			 MLX5_CMD_OP_DESTROY_XRC_SRQ);
		MLX5_SET(destroy_xrc_srq_in, din, xrc_srqn, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_DCT:
		MLX5_SET(destroy_dct_in, din, opcode, MLX5_CMD_OP_DESTROY_DCT);
		MLX5_SET(destroy_dct_in, din, dctn, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_XRQ:
		MLX5_SET(destroy_xrq_in, din, opcode, MLX5_CMD_OP_DESTROY_XRQ);
		MLX5_SET(destroy_xrq_in, din, xrqn, *obj_id);
		break;
	case MLX5_CMD_OP_ATTACH_TO_MCG:
		*dinlen = MLX5_ST_SZ_BYTES(detach_from_mcg_in);
		MLX5_SET(detach_from_mcg_in, din, qpn,
			 MLX5_GET(attach_to_mcg_in, in, qpn));
		memcpy(MLX5_ADDR_OF(detach_from_mcg_in, din, multicast_gid),
		       MLX5_ADDR_OF(attach_to_mcg_in, in, multicast_gid),
		       MLX5_FLD_SZ_BYTES(attach_to_mcg_in, multicast_gid));
		MLX5_SET(detach_from_mcg_in, din, opcode,
			 MLX5_CMD_OP_DETACH_FROM_MCG);
		MLX5_SET(detach_from_mcg_in, din, qpn, *obj_id);
		break;
	case MLX5_CMD_OP_ALLOC_XRCD:
		MLX5_SET(dealloc_xrcd_in, din, opcode,
			 MLX5_CMD_OP_DEALLOC_XRCD);
		MLX5_SET(dealloc_xrcd_in, din, xrcd, *obj_id);
		break;
	case MLX5_CMD_OP_CREATE_PSV:
		MLX5_SET(destroy_psv_in, din, opcode,
			 MLX5_CMD_OP_DESTROY_PSV);
		MLX5_SET(destroy_psv_in, din, psvn, *obj_id);
		break;
	default:
		/* The entry must match to one of the devx_is_obj_create_cmd */
		WARN_ON(true);
		break;
	}
}

static int devx_handle_mkey_indirect(struct devx_obj *obj,
				     struct mlx5_ib_dev *dev,
				     void *in, void *out)
{
	struct mlx5_ib_mkey *mkey = &obj->mkey;
	void *mkc;
	u8 key;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	key = MLX5_GET(mkc, mkc, mkey_7_0);
	mkey->key = mlx5_idx_to_mkey(
			MLX5_GET(create_mkey_out, out, mkey_index)) | key;
	mkey->type = MLX5_MKEY_INDIRECT_DEVX;
	mkey->ndescs = MLX5_GET(mkc, mkc, translations_octword_size);
	init_waitqueue_head(&mkey->wait);

	return mlx5r_store_odp_mkey(dev, mkey);
}

static int devx_handle_mkey_create(struct mlx5_ib_dev *dev,
				   struct devx_obj *obj,
				   void *in, int in_len)
{
	int min_len = MLX5_BYTE_OFF(create_mkey_in, memory_key_mkey_entry) +
			MLX5_FLD_SZ_BYTES(create_mkey_in,
			memory_key_mkey_entry);
	void *mkc;
	u8 access_mode;

	if (in_len < min_len)
		return -EINVAL;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	access_mode = MLX5_GET(mkc, mkc, access_mode_1_0);
	access_mode |= MLX5_GET(mkc, mkc, access_mode_4_2) << 2;

	if (access_mode == MLX5_MKC_ACCESS_MODE_KLMS ||
		access_mode == MLX5_MKC_ACCESS_MODE_KSM) {
		if (IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING))
			obj->flags |= DEVX_OBJ_FLAGS_INDIRECT_MKEY;
		return 0;
	}

	MLX5_SET(create_mkey_in, in, mkey_umem_valid, 1);
	return 0;
}

static void devx_cleanup_subscription(struct mlx5_ib_dev *dev,
				      struct devx_event_subscription *sub)
{
	struct devx_event *event;
	struct devx_obj_event *xa_val_level2;

	if (sub->is_cleaned)
		return;

	sub->is_cleaned = 1;
	list_del_rcu(&sub->xa_list);

	if (list_empty(&sub->obj_list))
		return;

	list_del_rcu(&sub->obj_list);
	/* check whether key level 1 for this obj_sub_list is empty */
	event = xa_load(&dev->devx_event_table.event_xa,
			sub->xa_key_level1);
	WARN_ON(!event);

	xa_val_level2 = xa_load(&event->object_ids, sub->xa_key_level2);
	if (list_empty(&xa_val_level2->obj_sub_list)) {
		xa_erase(&event->object_ids,
			 sub->xa_key_level2);
		kfree_rcu(xa_val_level2, rcu);
	}
}

static int devx_obj_cleanup(struct ib_uobject *uobject,
			    enum rdma_remove_reason why,
			    struct uverbs_attr_bundle *attrs)
{
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	struct mlx5_devx_event_table *devx_event_table;
	struct devx_obj *obj = uobject->object;
	struct devx_event_subscription *sub_entry, *tmp;
	struct mlx5_ib_dev *dev;
	int ret;

	dev = mlx5_udata_to_mdev(&attrs->driver_udata);
	if (obj->flags & DEVX_OBJ_FLAGS_INDIRECT_MKEY &&
	    xa_erase(&obj->ib_dev->odp_mkeys,
		     mlx5_base_mkey(obj->mkey.key)))
		/*
		 * The pagefault_single_data_segment() does commands against
		 * the mmkey, we must wait for that to stop before freeing the
		 * mkey, as another allocation could get the same mkey #.
		 */
		mlx5r_deref_wait_odp_mkey(&obj->mkey);

	if (obj->flags & DEVX_OBJ_FLAGS_DCT)
		ret = mlx5_core_destroy_dct(obj->ib_dev, &obj->core_dct);
	else if (obj->flags & DEVX_OBJ_FLAGS_CQ)
		ret = mlx5_core_destroy_cq(obj->ib_dev->mdev, &obj->core_cq);
	else
		ret = mlx5_cmd_exec(obj->ib_dev->mdev, obj->dinbox,
				    obj->dinlen, out, sizeof(out));
	if (ret)
		return ret;

	devx_event_table = &dev->devx_event_table;

	mutex_lock(&devx_event_table->event_xa_lock);
	list_for_each_entry_safe(sub_entry, tmp, &obj->event_sub, obj_list)
		devx_cleanup_subscription(dev, sub_entry);
	mutex_unlock(&devx_event_table->event_xa_lock);

	kfree(obj);
	return ret;
}

static void devx_cq_comp(struct mlx5_core_cq *mcq, struct mlx5_eqe *eqe)
{
	struct devx_obj *obj = container_of(mcq, struct devx_obj, core_cq);
	struct mlx5_devx_event_table *table;
	struct devx_event *event;
	struct devx_obj_event *obj_event;
	u32 obj_id = mcq->cqn;

	table = &obj->ib_dev->devx_event_table;
	rcu_read_lock();
	event = xa_load(&table->event_xa, MLX5_EVENT_TYPE_COMP);
	if (!event)
		goto out;

	obj_event = xa_load(&event->object_ids, obj_id);
	if (!obj_event)
		goto out;

	dispatch_event_fd(&obj_event->obj_sub_list, eqe);
out:
	rcu_read_unlock();
}

static bool is_apu_cq(struct mlx5_ib_dev *dev, const void *in)
{
	if (!MLX5_CAP_GEN(dev->mdev, apu) ||
	    !MLX5_GET(cqc, MLX5_ADDR_OF(create_cq_in, in, cq_context), apu_cq))
		return false;

	return true;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_OBJ_CREATE)(
	struct uverbs_attr_bundle *attrs)
{
	void *cmd_in = uverbs_attr_get_alloced_ptr(attrs, MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_IN);
	int cmd_out_len =  uverbs_attr_get_len(attrs,
					MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_OUT);
	int cmd_in_len = uverbs_attr_get_len(attrs,
					MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_IN);
	void *cmd_out;
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_DEVX_OBJ_CREATE_HANDLE);
	struct mlx5_ib_ucontext *c = rdma_udata_to_drv_context(
		&attrs->driver_udata, struct mlx5_ib_ucontext, ibucontext);
	struct mlx5_ib_dev *dev = to_mdev(c->ibucontext.device);
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	struct devx_obj *obj;
	u16 obj_type = 0;
	int err, err2 = 0;
	int uid;
	u32 obj_id;
	u16 opcode;

	if (MLX5_GET(general_obj_in_cmd_hdr, cmd_in, vhca_tunnel_id))
		return -EINVAL;

	uid = devx_get_uid(c, cmd_in);
	if (uid < 0)
		return uid;

	if (!devx_is_obj_create_cmd(cmd_in, &opcode))
		return -EINVAL;

	cmd_out = uverbs_zalloc(attrs, cmd_out_len);
	if (IS_ERR(cmd_out))
		return PTR_ERR(cmd_out);

	obj = kzalloc(sizeof(struct devx_obj), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	MLX5_SET(general_obj_in_cmd_hdr, cmd_in, uid, uid);
	if (opcode == MLX5_CMD_OP_CREATE_MKEY) {
		err = devx_handle_mkey_create(dev, obj, cmd_in, cmd_in_len);
		if (err)
			goto obj_free;
	} else {
		devx_set_umem_valid(cmd_in);
	}

	if (opcode == MLX5_CMD_OP_CREATE_DCT) {
		obj->flags |= DEVX_OBJ_FLAGS_DCT;
		err = mlx5_core_create_dct(dev, &obj->core_dct, cmd_in,
					   cmd_in_len, cmd_out, cmd_out_len);
	} else if (opcode == MLX5_CMD_OP_CREATE_CQ &&
		   !is_apu_cq(dev, cmd_in)) {
		obj->flags |= DEVX_OBJ_FLAGS_CQ;
		obj->core_cq.comp = devx_cq_comp;
		err = mlx5_create_cq(dev->mdev, &obj->core_cq,
				     cmd_in, cmd_in_len, cmd_out,
				     cmd_out_len);
	} else {
		err = mlx5_cmd_do(dev->mdev, cmd_in, cmd_in_len,
				  cmd_out, cmd_out_len);
	}

	if (err == -EREMOTEIO)
		err2 = uverbs_copy_to(attrs,
				      MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_OUT,
				      cmd_out, cmd_out_len);
	if (err)
		goto obj_free;

	if (opcode == MLX5_CMD_OP_ALLOC_FLOW_COUNTER) {
		u32 bulk = MLX5_GET(alloc_flow_counter_in,
				    cmd_in,
				    flow_counter_bulk_log_size);

		if (bulk)
			bulk = 1 << bulk;
		else
			bulk = 128UL * MLX5_GET(alloc_flow_counter_in,
						cmd_in,
						flow_counter_bulk);
		obj->flow_counter_bulk_size = bulk;
	}

	uobj->object = obj;
	INIT_LIST_HEAD(&obj->event_sub);
	obj->ib_dev = dev;
	devx_obj_build_destroy_cmd(cmd_in, cmd_out, obj->dinbox, &obj->dinlen,
				   &obj_id);
	WARN_ON(obj->dinlen > MLX5_MAX_DESTROY_INBOX_SIZE_DW * sizeof(u32));

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_OUT, cmd_out, cmd_out_len);
	if (err)
		goto obj_destroy;

	if (opcode == MLX5_CMD_OP_CREATE_GENERAL_OBJECT)
		obj_type = MLX5_GET(general_obj_in_cmd_hdr, cmd_in, obj_type);
	obj->obj_id = get_enc_obj_id(opcode | obj_type << 16, obj_id);

	if (obj->flags & DEVX_OBJ_FLAGS_INDIRECT_MKEY) {
		err = devx_handle_mkey_indirect(obj, dev, cmd_in, cmd_out);
		if (err)
			goto obj_destroy;
	}
	return 0;

obj_destroy:
	if (obj->flags & DEVX_OBJ_FLAGS_DCT)
		mlx5_core_destroy_dct(obj->ib_dev, &obj->core_dct);
	else if (obj->flags & DEVX_OBJ_FLAGS_CQ)
		mlx5_core_destroy_cq(obj->ib_dev->mdev, &obj->core_cq);
	else
		mlx5_cmd_exec(obj->ib_dev->mdev, obj->dinbox, obj->dinlen, out,
			      sizeof(out));
obj_free:
	kfree(obj);
	return err2 ?: err;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_OBJ_MODIFY)(
	struct uverbs_attr_bundle *attrs)
{
	void *cmd_in = uverbs_attr_get_alloced_ptr(attrs, MLX5_IB_ATTR_DEVX_OBJ_MODIFY_CMD_IN);
	int cmd_out_len = uverbs_attr_get_len(attrs,
					MLX5_IB_ATTR_DEVX_OBJ_MODIFY_CMD_OUT);
	struct ib_uobject *uobj = uverbs_attr_get_uobject(attrs,
							  MLX5_IB_ATTR_DEVX_OBJ_MODIFY_HANDLE);
	struct mlx5_ib_ucontext *c = rdma_udata_to_drv_context(
		&attrs->driver_udata, struct mlx5_ib_ucontext, ibucontext);
	struct mlx5_ib_dev *mdev = to_mdev(c->ibucontext.device);
	void *cmd_out;
	int err, err2;
	int uid;

	if (MLX5_GET(general_obj_in_cmd_hdr, cmd_in, vhca_tunnel_id))
		return -EINVAL;

	uid = devx_get_uid(c, cmd_in);
	if (uid < 0)
		return uid;

	if (!devx_is_obj_modify_cmd(cmd_in))
		return -EINVAL;

	if (!devx_is_valid_obj_id(attrs, uobj, cmd_in))
		return -EINVAL;

	cmd_out = uverbs_zalloc(attrs, cmd_out_len);
	if (IS_ERR(cmd_out))
		return PTR_ERR(cmd_out);

	MLX5_SET(general_obj_in_cmd_hdr, cmd_in, uid, uid);
	devx_set_umem_valid(cmd_in);

	err = mlx5_cmd_do(mdev->mdev, cmd_in,
			  uverbs_attr_get_len(attrs, MLX5_IB_ATTR_DEVX_OBJ_MODIFY_CMD_IN),
			  cmd_out, cmd_out_len);
	if (err && err != -EREMOTEIO)
		return err;

	err2 = uverbs_copy_to(attrs, MLX5_IB_ATTR_DEVX_OBJ_MODIFY_CMD_OUT,
			      cmd_out, cmd_out_len);

	return err2 ?: err;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_OBJ_QUERY)(
	struct uverbs_attr_bundle *attrs)
{
	void *cmd_in = uverbs_attr_get_alloced_ptr(attrs, MLX5_IB_ATTR_DEVX_OBJ_QUERY_CMD_IN);
	int cmd_out_len = uverbs_attr_get_len(attrs,
					      MLX5_IB_ATTR_DEVX_OBJ_QUERY_CMD_OUT);
	struct ib_uobject *uobj = uverbs_attr_get_uobject(attrs,
							  MLX5_IB_ATTR_DEVX_OBJ_QUERY_HANDLE);
	struct mlx5_ib_ucontext *c = rdma_udata_to_drv_context(
		&attrs->driver_udata, struct mlx5_ib_ucontext, ibucontext);
	void *cmd_out;
	int err, err2;
	int uid;
	struct mlx5_ib_dev *mdev = to_mdev(c->ibucontext.device);

	if (MLX5_GET(general_obj_in_cmd_hdr, cmd_in, vhca_tunnel_id))
		return -EINVAL;

	uid = devx_get_uid(c, cmd_in);
	if (uid < 0)
		return uid;

	if (!devx_is_obj_query_cmd(cmd_in))
		return -EINVAL;

	if (!devx_is_valid_obj_id(attrs, uobj, cmd_in))
		return -EINVAL;

	cmd_out = uverbs_zalloc(attrs, cmd_out_len);
	if (IS_ERR(cmd_out))
		return PTR_ERR(cmd_out);

	MLX5_SET(general_obj_in_cmd_hdr, cmd_in, uid, uid);
	err = mlx5_cmd_do(mdev->mdev, cmd_in,
			  uverbs_attr_get_len(attrs, MLX5_IB_ATTR_DEVX_OBJ_QUERY_CMD_IN),
			  cmd_out, cmd_out_len);
	if (err && err != -EREMOTEIO)
		return err;

	err2 = uverbs_copy_to(attrs, MLX5_IB_ATTR_DEVX_OBJ_QUERY_CMD_OUT,
			      cmd_out, cmd_out_len);

	return err2 ?: err;
}

struct devx_async_event_queue {
	spinlock_t		lock;
	wait_queue_head_t	poll_wait;
	struct list_head	event_list;
	atomic_t		bytes_in_use;
	u8			is_destroyed:1;
};

struct devx_async_cmd_event_file {
	struct ib_uobject		uobj;
	struct devx_async_event_queue	ev_queue;
	struct mlx5_async_ctx		async_ctx;
};

static void devx_init_event_queue(struct devx_async_event_queue *ev_queue)
{
	spin_lock_init(&ev_queue->lock);
	INIT_LIST_HEAD(&ev_queue->event_list);
	init_waitqueue_head(&ev_queue->poll_wait);
	atomic_set(&ev_queue->bytes_in_use, 0);
	ev_queue->is_destroyed = 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_ASYNC_CMD_FD_ALLOC)(
	struct uverbs_attr_bundle *attrs)
{
	struct devx_async_cmd_event_file *ev_file;

	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_DEVX_ASYNC_CMD_FD_ALLOC_HANDLE);
	struct mlx5_ib_dev *mdev = mlx5_udata_to_mdev(&attrs->driver_udata);

	ev_file = container_of(uobj, struct devx_async_cmd_event_file,
			       uobj);
	devx_init_event_queue(&ev_file->ev_queue);
	mlx5_cmd_init_async_ctx(mdev->mdev, &ev_file->async_ctx);
	return 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_ASYNC_EVENT_FD_ALLOC)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_DEVX_ASYNC_EVENT_FD_ALLOC_HANDLE);
	struct devx_async_event_file *ev_file;
	struct mlx5_ib_ucontext *c = rdma_udata_to_drv_context(
		&attrs->driver_udata, struct mlx5_ib_ucontext, ibucontext);
	struct mlx5_ib_dev *dev = to_mdev(c->ibucontext.device);
	u32 flags;
	int err;

	err = uverbs_get_flags32(&flags, attrs,
		MLX5_IB_ATTR_DEVX_ASYNC_EVENT_FD_ALLOC_FLAGS,
		MLX5_IB_UAPI_DEVX_CR_EV_CH_FLAGS_OMIT_DATA);

	if (err)
		return err;

	ev_file = container_of(uobj, struct devx_async_event_file,
			       uobj);
	spin_lock_init(&ev_file->lock);
	INIT_LIST_HEAD(&ev_file->event_list);
	init_waitqueue_head(&ev_file->poll_wait);
	if (flags & MLX5_IB_UAPI_DEVX_CR_EV_CH_FLAGS_OMIT_DATA)
		ev_file->omit_data = 1;
	INIT_LIST_HEAD(&ev_file->subscribed_events_list);
	ev_file->dev = dev;
	get_device(&dev->ib_dev.dev);
	return 0;
}

static void devx_query_callback(int status, struct mlx5_async_work *context)
{
	struct devx_async_data *async_data =
		container_of(context, struct devx_async_data, cb_work);
	struct devx_async_cmd_event_file *ev_file = async_data->ev_file;
	struct devx_async_event_queue *ev_queue = &ev_file->ev_queue;
	unsigned long flags;

	/*
	 * Note that if the struct devx_async_cmd_event_file uobj begins to be
	 * destroyed it will block at mlx5_cmd_cleanup_async_ctx() until this
	 * routine returns, ensuring that it always remains valid here.
	 */
	spin_lock_irqsave(&ev_queue->lock, flags);
	list_add_tail(&async_data->list, &ev_queue->event_list);
	spin_unlock_irqrestore(&ev_queue->lock, flags);

	wake_up_interruptible(&ev_queue->poll_wait);
}

#define MAX_ASYNC_BYTES_IN_USE (1024 * 1024) /* 1MB */

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_OBJ_ASYNC_QUERY)(
	struct uverbs_attr_bundle *attrs)
{
	void *cmd_in = uverbs_attr_get_alloced_ptr(attrs,
				MLX5_IB_ATTR_DEVX_OBJ_QUERY_ASYNC_CMD_IN);
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
				attrs,
				MLX5_IB_ATTR_DEVX_OBJ_QUERY_ASYNC_HANDLE);
	u16 cmd_out_len;
	struct mlx5_ib_ucontext *c = rdma_udata_to_drv_context(
		&attrs->driver_udata, struct mlx5_ib_ucontext, ibucontext);
	struct ib_uobject *fd_uobj;
	int err;
	int uid;
	struct mlx5_ib_dev *mdev = to_mdev(c->ibucontext.device);
	struct devx_async_cmd_event_file *ev_file;
	struct devx_async_data *async_data;

	if (MLX5_GET(general_obj_in_cmd_hdr, cmd_in, vhca_tunnel_id))
		return -EINVAL;

	uid = devx_get_uid(c, cmd_in);
	if (uid < 0)
		return uid;

	if (!devx_is_obj_query_cmd(cmd_in))
		return -EINVAL;

	err = uverbs_get_const(&cmd_out_len, attrs,
			       MLX5_IB_ATTR_DEVX_OBJ_QUERY_ASYNC_OUT_LEN);
	if (err)
		return err;

	if (!devx_is_valid_obj_id(attrs, uobj, cmd_in))
		return -EINVAL;

	fd_uobj = uverbs_attr_get_uobject(attrs,
				MLX5_IB_ATTR_DEVX_OBJ_QUERY_ASYNC_FD);
	if (IS_ERR(fd_uobj))
		return PTR_ERR(fd_uobj);

	ev_file = container_of(fd_uobj, struct devx_async_cmd_event_file,
			       uobj);

	if (atomic_add_return(cmd_out_len, &ev_file->ev_queue.bytes_in_use) >
			MAX_ASYNC_BYTES_IN_USE) {
		atomic_sub(cmd_out_len, &ev_file->ev_queue.bytes_in_use);
		return -EAGAIN;
	}

	async_data = kvzalloc(struct_size(async_data, hdr.out_data,
					  cmd_out_len), GFP_KERNEL);
	if (!async_data) {
		err = -ENOMEM;
		goto sub_bytes;
	}

	err = uverbs_copy_from(&async_data->hdr.wr_id, attrs,
			       MLX5_IB_ATTR_DEVX_OBJ_QUERY_ASYNC_WR_ID);
	if (err)
		goto free_async;

	async_data->cmd_out_len = cmd_out_len;
	async_data->mdev = mdev;
	async_data->ev_file = ev_file;

	MLX5_SET(general_obj_in_cmd_hdr, cmd_in, uid, uid);
	err = mlx5_cmd_exec_cb(&ev_file->async_ctx, cmd_in,
		    uverbs_attr_get_len(attrs,
				MLX5_IB_ATTR_DEVX_OBJ_QUERY_ASYNC_CMD_IN),
		    async_data->hdr.out_data,
		    async_data->cmd_out_len,
		    devx_query_callback, &async_data->cb_work);

	if (err)
		goto free_async;

	return 0;

free_async:
	kvfree(async_data);
sub_bytes:
	atomic_sub(cmd_out_len, &ev_file->ev_queue.bytes_in_use);
	return err;
}

static void
subscribe_event_xa_dealloc(struct mlx5_devx_event_table *devx_event_table,
			   u32 key_level1,
			   bool is_level2,
			   u32 key_level2)
{
	struct devx_event *event;
	struct devx_obj_event *xa_val_level2;

	/* Level 1 is valid for future use, no need to free */
	if (!is_level2)
		return;

	event = xa_load(&devx_event_table->event_xa, key_level1);
	WARN_ON(!event);

	xa_val_level2 = xa_load(&event->object_ids,
				key_level2);
	if (list_empty(&xa_val_level2->obj_sub_list)) {
		xa_erase(&event->object_ids,
			 key_level2);
		kfree_rcu(xa_val_level2, rcu);
	}
}

static int
subscribe_event_xa_alloc(struct mlx5_devx_event_table *devx_event_table,
			 u32 key_level1,
			 bool is_level2,
			 u32 key_level2)
{
	struct devx_obj_event *obj_event;
	struct devx_event *event;
	int err;

	event = xa_load(&devx_event_table->event_xa, key_level1);
	if (!event) {
		event = kzalloc(sizeof(*event), GFP_KERNEL);
		if (!event)
			return -ENOMEM;

		INIT_LIST_HEAD(&event->unaffiliated_list);
		xa_init(&event->object_ids);

		err = xa_insert(&devx_event_table->event_xa,
				key_level1,
				event,
				GFP_KERNEL);
		if (err) {
			kfree(event);
			return err;
		}
	}

	if (!is_level2)
		return 0;

	obj_event = xa_load(&event->object_ids, key_level2);
	if (!obj_event) {
		obj_event = kzalloc(sizeof(*obj_event), GFP_KERNEL);
		if (!obj_event)
			/* Level1 is valid for future use, no need to free */
			return -ENOMEM;

		err = xa_insert(&event->object_ids,
				key_level2,
				obj_event,
				GFP_KERNEL);
		if (err) {
			kfree(obj_event);
			return err;
		}
		INIT_LIST_HEAD(&obj_event->obj_sub_list);
	}

	return 0;
}

static bool is_valid_events_legacy(int num_events, u16 *event_type_num_list,
				   struct devx_obj *obj)
{
	int i;

	for (i = 0; i < num_events; i++) {
		if (obj) {
			if (!is_legacy_obj_event_num(event_type_num_list[i]))
				return false;
		} else if (!is_legacy_unaffiliated_event_num(
				event_type_num_list[i])) {
			return false;
		}
	}

	return true;
}

#define MAX_SUPP_EVENT_NUM 255
static bool is_valid_events(struct mlx5_core_dev *dev,
			    int num_events, u16 *event_type_num_list,
			    struct devx_obj *obj)
{
	__be64 *aff_events;
	__be64 *unaff_events;
	int mask_entry;
	int mask_bit;
	int i;

	if (MLX5_CAP_GEN(dev, event_cap)) {
		aff_events = MLX5_CAP_DEV_EVENT(dev,
						user_affiliated_events);
		unaff_events = MLX5_CAP_DEV_EVENT(dev,
						  user_unaffiliated_events);
	} else {
		return is_valid_events_legacy(num_events, event_type_num_list,
					      obj);
	}

	for (i = 0; i < num_events; i++) {
		if (event_type_num_list[i] > MAX_SUPP_EVENT_NUM)
			return false;

		mask_entry = event_type_num_list[i] / 64;
		mask_bit = event_type_num_list[i] % 64;

		if (obj) {
			/* CQ completion */
			if (event_type_num_list[i] == 0)
				continue;

			if (!(be64_to_cpu(aff_events[mask_entry]) &
					(1ull << mask_bit)))
				return false;

			continue;
		}

		if (!(be64_to_cpu(unaff_events[mask_entry]) &
				(1ull << mask_bit)))
			return false;
	}

	return true;
}

#define MAX_NUM_EVENTS 16
static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_SUBSCRIBE_EVENT)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *devx_uobj = uverbs_attr_get_uobject(
				attrs,
				MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_OBJ_HANDLE);
	struct mlx5_ib_ucontext *c = rdma_udata_to_drv_context(
		&attrs->driver_udata, struct mlx5_ib_ucontext, ibucontext);
	struct mlx5_ib_dev *dev = to_mdev(c->ibucontext.device);
	struct ib_uobject *fd_uobj;
	struct devx_obj *obj = NULL;
	struct devx_async_event_file *ev_file;
	struct mlx5_devx_event_table *devx_event_table = &dev->devx_event_table;
	u16 *event_type_num_list;
	struct devx_event_subscription *event_sub, *tmp_sub;
	struct list_head sub_list;
	int redirect_fd;
	bool use_eventfd = false;
	int num_events;
	u16 obj_type = 0;
	u64 cookie = 0;
	u32 obj_id = 0;
	int err;
	int i;

	if (!c->devx_uid)
		return -EINVAL;

	if (!IS_ERR(devx_uobj)) {
		obj = (struct devx_obj *)devx_uobj->object;
		if (obj)
			obj_id = get_dec_obj_id(obj->obj_id);
	}

	fd_uobj = uverbs_attr_get_uobject(attrs,
				MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_FD_HANDLE);
	if (IS_ERR(fd_uobj))
		return PTR_ERR(fd_uobj);

	ev_file = container_of(fd_uobj, struct devx_async_event_file,
			       uobj);

	if (uverbs_attr_is_valid(attrs,
				 MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_FD_NUM)) {
		err = uverbs_copy_from(&redirect_fd, attrs,
			       MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_FD_NUM);
		if (err)
			return err;

		use_eventfd = true;
	}

	if (uverbs_attr_is_valid(attrs,
				 MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_COOKIE)) {
		if (use_eventfd)
			return -EINVAL;

		err = uverbs_copy_from(&cookie, attrs,
				MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_COOKIE);
		if (err)
			return err;
	}

	num_events = uverbs_attr_ptr_get_array_size(
		attrs, MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_TYPE_NUM_LIST,
		sizeof(u16));

	if (num_events < 0)
		return num_events;

	if (num_events > MAX_NUM_EVENTS)
		return -EINVAL;

	event_type_num_list = uverbs_attr_get_alloced_ptr(attrs,
			MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_TYPE_NUM_LIST);

	if (!is_valid_events(dev->mdev, num_events, event_type_num_list, obj))
		return -EINVAL;

	INIT_LIST_HEAD(&sub_list);

	/* Protect from concurrent subscriptions to same XA entries to allow
	 * both to succeed
	 */
	mutex_lock(&devx_event_table->event_xa_lock);
	for (i = 0; i < num_events; i++) {
		u32 key_level1;

		if (obj)
			obj_type = get_dec_obj_type(obj,
						    event_type_num_list[i]);
		key_level1 = event_type_num_list[i] | obj_type << 16;

		err = subscribe_event_xa_alloc(devx_event_table,
					       key_level1,
					       obj,
					       obj_id);
		if (err)
			goto err;

		event_sub = kzalloc(sizeof(*event_sub), GFP_KERNEL);
		if (!event_sub) {
			err = -ENOMEM;
			goto err;
		}

		list_add_tail(&event_sub->event_list, &sub_list);
		uverbs_uobject_get(&ev_file->uobj);
		if (use_eventfd) {
			event_sub->eventfd =
				eventfd_ctx_fdget(redirect_fd);

			if (IS_ERR(event_sub->eventfd)) {
				err = PTR_ERR(event_sub->eventfd);
				event_sub->eventfd = NULL;
				goto err;
			}
		}

		event_sub->cookie = cookie;
		event_sub->ev_file = ev_file;
		/* May be needed upon cleanup the devx object/subscription */
		event_sub->xa_key_level1 = key_level1;
		event_sub->xa_key_level2 = obj_id;
		INIT_LIST_HEAD(&event_sub->obj_list);
	}

	/* Once all the allocations and the XA data insertions were done we
	 * can go ahead and add all the subscriptions to the relevant lists
	 * without concern of a failure.
	 */
	list_for_each_entry_safe(event_sub, tmp_sub, &sub_list, event_list) {
		struct devx_event *event;
		struct devx_obj_event *obj_event;

		list_del_init(&event_sub->event_list);

		spin_lock_irq(&ev_file->lock);
		list_add_tail_rcu(&event_sub->file_list,
				  &ev_file->subscribed_events_list);
		spin_unlock_irq(&ev_file->lock);

		event = xa_load(&devx_event_table->event_xa,
				event_sub->xa_key_level1);
		WARN_ON(!event);

		if (!obj) {
			list_add_tail_rcu(&event_sub->xa_list,
					  &event->unaffiliated_list);
			continue;
		}

		obj_event = xa_load(&event->object_ids, obj_id);
		WARN_ON(!obj_event);
		list_add_tail_rcu(&event_sub->xa_list,
				  &obj_event->obj_sub_list);
		list_add_tail_rcu(&event_sub->obj_list,
				  &obj->event_sub);
	}

	mutex_unlock(&devx_event_table->event_xa_lock);
	return 0;

err:
	list_for_each_entry_safe(event_sub, tmp_sub, &sub_list, event_list) {
		list_del(&event_sub->event_list);

		subscribe_event_xa_dealloc(devx_event_table,
					   event_sub->xa_key_level1,
					   obj,
					   obj_id);

		if (event_sub->eventfd)
			eventfd_ctx_put(event_sub->eventfd);
		uverbs_uobject_put(&event_sub->ev_file->uobj);
		kfree(event_sub);
	}

	mutex_unlock(&devx_event_table->event_xa_lock);
	return err;
}

static int devx_umem_get(struct mlx5_ib_dev *dev, struct ib_ucontext *ucontext,
			 struct uverbs_attr_bundle *attrs,
			 struct devx_umem *obj, u32 access_flags)
{
	u64 addr;
	size_t size;
	int err;

	if (uverbs_copy_from(&addr, attrs, MLX5_IB_ATTR_DEVX_UMEM_REG_ADDR) ||
	    uverbs_copy_from(&size, attrs, MLX5_IB_ATTR_DEVX_UMEM_REG_LEN))
		return -EFAULT;

	err = ib_check_mr_access(&dev->ib_dev, access_flags);
	if (err)
		return err;

	if (uverbs_attr_is_valid(attrs, MLX5_IB_ATTR_DEVX_UMEM_REG_DMABUF_FD)) {
		struct ib_umem_dmabuf *umem_dmabuf;
		int dmabuf_fd;

		err = uverbs_get_raw_fd(&dmabuf_fd, attrs,
					MLX5_IB_ATTR_DEVX_UMEM_REG_DMABUF_FD);
		if (err)
			return -EFAULT;

		umem_dmabuf = ib_umem_dmabuf_get_pinned(
			&dev->ib_dev, addr, size, dmabuf_fd, access_flags);
		if (IS_ERR(umem_dmabuf))
			return PTR_ERR(umem_dmabuf);
		obj->umem = &umem_dmabuf->umem;
	} else {
		obj->umem = ib_umem_get(&dev->ib_dev, addr, size, access_flags);
		if (IS_ERR(obj->umem))
			return PTR_ERR(obj->umem);
	}
	return 0;
}

static unsigned int devx_umem_find_best_pgsize(struct ib_umem *umem,
					       unsigned long pgsz_bitmap)
{
	unsigned long page_size;

	/* Don't bother checking larger page sizes as offset must be zero and
	 * total DEVX umem length must be equal to total umem length.
	 */
	pgsz_bitmap &= GENMASK_ULL(max_t(u64, order_base_2(umem->length),
					 PAGE_SHIFT),
				   MLX5_ADAPTER_PAGE_SHIFT);
	if (!pgsz_bitmap)
		return 0;

	page_size = ib_umem_find_best_pgoff(umem, pgsz_bitmap, U64_MAX);
	if (!page_size)
		return 0;

	/* If the page_size is less than the CPU page size then we can use the
	 * offset and create a umem which is a subset of the page list.
	 * For larger page sizes we can't be sure the DMA  list reflects the
	 * VA so we must ensure that the umem extent is exactly equal to the
	 * page list. Reduce the page size until one of these cases is true.
	 */
	while ((ib_umem_dma_offset(umem, page_size) != 0 ||
		(umem->length % page_size) != 0) &&
		page_size > PAGE_SIZE)
		page_size /= 2;

	return page_size;
}

static int devx_umem_reg_cmd_alloc(struct mlx5_ib_dev *dev,
				   struct uverbs_attr_bundle *attrs,
				   struct devx_umem *obj,
				   struct devx_umem_reg_cmd *cmd,
				   int access)
{
	unsigned long pgsz_bitmap;
	unsigned int page_size;
	__be64 *mtt;
	void *umem;
	int ret;

	/*
	 * If the user does not pass in pgsz_bitmap then the user promises not
	 * to use umem_offset!=0 in any commands that allocate on top of the
	 * umem.
	 *
	 * If the user wants to use a umem_offset then it must pass in
	 * pgsz_bitmap which guides the maximum page size and thus maximum
	 * object alignment inside the umem. See the PRM.
	 *
	 * Users are not allowed to use IOVA here, mkeys are not supported on
	 * umem.
	 */
	ret = uverbs_get_const_default(&pgsz_bitmap, attrs,
			MLX5_IB_ATTR_DEVX_UMEM_REG_PGSZ_BITMAP,
			GENMASK_ULL(63,
				    min(PAGE_SHIFT, MLX5_ADAPTER_PAGE_SHIFT)));
	if (ret)
		return ret;

	page_size = devx_umem_find_best_pgsize(obj->umem, pgsz_bitmap);
	if (!page_size)
		return -EINVAL;

	cmd->inlen = MLX5_ST_SZ_BYTES(create_umem_in) +
		     (MLX5_ST_SZ_BYTES(mtt) *
		      ib_umem_num_dma_blocks(obj->umem, page_size));
	cmd->in = uverbs_zalloc(attrs, cmd->inlen);
	if (IS_ERR(cmd->in))
		return PTR_ERR(cmd->in);

	umem = MLX5_ADDR_OF(create_umem_in, cmd->in, umem);
	mtt = (__be64 *)MLX5_ADDR_OF(umem, umem, mtt);

	MLX5_SET(create_umem_in, cmd->in, opcode, MLX5_CMD_OP_CREATE_UMEM);
	MLX5_SET64(umem, umem, num_of_mtt,
		   ib_umem_num_dma_blocks(obj->umem, page_size));
	MLX5_SET(umem, umem, log_page_size,
		 order_base_2(page_size) - MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET(umem, umem, page_offset,
		 ib_umem_dma_offset(obj->umem, page_size));

	if (mlx5_umem_needs_ats(dev, obj->umem, access))
		MLX5_SET(umem, umem, ats, 1);

	mlx5_ib_populate_pas(obj->umem, page_size, mtt,
			     (obj->umem->writable ? MLX5_IB_MTT_WRITE : 0) |
				     MLX5_IB_MTT_READ);
	return 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_UMEM_REG)(
	struct uverbs_attr_bundle *attrs)
{
	struct devx_umem_reg_cmd cmd;
	struct devx_umem *obj;
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_DEVX_UMEM_REG_HANDLE);
	u32 obj_id;
	struct mlx5_ib_ucontext *c = rdma_udata_to_drv_context(
		&attrs->driver_udata, struct mlx5_ib_ucontext, ibucontext);
	struct mlx5_ib_dev *dev = to_mdev(c->ibucontext.device);
	int access_flags;
	int err;

	if (!c->devx_uid)
		return -EINVAL;

	err = uverbs_get_flags32(&access_flags, attrs,
				 MLX5_IB_ATTR_DEVX_UMEM_REG_ACCESS,
				 IB_ACCESS_LOCAL_WRITE |
				 IB_ACCESS_REMOTE_WRITE |
				 IB_ACCESS_REMOTE_READ |
				 IB_ACCESS_RELAXED_ORDERING);
	if (err)
		return err;

	obj = kzalloc(sizeof(struct devx_umem), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	err = devx_umem_get(dev, &c->ibucontext, attrs, obj, access_flags);
	if (err)
		goto err_obj_free;

	err = devx_umem_reg_cmd_alloc(dev, attrs, obj, &cmd, access_flags);
	if (err)
		goto err_umem_release;

	MLX5_SET(create_umem_in, cmd.in, uid, c->devx_uid);
	err = mlx5_cmd_exec(dev->mdev, cmd.in, cmd.inlen, cmd.out,
			    sizeof(cmd.out));
	if (err)
		goto err_umem_release;

	obj->mdev = dev->mdev;
	uobj->object = obj;
	devx_obj_build_destroy_cmd(cmd.in, cmd.out, obj->dinbox, &obj->dinlen, &obj_id);
	uverbs_finalize_uobj_create(attrs, MLX5_IB_ATTR_DEVX_UMEM_REG_HANDLE);

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_DEVX_UMEM_REG_OUT_ID, &obj_id,
			     sizeof(obj_id));
	return err;

err_umem_release:
	ib_umem_release(obj->umem);
err_obj_free:
	kfree(obj);
	return err;
}

static int devx_umem_cleanup(struct ib_uobject *uobject,
			     enum rdma_remove_reason why,
			     struct uverbs_attr_bundle *attrs)
{
	struct devx_umem *obj = uobject->object;
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	int err;

	err = mlx5_cmd_exec(obj->mdev, obj->dinbox, obj->dinlen, out, sizeof(out));
	if (err)
		return err;

	ib_umem_release(obj->umem);
	kfree(obj);
	return 0;
}

static bool is_unaffiliated_event(struct mlx5_core_dev *dev,
				  unsigned long event_type)
{
	__be64 *unaff_events;
	int mask_entry;
	int mask_bit;

	if (!MLX5_CAP_GEN(dev, event_cap))
		return is_legacy_unaffiliated_event_num(event_type);

	unaff_events = MLX5_CAP_DEV_EVENT(dev,
					  user_unaffiliated_events);
	WARN_ON(event_type > MAX_SUPP_EVENT_NUM);

	mask_entry = event_type / 64;
	mask_bit = event_type % 64;

	if (!(be64_to_cpu(unaff_events[mask_entry]) & (1ull << mask_bit)))
		return false;

	return true;
}

static u32 devx_get_obj_id_from_event(unsigned long event_type, void *data)
{
	struct mlx5_eqe *eqe = data;
	u32 obj_id = 0;

	switch (event_type) {
	case MLX5_EVENT_TYPE_SRQ_CATAS_ERROR:
	case MLX5_EVENT_TYPE_SRQ_RQ_LIMIT:
	case MLX5_EVENT_TYPE_PATH_MIG:
	case MLX5_EVENT_TYPE_COMM_EST:
	case MLX5_EVENT_TYPE_SQ_DRAINED:
	case MLX5_EVENT_TYPE_SRQ_LAST_WQE:
	case MLX5_EVENT_TYPE_WQ_CATAS_ERROR:
	case MLX5_EVENT_TYPE_PATH_MIG_FAILED:
	case MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
	case MLX5_EVENT_TYPE_WQ_ACCESS_ERROR:
		obj_id = be32_to_cpu(eqe->data.qp_srq.qp_srq_n) & 0xffffff;
		break;
	case MLX5_EVENT_TYPE_XRQ_ERROR:
		obj_id = be32_to_cpu(eqe->data.xrq_err.type_xrqn) & 0xffffff;
		break;
	case MLX5_EVENT_TYPE_DCT_DRAINED:
	case MLX5_EVENT_TYPE_DCT_KEY_VIOLATION:
		obj_id = be32_to_cpu(eqe->data.dct.dctn) & 0xffffff;
		break;
	case MLX5_EVENT_TYPE_CQ_ERROR:
		obj_id = be32_to_cpu(eqe->data.cq_err.cqn) & 0xffffff;
		break;
	default:
		obj_id = MLX5_GET(affiliated_event_header, &eqe->data, obj_id);
		break;
	}

	return obj_id;
}

static int deliver_event(struct devx_event_subscription *event_sub,
			 const void *data)
{
	struct devx_async_event_file *ev_file;
	struct devx_async_event_data *event_data;
	unsigned long flags;

	ev_file = event_sub->ev_file;

	if (ev_file->omit_data) {
		spin_lock_irqsave(&ev_file->lock, flags);
		if (!list_empty(&event_sub->event_list) ||
		    ev_file->is_destroyed) {
			spin_unlock_irqrestore(&ev_file->lock, flags);
			return 0;
		}

		list_add_tail(&event_sub->event_list, &ev_file->event_list);
		spin_unlock_irqrestore(&ev_file->lock, flags);
		wake_up_interruptible(&ev_file->poll_wait);
		return 0;
	}

	event_data = kzalloc(sizeof(*event_data) + sizeof(struct mlx5_eqe),
			     GFP_ATOMIC);
	if (!event_data) {
		spin_lock_irqsave(&ev_file->lock, flags);
		ev_file->is_overflow_err = 1;
		spin_unlock_irqrestore(&ev_file->lock, flags);
		return -ENOMEM;
	}

	event_data->hdr.cookie = event_sub->cookie;
	memcpy(event_data->hdr.out_data, data, sizeof(struct mlx5_eqe));

	spin_lock_irqsave(&ev_file->lock, flags);
	if (!ev_file->is_destroyed)
		list_add_tail(&event_data->list, &ev_file->event_list);
	else
		kfree(event_data);
	spin_unlock_irqrestore(&ev_file->lock, flags);
	wake_up_interruptible(&ev_file->poll_wait);

	return 0;
}

static void dispatch_event_fd(struct list_head *fd_list,
			      const void *data)
{
	struct devx_event_subscription *item;

	list_for_each_entry_rcu(item, fd_list, xa_list) {
		if (item->eventfd)
			eventfd_signal(item->eventfd);
		else
			deliver_event(item, data);
	}
}

static int devx_event_notifier(struct notifier_block *nb,
			       unsigned long event_type, void *data)
{
	struct mlx5_devx_event_table *table;
	struct mlx5_ib_dev *dev;
	struct devx_event *event;
	struct devx_obj_event *obj_event;
	u16 obj_type = 0;
	bool is_unaffiliated;
	u32 obj_id;

	/* Explicit filtering to kernel events which may occur frequently */
	if (event_type == MLX5_EVENT_TYPE_CMD ||
	    event_type == MLX5_EVENT_TYPE_PAGE_REQUEST)
		return NOTIFY_OK;

	table = container_of(nb, struct mlx5_devx_event_table, devx_nb.nb);
	dev = container_of(table, struct mlx5_ib_dev, devx_event_table);
	is_unaffiliated = is_unaffiliated_event(dev->mdev, event_type);

	if (!is_unaffiliated)
		obj_type = get_event_obj_type(event_type, data);

	rcu_read_lock();
	event = xa_load(&table->event_xa, event_type | (obj_type << 16));
	if (!event) {
		rcu_read_unlock();
		return NOTIFY_DONE;
	}

	if (is_unaffiliated) {
		dispatch_event_fd(&event->unaffiliated_list, data);
		rcu_read_unlock();
		return NOTIFY_OK;
	}

	obj_id = devx_get_obj_id_from_event(event_type, data);
	obj_event = xa_load(&event->object_ids, obj_id);
	if (!obj_event) {
		rcu_read_unlock();
		return NOTIFY_DONE;
	}

	dispatch_event_fd(&obj_event->obj_sub_list, data);

	rcu_read_unlock();
	return NOTIFY_OK;
}

int mlx5_ib_devx_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_devx_event_table *table = &dev->devx_event_table;
	int uid;

	uid = mlx5_ib_devx_create(dev, false);
	if (uid > 0) {
		dev->devx_whitelist_uid = uid;
		xa_init(&table->event_xa);
		mutex_init(&table->event_xa_lock);
		MLX5_NB_INIT(&table->devx_nb, devx_event_notifier, NOTIFY_ANY);
		mlx5_eq_notifier_register(dev->mdev, &table->devx_nb);
	}

	return 0;
}

void mlx5_ib_devx_cleanup(struct mlx5_ib_dev *dev)
{
	struct mlx5_devx_event_table *table = &dev->devx_event_table;
	struct devx_event_subscription *sub, *tmp;
	struct devx_event *event;
	void *entry;
	unsigned long id;

	if (dev->devx_whitelist_uid) {
		mlx5_eq_notifier_unregister(dev->mdev, &table->devx_nb);
		mutex_lock(&dev->devx_event_table.event_xa_lock);
		xa_for_each(&table->event_xa, id, entry) {
			event = entry;
			list_for_each_entry_safe(
				sub, tmp, &event->unaffiliated_list, xa_list)
				devx_cleanup_subscription(dev, sub);
			kfree(entry);
		}
		mutex_unlock(&dev->devx_event_table.event_xa_lock);
		xa_destroy(&table->event_xa);

		mlx5_ib_devx_destroy(dev, dev->devx_whitelist_uid);
	}
}

static ssize_t devx_async_cmd_event_read(struct file *filp, char __user *buf,
					 size_t count, loff_t *pos)
{
	struct devx_async_cmd_event_file *comp_ev_file = filp->private_data;
	struct devx_async_event_queue *ev_queue = &comp_ev_file->ev_queue;
	struct devx_async_data *event;
	int ret = 0;
	size_t eventsz;

	spin_lock_irq(&ev_queue->lock);

	while (list_empty(&ev_queue->event_list)) {
		spin_unlock_irq(&ev_queue->lock);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(
			    ev_queue->poll_wait,
			    (!list_empty(&ev_queue->event_list) ||
			     ev_queue->is_destroyed))) {
			return -ERESTARTSYS;
		}

		spin_lock_irq(&ev_queue->lock);
		if (ev_queue->is_destroyed) {
			spin_unlock_irq(&ev_queue->lock);
			return -EIO;
		}
	}

	event = list_entry(ev_queue->event_list.next,
			   struct devx_async_data, list);
	eventsz = event->cmd_out_len +
			sizeof(struct mlx5_ib_uapi_devx_async_cmd_hdr);

	if (eventsz > count) {
		spin_unlock_irq(&ev_queue->lock);
		return -ENOSPC;
	}

	list_del(ev_queue->event_list.next);
	spin_unlock_irq(&ev_queue->lock);

	if (copy_to_user(buf, &event->hdr, eventsz))
		ret = -EFAULT;
	else
		ret = eventsz;

	atomic_sub(event->cmd_out_len, &ev_queue->bytes_in_use);
	kvfree(event);
	return ret;
}

static __poll_t devx_async_cmd_event_poll(struct file *filp,
					      struct poll_table_struct *wait)
{
	struct devx_async_cmd_event_file *comp_ev_file = filp->private_data;
	struct devx_async_event_queue *ev_queue = &comp_ev_file->ev_queue;
	__poll_t pollflags = 0;

	poll_wait(filp, &ev_queue->poll_wait, wait);

	spin_lock_irq(&ev_queue->lock);
	if (ev_queue->is_destroyed)
		pollflags = EPOLLIN | EPOLLRDNORM | EPOLLRDHUP;
	else if (!list_empty(&ev_queue->event_list))
		pollflags = EPOLLIN | EPOLLRDNORM;
	spin_unlock_irq(&ev_queue->lock);

	return pollflags;
}

static const struct file_operations devx_async_cmd_event_fops = {
	.owner	 = THIS_MODULE,
	.read	 = devx_async_cmd_event_read,
	.poll    = devx_async_cmd_event_poll,
	.release = uverbs_uobject_fd_release,
	.llseek	 = no_llseek,
};

static ssize_t devx_async_event_read(struct file *filp, char __user *buf,
				     size_t count, loff_t *pos)
{
	struct devx_async_event_file *ev_file = filp->private_data;
	struct devx_event_subscription *event_sub;
	struct devx_async_event_data *event;
	int ret = 0;
	size_t eventsz;
	bool omit_data;
	void *event_data;

	omit_data = ev_file->omit_data;

	spin_lock_irq(&ev_file->lock);

	if (ev_file->is_overflow_err) {
		ev_file->is_overflow_err = 0;
		spin_unlock_irq(&ev_file->lock);
		return -EOVERFLOW;
	}


	while (list_empty(&ev_file->event_list)) {
		spin_unlock_irq(&ev_file->lock);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(ev_file->poll_wait,
			    (!list_empty(&ev_file->event_list) ||
			     ev_file->is_destroyed))) {
			return -ERESTARTSYS;
		}

		spin_lock_irq(&ev_file->lock);
		if (ev_file->is_destroyed) {
			spin_unlock_irq(&ev_file->lock);
			return -EIO;
		}
	}

	if (omit_data) {
		event_sub = list_first_entry(&ev_file->event_list,
					struct devx_event_subscription,
					event_list);
		eventsz = sizeof(event_sub->cookie);
		event_data = &event_sub->cookie;
	} else {
		event = list_first_entry(&ev_file->event_list,
				      struct devx_async_event_data, list);
		eventsz = sizeof(struct mlx5_eqe) +
			sizeof(struct mlx5_ib_uapi_devx_async_event_hdr);
		event_data = &event->hdr;
	}

	if (eventsz > count) {
		spin_unlock_irq(&ev_file->lock);
		return -EINVAL;
	}

	if (omit_data)
		list_del_init(&event_sub->event_list);
	else
		list_del(&event->list);

	spin_unlock_irq(&ev_file->lock);

	if (copy_to_user(buf, event_data, eventsz))
		/* This points to an application issue, not a kernel concern */
		ret = -EFAULT;
	else
		ret = eventsz;

	if (!omit_data)
		kfree(event);
	return ret;
}

static __poll_t devx_async_event_poll(struct file *filp,
				      struct poll_table_struct *wait)
{
	struct devx_async_event_file *ev_file = filp->private_data;
	__poll_t pollflags = 0;

	poll_wait(filp, &ev_file->poll_wait, wait);

	spin_lock_irq(&ev_file->lock);
	if (ev_file->is_destroyed)
		pollflags = EPOLLIN | EPOLLRDNORM | EPOLLRDHUP;
	else if (!list_empty(&ev_file->event_list))
		pollflags = EPOLLIN | EPOLLRDNORM;
	spin_unlock_irq(&ev_file->lock);

	return pollflags;
}

static void devx_free_subscription(struct rcu_head *rcu)
{
	struct devx_event_subscription *event_sub =
		container_of(rcu, struct devx_event_subscription, rcu);

	if (event_sub->eventfd)
		eventfd_ctx_put(event_sub->eventfd);
	uverbs_uobject_put(&event_sub->ev_file->uobj);
	kfree(event_sub);
}

static const struct file_operations devx_async_event_fops = {
	.owner	 = THIS_MODULE,
	.read	 = devx_async_event_read,
	.poll    = devx_async_event_poll,
	.release = uverbs_uobject_fd_release,
	.llseek	 = no_llseek,
};

static void devx_async_cmd_event_destroy_uobj(struct ib_uobject *uobj,
					      enum rdma_remove_reason why)
{
	struct devx_async_cmd_event_file *comp_ev_file =
		container_of(uobj, struct devx_async_cmd_event_file,
			     uobj);
	struct devx_async_event_queue *ev_queue = &comp_ev_file->ev_queue;
	struct devx_async_data *entry, *tmp;

	spin_lock_irq(&ev_queue->lock);
	ev_queue->is_destroyed = 1;
	spin_unlock_irq(&ev_queue->lock);
	wake_up_interruptible(&ev_queue->poll_wait);

	mlx5_cmd_cleanup_async_ctx(&comp_ev_file->async_ctx);

	spin_lock_irq(&comp_ev_file->ev_queue.lock);
	list_for_each_entry_safe(entry, tmp,
				 &comp_ev_file->ev_queue.event_list, list) {
		list_del(&entry->list);
		kvfree(entry);
	}
	spin_unlock_irq(&comp_ev_file->ev_queue.lock);
};

static void devx_async_event_destroy_uobj(struct ib_uobject *uobj,
					  enum rdma_remove_reason why)
{
	struct devx_async_event_file *ev_file =
		container_of(uobj, struct devx_async_event_file,
			     uobj);
	struct devx_event_subscription *event_sub, *event_sub_tmp;
	struct mlx5_ib_dev *dev = ev_file->dev;

	spin_lock_irq(&ev_file->lock);
	ev_file->is_destroyed = 1;

	/* free the pending events allocation */
	if (ev_file->omit_data) {
		struct devx_event_subscription *event_sub, *tmp;

		list_for_each_entry_safe(event_sub, tmp, &ev_file->event_list,
					 event_list)
			list_del_init(&event_sub->event_list);

	} else {
		struct devx_async_event_data *entry, *tmp;

		list_for_each_entry_safe(entry, tmp, &ev_file->event_list,
					 list) {
			list_del(&entry->list);
			kfree(entry);
		}
	}

	spin_unlock_irq(&ev_file->lock);
	wake_up_interruptible(&ev_file->poll_wait);

	mutex_lock(&dev->devx_event_table.event_xa_lock);
	/* delete the subscriptions which are related to this FD */
	list_for_each_entry_safe(event_sub, event_sub_tmp,
				 &ev_file->subscribed_events_list, file_list) {
		devx_cleanup_subscription(dev, event_sub);
		list_del_rcu(&event_sub->file_list);
		/* subscription may not be used by the read API any more */
		call_rcu(&event_sub->rcu, devx_free_subscription);
	}
	mutex_unlock(&dev->devx_event_table.event_xa_lock);

	put_device(&dev->ib_dev.dev);
};

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_UMEM_REG,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_DEVX_UMEM_REG_HANDLE,
			MLX5_IB_OBJECT_DEVX_UMEM,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_DEVX_UMEM_REG_ADDR,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_DEVX_UMEM_REG_LEN,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY),
	UVERBS_ATTR_RAW_FD(MLX5_IB_ATTR_DEVX_UMEM_REG_DMABUF_FD,
			   UA_OPTIONAL),
	UVERBS_ATTR_FLAGS_IN(MLX5_IB_ATTR_DEVX_UMEM_REG_ACCESS,
			     enum ib_access_flags),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_DEVX_UMEM_REG_PGSZ_BITMAP,
			     u64),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_DEVX_UMEM_REG_OUT_ID,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	MLX5_IB_METHOD_DEVX_UMEM_DEREG,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_DEVX_UMEM_DEREG_HANDLE,
			MLX5_IB_OBJECT_DEVX_UMEM,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_QUERY_EQN,
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_DEVX_QUERY_EQN_USER_VEC,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_DEVX_QUERY_EQN_DEV_EQN,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_QUERY_UAR,
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_DEVX_QUERY_UAR_USER_IDX,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_DEVX_QUERY_UAR_DEV_IDX,
			    UVERBS_ATTR_TYPE(u32),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_OTHER,
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_DEVX_OTHER_CMD_IN,
		UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_in_cmd_hdr)),
		UA_MANDATORY,
		UA_ALLOC_AND_COPY),
	UVERBS_ATTR_PTR_OUT(
		MLX5_IB_ATTR_DEVX_OTHER_CMD_OUT,
		UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_out_cmd_hdr)),
		UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_OBJ_CREATE,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_DEVX_OBJ_CREATE_HANDLE,
			MLX5_IB_OBJECT_DEVX_OBJ,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_IN,
		UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_in_cmd_hdr)),
		UA_MANDATORY,
		UA_ALLOC_AND_COPY),
	UVERBS_ATTR_PTR_OUT(
		MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_OUT,
		UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_out_cmd_hdr)),
		UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	MLX5_IB_METHOD_DEVX_OBJ_DESTROY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_DEVX_OBJ_DESTROY_HANDLE,
			MLX5_IB_OBJECT_DEVX_OBJ,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_OBJ_MODIFY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_DEVX_OBJ_MODIFY_HANDLE,
			UVERBS_IDR_ANY_OBJECT,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_DEVX_OBJ_MODIFY_CMD_IN,
		UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_in_cmd_hdr)),
		UA_MANDATORY,
		UA_ALLOC_AND_COPY),
	UVERBS_ATTR_PTR_OUT(
		MLX5_IB_ATTR_DEVX_OBJ_MODIFY_CMD_OUT,
		UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_out_cmd_hdr)),
		UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_OBJ_QUERY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_DEVX_OBJ_QUERY_HANDLE,
			UVERBS_IDR_ANY_OBJECT,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_DEVX_OBJ_QUERY_CMD_IN,
		UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_in_cmd_hdr)),
		UA_MANDATORY,
		UA_ALLOC_AND_COPY),
	UVERBS_ATTR_PTR_OUT(
		MLX5_IB_ATTR_DEVX_OBJ_QUERY_CMD_OUT,
		UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_out_cmd_hdr)),
		UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_OBJ_ASYNC_QUERY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_DEVX_OBJ_QUERY_HANDLE,
			UVERBS_IDR_ANY_OBJECT,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_DEVX_OBJ_QUERY_CMD_IN,
		UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_in_cmd_hdr)),
		UA_MANDATORY,
		UA_ALLOC_AND_COPY),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_DEVX_OBJ_QUERY_ASYNC_OUT_LEN,
		u16, UA_MANDATORY),
	UVERBS_ATTR_FD(MLX5_IB_ATTR_DEVX_OBJ_QUERY_ASYNC_FD,
		MLX5_IB_OBJECT_DEVX_ASYNC_CMD_FD,
		UVERBS_ACCESS_READ,
		UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_DEVX_OBJ_QUERY_ASYNC_WR_ID,
		UVERBS_ATTR_TYPE(u64),
		UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_SUBSCRIBE_EVENT,
	UVERBS_ATTR_FD(MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_FD_HANDLE,
		MLX5_IB_OBJECT_DEVX_ASYNC_EVENT_FD,
		UVERBS_ACCESS_READ,
		UA_MANDATORY),
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_OBJ_HANDLE,
		MLX5_IB_OBJECT_DEVX_OBJ,
		UVERBS_ACCESS_READ,
		UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_TYPE_NUM_LIST,
		UVERBS_ATTR_MIN_SIZE(sizeof(u16)),
		UA_MANDATORY,
		UA_ALLOC_AND_COPY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_COOKIE,
		UVERBS_ATTR_TYPE(u64),
		UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_DEVX_SUBSCRIBE_EVENT_FD_NUM,
		UVERBS_ATTR_TYPE(u32),
		UA_OPTIONAL));

DECLARE_UVERBS_GLOBAL_METHODS(MLX5_IB_OBJECT_DEVX,
			      &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_OTHER),
			      &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_QUERY_UAR),
			      &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_QUERY_EQN),
			      &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_SUBSCRIBE_EVENT));

DECLARE_UVERBS_NAMED_OBJECT(MLX5_IB_OBJECT_DEVX_OBJ,
			    UVERBS_TYPE_ALLOC_IDR(devx_obj_cleanup),
			    &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_OBJ_CREATE),
			    &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_OBJ_DESTROY),
			    &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_OBJ_MODIFY),
			    &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_OBJ_QUERY),
			    &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_OBJ_ASYNC_QUERY));

DECLARE_UVERBS_NAMED_OBJECT(MLX5_IB_OBJECT_DEVX_UMEM,
			    UVERBS_TYPE_ALLOC_IDR(devx_umem_cleanup),
			    &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_UMEM_REG),
			    &UVERBS_METHOD(MLX5_IB_METHOD_DEVX_UMEM_DEREG));


DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_ASYNC_CMD_FD_ALLOC,
	UVERBS_ATTR_FD(MLX5_IB_ATTR_DEVX_ASYNC_CMD_FD_ALLOC_HANDLE,
			MLX5_IB_OBJECT_DEVX_ASYNC_CMD_FD,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(
	MLX5_IB_OBJECT_DEVX_ASYNC_CMD_FD,
	UVERBS_TYPE_ALLOC_FD(sizeof(struct devx_async_cmd_event_file),
			     devx_async_cmd_event_destroy_uobj,
			     &devx_async_cmd_event_fops, "[devx_async_cmd]",
			     O_RDONLY),
	&UVERBS_METHOD(MLX5_IB_METHOD_DEVX_ASYNC_CMD_FD_ALLOC));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_DEVX_ASYNC_EVENT_FD_ALLOC,
	UVERBS_ATTR_FD(MLX5_IB_ATTR_DEVX_ASYNC_EVENT_FD_ALLOC_HANDLE,
			MLX5_IB_OBJECT_DEVX_ASYNC_EVENT_FD,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_FLAGS_IN(MLX5_IB_ATTR_DEVX_ASYNC_EVENT_FD_ALLOC_FLAGS,
			enum mlx5_ib_uapi_devx_create_event_channel_flags,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(
	MLX5_IB_OBJECT_DEVX_ASYNC_EVENT_FD,
	UVERBS_TYPE_ALLOC_FD(sizeof(struct devx_async_event_file),
			     devx_async_event_destroy_uobj,
			     &devx_async_event_fops, "[devx_async_event]",
			     O_RDONLY),
	&UVERBS_METHOD(MLX5_IB_METHOD_DEVX_ASYNC_EVENT_FD_ALLOC));

static bool devx_is_supported(struct ib_device *device)
{
	struct mlx5_ib_dev *dev = to_mdev(device);

	return MLX5_CAP_GEN(dev->mdev, log_max_uctx);
}

const struct uapi_definition mlx5_ib_devx_defs[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(
		MLX5_IB_OBJECT_DEVX,
		UAPI_DEF_IS_OBJ_SUPPORTED(devx_is_supported)),
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(
		MLX5_IB_OBJECT_DEVX_OBJ,
		UAPI_DEF_IS_OBJ_SUPPORTED(devx_is_supported)),
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(
		MLX5_IB_OBJECT_DEVX_UMEM,
		UAPI_DEF_IS_OBJ_SUPPORTED(devx_is_supported)),
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(
		MLX5_IB_OBJECT_DEVX_ASYNC_CMD_FD,
		UAPI_DEF_IS_OBJ_SUPPORTED(devx_is_supported)),
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(
		MLX5_IB_OBJECT_DEVX_ASYNC_EVENT_FD,
		UAPI_DEF_IS_OBJ_SUPPORTED(devx_is_supported)),
	{},
};
