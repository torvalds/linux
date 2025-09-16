// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include "hinic3_cmdq.h"
#include "hinic3_hw_comm.h"
#include "hinic3_hw_intf.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_io.h"

#define HINIC3_DEFAULT_TX_CI_PENDING_LIMIT    1
#define HINIC3_DEFAULT_TX_CI_COALESCING_TIME  1
#define HINIC3_DEFAULT_DROP_THD_ON            (0xFFFF)
#define HINIC3_DEFAULT_DROP_THD_OFF           0

#define HINIC3_CI_Q_ADDR_SIZE                (64)

#define HINIC3_CI_TABLE_SIZE(num_qps)  \
	(ALIGN((num_qps) * HINIC3_CI_Q_ADDR_SIZE, HINIC3_MIN_PAGE_SIZE))

#define HINIC3_CI_VADDR(base_addr, q_id)  \
	((u8 *)(base_addr) + (q_id) * HINIC3_CI_Q_ADDR_SIZE)

#define HINIC3_CI_PADDR(base_paddr, q_id)  \
	((base_paddr) + (q_id) * HINIC3_CI_Q_ADDR_SIZE)

#define SQ_WQ_PREFETCH_MAX        1
#define SQ_WQ_PREFETCH_MIN        1
#define SQ_WQ_PREFETCH_THRESHOLD  16

#define RQ_WQ_PREFETCH_MAX        4
#define RQ_WQ_PREFETCH_MIN        1
#define RQ_WQ_PREFETCH_THRESHOLD  256

/* (2048 - 8) / 64 */
#define HINIC3_Q_CTXT_MAX         31

enum hinic3_qp_ctxt_type {
	HINIC3_QP_CTXT_TYPE_SQ = 0,
	HINIC3_QP_CTXT_TYPE_RQ = 1,
};

struct hinic3_qp_ctxt_hdr {
	__le16 num_queues;
	__le16 queue_type;
	__le16 start_qid;
	__le16 rsvd;
};

struct hinic3_sq_ctxt {
	__le32 ci_pi;
	__le32 drop_mode_sp;
	__le32 wq_pfn_hi_owner;
	__le32 wq_pfn_lo;

	__le32 rsvd0;
	__le32 pkt_drop_thd;
	__le32 global_sq_id;
	__le32 vlan_ceq_attr;

	__le32 pref_cache;
	__le32 pref_ci_owner;
	__le32 pref_wq_pfn_hi_ci;
	__le32 pref_wq_pfn_lo;

	__le32 rsvd8;
	__le32 rsvd9;
	__le32 wq_block_pfn_hi;
	__le32 wq_block_pfn_lo;
};

struct hinic3_rq_ctxt {
	__le32 ci_pi;
	__le32 ceq_attr;
	__le32 wq_pfn_hi_type_owner;
	__le32 wq_pfn_lo;

	__le32 rsvd[3];
	__le32 cqe_sge_len;

	__le32 pref_cache;
	__le32 pref_ci_owner;
	__le32 pref_wq_pfn_hi_ci;
	__le32 pref_wq_pfn_lo;

	__le32 pi_paddr_hi;
	__le32 pi_paddr_lo;
	__le32 wq_block_pfn_hi;
	__le32 wq_block_pfn_lo;
};

struct hinic3_sq_ctxt_block {
	struct hinic3_qp_ctxt_hdr cmdq_hdr;
	struct hinic3_sq_ctxt     sq_ctxt[HINIC3_Q_CTXT_MAX];
};

struct hinic3_rq_ctxt_block {
	struct hinic3_qp_ctxt_hdr cmdq_hdr;
	struct hinic3_rq_ctxt     rq_ctxt[HINIC3_Q_CTXT_MAX];
};

struct hinic3_clean_queue_ctxt {
	struct hinic3_qp_ctxt_hdr cmdq_hdr;
	__le32                    rsvd;
};

#define SQ_CTXT_SIZE(num_sqs)  \
	(sizeof(struct hinic3_qp_ctxt_hdr) +  \
	(num_sqs) * sizeof(struct hinic3_sq_ctxt))

#define RQ_CTXT_SIZE(num_rqs)  \
	(sizeof(struct hinic3_qp_ctxt_hdr) +  \
	(num_rqs) * sizeof(struct hinic3_rq_ctxt))

#define SQ_CTXT_PREF_CI_HI_SHIFT           12
#define SQ_CTXT_PREF_CI_HI(val)            ((val) >> SQ_CTXT_PREF_CI_HI_SHIFT)

#define SQ_CTXT_PI_IDX_MASK                GENMASK(15, 0)
#define SQ_CTXT_CI_IDX_MASK                GENMASK(31, 16)
#define SQ_CTXT_CI_PI_SET(val, member)  \
	FIELD_PREP(SQ_CTXT_##member##_MASK, val)

#define SQ_CTXT_MODE_SP_FLAG_MASK          BIT(0)
#define SQ_CTXT_MODE_PKT_DROP_MASK         BIT(1)
#define SQ_CTXT_MODE_SET(val, member)  \
	FIELD_PREP(SQ_CTXT_MODE_##member##_MASK, val)

#define SQ_CTXT_WQ_PAGE_HI_PFN_MASK        GENMASK(19, 0)
#define SQ_CTXT_WQ_PAGE_OWNER_MASK         BIT(23)
#define SQ_CTXT_WQ_PAGE_SET(val, member)  \
	FIELD_PREP(SQ_CTXT_WQ_PAGE_##member##_MASK, val)

#define SQ_CTXT_PKT_DROP_THD_ON_MASK       GENMASK(15, 0)
#define SQ_CTXT_PKT_DROP_THD_OFF_MASK      GENMASK(31, 16)
#define SQ_CTXT_PKT_DROP_THD_SET(val, member)  \
	FIELD_PREP(SQ_CTXT_PKT_DROP_##member##_MASK, val)

#define SQ_CTXT_GLOBAL_SQ_ID_MASK          GENMASK(12, 0)
#define SQ_CTXT_GLOBAL_QUEUE_ID_SET(val, member)  \
	FIELD_PREP(SQ_CTXT_##member##_MASK, val)

#define SQ_CTXT_VLAN_INSERT_MODE_MASK      GENMASK(20, 19)
#define SQ_CTXT_VLAN_CEQ_EN_MASK           BIT(23)
#define SQ_CTXT_VLAN_CEQ_SET(val, member)  \
	FIELD_PREP(SQ_CTXT_VLAN_##member##_MASK, val)

#define SQ_CTXT_PREF_CACHE_THRESHOLD_MASK  GENMASK(13, 0)
#define SQ_CTXT_PREF_CACHE_MAX_MASK        GENMASK(24, 14)
#define SQ_CTXT_PREF_CACHE_MIN_MASK        GENMASK(31, 25)

#define SQ_CTXT_PREF_CI_HI_MASK            GENMASK(3, 0)
#define SQ_CTXT_PREF_OWNER_MASK            BIT(4)

#define SQ_CTXT_PREF_WQ_PFN_HI_MASK        GENMASK(19, 0)
#define SQ_CTXT_PREF_CI_LOW_MASK           GENMASK(31, 20)
#define SQ_CTXT_PREF_SET(val, member)  \
	FIELD_PREP(SQ_CTXT_PREF_##member##_MASK, val)

#define SQ_CTXT_WQ_BLOCK_PFN_HI_MASK       GENMASK(22, 0)
#define SQ_CTXT_WQ_BLOCK_SET(val, member)  \
	FIELD_PREP(SQ_CTXT_WQ_BLOCK_##member##_MASK, val)

#define RQ_CTXT_PI_IDX_MASK                GENMASK(15, 0)
#define RQ_CTXT_CI_IDX_MASK                GENMASK(31, 16)
#define RQ_CTXT_CI_PI_SET(val, member)  \
	FIELD_PREP(RQ_CTXT_##member##_MASK, val)

#define RQ_CTXT_CEQ_ATTR_INTR_MASK         GENMASK(30, 21)
#define RQ_CTXT_CEQ_ATTR_EN_MASK           BIT(31)
#define RQ_CTXT_CEQ_ATTR_SET(val, member)  \
	FIELD_PREP(RQ_CTXT_CEQ_ATTR_##member##_MASK, val)

#define RQ_CTXT_WQ_PAGE_HI_PFN_MASK        GENMASK(19, 0)
#define RQ_CTXT_WQ_PAGE_WQE_TYPE_MASK      GENMASK(29, 28)
#define RQ_CTXT_WQ_PAGE_OWNER_MASK         BIT(31)
#define RQ_CTXT_WQ_PAGE_SET(val, member)  \
	FIELD_PREP(RQ_CTXT_WQ_PAGE_##member##_MASK, val)

#define RQ_CTXT_CQE_LEN_MASK               GENMASK(29, 28)
#define RQ_CTXT_CQE_LEN_SET(val, member)  \
	FIELD_PREP(RQ_CTXT_##member##_MASK, val)

#define RQ_CTXT_PREF_CACHE_THRESHOLD_MASK  GENMASK(13, 0)
#define RQ_CTXT_PREF_CACHE_MAX_MASK        GENMASK(24, 14)
#define RQ_CTXT_PREF_CACHE_MIN_MASK        GENMASK(31, 25)

#define RQ_CTXT_PREF_CI_HI_MASK            GENMASK(3, 0)
#define RQ_CTXT_PREF_OWNER_MASK            BIT(4)

#define RQ_CTXT_PREF_WQ_PFN_HI_MASK        GENMASK(19, 0)
#define RQ_CTXT_PREF_CI_LOW_MASK           GENMASK(31, 20)
#define RQ_CTXT_PREF_SET(val, member)  \
	FIELD_PREP(RQ_CTXT_PREF_##member##_MASK, val)

#define RQ_CTXT_WQ_BLOCK_PFN_HI_MASK       GENMASK(22, 0)
#define RQ_CTXT_WQ_BLOCK_SET(val, member)  \
	FIELD_PREP(RQ_CTXT_WQ_BLOCK_##member##_MASK, val)

#define WQ_PAGE_PFN_SHIFT       12
#define WQ_BLOCK_PFN_SHIFT      9
#define WQ_PAGE_PFN(page_addr)  ((page_addr) >> WQ_PAGE_PFN_SHIFT)
#define WQ_BLOCK_PFN(page_addr) ((page_addr) >> WQ_BLOCK_PFN_SHIFT)

int hinic3_init_nic_io(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_nic_io *nic_io;
	int err;

	nic_io = kzalloc(sizeof(*nic_io), GFP_KERNEL);
	if (!nic_io)
		return -ENOMEM;

	nic_dev->nic_io = nic_io;

	err = hinic3_set_func_svc_used_state(hwdev, COMM_FUNC_SVC_T_NIC, 1);
	if (err) {
		dev_err(hwdev->dev, "Failed to set function svc used state\n");
		goto err_free_nicio;
	}

	err = hinic3_init_function_table(nic_dev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init function table\n");
		goto err_clear_func_svc_used_state;
	}

	nic_io->rx_buf_len = nic_dev->rx_buf_len;

	err = hinic3_get_nic_feature_from_hw(nic_dev);
	if (err) {
		dev_err(hwdev->dev, "Failed to get nic features\n");
		goto err_clear_func_svc_used_state;
	}

	nic_io->feature_cap &= HINIC3_NIC_F_ALL_MASK;
	nic_io->feature_cap &= HINIC3_NIC_DRV_DEFAULT_FEATURE;
	dev_dbg(hwdev->dev, "nic features: 0x%llx\n\n", nic_io->feature_cap);

	return 0;

err_clear_func_svc_used_state:
	hinic3_set_func_svc_used_state(hwdev, COMM_FUNC_SVC_T_NIC, 0);
err_free_nicio:
	nic_dev->nic_io = NULL;
	kfree(nic_io);

	return err;
}

void hinic3_free_nic_io(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;

	hinic3_set_func_svc_used_state(nic_dev->hwdev, COMM_FUNC_SVC_T_NIC, 0);
	nic_dev->nic_io = NULL;
	kfree(nic_io);
}

int hinic3_init_nicio_res(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	void __iomem *db_base;
	int err;

	nic_io->max_qps = hinic3_func_max_qnum(hwdev);

	err = hinic3_alloc_db_addr(hwdev, &db_base, NULL);
	if (err) {
		dev_err(hwdev->dev, "Failed to allocate doorbell for sqs\n");
		return err;
	}
	nic_io->sqs_db_addr = db_base;

	err = hinic3_alloc_db_addr(hwdev, &db_base, NULL);
	if (err) {
		hinic3_free_db_addr(hwdev, nic_io->sqs_db_addr);
		dev_err(hwdev->dev, "Failed to allocate doorbell for rqs\n");
		return err;
	}
	nic_io->rqs_db_addr = db_base;

	nic_io->ci_vaddr_base =
		dma_alloc_coherent(hwdev->dev,
				   HINIC3_CI_TABLE_SIZE(nic_io->max_qps),
				   &nic_io->ci_dma_base,
				   GFP_KERNEL);
	if (!nic_io->ci_vaddr_base) {
		hinic3_free_db_addr(hwdev, nic_io->sqs_db_addr);
		hinic3_free_db_addr(hwdev, nic_io->rqs_db_addr);
		return -ENOMEM;
	}

	return 0;
}

void hinic3_free_nicio_res(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;

	dma_free_coherent(hwdev->dev,
			  HINIC3_CI_TABLE_SIZE(nic_io->max_qps),
			  nic_io->ci_vaddr_base, nic_io->ci_dma_base);

	hinic3_free_db_addr(hwdev, nic_io->sqs_db_addr);
	hinic3_free_db_addr(hwdev, nic_io->rqs_db_addr);
}

static int hinic3_create_sq(struct hinic3_hwdev *hwdev,
			    struct hinic3_io_queue *sq,
			    u16 q_id, u32 sq_depth, u16 sq_msix_idx)
{
	int err;

	/* sq used & hardware request init 1 */
	sq->owner = 1;

	sq->q_id = q_id;
	sq->msix_entry_idx = sq_msix_idx;

	err = hinic3_wq_create(hwdev, &sq->wq, sq_depth,
			       BIT(HINIC3_SQ_WQEBB_SHIFT));
	if (err) {
		dev_err(hwdev->dev, "Failed to create tx queue %u wq\n",
			q_id);
		return err;
	}

	return 0;
}

static int hinic3_create_rq(struct hinic3_hwdev *hwdev,
			    struct hinic3_io_queue *rq,
			    u16 q_id, u32 rq_depth, u16 rq_msix_idx)
{
	int err;

	rq->q_id = q_id;
	rq->msix_entry_idx = rq_msix_idx;

	err = hinic3_wq_create(hwdev, &rq->wq, rq_depth,
			       BIT(HINIC3_RQ_WQEBB_SHIFT +
				   HINIC3_NORMAL_RQ_WQE));
	if (err) {
		dev_err(hwdev->dev, "Failed to create rx queue %u wq\n",
			q_id);
		return err;
	}

	return 0;
}

static int hinic3_create_qp(struct hinic3_hwdev *hwdev,
			    struct hinic3_io_queue *sq,
			    struct hinic3_io_queue *rq, u16 q_id, u32 sq_depth,
			    u32 rq_depth, u16 qp_msix_idx)
{
	int err;

	err = hinic3_create_sq(hwdev, sq, q_id, sq_depth, qp_msix_idx);
	if (err) {
		dev_err(hwdev->dev, "Failed to create sq, qid: %u\n",
			q_id);
		return err;
	}

	err = hinic3_create_rq(hwdev, rq, q_id, rq_depth, qp_msix_idx);
	if (err) {
		dev_err(hwdev->dev, "Failed to create rq, qid: %u\n",
			q_id);
		goto err_destroy_sq_wq;
	}

	return 0;

err_destroy_sq_wq:
	hinic3_wq_destroy(hwdev, &sq->wq);

	return err;
}

static void hinic3_destroy_qp(struct hinic3_hwdev *hwdev,
			      struct hinic3_io_queue *sq,
			      struct hinic3_io_queue *rq)
{
	hinic3_wq_destroy(hwdev, &sq->wq);
	hinic3_wq_destroy(hwdev, &rq->wq);
}

int hinic3_alloc_qps(struct hinic3_nic_dev *nic_dev,
		     struct hinic3_dyna_qp_params *qp_params)
{
	struct msix_entry *qps_msix_entries = nic_dev->qps_msix_entries;
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_io_queue *sqs;
	struct hinic3_io_queue *rqs;
	u16 q_id;
	int err;

	if (qp_params->num_qps > nic_io->max_qps || !qp_params->num_qps)
		return -EINVAL;

	sqs = kcalloc(qp_params->num_qps, sizeof(*sqs), GFP_KERNEL);
	if (!sqs) {
		err = -ENOMEM;
		goto err_out;
	}

	rqs = kcalloc(qp_params->num_qps, sizeof(*rqs), GFP_KERNEL);
	if (!rqs) {
		err = -ENOMEM;
		goto err_free_sqs;
	}

	for (q_id = 0; q_id < qp_params->num_qps; q_id++) {
		err = hinic3_create_qp(hwdev, &sqs[q_id], &rqs[q_id], q_id,
				       qp_params->sq_depth, qp_params->rq_depth,
				       qps_msix_entries[q_id].entry);
		if (err) {
			dev_err(hwdev->dev, "Failed to allocate qp %u, err: %d\n",
				q_id, err);
			goto err_destroy_qp;
		}
	}

	qp_params->sqs = sqs;
	qp_params->rqs = rqs;

	return 0;

err_destroy_qp:
	while (q_id > 0) {
		q_id--;
		hinic3_destroy_qp(hwdev, &sqs[q_id], &rqs[q_id]);
	}
	kfree(rqs);
err_free_sqs:
	kfree(sqs);
err_out:
	return err;
}

void hinic3_free_qps(struct hinic3_nic_dev *nic_dev,
		     struct hinic3_dyna_qp_params *qp_params)
{
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	u16 q_id;

	for (q_id = 0; q_id < qp_params->num_qps; q_id++)
		hinic3_destroy_qp(hwdev, &qp_params->sqs[q_id],
				  &qp_params->rqs[q_id]);

	kfree(qp_params->sqs);
	kfree(qp_params->rqs);
}

void hinic3_init_qps(struct hinic3_nic_dev *nic_dev,
		     struct hinic3_dyna_qp_params *qp_params)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_io_queue *sqs = qp_params->sqs;
	struct hinic3_io_queue *rqs = qp_params->rqs;
	u16 q_id;

	nic_io->num_qps = qp_params->num_qps;
	nic_io->sq = qp_params->sqs;
	nic_io->rq = qp_params->rqs;
	for (q_id = 0; q_id < nic_io->num_qps; q_id++) {
		sqs[q_id].cons_idx_addr =
			(u16 *)HINIC3_CI_VADDR(nic_io->ci_vaddr_base, q_id);
		/* clear ci value */
		WRITE_ONCE(*sqs[q_id].cons_idx_addr, 0);

		sqs[q_id].db_addr = nic_io->sqs_db_addr;
		rqs[q_id].db_addr = nic_io->rqs_db_addr;
	}
}

void hinic3_uninit_qps(struct hinic3_nic_dev *nic_dev,
		       struct hinic3_dyna_qp_params *qp_params)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;

	qp_params->sqs = nic_io->sq;
	qp_params->rqs = nic_io->rq;
	qp_params->num_qps = nic_io->num_qps;
}

static void hinic3_qp_prepare_cmdq_header(struct hinic3_qp_ctxt_hdr *qp_ctxt_hdr,
					  enum hinic3_qp_ctxt_type ctxt_type,
					  u16 num_queues, u16 q_id)
{
	qp_ctxt_hdr->queue_type = cpu_to_le16(ctxt_type);
	qp_ctxt_hdr->num_queues = cpu_to_le16(num_queues);
	qp_ctxt_hdr->start_qid = cpu_to_le16(q_id);
	qp_ctxt_hdr->rsvd = 0;
}

static void hinic3_sq_prepare_ctxt(struct hinic3_io_queue *sq, u16 sq_id,
				   struct hinic3_sq_ctxt *sq_ctxt)
{
	u64 wq_page_addr, wq_page_pfn, wq_block_pfn;
	u32 wq_block_pfn_hi, wq_block_pfn_lo;
	u32 wq_page_pfn_hi, wq_page_pfn_lo;
	u16 pi_start, ci_start;

	ci_start = hinic3_get_sq_local_ci(sq);
	pi_start = hinic3_get_sq_local_pi(sq);

	wq_page_addr = hinic3_wq_get_first_wqe_page_addr(&sq->wq);

	wq_page_pfn = WQ_PAGE_PFN(wq_page_addr);
	wq_page_pfn_hi = upper_32_bits(wq_page_pfn);
	wq_page_pfn_lo = lower_32_bits(wq_page_pfn);

	wq_block_pfn = WQ_BLOCK_PFN(sq->wq.wq_block_paddr);
	wq_block_pfn_hi = upper_32_bits(wq_block_pfn);
	wq_block_pfn_lo = lower_32_bits(wq_block_pfn);

	sq_ctxt->ci_pi =
		cpu_to_le32(SQ_CTXT_CI_PI_SET(ci_start, CI_IDX) |
			    SQ_CTXT_CI_PI_SET(pi_start, PI_IDX));

	sq_ctxt->drop_mode_sp =
		cpu_to_le32(SQ_CTXT_MODE_SET(0, SP_FLAG) |
			    SQ_CTXT_MODE_SET(0, PKT_DROP));

	sq_ctxt->wq_pfn_hi_owner =
		cpu_to_le32(SQ_CTXT_WQ_PAGE_SET(wq_page_pfn_hi, HI_PFN) |
			    SQ_CTXT_WQ_PAGE_SET(1, OWNER));

	sq_ctxt->wq_pfn_lo = cpu_to_le32(wq_page_pfn_lo);

	sq_ctxt->pkt_drop_thd =
		cpu_to_le32(SQ_CTXT_PKT_DROP_THD_SET(HINIC3_DEFAULT_DROP_THD_ON, THD_ON) |
			    SQ_CTXT_PKT_DROP_THD_SET(HINIC3_DEFAULT_DROP_THD_OFF, THD_OFF));

	sq_ctxt->global_sq_id =
		cpu_to_le32(SQ_CTXT_GLOBAL_QUEUE_ID_SET((u32)sq_id,
							GLOBAL_SQ_ID));

	/* enable insert c-vlan by default */
	sq_ctxt->vlan_ceq_attr =
		cpu_to_le32(SQ_CTXT_VLAN_CEQ_SET(0, CEQ_EN) |
			    SQ_CTXT_VLAN_CEQ_SET(1, INSERT_MODE));

	sq_ctxt->rsvd0 = 0;

	sq_ctxt->pref_cache =
		cpu_to_le32(SQ_CTXT_PREF_SET(SQ_WQ_PREFETCH_MIN, CACHE_MIN) |
			    SQ_CTXT_PREF_SET(SQ_WQ_PREFETCH_MAX, CACHE_MAX) |
			    SQ_CTXT_PREF_SET(SQ_WQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD));

	sq_ctxt->pref_ci_owner =
		cpu_to_le32(SQ_CTXT_PREF_SET(SQ_CTXT_PREF_CI_HI(ci_start), CI_HI) |
			    SQ_CTXT_PREF_SET(1, OWNER));

	sq_ctxt->pref_wq_pfn_hi_ci =
		cpu_to_le32(SQ_CTXT_PREF_SET(ci_start, CI_LOW) |
			    SQ_CTXT_PREF_SET(wq_page_pfn_hi, WQ_PFN_HI));

	sq_ctxt->pref_wq_pfn_lo = cpu_to_le32(wq_page_pfn_lo);

	sq_ctxt->wq_block_pfn_hi =
		cpu_to_le32(SQ_CTXT_WQ_BLOCK_SET(wq_block_pfn_hi, PFN_HI));

	sq_ctxt->wq_block_pfn_lo = cpu_to_le32(wq_block_pfn_lo);
}

static void hinic3_rq_prepare_ctxt_get_wq_info(struct hinic3_io_queue *rq,
					       u32 *wq_page_pfn_hi,
					       u32 *wq_page_pfn_lo,
					       u32 *wq_block_pfn_hi,
					       u32 *wq_block_pfn_lo)
{
	u64 wq_page_addr, wq_page_pfn, wq_block_pfn;

	wq_page_addr = hinic3_wq_get_first_wqe_page_addr(&rq->wq);

	wq_page_pfn = WQ_PAGE_PFN(wq_page_addr);
	*wq_page_pfn_hi = upper_32_bits(wq_page_pfn);
	*wq_page_pfn_lo = lower_32_bits(wq_page_pfn);

	wq_block_pfn = WQ_BLOCK_PFN(rq->wq.wq_block_paddr);
	*wq_block_pfn_hi = upper_32_bits(wq_block_pfn);
	*wq_block_pfn_lo = lower_32_bits(wq_block_pfn);
}

static void hinic3_rq_prepare_ctxt(struct hinic3_io_queue *rq,
				   struct hinic3_rq_ctxt *rq_ctxt)
{
	u32 wq_block_pfn_hi, wq_block_pfn_lo;
	u32 wq_page_pfn_hi, wq_page_pfn_lo;
	u16 pi_start, ci_start;

	ci_start = (rq->wq.cons_idx & rq->wq.idx_mask) << HINIC3_NORMAL_RQ_WQE;
	pi_start = (rq->wq.prod_idx & rq->wq.idx_mask) << HINIC3_NORMAL_RQ_WQE;

	hinic3_rq_prepare_ctxt_get_wq_info(rq, &wq_page_pfn_hi, &wq_page_pfn_lo,
					   &wq_block_pfn_hi, &wq_block_pfn_lo);

	rq_ctxt->ci_pi =
		cpu_to_le32(RQ_CTXT_CI_PI_SET(ci_start, CI_IDX) |
			    RQ_CTXT_CI_PI_SET(pi_start, PI_IDX));

	rq_ctxt->ceq_attr =
		cpu_to_le32(RQ_CTXT_CEQ_ATTR_SET(0, EN) |
			    RQ_CTXT_CEQ_ATTR_SET(rq->msix_entry_idx, INTR));

	rq_ctxt->wq_pfn_hi_type_owner =
		cpu_to_le32(RQ_CTXT_WQ_PAGE_SET(wq_page_pfn_hi, HI_PFN) |
			    RQ_CTXT_WQ_PAGE_SET(1, OWNER));

	/* use 16Byte WQE */
	rq_ctxt->wq_pfn_hi_type_owner |=
		cpu_to_le32(RQ_CTXT_WQ_PAGE_SET(2, WQE_TYPE));
	rq_ctxt->cqe_sge_len = cpu_to_le32(RQ_CTXT_CQE_LEN_SET(1, CQE_LEN));

	rq_ctxt->wq_pfn_lo = cpu_to_le32(wq_page_pfn_lo);

	rq_ctxt->pref_cache =
		cpu_to_le32(RQ_CTXT_PREF_SET(RQ_WQ_PREFETCH_MIN, CACHE_MIN) |
			    RQ_CTXT_PREF_SET(RQ_WQ_PREFETCH_MAX, CACHE_MAX) |
			    RQ_CTXT_PREF_SET(RQ_WQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD));

	rq_ctxt->pref_ci_owner =
		cpu_to_le32(RQ_CTXT_PREF_SET(SQ_CTXT_PREF_CI_HI(ci_start), CI_HI) |
			    RQ_CTXT_PREF_SET(1, OWNER));

	rq_ctxt->pref_wq_pfn_hi_ci =
		cpu_to_le32(RQ_CTXT_PREF_SET(wq_page_pfn_hi, WQ_PFN_HI) |
			    RQ_CTXT_PREF_SET(ci_start, CI_LOW));

	rq_ctxt->pref_wq_pfn_lo = cpu_to_le32(wq_page_pfn_lo);

	rq_ctxt->wq_block_pfn_hi =
		cpu_to_le32(RQ_CTXT_WQ_BLOCK_SET(wq_block_pfn_hi, PFN_HI));

	rq_ctxt->wq_block_pfn_lo = cpu_to_le32(wq_block_pfn_lo);
}

static int init_sq_ctxts(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_sq_ctxt_block *sq_ctxt_block;
	u16 q_id, curr_id, max_ctxts, i;
	struct hinic3_sq_ctxt *sq_ctxt;
	struct hinic3_cmd_buf *cmd_buf;
	struct hinic3_io_queue *sq;
	__le64 out_param;
	int err = 0;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		dev_err(hwdev->dev, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	q_id = 0;
	while (q_id < nic_io->num_qps) {
		sq_ctxt_block = cmd_buf->buf;
		sq_ctxt = sq_ctxt_block->sq_ctxt;

		max_ctxts = (nic_io->num_qps - q_id) > HINIC3_Q_CTXT_MAX ?
			     HINIC3_Q_CTXT_MAX : (nic_io->num_qps - q_id);

		hinic3_qp_prepare_cmdq_header(&sq_ctxt_block->cmdq_hdr,
					      HINIC3_QP_CTXT_TYPE_SQ, max_ctxts,
					      q_id);

		for (i = 0; i < max_ctxts; i++) {
			curr_id = q_id + i;
			sq = &nic_io->sq[curr_id];
			hinic3_sq_prepare_ctxt(sq, curr_id, &sq_ctxt[i]);
		}

		hinic3_cmdq_buf_swab32(sq_ctxt_block, sizeof(*sq_ctxt_block));

		cmd_buf->size = cpu_to_le16(SQ_CTXT_SIZE(max_ctxts));
		err = hinic3_cmdq_direct_resp(hwdev, MGMT_MOD_L2NIC,
					      L2NIC_UCODE_CMD_MODIFY_QUEUE_CTX,
					      cmd_buf, &out_param);
		if (err || out_param) {
			dev_err(hwdev->dev, "Failed to set SQ ctxts, err: %d, out_param: 0x%llx\n",
				err, out_param);
			err = -EFAULT;
			break;
		}

		q_id += max_ctxts;
	}

	hinic3_free_cmd_buf(hwdev, cmd_buf);

	return err;
}

static int init_rq_ctxts(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_rq_ctxt_block *rq_ctxt_block;
	u16 q_id, curr_id, max_ctxts, i;
	struct hinic3_rq_ctxt *rq_ctxt;
	struct hinic3_cmd_buf *cmd_buf;
	struct hinic3_io_queue *rq;
	__le64 out_param;
	int err = 0;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		dev_err(hwdev->dev, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	q_id = 0;
	while (q_id < nic_io->num_qps) {
		rq_ctxt_block = cmd_buf->buf;
		rq_ctxt = rq_ctxt_block->rq_ctxt;

		max_ctxts = (nic_io->num_qps - q_id) > HINIC3_Q_CTXT_MAX ?
				HINIC3_Q_CTXT_MAX : (nic_io->num_qps - q_id);

		hinic3_qp_prepare_cmdq_header(&rq_ctxt_block->cmdq_hdr,
					      HINIC3_QP_CTXT_TYPE_RQ, max_ctxts,
					      q_id);

		for (i = 0; i < max_ctxts; i++) {
			curr_id = q_id + i;
			rq = &nic_io->rq[curr_id];
			hinic3_rq_prepare_ctxt(rq, &rq_ctxt[i]);
		}

		hinic3_cmdq_buf_swab32(rq_ctxt_block, sizeof(*rq_ctxt_block));

		cmd_buf->size = cpu_to_le16(RQ_CTXT_SIZE(max_ctxts));

		err = hinic3_cmdq_direct_resp(hwdev, MGMT_MOD_L2NIC,
					      L2NIC_UCODE_CMD_MODIFY_QUEUE_CTX,
					      cmd_buf, &out_param);
		if (err || out_param) {
			dev_err(hwdev->dev, "Failed to set RQ ctxts, err: %d, out_param: 0x%llx\n",
				err, out_param);
			err = -EFAULT;
			break;
		}

		q_id += max_ctxts;
	}

	hinic3_free_cmd_buf(hwdev, cmd_buf);

	return err;
}

static int init_qp_ctxts(struct hinic3_nic_dev *nic_dev)
{
	int err;

	err = init_sq_ctxts(nic_dev);
	if (err)
		return err;

	err = init_rq_ctxts(nic_dev);
	if (err)
		return err;

	return 0;
}

static int clean_queue_offload_ctxt(struct hinic3_nic_dev *nic_dev,
				    enum hinic3_qp_ctxt_type ctxt_type)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_clean_queue_ctxt *ctxt_block;
	struct hinic3_cmd_buf *cmd_buf;
	__le64 out_param;
	int err;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		dev_err(hwdev->dev, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	ctxt_block = cmd_buf->buf;
	ctxt_block->cmdq_hdr.num_queues = cpu_to_le16(nic_io->max_qps);
	ctxt_block->cmdq_hdr.queue_type = cpu_to_le16(ctxt_type);
	ctxt_block->cmdq_hdr.start_qid = 0;
	ctxt_block->cmdq_hdr.rsvd = 0;
	ctxt_block->rsvd = 0;

	hinic3_cmdq_buf_swab32(ctxt_block, sizeof(*ctxt_block));

	cmd_buf->size = cpu_to_le16(sizeof(*ctxt_block));

	err = hinic3_cmdq_direct_resp(hwdev, MGMT_MOD_L2NIC,
				      L2NIC_UCODE_CMD_CLEAN_QUEUE_CTX,
				      cmd_buf, &out_param);
	if (err || out_param) {
		dev_err(hwdev->dev, "Failed to clean queue offload ctxts, err: %d,out_param: 0x%llx\n",
			err, out_param);

		err = -EFAULT;
	}

	hinic3_free_cmd_buf(hwdev, cmd_buf);

	return err;
}

static int clean_qp_offload_ctxt(struct hinic3_nic_dev *nic_dev)
{
	/* clean LRO/TSO context space */
	return clean_queue_offload_ctxt(nic_dev, HINIC3_QP_CTXT_TYPE_SQ) ||
	       clean_queue_offload_ctxt(nic_dev, HINIC3_QP_CTXT_TYPE_RQ);
}

/* init qps ctxt and set sq ci attr and arm all sq */
int hinic3_init_qp_ctxts(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_sq_attr sq_attr;
	u32 rq_depth;
	u16 q_id;
	int err;

	err = init_qp_ctxts(nic_dev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init QP ctxts\n");
		return err;
	}

	/* clean LRO/TSO context space */
	err = clean_qp_offload_ctxt(nic_dev);
	if (err) {
		dev_err(hwdev->dev, "Failed to clean qp offload ctxts\n");
		return err;
	}

	rq_depth = nic_io->rq[0].wq.q_depth << HINIC3_NORMAL_RQ_WQE;

	err = hinic3_set_root_ctxt(hwdev, rq_depth, nic_io->sq[0].wq.q_depth,
				   nic_io->rx_buf_len);
	if (err) {
		dev_err(hwdev->dev, "Failed to set root context\n");
		return err;
	}

	for (q_id = 0; q_id < nic_io->num_qps; q_id++) {
		sq_attr.ci_dma_base =
			HINIC3_CI_PADDR(nic_io->ci_dma_base, q_id) >> 0x2;
		sq_attr.pending_limit = HINIC3_DEFAULT_TX_CI_PENDING_LIMIT;
		sq_attr.coalescing_time = HINIC3_DEFAULT_TX_CI_COALESCING_TIME;
		sq_attr.intr_en = 1;
		sq_attr.intr_idx = nic_io->sq[q_id].msix_entry_idx;
		sq_attr.l2nic_sqn = q_id;
		sq_attr.dma_attr_off = 0;
		err = hinic3_set_ci_table(hwdev, &sq_attr);
		if (err) {
			dev_err(hwdev->dev, "Failed to set ci table\n");
			goto err_clean_root_ctxt;
		}
	}

	return 0;

err_clean_root_ctxt:
	hinic3_clean_root_ctxt(hwdev);

	return err;
}

void hinic3_free_qp_ctxts(struct hinic3_nic_dev *nic_dev)
{
	hinic3_clean_root_ctxt(nic_dev->hwdev);
}
