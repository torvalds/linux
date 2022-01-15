// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)			"habanalabs: " fmt

#include <uapi/misc/habanalabs.h>
#include "habanalabs.h"

#include <linux/pci.h>
#include <linux/hwmon.h>

enum hl_device_status hl_device_status(struct hl_device *hdev)
{
	enum hl_device_status status;

	if (atomic_read(&hdev->in_reset))
		status = HL_DEVICE_STATUS_IN_RESET;
	else if (hdev->needs_reset)
		status = HL_DEVICE_STATUS_NEEDS_RESET;
	else if (hdev->disabled)
		status = HL_DEVICE_STATUS_MALFUNCTION;
	else if (!hdev->init_done)
		status = HL_DEVICE_STATUS_IN_DEVICE_CREATION;
	else
		status = HL_DEVICE_STATUS_OPERATIONAL;

	return status;
}

bool hl_device_operational(struct hl_device *hdev,
		enum hl_device_status *status)
{
	enum hl_device_status current_status;

	current_status = hl_device_status(hdev);
	if (status)
		*status = current_status;

	switch (current_status) {
	case HL_DEVICE_STATUS_IN_RESET:
	case HL_DEVICE_STATUS_MALFUNCTION:
	case HL_DEVICE_STATUS_NEEDS_RESET:
		return false;
	case HL_DEVICE_STATUS_OPERATIONAL:
	case HL_DEVICE_STATUS_IN_DEVICE_CREATION:
	default:
		return true;
	}
}

static void hpriv_release(struct kref *ref)
{
	u64 idle_mask[HL_BUSY_ENGINES_MASK_EXT_SIZE] = {0};
	bool device_is_idle = true;
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

	if ((!hdev->pldm) && (hdev->pdev) &&
			(!hdev->asic_funcs->is_device_idle(hdev,
				idle_mask,
				HL_BUSY_ENGINES_MASK_EXT_SIZE, NULL))) {
		dev_err(hdev->dev,
			"device not idle after user context is closed (0x%llx_%llx)\n",
			idle_mask[1], idle_mask[0]);

		device_is_idle = false;
	}

	if ((hdev->reset_if_device_not_idle && !device_is_idle)
			|| hdev->reset_upon_device_release)
		hl_device_reset(hdev, HL_RESET_DEVICE_RELEASE);
}

void hl_hpriv_get(struct hl_fpriv *hpriv)
{
	kref_get(&hpriv->refcount);
}

int hl_hpriv_put(struct hl_fpriv *hpriv)
{
	return kref_put(&hpriv->refcount, hpriv_release);
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
	struct hl_device *hdev = hpriv->hdev;

	filp->private_data = NULL;

	if (!hdev) {
		pr_crit("Closing FD after device was removed. Memory leak will occur and it is advised to reboot.\n");
		put_pid(hpriv->taskpid);
		return 0;
	}

	/* Each pending user interrupt holds the user's context, hence we
	 * must release them all before calling hl_ctx_mgr_fini().
	 */
	hl_release_pending_user_interrupts(hpriv->hdev);

	hl_cb_mgr_fini(hdev, &hpriv->cb_mgr);
	hl_ctx_mgr_fini(hdev, &hpriv->ctx_mgr);

	if (!hl_hpriv_put(hpriv))
		dev_notice(hdev->dev,
			"User process closed FD but device still in use\n");

	hdev->last_open_session_duration_jif =
		jiffies - hdev->last_successful_open_jif;

	return 0;
}

static int hl_device_release_ctrl(struct inode *inode, struct file *filp)
{
	struct hl_fpriv *hpriv = filp->private_data;
	struct hl_device *hdev = hpriv->hdev;

	filp->private_data = NULL;

	if (!hdev) {
		pr_err("Closing FD after device was removed\n");
		goto out;
	}

	mutex_lock(&hdev->fpriv_list_lock);
	list_del(&hpriv->dev_node);
	mutex_unlock(&hdev->fpriv_list_lock);
out:
	put_pid(hpriv->taskpid);

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
	struct hl_device *hdev = hpriv->hdev;
	unsigned long vm_pgoff;

	if (!hdev) {
		pr_err_ratelimited("Trying to mmap after device was removed! Please close FD\n");
		return -ENODEV;
	}

	vm_pgoff = vma->vm_pgoff;
	vma->vm_pgoff = HL_MMAP_OFFSET_VALUE_GET(vm_pgoff);

	switch (vm_pgoff & HL_MMAP_TYPE_MASK) {
	case HL_MMAP_TYPE_CB:
		return hl_cb_mmap(hpriv, vma);

	case HL_MMAP_TYPE_BLOCK:
		return hl_hw_block_mmap(hpriv, vma);
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
	if (!hdev->cdev_sysfs_created)
		goto put_devices;

	hl_sysfs_fini(hdev);
	cdev_device_del(&hdev->cdev_ctrl, hdev->dev_ctrl);
	cdev_device_del(&hdev->cdev, hdev->dev);

put_devices:
	put_device(hdev->dev);
	put_device(hdev->dev_ctrl);
}

static void device_hard_reset_pending(struct work_struct *work)
{
	struct hl_device_reset_work *device_reset_work =
		container_of(work, struct hl_device_reset_work,
				reset_work.work);
	struct hl_device *hdev = device_reset_work->hdev;
	u32 flags;
	int rc;

	flags = HL_RESET_HARD | HL_RESET_FROM_RESET_THREAD;

	if (device_reset_work->fw_reset)
		flags |= HL_RESET_FW;

	rc = hl_device_reset(hdev, flags);
	if ((rc == -EBUSY) && !hdev->device_fini_pending) {
		dev_info(hdev->dev,
			"Could not reset device. will try again in %u seconds",
			HL_PENDING_RESET_PER_SEC);

		queue_delayed_work(device_reset_work->wq,
			&device_reset_work->reset_work,
			msecs_to_jiffies(HL_PENDING_RESET_PER_SEC * 1000));
	}
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
	int i, rc;
	char workq_name[32];

	switch (hdev->asic_type) {
	case ASIC_GOYA:
		goya_set_asic_funcs(hdev);
		strscpy(hdev->asic_name, "GOYA", sizeof(hdev->asic_name));
		break;
	case ASIC_GAUDI:
		gaudi_set_asic_funcs(hdev);
		strscpy(hdev->asic_name, "GAUDI", sizeof(hdev->asic_name));
		break;
	case ASIC_GAUDI_SEC:
		gaudi_set_asic_funcs(hdev);
		strscpy(hdev->asic_name, "GAUDI SEC", sizeof(hdev->asic_name));
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

	if (hdev->asic_prop.completion_queues_count) {
		hdev->cq_wq = kcalloc(hdev->asic_prop.completion_queues_count,
				sizeof(*hdev->cq_wq),
				GFP_KERNEL);
		if (!hdev->cq_wq) {
			rc = -ENOMEM;
			goto asid_fini;
		}
	}

	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++) {
		snprintf(workq_name, 32, "hl-free-jobs-%u", (u32) i);
		hdev->cq_wq[i] = create_singlethread_workqueue(workq_name);
		if (hdev->cq_wq[i] == NULL) {
			dev_err(hdev->dev, "Failed to allocate CQ workqueue\n");
			rc = -ENOMEM;
			goto free_cq_wq;
		}
	}

	hdev->eq_wq = alloc_workqueue("hl-events", WQ_UNBOUND, 0);
	if (hdev->eq_wq == NULL) {
		dev_err(hdev->dev, "Failed to allocate EQ workqueue\n");
		rc = -ENOMEM;
		goto free_cq_wq;
	}

	hdev->sob_reset_wq = alloc_workqueue("hl-sob-reset", WQ_UNBOUND, 0);
	if (!hdev->sob_reset_wq) {
		dev_err(hdev->dev,
			"Failed to allocate SOB reset workqueue\n");
		rc = -ENOMEM;
		goto free_eq_wq;
	}

	hdev->hl_chip_info = kzalloc(sizeof(struct hwmon_chip_info),
					GFP_KERNEL);
	if (!hdev->hl_chip_info) {
		rc = -ENOMEM;
		goto free_sob_reset_wq;
	}

	rc = hl_mmu_if_set_funcs(hdev);
	if (rc)
		goto free_chip_info;

	hl_cb_mgr_init(&hdev->kernel_cb_mgr);

	hdev->device_reset_work.wq =
			create_singlethread_workqueue("hl_device_reset");
	if (!hdev->device_reset_work.wq) {
		rc = -ENOMEM;
		dev_err(hdev->dev, "Failed to create device reset WQ\n");
		goto free_cb_mgr;
	}

	INIT_DELAYED_WORK(&hdev->device_reset_work.reset_work,
			device_hard_reset_pending);
	hdev->device_reset_work.hdev = hdev;
	hdev->device_fini_pending = 0;

	mutex_init(&hdev->send_cpu_message_lock);
	mutex_init(&hdev->debug_lock);
	INIT_LIST_HEAD(&hdev->cs_mirror_list);
	spin_lock_init(&hdev->cs_mirror_lock);
	INIT_LIST_HEAD(&hdev->fpriv_list);
	mutex_init(&hdev->fpriv_list_lock);
	atomic_set(&hdev->in_reset, 0);

	return 0;

free_cb_mgr:
	hl_cb_mgr_fini(hdev, &hdev->kernel_cb_mgr);
free_chip_info:
	kfree(hdev->hl_chip_info);
free_sob_reset_wq:
	destroy_workqueue(hdev->sob_reset_wq);
free_eq_wq:
	destroy_workqueue(hdev->eq_wq);
free_cq_wq:
	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		if (hdev->cq_wq[i])
			destroy_workqueue(hdev->cq_wq[i]);
	kfree(hdev->cq_wq);
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
	int i;

	mutex_destroy(&hdev->debug_lock);
	mutex_destroy(&hdev->send_cpu_message_lock);

	mutex_destroy(&hdev->fpriv_list_lock);

	hl_cb_mgr_fini(hdev, &hdev->kernel_cb_mgr);

	kfree(hdev->hl_chip_info);

	destroy_workqueue(hdev->sob_reset_wq);
	destroy_workqueue(hdev->eq_wq);
	destroy_workqueue(hdev->device_reset_work.wq);

	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		destroy_workqueue(hdev->cq_wq[i]);
	kfree(hdev->cq_wq);

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

	if (!hl_device_operational(hdev, NULL))
		goto reschedule;

	if (!hdev->asic_funcs->send_heartbeat(hdev))
		goto reschedule;

	dev_err(hdev->dev, "Device heartbeat failed!\n");
	hl_device_reset(hdev, HL_RESET_HARD | HL_RESET_HEARTBEAT);

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

int hl_device_utilization(struct hl_device *hdev, u32 *utilization)
{
	u64 max_power, curr_power, dc_power, dividend;
	int rc;

	max_power = hdev->asic_prop.max_power_default;
	dc_power = hdev->asic_prop.dc_power_default;
	rc = hl_fw_cpucp_power_get(hdev, &curr_power);

	if (rc)
		return rc;

	curr_power = clamp(curr_power, dc_power, max_power);

	dividend = (curr_power - dc_power) * 100;
	*utilization = (u32) div_u64(dividend, (max_power - dc_power));

	return 0;
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

		if (!hdev->hard_reset_pending)
			hdev->asic_funcs->set_clock_gating(hdev);

		goto out;
	}

	if (hdev->in_debug) {
		dev_err(hdev->dev,
			"Failed to enable debug mode because device is already in debug mode\n");
		rc = -EFAULT;
		goto out;
	}

	hdev->asic_funcs->disable_clock_gating(hdev);
	hdev->in_debug = 1;

out:
	mutex_unlock(&hdev->debug_lock);

	return rc;
}

static void take_release_locks(struct hl_device *hdev)
{
	/* Flush anyone that is inside the critical section of enqueue
	 * jobs to the H/W
	 */
	hdev->asic_funcs->hw_queues_lock(hdev);
	hdev->asic_funcs->hw_queues_unlock(hdev);

	/* Flush processes that are sending message to CPU */
	mutex_lock(&hdev->send_cpu_message_lock);
	mutex_unlock(&hdev->send_cpu_message_lock);

	/* Flush anyone that is inside device open */
	mutex_lock(&hdev->fpriv_list_lock);
	mutex_unlock(&hdev->fpriv_list_lock);
}

static void cleanup_resources(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	if (hard_reset)
		device_late_fini(hdev);

	/*
	 * Halt the engines and disable interrupts so we won't get any more
	 * completions from H/W and we won't have any accesses from the
	 * H/W to the host machine
	 */
	hdev->asic_funcs->halt_engines(hdev, hard_reset, fw_reset);

	/* Go over all the queues, release all CS and their jobs */
	hl_cs_rollback_all(hdev);

	/* Release all pending user interrupts, each pending user interrupt
	 * holds a reference to user context
	 */
	hl_release_pending_user_interrupts(hdev);
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

	take_release_locks(hdev);

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

	rc = hl_device_reset(hdev, HL_RESET_HARD);
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

static int device_kill_open_processes(struct hl_device *hdev, u32 timeout)
{
	struct hl_fpriv	*hpriv;
	struct task_struct *task = NULL;
	u32 pending_cnt;


	/* Giving time for user to close FD, and for processes that are inside
	 * hl_device_open to finish
	 */
	if (!list_empty(&hdev->fpriv_list))
		ssleep(1);

	if (timeout) {
		pending_cnt = timeout;
	} else {
		if (hdev->process_kill_trial_cnt) {
			/* Processes have been already killed */
			pending_cnt = 1;
			goto wait_for_processes;
		} else {
			/* Wait a small period after process kill */
			pending_cnt = HL_PENDING_RESET_PER_SEC;
		}
	}

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
		} else {
			dev_warn(hdev->dev,
				"Can't get task struct for PID so giving up on killing process\n");
			mutex_unlock(&hdev->fpriv_list_lock);
			return -ETIME;
		}
	}

	mutex_unlock(&hdev->fpriv_list_lock);

	/*
	 * We killed the open users, but that doesn't mean they are closed.
	 * It could be that they are running a long cleanup phase in the driver
	 * e.g. MMU unmappings, or running other long teardown flow even before
	 * our cleanup.
	 * Therefore we need to wait again to make sure they are closed before
	 * continuing with the reset.
	 */

wait_for_processes:
	while ((!list_empty(&hdev->fpriv_list)) && (pending_cnt)) {
		dev_dbg(hdev->dev,
			"Waiting for all unmap operations to finish before hard reset\n");

		pending_cnt--;

		ssleep(1);
	}

	/* All processes exited successfully */
	if (list_empty(&hdev->fpriv_list))
		return 0;

	/* Give up waiting for processes to exit */
	if (hdev->process_kill_trial_cnt == HL_PENDING_RESET_MAX_TRIALS)
		return -ETIME;

	hdev->process_kill_trial_cnt++;

	return -EBUSY;
}

static void device_disable_open_processes(struct hl_device *hdev)
{
	struct hl_fpriv *hpriv;

	mutex_lock(&hdev->fpriv_list_lock);
	list_for_each_entry(hpriv, &hdev->fpriv_list, dev_node)
		hpriv->hdev = NULL;
	mutex_unlock(&hdev->fpriv_list_lock);
}

/*
 * hl_device_reset - reset the device
 *
 * @hdev: pointer to habanalabs device structure
 * @flags: reset flags.
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
int hl_device_reset(struct hl_device *hdev, u32 flags)
{
	u64 idle_mask[HL_BUSY_ENGINES_MASK_EXT_SIZE] = {0};
	bool hard_reset, from_hard_reset_thread, fw_reset, hard_instead_soft = false;
	int i, rc;

	if (!hdev->init_done) {
		dev_err(hdev->dev,
			"Can't reset before initialization is done\n");
		return 0;
	}

	hard_reset = !!(flags & HL_RESET_HARD);
	from_hard_reset_thread = !!(flags & HL_RESET_FROM_RESET_THREAD);
	fw_reset = !!(flags & HL_RESET_FW);

	if (!hard_reset && !hdev->supports_soft_reset) {
		hard_instead_soft = true;
		hard_reset = true;
	}

	if (hdev->reset_upon_device_release &&
			(flags & HL_RESET_DEVICE_RELEASE)) {
		dev_dbg(hdev->dev,
			"Perform %s-reset upon device release\n",
			hard_reset ? "hard" : "soft");
		goto do_reset;
	}

	if (!hard_reset && !hdev->allow_external_soft_reset) {
		hard_instead_soft = true;
		hard_reset = true;
	}

	if (hard_instead_soft)
		dev_dbg(hdev->dev, "Doing hard-reset instead of soft-reset\n");

do_reset:
	/* Re-entry of reset thread */
	if (from_hard_reset_thread && hdev->process_kill_trial_cnt)
		goto kill_processes;

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

		/*
		 * 'reset cause' is being updated here, because getting here
		 * means that it's the 1st time and the last time we're here
		 * ('in_reset' makes sure of it). This makes sure that
		 * 'reset_cause' will continue holding its 1st recorded reason!
		 */
		if (flags & HL_RESET_HEARTBEAT)
			hdev->curr_reset_cause = HL_RESET_CAUSE_HEARTBEAT;
		else if (flags & HL_RESET_TDR)
			hdev->curr_reset_cause = HL_RESET_CAUSE_TDR;
		else
			hdev->curr_reset_cause = HL_RESET_CAUSE_UNKNOWN;

		/* If reset is due to heartbeat, device CPU is no responsive in
		 * which case no point sending PCI disable message to it.
		 *
		 * If F/W is performing the reset, no need to send it a message to disable
		 * PCI access
		 */
		if (hard_reset && !(flags & (HL_RESET_HEARTBEAT | HL_RESET_FW))) {
			/* Disable PCI access from device F/W so he won't send
			 * us additional interrupts. We disable MSI/MSI-X at
			 * the halt_engines function and we can't have the F/W
			 * sending us interrupts after that. We need to disable
			 * the access here because if the device is marked
			 * disable, the message won't be send. Also, in case
			 * of heartbeat, the device CPU is marked as disable
			 * so this message won't be sent
			 */
			if (hl_fw_send_pci_access_msg(hdev,
					CPUCP_PACKET_DISABLE_PCI_ACCESS))
				dev_warn(hdev->dev,
					"Failed to disable PCI access by F/W\n");
		}

		/* This also blocks future CS/VM/JOB completion operations */
		hdev->disabled = true;

		take_release_locks(hdev);

		dev_err(hdev->dev, "Going to RESET device!\n");
	}

again:
	if ((hard_reset) && (!from_hard_reset_thread)) {
		hdev->hard_reset_pending = true;

		hdev->process_kill_trial_cnt = 0;

		hdev->device_reset_work.fw_reset = fw_reset;

		/*
		 * Because the reset function can't run from heartbeat work,
		 * we need to call the reset function from a dedicated work.
		 */
		queue_delayed_work(hdev->device_reset_work.wq,
			&hdev->device_reset_work.reset_work, 0);

		return 0;
	}

	cleanup_resources(hdev, hard_reset, fw_reset);

kill_processes:
	if (hard_reset) {
		/* Kill processes here after CS rollback. This is because the
		 * process can't really exit until all its CSs are done, which
		 * is what we do in cs rollback
		 */
		rc = device_kill_open_processes(hdev, 0);

		if (rc == -EBUSY) {
			if (hdev->device_fini_pending) {
				dev_crit(hdev->dev,
					"Failed to kill all open processes, stopping hard reset\n");
				goto out_err;
			}

			/* signal reset thread to reschedule */
			return rc;
		}

		if (rc) {
			dev_crit(hdev->dev,
				"Failed to kill all open processes, stopping hard reset\n");
			goto out_err;
		}

		/* Flush the Event queue workers to make sure no other thread is
		 * reading or writing to registers during the reset
		 */
		flush_workqueue(hdev->eq_wq);
	}

	/* Reset the H/W. It will be in idle state after this returns */
	hdev->asic_funcs->hw_fini(hdev, hard_reset, fw_reset);

	if (hard_reset) {
		hdev->fw_loader.linux_loaded = false;

		/* Release kernel context */
		if (hdev->kernel_ctx && hl_ctx_put(hdev->kernel_ctx) == 1)
			hdev->kernel_ctx = NULL;

		hl_vm_fini(hdev);
		hl_mmu_fini(hdev);
		hl_eq_reset(hdev, &hdev->event_queue);
	}

	/* Re-initialize PI,CI to 0 in all queues (hw queue, cq) */
	hl_hw_queue_reset(hdev, hard_reset);
	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		hl_cq_reset(hdev, &hdev->completion_queue[i]);

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
			hl_mmu_fini(hdev);
			goto out_err;
		}

		hdev->compute_ctx = NULL;

		rc = hl_ctx_init(hdev, hdev->kernel_ctx, true);
		if (rc) {
			dev_err(hdev->dev,
				"failed to init kernel ctx in hard reset\n");
			kfree(hdev->kernel_ctx);
			hdev->kernel_ctx = NULL;
			hl_mmu_fini(hdev);
			goto out_err;
		}
	}

	/* Device is now enabled as part of the initialization requires
	 * communication with the device firmware to get information that
	 * is required for the initialization itself
	 */
	hdev->disabled = false;

	rc = hdev->asic_funcs->hw_init(hdev);
	if (rc) {
		dev_err(hdev->dev,
			"failed to initialize the H/W after reset\n");
		goto out_err;
	}

	/* If device is not idle fail the reset process */
	if (!hdev->asic_funcs->is_device_idle(hdev, idle_mask,
			HL_BUSY_ENGINES_MASK_EXT_SIZE, NULL)) {
		dev_err(hdev->dev,
			"device is not idle (mask 0x%llx_%llx) after reset\n",
			idle_mask[1], idle_mask[0]);
		rc = -EIO;
		goto out_err;
	}

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

		hl_set_max_power(hdev);
	} else {
		rc = hdev->asic_funcs->soft_reset_late_init(hdev);
		if (rc) {
			dev_err(hdev->dev,
				"Failed late init after soft reset\n");
			goto out_err;
		}
	}

	atomic_set(&hdev->in_reset, 0);
	hdev->needs_reset = false;

	dev_notice(hdev->dev, "Successfully finished resetting the device\n");

	if (hard_reset) {
		hdev->hard_reset_cnt++;

		/* After reset is done, we are ready to receive events from
		 * the F/W. We can't do it before because we will ignore events
		 * and if those events are fatal, we won't know about it and
		 * the device will be operational although it shouldn't be
		 */
		hdev->asic_funcs->enable_events_from_fw(hdev);
	} else {
		hdev->soft_reset_cnt++;
	}

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
	int i, rc, cq_cnt, user_interrupt_cnt, cq_ready_cnt;
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

	user_interrupt_cnt = hdev->asic_prop.user_interrupt_count;

	if (user_interrupt_cnt) {
		hdev->user_interrupt = kcalloc(user_interrupt_cnt,
				sizeof(*hdev->user_interrupt),
				GFP_KERNEL);

		if (!hdev->user_interrupt) {
			rc = -ENOMEM;
			goto early_fini;
		}
	}

	/*
	 * Start calling ASIC initialization. First S/W then H/W and finally
	 * late init
	 */
	rc = hdev->asic_funcs->sw_init(hdev);
	if (rc)
		goto user_interrupts_fini;


	/* initialize completion structure for multi CS wait */
	hl_multi_cs_completion_init(hdev);

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

	cq_cnt = hdev->asic_prop.completion_queues_count;

	/*
	 * Initialize the completion queues. Must be done before hw_init,
	 * because there the addresses of the completion queues are being
	 * passed as arguments to request_irq
	 */
	if (cq_cnt) {
		hdev->completion_queue = kcalloc(cq_cnt,
				sizeof(*hdev->completion_queue),
				GFP_KERNEL);

		if (!hdev->completion_queue) {
			dev_err(hdev->dev,
				"failed to allocate completion queues\n");
			rc = -ENOMEM;
			goto hw_queues_destroy;
		}
	}

	for (i = 0, cq_ready_cnt = 0 ; i < cq_cnt ; i++, cq_ready_cnt++) {
		rc = hl_cq_init(hdev, &hdev->completion_queue[i],
				hdev->asic_funcs->get_queue_id_for_cq(hdev, i));
		if (rc) {
			dev_err(hdev->dev,
				"failed to initialize completion queue\n");
			goto cq_fini;
		}
		hdev->completion_queue[i].cq_idx = i;
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

	hdev->asic_funcs->state_dump_init(hdev);

	hl_debugfs_add_device(hdev);

	/* debugfs nodes are created in hl_ctx_init so it must be called after
	 * hl_debugfs_add_device.
	 */
	rc = hl_ctx_init(hdev, hdev->kernel_ctx, true);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize kernel context\n");
		kfree(hdev->kernel_ctx);
		goto remove_device_from_debugfs;
	}

	rc = hl_cb_pool_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CB pool\n");
		goto release_ctx;
	}

	/*
	 * From this point, override rc (=0) in case of an error to allow
	 * debugging (by adding char devices and create sysfs nodes as part of
	 * the error flow).
	 */
	add_cdev_sysfs_on_err = true;

	/* Device is now enabled as part of the initialization requires
	 * communication with the device firmware to get information that
	 * is required for the initialization itself
	 */
	hdev->disabled = false;

	rc = hdev->asic_funcs->hw_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize the H/W\n");
		rc = 0;
		goto out_disabled;
	}

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
		hdev->asic_prop.dram_size / SZ_1G);

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

	/* Need to call this again because the max power might change,
	 * depending on card type for certain ASICs
	 */
	hl_set_max_power(hdev);

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

	/* After initialization is done, we are ready to receive events from
	 * the F/W. We can't do it before because we will ignore events and if
	 * those events are fatal, we won't know about it and the device will
	 * be operational although it shouldn't be
	 */
	hdev->asic_funcs->enable_events_from_fw(hdev);

	return 0;

release_ctx:
	if (hl_ctx_put(hdev->kernel_ctx) != 1)
		dev_err(hdev->dev,
			"kernel ctx is still alive on initialization failure\n");
remove_device_from_debugfs:
	hl_debugfs_remove_device(hdev);
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
user_interrupts_fini:
	kfree(hdev->user_interrupt);
early_fini:
	device_early_fini(hdev);
free_dev_ctrl:
	put_device(hdev->dev_ctrl);
free_dev:
	put_device(hdev->dev);
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
	ktime_t timeout;
	u64 reset_sec;
	int i, rc;

	dev_info(hdev->dev, "Removing device\n");

	hdev->device_fini_pending = 1;
	flush_delayed_work(&hdev->device_reset_work.reset_work);

	if (hdev->pldm)
		reset_sec = HL_PLDM_HARD_RESET_MAX_TIMEOUT;
	else
		reset_sec = HL_HARD_RESET_MAX_TIMEOUT;

	/*
	 * This function is competing with the reset function, so try to
	 * take the reset atomic and if we are already in middle of reset,
	 * wait until reset function is finished. Reset function is designed
	 * to always finish. However, in Gaudi, because of all the network
	 * ports, the hard reset could take between 10-30 seconds
	 */

	timeout = ktime_add_us(ktime_get(), reset_sec * 1000 * 1000);
	rc = atomic_cmpxchg(&hdev->in_reset, 0, 1);
	while (rc) {
		usleep_range(50, 200);
		rc = atomic_cmpxchg(&hdev->in_reset, 0, 1);
		if (ktime_compare(ktime_get(), timeout) > 0) {
			dev_crit(hdev->dev,
				"Failed to remove device because reset function did not finish\n");
			return;
		}
	}

	/* Disable PCI access from device F/W so it won't send us additional
	 * interrupts. We disable MSI/MSI-X at the halt_engines function and we
	 * can't have the F/W sending us interrupts after that. We need to
	 * disable the access here because if the device is marked disable, the
	 * message won't be send. Also, in case of heartbeat, the device CPU is
	 * marked as disable so this message won't be sent
	 */
	hl_fw_send_pci_access_msg(hdev,	CPUCP_PACKET_DISABLE_PCI_ACCESS);

	/* Mark device as disabled */
	hdev->disabled = true;

	take_release_locks(hdev);

	hdev->hard_reset_pending = true;

	hl_hwmon_fini(hdev);

	cleanup_resources(hdev, true, false);

	/* Kill processes here after CS rollback. This is because the process
	 * can't really exit until all its CSs are done, which is what we
	 * do in cs rollback
	 */
	dev_info(hdev->dev,
		"Waiting for all processes to exit (timeout of %u seconds)",
		HL_PENDING_RESET_LONG_SEC);

	rc = device_kill_open_processes(hdev, HL_PENDING_RESET_LONG_SEC);
	if (rc) {
		dev_crit(hdev->dev, "Failed to kill all open processes\n");
		device_disable_open_processes(hdev);
	}

	hl_cb_pool_fini(hdev);

	/* Reset the H/W. It will be in idle state after this returns */
	hdev->asic_funcs->hw_fini(hdev, true, false);

	hdev->fw_loader.linux_loaded = false;

	/* Release kernel context */
	if ((hdev->kernel_ctx) && (hl_ctx_put(hdev->kernel_ctx) != 1))
		dev_err(hdev->dev, "kernel ctx is still alive\n");

	hl_debugfs_remove_device(hdev);

	hl_vm_fini(hdev);

	hl_mmu_fini(hdev);

	hl_eq_fini(hdev, &hdev->event_queue);

	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		hl_cq_fini(hdev, &hdev->completion_queue[i]);
	kfree(hdev->completion_queue);
	kfree(hdev->user_interrupt);

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
