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

#define BRCMF_VERSION_STR		"4.218.248.5"

#include "fweh.h"

/*******************************************************************************
 * IO codes that are interpreted by dongle firmware
 ******************************************************************************/
#define BRCMF_C_GET_VERSION			1
#define BRCMF_C_UP				2
#define BRCMF_C_DOWN				3
#define BRCMF_C_SET_PROMISC			10
#define BRCMF_C_GET_RATE			12
#define BRCMF_C_GET_INFRA			19
#define BRCMF_C_SET_INFRA			20
#define BRCMF_C_GET_AUTH			21
#define BRCMF_C_SET_AUTH			22
#define BRCMF_C_GET_BSSID			23
#define BRCMF_C_GET_SSID			25
#define BRCMF_C_SET_SSID			26
#define BRCMF_C_TERMINATED			28
#define BRCMF_C_GET_CHANNEL			29
#define BRCMF_C_SET_CHANNEL			30
#define BRCMF_C_GET_SRL				31
#define BRCMF_C_SET_SRL				32
#define BRCMF_C_GET_LRL				33
#define BRCMF_C_SET_LRL				34
#define BRCMF_C_GET_RADIO			37
#define BRCMF_C_SET_RADIO			38
#define BRCMF_C_GET_PHYTYPE			39
#define BRCMF_C_SET_KEY				45
#define BRCMF_C_SET_PASSIVE_SCAN		49
#define BRCMF_C_SCAN				50
#define BRCMF_C_SCAN_RESULTS			51
#define BRCMF_C_DISASSOC			52
#define BRCMF_C_REASSOC				53
#define BRCMF_C_SET_ROAM_TRIGGER		55
#define BRCMF_C_SET_ROAM_DELTA			57
#define BRCMF_C_GET_BCNPRD			75
#define BRCMF_C_SET_BCNPRD			76
#define BRCMF_C_GET_DTIMPRD			77
#define BRCMF_C_SET_DTIMPRD			78
#define BRCMF_C_SET_COUNTRY			84
#define BRCMF_C_GET_PM				85
#define BRCMF_C_SET_PM				86
#define BRCMF_C_GET_CURR_RATESET		114
#define BRCMF_C_GET_AP				117
#define BRCMF_C_SET_AP				118
#define BRCMF_C_GET_RSSI			127
#define BRCMF_C_GET_WSEC			133
#define BRCMF_C_SET_WSEC			134
#define BRCMF_C_GET_PHY_NOISE			135
#define BRCMF_C_GET_BSS_INFO			136
#define BRCMF_C_GET_BANDLIST			140
#define BRCMF_C_SET_SCB_TIMEOUT			158
#define BRCMF_C_GET_PHYLIST			180
#define BRCMF_C_SET_SCAN_CHANNEL_TIME		185
#define BRCMF_C_SET_SCAN_UNASSOC_TIME		187
#define BRCMF_C_SCB_DEAUTHENTICATE_FOR_REASON	201
#define BRCMF_C_GET_VALID_CHANNELS		217
#define BRCMF_C_GET_KEY_PRIMARY			235
#define BRCMF_C_SET_KEY_PRIMARY			236
#define BRCMF_C_SET_SCAN_PASSIVE_TIME		258
#define BRCMF_C_GET_VAR				262
#define BRCMF_C_SET_VAR				263

/* phy types (returned by WLC_GET_PHYTPE) */
#define	WLC_PHY_TYPE_A		0
#define	WLC_PHY_TYPE_B		1
#define	WLC_PHY_TYPE_G		2
#define	WLC_PHY_TYPE_N		4
#define	WLC_PHY_TYPE_LP		5
#define	WLC_PHY_TYPE_SSN	6
#define	WLC_PHY_TYPE_HT		7
#define	WLC_PHY_TYPE_LCN	8
#define	WLC_PHY_TYPE_NULL	0xf

#define TOE_TX_CSUM_OL		0x00000001
#define TOE_RX_CSUM_OL		0x00000002

/* For supporting multiple interfaces */
#define BRCMF_MAX_IFS	16

#define DOT11_MAX_DEFAULT_KEYS	4

#define BRCMF_E_STATUS_SUCCESS			0
#define BRCMF_E_STATUS_FAIL			1
#define BRCMF_E_STATUS_TIMEOUT			2
#define BRCMF_E_STATUS_NO_NETWORKS		3
#define BRCMF_E_STATUS_ABORT			4
#define BRCMF_E_STATUS_NO_ACK			5
#define BRCMF_E_STATUS_UNSOLICITED		6
#define BRCMF_E_STATUS_ATTEMPT			7
#define BRCMF_E_STATUS_PARTIAL			8
#define BRCMF_E_STATUS_NEWSCAN			9
#define BRCMF_E_STATUS_NEWASSOC			10
#define BRCMF_E_STATUS_11HQUIET			11
#define BRCMF_E_STATUS_SUPPRESS			12
#define BRCMF_E_STATUS_NOCHANS			13
#define BRCMF_E_STATUS_CS_ABORT			15
#define BRCMF_E_STATUS_ERROR			16

#define BRCMF_E_REASON_INITIAL_ASSOC		0
#define BRCMF_E_REASON_LOW_RSSI			1
#define BRCMF_E_REASON_DEAUTH			2
#define BRCMF_E_REASON_DISASSOC			3
#define BRCMF_E_REASON_BCNS_LOST		4
#define BRCMF_E_REASON_MINTXRATE		9
#define BRCMF_E_REASON_TXFAIL			10

#define BRCMF_E_REASON_LINK_BSSCFG_DIS		4
#define BRCMF_E_REASON_FAST_ROAM_FAILED		5
#define BRCMF_E_REASON_DIRECTED_ROAM		6
#define BRCMF_E_REASON_TSPEC_REJECTED		7
#define BRCMF_E_REASON_BETTER_AP		8

#define BRCMF_E_PRUNE_ENCR_MISMATCH		1
#define BRCMF_E_PRUNE_BCAST_BSSID		2
#define BRCMF_E_PRUNE_MAC_DENY			3
#define BRCMF_E_PRUNE_MAC_NA			4
#define BRCMF_E_PRUNE_REG_PASSV			5
#define BRCMF_E_PRUNE_SPCT_MGMT			6
#define BRCMF_E_PRUNE_RADAR			7
#define BRCMF_E_RSN_MISMATCH			8
#define BRCMF_E_PRUNE_NO_COMMON_RATES		9
#define BRCMF_E_PRUNE_BASIC_RATES		10
#define BRCMF_E_PRUNE_CIPHER_NA			12
#define BRCMF_E_PRUNE_KNOWN_STA			13
#define BRCMF_E_PRUNE_WDS_PEER			15
#define BRCMF_E_PRUNE_QBSS_LOAD			16
#define BRCMF_E_PRUNE_HOME_AP			17

#define BRCMF_E_SUP_OTHER			0
#define BRCMF_E_SUP_DECRYPT_KEY_DATA		1
#define BRCMF_E_SUP_BAD_UCAST_WEP128		2
#define BRCMF_E_SUP_BAD_UCAST_WEP40		3
#define BRCMF_E_SUP_UNSUP_KEY_LEN		4
#define BRCMF_E_SUP_PW_KEY_CIPHER		5
#define BRCMF_E_SUP_MSG3_TOO_MANY_IE		6
#define BRCMF_E_SUP_MSG3_IE_MISMATCH		7
#define BRCMF_E_SUP_NO_INSTALL_FLAG		8
#define BRCMF_E_SUP_MSG3_NO_GTK			9
#define BRCMF_E_SUP_GRP_KEY_CIPHER		10
#define BRCMF_E_SUP_GRP_MSG1_NO_GTK		11
#define BRCMF_E_SUP_GTK_DECRYPT_FAIL		12
#define BRCMF_E_SUP_SEND_FAIL			13
#define BRCMF_E_SUP_DEAUTH			14

#define BRCMF_E_IF_ADD				1
#define BRCMF_E_IF_DEL				2
#define BRCMF_E_IF_CHANGE			3

#define BRCMF_E_IF_FLAG_NOIF			1

#define BRCMF_E_IF_ROLE_STA			0
#define BRCMF_E_IF_ROLE_AP			1
#define BRCMF_E_IF_ROLE_WDS			2

#define BRCMF_E_LINK_BCN_LOSS			1
#define BRCMF_E_LINK_DISASSOC			2
#define BRCMF_E_LINK_ASSOC_REC			3
#define BRCMF_E_LINK_BSSCFG_DIS			4

/* Small, medium and maximum buffer size for dcmd
 */
#define BRCMF_DCMD_SMLEN	256
#define BRCMF_DCMD_MEDLEN	1536
#define BRCMF_DCMD_MAXLEN	8192

#define BRCMF_AMPDU_RX_REORDER_MAXFLOWS		256

/* Length of firmware version string stored for
 * ethtool driver info which uses 32 bytes as well.
 */
#define BRCMF_DRIVER_FIRMWARE_VERSION_LEN	32

/* Bus independent dongle command */
struct brcmf_dcmd {
	uint cmd;		/* common dongle cmd definition */
	void *buf;		/* pointer to user buffer */
	uint len;		/* length of user buffer */
	u8 set;			/* get or set request (optional) */
	uint used;		/* bytes read or written (optional) */
	uint needed;		/* bytes needed (optional) */
};

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
#ifdef DEBUG
	struct dentry *dbgfs_dir;
#endif
};

struct brcmf_if_event {
	u8 ifidx;
	u8 action;
	u8 flags;
	u8 bssidx;
	u8 role;
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
u32 brcmf_get_chip_info(struct brcmf_if *ifp);
void brcmf_txfinalize(struct brcmf_pub *drvr, struct sk_buff *txp,
		      bool success);

/* Sets dongle media info (drv_version, mac address). */
int brcmf_c_preinit_dcmds(struct brcmf_if *ifp);

#endif				/* _BRCMF_H_ */
