/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __STA_INFO_H_
#define __STA_INFO_H_

#include "osdep_service.h"
#include "drv_types.h"
#include "wifi.h"

#define NUM_STA 32
#define NUM_ACL 64


/* if mode ==0, then the sta is allowed once the addr is hit.
 * if mode ==1, then the sta is rejected once the addr is non-hit.
 */
struct wlan_acl_node {
	struct list_head list;
	u8       addr[ETH_ALEN];
	u8       mode;
};

struct wlan_acl_pool {
	struct wlan_acl_node aclnode[NUM_ACL];
};

struct	stainfo_stats {

	uint	rx_pkts;
	uint	rx_bytes;
	u64	tx_pkts;
	uint	tx_bytes;
};

struct sta_info {
	spinlock_t lock;
	struct list_head list; /*free_sta_queue*/
	struct list_head hash_list; /*sta_hash*/
	struct sta_xmit_priv sta_xmitpriv;
	struct sta_recv_priv sta_recvpriv;
	uint state;
	uint aid;
	uint	mac_id;
	uint	qos_option;
	u8	hwaddr[ETH_ALEN];
	uint	ieee8021x_blocked;	/*0: allowed, 1:blocked */
	uint	XPrivacy; /*aes, tkip...*/
	union Keytype	tkiptxmickey;
	union Keytype	tkiprxmickey;
	union Keytype	x_UncstKey;
	union pn48		txpn;	/* PN48 used for Unicast xmit.*/
	union pn48		rxpn;	/* PN48 used for Unicast recv.*/
	u8	bssrateset[16];
	uint	bssratelen;
	s32  rssi;
	s32	signal_quality;
	struct stainfo_stats sta_stats;
	/*for A-MPDU Rx reordering buffer control */
	struct recv_reorder_ctrl recvreorder_ctrl[16];
	struct ht_priv	htpriv;
	/* Notes:
	 * STA_Mode:
	 * curr_network(mlme_priv/security_priv/qos/ht)
	 *   + sta_info: (STA & AP) CAP/INFO
	 * scan_q: AP CAP/INFO
	 * AP_Mode:
	 * curr_network(mlme_priv/security_priv/qos/ht) : AP CAP/INFO
	 * sta_info: (AP & STA) CAP/INFO
	 */
	struct list_head asoc_list;
	struct list_head auth_list;
	unsigned int expire_to;
	unsigned int auth_seq;
	unsigned int authalg;
	unsigned char chg_txt[128];
	unsigned int tx_ra_bitmap;
};

struct	sta_priv {
	u8 *pallocated_stainfo_buf;
	u8 *pstainfo_buf;
	struct  __queue	free_sta_queue;
	spinlock_t sta_hash_lock;
	struct list_head sta_hash[NUM_STA];
	int asoc_sta_count;
	struct  __queue sleep_q;
	struct  __queue wakeup_q;
	struct _adapter *padapter;
	struct list_head asoc_list;
	struct list_head auth_list;
	unsigned int auth_to;  /* sec, time to expire in authenticating. */
	unsigned int assoc_to; /* sec, time to expire before associating. */
	unsigned int expire_to; /* sec , time to expire after associated. */
};

static inline u32 wifi_mac_hash(u8 *mac)
{
	u32 x;

	x = mac[0];
	x = (x << 2) ^ mac[1];
	x = (x << 2) ^ mac[2];
	x = (x << 2) ^ mac[3];
	x = (x << 2) ^ mac[4];
	x = (x << 2) ^ mac[5];
	x ^= x >> 8;
	x  = x & (NUM_STA - 1);
	return x;
}

int _r8712_init_sta_priv(struct sta_priv *pstapriv);
void _r8712_free_sta_priv(struct sta_priv *pstapriv);
struct sta_info *r8712_alloc_stainfo(struct sta_priv *pstapriv,
				     u8 *hwaddr);
void r8712_free_stainfo(struct _adapter *padapter, struct sta_info *psta);
void r8712_free_all_stainfo(struct _adapter *padapter);
struct sta_info *r8712_get_stainfo(struct sta_priv *pstapriv, u8 *hwaddr);
void r8712_init_bcmc_stainfo(struct _adapter *padapter);
struct sta_info *r8712_get_bcmc_stainfo(struct _adapter *padapter);
u8 r8712_access_ctrl(struct wlan_acl_pool *pacl_list, u8 *mac_addr);

#endif /* _STA_INFO_H_ */

