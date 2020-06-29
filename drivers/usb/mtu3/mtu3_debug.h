/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtu3_debug.h - debug header
 *
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 */

#ifndef __MTU3_DEBUG_H__
#define __MTU3_DEBUG_H__

#include <linux/debugfs.h>

#define MTU3_DEBUGFS_NAME_LEN 32

struct mtu3_regset {
	char name[MTU3_DEBUGFS_NAME_LEN];
	struct debugfs_regset32 regset;
	size_t nregs;
};

struct mtu3_file_map {
	const char *name;
	int (*show)(struct seq_file *s, void *unused);
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
void ssusb_dev_debugfs_init(struct ssusb_mtk *ssusb);
void ssusb_dr_debugfs_init(struct ssusb_mtk *ssusb);
void ssusb_debugfs_create_root(struct ssusb_mtk *ssusb);
void ssusb_debugfs_remove_root(struct ssusb_mtk *ssusb);

#else
static inline void ssusb_dev_debugfs_init(struct ssusb_mtk *ssusb) {}
static inline void ssusb_dr_debugfs_init(struct ssusb_mtk *ssusb) {}
static inline void ssusb_debugfs_create_root(struct ssusb_mtk *ssusb) {}
static inline void ssusb_debugfs_remove_root(struct ssusb_mtk *ssusb) {}

#endif /* CONFIG_DEBUG_FS */

#if IS_ENABLED(CONFIG_TRACING)
void mtu3_dbg_trace(struct device *dev, const char *fmt, ...);

#else
static inline void mtu3_dbg_trace(struct device *dev, const char *fmt, ...) {}

#endif /* CONFIG_TRACING */

#endif /* __MTU3_DEBUG_H__ */
