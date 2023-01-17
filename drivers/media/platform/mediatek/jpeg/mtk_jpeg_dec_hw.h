/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 *         Rick Chang <rick.chang@mediatek.com>
 *         Xia Jiang <xia.jiang@mediatek.com>
 */

#ifndef _MTK_JPEG_DEC_HW_H
#define _MTK_JPEG_DEC_HW_H

#include <media/videobuf2-core.h>

#include "mtk_jpeg_dec_reg.h"

#define MTK_JPEG_COMP_MAX		3

enum {
	MTK_JPEG_DEC_RESULT_EOF_DONE		= 0,
	MTK_JPEG_DEC_RESULT_PAUSE		= 1,
	MTK_JPEG_DEC_RESULT_UNDERFLOW		= 2,
	MTK_JPEG_DEC_RESULT_OVERFLOW		= 3,
	MTK_JPEG_DEC_RESULT_ERROR_BS		= 4,
	MTK_JPEG_DEC_RESULT_ERROR_UNKNOWN	= 6
};

struct mtk_jpeg_dec_param {
	u32 pic_w;
	u32 pic_h;
	u32 dec_w;
	u32 dec_h;
	u32 src_color;
	u32 dst_fourcc;
	u32 mcu_w;
	u32 mcu_h;
	u32 total_mcu;
	u32 unit_num;
	u32 comp_num;
	u32 comp_id[MTK_JPEG_COMP_MAX];
	u32 sampling_w[MTK_JPEG_COMP_MAX];
	u32 sampling_h[MTK_JPEG_COMP_MAX];
	u32 qtbl_num[MTK_JPEG_COMP_MAX];
	u32 blk_num;
	u32 blk_comp[MTK_JPEG_COMP_MAX];
	u32 membership;
	u32 dma_mcu;
	u32 dma_group;
	u32 dma_last_mcu;
	u32 img_stride[MTK_JPEG_COMP_MAX];
	u32 mem_stride[MTK_JPEG_COMP_MAX];
	u32 comp_w[MTK_JPEG_COMP_MAX];
	u32 comp_size[MTK_JPEG_COMP_MAX];
	u32 y_size;
	u32 uv_size;
	u32 dec_size;
	u8 uv_brz_w;
};

struct mtk_jpeg_bs {
	dma_addr_t	str_addr;
	dma_addr_t	end_addr;
	size_t		size;
};

struct mtk_jpeg_fb {
	dma_addr_t	plane_addr[MTK_JPEG_COMP_MAX];
	size_t		size;
};

int mtk_jpeg_dec_fill_param(struct mtk_jpeg_dec_param *param);
u32 mtk_jpeg_dec_get_int_status(void __iomem *dec_reg_base);
u32 mtk_jpeg_dec_enum_result(u32 irq_result);
void mtk_jpeg_dec_set_config(void __iomem *base,
			     struct mtk_jpeg_dec_param *cfg,
			     u32 bitstream_size,
			     struct mtk_jpeg_bs *bs,
			     struct mtk_jpeg_fb *fb);
void mtk_jpeg_dec_reset(void __iomem *dec_reg_base);
void mtk_jpeg_dec_start(void __iomem *dec_reg_base);

#endif /* _MTK_JPEG_HW_H */
