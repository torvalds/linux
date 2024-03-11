// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/mtk_scp.h>
#include <media/videobuf2-dma-contig.h>

#include "mtk-mdp3-core.h"
#include "mtk-mdp3-cfg.h"
#include "mtk-mdp3-m2m.h"

static const struct of_device_id mdp_of_ids[] = {
	{ .compatible = "mediatek,mt8183-mdp3-rdma",
	  .data = &mt8183_mdp_driver_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mdp_of_ids);

static struct platform_device *__get_pdev_by_id(struct platform_device *pdev,
						enum mdp_infra_id id)
{
	struct device_node *node;
	struct platform_device *mdp_pdev = NULL;
	const struct mtk_mdp_driver_data *mdp_data;
	const char *compat;

	if (!pdev)
		return NULL;

	if (id < MDP_INFRA_MMSYS || id >= MDP_INFRA_MAX) {
		dev_err(&pdev->dev, "Illegal infra id %d\n", id);
		return NULL;
	}

	mdp_data = of_device_get_match_data(&pdev->dev);
	if (!mdp_data) {
		dev_err(&pdev->dev, "have no driver data to find node\n");
		return NULL;
	}
	compat = mdp_data->mdp_probe_infra[id].compatible;

	node = of_find_compatible_node(NULL, NULL, compat);
	if (WARN_ON(!node)) {
		dev_err(&pdev->dev, "find node from id %d failed\n", id);
		return NULL;
	}

	mdp_pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (WARN_ON(!mdp_pdev)) {
		dev_err(&pdev->dev, "find pdev from id %d failed\n", id);
		return NULL;
	}

	return mdp_pdev;
}

struct platform_device *mdp_get_plat_device(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *mdp_node;
	struct platform_device *mdp_pdev;

	mdp_node = of_parse_phandle(dev->of_node, MDP_PHANDLE_NAME, 0);
	if (!mdp_node) {
		dev_err(dev, "can't get node %s\n", MDP_PHANDLE_NAME);
		return NULL;
	}

	mdp_pdev = of_find_device_by_node(mdp_node);
	of_node_put(mdp_node);

	return mdp_pdev;
}
EXPORT_SYMBOL_GPL(mdp_get_plat_device);

int mdp_vpu_get_locked(struct mdp_dev *mdp)
{
	int ret = 0;

	if (mdp->vpu_count++ == 0) {
		ret = rproc_boot(mdp->rproc_handle);
		if (ret) {
			dev_err(&mdp->pdev->dev,
				"vpu_load_firmware failed %d\n", ret);
			goto err_load_vpu;
		}
		ret = mdp_vpu_register(mdp);
		if (ret) {
			dev_err(&mdp->pdev->dev,
				"mdp_vpu register failed %d\n", ret);
			goto err_reg_vpu;
		}
		ret = mdp_vpu_dev_init(&mdp->vpu, mdp->scp, &mdp->vpu_lock);
		if (ret) {
			dev_err(&mdp->pdev->dev,
				"mdp_vpu device init failed %d\n", ret);
			goto err_init_vpu;
		}
	}
	return 0;

err_init_vpu:
	mdp_vpu_unregister(mdp);
err_reg_vpu:
err_load_vpu:
	mdp->vpu_count--;
	return ret;
}

void mdp_vpu_put_locked(struct mdp_dev *mdp)
{
	if (--mdp->vpu_count == 0) {
		mdp_vpu_dev_deinit(&mdp->vpu);
		mdp_vpu_unregister(mdp);
	}
}

void mdp_video_device_release(struct video_device *vdev)
{
	struct mdp_dev *mdp = (struct mdp_dev *)video_get_drvdata(vdev);
	int i;

	scp_put(mdp->scp);

	destroy_workqueue(mdp->job_wq);
	destroy_workqueue(mdp->clock_wq);

	pm_runtime_disable(&mdp->pdev->dev);

	vb2_dma_contig_clear_max_seg_size(&mdp->pdev->dev);

	mdp_comp_destroy(mdp);
	for (i = 0; i < MDP_PIPE_MAX; i++)
		mtk_mutex_put(mdp->mdp_mutex[i]);

	mdp_vpu_shared_mem_free(&mdp->vpu);
	v4l2_m2m_release(mdp->m2m_dev);
	kfree(mdp);
}

static int mdp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mdp_dev *mdp;
	struct platform_device *mm_pdev;
	int ret, i, mutex_id;

	mdp = kzalloc(sizeof(*mdp), GFP_KERNEL);
	if (!mdp) {
		ret = -ENOMEM;
		goto err_return;
	}

	mdp->pdev = pdev;
	mdp->mdp_data = of_device_get_match_data(&pdev->dev);

	mm_pdev = __get_pdev_by_id(pdev, MDP_INFRA_MMSYS);
	if (!mm_pdev) {
		ret = -ENODEV;
		goto err_destroy_device;
	}
	mdp->mdp_mmsys = &mm_pdev->dev;

	mm_pdev = __get_pdev_by_id(pdev, MDP_INFRA_MUTEX);
	if (WARN_ON(!mm_pdev)) {
		ret = -ENODEV;
		goto err_destroy_device;
	}
	for (i = 0; i < mdp->mdp_data->pipe_info_len; i++) {
		mutex_id = mdp->mdp_data->pipe_info[i].mutex_id;
		if (!IS_ERR_OR_NULL(mdp->mdp_mutex[mutex_id]))
			continue;
		mdp->mdp_mutex[mutex_id] = mtk_mutex_get(&mm_pdev->dev);
		if (IS_ERR(mdp->mdp_mutex[mutex_id])) {
			ret = PTR_ERR(mdp->mdp_mutex[mutex_id]);
			goto err_free_mutex;
		}
	}

	ret = mdp_comp_config(mdp);
	if (ret) {
		dev_err(dev, "Failed to config mdp components\n");
		goto err_free_mutex;
	}

	mdp->job_wq = alloc_workqueue(MDP_MODULE_NAME, WQ_FREEZABLE, 0);
	if (!mdp->job_wq) {
		dev_err(dev, "Unable to create job workqueue\n");
		ret = -ENOMEM;
		goto err_deinit_comp;
	}

	mdp->clock_wq = alloc_workqueue(MDP_MODULE_NAME "-clock", WQ_FREEZABLE,
					0);
	if (!mdp->clock_wq) {
		dev_err(dev, "Unable to create clock workqueue\n");
		ret = -ENOMEM;
		goto err_destroy_job_wq;
	}

	mdp->scp = scp_get(pdev);
	if (!mdp->scp) {
		mm_pdev = __get_pdev_by_id(pdev, MDP_INFRA_SCP);
		if (WARN_ON(!mm_pdev)) {
			dev_err(&pdev->dev, "Could not get scp device\n");
			ret = -ENODEV;
			goto err_destroy_clock_wq;
		}
		mdp->scp = platform_get_drvdata(mm_pdev);
	}

	mdp->rproc_handle = scp_get_rproc(mdp->scp);
	dev_dbg(&pdev->dev, "MDP rproc_handle: %pK", mdp->rproc_handle);

	mutex_init(&mdp->vpu_lock);
	mutex_init(&mdp->m2m_lock);

	mdp->cmdq_clt = cmdq_mbox_create(dev, 0);
	if (IS_ERR(mdp->cmdq_clt)) {
		ret = PTR_ERR(mdp->cmdq_clt);
		goto err_put_scp;
	}

	init_waitqueue_head(&mdp->callback_wq);
	ida_init(&mdp->mdp_ida);
	platform_set_drvdata(pdev, mdp);

	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	ret = v4l2_device_register(dev, &mdp->v4l2_dev);
	if (ret) {
		dev_err(dev, "Failed to register v4l2 device\n");
		ret = -EINVAL;
		goto err_mbox_destroy;
	}

	ret = mdp_m2m_device_register(mdp);
	if (ret) {
		v4l2_err(&mdp->v4l2_dev, "Failed to register m2m device\n");
		goto err_unregister_device;
	}

	dev_dbg(dev, "mdp-%d registered successfully\n", pdev->id);
	return 0;

err_unregister_device:
	v4l2_device_unregister(&mdp->v4l2_dev);
err_mbox_destroy:
	cmdq_mbox_destroy(mdp->cmdq_clt);
err_put_scp:
	scp_put(mdp->scp);
err_destroy_clock_wq:
	destroy_workqueue(mdp->clock_wq);
err_destroy_job_wq:
	destroy_workqueue(mdp->job_wq);
err_deinit_comp:
	mdp_comp_destroy(mdp);
err_free_mutex:
	for (i = 0; i < mdp->mdp_data->pipe_info_len; i++)
		if (!IS_ERR_OR_NULL(mdp->mdp_mutex[i]))
			mtk_mutex_put(mdp->mdp_mutex[i]);
err_destroy_device:
	kfree(mdp);
err_return:
	dev_dbg(dev, "Errno %d\n", ret);
	return ret;
}

static void mdp_remove(struct platform_device *pdev)
{
	struct mdp_dev *mdp = platform_get_drvdata(pdev);

	v4l2_device_unregister(&mdp->v4l2_dev);

	dev_dbg(&pdev->dev, "%s driver unloaded\n", pdev->name);
}

static int __maybe_unused mdp_suspend(struct device *dev)
{
	struct mdp_dev *mdp = dev_get_drvdata(dev);
	int ret;

	atomic_set(&mdp->suspended, 1);

	if (atomic_read(&mdp->job_count)) {
		ret = wait_event_timeout(mdp->callback_wq,
					 !atomic_read(&mdp->job_count),
					 2 * HZ);
		if (ret == 0) {
			dev_err(dev,
				"%s:flushed cmdq task incomplete, count=%d\n",
				__func__, atomic_read(&mdp->job_count));
			return -EBUSY;
		}
	}

	return 0;
}

static int __maybe_unused mdp_resume(struct device *dev)
{
	struct mdp_dev *mdp = dev_get_drvdata(dev);

	atomic_set(&mdp->suspended, 0);

	return 0;
}

static const struct dev_pm_ops mdp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mdp_suspend, mdp_resume)
};

static struct platform_driver mdp_driver = {
	.probe		= mdp_probe,
	.remove_new	= mdp_remove,
	.driver = {
		.name	= MDP_MODULE_NAME,
		.pm	= &mdp_pm_ops,
		.of_match_table = mdp_of_ids,
	},
};

module_platform_driver(mdp_driver);

MODULE_AUTHOR("Ping-Hsun Wu <ping-hsun.wu@mediatek.com>");
MODULE_DESCRIPTION("MediaTek image processor 3 driver");
MODULE_LICENSE("GPL");
