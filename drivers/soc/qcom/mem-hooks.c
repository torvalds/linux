// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/oom.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/vmscan.h>
#include <linux/printk.h>
#include <linux/nodemask.h>
#include <linux/kthread.h>
#include <linux/swap.h>

static uint kswapd_threads;
module_param_named(kswapd_threads, kswapd_threads, uint, 0644);

static void balance_reclaim(void *unused, bool *balance_anon_file_reclaim)
{
	*balance_anon_file_reclaim = true;
}

static int kswapd_per_node_run(int nid, unsigned int kswapd_threads)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	unsigned int hid, start = 0;
	int ret = 0;

	if (pgdat->kswapd) {
		start = 1;
		pgdat->mkswapd[0] = pgdat->kswapd;
	}

	for (hid = start; hid < kswapd_threads; ++hid) {
		pgdat->mkswapd[hid] = kthread_run(kswapd, pgdat, "kswapd%d:%d",
								nid, hid);
		if (IS_ERR(pgdat->mkswapd[hid])) {
			/* failure at boot is fatal */
			WARN_ON(system_state < SYSTEM_RUNNING);
			pr_err("Failed to start kswapd%d on node %d\n",
				hid, nid);
			ret = PTR_ERR(pgdat->mkswapd[hid]);
			pgdat->mkswapd[hid] = NULL;
			continue;
		}
		if (!pgdat->kswapd)
			pgdat->kswapd = pgdat->mkswapd[hid];
	}
	return ret;
}

static void kswapd_per_node_stop(int nid, unsigned int kswapd_threads)
{
	int hid = 0;
	struct task_struct *kswapd;

	for (hid = 0; hid < kswapd_threads; hid++) {
		kswapd = NODE_DATA(nid)->mkswapd[hid];
		if (kswapd) {
			kthread_stop(kswapd);
			NODE_DATA(nid)->mkswapd[hid] = NULL;
		}
	}
	NODE_DATA(nid)->kswapd = NULL;
}

static void kswapd_threads_set(void *unused, int nid, bool *skip, bool run)
{
	*skip = true;
	if (run)
		kswapd_per_node_run(nid, kswapd_threads);
	else
		kswapd_per_node_stop(nid, kswapd_threads);

}

static int init_kswapd_per_node_hook(void)
{
	int ret = 0;
	int nid;

	if (kswapd_threads > MAX_KSWAPD_THREADS) {
		pr_err("Failed to set kswapd_threads to %d ,Max limit is %d\n",
				kswapd_threads, MAX_KSWAPD_THREADS);
		return ret;
	} else if (kswapd_threads > 1) {
		ret = register_trace_android_vh_kswapd_per_node(kswapd_threads_set, NULL);
		if (ret) {
			pr_err("Failed to register kswapd_per_node hooks\n");
			return ret;
		}
		for_each_node_state(nid, N_MEMORY)
			kswapd_per_node_run(nid, kswapd_threads);
	}
	return ret;
}

static int __init init_mem_hooks(void)
{
	int ret;

	ret = init_kswapd_per_node_hook();
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_QCOM_BALANCE_ANON_FILE_RECLAIM)) {
		ret = register_trace_android_rvh_set_balance_anon_file_reclaim(
							balance_reclaim,
							NULL);
		if (ret) {
			pr_err("Failed to register balance_anon_file_reclaim hooks\n");
			return ret;
		}
	}

	return 0;
}

module_init(init_mem_hooks);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Memory Trace Hook Call-Back Registration");
MODULE_LICENSE("GPL");
