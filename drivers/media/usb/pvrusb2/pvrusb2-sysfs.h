/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 */
#ifndef __PVRUSB2_SYSFS_H
#define __PVRUSB2_SYSFS_H

#include <linux/list.h>
#include <linux/sysfs.h>
#include "pvrusb2-context.h"

#ifdef CONFIG_VIDEO_PVRUSB2_SYSFS
void pvr2_sysfs_class_create(void);
void pvr2_sysfs_class_destroy(void);
void pvr2_sysfs_create(struct pvr2_context *mp);
#else
static inline void pvr2_sysfs_class_create(void) { }
static inline void pvr2_sysfs_class_destroy(void) { }
static inline void pvr2_sysfs_create(struct pvr2_context *mp) { }
#endif


#endif /* __PVRUSB2_SYSFS_H */
