// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */

#include <linux/module.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ioctl.h>
#include <media/videobuf-vmalloc.h>

#include "atomisp_cmd.h"
#include "atomisp_common.h"
#include "atomisp_fops.h"
#include "atomisp_internal.h"
#include "atomisp_ioctl.h"
#include "atomisp_compat.h"
#include "atomisp_subdev.h"
#include "atomisp_v4l2.h"
#include "atomisp-regs.h"
#include "hmm/hmm.h"

#include "type_support.h"
#include "device_access/device_access.h"

#include "atomisp_acc.h"

#define ISP_LEFT_PAD			128	/* equal to 2*NWAY */

/*
 * input image data, and current frame resolution for test
 */
#define	ISP_PARAM_MMAP_OFFSET	0xfffff000

#define MAGIC_CHECK(is, should)	\
	do { \
		if (unlikely((is) != (should))) { \
			pr_err("magic mismatch: %x (expected %x)\n", \
				is, should); \
			BUG(); \
		} \
	} while (0)

/*
 * Videobuf ops
 */
static int atomisp_buf_setup(struct videobuf_queue *vq, unsigned int *count,
			     unsigned int *size)
{
	struct atomisp_video_pipe *pipe = vq->priv_data;

	*size = pipe->pix.sizeimage;

	return 0;
}

static int atomisp_buf_prepare(struct videobuf_queue *vq,
			       struct videobuf_buffer *vb,
			       enum v4l2_field field)
{
	struct atomisp_video_pipe *pipe = vq->priv_data;

	vb->size = pipe->pix.sizeimage;
	vb->width = pipe->pix.width;
	vb->height = pipe->pix.height;
	vb->field = field;
	vb->state = VIDEOBUF_PREPARED;

	return 0;
}

static int atomisp_q_one_metadata_buffer(struct atomisp_sub_device *asd,
	enum atomisp_input_stream_id stream_id,
	enum ia_css_pipe_id css_pipe_id)
{
	struct atomisp_metadata_buf *metadata_buf;
	enum atomisp_metadata_type md_type =
	    atomisp_get_metadata_type(asd, css_pipe_id);
	struct list_head *metadata_list;

	if (asd->metadata_bufs_in_css[stream_id][css_pipe_id] >=
	    ATOMISP_CSS_Q_DEPTH)
		return 0; /* we have reached CSS queue depth */

	if (!list_empty(&asd->metadata[md_type])) {
		metadata_list = &asd->metadata[md_type];
	} else if (!list_empty(&asd->metadata_ready[md_type])) {
		metadata_list = &asd->metadata_ready[md_type];
	} else {
		dev_warn(asd->isp->dev, "%s: No metadata buffers available for type %d!\n",
			 __func__, md_type);
		return -EINVAL;
	}

	metadata_buf = list_entry(metadata_list->next,
				  struct atomisp_metadata_buf, list);
	list_del_init(&metadata_buf->list);

	if (atomisp_q_metadata_buffer_to_css(asd, metadata_buf,
					     stream_id, css_pipe_id)) {
		list_add(&metadata_buf->list, metadata_list);
		return -EINVAL;
	} else {
		list_add_tail(&metadata_buf->list,
			      &asd->metadata_in_css[md_type]);
	}
	asd->metadata_bufs_in_css[stream_id][css_pipe_id]++;

	return 0;
}

static int atomisp_q_one_s3a_buffer(struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    enum ia_css_pipe_id css_pipe_id)
{
	struct atomisp_s3a_buf *s3a_buf;
	struct list_head *s3a_list;
	unsigned int exp_id;

	if (asd->s3a_bufs_in_css[css_pipe_id] >= ATOMISP_CSS_Q_DEPTH)
		return 0; /* we have reached CSS queue depth */

	if (!list_empty(&asd->s3a_stats)) {
		s3a_list = &asd->s3a_stats;
	} else if (!list_empty(&asd->s3a_stats_ready)) {
		s3a_list = &asd->s3a_stats_ready;
	} else {
		dev_warn(asd->isp->dev, "%s: No s3a buffers available!\n",
			 __func__);
		return -EINVAL;
	}

	s3a_buf = list_entry(s3a_list->next, struct atomisp_s3a_buf, list);
	list_del_init(&s3a_buf->list);
	exp_id = s3a_buf->s3a_data->exp_id;

	hmm_flush_vmap(s3a_buf->s3a_data->data_ptr);
	if (atomisp_q_s3a_buffer_to_css(asd, s3a_buf,
					stream_id, css_pipe_id)) {
		/* got from head, so return back to the head */
		list_add(&s3a_buf->list, s3a_list);
		return -EINVAL;
	} else {
		list_add_tail(&s3a_buf->list, &asd->s3a_stats_in_css);
		if (s3a_list == &asd->s3a_stats_ready)
			dev_warn(asd->isp->dev, "%s: drop one s3a stat which has exp_id %d!\n",
				 __func__, exp_id);
	}

	asd->s3a_bufs_in_css[css_pipe_id]++;
	return 0;
}

static int atomisp_q_one_dis_buffer(struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    enum ia_css_pipe_id css_pipe_id)
{
	struct atomisp_dis_buf *dis_buf;
	unsigned long irqflags;

	if (asd->dis_bufs_in_css >=  ATOMISP_CSS_Q_DEPTH)
		return 0; /* we have reached CSS queue depth */

	spin_lock_irqsave(&asd->dis_stats_lock, irqflags);
	if (list_empty(&asd->dis_stats)) {
		spin_unlock_irqrestore(&asd->dis_stats_lock, irqflags);
		dev_warn(asd->isp->dev, "%s: No dis buffers available!\n",
			 __func__);
		return -EINVAL;
	}

	dis_buf = list_entry(asd->dis_stats.prev,
			     struct atomisp_dis_buf, list);
	list_del_init(&dis_buf->list);
	spin_unlock_irqrestore(&asd->dis_stats_lock, irqflags);

	hmm_flush_vmap(dis_buf->dis_data->data_ptr);
	if (atomisp_q_dis_buffer_to_css(asd, dis_buf,
					stream_id, css_pipe_id)) {
		spin_lock_irqsave(&asd->dis_stats_lock, irqflags);
		/* got from tail, so return back to the tail */
		list_add_tail(&dis_buf->list, &asd->dis_stats);
		spin_unlock_irqrestore(&asd->dis_stats_lock, irqflags);
		return -EINVAL;
	} else {
		spin_lock_irqsave(&asd->dis_stats_lock, irqflags);
		list_add_tail(&dis_buf->list, &asd->dis_stats_in_css);
		spin_unlock_irqrestore(&asd->dis_stats_lock, irqflags);
	}

	asd->dis_bufs_in_css++;

	return 0;
}

int atomisp_q_video_buffers_to_css(struct atomisp_sub_device *asd,
				   struct atomisp_video_pipe *pipe,
				   enum atomisp_input_stream_id stream_id,
				   enum ia_css_buffer_type css_buf_type,
				   enum ia_css_pipe_id css_pipe_id)
{
	struct videobuf_vmalloc_memory *vm_mem;
	struct atomisp_css_params_with_list *param;
	struct ia_css_dvs_grid_info *dvs_grid =
	    atomisp_css_get_dvs_grid_info(&asd->params.curr_grid_info);
	unsigned long irqflags;
	int err = 0;

	if (WARN_ON(css_pipe_id >= IA_CSS_PIPE_ID_NUM))
		return -EINVAL;

	while (pipe->buffers_in_css < ATOMISP_CSS_Q_DEPTH) {
		struct videobuf_buffer *vb;

		spin_lock_irqsave(&pipe->irq_lock, irqflags);
		if (list_empty(&pipe->activeq)) {
			spin_unlock_irqrestore(&pipe->irq_lock, irqflags);
			return -EINVAL;
		}
		vb = list_entry(pipe->activeq.next,
				struct videobuf_buffer, queue);
		list_del_init(&vb->queue);
		vb->state = VIDEOBUF_ACTIVE;
		spin_unlock_irqrestore(&pipe->irq_lock, irqflags);

		/*
		 * If there is a per_frame setting to apply on the buffer,
		 * do it before buffer en-queueing.
		 */
		vm_mem = vb->priv;

		param = pipe->frame_params[vb->i];
		if (param) {
			atomisp_makeup_css_parameters(asd,
						      &asd->params.css_param.update_flag,
						      &param->params);
			atomisp_apply_css_parameters(asd, &param->params);

			if (param->params.update_flag.dz_config &&
			    asd->run_mode->val != ATOMISP_RUN_MODE_VIDEO) {
				err = atomisp_calculate_real_zoom_region(asd,
					&param->params.dz_config, css_pipe_id);
				if (!err)
					asd->params.config.dz_config = &param->params.dz_config;
			}
			atomisp_css_set_isp_config_applied_frame(asd,
				vm_mem->vaddr);
			atomisp_css_update_isp_params_on_pipe(asd,
							      asd->stream_env[stream_id].pipes[css_pipe_id]);
			asd->params.dvs_6axis = (struct ia_css_dvs_6axis_config *)
						param->params.dvs_6axis;

			/*
			 * WORKAROUND:
			 * Because the camera halv3 can't ensure to set zoom
			 * region to per_frame setting and global setting at
			 * same time and only set zoom region to pre_frame
			 * setting now.so when the pre_frame setting include
			 * zoom region,I will set it to global setting.
			 */
			if (param->params.update_flag.dz_config &&
			    asd->run_mode->val != ATOMISP_RUN_MODE_VIDEO
			    && !err) {
				memcpy(&asd->params.css_param.dz_config,
				       &param->params.dz_config,
				       sizeof(struct ia_css_dz_config));
				asd->params.css_param.update_flag.dz_config =
				    (struct atomisp_dz_config *)
				    &asd->params.css_param.dz_config;
				asd->params.css_update_params_needed = true;
			}
		}
		/* Enqueue buffer */
		err = atomisp_q_video_buffer_to_css(asd, vm_mem, stream_id,
						    css_buf_type, css_pipe_id);
		if (err) {
			spin_lock_irqsave(&pipe->irq_lock, irqflags);
			list_add_tail(&vb->queue, &pipe->activeq);
			vb->state = VIDEOBUF_QUEUED;
			spin_unlock_irqrestore(&pipe->irq_lock, irqflags);
			dev_err(asd->isp->dev, "%s, css q fails: %d\n",
				__func__, err);
			return -EINVAL;
		}
		pipe->buffers_in_css++;

		/* enqueue 3A/DIS/metadata buffers */
		if (asd->params.curr_grid_info.s3a_grid.enable &&
		    css_pipe_id == asd->params.s3a_enabled_pipe &&
		    css_buf_type == IA_CSS_BUFFER_TYPE_OUTPUT_FRAME)
			atomisp_q_one_s3a_buffer(asd, stream_id,
						 css_pipe_id);

		if (asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream_info.
		    metadata_info.size &&
		    css_buf_type == IA_CSS_BUFFER_TYPE_OUTPUT_FRAME)
			atomisp_q_one_metadata_buffer(asd, stream_id,
						      css_pipe_id);

		if (dvs_grid && dvs_grid->enable &&
		    css_pipe_id == IA_CSS_PIPE_ID_VIDEO &&
		    css_buf_type == IA_CSS_BUFFER_TYPE_OUTPUT_FRAME)
			atomisp_q_one_dis_buffer(asd, stream_id,
						 css_pipe_id);
	}

	return 0;
}

static int atomisp_get_css_buf_type(struct atomisp_sub_device *asd,
				    enum ia_css_pipe_id pipe_id,
				    uint16_t source_pad)
{
	if (ATOMISP_USE_YUVPP(asd)) {
		/* when run ZSL case */
		if (asd->continuous_mode->val &&
		    asd->run_mode->val == ATOMISP_RUN_MODE_PREVIEW) {
			if (source_pad == ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE)
				return IA_CSS_BUFFER_TYPE_OUTPUT_FRAME;
			else if (source_pad == ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW)
				return IA_CSS_BUFFER_TYPE_SEC_OUTPUT_FRAME;
			else
				return IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME;
		}

		/*when run SDV case*/
		if (asd->continuous_mode->val &&
		    asd->run_mode->val == ATOMISP_RUN_MODE_VIDEO) {
			if (source_pad == ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE)
				return IA_CSS_BUFFER_TYPE_OUTPUT_FRAME;
			else if (source_pad == ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW)
				return IA_CSS_BUFFER_TYPE_SEC_VF_OUTPUT_FRAME;
			else if (source_pad == ATOMISP_SUBDEV_PAD_SOURCE_VIDEO)
				return IA_CSS_BUFFER_TYPE_SEC_OUTPUT_FRAME;
			else
				return IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME;
		}

		/*other case: default setting*/
		if (source_pad == ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE ||
		    source_pad == ATOMISP_SUBDEV_PAD_SOURCE_VIDEO ||
		    (source_pad == ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW &&
		     asd->run_mode->val != ATOMISP_RUN_MODE_VIDEO))
			return IA_CSS_BUFFER_TYPE_OUTPUT_FRAME;
		else
			return IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME;
	}

	if (pipe_id == IA_CSS_PIPE_ID_COPY ||
	    source_pad == ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE ||
	    source_pad == ATOMISP_SUBDEV_PAD_SOURCE_VIDEO ||
	    (source_pad == ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW &&
	     asd->run_mode->val != ATOMISP_RUN_MODE_VIDEO))
		return IA_CSS_BUFFER_TYPE_OUTPUT_FRAME;
	else
		return IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME;
}

static int atomisp_qbuffers_to_css_for_all_pipes(struct atomisp_sub_device *asd)
{
	enum ia_css_buffer_type buf_type;
	enum ia_css_pipe_id css_capture_pipe_id = IA_CSS_PIPE_ID_COPY;
	enum ia_css_pipe_id css_preview_pipe_id = IA_CSS_PIPE_ID_COPY;
	enum ia_css_pipe_id css_video_pipe_id = IA_CSS_PIPE_ID_COPY;
	enum atomisp_input_stream_id input_stream_id;
	struct atomisp_video_pipe *capture_pipe;
	struct atomisp_video_pipe *preview_pipe;
	struct atomisp_video_pipe *video_pipe;

	capture_pipe = &asd->video_out_capture;
	preview_pipe = &asd->video_out_preview;
	video_pipe = &asd->video_out_video_capture;

	buf_type = atomisp_get_css_buf_type(
		       asd, css_preview_pipe_id,
		       atomisp_subdev_source_pad(&preview_pipe->vdev));
	input_stream_id = ATOMISP_INPUT_STREAM_PREVIEW;
	atomisp_q_video_buffers_to_css(asd, preview_pipe,
				       input_stream_id,
				       buf_type, css_preview_pipe_id);

	buf_type = atomisp_get_css_buf_type(asd, css_capture_pipe_id,
					    atomisp_subdev_source_pad(&capture_pipe->vdev));
	input_stream_id = ATOMISP_INPUT_STREAM_GENERAL;
	atomisp_q_video_buffers_to_css(asd, capture_pipe,
				       input_stream_id,
				       buf_type, css_capture_pipe_id);

	buf_type = atomisp_get_css_buf_type(asd, css_video_pipe_id,
					    atomisp_subdev_source_pad(&video_pipe->vdev));
	input_stream_id = ATOMISP_INPUT_STREAM_VIDEO;
	atomisp_q_video_buffers_to_css(asd, video_pipe,
				       input_stream_id,
				       buf_type, css_video_pipe_id);
	return 0;
}

/* queue all available buffers to css */
int atomisp_qbuffers_to_css(struct atomisp_sub_device *asd)
{
	enum ia_css_buffer_type buf_type;
	enum ia_css_pipe_id css_capture_pipe_id = IA_CSS_PIPE_ID_NUM;
	enum ia_css_pipe_id css_preview_pipe_id = IA_CSS_PIPE_ID_NUM;
	enum ia_css_pipe_id css_video_pipe_id = IA_CSS_PIPE_ID_NUM;
	enum atomisp_input_stream_id input_stream_id;
	struct atomisp_video_pipe *capture_pipe = NULL;
	struct atomisp_video_pipe *vf_pipe = NULL;
	struct atomisp_video_pipe *preview_pipe = NULL;
	struct atomisp_video_pipe *video_pipe = NULL;
	bool raw_mode = atomisp_is_mbuscode_raw(
			    asd->fmt[asd->capture_pad].fmt.code);

	if (asd->isp->inputs[asd->input_curr].camera_caps->
	    sensor[asd->sensor_curr].stream_num == 2 &&
	    !asd->yuvpp_mode)
		return atomisp_qbuffers_to_css_for_all_pipes(asd);

	if (asd->vfpp->val == ATOMISP_VFPP_DISABLE_SCALER) {
		video_pipe = &asd->video_out_video_capture;
		css_video_pipe_id = IA_CSS_PIPE_ID_VIDEO;
	} else if (asd->vfpp->val == ATOMISP_VFPP_DISABLE_LOWLAT) {
		preview_pipe = &asd->video_out_capture;
		css_preview_pipe_id = IA_CSS_PIPE_ID_CAPTURE;
	} else if (asd->run_mode->val == ATOMISP_RUN_MODE_VIDEO) {
		if (asd->continuous_mode->val) {
			capture_pipe = &asd->video_out_capture;
			vf_pipe = &asd->video_out_vf;
			css_capture_pipe_id = IA_CSS_PIPE_ID_CAPTURE;
		}
		video_pipe = &asd->video_out_video_capture;
		preview_pipe = &asd->video_out_preview;
		css_video_pipe_id = IA_CSS_PIPE_ID_VIDEO;
		css_preview_pipe_id = IA_CSS_PIPE_ID_VIDEO;
	} else if (asd->continuous_mode->val) {
		capture_pipe = &asd->video_out_capture;
		vf_pipe = &asd->video_out_vf;
		preview_pipe = &asd->video_out_preview;

		css_preview_pipe_id = IA_CSS_PIPE_ID_PREVIEW;
		css_capture_pipe_id = IA_CSS_PIPE_ID_CAPTURE;
	} else if (asd->run_mode->val == ATOMISP_RUN_MODE_PREVIEW) {
		preview_pipe = &asd->video_out_preview;
		css_preview_pipe_id = IA_CSS_PIPE_ID_PREVIEW;
	} else {
		/* ATOMISP_RUN_MODE_STILL_CAPTURE */
		capture_pipe = &asd->video_out_capture;
		if (!raw_mode)
			vf_pipe = &asd->video_out_vf;
		css_capture_pipe_id = IA_CSS_PIPE_ID_CAPTURE;
	}

#ifdef ISP2401_NEW_INPUT_SYSTEM
	if (asd->copy_mode) {
		css_capture_pipe_id = IA_CSS_PIPE_ID_COPY;
		css_preview_pipe_id = IA_CSS_PIPE_ID_COPY;
		css_video_pipe_id = IA_CSS_PIPE_ID_COPY;
	}
#endif

	if (asd->yuvpp_mode) {
		capture_pipe = &asd->video_out_capture;
		video_pipe   = &asd->video_out_video_capture;
		preview_pipe = &asd->video_out_preview;
		css_capture_pipe_id = IA_CSS_PIPE_ID_COPY;
		css_video_pipe_id   = IA_CSS_PIPE_ID_YUVPP;
		css_preview_pipe_id = IA_CSS_PIPE_ID_YUVPP;
	}

	if (capture_pipe) {
		buf_type = atomisp_get_css_buf_type(
			       asd, css_capture_pipe_id,
			       atomisp_subdev_source_pad(&capture_pipe->vdev));
		input_stream_id = ATOMISP_INPUT_STREAM_GENERAL;

		/*
		 * use yuvpp pipe for SOC camera.
		 */
		if (ATOMISP_USE_YUVPP(asd))
			css_capture_pipe_id = IA_CSS_PIPE_ID_YUVPP;

		atomisp_q_video_buffers_to_css(asd, capture_pipe,
					       input_stream_id,
					       buf_type, css_capture_pipe_id);
	}

	if (vf_pipe) {
		buf_type = atomisp_get_css_buf_type(
			       asd, css_capture_pipe_id,
			       atomisp_subdev_source_pad(&vf_pipe->vdev));
		if (asd->stream_env[ATOMISP_INPUT_STREAM_POSTVIEW].stream)
			input_stream_id = ATOMISP_INPUT_STREAM_POSTVIEW;
		else
			input_stream_id = ATOMISP_INPUT_STREAM_GENERAL;

		/*
		 * use yuvpp pipe for SOC camera.
		 */
		if (ATOMISP_USE_YUVPP(asd))
			css_capture_pipe_id = IA_CSS_PIPE_ID_YUVPP;
		atomisp_q_video_buffers_to_css(asd, vf_pipe,
					       input_stream_id,
					       buf_type, css_capture_pipe_id);
	}

	if (preview_pipe) {
		buf_type = atomisp_get_css_buf_type(
			       asd, css_preview_pipe_id,
			       atomisp_subdev_source_pad(&preview_pipe->vdev));
		if (ATOMISP_SOC_CAMERA(asd) && css_preview_pipe_id == IA_CSS_PIPE_ID_YUVPP)
			input_stream_id = ATOMISP_INPUT_STREAM_GENERAL;
		/* else for ext isp use case */
		else if (css_preview_pipe_id == IA_CSS_PIPE_ID_YUVPP)
			input_stream_id = ATOMISP_INPUT_STREAM_VIDEO;
		else if (asd->stream_env[ATOMISP_INPUT_STREAM_PREVIEW].stream)
			input_stream_id = ATOMISP_INPUT_STREAM_PREVIEW;
		else
			input_stream_id = ATOMISP_INPUT_STREAM_GENERAL;

		/*
		 * use yuvpp pipe for SOC camera.
		 */
		if (ATOMISP_USE_YUVPP(asd))
			css_preview_pipe_id = IA_CSS_PIPE_ID_YUVPP;

		atomisp_q_video_buffers_to_css(asd, preview_pipe,
					       input_stream_id,
					       buf_type, css_preview_pipe_id);
	}

	if (video_pipe) {
		buf_type = atomisp_get_css_buf_type(
			       asd, css_video_pipe_id,
			       atomisp_subdev_source_pad(&video_pipe->vdev));
		if (asd->stream_env[ATOMISP_INPUT_STREAM_VIDEO].stream)
			input_stream_id = ATOMISP_INPUT_STREAM_VIDEO;
		else
			input_stream_id = ATOMISP_INPUT_STREAM_GENERAL;

		/*
		 * use yuvpp pipe for SOC camera.
		 */
		if (ATOMISP_USE_YUVPP(asd))
			css_video_pipe_id = IA_CSS_PIPE_ID_YUVPP;

		atomisp_q_video_buffers_to_css(asd, video_pipe,
					       input_stream_id,
					       buf_type, css_video_pipe_id);
	}

	return 0;
}

static void atomisp_buf_queue(struct videobuf_queue *vq,
			      struct videobuf_buffer *vb)
{
	struct atomisp_video_pipe *pipe = vq->priv_data;

	/*
	 * when a frame buffer meets following conditions, it should be put into
	 * the waiting list:
	 * 1.  It is not a main output frame, and it has a per-frame parameter
	 *     to go with it.
	 * 2.  It is not a main output frame, and the waiting buffer list is not
	 *     empty, to keep the FIFO sequence of frame buffer processing, it
	 *     is put to waiting list until previous per-frame parameter buffers
	 *     get enqueued.
	 */
	if (!atomisp_is_vf_pipe(pipe) &&
	    (pipe->frame_request_config_id[vb->i] ||
	     !list_empty(&pipe->buffers_waiting_for_param)))
		list_add_tail(&vb->queue, &pipe->buffers_waiting_for_param);
	else
		list_add_tail(&vb->queue, &pipe->activeq);

	vb->state = VIDEOBUF_QUEUED;
}

static void atomisp_buf_release(struct videobuf_queue *vq,
				struct videobuf_buffer *vb)
{
	vb->state = VIDEOBUF_NEEDS_INIT;
	atomisp_videobuf_free_buf(vb);
}

static int atomisp_buf_setup_output(struct videobuf_queue *vq,
				    unsigned int *count, unsigned int *size)
{
	struct atomisp_video_pipe *pipe = vq->priv_data;

	*size = pipe->pix.sizeimage;

	return 0;
}

static int atomisp_buf_prepare_output(struct videobuf_queue *vq,
				      struct videobuf_buffer *vb,
				      enum v4l2_field field)
{
	struct atomisp_video_pipe *pipe = vq->priv_data;

	vb->size = pipe->pix.sizeimage;
	vb->width = pipe->pix.width;
	vb->height = pipe->pix.height;
	vb->field = field;
	vb->state = VIDEOBUF_PREPARED;

	return 0;
}

static void atomisp_buf_queue_output(struct videobuf_queue *vq,
				     struct videobuf_buffer *vb)
{
	struct atomisp_video_pipe *pipe = vq->priv_data;

	list_add_tail(&vb->queue, &pipe->activeq_out);
	vb->state = VIDEOBUF_QUEUED;
}

static void atomisp_buf_release_output(struct videobuf_queue *vq,
				       struct videobuf_buffer *vb)
{
	videobuf_vmalloc_free(vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static const struct videobuf_queue_ops videobuf_qops = {
	.buf_setup	= atomisp_buf_setup,
	.buf_prepare	= atomisp_buf_prepare,
	.buf_queue	= atomisp_buf_queue,
	.buf_release	= atomisp_buf_release,
};

static const struct videobuf_queue_ops videobuf_qops_output = {
	.buf_setup	= atomisp_buf_setup_output,
	.buf_prepare	= atomisp_buf_prepare_output,
	.buf_queue	= atomisp_buf_queue_output,
	.buf_release	= atomisp_buf_release_output,
};

static int atomisp_init_pipe(struct atomisp_video_pipe *pipe)
{
	/* init locks */
	spin_lock_init(&pipe->irq_lock);

	videobuf_queue_vmalloc_init(&pipe->capq, &videobuf_qops, NULL,
				    &pipe->irq_lock,
				    V4L2_BUF_TYPE_VIDEO_CAPTURE,
				    V4L2_FIELD_NONE,
				    sizeof(struct atomisp_buffer), pipe,
				    NULL);	/* ext_lock: NULL */

	videobuf_queue_vmalloc_init(&pipe->outq, &videobuf_qops_output, NULL,
				    &pipe->irq_lock,
				    V4L2_BUF_TYPE_VIDEO_OUTPUT,
				    V4L2_FIELD_NONE,
				    sizeof(struct atomisp_buffer), pipe,
				    NULL);	/* ext_lock: NULL */

	INIT_LIST_HEAD(&pipe->activeq);
	INIT_LIST_HEAD(&pipe->activeq_out);
	INIT_LIST_HEAD(&pipe->buffers_waiting_for_param);
	INIT_LIST_HEAD(&pipe->per_frame_params);
	memset(pipe->frame_request_config_id, 0,
	       VIDEO_MAX_FRAME * sizeof(unsigned int));
	memset(pipe->frame_params, 0,
	       VIDEO_MAX_FRAME *
	       sizeof(struct atomisp_css_params_with_list *));

	return 0;
}

static void atomisp_dev_init_struct(struct atomisp_device *isp)
{
	unsigned int i;

	isp->sw_contex.file_input = false;
	isp->need_gfx_throttle = true;
	isp->isp_fatal_error = false;
	isp->mipi_frame_size = 0;

	for (i = 0; i < isp->input_cnt; i++)
		isp->inputs[i].asd = NULL;
	/*
	 * For Merrifield, frequency is scalable.
	 * After boot-up, the default frequency is 200MHz.
	 */
	isp->sw_contex.running_freq = ISP_FREQ_200MHZ;
}

static void atomisp_subdev_init_struct(struct atomisp_sub_device *asd)
{
	v4l2_ctrl_s_ctrl(asd->run_mode, ATOMISP_RUN_MODE_STILL_CAPTURE);
	memset(&asd->params.css_param, 0, sizeof(asd->params.css_param));
	asd->params.color_effect = V4L2_COLORFX_NONE;
	asd->params.bad_pixel_en = true;
	asd->params.gdc_cac_en = false;
	asd->params.video_dis_en = false;
	asd->params.sc_en = false;
	asd->params.fpn_en = false;
	asd->params.xnr_en = false;
	asd->params.false_color = 0;
	asd->params.online_process = 1;
	asd->params.yuv_ds_en = 0;
	/* s3a grid not enabled for any pipe */
	asd->params.s3a_enabled_pipe = IA_CSS_PIPE_ID_NUM;

	asd->params.offline_parm.num_captures = 1;
	asd->params.offline_parm.skip_frames = 0;
	asd->params.offline_parm.offset = 0;
	asd->delayed_init = ATOMISP_DELAYED_INIT_NOT_QUEUED;
	/* Add for channel */
	asd->input_curr = 0;

	asd->mipi_frame_size = 0;
	asd->copy_mode = false;
	asd->yuvpp_mode = false;

	asd->stream_prepared = false;
	asd->high_speed_mode = false;
	asd->sensor_array_res.height = 0;
	asd->sensor_array_res.width = 0;
	atomisp_css_init_struct(asd);
}

/*
 * file operation functions
 */
static unsigned int atomisp_subdev_users(struct atomisp_sub_device *asd)
{
	return asd->video_out_preview.users +
	       asd->video_out_vf.users +
	       asd->video_out_capture.users +
	       asd->video_out_video_capture.users +
	       asd->video_acc.users +
	       asd->video_in.users;
}

unsigned int atomisp_dev_users(struct atomisp_device *isp)
{
	unsigned int i, sum;

	for (i = 0, sum = 0; i < isp->num_of_streams; i++)
		sum += atomisp_subdev_users(&isp->asd[i]);

	return sum;
}

static int atomisp_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct atomisp_device *isp = video_get_drvdata(vdev);
	struct atomisp_video_pipe *pipe = NULL;
	struct atomisp_acc_pipe *acc_pipe = NULL;
	struct atomisp_sub_device *asd;
	bool acc_node = false;
	int ret;

	dev_dbg(isp->dev, "open device %s\n", vdev->name);

	rt_mutex_lock(&isp->mutex);

	acc_node = !strcmp(vdev->name, "ATOMISP ISP ACC");
	if (acc_node) {
		acc_pipe = atomisp_to_acc_pipe(vdev);
		asd = acc_pipe->asd;
	} else {
		pipe = atomisp_to_video_pipe(vdev);
		asd = pipe->asd;
	}
	asd->subdev.devnode = vdev;
	/* Deferred firmware loading case. */
	if (isp->css_env.isp_css_fw.bytes == 0) {
		dev_err(isp->dev, "Deferred firmware load.\n");
		isp->firmware = atomisp_load_firmware(isp);
		if (!isp->firmware) {
			dev_err(isp->dev, "Failed to load ISP firmware.\n");
			ret = -ENOENT;
			goto error;
		}
		ret = atomisp_css_load_firmware(isp);
		if (ret) {
			dev_err(isp->dev, "Failed to init css.\n");
			goto error;
		}
		/* No need to keep FW in memory anymore. */
		release_firmware(isp->firmware);
		isp->firmware = NULL;
		isp->css_env.isp_css_fw.data = NULL;
	}

	if (acc_node && acc_pipe->users) {
		dev_dbg(isp->dev, "acc node already opened\n");
		rt_mutex_unlock(&isp->mutex);
		return -EBUSY;
	} else if (acc_node) {
		goto dev_init;
	}

	if (!isp->input_cnt) {
		dev_err(isp->dev, "no camera attached\n");
		ret = -EINVAL;
		goto error;
	}

	/*
	 * atomisp does not allow multiple open
	 */
	if (pipe->users) {
		dev_dbg(isp->dev, "video node already opened\n");
		rt_mutex_unlock(&isp->mutex);
		return -EBUSY;
	}

	ret = atomisp_init_pipe(pipe);
	if (ret)
		goto error;

dev_init:
	if (atomisp_dev_users(isp)) {
		dev_dbg(isp->dev, "skip init isp in open\n");
		goto init_subdev;
	}

	/* runtime power management, turn on ISP */
	ret = pm_runtime_resume_and_get(vdev->v4l2_dev->dev);
	if (ret < 0) {
		dev_err(isp->dev, "Failed to power on device\n");
		goto error;
	}

	if (dypool_enable) {
		ret = hmm_pool_register(dypool_pgnr, HMM_POOL_TYPE_DYNAMIC);
		if (ret)
			dev_err(isp->dev, "Failed to register dynamic memory pool.\n");
	}

	/* Init ISP */
	if (atomisp_css_init(isp)) {
		ret = -EINVAL;
		/* Need to clean up CSS init if it fails. */
		goto css_error;
	}

	atomisp_dev_init_struct(isp);

	ret = v4l2_subdev_call(isp->flash, core, s_power, 1);
	if (ret < 0 && ret != -ENODEV && ret != -ENOIOCTLCMD) {
		dev_err(isp->dev, "Failed to power-on flash\n");
		goto css_error;
	}

init_subdev:
	if (atomisp_subdev_users(asd))
		goto done;

	atomisp_subdev_init_struct(asd);

done:

	if (acc_node)
		acc_pipe->users++;
	else
		pipe->users++;
	rt_mutex_unlock(&isp->mutex);

	/* Ensure that a mode is set */
	if (asd)
		v4l2_ctrl_s_ctrl(asd->run_mode, pipe->default_run_mode);

	return 0;

css_error:
	atomisp_css_uninit(isp);
	pm_runtime_put(vdev->v4l2_dev->dev);
error:
	hmm_pool_unregister(HMM_POOL_TYPE_DYNAMIC);
	rt_mutex_unlock(&isp->mutex);
	return ret;
}

static int atomisp_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct atomisp_device *isp = video_get_drvdata(vdev);
	struct atomisp_video_pipe *pipe;
	struct atomisp_acc_pipe *acc_pipe;
	struct atomisp_sub_device *asd;
	bool acc_node;
	struct v4l2_requestbuffers req;
	struct v4l2_subdev_fh fh;
	struct v4l2_rect clear_compose = {0};
	int ret = 0;

	v4l2_fh_init(&fh.vfh, vdev);

	req.count = 0;
	if (!isp)
		return -EBADF;

	mutex_lock(&isp->streamoff_mutex);
	rt_mutex_lock(&isp->mutex);

	dev_dbg(isp->dev, "release device %s\n", vdev->name);
	acc_node = !strcmp(vdev->name, "ATOMISP ISP ACC");
	if (acc_node) {
		acc_pipe = atomisp_to_acc_pipe(vdev);
		asd = acc_pipe->asd;
	} else {
		pipe = atomisp_to_video_pipe(vdev);
		asd = pipe->asd;
	}
	asd->subdev.devnode = vdev;
	if (acc_node) {
		acc_pipe->users--;
		goto subdev_uninit;
	}
	pipe->users--;

	if (pipe->capq.streaming)
		dev_warn(isp->dev,
			 "%s: ISP still streaming while closing!",
			 __func__);

	if (pipe->capq.streaming &&
	    __atomisp_streamoff(file, NULL, V4L2_BUF_TYPE_VIDEO_CAPTURE)) {
		dev_err(isp->dev,
			"atomisp_streamoff failed on release, driver bug");
		goto done;
	}

	if (pipe->users)
		goto done;

	if (__atomisp_reqbufs(file, NULL, &req)) {
		dev_err(isp->dev,
			"atomisp_reqbufs failed on release, driver bug");
		goto done;
	}

	if (pipe->outq.bufs[0]) {
		mutex_lock(&pipe->outq.vb_lock);
		videobuf_queue_cancel(&pipe->outq);
		mutex_unlock(&pipe->outq.vb_lock);
	}

	/*
	 * A little trick here:
	 * file injection input resolution is recorded in the sink pad,
	 * therefore can not be cleared when releaseing one device node.
	 * The sink pad setting can only be cleared when all device nodes
	 * get released.
	 */
	if (!isp->sw_contex.file_input && asd->fmt_auto->val) {
		struct v4l2_mbus_framefmt isp_sink_fmt = { 0 };

		atomisp_subdev_set_ffmt(&asd->subdev, fh.state,
					V4L2_SUBDEV_FORMAT_ACTIVE,
					ATOMISP_SUBDEV_PAD_SINK, &isp_sink_fmt);
	}
subdev_uninit:
	if (atomisp_subdev_users(asd))
		goto done;

	/* clear the sink pad for file input */
	if (isp->sw_contex.file_input && asd->fmt_auto->val) {
		struct v4l2_mbus_framefmt isp_sink_fmt = { 0 };

		atomisp_subdev_set_ffmt(&asd->subdev, fh.state,
					V4L2_SUBDEV_FORMAT_ACTIVE,
					ATOMISP_SUBDEV_PAD_SINK, &isp_sink_fmt);
	}

	atomisp_css_free_stat_buffers(asd);
	atomisp_free_internal_buffers(asd);
	ret = v4l2_subdev_call(isp->inputs[asd->input_curr].camera,
			       core, s_power, 0);
	if (ret)
		dev_warn(isp->dev, "Failed to power-off sensor\n");

	/* clear the asd field to show this camera is not used */
	isp->inputs[asd->input_curr].asd = NULL;
	asd->streaming = ATOMISP_DEVICE_STREAMING_DISABLED;

	if (atomisp_dev_users(isp))
		goto done;

	atomisp_acc_release(asd);

	atomisp_destroy_pipes_stream_force(asd);
	atomisp_css_uninit(isp);

	if (defer_fw_load) {
		ia_css_unload_firmware();
		isp->css_env.isp_css_fw.data = NULL;
		isp->css_env.isp_css_fw.bytes = 0;
	}

	hmm_pool_unregister(HMM_POOL_TYPE_DYNAMIC);

	ret = v4l2_subdev_call(isp->flash, core, s_power, 0);
	if (ret < 0 && ret != -ENODEV && ret != -ENOIOCTLCMD)
		dev_warn(isp->dev, "Failed to power-off flash\n");

	if (pm_runtime_put_sync(vdev->v4l2_dev->dev) < 0)
		dev_err(isp->dev, "Failed to power off device\n");

done:
	if (!acc_node) {
		atomisp_subdev_set_selection(&asd->subdev, fh.state,
					     V4L2_SUBDEV_FORMAT_ACTIVE,
					     atomisp_subdev_source_pad(vdev),
					     V4L2_SEL_TGT_COMPOSE, 0,
					     &clear_compose);
	}
	rt_mutex_unlock(&isp->mutex);
	mutex_unlock(&isp->streamoff_mutex);

	return 0;
}

/*
 * Memory help functions for image frame and private parameters
 */
static int do_isp_mm_remap(struct atomisp_device *isp,
			   struct vm_area_struct *vma,
			   ia_css_ptr isp_virt, u32 host_virt, u32 pgnr)
{
	u32 pfn;

	while (pgnr) {
		pfn = hmm_virt_to_phys(isp_virt) >> PAGE_SHIFT;
		if (remap_pfn_range(vma, host_virt, pfn,
				    PAGE_SIZE, PAGE_SHARED)) {
			dev_err(isp->dev, "remap_pfn_range err.\n");
			return -EAGAIN;
		}

		isp_virt += PAGE_SIZE;
		host_virt += PAGE_SIZE;
		pgnr--;
	}

	return 0;
}

static int frame_mmap(struct atomisp_device *isp,
		      const struct ia_css_frame *frame, struct vm_area_struct *vma)
{
	ia_css_ptr isp_virt;
	u32 host_virt;
	u32 pgnr;

	if (!frame) {
		dev_err(isp->dev, "%s: NULL frame pointer.\n", __func__);
		return -EINVAL;
	}

	host_virt = vma->vm_start;
	isp_virt = frame->data;
	atomisp_get_frame_pgnr(isp, frame, &pgnr);

	if (do_isp_mm_remap(isp, vma, isp_virt, host_virt, pgnr))
		return -EAGAIN;

	return 0;
}

int atomisp_videobuf_mmap_mapper(struct videobuf_queue *q,
				 struct vm_area_struct *vma)
{
	u32 offset = vma->vm_pgoff << PAGE_SHIFT;
	int ret = -EINVAL, i;
	struct atomisp_device *isp =
	    ((struct atomisp_video_pipe *)(q->priv_data))->isp;
	struct videobuf_vmalloc_memory *vm_mem;
	struct videobuf_mapping *map;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);
	if (!(vma->vm_flags & VM_WRITE) || !(vma->vm_flags & VM_SHARED)) {
		dev_err(isp->dev, "map appl bug: PROT_WRITE and MAP_SHARED are required\n");
		return -EINVAL;
	}

	mutex_lock(&q->vb_lock);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		struct videobuf_buffer *buf = q->bufs[i];

		if (!buf)
			continue;

		map = kzalloc(sizeof(struct videobuf_mapping), GFP_KERNEL);
		if (!map) {
			mutex_unlock(&q->vb_lock);
			return -ENOMEM;
		}

		buf->map = map;
		map->q = q;

		buf->baddr = vma->vm_start;

		if (buf && buf->memory == V4L2_MEMORY_MMAP &&
		    buf->boff == offset) {
			vm_mem = buf->priv;
			ret = frame_mmap(isp, vm_mem->vaddr, vma);
			vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
			break;
		}
	}
	mutex_unlock(&q->vb_lock);

	return ret;
}

/* The input frame contains left and right padding that need to be removed.
 * There is always ISP_LEFT_PAD padding on the left side.
 * There is also padding on the right (padded_width - width).
 */
static int remove_pad_from_frame(struct atomisp_device *isp,
				 struct ia_css_frame *in_frame, __u32 width, __u32 height)
{
	unsigned int i;
	unsigned short *buffer;
	int ret = 0;
	ia_css_ptr load = in_frame->data;
	ia_css_ptr store = load;

	buffer = kmalloc_array(width, sizeof(load), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	load += ISP_LEFT_PAD;
	for (i = 0; i < height; i++) {
		ret = hmm_load(load, buffer, width * sizeof(load));
		if (ret < 0)
			goto remove_pad_error;

		ret = hmm_store(store, buffer, width * sizeof(store));
		if (ret < 0)
			goto remove_pad_error;

		load  += in_frame->info.padded_width;
		store += width;
	}

remove_pad_error:
	kfree(buffer);
	return ret;
}

static int atomisp_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vdev = video_devdata(file);
	struct atomisp_device *isp = video_get_drvdata(vdev);
	struct atomisp_video_pipe *pipe = atomisp_to_video_pipe(vdev);
	struct atomisp_sub_device *asd = pipe->asd;
	struct ia_css_frame *raw_virt_addr;
	u32 start = vma->vm_start;
	u32 end = vma->vm_end;
	u32 size = end - start;
	u32 origin_size, new_size;
	int ret;

	if (!asd) {
		dev_err(isp->dev, "%s(): asd is NULL, device is %s\n",
			__func__, vdev->name);
		return -EINVAL;
	}

	if (!(vma->vm_flags & (VM_WRITE | VM_READ)))
		return -EACCES;

	rt_mutex_lock(&isp->mutex);

	if (!(vma->vm_flags & VM_SHARED)) {
		/* Map private buffer.
		 * Set VM_SHARED to the flags since we need
		 * to map the buffer page by page.
		 * Without VM_SHARED, remap_pfn_range() treats
		 * this kind of mapping as invalid.
		 */
		vma->vm_flags |= VM_SHARED;
		ret = hmm_mmap(vma, vma->vm_pgoff << PAGE_SHIFT);
		rt_mutex_unlock(&isp->mutex);
		return ret;
	}

	/* mmap for ISP offline raw data */
	if (atomisp_subdev_source_pad(vdev)
	    == ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE &&
	    vma->vm_pgoff == (ISP_PARAM_MMAP_OFFSET >> PAGE_SHIFT)) {
		new_size = pipe->pix.width * pipe->pix.height * 2;
		if (asd->params.online_process != 0) {
			ret = -EINVAL;
			goto error;
		}
		raw_virt_addr = asd->raw_output_frame;
		if (!raw_virt_addr) {
			dev_err(isp->dev, "Failed to request RAW frame\n");
			ret = -EINVAL;
			goto error;
		}

		ret = remove_pad_from_frame(isp, raw_virt_addr,
					    pipe->pix.width, pipe->pix.height);
		if (ret < 0) {
			dev_err(isp->dev, "remove pad failed.\n");
			goto error;
		}
		origin_size = raw_virt_addr->data_bytes;
		raw_virt_addr->data_bytes = new_size;

		if (size != PAGE_ALIGN(new_size)) {
			dev_err(isp->dev, "incorrect size for mmap ISP  Raw Frame\n");
			ret = -EINVAL;
			goto error;
		}

		if (frame_mmap(isp, raw_virt_addr, vma)) {
			dev_err(isp->dev, "frame_mmap failed.\n");
			raw_virt_addr->data_bytes = origin_size;
			ret = -EAGAIN;
			goto error;
		}
		raw_virt_addr->data_bytes = origin_size;
		vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
		rt_mutex_unlock(&isp->mutex);
		return 0;
	}

	/*
	 * mmap for normal frames
	 */
	if (size != pipe->pix.sizeimage) {
		dev_err(isp->dev, "incorrect size for mmap ISP frames\n");
		ret = -EINVAL;
		goto error;
	}
	rt_mutex_unlock(&isp->mutex);

	return atomisp_videobuf_mmap_mapper(&pipe->capq, vma);

error:
	rt_mutex_unlock(&isp->mutex);

	return ret;
}

static int atomisp_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vdev = video_devdata(file);
	struct atomisp_video_pipe *pipe = atomisp_to_video_pipe(vdev);

	return videobuf_mmap_mapper(&pipe->outq, vma);
}

static __poll_t atomisp_poll(struct file *file,
			     struct poll_table_struct *pt)
{
	struct video_device *vdev = video_devdata(file);
	struct atomisp_device *isp = video_get_drvdata(vdev);
	struct atomisp_video_pipe *pipe = atomisp_to_video_pipe(vdev);

	rt_mutex_lock(&isp->mutex);
	if (pipe->capq.streaming != 1) {
		rt_mutex_unlock(&isp->mutex);
		return EPOLLERR;
	}
	rt_mutex_unlock(&isp->mutex);

	return videobuf_poll_stream(file, &pipe->capq, pt);
}

const struct v4l2_file_operations atomisp_fops = {
	.owner = THIS_MODULE,
	.open = atomisp_open,
	.release = atomisp_release,
	.mmap = atomisp_mmap,
	.unlocked_ioctl = video_ioctl2,
#ifdef CONFIG_COMPAT
	/*
	 * this was removed because of bugs, the interface
	 * needs to be made safe for compat tasks instead.
	.compat_ioctl32 = atomisp_compat_ioctl32,
	 */
#endif
	.poll = atomisp_poll,
};

const struct v4l2_file_operations atomisp_file_fops = {
	.owner = THIS_MODULE,
	.open = atomisp_open,
	.release = atomisp_release,
	.mmap = atomisp_file_mmap,
	.unlocked_ioctl = video_ioctl2,
#ifdef CONFIG_COMPAT
	/* .compat_ioctl32 = atomisp_compat_ioctl32, */
#endif
	.poll = atomisp_poll,
};
