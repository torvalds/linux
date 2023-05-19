/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#ifndef _PDS_VDPA_DEBUGFS_H_
#define _PDS_VDPA_DEBUGFS_H_

#include <linux/debugfs.h>

void pds_vdpa_debugfs_create(void);
void pds_vdpa_debugfs_destroy(void);

#endif /* _PDS_VDPA_DEBUGFS_H_ */
