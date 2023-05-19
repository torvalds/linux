/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#ifndef _VDPA_DEV_H_
#define _VDPA_DEV_H_

#define PDS_VDPA_MAX_QUEUES	65

struct pds_vdpa_device {
	struct vdpa_device vdpa_dev;
	struct pds_vdpa_aux *vdpa_aux;
};

int pds_vdpa_get_mgmt_info(struct pds_vdpa_aux *vdpa_aux);
#endif /* _VDPA_DEV_H_ */
