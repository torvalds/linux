// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include <linux/pm_domain.h>
#include <linux/units.h>

#include "isp4.h"
#include "isp4_debug.h"
#include "isp4_fw_cmd_resp.h"
#include "isp4_interface.h"

#define ISP4SD_MIN_BUF_CNT_BEF_START_STREAM 4

#define ISP4SD_PERFORMANCE_STATE_LOW 0
#define ISP4SD_PERFORMANCE_STATE_HIGH 1

/* align 32KB */
#define ISP4SD_META_BUF_SIZE ALIGN(sizeof(struct isp4fw_meta_info), 0x8000)

#define to_isp4_subdev(sd) container_of(sd, struct isp4_subdev, sdev)

static const char *isp4sd_entity_name = "amd isp4";

static const char *isp4sd_thread_name[ISP4SD_MAX_FW_RESP_STREAM_NUM] = {
	"amd_isp4_thread_global",
	"amd_isp4_thread_stream1",
};

static void isp4sd_module_enable(struct isp4_subdev *isp_subdev, bool enable)
{
	if (isp_subdev->enable_gpio) {
		gpiod_set_value(isp_subdev->enable_gpio, enable ? 1 : 0);
		dev_dbg(isp_subdev->dev, "%s isp_subdev module\n",
			enable ? "enable" : "disable");
	}
}

static int isp4sd_setup_fw_mem_pool(struct isp4_subdev *isp_subdev)
{
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct isp4fw_cmd_send_buffer buf_type;
	struct device *dev = isp_subdev->dev;
	int ret;

	if (!ispif->fw_mem_pool) {
		dev_err(dev, "fail to alloc mem pool\n");
		return -ENOMEM;
	}

	/*
	 * The struct will be shared with ISP FW, use memset() to guarantee
	 * padding bits are zeroed, since this is not guaranteed on all
	 * compilers.
	 */
	memset(&buf_type, 0, sizeof(buf_type));
	buf_type.buffer_type = ISP4FW_BUFFER_TYPE_MEM_POOL;
	buf_type.buffer.vmid_space.bit.space = ISP4FW_ADDR_SPACE_TYPE_GPU_VA;
	isp4if_split_addr64(ispif->fw_mem_pool->gpu_mc_addr,
			    &buf_type.buffer.buf_base_a_lo,
			    &buf_type.buffer.buf_base_a_hi);
	buf_type.buffer.buf_size_a = ispif->fw_mem_pool->mem_size;

	ret = isp4if_send_command(ispif, ISP4FW_CMD_ID_SEND_BUFFER,
				  &buf_type, sizeof(buf_type));
	if (ret) {
		dev_err(dev, "send fw mem pool 0x%llx(%u) fail %d\n",
			ispif->fw_mem_pool->gpu_mc_addr,
			buf_type.buffer.buf_size_a, ret);
		return ret;
	}

	dev_dbg(dev, "send fw mem pool 0x%llx(%u) suc\n",
		ispif->fw_mem_pool->gpu_mc_addr, buf_type.buffer.buf_size_a);

	return 0;
}

static int isp4sd_set_stream_path(struct isp4_subdev *isp_subdev)
{
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct isp4fw_cmd_set_stream_cfg cmd;
	struct device *dev = isp_subdev->dev;

	/*
	 * The struct will be shared with ISP FW, use memset() to guarantee
	 * padding bits are zeroed, since this is not guaranteed on all
	 * compilers.
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.stream_cfg.mipi_pipe_path_cfg.isp4fw_sensor_id =
		ISP4FW_SENSOR_ID_ON_MIPI0;
	cmd.stream_cfg.mipi_pipe_path_cfg.b_enable = true;
	cmd.stream_cfg.isp_pipe_path_cfg.isp_pipe_id =
		ISP4FW_MIPI0_ISP_PIPELINE_ID;

	cmd.stream_cfg.b_enable_tnr = true;
	dev_dbg(dev, "isp4fw_sensor_id %d, pipeId 0x%x EnableTnr %u\n",
		cmd.stream_cfg.mipi_pipe_path_cfg.isp4fw_sensor_id,
		cmd.stream_cfg.isp_pipe_path_cfg.isp_pipe_id,
		cmd.stream_cfg.b_enable_tnr);

	return isp4if_send_command(ispif, ISP4FW_CMD_ID_SET_STREAM_CONFIG,
				   &cmd, sizeof(cmd));
}

static int isp4sd_send_meta_buf(struct isp4_subdev *isp_subdev)
{
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct isp4fw_cmd_send_buffer buf_type;
	struct device *dev = isp_subdev->dev;

	/*
	 * The struct will be shared with ISP FW, use memset() to guarantee
	 * padding bits are zeroed, since this is not guaranteed on all
	 * compilers.
	 */
	memset(&buf_type, 0, sizeof(buf_type));
	for (unsigned int i = 0; i < ISP4IF_MAX_STREAM_BUF_COUNT; i++) {
		struct isp4if_gpu_mem_info *meta_info_buf =
				isp_subdev->ispif.meta_info_buf[i];
		int ret;

		if (!meta_info_buf) {
			dev_err(dev, "fail for no meta info buf(%u)\n", i);
			return -ENOMEM;
		}

		buf_type.buffer_type = ISP4FW_BUFFER_TYPE_META_INFO;
		buf_type.buffer.vmid_space.bit.space =
			ISP4FW_ADDR_SPACE_TYPE_GPU_VA;
		isp4if_split_addr64(meta_info_buf->gpu_mc_addr,
				    &buf_type.buffer.buf_base_a_lo,
				    &buf_type.buffer.buf_base_a_hi);
		buf_type.buffer.buf_size_a = meta_info_buf->mem_size;
		ret = isp4if_send_command(ispif, ISP4FW_CMD_ID_SEND_BUFFER,
					  &buf_type, sizeof(buf_type));
		if (ret) {
			dev_err(dev, "send meta info(%u) fail\n", i);
			return ret;
		}
	}

	dev_dbg(dev, "send meta info suc\n");
	return 0;
}

static bool isp4sd_get_str_out_prop(struct isp4_subdev *isp_subdev,
				    struct isp4fw_image_prop *out_prop,
				    struct v4l2_subdev_state *state, u32 pad)
{
	struct device *dev = isp_subdev->dev;
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_state_get_format(state, pad, 0);
	if (!format) {
		dev_err(dev, "fail get subdev state format\n");
		return false;
	}

	switch (format->code) {
	case MEDIA_BUS_FMT_YUYV8_1_5X8:
		out_prop->image_format = ISP4FW_IMAGE_FORMAT_NV12;
		out_prop->width = format->width;
		out_prop->height = format->height;
		out_prop->luma_pitch = format->width;
		out_prop->chroma_pitch = out_prop->width;
		break;
	case MEDIA_BUS_FMT_YUYV8_1X16:
		out_prop->image_format = ISP4FW_IMAGE_FORMAT_YUV422INTERLEAVED;
		out_prop->width = format->width;
		out_prop->height = format->height;
		out_prop->luma_pitch = format->width * 2;
		out_prop->chroma_pitch = 0;
		break;
	default:
		dev_err(dev, "fail for bad image format:0x%x\n",
			format->code);
		return false;
	}

	if (!out_prop->width || !out_prop->height)
		return false;

	return true;
}

static int isp4sd_kickoff_stream(struct isp4_subdev *isp_subdev, u32 w, u32 h)
{
	struct isp4sd_sensor_info *sensor_info = &isp_subdev->sensor_info;
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct device *dev = isp_subdev->dev;

	if (sensor_info->status == ISP4SD_START_STATUS_STARTED)
		return 0;

	if (sensor_info->status == ISP4SD_START_STATUS_START_FAIL) {
		dev_err(dev, "fail for previous start fail\n");
		return -EINVAL;
	}

	dev_dbg(dev, "w:%u,h:%u\n", w, h);

	if (isp4sd_send_meta_buf(isp_subdev)) {
		dev_err(dev, "fail to send meta buf\n");
		sensor_info->status = ISP4SD_START_STATUS_START_FAIL;
		return -EINVAL;
	}

	sensor_info->status = ISP4SD_START_STATUS_OFF;

	if (!sensor_info->start_stream_cmd_sent &&
	    sensor_info->buf_sent_cnt >= ISP4SD_MIN_BUF_CNT_BEF_START_STREAM) {
		int ret = isp4if_send_command(ispif, ISP4FW_CMD_ID_START_STREAM,
					      NULL, 0);
		if (ret) {
			dev_err(dev, "fail to start stream\n");
			return ret;
		}

		sensor_info->start_stream_cmd_sent = true;
	} else {
		dev_dbg(dev,
			"no send START_STREAM, start_sent %u, buf_sent %u\n",
			sensor_info->start_stream_cmd_sent,
			sensor_info->buf_sent_cnt);
	}

	return 0;
}

static int isp4sd_setup_output(struct isp4_subdev *isp_subdev,
			       struct v4l2_subdev_state *state, u32 pad)
{
	struct isp4sd_output_info *output_info =
			&isp_subdev->sensor_info.output_info;
	struct isp4sd_sensor_info *sensor_info = &isp_subdev->sensor_info;
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct isp4fw_cmd_set_out_ch_prop cmd_ch_prop;
	struct isp4fw_cmd_enable_out_ch cmd_ch_en;
	struct device *dev = isp_subdev->dev;
	int ret;

	if (output_info->start_status == ISP4SD_START_STATUS_STARTED)
		return 0;

	if (output_info->start_status == ISP4SD_START_STATUS_START_FAIL) {
		dev_err(dev, "fail for previous start fail\n");
		return -EINVAL;
	}

	/*
	 * The struct will be shared with ISP FW, use memset() to guarantee
	 * padding bits are zeroed, since this is not guaranteed on all
	 * compilers.
	 */
	memset(&cmd_ch_prop, 0, sizeof(cmd_ch_prop));
	cmd_ch_prop.ch = ISP4FW_ISP_PIPE_OUT_CH_PREVIEW;

	if (!isp4sd_get_str_out_prop(isp_subdev,
				     &cmd_ch_prop.image_prop, state, pad)) {
		dev_err(dev, "fail to get out prop\n");
		return -EINVAL;
	}

	dev_dbg(dev, "channel:%s,fmt %s,w:h=%u:%u,lp:%u,cp%u\n",
		isp4dbg_get_out_ch_str(cmd_ch_prop.ch),
		isp4dbg_get_img_fmt_str(cmd_ch_prop.image_prop.image_format),
		cmd_ch_prop.image_prop.width, cmd_ch_prop.image_prop.height,
		cmd_ch_prop.image_prop.luma_pitch,
		cmd_ch_prop.image_prop.chroma_pitch);

	ret = isp4if_send_command(ispif, ISP4FW_CMD_ID_SET_OUT_CHAN_PROP,
				  &cmd_ch_prop, sizeof(cmd_ch_prop));
	if (ret) {
		output_info->start_status = ISP4SD_START_STATUS_START_FAIL;
		dev_err(dev, "fail to set out prop\n");
		return ret;
	}

	/*
	 * The struct will be shared with ISP FW, use memset() to guarantee
	 * padding bits are zeroed, since this is not guaranteed on all
	 * compilers.
	 */
	memset(&cmd_ch_en, 0, sizeof(cmd_ch_en));
	cmd_ch_en.ch = ISP4FW_ISP_PIPE_OUT_CH_PREVIEW;
	cmd_ch_en.is_enable = true;
	ret = isp4if_send_command(ispif, ISP4FW_CMD_ID_ENABLE_OUT_CHAN,
				  &cmd_ch_en, sizeof(cmd_ch_en));
	if (ret) {
		output_info->start_status = ISP4SD_START_STATUS_START_FAIL;
		dev_err(dev, "fail to enable channel\n");
		return ret;
	}

	dev_dbg(dev, "enable channel %s\n",
		isp4dbg_get_out_ch_str(cmd_ch_en.ch));

	if (!sensor_info->start_stream_cmd_sent) {
		ret = isp4sd_kickoff_stream(isp_subdev,
					    cmd_ch_prop.image_prop.width,
					    cmd_ch_prop.image_prop.height);
		if (ret) {
			dev_err(dev, "kickoff stream fail %d\n", ret);
			return ret;
		}
		/*
		 * sensor_info->start_stream_cmd_sent will be set to true
		 * 1. in isp4sd_kickoff_stream, if app first send buffer then
		 * start stream
		 * 2. in isp_set_stream_buf, if app first start stream, then
		 * send buffer because ISP FW has the requirement, host needs
		 * to send buffer before send start stream cmd
		 */
		if (sensor_info->start_stream_cmd_sent) {
			sensor_info->status = ISP4SD_START_STATUS_STARTED;
			output_info->start_status = ISP4SD_START_STATUS_STARTED;
			dev_dbg(dev, "kickoff stream suc,start cmd sent\n");
		}
	} else {
		dev_dbg(dev, "stream running, no need kickoff\n");
		output_info->start_status = ISP4SD_START_STATUS_STARTED;
	}

	dev_dbg(dev, "setup output suc\n");
	return 0;
}

static int isp4sd_init_stream(struct isp4_subdev *isp_subdev)
{
	struct device *dev = isp_subdev->dev;
	int ret;

	ret = isp4sd_setup_fw_mem_pool(isp_subdev);
	if (ret) {
		dev_err(dev, "fail to setup fw mem pool\n");
		return ret;
	}

	ret = isp4sd_set_stream_path(isp_subdev);
	if (ret) {
		dev_err(dev, "fail to setup stream path\n");
		return ret;
	}

	return 0;
}

static void isp4sd_uninit_stream(struct isp4_subdev *isp_subdev,
				 struct v4l2_subdev_state *state, u32 pad)
{
	struct isp4sd_sensor_info *sensor_info = &isp_subdev->sensor_info;
	struct isp4sd_output_info *output_info = &sensor_info->output_info;
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_state_get_format(state, pad, 0);
	if (!format) {
		dev_err(isp_subdev->dev, "fail to get v4l2 format\n");
	} else {
		memset(format, 0, sizeof(*format));
		format->code = MEDIA_BUS_FMT_YUYV8_1_5X8;
	}

	isp4if_clear_bufq(ispif);
	isp4if_clear_cmdq(ispif);

	sensor_info->start_stream_cmd_sent = false;
	sensor_info->buf_sent_cnt = 0;

	sensor_info->status = ISP4SD_START_STATUS_OFF;
	output_info->start_status = ISP4SD_START_STATUS_OFF;
}

static void isp4sd_fw_resp_cmd_done(struct isp4_subdev *isp_subdev,
				    enum isp4if_stream_id stream_id,
				    struct isp4fw_resp_cmd_done *para)
{
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct isp4if_cmd_element *ele =
		isp4if_rm_cmd_from_cmdq(ispif, para->cmd_seq_num, para->cmd_id);
	struct device *dev = isp_subdev->dev;

	dev_dbg(dev, "stream %d,cmd %s(0x%08x)(%d),seq %u, ele %p\n",
		stream_id,
		isp4dbg_get_cmd_str(para->cmd_id),
		para->cmd_id, para->cmd_status, para->cmd_seq_num,
		ele);

	if (ele) {
		complete(&ele->cmd_done);
		if (atomic_dec_and_test(&ele->refcnt))
			kfree(ele);
	}
}

static struct isp4fw_meta_info *
isp4sd_get_meta_by_mc(struct isp4_subdev *isp_subdev, u64 mc)
{
	for (unsigned int i = 0; i < ISP4IF_MAX_STREAM_BUF_COUNT; i++) {
		struct isp4if_gpu_mem_info *meta_info_buf =
				isp_subdev->ispif.meta_info_buf[i];

		if (meta_info_buf->gpu_mc_addr == mc)
			return meta_info_buf->sys_addr;
	}

	return NULL;
}

static void isp4sd_send_meta_info(struct isp4_subdev *isp_subdev,
				  u64 meta_info_mc)
{
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct isp4fw_cmd_send_buffer buf_type;
	struct device *dev = isp_subdev->dev;

	if (isp_subdev->sensor_info.status != ISP4SD_START_STATUS_STARTED) {
		dev_warn(dev, "not working status %i, meta_info 0x%llx\n",
			 isp_subdev->sensor_info.status, meta_info_mc);
		return;
	}

	/*
	 * The struct will be shared with ISP FW, use memset() to guarantee
	 * padding bits are zeroed, since this is not guaranteed on all
	 * compilers.
	 */
	memset(&buf_type, 0, sizeof(buf_type));
	buf_type.buffer_type = ISP4FW_BUFFER_TYPE_META_INFO;
	buf_type.buffer.vmid_space.bit.space = ISP4FW_ADDR_SPACE_TYPE_GPU_VA;
	isp4if_split_addr64(meta_info_mc,
			    &buf_type.buffer.buf_base_a_lo,
			    &buf_type.buffer.buf_base_a_hi);
	buf_type.buffer.buf_size_a = ISP4SD_META_BUF_SIZE;

	if (isp4if_send_command(ispif, ISP4FW_CMD_ID_SEND_BUFFER,
				&buf_type, sizeof(buf_type)))
		dev_err(dev, "fail send meta_info 0x%llx\n",
			meta_info_mc);
	else
		dev_dbg(dev, "resend meta_info 0x%llx\n", meta_info_mc);
}

static void isp4sd_fw_resp_frame_done(struct isp4_subdev *isp_subdev,
				      enum isp4if_stream_id stream_id,
				      struct isp4fw_resp_param_package *para)
{
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct device *dev = isp_subdev->dev;
	struct isp4if_img_buf_node *prev;
	struct isp4fw_meta_info *meta;
	u64 mc;

	mc = isp4if_join_addr64(para->package_addr_lo, para->package_addr_hi);
	meta = isp4sd_get_meta_by_mc(isp_subdev, mc);
	if (!meta) {
		dev_err(dev, "fail to get meta from mc %llx\n", mc);
		return;
	}

	dev_dbg(dev, "ts:%llu,streamId:%d,poc:%u,preview_en:%u,status:%s(%i)\n",
		ktime_get_ns(), stream_id, meta->poc, meta->preview.enabled,
		isp4dbg_get_buf_done_str(meta->preview.status),
		meta->preview.status);

	if (meta->preview.enabled &&
	    (meta->preview.status == ISP4FW_BUFFER_STATUS_SKIPPED ||
	     meta->preview.status == ISP4FW_BUFFER_STATUS_DONE ||
	     meta->preview.status == ISP4FW_BUFFER_STATUS_DIRTY)) {
		prev = isp4if_dequeue_buffer(ispif);
		if (prev) {
			isp4dbg_show_bufmeta_info(dev, "prev", &meta->preview,
						  &prev->buf_info);
			isp4vid_handle_frame_done(&isp_subdev->isp_vdev,
						  &prev->buf_info);
			isp4if_dealloc_buffer_node(prev);
		} else {
			dev_err(dev, "fail null prev buf\n");
		}
	} else if (meta->preview.enabled) {
		dev_err(dev, "fail bad preview status %u(%s)\n",
			meta->preview.status,
			isp4dbg_get_buf_done_str(meta->preview.status));
	}

	if (isp_subdev->sensor_info.status == ISP4SD_START_STATUS_STARTED)
		isp4sd_send_meta_info(isp_subdev, mc);

	dev_dbg(dev, "stream_id:%d, status:%d\n", stream_id,
		isp_subdev->sensor_info.status);
}

static void isp4sd_fw_resp_func(struct isp4_subdev *isp_subdev,
				enum isp4if_stream_id stream_id)
{
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct device *dev = isp_subdev->dev;
	struct isp4fw_resp resp;

	if (stream_id == ISP4IF_STREAM_ID_1)
		isp_fw_log_print(isp_subdev);

	while (true) {
		if (isp4if_f2h_resp(ispif, stream_id, &resp)) {
			/* Re-enable the interrupt */
			isp4_intr_enable(isp_subdev, stream_id, true);
			/*
			 * Recheck to see if there is a new response.
			 * To ensure that an in-flight interrupt is not lost,
			 * enabling the interrupt must occur _before_ checking
			 * for a new response, hence a memory barrier is needed.
			 * Disable the interrupt again if there was a new
			 * response.
			 */
			mb();
			if (likely(isp4if_f2h_resp(ispif, stream_id, &resp)))
				break;

			isp4_intr_enable(isp_subdev, stream_id, false);
		}

		switch (resp.resp_id) {
		case ISP4FW_RESP_ID_CMD_DONE:
			isp4sd_fw_resp_cmd_done(isp_subdev, stream_id,
						&resp.param.cmd_done);
			break;
		case ISP4FW_RESP_ID_NOTI_FRAME_DONE:
			isp4sd_fw_resp_frame_done(isp_subdev, stream_id,
						  &resp.param.frame_done);
			break;
		default:
			dev_err(dev, "-><- fail respid %s(0x%x)\n",
				isp4dbg_get_resp_str(resp.resp_id),
				resp.resp_id);
			break;
		}
	}
}

static s32 isp4sd_fw_resp_thread(void *context)
{
	struct isp4_subdev_thread_param *para = context;
	struct isp4_subdev *isp_subdev = para->isp_subdev;
	struct isp4sd_thread_handler *thread_ctx =
			&isp_subdev->fw_resp_thread[para->idx];
	struct device *dev = isp_subdev->dev;

	dev_dbg(dev, "[%u] fw resp thread started\n", para->idx);
	while (true) {
		wait_event_interruptible(thread_ctx->waitq,
					 thread_ctx->resp_ready);
		thread_ctx->resp_ready = false;

		if (kthread_should_stop()) {
			dev_dbg(dev, "[%u] fw resp thread quit\n", para->idx);
			break;
		}

		isp4sd_fw_resp_func(isp_subdev, para->idx);
	}

	return 0;
}

static int isp4sd_stop_resp_proc_threads(struct isp4_subdev *isp_subdev)
{
	for (unsigned int i = 0; i < ISP4SD_MAX_FW_RESP_STREAM_NUM; i++) {
		struct isp4sd_thread_handler *thread_ctx =
				&isp_subdev->fw_resp_thread[i];

		if (thread_ctx->thread) {
			kthread_stop(thread_ctx->thread);
			thread_ctx->thread = NULL;
		}
	}

	return 0;
}

static int isp4sd_start_resp_proc_threads(struct isp4_subdev *isp_subdev)
{
	struct device *dev = isp_subdev->dev;

	for (unsigned int i = 0; i < ISP4SD_MAX_FW_RESP_STREAM_NUM; i++) {
		struct isp4sd_thread_handler *thread_ctx =
				&isp_subdev->fw_resp_thread[i];

		isp_subdev->isp_resp_para[i].idx = i;
		isp_subdev->isp_resp_para[i].isp_subdev = isp_subdev;
		init_waitqueue_head(&thread_ctx->waitq);
		thread_ctx->resp_ready = false;

		thread_ctx->thread = kthread_run(isp4sd_fw_resp_thread,
						 &isp_subdev->isp_resp_para[i],
						 isp4sd_thread_name[i]);
		if (IS_ERR(thread_ctx->thread)) {
			dev_err(dev, "create thread [%d] fail\n", i);
			thread_ctx->thread = NULL;
			isp4sd_stop_resp_proc_threads(isp_subdev);
			return -EINVAL;
		}
	}

	return 0;
}

int isp4sd_pwroff_and_deinit(struct v4l2_subdev *sd)
{
	struct isp4_subdev *isp_subdev = to_isp4_subdev(sd);
	struct isp4sd_sensor_info *sensor_info = &isp_subdev->sensor_info;
	unsigned int perf_state = ISP4SD_PERFORMANCE_STATE_LOW;
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct device *dev = isp_subdev->dev;
	int ret;

	guard(mutex)(&isp_subdev->ops_mutex);
	if (sensor_info->status == ISP4SD_START_STATUS_STARTED) {
		dev_err(dev, "fail for stream still running\n");
		return -EINVAL;
	}

	sensor_info->status = ISP4SD_START_STATUS_OFF;

	if (isp_subdev->irq_enabled) {
		for (unsigned int i = 0; i < ISP4SD_MAX_FW_RESP_STREAM_NUM; i++)
			disable_irq(isp_subdev->irq[i]);
		isp_subdev->irq_enabled = false;
	}

	isp4sd_stop_resp_proc_threads(isp_subdev);
	dev_dbg(dev, "isp_subdev stop resp proc threads suc\n");

	isp4if_stop(ispif);

	ret = dev_pm_genpd_set_performance_state(dev, perf_state);
	if (ret)
		dev_err(dev,
			"fail to set isp_subdev performance state %u,ret %d\n",
			perf_state, ret);

	/* hold ccpu reset */
	isp4hw_wreg(isp_subdev->mmio, ISP_SOFT_RESET, 0);
	isp4hw_wreg(isp_subdev->mmio, ISP_POWER_STATUS, 0);
	ret = pm_runtime_put_sync(dev);
	if (ret)
		dev_err(dev, "power off isp_subdev fail %d\n", ret);
	else
		dev_dbg(dev, "power off isp_subdev suc\n");

	ispif->status = ISP4IF_STATUS_PWR_OFF;
	isp4if_clear_cmdq(ispif);
	isp4sd_module_enable(isp_subdev, false);

	/*
	 * When opening the camera, isp4sd_module_enable(isp_subdev, true) is
	 * called. Hardware requires at least a 20ms delay between disabling
	 * and enabling the module, so a sleep is added to ensure ISP stability
	 * during quick reopen scenarios.
	 */
	msleep(20);

	return 0;
}

int isp4sd_pwron_and_init(struct v4l2_subdev *sd)
{
	struct isp4_subdev *isp_subdev = to_isp4_subdev(sd);
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct device *dev = isp_subdev->dev;
	int ret;

	guard(mutex)(&isp_subdev->ops_mutex);
	if (ispif->status == ISP4IF_STATUS_FW_RUNNING) {
		dev_dbg(dev, "camera already opened, do nothing\n");
		return 0;
	}

	isp4sd_module_enable(isp_subdev, true);

	if (ispif->status < ISP4IF_STATUS_PWR_ON) {
		unsigned int perf_state = ISP4SD_PERFORMANCE_STATE_HIGH;

		ret = pm_runtime_resume_and_get(dev);
		if (ret) {
			dev_err(dev, "fail to power on isp_subdev ret %d\n",
				ret);
			goto err_deinit;
		}

		/* ISPPG ISP Power Status */
		isp4hw_wreg(isp_subdev->mmio, ISP_POWER_STATUS, 0x7FF);
		ret = dev_pm_genpd_set_performance_state(dev, perf_state);
		if (ret) {
			dev_err(dev,
				"fail to set performance state %u, ret %d\n",
				perf_state, ret);
			goto err_deinit;
		}

		ispif->status = ISP4IF_STATUS_PWR_ON;
	}

	isp_subdev->sensor_info.start_stream_cmd_sent = false;
	isp_subdev->sensor_info.buf_sent_cnt = 0;

	ret = isp4if_start(ispif);
	if (ret) {
		dev_err(dev, "fail to start isp_subdev interface\n");
		goto err_deinit;
	}

	if (isp4sd_start_resp_proc_threads(isp_subdev)) {
		dev_err(dev, "isp_start_resp_proc_threads fail\n");
		goto err_deinit;
	}

	dev_dbg(dev, "create resp threads ok\n");

	for (unsigned int i = 0; i < ISP4SD_MAX_FW_RESP_STREAM_NUM; i++)
		enable_irq(isp_subdev->irq[i]);
	isp_subdev->irq_enabled = true;

	return 0;
err_deinit:
	isp4sd_pwroff_and_deinit(sd);
	return -EINVAL;
}

static int isp4sd_stop_stream(struct isp4_subdev *isp_subdev,
			      struct v4l2_subdev_state *state, u32 pad)
{
	struct isp4sd_sensor_info *sensor_info = &isp_subdev->sensor_info;
	struct isp4sd_output_info *output_info = &sensor_info->output_info;
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct device *dev = isp_subdev->dev;

	guard(mutex)(&isp_subdev->ops_mutex);
	dev_dbg(dev, "status %i\n", output_info->start_status);

	if (output_info->start_status == ISP4SD_START_STATUS_STARTED) {
		struct isp4fw_cmd_enable_out_ch cmd_ch_disable;
		int ret;

		/*
		 * The struct will be shared with ISP FW, use memset() to
		 * guarantee padding bits are zeroed, since this is not
		 * guaranteed on all compilers.
		 */
		memset(&cmd_ch_disable, 0, sizeof(cmd_ch_disable));
		cmd_ch_disable.ch = ISP4FW_ISP_PIPE_OUT_CH_PREVIEW;
		/* `cmd_ch_disable.is_enable` is already false */
		ret = isp4if_send_command_sync(ispif,
					       ISP4FW_CMD_ID_ENABLE_OUT_CHAN,
					       &cmd_ch_disable,
					       sizeof(cmd_ch_disable));
		if (ret)
			dev_err(dev, "fail to disable stream\n");
		else
			dev_dbg(dev, "wait disable stream suc\n");

		ret = isp4if_send_command_sync(ispif, ISP4FW_CMD_ID_STOP_STREAM,
					       NULL, 0);
		if (ret)
			dev_err(dev, "fail to stop stream\n");
		else
			dev_dbg(dev, "wait stop stream suc\n");
	}

	isp4sd_uninit_stream(isp_subdev, state, pad);

	/*
	 * Return success to ensure the stop process proceeds,
	 * and disregard any errors since they are not fatal.
	 */
	return 0;
}

static int isp4sd_start_stream(struct isp4_subdev *isp_subdev,
			       struct v4l2_subdev_state *state, u32 pad)
{
	struct isp4sd_output_info *output_info =
			&isp_subdev->sensor_info.output_info;
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct device *dev = isp_subdev->dev;
	int ret;

	guard(mutex)(&isp_subdev->ops_mutex);

	if (ispif->status != ISP4IF_STATUS_FW_RUNNING) {
		dev_err(dev, "fail, bad fsm %d\n", ispif->status);
		return -EINVAL;
	}

	switch (output_info->start_status) {
	case ISP4SD_START_STATUS_OFF:
		break;
	case ISP4SD_START_STATUS_STARTED:
		dev_dbg(dev, "stream already started, do nothing\n");
		return 0;
	case ISP4SD_START_STATUS_START_FAIL:
		dev_err(dev, "stream previously failed to start\n");
		return -EINVAL;
	}

	ret = isp4sd_init_stream(isp_subdev);
	if (ret) {
		dev_err(dev, "fail to init isp_subdev stream\n");
		goto err_stop_stream;
	}

	ret = isp4sd_setup_output(isp_subdev, state, pad);
	if (ret) {
		dev_err(dev, "fail to setup output\n");
		goto err_stop_stream;
	}

	return 0;

err_stop_stream:
	isp4sd_stop_stream(isp_subdev, state, pad);
	return ret;
}

int isp4sd_ioc_send_img_buf(struct v4l2_subdev *sd,
			    struct isp4if_img_buf_info *buf_info)
{
	struct isp4_subdev *isp_subdev = to_isp4_subdev(sd);
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct isp4if_img_buf_node *buf_node;
	struct device *dev = isp_subdev->dev;
	int ret;

	guard(mutex)(&isp_subdev->ops_mutex);

	if (ispif->status != ISP4IF_STATUS_FW_RUNNING) {
		dev_err(dev, "fail send img buf for bad fsm %d\n",
			ispif->status);
		return -EINVAL;
	}

	buf_node = isp4if_alloc_buffer_node(buf_info);
	if (!buf_node) {
		dev_err(dev, "fail alloc sys img buf info node\n");
		return -ENOMEM;
	}

	ret = isp4if_queue_buffer(ispif, buf_node);
	if (ret) {
		dev_err(dev, "fail to queue image buf, %d\n", ret);
		goto error_release_buf_node;
	}

	if (!isp_subdev->sensor_info.start_stream_cmd_sent) {
		isp_subdev->sensor_info.buf_sent_cnt++;

		if (isp_subdev->sensor_info.buf_sent_cnt >=
		    ISP4SD_MIN_BUF_CNT_BEF_START_STREAM) {
			ret = isp4if_send_command(ispif,
						  ISP4FW_CMD_ID_START_STREAM,
						  NULL, 0);
			if (ret) {
				dev_err(dev, "fail to START_STREAM");
				goto error_release_buf_node;
			}
			isp_subdev->sensor_info.start_stream_cmd_sent = true;
			isp_subdev->sensor_info.output_info.start_status =
				ISP4SD_START_STATUS_STARTED;
			isp_subdev->sensor_info.status =
				ISP4SD_START_STATUS_STARTED;
		} else {
			dev_dbg(dev,
				"no send start, required %u, buf sent %u\n",
				ISP4SD_MIN_BUF_CNT_BEF_START_STREAM,
				isp_subdev->sensor_info.buf_sent_cnt);
		}
	}

	return 0;

error_release_buf_node:
	isp4if_dealloc_buffer_node(buf_node);
	return ret;
}

static const struct v4l2_subdev_video_ops isp4sd_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static int isp4sd_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *format)
{
	struct isp4sd_output_info *stream_info =
		&(to_isp4_subdev(sd)->sensor_info.output_info);
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_state_get_format(sd_state, format->pad);

	if (!fmt) {
		dev_err(sd->dev, "fail to get state format\n");
		return -EINVAL;
	}

	*fmt = format->format;
	switch (fmt->code) {
	case MEDIA_BUS_FMT_YUYV8_1X16:
		stream_info->image_size = fmt->width * fmt->height * 2;
		break;
	case MEDIA_BUS_FMT_YUYV8_1_5X8:
	default:
		stream_info->image_size = fmt->width * fmt->height * 3 / 2;
		break;
	}

	if (!stream_info->image_size) {
		dev_err(sd->dev,
			"fail set pad format,code 0x%x,width %u, height %u\n",
			fmt->code, fmt->width, fmt->height);
		return -EINVAL;
	}

	dev_dbg(sd->dev, "set pad format suc, code:%x w:%u h:%u size:%u\n",
		fmt->code, fmt->width, fmt->height,
		stream_info->image_size);

	return 0;
}

static int isp4sd_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct isp4_subdev *isp_subdev = to_isp4_subdev(sd);

	return isp4sd_start_stream(isp_subdev, state, pad);
}

static int isp4sd_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct isp4_subdev *isp_subdev = to_isp4_subdev(sd);

	return isp4sd_stop_stream(isp_subdev, state, pad);
}

static const struct v4l2_subdev_pad_ops isp4sd_pad_ops = {
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = isp4sd_set_pad_format,
	.enable_streams = isp4sd_enable_streams,
	.disable_streams = isp4sd_disable_streams,
};

static const struct v4l2_subdev_ops isp4sd_subdev_ops = {
	.video = &isp4sd_video_ops,
	.pad = &isp4sd_pad_ops,
};

int isp4sd_init(struct isp4_subdev *isp_subdev, struct v4l2_device *v4l2_dev,
		int irq[ISP4SD_MAX_FW_RESP_STREAM_NUM])
{
	struct isp4sd_sensor_info *sensor_info = &isp_subdev->sensor_info;
	struct isp4_interface *ispif = &isp_subdev->ispif;
	struct device *dev = v4l2_dev->dev;
	int ret;

	isp_subdev->dev = dev;
	v4l2_subdev_init(&isp_subdev->sdev, &isp4sd_subdev_ops);
	isp_subdev->sdev.owner = THIS_MODULE;
	isp_subdev->sdev.dev = dev;
	snprintf(isp_subdev->sdev.name, sizeof(isp_subdev->sdev.name), "%s",
		 dev_name(dev));

	isp_subdev->sdev.entity.name = isp4sd_entity_name;
	isp_subdev->sdev.entity.function = MEDIA_ENT_F_PROC_VIDEO_ISP;
	isp_subdev->sdev_pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&isp_subdev->sdev.entity, 1,
				     &isp_subdev->sdev_pad);
	if (ret) {
		dev_err(dev, "fail to init isp4 subdev entity pad %d\n", ret);
		return ret;
	}

	ret = v4l2_subdev_init_finalize(&isp_subdev->sdev);
	if (ret < 0) {
		dev_err(dev, "fail to init finalize isp4 subdev %d\n",
			ret);
		return ret;
	}

	ret = v4l2_device_register_subdev(v4l2_dev, &isp_subdev->sdev);
	if (ret) {
		dev_err(dev, "fail to register isp4 subdev to V4L2 device %d\n",
			ret);
		goto err_media_clean_up;
	}

	isp4if_init(ispif, dev, isp_subdev->mmio);

	mutex_init(&isp_subdev->ops_mutex);
	sensor_info->status = ISP4SD_START_STATUS_OFF;

	/* create ISP enable gpio control */
	isp_subdev->enable_gpio = devm_gpiod_get(isp_subdev->dev,
						 "enable_isp",
						 GPIOD_OUT_LOW);
	if (IS_ERR(isp_subdev->enable_gpio)) {
		ret = PTR_ERR(isp_subdev->enable_gpio);
		dev_err(dev, "fail to get gpiod %d\n", ret);
		goto err_subdev_unreg;
	}

	for (unsigned int i = 0; i < ISP4SD_MAX_FW_RESP_STREAM_NUM; i++)
		isp_subdev->irq[i] = irq[i];

	isp_subdev->host2fw_seq_num = 1;
	ispif->status = ISP4IF_STATUS_PWR_OFF;

	ret = isp4vid_dev_init(&isp_subdev->isp_vdev, &isp_subdev->sdev);
	if (ret)
		goto err_subdev_unreg;

	return 0;

err_subdev_unreg:
	v4l2_device_unregister_subdev(&isp_subdev->sdev);
err_media_clean_up:
	v4l2_subdev_cleanup(&isp_subdev->sdev);
	media_entity_cleanup(&isp_subdev->sdev.entity);
	return ret;
}

void isp4sd_deinit(struct isp4_subdev *isp_subdev)
{
	struct isp4_interface *ispif = &isp_subdev->ispif;

	isp4vid_dev_deinit(&isp_subdev->isp_vdev);
	v4l2_device_unregister_subdev(&isp_subdev->sdev);
	media_entity_cleanup(&isp_subdev->sdev.entity);
	isp4if_deinit(ispif);
	isp4sd_module_enable(isp_subdev, false);

	ispif->status = ISP4IF_STATUS_PWR_OFF;
}
