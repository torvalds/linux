/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#ifndef _CMDS_H_
#define _CMDS_H_

int pds_vfio_register_client_cmd(struct pds_vfio_pci_device *pds_vfio);
void pds_vfio_unregister_client_cmd(struct pds_vfio_pci_device *pds_vfio);

#endif /* _CMDS_H_ */
