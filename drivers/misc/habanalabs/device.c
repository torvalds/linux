// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)			"habanalabs: " fmt

#include "habanalabs.h"

#include <linux/pci.h>
#include <linux/sched/signal.h>
#include <linux/hwmon.h>
#include <uapi/misc/habanalabs.h>

#define HL_PLDM_PENDING_RESET_PER_SEC	(HL_PENDING_RESET_PER_SEC * 10)

bool hl_device_disabled_or_in_reset(struct hl_device *hdev)
{
	if ((hdev->disabled) || (atomic_read(&hdev->in_reset)))
		return true;
	else
		return false;
}

enum hl_device_status hl_device_status(struct hl_device *hdev)
{
	enum hl_device_status status;

	if (hdev->disabled)
		status = HL_DEVICE_STATUS_MALFUNCTION;
	else if (atomic_read(&hdev->in_reset))
		status = HL_DEVICE_STATUS_IN_RESET;
	else
		status = HL_DEVICE_STATUS_OPERATIONAL;

	return status;
};

static void hpriv_release(struct kref *ref)
{
	struct hl_fpriv *hpriv;
	struct hl_device *hdev;

	hpriv = container_of(ref, struct hl_fpriv, refcount);

	hdev = hpriv->hdev;

	put_pid(hpriv->taskpid);

	hl_debugfs_remove_file(hpriv);

	mutex_destroy(&hpriv->restore_phase_mutex);

	mutex_lock(&hdev->fpriv_list_lock);
	list_del(&hpriv->dev_node);
	hdev->compute_ctx = NULL;
	mutex_unlock(&hdev->fpriv_list_lock);

	kfree(hpriv);
}

void hl_hpriv_get(struct hl_fpriv *hpriv)
{
	kref_get(&hpriv->refcount);
}

void hl_hpriv_put(struct hl_fpriv *hpriv)
{
	kref_put(&hpriv->refcount, hpriv_release);
}

/*
 * hl_device_release - release function for habanalabs device
 *
 * @inode: pointer to inode structure
 * @filp: pointer to file structure
 *
 * Called when process closes an habanalabs device
 */
static int hl_device_release(struct inode *inode, struct file *filp)
{
	struct hl_fpriv *hpriv = filp->private_data;

	hl_cb_mgr_fini(hpriv->hdev, &hpriv->cb_mgr);
	hl_ctx_mgr_fini(hpriv->hdev, &hpriv->ctx_mgr);

	filp->private_data = NULL;

	hl_hpriv_put(hpriv);

	return 0;
}

static int hl_device_release_ctrl(struct inode *inode, struct file *filp)
{
	struct hl_fpriv *hpriv = filp->private_data;
	struct hl_device *hdev;

	filp->private_data = NULL;

	hdev = hpriv->hdev;

	mutex_lock(&hdev->fpriv_list_lock);
	list_del(&hpriv->dev_node);
	mutex_unlock(&hdev->fpriv_list_lock);

	kfree(hpriv);

	return 0;
}

/*
 * hl_mmap - mmap function for habanalabs device
 *
 * @*filp: pointer to file structure
 * @*vma: pointer to vm_area_struct of the process
 *
 * Called when process does an mmap on habanalabs device. Call the device's mmap
 * function at the end of the common code.
 */
static int hl_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct hl_fpriv *hpriv = filp->private_data;

	if ((vma->vm_pgoff & HL_MMAP_CB_MASK) == HL_MMAP_CB_MASK) {
		vma->vm_pgoff ^= HL_MMAP_CB_MASK;
		return hl_cb_mmap(hpriv, vma);
	}

	return -EINVAL;
}

static const struct file_operations hl_ops = {
	.owner = THIS_MODULE,
	.open = hl_device_open,
	.release = hl_device_release,
	.mmap = hl_mmap,
	.unlocked_ioctl = hl_ioctl,
	.compat_ioctl = hl_ioctl
};

static const struct file_operations hl_ctrl_ops = {
	.owner = THIS_MODULE,
	.open = hl_device_open_ctrl,
	.release = hl_device_release_ctrl,
	.unlocked_ioctl = hl_ioctl_control,
	.compat_ioctl = hl_ioctl_control
};

static void device_release_func(struct device *dev)
{
	kfree(dev);
}

/*
 * device_init_cdev - Initialize cdev and device for habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 * @hclass: pointer to the class object of the device
 * @minor: minor number of the specific device
 * @fpos: file operations to install for this device
 * @name: name of the device as it will appear in the filesystem
 * @cdev: pointer to the char device object that will be initialized
 * @dev: pointer to the device object that will be initialized
 *
 * Initialize a cdev and a Linux device for habanalabs's device.
 */
static int device_init_cdev(struct hl_device *hdev, struct class *hclass,
				int minor, const struct file_operations *fops,
				char *name, struct cdev *cdev,
				struct device **dev)
{
	cdev_init(cdev, fops);
	cdev->owner = THIS_MODULE;

	*dev = kzalloc(sizeof(**dev), GFP_KERNEL);
	if (!*dev)
		return -ENOMEM;

	device_initialize(*dev);
	(*dev)->devt = MKDEV(hdev->major, minor);
	(*dev)->class = hclass;
	(*dev)->release = device_release_func;
	dev_set_drvdata(*dev, hdev);
	dev_set_name(*dev, "%s", name);

	return 0;
}

static int device_cdev_sysfs_add(struct hl_device *hdev)
{
	int rc;

	rc = cdev_device_add(&hdev->cdev, hdev->dev);
	if (rc) {
		dev_err(hdev->dev,
			"failed to add a char device to the system\n");
		return rc;
	}

	rc = cdev_device_add(&hdev->cdev_ctrl, hdev->dev_ctrl);
	if (rc) {
		dev_err(hdev->dev,
			"failed to add a control char device to the system\n");
		goto delete_cdev_device;
	}

	/* hl_sysfs_init() must be done after adding the device to the system */
	rc = hl_sysfs_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize sysfs\n");
		goto delete_ctrl_cdev_device;
	}

	hdev->cdev_sysfs_created = true;

	return 0;

delete_ctrl_cdev_device:
	cdev_device_del(&hdev->cdev_ctrl, hdev->dev_ctrl);
delete_cdev_device:
	cdev_device_del(&hdev->cdev, hdev->dev);
	return rc;
}

static void device_cdev_sysfs_del(struct hl_device *hdev)
{
	/* device_release() won't be called so must free devices explicitly */
	if (!hdev->cdev_sysfs_created) {
		kfree(hdev->dev_ctrl);
		kfree(hdev->dev);
		return;
	}

	hl_sysfs_fini(hdev);
	cdev_device_del(&hdev->cdev_ctrl, hdev->dev_ctrl);
	cdev_device_del(&hdev->cdev, hdev->dev);
}

/*
 * device_early_init - do some early initialization for the habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Install the relevant function pointers and call the early_init function,
 * if such a function exists
 */
static int device_early_init(struct hl_device *hdev)
{
	int rc;

	switch (hdev->asic_type) {
	case ASIC_GOYA:
		goya_set_asic_funcs(hdev);
		strlcpy(hdev->asic_name, "GOYA", sizeof(hdev->asic_name));
		break;
	default:
		dev_err(hdev->dev, "Unrecognized ASIC type %d\n",
			hdev->asic_type);
		return -EINVAL;
	}

	rc = hdev->asic_funcs->early_init(hdev);
	if (rc)
		return rc;

	rc = hl_asid_init(hdev);
	if (rc)
		goto early_fini;

	hdev->cq_wq = alloc_workqueue("hl-free-jobs", WQ_UNBOUND, 0);
	if (hdev->cq_wq == NULL) {
		dev_err(hdev->dev, "Failed to allocate CQ workqueue\n");
		rc = -ENOMEM;
		goto asid_fini;
	}

	hdev->eq_wq = alloc_workqueue("hl-events", WQ_UNBOUND, 0);
	if (hdev->eq_wq == NULL) {
		dev_err(hdev->dev, "Failed to allocate EQ workqueue\n");
		rc = -ENOMEM;
		goto free_cq_wq;
	}

	hdev->hl_chip_info = kzalloc(sizeof(struct hwmon_chip_info),
					GFP_KERNEL);
	if (!hdev->hl_chip_info) {
		rc = -ENOMEM;
		goto free_eq_wq;
	}

	hdev->idle_busy_ts_arr = kmalloc_array(HL_IDLE_BUSY_TS_ARR_SIZE,
					sizeof(struct hl_device_idle_busy_ts),
					(GFP_KERNEL | __GFP_ZERO));
	if (!hdev->idle_busy_ts_arr) {
		rc = -ENOMEM;
		goto free_chip_info;
	}

	hl_cb_mgr_init(&hdev->kernel_cb_mgr);

	mutex_init(&hdev->send_cpu_message_lock);
	mutex_init(&hdev->debug_lock);
	mutex_init(&hdev->mmu_cache_lock);
	INIT_LIST_HEAD(&hdev->hw_queues_mirror_list);
	spin_lock_init(&hdev->hw_queues_mirror_lock);
	INIT_LIST_HEAD(&hdev->fpriv_list);
	mutex_init(&hdev->fpriv_list_lock);
	atomic_set(&hdev->in_reset, 0);

	return 0;

free_chip_info:
	kfree(hdev->hl_chip_info);
free_eq_wq:
	destroy_workqueue(hdev->eq_wq);
free_cq_wq:
	destroy_workqueue(hdev->cq_wq);
asid_fini:
	hl_asid_fini(hdev);
early_fini:
	if (hdev->asic_funcs->early_fini)
		hdev->asic_funcs->early_fini(hdev);

	return rc;
}

/*
 * device_early_fini - finalize all that was done in device_early_init
 *
 * @hdev: pointer to habanalabs device structure
 *
 */
static void device_early_fini(struct hl_device *hdev)
{
	mutex_destroy(&hdev->mmu_cache_lock);
	mutex_destroy(&hdev->debug_lock);
	mutex_destroy(&hdev->send_cpu_message_lock);

	mutex_destroy(&hdev->fpriv_list_lock);

	hl_cb_mgr_fini(hdev, &hdev->kernel_cb_mgr);

	kfree(hdev->idle_busy_ts_arr);
	kfree(hdev->hl_chip_info);

	destroy_workqueue(hdev->eq_wq);
	destroy_workqueue(hdev->cq_wq);

	hl_asid_fini(hdev);

	if (hdev->asic_funcs->early_fini)
		hdev->asic_funcs->early_fini(hdev);
}

static void set_freq_to_low_job(struct work_struct *work)
{
	struct hl_device *hdev = container_of(work, struct hl_device,
						work_freq.work);

	mutex_lock(&hdev->fpriv_list_lock);

	if (!hdev->compute_ctx)
		hl_device_set_frequency(hdev, PLL_LOW);

	mutex_unlock(&hdev->fpriv_list_lock);

	schedule_delayed_work(&hdev->work_freq,
			usecs_to_jiffies(HL_PLL_LOW_JOB_FREQ_USEC));
}

static void hl_device_heartbeat(struct work_struct *work)
{
	struct hl_device *hdev = container_of(work, struct hl_device,
						work_heartbeat.work);

	if (hl_device_disabled_or_in_reset(hdev))
		goto reschedule;

	if (!hdev->asic_funcs->send_heartbeat(hdev))
		goto reschedule;

	dev_err(hdev->dev, "Device heartbeat failed!\n");
	hl_device_reset(hdev, true, false);

	return;

reschedule:
	schedule_delayed_work(&hdev->work_heartbeat,
			usecs_to_jiffies(HL_HEARTBEAT_PER_USEC));
}

/*
 * device_late_init - do late stuff initialization for the habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Do stuff that either needs the device H/W queues to be active or needs
 * to happen after all the rest of the initialization is finished
 */
static int device_late_init(struct hl_device *hdev)
{
	int rc;

	if (hdev->asic_funcs->late_init) {
		rc = hdev->asic_funcs->late_init(hdev);
		if (rc) {
			dev_err(hdev->dev,
				"failed late initialization for the H/W\n");
			return rc;
		}
	}

	hdev->high_pll = hdev->asic_prop.high_pll;

	/* force setting to low frequency */
	hdev->curr_pll_profile = PLL_LOW;

	if (hdev->pm_mng_profile == PM_AUTO)
		hdev->asic_funcs->set_pll_profile(hdev, PLL_LOW);
	else
		hdev->asic_funcs->set_pll_profile(hdev, PLL_LAST);

	INIT_DELAYED_WORK(&hdev->work_freq, set_freq_to_low_job);
	schedule_delayed_work(&hdev->work_freq,
	usecs_to_jiffies(HL_PLL_LOW_JOB_FREQ_USEC));

	if (hdev->heartbeat) {
		INIT_DELAYED_WORK(&hdev->work_heartbeat, hl_device_heartbeat);
		schedule_delayed_work(&hdev->work_heartbeat,
				usecs_to_jiffies(HL_HEARTBEAT_PER_USEC));
	}

	hdev->late_init_done = true;

	return 0;
}

/*
 * device_late_fini - finalize all that was done in device_late_init
 *
 * @hdev: pointer to habanalabs device structure
 *
 */
static void device_late_fini(struct hl_device *hdev)
{
	if (!hdev->late_init_done)
		return;

	cancel_delayed_work_sync(&hdev->work_freq);
	if (hdev->heartbeat)
		cancel_delayed_work_sync(&hdev->work_heartbeat);

	if (hdev->asic_funcs->late_fini)
		hdev->asic_funcs->late_fini(hdev);

	hdev->late_init_done = false;
}

uint32_t hl_device_utilization(struct hl_device *hdev, uint32_t period_ms)
{
	struct hl_device_idle_busy_ts *ts;
	ktime_t zero_ktime, curr = ktime_get();
	u32 overlap_cnt = 0, last_index = hdev->idle_busy_ts_idx;
	s64 period_us, last_start_us, last_end_us, last_busy_time_us,
		total_busy_time_us = 0, total_busy_time_ms;

	zero_ktime = ktime_set(0, 0);
	period_us = period_ms * USEC_PER_MSEC;
	ts = &hdev->idle_busy_ts_arr[last_index];

	/* check case that device is currently in idle */
	if (!ktime_compare(ts->busy_to_idle_ts, zero_ktime) &&
			!ktime_compare(ts->idle_to_busy_ts, zero_ktime)) {

		last_index--;
		/* Handle case idle_busy_ts_idx was 0 */
		if (last_index > HL_IDLE_BUSY_TS_ARR_SIZE)
			last_index = HL_IDLE_BUSY_TS_ARR_SIZE - 1;

		ts = &hdev->idle_busy_ts_arr[last_index];
	}

	while (overlap_cnt < HL_IDLE_BUSY_TS_ARR_SIZE) {
		/* Check if we are in last sample case. i.e. if the sample
		 * begun before the sampling period. This could be a real
		 * sample or 0 so need to handle both cases
		 */
		last_start_us = ktime_to_us(
				ktime_sub(curr, ts->idle_to_busy_ts));

		if (last_start_us > period_us) {

			/* First check two cases:
			 * 1. If the device is currently busy
			 * 2. If the device was idle during the whole sampling
			 *    period
			 */

			if (!ktime_compare(ts->busy_to_idle_ts, zero_ktime)) {
				/* Check if the device is currently busy */
				if (ktime_compare(ts->idle_to_busy_ts,
						zero_ktime))
					return 100;

				/* We either didn't have any activity or we
				 * reached an entry which is 0. Either way,
				 * exit and return what was accumulated so far
				 */
				break;
			}

			/* If sample has finished, check it is relevant */
			last_end_us = ktime_to_us(
					ktime_sub(curr, ts->busy_to_idle_ts));

			if (last_end_us > period_us)
				break;

			/* It is relevant so add it but with adjustment */
			last_busy_time_us = ktime_to_us(
						ktime_sub(ts->busy_to_idle_ts,
						ts->idle_to_busy_ts));
			total_busy_time_us += last_busy_time_us -
					(last_start_us - period_us);
			break;
		}

		/* Check if the sample is finished or still open */
		if (ktime_compare(ts->busy_to_idle_ts, zero_ktime))
			last_busy_time_us = ktime_to_us(
						ktime_sub(ts->busy_to_idle_ts,
						ts->idle_to_busy_ts));
		else
			last_busy_time_us = ktime_to_us(
					ktime_sub(curr, ts->idle_to_busy_ts));

		total_busy_time_us += last_busy_time_us;

		last_index--;
		/* Handle case idle_busy_ts_idx was 0 */
		if (last_index > HL_IDLE_BUSY_TS_ARR_SIZE)
			last_index = HL_IDLE_BUSY_TS_ARR_SIZE - 1;

		ts = &hdev->idle_busy_ts_arr[last_index];

		overlap_cnt++;
	}

	total_busy_time_ms = DIV_ROUND_UP_ULL(total_busy_time_us,
						USEC_PER_MSEC);

	return DIV_ROUND_UP_ULL(total_busy_time_ms * 100, period_ms);
}

/*
 * hl_device_set_frequency - set the frequency of the device
 *
 * @hdev: pointer to habanalabs device structure
 * @freq: the new frequency value
 *
 * Change the frequency if needed. This function has no protection against
 * concurrency, therefore it is assumed that the calling function has protected
 * itself against the case of calling this function from multiple threads with
 * different values
 *
 * Returns 0 if no change was done, otherwise returns 1
 */
int hl_device_set_frequency(struct hl_device *hdev, enum hl_pll_frequency freq)
{
	if ((hdev->pm_mng_profile == PM_MANUAL) ||
			(hdev->curr_pll_profile == freq))
		return 0;

	dev_dbg(hdev->dev, "Changing device frequency to %s\n",
		freq == PLL_HIGH ? "high" : "low");

	hdev->asic_funcs->set_pll_profile(hdev, freq);

	hdev->curr_pll_profile = freq;

	return 1;
}

int hl_device_set_debug_mode(struct hl_device *hdev, bool enable)
{
	int rc = 0;

	mutex_lock(&hdev->debug_lock);

	if (!enable) {
		if (!hdev->in_debug) {
			dev_err(hdev->dev,
				"Failed to disable debug mode because device was not in debug mode\n");
			rc = -EFAULT;
			goto out;
		}

		if (!hdev->hard_reset_pending)
			hdev->asic_funcs->halt_coresight(hdev);

		hdev->in_debug = 0;

		goto out;
	}

	if (hdev->in_debug) {
		dev_err(hdev->dev,
			"Failed to enable debug mode because device is already in debug mode\n");
		rc = -EFAULT;
		goto out;
	}

	hdev->in_debug = 1;

out:
	mutex_unlock(&hdev->debug_lock);

	return rc;
}

/*
 * hl_device_suspend - initiate device suspend
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Puts the hw in the suspend state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver suspend.
 */
int hl_device_suspend(struct hl_device *hdev)
{
	int rc;

	pci_save_state(hdev->pdev);

	/* Block future CS/VM/JOB completion operations */
	rc = atomic_cmpxchg(&hdev->in_reset, 0, 1);
	if (rc) {
		dev_err(hdev->dev, "Can't suspend while in reset\n");
		return -EIO;
	}

	/* This blocks all other stuff that is not blocked by in_reset */
	hdev->disabled = true;

	/*
	 * Flush anyone that is inside the critical section of enqueue
	 * jobs to the H/W
	 */
	hdev->asic_funcs->hw_queues_lock(hdev);
	hdev->asic_funcs->hw_queues_unlock(hdev);

	/* Flush processes that are sending message to CPU */
	mutex_lock(&hdev->send_cpu_message_lock);
	mutex_unlock(&hdev->send_cpu_message_lock);

	rc = hdev->asic_funcs->suspend(hdev);
	if (rc)
		dev_err(hdev->dev,
			"Failed to disable PCI access of device CPU\n");

	/* Shut down the device */
	pci_disable_device(hdev->pdev);
	pci_set_power_state(hdev->pdev, PCI_D3hot);

	return 0;
}

/*
 * hl_device_resume - initiate device resume
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Bring the hw back to operating state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver resume.
 */
int hl_device_resume(struct hl_device *hdev)
{
	int rc;

	pci_set_power_state(hdev->pdev, PCI_D0);
	pci_restore_state(hdev->pdev);
	rc = pci_enable_device_mem(hdev->pdev);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to enable PCI device in resume\n");
		return rc;
	}

	pci_set_master(hdev->pdev);

	rc = hdev->asic_funcs->resume(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to resume device after suspend\n");
		goto disable_device;
	}


	hdev->disabled = false;
	atomic_set(&hdev->in_reset, 0);

	rc = hl_device_reset(hdev, true, false);
	if (rc) {
		dev_err(hdev->dev, "Failed to reset device during resume\n");
		goto disable_device;
	}

	return 0;

disable_device:
	pci_clear_master(hdev->pdev);
	pci_disable_device(hdev->pdev);

	return rc;
}

static void device_kill_open_processes(struct hl_device *hdev)
{
	u16 pending_total, pending_cnt;
	struct hl_fpriv	*hpriv;
	struct task_struct *task = NULL;

	if (hdev->pldm)
		pending_total = HL_PLDM_PENDING_RESET_PER_SEC;
	else
		pending_total = HL_PENDING_RESET_PER_SEC;

	/* Giving time for user to close FD, and for processes that are inside
	 * hl_device_open to finish
	 */
	if (!list_empty(&hdev->fpriv_list))
		ssleep(1);

	mutex_lock(&hdev->fpriv_list_lock);

	/* This section must be protected because we are dereferencing
	 * pointers that are freed if the process exits
	 */
	list_for_each_entry(hpriv, &hdev->fpriv_list, dev_node) {
		task = get_pid_task(hpriv->taskpid, PIDTYPE_PID);
		if (task) {
			dev_info(hdev->dev, "Killing user process pid=%d\n",
				task_pid_nr(task));
			send_sig(SIGKILL, task, 1);
			usleep_range(1000, 10000);

			put_task_struct(task);
		}
	}

	mutex_unlock(&hdev->fpriv_list_lock);

	/* We killed the open users, but because the driver cleans up after the
	 * user contexts are closed (e.g. mmu mappings), we need to wait again
	 * to make sure the cleaning phase is finished before continuing with
	 * the reset
	 */

	pending_cnt = pending_total;

	while ((!list_empty(&hdev->fpriv_list)) && (pending_cnt)) {
		dev_info(hdev->dev,
			"Waiting for all unmap operations to finish before hard reset\n");

		pending_cnt--;

		ssleep(1);
	}

	if (!list_empty(&hdev->fpriv_list))
		dev_crit(hdev->dev,
			"Going to hard reset with open user contexts\n");
}

static void device_hard_reset_pending(struct work_struct *work)
{
	struct hl_device_reset_work *device_reset_work =
		container_of(work, struct hl_device_reset_work, reset_work);
	struct hl_device *hdev = device_reset_work->hdev;

	hl_device_reset(hdev, true, true);

	kfree(device_reset_work);
}

/*
 * hl_device_reset - reset the device
 *
 * @hdev: pointer to habanalabs device structure
 * @hard_reset: should we do hard reset to all engines or just reset the
 *              compute/dma engines
 *
 * Block future CS and wait for pending CS to be enqueued
 * Call ASIC H/W fini
 * Flush all completions
 * Re-initialize all internal data structures
 * Call ASIC H/W init, late_init
 * Test queues
 * Enable device
 *
 * Returns 0 for success or an error on failure.
 */
int hl_device_reset(struct hl_device *hdev, bool hard_reset,
			bool from_hard_reset_thread)
{
	int i, rc;

	if (!hdev->init_done) {
		dev_err(hdev->dev,
			"Can't reset before initialization is done\n");
		return 0;
	}

	/*
	 * Prevent concurrency in this function - only one reset should be
	 * done at any given time. Only need to perform this if we didn't
	 * get from the dedicated hard reset thread
	 */
	if (!from_hard_reset_thread) {
		/* Block future CS/VM/JOB completion operations */
		rc = atomic_cmpxchg(&hdev->in_reset, 0, 1);
		if (rc)
			return 0;

		/* This also blocks future CS/VM/JOB completion operations */
		hdev->disabled = true;

		/* Flush anyone that is inside the critical section of enqueue
		 * jobs to the H/W
		 */
		hdev->asic_funcs->hw_queues_lock(hdev);
		hdev->asic_funcs->hw_queues_unlock(hdev);

		/* Flush anyone that is inside device open */
		mutex_lock(&hdev->fpriv_list_lock);
		mutex_unlock(&hdev->fpriv_list_lock);

		dev_err(hdev->dev, "Going to RESET device!\n");
	}

again:
	if ((hard_reset) && (!from_hard_reset_thread)) {
		struct hl_device_reset_work *device_reset_work;

		hdev->hard_reset_pending = true;

		device_reset_work = kzalloc(sizeof(*device_reset_work),
						GFP_ATOMIC);
		if (!device_reset_work) {
			rc = -ENOMEM;
			goto out_err;
		}

		/*
		 * Because the reset function can't run from interrupt or
		 * from heartbeat work, we need to call the reset function
		 * from a dedicated work
		 */
		INIT_WORK(&device_reset_work->reset_work,
				device_hard_reset_pending);
		device_reset_work->hdev = hdev;
		schedule_work(&device_reset_work->reset_work);

		return 0;
	}

	if (hard_reset) {
		device_late_fini(hdev);

		/*
		 * Now that the heartbeat thread is closed, flush processes
		 * which are sending messages to CPU
		 */
		mutex_lock(&hdev->send_cpu_message_lock);
		mutex_unlock(&hdev->send_cpu_message_lock);
	}

	/*
	 * Halt the engines and disable interrupts so we won't get any more
	 * completions from H/W and we won't have any accesses from the
	 * H/W to the host machine
	 */
	hdev->asic_funcs->halt_engines(hdev, hard_reset);

	/* Go over all the queues, release all CS and their jobs */
	hl_cs_rollback_all(hdev);

	if (hard_reset) {
		/* Kill processes here after CS rollback. This is because the
		 * process can't really exit until all its CSs are done, which
		 * is what we do in cs rollback
		 */
		device_kill_open_processes(hdev);

		/* Flush the Event queue workers to make sure no other thread is
		 * reading or writing to registers during the reset
		 */
		flush_workqueue(hdev->eq_wq);
	}

	/* Release kernel context */
	if ((hard_reset) && (hl_ctx_put(hdev->kernel_ctx) == 1))
		hdev->kernel_ctx = NULL;

	/* Reset the H/W. It will be in idle state after this returns */
	hdev->asic_funcs->hw_fini(hdev, hard_reset);

	if (hard_reset) {
		hl_vm_fini(hdev);
		hl_mmu_fini(hdev);
		hl_eq_reset(hdev, &hdev->event_queue);
	}

	/* Re-initialize PI,CI to 0 in all queues (hw queue, cq) */
	hl_hw_queue_reset(hdev, hard_reset);
	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		hl_cq_reset(hdev, &hdev->completion_queue[i]);

	hdev->idle_busy_ts_idx = 0;
	hdev->idle_busy_ts_arr[0].busy_to_idle_ts = ktime_set(0, 0);
	hdev->idle_busy_ts_arr[0].idle_to_busy_ts = ktime_set(0, 0);

	if (hdev->cs_active_cnt)
		dev_crit(hdev->dev, "CS active cnt %d is not 0 during reset\n",
			hdev->cs_active_cnt);

	mutex_lock(&hdev->fpriv_list_lock);

	/* Make sure the context switch phase will run again */
	if (hdev->compute_ctx) {
		atomic_set(&hdev->compute_ctx->thread_ctx_switch_token, 1);
		hdev->compute_ctx->thread_ctx_switch_wait_token = 0;
	}

	mutex_unlock(&hdev->fpriv_list_lock);

	/* Finished tear-down, starting to re-initialize */

	if (hard_reset) {
		hdev->device_cpu_disabled = false;
		hdev->hard_reset_pending = false;

		if (hdev->kernel_ctx) {
			dev_crit(hdev->dev,
				"kernel ctx was alive during hard reset, something is terribly wrong\n");
			rc = -EBUSY;
			goto out_err;
		}

		rc = hl_mmu_init(hdev);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to initialize MMU S/W after hard reset\n");
			goto out_err;
		}

		/* Allocate the kernel context */
		hdev->kernel_ctx = kzalloc(sizeof(*hdev->kernel_ctx),
						GFP_KERNEL);
		if (!hdev->kernel_ctx) {
			rc = -ENOMEM;
			goto out_err;
		}

		hdev->compute_ctx = NULL;

		rc = hl_ctx_init(hdev, hdev->kernel_ctx, true);
		if (rc) {
			dev_err(hdev->dev,
				"failed to init kernel ctx in hard reset\n");
			kfree(hdev->kernel_ctx);
			hdev->kernel_ctx = NULL;
			goto out_err;
		}
	}

	rc = hdev->asic_funcs->hw_init(hdev);
	if (rc) {
		dev_err(hdev->dev,
			"failed to initialize the H/W after reset\n");
		goto out_err;
	}

	hdev->disabled = false;

	/* Check that the communication with the device is working */
	rc = hdev->asic_funcs->test_queues(hdev);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to detect if device is alive after reset\n");
		goto out_err;
	}

	if (hard_reset) {
		rc = device_late_init(hdev);
		if (rc) {
			dev_err(hdev->dev,
				"Failed late init after hard reset\n");
			goto out_err;
		}

		rc = hl_vm_init(hdev);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to init memory module after hard reset\n");
			goto out_err;
		}

		hl_set_max_power(hdev, hdev->max_power);
	} else {
		rc = hdev->asic_funcs->soft_reset_late_init(hdev);
		if (rc) {
			dev_err(hdev->dev,
				"Failed late init after soft reset\n");
			goto out_err;
		}
	}

	atomic_set(&hdev->in_reset, 0);

	if (hard_reset)
		hdev->hard_reset_cnt++;
	else
		hdev->soft_reset_cnt++;

	dev_warn(hdev->dev, "Successfully finished resetting the device\n");

	return 0;

out_err:
	hdev->disabled = true;

	if (hard_reset) {
		dev_err(hdev->dev,
			"Failed to reset! Device is NOT usable\n");
		hdev->hard_reset_cnt++;
	} else {
		dev_err(hdev->dev,
			"Failed to do soft-reset, trying hard reset\n");
		hdev->soft_reset_cnt++;
		hard_reset = true;
		goto again;
	}

	atomic_set(&hdev->in_reset, 0);

	return rc;
}

/*
 * hl_device_init - main initialization function for habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Allocate an id for the device, do early initialization and then call the
 * ASIC specific initialization functions. Finally, create the cdev and the
 * Linux device to expose it to the user
 */
int hl_device_init(struct hl_device *hdev, struct class *hclass)
{
	int i, rc, cq_ready_cnt;
	char *name;
	bool add_cdev_sysfs_on_err = false;

	name = kasprintf(GFP_KERNEL, "hl%d", hdev->id / 2);
	if (!name) {
		rc = -ENOMEM;
		goto out_disabled;
	}

	/* Initialize cdev and device structures */
	rc = device_init_cdev(hdev, hclass, hdev->id, &hl_ops, name,
				&hdev->cdev, &hdev->dev);

	kfree(name);

	if (rc)
		goto out_disabled;

	name = kasprintf(GFP_KERNEL, "hl_controlD%d", hdev->id / 2);
	if (!name) {
		rc = -ENOMEM;
		goto free_dev;
	}

	/* Initialize cdev and device structures for control device */
	rc = device_init_cdev(hdev, hclass, hdev->id_control, &hl_ctrl_ops,
				name, &hdev->cdev_ctrl, &hdev->dev_ctrl);

	kfree(name);

	if (rc)
		goto free_dev;

	/* Initialize ASIC function pointers and perform early init */
	rc = device_early_init(hdev);
	if (rc)
		goto free_dev_ctrl;

	/*
	 * Start calling ASIC initialization. First S/W then H/W and finally
	 * late init
	 */
	rc = hdev->asic_funcs->sw_init(hdev);
	if (rc)
		goto early_fini;

	/*
	 * Initialize the H/W queues. Must be done before hw_init, because
	 * there the addresses of the kernel queue are being written to the
	 * registers of the device
	 */
	rc = hl_hw_queues_create(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize kernel queues\n");
		goto sw_fini;
	}

	/*
	 * Initialize the completion queues. Must be done before hw_init,
	 * because there the addresses of the completion queues are being
	 * passed as arguments to request_irq
	 */
	hdev->completion_queue =
			kcalloc(hdev->asic_prop.completion_queues_count,
				sizeof(*hdev->completion_queue), GFP_KERNEL);

	if (!hdev->completion_queue) {
		dev_err(hdev->dev, "failed to allocate completion queues\n");
		rc = -ENOMEM;
		goto hw_queues_destroy;
	}

	for (i = 0, cq_ready_cnt = 0;
			i < hdev->asic_prop.completion_queues_count;
			i++, cq_ready_cnt++) {
		rc = hl_cq_init(hdev, &hdev->completion_queue[i], i);
		if (rc) {
			dev_err(hdev->dev,
				"failed to initialize completion queue\n");
			goto cq_fini;
		}
	}

	/*
	 * Initialize the event queue. Must be done before hw_init,
	 * because there the address of the event queue is being
	 * passed as argument to request_irq
	 */
	rc = hl_eq_init(hdev, &hdev->event_queue);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize event queue\n");
		goto cq_fini;
	}

	/* MMU S/W must be initialized before kernel context is created */
	rc = hl_mmu_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize MMU S/W structures\n");
		goto eq_fini;
	}

	/* Allocate the kernel context */
	hdev->kernel_ctx = kzalloc(sizeof(*hdev->kernel_ctx), GFP_KERNEL);
	if (!hdev->kernel_ctx) {
		rc = -ENOMEM;
		goto mmu_fini;
	}

	hdev->compute_ctx = NULL;

	rc = hl_ctx_init(hdev, hdev->kernel_ctx, true);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize kernel context\n");
		kfree(hdev->kernel_ctx);
		goto mmu_fini;
	}

	rc = hl_cb_pool_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CB pool\n");
		goto release_ctx;
	}

	hl_debugfs_add_device(hdev);

	if (hdev->asic_funcs->get_hw_state(hdev) == HL_DEVICE_HW_STATE_DIRTY) {
		dev_info(hdev->dev,
			"H/W state is dirty, must reset before initializing\n");
		hdev->asic_funcs->halt_engines(hdev, true);
		hdev->asic_funcs->hw_fini(hdev, true);
	}

	/*
	 * From this point, in case of an error, add char devices and create
	 * sysfs nodes as part of the error flow, to allow debugging.
	 */
	add_cdev_sysfs_on_err = true;

	rc = hdev->asic_funcs->hw_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize the H/W\n");
		rc = 0;
		goto out_disabled;
	}

	hdev->disabled = false;

	/* Check that the communication with the device is working */
	rc = hdev->asic_funcs->test_queues(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to detect if device is alive\n");
		rc = 0;
		goto out_disabled;
	}

	rc = device_late_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed late initialization\n");
		rc = 0;
		goto out_disabled;
	}

	dev_info(hdev->dev, "Found %s device with %lluGB DRAM\n",
		hdev->asic_name,
		hdev->asic_prop.dram_size / 1024 / 1024 / 1024);

	rc = hl_vm_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize memory module\n");
		rc = 0;
		goto out_disabled;
	}

	/*
	 * Expose devices and sysfs nodes to user.
	 * From here there is no need to add char devices and create sysfs nodes
	 * in case of an error.
	 */
	add_cdev_sysfs_on_err = false;
	rc = device_cdev_sysfs_add(hdev);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to add char devices and sysfs nodes\n");
		rc = 0;
		goto out_disabled;
	}

	/*
	 * hl_hwmon_init() must be called after device_late_init(), because only
	 * there we get the information from the device about which
	 * hwmon-related sensors the device supports.
	 * Furthermore, it must be done after adding the device to the system.
	 */
	rc = hl_hwmon_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize hwmon\n");
		rc = 0;
		goto out_disabled;
	}

	dev_notice(hdev->dev,
		"Successfully added device to habanalabs driver\n");

	hdev->init_done = true;

	return 0;

release_ctx:
	if (hl_ctx_put(hdev->kernel_ctx) != 1)
		dev_err(hdev->dev,
			"kernel ctx is still alive on initialization failure\n");
mmu_fini:
	hl_mmu_fini(hdev);
eq_fini:
	hl_eq_fini(hdev, &hdev->event_queue);
cq_fini:
	for (i = 0 ; i < cq_ready_cnt ; i++)
		hl_cq_fini(hdev, &hdev->completion_queue[i]);
	kfree(hdev->completion_queue);
hw_queues_destroy:
	hl_hw_queues_destroy(hdev);
sw_fini:
	hdev->asic_funcs->sw_fini(hdev);
early_fini:
	device_early_fini(hdev);
free_dev_ctrl:
	kfree(hdev->dev_ctrl);
free_dev:
	kfree(hdev->dev);
out_disabled:
	hdev->disabled = true;
	if (add_cdev_sysfs_on_err)
		device_cdev_sysfs_add(hdev);
	if (hdev->pdev)
		dev_err(&hdev->pdev->dev,
			"Failed to initialize hl%d. Device is NOT usable !\n",
			hdev->id / 2);
	else
		pr_err("Failed to initialize hl%d. Device is NOT usable !\n",
			hdev->id / 2);

	return rc;
}

/*
 * hl_device_fini - main tear-down function for habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Destroy the device, call ASIC fini functions and release the id
 */
void hl_device_fini(struct hl_device *hdev)
{
	int i, rc;
	ktime_t timeout;

	dev_info(hdev->dev, "Removing device\n");

	/*
	 * This function is competing with the reset function, so try to
	 * take the reset atomic and if we are already in middle of reset,
	 * wait until reset function is finished. Reset function is designed
	 * to always finish (could take up to a few seconds in worst case).
	 */

	timeout = ktime_add_us(ktime_get(),
				HL_PENDING_RESET_PER_SEC * 1000 * 1000 * 4);
	rc = atomic_cmpxchg(&hdev->in_reset, 0, 1);
	while (rc) {
		usleep_range(50, 200);
		rc = atomic_cmpxchg(&hdev->in_reset, 0, 1);
		if (ktime_compare(ktime_get(), timeout) > 0) {
			WARN(1, "Failed to remove device because reset function did not finish\n");
			return;
		}
	}

	/* Mark device as disabled */
	hdev->disabled = true;

	/* Flush anyone that is inside the critical section of enqueue
	 * jobs to the H/W
	 */
	hdev->asic_funcs->hw_queues_lock(hdev);
	hdev->asic_funcs->hw_queues_unlock(hdev);

	/* Flush anyone that is inside device open */
	mutex_lock(&hdev->fpriv_list_lock);
	mutex_unlock(&hdev->fpriv_list_lock);

	hdev->hard_reset_pending = true;

	hl_hwmon_fini(hdev);

	device_late_fini(hdev);

	hl_debugfs_remove_device(hdev);

	/*
	 * Halt the engines and disable interrupts so we won't get any more
	 * completions from H/W and we won't have any accesses from the
	 * H/W to the host machine
	 */
	hdev->asic_funcs->halt_engines(hdev, true);

	/* Go over all the queues, release all CS and their jobs */
	hl_cs_rollback_all(hdev);

	/* Kill processes here after CS rollback. This is because the process
	 * can't really exit until all its CSs are done, which is what we
	 * do in cs rollback
	 */
	device_kill_open_processes(hdev);

	hl_cb_pool_fini(hdev);

	/* Release kernel context */
	if ((hdev->kernel_ctx) && (hl_ctx_put(hdev->kernel_ctx) != 1))
		dev_err(hdev->dev, "kernel ctx is still alive\n");

	/* Reset the H/W. It will be in idle state after this returns */
	hdev->asic_funcs->hw_fini(hdev, true);

	hl_vm_fini(hdev);

	hl_mmu_fini(hdev);

	hl_eq_fini(hdev, &hdev->event_queue);

	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		hl_cq_fini(hdev, &hdev->completion_queue[i]);
	kfree(hdev->completion_queue);

	hl_hw_queues_destroy(hdev);

	/* Call ASIC S/W finalize function */
	hdev->asic_funcs->sw_fini(hdev);

	device_early_fini(hdev);

	/* Hide devices and sysfs nodes from user */
	device_cdev_sysfs_del(hdev);

	pr_info("removed device successfully\n");
}

/*
 * MMIO register access helper functions.
 */

/*
 * hl_rreg - Read an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 *
 * Returns the value of the MMIO register we are asked to read
 *
 */
inline u32 hl_rreg(struct hl_device *hdev, u32 reg)
{
	return readl(hdev->rmmio + reg);
}

/*
 * hl_wreg - Write to an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 * @val: 32-bit value
 *
 * Writes the 32-bit value into the MMIO register
 *
 */
inline void hl_wreg(struct hl_device *hdev, u32 reg, u32 val)
{
	writel(val, hdev->rmmio + reg);
}
