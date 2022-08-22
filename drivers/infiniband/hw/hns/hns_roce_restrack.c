// SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
// Copyright (c) 2019 Hisilicon Limited.

#include <rdma/rdma_cm.h>
#include <rdma/restrack.h>
#include <uapi/rdma/rdma_netlink.h>
#include "hnae3.h"
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_hw_v2.h"

#define MAX_ENTRY_NUM 256

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
	u32 data[MAX_ENTRY_NUM] = {};
	int offset = 0;
	int ret;

	if (!hr_dev->hw->query_cqc)
		return -EINVAL;

	ret = hr_dev->hw->query_cqc(hr_dev, hr_cq->cqn, &context);
	if (ret)
		return -EINVAL;

	data[offset++] = hr_reg_read(&context, CQC_CQ_ST);
	data[offset++] = hr_reg_read(&context, CQC_SHIFT);
	data[offset++] = hr_reg_read(&context, CQC_CQE_SIZE);
	data[offset++] = hr_reg_read(&context, CQC_CQE_CNT);
	data[offset++] = hr_reg_read(&context, CQC_CQ_PRODUCER_IDX);
	data[offset++] = hr_reg_read(&context, CQC_CQ_CONSUMER_IDX);
	data[offset++] = hr_reg_read(&context, CQC_DB_RECORD_EN);
	data[offset++] = hr_reg_read(&context, CQC_ARM_ST);
	data[offset++] = hr_reg_read(&context, CQC_CMD_SN);
	data[offset++] = hr_reg_read(&context, CQC_CEQN);
	data[offset++] = hr_reg_read(&context, CQC_CQ_MAX_CNT);
	data[offset++] = hr_reg_read(&context, CQC_CQ_PERIOD);
	data[offset++] = hr_reg_read(&context, CQC_CQE_HOP_NUM);
	data[offset++] = hr_reg_read(&context, CQC_CQE_BAR_PG_SZ);
	data[offset++] = hr_reg_read(&context, CQC_CQE_BUF_PG_SZ);

	ret = nla_put(msg, RDMA_NLDEV_ATTR_RES_RAW, offset * sizeof(u32), data);

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
	struct hns_roce_v2_qp_context context;
	u32 data[MAX_ENTRY_NUM] = {};
	int offset = 0;
	int ret;

	if (!hr_dev->hw->query_qpc)
		return -EINVAL;

	ret = hr_dev->hw->query_qpc(hr_dev, hr_qp->qpn, &context);
	if (ret)
		return -EINVAL;

	data[offset++] = hr_reg_read(&context, QPC_QP_ST);
	data[offset++] = hr_reg_read(&context, QPC_ERR_TYPE);
	data[offset++] = hr_reg_read(&context, QPC_CHECK_FLG);
	data[offset++] = hr_reg_read(&context, QPC_SRQ_EN);
	data[offset++] = hr_reg_read(&context, QPC_SRQN);
	data[offset++] = hr_reg_read(&context, QPC_QKEY_XRCD);
	data[offset++] = hr_reg_read(&context, QPC_TX_CQN);
	data[offset++] = hr_reg_read(&context, QPC_RX_CQN);
	data[offset++] = hr_reg_read(&context, QPC_SQ_PRODUCER_IDX);
	data[offset++] = hr_reg_read(&context, QPC_SQ_CONSUMER_IDX);
	data[offset++] = hr_reg_read(&context, QPC_RQ_RECORD_EN);
	data[offset++] = hr_reg_read(&context, QPC_RQ_PRODUCER_IDX);
	data[offset++] = hr_reg_read(&context, QPC_RQ_CONSUMER_IDX);
	data[offset++] = hr_reg_read(&context, QPC_SQ_SHIFT);
	data[offset++] = hr_reg_read(&context, QPC_RQWS);
	data[offset++] = hr_reg_read(&context, QPC_RQ_SHIFT);
	data[offset++] = hr_reg_read(&context, QPC_SGE_SHIFT);
	data[offset++] = hr_reg_read(&context, QPC_SQ_HOP_NUM);
	data[offset++] = hr_reg_read(&context, QPC_RQ_HOP_NUM);
	data[offset++] = hr_reg_read(&context, QPC_SGE_HOP_NUM);
	data[offset++] = hr_reg_read(&context, QPC_WQE_SGE_BA_PG_SZ);
	data[offset++] = hr_reg_read(&context, QPC_WQE_SGE_BUF_PG_SZ);
	data[offset++] = hr_reg_read(&context, QPC_RETRY_NUM_INIT);
	data[offset++] = hr_reg_read(&context, QPC_RETRY_CNT);
	data[offset++] = hr_reg_read(&context, QPC_SQ_CUR_PSN);
	data[offset++] = hr_reg_read(&context, QPC_SQ_MAX_PSN);
	data[offset++] = hr_reg_read(&context, QPC_SQ_FLUSH_IDX);
	data[offset++] = hr_reg_read(&context, QPC_SQ_MAX_IDX);
	data[offset++] = hr_reg_read(&context, QPC_SQ_TX_ERR);
	data[offset++] = hr_reg_read(&context, QPC_SQ_RX_ERR);
	data[offset++] = hr_reg_read(&context, QPC_RQ_RX_ERR);
	data[offset++] = hr_reg_read(&context, QPC_RQ_TX_ERR);
	data[offset++] = hr_reg_read(&context, QPC_RQ_CQE_IDX);
	data[offset++] = hr_reg_read(&context, QPC_RQ_RTY_TX_ERR);

	ret = nla_put(msg, RDMA_NLDEV_ATTR_RES_RAW, offset * sizeof(u32), data);

	return ret;
}
