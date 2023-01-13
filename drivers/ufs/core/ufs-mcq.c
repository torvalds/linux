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

int ufshcd_mcq_init(struct ufs_hba *hba)
{
	int ret;

	ret = ufshcd_mcq_config_nr_queues(hba);

	return ret;
}
