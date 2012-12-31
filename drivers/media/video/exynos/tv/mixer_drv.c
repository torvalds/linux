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
#include <linux/kernel.h>
#include <linux/videodev2_exynos_media.h>
#include <mach/dev.h>

#include <mach/videonode-exynos5.h>
#include <media/exynos_mc.h>

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

static void mxr_set_alpha_blend(struct mxr_device *mdev)
{
	int i, j;
	int layer_en, pixel_en, chroma_en;
	u32 a, v;

	for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
		for (j = 0; j < MXR_MAX_LAYERS; ++j) {
			layer_en = mdev->sub_mxr[i].layer[j]->layer_blend_en;
			a = mdev->sub_mxr[i].layer[j]->layer_alpha;
			pixel_en = mdev->sub_mxr[i].layer[j]->pixel_blend_en;
			chroma_en = mdev->sub_mxr[i].layer[j]->chroma_en;
			v = mdev->sub_mxr[i].layer[j]->chroma_val;

			mxr_dbg(mdev, "mixer%d: layer%d\n", i, j);
			mxr_dbg(mdev, "layer blend is %s, alpha = %d\n",
					layer_en ? "enabled" : "disabled", a);
			mxr_dbg(mdev, "pixel blend is %s\n",
					pixel_en ? "enabled" : "disabled");
			mxr_dbg(mdev, "chromakey is %s, value = %d\n",
					chroma_en ? "enabled" : "disabled", v);

			mxr_reg_set_layer_blend(mdev, i, j, layer_en);
			mxr_reg_layer_alpha(mdev, i, j, a);
			mxr_reg_set_pixel_blend(mdev, i, j, pixel_en);
			mxr_reg_set_colorkey(mdev, i, j, chroma_en);
			mxr_reg_colorkey_val(mdev, i, j, v);
		}
	}
}

static int mxr_streamer_get(struct mxr_device *mdev, struct v4l2_subdev* sd)
{
	int i, ret;
	int local = 1;
	struct sub_mxr_device *sub_mxr;
	struct mxr_layer *layer;
	struct media_pad *pad;
	struct v4l2_mbus_framefmt mbus_fmt;
#if defined(CONFIG_CPU_EXYNOS4210)
	struct mxr_resources *res = &mdev->res;
#endif

	mutex_lock(&mdev->s_mutex);
	++mdev->n_streamer;
	mxr_dbg(mdev, "%s(%d)\n", __func__, mdev->n_streamer);
	/* If pipeline is started from Gscaler input video device,
	 * TV basic configuration must be set before running mixer */

#if defined(CONFIG_BUSFREQ_OPP)
	/* add bus device ptr for using bus frequency with opp */
	mdev->bus_dev = dev_get("exynos-busfreq");
#endif

	if (mdev->mxr_data_from == FROM_GSC_SD) {
		mxr_dbg(mdev, "%s: from gscaler\n", __func__);
		local = 0;
		/* enable mixer clock */
		ret = mxr_power_get(mdev);
		if (ret) {
			mxr_err(mdev, "power on failed\n");
			return -ENODEV;
		}
		/* turn on connected output device through link
		 * with mixer */
		mxr_output_get(mdev);

		for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
			sub_mxr = &mdev->sub_mxr[i];
			if (sub_mxr->local) {
				layer = sub_mxr->layer[MXR_LAYER_VIDEO];
				layer->pipe.state = MXR_PIPELINE_STREAMING;
				mxr_layer_geo_fix(layer);
				layer->ops.format_set(layer);
				layer->ops.stream_set(layer, 1);
				local += sub_mxr->local;
			}
		}
		if (local == 2)
			mxr_layer_sync(mdev, MXR_ENABLE);

		/* Set the TVOUT register about gsc-mixer local path */
		mxr_reg_local_path_set(mdev, mdev->mxr0_gsc, mdev->mxr1_gsc, mdev->flags);
	}

	/* Alpha blending configuration always can be changed
	 * whenever streaming */
	mxr_set_alpha_blend(mdev);
	mxr_reg_set_layer_prio(mdev);

	if ((mdev->n_streamer == 1 && local == 1) ||
	    (mdev->n_streamer == 2 && local == 2)) {
		for (i = MXR_PAD_SOURCE_GSCALER; i < MXR_PADS_NUM; ++i) {
			pad = &sd->entity.pads[i];

			/* find sink pad of output via enabled link*/
			pad = media_entity_remote_source(pad);
			if (pad)
				if (media_entity_type(pad->entity)
						== MEDIA_ENT_T_V4L2_SUBDEV)
					break;

			if (i == MXR_PAD_SOURCE_GRP1)
				return -ENODEV;
		}

		sd = media_entity_to_v4l2_subdev(pad->entity);

		mxr_dbg(mdev, "cookie of current output = (%d)\n",
			to_output(mdev)->cookie);

#if defined(CONFIG_BUSFREQ_OPP)
		/* Request min 200MHz */
		dev_lock(mdev->bus_dev, mdev->dev, INT_LOCK_TV);
#endif

#if defined(CONFIG_CPU_EXYNOS4210)
		if (to_output(mdev)->cookie == 0)
			clk_set_parent(res->sclk_mixer, res->sclk_dac);
		else
			clk_set_parent(res->sclk_mixer, res->sclk_hdmi);
#endif
		mxr_reg_s_output(mdev, to_output(mdev)->cookie);

		ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mbus_fmt);
		if (ret) {
			mxr_err(mdev, "failed to get mbus_fmt for output %s\n",
					sd->name);
			return ret;
		}

		mxr_reg_set_mbus_fmt(mdev, &mbus_fmt);
		ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mbus_fmt);
		if (ret) {
			mxr_err(mdev, "failed to set mbus_fmt for output %s\n",
					sd->name);
			return ret;
		}
		mxr_reg_streamon(mdev);

		ret = v4l2_subdev_call(sd, video, s_stream, 1);
		if (ret) {
			mxr_err(mdev, "starting stream failed for output %s\n",
					sd->name);
			return ret;
		}

		ret = mxr_reg_wait4vsync(mdev);
		if (ret) {
			mxr_err(mdev, "failed to get vsync (%d) from output\n",
					ret);
			return ret;
		}
	}

	mutex_unlock(&mdev->s_mutex);
	mxr_reg_dump(mdev);

	return 0;
}

static int mxr_streamer_put(struct mxr_device *mdev, struct v4l2_subdev *sd)
{
	int ret, i;
	int local = 1;
	struct media_pad *pad;
	struct sub_mxr_device *sub_mxr;
	struct mxr_layer *layer;
	struct v4l2_subdev *hdmi_sd;
	struct v4l2_subdev *gsc_sd;
	struct exynos_entity_data *md_data;

	mutex_lock(&mdev->s_mutex);
	--mdev->n_streamer;
	mxr_dbg(mdev, "%s(%d)\n", __func__, mdev->n_streamer);

	/* distinction number of local path */
	if (mdev->mxr_data_from == FROM_GSC_SD) {
		local = 0;
		for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
			sub_mxr = &mdev->sub_mxr[i];
			if (sub_mxr->local) {
				local += sub_mxr->local;
			}
		}
		if (local == 2)
			mxr_layer_sync(mdev, MXR_DISABLE);
	}

	if ((mdev->n_streamer == 0 && local == 1) ||
	    (mdev->n_streamer == 1 && local == 2)) {
		for (i = MXR_PAD_SOURCE_GSCALER; i < MXR_PADS_NUM; ++i) {
			pad = &sd->entity.pads[i];

			/* find sink pad of output via enabled link*/
			pad = media_entity_remote_source(pad);
			if (pad)
				if (media_entity_type(pad->entity)
						== MEDIA_ENT_T_V4L2_SUBDEV)
					break;

			if (i == MXR_PAD_SOURCE_GRP1)
				return -ENODEV;
		}

		hdmi_sd = media_entity_to_v4l2_subdev(pad->entity);

		mxr_reg_streamoff(mdev);
#if defined(CONFIG_BUSFREQ_OPP)
		dev_unlock(mdev->bus_dev, mdev->dev);
#endif

		/* vsync applies Mixer setup */
		ret = mxr_reg_wait4vsync(mdev);
		if (ret) {
			mxr_err(mdev, "failed to get vsync (%d) from output\n",
					ret);
			return ret;
		}
	}
	/* When using local path between gscaler and mixer, below stop sequence
	 * must be processed */
	if (mdev->mxr_data_from == FROM_GSC_SD) {
		pad = &sd->entity.pads[MXR_PAD_SINK_GSCALER];
		pad = media_entity_remote_source(pad);
		if (pad) {
			gsc_sd = media_entity_to_v4l2_subdev(
					pad->entity);
			mxr_dbg(mdev, "stop from %s\n", gsc_sd->name);
			md_data = (struct exynos_entity_data *)
				gsc_sd->dev_priv;
			md_data->media_ops->power_off(gsc_sd);
		}
	}

	if ((mdev->n_streamer == 0 && local == 1) ||
	    (mdev->n_streamer == 1 && local == 2)) {
		ret = v4l2_subdev_call(hdmi_sd, video, s_stream, 0);
		if (ret) {
			mxr_err(mdev, "stopping stream failed for output %s\n",
					hdmi_sd->name);
			return ret;
		}
	}
	/* turn off connected output device through link
	 * with mixer */
	if (mdev->mxr_data_from == FROM_GSC_SD) {
		for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
			sub_mxr = &mdev->sub_mxr[i];
			if (sub_mxr->local) {
				layer = sub_mxr->layer[MXR_LAYER_VIDEO];
				layer->ops.stream_set(layer, 0);
				layer->pipe.state = MXR_PIPELINE_IDLE;
			}
		}
		mxr_reg_local_path_clear(mdev);
		mxr_output_put(mdev);

		/* disable mixer clock */
		mxr_power_put(mdev);
	}
	WARN(mdev->n_streamer < 0, "negative number of streamers (%d)\n",
		mdev->n_streamer);
	mutex_unlock(&mdev->s_mutex);
	mxr_reg_dump(mdev);

	return 0;
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

static int mxr_runtime_resume(struct device *dev);
static int mxr_runtime_suspend(struct device *dev);

int mxr_power_get(struct mxr_device *mdev)
{
	/* If runtime PM is not implemented, mxr_runtime_resume
	 * function is directly called.
	 */
#ifdef CONFIG_PM_RUNTIME
	int ret = pm_runtime_get_sync(mdev->dev);
	/* returning 1 means that power is already enabled,
	 * so zero success be returned */
	if (IS_ERR_VALUE(ret))
		return ret;
	return 0;
#else
	mxr_runtime_resume(mdev->dev);
	return 0;
#endif
}

void mxr_power_put(struct mxr_device *mdev)
{
	/* If runtime PM is not implemented, mxr_runtime_suspend
	 * function is directly called.
	 */
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_put_sync(mdev->dev);
#else
	mxr_runtime_suspend(mdev->dev);
#endif
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

#if defined(CONFIG_ARCH_EXYNOS4)
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
#endif

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
#if defined(CONFIG_ARCH_EXYNOS4)
	iounmap(mdev->res.vp_regs);

fail_mxr_regs:
#endif
	iounmap(mdev->res.mxr_regs);

fail:
	return ret;
}

static void mxr_release_plat_resources(struct mxr_device *mdev)
{
	free_irq(mdev->res.irq, mdev);
#if defined(CONFIG_ARCH_EXYNOS4)
	iounmap(mdev->res.vp_regs);
#endif
	iounmap(mdev->res.mxr_regs);
}

static void mxr_release_clocks(struct mxr_device *mdev)
{
	struct mxr_resources *res = &mdev->res;

#if defined(CONFIG_ARCH_EXYNOS4)
	if (!IS_ERR_OR_NULL(res->vp))
		clk_put(res->vp);
#endif
#if defined(CONFIG_CPU_EXYNOS4210)
	if (!IS_ERR_OR_NULL(res->sclk_mixer))
		clk_put(res->sclk_mixer);
	if (!IS_ERR_OR_NULL(res->sclk_dac))
		clk_put(res->sclk_dac);
#endif
	if (!IS_ERR_OR_NULL(res->mixer))
		clk_put(res->mixer);
	if (!IS_ERR_OR_NULL(res->sclk_hdmi))
		clk_put(res->sclk_hdmi);
}

static int mxr_acquire_clocks(struct mxr_device *mdev)
{
	struct mxr_resources *res = &mdev->res;
	struct device *dev = mdev->dev;

#if defined(CONFIG_ARCH_EXYNOS4)
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
#endif
#if defined(CONFIG_CPU_EXYNOS4210)

	res->sclk_dac = clk_get(dev, "sclk_dac");
	if (IS_ERR_OR_NULL(res->sclk_dac)) {
		mxr_err(mdev, "failed to get clock 'sclk_dac'\n");
		goto fail;
	}
#endif
	res->mixer = clk_get(dev, "mixer");
	if (IS_ERR_OR_NULL(res->mixer)) {
		mxr_err(mdev, "failed to get clock 'mixer'\n");
		goto fail;
	}
	res->sclk_hdmi = clk_get(dev, "sclk_hdmi");
	if (IS_ERR_OR_NULL(res->sclk_hdmi)) {
		mxr_err(mdev, "failed to get clock 'sclk_hdmi'\n");
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
	int i, j;

	for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
		for (j = 0; j < MXR_MAX_LAYERS; ++j)
			if (mdev->sub_mxr[i].layer[j])
				mxr_layer_release(mdev->sub_mxr[i].layer[j]);
	}
}

static int __devinit mxr_acquire_layers(struct mxr_device *mdev,
	struct mxr_platform_data *pdata)
{
	struct sub_mxr_device *sub_mxr;

	sub_mxr = &mdev->sub_mxr[MXR_SUB_MIXER0];
#if defined(CONFIG_ARCH_EXYNOS4)
	sub_mxr->layer[MXR_LAYER_VIDEO] = mxr_vp_layer_create(mdev,
			MXR_SUB_MIXER0, 0, EXYNOS_VIDEONODE_MXR_VIDEO);
#else
	sub_mxr->layer[MXR_LAYER_VIDEO] =
		mxr_video_layer_create(mdev, MXR_SUB_MIXER0, 0);
#endif
	sub_mxr->layer[MXR_LAYER_GRP0] = mxr_graph_layer_create(mdev,
			MXR_SUB_MIXER0, 0, EXYNOS_VIDEONODE_MXR_GRP(0));
	sub_mxr->layer[MXR_LAYER_GRP1] = mxr_graph_layer_create(mdev,
			MXR_SUB_MIXER0, 1, EXYNOS_VIDEONODE_MXR_GRP(1));
	if (!sub_mxr->layer[MXR_LAYER_VIDEO] || !sub_mxr->layer[MXR_LAYER_GRP0]
			|| !sub_mxr->layer[MXR_LAYER_GRP1]) {
		mxr_err(mdev, "failed to acquire layers\n");
		goto fail;
	}

	/* Exynos5250 supports 2 sub-mixers */
	if (MXR_MAX_SUB_MIXERS == 2) {
		sub_mxr = &mdev->sub_mxr[MXR_SUB_MIXER1];
		sub_mxr->layer[MXR_LAYER_VIDEO] =
			mxr_video_layer_create(mdev, MXR_SUB_MIXER1, 1);
		sub_mxr->layer[MXR_LAYER_GRP0] = mxr_graph_layer_create(mdev,
				MXR_SUB_MIXER1, 2, EXYNOS_VIDEONODE_MXR_GRP(2));
		sub_mxr->layer[MXR_LAYER_GRP1] = mxr_graph_layer_create(mdev,
				MXR_SUB_MIXER1, 3, EXYNOS_VIDEONODE_MXR_GRP(3));
		if (!sub_mxr->layer[MXR_LAYER_VIDEO] ||
				!sub_mxr->layer[MXR_LAYER_GRP0] ||
				!sub_mxr->layer[MXR_LAYER_GRP1]) {
			mxr_err(mdev, "failed to acquire layers\n");
			goto fail;
		}
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
#if defined(CONFIG_ARCH_EXYNOS4)
	clk_enable(res->vp);
#endif
#if defined(CONFIG_CPU_EXYNOS4210)
	clk_enable(res->sclk_mixer);
#endif
	/* enable system mmu for tv. It must be enabled after enabling
	 * mixer's clock. Because of system mmu limitation. */
	mdev->vb2->resume(mdev->alloc_ctx);
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
	/* disable system mmu for tv. It must be disabled before disabling
	 * mixer's clock. Because of system mmu limitation. */
	mdev->vb2->suspend(mdev->alloc_ctx);
	/* turn clocks off */
#if defined(CONFIG_CPU_EXYNOS4210)
	clk_disable(res->sclk_mixer);
#endif
#if defined(CONFIG_ARCH_EXYNOS4)
	clk_disable(res->vp);
#endif
	clk_disable(res->mixer);
	mutex_unlock(&mdev->mutex);
	mxr_dbg(mdev, "suspend - finished\n");
	return 0;
}

/* ---------- SUB-DEVICE CALLBACKS ----------- */

static const struct dev_pm_ops mxr_pm_ops = {
	.runtime_suspend = mxr_runtime_suspend,
	.runtime_resume	 = mxr_runtime_resume,
};

static int mxr_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

/* When mixer is connected to gscaler through local path, only gscaler's
 * video device can command alpha blending functionality for mixer */
static int mxr_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct mxr_device *mdev = sd_to_mdev(sd);
	struct mxr_layer *layer;
	int v = ctrl->value;
	int num = 0;

	mxr_dbg(mdev, "%s start\n", __func__);
	mxr_dbg(mdev, "id = %d, value = %d\n", ctrl->id, ctrl->value);

	if (!strcmp(sd->name, "s5p-mixer0"))
		num = MXR_SUB_MIXER0;
	else if (!strcmp(sd->name, "s5p-mixer1"))
		num = MXR_SUB_MIXER1;

	layer = mdev->sub_mxr[num].layer[MXR_LAYER_VIDEO];
	switch (ctrl->id) {
	case V4L2_CID_TV_LAYER_BLEND_ENABLE:
		layer->layer_blend_en = v;
		break;
	case V4L2_CID_TV_LAYER_BLEND_ALPHA:
		layer->layer_alpha = (u32)v;
		break;
	case V4L2_CID_TV_PIXEL_BLEND_ENABLE:
		layer->pixel_blend_en = v;
		break;
	case V4L2_CID_TV_CHROMA_ENABLE:
		layer->chroma_en = v;
		break;
	case V4L2_CID_TV_CHROMA_VALUE:
		layer->chroma_val = (u32)v;
		break;
	case V4L2_CID_TV_LAYER_PRIO:
		layer->prio = (u8)v;
		if (layer->pipe.state == MXR_PIPELINE_STREAMING)
			mxr_reg_set_layer_prio(mdev);
		break;
	default:
		mxr_err(mdev, "invalid control id\n");
		return -EINVAL;
	}

	return 0;
}

static int mxr_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct mxr_device *mdev = sd_to_mdev(sd);
	struct exynos_entity_data *md_data;
	int ret;

	/* It can be known which entity calls this function */
	md_data = v4l2_get_subdevdata(sd);
	mdev->mxr_data_from = md_data->mxr_data_from;

	if (enable)
		ret = mxr_streamer_get(mdev, sd);
	else
		ret = mxr_streamer_put(mdev, sd);

	if (ret)
		return ret;

	return 0;
}

static struct v4l2_mbus_framefmt *
__mxr_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		unsigned int pad, enum v4l2_subdev_format_whence which)
{
	struct sub_mxr_device *sub_mxr = sd_to_sub_mxr(sd);

	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(fh, pad);
	else
		return &sub_mxr->mbus_fmt[pad];
}

static struct v4l2_rect *
__mxr_get_crop(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		unsigned int pad, enum v4l2_subdev_format_whence which)
{
	struct sub_mxr_device *sub_mxr = sd_to_sub_mxr(sd);

	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(fh, pad);
	else
		return &sub_mxr->crop[pad];
}

static unsigned int mxr_adjust_graph_format(unsigned int code)
{
	switch (code) {
	case V4L2_MBUS_FMT_RGB444_2X8_PADHI_BE:
	case V4L2_MBUS_FMT_RGB444_2X8_PADHI_LE:
	case V4L2_MBUS_FMT_RGB555_2X8_PADHI_BE:
	case V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE:
	case V4L2_MBUS_FMT_RGB565_2X8_BE:
	case V4L2_MBUS_FMT_RGB565_2X8_LE:
	case V4L2_MBUS_FMT_XRGB8888_4X8_LE:
		return code;
	default:
		return V4L2_MBUS_FMT_XRGB8888_4X8_LE; /* default format */
	}
}

/* This can be moved to graphic layer's callback function */
static void mxr_set_layer_src_fmt(struct sub_mxr_device *sub_mxr, u32 pad)
{
	/* sink pad number and array index of layer are same */
	struct mxr_layer *layer = sub_mxr->layer[pad];
	struct v4l2_mbus_framefmt *fmt = &sub_mxr->mbus_fmt[pad];
	u32 fourcc;

	switch (fmt->code) {
	case V4L2_MBUS_FMT_RGB444_2X8_PADHI_BE:
	case V4L2_MBUS_FMT_RGB444_2X8_PADHI_LE:
		fourcc = V4L2_PIX_FMT_RGB444;
		break;
	case V4L2_MBUS_FMT_RGB555_2X8_PADHI_BE:
	case V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE:
		fourcc = V4L2_PIX_FMT_RGB555;
		break;
	case V4L2_MBUS_FMT_RGB565_2X8_BE:
	case V4L2_MBUS_FMT_RGB565_2X8_LE:
		fourcc = V4L2_PIX_FMT_RGB565;
		break;
	case V4L2_MBUS_FMT_XRGB8888_4X8_LE:
		fourcc = V4L2_PIX_FMT_BGR32;
		break;
	}
	/* This will be applied to hardware right after streamon */
	layer->fmt = find_format_by_fourcc(layer, fourcc);
}

static int mxr_try_format(struct mxr_device *mdev,
		struct v4l2_subdev_fh *fh, u32 pad,
		struct v4l2_mbus_framefmt *fmt,
		enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt mbus_fmt;

	fmt->width = clamp_val(fmt->width, 1, 32767);
	fmt->height = clamp_val(fmt->height, 1, 2047);

	switch (pad) {
	case MXR_PAD_SINK_GSCALER:
		fmt->code = V4L2_MBUS_FMT_YUV8_1X24;
		break;
	case MXR_PAD_SINK_GRP0:
	case MXR_PAD_SINK_GRP1:
		fmt->code = mxr_adjust_graph_format(fmt->code);
		break;
	case MXR_PAD_SOURCE_GSCALER:
	case MXR_PAD_SOURCE_GRP0:
	case MXR_PAD_SOURCE_GRP1:
		mxr_get_mbus_fmt(mdev, &mbus_fmt);
		fmt->code = (fmt->code == V4L2_MBUS_FMT_YUV8_1X24) ?
			V4L2_MBUS_FMT_YUV8_1X24 : V4L2_MBUS_FMT_XRGB8888_4X8_LE;
		fmt->width = mbus_fmt.width;
		fmt->height = mbus_fmt.height;
		break;
	}

	return 0;
}

static void mxr_apply_format(struct v4l2_subdev *sd,
		struct v4l2_subdev_fh *fh, u32 pad,
		struct v4l2_mbus_framefmt *fmt,
		enum v4l2_subdev_format_whence which)
{
	struct sub_mxr_device *sub_mxr;
	struct mxr_device *mdev;
	int i, j;
	sub_mxr = sd_to_sub_mxr(sd);
	mdev = sd_to_mdev(sd);

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		if (pad == MXR_PAD_SINK_GRP0 || pad == MXR_PAD_SINK_GRP1) {
			struct mxr_layer *layer = sub_mxr->layer[pad];

			mxr_set_layer_src_fmt(sub_mxr, pad);
			layer->geo.src.full_width = fmt->width;
			layer->geo.src.full_height = fmt->height;
			layer->ops.fix_geometry(layer);
		} else if (pad == MXR_PAD_SOURCE_GSCALER
				|| pad == MXR_PAD_SOURCE_GRP0
				|| pad == MXR_PAD_SOURCE_GRP1) {
			for (i = 0; i < MXR_MAX_LAYERS; ++i) {
				struct mxr_layer *layer = sub_mxr->layer[i];
				layer->geo.dst.full_width = fmt->width;
				layer->geo.dst.full_height = fmt->height;
				layer->ops.fix_geometry(layer);
			}
			for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
				sub_mxr = &mdev->sub_mxr[i];
				for (j = MXR_PAD_SOURCE_GSCALER;
						j < MXR_PADS_NUM; ++j)
					sub_mxr->mbus_fmt[j].code = fmt->code;
			}
		}
	}
}

static int mxr_try_crop(struct v4l2_subdev *sd,
		struct v4l2_subdev_fh *fh, unsigned int pad,
		struct v4l2_rect *r, enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = __mxr_get_fmt(sd, fh, pad, which);
	if (fmt == NULL)
		return -EINVAL;

	r->left = clamp_val(r->left, 0, fmt->width);
	r->top = clamp_val(r->top, 0, fmt->height);
	r->width = clamp_val(r->width, 1, fmt->width - r->left);
	r->height = clamp_val(r->height, 1, fmt->height - r->top);

	/* need to align size with G-Scaler */
	if (pad == MXR_PAD_SINK_GSCALER || pad == MXR_PAD_SOURCE_GSCALER)
		if (r->width % 2)
			r->width -= 1;

	return 0;
}

static void mxr_apply_crop(struct v4l2_subdev *sd,
		struct v4l2_subdev_fh *fh, unsigned int pad,
		struct v4l2_rect *r, enum v4l2_subdev_format_whence which)
{
	struct sub_mxr_device *sub_mxr = sd_to_sub_mxr(sd);
	struct mxr_layer *layer;

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		if (pad == MXR_PAD_SINK_GRP0 || pad == MXR_PAD_SINK_GRP1) {
			layer = sub_mxr->layer[pad];

			layer->geo.src.width = r->width;
			layer->geo.src.height = r->height;
			layer->geo.src.x_offset = r->left;
			layer->geo.src.y_offset = r->top;
			layer->ops.fix_geometry(layer);
		} else if (pad == MXR_PAD_SOURCE_GSCALER
				|| pad == MXR_PAD_SOURCE_GRP0
				|| pad == MXR_PAD_SOURCE_GRP1) {
			layer = sub_mxr->layer[pad - (MXR_PADS_NUM >> 1)];

			layer->geo.dst.width = r->width;
			layer->geo.dst.height = r->height;
			layer->geo.dst.x_offset = r->left;
			layer->geo.dst.y_offset = r->top;
			layer->ops.fix_geometry(layer);
		}
	}
}

static int mxr_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = __mxr_get_fmt(sd, fh, format->pad, format->which);
	if (fmt == NULL)
		return -EINVAL;

	format->format = *fmt;

	return 0;
}

static int mxr_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_format *format)
{
	struct mxr_device *mdev = sd_to_mdev(sd);
	struct v4l2_mbus_framefmt *fmt;
	int ret;
	u32 pad;

	fmt = __mxr_get_fmt(sd, fh, format->pad, format->which);
	if (fmt == NULL)
		return -EINVAL;

	ret = mxr_try_format(mdev, fh, format->pad, &format->format,
			format->which);
	if (ret)
		return ret;

	*fmt = format->format;

	mxr_apply_format(sd, fh, format->pad, &format->format, format->which);

	if (format->pad == MXR_PAD_SINK_GSCALER ||
			format->pad == MXR_PAD_SINK_GRP0 ||
			format->pad == MXR_PAD_SINK_GRP1) {
		pad = format->pad + (MXR_PADS_NUM >> 1);
		fmt = __mxr_get_fmt(sd, fh, pad, format->which);
		if (fmt == NULL)
			return -EINVAL;

		*fmt = format->format;

		ret = mxr_try_format(mdev, fh, pad, fmt, format->which);
		if (ret)
			return ret;

		mxr_apply_format(sd, fh, pad, fmt, format->which);
	}

	return 0;
}

static int mxr_set_crop(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_crop *crop)
{
	struct v4l2_rect *r;
	int ret;
	u32 pad;

	r = __mxr_get_crop(sd, fh, crop->pad, crop->which);
	if (r == NULL)
		return -EINVAL;

	ret = mxr_try_crop(sd, fh, crop->pad, &crop->rect, crop->which);
	if (ret)
		return ret;

	/* transfer adjusted crop information to user space */
	*r = crop->rect;

	/* reserved[0] is used for sink pad number temporally */
	mxr_apply_crop(sd, fh, crop->pad, r, crop->which);

	/* In case of sink pad, crop info will be propagated to source pad */
	if (crop->pad == MXR_PAD_SINK_GSCALER ||
			crop->pad == MXR_PAD_SINK_GRP0 ||
			crop->pad == MXR_PAD_SINK_GRP1) {
		pad = crop->pad + (MXR_PADS_NUM >> 1);
		r = __mxr_get_crop(sd, fh, pad, crop->which);
		if (r == NULL)
			return -EINVAL;
		/* store propagated crop info to source pad */
		*r = crop->rect;

		ret = mxr_try_crop(sd, fh, pad, r, crop->which);
		if (ret)
			return ret;

		mxr_apply_crop(sd, fh, pad, r, crop->which);
	}

	return 0;
}

static int mxr_get_crop(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_crop *crop)
{
	struct v4l2_rect *r;

	r = __mxr_get_crop(sd, fh, crop->pad, crop->which);
	if (r == NULL)
		return -EINVAL;

	crop->rect = *r;

	return 0;
}

static const struct v4l2_subdev_core_ops mxr_sd_core_ops = {
	.s_power = mxr_s_power,
	.s_ctrl = mxr_s_ctrl,
};

static const struct v4l2_subdev_video_ops mxr_sd_video_ops = {
	.s_stream = mxr_s_stream,
};

static const struct v4l2_subdev_pad_ops	mxr_sd_pad_ops = {
	.get_fmt = mxr_get_fmt,
	.set_fmt = mxr_set_fmt,
	.get_crop = mxr_get_crop,
	.set_crop = mxr_set_crop
};

static const struct v4l2_subdev_ops mxr_sd_ops = {
	.core = &mxr_sd_core_ops,
	.video = &mxr_sd_video_ops,
	.pad = &mxr_sd_pad_ops,
};

static int mxr_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	struct media_pad *pad;
	struct sub_mxr_device *sub_mxr = entity_to_sub_mxr(entity);
	struct mxr_device *mdev = sub_mxr_to_mdev(sub_mxr);
	int i;
	int gsc_num = 0;

	/* difficult to get dev ptr */
	printk(KERN_DEBUG "%s %s\n", __func__, flags ? "start" : "stop");

	if (flags & MEDIA_LNK_FL_ENABLED) {
		sub_mxr->use = 1;
		if (local->index == MXR_PAD_SINK_GSCALER)
			sub_mxr->local = 1;
		/* find a remote pad by interating over all links
		 * until enabled link is found.
		 * This will be remove. because Exynos5250 only supports
		 * HDMI output */
		pad = media_entity_remote_source((struct media_pad *)local);
		if (pad) {
			printk(KERN_ERR "%s is already connected to %s\n",
					entity->name, pad->entity->name);
			return -EBUSY;
		}
	} else {
		if (local->index == MXR_PAD_SINK_GSCALER)
			sub_mxr->local = 0;
		sub_mxr->use = 0;
		for (i = 0; i < entity->num_links; ++i)
			if (entity->links[i].flags & MEDIA_LNK_FL_ENABLED)
				sub_mxr->use = 1;
	}

	if (!strcmp(remote->entity->name, "exynos-gsc-sd.0"))
		gsc_num = 0;
	else if (!strcmp(remote->entity->name, "exynos-gsc-sd.1"))
		gsc_num = 1;
	else if (!strcmp(remote->entity->name, "exynos-gsc-sd.2"))
		gsc_num = 2;
	else if (!strcmp(remote->entity->name, "exynos-gsc-sd.3"))
		gsc_num = 3;

	if (!strcmp(local->entity->name, "s5p-mixer0"))
		mdev->mxr0_gsc = gsc_num;
	else if (!strcmp(local->entity->name, "s5p-mixer1"))
		mdev->mxr1_gsc = gsc_num;

	/* deliver those variables to mxr_streamer_get() */
	mdev->flags = flags;
	return 0;
}

/* mixer entity operations */
static const struct media_entity_operations mxr_entity_ops = {
	.link_setup = mxr_link_setup,
};

/* ---------- MEDIA CONTROLLER MANAGEMENT ----------- */

static int mxr_register_entity(struct mxr_device *mdev, int mxr_num)
{
	struct v4l2_subdev *sd = &mdev->sub_mxr[mxr_num].sd;
	struct media_pad *pads = mdev->sub_mxr[mxr_num].pads;
	struct media_entity *me = &sd->entity;
	struct exynos_md *md;
	int ret;

	mxr_dbg(mdev, "mixer%d entity init\n", mxr_num);

	/* init mixer sub-device */
	v4l2_subdev_init(sd, &mxr_sd_ops);
	sd->owner = THIS_MODULE;
	sprintf(sd->name, "s5p-mixer%d", mxr_num);

	/* mixer sub-device can be opened in user space */
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* init mixer sub-device as entity */
	pads[MXR_PAD_SINK_GSCALER].flags = MEDIA_PAD_FL_SINK;
	pads[MXR_PAD_SINK_GRP0].flags = MEDIA_PAD_FL_SINK;
	pads[MXR_PAD_SINK_GRP1].flags = MEDIA_PAD_FL_SINK;
	pads[MXR_PAD_SOURCE_GSCALER].flags = MEDIA_PAD_FL_SOURCE;
	pads[MXR_PAD_SOURCE_GRP0].flags = MEDIA_PAD_FL_SOURCE;
	pads[MXR_PAD_SOURCE_GRP1].flags = MEDIA_PAD_FL_SOURCE;
	me->ops = &mxr_entity_ops;
	ret = media_entity_init(me, MXR_PADS_NUM, pads, 0);
	if (ret) {
		mxr_err(mdev, "failed to initialize media entity\n");
		return ret;
	}

	md = (struct exynos_md *)module_name_to_driver_data(MDEV_MODULE_NAME);
	if (!md) {
		mxr_err(mdev, "failed to get output media device\n");
		return -ENODEV;
	}

	ret = v4l2_device_register_subdev(&md->v4l2_dev, sd);
	if (ret) {
		mxr_err(mdev, "failed to register mixer subdev\n");
		return ret;
	}

	return 0;
}

static int mxr_register_entities(struct mxr_device *mdev)
{
	int ret, i;

	for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
		ret = mxr_register_entity(mdev, i);
		if (ret)
			return ret;
	}

	return 0;
}

static void mxr_unregister_entity(struct mxr_device *mdev, int mxr_num)
{
	v4l2_device_unregister_subdev(&mdev->sub_mxr[mxr_num].sd);
}

static void mxr_unregister_entities(struct mxr_device *mdev)
{
	int i;

	for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i)
		mxr_unregister_entity(mdev, i);
}

static void mxr_entities_info_print(struct mxr_device *mdev)
{
	struct v4l2_subdev *sd;
	struct media_entity *sd_me;
	struct media_entity *vd_me;
	int num_layers;
	int i, j;

#if defined(CONFIG_ARCH_EXYNOS4)
	num_layers = 3;
#else
	num_layers = 2;
#endif
	mxr_dbg(mdev, "\n************ MIXER entities info ***********\n");

	for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
		mxr_dbg(mdev, "[SUB DEVICE INFO]\n");
		sd = &mdev->sub_mxr[i].sd;
		sd_me = &sd->entity;
		entity_info_print(sd_me, mdev->dev);

		for (j = 0; j < num_layers; ++j) {
			vd_me = &mdev->sub_mxr[i].layer[j]->vfd.entity;

			mxr_dbg(mdev, "\n[VIDEO DEVICE %d INFO]\n", j);
			entity_info_print(vd_me, mdev->dev);
		}
	}

	mxr_dbg(mdev, "**************************************************\n\n");
}

static int mxr_create_links_sub_mxr(struct mxr_device *mdev, int mxr_num,
		int flags)
{
	struct exynos_md *md;
	struct mxr_layer *layer;
	int ret;
	int i, j;
	char err[80];

	mxr_info(mdev, "mixer%d create links\n", mxr_num);

	memset(err, 0, sizeof(err));

	/* link creation : gscaler0~3[1] -> mixer[0] */
	md = (struct exynos_md *)module_name_to_driver_data(MDEV_MODULE_NAME);
	for (i = 0; i < MAX_GSC_SUBDEV; ++i) {
		if (md->gsc_sd[i] != NULL) {
			ret = media_entity_create_link(&md->gsc_sd[i]->entity,
				GSC_OUT_PAD_SOURCE,
				&mdev->sub_mxr[mxr_num].sd.entity,
				MXR_PAD_SINK_GSCALER, 0);
			if (ret) {
				sprintf(err, "%s --> %s",
					md->gsc_sd[i]->entity.name,
					mdev->sub_mxr[mxr_num].sd.entity.name);
				goto fail;
			}
		}
	}

	/* link creation : mixer input0[0] -> mixer[1] */
	layer = mdev->sub_mxr[mxr_num].layer[MXR_LAYER_GRP0];
	ret = media_entity_create_link(&layer->vfd.entity, 0,
		&mdev->sub_mxr[mxr_num].sd.entity, MXR_PAD_SINK_GRP0, flags);
	if (ret) {
		sprintf(err, "%s --> %s", layer->vfd.entity.name,
				mdev->sub_mxr[mxr_num].sd.entity.name);
		goto fail;
	}

	/* link creation : mixer input1[0] -> mixer[2] */
	layer = mdev->sub_mxr[mxr_num].layer[MXR_LAYER_GRP1];
	ret = media_entity_create_link(&layer->vfd.entity, 0,
		&mdev->sub_mxr[mxr_num].sd.entity, MXR_PAD_SINK_GRP1, flags);
	if (ret) {
		sprintf(err, "%s --> %s", layer->vfd.entity.name,
				mdev->sub_mxr[mxr_num].sd.entity.name);
		goto fail;
	}

	/* link creation : mixer[3,4,5] -> output device(hdmi or sdo)[0] */
	mxr_dbg(mdev, "output device count = %d\n", mdev->output_cnt);
	for (i = 0; i < mdev->output_cnt; ++i) { /* sink pad of hdmi/sdo is 0 */
		flags = 0;
		/* default output device link is HDMI */
		if (!strcmp(mdev->output[i]->sd->name, "s5p-hdmi"))
			flags = MEDIA_LNK_FL_ENABLED;

		for (j = MXR_PAD_SOURCE_GSCALER; j < MXR_PADS_NUM; ++j) {
			ret = media_entity_create_link(
					&mdev->sub_mxr[mxr_num].sd.entity,
					j, &mdev->output[i]->sd->entity,
					0, flags);
			if (ret) {
				sprintf(err, "%s --> %s",
					mdev->sub_mxr[mxr_num].sd.entity.name,
					mdev->output[i]->sd->entity.name);
				goto fail;
			}
		}
	}

	return 0;

fail:
	mxr_err(mdev, "failed to create link : %s\n", err);
	return ret;
}

static int mxr_create_links(struct mxr_device *mdev)
{
	int ret, i;
	int flags;

#if defined(CONFIG_ARCH_EXYNOS4)
	struct mxr_layer *layer;
	struct media_entity *source, *sink;

	layer = mdev->sub_mxr[MXR_SUB_MIXER0].layer[MXR_LAYER_VIDEO];
	source = &layer->vfd.entity;
	sink = &mdev->sub_mxr[MXR_SUB_MIXER0].sd.entity;
	ret = media_entity_create_link(source, 0, sink, MXR_PAD_SINK_GSCALER,
			MEDIA_LNK_FL_ENABLED);
#endif
	for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
		if (mdev->sub_mxr[i].use)
			flags = MEDIA_LNK_FL_ENABLED;
		else
			flags = 0;

		ret = mxr_create_links_sub_mxr(mdev, i, flags);
		if (ret)
			return ret;
	}

	mxr_info(mdev, "mixer links are created successfully\n");

	return 0;
}

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

	/* use only sub mixer0 as default */
	mdev->sub_mxr[MXR_SUB_MIXER0].use = 1;
	mdev->sub_mxr[MXR_SUB_MIXER1].use = 1;

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
	mdev->vb2 = &mxr_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
	mdev->vb2 = &mxr_vb2_ion;
#endif

	mutex_init(&mdev->mutex);
	mutex_init(&mdev->s_mutex);
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

	/* register mixer subdev as entity */
	ret = mxr_register_entities(mdev);
	if (ret)
		goto fail_video;

	/* configure layers */
	ret = mxr_acquire_layers(mdev, pdata);
	if (ret)
		goto fail_entity;

	/* create links connected to gscaler, mixer inputs and hdmi */
	ret = mxr_create_links(mdev);
	if (ret)
		goto fail_entity;

	dev_set_drvdata(dev, mdev);

	pm_runtime_enable(dev);

	mxr_entities_info_print(mdev);

	mxr_info(mdev, "probe successful\n");
	return 0;

fail_entity:
	mxr_unregister_entities(mdev);

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

	dev_info(dev, "remove sucessful\n");
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
	static const char banner[] __initdata = KERN_INFO
		"Samsung TV Mixer driver, "
		"(c) 2010-2011 Samsung Electronics Co., Ltd.\n";
	printk(banner);

	/* Loading auxiliary modules */
	for (i = 0; i < ARRAY_SIZE(mxr_output_conf); ++i)
		request_module(mxr_output_conf[i].module_name);

	ret = platform_driver_register(&mxr_driver);
	if (ret != 0) {
		printk(KERN_ERR "registration of MIXER driver failed\n");
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
