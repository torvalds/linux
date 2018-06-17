// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#include <rdma/uverbs_types.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/mlx5_user_ioctl_cmds.h>
#include <rdma/ib_umem.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/fs.h>
#include "mlx5_ib.h"

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

#define MLX5_MAX_DESTROY_INBOX_SIZE_DW MLX5_ST_SZ_DW(delete_fte_in)
struct devx_obj {
	struct mlx5_core_dev	*mdev;
	u32			obj_id;
	u32			dinlen; /* destroy inbox length */
	u32			dinbox[MLX5_MAX_DESTROY_INBOX_SIZE_DW];
};

static struct mlx5_ib_ucontext *devx_ufile2uctx(struct ib_uverbs_file *file)
{
	return to_mucontext(ib_uverbs_get_ucontext(file));
}

int mlx5_ib_devx_create(struct mlx5_ib_dev *dev, struct mlx5_ib_ucontext *context)
{
	u32 in[MLX5_ST_SZ_DW(create_uctx_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};
	u64 general_obj_types;
	void *uctx;
	void *hdr;
	int err;

	uctx = MLX5_ADDR_OF(create_uctx_in, in, uctx);
	hdr = MLX5_ADDR_OF(create_uctx_in, in, hdr);

	general_obj_types = MLX5_CAP_GEN_64(dev->mdev, general_obj_types);
	if (!(general_obj_types & MLX5_GENERAL_OBJ_TYPES_CAP_UCTX) ||
	    !(general_obj_types & MLX5_GENERAL_OBJ_TYPES_CAP_UMEM))
		return -EINVAL;

	if (!capable(CAP_NET_RAW))
		return -EPERM;

	MLX5_SET(general_obj_in_cmd_hdr, hdr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_type, MLX5_OBJ_TYPE_UCTX);

	err = mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	context->devx_uid = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
	return 0;
}

void mlx5_ib_devx_destroy(struct mlx5_ib_dev *dev,
			  struct mlx5_ib_ucontext *context)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {0};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {0};

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_OBJ_TYPE_UCTX);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, context->devx_uid);

	mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
}

static bool devx_is_obj_create_cmd(const void *in)
{
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);

	switch (opcode) {
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
	case MLX5_CMD_OP_ALLOC_ENCAP_HEADER:
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
	default:
		return false;
	}
}

static bool devx_is_general_cmd(const void *in)
{
	u16 opcode = MLX5_GET(general_obj_in_cmd_hdr, in, opcode);

	switch (opcode) {
	case MLX5_CMD_OP_QUERY_HCA_CAP:
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
		return true;
	default:
		return false;
	}
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_OTHER)(struct ib_device *ib_dev,
				  struct ib_uverbs_file *file,
				  struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_ucontext *c = devx_ufile2uctx(file);
	struct mlx5_ib_dev *dev = to_mdev(ib_dev);
	void *cmd_in = uverbs_attr_get_alloced_ptr(
		attrs, MLX5_IB_ATTR_DEVX_OTHER_CMD_IN);
	int cmd_out_len = uverbs_attr_get_len(attrs,
					MLX5_IB_ATTR_DEVX_OTHER_CMD_OUT);
	void *cmd_out;
	int err;

	if (!c->devx_uid)
		return -EPERM;

	/* Only white list of some general HCA commands are allowed for this method. */
	if (!devx_is_general_cmd(cmd_in))
		return -EINVAL;

	cmd_out = kvzalloc(cmd_out_len, GFP_KERNEL);
	if (!cmd_out)
		return -ENOMEM;

	MLX5_SET(general_obj_in_cmd_hdr, cmd_in, uid, c->devx_uid);
	err = mlx5_cmd_exec(dev->mdev, cmd_in,
			    uverbs_attr_get_len(attrs, MLX5_IB_ATTR_DEVX_OTHER_CMD_IN),
			    cmd_out, cmd_out_len);
	if (err)
		goto other_cmd_free;

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_DEVX_OTHER_CMD_OUT, cmd_out, cmd_out_len);

other_cmd_free:
	kvfree(cmd_out);
	return err;
}

static void devx_obj_build_destroy_cmd(void *in, void *out, void *din,
				       u32 *dinlen,
				       u32 *obj_id)
{
	u16 obj_type = MLX5_GET(general_obj_in_cmd_hdr, in, obj_type);
	u16 uid = MLX5_GET(general_obj_in_cmd_hdr, in, uid);

	*obj_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
	*dinlen = MLX5_ST_SZ_BYTES(general_obj_in_cmd_hdr);

	MLX5_SET(general_obj_in_cmd_hdr, din, obj_id, *obj_id);
	MLX5_SET(general_obj_in_cmd_hdr, din, uid, uid);

	switch (MLX5_GET(general_obj_in_cmd_hdr, in, opcode)) {
	case MLX5_CMD_OP_CREATE_GENERAL_OBJECT:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
		MLX5_SET(general_obj_in_cmd_hdr, din, obj_type, obj_type);
		break;

	case MLX5_CMD_OP_CREATE_MKEY:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_MKEY);
		break;
	case MLX5_CMD_OP_CREATE_CQ:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_CQ);
		break;
	case MLX5_CMD_OP_ALLOC_PD:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DEALLOC_PD);
		break;
	case MLX5_CMD_OP_ALLOC_TRANSPORT_DOMAIN:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DEALLOC_TRANSPORT_DOMAIN);
		break;
	case MLX5_CMD_OP_CREATE_RMP:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_RMP);
		break;
	case MLX5_CMD_OP_CREATE_SQ:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_SQ);
		break;
	case MLX5_CMD_OP_CREATE_RQ:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_RQ);
		break;
	case MLX5_CMD_OP_CREATE_RQT:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_RQT);
		break;
	case MLX5_CMD_OP_CREATE_TIR:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_TIR);
		break;
	case MLX5_CMD_OP_CREATE_TIS:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_TIS);
		break;
	case MLX5_CMD_OP_ALLOC_Q_COUNTER:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DEALLOC_Q_COUNTER);
		break;
	case MLX5_CMD_OP_CREATE_FLOW_TABLE:
		*dinlen = MLX5_ST_SZ_BYTES(destroy_flow_table_in);
		*obj_id = MLX5_GET(create_flow_table_out, out, table_id);
		MLX5_SET(destroy_flow_table_in, din, other_vport,
			 MLX5_GET(create_flow_table_in,  in, other_vport));
		MLX5_SET(destroy_flow_table_in, din, vport_number,
			 MLX5_GET(create_flow_table_in,  in, vport_number));
		MLX5_SET(destroy_flow_table_in, din, table_type,
			 MLX5_GET(create_flow_table_in,  in, table_type));
		MLX5_SET(destroy_flow_table_in, din, table_id, *obj_id);
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DESTROY_FLOW_TABLE);
		break;
	case MLX5_CMD_OP_CREATE_FLOW_GROUP:
		*dinlen = MLX5_ST_SZ_BYTES(destroy_flow_group_in);
		*obj_id = MLX5_GET(create_flow_group_out, out, group_id);
		MLX5_SET(destroy_flow_group_in, din, other_vport,
			 MLX5_GET(create_flow_group_in, in, other_vport));
		MLX5_SET(destroy_flow_group_in, din, vport_number,
			 MLX5_GET(create_flow_group_in, in, vport_number));
		MLX5_SET(destroy_flow_group_in, din, table_type,
			 MLX5_GET(create_flow_group_in, in, table_type));
		MLX5_SET(destroy_flow_group_in, din, table_id,
			 MLX5_GET(create_flow_group_in, in, table_id));
		MLX5_SET(destroy_flow_group_in, din, group_id, *obj_id);
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DESTROY_FLOW_GROUP);
		break;
	case MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY:
		*dinlen = MLX5_ST_SZ_BYTES(delete_fte_in);
		*obj_id = MLX5_GET(set_fte_in, in, flow_index);
		MLX5_SET(delete_fte_in, din, other_vport,
			 MLX5_GET(set_fte_in,  in, other_vport));
		MLX5_SET(delete_fte_in, din, vport_number,
			 MLX5_GET(set_fte_in, in, vport_number));
		MLX5_SET(delete_fte_in, din, table_type,
			 MLX5_GET(set_fte_in, in, table_type));
		MLX5_SET(delete_fte_in, din, table_id,
			 MLX5_GET(set_fte_in, in, table_id));
		MLX5_SET(delete_fte_in, din, flow_index, *obj_id);
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY);
		break;
	case MLX5_CMD_OP_ALLOC_FLOW_COUNTER:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DEALLOC_FLOW_COUNTER);
		break;
	case MLX5_CMD_OP_ALLOC_ENCAP_HEADER:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DEALLOC_ENCAP_HEADER);
		break;
	case MLX5_CMD_OP_ALLOC_MODIFY_HEADER_CONTEXT:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DEALLOC_MODIFY_HEADER_CONTEXT);
		break;
	case MLX5_CMD_OP_CREATE_SCHEDULING_ELEMENT:
		*dinlen = MLX5_ST_SZ_BYTES(destroy_scheduling_element_in);
		*obj_id = MLX5_GET(create_scheduling_element_out, out,
				   scheduling_element_id);
		MLX5_SET(destroy_scheduling_element_in, din,
			 scheduling_hierarchy,
			 MLX5_GET(create_scheduling_element_in, in,
				  scheduling_hierarchy));
		MLX5_SET(destroy_scheduling_element_in, din,
			 scheduling_element_id, *obj_id);
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DESTROY_SCHEDULING_ELEMENT);
		break;
	case MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT:
		*dinlen = MLX5_ST_SZ_BYTES(delete_vxlan_udp_dport_in);
		*obj_id = MLX5_GET(add_vxlan_udp_dport_in, in, vxlan_udp_port);
		MLX5_SET(delete_vxlan_udp_dport_in, din, vxlan_udp_port, *obj_id);
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DELETE_VXLAN_UDP_DPORT);
		break;
	case MLX5_CMD_OP_SET_L2_TABLE_ENTRY:
		*dinlen = MLX5_ST_SZ_BYTES(delete_l2_table_entry_in);
		*obj_id = MLX5_GET(set_l2_table_entry_in, in, table_index);
		MLX5_SET(delete_l2_table_entry_in, din, table_index, *obj_id);
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DELETE_L2_TABLE_ENTRY);
		break;
	case MLX5_CMD_OP_CREATE_QP:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_QP);
		break;
	case MLX5_CMD_OP_CREATE_SRQ:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_SRQ);
		break;
	case MLX5_CMD_OP_CREATE_XRC_SRQ:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode,
			 MLX5_CMD_OP_DESTROY_XRC_SRQ);
		break;
	case MLX5_CMD_OP_CREATE_DCT:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_DCT);
		break;
	case MLX5_CMD_OP_CREATE_XRQ:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DESTROY_XRQ);
		break;
	case MLX5_CMD_OP_ATTACH_TO_MCG:
		*dinlen = MLX5_ST_SZ_BYTES(detach_from_mcg_in);
		MLX5_SET(detach_from_mcg_in, din, qpn,
			 MLX5_GET(attach_to_mcg_in, in, qpn));
		memcpy(MLX5_ADDR_OF(detach_from_mcg_in, din, multicast_gid),
		       MLX5_ADDR_OF(attach_to_mcg_in, in, multicast_gid),
		       MLX5_FLD_SZ_BYTES(attach_to_mcg_in, multicast_gid));
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DETACH_FROM_MCG);
		break;
	case MLX5_CMD_OP_ALLOC_XRCD:
		MLX5_SET(general_obj_in_cmd_hdr, din, opcode, MLX5_CMD_OP_DEALLOC_XRCD);
		break;
	default:
		/* The entry must match to one of the devx_is_obj_create_cmd */
		WARN_ON(true);
		break;
	}
}

static int devx_obj_cleanup(struct ib_uobject *uobject,
			    enum rdma_remove_reason why)
{
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	struct devx_obj *obj = uobject->object;
	int ret;

	ret = mlx5_cmd_exec(obj->mdev, obj->dinbox, obj->dinlen, out, sizeof(out));
	if (ret && why == RDMA_REMOVE_DESTROY)
		return ret;

	kfree(obj);
	return ret;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_OBJ_DESTROY)(struct ib_device *ib_dev,
				    struct ib_uverbs_file *file,
				    struct uverbs_attr_bundle *attrs)
{
	return 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_DEVX_OBJ_CREATE)(struct ib_device *ib_dev,
				   struct ib_uverbs_file *file,
				   struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_ucontext *c = devx_ufile2uctx(file);
	struct mlx5_ib_dev *dev = to_mdev(ib_dev);
	void *cmd_in = uverbs_attr_get_alloced_ptr(attrs, MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_IN);
	int cmd_out_len =  uverbs_attr_get_len(attrs,
					MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_OUT);
	void *cmd_out;
	struct ib_uobject *uobj;
	struct devx_obj *obj;
	int err;

	if (!c->devx_uid)
		return -EPERM;

	if (!devx_is_obj_create_cmd(cmd_in))
		return -EINVAL;

	obj = kzalloc(sizeof(struct devx_obj), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	cmd_out = kvzalloc(cmd_out_len, GFP_KERNEL);
	if (!cmd_out) {
		err = -ENOMEM;
		goto obj_free;
	}

	MLX5_SET(general_obj_in_cmd_hdr, cmd_in, uid, c->devx_uid);
	err = mlx5_cmd_exec(dev->mdev, cmd_in,
			    uverbs_attr_get_len(attrs, MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_IN),
			    cmd_out, cmd_out_len);
	if (err)
		goto cmd_free;

	uobj = uverbs_attr_get_uobject(attrs, MLX5_IB_ATTR_DEVX_OBJ_CREATE_HANDLE);
	uobj->object = obj;
	obj->mdev = dev->mdev;
	devx_obj_build_destroy_cmd(cmd_in, cmd_out, obj->dinbox, &obj->dinlen, &obj->obj_id);
	WARN_ON(obj->dinlen > MLX5_MAX_DESTROY_INBOX_SIZE_DW * sizeof(u32));

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_OUT, cmd_out, cmd_out_len);
	if (err)
		goto cmd_free;

	kvfree(cmd_out);
	return 0;

cmd_free:
	kvfree(cmd_out);
obj_free:
	kfree(obj);
	return err;
}

static DECLARE_UVERBS_NAMED_METHOD(MLX5_IB_METHOD_DEVX_OTHER,
	&UVERBS_ATTR_PTR_IN_SZ(MLX5_IB_ATTR_DEVX_OTHER_CMD_IN,
			       UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_in_cmd_hdr)),
			       UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY |
					UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO |
					UVERBS_ATTR_SPEC_F_ALLOC_AND_COPY)),
	&UVERBS_ATTR_PTR_OUT_SZ(MLX5_IB_ATTR_DEVX_OTHER_CMD_OUT,
				UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_out_cmd_hdr)),
				UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY |
					 UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO))
);

static DECLARE_UVERBS_NAMED_METHOD(MLX5_IB_METHOD_DEVX_OBJ_CREATE,
	&UVERBS_ATTR_IDR(MLX5_IB_ATTR_DEVX_OBJ_CREATE_HANDLE,
			 MLX5_IB_OBJECT_DEVX_OBJ,
			 UVERBS_ACCESS_NEW,
			 UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY)),
	&UVERBS_ATTR_PTR_IN_SZ(MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_IN,
			       UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_in_cmd_hdr)),
			       UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY |
					UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO |
					UVERBS_ATTR_SPEC_F_ALLOC_AND_COPY)),
	&UVERBS_ATTR_PTR_OUT_SZ(MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_OUT,
				UVERBS_ATTR_MIN_SIZE(MLX5_ST_SZ_BYTES(general_obj_out_cmd_hdr)),
				UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY |
					 UVERBS_ATTR_SPEC_F_MIN_SZ_OR_ZERO)));

static DECLARE_UVERBS_NAMED_METHOD(MLX5_IB_METHOD_DEVX_OBJ_DESTROY,
	&UVERBS_ATTR_IDR(MLX5_IB_ATTR_DEVX_OBJ_DESTROY_HANDLE,
			 MLX5_IB_OBJECT_DEVX_OBJ,
			 UVERBS_ACCESS_DESTROY,
			 UA_FLAGS(UVERBS_ATTR_SPEC_F_MANDATORY)));

static DECLARE_UVERBS_GLOBAL_METHODS(MLX5_IB_OBJECT_DEVX,
	&UVERBS_METHOD(MLX5_IB_METHOD_DEVX_OTHER));

static DECLARE_UVERBS_NAMED_OBJECT(MLX5_IB_OBJECT_DEVX_OBJ,
	&UVERBS_TYPE_ALLOC_IDR(0, devx_obj_cleanup),
		&UVERBS_METHOD(MLX5_IB_METHOD_DEVX_OBJ_CREATE),
		&UVERBS_METHOD(MLX5_IB_METHOD_DEVX_OBJ_DESTROY));

static DECLARE_UVERBS_OBJECT_TREE(devx_objects,
	&UVERBS_OBJECT(MLX5_IB_OBJECT_DEVX),
	&UVERBS_OBJECT(MLX5_IB_OBJECT_DEVX_OBJ));
