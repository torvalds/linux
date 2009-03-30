#ifndef P54COMMON_H
#define P54COMMON_H

/*
 * Common code specific definitions for mac80211 Prism54 drivers
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007, Christian Lamparter <chunkeey@web.de>
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

struct bootrec {
	__le32 code;
	__le32 len;
	u32 data[10];
} __attribute__((packed));

#define PDR_SYNTH_FRONTEND_MASK		0x0007
#define PDR_SYNTH_FRONTEND_DUETTE3	0x0001
#define PDR_SYNTH_FRONTEND_DUETTE2	0x0002
#define PDR_SYNTH_FRONTEND_FRISBEE	0x0003
#define PDR_SYNTH_FRONTEND_XBOW		0x0004
#define PDR_SYNTH_FRONTEND_LONGBOW	0x0005
#define PDR_SYNTH_IQ_CAL_MASK		0x0018
#define PDR_SYNTH_IQ_CAL_PA_DETECTOR	0x0000
#define PDR_SYNTH_IQ_CAL_DISABLED	0x0008
#define PDR_SYNTH_IQ_CAL_ZIF		0x0010
#define PDR_SYNTH_FAA_SWITCH_MASK	0x0020
#define PDR_SYNTH_FAA_SWITCH_ENABLED	0x0020
#define PDR_SYNTH_24_GHZ_MASK		0x0040
#define PDR_SYNTH_24_GHZ_DISABLED	0x0040
#define PDR_SYNTH_5_GHZ_MASK		0x0080
#define PDR_SYNTH_5_GHZ_DISABLED	0x0080
#define PDR_SYNTH_RX_DIV_MASK		0x0100
#define PDR_SYNTH_RX_DIV_SUPPORTED	0x0100
#define PDR_SYNTH_TX_DIV_MASK		0x0200
#define PDR_SYNTH_TX_DIV_SUPPORTED	0x0200

struct bootrec_exp_if {
	__le16 role;
	__le16 if_id;
	__le16 variant;
	__le16 btm_compat;
	__le16 top_compat;
} __attribute__((packed));

#define BR_DESC_PRIV_CAP_WEP		BIT(0)
#define BR_DESC_PRIV_CAP_TKIP		BIT(1)
#define BR_DESC_PRIV_CAP_MICHAEL	BIT(2)
#define BR_DESC_PRIV_CAP_CCX_CP		BIT(3)
#define BR_DESC_PRIV_CAP_CCX_MIC	BIT(4)
#define BR_DESC_PRIV_CAP_AESCCMP	BIT(5)

struct bootrec_desc {
	__le16 modes;
	__le16 flags;
	__le32 rx_start;
	__le32 rx_end;
	u8 headroom;
	u8 tailroom;
	u8 tx_queues;
	u8 tx_depth;
	u8 privacy_caps;
	u8 rx_keycache_size;
	u8 time_size;
	u8 padding;
	u8 rates[16];
	u8 padding2[4];
	__le16 rx_mtu;
} __attribute__((packed));

#define BR_CODE_MIN			0x80000000
#define BR_CODE_COMPONENT_ID		0x80000001
#define BR_CODE_COMPONENT_VERSION	0x80000002
#define BR_CODE_DEPENDENT_IF		0x80000003
#define BR_CODE_EXPOSED_IF		0x80000004
#define BR_CODE_DESCR			0x80000101
#define BR_CODE_MAX			0x8FFFFFFF
#define BR_CODE_END_OF_BRA		0xFF0000FF
#define LEGACY_BR_CODE_END_OF_BRA	0xFFFFFFFF

#define P54_HDR_FLAG_DATA_ALIGN		BIT(14)
#define P54_HDR_FLAG_DATA_OUT_PROMISC	BIT(0)
#define P54_HDR_FLAG_DATA_OUT_TIMESTAMP BIT(1)
#define P54_HDR_FLAG_DATA_OUT_SEQNR	BIT(2)
#define P54_HDR_FLAG_DATA_OUT_BIT3	BIT(3)
#define P54_HDR_FLAG_DATA_OUT_BURST	BIT(4)
#define P54_HDR_FLAG_DATA_OUT_NOCANCEL	BIT(5)
#define P54_HDR_FLAG_DATA_OUT_CLEARTIM	BIT(6)
#define P54_HDR_FLAG_DATA_OUT_HITCHHIKE	BIT(7)
#define P54_HDR_FLAG_DATA_OUT_COMPRESS	BIT(8)
#define P54_HDR_FLAG_DATA_OUT_CONCAT	BIT(9)
#define P54_HDR_FLAG_DATA_OUT_PCS_ACCEPT BIT(10)
#define P54_HDR_FLAG_DATA_OUT_WAITEOSP	BIT(11)

#define P54_HDR_FLAG_DATA_IN_FCS_GOOD	BIT(0)
#define P54_HDR_FLAG_DATA_IN_MATCH_MAC	BIT(1)
#define P54_HDR_FLAG_DATA_IN_MCBC	BIT(2)
#define P54_HDR_FLAG_DATA_IN_BEACON	BIT(3)
#define P54_HDR_FLAG_DATA_IN_MATCH_BSS	BIT(4)
#define P54_HDR_FLAG_DATA_IN_BCAST_BSS	BIT(5)
#define P54_HDR_FLAG_DATA_IN_DATA	BIT(6)
#define P54_HDR_FLAG_DATA_IN_TRUNCATED	BIT(7)
#define P54_HDR_FLAG_DATA_IN_BIT8	BIT(8)
#define P54_HDR_FLAG_DATA_IN_TRANSPARENT BIT(9)

/* PDA defines are Copyright (C) 2005 Nokia Corporation (taken from islsm_pda.h) */

struct pda_entry {
	__le16 len;	/* includes both code and data */
	__le16 code;
	u8 data[0];
} __attribute__ ((packed));

struct eeprom_pda_wrap {
	__le32 magic;
	__le16 pad;
	__le16 len;
	__le32 arm_opcode;
	u8 data[0];
} __attribute__ ((packed));

struct p54_iq_autocal_entry {
	__le16 iq_param[4];
} __attribute__ ((packed));

struct pda_iq_autocal_entry {
        __le16 freq;
	struct p54_iq_autocal_entry params;
} __attribute__ ((packed));

struct pda_channel_output_limit {
	__le16 freq;
	u8 val_bpsk;
	u8 val_qpsk;
	u8 val_16qam;
	u8 val_64qam;
	u8 rate_set_mask;
	u8 rate_set_size;
} __attribute__ ((packed));

struct pda_pa_curve_data_sample_rev0 {
	u8 rf_power;
	u8 pa_detector;
	u8 pcv;
} __attribute__ ((packed));

struct pda_pa_curve_data_sample_rev1 {
	u8 rf_power;
	u8 pa_detector;
	u8 data_barker;
	u8 data_bpsk;
	u8 data_qpsk;
	u8 data_16qam;
	u8 data_64qam;
} __attribute__ ((packed));

struct p54_pa_curve_data_sample {
	u8 rf_power;
	u8 pa_detector;
	u8 data_barker;
	u8 data_bpsk;
	u8 data_qpsk;
	u8 data_16qam;
	u8 data_64qam;
	u8 padding;
} __attribute__ ((packed));

struct pda_pa_curve_data {
	u8 cal_method_rev;
	u8 channels;
	u8 points_per_channel;
	u8 padding;
	u8 data[0];
} __attribute__ ((packed));

struct pda_rssi_cal_entry {
	__le16 mul;
	__le16 add;
} __attribute__ ((packed));

struct pda_country {
	u8 regdomain;
	u8 alpha2[2];
	u8 flags;
} __attribute__ ((packed));

/*
 * Warning: Longbow's structures are bogus.
 */
struct p54_channel_output_limit_longbow {
	__le16 rf_power_points[12];
} __attribute__ ((packed));

struct p54_pa_curve_data_sample_longbow {
	__le16 rf_power;
	__le16 pa_detector;
	struct {
		__le16 data[4];
	} points[3] __attribute__ ((packed));
} __attribute__ ((packed));

struct pda_custom_wrapper {
	__le16 entries;
	__le16 entry_size;
	__le16 offset;
	__le16 len;
	u8 data[0];
} __attribute__ ((packed));

/*
 * this defines the PDR codes used to build PDAs as defined in document
 * number 553155. The current implementation mirrors version 1.1 of the
 * document and lists only PDRs supported by the ARM platform.
 */

/* common and choice range (0x0000 - 0x0fff) */
#define PDR_END					0x0000
#define PDR_MANUFACTURING_PART_NUMBER		0x0001
#define PDR_PDA_VERSION				0x0002
#define PDR_NIC_SERIAL_NUMBER			0x0003

#define PDR_MAC_ADDRESS				0x0101
#define PDR_REGULATORY_DOMAIN_LIST		0x0103
#define PDR_TEMPERATURE_TYPE			0x0107

#define PDR_PRISM_PCI_IDENTIFIER		0x0402

/* ARM range (0x1000 - 0x1fff) */
#define PDR_COUNTRY_INFORMATION			0x1000
#define PDR_INTERFACE_LIST			0x1001
#define PDR_HARDWARE_PLATFORM_COMPONENT_ID	0x1002
#define PDR_OEM_NAME				0x1003
#define PDR_PRODUCT_NAME			0x1004
#define PDR_UTF8_OEM_NAME			0x1005
#define PDR_UTF8_PRODUCT_NAME			0x1006
#define PDR_COUNTRY_LIST			0x1007
#define PDR_DEFAULT_COUNTRY			0x1008

#define PDR_ANTENNA_GAIN			0x1100

#define PDR_PRISM_INDIGO_PA_CALIBRATION_DATA	0x1901
#define PDR_RSSI_LINEAR_APPROXIMATION		0x1902
#define PDR_PRISM_PA_CAL_OUTPUT_POWER_LIMITS	0x1903
#define PDR_PRISM_PA_CAL_CURVE_DATA		0x1904
#define PDR_RSSI_LINEAR_APPROXIMATION_DUAL_BAND	0x1905
#define PDR_PRISM_ZIF_TX_IQ_CALIBRATION		0x1906
#define PDR_REGULATORY_POWER_LIMITS		0x1907
#define PDR_RSSI_LINEAR_APPROXIMATION_EXTENDED	0x1908
#define PDR_RADIATED_TRANSMISSION_CORRECTION	0x1909
#define PDR_PRISM_TX_IQ_CALIBRATION		0x190a

/* reserved range (0x2000 - 0x7fff) */

/* customer range (0x8000 - 0xffff) */
#define PDR_BASEBAND_REGISTERS				0x8000
#define PDR_PER_CHANNEL_BASEBAND_REGISTERS		0x8001

/* used by our modificated eeprom image */
#define PDR_RSSI_LINEAR_APPROXIMATION_CUSTOM		0xDEAD
#define PDR_PRISM_PA_CAL_OUTPUT_POWER_LIMITS_CUSTOM	0xBEEF
#define PDR_PRISM_PA_CAL_CURVE_DATA_CUSTOM		0xB05D

/* PDR definitions for default country & country list */
#define PDR_COUNTRY_CERT_CODE		0x80
#define PDR_COUNTRY_CERT_CODE_REAL	0x00
#define PDR_COUNTRY_CERT_CODE_PSEUDO	0x80
#define PDR_COUNTRY_CERT_BAND		0x40
#define PDR_COUNTRY_CERT_BAND_2GHZ	0x00
#define PDR_COUNTRY_CERT_BAND_5GHZ	0x40
#define PDR_COUNTRY_CERT_IODOOR		0x30
#define PDR_COUNTRY_CERT_IODOOR_BOTH	0x00
#define PDR_COUNTRY_CERT_IODOOR_INDOOR	0x20
#define PDR_COUNTRY_CERT_IODOOR_OUTDOOR	0x30
#define PDR_COUNTRY_CERT_INDEX		0x0F

struct p54_eeprom_lm86 {
	union {
		struct {
			__le16 offset;
			__le16 len;
			u8 data[0];
		} v1;
		struct {
			__le32 offset;
			__le16 len;
			u8 magic2;
			u8 pad;
			u8 magic[4];
			u8 data[0];
		} v2;
	}  __attribute__ ((packed));
} __attribute__ ((packed));

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
} __attribute__ ((packed));

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
} __attribute__ ((packed));

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
} __attribute__ ((packed));

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
		} __attribute__((packed)) longbow;
		struct {
			u8 output_power;
			u8 cts_rate;
			u8 unalloc;
		} __attribute__ ((packed)) normal;
	} __attribute__ ((packed));
	u8 unalloc2[2];
	u8 align[0];
} __attribute__ ((packed));

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
		} v1 __attribute__ ((packed));
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
		} v2 __attribute__ ((packed));
	} __attribute__ ((packed));
} __attribute__ ((packed));

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
} __attribute__ ((packed));

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
} __attribute__ ((packed));

struct p54_scan_body_longbow {
	struct p54_channel_output_limit_longbow power_limits;
	struct p54_pa_curve_data_sample_longbow curve_data[8];
	__le16 unkn[6];		/* maybe more power_limits or rate_mask */
} __attribute__ ((packed));

union p54_scan_body_union {
	struct p54_scan_body normal;
	struct p54_scan_body_longbow longbow;
} __attribute__ ((packed));

struct p54_scan_tail_rate {
	__le32 basic_rate_mask;
	u8 rts_rates[8];
} __attribute__ ((packed));

struct p54_led {
	__le16 flags;
	__le16 mask[2];
	__le16 delay[2];
} __attribute__ ((packed));

struct p54_edcf {
	u8 flags;
	u8 slottime;
	u8 sifs;
	u8 eofpad;
	struct p54_edcf_queue_param queue[8];
	u8 mapping[4];
	__le16 frameburst;
	__le16 round_trip_delay;
} __attribute__ ((packed));

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
} __attribute__ ((packed));

struct p54_xbow_synth {
	__le16 magic1;
	__le16 magic2;
	__le16 freq;
	u32 padding[5];
} __attribute__ ((packed));

struct p54_timer {
	__le32 interval;
} __attribute__ ((packed));

struct p54_keycache {
	u8 entry;
	u8 key_id;
	u8 mac[ETH_ALEN];
	u8 padding[2];
	u8 key_type;
	u8 key_len;
	u8 key[24];
} __attribute__ ((packed));

struct p54_burst {
	u8 flags;
	u8 queue;
	u8 backlog;
	u8 pad;
	__le16 durations[32];
} __attribute__ ((packed));

struct p54_psm_interval {
	__le16 interval;
	__le16 periods;
} __attribute__ ((packed));

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
} __attribute__ ((packed));

#define MC_FILTER_ADDRESS_NUM 4

struct p54_group_address_table {
	__le16 filter_enable;
	__le16 num_address;
	u8 mac_list[MC_FILTER_ADDRESS_NUM][ETH_ALEN];
} __attribute__ ((packed));

struct p54_txcancel {
	__le32 req_id;
} __attribute__ ((packed));

struct p54_sta_unlock {
	u8 addr[ETH_ALEN];
	u16 padding;
} __attribute__ ((packed));

#define P54_TIM_CLEAR BIT(15)
struct p54_tim {
	u8 count;
	u8 padding[3];
	__le16 entry[8];
} __attribute__ ((packed));

struct p54_cce_quiet {
	__le32 period;
} __attribute__ ((packed));

struct p54_bt_balancer {
	__le16 prio_thresh;
	__le16 acl_thresh;
} __attribute__ ((packed));

struct p54_arp_table {
	__le16 filter_enable;
	u8 ipv4_addr[4];
} __attribute__ ((packed));

#endif /* P54COMMON_H */
