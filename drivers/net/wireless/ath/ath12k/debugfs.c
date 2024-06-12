// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "core.h"
#include "debugfs.h"

static ssize_t ath12k_write_simulate_radar(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath12k *ar = file->private_data;
	int ret;

	mutex_lock(&ar->conf_mutex);
	ret = ath12k_wmi_simulate_radar(ar);
	if (ret)
		goto exit;

	ret = count;
exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_simulate_radar = {
	.write = ath12k_write_simulate_radar,
	.open = simple_open
};

void ath12k_debugfs_soc_create(struct ath12k_base *ab)
{
	bool dput_needed;
	char soc_name[64] = { 0 };
	struct dentry *debugfs_ath12k;

	debugfs_ath12k = debugfs_lookup("ath12k", NULL);
	if (debugfs_ath12k) {
		/* a dentry from lookup() needs dput() after we don't use it */
		dput_needed = true;
	} else {
		debugfs_ath12k = debugfs_create_dir("ath12k", NULL);
		if (IS_ERR_OR_NULL(debugfs_ath12k))
			return;
		dput_needed = false;
	}

	scnprintf(soc_name, sizeof(soc_name), "%s-%s", ath12k_bus_str(ab->hif.bus),
		  dev_name(ab->dev));

	ab->debugfs_soc = debugfs_create_dir(soc_name, debugfs_ath12k);

	if (dput_needed)
		dput(debugfs_ath12k);
}

void ath12k_debugfs_soc_destroy(struct ath12k_base *ab)
{
	debugfs_remove_recursive(ab->debugfs_soc);
	ab->debugfs_soc = NULL;
	/* We are not removing ath12k directory on purpose, even if it
	 * would be empty. This simplifies the directory handling and it's
	 * a minor cosmetic issue to leave an empty ath12k directory to
	 * debugfs.
	 */
}

void ath12k_debugfs_register(struct ath12k *ar)
{
	struct ath12k_base *ab = ar->ab;
	struct ieee80211_hw *hw = ar->ah->hw;
	char pdev_name[5];
	char buf[100] = {0};

	scnprintf(pdev_name, sizeof(pdev_name), "%s%d", "mac", ar->pdev_idx);

	ar->debug.debugfs_pdev = debugfs_create_dir(pdev_name, ab->debugfs_soc);

	/* Create a symlink under ieee80211/phy* */
	scnprintf(buf, sizeof(buf), "../../ath12k/%pd2", ar->debug.debugfs_pdev);
	debugfs_create_symlink("ath12k", hw->wiphy->debugfsdir, buf);

	if (ar->mac.sbands[NL80211_BAND_5GHZ].channels) {
		debugfs_create_file("dfs_simulate_radar", 0200,
				    ar->debug.debugfs_pdev, ar,
				    &fops_simulate_radar);
	}
}
