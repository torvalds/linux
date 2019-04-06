// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

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

	kfree(hpriv);

	/* Now the FD is really closed */
	atomic_dec(&hdev->fd_open_cnt);

	/* This allows a new user context to open the device */
	hdev->user_ctx = NULL;
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

/*
 * device_setup_cdev - setup cdev and device for habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 * @hclass: pointer to the class object of the device
 * @minor: minor number of the specific device
 * @fpos : file operations to install for this device
 *
 * Create a cdev and a Linux device for habanalabs's device. Need to be
 * called at the end of the habanalabs device initialization process,
 * because this function exposes the device to the user
 */
static int device_setup_cdev(struct hl_device *hdev, struct class *hclass,
				int minor, const struct file_operations *fops)
{
	int err, devno = MKDEV(hdev->major, minor);
	struct cdev *hdev_cdev = &hdev->cdev;
	char *name;

	name = kasprintf(GFP_KERNEL, "hl%d", hdev->id);
	if (!name)
		return -ENOMEM;

	cdev_init(hdev_cdev, fops);
	hdev_cdev->owner = THIS_MODULE;
	err = cdev_add(hdev_cdev, devno, 1);
	if (err) {
		pr_err("Failed to add char device %s\n", name);
		goto err_cdev_add;
	}

	hdev->dev = device_create(hclass, NULL, devno, NULL, "%s", name);
	if (IS_ERR(hdev->dev)) {
		pr_err("Failed to create device %s\n", name);
		err = PTR_ERR(hdev->dev);
		goto err_device_create;
	}

	dev_set_drvdata(hdev->dev, hdev);

	kfree(name);

	return 0;

err_device_create:
	cdev_del(hdev_cdev);
err_cdev_add:
	kfree(name);
	return err;
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

	hl_cb_mgr_init(&hdev->kernel_cb_mgr);

	mutex_init(&hdev->fd_open_cnt_lock);
	mutex_init(&hdev->send_cpu_message_lock);
	INIT_LIST_HEAD(&hdev->hw_queues_mirror_list);
	spin_lock_init(&hdev->hw_queues_mirror_lock);
	atomic_set(&hdev->in_reset, 0);
	atomic_set(&hdev->fd_open_cnt, 0);
	atomic_set(&hdev->cs_active_cnt, 0);

	return 0;

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
	mutex_destroy(&hdev->send_cpu_message_lock);

	hl_cb_mgr_fini(hdev, &hdev->kernel_cb_mgr);

	kfree(hdev->hl_chip_info);

	destroy_workqueue(hdev->eq_wq);
	destroy_workqueue(hdev->cq_wq);

	hl_asid_fini(hdev);

	if (hdev->asic_funcs->early_fini)
		hdev->asic_funcs->early_fini(hdev);

	mutex_destroy(&hdev->fd_open_cnt_lock);
}

static void set_freq_to_low_job(struct work_struct *work)
{
	struct hl_device *hdev = container_of(work, struct hl_device,
						work_freq.work);

	if (atomic_read(&hdev->fd_open_cnt) == 0)
		hl_device_set_frequency(hdev, PLL_LOW);

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

	INIT_DELAYED_WORK(&hdev->work_freq, set_freq_to_low_job);
	hdev->high_pll = hdev->asic_prop.high_pll;

	/* force setting to low frequency */
	atomic_set(&hdev->curr_pll_profile, PLL_LOW);

	if (hdev->pm_mng_profile == PM_AUTO)
		hdev->asic_funcs->set_pll_profile(hdev, PLL_LOW);
	else
		hdev->asic_funcs->set_pll_profile(hdev, PLL_LAST);

	if (hdev->asic_funcs->late_init) {
		rc = hdev->asic_funcs->late_init(hdev);
		if (rc) {
			dev_err(hdev->dev,
				"failed late initialization for the H/W\n");
			return rc;
		}
	}

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

/*
 * hl_device_set_frequency - set the frequency of the device
 *
 * @hdev: pointer to habanalabs device structure
 * @freq: the new frequency value
 *
 * Change the frequency if needed.
 * We allose to set PLL to low only if there is no user process
 * Returns 0 if no change was done, otherwise returns 1;
 */
int hl_device_set_frequency(struct hl_device *hdev, enum hl_pll_frequency freq)
{
	enum hl_pll_frequency old_freq =
			(freq == PLL_HIGH) ? PLL_LOW : PLL_HIGH;
	int ret;

	if (hdev->pm_mng_profile == PM_MANUAL)
		return 0;

	ret = atomic_cmpxchg(&hdev->curr_pll_profile, old_freq, freq);
	if (ret == freq)
		return 0;

	/*
	 * in case we want to lower frequency, check if device is not
	 * opened. We must have a check here to workaround race condition with
	 * hl_device_open
	 */
	if ((freq == PLL_LOW) && (atomic_read(&hdev->fd_open_cnt) > 0)) {
		atomic_set(&hdev->curr_pll_profile, PLL_HIGH);
		return 0;
	}

	dev_dbg(hdev->dev, "Changing device frequency to %s\n",
		freq == PLL_HIGH ? "high" : "low");

	hdev->asic_funcs->set_pll_profile(hdev, freq);

	return 1;
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
	struct task_struct *task = NULL;

	if (hdev->pldm)
		pending_total = HL_PLDM_PENDING_RESET_PER_SEC;
	else
		pending_total = HL_PENDING_RESET_PER_SEC;

	pending_cnt = pending_total;

	/* Flush all processes that are inside hl_open */
	mutex_lock(&hdev->fd_open_cnt_lock);

	while ((atomic_read(&hdev->fd_open_cnt)) && (pending_cnt)) {

		pending_cnt--;

		dev_info(hdev->dev,
			"Can't HARD reset, waiting for user to close FD\n");
		ssleep(1);
	}

	if (atomic_read(&hdev->fd_open_cnt)) {
		task = get_pid_task(hdev->user_ctx->hpriv->taskpid,
					PIDTYPE_PID);
		if (task) {
			dev_info(hdev->dev, "Killing user processes\n");
			send_sig(SIGKILL, task, 1);
			msleep(100);

			put_task_struct(task);
		}
	}

	/* We killed the open users, but because the driver cleans up after the
	 * user contexts are closed (e.g. mmu mappings), we need to wait again
	 * to make sure the cleaning phase is finished before continuing with
	 * the reset
	 */

	pending_cnt = pending_total;

	while ((atomic_read(&hdev->fd_open_cnt)) && (pending_cnt)) {

		pending_cnt--;

		ssleep(1);
	}

	if (atomic_read(&hdev->fd_open_cnt))
		dev_crit(hdev->dev,
			"Going to hard reset with open user contexts\n");

	mutex_unlock(&hdev->fd_open_cnt_lock);

}

static void device_hard_reset_pending(struct work_struct *work)
{
	struct hl_device_reset_work *device_reset_work =
		container_of(work, struct hl_device_reset_work, reset_work);
	struct hl_device *hdev = device_reset_work->hdev;

	device_kill_open_processes(hdev);

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

		/*
		 * Flush anyone that is inside the critical section of enqueue
		 * jobs to the H/W
		 */
		hdev->asic_funcs->hw_queues_lock(hdev);
		hdev->asic_funcs->hw_queues_unlock(hdev);

		dev_err(hdev->dev, "Going to RESET device!\n");
	}

again:
	if ((hard_reset) && (!from_hard_reset_thread)) {
		struct hl_device_reset_work *device_reset_work;

		if (!hdev->pdev) {
			dev_err(hdev->dev,
				"Reset action is NOT supported in simulator\n");
			rc = -EINVAL;
			goto out_err;
		}

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

	/* Release kernel context */
	if ((hard_reset) && (hl_ctx_put(hdev->kernel_ctx) == 1))
		hdev->kernel_ctx = NULL;

	/* Reset the H/W. It will be in idle state after this returns */
	hdev->asic_funcs->hw_fini(hdev, hard_reset);

	if (hard_reset) {
		hl_vm_fini(hdev);
		hl_eq_reset(hdev, &hdev->event_queue);
	}

	/* Re-initialize PI,CI to 0 in all queues (hw queue, cq) */
	hl_hw_queue_reset(hdev, hard_reset);
	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		hl_cq_reset(hdev, &hdev->completion_queue[i]);

	/* Make sure the setup phase for the user context will run again */
	if (hdev->user_ctx) {
		atomic_set(&hdev->user_ctx->thread_restore_token, 1);
		hdev->user_ctx->thread_restore_wait_token = 0;
	}

	/* Finished tear-down, starting to re-initialize */

	if (hard_reset) {
		hdev->device_cpu_disabled = false;

		if (hdev->kernel_ctx) {
			dev_crit(hdev->dev,
				"kernel ctx was alive during hard reset, something is terribly wrong\n");
			rc = -EBUSY;
			goto out_err;
		}

		/* Allocate the kernel context */
		hdev->kernel_ctx = kzalloc(sizeof(*hdev->kernel_ctx),
						GFP_KERNEL);
		if (!hdev->kernel_ctx) {
			rc = -ENOMEM;
			goto out_err;
		}

		hdev->user_ctx = NULL;

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

		hdev->hard_reset_pending = false;
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

	/* Create device */
	rc = device_setup_cdev(hdev, hclass, hdev->id, &hl_ops);

	if (rc)
		goto out_disabled;

	/* Initialize ASIC function pointers and perform early init */
	rc = device_early_init(hdev);
	if (rc)
		goto release_device;

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

	/* Allocate the kernel context */
	hdev->kernel_ctx = kzalloc(sizeof(*hdev->kernel_ctx), GFP_KERNEL);
	if (!hdev->kernel_ctx) {
		rc = -ENOMEM;
		goto eq_fini;
	}

	hdev->user_ctx = NULL;

	rc = hl_ctx_init(hdev, hdev->kernel_ctx, true);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize kernel context\n");
		goto free_ctx;
	}

	rc = hl_cb_pool_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CB pool\n");
		goto release_ctx;
	}

	rc = hl_sysfs_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize sysfs\n");
		goto free_cb_pool;
	}

	hl_debugfs_add_device(hdev);

	if (hdev->asic_funcs->get_hw_state(hdev) == HL_DEVICE_HW_STATE_DIRTY) {
		dev_info(hdev->dev,
			"H/W state is dirty, must reset before initializing\n");
		hdev->asic_funcs->hw_fini(hdev, true);
	}

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

	/* After test_queues, KMD can start sending messages to device CPU */

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
	 * hl_hwmon_init must be called after device_late_init, because only
	 * there we get the information from the device about which
	 * hwmon-related sensors the device supports
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

free_cb_pool:
	hl_cb_pool_fini(hdev);
release_ctx:
	if (hl_ctx_put(hdev->kernel_ctx) != 1)
		dev_err(hdev->dev,
			"kernel ctx is still alive on initialization failure\n");
free_ctx:
	kfree(hdev->kernel_ctx);
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
release_device:
	device_destroy(hclass, hdev->dev->devt);
	cdev_del(&hdev->cdev);
out_disabled:
	hdev->disabled = true;
	if (hdev->pdev)
		dev_err(&hdev->pdev->dev,
			"Failed to initialize hl%d. Device is NOT usable !\n",
			hdev->id);
	else
		pr_err("Failed to initialize hl%d. Device is NOT usable !\n",
			hdev->id);

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

	/*
	 * Flush anyone that is inside the critical section of enqueue
	 * jobs to the H/W
	 */
	hdev->asic_funcs->hw_queues_lock(hdev);
	hdev->asic_funcs->hw_queues_unlock(hdev);

	device_kill_open_processes(hdev);

	hl_hwmon_fini(hdev);

	device_late_fini(hdev);

	hl_debugfs_remove_device(hdev);

	hl_sysfs_fini(hdev);

	/*
	 * Halt the engines and disable interrupts so we won't get any more
	 * completions from H/W and we won't have any accesses from the
	 * H/W to the host machine
	 */
	hdev->asic_funcs->halt_engines(hdev, true);

	/* Go over all the queues, release all CS and their jobs */
	hl_cs_rollback_all(hdev);

	hl_cb_pool_fini(hdev);

	/* Release kernel context */
	if ((hdev->kernel_ctx) && (hl_ctx_put(hdev->kernel_ctx) != 1))
		dev_err(hdev->dev, "kernel ctx is still alive\n");

	/* Reset the H/W. It will be in idle state after this returns */
	hdev->asic_funcs->hw_fini(hdev, true);

	hl_vm_fini(hdev);

	hl_eq_fini(hdev, &hdev->event_queue);

	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		hl_cq_fini(hdev, &hdev->completion_queue[i]);
	kfree(hdev->completion_queue);

	hl_hw_queues_destroy(hdev);

	/* Call ASIC S/W finalize function */
	hdev->asic_funcs->sw_fini(hdev);

	device_early_fini(hdev);

	/* Hide device from user */
	device_destroy(hdev->dev->class, hdev->dev->devt);
	cdev_del(&hdev->cdev);

	pr_info("removed device successfully\n");
}

/*
 * hl_poll_timeout_memory - Periodically poll a host memory address
 *                              until it is not zero or a timeout occurs
 * @hdev: pointer to habanalabs device structure
 * @addr: Address to poll
 * @timeout_us: timeout in us
 * @val: Variable to read the value into
 *
 * Returns 0 on success and -ETIMEDOUT upon a timeout. In either
 * case, the last read value at @addr is stored in @val. Must not
 * be called from atomic context if sleep_us or timeout_us are used.
 *
 * The function sleeps for 100us with timeout value of
 * timeout_us
 */
int hl_poll_timeout_memory(struct hl_device *hdev, u64 addr,
				u32 timeout_us, u32 *val)
{
	/*
	 * address in this function points always to a memory location in the
	 * host's (server's) memory. That location is updated asynchronously
	 * either by the direct access of the device or by another core
	 */
	u32 *paddr = (u32 *) (uintptr_t) addr;
	ktime_t timeout = ktime_add_us(ktime_get(), timeout_us);

	might_sleep();

	for (;;) {
		/*
		 * Flush CPU read/write buffers to make sure we read updates
		 * done by other cores or by the device
		 */
		mb();
		*val = *paddr;
		if (*val)
			break;
		if (ktime_compare(ktime_get(), timeout) > 0) {
			*val = *paddr;
			break;
		}
		usleep_range((100 >> 2) + 1, 100);
	}

	return *val ? 0 : -ETIMEDOUT;
}

/*
 * hl_poll_timeout_devicememory - Periodically poll a device memory address
 *                                until it is not zero or a timeout occurs
 * @hdev: pointer to habanalabs device structure
 * @addr: Device address to poll
 * @timeout_us: timeout in us
 * @val: Variable to read the value into
 *
 * Returns 0 on success and -ETIMEDOUT upon a timeout. In either
 * case, the last read value at @addr is stored in @val. Must not
 * be called from atomic context if sleep_us or timeout_us are used.
 *
 * The function sleeps for 100us with timeout value of
 * timeout_us
 */
int hl_poll_timeout_device_memory(struct hl_device *hdev, void __iomem *addr,
				u32 timeout_us, u32 *val)
{
	ktime_t timeout = ktime_add_us(ktime_get(), timeout_us);

	might_sleep();

	for (;;) {
		*val = readl(addr);
		if (*val)
			break;
		if (ktime_compare(ktime_get(), timeout) > 0) {
			*val = readl(addr);
			break;
		}
		usleep_range((100 >> 2) + 1, 100);
	}

	return *val ? 0 : -ETIMEDOUT;
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
