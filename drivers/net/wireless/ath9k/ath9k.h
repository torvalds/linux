/*
 * Copyright (c) 2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef ATH9K_H
#define ATH9K_H

#include <linux/io.h>

#define ATHEROS_VENDOR_ID	0x168c

#define AR5416_DEVID_PCI	0x0023
#define AR5416_DEVID_PCIE	0x0024
#define AR9160_DEVID_PCI	0x0027
#define AR9280_DEVID_PCI	0x0029
#define AR9280_DEVID_PCIE	0x002a

#define AR5416_AR9100_DEVID	0x000b

#define	AR_SUBVENDOR_ID_NOG	0x0e11
#define AR_SUBVENDOR_ID_NEW_A	0x7065

#define ATH9K_TXERR_XRETRY         0x01
#define ATH9K_TXERR_FILT           0x02
#define ATH9K_TXERR_FIFO           0x04
#define ATH9K_TXERR_XTXOP          0x08
#define ATH9K_TXERR_TIMER_EXPIRED  0x10

#define ATH9K_TX_BA                0x01
#define ATH9K_TX_PWRMGMT           0x02
#define ATH9K_TX_DESC_CFG_ERR      0x04
#define ATH9K_TX_DATA_UNDERRUN     0x08
#define ATH9K_TX_DELIM_UNDERRUN    0x10
#define ATH9K_TX_SW_ABORTED        0x40
#define ATH9K_TX_SW_FILTERED       0x80

#define NBBY    8

struct ath_tx_status {
	u32 ts_tstamp;
	u16 ts_seqnum;
	u8 ts_status;
	u8 ts_ratecode;
	u8 ts_rateindex;
	int8_t ts_rssi;
	u8 ts_shortretry;
	u8 ts_longretry;
	u8 ts_virtcol;
	u8 ts_antenna;
	u8 ts_flags;
	int8_t ts_rssi_ctl0;
	int8_t ts_rssi_ctl1;
	int8_t ts_rssi_ctl2;
	int8_t ts_rssi_ext0;
	int8_t ts_rssi_ext1;
	int8_t ts_rssi_ext2;
	u8 pad[3];
	u32 ba_low;
	u32 ba_high;
	u32 evm0;
	u32 evm1;
	u32 evm2;
};

struct ath_rx_status {
	u32 rs_tstamp;
	u16 rs_datalen;
	u8 rs_status;
	u8 rs_phyerr;
	int8_t rs_rssi;
	u8 rs_keyix;
	u8 rs_rate;
	u8 rs_antenna;
	u8 rs_more;
	int8_t rs_rssi_ctl0;
	int8_t rs_rssi_ctl1;
	int8_t rs_rssi_ctl2;
	int8_t rs_rssi_ext0;
	int8_t rs_rssi_ext1;
	int8_t rs_rssi_ext2;
	u8 rs_isaggr;
	u8 rs_moreaggr;
	u8 rs_num_delims;
	u8 rs_flags;
	u32 evm0;
	u32 evm1;
	u32 evm2;
};

#define ATH9K_RXERR_CRC           0x01
#define ATH9K_RXERR_PHY           0x02
#define ATH9K_RXERR_FIFO          0x04
#define ATH9K_RXERR_DECRYPT       0x08
#define ATH9K_RXERR_MIC           0x10

#define ATH9K_RX_MORE             0x01
#define ATH9K_RX_MORE_AGGR        0x02
#define ATH9K_RX_GI               0x04
#define ATH9K_RX_2040             0x08
#define ATH9K_RX_DELIM_CRC_PRE    0x10
#define ATH9K_RX_DELIM_CRC_POST   0x20
#define ATH9K_RX_DECRYPT_BUSY     0x40

#define ATH9K_RXKEYIX_INVALID	((u8)-1)
#define ATH9K_TXKEYIX_INVALID	((u32)-1)

struct ath_desc {
	u32 ds_link;
	u32 ds_data;
	u32 ds_ctl0;
	u32 ds_ctl1;
	u32 ds_hw[20];
	union {
		struct ath_tx_status tx;
		struct ath_rx_status rx;
		void *stats;
	} ds_us;
	void *ds_vdata;
} __packed;

#define	ds_txstat	ds_us.tx
#define	ds_rxstat	ds_us.rx
#define ds_stat		ds_us.stats

#define ATH9K_TXDESC_CLRDMASK		0x0001
#define ATH9K_TXDESC_NOACK		0x0002
#define ATH9K_TXDESC_RTSENA		0x0004
#define ATH9K_TXDESC_CTSENA		0x0008
#define ATH9K_TXDESC_INTREQ		0x0010
#define ATH9K_TXDESC_VEOL		0x0020
#define ATH9K_TXDESC_EXT_ONLY		0x0040
#define ATH9K_TXDESC_EXT_AND_CTL	0x0080
#define ATH9K_TXDESC_VMF		0x0100
#define ATH9K_TXDESC_FRAG_IS_ON 	0x0200

#define ATH9K_RXDESC_INTREQ		0x0020

enum wireless_mode {
	ATH9K_MODE_11A = 0,
	ATH9K_MODE_11B = 2,
	ATH9K_MODE_11G = 3,
	ATH9K_MODE_11NA_HT20 = 6,
	ATH9K_MODE_11NG_HT20 = 7,
	ATH9K_MODE_11NA_HT40PLUS = 8,
	ATH9K_MODE_11NA_HT40MINUS = 9,
	ATH9K_MODE_11NG_HT40PLUS = 10,
	ATH9K_MODE_11NG_HT40MINUS = 11,
	ATH9K_MODE_MAX
};

enum ath9k_hw_caps {
	ATH9K_HW_CAP_CHAN_SPREAD		= BIT(0),
	ATH9K_HW_CAP_MIC_AESCCM                 = BIT(1),
	ATH9K_HW_CAP_MIC_CKIP                   = BIT(2),
	ATH9K_HW_CAP_MIC_TKIP                   = BIT(3),
	ATH9K_HW_CAP_CIPHER_AESCCM              = BIT(4),
	ATH9K_HW_CAP_CIPHER_CKIP                = BIT(5),
	ATH9K_HW_CAP_CIPHER_TKIP                = BIT(6),
	ATH9K_HW_CAP_VEOL                       = BIT(7),
	ATH9K_HW_CAP_BSSIDMASK                  = BIT(8),
	ATH9K_HW_CAP_MCAST_KEYSEARCH            = BIT(9),
	ATH9K_HW_CAP_CHAN_HALFRATE              = BIT(10),
	ATH9K_HW_CAP_CHAN_QUARTERRATE           = BIT(11),
	ATH9K_HW_CAP_HT                         = BIT(12),
	ATH9K_HW_CAP_GTT                        = BIT(13),
	ATH9K_HW_CAP_FASTCC                     = BIT(14),
	ATH9K_HW_CAP_RFSILENT                   = BIT(15),
	ATH9K_HW_CAP_WOW                        = BIT(16),
	ATH9K_HW_CAP_CST                        = BIT(17),
	ATH9K_HW_CAP_ENHANCEDPM                 = BIT(18),
	ATH9K_HW_CAP_AUTOSLEEP                  = BIT(19),
	ATH9K_HW_CAP_4KB_SPLITTRANS             = BIT(20),
	ATH9K_HW_CAP_WOW_MATCHPATTERN_EXACT     = BIT(21),
};

enum ath9k_capability_type {
	ATH9K_CAP_CIPHER = 0,
	ATH9K_CAP_TKIP_MIC,
	ATH9K_CAP_TKIP_SPLIT,
	ATH9K_CAP_PHYCOUNTERS,
	ATH9K_CAP_DIVERSITY,
	ATH9K_CAP_TXPOW,
	ATH9K_CAP_PHYDIAG,
	ATH9K_CAP_MCAST_KEYSRCH,
	ATH9K_CAP_TSF_ADJUST,
	ATH9K_CAP_WME_TKIPMIC,
	ATH9K_CAP_RFSILENT,
	ATH9K_CAP_ANT_CFG_2GHZ,
	ATH9K_CAP_ANT_CFG_5GHZ
};

struct ath9k_hw_capabilities {
	u32 hw_caps; /* ATH9K_HW_CAP_* from ath9k_hw_caps */
	DECLARE_BITMAP(wireless_modes, ATH9K_MODE_MAX); /* ATH9K_MODE_* */
	u16 total_queues;
	u16 keycache_size;
	u16 low_5ghz_chan, high_5ghz_chan;
	u16 low_2ghz_chan, high_2ghz_chan;
	u16 num_mr_retries;
	u16 rts_aggr_limit;
	u8 tx_chainmask;
	u8 rx_chainmask;
	u16 tx_triglevel_max;
	u16 reg_cap;
	u8 num_gpio_pins;
	u8 num_antcfg_2ghz;
	u8 num_antcfg_5ghz;
};

struct ath9k_ops_config {
	int dma_beacon_response_time;
	int sw_beacon_response_time;
	int additional_swba_backoff;
	int ack_6mb;
	int cwm_ignore_extcca;
	u8 pcie_powersave_enable;
	u8 pcie_l1skp_enable;
	u8 pcie_clock_req;
	u32 pcie_waen;
	int pcie_power_reset;
	u8 pcie_restore;
	u8 analog_shiftreg;
	u8 ht_enable;
	u32 ofdm_trig_low;
	u32 ofdm_trig_high;
	u32 cck_trig_high;
	u32 cck_trig_low;
	u32 enable_ani;
	u8 noise_immunity_level;
	u32 ofdm_weaksignal_det;
	u32 cck_weaksignal_thr;
	u8 spur_immunity_level;
	u8 firstep_level;
	int8_t rssi_thr_high;
	int8_t rssi_thr_low;
	u16 diversity_control;
	u16 antenna_switch_swap;
	int serialize_regmode;
	int intr_mitigation;
#define SPUR_DISABLE        	0
#define SPUR_ENABLE_IOCTL   	1
#define SPUR_ENABLE_EEPROM  	2
#define AR_EEPROM_MODAL_SPURS   5
#define AR_SPUR_5413_1      	1640
#define AR_SPUR_5413_2      	1200
#define AR_NO_SPUR      	0x8000
#define AR_BASE_FREQ_2GHZ   	2300
#define AR_BASE_FREQ_5GHZ   	4900
#define AR_SPUR_FEEQ_BOUND_HT40 19
#define AR_SPUR_FEEQ_BOUND_HT20 10
	int spurmode;
	u16 spurchans[AR_EEPROM_MODAL_SPURS][2];
};

enum ath9k_tx_queue {
	ATH9K_TX_QUEUE_INACTIVE = 0,
	ATH9K_TX_QUEUE_DATA,
	ATH9K_TX_QUEUE_BEACON,
	ATH9K_TX_QUEUE_CAB,
	ATH9K_TX_QUEUE_UAPSD,
	ATH9K_TX_QUEUE_PSPOLL
};

#define	ATH9K_NUM_TX_QUEUES 10

enum ath9k_tx_queue_subtype {
	ATH9K_WME_AC_BK = 0,
	ATH9K_WME_AC_BE,
	ATH9K_WME_AC_VI,
	ATH9K_WME_AC_VO,
	ATH9K_WME_UPSD
};

enum ath9k_tx_queue_flags {
	TXQ_FLAG_TXOKINT_ENABLE = 0x0001,
	TXQ_FLAG_TXERRINT_ENABLE = 0x0001,
	TXQ_FLAG_TXDESCINT_ENABLE = 0x0002,
	TXQ_FLAG_TXEOLINT_ENABLE = 0x0004,
	TXQ_FLAG_TXURNINT_ENABLE = 0x0008,
	TXQ_FLAG_BACKOFF_DISABLE = 0x0010,
	TXQ_FLAG_COMPRESSION_ENABLE = 0x0020,
	TXQ_FLAG_RDYTIME_EXP_POLICY_ENABLE = 0x0040,
	TXQ_FLAG_FRAG_BURST_BACKOFF_ENABLE = 0x0080,
};

#define ATH9K_TXQ_USEDEFAULT ((u32) -1)

#define ATH9K_DECOMP_MASK_SIZE     128
#define ATH9K_READY_TIME_LO_BOUND  50
#define ATH9K_READY_TIME_HI_BOUND  96

enum ath9k_pkt_type {
	ATH9K_PKT_TYPE_NORMAL = 0,
	ATH9K_PKT_TYPE_ATIM,
	ATH9K_PKT_TYPE_PSPOLL,
	ATH9K_PKT_TYPE_BEACON,
	ATH9K_PKT_TYPE_PROBE_RESP,
	ATH9K_PKT_TYPE_CHIRP,
	ATH9K_PKT_TYPE_GRP_POLL,
};

struct ath9k_tx_queue_info {
	u32 tqi_ver;
	enum ath9k_tx_queue tqi_type;
	enum ath9k_tx_queue_subtype tqi_subtype;
	enum ath9k_tx_queue_flags tqi_qflags;
	u32 tqi_priority;
	u32 tqi_aifs;
	u32 tqi_cwmin;
	u32 tqi_cwmax;
	u16 tqi_shretry;
	u16 tqi_lgretry;
	u32 tqi_cbrPeriod;
	u32 tqi_cbrOverflowLimit;
	u32 tqi_burstTime;
	u32 tqi_readyTime;
	u32 tqi_physCompBuf;
	u32 tqi_intFlags;
};

enum ath9k_rx_filter {
	ATH9K_RX_FILTER_UCAST = 0x00000001,
	ATH9K_RX_FILTER_MCAST = 0x00000002,
	ATH9K_RX_FILTER_BCAST = 0x00000004,
	ATH9K_RX_FILTER_CONTROL = 0x00000008,
	ATH9K_RX_FILTER_BEACON = 0x00000010,
	ATH9K_RX_FILTER_PROM = 0x00000020,
	ATH9K_RX_FILTER_PROBEREQ = 0x00000080,
	ATH9K_RX_FILTER_PSPOLL = 0x00004000,
	ATH9K_RX_FILTER_PHYERR = 0x00000100,
	ATH9K_RX_FILTER_PHYRADAR = 0x00002000,
};

enum ath9k_int {
	ATH9K_INT_RX = 0x00000001,
	ATH9K_INT_RXDESC = 0x00000002,
	ATH9K_INT_RXNOFRM = 0x00000008,
	ATH9K_INT_RXEOL = 0x00000010,
	ATH9K_INT_RXORN = 0x00000020,
	ATH9K_INT_TX = 0x00000040,
	ATH9K_INT_TXDESC = 0x00000080,
	ATH9K_INT_TIM_TIMER = 0x00000100,
	ATH9K_INT_TXURN = 0x00000800,
	ATH9K_INT_MIB = 0x00001000,
	ATH9K_INT_RXPHY = 0x00004000,
	ATH9K_INT_RXKCM = 0x00008000,
	ATH9K_INT_SWBA = 0x00010000,
	ATH9K_INT_BMISS = 0x00040000,
	ATH9K_INT_BNR = 0x00100000,
	ATH9K_INT_TIM = 0x00200000,
	ATH9K_INT_DTIM = 0x00400000,
	ATH9K_INT_DTIMSYNC = 0x00800000,
	ATH9K_INT_GPIO = 0x01000000,
	ATH9K_INT_CABEND = 0x02000000,
	ATH9K_INT_CST = 0x10000000,
	ATH9K_INT_GTT = 0x20000000,
	ATH9K_INT_FATAL = 0x40000000,
	ATH9K_INT_GLOBAL = 0x80000000,
	ATH9K_INT_BMISC = ATH9K_INT_TIM |
		ATH9K_INT_DTIM |
		ATH9K_INT_DTIMSYNC |
		ATH9K_INT_CABEND,
	ATH9K_INT_COMMON = ATH9K_INT_RXNOFRM |
		ATH9K_INT_RXDESC |
		ATH9K_INT_RXEOL |
		ATH9K_INT_RXORN |
		ATH9K_INT_TXURN |
		ATH9K_INT_TXDESC |
		ATH9K_INT_MIB |
		ATH9K_INT_RXPHY |
		ATH9K_INT_RXKCM |
		ATH9K_INT_SWBA |
		ATH9K_INT_BMISS |
		ATH9K_INT_GPIO,
	ATH9K_INT_NOCARD = 0xffffffff
};

struct ath9k_rate_table {
	int rateCount;
	u8 rateCodeToIndex[256];
	struct {
		u8 valid;
		u8 phy;
		u32 rateKbps;
		u8 rateCode;
		u8 shortPreamble;
		u8 dot11Rate;
		u8 controlRate;
		u16 lpAckDuration;
		u16 spAckDuration;
	} info[32];
};

#define ATH9K_RATESERIES_RTS_CTS  0x0001
#define ATH9K_RATESERIES_2040     0x0002
#define ATH9K_RATESERIES_HALFGI   0x0004

struct ath9k_11n_rate_series {
	u32 Tries;
	u32 Rate;
	u32 PktDuration;
	u32 ChSel;
	u32 RateFlags;
};

#define CHANNEL_CW_INT    0x00002
#define CHANNEL_CCK       0x00020
#define CHANNEL_OFDM      0x00040
#define CHANNEL_2GHZ      0x00080
#define CHANNEL_5GHZ      0x00100
#define CHANNEL_PASSIVE   0x00200
#define CHANNEL_DYN       0x00400
#define CHANNEL_HALF      0x04000
#define CHANNEL_QUARTER   0x08000
#define CHANNEL_HT20      0x10000
#define CHANNEL_HT40PLUS  0x20000
#define CHANNEL_HT40MINUS 0x40000

#define CHANNEL_INTERFERENCE    0x01
#define CHANNEL_DFS             0x02
#define CHANNEL_4MS_LIMIT       0x04
#define CHANNEL_DFS_CLEAR       0x08
#define CHANNEL_DISALLOW_ADHOC  0x10
#define CHANNEL_PER_11D_ADHOC   0x20

#define CHANNEL_A           (CHANNEL_5GHZ|CHANNEL_OFDM)
#define CHANNEL_B           (CHANNEL_2GHZ|CHANNEL_CCK)
#define CHANNEL_G           (CHANNEL_2GHZ|CHANNEL_OFDM)
#define CHANNEL_G_HT20      (CHANNEL_2GHZ|CHANNEL_HT20)
#define CHANNEL_A_HT20      (CHANNEL_5GHZ|CHANNEL_HT20)
#define CHANNEL_G_HT40PLUS  (CHANNEL_2GHZ|CHANNEL_HT40PLUS)
#define CHANNEL_G_HT40MINUS (CHANNEL_2GHZ|CHANNEL_HT40MINUS)
#define CHANNEL_A_HT40PLUS  (CHANNEL_5GHZ|CHANNEL_HT40PLUS)
#define CHANNEL_A_HT40MINUS (CHANNEL_5GHZ|CHANNEL_HT40MINUS)
#define CHANNEL_ALL				\
	(CHANNEL_OFDM|				\
	 CHANNEL_CCK|				\
	 CHANNEL_2GHZ |				\
	 CHANNEL_5GHZ |				\
	 CHANNEL_HT20 |				\
	 CHANNEL_HT40PLUS |			\
	 CHANNEL_HT40MINUS)

struct ath9k_channel {
	u16 channel;
	u32 channelFlags;
	u8 privFlags;
	int8_t maxRegTxPower;
	int8_t maxTxPower;
	int8_t minTxPower;
	u32 chanmode;
	int32_t CalValid;
	bool oneTimeCalsDone;
	int8_t iCoff;
	int8_t qCoff;
	int16_t rawNoiseFloor;
	int8_t antennaMax;
	u32 regDmnFlags;
	u32 conformanceTestLimit[3]; /* 0:11a, 1: 11b, 2:11g */
#ifdef ATH_NF_PER_CHAN
	struct ath9k_nfcal_hist nfCalHist[NUM_NF_READINGS];
#endif
};

#define IS_CHAN_A(_c) ((((_c)->channelFlags & CHANNEL_A) == CHANNEL_A) || \
       (((_c)->channelFlags & CHANNEL_A_HT20) == CHANNEL_A_HT20) || \
       (((_c)->channelFlags & CHANNEL_A_HT40PLUS) == CHANNEL_A_HT40PLUS) || \
       (((_c)->channelFlags & CHANNEL_A_HT40MINUS) == CHANNEL_A_HT40MINUS))
#define IS_CHAN_B(_c) (((_c)->channelFlags & CHANNEL_B) == CHANNEL_B)
#define IS_CHAN_G(_c) ((((_c)->channelFlags & (CHANNEL_G)) == CHANNEL_G) || \
       (((_c)->channelFlags & CHANNEL_G_HT20) == CHANNEL_G_HT20) || \
       (((_c)->channelFlags & CHANNEL_G_HT40PLUS) == CHANNEL_G_HT40PLUS) || \
       (((_c)->channelFlags & CHANNEL_G_HT40MINUS) == CHANNEL_G_HT40MINUS))
#define IS_CHAN_CCK(_c) (((_c)->channelFlags & CHANNEL_CCK) != 0)
#define IS_CHAN_OFDM(_c) (((_c)->channelFlags & CHANNEL_OFDM) != 0)
#define IS_CHAN_5GHZ(_c) (((_c)->channelFlags & CHANNEL_5GHZ) != 0)
#define IS_CHAN_2GHZ(_c) (((_c)->channelFlags & CHANNEL_2GHZ) != 0)
#define IS_CHAN_PASSIVE(_c) (((_c)->channelFlags & CHANNEL_PASSIVE) != 0)
#define IS_CHAN_HALF_RATE(_c) (((_c)->channelFlags & CHANNEL_HALF) != 0)
#define IS_CHAN_QUARTER_RATE(_c) (((_c)->channelFlags & CHANNEL_QUARTER) != 0)

/* These macros check chanmode and not channelFlags */
#define IS_CHAN_HT20(_c) (((_c)->chanmode == CHANNEL_A_HT20) ||	\
			  ((_c)->chanmode == CHANNEL_G_HT20))
#define IS_CHAN_HT40(_c) (((_c)->chanmode == CHANNEL_A_HT40PLUS) ||	\
			  ((_c)->chanmode == CHANNEL_A_HT40MINUS) ||	\
			  ((_c)->chanmode == CHANNEL_G_HT40PLUS) ||	\
			  ((_c)->chanmode == CHANNEL_G_HT40MINUS))
#define IS_CHAN_HT(_c) (IS_CHAN_HT20((_c)) || IS_CHAN_HT40((_c)))

#define IS_CHAN_IN_PUBLIC_SAFETY_BAND(_c) ((_c) > 4940 && (_c) < 4990)
#define IS_CHAN_A_5MHZ_SPACED(_c)			\
	((((_c)->channelFlags & CHANNEL_5GHZ) != 0) &&	\
	 (((_c)->channel % 20) != 0) &&			\
	 (((_c)->channel % 10) != 0))

struct ath9k_keyval {
	u8 kv_type;
	u8 kv_pad;
	u16 kv_len;
	u8 kv_val[16];
	u8 kv_mic[8];
	u8 kv_txmic[8];
};

enum ath9k_key_type {
	ATH9K_KEY_TYPE_CLEAR,
	ATH9K_KEY_TYPE_WEP,
	ATH9K_KEY_TYPE_AES,
	ATH9K_KEY_TYPE_TKIP,
};

enum ath9k_cipher {
	ATH9K_CIPHER_WEP = 0,
	ATH9K_CIPHER_AES_OCB = 1,
	ATH9K_CIPHER_AES_CCM = 2,
	ATH9K_CIPHER_CKIP = 3,
	ATH9K_CIPHER_TKIP = 4,
	ATH9K_CIPHER_CLR = 5,
	ATH9K_CIPHER_MIC = 127
};

#define AR_EEPROM_EEPCAP_COMPRESS_DIS   0x0001
#define AR_EEPROM_EEPCAP_AES_DIS        0x0002
#define AR_EEPROM_EEPCAP_FASTFRAME_DIS  0x0004
#define AR_EEPROM_EEPCAP_BURST_DIS      0x0008
#define AR_EEPROM_EEPCAP_MAXQCU         0x01F0
#define AR_EEPROM_EEPCAP_MAXQCU_S       4
#define AR_EEPROM_EEPCAP_HEAVY_CLIP_EN  0x0200
#define AR_EEPROM_EEPCAP_KC_ENTRIES     0xF000
#define AR_EEPROM_EEPCAP_KC_ENTRIES_S   12

#define AR_EEPROM_EEREGCAP_EN_FCC_MIDBAND   0x0040
#define AR_EEPROM_EEREGCAP_EN_KK_U1_EVEN    0x0080
#define AR_EEPROM_EEREGCAP_EN_KK_U2         0x0100
#define AR_EEPROM_EEREGCAP_EN_KK_MIDBAND    0x0200
#define AR_EEPROM_EEREGCAP_EN_KK_U1_ODD     0x0400
#define AR_EEPROM_EEREGCAP_EN_KK_NEW_11A    0x0800

#define AR_EEPROM_EEREGCAP_EN_KK_U1_ODD_PRE4_0  0x4000
#define AR_EEPROM_EEREGCAP_EN_KK_NEW_11A_PRE4_0 0x8000

#define SD_NO_CTL               0xE0
#define NO_CTL                  0xff
#define CTL_MODE_M              7
#define CTL_11A                 0
#define CTL_11B                 1
#define CTL_11G                 2
#define CTL_2GHT20              5
#define CTL_5GHT20              6
#define CTL_2GHT40              7
#define CTL_5GHT40              8

#define AR_EEPROM_MAC(i)        (0x1d+(i))

#define AR_EEPROM_RFSILENT_GPIO_SEL     0x001c
#define AR_EEPROM_RFSILENT_GPIO_SEL_S   2
#define AR_EEPROM_RFSILENT_POLARITY     0x0002
#define AR_EEPROM_RFSILENT_POLARITY_S   1

#define CTRY_DEBUG 0x1ff
#define	CTRY_DEFAULT 0

enum reg_ext_bitmap {
	REG_EXT_JAPAN_MIDBAND = 1,
	REG_EXT_FCC_DFS_HT40 = 2,
	REG_EXT_JAPAN_NONDFS_HT40 = 3,
	REG_EXT_JAPAN_DFS_HT40 = 4
};

struct ath9k_country_entry {
	u16 countryCode;
	u16 regDmnEnum;
	u16 regDmn5G;
	u16 regDmn2G;
	u8 isMultidomain;
	u8 iso[3];
};

#define REG_WRITE(_ah, _reg, _val) iowrite32(_val, _ah->ah_sh + _reg)
#define REG_READ(_ah, _reg) ioread32(_ah->ah_sh + _reg)

#define SM(_v, _f)  (((_v) << _f##_S) & _f)
#define MS(_v, _f)  (((_v) & _f) >> _f##_S)
#define REG_RMW(_a, _r, _set, _clr)    \
	REG_WRITE(_a, _r, (REG_READ(_a, _r) & ~(_clr)) | (_set))
#define REG_RMW_FIELD(_a, _r, _f, _v) \
	REG_WRITE(_a, _r, \
	(REG_READ(_a, _r) & ~_f) | (((_v) << _f##_S) & _f))
#define REG_SET_BIT(_a, _r, _f) \
	REG_WRITE(_a, _r, REG_READ(_a, _r) | _f)
#define REG_CLR_BIT(_a, _r, _f) \
	REG_WRITE(_a, _r, REG_READ(_a, _r) & ~_f)

#define ATH9K_TXQ_USE_LOCKOUT_BKOFF_DIS   0x00000001

#define INIT_AIFS       2
#define INIT_CWMIN      15
#define INIT_CWMIN_11B  31
#define INIT_CWMAX      1023
#define INIT_SH_RETRY   10
#define INIT_LG_RETRY   10
#define INIT_SSH_RETRY  32
#define INIT_SLG_RETRY  32

#define WLAN_CTRL_FRAME_SIZE (2+2+6+4)

#define ATH_AMPDU_LIMIT_MAX      (64 * 1024 - 1)
#define ATH_AMPDU_LIMIT_DEFAULT  ATH_AMPDU_LIMIT_MAX

#define IEEE80211_WEP_IVLEN      3
#define IEEE80211_WEP_KIDLEN     1
#define IEEE80211_WEP_CRCLEN     4
#define IEEE80211_MAX_MPDU_LEN  (3840 + FCS_LEN +		\
				 (IEEE80211_WEP_IVLEN +		\
				  IEEE80211_WEP_KIDLEN +	\
				  IEEE80211_WEP_CRCLEN))
#define IEEE80211_MAX_LEN       (2300 + FCS_LEN +		\
				 (IEEE80211_WEP_IVLEN +		\
				  IEEE80211_WEP_KIDLEN +	\
				  IEEE80211_WEP_CRCLEN))

#define MAX_RATE_POWER 63

enum ath9k_power_mode {
	ATH9K_PM_AWAKE = 0,
	ATH9K_PM_FULL_SLEEP,
	ATH9K_PM_NETWORK_SLEEP,
	ATH9K_PM_UNDEFINED
};

struct ath9k_mib_stats {
	u32 ackrcv_bad;
	u32 rts_bad;
	u32 rts_good;
	u32 fcs_bad;
	u32 beacons;
};

enum ath9k_ant_setting {
	ATH9K_ANT_VARIABLE = 0,
	ATH9K_ANT_FIXED_A,
	ATH9K_ANT_FIXED_B
};

enum ath9k_opmode {
	ATH9K_M_STA = 1,
	ATH9K_M_IBSS = 0,
	ATH9K_M_HOSTAP = 6,
	ATH9K_M_MONITOR = 8
};

#define ATH9K_SLOT_TIME_6 6
#define ATH9K_SLOT_TIME_9 9
#define ATH9K_SLOT_TIME_20 20

enum ath9k_ht_macmode {
	ATH9K_HT_MACMODE_20 = 0,
	ATH9K_HT_MACMODE_2040 = 1,
};

enum ath9k_ht_extprotspacing {
	ATH9K_HT_EXTPROTSPACING_20 = 0,
	ATH9K_HT_EXTPROTSPACING_25 = 1,
};

struct ath9k_ht_cwm {
	enum ath9k_ht_macmode ht_macmode;
	enum ath9k_ht_extprotspacing ht_extprotspacing;
};

enum ath9k_ani_cmd {
	ATH9K_ANI_PRESENT = 0x1,
	ATH9K_ANI_NOISE_IMMUNITY_LEVEL = 0x2,
	ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION = 0x4,
	ATH9K_ANI_CCK_WEAK_SIGNAL_THR = 0x8,
	ATH9K_ANI_FIRSTEP_LEVEL = 0x10,
	ATH9K_ANI_SPUR_IMMUNITY_LEVEL = 0x20,
	ATH9K_ANI_MODE = 0x40,
	ATH9K_ANI_PHYERR_RESET = 0x80,
	ATH9K_ANI_ALL = 0xff
};

enum phytype {
	PHY_DS,
	PHY_FH,
	PHY_OFDM,
	PHY_HT,
};
#define PHY_CCK PHY_DS

enum ath9k_tp_scale {
	ATH9K_TP_SCALE_MAX = 0,
	ATH9K_TP_SCALE_50,
	ATH9K_TP_SCALE_25,
	ATH9K_TP_SCALE_12,
	ATH9K_TP_SCALE_MIN
};

enum ser_reg_mode {
	SER_REG_MODE_OFF = 0,
	SER_REG_MODE_ON = 1,
	SER_REG_MODE_AUTO = 2,
};

#define AR_PHY_CCA_MAX_GOOD_VALUE      		-85
#define AR_PHY_CCA_MAX_HIGH_VALUE      		-62
#define AR_PHY_CCA_MIN_BAD_VALUE       		-121
#define AR_PHY_CCA_FILTERWINDOW_LENGTH_INIT     3
#define AR_PHY_CCA_FILTERWINDOW_LENGTH          5

#define ATH9K_NF_CAL_HIST_MAX           5
#define NUM_NF_READINGS                 6

struct ath9k_nfcal_hist {
	int16_t nfCalBuffer[ATH9K_NF_CAL_HIST_MAX];
	u8 currIndex;
	int16_t privNF;
	u8 invalidNFcount;
};

struct ath9k_beacon_state {
	u32 bs_nexttbtt;
	u32 bs_nextdtim;
	u32 bs_intval;
#define ATH9K_BEACON_PERIOD       0x0000ffff
#define ATH9K_BEACON_ENA          0x00800000
#define ATH9K_BEACON_RESET_TSF    0x01000000
	u32 bs_dtimperiod;
	u16 bs_cfpperiod;
	u16 bs_cfpmaxduration;
	u32 bs_cfpnext;
	u16 bs_timoffset;
	u16 bs_bmissthreshold;
	u32 bs_sleepduration;
};

struct ath9k_node_stats {
	u32 ns_avgbrssi;
	u32 ns_avgrssi;
	u32 ns_avgtxrssi;
	u32 ns_avgtxrate;
};

#define ATH9K_RSSI_EP_MULTIPLIER  (1<<7)

enum ath9k_gpio_output_mux_type {
	ATH9K_GPIO_OUTPUT_MUX_AS_OUTPUT,
	ATH9K_GPIO_OUTPUT_MUX_AS_PCIE_ATTENTION_LED,
	ATH9K_GPIO_OUTPUT_MUX_AS_PCIE_POWER_LED,
	ATH9K_GPIO_OUTPUT_MUX_AS_MAC_NETWORK_LED,
	ATH9K_GPIO_OUTPUT_MUX_AS_MAC_POWER_LED,
	ATH9K_GPIO_OUTPUT_MUX_NUM_ENTRIES
};

enum {
	ATH9K_RESET_POWER_ON,
	ATH9K_RESET_WARM,
	ATH9K_RESET_COLD,
};

#define AH_USE_EEPROM   0x1

struct ath_hal {
	u32 ah_magic;
	u16 ah_devid;
	u16 ah_subvendorid;
	u32 ah_macVersion;
	u16 ah_macRev;
	u16 ah_phyRev;
	u16 ah_analog5GhzRev;
	u16 ah_analog2GhzRev;

	void __iomem *ah_sh;
	struct ath_softc *ah_sc;
	enum ath9k_opmode ah_opmode;
	struct ath9k_ops_config ah_config;
	struct ath9k_hw_capabilities ah_caps;

	u16 ah_countryCode;
	u32 ah_flags;
	int16_t ah_powerLimit;
	u16 ah_maxPowerLevel;
	u32 ah_tpScale;
	u16 ah_currentRD;
	u16 ah_currentRDExt;
	u16 ah_currentRDInUse;
	u16 ah_currentRD5G;
	u16 ah_currentRD2G;
	char ah_iso[4];

	struct ath9k_channel ah_channels[150];
	struct ath9k_channel *ah_curchan;
	u32 ah_nchan;

	u16 ah_rfsilent;
	bool ah_rfkillEnabled;
	bool ah_isPciExpress;
	u16 ah_txTrigLevel;

#ifndef ATH_NF_PER_CHAN
	struct ath9k_nfcal_hist nfCalHist[NUM_NF_READINGS];
#endif
};

struct chan_centers {
	u16 synth_center;
	u16 ctl_center;
	u16 ext_center;
};

int ath_hal_getcapability(struct ath_hal *ah,
			  enum ath9k_capability_type type,
			  u32 capability,
			  u32 *result);
const struct ath9k_rate_table *ath9k_hw_getratetable(struct ath_hal *ah,
						     u32 mode);
void ath9k_hw_detach(struct ath_hal *ah);
struct ath_hal *ath9k_hw_attach(u16 devid,
				struct ath_softc *sc,
				void __iomem *mem,
				int *error);
bool ath9k_regd_init_channels(struct ath_hal *ah,
			      u32 maxchans, u32 *nchans,
			      u8 *regclassids,
			      u32 maxregids, u32 *nregids,
			      u16 cc,
			      bool enableOutdoor,
			      bool enableExtendedChannels);
u32 ath9k_hw_mhz2ieee(struct ath_hal *ah, u32 freq, u32 flags);
enum ath9k_int ath9k_hw_set_interrupts(struct ath_hal *ah,
				     enum ath9k_int ints);
bool ath9k_hw_reset(struct ath_hal *ah,
		    struct ath9k_channel *chan,
		    enum ath9k_ht_macmode macmode,
		    u8 txchainmask, u8 rxchainmask,
		    enum ath9k_ht_extprotspacing extprotspacing,
		    bool bChannelChange,
		    int *status);
bool ath9k_hw_phy_disable(struct ath_hal *ah);
void ath9k_hw_reset_calvalid(struct ath_hal *ah, struct ath9k_channel *chan,
			     bool *isCalDone);
void ath9k_hw_ani_monitor(struct ath_hal *ah,
			  const struct ath9k_node_stats *stats,
			  struct ath9k_channel *chan);
bool ath9k_hw_calibrate(struct ath_hal *ah,
			struct ath9k_channel *chan,
			u8 rxchainmask,
			bool longcal,
			bool *isCalDone);
int16_t ath9k_hw_getchan_noise(struct ath_hal *ah,
			       struct ath9k_channel *chan);
void ath9k_hw_write_associd(struct ath_hal *ah, const u8 *bssid,
			    u16 assocId);
void ath9k_hw_setrxfilter(struct ath_hal *ah, u32 bits);
void ath9k_hw_write_associd(struct ath_hal *ah, const u8 *bssid,
			    u16 assocId);
bool ath9k_hw_stoptxdma(struct ath_hal *ah, u32 q);
void ath9k_hw_reset_tsf(struct ath_hal *ah);
bool ath9k_hw_keyisvalid(struct ath_hal *ah, u16 entry);
bool ath9k_hw_keysetmac(struct ath_hal *ah, u16 entry,
			const u8 *mac);
bool ath9k_hw_set_keycache_entry(struct ath_hal *ah,
				 u16 entry,
				 const struct ath9k_keyval *k,
				 const u8 *mac,
				 int xorKey);
bool ath9k_hw_set_tsfadjust(struct ath_hal *ah,
			    u32 setting);
void ath9k_hw_configpcipowersave(struct ath_hal *ah, int restore);
bool ath9k_hw_intrpend(struct ath_hal *ah);
bool ath9k_hw_getisr(struct ath_hal *ah, enum ath9k_int *masked);
bool ath9k_hw_updatetxtriglevel(struct ath_hal *ah,
				bool bIncTrigLevel);
void ath9k_hw_procmibevent(struct ath_hal *ah,
			   const struct ath9k_node_stats *stats);
bool ath9k_hw_setrxabort(struct ath_hal *ah, bool set);
void ath9k_hw_set11nmac2040(struct ath_hal *ah, enum ath9k_ht_macmode mode);
bool ath9k_hw_phycounters(struct ath_hal *ah);
bool ath9k_hw_keyreset(struct ath_hal *ah, u16 entry);
bool ath9k_hw_getcapability(struct ath_hal *ah,
			    enum ath9k_capability_type type,
			    u32 capability,
			    u32 *result);
bool ath9k_hw_setcapability(struct ath_hal *ah,
			    enum ath9k_capability_type type,
			    u32 capability,
			    u32 setting,
			    int *status);
u32 ath9k_hw_getdefantenna(struct ath_hal *ah);
void ath9k_hw_getmac(struct ath_hal *ah, u8 *mac);
void ath9k_hw_getbssidmask(struct ath_hal *ah, u8 *mask);
bool ath9k_hw_setbssidmask(struct ath_hal *ah,
			   const u8 *mask);
bool ath9k_hw_setpower(struct ath_hal *ah,
		       enum ath9k_power_mode mode);
enum ath9k_int ath9k_hw_intrget(struct ath_hal *ah);
u64 ath9k_hw_gettsf64(struct ath_hal *ah);
u32 ath9k_hw_getdefantenna(struct ath_hal *ah);
bool ath9k_hw_setslottime(struct ath_hal *ah, u32 us);
bool ath9k_hw_setantennaswitch(struct ath_hal *ah,
			       enum ath9k_ant_setting settings,
			       struct ath9k_channel *chan,
			       u8 *tx_chainmask,
			       u8 *rx_chainmask,
			       u8 *antenna_cfgd);
void ath9k_hw_setantenna(struct ath_hal *ah, u32 antenna);
int ath9k_hw_select_antconfig(struct ath_hal *ah,
			      u32 cfg);
bool ath9k_hw_puttxbuf(struct ath_hal *ah, u32 q,
		       u32 txdp);
bool ath9k_hw_txstart(struct ath_hal *ah, u32 q);
u16 ath9k_hw_computetxtime(struct ath_hal *ah,
				 const struct ath9k_rate_table *rates,
				 u32 frameLen, u16 rateix,
				 bool shortPreamble);
void ath9k_hw_set11n_ratescenario(struct ath_hal *ah, struct ath_desc *ds,
				  struct ath_desc *lastds,
				  u32 durUpdateEn, u32 rtsctsRate,
				  u32 rtsctsDuration,
				  struct ath9k_11n_rate_series series[],
				  u32 nseries, u32 flags);
void ath9k_hw_set11n_burstduration(struct ath_hal *ah,
				   struct ath_desc *ds,
				   u32 burstDuration);
void ath9k_hw_cleartxdesc(struct ath_hal *ah, struct ath_desc *ds);
u32 ath9k_hw_reverse_bits(u32 val, u32 n);
bool ath9k_hw_resettxqueue(struct ath_hal *ah, u32 q);
u32 ath9k_regd_get_ctl(struct ath_hal *ah, struct ath9k_channel *chan);
u32 ath9k_regd_get_antenna_allowed(struct ath_hal *ah,
				     struct ath9k_channel *chan);
u32 ath9k_hw_mhz2ieee(struct ath_hal *ah, u32 freq, u32 flags);
bool ath9k_hw_get_txq_props(struct ath_hal *ah, int q,
			    struct ath9k_tx_queue_info *qinfo);
bool ath9k_hw_set_txq_props(struct ath_hal *ah, int q,
			    const struct ath9k_tx_queue_info *qinfo);
struct ath9k_channel *ath9k_regd_check_channel(struct ath_hal *ah,
					      const struct ath9k_channel *c);
void ath9k_hw_set11n_txdesc(struct ath_hal *ah, struct ath_desc *ds,
			    u32 pktLen, enum ath9k_pkt_type type,
			    u32 txPower, u32 keyIx,
			    enum ath9k_key_type keyType, u32 flags);
bool ath9k_hw_filltxdesc(struct ath_hal *ah, struct ath_desc *ds,
			 u32 segLen, bool firstSeg,
			 bool lastSeg,
			 const struct ath_desc *ds0);
u32 ath9k_hw_GetMibCycleCountsPct(struct ath_hal *ah,
					u32 *rxc_pcnt,
					u32 *rxf_pcnt,
					u32 *txf_pcnt);
void ath9k_hw_dmaRegDump(struct ath_hal *ah);
void ath9k_hw_beaconinit(struct ath_hal *ah,
			 u32 next_beacon, u32 beacon_period);
void ath9k_hw_set_sta_beacon_timers(struct ath_hal *ah,
				    const struct ath9k_beacon_state *bs);
bool ath9k_hw_setuprxdesc(struct ath_hal *ah, struct ath_desc *ds,
			  u32 size, u32 flags);
void ath9k_hw_putrxbuf(struct ath_hal *ah, u32 rxdp);
void ath9k_hw_rxena(struct ath_hal *ah);
void ath9k_hw_setopmode(struct ath_hal *ah);
bool ath9k_hw_setmac(struct ath_hal *ah, const u8 *mac);
void ath9k_hw_setmcastfilter(struct ath_hal *ah, u32 filter0,
			     u32 filter1);
u32 ath9k_hw_getrxfilter(struct ath_hal *ah);
void ath9k_hw_startpcureceive(struct ath_hal *ah);
void ath9k_hw_stoppcurecv(struct ath_hal *ah);
bool ath9k_hw_stopdmarecv(struct ath_hal *ah);
int ath9k_hw_rxprocdesc(struct ath_hal *ah,
			struct ath_desc *ds, u32 pa,
			struct ath_desc *nds, u64 tsf);
u32 ath9k_hw_gettxbuf(struct ath_hal *ah, u32 q);
int ath9k_hw_txprocdesc(struct ath_hal *ah,
			struct ath_desc *ds);
void ath9k_hw_set11n_aggr_middle(struct ath_hal *ah, struct ath_desc *ds,
				 u32 numDelims);
void ath9k_hw_set11n_aggr_first(struct ath_hal *ah, struct ath_desc *ds,
				u32 aggrLen);
void ath9k_hw_set11n_aggr_last(struct ath_hal *ah, struct ath_desc *ds);
bool ath9k_hw_releasetxqueue(struct ath_hal *ah, u32 q);
void ath9k_hw_gettxintrtxqs(struct ath_hal *ah, u32 *txqs);
void ath9k_hw_clr11n_aggr(struct ath_hal *ah, struct ath_desc *ds);
void ath9k_hw_set11n_virtualmorefrag(struct ath_hal *ah,
				     struct ath_desc *ds, u32 vmf);
bool ath9k_hw_set_txpowerlimit(struct ath_hal *ah, u32 limit);
bool ath9k_regd_is_public_safety_sku(struct ath_hal *ah);
int ath9k_hw_setuptxqueue(struct ath_hal *ah, enum ath9k_tx_queue type,
			  const struct ath9k_tx_queue_info *qinfo);
u32 ath9k_hw_numtxpending(struct ath_hal *ah, u32 q);
const char *ath9k_hw_probe(u16 vendorid, u16 devid);
bool ath9k_hw_disable(struct ath_hal *ah);
void ath9k_hw_rfdetach(struct ath_hal *ah);
void ath9k_hw_get_channel_centers(struct ath_hal *ah,
				  struct ath9k_channel *chan,
				  struct chan_centers *centers);
bool ath9k_get_channel_edges(struct ath_hal *ah,
			     u16 flags, u16 *low,
			     u16 *high);
#endif
