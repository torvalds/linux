// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#include <rdma/uverbs_types.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/uverbs_std_types.h>
#include <rdma/mlx5_user_ioctl_cmds.h>
#include <rdma/mlx5_user_ioctl_verbs.h>
#include <rdma/ib_umem.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/fs.h>
#include "mlx5_ib.h"

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

static int
mlx5_ib_ft_type_to_namespace(enum mlx5_ib_uapi_flow_table_type table_type,
			     enum mlx5_flow_namespace_type *namespace)
{
	switch (table_type) {
	case MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_RX:
		*namespace = MLX5_FLOW_NAMESPACE_BYPASS;
		break;
	case MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_TX:
		*namespace = MLX5_FLOW_NAMESPACE_EGRESS;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct uverbs_attr_spec mlx5_ib_flow_type[] = {
	[MLX5_IB_FLOW_TYPE_NORMAL] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		.u.ptr = {
			.len = sizeof(u16), /* data is priority */
			.min_len = sizeof(u16),
		}
	},
	[MLX5_IB_FLOW_TYPE_SNIFFER] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
	[MLX5_IB_FLOW_TYPE_ALL_DEFAULT] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
	[MLX5_IB_FLOW_TYPE_MC_DEFAULT] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
};

#define MLX5_IB_CREATE_FLOW_MAX_FLOW_ACTIONS 2
static int UVERBS_HANDLER(MLX5_IB_METHOD_CREATE_FLOW)(
	struct uverbs_attr_bundle *attrs)
{
	struct mlx5_flow_act flow_act = {.flow_tag = MLX5_FS_DEFAULT_FLOW_TAG};
	struct mlx5_ib_flow_handler *flow_handler;
	struct mlx5_ib_flow_matcher *fs_matcher;
	struct ib_uobject **arr_flow_actions;
	struct ib_uflow_resources *uflow_res;
	void *devx_obj;
	int dest_id, dest_type;
	void *cmd_in;
	int inlen;
	bool dest_devx, dest_qp;
	struct ib_qp *qp = NULL;
	struct ib_uobject *uobj =
		uverbs_attr_get_uobject(attrs, MLX5_IB_ATTR_CREATE_FLOW_HANDLE);
	struct mlx5_ib_dev *dev = to_mdev(uobj->context->device);
	int len, ret, i;
	u32 counter_id = 0;

	if (!capable(CAP_NET_RAW))
		return -EPERM;

	dest_devx =
		uverbs_attr_is_valid(attrs, MLX5_IB_ATTR_CREATE_FLOW_DEST_DEVX);
	dest_qp = uverbs_attr_is_valid(attrs,
				       MLX5_IB_ATTR_CREATE_FLOW_DEST_QP);

	fs_matcher = uverbs_attr_get_obj(attrs,
					 MLX5_IB_ATTR_CREATE_FLOW_MATCHER);
	if (fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_BYPASS &&
	    ((dest_devx && dest_qp) || (!dest_devx && !dest_qp)))
		return -EINVAL;

	if (dest_devx) {
		devx_obj = uverbs_attr_get_obj(
			attrs, MLX5_IB_ATTR_CREATE_FLOW_DEST_DEVX);
		if (IS_ERR(devx_obj))
			return PTR_ERR(devx_obj);

		/* Verify that the given DEVX object is a flow
		 * steering destination.
		 */
		if (!mlx5_ib_devx_is_flow_dest(devx_obj, &dest_id, &dest_type))
			return -EINVAL;
	} else if (dest_qp) {
		struct mlx5_ib_qp *mqp;

		qp = uverbs_attr_get_obj(attrs,
					 MLX5_IB_ATTR_CREATE_FLOW_DEST_QP);
		if (IS_ERR(qp))
			return PTR_ERR(qp);

		if (qp->qp_type != IB_QPT_RAW_PACKET)
			return -EINVAL;

		mqp = to_mqp(qp);
		if (mqp->flags & MLX5_IB_QP_RSS)
			dest_id = mqp->rss_qp.tirn;
		else
			dest_id = mqp->raw_packet_qp.rq.tirn;
		dest_type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	} else {
		dest_type = MLX5_FLOW_DESTINATION_TYPE_PORT;
	}

	len = uverbs_attr_get_uobjs_arr(attrs,
		MLX5_IB_ATTR_CREATE_FLOW_ARR_COUNTERS_DEVX, &arr_flow_actions);
	if (len) {
		devx_obj = arr_flow_actions[0]->object;

		if (!mlx5_ib_devx_is_flow_counter(devx_obj, &counter_id))
			return -EINVAL;
		flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
	}

	if (dest_type == MLX5_FLOW_DESTINATION_TYPE_TIR &&
	    fs_matcher->ns_type == MLX5_FLOW_NAMESPACE_EGRESS)
		return -EINVAL;

	cmd_in = uverbs_attr_get_alloced_ptr(
		attrs, MLX5_IB_ATTR_CREATE_FLOW_MATCH_VALUE);
	inlen = uverbs_attr_get_len(attrs,
				    MLX5_IB_ATTR_CREATE_FLOW_MATCH_VALUE);

	uflow_res = flow_resources_alloc(MLX5_IB_CREATE_FLOW_MAX_FLOW_ACTIONS);
	if (!uflow_res)
		return -ENOMEM;

	len = uverbs_attr_get_uobjs_arr(attrs,
		MLX5_IB_ATTR_CREATE_FLOW_ARR_FLOW_ACTIONS, &arr_flow_actions);
	for (i = 0; i < len; i++) {
		struct mlx5_ib_flow_action *maction =
			to_mflow_act(arr_flow_actions[i]->object);

		ret = parse_flow_flow_action(maction, false, &flow_act);
		if (ret)
			goto err_out;
		flow_resources_add(uflow_res, IB_FLOW_SPEC_ACTION_HANDLE,
				   arr_flow_actions[i]->object);
	}

	ret = uverbs_copy_from(&flow_act.flow_tag, attrs,
			       MLX5_IB_ATTR_CREATE_FLOW_TAG);
	if (!ret) {
		if (flow_act.flow_tag >= BIT(24)) {
			ret = -EINVAL;
			goto err_out;
		}
		flow_act.flags |= FLOW_ACT_HAS_TAG;
	}

	flow_handler = mlx5_ib_raw_fs_rule_add(dev, fs_matcher, &flow_act,
					       counter_id,
					       cmd_in, inlen,
					       dest_id, dest_type);
	if (IS_ERR(flow_handler)) {
		ret = PTR_ERR(flow_handler);
		goto err_out;
	}

	ib_set_flow(uobj, &flow_handler->ibflow, qp, &dev->ib_dev, uflow_res);

	return 0;
err_out:
	ib_uverbs_flow_resources_free(uflow_res);
	return ret;
}

static int flow_matcher_cleanup(struct ib_uobject *uobject,
				enum rdma_remove_reason why,
				struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_flow_matcher *obj = uobject->object;
	int ret;

	ret = ib_destroy_usecnt(&obj->usecnt, why, uobject);
	if (ret)
		return ret;

	kfree(obj);
	return 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_FLOW_MATCHER_CREATE)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_FLOW_MATCHER_CREATE_HANDLE);
	struct mlx5_ib_dev *dev = to_mdev(uobj->context->device);
	struct mlx5_ib_flow_matcher *obj;
	u32 flags;
	int err;

	obj = kzalloc(sizeof(struct mlx5_ib_flow_matcher), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	obj->ns_type = MLX5_FLOW_NAMESPACE_BYPASS;
	obj->mask_len = uverbs_attr_get_len(
		attrs, MLX5_IB_ATTR_FLOW_MATCHER_MATCH_MASK);
	err = uverbs_copy_from(&obj->matcher_mask,
			       attrs,
			       MLX5_IB_ATTR_FLOW_MATCHER_MATCH_MASK);
	if (err)
		goto end;

	obj->flow_type = uverbs_attr_get_enum_id(
		attrs, MLX5_IB_ATTR_FLOW_MATCHER_FLOW_TYPE);

	if (obj->flow_type == MLX5_IB_FLOW_TYPE_NORMAL) {
		err = uverbs_copy_from(&obj->priority,
				       attrs,
				       MLX5_IB_ATTR_FLOW_MATCHER_FLOW_TYPE);
		if (err)
			goto end;
	}

	err = uverbs_copy_from(&obj->match_criteria_enable,
			       attrs,
			       MLX5_IB_ATTR_FLOW_MATCHER_MATCH_CRITERIA);
	if (err)
		goto end;

	err = uverbs_get_flags32(&flags, attrs,
				 MLX5_IB_ATTR_FLOW_MATCHER_FLOW_FLAGS,
				 IB_FLOW_ATTR_FLAGS_EGRESS);
	if (err)
		goto end;

	if (flags) {
		err = mlx5_ib_ft_type_to_namespace(
			MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_TX, &obj->ns_type);
		if (err)
			goto end;
	}

	uobj->object = obj;
	obj->mdev = dev->mdev;
	atomic_set(&obj->usecnt, 0);
	return 0;

end:
	kfree(obj);
	return err;
}

void mlx5_ib_destroy_flow_action_raw(struct mlx5_ib_flow_action *maction)
{
	switch (maction->flow_action_raw.sub_type) {
	case MLX5_IB_FLOW_ACTION_MODIFY_HEADER:
		mlx5_modify_header_dealloc(maction->flow_action_raw.dev->mdev,
					   maction->flow_action_raw.action_id);
		break;
	case MLX5_IB_FLOW_ACTION_PACKET_REFORMAT:
		mlx5_packet_reformat_dealloc(maction->flow_action_raw.dev->mdev,
			maction->flow_action_raw.action_id);
		break;
	case MLX5_IB_FLOW_ACTION_DECAP:
		break;
	default:
		break;
	}
}

static struct ib_flow_action *
mlx5_ib_create_modify_header(struct mlx5_ib_dev *dev,
			     enum mlx5_ib_uapi_flow_table_type ft_type,
			     u8 num_actions, void *in)
{
	enum mlx5_flow_namespace_type namespace;
	struct mlx5_ib_flow_action *maction;
	int ret;

	ret = mlx5_ib_ft_type_to_namespace(ft_type, &namespace);
	if (ret)
		return ERR_PTR(-EINVAL);

	maction = kzalloc(sizeof(*maction), GFP_KERNEL);
	if (!maction)
		return ERR_PTR(-ENOMEM);

	ret = mlx5_modify_header_alloc(dev->mdev, namespace, num_actions, in,
				       &maction->flow_action_raw.action_id);

	if (ret) {
		kfree(maction);
		return ERR_PTR(ret);
	}
	maction->flow_action_raw.sub_type =
		MLX5_IB_FLOW_ACTION_MODIFY_HEADER;
	maction->flow_action_raw.dev = dev;

	return &maction->ib_action;
}

static bool mlx5_ib_modify_header_supported(struct mlx5_ib_dev *dev)
{
	return MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev,
					 max_modify_header_actions) ||
	       MLX5_CAP_FLOWTABLE_NIC_TX(dev->mdev, max_modify_header_actions);
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_FLOW_ACTION_CREATE_MODIFY_HEADER)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_CREATE_MODIFY_HEADER_HANDLE);
	struct mlx5_ib_dev *mdev = to_mdev(uobj->context->device);
	enum mlx5_ib_uapi_flow_table_type ft_type;
	struct ib_flow_action *action;
	int num_actions;
	void *in;
	int ret;

	if (!mlx5_ib_modify_header_supported(mdev))
		return -EOPNOTSUPP;

	in = uverbs_attr_get_alloced_ptr(attrs,
		MLX5_IB_ATTR_CREATE_MODIFY_HEADER_ACTIONS_PRM);

	num_actions = uverbs_attr_ptr_get_array_size(
		attrs, MLX5_IB_ATTR_CREATE_MODIFY_HEADER_ACTIONS_PRM,
		MLX5_UN_SZ_BYTES(set_action_in_add_action_in_auto));
	if (num_actions < 0)
		return num_actions;

	ret = uverbs_get_const(&ft_type, attrs,
			       MLX5_IB_ATTR_CREATE_MODIFY_HEADER_FT_TYPE);
	if (ret)
		return ret;
	action = mlx5_ib_create_modify_header(mdev, ft_type, num_actions, in);
	if (IS_ERR(action))
		return PTR_ERR(action);

	uverbs_flow_action_fill_action(action, uobj, uobj->context->device,
				       IB_FLOW_ACTION_UNSPECIFIED);

	return 0;
}

static bool mlx5_ib_flow_action_packet_reformat_valid(struct mlx5_ib_dev *ibdev,
						      u8 packet_reformat_type,
						      u8 ft_type)
{
	switch (packet_reformat_type) {
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L2_TUNNEL:
		if (ft_type == MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_TX)
			return MLX5_CAP_FLOWTABLE(ibdev->mdev,
						  encap_general_header);
		break;
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L3_TUNNEL:
		if (ft_type == MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_TX)
			return MLX5_CAP_FLOWTABLE_NIC_TX(ibdev->mdev,
				reformat_l2_to_l3_tunnel);
		break;
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L3_TUNNEL_TO_L2:
		if (ft_type == MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_RX)
			return MLX5_CAP_FLOWTABLE_NIC_RX(ibdev->mdev,
				reformat_l3_tunnel_to_l2);
		break;
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TUNNEL_TO_L2:
		if (ft_type == MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_RX)
			return MLX5_CAP_FLOWTABLE_NIC_RX(ibdev->mdev, decap);
		break;
	default:
		break;
	}

	return false;
}

static int mlx5_ib_dv_to_prm_packet_reforamt_type(u8 dv_prt, u8 *prm_prt)
{
	switch (dv_prt) {
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L2_TUNNEL:
		*prm_prt = MLX5_REFORMAT_TYPE_L2_TO_L2_TUNNEL;
		break;
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L3_TUNNEL_TO_L2:
		*prm_prt = MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2;
		break;
	case MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L3_TUNNEL:
		*prm_prt = MLX5_REFORMAT_TYPE_L2_TO_L3_TUNNEL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mlx5_ib_flow_action_create_packet_reformat_ctx(
	struct mlx5_ib_dev *dev,
	struct mlx5_ib_flow_action *maction,
	u8 ft_type, u8 dv_prt,
	void *in, size_t len)
{
	enum mlx5_flow_namespace_type namespace;
	u8 prm_prt;
	int ret;

	ret = mlx5_ib_ft_type_to_namespace(ft_type, &namespace);
	if (ret)
		return ret;

	ret = mlx5_ib_dv_to_prm_packet_reforamt_type(dv_prt, &prm_prt);
	if (ret)
		return ret;

	ret = mlx5_packet_reformat_alloc(dev->mdev, prm_prt, len,
					 in, namespace,
					 &maction->flow_action_raw.action_id);
	if (ret)
		return ret;

	maction->flow_action_raw.sub_type =
		MLX5_IB_FLOW_ACTION_PACKET_REFORMAT;
	maction->flow_action_raw.dev = dev;

	return 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_FLOW_ACTION_CREATE_PACKET_REFORMAT)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(attrs,
		MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_HANDLE);
	struct mlx5_ib_dev *mdev = to_mdev(uobj->context->device);
	enum mlx5_ib_uapi_flow_action_packet_reformat_type dv_prt;
	enum mlx5_ib_uapi_flow_table_type ft_type;
	struct mlx5_ib_flow_action *maction;
	int ret;

	ret = uverbs_get_const(&ft_type, attrs,
			       MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_FT_TYPE);
	if (ret)
		return ret;

	ret = uverbs_get_const(&dv_prt, attrs,
			       MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_TYPE);
	if (ret)
		return ret;

	if (!mlx5_ib_flow_action_packet_reformat_valid(mdev, dv_prt, ft_type))
		return -EOPNOTSUPP;

	maction = kzalloc(sizeof(*maction), GFP_KERNEL);
	if (!maction)
		return -ENOMEM;

	if (dv_prt ==
	    MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TUNNEL_TO_L2) {
		maction->flow_action_raw.sub_type =
			MLX5_IB_FLOW_ACTION_DECAP;
		maction->flow_action_raw.dev = mdev;
	} else {
		void *in;
		int len;

		in = uverbs_attr_get_alloced_ptr(attrs,
			MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_DATA_BUF);
		if (IS_ERR(in)) {
			ret = PTR_ERR(in);
			goto free_maction;
		}

		len = uverbs_attr_get_len(attrs,
			MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_DATA_BUF);

		ret = mlx5_ib_flow_action_create_packet_reformat_ctx(mdev,
			maction, ft_type, dv_prt, in, len);
		if (ret)
			goto free_maction;
	}

	uverbs_flow_action_fill_action(&maction->ib_action, uobj,
				       uobj->context->device,
				       IB_FLOW_ACTION_UNSPECIFIED);
	return 0;

free_maction:
	kfree(maction);
	return ret;
}

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_CREATE_FLOW,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_FLOW_HANDLE,
			UVERBS_OBJECT_FLOW,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_CREATE_FLOW_MATCH_VALUE,
		UVERBS_ATTR_SIZE(1, sizeof(struct mlx5_ib_match_params)),
		UA_MANDATORY,
		UA_ALLOC_AND_COPY),
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_FLOW_MATCHER,
			MLX5_IB_OBJECT_FLOW_MATCHER,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_FLOW_DEST_QP,
			UVERBS_OBJECT_QP,
			UVERBS_ACCESS_READ),
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_FLOW_DEST_DEVX,
			MLX5_IB_OBJECT_DEVX_OBJ,
			UVERBS_ACCESS_READ),
	UVERBS_ATTR_IDRS_ARR(MLX5_IB_ATTR_CREATE_FLOW_ARR_FLOW_ACTIONS,
			     UVERBS_OBJECT_FLOW_ACTION,
			     UVERBS_ACCESS_READ, 1,
			     MLX5_IB_CREATE_FLOW_MAX_FLOW_ACTIONS,
			     UA_OPTIONAL),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_CREATE_FLOW_TAG,
			   UVERBS_ATTR_TYPE(u32),
			   UA_OPTIONAL),
	UVERBS_ATTR_IDRS_ARR(MLX5_IB_ATTR_CREATE_FLOW_ARR_COUNTERS_DEVX,
			     MLX5_IB_OBJECT_DEVX_OBJ,
			     UVERBS_ACCESS_READ, 1, 1,
			     UA_OPTIONAL));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	MLX5_IB_METHOD_DESTROY_FLOW,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_FLOW_HANDLE,
			UVERBS_OBJECT_FLOW,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

ADD_UVERBS_METHODS(mlx5_ib_fs,
		   UVERBS_OBJECT_FLOW,
		   &UVERBS_METHOD(MLX5_IB_METHOD_CREATE_FLOW),
		   &UVERBS_METHOD(MLX5_IB_METHOD_DESTROY_FLOW));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_FLOW_ACTION_CREATE_MODIFY_HEADER,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_MODIFY_HEADER_HANDLE,
			UVERBS_OBJECT_FLOW_ACTION,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_CREATE_MODIFY_HEADER_ACTIONS_PRM,
			   UVERBS_ATTR_MIN_SIZE(MLX5_UN_SZ_BYTES(
				   set_action_in_add_action_in_auto)),
			   UA_MANDATORY,
			   UA_ALLOC_AND_COPY),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_CREATE_MODIFY_HEADER_FT_TYPE,
			     enum mlx5_ib_uapi_flow_table_type,
			     UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_FLOW_ACTION_CREATE_PACKET_REFORMAT,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_HANDLE,
			UVERBS_OBJECT_FLOW_ACTION,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_DATA_BUF,
			   UVERBS_ATTR_MIN_SIZE(1),
			   UA_ALLOC_AND_COPY,
			   UA_OPTIONAL),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_TYPE,
			     enum mlx5_ib_uapi_flow_action_packet_reformat_type,
			     UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_CREATE_PACKET_REFORMAT_FT_TYPE,
			     enum mlx5_ib_uapi_flow_table_type,
			     UA_MANDATORY));

ADD_UVERBS_METHODS(
	mlx5_ib_flow_actions,
	UVERBS_OBJECT_FLOW_ACTION,
	&UVERBS_METHOD(MLX5_IB_METHOD_FLOW_ACTION_CREATE_MODIFY_HEADER),
	&UVERBS_METHOD(MLX5_IB_METHOD_FLOW_ACTION_CREATE_PACKET_REFORMAT));

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_FLOW_MATCHER_CREATE,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_FLOW_MATCHER_CREATE_HANDLE,
			MLX5_IB_OBJECT_FLOW_MATCHER,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_FLOW_MATCHER_MATCH_MASK,
		UVERBS_ATTR_SIZE(1, sizeof(struct mlx5_ib_match_params)),
		UA_MANDATORY),
	UVERBS_ATTR_ENUM_IN(MLX5_IB_ATTR_FLOW_MATCHER_FLOW_TYPE,
			    mlx5_ib_flow_type,
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_FLOW_MATCHER_MATCH_CRITERIA,
			   UVERBS_ATTR_TYPE(u8),
			   UA_MANDATORY),
	UVERBS_ATTR_FLAGS_IN(MLX5_IB_ATTR_FLOW_MATCHER_FLOW_FLAGS,
			     enum ib_flow_flags,
			     UA_OPTIONAL));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	MLX5_IB_METHOD_FLOW_MATCHER_DESTROY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_FLOW_MATCHER_DESTROY_HANDLE,
			MLX5_IB_OBJECT_FLOW_MATCHER,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(MLX5_IB_OBJECT_FLOW_MATCHER,
			    UVERBS_TYPE_ALLOC_IDR(flow_matcher_cleanup),
			    &UVERBS_METHOD(MLX5_IB_METHOD_FLOW_MATCHER_CREATE),
			    &UVERBS_METHOD(MLX5_IB_METHOD_FLOW_MATCHER_DESTROY));

static bool flow_is_supported(struct ib_device *device)
{
	return !to_mdev(device)->rep;
}

const struct uapi_definition mlx5_ib_flow_defs[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(
		MLX5_IB_OBJECT_FLOW_MATCHER,
		UAPI_DEF_IS_OBJ_SUPPORTED(flow_is_supported)),
	UAPI_DEF_CHAIN_OBJ_TREE(
		UVERBS_OBJECT_FLOW,
		&mlx5_ib_fs),
	UAPI_DEF_CHAIN_OBJ_TREE(UVERBS_OBJECT_FLOW_ACTION,
				&mlx5_ib_flow_actions),
	{},
};
