// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Houlong Wei <houlong.wei@mediatek.com>
 *         Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/workqueue.h>
#include <soc/mediatek/smi.h>

#include "mtk_mdp_core.h"
#include "mtk_mdp_m2m.h"
#include "mtk_vpu.h"

/* MDP debug log level (0-3). 3 shows all the logs. */
int mtk_mdp_dbg_level;
EXPORT_SYMBOL(mtk_mdp_dbg_level);

module_param(mtk_mdp_dbg_level, int, 0644);

static const struct of_device_id mtk_mdp_comp_dt_ids[] = {
	{
		.compatible = "mediatek,mt8173-mdp-rdma",
		.data = (void *)MTK_MDP_RDMA
	}, {
		.compatible = "mediatek,mt8173-mdp-rsz",
		.data = (void *)MTK_MDP_RSZ
	}, {
		.compatible = "mediatek,mt8173-mdp-wdma",
		.data = (void *)MTK_MDP_WDMA
	}, {
		.compatible = "mediatek,mt8173-mdp-wrot",
		.data = (void *)MTK_MDP_WROT
	},
	{ },
};

static const struct of_device_id mtk_mdp_of_ids[] = {
	{ .compatible = "mediatek,mt8173-mdp", },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_mdp_of_ids);

static void mtk_mdp_clock_on(struct mtk_mdp_dev *mdp)
{
	struct device *dev = &mdp->pdev->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(mdp->comp); i++)
		mtk_mdp_comp_clock_on(dev, mdp->comp[i]);
}

static void mtk_mdp_clock_off(struct mtk_mdp_dev *mdp)
{
	struct device *dev = &mdp->pdev->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(mdp->comp); i++)
		mtk_mdp_comp_clock_off(dev, mdp->comp[i]);
}

static void mtk_mdp_wdt_worker(struct work_struct *work)
{
	struct mtk_mdp_dev *mdp =
			container_of(work, struct mtk_mdp_dev, wdt_work);
	struct mtk_mdp_ctx *ctx;

	mtk_mdp_err("Watchdog timeout");

	list_for_each_entry(ctx, &mdp->ctx_list, list) {
		mtk_mdp_dbg(0, "[%d] Change as state error", ctx->id);
		mtk_mdp_ctx_state_lock_set(ctx, MTK_MDP_CTX_ERROR);
	}
}

static void mtk_mdp_reset_handler(void *priv)
{
	struct mtk_mdp_dev *mdp = priv;

	queue_work(mdp->wdt_wq, &mdp->wdt_work);
}

static int mtk_mdp_probe(struct platform_device *pdev)
{
	struct mtk_mdp_dev *mdp;
	struct device *dev = &pdev->dev;
	struct device_node *node, *parent;
	int i, ret = 0;

	mdp = devm_kzalloc(dev, sizeof(*mdp), GFP_KERNEL);
	if (!mdp)
		return -ENOMEM;

	mdp->id = pdev->id;
	mdp->pdev = pdev;
	INIT_LIST_HEAD(&mdp->ctx_list);

	mutex_init(&mdp->lock);
	mutex_init(&mdp->vpulock);

	/* Old dts had the components as child nodes */
	node = of_get_next_child(dev->of_node, NULL);
	if (node) {
		of_node_put(node);
		parent = dev->of_node;
		dev_warn(dev, "device tree is out of date\n");
	} else {
		parent = dev->of_node->parent;
	}

	/* Iterate over sibling MDP function blocks */
	for_each_child_of_node(parent, node) {
		const struct of_device_id *of_id;
		enum mtk_mdp_comp_type comp_type;
		int comp_id;
		struct mtk_mdp_comp *comp;

		of_id = of_match_node(mtk_mdp_comp_dt_ids, node);
		if (!of_id)
			continue;

		if (!of_device_is_available(node)) {
			dev_err(dev, "Skipping disabled component %pOF\n",
				node);
			continue;
		}

		comp_type = (enum mtk_mdp_comp_type)of_id->data;
		comp_id = mtk_mdp_comp_get_id(dev, node, comp_type);
		if (comp_id < 0) {
			dev_warn(dev, "Skipping unknown component %pOF\n",
				 node);
			continue;
		}

		comp = devm_kzalloc(dev, sizeof(*comp), GFP_KERNEL);
		if (!comp) {
			ret = -ENOMEM;
			of_node_put(node);
			goto err_comp;
		}
		mdp->comp[comp_id] = comp;

		ret = mtk_mdp_comp_init(dev, node, comp, comp_id);
		if (ret) {
			of_node_put(node);
			goto err_comp;
		}
	}

	mdp->job_wq = create_singlethread_workqueue(MTK_MDP_MODULE_NAME);
	if (!mdp->job_wq) {
		dev_err(&pdev->dev, "unable to alloc job workqueue\n");
		ret = -ENOMEM;
		goto err_alloc_job_wq;
	}

	mdp->wdt_wq = create_singlethread_workqueue("mdp_wdt_wq");
	if (!mdp->wdt_wq) {
		dev_err(&pdev->dev, "unable to alloc wdt workqueue\n");
		ret = -ENOMEM;
		goto err_alloc_wdt_wq;
	}
	INIT_WORK(&mdp->wdt_work, mtk_mdp_wdt_worker);

	ret = v4l2_device_register(dev, &mdp->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register v4l2 device\n");
		ret = -EINVAL;
		goto err_dev_register;
	}

	ret = mtk_mdp_register_m2m_device(mdp);
	if (ret) {
		v4l2_err(&mdp->v4l2_dev, "Failed to init mem2mem device\n");
		goto err_m2m_register;
	}

	mdp->vpu_dev = vpu_get_plat_device(pdev);
	vpu_wdt_reg_handler(mdp->vpu_dev, mtk_mdp_reset_handler, mdp,
			    VPU_RST_MDP);

	platform_set_drvdata(pdev, mdp);

	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	pm_runtime_enable(dev);
	dev_dbg(dev, "mdp-%d registered successfully\n", mdp->id);

	return 0;

err_m2m_register:
	v4l2_device_unregister(&mdp->v4l2_dev);

err_dev_register:
	destroy_workqueue(mdp->wdt_wq);

err_alloc_wdt_wq:
	destroy_workqueue(mdp->job_wq);

err_alloc_job_wq:

err_comp:
	for (i = 0; i < ARRAY_SIZE(mdp->comp); i++)
		mtk_mdp_comp_deinit(dev, mdp->comp[i]);

	dev_dbg(dev, "err %d\n", ret);
	return ret;
}

static int mtk_mdp_remove(struct platform_device *pdev)
{
	struct mtk_mdp_dev *mdp = platform_get_drvdata(pdev);
	int i;

	pm_runtime_disable(&pdev->dev);
	vb2_dma_contig_clear_max_seg_size(&pdev->dev);
	mtk_mdp_unregister_m2m_device(mdp);
	v4l2_device_unregister(&mdp->v4l2_dev);

	flush_workqueue(mdp->wdt_wq);
	destroy_workqueue(mdp->wdt_wq);

	flush_workqueue(mdp->job_wq);
	destroy_workqueue(mdp->job_wq);

	for (i = 0; i < ARRAY_SIZE(mdp->comp); i++)
		mtk_mdp_comp_deinit(&pdev->dev, mdp->comp[i]);

	dev_dbg(&pdev->dev, "%s driver unloaded\n", pdev->name);
	return 0;
}

static int __maybe_unused mtk_mdp_pm_suspend(struct device *dev)
{
	struct mtk_mdp_dev *mdp = dev_get_drvdata(dev);

	mtk_mdp_clock_off(mdp);

	return 0;
}

static int __maybe_unused mtk_mdp_pm_resume(struct device *dev)
{
	struct mtk_mdp_dev *mdp = dev_get_drvdata(dev);

	mtk_mdp_clock_on(mdp);

	return 0;
}

static int __maybe_unused mtk_mdp_suspend(struct device *dev)
{
	if (pm_runtime_suspended(dev))
		return 0;

	return mtk_mdp_pm_suspend(dev);
}

static int __maybe_unused mtk_mdp_resume(struct device *dev)
{
	if (pm_runtime_suspended(dev))
		return 0;

	return mtk_mdp_pm_resume(dev);
}

static const struct dev_pm_ops mtk_mdp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk_mdp_suspend, mtk_mdp_resume)
	SET_RUNTIME_PM_OPS(mtk_mdp_pm_suspend, mtk_mdp_pm_resume, NULL)
};

static struct platform_driver mtk_mdp_driver = {
	.probe		= mtk_mdp_probe,
	.remove		= mtk_mdp_remove,
	.driver = {
		.name	= MTK_MDP_MODULE_NAME,
		.pm	= &mtk_mdp_pm_ops,
		.of_match_table = mtk_mdp_of_ids,
	}
};

module_platform_driver(mtk_mdp_driver);

MODULE_AUTHOR("Houlong Wei <houlong.wei@mediatek.com>");
MODULE_DESCRIPTION("Mediatek image processor driver");
MODULE_LICENSE("GPL v2");
