/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_COMMON_H
#define EFX_COMMON_H

int efx_init_io(struct efx_nic *efx, int bar, dma_addr_t dma_mask,
		unsigned int mem_map_size);
void efx_fini_io(struct efx_nic *efx, int bar);
int efx_init_struct(struct efx_nic *efx, struct pci_dev *pci_dev,
		    struct net_device *net_dev);
void efx_fini_struct(struct efx_nic *efx);

void efx_start_all(struct efx_nic *efx);
void efx_stop_all(struct efx_nic *efx);

int efx_create_reset_workqueue(void);
void efx_queue_reset_work(struct efx_nic *efx);
void efx_flush_reset_workqueue(struct efx_nic *efx);
void efx_destroy_reset_workqueue(void);

void efx_start_monitor(struct efx_nic *efx);

int __efx_reconfigure_port(struct efx_nic *efx);
int efx_reconfigure_port(struct efx_nic *efx);

#define EFX_ASSERT_RESET_SERIALISED(efx)		\
	do {						\
		if ((efx->state == STATE_READY) ||	\
		    (efx->state == STATE_RECOVERY) ||	\
		    (efx->state == STATE_DISABLED))	\
			ASSERT_RTNL();			\
	} while (0)

int efx_try_recovery(struct efx_nic *efx);
void efx_reset_down(struct efx_nic *efx, enum reset_type method);
int efx_reset_up(struct efx_nic *efx, enum reset_type method, bool ok);
int efx_reset(struct efx_nic *efx, enum reset_type method);
void efx_schedule_reset(struct efx_nic *efx, enum reset_type type);

static inline int efx_check_disabled(struct efx_nic *efx)
{
	if (efx->state == STATE_DISABLED || efx->state == STATE_RECOVERY) {
		netif_err(efx, drv, efx->net_dev,
			  "device is disabled due to earlier errors\n");
		return -EIO;
	}
	return 0;
}

void efx_mac_reconfigure(struct efx_nic *efx);
void efx_link_status_changed(struct efx_nic *efx);

#endif
