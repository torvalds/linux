// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2013-2020, Mellanox Technologies inc. All rights reserved.
 */

#include "mlx5_ib.h"
#include <linux/mlx5/eswitch.h>
#include "counters.h"
#include "ib_rep.h"
#include "qp.h"

struct mlx5_ib_counter {
	const char *name;
	size_t offset;
};

#define INIT_Q_COUNTER(_name)		\
	{ .name = #_name, .offset = MLX5_BYTE_OFF(query_q_counter_out, _name)}

static const struct mlx5_ib_counter basic_q_cnts[] = {
	INIT_Q_COUNTER(rx_write_requests),
	INIT_Q_COUNTER(rx_read_requests),
	INIT_Q_COUNTER(rx_atomic_requests),
	INIT_Q_COUNTER(out_of_buffer),
};

static const struct mlx5_ib_counter out_of_seq_q_cnts[] = {
	INIT_Q_COUNTER(out_of_sequence),
};

static const struct mlx5_ib_counter retrans_q_cnts[] = {
	INIT_Q_COUNTER(duplicate_request),
	INIT_Q_COUNTER(rnr_nak_retry_err),
	INIT_Q_COUNTER(packet_seq_err),
	INIT_Q_COUNTER(implied_nak_seq_err),
	INIT_Q_COUNTER(local_ack_timeout_err),
};

#define INIT_CONG_COUNTER(_name)		\
	{ .name = #_name, .offset =	\
		MLX5_BYTE_OFF(query_cong_statistics_out, _name ## _high)}

static const struct mlx5_ib_counter cong_cnts[] = {
	INIT_CONG_COUNTER(rp_cnp_ignored),
	INIT_CONG_COUNTER(rp_cnp_handled),
	INIT_CONG_COUNTER(np_ecn_marked_roce_packets),
	INIT_CONG_COUNTER(np_cnp_sent),
};

static const struct mlx5_ib_counter extended_err_cnts[] = {
	INIT_Q_COUNTER(resp_local_length_error),
	INIT_Q_COUNTER(resp_cqe_error),
	INIT_Q_COUNTER(req_cqe_error),
	INIT_Q_COUNTER(req_remote_invalid_request),
	INIT_Q_COUNTER(req_remote_access_errors),
	INIT_Q_COUNTER(resp_remote_access_errors),
	INIT_Q_COUNTER(resp_cqe_flush_error),
	INIT_Q_COUNTER(req_cqe_flush_error),
};

static const struct mlx5_ib_counter roce_accl_cnts[] = {
	INIT_Q_COUNTER(roce_adp_retrans),
	INIT_Q_COUNTER(roce_adp_retrans_to),
	INIT_Q_COUNTER(roce_slow_restart),
	INIT_Q_COUNTER(roce_slow_restart_cnps),
	INIT_Q_COUNTER(roce_slow_restart_trans),
};

#define INIT_EXT_PPCNT_COUNTER(_name)		\
	{ .name = #_name, .offset =	\
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_extended_cntrs_grp_data_layout._name##_high)}

static const struct mlx5_ib_counter ext_ppcnt_cnts[] = {
	INIT_EXT_PPCNT_COUNTER(rx_icrc_encapsulated),
};

static int mlx5_ib_read_counters(struct ib_counters *counters,
				 struct ib_counters_read_attr *read_attr,
				 struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_mcounters *mcounters = to_mcounters(counters);
	struct mlx5_read_counters_attr mread_attr = {};
	struct mlx5_ib_flow_counters_desc *desc;
	int ret, i;

	mutex_lock(&mcounters->mcntrs_mutex);
	if (mcounters->cntrs_max_index > read_attr->ncounters) {
		ret = -EINVAL;
		goto err_bound;
	}

	mread_attr.out = kcalloc(mcounters->counters_num, sizeof(u64),
				 GFP_KERNEL);
	if (!mread_attr.out) {
		ret = -ENOMEM;
		goto err_bound;
	}

	mread_attr.hw_cntrs_hndl = mcounters->hw_cntrs_hndl;
	mread_attr.flags = read_attr->flags;
	ret = mcounters->read_counters(counters->device, &mread_attr);
	if (ret)
		goto err_read;

	/* do the pass over the counters data array to assign according to the
	 * descriptions and indexing pairs
	 */
	desc = mcounters->counters_data;
	for (i = 0; i < mcounters->ncounters; i++)
		read_attr->counters_buff[desc[i].index] += mread_attr.out[desc[i].description];

err_read:
	kfree(mread_attr.out);
err_bound:
	mutex_unlock(&mcounters->mcntrs_mutex);
	return ret;
}

static int mlx5_ib_destroy_counters(struct ib_counters *counters)
{
	struct mlx5_ib_mcounters *mcounters = to_mcounters(counters);

	mlx5_ib_counters_clear_description(counters);
	if (mcounters->hw_cntrs_hndl)
		mlx5_fc_destroy(to_mdev(counters->device)->mdev,
				mcounters->hw_cntrs_hndl);
	return 0;
}

static int mlx5_ib_create_counters(struct ib_counters *counters,
				   struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_mcounters *mcounters = to_mcounters(counters);

	mutex_init(&mcounters->mcntrs_mutex);
	return 0;
}


static bool is_mdev_switchdev_mode(const struct mlx5_core_dev *mdev)
{
	return MLX5_ESWITCH_MANAGER(mdev) &&
	       mlx5_ib_eswitch_mode(mdev->priv.eswitch) ==
		       MLX5_ESWITCH_OFFLOADS;
}

static const struct mlx5_ib_counters *get_counters(struct mlx5_ib_dev *dev,
						   u8 port_num)
{
	return is_mdev_switchdev_mode(dev->mdev) ? &dev->port[0].cnts :
						   &dev->port[port_num].cnts;
}

/**
 * mlx5_ib_get_counters_id - Returns counters id to use for device+port
 * @dev:	Pointer to mlx5 IB device
 * @port_num:	Zero based port number
 *
 * mlx5_ib_get_counters_id() Returns counters set id to use for given
 * device port combination in switchdev and non switchdev mode of the
 * parent device.
 */
u16 mlx5_ib_get_counters_id(struct mlx5_ib_dev *dev, u8 port_num)
{
	const struct mlx5_ib_counters *cnts = get_counters(dev, port_num);

	return cnts->set_id;
}

static struct rdma_hw_stats *mlx5_ib_alloc_hw_stats(struct ib_device *ibdev,
						    u8 port_num)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	const struct mlx5_ib_counters *cnts;
	bool is_switchdev = is_mdev_switchdev_mode(dev->mdev);

	if ((is_switchdev && port_num) || (!is_switchdev && !port_num))
		return NULL;

	cnts = get_counters(dev, port_num - 1);

	return rdma_alloc_hw_stats_struct(cnts->names,
					  cnts->num_q_counters +
					  cnts->num_cong_counters +
					  cnts->num_ext_ppcnt_counters,
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

static int mlx5_ib_query_q_counters(struct mlx5_core_dev *mdev,
				    const struct mlx5_ib_counters *cnts,
				    struct rdma_hw_stats *stats,
				    u16 set_id)
{
	u32 out[MLX5_ST_SZ_DW(query_q_counter_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_q_counter_in)] = {};
	__be32 val;
	int ret, i;

	MLX5_SET(query_q_counter_in, in, opcode, MLX5_CMD_OP_QUERY_Q_COUNTER);
	MLX5_SET(query_q_counter_in, in, counter_set_id, set_id);
	ret = mlx5_cmd_exec_inout(mdev, query_q_counter, in, out);
	if (ret)
		return ret;

	for (i = 0; i < cnts->num_q_counters; i++) {
		val = *(__be32 *)((void *)out + cnts->offsets[i]);
		stats->value[i] = (u64)be32_to_cpu(val);
	}

	return 0;
}

static int mlx5_ib_query_ext_ppcnt_counters(struct mlx5_ib_dev *dev,
					    const struct mlx5_ib_counters *cnts,
					    struct rdma_hw_stats *stats)
{
	int offset = cnts->num_q_counters + cnts->num_cong_counters;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	int ret, i;
	void *out;

	out = kvzalloc(sz, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	MLX5_SET(ppcnt_reg, in, grp, MLX5_ETHERNET_EXTENDED_COUNTERS_GROUP);
	ret = mlx5_core_access_reg(dev->mdev, in, sz, out, sz, MLX5_REG_PPCNT,
				   0, 0);
	if (ret)
		goto free;

	for (i = 0; i < cnts->num_ext_ppcnt_counters; i++)
		stats->value[i + offset] =
			be64_to_cpup((__be64 *)(out +
				    cnts->offsets[i + offset]));
free:
	kvfree(out);
	return ret;
}

static int mlx5_ib_get_hw_stats(struct ib_device *ibdev,
				struct rdma_hw_stats *stats,
				u8 port_num, int index)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	const struct mlx5_ib_counters *cnts = get_counters(dev, port_num - 1);
	struct mlx5_core_dev *mdev;
	int ret, num_counters;
	u8 mdev_port_num;

	if (!stats)
		return -EINVAL;

	num_counters = cnts->num_q_counters +
		       cnts->num_cong_counters +
		       cnts->num_ext_ppcnt_counters;

	/* q_counters are per IB device, query the master mdev */
	ret = mlx5_ib_query_q_counters(dev->mdev, cnts, stats, cnts->set_id);
	if (ret)
		return ret;

	if (MLX5_CAP_PCAM_FEATURE(dev->mdev, rx_icrc_encapsulated_counter)) {
		ret =  mlx5_ib_query_ext_ppcnt_counters(dev, cnts, stats);
		if (ret)
			return ret;
	}

	if (MLX5_CAP_GEN(dev->mdev, cc_query_allowed)) {
		mdev = mlx5_ib_get_native_port_mdev(dev, port_num,
						    &mdev_port_num);
		if (!mdev) {
			/* If port is not affiliated yet, its in down state
			 * which doesn't have any counters yet, so it would be
			 * zero. So no need to read from the HCA.
			 */
			goto done;
		}
		ret = mlx5_lag_query_cong_counters(dev->mdev,
						   stats->value +
						   cnts->num_q_counters,
						   cnts->num_cong_counters,
						   cnts->offsets +
						   cnts->num_q_counters);

		mlx5_ib_put_native_port_mdev(dev, port_num);
		if (ret)
			return ret;
	}

done:
	return num_counters;
}

static struct rdma_hw_stats *
mlx5_ib_counter_alloc_stats(struct rdma_counter *counter)
{
	struct mlx5_ib_dev *dev = to_mdev(counter->device);
	const struct mlx5_ib_counters *cnts =
		get_counters(dev, counter->port - 1);

	return rdma_alloc_hw_stats_struct(cnts->names,
					  cnts->num_q_counters +
					  cnts->num_cong_counters +
					  cnts->num_ext_ppcnt_counters,
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

static int mlx5_ib_counter_update_stats(struct rdma_counter *counter)
{
	struct mlx5_ib_dev *dev = to_mdev(counter->device);
	const struct mlx5_ib_counters *cnts =
		get_counters(dev, counter->port - 1);

	return mlx5_ib_query_q_counters(dev->mdev, cnts,
					counter->stats, counter->id);
}

static int mlx5_ib_counter_dealloc(struct rdma_counter *counter)
{
	struct mlx5_ib_dev *dev = to_mdev(counter->device);
	u32 in[MLX5_ST_SZ_DW(dealloc_q_counter_in)] = {};

	if (!counter->id)
		return 0;

	MLX5_SET(dealloc_q_counter_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_Q_COUNTER);
	MLX5_SET(dealloc_q_counter_in, in, counter_set_id, counter->id);
	return mlx5_cmd_exec_in(dev->mdev, dealloc_q_counter, in);
}

static int mlx5_ib_counter_bind_qp(struct rdma_counter *counter,
				   struct ib_qp *qp)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	int err;

	if (!counter->id) {
		u32 out[MLX5_ST_SZ_DW(alloc_q_counter_out)] = {};
		u32 in[MLX5_ST_SZ_DW(alloc_q_counter_in)] = {};

		MLX5_SET(alloc_q_counter_in, in, opcode,
			 MLX5_CMD_OP_ALLOC_Q_COUNTER);
		MLX5_SET(alloc_q_counter_in, in, uid, MLX5_SHARED_RESOURCE_UID);
		err = mlx5_cmd_exec_inout(dev->mdev, alloc_q_counter, in, out);
		if (err)
			return err;
		counter->id =
			MLX5_GET(alloc_q_counter_out, out, counter_set_id);
	}

	err = mlx5_ib_qp_set_counter(qp, counter);
	if (err)
		goto fail_set_counter;

	return 0;

fail_set_counter:
	mlx5_ib_counter_dealloc(counter);
	counter->id = 0;

	return err;
}

static int mlx5_ib_counter_unbind_qp(struct ib_qp *qp)
{
	return mlx5_ib_qp_set_counter(qp, NULL);
}


static void mlx5_ib_fill_counters(struct mlx5_ib_dev *dev,
				  const char **names,
				  size_t *offsets)
{
	int i;
	int j = 0;

	for (i = 0; i < ARRAY_SIZE(basic_q_cnts); i++, j++) {
		names[j] = basic_q_cnts[i].name;
		offsets[j] = basic_q_cnts[i].offset;
	}

	if (MLX5_CAP_GEN(dev->mdev, out_of_seq_cnt)) {
		for (i = 0; i < ARRAY_SIZE(out_of_seq_q_cnts); i++, j++) {
			names[j] = out_of_seq_q_cnts[i].name;
			offsets[j] = out_of_seq_q_cnts[i].offset;
		}
	}

	if (MLX5_CAP_GEN(dev->mdev, retransmission_q_counters)) {
		for (i = 0; i < ARRAY_SIZE(retrans_q_cnts); i++, j++) {
			names[j] = retrans_q_cnts[i].name;
			offsets[j] = retrans_q_cnts[i].offset;
		}
	}

	if (MLX5_CAP_GEN(dev->mdev, enhanced_error_q_counters)) {
		for (i = 0; i < ARRAY_SIZE(extended_err_cnts); i++, j++) {
			names[j] = extended_err_cnts[i].name;
			offsets[j] = extended_err_cnts[i].offset;
		}
	}

	if (MLX5_CAP_GEN(dev->mdev, roce_accl)) {
		for (i = 0; i < ARRAY_SIZE(roce_accl_cnts); i++, j++) {
			names[j] = roce_accl_cnts[i].name;
			offsets[j] = roce_accl_cnts[i].offset;
		}
	}

	if (MLX5_CAP_GEN(dev->mdev, cc_query_allowed)) {
		for (i = 0; i < ARRAY_SIZE(cong_cnts); i++, j++) {
			names[j] = cong_cnts[i].name;
			offsets[j] = cong_cnts[i].offset;
		}
	}

	if (MLX5_CAP_PCAM_FEATURE(dev->mdev, rx_icrc_encapsulated_counter)) {
		for (i = 0; i < ARRAY_SIZE(ext_ppcnt_cnts); i++, j++) {
			names[j] = ext_ppcnt_cnts[i].name;
			offsets[j] = ext_ppcnt_cnts[i].offset;
		}
	}
}


static int __mlx5_ib_alloc_counters(struct mlx5_ib_dev *dev,
				    struct mlx5_ib_counters *cnts)
{
	u32 num_counters;

	num_counters = ARRAY_SIZE(basic_q_cnts);

	if (MLX5_CAP_GEN(dev->mdev, out_of_seq_cnt))
		num_counters += ARRAY_SIZE(out_of_seq_q_cnts);

	if (MLX5_CAP_GEN(dev->mdev, retransmission_q_counters))
		num_counters += ARRAY_SIZE(retrans_q_cnts);

	if (MLX5_CAP_GEN(dev->mdev, enhanced_error_q_counters))
		num_counters += ARRAY_SIZE(extended_err_cnts);

	if (MLX5_CAP_GEN(dev->mdev, roce_accl))
		num_counters += ARRAY_SIZE(roce_accl_cnts);

	cnts->num_q_counters = num_counters;

	if (MLX5_CAP_GEN(dev->mdev, cc_query_allowed)) {
		cnts->num_cong_counters = ARRAY_SIZE(cong_cnts);
		num_counters += ARRAY_SIZE(cong_cnts);
	}
	if (MLX5_CAP_PCAM_FEATURE(dev->mdev, rx_icrc_encapsulated_counter)) {
		cnts->num_ext_ppcnt_counters = ARRAY_SIZE(ext_ppcnt_cnts);
		num_counters += ARRAY_SIZE(ext_ppcnt_cnts);
	}
	cnts->names = kcalloc(num_counters, sizeof(*cnts->names), GFP_KERNEL);
	if (!cnts->names)
		return -ENOMEM;

	cnts->offsets = kcalloc(num_counters,
				sizeof(*cnts->offsets), GFP_KERNEL);
	if (!cnts->offsets)
		goto err_names;

	return 0;

err_names:
	kfree(cnts->names);
	cnts->names = NULL;
	return -ENOMEM;
}

static void mlx5_ib_dealloc_counters(struct mlx5_ib_dev *dev)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_q_counter_in)] = {};
	int num_cnt_ports;
	int i;

	num_cnt_ports = is_mdev_switchdev_mode(dev->mdev) ? 1 : dev->num_ports;

	MLX5_SET(dealloc_q_counter_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_Q_COUNTER);

	for (i = 0; i < num_cnt_ports; i++) {
		if (dev->port[i].cnts.set_id) {
			MLX5_SET(dealloc_q_counter_in, in, counter_set_id,
				 dev->port[i].cnts.set_id);
			mlx5_cmd_exec_in(dev->mdev, dealloc_q_counter, in);
		}
		kfree(dev->port[i].cnts.names);
		kfree(dev->port[i].cnts.offsets);
	}
}

static int mlx5_ib_alloc_counters(struct mlx5_ib_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(alloc_q_counter_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_q_counter_in)] = {};
	int num_cnt_ports;
	int err = 0;
	int i;
	bool is_shared;

	MLX5_SET(alloc_q_counter_in, in, opcode, MLX5_CMD_OP_ALLOC_Q_COUNTER);
	is_shared = MLX5_CAP_GEN(dev->mdev, log_max_uctx) != 0;
	num_cnt_ports = is_mdev_switchdev_mode(dev->mdev) ? 1 : dev->num_ports;

	for (i = 0; i < num_cnt_ports; i++) {
		err = __mlx5_ib_alloc_counters(dev, &dev->port[i].cnts);
		if (err)
			goto err_alloc;

		mlx5_ib_fill_counters(dev, dev->port[i].cnts.names,
				      dev->port[i].cnts.offsets);

		MLX5_SET(alloc_q_counter_in, in, uid,
			 is_shared ? MLX5_SHARED_RESOURCE_UID : 0);

		err = mlx5_cmd_exec_inout(dev->mdev, alloc_q_counter, in, out);
		if (err) {
			mlx5_ib_warn(dev,
				     "couldn't allocate queue counter for port %d, err %d\n",
				     i + 1, err);
			goto err_alloc;
		}

		dev->port[i].cnts.set_id =
			MLX5_GET(alloc_q_counter_out, out, counter_set_id);
	}
	return 0;

err_alloc:
	mlx5_ib_dealloc_counters(dev);
	return err;
}

static int read_flow_counters(struct ib_device *ibdev,
			      struct mlx5_read_counters_attr *read_attr)
{
	struct mlx5_fc *fc = read_attr->hw_cntrs_hndl;
	struct mlx5_ib_dev *dev = to_mdev(ibdev);

	return mlx5_fc_query(dev->mdev, fc,
			     &read_attr->out[IB_COUNTER_PACKETS],
			     &read_attr->out[IB_COUNTER_BYTES]);
}

/* flow counters currently expose two counters packets and bytes */
#define FLOW_COUNTERS_NUM 2
static int counters_set_description(
	struct ib_counters *counters, enum mlx5_ib_counters_type counters_type,
	struct mlx5_ib_flow_counters_desc *desc_data, u32 ncounters)
{
	struct mlx5_ib_mcounters *mcounters = to_mcounters(counters);
	u32 cntrs_max_index = 0;
	int i;

	if (counters_type != MLX5_IB_COUNTERS_FLOW)
		return -EINVAL;

	/* init the fields for the object */
	mcounters->type = counters_type;
	mcounters->read_counters = read_flow_counters;
	mcounters->counters_num = FLOW_COUNTERS_NUM;
	mcounters->ncounters = ncounters;
	/* each counter entry have both description and index pair */
	for (i = 0; i < ncounters; i++) {
		if (desc_data[i].description > IB_COUNTER_BYTES)
			return -EINVAL;

		if (cntrs_max_index <= desc_data[i].index)
			cntrs_max_index = desc_data[i].index + 1;
	}

	mutex_lock(&mcounters->mcntrs_mutex);
	mcounters->counters_data = desc_data;
	mcounters->cntrs_max_index = cntrs_max_index;
	mutex_unlock(&mcounters->mcntrs_mutex);

	return 0;
}

#define MAX_COUNTERS_NUM (USHRT_MAX / (sizeof(u32) * 2))
int mlx5_ib_flow_counters_set_data(struct ib_counters *ibcounters,
				   struct mlx5_ib_create_flow *ucmd)
{
	struct mlx5_ib_mcounters *mcounters = to_mcounters(ibcounters);
	struct mlx5_ib_flow_counters_data *cntrs_data = NULL;
	struct mlx5_ib_flow_counters_desc *desc_data = NULL;
	bool hw_hndl = false;
	int ret = 0;

	if (ucmd && ucmd->ncounters_data != 0) {
		cntrs_data = ucmd->data;
		if (cntrs_data->ncounters > MAX_COUNTERS_NUM)
			return -EINVAL;

		desc_data = kcalloc(cntrs_data->ncounters,
				    sizeof(*desc_data),
				    GFP_KERNEL);
		if (!desc_data)
			return  -ENOMEM;

		if (copy_from_user(desc_data,
				   u64_to_user_ptr(cntrs_data->counters_data),
				   sizeof(*desc_data) * cntrs_data->ncounters)) {
			ret = -EFAULT;
			goto free;
		}
	}

	if (!mcounters->hw_cntrs_hndl) {
		mcounters->hw_cntrs_hndl = mlx5_fc_create(
			to_mdev(ibcounters->device)->mdev, false);
		if (IS_ERR(mcounters->hw_cntrs_hndl)) {
			ret = PTR_ERR(mcounters->hw_cntrs_hndl);
			goto free;
		}
		hw_hndl = true;
	}

	if (desc_data) {
		/* counters already bound to at least one flow */
		if (mcounters->cntrs_max_index) {
			ret = -EINVAL;
			goto free_hndl;
		}

		ret = counters_set_description(ibcounters,
					       MLX5_IB_COUNTERS_FLOW,
					       desc_data,
					       cntrs_data->ncounters);
		if (ret)
			goto free_hndl;

	} else if (!mcounters->cntrs_max_index) {
		/* counters not bound yet, must have udata passed */
		ret = -EINVAL;
		goto free_hndl;
	}

	return 0;

free_hndl:
	if (hw_hndl) {
		mlx5_fc_destroy(to_mdev(ibcounters->device)->mdev,
				mcounters->hw_cntrs_hndl);
		mcounters->hw_cntrs_hndl = NULL;
	}
free:
	kfree(desc_data);
	return ret;
}

void mlx5_ib_counters_clear_description(struct ib_counters *counters)
{
	struct mlx5_ib_mcounters *mcounters;

	if (!counters || atomic_read(&counters->usecnt) != 1)
		return;

	mcounters = to_mcounters(counters);

	mutex_lock(&mcounters->mcntrs_mutex);
	kfree(mcounters->counters_data);
	mcounters->counters_data = NULL;
	mcounters->cntrs_max_index = 0;
	mutex_unlock(&mcounters->mcntrs_mutex);
}

static const struct ib_device_ops hw_stats_ops = {
	.alloc_hw_stats = mlx5_ib_alloc_hw_stats,
	.get_hw_stats = mlx5_ib_get_hw_stats,
	.counter_bind_qp = mlx5_ib_counter_bind_qp,
	.counter_unbind_qp = mlx5_ib_counter_unbind_qp,
	.counter_dealloc = mlx5_ib_counter_dealloc,
	.counter_alloc_stats = mlx5_ib_counter_alloc_stats,
	.counter_update_stats = mlx5_ib_counter_update_stats,
};

static const struct ib_device_ops counters_ops = {
	.create_counters = mlx5_ib_create_counters,
	.destroy_counters = mlx5_ib_destroy_counters,
	.read_counters = mlx5_ib_read_counters,

	INIT_RDMA_OBJ_SIZE(ib_counters, mlx5_ib_mcounters, ibcntrs),
};

int mlx5_ib_counters_init(struct mlx5_ib_dev *dev)
{
	ib_set_device_ops(&dev->ib_dev, &counters_ops);

	if (!MLX5_CAP_GEN(dev->mdev, max_qp_cnt))
		return 0;

	ib_set_device_ops(&dev->ib_dev, &hw_stats_ops);
	return mlx5_ib_alloc_counters(dev);
}

void mlx5_ib_counters_cleanup(struct mlx5_ib_dev *dev)
{
	if (!MLX5_CAP_GEN(dev->mdev, max_qp_cnt))
		return;

	mlx5_ib_dealloc_counters(dev);
}
