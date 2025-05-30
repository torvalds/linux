// SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
// Copyright (c) 2019 Hisilicon Limited.

#include <rdma/rdma_cm.h>
#include <rdma/restrack.h>
#include <uapi/rdma/rdma_netlink.h>
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

int hns_roce_fill_res_cq_entry_raw(struct sk_buff *msg, struct ib_cq *ib_cq)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_cq->device);
	struct hns_roce_cq *hr_cq = to_hr_cq(ib_cq);
	struct hns_roce_v2_cq_context context;
	int ret;

	if (!hr_dev->hw->query_cqc)
		return -EINVAL;

	ret = hr_dev->hw->query_cqc(hr_dev, hr_cq->cqn, &context);
	if (ret)
		return -EINVAL;

	ret = nla_put(msg, RDMA_NLDEV_ATTR_RES_RAW, sizeof(context), &context);

	return ret;
}

int hns_roce_fill_res_qp_entry(struct sk_buff *msg, struct ib_qp *ib_qp)
{
	struct hns_roce_qp *hr_qp = to_hr_qp(ib_qp);
	struct nlattr *table_attr;

	table_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr)
		return -EMSGSIZE;

	if (rdma_nl_put_driver_u32_hex(msg, "sq_wqe_cnt", hr_qp->sq.wqe_cnt))
		goto err;

	if (rdma_nl_put_driver_u32_hex(msg, "sq_max_gs", hr_qp->sq.max_gs))
		goto err;

	if (rdma_nl_put_driver_u32_hex(msg, "rq_wqe_cnt", hr_qp->rq.wqe_cnt))
		goto err;

	if (rdma_nl_put_driver_u32_hex(msg, "rq_max_gs", hr_qp->rq.max_gs))
		goto err;

	if (rdma_nl_put_driver_u32_hex(msg, "ext_sge_sge_cnt", hr_qp->sge.sge_cnt))
		goto err;

	nla_nest_end(msg, table_attr);

	return 0;

err:
	nla_nest_cancel(msg, table_attr);

	return -EMSGSIZE;
}

int hns_roce_fill_res_qp_entry_raw(struct sk_buff *msg, struct ib_qp *ib_qp)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_qp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ib_qp);
	struct hns_roce_full_qp_ctx {
		struct hns_roce_v2_qp_context qpc;
		struct hns_roce_v2_scc_context sccc;
	} context = {};
	int ret;

	if (!hr_dev->hw->query_qpc)
		return -EINVAL;

	ret = hr_dev->hw->query_qpc(hr_dev, hr_qp->qpn, &context.qpc);
	if (ret)
		return ret;

	/* If SCC is disabled or the query fails, the queried SCCC will
	 * be all 0.
	 */
	if (!(hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_FLOW_CTRL) ||
	    !hr_dev->hw->query_sccc)
		goto out;

	ret = hr_dev->hw->query_sccc(hr_dev, hr_qp->qpn, &context.sccc);
	if (ret)
		ibdev_warn_ratelimited(&hr_dev->ib_dev,
				       "failed to query SCCC, ret = %d.\n",
				       ret);

out:
	ret = nla_put(msg, RDMA_NLDEV_ATTR_RES_RAW, sizeof(context), &context);

	return ret;
}

int hns_roce_fill_res_mr_entry(struct sk_buff *msg, struct ib_mr *ib_mr)
{
	struct hns_roce_mr *hr_mr = to_hr_mr(ib_mr);
	struct nlattr *table_attr;

	table_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr)
		return -EMSGSIZE;

	if (rdma_nl_put_driver_u32_hex(msg, "pbl_hop_num", hr_mr->pbl_hop_num))
		goto err;

	if (rdma_nl_put_driver_u32_hex(msg, "ba_pg_shift",
				       hr_mr->pbl_mtr.hem_cfg.ba_pg_shift))
		goto err;

	if (rdma_nl_put_driver_u32_hex(msg, "buf_pg_shift",
				       hr_mr->pbl_mtr.hem_cfg.buf_pg_shift))
		goto err;

	nla_nest_end(msg, table_attr);

	return 0;

err:
	nla_nest_cancel(msg, table_attr);

	return -EMSGSIZE;
}

int hns_roce_fill_res_mr_entry_raw(struct sk_buff *msg, struct ib_mr *ib_mr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_mr->device);
	struct hns_roce_mr *hr_mr = to_hr_mr(ib_mr);
	struct hns_roce_v2_mpt_entry context;
	int ret;

	if (!hr_dev->hw->query_mpt)
		return -EINVAL;

	ret = hr_dev->hw->query_mpt(hr_dev, hr_mr->key, &context);
	if (ret)
		return -EINVAL;

	ret = nla_put(msg, RDMA_NLDEV_ATTR_RES_RAW, sizeof(context), &context);

	return ret;
}

int hns_roce_fill_res_srq_entry(struct sk_buff *msg, struct ib_srq *ib_srq)
{
	struct hns_roce_srq *hr_srq = to_hr_srq(ib_srq);
	struct nlattr *table_attr;

	table_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr)
		return -EMSGSIZE;

	if (rdma_nl_put_driver_u32_hex(msg, "srqn", hr_srq->srqn))
		goto err;

	if (rdma_nl_put_driver_u32_hex(msg, "wqe_cnt", hr_srq->wqe_cnt))
		goto err;

	if (rdma_nl_put_driver_u32_hex(msg, "max_gs", hr_srq->max_gs))
		goto err;

	if (rdma_nl_put_driver_u32_hex(msg, "xrcdn", hr_srq->xrcdn))
		goto err;

	nla_nest_end(msg, table_attr);

	return 0;

err:
	nla_nest_cancel(msg, table_attr);
	return -EMSGSIZE;
}

int hns_roce_fill_res_srq_entry_raw(struct sk_buff *msg, struct ib_srq *ib_srq)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_srq->device);
	struct hns_roce_srq *hr_srq = to_hr_srq(ib_srq);
	struct hns_roce_srq_context context;
	int ret;

	if (!hr_dev->hw->query_srqc)
		return -EINVAL;

	ret = hr_dev->hw->query_srqc(hr_dev, hr_srq->srqn, &context);
	if (ret)
		return ret;

	ret = nla_put(msg, RDMA_NLDEV_ATTR_RES_RAW, sizeof(context), &context);

	return ret;
}
