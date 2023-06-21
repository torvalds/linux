// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <trace/hooks/epoch.h>

static u64 suspend_ns;
static u64 suspend_cycles;
static u64 resume_cycles;

static void msm_show_suspend_epoch_val(void *data, u64 ns, u64 cycles)
{
	suspend_ns = ns;
	suspend_cycles = cycles;
	pr_info("suspend ns:%17llu      suspend cycles:%17llu\n",
						suspend_ns, suspend_cycles);
}

static void msm_show_resume_epoch_val(void *data, u64 cycles)
{
	resume_cycles = cycles;
	pr_info("resume cycles:%17llu\n", resume_cycles);
}

static int __init msm_show_epoch_init(void)
{
	register_trace_android_vh_show_suspend_epoch_val(
					msm_show_suspend_epoch_val, NULL);
	register_trace_android_vh_show_resume_epoch_val(
					msm_show_resume_epoch_val, NULL);

	return 0;
}

#if IS_MODULE(CONFIG_SHOW_SUSPEND_EPOCH)
module_init(msm_show_epoch_init);
#else
pure_initcall(msm_show_epoch_init);
#endif

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. show epoch values driver");
MODULE_LICENSE("GPL");
