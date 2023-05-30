/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef __MTK_VCODEC_DBGFS_H__
#define __MTK_VCODEC_DBGFS_H__

struct mtk_vcodec_dev;

/**
 * struct mtk_vcodec_dbgfs  - dbgfs information
 * @vcodec_root: vcodec dbgfs entry
 */
struct mtk_vcodec_dbgfs {
	struct dentry *vcodec_root;
};

#if defined(CONFIG_DEBUG_FS)
void mtk_vcodec_dbgfs_init(struct mtk_vcodec_dev *vcodec_dev);
void mtk_vcodec_dbgfs_deinit(struct mtk_vcodec_dev *vcodec_dev);
#else
static inline void mtk_vcodec_dbgfs_init(struct mtk_vcodec_dev *vcodec_dev)
{
}

static inline void mtk_vcodec_dbgfs_deinit(struct mtk_vcodec_dev *vcodec_dev)
{
}
#endif
#endif
