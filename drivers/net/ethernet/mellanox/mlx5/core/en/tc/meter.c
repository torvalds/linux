// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "lib/aso.h"
#include "en/tc/post_act.h"
#include "meter.h"
#include "en/tc_priv.h"
#include "post_meter.h"

#define MLX5_START_COLOR_SHIFT 28
#define MLX5_METER_MODE_SHIFT 24
#define MLX5_CBS_EXP_SHIFT 24
#define MLX5_CBS_MAN_SHIFT 16
#define MLX5_CIR_EXP_SHIFT 8

/* cir = 8*(10^9)*cir_mantissa/(2^cir_exponent)) bits/s */
#define MLX5_CONST_CIR 8000000000ULL
#define MLX5_CALC_CIR(m, e)  ((MLX5_CONST_CIR * (m)) >> (e))
#define MLX5_MAX_CIR ((MLX5_CONST_CIR * 0x100) - 1)

/* cbs = cbs_mantissa*2^cbs_exponent */
#define MLX5_CALC_CBS(m, e)  ((m) << (e))
#define MLX5_MAX_CBS ((0x100ULL << 0x1F) - 1)
#define MLX5_MAX_HW_CBS 0x7FFFFFFF

struct mlx5e_flow_meter_aso_obj {
	struct list_head entry;
	int base_id;
	int total_meters;

	unsigned long meters_map[0]; /* must be at the end of this struct */
};

struct mlx5e_flow_meters {
	enum mlx5_flow_namespace_type ns_type;
	struct mlx5_aso *aso;
	struct mutex aso_lock; /* Protects aso operations */
	int log_granularity;
	u32 pdn;

	DECLARE_HASHTABLE(hashtbl, 8);

	struct mutex sync_lock; /* protect flow meter operations */
	struct list_head partial_list;
	struct list_head full_list;

	struct mlx5_core_dev *mdev;
	struct mlx5e_post_act *post_act;

	struct mlx5e_post_meter_priv *post_meter;
};

static void
mlx5e_flow_meter_cir_calc(u64 cir, u8 *man, u8 *exp)
{
	s64 _cir, _delta, delta = S64_MAX;
	u8 e, _man = 0, _exp = 0;
	u64 m;

	for (e = 0; e <= 0x1F; e++) { /* exp width 5bit */
		m = cir << e;
		if ((s64)m < 0) /* overflow */
			break;
		m /= MLX5_CONST_CIR;
		if (m > 0xFF) /* man width 8 bit */
			continue;
		_cir = MLX5_CALC_CIR(m, e);
		_delta = cir - _cir;
		if (_delta < delta) {
			_man = m;
			_exp = e;
			if (!_delta)
				goto found;
			delta = _delta;
		}
	}

found:
	*man = _man;
	*exp = _exp;
}

static void
mlx5e_flow_meter_cbs_calc(u64 cbs, u8 *man, u8 *exp)
{
	s64 _cbs, _delta, delta = S64_MAX;
	u8 e, _man = 0, _exp = 0;
	u64 m;

	for (e = 0; e <= 0x1F; e++) { /* exp width 5bit */
		m = cbs >> e;
		if (m > 0xFF) /* man width 8 bit */
			continue;
		_cbs = MLX5_CALC_CBS(m, e);
		_delta = cbs - _cbs;
		if (_delta < delta) {
			_man = m;
			_exp = e;
			if (!_delta)
				goto found;
			delta = _delta;
		}
	}

found:
	*man = _man;
	*exp = _exp;
}

int
mlx5e_tc_meter_modify(struct mlx5_core_dev *mdev,
		      struct mlx5e_flow_meter_handle *meter,
		      struct mlx5e_flow_meter_params *meter_params)
{
	struct mlx5_wqe_aso_ctrl_seg *aso_ctrl;
	struct mlx5_wqe_aso_data_seg *aso_data;
	struct mlx5e_flow_meters *flow_meters;
	u8 cir_man, cir_exp, cbs_man, cbs_exp;
	struct mlx5_aso_wqe *aso_wqe;
	struct mlx5_aso *aso;
	u64 rate, burst;
	u8 ds_cnt;
	int err;

	rate = meter_params->rate;
	burst = meter_params->burst;

	/* HW treats each packet as 128 bytes in PPS mode */
	if (meter_params->mode == MLX5_RATE_LIMIT_PPS) {
		rate <<= 10;
		burst <<= 7;
	}

	if (!rate || rate > MLX5_MAX_CIR || !burst || burst > MLX5_MAX_CBS)
		return -EINVAL;

	/* HW has limitation of total 31 bits for cbs */
	if (burst > MLX5_MAX_HW_CBS) {
		mlx5_core_warn(mdev,
			       "burst(%lld) is too large, use HW allowed value(%d)\n",
			       burst, MLX5_MAX_HW_CBS);
		burst = MLX5_MAX_HW_CBS;
	}

	mlx5_core_dbg(mdev, "meter mode=%d\n", meter_params->mode);
	mlx5e_flow_meter_cir_calc(rate, &cir_man, &cir_exp);
	mlx5_core_dbg(mdev, "rate=%lld, cir=%lld, exp=%d, man=%d\n",
		      rate, MLX5_CALC_CIR(cir_man, cir_exp), cir_exp, cir_man);
	mlx5e_flow_meter_cbs_calc(burst, &cbs_man, &cbs_exp);
	mlx5_core_dbg(mdev, "burst=%lld, cbs=%lld, exp=%d, man=%d\n",
		      burst, MLX5_CALC_CBS((u64)cbs_man, cbs_exp), cbs_exp, cbs_man);

	if (!cir_man || !cbs_man)
		return -EINVAL;

	flow_meters = meter->flow_meters;
	aso = flow_meters->aso;

	mutex_lock(&flow_meters->aso_lock);
	aso_wqe = mlx5_aso_get_wqe(aso);
	ds_cnt = DIV_ROUND_UP(sizeof(struct mlx5_aso_wqe_data), MLX5_SEND_WQE_DS);
	mlx5_aso_build_wqe(aso, ds_cnt, aso_wqe, meter->obj_id,
			   MLX5_ACCESS_ASO_OPC_MOD_FLOW_METER);

	aso_ctrl = &aso_wqe->aso_ctrl;
	memset(aso_ctrl, 0, sizeof(*aso_ctrl));
	aso_ctrl->data_mask_mode = MLX5_ASO_DATA_MASK_MODE_BYTEWISE_64BYTE << 6;
	aso_ctrl->condition_1_0_operand = MLX5_ASO_ALWAYS_TRUE |
					  MLX5_ASO_ALWAYS_TRUE << 4;
	aso_ctrl->data_offset_condition_operand = MLX5_ASO_LOGICAL_OR << 6;
	aso_ctrl->data_mask = cpu_to_be64(0x80FFFFFFULL << (meter->idx ? 0 : 32));

	aso_data = (struct mlx5_wqe_aso_data_seg *)(aso_wqe + 1);
	memset(aso_data, 0, sizeof(*aso_data));
	aso_data->bytewise_data[meter->idx * 8] = cpu_to_be32((0x1 << 31) | /* valid */
					(MLX5_FLOW_METER_COLOR_GREEN << MLX5_START_COLOR_SHIFT));
	if (meter_params->mode == MLX5_RATE_LIMIT_PPS)
		aso_data->bytewise_data[meter->idx * 8] |=
			cpu_to_be32(MLX5_FLOW_METER_MODE_NUM_PACKETS << MLX5_METER_MODE_SHIFT);
	else
		aso_data->bytewise_data[meter->idx * 8] |=
			cpu_to_be32(MLX5_FLOW_METER_MODE_BYTES_IP_LENGTH << MLX5_METER_MODE_SHIFT);

	aso_data->bytewise_data[meter->idx * 8 + 2] = cpu_to_be32((cbs_exp << MLX5_CBS_EXP_SHIFT) |
								  (cbs_man << MLX5_CBS_MAN_SHIFT) |
								  (cir_exp << MLX5_CIR_EXP_SHIFT) |
								  cir_man);

	mlx5_aso_post_wqe(aso, true, &aso_wqe->ctrl);

	/* With newer FW, the wait for the first ASO WQE is more than 2us, put the wait 10ms. */
	err = mlx5_aso_poll_cq(aso, true, 10);
	mutex_unlock(&flow_meters->aso_lock);

	return err;
}

static int
mlx5e_flow_meter_create_aso_obj(struct mlx5e_flow_meters *flow_meters, int *obj_id)
{
	u32 in[MLX5_ST_SZ_DW(create_flow_meter_aso_obj_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	struct mlx5_core_dev *mdev = flow_meters->mdev;
	void *obj;
	int err;

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_FLOW_METER_ASO);
	MLX5_SET(general_obj_in_cmd_hdr, in, log_obj_range, flow_meters->log_granularity);

	obj = MLX5_ADDR_OF(create_flow_meter_aso_obj_in, in, flow_meter_aso_obj);
	MLX5_SET(flow_meter_aso_obj, obj, meter_aso_access_pd, flow_meters->pdn);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (!err) {
		*obj_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
		mlx5_core_dbg(mdev, "flow meter aso obj(0x%x) created\n", *obj_id);
	}

	return err;
}

static void
mlx5e_flow_meter_destroy_aso_obj(struct mlx5_core_dev *mdev, u32 obj_id)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_FLOW_METER_ASO);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, obj_id);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	mlx5_core_dbg(mdev, "flow meter aso obj(0x%x) destroyed\n", obj_id);
}

static struct mlx5e_flow_meter_handle *
__mlx5e_flow_meter_alloc(struct mlx5e_flow_meters *flow_meters)
{
	struct mlx5_core_dev *mdev = flow_meters->mdev;
	struct mlx5e_flow_meter_aso_obj *meters_obj;
	struct mlx5e_flow_meter_handle *meter;
	int err, pos, total;
	u32 id;

	meter = kzalloc(sizeof(*meter), GFP_KERNEL);
	if (!meter)
		return ERR_PTR(-ENOMEM);

	meters_obj = list_first_entry_or_null(&flow_meters->partial_list,
					      struct mlx5e_flow_meter_aso_obj,
					      entry);
	/* 2 meters in one object */
	total = 1 << (flow_meters->log_granularity + 1);
	if (!meters_obj) {
		err = mlx5e_flow_meter_create_aso_obj(flow_meters, &id);
		if (err) {
			mlx5_core_err(mdev, "Failed to create flow meter ASO object\n");
			goto err_create;
		}

		meters_obj = kzalloc(sizeof(*meters_obj) + BITS_TO_BYTES(total),
				     GFP_KERNEL);
		if (!meters_obj) {
			err = -ENOMEM;
			goto err_mem;
		}

		meters_obj->base_id = id;
		meters_obj->total_meters = total;
		list_add(&meters_obj->entry, &flow_meters->partial_list);
		pos = 0;
	} else {
		pos = find_first_zero_bit(meters_obj->meters_map, total);
		if (bitmap_weight(meters_obj->meters_map, total) == total - 1) {
			list_del(&meters_obj->entry);
			list_add(&meters_obj->entry, &flow_meters->full_list);
		}
	}

	bitmap_set(meters_obj->meters_map, pos, 1);
	meter->flow_meters = flow_meters;
	meter->meters_obj = meters_obj;
	meter->obj_id = meters_obj->base_id + pos / 2;
	meter->idx = pos % 2;

	mlx5_core_dbg(mdev, "flow meter allocated, obj_id=0x%x, index=%d\n",
		      meter->obj_id, meter->idx);

	return meter;

err_mem:
	mlx5e_flow_meter_destroy_aso_obj(mdev, id);
err_create:
	kfree(meter);
	return ERR_PTR(err);
}

static void
__mlx5e_flow_meter_free(struct mlx5e_flow_meter_handle *meter)
{
	struct mlx5e_flow_meters *flow_meters = meter->flow_meters;
	struct mlx5_core_dev *mdev = flow_meters->mdev;
	struct mlx5e_flow_meter_aso_obj *meters_obj;
	int n, pos;

	meters_obj = meter->meters_obj;
	pos = (meter->obj_id - meters_obj->base_id) * 2 + meter->idx;
	bitmap_clear(meters_obj->meters_map, pos, 1);
	n = bitmap_weight(meters_obj->meters_map, meters_obj->total_meters);
	if (n == 0) {
		list_del(&meters_obj->entry);
		mlx5e_flow_meter_destroy_aso_obj(mdev, meters_obj->base_id);
		kfree(meters_obj);
	} else if (n == meters_obj->total_meters - 1) {
		list_del(&meters_obj->entry);
		list_add(&meters_obj->entry, &flow_meters->partial_list);
	}

	mlx5_core_dbg(mdev, "flow meter freed, obj_id=0x%x, index=%d\n",
		      meter->obj_id, meter->idx);
	kfree(meter);
}

struct mlx5e_flow_meter_handle *
mlx5e_tc_meter_get(struct mlx5_core_dev *mdev, struct mlx5e_flow_meter_params *params)
{
	struct mlx5e_flow_meters *flow_meters;
	struct mlx5e_flow_meter_handle *meter;
	int err;

	flow_meters = mlx5e_get_flow_meters(mdev);
	if (!flow_meters)
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&flow_meters->sync_lock);
	hash_for_each_possible(flow_meters->hashtbl, meter, hlist, params->index)
		if (meter->params.index == params->index)
			goto add_ref;

	meter = __mlx5e_flow_meter_alloc(flow_meters);
	if (IS_ERR(meter)) {
		err = PTR_ERR(meter);
		goto err_alloc;
	}

	hash_add(flow_meters->hashtbl, &meter->hlist, params->index);
	meter->params.index = params->index;

add_ref:
	meter->refcnt++;

	if (meter->params.mode != params->mode || meter->params.rate != params->rate ||
	    meter->params.burst != params->burst) {
		err = mlx5e_tc_meter_modify(mdev, meter, params);
		if (err)
			goto err_update;

		meter->params.mode = params->mode;
		meter->params.rate = params->rate;
		meter->params.burst = params->burst;
	}

	mutex_unlock(&flow_meters->sync_lock);
	return meter;

err_update:
	if (--meter->refcnt == 0) {
		hash_del(&meter->hlist);
		__mlx5e_flow_meter_free(meter);
	}
err_alloc:
	mutex_unlock(&flow_meters->sync_lock);
	return ERR_PTR(err);
}

void
mlx5e_tc_meter_put(struct mlx5e_flow_meter_handle *meter)
{
	struct mlx5e_flow_meters *flow_meters = meter->flow_meters;

	mutex_lock(&flow_meters->sync_lock);
	if (--meter->refcnt == 0) {
		hash_del(&meter->hlist);
		__mlx5e_flow_meter_free(meter);
	}
	mutex_unlock(&flow_meters->sync_lock);
}

struct mlx5_flow_table *
mlx5e_tc_meter_get_post_meter_ft(struct mlx5e_flow_meters *flow_meters)
{
	return mlx5e_post_meter_get_ft(flow_meters->post_meter);
}

struct mlx5e_flow_meters *
mlx5e_flow_meters_init(struct mlx5e_priv *priv,
		       enum mlx5_flow_namespace_type ns_type,
		       struct mlx5e_post_act *post_act)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_flow_meters *flow_meters;
	int err;

	if (!(MLX5_CAP_GEN_64(mdev, general_obj_types) &
	      MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_FLOW_METER_ASO))
		return ERR_PTR(-EOPNOTSUPP);

	if (IS_ERR_OR_NULL(post_act)) {
		netdev_dbg(priv->netdev,
			   "flow meter offload is not supported, post action is missing\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	flow_meters = kzalloc(sizeof(*flow_meters), GFP_KERNEL);
	if (!flow_meters)
		return ERR_PTR(-ENOMEM);

	err = mlx5_core_alloc_pd(mdev, &flow_meters->pdn);
	if (err) {
		mlx5_core_err(mdev, "Failed to alloc pd for flow meter aso, err=%d\n", err);
		goto err_out;
	}

	flow_meters->aso = mlx5_aso_create(mdev, flow_meters->pdn);
	if (IS_ERR(flow_meters->aso)) {
		mlx5_core_warn(mdev, "Failed to create aso wqe for flow meter\n");
		err = PTR_ERR(flow_meters->aso);
		goto err_sq;
	}

	flow_meters->post_meter = mlx5e_post_meter_init(priv, ns_type, post_act);
	if (IS_ERR(flow_meters->post_meter)) {
		err = PTR_ERR(flow_meters->post_meter);
		goto err_post_meter;
	}

	mutex_init(&flow_meters->sync_lock);
	INIT_LIST_HEAD(&flow_meters->partial_list);
	INIT_LIST_HEAD(&flow_meters->full_list);

	flow_meters->ns_type = ns_type;
	flow_meters->mdev = mdev;
	flow_meters->post_act = post_act;
	mutex_init(&flow_meters->aso_lock);
	flow_meters->log_granularity = min_t(int, 6,
					     MLX5_CAP_QOS(mdev, log_meter_aso_max_alloc));

	return flow_meters;

err_post_meter:
	mlx5_aso_destroy(flow_meters->aso);
err_sq:
	mlx5_core_dealloc_pd(mdev, flow_meters->pdn);
err_out:
	kfree(flow_meters);
	return ERR_PTR(err);
}

void
mlx5e_flow_meters_cleanup(struct mlx5e_flow_meters *flow_meters)
{
	if (IS_ERR_OR_NULL(flow_meters))
		return;

	mlx5e_post_meter_cleanup(flow_meters->post_meter);
	mlx5_aso_destroy(flow_meters->aso);
	mlx5_core_dealloc_pd(flow_meters->mdev, flow_meters->pdn);

	kfree(flow_meters);
}
