// SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
// Copyright (c) 2019 Hisilicon Limited.

#include <rdma/rdma_cm.h>
#include <rdma/restrack.h>
#include <uapi/rdma/rdma_netlink.h>
#include "hnae3.h"
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_hw_v2.h"

static int hns_roce_fill_cq(struct sk_buff *msg,
			    struct hns_roce_v2_cq_context *context)
{
	if (rdma_nl_put_driver_u32(msg, "state",
				   hr_reg_read(context, CQC_ARM_ST)))

		goto err;

	if (rdma_nl_put_driver_u32(msg, "ceqn",
				   hr_reg_read(context, CQC_CEQN)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "cqn",
				   hr_reg_read(context, CQC_CQN)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "hopnum",
				   hr_reg_read(context, CQC_CQE_HOP_NUM)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "pi",
				   hr_reg_read(context, CQC_CQ_PRODUCER_IDX)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "ci",
				   hr_reg_read(context, CQC_CQ_CONSUMER_IDX)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "coalesce",
				   hr_reg_read(context, CQC_CQ_MAX_CNT)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "period",
				   hr_reg_read(context, CQC_CQ_PERIOD)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "cnt",
				   hr_reg_read(context, CQC_CQE_CNT)))
		goto err;

	return 0;

err:
	return -EMSGSIZE;
}

int hns_roce_fill_res_cq_entry(struct sk_buff *msg, struct ib_cq *ib_cq)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_cq->device);
	struct hns_roce_cq *hr_cq = to_hr_cq(ib_cq);
	struct hns_roce_v2_cq_context context;
	struct nlattr *table_attr;
	int ret;

	if (!hr_dev->hw->query_cqc)
		return -EINVAL;

	ret = hr_dev->hw->query_cqc(hr_dev, hr_cq->cqn, &context);
	if (ret)
		return -EINVAL;

	table_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr)
		return -EMSGSIZE;

	if (hns_roce_fill_cq(msg, &context))
		goto err;

	nla_nest_end(msg, table_attr);

	return 0;

err:
	nla_nest_cancel(msg, table_attr);

	return -EMSGSIZE;
}
