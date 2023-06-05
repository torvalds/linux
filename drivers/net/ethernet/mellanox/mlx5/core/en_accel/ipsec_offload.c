// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2017, Mellanox Technologies inc. All rights reserved. */

#include "mlx5_core.h"
#include "en.h"
#include "ipsec.h"
#include "lib/crypto.h"

enum {
	MLX5_IPSEC_ASO_REMOVE_FLOW_PKT_CNT_OFFSET,
	MLX5_IPSEC_ASO_REMOVE_FLOW_SOFT_LFT_OFFSET,
};

u32 mlx5_ipsec_device_caps(struct mlx5_core_dev *mdev)
{
	u32 caps = 0;

	if (!MLX5_CAP_GEN(mdev, ipsec_offload))
		return 0;

	if (!MLX5_CAP_GEN(mdev, log_max_dek))
		return 0;

	if (!(MLX5_CAP_GEN_64(mdev, general_obj_types) &
	    MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC))
		return 0;

	if (!MLX5_CAP_FLOWTABLE_NIC_TX(mdev, ipsec_encrypt) ||
	    !MLX5_CAP_FLOWTABLE_NIC_RX(mdev, ipsec_decrypt))
		return 0;

	if (!MLX5_CAP_IPSEC(mdev, ipsec_crypto_esp_aes_gcm_128_encrypt) ||
	    !MLX5_CAP_IPSEC(mdev, ipsec_crypto_esp_aes_gcm_128_decrypt))
		return 0;

	if (MLX5_CAP_IPSEC(mdev, ipsec_crypto_offload) &&
	    MLX5_CAP_ETH(mdev, insert_trailer) && MLX5_CAP_ETH(mdev, swp))
		caps |= MLX5_IPSEC_CAP_CRYPTO;

	if (MLX5_CAP_IPSEC(mdev, ipsec_full_offload)) {
		if (MLX5_CAP_FLOWTABLE_NIC_TX(mdev,
					      reformat_add_esp_trasport) &&
		    MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
					      reformat_del_esp_trasport) &&
		    MLX5_CAP_FLOWTABLE_NIC_RX(mdev, decap))
			caps |= MLX5_IPSEC_CAP_PACKET_OFFLOAD;

		if (MLX5_CAP_FLOWTABLE_NIC_TX(mdev, ignore_flow_level) &&
		    MLX5_CAP_FLOWTABLE_NIC_RX(mdev, ignore_flow_level))
			caps |= MLX5_IPSEC_CAP_PRIO;

		if (MLX5_CAP_FLOWTABLE_NIC_TX(mdev,
					      reformat_l2_to_l3_esp_tunnel) &&
		    MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
					      reformat_l3_esp_tunnel_to_l2))
			caps |= MLX5_IPSEC_CAP_TUNNEL;
	}

	if (mlx5_get_roce_state(mdev) &&
	    MLX5_CAP_GEN_2(mdev, flow_table_type_2_type) & MLX5_FT_NIC_RX_2_NIC_RX_RDMA &&
	    MLX5_CAP_GEN_2(mdev, flow_table_type_2_type) & MLX5_FT_NIC_TX_RDMA_2_NIC_TX)
		caps |= MLX5_IPSEC_CAP_ROCE;

	if (!caps)
		return 0;

	if (MLX5_CAP_IPSEC(mdev, ipsec_esn))
		caps |= MLX5_IPSEC_CAP_ESN;

	/* We can accommodate up to 2^24 different IPsec objects
	 * because we use up to 24 bit in flow table metadata
	 * to hold the IPsec Object unique handle.
	 */
	WARN_ON_ONCE(MLX5_CAP_IPSEC(mdev, log_max_ipsec_offload) > 24);
	return caps;
}
EXPORT_SYMBOL_GPL(mlx5_ipsec_device_caps);

static void mlx5e_ipsec_packet_setup(void *obj, u32 pdn,
				     struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	void *aso_ctx;

	aso_ctx = MLX5_ADDR_OF(ipsec_obj, obj, ipsec_aso);
	if (attrs->replay_esn.trigger) {
		MLX5_SET(ipsec_aso, aso_ctx, esn_event_arm, 1);

		if (attrs->dir == XFRM_DEV_OFFLOAD_IN) {
			MLX5_SET(ipsec_aso, aso_ctx, window_sz,
				 attrs->replay_esn.replay_window / 64);
			MLX5_SET(ipsec_aso, aso_ctx, mode,
				 MLX5_IPSEC_ASO_REPLAY_PROTECTION);
		}
		MLX5_SET(ipsec_aso, aso_ctx, mode_parameter,
			 attrs->replay_esn.esn);
	}

	/* ASO context */
	MLX5_SET(ipsec_obj, obj, ipsec_aso_access_pd, pdn);
	MLX5_SET(ipsec_obj, obj, full_offload, 1);
	MLX5_SET(ipsec_aso, aso_ctx, valid, 1);
	/* MLX5_IPSEC_ASO_REG_C_4_5 is type C register that is used
	 * in flow steering to perform matching against. Please be
	 * aware that this register was chosen arbitrary and can't
	 * be used in other places as long as IPsec packet offload
	 * active.
	 */
	MLX5_SET(ipsec_obj, obj, aso_return_reg, MLX5_IPSEC_ASO_REG_C_4_5);
	if (attrs->dir == XFRM_DEV_OFFLOAD_OUT)
		MLX5_SET(ipsec_aso, aso_ctx, mode, MLX5_IPSEC_ASO_INC_SN);

	if (attrs->lft.hard_packet_limit != XFRM_INF) {
		MLX5_SET(ipsec_aso, aso_ctx, remove_flow_pkt_cnt,
			 attrs->lft.hard_packet_limit);
		MLX5_SET(ipsec_aso, aso_ctx, hard_lft_arm, 1);
	}

	if (attrs->lft.soft_packet_limit != XFRM_INF) {
		MLX5_SET(ipsec_aso, aso_ctx, remove_flow_soft_lft,
			 attrs->lft.soft_packet_limit);

		MLX5_SET(ipsec_aso, aso_ctx, soft_lft_arm, 1);
	}
}

static int mlx5_create_ipsec_obj(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_accel_esp_xfrm_attrs *attrs = &sa_entry->attrs;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	struct aes_gcm_keymat *aes_gcm = &attrs->aes_gcm;
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	u32 in[MLX5_ST_SZ_DW(create_ipsec_obj_in)] = {};
	void *obj, *salt_p, *salt_iv_p;
	struct mlx5e_hw_objs *res;
	int err;

	obj = MLX5_ADDR_OF(create_ipsec_obj_in, in, ipsec_object);

	/* salt and seq_iv */
	salt_p = MLX5_ADDR_OF(ipsec_obj, obj, salt);
	memcpy(salt_p, &aes_gcm->salt, sizeof(aes_gcm->salt));

	MLX5_SET(ipsec_obj, obj, icv_length, MLX5_IPSEC_OBJECT_ICV_LEN_16B);
	salt_iv_p = MLX5_ADDR_OF(ipsec_obj, obj, implicit_iv);
	memcpy(salt_iv_p, &aes_gcm->seq_iv, sizeof(aes_gcm->seq_iv));
	/* esn */
	if (attrs->replay_esn.trigger) {
		MLX5_SET(ipsec_obj, obj, esn_en, 1);
		MLX5_SET(ipsec_obj, obj, esn_msb, attrs->replay_esn.esn_msb);
		MLX5_SET(ipsec_obj, obj, esn_overlap, attrs->replay_esn.overlap);
	}

	MLX5_SET(ipsec_obj, obj, dekn, sa_entry->enc_key_id);

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_IPSEC);

	res = &mdev->mlx5e_res.hw_objs;
	if (attrs->type == XFRM_DEV_OFFLOAD_PACKET)
		mlx5e_ipsec_packet_setup(obj, res->pdn, attrs);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (!err)
		sa_entry->ipsec_obj_id =
			MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	return err;
}

static void mlx5_destroy_ipsec_obj(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_IPSEC);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, sa_entry->ipsec_obj_id);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

int mlx5_ipsec_create_sa_ctx(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct aes_gcm_keymat *aes_gcm = &sa_entry->attrs.aes_gcm;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	int err;

	/* key */
	err = mlx5_create_encryption_key(mdev, aes_gcm->aes_key,
					 aes_gcm->key_len / BITS_PER_BYTE,
					 MLX5_ACCEL_OBJ_IPSEC_KEY,
					 &sa_entry->enc_key_id);
	if (err) {
		mlx5_core_dbg(mdev, "Failed to create encryption key (err = %d)\n", err);
		return err;
	}

	err = mlx5_create_ipsec_obj(sa_entry);
	if (err) {
		mlx5_core_dbg(mdev, "Failed to create IPsec object (err = %d)\n", err);
		goto err_enc_key;
	}

	return 0;

err_enc_key:
	mlx5_destroy_encryption_key(mdev, sa_entry->enc_key_id);
	return err;
}

void mlx5_ipsec_free_sa_ctx(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);

	mlx5_destroy_ipsec_obj(sa_entry);
	mlx5_destroy_encryption_key(mdev, sa_entry->enc_key_id);
}

static int mlx5_modify_ipsec_obj(struct mlx5e_ipsec_sa_entry *sa_entry,
				 const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	u32 in[MLX5_ST_SZ_DW(modify_ipsec_obj_in)] = {};
	u32 out[MLX5_ST_SZ_DW(query_ipsec_obj_out)];
	u64 modify_field_select = 0;
	u64 general_obj_types;
	void *obj;
	int err;

	general_obj_types = MLX5_CAP_GEN_64(mdev, general_obj_types);
	if (!(general_obj_types & MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC))
		return -EINVAL;

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_GENERAL_OBJECT_TYPES_IPSEC);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, sa_entry->ipsec_obj_id);
	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (err) {
		mlx5_core_err(mdev, "Query IPsec object failed (Object id %d), err = %d\n",
			      sa_entry->ipsec_obj_id, err);
		return err;
	}

	obj = MLX5_ADDR_OF(query_ipsec_obj_out, out, ipsec_object);
	modify_field_select = MLX5_GET64(ipsec_obj, obj, modify_field_select);

	/* esn */
	if (!(modify_field_select & MLX5_MODIFY_IPSEC_BITMASK_ESN_OVERLAP) ||
	    !(modify_field_select & MLX5_MODIFY_IPSEC_BITMASK_ESN_MSB))
		return -EOPNOTSUPP;

	obj = MLX5_ADDR_OF(modify_ipsec_obj_in, in, ipsec_object);
	MLX5_SET64(ipsec_obj, obj, modify_field_select,
		   MLX5_MODIFY_IPSEC_BITMASK_ESN_OVERLAP |
			   MLX5_MODIFY_IPSEC_BITMASK_ESN_MSB);
	MLX5_SET(ipsec_obj, obj, esn_msb, attrs->replay_esn.esn_msb);
	MLX5_SET(ipsec_obj, obj, esn_overlap, attrs->replay_esn.overlap);

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

void mlx5_accel_esp_modify_xfrm(struct mlx5e_ipsec_sa_entry *sa_entry,
				const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	int err;

	err = mlx5_modify_ipsec_obj(sa_entry, attrs);
	if (err)
		return;

	memcpy(&sa_entry->attrs, attrs, sizeof(sa_entry->attrs));
}

static void mlx5e_ipsec_aso_update(struct mlx5e_ipsec_sa_entry *sa_entry,
				   struct mlx5_wqe_aso_ctrl_seg *data)
{
	data->data_mask_mode = MLX5_ASO_DATA_MASK_MODE_BITWISE_64BIT << 6;
	data->condition_1_0_operand = MLX5_ASO_ALWAYS_TRUE |
				      MLX5_ASO_ALWAYS_TRUE << 4;

	mlx5e_ipsec_aso_query(sa_entry, data);
}

static void mlx5e_ipsec_update_esn_state(struct mlx5e_ipsec_sa_entry *sa_entry,
					 u32 mode_param)
{
	struct mlx5_accel_esp_xfrm_attrs attrs = {};
	struct mlx5_wqe_aso_ctrl_seg data = {};

	if (mode_param < MLX5E_IPSEC_ESN_SCOPE_MID) {
		sa_entry->esn_state.esn_msb++;
		sa_entry->esn_state.overlap = 0;
	} else {
		sa_entry->esn_state.overlap = 1;
	}

	mlx5e_ipsec_build_accel_xfrm_attrs(sa_entry, &attrs);
	mlx5_accel_esp_modify_xfrm(sa_entry, &attrs);

	data.data_offset_condition_operand =
		MLX5_IPSEC_ASO_REMOVE_FLOW_PKT_CNT_OFFSET;
	data.bitwise_data = cpu_to_be64(BIT_ULL(54));
	data.data_mask = data.bitwise_data;

	mlx5e_ipsec_aso_update(sa_entry, &data);
}

static void mlx5e_ipsec_aso_update_hard(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_wqe_aso_ctrl_seg data = {};

	data.data_offset_condition_operand =
		MLX5_IPSEC_ASO_REMOVE_FLOW_PKT_CNT_OFFSET;
	data.bitwise_data = cpu_to_be64(BIT_ULL(57) + BIT_ULL(31));
	data.data_mask = data.bitwise_data;
	mlx5e_ipsec_aso_update(sa_entry, &data);
}

static void mlx5e_ipsec_aso_update_soft(struct mlx5e_ipsec_sa_entry *sa_entry,
					u32 val)
{
	struct mlx5_wqe_aso_ctrl_seg data = {};

	data.data_offset_condition_operand =
		MLX5_IPSEC_ASO_REMOVE_FLOW_SOFT_LFT_OFFSET;
	data.bitwise_data = cpu_to_be64(val);
	data.data_mask = cpu_to_be64(U32_MAX);
	mlx5e_ipsec_aso_update(sa_entry, &data);
}

static void mlx5e_ipsec_handle_limits(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_accel_esp_xfrm_attrs *attrs = &sa_entry->attrs;
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	struct mlx5e_ipsec_aso *aso = ipsec->aso;
	bool soft_arm, hard_arm;
	u64 hard_cnt;

	lockdep_assert_held(&sa_entry->x->lock);

	soft_arm = !MLX5_GET(ipsec_aso, aso->ctx, soft_lft_arm);
	hard_arm = !MLX5_GET(ipsec_aso, aso->ctx, hard_lft_arm);
	if (!soft_arm && !hard_arm)
		/* It is not lifetime event */
		return;

	hard_cnt = MLX5_GET(ipsec_aso, aso->ctx, remove_flow_pkt_cnt);
	if (!hard_cnt || hard_arm) {
		/* It is possible to see packet counter equal to zero without
		 * hard limit event armed. Such situation can be if packet
		 * decreased, while we handled soft limit event.
		 *
		 * However it will be HW/FW bug if hard limit event is raised
		 * and packet counter is not zero.
		 */
		WARN_ON_ONCE(hard_arm && hard_cnt);

		/* Notify about hard limit */
		xfrm_state_check_expire(sa_entry->x);
		return;
	}

	/* We are in soft limit event. */
	if (!sa_entry->limits.soft_limit_hit &&
	    sa_entry->limits.round == attrs->lft.numb_rounds_soft) {
		sa_entry->limits.soft_limit_hit = true;
		/* Notify about soft limit */
		xfrm_state_check_expire(sa_entry->x);

		if (sa_entry->limits.round == attrs->lft.numb_rounds_hard)
			goto hard;

		if (attrs->lft.soft_packet_limit > BIT_ULL(31)) {
			/* We cannot avoid a soft_value that might have the high
			 * bit set. For instance soft_value=2^31+1 cannot be
			 * adjusted to the low bit clear version of soft_value=1
			 * because it is too close to 0.
			 *
			 * Thus we have this corner case where we can hit the
			 * soft_limit with the high bit set, but cannot adjust
			 * the counter. Thus we set a temporary interrupt_value
			 * at least 2^30 away from here and do the adjustment
			 * then.
			 */
			mlx5e_ipsec_aso_update_soft(sa_entry,
						    BIT_ULL(31) - BIT_ULL(30));
			sa_entry->limits.fix_limit = true;
			return;
		}

		sa_entry->limits.fix_limit = true;
	}

hard:
	if (sa_entry->limits.round == attrs->lft.numb_rounds_hard) {
		mlx5e_ipsec_aso_update_soft(sa_entry, 0);
		attrs->lft.soft_packet_limit = XFRM_INF;
		return;
	}

	mlx5e_ipsec_aso_update_hard(sa_entry);
	sa_entry->limits.round++;
	if (sa_entry->limits.round == attrs->lft.numb_rounds_soft)
		mlx5e_ipsec_aso_update_soft(sa_entry,
					    attrs->lft.soft_packet_limit);
	if (sa_entry->limits.fix_limit) {
		sa_entry->limits.fix_limit = false;
		mlx5e_ipsec_aso_update_soft(sa_entry, BIT_ULL(31) - 1);
	}
}

static void mlx5e_ipsec_handle_event(struct work_struct *_work)
{
	struct mlx5e_ipsec_work *work =
		container_of(_work, struct mlx5e_ipsec_work, work);
	struct mlx5e_ipsec_sa_entry *sa_entry = work->data;
	struct mlx5_accel_esp_xfrm_attrs *attrs;
	struct mlx5e_ipsec_aso *aso;
	int ret;

	aso = sa_entry->ipsec->aso;
	attrs = &sa_entry->attrs;

	spin_lock(&sa_entry->x->lock);
	ret = mlx5e_ipsec_aso_query(sa_entry, NULL);
	if (ret)
		goto unlock;

	if (attrs->replay_esn.trigger &&
	    !MLX5_GET(ipsec_aso, aso->ctx, esn_event_arm)) {
		u32 mode_param = MLX5_GET(ipsec_aso, aso->ctx, mode_parameter);

		mlx5e_ipsec_update_esn_state(sa_entry, mode_param);
	}

	if (attrs->lft.soft_packet_limit != XFRM_INF)
		mlx5e_ipsec_handle_limits(sa_entry);

unlock:
	spin_unlock(&sa_entry->x->lock);
	kfree(work);
}

static int mlx5e_ipsec_event(struct notifier_block *nb, unsigned long event,
			     void *data)
{
	struct mlx5e_ipsec *ipsec = container_of(nb, struct mlx5e_ipsec, nb);
	struct mlx5e_ipsec_sa_entry *sa_entry;
	struct mlx5_eqe_obj_change *object;
	struct mlx5e_ipsec_work *work;
	struct mlx5_eqe *eqe = data;
	u16 type;

	if (event != MLX5_EVENT_TYPE_OBJECT_CHANGE)
		return NOTIFY_DONE;

	object = &eqe->data.obj_change;
	type = be16_to_cpu(object->obj_type);

	if (type != MLX5_GENERAL_OBJECT_TYPES_IPSEC)
		return NOTIFY_DONE;

	sa_entry = xa_load(&ipsec->sadb, be32_to_cpu(object->obj_id));
	if (!sa_entry)
		return NOTIFY_DONE;

	work = kmalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return NOTIFY_DONE;

	INIT_WORK(&work->work, mlx5e_ipsec_handle_event);
	work->data = sa_entry;

	queue_work(ipsec->wq, &work->work);
	return NOTIFY_OK;
}

int mlx5e_ipsec_aso_init(struct mlx5e_ipsec *ipsec)
{
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5e_ipsec_aso *aso;
	struct mlx5e_hw_objs *res;
	struct device *pdev;
	int err;

	aso = kzalloc(sizeof(*ipsec->aso), GFP_KERNEL);
	if (!aso)
		return -ENOMEM;

	res = &mdev->mlx5e_res.hw_objs;

	pdev = mlx5_core_dma_dev(mdev);
	aso->dma_addr = dma_map_single(pdev, aso->ctx, sizeof(aso->ctx),
				       DMA_BIDIRECTIONAL);
	err = dma_mapping_error(pdev, aso->dma_addr);
	if (err)
		goto err_dma;

	aso->aso = mlx5_aso_create(mdev, res->pdn);
	if (IS_ERR(aso->aso)) {
		err = PTR_ERR(aso->aso);
		goto err_aso_create;
	}

	spin_lock_init(&aso->lock);
	ipsec->nb.notifier_call = mlx5e_ipsec_event;
	mlx5_notifier_register(mdev, &ipsec->nb);

	ipsec->aso = aso;
	return 0;

err_aso_create:
	dma_unmap_single(pdev, aso->dma_addr, sizeof(aso->ctx),
			 DMA_BIDIRECTIONAL);
err_dma:
	kfree(aso);
	return err;
}

void mlx5e_ipsec_aso_cleanup(struct mlx5e_ipsec *ipsec)
{
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5e_ipsec_aso *aso;
	struct device *pdev;

	aso = ipsec->aso;
	pdev = mlx5_core_dma_dev(mdev);

	mlx5_notifier_unregister(mdev, &ipsec->nb);
	mlx5_aso_destroy(aso->aso);
	dma_unmap_single(pdev, aso->dma_addr, sizeof(aso->ctx),
			 DMA_BIDIRECTIONAL);
	kfree(aso);
}

static void mlx5e_ipsec_aso_copy(struct mlx5_wqe_aso_ctrl_seg *ctrl,
				 struct mlx5_wqe_aso_ctrl_seg *data)
{
	if (!data)
		return;

	ctrl->data_mask_mode = data->data_mask_mode;
	ctrl->condition_1_0_operand = data->condition_1_0_operand;
	ctrl->condition_1_0_offset = data->condition_1_0_offset;
	ctrl->data_offset_condition_operand = data->data_offset_condition_operand;
	ctrl->condition_0_data = data->condition_0_data;
	ctrl->condition_0_mask = data->condition_0_mask;
	ctrl->condition_1_data = data->condition_1_data;
	ctrl->condition_1_mask = data->condition_1_mask;
	ctrl->bitwise_data = data->bitwise_data;
	ctrl->data_mask = data->data_mask;
}

int mlx5e_ipsec_aso_query(struct mlx5e_ipsec_sa_entry *sa_entry,
			  struct mlx5_wqe_aso_ctrl_seg *data)
{
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	struct mlx5e_ipsec_aso *aso = ipsec->aso;
	struct mlx5_core_dev *mdev = ipsec->mdev;
	struct mlx5_wqe_aso_ctrl_seg *ctrl;
	struct mlx5e_hw_objs *res;
	struct mlx5_aso_wqe *wqe;
	unsigned long expires;
	u8 ds_cnt;
	int ret;

	lockdep_assert_held(&sa_entry->x->lock);
	res = &mdev->mlx5e_res.hw_objs;

	spin_lock_bh(&aso->lock);
	memset(aso->ctx, 0, sizeof(aso->ctx));
	wqe = mlx5_aso_get_wqe(aso->aso);
	ds_cnt = DIV_ROUND_UP(sizeof(*wqe), MLX5_SEND_WQE_DS);
	mlx5_aso_build_wqe(aso->aso, ds_cnt, wqe, sa_entry->ipsec_obj_id,
			   MLX5_ACCESS_ASO_OPC_MOD_IPSEC);

	ctrl = &wqe->aso_ctrl;
	ctrl->va_l =
		cpu_to_be32(lower_32_bits(aso->dma_addr) | ASO_CTRL_READ_EN);
	ctrl->va_h = cpu_to_be32(upper_32_bits(aso->dma_addr));
	ctrl->l_key = cpu_to_be32(res->mkey);
	mlx5e_ipsec_aso_copy(ctrl, data);

	mlx5_aso_post_wqe(aso->aso, false, &wqe->ctrl);
	expires = jiffies + msecs_to_jiffies(10);
	do {
		ret = mlx5_aso_poll_cq(aso->aso, false);
		if (ret)
			usleep_range(2, 10);
	} while (ret && time_is_after_jiffies(expires));
	spin_unlock_bh(&aso->lock);
	return ret;
}
