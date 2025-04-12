// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0+

/*
 * Ioctls implementations for the virtio-media driver.
 *
 * Copyright (c) 2024-2025 Google LLC.
 */

#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/virtio_config.h>
#include <linux/vmalloc.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "scatterlist_builder.h"
#include "virtio_media.h"

/**
 * virtio_media_send_r_ioctl() - Send a read-only ioctl to the device.
 * @fh: file handler of the session doing the ioctl.
 * @ioctl: ``VIDIOC_*`` ioctl code.
 * @ioctl_data: pointer to the ioctl payload.
 * @ioctl_data_len: length in bytes of the ioctl payload.
 *
 * Send an ioctl that has no driver payload, but expects a response from the
 * host (i.e. an ioctl specified with ``_IOR``).
 */
static int virtio_media_send_r_ioctl(struct v4l2_fh *fh, u32 ioctl,
				     void *ioctl_data, size_t ioctl_data_len)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	struct scatterlist *sgs[3];
	struct scatterlist_builder builder = {
		.descs = session->command_sgs.sgl,
		.num_descs = DESC_CHAIN_MAX_LEN,
		.cur_desc = 0,
		.shadow_buffer = session->shadow_buf,
		.shadow_buffer_size = VIRTIO_SHADOW_BUF_SIZE,
		.shadow_buffer_pos = 0,
		.sgs = sgs,
		.num_sgs = ARRAY_SIZE(sgs),
		.cur_sg = 0,
	};
	int ret;

	/* Command descriptor */
	ret = scatterlist_builder_add_ioctl_cmd(&builder, session, ioctl);
	if (ret)
		return ret;

	/* Response descriptor */
	ret = scatterlist_builder_add_ioctl_resp(&builder, session);
	if (ret)
		return ret;

	/* Response payload */
	ret = scatterlist_builder_add_data(&builder, ioctl_data,
					   ioctl_data_len);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to prepare command descriptor chain\n");
		return ret;
	}

	ret = virtio_media_send_command(
		vv, sgs, 1, 2,
		sizeof(struct virtio_media_resp_ioctl) + ioctl_data_len, NULL);
	if (ret < 0)
		return ret;

	ret = scatterlist_builder_retrieve_data(&builder, 2, ioctl_data);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to retrieve response descriptor chain\n");
		return ret;
	}

	return 0;
}

/**
 * virtio_media_send_w_ioctl() - Send a write-only ioctl to the device.
 * @fh: file handler of the session doing the ioctl.
 * @ioctl: ``VIDIOC_*`` ioctl code.
 * @ioctl_data: pointer to the ioctl payload.
 * @ioctl_data_len: length in bytes of the ioctl payload.
 *
 * Send an ioctl that does not expect a reply beyond an error status (i.e. an
 * ioctl specified with ``_IOW``) to the host.
 */
static int virtio_media_send_w_ioctl(struct v4l2_fh *fh, u32 ioctl,
				     const void *ioctl_data,
				     size_t ioctl_data_len)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	struct scatterlist *sgs[3];
	struct scatterlist_builder builder = {
		.descs = session->command_sgs.sgl,
		.num_descs = DESC_CHAIN_MAX_LEN,
		.cur_desc = 0,
		.shadow_buffer = session->shadow_buf,
		.shadow_buffer_size = VIRTIO_SHADOW_BUF_SIZE,
		.shadow_buffer_pos = 0,
		.sgs = sgs,
		.num_sgs = ARRAY_SIZE(sgs),
		.cur_sg = 0,
	};
	int ret;

	/* Command descriptor */
	ret = scatterlist_builder_add_ioctl_cmd(&builder, session, ioctl);
	if (ret)
		return ret;

	/* Command payload */
	ret = scatterlist_builder_add_data(&builder, (void *)ioctl_data,
					   ioctl_data_len);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to prepare command descriptor chain\n");
		return ret;
	}

	/* Response descriptor */
	ret = scatterlist_builder_add_ioctl_resp(&builder, session);
	if (ret)
		return ret;

	ret = virtio_media_send_command(
		vv, sgs, 2, 1, sizeof(struct virtio_media_resp_ioctl), NULL);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * virtio_media_send_wr_ioctl() - Send a read-write ioctl to the device.
 * @fh: file handler of the session doing the ioctl.
 * @ioctl: ``VIDIOC_*`` ioctl code.
 * @ioctl_data: pointer to the ioctl payload.
 * @ioctl_data_len: length in bytes of the ioctl payload.
 * @minimum_resp_payload: minimum expected length of the response's payload.
 *
 * Sends an ioctl that expects a response of exactly the same size as the
 * input (i.e. an ioctl specified with ``_IOWR``) to the host.
 *
 * This corresponds to what most V4L2 ioctls do. For instance
 * ``VIDIOC_ENUM_FMT`` takes a partially-initialized ``struct v4l2_fmtdesc``
 * and returns its filled version.
 */
static int virtio_media_send_wr_ioctl(struct v4l2_fh *fh, u32 ioctl,
				      void *ioctl_data, size_t ioctl_data_len,
				      size_t minimum_resp_payload)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	struct scatterlist *sgs[4];
	struct scatterlist_builder builder = {
		.descs = session->command_sgs.sgl,
		.num_descs = DESC_CHAIN_MAX_LEN,
		.cur_desc = 0,
		.shadow_buffer = session->shadow_buf,
		.shadow_buffer_size = VIRTIO_SHADOW_BUF_SIZE,
		.shadow_buffer_pos = 0,
		.sgs = sgs,
		.num_sgs = ARRAY_SIZE(sgs),
		.cur_sg = 0,
	};
	int ret;

	/* Command descriptor */
	ret = scatterlist_builder_add_ioctl_cmd(&builder, session, ioctl);
	if (ret)
		return ret;

	/* Command payload */
	ret = scatterlist_builder_add_data(&builder, ioctl_data,
					   ioctl_data_len);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to prepare command descriptor chain\n");
		return ret;
	}

	/* Response descriptor */
	ret = scatterlist_builder_add_ioctl_resp(&builder, session);
	if (ret)
		return ret;

	/* Response payload, same as command */
	ret = scatterlist_builder_add_descriptor(&builder, 1);
	if (ret)
		return ret;

	ret = virtio_media_send_command(vv, sgs, 2, 2,
					sizeof(struct virtio_media_resp_ioctl) +
						minimum_resp_payload,
					NULL);
	if (ret < 0)
		return ret;

	ret = scatterlist_builder_retrieve_data(&builder, 3, ioctl_data);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to retrieve response descriptor chain\n");
		return ret;
	}

	return 0;
}

/**
 * virtio_media_send_buffer_ioctl() - Send an ioctl taking a buffer as
 * parameter to the device.
 * @fh: file handler of the session doing the ioctl.
 * @ioctl: ``VIDIOC_*`` ioctl code.
 * @b: ``v4l2_buffer`` to be sent as the ioctl payload.
 *
 * Buffers can require an additional descriptor to send their planes array, and
 * can have pointers to userspace memory hence this dedicated function.
 */
static int virtio_media_send_buffer_ioctl(struct v4l2_fh *fh, u32 ioctl,
					  struct v4l2_buffer *b)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	struct v4l2_plane *orig_planes = NULL;
	struct scatterlist *sgs[64];
	/* End of the device-readable buffer SGs, to reuse in device-writable section. */
	size_t num_cmd_sgs;
	size_t end_buf_sg;
	struct scatterlist_builder builder = {
		.descs = session->command_sgs.sgl,
		.num_descs = DESC_CHAIN_MAX_LEN,
		.cur_desc = 0,
		.shadow_buffer = session->shadow_buf,
		.shadow_buffer_size = VIRTIO_SHADOW_BUF_SIZE,
		.shadow_buffer_pos = 0,
		.sgs = sgs,
		.num_sgs = ARRAY_SIZE(sgs),
		.cur_sg = 0,
	};
	size_t resp_len;
	int ret;
	int i;

	if (b->type > VIRTIO_MEDIA_LAST_QUEUE)
		return -EINVAL;

	if (V4L2_TYPE_IS_MULTIPLANAR(b->type))
		orig_planes = b->m.planes;

	/* Command descriptor */
	ret = scatterlist_builder_add_ioctl_cmd(&builder, session, ioctl);
	if (ret)
		return ret;

	/* Command payload (struct v4l2_buffer) */
	ret = scatterlist_builder_add_buffer(&builder, b);
	if (ret < 0)
		return ret;

	end_buf_sg = builder.cur_sg;

	/* Payload of SHARED_PAGES buffers, if relevant */
	ret = scatterlist_builder_add_buffer_userptr(&builder, b);
	if (ret < 0)
		return ret;

	num_cmd_sgs = builder.cur_sg;

	/* Response descriptor */
	ret = scatterlist_builder_add_ioctl_resp(&builder, session);
	if (ret)
		return ret;

	/* Response payload (same as input, but no userptr mapping) */
	for (i = 1; i < end_buf_sg; i++) {
		ret = scatterlist_builder_add_descriptor(&builder, i);
		if (ret < 0)
			return ret;
	}

	ret = virtio_media_send_command(
		vv, builder.sgs, num_cmd_sgs, builder.cur_sg - num_cmd_sgs,
		sizeof(struct virtio_media_resp_ioctl) + sizeof(*b), &resp_len);
	if (ret < 0)
		return ret;

	resp_len -= sizeof(struct virtio_media_resp_ioctl);

	/* Make sure that the reply length covers our v4l2_buffer */
	if (resp_len < sizeof(*b))
		return -EINVAL;

	ret = scatterlist_builder_retrieve_buffer(&builder, num_cmd_sgs + 1, b,
						  orig_planes);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to retrieve response descriptor chain\n");
		return ret;
	}

	return 0;
}

/**
 * virtio_media_send_ext_controls_ioctl() - Send an ioctl taking extended
 * controls as parameters to the device.
 * @fh: file handler of the session doing the ioctl.
 * @ioctl: ``VIDIOC_*`` ioctl code.
 * @ctrls: ``v4l2_ext_controls`` to be sent as the ioctl payload.
 *
 * Queues an ioctl that sends a ``v4l2_ext_controls`` to the host and receives
 * an updated version.
 *
 * ``v4l2_ext_controls`` has a pointer to an array of ``v4l2_ext_control``, and
 * also potentially pointers to user-space memory that we need to map properly,
 * hence the dedicated function.
 */
static int virtio_media_send_ext_controls_ioctl(struct v4l2_fh *fh, u32 ioctl,
						struct v4l2_ext_controls *ctrls)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	size_t num_cmd_sgs;
	size_t end_ctrls_sg;
	struct v4l2_ext_control *controls_backup = ctrls->controls;
	const u32 num_ctrls = ctrls->count;
	struct scatterlist *sgs[64];
	struct scatterlist_builder builder = {
		.descs = session->command_sgs.sgl,
		.num_descs = DESC_CHAIN_MAX_LEN,
		.cur_desc = 0,
		.shadow_buffer = session->shadow_buf,
		.shadow_buffer_size = VIRTIO_SHADOW_BUF_SIZE,
		.shadow_buffer_pos = 0,
		.sgs = sgs,
		.num_sgs = ARRAY_SIZE(sgs),
		.cur_sg = 0,
	};
	size_t resp_len = 0;
	int ret;
	int i;

	/* Command descriptor */
	ret = scatterlist_builder_add_ioctl_cmd(&builder, session, ioctl);
	if (ret)
		return ret;

	/* v4l2_controls */
	ret = scatterlist_builder_add_ext_ctrls(&builder, ctrls);
	if (ret)
		return ret;

	end_ctrls_sg = builder.cur_sg;

	ret = scatterlist_builder_add_ext_ctrls_userptrs(&builder, ctrls);
	if (ret)
		return ret;

	num_cmd_sgs = builder.cur_sg;

	/* Response descriptor */
	ret = scatterlist_builder_add_ioctl_resp(&builder, session);
	if (ret)
		return ret;

	/* Response payload (same as input but without userptrs) */
	for (i = 1; i < end_ctrls_sg; i++) {
		ret = scatterlist_builder_add_descriptor(&builder, i);
		if (ret < 0)
			return ret;
	}

	ret = virtio_media_send_command(
		vv, builder.sgs, num_cmd_sgs, builder.cur_sg - num_cmd_sgs,
		sizeof(struct virtio_media_resp_ioctl) + sizeof(*ctrls),
		&resp_len);

	/* Just in case the host touched these. */
	ctrls->controls = controls_backup;
	if (ctrls->count != num_ctrls) {
		v4l2_err(
			&vv->v4l2_dev,
			"device returned a number of controls different than the one submitted\n");
	}
	if (ctrls->count > num_ctrls)
		return -ENOSPC;

	/* Event if we have received an error, we may need to read our payload back */
	if (ret < 0 && resp_len >= sizeof(struct virtio_media_resp_ioctl) +
					   sizeof(*ctrls)) {
		/* Deliberately ignore the error here as we want to return the previous one */
		scatterlist_builder_retrieve_ext_ctrls(&builder,
						       num_cmd_sgs + 1, ctrls);
		return ret;
	}

	resp_len -= sizeof(struct virtio_media_resp_ioctl);

	/* Make sure that the reply's length covers our v4l2_ext_controls */
	if (resp_len < sizeof(*ctrls))
		return -EINVAL;

	ret = scatterlist_builder_retrieve_ext_ctrls(&builder, num_cmd_sgs + 1,
						     ctrls);
	if (ret)
		return ret;

	return 0;
}

/**
 * virtio_media_clear_queue() - clear all pending buffers on a streamed-off queue.
 * @session: session which the queue to clear belongs to.
 * @queue: state of the queue to clear.
 *
 * Helper function to clear the list of buffers waiting to be dequeued on a
 * queue that has just been streamed off.
 */
static void virtio_media_clear_queue(struct virtio_media_session *session,
				     struct virtio_media_queue_state *queue)
{
	struct list_head *p, *n;
	int i;

	mutex_lock(&session->queues_lock);

	list_for_each_safe(p, n, &queue->pending_dqbufs) {
		struct virtio_media_buffer *dqbuf =
			list_entry(p, struct virtio_media_buffer, list);

		list_del(&dqbuf->list);
	}

	/* All buffers are now dequeued. */
	for (i = 0; i < queue->allocated_bufs; i++)
		queue->buffers[i].buffer.flags = 0;

	queue->queued_bufs = 0;
	queue->streaming = false;
	queue->is_capture_last = false;

	mutex_unlock(&session->queues_lock);
}

/*
 * Macros suitable for defining ioctls with a constant size payload.
 */

#define SIMPLE_WR_IOCTL(name, ioctl, payload_t)                       \
	static int virtio_media_##name(struct file *file, void *fh,   \
				       payload_t *payload)            \
	{                                                             \
		return virtio_media_send_wr_ioctl(fh, ioctl, payload, \
						  sizeof(*payload),   \
						  sizeof(*payload));  \
	}
#define SIMPLE_R_IOCTL(name, ioctl, payload_t)                       \
	static int virtio_media_##name(struct file *file, void *fh,  \
				       payload_t *payload)           \
	{                                                            \
		return virtio_media_send_r_ioctl(fh, ioctl, payload, \
						 sizeof(*payload));  \
	}
#define SIMPLE_W_IOCTL(name, ioctl, payload_t)                       \
	static int virtio_media_##name(struct file *file, void *fh,  \
				       payload_t *payload)           \
	{                                                            \
		return virtio_media_send_w_ioctl(fh, ioctl, payload, \
						 sizeof(*payload));  \
	}

/*
 * V4L2 ioctl handlers.
 *
 * Most of these functions just forward the ioctl to the host, for these we can
 * use one of the SIMPLE_*_IOCTL macros. Exceptions that have their own
 * standalone function follow.
 */

SIMPLE_WR_IOCTL(enum_fmt, VIDIOC_ENUM_FMT, struct v4l2_fmtdesc)
SIMPLE_WR_IOCTL(g_fmt, VIDIOC_G_FMT, struct v4l2_format)
SIMPLE_WR_IOCTL(s_fmt, VIDIOC_S_FMT, struct v4l2_format)
SIMPLE_WR_IOCTL(try_fmt, VIDIOC_TRY_FMT, struct v4l2_format)
SIMPLE_WR_IOCTL(enum_framesizes, VIDIOC_ENUM_FRAMESIZES,
		struct v4l2_frmsizeenum)
SIMPLE_WR_IOCTL(enum_frameintervals, VIDIOC_ENUM_FRAMEINTERVALS,
		struct v4l2_frmivalenum)
SIMPLE_WR_IOCTL(query_ext_ctrl, VIDIOC_QUERY_EXT_CTRL,
		struct v4l2_query_ext_ctrl)
SIMPLE_WR_IOCTL(s_dv_timings, VIDIOC_S_DV_TIMINGS, struct v4l2_dv_timings)
SIMPLE_WR_IOCTL(g_dv_timings, VIDIOC_G_DV_TIMINGS, struct v4l2_dv_timings)
SIMPLE_R_IOCTL(query_dv_timings, VIDIOC_QUERY_DV_TIMINGS,
	       struct v4l2_dv_timings)
SIMPLE_WR_IOCTL(enum_dv_timings, VIDIOC_ENUM_DV_TIMINGS,
		struct v4l2_enum_dv_timings)
SIMPLE_WR_IOCTL(dv_timings_cap, VIDIOC_DV_TIMINGS_CAP,
		struct v4l2_dv_timings_cap)
SIMPLE_WR_IOCTL(enuminput, VIDIOC_ENUMINPUT, struct v4l2_input)
SIMPLE_WR_IOCTL(querymenu, VIDIOC_QUERYMENU, struct v4l2_querymenu)
SIMPLE_WR_IOCTL(enumoutput, VIDIOC_ENUMOUTPUT, struct v4l2_output)
SIMPLE_WR_IOCTL(enumaudio, VIDIOC_ENUMAUDIO, struct v4l2_audio)
SIMPLE_R_IOCTL(g_audio, VIDIOC_G_AUDIO, struct v4l2_audio)
SIMPLE_W_IOCTL(s_audio, VIDIOC_S_AUDIO, const struct v4l2_audio)
SIMPLE_WR_IOCTL(enumaudout, VIDIOC_ENUMAUDOUT, struct v4l2_audioout)
SIMPLE_R_IOCTL(g_audout, VIDIOC_G_AUDOUT, struct v4l2_audioout)
SIMPLE_W_IOCTL(s_audout, VIDIOC_S_AUDOUT, const struct v4l2_audioout)
SIMPLE_WR_IOCTL(g_modulator, VIDIOC_G_MODULATOR, struct v4l2_modulator)
SIMPLE_W_IOCTL(s_modulator, VIDIOC_S_MODULATOR, const struct v4l2_modulator)
SIMPLE_WR_IOCTL(g_selection, VIDIOC_G_SELECTION, struct v4l2_selection)
SIMPLE_WR_IOCTL(s_selection, VIDIOC_S_SELECTION, struct v4l2_selection)
SIMPLE_R_IOCTL(g_enc_index, VIDIOC_G_ENC_INDEX, struct v4l2_enc_idx)
SIMPLE_WR_IOCTL(encoder_cmd, VIDIOC_ENCODER_CMD, struct v4l2_encoder_cmd)
SIMPLE_WR_IOCTL(try_encoder_cmd, VIDIOC_TRY_ENCODER_CMD,
		struct v4l2_encoder_cmd)
SIMPLE_WR_IOCTL(try_decoder_cmd, VIDIOC_TRY_DECODER_CMD,
		struct v4l2_decoder_cmd)
SIMPLE_WR_IOCTL(g_parm, VIDIOC_G_PARM, struct v4l2_streamparm)
SIMPLE_WR_IOCTL(s_parm, VIDIOC_S_PARM, struct v4l2_streamparm)
SIMPLE_R_IOCTL(g_std, VIDIOC_G_STD, v4l2_std_id)
SIMPLE_R_IOCTL(querystd, VIDIOC_QUERYSTD, v4l2_std_id)
SIMPLE_WR_IOCTL(enumstd, VIDIOC_ENUMSTD, struct v4l2_standard)
SIMPLE_WR_IOCTL(g_tuner, VIDIOC_G_TUNER, struct v4l2_tuner)
SIMPLE_W_IOCTL(s_tuner, VIDIOC_S_TUNER, const struct v4l2_tuner)
SIMPLE_WR_IOCTL(g_frequency, VIDIOC_G_FREQUENCY, struct v4l2_frequency)
SIMPLE_W_IOCTL(s_frequency, VIDIOC_S_FREQUENCY, const struct v4l2_frequency)
SIMPLE_WR_IOCTL(enum_freq_bands, VIDIOC_ENUM_FREQ_BANDS,
		struct v4l2_frequency_band)
SIMPLE_WR_IOCTL(g_sliced_vbi_cap, VIDIOC_G_SLICED_VBI_CAP,
		struct v4l2_sliced_vbi_cap)
SIMPLE_W_IOCTL(s_hw_freq_seek, VIDIOC_S_HW_FREQ_SEEK,
	       const struct v4l2_hw_freq_seek)

/*
 * QUERYCAP is handled by reading the configuration area.
 *
 */

static int virtio_media_querycap(struct file *file, void *fh,
				 struct v4l2_capability *cap)
{
	struct video_device *video_dev = video_devdata(file);
	struct virtio_media *vv = to_virtio_media(video_dev);

	strscpy(cap->bus_info, "platform:virtio-media");

	if (!virtio_media_driver_name)
		strscpy(cap->driver, VIRTIO_MEDIA_DEFAULT_DRIVER_NAME);
	else
		strscpy(cap->driver, virtio_media_driver_name);

	virtio_cread_bytes(vv->virtio_dev, 8, cap->card, sizeof(cap->card));

	cap->capabilities = video_dev->device_caps | V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = video_dev->device_caps;

	return 0;
}

/*
 * Extended control ioctls are handled mostly identically.
 */

static int virtio_media_g_ext_ctrls(struct file *file, void *fh,
				    struct v4l2_ext_controls *ctrls)
{
	return virtio_media_send_ext_controls_ioctl(fh, VIDIOC_G_EXT_CTRLS,
						    ctrls);
}

static int virtio_media_s_ext_ctrls(struct file *file, void *fh,
				    struct v4l2_ext_controls *ctrls)
{
	return virtio_media_send_ext_controls_ioctl(fh, VIDIOC_S_EXT_CTRLS,
						    ctrls);
}

static int virtio_media_try_ext_ctrls(struct file *file, void *fh,
				      struct v4l2_ext_controls *ctrls)
{
	return virtio_media_send_ext_controls_ioctl(fh, VIDIOC_TRY_EXT_CTRLS,
						    ctrls);
}

/*
 * Subscribe/unsubscribe from an event.
 */

static int
virtio_media_subscribe_event(struct v4l2_fh *fh,
			     const struct v4l2_event_subscription *sub)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	int ret;

	/* First subscribe to the event in the guest. */
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		ret = v4l2_src_change_event_subscribe(fh, sub);
		break;
	default:
		ret = v4l2_event_subscribe(fh, sub, 1, NULL);
		break;
	}
	if (ret)
		return ret;

	/* Then ask the host to signal us these events. */
	ret = virtio_media_send_w_ioctl(fh, VIDIOC_SUBSCRIBE_EVENT, sub,
					sizeof(*sub));
	if (ret < 0) {
		v4l2_event_unsubscribe(fh, sub);
		return ret;
	}

	/*
	 * Subscribing to an event may result in that event being signaled
	 * immediately. Process all pending events to make sure we don't miss it.
	 */
	if (sub->flags & V4L2_EVENT_SUB_FL_SEND_INITIAL)
		virtio_media_process_events(vv);

	return 0;
}

static int
virtio_media_unsubscribe_event(struct v4l2_fh *fh,
			       const struct v4l2_event_subscription *sub)
{
	int ret;

	ret = virtio_media_send_w_ioctl(fh, VIDIOC_UNSUBSCRIBE_EVENT, sub,
					sizeof(*sub));
	if (ret < 0)
		return ret;

	ret = v4l2_event_unsubscribe(fh, sub);
	if (ret)
		return ret;

	return 0;
}

/*
 * Streamon/off affect the local queue state.
 */

static int virtio_media_streamon(struct file *file, void *fh,
				 enum v4l2_buf_type i)
{
	struct virtio_media_session *session = fh_to_session(fh);
	int ret;

	if (i > VIRTIO_MEDIA_LAST_QUEUE)
		return -EINVAL;

	ret = virtio_media_send_w_ioctl(fh, VIDIOC_STREAMON, &i, sizeof(i));
	if (ret < 0)
		return ret;

	session->queues[i].streaming = true;

	return 0;
}

static int virtio_media_streamoff(struct file *file, void *fh,
				  enum v4l2_buf_type i)
{
	struct virtio_media_session *session = fh_to_session(fh);
	int ret;

	if (i > VIRTIO_MEDIA_LAST_QUEUE)
		return -EINVAL;

	ret = virtio_media_send_w_ioctl(fh, VIDIOC_STREAMOFF, &i, sizeof(i));
	if (ret < 0)
		return ret;

	virtio_media_clear_queue(session, &session->queues[i]);

	return 0;
}

/*
 * Buffer creation/queuing functions deal with the local driver state.
 */

static int virtio_media_reqbufs(struct file *file, void *fh,
				struct v4l2_requestbuffers *b)
{
	struct virtio_media_session *session = fh_to_session(fh);
	struct virtio_media_queue_state *queue;
	int ret;

	if (b->type > VIRTIO_MEDIA_LAST_QUEUE)
		return -EINVAL;

	if (b->memory == V4L2_MEMORY_USERPTR && !virtio_media_allow_userptr)
		return -EINVAL;

	ret = virtio_media_send_wr_ioctl(fh, VIDIOC_REQBUFS, b, sizeof(*b),
					 sizeof(*b));
	if (ret)
		return ret;

	queue = &session->queues[b->type];

	/* REQBUFS(0) is an implicit STREAMOFF. */
	if (b->count == 0)
		virtio_media_clear_queue(session, queue);

	vfree(queue->buffers);
	queue->buffers = NULL;

	if (b->count > 0) {
		queue->buffers =
			vzalloc(sizeof(struct virtio_media_buffer) * b->count);
		if (!queue->buffers)
			return -ENOMEM;
	}

	queue->allocated_bufs = b->count;

	/*
	 * If a multiplanar queue is successfully used here, this means
	 * we are using the multiplanar interface.
	 */
	if (V4L2_TYPE_IS_MULTIPLANAR(b->type))
		session->uses_mplane = true;

	if (!virtio_media_allow_userptr)
		b->capabilities &= ~V4L2_BUF_CAP_SUPPORTS_USERPTR;

	/* We do not support DMABUF yet. */
	b->capabilities &= ~V4L2_BUF_CAP_SUPPORTS_DMABUF;

	return 0;
}

static int virtio_media_querybuf(struct file *file, void *fh,
				 struct v4l2_buffer *b)
{
	struct virtio_media_session *session = fh_to_session(fh);
	struct virtio_media_queue_state *queue;
	struct virtio_media_buffer *buffer;
	int ret;

	ret = virtio_media_send_buffer_ioctl(fh, VIDIOC_QUERYBUF, b);
	if (ret)
		return ret;

	if (b->type > VIRTIO_MEDIA_LAST_QUEUE)
		return -EINVAL;

	queue = &session->queues[b->type];
	if (b->index >= queue->allocated_bufs)
		return -EINVAL;

	buffer = &queue->buffers[b->index];
	/* Set the DONE flag if the buffer is waiting in our own dequeue queue. */
	b->flags |= (buffer->buffer.flags & V4L2_BUF_FLAG_DONE);

	return 0;
}

static int virtio_media_create_bufs(struct file *file, void *fh,
				    struct v4l2_create_buffers *b)
{
	struct virtio_media_session *session = fh_to_session(fh);
	struct virtio_media_queue_state *queue;
	struct virtio_media_buffer *buffers;
	u32 type = b->format.type;
	int ret;

	if (type > VIRTIO_MEDIA_LAST_QUEUE)
		return -EINVAL;

	queue = &session->queues[type];

	ret = virtio_media_send_wr_ioctl(fh, VIDIOC_CREATE_BUFS, b, sizeof(*b),
					 sizeof(*b));
	if (ret)
		return ret;

	/* If count is zero, we were just checking for format. */
	if (b->count == 0)
		return 0;

	buffers = queue->buffers;

	queue->buffers =
		vzalloc(sizeof(*queue->buffers) * (b->index + b->count));
	if (!queue->buffers) {
		queue->buffers = buffers;
		return -ENOMEM;
	}

	memcpy(queue->buffers, buffers,
	       sizeof(*buffers) * queue->allocated_bufs);
	vfree(buffers);

	queue->allocated_bufs = b->index + b->count;

	return 0;
}

static int virtio_media_prepare_buf(struct file *file, void *fh,
				    struct v4l2_buffer *b)
{
	struct virtio_media_session *session = fh_to_session(fh);
	struct virtio_media_queue_state *queue;
	struct virtio_media_buffer *buffer;
	int i, ret;

	if (b->type > VIRTIO_MEDIA_LAST_QUEUE)
		return -EINVAL;
	queue = &session->queues[b->type];
	if (b->index >= queue->allocated_bufs)
		return -EINVAL;
	buffer = &queue->buffers[b->index];

	buffer->buffer.m = b->m;
	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		if (b->length > VIDEO_MAX_PLANES)
			return -EINVAL;
		for (i = 0; i < b->length; i++)
			buffer->planes[i].m = b->m.planes[i].m;
	}

	ret = virtio_media_send_buffer_ioctl(fh, VIDIOC_PREPARE_BUF, b);
	if (ret)
		return ret;

	buffer->buffer.flags = V4L2_BUF_FLAG_PREPARED;

	return 0;
}

static int virtio_media_qbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct virtio_media_session *session = fh_to_session(fh);
	struct virtio_media_queue_state *queue;
	struct virtio_media_buffer *buffer;
	bool prepared;
	u32 old_flags;
	int i, ret;

	if (b->type > VIRTIO_MEDIA_LAST_QUEUE)
		return -EINVAL;
	queue = &session->queues[b->type];
	if (b->index >= queue->allocated_bufs)
		return -EINVAL;
	buffer = &queue->buffers[b->index];
	prepared = buffer->buffer.flags & V4L2_BUF_FLAG_PREPARED;

	/*
	 * Store the buffer and plane `m` information so we can retrieve it again
	 * when DQBUF occurs.
	 */
	if (!prepared) {
		buffer->buffer.m = b->m;
		if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
			if (b->length > VIDEO_MAX_PLANES)
				return -EINVAL;
			for (i = 0; i < b->length; i++)
				buffer->planes[i].m = b->m.planes[i].m;
		}
	}
	old_flags = buffer->buffer.flags;
	buffer->buffer.flags = V4L2_BUF_FLAG_QUEUED;

	ret = virtio_media_send_buffer_ioctl(fh, VIDIOC_QBUF, b);
	if (ret) {
		/* Rollback the previous flags as the buffer is not queued. */
		buffer->buffer.flags = old_flags;
		return ret;
	}

	queue->queued_bufs += 1;

	return 0;
}

static int virtio_media_dqbuf(struct file *file, void *fh,
			      struct v4l2_buffer *b)
{
	struct video_device *video_dev = video_devdata(file);
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session =
		fh_to_session(file->private_data);
	struct virtio_media_buffer *dqbuf;
	struct virtio_media_queue_state *queue;
	struct list_head *buffer_queue;
	struct v4l2_plane *planes_backup = NULL;
	const bool is_multiplanar = V4L2_TYPE_IS_MULTIPLANAR(b->type);
	int ret;

	if (b->type > VIRTIO_MEDIA_LAST_QUEUE)
		return -EINVAL;

	queue = &session->queues[b->type];

	/*
	 * If a buffer with the LAST flag has been returned, subsequent calls to DQBUF
	 * must return -EPIPE until the queue is cleared.
	 */
	if (queue->is_capture_last)
		return -EPIPE;

	buffer_queue = &queue->pending_dqbufs;

	if (session->nonblocking_dequeue) {
		if (list_empty(buffer_queue))
			return -EAGAIN;
	} else if (queue->allocated_bufs == 0) {
		return -EINVAL;
	} else if (!queue->streaming) {
		return -EINVAL;
	}

	/*
	 * vv->lock has been acquired by virtio_media_device_ioctl. Release it
	 * while we want to other ioctls for this session can be processed and
	 * potentially trigger dqbuf_wait.
	 */
	mutex_unlock(&vv->vlock);
	ret = wait_event_interruptible(session->dqbuf_wait,
				       !list_empty(buffer_queue));
	mutex_lock(&vv->vlock);
	if (ret)
		return -EINTR;

	mutex_lock(&session->queues_lock);
	dqbuf = list_first_entry(buffer_queue, struct virtio_media_buffer,
				 list);
	list_del(&dqbuf->list);
	mutex_unlock(&session->queues_lock);

	/* Clear the DONE flag as the buffer is now being dequeued. */
	dqbuf->buffer.flags &= ~V4L2_BUF_FLAG_DONE;

	if (is_multiplanar) {
		size_t nb_planes = min_t(u32, b->length, VIDEO_MAX_PLANES);

		memcpy(b->m.planes, dqbuf->planes,
		       nb_planes * sizeof(struct v4l2_plane));
		planes_backup = b->m.planes;
	}

	memcpy(b, &dqbuf->buffer, sizeof(*b));

	if (is_multiplanar)
		b->m.planes = planes_backup;

	if (V4L2_TYPE_IS_CAPTURE(b->type) && b->flags & V4L2_BUF_FLAG_LAST)
		queue->is_capture_last = true;

	return 0;
}

/*
 * s/g_input/output work with an unsigned int - recast this to a u32 so the
 * size is unambiguous.
 */

static int virtio_media_g_input(struct file *file, void *fh, unsigned int *i)
{
	u32 input;
	int ret;

	ret = virtio_media_send_wr_ioctl(fh, VIDIOC_G_INPUT, &input,
					 sizeof(input), sizeof(input));
	if (ret)
		return ret;

	*i = input;

	return 0;
}

static int virtio_media_s_input(struct file *file, void *fh, unsigned int i)
{
	u32 input = i;

	return virtio_media_send_wr_ioctl(fh, VIDIOC_S_INPUT, &input,
					  sizeof(input), sizeof(input));
}

static int virtio_media_g_output(struct file *file, void *fh, unsigned int *o)
{
	u32 output;
	int ret;

	ret = virtio_media_send_wr_ioctl(fh, VIDIOC_G_OUTPUT, &output,
					 sizeof(output), sizeof(output));
	if (ret)
		return ret;

	*o = output;

	return 0;
}

static int virtio_media_s_output(struct file *file, void *fh, unsigned int o)
{
	u32 output = o;

	return virtio_media_send_wr_ioctl(fh, VIDIOC_S_OUTPUT, &output,
					  sizeof(output), sizeof(output));
}

/*
 * decoder_cmd can affect the state of the CAPTURE queue.
 */

static int virtio_media_decoder_cmd(struct file *file, void *fh,
				    struct v4l2_decoder_cmd *cmd)
{
	struct virtio_media_session *session = fh_to_session(fh);
	int ret;

	ret = virtio_media_send_wr_ioctl(fh, VIDIOC_DECODER_CMD, cmd,
					 sizeof(*cmd), sizeof(*cmd));
	if (ret)
		return ret;

	/* A START command makes the CAPTURE queue able to dequeue again. */
	if (cmd->cmd == V4L2_DEC_CMD_START) {
		session->queues[V4L2_BUF_TYPE_VIDEO_CAPTURE].is_capture_last =
			false;
		session->queues[V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE]
			.is_capture_last = false;
	}

	return 0;
}

/*
 * s_std doesn't work with a pointer, so we cannot use SIMPLE_W_IOCTL.
 */

static int virtio_media_s_std(struct file *file, void *fh, v4l2_std_id s)
{
	int ret;

	ret = virtio_media_send_w_ioctl(fh, VIDIOC_S_STD, &s, sizeof(s));
	if (ret)
		return ret;

	return 0;
}

const struct v4l2_ioctl_ops virtio_media_ioctl_ops = {
	/* VIDIOC_QUERYCAP handler */
	.vidioc_querycap = virtio_media_querycap,

	/* VIDIOC_ENUM_FMT handlers */
	.vidioc_enum_fmt_vid_cap = virtio_media_enum_fmt,
	.vidioc_enum_fmt_vid_overlay = virtio_media_enum_fmt,
	.vidioc_enum_fmt_vid_out = virtio_media_enum_fmt,
	.vidioc_enum_fmt_sdr_cap = virtio_media_enum_fmt,
	.vidioc_enum_fmt_sdr_out = virtio_media_enum_fmt,
	.vidioc_enum_fmt_meta_cap = virtio_media_enum_fmt,
	.vidioc_enum_fmt_meta_out = virtio_media_enum_fmt,

	/* VIDIOC_G_FMT handlers */
	.vidioc_g_fmt_vid_cap = virtio_media_g_fmt,
	.vidioc_g_fmt_vid_overlay = virtio_media_g_fmt,
	.vidioc_g_fmt_vid_out = virtio_media_g_fmt,
	.vidioc_g_fmt_vid_out_overlay = virtio_media_g_fmt,
	.vidioc_g_fmt_vbi_cap = virtio_media_g_fmt,
	.vidioc_g_fmt_vbi_out = virtio_media_g_fmt,
	.vidioc_g_fmt_sliced_vbi_cap = virtio_media_g_fmt,
	.vidioc_g_fmt_sliced_vbi_out = virtio_media_g_fmt,
	.vidioc_g_fmt_vid_cap_mplane = virtio_media_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = virtio_media_g_fmt,
	.vidioc_g_fmt_sdr_cap = virtio_media_g_fmt,
	.vidioc_g_fmt_sdr_out = virtio_media_g_fmt,
	.vidioc_g_fmt_meta_cap = virtio_media_g_fmt,
	.vidioc_g_fmt_meta_out = virtio_media_g_fmt,

	/* VIDIOC_S_FMT handlers */
	.vidioc_s_fmt_vid_cap = virtio_media_s_fmt,
	.vidioc_s_fmt_vid_overlay = virtio_media_s_fmt,
	.vidioc_s_fmt_vid_out = virtio_media_s_fmt,
	.vidioc_s_fmt_vid_out_overlay = virtio_media_s_fmt,
	.vidioc_s_fmt_vbi_cap = virtio_media_s_fmt,
	.vidioc_s_fmt_vbi_out = virtio_media_s_fmt,
	.vidioc_s_fmt_sliced_vbi_cap = virtio_media_s_fmt,
	.vidioc_s_fmt_sliced_vbi_out = virtio_media_s_fmt,
	.vidioc_s_fmt_vid_cap_mplane = virtio_media_s_fmt,
	.vidioc_s_fmt_vid_out_mplane = virtio_media_s_fmt,
	.vidioc_s_fmt_sdr_cap = virtio_media_s_fmt,
	.vidioc_s_fmt_sdr_out = virtio_media_s_fmt,
	.vidioc_s_fmt_meta_cap = virtio_media_s_fmt,
	.vidioc_s_fmt_meta_out = virtio_media_s_fmt,

	/* VIDIOC_TRY_FMT handlers */
	.vidioc_try_fmt_vid_cap = virtio_media_try_fmt,
	.vidioc_try_fmt_vid_overlay = virtio_media_try_fmt,
	.vidioc_try_fmt_vid_out = virtio_media_try_fmt,
	.vidioc_try_fmt_vid_out_overlay = virtio_media_try_fmt,
	.vidioc_try_fmt_vbi_cap = virtio_media_try_fmt,
	.vidioc_try_fmt_vbi_out = virtio_media_try_fmt,
	.vidioc_try_fmt_sliced_vbi_cap = virtio_media_try_fmt,
	.vidioc_try_fmt_sliced_vbi_out = virtio_media_try_fmt,
	.vidioc_try_fmt_vid_cap_mplane = virtio_media_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = virtio_media_try_fmt,
	.vidioc_try_fmt_sdr_cap = virtio_media_try_fmt,
	.vidioc_try_fmt_sdr_out = virtio_media_try_fmt,
	.vidioc_try_fmt_meta_cap = virtio_media_try_fmt,
	.vidioc_try_fmt_meta_out = virtio_media_try_fmt,

	/* Buffer handlers */
	.vidioc_reqbufs = virtio_media_reqbufs,
	.vidioc_querybuf = virtio_media_querybuf,
	.vidioc_qbuf = virtio_media_qbuf,
	.vidioc_expbuf = NULL,
	.vidioc_dqbuf = virtio_media_dqbuf,
	.vidioc_create_bufs = virtio_media_create_bufs,
	.vidioc_prepare_buf = virtio_media_prepare_buf,
	/* Overlay interface not supported yet */
	.vidioc_overlay = NULL,
	/* Overlay interface not supported yet */
	.vidioc_g_fbuf = NULL,
	/* Overlay interface not supported yet */
	.vidioc_s_fbuf = NULL,

	/* Stream on/off */
	.vidioc_streamon = virtio_media_streamon,
	.vidioc_streamoff = virtio_media_streamoff,

	/* Standard handling */
	.vidioc_g_std = virtio_media_g_std,
	.vidioc_s_std = virtio_media_s_std,
	.vidioc_querystd = virtio_media_querystd,

	/* Input handling */
	.vidioc_enum_input = virtio_media_enuminput,
	.vidioc_g_input = virtio_media_g_input,
	.vidioc_s_input = virtio_media_s_input,

	/* Output handling */
	.vidioc_enum_output = virtio_media_enumoutput,
	.vidioc_g_output = virtio_media_g_output,
	.vidioc_s_output = virtio_media_s_output,

	/* Control handling */
	.vidioc_query_ext_ctrl = virtio_media_query_ext_ctrl,
	.vidioc_g_ext_ctrls = virtio_media_g_ext_ctrls,
	.vidioc_s_ext_ctrls = virtio_media_s_ext_ctrls,
	.vidioc_try_ext_ctrls = virtio_media_try_ext_ctrls,
	.vidioc_querymenu = virtio_media_querymenu,

	/* Audio ioctls */
	.vidioc_enumaudio = virtio_media_enumaudio,
	.vidioc_g_audio = virtio_media_g_audio,
	.vidioc_s_audio = virtio_media_s_audio,

	/* Audio out ioctls */
	.vidioc_enumaudout = virtio_media_enumaudout,
	.vidioc_g_audout = virtio_media_g_audout,
	.vidioc_s_audout = virtio_media_s_audout,
	.vidioc_g_modulator = virtio_media_g_modulator,
	.vidioc_s_modulator = virtio_media_s_modulator,

	/* Crop ioctls */
	/* Not directly an ioctl (part of VIDIOC_CROPCAP), so no need to implement */
	.vidioc_g_pixelaspect = NULL,
	.vidioc_g_selection = virtio_media_g_selection,
	.vidioc_s_selection = virtio_media_s_selection,

	/* Compression ioctls */
	/* Deprecated in V4L2. */
	.vidioc_g_jpegcomp = NULL,
	/* Deprecated in V4L2. */
	.vidioc_s_jpegcomp = NULL,
	.vidioc_g_enc_index = virtio_media_g_enc_index,
	.vidioc_encoder_cmd = virtio_media_encoder_cmd,
	.vidioc_try_encoder_cmd = virtio_media_try_encoder_cmd,
	.vidioc_decoder_cmd = virtio_media_decoder_cmd,
	.vidioc_try_decoder_cmd = virtio_media_try_decoder_cmd,

	/* Stream type-dependent parameter ioctls */
	.vidioc_g_parm = virtio_media_g_parm,
	.vidioc_s_parm = virtio_media_s_parm,

	/* Tuner ioctls */
	.vidioc_g_tuner = virtio_media_g_tuner,
	.vidioc_s_tuner = virtio_media_s_tuner,
	.vidioc_g_frequency = virtio_media_g_frequency,
	.vidioc_s_frequency = virtio_media_s_frequency,
	.vidioc_enum_freq_bands = virtio_media_enum_freq_bands,

	/* Sliced VBI cap */
	.vidioc_g_sliced_vbi_cap = virtio_media_g_sliced_vbi_cap,

	/* Log status ioctl */
	/* Guest-only operation */
	.vidioc_log_status = NULL,

	.vidioc_s_hw_freq_seek = virtio_media_s_hw_freq_seek,

	.vidioc_enum_framesizes = virtio_media_enum_framesizes,
	.vidioc_enum_frameintervals = virtio_media_enum_frameintervals,

	/* DV Timings IOCTLs */
	.vidioc_s_dv_timings = virtio_media_s_dv_timings,
	.vidioc_g_dv_timings = virtio_media_g_dv_timings,
	.vidioc_query_dv_timings = virtio_media_query_dv_timings,
	.vidioc_enum_dv_timings = virtio_media_enum_dv_timings,
	.vidioc_dv_timings_cap = virtio_media_dv_timings_cap,
	.vidioc_g_edid = NULL,
	.vidioc_s_edid = NULL,

	.vidioc_subscribe_event = virtio_media_subscribe_event,
	.vidioc_unsubscribe_event = virtio_media_unsubscribe_event,

	/* For other private ioctls */
	.vidioc_default = NULL,
};

long virtio_media_device_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct video_device *video_dev = video_devdata(file);
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct v4l2_fh *vfh = NULL;
	struct v4l2_standard standard;
	v4l2_std_id std_id = 0;
	int ret;

	if (test_bit(V4L2_FL_USES_V4L2_FH, &video_dev->flags))
		vfh = file->private_data;

	mutex_lock(&vv->vlock);

	/*
	 * We need to handle a few ioctls manually because their result rely on
	 * vfd->tvnorms, which is normally updated by the driver as S_INPUT is
	 * called. Since we want to just pass these ioctls through, we have to hijack
	 * them from here.
	 */
	switch (cmd) {
	case VIDIOC_S_STD:
		ret = copy_from_user(&std_id, (void __user *)arg,
				     sizeof(std_id));
		if (ret) {
			ret = -EINVAL;
			break;
		}
		ret = virtio_media_s_std(file, vfh, std_id);
		break;
	case VIDIOC_ENUMSTD:
		ret = copy_from_user(&standard, (void __user *)arg,
				     sizeof(standard));
		if (ret) {
			ret = -EINVAL;
			break;
		}
		ret = virtio_media_enumstd(file, vfh, &standard);
		if (ret)
			break;
		ret = copy_to_user((void __user *)arg, &standard,
				   sizeof(standard));
		if (ret)
			ret = -EINVAL;
		break;
	case VIDIOC_QUERYSTD:
		ret = virtio_media_querystd(file, vfh, &std_id);
		if (ret)
			break;
		ret = copy_to_user((void __user *)arg, &std_id, sizeof(std_id));
		if (ret)
			ret = -EINVAL;
		break;
	default:
		ret = video_ioctl2(file, cmd, arg);
		break;
	}

	mutex_unlock(&vv->vlock);

	return ret;
}
