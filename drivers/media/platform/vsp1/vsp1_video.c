// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_video.c  --  R-Car VSP1 Video Node
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>
#include <linux/wait.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "vsp1.h"
#include "vsp1_brx.h"
#include "vsp1_dl.h"
#include "vsp1_entity.h"
#include "vsp1_hgo.h"
#include "vsp1_hgt.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_uds.h"
#include "vsp1_video.h"

#define VSP1_VIDEO_DEF_FORMAT		V4L2_PIX_FMT_YUYV
#define VSP1_VIDEO_DEF_WIDTH		1024
#define VSP1_VIDEO_DEF_HEIGHT		768

#define VSP1_VIDEO_MAX_WIDTH		8190U
#define VSP1_VIDEO_MAX_HEIGHT		8190U

/* -----------------------------------------------------------------------------
 * Helper functions
 */

static struct v4l2_subdev *
vsp1_video_remote_subdev(struct media_pad *local, u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_pad(local);
	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

static int vsp1_video_verify_format(struct vsp1_video *video)
{
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	int ret;

	subdev = vsp1_video_remote_subdev(&video->pad, &fmt.pad);
	if (subdev == NULL)
		return -EINVAL;

	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -EINVAL : ret;

	if (video->rwpf->fmtinfo->mbus != fmt.format.code ||
	    video->rwpf->format.height != fmt.format.height ||
	    video->rwpf->format.width != fmt.format.width)
		return -EINVAL;

	return 0;
}

static int __vsp1_video_try_format(struct vsp1_video *video,
				   struct v4l2_pix_format_mplane *pix,
				   const struct vsp1_format_info **fmtinfo)
{
	static const u32 xrgb_formats[][2] = {
		{ V4L2_PIX_FMT_RGB444, V4L2_PIX_FMT_XRGB444 },
		{ V4L2_PIX_FMT_RGB555, V4L2_PIX_FMT_XRGB555 },
		{ V4L2_PIX_FMT_BGR32, V4L2_PIX_FMT_XBGR32 },
		{ V4L2_PIX_FMT_RGB32, V4L2_PIX_FMT_XRGB32 },
	};

	const struct vsp1_format_info *info;
	unsigned int width = pix->width;
	unsigned int height = pix->height;
	unsigned int i;

	/*
	 * Backward compatibility: replace deprecated RGB formats by their XRGB
	 * equivalent. This selects the format older userspace applications want
	 * while still exposing the new format.
	 */
	for (i = 0; i < ARRAY_SIZE(xrgb_formats); ++i) {
		if (xrgb_formats[i][0] == pix->pixelformat) {
			pix->pixelformat = xrgb_formats[i][1];
			break;
		}
	}

	/*
	 * Retrieve format information and select the default format if the
	 * requested format isn't supported.
	 */
	info = vsp1_get_format_info(video->vsp1, pix->pixelformat);
	if (info == NULL)
		info = vsp1_get_format_info(video->vsp1, VSP1_VIDEO_DEF_FORMAT);

	pix->pixelformat = info->fourcc;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->field = V4L2_FIELD_NONE;

	if (info->fourcc == V4L2_PIX_FMT_HSV24 ||
	    info->fourcc == V4L2_PIX_FMT_HSV32)
		pix->hsv_enc = V4L2_HSV_ENC_256;

	memset(pix->reserved, 0, sizeof(pix->reserved));

	/* Align the width and height for YUV 4:2:2 and 4:2:0 formats. */
	width = round_down(width, info->hsub);
	height = round_down(height, info->vsub);

	/* Clamp the width and height. */
	pix->width = clamp(width, info->hsub, VSP1_VIDEO_MAX_WIDTH);
	pix->height = clamp(height, info->vsub, VSP1_VIDEO_MAX_HEIGHT);

	/*
	 * Compute and clamp the stride and image size. While not documented in
	 * the datasheet, strides not aligned to a multiple of 128 bytes result
	 * in image corruption.
	 */
	for (i = 0; i < min(info->planes, 2U); ++i) {
		unsigned int hsub = i > 0 ? info->hsub : 1;
		unsigned int vsub = i > 0 ? info->vsub : 1;
		unsigned int align = 128;
		unsigned int bpl;

		bpl = clamp_t(unsigned int, pix->plane_fmt[i].bytesperline,
			      pix->width / hsub * info->bpp[i] / 8,
			      round_down(65535U, align));

		pix->plane_fmt[i].bytesperline = round_up(bpl, align);
		pix->plane_fmt[i].sizeimage = pix->plane_fmt[i].bytesperline
					    * pix->height / vsub;
	}

	if (info->planes == 3) {
		/* The second and third planes must have the same stride. */
		pix->plane_fmt[2].bytesperline = pix->plane_fmt[1].bytesperline;
		pix->plane_fmt[2].sizeimage = pix->plane_fmt[1].sizeimage;
	}

	pix->num_planes = info->planes;

	if (fmtinfo)
		*fmtinfo = info;

	return 0;
}

/* -----------------------------------------------------------------------------
 * VSP1 Partition Algorithm support
 */

/**
 * vsp1_video_calculate_partition - Calculate the active partition output window
 *
 * @pipe: the pipeline
 * @partition: partition that will hold the calculated values
 * @div_size: pre-determined maximum partition division size
 * @index: partition index
 */
static void vsp1_video_calculate_partition(struct vsp1_pipeline *pipe,
					   struct vsp1_partition *partition,
					   unsigned int div_size,
					   unsigned int index)
{
	const struct v4l2_mbus_framefmt *format;
	struct vsp1_partition_window window;
	unsigned int modulus;

	/*
	 * Partitions are computed on the size before rotation, use the format
	 * at the WPF sink.
	 */
	format = vsp1_entity_get_pad_format(&pipe->output->entity,
					    pipe->output->entity.config,
					    RWPF_PAD_SINK);

	/* A single partition simply processes the output size in full. */
	if (pipe->partitions <= 1) {
		window.left = 0;
		window.width = format->width;

		vsp1_pipeline_propagate_partition(pipe, partition, index,
						  &window);
		return;
	}

	/* Initialise the partition with sane starting conditions. */
	window.left = index * div_size;
	window.width = div_size;

	modulus = format->width % div_size;

	/*
	 * We need to prevent the last partition from being smaller than the
	 * *minimum* width of the hardware capabilities.
	 *
	 * If the modulus is less than half of the partition size,
	 * the penultimate partition is reduced to half, which is added
	 * to the final partition: |1234|1234|1234|12|341|
	 * to prevent this:        |1234|1234|1234|1234|1|.
	 */
	if (modulus) {
		/*
		 * pipe->partitions is 1 based, whilst index is a 0 based index.
		 * Normalise this locally.
		 */
		unsigned int partitions = pipe->partitions - 1;

		if (modulus < div_size / 2) {
			if (index == partitions - 1) {
				/* Halve the penultimate partition. */
				window.width = div_size / 2;
			} else if (index == partitions) {
				/* Increase the final partition. */
				window.width = (div_size / 2) + modulus;
				window.left -= div_size / 2;
			}
		} else if (index == partitions) {
			window.width = modulus;
		}
	}

	vsp1_pipeline_propagate_partition(pipe, partition, index, &window);
}

static int vsp1_video_pipeline_setup_partitions(struct vsp1_pipeline *pipe)
{
	struct vsp1_device *vsp1 = pipe->output->entity.vsp1;
	const struct v4l2_mbus_framefmt *format;
	struct vsp1_entity *entity;
	unsigned int div_size;
	unsigned int i;

	/*
	 * Partitions are computed on the size before rotation, use the format
	 * at the WPF sink.
	 */
	format = vsp1_entity_get_pad_format(&pipe->output->entity,
					    pipe->output->entity.config,
					    RWPF_PAD_SINK);
	div_size = format->width;

	/*
	 * Only Gen3 hardware requires image partitioning, Gen2 will operate
	 * with a single partition that covers the whole output.
	 */
	if (vsp1->info->gen == 3) {
		list_for_each_entry(entity, &pipe->entities, list_pipe) {
			unsigned int entity_max;

			if (!entity->ops->max_width)
				continue;

			entity_max = entity->ops->max_width(entity, pipe);
			if (entity_max)
				div_size = min(div_size, entity_max);
		}
	}

	pipe->partitions = DIV_ROUND_UP(format->width, div_size);
	pipe->part_table = kcalloc(pipe->partitions, sizeof(*pipe->part_table),
				   GFP_KERNEL);
	if (!pipe->part_table)
		return -ENOMEM;

	for (i = 0; i < pipe->partitions; ++i)
		vsp1_video_calculate_partition(pipe, &pipe->part_table[i],
					       div_size, i);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Pipeline Management
 */

/*
 * vsp1_video_complete_buffer - Complete the current buffer
 * @video: the video node
 *
 * This function completes the current buffer by filling its sequence number,
 * time stamp and payload size, and hands it back to the videobuf core.
 *
 * Return the next queued buffer or NULL if the queue is empty.
 */
static struct vsp1_vb2_buffer *
vsp1_video_complete_buffer(struct vsp1_video *video)
{
	struct vsp1_pipeline *pipe = video->rwpf->entity.pipe;
	struct vsp1_vb2_buffer *next = NULL;
	struct vsp1_vb2_buffer *done;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&video->irqlock, flags);

	if (list_empty(&video->irqqueue)) {
		spin_unlock_irqrestore(&video->irqlock, flags);
		return NULL;
	}

	done = list_first_entry(&video->irqqueue,
				struct vsp1_vb2_buffer, queue);

	list_del(&done->queue);

	if (!list_empty(&video->irqqueue))
		next = list_first_entry(&video->irqqueue,
					struct vsp1_vb2_buffer, queue);

	spin_unlock_irqrestore(&video->irqlock, flags);

	done->buf.sequence = pipe->sequence;
	done->buf.vb2_buf.timestamp = ktime_get_ns();
	for (i = 0; i < done->buf.vb2_buf.num_planes; ++i)
		vb2_set_plane_payload(&done->buf.vb2_buf, i,
				      vb2_plane_size(&done->buf.vb2_buf, i));
	vb2_buffer_done(&done->buf.vb2_buf, VB2_BUF_STATE_DONE);

	return next;
}

static void vsp1_video_frame_end(struct vsp1_pipeline *pipe,
				 struct vsp1_rwpf *rwpf)
{
	struct vsp1_video *video = rwpf->video;
	struct vsp1_vb2_buffer *buf;

	buf = vsp1_video_complete_buffer(video);
	if (buf == NULL)
		return;

	video->rwpf->mem = buf->mem;
	pipe->buffers_ready |= 1 << video->pipe_index;
}

static void vsp1_video_pipeline_run_partition(struct vsp1_pipeline *pipe,
					      struct vsp1_dl_list *dl,
					      unsigned int partition)
{
	struct vsp1_dl_body *dlb = vsp1_dl_list_get_body0(dl);
	struct vsp1_entity *entity;

	pipe->partition = &pipe->part_table[partition];

	list_for_each_entry(entity, &pipe->entities, list_pipe)
		vsp1_entity_configure_partition(entity, pipe, dl, dlb);
}

static void vsp1_video_pipeline_run(struct vsp1_pipeline *pipe)
{
	struct vsp1_device *vsp1 = pipe->output->entity.vsp1;
	struct vsp1_entity *entity;
	struct vsp1_dl_body *dlb;
	struct vsp1_dl_list *dl;
	unsigned int partition;

	dl = vsp1_dl_list_get(pipe->output->dlm);

	/*
	 * If the VSP hardware isn't configured yet (which occurs either when
	 * processing the first frame or after a system suspend/resume), add the
	 * cached stream configuration to the display list to perform a full
	 * initialisation.
	 */
	if (!pipe->configured)
		vsp1_dl_list_add_body(dl, pipe->stream_config);

	dlb = vsp1_dl_list_get_body0(dl);

	list_for_each_entry(entity, &pipe->entities, list_pipe)
		vsp1_entity_configure_frame(entity, pipe, dl, dlb);

	/* Run the first partition. */
	vsp1_video_pipeline_run_partition(pipe, dl, 0);

	/* Process consecutive partitions as necessary. */
	for (partition = 1; partition < pipe->partitions; ++partition) {
		struct vsp1_dl_list *dl_next;

		dl_next = vsp1_dl_list_get(pipe->output->dlm);

		/*
		 * An incomplete chain will still function, but output only
		 * the partitions that had a dl available. The frame end
		 * interrupt will be marked on the last dl in the chain.
		 */
		if (!dl_next) {
			dev_err(vsp1->dev, "Failed to obtain a dl list. Frame will be incomplete\n");
			break;
		}

		vsp1_video_pipeline_run_partition(pipe, dl_next, partition);
		vsp1_dl_list_add_chain(dl, dl_next);
	}

	/* Complete, and commit the head display list. */
	vsp1_dl_list_commit(dl, 0);
	pipe->configured = true;

	vsp1_pipeline_run(pipe);
}

static void vsp1_video_pipeline_frame_end(struct vsp1_pipeline *pipe,
					  unsigned int completion)
{
	struct vsp1_device *vsp1 = pipe->output->entity.vsp1;
	enum vsp1_pipeline_state state;
	unsigned long flags;
	unsigned int i;

	/* M2M Pipelines should never call here with an incomplete frame. */
	WARN_ON_ONCE(!(completion & VSP1_DL_FRAME_END_COMPLETED));

	spin_lock_irqsave(&pipe->irqlock, flags);

	/* Complete buffers on all video nodes. */
	for (i = 0; i < vsp1->info->rpf_count; ++i) {
		if (!pipe->inputs[i])
			continue;

		vsp1_video_frame_end(pipe, pipe->inputs[i]);
	}

	vsp1_video_frame_end(pipe, pipe->output);

	state = pipe->state;
	pipe->state = VSP1_PIPELINE_STOPPED;

	/*
	 * If a stop has been requested, mark the pipeline as stopped and
	 * return. Otherwise restart the pipeline if ready.
	 */
	if (state == VSP1_PIPELINE_STOPPING)
		wake_up(&pipe->wq);
	else if (vsp1_pipeline_ready(pipe))
		vsp1_video_pipeline_run(pipe);

	spin_unlock_irqrestore(&pipe->irqlock, flags);
}

static int vsp1_video_pipeline_build_branch(struct vsp1_pipeline *pipe,
					    struct vsp1_rwpf *input,
					    struct vsp1_rwpf *output)
{
	struct media_entity_enum ent_enum;
	struct vsp1_entity *entity;
	struct media_pad *pad;
	struct vsp1_brx *brx = NULL;
	int ret;

	ret = media_entity_enum_init(&ent_enum, &input->entity.vsp1->media_dev);
	if (ret < 0)
		return ret;

	/*
	 * The main data path doesn't include the HGO or HGT, use
	 * vsp1_entity_remote_pad() to traverse the graph.
	 */

	pad = vsp1_entity_remote_pad(&input->entity.pads[RWPF_PAD_SOURCE]);

	while (1) {
		if (pad == NULL) {
			ret = -EPIPE;
			goto out;
		}

		/* We've reached a video node, that shouldn't have happened. */
		if (!is_media_entity_v4l2_subdev(pad->entity)) {
			ret = -EPIPE;
			goto out;
		}

		entity = to_vsp1_entity(
			media_entity_to_v4l2_subdev(pad->entity));

		/*
		 * A BRU or BRS is present in the pipeline, store its input pad
		 * number in the input RPF for use when configuring the RPF.
		 */
		if (entity->type == VSP1_ENTITY_BRU ||
		    entity->type == VSP1_ENTITY_BRS) {
			/* BRU and BRS can't be chained. */
			if (brx) {
				ret = -EPIPE;
				goto out;
			}

			brx = to_brx(&entity->subdev);
			brx->inputs[pad->index].rpf = input;
			input->brx_input = pad->index;
		}

		/* We've reached the WPF, we're done. */
		if (entity->type == VSP1_ENTITY_WPF)
			break;

		/* Ensure the branch has no loop. */
		if (media_entity_enum_test_and_set(&ent_enum,
						   &entity->subdev.entity)) {
			ret = -EPIPE;
			goto out;
		}

		/* UDS can't be chained. */
		if (entity->type == VSP1_ENTITY_UDS) {
			if (pipe->uds) {
				ret = -EPIPE;
				goto out;
			}

			pipe->uds = entity;
			pipe->uds_input = brx ? &brx->entity : &input->entity;
		}

		/* Follow the source link, ignoring any HGO or HGT. */
		pad = &entity->pads[entity->source_pad];
		pad = vsp1_entity_remote_pad(pad);
	}

	/* The last entity must be the output WPF. */
	if (entity != &output->entity)
		ret = -EPIPE;

out:
	media_entity_enum_cleanup(&ent_enum);

	return ret;
}

static int vsp1_video_pipeline_build(struct vsp1_pipeline *pipe,
				     struct vsp1_video *video)
{
	struct media_graph graph;
	struct media_entity *entity = &video->video.entity;
	struct media_device *mdev = entity->graph_obj.mdev;
	unsigned int i;
	int ret;

	/* Walk the graph to locate the entities and video nodes. */
	ret = media_graph_walk_init(&graph, mdev);
	if (ret)
		return ret;

	media_graph_walk_start(&graph, entity);

	while ((entity = media_graph_walk_next(&graph))) {
		struct v4l2_subdev *subdev;
		struct vsp1_rwpf *rwpf;
		struct vsp1_entity *e;

		if (!is_media_entity_v4l2_subdev(entity))
			continue;

		subdev = media_entity_to_v4l2_subdev(entity);
		e = to_vsp1_entity(subdev);
		list_add_tail(&e->list_pipe, &pipe->entities);
		e->pipe = pipe;

		switch (e->type) {
		case VSP1_ENTITY_RPF:
			rwpf = to_rwpf(subdev);
			pipe->inputs[rwpf->entity.index] = rwpf;
			rwpf->video->pipe_index = ++pipe->num_inputs;
			break;

		case VSP1_ENTITY_WPF:
			rwpf = to_rwpf(subdev);
			pipe->output = rwpf;
			rwpf->video->pipe_index = 0;
			break;

		case VSP1_ENTITY_LIF:
			pipe->lif = e;
			break;

		case VSP1_ENTITY_BRU:
		case VSP1_ENTITY_BRS:
			pipe->brx = e;
			break;

		case VSP1_ENTITY_HGO:
			pipe->hgo = e;
			break;

		case VSP1_ENTITY_HGT:
			pipe->hgt = e;
			break;

		default:
			break;
		}
	}

	media_graph_walk_cleanup(&graph);

	/* We need one output and at least one input. */
	if (pipe->num_inputs == 0 || !pipe->output)
		return -EPIPE;

	/*
	 * Follow links downstream for each input and make sure the graph
	 * contains no loop and that all branches end at the output WPF.
	 */
	for (i = 0; i < video->vsp1->info->rpf_count; ++i) {
		if (!pipe->inputs[i])
			continue;

		ret = vsp1_video_pipeline_build_branch(pipe, pipe->inputs[i],
						       pipe->output);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int vsp1_video_pipeline_init(struct vsp1_pipeline *pipe,
				    struct vsp1_video *video)
{
	vsp1_pipeline_init(pipe);

	pipe->frame_end = vsp1_video_pipeline_frame_end;

	return vsp1_video_pipeline_build(pipe, video);
}

static struct vsp1_pipeline *vsp1_video_pipeline_get(struct vsp1_video *video)
{
	struct vsp1_pipeline *pipe;
	int ret;

	/*
	 * Get a pipeline object for the video node. If a pipeline has already
	 * been allocated just increment its reference count and return it.
	 * Otherwise allocate a new pipeline and initialize it, it will be freed
	 * when the last reference is released.
	 */
	if (!video->rwpf->entity.pipe) {
		pipe = kzalloc(sizeof(*pipe), GFP_KERNEL);
		if (!pipe)
			return ERR_PTR(-ENOMEM);

		ret = vsp1_video_pipeline_init(pipe, video);
		if (ret < 0) {
			vsp1_pipeline_reset(pipe);
			kfree(pipe);
			return ERR_PTR(ret);
		}
	} else {
		pipe = video->rwpf->entity.pipe;
		kref_get(&pipe->kref);
	}

	return pipe;
}

static void vsp1_video_pipeline_release(struct kref *kref)
{
	struct vsp1_pipeline *pipe = container_of(kref, typeof(*pipe), kref);

	vsp1_pipeline_reset(pipe);
	kfree(pipe);
}

static void vsp1_video_pipeline_put(struct vsp1_pipeline *pipe)
{
	struct media_device *mdev = &pipe->output->entity.vsp1->media_dev;

	mutex_lock(&mdev->graph_mutex);
	kref_put(&pipe->kref, vsp1_video_pipeline_release);
	mutex_unlock(&mdev->graph_mutex);
}

/* -----------------------------------------------------------------------------
 * videobuf2 Queue Operations
 */

static int
vsp1_video_queue_setup(struct vb2_queue *vq,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct vsp1_video *video = vb2_get_drv_priv(vq);
	const struct v4l2_pix_format_mplane *format = &video->rwpf->format;
	unsigned int i;

	if (*nplanes) {
		if (*nplanes != format->num_planes)
			return -EINVAL;

		for (i = 0; i < *nplanes; i++)
			if (sizes[i] < format->plane_fmt[i].sizeimage)
				return -EINVAL;
		return 0;
	}

	*nplanes = format->num_planes;

	for (i = 0; i < format->num_planes; ++i)
		sizes[i] = format->plane_fmt[i].sizeimage;

	return 0;
}

static int vsp1_video_buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vsp1_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct vsp1_vb2_buffer *buf = to_vsp1_vb2_buffer(vbuf);
	const struct v4l2_pix_format_mplane *format = &video->rwpf->format;
	unsigned int i;

	if (vb->num_planes < format->num_planes)
		return -EINVAL;

	for (i = 0; i < vb->num_planes; ++i) {
		buf->mem.addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);

		if (vb2_plane_size(vb, i) < format->plane_fmt[i].sizeimage)
			return -EINVAL;
	}

	for ( ; i < 3; ++i)
		buf->mem.addr[i] = 0;

	return 0;
}

static void vsp1_video_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vsp1_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct vsp1_pipeline *pipe = video->rwpf->entity.pipe;
	struct vsp1_vb2_buffer *buf = to_vsp1_vb2_buffer(vbuf);
	unsigned long flags;
	bool empty;

	spin_lock_irqsave(&video->irqlock, flags);
	empty = list_empty(&video->irqqueue);
	list_add_tail(&buf->queue, &video->irqqueue);
	spin_unlock_irqrestore(&video->irqlock, flags);

	if (!empty)
		return;

	spin_lock_irqsave(&pipe->irqlock, flags);

	video->rwpf->mem = buf->mem;
	pipe->buffers_ready |= 1 << video->pipe_index;

	if (vb2_is_streaming(&video->queue) &&
	    vsp1_pipeline_ready(pipe))
		vsp1_video_pipeline_run(pipe);

	spin_unlock_irqrestore(&pipe->irqlock, flags);
}

static int vsp1_video_setup_pipeline(struct vsp1_pipeline *pipe)
{
	struct vsp1_entity *entity;
	int ret;

	/* Determine this pipelines sizes for image partitioning support. */
	ret = vsp1_video_pipeline_setup_partitions(pipe);
	if (ret < 0)
		return ret;

	if (pipe->uds) {
		struct vsp1_uds *uds = to_uds(&pipe->uds->subdev);

		/*
		 * If a BRU or BRS is present in the pipeline before the UDS,
		 * the alpha component doesn't need to be scaled as the BRU and
		 * BRS output alpha value is fixed to 255. Otherwise we need to
		 * scale the alpha component only when available at the input
		 * RPF.
		 */
		if (pipe->uds_input->type == VSP1_ENTITY_BRU ||
		    pipe->uds_input->type == VSP1_ENTITY_BRS) {
			uds->scale_alpha = false;
		} else {
			struct vsp1_rwpf *rpf =
				to_rwpf(&pipe->uds_input->subdev);

			uds->scale_alpha = rpf->fmtinfo->alpha;
		}
	}

	/*
	 * Compute and cache the stream configuration into a body. The cached
	 * body will be added to the display list by vsp1_video_pipeline_run()
	 * whenever the pipeline needs to be fully reconfigured.
	 */
	pipe->stream_config = vsp1_dlm_dl_body_get(pipe->output->dlm);
	if (!pipe->stream_config)
		return -ENOMEM;

	list_for_each_entry(entity, &pipe->entities, list_pipe) {
		vsp1_entity_route_setup(entity, pipe, pipe->stream_config);
		vsp1_entity_configure_stream(entity, pipe, NULL,
					     pipe->stream_config);
	}

	return 0;
}

static void vsp1_video_release_buffers(struct vsp1_video *video)
{
	struct vsp1_vb2_buffer *buffer;
	unsigned long flags;

	/* Remove all buffers from the IRQ queue. */
	spin_lock_irqsave(&video->irqlock, flags);
	list_for_each_entry(buffer, &video->irqqueue, queue)
		vb2_buffer_done(&buffer->buf.vb2_buf, VB2_BUF_STATE_ERROR);
	INIT_LIST_HEAD(&video->irqqueue);
	spin_unlock_irqrestore(&video->irqlock, flags);
}

static void vsp1_video_cleanup_pipeline(struct vsp1_pipeline *pipe)
{
	lockdep_assert_held(&pipe->lock);

	/* Release any cached configuration from our output video. */
	vsp1_dl_body_put(pipe->stream_config);
	pipe->stream_config = NULL;
	pipe->configured = false;

	/* Release our partition table allocation. */
	kfree(pipe->part_table);
	pipe->part_table = NULL;
}

static int vsp1_video_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vsp1_video *video = vb2_get_drv_priv(vq);
	struct vsp1_pipeline *pipe = video->rwpf->entity.pipe;
	bool start_pipeline = false;
	unsigned long flags;
	int ret;

	mutex_lock(&pipe->lock);
	if (pipe->stream_count == pipe->num_inputs) {
		ret = vsp1_video_setup_pipeline(pipe);
		if (ret < 0) {
			vsp1_video_release_buffers(video);
			vsp1_video_cleanup_pipeline(pipe);
			mutex_unlock(&pipe->lock);
			return ret;
		}

		start_pipeline = true;
	}

	pipe->stream_count++;
	mutex_unlock(&pipe->lock);

	/*
	 * vsp1_pipeline_ready() is not sufficient to establish that all streams
	 * are prepared and the pipeline is configured, as multiple streams
	 * can race through streamon with buffers already queued; Therefore we
	 * don't even attempt to start the pipeline until the last stream has
	 * called through here.
	 */
	if (!start_pipeline)
		return 0;

	spin_lock_irqsave(&pipe->irqlock, flags);
	if (vsp1_pipeline_ready(pipe))
		vsp1_video_pipeline_run(pipe);
	spin_unlock_irqrestore(&pipe->irqlock, flags);

	return 0;
}

static void vsp1_video_stop_streaming(struct vb2_queue *vq)
{
	struct vsp1_video *video = vb2_get_drv_priv(vq);
	struct vsp1_pipeline *pipe = video->rwpf->entity.pipe;
	unsigned long flags;
	int ret;

	/*
	 * Clear the buffers ready flag to make sure the device won't be started
	 * by a QBUF on the video node on the other side of the pipeline.
	 */
	spin_lock_irqsave(&video->irqlock, flags);
	pipe->buffers_ready &= ~(1 << video->pipe_index);
	spin_unlock_irqrestore(&video->irqlock, flags);

	mutex_lock(&pipe->lock);
	if (--pipe->stream_count == pipe->num_inputs) {
		/* Stop the pipeline. */
		ret = vsp1_pipeline_stop(pipe);
		if (ret == -ETIMEDOUT)
			dev_err(video->vsp1->dev, "pipeline stop timeout\n");

		vsp1_video_cleanup_pipeline(pipe);
	}
	mutex_unlock(&pipe->lock);

	media_pipeline_stop(&video->video.entity);
	vsp1_video_release_buffers(video);
	vsp1_video_pipeline_put(pipe);
}

static const struct vb2_ops vsp1_video_queue_qops = {
	.queue_setup = vsp1_video_queue_setup,
	.buf_prepare = vsp1_video_buffer_prepare,
	.buf_queue = vsp1_video_buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = vsp1_video_start_streaming,
	.stop_streaming = vsp1_video_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int
vsp1_video_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_video *video = to_vsp1_video(vfh->vdev);

	cap->capabilities = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_STREAMING
			  | V4L2_CAP_VIDEO_CAPTURE_MPLANE
			  | V4L2_CAP_VIDEO_OUTPUT_MPLANE;


	strscpy(cap->driver, "vsp1", sizeof(cap->driver));
	strscpy(cap->card, video->video.name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(video->vsp1->dev));

	return 0;
}

static int
vsp1_video_get_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_video *video = to_vsp1_video(vfh->vdev);

	if (format->type != video->queue.type)
		return -EINVAL;

	mutex_lock(&video->lock);
	format->fmt.pix_mp = video->rwpf->format;
	mutex_unlock(&video->lock);

	return 0;
}

static int
vsp1_video_try_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_video *video = to_vsp1_video(vfh->vdev);

	if (format->type != video->queue.type)
		return -EINVAL;

	return __vsp1_video_try_format(video, &format->fmt.pix_mp, NULL);
}

static int
vsp1_video_set_format(struct file *file, void *fh, struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_video *video = to_vsp1_video(vfh->vdev);
	const struct vsp1_format_info *info;
	int ret;

	if (format->type != video->queue.type)
		return -EINVAL;

	ret = __vsp1_video_try_format(video, &format->fmt.pix_mp, &info);
	if (ret < 0)
		return ret;

	mutex_lock(&video->lock);

	if (vb2_is_busy(&video->queue)) {
		ret = -EBUSY;
		goto done;
	}

	video->rwpf->format = format->fmt.pix_mp;
	video->rwpf->fmtinfo = info;

done:
	mutex_unlock(&video->lock);
	return ret;
}

static int
vsp1_video_streamon(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_video *video = to_vsp1_video(vfh->vdev);
	struct media_device *mdev = &video->vsp1->media_dev;
	struct vsp1_pipeline *pipe;
	int ret;

	if (video->queue.owner && video->queue.owner != file->private_data)
		return -EBUSY;

	/*
	 * Get a pipeline for the video node and start streaming on it. No link
	 * touching an entity in the pipeline can be activated or deactivated
	 * once streaming is started.
	 */
	mutex_lock(&mdev->graph_mutex);

	pipe = vsp1_video_pipeline_get(video);
	if (IS_ERR(pipe)) {
		mutex_unlock(&mdev->graph_mutex);
		return PTR_ERR(pipe);
	}

	ret = __media_pipeline_start(&video->video.entity, &pipe->pipe);
	if (ret < 0) {
		mutex_unlock(&mdev->graph_mutex);
		goto err_pipe;
	}

	mutex_unlock(&mdev->graph_mutex);

	/*
	 * Verify that the configured format matches the output of the connected
	 * subdev.
	 */
	ret = vsp1_video_verify_format(video);
	if (ret < 0)
		goto err_stop;

	/* Start the queue. */
	ret = vb2_streamon(&video->queue, type);
	if (ret < 0)
		goto err_stop;

	return 0;

err_stop:
	media_pipeline_stop(&video->video.entity);
err_pipe:
	vsp1_video_pipeline_put(pipe);
	return ret;
}

static const struct v4l2_ioctl_ops vsp1_video_ioctl_ops = {
	.vidioc_querycap		= vsp1_video_querycap,
	.vidioc_g_fmt_vid_cap_mplane	= vsp1_video_get_format,
	.vidioc_s_fmt_vid_cap_mplane	= vsp1_video_set_format,
	.vidioc_try_fmt_vid_cap_mplane	= vsp1_video_try_format,
	.vidioc_g_fmt_vid_out_mplane	= vsp1_video_get_format,
	.vidioc_s_fmt_vid_out_mplane	= vsp1_video_set_format,
	.vidioc_try_fmt_vid_out_mplane	= vsp1_video_try_format,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vsp1_video_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * V4L2 File Operations
 */

static int vsp1_video_open(struct file *file)
{
	struct vsp1_video *video = video_drvdata(file);
	struct v4l2_fh *vfh;
	int ret = 0;

	vfh = kzalloc(sizeof(*vfh), GFP_KERNEL);
	if (vfh == NULL)
		return -ENOMEM;

	v4l2_fh_init(vfh, &video->video);
	v4l2_fh_add(vfh);

	file->private_data = vfh;

	ret = vsp1_device_get(video->vsp1);
	if (ret < 0) {
		v4l2_fh_del(vfh);
		v4l2_fh_exit(vfh);
		kfree(vfh);
	}

	return ret;
}

static int vsp1_video_release(struct file *file)
{
	struct vsp1_video *video = video_drvdata(file);
	struct v4l2_fh *vfh = file->private_data;

	mutex_lock(&video->lock);
	if (video->queue.owner == vfh) {
		vb2_queue_release(&video->queue);
		video->queue.owner = NULL;
	}
	mutex_unlock(&video->lock);

	vsp1_device_put(video->vsp1);

	v4l2_fh_release(file);

	file->private_data = NULL;

	return 0;
}

static const struct v4l2_file_operations vsp1_video_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = vsp1_video_open,
	.release = vsp1_video_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

/* -----------------------------------------------------------------------------
 * Suspend and Resume
 */

void vsp1_video_suspend(struct vsp1_device *vsp1)
{
	unsigned long flags;
	unsigned int i;
	int ret;

	/*
	 * To avoid increasing the system suspend time needlessly, loop over the
	 * pipelines twice, first to set them all to the stopping state, and
	 * then to wait for the stop to complete.
	 */
	for (i = 0; i < vsp1->info->wpf_count; ++i) {
		struct vsp1_rwpf *wpf = vsp1->wpf[i];
		struct vsp1_pipeline *pipe;

		if (wpf == NULL)
			continue;

		pipe = wpf->entity.pipe;
		if (pipe == NULL)
			continue;

		spin_lock_irqsave(&pipe->irqlock, flags);
		if (pipe->state == VSP1_PIPELINE_RUNNING)
			pipe->state = VSP1_PIPELINE_STOPPING;
		spin_unlock_irqrestore(&pipe->irqlock, flags);
	}

	for (i = 0; i < vsp1->info->wpf_count; ++i) {
		struct vsp1_rwpf *wpf = vsp1->wpf[i];
		struct vsp1_pipeline *pipe;

		if (wpf == NULL)
			continue;

		pipe = wpf->entity.pipe;
		if (pipe == NULL)
			continue;

		ret = wait_event_timeout(pipe->wq, vsp1_pipeline_stopped(pipe),
					 msecs_to_jiffies(500));
		if (ret == 0)
			dev_warn(vsp1->dev, "pipeline %u stop timeout\n",
				 wpf->entity.index);
	}
}

void vsp1_video_resume(struct vsp1_device *vsp1)
{
	unsigned long flags;
	unsigned int i;

	/* Resume all running pipelines. */
	for (i = 0; i < vsp1->info->wpf_count; ++i) {
		struct vsp1_rwpf *wpf = vsp1->wpf[i];
		struct vsp1_pipeline *pipe;

		if (wpf == NULL)
			continue;

		pipe = wpf->entity.pipe;
		if (pipe == NULL)
			continue;

		/*
		 * The hardware may have been reset during a suspend and will
		 * need a full reconfiguration.
		 */
		pipe->configured = false;

		spin_lock_irqsave(&pipe->irqlock, flags);
		if (vsp1_pipeline_ready(pipe))
			vsp1_video_pipeline_run(pipe);
		spin_unlock_irqrestore(&pipe->irqlock, flags);
	}
}

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_video *vsp1_video_create(struct vsp1_device *vsp1,
				     struct vsp1_rwpf *rwpf)
{
	struct vsp1_video *video;
	const char *direction;
	int ret;

	video = devm_kzalloc(vsp1->dev, sizeof(*video), GFP_KERNEL);
	if (!video)
		return ERR_PTR(-ENOMEM);

	rwpf->video = video;

	video->vsp1 = vsp1;
	video->rwpf = rwpf;

	if (rwpf->entity.type == VSP1_ENTITY_RPF) {
		direction = "input";
		video->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		video->pad.flags = MEDIA_PAD_FL_SOURCE;
		video->video.vfl_dir = VFL_DIR_TX;
		video->video.device_caps = V4L2_CAP_VIDEO_OUTPUT_MPLANE |
					   V4L2_CAP_STREAMING;
	} else {
		direction = "output";
		video->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		video->pad.flags = MEDIA_PAD_FL_SINK;
		video->video.vfl_dir = VFL_DIR_RX;
		video->video.device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
					   V4L2_CAP_STREAMING;
	}

	mutex_init(&video->lock);
	spin_lock_init(&video->irqlock);
	INIT_LIST_HEAD(&video->irqqueue);

	/* Initialize the media entity... */
	ret = media_entity_pads_init(&video->video.entity, 1, &video->pad);
	if (ret < 0)
		return ERR_PTR(ret);

	/* ... and the format ... */
	rwpf->format.pixelformat = VSP1_VIDEO_DEF_FORMAT;
	rwpf->format.width = VSP1_VIDEO_DEF_WIDTH;
	rwpf->format.height = VSP1_VIDEO_DEF_HEIGHT;
	__vsp1_video_try_format(video, &rwpf->format, &rwpf->fmtinfo);

	/* ... and the video node... */
	video->video.v4l2_dev = &video->vsp1->v4l2_dev;
	video->video.fops = &vsp1_video_fops;
	snprintf(video->video.name, sizeof(video->video.name), "%s %s",
		 rwpf->entity.subdev.name, direction);
	video->video.vfl_type = VFL_TYPE_GRABBER;
	video->video.release = video_device_release_empty;
	video->video.ioctl_ops = &vsp1_video_ioctl_ops;

	video_set_drvdata(&video->video, video);

	video->queue.type = video->type;
	video->queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	video->queue.lock = &video->lock;
	video->queue.drv_priv = video;
	video->queue.buf_struct_size = sizeof(struct vsp1_vb2_buffer);
	video->queue.ops = &vsp1_video_queue_qops;
	video->queue.mem_ops = &vb2_dma_contig_memops;
	video->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	video->queue.dev = video->vsp1->bus_master;
	ret = vb2_queue_init(&video->queue);
	if (ret < 0) {
		dev_err(video->vsp1->dev, "failed to initialize vb2 queue\n");
		goto error;
	}

	/* ... and register the video device. */
	video->video.queue = &video->queue;
	ret = video_register_device(&video->video, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(video->vsp1->dev, "failed to register video device\n");
		goto error;
	}

	return video;

error:
	vsp1_video_cleanup(video);
	return ERR_PTR(ret);
}

void vsp1_video_cleanup(struct vsp1_video *video)
{
	if (video_is_registered(&video->video))
		video_unregister_device(&video->video);

	media_entity_cleanup(&video->video.entity);
}
