// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
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

	if (mdev->funcs->dump_register)
		mdev->funcs->dump_register(mdev, sf);

	for (i = 0; i < mdev->n_pipelines; i++)
		komeda_pipeline_dump_register(mdev->pipelines[i], sf);

	return 0;
}

static int komeda_register_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, komeda_register_show, inode->i_private);
}

static const struct file_operations komeda_register_fops = {
	.owner		= THIS_MODULE,
	.open		= komeda_register_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#ifdef CONFIG_DEBUG_FS
static void komeda_debugfs_init(struct komeda_dev *mdev)
{
	if (!debugfs_initialized())
		return;

	mdev->debugfs_root = debugfs_create_dir("komeda", NULL);
	if (IS_ERR_OR_NULL(mdev->debugfs_root))
		return;

	debugfs_create_file("register", 0444, mdev->debugfs_root,
			    mdev, &komeda_register_fops);
}
#endif

static ssize_t
core_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct komeda_dev *mdev = dev_to_mdev(dev);

	return snprintf(buf, PAGE_SIZE, "0x%08x\n", mdev->chip.core_id);
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
	return snprintf(buf, PAGE_SIZE, "0x%08x\n", config_id.value);
}
static DEVICE_ATTR_RO(config_id);

static struct attribute *komeda_sysfs_entries[] = {
	&dev_attr_core_id.attr,
	&dev_attr_config_id.attr,
	NULL,
};

static struct attribute_group komeda_sysfs_attr_group = {
	.attrs = komeda_sysfs_entries,
};

static int komeda_parse_pipe_dt(struct komeda_dev *mdev, struct device_node *np)
{
	struct komeda_pipeline *pipe;
	struct clk *clk;
	u32 pipe_id;
	int ret = 0;

	ret = of_property_read_u32(np, "reg", &pipe_id);
	if (ret != 0 || pipe_id >= mdev->n_pipelines)
		return -EINVAL;

	pipe = mdev->pipelines[pipe_id];

	clk = of_clk_get_by_name(np, "aclk");
	if (IS_ERR(clk)) {
		DRM_ERROR("get aclk for pipeline %d failed!\n", pipe_id);
		return PTR_ERR(clk);
	}
	pipe->aclk = clk;

	clk = of_clk_get_by_name(np, "pxclk");
	if (IS_ERR(clk)) {
		DRM_ERROR("get pxclk for pipeline %d failed!\n", pipe_id);
		return PTR_ERR(clk);
	}
	pipe->pxlclk = clk;

	/* enum ports */
	pipe->of_output_dev =
		of_graph_get_remote_node(np, KOMEDA_OF_PORT_OUTPUT, 0);
	pipe->of_output_port =
		of_graph_get_port_by_id(np, KOMEDA_OF_PORT_OUTPUT);

	pipe->of_node = np;

	return 0;
}

static int komeda_parse_dt(struct device *dev, struct komeda_dev *mdev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *child, *np = dev->of_node;
	struct clk *clk;
	int ret;

	clk = devm_clk_get(dev, "mclk");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	mdev->mclk = clk;
	mdev->irq  = platform_get_irq(pdev, 0);
	if (mdev->irq < 0) {
		DRM_ERROR("could not get IRQ number.\n");
		return mdev->irq;
	}

	for_each_available_child_of_node(np, child) {
		if (of_node_cmp(child->name, "pipeline") == 0) {
			ret = komeda_parse_pipe_dt(mdev, child);
			if (ret) {
				DRM_ERROR("parse pipeline dt error!\n");
				of_node_put(child);
				break;
			}
		}
	}

	return ret;
}

struct komeda_dev *komeda_dev_create(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct komeda_product_data *product;
	struct komeda_dev *mdev;
	struct resource *io_res;
	int err = 0;

	product = of_device_get_match_data(dev);
	if (!product)
		return ERR_PTR(-ENODEV);

	io_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io_res) {
		DRM_ERROR("No registers defined.\n");
		return ERR_PTR(-ENODEV);
	}

	mdev = devm_kzalloc(dev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return ERR_PTR(-ENOMEM);

	mutex_init(&mdev->lock);

	mdev->dev = dev;
	mdev->reg_base = devm_ioremap_resource(dev, io_res);
	if (IS_ERR(mdev->reg_base)) {
		DRM_ERROR("Map register space failed.\n");
		err = PTR_ERR(mdev->reg_base);
		mdev->reg_base = NULL;
		goto err_cleanup;
	}

	mdev->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(mdev->pclk)) {
		DRM_ERROR("Get APB clk failed.\n");
		err = PTR_ERR(mdev->pclk);
		mdev->pclk = NULL;
		goto err_cleanup;
	}

	/* Enable APB clock to access the registers */
	clk_prepare_enable(mdev->pclk);

	mdev->funcs = product->identify(mdev->reg_base, &mdev->chip);
	if (!komeda_product_match(mdev, product->product_id)) {
		DRM_ERROR("DT configured %x mismatch with real HW %x.\n",
			  product->product_id,
			  MALIDP_CORE_ID_PRODUCT_ID(mdev->chip.core_id));
		err = -ENODEV;
		goto err_cleanup;
	}

	DRM_INFO("Found ARM Mali-D%x version r%dp%d\n",
		 MALIDP_CORE_ID_PRODUCT_ID(mdev->chip.core_id),
		 MALIDP_CORE_ID_MAJOR(mdev->chip.core_id),
		 MALIDP_CORE_ID_MINOR(mdev->chip.core_id));

	mdev->funcs->init_format_table(mdev);

	err = mdev->funcs->enum_resources(mdev);
	if (err) {
		DRM_ERROR("enumerate display resource failed.\n");
		goto err_cleanup;
	}

	err = komeda_parse_dt(dev, mdev);
	if (err) {
		DRM_ERROR("parse device tree failed.\n");
		goto err_cleanup;
	}

	err = komeda_assemble_pipelines(mdev);
	if (err) {
		DRM_ERROR("assemble display pipelines failed.\n");
		goto err_cleanup;
	}

	dev->dma_parms = &mdev->dma_parms;
	dma_set_max_seg_size(dev, DMA_BIT_MASK(32));

	err = sysfs_create_group(&dev->kobj, &komeda_sysfs_attr_group);
	if (err) {
		DRM_ERROR("create sysfs group failed.\n");
		goto err_cleanup;
	}

#ifdef CONFIG_DEBUG_FS
	komeda_debugfs_init(mdev);
#endif

	return mdev;

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

	for (i = 0; i < mdev->n_pipelines; i++) {
		komeda_pipeline_destroy(mdev, mdev->pipelines[i]);
		mdev->pipelines[i] = NULL;
	}

	mdev->n_pipelines = 0;

	if (funcs && funcs->cleanup)
		funcs->cleanup(mdev);

	if (mdev->reg_base) {
		devm_iounmap(dev, mdev->reg_base);
		mdev->reg_base = NULL;
	}

	if (mdev->mclk) {
		devm_clk_put(dev, mdev->mclk);
		mdev->mclk = NULL;
	}

	if (mdev->pclk) {
		clk_disable_unprepare(mdev->pclk);
		devm_clk_put(dev, mdev->pclk);
		mdev->pclk = NULL;
	}

	devm_kfree(dev, mdev);
}
