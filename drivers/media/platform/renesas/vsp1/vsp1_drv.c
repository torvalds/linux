// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_drv.c  --  R-Car VSP1 Driver
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/videodev2.h>

#include <media/rcar-fcp.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_brx.h"
#include "vsp1_clu.h"
#include "vsp1_dl.h"
#include "vsp1_drm.h"
#include "vsp1_hgo.h"
#include "vsp1_hgt.h"
#include "vsp1_hsit.h"
#include "vsp1_lif.h"
#include "vsp1_lut.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_sru.h"
#include "vsp1_uds.h"
#include "vsp1_uif.h"
#include "vsp1_video.h"

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

static irqreturn_t vsp1_irq_handler(int irq, void *data)
{
	u32 mask = VI6_WPF_IRQ_STA_DFE | VI6_WPF_IRQ_STA_FRE |
		   VI6_WPF_IRQ_STA_UND;
	struct vsp1_device *vsp1 = data;
	irqreturn_t ret = IRQ_NONE;
	unsigned int i;
	u32 status;

	for (i = 0; i < vsp1->info->wpf_count; ++i) {
		struct vsp1_rwpf *wpf = vsp1->wpf[i];

		if (wpf == NULL)
			continue;

		status = vsp1_read(vsp1, VI6_WPF_IRQ_STA(i));
		vsp1_write(vsp1, VI6_WPF_IRQ_STA(i), ~status & mask);

		if ((status & VI6_WPF_IRQ_STA_UND) && wpf->entity.pipe) {
			wpf->entity.pipe->underrun_count++;

			dev_warn_ratelimited(vsp1->dev,
				"Underrun occurred at WPF%u (total underruns %u)\n",
				i, wpf->entity.pipe->underrun_count);
		}

		if (status & VI6_WPF_IRQ_STA_DFE) {
			vsp1_pipeline_frame_end(wpf->entity.pipe);
			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

/* -----------------------------------------------------------------------------
 * Entities
 */

/*
 * vsp1_create_sink_links - Create links from all sources to the given sink
 *
 * This function creates media links from all valid sources to the given sink
 * pad. Links that would be invalid according to the VSP1 hardware capabilities
 * are skipped. Those include all links
 *
 * - from a UDS to a UDS (UDS entities can't be chained)
 * - from an entity to itself (no loops are allowed)
 *
 * Furthermore, the BRS can't be connected to histogram generators, but no
 * special check is currently needed as all VSP instances that include a BRS
 * have no histogram generator.
 */
static int vsp1_create_sink_links(struct vsp1_device *vsp1,
				  struct vsp1_entity *sink)
{
	struct media_entity *entity = &sink->subdev.entity;
	struct vsp1_entity *source;
	unsigned int pad;
	int ret;

	list_for_each_entry(source, &vsp1->entities, list_dev) {
		u32 flags;

		if (source->type == sink->type)
			continue;

		if (source->type == VSP1_ENTITY_HGO ||
		    source->type == VSP1_ENTITY_HGT ||
		    source->type == VSP1_ENTITY_LIF ||
		    source->type == VSP1_ENTITY_WPF)
			continue;

		flags = source->type == VSP1_ENTITY_RPF &&
			sink->type == VSP1_ENTITY_WPF &&
			source->index == sink->index
		      ? MEDIA_LNK_FL_ENABLED : 0;

		for (pad = 0; pad < entity->num_pads; ++pad) {
			if (!(entity->pads[pad].flags & MEDIA_PAD_FL_SINK))
				continue;

			ret = media_create_pad_link(&source->subdev.entity,
						       source->source_pad,
						       entity, pad, flags);
			if (ret < 0)
				return ret;

			if (flags & MEDIA_LNK_FL_ENABLED)
				source->sink = sink;
		}
	}

	return 0;
}

static int vsp1_uapi_create_links(struct vsp1_device *vsp1)
{
	struct vsp1_entity *entity;
	unsigned int i;
	int ret;

	list_for_each_entry(entity, &vsp1->entities, list_dev) {
		if (entity->type == VSP1_ENTITY_LIF ||
		    entity->type == VSP1_ENTITY_RPF)
			continue;

		ret = vsp1_create_sink_links(vsp1, entity);
		if (ret < 0)
			return ret;
	}

	if (vsp1->hgo) {
		ret = media_create_pad_link(&vsp1->hgo->histo.entity.subdev.entity,
					    HISTO_PAD_SOURCE,
					    &vsp1->hgo->histo.video.entity, 0,
					    MEDIA_LNK_FL_ENABLED |
					    MEDIA_LNK_FL_IMMUTABLE);
		if (ret < 0)
			return ret;
	}

	if (vsp1->hgt) {
		ret = media_create_pad_link(&vsp1->hgt->histo.entity.subdev.entity,
					    HISTO_PAD_SOURCE,
					    &vsp1->hgt->histo.video.entity, 0,
					    MEDIA_LNK_FL_ENABLED |
					    MEDIA_LNK_FL_IMMUTABLE);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < vsp1->info->lif_count; ++i) {
		if (!vsp1->lif[i])
			continue;

		ret = media_create_pad_link(&vsp1->wpf[i]->entity.subdev.entity,
					    RWPF_PAD_SOURCE,
					    &vsp1->lif[i]->entity.subdev.entity,
					    LIF_PAD_SINK, 0);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		struct vsp1_rwpf *rpf = vsp1->rpf[i];

		ret = media_create_pad_link(&rpf->video->video.entity, 0,
					    &rpf->entity.subdev.entity,
					    RWPF_PAD_SINK,
					    MEDIA_LNK_FL_ENABLED |
					    MEDIA_LNK_FL_IMMUTABLE);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < vsp1->info->wpf_count; ++i) {
		/*
		 * Connect the video device to the WPF. All connections are
		 * immutable.
		 */
		struct vsp1_rwpf *wpf = vsp1->wpf[i];

		ret = media_create_pad_link(&wpf->entity.subdev.entity,
					    RWPF_PAD_SOURCE,
					    &wpf->video->video.entity, 0,
					    MEDIA_LNK_FL_IMMUTABLE |
					    MEDIA_LNK_FL_ENABLED);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void vsp1_destroy_entities(struct vsp1_device *vsp1)
{
	struct vsp1_entity *entity, *_entity;
	struct vsp1_video *video, *_video;

	list_for_each_entry_safe(entity, _entity, &vsp1->entities, list_dev) {
		list_del(&entity->list_dev);
		vsp1_entity_destroy(entity);
	}

	list_for_each_entry_safe(video, _video, &vsp1->videos, list) {
		list_del(&video->list);
		vsp1_video_cleanup(video);
	}

	v4l2_device_unregister(&vsp1->v4l2_dev);
	if (vsp1->info->uapi)
		media_device_unregister(&vsp1->media_dev);
	media_device_cleanup(&vsp1->media_dev);

	if (!vsp1->info->uapi)
		vsp1_drm_cleanup(vsp1);
}

static int vsp1_create_entities(struct vsp1_device *vsp1)
{
	struct media_device *mdev = &vsp1->media_dev;
	struct v4l2_device *vdev = &vsp1->v4l2_dev;
	struct vsp1_entity *entity;
	unsigned int i;
	int ret;

	mdev->dev = vsp1->dev;
	mdev->hw_revision = vsp1->version;
	strscpy(mdev->model, vsp1->info->model, sizeof(mdev->model));
	media_device_init(mdev);

	vsp1->media_ops.link_setup = vsp1_entity_link_setup;
	/*
	 * Don't perform link validation when the userspace API is disabled as
	 * the pipeline is configured internally by the driver in that case, and
	 * its configuration can thus be trusted.
	 */
	if (vsp1->info->uapi)
		vsp1->media_ops.link_validate = v4l2_subdev_link_validate;

	vdev->mdev = mdev;
	ret = v4l2_device_register(vsp1->dev, vdev);
	if (ret < 0) {
		dev_err(vsp1->dev, "V4L2 device registration failed (%d)\n",
			ret);
		goto done;
	}

	/* Instantiate all the entities. */
	if (vsp1_feature(vsp1, VSP1_HAS_BRS)) {
		vsp1->brs = vsp1_brx_create(vsp1, VSP1_ENTITY_BRS);
		if (IS_ERR(vsp1->brs)) {
			ret = PTR_ERR(vsp1->brs);
			goto done;
		}

		list_add_tail(&vsp1->brs->entity.list_dev, &vsp1->entities);
	}

	if (vsp1_feature(vsp1, VSP1_HAS_BRU)) {
		vsp1->bru = vsp1_brx_create(vsp1, VSP1_ENTITY_BRU);
		if (IS_ERR(vsp1->bru)) {
			ret = PTR_ERR(vsp1->bru);
			goto done;
		}

		list_add_tail(&vsp1->bru->entity.list_dev, &vsp1->entities);
	}

	if (vsp1_feature(vsp1, VSP1_HAS_CLU)) {
		vsp1->clu = vsp1_clu_create(vsp1);
		if (IS_ERR(vsp1->clu)) {
			ret = PTR_ERR(vsp1->clu);
			goto done;
		}

		list_add_tail(&vsp1->clu->entity.list_dev, &vsp1->entities);
	}

	vsp1->hsi = vsp1_hsit_create(vsp1, true);
	if (IS_ERR(vsp1->hsi)) {
		ret = PTR_ERR(vsp1->hsi);
		goto done;
	}

	list_add_tail(&vsp1->hsi->entity.list_dev, &vsp1->entities);

	vsp1->hst = vsp1_hsit_create(vsp1, false);
	if (IS_ERR(vsp1->hst)) {
		ret = PTR_ERR(vsp1->hst);
		goto done;
	}

	list_add_tail(&vsp1->hst->entity.list_dev, &vsp1->entities);

	if (vsp1_feature(vsp1, VSP1_HAS_HGO) && vsp1->info->uapi) {
		vsp1->hgo = vsp1_hgo_create(vsp1);
		if (IS_ERR(vsp1->hgo)) {
			ret = PTR_ERR(vsp1->hgo);
			goto done;
		}

		list_add_tail(&vsp1->hgo->histo.entity.list_dev,
			      &vsp1->entities);
	}

	if (vsp1_feature(vsp1, VSP1_HAS_HGT) && vsp1->info->uapi) {
		vsp1->hgt = vsp1_hgt_create(vsp1);
		if (IS_ERR(vsp1->hgt)) {
			ret = PTR_ERR(vsp1->hgt);
			goto done;
		}

		list_add_tail(&vsp1->hgt->histo.entity.list_dev,
			      &vsp1->entities);
	}

	/*
	 * The LIFs are only supported when used in conjunction with the DU, in
	 * which case the userspace API is disabled. If the userspace API is
	 * enabled skip the LIFs, even when present.
	 */
	if (!vsp1->info->uapi) {
		for (i = 0; i < vsp1->info->lif_count; ++i) {
			struct vsp1_lif *lif;

			lif = vsp1_lif_create(vsp1, i);
			if (IS_ERR(lif)) {
				ret = PTR_ERR(lif);
				goto done;
			}

			vsp1->lif[i] = lif;
			list_add_tail(&lif->entity.list_dev, &vsp1->entities);
		}
	}

	if (vsp1_feature(vsp1, VSP1_HAS_LUT)) {
		vsp1->lut = vsp1_lut_create(vsp1);
		if (IS_ERR(vsp1->lut)) {
			ret = PTR_ERR(vsp1->lut);
			goto done;
		}

		list_add_tail(&vsp1->lut->entity.list_dev, &vsp1->entities);
	}

	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		struct vsp1_rwpf *rpf;

		rpf = vsp1_rpf_create(vsp1, i);
		if (IS_ERR(rpf)) {
			ret = PTR_ERR(rpf);
			goto done;
		}

		vsp1->rpf[i] = rpf;
		list_add_tail(&rpf->entity.list_dev, &vsp1->entities);

		if (vsp1->info->uapi) {
			struct vsp1_video *video = vsp1_video_create(vsp1, rpf);

			if (IS_ERR(video)) {
				ret = PTR_ERR(video);
				goto done;
			}

			list_add_tail(&video->list, &vsp1->videos);
		}
	}

	if (vsp1_feature(vsp1, VSP1_HAS_SRU)) {
		vsp1->sru = vsp1_sru_create(vsp1);
		if (IS_ERR(vsp1->sru)) {
			ret = PTR_ERR(vsp1->sru);
			goto done;
		}

		list_add_tail(&vsp1->sru->entity.list_dev, &vsp1->entities);
	}

	for (i = 0; i < vsp1->info->uds_count; ++i) {
		struct vsp1_uds *uds;

		uds = vsp1_uds_create(vsp1, i);
		if (IS_ERR(uds)) {
			ret = PTR_ERR(uds);
			goto done;
		}

		vsp1->uds[i] = uds;
		list_add_tail(&uds->entity.list_dev, &vsp1->entities);
	}

	for (i = 0; i < vsp1->info->uif_count; ++i) {
		struct vsp1_uif *uif;

		uif = vsp1_uif_create(vsp1, i);
		if (IS_ERR(uif)) {
			ret = PTR_ERR(uif);
			goto done;
		}

		vsp1->uif[i] = uif;
		list_add_tail(&uif->entity.list_dev, &vsp1->entities);
	}

	for (i = 0; i < vsp1->info->wpf_count; ++i) {
		struct vsp1_rwpf *wpf;

		wpf = vsp1_wpf_create(vsp1, i);
		if (IS_ERR(wpf)) {
			ret = PTR_ERR(wpf);
			goto done;
		}

		vsp1->wpf[i] = wpf;
		list_add_tail(&wpf->entity.list_dev, &vsp1->entities);

		if (vsp1->info->uapi) {
			struct vsp1_video *video = vsp1_video_create(vsp1, wpf);

			if (IS_ERR(video)) {
				ret = PTR_ERR(video);
				goto done;
			}

			list_add_tail(&video->list, &vsp1->videos);
		}
	}

	/* Register all subdevs. */
	list_for_each_entry(entity, &vsp1->entities, list_dev) {
		ret = v4l2_device_register_subdev(&vsp1->v4l2_dev,
						  &entity->subdev);
		if (ret < 0)
			goto done;
	}

	/*
	 * Create links and register subdev nodes if the userspace API is
	 * enabled or initialize the DRM pipeline otherwise.
	 */
	if (vsp1->info->uapi) {
		ret = vsp1_uapi_create_links(vsp1);
		if (ret < 0)
			goto done;

		ret = v4l2_device_register_subdev_nodes(&vsp1->v4l2_dev);
		if (ret < 0)
			goto done;

		ret = media_device_register(mdev);
	} else {
		ret = vsp1_drm_init(vsp1);
	}

done:
	if (ret < 0)
		vsp1_destroy_entities(vsp1);

	return ret;
}

int vsp1_reset_wpf(struct vsp1_device *vsp1, unsigned int index)
{
	unsigned int timeout;
	u32 status;

	status = vsp1_read(vsp1, VI6_STATUS);
	if (!(status & VI6_STATUS_SYS_ACT(index)))
		return 0;

	vsp1_write(vsp1, VI6_SRESET, VI6_SRESET_SRTS(index));
	for (timeout = 10; timeout > 0; --timeout) {
		status = vsp1_read(vsp1, VI6_STATUS);
		if (!(status & VI6_STATUS_SYS_ACT(index)))
			break;

		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(vsp1->dev, "failed to reset wpf.%u\n", index);
		return -ETIMEDOUT;
	}

	return 0;
}

static int vsp1_device_init(struct vsp1_device *vsp1)
{
	unsigned int i;
	int ret;

	/* Reset any channel that might be running. */
	for (i = 0; i < vsp1->info->wpf_count; ++i) {
		ret = vsp1_reset_wpf(vsp1, i);
		if (ret < 0)
			return ret;
	}

	vsp1_write(vsp1, VI6_CLK_DCSWT, (8 << VI6_CLK_DCSWT_CSTPW_SHIFT) |
		   (8 << VI6_CLK_DCSWT_CSTRW_SHIFT));

	for (i = 0; i < vsp1->info->rpf_count; ++i)
		vsp1_write(vsp1, VI6_DPR_RPF_ROUTE(i), VI6_DPR_NODE_UNUSED);

	for (i = 0; i < vsp1->info->uds_count; ++i)
		vsp1_write(vsp1, VI6_DPR_UDS_ROUTE(i), VI6_DPR_NODE_UNUSED);

	for (i = 0; i < vsp1->info->uif_count; ++i)
		vsp1_write(vsp1, VI6_DPR_UIF_ROUTE(i), VI6_DPR_NODE_UNUSED);

	vsp1_write(vsp1, VI6_DPR_SRU_ROUTE, VI6_DPR_NODE_UNUSED);
	vsp1_write(vsp1, VI6_DPR_LUT_ROUTE, VI6_DPR_NODE_UNUSED);
	vsp1_write(vsp1, VI6_DPR_CLU_ROUTE, VI6_DPR_NODE_UNUSED);
	vsp1_write(vsp1, VI6_DPR_HST_ROUTE, VI6_DPR_NODE_UNUSED);
	vsp1_write(vsp1, VI6_DPR_HSI_ROUTE, VI6_DPR_NODE_UNUSED);
	vsp1_write(vsp1, VI6_DPR_BRU_ROUTE, VI6_DPR_NODE_UNUSED);

	if (vsp1_feature(vsp1, VSP1_HAS_BRS))
		vsp1_write(vsp1, VI6_DPR_ILV_BRS_ROUTE, VI6_DPR_NODE_UNUSED);

	vsp1_write(vsp1, VI6_DPR_HGO_SMPPT, (7 << VI6_DPR_SMPPT_TGW_SHIFT) |
		   (VI6_DPR_NODE_UNUSED << VI6_DPR_SMPPT_PT_SHIFT));
	vsp1_write(vsp1, VI6_DPR_HGT_SMPPT, (7 << VI6_DPR_SMPPT_TGW_SHIFT) |
		   (VI6_DPR_NODE_UNUSED << VI6_DPR_SMPPT_PT_SHIFT));

	vsp1_dlm_setup(vsp1);

	return 0;
}

static void vsp1_mask_all_interrupts(struct vsp1_device *vsp1)
{
	unsigned int i;

	for (i = 0; i < vsp1->info->lif_count; ++i)
		vsp1_write(vsp1, VI6_DISP_IRQ_ENB(i), 0);
	for (i = 0; i < vsp1->info->wpf_count; ++i)
		vsp1_write(vsp1, VI6_WPF_IRQ_ENB(i), 0);
}

/*
 * vsp1_device_get - Acquire the VSP1 device
 *
 * Make sure the device is not suspended and initialize it if needed.
 *
 * Return 0 on success or a negative error code otherwise.
 */
int vsp1_device_get(struct vsp1_device *vsp1)
{
	return pm_runtime_resume_and_get(vsp1->dev);
}

/*
 * vsp1_device_put - Release the VSP1 device
 *
 * Decrement the VSP1 reference count and cleanup the device if the last
 * reference is released.
 */
void vsp1_device_put(struct vsp1_device *vsp1)
{
	pm_runtime_put_sync(vsp1->dev);
}

/* -----------------------------------------------------------------------------
 * Power Management
 */

static int __maybe_unused vsp1_pm_suspend(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	/*
	 * When used as part of a display pipeline, the VSP is stopped and
	 * restarted explicitly by the DU.
	 */
	if (!vsp1->drm)
		vsp1_video_suspend(vsp1);

	pm_runtime_force_suspend(vsp1->dev);

	return 0;
}

static int __maybe_unused vsp1_pm_resume(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	pm_runtime_force_resume(vsp1->dev);

	/*
	 * When used as part of a display pipeline, the VSP is stopped and
	 * restarted explicitly by the DU.
	 */
	if (!vsp1->drm)
		vsp1_video_resume(vsp1);

	return 0;
}

static int __maybe_unused vsp1_pm_runtime_suspend(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	rcar_fcp_disable(vsp1->fcp);
	reset_control_assert(vsp1->rstc);

	return 0;
}

static int __maybe_unused vsp1_pm_runtime_resume(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(vsp1->rstc);
	if (ret < 0)
		return ret;

	if (vsp1->info) {
		/*
		 * On R-Car Gen2 and RZ/G1, vsp1 register access after deassert
		 * can cause lock-up. It is a special case and needs some delay
		 * to avoid this lock-up.
		 */
		if (vsp1->info->gen == 2)
			udelay(1);

		ret = vsp1_device_init(vsp1);
		if (ret < 0)
			goto done;
	}

	ret = rcar_fcp_enable(vsp1->fcp);

done:
	if (ret < 0)
		reset_control_assert(vsp1->rstc);

	return ret;
}

static const struct dev_pm_ops vsp1_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(vsp1_pm_suspend, vsp1_pm_resume)
	SET_RUNTIME_PM_OPS(vsp1_pm_runtime_suspend, vsp1_pm_runtime_resume, NULL)
};

/* -----------------------------------------------------------------------------
 * Platform Driver
 */

static const struct vsp1_device_info vsp1_device_infos[] = {
	{
		.version = VI6_IP_VERSION_MODEL_VSPS_H2,
		.model = "VSP1-S",
		.gen = 2,
		.features = VSP1_HAS_BRU | VSP1_HAS_CLU | VSP1_HAS_HGO
			  | VSP1_HAS_HGT | VSP1_HAS_LUT | VSP1_HAS_SRU
			  | VSP1_HAS_WPF_VFLIP,
		.rpf_count = 5,
		.uds_count = 3,
		.wpf_count = 4,
		.num_bru_inputs = 4,
		.uapi = true,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPR_H2,
		.model = "VSP1-R",
		.gen = 2,
		.features = VSP1_HAS_BRU | VSP1_HAS_SRU | VSP1_HAS_WPF_VFLIP,
		.rpf_count = 5,
		.uds_count = 3,
		.wpf_count = 4,
		.num_bru_inputs = 4,
		.uapi = true,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPD_GEN2,
		.model = "VSP1-D",
		.gen = 2,
		.features = VSP1_HAS_BRU | VSP1_HAS_HGO | VSP1_HAS_LUT,
		.lif_count = 1,
		.rpf_count = 4,
		.uds_count = 1,
		.wpf_count = 1,
		.num_bru_inputs = 4,
		.uapi = true,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPS_M2,
		.model = "VSP1-S",
		.gen = 2,
		.features = VSP1_HAS_BRU | VSP1_HAS_CLU | VSP1_HAS_HGO
			  | VSP1_HAS_HGT | VSP1_HAS_LUT | VSP1_HAS_SRU
			  | VSP1_HAS_WPF_VFLIP,
		.rpf_count = 5,
		.uds_count = 1,
		.wpf_count = 4,
		.num_bru_inputs = 4,
		.uapi = true,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPS_V2H,
		.model = "VSP1V-S",
		.gen = 2,
		.features = VSP1_HAS_BRU | VSP1_HAS_CLU | VSP1_HAS_LUT
			  | VSP1_HAS_SRU | VSP1_HAS_WPF_VFLIP,
		.rpf_count = 4,
		.uds_count = 1,
		.wpf_count = 4,
		.num_bru_inputs = 4,
		.uapi = true,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPD_V2H,
		.model = "VSP1V-D",
		.gen = 2,
		.features = VSP1_HAS_BRU | VSP1_HAS_CLU | VSP1_HAS_LUT,
		.lif_count = 1,
		.rpf_count = 4,
		.uds_count = 1,
		.wpf_count = 1,
		.num_bru_inputs = 4,
		.uapi = true,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPI_GEN3,
		.model = "VSP2-I",
		.gen = 3,
		.features = VSP1_HAS_CLU | VSP1_HAS_HGO | VSP1_HAS_HGT
			  | VSP1_HAS_LUT | VSP1_HAS_SRU | VSP1_HAS_WPF_HFLIP
			  | VSP1_HAS_WPF_VFLIP,
		.rpf_count = 1,
		.uds_count = 1,
		.wpf_count = 1,
		.uapi = true,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPBD_GEN3,
		.model = "VSP2-BD",
		.gen = 3,
		.features = VSP1_HAS_BRU | VSP1_HAS_WPF_VFLIP,
		.rpf_count = 5,
		.wpf_count = 1,
		.num_bru_inputs = 5,
		.uapi = true,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPBC_GEN3,
		.model = "VSP2-BC",
		.gen = 3,
		.features = VSP1_HAS_BRU | VSP1_HAS_CLU | VSP1_HAS_HGO
			  | VSP1_HAS_LUT | VSP1_HAS_WPF_VFLIP,
		.rpf_count = 5,
		.wpf_count = 1,
		.num_bru_inputs = 5,
		.uapi = true,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPBS_GEN3,
		.model = "VSP2-BS",
		.gen = 3,
		.features = VSP1_HAS_BRS | VSP1_HAS_WPF_VFLIP,
		.rpf_count = 2,
		.wpf_count = 1,
		.uapi = true,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPD_GEN3,
		.model = "VSP2-D",
		.gen = 3,
		.features = VSP1_HAS_BRU | VSP1_HAS_WPF_VFLIP | VSP1_HAS_EXT_DL,
		.lif_count = 1,
		.rpf_count = 5,
		.uif_count = 1,
		.wpf_count = 2,
		.num_bru_inputs = 5,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPD_V3,
		.model = "VSP2-D",
		.soc = VI6_IP_VERSION_SOC_V3H,
		.gen = 3,
		.features = VSP1_HAS_BRS | VSP1_HAS_BRU,
		.lif_count = 1,
		.rpf_count = 5,
		.uif_count = 1,
		.wpf_count = 1,
		.num_bru_inputs = 5,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPD_V3,
		.model = "VSP2-D",
		.soc = VI6_IP_VERSION_SOC_V3M,
		.gen = 3,
		.features = VSP1_HAS_BRS | VSP1_HAS_BRU | VSP1_HAS_NON_ZERO_LBA,
		.lif_count = 1,
		.rpf_count = 5,
		.uif_count = 1,
		.wpf_count = 1,
		.num_bru_inputs = 5,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPDL_GEN3,
		.model = "VSP2-DL",
		.gen = 3,
		.features = VSP1_HAS_BRS | VSP1_HAS_BRU | VSP1_HAS_EXT_DL,
		.lif_count = 2,
		.rpf_count = 5,
		.uif_count = 2,
		.wpf_count = 2,
		.num_bru_inputs = 5,
	}, {
		.version = VI6_IP_VERSION_MODEL_VSPD_GEN4,
		.model = "VSP2-D",
		.gen = 4,
		.features = VSP1_HAS_BRU | VSP1_HAS_EXT_DL,
		.lif_count = 1,
		.rpf_count = 5,
		.uif_count = 2,
		.wpf_count = 1,
		.num_bru_inputs = 5,
	},
};

static const struct vsp1_device_info rzg2l_vsp2_device_info = {
	.version = VI6_IP_VERSION_MODEL_VSPD_RZG2L,
	.model = "VSP2-D",
	.soc = VI6_IP_VERSION_SOC_RZG2L,
	.gen = 3,
	.features = VSP1_HAS_BRS | VSP1_HAS_WPF_VFLIP | VSP1_HAS_EXT_DL
		  | VSP1_HAS_NON_ZERO_LBA,
	.lif_count = 1,
	.rpf_count = 2,
	.wpf_count = 1,
};

static const struct vsp1_device_info *vsp1_lookup_info(struct vsp1_device *vsp1)
{
	const struct vsp1_device_info *info;
	unsigned int i;
	u32 model;
	u32 soc;

	/*
	 * Try the info stored in match data first for devices that don't have
	 * a version register.
	 */
	info = of_device_get_match_data(vsp1->dev);
	if (info) {
		vsp1->version = VI6_IP_VERSION_VSP_SW | info->version | info->soc;
		return info;
	}

	vsp1->version = vsp1_read(vsp1, VI6_IP_VERSION);
	model = vsp1->version & VI6_IP_VERSION_MODEL_MASK;
	soc = vsp1->version & VI6_IP_VERSION_SOC_MASK;

	for (i = 0; i < ARRAY_SIZE(vsp1_device_infos); ++i) {
		info = &vsp1_device_infos[i];

		if (model == info->version && (!info->soc || soc == info->soc))
			return info;
	}

	dev_err(vsp1->dev, "unsupported IP version 0x%08x\n", vsp1->version);

	return NULL;
}

static int vsp1_probe(struct platform_device *pdev)
{
	struct vsp1_device *vsp1;
	struct device_node *fcp_node;
	int ret;
	int irq;

	vsp1 = devm_kzalloc(&pdev->dev, sizeof(*vsp1), GFP_KERNEL);
	if (vsp1 == NULL)
		return -ENOMEM;

	vsp1->dev = &pdev->dev;
	INIT_LIST_HEAD(&vsp1->entities);
	INIT_LIST_HEAD(&vsp1->videos);

	platform_set_drvdata(pdev, vsp1);

	/* I/O and IRQ resources (clock managed by the clock PM domain). */
	vsp1->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vsp1->mmio))
		return PTR_ERR(vsp1->mmio);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	vsp1->rstc = devm_reset_control_get_shared(&pdev->dev, NULL);
	if (IS_ERR(vsp1->rstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(vsp1->rstc),
				     "failed to get reset control\n");

	/* FCP (optional). */
	fcp_node = of_parse_phandle(pdev->dev.of_node, "renesas,fcp", 0);
	if (fcp_node) {
		vsp1->fcp = rcar_fcp_get(fcp_node);
		of_node_put(fcp_node);
		if (IS_ERR(vsp1->fcp)) {
			dev_dbg(&pdev->dev, "FCP not found (%ld)\n",
				PTR_ERR(vsp1->fcp));
			return PTR_ERR(vsp1->fcp);
		}

		/*
		 * When the FCP is present, it handles all bus master accesses
		 * for the VSP and must thus be used in place of the VSP device
		 * to map DMA buffers.
		 */
		vsp1->bus_master = rcar_fcp_get_device(vsp1->fcp);
	} else {
		vsp1->bus_master = vsp1->dev;
	}

	/* Configure device parameters based on the version register. */
	pm_runtime_enable(&pdev->dev);

	ret = vsp1_device_get(vsp1);
	if (ret < 0)
		goto done;

	vsp1->info = vsp1_lookup_info(vsp1);
	if (!vsp1->info) {
		vsp1_device_put(vsp1);
		ret = -ENXIO;
		goto done;
	}

	dev_dbg(&pdev->dev, "IP version 0x%08x\n", vsp1->version);

	/*
	 * Previous use of the hardware (e.g. by the bootloader) could leave
	 * some interrupts enabled and pending.
	 *
	 * TODO: Investigate if this shouldn't be better handled by using the
	 * device reset provided by the CPG.
	 */
	vsp1_mask_all_interrupts(vsp1);

	vsp1_device_put(vsp1);

	ret = devm_request_irq(&pdev->dev, irq, vsp1_irq_handler,
			       IRQF_SHARED, dev_name(&pdev->dev), vsp1);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		goto done;
	}

	/* Instantiate entities. */
	ret = vsp1_create_entities(vsp1);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to create entities\n");
		goto done;
	}

done:
	if (ret) {
		pm_runtime_disable(&pdev->dev);
		rcar_fcp_put(vsp1->fcp);
	}

	return ret;
}

static void vsp1_remove(struct platform_device *pdev)
{
	struct vsp1_device *vsp1 = platform_get_drvdata(pdev);

	vsp1_destroy_entities(vsp1);
	rcar_fcp_put(vsp1->fcp);

	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id vsp1_of_match[] = {
	{ .compatible = "renesas,vsp1" },
	{ .compatible = "renesas,vsp2" },
	{ .compatible = "renesas,r9a07g044-vsp2", .data = &rzg2l_vsp2_device_info },
	{ },
};
MODULE_DEVICE_TABLE(of, vsp1_of_match);

static struct platform_driver vsp1_platform_driver = {
	.probe		= vsp1_probe,
	.remove		= vsp1_remove,
	.driver		= {
		.name	= "vsp1",
		.pm	= &vsp1_pm_ops,
		.of_match_table = vsp1_of_match,
	},
};

module_platform_driver(vsp1_platform_driver);

MODULE_ALIAS("vsp1");
MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas VSP1 Driver");
MODULE_LICENSE("GPL");
