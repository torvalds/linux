// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/err.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/ipc_logging.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include "internal.h"

#define OCP_LOG_ENTRY_SIZE	2
#define IPC_LOG_PAGES		3

struct ocp_log_entry {
	u16	ppid;
	u8	mode_at_ocp;
};

struct nvmem_log_info {
	struct nvmem_cell	*nvmem_cell;
	int			entry_count;
	void			*ipc_log;
};

struct ocp_notifier_dev {
	struct device		*dev;
	struct idr		regulators;
	struct nvmem_log_info	ocp;
	struct nvmem_log_info	alarm;
};

static const char *rdev_name(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->name)
		return rdev->constraints->name;
	else if (rdev->desc->name)
		return rdev->desc->name;
	else
		return "";
}

#define BUF_SIZE 16

static int ocp_notifier_get_regulator_name(struct ocp_notifier_dev *ocp_dev,
					   int ppid, const char **name)
{
	struct regulator_dev *rdev;
	struct regulator *reg;
	char buf[BUF_SIZE];
	int ret;

	rdev = idr_find(&ocp_dev->regulators, ppid);

	if (!rdev) {
		buf[0] = '\0';
		scnprintf(buf, BUF_SIZE, "periph-%03x", ppid);
		reg = regulator_get_optional(ocp_dev->dev, buf);
		if (IS_ERR(reg)) {
			ret = PTR_ERR(reg);
			if (ret == -EPROBE_DEFER)
				return ret;
			/* Ignore case of unspecified supply mapping */
			dev_dbg(ocp_dev->dev, "failed to get %s-supply, ret=%d\n",
				buf, ret);
		} else {
			rdev = reg->rdev;
			regulator_put(reg);
			ret = idr_alloc(&ocp_dev->regulators, rdev, ppid,
					ppid + 1, GFP_KERNEL);
			if (ret < 0)
				return ret;
			else if (ret != ppid)
				return -EINVAL;
		}
	}

	if (rdev)
		*name = rdev_name(rdev);

	return 0;
}

static int ocp_notifier_log_event(struct ocp_notifier_dev *ocp_dev,
				  struct nvmem_log_info *log_info,
				  struct ocp_log_entry *entry,
				  const char *label)
{
	const char *name = NULL;
	int ret;

	if (entry->ppid == 0)
		return 0;

	ret = ocp_notifier_get_regulator_name(ocp_dev, entry->ppid, &name);
	if (ret)
		return ret;

	if (name) {
		pr_err_ratelimited("%s name=%s, ppid=0x%03X, mode=%u\n",
			label, name, entry->ppid, entry->mode_at_ocp);
		ipc_log_string(log_info->ipc_log, "%s name=%s, ppid=0x%03X, mode=%u\n",
			label, name, entry->ppid, entry->mode_at_ocp);
	} else {
		pr_err_ratelimited("%s ppid=0x%03X, mode=%u\n",
			label, entry->ppid, entry->mode_at_ocp);
		ipc_log_string(log_info->ipc_log, "%s ppid=0x%03X, mode=%u\n",
			label, entry->ppid, entry->mode_at_ocp);
	}

	return 0;
}

static int ocp_notifier_read_entry(struct ocp_notifier_dev *ocp_dev,
				   struct nvmem_log_info *log_info,
				   u32 index,
				   struct ocp_log_entry *entry)
{
	size_t len = 0;
	u8 *buf;
	int ret, i;

	if (index >= log_info->entry_count)
		return -EINVAL;

	buf = nvmem_cell_read(log_info->nvmem_cell, &len);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		dev_err(ocp_dev->dev, "failed to read nvmem cell, ret=%d\n", ret);
		return ret;
	}

	i = index * OCP_LOG_ENTRY_SIZE;
	if (i + 1 >= len) {
		dev_err(ocp_dev->dev, "invalid OCP log index=%i\n", i);
		kfree(buf);
		return -EINVAL;
	}

	/*
	 * OCP log entry layout:
	 * Byte 0:	[7:4] - SID
	 *		[2:0] - mode at OCP
	 * Byte 1:	[7:0] - PID
	 */
	entry->ppid = (((u16)buf[i] << 4) & 0xF00) | buf[i + 1];
	entry->mode_at_ocp = buf[i] & 0x7;

	kfree(buf);

	return 0;
}

static irqreturn_t ocp_notifier_handler(int irq, void *data)
{
	struct ocp_notifier_dev *ocp_dev = data;
	struct ocp_log_entry entry = {0};
	struct regulator_dev *rdev;
	int ret;

	ret = ocp_notifier_read_entry(ocp_dev, &ocp_dev->ocp, 0, &entry);
	if (ret)
		goto done;

	ret = ocp_notifier_log_event(ocp_dev, &ocp_dev->ocp, &entry,
				     "Regulator OCP during runtime:");
	if (ret)
		goto done;

	rdev = idr_find(&ocp_dev->regulators, entry.ppid);
	if (!rdev)
		goto done;

	regulator_notifier_call_chain(rdev, REGULATOR_EVENT_OVER_CURRENT, NULL);
done:
	return IRQ_HANDLED;
}

static irqreturn_t alarm_notifier_handler(int irq, void *data)
{
	struct ocp_notifier_dev *ocp_dev = data;
	struct ocp_log_entry entry = {0};
	struct regulator_dev *rdev;
	int ret;

	ret = ocp_notifier_read_entry(ocp_dev, &ocp_dev->alarm, 0, &entry);
	if (ret)
		goto done;

	ret = ocp_notifier_log_event(ocp_dev, &ocp_dev->alarm, &entry,
				     "Regulator alarm during runtime:");
	if (ret)
		goto done;

	rdev = idr_find(&ocp_dev->regulators, entry.ppid);
	if (!rdev)
		goto done;

	regulator_notifier_call_chain(rdev, REGULATOR_EVENT_UNDER_VOLTAGE,
				      NULL);
done:
	return IRQ_HANDLED;
}

static int alarm_notifier_init(struct platform_device *pdev,
			       struct ocp_notifier_dev *ocp_dev)
{
	struct ocp_log_entry entry = {0};
	size_t len = 0;
	int ret, i, irq;
	u8 *buf;

	ocp_dev->alarm.nvmem_cell = devm_nvmem_cell_get(&pdev->dev,
							"alarm_log");
	if (IS_ERR(ocp_dev->alarm.nvmem_cell)) {
		ret = PTR_ERR(ocp_dev->alarm.nvmem_cell);
		if (ret == -EPROBE_DEFER)
			return ret;
		/* Alarm event details are optional */
		ocp_dev->alarm.nvmem_cell = NULL;
		return 0;
	}

	irq = platform_get_irq(pdev, 1);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get alarm irq, ret=%d\n", irq);
		return irq;
	}

	ocp_dev->alarm.ipc_log = ipc_log_context_create(IPC_LOG_PAGES,
							"regulator_alarm", 0);

	buf = nvmem_cell_read(ocp_dev->alarm.nvmem_cell, &len);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		dev_err(&pdev->dev, "failed to read alarm nvmem cell, ret=%d\n", ret);
		goto free_log;
	}
	ocp_dev->alarm.entry_count = len / OCP_LOG_ENTRY_SIZE;
	kfree(buf);

	for (i = 0; i < ocp_dev->alarm.entry_count; i++) {
		ret = ocp_notifier_read_entry(ocp_dev, &ocp_dev->alarm, i,
					      &entry);
		if (ret)
			goto free_log;

		ret = ocp_notifier_log_event(ocp_dev, &ocp_dev->alarm, &entry,
				"Regulator alarm event before kernel boot:");
		if (ret) {
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "failed to log alarm entry %d, ret=%d\n",
					i, ret);
			goto free_log;
		}
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					alarm_notifier_handler, IRQF_ONESHOT,
					"regulator-alarm", ocp_dev);
	if (ret)
		goto free_log;

	return 0;

free_log:
	ipc_log_context_destroy(ocp_dev->alarm.ipc_log);
	return ret;
}

static int ocp_notifier_probe(struct platform_device *pdev)
{
	struct ocp_notifier_dev *ocp_dev;
	struct ocp_log_entry entry = {0};
	size_t len = 0;
	int ret, i, irq;
	u8 *buf;

	ocp_dev = devm_kzalloc(&pdev->dev, sizeof(*ocp_dev), GFP_KERNEL);
	if (!ocp_dev)
		return -ENOMEM;
	ocp_dev->dev = &pdev->dev;

	ocp_dev->ocp.nvmem_cell = devm_nvmem_cell_get(&pdev->dev, "ocp_log");
	if (IS_ERR(ocp_dev->ocp.nvmem_cell)) {
		ret = PTR_ERR(ocp_dev->ocp.nvmem_cell);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get nvmem cell, ret=%d\n",
				ret);
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get OCP irq, ret=%d\n", irq);
		return irq;
	}

	ocp_dev->ocp.ipc_log = ipc_log_context_create(IPC_LOG_PAGES,
						      "regulator_ocp", 0);
	platform_set_drvdata(pdev, ocp_dev);

	buf = nvmem_cell_read(ocp_dev->ocp.nvmem_cell, &len);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		dev_err(&pdev->dev, "failed to read OCP nvmem cell, ret=%d\n", ret);
		goto free_log;
	}
	ocp_dev->ocp.entry_count = len / OCP_LOG_ENTRY_SIZE;
	kfree(buf);

	idr_init(&ocp_dev->regulators);

	for (i = 0; i < ocp_dev->ocp.entry_count; i++) {
		ret = ocp_notifier_read_entry(ocp_dev, &ocp_dev->ocp, i,
					      &entry);
		if (ret)
			goto free_idr;

		ret = ocp_notifier_log_event(ocp_dev, &ocp_dev->ocp, &entry,
				"Regulator OCP event before kernel boot:");
		if (ret) {
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "failed to log OCP entry %d, ret=%d\n",
					i, ret);
			goto free_idr;
		}
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					ocp_notifier_handler, IRQF_ONESHOT,
					"regulator-ocp", ocp_dev);
	if (ret)
		goto free_idr;

	ret = alarm_notifier_init(pdev, ocp_dev);
	if (ret)
		goto free_idr;

	return 0;

free_idr:
	idr_destroy(&ocp_dev->regulators);
free_log:
	ipc_log_context_destroy(ocp_dev->ocp.ipc_log);
	return ret;
}

static int ocp_notifier_remove(struct platform_device *pdev)
{
	struct ocp_notifier_dev *ocp_dev = platform_get_drvdata(pdev);

	ipc_log_context_destroy(ocp_dev->ocp.ipc_log);
	ipc_log_context_destroy(ocp_dev->alarm.ipc_log);
	idr_destroy(&ocp_dev->regulators);

	return 0;
}

static const struct of_device_id ocp_notifier_of_match[] = {
	{ .compatible = "qcom,regulator-ocp-notifier" },
	{}
};
MODULE_DEVICE_TABLE(of, ocp_notifier_of_match);

static struct platform_driver ocp_notifier_driver = {
	.driver = {
		.name = "qti-ocp-notifier",
		.of_match_table = of_match_ptr(ocp_notifier_of_match),
	},
	.probe = ocp_notifier_probe,
	.remove = ocp_notifier_remove,
};
module_platform_driver(ocp_notifier_driver);

MODULE_DESCRIPTION("QTI Regulator OCP Notifier Driver");
MODULE_LICENSE("GPL v2");
