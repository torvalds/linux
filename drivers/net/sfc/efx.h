/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_EFX_H
#define EFX_EFX_H

#include "net_driver.h"

/* PCI IDs */
#define EFX_VENDID_SFC	        0x1924
#define FALCON_A_P_DEVID	0x0703
#define FALCON_A_S_DEVID        0x6703
#define FALCON_B_P_DEVID        0x0710
#define BETHPAGE_A_P_DEVID      0x0803
#define SIENA_A_P_DEVID         0x0813

/* Solarstorm controllers use BAR 0 for I/O space and BAR 2(&3) for memory */
#define EFX_MEM_BAR 2

/* TX */
extern int efx_probe_tx_queue(struct efx_tx_queue *tx_queue);
extern void efx_remove_tx_queue(struct efx_tx_queue *tx_queue);
extern void efx_init_tx_queue(struct efx_tx_queue *tx_queue);
extern void efx_fini_tx_queue(struct efx_tx_queue *tx_queue);
extern void efx_release_tx_buffers(struct efx_tx_queue *tx_queue);
extern netdev_tx_t
efx_hard_start_xmit(struct sk_buff *skb, struct net_device *net_dev);
extern netdev_tx_t
efx_enqueue_skb(struct efx_tx_queue *tx_queue, struct sk_buff *skb);
extern void efx_xmit_done(struct efx_tx_queue *tx_queue, unsigned int index);
extern void efx_stop_queue(struct efx_channel *channel);
extern void efx_wake_queue(struct efx_channel *channel);
#define EFX_TXQ_SIZE 1024
#define EFX_TXQ_MASK (EFX_TXQ_SIZE - 1)

/* RX */
extern int efx_probe_rx_queue(struct efx_rx_queue *rx_queue);
extern void efx_remove_rx_queue(struct efx_rx_queue *rx_queue);
extern void efx_init_rx_queue(struct efx_rx_queue *rx_queue);
extern void efx_fini_rx_queue(struct efx_rx_queue *rx_queue);
extern void efx_rx_strategy(struct efx_channel *channel);
extern void efx_fast_push_rx_descriptors(struct efx_rx_queue *rx_queue);
extern void efx_rx_work(struct work_struct *data);
extern void __efx_rx_packet(struct efx_channel *channel,
			    struct efx_rx_buffer *rx_buf, bool checksummed);
extern void efx_rx_packet(struct efx_rx_queue *rx_queue, unsigned int index,
			  unsigned int len, bool checksummed, bool discard);
extern void efx_schedule_slow_fill(struct efx_rx_queue *rx_queue, int delay);
#define EFX_RXQ_SIZE 1024
#define EFX_RXQ_MASK (EFX_RXQ_SIZE - 1)

/* Channels */
extern void efx_process_channel_now(struct efx_channel *channel);
#define EFX_EVQ_SIZE 4096
#define EFX_EVQ_MASK (EFX_EVQ_SIZE - 1)

/* Ports */
extern int efx_reconfigure_port(struct efx_nic *efx);
extern int __efx_reconfigure_port(struct efx_nic *efx);

/* Ethtool support */
extern int efx_ethtool_get_settings(struct net_device *net_dev,
				    struct ethtool_cmd *ecmd);
extern int efx_ethtool_set_settings(struct net_device *net_dev,
				    struct ethtool_cmd *ecmd);
extern const struct ethtool_ops efx_ethtool_ops;

/* Reset handling */
extern int efx_reset(struct efx_nic *efx, enum reset_type method);
extern void efx_reset_down(struct efx_nic *efx, enum reset_type method);
extern int efx_reset_up(struct efx_nic *efx, enum reset_type method, bool ok);

/* Global */
extern void efx_schedule_reset(struct efx_nic *efx, enum reset_type type);
extern void efx_init_irq_moderation(struct efx_nic *efx, int tx_usecs,
				    int rx_usecs, bool rx_adaptive);
extern int efx_request_power(struct efx_nic *efx, int mw, const char *name);
extern void efx_hex_dump(const u8 *, unsigned int, const char *);

/* Dummy PHY ops for PHY drivers */
extern int efx_port_dummy_op_int(struct efx_nic *efx);
extern void efx_port_dummy_op_void(struct efx_nic *efx);
extern void
efx_port_dummy_op_set_id_led(struct efx_nic *efx, enum efx_led_mode mode);
extern bool efx_port_dummy_op_poll(struct efx_nic *efx);

/* MTD */
#ifdef CONFIG_SFC_MTD
extern int efx_mtd_probe(struct efx_nic *efx);
extern void efx_mtd_rename(struct efx_nic *efx);
extern void efx_mtd_remove(struct efx_nic *efx);
#else
static inline int efx_mtd_probe(struct efx_nic *efx) { return 0; }
static inline void efx_mtd_rename(struct efx_nic *efx) {}
static inline void efx_mtd_remove(struct efx_nic *efx) {}
#endif

extern unsigned int efx_monitor_interval;

static inline void efx_schedule_channel(struct efx_channel *channel)
{
	EFX_TRACE(channel->efx, "channel %d scheduling NAPI poll on CPU%d\n",
		  channel->channel, raw_smp_processor_id());
	channel->work_pending = true;

	napi_schedule(&channel->napi_str);
}

extern void efx_link_status_changed(struct efx_nic *efx);
extern void efx_link_set_advertising(struct efx_nic *efx, u32);
extern void efx_link_set_wanted_fc(struct efx_nic *efx, enum efx_fc_type);

#endif /* EFX_EFX_H */
