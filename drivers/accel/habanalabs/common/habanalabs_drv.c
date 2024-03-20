// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#define pr_fmt(fmt)		"habanalabs: " fmt

#include "habanalabs.h"
#include "../include/hw_ip/pci/pci_general.h"

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#define CREATE_TRACE_POINTS
#include <trace/events/habanalabs.h>

#define HL_DRIVER_AUTHOR	"HabanaLabs Kernel Driver Team"

#define HL_DRIVER_DESC		"Driver for HabanaLabs's AI Accelerators"

MODULE_AUTHOR(HL_DRIVER_AUTHOR);
MODULE_DESCRIPTION(HL_DRIVER_DESC);
MODULE_LICENSE("GPL v2");

static int hl_major;
static struct class *hl_class;
static DEFINE_IDR(hl_devs_idr);
static DEFINE_MUTEX(hl_devs_idr_lock);

#define HL_DEFAULT_TIMEOUT_LOCKED	30	/* 30 seconds */
#define GAUDI_DEFAULT_TIMEOUT_LOCKED	600	/* 10 minutes */

static int timeout_locked = HL_DEFAULT_TIMEOUT_LOCKED;
static int reset_on_lockup = 1;
static int memory_scrub;
static ulong boot_error_status_mask = ULONG_MAX;

module_param(timeout_locked, int, 0444);
MODULE_PARM_DESC(timeout_locked,
	"Device lockup timeout in seconds (0 = disabled, default 30s)");

module_param(reset_on_lockup, int, 0444);
MODULE_PARM_DESC(reset_on_lockup,
	"Do device reset on lockup (0 = no, 1 = yes, default yes)");

module_param(memory_scrub, int, 0444);
MODULE_PARM_DESC(memory_scrub,
	"Scrub device memory in various states (0 = no, 1 = yes, default no)");

module_param(boot_error_status_mask, ulong, 0444);
MODULE_PARM_DESC(boot_error_status_mask,
	"Mask of the error status during device CPU boot (If bitX is cleared then error X is masked. Default all 1's)");

#define PCI_IDS_GOYA			0x0001
#define PCI_IDS_GAUDI			0x1000
#define PCI_IDS_GAUDI_SEC		0x1010

#define PCI_IDS_GAUDI2			0x1020

static const struct pci_device_id ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GOYA), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI_SEC), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI2), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ids);

/*
 * get_asic_type - translate device id to asic type
 *
 * @hdev: pointer to habanalabs device structure.
 *
 * Translate device id and revision id to asic type.
 * In case of unidentified device, return -1
 */
static enum hl_asic_type get_asic_type(struct hl_device *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	enum hl_asic_type asic_type = ASIC_INVALID;

	switch (pdev->device) {
	case PCI_IDS_GOYA:
		asic_type = ASIC_GOYA;
		break;
	case PCI_IDS_GAUDI:
		asic_type = ASIC_GAUDI;
		break;
	case PCI_IDS_GAUDI_SEC:
		asic_type = ASIC_GAUDI_SEC;
		break;
	case PCI_IDS_GAUDI2:
		switch (pdev->revision) {
		case REV_ID_A:
			asic_type = ASIC_GAUDI2;
			break;
		case REV_ID_B:
			asic_type = ASIC_GAUDI2B;
			break;
		case REV_ID_C:
			asic_type = ASIC_GAUDI2C;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return asic_type;
}

static bool is_asic_secured(enum hl_asic_type asic_type)
{
	switch (asic_type) {
	case ASIC_GAUDI_SEC:
		return true;
	default:
		return false;
	}
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
	enum hl_device_status status;
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

	mutex_init(&hpriv->notifier_event.lock);
	mutex_init(&hpriv->restore_phase_mutex);
	mutex_init(&hpriv->ctx_lock);
	kref_init(&hpriv->refcount);
	nonseekable_open(inode, filp);

	hl_ctx_mgr_init(&hpriv->ctx_mgr);
	hl_mem_mgr_init(hpriv->hdev->dev, &hpriv->mem_mgr);

	hpriv->taskpid = get_task_pid(current, PIDTYPE_PID);

	mutex_lock(&hdev->fpriv_list_lock);

	if (!hl_device_operational(hdev, &status)) {
		dev_dbg_ratelimited(hdev->dev,
			"Can't open %s because it is %s\n",
			dev_name(hdev->dev), hdev->status[status]);

		if (status == HL_DEVICE_STATUS_IN_RESET ||
					status == HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE)
			rc = -EAGAIN;
		else
			rc = -EPERM;

		goto out_err;
	}

	if (hdev->is_in_dram_scrub) {
		dev_dbg_ratelimited(hdev->dev,
			"Can't open %s during dram scrub\n",
			dev_name(hdev->dev));
		rc = -EAGAIN;
		goto out_err;
	}

	if (hdev->compute_ctx_in_release) {
		dev_dbg_ratelimited(hdev->dev,
			"Can't open %s because another user is still releasing it\n",
			dev_name(hdev->dev));
		rc = -EAGAIN;
		goto out_err;
	}

	if (hdev->is_compute_ctx_active) {
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

	list_add(&hpriv->dev_node, &hdev->fpriv_list);
	mutex_unlock(&hdev->fpriv_list_lock);

	hdev->asic_funcs->send_device_activity(hdev, true);

	hl_debugfs_add_file(hpriv);

	hl_enable_err_info_capture(&hdev->captured_err_info);

	hdev->open_counter++;
	hdev->last_successful_open_jif = jiffies;
	hdev->last_successful_open_ktime = ktime_get();

	return 0;

out_err:
	mutex_unlock(&hdev->fpriv_list_lock);
	hl_mem_mgr_fini(&hpriv->mem_mgr);
	hl_mem_mgr_idr_destroy(&hpriv->mem_mgr);
	hl_ctx_mgr_fini(hpriv->hdev, &hpriv->ctx_mgr);
	filp->private_data = NULL;
	mutex_destroy(&hpriv->ctx_lock);
	mutex_destroy(&hpriv->restore_phase_mutex);
	mutex_destroy(&hpriv->notifier_event.lock);
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

	/* Prevent other routines from reading partial hpriv data by
	 * initializing hpriv fields before inserting it to the list
	 */
	hpriv->hdev = hdev;
	filp->private_data = hpriv;
	hpriv->filp = filp;

	mutex_init(&hpriv->notifier_event.lock);
	nonseekable_open(inode, filp);

	hpriv->taskpid = get_task_pid(current, PIDTYPE_PID);

	mutex_lock(&hdev->fpriv_ctrl_list_lock);

	if (!hl_ctrl_device_operational(hdev, NULL)) {
		dev_dbg_ratelimited(hdev->dev_ctrl,
			"Can't open %s because it is disabled\n",
			dev_name(hdev->dev_ctrl));
		rc = -EPERM;
		goto out_err;
	}

	list_add(&hpriv->dev_node, &hdev->fpriv_ctrl_list);
	mutex_unlock(&hdev->fpriv_ctrl_list_lock);

	return 0;

out_err:
	mutex_unlock(&hdev->fpriv_ctrl_list_lock);
	filp->private_data = NULL;
	put_pid(hpriv->taskpid);

	kfree(hpriv);

	return rc;
}

static void set_driver_behavior_per_device(struct hl_device *hdev)
{
	hdev->nic_ports_mask = 0;
	hdev->fw_components = FW_TYPE_ALL_TYPES;
	hdev->cpu_queues_enable = 1;
	hdev->pldm = 0;
	hdev->hard_reset_on_fw_events = 1;
	hdev->bmc_enable = 1;
	hdev->reset_on_preboot_fail = 1;
	hdev->heartbeat = 1;
}

static void copy_kernel_module_params_to_device(struct hl_device *hdev)
{
	hdev->asic_prop.fw_security_enabled = is_asic_secured(hdev->asic_type);

	hdev->major = hl_major;
	hdev->hclass = hl_class;
	hdev->memory_scrub = memory_scrub;
	hdev->reset_on_lockup = reset_on_lockup;
	hdev->boot_error_status_mask = boot_error_status_mask;
}

static void fixup_device_params_per_asic(struct hl_device *hdev, int timeout)
{
	switch (hdev->asic_type) {
	case ASIC_GAUDI:
	case ASIC_GAUDI_SEC:
		/* If user didn't request a different timeout than the default one, we have
		 * a different default timeout for Gaudi
		 */
		if (timeout == HL_DEFAULT_TIMEOUT_LOCKED)
			hdev->timeout_jiffies = msecs_to_jiffies(GAUDI_DEFAULT_TIMEOUT_LOCKED *
										MSEC_PER_SEC);

		hdev->reset_upon_device_release = 0;
		break;

	case ASIC_GOYA:
		hdev->reset_upon_device_release = 0;
		break;

	default:
		hdev->reset_upon_device_release = 1;
		break;
	}
}

static int fixup_device_params(struct hl_device *hdev)
{
	int tmp_timeout;

	tmp_timeout = timeout_locked;

	hdev->fw_poll_interval_usec = HL_FW_STATUS_POLL_INTERVAL_USEC;
	hdev->fw_comms_poll_interval_usec = HL_FW_STATUS_POLL_INTERVAL_USEC;

	if (tmp_timeout)
		hdev->timeout_jiffies = msecs_to_jiffies(tmp_timeout * MSEC_PER_SEC);
	else
		hdev->timeout_jiffies = MAX_SCHEDULE_TIMEOUT;

	hdev->stop_on_err = true;
	hdev->reset_info.curr_reset_cause = HL_RESET_CAUSE_UNKNOWN;
	hdev->reset_info.prev_reset_trigger = HL_RESET_TRIGGER_DEFAULT;

	/* Enable only after the initialization of the device */
	hdev->disabled = true;

	if (!(hdev->fw_components & FW_TYPE_PREBOOT_CPU) &&
			(hdev->fw_components & ~FW_TYPE_PREBOOT_CPU)) {
		pr_err("Preboot must be set along with other components");
		return -EINVAL;
	}

	/* If CPU queues not enabled, no way to do heartbeat */
	if (!hdev->cpu_queues_enable)
		hdev->heartbeat = 0;
	fixup_device_params_per_asic(hdev, tmp_timeout);

	return 0;
}

/**
 * create_hdev - create habanalabs device instance
 *
 * @dev: will hold the pointer to the new habanalabs device structure
 * @pdev: pointer to the pci device
 *
 * Allocate memory for habanalabs device and initialize basic fields
 * Identify the ASIC type
 * Allocate ID (minor) for the device (only for real devices)
 */
static int create_hdev(struct hl_device **dev, struct pci_dev *pdev)
{
	int main_id, ctrl_id = 0, rc = 0;
	struct hl_device *hdev;

	*dev = NULL;

	hdev = kzalloc(sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;

	/* Will be NULL in case of simulator device */
	hdev->pdev = pdev;

	/* Assign status description string */
	strncpy(hdev->status[HL_DEVICE_STATUS_OPERATIONAL], "operational", HL_STR_MAX);
	strncpy(hdev->status[HL_DEVICE_STATUS_IN_RESET], "in reset", HL_STR_MAX);
	strncpy(hdev->status[HL_DEVICE_STATUS_MALFUNCTION], "disabled", HL_STR_MAX);
	strncpy(hdev->status[HL_DEVICE_STATUS_NEEDS_RESET], "needs reset", HL_STR_MAX);
	strncpy(hdev->status[HL_DEVICE_STATUS_IN_DEVICE_CREATION],
					"in device creation", HL_STR_MAX);
	strncpy(hdev->status[HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE],
					"in reset after device release", HL_STR_MAX);


	/* First, we must find out which ASIC are we handling. This is needed
	 * to configure the behavior of the driver (kernel parameters)
	 */
	hdev->asic_type = get_asic_type(hdev);
	if (hdev->asic_type == ASIC_INVALID) {
		dev_err(&pdev->dev, "Unsupported ASIC\n");
		rc = -ENODEV;
		goto free_hdev;
	}

	copy_kernel_module_params_to_device(hdev);

	set_driver_behavior_per_device(hdev);

	fixup_device_params(hdev);

	mutex_lock(&hl_devs_idr_lock);

	/* Always save 2 numbers, 1 for main device and 1 for control.
	 * They must be consecutive
	 */
	main_id = idr_alloc(&hl_devs_idr, hdev, 0, HL_MAX_MINORS, GFP_KERNEL);

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
static void destroy_hdev(struct hl_device *hdev)
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

/**
 * hl_pci_probe - probe PCI habanalabs devices
 *
 * @pdev: pointer to pci device
 * @id: pointer to pci device id structure
 *
 * Standard PCI probe function for habanalabs device.
 * Create a new habanalabs device and initialize it according to the
 * device's type
 */
static int hl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct hl_device *hdev;
	int rc;

	dev_info(&pdev->dev, HL_NAME
		 " device found [%04x:%04x] (rev %x)\n",
		 (int)pdev->vendor, (int)pdev->device, (int)pdev->revision);

	rc = create_hdev(&hdev, pdev);
	if (rc)
		return rc;

	pci_set_drvdata(pdev, hdev);

	rc = hl_device_init(hdev);
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

/**
 * hl_pci_err_detected - a PCI bus error detected on this device
 *
 * @pdev: pointer to pci device
 * @state: PCI error type
 *
 * Called by the PCI subsystem whenever a non-correctable
 * PCI bus error is detected
 */
static pci_ers_result_t
hl_pci_err_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct hl_device *hdev = pci_get_drvdata(pdev);
	enum pci_ers_result result;

	switch (state) {
	case pci_channel_io_normal:
		dev_warn(hdev->dev, "PCI normal state error detected\n");
		return PCI_ERS_RESULT_CAN_RECOVER;

	case pci_channel_io_frozen:
		dev_warn(hdev->dev, "PCI frozen state error detected\n");
		result = PCI_ERS_RESULT_NEED_RESET;
		break;

	case pci_channel_io_perm_failure:
		dev_warn(hdev->dev, "PCI failure state error detected\n");
		result = PCI_ERS_RESULT_DISCONNECT;
		break;

	default:
		result = PCI_ERS_RESULT_NONE;
	}

	hdev->asic_funcs->halt_engines(hdev, true, false);

	return result;
}

/**
 * hl_pci_err_resume - resume after a PCI slot reset
 *
 * @pdev: pointer to pci device
 *
 */
static void hl_pci_err_resume(struct pci_dev *pdev)
{
	struct hl_device *hdev = pci_get_drvdata(pdev);

	dev_warn(hdev->dev, "Resuming device after PCI slot reset\n");
	hl_device_resume(hdev);
}

/**
 * hl_pci_err_slot_reset - a PCI slot reset has just happened
 *
 * @pdev: pointer to pci device
 *
 * Determine if the driver can recover from the PCI slot reset
 */
static pci_ers_result_t hl_pci_err_slot_reset(struct pci_dev *pdev)
{
	struct hl_device *hdev = pci_get_drvdata(pdev);

	dev_warn(hdev->dev, "PCI slot reset detected\n");

	return PCI_ERS_RESULT_RECOVERED;
}

static const struct dev_pm_ops hl_pm_ops = {
	.suspend = hl_pmops_suspend,
	.resume = hl_pmops_resume,
};

static const struct pci_error_handlers hl_pci_err_handler = {
	.error_detected = hl_pci_err_detected,
	.slot_reset = hl_pci_err_slot_reset,
	.resume = hl_pci_err_resume,
};

static struct pci_driver hl_pci_driver = {
	.name = HL_NAME,
	.id_table = ids,
	.probe = hl_pci_probe,
	.remove = hl_pci_remove,
	.shutdown = hl_pci_remove,
	.driver = {
		.name = HL_NAME,
		.pm = &hl_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.err_handler = &hl_pci_err_handler,
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

	hl_class = class_create(HL_NAME);
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
