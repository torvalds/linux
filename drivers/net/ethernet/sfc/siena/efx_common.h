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

int efx_siena_init_io(struct efx_nic *efx, int bar, dma_addr_t dma_mask,
		      unsigned int mem_map_size);
void efx_siena_fini_io(struct efx_nic *efx);
int efx_siena_init_struct(struct efx_nic *efx, struct pci_dev *pci_dev,
			  struct net_device *net_dev);
void efx_siena_fini_struct(struct efx_nic *efx);

#define EFX_MAX_DMAQ_SIZE 4096UL
#define EFX_DEFAULT_DMAQ_SIZE 1024UL
#define EFX_MIN_DMAQ_SIZE 512UL

#define EFX_MAX_EVQ_SIZE 16384UL
#define EFX_MIN_EVQ_SIZE 512UL

void efx_siena_link_clear_advertising(struct efx_nic *efx);
void efx_siena_link_set_wanted_fc(struct efx_nic *efx, u8 wanted_fc);

void efx_siena_start_all(struct efx_nic *efx);
void efx_siena_stop_all(struct efx_nic *efx);

void efx_siena_net_stats(struct net_device *net_dev,
			 struct rtnl_link_stats64 *stats);

int efx_siena_create_reset_workqueue(void);
void efx_siena_queue_reset_work(struct efx_nic *efx);
void efx_siena_flush_reset_workqueue(struct efx_nic *efx);
void efx_siena_destroy_reset_workqueue(void);

void efx_siena_start_monitor(struct efx_nic *efx);

int __efx_siena_reconfigure_port(struct efx_nic *efx);
int efx_siena_reconfigure_port(struct efx_nic *efx);

#define EFX_ASSERT_RESET_SERIALISED(efx)		\
	do {						\
		if ((efx->state == STATE_READY) ||	\
		    (efx->state == STATE_RECOVERY) ||	\
		    (efx->state == STATE_DISABLED))	\
			ASSERT_RTNL();			\
	} while (0)

int efx_siena_try_recovery(struct efx_nic *efx);
void efx_siena_reset_down(struct efx_nic *efx, enum reset_type method);
void efx_siena_watchdog(struct net_device *net_dev, unsigned int txqueue);
int efx_siena_reset_up(struct efx_nic *efx, enum reset_type method, bool ok);
int efx_siena_reset(struct efx_nic *efx, enum reset_type method);
void efx_siena_schedule_reset(struct efx_nic *efx, enum reset_type type);

/* Dummy PHY ops for PHY drivers */
int efx_siena_port_dummy_op_int(struct efx_nic *efx);
void efx_siena_port_dummy_op_void(struct efx_nic *efx);

static inline int efx_check_disabled(struct efx_nic *efx)
{
	if (efx->state == STATE_DISABLED || efx->state == STATE_RECOVERY) {
		netif_err(efx, drv, efx->net_dev,
			  "device is disabled due to earlier errors\n");
		return -EIO;
	}
	return 0;
}

static inline void efx_schedule_channel(struct efx_channel *channel)
{
	netif_vdbg(channel->efx, intr, channel->efx->net_dev,
		   "channel %d scheduling NAPI poll on CPU%d\n",
		   channel->channel, raw_smp_processor_id());

	napi_schedule(&channel->napi_str);
}

static inline void efx_schedule_channel_irq(struct efx_channel *channel)
{
	channel->event_test_cpu = raw_smp_processor_id();
	efx_schedule_channel(channel);
}

#ifdef CONFIG_SFC_SIENA_MCDI_LOGGING
void efx_siena_init_mcdi_logging(struct efx_nic *efx);
void efx_siena_fini_mcdi_logging(struct efx_nic *efx);
#else
static inline void efx_siena_init_mcdi_logging(struct efx_nic *efx) {}
static inline void efx_siena_fini_mcdi_logging(struct efx_nic *efx) {}
#endif

void efx_siena_mac_reconfigure(struct efx_nic *efx, bool mtu_only);
int efx_siena_set_mac_address(struct net_device *net_dev, void *data);
void efx_siena_set_rx_mode(struct net_device *net_dev);
int efx_siena_set_features(struct net_device *net_dev, netdev_features_t data);
void efx_siena_link_status_changed(struct efx_nic *efx);
unsigned int efx_siena_xdp_max_mtu(struct efx_nic *efx);
int efx_siena_change_mtu(struct net_device *net_dev, int new_mtu);

extern const struct pci_error_handlers efx_siena_err_handlers;

netdev_features_t efx_siena_features_check(struct sk_buff *skb,
					   struct net_device *dev,
					   netdev_features_t features);

int efx_siena_get_phys_port_id(struct net_device *net_dev,
			       struct netdev_phys_item_id *ppid);

int efx_siena_get_phys_port_name(struct net_device *net_dev,
				 char *name, size_t len);
#endif
