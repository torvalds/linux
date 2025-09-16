/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __STA_INFO_H_
#define __STA_INFO_H_


#define IBSS_START_MAC_ID	2
#define NUM_STA 32
#define NUM_ACL 16


/* if mode == 0, then the sta is allowed once the addr is hit. */
/* if mode == 1, then the sta is rejected once the addr is non-hit. */
struct rtw_wlan_acl_node {
	struct list_head list;
	u8		 addr[ETH_ALEN];
	u8		 valid;
};

/* mode = 0, disable */
/* mode = 1, accept unless in deny list */
/* mode =2, deny unless in accept list */
struct wlan_acl_pool {
	int mode;
	int num;
	struct rtw_wlan_acl_node aclnode[NUM_ACL];
	struct __queue	acl_node_q;
};

struct rssi_sta {
	s32	UndecoratedSmoothedPWDB;
	s32	UndecoratedSmoothedCCK;
	s32	UndecoratedSmoothedOFDM;
	u64	PacketMap;
	u8 ValidBit;
};

struct	stainfo_stats	{

	u64 rx_mgnt_pkts;
		u64 rx_beacon_pkts;
		u64 rx_probereq_pkts;
		u64 rx_probersp_pkts;
		u64 rx_probersp_bm_pkts;
		u64 rx_probersp_uo_pkts;
	u64 rx_ctrl_pkts;
	u64 rx_data_pkts;

	u64	last_rx_mgnt_pkts;
		u64 last_rx_beacon_pkts;
		u64 last_rx_probereq_pkts;
		u64 last_rx_probersp_pkts;
		u64 last_rx_probersp_bm_pkts;
		u64 last_rx_probersp_uo_pkts;
	u64	last_rx_ctrl_pkts;
	u64	last_rx_data_pkts;

	u64	rx_bytes;
	u64	rx_drops;

	u64	tx_pkts;
	u64	tx_bytes;
	u64  tx_drops;
};

struct sta_info {

	spinlock_t	lock;
	struct list_head	list; /* free_sta_queue */
	struct list_head	hash_list; /* sta_hash */
	struct adapter *padapter;

	struct sta_xmit_priv sta_xmitpriv;
	struct sta_recv_priv sta_recvpriv;

	struct __queue sleep_q;
	unsigned int sleepq_len;

	uint state;
	uint aid;
	uint mac_id;
	uint qos_option;
	u8 hwaddr[ETH_ALEN];

	bool ieee8021x_blocked;
	uint	dot118021XPrivacy; /* aes, tkip... */
	union Keytype	dot11tkiptxmickey;
	union Keytype	dot11tkiprxmickey;
	union Keytype	dot118021x_UncstKey;
	union pn48		dot11txpn;			/*  PN48 used for Unicast xmit */
	union pn48		dot11wtxpn;			/*  PN48 used for Unicast mgmt xmit. */
	union pn48		dot11rxpn;			/*  PN48 used for Unicast recv. */


	u8 bssrateset[16];
	u32 bssratelen;
	s32  rssi;
	s32	signal_quality;

	u8 cts2self;
	u8 rtsen;

	u8 raid;
	u8 init_rate;
	u32 ra_mask;
	u8 wireless_mode;	/*  NETWORK_TYPE */
	u8 bw_mode;

	u8 ldpc;
	u8 stbc;

	struct stainfo_stats sta_stats;

	/* for A-MPDU TX, ADDBA timeout check */
	struct timer_list addba_retry_timer;

	/* for A-MPDU Rx reordering buffer control */
	struct recv_reorder_ctrl recvreorder_ctrl[16];

	/* for A-MPDU Tx */
	/* unsigned char 	ampdu_txen_bitmap; */
	u16 BA_starting_seqctrl[16];


	struct ht_priv htpriv;

	/* Notes: */
	/* STA_Mode: */
	/* curr_network(mlme_priv/security_priv/qos/ht) + sta_info: (STA & AP) CAP/INFO */
	/* scan_q: AP CAP/INFO */

	/* AP_Mode: */
	/* curr_network(mlme_priv/security_priv/qos/ht) : AP CAP/INFO */
	/* sta_info: (AP & STA) CAP/INFO */

	struct list_head asoc_list;
	struct list_head auth_list;

	unsigned int expire_to;
	unsigned int auth_seq;
	unsigned int authalg;
	unsigned char chg_txt[128];

	u16 capability;
	int flags;

	int dot8021xalg;/* 0:disable, 1:psk, 2:802.1x */
	int wpa_psk;/* 0:disable, bit(0): WPA, bit(1):WPA2 */
	int wpa_group_cipher;
	int wpa2_group_cipher;
	int wpa_pairwise_cipher;
	int wpa2_pairwise_cipher;

	u8 bpairwise_key_installed;

	u8 wpa_ie[32];

	u8 nonerp_set;
	u8 no_short_slot_time_set;
	u8 no_short_preamble_set;
	u8 no_ht_gf_set;
	u8 no_ht_set;
	u8 ht_20mhz_set;

	unsigned int tx_ra_bitmap;
	u8 qos_info;

	u8 max_sp_len;
	u8 uapsd_bk;/* BIT(0): Delivery enabled, BIT(1): Trigger enabled */
	u8 uapsd_be;
	u8 uapsd_vi;
	u8 uapsd_vo;

	u8 has_legacy_ac;
	unsigned int sleepq_ac_len;

	u8 under_exist_checking;

	u8 keep_alive_trycnt;

	u8 *passoc_req;
	u32 assoc_req_len;

	/* for DM */
	struct rssi_sta	 rssi_stat;

	/* ODM_STA_INFO_T */
	/*  ================ODM Relative Info ======================= */
	/*  Please be care, dont declare too much structure here. It will cost memory * STA support num. */
	/*  */
	/*  */
	/*  2011/10/20 MH Add for ODM STA info. */
	/*  */
	/*  Driver Write */
	u8 bValid;				/*  record the sta status link or not? */
	u8 IOTPeer;			/*  Enum value.	HT_IOT_PEER_E */
	/*  ODM Write */
	/* 1 PHY_STATUS_INFO */
	u8 RSSI_Path[4];		/*  */
	u8 RSSI_Ave;
	u8 RXEVM[4];
	u8 RXSNR[4];

	u8 rssi_level;			/* for Refresh RA mask */
	/*  ODM Write */
	/* 1 TX_INFO (may changed by IC) */
	/* TX_INFO_T		pTxInfo;		 Define in IC folder. Move lower layer. */
	/*  */
	/*  ================ODM Relative Info ======================= */
	/*  */

	/* To store the sequence number of received management frame */
	u16 RxMgmtFrameSeqNum;
};

#define sta_rx_pkts(sta) \
	(sta->sta_stats.rx_mgnt_pkts \
	+ sta->sta_stats.rx_ctrl_pkts \
	+ sta->sta_stats.rx_data_pkts)

#define sta_rx_data_pkts(sta) \
	(sta->sta_stats.rx_data_pkts)

#define sta_last_rx_data_pkts(sta) \
	(sta->sta_stats.last_rx_data_pkts)

#define sta_rx_beacon_pkts(sta) \
	(sta->sta_stats.rx_beacon_pkts)

#define sta_last_rx_beacon_pkts(sta) \
	(sta->sta_stats.last_rx_beacon_pkts)

#define sta_rx_probersp_pkts(sta) \
	(sta->sta_stats.rx_probersp_pkts)

#define sta_last_rx_probersp_pkts(sta) \
	(sta->sta_stats.last_rx_probersp_pkts)

#define sta_update_last_rx_pkts(sta) \
	do { \
		sta->sta_stats.last_rx_mgnt_pkts = sta->sta_stats.rx_mgnt_pkts; \
		sta->sta_stats.last_rx_beacon_pkts = sta->sta_stats.rx_beacon_pkts; \
		sta->sta_stats.last_rx_probereq_pkts = sta->sta_stats.rx_probereq_pkts; \
		sta->sta_stats.last_rx_probersp_pkts = sta->sta_stats.rx_probersp_pkts; \
		sta->sta_stats.last_rx_probersp_bm_pkts = sta->sta_stats.rx_probersp_bm_pkts; \
		sta->sta_stats.last_rx_probersp_uo_pkts = sta->sta_stats.rx_probersp_uo_pkts; \
		sta->sta_stats.last_rx_ctrl_pkts = sta->sta_stats.rx_ctrl_pkts; \
		sta->sta_stats.last_rx_data_pkts = sta->sta_stats.rx_data_pkts; \
	} while (0)

#define STA_PKTS_FMT "(m:%llu, c:%llu, d:%llu)"

struct	sta_priv {

	u8 *pallocated_stainfo_buf;
	u8 *pstainfo_buf;
	struct __queue	free_sta_queue;

	spinlock_t sta_hash_lock;
	struct list_head   sta_hash[NUM_STA];
	int asoc_sta_count;
	struct __queue sleep_q;
	struct __queue wakeup_q;

	struct adapter *padapter;

	struct list_head asoc_list;
	struct list_head auth_list;
	spinlock_t asoc_list_lock;
	spinlock_t auth_list_lock;
	u8 asoc_list_cnt;
	u8 auth_list_cnt;

	unsigned int auth_to;  /* sec, time to expire in authenticating. */
	unsigned int assoc_to; /* sec, time to expire before associating. */
	unsigned int expire_to; /* sec , time to expire after associated. */

	/* pointers to STA info; based on allocated AID or NULL if AID free
	 * AID is in the range 1-2007, so sta_aid[0] corresponders to AID 1
	 * and so on
	 */
	struct sta_info *sta_aid[NUM_STA];

	u16 sta_dz_bitmap;/* only support for 15 stations, aid bitmap for sleeping stations. */
	u16 tim_bitmap;/* only support 15 stations, aid = 0~15 mapping bit0~bit15 */

	u16 max_num_sta;

	struct wlan_acl_pool acl_list;
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


extern u32 _rtw_init_sta_priv(struct sta_priv *pstapriv);
extern u32 _rtw_free_sta_priv(struct sta_priv *pstapriv);

#define stainfo_offset_valid(offset) (offset < NUM_STA && offset >= 0)
int rtw_stainfo_offset(struct sta_priv *stapriv, struct sta_info *sta);
struct sta_info *rtw_get_stainfo_by_offset(struct sta_priv *stapriv, int offset);

extern struct sta_info *rtw_alloc_stainfo(struct	sta_priv *pstapriv, u8 *hwaddr);
extern u32 rtw_free_stainfo(struct adapter *padapter, struct sta_info *psta);
extern void rtw_free_all_stainfo(struct adapter *padapter);
extern struct sta_info *rtw_get_stainfo(struct sta_priv *pstapriv, u8 *hwaddr);
extern u32 rtw_init_bcmc_stainfo(struct adapter *padapter);
extern struct sta_info *rtw_get_bcmc_stainfo(struct adapter *padapter);
extern u8 rtw_access_ctrl(struct adapter *padapter, u8 *mac_addr);

#endif /* _STA_INFO_H_ */
