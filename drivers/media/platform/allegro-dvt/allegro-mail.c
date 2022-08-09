// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Pengutronix, Michael Tretter <kernel@pengutronix.de>
 *
 * Helper functions for handling messages that are send via mailbox to the
 * Allegro VCU firmware.
 */

#include <linux/bitfield.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/videodev2.h>

#include "allegro-mail.h"

const char *msg_type_name(enum mcu_msg_type type)
{
	static char buf[9];

	switch (type) {
	case MCU_MSG_TYPE_INIT:
		return "INIT";
	case MCU_MSG_TYPE_CREATE_CHANNEL:
		return "CREATE_CHANNEL";
	case MCU_MSG_TYPE_DESTROY_CHANNEL:
		return "DESTROY_CHANNEL";
	case MCU_MSG_TYPE_ENCODE_FRAME:
		return "ENCODE_FRAME";
	case MCU_MSG_TYPE_PUT_STREAM_BUFFER:
		return "PUT_STREAM_BUFFER";
	case MCU_MSG_TYPE_PUSH_BUFFER_INTERMEDIATE:
		return "PUSH_BUFFER_INTERMEDIATE";
	case MCU_MSG_TYPE_PUSH_BUFFER_REFERENCE:
		return "PUSH_BUFFER_REFERENCE";
	default:
		snprintf(buf, sizeof(buf), "(0x%04x)", type);
		return buf;
	}
}
EXPORT_SYMBOL(msg_type_name);

static ssize_t
allegro_enc_init(u32 *dst, struct mcu_msg_init_request *msg)
{
	unsigned int i = 0;
	enum mcu_msg_version version = msg->header.version;

	dst[i++] = msg->reserved0;
	dst[i++] = msg->suballoc_dma;
	dst[i++] = msg->suballoc_size;
	dst[i++] = msg->encoder_buffer_size;
	dst[i++] = msg->encoder_buffer_color_depth;
	dst[i++] = msg->num_cores;
	if (version >= MCU_MSG_VERSION_2019_2) {
		dst[i++] = msg->clk_rate;
		dst[i++] = 0;
	}

	return i * sizeof(*dst);
}

static inline u32 settings_get_mcu_codec(struct create_channel_param *param)
{
	enum mcu_msg_version version = param->version;
	u32 pixelformat = param->codec;

	if (version < MCU_MSG_VERSION_2019_2) {
		switch (pixelformat) {
		case V4L2_PIX_FMT_HEVC:
			return 2;
		case V4L2_PIX_FMT_H264:
		default:
			return 1;
		}
	} else {
		switch (pixelformat) {
		case V4L2_PIX_FMT_HEVC:
			return 1;
		case V4L2_PIX_FMT_H264:
		default:
			return 0;
		}
	}
}

ssize_t
allegro_encode_config_blob(u32 *dst, struct create_channel_param *param)
{
	enum mcu_msg_version version = param->version;
	unsigned int i = 0;
	unsigned int j = 0;
	u32 val;
	unsigned int codec = settings_get_mcu_codec(param);

	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = param->layer_id;
	dst[i++] = FIELD_PREP(GENMASK(31, 16), param->height) |
		   FIELD_PREP(GENMASK(15, 0), param->width);
	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = param->videomode;
	dst[i++] = param->format;
	if (version < MCU_MSG_VERSION_2019_2)
		dst[i++] = param->colorspace;
	dst[i++] = param->src_mode;
	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = param->src_bit_depth;
	dst[i++] = FIELD_PREP(GENMASK(31, 24), codec) |
		   FIELD_PREP(GENMASK(23, 8), param->constraint_set_flags) |
		   FIELD_PREP(GENMASK(7, 0), param->profile);
	dst[i++] = FIELD_PREP(GENMASK(31, 16), param->tier) |
		   FIELD_PREP(GENMASK(15, 0), param->level);

	val = 0;
	val |= param->temporal_mvp_enable ? BIT(20) : 0;
	val |= FIELD_PREP(GENMASK(7, 4), param->log2_max_frame_num);
	if (version >= MCU_MSG_VERSION_2019_2)
		val |= FIELD_PREP(GENMASK(3, 0), param->log2_max_poc - 1);
	else
		val |= FIELD_PREP(GENMASK(3, 0), param->log2_max_poc);
	dst[i++] = val;

	val = 0;
	val |= param->enable_reordering ? BIT(0) : 0;
	val |= param->dbf_ovr_en ? BIT(2) : 0;
	val |= param->override_lf ? BIT(12) : 0;
	dst[i++] = val;

	if (version >= MCU_MSG_VERSION_2019_2) {
		val = 0;
		val |= param->custom_lda ? BIT(2) : 0;
		val |= param->rdo_cost_mode ? BIT(20) : 0;
		dst[i++] = val;

		val = 0;
		val |= param->lf ? BIT(2) : 0;
		val |= param->lf_x_tile ? BIT(3) : 0;
		val |= param->lf_x_slice ? BIT(4) : 0;
		dst[i++] = val;
	} else {
		val = 0;
		dst[i++] = val;
	}

	dst[i++] = FIELD_PREP(GENMASK(15, 8), param->beta_offset) |
		   FIELD_PREP(GENMASK(7, 0), param->tc_offset);
	dst[i++] = param->unknown11;
	dst[i++] = param->unknown12;
	dst[i++] = param->num_slices;
	dst[i++] = param->encoder_buffer_offset;
	dst[i++] = param->encoder_buffer_enabled;

	dst[i++] = FIELD_PREP(GENMASK(31, 16), param->clip_vrt_range) |
		   FIELD_PREP(GENMASK(15, 0), param->clip_hrz_range);
	dst[i++] = FIELD_PREP(GENMASK(31, 16), param->me_range[1]) |
		   FIELD_PREP(GENMASK(15, 0), param->me_range[0]);
	dst[i++] = FIELD_PREP(GENMASK(31, 16), param->me_range[3]) |
		   FIELD_PREP(GENMASK(15, 0), param->me_range[2]);
	dst[i++] = FIELD_PREP(GENMASK(31, 24), param->min_tu_size) |
		   FIELD_PREP(GENMASK(23, 16), param->max_tu_size) |
		   FIELD_PREP(GENMASK(15, 8), param->min_cu_size) |
		   FIELD_PREP(GENMASK(8, 0), param->max_cu_size);
	dst[i++] = FIELD_PREP(GENMASK(15, 8), param->max_transfo_depth_intra) |
		   FIELD_PREP(GENMASK(7, 0), param->max_transfo_depth_inter);
	dst[i++] = param->entropy_mode;
	dst[i++] = param->wp_mode;

	dst[i++] = param->rate_control_mode;
	dst[i++] = param->initial_rem_delay;
	dst[i++] = param->cpb_size;
	dst[i++] = FIELD_PREP(GENMASK(31, 16), param->clk_ratio) |
		   FIELD_PREP(GENMASK(15, 0), param->framerate);
	dst[i++] = param->target_bitrate;
	dst[i++] = param->max_bitrate;
	dst[i++] = FIELD_PREP(GENMASK(31, 16), param->min_qp) |
		   FIELD_PREP(GENMASK(15, 0), param->initial_qp);
	dst[i++] = FIELD_PREP(GENMASK(31, 16), param->ip_delta) |
		   FIELD_PREP(GENMASK(15, 0), param->max_qp);
	dst[i++] = FIELD_PREP(GENMASK(31, 16), param->golden_ref) |
		   FIELD_PREP(GENMASK(15, 0), param->pb_delta);
	dst[i++] = FIELD_PREP(GENMASK(31, 16), param->golden_ref_frequency) |
		   FIELD_PREP(GENMASK(15, 0), param->golden_delta);
	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = param->rate_control_option;
	else
		dst[i++] = 0;

	if (version >= MCU_MSG_VERSION_2019_2) {
		dst[i++] = param->num_pixel;
		dst[i++] = FIELD_PREP(GENMASK(31, 16), param->max_pixel_value) |
			FIELD_PREP(GENMASK(15, 0), param->max_psnr);
		for (j = 0; j < 3; j++)
			dst[i++] = param->maxpicturesize[j];
	}

	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = param->gop_ctrl_mode;
	else
		dst[i++] = 0;

	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = FIELD_PREP(GENMASK(31, 24), param->freq_golden_ref) |
			   FIELD_PREP(GENMASK(23, 16), param->num_b) |
			   FIELD_PREP(GENMASK(15, 0), param->gop_length);
	dst[i++] = param->freq_idr;
	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = param->enable_lt;
	dst[i++] = param->freq_lt;
	dst[i++] = param->gdr_mode;
	if (version < MCU_MSG_VERSION_2019_2)
		dst[i++] = FIELD_PREP(GENMASK(31, 24), param->freq_golden_ref) |
			   FIELD_PREP(GENMASK(23, 16), param->num_b) |
			   FIELD_PREP(GENMASK(15, 0), param->gop_length);

	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = param->tmpdqp;

	dst[i++] = param->subframe_latency;
	dst[i++] = param->lda_control_mode;
	if (version < MCU_MSG_VERSION_2019_2)
		dst[i++] = param->unknown41;

	if (version >= MCU_MSG_VERSION_2019_2) {
		for (j = 0; j < 6; j++)
			dst[i++] = param->lda_factors[j];
		dst[i++] = param->max_num_merge_cand;
	}

	return i * sizeof(*dst);
}

static ssize_t
allegro_enc_create_channel(u32 *dst, struct mcu_msg_create_channel *msg)
{
	enum mcu_msg_version version = msg->header.version;
	unsigned int i = 0;

	dst[i++] = msg->user_id;

	if (version >= MCU_MSG_VERSION_2019_2) {
		dst[i++] = msg->blob_mcu_addr;
	} else {
		memcpy(&dst[i], msg->blob, msg->blob_size);
		i += msg->blob_size / sizeof(*dst);
	}

	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = msg->ep1_addr;

	return i * sizeof(*dst);
}

ssize_t allegro_decode_config_blob(struct create_channel_param *param,
				   struct mcu_msg_create_channel_response *msg,
				   u32 *src)
{
	enum mcu_msg_version version = msg->header.version;

	if (version >= MCU_MSG_VERSION_2019_2) {
		param->num_ref_idx_l0 = FIELD_GET(GENMASK(7, 4), src[9]);
		param->num_ref_idx_l1 = FIELD_GET(GENMASK(11, 8), src[9]);
	} else {
		param->num_ref_idx_l0 = msg->num_ref_idx_l0;
		param->num_ref_idx_l1 = msg->num_ref_idx_l1;
	}

	return 0;
}

static ssize_t
allegro_enc_destroy_channel(u32 *dst, struct mcu_msg_destroy_channel *msg)
{
	unsigned int i = 0;

	dst[i++] = msg->channel_id;

	return i * sizeof(*dst);
}

static ssize_t
allegro_enc_push_buffers(u32 *dst, struct mcu_msg_push_buffers_internal *msg)
{
	unsigned int i = 0;
	struct mcu_msg_push_buffers_internal_buffer *buffer;
	unsigned int num_buffers = msg->num_buffers;
	unsigned int j;

	dst[i++] = msg->channel_id;

	for (j = 0; j < num_buffers; j++) {
		buffer = &msg->buffer[j];
		dst[i++] = buffer->dma_addr;
		dst[i++] = buffer->mcu_addr;
		dst[i++] = buffer->size;
	}

	return i * sizeof(*dst);
}

static ssize_t
allegro_enc_put_stream_buffer(u32 *dst,
			      struct mcu_msg_put_stream_buffer *msg)
{
	unsigned int i = 0;

	dst[i++] = msg->channel_id;
	dst[i++] = msg->dma_addr;
	dst[i++] = msg->mcu_addr;
	dst[i++] = msg->size;
	dst[i++] = msg->offset;
	dst[i++] = lower_32_bits(msg->dst_handle);
	dst[i++] = upper_32_bits(msg->dst_handle);

	return i * sizeof(*dst);
}

static ssize_t
allegro_enc_encode_frame(u32 *dst, struct mcu_msg_encode_frame *msg)
{
	enum mcu_msg_version version = msg->header.version;
	unsigned int i = 0;

	dst[i++] = msg->channel_id;

	dst[i++] = msg->reserved;
	dst[i++] = msg->encoding_options;
	dst[i++] = FIELD_PREP(GENMASK(31, 16), msg->padding) |
		   FIELD_PREP(GENMASK(15, 0), msg->pps_qp);

	if (version >= MCU_MSG_VERSION_2019_2) {
		dst[i++] = 0;
		dst[i++] = 0;
		dst[i++] = 0;
		dst[i++] = 0;
	}

	dst[i++] = lower_32_bits(msg->user_param);
	dst[i++] = upper_32_bits(msg->user_param);
	dst[i++] = lower_32_bits(msg->src_handle);
	dst[i++] = upper_32_bits(msg->src_handle);
	dst[i++] = msg->request_options;
	dst[i++] = msg->src_y;
	dst[i++] = msg->src_uv;
	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = msg->is_10_bit;
	dst[i++] = msg->stride;
	if (version >= MCU_MSG_VERSION_2019_2)
		dst[i++] = msg->format;
	dst[i++] = msg->ep2;
	dst[i++] = lower_32_bits(msg->ep2_v);
	dst[i++] = upper_32_bits(msg->ep2_v);

	return i * sizeof(*dst);
}

static ssize_t
allegro_dec_init(struct mcu_msg_init_response *msg, u32 *src)
{
	unsigned int i = 0;

	msg->reserved0 = src[i++];

	return i * sizeof(*src);
}

static ssize_t
allegro_dec_create_channel(struct mcu_msg_create_channel_response *msg,
			   u32 *src)
{
	enum mcu_msg_version version = msg->header.version;
	unsigned int i = 0;

	msg->channel_id = src[i++];
	msg->user_id = src[i++];
	/*
	 * Version >= MCU_MSG_VERSION_2019_2 is handled in
	 * allegro_decode_config_blob().
	 */
	if (version < MCU_MSG_VERSION_2019_2) {
		msg->options = src[i++];
		msg->num_core = src[i++];
		msg->num_ref_idx_l0 = FIELD_GET(GENMASK(7, 4), src[i]);
		msg->num_ref_idx_l1 = FIELD_GET(GENMASK(11, 8), src[i++]);
	}
	msg->int_buffers_count = src[i++];
	msg->int_buffers_size = src[i++];
	msg->rec_buffers_count = src[i++];
	msg->rec_buffers_size = src[i++];
	msg->reserved = src[i++];
	msg->error_code = src[i++];

	return i * sizeof(*src);
}

static ssize_t
allegro_dec_destroy_channel(struct mcu_msg_destroy_channel_response *msg,
			    u32 *src)
{
	unsigned int i = 0;

	msg->channel_id = src[i++];

	return i * sizeof(*src);
}

static ssize_t
allegro_dec_encode_frame(struct mcu_msg_encode_frame_response *msg, u32 *src)
{
	enum mcu_msg_version version = msg->header.version;
	unsigned int i = 0;
	unsigned int j;

	msg->channel_id = src[i++];

	msg->dst_handle = src[i++];
	msg->dst_handle |= (((u64)src[i++]) << 32);
	msg->user_param = src[i++];
	msg->user_param |= (((u64)src[i++]) << 32);
	msg->src_handle = src[i++];
	msg->src_handle |= (((u64)src[i++]) << 32);
	msg->skip = FIELD_GET(GENMASK(31, 16), src[i]);
	msg->is_ref = FIELD_GET(GENMASK(15, 0), src[i++]);
	msg->initial_removal_delay = src[i++];
	msg->dpb_output_delay = src[i++];
	msg->size = src[i++];
	msg->frame_tag_size = src[i++];
	msg->stuffing = src[i++];
	msg->filler = src[i++];
	msg->num_row = FIELD_GET(GENMASK(31, 16), src[i]);
	msg->num_column = FIELD_GET(GENMASK(15, 0), src[i++]);
	msg->num_ref_idx_l1 = FIELD_GET(GENMASK(31, 24), src[i]);
	msg->num_ref_idx_l0 = FIELD_GET(GENMASK(23, 16), src[i]);
	msg->qp = FIELD_GET(GENMASK(15, 0), src[i++]);
	msg->partition_table_offset = src[i++];
	msg->partition_table_size = src[i++];
	msg->sum_complex = src[i++];
	for (j = 0; j < 4; j++)
		msg->tile_width[j] = src[i++];
	for (j = 0; j < 22; j++)
		msg->tile_height[j] = src[i++];
	msg->error_code = src[i++];
	msg->slice_type = src[i++];
	msg->pic_struct = src[i++];
	msg->reserved = FIELD_GET(GENMASK(31, 24), src[i]);
	msg->is_last_slice = FIELD_GET(GENMASK(23, 16), src[i]);
	msg->is_first_slice = FIELD_GET(GENMASK(15, 8), src[i]);
	msg->is_idr = FIELD_GET(GENMASK(7, 0), src[i++]);

	msg->reserved1 = FIELD_GET(GENMASK(31, 16), src[i]);
	msg->pps_qp = FIELD_GET(GENMASK(15, 0), src[i++]);

	msg->reserved2 = src[i++];
	if (version >= MCU_MSG_VERSION_2019_2) {
		msg->reserved3 = src[i++];
		msg->reserved4 = src[i++];
		msg->reserved5 = src[i++];
		msg->reserved6 = src[i++];
	}

	return i * sizeof(*src);
}

/**
 * allegro_encode_mail() - Encode allegro messages to firmware format
 * @dst: Pointer to the memory that will be filled with data
 * @msg: The allegro message that will be encoded
 */
ssize_t allegro_encode_mail(u32 *dst, void *msg)
{
	const struct mcu_msg_header *header = msg;
	ssize_t size;

	if (!msg || !dst)
		return -EINVAL;

	switch (header->type) {
	case MCU_MSG_TYPE_INIT:
		size = allegro_enc_init(&dst[1], msg);
		break;
	case MCU_MSG_TYPE_CREATE_CHANNEL:
		size = allegro_enc_create_channel(&dst[1], msg);
		break;
	case MCU_MSG_TYPE_DESTROY_CHANNEL:
		size = allegro_enc_destroy_channel(&dst[1], msg);
		break;
	case MCU_MSG_TYPE_ENCODE_FRAME:
		size = allegro_enc_encode_frame(&dst[1], msg);
		break;
	case MCU_MSG_TYPE_PUT_STREAM_BUFFER:
		size = allegro_enc_put_stream_buffer(&dst[1], msg);
		break;
	case MCU_MSG_TYPE_PUSH_BUFFER_INTERMEDIATE:
	case MCU_MSG_TYPE_PUSH_BUFFER_REFERENCE:
		size = allegro_enc_push_buffers(&dst[1], msg);
		break;
	default:
		return -EINVAL;
	}

	/*
	 * The encoded messages might have different length depending on
	 * the firmware version or certain fields. Therefore, we have to
	 * set the body length after encoding the message.
	 */
	dst[0] = FIELD_PREP(GENMASK(31, 16), header->type) |
		 FIELD_PREP(GENMASK(15, 0), size);

	return size + sizeof(*dst);
}

/**
 * allegro_decode_mail() - Parse allegro messages from the firmware.
 * @msg: The mcu_msg_response that will be filled with parsed values.
 * @src: Pointer to the memory that will be parsed
 *
 * The message format in the mailbox depends on the firmware. Parse the
 * different formats into a uniform message format that can be used without
 * taking care of the firmware version.
 */
int allegro_decode_mail(void *msg, u32 *src)
{
	struct mcu_msg_header *header;

	if (!src || !msg)
		return -EINVAL;

	header = msg;
	header->type = FIELD_GET(GENMASK(31, 16), src[0]);

	src++;
	switch (header->type) {
	case MCU_MSG_TYPE_INIT:
		allegro_dec_init(msg, src);
		break;
	case MCU_MSG_TYPE_CREATE_CHANNEL:
		allegro_dec_create_channel(msg, src);
		break;
	case MCU_MSG_TYPE_DESTROY_CHANNEL:
		allegro_dec_destroy_channel(msg, src);
		break;
	case MCU_MSG_TYPE_ENCODE_FRAME:
		allegro_dec_encode_frame(msg, src);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
