// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include "komeda_dev.h"

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

	err = mdev->funcs->enum_resources(mdev);
	if (err) {
		DRM_ERROR("enumerate display resource failed.\n");
		goto err_cleanup;
	}

	return mdev;

err_cleanup:
	komeda_dev_destroy(mdev);
	return ERR_PTR(err);
}

void komeda_dev_destroy(struct komeda_dev *mdev)
{
	struct device *dev = mdev->dev;
	struct komeda_dev_funcs *funcs = mdev->funcs;
	int i;

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
