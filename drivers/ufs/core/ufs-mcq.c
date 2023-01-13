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

#define MAX_DEV_CMD_ENTRIES	2
#define MCQ_CFG_MAC_MASK	GENMASK(16, 8)

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
 * ufshcd_mcq_decide_queue_depth - decide the queue depth
 * @hba - per adapter instance
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


int ufshcd_mcq_init(struct ufs_hba *hba)
{
	struct ufs_hw_queue *hwq;
	int ret, i;

	ret = ufshcd_mcq_config_nr_queues(hba);
	if (ret)
		return ret;

	ret = ufshcd_vops_mcq_config_resource(hba);
	if (ret)
		return ret;

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
	}

	/* The very first HW queue serves device commands */
	hba->dev_cmd_queue = &hba->uhq[0];
	/* Give dev_cmd_queue the minimal number of entries */
	hba->dev_cmd_queue->max_entries = MAX_DEV_CMD_ENTRIES;

	return 0;
}
