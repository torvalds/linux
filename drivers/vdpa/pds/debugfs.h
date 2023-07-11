/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#ifndef _PDS_VDPA_DEBUGFS_H_
#define _PDS_VDPA_DEBUGFS_H_

#include <linux/debugfs.h>

void pds_vdpa_debugfs_create(void);
void pds_vdpa_debugfs_destroy(void);
void pds_vdpa_debugfs_add_pcidev(struct pds_vdpa_aux *vdpa_aux);
void pds_vdpa_debugfs_add_ident(struct pds_vdpa_aux *vdpa_aux);
void pds_vdpa_debugfs_add_vdpadev(struct pds_vdpa_aux *vdpa_aux);
void pds_vdpa_debugfs_del_vdpadev(struct pds_vdpa_aux *vdpa_aux);
void pds_vdpa_debugfs_reset_vdpadev(struct pds_vdpa_aux *vdpa_aux);

#endif /* _PDS_VDPA_DEBUGFS_H_ */
