/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#ifndef _VFIO_DEV_H_
#define _VFIO_DEV_H_

#include <linux/pci.h>
#include <linux/vfio_pci_core.h>

struct pds_vfio_pci_device {
	struct vfio_pci_core_device vfio_coredev;

	int vf_id;
};

const struct vfio_device_ops *pds_vfio_ops_info(void);
struct pds_vfio_pci_device *pds_vfio_pci_drvdata(struct pci_dev *pdev);

#endif /* _VFIO_DEV_H_ */
