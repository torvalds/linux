// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/of.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/rpmsg/qcom_glink.h>
#include <linux/rpmsg.h>
#include <linux/ipc_logging.h>
#include <trace/events/rproc_qcom.h>

#define GLINK_PROBE_LOG_PAGE_CNT 4
static void *glink_ilc;

#define GLINK_INFO(x, ...) ipc_log_string(glink_ilc, x, ##__VA_ARGS__)

#define GLINK_ERR(dev, x, ...)					       \
do {								       \
	dev_err(dev, "[%s]: "x, __func__, ##__VA_ARGS__);	       \
	ipc_log_string(glink_ilc, "[%s]: "x, __func__, ##__VA_ARGS__); \
} while (0)

extern const struct dev_pm_ops glink_native_pm_ops;

struct edge_info {
	struct list_head list;
	struct device *dev;
	struct device_node *node;

	const char *glink_label;
	const char *ssr_label;
	void *glink;

	int (*register_fn)(struct edge_info *einfo);
	void (*unregister_fn)(struct edge_info *einfo);
	struct notifier_block nb;
	void *notifier_handle;
};
static LIST_HEAD(edge_infos);

static int glink_probe_ssr_cb(struct notifier_block *this,
			      unsigned long code, void *data)
{
	struct edge_info *einfo = container_of(this, struct edge_info, nb);

	GLINK_INFO("received %ld for %s\n", code, einfo->ssr_label);
	switch (code) {
	case QCOM_SSR_AFTER_POWERUP:
		trace_rproc_qcom_event(dev_name(einfo->dev),
				"QCOM_SSR_AFTER_POWERUP", "glink_probe_ssr-enter");
		einfo->register_fn(einfo);
		break;
	case QCOM_SSR_AFTER_SHUTDOWN:
		trace_rproc_qcom_event(dev_name(einfo->dev),
				"QCOM_SSR_AFTER_SHUTDOWN", "glink_probe_ssr-enter");
		einfo->unregister_fn(einfo);
		break;
	default:
		break;
	}

	trace_rproc_qcom_event(dev_name(einfo->dev), "glink_probe_ssr", "exit");
	return NOTIFY_DONE;
}

static int glink_probe_smem_reg(struct edge_info *einfo)
{
	struct device *dev = einfo->dev;

	einfo->glink = qcom_glink_smem_register(dev, einfo->node);
	if (IS_ERR_OR_NULL(einfo->glink)) {
		GLINK_ERR(dev, "register failed for %s\n", einfo->ssr_label);
		einfo->glink = NULL;
	}
	GLINK_INFO("register successful for %s\n", einfo->ssr_label);

	return 0;
}

static void glink_probe_smem_unreg(struct edge_info *einfo)
{
	if (einfo->glink)
		qcom_glink_smem_unregister(einfo->glink);

	einfo->glink = NULL;
	GLINK_INFO("unregister for %s\n", einfo->ssr_label);
}
static int glink_probe_spss_reg(struct edge_info *einfo)
{
	struct device *dev = einfo->dev;

	einfo->glink = qcom_glink_spss_register(dev, einfo->node);
	if (IS_ERR_OR_NULL(einfo->glink)) {
		GLINK_ERR(dev, "register failed for %s\n", einfo->ssr_label);
		einfo->glink = NULL;
	}

	return 0;
}

static void glink_probe_spss_unreg(struct edge_info *einfo)
{
	if (einfo->glink)
		qcom_glink_spss_unregister(einfo->glink);

	einfo->glink = NULL;
}

static void probe_subsystem(struct device *dev, struct device_node *np)
{
	struct edge_info *einfo;
	const char *transport;
	int ret;
	void *handle;

	einfo = devm_kzalloc(dev, sizeof(*einfo), GFP_KERNEL);
	if (!einfo)
		return;

	ret = of_property_read_string(np, "label", &einfo->ssr_label);
	if (ret < 0)
		einfo->ssr_label = np->name;

	ret = of_property_read_string(np, "qcom,glink-label",
				      &einfo->glink_label);
	if (ret < 0) {
		GLINK_ERR(dev, "no qcom,glink-label for %s\n",
			  einfo->ssr_label);
		return;
	}

	einfo->dev = dev;
	einfo->node = np;

	ret = of_property_read_string(np, "transport", &transport);
	if (ret < 0) {
		GLINK_ERR(dev, "%s missing transport\n", einfo->ssr_label);
		return;
	}

	if (!strcmp(transport, "smem")) {
		einfo->register_fn = glink_probe_smem_reg;
		einfo->unregister_fn = glink_probe_smem_unreg;
	} else if (!strcmp(transport, "spss")) {
		einfo->register_fn = glink_probe_spss_reg;
		einfo->unregister_fn = glink_probe_spss_unreg;
	}

	einfo->nb.notifier_call = glink_probe_ssr_cb;

	handle = qcom_register_ssr_notifier(einfo->ssr_label, &einfo->nb);
	if (IS_ERR_OR_NULL(handle)) {
		GLINK_ERR(dev, "could not register for SSR notifier for %s\n",
			  einfo->ssr_label);
		return;
	}

	einfo->notifier_handle = handle;

	list_add_tail(&einfo->list, &edge_infos);
	GLINK_INFO("probe successful for %s\n", einfo->ssr_label);
}

static int glink_probe(struct platform_device *pdev)
{
	struct device_node *pn = pdev->dev.of_node;
	struct device_node *cn;

	for_each_available_child_of_node(pn, cn) {
		probe_subsystem(&pdev->dev, cn);
	}
	return 0;
}

static int glink_remove(struct platform_device *pdev)
{
	struct device_node *pn = pdev->dev.of_node;
	struct device_node *cn;
	struct edge_info *einfo, *tmp;

	for_each_available_child_of_node(pn, cn) {
		list_for_each_entry_safe(einfo, tmp, &edge_infos, list) {
			qcom_unregister_ssr_notifier(einfo->notifier_handle,
							 &einfo->nb);
			list_del(&einfo->list);
		}
	}
	return 0;
}

static const struct of_device_id glink_match_table[] = {
	{ .compatible = "qcom,glink" },
	{},
};

static struct platform_driver glink_probe_driver = {
	.probe = glink_probe,
	.remove = glink_remove,
	.driver = {
		.name = "msm_glink",
		.of_match_table = glink_match_table,
		.pm = &glink_native_pm_ops,
	},
};

static int __init glink_probe_init(void)
{
	int ret;

	glink_ilc = ipc_log_context_create(GLINK_PROBE_LOG_PAGE_CNT,
					   "glink_probe", 0);

	ret = platform_driver_register(&glink_probe_driver);
	if (ret) {
		pr_err("%s: glink_probe register failed %d\n",
			__func__, ret);
		return ret;
	}

	return 0;
}
arch_initcall(glink_probe_init);

static void __exit glink_probe_exit(void)
{
	if (glink_ilc)
		ipc_log_context_destroy(glink_ilc);

	platform_driver_unregister(&glink_probe_driver);
}
module_exit(glink_probe_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. GLINK probe helper driver");
MODULE_LICENSE("GPL");
