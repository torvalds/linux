/*
 * Copyright (C) STMicroelectronics SA 2013
 * Author: Hugues Fruchet <hugues.fruchet@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/slab.h>

#include "delta.h"
#include "delta-ipc.h"
#include "delta-mjpeg.h"
#include "delta-mjpeg-fw.h"

#define DELTA_MJPEG_MAX_RESO DELTA_MAX_RESO

struct delta_mjpeg_ctx {
	/* jpeg header */
	struct mjpeg_header header_struct;
	struct mjpeg_header *header;

	/* ipc */
	void *ipc_hdl;
	struct delta_buf *ipc_buf;

	/* decoded output frame */
	struct delta_frame *out_frame;

	unsigned char str[3000];
};

#define to_ctx(ctx) ((struct delta_mjpeg_ctx *)(ctx)->priv)

static char *ipc_open_param_str(struct jpeg_video_decode_init_params_t *p,
				char *str, unsigned int len)
{
	char *b = str;

	if (!p)
		return "";

	b += snprintf(b, len,
		      "jpeg_video_decode_init_params_t\n"
		      "circular_buffer_begin_addr_p 0x%x\n"
		      "circular_buffer_end_addr_p   0x%x\n",
		      p->circular_buffer_begin_addr_p,
		      p->circular_buffer_end_addr_p);

	return str;
}

static char *ipc_decode_param_str(struct jpeg_decode_params_t *p,
				  char *str, unsigned int len)
{
	char *b = str;

	if (!p)
		return "";

	b += snprintf(b, len,
		      "jpeg_decode_params_t\n"
		      "picture_start_addr_p                  0x%x\n"
		      "picture_end_addr_p                    0x%x\n"
		      "decoding_mode                        %d\n"
		      "display_buffer_addr.display_decimated_luma_p   0x%x\n"
		      "display_buffer_addr.display_decimated_chroma_p 0x%x\n"
		      "main_aux_enable                       %d\n"
		      "additional_flags                     0x%x\n"
		      "field_flag                           %x\n"
		      "is_jpeg_image                        %x\n",
		      p->picture_start_addr_p,
		      p->picture_end_addr_p,
		      p->decoding_mode,
		      p->display_buffer_addr.display_decimated_luma_p,
		      p->display_buffer_addr.display_decimated_chroma_p,
		      p->main_aux_enable, p->additional_flags,
		      p->field_flag,
		      p->is_jpeg_image);

	return str;
}

static inline bool is_stream_error(enum jpeg_decoding_error_t err)
{
	switch (err) {
	case JPEG_DECODER_UNDEFINED_HUFF_TABLE:
	case JPEG_DECODER_BAD_RESTART_MARKER:
	case JPEG_DECODER_BAD_SOS_SPECTRAL:
	case JPEG_DECODER_BAD_SOS_SUCCESSIVE:
	case JPEG_DECODER_BAD_HEADER_LENGTH:
	case JPEG_DECODER_BAD_COUNT_VALUE:
	case JPEG_DECODER_BAD_DHT_MARKER:
	case JPEG_DECODER_BAD_INDEX_VALUE:
	case JPEG_DECODER_BAD_NUMBER_HUFFMAN_TABLES:
	case JPEG_DECODER_BAD_QUANT_TABLE_LENGTH:
	case JPEG_DECODER_BAD_NUMBER_QUANT_TABLES:
	case JPEG_DECODER_BAD_COMPONENT_COUNT:
		return true;
	default:
		return false;
	}
}

static inline const char *err_str(enum jpeg_decoding_error_t err)
{
	switch (err) {
	case JPEG_DECODER_NO_ERROR:
		return "JPEG_DECODER_NO_ERROR";
	case JPEG_DECODER_UNDEFINED_HUFF_TABLE:
		return "JPEG_DECODER_UNDEFINED_HUFF_TABLE";
	case JPEG_DECODER_UNSUPPORTED_MARKER:
		return "JPEG_DECODER_UNSUPPORTED_MARKER";
	case JPEG_DECODER_UNABLE_ALLOCATE_MEMORY:
		return "JPEG_DECODER_UNABLE_ALLOCATE_MEMORY";
	case JPEG_DECODER_NON_SUPPORTED_SAMP_FACTORS:
		return "JPEG_DECODER_NON_SUPPORTED_SAMP_FACTORS";
	case JPEG_DECODER_BAD_PARAMETER:
		return "JPEG_DECODER_BAD_PARAMETER";
	case JPEG_DECODER_DECODE_ERROR:
		return "JPEG_DECODER_DECODE_ERROR";
	case JPEG_DECODER_BAD_RESTART_MARKER:
		return "JPEG_DECODER_BAD_RESTART_MARKER";
	case JPEG_DECODER_UNSUPPORTED_COLORSPACE:
		return "JPEG_DECODER_UNSUPPORTED_COLORSPACE";
	case JPEG_DECODER_BAD_SOS_SPECTRAL:
		return "JPEG_DECODER_BAD_SOS_SPECTRAL";
	case JPEG_DECODER_BAD_SOS_SUCCESSIVE:
		return "JPEG_DECODER_BAD_SOS_SUCCESSIVE";
	case JPEG_DECODER_BAD_HEADER_LENGTH:
		return "JPEG_DECODER_BAD_HEADER_LENGTH";
	case JPEG_DECODER_BAD_COUNT_VALUE:
		return "JPEG_DECODER_BAD_COUNT_VALUE";
	case JPEG_DECODER_BAD_DHT_MARKER:
		return "JPEG_DECODER_BAD_DHT_MARKER";
	case JPEG_DECODER_BAD_INDEX_VALUE:
		return "JPEG_DECODER_BAD_INDEX_VALUE";
	case JPEG_DECODER_BAD_NUMBER_HUFFMAN_TABLES:
		return "JPEG_DECODER_BAD_NUMBER_HUFFMAN_TABLES";
	case JPEG_DECODER_BAD_QUANT_TABLE_LENGTH:
		return "JPEG_DECODER_BAD_QUANT_TABLE_LENGTH";
	case JPEG_DECODER_BAD_NUMBER_QUANT_TABLES:
		return "JPEG_DECODER_BAD_NUMBER_QUANT_TABLES";
	case JPEG_DECODER_BAD_COMPONENT_COUNT:
		return "JPEG_DECODER_BAD_COMPONENT_COUNT";
	case JPEG_DECODER_DIVIDE_BY_ZERO_ERROR:
		return "JPEG_DECODER_DIVIDE_BY_ZERO_ERROR";
	case JPEG_DECODER_NOT_JPG_IMAGE:
		return "JPEG_DECODER_NOT_JPG_IMAGE";
	case JPEG_DECODER_UNSUPPORTED_ROTATION_ANGLE:
		return "JPEG_DECODER_UNSUPPORTED_ROTATION_ANGLE";
	case JPEG_DECODER_UNSUPPORTED_SCALING:
		return "JPEG_DECODER_UNSUPPORTED_SCALING";
	case JPEG_DECODER_INSUFFICIENT_OUTPUTBUFFER_SIZE:
		return "JPEG_DECODER_INSUFFICIENT_OUTPUTBUFFER_SIZE";
	case JPEG_DECODER_BAD_HWCFG_GP_VERSION_VALUE:
		return "JPEG_DECODER_BAD_HWCFG_GP_VERSION_VALUE";
	case JPEG_DECODER_BAD_VALUE_FROM_RED:
		return "JPEG_DECODER_BAD_VALUE_FROM_RED";
	case JPEG_DECODER_BAD_SUBREGION_PARAMETERS:
		return "JPEG_DECODER_BAD_SUBREGION_PARAMETERS";
	case JPEG_DECODER_PROGRESSIVE_DECODE_NOT_SUPPORTED:
		return "JPEG_DECODER_PROGRESSIVE_DECODE_NOT_SUPPORTED";
	case JPEG_DECODER_ERROR_TASK_TIMEOUT:
		return "JPEG_DECODER_ERROR_TASK_TIMEOUT";
	case JPEG_DECODER_ERROR_FEATURE_NOT_SUPPORTED:
		return "JPEG_DECODER_ERROR_FEATURE_NOT_SUPPORTED";
	default:
		return "!unknown MJPEG error!";
	}
}

static bool delta_mjpeg_check_status(struct delta_ctx *pctx,
				     struct jpeg_decode_return_params_t *status)
{
	struct delta_dev *delta = pctx->dev;
	bool dump = false;

	if (status->error_code == JPEG_DECODER_NO_ERROR)
		goto out;

	if (is_stream_error(status->error_code)) {
		dev_warn_ratelimited(delta->dev,
				     "%s  firmware: stream error @ frame %d (%s)\n",
				     pctx->name, pctx->decoded_frames,
				     err_str(status->error_code));
		pctx->stream_errors++;
	} else {
		dev_warn_ratelimited(delta->dev,
				     "%s  firmware: decode error @ frame %d (%s)\n",
				     pctx->name, pctx->decoded_frames,
				     err_str(status->error_code));
		pctx->decode_errors++;
		dump = true;
	}

out:
	dev_dbg(delta->dev,
		"%s  firmware: decoding time(us)=%d\n", pctx->name,
		status->decode_time_in_us);

	return dump;
}

static int delta_mjpeg_ipc_open(struct delta_ctx *pctx)
{
	struct delta_dev *delta = pctx->dev;
	struct delta_mjpeg_ctx *ctx = to_ctx(pctx);
	int ret = 0;
	struct jpeg_video_decode_init_params_t params_struct;
	struct jpeg_video_decode_init_params_t *params = &params_struct;
	struct delta_buf *ipc_buf;
	u32 ipc_buf_size;
	struct delta_ipc_param ipc_param;
	void *hdl;

	memset(params, 0, sizeof(*params));
	params->circular_buffer_begin_addr_p = 0x00000000;
	params->circular_buffer_end_addr_p = 0xffffffff;

	dev_vdbg(delta->dev,
		 "%s  %s\n", pctx->name,
		 ipc_open_param_str(params, ctx->str, sizeof(ctx->str)));

	ipc_param.size = sizeof(*params);
	ipc_param.data = params;
	ipc_buf_size = sizeof(struct jpeg_decode_params_t) +
	    sizeof(struct jpeg_decode_return_params_t);
	ret = delta_ipc_open(pctx, "JPEG_DECODER_HW0", &ipc_param,
			     ipc_buf_size, &ipc_buf, &hdl);
	if (ret) {
		dev_err(delta->dev,
			"%s  dumping command %s\n", pctx->name,
			ipc_open_param_str(params, ctx->str, sizeof(ctx->str)));
		return ret;
	}

	ctx->ipc_buf = ipc_buf;
	ctx->ipc_hdl = hdl;

	return 0;
}

static int delta_mjpeg_ipc_decode(struct delta_ctx *pctx, struct delta_au *au)
{
	struct delta_dev *delta = pctx->dev;
	struct delta_mjpeg_ctx *ctx = to_ctx(pctx);
	int ret = 0;
	struct jpeg_decode_params_t *params = ctx->ipc_buf->vaddr;
	struct jpeg_decode_return_params_t *status =
	    ctx->ipc_buf->vaddr + sizeof(*params);
	struct delta_frame *frame;
	struct delta_ipc_param ipc_param, ipc_status;

	ret = delta_get_free_frame(pctx, &frame);
	if (ret)
		return ret;

	memset(params, 0, sizeof(*params));

	params->picture_start_addr_p = (u32)(au->paddr);
	params->picture_end_addr_p = (u32)(au->paddr + au->size - 1);

	/*
	 * !WARNING!
	 * the NV12 decoded frame is only available
	 * on decimated output when enabling flag
	 * "JPEG_ADDITIONAL_FLAG_420MB"...
	 * the non decimated output gives YUV422SP
	 */
	params->main_aux_enable = JPEG_DISP_AUX_EN;
	params->additional_flags = JPEG_ADDITIONAL_FLAG_420MB;
	params->horizontal_decimation_factor = JPEG_HDEC_1;
	params->vertical_decimation_factor = JPEG_VDEC_1;
	params->decoding_mode = JPEG_NORMAL_DECODE;

	params->display_buffer_addr.struct_size =
	    sizeof(struct jpeg_display_buffer_address_t);
	params->display_buffer_addr.display_decimated_luma_p =
	    (u32)frame->paddr;
	params->display_buffer_addr.display_decimated_chroma_p =
	    (u32)(frame->paddr
		  + frame->info.aligned_width * frame->info.aligned_height);

	dev_vdbg(delta->dev,
		 "%s  %s\n", pctx->name,
		 ipc_decode_param_str(params, ctx->str, sizeof(ctx->str)));

	/* status */
	memset(status, 0, sizeof(*status));
	status->error_code = JPEG_DECODER_NO_ERROR;

	ipc_param.size = sizeof(*params);
	ipc_param.data = params;
	ipc_status.size = sizeof(*status);
	ipc_status.data = status;
	ret = delta_ipc_decode(ctx->ipc_hdl, &ipc_param, &ipc_status);
	if (ret) {
		dev_err(delta->dev,
			"%s  dumping command %s\n", pctx->name,
			ipc_decode_param_str(params, ctx->str,
					     sizeof(ctx->str)));
		return ret;
	}

	pctx->decoded_frames++;

	/* check firmware decoding status */
	if (delta_mjpeg_check_status(pctx, status)) {
		dev_err(delta->dev,
			"%s  dumping command %s\n", pctx->name,
			ipc_decode_param_str(params, ctx->str,
					     sizeof(ctx->str)));
	}

	frame->field = V4L2_FIELD_NONE;
	frame->flags = V4L2_BUF_FLAG_KEYFRAME;
	frame->state |= DELTA_FRAME_DEC;

	ctx->out_frame = frame;

	return 0;
}

static int delta_mjpeg_open(struct delta_ctx *pctx)
{
	struct delta_mjpeg_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	pctx->priv = ctx;

	return 0;
}

static int delta_mjpeg_close(struct delta_ctx *pctx)
{
	struct delta_mjpeg_ctx *ctx = to_ctx(pctx);

	if (ctx->ipc_hdl) {
		delta_ipc_close(ctx->ipc_hdl);
		ctx->ipc_hdl = NULL;
	}

	kfree(ctx);

	return 0;
}

static int delta_mjpeg_get_streaminfo(struct delta_ctx *pctx,
				      struct delta_streaminfo *streaminfo)
{
	struct delta_mjpeg_ctx *ctx = to_ctx(pctx);

	if (!ctx->header)
		goto nodata;

	streaminfo->streamformat = V4L2_PIX_FMT_MJPEG;
	streaminfo->width = ctx->header->frame_width;
	streaminfo->height = ctx->header->frame_height;

	/* progressive stream */
	streaminfo->field = V4L2_FIELD_NONE;

	streaminfo->dpb = 1;

	return 0;

nodata:
	return -ENODATA;
}

static int delta_mjpeg_decode(struct delta_ctx *pctx, struct delta_au *pau)
{
	struct delta_dev *delta = pctx->dev;
	struct delta_mjpeg_ctx *ctx = to_ctx(pctx);
	int ret;
	struct delta_au au = *pau;
	unsigned int data_offset;
	struct mjpeg_header *header = &ctx->header_struct;

	if (!ctx->header) {
		ret = delta_mjpeg_read_header(pctx, au.vaddr, au.size,
					      header, &data_offset);
		if (ret) {
			pctx->stream_errors++;
			goto err;
		}
		if (header->frame_width * header->frame_height >
		    DELTA_MJPEG_MAX_RESO) {
			dev_err(delta->dev,
				"%s  stream resolution too large: %dx%d > %d pixels budget\n",
				pctx->name,
				header->frame_width,
				header->frame_height, DELTA_MJPEG_MAX_RESO);
			ret = -EINVAL;
			goto err;
		}
		ctx->header = header;
		goto out;
	}

	if (!ctx->ipc_hdl) {
		ret = delta_mjpeg_ipc_open(pctx);
		if (ret)
			goto err;
	}

	ret = delta_mjpeg_read_header(pctx, au.vaddr, au.size,
				      ctx->header, &data_offset);
	if (ret) {
		pctx->stream_errors++;
		goto err;
	}

	au.paddr += data_offset;
	au.vaddr += data_offset;

	ret = delta_mjpeg_ipc_decode(pctx, &au);
	if (ret)
		goto err;

out:
	return 0;

err:
	return ret;
}

static int delta_mjpeg_get_frame(struct delta_ctx *pctx,
				 struct delta_frame **frame)
{
	struct delta_mjpeg_ctx *ctx = to_ctx(pctx);

	if (!ctx->out_frame)
		return -ENODATA;

	*frame = ctx->out_frame;

	ctx->out_frame = NULL;

	return 0;
}

const struct delta_dec mjpegdec = {
	.name = "MJPEG",
	.streamformat = V4L2_PIX_FMT_MJPEG,
	.pixelformat = V4L2_PIX_FMT_NV12,
	.open = delta_mjpeg_open,
	.close = delta_mjpeg_close,
	.get_streaminfo = delta_mjpeg_get_streaminfo,
	.get_frameinfo = delta_get_frameinfo_default,
	.decode = delta_mjpeg_decode,
	.get_frame = delta_mjpeg_get_frame,
	.recycle = delta_recycle_default,
};
