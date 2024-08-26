/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * DSA driver for:
 * Hirschmann Hellcreek TSN switch.
 *
 * Copyright (C) 2019,2020 Hochschule Offenburg
 * Copyright (C) 2019,2020 Linutronix GmbH
 * Authors: Kurt Kanzenbach <kurt@linutronix.de>
 *	    Kamil Alkhouri <kamil.alkhouri@hs-offenburg.de>
 */

#ifndef _HELLCREEK_HWTSTAMP_H_
#define _HELLCREEK_HWTSTAMP_H_

#include <net/dsa.h>
#include "hellcreek.h"

/* Timestamp Register */
#define PR_TS_RX_P1_STATUS_C	(0x1d * 2)
#define PR_TS_RX_P1_DATA_C	(0x1e * 2)
#define PR_TS_TX_P1_STATUS_C	(0x1f * 2)
#define PR_TS_TX_P1_DATA_C	(0x20 * 2)
#define PR_TS_RX_P2_STATUS_C	(0x25 * 2)
#define PR_TS_RX_P2_DATA_C	(0x26 * 2)
#define PR_TS_TX_P2_STATUS_C	(0x27 * 2)
#define PR_TS_TX_P2_DATA_C	(0x28 * 2)

#define PR_TS_STATUS_TS_AVAIL	BIT(2)
#define PR_TS_STATUS_TS_LOST	BIT(3)

#define SKB_PTP_TYPE(__skb) (*(unsigned int *)((__skb)->cb))

/* TX_TSTAMP_TIMEOUT: This limits the time spent polling for a TX
 * timestamp. When working properly, hardware will produce a timestamp
 * within 1ms. Software may enounter delays, so the timeout is set
 * accordingly.
 */
#define TX_TSTAMP_TIMEOUT	msecs_to_jiffies(40)

int hellcreek_port_hwtstamp_set(struct dsa_switch *ds, int port,
				struct ifreq *ifr);
int hellcreek_port_hwtstamp_get(struct dsa_switch *ds, int port,
				struct ifreq *ifr);

bool hellcreek_port_rxtstamp(struct dsa_switch *ds, int port,
			     struct sk_buff *clone, unsigned int type);
void hellcreek_port_txtstamp(struct dsa_switch *ds, int port,
			     struct sk_buff *skb);

int hellcreek_get_ts_info(struct dsa_switch *ds, int port,
			  struct kernel_ethtool_ts_info *info);

long hellcreek_hwtstamp_work(struct ptp_clock_info *ptp);

int hellcreek_hwtstamp_setup(struct hellcreek *chip);
void hellcreek_hwtstamp_free(struct hellcreek *chip);

#endif /* _HELLCREEK_HWTSTAMP_H_ */
