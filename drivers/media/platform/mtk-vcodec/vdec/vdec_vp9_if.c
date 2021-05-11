// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Daniel Hsiao <daniel.hsiao@mediatek.com>
 *	Kai-Sean Yang <kai-sean.yang@mediatek.com>
 *	Tiffany Lin <tiffany.lin@mediatek.com>
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <linux/time.h>

#include "../mtk_vcodec_intr.h"
#include "../vdec_drv_base.h"
#include "../vdec_vpu_if.h"

#define VP9_SUPER_FRAME_BS_SZ 64
#define MAX_VP9_DPB_SIZE	9

#define REFS_PER_FRAME 3
#define MAX_NUM_REF_FRAMES 8
#define VP9_MAX_FRM_BUF_NUM 9
#define VP9_MAX_FRM_BUF_NODE_NUM (VP9_MAX_FRM_BUF_NUM * 2)
#define VP9_SEG_ID_SZ 0x12000

/**
 * struct vp9_dram_buf - contains buffer info for vpu
 * @va : cpu address
 * @pa : iova address
 * @sz : buffer size
 * @padding : for 64 bytes alignment
 */
struct vp9_dram_buf {
	unsigned long va;
	unsigned long pa;
	unsigned int sz;
	unsigned int padding;
};

/**
 * struct vp9_fb_info - contains frame buffer info
 * @fb : frmae buffer
 * @reserved : reserved field used by vpu
 */
struct vp9_fb_info {
	struct vdec_fb *fb;
	unsigned int reserved[32];
};

/**
 * struct vp9_ref_cnt_buf - contains reference buffer information
 * @buf : referenced frame buffer
 * @ref_cnt : referenced frame buffer's reference count.
 *	When reference count=0, remove it from reference list
 */
struct vp9_ref_cnt_buf {
	struct vp9_fb_info buf;
	unsigned int ref_cnt;
};

/**
 * struct vp9_ref_buf - contains current frame's reference buffer information
 * @buf : reference buffer
 * @idx : reference buffer index to frm_bufs
 * @reserved : reserved field used by vpu
 */
struct vp9_ref_buf {
	struct vp9_fb_info *buf;
	unsigned int idx;
	unsigned int reserved[6];
};

/**
 * struct vp9_sf_ref_fb - contains frame buffer info
 * @fb : super frame reference frame buffer
 * @used : this reference frame info entry is used
 * @padding : for 64 bytes size align
 */
struct vp9_sf_ref_fb {
	struct vdec_fb fb;
	int used;
	int padding;
};

/*
 * struct vdec_vp9_vsi - shared buffer between host and VPU firmware
 *	AP-W/R : AP is writer/reader on this item
 *	VPU-W/R: VPU is write/reader on this item
 * @sf_bs_buf : super frame backup buffer (AP-W, VPU-R)
 * @sf_ref_fb : record supoer frame reference buffer information
 *	(AP-R/W, VPU-R/W)
 * @sf_next_ref_fb_idx : next available super frame (AP-W, VPU-R)
 * @sf_frm_cnt : super frame count, filled by vpu (AP-R, VPU-W)
 * @sf_frm_offset : super frame offset, filled by vpu (AP-R, VPU-W)
 * @sf_frm_sz : super frame size, filled by vpu (AP-R, VPU-W)
 * @sf_frm_idx : current super frame (AP-R, VPU-W)
 * @sf_init : inform super frame info already parsed by vpu (AP-R, VPU-W)
 * @fb : capture buffer (AP-W, VPU-R)
 * @bs : bs buffer (AP-W, VPU-R)
 * @cur_fb : current show capture buffer (AP-R/W, VPU-R/W)
 * @pic_w : picture width (AP-R, VPU-W)
 * @pic_h : picture height (AP-R, VPU-W)
 * @buf_w : codec width (AP-R, VPU-W)
 * @buf_h : coded height (AP-R, VPU-W)
 * @buf_sz_y_bs : ufo compressed y plane size (AP-R, VPU-W)
 * @buf_sz_c_bs : ufo compressed cbcr plane size (AP-R, VPU-W)
 * @buf_len_sz_y : size used to store y plane ufo info (AP-R, VPU-W)
 * @buf_len_sz_c : size used to store cbcr plane ufo info (AP-R, VPU-W)

 * @profile : profile sparsed from vpu (AP-R, VPU-W)
 * @show_frame : [BIT(0)] display this frame or not (AP-R, VPU-W)
 *	[BIT(1)] reset segment data or not (AP-R, VPU-W)
 *	[BIT(2)] trig decoder hardware or not (AP-R, VPU-W)
 *	[BIT(3)] ask VPU to set bits(0~4) accordingly (AP-W, VPU-R)
 *	[BIT(4)] do not reset segment data before every frame (AP-R, VPU-W)
 * @show_existing_frame : inform this frame is show existing frame
 *	(AP-R, VPU-W)
 * @frm_to_show_idx : index to show frame (AP-R, VPU-W)

 * @refresh_frm_flags : indicate when frame need to refine reference count
 *	(AP-R, VPU-W)
 * @resolution_changed : resolution change in this frame (AP-R, VPU-W)

 * @frm_bufs : maintain reference buffer info (AP-R/W, VPU-R/W)
 * @ref_frm_map : maintain reference buffer map info (AP-R/W, VPU-R/W)
 * @new_fb_idx : index to frm_bufs array (AP-R, VPU-W)
 * @frm_num : decoded frame number, include sub-frame count (AP-R, VPU-W)
 * @mv_buf : motion vector working buffer (AP-W, VPU-R)
 * @frm_refs : maintain three reference buffer info (AP-R/W, VPU-R/W)
 * @seg_id_buf : segmentation map working buffer (AP-W, VPU-R)
 */
struct vdec_vp9_vsi {
	unsigned char sf_bs_buf[VP9_SUPER_FRAME_BS_SZ];
	struct vp9_sf_ref_fb sf_ref_fb[VP9_MAX_FRM_BUF_NUM-1];
	int sf_next_ref_fb_idx;
	unsigned int sf_frm_cnt;
	unsigned int sf_frm_offset[VP9_MAX_FRM_BUF_NUM-1];
	unsigned int sf_frm_sz[VP9_MAX_FRM_BUF_NUM-1];
	unsigned int sf_frm_idx;
	unsigned int sf_init;
	struct vdec_fb fb;
	struct mtk_vcodec_mem bs;
	struct vdec_fb cur_fb;
	unsigned int pic_w;
	unsigned int pic_h;
	unsigned int buf_w;
	unsigned int buf_h;
	unsigned int buf_sz_y_bs;
	unsigned int buf_sz_c_bs;
	unsigned int buf_len_sz_y;
	unsigned int buf_len_sz_c;
	unsigned int profile;
	unsigned int show_frame;
	unsigned int show_existing_frame;
	unsigned int frm_to_show_idx;
	unsigned int refresh_frm_flags;
	unsigned int resolution_changed;

	struct vp9_ref_cnt_buf frm_bufs[VP9_MAX_FRM_BUF_NUM];
	int ref_frm_map[MAX_NUM_REF_FRAMES];
	unsigned int new_fb_idx;
	unsigned int frm_num;
	struct vp9_dram_buf mv_buf;

	struct vp9_ref_buf frm_refs[REFS_PER_FRAME];
	struct vp9_dram_buf seg_id_buf;

};

/*
 * struct vdec_vp9_inst - vp9 decode instance
 * @mv_buf : working buffer for mv
 * @seg_id_buf : working buffer for segmentation map
 * @dec_fb : vdec_fb node to link fb to different fb_xxx_list
 * @available_fb_node_list : current available vdec_fb node
 * @fb_use_list : current used or referenced vdec_fb
 * @fb_free_list : current available to free vdec_fb
 * @fb_disp_list : current available to display vdec_fb
 * @cur_fb : current frame buffer
 * @ctx : current decode context
 * @vpu : vpu instance information
 * @vsi : shared buffer between host and VPU firmware
 * @total_frm_cnt : total frame count, it do not include sub-frames in super
 *	    frame
 * @mem : instance memory information
 */
struct vdec_vp9_inst {
	struct mtk_vcodec_mem mv_buf;
	struct mtk_vcodec_mem seg_id_buf;

	struct vdec_fb_node dec_fb[VP9_MAX_FRM_BUF_NODE_NUM];
	struct list_head available_fb_node_list;
	struct list_head fb_use_list;
	struct list_head fb_free_list;
	struct list_head fb_disp_list;
	struct vdec_fb *cur_fb;
	struct mtk_vcodec_ctx *ctx;
	struct vdec_vpu_inst vpu;
	struct vdec_vp9_vsi *vsi;
	unsigned int total_frm_cnt;
	struct mtk_vcodec_mem mem;
};

static bool vp9_is_sf_ref_fb(struct vdec_vp9_inst *inst, struct vdec_fb *fb)
{
	int i;
	struct vdec_vp9_vsi *vsi = inst->vsi;

	for (i = 0; i < ARRAY_SIZE(vsi->sf_ref_fb); i++) {
		if (fb == &vsi->sf_ref_fb[i].fb)
			return true;
	}
	return false;
}

static struct vdec_fb *vp9_rm_from_fb_use_list(struct vdec_vp9_inst
					*inst, void *addr)
{
	struct vdec_fb *fb = NULL;
	struct vdec_fb_node *node;

	list_for_each_entry(node, &inst->fb_use_list, list) {
		fb = (struct vdec_fb *)node->fb;
		if (fb->base_y.va == addr) {
			list_move_tail(&node->list,
				       &inst->available_fb_node_list);
			break;
		}
	}
	return fb;
}

static void vp9_add_to_fb_free_list(struct vdec_vp9_inst *inst,
			     struct vdec_fb *fb)
{
	struct vdec_fb_node *node;

	if (fb) {
		node = list_first_entry_or_null(&inst->available_fb_node_list,
					struct vdec_fb_node, list);

		if (node) {
			node->fb = fb;
			list_move_tail(&node->list, &inst->fb_free_list);
		}
	} else {
		mtk_vcodec_debug(inst, "No free fb node");
	}
}

static void vp9_free_sf_ref_fb(struct vdec_fb *fb)
{
	struct vp9_sf_ref_fb *sf_ref_fb =
		container_of(fb, struct vp9_sf_ref_fb, fb);

	sf_ref_fb->used = 0;
}

static void vp9_ref_cnt_fb(struct vdec_vp9_inst *inst, int *idx,
			   int new_idx)
{
	struct vdec_vp9_vsi *vsi = inst->vsi;
	int ref_idx = *idx;

	if (ref_idx >= 0 && vsi->frm_bufs[ref_idx].ref_cnt > 0) {
		vsi->frm_bufs[ref_idx].ref_cnt--;

		if (vsi->frm_bufs[ref_idx].ref_cnt == 0) {
			if (!vp9_is_sf_ref_fb(inst,
					      vsi->frm_bufs[ref_idx].buf.fb)) {
				struct vdec_fb *fb;

				fb = vp9_rm_from_fb_use_list(inst,
				     vsi->frm_bufs[ref_idx].buf.fb->base_y.va);
				vp9_add_to_fb_free_list(inst, fb);
			} else
				vp9_free_sf_ref_fb(
					vsi->frm_bufs[ref_idx].buf.fb);
		}
	}

	*idx = new_idx;
	vsi->frm_bufs[new_idx].ref_cnt++;
}

static void vp9_free_all_sf_ref_fb(struct vdec_vp9_inst *inst)
{
	int i;
	struct vdec_vp9_vsi *vsi = inst->vsi;

	for (i = 0; i < ARRAY_SIZE(vsi->sf_ref_fb); i++) {
		if (vsi->sf_ref_fb[i].fb.base_y.va) {
			mtk_vcodec_mem_free(inst->ctx,
				&vsi->sf_ref_fb[i].fb.base_y);
			mtk_vcodec_mem_free(inst->ctx,
				&vsi->sf_ref_fb[i].fb.base_c);
			vsi->sf_ref_fb[i].used = 0;
		}
	}
}

/* For each sub-frame except the last one, the driver will dynamically
 * allocate reference buffer by calling vp9_get_sf_ref_fb()
 * The last sub-frame will use the original fb provided by the
 * vp9_dec_decode() interface
 */
static int vp9_get_sf_ref_fb(struct vdec_vp9_inst *inst)
{
	int idx;
	struct mtk_vcodec_mem *mem_basy_y;
	struct mtk_vcodec_mem *mem_basy_c;
	struct vdec_vp9_vsi *vsi = inst->vsi;

	for (idx = 0;
		idx < ARRAY_SIZE(vsi->sf_ref_fb);
		idx++) {
		if (vsi->sf_ref_fb[idx].fb.base_y.va &&
		    vsi->sf_ref_fb[idx].used == 0) {
			return idx;
		}
	}

	for (idx = 0;
		idx < ARRAY_SIZE(vsi->sf_ref_fb);
		idx++) {
		if (vsi->sf_ref_fb[idx].fb.base_y.va == NULL)
			break;
	}

	if (idx == ARRAY_SIZE(vsi->sf_ref_fb)) {
		mtk_vcodec_err(inst, "List Full");
		return -1;
	}

	mem_basy_y = &vsi->sf_ref_fb[idx].fb.base_y;
	mem_basy_y->size = vsi->buf_sz_y_bs +
		vsi->buf_len_sz_y;

	if (mtk_vcodec_mem_alloc(inst->ctx, mem_basy_y)) {
		mtk_vcodec_err(inst, "Cannot allocate sf_ref_buf y_buf");
		return -1;
	}

	mem_basy_c = &vsi->sf_ref_fb[idx].fb.base_c;
	mem_basy_c->size = vsi->buf_sz_c_bs +
		vsi->buf_len_sz_c;

	if (mtk_vcodec_mem_alloc(inst->ctx, mem_basy_c)) {
		mtk_vcodec_err(inst, "Cannot allocate sf_ref_fb c_buf");
		return -1;
	}
	vsi->sf_ref_fb[idx].used = 0;

	return idx;
}

static bool vp9_alloc_work_buf(struct vdec_vp9_inst *inst)
{
	struct vdec_vp9_vsi *vsi = inst->vsi;
	int result;
	struct mtk_vcodec_mem *mem;

	unsigned int max_pic_w;
	unsigned int max_pic_h;


	if (!(inst->ctx->dev->dec_capability &
		VCODEC_CAPABILITY_4K_DISABLED)) {
		max_pic_w = VCODEC_DEC_4K_CODED_WIDTH;
		max_pic_h = VCODEC_DEC_4K_CODED_HEIGHT;
	} else {
		max_pic_w = MTK_VDEC_MAX_W;
		max_pic_h = MTK_VDEC_MAX_H;
	}

	if ((vsi->pic_w > max_pic_w) ||
		(vsi->pic_h > max_pic_h)) {
		mtk_vcodec_err(inst, "Invalid w/h %d/%d",
				vsi->pic_w, vsi->pic_h);
		return false;
	}

	mtk_vcodec_debug(inst, "BUF CHG(%d): w/h/sb_w/sb_h=%d/%d/%d/%d",
			vsi->resolution_changed,
			vsi->pic_w,
			vsi->pic_h,
			vsi->buf_w,
			vsi->buf_h);

	mem = &inst->mv_buf;
	if (mem->va)
		mtk_vcodec_mem_free(inst->ctx, mem);

	mem->size = ((vsi->buf_w / 64) *
		    (vsi->buf_h / 64) + 2) * 36 * 16;
	result = mtk_vcodec_mem_alloc(inst->ctx, mem);
	if (result) {
		mem->size = 0;
		mtk_vcodec_err(inst, "Cannot allocate mv_buf");
		return false;
	}
	/* Set the va again */
	vsi->mv_buf.va = (unsigned long)mem->va;
	vsi->mv_buf.pa = (unsigned long)mem->dma_addr;
	vsi->mv_buf.sz = (unsigned int)mem->size;


	mem = &inst->seg_id_buf;
	if (mem->va)
		mtk_vcodec_mem_free(inst->ctx, mem);

	mem->size = VP9_SEG_ID_SZ;
	result = mtk_vcodec_mem_alloc(inst->ctx, mem);
	if (result) {
		mem->size = 0;
		mtk_vcodec_err(inst, "Cannot allocate seg_id_buf");
		return false;
	}
	/* Set the va again */
	vsi->seg_id_buf.va = (unsigned long)mem->va;
	vsi->seg_id_buf.pa = (unsigned long)mem->dma_addr;
	vsi->seg_id_buf.sz = (unsigned int)mem->size;


	vp9_free_all_sf_ref_fb(inst);
	vsi->sf_next_ref_fb_idx = vp9_get_sf_ref_fb(inst);

	return true;
}

static bool vp9_add_to_fb_disp_list(struct vdec_vp9_inst *inst,
			     struct vdec_fb *fb)
{
	struct vdec_fb_node *node;

	if (!fb) {
		mtk_vcodec_err(inst, "fb == NULL");
		return false;
	}

	node = list_first_entry_or_null(&inst->available_fb_node_list,
					struct vdec_fb_node, list);
	if (node) {
		node->fb = fb;
		list_move_tail(&node->list, &inst->fb_disp_list);
	} else {
		mtk_vcodec_err(inst, "No available fb node");
		return false;
	}

	return true;
}

/* If any buffer updating is signaled it should be done here. */
static void vp9_swap_frm_bufs(struct vdec_vp9_inst *inst)
{
	struct vdec_vp9_vsi *vsi = inst->vsi;
	struct vp9_fb_info *frm_to_show;
	int ref_index = 0, mask;

	for (mask = vsi->refresh_frm_flags; mask; mask >>= 1) {
		if (mask & 1)
			vp9_ref_cnt_fb(inst, &vsi->ref_frm_map[ref_index],
				       vsi->new_fb_idx);
		++ref_index;
	}

	frm_to_show = &vsi->frm_bufs[vsi->new_fb_idx].buf;
	vsi->frm_bufs[vsi->new_fb_idx].ref_cnt--;

	if (frm_to_show->fb != inst->cur_fb) {
		/* This frame is show exist frame and no decode output
		 * copy frame data from frm_to_show to current CAPTURE
		 * buffer
		 */
		if ((frm_to_show->fb != NULL) &&
			(inst->cur_fb->base_y.size >=
			frm_to_show->fb->base_y.size) &&
			(inst->cur_fb->base_c.size >=
			frm_to_show->fb->base_c.size)) {
			memcpy((void *)inst->cur_fb->base_y.va,
				(void *)frm_to_show->fb->base_y.va,
				frm_to_show->fb->base_y.size);
			memcpy((void *)inst->cur_fb->base_c.va,
				(void *)frm_to_show->fb->base_c.va,
				frm_to_show->fb->base_c.size);
		} else {
			/* After resolution change case, current CAPTURE buffer
			 * may have less buffer size than frm_to_show buffer
			 * size
			 */
			if (frm_to_show->fb != NULL)
				mtk_vcodec_err(inst,
					"inst->cur_fb->base_y.size=%zu, frm_to_show->fb.base_y.size=%zu",
					inst->cur_fb->base_y.size,
					frm_to_show->fb->base_y.size);
		}
		if (!vp9_is_sf_ref_fb(inst, inst->cur_fb)) {
			if (vsi->show_frame & BIT(0))
				vp9_add_to_fb_disp_list(inst, inst->cur_fb);
		}
	} else {
		if (!vp9_is_sf_ref_fb(inst, inst->cur_fb)) {
			if (vsi->show_frame & BIT(0))
				vp9_add_to_fb_disp_list(inst, frm_to_show->fb);
		}
	}

	/* when ref_cnt ==0, move this fb to fb_free_list. v4l2 driver will
	 * clean fb_free_list
	 */
	if (vsi->frm_bufs[vsi->new_fb_idx].ref_cnt == 0) {
		if (!vp9_is_sf_ref_fb(
			inst, vsi->frm_bufs[vsi->new_fb_idx].buf.fb)) {
			struct vdec_fb *fb;

			fb = vp9_rm_from_fb_use_list(inst,
			vsi->frm_bufs[vsi->new_fb_idx].buf.fb->base_y.va);

			vp9_add_to_fb_free_list(inst, fb);
		} else {
			vp9_free_sf_ref_fb(
				vsi->frm_bufs[vsi->new_fb_idx].buf.fb);
		}
	}

	/* if this super frame and it is not last sub-frame, get next fb for
	 * sub-frame decode
	 */
	if (vsi->sf_frm_cnt > 0 && vsi->sf_frm_idx != vsi->sf_frm_cnt - 1)
		vsi->sf_next_ref_fb_idx = vp9_get_sf_ref_fb(inst);
}

static bool vp9_wait_dec_end(struct vdec_vp9_inst *inst)
{
	struct mtk_vcodec_ctx *ctx = inst->ctx;

	mtk_vcodec_wait_for_done_ctx(inst->ctx,
			MTK_INST_IRQ_RECEIVED,
			WAIT_INTR_TIMEOUT_MS);

	if (ctx->irq_status & MTK_VDEC_IRQ_STATUS_DEC_SUCCESS)
		return true;
	else
		return false;
}

static struct vdec_vp9_inst *vp9_alloc_inst(struct mtk_vcodec_ctx *ctx)
{
	int result;
	struct mtk_vcodec_mem mem;
	struct vdec_vp9_inst *inst;

	memset(&mem, 0, sizeof(mem));
	mem.size = sizeof(struct vdec_vp9_inst);
	result = mtk_vcodec_mem_alloc(ctx, &mem);
	if (result)
		return NULL;

	inst = mem.va;
	inst->mem = mem;

	return inst;
}

static void vp9_free_inst(struct vdec_vp9_inst *inst)
{
	struct mtk_vcodec_mem mem;

	mem = inst->mem;
	if (mem.va)
		mtk_vcodec_mem_free(inst->ctx, &mem);
}

static bool vp9_decode_end_proc(struct vdec_vp9_inst *inst)
{
	struct vdec_vp9_vsi *vsi = inst->vsi;
	bool ret = false;

	if (!vsi->show_existing_frame) {
		ret = vp9_wait_dec_end(inst);
		if (!ret) {
			mtk_vcodec_err(inst, "Decode failed, Decode Timeout @[%d]",
				vsi->frm_num);
			return false;
		}

		if (vpu_dec_end(&inst->vpu)) {
			mtk_vcodec_err(inst, "vp9_dec_vpu_end failed");
			return false;
		}
		mtk_vcodec_debug(inst, "Decode Ok @%d (%d/%d)", vsi->frm_num,
				vsi->pic_w, vsi->pic_h);
	} else {
		mtk_vcodec_debug(inst, "Decode Ok @%d (show_existing_frame)",
				vsi->frm_num);
	}

	vp9_swap_frm_bufs(inst);
	vsi->frm_num++;
	return true;
}

static bool vp9_is_last_sub_frm(struct vdec_vp9_inst *inst)
{
	struct vdec_vp9_vsi *vsi = inst->vsi;

	if (vsi->sf_frm_cnt <= 0 || vsi->sf_frm_idx == vsi->sf_frm_cnt)
		return true;

	return false;
}

static struct vdec_fb *vp9_rm_from_fb_disp_list(struct vdec_vp9_inst *inst)
{
	struct vdec_fb_node *node;
	struct vdec_fb *fb = NULL;

	node = list_first_entry_or_null(&inst->fb_disp_list,
					struct vdec_fb_node, list);
	if (node) {
		fb = (struct vdec_fb *)node->fb;
		fb->status |= FB_ST_DISPLAY;
		list_move_tail(&node->list, &inst->available_fb_node_list);
		mtk_vcodec_debug(inst, "[FB] get disp fb %p st=%d",
				 node->fb, fb->status);
	} else
		mtk_vcodec_debug(inst, "[FB] there is no disp fb");

	return fb;
}

static bool vp9_add_to_fb_use_list(struct vdec_vp9_inst *inst,
			    struct vdec_fb *fb)
{
	struct vdec_fb_node *node;

	if (!fb) {
		mtk_vcodec_debug(inst, "fb == NULL");
		return false;
	}

	node = list_first_entry_or_null(&inst->available_fb_node_list,
					struct vdec_fb_node, list);
	if (node) {
		node->fb = fb;
		list_move_tail(&node->list, &inst->fb_use_list);
	} else {
		mtk_vcodec_err(inst, "No free fb node");
		return false;
	}
	return true;
}

static void vp9_reset(struct vdec_vp9_inst *inst)
{
	struct vdec_fb_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, &inst->fb_use_list, list)
		list_move_tail(&node->list, &inst->fb_free_list);

	vp9_free_all_sf_ref_fb(inst);
	inst->vsi->sf_next_ref_fb_idx = vp9_get_sf_ref_fb(inst);

	if (vpu_dec_reset(&inst->vpu))
		mtk_vcodec_err(inst, "vp9_dec_vpu_reset failed");

	/* Set the va again, since vpu_dec_reset will clear mv_buf in vpu */
	inst->vsi->mv_buf.va = (unsigned long)inst->mv_buf.va;
	inst->vsi->mv_buf.pa = (unsigned long)inst->mv_buf.dma_addr;
	inst->vsi->mv_buf.sz = (unsigned long)inst->mv_buf.size;

	/* Set the va again, since vpu_dec_reset will clear seg_id_buf in vpu */
	inst->vsi->seg_id_buf.va = (unsigned long)inst->seg_id_buf.va;
	inst->vsi->seg_id_buf.pa = (unsigned long)inst->seg_id_buf.dma_addr;
	inst->vsi->seg_id_buf.sz = (unsigned long)inst->seg_id_buf.size;

}

static void init_all_fb_lists(struct vdec_vp9_inst *inst)
{
	int i;

	INIT_LIST_HEAD(&inst->available_fb_node_list);
	INIT_LIST_HEAD(&inst->fb_use_list);
	INIT_LIST_HEAD(&inst->fb_free_list);
	INIT_LIST_HEAD(&inst->fb_disp_list);

	for (i = 0; i < ARRAY_SIZE(inst->dec_fb); i++) {
		INIT_LIST_HEAD(&inst->dec_fb[i].list);
		inst->dec_fb[i].fb = NULL;
		list_add_tail(&inst->dec_fb[i].list,
			      &inst->available_fb_node_list);
	}
}

static void get_pic_info(struct vdec_vp9_inst *inst, struct vdec_pic_info *pic)
{
	pic->fb_sz[0] = inst->vsi->buf_sz_y_bs + inst->vsi->buf_len_sz_y;
	pic->fb_sz[1] = inst->vsi->buf_sz_c_bs + inst->vsi->buf_len_sz_c;

	pic->pic_w = inst->vsi->pic_w;
	pic->pic_h = inst->vsi->pic_h;
	pic->buf_w = inst->vsi->buf_w;
	pic->buf_h = inst->vsi->buf_h;

	mtk_vcodec_debug(inst, "pic(%d, %d), buf(%d, %d)",
		 pic->pic_w, pic->pic_h, pic->buf_w, pic->buf_h);
	mtk_vcodec_debug(inst, "fb size: Y(%d), C(%d)",
		pic->fb_sz[0],
		pic->fb_sz[1]);
}

static void get_disp_fb(struct vdec_vp9_inst *inst, struct vdec_fb **out_fb)
{

	*out_fb = vp9_rm_from_fb_disp_list(inst);
	if (*out_fb)
		(*out_fb)->status |= FB_ST_DISPLAY;
}

static void get_free_fb(struct vdec_vp9_inst *inst, struct vdec_fb **out_fb)
{
	struct vdec_fb_node *node;
	struct vdec_fb *fb = NULL;

	node = list_first_entry_or_null(&inst->fb_free_list,
					struct vdec_fb_node, list);
	if (node) {
		list_move_tail(&node->list, &inst->available_fb_node_list);
		fb = (struct vdec_fb *)node->fb;
		fb->status |= FB_ST_FREE;
		mtk_vcodec_debug(inst, "[FB] get free fb %p st=%d",
				 node->fb, fb->status);
	} else {
		mtk_vcodec_debug(inst, "[FB] there is no free fb");
	}

	*out_fb = fb;
}

static int validate_vsi_array_indexes(struct vdec_vp9_inst *inst,
		struct vdec_vp9_vsi *vsi) {
	if (vsi->sf_frm_idx >= VP9_MAX_FRM_BUF_NUM - 1) {
		mtk_vcodec_err(inst, "Invalid vsi->sf_frm_idx=%u.",
				vsi->sf_frm_idx);
		return -EIO;
	}
	if (vsi->frm_to_show_idx >= VP9_MAX_FRM_BUF_NUM) {
		mtk_vcodec_err(inst, "Invalid vsi->frm_to_show_idx=%u.",
				vsi->frm_to_show_idx);
		return -EIO;
	}
	if (vsi->new_fb_idx >= VP9_MAX_FRM_BUF_NUM) {
		mtk_vcodec_err(inst, "Invalid vsi->new_fb_idx=%u.",
				vsi->new_fb_idx);
		return -EIO;
	}
	return 0;
}

static void vdec_vp9_deinit(void *h_vdec)
{
	struct vdec_vp9_inst *inst = (struct vdec_vp9_inst *)h_vdec;
	struct mtk_vcodec_mem *mem;
	int ret = 0;

	ret = vpu_dec_deinit(&inst->vpu);
	if (ret)
		mtk_vcodec_err(inst, "vpu_dec_deinit failed");

	mem = &inst->mv_buf;
	if (mem->va)
		mtk_vcodec_mem_free(inst->ctx, mem);

	mem = &inst->seg_id_buf;
	if (mem->va)
		mtk_vcodec_mem_free(inst->ctx, mem);

	vp9_free_all_sf_ref_fb(inst);
	vp9_free_inst(inst);
}

static int vdec_vp9_init(struct mtk_vcodec_ctx *ctx)
{
	struct vdec_vp9_inst *inst;

	inst = vp9_alloc_inst(ctx);
	if (!inst)
		return -ENOMEM;

	inst->total_frm_cnt = 0;
	inst->ctx = ctx;

	inst->vpu.id = IPI_VDEC_VP9;
	inst->vpu.ctx = ctx;

	if (vpu_dec_init(&inst->vpu)) {
		mtk_vcodec_err(inst, "vp9_dec_vpu_init failed");
		goto err_deinit_inst;
	}

	inst->vsi = (struct vdec_vp9_vsi *)inst->vpu.vsi;

	inst->vsi->show_frame |= BIT(3);

	init_all_fb_lists(inst);

	ctx->drv_handle = inst;
	return 0;

err_deinit_inst:
	vp9_free_inst(inst);

	return -EINVAL;
}

static int vdec_vp9_decode(void *h_vdec, struct mtk_vcodec_mem *bs,
			   struct vdec_fb *fb, bool *res_chg)
{
	int ret = 0;
	struct vdec_vp9_inst *inst = (struct vdec_vp9_inst *)h_vdec;
	struct vdec_vp9_vsi *vsi = inst->vsi;
	u32 data[3];
	int i;

	*res_chg = false;

	if ((bs == NULL) && (fb == NULL)) {
		mtk_vcodec_debug(inst, "[EOS]");
		vp9_reset(inst);
		return ret;
	}

	if (bs == NULL) {
		mtk_vcodec_err(inst, "bs == NULL");
		return -EINVAL;
	}

	mtk_vcodec_debug(inst, "Input BS Size = %zu", bs->size);

	while (1) {
		struct vdec_fb *cur_fb = NULL;

		data[0] = *((unsigned int *)bs->va);
		data[1] = *((unsigned int *)(bs->va + 4));
		data[2] = *((unsigned int *)(bs->va + 8));

		vsi->bs = *bs;

		if (fb)
			vsi->fb = *fb;

		if (!vsi->sf_init) {
			unsigned int sf_bs_sz;
			unsigned int sf_bs_off;
			unsigned char *sf_bs_src;
			unsigned char *sf_bs_dst;

			sf_bs_sz = bs->size > VP9_SUPER_FRAME_BS_SZ ?
					VP9_SUPER_FRAME_BS_SZ : bs->size;
			sf_bs_off = VP9_SUPER_FRAME_BS_SZ - sf_bs_sz;
			sf_bs_src = bs->va + bs->size - sf_bs_sz;
			sf_bs_dst = vsi->sf_bs_buf + sf_bs_off;
			memcpy(sf_bs_dst, sf_bs_src, sf_bs_sz);
		} else {
			if ((vsi->sf_frm_cnt > 0) &&
				(vsi->sf_frm_idx < vsi->sf_frm_cnt)) {
				unsigned int idx = vsi->sf_frm_idx;

				memcpy((void *)bs->va,
					(void *)(bs->va +
					vsi->sf_frm_offset[idx]),
					vsi->sf_frm_sz[idx]);
			}
		}

		if (!(vsi->show_frame & BIT(4)))
			memset(inst->seg_id_buf.va, 0, inst->seg_id_buf.size);

		ret = vpu_dec_start(&inst->vpu, data, 3);
		if (ret) {
			mtk_vcodec_err(inst, "vpu_dec_start failed");
			goto DECODE_ERROR;
		}

		if (vsi->show_frame & BIT(1)) {
			memset(inst->seg_id_buf.va, 0, inst->seg_id_buf.size);

			if (vsi->show_frame & BIT(2)) {
				ret = vpu_dec_start(&inst->vpu, NULL, 0);
				if (ret) {
					mtk_vcodec_err(inst, "vpu trig decoder failed");
					goto DECODE_ERROR;
				}
			}
		}

		ret = validate_vsi_array_indexes(inst, vsi);
		if (ret) {
			mtk_vcodec_err(inst, "Invalid values from VPU.");
			goto DECODE_ERROR;
		}

		if (vsi->resolution_changed) {
			if (!vp9_alloc_work_buf(inst)) {
				ret = -EIO;
				goto DECODE_ERROR;
			}
		}

		if (vsi->sf_frm_cnt > 0) {
			cur_fb = &vsi->sf_ref_fb[vsi->sf_next_ref_fb_idx].fb;

			if (vsi->sf_frm_idx < vsi->sf_frm_cnt)
				inst->cur_fb = cur_fb;
			else
				inst->cur_fb = fb;
		} else {
			inst->cur_fb = fb;
		}

		vsi->frm_bufs[vsi->new_fb_idx].buf.fb = inst->cur_fb;
		if (!vp9_is_sf_ref_fb(inst, inst->cur_fb))
			vp9_add_to_fb_use_list(inst, inst->cur_fb);

		mtk_vcodec_debug(inst, "[#pic %d]", vsi->frm_num);

		if (vsi->show_existing_frame)
			mtk_vcodec_debug(inst,
				"drv->new_fb_idx=%d, drv->frm_to_show_idx=%d",
				vsi->new_fb_idx, vsi->frm_to_show_idx);

		if (vsi->show_existing_frame && (vsi->frm_to_show_idx <
					VP9_MAX_FRM_BUF_NUM)) {
			mtk_vcodec_debug(inst,
				"Skip Decode drv->new_fb_idx=%d, drv->frm_to_show_idx=%d",
				vsi->new_fb_idx, vsi->frm_to_show_idx);

			vp9_ref_cnt_fb(inst, &vsi->new_fb_idx,
					vsi->frm_to_show_idx);
		}

		/* VPU assign the buffer pointer in its address space,
		 * reassign here
		 */
		for (i = 0; i < ARRAY_SIZE(vsi->frm_refs); i++) {
			unsigned int idx = vsi->frm_refs[i].idx;

			vsi->frm_refs[i].buf = &vsi->frm_bufs[idx].buf;
		}

		if (vsi->resolution_changed) {
			*res_chg = true;
			mtk_vcodec_debug(inst, "VDEC_ST_RESOLUTION_CHANGED");

			ret = 0;
			goto DECODE_ERROR;
		}

		if (!vp9_decode_end_proc(inst)) {
			mtk_vcodec_err(inst, "vp9_decode_end_proc");
			ret = -EINVAL;
			goto DECODE_ERROR;
		}

		if (vp9_is_last_sub_frm(inst))
			break;

	}
	inst->total_frm_cnt++;

DECODE_ERROR:
	if (ret < 0)
		vp9_add_to_fb_free_list(inst, fb);

	return ret;
}

static void get_crop_info(struct vdec_vp9_inst *inst, struct v4l2_rect *cr)
{
	cr->left = 0;
	cr->top = 0;
	cr->width = inst->vsi->pic_w;
	cr->height = inst->vsi->pic_h;
	mtk_vcodec_debug(inst, "get crop info l=%d, t=%d, w=%d, h=%d\n",
			 cr->left, cr->top, cr->width, cr->height);
}

static int vdec_vp9_get_param(void *h_vdec, enum vdec_get_param_type type,
			      void *out)
{
	struct vdec_vp9_inst *inst = (struct vdec_vp9_inst *)h_vdec;
	int ret = 0;

	switch (type) {
	case GET_PARAM_DISP_FRAME_BUFFER:
		get_disp_fb(inst, out);
		break;
	case GET_PARAM_FREE_FRAME_BUFFER:
		get_free_fb(inst, out);
		break;
	case GET_PARAM_PIC_INFO:
		get_pic_info(inst, out);
		break;
	case GET_PARAM_DPB_SIZE:
		*((unsigned int *)out) = MAX_VP9_DPB_SIZE;
		break;
	case GET_PARAM_CROP_INFO:
		get_crop_info(inst, out);
		break;
	default:
		mtk_vcodec_err(inst, "not supported param type %d", type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

const struct vdec_common_if vdec_vp9_if = {
	.init		= vdec_vp9_init,
	.decode		= vdec_vp9_decode,
	.get_param	= vdec_vp9_get_param,
	.deinit		= vdec_vp9_deinit,
};
