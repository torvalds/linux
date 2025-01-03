// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_pfvf_pf_msg.h"

struct adf_fatal_error_data {
	struct adf_accel_dev *accel_dev;
	struct work_struct work;
};

static struct workqueue_struct *device_reset_wq;
static struct workqueue_struct *device_sriov_wq;

static pci_ers_result_t adf_error_detected(struct pci_dev *pdev,
					   pci_channel_state_t state)
{
	struct adf_accel_dev *accel_dev = adf_devmgr_pci_to_accel_dev(pdev);

	dev_info(&pdev->dev, "Acceleration driver hardware error detected.\n");
	if (!accel_dev) {
		dev_err(&pdev->dev, "Can't find acceleration device\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	if (state == pci_channel_io_perm_failure) {
		dev_err(&pdev->dev, "Can't recover from device error\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	set_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
	if (accel_dev->hw_device->exit_arb) {
		dev_dbg(&pdev->dev, "Disabling arbitration\n");
		accel_dev->hw_device->exit_arb(accel_dev);
	}
	adf_error_notifier(accel_dev);
	adf_pf2vf_notify_fatal_error(accel_dev);
	adf_dev_restarting_notify(accel_dev);
	adf_pf2vf_notify_restarting(accel_dev);
	adf_pf2vf_wait_for_restarting_complete(accel_dev);
	pci_clear_master(pdev);
	adf_dev_down(accel_dev);

	return PCI_ERS_RESULT_NEED_RESET;
}

/* reset dev data */
struct adf_reset_dev_data {
	int mode;
	struct adf_accel_dev *accel_dev;
	struct completion compl;
	struct work_struct reset_work;
};

/* sriov dev data */
struct adf_sriov_dev_data {
	struct adf_accel_dev *accel_dev;
	struct completion compl;
	struct work_struct sriov_work;
};

void adf_reset_sbr(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);
	struct pci_dev *parent = pdev->bus->self;
	u16 bridge_ctl = 0;

	if (!parent)
		parent = pdev;

	if (!pci_wait_for_pending_transaction(pdev))
		dev_info(&GET_DEV(accel_dev),
			 "Transaction still in progress. Proceeding\n");

	dev_info(&GET_DEV(accel_dev), "Secondary bus reset\n");

	pci_read_config_word(parent, PCI_BRIDGE_CONTROL, &bridge_ctl);
	bridge_ctl |= PCI_BRIDGE_CTL_BUS_RESET;
	pci_write_config_word(parent, PCI_BRIDGE_CONTROL, bridge_ctl);
	msleep(100);
	bridge_ctl &= ~PCI_BRIDGE_CTL_BUS_RESET;
	pci_write_config_word(parent, PCI_BRIDGE_CONTROL, bridge_ctl);
	msleep(100);
}
EXPORT_SYMBOL_GPL(adf_reset_sbr);

void adf_reset_flr(struct adf_accel_dev *accel_dev)
{
	pcie_flr(accel_to_pci_dev(accel_dev));
}
EXPORT_SYMBOL_GPL(adf_reset_flr);

void adf_dev_restore(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);

	if (hw_device->reset_device) {
		dev_info(&GET_DEV(accel_dev), "Resetting device qat_dev%d\n",
			 accel_dev->accel_id);
		hw_device->reset_device(accel_dev);
		pci_restore_state(pdev);
		pci_save_state(pdev);
	}
}

static void adf_device_sriov_worker(struct work_struct *work)
{
	struct adf_sriov_dev_data *sriov_data =
		container_of(work, struct adf_sriov_dev_data, sriov_work);

	adf_reenable_sriov(sriov_data->accel_dev);
	complete(&sriov_data->compl);
}

static void adf_device_reset_worker(struct work_struct *work)
{
	struct adf_reset_dev_data *reset_data =
		  container_of(work, struct adf_reset_dev_data, reset_work);
	struct adf_accel_dev *accel_dev = reset_data->accel_dev;
	unsigned long wait_jiffies = msecs_to_jiffies(10000);
	struct adf_sriov_dev_data sriov_data;

	adf_dev_restarting_notify(accel_dev);
	if (adf_dev_restart(accel_dev)) {
		/* The device hanged and we can't restart it so stop here */
		dev_err(&GET_DEV(accel_dev), "Restart device failed\n");
		if (reset_data->mode == ADF_DEV_RESET_ASYNC)
			kfree(reset_data);
		WARN(1, "QAT: device restart failed. Device is unusable\n");
		return;
	}

	sriov_data.accel_dev = accel_dev;
	init_completion(&sriov_data.compl);
	INIT_WORK(&sriov_data.sriov_work, adf_device_sriov_worker);
	queue_work(device_sriov_wq, &sriov_data.sriov_work);
	if (wait_for_completion_timeout(&sriov_data.compl, wait_jiffies))
		adf_pf2vf_notify_restarted(accel_dev);

	adf_dev_restarted_notify(accel_dev);
	clear_bit(ADF_STATUS_RESTARTING, &accel_dev->status);

	/* The dev is back alive. Notify the caller if in sync mode */
	if (reset_data->mode == ADF_DEV_RESET_ASYNC)
		kfree(reset_data);
	else
		complete(&reset_data->compl);
}

static int adf_dev_aer_schedule_reset(struct adf_accel_dev *accel_dev,
				      enum adf_dev_reset_mode mode)
{
	struct adf_reset_dev_data *reset_data;

	if (!adf_dev_started(accel_dev) ||
	    test_bit(ADF_STATUS_RESTARTING, &accel_dev->status))
		return 0;

	set_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
	reset_data = kzalloc(sizeof(*reset_data), GFP_KERNEL);
	if (!reset_data)
		return -ENOMEM;
	reset_data->accel_dev = accel_dev;
	init_completion(&reset_data->compl);
	reset_data->mode = mode;
	INIT_WORK(&reset_data->reset_work, adf_device_reset_worker);
	queue_work(device_reset_wq, &reset_data->reset_work);

	/* If in sync mode wait for the result */
	if (mode == ADF_DEV_RESET_SYNC) {
		int ret = 0;
		/* Maximum device reset time is 10 seconds */
		unsigned long wait_jiffies = msecs_to_jiffies(10000);
		unsigned long timeout = wait_for_completion_timeout(
				   &reset_data->compl, wait_jiffies);
		if (!timeout) {
			dev_err(&GET_DEV(accel_dev),
				"Reset device timeout expired\n");
			cancel_work_sync(&reset_data->reset_work);
			ret = -EFAULT;
		}
		kfree(reset_data);
		return ret;
	}
	return 0;
}

static pci_ers_result_t adf_slot_reset(struct pci_dev *pdev)
{
	struct adf_accel_dev *accel_dev = adf_devmgr_pci_to_accel_dev(pdev);
	int res = 0;

	if (!accel_dev) {
		pr_err("QAT: Can't find acceleration device\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	if (!pdev->is_busmaster)
		pci_set_master(pdev);
	pci_restore_state(pdev);
	pci_save_state(pdev);
	res = adf_dev_up(accel_dev, false);
	if (res && res != -EALREADY)
		return PCI_ERS_RESULT_DISCONNECT;

	adf_reenable_sriov(accel_dev);
	adf_pf2vf_notify_restarted(accel_dev);
	adf_dev_restarted_notify(accel_dev);
	clear_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
	return PCI_ERS_RESULT_RECOVERED;
}

static void adf_resume(struct pci_dev *pdev)
{
	dev_info(&pdev->dev, "Acceleration driver reset completed\n");
	dev_info(&pdev->dev, "Device is up and running\n");
}

const struct pci_error_handlers adf_err_handler = {
	.error_detected = adf_error_detected,
	.slot_reset = adf_slot_reset,
	.resume = adf_resume,
};
EXPORT_SYMBOL_GPL(adf_err_handler);

int adf_dev_autoreset(struct adf_accel_dev *accel_dev)
{
	if (accel_dev->autoreset_on_error)
		return adf_dev_aer_schedule_reset(accel_dev, ADF_DEV_RESET_ASYNC);

	return 0;
}

static void adf_notify_fatal_error_worker(struct work_struct *work)
{
	struct adf_fatal_error_data *wq_data =
			container_of(work, struct adf_fatal_error_data, work);
	struct adf_accel_dev *accel_dev = wq_data->accel_dev;
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;

	adf_error_notifier(accel_dev);

	if (!accel_dev->is_vf) {
		/* Disable arbitration to stop processing of new requests */
		if (accel_dev->autoreset_on_error && hw_device->exit_arb)
			hw_device->exit_arb(accel_dev);
		if (accel_dev->pf.vf_info)
			adf_pf2vf_notify_fatal_error(accel_dev);
		adf_dev_autoreset(accel_dev);
	}

	kfree(wq_data);
}

int adf_notify_fatal_error(struct adf_accel_dev *accel_dev)
{
	struct adf_fatal_error_data *wq_data;

	wq_data = kzalloc(sizeof(*wq_data), GFP_ATOMIC);
	if (!wq_data)
		return -ENOMEM;

	wq_data->accel_dev = accel_dev;
	INIT_WORK(&wq_data->work, adf_notify_fatal_error_worker);
	adf_misc_wq_queue_work(&wq_data->work);

	return 0;
}

int adf_init_aer(void)
{
	device_reset_wq = alloc_workqueue("qat_device_reset_wq",
					  WQ_MEM_RECLAIM, 0);
	if (!device_reset_wq)
		return -EFAULT;

	device_sriov_wq = alloc_workqueue("qat_device_sriov_wq", 0, 0);
	if (!device_sriov_wq) {
		destroy_workqueue(device_reset_wq);
		device_reset_wq = NULL;
		return -EFAULT;
	}

	return 0;
}

void adf_exit_aer(void)
{
	if (device_reset_wq)
		destroy_workqueue(device_reset_wq);
	device_reset_wq = NULL;

	if (device_sriov_wq)
		destroy_workqueue(device_sriov_wq);
	device_sriov_wq = NULL;
}
