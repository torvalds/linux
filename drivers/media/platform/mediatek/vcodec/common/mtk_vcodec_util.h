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

#define MTK_DBG_VCODEC_STR "[MTK_VCODEC]"
#define MTK_DBG_V4L2_STR "[MTK_V4L2]"

struct mtk_vcodec_mem {
	size_t size;
	void *va;
	dma_addr_t dma_addr;
};

struct mtk_vcodec_fb {
	size_t size;
	dma_addr_t dma_addr;
};

struct mtk_vcodec_dec_ctx;
struct mtk_vcodec_dec_dev;

#undef pr_fmt
#define pr_fmt(fmt) "%s(),%d: " fmt, __func__, __LINE__

#define mtk_v4l2_err(plat_dev, fmt, args...)                            \
	dev_err(&(plat_dev)->dev, "[MTK_V4L2][ERROR] " fmt "\n", ##args)

#define mtk_vcodec_err(inst_id, plat_dev, fmt, args...)                                 \
	dev_err(&(plat_dev)->dev, "[MTK_VCODEC][ERROR][%d]: " fmt "\n", inst_id, ##args)

#if defined(CONFIG_DEBUG_FS)
extern int mtk_v4l2_dbg_level;
extern int mtk_vcodec_dbg;

#define mtk_v4l2_debug(plat_dev, level, fmt, args...)                             \
	do {                                                                      \
		if (mtk_v4l2_dbg_level >= (level))                                \
			dev_dbg(&(plat_dev)->dev, "[MTK_V4L2] %s, %d: " fmt "\n", \
				 __func__, __LINE__, ##args);                     \
	} while (0)

#define mtk_vcodec_debug(inst_id, plat_dev, fmt, args...)                               \
	do {                                                                            \
		if (mtk_vcodec_dbg)                                                     \
			dev_dbg(&(plat_dev)->dev, "[MTK_VCODEC][%d]: %s, %d " fmt "\n", \
				inst_id, __func__, __LINE__, ##args);                   \
	} while (0)
#else
#define mtk_v4l2_debug(plat_dev, level, fmt, args...)              \
	dev_dbg(&(plat_dev)->dev, "[MTK_V4L2]: " fmt "\n", ##args)

#define mtk_vcodec_debug(inst_id, plat_dev, fmt, args...)			\
	dev_dbg(&(plat_dev)->dev, "[MTK_VCODEC][%d]: " fmt "\n", inst_id, ##args)
#endif

void __iomem *mtk_vcodec_get_reg_addr(void __iomem **reg_base, unsigned int reg_idx);
int mtk_vcodec_write_vdecsys(struct mtk_vcodec_dec_ctx *ctx, unsigned int reg, unsigned int val);
int mtk_vcodec_mem_alloc(void *priv, struct mtk_vcodec_mem *mem);
void mtk_vcodec_mem_free(void *priv, struct mtk_vcodec_mem *mem);
void mtk_vcodec_set_curr_ctx(struct mtk_vcodec_dec_dev *vdec_dev,
			     struct mtk_vcodec_dec_ctx *ctx, int hw_idx);
struct mtk_vcodec_dec_ctx *mtk_vcodec_get_curr_ctx(struct mtk_vcodec_dec_dev *vdec_dev,
						   unsigned int hw_idx);
void *mtk_vcodec_get_hw_dev(struct mtk_vcodec_dec_dev *dev, int hw_idx);

#endif /* _MTK_VCODEC_UTIL_H_ */
