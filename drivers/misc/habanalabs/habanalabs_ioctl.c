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

static int hw_ip_info(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_hw_ip_info hw_ip = {0};
	u32 size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 sram_kmd_size, dram_kmd_size;

	if ((!size) || (!out))
		return -EINVAL;

	sram_kmd_size = (prop->sram_user_base_address -
				prop->sram_base_address);
	dram_kmd_size = (prop->dram_user_base_address -
				prop->dram_base_address);

	hw_ip.device_id = hdev->asic_funcs->get_pci_id(hdev);
	hw_ip.sram_base_address = prop->sram_user_base_address;
	hw_ip.dram_base_address = prop->dram_user_base_address;
	hw_ip.tpc_enabled_mask = prop->tpc_enabled_mask;
	hw_ip.sram_size = prop->sram_size - sram_kmd_size;
	hw_ip.dram_size = prop->dram_size - dram_kmd_size;
	if (hw_ip.dram_size > 0)
		hw_ip.dram_enabled = 1;
	hw_ip.num_of_events = prop->num_of_events;
	memcpy(hw_ip.armcp_version,
		prop->armcp_info.armcp_version, VERSION_MAX_LEN);
	hw_ip.armcp_cpld_version = __le32_to_cpu(prop->armcp_info.cpld_version);
	hw_ip.psoc_pci_pll_nr = prop->psoc_pci_pll_nr;
	hw_ip.psoc_pci_pll_nf = prop->psoc_pci_pll_nf;
	hw_ip.psoc_pci_pll_od = prop->psoc_pci_pll_od;
	hw_ip.psoc_pci_pll_div_factor = prop->psoc_pci_pll_div_factor;

	return copy_to_user(out, &hw_ip,
		min((size_t)size, sizeof(hw_ip))) ? -EFAULT : 0;
}

static int hw_events_info(struct hl_device *hdev, struct hl_info_args *args)
{
	u32 size, max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	void *arr;

	if ((!max_size) || (!out))
		return -EINVAL;

	arr = hdev->asic_funcs->get_events_stat(hdev, &size);

	return copy_to_user(out, arr, min(max_size, size)) ? -EFAULT : 0;
}

static int dram_usage_info(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_dram_usage dram_usage = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 dram_kmd_size;

	if ((!max_size) || (!out))
		return -EINVAL;

	dram_kmd_size = (prop->dram_user_base_address -
				prop->dram_base_address);
	dram_usage.dram_free_mem = (prop->dram_size - dram_kmd_size) -
					atomic64_read(&hdev->dram_used_mem);
	dram_usage.ctx_dram_mem = atomic64_read(&hdev->user_ctx->dram_phys_mem);

	return copy_to_user(out, &dram_usage,
		min((size_t) max_size, sizeof(dram_usage))) ? -EFAULT : 0;
}

static int hw_idle(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_hw_idle hw_idle = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	hw_idle.is_idle = hdev->asic_funcs->is_device_idle(hdev);

	return copy_to_user(out, &hw_idle,
		min((size_t) max_size, sizeof(hw_idle))) ? -EFAULT : 0;
}

static int hl_info_ioctl(struct hl_fpriv *hpriv, void *data)
{
	struct hl_info_args *args = data;
	struct hl_device *hdev = hpriv->hdev;
	int rc;

	if (hl_device_disabled_or_in_reset(hdev)) {
		dev_err(hdev->dev,
			"Device is disabled or in reset. Can't execute INFO IOCTL\n");
		return -EBUSY;
	}

	switch (args->op) {
	case HL_INFO_HW_IP_INFO:
		rc = hw_ip_info(hdev, args);
		break;

	case HL_INFO_HW_EVENTS:
		rc = hw_events_info(hdev, args);
		break;

	case HL_INFO_DRAM_USAGE:
		rc = dram_usage_info(hdev, args);
		break;

	case HL_INFO_HW_IDLE:
		rc = hw_idle(hdev, args);
		break;

	default:
		dev_err(hdev->dev, "Invalid request %d\n", args->op);
		rc = -ENOTTY;
		break;
	}

	return rc;
}

#define HL_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func}

static const struct hl_ioctl_desc hl_ioctls[] = {
	HL_IOCTL_DEF(HL_IOCTL_INFO, hl_info_ioctl),
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
