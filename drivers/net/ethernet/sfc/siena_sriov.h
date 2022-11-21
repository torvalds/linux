/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2015 Solarflare Communications Inc.
 */

#ifndef SIENA_SRIOV_H
#define SIENA_SRIOV_H

#include "net_driver.h"

/* On the SFC9000 family each port is associated with 1 PCI physical
 * function (PF) handled by sfc and a configurable number of virtual
 * functions (VFs) that may be handled by some other driver, often in
 * a VM guest.  The queue pointer registers are mapped in both PF and
 * VF BARs such that an 8K region provides access to a single RX, TX
 * and event queue (collectively a Virtual Interface, VI or VNIC).
 *
 * The PF has access to all 1024 VIs while VFs are mapped to VIs
 * according to VI_BASE and VI_SCALE: VF i has access to VIs numbered
 * in range [VI_BASE + i << VI_SCALE, VI_BASE + i + 1 << VI_SCALE).
 * The number of VIs and the VI_SCALE value are configurable but must
 * be established at boot time by firmware.
 */

/* Maximum VI_SCALE parameter supported by Siena */
#define EFX_VI_SCALE_MAX 6
/* Base VI to use for SR-IOV. Must be aligned to (1 << EFX_VI_SCALE_MAX),
 * so this is the smallest allowed value.
 */
#define EFX_VI_BASE 128U
/* Maximum number of VFs allowed */
#define EFX_VF_COUNT_MAX 127
/* Limit EVQs on VFs to be only 8k to reduce buffer table reservation */
#define EFX_MAX_VF_EVQ_SIZE 8192UL
/* The number of buffer table entries reserved for each VI on a VF */
#define EFX_VF_BUFTBL_PER_VI					\
	((EFX_MAX_VF_EVQ_SIZE + 2 * EFX_MAX_DMAQ_SIZE) *	\
	 sizeof(efx_qword_t) / EFX_BUF_SIZE)

int efx_siena_sriov_configure(struct efx_nic *efx, int num_vfs);
int efx_siena_sriov_init(struct efx_nic *efx);
void efx_siena_sriov_fini(struct efx_nic *efx);
int efx_siena_sriov_mac_address_changed(struct efx_nic *efx);
bool efx_siena_sriov_wanted(struct efx_nic *efx);
void efx_siena_sriov_reset(struct efx_nic *efx);
void efx_siena_sriov_flr(struct efx_nic *efx, unsigned flr);

int efx_siena_sriov_set_vf_mac(struct efx_nic *efx, int vf, const u8 *mac);
int efx_siena_sriov_set_vf_vlan(struct efx_nic *efx, int vf,
				u16 vlan, u8 qos);
int efx_siena_sriov_set_vf_spoofchk(struct efx_nic *efx, int vf,
				    bool spoofchk);
int efx_siena_sriov_get_vf_config(struct efx_nic *efx, int vf,
				  struct ifla_vf_info *ivf);

#ifdef CONFIG_SFC_SRIOV

static inline bool efx_siena_sriov_enabled(struct efx_nic *efx)
{
	return efx->vf_init_count != 0;
}
#else /* !CONFIG_SFC_SRIOV */
static inline bool efx_siena_sriov_enabled(struct efx_nic *efx)
{
	return false;
}
#endif /* CONFIG_SFC_SRIOV */

void efx_siena_sriov_probe(struct efx_nic *efx);
void efx_siena_sriov_tx_flush_done(struct efx_nic *efx, efx_qword_t *event);
void efx_siena_sriov_rx_flush_done(struct efx_nic *efx, efx_qword_t *event);
void efx_siena_sriov_event(struct efx_channel *channel, efx_qword_t *event);
void efx_siena_sriov_desc_fetch_err(struct efx_nic *efx, unsigned dmaq);

#endif /* SIENA_SRIOV_H */
