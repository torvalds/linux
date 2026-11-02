// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Intel Corporation
 */

#include <linux/cleanup.h>
#include <linux/cacheflush.h>
#include <linux/pm_runtime.h>

#include "ipu6-bus.h"
#include "ipu6-dma.h"
#include "ipu6-isys.h"
#include "ipu6-platform-regs.h"
#include "ipu7-boot.h"
#include "ipu7-fw-com.h"
#include "ipu7-fw-isys.h"
#include "ipu7-isys-csi2-regs.h"
#include "ipu7-platform-regs.h"

static void ipu7_fw_isys_cleanup(struct ipu6_isys *isys)
{
	struct ipu6_bus_device *adev = isys->adev;
	struct ipu7_fw_com_context *fwctx = isys->fwctx;

	if (!fwctx)
		return;

	ipu6_ipu7_release_boot_config(adev, fwctx);

	if (fwctx->fw_config) {
		ipu6_dma_free(adev, sizeof(struct ipu7_insys_config),
			      fwctx->fw_config, fwctx->fw_config_dma_addr, 0);
		fwctx->fw_config = NULL;
		fwctx->fw_config_dma_addr = 0;
	}

	isys->fwctx = NULL;
	kfree(fwctx->queue_configs);
	kfree(fwctx);
}

static int ipu7_fw_isys_open(struct ipu6_isys *isys)
{
	return ipu6_ipu7_boot_start_fw(isys->adev, isys->fwctx);
}

static int ipu7_fw_isys_close(struct ipu6_isys *isys)
{
	int ret;

	ret = ipu6_ipu7_boot_stop_fw(isys->adev);
	if (ret)
		return ret;

	ipu7_fw_isys_cleanup(isys);

	return ret;
}

static int ipu7_fw_isys_init(struct ipu6_isys *isys, unsigned int num_streams)
{
	struct ipu7_fw_com_queue_config *queue_configs;
	struct ipu6_bus_device *adev = isys->adev;
	struct device *dev = &adev->auxdev.dev;
	struct ipu7_insys_config *fw_config;
	struct ipu7_fw_com_context *fwctx;
	dma_addr_t fw_config_dma_addr;
	unsigned int num_queues;
	u32 freq;
	int ret;

	/* Allocate and init firmware context. */
	fwctx = kzalloc_obj(*fwctx);
	if (!fwctx)
		return -ENOMEM;

	fwctx->num_input_queues = IPU7_INSYS_MAX_INPUT_QUEUES;
	fwctx->num_output_queues = IPU7_INSYS_MAX_OUTPUT_QUEUES;
	num_queues = fwctx->num_input_queues + fwctx->num_output_queues;

	queue_configs = kzalloc_objs(*queue_configs, num_queues);
	if (!queue_configs) {
		ret = -ENOMEM;
		goto err_free_fwctx;
	}
	fwctx->fw_entry = adev->fw_entry;
	fwctx->queue_configs = queue_configs;
	queue_configs[IPU7_INSYS_OUTPUT_MSG_QUEUE].max_capacity =
		IPU7_ISYS_SIZE_RECV_QUEUE;
	queue_configs[IPU7_INSYS_OUTPUT_MSG_QUEUE].token_size_in_bytes =
		sizeof(struct ipu7_insys_resp);
	queue_configs[IPU7_INSYS_OUTPUT_LOG_QUEUE].max_capacity =
		IPU7_ISYS_SIZE_LOG_QUEUE;
	queue_configs[IPU7_INSYS_OUTPUT_LOG_QUEUE].token_size_in_bytes =
		sizeof(struct ipu7_insys_resp);
	queue_configs[IPU7_INSYS_OUTPUT_RESERVED_QUEUE].max_capacity = 0;
	queue_configs[IPU7_INSYS_OUTPUT_RESERVED_QUEUE].token_size_in_bytes = 0;

	queue_configs[IPU7_INSYS_INPUT_DEV_QUEUE].max_capacity =
		IPU7_ISYS_MAX_STREAMS;
	queue_configs[IPU7_INSYS_INPUT_DEV_QUEUE].token_size_in_bytes =
		sizeof(struct ipu7_insys_send_queue_token);

	for (unsigned int i = IPU7_INSYS_INPUT_MSG_QUEUE; i < num_queues; i++) {
		queue_configs[i].max_capacity = IPU7_ISYS_SIZE_SEND_QUEUE;
		queue_configs[i].token_size_in_bytes =
			sizeof(struct ipu7_insys_send_queue_token);
	}

	/* Allocate ISYS subsys config. */
	fw_config = ipu6_dma_alloc(adev, sizeof(*fw_config),
				   &fw_config_dma_addr, GFP_KERNEL, 0);
	if (!fw_config) {
		dev_err(dev, "Failed to allocate isys subsys config.\n");
		ret = -ENOMEM;
		goto err_free_queue_configs;
	}
	fwctx->fw_config = fw_config;
	fwctx->fw_config_dma_addr = fw_config_dma_addr;
	memset(fw_config, 0, sizeof(*fw_config));
	fw_config->logger_config.use_source_severity = 0;
	fw_config->logger_config.use_channels_enable_bitmask = 1;
	fw_config->logger_config.channels_enable_bitmask =
				IPU7_LOGGER_CFG_CHANNEL_ENABLE_SYSCOM;
	fw_config->logger_config.hw_printf_buffer_base_addr = 0U;
	fw_config->logger_config.hw_printf_buffer_size_bytes = 0U;
	fw_config->wdt_config.wdt_timer1_us = 0;
	fw_config->wdt_config.wdt_timer2_us = 0;
	freq = ipu7_buttress_get_isys_freq(adev->isp);

	ipu6_dma_sync_single(adev, fw_config_dma_addr,
			     sizeof(struct ipu7_insys_config));

	isys->fwctx = fwctx;

	ret = ipu6_ipu7_init_boot_config(adev, fwctx, queue_configs,
					 num_queues, freq, fw_config_dma_addr,
					 IS_IPU8(adev->isp) ? 2U : 1U);
	if (ret) {
		ipu7_fw_isys_cleanup(isys);
		return ret;
	}

	ret = ipu7_fw_isys_open(isys);
	if (ret)
		ipu7_fw_isys_cleanup(isys);

	return ret;

err_free_queue_configs:
	kfree(queue_configs);
err_free_fwctx:
	kfree(fwctx);

	return ret;
}

static struct ipu7_insys_resp *ipu7_fw_isys_get_resp(struct ipu6_isys *isys)
{
	return ipu7_fw_com_get_token(isys->fwctx, IPU7_INSYS_OUTPUT_MSG_QUEUE);
}

static void ipu7_fw_isys_put_resp(struct ipu6_isys *isys)
{
	ipu7_fw_com_put_token(isys->fwctx, IPU7_INSYS_OUTPUT_MSG_QUEUE);
}

static int ipu7_isys_fw_pin_cfg(struct ipu6_isys_video *av,
				struct ipu6_isys_stream *stream,
				struct media_pad *src_pad,
				struct v4l2_mbus_frame_desc_entry *entry,
				void *__cfg)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(src_pad->entity);
	struct v4l2_subdev_state *state = v4l2_subdev_get_locked_active_state(sd);
	struct ipu7_fw_isys_stream_cfg *cfg = __cfg;
	struct ipu7_fw_isys_input_pin *input_pin;
	struct ipu7_fw_isys_output_pin *output_pin;
	struct ipu6_isys_queue *aq = &av->aq;
	struct v4l2_mbus_framefmt *fmt;
	const struct ipu6_isys_pixelformat *pfmt =
		ipu6_isys_get_isys_format(ipu6_isys_get_format(av), 0);
	int input_pins = cfg->nof_input_pins++;
	int output_pins;
	u32 src_stream;

	src_stream = __ipu6_isys_get_src_stream_by_src_pad(state, src_pad->index);
	fmt = v4l2_subdev_state_get_format(state, src_pad->index, src_stream);

	input_pin = &cfg->input_pins[input_pins];
	input_pin->input_res.width = fmt->width;
	input_pin->input_res.height = fmt->height;
	input_pin->dt = entry->bus.csi2.dt;
	input_pin->disable_mipi_unpacking = 0;
	if (pfmt->bpp == pfmt->bpp_packed && pfmt->bpp % BITS_PER_BYTE)
		input_pin->disable_mipi_unpacking = 1;
	input_pin->mapped_dt = IPU7_N_INSYS_MIPI_DATA_TYPE;
	input_pin->dt_rename_mode = IPU7_INSYS_MIPI_DT_NO_RENAME;
	input_pin->sync_msg_map =
		IPU7_INSYS_STREAM_SYNC_MSG_SEND_RESP_SOF |
		IPU7_INSYS_STREAM_SYNC_MSG_SEND_RESP_SOF_DISCARDED |
		IPU7_INSYS_STREAM_SYNC_MSG_SEND_IRQ_SOF |
		IPU7_INSYS_STREAM_SYNC_MSG_SEND_IRQ_SOF_DISCARDED;

	output_pins = cfg->nof_output_pins++;
	aq->fw_output = output_pins;
	av->stream->output_pins_queue[output_pins] = aq;

	output_pin = &cfg->output_pins[output_pins];
	memset(output_pin, 0, sizeof(*output_pin));
	output_pin->link.buffer_lines = 0;
	output_pin->link.foreign_key = IPU7_MSG_LINK_FOREIGN_KEY_NONE;
	output_pin->link.pbk_id = IPU7_MSG_LINK_PBK_ID_DONT_CARE;
	output_pin->link.pbk_slot_id = IPU7_MSG_LINK_PBK_SLOT_ID_DONT_CARE;
	output_pin->link.dest = 0; /* IPU_INSYS_OUTPUT_LINK_DEST_MEM */
	output_pin->link.use_sw_managed = 1;
	output_pin->crop.line_top = 0;
	output_pin->crop.line_bottom = 0;
	output_pin->dpcm.enable = 0;
	output_pin->ft = pfmt->css_pixelformat;
	output_pin->stride = ipu6_isys_get_bytes_per_line(av);
	output_pin->send_irq = 1;
	output_pin->input_pin_id = input_pins;

	return 0;
}

static int
ipu7_fw_isys_send_cmd(struct ipu6_isys *isys, const unsigned int stream_handle,
		      void *cpu_mapped_buf, dma_addr_t dma_mapped_buf,
		      size_t size, u16 send_type)
{
	struct ipu7_fw_com_context *ctx = isys->fwctx;
	/*struct device *dev = &isys->adev->auxdev.dev;*/
	struct ipu7_insys_send_queue_token *token;

	if (send_type >= N_IPU7_INSYS_SEND_TYPE)
		return -EINVAL;

	if (cpu_mapped_buf)
		clflush_cache_range(cpu_mapped_buf, size);

	token = ipu7_fw_com_get_token(ctx, stream_handle +
				      IPU7_INSYS_INPUT_MSG_QUEUE);
	if (!token)
		return -EBUSY;

	token->addr = dma_mapped_buf;
	token->buf_handle = (unsigned long)cpu_mapped_buf;
	token->send_type = send_type;
	token->stream_id = stream_handle;
	token->flag = IPU7_INSYS_SEND_QUEUE_TOKEN_FLAG_NONE;

	ipu7_fw_com_put_token(ctx, stream_handle + IPU7_INSYS_INPUT_MSG_QUEUE);
	/* now wakeup FW */
	ipu7_buttress_wakeup_isys(isys->adev->isp);

	return 0;
}

static void ipu7_fw_isys_dump_stream_cfg(struct device *dev,
					 struct isys_fw_msgs *msg)
{
	struct ipu7_fw_isys_stream_cfg *cfg = &msg->ipu7.stream;
	unsigned int i;

	dev_dbg(dev, "---------------------------\n");
	dev_dbg(dev, "IPU_FW_ISYS_STREAM_CFG_DATA\n");

	dev_dbg(dev, ".port id %d\n", cfg->port_id);
	dev_dbg(dev, ".vc %d\n", cfg->vc);
	dev_dbg(dev, ".nof_input_pins = %d\n", cfg->nof_input_pins);
	dev_dbg(dev, ".nof_output_pins = %d\n", cfg->nof_output_pins);
	dev_dbg(dev, ".stream_msg_map = 0x%x\n", cfg->stream_msg_map);

	for (i = 0; i < cfg->nof_input_pins; i++) {
		dev_dbg(dev, ".input_pin[%d]:\n", i);
		dev_dbg(dev, "\t.dt = 0x%0x\n",
			cfg->input_pins[i].dt);
		dev_dbg(dev, "\t.disable_mipi_unpacking = %d\n",
			cfg->input_pins[i].disable_mipi_unpacking);
		dev_dbg(dev, "\t.dt_rename_mode = %d\n",
			cfg->input_pins[i].dt_rename_mode);
		dev_dbg(dev, "\t.mapped_dt = 0x%0x\n",
			cfg->input_pins[i].mapped_dt);
		dev_dbg(dev, "\t.input_res = %d x %d\n",
			cfg->input_pins[i].input_res.width,
			cfg->input_pins[i].input_res.height);
		dev_dbg(dev, "\t.sync_msg_map = 0x%x\n",
			cfg->input_pins[i].sync_msg_map);
	}

	for (i = 0; i < cfg->nof_output_pins; i++) {
		dev_dbg(dev, ".output_pin[%d]:\n", i);
		dev_dbg(dev, "\t.input_pin_id = %d\n",
			cfg->output_pins[i].input_pin_id);
		dev_dbg(dev, "\t.stride = %d\n", cfg->output_pins[i].stride);
		dev_dbg(dev, "\t.send_irq = %d\n",
			cfg->output_pins[i].send_irq);
		dev_dbg(dev, "\t.ft = %d\n", cfg->output_pins[i].ft);

		dev_dbg(dev, "\t.link.buffer_lines = %d\n",
			cfg->output_pins[i].link.buffer_lines);
		dev_dbg(dev, "\t.link.foreign_key = %d\n",
			cfg->output_pins[i].link.foreign_key);
		dev_dbg(dev, "\t.link.granularity_pointer_update = %d\n",
			cfg->output_pins[i].link.granularity_pointer_update);
		dev_dbg(dev, "\t.link.msg_link_streaming_mode = %d\n",
			cfg->output_pins[i].link.msg_link_streaming_mode);
		dev_dbg(dev, "\t.link.pbk_id = %d\n",
			cfg->output_pins[i].link.pbk_id);
		dev_dbg(dev, "\t.link.pbk_slot_id = %d\n",
			cfg->output_pins[i].link.pbk_slot_id);
		dev_dbg(dev, "\t.link.dest = %d\n",
			cfg->output_pins[i].link.dest);
		dev_dbg(dev, "\t.link.use_sw_managed = %d\n",
			cfg->output_pins[i].link.use_sw_managed);
		dev_dbg(dev, "\t.link.is_snoop = %d\n",
			cfg->output_pins[i].link.is_snoop);

		dev_dbg(dev, "\t.crop.line_top = %d\n",
			cfg->output_pins[i].crop.line_top);
		dev_dbg(dev, "\t.crop.line_bottom = %d\n",
			cfg->output_pins[i].crop.line_bottom);

		dev_dbg(dev, "\t.dpcm_enable = %d\n",
			cfg->output_pins[i].dpcm.enable);
		dev_dbg(dev, "\t.dpcm.type = %d\n",
			cfg->output_pins[i].dpcm.type);
		dev_dbg(dev, "\t.dpcm.predictor = %d\n",
			cfg->output_pins[i].dpcm.predictor);
	}
	dev_dbg(dev, "---------------------------\n");
}

static void ipu7_fw_isys_dump_frame_buf_set(struct device *dev,
					    struct isys_fw_msgs *msg,
					    unsigned int outputs)
{
	struct ipu7_fw_isys_frame_buff_set *buf = &msg->ipu7.frame;

	dev_dbg(dev, "--------------------------\n");
	dev_dbg(dev, "IPU_ISYS_BUFF_SET\n");
	dev_dbg(dev, ".capture_msg_map = %d\n", buf->capture_msg_map);
	dev_dbg(dev, ".frame_id = %d\n", buf->frame_id);
	dev_dbg(dev, ".skip_frame = %d\n", buf->skip_frame);

	for (unsigned int i = 0; i < outputs; i++) {
		dev_dbg(dev, ".output_pin[%d]:\n", i);
		dev_dbg(dev, "\t.user_token = %llx\n",
			buf->output_pins[i].user_token);
		dev_dbg(dev, "\t.addr = 0x%x\n", buf->output_pins[i].addr);
	}
	dev_dbg(dev, "---------------------------\n");
}

static int ipu7_fw_isys_prepare_stream_cfg(struct ipu6_isys_stream *stream,
					   struct v4l2_mbus_frame_desc *desc,
					   struct isys_fw_msgs *msg)
{
	struct ipu7_fw_isys_stream_cfg *cfg = &msg->ipu7.stream;
	struct device *dev = &stream->isys->adev->auxdev.dev;
	int ret;

	memset(cfg, 0, sizeof(*cfg));
	cfg->port_id = stream->asd->source;
	cfg->vc = stream->vc;
	cfg->stream_msg_map = IPU7_INSYS_STREAM_ENABLE_MSG_SEND_RESP |
			      IPU7_INSYS_STREAM_ENABLE_MSG_SEND_IRQ;

	ret = ipu6_isys_fw_pins_prepare(stream, desc, ipu7_isys_fw_pin_cfg,
					cfg);
	if (ret)
		return ret;

	stream->nr_output_pins = cfg->nof_output_pins;

	ipu7_fw_isys_dump_stream_cfg(dev, msg);

	return 0;
}

static void
ipu7_isys_buf_to_fw_frame_buf_pin(struct vb2_buffer *vb,
				  struct ipu7_fw_isys_frame_buff_set *set)
{
	struct ipu6_isys_queue *aq = vb2_queue_to_isys_queue(vb->vb2_queue);
	struct vb2_v4l2_buffer *vvb = to_vb2_v4l2_buffer(vb);
	struct ipu6_isys_video_buffer *ivb =
		vb2_buffer_to_ipu6_isys_video_buffer(vvb);

	set->output_pins[aq->fw_output].addr = ivb->dma_addr;
	set->output_pins[aq->fw_output].user_token = (u64)(uintptr_t)set;
}

static void
ipu7_fw_isys_prepare_buf_set(struct isys_fw_msgs *msg,
			     struct ipu6_isys_stream *stream,
			     struct ipu6_isys_buffer_list *bl)
{
	struct ipu7_fw_isys_frame_buff_set *set = &msg->ipu7.frame;
	struct ipu6_isys_buffer *ib;

	WARN_ON(!bl->nbufs);

	memset(set, 0, sizeof(*set));
	set->capture_msg_map = IPU7_INSYS_FRAME_ENABLE_MSG_SEND_RESP |
			       IPU7_INSYS_FRAME_ENABLE_MSG_SEND_IRQ;
	set->frame_id = atomic_fetch_inc(&stream->buf_id) % 256;

	list_for_each_entry(ib, &bl->head, head) {
		struct vb2_buffer *vb = ipu6_isys_buffer_to_vb2_buffer(ib);

		ipu7_isys_buf_to_fw_frame_buf_pin(vb, set);
	}

	dev_dbg(&stream->isys->adev->auxdev.dev,
		"ipu7 frame_buf: cap_map=0x%x fid=%u opin[0].addr=0x%x token=0x%llx\n",
		set->capture_msg_map, set->frame_id,
		set->output_pins[0].addr, set->output_pins[0].user_token);
}

static void isys_stream_cfg_to_ipu8(struct ipu7_fw_isys_stream_cfg_ipu8 *dst,
				    const struct ipu7_fw_isys_stream_cfg *src)
{
	memset(dst, 0, sizeof(*dst));
	memcpy(dst->input_pins, src->input_pins, sizeof(dst->input_pins));
	dst->stream_msg_map = src->stream_msg_map;
	dst->port_id = src->port_id;
	dst->vc = src->vc;
	dst->nof_input_pins = src->nof_input_pins;
	dst->nof_output_pins = src->nof_output_pins;
	for (unsigned int i = 0; i < ARRAY_SIZE(src->output_pins); i++) {
		dst->output_pins[i].link = src->output_pins[i].link;
		dst->output_pins[i].crop.line_top =
			src->output_pins[i].crop.line_top;
		dst->output_pins[i].crop.line_bottom =
			src->output_pins[i].crop.line_bottom;
		dst->output_pins[i].dpcm = src->output_pins[i].dpcm;
		dst->output_pins[i].stride = src->output_pins[i].stride;
		dst->output_pins[i].ft = src->output_pins[i].ft;
		dst->output_pins[i].send_irq = src->output_pins[i].send_irq;
		dst->output_pins[i].input_pin_id =
			src->output_pins[i].input_pin_id;
		dst->output_pins[i].early_ack_en =
			src->output_pins[i].early_ack_en;
	}
}

static void isys_buffset_to_ipu8(struct ipu7_fw_isys_frame_buff_set_ipu8 *dst,
				 const struct ipu7_fw_isys_frame_buff_set *src)
{
	memset(dst, 0, sizeof(*dst));
	for (unsigned int i = 0; i < ARRAY_SIZE(src->output_pins); i++)
		dst->output_pins[i].pin_payload = src->output_pins[i];
	dst->capture_msg_map = src->capture_msg_map;
	dst->frame_id = src->frame_id;
	dst->skip_frame = src->skip_frame;
}

static int ipu7_fw_isys_stream_open(struct ipu6_isys *isys,
				    const unsigned int stream_handle,
				    struct isys_fw_msgs *msg)
{
	if (IS_IPU8(isys->adev->isp)) {
		struct ipu7_fw_isys_stream_cfg cfg = msg->ipu7.stream;

		isys_stream_cfg_to_ipu8(&msg->ipu7.stream_ipu8, &cfg);

		return ipu7_fw_isys_send_cmd(isys, stream_handle,
					     &msg->ipu7.stream_ipu8,
					     msg->dma_addr,
					     sizeof(msg->ipu7.stream_ipu8),
					     IPU7_INSYS_SEND_TYPE_STREAM_OPEN);
	}

	return ipu7_fw_isys_send_cmd(isys, stream_handle, &msg->ipu7.stream,
				     msg->dma_addr, sizeof(msg->ipu7.stream),
				     IPU7_INSYS_SEND_TYPE_STREAM_OPEN);
}

static int ipu7_fw_isys_stream_close(struct ipu6_isys *isys,
				     const unsigned int stream_handle)
{
	return ipu7_fw_isys_send_cmd(isys, stream_handle, NULL, 0, 0,
				     IPU7_INSYS_SEND_TYPE_STREAM_CLOSE);
}

static int ipu7_fw_isys_stream_flush(struct ipu6_isys *isys,
				     const unsigned int stream_handle)
{
	return ipu7_fw_isys_send_cmd(isys, stream_handle, NULL, 0, 0,
				     IPU7_INSYS_SEND_TYPE_STREAM_FLUSH);
}

static int ipu7_fw_isys_send_frame_buff_set(struct ipu6_isys *isys,
					    const unsigned int stream_handle,
					    struct isys_fw_msgs *msg, u16 send_type)
{
	if (IS_IPU8(isys->adev->isp)) {
		struct ipu7_fw_isys_frame_buff_set set = msg->ipu7.frame;

		isys_buffset_to_ipu8(&msg->ipu7.frame_ipu8, &set);

		return ipu7_fw_isys_send_cmd(isys, stream_handle,
					     &msg->ipu7.frame_ipu8,
					     msg->dma_addr,
					     sizeof(msg->ipu7.frame_ipu8),
					     send_type);
	}

	return ipu7_fw_isys_send_cmd(isys, stream_handle, &msg->ipu7.frame,
				     msg->dma_addr, sizeof(msg->ipu7.frame),
				     send_type);
}

static int ipu7_fw_isys_stream_start(struct ipu6_isys *isys,
				     const unsigned int stream_handle,
				     struct isys_fw_msgs *msg, bool capture)
{
	return ipu7_fw_isys_send_frame_buff_set(isys, stream_handle, msg,
						 IPU7_INSYS_SEND_TYPE_STREAM_START_AND_CAPTURE);
}

static int ipu7_fw_isys_stream_capture(struct ipu6_isys *isys,
				       const unsigned int stream_handle,
				       struct isys_fw_msgs *msg)
{
	return ipu7_fw_isys_send_frame_buff_set(isys, stream_handle, msg,
						 IPU7_INSYS_SEND_TYPE_STREAM_CAPTURE);
}

const struct ipu6_fw_isys_ops ipu7_fw_isys_ops = {
	.init = ipu7_fw_isys_init,
	.close = ipu7_fw_isys_close,
	.send_cmd = ipu7_fw_isys_send_cmd,
	.cleanup = ipu7_fw_isys_cleanup,
	.prepare_stream_cfg = ipu7_fw_isys_prepare_stream_cfg,
	.prepare_buf_set = ipu7_fw_isys_prepare_buf_set,
	.stream_open = ipu7_fw_isys_stream_open,
	.stream_start = ipu7_fw_isys_stream_start,
	.stream_capture = ipu7_fw_isys_stream_capture,
	.stream_flush = ipu7_fw_isys_stream_flush,
	.stream_close = ipu7_fw_isys_stream_close,
	.dump_stream_cfg = ipu7_fw_isys_dump_stream_cfg,
	.dump_frame_buf_set = ipu7_fw_isys_dump_frame_buf_set,
};

static const struct ipu7_csi2_error {
	const char *error_string;
	bool is_info_only;
} dphy_rx_errors[] = {
	{ "Error handler FIFO full", false },
	{ "Reserved Short Packet encoding detected", true },
	{ "Reserved Long Packet encoding detected", true },
	{ "Received packet is too short", false},
	{ "Received packet is too long", false},
	{ "Short packet discarded due to errors", false },
	{ "Long packet discarded due to errors", false },
	{ "CSI Combo Rx interrupt", false },
	{ "IDI CDC FIFO overflow(remaining bits are reserved as 0)", false },
	{ "Received NULL packet", true },
	{ "Received blanking packet", true },
	{ "Tie to 0", true },
};

static void ipu7_isys_register_errors(struct ipu6_isys_csi2 *csi2)
{
	u32 offset = IPU7_IS_IO_CSI2_ERR_LEGACY_IRQ_CTL_BASE(csi2->port);
	u32 status = readl(csi2->base + offset + IPU7_IRQ_CTL_STATUS);
	u32 mask = IPU7_CSI_RX_ERROR_IRQ_MASK;

	if (!status)
		return;

	dev_dbg(&csi2->isys->adev->auxdev.dev, "csi2-%u error status 0x%08x\n",
		csi2->port, status);

	writel(status & mask, csi2->base + offset + IPU7_IRQ_CTL_CLEAR);
	csi2->receiver_errors |= status & mask;
}

static void ipu7_isys_csi2_error(struct ipu6_isys_csi2 *csi2)
{
	u32 status;

	/* Register errors once more in case of error interrupts are disabled */
	ipu7_isys_register_errors(csi2);
	status = csi2->receiver_errors;
	csi2->receiver_errors = 0;

	for (unsigned int i = 0; i < ARRAY_SIZE(dphy_rx_errors); i++) {
		if (status & BIT(i))
			dev_err_ratelimited(&csi2->isys->adev->auxdev.dev,
					    "csi2-%i error: %s\n",
					    csi2->port,
					    dphy_rx_errors[i].error_string);
	}
}

static const struct resp_to_msg {
	enum ipu7_insys_resp_type type;
	const char *msg;
} is_fw_msg[] = {
	{ IPU7_INSYS_RESP_TYPE_STREAM_OPEN_DONE, "STREAM_OPEN_DONE" },
	{ IPU7_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_ACK,
	 "STREAM_START_AND_CAPTURE_ACK" },
	{ IPU7_INSYS_RESP_TYPE_STREAM_CAPTURE_ACK, "STREAM_CAPTURE_ACK" },
	{ IPU7_INSYS_RESP_TYPE_STREAM_ABORT_ACK, "STREAM_ABORT_ACK" },
	{ IPU7_INSYS_RESP_TYPE_STREAM_FLUSH_ACK, "STREAM_FLUSH_ACK" },
	{ IPU7_INSYS_RESP_TYPE_STREAM_CLOSE_ACK, "STREAM_CLOSE_ACK" },
	{ IPU7_INSYS_RESP_TYPE_PIN_DATA_READY, "PIN_DATA_READY" },
	{ IPU7_INSYS_RESP_TYPE_FRAME_SOF, "FRAME_SOF" },
	{ IPU7_INSYS_RESP_TYPE_FRAME_EOF, "FRAME_EOF" },
	{ IPU7_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_DONE,
	 "STREAM_START_AND_CAPTURE_DONE" },
	{ IPU7_INSYS_RESP_TYPE_STREAM_CAPTURE_DONE, "STREAM_CAPTURE_DONE" },
	{ N_IPU7_INSYS_RESP_TYPE, "N_IPU7_INSYS_RESP_TYPE" },
};

static int ipu7_isys_isr_one(struct ipu6_bus_device *adev)
{
	struct ipu6_isys *isys = ipu6_bus_get_drvdata(adev);
	struct ipu6_isys_stream *stream = NULL;
	struct device *dev = &adev->auxdev.dev;
	struct ipu6_isys_csi2 *csi2 = NULL;
	struct ipu7_fw_isys_msg_err err_info;
	struct isys_fw_msgs *isys_fw_msg;
	struct ipu7_insys_resp *resp;
	unsigned long flags;
	u64 ts;

	resp = ipu7_fw_isys_get_resp(isys);
	if (!resp)
		return 1;

	if (resp->type >= N_IPU7_INSYS_RESP_TYPE) {
		dev_err(dev, "Unknown response type %u stream %u\n",
			resp->type, resp->stream_id);
		ipu7_fw_isys_put_resp(isys);
		return 1;
	}

	err_info = resp->error_info;
	ts = ((u64)resp->timestamp[1] << 32) | resp->timestamp[0];

	if (err_info.err_group == INSYS_MSG_ERR_GROUP_CAPTURE &&
	    err_info.err_code == INSYS_MSG_ERR_CAPTURE_SYNC_FRAME_DROP) {
		/* receive a sp w/o command, firmware drop it */
		dev_dbg(dev, "FRAME DROP: %02u %s stream %u\n",
			resp->type, is_fw_msg[resp->type].msg,
			resp->stream_id);
		dev_dbg(dev, "\tpin %u buf_id %llx frame %u\n",
			resp->pin_id, resp->buf_id, resp->frame_id);
		dev_dbg(dev, "\terror group %u code %u details [%u %u]\n",
			err_info.err_group, err_info.err_code,
			err_info.err_detail[0], err_info.err_detail[1]);
	} else if (err_info.err_code) {
		dev_err(dev, "%02u %s stream %u pin %u buf_id %llx frame %u\n",
			resp->type, is_fw_msg[resp->type].msg, resp->stream_id,
			resp->pin_id, resp->buf_id, resp->frame_id);
		dev_err(dev, "\terror group %u code %u details [%u %u]\n",
			err_info.err_group, err_info.err_code,
			err_info.err_detail[0], err_info.err_detail[1]);
	} else {
		dev_dbg(dev, "%02u %s stream %u pin %u buf_id %llx frame %u\n",
			resp->type, is_fw_msg[resp->type].msg, resp->stream_id,
			resp->pin_id, resp->buf_id, resp->frame_id);
		dev_dbg(dev, "\tts %llu\n", ts);
	}

	if (resp->stream_id >= IPU7_ISYS_MAX_STREAMS) {
		dev_err(dev, "bad stream handle %u\n",
			resp->stream_id);
		goto leave_nounlock;
	}

	spin_lock_irqsave(&isys->streams_lock, flags);

	stream = resp->stream_id < IPU6_ISYS_MAX_STREAMS ?
		isys->streams_by_handle[resp->stream_id] : NULL;
	if (!stream) {
		dev_err(dev, "stream of stream_handle %u is unused\n",
			resp->stream_id);
		goto leave;
	}

	stream->error = err_info.err_code;

	if (stream->asd)
		csi2 = ipu6_isys_subdev_to_csi2(stream->asd);

	switch (resp->type) {
	case IPU7_INSYS_RESP_TYPE_STREAM_OPEN_DONE:
		complete(&stream->stream_open_completion);
		break;
	case IPU7_INSYS_RESP_TYPE_STREAM_CLOSE_ACK:
		complete(&stream->stream_close_completion);
		break;
	case IPU7_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_ACK:
		complete(&stream->stream_start_completion);
		break;
	case IPU7_INSYS_RESP_TYPE_STREAM_ABORT_ACK:
		complete(&stream->stream_stop_completion);
		break;
	case IPU7_INSYS_RESP_TYPE_STREAM_FLUSH_ACK:
		complete(&stream->stream_stop_completion);
		break;
	case IPU7_INSYS_RESP_TYPE_PIN_DATA_READY:
		/*
		 * firmware only release the capture msg until software
		 * get pin_data_ready event
		 */
		isys_fw_msg = container_of((void *)(uintptr_t)resp->buf_id,
					   struct isys_fw_msgs, dummy);

		ipu6_put_fw_msg_buf(ipu6_bus_get_drvdata(adev), isys_fw_msg);
		if (resp->pin_id < IPU6_ISYS_OUTPUT_PINS &&
		    stream->output_pins_queue[resp->pin_id])
			ipu6_stream_buf_ready(stream, resp->pin_id,
					      resp->pin.addr, ts, 0);
		else
			dev_err(dev, "No handler for pin %u ready\n",
				resp->pin_id);
		if (csi2)
			ipu7_isys_csi2_error(csi2);

		break;
	case IPU7_INSYS_RESP_TYPE_STREAM_CAPTURE_ACK:
		break;
	case IPU7_INSYS_RESP_TYPE_STREAM_START_AND_CAPTURE_DONE:
	case IPU7_INSYS_RESP_TYPE_STREAM_CAPTURE_DONE:
		break;
	case IPU7_INSYS_RESP_TYPE_FRAME_SOF:
		if (csi2)
			ipu6_isys_csi2_sof_event_by_stream(stream);

		stream->seq[stream->seq_index].sequence =
			atomic_read(&stream->sequence) - 1U;
		stream->seq[stream->seq_index].timestamp = ts;
		dev_dbg(dev,
			"SOF: stream %u frame %u (index %u), ts 0x%16.16llx\n",
			resp->stream_id, resp->frame_id,
			stream->seq[stream->seq_index].sequence, ts);
		stream->seq_index = (stream->seq_index + 1U)
			% IPU6_ISYS_MAX_PARALLEL_SOF;
		break;
	case IPU7_INSYS_RESP_TYPE_FRAME_EOF:
		if (csi2)
			ipu6_isys_csi2_eof_event_by_stream(stream);

		dev_dbg(dev, "eof: stream %d(index %u) ts 0x%16.16llx\n",
			resp->stream_id,
			stream->seq[stream->seq_index].sequence, ts);
		break;
	default:
		dev_err(dev, "Unknown response type %u stream %u\n",
			resp->type, resp->stream_id);
		break;
	}

leave:
	spin_unlock_irqrestore(&isys->streams_lock, flags);

leave_nounlock:
	ipu7_fw_isys_put_resp(isys);

	return 0;
}

#define IPU7_NR_OF_CSI2_VC		16U
static void ipu7_isys_csi2_isr(struct ipu6_isys_csi2 *csi2)
{
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	struct ipu6_device *isp = csi2->isys->adev->isp;
	struct ipu6_isys_stream *s;
	u32 sync, offset;
	u32 fe = 0;
	u8 vc;

	ipu7_isys_register_errors(csi2);

	offset = IPU7_IS_IO_CSI2_SYNC_LEGACY_IRQ_CTL_BASE(csi2->port);
	sync = readl(csi2->base + offset + IPU7_IRQ_CTL_STATUS);
	writel(sync, csi2->base + offset + IPU7_IRQ_CTL_CLEAR);
	dev_dbg(dev, "csi2-%u sync status 0x%08x\n", csi2->port, sync);

	if (!IS_IPU7_MTL(isp)) {
		fe = readl(csi2->base + offset + IPU7_IRQ1_CTL_STATUS);
		writel(fe, csi2->base + offset + IPU7_IRQ1_CTL_CLEAR);
		dev_dbg(dev, "csi2-%u FE status 0x%08x\n", csi2->port, fe);
	}

	for (vc = 0; vc < IPU7_NR_OF_CSI2_VC && (sync || fe); vc++) {
		s = csi2->streams_by_vc[vc];
		if (!s)
			continue;

		if (!IS_IPU7_MTL(isp)) {
			if (sync & IPU7P5_CSI_RX_SYNC_FS_VC & (1U << vc))
				ipu6_isys_csi2_sof_event_by_stream(s);

			if (fe & IPU7P5_CSI_RX_SYNC_FE_VC & (1U << vc))
				ipu6_isys_csi2_eof_event_by_stream(s);
		} else {
			if (sync & IPU7_CSI_RX_SYNC_FS_VC & (1U << (vc * 2)))
				ipu6_isys_csi2_sof_event_by_stream(s);

			if (sync & IPU7_CSI_RX_SYNC_FE_VC & (2U << (vc * 2)))
				ipu6_isys_csi2_eof_event_by_stream(s);
		}
	}
}

static void ipu7_dispatch_csi2_isr(struct ipu6_isys *isys, u32 status)
{
	for (unsigned int i = 0; i < isys->pdata->ipdata->csi2.nports; i++) {
		if (!isys->csi2[i].base)
			continue;
		if (status & isys->csi2[i].legacy_irq_mask)
			ipu7_isys_csi2_isr(&isys->csi2[i]);
	}
}

irqreturn_t ipu7_isys_isr(struct ipu6_bus_device *adev)
{
	struct ipu6_isys *isys = ipu6_bus_get_drvdata(adev);
	void __iomem *base = isys->pdata->base;
	u32 status_sw, status_csi;
	u32 csi_offset, sw_offset;
	int pm_status;

	pm_status = pm_runtime_get_if_active(&adev->auxdev.dev);
	if (!pm_status)
		return 0;

	csi_offset = IPU7_IS_IO_CSI2_LEGACY_IRQ_CTRL_BASE;
	sw_offset = IPU7_IS_UC_CTRL_BASE;

	status_csi = readl(base + csi_offset + IPU7_IRQ_CTL_STATUS);
	status_sw = readl(base + sw_offset + IPU7_TO_SW_IRQ_CNTL_STATUS);

	if (!status_csi && !status_sw) {
		if (pm_status > 0)
			pm_runtime_put(&adev->auxdev.dev);
		return IRQ_NONE;
	}

	do {
		writel(status_sw, base + sw_offset + IPU7_TO_SW_IRQ_CNTL_CLEAR);
		writel(status_csi, base + csi_offset + IPU7_IRQ_CTL_CLEAR);

		if (isys->isr_csi2_bits & status_csi)
			ipu7_dispatch_csi2_isr(isys, status_csi);

		if (!ipu7_isys_isr_one(adev))
			status_sw = IPU7_TO_SW_IRQ_FW;
		else
			status_sw = 0;

		status_csi = readl(base + csi_offset + IPU7_IRQ_CTL_STATUS);
		status_sw |= readl(base + sw_offset +
				   IPU7_TO_SW_IRQ_CNTL_STATUS);
	} while ((status_csi & isys->isr_csi2_bits) ||
		 (status_sw & IPU7_TO_SW_IRQ_FW));

	writel(IPU7_IS_UC_TO_SW_IRQ_MASK,
	       base + sw_offset + IPU7_TO_SW_IRQ_CNTL_MASK_N);

	if (pm_status > 0)
		pm_runtime_put(&adev->auxdev.dev);

	return IRQ_HANDLED;
}
