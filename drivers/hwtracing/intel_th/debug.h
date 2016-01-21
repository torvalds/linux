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

#ifndef __INTEL_TH_DEBUG_H__
#define __INTEL_TH_DEBUG_H__

#ifdef CONFIG_INTEL_TH_DEBUG
extern struct dentry *intel_th_dbg;

void intel_th_debug_init(void);
void intel_th_debug_done(void);
#else
static inline void intel_th_debug_init(void)
{
}

static inline void intel_th_debug_done(void)
{
}
#endif

#endif /* __INTEL_TH_DEBUG_H__ */
