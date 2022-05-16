// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2019-2020, Mellanox Technologies Ltd. All rights reserved.
 */

#include <uapi/rdma/rdma_netlink.h>
#include <linux/mlx5/rsc_dump.h>
#include <rdma/ib_umem_odp.h>
#include <rdma/restrack.h>
#include "mlx5_ib.h"
#include "restrack.h"

#define MAX_DUMP_SIZE 1024

static int dump_rsc(struct mlx5_core_dev *dev, enum mlx5_sgmt_type type,
		    int index, void *data, int *data_len)
{
	struct mlx5_core_dev *mdev = dev;
	struct mlx5_rsc_dump_cmd *cmd;
	struct mlx5_rsc_key key = {};
	struct page *page;
	int offset = 0;
	int err = 0;
	int cmd_err;
	int size;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	key.size = PAGE_SIZE;
	key.rsc = type;
	key.index1 = index;
	key.num_of_obj1 = 1;

	cmd = mlx5_rsc_dump_cmd_create(mdev, &key);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto free_page;
	}

	do {
		cmd_err = mlx5_rsc_dump_next(mdev, cmd, page, &size);
		if (cmd_err < 0 || size + offset > MAX_DUMP_SIZE) {
			err = cmd_err;
			goto destroy_cmd;
		}
		memcpy(data + offset, page_address(page), size);
		offset += size;
	} while (cmd_err > 0);
	*data_len = offset;

destroy_cmd:
	mlx5_rsc_dump_cmd_destroy(cmd);
free_page:
	__free_page(page);
	return err;
}

static int fill_res_raw(struct sk_buff *msg, struct mlx5_ib_dev *dev,
			enum mlx5_sgmt_type type, u32 key)
{
	int len = 0;
	void *data;
	int err;

	data = kzalloc(MAX_DUMP_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	err = dump_rsc(dev->mdev, type, key, data, &len);
	if (err)
		goto out;

	err = nla_put(msg, RDMA_NLDEV_ATTR_RES_RAW, len, data);
out:
	kfree(data);
	return err;
}

static int fill_stat_mr_entry(struct sk_buff *msg, struct ib_mr *ibmr)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct nlattr *table_attr;

	if (!(mr->access_flags & IB_ACCESS_ON_DEMAND))
		return 0;

	table_attr = nla_nest_start(msg,
				    RDMA_NLDEV_ATTR_STAT_HWCOUNTERS);

	if (!table_attr)
		goto err;

	if (rdma_nl_stat_hwcounter_entry(msg, "page_faults",
					 atomic64_read(&mr->odp_stats.faults)))
		goto err_table;
	if (rdma_nl_stat_hwcounter_entry(
		    msg, "page_invalidations",
		    atomic64_read(&mr->odp_stats.invalidations)))
		goto err_table;
	if (rdma_nl_stat_hwcounter_entry(msg, "page_prefetch",
					 atomic64_read(&mr->odp_stats.prefetch)))
		goto err_table;

	nla_nest_end(msg, table_attr);
	return 0;

err_table:
	nla_nest_cancel(msg, table_attr);
err:
	return -EMSGSIZE;
}

static int fill_res_mr_entry_raw(struct sk_buff *msg, struct ib_mr *ibmr)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);

	return fill_res_raw(msg, mr->dev, MLX5_SGMT_TYPE_PRM_QUERY_MKEY,
			    mlx5_mkey_to_idx(mr->mmkey.key));
}

static int fill_res_mr_entry(struct sk_buff *msg, struct ib_mr *ibmr)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct nlattr *table_attr;

	if (!(mr->access_flags & IB_ACCESS_ON_DEMAND))
		return 0;

	table_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr)
		goto err;

	if (mr->is_odp_implicit) {
		if (rdma_nl_put_driver_string(msg, "odp", "implicit"))
			goto err;
	} else {
		if (rdma_nl_put_driver_string(msg, "odp", "explicit"))
			goto err;
	}

	nla_nest_end(msg, table_attr);
	return 0;

err:
	nla_nest_cancel(msg, table_attr);
	return -EMSGSIZE;
}

static int fill_res_cq_entry_raw(struct sk_buff *msg, struct ib_cq *ibcq)
{
	struct mlx5_ib_dev *dev = to_mdev(ibcq->device);
	struct mlx5_ib_cq *cq = to_mcq(ibcq);

	return fill_res_raw(msg, dev, MLX5_SGMT_TYPE_PRM_QUERY_CQ, cq->mcq.cqn);
}

static int fill_res_qp_entry_raw(struct sk_buff *msg, struct ib_qp *ibqp)
{
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);

	return fill_res_raw(msg, dev, MLX5_SGMT_TYPE_PRM_QUERY_QP,
			    ibqp->qp_num);
}

static const struct ib_device_ops restrack_ops = {
	.fill_res_cq_entry_raw = fill_res_cq_entry_raw,
	.fill_res_mr_entry = fill_res_mr_entry,
	.fill_res_mr_entry_raw = fill_res_mr_entry_raw,
	.fill_res_qp_entry_raw = fill_res_qp_entry_raw,
	.fill_stat_mr_entry = fill_stat_mr_entry,
};

int mlx5_ib_restrack_init(struct mlx5_ib_dev *dev)
{
	ib_set_device_ops(&dev->ib_dev, &restrack_ops);
	return 0;
}
