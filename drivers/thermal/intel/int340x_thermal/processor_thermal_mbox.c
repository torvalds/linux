// SPDX-License-Identifier: GPL-2.0-only
/*
 * processor thermal device mailbox driver for Workload type hints
 * Copyright (c) 2020, Intel Corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include "processor_thermal_device.h"

#define MBOX_CMD_WORKLOAD_TYPE_READ	0x0E
#define MBOX_CMD_WORKLOAD_TYPE_WRITE	0x0F

#define MBOX_OFFSET_DATA		0x5810
#define MBOX_OFFSET_INTERFACE		0x5818

#define MBOX_BUSY_BIT			31
#define MBOX_RETRY_COUNT		100

#define MBOX_DATA_BIT_VALID		31
#define MBOX_DATA_BIT_AC_DC		30

static DEFINE_MUTEX(mbox_lock);

static int send_mbox_cmd(struct pci_dev *pdev, u16 cmd_id, u32 cmd_data, u64 *cmd_resp)
{
	struct proc_thermal_device *proc_priv;
	u32 retries, data;
	int ret;

	mutex_lock(&mbox_lock);
	proc_priv = pci_get_drvdata(pdev);

	/* Poll for rb bit == 0 */
	retries = MBOX_RETRY_COUNT;
	do {
		data = readl((void __iomem *) (proc_priv->mmio_base + MBOX_OFFSET_INTERFACE));
		if (data & BIT_ULL(MBOX_BUSY_BIT)) {
			ret = -EBUSY;
			continue;
		}
		ret = 0;
		break;
	} while (--retries);

	if (ret)
		goto unlock_mbox;

	if (cmd_id == MBOX_CMD_WORKLOAD_TYPE_WRITE)
		writel(cmd_data, (void __iomem *) ((proc_priv->mmio_base + MBOX_OFFSET_DATA)));

	/* Write command register */
	data = BIT_ULL(MBOX_BUSY_BIT) | cmd_id;
	writel(data, (void __iomem *) ((proc_priv->mmio_base + MBOX_OFFSET_INTERFACE)));

	/* Poll for rb bit == 0 */
	retries = MBOX_RETRY_COUNT;
	do {
		data = readl((void __iomem *) (proc_priv->mmio_base + MBOX_OFFSET_INTERFACE));
		if (data & BIT_ULL(MBOX_BUSY_BIT)) {
			ret = -EBUSY;
			continue;
		}

		if (data) {
			ret = -ENXIO;
			goto unlock_mbox;
		}

		ret = 0;

		if (!cmd_resp)
			break;

		if (cmd_id == MBOX_CMD_WORKLOAD_TYPE_READ)
			*cmd_resp = readl((void __iomem *) (proc_priv->mmio_base + MBOX_OFFSET_DATA));
		else
			*cmd_resp = readq((void __iomem *) (proc_priv->mmio_base + MBOX_OFFSET_DATA));

		break;
	} while (--retries);

unlock_mbox:
	mutex_unlock(&mbox_lock);
	return ret;
}

int processor_thermal_send_mbox_cmd(struct pci_dev *pdev, u16 cmd_id, u32 cmd_data, u64 *cmd_resp)
{
	return send_mbox_cmd(pdev, cmd_id, cmd_data, cmd_resp);
}
EXPORT_SYMBOL_GPL(processor_thermal_send_mbox_cmd);

/* List of workload types */
static const char * const workload_types[] = {
	"none",
	"idle",
	"semi_active",
	"bursty",
	"sustained",
	"battery_life",
	NULL
};


static ssize_t workload_available_types_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	int i = 0;
	int ret = 0;

	while (workload_types[i] != NULL)
		ret += sprintf(&buf[ret], "%s ", workload_types[i++]);

	ret += sprintf(&buf[ret], "\n");

	return ret;
}

static DEVICE_ATTR_RO(workload_available_types);

static ssize_t workload_type_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	char str_preference[15];
	u32 data = 0;
	ssize_t ret;

	ret = sscanf(buf, "%14s", str_preference);
	if (ret != 1)
		return -EINVAL;

	ret = match_string(workload_types, -1, str_preference);
	if (ret < 0)
		return ret;

	ret &= 0xff;

	if (ret)
		data = BIT(MBOX_DATA_BIT_VALID) | BIT(MBOX_DATA_BIT_AC_DC);

	data |= ret;

	ret = send_mbox_cmd(pdev, MBOX_CMD_WORKLOAD_TYPE_WRITE, data, NULL);
	if (ret)
		return false;

	return count;
}

static ssize_t workload_type_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	u64 cmd_resp;
	int ret;

	ret = send_mbox_cmd(pdev, MBOX_CMD_WORKLOAD_TYPE_READ, 0, &cmd_resp);
	if (ret)
		return false;

	cmd_resp &= 0xff;

	if (cmd_resp > ARRAY_SIZE(workload_types) - 1)
		return -EINVAL;

	return sprintf(buf, "%s\n", workload_types[cmd_resp]);
}

static DEVICE_ATTR_RW(workload_type);

static struct attribute *workload_req_attrs[] = {
	&dev_attr_workload_available_types.attr,
	&dev_attr_workload_type.attr,
	NULL
};

static const struct attribute_group workload_req_attribute_group = {
	.attrs = workload_req_attrs,
	.name = "workload_request"
};



static bool workload_req_created;

int proc_thermal_mbox_add(struct pci_dev *pdev, struct proc_thermal_device *proc_priv)
{
	u64 cmd_resp;
	int ret;

	/* Check if there is a mailbox support, if fails return success */
	ret = send_mbox_cmd(pdev, MBOX_CMD_WORKLOAD_TYPE_READ, 0, &cmd_resp);
	if (ret)
		return 0;

	ret = sysfs_create_group(&pdev->dev.kobj, &workload_req_attribute_group);
	if (ret)
		return ret;

	workload_req_created = true;

	return 0;
}
EXPORT_SYMBOL_GPL(proc_thermal_mbox_add);

void proc_thermal_mbox_remove(struct pci_dev *pdev)
{
	if (workload_req_created)
		sysfs_remove_group(&pdev->dev.kobj, &workload_req_attribute_group);

	workload_req_created = false;

}
EXPORT_SYMBOL_GPL(proc_thermal_mbox_remove);

MODULE_LICENSE("GPL v2");
