/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/****************
 * Common types *
 */

#ifndef _BRCMF_H_
#define _BRCMF_H_

#include "fweh.h"

#define TOE_TX_CSUM_OL		0x00000001
#define TOE_RX_CSUM_OL		0x00000002

/* For supporting multiple interfaces */
#define BRCMF_MAX_IFS	16

#define DOT11_MAX_DEFAULT_KEYS	4

/* Small, medium and maximum buffer size for dcmd
 */
#define BRCMF_DCMD_SMLEN	256
#define BRCMF_DCMD_MEDLEN	1536
#define BRCMF_DCMD_MAXLEN	8192

/* IOCTL from host to device are limited in lenght. A device can only handle
 * ethernet frame size. This limitation is to be applied by protocol layer.
 */
#define BRCMF_TX_IOCTL_MAX_MSG_SIZE	(ETH_FRAME_LEN+ETH_FCS_LEN)

#define BRCMF_AMPDU_RX_REORDER_MAXFLOWS		256

/* Length of firmware version string stored for
 * ethtool driver info which uses 32 bytes as well.
 */
#define BRCMF_DRIVER_FIRMWARE_VERSION_LEN	32

/**
 * struct brcmf_ampdu_rx_reorder - AMPDU receive reorder info
 *
 * @pktslots: dynamic allocated array for ordering AMPDU packets.
 * @flow_id: AMPDU flow identifier.
 * @cur_idx: last AMPDU index from firmware.
 * @exp_idx: expected next AMPDU index.
 * @max_idx: maximum amount of packets per AMPDU.
 * @pend_pkts: number of packets currently in @pktslots.
 */
struct brcmf_ampdu_rx_reorder {
	struct sk_buff **pktslots;
	u8 flow_id;
	u8 cur_idx;
	u8 exp_idx;
	u8 max_idx;
	u8 pend_pkts;
};

/* Forward decls for struct brcmf_pub (see below) */
struct brcmf_proto;	/* device communication protocol info */
struct brcmf_cfg80211_dev; /* cfg80211 device info */
struct brcmf_fws_info; /* firmware signalling info */

/* Common structure for module and instance linkage */
struct brcmf_pub {
	/* Linkage ponters */
	struct brcmf_bus *bus_if;
	struct brcmf_proto *proto;
	struct brcmf_cfg80211_info *config;

	/* Internal brcmf items */
	uint hdrlen;		/* Total BRCMF header length (proto + bus) */
	uint rxsz;		/* Rx buffer size bus module should use */
	u8 wme_dp;		/* wme discard priority */

	/* Dongle media info */
	char fwver[BRCMF_DRIVER_FIRMWARE_VERSION_LEN];
	u8 mac[ETH_ALEN];		/* MAC address obtained from dongle */

	/* Multicast data packets sent to dongle */
	unsigned long tx_multicast;

	struct brcmf_if *iflist[BRCMF_MAX_IFS];

	struct mutex proto_block;
	unsigned char proto_buf[BRCMF_DCMD_MAXLEN];

	struct brcmf_fweh_info fweh;

	struct brcmf_fws_info *fws;

	struct brcmf_ampdu_rx_reorder
		*reorder_flows[BRCMF_AMPDU_RX_REORDER_MAXFLOWS];

	u32 feat_flags;
	u32 chip_quirks;

#ifdef DEBUG
	struct dentry *dbgfs_dir;
#endif
};

/* forward declarations */
struct brcmf_cfg80211_vif;
struct brcmf_fws_mac_descriptor;

/**
 * enum brcmf_netif_stop_reason - reason for stopping netif queue.
 *
 * @BRCMF_NETIF_STOP_REASON_FWS_FC:
 *	netif stopped due to firmware signalling flow control.
 * @BRCMF_NETIF_STOP_REASON_BLOCK_BUS:
 *	netif stopped due to bus blocking.
 */
enum brcmf_netif_stop_reason {
	BRCMF_NETIF_STOP_REASON_FWS_FC = 1,
	BRCMF_NETIF_STOP_REASON_BLOCK_BUS = 2
};

/**
 * struct brcmf_if - interface control information.
 *
 * @drvr: points to device related information.
 * @vif: points to cfg80211 specific interface information.
 * @ndev: associated network device.
 * @stats: interface specific network statistics.
 * @setmacaddr_work: worker object for setting mac address.
 * @multicast_work: worker object for multicast provisioning.
 * @fws_desc: interface specific firmware-signalling descriptor.
 * @ifidx: interface index in device firmware.
 * @bssidx: index of bss associated with this interface.
 * @mac_addr: assigned mac address.
 * @netif_stop: bitmap indicates reason why netif queues are stopped.
 * @netif_stop_lock: spinlock for update netif_stop from multiple sources.
 * @pend_8021x_cnt: tracks outstanding number of 802.1x frames.
 * @pend_8021x_wait: used for signalling change in count.
 */
struct brcmf_if {
	struct brcmf_pub *drvr;
	struct brcmf_cfg80211_vif *vif;
	struct net_device *ndev;
	struct net_device_stats stats;
	struct work_struct setmacaddr_work;
	struct work_struct multicast_work;
	struct brcmf_fws_mac_descriptor *fws_desc;
	int ifidx;
	s32 bssidx;
	u8 mac_addr[ETH_ALEN];
	u8 netif_stop;
	spinlock_t netif_stop_lock;
	atomic_t pend_8021x_cnt;
	wait_queue_head_t pend_8021x_wait;
};

struct brcmf_skb_reorder_data {
	u8 *reorder;
};

int brcmf_netdev_wait_pend8021x(struct net_device *ndev);

/* Return pointer to interface name */
char *brcmf_ifname(struct brcmf_pub *drvr, int idx);

int brcmf_net_attach(struct brcmf_if *ifp, bool rtnl_locked);
struct brcmf_if *brcmf_add_if(struct brcmf_pub *drvr, s32 bssidx, s32 ifidx,
			      char *name, u8 *mac_addr);
void brcmf_del_if(struct brcmf_pub *drvr, s32 bssidx);
void brcmf_txflowblock_if(struct brcmf_if *ifp,
			  enum brcmf_netif_stop_reason reason, bool state);
void brcmf_txfinalize(struct brcmf_pub *drvr, struct sk_buff *txp, u8 ifidx,
		      bool success);

/* Sets dongle media info (drv_version, mac address). */
int brcmf_c_preinit_dcmds(struct brcmf_if *ifp);

#endif				/* _BRCMF_H_ */
