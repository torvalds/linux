// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include <drm/drm_print.h>

#include "komeda_dev.h"

static int komeda_register_show(struct seq_file *sf, void *x)
{
	struct komeda_dev *mdev = sf->private;
	int i;

	seq_puts(sf, "\n====== Komeda register dump =========\n");

	pm_runtime_get_sync(mdev->dev);

	if (mdev->funcs->dump_register)
		mdev->funcs->dump_register(mdev, sf);

	for (i = 0; i < mdev->n_pipelines; i++)
		komeda_pipeline_dump_register(mdev->pipelines[i], sf);

	pm_runtime_put(mdev->dev);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(komeda_register);

#ifdef CONFIG_DEBUG_FS
static void komeda_debugfs_init(struct komeda_dev *mdev)
{
	if (!debugfs_initialized())
		return;

	mdev->debugfs_root = debugfs_create_dir("komeda", NULL);
	debugfs_create_file("register", 0444, mdev->debugfs_root,
			    mdev, &komeda_register_fops);
	debugfs_create_x16("err_verbosity", 0664, mdev->debugfs_root,
			   &mdev->err_verbosity);
}
#endif

static ssize_t
core_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct komeda_dev *mdev = dev_to_mdev(dev);

	return sysfs_emit(buf, "0x%08x\n", mdev->chip.core_id);
}
static DEVICE_ATTR_RO(core_id);

static ssize_t
config_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct komeda_dev *mdev = dev_to_mdev(dev);
	struct komeda_pipeline *pipe = mdev->pipelines[0];
	union komeda_config_id config_id;
	int i;

	memset(&config_id, 0, sizeof(config_id));

	config_id.max_line_sz = pipe->layers[0]->hsize_in.end;
	config_id.n_pipelines = mdev->n_pipelines;
	config_id.n_scalers = pipe->n_scalers;
	config_id.n_layers = pipe->n_layers;
	config_id.n_richs = 0;
	for (i = 0; i < pipe->n_layers; i++) {
		if (pipe->layers[i]->layer_type == KOMEDA_FMT_RICH_LAYER)
			config_id.n_richs++;
	}
	return sysfs_emit(buf, "0x%08x\n", config_id.value);
}
static DEVICE_ATTR_RO(config_id);

static ssize_t
aclk_hz_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct komeda_dev *mdev = dev_to_mdev(dev);

	return sysfs_emit(buf, "%lu\n", clk_get_rate(mdev->aclk));
}
static DEVICE_ATTR_RO(aclk_hz);

static struct attribute *komeda_sysfs_entries[] = {
	&dev_attr_core_id.attr,
	&dev_attr_config_id.attr,
	&dev_attr_aclk_hz.attr,
	NULL,
};

static struct attribute_group komeda_sysfs_attr_group = {
	.attrs = komeda_sysfs_entries,
};

static int komeda_parse_pipe_dt(struct komeda_pipeline *pipe)
{
	struct device_node *np = pipe->of_node;
	struct clk *clk;

	clk = of_clk_get_by_name(np, "pxclk");
	if (IS_ERR(clk)) {
		DRM_ERROR("get pxclk for pipeline %d failed!\n", pipe->id);
		return PTR_ERR(clk);
	}
	pipe->pxlclk = clk;

	/* enum ports */
	pipe->of_output_links[0] =
		of_graph_get_remote_node(np, KOMEDA_OF_PORT_OUTPUT, 0);
	pipe->of_output_links[1] =
		of_graph_get_remote_node(np, KOMEDA_OF_PORT_OUTPUT, 1);
	pipe->of_output_port =
		of_graph_get_port_by_id(np, KOMEDA_OF_PORT_OUTPUT);

	pipe->dual_link = pipe->of_output_links[0] && pipe->of_output_links[1];

	return 0;
}

static int komeda_parse_dt(struct device *dev, struct komeda_dev *mdev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *child, *np = dev->of_node;
	struct komeda_pipeline *pipe;
	u32 pipe_id = U32_MAX;
	int ret = -1;

	mdev->irq  = platform_get_irq(pdev, 0);
	if (mdev->irq < 0) {
		DRM_ERROR("could not get IRQ number.\n");
		return mdev->irq;
	}

	/* Get the optional framebuffer memory resource */
	ret = of_reserved_mem_device_init(dev);
	if (ret && ret != -ENODEV)
		return ret;

	for_each_available_child_of_node(np, child) {
		if (of_node_name_eq(child, "pipeline")) {
			of_property_read_u32(child, "reg", &pipe_id);
			if (pipe_id >= mdev->n_pipelines) {
				DRM_WARN("Skip the redundant DT node: pipeline-%u.\n",
					 pipe_id);
				continue;
			}
			mdev->pipelines[pipe_id]->of_node = of_node_get(child);
		}
	}

	for (pipe_id = 0; pipe_id < mdev->n_pipelines; pipe_id++) {
		pipe = mdev->pipelines[pipe_id];

		if (!pipe->of_node) {
			DRM_ERROR("Pipeline-%d doesn't have a DT node.\n",
				  pipe->id);
			return -EINVAL;
		}
		ret = komeda_parse_pipe_dt(pipe);
		if (ret)
			return ret;
	}

	return 0;
}

struct komeda_dev *komeda_dev_create(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	komeda_identify_func komeda_identify;
	struct komeda_dev *mdev;
	int err = 0;

	komeda_identify = of_device_get_match_data(dev);
	if (!komeda_identify)
		return ERR_PTR(-ENODEV);

	mdev = devm_kzalloc(dev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return ERR_PTR(-ENOMEM);

	mutex_init(&mdev->lock);

	mdev->dev = dev;
	mdev->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mdev->reg_base)) {
		DRM_ERROR("Map register space failed.\n");
		err = PTR_ERR(mdev->reg_base);
		mdev->reg_base = NULL;
		goto err_cleanup;
	}

	mdev->aclk = devm_clk_get(dev, "aclk");
	if (IS_ERR(mdev->aclk)) {
		DRM_ERROR("Get engine clk failed.\n");
		err = PTR_ERR(mdev->aclk);
		mdev->aclk = NULL;
		goto err_cleanup;
	}

	clk_prepare_enable(mdev->aclk);

	mdev->funcs = komeda_identify(mdev->reg_base, &mdev->chip);
	if (!mdev->funcs) {
		DRM_ERROR("Failed to identify the HW.\n");
		err = -ENODEV;
		goto disable_clk;
	}

	DRM_INFO("Found ARM Mali-D%x version r%dp%d\n",
		 MALIDP_CORE_ID_PRODUCT_ID(mdev->chip.core_id),
		 MALIDP_CORE_ID_MAJOR(mdev->chip.core_id),
		 MALIDP_CORE_ID_MINOR(mdev->chip.core_id));

	mdev->funcs->init_format_table(mdev);

	err = mdev->funcs->enum_resources(mdev);
	if (err) {
		DRM_ERROR("enumerate display resource failed.\n");
		goto disable_clk;
	}

	err = komeda_parse_dt(dev, mdev);
	if (err) {
		DRM_ERROR("parse device tree failed.\n");
		goto disable_clk;
	}

	err = komeda_assemble_pipelines(mdev);
	if (err) {
		DRM_ERROR("assemble display pipelines failed.\n");
		goto disable_clk;
	}

	dma_set_max_seg_size(dev, U32_MAX);

	mdev->iommu = iommu_get_domain_for_dev(mdev->dev);
	if (!mdev->iommu)
		DRM_INFO("continue without IOMMU support!\n");

	clk_disable_unprepare(mdev->aclk);

	err = sysfs_create_group(&dev->kobj, &komeda_sysfs_attr_group);
	if (err) {
		DRM_ERROR("create sysfs group failed.\n");
		goto err_cleanup;
	}

	mdev->err_verbosity = KOMEDA_DEV_PRINT_ERR_EVENTS;

#ifdef CONFIG_DEBUG_FS
	komeda_debugfs_init(mdev);
#endif

	return mdev;

disable_clk:
	clk_disable_unprepare(mdev->aclk);
err_cleanup:
	komeda_dev_destroy(mdev);
	return ERR_PTR(err);
}

void komeda_dev_destroy(struct komeda_dev *mdev)
{
	struct device *dev = mdev->dev;
	const struct komeda_dev_funcs *funcs = mdev->funcs;
	int i;

	sysfs_remove_group(&dev->kobj, &komeda_sysfs_attr_group);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(mdev->debugfs_root);
#endif

	if (mdev->aclk)
		clk_prepare_enable(mdev->aclk);

	for (i = 0; i < mdev->n_pipelines; i++) {
		komeda_pipeline_destroy(mdev, mdev->pipelines[i]);
		mdev->pipelines[i] = NULL;
	}

	mdev->n_pipelines = 0;

	of_reserved_mem_device_release(dev);

	if (funcs && funcs->cleanup)
		funcs->cleanup(mdev);

	if (mdev->reg_base) {
		devm_iounmap(dev, mdev->reg_base);
		mdev->reg_base = NULL;
	}

	if (mdev->aclk) {
		clk_disable_unprepare(mdev->aclk);
		devm_clk_put(dev, mdev->aclk);
		mdev->aclk = NULL;
	}

	devm_kfree(dev, mdev);
}

int komeda_dev_resume(struct komeda_dev *mdev)
{
	clk_prepare_enable(mdev->aclk);

	mdev->funcs->enable_irq(mdev);

	if (mdev->iommu && mdev->funcs->connect_iommu)
		if (mdev->funcs->connect_iommu(mdev))
			DRM_ERROR("connect iommu failed.\n");

	return 0;
}

int komeda_dev_suspend(struct komeda_dev *mdev)
{
	if (mdev->iommu && mdev->funcs->disconnect_iommu)
		if (mdev->funcs->disconnect_iommu(mdev))
			DRM_ERROR("disconnect iommu failed.\n");

	mdev->funcs->disable_irq(mdev);

	clk_disable_unprepare(mdev->aclk);

	return 0;
}
