// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/vmalloc.h>
#include "core.h"
#include "debug.h"
#include "debugfs_htt_stats.h"

static ssize_t ath12k_read_htt_stats_type(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ath12k *ar = file->private_data;
	enum ath12k_dbg_htt_ext_stats_type type;
	char buf[32];
	size_t len;

	mutex_lock(&ar->conf_mutex);
	type = ar->debug.htt_stats.type;
	mutex_unlock(&ar->conf_mutex);

	len = scnprintf(buf, sizeof(buf), "%u\n", type);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath12k_write_htt_stats_type(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath12k *ar = file->private_data;
	enum ath12k_dbg_htt_ext_stats_type type;
	unsigned int cfg_param[4] = {0};
	const int size = 32;
	int num_args;

	char *buf __free(kfree) = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	num_args = sscanf(buf, "%u %u %u %u %u\n", &type, &cfg_param[0],
			  &cfg_param[1], &cfg_param[2], &cfg_param[3]);
	if (!num_args || num_args > 5)
		return -EINVAL;

	if (type == ATH12K_DBG_HTT_EXT_STATS_RESET ||
	    type >= ATH12K_DBG_HTT_NUM_EXT_STATS)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	ar->debug.htt_stats.type = type;
	ar->debug.htt_stats.cfg_param[0] = cfg_param[0];
	ar->debug.htt_stats.cfg_param[1] = cfg_param[1];
	ar->debug.htt_stats.cfg_param[2] = cfg_param[2];
	ar->debug.htt_stats.cfg_param[3] = cfg_param[3];

	mutex_unlock(&ar->conf_mutex);

	return count;
}

static const struct file_operations fops_htt_stats_type = {
	.read = ath12k_read_htt_stats_type,
	.write = ath12k_write_htt_stats_type,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath12k_debugfs_htt_stats_register(struct ath12k *ar)
{
	debugfs_create_file("htt_stats_type", 0600, ar->debug.debugfs_pdev,
			    ar, &fops_htt_stats_type);
}
