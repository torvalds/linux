// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/pci.h>
#include <linux/delay.h>

static void hpriv_release(struct kref *ref)
{
	struct hl_fpriv *hpriv;
	struct hl_device *hdev;

	hpriv = container_of(ref, struct hl_fpriv, refcount);

	hdev = hpriv->hdev;

	put_pid(hpriv->taskpid);

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

	return hpriv->hdev->asic_funcs->mmap(hpriv, vma);
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

	hl_cb_mgr_init(&hdev->kernel_cb_mgr);

	mutex_init(&hdev->fd_open_cnt_lock);
	mutex_init(&hdev->send_cpu_message_lock);
	atomic_set(&hdev->fd_open_cnt, 0);

	return 0;

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

	destroy_workqueue(hdev->eq_wq);
	destroy_workqueue(hdev->cq_wq);

	hl_asid_fini(hdev);

	if (hdev->asic_funcs->early_fini)
		hdev->asic_funcs->early_fini(hdev);

	mutex_destroy(&hdev->fd_open_cnt_lock);
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
	rc = pci_enable_device(hdev->pdev);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to enable PCI device in resume\n");
		return rc;
	}

	rc = hdev->asic_funcs->resume(hdev);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to enable PCI access from device CPU\n");
		return rc;
	}

	return 0;
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

	dev_notice(hdev->dev,
		"Successfully added device to habanalabs driver\n");

	return 0;

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
	int i;
	dev_info(hdev->dev, "Removing device\n");

	/* Mark device as disabled */
	hdev->disabled = true;

	/*
	 * Halt the engines and disable interrupts so we won't get any more
	 * completions from H/W and we won't have any accesses from the
	 * H/W to the host machine
	 */
	hdev->asic_funcs->halt_engines(hdev, true);

	hl_cb_pool_fini(hdev);

	/* Release kernel context */
	if ((hdev->kernel_ctx) && (hl_ctx_put(hdev->kernel_ctx) != 1))
		dev_err(hdev->dev, "kernel ctx is still alive\n");

	/* Reset the H/W. It will be in idle state after this returns */
	hdev->asic_funcs->hw_fini(hdev, true);

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
