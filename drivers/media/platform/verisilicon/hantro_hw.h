/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hantro VPU codec driver
 *
 * Copyright 2018 Google LLC.
 *	Tomasz Figa <tfiga@chromium.org>
 */

#ifndef HANTRO_HW_H_
#define HANTRO_HW_H_

#include <linux/interrupt.h>
#include <linux/v4l2-controls.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-vp9.h>
#include <media/videobuf2-core.h>

#include "rockchip_av1_entropymode.h"
#include "rockchip_av1_filmgrain.h"

#define DEC_8190_ALIGN_MASK	0x07U

#define MB_DIM			16
#define TILE_MB_DIM		4
#define MB_WIDTH(w)		DIV_ROUND_UP(w, MB_DIM)
#define MB_HEIGHT(h)		DIV_ROUND_UP(h, MB_DIM)

#define FMT_MIN_WIDTH		48
#define FMT_MIN_HEIGHT		48
#define FMT_HD_WIDTH		1280
#define FMT_HD_HEIGHT		720
#define FMT_FHD_WIDTH		1920
#define FMT_FHD_HEIGHT		1088
#define FMT_UHD_WIDTH		3840
#define FMT_UHD_HEIGHT		2160
#define FMT_4K_WIDTH		4096
#define FMT_4K_HEIGHT		2304

#define NUM_REF_PICTURES	(V4L2_HEVC_DPB_ENTRIES_NUM_MAX + 1)

#define AV1_MAX_FRAME_BUF_COUNT	(V4L2_AV1_TOTAL_REFS_PER_FRAME + 1)

#define MAX_POSTPROC_BUFFERS	64

#define CBS_SIZE	16	/* compression table size in bytes */
#define CBS_LUMA	8	/* luminance CBS is composed of 1 8x8 coded block */
#define CBS_CHROMA_W	(8 * 2)	/* chrominance CBS is composed of two 8x4 coded
				 * blocks, with Cb CB first then Cr CB following
				 */
#define CBS_CHROMA_H	4

struct hantro_dev;
struct hantro_ctx;
struct hantro_buf;
struct hantro_variant;

/**
 * struct hantro_aux_buf - auxiliary DMA buffer for hardware data
 *
 * @cpu:	CPU pointer to the buffer.
 * @dma:	DMA address of the buffer.
 * @size:	Size of the buffer.
 * @attrs:	Attributes of the DMA mapping.
 */
struct hantro_aux_buf {
	void *cpu;
	dma_addr_t dma;
	size_t size;
	unsigned long attrs;
};

/* Max. number of entries in the DPB (HW limitation). */
#define HANTRO_H264_DPB_SIZE		16

/**
 * struct hantro_h264_dec_ctrls
 *
 * @decode:	Decode params
 * @scaling:	Scaling info
 * @sps:	SPS info
 * @pps:	PPS info
 */
struct hantro_h264_dec_ctrls {
	const struct v4l2_ctrl_h264_decode_params *decode;
	const struct v4l2_ctrl_h264_scaling_matrix *scaling;
	const struct v4l2_ctrl_h264_sps *sps;
	const struct v4l2_ctrl_h264_pps *pps;
};

/**
 * struct hantro_h264_dec_reflists
 *
 * @p:		P reflist
 * @b0:		B0 reflist
 * @b1:		B1 reflist
 */
struct hantro_h264_dec_reflists {
	struct v4l2_h264_reference p[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference b0[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference b1[V4L2_H264_REF_LIST_LEN];
};

/**
 * struct hantro_h264_dec_hw_ctx
 *
 * @priv:	Private auxiliary buffer for hardware.
 * @dpb:	DPB
 * @reflists:	P/B0/B1 reflists
 * @ctrls:	V4L2 controls attached to a run
 * @dpb_longterm: DPB long-term
 * @dpb_valid:	  DPB valid
 * @cur_poc:	Current picture order count
 */
struct hantro_h264_dec_hw_ctx {
	struct hantro_aux_buf priv;
	struct v4l2_h264_dpb_entry dpb[HANTRO_H264_DPB_SIZE];
	struct hantro_h264_dec_reflists reflists;
	struct hantro_h264_dec_ctrls ctrls;
	u32 dpb_longterm;
	u32 dpb_valid;
	s32 cur_poc;
};

/**
 * struct hantro_hevc_dec_ctrls
 * @decode_params: Decode params
 * @scaling:	Scaling matrix
 * @sps:	SPS info
 * @pps:	PPS info
 * @hevc_hdr_skip_length: the number of data (in bits) to skip in the
 *			  slice segment header syntax after 'slice type'
 *			  token
 */
struct hantro_hevc_dec_ctrls {
	const struct v4l2_ctrl_hevc_decode_params *decode_params;
	const struct v4l2_ctrl_hevc_scaling_matrix *scaling;
	const struct v4l2_ctrl_hevc_sps *sps;
	const struct v4l2_ctrl_hevc_pps *pps;
	u32 hevc_hdr_skip_length;
};

/**
 * struct hantro_hevc_dec_hw_ctx
 * @tile_sizes:		Tile sizes buffer
 * @tile_filter:	Tile vertical filter buffer
 * @tile_sao:		Tile SAO buffer
 * @tile_bsd:		Tile BSD control buffer
 * @ref_bufs:		Internal reference buffers
 * @scaling_lists:	Scaling lists buffer
 * @ref_bufs_poc:	Internal reference buffers picture order count
 * @ref_bufs_used:	Bitfield of used reference buffers
 * @ctrls:		V4L2 controls attached to a run
 * @num_tile_cols_allocated: number of allocated tiles
 * @use_compression:	use reference buffer compression
 */
struct hantro_hevc_dec_hw_ctx {
	struct hantro_aux_buf tile_sizes;
	struct hantro_aux_buf tile_filter;
	struct hantro_aux_buf tile_sao;
	struct hantro_aux_buf tile_bsd;
	struct hantro_aux_buf ref_bufs[NUM_REF_PICTURES];
	struct hantro_aux_buf scaling_lists;
	s32 ref_bufs_poc[NUM_REF_PICTURES];
	u32 ref_bufs_used;
	struct hantro_hevc_dec_ctrls ctrls;
	unsigned int num_tile_cols_allocated;
	bool use_compression;
};

/**
 * struct hantro_mpeg2_dec_hw_ctx
 *
 * @qtable:		Quantization table
 */
struct hantro_mpeg2_dec_hw_ctx {
	struct hantro_aux_buf qtable;
};

/**
 * struct hantro_vp8_dec_hw_ctx
 *
 * @segment_map:	Segment map buffer.
 * @prob_tbl:		Probability table buffer.
 */
struct hantro_vp8_dec_hw_ctx {
	struct hantro_aux_buf segment_map;
	struct hantro_aux_buf prob_tbl;
};

/**
 * struct hantro_vp9_frame_info
 *
 * @valid: frame info valid flag
 * @frame_context_idx: index of frame context
 * @reference_mode: inter prediction type
 * @tx_mode: transform mode
 * @interpolation_filter: filter selection for inter prediction
 * @flags: frame flags
 * @timestamp: frame timestamp
 */
struct hantro_vp9_frame_info {
	u32 valid : 1;
	u32 frame_context_idx : 2;
	u32 reference_mode : 2;
	u32 tx_mode : 3;
	u32 interpolation_filter : 3;
	u32 flags;
	u64 timestamp;
};

#define MAX_SB_COLS	64
#define MAX_SB_ROWS	34

/**
 * struct hantro_vp9_dec_hw_ctx
 *
 * @tile_edge: auxiliary DMA buffer for tile edge processing
 * @segment_map: auxiliary DMA buffer for segment map
 * @misc: auxiliary DMA buffer for tile info, probabilities and hw counters
 * @cnts: vp9 library struct for abstracting hw counters access
 * @probability_tables: VP9 probability tables implied by the spec
 * @frame_context: VP9 frame contexts
 * @cur: current frame information
 * @last: last frame information
 * @bsd_ctrl_offset: bsd offset into tile_edge
 * @segment_map_size: size of segment map
 * @ctx_counters_offset: hw counters offset into misc
 * @tile_info_offset: tile info offset into misc
 * @tile_r_info: per-tile information array
 * @tile_c_info: per-tile information array
 * @last_tile_r: last number of tile rows
 * @last_tile_c: last number of tile cols
 * @last_sbs_r: last number of superblock rows
 * @last_sbs_c: last number of superblock cols
 * @active_segment: number of active segment (alternating between 0 and 1)
 * @feature_enabled: segmentation feature enabled flags
 * @feature_data: segmentation feature data
 */
struct hantro_vp9_dec_hw_ctx {
	struct hantro_aux_buf tile_edge;
	struct hantro_aux_buf segment_map;
	struct hantro_aux_buf misc;
	struct v4l2_vp9_frame_symbol_counts cnts;
	struct v4l2_vp9_frame_context probability_tables;
	struct v4l2_vp9_frame_context frame_context[4];
	struct hantro_vp9_frame_info cur;
	struct hantro_vp9_frame_info last;

	unsigned int bsd_ctrl_offset;
	unsigned int segment_map_size;
	unsigned int ctx_counters_offset;
	unsigned int tile_info_offset;

	unsigned short tile_r_info[MAX_SB_ROWS];
	unsigned short tile_c_info[MAX_SB_COLS];
	unsigned int last_tile_r;
	unsigned int last_tile_c;
	unsigned int last_sbs_r;
	unsigned int last_sbs_c;

	unsigned int active_segment;
	u8 feature_enabled[8];
	s16 feature_data[8][4];
};

/**
 * struct hantro_av1_dec_ctrls
 * @sequence:		AV1 Sequence
 * @tile_group_entry:	AV1 Tile Group entry
 * @frame:		AV1 Frame Header OBU
 * @film_grain:		AV1 Film Grain
 */
struct hantro_av1_dec_ctrls {
	const struct v4l2_ctrl_av1_sequence *sequence;
	const struct v4l2_ctrl_av1_tile_group_entry *tile_group_entry;
	const struct v4l2_ctrl_av1_frame *frame;
	const struct v4l2_ctrl_av1_film_grain *film_grain;
};

struct hantro_av1_frame_ref {
	int width;
	int height;
	int mi_cols;
	int mi_rows;
	u64 timestamp;
	enum v4l2_av1_frame_type frame_type;
	bool used;
	u32 order_hint;
	u32 order_hints[V4L2_AV1_TOTAL_REFS_PER_FRAME];
	struct vb2_v4l2_buffer *vb2_ref;
};

/**
 * struct hantro_av1_dec_hw_ctx
 * @db_data_col:	db tile col data buffer
 * @db_ctrl_col:	db tile col ctrl buffer
 * @cdef_col:		cdef tile col buffer
 * @sr_col:		sr tile col buffer
 * @lr_col:		lr tile col buffer
 * @global_model:	global model buffer
 * @tile_info:		tile info buffer
 * @segment:		segmentation info buffer
 * @film_grain:		film grain buffer
 * @prob_tbl:		probability table
 * @prob_tbl_out:	probability table output
 * @tile_buf:		tile buffer
 * @ctrls:		V4L2 controls attached to a run
 * @frame_refs:		reference frames info slots
 * @ref_frame_sign_bias: array of sign bias
 * @num_tile_cols_allocated: number of allocated tiles
 * @cdfs:		current probabilities structure
 * @cdfs_ndvc:		current mv probabilities structure
 * @default_cdfs:	default probabilities structure
 * @default_cdfs_ndvc:	default mv probabilties structure
 * @cdfs_last:		stored probabilities structures
 * @cdfs_last_ndvc:	stored mv probabilities structures
 * @current_frame_index: index of the current in frame_refs array
 */
struct hantro_av1_dec_hw_ctx {
	struct hantro_aux_buf db_data_col;
	struct hantro_aux_buf db_ctrl_col;
	struct hantro_aux_buf cdef_col;
	struct hantro_aux_buf sr_col;
	struct hantro_aux_buf lr_col;
	struct hantro_aux_buf global_model;
	struct hantro_aux_buf tile_info;
	struct hantro_aux_buf segment;
	struct hantro_aux_buf film_grain;
	struct hantro_aux_buf prob_tbl;
	struct hantro_aux_buf prob_tbl_out;
	struct hantro_aux_buf tile_buf;
	struct hantro_av1_dec_ctrls ctrls;
	struct hantro_av1_frame_ref frame_refs[AV1_MAX_FRAME_BUF_COUNT];
	u32 ref_frame_sign_bias[V4L2_AV1_TOTAL_REFS_PER_FRAME];
	unsigned int num_tile_cols_allocated;
	struct av1cdfs *cdfs;
	struct mvcdfs  *cdfs_ndvc;
	struct av1cdfs default_cdfs;
	struct mvcdfs  default_cdfs_ndvc;
	struct av1cdfs cdfs_last[NUM_REF_FRAMES];
	struct mvcdfs  cdfs_last_ndvc[NUM_REF_FRAMES];
	int current_frame_index;
};
/**
 * struct hantro_postproc_ctx
 *
 * @dec_q:		References buffers, in decoder format.
 */
struct hantro_postproc_ctx {
	struct hantro_aux_buf dec_q[MAX_POSTPROC_BUFFERS];
};

/**
 * struct hantro_postproc_ops - post-processor operations
 *
 * @enable:		Enable the post-processor block. Optional.
 * @disable:		Disable the post-processor block. Optional.
 * @enum_framesizes:	Enumerate possible scaled output formats.
 *			Returns zero if OK, a negative value in error cases.
 *			Optional.
 */
struct hantro_postproc_ops {
	void (*enable)(struct hantro_ctx *ctx);
	void (*disable)(struct hantro_ctx *ctx);
	int (*enum_framesizes)(struct hantro_ctx *ctx, struct v4l2_frmsizeenum *fsize);
};

/**
 * struct hantro_codec_ops - codec mode specific operations
 *
 * @init:	If needed, can be used for initialization.
 *		Optional and called from process context.
 * @exit:	If needed, can be used to undo the .init phase.
 *		Optional and called from process context.
 * @run:	Start single {en,de)coding job. Called from atomic context
 *		to indicate that a pair of buffers is ready and the hardware
 *		should be programmed and started. Returns zero if OK, a
 *		negative value in error cases.
 * @done:	Read back processing results and additional data from hardware.
 * @reset:	Reset the hardware in case of a timeout.
 */
struct hantro_codec_ops {
	int (*init)(struct hantro_ctx *ctx);
	void (*exit)(struct hantro_ctx *ctx);
	int (*run)(struct hantro_ctx *ctx);
	void (*done)(struct hantro_ctx *ctx);
	void (*reset)(struct hantro_ctx *ctx);
};

/**
 * enum hantro_enc_fmt - source format ID for hardware registers.
 *
 * @ROCKCHIP_VPU_ENC_FMT_YUV420P: Y/CbCr 4:2:0 planar format
 * @ROCKCHIP_VPU_ENC_FMT_YUV420SP: Y/CbCr 4:2:0 semi-planar format
 * @ROCKCHIP_VPU_ENC_FMT_YUYV422: YUV 4:2:2 packed format (YUYV)
 * @ROCKCHIP_VPU_ENC_FMT_UYVY422: YUV 4:2:2 packed format (UYVY)
 */
enum hantro_enc_fmt {
	ROCKCHIP_VPU_ENC_FMT_YUV420P = 0,
	ROCKCHIP_VPU_ENC_FMT_YUV420SP = 1,
	ROCKCHIP_VPU_ENC_FMT_YUYV422 = 2,
	ROCKCHIP_VPU_ENC_FMT_UYVY422 = 3,
};

extern const struct hantro_variant imx8mm_vpu_g1_variant;
extern const struct hantro_variant imx8mq_vpu_g1_variant;
extern const struct hantro_variant imx8mq_vpu_g2_variant;
extern const struct hantro_variant imx8mq_vpu_variant;
extern const struct hantro_variant px30_vpu_variant;
extern const struct hantro_variant rk3036_vpu_variant;
extern const struct hantro_variant rk3066_vpu_variant;
extern const struct hantro_variant rk3288_vpu_variant;
extern const struct hantro_variant rk3328_vpu_variant;
extern const struct hantro_variant rk3399_vpu_variant;
extern const struct hantro_variant rk3568_vepu_variant;
extern const struct hantro_variant rk3568_vpu_variant;
extern const struct hantro_variant rk3588_vpu981_variant;
extern const struct hantro_variant sama5d4_vdec_variant;
extern const struct hantro_variant sunxi_vpu_variant;
extern const struct hantro_variant stm32mp25_vdec_variant;
extern const struct hantro_variant stm32mp25_venc_variant;

extern const struct hantro_postproc_ops hantro_g1_postproc_ops;
extern const struct hantro_postproc_ops hantro_g2_postproc_ops;
extern const struct hantro_postproc_ops rockchip_vpu981_postproc_ops;

extern const u32 hantro_vp8_dec_mc_filter[8][6];

void hantro_watchdog(struct work_struct *work);
void hantro_run(struct hantro_ctx *ctx);
void hantro_irq_done(struct hantro_dev *vpu,
		     enum vb2_buffer_state result);
void hantro_start_prepare_run(struct hantro_ctx *ctx);
void hantro_end_prepare_run(struct hantro_ctx *ctx);

irqreturn_t hantro_g1_irq(int irq, void *dev_id);
void hantro_g1_reset(struct hantro_ctx *ctx);

int hantro_h1_jpeg_enc_run(struct hantro_ctx *ctx);
int rockchip_vpu2_jpeg_enc_run(struct hantro_ctx *ctx);
void hantro_h1_jpeg_enc_done(struct hantro_ctx *ctx);
void rockchip_vpu2_jpeg_enc_done(struct hantro_ctx *ctx);

dma_addr_t hantro_h264_get_ref_buf(struct hantro_ctx *ctx,
				   unsigned int dpb_idx);
u16 hantro_h264_get_ref_nbr(struct hantro_ctx *ctx,
			    unsigned int dpb_idx);
int hantro_h264_dec_prepare_run(struct hantro_ctx *ctx);
int rockchip_vpu2_h264_dec_run(struct hantro_ctx *ctx);
int hantro_g1_h264_dec_run(struct hantro_ctx *ctx);
int hantro_h264_dec_init(struct hantro_ctx *ctx);
void hantro_h264_dec_exit(struct hantro_ctx *ctx);

int hantro_hevc_dec_init(struct hantro_ctx *ctx);
void hantro_hevc_dec_exit(struct hantro_ctx *ctx);
int hantro_g2_hevc_dec_run(struct hantro_ctx *ctx);
int hantro_hevc_dec_prepare_run(struct hantro_ctx *ctx);
void hantro_hevc_ref_init(struct hantro_ctx *ctx);
dma_addr_t hantro_hevc_get_ref_buf(struct hantro_ctx *ctx, s32 poc);
int hantro_hevc_add_ref_buf(struct hantro_ctx *ctx, int poc, dma_addr_t addr);

int rockchip_vpu981_av1_dec_init(struct hantro_ctx *ctx);
void rockchip_vpu981_av1_dec_exit(struct hantro_ctx *ctx);
int rockchip_vpu981_av1_dec_run(struct hantro_ctx *ctx);
void rockchip_vpu981_av1_dec_done(struct hantro_ctx *ctx);

static inline unsigned short hantro_vp9_num_sbs(unsigned short dimension)
{
	return (dimension + 63) / 64;
}

static inline size_t
hantro_vp9_mv_size(unsigned int width, unsigned int height)
{
	int num_ctbs;

	/*
	 * There can be up to (CTBs x 64) number of blocks,
	 * and the motion vector for each block needs 16 bytes.
	 */
	num_ctbs = hantro_vp9_num_sbs(width) * hantro_vp9_num_sbs(height);
	return (num_ctbs * 64) * 16;
}

static inline size_t
hantro_h264_mv_size(unsigned int width, unsigned int height)
{
	/*
	 * A decoded 8-bit 4:2:0 NV12 frame may need memory for up to
	 * 448 bytes per macroblock with additional 32 bytes on
	 * multi-core variants.
	 *
	 * The H264 decoder needs extra space on the output buffers
	 * to store motion vectors. This is needed for reference
	 * frames and only if the format is non-post-processed NV12.
	 *
	 * Memory layout is as follow:
	 *
	 * +---------------------------+
	 * | Y-plane   256 bytes x MBs |
	 * +---------------------------+
	 * | UV-plane  128 bytes x MBs |
	 * +---------------------------+
	 * | MV buffer  64 bytes x MBs |
	 * +---------------------------+
	 * | MC sync          32 bytes |
	 * +---------------------------+
	 */
	return 64 * MB_WIDTH(width) * MB_WIDTH(height) + 32;
}

static inline size_t
hantro_hevc_mv_size(unsigned int width, unsigned int height)
{
	/*
	 * A CTB can be 64x64, 32x32 or 16x16.
	 * Allocated memory for the "worse" case: 16x16
	 */
	return width * height / 16;
}

static inline size_t
hantro_hevc_luma_compressed_size(unsigned int width, unsigned int height)
{
	u32 pic_width_in_cbsy =
		round_up((width + CBS_LUMA - 1) / CBS_LUMA, CBS_SIZE);
	u32 pic_height_in_cbsy = (height + CBS_LUMA - 1) / CBS_LUMA;

	return round_up(pic_width_in_cbsy * pic_height_in_cbsy, CBS_SIZE);
}

static inline size_t
hantro_hevc_chroma_compressed_size(unsigned int width, unsigned int height)
{
	u32 pic_width_in_cbsc =
		round_up((width + CBS_CHROMA_W - 1) / CBS_CHROMA_W, CBS_SIZE);
	u32 pic_height_in_cbsc = (height / 2 + CBS_CHROMA_H - 1) / CBS_CHROMA_H;

	return round_up(pic_width_in_cbsc * pic_height_in_cbsc, CBS_SIZE);
}

static inline size_t
hantro_hevc_compressed_size(unsigned int width, unsigned int height)
{
	return hantro_hevc_luma_compressed_size(width, height) +
	       hantro_hevc_chroma_compressed_size(width, height);
}

static inline unsigned short hantro_av1_num_sbs(unsigned short dimension)
{
	return DIV_ROUND_UP(dimension, 64);
}

static inline size_t
hantro_av1_mv_size(unsigned int width, unsigned int height)
{
	size_t num_sbs = hantro_av1_num_sbs(width) * hantro_av1_num_sbs(height);

	return ALIGN(num_sbs * 384, 16) * 2 + 512;
}

size_t hantro_g2_chroma_offset(struct hantro_ctx *ctx);
size_t hantro_g2_motion_vectors_offset(struct hantro_ctx *ctx);
size_t hantro_g2_luma_compress_offset(struct hantro_ctx *ctx);
size_t hantro_g2_chroma_compress_offset(struct hantro_ctx *ctx);

int hantro_g1_mpeg2_dec_run(struct hantro_ctx *ctx);
int rockchip_vpu2_mpeg2_dec_run(struct hantro_ctx *ctx);
void hantro_mpeg2_dec_copy_qtable(u8 *qtable,
				  const struct v4l2_ctrl_mpeg2_quantisation *ctrl);
int hantro_mpeg2_dec_init(struct hantro_ctx *ctx);
void hantro_mpeg2_dec_exit(struct hantro_ctx *ctx);

int hantro_g1_vp8_dec_run(struct hantro_ctx *ctx);
int rockchip_vpu2_vp8_dec_run(struct hantro_ctx *ctx);
int hantro_vp8_dec_init(struct hantro_ctx *ctx);
void hantro_vp8_dec_exit(struct hantro_ctx *ctx);
void hantro_vp8_prob_update(struct hantro_ctx *ctx,
			    const struct v4l2_ctrl_vp8_frame *hdr);

int hantro_g2_vp9_dec_run(struct hantro_ctx *ctx);
void hantro_g2_vp9_dec_done(struct hantro_ctx *ctx);
int hantro_vp9_dec_init(struct hantro_ctx *ctx);
void hantro_vp9_dec_exit(struct hantro_ctx *ctx);
void hantro_g2_check_idle(struct hantro_dev *vpu);
irqreturn_t hantro_g2_irq(int irq, void *dev_id);

#endif /* HANTRO_HW_H_ */
