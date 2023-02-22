/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for AMD network controllers and boards
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef _EFX_DEVLINK_H
#define _EFX_DEVLINK_H

#include "net_driver.h"
#include <net/devlink.h>

/* Custom devlink-info version object names for details that do not map to the
 * generic standardized names.
 */
#define EFX_DEVLINK_INFO_VERSION_FW_MGMT_SUC	"fw.mgmt.suc"
#define EFX_DEVLINK_INFO_VERSION_FW_MGMT_CMC	"fw.mgmt.cmc"
#define EFX_DEVLINK_INFO_VERSION_FPGA_REV	"fpga.rev"
#define EFX_DEVLINK_INFO_VERSION_DATAPATH_HW	"fpga.app"
#define EFX_DEVLINK_INFO_VERSION_DATAPATH_FW	DEVLINK_INFO_VERSION_GENERIC_FW_APP
#define EFX_DEVLINK_INFO_VERSION_SOC_BOOT	"coproc.boot"
#define EFX_DEVLINK_INFO_VERSION_SOC_UBOOT	"coproc.uboot"
#define EFX_DEVLINK_INFO_VERSION_SOC_MAIN	"coproc.main"
#define EFX_DEVLINK_INFO_VERSION_SOC_RECOVERY	"coproc.recovery"
#define EFX_DEVLINK_INFO_VERSION_FW_EXPROM	"fw.exprom"
#define EFX_DEVLINK_INFO_VERSION_FW_UEFI	"fw.uefi"

#define EFX_MAX_VERSION_INFO_LEN	64

int efx_probe_devlink_and_lock(struct efx_nic *efx);
void efx_probe_devlink_unlock(struct efx_nic *efx);
void efx_fini_devlink_lock(struct efx_nic *efx);
void efx_fini_devlink_and_unlock(struct efx_nic *efx);

#ifdef CONFIG_SFC_SRIOV
struct efx_rep;

void ef100_pf_set_devlink_port(struct efx_nic *efx);
void ef100_rep_set_devlink_port(struct efx_rep *efv);
void ef100_pf_unset_devlink_port(struct efx_nic *efx);
void ef100_rep_unset_devlink_port(struct efx_rep *efv);
#endif
#endif	/* _EFX_DEVLINK_H */
