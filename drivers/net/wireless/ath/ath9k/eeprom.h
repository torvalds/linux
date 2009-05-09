/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
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

#ifndef EEPROM_H
#define EEPROM_H

#include <net/cfg80211.h>

#define AH_USE_EEPROM   0x1

#ifdef __BIG_ENDIAN
#define AR5416_EEPROM_MAGIC 0x5aa5
#else
#define AR5416_EEPROM_MAGIC 0xa55a
#endif

#define CTRY_DEBUG   0x1ff
#define	CTRY_DEFAULT 0

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

#define AR5416_EEPROM_MAGIC_OFFSET  0x0
#define AR5416_EEPROM_S             2
#define AR5416_EEPROM_OFFSET        0x2000
#define AR5416_EEPROM_MAX           0xae0

#define AR5416_EEPROM_START_ADDR \
	(AR_SREV_9100(ah)) ? 0x1fff1000 : 0x503f1200

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

#define EXT_ADDITIVE (0x8000)
#define CTL_11A_EXT (CTL_11A | EXT_ADDITIVE)
#define CTL_11G_EXT (CTL_11G | EXT_ADDITIVE)
#define CTL_11B_EXT (CTL_11B | EXT_ADDITIVE)

#define SUB_NUM_CTL_MODES_AT_5G_40 2
#define SUB_NUM_CTL_MODES_AT_2G_40 3

#define INCREASE_MAXPOW_BY_TWO_CHAIN     6  /* 10*log10(2)*2 */
#define INCREASE_MAXPOW_BY_THREE_CHAIN   10 /* 10*log10(3)*2 */

/*
 * For AR9285 and later chipsets, the following bits are not being programmed
 * in EEPROM and so need to be enabled always.
 *
 * Bit 0: en_fcc_mid
 * Bit 1: en_jap_mid
 * Bit 2: en_fcc_dfs_ht40
 * Bit 3: en_jap_ht40
 * Bit 4: en_jap_dfs_ht40
 */
#define AR9285_RDEXT_DEFAULT    0x1F

#define AR_EEPROM_MAC(i)	(0x1d+(i))
#define ATH9K_POW_SM(_r, _s)	(((_r) & 0x3f) << (_s))
#define FREQ2FBIN(x, y)		((y) ? ((x) - 2300) : (((x) - 4800) / 5))
#define ath9k_hw_use_flash(_ah)	(!(_ah->ah_flags & AH_USE_EEPROM))

#define AR5416_VER_MASK (eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK)
#define OLC_FOR_AR9280_20_LATER (AR_SREV_9280_20_OR_LATER(ah) && \
				 ah->eep_ops->get_eeprom(ah, EEP_OL_PWRCTRL))

#define AR_EEPROM_RFSILENT_GPIO_SEL     0x001c
#define AR_EEPROM_RFSILENT_GPIO_SEL_S   2
#define AR_EEPROM_RFSILENT_POLARITY     0x0002
#define AR_EEPROM_RFSILENT_POLARITY_S   1

#define EEP_RFSILENT_ENABLED        0x0001
#define EEP_RFSILENT_ENABLED_S      0
#define EEP_RFSILENT_POLARITY       0x0002
#define EEP_RFSILENT_POLARITY_S     1
#define EEP_RFSILENT_GPIO_SEL       0x001c
#define EEP_RFSILENT_GPIO_SEL_S     2

#define AR5416_OPFLAGS_11A           0x01
#define AR5416_OPFLAGS_11G           0x02
#define AR5416_OPFLAGS_N_5G_HT40     0x04
#define AR5416_OPFLAGS_N_2G_HT40     0x08
#define AR5416_OPFLAGS_N_5G_HT20     0x10
#define AR5416_OPFLAGS_N_2G_HT20     0x20

#define AR5416_EEP_NO_BACK_VER       0x1
#define AR5416_EEP_VER               0xE
#define AR5416_EEP_VER_MINOR_MASK    0x0FFF
#define AR5416_EEP_MINOR_VER_2       0x2
#define AR5416_EEP_MINOR_VER_3       0x3
#define AR5416_EEP_MINOR_VER_7       0x7
#define AR5416_EEP_MINOR_VER_9       0x9
#define AR5416_EEP_MINOR_VER_16      0x10
#define AR5416_EEP_MINOR_VER_17      0x11
#define AR5416_EEP_MINOR_VER_19      0x13
#define AR5416_EEP_MINOR_VER_20      0x14
#define AR5416_EEP_MINOR_VER_22      0x16

#define AR5416_NUM_5G_CAL_PIERS         8
#define AR5416_NUM_2G_CAL_PIERS         4
#define AR5416_NUM_5G_20_TARGET_POWERS  8
#define AR5416_NUM_5G_40_TARGET_POWERS  8
#define AR5416_NUM_2G_CCK_TARGET_POWERS 3
#define AR5416_NUM_2G_20_TARGET_POWERS  4
#define AR5416_NUM_2G_40_TARGET_POWERS  4
#define AR5416_NUM_CTLS                 24
#define AR5416_NUM_BAND_EDGES           8
#define AR5416_NUM_PD_GAINS             4
#define AR5416_PD_GAINS_IN_MASK         4
#define AR5416_PD_GAIN_ICEPTS           5
#define AR5416_EEPROM_MODAL_SPURS       5
#define AR5416_MAX_RATE_POWER           63
#define AR5416_NUM_PDADC_VALUES         128
#define AR5416_BCHAN_UNUSED             0xFF
#define AR5416_MAX_PWR_RANGE_IN_HALF_DB 64
#define AR5416_MAX_CHAINS               3
#define AR5416_PWR_TABLE_OFFSET         -5

/* Rx gain type values */
#define AR5416_EEP_RXGAIN_23DB_BACKOFF     0
#define AR5416_EEP_RXGAIN_13DB_BACKOFF     1
#define AR5416_EEP_RXGAIN_ORIG             2

/* Tx gain type values */
#define AR5416_EEP_TXGAIN_ORIGINAL         0
#define AR5416_EEP_TXGAIN_HIGH_POWER       1

#define AR5416_EEP4K_START_LOC                64
#define AR5416_EEP4K_NUM_2G_CAL_PIERS         3
#define AR5416_EEP4K_NUM_2G_CCK_TARGET_POWERS 3
#define AR5416_EEP4K_NUM_2G_20_TARGET_POWERS  3
#define AR5416_EEP4K_NUM_2G_40_TARGET_POWERS  3
#define AR5416_EEP4K_NUM_CTLS                 12
#define AR5416_EEP4K_NUM_BAND_EDGES           4
#define AR5416_EEP4K_NUM_PD_GAINS             2
#define AR5416_EEP4K_PD_GAINS_IN_MASK         4
#define AR5416_EEP4K_PD_GAIN_ICEPTS           5
#define AR5416_EEP4K_MAX_CHAINS               1

#define AR9280_TX_GAIN_TABLE_SIZE 22

enum eeprom_param {
	EEP_NFTHRESH_5,
	EEP_NFTHRESH_2,
	EEP_MAC_MSW,
	EEP_MAC_MID,
	EEP_MAC_LSW,
	EEP_REG_0,
	EEP_REG_1,
	EEP_OP_CAP,
	EEP_OP_MODE,
	EEP_RF_SILENT,
	EEP_OB_5,
	EEP_DB_5,
	EEP_OB_2,
	EEP_DB_2,
	EEP_MINOR_REV,
	EEP_TX_MASK,
	EEP_RX_MASK,
	EEP_RXGAIN_TYPE,
	EEP_TXGAIN_TYPE,
	EEP_OL_PWRCTRL,
	EEP_RC_CHAIN_MASK,
	EEP_DAC_HPWR_5G,
	EEP_FRAC_N_5G
};

enum ar5416_rates {
	rate6mb, rate9mb, rate12mb, rate18mb,
	rate24mb, rate36mb, rate48mb, rate54mb,
	rate1l, rate2l, rate2s, rate5_5l,
	rate5_5s, rate11l, rate11s, rateXr,
	rateHt20_0, rateHt20_1, rateHt20_2, rateHt20_3,
	rateHt20_4, rateHt20_5, rateHt20_6, rateHt20_7,
	rateHt40_0, rateHt40_1, rateHt40_2, rateHt40_3,
	rateHt40_4, rateHt40_5, rateHt40_6, rateHt40_7,
	rateDupCck, rateDupOfdm, rateExtCck, rateExtOfdm,
	Ar5416RateSize
};

enum ath9k_hal_freq_band {
	ATH9K_HAL_FREQ_BAND_5GHZ = 0,
	ATH9K_HAL_FREQ_BAND_2GHZ = 1
};

struct base_eep_header {
	u16 length;
	u16 checksum;
	u16 version;
	u8 opCapFlags;
	u8 eepMisc;
	u16 regDmn[2];
	u8 macAddr[6];
	u8 rxMask;
	u8 txMask;
	u16 rfSilent;
	u16 blueToothOptions;
	u16 deviceCap;
	u32 binBuildNumber;
	u8 deviceType;
	u8 pwdclkind;
	u8 futureBase_1[2];
	u8 rxGainType;
	u8 dacHiPwrMode_5G;
	u8 openLoopPwrCntl;
	u8 dacLpMode;
	u8 txGainType;
	u8 rcChainMask;
	u8 desiredScaleCCK;
	u8 power_table_offset;
	u8 frac_n_5g;
	u8 futureBase_3[21];
} __packed;

struct base_eep_header_4k {
	u16 length;
	u16 checksum;
	u16 version;
	u8 opCapFlags;
	u8 eepMisc;
	u16 regDmn[2];
	u8 macAddr[6];
	u8 rxMask;
	u8 txMask;
	u16 rfSilent;
	u16 blueToothOptions;
	u16 deviceCap;
	u32 binBuildNumber;
	u8 deviceType;
	u8 txGainType;
} __packed;


struct spur_chan {
	u16 spurChan;
	u8 spurRangeLow;
	u8 spurRangeHigh;
} __packed;

struct modal_eep_header {
	u32 antCtrlChain[AR5416_MAX_CHAINS];
	u32 antCtrlCommon;
	u8 antennaGainCh[AR5416_MAX_CHAINS];
	u8 switchSettling;
	u8 txRxAttenCh[AR5416_MAX_CHAINS];
	u8 rxTxMarginCh[AR5416_MAX_CHAINS];
	u8 adcDesiredSize;
	u8 pgaDesiredSize;
	u8 xlnaGainCh[AR5416_MAX_CHAINS];
	u8 txEndToXpaOff;
	u8 txEndToRxOn;
	u8 txFrameToXpaOn;
	u8 thresh62;
	u8 noiseFloorThreshCh[AR5416_MAX_CHAINS];
	u8 xpdGain;
	u8 xpd;
	u8 iqCalICh[AR5416_MAX_CHAINS];
	u8 iqCalQCh[AR5416_MAX_CHAINS];
	u8 pdGainOverlap;
	u8 ob;
	u8 db;
	u8 xpaBiasLvl;
	u8 pwrDecreaseFor2Chain;
	u8 pwrDecreaseFor3Chain;
	u8 txFrameToDataStart;
	u8 txFrameToPaOn;
	u8 ht40PowerIncForPdadc;
	u8 bswAtten[AR5416_MAX_CHAINS];
	u8 bswMargin[AR5416_MAX_CHAINS];
	u8 swSettleHt40;
	u8 xatten2Db[AR5416_MAX_CHAINS];
	u8 xatten2Margin[AR5416_MAX_CHAINS];
	u8 ob_ch1;
	u8 db_ch1;
	u8 useAnt1:1,
	    force_xpaon:1,
	    local_bias:1,
	    femBandSelectUsed:1, xlnabufin:1, xlnaisel:2, xlnabufmode:1;
	u8 miscBits;
	u16 xpaBiasLvlFreq[3];
	u8 futureModal[6];

	struct spur_chan spurChans[AR5416_EEPROM_MODAL_SPURS];
} __packed;

struct calDataPerFreqOpLoop {
	u8 pwrPdg[2][5];
	u8 vpdPdg[2][5];
	u8 pcdac[2][5];
	u8 empty[2][5];
} __packed;

struct modal_eep_4k_header {
	u32  antCtrlChain[AR5416_EEP4K_MAX_CHAINS];
	u32  antCtrlCommon;
	u8   antennaGainCh[AR5416_EEP4K_MAX_CHAINS];
	u8   switchSettling;
	u8   txRxAttenCh[AR5416_EEP4K_MAX_CHAINS];
	u8   rxTxMarginCh[AR5416_EEP4K_MAX_CHAINS];
	u8   adcDesiredSize;
	u8   pgaDesiredSize;
	u8   xlnaGainCh[AR5416_EEP4K_MAX_CHAINS];
	u8   txEndToXpaOff;
	u8   txEndToRxOn;
	u8   txFrameToXpaOn;
	u8   thresh62;
	u8   noiseFloorThreshCh[AR5416_EEP4K_MAX_CHAINS];
	u8   xpdGain;
	u8   xpd;
	u8   iqCalICh[AR5416_EEP4K_MAX_CHAINS];
	u8   iqCalQCh[AR5416_EEP4K_MAX_CHAINS];
	u8   pdGainOverlap;
	u8   ob_01;
	u8   db1_01;
	u8   xpaBiasLvl;
	u8   txFrameToDataStart;
	u8   txFrameToPaOn;
	u8   ht40PowerIncForPdadc;
	u8   bswAtten[AR5416_EEP4K_MAX_CHAINS];
	u8   bswMargin[AR5416_EEP4K_MAX_CHAINS];
	u8   swSettleHt40;
	u8   xatten2Db[AR5416_EEP4K_MAX_CHAINS];
	u8   xatten2Margin[AR5416_EEP4K_MAX_CHAINS];
	u8   db2_01;
	u8   version;
	u16  ob_234;
	u16  db1_234;
	u16  db2_234;
	u8   futureModal[4];

	struct spur_chan spurChans[AR5416_EEPROM_MODAL_SPURS];
} __packed;


struct cal_data_per_freq {
	u8 pwrPdg[AR5416_NUM_PD_GAINS][AR5416_PD_GAIN_ICEPTS];
	u8 vpdPdg[AR5416_NUM_PD_GAINS][AR5416_PD_GAIN_ICEPTS];
} __packed;

struct cal_data_per_freq_4k {
	u8 pwrPdg[AR5416_EEP4K_NUM_PD_GAINS][AR5416_EEP4K_PD_GAIN_ICEPTS];
	u8 vpdPdg[AR5416_EEP4K_NUM_PD_GAINS][AR5416_EEP4K_PD_GAIN_ICEPTS];
} __packed;

struct cal_target_power_leg {
	u8 bChannel;
	u8 tPow2x[4];
} __packed;

struct cal_target_power_ht {
	u8 bChannel;
	u8 tPow2x[8];
} __packed;


#ifdef __BIG_ENDIAN_BITFIELD
struct cal_ctl_edges {
	u8 bChannel;
	u8 flag:2, tPower:6;
} __packed;
#else
struct cal_ctl_edges {
	u8 bChannel;
	u8 tPower:6, flag:2;
} __packed;
#endif

struct cal_ctl_data {
	struct cal_ctl_edges
	ctlEdges[AR5416_MAX_CHAINS][AR5416_NUM_BAND_EDGES];
} __packed;

struct cal_ctl_data_4k {
	struct cal_ctl_edges
	ctlEdges[AR5416_EEP4K_MAX_CHAINS][AR5416_EEP4K_NUM_BAND_EDGES];
} __packed;

struct ar5416_eeprom_def {
	struct base_eep_header baseEepHeader;
	u8 custData[64];
	struct modal_eep_header modalHeader[2];
	u8 calFreqPier5G[AR5416_NUM_5G_CAL_PIERS];
	u8 calFreqPier2G[AR5416_NUM_2G_CAL_PIERS];
	struct cal_data_per_freq
	 calPierData5G[AR5416_MAX_CHAINS][AR5416_NUM_5G_CAL_PIERS];
	struct cal_data_per_freq
	 calPierData2G[AR5416_MAX_CHAINS][AR5416_NUM_2G_CAL_PIERS];
	struct cal_target_power_leg
	 calTargetPower5G[AR5416_NUM_5G_20_TARGET_POWERS];
	struct cal_target_power_ht
	 calTargetPower5GHT20[AR5416_NUM_5G_20_TARGET_POWERS];
	struct cal_target_power_ht
	 calTargetPower5GHT40[AR5416_NUM_5G_40_TARGET_POWERS];
	struct cal_target_power_leg
	 calTargetPowerCck[AR5416_NUM_2G_CCK_TARGET_POWERS];
	struct cal_target_power_leg
	 calTargetPower2G[AR5416_NUM_2G_20_TARGET_POWERS];
	struct cal_target_power_ht
	 calTargetPower2GHT20[AR5416_NUM_2G_20_TARGET_POWERS];
	struct cal_target_power_ht
	 calTargetPower2GHT40[AR5416_NUM_2G_40_TARGET_POWERS];
	u8 ctlIndex[AR5416_NUM_CTLS];
	struct cal_ctl_data ctlData[AR5416_NUM_CTLS];
	u8 padding;
} __packed;

struct ar5416_eeprom_4k {
	struct base_eep_header_4k baseEepHeader;
	u8 custData[20];
	struct modal_eep_4k_header modalHeader;
	u8 calFreqPier2G[AR5416_EEP4K_NUM_2G_CAL_PIERS];
	struct cal_data_per_freq_4k
	calPierData2G[AR5416_EEP4K_MAX_CHAINS][AR5416_EEP4K_NUM_2G_CAL_PIERS];
	struct cal_target_power_leg
	calTargetPowerCck[AR5416_EEP4K_NUM_2G_CCK_TARGET_POWERS];
	struct cal_target_power_leg
	calTargetPower2G[AR5416_EEP4K_NUM_2G_20_TARGET_POWERS];
	struct cal_target_power_ht
	calTargetPower2GHT20[AR5416_EEP4K_NUM_2G_20_TARGET_POWERS];
	struct cal_target_power_ht
	calTargetPower2GHT40[AR5416_EEP4K_NUM_2G_40_TARGET_POWERS];
	u8 ctlIndex[AR5416_EEP4K_NUM_CTLS];
	struct cal_ctl_data_4k ctlData[AR5416_EEP4K_NUM_CTLS];
	u8 padding;
} __packed;

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

enum ath9k_eep_map {
	EEP_MAP_DEFAULT = 0x0,
	EEP_MAP_4KBITS,
	EEP_MAP_MAX
};

struct eeprom_ops {
	int (*check_eeprom)(struct ath_hw *hw);
	u32 (*get_eeprom)(struct ath_hw *hw, enum eeprom_param param);
	bool (*fill_eeprom)(struct ath_hw *hw);
	int (*get_eeprom_ver)(struct ath_hw *hw);
	int (*get_eeprom_rev)(struct ath_hw *hw);
	u8 (*get_num_ant_config)(struct ath_hw *hw, enum ieee80211_band band);
	u16 (*get_eeprom_antenna_cfg)(struct ath_hw *hw,
				      struct ath9k_channel *chan);
	void (*set_board_values)(struct ath_hw *hw, struct ath9k_channel *chan);
	void (*set_addac)(struct ath_hw *hw, struct ath9k_channel *chan);
	void (*set_txpower)(struct ath_hw *hw, struct ath9k_channel *chan,
			   u16 cfgCtl, u8 twiceAntennaReduction,
			   u8 twiceMaxRegulatoryPower, u8 powerLimit);
	u16 (*get_spur_channel)(struct ath_hw *ah, u16 i, bool is2GHz);
};

#define ar5416_get_ntxchains(_txchainmask)			\
	(((_txchainmask >> 2) & 1) +                            \
	 ((_txchainmask >> 1) & 1) + (_txchainmask & 1))

int ath9k_hw_eeprom_attach(struct ath_hw *ah);

#endif /* EEPROM_H */
