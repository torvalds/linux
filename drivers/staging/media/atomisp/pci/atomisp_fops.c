// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 */

#include <linux/module.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ioctl.h>
#include <media/videobuf2-vmalloc.h>

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

#include "ia_css_frame.h"
#include "type_support.h"
#include "device_access/device_access.h"

/*
 * Videobuf2 ops
 */
static int atomisp_queue_setup(struct vb2_queue *vq,
			       unsigned int *nbuffers, unsigned int *nplanes,
			       unsigned int sizes[], struct device *alloc_devs[])
{
	struct atomisp_video_pipe *pipe = container_of(vq, struct atomisp_video_pipe, vb_queue);
	int ret;

	mutex_lock(&pipe->asd->isp->mutex); /* for get_css_frame_info() / set_fmt() */

	/*
	 * When VIDIOC_S_FMT has not been called before VIDIOC_REQBUFS, then
	 * this will fail. Call atomisp_set_fmt() ourselves and try again.
	 */
	ret = atomisp_get_css_frame_info(pipe->asd, &pipe->frame_info);
	if (ret) {
		struct v4l2_format f = {
			.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420,
			.fmt.pix.width = 10000,
			.fmt.pix.height = 10000,
		};

		ret = atomisp_set_fmt(&pipe->vdev, &f);
		if (ret)
			goto out;

		ret = atomisp_get_css_frame_info(pipe->asd, &pipe->frame_info);
		if (ret)
			goto out;
	}

	atomisp_alloc_css_stat_bufs(pipe->asd, ATOMISP_INPUT_STREAM_GENERAL);

	*nplanes = 1;
	sizes[0] = PAGE_ALIGN(pipe->pix.sizeimage);

out:
	mutex_unlock(&pipe->asd->isp->mutex);
	return ret;
}

static int atomisp_buf_init(struct vb2_buffer *vb)
{
	struct atomisp_video_pipe *pipe = vb_to_pipe(vb);
	struct ia_css_frame *frame = vb_to_frame(vb);
	int ret;

	ret = ia_css_frame_init_from_info(frame, &pipe->frame_info);
	if (ret)
		return ret;

	if (frame->data_bytes > vb2_plane_size(vb, 0)) {
		dev_err(pipe->asd->isp->dev, "Internal error frame.data_bytes(%u) > vb.length(%lu)\n",
			frame->data_bytes, vb2_plane_size(vb, 0));
		return -EIO;
	}

	frame->data = hmm_create_from_vmalloc_buf(vb2_plane_size(vb, 0),
						  vb2_plane_vaddr(vb, 0));
	if (frame->data == mmgr_NULL)
		return -ENOMEM;

	return 0;
}

static int atomisp_q_one_metadata_buffer(struct atomisp_sub_device *asd,
	enum atomisp_input_stream_id stream_id,
	enum ia_css_pipe_id css_pipe_id)
{
	struct atomisp_metadata_buf *metadata_buf;
	enum atomisp_metadata_type md_type = ATOMISP_MAIN_METADATA;
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
			dev_dbg(asd->isp->dev, "drop one s3a stat with exp_id %d\n", exp_id);
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

static int atomisp_q_video_buffers_to_css(struct atomisp_sub_device *asd,
					  struct atomisp_video_pipe *pipe,
					  enum atomisp_input_stream_id stream_id,
					  enum ia_css_buffer_type css_buf_type,
					  enum ia_css_pipe_id css_pipe_id)
{
	struct atomisp_css_params_with_list *param;
	struct ia_css_dvs_grid_info *dvs_grid =
	    atomisp_css_get_dvs_grid_info(&asd->params.curr_grid_info);
	unsigned long irqflags;
	int space, err = 0;

	lockdep_assert_held(&asd->isp->mutex);

	if (WARN_ON(css_pipe_id >= IA_CSS_PIPE_ID_NUM))
		return -EINVAL;

	if (pipe->stopping)
		return -EINVAL;

	space = ATOMISP_CSS_Q_DEPTH - atomisp_buffers_in_css(pipe);
	while (space--) {
		struct ia_css_frame *frame;

		spin_lock_irqsave(&pipe->irq_lock, irqflags);
		frame = list_first_entry_or_null(&pipe->activeq, struct ia_css_frame, queue);
		if (frame)
			list_move_tail(&frame->queue, &pipe->buffers_in_css);
		spin_unlock_irqrestore(&pipe->irq_lock, irqflags);

		if (!frame)
			return -EINVAL;

		/*
		 * If there is a per_frame setting to apply on the buffer,
		 * do it before buffer en-queueing.
		 */
		param = pipe->frame_params[frame->vb.vb2_buf.index];
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
			atomisp_css_set_isp_config_applied_frame(asd, frame);
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
			pipe->frame_params[frame->vb.vb2_buf.index] = NULL;
		}
		/* Enqueue buffer */
		err = atomisp_q_video_buffer_to_css(asd, frame, stream_id,
						    css_buf_type, css_pipe_id);
		if (err) {
			spin_lock_irqsave(&pipe->irq_lock, irqflags);
			list_move_tail(&frame->queue, &pipe->activeq);
			spin_unlock_irqrestore(&pipe->irq_lock, irqflags);
			dev_err(asd->isp->dev, "%s, css q fails: %d\n",
				__func__, err);
			return -EINVAL;
		}

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

/* queue all available buffers to css */
int atomisp_qbuffers_to_css(struct atomisp_sub_device *asd)
{
	enum ia_css_pipe_id pipe_id;

	if (asd->copy_mode) {
		pipe_id = IA_CSS_PIPE_ID_COPY;
	} else if (asd->vfpp->val == ATOMISP_VFPP_DISABLE_SCALER) {
		pipe_id = IA_CSS_PIPE_ID_VIDEO;
	} else if (asd->vfpp->val == ATOMISP_VFPP_DISABLE_LOWLAT) {
		pipe_id = IA_CSS_PIPE_ID_CAPTURE;
	} else if (asd->run_mode->val == ATOMISP_RUN_MODE_VIDEO) {
		pipe_id = IA_CSS_PIPE_ID_VIDEO;
	} else if (asd->run_mode->val == ATOMISP_RUN_MODE_PREVIEW) {
		pipe_id = IA_CSS_PIPE_ID_PREVIEW;
	} else {
		/* ATOMISP_RUN_MODE_STILL_CAPTURE */
		pipe_id = IA_CSS_PIPE_ID_CAPTURE;
	}

	atomisp_q_video_buffers_to_css(asd, &asd->video_out,
				       ATOMISP_INPUT_STREAM_GENERAL,
				       IA_CSS_BUFFER_TYPE_OUTPUT_FRAME, pipe_id);
	return 0;
}

static void atomisp_buf_queue(struct vb2_buffer *vb)
{
	struct atomisp_video_pipe *pipe = vb_to_pipe(vb);
	struct ia_css_frame *frame = vb_to_frame(vb);
	struct atomisp_sub_device *asd = pipe->asd;
	unsigned long irqflags;
	int ret;

	mutex_lock(&asd->isp->mutex);

	ret = atomisp_pipe_check(pipe, false);
	if (ret || pipe->stopping) {
		spin_lock_irqsave(&pipe->irq_lock, irqflags);
		atomisp_buffer_done(frame, VB2_BUF_STATE_ERROR);
		spin_unlock_irqrestore(&pipe->irq_lock, irqflags);
		goto out_unlock;
	}

	/* FIXME this ugliness comes from the original atomisp buffer handling */
	if (!(vb->skip_cache_sync_on_finish && vb->skip_cache_sync_on_prepare))
		wbinvd();

	pipe->frame_params[vb->index] = NULL;

	spin_lock_irqsave(&pipe->irq_lock, irqflags);
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
	if (pipe->frame_request_config_id[vb->index] ||
	    !list_empty(&pipe->buffers_waiting_for_param))
		list_add_tail(&frame->queue, &pipe->buffers_waiting_for_param);
	else
		list_add_tail(&frame->queue, &pipe->activeq);

	spin_unlock_irqrestore(&pipe->irq_lock, irqflags);

	/* TODO: do this better, not best way to queue to css */
	if (asd->streaming) {
		if (!list_empty(&pipe->buffers_waiting_for_param))
			atomisp_handle_parameter_and_buffer(pipe);
		else
			atomisp_qbuffers_to_css(asd);
	}

out_unlock:
	mutex_unlock(&asd->isp->mutex);
}

static void atomisp_buf_cleanup(struct vb2_buffer *vb)
{
	struct atomisp_video_pipe *pipe = vb_to_pipe(vb);
	struct ia_css_frame *frame = vb_to_frame(vb);
	int index = frame->vb.vb2_buf.index;

	pipe->frame_request_config_id[index] = 0;
	pipe->frame_params[index] = NULL;

	hmm_free(frame->data);
}

const struct vb2_ops atomisp_vb2_ops = {
	.queue_setup		= atomisp_queue_setup,
	.buf_init		= atomisp_buf_init,
	.buf_cleanup		= atomisp_buf_cleanup,
	.buf_queue		= atomisp_buf_queue,
	.start_streaming	= atomisp_start_streaming,
	.stop_streaming		= atomisp_stop_streaming,
};

static void atomisp_dev_init_struct(struct atomisp_device *isp)
{
	isp->isp_fatal_error = false;

	/*
	 * For Merrifield, frequency is scalable.
	 * After boot-up, the default frequency is 200MHz.
	 */
	isp->running_freq = ISP_FREQ_200MHZ;
}

static void atomisp_subdev_init_struct(struct atomisp_sub_device *asd)
{
	memset(&asd->params.css_param, 0, sizeof(asd->params.css_param));
	asd->params.color_effect = V4L2_COLORFX_NONE;
	asd->params.bad_pixel_en = true;
	asd->params.gdc_cac_en = false;
	asd->params.video_dis_en = false;
	asd->params.sc_en = false;
	asd->params.fpn_en = false;
	asd->params.xnr_en = false;
	asd->params.false_color = 0;
	asd->params.yuv_ds_en = 0;
	/* s3a grid not enabled for any pipe */
	asd->params.s3a_enabled_pipe = IA_CSS_PIPE_ID_NUM;

	asd->copy_mode = false;

	asd->stream_prepared = false;
	asd->high_speed_mode = false;
	asd->sensor_array_res.height = 0;
	asd->sensor_array_res.width = 0;
	atomisp_css_init_struct(asd);
}

/*
 * file operation functions
 */
static int atomisp_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct atomisp_device *isp = video_get_drvdata(vdev);
	struct atomisp_video_pipe *pipe = atomisp_to_video_pipe(vdev);
	struct atomisp_sub_device *asd = pipe->asd;
	int ret;

	dev_dbg(isp->dev, "open device %s\n", vdev->name);

	ret = v4l2_fh_open(file);
	if (ret)
		return ret;

	mutex_lock(&isp->mutex);

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
		ret = -EBUSY;
		goto error;
	}

	/* runtime power management, turn on ISP */
	ret = pm_runtime_resume_and_get(vdev->v4l2_dev->dev);
	if (ret < 0) {
		dev_err(isp->dev, "Failed to power on device\n");
		goto error;
	}

	atomisp_dev_init_struct(isp);
	atomisp_subdev_init_struct(asd);

	pipe->users++;
	mutex_unlock(&isp->mutex);
	return 0;

error:
	mutex_unlock(&isp->mutex);
	v4l2_fh_release(file);
	return ret;
}

static int atomisp_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct atomisp_device *isp = video_get_drvdata(vdev);
	struct atomisp_video_pipe *pipe = atomisp_to_video_pipe(vdev);
	struct atomisp_sub_device *asd = pipe->asd;
	struct v4l2_subdev_fh fh;

	v4l2_fh_init(&fh.vfh, vdev);

	dev_dbg(isp->dev, "release device %s\n", vdev->name);

	/* Note file must not be used after this! */
	vb2_fop_release(file);

	mutex_lock(&isp->mutex);

	pipe->users--;

	atomisp_css_free_stat_buffers(asd);
	atomisp_free_internal_buffers(asd);

	atomisp_s_sensor_power(isp, asd->input_curr, 0);

	atomisp_destroy_pipes_stream(asd);

	if (pm_runtime_put_sync(vdev->v4l2_dev->dev) < 0)
		dev_err(isp->dev, "Failed to power off device\n");

	mutex_unlock(&isp->mutex);
	return 0;
}

const struct v4l2_file_operations atomisp_fops = {
	.owner = THIS_MODULE,
	.open = atomisp_open,
	.release = atomisp_release,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
#ifdef CONFIG_COMPAT
	/*
	 * this was removed because of bugs, the interface
	 * needs to be made safe for compat tasks instead.
	.compat_ioctl32 = atomisp_compat_ioctl32,
	 */
#endif
};
