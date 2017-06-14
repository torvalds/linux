/*
 * CAN driver for PEAK System micro-CAN based adapters
 *
 * Copyright (C) 2003-2011 PEAK System-Technik GmbH
 * Copyright (C) 2011-2013 Stephane Grosjean <s.grosjean@peak-system.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */
#ifndef PEAK_CANFD_USER_H
#define PEAK_CANFD_USER_H

#include <linux/can/dev/peak_canfd.h>

#define PCANFD_ECHO_SKB_DEF		-1

/* data structure private to each uCAN interface */
struct peak_canfd_priv {
	struct can_priv can;		/* socket-can private data */
	struct net_device *ndev;	/* network device */
	int index;			/* channel index */

	struct can_berr_counter bec;	/* rx/tx err counters */

	int echo_idx;			/* echo skb free slot index */
	spinlock_t echo_lock;

	int cmd_len;
	void *cmd_buffer;
	int cmd_maxlen;

	int (*pre_cmd)(struct peak_canfd_priv *priv);
	int (*write_cmd)(struct peak_canfd_priv *priv);
	int (*post_cmd)(struct peak_canfd_priv *priv);

	int (*enable_tx_path)(struct peak_canfd_priv *priv);
	void *(*alloc_tx_msg)(struct peak_canfd_priv *priv, u16 msg_size,
			      int *room_left);
	int (*write_tx_msg)(struct peak_canfd_priv *priv,
			    struct pucan_tx_msg *msg);
};

struct net_device *alloc_peak_canfd_dev(int sizeof_priv, int index,
					int echo_skb_max);
int peak_canfd_handle_msg(struct peak_canfd_priv *priv,
			  struct pucan_rx_msg *msg);
int peak_canfd_handle_msgs_list(struct peak_canfd_priv *priv,
				struct pucan_rx_msg *rx_msg, int rx_count);
#endif
