// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_wpf.c  --  R-Car VSP1 Write Pixel Formatter
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_video.h"

#define WPF_GEN2_MAX_WIDTH			2048U
#define WPF_GEN2_MAX_HEIGHT			2048U
#define WPF_GEN3_MAX_WIDTH			8190U
#define WPF_GEN3_MAX_HEIGHT			8190U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_wpf_write(struct vsp1_rwpf *wpf,
				  struct vsp1_dl_body *dlb, u32 reg, u32 data)
{
	vsp1_dl_body_write(dlb, reg + wpf->entity.index * VI6_WPF_OFFSET, data);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

enum wpf_flip_ctrl {
	WPF_CTRL_VFLIP = 0,
	WPF_CTRL_HFLIP = 1,
};

static int vsp1_wpf_set_rotation(struct vsp1_rwpf *wpf, unsigned int rotation)
{
	struct vsp1_video *video = wpf->video;
	struct v4l2_mbus_framefmt *sink_format;
	struct v4l2_mbus_framefmt *source_format;
	bool rotate;
	int ret = 0;

	/*
	 * Only consider the 0°/180° from/to 90°/270° modifications, the rest
	 * is taken care of by the flipping configuration.
	 */
	rotate = rotation == 90 || rotation == 270;
	if (rotate == wpf->flip.rotate)
		return 0;

	/* Changing rotation isn't allowed when buffers are allocated. */
	mutex_lock(&video->lock);

	if (vb2_is_busy(&video->queue)) {
		ret = -EBUSY;
		goto done;
	}

	sink_format = vsp1_entity_get_pad_format(&wpf->entity,
						 wpf->entity.config,
						 RWPF_PAD_SINK);
	source_format = vsp1_entity_get_pad_format(&wpf->entity,
						   wpf->entity.config,
						   RWPF_PAD_SOURCE);

	mutex_lock(&wpf->entity.lock);

	if (rotate) {
		source_format->width = sink_format->height;
		source_format->height = sink_format->width;
	} else {
		source_format->width = sink_format->width;
		source_format->height = sink_format->height;
	}

	wpf->flip.rotate = rotate;

	mutex_unlock(&wpf->entity.lock);

done:
	mutex_unlock(&video->lock);
	return ret;
}

static int vsp1_wpf_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vsp1_rwpf *wpf =
		container_of(ctrl->handler, struct vsp1_rwpf, ctrls);
	unsigned int rotation;
	u32 flip = 0;
	int ret;

	/* Update the rotation. */
	rotation = wpf->flip.ctrls.rotate ? wpf->flip.ctrls.rotate->val : 0;
	ret = vsp1_wpf_set_rotation(wpf, rotation);
	if (ret < 0)
		return ret;

	/*
	 * Compute the flip value resulting from all three controls, with
	 * rotation by 180° flipping the image in both directions. Store the
	 * result in the pending flip field for the next frame that will be
	 * processed.
	 */
	if (wpf->flip.ctrls.vflip->val)
		flip |= BIT(WPF_CTRL_VFLIP);

	if (wpf->flip.ctrls.hflip && wpf->flip.ctrls.hflip->val)
		flip |= BIT(WPF_CTRL_HFLIP);

	if (rotation == 180 || rotation == 270)
		flip ^= BIT(WPF_CTRL_VFLIP) | BIT(WPF_CTRL_HFLIP);

	spin_lock_irq(&wpf->flip.lock);
	wpf->flip.pending = flip;
	spin_unlock_irq(&wpf->flip.lock);

	return 0;
}

static const struct v4l2_ctrl_ops vsp1_wpf_ctrl_ops = {
	.s_ctrl = vsp1_wpf_s_ctrl,
};

static int wpf_init_controls(struct vsp1_rwpf *wpf)
{
	struct vsp1_device *vsp1 = wpf->entity.vsp1;
	unsigned int num_flip_ctrls;

	spin_lock_init(&wpf->flip.lock);

	if (wpf->entity.index != 0) {
		/* Only WPF0 supports flipping. */
		num_flip_ctrls = 0;
	} else if (vsp1_feature(vsp1, VSP1_HAS_WPF_HFLIP)) {
		/*
		 * When horizontal flip is supported the WPF implements three
		 * controls (horizontal flip, vertical flip and rotation).
		 */
		num_flip_ctrls = 3;
	} else if (vsp1_feature(vsp1, VSP1_HAS_WPF_VFLIP)) {
		/*
		 * When only vertical flip is supported the WPF implements a
		 * single control (vertical flip).
		 */
		num_flip_ctrls = 1;
	} else {
		/* Otherwise flipping is not supported. */
		num_flip_ctrls = 0;
	}

	vsp1_rwpf_init_ctrls(wpf, num_flip_ctrls);

	if (num_flip_ctrls >= 1) {
		wpf->flip.ctrls.vflip =
			v4l2_ctrl_new_std(&wpf->ctrls, &vsp1_wpf_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	}

	if (num_flip_ctrls == 3) {
		wpf->flip.ctrls.hflip =
			v4l2_ctrl_new_std(&wpf->ctrls, &vsp1_wpf_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
		wpf->flip.ctrls.rotate =
			v4l2_ctrl_new_std(&wpf->ctrls, &vsp1_wpf_ctrl_ops,
					  V4L2_CID_ROTATE, 0, 270, 90, 0);
		v4l2_ctrl_cluster(3, &wpf->flip.ctrls.vflip);
	}

	if (wpf->ctrls.error) {
		dev_err(vsp1->dev, "wpf%u: failed to initialize controls\n",
			wpf->entity.index);
		return wpf->ctrls.error;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Core Operations
 */

static int wpf_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct vsp1_rwpf *wpf = to_rwpf(subdev);
	struct vsp1_device *vsp1 = wpf->entity.vsp1;

	if (enable)
		return 0;

	/*
	 * Write to registers directly when stopping the stream as there will be
	 * no pipeline run to apply the display list.
	 */
	vsp1_write(vsp1, VI6_WPF_IRQ_ENB(wpf->entity.index), 0);
	vsp1_write(vsp1, wpf->entity.index * VI6_WPF_OFFSET +
		   VI6_WPF_SRCRPF, 0);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static const struct v4l2_subdev_video_ops wpf_video_ops = {
	.s_stream = wpf_s_stream,
};

static const struct v4l2_subdev_ops wpf_ops = {
	.video	= &wpf_video_ops,
	.pad    = &vsp1_rwpf_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void vsp1_wpf_destroy(struct vsp1_entity *entity)
{
	struct vsp1_rwpf *wpf = entity_to_rwpf(entity);

	vsp1_dlm_destroy(wpf->dlm);
}

static int wpf_configure_writeback_chain(struct vsp1_rwpf *wpf,
					 struct vsp1_dl_list *dl)
{
	unsigned int index = wpf->entity.index;
	struct vsp1_dl_list *dl_next;
	struct vsp1_dl_body *dlb;

	dl_next = vsp1_dl_list_get(wpf->dlm);
	if (!dl_next) {
		dev_err(wpf->entity.vsp1->dev,
			"Failed to obtain a dl list, disabling writeback\n");
		return -ENOMEM;
	}

	dlb = vsp1_dl_list_get_body0(dl_next);
	vsp1_dl_body_write(dlb, VI6_WPF_WRBCK_CTRL(index), 0);
	vsp1_dl_list_add_chain(dl, dl_next);

	return 0;
}

static void wpf_configure_stream(struct vsp1_entity *entity,
				 struct vsp1_pipeline *pipe,
				 struct vsp1_dl_list *dl,
				 struct vsp1_dl_body *dlb)
{
	struct vsp1_rwpf *wpf = to_rwpf(&entity->subdev);
	struct vsp1_device *vsp1 = wpf->entity.vsp1;
	const struct v4l2_mbus_framefmt *source_format;
	const struct v4l2_mbus_framefmt *sink_format;
	unsigned int index = wpf->entity.index;
	unsigned int i;
	u32 outfmt = 0;
	u32 srcrpf = 0;
	int ret;

	sink_format = vsp1_entity_get_pad_format(&wpf->entity,
						 wpf->entity.config,
						 RWPF_PAD_SINK);
	source_format = vsp1_entity_get_pad_format(&wpf->entity,
						   wpf->entity.config,
						   RWPF_PAD_SOURCE);

	/* Format */
	if (!pipe->lif || wpf->writeback) {
		const struct v4l2_pix_format_mplane *format = &wpf->format;
		const struct vsp1_format_info *fmtinfo = wpf->fmtinfo;

		outfmt = fmtinfo->hwfmt << VI6_WPF_OUTFMT_WRFMT_SHIFT;

		if (wpf->flip.rotate)
			outfmt |= VI6_WPF_OUTFMT_ROT;

		if (fmtinfo->alpha)
			outfmt |= VI6_WPF_OUTFMT_PXA;
		if (fmtinfo->swap_yc)
			outfmt |= VI6_WPF_OUTFMT_SPYCS;
		if (fmtinfo->swap_uv)
			outfmt |= VI6_WPF_OUTFMT_SPUVS;

		/* Destination stride and byte swapping. */
		vsp1_wpf_write(wpf, dlb, VI6_WPF_DSTM_STRIDE_Y,
			       format->plane_fmt[0].bytesperline);
		if (format->num_planes > 1)
			vsp1_wpf_write(wpf, dlb, VI6_WPF_DSTM_STRIDE_C,
				       format->plane_fmt[1].bytesperline);

		vsp1_wpf_write(wpf, dlb, VI6_WPF_DSWAP, fmtinfo->swap);

		if (vsp1_feature(vsp1, VSP1_HAS_WPF_HFLIP) && index == 0)
			vsp1_wpf_write(wpf, dlb, VI6_WPF_ROT_CTRL,
				       VI6_WPF_ROT_CTRL_LN16 |
				       (256 << VI6_WPF_ROT_CTRL_LMEM_WD_SHIFT));
	}

	if (sink_format->code != source_format->code)
		outfmt |= VI6_WPF_OUTFMT_CSC;

	wpf->outfmt = outfmt;

	vsp1_dl_body_write(dlb, VI6_DPR_WPF_FPORCH(index),
			   VI6_DPR_WPF_FPORCH_FP_WPFN);

	/*
	 * Sources. If the pipeline has a single input and BRx is not used,
	 * configure it as the master layer. Otherwise configure all
	 * inputs as sub-layers and select the virtual RPF as the master
	 * layer.
	 */
	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		struct vsp1_rwpf *input = pipe->inputs[i];

		if (!input)
			continue;

		srcrpf |= (!pipe->brx && pipe->num_inputs == 1)
			? VI6_WPF_SRCRPF_RPF_ACT_MST(input->entity.index)
			: VI6_WPF_SRCRPF_RPF_ACT_SUB(input->entity.index);
	}

	if (pipe->brx)
		srcrpf |= pipe->brx->type == VSP1_ENTITY_BRU
			? VI6_WPF_SRCRPF_VIRACT_MST
			: VI6_WPF_SRCRPF_VIRACT2_MST;

	vsp1_wpf_write(wpf, dlb, VI6_WPF_SRCRPF, srcrpf);

	/* Enable interrupts. */
	vsp1_dl_body_write(dlb, VI6_WPF_IRQ_STA(index), 0);
	vsp1_dl_body_write(dlb, VI6_WPF_IRQ_ENB(index),
			   VI6_WPF_IRQ_ENB_DFEE);

	/*
	 * Configure writeback for display pipelines (the wpf writeback flag is
	 * never set for memory-to-memory pipelines). Start by adding a chained
	 * display list to disable writeback after a single frame, and process
	 * to enable writeback. If the display list allocation fails don't
	 * enable writeback as we wouldn't be able to safely disable it,
	 * resulting in possible memory corruption.
	 */
	if (wpf->writeback) {
		ret = wpf_configure_writeback_chain(wpf, dl);
		if (ret < 0)
			wpf->writeback = false;
	}

	vsp1_dl_body_write(dlb, VI6_WPF_WRBCK_CTRL(index),
			   wpf->writeback ? VI6_WPF_WRBCK_CTRL_WBMD : 0);
}

static void wpf_configure_frame(struct vsp1_entity *entity,
				struct vsp1_pipeline *pipe,
				struct vsp1_dl_list *dl,
				struct vsp1_dl_body *dlb)
{
	const unsigned int mask = BIT(WPF_CTRL_VFLIP)
				| BIT(WPF_CTRL_HFLIP);
	struct vsp1_rwpf *wpf = to_rwpf(&entity->subdev);
	unsigned long flags;
	u32 outfmt;

	spin_lock_irqsave(&wpf->flip.lock, flags);
	wpf->flip.active = (wpf->flip.active & ~mask)
			 | (wpf->flip.pending & mask);
	spin_unlock_irqrestore(&wpf->flip.lock, flags);

	outfmt = (wpf->alpha << VI6_WPF_OUTFMT_PDV_SHIFT) | wpf->outfmt;

	if (wpf->flip.active & BIT(WPF_CTRL_VFLIP))
		outfmt |= VI6_WPF_OUTFMT_FLP;
	if (wpf->flip.active & BIT(WPF_CTRL_HFLIP))
		outfmt |= VI6_WPF_OUTFMT_HFLP;

	vsp1_wpf_write(wpf, dlb, VI6_WPF_OUTFMT, outfmt);
}

static void wpf_configure_partition(struct vsp1_entity *entity,
				    struct vsp1_pipeline *pipe,
				    struct vsp1_dl_list *dl,
				    struct vsp1_dl_body *dlb)
{
	struct vsp1_rwpf *wpf = to_rwpf(&entity->subdev);
	struct vsp1_device *vsp1 = wpf->entity.vsp1;
	struct vsp1_rwpf_memory mem = wpf->mem;
	const struct v4l2_mbus_framefmt *sink_format;
	const struct v4l2_pix_format_mplane *format = &wpf->format;
	const struct vsp1_format_info *fmtinfo = wpf->fmtinfo;
	unsigned int width;
	unsigned int height;
	unsigned int left;
	unsigned int offset;
	unsigned int flip;
	unsigned int i;

	sink_format = vsp1_entity_get_pad_format(&wpf->entity,
						 wpf->entity.config,
						 RWPF_PAD_SINK);
	width = sink_format->width;
	height = sink_format->height;
	left = 0;

	/*
	 * Cropping. The partition algorithm can split the image into
	 * multiple slices.
	 */
	if (pipe->partitions > 1) {
		width = pipe->partition->wpf.width;
		left = pipe->partition->wpf.left;
	}

	vsp1_wpf_write(wpf, dlb, VI6_WPF_HSZCLIP, VI6_WPF_SZCLIP_EN |
		       (0 << VI6_WPF_SZCLIP_OFST_SHIFT) |
		       (width << VI6_WPF_SZCLIP_SIZE_SHIFT));
	vsp1_wpf_write(wpf, dlb, VI6_WPF_VSZCLIP, VI6_WPF_SZCLIP_EN |
		       (0 << VI6_WPF_SZCLIP_OFST_SHIFT) |
		       (height << VI6_WPF_SZCLIP_SIZE_SHIFT));

	/*
	 * For display pipelines without writeback enabled there's no memory
	 * address to configure, return now.
	 */
	if (pipe->lif && !wpf->writeback)
		return;

	/*
	 * Update the memory offsets based on flipping configuration.
	 * The destination addresses point to the locations where the
	 * VSP starts writing to memory, which can be any corner of the
	 * image depending on the combination of flipping and rotation.
	 */

	/*
	 * First take the partition left coordinate into account.
	 * Compute the offset to order the partitions correctly on the
	 * output based on whether flipping is enabled. Consider
	 * horizontal flipping when rotation is disabled but vertical
	 * flipping when rotation is enabled, as rotating the image
	 * switches the horizontal and vertical directions. The offset
	 * is applied horizontally or vertically accordingly.
	 */
	flip = wpf->flip.active;

	if (flip & BIT(WPF_CTRL_HFLIP) && !wpf->flip.rotate)
		offset = format->width - left - width;
	else if (flip & BIT(WPF_CTRL_VFLIP) && wpf->flip.rotate)
		offset = format->height - left - width;
	else
		offset = left;

	for (i = 0; i < format->num_planes; ++i) {
		unsigned int hsub = i > 0 ? fmtinfo->hsub : 1;
		unsigned int vsub = i > 0 ? fmtinfo->vsub : 1;

		if (wpf->flip.rotate)
			mem.addr[i] += offset / vsub
				     * format->plane_fmt[i].bytesperline;
		else
			mem.addr[i] += offset / hsub
				     * fmtinfo->bpp[i] / 8;
	}

	if (flip & BIT(WPF_CTRL_VFLIP)) {
		/*
		 * When rotating the output (after rotation) image
		 * height is equal to the partition width (before
		 * rotation). Otherwise it is equal to the output
		 * image height.
		 */
		if (wpf->flip.rotate)
			height = width;
		else
			height = format->height;

		mem.addr[0] += (height - 1)
			     * format->plane_fmt[0].bytesperline;

		if (format->num_planes > 1) {
			offset = (height / fmtinfo->vsub - 1)
			       * format->plane_fmt[1].bytesperline;
			mem.addr[1] += offset;
			mem.addr[2] += offset;
		}
	}

	if (wpf->flip.rotate && !(flip & BIT(WPF_CTRL_HFLIP))) {
		unsigned int hoffset = max(0, (int)format->width - 16);

		/*
		 * Compute the output coordinate. The partition
		 * horizontal (left) offset becomes a vertical offset.
		 */
		for (i = 0; i < format->num_planes; ++i) {
			unsigned int hsub = i > 0 ? fmtinfo->hsub : 1;

			mem.addr[i] += hoffset / hsub
				     * fmtinfo->bpp[i] / 8;
		}
	}

	/*
	 * On Gen3 hardware the SPUVS bit has no effect on 3-planar
	 * formats. Swap the U and V planes manually in that case.
	 */
	if (vsp1->info->gen == 3 && format->num_planes == 3 &&
	    fmtinfo->swap_uv)
		swap(mem.addr[1], mem.addr[2]);

	vsp1_wpf_write(wpf, dlb, VI6_WPF_DSTM_ADDR_Y, mem.addr[0]);
	vsp1_wpf_write(wpf, dlb, VI6_WPF_DSTM_ADDR_C0, mem.addr[1]);
	vsp1_wpf_write(wpf, dlb, VI6_WPF_DSTM_ADDR_C1, mem.addr[2]);

	/*
	 * Writeback operates in single-shot mode and lasts for a single frame,
	 * reset the writeback flag to false for the next frame.
	 */
	wpf->writeback = false;
}

static unsigned int wpf_max_width(struct vsp1_entity *entity,
				  struct vsp1_pipeline *pipe)
{
	struct vsp1_rwpf *wpf = to_rwpf(&entity->subdev);

	return wpf->flip.rotate ? 256 : wpf->max_width;
}

static void wpf_partition(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_partition *partition,
			  unsigned int partition_idx,
			  struct vsp1_partition_window *window)
{
	partition->wpf = *window;
}

static const struct vsp1_entity_operations wpf_entity_ops = {
	.destroy = vsp1_wpf_destroy,
	.configure_stream = wpf_configure_stream,
	.configure_frame = wpf_configure_frame,
	.configure_partition = wpf_configure_partition,
	.max_width = wpf_max_width,
	.partition = wpf_partition,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_rwpf *vsp1_wpf_create(struct vsp1_device *vsp1, unsigned int index)
{
	struct vsp1_rwpf *wpf;
	char name[6];
	int ret;

	wpf = devm_kzalloc(vsp1->dev, sizeof(*wpf), GFP_KERNEL);
	if (wpf == NULL)
		return ERR_PTR(-ENOMEM);

	if (vsp1->info->gen == 2) {
		wpf->max_width = WPF_GEN2_MAX_WIDTH;
		wpf->max_height = WPF_GEN2_MAX_HEIGHT;
	} else {
		wpf->max_width = WPF_GEN3_MAX_WIDTH;
		wpf->max_height = WPF_GEN3_MAX_HEIGHT;
	}

	wpf->entity.ops = &wpf_entity_ops;
	wpf->entity.type = VSP1_ENTITY_WPF;
	wpf->entity.index = index;

	sprintf(name, "wpf.%u", index);
	ret = vsp1_entity_init(vsp1, &wpf->entity, name, 2, &wpf_ops,
			       MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the display list manager. */
	wpf->dlm = vsp1_dlm_create(vsp1, index, 64);
	if (!wpf->dlm) {
		ret = -ENOMEM;
		goto error;
	}

	/* Initialize the control handler. */
	ret = wpf_init_controls(wpf);
	if (ret < 0) {
		dev_err(vsp1->dev, "wpf%u: failed to initialize controls\n",
			index);
		goto error;
	}

	v4l2_ctrl_handler_setup(&wpf->ctrls);

	return wpf;

error:
	vsp1_entity_destroy(&wpf->entity);
	return ERR_PTR(ret);
}
