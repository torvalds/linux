// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2019, Mellanox Technologies inc.  All rights reserved.
 */

#include <uapi/rdma/rdma_netlink.h>
#include <rdma/ib_umem_odp.h>
#include <rdma/restrack.h>
#include "mlx5_ib.h"

static int fill_stat_mr_entry(struct sk_buff *msg,
			      struct rdma_restrack_entry *res)
{
	struct ib_mr *ibmr = container_of(res, struct ib_mr, res);
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

	nla_nest_end(msg, table_attr);
	return 0;

err_table:
	nla_nest_cancel(msg, table_attr);
err:
	return -EMSGSIZE;
}

static int fill_res_mr_entry(struct sk_buff *msg,
			     struct rdma_restrack_entry *res)
{
	struct ib_mr *ibmr = container_of(res, struct ib_mr, res);
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

int mlx5_ib_fill_res_entry(struct sk_buff *msg,
			   struct rdma_restrack_entry *res)
{
	if (res->type == RDMA_RESTRACK_MR)
		return fill_res_mr_entry(msg, res);

	return 0;
}

int mlx5_ib_fill_stat_entry(struct sk_buff *msg,
			    struct rdma_restrack_entry *res)
{
	if (res->type == RDMA_RESTRACK_MR)
		return fill_stat_mr_entry(msg, res);

	return 0;
}
