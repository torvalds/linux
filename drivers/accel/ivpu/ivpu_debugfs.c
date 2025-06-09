// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include <linux/debugfs.h>
#include <linux/fault-inject.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>

#include <uapi/drm/ivpu_accel.h>

#include "ivpu_debugfs.h"
#include "ivpu_drv.h"
#include "ivpu_fw.h"
#include "ivpu_fw_log.h"
#include "ivpu_gem.h"
#include "ivpu_hw.h"
#include "ivpu_jsm_msg.h"
#include "ivpu_pm.h"

static inline struct ivpu_device *seq_to_ivpu(struct seq_file *s)
{
	struct drm_debugfs_entry *entry = s->private;

	return to_ivpu_device(entry->dev);
}

static int bo_list_show(struct seq_file *s, void *v)
{
	struct drm_printer p = drm_seq_file_printer(s);
	struct ivpu_device *vdev = seq_to_ivpu(s);

	ivpu_bo_list(&vdev->drm, &p);

	return 0;
}

static int fw_name_show(struct seq_file *s, void *v)
{
	struct ivpu_device *vdev = seq_to_ivpu(s);

	seq_printf(s, "%s\n", vdev->fw->name);
	return 0;
}

static int fw_version_show(struct seq_file *s, void *v)
{
	struct ivpu_device *vdev = seq_to_ivpu(s);

	seq_printf(s, "%s\n", vdev->fw->version);
	return 0;
}

static int fw_trace_capability_show(struct seq_file *s, void *v)
{
	struct ivpu_device *vdev = seq_to_ivpu(s);
	u64 trace_hw_component_mask;
	u32 trace_destination_mask;
	int ret;

	ret = ivpu_jsm_trace_get_capability(vdev, &trace_destination_mask,
					    &trace_hw_component_mask);
	if (!ret) {
		seq_printf(s,
			   "trace_destination_mask:  %#18x\n"
			   "trace_hw_component_mask: %#18llx\n",
			   trace_destination_mask, trace_hw_component_mask);
	}
	return 0;
}

static int fw_trace_config_show(struct seq_file *s, void *v)
{
	struct ivpu_device *vdev = seq_to_ivpu(s);
	/**
	 * WA: VPU_JSM_MSG_TRACE_GET_CONFIG command is not working yet,
	 * so we use values from vdev->fw instead of calling ivpu_jsm_trace_get_config()
	 */
	u32 trace_level = vdev->fw->trace_level;
	u32 trace_destination_mask = vdev->fw->trace_destination_mask;
	u64 trace_hw_component_mask = vdev->fw->trace_hw_component_mask;

	seq_printf(s,
		   "trace_level:             %#18x\n"
		   "trace_destination_mask:  %#18x\n"
		   "trace_hw_component_mask: %#18llx\n",
		   trace_level, trace_destination_mask, trace_hw_component_mask);

	return 0;
}

static int last_bootmode_show(struct seq_file *s, void *v)
{
	struct ivpu_device *vdev = seq_to_ivpu(s);

	seq_printf(s, "%s\n", (vdev->pm->is_warmboot) ? "warmboot" : "coldboot");

	return 0;
}

static int reset_counter_show(struct seq_file *s, void *v)
{
	struct ivpu_device *vdev = seq_to_ivpu(s);

	seq_printf(s, "%d\n", atomic_read(&vdev->pm->reset_counter));
	return 0;
}

static int reset_pending_show(struct seq_file *s, void *v)
{
	struct ivpu_device *vdev = seq_to_ivpu(s);

	seq_printf(s, "%d\n", atomic_read(&vdev->pm->reset_pending));
	return 0;
}

static int firewall_irq_counter_show(struct seq_file *s, void *v)
{
	struct ivpu_device *vdev = seq_to_ivpu(s);

	seq_printf(s, "%d\n", atomic_read(&vdev->hw->firewall_irq_counter));
	return 0;
}

static const struct drm_debugfs_info vdev_debugfs_list[] = {
	{"bo_list", bo_list_show, 0},
	{"fw_name", fw_name_show, 0},
	{"fw_version", fw_version_show, 0},
	{"fw_trace_capability", fw_trace_capability_show, 0},
	{"fw_trace_config", fw_trace_config_show, 0},
	{"last_bootmode", last_bootmode_show, 0},
	{"reset_counter", reset_counter_show, 0},
	{"reset_pending", reset_pending_show, 0},
	{"firewall_irq_counter", firewall_irq_counter_show, 0},
};

static int dvfs_mode_get(void *data, u64 *dvfs_mode)
{
	struct ivpu_device *vdev = (struct ivpu_device *)data;

	*dvfs_mode = vdev->fw->dvfs_mode;
	return 0;
}

static int dvfs_mode_set(void *data, u64 dvfs_mode)
{
	struct ivpu_device *vdev = (struct ivpu_device *)data;

	vdev->fw->dvfs_mode = (u32)dvfs_mode;
	return pci_try_reset_function(to_pci_dev(vdev->drm.dev));
}

DEFINE_DEBUGFS_ATTRIBUTE(dvfs_mode_fops, dvfs_mode_get, dvfs_mode_set, "%llu\n");

static ssize_t
fw_dyndbg_fops_write(struct file *file, const char __user *user_buf, size_t size, loff_t *pos)
{
	struct ivpu_device *vdev = file->private_data;
	char buffer[VPU_DYNDBG_CMD_MAX_LEN] = {};
	int ret;

	if (size >= VPU_DYNDBG_CMD_MAX_LEN)
		return -EINVAL;

	ret = strncpy_from_user(buffer, user_buf, size);
	if (ret < 0)
		return ret;

	ivpu_jsm_dyndbg_control(vdev, buffer, size);
	return size;
}

static const struct file_operations fw_dyndbg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = fw_dyndbg_fops_write,
};

static int fw_log_show(struct seq_file *s, void *v)
{
	struct ivpu_device *vdev = s->private;
	struct drm_printer p = drm_seq_file_printer(s);

	ivpu_fw_log_print(vdev, true, &p);
	return 0;
}

static int fw_log_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, fw_log_show, inode->i_private);
}

static ssize_t
fw_log_fops_write(struct file *file, const char __user *user_buf, size_t size, loff_t *pos)
{
	struct seq_file *s = file->private_data;
	struct ivpu_device *vdev = s->private;

	if (!size)
		return -EINVAL;

	ivpu_fw_log_mark_read(vdev);
	return size;
}

static const struct file_operations fw_log_fops = {
	.owner = THIS_MODULE,
	.open = fw_log_fops_open,
	.write = fw_log_fops_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t
fw_profiling_freq_fops_write(struct file *file, const char __user *user_buf,
			     size_t size, loff_t *pos)
{
	struct ivpu_device *vdev = file->private_data;
	bool enable;
	int ret;

	ret = kstrtobool_from_user(user_buf, size, &enable);
	if (ret < 0)
		return ret;

	ivpu_hw_profiling_freq_drive(vdev, enable);

	ret = pci_try_reset_function(to_pci_dev(vdev->drm.dev));
	if (ret)
		return ret;

	return size;
}

static const struct file_operations fw_profiling_freq_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = fw_profiling_freq_fops_write,
};

static ssize_t
fw_trace_destination_mask_fops_write(struct file *file, const char __user *user_buf,
				     size_t size, loff_t *pos)
{
	struct ivpu_device *vdev = file->private_data;
	struct ivpu_fw_info *fw = vdev->fw;
	u32 trace_destination_mask;
	int ret;

	ret = kstrtou32_from_user(user_buf, size, 0, &trace_destination_mask);
	if (ret < 0)
		return ret;

	fw->trace_destination_mask = trace_destination_mask;

	ivpu_jsm_trace_set_config(vdev, fw->trace_level, trace_destination_mask,
				  fw->trace_hw_component_mask);

	return size;
}

static const struct file_operations fw_trace_destination_mask_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = fw_trace_destination_mask_fops_write,
};

static ssize_t
fw_trace_hw_comp_mask_fops_write(struct file *file, const char __user *user_buf,
				 size_t size, loff_t *pos)
{
	struct ivpu_device *vdev = file->private_data;
	struct ivpu_fw_info *fw = vdev->fw;
	u64 trace_hw_component_mask;
	int ret;

	ret = kstrtou64_from_user(user_buf, size, 0, &trace_hw_component_mask);
	if (ret < 0)
		return ret;

	fw->trace_hw_component_mask = trace_hw_component_mask;

	ivpu_jsm_trace_set_config(vdev, fw->trace_level, fw->trace_destination_mask,
				  trace_hw_component_mask);

	return size;
}

static const struct file_operations fw_trace_hw_comp_mask_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = fw_trace_hw_comp_mask_fops_write,
};

static ssize_t
fw_trace_level_fops_write(struct file *file, const char __user *user_buf, size_t size, loff_t *pos)
{
	struct ivpu_device *vdev = file->private_data;
	struct ivpu_fw_info *fw = vdev->fw;
	u32 trace_level;
	int ret;

	ret = kstrtou32_from_user(user_buf, size, 0, &trace_level);
	if (ret < 0)
		return ret;

	fw->trace_level = trace_level;

	ivpu_jsm_trace_set_config(vdev, trace_level, fw->trace_destination_mask,
				  fw->trace_hw_component_mask);

	return size;
}

static const struct file_operations fw_trace_level_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = fw_trace_level_fops_write,
};

static ssize_t
ivpu_force_recovery_fn(struct file *file, const char __user *user_buf, size_t size, loff_t *pos)
{
	struct ivpu_device *vdev = file->private_data;
	int ret;

	if (!size)
		return -EINVAL;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return ret;

	ivpu_pm_trigger_recovery(vdev, "debugfs");
	flush_work(&vdev->pm->recovery_work);
	ivpu_rpm_put(vdev);
	return size;
}

static const struct file_operations ivpu_force_recovery_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = ivpu_force_recovery_fn,
};

static int ivpu_reset_engine_fn(void *data, u64 val)
{
	struct ivpu_device *vdev = (struct ivpu_device *)data;

	return ivpu_jsm_reset_engine(vdev, (u32)val);
}

DEFINE_DEBUGFS_ATTRIBUTE(ivpu_reset_engine_fops, NULL, ivpu_reset_engine_fn, "0x%02llx\n");

static int ivpu_resume_engine_fn(void *data, u64 val)
{
	struct ivpu_device *vdev = (struct ivpu_device *)data;

	return ivpu_jsm_hws_resume_engine(vdev, (u32)val);
}

DEFINE_DEBUGFS_ATTRIBUTE(ivpu_resume_engine_fops, NULL, ivpu_resume_engine_fn, "0x%02llx\n");

static int dct_active_get(void *data, u64 *active_percent)
{
	struct ivpu_device *vdev = data;

	*active_percent = vdev->pm->dct_active_percent;

	return 0;
}

static int dct_active_set(void *data, u64 active_percent)
{
	struct ivpu_device *vdev = data;
	int ret;

	if (active_percent > 100)
		return -EINVAL;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return ret;

	if (active_percent)
		ret = ivpu_pm_dct_enable(vdev, active_percent);
	else
		ret = ivpu_pm_dct_disable(vdev);

	ivpu_rpm_put(vdev);

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(ivpu_dct_fops, dct_active_get, dct_active_set, "%llu\n");

static int priority_bands_show(struct seq_file *s, void *v)
{
	struct ivpu_device *vdev = s->private;
	struct ivpu_hw_info *hw = vdev->hw;

	for (int band = VPU_JOB_SCHEDULING_PRIORITY_BAND_IDLE;
	     band < VPU_JOB_SCHEDULING_PRIORITY_BAND_COUNT; band++) {
		switch (band) {
		case VPU_JOB_SCHEDULING_PRIORITY_BAND_IDLE:
			seq_puts(s, "Idle:     ");
			break;

		case VPU_JOB_SCHEDULING_PRIORITY_BAND_NORMAL:
			seq_puts(s, "Normal:   ");
			break;

		case VPU_JOB_SCHEDULING_PRIORITY_BAND_FOCUS:
			seq_puts(s, "Focus:    ");
			break;

		case VPU_JOB_SCHEDULING_PRIORITY_BAND_REALTIME:
			seq_puts(s, "Realtime: ");
			break;
		}

		seq_printf(s, "grace_period %9u process_grace_period %9u process_quantum %9u\n",
			   hw->hws.grace_period[band], hw->hws.process_grace_period[band],
			   hw->hws.process_quantum[band]);
	}

	return 0;
}

static int priority_bands_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, priority_bands_show, inode->i_private);
}

static ssize_t
priority_bands_fops_write(struct file *file, const char __user *user_buf, size_t size, loff_t *pos)
{
	struct seq_file *s = file->private_data;
	struct ivpu_device *vdev = s->private;
	char buf[64];
	u32 grace_period;
	u32 process_grace_period;
	u32 process_quantum;
	u32 band;
	int ret;

	if (size >= sizeof(buf))
		return -EINVAL;

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, pos, user_buf, size);
	if (ret < 0)
		return ret;

	buf[ret] = '\0';
	ret = sscanf(buf, "%u %u %u %u", &band, &grace_period, &process_grace_period,
		     &process_quantum);
	if (ret != 4)
		return -EINVAL;

	if (band >= VPU_JOB_SCHEDULING_PRIORITY_BAND_COUNT)
		return -EINVAL;

	vdev->hw->hws.grace_period[band] = grace_period;
	vdev->hw->hws.process_grace_period[band] = process_grace_period;
	vdev->hw->hws.process_quantum[band] = process_quantum;

	return size;
}

static const struct file_operations ivpu_hws_priority_bands_fops = {
	.owner = THIS_MODULE,
	.open = priority_bands_fops_open,
	.write = priority_bands_fops_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void ivpu_debugfs_init(struct ivpu_device *vdev)
{
	struct dentry *debugfs_root = vdev->drm.debugfs_root;

	drm_debugfs_add_files(&vdev->drm, vdev_debugfs_list, ARRAY_SIZE(vdev_debugfs_list));

	debugfs_create_file("force_recovery", 0200, debugfs_root, vdev,
			    &ivpu_force_recovery_fops);

	debugfs_create_file("dvfs_mode", 0644, debugfs_root, vdev,
			    &dvfs_mode_fops);

	debugfs_create_file("fw_dyndbg", 0200, debugfs_root, vdev,
			    &fw_dyndbg_fops);
	debugfs_create_file("fw_log", 0644, debugfs_root, vdev,
			    &fw_log_fops);
	debugfs_create_file("fw_trace_destination_mask", 0200, debugfs_root, vdev,
			    &fw_trace_destination_mask_fops);
	debugfs_create_file("fw_trace_hw_comp_mask", 0200, debugfs_root, vdev,
			    &fw_trace_hw_comp_mask_fops);
	debugfs_create_file("fw_trace_level", 0200, debugfs_root, vdev,
			    &fw_trace_level_fops);
	debugfs_create_file("hws_priority_bands", 0200, debugfs_root, vdev,
			    &ivpu_hws_priority_bands_fops);

	debugfs_create_file("reset_engine", 0200, debugfs_root, vdev,
			    &ivpu_reset_engine_fops);
	debugfs_create_file("resume_engine", 0200, debugfs_root, vdev,
			    &ivpu_resume_engine_fops);

	if (ivpu_hw_ip_gen(vdev) >= IVPU_HW_IP_40XX) {
		debugfs_create_file("fw_profiling_freq_drive", 0200,
				    debugfs_root, vdev, &fw_profiling_freq_fops);
		debugfs_create_file("dct", 0644, debugfs_root, vdev, &ivpu_dct_fops);
	}

#ifdef CONFIG_FAULT_INJECTION
	fault_create_debugfs_attr("fail_hw", debugfs_root, &ivpu_hw_failure);
#endif
}
