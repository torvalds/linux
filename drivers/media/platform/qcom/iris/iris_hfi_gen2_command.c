// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitfield.h>

#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_packet.h"

#define UNSPECIFIED_COLOR_FORMAT 5
#define NUM_SYS_INIT_PACKETS 8

#define SYS_INIT_PKT_SIZE (sizeof(struct iris_hfi_header) + \
	NUM_SYS_INIT_PACKETS * (sizeof(struct iris_hfi_packet) + sizeof(u32)))

#define SYS_IFPC_PKT_SIZE (sizeof(struct iris_hfi_header) + \
	sizeof(struct iris_hfi_packet) + sizeof(u32))

#define SYS_NO_PAYLOAD_PKT_SIZE (sizeof(struct iris_hfi_header) + \
	sizeof(struct iris_hfi_packet))

static int iris_hfi_gen2_sys_init(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_INIT_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_sys_init(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static int iris_hfi_gen2_sys_image_version(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_NO_PAYLOAD_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_image_version(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static int iris_hfi_gen2_sys_interframe_powercollapse(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_IFPC_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_sys_interframe_powercollapse(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static int iris_hfi_gen2_sys_pc_prep(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_NO_PAYLOAD_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_sys_pc_prep(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static u32 iris_hfi_gen2_get_port(u32 plane)
{
	switch (plane) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return HFI_PORT_BITSTREAM;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return HFI_PORT_RAW;
	default:
		return HFI_PORT_NONE;
	}
}

static int iris_hfi_gen2_session_set_property(struct iris_inst *inst, u32 packet_type, u32 flag,
					      u32 plane, u32 payload_type, void *payload,
					      u32 payload_size)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);

	iris_hfi_gen2_packet_session_property(inst,
					      packet_type,
					      flag,
					      plane,
					      payload_type,
					      payload,
					      payload_size);

	return iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
					inst_hfi_gen2->packet->size);
}

static int iris_hfi_gen2_set_bitstream_resolution(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	u32 resolution = inst->fmt_src->fmt.pix_mp.width << 16 |
		inst->fmt_src->fmt.pix_mp.height;

	inst_hfi_gen2->src_subcr_params.bitstream_resolution = resolution;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_BITSTREAM_RESOLUTION,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &resolution,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_crop_offsets(struct iris_inst *inst)
{
	u32 bottom_offset = (inst->fmt_src->fmt.pix_mp.height - inst->crop.height);
	u32 right_offset = (inst->fmt_src->fmt.pix_mp.width - inst->crop.width);
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	u32 left_offset = inst->crop.left;
	u32 top_offset = inst->crop.top;
	u32 payload[2];

	payload[0] = FIELD_PREP(GENMASK(31, 16), left_offset) | top_offset;
	payload[1] = FIELD_PREP(GENMASK(31, 16), right_offset) | bottom_offset;
	inst_hfi_gen2->src_subcr_params.crop_offsets[0] = payload[0];
	inst_hfi_gen2->src_subcr_params.crop_offsets[1] = payload[1];

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_CROP_OFFSETS,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_64_PACKED,
						  &payload,
						  sizeof(u64));
}

static int iris_hfi_gen2_set_bit_dpeth(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	u32 bitdepth = BIT_DEPTH_8;

	inst_hfi_gen2->src_subcr_params.bit_depth = bitdepth;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_LUMA_CHROMA_BIT_DEPTH,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &bitdepth,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_coded_frames(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	u32 coded_frames = 0;

	if (inst->fw_caps[CODED_FRAMES].value == CODED_FRAMES_PROGRESSIVE)
		coded_frames = HFI_BITMASK_FRAME_MBS_ONLY_FLAG;
	inst_hfi_gen2->src_subcr_params.coded_frames = coded_frames;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_CODED_FRAMES,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &coded_frames,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_min_output_count(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	u32 min_output = inst->buffers[BUF_OUTPUT].min_count;

	inst_hfi_gen2->src_subcr_params.fw_min_count = min_output;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &min_output,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_picture_order_count(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	u32 poc = 0;

	inst_hfi_gen2->src_subcr_params.pic_order_cnt = poc;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_PIC_ORDER_CNT_TYPE,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &poc,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_colorspace(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	struct v4l2_pix_format_mplane *pixmp = &inst->fmt_src->fmt.pix_mp;
	u32 video_signal_type_present_flag = 0, color_info;
	u32 matrix_coeff = HFI_MATRIX_COEFF_RESERVED;
	u32 video_format = UNSPECIFIED_COLOR_FORMAT;
	u32 full_range = V4L2_QUANTIZATION_DEFAULT;
	u32 transfer_char = HFI_TRANSFER_RESERVED;
	u32 colour_description_present_flag = 0;
	u32 primaries = HFI_PRIMARIES_RESERVED;

	if (pixmp->colorspace != V4L2_COLORSPACE_DEFAULT ||
	    pixmp->ycbcr_enc != V4L2_YCBCR_ENC_DEFAULT ||
	    pixmp->xfer_func != V4L2_XFER_FUNC_DEFAULT) {
		colour_description_present_flag = 1;
		video_signal_type_present_flag = 1;
		primaries = iris_hfi_gen2_get_color_primaries(pixmp->colorspace);
		matrix_coeff = iris_hfi_gen2_get_matrix_coefficients(pixmp->ycbcr_enc);
		transfer_char = iris_hfi_gen2_get_transfer_char(pixmp->xfer_func);
	}

	if (pixmp->quantization != V4L2_QUANTIZATION_DEFAULT) {
		video_signal_type_present_flag = 1;
		full_range = pixmp->quantization == V4L2_QUANTIZATION_FULL_RANGE ? 1 : 0;
	}

	color_info = iris_hfi_gen2_get_color_info(matrix_coeff, transfer_char, primaries,
						  colour_description_present_flag, full_range,
						  video_format, video_signal_type_present_flag);

	inst_hfi_gen2->src_subcr_params.color_info = color_info;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_SIGNAL_COLOR_INFO,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_32_PACKED,
						  &color_info,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_profile(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	u32 profile = inst->fw_caps[PROFILE].value;

	inst_hfi_gen2->src_subcr_params.profile = profile;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_PROFILE,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32_ENUM,
						  &profile,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_level(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	u32 level = inst->fw_caps[LEVEL].value;

	inst_hfi_gen2->src_subcr_params.level = level;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_LEVEL,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32_ENUM,
						  &level,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_colorformat(struct iris_inst *inst)
{
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	u32 hfi_colorformat, pixelformat;

	pixelformat = inst->fmt_dst->fmt.pix_mp.pixelformat;
	hfi_colorformat = pixelformat == V4L2_PIX_FMT_NV12 ? HFI_COLOR_FMT_NV12 : 0;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_COLOR_FORMAT,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U32,
						  &hfi_colorformat,
						  sizeof(u32));
}

static int iris_hfi_gen2_set_linear_stride_scanline(struct iris_inst *inst)
{
	u32 port = iris_hfi_gen2_get_port(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	u32 pixelformat = inst->fmt_dst->fmt.pix_mp.pixelformat;
	u32 scanline_y = inst->fmt_dst->fmt.pix_mp.height;
	u32 stride_y = inst->fmt_dst->fmt.pix_mp.width;
	u32 scanline_uv = scanline_y / 2;
	u32 stride_uv = stride_y;
	u32 payload[2];

	if (pixelformat != V4L2_PIX_FMT_NV12)
		return 0;

	payload[0] = stride_y << 16 | scanline_y;
	payload[1] = stride_uv << 16 | scanline_uv;

	return iris_hfi_gen2_session_set_property(inst,
						  HFI_PROP_LINEAR_STRIDE_SCANLINE,
						  HFI_HOST_FLAGS_NONE,
						  port,
						  HFI_PAYLOAD_U64,
						  &payload,
						  sizeof(u64));
}

static int iris_hfi_gen2_session_set_config_params(struct iris_inst *inst, u32 plane)
{
	struct iris_core *core = inst->core;
	u32 config_params_size, i, j;
	const u32 *config_params;
	int ret;

	static const struct iris_hfi_prop_type_handle prop_type_handle_arr[] = {
		{HFI_PROP_BITSTREAM_RESOLUTION,       iris_hfi_gen2_set_bitstream_resolution   },
		{HFI_PROP_CROP_OFFSETS,               iris_hfi_gen2_set_crop_offsets           },
		{HFI_PROP_CODED_FRAMES,               iris_hfi_gen2_set_coded_frames           },
		{HFI_PROP_LUMA_CHROMA_BIT_DEPTH,      iris_hfi_gen2_set_bit_dpeth              },
		{HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT, iris_hfi_gen2_set_min_output_count       },
		{HFI_PROP_PIC_ORDER_CNT_TYPE,         iris_hfi_gen2_set_picture_order_count    },
		{HFI_PROP_SIGNAL_COLOR_INFO,          iris_hfi_gen2_set_colorspace             },
		{HFI_PROP_PROFILE,                    iris_hfi_gen2_set_profile                },
		{HFI_PROP_LEVEL,                      iris_hfi_gen2_set_level                  },
		{HFI_PROP_COLOR_FORMAT,               iris_hfi_gen2_set_colorformat            },
		{HFI_PROP_LINEAR_STRIDE_SCANLINE,     iris_hfi_gen2_set_linear_stride_scanline },
	};

	if (V4L2_TYPE_IS_OUTPUT(plane)) {
		config_params = core->iris_platform_data->input_config_params;
		config_params_size = core->iris_platform_data->input_config_params_size;
	} else {
		config_params = core->iris_platform_data->output_config_params;
		config_params_size = core->iris_platform_data->output_config_params_size;
	}

	if (!config_params || !config_params_size)
		return -EINVAL;

	for (i = 0; i < config_params_size; i++) {
		for (j = 0; j < ARRAY_SIZE(prop_type_handle_arr); j++) {
			if (prop_type_handle_arr[j].type == config_params[i]) {
				ret = prop_type_handle_arr[j].handle(inst);
				if (ret)
					return ret;
				break;
			}
		}
	}

	return 0;
}

static int iris_hfi_gen2_session_set_codec(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 codec = HFI_CODEC_DECODE_AVC;

	iris_hfi_gen2_packet_session_property(inst,
					      HFI_PROP_CODEC,
					      HFI_HOST_FLAGS_NONE,
					      HFI_PORT_NONE,
					      HFI_PAYLOAD_U32_ENUM,
					      &codec,
					      sizeof(u32));

	return iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
					inst_hfi_gen2->packet->size);
}

static int iris_hfi_gen2_session_set_default_header(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 default_header = false;

	iris_hfi_gen2_packet_session_property(inst,
					      HFI_PROP_DEC_DEFAULT_HEADER,
					      HFI_HOST_FLAGS_NONE,
					      HFI_PORT_BITSTREAM,
					      HFI_PAYLOAD_U32,
					      &default_header,
					      sizeof(u32));

	return iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
					inst_hfi_gen2->packet->size);
}

static int iris_hfi_gen2_session_open(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	int ret;

	if (inst->state != IRIS_INST_DEINIT)
		return -EALREADY;

	inst_hfi_gen2->packet = kzalloc(4096, GFP_KERNEL);
	if (!inst_hfi_gen2->packet)
		return -ENOMEM;

	iris_hfi_gen2_packet_session_command(inst,
					     HFI_CMD_OPEN,
					     HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					     HFI_HOST_FLAGS_INTR_REQUIRED,
					     HFI_PORT_NONE,
					     0,
					     HFI_PAYLOAD_U32,
					     &inst->session_id,
					     sizeof(u32));

	ret = iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
				       inst_hfi_gen2->packet->size);
	if (ret)
		goto fail_free_packet;

	ret = iris_hfi_gen2_session_set_codec(inst);
	if (ret)
		goto fail_free_packet;

	ret = iris_hfi_gen2_session_set_default_header(inst);
	if (ret)
		goto fail_free_packet;

	return 0;

fail_free_packet:
	kfree(inst_hfi_gen2->packet);
	inst_hfi_gen2->packet = NULL;

	return ret;
}

static int iris_hfi_gen2_session_close(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	int ret;

	if (!inst_hfi_gen2->packet)
		return -EINVAL;

	iris_hfi_gen2_packet_session_command(inst,
					     HFI_CMD_CLOSE,
					     (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					     HFI_HOST_FLAGS_INTR_REQUIRED |
					     HFI_HOST_FLAGS_NON_DISCARDABLE),
					     HFI_PORT_NONE,
					     inst->session_id,
					     HFI_PAYLOAD_NONE,
					     NULL,
					     0);

	ret = iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
				       inst_hfi_gen2->packet->size);

	kfree(inst_hfi_gen2->packet);
	inst_hfi_gen2->packet = NULL;

	return ret;
}

static int iris_hfi_gen2_session_start(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);

	iris_hfi_gen2_packet_session_command(inst,
					     HFI_CMD_START,
					     (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					     HFI_HOST_FLAGS_INTR_REQUIRED),
					     iris_hfi_gen2_get_port(plane),
					     inst->session_id,
					     HFI_PAYLOAD_NONE,
					     NULL,
					     0);

	return iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
					inst_hfi_gen2->packet->size);
}

static int iris_hfi_gen2_session_stop(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	int ret = 0;

	reinit_completion(&inst->completion);

	iris_hfi_gen2_packet_session_command(inst,
					     HFI_CMD_STOP,
					     (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					     HFI_HOST_FLAGS_INTR_REQUIRED |
					     HFI_HOST_FLAGS_NON_DISCARDABLE),
					     iris_hfi_gen2_get_port(plane),
					     inst->session_id,
					     HFI_PAYLOAD_NONE,
					     NULL,
					     0);

	ret = iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
				       inst_hfi_gen2->packet->size);
	if (ret)
		return ret;

	return iris_wait_for_session_response(inst, false);
}

static const struct iris_hfi_command_ops iris_hfi_gen2_command_ops = {
	.sys_init = iris_hfi_gen2_sys_init,
	.sys_image_version = iris_hfi_gen2_sys_image_version,
	.sys_interframe_powercollapse = iris_hfi_gen2_sys_interframe_powercollapse,
	.sys_pc_prep = iris_hfi_gen2_sys_pc_prep,
	.session_open = iris_hfi_gen2_session_open,
	.session_set_config_params = iris_hfi_gen2_session_set_config_params,
	.session_set_property = iris_hfi_gen2_session_set_property,
	.session_start = iris_hfi_gen2_session_start,
	.session_stop = iris_hfi_gen2_session_stop,
	.session_close = iris_hfi_gen2_session_close,
};

void iris_hfi_gen2_command_ops_init(struct iris_core *core)
{
	core->hfi_ops = &iris_hfi_gen2_command_ops;
}

struct iris_inst *iris_hfi_gen2_get_instance(void)
{
	return kzalloc(sizeof(struct iris_inst_hfi_gen2), GFP_KERNEL);
}
