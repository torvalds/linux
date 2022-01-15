// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select Interface: Mbox via PCI Interface
 * Copyright (c) 2019, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#include <linux/cpufeature.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <uapi/linux/isst_if.h>

#include "isst_if_common.h"

#define PUNIT_MAILBOX_DATA		0xA0
#define PUNIT_MAILBOX_INTERFACE		0xA4
#define PUNIT_MAILBOX_BUSY_BIT		31

/*
 * The average time to complete mailbox commands is less than 40us. Most of
 * the commands complete in few micro seconds. But the same firmware handles
 * requests from all power management features.
 * We can create a scenario where we flood the firmware with requests then
 * the mailbox response can be delayed for 100s of micro seconds. So define
 * two timeouts. One for average case and one for long.
 * If the firmware is taking more than average, just call cond_resched().
 */
#define OS_MAILBOX_TIMEOUT_AVG_US	40
#define OS_MAILBOX_TIMEOUT_MAX_US	1000

struct isst_if_device {
	struct mutex mutex;
};

static int isst_if_mbox_cmd(struct pci_dev *pdev,
			    struct isst_if_mbox_cmd *mbox_cmd)
{
	s64 tm_delta = 0;
	ktime_t tm;
	u32 data;
	int ret;

	/* Poll for rb bit == 0 */
	tm = ktime_get();
	do {
		ret = pci_read_config_dword(pdev, PUNIT_MAILBOX_INTERFACE,
					    &data);
		if (ret)
			return ret;

		if (data & BIT_ULL(PUNIT_MAILBOX_BUSY_BIT)) {
			ret = -EBUSY;
			tm_delta = ktime_us_delta(ktime_get(), tm);
			if (tm_delta > OS_MAILBOX_TIMEOUT_AVG_US)
				cond_resched();
			continue;
		}
		ret = 0;
		break;
	} while (tm_delta < OS_MAILBOX_TIMEOUT_MAX_US);

	if (ret)
		return ret;

	/* Write DATA register */
	ret = pci_write_config_dword(pdev, PUNIT_MAILBOX_DATA,
				     mbox_cmd->req_data);
	if (ret)
		return ret;

	/* Write command register */
	data = BIT_ULL(PUNIT_MAILBOX_BUSY_BIT) |
		      (mbox_cmd->parameter & GENMASK_ULL(13, 0)) << 16 |
		      (mbox_cmd->sub_command << 8) |
		      mbox_cmd->command;

	ret = pci_write_config_dword(pdev, PUNIT_MAILBOX_INTERFACE, data);
	if (ret)
		return ret;

	/* Poll for rb bit == 0 */
	tm_delta = 0;
	tm = ktime_get();
	do {
		ret = pci_read_config_dword(pdev, PUNIT_MAILBOX_INTERFACE,
					    &data);
		if (ret)
			return ret;

		if (data & BIT_ULL(PUNIT_MAILBOX_BUSY_BIT)) {
			ret = -EBUSY;
			tm_delta = ktime_us_delta(ktime_get(), tm);
			if (tm_delta > OS_MAILBOX_TIMEOUT_AVG_US)
				cond_resched();
			continue;
		}

		if (data & 0xff)
			return -ENXIO;

		ret = pci_read_config_dword(pdev, PUNIT_MAILBOX_DATA, &data);
		if (ret)
			return ret;

		mbox_cmd->resp_data = data;
		ret = 0;
		break;
	} while (tm_delta < OS_MAILBOX_TIMEOUT_MAX_US);

	return ret;
}

static long isst_if_mbox_proc_cmd(u8 *cmd_ptr, int *write_only, int resume)
{
	struct isst_if_mbox_cmd *mbox_cmd;
	struct isst_if_device *punit_dev;
	struct pci_dev *pdev;
	int ret;

	mbox_cmd = (struct isst_if_mbox_cmd *)cmd_ptr;

	if (isst_if_mbox_cmd_invalid(mbox_cmd))
		return -EINVAL;

	if (isst_if_mbox_cmd_set_req(mbox_cmd) && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	pdev = isst_if_get_pci_dev(mbox_cmd->logical_cpu, 1, 30, 1);
	if (!pdev)
		return -EINVAL;

	punit_dev = pci_get_drvdata(pdev);
	if (!punit_dev)
		return -EINVAL;

	/*
	 * Basically we are allowing one complete mailbox transaction on
	 * a mapped PCI device at a time.
	 */
	mutex_lock(&punit_dev->mutex);
	ret = isst_if_mbox_cmd(pdev, mbox_cmd);
	if (!ret && !resume && isst_if_mbox_cmd_set_req(mbox_cmd))
		ret = isst_store_cmd(mbox_cmd->command,
				     mbox_cmd->sub_command,
				     mbox_cmd->logical_cpu, 1,
				     mbox_cmd->parameter,
				     mbox_cmd->req_data);
	mutex_unlock(&punit_dev->mutex);
	if (ret)
		return ret;

	*write_only = 0;

	return 0;
}

static const struct pci_device_id isst_if_mbox_ids[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_CFG_MBOX_DEVID_0)},
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_CFG_MBOX_DEVID_1)},
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, isst_if_mbox_ids);

static int isst_if_mbox_probe(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	struct isst_if_device *punit_dev;
	struct isst_if_cmd_cb cb;
	int ret;

	punit_dev = devm_kzalloc(&pdev->dev, sizeof(*punit_dev), GFP_KERNEL);
	if (!punit_dev)
		return -ENOMEM;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	mutex_init(&punit_dev->mutex);
	pci_set_drvdata(pdev, punit_dev);

	memset(&cb, 0, sizeof(cb));
	cb.cmd_size = sizeof(struct isst_if_mbox_cmd);
	cb.offset = offsetof(struct isst_if_mbox_cmds, mbox_cmd);
	cb.cmd_callback = isst_if_mbox_proc_cmd;
	cb.owner = THIS_MODULE;
	ret = isst_if_cdev_register(ISST_IF_DEV_MBOX, &cb);

	if (ret)
		mutex_destroy(&punit_dev->mutex);

	return ret;
}

static void isst_if_mbox_remove(struct pci_dev *pdev)
{
	struct isst_if_device *punit_dev;

	punit_dev = pci_get_drvdata(pdev);
	isst_if_cdev_unregister(ISST_IF_DEV_MBOX);
	mutex_destroy(&punit_dev->mutex);
}

static int __maybe_unused isst_if_resume(struct device *device)
{
	isst_resume_common();
	return 0;
}

static SIMPLE_DEV_PM_OPS(isst_if_pm_ops, NULL, isst_if_resume);

static struct pci_driver isst_if_pci_driver = {
	.name			= "isst_if_mbox_pci",
	.id_table		= isst_if_mbox_ids,
	.probe			= isst_if_mbox_probe,
	.remove			= isst_if_mbox_remove,
	.driver.pm		= &isst_if_pm_ops,
};

module_pci_driver(isst_if_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel speed select interface pci mailbox driver");
