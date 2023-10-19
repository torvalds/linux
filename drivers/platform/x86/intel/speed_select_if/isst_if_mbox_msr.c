// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select Interface: Mbox via MSR Interface
 * Copyright (c) 2019, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/cpuhotplug.h>
#include <linux/pci.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/topology.h>
#include <linux/uaccess.h>
#include <uapi/linux/isst_if.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#include "isst_if_common.h"

#define MSR_OS_MAILBOX_INTERFACE	0xB0
#define MSR_OS_MAILBOX_DATA		0xB1
#define MSR_OS_MAILBOX_BUSY_BIT		31

/*
 * Based on experiments count is never more than 1, as the MSR overhead
 * is enough to finish the command. So here this is the worst case number.
 */
#define OS_MAILBOX_RETRY_COUNT		3

static int isst_if_send_mbox_cmd(u8 command, u8 sub_command, u32 parameter,
				 u32 command_data, u32 *response_data)
{
	u32 retries;
	u64 data;
	int ret;

	/* Poll for rb bit == 0 */
	retries = OS_MAILBOX_RETRY_COUNT;
	do {
		rdmsrl(MSR_OS_MAILBOX_INTERFACE, data);
		if (data & BIT_ULL(MSR_OS_MAILBOX_BUSY_BIT)) {
			ret = -EBUSY;
			continue;
		}
		ret = 0;
		break;
	} while (--retries);

	if (ret)
		return ret;

	/* Write DATA register */
	wrmsrl(MSR_OS_MAILBOX_DATA, command_data);

	/* Write command register */
	data = BIT_ULL(MSR_OS_MAILBOX_BUSY_BIT) |
		      (parameter & GENMASK_ULL(13, 0)) << 16 |
		      (sub_command << 8) |
		      command;
	wrmsrl(MSR_OS_MAILBOX_INTERFACE, data);

	/* Poll for rb bit == 0 */
	retries = OS_MAILBOX_RETRY_COUNT;
	do {
		rdmsrl(MSR_OS_MAILBOX_INTERFACE, data);
		if (data & BIT_ULL(MSR_OS_MAILBOX_BUSY_BIT)) {
			ret = -EBUSY;
			continue;
		}

		if (data & 0xff)
			return -ENXIO;

		if (response_data) {
			rdmsrl(MSR_OS_MAILBOX_DATA, data);
			*response_data = data;
		}
		ret = 0;
		break;
	} while (--retries);

	return ret;
}

struct msrl_action {
	int err;
	struct isst_if_mbox_cmd *mbox_cmd;
};

/* revisit, smp_call_function_single should be enough for atomic mailbox! */
static void msrl_update_func(void *info)
{
	struct msrl_action *act = info;

	act->err = isst_if_send_mbox_cmd(act->mbox_cmd->command,
					 act->mbox_cmd->sub_command,
					 act->mbox_cmd->parameter,
					 act->mbox_cmd->req_data,
					 &act->mbox_cmd->resp_data);
}

static long isst_if_mbox_proc_cmd(u8 *cmd_ptr, int *write_only, int resume)
{
	struct msrl_action action;
	int ret;

	action.mbox_cmd = (struct isst_if_mbox_cmd *)cmd_ptr;

	if (isst_if_mbox_cmd_invalid(action.mbox_cmd))
		return -EINVAL;

	if (isst_if_mbox_cmd_set_req(action.mbox_cmd) &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * To complete mailbox command, we need to access two MSRs.
	 * So we don't want race to complete a mailbox transcation.
	 * Here smp_call ensures that msrl_update_func() has no race
	 * and also with wait flag, wait for completion.
	 * smp_call_function_single is using get_cpu() and put_cpu().
	 */
	ret = smp_call_function_single(action.mbox_cmd->logical_cpu,
				       msrl_update_func, &action, 1);
	if (ret)
		return ret;

	if (!action.err && !resume && isst_if_mbox_cmd_set_req(action.mbox_cmd))
		action.err = isst_store_cmd(action.mbox_cmd->command,
					    action.mbox_cmd->sub_command,
					    action.mbox_cmd->logical_cpu, 1,
					    action.mbox_cmd->parameter,
					    action.mbox_cmd->req_data);
	*write_only = 0;

	return action.err;
}


static int isst_pm_notify(struct notifier_block *nb,
			       unsigned long mode, void *_unused)
{
	switch (mode) {
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		isst_resume_common();
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block isst_pm_nb = {
	.notifier_call = isst_pm_notify,
};

static const struct x86_cpu_id isst_if_cpu_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_X, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, isst_if_cpu_ids);

static int __init isst_if_mbox_init(void)
{
	struct isst_if_cmd_cb cb;
	const struct x86_cpu_id *id;
	u64 data;
	int ret;

	id = x86_match_cpu(isst_if_cpu_ids);
	if (!id)
		return -ENODEV;

	/* Check presence of mailbox MSRs */
	ret = rdmsrl_safe(MSR_OS_MAILBOX_INTERFACE, &data);
	if (ret)
		return ret;

	ret = rdmsrl_safe(MSR_OS_MAILBOX_DATA, &data);
	if (ret)
		return ret;

	memset(&cb, 0, sizeof(cb));
	cb.cmd_size = sizeof(struct isst_if_mbox_cmd);
	cb.offset = offsetof(struct isst_if_mbox_cmds, mbox_cmd);
	cb.cmd_callback = isst_if_mbox_proc_cmd;
	cb.owner = THIS_MODULE;
	ret = isst_if_cdev_register(ISST_IF_DEV_MBOX, &cb);
	if (ret)
		return ret;

	ret = register_pm_notifier(&isst_pm_nb);
	if (ret)
		isst_if_cdev_unregister(ISST_IF_DEV_MBOX);

	return ret;
}
module_init(isst_if_mbox_init)

static void __exit isst_if_mbox_exit(void)
{
	unregister_pm_notifier(&isst_pm_nb);
	isst_if_cdev_unregister(ISST_IF_DEV_MBOX);
}
module_exit(isst_if_mbox_exit)

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel speed select interface mailbox driver");
