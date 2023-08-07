/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef __MTK_VCODEC_DBGFS_H__
#define __MTK_VCODEC_DBGFS_H__

struct mtk_vcodec_dev;
struct mtk_vcodec_ctx;

/*
 * enum mtk_vdec_dbgfs_log_index  - used to get different debug information
 */
enum mtk_vdec_dbgfs_log_index {
	MTK_VDEC_DBGFS_PICINFO,
	MTK_VDEC_DBGFS_FORMAT,
	MTK_VDEC_DBGFS_MAX,
};

/**
 * struct mtk_vcodec_dbgfs_inst  - debugfs information for each inst
 * @node:       list node for each inst
 * @vcodec_ctx: struct mtk_vcodec_ctx
 * @inst_id:    index of the context that the same with ctx->id
 */
struct mtk_vcodec_dbgfs_inst {
	struct list_head node;
	struct mtk_vcodec_ctx *vcodec_ctx;
	int inst_id;
};

/**
 * struct mtk_vcodec_dbgfs  - dbgfs information
 * @dbgfs_head:  list head used to link each instance
 * @vcodec_root: vcodec dbgfs entry
 * @dbgfs_lock:  dbgfs lock used to protect dbgfs_buf
 * @dbgfs_buf:   dbgfs buf used to store dbgfs cmd
 * @buf_size:    buffer size of dbgfs
 * @inst_count:  the count of total instance
 */
struct mtk_vcodec_dbgfs {
	struct list_head dbgfs_head;
	struct dentry *vcodec_root;
	struct mutex dbgfs_lock;
	char dbgfs_buf[1024];
	int buf_size;
	int inst_count;
};

#if defined(CONFIG_DEBUG_FS)
void mtk_vcodec_dbgfs_create(struct mtk_vcodec_ctx *ctx);
void mtk_vcodec_dbgfs_remove(struct mtk_vcodec_dev *vcodec_dev, int ctx_id);
void mtk_vcodec_dbgfs_init(struct mtk_vcodec_dev *vcodec_dev, bool is_encode);
void mtk_vcodec_dbgfs_deinit(struct mtk_vcodec_dev *vcodec_dev);
#else
static inline void mtk_vcodec_dbgfs_create(struct mtk_vcodec_ctx *ctx)
{
}

static inline void mtk_vcodec_dbgfs_remove(struct mtk_vcodec_dev *vcodec_dev, int ctx_id)
{
}

static inline void mtk_vcodec_dbgfs_init(struct mtk_vcodec_dev *vcodec_dev, bool is_encode)
{
}

static inline void mtk_vcodec_dbgfs_deinit(struct mtk_vcodec_dev *vcodec_dev)
{
}
#endif
#endif
