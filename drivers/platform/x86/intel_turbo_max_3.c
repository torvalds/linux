/*
 * Intel Turbo Boost Max Technology 3.0 legacy (non HWP) enumeration driver
 * Copyright (c) 2017, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/topology.h>
#include <linux/workqueue.h>
#include <linux/cpuhotplug.h>
#include <linux/cpufeature.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#define MSR_OC_MAILBOX			0x150
#define MSR_OC_MAILBOX_CMD_OFFSET	32
#define MSR_OC_MAILBOX_RSP_OFFSET	32
#define MSR_OC_MAILBOX_BUSY_BIT		63
#define OC_MAILBOX_FC_CONTROL_CMD	0x1C

/*
 * Typical latency to get mail box response is ~3us, It takes +3 us to
 * process reading mailbox after issuing mailbox write on a Broadwell 3.4 GHz
 * system. So for most of the time, the first mailbox read should have the
 * response, but to avoid some boundary cases retry twice.
 */
#define OC_MAILBOX_RETRY_COUNT		2

static int get_oc_core_priority(unsigned int cpu)
{
	u64 value, cmd = OC_MAILBOX_FC_CONTROL_CMD;
	int ret, i;

	/* Issue favored core read command */
	value = cmd << MSR_OC_MAILBOX_CMD_OFFSET;
	/* Set the busy bit to indicate OS is trying to issue command */
	value |=  BIT_ULL(MSR_OC_MAILBOX_BUSY_BIT);
	ret = wrmsrl_safe(MSR_OC_MAILBOX, value);
	if (ret) {
		pr_debug("cpu %d OC mailbox write failed\n", cpu);
		return ret;
	}

	for (i = 0; i < OC_MAILBOX_RETRY_COUNT; ++i) {
		ret = rdmsrl_safe(MSR_OC_MAILBOX, &value);
		if (ret) {
			pr_debug("cpu %d OC mailbox read failed\n", cpu);
			break;
		}

		if (value & BIT_ULL(MSR_OC_MAILBOX_BUSY_BIT)) {
			pr_debug("cpu %d OC mailbox still processing\n", cpu);
			ret = -EBUSY;
			continue;
		}

		if ((value >> MSR_OC_MAILBOX_RSP_OFFSET) & 0xff) {
			pr_debug("cpu %d OC mailbox cmd failed\n", cpu);
			ret = -ENXIO;
			break;
		}

		ret = value & 0xff;
		pr_debug("cpu %d max_ratio %d\n", cpu, ret);
		break;
	}

	return ret;
}

/*
 * The work item is needed to avoid CPU hotplug locking issues. The function
 * itmt_legacy_set_priority() is called from CPU online callback, so can't
 * call sched_set_itmt_support() from there as this function will aquire
 * hotplug locks in its path.
 */
static void itmt_legacy_work_fn(struct work_struct *work)
{
	sched_set_itmt_support();
}

static DECLARE_WORK(sched_itmt_work, itmt_legacy_work_fn);

static int itmt_legacy_cpu_online(unsigned int cpu)
{
	static u32 max_highest_perf = 0, min_highest_perf = U32_MAX;
	int priority;

	priority = get_oc_core_priority(cpu);
	if (priority < 0)
		return 0;

	sched_set_itmt_core_prio(priority, cpu);

	/* Enable ITMT feature when a core with different priority is found */
	if (max_highest_perf <= min_highest_perf) {
		if (priority > max_highest_perf)
			max_highest_perf = priority;

		if (priority < min_highest_perf)
			min_highest_perf = priority;

		if (max_highest_perf > min_highest_perf)
			schedule_work(&sched_itmt_work);
	}

	return 0;
}

#define ICPU(model)     { X86_VENDOR_INTEL, 6, model, X86_FEATURE_ANY, }

static const struct x86_cpu_id itmt_legacy_cpu_ids[] = {
	ICPU(INTEL_FAM6_BROADWELL_X),
	{}
};

static int __init itmt_legacy_init(void)
{
	const struct x86_cpu_id *id;
	int ret;

	id = x86_match_cpu(itmt_legacy_cpu_ids);
	if (!id)
		return -ENODEV;

	if (boot_cpu_has(X86_FEATURE_HWP))
		return -ENODEV;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"platform/x86/turbo_max_3:online",
				itmt_legacy_cpu_online,	NULL);
	if (ret < 0)
		return ret;

	return 0;
}
late_initcall(itmt_legacy_init)
