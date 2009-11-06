/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved.
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __ETHPORT_DEFS_H__
#define __ETHPORT_DEFS_H__

struct bnad_drv_stats {
	u64 netif_queue_stop;
	u64 netif_queue_wakeup;
	u64 tso4;
	u64 tso6;
	u64 tso_err;
	u64 tcpcsum_offload;
	u64 udpcsum_offload;
	u64 csum_help;
	u64 csum_help_err;

	u64 hw_stats_updates;
	u64 netif_rx_schedule;
	u64 netif_rx_complete;
	u64 netif_rx_dropped;
};
#endif
