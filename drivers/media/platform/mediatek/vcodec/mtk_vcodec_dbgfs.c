// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#include <linux/debugfs.h>

#include "mtk_vcodec_dbgfs.h"
#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_util.h"

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
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dbgfs_init);

void mtk_vcodec_dbgfs_deinit(struct mtk_vcodec_dev *vcodec_dev)
{
	debugfs_remove_recursive(vcodec_dev->dbgfs.vcodec_root);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_dbgfs_deinit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Mediatek video codec driver");
