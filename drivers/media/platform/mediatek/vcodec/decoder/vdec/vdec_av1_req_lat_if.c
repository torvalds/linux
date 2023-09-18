// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Xiaoyong Lu <xiaoyong.lu@mediatek.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <media/videobuf2-dma-contig.h>

#include "../mtk_vcodec_dec.h"
#include "../../common/mtk_vcodec_intr.h"
#include "../vdec_drv_base.h"
#include "../vdec_drv_if.h"
#include "../vdec_vpu_if.h"

#define AV1_MAX_FRAME_BUF_COUNT		(V4L2_AV1_TOTAL_REFS_PER_FRAME + 1)
#define AV1_TILE_BUF_SIZE		64
#define AV1_SCALE_SUBPEL_BITS		10
#define AV1_REF_SCALE_SHIFT		14
#define AV1_REF_NO_SCALE		BIT(AV1_REF_SCALE_SHIFT)
#define AV1_REF_INVALID_SCALE		-1
#define AV1_CDF_TABLE_BUFFER_SIZE	16384
#define AV1_PRIMARY_REF_NONE		7

#define AV1_INVALID_IDX			-1

#define AV1_DIV_ROUND_UP_POW2(value, n)			\
({							\
	typeof(n) _n  = n;				\
	typeof(value) _value = value;			\
	(_value + (BIT(_n) >> 1)) >> _n;		\
})

#define AV1_DIV_ROUND_UP_POW2_SIGNED(value, n)				\
({									\
	typeof(n) _n_  = n;						\
	typeof(value) _value_ = value;					\
	(((_value_) < 0) ? -AV1_DIV_ROUND_UP_POW2(-(_value_), (_n_))	\
		: AV1_DIV_ROUND_UP_POW2((_value_), (_n_)));		\
})

#define BIT_FLAG(x, bit)		(!!((x)->flags & (bit)))
#define SEGMENTATION_FLAG(x, name)	(!!((x)->flags & V4L2_AV1_SEGMENTATION_FLAG_##name))
#define QUANT_FLAG(x, name)		(!!((x)->flags & V4L2_AV1_QUANTIZATION_FLAG_##name))
#define SEQUENCE_FLAG(x, name)		(!!((x)->flags & V4L2_AV1_SEQUENCE_FLAG_##name))
#define FH_FLAG(x, name)		(!!((x)->flags & V4L2_AV1_FRAME_FLAG_##name))

#define MINQ 0
#define MAXQ 255

#define DIV_LUT_PREC_BITS 14
#define DIV_LUT_BITS 8
#define DIV_LUT_NUM BIT(DIV_LUT_BITS)
#define WARP_PARAM_REDUCE_BITS 6
#define WARPEDMODEL_PREC_BITS 16

#define SEG_LVL_ALT_Q 0
#define SECONDARY_FILTER_STRENGTH_NUM_BITS 2

static const short div_lut[DIV_LUT_NUM + 1] = {
	16384, 16320, 16257, 16194, 16132, 16070, 16009, 15948, 15888, 15828, 15768,
	15709, 15650, 15592, 15534, 15477, 15420, 15364, 15308, 15252, 15197, 15142,
	15087, 15033, 14980, 14926, 14873, 14821, 14769, 14717, 14665, 14614, 14564,
	14513, 14463, 14413, 14364, 14315, 14266, 14218, 14170, 14122, 14075, 14028,
	13981, 13935, 13888, 13843, 13797, 13752, 13707, 13662, 13618, 13574, 13530,
	13487, 13443, 13400, 13358, 13315, 13273, 13231, 13190, 13148, 13107, 13066,
	13026, 12985, 12945, 12906, 12866, 12827, 12788, 12749, 12710, 12672, 12633,
	12596, 12558, 12520, 12483, 12446, 12409, 12373, 12336, 12300, 12264, 12228,
	12193, 12157, 12122, 12087, 12053, 12018, 11984, 11950, 11916, 11882, 11848,
	11815, 11782, 11749, 11716, 11683, 11651, 11619, 11586, 11555, 11523, 11491,
	11460, 11429, 11398, 11367, 11336, 11305, 11275, 11245, 11215, 11185, 11155,
	11125, 11096, 11067, 11038, 11009, 10980, 10951, 10923, 10894, 10866, 10838,
	10810, 10782, 10755, 10727, 10700, 10673, 10645, 10618, 10592, 10565, 10538,
	10512, 10486, 10460, 10434, 10408, 10382, 10356, 10331, 10305, 10280, 10255,
	10230, 10205, 10180, 10156, 10131, 10107, 10082, 10058, 10034, 10010, 9986,
	9963,  9939,  9916,  9892,  9869,  9846,  9823,  9800,  9777,  9754,  9732,
	9709,  9687,  9664,  9642,  9620,  9598,  9576,  9554,  9533,  9511,  9489,
	9468,  9447,  9425,  9404,  9383,  9362,  9341,  9321,  9300,  9279,  9259,
	9239,  9218,  9198,  9178,  9158,  9138,  9118,  9098,  9079,  9059,  9039,
	9020,  9001,  8981,  8962,  8943,  8924,  8905,  8886,  8867,  8849,  8830,
	8812,  8793,  8775,  8756,  8738,  8720,  8702,  8684,  8666,  8648,  8630,
	8613,  8595,  8577,  8560,  8542,  8525,  8508,  8490,  8473,  8456,  8439,
	8422,  8405,  8389,  8372,  8355,  8339,  8322,  8306,  8289,  8273,  8257,
	8240,  8224,  8208,  8192,
};

/**
 * struct vdec_av1_slice_init_vsi - VSI used to initialize instance
 * @architecture:	architecture type
 * @reserved:		reserved
 * @core_vsi:		for core vsi
 * @cdf_table_addr:	cdf table addr
 * @cdf_table_size:	cdf table size
 * @iq_table_addr:	iq table addr
 * @iq_table_size:	iq table size
 * @vsi_size:		share vsi structure size
 */
struct vdec_av1_slice_init_vsi {
	u32 architecture;
	u32 reserved;
	u64 core_vsi;
	u64 cdf_table_addr;
	u32 cdf_table_size;
	u64 iq_table_addr;
	u32 iq_table_size;
	u32 vsi_size;
};

/**
 * struct vdec_av1_slice_mem - memory address and size
 * @buf:		dma_addr padding
 * @dma_addr:		buffer address
 * @size:		buffer size
 * @dma_addr_end:	buffer end address
 * @padding:		for padding
 */
struct vdec_av1_slice_mem {
	union {
		u64 buf;
		dma_addr_t dma_addr;
	};
	union {
		size_t size;
		dma_addr_t dma_addr_end;
		u64 padding;
	};
};

/**
 * struct vdec_av1_slice_state - decoding state
 * @err                   : err type for decode
 * @full                  : transcoded buffer is full or not
 * @timeout               : decode timeout or not
 * @perf                  : performance enable
 * @crc                   : hw checksum
 * @out_size              : hw output size
 */
struct vdec_av1_slice_state {
	int err;
	u32 full;
	u32 timeout;
	u32 perf;
	u32 crc[16];
	u32 out_size;
};

/*
 * enum vdec_av1_slice_resolution_level - resolution level
 */
enum vdec_av1_slice_resolution_level {
	AV1_RES_NONE,
	AV1_RES_FHD,
	AV1_RES_4K,
	AV1_RES_8K,
};

/*
 * enum vdec_av1_slice_frame_type - av1 frame type
 */
enum vdec_av1_slice_frame_type {
	AV1_KEY_FRAME = 0,
	AV1_INTER_FRAME,
	AV1_INTRA_ONLY_FRAME,
	AV1_SWITCH_FRAME,
	AV1_FRAME_TYPES,
};

/*
 * enum vdec_av1_slice_reference_mode - reference mode type
 */
enum vdec_av1_slice_reference_mode {
	AV1_SINGLE_REFERENCE = 0,
	AV1_COMPOUND_REFERENCE,
	AV1_REFERENCE_MODE_SELECT,
	AV1_REFERENCE_MODES,
};

/**
 * struct vdec_av1_slice_tile_group - info for each tile
 * @num_tiles:			tile number
 * @tile_size:			input size for each tile
 * @tile_start_offset:		tile offset to input buffer
 */
struct vdec_av1_slice_tile_group {
	u32 num_tiles;
	u32 tile_size[V4L2_AV1_MAX_TILE_COUNT];
	u32 tile_start_offset[V4L2_AV1_MAX_TILE_COUNT];
};

/**
 * struct vdec_av1_slice_scale_factors - scale info for each ref frame
 * @is_scaled:  frame is scaled or not
 * @x_scale:    frame width scale coefficient
 * @y_scale:    frame height scale coefficient
 * @x_step:     width step for x_scale
 * @y_step:     height step for y_scale
 */
struct vdec_av1_slice_scale_factors {
	u8 is_scaled;
	int x_scale;
	int y_scale;
	int x_step;
	int y_step;
};

/**
 * struct vdec_av1_slice_frame_refs - ref frame info
 * @ref_fb_idx:         ref slot index
 * @ref_map_idx:        ref frame index
 * @scale_factors:      scale factors for each ref frame
 */
struct vdec_av1_slice_frame_refs {
	int ref_fb_idx;
	int ref_map_idx;
	struct vdec_av1_slice_scale_factors scale_factors;
};

/**
 * struct vdec_av1_slice_gm - AV1 Global Motion parameters
 * @wmtype:     The type of global motion transform used
 * @wmmat:      gm_params
 * @alpha:      alpha info
 * @beta:       beta info
 * @gamma:      gamma info
 * @delta:      delta info
 * @invalid:    is invalid or not
 */
struct vdec_av1_slice_gm {
	int wmtype;
	int wmmat[8];
	short alpha;
	short beta;
	short gamma;
	short delta;
	char invalid;
};

/**
 * struct vdec_av1_slice_sm - AV1 Skip Mode parameters
 * @skip_mode_allowed:  Skip Mode is allowed or not
 * @skip_mode_present:  specified that the skip_mode will be present or not
 * @skip_mode_frame:    specifies the frames to use for compound prediction
 */
struct vdec_av1_slice_sm {
	u8 skip_mode_allowed;
	u8 skip_mode_present;
	int skip_mode_frame[2];
};

/**
 * struct vdec_av1_slice_seg - AV1 Segmentation params
 * @segmentation_enabled:        this frame makes use of the segmentation tool or not
 * @segmentation_update_map:     segmentation map are updated during the decoding frame
 * @segmentation_temporal_update:segmentation map are coded relative the existing segmentaion map
 * @segmentation_update_data:    new parameters are about to be specified for each segment
 * @feature_data:                specifies the feature data for a segment feature
 * @feature_enabled_mask:        the corresponding feature value is coded or not.
 * @segid_preskip:               segment id will be read before the skip syntax element.
 * @last_active_segid:           the highest numbered segment id that has some enabled feature
 */
struct vdec_av1_slice_seg {
	u8 segmentation_enabled;
	u8 segmentation_update_map;
	u8 segmentation_temporal_update;
	u8 segmentation_update_data;
	int feature_data[V4L2_AV1_MAX_SEGMENTS][V4L2_AV1_SEG_LVL_MAX];
	u16 feature_enabled_mask[V4L2_AV1_MAX_SEGMENTS];
	int segid_preskip;
	int last_active_segid;
};

/**
 * struct vdec_av1_slice_delta_q_lf - AV1 Loop Filter delta parameters
 * @delta_q_present:    specified whether quantizer index delta values are present
 * @delta_q_res:        specifies the left shift which should be applied to decoded quantizer index
 * @delta_lf_present:   specifies whether loop filter delta values are present
 * @delta_lf_res:       specifies the left shift which should be applied to decoded
 *                      loop filter delta values
 * @delta_lf_multi:     specifies that separate loop filter deltas are sent for horizontal
 *                      luma edges,vertical luma edges,the u edges, and the v edges.
 */
struct vdec_av1_slice_delta_q_lf {
	u8 delta_q_present;
	u8 delta_q_res;
	u8 delta_lf_present;
	u8 delta_lf_res;
	u8 delta_lf_multi;
};

/**
 * struct vdec_av1_slice_quantization - AV1 Quantization params
 * @base_q_idx:         indicates the base frame qindex. This is used for Y AC
 *                      coefficients and as the base value for the other quantizers.
 * @qindex:             qindex
 * @delta_qydc:         indicates the Y DC quantizer relative to base_q_idx
 * @delta_qudc:         indicates the U DC quantizer relative to base_q_idx.
 * @delta_quac:         indicates the U AC quantizer relative to base_q_idx
 * @delta_qvdc:         indicates the V DC quantizer relative to base_q_idx
 * @delta_qvac:         indicates the V AC quantizer relative to base_q_idx
 * @using_qmatrix:      specifies that the quantizer matrix will be used to
 *                      compute quantizers
 * @qm_y:               specifies the level in the quantizer matrix that should
 *                      be used for luma plane decoding
 * @qm_u:               specifies the level in the quantizer matrix that should
 *                      be used for chroma U plane decoding.
 * @qm_v:               specifies the level in the quantizer matrix that should be
 *                      used for chroma V plane decoding
 */
struct vdec_av1_slice_quantization {
	int base_q_idx;
	int qindex[V4L2_AV1_MAX_SEGMENTS];
	int delta_qydc;
	int delta_qudc;
	int delta_quac;
	int delta_qvdc;
	int delta_qvac;
	u8 using_qmatrix;
	u8 qm_y;
	u8 qm_u;
	u8 qm_v;
};

/**
 * struct vdec_av1_slice_lr - AV1 Loop Restauration parameters
 * @use_lr:                     whether to use loop restoration
 * @use_chroma_lr:              whether to use chroma loop restoration
 * @frame_restoration_type:     specifies the type of restoration used for each plane
 * @loop_restoration_size:      pecifies the size of loop restoration units in units
 *                              of samples in the current plane
 */
struct vdec_av1_slice_lr {
	u8 use_lr;
	u8 use_chroma_lr;
	u8 frame_restoration_type[V4L2_AV1_NUM_PLANES_MAX];
	u32 loop_restoration_size[V4L2_AV1_NUM_PLANES_MAX];
};

/**
 * struct vdec_av1_slice_loop_filter - AV1 Loop filter parameters
 * @loop_filter_level:          an array containing loop filter strength values.
 * @loop_filter_ref_deltas:     contains the adjustment needed for the filter
 *                              level based on the chosen reference frame
 * @loop_filter_mode_deltas:    contains the adjustment needed for the filter
 *                              level based on the chosen mode
 * @loop_filter_sharpness:      indicates the sharpness level. The loop_filter_level
 *                              and loop_filter_sharpness together determine when
 *                              a block edge is filtered, and by how much the
 *                              filtering can change the sample values
 * @loop_filter_delta_enabled:  filetr level depends on the mode and reference
 *                              frame used to predict a block
 */
struct vdec_av1_slice_loop_filter {
	u8 loop_filter_level[4];
	int loop_filter_ref_deltas[V4L2_AV1_TOTAL_REFS_PER_FRAME];
	int loop_filter_mode_deltas[2];
	u8 loop_filter_sharpness;
	u8 loop_filter_delta_enabled;
};

/**
 * struct vdec_av1_slice_cdef - AV1 CDEF parameters
 * @cdef_damping:       controls the amount of damping in the deringing filter
 * @cdef_y_strength:    specifies the strength of the primary filter and secondary filter
 * @cdef_uv_strength:   specifies the strength of the primary filter and secondary filter
 * @cdef_bits:          specifies the number of bits needed to specify which
 *                      CDEF filter to apply
 */
struct vdec_av1_slice_cdef {
	u8 cdef_damping;
	u8 cdef_y_strength[8];
	u8 cdef_uv_strength[8];
	u8 cdef_bits;
};

/**
 * struct vdec_av1_slice_mfmv - AV1 mfmv parameters
 * @mfmv_valid_ref:     mfmv_valid_ref
 * @mfmv_dir:           mfmv_dir
 * @mfmv_ref_to_cur:    mfmv_ref_to_cur
 * @mfmv_ref_frame_idx: mfmv_ref_frame_idx
 * @mfmv_count:         mfmv_count
 */
struct vdec_av1_slice_mfmv {
	u32 mfmv_valid_ref[3];
	u32 mfmv_dir[3];
	int mfmv_ref_to_cur[3];
	int mfmv_ref_frame_idx[3];
	int mfmv_count;
};

/**
 * struct vdec_av1_slice_tile - AV1 Tile info
 * @tile_cols:                  specifies the number of tiles across the frame
 * @tile_rows:                  pecifies the number of tiles down the frame
 * @mi_col_starts:              an array specifying the start column
 * @mi_row_starts:              an array specifying the start row
 * @context_update_tile_id:     specifies which tile to use for the CDF update
 * @uniform_tile_spacing_flag:  tiles are uniformly spaced across the frame
 *                              or the tile sizes are coded
 */
struct vdec_av1_slice_tile {
	u8 tile_cols;
	u8 tile_rows;
	int mi_col_starts[V4L2_AV1_MAX_TILE_COLS + 1];
	int mi_row_starts[V4L2_AV1_MAX_TILE_ROWS + 1];
	u8 context_update_tile_id;
	u8 uniform_tile_spacing_flag;
};

/**
 * struct vdec_av1_slice_uncompressed_header - Represents an AV1 Frame Header OBU
 * @use_ref_frame_mvs:          use_ref_frame_mvs flag
 * @order_hint:                 specifies OrderHintBits least significant bits of the expected
 * @gm:                         global motion param
 * @upscaled_width:             the upscaled width
 * @frame_width:                frame's width
 * @frame_height:               frame's height
 * @reduced_tx_set:             frame is restricted to a reduced subset of the full
 *                              set of transform types
 * @tx_mode:                    specifies how the transform size is determined
 * @uniform_tile_spacing_flag:  tiles are uniformly spaced across the frame
 *                              or the tile sizes are coded
 * @interpolation_filter:       specifies the filter selection used for performing inter prediction
 * @allow_warped_motion:        motion_mode may be present or not
 * @is_motion_mode_switchable : euqlt to 0 specifies that only the SIMPLE motion mode will be used
 * @reference_mode :            frame reference mode selected
 * @allow_high_precision_mv:    specifies that motion vectors are specified to
 *                              quarter pel precision or to eighth pel precision
 * @allow_intra_bc:             ubducates that intra block copy may be used in this frame
 * @force_integer_mv:           specifies motion vectors will always be integers or
 *                              can contain fractional bits
 * @allow_screen_content_tools: intra blocks may use palette encoding
 * @error_resilient_mode:       error resislent mode is enable/disable
 * @frame_type:                 specifies the AV1 frame type
 * @primary_ref_frame:          specifies which reference frame contains the CDF values
 *                              and other state that should be loaded at the start of the frame
 *                              slots will be updated with the current frame after it is decoded
 * @disable_frame_end_update_cdf:indicates the end of frame CDF update is disable or enable
 * @disable_cdf_update:         specified whether the CDF update in the symbol
 *                              decoding process should be disables
 * @skip_mode:                  av1 skip mode parameters
 * @seg:                        av1 segmentaon parameters
 * @delta_q_lf:                 av1 delta loop fileter
 * @quant:                      av1 Quantization params
 * @lr:                         av1 Loop Restauration parameters
 * @superres_denom:             the denominator for the upscaling ratio
 * @loop_filter:                av1 Loop filter parameters
 * @cdef:                       av1 CDEF parameters
 * @mfmv:                       av1 mfmv parameters
 * @tile:                       av1 Tile info
 * @frame_is_intra:             intra frame
 * @loss_less_array:            loss less array
 * @coded_loss_less:            coded lsss less
 * @mi_rows:                    size of mi unit in rows
 * @mi_cols:                    size of mi unit in cols
 */
struct vdec_av1_slice_uncompressed_header {
	u8 use_ref_frame_mvs;
	int order_hint;
	struct vdec_av1_slice_gm gm[V4L2_AV1_TOTAL_REFS_PER_FRAME];
	u32 upscaled_width;
	u32 frame_width;
	u32 frame_height;
	u8 reduced_tx_set;
	u8 tx_mode;
	u8 uniform_tile_spacing_flag;
	u8 interpolation_filter;
	u8 allow_warped_motion;
	u8 is_motion_mode_switchable;
	u8 reference_mode;
	u8 allow_high_precision_mv;
	u8 allow_intra_bc;
	u8 force_integer_mv;
	u8 allow_screen_content_tools;
	u8 error_resilient_mode;
	u8 frame_type;
	u8 primary_ref_frame;
	u8 disable_frame_end_update_cdf;
	u32 disable_cdf_update;
	struct vdec_av1_slice_sm skip_mode;
	struct vdec_av1_slice_seg seg;
	struct vdec_av1_slice_delta_q_lf delta_q_lf;
	struct vdec_av1_slice_quantization quant;
	struct vdec_av1_slice_lr lr;
	u32 superres_denom;
	struct vdec_av1_slice_loop_filter loop_filter;
	struct vdec_av1_slice_cdef cdef;
	struct vdec_av1_slice_mfmv mfmv;
	struct vdec_av1_slice_tile tile;
	u8 frame_is_intra;
	u8 loss_less_array[V4L2_AV1_MAX_SEGMENTS];
	u8 coded_loss_less;
	u32 mi_rows;
	u32 mi_cols;
};

/**
 * struct vdec_av1_slice_seq_header - Represents an AV1 Sequence OBU
 * @bitdepth:                   the bitdepth to use for the sequence
 * @enable_superres:            specifies whether the use_superres syntax element may be present
 * @enable_filter_intra:        specifies the use_filter_intra syntax element may be present
 * @enable_intra_edge_filter:   whether the intra edge filtering process should be enabled
 * @enable_interintra_compound: specifies the mode info fo rinter blocks may
 *                              contain the syntax element interintra
 * @enable_masked_compound:     specifies the mode info fo rinter blocks may
 *                              contain the syntax element compound_type
 * @enable_dual_filter:         the inter prediction filter type may be specified independently
 * @enable_jnt_comp:            distance weights process may be used for inter prediction
 * @mono_chrome:                indicates the video does not contain U and V color planes
 * @enable_order_hint:          tools based on the values of order hints may be used
 * @order_hint_bits:            the number of bits used for the order_hint field at each frame
 * @use_128x128_superblock:     indicates superblocks contain 128*128 luma samples
 * @subsampling_x:              the chroma subsamling format
 * @subsampling_y:              the chroma subsamling format
 * @max_frame_width:            the maximum frame width for the frames represented by sequence
 * @max_frame_height:           the maximum frame height for the frames represented by sequence
 */
struct vdec_av1_slice_seq_header {
	u8 bitdepth;
	u8 enable_superres;
	u8 enable_filter_intra;
	u8 enable_intra_edge_filter;
	u8 enable_interintra_compound;
	u8 enable_masked_compound;
	u8 enable_dual_filter;
	u8 enable_jnt_comp;
	u8 mono_chrome;
	u8 enable_order_hint;
	u8 order_hint_bits;
	u8 use_128x128_superblock;
	u8 subsampling_x;
	u8 subsampling_y;
	u32 max_frame_width;
	u32 max_frame_height;
};

/**
 * struct vdec_av1_slice_frame - Represents current Frame info
 * @uh:                         uncompressed header info
 * @seq:                        sequence header info
 * @large_scale_tile:           is large scale mode
 * @cur_ts:                     current frame timestamp
 * @prev_fb_idx:                prev slot id
 * @ref_frame_sign_bias:        arrays for ref_frame sign bias
 * @order_hints:                arrays for ref_frame order hint
 * @ref_frame_valid:            arrays for valid ref_frame
 * @ref_frame_map:              map to slot frame info
 * @frame_refs:                 ref_frame info
 */
struct vdec_av1_slice_frame {
	struct vdec_av1_slice_uncompressed_header uh;
	struct vdec_av1_slice_seq_header seq;
	u8 large_scale_tile;
	u64 cur_ts;
	int prev_fb_idx;
	u8 ref_frame_sign_bias[V4L2_AV1_TOTAL_REFS_PER_FRAME];
	u32 order_hints[V4L2_AV1_REFS_PER_FRAME];
	u32 ref_frame_valid[V4L2_AV1_REFS_PER_FRAME];
	int ref_frame_map[V4L2_AV1_TOTAL_REFS_PER_FRAME];
	struct vdec_av1_slice_frame_refs frame_refs[V4L2_AV1_REFS_PER_FRAME];
};

/**
 * struct vdec_av1_slice_work_buffer - work buffer for lat
 * @mv_addr:    mv buffer memory info
 * @cdf_addr:   cdf buffer memory info
 * @segid_addr: segid buffer memory info
 */
struct vdec_av1_slice_work_buffer {
	struct vdec_av1_slice_mem mv_addr;
	struct vdec_av1_slice_mem cdf_addr;
	struct vdec_av1_slice_mem segid_addr;
};

/**
 * struct vdec_av1_slice_frame_info - frame info for each slot
 * @frame_type:         frame type
 * @frame_is_intra:     is intra frame
 * @order_hint:         order hint
 * @order_hints:        referece frame order hint
 * @upscaled_width:     upscale width
 * @pic_pitch:          buffer pitch
 * @frame_width:        frane width
 * @frame_height:       frame height
 * @mi_rows:            rows in mode info
 * @mi_cols:            cols in mode info
 * @ref_count:          mark to reference frame counts
 */
struct vdec_av1_slice_frame_info {
	u8 frame_type;
	u8 frame_is_intra;
	int order_hint;
	u32 order_hints[V4L2_AV1_REFS_PER_FRAME];
	u32 upscaled_width;
	u32 pic_pitch;
	u32 frame_width;
	u32 frame_height;
	u32 mi_rows;
	u32 mi_cols;
	int ref_count;
};

/**
 * struct vdec_av1_slice_slot - slot info that needs to be saved in the global instance
 * @frame_info: frame info for each slot
 * @timestamp:  time stamp info
 */
struct vdec_av1_slice_slot {
	struct vdec_av1_slice_frame_info frame_info[AV1_MAX_FRAME_BUF_COUNT];
	u64 timestamp[AV1_MAX_FRAME_BUF_COUNT];
};

/**
 * struct vdec_av1_slice_fb - frame buffer for decoding
 * @y:  current y buffer address info
 * @c:  current c buffer address info
 */
struct vdec_av1_slice_fb {
	struct vdec_av1_slice_mem y;
	struct vdec_av1_slice_mem c;
};

/**
 * struct vdec_av1_slice_vsi - exchange frame information between Main CPU and MicroP
 * @bs:			input buffer info
 * @work_buffer:	working buffe for hw
 * @cdf_table:		cdf_table buffer
 * @cdf_tmp:		cdf temp buffer
 * @rd_mv:		mv buffer for lat output , core input
 * @ube:		ube buffer
 * @trans:		transcoded buffer
 * @err_map:		err map buffer
 * @row_info:		row info buffer
 * @fb:			current y/c buffer
 * @ref:		ref y/c buffer
 * @iq_table:		iq table buffer
 * @tile:		tile buffer
 * @slots:		slots info for each frame
 * @slot_id:		current frame slot id
 * @frame:		current frame info
 * @state:		status after decode done
 * @cur_lst_tile_id:	tile id for large scale
 */
struct vdec_av1_slice_vsi {
	/* lat */
	struct vdec_av1_slice_mem bs;
	struct vdec_av1_slice_work_buffer work_buffer[AV1_MAX_FRAME_BUF_COUNT];
	struct vdec_av1_slice_mem cdf_table;
	struct vdec_av1_slice_mem cdf_tmp;
	/* LAT stage's output, Core stage's input */
	struct vdec_av1_slice_mem rd_mv;
	struct vdec_av1_slice_mem ube;
	struct vdec_av1_slice_mem trans;
	struct vdec_av1_slice_mem err_map;
	struct vdec_av1_slice_mem row_info;
	/* core */
	struct vdec_av1_slice_fb fb;
	struct vdec_av1_slice_fb ref[V4L2_AV1_REFS_PER_FRAME];
	struct vdec_av1_slice_mem iq_table;
	/* lat and core share*/
	struct vdec_av1_slice_mem tile;
	struct vdec_av1_slice_slot slots;
	s8 slot_id;
	struct vdec_av1_slice_frame frame;
	struct vdec_av1_slice_state state;
	u32 cur_lst_tile_id;
};

/**
 * struct vdec_av1_slice_pfc - per-frame context that contains a local vsi.
 *                             pass it from lat to core
 * @vsi:        local vsi. copy to/from remote vsi before/after decoding
 * @ref_idx:    reference buffer timestamp
 * @seq:        picture sequence
 */
struct vdec_av1_slice_pfc {
	struct vdec_av1_slice_vsi vsi;
	u64 ref_idx[V4L2_AV1_REFS_PER_FRAME];
	int seq;
};

/**
 * struct vdec_av1_slice_instance - represent one av1 instance
 * @ctx:                pointer to codec's context
 * @vpu:                VPU instance
 * @iq_table:           iq table buffer
 * @cdf_table:          cdf table buffer
 * @mv:                 mv working buffer
 * @cdf:                cdf working buffer
 * @seg:                segmentation working buffer
 * @cdf_temp:           cdf temp buffer
 * @tile:               tile buffer
 * @slots:              slots info
 * @tile_group:         tile_group entry
 * @level:              level of current resolution
 * @width:              width of last picture
 * @height:             height of last picture
 * @frame_type:         frame_type of last picture
 * @irq_enabled:        irq to Main CPU or MicroP
 * @inneracing_mode:    is inneracing mode
 * @init_vsi:           vsi used for initialized AV1 instance
 * @vsi:                vsi used for decoding/flush ...
 * @core_vsi:           vsi used for Core stage
 * @seq:                global picture sequence
 */
struct vdec_av1_slice_instance {
	struct mtk_vcodec_dec_ctx *ctx;
	struct vdec_vpu_inst vpu;

	struct mtk_vcodec_mem iq_table;
	struct mtk_vcodec_mem cdf_table;

	struct mtk_vcodec_mem mv[AV1_MAX_FRAME_BUF_COUNT];
	struct mtk_vcodec_mem cdf[AV1_MAX_FRAME_BUF_COUNT];
	struct mtk_vcodec_mem seg[AV1_MAX_FRAME_BUF_COUNT];
	struct mtk_vcodec_mem cdf_temp;
	struct mtk_vcodec_mem tile;
	struct vdec_av1_slice_slot slots;
	struct vdec_av1_slice_tile_group tile_group;

	/* for resolution change and get_pic_info */
	enum vdec_av1_slice_resolution_level level;
	u32 width;
	u32 height;

	u32 frame_type;
	u32 irq_enabled;
	u32 inneracing_mode;

	/* MicroP vsi */
	union {
		struct vdec_av1_slice_init_vsi *init_vsi;
		struct vdec_av1_slice_vsi *vsi;
	};
	struct vdec_av1_slice_vsi *core_vsi;
	int seq;
};

static int vdec_av1_slice_core_decode(struct vdec_lat_buf *lat_buf);

static inline int vdec_av1_slice_get_msb(u32 n)
{
	if (n == 0)
		return 0;
	return 31 ^ __builtin_clz(n);
}

static inline bool vdec_av1_slice_need_scale(u32 ref_width, u32 ref_height,
					     u32 this_width, u32 this_height)
{
	return ((this_width << 1) >= ref_width) &&
		((this_height << 1) >= ref_height) &&
		(this_width <= (ref_width << 4)) &&
		(this_height <= (ref_height << 4));
}

static void *vdec_av1_get_ctrl_ptr(struct mtk_vcodec_dec_ctx *ctx, int id)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl, id);

	if (!ctrl)
		return ERR_PTR(-EINVAL);

	return ctrl->p_cur.p;
}

static int vdec_av1_slice_init_cdf_table(struct vdec_av1_slice_instance *instance)
{
	u8 *remote_cdf_table;
	struct mtk_vcodec_dec_ctx *ctx;
	struct vdec_av1_slice_init_vsi *vsi;
	int ret;

	ctx = instance->ctx;
	vsi = instance->vpu.vsi;
	remote_cdf_table = mtk_vcodec_fw_map_dm_addr(ctx->dev->fw_handler,
						     (u32)vsi->cdf_table_addr);
	if (IS_ERR(remote_cdf_table)) {
		mtk_vdec_err(ctx, "failed to map cdf table\n");
		return PTR_ERR(remote_cdf_table);
	}

	mtk_vdec_debug(ctx, "map cdf table to 0x%p\n", remote_cdf_table);

	if (instance->cdf_table.va)
		mtk_vcodec_mem_free(ctx, &instance->cdf_table);
	instance->cdf_table.size = vsi->cdf_table_size;

	ret = mtk_vcodec_mem_alloc(ctx, &instance->cdf_table);
	if (ret)
		return ret;

	memcpy(instance->cdf_table.va, remote_cdf_table, vsi->cdf_table_size);

	return 0;
}

static int vdec_av1_slice_init_iq_table(struct vdec_av1_slice_instance *instance)
{
	u8 *remote_iq_table;
	struct mtk_vcodec_dec_ctx *ctx;
	struct vdec_av1_slice_init_vsi *vsi;
	int ret;

	ctx = instance->ctx;
	vsi = instance->vpu.vsi;
	remote_iq_table = mtk_vcodec_fw_map_dm_addr(ctx->dev->fw_handler,
						    (u32)vsi->iq_table_addr);
	if (IS_ERR(remote_iq_table)) {
		mtk_vdec_err(ctx, "failed to map iq table\n");
		return PTR_ERR(remote_iq_table);
	}

	mtk_vdec_debug(ctx, "map iq table to 0x%p\n", remote_iq_table);

	if (instance->iq_table.va)
		mtk_vcodec_mem_free(ctx, &instance->iq_table);
	instance->iq_table.size = vsi->iq_table_size;

	ret = mtk_vcodec_mem_alloc(ctx, &instance->iq_table);
	if (ret)
		return ret;

	memcpy(instance->iq_table.va, remote_iq_table, vsi->iq_table_size);

	return 0;
}

static int vdec_av1_slice_get_new_slot(struct vdec_av1_slice_vsi *vsi)
{
	struct vdec_av1_slice_slot *slots = &vsi->slots;
	int new_slot_idx = AV1_INVALID_IDX;
	int i;

	for (i = 0; i < AV1_MAX_FRAME_BUF_COUNT; i++) {
		if (slots->frame_info[i].ref_count == 0) {
			new_slot_idx = i;
			break;
		}
	}

	if (new_slot_idx != AV1_INVALID_IDX) {
		slots->frame_info[new_slot_idx].ref_count++;
		slots->timestamp[new_slot_idx] = vsi->frame.cur_ts;
	}

	return new_slot_idx;
}

static inline void vdec_av1_slice_clear_fb(struct vdec_av1_slice_frame_info *frame_info)
{
	memset((void *)frame_info, 0, sizeof(struct vdec_av1_slice_frame_info));
}

static void vdec_av1_slice_decrease_ref_count(struct vdec_av1_slice_slot *slots, int fb_idx)
{
	struct vdec_av1_slice_frame_info *frame_info = slots->frame_info;

	frame_info[fb_idx].ref_count--;
	if (frame_info[fb_idx].ref_count < 0) {
		frame_info[fb_idx].ref_count = 0;
		pr_err(MTK_DBG_V4L2_STR "av1_error: %s() fb_idx %d decrease ref_count error\n",
		       __func__, fb_idx);
	}

	vdec_av1_slice_clear_fb(&frame_info[fb_idx]);
}

static void vdec_av1_slice_cleanup_slots(struct vdec_av1_slice_slot *slots,
					 struct vdec_av1_slice_frame *frame,
					 struct v4l2_ctrl_av1_frame *ctrl_fh)
{
	int slot_id, ref_id;

	for (ref_id = 0; ref_id < V4L2_AV1_TOTAL_REFS_PER_FRAME; ref_id++)
		frame->ref_frame_map[ref_id] = AV1_INVALID_IDX;

	for (slot_id = 0; slot_id < AV1_MAX_FRAME_BUF_COUNT; slot_id++) {
		u64 timestamp = slots->timestamp[slot_id];
		bool ref_used = false;

		/* ignored unused slots */
		if (slots->frame_info[slot_id].ref_count == 0)
			continue;

		for (ref_id = 0; ref_id < V4L2_AV1_TOTAL_REFS_PER_FRAME; ref_id++) {
			if (ctrl_fh->reference_frame_ts[ref_id] == timestamp) {
				frame->ref_frame_map[ref_id] = slot_id;
				ref_used = true;
			}
		}

		if (!ref_used)
			vdec_av1_slice_decrease_ref_count(slots, slot_id);
	}
}

static void vdec_av1_slice_setup_slot(struct vdec_av1_slice_instance *instance,
				      struct vdec_av1_slice_vsi *vsi,
				      struct v4l2_ctrl_av1_frame *ctrl_fh)
{
	struct vdec_av1_slice_frame_info *cur_frame_info;
	struct vdec_av1_slice_uncompressed_header *uh = &vsi->frame.uh;
	int ref_id;

	memcpy(&vsi->slots, &instance->slots, sizeof(instance->slots));
	vdec_av1_slice_cleanup_slots(&vsi->slots, &vsi->frame, ctrl_fh);
	vsi->slot_id = vdec_av1_slice_get_new_slot(vsi);

	if (vsi->slot_id == AV1_INVALID_IDX) {
		mtk_v4l2_vdec_err(instance->ctx, "warning:av1 get invalid index slot\n");
		vsi->slot_id = 0;
	}
	cur_frame_info = &vsi->slots.frame_info[vsi->slot_id];
	cur_frame_info->frame_type = uh->frame_type;
	cur_frame_info->frame_is_intra = ((uh->frame_type == AV1_INTRA_ONLY_FRAME) ||
					  (uh->frame_type == AV1_KEY_FRAME));
	cur_frame_info->order_hint = uh->order_hint;
	cur_frame_info->upscaled_width = uh->upscaled_width;
	cur_frame_info->pic_pitch = 0;
	cur_frame_info->frame_width = uh->frame_width;
	cur_frame_info->frame_height = uh->frame_height;
	cur_frame_info->mi_cols = ((uh->frame_width + 7) >> 3) << 1;
	cur_frame_info->mi_rows = ((uh->frame_height + 7) >> 3) << 1;

	/* ensure current frame is properly mapped if referenced */
	for (ref_id = 0; ref_id < V4L2_AV1_TOTAL_REFS_PER_FRAME; ref_id++) {
		u64 timestamp = vsi->slots.timestamp[vsi->slot_id];

		if (ctrl_fh->reference_frame_ts[ref_id] == timestamp)
			vsi->frame.ref_frame_map[ref_id] = vsi->slot_id;
	}
}

static int vdec_av1_slice_alloc_working_buffer(struct vdec_av1_slice_instance *instance,
					       struct vdec_av1_slice_vsi *vsi)
{
	struct mtk_vcodec_dec_ctx *ctx = instance->ctx;
	enum vdec_av1_slice_resolution_level level;
	u32 max_sb_w, max_sb_h, max_w, max_h, w, h;
	int i, ret;

	w = vsi->frame.uh.frame_width;
	h = vsi->frame.uh.frame_height;

	if (w > VCODEC_DEC_4K_CODED_WIDTH || h > VCODEC_DEC_4K_CODED_HEIGHT)
		/* 8K */
		return -EINVAL;

	if (w > MTK_VDEC_MAX_W || h > MTK_VDEC_MAX_H) {
		/* 4K */
		level = AV1_RES_4K;
		max_w = VCODEC_DEC_4K_CODED_WIDTH;
		max_h = VCODEC_DEC_4K_CODED_HEIGHT;
	} else {
		/* FHD */
		level = AV1_RES_FHD;
		max_w = MTK_VDEC_MAX_W;
		max_h = MTK_VDEC_MAX_H;
	}

	if (level == instance->level)
		return 0;

	mtk_vdec_debug(ctx, "resolution level changed from %u to %u, %ux%u",
		       instance->level, level, w, h);

	max_sb_w = DIV_ROUND_UP(max_w, 128);
	max_sb_h = DIV_ROUND_UP(max_h, 128);

	for (i = 0; i < AV1_MAX_FRAME_BUF_COUNT; i++) {
		if (instance->mv[i].va)
			mtk_vcodec_mem_free(ctx, &instance->mv[i]);
		instance->mv[i].size = max_sb_w * max_sb_h * SZ_1K;
		ret = mtk_vcodec_mem_alloc(ctx, &instance->mv[i]);
		if (ret)
			goto err;

		if (instance->seg[i].va)
			mtk_vcodec_mem_free(ctx, &instance->seg[i]);
		instance->seg[i].size = max_sb_w * max_sb_h * 512;
		ret = mtk_vcodec_mem_alloc(ctx, &instance->seg[i]);
		if (ret)
			goto err;

		if (instance->cdf[i].va)
			mtk_vcodec_mem_free(ctx, &instance->cdf[i]);
		instance->cdf[i].size = AV1_CDF_TABLE_BUFFER_SIZE;
		ret = mtk_vcodec_mem_alloc(ctx, &instance->cdf[i]);
		if (ret)
			goto err;
	}

	if (!instance->cdf_temp.va) {
		instance->cdf_temp.size = (SZ_1K * 16 * 100);
		ret = mtk_vcodec_mem_alloc(ctx, &instance->cdf_temp);
		if (ret)
			goto err;
		vsi->cdf_tmp.buf = instance->cdf_temp.dma_addr;
		vsi->cdf_tmp.size = instance->cdf_temp.size;
	}

	if (instance->tile.va)
		mtk_vcodec_mem_free(ctx, &instance->tile);

	instance->tile.size = AV1_TILE_BUF_SIZE * V4L2_AV1_MAX_TILE_COUNT;
	ret = mtk_vcodec_mem_alloc(ctx, &instance->tile);
	if (ret)
		goto err;

	instance->level = level;
	return 0;

err:
	instance->level = AV1_RES_NONE;
	return ret;
}

static void vdec_av1_slice_free_working_buffer(struct vdec_av1_slice_instance *instance)
{
	struct mtk_vcodec_dec_ctx *ctx = instance->ctx;
	int i;

	for (i = 0; i < ARRAY_SIZE(instance->mv); i++)
		mtk_vcodec_mem_free(ctx, &instance->mv[i]);

	for (i = 0; i < ARRAY_SIZE(instance->seg); i++)
		mtk_vcodec_mem_free(ctx, &instance->seg[i]);

	for (i = 0; i < ARRAY_SIZE(instance->cdf); i++)
		mtk_vcodec_mem_free(ctx, &instance->cdf[i]);

	mtk_vcodec_mem_free(ctx, &instance->tile);
	mtk_vcodec_mem_free(ctx, &instance->cdf_temp);
	mtk_vcodec_mem_free(ctx, &instance->cdf_table);
	mtk_vcodec_mem_free(ctx, &instance->iq_table);

	instance->level = AV1_RES_NONE;
}

static inline void vdec_av1_slice_vsi_from_remote(struct vdec_av1_slice_vsi *vsi,
						  struct vdec_av1_slice_vsi *remote_vsi)
{
	memcpy(&vsi->trans, &remote_vsi->trans, sizeof(vsi->trans));
	memcpy(&vsi->state, &remote_vsi->state, sizeof(vsi->state));
}

static inline void vdec_av1_slice_vsi_to_remote(struct vdec_av1_slice_vsi *vsi,
						struct vdec_av1_slice_vsi *remote_vsi)
{
	memcpy(remote_vsi, vsi, sizeof(*vsi));
}

static int vdec_av1_slice_setup_lat_from_src_buf(struct vdec_av1_slice_instance *instance,
						 struct vdec_av1_slice_vsi *vsi,
						 struct vdec_lat_buf *lat_buf)
{
	struct vb2_v4l2_buffer *src;
	struct vb2_v4l2_buffer *dst;

	src = v4l2_m2m_next_src_buf(instance->ctx->m2m_ctx);
	if (!src)
		return -EINVAL;

	lat_buf->src_buf_req = src->vb2_buf.req_obj.req;
	dst = &lat_buf->ts_info;
	v4l2_m2m_buf_copy_metadata(src, dst, true);
	vsi->frame.cur_ts = dst->vb2_buf.timestamp;

	return 0;
}

static short vdec_av1_slice_resolve_divisor_32(u32 D, short *shift)
{
	int f;
	int e;

	*shift = vdec_av1_slice_get_msb(D);
	/* e is obtained from D after resetting the most significant 1 bit. */
	e = D - ((u32)1 << *shift);
	/* Get the most significant DIV_LUT_BITS (8) bits of e into f */
	if (*shift > DIV_LUT_BITS)
		f = AV1_DIV_ROUND_UP_POW2(e, *shift - DIV_LUT_BITS);
	else
		f = e << (DIV_LUT_BITS - *shift);
	if (f > DIV_LUT_NUM)
		return -1;
	*shift += DIV_LUT_PREC_BITS;
	/* Use f as lookup into the precomputed table of multipliers */
	return div_lut[f];
}

static void vdec_av1_slice_get_shear_params(struct vdec_av1_slice_gm *gm_params)
{
	const int *mat = gm_params->wmmat;
	short shift;
	short y;
	long long gv, dv;

	if (gm_params->wmmat[2] <= 0)
		return;

	gm_params->alpha = clamp_val(mat[2] - (1 << WARPEDMODEL_PREC_BITS), S16_MIN, S16_MAX);
	gm_params->beta = clamp_val(mat[3], S16_MIN, S16_MAX);

	y = vdec_av1_slice_resolve_divisor_32(abs(mat[2]), &shift) * (mat[2] < 0 ? -1 : 1);

	gv = ((long long)mat[4] * (1 << WARPEDMODEL_PREC_BITS)) * y;
	gm_params->gamma = clamp_val((int)AV1_DIV_ROUND_UP_POW2_SIGNED(gv, shift),
				     S16_MIN, S16_MAX);

	dv = ((long long)mat[3] * mat[4]) * y;
	gm_params->delta = clamp_val(mat[5] - (int)AV1_DIV_ROUND_UP_POW2_SIGNED(dv, shift) -
				     (1 << WARPEDMODEL_PREC_BITS), S16_MIN, S16_MAX);

	gm_params->alpha = AV1_DIV_ROUND_UP_POW2_SIGNED(gm_params->alpha, WARP_PARAM_REDUCE_BITS) *
							(1 << WARP_PARAM_REDUCE_BITS);
	gm_params->beta = AV1_DIV_ROUND_UP_POW2_SIGNED(gm_params->beta, WARP_PARAM_REDUCE_BITS) *
						       (1 << WARP_PARAM_REDUCE_BITS);
	gm_params->gamma = AV1_DIV_ROUND_UP_POW2_SIGNED(gm_params->gamma, WARP_PARAM_REDUCE_BITS) *
							(1 << WARP_PARAM_REDUCE_BITS);
	gm_params->delta = AV1_DIV_ROUND_UP_POW2_SIGNED(gm_params->delta, WARP_PARAM_REDUCE_BITS) *
							(1 << WARP_PARAM_REDUCE_BITS);
}

static void vdec_av1_slice_setup_gm(struct vdec_av1_slice_gm *gm,
				    struct v4l2_av1_global_motion *ctrl_gm)
{
	u32 i, j;

	for (i = 0; i < V4L2_AV1_TOTAL_REFS_PER_FRAME; i++) {
		gm[i].wmtype = ctrl_gm->type[i];
		for (j = 0; j < 6; j++)
			gm[i].wmmat[j] = ctrl_gm->params[i][j];

		gm[i].invalid = !!(ctrl_gm->invalid & BIT(i));
		gm[i].alpha = 0;
		gm[i].beta = 0;
		gm[i].gamma = 0;
		gm[i].delta = 0;
		if (gm[i].wmtype <= V4L2_AV1_WARP_MODEL_AFFINE)
			vdec_av1_slice_get_shear_params(&gm[i]);
	}
}

static void vdec_av1_slice_setup_seg(struct vdec_av1_slice_seg *seg,
				     struct v4l2_av1_segmentation *ctrl_seg)
{
	u32 i, j;

	seg->segmentation_enabled = SEGMENTATION_FLAG(ctrl_seg, ENABLED);
	seg->segmentation_update_map = SEGMENTATION_FLAG(ctrl_seg, UPDATE_MAP);
	seg->segmentation_temporal_update = SEGMENTATION_FLAG(ctrl_seg, TEMPORAL_UPDATE);
	seg->segmentation_update_data = SEGMENTATION_FLAG(ctrl_seg, UPDATE_DATA);
	seg->segid_preskip = SEGMENTATION_FLAG(ctrl_seg, SEG_ID_PRE_SKIP);
	seg->last_active_segid = ctrl_seg->last_active_seg_id;

	for (i = 0; i < V4L2_AV1_MAX_SEGMENTS; i++) {
		seg->feature_enabled_mask[i] = ctrl_seg->feature_enabled[i];
		for (j = 0; j < V4L2_AV1_SEG_LVL_MAX; j++)
			seg->feature_data[i][j] = ctrl_seg->feature_data[i][j];
	}
}

static void vdec_av1_slice_setup_quant(struct vdec_av1_slice_quantization *quant,
				       struct v4l2_av1_quantization *ctrl_quant)
{
	quant->base_q_idx = ctrl_quant->base_q_idx;
	quant->delta_qydc = ctrl_quant->delta_q_y_dc;
	quant->delta_qudc = ctrl_quant->delta_q_u_dc;
	quant->delta_quac = ctrl_quant->delta_q_u_ac;
	quant->delta_qvdc = ctrl_quant->delta_q_v_dc;
	quant->delta_qvac = ctrl_quant->delta_q_v_ac;
	quant->qm_y = ctrl_quant->qm_y;
	quant->qm_u = ctrl_quant->qm_u;
	quant->qm_v = ctrl_quant->qm_v;
	quant->using_qmatrix = QUANT_FLAG(ctrl_quant, USING_QMATRIX);
}

static int vdec_av1_slice_get_qindex(struct vdec_av1_slice_uncompressed_header *uh,
				     int segmentation_id)
{
	struct vdec_av1_slice_seg *seg = &uh->seg;
	struct vdec_av1_slice_quantization *quant = &uh->quant;
	int data = 0, qindex = 0;

	if (seg->segmentation_enabled &&
	    (seg->feature_enabled_mask[segmentation_id] & BIT(SEG_LVL_ALT_Q))) {
		data = seg->feature_data[segmentation_id][SEG_LVL_ALT_Q];
		qindex = quant->base_q_idx + data;
		return clamp_val(qindex, 0, MAXQ);
	}

	return quant->base_q_idx;
}

static void vdec_av1_slice_setup_lr(struct vdec_av1_slice_lr *lr,
				    struct v4l2_av1_loop_restoration  *ctrl_lr)
{
	int i;

	lr->use_lr = 0;
	lr->use_chroma_lr = 0;
	for (i = 0; i < V4L2_AV1_NUM_PLANES_MAX; i++) {
		lr->frame_restoration_type[i] = ctrl_lr->frame_restoration_type[i];
		lr->loop_restoration_size[i] = ctrl_lr->loop_restoration_size[i];
		if (lr->frame_restoration_type[i]) {
			lr->use_lr = 1;
			if (i > 0)
				lr->use_chroma_lr = 1;
		}
	}
}

static void vdec_av1_slice_setup_lf(struct vdec_av1_slice_loop_filter *lf,
				    struct v4l2_av1_loop_filter *ctrl_lf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lf->loop_filter_level); i++)
		lf->loop_filter_level[i] = ctrl_lf->level[i];

	for (i = 0; i < V4L2_AV1_TOTAL_REFS_PER_FRAME; i++)
		lf->loop_filter_ref_deltas[i] = ctrl_lf->ref_deltas[i];

	for (i = 0; i < ARRAY_SIZE(lf->loop_filter_mode_deltas); i++)
		lf->loop_filter_mode_deltas[i] = ctrl_lf->mode_deltas[i];

	lf->loop_filter_sharpness = ctrl_lf->sharpness;
	lf->loop_filter_delta_enabled =
		   BIT_FLAG(ctrl_lf, V4L2_AV1_LOOP_FILTER_FLAG_DELTA_ENABLED);
}

static void vdec_av1_slice_setup_cdef(struct vdec_av1_slice_cdef *cdef,
				      struct v4l2_av1_cdef *ctrl_cdef)
{
	int i;

	cdef->cdef_damping = ctrl_cdef->damping_minus_3 + 3;
	cdef->cdef_bits = ctrl_cdef->bits;

	for (i = 0; i < V4L2_AV1_CDEF_MAX; i++) {
		if (ctrl_cdef->y_sec_strength[i] == 4)
			ctrl_cdef->y_sec_strength[i] -= 1;

		if (ctrl_cdef->uv_sec_strength[i] == 4)
			ctrl_cdef->uv_sec_strength[i] -= 1;

		cdef->cdef_y_strength[i] =
			ctrl_cdef->y_pri_strength[i] << SECONDARY_FILTER_STRENGTH_NUM_BITS |
			ctrl_cdef->y_sec_strength[i];
		cdef->cdef_uv_strength[i] =
			ctrl_cdef->uv_pri_strength[i] << SECONDARY_FILTER_STRENGTH_NUM_BITS |
			ctrl_cdef->uv_sec_strength[i];
	}
}

static void vdec_av1_slice_setup_seq(struct vdec_av1_slice_seq_header *seq,
				     struct v4l2_ctrl_av1_sequence *ctrl_seq)
{
	seq->bitdepth = ctrl_seq->bit_depth;
	seq->max_frame_width = ctrl_seq->max_frame_width_minus_1 + 1;
	seq->max_frame_height = ctrl_seq->max_frame_height_minus_1 + 1;
	seq->enable_superres = SEQUENCE_FLAG(ctrl_seq, ENABLE_SUPERRES);
	seq->enable_filter_intra = SEQUENCE_FLAG(ctrl_seq, ENABLE_FILTER_INTRA);
	seq->enable_intra_edge_filter = SEQUENCE_FLAG(ctrl_seq, ENABLE_INTRA_EDGE_FILTER);
	seq->enable_interintra_compound = SEQUENCE_FLAG(ctrl_seq, ENABLE_INTERINTRA_COMPOUND);
	seq->enable_masked_compound = SEQUENCE_FLAG(ctrl_seq, ENABLE_MASKED_COMPOUND);
	seq->enable_dual_filter = SEQUENCE_FLAG(ctrl_seq, ENABLE_DUAL_FILTER);
	seq->enable_jnt_comp = SEQUENCE_FLAG(ctrl_seq, ENABLE_JNT_COMP);
	seq->mono_chrome = SEQUENCE_FLAG(ctrl_seq, MONO_CHROME);
	seq->enable_order_hint = SEQUENCE_FLAG(ctrl_seq, ENABLE_ORDER_HINT);
	seq->order_hint_bits = ctrl_seq->order_hint_bits;
	seq->use_128x128_superblock = SEQUENCE_FLAG(ctrl_seq, USE_128X128_SUPERBLOCK);
	seq->subsampling_x = SEQUENCE_FLAG(ctrl_seq, SUBSAMPLING_X);
	seq->subsampling_y = SEQUENCE_FLAG(ctrl_seq, SUBSAMPLING_Y);
}

static void vdec_av1_slice_setup_tile(struct vdec_av1_slice_frame *frame,
				      struct v4l2_av1_tile_info *ctrl_tile)
{
	struct vdec_av1_slice_seq_header *seq = &frame->seq;
	struct vdec_av1_slice_tile *tile = &frame->uh.tile;
	u32 mib_size_log2 = seq->use_128x128_superblock ? 5 : 4;
	int i;

	tile->tile_cols = ctrl_tile->tile_cols;
	tile->tile_rows = ctrl_tile->tile_rows;
	tile->context_update_tile_id = ctrl_tile->context_update_tile_id;
	tile->uniform_tile_spacing_flag =
		BIT_FLAG(ctrl_tile, V4L2_AV1_TILE_INFO_FLAG_UNIFORM_TILE_SPACING);

	for (i = 0; i < tile->tile_cols + 1; i++)
		tile->mi_col_starts[i] =
			ALIGN(ctrl_tile->mi_col_starts[i], BIT(mib_size_log2)) >> mib_size_log2;

	for (i = 0; i < tile->tile_rows + 1; i++)
		tile->mi_row_starts[i] =
			ALIGN(ctrl_tile->mi_row_starts[i], BIT(mib_size_log2)) >> mib_size_log2;
}

static void vdec_av1_slice_setup_uh(struct vdec_av1_slice_instance *instance,
				    struct vdec_av1_slice_frame *frame,
				    struct v4l2_ctrl_av1_frame *ctrl_fh)
{
	struct vdec_av1_slice_uncompressed_header *uh = &frame->uh;
	int i;

	uh->use_ref_frame_mvs = FH_FLAG(ctrl_fh, USE_REF_FRAME_MVS);
	uh->order_hint = ctrl_fh->order_hint;
	vdec_av1_slice_setup_gm(uh->gm, &ctrl_fh->global_motion);
	uh->upscaled_width = ctrl_fh->upscaled_width;
	uh->frame_width = ctrl_fh->frame_width_minus_1 + 1;
	uh->frame_height = ctrl_fh->frame_height_minus_1 + 1;
	uh->mi_cols = ((uh->frame_width + 7) >> 3) << 1;
	uh->mi_rows = ((uh->frame_height + 7) >> 3) << 1;
	uh->reduced_tx_set = FH_FLAG(ctrl_fh, REDUCED_TX_SET);
	uh->tx_mode = ctrl_fh->tx_mode;
	uh->uniform_tile_spacing_flag =
		BIT_FLAG(&ctrl_fh->tile_info, V4L2_AV1_TILE_INFO_FLAG_UNIFORM_TILE_SPACING);
	uh->interpolation_filter = ctrl_fh->interpolation_filter;
	uh->allow_warped_motion = FH_FLAG(ctrl_fh, ALLOW_WARPED_MOTION);
	uh->is_motion_mode_switchable = FH_FLAG(ctrl_fh, IS_MOTION_MODE_SWITCHABLE);
	uh->frame_type = ctrl_fh->frame_type;
	uh->frame_is_intra = (uh->frame_type == V4L2_AV1_INTRA_ONLY_FRAME ||
			      uh->frame_type == V4L2_AV1_KEY_FRAME);

	if (!uh->frame_is_intra && FH_FLAG(ctrl_fh, REFERENCE_SELECT))
		uh->reference_mode = AV1_REFERENCE_MODE_SELECT;
	else
		uh->reference_mode = AV1_SINGLE_REFERENCE;

	uh->allow_high_precision_mv = FH_FLAG(ctrl_fh, ALLOW_HIGH_PRECISION_MV);
	uh->allow_intra_bc = FH_FLAG(ctrl_fh, ALLOW_INTRABC);
	uh->force_integer_mv = FH_FLAG(ctrl_fh, FORCE_INTEGER_MV);
	uh->allow_screen_content_tools = FH_FLAG(ctrl_fh, ALLOW_SCREEN_CONTENT_TOOLS);
	uh->error_resilient_mode = FH_FLAG(ctrl_fh, ERROR_RESILIENT_MODE);
	uh->primary_ref_frame = ctrl_fh->primary_ref_frame;
	uh->disable_frame_end_update_cdf =
			FH_FLAG(ctrl_fh, DISABLE_FRAME_END_UPDATE_CDF);
	uh->disable_cdf_update = FH_FLAG(ctrl_fh, DISABLE_CDF_UPDATE);
	uh->skip_mode.skip_mode_present = FH_FLAG(ctrl_fh, SKIP_MODE_PRESENT);
	uh->skip_mode.skip_mode_frame[0] =
		ctrl_fh->skip_mode_frame[0] - V4L2_AV1_REF_LAST_FRAME;
	uh->skip_mode.skip_mode_frame[1] =
		ctrl_fh->skip_mode_frame[1] - V4L2_AV1_REF_LAST_FRAME;
	uh->skip_mode.skip_mode_allowed = ctrl_fh->skip_mode_frame[0] ? 1 : 0;

	vdec_av1_slice_setup_seg(&uh->seg, &ctrl_fh->segmentation);
	uh->delta_q_lf.delta_q_present = QUANT_FLAG(&ctrl_fh->quantization, DELTA_Q_PRESENT);
	uh->delta_q_lf.delta_q_res = 1 << ctrl_fh->quantization.delta_q_res;
	uh->delta_q_lf.delta_lf_present =
		BIT_FLAG(&ctrl_fh->loop_filter, V4L2_AV1_LOOP_FILTER_FLAG_DELTA_LF_PRESENT);
	uh->delta_q_lf.delta_lf_res = ctrl_fh->loop_filter.delta_lf_res;
	uh->delta_q_lf.delta_lf_multi =
		BIT_FLAG(&ctrl_fh->loop_filter, V4L2_AV1_LOOP_FILTER_FLAG_DELTA_LF_MULTI);
	vdec_av1_slice_setup_quant(&uh->quant, &ctrl_fh->quantization);

	uh->coded_loss_less = 1;
	for (i = 0; i < V4L2_AV1_MAX_SEGMENTS; i++) {
		uh->quant.qindex[i] = vdec_av1_slice_get_qindex(uh, i);
		uh->loss_less_array[i] =
			(uh->quant.qindex[i] == 0 && uh->quant.delta_qydc == 0 &&
			uh->quant.delta_quac == 0 && uh->quant.delta_qudc == 0 &&
			uh->quant.delta_qvac == 0 && uh->quant.delta_qvdc == 0);

		if (!uh->loss_less_array[i])
			uh->coded_loss_less = 0;
	}

	vdec_av1_slice_setup_lr(&uh->lr, &ctrl_fh->loop_restoration);
	uh->superres_denom = ctrl_fh->superres_denom;
	vdec_av1_slice_setup_lf(&uh->loop_filter, &ctrl_fh->loop_filter);
	vdec_av1_slice_setup_cdef(&uh->cdef, &ctrl_fh->cdef);
	vdec_av1_slice_setup_tile(frame, &ctrl_fh->tile_info);
}

static int vdec_av1_slice_setup_tile_group(struct vdec_av1_slice_instance *instance,
					   struct vdec_av1_slice_vsi *vsi)
{
	struct v4l2_ctrl_av1_tile_group_entry *ctrl_tge;
	struct vdec_av1_slice_tile_group *tile_group = &instance->tile_group;
	struct vdec_av1_slice_uncompressed_header *uh = &vsi->frame.uh;
	struct vdec_av1_slice_tile *tile = &uh->tile;
	struct v4l2_ctrl *ctrl;
	u32 tge_size;
	int i;

	ctrl = v4l2_ctrl_find(&instance->ctx->ctrl_hdl, V4L2_CID_STATELESS_AV1_TILE_GROUP_ENTRY);
	if (!ctrl)
		return -EINVAL;

	tge_size = ctrl->elems;
	ctrl_tge = (struct v4l2_ctrl_av1_tile_group_entry *)ctrl->p_cur.p;

	tile_group->num_tiles = tile->tile_cols * tile->tile_rows;

	if (tile_group->num_tiles != tge_size ||
	    tile_group->num_tiles > V4L2_AV1_MAX_TILE_COUNT) {
		mtk_vdec_err(instance->ctx, "invalid tge_size %d, tile_num:%d\n",
			     tge_size, tile_group->num_tiles);
		return -EINVAL;
	}

	for (i = 0; i < tge_size; i++) {
		if (i != ctrl_tge[i].tile_row * vsi->frame.uh.tile.tile_cols +
		    ctrl_tge[i].tile_col) {
			mtk_vdec_err(instance->ctx, "invalid tge info %d, %d %d %d\n",
				     i, ctrl_tge[i].tile_row, ctrl_tge[i].tile_col,
				     vsi->frame.uh.tile.tile_rows);
			return -EINVAL;
		}
		tile_group->tile_size[i] = ctrl_tge[i].tile_size;
		tile_group->tile_start_offset[i] = ctrl_tge[i].tile_offset;
	}

	return 0;
}

static inline void vdec_av1_slice_setup_state(struct vdec_av1_slice_vsi *vsi)
{
	memset(&vsi->state, 0, sizeof(vsi->state));
}

static void vdec_av1_slice_setup_scale_factors(struct vdec_av1_slice_frame_refs *frame_ref,
					       struct vdec_av1_slice_frame_info *ref_frame_info,
					       struct vdec_av1_slice_uncompressed_header *uh)
{
	struct vdec_av1_slice_scale_factors *scale_factors = &frame_ref->scale_factors;
	u32 ref_upscaled_width = ref_frame_info->upscaled_width;
	u32 ref_frame_height = ref_frame_info->frame_height;
	u32 frame_width = uh->frame_width;
	u32 frame_height = uh->frame_height;

	if (!vdec_av1_slice_need_scale(ref_upscaled_width, ref_frame_height,
				       frame_width, frame_height)) {
		scale_factors->x_scale = -1;
		scale_factors->y_scale = -1;
		scale_factors->is_scaled = 0;
		return;
	}

	scale_factors->x_scale =
		((ref_upscaled_width << AV1_REF_SCALE_SHIFT) + (frame_width >> 1)) / frame_width;
	scale_factors->y_scale =
		((ref_frame_height << AV1_REF_SCALE_SHIFT) + (frame_height >> 1)) / frame_height;
	scale_factors->is_scaled =
		(scale_factors->x_scale != AV1_REF_INVALID_SCALE) &&
		(scale_factors->y_scale != AV1_REF_INVALID_SCALE) &&
		(scale_factors->x_scale != AV1_REF_NO_SCALE ||
		 scale_factors->y_scale != AV1_REF_NO_SCALE);
	scale_factors->x_step =
		AV1_DIV_ROUND_UP_POW2(scale_factors->x_scale,
				      AV1_REF_SCALE_SHIFT - AV1_SCALE_SUBPEL_BITS);
	scale_factors->y_step =
		AV1_DIV_ROUND_UP_POW2(scale_factors->y_scale,
				      AV1_REF_SCALE_SHIFT - AV1_SCALE_SUBPEL_BITS);
}

static unsigned char vdec_av1_slice_get_sign_bias(int a,
						  int b,
						  u8 enable_order_hint,
						  u8 order_hint_bits)
{
	int diff = 0;
	int m = 0;
	unsigned char result = 0;

	if (!enable_order_hint)
		return 0;

	diff = a - b;
	m = 1 << (order_hint_bits - 1);
	diff = (diff & (m - 1)) - (diff & m);

	if (diff > 0)
		result = 1;

	return result;
}

static void vdec_av1_slice_setup_ref(struct vdec_av1_slice_pfc *pfc,
				     struct v4l2_ctrl_av1_frame *ctrl_fh)
{
	struct vdec_av1_slice_vsi *vsi = &pfc->vsi;
	struct vdec_av1_slice_frame *frame = &vsi->frame;
	struct vdec_av1_slice_slot *slots = &vsi->slots;
	struct vdec_av1_slice_uncompressed_header *uh = &frame->uh;
	struct vdec_av1_slice_seq_header *seq = &frame->seq;
	struct vdec_av1_slice_frame_info *cur_frame_info =
		&slots->frame_info[vsi->slot_id];
	struct vdec_av1_slice_frame_info *frame_info;
	int i, slot_id;

	if (uh->frame_is_intra)
		return;

	for (i = 0; i < V4L2_AV1_REFS_PER_FRAME; i++) {
		int ref_idx = ctrl_fh->ref_frame_idx[i];

		pfc->ref_idx[i] = ctrl_fh->reference_frame_ts[ref_idx];
		slot_id = frame->ref_frame_map[ref_idx];
		frame_info = &slots->frame_info[slot_id];
		if (slot_id == AV1_INVALID_IDX) {
			pr_err(MTK_DBG_V4L2_STR "cannot match reference[%d] 0x%llx\n", i,
			       ctrl_fh->reference_frame_ts[ref_idx]);
			frame->order_hints[i] = 0;
			frame->ref_frame_valid[i] = 0;
			continue;
		}

		frame->frame_refs[i].ref_fb_idx = slot_id;
		vdec_av1_slice_setup_scale_factors(&frame->frame_refs[i],
						   frame_info, uh);
		if (!seq->enable_order_hint)
			frame->ref_frame_sign_bias[i + 1] = 0;
		else
			frame->ref_frame_sign_bias[i + 1] =
				vdec_av1_slice_get_sign_bias(frame_info->order_hint,
							     uh->order_hint,
							     seq->enable_order_hint,
							     seq->order_hint_bits);

		frame->order_hints[i] = ctrl_fh->order_hints[i + 1];
		cur_frame_info->order_hints[i] = frame->order_hints[i];
		frame->ref_frame_valid[i] = 1;
	}
}

static void vdec_av1_slice_get_previous(struct vdec_av1_slice_vsi *vsi)
{
	struct vdec_av1_slice_frame *frame = &vsi->frame;

	if (frame->uh.primary_ref_frame == AV1_PRIMARY_REF_NONE)
		frame->prev_fb_idx = AV1_INVALID_IDX;
	else
		frame->prev_fb_idx = frame->frame_refs[frame->uh.primary_ref_frame].ref_fb_idx;
}

static inline void vdec_av1_slice_setup_operating_mode(struct vdec_av1_slice_instance *instance,
						       struct vdec_av1_slice_frame *frame)
{
	frame->large_scale_tile = 0;
}

static int vdec_av1_slice_setup_pfc(struct vdec_av1_slice_instance *instance,
				    struct vdec_av1_slice_pfc *pfc)
{
	struct v4l2_ctrl_av1_frame *ctrl_fh;
	struct v4l2_ctrl_av1_sequence *ctrl_seq;
	struct vdec_av1_slice_vsi *vsi = &pfc->vsi;
	int ret = 0;

	/* frame header */
	ctrl_fh = (struct v4l2_ctrl_av1_frame *)
		  vdec_av1_get_ctrl_ptr(instance->ctx,
					V4L2_CID_STATELESS_AV1_FRAME);
	if (IS_ERR(ctrl_fh))
		return PTR_ERR(ctrl_fh);

	ctrl_seq = (struct v4l2_ctrl_av1_sequence *)
		   vdec_av1_get_ctrl_ptr(instance->ctx,
					 V4L2_CID_STATELESS_AV1_SEQUENCE);
	if (IS_ERR(ctrl_seq))
		return PTR_ERR(ctrl_seq);

	/* setup vsi information */
	vdec_av1_slice_setup_seq(&vsi->frame.seq, ctrl_seq);
	vdec_av1_slice_setup_uh(instance, &vsi->frame, ctrl_fh);
	vdec_av1_slice_setup_operating_mode(instance, &vsi->frame);

	vdec_av1_slice_setup_state(vsi);
	vdec_av1_slice_setup_slot(instance, vsi, ctrl_fh);
	vdec_av1_slice_setup_ref(pfc, ctrl_fh);
	vdec_av1_slice_get_previous(vsi);

	pfc->seq = instance->seq;
	instance->seq++;

	return ret;
}

static void vdec_av1_slice_setup_lat_buffer(struct vdec_av1_slice_instance *instance,
					    struct vdec_av1_slice_vsi *vsi,
					    struct mtk_vcodec_mem *bs,
					    struct vdec_lat_buf *lat_buf)
{
	struct vdec_av1_slice_work_buffer *work_buffer;
	int i;

	vsi->bs.dma_addr = bs->dma_addr;
	vsi->bs.size = bs->size;

	vsi->ube.dma_addr = lat_buf->ctx->msg_queue.wdma_addr.dma_addr;
	vsi->ube.size = lat_buf->ctx->msg_queue.wdma_addr.size;
	vsi->trans.dma_addr = lat_buf->ctx->msg_queue.wdma_wptr_addr;
	/* used to store trans end */
	vsi->trans.dma_addr_end = lat_buf->ctx->msg_queue.wdma_rptr_addr;
	vsi->err_map.dma_addr = lat_buf->wdma_err_addr.dma_addr;
	vsi->err_map.size = lat_buf->wdma_err_addr.size;
	vsi->rd_mv.dma_addr = lat_buf->rd_mv_addr.dma_addr;
	vsi->rd_mv.size = lat_buf->rd_mv_addr.size;

	vsi->row_info.buf = 0;
	vsi->row_info.size = 0;

	work_buffer = vsi->work_buffer;

	for (i = 0; i < AV1_MAX_FRAME_BUF_COUNT; i++) {
		work_buffer[i].mv_addr.buf = instance->mv[i].dma_addr;
		work_buffer[i].mv_addr.size = instance->mv[i].size;
		work_buffer[i].segid_addr.buf = instance->seg[i].dma_addr;
		work_buffer[i].segid_addr.size = instance->seg[i].size;
		work_buffer[i].cdf_addr.buf = instance->cdf[i].dma_addr;
		work_buffer[i].cdf_addr.size = instance->cdf[i].size;
	}

	vsi->cdf_tmp.buf = instance->cdf_temp.dma_addr;
	vsi->cdf_tmp.size = instance->cdf_temp.size;

	vsi->tile.buf = instance->tile.dma_addr;
	vsi->tile.size = instance->tile.size;
	memcpy(lat_buf->tile_addr.va, instance->tile.va, 64 * instance->tile_group.num_tiles);

	vsi->cdf_table.buf = instance->cdf_table.dma_addr;
	vsi->cdf_table.size = instance->cdf_table.size;
	vsi->iq_table.buf = instance->iq_table.dma_addr;
	vsi->iq_table.size = instance->iq_table.size;
}

static void vdec_av1_slice_setup_seg_buffer(struct vdec_av1_slice_instance *instance,
					    struct vdec_av1_slice_vsi *vsi)
{
	struct vdec_av1_slice_uncompressed_header *uh = &vsi->frame.uh;
	struct mtk_vcodec_mem *buf;

	/* reset segment buffer */
	if (uh->primary_ref_frame == AV1_PRIMARY_REF_NONE || !uh->seg.segmentation_enabled) {
		mtk_vdec_debug(instance->ctx, "reset seg %d\n", vsi->slot_id);
		if (vsi->slot_id != AV1_INVALID_IDX) {
			buf = &instance->seg[vsi->slot_id];
			memset(buf->va, 0, buf->size);
		}
	}
}

static void vdec_av1_slice_setup_tile_buffer(struct vdec_av1_slice_instance *instance,
					     struct vdec_av1_slice_vsi *vsi,
					     struct mtk_vcodec_mem *bs)
{
	struct vdec_av1_slice_tile_group *tile_group = &instance->tile_group;
	struct vdec_av1_slice_uncompressed_header *uh = &vsi->frame.uh;
	struct vdec_av1_slice_tile *tile = &uh->tile;
	u32 tile_num, tile_row, tile_col;
	u32 allow_update_cdf = 0;
	u32 sb_boundary_x_m1 = 0, sb_boundary_y_m1 = 0;
	int tile_info_base;
	u64 tile_buf_pa;
	u32 *tile_info_buf = instance->tile.va;
	u64 pa = (u64)bs->dma_addr;

	if (uh->disable_cdf_update == 0)
		allow_update_cdf = 1;

	for (tile_num = 0; tile_num < tile_group->num_tiles; tile_num++) {
		/* each uint32 takes place of 4 bytes */
		tile_info_base = (AV1_TILE_BUF_SIZE * tile_num) >> 2;
		tile_row = tile_num / tile->tile_cols;
		tile_col = tile_num % tile->tile_cols;
		tile_info_buf[tile_info_base + 0] = (tile_group->tile_size[tile_num] << 3);
		tile_buf_pa = pa + tile_group->tile_start_offset[tile_num];

		/* save av1 tile high 4bits(bit 32-35) address in lower 4 bits position
		 * and clear original for hw requirement.
		 */
		tile_info_buf[tile_info_base + 1] = (tile_buf_pa & 0xFFFFFFF0ull) |
			((tile_buf_pa & 0xF00000000ull) >> 32);
		tile_info_buf[tile_info_base + 2] = (tile_buf_pa & 0xFull) << 3;

		sb_boundary_x_m1 =
			(tile->mi_col_starts[tile_col + 1] - tile->mi_col_starts[tile_col] - 1) &
			0x3f;
		sb_boundary_y_m1 =
			(tile->mi_row_starts[tile_row + 1] - tile->mi_row_starts[tile_row] - 1) &
			0x1ff;

		tile_info_buf[tile_info_base + 3] = (sb_boundary_y_m1 << 7) | sb_boundary_x_m1;
		tile_info_buf[tile_info_base + 4] = ((allow_update_cdf << 18) | (1 << 16));

		if (tile_num == tile->context_update_tile_id &&
		    uh->disable_frame_end_update_cdf == 0)
			tile_info_buf[tile_info_base + 4] |= (1 << 17);

		mtk_vdec_debug(instance->ctx, "// tile buf %d pos(%dx%d) offset 0x%x\n",
			       tile_num, tile_row, tile_col, tile_info_base);
		mtk_vdec_debug(instance->ctx, "// %08x %08x %08x %08x\n",
			       tile_info_buf[tile_info_base + 0],
			       tile_info_buf[tile_info_base + 1],
			       tile_info_buf[tile_info_base + 2],
			       tile_info_buf[tile_info_base + 3]);
		mtk_vdec_debug(instance->ctx, "// %08x %08x %08x %08x\n",
			       tile_info_buf[tile_info_base + 4],
			       tile_info_buf[tile_info_base + 5],
			       tile_info_buf[tile_info_base + 6],
			       tile_info_buf[tile_info_base + 7]);
	}
}

static int vdec_av1_slice_setup_lat(struct vdec_av1_slice_instance *instance,
				    struct mtk_vcodec_mem *bs,
				    struct vdec_lat_buf *lat_buf,
				    struct vdec_av1_slice_pfc *pfc)
{
	struct vdec_av1_slice_vsi *vsi = &pfc->vsi;
	int ret;

	ret = vdec_av1_slice_setup_lat_from_src_buf(instance, vsi, lat_buf);
	if (ret)
		return ret;

	ret = vdec_av1_slice_setup_pfc(instance, pfc);
	if (ret)
		return ret;

	ret = vdec_av1_slice_setup_tile_group(instance, vsi);
	if (ret)
		return ret;

	ret = vdec_av1_slice_alloc_working_buffer(instance, vsi);
	if (ret)
		return ret;

	vdec_av1_slice_setup_seg_buffer(instance, vsi);
	vdec_av1_slice_setup_tile_buffer(instance, vsi, bs);
	vdec_av1_slice_setup_lat_buffer(instance, vsi, bs, lat_buf);

	return 0;
}

static int vdec_av1_slice_update_lat(struct vdec_av1_slice_instance *instance,
				     struct vdec_lat_buf *lat_buf,
				     struct vdec_av1_slice_pfc *pfc)
{
	struct vdec_av1_slice_vsi *vsi;

	vsi = &pfc->vsi;
	mtk_vdec_debug(instance->ctx, "frame %u LAT CRC 0x%08x, output size is %d\n",
		       pfc->seq, vsi->state.crc[0], vsi->state.out_size);

	/* buffer full, need to re-decode */
	if (vsi->state.full) {
		/* buffer not enough */
		if (vsi->trans.dma_addr_end - vsi->trans.dma_addr == vsi->ube.size)
			return -ENOMEM;
		return -EAGAIN;
	}

	instance->width = vsi->frame.uh.upscaled_width;
	instance->height = vsi->frame.uh.frame_height;
	instance->frame_type = vsi->frame.uh.frame_type;

	return 0;
}

static int vdec_av1_slice_setup_core_to_dst_buf(struct vdec_av1_slice_instance *instance,
						struct vdec_lat_buf *lat_buf)
{
	struct vb2_v4l2_buffer *dst;

	dst = v4l2_m2m_next_dst_buf(instance->ctx->m2m_ctx);
	if (!dst)
		return -EINVAL;

	v4l2_m2m_buf_copy_metadata(&lat_buf->ts_info, dst, true);

	return 0;
}

static int vdec_av1_slice_setup_core_buffer(struct vdec_av1_slice_instance *instance,
					    struct vdec_av1_slice_pfc *pfc,
					    struct vdec_av1_slice_vsi *vsi,
					    struct vdec_fb *fb,
					    struct vdec_lat_buf *lat_buf)
{
	struct vb2_buffer *vb;
	struct vb2_queue *vq;
	int w, h, plane, size;
	int i;

	plane = instance->ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes;
	w = vsi->frame.uh.upscaled_width;
	h = vsi->frame.uh.frame_height;
	size = ALIGN(w, VCODEC_DEC_ALIGNED_64) * ALIGN(h, VCODEC_DEC_ALIGNED_64);

	/* frame buffer */
	vsi->fb.y.dma_addr = fb->base_y.dma_addr;
	if (plane == 1)
		vsi->fb.c.dma_addr = fb->base_y.dma_addr + size;
	else
		vsi->fb.c.dma_addr = fb->base_c.dma_addr;

	/* reference buffers */
	vq = v4l2_m2m_get_vq(instance->ctx->m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (!vq)
		return -EINVAL;

	/* get current output buffer */
	vb = &v4l2_m2m_next_dst_buf(instance->ctx->m2m_ctx)->vb2_buf;
	if (!vb)
		return -EINVAL;

	/* get buffer address from vb2buf */
	for (i = 0; i < V4L2_AV1_REFS_PER_FRAME; i++) {
		struct vdec_av1_slice_fb *vref = &vsi->ref[i];

		vb = vb2_find_buffer(vq, pfc->ref_idx[i]);
		if (!vb) {
			memset(vref, 0, sizeof(*vref));
			continue;
		}

		vref->y.dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);
		if (plane == 1)
			vref->c.dma_addr = vref->y.dma_addr + size;
		else
			vref->c.dma_addr = vb2_dma_contig_plane_dma_addr(vb, 1);
	}
	vsi->tile.dma_addr = lat_buf->tile_addr.dma_addr;
	vsi->tile.size = lat_buf->tile_addr.size;

	return 0;
}

static int vdec_av1_slice_setup_core(struct vdec_av1_slice_instance *instance,
				     struct vdec_fb *fb,
				     struct vdec_lat_buf *lat_buf,
				     struct vdec_av1_slice_pfc *pfc)
{
	struct vdec_av1_slice_vsi *vsi = &pfc->vsi;
	int ret;

	ret = vdec_av1_slice_setup_core_to_dst_buf(instance, lat_buf);
	if (ret)
		return ret;

	ret = vdec_av1_slice_setup_core_buffer(instance, pfc, vsi, fb, lat_buf);
	if (ret)
		return ret;

	return 0;
}

static int vdec_av1_slice_update_core(struct vdec_av1_slice_instance *instance,
				      struct vdec_lat_buf *lat_buf,
				      struct vdec_av1_slice_pfc *pfc)
{
	struct vdec_av1_slice_vsi *vsi = instance->core_vsi;

	mtk_vdec_debug(instance->ctx, "frame %u Y_CRC %08x %08x %08x %08x\n",
		       pfc->seq, vsi->state.crc[0], vsi->state.crc[1],
		       vsi->state.crc[2], vsi->state.crc[3]);
	mtk_vdec_debug(instance->ctx, "frame %u C_CRC %08x %08x %08x %08x\n",
		       pfc->seq, vsi->state.crc[8], vsi->state.crc[9],
		       vsi->state.crc[10], vsi->state.crc[11]);

	return 0;
}

static int vdec_av1_slice_init(struct mtk_vcodec_dec_ctx *ctx)
{
	struct vdec_av1_slice_instance *instance;
	struct vdec_av1_slice_init_vsi *vsi;
	int ret;

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return -ENOMEM;

	instance->ctx = ctx;
	instance->vpu.id = SCP_IPI_VDEC_LAT;
	instance->vpu.core_id = SCP_IPI_VDEC_CORE;
	instance->vpu.ctx = ctx;
	instance->vpu.codec_type = ctx->current_codec;

	ret = vpu_dec_init(&instance->vpu);
	if (ret) {
		mtk_vdec_err(ctx, "failed to init vpu dec, ret %d\n", ret);
		goto error_vpu_init;
	}

	/* init vsi and global flags */
	vsi = instance->vpu.vsi;
	if (!vsi) {
		mtk_vdec_err(ctx, "failed to get AV1 vsi\n");
		ret = -EINVAL;
		goto error_vsi;
	}
	instance->init_vsi = vsi;
	instance->core_vsi = mtk_vcodec_fw_map_dm_addr(ctx->dev->fw_handler, (u32)vsi->core_vsi);

	if (!instance->core_vsi) {
		mtk_vdec_err(ctx, "failed to get AV1 core vsi\n");
		ret = -EINVAL;
		goto error_vsi;
	}

	if (vsi->vsi_size != sizeof(struct vdec_av1_slice_vsi))
		mtk_vdec_err(ctx, "remote vsi size 0x%x mismatch! expected: 0x%zx\n",
			     vsi->vsi_size, sizeof(struct vdec_av1_slice_vsi));

	instance->irq_enabled = 1;
	instance->inneracing_mode = IS_VDEC_INNER_RACING(instance->ctx->dev->dec_capability);

	mtk_vdec_debug(ctx, "vsi 0x%p core_vsi 0x%llx 0x%p, inneracing_mode %d\n",
		       vsi, vsi->core_vsi, instance->core_vsi, instance->inneracing_mode);

	ret = vdec_av1_slice_init_cdf_table(instance);
	if (ret)
		goto error_vsi;

	ret = vdec_av1_slice_init_iq_table(instance);
	if (ret)
		goto error_vsi;

	ctx->drv_handle = instance;

	return 0;
error_vsi:
	vpu_dec_deinit(&instance->vpu);
error_vpu_init:
	kfree(instance);

	return ret;
}

static void vdec_av1_slice_deinit(void *h_vdec)
{
	struct vdec_av1_slice_instance *instance = h_vdec;

	if (!instance)
		return;
	mtk_vdec_debug(instance->ctx, "h_vdec 0x%p\n", h_vdec);
	vpu_dec_deinit(&instance->vpu);
	vdec_av1_slice_free_working_buffer(instance);
	vdec_msg_queue_deinit(&instance->ctx->msg_queue, instance->ctx);
	kfree(instance);
}

static int vdec_av1_slice_flush(void *h_vdec, struct mtk_vcodec_mem *bs,
				struct vdec_fb *fb, bool *res_chg)
{
	struct vdec_av1_slice_instance *instance = h_vdec;
	int i;

	mtk_vdec_debug(instance->ctx, "flush ...\n");

	vdec_msg_queue_wait_lat_buf_full(&instance->ctx->msg_queue);

	for (i = 0; i < AV1_MAX_FRAME_BUF_COUNT; i++)
		vdec_av1_slice_clear_fb(&instance->slots.frame_info[i]);

	return vpu_dec_reset(&instance->vpu);
}

static void vdec_av1_slice_get_pic_info(struct vdec_av1_slice_instance *instance)
{
	struct mtk_vcodec_dec_ctx *ctx = instance->ctx;
	u32 data[3];

	mtk_vdec_debug(ctx, "w %u h %u\n", ctx->picinfo.pic_w, ctx->picinfo.pic_h);

	data[0] = ctx->picinfo.pic_w;
	data[1] = ctx->picinfo.pic_h;
	data[2] = ctx->capture_fourcc;
	vpu_dec_get_param(&instance->vpu, data, 3, GET_PARAM_PIC_INFO);

	ctx->picinfo.buf_w = ALIGN(ctx->picinfo.pic_w, VCODEC_DEC_ALIGNED_64);
	ctx->picinfo.buf_h = ALIGN(ctx->picinfo.pic_h, VCODEC_DEC_ALIGNED_64);
	ctx->picinfo.fb_sz[0] = instance->vpu.fb_sz[0];
	ctx->picinfo.fb_sz[1] = instance->vpu.fb_sz[1];
}

static inline void vdec_av1_slice_get_dpb_size(struct vdec_av1_slice_instance *instance,
					       u32 *dpb_sz)
{
	/* refer av1 specification */
	*dpb_sz = V4L2_AV1_TOTAL_REFS_PER_FRAME + 1;
}

static void vdec_av1_slice_get_crop_info(struct vdec_av1_slice_instance *instance,
					 struct v4l2_rect *cr)
{
	struct mtk_vcodec_dec_ctx *ctx = instance->ctx;

	cr->left = 0;
	cr->top = 0;
	cr->width = ctx->picinfo.pic_w;
	cr->height = ctx->picinfo.pic_h;

	mtk_vdec_debug(ctx, "l=%d, t=%d, w=%d, h=%d\n",
		       cr->left, cr->top, cr->width, cr->height);
}

static int vdec_av1_slice_get_param(void *h_vdec, enum vdec_get_param_type type, void *out)
{
	struct vdec_av1_slice_instance *instance = h_vdec;

	switch (type) {
	case GET_PARAM_PIC_INFO:
		vdec_av1_slice_get_pic_info(instance);
		break;
	case GET_PARAM_DPB_SIZE:
		vdec_av1_slice_get_dpb_size(instance, out);
		break;
	case GET_PARAM_CROP_INFO:
		vdec_av1_slice_get_crop_info(instance, out);
		break;
	default:
		mtk_vdec_err(instance->ctx, "invalid get parameter type=%d\n", type);
		return -EINVAL;
	}

	return 0;
}

static int vdec_av1_slice_lat_decode(void *h_vdec, struct mtk_vcodec_mem *bs,
				     struct vdec_fb *fb, bool *res_chg)
{
	struct vdec_av1_slice_instance *instance = h_vdec;
	struct vdec_lat_buf *lat_buf;
	struct vdec_av1_slice_pfc *pfc;
	struct vdec_av1_slice_vsi *vsi;
	struct mtk_vcodec_dec_ctx *ctx;
	int ret;

	if (!instance || !instance->ctx)
		return -EINVAL;

	ctx = instance->ctx;
	/* init msgQ for the first time */
	if (vdec_msg_queue_init(&ctx->msg_queue, ctx,
				vdec_av1_slice_core_decode, sizeof(*pfc))) {
		mtk_vdec_err(ctx, "failed to init AV1 msg queue\n");
		return -ENOMEM;
	}

	/* bs NULL means flush decoder */
	if (!bs)
		return vdec_av1_slice_flush(h_vdec, bs, fb, res_chg);

	lat_buf = vdec_msg_queue_dqbuf(&ctx->msg_queue.lat_ctx);
	if (!lat_buf) {
		mtk_vdec_err(ctx, "failed to get AV1 lat buf\n");
		return -EAGAIN;
	}
	pfc = (struct vdec_av1_slice_pfc *)lat_buf->private_data;
	if (!pfc) {
		ret = -EINVAL;
		goto err_free_fb_out;
	}
	vsi = &pfc->vsi;

	ret = vdec_av1_slice_setup_lat(instance, bs, lat_buf, pfc);
	if (ret) {
		mtk_vdec_err(ctx, "failed to setup AV1 lat ret %d\n", ret);
		goto err_free_fb_out;
	}

	vdec_av1_slice_vsi_to_remote(vsi, instance->vsi);
	ret = vpu_dec_start(&instance->vpu, NULL, 0);
	if (ret) {
		mtk_vdec_err(ctx, "failed to dec AV1 ret %d\n", ret);
		goto err_free_fb_out;
	}
	if (instance->inneracing_mode)
		vdec_msg_queue_qbuf(&ctx->msg_queue.core_ctx, lat_buf);

	if (instance->irq_enabled) {
		ret = mtk_vcodec_wait_for_done_ctx(ctx, MTK_INST_IRQ_RECEIVED,
						   WAIT_INTR_TIMEOUT_MS,
						   MTK_VDEC_LAT0);
		/* update remote vsi if decode timeout */
		if (ret) {
			mtk_vdec_err(ctx, "AV1 Frame %d decode timeout %d\n", pfc->seq, ret);
			WRITE_ONCE(instance->vsi->state.timeout, 1);
		}
		vpu_dec_end(&instance->vpu);
	}

	vdec_av1_slice_vsi_from_remote(vsi, instance->vsi);
	ret = vdec_av1_slice_update_lat(instance, lat_buf, pfc);

	/* LAT trans full, re-decode */
	if (ret == -EAGAIN) {
		mtk_vdec_err(ctx, "AV1 Frame %d trans full\n", pfc->seq);
		if (!instance->inneracing_mode)
			vdec_msg_queue_qbuf(&ctx->msg_queue.lat_ctx, lat_buf);
		return 0;
	}

	/* LAT trans full, no more UBE or decode timeout */
	if (ret == -ENOMEM || vsi->state.timeout) {
		mtk_vdec_err(ctx, "AV1 Frame %d insufficient buffer or timeout\n", pfc->seq);
		if (!instance->inneracing_mode)
			vdec_msg_queue_qbuf(&ctx->msg_queue.lat_ctx, lat_buf);
		return -EBUSY;
	}
	vsi->trans.dma_addr_end += ctx->msg_queue.wdma_addr.dma_addr;
	mtk_vdec_debug(ctx, "lat dma 1 0x%pad 0x%pad\n",
		       &pfc->vsi.trans.dma_addr, &pfc->vsi.trans.dma_addr_end);

	vdec_msg_queue_update_ube_wptr(&ctx->msg_queue, vsi->trans.dma_addr_end);

	if (!instance->inneracing_mode)
		vdec_msg_queue_qbuf(&ctx->msg_queue.core_ctx, lat_buf);
	memcpy(&instance->slots, &vsi->slots, sizeof(instance->slots));

	return 0;

err_free_fb_out:
	vdec_msg_queue_qbuf(&ctx->msg_queue.lat_ctx, lat_buf);

	if (pfc)
		mtk_vdec_err(ctx, "slice dec number: %d err: %d", pfc->seq, ret);

	return ret;
}

static int vdec_av1_slice_core_decode(struct vdec_lat_buf *lat_buf)
{
	struct vdec_av1_slice_instance *instance;
	struct vdec_av1_slice_pfc *pfc;
	struct mtk_vcodec_dec_ctx *ctx = NULL;
	struct vdec_fb *fb = NULL;
	int ret = -EINVAL;

	if (!lat_buf)
		return -EINVAL;

	pfc = lat_buf->private_data;
	ctx = lat_buf->ctx;
	if (!pfc || !ctx)
		return -EINVAL;

	instance = ctx->drv_handle;
	if (!instance)
		goto err;

	fb = ctx->dev->vdec_pdata->get_cap_buffer(ctx);
	if (!fb) {
		ret = -EBUSY;
		goto err;
	}

	ret = vdec_av1_slice_setup_core(instance, fb, lat_buf, pfc);
	if (ret) {
		mtk_vdec_err(ctx, "vdec_av1_slice_setup_core\n");
		goto err;
	}
	vdec_av1_slice_vsi_to_remote(&pfc->vsi, instance->core_vsi);
	ret = vpu_dec_core(&instance->vpu);
	if (ret) {
		mtk_vdec_err(ctx, "vpu_dec_core\n");
		goto err;
	}

	if (instance->irq_enabled) {
		ret = mtk_vcodec_wait_for_done_ctx(ctx, MTK_INST_IRQ_RECEIVED,
						   WAIT_INTR_TIMEOUT_MS,
						   MTK_VDEC_CORE);
		/* update remote vsi if decode timeout */
		if (ret) {
			mtk_vdec_err(ctx, "AV1 frame %d core timeout\n", pfc->seq);
			WRITE_ONCE(instance->vsi->state.timeout, 1);
		}
		vpu_dec_core_end(&instance->vpu);
	}

	ret = vdec_av1_slice_update_core(instance, lat_buf, pfc);
	if (ret) {
		mtk_vdec_err(ctx, "vdec_av1_slice_update_core\n");
		goto err;
	}

	mtk_vdec_debug(ctx, "core dma_addr_end 0x%pad\n",
		       &instance->core_vsi->trans.dma_addr_end);
	vdec_msg_queue_update_ube_rptr(&ctx->msg_queue, instance->core_vsi->trans.dma_addr_end);

	ctx->dev->vdec_pdata->cap_to_disp(ctx, 0, lat_buf->src_buf_req);

	return 0;

err:
	/* always update read pointer */
	vdec_msg_queue_update_ube_rptr(&ctx->msg_queue, pfc->vsi.trans.dma_addr_end);

	if (fb)
		ctx->dev->vdec_pdata->cap_to_disp(ctx, 1, lat_buf->src_buf_req);

	return ret;
}

const struct vdec_common_if vdec_av1_slice_lat_if = {
	.init		= vdec_av1_slice_init,
	.decode		= vdec_av1_slice_lat_decode,
	.get_param	= vdec_av1_slice_get_param,
	.deinit		= vdec_av1_slice_deinit,
};
