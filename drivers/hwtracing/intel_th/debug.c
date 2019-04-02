// SPDX-License-Identifier: GPL-2.0
/*
 * Intel(R) Trace Hub driver deging
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/defs.h>

#include "intel_th.h"
#include "de.h"

struct dentry *intel_th_dbg;

void intel_th_de_init(void)
{
	intel_th_dbg = defs_create_dir("intel_th", NULL);
	if (IS_ERR(intel_th_dbg))
		intel_th_dbg = NULL;
}

void intel_th_de_done(void)
{
	defs_remove(intel_th_dbg);
	intel_th_dbg = NULL;
}
