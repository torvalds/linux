// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Jungchang Tsao <jungchang.tsao@mediatek.com>
 *         Daniel Hsiao <daniel.hsiao@mediatek.com>
 *         PoChun Lin <pochun.lin@mediatek.com>
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "../mtk_vcodec_enc_drv.h"
#include "../../common/mtk_vcodec_intr.h"
#include "../mtk_vcodec_enc.h"
#include "../mtk_vcodec_enc_pm.h"
#include "../venc_drv_base.h"
#include "../venc_ipi_msg.h"
#include "../venc_vpu_if.h"

static const char h264_filler_marker[] = {0x0, 0x0, 0x0, 0x1, 0xc};

#define H264_FILLER_MARKER_SIZE ARRAY_SIZE(h264_filler_marker)
#define VENC_PIC_BITSTREAM_BYTE_CNT 0x0098

/*
 * enum venc_h264_frame_type - h264 encoder output bitstream frame type
 */
enum venc_h264_frame_type {
	VENC_H264_IDR_FRM,
	VENC_H264_I_FRM,
	VENC_H264_P_FRM,
	VENC_H264_B_FRM,
};

/*
 * enum venc_h264_vpu_work_buf - h264 encoder buffer index
 */
enum venc_h264_vpu_work_buf {
	VENC_H264_VPU_WORK_BUF_RC_INFO,
	VENC_H264_VPU_WORK_BUF_RC_CODE,
	VENC_H264_VPU_WORK_BUF_REC_LUMA,
	VENC_H264_VPU_WORK_BUF_REC_CHROMA,
	VENC_H264_VPU_WORK_BUF_REF_LUMA,
	VENC_H264_VPU_WORK_BUF_REF_CHROMA,
	VENC_H264_VPU_WORK_BUF_MV_INFO_1,
	VENC_H264_VPU_WORK_BUF_MV_INFO_2,
	VENC_H264_VPU_WORK_BUF_SKIP_FRAME,
	VENC_H264_VPU_WORK_BUF_MAX,
};

/*
 * enum venc_h264_bs_mode - for bs_mode argument in h264_enc_vpu_encode
 */
enum venc_h264_bs_mode {
	H264_BS_MODE_SPS,
	H264_BS_MODE_PPS,
	H264_BS_MODE_FRAME,
};

/*
 * struct venc_h264_vpu_config - Structure for h264 encoder configuration
 *                               AP-W/R : AP is writer/reader on this item
 *                               VPU-W/R: VPU is write/reader on this item
 * @input_fourcc: input fourcc
 * @bitrate: target bitrate (in bps)
 * @pic_w: picture width. Picture size is visible stream resolution, in pixels,
 *         to be used for display purposes; must be smaller or equal to buffer
 *         size.
 * @pic_h: picture height
 * @buf_w: buffer width. Buffer size is stream resolution in pixels aligned to
 *         hardware requirements.
 * @buf_h: buffer height
 * @gop_size: group of picture size (idr frame)
 * @intra_period: intra frame period
 * @framerate: frame rate in fps
 * @profile: as specified in standard
 * @level: as specified in standard
 * @wfd: WFD mode 1:on, 0:off
 */
struct venc_h264_vpu_config {
	u32 input_fourcc;
	u32 bitrate;
	u32 pic_w;
	u32 pic_h;
	u32 buf_w;
	u32 buf_h;
	u32 gop_size;
	u32 intra_period;
	u32 framerate;
	u32 profile;
	u32 level;
	u32 wfd;
};

/*
 * struct venc_h264_vpu_buf - Structure for buffer information
 *                            AP-W/R : AP is writer/reader on this item
 *                            VPU-W/R: VPU is write/reader on this item
 * @iova: IO virtual address
 * @vpua: VPU side memory addr which is used by RC_CODE
 * @size: buffer size (in bytes)
 */
struct venc_h264_vpu_buf {
	u32 iova;
	u32 vpua;
	u32 size;
};

/*
 * struct venc_h264_vsi - Structure for VPU driver control and info share
 *                        AP-W/R : AP is writer/reader on this item
 *                        VPU-W/R: VPU is write/reader on this item
 * This structure is allocated in VPU side and shared to AP side.
 * @config: h264 encoder configuration
 * @work_bufs: working buffer information in VPU side
 * The work_bufs here is for storing the 'size' info shared to AP side.
 * The similar item in struct venc_h264_inst is for memory allocation
 * in AP side. The AP driver will copy the 'size' from here to the one in
 * struct mtk_vcodec_mem, then invoke mtk_vcodec_mem_alloc to allocate
 * the buffer. After that, bypass the 'dma_addr' to the 'iova' field here for
 * register setting in VPU side.
 */
struct venc_h264_vsi {
	struct venc_h264_vpu_config config;
	struct venc_h264_vpu_buf work_bufs[VENC_H264_VPU_WORK_BUF_MAX];
};

/**
 * struct venc_h264_vpu_config_ext - Structure for h264 encoder configuration
 *                                   AP-W/R : AP is writer/reader on this item
 *                                   VPU-W/R: VPU is write/reader on this item
 * @input_fourcc: input fourcc
 * @bitrate: target bitrate (in bps)
 * @pic_w: picture width. Picture size is visible stream resolution, in pixels,
 *         to be used for display purposes; must be smaller or equal to buffer
 *         size.
 * @pic_h: picture height
 * @buf_w: buffer width. Buffer size is stream resolution in pixels aligned to
 *         hardware requirements.
 * @buf_h: buffer height
 * @gop_size: group of picture size (idr frame)
 * @intra_period: intra frame period
 * @framerate: frame rate in fps
 * @profile: as specified in standard
 * @level: as specified in standard
 * @wfd: WFD mode 1:on, 0:off
 * @max_qp: max quant parameter
 * @min_qp: min quant parameter
 * @reserved: reserved configs
 */
struct venc_h264_vpu_config_ext {
	u32 input_fourcc;
	u32 bitrate;
	u32 pic_w;
	u32 pic_h;
	u32 buf_w;
	u32 buf_h;
	u32 gop_size;
	u32 intra_period;
	u32 framerate;
	u32 profile;
	u32 level;
	u32 wfd;
	u32 max_qp;
	u32 min_qp;
	u32 reserved[8];
};

/**
 * struct venc_h264_vpu_buf_34 - Structure for 34-bit buffer information
 *                               AP-W/R : AP is writer/reader on this item
 *                               VPU-W/R: VPU is write/reader on this item
 * @iova: 34-bit IO virtual address
 * @vpua: VPU side memory addr which is used by RC_CODE
 * @size: buffer size (in bytes)
 */
struct venc_h264_vpu_buf_34 {
	u64 iova;
	u32 vpua;
	u32 size;
};

/**
 * struct venc_h264_vsi_34 - Structure for VPU driver control and info share
 *                           Used for 34-bit iova sharing
 * @config: h264 encoder configuration
 * @work_bufs: working buffer information in VPU side
 */
struct venc_h264_vsi_34 {
	struct venc_h264_vpu_config_ext config;
	struct venc_h264_vpu_buf_34 work_bufs[VENC_H264_VPU_WORK_BUF_MAX];
};

/*
 * struct venc_h264_inst - h264 encoder AP driver instance
 * @hw_base: h264 encoder hardware register base
 * @work_bufs: working buffer
 * @pps_buf: buffer to store the pps bitstream
 * @work_buf_allocated: working buffer allocated flag
 * @frm_cnt: encoded frame count
 * @prepend_hdr: when the v4l2 layer send VENC_SET_PARAM_PREPEND_HEADER cmd
 *  through h264_enc_set_param interface, it will set this flag and prepend the
 *  sps/pps in h264_enc_encode function.
 * @vpu_inst: VPU instance to exchange information between AP and VPU
 * @vsi: driver structure allocated by VPU side and shared to AP side for
 *	 control and info share
 * @vsi_34: driver structure allocated by VPU side and shared to AP side for
 *	 control and info share, used for 34-bit iova sharing.
 * @ctx: context for v4l2 layer integration
 */
struct venc_h264_inst {
	void __iomem *hw_base;
	struct mtk_vcodec_mem work_bufs[VENC_H264_VPU_WORK_BUF_MAX];
	struct mtk_vcodec_mem pps_buf;
	bool work_buf_allocated;
	unsigned int frm_cnt;
	unsigned int skip_frm_cnt;
	unsigned int prepend_hdr;
	struct venc_vpu_inst vpu_inst;
	struct venc_h264_vsi *vsi;
	struct venc_h264_vsi_34 *vsi_34;
	struct mtk_vcodec_enc_ctx *ctx;
};

static inline u32 h264_read_reg(struct venc_h264_inst *inst, u32 addr)
{
	return readl(inst->hw_base + addr);
}

static unsigned int h264_get_profile(struct venc_h264_inst *inst,
				     unsigned int profile)
{
	switch (profile) {
	case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
		return 66;
	case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
		return 77;
	case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
		return 100;
	case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE:
		mtk_venc_err(inst->ctx, "unsupported CONSTRAINED_BASELINE");
		return 0;
	case V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED:
		mtk_venc_err(inst->ctx, "unsupported EXTENDED");
		return 0;
	default:
		mtk_venc_debug(inst->ctx, "unsupported profile %d", profile);
		return 100;
	}
}

static unsigned int h264_get_level(struct venc_h264_inst *inst,
				   unsigned int level)
{
	switch (level) {
	case V4L2_MPEG_VIDEO_H264_LEVEL_1B:
		mtk_venc_err(inst->ctx, "unsupported 1B");
		return 0;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_0:
		return 10;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_1:
		return 11;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_2:
		return 12;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_3:
		return 13;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_0:
		return 20;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_1:
		return 21;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_2:
		return 22;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_0:
		return 30;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_1:
		return 31;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_2:
		return 32;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_0:
		return 40;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_1:
		return 41;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_2:
		return 42;
	case V4L2_MPEG_VIDEO_H264_LEVEL_5_0:
		return 50;
	case V4L2_MPEG_VIDEO_H264_LEVEL_5_1:
		return 51;
	default:
		mtk_venc_debug(inst->ctx, "unsupported level %d", level);
		return 31;
	}
}

static void h264_enc_free_work_buf(struct venc_h264_inst *inst)
{
	int i;

	/* Except the SKIP_FRAME buffers,
	 * other buffers need to be freed by AP.
	 */
	for (i = 0; i < VENC_H264_VPU_WORK_BUF_MAX; i++) {
		if (i != VENC_H264_VPU_WORK_BUF_SKIP_FRAME)
			mtk_vcodec_mem_free(inst->ctx, &inst->work_bufs[i]);
	}

	mtk_vcodec_mem_free(inst->ctx, &inst->pps_buf);
}

static int h264_enc_alloc_work_buf(struct venc_h264_inst *inst, bool is_34bit)
{
	struct venc_h264_vpu_buf *wb = NULL;
	struct venc_h264_vpu_buf_34 *wb_34 = NULL;
	int i;
	u32 vpua, wb_size;
	int ret = 0;

	if (is_34bit)
		wb_34 = inst->vsi_34->work_bufs;
	else
		wb = inst->vsi->work_bufs;

	for (i = 0; i < VENC_H264_VPU_WORK_BUF_MAX; i++) {
		/*
		 * This 'wb' structure is set by VPU side and shared to AP for
		 * buffer allocation and IO virtual addr mapping. For most of
		 * the buffers, AP will allocate the buffer according to 'size'
		 * field and store the IO virtual addr in 'iova' field. There
		 * are two exceptions:
		 * (1) RC_CODE buffer, it's pre-allocated in the VPU side, and
		 * save the VPU addr in the 'vpua' field. The AP will translate
		 * the VPU addr to the corresponding IO virtual addr and store
		 * in 'iova' field for reg setting in VPU side.
		 * (2) SKIP_FRAME buffer, it's pre-allocated in the VPU side,
		 * and save the VPU addr in the 'vpua' field. The AP will
		 * translate the VPU addr to the corresponding AP side virtual
		 * address and do some memcpy access to move to bitstream buffer
		 * assigned by v4l2 layer.
		 */
		if (is_34bit) {
			inst->work_bufs[i].size = wb_34[i].size;
			vpua = wb_34[i].vpua;
			wb_size = wb_34[i].size;
		} else {
			inst->work_bufs[i].size = wb[i].size;
			vpua = wb[i].vpua;
			wb_size = wb[i].size;
		}

		if (i == VENC_H264_VPU_WORK_BUF_SKIP_FRAME) {
			struct mtk_vcodec_fw *handler;

			handler = inst->vpu_inst.ctx->dev->fw_handler;
			inst->work_bufs[i].va =
				mtk_vcodec_fw_map_dm_addr(handler, vpua);
			inst->work_bufs[i].dma_addr = 0;
		} else {
			ret = mtk_vcodec_mem_alloc(inst->ctx,
						   &inst->work_bufs[i]);
			if (ret) {
				mtk_venc_err(inst->ctx, "cannot allocate buf %d", i);
				goto err_alloc;
			}
			/*
			 * This RC_CODE is pre-allocated by VPU and saved in VPU
			 * addr. So we need use memcpy to copy RC_CODE from VPU
			 * addr into IO virtual addr in 'iova' field for reg
			 * setting in VPU side.
			 */
			if (i == VENC_H264_VPU_WORK_BUF_RC_CODE) {
				struct mtk_vcodec_fw *handler;
				void *tmp_va;

				handler = inst->vpu_inst.ctx->dev->fw_handler;
				tmp_va = mtk_vcodec_fw_map_dm_addr(handler,
								   vpua);
				memcpy(inst->work_bufs[i].va, tmp_va, wb_size);
			}
		}
		if (is_34bit)
			wb_34[i].iova = inst->work_bufs[i].dma_addr;
		else
			wb[i].iova = inst->work_bufs[i].dma_addr;

		mtk_venc_debug(inst->ctx, "work_buf[%d] va=0x%p iova=%pad size=%zu",
			       i, inst->work_bufs[i].va,
			       &inst->work_bufs[i].dma_addr,
			       inst->work_bufs[i].size);
	}

	/* the pps_buf is used by AP side only */
	inst->pps_buf.size = 128;
	ret = mtk_vcodec_mem_alloc(inst->ctx, &inst->pps_buf);
	if (ret) {
		mtk_venc_err(inst->ctx, "cannot allocate pps_buf");
		goto err_alloc;
	}

	return ret;

err_alloc:
	h264_enc_free_work_buf(inst);

	return ret;
}

static unsigned int h264_enc_wait_venc_done(struct venc_h264_inst *inst)
{
	unsigned int irq_status = 0;
	struct mtk_vcodec_enc_ctx *ctx = (struct mtk_vcodec_enc_ctx *)inst->ctx;

	if (!mtk_vcodec_wait_for_done_ctx(ctx, MTK_INST_IRQ_RECEIVED,
					  WAIT_INTR_TIMEOUT_MS, 0)) {
		irq_status = ctx->irq_status;
		mtk_venc_debug(ctx, "irq_status %x <-", irq_status);
	}
	return irq_status;
}

static int h264_frame_type(unsigned int frm_cnt, unsigned int gop_size,
			   unsigned int intra_period)
{
	if ((gop_size != 0 && (frm_cnt % gop_size) == 0) ||
	    (frm_cnt == 0 && gop_size == 0)) {
		/* IDR frame */
		return VENC_H264_IDR_FRM;
	} else if ((intra_period != 0 && (frm_cnt % intra_period) == 0) ||
		   (frm_cnt == 0 && intra_period == 0)) {
		/* I frame */
		return VENC_H264_I_FRM;
	} else {
		return VENC_H264_P_FRM;  /* Note: B frames are not supported */
	}
}

static int h264_encode_sps(struct venc_h264_inst *inst,
			   struct mtk_vcodec_mem *bs_buf,
			   unsigned int *bs_size)
{
	int ret = 0;
	unsigned int irq_status;

	ret = vpu_enc_encode(&inst->vpu_inst, H264_BS_MODE_SPS, NULL, bs_buf, NULL);
	if (ret)
		return ret;

	irq_status = h264_enc_wait_venc_done(inst);
	if (irq_status != MTK_VENC_IRQ_STATUS_SPS) {
		mtk_venc_err(inst->ctx, "expect irq status %d", MTK_VENC_IRQ_STATUS_SPS);
		return -EINVAL;
	}

	*bs_size = h264_read_reg(inst, VENC_PIC_BITSTREAM_BYTE_CNT);
	mtk_venc_debug(inst->ctx, "bs size %d <-", *bs_size);

	return ret;
}

static int h264_encode_pps(struct venc_h264_inst *inst,
			   struct mtk_vcodec_mem *bs_buf,
			   unsigned int *bs_size)
{
	int ret = 0;
	unsigned int irq_status;

	ret = vpu_enc_encode(&inst->vpu_inst, H264_BS_MODE_PPS, NULL, bs_buf, NULL);
	if (ret)
		return ret;

	irq_status = h264_enc_wait_venc_done(inst);
	if (irq_status != MTK_VENC_IRQ_STATUS_PPS) {
		mtk_venc_err(inst->ctx, "expect irq status %d", MTK_VENC_IRQ_STATUS_PPS);
		return -EINVAL;
	}

	*bs_size = h264_read_reg(inst, VENC_PIC_BITSTREAM_BYTE_CNT);
	mtk_venc_debug(inst->ctx, "bs size %d <-", *bs_size);

	return ret;
}

static int h264_encode_header(struct venc_h264_inst *inst,
			      struct mtk_vcodec_mem *bs_buf,
			      unsigned int *bs_size)
{
	int ret = 0;
	unsigned int bs_size_sps;
	unsigned int bs_size_pps;

	ret = h264_encode_sps(inst, bs_buf, &bs_size_sps);
	if (ret)
		return ret;

	ret = h264_encode_pps(inst, &inst->pps_buf, &bs_size_pps);
	if (ret)
		return ret;

	memcpy(bs_buf->va + bs_size_sps, inst->pps_buf.va, bs_size_pps);
	*bs_size = bs_size_sps + bs_size_pps;

	return ret;
}

static int h264_encode_frame(struct venc_h264_inst *inst,
			     struct venc_frm_buf *frm_buf,
			     struct mtk_vcodec_mem *bs_buf,
			     unsigned int *bs_size)
{
	int ret = 0;
	unsigned int gop_size;
	unsigned int intra_period;
	unsigned int irq_status;
	struct venc_frame_info frame_info;
	struct mtk_vcodec_enc_ctx *ctx = inst->ctx;

	mtk_venc_debug(ctx, "frm_cnt = %d\n ", inst->frm_cnt);

	if (MTK_ENC_IOVA_IS_34BIT(ctx)) {
		gop_size = inst->vsi_34->config.gop_size;
		intra_period = inst->vsi_34->config.intra_period;
	} else {
		gop_size = inst->vsi->config.gop_size;
		intra_period = inst->vsi->config.intra_period;
	}
	frame_info.frm_count = inst->frm_cnt;
	frame_info.skip_frm_count = inst->skip_frm_cnt;
	frame_info.frm_type = h264_frame_type(inst->frm_cnt, gop_size,
					      intra_period);
	mtk_venc_debug(ctx, "frm_count = %d,skip_frm_count =%d,frm_type=%d.\n",
		       frame_info.frm_count, frame_info.skip_frm_count,
		       frame_info.frm_type);

	ret = vpu_enc_encode(&inst->vpu_inst, H264_BS_MODE_FRAME,
			     frm_buf, bs_buf, &frame_info);
	if (ret)
		return ret;

	/*
	 * skip frame case: The skip frame buffer is composed by vpu side only,
	 * it does not trigger the hw, so skip the wait interrupt operation.
	 */
	if (inst->vpu_inst.state == VEN_IPI_MSG_ENC_STATE_SKIP) {
		*bs_size = inst->vpu_inst.bs_size;
		memcpy(bs_buf->va,
		       inst->work_bufs[VENC_H264_VPU_WORK_BUF_SKIP_FRAME].va,
		       *bs_size);
		++inst->frm_cnt;
		++inst->skip_frm_cnt;
		return 0;
	}

	irq_status = h264_enc_wait_venc_done(inst);
	if (irq_status != MTK_VENC_IRQ_STATUS_FRM) {
		mtk_venc_err(ctx, "irq_status=%d failed", irq_status);
		return -EIO;
	}

	*bs_size = h264_read_reg(inst, VENC_PIC_BITSTREAM_BYTE_CNT);

	++inst->frm_cnt;
	mtk_venc_debug(ctx, "frm %d bs_size %d key_frm %d <-",
		       inst->frm_cnt, *bs_size, inst->vpu_inst.is_key_frm);

	return 0;
}

static void h264_encode_filler(struct venc_h264_inst *inst, void *buf,
			       int size)
{
	unsigned char *p = buf;

	if (size < H264_FILLER_MARKER_SIZE) {
		mtk_venc_err(inst->ctx, "filler size too small %d", size);
		return;
	}

	memcpy(p, h264_filler_marker, ARRAY_SIZE(h264_filler_marker));
	size -= H264_FILLER_MARKER_SIZE;
	p += H264_FILLER_MARKER_SIZE;
	memset(p, 0xff, size);
}

static int h264_enc_init(struct mtk_vcodec_enc_ctx *ctx)
{
	const bool is_ext = MTK_ENC_CTX_IS_EXT(ctx);
	int ret = 0;
	struct venc_h264_inst *inst;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	inst->ctx = ctx;
	inst->vpu_inst.ctx = ctx;
	inst->vpu_inst.id = is_ext ? SCP_IPI_VENC_H264 : IPI_VENC_H264;
	inst->hw_base = mtk_vcodec_get_reg_addr(inst->ctx->dev->reg_base, VENC_SYS);

	ret = vpu_enc_init(&inst->vpu_inst);

	if (MTK_ENC_IOVA_IS_34BIT(ctx))
		inst->vsi_34 = (struct venc_h264_vsi_34 *)inst->vpu_inst.vsi;
	else
		inst->vsi = (struct venc_h264_vsi *)inst->vpu_inst.vsi;

	if (ret)
		kfree(inst);
	else
		ctx->drv_handle = inst;

	return ret;
}

static int h264_enc_encode(void *handle,
			   enum venc_start_opt opt,
			   struct venc_frm_buf *frm_buf,
			   struct mtk_vcodec_mem *bs_buf,
			   struct venc_done_result *result)
{
	int ret = 0;
	struct venc_h264_inst *inst = (struct venc_h264_inst *)handle;
	struct mtk_vcodec_enc_ctx *ctx = inst->ctx;

	mtk_venc_debug(ctx, "opt %d ->", opt);

	enable_irq(ctx->dev->enc_irq);

	switch (opt) {
	case VENC_START_OPT_ENCODE_SEQUENCE_HEADER: {
		unsigned int bs_size_hdr;

		ret = h264_encode_header(inst, bs_buf, &bs_size_hdr);
		if (ret)
			goto encode_err;

		result->bs_size = bs_size_hdr;
		result->is_key_frm = false;
		break;
	}

	case VENC_START_OPT_ENCODE_FRAME: {
		int hdr_sz;
		int hdr_sz_ext;
		int filler_sz = 0;
		const int bs_alignment = 128;
		struct mtk_vcodec_mem tmp_bs_buf;
		unsigned int bs_size_hdr;
		unsigned int bs_size_frm;

		if (!inst->prepend_hdr) {
			ret = h264_encode_frame(inst, frm_buf, bs_buf,
						&result->bs_size);
			if (ret)
				goto encode_err;
			result->is_key_frm = inst->vpu_inst.is_key_frm;
			break;
		}

		mtk_venc_debug(ctx, "h264_encode_frame prepend SPS/PPS");

		ret = h264_encode_header(inst, bs_buf, &bs_size_hdr);
		if (ret)
			goto encode_err;

		hdr_sz = bs_size_hdr;
		hdr_sz_ext = (hdr_sz & (bs_alignment - 1));
		if (hdr_sz_ext) {
			filler_sz = bs_alignment - hdr_sz_ext;
			if (hdr_sz_ext + H264_FILLER_MARKER_SIZE > bs_alignment)
				filler_sz += bs_alignment;
			h264_encode_filler(inst, bs_buf->va + hdr_sz,
					   filler_sz);
		}

		tmp_bs_buf.va = bs_buf->va + hdr_sz + filler_sz;
		tmp_bs_buf.dma_addr = bs_buf->dma_addr + hdr_sz + filler_sz;
		tmp_bs_buf.size = bs_buf->size - (hdr_sz + filler_sz);

		ret = h264_encode_frame(inst, frm_buf, &tmp_bs_buf,
					&bs_size_frm);
		if (ret)
			goto encode_err;

		result->bs_size = hdr_sz + filler_sz + bs_size_frm;

		mtk_venc_debug(ctx, "hdr %d filler %d frame %d bs %d",
			       hdr_sz, filler_sz, bs_size_frm, result->bs_size);

		inst->prepend_hdr = 0;
		result->is_key_frm = inst->vpu_inst.is_key_frm;
		break;
	}

	default:
		mtk_venc_err(ctx, "venc_start_opt %d not supported", opt);
		ret = -EINVAL;
		break;
	}

encode_err:

	disable_irq(ctx->dev->enc_irq);
	mtk_venc_debug(ctx, "opt %d <-", opt);

	return ret;
}

static void h264_enc_set_vsi_configs(struct venc_h264_inst *inst,
				     struct venc_enc_param *enc_prm)
{
	inst->vsi->config.input_fourcc = enc_prm->input_yuv_fmt;
	inst->vsi->config.bitrate = enc_prm->bitrate;
	inst->vsi->config.pic_w = enc_prm->width;
	inst->vsi->config.pic_h = enc_prm->height;
	inst->vsi->config.buf_w = enc_prm->buf_width;
	inst->vsi->config.buf_h = enc_prm->buf_height;
	inst->vsi->config.gop_size = enc_prm->gop_size;
	inst->vsi->config.framerate = enc_prm->frm_rate;
	inst->vsi->config.intra_period = enc_prm->intra_period;
	inst->vsi->config.profile =
		h264_get_profile(inst, enc_prm->h264_profile);
	inst->vsi->config.level =
		h264_get_level(inst, enc_prm->h264_level);
	inst->vsi->config.wfd = 0;
}

static void h264_enc_set_vsi_34_configs(struct venc_h264_inst *inst,
					struct venc_enc_param *enc_prm)
{
	inst->vsi_34->config.input_fourcc = enc_prm->input_yuv_fmt;
	inst->vsi_34->config.bitrate = enc_prm->bitrate;
	inst->vsi_34->config.pic_w = enc_prm->width;
	inst->vsi_34->config.pic_h = enc_prm->height;
	inst->vsi_34->config.buf_w = enc_prm->buf_width;
	inst->vsi_34->config.buf_h = enc_prm->buf_height;
	inst->vsi_34->config.gop_size = enc_prm->gop_size;
	inst->vsi_34->config.framerate = enc_prm->frm_rate;
	inst->vsi_34->config.intra_period = enc_prm->intra_period;
	inst->vsi_34->config.profile =
		h264_get_profile(inst, enc_prm->h264_profile);
	inst->vsi_34->config.level =
		h264_get_level(inst, enc_prm->h264_level);
	inst->vsi_34->config.wfd = 0;
}

static int h264_enc_set_param(void *handle,
			      enum venc_set_param_type type,
			      struct venc_enc_param *enc_prm)
{
	int ret = 0;
	struct venc_h264_inst *inst = (struct venc_h264_inst *)handle;
	struct mtk_vcodec_enc_ctx *ctx = inst->ctx;
	const bool is_34bit = MTK_ENC_IOVA_IS_34BIT(ctx);

	mtk_venc_debug(ctx, "->type=%d", type);

	switch (type) {
	case VENC_SET_PARAM_ENC:
		if (is_34bit)
			h264_enc_set_vsi_34_configs(inst, enc_prm);
		else
			h264_enc_set_vsi_configs(inst, enc_prm);
		ret = vpu_enc_set_param(&inst->vpu_inst, type, enc_prm);
		if (ret)
			break;
		if (inst->work_buf_allocated) {
			h264_enc_free_work_buf(inst);
			inst->work_buf_allocated = false;
		}
		ret = h264_enc_alloc_work_buf(inst, is_34bit);
		if (ret)
			break;
		inst->work_buf_allocated = true;
		break;

	case VENC_SET_PARAM_PREPEND_HEADER:
		inst->prepend_hdr = 1;
		mtk_venc_debug(ctx, "set prepend header mode");
		break;
	case VENC_SET_PARAM_FORCE_INTRA:
	case VENC_SET_PARAM_GOP_SIZE:
	case VENC_SET_PARAM_INTRA_PERIOD:
		inst->frm_cnt = 0;
		inst->skip_frm_cnt = 0;
		fallthrough;
	default:
		ret = vpu_enc_set_param(&inst->vpu_inst, type, enc_prm);
		break;
	}

	return ret;
}

static int h264_enc_deinit(void *handle)
{
	int ret = 0;
	struct venc_h264_inst *inst = (struct venc_h264_inst *)handle;

	ret = vpu_enc_deinit(&inst->vpu_inst);

	if (inst->work_buf_allocated)
		h264_enc_free_work_buf(inst);

	kfree(inst);

	return ret;
}

const struct venc_common_if venc_h264_if = {
	.init = h264_enc_init,
	.encode = h264_enc_encode,
	.set_param = h264_enc_set_param,
	.deinit = h264_enc_deinit,
};
