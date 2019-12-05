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
				   roce_get_field(context->byte_4_pg_ceqn,
						  V2_CQC_BYTE_4_ARM_ST_M,
						  V2_CQC_BYTE_4_ARM_ST_S)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "ceqn",
				   roce_get_field(context->byte_4_pg_ceqn,
						  V2_CQC_BYTE_4_CEQN_M,
						  V2_CQC_BYTE_4_CEQN_S)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "cqn",
				   roce_get_field(context->byte_8_cqn,
						  V2_CQC_BYTE_8_CQN_M,
						  V2_CQC_BYTE_8_CQN_S)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "hopnum",
				   roce_get_field(context->byte_16_hop_addr,
						  V2_CQC_BYTE_16_CQE_HOP_NUM_M,
						  V2_CQC_BYTE_16_CQE_HOP_NUM_S)))
		goto err;

	if (rdma_nl_put_driver_u32(
		    msg, "pi",
		    roce_get_field(context->byte_28_cq_pi,
				   V2_CQC_BYTE_28_CQ_PRODUCER_IDX_M,
				   V2_CQC_BYTE_28_CQ_PRODUCER_IDX_S)))
		goto err;

	if (rdma_nl_put_driver_u32(
		    msg, "ci",
		    roce_get_field(context->byte_32_cq_ci,
				   V2_CQC_BYTE_32_CQ_CONSUMER_IDX_M,
				   V2_CQC_BYTE_32_CQ_CONSUMER_IDX_S)))
		goto err;

	if (rdma_nl_put_driver_u32(
		    msg, "coalesce",
		    roce_get_field(context->byte_56_cqe_period_maxcnt,
				   V2_CQC_BYTE_56_CQ_MAX_CNT_M,
				   V2_CQC_BYTE_56_CQ_MAX_CNT_S)))
		goto err;

	if (rdma_nl_put_driver_u32(
		    msg, "period",
		    roce_get_field(context->byte_56_cqe_period_maxcnt,
				   V2_CQC_BYTE_56_CQ_PERIOD_M,
				   V2_CQC_BYTE_56_CQ_PERIOD_S)))
		goto err;

	if (rdma_nl_put_driver_u32(msg, "cnt",
				   roce_get_field(context->byte_52_cqe_cnt,
						  V2_CQC_BYTE_52_CQE_CNT_M,
						  V2_CQC_BYTE_52_CQE_CNT_S)))
		goto err;

	return 0;

err:
	return -EMSGSIZE;
}

static int hns_roce_fill_res_cq_entry(struct sk_buff *msg,
				      struct rdma_restrack_entry *res)
{
	struct ib_cq *ib_cq = container_of(res, struct ib_cq, res);
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_cq->device);
	struct hns_roce_cq *hr_cq = to_hr_cq(ib_cq);
	struct hns_roce_v2_cq_context *context;
	struct nlattr *table_attr;
	int ret;

	if (!hr_dev->dfx->query_cqc_info)
		return -EINVAL;

	context = kzalloc(sizeof(struct hns_roce_v2_cq_context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	ret = hr_dev->dfx->query_cqc_info(hr_dev, hr_cq->cqn, (int *)context);
	if (ret)
		goto err;

	table_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr) {
		ret = -EMSGSIZE;
		goto err;
	}

	if (hns_roce_fill_cq(msg, context)) {
		ret = -EMSGSIZE;
		goto err_cancel_table;
	}

	nla_nest_end(msg, table_attr);
	kfree(context);

	return 0;

err_cancel_table:
	nla_nest_cancel(msg, table_attr);
err:
	kfree(context);
	return ret;
}

int hns_roce_fill_res_entry(struct sk_buff *msg,
			    struct rdma_restrack_entry *res)
{
	if (res->type == RDMA_RESTRACK_CQ)
		return hns_roce_fill_res_cq_entry(msg, res);

	return 0;
}
