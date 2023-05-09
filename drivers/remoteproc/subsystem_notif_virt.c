// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include "qcom_common.h"

#define CLIENT_STATE_OFFSET 4
#define SUBSYS_STATE_OFFSET 8

static void __iomem *base_reg;

enum subsystem_type {
	VIRTUAL,
	NATIVE,
};

struct subsystem_descriptor {
	const char *name;
	u32 offset;
	enum subsystem_type type;
	struct notifier_block nb;
	void *handle;
	int ssr_irq;
	struct list_head subsystem_list;
	struct work_struct work;
};

static LIST_HEAD(subsystem_descriptor_list);
static struct workqueue_struct *ssr_wq;

static void subsystem_notif_wq_func(struct work_struct *work)
{
	struct subsystem_descriptor *subsystem =
		container_of(work, struct subsystem_descriptor, work);
	void *subsystem_handle;
	int state, ret;

	state = readl_relaxed(base_reg + subsystem->offset);
	subsystem_handle = qcom_ssr_get_subsys(subsystem->name);
	ret = qcom_notify_ssr_clients(subsystem_handle, state, NULL);
	writel_relaxed(ret, base_reg + subsystem->offset + CLIENT_STATE_OFFSET);
}

static int subsystem_state_callback(struct notifier_block *this,
		unsigned long value, void *priv)
{
	struct subsystem_descriptor *subsystem =
		container_of(this, struct subsystem_descriptor, nb);

	writel_relaxed(value, base_reg + subsystem->offset +
			SUBSYS_STATE_OFFSET);

	return NOTIFY_OK;
}

static irqreturn_t subsystem_restart_irq_handler(int irq, void *dev_id)
{
	struct subsystem_descriptor *subsystem = dev_id;

	queue_work(ssr_wq, &subsystem->work);

	return IRQ_HANDLED;
}

static int get_resources(struct platform_device *pdev)
{
	struct device_node *node;
	struct device_node *child = NULL;
	const char *ss_type;
	struct resource *res;
	struct subsystem_descriptor *subsystem = NULL;
	int ret = 0;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vdev_base");
	base_reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR_OR_NULL(base_reg)) {
		dev_err(&pdev->dev, "Memory mapping failed\n");
		return -ENOMEM;
	}

	node = pdev->dev.of_node;
	for_each_child_of_node(node, child) {

		subsystem = devm_kzalloc(&pdev->dev,
				sizeof(struct subsystem_descriptor),
				GFP_KERNEL);
		if (!subsystem)
			return -ENOMEM;

		subsystem->name =
			of_get_property(child, "subsys-name", NULL);
		if (IS_ERR_OR_NULL(subsystem->name)) {
			dev_err(&pdev->dev, "Could not find subsystem name\n");
			return -EINVAL;
		}

		ret = of_property_read_u32(child, "offset",
				&subsystem->offset);
		if (ret) {
			dev_err(&pdev->dev, "offset reading for %s failed\n",
					subsystem->name);
			return -EINVAL;
		}

		ret = of_property_read_string(child, "type",
				&ss_type);
		if (ret) {
			dev_err(&pdev->dev, "type reading for %s failed\n",
					subsystem->name);
			return -EINVAL;
		}

		if (!strcmp(ss_type, "virtual"))
			subsystem->type = VIRTUAL;

		if (!strcmp(ss_type, "native"))
			subsystem->type = NATIVE;

		switch (subsystem->type) {
		case NATIVE:
			subsystem->nb.notifier_call =
				subsystem_state_callback;

			subsystem->handle =
				qcom_register_ssr_notifier(
					subsystem->name, &subsystem->nb);
			if (IS_ERR_OR_NULL(subsystem->handle)) {
				dev_err(&pdev->dev,
					"Could not register SSR notifier cb\n");
				return -EINVAL;
			}
			list_add_tail(&subsystem->subsystem_list,
					&subsystem_descriptor_list);
			break;
		case VIRTUAL:
			subsystem->ssr_irq =
				of_irq_get_byname(child, "state-irq");
			if (subsystem->ssr_irq < 0) {
				dev_err(&pdev->dev, "Could not find IRQ\n");
				return -EINVAL;
			}
			ret = devm_request_threaded_irq(&pdev->dev,
					subsystem->ssr_irq, NULL,
					subsystem_restart_irq_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					subsystem->name, subsystem);
			INIT_WORK(&subsystem->work, subsystem_notif_wq_func);
			break;
		default:
			dev_err(&pdev->dev, "Unsupported type %d\n",
				subsystem->type);
		}
	}

	return 0;
}

static void release_resources(void)
{
	struct subsystem_descriptor *subsystem, *node;

	list_for_each_entry_safe(subsystem, node, &subsystem_descriptor_list,
			subsystem_list) {
		qcom_unregister_ssr_notifier(subsystem->handle,
				&subsystem->nb);
		list_del(&subsystem->subsystem_list);
	}
}

static int subsys_notif_virt_probe(struct platform_device *pdev)
{
	int ret = 0;

	if (!pdev) {
		pr_err("%s: pdev is NULL\n", __func__);
		return -EINVAL;
	}

	ssr_wq = create_singlethread_workqueue("ssr_wq");
	if (!ssr_wq) {
		dev_err(&pdev->dev, "Workqueue creation failed\n");
		return -ENOMEM;
	}

	ret = get_resources(pdev);
	if (ret)
		destroy_workqueue(ssr_wq);

	return ret;
}

static int subsys_notif_virt_remove(struct platform_device *pdev)
{
	destroy_workqueue(ssr_wq);
	release_resources();

	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "qcom,subsys-notif-virt" },
	{},
};

static struct platform_driver subsys_notif_virt_driver = {
	.probe = subsys_notif_virt_probe,
	.remove = subsys_notif_virt_remove,
	.driver = {
		.name = "subsys_notif_virt",
		.of_match_table = match_table,
	},
};

static int __init subsys_notif_virt_init(void)
{
	return platform_driver_register(&subsys_notif_virt_driver);
}
module_init(subsys_notif_virt_init);

static void __exit subsys_notif_virt_exit(void)
{
	platform_driver_unregister(&subsys_notif_virt_driver);
}
module_exit(subsys_notif_virt_exit);

MODULE_DESCRIPTION("Subsystem Notification Virtual Driver");
MODULE_LICENSE("GPL");
