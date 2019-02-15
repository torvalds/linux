// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include <uapi/misc/habanalabs.h>
#include "habanalabs.h"

#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define HL_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func}

static const struct hl_ioctl_desc hl_ioctls[] = {
	HL_IOCTL_DEF(HL_IOCTL_CB, hl_cb_ioctl),
	HL_IOCTL_DEF(HL_IOCTL_CS, hl_cs_ioctl),
	HL_IOCTL_DEF(HL_IOCTL_WAIT_CS, hl_cs_wait_ioctl),
	HL_IOCTL_DEF(HL_IOCTL_MEMORY, hl_mem_ioctl)
};

#define HL_CORE_IOCTL_COUNT	ARRAY_SIZE(hl_ioctls)

long hl_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct hl_fpriv *hpriv = filep->private_data;
	struct hl_device *hdev = hpriv->hdev;
	hl_ioctl_t *func;
	const struct hl_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128] = {0};
	char *kdata = NULL;
	unsigned int usize, asize;
	int retcode;

	if (hdev->hard_reset_pending) {
		dev_crit_ratelimited(hdev->dev,
			"Device HARD reset pending! Please close FD\n");
		return -ENODEV;
	}

	if ((nr >= HL_COMMAND_START) && (nr < HL_COMMAND_END)) {
		u32 hl_size;

		ioctl = &hl_ioctls[nr];

		hl_size = _IOC_SIZE(ioctl->cmd);
		usize = asize = _IOC_SIZE(cmd);
		if (hl_size > asize)
			asize = hl_size;

		cmd = ioctl->cmd;
	} else {
		dev_err(hdev->dev, "invalid ioctl: pid=%d, nr=0x%02x\n",
			  task_pid_nr(current), nr);
		return -ENOTTY;
	}

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		dev_dbg(hdev->dev, "no function\n");
		retcode = -ENOTTY;
		goto out_err;
	}

	if (cmd & (IOC_IN | IOC_OUT)) {
		if (asize <= sizeof(stack_kdata)) {
			kdata = stack_kdata;
		} else {
			kdata = kzalloc(asize, GFP_KERNEL);
			if (!kdata) {
				retcode = -ENOMEM;
				goto out_err;
			}
		}
	}

	if (cmd & IOC_IN) {
		if (copy_from_user(kdata, (void __user *)arg, usize)) {
			retcode = -EFAULT;
			goto out_err;
		}
	} else if (cmd & IOC_OUT) {
		memset(kdata, 0, usize);
	}

	retcode = func(hpriv, kdata);

	if (cmd & IOC_OUT)
		if (copy_to_user((void __user *)arg, kdata, usize))
			retcode = -EFAULT;

out_err:
	if (retcode)
		dev_dbg(hdev->dev,
			"error in ioctl: pid=%d, cmd=0x%02x, nr=0x%02x\n",
			  task_pid_nr(current), cmd, nr);

	if (kdata != stack_kdata)
		kfree(kdata);

	return retcode;
}
