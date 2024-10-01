// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Daniel Hsiao <daniel.hsiao@mediatek.com>
 *         PoChun Lin <pochun.lin@mediatek.com>
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "../mtk_vcodec_drv.h"
#include "../mtk_vcodec_util.h"
#include "../mtk_vcodec_intr.h"
#include "../mtk_vcodec_enc.h"
#include "../mtk_vcodec_enc_pm.h"
#include "../venc_drv_base.h"
#include "../venc_ipi_msg.h"
#include "../venc_vpu_if.h"

#define VENC_BITSTREAM_FRAME_SIZE 0x0098
#define VENC_BITSTREAM_HEADER_LEN 0x00e8

/* This ac_tag is vp8 frame tag. */
#define MAX_AC_TAG_SIZE 10

/*
 * enum venc_vp8_vpu_work_buf - vp8 encoder buffer index
 */
enum venc_vp8_vpu_work_buf {
	VENC_VP8_VPU_WORK_BUF_LUMA,
	VENC_VP8_VPU_WORK_BUF_LUMA2,
	VENC_VP8_VPU_WORK_BUF_LUMA3,
	VENC_VP8_VPU_WORK_BUF_CHROMA,
	VENC_VP8_VPU_WORK_BUF_CHROMA2,
	VENC_VP8_VPU_WORK_BUF_CHROMA3,
	VENC_VP8_VPU_WORK_BUF_MV_INFO,
	VENC_VP8_VPU_WORK_BUF_BS_HEADER,
	VENC_VP8_VPU_WORK_BUF_PROB_BUF,
	VENC_VP8_VPU_WORK_BUF_RC_INFO,
	VENC_VP8_VPU_WORK_BUF_RC_CODE,
	VENC_VP8_VPU_WORK_BUF_RC_CODE2,
	VENC_VP8_VPU_WORK_BUF_RC_CODE3,
	VENC_VP8_VPU_WORK_BUF_MAX,
};

/*
 * struct venc_vp8_vpu_config - Structure for vp8 encoder configuration
 *                              AP-W/R : AP is writer/reader on this item
 *                              VPU-W/R: VPU is write/reader on this item
 * @input_fourcc: input fourcc
 * @bitrate: target bitrate (in bps)
 * @pic_w: picture width. Picture size is visible stream resolution, in pixels,
 *         to be used for display purposes; must be smaller or equal to buffer
 *         size.
 * @pic_h: picture height
 * @buf_w: buffer width (with 16 alignment). Buffer size is stream resolution
 *         in pixels aligned to hardware requirements.
 * @buf_h: buffer height (with 16 alignment)
 * @gop_size: group of picture size (key frame)
 * @framerate: frame rate in fps
 * @ts_mode: temporal scalability mode (0: disable, 1: enable)
 *	     support three temporal layers - 0: 7.5fps 1: 7.5fps 2: 15fps.
 */
struct venc_vp8_vpu_config {
	u32 input_fourcc;
	u32 bitrate;
	u32 pic_w;
	u32 pic_h;
	u32 buf_w;
	u32 buf_h;
	u32 gop_size;
	u32 framerate;
	u32 ts_mode;
};

/*
 * struct venc_vp8_vpu_buf - Structure for buffer information
 *                           AP-W/R : AP is writer/reader on this item
 *                           VPU-W/R: VPU is write/reader on this item
 * @iova: IO virtual address
 * @vpua: VPU side memory addr which is used by RC_CODE
 * @size: buffer size (in bytes)
 */
struct venc_vp8_vpu_buf {
	u32 iova;
	u32 vpua;
	u32 size;
};

/*
 * struct venc_vp8_vsi - Structure for VPU driver control and info share
 *                       AP-W/R : AP is writer/reader on this item
 *                       VPU-W/R: VPU is write/reader on this item
 * This structure is allocated in VPU side and shared to AP side.
 * @config: vp8 encoder configuration
 * @work_bufs: working buffer information in VPU side
 * The work_bufs here is for storing the 'size' info shared to AP side.
 * The similar item in struct venc_vp8_inst is for memory allocation
 * in AP side. The AP driver will copy the 'size' from here to the one in
 * struct mtk_vcodec_mem, then invoke mtk_vcodec_mem_alloc to allocate
 * the buffer. After that, bypass the 'dma_addr' to the 'iova' field here for
 * register setting in VPU side.
 */
struct venc_vp8_vsi {
	struct venc_vp8_vpu_config config;
	struct venc_vp8_vpu_buf work_bufs[VENC_VP8_VPU_WORK_BUF_MAX];
};

/*
 * struct venc_vp8_inst - vp8 encoder AP driver instance
 * @hw_base: vp8 encoder hardware register base
 * @work_bufs: working buffer
 * @work_buf_allocated: working buffer allocated flag
 * @frm_cnt: encoded frame count, it's used for I-frame judgement and
 *	     reset when force intra cmd received.
 * @ts_mode: temporal scalability mode (0: disable, 1: enable)
 *	     support three temporal layers - 0: 7.5fps 1: 7.5fps 2: 15fps.
 * @vpu_inst: VPU instance to exchange information between AP and VPU
 * @vsi: driver structure allocated by VPU side and shared to AP side for
 *	 control and info share
 * @ctx: context for v4l2 layer integration
 */
struct venc_vp8_inst {
	void __iomem *hw_base;
	struct mtk_vcodec_mem work_bufs[VENC_VP8_VPU_WORK_BUF_MAX];
	bool work_buf_allocated;
	unsigned int frm_cnt;
	unsigned int ts_mode;
	struct venc_vpu_inst vpu_inst;
	struct venc_vp8_vsi *vsi;
	struct mtk_vcodec_ctx *ctx;
};

static inline u32 vp8_enc_read_reg(struct venc_vp8_inst *inst, u32 addr)
{
	return readl(inst->hw_base + addr);
}

static void vp8_enc_free_work_buf(struct venc_vp8_inst *inst)
{
	int i;

	mtk_vcodec_debug_enter(inst);

	/* Buffers need to be freed by AP. */
	for (i = 0; i < VENC_VP8_VPU_WORK_BUF_MAX; i++) {
		if (inst->work_bufs[i].size == 0)
			continue;
		mtk_vcodec_mem_free(inst->ctx, &inst->work_bufs[i]);
	}

	mtk_vcodec_debug_leave(inst);
}

static int vp8_enc_alloc_work_buf(struct venc_vp8_inst *inst)
{
	int i;
	int ret = 0;
	struct venc_vp8_vpu_buf *wb = inst->vsi->work_bufs;

	mtk_vcodec_debug_enter(inst);

	for (i = 0; i < VENC_VP8_VPU_WORK_BUF_MAX; i++) {
		if (wb[i].size == 0)
			continue;
		/*
		 * This 'wb' structure is set by VPU side and shared to AP for
		 * buffer allocation and IO virtual addr mapping. For most of
		 * the buffers, AP will allocate the buffer according to 'size'
		 * field and store the IO virtual addr in 'iova' field. For the
		 * RC_CODEx buffers, they are pre-allocated in the VPU side
		 * because they are inside VPU SRAM, and save the VPU addr in
		 * the 'vpua' field. The AP will translate the VPU addr to the
		 * corresponding IO virtual addr and store in 'iova' field.
		 */
		inst->work_bufs[i].size = wb[i].size;
		ret = mtk_vcodec_mem_alloc(inst->ctx, &inst->work_bufs[i]);
		if (ret) {
			mtk_vcodec_err(inst,
				       "cannot alloc work_bufs[%d]", i);
			goto err_alloc;
		}
		/*
		 * This RC_CODEx is pre-allocated by VPU and saved in VPU addr.
		 * So we need use memcpy to copy RC_CODEx from VPU addr into IO
		 * virtual addr in 'iova' field for reg setting in VPU side.
		 */
		if (i == VENC_VP8_VPU_WORK_BUF_RC_CODE ||
		    i == VENC_VP8_VPU_WORK_BUF_RC_CODE2 ||
		    i == VENC_VP8_VPU_WORK_BUF_RC_CODE3) {
			struct mtk_vcodec_fw *handler;
			void *tmp_va;

			handler = inst->vpu_inst.ctx->dev->fw_handler;
			tmp_va = mtk_vcodec_fw_map_dm_addr(handler,
							   wb[i].vpua);
			memcpy(inst->work_bufs[i].va, tmp_va, wb[i].size);
		}
		wb[i].iova = inst->work_bufs[i].dma_addr;

		mtk_vcodec_debug(inst,
				 "work_bufs[%d] va=0x%p,iova=%pad,size=%zu",
				 i, inst->work_bufs[i].va,
				 &inst->work_bufs[i].dma_addr,
				 inst->work_bufs[i].size);
	}

	mtk_vcodec_debug_leave(inst);

	return ret;

err_alloc:
	vp8_enc_free_work_buf(inst);

	return ret;
}

static unsigned int vp8_enc_wait_venc_done(struct venc_vp8_inst *inst)
{
	unsigned int irq_status = 0;
	struct mtk_vcodec_ctx *ctx = (struct mtk_vcodec_ctx *)inst->ctx;

	if (!mtk_vcodec_wait_for_done_ctx(ctx, MTK_INST_IRQ_RECEIVED,
					  WAIT_INTR_TIMEOUT_MS, 0)) {
		irq_status = ctx->irq_status;
		mtk_vcodec_debug(inst, "isr return %x", irq_status);
	}
	return irq_status;
}

/*
 * Compose ac_tag, bitstream header and bitstream payload into
 * one bitstream buffer.
 */
static int vp8_enc_compose_one_frame(struct venc_vp8_inst *inst,
				     struct mtk_vcodec_mem *bs_buf,
				     unsigned int *bs_size)
{
	unsigned int not_key;
	u32 bs_frm_size;
	u32 bs_hdr_len;
	unsigned int ac_tag_size;
	u8 ac_tag[MAX_AC_TAG_SIZE];
	u32 tag;

	bs_frm_size = vp8_enc_read_reg(inst, VENC_BITSTREAM_FRAME_SIZE);
	bs_hdr_len = vp8_enc_read_reg(inst, VENC_BITSTREAM_HEADER_LEN);

	/* if a frame is key frame, not_key is 0 */
	not_key = !inst->vpu_inst.is_key_frm;
	tag = (bs_hdr_len << 5) | 0x10 | not_key;
	ac_tag[0] = tag & 0xff;
	ac_tag[1] = (tag >> 8) & 0xff;
	ac_tag[2] = (tag >> 16) & 0xff;

	/* key frame */
	if (not_key == 0) {
		ac_tag_size = MAX_AC_TAG_SIZE;
		ac_tag[3] = 0x9d;
		ac_tag[4] = 0x01;
		ac_tag[5] = 0x2a;
		ac_tag[6] = inst->vsi->config.pic_w;
		ac_tag[7] = inst->vsi->config.pic_w >> 8;
		ac_tag[8] = inst->vsi->config.pic_h;
		ac_tag[9] = inst->vsi->config.pic_h >> 8;
	} else {
		ac_tag_size = 3;
	}

	if (bs_buf->size < bs_hdr_len + bs_frm_size + ac_tag_size) {
		mtk_vcodec_err(inst, "bitstream buf size is too small(%zu)",
			       bs_buf->size);
		return -EINVAL;
	}

	/*
	* (1) The vp8 bitstream header and body are generated by the HW vp8
	* encoder separately at the same time. We cannot know the bitstream
	* header length in advance.
	* (2) From the vp8 spec, there is no stuffing byte allowed between the
	* ac tag, bitstream header and bitstream body.
	*/
	memmove(bs_buf->va + bs_hdr_len + ac_tag_size,
		bs_buf->va, bs_frm_size);
	memcpy(bs_buf->va + ac_tag_size,
	       inst->work_bufs[VENC_VP8_VPU_WORK_BUF_BS_HEADER].va,
	       bs_hdr_len);
	memcpy(bs_buf->va, ac_tag, ac_tag_size);
	*bs_size = bs_frm_size + bs_hdr_len + ac_tag_size;

	return 0;
}

static int vp8_enc_encode_frame(struct venc_vp8_inst *inst,
				struct venc_frm_buf *frm_buf,
				struct mtk_vcodec_mem *bs_buf,
				unsigned int *bs_size)
{
	int ret = 0;
	unsigned int irq_status;

	mtk_vcodec_debug(inst, "->frm_cnt=%d", inst->frm_cnt);

	ret = vpu_enc_encode(&inst->vpu_inst, 0, frm_buf, bs_buf, NULL);
	if (ret)
		return ret;

	irq_status = vp8_enc_wait_venc_done(inst);
	if (irq_status != MTK_VENC_IRQ_STATUS_FRM) {
		mtk_vcodec_err(inst, "irq_status=%d failed", irq_status);
		return -EIO;
	}

	if (vp8_enc_compose_one_frame(inst, bs_buf, bs_size)) {
		mtk_vcodec_err(inst, "vp8_enc_compose_one_frame failed");
		return -EINVAL;
	}

	inst->frm_cnt++;
	mtk_vcodec_debug(inst, "<-size=%d key_frm=%d", *bs_size,
			 inst->vpu_inst.is_key_frm);

	return ret;
}

static int vp8_enc_init(struct mtk_vcodec_ctx *ctx)
{
	int ret = 0;
	struct venc_vp8_inst *inst;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	inst->ctx = ctx;
	inst->vpu_inst.ctx = ctx;
	inst->vpu_inst.id = IPI_VENC_VP8;
	inst->hw_base = mtk_vcodec_get_reg_addr(inst->ctx, VENC_LT_SYS);

	mtk_vcodec_debug_enter(inst);

	ret = vpu_enc_init(&inst->vpu_inst);

	inst->vsi = (struct venc_vp8_vsi *)inst->vpu_inst.vsi;

	mtk_vcodec_debug_leave(inst);

	if (ret)
		kfree(inst);
	else
		ctx->drv_handle = inst;

	return ret;
}

static int vp8_enc_encode(void *handle,
			  enum venc_start_opt opt,
			  struct venc_frm_buf *frm_buf,
			  struct mtk_vcodec_mem *bs_buf,
			  struct venc_done_result *result)
{
	int ret = 0;
	struct venc_vp8_inst *inst = (struct venc_vp8_inst *)handle;
	struct mtk_vcodec_ctx *ctx = inst->ctx;

	mtk_vcodec_debug_enter(inst);

	enable_irq(ctx->dev->enc_irq);

	switch (opt) {
	case VENC_START_OPT_ENCODE_FRAME:
		ret = vp8_enc_encode_frame(inst, frm_buf, bs_buf,
					   &result->bs_size);
		if (ret)
			goto encode_err;
		result->is_key_frm = inst->vpu_inst.is_key_frm;
		break;

	default:
		mtk_vcodec_err(inst, "opt not support:%d", opt);
		ret = -EINVAL;
		break;
	}

encode_err:

	disable_irq(ctx->dev->enc_irq);
	mtk_vcodec_debug_leave(inst);

	return ret;
}

static int vp8_enc_set_param(void *handle,
			     enum venc_set_param_type type,
			     struct venc_enc_param *enc_prm)
{
	int ret = 0;
	struct venc_vp8_inst *inst = (struct venc_vp8_inst *)handle;

	mtk_vcodec_debug(inst, "->type=%d", type);

	switch (type) {
	case VENC_SET_PARAM_ENC:
		inst->vsi->config.input_fourcc = enc_prm->input_yuv_fmt;
		inst->vsi->config.bitrate = enc_prm->bitrate;
		inst->vsi->config.pic_w = enc_prm->width;
		inst->vsi->config.pic_h = enc_prm->height;
		inst->vsi->config.buf_w = enc_prm->buf_width;
		inst->vsi->config.buf_h = enc_prm->buf_height;
		inst->vsi->config.gop_size = enc_prm->gop_size;
		inst->vsi->config.framerate = enc_prm->frm_rate;
		inst->vsi->config.ts_mode = inst->ts_mode;
		ret = vpu_enc_set_param(&inst->vpu_inst, type, enc_prm);
		if (ret)
			break;
		if (inst->work_buf_allocated) {
			vp8_enc_free_work_buf(inst);
			inst->work_buf_allocated = false;
		}
		ret = vp8_enc_alloc_work_buf(inst);
		if (ret)
			break;
		inst->work_buf_allocated = true;
		break;

	/*
	 * VENC_SET_PARAM_TS_MODE must be called before VENC_SET_PARAM_ENC
	 */
	case VENC_SET_PARAM_TS_MODE:
		inst->ts_mode = 1;
		mtk_vcodec_debug(inst, "set ts_mode");
		break;

	default:
		ret = vpu_enc_set_param(&inst->vpu_inst, type, enc_prm);
		break;
	}

	mtk_vcodec_debug_leave(inst);

	return ret;
}

static int vp8_enc_deinit(void *handle)
{
	int ret = 0;
	struct venc_vp8_inst *inst = (struct venc_vp8_inst *)handle;

	mtk_vcodec_debug_enter(inst);

	ret = vpu_enc_deinit(&inst->vpu_inst);

	if (inst->work_buf_allocated)
		vp8_enc_free_work_buf(inst);

	mtk_vcodec_debug_leave(inst);
	kfree(inst);

	return ret;
}

const struct venc_common_if venc_vp8_if = {
	.init = vp8_enc_init,
	.encode = vp8_enc_encode,
	.set_param = vp8_enc_set_param,
	.deinit = vp8_enc_deinit,
};
