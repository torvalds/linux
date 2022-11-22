// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <trace/hooks/cpuidle_psci.h>
#include <trace/hooks/bl_hib.h>
#include <linux/blkdev.h>
#include <linux/swap.h>
#include <soc/qcom/qcom_hibernation.h>

#define __NEW_UTS_LEN 64

struct block_device *hiber_bdev;
EXPORT_SYMBOL(hiber_bdev);

struct arch_hibernate_hdr_invariants {
	char uts_version[__NEW_UTS_LEN + 1];
};

struct arch_hibernate_hdr {
	struct arch_hibernate_hdr_invariants invariants;

	/* These are needed to find the relocated kernel if built with kaslr */
	phys_addr_t	ttbr1_el1;
	void		(*reenter_kernel)(void);

	/*
	 * We need to know where the __hyp_stub_vectors are after restore to
	 * re-configure el2.
	 */
	phys_addr_t	__hyp_stub_vectors;
	u64		sleep_cpu_mpidr;

	ANDROID_VENDOR_DATA(1);
};

static void save_hib_resume_bdev(void *data, struct block_device *hib_resume_bdev)
{
	hiber_bdev = hib_resume_bdev;
}

static void check_hibernation_swap(void *data, struct block_device *dev,
			bool *hib_swap)
{
	if (dev == hiber_bdev)
		*hib_swap = true;
	else
		*hib_swap = false;
}

static void save_cpu_resume(void *data, u64 *addr, u64 phys_addr)
{
	*addr = phys_addr;
}

static int __init init_s2d_hooks(void)
{
	register_trace_android_vh_save_hib_resume_bdev(save_hib_resume_bdev, NULL);
	register_trace_android_vh_check_hibernation_swap(check_hibernation_swap, NULL);
	register_trace_android_vh_save_cpu_resume(save_cpu_resume, NULL);
	return 0;
}

module_init(init_s2d_hooks);
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Bootloader Hibernation Vendor hooks");
MODULE_LICENSE("GPL");
