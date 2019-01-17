// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <mjourdan@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_hevc.h"
#include "dos_regs.h"
#include "hevc_regs.h"
#include "vdec_helpers.h"
#include "codec_hevc_common.h"

/* HEVC reg mapping */
#define VP9_DEC_STATUS_REG	HEVC_ASSIST_SCRATCH_0
	#define VP9_10B_DECODE_SLICE	5
	#define VP9_HEAD_PARSER_DONE	0xf0
#define VP9_RPM_BUFFER		HEVC_ASSIST_SCRATCH_1
#define VP9_SHORT_TERM_RPS	HEVC_ASSIST_SCRATCH_2
#define VP9_ADAPT_PROB_REG	HEVC_ASSIST_SCRATCH_3
#define VP9_MMU_MAP_BUFFER	HEVC_ASSIST_SCRATCH_4
#define VP9_PPS_BUFFER		HEVC_ASSIST_SCRATCH_5
#define VP9_SAO_UP		HEVC_ASSIST_SCRATCH_6
#define VP9_STREAM_SWAP_BUFFER	HEVC_ASSIST_SCRATCH_7
#define VP9_STREAM_SWAP_BUFFER2 HEVC_ASSIST_SCRATCH_8
#define VP9_PROB_SWAP_BUFFER	HEVC_ASSIST_SCRATCH_9
#define VP9_COUNT_SWAP_BUFFER	HEVC_ASSIST_SCRATCH_A
#define VP9_SEG_MAP_BUFFER	HEVC_ASSIST_SCRATCH_B
#define VP9_SCALELUT		HEVC_ASSIST_SCRATCH_D
#define VP9_WAIT_FLAG		HEVC_ASSIST_SCRATCH_E
#define LMEM_DUMP_ADR		HEVC_ASSIST_SCRATCH_F
#define NAL_SEARCH_CTL		HEVC_ASSIST_SCRATCH_I
#define VP9_DECODE_MODE		HEVC_ASSIST_SCRATCH_J
	#define DECODE_MODE_SINGLE 0
#define DECODE_STOP_POS		HEVC_ASSIST_SCRATCH_K
#define HEVC_DECODE_COUNT	HEVC_ASSIST_SCRATCH_M
#define HEVC_DECODE_SIZE	HEVC_ASSIST_SCRATCH_N

/* VP9 Constants */
#define LCU_SIZE		64
#define MAX_REF_PIC_NUM		24
#define REFS_PER_FRAME		3
#define REF_FRAMES		8
#define MV_MEM_UNIT		0x240

enum FRAME_TYPE {
	KEY_FRAME = 0,
	INTER_FRAME = 1,
	FRAME_TYPES,
};

/* VP9 Workspace layout */
#define MPRED_MV_BUF_SIZE 0x120000

#define IPP_SIZE	0x4000
#define SAO_ABV_SIZE	0x30000
#define SAO_VB_SIZE	0x30000
#define SH_TM_RPS_SIZE	0x800
#define VPS_SIZE	0x800
#define SPS_SIZE	0x800
#define PPS_SIZE	0x2000
#define SAO_UP_SIZE	0x2800
#define SWAP_BUF_SIZE	0x800
#define SWAP_BUF2_SIZE	0x800
#define SCALELUT_SIZE	0x8000
#define DBLK_PARA_SIZE	0x80000
#define DBLK_DATA_SIZE	0x80000
#define SEG_MAP_SIZE	0xd800
#define PROB_SIZE	0x5000
#define COUNT_SIZE	0x3000
#define MMU_VBH_SIZE	0x5000
#define MPRED_ABV_SIZE	0x10000
#define MPRED_MV_SIZE	(MPRED_MV_BUF_SIZE * MAX_REF_PIC_NUM)
#define RPM_BUF_SIZE	0x100
#define LMEM_SIZE	0x800

#define IPP_OFFSET       0x00
#define SAO_ABV_OFFSET   (IPP_OFFSET + IPP_SIZE)
#define SAO_VB_OFFSET    (SAO_ABV_OFFSET + SAO_ABV_SIZE)
#define SH_TM_RPS_OFFSET (SAO_VB_OFFSET + SAO_VB_SIZE)
#define VPS_OFFSET       (SH_TM_RPS_OFFSET + SH_TM_RPS_SIZE)
#define SPS_OFFSET       (VPS_OFFSET + VPS_SIZE)
#define PPS_OFFSET       (SPS_OFFSET + SPS_SIZE)
#define SAO_UP_OFFSET    (PPS_OFFSET + PPS_SIZE)
#define SWAP_BUF_OFFSET  (SAO_UP_OFFSET + SAO_UP_SIZE)
#define SWAP_BUF2_OFFSET (SWAP_BUF_OFFSET + SWAP_BUF_SIZE)
#define SCALELUT_OFFSET  (SWAP_BUF2_OFFSET + SWAP_BUF2_SIZE)
#define DBLK_PARA_OFFSET (SCALELUT_OFFSET + SCALELUT_SIZE)
#define DBLK_DATA_OFFSET (DBLK_PARA_OFFSET + DBLK_PARA_SIZE)
#define SEG_MAP_OFFSET   (DBLK_DATA_OFFSET + DBLK_DATA_SIZE)
#define PROB_OFFSET	(SEG_MAP_OFFSET + SEG_MAP_SIZE)
#define COUNT_OFFSET	(PROB_OFFSET + PROB_SIZE)
#define MMU_VBH_OFFSET   (COUNT_OFFSET + COUNT_SIZE)
#define MPRED_ABV_OFFSET (MMU_VBH_OFFSET + MMU_VBH_SIZE)
#define MPRED_MV_OFFSET  (MPRED_ABV_OFFSET + MPRED_ABV_SIZE)
#define RPM_OFFSET       (MPRED_MV_OFFSET + MPRED_MV_SIZE)
#define LMEM_OFFSET      (RPM_OFFSET + RPM_BUF_SIZE)

#define SIZE_WORKSPACE	ALIGN(LMEM_OFFSET + LMEM_SIZE, 64 * SZ_1K)

#define NONE           -1
#define INTRA_FRAME     0
#define LAST_FRAME      1
#define GOLDEN_FRAME    2
#define ALTREF_FRAME    3
#define MAX_REF_FRAMES  4

/*
 * Defines, declarations, sub-functions for vp9 de-block loop
	filter Thr/Lvl table update
 * - struct segmentation is for loop filter only (removed something)
 * - function "vp9_loop_filter_init" and "vp9_loop_filter_frame_init" will
	be instantiated in C_Entry
 * - vp9_loop_filter_init run once before decoding start
 * - vp9_loop_filter_frame_init run before every frame decoding start
 * - set video format to VP9 is in vp9_loop_filter_init
 */
#define MAX_LOOP_FILTER		63
#define MAX_REF_LF_DELTAS	4
#define MAX_MODE_LF_DELTAS	2
#define SEGMENT_DELTADATA	0
#define SEGMENT_ABSDATA		1
#define MAX_SEGMENTS		8

union rpm_param {
	struct {
		u16 data[RPM_BUF_SIZE];
	} l;
	struct {
		u16 profile;
		u16 show_existing_frame;
		u16 frame_to_show_idx;
		u16 frame_type; /*1 bit*/
		u16 show_frame; /*1 bit*/
		u16 error_resilient_mode; /*1 bit*/
		u16 intra_only; /*1 bit*/
		u16 display_size_present; /*1 bit*/
		u16 reset_frame_context;
		u16 refresh_frame_flags;
		u16 width;
		u16 height;
		u16 display_width;
		u16 display_height;
		u16 ref_info;
		u16 same_frame_size;
		u16 mode_ref_delta_enabled;
		u16 ref_deltas[4];
		u16 mode_deltas[2];
		u16 filter_level;
		u16 sharpness_level;
		u16 bit_depth;
		u16 seg_quant_info[8];
		u16 seg_enabled;
		u16 seg_abs_delta;
		/* bit 15: feature enabled; bit 8, sign; bit[5:0], data */
		u16 seg_lf_info[8];
	} p;
};

enum SEG_LVL_FEATURES {
	SEG_LVL_ALT_Q = 0,	/* Use alternate Quantizer */
	SEG_LVL_ALT_LF = 1,	/* Use alternate loop filter value */
	SEG_LVL_REF_FRAME = 2,	/* Optional Segment reference frame */
	SEG_LVL_SKIP = 3,	/* Optional Segment (0,0) + skip mode */
	SEG_LVL_MAX = 4		/* Number of features supported */
};

struct segmentation {
	uint8_t enabled;
	uint8_t update_map;
	uint8_t update_data;
	uint8_t abs_delta;
	uint8_t temporal_update;
	int16_t feature_data[MAX_SEGMENTS][SEG_LVL_MAX];
	unsigned int feature_mask[MAX_SEGMENTS];
};

struct loop_filter_thresh {
	uint8_t mblim;
	uint8_t lim;
	uint8_t hev_thr;
};

struct loop_filter_info_n {
	struct loop_filter_thresh lfthr[MAX_LOOP_FILTER + 1];
	uint8_t lvl[MAX_SEGMENTS][MAX_REF_FRAMES][MAX_MODE_LF_DELTAS];
};

struct loopfilter {
	int filter_level;

	int sharpness_level;
	int last_sharpness_level;

	uint8_t mode_ref_delta_enabled;
	uint8_t mode_ref_delta_update;

	/*0 = Intra, Last, GF, ARF*/
	signed char ref_deltas[MAX_REF_LF_DELTAS];
	signed char last_ref_deltas[MAX_REF_LF_DELTAS];

	/*0 = ZERO_MV, MV*/
	signed char mode_deltas[MAX_MODE_LF_DELTAS];
	signed char last_mode_deltas[MAX_MODE_LF_DELTAS];
};

struct vp9_frame {
	struct list_head list;
	struct vb2_v4l2_buffer *vbuf;
	int index;
	int intra_only;
	int show;
	int type;
	int done;
};

struct codec_vp9 {
	struct mutex lock;

	/* Buffer for the VP9 Workspace */
	void      *workspace_vaddr;
	dma_addr_t workspace_paddr;

	/* Contains many information parsed from the bitstream */
	union rpm_param rpm_param;

	/* Whether we detected the bitstream as 10-bit */
	int is_10bit;

	/* Coded resolution reported by the hardware */
	u32 width, height;

	/* All ref frames used by the HW at a given time */
	struct list_head ref_frames_list;
	u32 frames_num;

	/* In case of downsampling (decoding with FBC but outputting in NV12M),
	 * we need to allocate additional buffers for FBC.
	 */
	void      *fbc_buffer_vaddr[MAX_REF_PIC_NUM];
	dma_addr_t fbc_buffer_paddr[MAX_REF_PIC_NUM];

	int ref_frame_map[REF_FRAMES];
	int next_ref_frame_map[REF_FRAMES];
	struct vp9_frame* frame_refs[REFS_PER_FRAME];

	u32 lcu_total;

	/* loop filter */
	int default_filt_lvl;
	struct loop_filter_info_n lfi;
	struct loopfilter lf;
	struct segmentation seg_4lf;

	struct vp9_frame *cur_frame;
	struct vp9_frame *prev_frame;
};

static int vp9_clamp(int value, int low, int high)
{
	return value < low ? low : (value > high ? high : value);
}

static int segfeature_active(struct segmentation *seg,
			int segment_id,
			enum SEG_LVL_FEATURES feature_id)
{
	return seg->enabled &&
		(seg->feature_mask[segment_id] & (1 << feature_id));
}

static int get_segdata(struct segmentation *seg, int segment_id,
				enum SEG_LVL_FEATURES feature_id)
{
	return seg->feature_data[segment_id][feature_id];
}

static void vp9_update_sharpness(struct loop_filter_info_n *lfi,
					int sharpness_lvl)
{
	int lvl;
	/*For each possible value for the loop filter fill out limits*/
	for (lvl = 0; lvl <= MAX_LOOP_FILTER; lvl++) {
		/*Set loop filter parameters that control sharpness.*/
		int block_inside_limit = lvl >> ((sharpness_lvl > 0) +
					(sharpness_lvl > 4));

		if (sharpness_lvl > 0) {
			if (block_inside_limit > (9 - sharpness_lvl))
				block_inside_limit = (9 - sharpness_lvl);
		}

		if (block_inside_limit < 1)
			block_inside_limit = 1;

		lfi->lfthr[lvl].lim = (uint8_t)block_inside_limit;
		lfi->lfthr[lvl].mblim = (uint8_t)(2 * (lvl + 2) +
				block_inside_limit);
	}
}

/*instantiate this function once when decode is started*/
static void
vp9_loop_filter_init(struct amvdec_core *core, struct codec_vp9 *vp9)
{
	struct loop_filter_info_n *lfi = &vp9->lfi;
	struct loopfilter *lf = &vp9->lf;
	struct segmentation *seg_4lf = &vp9->seg_4lf;
	int i;

	memset(lfi, 0, sizeof(struct loop_filter_info_n));
	memset(lf, 0, sizeof(struct loopfilter));
	memset(seg_4lf, 0, sizeof(struct segmentation));
	lf->sharpness_level = 0;
	vp9_update_sharpness(lfi, lf->sharpness_level);
	lf->last_sharpness_level = lf->sharpness_level;

	for (i = 0; i < 32; i++) {
		unsigned int thr;
		thr = ((lfi->lfthr[i * 2 + 1].lim & 0x3f)<<8) |
			(lfi->lfthr[i * 2 + 1].mblim & 0xff);
		thr = (thr<<16) | ((lfi->lfthr[i*2].lim & 0x3f)<<8) |
			(lfi->lfthr[i * 2].mblim & 0xff);
		amvdec_write_dos(core, HEVC_DBLK_CFG9, thr);
	}

	amvdec_write_dos(core, HEVC_DBLK_CFGB, 0x40400001);
}

static void
vp9_loop_filter_frame_init(struct amvdec_core *core, struct segmentation *seg,
			   struct loop_filter_info_n *lfi,
			   struct loopfilter *lf, int default_filt_lvl)
{
	int i;
	int seg_id;
	/*n_shift is the multiplier for lf_deltas
	the multiplier is 1 for when filter_lvl is between 0 and 31;
	2 when filter_lvl is between 32 and 63*/
	const int scale = 1 << (default_filt_lvl >> 5);

	/*update limits if sharpness has changed*/
	if (lf->last_sharpness_level != lf->sharpness_level) {
		vp9_update_sharpness(lfi, lf->sharpness_level);
		lf->last_sharpness_level = lf->sharpness_level;

		/*Write to register*/
		for (i = 0; i < 32; i++) {
			unsigned int thr;
			thr = ((lfi->lfthr[i * 2 + 1].lim & 0x3f) << 8)
				| (lfi->lfthr[i * 2 + 1].mblim & 0xff);
			thr = (thr << 16) | ((lfi->lfthr[i * 2].lim & 0x3f) << 8)
				| (lfi->lfthr[i * 2].mblim & 0xff);
			amvdec_write_dos(core, HEVC_DBLK_CFG9, thr);
		}
	}

	for (seg_id = 0; seg_id < MAX_SEGMENTS; seg_id++) {/*MAX_SEGMENTS = 8*/
		int lvl_seg = default_filt_lvl;
		if (segfeature_active(seg, seg_id, SEG_LVL_ALT_LF)) {
			const int data = get_segdata(seg, seg_id,
						SEG_LVL_ALT_LF);
			lvl_seg = vp9_clamp(seg->abs_delta == SEGMENT_ABSDATA ?
				data : default_filt_lvl + data,
				0, MAX_LOOP_FILTER);
		}

		if (!lf->mode_ref_delta_enabled) {
			/*we could get rid of this if we assume that deltas are set to
			zero when not in use; encoder always uses deltas*/
			memset(lfi->lvl[seg_id], lvl_seg, sizeof(lfi->lvl[seg_id]));
		} else {
			int ref, mode;
			const int intra_lvl = lvl_seg +	lf->ref_deltas[INTRA_FRAME]
						* scale;
			lfi->lvl[seg_id][INTRA_FRAME][0] =
					vp9_clamp(intra_lvl, 0, MAX_LOOP_FILTER);

			for (ref = LAST_FRAME; ref < MAX_REF_FRAMES; ++ref) {
				/* LAST_FRAME = 1, MAX_REF_FRAMES = 4*/
				for (mode = 0; mode < MAX_MODE_LF_DELTAS; ++mode) {
					/*MAX_MODE_LF_DELTAS = 2*/
					const int inter_lvl =
						lvl_seg + lf->ref_deltas[ref] * scale
						+ lf->mode_deltas[mode] * scale;
					lfi->lvl[seg_id][ref][mode] =
						vp9_clamp(inter_lvl, 0,
						MAX_LOOP_FILTER);
				}
			}
		}
	}

	for (i = 0; i < 16; i++) {
		unsigned int level;
		level = ((lfi->lvl[i >> 1][3][i & 1] & 0x3f) << 24) |
			((lfi->lvl[i >> 1][2][i & 1] & 0x3f) << 16) |
			((lfi->lvl[i >> 1][1][i & 1] & 0x3f) << 8) |
			(lfi->lvl[i >> 1][0][i & 1] & 0x3f);
		if (!default_filt_lvl)
			level = 0;
		amvdec_write_dos(core, HEVC_DBLK_CFGA, level);
	}
}

static void codec_vp9_flush_output(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;
	struct vp9_frame *tmp, *n;

	list_for_each_entry_safe(tmp, n, &vp9->ref_frames_list, list) {
		if (!tmp->done) {
			if (tmp->show)
				amvdec_dst_buf_done(sess, tmp->vbuf, V4L2_FIELD_NONE);
			else
				v4l2_m2m_buf_queue(sess->m2m_ctx, tmp->vbuf);
			vp9->frames_num--;
		}

		list_del(&tmp->list);
		kfree(tmp);
	}
}

static u32 codec_vp9_num_pending_bufs(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;

	if (!vp9)
		return 0;

	return vp9->frames_num;
}

static int
codec_vp9_setup_workspace(struct amvdec_core *core, struct codec_vp9 *vp9)
{
	dma_addr_t wkaddr;

	/* Allocate some memory for the VP9 decoder's state */
	vp9->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE,
						   &wkaddr, GFP_KERNEL);
	if (!vp9->workspace_vaddr) {
		dev_err(core->dev, "Failed to allocate VP9 Workspace\n");
		return -ENOMEM;
	}

	vp9->workspace_paddr = wkaddr;

	amvdec_write_dos(core, HEVCD_IPP_LINEBUFF_BASE, wkaddr + IPP_OFFSET);
	amvdec_write_dos(core, VP9_RPM_BUFFER, wkaddr + RPM_OFFSET);
	amvdec_write_dos(core, VP9_SHORT_TERM_RPS, wkaddr + SH_TM_RPS_OFFSET);
	amvdec_write_dos(core, VP9_PPS_BUFFER, wkaddr + PPS_OFFSET);
	amvdec_write_dos(core, VP9_SAO_UP, wkaddr + SAO_UP_OFFSET);

	/* No MMU */
	amvdec_write_dos(core, VP9_STREAM_SWAP_BUFFER,
			 wkaddr + SWAP_BUF_OFFSET);
	amvdec_write_dos(core, VP9_STREAM_SWAP_BUFFER2,
			 wkaddr + SWAP_BUF2_OFFSET);
	amvdec_write_dos(core, VP9_SCALELUT, wkaddr + SCALELUT_OFFSET);
	amvdec_write_dos(core, HEVC_DBLK_CFG4, wkaddr + DBLK_PARA_OFFSET);
	amvdec_write_dos(core, HEVC_DBLK_CFG5, wkaddr + DBLK_DATA_OFFSET);
	amvdec_write_dos(core, VP9_SEG_MAP_BUFFER, wkaddr + SEG_MAP_OFFSET);
	amvdec_write_dos(core, VP9_PROB_SWAP_BUFFER, wkaddr + PROB_OFFSET);
	amvdec_write_dos(core, VP9_COUNT_SWAP_BUFFER, wkaddr + COUNT_OFFSET);

	return 0;
}

static int codec_vp9_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9;
	u32 val;
	int i;
	int ret;

	vp9 = kzalloc(sizeof(*vp9), GFP_KERNEL);
	if (!vp9)
		return -ENOMEM;

	ret = codec_vp9_setup_workspace(core, vp9);
	if (ret)
		goto free_vp9;

	amvdec_write_dos_bits(core, HEVC_STREAM_CONTROL, BIT(0));

	val = amvdec_read_dos(core, HEVC_PARSER_INT_CONTROL) & 0x7fffffff;
	val |= (3 << 29) | BIT(24) | BIT(22) | BIT(7) | BIT(4) | BIT(0);
	amvdec_write_dos(core, HEVC_PARSER_INT_CONTROL, val);
	amvdec_write_dos_bits(core, HEVC_SHIFT_STATUS, BIT(0));
	amvdec_write_dos(core, HEVC_SHIFT_CONTROL, BIT(10) | BIT(9) |
			 (3 << 6) | BIT(5) | BIT(2) | BIT(1) | BIT(0));
	amvdec_write_dos(core, HEVC_CABAC_CONTROL, BIT(0));
	amvdec_write_dos(core, HEVC_PARSER_CORE_CONTROL, BIT(0));
	amvdec_write_dos(core, HEVC_SHIFT_STARTCODE, 0x00000001);

	amvdec_write_dos(core, VP9_DEC_STATUS_REG, 0);

	amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE, BIT(16));
	for (i = 0; i < ARRAY_SIZE(vdec_hevc_parser_cmd); ++i)
		amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE,
				 vdec_hevc_parser_cmd[i]);

	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_0, PARSER_CMD_SKIP_CFG_0);
	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_1, PARSER_CMD_SKIP_CFG_1);
	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_2, PARSER_CMD_SKIP_CFG_2);
	amvdec_write_dos(core, HEVC_PARSER_IF_CONTROL,
			 BIT(5) | BIT(2) | BIT(0));

	amvdec_write_dos(core, HEVCD_IPP_TOP_CNTL, BIT(0));
	amvdec_write_dos(core, HEVCD_IPP_TOP_CNTL, BIT(1));

	amvdec_write_dos(core, VP9_WAIT_FLAG, 1);

	/* clear mailbox interrupt */
	amvdec_write_dos(core, HEVC_ASSIST_MBOX1_CLR_REG, 1);
	/* enable mailbox interrupt */
	amvdec_write_dos(core, HEVC_ASSIST_MBOX1_MASK, 1);
	/* disable PSCALE for hardware sharing */
	amvdec_write_dos(core, HEVC_PSCALE_CTRL, 0);
	/* Let the uCode do all the parsing */
	amvdec_write_dos(core, NAL_SEARCH_CTL, 0x8);

	amvdec_write_dos(core, DECODE_STOP_POS, 0);
	amvdec_write_dos(core, VP9_DECODE_MODE, DECODE_MODE_SINGLE);

	printk("decode_count: %u; decode_size: %u\n", amvdec_read_dos(core, HEVC_DECODE_COUNT), amvdec_read_dos(core, HEVC_DECODE_SIZE));

	vp9_loop_filter_init(core, vp9);

	INIT_LIST_HEAD(&vp9->ref_frames_list);
	mutex_init(&vp9->lock);
	memset(&vp9->ref_frame_map, -1, sizeof(vp9->ref_frame_map));
	memset(&vp9->next_ref_frame_map, -1, sizeof(vp9->next_ref_frame_map));
	sess->priv = vp9;

	return 0;

free_vp9:
	kfree(vp9);
	return ret;
}

static int codec_vp9_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9 = sess->priv;

	if (vp9->workspace_vaddr)
		dma_free_coherent(core->dev, SIZE_WORKSPACE,
				  vp9->workspace_vaddr,
				  vp9->workspace_paddr);

	codec_hevc_free_fbc_buffers(sess);
	return 0;
}

static void codec_vp9_set_sao(struct amvdec_session *sess, struct vb2_buffer *vb)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9 = sess->priv;

	dma_addr_t buf_y_paddr;
	dma_addr_t buf_u_v_paddr;
	u32 val;

	if (codec_hevc_use_downsample(sess->pixfmt_cap, vp9->is_10bit))
		buf_y_paddr =
			sess->fbc_buffer_paddr[vb->index];
	else
		buf_y_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 0);

	if (codec_hevc_use_fbc(sess->pixfmt_cap, vp9->is_10bit)) {
		val = amvdec_read_dos(core, HEVC_SAO_CTRL5) & ~0xff0200;
		amvdec_write_dos(core, HEVC_SAO_CTRL5, val);
		amvdec_write_dos(core, HEVC_CM_BODY_START_ADDR, buf_y_paddr);
	}

	if (sess->pixfmt_cap == V4L2_PIX_FMT_NV12M) {
		buf_y_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 0);
		buf_u_v_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 1);
		amvdec_write_dos(core, HEVC_SAO_Y_START_ADDR, buf_y_paddr);
		amvdec_write_dos(core, HEVC_SAO_C_START_ADDR, buf_u_v_paddr);
		amvdec_write_dos(core, HEVC_SAO_Y_WPTR, buf_y_paddr);
		amvdec_write_dos(core, HEVC_SAO_C_WPTR, buf_u_v_paddr);
	}

	amvdec_write_dos(core, HEVC_SAO_Y_LENGTH,
			 amvdec_get_output_size(sess));
	amvdec_write_dos(core, HEVC_SAO_C_LENGTH,
			 (amvdec_get_output_size(sess) / 2));

	val = amvdec_read_dos(core, HEVC_SAO_CTRL1) & ~0x3ff3;
	val |= 0xff0; /* Set endianness for 2-bytes swaps (nv12) */
	if (!codec_hevc_use_fbc(sess->pixfmt_cap, vp9->is_10bit))
		val |= BIT(0); /* disable cm compression */
	else if (sess->pixfmt_cap == V4L2_PIX_FMT_AM21C)
		val |= BIT(1); /* Disable double write */

	amvdec_write_dos(core, HEVC_SAO_CTRL1, val);

	if (!codec_hevc_use_fbc(sess->pixfmt_cap, vp9->is_10bit)) {
		/* no downscale for NV12 */
		val = amvdec_read_dos(core, HEVC_SAO_CTRL5) & ~0xff0000;
		amvdec_write_dos(core, HEVC_SAO_CTRL5, val);
	}

	val = amvdec_read_dos(core, HEVCD_IPP_AXIIF_CONFIG) & ~0x30;
	val |= 0xf;
	amvdec_write_dos(core, HEVCD_IPP_AXIIF_CONFIG, val);
}

static dma_addr_t codec_vp9_get_frame_mv_paddr(struct codec_vp9 *vp9,
					       struct vp9_frame *frame)
{
	return vp9->workspace_paddr + MPRED_MV_OFFSET +
	       (frame->index * MPRED_MV_BUF_SIZE);
}

static void codec_vp9_set_mpred_mv(struct amvdec_core *core,
				   struct codec_vp9 *vp9)
{
	int mpred_mv_rd_end_addr;
	int use_prev_frame_mvs = !vp9->prev_frame->intra_only &&
				 vp9->prev_frame->show &&
				 vp9->prev_frame->type != KEY_FRAME;

	amvdec_write_dos(core, HEVC_MPRED_CTRL3, 0x24122412);
	amvdec_write_dos(core, HEVC_MPRED_ABV_START_ADDR,
			 vp9->workspace_paddr + MPRED_ABV_OFFSET);

	amvdec_clear_dos_bits(core, HEVC_MPRED_CTRL4, BIT(6));
	if (use_prev_frame_mvs)
		amvdec_write_dos_bits(core, HEVC_MPRED_CTRL4, BIT(6));

	amvdec_write_dos(core, HEVC_MPRED_MV_WR_START_ADDR,
			 codec_vp9_get_frame_mv_paddr(vp9, vp9->cur_frame));
	amvdec_write_dos(core, HEVC_MPRED_MV_WPTR,
			 codec_vp9_get_frame_mv_paddr(vp9, vp9->cur_frame));

	amvdec_write_dos(core, HEVC_MPRED_MV_RD_START_ADDR,
			 codec_vp9_get_frame_mv_paddr(vp9, vp9->prev_frame));
	amvdec_write_dos(core, HEVC_MPRED_MV_RPTR,
			 codec_vp9_get_frame_mv_paddr(vp9, vp9->prev_frame));

	mpred_mv_rd_end_addr = codec_vp9_get_frame_mv_paddr(vp9, vp9->prev_frame)
			       + (vp9->lcu_total * MV_MEM_UNIT);
	amvdec_write_dos(core, HEVC_MPRED_MV_RD_END_ADDR, mpred_mv_rd_end_addr);
}

static void codec_vp9_update_next_ref(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	u32 buf_idx = vp9->cur_frame->index;
	int ref_index = 0;
	int refresh_frame_flags;
	int mask;

	refresh_frame_flags = vp9->cur_frame->type == KEY_FRAME ? 0xff :
			      param->p.refresh_frame_flags;

	for (mask = refresh_frame_flags; mask; mask >>= 1) {
		//printk("mask=%08X; ref_index=%d\n", mask, ref_index);
		if (mask & 1)
			vp9->next_ref_frame_map[ref_index] = buf_idx;
		else
			vp9->next_ref_frame_map[ref_index] =
				vp9->ref_frame_map[ref_index];

		++ref_index;
	}

	for (; ref_index < REF_FRAMES; ++ref_index)
		vp9->next_ref_frame_map[ref_index] =
			vp9->ref_frame_map[ref_index];
}

static void codec_vp9_update_ref(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	int ref_index = 0;
	int mask;
	int refresh_frame_flags;

	if (!vp9->cur_frame)
		return;

	refresh_frame_flags = vp9->cur_frame->type == KEY_FRAME ?
			      0xff :
			      param->p.refresh_frame_flags;

	for (mask = refresh_frame_flags; mask; mask >>= 1) {
		vp9->ref_frame_map[ref_index] =
			vp9->next_ref_frame_map[ref_index];
		++ref_index;
	}

	if (param->p.show_existing_frame)
		return;

	for (; ref_index < REF_FRAMES; ++ref_index)
		vp9->ref_frame_map[ref_index] =
			vp9->next_ref_frame_map[ref_index];
}

static struct vp9_frame * codec_vp9_get_frame_by_idx(struct codec_vp9 *vp9, int idx)
{
	struct vp9_frame *frame;

	list_for_each_entry(frame, &vp9->ref_frames_list, list) {
		if (frame->index == idx)
			return frame;
	}

	return NULL;
}

static void codec_vp9_sync_ref(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	int i;

	for (i = 0; i < REFS_PER_FRAME; ++i) {
		const int ref = (param->p.ref_info >>
				 (((REFS_PER_FRAME-i-1)*4)+1)) & 0x7;
		const int idx = vp9->ref_frame_map[ref];

		vp9->frame_refs[i] = codec_vp9_get_frame_by_idx(vp9, idx);
	}
}

static void codec_vp9_set_refs(struct amvdec_session *sess,
			       struct codec_vp9 *vp9)
{
	struct amvdec_core *core = sess->core;
	int i;

	for (i = 0; i < REFS_PER_FRAME; ++i) {
		struct vp9_frame *frame = vp9->frame_refs[i];
		int id_y;
		int id_u_v;

		if (!frame)
			continue;

		if (codec_hevc_use_fbc(sess->pixfmt_cap, vp9->is_10bit)) {
			id_y = frame->index;
			id_u_v = id_y;
		} else {
			id_y = frame->index * 2;
			id_u_v = id_y + 1;
		}

		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR,
				 (id_u_v << 16) | (id_u_v << 8) | id_y);
	}
}

static void codec_vp9_set_mc(struct amvdec_session *sess,
			     struct codec_vp9 *vp9)
{
	struct amvdec_core *core = sess->core;
	int i;

	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	codec_vp9_set_refs(sess, vp9);
	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, (16 << 8) | 1);
	codec_vp9_set_refs(sess, vp9);

	amvdec_write_dos(core, VP9D_MPP_REFINFO_TBL_ACCCONFIG, BIT(2));
	for (i = 0; i < REFS_PER_FRAME; ++i) {
		if (!vp9->frame_refs[i])
			continue;

		amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA, vp9->width);
		amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA, vp9->height);
		amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA,
				 (vp9->width << 14) / vp9->width);
		amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA,
				 (vp9->height << 14) / vp9->height);
		amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA,
				 amvdec_am21c_body_size(vp9->width, vp9->height) >> 5);
	}

	amvdec_write_dos(core, VP9D_MPP_REF_SCALE_ENBL, 0);
}

static struct vp9_frame *codec_vp9_get_new_frame(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;
	union rpm_param *param = &vp9->rpm_param;
	struct vb2_v4l2_buffer *vbuf;
	struct vp9_frame *new_frame;

	new_frame = kzalloc(sizeof(*new_frame), GFP_KERNEL);
	if (!new_frame)
		return NULL;

	vbuf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx);
	if (!vbuf) {
		dev_err(sess->core->dev, "No dst buffer available\n");
		kfree(new_frame);
		return NULL;
	}

	while (codec_vp9_get_frame_by_idx(vp9, vbuf->vb2_buf.index)) {
		struct vb2_v4l2_buffer *old_vbuf = vbuf;
		vbuf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx);
		v4l2_m2m_buf_queue(sess->m2m_ctx, old_vbuf);
		if (!vbuf) {
			dev_err(sess->core->dev, "No dst buffer available\n");
			kfree(new_frame);
			return NULL;
		}
	}

	new_frame->vbuf = vbuf;
	new_frame->index = vbuf->vb2_buf.index;
	new_frame->intra_only = param->p.intra_only;
	new_frame->show = param->p.show_frame;
	new_frame->type = param->p.frame_type;
	list_add_tail(&new_frame->list, &vp9->ref_frames_list);
	vp9->frames_num++;

	return new_frame;
}

static void codec_vp9_show_existing_frame(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;

	if (!param->p.show_existing_frame)
		return;

	printk("showing frame %u\n", param->p.frame_to_show_idx);
}

static void codec_vp9_rm_noshow_frame(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;
	struct vp9_frame *tmp;

	list_for_each_entry(tmp, &vp9->ref_frames_list, list) {
		if (tmp->show)
			continue;

		printk("rm noshow: %u\n", tmp->index);
		v4l2_m2m_buf_queue(sess->m2m_ctx, tmp->vbuf);
		list_del(&tmp->list);
		kfree(tmp);
		vp9->frames_num--;
		return;
	}
}

static void codec_vp9_process_frame(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9 = sess->priv;
	union rpm_param *param = &vp9->rpm_param;
	int intra_only;

	if (!param->p.show_frame)
		codec_vp9_rm_noshow_frame(sess);

	vp9->cur_frame = codec_vp9_get_new_frame(sess);
	if (!vp9->cur_frame)
		return;

	printk("frame type: %08X; show_exist: %u; show: %u, intra_only: %u\n", param->p.frame_type, param->p.show_existing_frame, param->p.show_frame, param->p.intra_only);
	codec_vp9_sync_ref(vp9);
	codec_vp9_update_next_ref(vp9);
	codec_vp9_show_existing_frame(vp9);

	intra_only = param->p.show_frame ? 0 : param->p.intra_only;
	/* clear mpred (for keyframe only) */
	if (param->p.frame_type != KEY_FRAME && !intra_only) {
		codec_vp9_set_mc(sess, vp9);
		codec_vp9_set_mpred_mv(core, vp9);
	} else {
		amvdec_clear_dos_bits(core, HEVC_MPRED_CTRL4, BIT(6));
	}

	amvdec_write_dos(core, HEVC_PARSER_PICTURE_SIZE,
			 (vp9->height << 16) | vp9->width);
	codec_vp9_set_sao(sess, &vp9->cur_frame->vbuf->vb2_buf);

	vp9_loop_filter_frame_init(core, &vp9->seg_4lf,
		&vp9->lfi, &vp9->lf, vp9->default_filt_lvl);

	/* ask uCode to start decoding */
	amvdec_write_dos(core, VP9_DEC_STATUS_REG, VP9_10B_DECODE_SLICE);
}

static void codec_vp9_process_lf(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	int i;

	vp9->lf.mode_ref_delta_enabled = param->p.mode_ref_delta_enabled;
	vp9->lf.sharpness_level = param->p.sharpness_level;
	vp9->default_filt_lvl = param->p.filter_level;
	vp9->seg_4lf.enabled = param->p.seg_enabled;
	vp9->seg_4lf.abs_delta = param->p.seg_abs_delta;

	for (i = 0; i < 4; i++)
		vp9->lf.ref_deltas[i] = param->p.ref_deltas[i];

	for (i = 0; i < 2; i++)
		vp9->lf.mode_deltas[i] = param->p.mode_deltas[i];

	for (i = 0; i < MAX_SEGMENTS; i++)
		vp9->seg_4lf.feature_mask[i] = (param->p.seg_lf_info[i] &
		0x8000) ? (1 << SEG_LVL_ALT_LF) : 0;

	for (i = 0; i < MAX_SEGMENTS; i++)
		vp9->seg_4lf.feature_data[i][SEG_LVL_ALT_LF]
		= (param->p.seg_lf_info[i]
		& 0x100) ? -(param->p.seg_lf_info[i]
		& 0x3f) : (param->p.seg_lf_info[i] & 0x3f);
}

static void codec_vp9_resume(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;

	if (codec_hevc_setup_buffers(sess, vp9->is_10bit)) {
		amvdec_abort(sess);
		return;
	}

	codec_hevc_setup_decode_head(sess, vp9->is_10bit);
	codec_vp9_process_lf(vp9);
	codec_vp9_process_frame(sess);
}

/**
 * The RPM section within the workspace contains
 * many information regarding the parsed bitstream
 */
static void codec_vp9_fetch_rpm(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;
	u16 *rpm_vaddr = vp9->workspace_vaddr + RPM_OFFSET;
	int i, j;

	for (i = 0; i < RPM_BUF_SIZE; i += 4)
		for (j = 0; j < 4; j++)
			vp9->rpm_param.l.data[i + j] = rpm_vaddr[i + 3 - j];
}

static int codec_vp9_process_rpm(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	int src_changed = 0;
	int is_10bit = 0;
	int pic_width_64 = ALIGN(param->p.width, 64);
	int pic_height_32 = ALIGN(param->p.height, 32);
	int pic_width_lcu  = (pic_width_64 % LCU_SIZE) ?
				pic_width_64 / LCU_SIZE  + 1
				: pic_width_64 / LCU_SIZE;
	int pic_height_lcu = (pic_height_32 % LCU_SIZE) ?
				pic_height_32 / LCU_SIZE + 1
				: pic_height_32 / LCU_SIZE;
	vp9->lcu_total = pic_width_lcu * pic_height_lcu;

	if (param->p.bit_depth == 10)
		is_10bit = 1;

	if (vp9->width != param->p.width ||
	    vp9->height != param->p.height ||
	    vp9->is_10bit != is_10bit)
	    src_changed = 1;

	vp9->width = param->p.width;
	vp9->height = param->p.height;
	vp9->is_10bit = is_10bit;

	printk("width: %u; height: %u; is_10bit: %d; src_changed: %d\n", vp9->width, vp9->height, is_10bit, src_changed);
	return src_changed;
}

static bool codec_vp9_is_ref(struct codec_vp9 *vp9, struct vp9_frame *frame)
{
	int i;

	for (i = 0; i < REFS_PER_FRAME; ++i)
		if (vp9->frame_refs[i] == frame)
			return true;

	return false;
}

static void codec_vp9_show_frame(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;
	struct vp9_frame *tmp, *n;

	list_for_each_entry_safe(tmp, n, &vp9->ref_frames_list, list) {
		if (!tmp->show || tmp == vp9->cur_frame)
			continue;

		if (!tmp->done) {
			printk("Doning %u\n", tmp->index);
			amvdec_dst_buf_done(sess, tmp->vbuf, V4L2_FIELD_NONE);
			tmp->done = 1;
			vp9->frames_num--;
		}

		if (codec_vp9_is_ref(vp9, tmp))
			continue;

		printk("deleting %d\n", tmp->index);
		list_del(&tmp->list);
		kfree(tmp);
	}
}

static irqreturn_t codec_vp9_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9 = sess->priv;
	u32 dec_status = amvdec_read_dos(core, VP9_DEC_STATUS_REG);
	u32 prob_status = amvdec_read_dos(core, VP9_ADAPT_PROB_REG);
	int i;

	if (!vp9)
		return IRQ_HANDLED;

	mutex_lock(&vp9->lock);
	if (dec_status != VP9_HEAD_PARSER_DONE) {
		dev_err(core->dev_dec, "Unrecognized dec_status: %08X\n",
			dec_status);
		amvdec_abort(sess);
		goto unlock;
	}

	printk("ISR: %08X;%08X\n", dec_status, prob_status);
	sess->keyframe_found = 1;

	/* Invalidate first 3 refs */
	for (i = 0; i < 3; ++i)
		vp9->frame_refs[i] = NULL;

	vp9->prev_frame = vp9->cur_frame;
	codec_vp9_update_ref(vp9);

	codec_vp9_fetch_rpm(sess);
	if (codec_vp9_process_rpm(vp9)) {
		amvdec_src_change(sess, vp9->width, vp9->height, 16);
		goto unlock;
	}

	codec_vp9_process_lf(vp9);
	codec_vp9_process_frame(sess);
	codec_vp9_show_frame(sess);

unlock:
	mutex_unlock(&vp9->lock);
	return IRQ_HANDLED;
}

static irqreturn_t codec_vp9_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_vp9_ops = {
	.start = codec_vp9_start,
	.stop = codec_vp9_stop,
	.isr = codec_vp9_isr,
	.threaded_isr = codec_vp9_threaded_isr,
	.num_pending_bufs = codec_vp9_num_pending_bufs,
	.drain = codec_vp9_flush_output,
	.resume = codec_vp9_resume,
};
