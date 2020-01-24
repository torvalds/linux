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

static u32 hl_debug_struct_size[HL_DEBUG_OP_TIMESTAMP + 1] = {
	[HL_DEBUG_OP_ETR] = sizeof(struct hl_debug_params_etr),
	[HL_DEBUG_OP_ETF] = sizeof(struct hl_debug_params_etf),
	[HL_DEBUG_OP_STM] = sizeof(struct hl_debug_params_stm),
	[HL_DEBUG_OP_FUNNEL] = 0,
	[HL_DEBUG_OP_BMON] = sizeof(struct hl_debug_params_bmon),
	[HL_DEBUG_OP_SPMU] = sizeof(struct hl_debug_params_spmu),
	[HL_DEBUG_OP_TIMESTAMP] = 0

};

static int device_status_info(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_device_status dev_stat = {0};
	u32 size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!size) || (!out))
		return -EINVAL;

	dev_stat.status = hl_device_status(hdev);

	return copy_to_user(out, &dev_stat,
			min((size_t)size, sizeof(dev_stat))) ? -EFAULT : 0;
}

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
	if (hw_ip.dram_size > PAGE_SIZE)
		hw_ip.dram_enabled = 1;
	hw_ip.num_of_events = prop->num_of_events;

	memcpy(hw_ip.armcp_version, prop->armcp_info.armcp_version,
		min(VERSION_MAX_LEN, HL_INFO_VERSION_MAX_LEN));

	memcpy(hw_ip.card_name, prop->armcp_info.card_name,
		min(CARD_NAME_MAX_LEN, HL_INFO_CARD_NAME_MAX_LEN));

	hw_ip.armcp_cpld_version = le32_to_cpu(prop->armcp_info.cpld_version);
	hw_ip.psoc_pci_pll_nr = prop->psoc_pci_pll_nr;
	hw_ip.psoc_pci_pll_nf = prop->psoc_pci_pll_nf;
	hw_ip.psoc_pci_pll_od = prop->psoc_pci_pll_od;
	hw_ip.psoc_pci_pll_div_factor = prop->psoc_pci_pll_div_factor;

	return copy_to_user(out, &hw_ip,
		min((size_t)size, sizeof(hw_ip))) ? -EFAULT : 0;
}

static int hw_events_info(struct hl_device *hdev, bool aggregate,
			struct hl_info_args *args)
{
	u32 size, max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	void *arr;

	if ((!max_size) || (!out))
		return -EINVAL;

	arr = hdev->asic_funcs->get_events_stat(hdev, aggregate, &size);

	return copy_to_user(out, arr, min(max_size, size)) ? -EFAULT : 0;
}

static int dram_usage_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
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
	if (hpriv->ctx)
		dram_usage.ctx_dram_mem =
			atomic64_read(&hpriv->ctx->dram_phys_mem);

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

	hw_idle.is_idle = hdev->asic_funcs->is_device_idle(hdev,
					&hw_idle.busy_engines_mask, NULL);

	return copy_to_user(out, &hw_idle,
		min((size_t) max_size, sizeof(hw_idle))) ? -EFAULT : 0;
}

static int debug_coresight(struct hl_device *hdev, struct hl_debug_args *args)
{
	struct hl_debug_params *params;
	void *input = NULL, *output = NULL;
	int rc;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	params->reg_idx = args->reg_idx;
	params->enable = args->enable;
	params->op = args->op;

	if (args->input_ptr && args->input_size) {
		input = kzalloc(hl_debug_struct_size[args->op], GFP_KERNEL);
		if (!input) {
			rc = -ENOMEM;
			goto out;
		}

		if (copy_from_user(input, u64_to_user_ptr(args->input_ptr),
					args->input_size)) {
			rc = -EFAULT;
			dev_err(hdev->dev, "failed to copy input debug data\n");
			goto out;
		}

		params->input = input;
	}

	if (args->output_ptr && args->output_size) {
		output = kzalloc(args->output_size, GFP_KERNEL);
		if (!output) {
			rc = -ENOMEM;
			goto out;
		}

		params->output = output;
		params->output_size = args->output_size;
	}

	rc = hdev->asic_funcs->debug_coresight(hdev, params);
	if (rc) {
		dev_err(hdev->dev,
			"debug coresight operation failed %d\n", rc);
		goto out;
	}

	if (output && copy_to_user((void __user *) (uintptr_t) args->output_ptr,
					output, args->output_size)) {
		dev_err(hdev->dev, "copy to user failed in debug ioctl\n");
		rc = -EFAULT;
		goto out;
	}


out:
	kfree(params);
	kfree(output);
	kfree(input);

	return rc;
}

static int device_utilization(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_device_utilization device_util = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	if ((args->period_ms < 100) || (args->period_ms > 1000) ||
		(args->period_ms % 100)) {
		dev_err(hdev->dev,
			"period %u must be between 100 - 1000 and must be divisible by 100\n",
			args->period_ms);
		return -EINVAL;
	}

	device_util.utilization = hl_device_utilization(hdev, args->period_ms);

	return copy_to_user(out, &device_util,
		min((size_t) max_size, sizeof(device_util))) ? -EFAULT : 0;
}

static int get_clk_rate(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_clk_rate clk_rate = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hdev->asic_funcs->get_clk_rate(hdev, &clk_rate.cur_clk_rate_mhz,
						&clk_rate.max_clk_rate_mhz);
	if (rc)
		return rc;

	return copy_to_user(out, &clk_rate,
		min((size_t) max_size, sizeof(clk_rate))) ? -EFAULT : 0;
}

static int get_reset_count(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_reset_count reset_count = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	reset_count.hard_reset_cnt = hdev->hard_reset_cnt;
	reset_count.soft_reset_cnt = hdev->soft_reset_cnt;

	return copy_to_user(out, &reset_count,
		min((size_t) max_size, sizeof(reset_count))) ? -EFAULT : 0;
}

static int _hl_info_ioctl(struct hl_fpriv *hpriv, void *data,
				struct device *dev)
{
	struct hl_info_args *args = data;
	struct hl_device *hdev = hpriv->hdev;
	int rc;

	/*
	 * Information is returned for the following opcodes even if the device
	 * is disabled or in reset.
	 */
	switch (args->op) {
	case HL_INFO_HW_IP_INFO:
		return hw_ip_info(hdev, args);

	case HL_INFO_DEVICE_STATUS:
		return device_status_info(hdev, args);

	case HL_INFO_RESET_COUNT:
		return get_reset_count(hdev, args);

	default:
		break;
	}

	if (hl_device_disabled_or_in_reset(hdev)) {
		dev_warn_ratelimited(dev,
			"Device is %s. Can't execute INFO IOCTL\n",
			atomic_read(&hdev->in_reset) ? "in_reset" : "disabled");
		return -EBUSY;
	}

	switch (args->op) {
	case HL_INFO_HW_EVENTS:
		rc = hw_events_info(hdev, false, args);
		break;

	case HL_INFO_DRAM_USAGE:
		rc = dram_usage_info(hpriv, args);
		break;

	case HL_INFO_HW_IDLE:
		rc = hw_idle(hdev, args);
		break;

	case HL_INFO_DEVICE_UTILIZATION:
		rc = device_utilization(hdev, args);
		break;

	case HL_INFO_HW_EVENTS_AGGREGATE:
		rc = hw_events_info(hdev, true, args);
		break;

	case HL_INFO_CLK_RATE:
		rc = get_clk_rate(hdev, args);
		break;

	default:
		dev_err(dev, "Invalid request %d\n", args->op);
		rc = -ENOTTY;
		break;
	}

	return rc;
}

static int hl_info_ioctl(struct hl_fpriv *hpriv, void *data)
{
	return _hl_info_ioctl(hpriv, data, hpriv->hdev->dev);
}

static int hl_info_ioctl_control(struct hl_fpriv *hpriv, void *data)
{
	return _hl_info_ioctl(hpriv, data, hpriv->hdev->dev_ctrl);
}

static int hl_debug_ioctl(struct hl_fpriv *hpriv, void *data)
{
	struct hl_debug_args *args = data;
	struct hl_device *hdev = hpriv->hdev;
	int rc = 0;

	if (hl_device_disabled_or_in_reset(hdev)) {
		dev_warn_ratelimited(hdev->dev,
			"Device is %s. Can't execute DEBUG IOCTL\n",
			atomic_read(&hdev->in_reset) ? "in_reset" : "disabled");
		return -EBUSY;
	}

	switch (args->op) {
	case HL_DEBUG_OP_ETR:
	case HL_DEBUG_OP_ETF:
	case HL_DEBUG_OP_STM:
	case HL_DEBUG_OP_FUNNEL:
	case HL_DEBUG_OP_BMON:
	case HL_DEBUG_OP_SPMU:
	case HL_DEBUG_OP_TIMESTAMP:
		if (!hdev->in_debug) {
			dev_err_ratelimited(hdev->dev,
				"Rejecting debug configuration request because device not in debug mode\n");
			return -EFAULT;
		}
		args->input_size =
			min(args->input_size, hl_debug_struct_size[args->op]);
		rc = debug_coresight(hdev, args);
		break;
	case HL_DEBUG_OP_SET_MODE:
		rc = hl_device_set_debug_mode(hdev, (bool) args->enable);
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
	HL_IOCTL_DEF(HL_IOCTL_MEMORY, hl_mem_ioctl),
	HL_IOCTL_DEF(HL_IOCTL_DEBUG, hl_debug_ioctl)
};

static const struct hl_ioctl_desc hl_ioctls_control[] = {
	HL_IOCTL_DEF(HL_IOCTL_INFO, hl_info_ioctl_control)
};

static long _hl_ioctl(struct file *filep, unsigned int cmd, unsigned long arg,
		const struct hl_ioctl_desc *ioctl, struct device *dev)
{
	struct hl_fpriv *hpriv = filep->private_data;
	struct hl_device *hdev = hpriv->hdev;
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128] = {0};
	char *kdata = NULL;
	unsigned int usize, asize;
	hl_ioctl_t *func;
	u32 hl_size;
	int retcode;

	if (hdev->hard_reset_pending) {
		dev_crit_ratelimited(hdev->dev_ctrl,
			"Device HARD reset pending! Please close FD\n");
		return -ENODEV;
	}

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		dev_dbg(dev, "no function\n");
		retcode = -ENOTTY;
		goto out_err;
	}

	hl_size = _IOC_SIZE(ioctl->cmd);
	usize = asize = _IOC_SIZE(cmd);
	if (hl_size > asize)
		asize = hl_size;

	cmd = ioctl->cmd;

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

	if ((cmd & IOC_OUT) && copy_to_user((void __user *)arg, kdata, usize))
		retcode = -EFAULT;

out_err:
	if (retcode)
		dev_dbg(dev, "error in ioctl: pid=%d, cmd=0x%02x, nr=0x%02x\n",
			  task_pid_nr(current), cmd, nr);

	if (kdata != stack_kdata)
		kfree(kdata);

	return retcode;
}

long hl_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct hl_fpriv *hpriv = filep->private_data;
	struct hl_device *hdev = hpriv->hdev;
	const struct hl_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);

	if ((nr >= HL_COMMAND_START) && (nr < HL_COMMAND_END)) {
		ioctl = &hl_ioctls[nr];
	} else {
		dev_err(hdev->dev, "invalid ioctl: pid=%d, nr=0x%02x\n",
			task_pid_nr(current), nr);
		return -ENOTTY;
	}

	return _hl_ioctl(filep, cmd, arg, ioctl, hdev->dev);
}

long hl_ioctl_control(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct hl_fpriv *hpriv = filep->private_data;
	struct hl_device *hdev = hpriv->hdev;
	const struct hl_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);

	if (nr == _IOC_NR(HL_IOCTL_INFO)) {
		ioctl = &hl_ioctls_control[nr];
	} else {
		dev_err(hdev->dev_ctrl, "invalid ioctl: pid=%d, nr=0x%02x\n",
			task_pid_nr(current), nr);
		return -ENOTTY;
	}

	return _hl_ioctl(filep, cmd, arg, ioctl, hdev->dev_ctrl);
}
