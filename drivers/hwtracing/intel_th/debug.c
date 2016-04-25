/*
 * Intel(R) Trace Hub driver debugging
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/debugfs.h>

#include "intel_th.h"
#include "debug.h"

struct dentry *intel_th_dbg;

void intel_th_debug_init(void)
{
	intel_th_dbg = debugfs_create_dir("intel_th", NULL);
	if (IS_ERR(intel_th_dbg))
		intel_th_dbg = NULL;
}

void intel_th_debug_done(void)
{
	debugfs_remove(intel_th_dbg);
	intel_th_dbg = NULL;
}
