/*
 * LMAC Interface specific definitions for mac80211 Prism54 drivers
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007 - 2009, Christian Lamparter <chunkeey@web.de>
 *
 * Based on:
 * - the islsm (softmac prism54) driver, which is:
 *   Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * - LMAC API interface header file for STLC4560 (lmac_longbow.h)
 *   Copyright (C) 2007 Conexant Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef LMAC_H
#define LMAC_H

enum p54_control_frame_types {
	P54_CONTROL_TYPE_SETUP = 0,
	P54_CONTROL_TYPE_SCAN,
	P54_CONTROL_TYPE_TRAP,
	P54_CONTROL_TYPE_DCFINIT,
	P54_CONTROL_TYPE_RX_KEYCACHE,
	P54_CONTROL_TYPE_TIM,
	P54_CONTROL_TYPE_PSM,
	P54_CONTROL_TYPE_TXCANCEL,
	P54_CONTROL_TYPE_TXDONE,
	P54_CONTROL_TYPE_BURST,
	P54_CONTROL_TYPE_STAT_READBACK,
	P54_CONTROL_TYPE_BBP,
	P54_CONTROL_TYPE_EEPROM_READBACK,
	P54_CONTROL_TYPE_LED,
	P54_CONTROL_TYPE_GPIO,
	P54_CONTROL_TYPE_TIMER,
	P54_CONTROL_TYPE_MODULATION,
	P54_CONTROL_TYPE_SYNTH_CONFIG,
	P54_CONTROL_TYPE_DETECTOR_VALUE,
	P54_CONTROL_TYPE_XBOW_SYNTH_CFG,
	P54_CONTROL_TYPE_CCE_QUIET,
	P54_CONTROL_TYPE_PSM_STA_UNLOCK,
	P54_CONTROL_TYPE_PCS,
	P54_CONTROL_TYPE_BT_BALANCER = 28,
	P54_CONTROL_TYPE_GROUP_ADDRESS_TABLE = 30,
	P54_CONTROL_TYPE_ARPTABLE = 31,
	P54_CONTROL_TYPE_BT_OPTIONS = 35,
};

#define P54_HDR_FLAG_CONTROL		BIT(15)
#define P54_HDR_FLAG_CONTROL_OPSET	(BIT(15) + BIT(0))
#define P54_HDR_FLAG_DATA_ALIGN		BIT(14)

#define P54_HDR_FLAG_DATA_OUT_PROMISC		BIT(0)
#define P54_HDR_FLAG_DATA_OUT_TIMESTAMP		BIT(1)
#define P54_HDR_FLAG_DATA_OUT_SEQNR		BIT(2)
#define P54_HDR_FLAG_DATA_OUT_BIT3		BIT(3)
#define P54_HDR_FLAG_DATA_OUT_BURST		BIT(4)
#define P54_HDR_FLAG_DATA_OUT_NOCANCEL		BIT(5)
#define P54_HDR_FLAG_DATA_OUT_CLEARTIM		BIT(6)
#define P54_HDR_FLAG_DATA_OUT_HITCHHIKE		BIT(7)
#define P54_HDR_FLAG_DATA_OUT_COMPRESS		BIT(8)
#define P54_HDR_FLAG_DATA_OUT_CONCAT		BIT(9)
#define P54_HDR_FLAG_DATA_OUT_PCS_ACCEPT	BIT(10)
#define P54_HDR_FLAG_DATA_OUT_WAITEOSP		BIT(11)

#define P54_HDR_FLAG_DATA_IN_FCS_GOOD		BIT(0)
#define P54_HDR_FLAG_DATA_IN_MATCH_MAC		BIT(1)
#define P54_HDR_FLAG_DATA_IN_MCBC		BIT(2)
#define P54_HDR_FLAG_DATA_IN_BEACON		BIT(3)
#define P54_HDR_FLAG_DATA_IN_MATCH_BSS		BIT(4)
#define P54_HDR_FLAG_DATA_IN_BCAST_BSS		BIT(5)
#define P54_HDR_FLAG_DATA_IN_DATA		BIT(6)
#define P54_HDR_FLAG_DATA_IN_TRUNCATED		BIT(7)
#define P54_HDR_FLAG_DATA_IN_BIT8		BIT(8)
#define P54_HDR_FLAG_DATA_IN_TRANSPARENT	BIT(9)

struct p54_hdr {
	__le16 flags;
	__le16 len;
	__le32 req_id;
	__le16 type;	/* enum p54_control_frame_types */
	u8 rts_tries;
	u8 tries;
	u8 data[0];
} __packed;

#define GET_REQ_ID(skb)							\
	(((struct p54_hdr *) ((struct sk_buff *) skb)->data)->req_id)	\

#define FREE_AFTER_TX(skb)						\
	((((struct p54_hdr *) ((struct sk_buff *) skb)->data)->		\
	flags) == cpu_to_le16(P54_HDR_FLAG_CONTROL_OPSET))

#define IS_DATA_FRAME(skb)						\
	(!((((struct p54_hdr *) ((struct sk_buff *) skb)->data)->	\
	flags) & cpu_to_le16(P54_HDR_FLAG_CONTROL)))

#define GET_HW_QUEUE(skb)						\
	(((struct p54_tx_data *)((struct p54_hdr *)			\
	skb->data)->data)->hw_queue)

/*
 * shared interface ID definitions
 * The interface ID is a unique identification of a specific interface.
 * The following values are reserved: 0x0000, 0x0002, 0x0012, 0x0014, 0x0015
 */
#define IF_ID_ISL36356A			0x0001	/* ISL36356A <-> Firmware */
#define IF_ID_MVC			0x0003	/* MAC Virtual Coprocessor */
#define IF_ID_DEBUG			0x0008	/* PolDebug Interface */
#define IF_ID_PRODUCT			0x0009
#define IF_ID_OEM			0x000a
#define IF_ID_PCI3877			0x000b	/* 3877 <-> Host PCI */
#define IF_ID_ISL37704C			0x000c	/* ISL37704C <-> Fw */
#define IF_ID_ISL39000			0x000f	/* ISL39000 <-> Fw */
#define IF_ID_ISL39300A			0x0010	/* ISL39300A <-> Fw */
#define IF_ID_ISL37700_UAP		0x0016	/* ISL37700 uAP Fw <-> Fw */
#define IF_ID_ISL39000_UAP		0x0017	/* ISL39000 uAP Fw <-> Fw */
#define IF_ID_LMAC			0x001a	/* Interface exposed by LMAC */

struct exp_if {
	__le16 role;
	__le16 if_id;
	__le16 variant;
	__le16 btm_compat;
	__le16 top_compat;
} __packed;

struct dep_if {
	__le16 role;
	__le16 if_id;
	__le16 variant;
} __packed;

/* driver <-> lmac definitions */
struct p54_eeprom_lm86 {
	union {
		struct {
			__le16 offset;
			__le16 len;
			u8 data[0];
		} __packed v1;
		struct {
			__le32 offset;
			__le16 len;
			u8 magic2;
			u8 pad;
			u8 magic[4];
			u8 data[0];
		} __packed v2;
	}  __packed;
} __packed;

enum p54_rx_decrypt_status {
	P54_DECRYPT_NONE = 0,
	P54_DECRYPT_OK,
	P54_DECRYPT_NOKEY,
	P54_DECRYPT_NOMICHAEL,
	P54_DECRYPT_NOCKIPMIC,
	P54_DECRYPT_FAIL_WEP,
	P54_DECRYPT_FAIL_TKIP,
	P54_DECRYPT_FAIL_MICHAEL,
	P54_DECRYPT_FAIL_CKIPKP,
	P54_DECRYPT_FAIL_CKIPMIC,
	P54_DECRYPT_FAIL_AESCCMP
};

struct p54_rx_data {
	__le16 flags;
	__le16 len;
	__le16 freq;
	u8 antenna;
	u8 rate;
	u8 rssi;
	u8 quality;
	u8 decrypt_status;
	u8 rssi_raw;
	__le32 tsf32;
	__le32 unalloc0;
	u8 align[0];
} __packed;

enum p54_trap_type {
	P54_TRAP_SCAN = 0,
	P54_TRAP_TIMER,
	P54_TRAP_BEACON_TX,
	P54_TRAP_FAA_RADIO_ON,
	P54_TRAP_FAA_RADIO_OFF,
	P54_TRAP_RADAR,
	P54_TRAP_NO_BEACON,
	P54_TRAP_TBTT,
	P54_TRAP_SCO_ENTER,
	P54_TRAP_SCO_EXIT
};

struct p54_trap {
	__le16 event;
	__le16 frequency;
} __packed;

enum p54_frame_sent_status {
	P54_TX_OK = 0,
	P54_TX_FAILED,
	P54_TX_PSM,
	P54_TX_PSM_CANCELLED = 4
};

struct p54_frame_sent {
	u8 status;
	u8 tries;
	u8 ack_rssi;
	u8 quality;
	__le16 seq;
	u8 antenna;
	u8 padding;
} __packed;

enum p54_tx_data_crypt {
	P54_CRYPTO_NONE = 0,
	P54_CRYPTO_WEP,
	P54_CRYPTO_TKIP,
	P54_CRYPTO_TKIPMICHAEL,
	P54_CRYPTO_CCX_WEPMIC,
	P54_CRYPTO_CCX_KPMIC,
	P54_CRYPTO_CCX_KP,
	P54_CRYPTO_AESCCMP
};

enum p54_tx_data_queue {
	P54_QUEUE_BEACON	= 0,
	P54_QUEUE_FWSCAN	= 1,
	P54_QUEUE_MGMT		= 2,
	P54_QUEUE_CAB		= 3,
	P54_QUEUE_DATA		= 4,

	P54_QUEUE_AC_NUM	= 4,
	P54_QUEUE_AC_VO		= 4,
	P54_QUEUE_AC_VI		= 5,
	P54_QUEUE_AC_BE		= 6,
	P54_QUEUE_AC_BK		= 7,

	/* keep last */
	P54_QUEUE_NUM		= 8,
};

#define IS_QOS_QUEUE(n)	(n >= P54_QUEUE_DATA)

struct p54_tx_data {
	u8 rateset[8];
	u8 rts_rate_idx;
	u8 crypt_offset;
	u8 key_type;
	u8 key_len;
	u8 key[16];
	u8 hw_queue;
	u8 backlog;
	__le16 durations[4];
	u8 tx_antenna;
	union {
		struct {
			u8 cts_rate;
			__le16 output_power;
		} __packed longbow;
		struct {
			u8 output_power;
			u8 cts_rate;
			u8 unalloc;
		} __packed normal;
	} __packed;
	u8 unalloc2[2];
	u8 align[0];
} __packed;

/* unit is ms */
#define P54_TX_FRAME_LIFETIME 2000
#define P54_TX_TIMEOUT 4000
#define P54_STATISTICS_UPDATE 5000

#define P54_FILTER_TYPE_NONE		0
#define P54_FILTER_TYPE_STATION		BIT(0)
#define P54_FILTER_TYPE_IBSS		BIT(1)
#define P54_FILTER_TYPE_AP		BIT(2)
#define P54_FILTER_TYPE_TRANSPARENT	BIT(3)
#define P54_FILTER_TYPE_PROMISCUOUS	BIT(4)
#define P54_FILTER_TYPE_HIBERNATE	BIT(5)
#define P54_FILTER_TYPE_NOACK		BIT(6)
#define P54_FILTER_TYPE_RX_DISABLED	BIT(7)

struct p54_setup_mac {
	__le16 mac_mode;
	u8 mac_addr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	u8 rx_antenna;
	u8 rx_align;
	union {
		struct {
			__le32 basic_rate_mask;
			u8 rts_rates[8];
			__le32 rx_addr;
			__le16 max_rx;
			__le16 rxhw;
			__le16 wakeup_timer;
			__le16 unalloc0;
		} __packed v1;
		struct {
			__le32 rx_addr;
			__le16 max_rx;
			__le16 rxhw;
			__le16 timer;
			__le16 truncate;
			__le32 basic_rate_mask;
			u8 sbss_offset;
			u8 mcast_window;
			u8 rx_rssi_threshold;
			u8 rx_ed_threshold;
			__le32 ref_clock;
			__le16 lpf_bandwidth;
			__le16 osc_start_delay;
		} __packed v2;
	} __packed;
} __packed;

#define P54_SETUP_V1_LEN 40
#define P54_SETUP_V2_LEN (sizeof(struct p54_setup_mac))

#define P54_SCAN_EXIT	BIT(0)
#define P54_SCAN_TRAP	BIT(1)
#define P54_SCAN_ACTIVE BIT(2)
#define P54_SCAN_FILTER BIT(3)

struct p54_scan_head {
	__le16 mode;
	__le16 dwell;
	u8 scan_params[20];
	__le16 freq;
} __packed;

struct p54_pa_curve_data_sample {
	u8 rf_power;
	u8 pa_detector;
	u8 data_barker;
	u8 data_bpsk;
	u8 data_qpsk;
	u8 data_16qam;
	u8 data_64qam;
	u8 padding;
} __packed;

struct p54_scan_body {
	u8 pa_points_per_curve;
	u8 val_barker;
	u8 val_bpsk;
	u8 val_qpsk;
	u8 val_16qam;
	u8 val_64qam;
	struct p54_pa_curve_data_sample curve_data[8];
	u8 dup_bpsk;
	u8 dup_qpsk;
	u8 dup_16qam;
	u8 dup_64qam;
} __packed;

/*
 * Warning: Longbow's structures are bogus.
 */
struct p54_channel_output_limit_longbow {
	__le16 rf_power_points[12];
} __packed;

struct p54_pa_curve_data_sample_longbow {
	__le16 rf_power;
	__le16 pa_detector;
	struct {
		__le16 data[4];
	} points[3] __packed;
} __packed;

struct p54_scan_body_longbow {
	struct p54_channel_output_limit_longbow power_limits;
	struct p54_pa_curve_data_sample_longbow curve_data[8];
	__le16 unkn[6];		/* maybe more power_limits or rate_mask */
} __packed;

union p54_scan_body_union {
	struct p54_scan_body normal;
	struct p54_scan_body_longbow longbow;
} __packed;

struct p54_scan_tail_rate {
	__le32 basic_rate_mask;
	u8 rts_rates[8];
} __packed;

struct p54_led {
	__le16 flags;
	__le16 mask[2];
	__le16 delay[2];
} __packed;

struct p54_edcf {
	u8 flags;
	u8 slottime;
	u8 sifs;
	u8 eofpad;
	struct p54_edcf_queue_param queue[8];
	u8 mapping[4];
	__le16 frameburst;
	__le16 round_trip_delay;
} __packed;

struct p54_statistics {
	__le32 rx_success;
	__le32 rx_bad_fcs;
	__le32 rx_abort;
	__le32 rx_abort_phy;
	__le32 rts_success;
	__le32 rts_fail;
	__le32 tsf32;
	__le32 airtime;
	__le32 noise;
	__le32 sample_noise[8];
	__le32 sample_cca;
	__le32 sample_tx;
} __packed;

struct p54_xbow_synth {
	__le16 magic1;
	__le16 magic2;
	__le16 freq;
	u32 padding[5];
} __packed;

struct p54_timer {
	__le32 interval;
} __packed;

struct p54_keycache {
	u8 entry;
	u8 key_id;
	u8 mac[ETH_ALEN];
	u8 padding[2];
	u8 key_type;
	u8 key_len;
	u8 key[24];
} __packed;

struct p54_burst {
	u8 flags;
	u8 queue;
	u8 backlog;
	u8 pad;
	__le16 durations[32];
} __packed;

struct p54_psm_interval {
	__le16 interval;
	__le16 periods;
} __packed;

#define P54_PSM_CAM			0
#define P54_PSM				BIT(0)
#define P54_PSM_DTIM			BIT(1)
#define P54_PSM_MCBC			BIT(2)
#define P54_PSM_CHECKSUM		BIT(3)
#define P54_PSM_SKIP_MORE_DATA		BIT(4)
#define P54_PSM_BEACON_TIMEOUT		BIT(5)
#define P54_PSM_HFOSLEEP		BIT(6)
#define P54_PSM_AUTOSWITCH_SLEEP	BIT(7)
#define P54_PSM_LPIT			BIT(8)
#define P54_PSM_BF_UCAST_SKIP		BIT(9)
#define P54_PSM_BF_MCAST_SKIP		BIT(10)

struct p54_psm {
	__le16 mode;
	__le16 aid;
	struct p54_psm_interval intervals[4];
	u8 beacon_rssi_skip_max;
	u8 rssi_delta_threshold;
	u8 nr;
	u8 exclude[1];
} __packed;

#define MC_FILTER_ADDRESS_NUM 4

struct p54_group_address_table {
	__le16 filter_enable;
	__le16 num_address;
	u8 mac_list[MC_FILTER_ADDRESS_NUM][ETH_ALEN];
} __packed;

struct p54_txcancel {
	__le32 req_id;
} __packed;

struct p54_sta_unlock {
	u8 addr[ETH_ALEN];
	u16 padding;
} __packed;

#define P54_TIM_CLEAR BIT(15)
struct p54_tim {
	u8 count;
	u8 padding[3];
	__le16 entry[8];
} __packed;

struct p54_cce_quiet {
	__le32 period;
} __packed;

struct p54_bt_balancer {
	__le16 prio_thresh;
	__le16 acl_thresh;
} __packed;

struct p54_arp_table {
	__le16 filter_enable;
	u8 ipv4_addr[4];
} __packed;

/* LED control */
int p54_set_leds(struct p54_common *priv);
int p54_init_leds(struct p54_common *priv);
void p54_unregister_leds(struct p54_common *priv);

/* xmit functions */
void p54_tx_80211(struct ieee80211_hw *dev, struct sk_buff *skb);
int p54_tx_cancel(struct p54_common *priv, __le32 req_id);
void p54_tx(struct p54_common *priv, struct sk_buff *skb);

/* synth/phy configuration */
int p54_init_xbow_synth(struct p54_common *priv);
int p54_scan(struct p54_common *priv, u16 mode, u16 dwell);

/* MAC */
int p54_sta_unlock(struct p54_common *priv, u8 *addr);
int p54_update_beacon_tim(struct p54_common *priv, u16 aid, bool set);
int p54_setup_mac(struct p54_common *priv);
int p54_set_ps(struct p54_common *priv);
int p54_fetch_statistics(struct p54_common *priv);

/* e/v DCF setup */
int p54_set_edcf(struct p54_common *priv);

/* cryptographic engine */
int p54_upload_key(struct p54_common *priv, u8 algo, int slot,
		   u8 idx, u8 len, u8 *addr, u8* key);

/* eeprom */
int p54_download_eeprom(struct p54_common *priv, void *buf,
			u16 offset, u16 len);
struct p54_rssi_db_entry *p54_rssi_find(struct p54_common *p, const u16 freq);

/* utility */
u8 *p54_find_ie(struct sk_buff *skb, u8 ie);

#endif /* LMAC_H */
