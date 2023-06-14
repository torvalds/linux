// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#include <linux/debugfs.h>

#include "mtk_vcodec_dbgfs.h"
#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_util.h"

static void mtk_vdec_dbgfs_get_format_type(struct mtk_vcodec_ctx *ctx, char *buf,
					   int *used, int total)
{
	int curr_len;

	switch (ctx->current_codec) {
	case V4L2_PIX_FMT_H264_SLICE:
		curr_len = snprintf(buf + *used, total - *used,
				    "\toutput format: h264 slice\n");
		break;
	case V4L2_PIX_FMT_VP8_FRAME:
		curr_len = snprintf(buf + *used, total - *used,
				    "\toutput format: vp8 slice\n");
		break;
	case V4L2_PIX_FMT_VP9_FRAME:
		curr_len = snprintf(buf + *used, total - *used,
				    "\toutput format: vp9 slice\n");
		break;
	default:
		curr_len = snprintf(buf + *used, total - *used,
				    "\tunsupported output format: 0x%x\n",
				    ctx->current_codec);
	}
	*used += curr_len;

	switch (ctx->capture_fourcc) {
	case V4L2_PIX_FMT_MM21:
		curr_len = snprintf(buf + *used, total - *used,
				    "\tcapture format: MM21\n");
		break;
	case V4L2_PIX_FMT_MT21C:
		curr_len = snprintf(buf + *used, total - *used,
				    "\tcapture format: MT21C\n");
		break;
	default:
		curr_len = snprintf(buf + *used, total - *used,
				    "\tunsupported capture format: 0x%x\n",
				    ctx->capture_fourcc);
	}
	*used += curr_len;
}

static void mtk_vdec_dbgfs_get_help(char *buf, int *used, int total)
{
	int curr_len;

	curr_len = snprintf(buf + *used, total - *used,
			    "help: (1: echo -'info' > vdec 2: cat vdec)\n");
	*used += curr_len;

	curr_len = snprintf(buf + *used, total - *used,
			    "\t-picinfo: get resolution\n");
	*used += curr_len;

	curr_len = snprintf(buf + *used, total - *used,
			    "\t-format: get output & capture queue format\n");
	*used += curr_len;
}

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

static ssize_t mtk_vdec_dbgfs_read(struct file *filp, char __user *ubuf,
				   size_t count, loff_t *ppos)
{
	struct mtk_vcodec_dev *vcodec_dev = filp->private_data;
	struct mtk_vcodec_dbgfs *dbgfs = &vcodec_dev->dbgfs;
	struct mtk_vcodec_dbgfs_inst *dbgfs_inst;
	struct mtk_vcodec_ctx *ctx;
	int total_len = 200 * (dbgfs->inst_count == 0 ? 1 : dbgfs->inst_count);
	int used_len = 0, curr_len, ret;
	bool dbgfs_index[MTK_VDEC_DBGFS_MAX] = {0};
	char *buf = kmalloc(total_len, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	if (strstr(dbgfs->dbgfs_buf, "-help") || dbgfs->buf_size == 1) {
		mtk_vdec_dbgfs_get_help(buf, &used_len, total_len);
		goto read_buffer;
	}

	if (strstr(dbgfs->dbgfs_buf, "-picinfo"))
		dbgfs_index[MTK_VDEC_DBGFS_PICINFO] = true;

	if (strstr(dbgfs->dbgfs_buf, "-format"))
		dbgfs_index[MTK_VDEC_DBGFS_FORMAT] = true;

	mutex_lock(&dbgfs->dbgfs_lock);
	list_for_each_entry(dbgfs_inst, &dbgfs->dbgfs_head, node) {
		ctx = dbgfs_inst->vcodec_ctx;

		curr_len = snprintf(buf + used_len, total_len - used_len,
				    "inst[%d]:\n ", ctx->id);
		used_len += curr_len;

		if (dbgfs_index[MTK_VDEC_DBGFS_PICINFO]) {
			curr_len = snprintf(buf + used_len, total_len - used_len,
					    "\treal(%dx%d)=>align(%dx%d)\n",
					    ctx->picinfo.pic_w, ctx->picinfo.pic_h,
					    ctx->picinfo.buf_w, ctx->picinfo.buf_h);
			used_len += curr_len;
		}

		if (dbgfs_index[MTK_VDEC_DBGFS_FORMAT])
			mtk_vdec_dbgfs_get_format_type(ctx, buf, &used_len, total_len);
	}
	mutex_unlock(&dbgfs->dbgfs_lock);
read_buffer:
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, used_len);
	kfree(buf);
	return ret;
}

static const struct file_operations vdec_fops = {
	.open = simple_open,
	.write = mtk_vdec_dbgfs_write,
	.read = mtk_vdec_dbgfs_read,
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
			list_del(&dbgfs_inst->node);
			kfree(dbgfs_inst);
			return;
		}
	}
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dbgfs_remove);

void mtk_vcodec_dbgfs_init(struct mtk_vcodec_dev *vcodec_dev, bool is_encode)
{
	struct dentry *vcodec_root;

	if (is_encode)
		vcodec_dev->dbgfs.vcodec_root = debugfs_create_dir("vcodec-enc", NULL);
	else
		vcodec_dev->dbgfs.vcodec_root = debugfs_create_dir("vcodec-dec", NULL);
	if (IS_ERR(vcodec_dev->dbgfs.vcodec_root))
		dev_err(&vcodec_dev->plat_dev->dev, "create vcodec dir err:%ld\n",
			PTR_ERR(vcodec_dev->dbgfs.vcodec_root));

	vcodec_root = vcodec_dev->dbgfs.vcodec_root;
	debugfs_create_x32("mtk_v4l2_dbg_level", 0644, vcodec_root, &mtk_v4l2_dbg_level);
	debugfs_create_x32("mtk_vcodec_dbg", 0644, vcodec_root, &mtk_vcodec_dbg);

	vcodec_dev->dbgfs.inst_count = 0;
	if (is_encode)
		return;

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
