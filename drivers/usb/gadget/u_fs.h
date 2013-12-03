/*
 * u_fs.h
 *
 * Utility definitions for the FunctionFS
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef U_FFS_H
#define U_FFS_H

#include <linux/usb/composite.h>
#include <linux/list.h>
#include <linux/mutex.h>

struct ffs_dev {
	const char *name;
	bool mounted;
	bool desc_ready;
	bool single;
	struct ffs_data *ffs_data;
	struct list_head entry;

	int (*ffs_ready_callback)(struct ffs_data *ffs);
	void (*ffs_closed_callback)(struct ffs_data *ffs);
};

extern struct mutex ffs_lock;

static inline void ffs_dev_lock(void)
{
	mutex_lock(&ffs_lock);
}

static inline void ffs_dev_unlock(void)
{
	mutex_unlock(&ffs_lock);
}

struct ffs_dev *ffs_alloc_dev(void);
int ffs_name_dev(struct ffs_dev *dev, const char *name);
int ffs_single_dev(struct ffs_dev *dev);
void ffs_free_dev(struct ffs_dev *dev);

#endif /* U_FFS_H */
