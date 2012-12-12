/*
 * Samsung TV Mixer driver
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * Tomasz Stanislawski, <t.stanislaws@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */

#include "mixer.h"

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>

MODULE_AUTHOR("Tomasz Stanislawski, <t.stanislaws@samsung.com>");
MODULE_DESCRIPTION("Samsung MIXER");
MODULE_LICENSE("GPL");

/* --------- DRIVER PARAMETERS ---------- */

static struct mxr_output_conf mxr_output_conf[] = {
	{
		.output_name = "S5P HDMI connector",
		.module_name = "s5p-hdmi",
		.cookie = 1,
	},
	{
		.output_name = "S5P SDO connector",
		.module_name = "s5p-sdo",
		.cookie = 0,
	},
};

void mxr_get_mbus_fmt(struct mxr_device *mdev,
	struct v4l2_mbus_framefmt *mbus_fmt)
{
	struct v4l2_subdev *sd;
	int ret;

	mutex_lock(&mdev->mutex);
	sd = to_outsd(mdev);
	ret = v4l2_subdev_call(sd, video, g_mbus_fmt, mbus_fmt);
	WARN(ret, "failed to get mbus_fmt for output %s\n", sd->name);
	mutex_unlock(&mdev->mutex);
}

void mxr_streamer_get(struct mxr_device *mdev)
{
	mutex_lock(&mdev->mutex);
	++mdev->n_streamer;
	mxr_dbg(mdev, "%s(%d)\n", __func__, mdev->n_streamer);
	if (mdev->n_streamer == 1) {
		struct v4l2_subdev *sd = to_outsd(mdev);
		struct v4l2_mbus_framefmt mbus_fmt;
		struct mxr_resources *res = &mdev->res;
		int ret;

		if (to_output(mdev)->cookie == 0)
			clk_set_parent(res->sclk_mixer, res->sclk_dac);
		else
			clk_set_parent(res->sclk_mixer, res->sclk_hdmi);
		mxr_reg_s_output(mdev, to_output(mdev)->cookie);

		ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mbus_fmt);
		WARN(ret, "failed to get mbus_fmt for output %s\n", sd->name);
		ret = v4l2_subdev_call(sd, video, s_stream, 1);
		WARN(ret, "starting stream failed for output %s\n", sd->name);

		mxr_reg_set_mbus_fmt(mdev, &mbus_fmt);
		mxr_reg_streamon(mdev);
		ret = mxr_reg_wait4vsync(mdev);
		WARN(ret, "failed to get vsync (%d) from output\n", ret);
	}
	mutex_unlock(&mdev->mutex);
	mxr_reg_dump(mdev);
	/* FIXME: what to do when streaming fails? */
}

void mxr_streamer_put(struct mxr_device *mdev)
{
	mutex_lock(&mdev->mutex);
	--mdev->n_streamer;
	mxr_dbg(mdev, "%s(%d)\n", __func__, mdev->n_streamer);
	if (mdev->n_streamer == 0) {
		int ret;
		struct v4l2_subdev *sd = to_outsd(mdev);

		mxr_reg_streamoff(mdev);
		/* vsync applies Mixer setup */
		ret = mxr_reg_wait4vsync(mdev);
		WARN(ret, "failed to get vsync (%d) from output\n", ret);
		ret = v4l2_subdev_call(sd, video, s_stream, 0);
		WARN(ret, "stopping stream failed for output %s\n", sd->name);
	}
	WARN(mdev->n_streamer < 0, "negative number of streamers (%d)\n",
		mdev->n_streamer);
	mutex_unlock(&mdev->mutex);
	mxr_reg_dump(mdev);
}

void mxr_output_get(struct mxr_device *mdev)
{
	mutex_lock(&mdev->mutex);
	++mdev->n_output;
	mxr_dbg(mdev, "%s(%d)\n", __func__, mdev->n_output);
	/* turn on auxiliary driver */
	if (mdev->n_output == 1)
		v4l2_subdev_call(to_outsd(mdev), core, s_power, 1);
	mutex_unlock(&mdev->mutex);
}

void mxr_output_put(struct mxr_device *mdev)
{
	mutex_lock(&mdev->mutex);
	--mdev->n_output;
	mxr_dbg(mdev, "%s(%d)\n", __func__, mdev->n_output);
	/* turn on auxiliary driver */
	if (mdev->n_output == 0)
		v4l2_subdev_call(to_outsd(mdev), core, s_power, 0);
	WARN(mdev->n_output < 0, "negative number of output users (%d)\n",
		mdev->n_output);
	mutex_unlock(&mdev->mutex);
}

int mxr_power_get(struct mxr_device *mdev)
{
	int ret = pm_runtime_get_sync(mdev->dev);

	/* returning 1 means that power is already enabled,
	 * so zero success be returned */
	if (IS_ERR_VALUE(ret))
		return ret;
	return 0;
}

void mxr_power_put(struct mxr_device *mdev)
{
	pm_runtime_put_sync(mdev->dev);
}

/* --------- RESOURCE MANAGEMENT -------------*/

static int __devinit mxr_acquire_plat_resources(struct mxr_device *mdev,
	struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mxr");
	if (res == NULL) {
		mxr_err(mdev, "get memory resource failed.\n");
		ret = -ENXIO;
		goto fail;
	}

	mdev->res.mxr_regs = ioremap(res->start, resource_size(res));
	if (mdev->res.mxr_regs == NULL) {
		mxr_err(mdev, "register mapping failed.\n");
		ret = -ENXIO;
		goto fail;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vp");
	if (res == NULL) {
		mxr_err(mdev, "get memory resource failed.\n");
		ret = -ENXIO;
		goto fail_mxr_regs;
	}

	mdev->res.vp_regs = ioremap(res->start, resource_size(res));
	if (mdev->res.vp_regs == NULL) {
		mxr_err(mdev, "register mapping failed.\n");
		ret = -ENXIO;
		goto fail_mxr_regs;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "irq");
	if (res == NULL) {
		mxr_err(mdev, "get interrupt resource failed.\n");
		ret = -ENXIO;
		goto fail_vp_regs;
	}

	ret = request_irq(res->start, mxr_irq_handler, 0, "s5p-mixer", mdev);
	if (ret) {
		mxr_err(mdev, "request interrupt failed.\n");
		goto fail_vp_regs;
	}
	mdev->res.irq = res->start;

	return 0;

fail_vp_regs:
	iounmap(mdev->res.vp_regs);

fail_mxr_regs:
	iounmap(mdev->res.mxr_regs);

fail:
	return ret;
}

static void mxr_release_plat_resources(struct mxr_device *mdev)
{
	free_irq(mdev->res.irq, mdev);
	iounmap(mdev->res.vp_regs);
	iounmap(mdev->res.mxr_regs);
}

static void mxr_release_clocks(struct mxr_device *mdev)
{
	struct mxr_resources *res = &mdev->res;

	if (!IS_ERR_OR_NULL(res->sclk_dac))
		clk_put(res->sclk_dac);
	if (!IS_ERR_OR_NULL(res->sclk_hdmi))
		clk_put(res->sclk_hdmi);
	if (!IS_ERR_OR_NULL(res->sclk_mixer))
		clk_put(res->sclk_mixer);
	if (!IS_ERR_OR_NULL(res->vp))
		clk_put(res->vp);
	if (!IS_ERR_OR_NULL(res->mixer))
		clk_put(res->mixer);
}

static int mxr_acquire_clocks(struct mxr_device *mdev)
{
	struct mxr_resources *res = &mdev->res;
	struct device *dev = mdev->dev;

	res->mixer = clk_get(dev, "mixer");
	if (IS_ERR_OR_NULL(res->mixer)) {
		mxr_err(mdev, "failed to get clock 'mixer'\n");
		goto fail;
	}
	res->vp = clk_get(dev, "vp");
	if (IS_ERR_OR_NULL(res->vp)) {
		mxr_err(mdev, "failed to get clock 'vp'\n");
		goto fail;
	}
	res->sclk_mixer = clk_get(dev, "sclk_mixer");
	if (IS_ERR_OR_NULL(res->sclk_mixer)) {
		mxr_err(mdev, "failed to get clock 'sclk_mixer'\n");
		goto fail;
	}
	res->sclk_hdmi = clk_get(dev, "sclk_hdmi");
	if (IS_ERR_OR_NULL(res->sclk_hdmi)) {
		mxr_err(mdev, "failed to get clock 'sclk_hdmi'\n");
		goto fail;
	}
	res->sclk_dac = clk_get(dev, "sclk_dac");
	if (IS_ERR_OR_NULL(res->sclk_dac)) {
		mxr_err(mdev, "failed to get clock 'sclk_dac'\n");
		goto fail;
	}

	return 0;
fail:
	mxr_release_clocks(mdev);
	return -ENODEV;
}

static int __devinit mxr_acquire_resources(struct mxr_device *mdev,
	struct platform_device *pdev)
{
	int ret;
	ret = mxr_acquire_plat_resources(mdev, pdev);

	if (ret)
		goto fail;

	ret = mxr_acquire_clocks(mdev);
	if (ret)
		goto fail_plat;

	mxr_info(mdev, "resources acquired\n");
	return 0;

fail_plat:
	mxr_release_plat_resources(mdev);
fail:
	mxr_err(mdev, "resources acquire failed\n");
	return ret;
}

static void mxr_release_resources(struct mxr_device *mdev)
{
	mxr_release_clocks(mdev);
	mxr_release_plat_resources(mdev);
	memset(&mdev->res, 0, sizeof mdev->res);
}

static void mxr_release_layers(struct mxr_device *mdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mdev->layer); ++i)
		if (mdev->layer[i])
			mxr_layer_release(mdev->layer[i]);
}

static int __devinit mxr_acquire_layers(struct mxr_device *mdev,
	struct mxr_platform_data *pdata)
{
	mdev->layer[0] = mxr_graph_layer_create(mdev, 0);
	mdev->layer[1] = mxr_graph_layer_create(mdev, 1);
	mdev->layer[2] = mxr_vp_layer_create(mdev, 0);

	if (!mdev->layer[0] || !mdev->layer[1] || !mdev->layer[2]) {
		mxr_err(mdev, "failed to acquire layers\n");
		goto fail;
	}

	return 0;

fail:
	mxr_release_layers(mdev);
	return -ENODEV;
}

/* ---------- POWER MANAGEMENT ----------- */

static int mxr_runtime_resume(struct device *dev)
{
	struct mxr_device *mdev = to_mdev(dev);
	struct mxr_resources *res = &mdev->res;

	mxr_dbg(mdev, "resume - start\n");
	mutex_lock(&mdev->mutex);
	/* turn clocks on */
	clk_enable(res->mixer);
	clk_enable(res->vp);
	clk_enable(res->sclk_mixer);
	/* apply default configuration */
	mxr_reg_reset(mdev);
	mxr_dbg(mdev, "resume - finished\n");

	mutex_unlock(&mdev->mutex);
	return 0;
}

static int mxr_runtime_suspend(struct device *dev)
{
	struct mxr_device *mdev = to_mdev(dev);
	struct mxr_resources *res = &mdev->res;
	mxr_dbg(mdev, "suspend - start\n");
	mutex_lock(&mdev->mutex);
	/* turn clocks off */
	clk_disable(res->sclk_mixer);
	clk_disable(res->vp);
	clk_disable(res->mixer);
	mutex_unlock(&mdev->mutex);
	mxr_dbg(mdev, "suspend - finished\n");
	return 0;
}

static const struct dev_pm_ops mxr_pm_ops = {
	.runtime_suspend = mxr_runtime_suspend,
	.runtime_resume	 = mxr_runtime_resume,
};

/* --------- DRIVER INITIALIZATION ---------- */

static int __devinit mxr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mxr_platform_data *pdata = dev->platform_data;
	struct mxr_device *mdev;
	int ret;

	/* mdev does not exist yet so no mxr_dbg is used */
	dev_info(dev, "probe start\n");

	mdev = kzalloc(sizeof *mdev, GFP_KERNEL);
	if (!mdev) {
		dev_err(dev, "not enough memory.\n");
		ret = -ENOMEM;
		goto fail;
	}

	/* setup pointer to master device */
	mdev->dev = dev;

	mutex_init(&mdev->mutex);
	spin_lock_init(&mdev->reg_slock);
	init_waitqueue_head(&mdev->event_queue);

	/* acquire resources: regs, irqs, clocks, regulators */
	ret = mxr_acquire_resources(mdev, pdev);
	if (ret)
		goto fail_mem;

	/* configure resources for video output */
	ret = mxr_acquire_video(mdev, mxr_output_conf,
		ARRAY_SIZE(mxr_output_conf));
	if (ret)
		goto fail_resources;

	/* configure layers */
	ret = mxr_acquire_layers(mdev, pdata);
	if (ret)
		goto fail_video;

	pm_runtime_enable(dev);

	mxr_info(mdev, "probe successful\n");
	return 0;

fail_video:
	mxr_release_video(mdev);

fail_resources:
	mxr_release_resources(mdev);

fail_mem:
	kfree(mdev);

fail:
	dev_info(dev, "probe failed\n");
	return ret;
}

static int __devexit mxr_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mxr_device *mdev = to_mdev(dev);

	pm_runtime_disable(dev);

	mxr_release_layers(mdev);
	mxr_release_video(mdev);
	mxr_release_resources(mdev);

	kfree(mdev);

	dev_info(dev, "remove successful\n");
	return 0;
}

static struct platform_driver mxr_driver __refdata = {
	.probe = mxr_probe,
	.remove = __devexit_p(mxr_remove),
	.driver = {
		.name = MXR_DRIVER_NAME,
		.owner = THIS_MODULE,
		.pm = &mxr_pm_ops,
	}
};

static int __init mxr_init(void)
{
	int i, ret;
	static const char banner[] __initconst =
		"Samsung TV Mixer driver, "
		"(c) 2010-2011 Samsung Electronics Co., Ltd.\n";
	pr_info("%s\n", banner);

	/* Loading auxiliary modules */
	for (i = 0; i < ARRAY_SIZE(mxr_output_conf); ++i)
		request_module(mxr_output_conf[i].module_name);

	ret = platform_driver_register(&mxr_driver);
	if (ret != 0) {
		pr_err("s5p-tv: registration of MIXER driver failed\n");
		return -ENXIO;
	}

	return 0;
}
module_init(mxr_init);

static void __exit mxr_exit(void)
{
	platform_driver_unregister(&mxr_driver);
}
module_exit(mxr_exit);
