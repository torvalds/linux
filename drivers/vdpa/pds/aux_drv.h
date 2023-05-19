/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#ifndef _AUX_DRV_H_
#define _AUX_DRV_H_

#define PDS_VDPA_DRV_DESCRIPTION    "AMD/Pensando vDPA VF Device Driver"
#define PDS_VDPA_DRV_NAME           KBUILD_MODNAME

struct pds_vdpa_aux {
	struct pds_auxiliary_dev *padev;

	struct vdpa_mgmt_dev vdpa_mdev;

	struct pds_vdpa_ident ident;

	int vf_id;
	struct dentry *dentry;

	int nintrs;
};
#endif /* _AUX_DRV_H_ */
