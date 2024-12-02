/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2012 Texas Instruments. All rights reserved.
 */

#ifndef __WL12XX_DEBUGFS_H__
#define __WL12XX_DEBUGFS_H__

int wl12xx_debugfs_add_files(struct wl1271 *wl,
			     struct dentry *rootdir);

#endif /* __WL12XX_DEBUGFS_H__ */
