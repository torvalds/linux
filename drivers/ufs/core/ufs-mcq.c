// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center. All rights reserved.
 *
 * Authors:
 *	Asutosh Das <quic_asutoshd@quicinc.com>
 *	Can Guo <quic_cang@quicinc.com>
 */

#include <asm/unaligned.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "ufshcd-priv.h"

#define MAX_QUEUE_SUP GENMASK(7, 0)
#define UFS_MCQ_MIN_RW_QUEUES 2
#define UFS_MCQ_MIN_READ_QUEUES 0
#define UFS_MCQ_NUM_DEV_CMD_QUEUES 1
#define UFS_MCQ_MIN_POLL_QUEUES 0
#define QUEUE_EN_OFFSET 31
#define QUEUE_ID_OFFSET 16

#define MAX_DEV_CMD_ENTRIES	2
#define MCQ_CFG_MAC_MASK	GENMASK(16, 8)
#define MCQ_QCFG_SIZE		0x40
#define MCQ_ENTRY_SIZE_IN_DWORD	8
#define CQE_UCD_BA GENMASK_ULL(63, 7)

static int rw_queue_count_set(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, UFS_MCQ_MIN_RW_QUEUES,
				     num_possible_cpus());
}

static const struct kernel_param_ops rw_queue_count_ops = {
	.set = rw_queue_count_set,
	.get = param_get_uint,
};

static unsigned int rw_queues;
module_param_cb(rw_queues, &rw_queue_count_ops, &rw_queues, 0644);
MODULE_PARM_DESC(rw_queues,
		 "Number of interrupt driven I/O queues used for rw. Default value is nr_cpus");

static int read_queue_count_set(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, UFS_MCQ_MIN_READ_QUEUES,
				     num_possible_cpus());
}

static const struct kernel_param_ops read_queue_count_ops = {
	.set = read_queue_count_set,
	.get = param_get_uint,
};

static unsigned int read_queues;
module_param_cb(read_queues, &read_queue_count_ops, &read_queues, 0644);
MODULE_PARM_DESC(read_queues,
		 "Number of interrupt driven read queues used for read. Default value is 0");

static int poll_queue_count_set(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, UFS_MCQ_MIN_POLL_QUEUES,
				     num_possible_cpus());
}

static const struct kernel_param_ops poll_queue_count_ops = {
	.set = poll_queue_count_set,
	.get = param_get_uint,
};

static unsigned int poll_queues = 1;
module_param_cb(poll_queues, &poll_queue_count_ops, &poll_queues, 0644);
MODULE_PARM_DESC(poll_queues,
		 "Number of poll queues used for r/w. Default value is 1");

/**
 * ufshcd_mcq_config_mac - Set the #Max Activ Cmds.
 * @hba: per adapter instance
 * @max_active_cmds: maximum # of active commands to the device at any time.
 *
 * The controller won't send more than the max_active_cmds to the device at
 * any time.
 */
void ufshcd_mcq_config_mac(struct ufs_hba *hba, u32 max_active_cmds)
{
	u32 val;

	val = ufshcd_readl(hba, REG_UFS_MCQ_CFG);
	val &= ~MCQ_CFG_MAC_MASK;
	val |= FIELD_PREP(MCQ_CFG_MAC_MASK, max_active_cmds);
	ufshcd_writel(hba, val, REG_UFS_MCQ_CFG);
}

/**
 * ufshcd_mcq_req_to_hwq - find the hardware queue on which the
 * request would be issued.
 * @hba: per adapter instance
 * @req: pointer to the request to be issued
 *
 * Returns the hardware queue instance on which the request would
 * be queued.
 */
struct ufs_hw_queue *ufshcd_mcq_req_to_hwq(struct ufs_hba *hba,
					 struct request *req)
{
	u32 utag = blk_mq_unique_tag(req);
	u32 hwq = blk_mq_unique_tag_to_hwq(utag);

	/* uhq[0] is used to serve device commands */
	return &hba->uhq[hwq + UFSHCD_MCQ_IO_QUEUE_OFFSET];
}

/**
 * ufshcd_mcq_decide_queue_depth - decide the queue depth
 * @hba: per adapter instance
 *
 * Returns queue-depth on success, non-zero on error
 *
 * MAC - Max. Active Command of the Host Controller (HC)
 * HC wouldn't send more than this commands to the device.
 * It is mandatory to implement get_hba_mac() to enable MCQ mode.
 * Calculates and adjusts the queue depth based on the depth
 * supported by the HC and ufs device.
 */
int ufshcd_mcq_decide_queue_depth(struct ufs_hba *hba)
{
	int mac;

	/* Mandatory to implement get_hba_mac() */
	mac = ufshcd_mcq_vops_get_hba_mac(hba);
	if (mac < 0) {
		dev_err(hba->dev, "Failed to get mac, err=%d\n", mac);
		return mac;
	}

	WARN_ON_ONCE(!hba->dev_info.bqueuedepth);
	/*
	 * max. value of bqueuedepth = 256, mac is host dependent.
	 * It is mandatory for UFS device to define bQueueDepth if
	 * shared queuing architecture is enabled.
	 */
	return min_t(int, mac, hba->dev_info.bqueuedepth);
}

static int ufshcd_mcq_config_nr_queues(struct ufs_hba *hba)
{
	int i;
	u32 hba_maxq, rem, tot_queues;
	struct Scsi_Host *host = hba->host;

	hba_maxq = FIELD_GET(MAX_QUEUE_SUP, hba->mcq_capabilities);

	tot_queues = UFS_MCQ_NUM_DEV_CMD_QUEUES + read_queues + poll_queues +
			rw_queues;

	if (hba_maxq < tot_queues) {
		dev_err(hba->dev, "Total queues (%d) exceeds HC capacity (%d)\n",
			tot_queues, hba_maxq);
		return -EOPNOTSUPP;
	}

	rem = hba_maxq - UFS_MCQ_NUM_DEV_CMD_QUEUES;

	if (rw_queues) {
		hba->nr_queues[HCTX_TYPE_DEFAULT] = rw_queues;
		rem -= hba->nr_queues[HCTX_TYPE_DEFAULT];
	} else {
		rw_queues = num_possible_cpus();
	}

	if (poll_queues) {
		hba->nr_queues[HCTX_TYPE_POLL] = poll_queues;
		rem -= hba->nr_queues[HCTX_TYPE_POLL];
	}

	if (read_queues) {
		hba->nr_queues[HCTX_TYPE_READ] = read_queues;
		rem -= hba->nr_queues[HCTX_TYPE_READ];
	}

	if (!hba->nr_queues[HCTX_TYPE_DEFAULT])
		hba->nr_queues[HCTX_TYPE_DEFAULT] = min3(rem, rw_queues,
							 num_possible_cpus());

	for (i = 0; i < HCTX_MAX_TYPES; i++)
		host->nr_hw_queues += hba->nr_queues[i];

	hba->nr_hw_queues = host->nr_hw_queues + UFS_MCQ_NUM_DEV_CMD_QUEUES;
	return 0;
}

int ufshcd_mcq_memory_alloc(struct ufs_hba *hba)
{
	struct ufs_hw_queue *hwq;
	size_t utrdl_size, cqe_size;
	int i;

	for (i = 0; i < hba->nr_hw_queues; i++) {
		hwq = &hba->uhq[i];

		utrdl_size = sizeof(struct utp_transfer_req_desc) *
			     hwq->max_entries;
		hwq->sqe_base_addr = dmam_alloc_coherent(hba->dev, utrdl_size,
							 &hwq->sqe_dma_addr,
							 GFP_KERNEL);
		if (!hwq->sqe_dma_addr) {
			dev_err(hba->dev, "SQE allocation failed\n");
			return -ENOMEM;
		}

		cqe_size = sizeof(struct cq_entry) * hwq->max_entries;
		hwq->cqe_base_addr = dmam_alloc_coherent(hba->dev, cqe_size,
							 &hwq->cqe_dma_addr,
							 GFP_KERNEL);
		if (!hwq->cqe_dma_addr) {
			dev_err(hba->dev, "CQE allocation failed\n");
			return -ENOMEM;
		}
	}

	return 0;
}


/* Operation and runtime registers configuration */
#define MCQ_CFG_n(r, i)	((r) + MCQ_QCFG_SIZE * (i))
#define MCQ_OPR_OFFSET_n(p, i) \
	(hba->mcq_opr[(p)].offset + hba->mcq_opr[(p)].stride * (i))

static void __iomem *mcq_opr_base(struct ufs_hba *hba,
					 enum ufshcd_mcq_opr n, int i)
{
	struct ufshcd_mcq_opr_info_t *opr = &hba->mcq_opr[n];

	return opr->base + opr->stride * i;
}

u32 ufshcd_mcq_read_cqis(struct ufs_hba *hba, int i)
{
	return readl(mcq_opr_base(hba, OPR_CQIS, i) + REG_CQIS);
}

void ufshcd_mcq_write_cqis(struct ufs_hba *hba, u32 val, int i)
{
	writel(val, mcq_opr_base(hba, OPR_CQIS, i) + REG_CQIS);
}
EXPORT_SYMBOL_GPL(ufshcd_mcq_write_cqis);

/*
 * Current MCQ specification doesn't provide a Task Tag or its equivalent in
 * the Completion Queue Entry. Find the Task Tag using an indirect method.
 */
static int ufshcd_mcq_get_tag(struct ufs_hba *hba,
				     struct ufs_hw_queue *hwq,
				     struct cq_entry *cqe)
{
	u64 addr;

	/* sizeof(struct utp_transfer_cmd_desc) must be a multiple of 128 */
	BUILD_BUG_ON(sizeof(struct utp_transfer_cmd_desc) & GENMASK(6, 0));

	/* Bits 63:7 UCD base address, 6:5 are reserved, 4:0 is SQ ID */
	addr = (le64_to_cpu(cqe->command_desc_base_addr) & CQE_UCD_BA) -
		hba->ucdl_dma_addr;

	return div_u64(addr, sizeof(struct utp_transfer_cmd_desc));
}

static void ufshcd_mcq_process_cqe(struct ufs_hba *hba,
					    struct ufs_hw_queue *hwq)
{
	struct cq_entry *cqe = ufshcd_mcq_cur_cqe(hwq);
	int tag = ufshcd_mcq_get_tag(hba, hwq, cqe);

	ufshcd_compl_one_cqe(hba, tag, cqe);
}

unsigned long ufshcd_mcq_poll_cqe_nolock(struct ufs_hba *hba,
					 struct ufs_hw_queue *hwq)
{
	unsigned long completed_reqs = 0;

	ufshcd_mcq_update_cq_tail_slot(hwq);
	while (!ufshcd_mcq_is_cq_empty(hwq)) {
		ufshcd_mcq_process_cqe(hba, hwq);
		ufshcd_mcq_inc_cq_head_slot(hwq);
		completed_reqs++;
	}

	if (completed_reqs)
		ufshcd_mcq_update_cq_head(hwq);

	return completed_reqs;
}
EXPORT_SYMBOL_GPL(ufshcd_mcq_poll_cqe_nolock);

unsigned long ufshcd_mcq_poll_cqe_lock(struct ufs_hba *hba,
				       struct ufs_hw_queue *hwq)
{
	unsigned long completed_reqs, flags;

	spin_lock_irqsave(&hwq->cq_lock, flags);
	completed_reqs = ufshcd_mcq_poll_cqe_nolock(hba, hwq);
	spin_unlock_irqrestore(&hwq->cq_lock, flags);

	return completed_reqs;
}

void ufshcd_mcq_make_queues_operational(struct ufs_hba *hba)
{
	struct ufs_hw_queue *hwq;
	u16 qsize;
	int i;

	for (i = 0; i < hba->nr_hw_queues; i++) {
		hwq = &hba->uhq[i];
		hwq->id = i;
		qsize = hwq->max_entries * MCQ_ENTRY_SIZE_IN_DWORD - 1;

		/* Submission Queue Lower Base Address */
		ufsmcq_writelx(hba, lower_32_bits(hwq->sqe_dma_addr),
			      MCQ_CFG_n(REG_SQLBA, i));
		/* Submission Queue Upper Base Address */
		ufsmcq_writelx(hba, upper_32_bits(hwq->sqe_dma_addr),
			      MCQ_CFG_n(REG_SQUBA, i));
		/* Submission Queue Doorbell Address Offset */
		ufsmcq_writelx(hba, MCQ_OPR_OFFSET_n(OPR_SQD, i),
			      MCQ_CFG_n(REG_SQDAO, i));
		/* Submission Queue Interrupt Status Address Offset */
		ufsmcq_writelx(hba, MCQ_OPR_OFFSET_n(OPR_SQIS, i),
			      MCQ_CFG_n(REG_SQISAO, i));

		/* Completion Queue Lower Base Address */
		ufsmcq_writelx(hba, lower_32_bits(hwq->cqe_dma_addr),
			      MCQ_CFG_n(REG_CQLBA, i));
		/* Completion Queue Upper Base Address */
		ufsmcq_writelx(hba, upper_32_bits(hwq->cqe_dma_addr),
			      MCQ_CFG_n(REG_CQUBA, i));
		/* Completion Queue Doorbell Address Offset */
		ufsmcq_writelx(hba, MCQ_OPR_OFFSET_n(OPR_CQD, i),
			      MCQ_CFG_n(REG_CQDAO, i));
		/* Completion Queue Interrupt Status Address Offset */
		ufsmcq_writelx(hba, MCQ_OPR_OFFSET_n(OPR_CQIS, i),
			      MCQ_CFG_n(REG_CQISAO, i));

		/* Save the base addresses for quicker access */
		hwq->mcq_sq_head = mcq_opr_base(hba, OPR_SQD, i) + REG_SQHP;
		hwq->mcq_sq_tail = mcq_opr_base(hba, OPR_SQD, i) + REG_SQTP;
		hwq->mcq_cq_head = mcq_opr_base(hba, OPR_CQD, i) + REG_CQHP;
		hwq->mcq_cq_tail = mcq_opr_base(hba, OPR_CQD, i) + REG_CQTP;

		/* Reinitializing is needed upon HC reset */
		hwq->sq_tail_slot = hwq->cq_tail_slot = hwq->cq_head_slot = 0;

		/* Enable Tail Entry Push Status interrupt only for non-poll queues */
		if (i < hba->nr_hw_queues - hba->nr_queues[HCTX_TYPE_POLL])
			writel(1, mcq_opr_base(hba, OPR_CQIS, i) + REG_CQIE);

		/* Completion Queue Enable|Size to Completion Queue Attribute */
		ufsmcq_writel(hba, (1 << QUEUE_EN_OFFSET) | qsize,
			      MCQ_CFG_n(REG_CQATTR, i));

		/*
		 * Submission Qeueue Enable|Size|Completion Queue ID to
		 * Submission Queue Attribute
		 */
		ufsmcq_writel(hba, (1 << QUEUE_EN_OFFSET) | qsize |
			      (i << QUEUE_ID_OFFSET),
			      MCQ_CFG_n(REG_SQATTR, i));
	}
}

void ufshcd_mcq_enable_esi(struct ufs_hba *hba)
{
	ufshcd_writel(hba, ufshcd_readl(hba, REG_UFS_MEM_CFG) | 0x2,
		      REG_UFS_MEM_CFG);
}
EXPORT_SYMBOL_GPL(ufshcd_mcq_enable_esi);

void ufshcd_mcq_config_esi(struct ufs_hba *hba, struct msi_msg *msg)
{
	ufshcd_writel(hba, msg->address_lo, REG_UFS_ESILBA);
	ufshcd_writel(hba, msg->address_hi, REG_UFS_ESIUBA);
}
EXPORT_SYMBOL_GPL(ufshcd_mcq_config_esi);

int ufshcd_mcq_init(struct ufs_hba *hba)
{
	struct Scsi_Host *host = hba->host;
	struct ufs_hw_queue *hwq;
	int ret, i;

	ret = ufshcd_mcq_config_nr_queues(hba);
	if (ret)
		return ret;

	ret = ufshcd_vops_mcq_config_resource(hba);
	if (ret)
		return ret;

	ret = ufshcd_mcq_vops_op_runtime_config(hba);
	if (ret) {
		dev_err(hba->dev, "Operation runtime config failed, ret=%d\n",
			ret);
		return ret;
	}
	hba->uhq = devm_kzalloc(hba->dev,
				hba->nr_hw_queues * sizeof(struct ufs_hw_queue),
				GFP_KERNEL);
	if (!hba->uhq) {
		dev_err(hba->dev, "ufs hw queue memory allocation failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < hba->nr_hw_queues; i++) {
		hwq = &hba->uhq[i];
		hwq->max_entries = hba->nutrs;
		spin_lock_init(&hwq->sq_lock);
		spin_lock_init(&hwq->cq_lock);
	}

	/* The very first HW queue serves device commands */
	hba->dev_cmd_queue = &hba->uhq[0];
	/* Give dev_cmd_queue the minimal number of entries */
	hba->dev_cmd_queue->max_entries = MAX_DEV_CMD_ENTRIES;

	host->host_tagset = 1;
	return 0;
}
