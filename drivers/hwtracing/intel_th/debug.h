/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel(R) Trace Hub driver deging
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 */

#ifndef __INTEL_TH_DE_H__
#define __INTEL_TH_DE_H__

#ifdef CONFIG_INTEL_TH_DE
extern struct dentry *intel_th_dbg;

void intel_th_de_init(void);
void intel_th_de_done(void);
#else
static inline void intel_th_de_init(void)
{
}

static inline void intel_th_de_done(void)
{
}
#endif

#endif /* __INTEL_TH_DE_H__ */
