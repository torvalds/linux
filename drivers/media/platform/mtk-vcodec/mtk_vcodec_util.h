/* SPDX-License-Identifier: GPL-2.0 */
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: PC Chen <pc.chen@mediatek.com>
*	Tiffany Lin <tiffany.lin@mediatek.com>
*/

#ifndef _MTK_VCODEC_UTIL_H_
#define _MTK_VCODEC_UTIL_H_

#include <linux/types.h>
#include <linux/dma-direction.h>

struct mtk_vcodec_mem {
	size_t size;
	void *va;
	dma_addr_t dma_addr;
};

struct mtk_vcodec_fb {
	size_t size;
	dma_addr_t dma_addr;
};

struct mtk_vcodec_ctx;
struct mtk_vcodec_dev;

#undef pr_fmt
#define pr_fmt(fmt) "%s(),%d: " fmt, __func__, __LINE__

#define mtk_v4l2_err(fmt, args...)                \
	pr_err("[MTK_V4L2][ERROR] " fmt "\n", ##args)

#define mtk_vcodec_err(h, fmt, args...)				\
	pr_err("[MTK_VCODEC][ERROR][%d]: " fmt "\n",		\
	       ((struct mtk_vcodec_ctx *)(h)->ctx)->id, ##args)


#define mtk_v4l2_debug(level, fmt, args...) pr_debug(fmt, ##args)

#define mtk_v4l2_debug_enter()  mtk_v4l2_debug(3, "+")
#define mtk_v4l2_debug_leave()  mtk_v4l2_debug(3, "-")

#define mtk_vcodec_debug(h, fmt, args...)			\
	pr_debug("[MTK_VCODEC][%d]: " fmt "\n",			\
		((struct mtk_vcodec_ctx *)(h)->ctx)->id, ##args)

#define mtk_vcodec_debug_enter(h)  mtk_vcodec_debug(h, "+")
#define mtk_vcodec_debug_leave(h)  mtk_vcodec_debug(h, "-")

void __iomem *mtk_vcodec_get_reg_addr(struct mtk_vcodec_ctx *data,
				unsigned int reg_idx);
int mtk_vcodec_mem_alloc(struct mtk_vcodec_ctx *data,
				struct mtk_vcodec_mem *mem);
void mtk_vcodec_mem_free(struct mtk_vcodec_ctx *data,
				struct mtk_vcodec_mem *mem);
void mtk_vcodec_set_curr_ctx(struct mtk_vcodec_dev *dev,
	struct mtk_vcodec_ctx *ctx);
struct mtk_vcodec_ctx *mtk_vcodec_get_curr_ctx(struct mtk_vcodec_dev *dev);

#endif /* _MTK_VCODEC_UTIL_H_ */
