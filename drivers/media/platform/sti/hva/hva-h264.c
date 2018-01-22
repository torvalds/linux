/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Yannick Fertre <yannick.fertre@st.com>
 *          Hugues Fruchet <hugues.fruchet@st.com>
 * License terms:  GNU General Public License (GPL), version 2
 */

#include "hva.h"
#include "hva-hw.h"

#define MAX_SPS_PPS_SIZE 128

#define BITSTREAM_OFFSET_MASK 0x7F

/* video max size*/
#define H264_MAX_SIZE_W 1920
#define H264_MAX_SIZE_H 1920

/* macroBlocs number (width & height) */
#define MB_W(w) ((w + 0xF)  / 0x10)
#define MB_H(h) ((h + 0xF)  / 0x10)

/* formula to get temporal or spatial data size */
#define DATA_SIZE(w, h) (MB_W(w) * MB_H(h) * 16)

#define SEARCH_WINDOW_BUFFER_MAX_SIZE(w) ((4 * MB_W(w) + 42) * 256 * 3 / 2)
#define CABAC_CONTEXT_BUFFER_MAX_SIZE(w) (MB_W(w) * 16)
#define CTX_MB_BUFFER_MAX_SIZE(w) (MB_W(w) * 16 * 8)
#define SLICE_HEADER_SIZE (4 * 16)
#define BRC_DATA_SIZE (5 * 16)

/* source buffer copy in YUV 420 MB-tiled format with size=16*256*3/2 */
#define CURRENT_WINDOW_BUFFER_MAX_SIZE (16 * 256 * 3 / 2)

/*
 * 4 lines of pixels (in Luma, Chroma blue and Chroma red) of top MB
 * for deblocking with size=4*16*MBx*2
 */
#define LOCAL_RECONSTRUCTED_BUFFER_MAX_SIZE(w) (4 * 16 * MB_W(w) * 2)

/* factor for bitrate and cpb buffer size max values if profile >= high */
#define H264_FACTOR_HIGH 1200

/* factor for bitrate and cpb buffer size max values if profile < high */
#define H264_FACTOR_BASELINE 1000

/* number of bytes for NALU_TYPE_FILLER_DATA header and footer */
#define H264_FILLER_DATA_SIZE 6

struct h264_profile {
	enum v4l2_mpeg_video_h264_level level;
	u32 max_mb_per_seconds;
	u32 max_frame_size;
	u32 max_bitrate;
	u32 max_cpb_size;
	u32 min_comp_ratio;
};

static const struct h264_profile h264_infos_list[] = {
	{V4L2_MPEG_VIDEO_H264_LEVEL_1_0, 1485, 99, 64, 175, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_1B, 1485, 99, 128, 350, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_1_1, 3000, 396, 192, 500, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_1_2, 6000, 396, 384, 1000, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_1_3, 11880, 396, 768, 2000, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_2_0, 11880, 396, 2000, 2000, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_2_1, 19800, 792, 4000, 4000, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_2_2, 20250, 1620, 4000, 4000, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_3_0, 40500, 1620, 10000, 10000, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_3_1, 108000, 3600, 14000, 14000, 4},
	{V4L2_MPEG_VIDEO_H264_LEVEL_3_2, 216000, 5120, 20000, 20000, 4},
	{V4L2_MPEG_VIDEO_H264_LEVEL_4_0, 245760, 8192, 20000, 25000, 4},
	{V4L2_MPEG_VIDEO_H264_LEVEL_4_1, 245760, 8192, 50000, 62500, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_4_2, 522240, 8704, 50000, 62500, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_5_0, 589824, 22080, 135000, 135000, 2},
	{V4L2_MPEG_VIDEO_H264_LEVEL_5_1, 983040, 36864, 240000, 240000, 2}
};

enum hva_brc_type {
	BRC_TYPE_NONE = 0,
	BRC_TYPE_CBR = 1,
	BRC_TYPE_VBR = 2,
	BRC_TYPE_VBR_LOW_DELAY = 3
};

enum hva_entropy_coding_mode {
	CAVLC = 0,
	CABAC = 1
};

enum hva_picture_coding_type {
	PICTURE_CODING_TYPE_I = 0,
	PICTURE_CODING_TYPE_P = 1,
	PICTURE_CODING_TYPE_B = 2
};

enum hva_h264_sampling_mode {
	SAMPLING_MODE_NV12 = 0,
	SAMPLING_MODE_UYVY = 1,
	SAMPLING_MODE_RGB3 = 3,
	SAMPLING_MODE_XRGB4 = 4,
	SAMPLING_MODE_NV21 = 8,
	SAMPLING_MODE_VYUY = 9,
	SAMPLING_MODE_BGR3 = 11,
	SAMPLING_MODE_XBGR4 = 12,
	SAMPLING_MODE_RGBX4 = 20,
	SAMPLING_MODE_BGRX4 = 28
};

enum hva_h264_nalu_type {
	NALU_TYPE_UNKNOWN = 0,
	NALU_TYPE_SLICE = 1,
	NALU_TYPE_SLICE_DPA = 2,
	NALU_TYPE_SLICE_DPB = 3,
	NALU_TYPE_SLICE_DPC = 4,
	NALU_TYPE_SLICE_IDR = 5,
	NALU_TYPE_SEI = 6,
	NALU_TYPE_SPS = 7,
	NALU_TYPE_PPS = 8,
	NALU_TYPE_AU_DELIMITER = 9,
	NALU_TYPE_SEQ_END = 10,
	NALU_TYPE_STREAM_END = 11,
	NALU_TYPE_FILLER_DATA = 12,
	NALU_TYPE_SPS_EXT = 13,
	NALU_TYPE_PREFIX_UNIT = 14,
	NALU_TYPE_SUBSET_SPS = 15,
	NALU_TYPE_SLICE_AUX = 19,
	NALU_TYPE_SLICE_EXT = 20
};

enum hva_h264_sei_payload_type {
	SEI_BUFFERING_PERIOD = 0,
	SEI_PICTURE_TIMING = 1,
	SEI_STEREO_VIDEO_INFO = 21,
	SEI_FRAME_PACKING_ARRANGEMENT = 45
};

/*
 * stereo Video Info struct
 */
struct hva_h264_stereo_video_sei {
	u8 field_views_flag;
	u8 top_field_is_left_view_flag;
	u8 current_frame_is_left_view_flag;
	u8 next_frame_is_second_view_flag;
	u8 left_view_self_contained_flag;
	u8 right_view_self_contained_flag;
};

/*
 * struct hva_h264_td
 *
 * @frame_width: width in pixels of the buffer containing the input frame
 * @frame_height: height in pixels of the buffer containing the input frame
 * @frame_num: the parameter to be written in the slice header
 * @picture_coding_type: type I, P or B
 * @pic_order_cnt_type: POC mode, as defined in H264 std : can be 0,1,2
 * @first_picture_in_sequence: flag telling to encoder that this is the
 *			       first picture in a video sequence.
 *			       Used for VBR
 * @slice_size_type: 0 = no constraint to close the slice
 *		     1= a slice is closed as soon as the slice_mb_size limit
 *			is reached
 *		     2= a slice is closed as soon as the slice_byte_size limit
 *			is reached
 *		     3= a slice is closed as soon as either the slice_byte_size
 *			limit or the slice_mb_size limit is reached
 * @slice_mb_size: defines the slice size in number of macroblocks
 *		   (used when slice_size_type=1 or slice_size_type=3)
 * @ir_param_option: defines the number of macroblocks per frame to be
 *		     refreshed by AIR algorithm OR the refresh period
 *		     by CIR algorithm
 * @intra_refresh_type: enables the adaptive intra refresh algorithm.
 *			Disable=0 / Adaptative=1 and Cycle=2 as intra refresh
 * @use_constrained_intra_flag: constrained_intra_pred_flag from PPS
 * @transform_mode: controls the use of 4x4/8x8 transform mode
 * @disable_deblocking_filter_idc:
 *		     0: specifies that all luma and chroma block edges of
 *			the slice are filtered.
 *		     1: specifies that deblocking is disabled for all block
 *			edges of the slice.
 *		     2: specifies that all luma and chroma block edges of
 *			the slice are filtered with exception of the block edges
 *			that coincide with slice boundaries
 * @slice_alpha_c0_offset_div2: to be written in slice header,
 *				controls deblocking
 * @slice_beta_offset_div2: to be written in slice header,
 *			    controls deblocking
 * @encoder_complexity: encoder complexity control (IME).
 *		     0 = I_16x16, P_16x16, Full ME Complexity
 *		     1 = I_16x16, I_NxN, P_16x16, Full ME Complexity
 *		     2 = I_16x16, I_NXN, P_16x16, P_WxH, Full ME Complexity
 *		     4 = I_16x16, P_16x16, Reduced ME Complexity
 *		     5 = I_16x16, I_NxN, P_16x16, Reduced ME Complexity
 *		     6 = I_16x16, I_NXN, P_16x16, P_WxH, Reduced ME Complexity
 *  @chroma_qp_index_offset: coming from picture parameter set
 *			     (PPS see [H.264 STD] 7.4.2.2)
 *  @entropy_coding_mode: entropy coding mode.
 *			  0 = CAVLC
 *			  1 = CABAC
 * @brc_type: selects the bit-rate control algorithm
 *		     0 = constant Qp, (no BRC)
 *		     1 = CBR
 *		     2 = VBR
 * @quant: Quantization param used in case of fix QP encoding (no BRC)
 * @non_VCL_NALU_Size: size of non-VCL NALUs (SPS, PPS, filler),
 *		       used by BRC
 * @cpb_buffer_size: size of Coded Picture Buffer, used by BRC
 * @bit_rate: target bitrate, for BRC
 * @qp_min: min QP threshold
 * @qp_max: max QP threshold
 * @framerate_num: target framerate numerator , used by BRC
 * @framerate_den: target framerate denomurator , used by BRC
 * @delay: End-to-End Initial Delay
 * @strict_HRD_compliancy: flag for HDR compliancy (1)
 *			   May impact quality encoding
 * @addr_source_buffer: address of input frame buffer for current frame
 * @addr_fwd_Ref_Buffer: address of reference frame buffer
 * @addr_rec_buffer: address of reconstructed frame buffer
 * @addr_output_bitstream_start: output bitstream start address
 * @addr_output_bitstream_end: output bitstream end address
 * @addr_external_sw : address of external search window
 * @addr_lctx : address of context picture buffer
 * @addr_local_rec_buffer: address of local reconstructed buffer
 * @addr_spatial_context: address of spatial context buffer
 * @bitstream_offset: offset in bits between aligned bitstream start
 *		      address and first bit to be written by HVA.
 *		      Range value is [0..63]
 * @sampling_mode: Input picture format .
 *		     0: YUV420 semi_planar Interleaved
 *		     1: YUV422 raster Interleaved
 * @addr_param_out: address of output parameters structure
 * @addr_scaling_matrix: address to the coefficient of
 *			 the inverse scaling matrix
 * @addr_scaling_matrix_dir: address to the coefficient of
 *			     the direct scaling matrix
 * @addr_cabac_context_buffer: address of cabac context buffer
 * @GmvX: Input information about the horizontal global displacement of
 *	  the encoded frame versus the previous one
 * @GmvY: Input information about the vertical global displacement of
 *	  the encoded frame versus the previous one
 * @window_width: width in pixels of the window to be encoded inside
 *		  the input frame
 * @window_height: width in pixels of the window to be encoded inside
 *		   the input frame
 * @window_horizontal_offset: horizontal offset in pels for input window
 *			      within input frame
 * @window_vertical_offset: vertical offset in pels for input window
 *			    within input frame
 * @addr_roi: Map of QP offset for the Region of Interest algorithm and
 *	      also used for Error map.
 *	      Bit 0-6 used for qp offset (value -64 to 63).
 *	      Bit 7 used to force intra
 * @addr_slice_header: address to slice header
 * @slice_header_size_in_bits: size in bits of the Slice header
 * @slice_header_offset0: Slice header offset where to insert
 *			  first_Mb_in_slice
 * @slice_header_offset1: Slice header offset where to insert
 *			  slice_qp_delta
 * @slice_header_offset2: Slice header offset where to insert
 *			  num_MBs_in_slice
 * @slice_synchro_enable: enable "slice ready" interrupt after each slice
 * @max_slice_number: Maximum number of slice in a frame
 *		      (0 is strictly forbidden)
 * @rgb2_yuv_y_coeff: Four coefficients (C0C1C2C3) to convert from RGB to
 *		      YUV for the Y component.
 *		      Y = C0*R + C1*G + C2*B + C3 (C0 is on byte 0)
 * @rgb2_yuv_u_coeff: four coefficients (C0C1C2C3) to convert from RGB to
 *		      YUV for the Y component.
 *		      Y = C0*R + C1*G + C2*B + C3 (C0 is on byte 0)
 * @rgb2_yuv_v_coeff: Four coefficients (C0C1C2C3) to convert from RGB to
 *		      YUV for the U (Cb) component.
 *		      U = C0*R + C1*G + C2*B + C3 (C0 is on byte 0)
 * @slice_byte_size: maximum slice size in bytes
 *		     (used when slice_size_type=2 or slice_size_type=3)
 * @max_air_intra_mb_nb: Maximum number of intra macroblock in a frame
 *			 for the AIR algorithm
 * @brc_no_skip: Disable skipping in the Bitrate Controller
 * @addr_brc_in_out_parameter: address of static buffer for BRC parameters
 */
struct hva_h264_td {
	u16 frame_width;
	u16 frame_height;
	u32 frame_num;
	u16 picture_coding_type;
	u16 reserved1;
	u16 pic_order_cnt_type;
	u16 first_picture_in_sequence;
	u16 slice_size_type;
	u16 reserved2;
	u32 slice_mb_size;
	u16 ir_param_option;
	u16 intra_refresh_type;
	u16 use_constrained_intra_flag;
	u16 transform_mode;
	u16 disable_deblocking_filter_idc;
	s16 slice_alpha_c0_offset_div2;
	s16 slice_beta_offset_div2;
	u16 encoder_complexity;
	s16 chroma_qp_index_offset;
	u16 entropy_coding_mode;
	u16 brc_type;
	u16 quant;
	u32 non_vcl_nalu_size;
	u32 cpb_buffer_size;
	u32 bit_rate;
	u16 qp_min;
	u16 qp_max;
	u16 framerate_num;
	u16 framerate_den;
	u16 delay;
	u16 strict_hrd_compliancy;
	u32 addr_source_buffer;
	u32 addr_fwd_ref_buffer;
	u32 addr_rec_buffer;
	u32 addr_output_bitstream_start;
	u32 addr_output_bitstream_end;
	u32 addr_external_sw;
	u32 addr_lctx;
	u32 addr_local_rec_buffer;
	u32 addr_spatial_context;
	u16 bitstream_offset;
	u16 sampling_mode;
	u32 addr_param_out;
	u32 addr_scaling_matrix;
	u32 addr_scaling_matrix_dir;
	u32 addr_cabac_context_buffer;
	u32 reserved3;
	u32 reserved4;
	s16 gmv_x;
	s16 gmv_y;
	u16 window_width;
	u16 window_height;
	u16 window_horizontal_offset;
	u16 window_vertical_offset;
	u32 addr_roi;
	u32 addr_slice_header;
	u16 slice_header_size_in_bits;
	u16 slice_header_offset0;
	u16 slice_header_offset1;
	u16 slice_header_offset2;
	u32 reserved5;
	u32 reserved6;
	u16 reserved7;
	u16 reserved8;
	u16 slice_synchro_enable;
	u16 max_slice_number;
	u32 rgb2_yuv_y_coeff;
	u32 rgb2_yuv_u_coeff;
	u32 rgb2_yuv_v_coeff;
	u32 slice_byte_size;
	u16 max_air_intra_mb_nb;
	u16 brc_no_skip;
	u32 addr_temporal_context;
	u32 addr_brc_in_out_parameter;
};

/*
 * struct hva_h264_slice_po
 *
 * @ slice_size: slice size
 * @ slice_start_time: start time
 * @ slice_stop_time: stop time
 * @ slice_num: slice number
 */
struct hva_h264_slice_po {
	u32 slice_size;
	u32 slice_start_time;
	u32 slice_end_time;
	u32 slice_num;
};

/*
 * struct hva_h264_po
 *
 * @ bitstream_size: bitstream size
 * @ dct_bitstream_size: dtc bitstream size
 * @ stuffing_bits: number of stuffing bits inserted by the encoder
 * @ removal_time: removal time of current frame (nb of ticks 1/framerate)
 * @ hvc_start_time: hvc start time
 * @ hvc_stop_time: hvc stop time
 * @ slice_count: slice count
 */
struct hva_h264_po {
	u32 bitstream_size;
	u32 dct_bitstream_size;
	u32 stuffing_bits;
	u32 removal_time;
	u32 hvc_start_time;
	u32 hvc_stop_time;
	u32 slice_count;
	u32 reserved0;
	struct hva_h264_slice_po slice_params[16];
};

struct hva_h264_task {
	struct hva_h264_td td;
	struct hva_h264_po po;
};

/*
 * struct hva_h264_ctx
 *
 * @seq_info:  sequence information buffer
 * @ref_frame: reference frame buffer
 * @rec_frame: reconstructed frame buffer
 * @task:      task descriptor
 */
struct hva_h264_ctx {
	struct hva_buffer *seq_info;
	struct hva_buffer *ref_frame;
	struct hva_buffer *rec_frame;
	struct hva_buffer *task;
};

static int hva_h264_fill_slice_header(struct hva_ctx *pctx,
				      u8 *slice_header_addr,
				      struct hva_controls *ctrls,
				      int frame_num,
				      u16 *header_size,
				      u16 *header_offset0,
				      u16 *header_offset1,
				      u16 *header_offset2)
{
	/*
	 * with this HVA hardware version, part of the slice header is computed
	 * on host and part by hardware.
	 * The part of host is precomputed and available through this array.
	 */
	struct device *dev = ctx_to_dev(pctx);
	int  cabac = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC;
	const unsigned char slice_header[] = { 0x00, 0x00, 0x00, 0x01,
					       0x41, 0x34, 0x07, 0x00};
	int idr_pic_id = frame_num % 2;
	enum hva_picture_coding_type type;
	u32 frame_order = frame_num % ctrls->gop_size;

	if (!(frame_num % ctrls->gop_size))
		type = PICTURE_CODING_TYPE_I;
	else
		type = PICTURE_CODING_TYPE_P;

	memcpy(slice_header_addr, slice_header, sizeof(slice_header));

	*header_size = 56;
	*header_offset0 = 40;
	*header_offset1 = 13;
	*header_offset2 = 0;

	if (type == PICTURE_CODING_TYPE_I) {
		slice_header_addr[4] = 0x65;
		slice_header_addr[5] = 0x11;

		/* toggle the I frame */
		if ((frame_num / ctrls->gop_size) % 2) {
			*header_size += 4;
			*header_offset1 += 4;
			slice_header_addr[6] = 0x04;
			slice_header_addr[7] = 0x70;

		} else {
			*header_size += 2;
			*header_offset1 += 2;
			slice_header_addr[6] = 0x09;
			slice_header_addr[7] = 0xC0;
		}
	} else {
		if (ctrls->entropy_mode == cabac) {
			*header_size += 1;
			*header_offset1 += 1;
			slice_header_addr[7] = 0x80;
		}
		/*
		 * update slice header with P frame order
		 * frame order is limited to 16 (coded on 4bits only)
		 */
		slice_header_addr[5] += ((frame_order & 0x0C) >> 2);
		slice_header_addr[6] += ((frame_order & 0x03) << 6);
	}

	dev_dbg(dev,
		"%s   %s slice header order %d idrPicId %d header size %d\n",
		pctx->name, __func__, frame_order, idr_pic_id, *header_size);
	return 0;
}

static int hva_h264_fill_data_nal(struct hva_ctx *pctx,
				  unsigned int stuffing_bytes, u8 *addr,
				  unsigned int stream_size, unsigned int *size)
{
	struct device *dev = ctx_to_dev(pctx);
	const u8 start[] = { 0x00, 0x00, 0x00, 0x01 };

	dev_dbg(dev, "%s   %s stuffing bytes %d\n", pctx->name, __func__,
		stuffing_bytes);

	if ((*size + stuffing_bytes + H264_FILLER_DATA_SIZE) > stream_size) {
		dev_dbg(dev, "%s   %s too many stuffing bytes %d\n",
			pctx->name, __func__, stuffing_bytes);
		return 0;
	}

	/* start code */
	memcpy(addr + *size, start, sizeof(start));
	*size += sizeof(start);

	/* nal_unit_type */
	addr[*size] = NALU_TYPE_FILLER_DATA;
	*size += 1;

	memset(addr + *size, 0xff, stuffing_bytes);
	*size += stuffing_bytes;

	addr[*size] = 0x80;
	*size += 1;

	return 0;
}

static int hva_h264_fill_sei_nal(struct hva_ctx *pctx,
				 enum hva_h264_sei_payload_type type,
				 u8 *addr, u32 *size)
{
	struct device *dev = ctx_to_dev(pctx);
	const u8 start[] = { 0x00, 0x00, 0x00, 0x01 };
	struct hva_h264_stereo_video_sei info;
	u8 offset = 7;
	u8 msg = 0;

	/* start code */
	memcpy(addr + *size, start, sizeof(start));
	*size += sizeof(start);

	/* nal_unit_type */
	addr[*size] = NALU_TYPE_SEI;
	*size += 1;

	/* payload type */
	addr[*size] = type;
	*size += 1;

	switch (type) {
	case SEI_STEREO_VIDEO_INFO:
		memset(&info, 0, sizeof(info));

		/* set to top/bottom frame packing arrangement */
		info.field_views_flag = 1;
		info.top_field_is_left_view_flag = 1;

		/* payload size */
		addr[*size] = 1;
		*size += 1;

		/* payload */
		msg = info.field_views_flag << offset--;

		if (info.field_views_flag) {
			msg |= info.top_field_is_left_view_flag <<
			       offset--;
		} else {
			msg |= info.current_frame_is_left_view_flag <<
			       offset--;
			msg |= info.next_frame_is_second_view_flag <<
			       offset--;
		}
		msg |= info.left_view_self_contained_flag << offset--;
		msg |= info.right_view_self_contained_flag << offset--;

		addr[*size] = msg;
		*size += 1;

		addr[*size] = 0x80;
		*size += 1;

		return 0;
	case SEI_BUFFERING_PERIOD:
	case SEI_PICTURE_TIMING:
	case SEI_FRAME_PACKING_ARRANGEMENT:
	default:
		dev_err(dev, "%s   sei nal type not supported %d\n",
			pctx->name, type);
		return -EINVAL;
	}
}

static int hva_h264_prepare_task(struct hva_ctx *pctx,
				 struct hva_h264_task *task,
				 struct hva_frame *frame,
				 struct hva_stream *stream)
{
	struct hva_dev *hva = ctx_to_hdev(pctx);
	struct device *dev = ctx_to_dev(pctx);
	struct hva_h264_ctx *ctx = (struct hva_h264_ctx *)pctx->priv;
	struct hva_buffer *seq_info = ctx->seq_info;
	struct hva_buffer *fwd_ref_frame = ctx->ref_frame;
	struct hva_buffer *loc_rec_frame = ctx->rec_frame;
	struct hva_h264_td *td = &task->td;
	struct hva_controls *ctrls = &pctx->ctrls;
	struct v4l2_fract *time_per_frame = &pctx->ctrls.time_per_frame;
	int cavlc =  V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC;
	u32 frame_num = pctx->stream_num;
	u32 addr_esram = hva->esram_addr;
	enum v4l2_mpeg_video_h264_level level;
	dma_addr_t paddr = 0;
	u8 *slice_header_vaddr;
	u32 frame_width = frame->info.aligned_width;
	u32 frame_height = frame->info.aligned_height;
	u32 max_cpb_buffer_size;
	unsigned int payload = stream->bytesused;
	u32 max_bitrate;

	/* check width and height parameters */
	if ((frame_width > max(H264_MAX_SIZE_W, H264_MAX_SIZE_H)) ||
	    (frame_height > max(H264_MAX_SIZE_W, H264_MAX_SIZE_H))) {
		dev_err(dev,
			"%s   width(%d) or height(%d) exceeds limits (%dx%d)\n",
			pctx->name, frame_width, frame_height,
			H264_MAX_SIZE_W, H264_MAX_SIZE_H);
		pctx->frame_errors++;
		return -EINVAL;
	}

	level = ctrls->level;

	memset(td, 0, sizeof(struct hva_h264_td));

	td->frame_width = frame_width;
	td->frame_height = frame_height;

	/* set frame alignement */
	td->window_width =  frame_width;
	td->window_height = frame_height;
	td->window_horizontal_offset = 0;
	td->window_vertical_offset = 0;

	td->first_picture_in_sequence = (!frame_num) ? 1 : 0;

	/* pic_order_cnt_type hard coded to '2' as only I & P frames */
	td->pic_order_cnt_type = 2;

	/* useConstrainedIntraFlag set to false for better coding efficiency */
	td->use_constrained_intra_flag = false;
	td->brc_type = (ctrls->bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
			? BRC_TYPE_CBR : BRC_TYPE_VBR;

	td->entropy_coding_mode = (ctrls->entropy_mode == cavlc) ? CAVLC :
				  CABAC;

	td->bit_rate = ctrls->bitrate;

	/* set framerate, framerate = 1 n/ time per frame */
	if (time_per_frame->numerator >= 536) {
		/*
		 * due to a hardware bug, framerate denominator can't exceed
		 * 536 (BRC overflow). Compute nearest framerate
		 */
		td->framerate_den = 1;
		td->framerate_num = (time_per_frame->denominator +
				    (time_per_frame->numerator >> 1) - 1) /
				    time_per_frame->numerator;

		/*
		 * update bitrate to introduce a correction due to
		 * the new framerate
		 * new bitrate = (old bitrate * new framerate) / old framerate
		 */
		td->bit_rate /= time_per_frame->numerator;
		td->bit_rate *= time_per_frame->denominator;
		td->bit_rate /= td->framerate_num;
	} else {
		td->framerate_den = time_per_frame->numerator;
		td->framerate_num = time_per_frame->denominator;
	}

	/* compute maximum bitrate depending on profile */
	if (ctrls->profile >= V4L2_MPEG_VIDEO_H264_PROFILE_HIGH)
		max_bitrate = h264_infos_list[level].max_bitrate *
			      H264_FACTOR_HIGH;
	else
		max_bitrate = h264_infos_list[level].max_bitrate *
			      H264_FACTOR_BASELINE;

	/* check if bitrate doesn't exceed max size */
	if (td->bit_rate > max_bitrate) {
		dev_dbg(dev,
			"%s   bitrate (%d) larger than level and profile allow, clip to %d\n",
			pctx->name, td->bit_rate, max_bitrate);
		td->bit_rate = max_bitrate;
	}

	/* convert cpb_buffer_size in bits */
	td->cpb_buffer_size = ctrls->cpb_size * 8000;

	/* compute maximum cpb buffer size depending on profile */
	if (ctrls->profile >= V4L2_MPEG_VIDEO_H264_PROFILE_HIGH)
		max_cpb_buffer_size =
		    h264_infos_list[level].max_cpb_size * H264_FACTOR_HIGH;
	else
		max_cpb_buffer_size =
		    h264_infos_list[level].max_cpb_size * H264_FACTOR_BASELINE;

	/* check if cpb buffer size doesn't exceed max size */
	if (td->cpb_buffer_size > max_cpb_buffer_size) {
		dev_dbg(dev,
			"%s   cpb size larger than level %d allows, clip to %d\n",
			pctx->name, td->cpb_buffer_size, max_cpb_buffer_size);
		td->cpb_buffer_size = max_cpb_buffer_size;
	}

	/* enable skipping in the Bitrate Controller */
	td->brc_no_skip = 0;

	/* initial delay */
	if ((ctrls->bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR) &&
	    td->bit_rate)
		td->delay = 1000 * (td->cpb_buffer_size / td->bit_rate);
	else
		td->delay = 0;

	switch (frame->info.pixelformat) {
	case V4L2_PIX_FMT_NV12:
		td->sampling_mode = SAMPLING_MODE_NV12;
		break;
	case V4L2_PIX_FMT_NV21:
		td->sampling_mode = SAMPLING_MODE_NV21;
		break;
	default:
		dev_err(dev, "%s   invalid source pixel format\n",
			pctx->name);
		pctx->frame_errors++;
		return -EINVAL;
	}

	/*
	 * fill matrix color converter (RGB to YUV)
	 * Y = 0,299 R + 0,587 G + 0,114 B
	 * Cb = -0,1687 R -0,3313 G + 0,5 B + 128
	 * Cr = 0,5 R - 0,4187 G - 0,0813 B + 128
	 */
	td->rgb2_yuv_y_coeff = 0x12031008;
	td->rgb2_yuv_u_coeff = 0x800EF7FB;
	td->rgb2_yuv_v_coeff = 0x80FEF40E;

	/* enable/disable transform mode */
	td->transform_mode = ctrls->dct8x8;

	/* encoder complexity fix to 2, ENCODE_I_16x16_I_NxN_P_16x16_P_WxH */
	td->encoder_complexity = 2;

	/* quant fix to 28, default VBR value */
	td->quant = 28;

	if (td->framerate_den == 0) {
		dev_err(dev, "%s   invalid framerate\n", pctx->name);
		pctx->frame_errors++;
		return -EINVAL;
	}

	/* if automatic framerate, deactivate bitrate controller */
	if (td->framerate_num == 0)
		td->brc_type = 0;

	/* compliancy fix to true */
	td->strict_hrd_compliancy = 1;

	/* set minimum & maximum quantizers */
	td->qp_min = clamp_val(ctrls->qpmin, 0, 51);
	td->qp_max = clamp_val(ctrls->qpmax, 0, 51);

	td->addr_source_buffer = frame->paddr;
	td->addr_fwd_ref_buffer = fwd_ref_frame->paddr;
	td->addr_rec_buffer = loc_rec_frame->paddr;

	td->addr_output_bitstream_end = (u32)stream->paddr + stream->size;

	td->addr_output_bitstream_start = (u32)stream->paddr;
	td->bitstream_offset = (((u32)stream->paddr & 0xF) << 3) &
			       BITSTREAM_OFFSET_MASK;

	td->addr_param_out = (u32)ctx->task->paddr +
			     offsetof(struct hva_h264_task, po);

	/* swap spatial and temporal context */
	if (frame_num % 2) {
		paddr = seq_info->paddr;
		td->addr_spatial_context =  ALIGN(paddr, 0x100);
		paddr = seq_info->paddr + DATA_SIZE(frame_width,
							frame_height);
		td->addr_temporal_context = ALIGN(paddr, 0x100);
	} else {
		paddr = seq_info->paddr;
		td->addr_temporal_context = ALIGN(paddr, 0x100);
		paddr = seq_info->paddr + DATA_SIZE(frame_width,
							frame_height);
		td->addr_spatial_context =  ALIGN(paddr, 0x100);
	}

	paddr = seq_info->paddr + 2 * DATA_SIZE(frame_width, frame_height);

	td->addr_brc_in_out_parameter =  ALIGN(paddr, 0x100);

	paddr = td->addr_brc_in_out_parameter + BRC_DATA_SIZE;
	td->addr_slice_header =  ALIGN(paddr, 0x100);
	td->addr_external_sw =  ALIGN(addr_esram, 0x100);

	addr_esram += SEARCH_WINDOW_BUFFER_MAX_SIZE(frame_width);
	td->addr_local_rec_buffer = ALIGN(addr_esram, 0x100);

	addr_esram += LOCAL_RECONSTRUCTED_BUFFER_MAX_SIZE(frame_width);
	td->addr_lctx = ALIGN(addr_esram, 0x100);

	addr_esram += CTX_MB_BUFFER_MAX_SIZE(max(frame_width, frame_height));
	td->addr_cabac_context_buffer = ALIGN(addr_esram, 0x100);

	if (!(frame_num % ctrls->gop_size)) {
		td->picture_coding_type = PICTURE_CODING_TYPE_I;
		stream->vbuf.flags |= V4L2_BUF_FLAG_KEYFRAME;
	} else {
		td->picture_coding_type = PICTURE_CODING_TYPE_P;
		stream->vbuf.flags &= ~V4L2_BUF_FLAG_KEYFRAME;
	}

	/* fill the slice header part */
	slice_header_vaddr = seq_info->vaddr + (td->addr_slice_header -
			     seq_info->paddr);

	hva_h264_fill_slice_header(pctx, slice_header_vaddr, ctrls, frame_num,
				   &td->slice_header_size_in_bits,
				   &td->slice_header_offset0,
				   &td->slice_header_offset1,
				   &td->slice_header_offset2);

	td->chroma_qp_index_offset = 2;
	td->slice_synchro_enable = 0;
	td->max_slice_number = 1;

	/*
	 * check the sps/pps header size for key frame only
	 * sps/pps header was previously fill by libv4l
	 * during qbuf of stream buffer
	 */
	if ((stream->vbuf.flags == V4L2_BUF_FLAG_KEYFRAME) &&
	    (payload > MAX_SPS_PPS_SIZE)) {
		dev_err(dev, "%s   invalid sps/pps size %d\n", pctx->name,
			payload);
		pctx->frame_errors++;
		return -EINVAL;
	}

	if (stream->vbuf.flags != V4L2_BUF_FLAG_KEYFRAME)
		payload = 0;

	/* add SEI nal (video stereo info) */
	if (ctrls->sei_fp && hva_h264_fill_sei_nal(pctx, SEI_STEREO_VIDEO_INFO,
						   (u8 *)stream->vaddr,
						   &payload)) {
		dev_err(dev, "%s   fail to get SEI nal\n", pctx->name);
		pctx->frame_errors++;
		return -EINVAL;
	}

	/* fill size of non-VCL NAL units (SPS, PPS, filler and SEI) */
	td->non_vcl_nalu_size = payload * 8;

	/* compute bitstream offset & new start address of bitstream */
	td->addr_output_bitstream_start += ((payload >> 4) << 4);
	td->bitstream_offset += (payload - ((payload >> 4) << 4)) * 8;

	stream->bytesused = payload;

	return 0;
}

static unsigned int hva_h264_get_stream_size(struct hva_h264_task *task)
{
	struct hva_h264_po *po = &task->po;

	return po->bitstream_size;
}

static u32 hva_h264_get_stuffing_bytes(struct hva_h264_task *task)
{
	struct hva_h264_po *po = &task->po;

	return po->stuffing_bits >> 3;
}

static int hva_h264_open(struct hva_ctx *pctx)
{
	struct device *dev = ctx_to_dev(pctx);
	struct hva_h264_ctx *ctx;
	struct hva_dev *hva = ctx_to_hdev(pctx);
	u32 frame_width = pctx->frameinfo.aligned_width;
	u32 frame_height = pctx->frameinfo.aligned_height;
	u32 size;
	int ret;

	/* check esram size necessary to encode a frame */
	size = SEARCH_WINDOW_BUFFER_MAX_SIZE(frame_width) +
	       LOCAL_RECONSTRUCTED_BUFFER_MAX_SIZE(frame_width) +
	       CTX_MB_BUFFER_MAX_SIZE(max(frame_width, frame_height)) +
	       CABAC_CONTEXT_BUFFER_MAX_SIZE(frame_width);

	if (hva->esram_size < size) {
		dev_err(dev, "%s   not enough esram (max:%d request:%d)\n",
			pctx->name, hva->esram_size, size);
		ret = -EINVAL;
		goto err;
	}

	/* allocate context for codec */
	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto err;
	}

	/* allocate sequence info buffer */
	ret = hva_mem_alloc(pctx,
			    2 * DATA_SIZE(frame_width, frame_height) +
			    SLICE_HEADER_SIZE +
			    BRC_DATA_SIZE,
			    "hva sequence info",
			    &ctx->seq_info);
	if (ret) {
		dev_err(dev,
			"%s   failed to allocate sequence info buffer\n",
			pctx->name);
		goto err_ctx;
	}

	/* allocate reference frame buffer */
	ret = hva_mem_alloc(pctx,
			    frame_width * frame_height * 3 / 2,
			    "hva reference frame",
			    &ctx->ref_frame);
	if (ret) {
		dev_err(dev, "%s   failed to allocate reference frame buffer\n",
			pctx->name);
		goto err_seq_info;
	}

	/* allocate reconstructed frame buffer */
	ret = hva_mem_alloc(pctx,
			    frame_width * frame_height * 3 / 2,
			    "hva reconstructed frame",
			    &ctx->rec_frame);
	if (ret) {
		dev_err(dev,
			"%s   failed to allocate reconstructed frame buffer\n",
			pctx->name);
		goto err_ref_frame;
	}

	/* allocate task descriptor */
	ret = hva_mem_alloc(pctx,
			    sizeof(struct hva_h264_task),
			    "hva task descriptor",
			    &ctx->task);
	if (ret) {
		dev_err(dev,
			"%s   failed to allocate task descriptor\n",
			pctx->name);
		goto err_rec_frame;
	}

	pctx->priv = (void *)ctx;

	return 0;

err_rec_frame:
	hva_mem_free(pctx, ctx->rec_frame);
err_ref_frame:
	hva_mem_free(pctx, ctx->ref_frame);
err_seq_info:
	hva_mem_free(pctx, ctx->seq_info);
err_ctx:
	devm_kfree(dev, ctx);
err:
	pctx->sys_errors++;
	return ret;
}

static int hva_h264_close(struct hva_ctx *pctx)
{
	struct hva_h264_ctx *ctx = (struct hva_h264_ctx *)pctx->priv;
	struct device *dev = ctx_to_dev(pctx);

	if (ctx->seq_info)
		hva_mem_free(pctx, ctx->seq_info);

	if (ctx->ref_frame)
		hva_mem_free(pctx, ctx->ref_frame);

	if (ctx->rec_frame)
		hva_mem_free(pctx, ctx->rec_frame);

	if (ctx->task)
		hva_mem_free(pctx, ctx->task);

	devm_kfree(dev, ctx);

	return 0;
}

static int hva_h264_encode(struct hva_ctx *pctx, struct hva_frame *frame,
			   struct hva_stream *stream)
{
	struct hva_h264_ctx *ctx = (struct hva_h264_ctx *)pctx->priv;
	struct hva_h264_task *task = (struct hva_h264_task *)ctx->task->vaddr;
	u32 stuffing_bytes = 0;
	int ret = 0;

	ret = hva_h264_prepare_task(pctx, task, frame, stream);
	if (ret)
		goto err;

	ret = hva_hw_execute_task(pctx, H264_ENC, ctx->task);
	if (ret)
		goto err;

	pctx->stream_num++;
	stream->bytesused += hva_h264_get_stream_size(task);

	stuffing_bytes = hva_h264_get_stuffing_bytes(task);

	if (stuffing_bytes)
		hva_h264_fill_data_nal(pctx, stuffing_bytes,
				       (u8 *)stream->vaddr,
				       stream->size,
				       &stream->bytesused);

	/* switch reference & reconstructed frame */
	swap(ctx->ref_frame, ctx->rec_frame);

	return 0;
err:
	stream->bytesused = 0;
	return ret;
}

const struct hva_enc nv12h264enc = {
	.name = "H264(NV12)",
	.pixelformat = V4L2_PIX_FMT_NV12,
	.streamformat = V4L2_PIX_FMT_H264,
	.max_width = H264_MAX_SIZE_W,
	.max_height = H264_MAX_SIZE_H,
	.open = hva_h264_open,
	.close = hva_h264_close,
	.encode = hva_h264_encode,
};

const struct hva_enc nv21h264enc = {
	.name = "H264(NV21)",
	.pixelformat = V4L2_PIX_FMT_NV21,
	.streamformat = V4L2_PIX_FMT_H264,
	.max_width = H264_MAX_SIZE_W,
	.max_height = H264_MAX_SIZE_H,
	.open = hva_h264_open,
	.close = hva_h264_close,
	.encode = hva_h264_encode,
};
