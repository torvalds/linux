// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_vspx.c  --  R-Car Gen 4 VSPX
 *
 * Copyright (C) 2025 Ideas On Board Oy
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include "vsp1_vspx.h"

#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>
#include <media/vsp1.h>

#include "vsp1_dl.h"
#include "vsp1_iif.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"

/*
 * struct vsp1_vspx_pipeline - VSPX pipeline
 * @pipe: the VSP1 pipeline
 * @partition: the pre-calculated partition used by the pipeline
 * @mutex: protects the streaming start/stop sequences
 * @lock: protect access to the enabled flag
 * @enabled: the enable flag
 * @vspx_frame_end: frame end callback
 * @frame_end_data: data for the frame end callback
 */
struct vsp1_vspx_pipeline {
	struct vsp1_pipeline pipe;
	struct vsp1_partition partition;

	/*
	 * Protects the streaming start/stop sequences.
	 *
	 * The start/stop sequences cannot be locked with the 'lock' spinlock
	 * as they acquire mutexes when handling the pm runtime and the vsp1
	 * pipe start/stop operations. Provide a dedicated mutex for this
	 * reason.
	 */
	struct mutex mutex;

	/*
	 * Protects the enable flag.
	 *
	 * The enabled flag is contended between the start/stop streaming
	 * routines and the job_run one, which cannot take a mutex as it is
	 * called from the ISP irq context.
	 */
	spinlock_t lock;
	bool enabled;

	void (*vspx_frame_end)(void *frame_end_data);
	void *frame_end_data;
};

static inline struct vsp1_vspx_pipeline *
to_vsp1_vspx_pipeline(struct vsp1_pipeline *pipe)
{
	return container_of(pipe, struct vsp1_vspx_pipeline, pipe);
}

/*
 * struct vsp1_vspx - VSPX device
 * @vsp1: the VSP1 device
 * @pipe: the VSPX pipeline
 */
struct vsp1_vspx {
	struct vsp1_device *vsp1;
	struct vsp1_vspx_pipeline pipe;
};

/* Apply the given width, height and fourcc to the RWPF's subdevice */
static int vsp1_vspx_rwpf_set_subdev_fmt(struct vsp1_device *vsp1,
					 struct vsp1_rwpf *rwpf,
					 u32 isp_fourcc,
					 unsigned int width,
					 unsigned int height)
{
	struct vsp1_entity *ent = &rwpf->entity;
	struct v4l2_subdev_format format = {};
	u32 vspx_fourcc;

	switch (isp_fourcc) {
	case V4L2_PIX_FMT_GREY:
		/* 8 bit RAW Bayer image. */
		vspx_fourcc = V4L2_PIX_FMT_RGB332;
		break;
	case V4L2_PIX_FMT_Y10:
	case V4L2_PIX_FMT_Y12:
	case V4L2_PIX_FMT_Y16:
		/* 10, 12 and 16 bit RAW Bayer image. */
		vspx_fourcc = V4L2_PIX_FMT_RGB565;
		break;
	case V4L2_META_FMT_GENERIC_8:
		/* ConfigDMA parameters buffer. */
		vspx_fourcc = V4L2_PIX_FMT_XBGR32;
		break;
	default:
		return -EINVAL;
	}

	rwpf->fmtinfo = vsp1_get_format_info(vsp1, vspx_fourcc);

	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	format.pad = RWPF_PAD_SINK;
	format.format.width = width;
	format.format.height = height;
	format.format.field = V4L2_FIELD_NONE;
	format.format.code = rwpf->fmtinfo->mbus;

	return v4l2_subdev_call(&ent->subdev, pad, set_fmt, NULL, &format);
}

/* Configure the RPF->IIF->WPF pipeline for ConfigDMA or RAW image transfer. */
static int vsp1_vspx_pipeline_configure(struct vsp1_device *vsp1,
					dma_addr_t addr, u32 isp_fourcc,
					unsigned int width, unsigned int height,
					unsigned int stride,
					unsigned int iif_sink_pad,
					struct vsp1_dl_list *dl,
					struct vsp1_dl_body *dlb)
{
	struct vsp1_vspx_pipeline *vspx_pipe = &vsp1->vspx->pipe;
	struct vsp1_pipeline *pipe = &vspx_pipe->pipe;
	struct vsp1_rwpf *rpf0 = pipe->inputs[0];
	int ret;

	ret = vsp1_vspx_rwpf_set_subdev_fmt(vsp1, rpf0, isp_fourcc, width,
					    height);
	if (ret)
		return ret;

	ret = vsp1_vspx_rwpf_set_subdev_fmt(vsp1, pipe->output, isp_fourcc,
					    width, height);
	if (ret)
		return ret;

	vsp1_pipeline_calculate_partition(pipe, &pipe->part_table[0], width, 0);
	rpf0->format.plane_fmt[0].bytesperline = stride;
	rpf0->format.num_planes = 1;
	rpf0->mem.addr[0] = addr;

	/*
	 * Connect RPF0 to the IIF sink pad corresponding to the config or image
	 * path.
	 */
	rpf0->entity.sink_pad = iif_sink_pad;

	vsp1_entity_route_setup(&rpf0->entity, pipe, dlb);
	vsp1_entity_configure_stream(&rpf0->entity, rpf0->entity.state, pipe,
				     dl, dlb);
	vsp1_entity_configure_partition(&rpf0->entity, pipe,
					&pipe->part_table[0], dl, dlb);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Interrupt handling
 */

static void vsp1_vspx_pipeline_frame_end(struct vsp1_pipeline *pipe,
					 unsigned int completion)
{
	struct vsp1_vspx_pipeline *vspx_pipe = to_vsp1_vspx_pipeline(pipe);

	scoped_guard(spinlock_irqsave, &pipe->irqlock) {
		/*
		 * Operating the vsp1_pipe in singleshot mode requires to
		 * manually set the pipeline state to stopped when a transfer
		 * is completed.
		 */
		pipe->state = VSP1_PIPELINE_STOPPED;
	}

	if (vspx_pipe->vspx_frame_end)
		vspx_pipe->vspx_frame_end(vspx_pipe->frame_end_data);
}

/* -----------------------------------------------------------------------------
 * ISP Driver API (include/media/vsp1.h)
 */

/**
 * vsp1_isp_init() - Initialize the VSPX
 * @dev: The VSP1 struct device
 *
 * Return: %0 on success or a negative error code on failure
 */
int vsp1_isp_init(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	if (!vsp1)
		return -EPROBE_DEFER;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_isp_init);

/**
 * vsp1_isp_get_bus_master - Get VSPX bus master
 * @dev: The VSP1 struct device
 *
 * The VSPX accesses memory through an FCPX instance. When allocating memory
 * buffers that will have to be accessed by the VSPX the 'struct device' of
 * the FCPX should be used. Use this function to get a reference to it.
 *
 * Return: a pointer to the bus master's device
 */
struct device *vsp1_isp_get_bus_master(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	if (!vsp1)
		return ERR_PTR(-ENODEV);

	return vsp1->bus_master;
}
EXPORT_SYMBOL_GPL(vsp1_isp_get_bus_master);

/**
 * vsp1_isp_alloc_buffer - Allocate a buffer in the VSPX address space
 * @dev: The VSP1 struct device
 * @size: The size of the buffer to be allocated by the VSPX
 * @buffer_desc: The buffer descriptor. Will be filled with the buffer
 *		 CPU-mapped address, the bus address and the size of the
 *		 allocated buffer
 *
 * Allocate a buffer that will be later accessed by the VSPX. Buffers allocated
 * using vsp1_isp_alloc_buffer() shall be released with a call to
 * vsp1_isp_free_buffer(). This function is used by the ISP driver to allocate
 * memory for the ConfigDMA parameters buffer.
 *
 * Return: %0 on success or a negative error code on failure
 */
int vsp1_isp_alloc_buffer(struct device *dev, size_t size,
			  struct vsp1_isp_buffer_desc *buffer_desc)
{
	struct device *bus_master = vsp1_isp_get_bus_master(dev);

	if (IS_ERR_OR_NULL(bus_master))
		return -ENODEV;

	buffer_desc->cpu_addr = dma_alloc_coherent(bus_master, size,
						   &buffer_desc->dma_addr,
						   GFP_KERNEL);
	if (!buffer_desc->cpu_addr)
		return -ENOMEM;

	buffer_desc->size = size;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_isp_alloc_buffer);

/**
 * vsp1_isp_free_buffer - Release a buffer allocated by vsp1_isp_alloc_buffer()
 * @dev: The VSP1 struct device
 * @buffer_desc: The descriptor of the buffer to release as returned by
 *		 vsp1_isp_alloc_buffer()
 *
 * Release memory in the VSPX address space allocated by
 * vsp1_isp_alloc_buffer().
 */
void vsp1_isp_free_buffer(struct device *dev,
			  struct vsp1_isp_buffer_desc *buffer_desc)
{
	struct device *bus_master = vsp1_isp_get_bus_master(dev);

	if (IS_ERR_OR_NULL(bus_master))
		return;

	dma_free_coherent(bus_master, buffer_desc->size, buffer_desc->cpu_addr,
			  buffer_desc->dma_addr);
}
EXPORT_SYMBOL_GPL(vsp1_isp_free_buffer);

/**
 * vsp1_isp_start_streaming - Start processing VSPX jobs
 * @dev: The VSP1 struct device
 * @frame_end: The frame end callback description
 *
 * Start the VSPX and prepare for accepting buffer transfer job requests.
 * The caller is responsible for tracking the started state of the VSPX.
 * Attempting to start an already started VSPX instance is an error.
 *
 * Return: %0 on success or a negative error code on failure
 */
int vsp1_isp_start_streaming(struct device *dev,
			     struct vsp1_vspx_frame_end *frame_end)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_vspx_pipeline *vspx_pipe = &vsp1->vspx->pipe;
	struct vsp1_pipeline *pipe = &vspx_pipe->pipe;
	u32 value;
	int ret;

	if (!frame_end)
		return -EINVAL;

	guard(mutex)(&vspx_pipe->mutex);

	scoped_guard(spinlock_irq, &vspx_pipe->lock) {
		if (vspx_pipe->enabled)
			return -EBUSY;
	}

	vspx_pipe->vspx_frame_end = frame_end->vspx_frame_end;
	vspx_pipe->frame_end_data = frame_end->frame_end_data;

	/* Enable the VSP1 and prepare for streaming. */
	vsp1_pipeline_dump(pipe, "VSPX job");

	ret = vsp1_device_get(vsp1);
	if (ret < 0)
		return ret;

	/*
	 * Make sure VSPX is not active. This should never happen in normal
	 * usage
	 */
	value = vsp1_read(vsp1, VI6_CMD(0));
	if (value & VI6_CMD_STRCMD) {
		dev_err(vsp1->dev,
			"%s: Starting of WPF0 already reserved\n", __func__);
		ret = -EBUSY;
		goto error_put;
	}

	value = vsp1_read(vsp1, VI6_STATUS);
	if (value & VI6_STATUS_SYS_ACT(0)) {
		dev_err(vsp1->dev,
			"%s: WPF0 has not entered idle state\n", __func__);
		ret = -EBUSY;
		goto error_put;
	}

	scoped_guard(spinlock_irq, &vspx_pipe->lock) {
		vspx_pipe->enabled = true;
	}

	return 0;

error_put:
	vsp1_device_put(vsp1);
	return ret;
}
EXPORT_SYMBOL_GPL(vsp1_isp_start_streaming);

/**
 * vsp1_isp_stop_streaming - Stop the VSPX
 * @dev: The VSP1 struct device
 *
 * Stop the VSPX operation by stopping the vsp1 pipeline and waiting for the
 * last frame in transfer, if any, to complete.
 *
 * The caller is responsible for tracking the stopped state of the VSPX.
 * Attempting to stop an already stopped VSPX instance is a nop.
 */
void vsp1_isp_stop_streaming(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_vspx_pipeline *vspx_pipe = &vsp1->vspx->pipe;
	struct vsp1_pipeline *pipe = &vspx_pipe->pipe;

	guard(mutex)(&vspx_pipe->mutex);

	scoped_guard(spinlock_irq, &vspx_pipe->lock) {
		if (!vspx_pipe->enabled)
			return;

		vspx_pipe->enabled = false;
	}

	WARN_ON_ONCE(vsp1_pipeline_stop(pipe));

	vspx_pipe->vspx_frame_end = NULL;
	vsp1_dlm_reset(pipe->output->dlm);
	vsp1_device_put(vsp1);
}
EXPORT_SYMBOL_GPL(vsp1_isp_stop_streaming);

/**
 * vsp1_isp_job_prepare - Prepare a new buffer transfer job
 * @dev: The VSP1 struct device
 * @job: The job description
 *
 * Prepare a new buffer transfer job by populating a display list that will be
 * later executed by a call to vsp1_isp_job_run(). All pending jobs must be
 * released after stopping the streaming operations with a call to
 * vsp1_isp_job_release().
 *
 * In order for the VSPX to accept new jobs to prepare the VSPX must have been
 * started.
 *
 * Return: %0 on success or a negative error code on failure
 */
int vsp1_isp_job_prepare(struct device *dev, struct vsp1_isp_job_desc *job)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_vspx_pipeline *vspx_pipe = &vsp1->vspx->pipe;
	struct vsp1_pipeline *pipe = &vspx_pipe->pipe;
	const struct v4l2_pix_format_mplane *pix_mp;
	struct vsp1_dl_list *second_dl = NULL;
	struct vsp1_dl_body *dlb;
	struct vsp1_dl_list *dl;
	int ret;

	/*
	 * Transfer the buffers described in the job: an optional ConfigDMA
	 * parameters buffer and a RAW image.
	 */

	job->dl = vsp1_dl_list_get(pipe->output->dlm);
	if (!job->dl)
		return -ENOMEM;

	dl = job->dl;
	dlb = vsp1_dl_list_get_body0(dl);

	/* Configure IIF routing and enable IIF function. */
	vsp1_entity_route_setup(pipe->iif, pipe, dlb);
	vsp1_entity_configure_stream(pipe->iif, pipe->iif->state, pipe,
				     dl, dlb);

	/* Configure WPF0 to enable RPF0 as source. */
	vsp1_entity_route_setup(&pipe->output->entity, pipe, dlb);
	vsp1_entity_configure_stream(&pipe->output->entity,
				     pipe->output->entity.state, pipe,
				     dl, dlb);

	if (job->config.pairs) {
		/*
		 * Writing less than 17 pairs corrupts the output images ( < 16
		 * pairs) or freezes the VSPX operations (= 16 pairs). Only
		 * allow more than 16 pairs to be written.
		 */
		if (job->config.pairs <= 16) {
			ret = -EINVAL;
			goto error_put_dl;
		}

		/*
		 * Configure RPF0 for ConfigDMA data. Transfer the number of
		 * configuration pairs plus 2 words for the header.
		 */
		ret = vsp1_vspx_pipeline_configure(vsp1, job->config.mem,
						   V4L2_META_FMT_GENERIC_8,
						   job->config.pairs * 2 + 2, 1,
						   job->config.pairs * 2 + 2,
						   VSPX_IIF_SINK_PAD_CONFIG,
						   dl, dlb);
		if (ret)
			goto error_put_dl;

		second_dl = vsp1_dl_list_get(pipe->output->dlm);
		if (!second_dl) {
			ret = -ENOMEM;
			goto error_put_dl;
		}

		dl = second_dl;
		dlb = vsp1_dl_list_get_body0(dl);
	}

	/* Configure RPF0 for RAW image transfer. */
	pix_mp = &job->img.fmt;
	ret = vsp1_vspx_pipeline_configure(vsp1, job->img.mem,
					   pix_mp->pixelformat,
					   pix_mp->width, pix_mp->height,
					   pix_mp->plane_fmt[0].bytesperline,
					   VSPX_IIF_SINK_PAD_IMG, dl, dlb);
	if (ret)
		goto error_put_dl;

	if (second_dl)
		vsp1_dl_list_add_chain(job->dl, second_dl);

	return 0;

error_put_dl:
	if (second_dl)
		vsp1_dl_list_put(second_dl);
	vsp1_dl_list_put(job->dl);
	job->dl = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(vsp1_isp_job_prepare);

/**
 * vsp1_isp_job_run - Run a buffer transfer job
 * @dev: The VSP1 struct device
 * @job: The job to be run
 *
 * Run the display list contained in the job description provided by the caller.
 * The job must have been prepared with a call to vsp1_isp_job_prepare() and
 * the job's display list shall be valid.
 *
 * Jobs can be run only on VSPX instances which have been started. Requests
 * to run a job after the VSPX has been stopped return -EINVAL and the job
 * resources shall be released by the caller with vsp1_isp_job_release().
 * When a job is run successfully all the resources acquired by
 * vsp1_isp_job_prepare() are released by this function and no further action
 * is required to the caller.
 *
 * Return: %0 on success or a negative error code on failure
 */
int vsp1_isp_job_run(struct device *dev, struct vsp1_isp_job_desc *job)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_vspx_pipeline *vspx_pipe = &vsp1->vspx->pipe;
	struct vsp1_pipeline *pipe = &vspx_pipe->pipe;
	u32 value;

	/* Make sure VSPX is not busy processing a frame. */
	value = vsp1_read(vsp1, VI6_CMD(0));
	if (value) {
		dev_err(vsp1->dev,
			"%s: Starting of WPF0 already reserved\n", __func__);
		return -EBUSY;
	}

	scoped_guard(spinlock_irqsave, &vspx_pipe->lock) {
		/*
		 * If a new job is scheduled when the VSPX is stopped, do not
		 * run it.
		 */
		if (!vspx_pipe->enabled)
			return -EINVAL;

		vsp1_dl_list_commit(job->dl, 0);

		/*
		 * The display list is now under control of the display list
		 * manager and will be released automatically when the job
		 * completes.
		 */
		job->dl = NULL;
	}

	scoped_guard(spinlock_irqsave, &pipe->irqlock) {
		vsp1_pipeline_run(pipe);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_isp_job_run);

/**
 * vsp1_isp_job_release - Release a non processed transfer job
 * @dev: The VSP1 struct device
 * @job: The job to release
 *
 * Release a job prepared by a call to vsp1_isp_job_prepare() and not yet
 * run. All pending jobs shall be released after streaming has been stopped.
 */
void vsp1_isp_job_release(struct device *dev,
			  struct vsp1_isp_job_desc *job)
{
	vsp1_dl_list_put(job->dl);
}
EXPORT_SYMBOL_GPL(vsp1_isp_job_release);

/* -----------------------------------------------------------------------------
 * Initialization and cleanup
 */

int vsp1_vspx_init(struct vsp1_device *vsp1)
{
	struct vsp1_vspx_pipeline *vspx_pipe;
	struct vsp1_pipeline *pipe;

	vsp1->vspx = devm_kzalloc(vsp1->dev, sizeof(*vsp1->vspx), GFP_KERNEL);
	if (!vsp1->vspx)
		return -ENOMEM;

	vsp1->vspx->vsp1 = vsp1;

	vspx_pipe = &vsp1->vspx->pipe;
	vspx_pipe->enabled = false;

	pipe = &vspx_pipe->pipe;

	vsp1_pipeline_init(pipe);

	pipe->partitions = 1;
	pipe->part_table = &vspx_pipe->partition;
	pipe->interlaced = false;
	pipe->frame_end = vsp1_vspx_pipeline_frame_end;

	mutex_init(&vspx_pipe->mutex);
	spin_lock_init(&vspx_pipe->lock);

	/*
	 * Initialize RPF0 as input for VSPX and use it unconditionally for
	 * now.
	 */
	pipe->inputs[0] = vsp1->rpf[0];
	pipe->inputs[0]->entity.pipe = pipe;
	pipe->inputs[0]->entity.sink = &vsp1->iif->entity;
	list_add_tail(&pipe->inputs[0]->entity.list_pipe, &pipe->entities);

	pipe->iif = &vsp1->iif->entity;
	pipe->iif->pipe = pipe;
	pipe->iif->sink = &vsp1->wpf[0]->entity;
	pipe->iif->sink_pad = RWPF_PAD_SINK;
	list_add_tail(&pipe->iif->list_pipe, &pipe->entities);

	pipe->output = vsp1->wpf[0];
	pipe->output->entity.pipe = pipe;
	list_add_tail(&pipe->output->entity.list_pipe, &pipe->entities);

	return 0;
}

void vsp1_vspx_cleanup(struct vsp1_device *vsp1)
{
	struct vsp1_vspx_pipeline *vspx_pipe = &vsp1->vspx->pipe;

	mutex_destroy(&vspx_pipe->mutex);
}
