// SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
// Copyright (c) 2019 Hisilicon Limited.

#include <rdma/rdma_cm.h>
#include <rdma/restrack.h>
#include <uapi/rdma/rdma_netlink.h>
#include "hnae3.h"
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_hw_v2.h"

int hns_roce_fill_res_cq_entry(struct sk_buff *msg, struct ib_cq *ib_cq)
{
	struct hns_roce_cq *hr_cq = to_hr_cq(ib_cq);
	struct nlattr *table_attr;

	table_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr)
		return -EMSGSIZE;

	if (rdma_nl_put_driver_u32(msg, "cq_depth", hr_cq->cq_depth))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "cons_index", hr_cq->cons_index))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "cqe_size", hr_cq->cqe_size))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "arm_sn", hr_cq->arm_sn))
		goto err;

	nla_nest_end(msg, table_attr);

	return 0;

err:
	nla_nest_cancel(msg, table_attr);

	return -EMSGSIZE;
}
