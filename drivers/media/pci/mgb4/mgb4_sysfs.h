/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2022 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

#ifndef __MGB4_SYSFS_H__
#define __MGB4_SYSFS_H__

#include <linux/sysfs.h>

extern struct attribute *mgb4_pci_attrs[];
extern struct attribute *mgb4_fpdl3_in_attrs[];
extern struct attribute *mgb4_gmsl_in_attrs[];
extern struct attribute *mgb4_fpdl3_out_attrs[];
extern struct attribute *mgb4_gmsl_out_attrs[];

#endif
