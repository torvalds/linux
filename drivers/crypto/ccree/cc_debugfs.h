/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

#ifndef __CC_DEFS_H__
#define __CC_DEFS_H__

#ifdef CONFIG_DE_FS
void cc_defs_global_init(void);
void cc_defs_global_fini(void);

int cc_defs_init(struct cc_drvdata *drvdata);
void cc_defs_fini(struct cc_drvdata *drvdata);

#else

static inline void cc_defs_global_init(void) {}
static inline void cc_defs_global_fini(void) {}

static inline int cc_defs_init(struct cc_drvdata *drvdata)
{
	return 0;
}

static inline void cc_defs_fini(struct cc_drvdata *drvdata) {}

#endif

#endif /*__CC_SYSFS_H__*/
