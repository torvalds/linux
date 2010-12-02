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

#include "../ath.h"
#include <net/cfg80211.h>
#include "ar9003_eeprom.h"

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
#define CTL_MODE_M              0xf
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

#define ATH9K_POW_SM(_r, _s)	(((_r) & 0x3f) << (_s))
#define FREQ2FBIN(x, y)		((y) ? ((x) - 2300) : (((x) - 4800) / 5))
#define ath9k_hw_use_flash(_ah)	(!(_ah->ah_flags & AH_USE_EEPROM))

#define AR5416_VER_MASK (eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK)
#define OLC_FOR_AR9280_20_LATER (AR_SREV_9280_20_OR_LATER(ah) && \
				 ah->eep_ops->get_eeprom(ah, EEP_OL_PWRCTRL))
#define OLC_FOR_AR9287_10_LATER (AR_SREV_9287_11_OR_LATER(ah) && \
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
#define AR5416_EEP_MINOR_VER_21      0x15
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
#define AR9300_MAX_CHAINS		3
#define AR5416_PWR_TABLE_OFFSET_DB     -5

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

#define AR9287_EEP_VER               0xE
#define AR9287_EEP_VER_MINOR_MASK    0xFFF
#define AR9287_EEP_MINOR_VER_1       0x1
#define AR9287_EEP_MINOR_VER_2       0x2
#define AR9287_EEP_MINOR_VER_3       0x3
#define AR9287_EEP_MINOR_VER         AR9287_EEP_MINOR_VER_3
#define AR9287_EEP_MINOR_VER_b       AR9287_EEP_MINOR_VER
#define AR9287_EEP_NO_BACK_VER       AR9287_EEP_MINOR_VER_1

#define AR9287_EEP_START_LOC            128
#define AR9287_HTC_EEP_START_LOC        256
#define AR9287_NUM_2G_CAL_PIERS         3
#define AR9287_NUM_2G_CCK_TARGET_POWERS 3
#define AR9287_NUM_2G_20_TARGET_POWERS  3
#define AR9287_NUM_2G_40_TARGET_POWERS  3
#define AR9287_NUM_CTLS              	12
#define AR9287_NUM_BAND_EDGES        	4
#define AR9287_NUM_PD_GAINS             4
#define AR9287_PD_GAINS_IN_MASK         4
#define AR9287_PD_GAIN_ICEPTS           1
#define AR9287_EEPROM_MODAL_SPURS       5
#define AR9287_MAX_RATE_POWER           63
#define AR9287_NUM_PDADC_VALUES         128
#define AR9287_NUM_RATES                16
#define AR9287_BCHAN_UNUSED             0xFF
#define AR9287_MAX_PWR_RANGE_IN_HALF_DB 64
#define AR9287_OPFLAGS_11A              0x01
#define AR9287_OPFLAGS_11G              0x02
#define AR9287_OPFLAGS_2G_HT40          0x08
#define AR9287_OPFLAGS_2G_HT20          0x20
#define AR9287_OPFLAGS_5G_HT40          0x04
#define AR9287_OPFLAGS_5G_HT20          0x10
#define AR9287_EEPMISC_BIG_ENDIAN       0x01
#define AR9287_EEPMISC_WOW              0x02
#define AR9287_MAX_CHAINS               2
#define AR9287_ANT_16S                  32
#define AR9287_custdatasize             20

#define AR9287_NUM_ANT_CHAIN_FIELDS     6
#define AR9287_NUM_ANT_COMMON_FIELDS    4
#define AR9287_SIZE_ANT_CHAIN_FIELD     2
#define AR9287_SIZE_ANT_COMMON_FIELD    4
#define AR9287_ANT_CHAIN_MASK           0x3
#define AR9287_ANT_COMMON_MASK          0xf
#define AR9287_CHAIN_0_IDX              0
#define AR9287_CHAIN_1_IDX              1
#define AR9287_DATA_SZ                  32

#define AR9287_PWR_TABLE_OFFSET_DB  -5

#define AR9287_CHECKSUM_LOCATION (AR9287_EEP_START_LOC + 1)

#define CTL_EDGE_TPOWER(_ctl) ((_ctl) & 0x3f)
#define CTL_EDGE_FLAGS(_ctl) (((_ctl) >> 6) & 0x03)

#define LNA_CTL_BUF_MODE	BIT(0)
#define LNA_CTL_ISEL_LO		BIT(1)
#define LNA_CTL_ISEL_HI		BIT(2)
#define LNA_CTL_BUF_IN		BIT(3)
#define LNA_CTL_FEM_BAND	BIT(4)
#define LNA_CTL_LOCAL_BIAS	BIT(5)
#define LNA_CTL_FORCE_XPA	BIT(6)
#define LNA_CTL_USE_ANT1	BIT(7)

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
	EEP_FSTCLK_5G,
	EEP_RXGAIN_TYPE,
	EEP_OL_PWRCTRL,
	EEP_TXGAIN_TYPE,
	EEP_RC_CHAIN_MASK,
	EEP_DAC_HPWR_5G,
	EEP_FRAC_N_5G,
	EEP_DEV_TYPE,
	EEP_TEMPSENSE_SLOPE,
	EEP_TEMPSENSE_SLOPE_PAL_ON,
	EEP_PWR_TABLE_OFFSET,
	EEP_DRIVE_STRENGTH,
	EEP_INTERNAL_REGULATOR,
	EEP_SWREG,
	EEP_PAPRD,
	EEP_MODAL_VER,
	EEP_ANT_DIV_CTL1,
	EEP_CHAIN_MASK_REDUCE
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
	u8 fastClk5g;
	u8 divChain;
	u8 rxGainType;
	u8 dacHiPwrMode_5G;
	u8 openLoopPwrCntl;
	u8 dacLpMode;
	u8 txGainType;
	u8 rcChainMask;
	u8 desiredScaleCCK;
	u8 pwr_table_offset;
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
	u8 lna_ctl;
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
	u32 antCtrlChain[AR5416_EEP4K_MAX_CHAINS];
	u32 antCtrlCommon;
	u8 antennaGainCh[AR5416_EEP4K_MAX_CHAINS];
	u8 switchSettling;
	u8 txRxAttenCh[AR5416_EEP4K_MAX_CHAINS];
	u8 rxTxMarginCh[AR5416_EEP4K_MAX_CHAINS];
	u8 adcDesiredSize;
	u8 pgaDesiredSize;
	u8 xlnaGainCh[AR5416_EEP4K_MAX_CHAINS];
	u8 txEndToXpaOff;
	u8 txEndToRxOn;
	u8 txFrameToXpaOn;
	u8 thresh62;
	u8 noiseFloorThreshCh[AR5416_EEP4K_MAX_CHAINS];
	u8 xpdGain;
	u8 xpd;
	u8 iqCalICh[AR5416_EEP4K_MAX_CHAINS];
	u8 iqCalQCh[AR5416_EEP4K_MAX_CHAINS];
	u8 pdGainOverlap;
#ifdef __BIG_ENDIAN_BITFIELD
	u8 ob_1:4, ob_0:4;
	u8 db1_1:4, db1_0:4;
#else
	u8 ob_0:4, ob_1:4;
	u8 db1_0:4, db1_1:4;
#endif
	u8 xpaBiasLvl;
	u8 txFrameToDataStart;
	u8 txFrameToPaOn;
	u8 ht40PowerIncForPdadc;
	u8 bswAtten[AR5416_EEP4K_MAX_CHAINS];
	u8 bswMargin[AR5416_EEP4K_MAX_CHAINS];
	u8 swSettleHt40;
	u8 xatten2Db[AR5416_EEP4K_MAX_CHAINS];
	u8 xatten2Margin[AR5416_EEP4K_MAX_CHAINS];
#ifdef __BIG_ENDIAN_BITFIELD
	u8 db2_1:4, db2_0:4;
#else
	u8 db2_0:4, db2_1:4;
#endif
	u8 version;
#ifdef __BIG_ENDIAN_BITFIELD
	u8 ob_3:4, ob_2:4;
	u8 antdiv_ctl1:4, ob_4:4;
	u8 db1_3:4, db1_2:4;
	u8 antdiv_ctl2:4, db1_4:4;
	u8 db2_2:4, db2_3:4;
	u8 reserved:4, db2_4:4;
#else
	u8 ob_2:4, ob_3:4;
	u8 ob_4:4, antdiv_ctl1:4;
	u8 db1_2:4, db1_3:4;
	u8 db1_4:4, antdiv_ctl2:4;
	u8 db2_2:4, db2_3:4;
	u8 db2_4:4, reserved:4;
#endif
	u8 futureModal[4];
	struct spur_chan spurChans[AR5416_EEPROM_MODAL_SPURS];
} __packed;

struct base_eep_ar9287_header {
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
	u8 openLoopPwrCntl;
	int8_t pwrTableOffset;
	int8_t tempSensSlope;
	int8_t tempSensSlopePalOn;
	u8 futureBase[29];
} __packed;

struct modal_eep_ar9287_header {
	u32 antCtrlChain[AR9287_MAX_CHAINS];
	u32 antCtrlCommon;
	int8_t antennaGainCh[AR9287_MAX_CHAINS];
	u8 switchSettling;
	u8 txRxAttenCh[AR9287_MAX_CHAINS];
	u8 rxTxMarginCh[AR9287_MAX_CHAINS];
	int8_t adcDesiredSize;
	u8 txEndToXpaOff;
	u8 txEndToRxOn;
	u8 txFrameToXpaOn;
	u8 thresh62;
	int8_t noiseFloorThreshCh[AR9287_MAX_CHAINS];
	u8 xpdGain;
	u8 xpd;
	int8_t iqCalICh[AR9287_MAX_CHAINS];
	int8_t iqCalQCh[AR9287_MAX_CHAINS];
	u8 pdGainOverlap;
	u8 xpaBiasLvl;
	u8 txFrameToDataStart;
	u8 txFrameToPaOn;
	u8 ht40PowerIncForPdadc;
	u8 bswAtten[AR9287_MAX_CHAINS];
	u8 bswMargin[AR9287_MAX_CHAINS];
	u8 swSettleHt40;
	u8 version;
	u8 db1;
	u8 db2;
	u8 ob_cck;
	u8 ob_psk;
	u8 ob_qam;
	u8 ob_pal_off;
	u8 futureModal[30];
	struct spur_chan spurChans[AR9287_EEPROM_MODAL_SPURS];
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

struct cal_ctl_edges {
	u8 bChannel;
	u8 ctl;
} __packed;

struct cal_data_op_loop_ar9287 {
	u8 pwrPdg[2][5];
	u8 vpdPdg[2][5];
	u8 pcdac[2][5];
	u8 empty[2][5];
} __packed;

struct cal_data_per_freq_ar9287 {
	u8 pwrPdg[AR9287_NUM_PD_GAINS][AR9287_PD_GAIN_ICEPTS];
	u8 vpdPdg[AR9287_NUM_PD_GAINS][AR9287_PD_GAIN_ICEPTS];
} __packed;

union cal_data_per_freq_ar9287_u {
	struct cal_data_op_loop_ar9287 calDataOpen;
	struct cal_data_per_freq_ar9287 calDataClose;
} __packed;

struct cal_ctl_data_ar9287 {
	struct cal_ctl_edges
	ctlEdges[AR9287_MAX_CHAINS][AR9287_NUM_BAND_EDGES];
} __packed;

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

struct ar9287_eeprom {
	struct base_eep_ar9287_header baseEepHeader;
	u8 custData[AR9287_DATA_SZ];
	struct modal_eep_ar9287_header modalHeader;
	u8 calFreqPier2G[AR9287_NUM_2G_CAL_PIERS];
	union cal_data_per_freq_ar9287_u
	calPierData2G[AR9287_MAX_CHAINS][AR9287_NUM_2G_CAL_PIERS];
	struct cal_target_power_leg
	calTargetPowerCck[AR9287_NUM_2G_CCK_TARGET_POWERS];
	struct cal_target_power_leg
	calTargetPower2G[AR9287_NUM_2G_20_TARGET_POWERS];
	struct cal_target_power_ht
	calTargetPower2GHT20[AR9287_NUM_2G_20_TARGET_POWERS];
	struct cal_target_power_ht
	calTargetPower2GHT40[AR9287_NUM_2G_40_TARGET_POWERS];
	u8 ctlIndex[AR9287_NUM_CTLS];
	struct cal_ctl_data_ar9287 ctlData[AR9287_NUM_CTLS];
	u8 padding;
} __packed;

enum reg_ext_bitmap {
	REG_EXT_FCC_MIDBAND = 0,
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

struct eeprom_ops {
	int (*check_eeprom)(struct ath_hw *hw);
	u32 (*get_eeprom)(struct ath_hw *hw, enum eeprom_param param);
	bool (*fill_eeprom)(struct ath_hw *hw);
	int (*get_eeprom_ver)(struct ath_hw *hw);
	int (*get_eeprom_rev)(struct ath_hw *hw);
	u8 (*get_num_ant_config)(struct ath_hw *hw,
				 enum ath9k_hal_freq_band band);
	u32 (*get_eeprom_antenna_cfg)(struct ath_hw *hw,
				      struct ath9k_channel *chan);
	void (*set_board_values)(struct ath_hw *hw, struct ath9k_channel *chan);
	void (*set_addac)(struct ath_hw *hw, struct ath9k_channel *chan);
	void (*set_txpower)(struct ath_hw *hw, struct ath9k_channel *chan,
			   u16 cfgCtl, u8 twiceAntennaReduction,
			   u8 twiceMaxRegulatoryPower, u8 powerLimit,
			   bool test);
	u16 (*get_spur_channel)(struct ath_hw *ah, u16 i, bool is2GHz);
};

void ath9k_hw_analog_shift_regwrite(struct ath_hw *ah, u32 reg, u32 val);
void ath9k_hw_analog_shift_rmw(struct ath_hw *ah, u32 reg, u32 mask,
			       u32 shift, u32 val);
int16_t ath9k_hw_interpolate(u16 target, u16 srcLeft, u16 srcRight,
			     int16_t targetLeft,
			     int16_t targetRight);
bool ath9k_hw_get_lower_upper_index(u8 target, u8 *pList, u16 listSize,
				    u16 *indexL, u16 *indexR);
bool ath9k_hw_nvram_read(struct ath_common *common, u32 off, u16 *data);
void ath9k_hw_fill_vpd_table(u8 pwrMin, u8 pwrMax, u8 *pPwrList,
			     u8 *pVpdList, u16 numIntercepts,
			     u8 *pRetVpdList);
void ath9k_hw_get_legacy_target_powers(struct ath_hw *ah,
				       struct ath9k_channel *chan,
				       struct cal_target_power_leg *powInfo,
				       u16 numChannels,
				       struct cal_target_power_leg *pNewPower,
				       u16 numRates, bool isExtTarget);
void ath9k_hw_get_target_powers(struct ath_hw *ah,
				struct ath9k_channel *chan,
				struct cal_target_power_ht *powInfo,
				u16 numChannels,
				struct cal_target_power_ht *pNewPower,
				u16 numRates, bool isHt40Target);
u16 ath9k_hw_get_max_edge_power(u16 freq, struct cal_ctl_edges *pRdEdgesPower,
				bool is2GHz, int num_band_edges);
void ath9k_hw_update_regulatory_maxpower(struct ath_hw *ah);
int ath9k_hw_eeprom_init(struct ath_hw *ah);

#define ar5416_get_ntxchains(_txchainmask)			\
	(((_txchainmask >> 2) & 1) +                            \
	 ((_txchainmask >> 1) & 1) + (_txchainmask & 1))

extern const struct eeprom_ops eep_def_ops;
extern const struct eeprom_ops eep_4k_ops;
extern const struct eeprom_ops eep_ar9287_ops;
extern const struct eeprom_ops eep_ar9287_ops;
extern const struct eeprom_ops eep_ar9300_ops;

#endif /* EEPROM_H */
