// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#define pr_fmt(fmt)		"habanalabs: " fmt

#include "habanalabs.h"

#include <linux/pci.h>
#include <linux/module.h>

#define HL_DRIVER_AUTHOR	"HabanaLabs Kernel Driver Team"

#define HL_DRIVER_DESC		"Driver for HabanaLabs's AI Accelerators"

MODULE_AUTHOR(HL_DRIVER_AUTHOR);
MODULE_DESCRIPTION(HL_DRIVER_DESC);
MODULE_LICENSE("GPL v2");

static int hl_major;
static struct class *hl_class;
static DEFINE_IDR(hl_devs_idr);
static DEFINE_MUTEX(hl_devs_idr_lock);

static int timeout_locked = 5;
static int reset_on_lockup = 1;

module_param(timeout_locked, int, 0444);
MODULE_PARM_DESC(timeout_locked,
	"Device lockup timeout in seconds (0 = disabled, default 5s)");

module_param(reset_on_lockup, int, 0444);
MODULE_PARM_DESC(reset_on_lockup,
	"Do device reset on lockup (0 = no, 1 = yes, default yes)");

#define PCI_VENDOR_ID_HABANALABS	0x1da3

#define PCI_IDS_GOYA			0x0001

static const struct pci_device_id ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GOYA), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ids);

/*
 * get_asic_type - translate device id to asic type
 *
 * @device: id of the PCI device
 *
 * Translate device id to asic type.
 * In case of unidentified device, return -1
 */
static enum hl_asic_type get_asic_type(u16 device)
{
	enum hl_asic_type asic_type;

	switch (device) {
	case PCI_IDS_GOYA:
		asic_type = ASIC_GOYA;
		break;
	default:
		asic_type = ASIC_INVALID;
		break;
	}

	return asic_type;
}

/*
 * hl_device_open - open function for habanalabs device
 *
 * @inode: pointer to inode structure
 * @filp: pointer to file structure
 *
 * Called when process opens an habanalabs device.
 */
int hl_device_open(struct inode *inode, struct file *filp)
{
	struct hl_device *hdev;
	struct hl_fpriv *hpriv;
	int rc;

	mutex_lock(&hl_devs_idr_lock);
	hdev = idr_find(&hl_devs_idr, iminor(inode));
	mutex_unlock(&hl_devs_idr_lock);

	if (!hdev) {
		pr_err("Couldn't find device %d:%d\n",
			imajor(inode), iminor(inode));
		return -ENXIO;
	}

	hpriv = kzalloc(sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;

	hpriv->hdev = hdev;
	filp->private_data = hpriv;
	hpriv->filp = filp;
	mutex_init(&hpriv->restore_phase_mutex);
	kref_init(&hpriv->refcount);
	nonseekable_open(inode, filp);

	hl_cb_mgr_init(&hpriv->cb_mgr);
	hl_ctx_mgr_init(&hpriv->ctx_mgr);

	hpriv->taskpid = find_get_pid(current->pid);

	mutex_lock(&hdev->fpriv_list_lock);

	if (hl_device_disabled_or_in_reset(hdev)) {
		dev_err_ratelimited(hdev->dev,
			"Can't open %s because it is disabled or in reset\n",
			dev_name(hdev->dev));
		rc = -EPERM;
		goto out_err;
	}

	if (hdev->in_debug) {
		dev_err_ratelimited(hdev->dev,
			"Can't open %s because it is being debugged by another user\n",
			dev_name(hdev->dev));
		rc = -EPERM;
		goto out_err;
	}

	if (hdev->compute_ctx) {
		dev_dbg_ratelimited(hdev->dev,
			"Can't open %s because another user is working on it\n",
			dev_name(hdev->dev));
		rc = -EBUSY;
		goto out_err;
	}

	rc = hl_ctx_create(hdev, hpriv);
	if (rc) {
		dev_err(hdev->dev, "Failed to create context %d\n", rc);
		goto out_err;
	}

	/* Device is IDLE at this point so it is legal to change PLLs.
	 * There is no need to check anything because if the PLL is
	 * already HIGH, the set function will return without doing
	 * anything
	 */
	hl_device_set_frequency(hdev, PLL_HIGH);

	list_add(&hpriv->dev_node, &hdev->fpriv_list);
	mutex_unlock(&hdev->fpriv_list_lock);

	hl_debugfs_add_file(hpriv);

	return 0;

out_err:
	mutex_unlock(&hdev->fpriv_list_lock);

	hl_cb_mgr_fini(hpriv->hdev, &hpriv->cb_mgr);
	hl_ctx_mgr_fini(hpriv->hdev, &hpriv->ctx_mgr);
	filp->private_data = NULL;
	mutex_destroy(&hpriv->restore_phase_mutex);
	put_pid(hpriv->taskpid);

	kfree(hpriv);
	return rc;
}

int hl_device_open_ctrl(struct inode *inode, struct file *filp)
{
	struct hl_device *hdev;
	struct hl_fpriv *hpriv;
	int rc;

	mutex_lock(&hl_devs_idr_lock);
	hdev = idr_find(&hl_devs_idr, iminor(inode));
	mutex_unlock(&hl_devs_idr_lock);

	if (!hdev) {
		pr_err("Couldn't find device %d:%d\n",
			imajor(inode), iminor(inode));
		return -ENXIO;
	}

	hpriv = kzalloc(sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;

	mutex_lock(&hdev->fpriv_list_lock);

	if (hl_device_disabled_or_in_reset(hdev)) {
		dev_err_ratelimited(hdev->dev_ctrl,
			"Can't open %s because it is disabled or in reset\n",
			dev_name(hdev->dev_ctrl));
		rc = -EPERM;
		goto out_err;
	}

	list_add(&hpriv->dev_node, &hdev->fpriv_list);
	mutex_unlock(&hdev->fpriv_list_lock);

	hpriv->hdev = hdev;
	filp->private_data = hpriv;
	hpriv->filp = filp;
	hpriv->is_control = true;
	nonseekable_open(inode, filp);

	hpriv->taskpid = find_get_pid(current->pid);

	return 0;

out_err:
	mutex_unlock(&hdev->fpriv_list_lock);
	kfree(hpriv);
	return rc;
}

static void set_driver_behavior_per_device(struct hl_device *hdev)
{
	hdev->mmu_enable = 1;
	hdev->cpu_enable = 1;
	hdev->fw_loading = 1;
	hdev->cpu_queues_enable = 1;
	hdev->heartbeat = 1;

	hdev->reset_pcilink = 0;
}

/*
 * create_hdev - create habanalabs device instance
 *
 * @dev: will hold the pointer to the new habanalabs device structure
 * @pdev: pointer to the pci device
 * @asic_type: in case of simulator device, which device is it
 * @minor: in case of simulator device, the minor of the device
 *
 * Allocate memory for habanalabs device and initialize basic fields
 * Identify the ASIC type
 * Allocate ID (minor) for the device (only for real devices)
 */
int create_hdev(struct hl_device **dev, struct pci_dev *pdev,
		enum hl_asic_type asic_type, int minor)
{
	struct hl_device *hdev;
	int rc, main_id, ctrl_id = 0;

	*dev = NULL;

	hdev = kzalloc(sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;

	/* First, we must find out which ASIC are we handling. This is needed
	 * to configure the behavior of the driver (kernel parameters)
	 */
	if (pdev) {
		hdev->asic_type = get_asic_type(pdev->device);
		if (hdev->asic_type == ASIC_INVALID) {
			dev_err(&pdev->dev, "Unsupported ASIC\n");
			rc = -ENODEV;
			goto free_hdev;
		}
	} else {
		hdev->asic_type = asic_type;
	}

	hdev->major = hl_major;
	hdev->reset_on_lockup = reset_on_lockup;
	hdev->pldm = 0;

	set_driver_behavior_per_device(hdev);

	if (timeout_locked)
		hdev->timeout_jiffies = msecs_to_jiffies(timeout_locked * 1000);
	else
		hdev->timeout_jiffies = MAX_SCHEDULE_TIMEOUT;

	hdev->disabled = true;
	hdev->pdev = pdev; /* can be NULL in case of simulator device */

	/* Set default DMA mask to 32 bits */
	hdev->dma_mask = 32;

	mutex_lock(&hl_devs_idr_lock);

	/* Always save 2 numbers, 1 for main device and 1 for control.
	 * They must be consecutive
	 */
	main_id = idr_alloc(&hl_devs_idr, hdev, 0, HL_MAX_MINORS,
				GFP_KERNEL);

	if (main_id >= 0)
		ctrl_id = idr_alloc(&hl_devs_idr, hdev, main_id + 1,
					main_id + 2, GFP_KERNEL);

	mutex_unlock(&hl_devs_idr_lock);

	if ((main_id < 0) || (ctrl_id < 0)) {
		if ((main_id == -ENOSPC) || (ctrl_id == -ENOSPC))
			pr_err("too many devices in the system\n");

		if (main_id >= 0) {
			mutex_lock(&hl_devs_idr_lock);
			idr_remove(&hl_devs_idr, main_id);
			mutex_unlock(&hl_devs_idr_lock);
		}

		rc = -EBUSY;
		goto free_hdev;
	}

	hdev->id = main_id;
	hdev->id_control = ctrl_id;

	*dev = hdev;

	return 0;

free_hdev:
	kfree(hdev);
	return rc;
}

/*
 * destroy_hdev - destroy habanalabs device instance
 *
 * @dev: pointer to the habanalabs device structure
 *
 */
void destroy_hdev(struct hl_device *hdev)
{
	/* Remove device from the device list */
	mutex_lock(&hl_devs_idr_lock);
	idr_remove(&hl_devs_idr, hdev->id);
	idr_remove(&hl_devs_idr, hdev->id_control);
	mutex_unlock(&hl_devs_idr_lock);

	kfree(hdev);
}

static int hl_pmops_suspend(struct device *dev)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	pr_debug("Going to suspend PCI device\n");

	if (!hdev) {
		pr_err("device pointer is NULL in suspend\n");
		return 0;
	}

	return hl_device_suspend(hdev);
}

static int hl_pmops_resume(struct device *dev)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	pr_debug("Going to resume PCI device\n");

	if (!hdev) {
		pr_err("device pointer is NULL in resume\n");
		return 0;
	}

	return hl_device_resume(hdev);
}

/*
 * hl_pci_probe - probe PCI habanalabs devices
 *
 * @pdev: pointer to pci device
 * @id: pointer to pci device id structure
 *
 * Standard PCI probe function for habanalabs device.
 * Create a new habanalabs device and initialize it according to the
 * device's type
 */
static int hl_pci_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	struct hl_device *hdev;
	int rc;

	dev_info(&pdev->dev, HL_NAME
		 " device found [%04x:%04x] (rev %x)\n",
		 (int)pdev->vendor, (int)pdev->device, (int)pdev->revision);

	rc = create_hdev(&hdev, pdev, ASIC_INVALID, -1);
	if (rc)
		return rc;

	pci_set_drvdata(pdev, hdev);

	rc = hl_device_init(hdev, hl_class);
	if (rc) {
		dev_err(&pdev->dev, "Fatal error during habanalabs device init\n");
		rc = -ENODEV;
		goto disable_device;
	}

	return 0;

disable_device:
	pci_set_drvdata(pdev, NULL);
	destroy_hdev(hdev);

	return rc;
}

/*
 * hl_pci_remove - remove PCI habanalabs devices
 *
 * @pdev: pointer to pci device
 *
 * Standard PCI remove function for habanalabs device
 */
static void hl_pci_remove(struct pci_dev *pdev)
{
	struct hl_device *hdev;

	hdev = pci_get_drvdata(pdev);
	if (!hdev)
		return;

	hl_device_fini(hdev);
	pci_set_drvdata(pdev, NULL);

	destroy_hdev(hdev);
}

static const struct dev_pm_ops hl_pm_ops = {
	.suspend = hl_pmops_suspend,
	.resume = hl_pmops_resume,
};

static struct pci_driver hl_pci_driver = {
	.name = HL_NAME,
	.id_table = ids,
	.probe = hl_pci_probe,
	.remove = hl_pci_remove,
	.driver.pm = &hl_pm_ops,
};

/*
 * hl_init - Initialize the habanalabs kernel driver
 */
static int __init hl_init(void)
{
	int rc;
	dev_t dev;

	pr_info("loading driver\n");

	rc = alloc_chrdev_region(&dev, 0, HL_MAX_MINORS, HL_NAME);
	if (rc < 0) {
		pr_err("unable to get major\n");
		return rc;
	}

	hl_major = MAJOR(dev);

	hl_class = class_create(THIS_MODULE, HL_NAME);
	if (IS_ERR(hl_class)) {
		pr_err("failed to allocate class\n");
		rc = PTR_ERR(hl_class);
		goto remove_major;
	}

	hl_debugfs_init();

	rc = pci_register_driver(&hl_pci_driver);
	if (rc) {
		pr_err("failed to register pci device\n");
		goto remove_debugfs;
	}

	pr_debug("driver loaded\n");

	return 0;

remove_debugfs:
	hl_debugfs_fini();
	class_destroy(hl_class);
remove_major:
	unregister_chrdev_region(MKDEV(hl_major, 0), HL_MAX_MINORS);
	return rc;
}

/*
 * hl_exit - Release all resources of the habanalabs kernel driver
 */
static void __exit hl_exit(void)
{
	pci_unregister_driver(&hl_pci_driver);

	/*
	 * Removing debugfs must be after all devices or simulator devices
	 * have been removed because otherwise we get a bug in the
	 * debugfs module for referencing NULL objects
	 */
	hl_debugfs_fini();

	class_destroy(hl_class);
	unregister_chrdev_region(MKDEV(hl_major, 0), HL_MAX_MINORS);

	idr_destroy(&hl_devs_idr);

	pr_debug("driver removed\n");
}

module_init(hl_init);
module_exit(hl_exit);
