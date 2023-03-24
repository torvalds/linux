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

static void balance_reclaim(void *unused, bool *balance_anon_file_reclaim)
{
	*balance_anon_file_reclaim = true;
}

static int __init init_mem_hooks(void)
{
	int ret;

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
