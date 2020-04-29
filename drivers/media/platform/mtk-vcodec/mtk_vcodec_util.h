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

extern int mtk_v4l2_dbg_level;
extern bool mtk_vcodec_dbg;


#define mtk_v4l2_err(fmt, args...)                \
	pr_err("[MTK_V4L2][ERROR] %s:%d: " fmt "\n", __func__, __LINE__, \
	       ##args)

#define mtk_vcodec_err(h, fmt, args...)					\
	pr_err("[MTK_VCODEC][ERROR][%d]: %s() " fmt "\n",		\
	       ((struct mtk_vcodec_ctx *)h->ctx)->id, __func__, ##args)


#if defined(DEBUG)

#define mtk_v4l2_debug(level, fmt, args...)				 \
	do {								 \
		if (mtk_v4l2_dbg_level >= level)			 \
			pr_info("[MTK_V4L2] level=%d %s(),%d: " fmt "\n",\
				level, __func__, __LINE__, ##args);	 \
	} while (0)

#define mtk_v4l2_debug_enter()  mtk_v4l2_debug(3, "+")
#define mtk_v4l2_debug_leave()  mtk_v4l2_debug(3, "-")

#define mtk_vcodec_debug(h, fmt, args...)				\
	do {								\
		if (mtk_vcodec_dbg)					\
			pr_info("[MTK_VCODEC][%d]: %s() " fmt "\n",	\
				((struct mtk_vcodec_ctx *)h->ctx)->id, \
				__func__, ##args);			\
	} while (0)

#define mtk_vcodec_debug_enter(h)  mtk_vcodec_debug(h, "+")
#define mtk_vcodec_debug_leave(h)  mtk_vcodec_debug(h, "-")

#else

#define mtk_v4l2_debug(level, fmt, args...) {}
#define mtk_v4l2_debug_enter() {}
#define mtk_v4l2_debug_leave() {}

#define mtk_vcodec_debug(h, fmt, args...) {}
#define mtk_vcodec_debug_enter(h) {}
#define mtk_vcodec_debug_leave(h) {}

#endif

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
