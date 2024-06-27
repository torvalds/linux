// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/vmalloc.h>
#include "core.h"
#include "debug.h"
#include "debugfs_htt_stats.h"
#include "dp_tx.h"

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

static int ath12k_debugfs_htt_stats_req(struct ath12k *ar)
{
	struct debug_htt_stats_req *stats_req = ar->debug.htt_stats.stats_req;
	enum ath12k_dbg_htt_ext_stats_type type = stats_req->type;
	u64 cookie;
	int ret, pdev_id;
	struct htt_ext_stats_cfg_params cfg_params = { 0 };

	lockdep_assert_held(&ar->conf_mutex);

	init_completion(&stats_req->htt_stats_rcvd);

	pdev_id = ath12k_mac_get_target_pdev_id(ar);
	stats_req->done = false;
	stats_req->pdev_id = pdev_id;

	cookie = u64_encode_bits(ATH12K_HTT_STATS_MAGIC_VALUE,
				 ATH12K_HTT_STATS_COOKIE_MSB);
	cookie |= u64_encode_bits(pdev_id, ATH12K_HTT_STATS_COOKIE_LSB);

	if (stats_req->override_cfg_param) {
		cfg_params.cfg0 = stats_req->cfg_param[0];
		cfg_params.cfg1 = stats_req->cfg_param[1];
		cfg_params.cfg2 = stats_req->cfg_param[2];
		cfg_params.cfg3 = stats_req->cfg_param[3];
	}

	ret = ath12k_dp_tx_htt_h2t_ext_stats_req(ar, type, &cfg_params, cookie);
	if (ret) {
		ath12k_warn(ar->ab, "failed to send htt stats request: %d\n", ret);
		return ret;
	}
	if (!wait_for_completion_timeout(&stats_req->htt_stats_rcvd, 3 * HZ)) {
		spin_lock_bh(&ar->data_lock);
		if (!stats_req->done) {
			stats_req->done = true;
			spin_unlock_bh(&ar->data_lock);
			ath12k_warn(ar->ab, "stats request timed out\n");
			return -ETIMEDOUT;
		}
		spin_unlock_bh(&ar->data_lock);
	}

	return 0;
}

static int ath12k_open_htt_stats(struct inode *inode,
				 struct file *file)
{
	struct ath12k *ar = inode->i_private;
	struct debug_htt_stats_req *stats_req;
	enum ath12k_dbg_htt_ext_stats_type type = ar->debug.htt_stats.type;
	struct ath12k_hw *ah = ath12k_ar_to_ah(ar);
	int ret;

	if (type == ATH12K_DBG_HTT_EXT_STATS_RESET)
		return -EPERM;

	mutex_lock(&ar->conf_mutex);

	if (ah->state != ATH12K_HW_STATE_ON) {
		ret = -ENETDOWN;
		goto err_unlock;
	}

	if (ar->debug.htt_stats.stats_req) {
		ret = -EAGAIN;
		goto err_unlock;
	}

	stats_req = kzalloc(sizeof(*stats_req) + ATH12K_HTT_STATS_BUF_SIZE, GFP_KERNEL);
	if (!stats_req) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	ar->debug.htt_stats.stats_req = stats_req;
	stats_req->type = type;
	stats_req->cfg_param[0] = ar->debug.htt_stats.cfg_param[0];
	stats_req->cfg_param[1] = ar->debug.htt_stats.cfg_param[1];
	stats_req->cfg_param[2] = ar->debug.htt_stats.cfg_param[2];
	stats_req->cfg_param[3] = ar->debug.htt_stats.cfg_param[3];
	stats_req->override_cfg_param = !!stats_req->cfg_param[0] ||
					!!stats_req->cfg_param[1] ||
					!!stats_req->cfg_param[2] ||
					!!stats_req->cfg_param[3];

	ret = ath12k_debugfs_htt_stats_req(ar);
	if (ret < 0)
		goto out;

	file->private_data = stats_req;

	mutex_unlock(&ar->conf_mutex);

	return 0;
out:
	kfree(stats_req);
	ar->debug.htt_stats.stats_req = NULL;
err_unlock:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static int ath12k_release_htt_stats(struct inode *inode,
				    struct file *file)
{
	struct ath12k *ar = inode->i_private;

	mutex_lock(&ar->conf_mutex);
	kfree(file->private_data);
	ar->debug.htt_stats.stats_req = NULL;
	mutex_unlock(&ar->conf_mutex);

	return 0;
}

static ssize_t ath12k_read_htt_stats(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct debug_htt_stats_req *stats_req = file->private_data;
	char *buf;
	u32 length;

	buf = stats_req->buf;
	length = min_t(u32, stats_req->buf_len, ATH12K_HTT_STATS_BUF_SIZE);
	return simple_read_from_buffer(user_buf, count, ppos, buf, length);
}

static const struct file_operations fops_dump_htt_stats = {
	.open = ath12k_open_htt_stats,
	.release = ath12k_release_htt_stats,
	.read = ath12k_read_htt_stats,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath12k_write_htt_stats_reset(struct file *file,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct ath12k *ar = file->private_data;
	enum ath12k_dbg_htt_ext_stats_type type;
	struct htt_ext_stats_cfg_params cfg_params = { 0 };
	u8 param_pos;
	int ret;

	ret = kstrtou32_from_user(user_buf, count, 0, &type);
	if (ret)
		return ret;

	if (type >= ATH12K_DBG_HTT_NUM_EXT_STATS ||
	    type == ATH12K_DBG_HTT_EXT_STATS_RESET)
		return -E2BIG;

	mutex_lock(&ar->conf_mutex);
	cfg_params.cfg0 = HTT_STAT_DEFAULT_RESET_START_OFFSET;
	param_pos = (type >> 5) + 1;

	switch (param_pos) {
	case ATH12K_HTT_STATS_RESET_PARAM_CFG_32_BYTES:
		cfg_params.cfg1 = 1 << (cfg_params.cfg0 + type);
		break;
	case ATH12K_HTT_STATS_RESET_PARAM_CFG_64_BYTES:
		cfg_params.cfg2 = ATH12K_HTT_STATS_RESET_BITMAP32_BIT(cfg_params.cfg0 +
								      type);
		break;
	case ATH12K_HTT_STATS_RESET_PARAM_CFG_128_BYTES:
		cfg_params.cfg3 = ATH12K_HTT_STATS_RESET_BITMAP64_BIT(cfg_params.cfg0 +
								      type);
		break;
	default:
		break;
	}

	ret = ath12k_dp_tx_htt_h2t_ext_stats_req(ar,
						 ATH12K_DBG_HTT_EXT_STATS_RESET,
						 &cfg_params,
						 0ULL);
	if (ret) {
		ath12k_warn(ar->ab, "failed to send htt stats request: %d\n", ret);
		mutex_unlock(&ar->conf_mutex);
		return ret;
	}

	ar->debug.htt_stats.reset = type;
	mutex_unlock(&ar->conf_mutex);

	return count;
}

static const struct file_operations fops_htt_stats_reset = {
	.write = ath12k_write_htt_stats_reset,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath12k_debugfs_htt_stats_register(struct ath12k *ar)
{
	debugfs_create_file("htt_stats_type", 0600, ar->debug.debugfs_pdev,
			    ar, &fops_htt_stats_type);
	debugfs_create_file("htt_stats", 0400, ar->debug.debugfs_pdev,
			    ar, &fops_dump_htt_stats);
	debugfs_create_file("htt_stats_reset", 0200, ar->debug.debugfs_pdev,
			    ar, &fops_htt_stats_reset);
}
