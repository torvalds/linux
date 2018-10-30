/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2018 Microchip Technology Inc. */

#ifndef _LAN743X_PTP_H
#define _LAN743X_PTP_H

#include "linux/ptp_clock_kernel.h"
#include "linux/netdevice.h"

struct lan743x_adapter;

/* GPIO */
struct lan743x_gpio {
	/* gpio_lock: used to prevent concurrent access to gpio settings */
	spinlock_t gpio_lock;

	int used_bits;
	int output_bits;
	int ptp_bits;
	u32 gpio_cfg0;
	u32 gpio_cfg1;
	u32 gpio_cfg2;
	u32 gpio_cfg3;
};

int lan743x_gpio_init(struct lan743x_adapter *adapter);

void lan743x_ptp_isr(void *context);
bool lan743x_ptp_request_tx_timestamp(struct lan743x_adapter *adapter);
void lan743x_ptp_unrequest_tx_timestamp(struct lan743x_adapter *adapter);
void lan743x_ptp_tx_timestamp_skb(struct lan743x_adapter *adapter,
				  struct sk_buff *skb, bool ignore_sync);
int lan743x_ptp_init(struct lan743x_adapter *adapter);
int lan743x_ptp_open(struct lan743x_adapter *adapter);
void lan743x_ptp_close(struct lan743x_adapter *adapter);
void lan743x_ptp_update_latency(struct lan743x_adapter *adapter,
				u32 link_speed);

int lan743x_ptp_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);

#define LAN743X_PTP_NUMBER_OF_TX_TIMESTAMPS (4)

#define PTP_FLAG_PTP_CLOCK_REGISTERED	BIT(1)
#define PTP_FLAG_ISR_ENABLED			BIT(2)

struct lan743x_ptp {
	int flags;

	/* command_lock: used to prevent concurrent ptp commands */
	struct mutex	command_lock;

	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;
	struct ptp_pin_desc pin_config[1];

#define LAN743X_PTP_NUMBER_OF_EVENT_CHANNELS (2)
	unsigned long used_event_ch;

	int perout_event_ch;
	int perout_gpio_bit;

	/* tx_ts_lock: used to prevent concurrent access to timestamp arrays */
	spinlock_t	tx_ts_lock;
	int pending_tx_timestamps;
	struct sk_buff *tx_ts_skb_queue[LAN743X_PTP_NUMBER_OF_TX_TIMESTAMPS];
	unsigned int	tx_ts_ignore_sync_queue;
	int tx_ts_skb_queue_size;
	u32 tx_ts_seconds_queue[LAN743X_PTP_NUMBER_OF_TX_TIMESTAMPS];
	u32 tx_ts_nseconds_queue[LAN743X_PTP_NUMBER_OF_TX_TIMESTAMPS];
	u32 tx_ts_header_queue[LAN743X_PTP_NUMBER_OF_TX_TIMESTAMPS];
	int tx_ts_queue_size;
};

#endif /* _LAN743X_PTP_H */
