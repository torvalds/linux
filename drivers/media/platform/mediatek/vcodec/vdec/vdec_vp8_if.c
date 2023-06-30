// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Jungchang Tsao <jungchang.tsao@mediatek.com>
 *	   PC Chen <pc.chen@mediatek.com>
 */

#include <linux/slab.h>
#include "../vdec_drv_if.h"
#include "../mtk_vcodec_util.h"
#include "../mtk_vcodec_dec.h"
#include "../mtk_vcodec_intr.h"
#include "../vdec_vpu_if.h"
#include "../vdec_drv_base.h"

/* Decoding picture buffer size (3 reference frames plus current frame) */
#define VP8_DPB_SIZE			4

/* HW working buffer size (bytes) */
#define VP8_WORKING_BUF_SZ		(45 * 4096)

/* HW control register address */
#define VP8_SEGID_DRAM_ADDR		0x3c
#define VP8_HW_VLD_ADDR			0x93C
#define VP8_HW_VLD_VALUE		0x940
#define VP8_BSASET			0x100
#define VP8_BSDSET			0x104
#define VP8_RW_CKEN_SET			0x0
#define VP8_RW_DCM_CON			0x18
#define VP8_WO_VLD_SRST			0x108
#define VP8_RW_MISC_SYS_SEL		0x84
#define VP8_RW_MISC_SPEC_CON		0xC8
#define VP8_WO_VLD_SRST			0x108
#define VP8_RW_VP8_CTRL			0xA4
#define VP8_RW_MISC_DCM_CON		0xEC
#define VP8_RW_MISC_SRST		0xF4
#define VP8_RW_MISC_FUNC_CON		0xCC

#define VP8_MAX_FRM_BUF_NUM		5
#define VP8_MAX_FRM_BUF_NODE_NUM	(VP8_MAX_FRM_BUF_NUM * 2)

/* required buffer size (bytes) to store decode information */
#define VP8_HW_SEGMENT_DATA_SZ		272
#define VP8_HW_SEGMENT_UINT		4

#define VP8_DEC_TABLE_PROC_LOOP		96
#define VP8_DEC_TABLE_UNIT		3
#define VP8_DEC_TABLE_SZ		300
#define VP8_DEC_TABLE_OFFSET		2
#define VP8_DEC_TABLE_RW_UNIT		4

/**
 * struct vdec_vp8_dec_info - decode misc information
 * @working_buf_dma   : working buffer dma address
 * @prev_y_dma        : previous decoded frame buffer Y plane address
 * @cur_y_fb_dma      : current plane Y frame buffer dma address
 * @cur_c_fb_dma      : current plane C frame buffer dma address
 * @bs_dma	      : bitstream dma address
 * @bs_sz	      : bitstream size
 * @resolution_changed: resolution change flag 1 - changed,  0 - not change
 * @show_frame	      : display this frame or not
 * @wait_key_frame    : wait key frame coming
 */
struct vdec_vp8_dec_info {
	uint64_t working_buf_dma;
	uint64_t prev_y_dma;
	uint64_t cur_y_fb_dma;
	uint64_t cur_c_fb_dma;
	uint64_t bs_dma;
	uint32_t bs_sz;
	uint32_t resolution_changed;
	uint32_t show_frame;
	uint32_t wait_key_frame;
};

/**
 * struct vdec_vp8_vsi - VPU shared information
 * @dec			: decoding information
 * @pic			: picture information
 * @dec_table		: decoder coefficient table
 * @segment_buf		: segmentation buffer
 * @load_data		: flag to indicate reload decode data
 */
struct vdec_vp8_vsi {
	struct vdec_vp8_dec_info dec;
	struct vdec_pic_info pic;
	uint32_t dec_table[VP8_DEC_TABLE_SZ];
	uint32_t segment_buf[VP8_HW_SEGMENT_DATA_SZ][VP8_HW_SEGMENT_UINT];
	uint32_t load_data;
};

/**
 * struct vdec_vp8_hw_reg_base - HW register base
 * @misc	: base address for misc
 * @ld		: base address for ld
 * @top		: base address for top
 * @cm		: base address for cm
 * @hwd		: base address for hwd
 * @hwb		: base address for hwb
 */
struct vdec_vp8_hw_reg_base {
	void __iomem *misc;
	void __iomem *ld;
	void __iomem *top;
	void __iomem *cm;
	void __iomem *hwd;
	void __iomem *hwb;
};

/**
 * struct vdec_vp8_vpu_inst - VPU instance for VP8 decode
 * @wq_hd	: Wait queue to wait VPU message ack
 * @signaled	: 1 - Host has received ack message from VPU, 0 - not receive
 * @failure	: VPU execution result status 0 - success, others - fail
 * @inst_addr	: VPU decoder instance address
 */
struct vdec_vp8_vpu_inst {
	wait_queue_head_t wq_hd;
	int signaled;
	int failure;
	uint32_t inst_addr;
};

/* frame buffer (fb) list
 * [available_fb_node_list]  - decode fb are initialized to 0 and populated in
 * [fb_use_list]  - fb is set after decode and is moved to this list
 * [fb_free_list] - fb is not needed for reference will be moved from
 *		     [fb_use_list] to [fb_free_list] and
 *		     once user remove fb from [fb_free_list],
 *		     it is circulated back to [available_fb_node_list]
 * [fb_disp_list] - fb is set after decode and is moved to this list
 *                   once user remove fb from [fb_disp_list] it is
 *                   circulated back to [available_fb_node_list]
 */

/**
 * struct vdec_vp8_inst - VP8 decoder instance
 * @cur_fb		   : current frame buffer
 * @dec_fb		   : decode frame buffer node
 * @available_fb_node_list : list to store available frame buffer node
 * @fb_use_list		   : list to store frame buffer in use
 * @fb_free_list	   : list to store free frame buffer
 * @fb_disp_list	   : list to store display ready frame buffer
 * @working_buf		   : HW decoder working buffer
 * @reg_base		   : HW register base address
 * @frm_cnt		   : decode frame count
 * @ctx			   : V4L2 context
 * @vpu			   : VPU instance for decoder
 * @vsi			   : VPU share information
 */
struct vdec_vp8_inst {
	struct vdec_fb *cur_fb;
	struct vdec_fb_node dec_fb[VP8_MAX_FRM_BUF_NODE_NUM];
	struct list_head available_fb_node_list;
	struct list_head fb_use_list;
	struct list_head fb_free_list;
	struct list_head fb_disp_list;
	struct mtk_vcodec_mem working_buf;
	struct vdec_vp8_hw_reg_base reg_base;
	unsigned int frm_cnt;
	struct mtk_vcodec_ctx *ctx;
	struct vdec_vpu_inst vpu;
	struct vdec_vp8_vsi *vsi;
};

static void get_hw_reg_base(struct vdec_vp8_inst *inst)
{
	inst->reg_base.top = mtk_vcodec_get_reg_addr(inst->ctx, VDEC_TOP);
	inst->reg_base.cm = mtk_vcodec_get_reg_addr(inst->ctx, VDEC_CM);
	inst->reg_base.hwd = mtk_vcodec_get_reg_addr(inst->ctx, VDEC_HWD);
	inst->reg_base.misc = mtk_vcodec_get_reg_addr(inst->ctx, VDEC_MISC);
	inst->reg_base.ld = mtk_vcodec_get_reg_addr(inst->ctx, VDEC_LD);
	inst->reg_base.hwb = mtk_vcodec_get_reg_addr(inst->ctx, VDEC_HWB);
}

static void write_hw_segmentation_data(struct vdec_vp8_inst *inst)
{
	int i, j;
	u32 seg_id_addr;
	u32 val;
	void __iomem *cm = inst->reg_base.cm;
	struct vdec_vp8_vsi *vsi = inst->vsi;

	seg_id_addr = readl(inst->reg_base.top + VP8_SEGID_DRAM_ADDR) >> 4;

	for (i = 0; i < ARRAY_SIZE(vsi->segment_buf); i++) {
		for (j = ARRAY_SIZE(vsi->segment_buf[i]) - 1; j >= 0; j--) {
			val = (1 << 16) + ((seg_id_addr + i) << 2) + j;
			writel(val, cm + VP8_HW_VLD_ADDR);

			val = vsi->segment_buf[i][j];
			writel(val, cm + VP8_HW_VLD_VALUE);
		}
	}
}

static void read_hw_segmentation_data(struct vdec_vp8_inst *inst)
{
	int i, j;
	u32 seg_id_addr;
	u32 val;
	void __iomem *cm = inst->reg_base.cm;
	struct vdec_vp8_vsi *vsi = inst->vsi;

	seg_id_addr = readl(inst->reg_base.top + VP8_SEGID_DRAM_ADDR) >> 4;

	for (i = 0; i < ARRAY_SIZE(vsi->segment_buf); i++) {
		for (j = ARRAY_SIZE(vsi->segment_buf[i]) - 1; j >= 0; j--) {
			val = ((seg_id_addr + i) << 2) + j;
			writel(val, cm + VP8_HW_VLD_ADDR);

			val = readl(cm + VP8_HW_VLD_VALUE);
			vsi->segment_buf[i][j] = val;
		}
	}
}

/* reset HW and enable HW read/write data function */
static void enable_hw_rw_function(struct vdec_vp8_inst *inst)
{
	u32 val = 0;
	void __iomem *misc = inst->reg_base.misc;
	void __iomem *ld = inst->reg_base.ld;
	void __iomem *hwb = inst->reg_base.hwb;
	void __iomem *hwd = inst->reg_base.hwd;

	mtk_vcodec_write_vdecsys(inst->ctx, VP8_RW_CKEN_SET, 0x1);
	writel(0x101, ld + VP8_WO_VLD_SRST);
	writel(0x101, hwb + VP8_WO_VLD_SRST);

	mtk_vcodec_write_vdecsys(inst->ctx, 0, 0x1);
	val = readl(misc + VP8_RW_MISC_SRST);
	writel((val & 0xFFFFFFFE), misc + VP8_RW_MISC_SRST);

	writel(0x1, misc + VP8_RW_MISC_SYS_SEL);
	writel(0x17F, misc + VP8_RW_MISC_SPEC_CON);
	writel(0x71201100, misc + VP8_RW_MISC_FUNC_CON);
	writel(0x0, ld + VP8_WO_VLD_SRST);
	writel(0x0, hwb + VP8_WO_VLD_SRST);
	mtk_vcodec_write_vdecsys(inst->ctx, VP8_RW_DCM_CON, 0x1);
	writel(0x1, misc + VP8_RW_MISC_DCM_CON);
	writel(0x1, hwd + VP8_RW_VP8_CTRL);
}

static void store_dec_table(struct vdec_vp8_inst *inst)
{
	int i, j;
	u32 addr = 0, val = 0;
	void __iomem *hwd = inst->reg_base.hwd;
	u32 *p = &inst->vsi->dec_table[VP8_DEC_TABLE_OFFSET];

	for (i = 0; i < VP8_DEC_TABLE_PROC_LOOP; i++) {
		writel(addr, hwd + VP8_BSASET);
		for (j = 0; j < VP8_DEC_TABLE_UNIT ; j++) {
			val = *p++;
			writel(val, hwd + VP8_BSDSET);
		}
		addr += VP8_DEC_TABLE_RW_UNIT;
	}
}

static void load_dec_table(struct vdec_vp8_inst *inst)
{
	int i;
	u32 addr = 0;
	u32 *p = &inst->vsi->dec_table[VP8_DEC_TABLE_OFFSET];
	void __iomem *hwd = inst->reg_base.hwd;

	for (i = 0; i < VP8_DEC_TABLE_PROC_LOOP; i++) {
		writel(addr, hwd + VP8_BSASET);
		/* read total 11 bytes */
		*p++ = readl(hwd + VP8_BSDSET);
		*p++ = readl(hwd + VP8_BSDSET);
		*p++ = readl(hwd + VP8_BSDSET) & 0xFFFFFF;
		addr += VP8_DEC_TABLE_RW_UNIT;
	}
}

static void get_pic_info(struct vdec_vp8_inst *inst, struct vdec_pic_info *pic)
{
	*pic = inst->vsi->pic;

	mtk_vcodec_debug(inst, "pic(%d, %d), buf(%d, %d)",
			 pic->pic_w, pic->pic_h, pic->buf_w, pic->buf_h);
	mtk_vcodec_debug(inst, "fb size: Y(%d), C(%d)",
			 pic->fb_sz[0], pic->fb_sz[1]);
}

static void vp8_dec_finish(struct vdec_vp8_inst *inst)
{
	struct vdec_fb_node *node;
	uint64_t prev_y_dma = inst->vsi->dec.prev_y_dma;

	mtk_vcodec_debug(inst, "prev fb base dma=%llx", prev_y_dma);

	/* put last decode ok frame to fb_free_list */
	if (prev_y_dma != 0) {
		list_for_each_entry(node, &inst->fb_use_list, list) {
			struct vdec_fb *fb = (struct vdec_fb *)node->fb;

			if (prev_y_dma == (uint64_t)fb->base_y.dma_addr) {
				list_move_tail(&node->list,
					       &inst->fb_free_list);
				break;
			}
		}
	}

	/* available_fb_node_list -> fb_use_list */
	node = list_first_entry(&inst->available_fb_node_list,
				struct vdec_fb_node, list);
	node->fb = inst->cur_fb;
	list_move_tail(&node->list, &inst->fb_use_list);

	/* available_fb_node_list -> fb_disp_list */
	if (inst->vsi->dec.show_frame) {
		node = list_first_entry(&inst->available_fb_node_list,
					struct vdec_fb_node, list);
		node->fb = inst->cur_fb;
		list_move_tail(&node->list, &inst->fb_disp_list);
	}
}

static void move_fb_list_use_to_free(struct vdec_vp8_inst *inst)
{
	struct vdec_fb_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, &inst->fb_use_list, list)
		list_move_tail(&node->list, &inst->fb_free_list);
}

static void init_list(struct vdec_vp8_inst *inst)
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

static void add_fb_to_free_list(struct vdec_vp8_inst *inst, void *fb)
{
	struct vdec_fb_node *node;

	if (fb) {
		node = list_first_entry(&inst->available_fb_node_list,
					struct vdec_fb_node, list);
		node->fb = fb;
		list_move_tail(&node->list, &inst->fb_free_list);
	}
}

static int alloc_working_buf(struct vdec_vp8_inst *inst)
{
	int err;
	struct mtk_vcodec_mem *mem = &inst->working_buf;

	mem->size = VP8_WORKING_BUF_SZ;
	err = mtk_vcodec_mem_alloc(inst->ctx, mem);
	if (err) {
		mtk_vcodec_err(inst, "Cannot allocate working buffer");
		return err;
	}

	inst->vsi->dec.working_buf_dma = (uint64_t)mem->dma_addr;
	return 0;
}

static void free_working_buf(struct vdec_vp8_inst *inst)
{
	struct mtk_vcodec_mem *mem = &inst->working_buf;

	if (mem->va)
		mtk_vcodec_mem_free(inst->ctx, mem);

	inst->vsi->dec.working_buf_dma = 0;
}

static int vdec_vp8_init(struct mtk_vcodec_ctx *ctx)
{
	struct vdec_vp8_inst *inst;
	int err;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return  -ENOMEM;

	inst->ctx = ctx;

	inst->vpu.id = IPI_VDEC_VP8;
	inst->vpu.ctx = ctx;

	err = vpu_dec_init(&inst->vpu);
	if (err) {
		mtk_vcodec_err(inst, "vdec_vp8 init err=%d", err);
		goto error_free_inst;
	}

	inst->vsi = (struct vdec_vp8_vsi *)inst->vpu.vsi;
	init_list(inst);
	err = alloc_working_buf(inst);
	if (err)
		goto error_deinit;

	get_hw_reg_base(inst);
	mtk_vcodec_debug(inst, "VP8 Instance >> %p", inst);

	ctx->drv_handle = inst;
	return 0;

error_deinit:
	vpu_dec_deinit(&inst->vpu);
error_free_inst:
	kfree(inst);
	return err;
}

static int vdec_vp8_decode(void *h_vdec, struct mtk_vcodec_mem *bs,
			   struct vdec_fb *fb, bool *res_chg)
{
	struct vdec_vp8_inst *inst = (struct vdec_vp8_inst *)h_vdec;
	struct vdec_vp8_dec_info *dec = &inst->vsi->dec;
	struct vdec_vpu_inst *vpu = &inst->vpu;
	unsigned char *bs_va;
	unsigned int data;
	int err = 0;
	uint64_t y_fb_dma;
	uint64_t c_fb_dma;

	/* bs NULL means flush decoder */
	if (bs == NULL) {
		move_fb_list_use_to_free(inst);
		return vpu_dec_reset(vpu);
	}

	y_fb_dma = fb ? (u64)fb->base_y.dma_addr : 0;
	c_fb_dma = fb ? (u64)fb->base_c.dma_addr : 0;

	mtk_vcodec_debug(inst, "+ [%d] FB y_dma=%llx c_dma=%llx fb=%p",
			 inst->frm_cnt, y_fb_dma, c_fb_dma, fb);

	inst->cur_fb = fb;
	dec->bs_dma = (unsigned long)bs->dma_addr;
	dec->bs_sz = bs->size;
	dec->cur_y_fb_dma = y_fb_dma;
	dec->cur_c_fb_dma = c_fb_dma;

	mtk_vcodec_debug(inst, "\n + FRAME[%d] +\n", inst->frm_cnt);

	write_hw_segmentation_data(inst);
	enable_hw_rw_function(inst);
	store_dec_table(inst);

	bs_va = (unsigned char *)bs->va;

	/* retrieve width/hight and scale info from header */
	data = (*(bs_va + 9) << 24) | (*(bs_va + 8) << 16) |
	       (*(bs_va + 7) << 8) | *(bs_va + 6);
	err = vpu_dec_start(vpu, &data, 1);
	if (err) {
		add_fb_to_free_list(inst, fb);
		if (dec->wait_key_frame) {
			mtk_vcodec_debug(inst, "wait key frame !");
			return 0;
		}

		goto error;
	}

	if (dec->resolution_changed) {
		mtk_vcodec_debug(inst, "- resolution_changed -");
		*res_chg = true;
		add_fb_to_free_list(inst, fb);
		return 0;
	}

	/* wait decoder done interrupt */
	mtk_vcodec_wait_for_done_ctx(inst->ctx, MTK_INST_IRQ_RECEIVED,
				     WAIT_INTR_TIMEOUT_MS, 0);

	if (inst->vsi->load_data)
		load_dec_table(inst);

	vp8_dec_finish(inst);
	read_hw_segmentation_data(inst);

	err = vpu_dec_end(vpu);
	if (err)
		goto error;

	mtk_vcodec_debug(inst, "\n - FRAME[%d] - show=%d\n", inst->frm_cnt,
			 dec->show_frame);
	inst->frm_cnt++;
	*res_chg = false;
	return 0;

error:
	mtk_vcodec_err(inst, "\n - FRAME[%d] - err=%d\n", inst->frm_cnt, err);
	return err;
}

static void get_disp_fb(struct vdec_vp8_inst *inst, struct vdec_fb **out_fb)
{
	struct vdec_fb_node *node;
	struct vdec_fb *fb;

	node = list_first_entry_or_null(&inst->fb_disp_list,
					struct vdec_fb_node, list);
	if (node) {
		list_move_tail(&node->list, &inst->available_fb_node_list);
		fb = (struct vdec_fb *)node->fb;
		fb->status |= FB_ST_DISPLAY;
		mtk_vcodec_debug(inst, "[FB] get disp fb %p st=%d",
				 node->fb, fb->status);
	} else {
		fb = NULL;
		mtk_vcodec_debug(inst, "[FB] there is no disp fb");
	}

	*out_fb = fb;
}

static void get_free_fb(struct vdec_vp8_inst *inst, struct vdec_fb **out_fb)
{
	struct vdec_fb_node *node;
	struct vdec_fb *fb;

	node = list_first_entry_or_null(&inst->fb_free_list,
					struct vdec_fb_node, list);
	if (node) {
		list_move_tail(&node->list, &inst->available_fb_node_list);
		fb = (struct vdec_fb *)node->fb;
		fb->status |= FB_ST_FREE;
		mtk_vcodec_debug(inst, "[FB] get free fb %p st=%d",
				 node->fb, fb->status);
	} else {
		fb = NULL;
		mtk_vcodec_debug(inst, "[FB] there is no free fb");
	}

	*out_fb = fb;
}

static void get_crop_info(struct vdec_vp8_inst *inst, struct v4l2_rect *cr)
{
	cr->left = 0;
	cr->top = 0;
	cr->width = inst->vsi->pic.pic_w;
	cr->height = inst->vsi->pic.pic_h;
	mtk_vcodec_debug(inst, "get crop info l=%d, t=%d, w=%d, h=%d",
			 cr->left, cr->top, cr->width, cr->height);
}

static int vdec_vp8_get_param(void *h_vdec, enum vdec_get_param_type type,
			      void *out)
{
	struct vdec_vp8_inst *inst = (struct vdec_vp8_inst *)h_vdec;

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

	case GET_PARAM_CROP_INFO:
		get_crop_info(inst, out);
		break;

	case GET_PARAM_DPB_SIZE:
		*((unsigned int *)out) = VP8_DPB_SIZE;
		break;

	default:
		mtk_vcodec_err(inst, "invalid get parameter type=%d", type);
		return -EINVAL;
	}

	return 0;
}

static void vdec_vp8_deinit(void *h_vdec)
{
	struct vdec_vp8_inst *inst = (struct vdec_vp8_inst *)h_vdec;

	mtk_vcodec_debug_enter(inst);

	vpu_dec_deinit(&inst->vpu);
	free_working_buf(inst);
	kfree(inst);
}

const struct vdec_common_if vdec_vp8_if = {
	.init		= vdec_vp8_init,
	.decode		= vdec_vp8_decode,
	.get_param	= vdec_vp8_get_param,
	.deinit		= vdec_vp8_deinit,
};
