// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#include <linux/debugfs.h>

#include "mtk_vcodec_dbgfs.h"
#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_util.h"

static ssize_t mtk_vdec_dbgfs_write(struct file *filp, const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	struct mtk_vcodec_dev *vcodec_dev = filp->private_data;
	struct mtk_vcodec_dbgfs *dbgfs = &vcodec_dev->dbgfs;

	mutex_lock(&dbgfs->dbgfs_lock);
	dbgfs->buf_size = simple_write_to_buffer(dbgfs->dbgfs_buf, sizeof(dbgfs->dbgfs_buf),
						 ppos, ubuf, count);
	mutex_unlock(&dbgfs->dbgfs_lock);
	if (dbgfs->buf_size > 0)
		return count;

	return dbgfs->buf_size;
}

static const struct file_operations vdec_fops = {
	.open = simple_open,
	.write = mtk_vdec_dbgfs_write,
};

void mtk_vcodec_dbgfs_create(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_vcodec_dbgfs_inst *dbgfs_inst;
	struct mtk_vcodec_dev *vcodec_dev = ctx->dev;

	dbgfs_inst = kzalloc(sizeof(*dbgfs_inst), GFP_KERNEL);
	if (!dbgfs_inst)
		return;

	list_add_tail(&dbgfs_inst->node, &vcodec_dev->dbgfs.dbgfs_head);

	vcodec_dev->dbgfs.inst_count++;

	dbgfs_inst->inst_id = ctx->id;
	dbgfs_inst->vcodec_ctx = ctx;
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dbgfs_create);

void mtk_vcodec_dbgfs_remove(struct mtk_vcodec_dev *vcodec_dev, int ctx_id)
{
	struct mtk_vcodec_dbgfs_inst *dbgfs_inst;

	list_for_each_entry(dbgfs_inst, &vcodec_dev->dbgfs.dbgfs_head, node) {
		if (dbgfs_inst->inst_id == ctx_id) {
			vcodec_dev->dbgfs.inst_count--;
			break;
		}
	}

	if (dbgfs_inst) {
		list_del(&dbgfs_inst->node);
		kfree(dbgfs_inst);
	}
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dbgfs_remove);

void mtk_vcodec_dbgfs_init(struct mtk_vcodec_dev *vcodec_dev)
{
	struct dentry *vcodec_root;

	vcodec_dev->dbgfs.vcodec_root = debugfs_create_dir("vcodec-dec", NULL);
	if (IS_ERR(vcodec_dev->dbgfs.vcodec_root))
		dev_err(&vcodec_dev->plat_dev->dev, "create vcodec dir err:%d\n",
			IS_ERR(vcodec_dev->dbgfs.vcodec_root));

	vcodec_root = vcodec_dev->dbgfs.vcodec_root;
	debugfs_create_x32("mtk_v4l2_dbg_level", 0644, vcodec_root, &mtk_v4l2_dbg_level);
	debugfs_create_x32("mtk_vcodec_dbg", 0644, vcodec_root, &mtk_vcodec_dbg);

	vcodec_dev->dbgfs.inst_count = 0;

	INIT_LIST_HEAD(&vcodec_dev->dbgfs.dbgfs_head);
	debugfs_create_file("vdec", 0200, vcodec_root, vcodec_dev, &vdec_fops);
	mutex_init(&vcodec_dev->dbgfs.dbgfs_lock);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dbgfs_init);

void mtk_vcodec_dbgfs_deinit(struct mtk_vcodec_dev *vcodec_dev)
{
	debugfs_remove_recursive(vcodec_dev->dbgfs.vcodec_root);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dbgfs_deinit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Mediatek video codec driver");
