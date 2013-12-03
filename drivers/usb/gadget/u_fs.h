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

struct ffs_dev {
	const char *name;
	bool mounted;
	bool desc_ready;
	struct ffs_data *ffs_data;
};

#endif /* U_FFS_H */
