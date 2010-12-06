#ifndef AR9003_EEPROM_H
#define AR9003_EEPROM_H

#include <linux/types.h>

#define AR9300_EEP_VER               0xD000
#define AR9300_EEP_VER_MINOR_MASK    0xFFF
#define AR9300_EEP_MINOR_VER_1       0x1
#define AR9300_EEP_MINOR_VER         AR9300_EEP_MINOR_VER_1

/* 16-bit offset location start of calibration struct */
#define AR9300_EEP_START_LOC         256
#define AR9300_NUM_5G_CAL_PIERS      8
#define AR9300_NUM_2G_CAL_PIERS      3
#define AR9300_NUM_5G_20_TARGET_POWERS  8
#define AR9300_NUM_5G_40_TARGET_POWERS  8
#define AR9300_NUM_2G_CCK_TARGET_POWERS 2
#define AR9300_NUM_2G_20_TARGET_POWERS  3
#define AR9300_NUM_2G_40_TARGET_POWERS  3
/* #define AR9300_NUM_CTLS              21 */
#define AR9300_NUM_CTLS_5G           9
#define AR9300_NUM_CTLS_2G           12
#define AR9300_CTL_MODE_M            0xF
#define AR9300_NUM_BAND_EDGES_5G     8
#define AR9300_NUM_BAND_EDGES_2G     4
#define AR9300_NUM_PD_GAINS          4
#define AR9300_PD_GAINS_IN_MASK      4
#define AR9300_PD_GAIN_ICEPTS        5
#define AR9300_EEPROM_MODAL_SPURS    5
#define AR9300_MAX_RATE_POWER        63
#define AR9300_NUM_PDADC_VALUES      128
#define AR9300_NUM_RATES             16
#define AR9300_BCHAN_UNUSED          0xFF
#define AR9300_MAX_PWR_RANGE_IN_HALF_DB 64
#define AR9300_OPFLAGS_11A           0x01
#define AR9300_OPFLAGS_11G           0x02
#define AR9300_OPFLAGS_5G_HT40       0x04
#define AR9300_OPFLAGS_2G_HT40       0x08
#define AR9300_OPFLAGS_5G_HT20       0x10
#define AR9300_OPFLAGS_2G_HT20       0x20
#define AR9300_EEPMISC_BIG_ENDIAN    0x01
#define AR9300_EEPMISC_WOW           0x02
#define AR9300_CUSTOMER_DATA_SIZE    20

#define FREQ2FBIN(x, y) ((y) ? ((x) - 2300) : (((x) - 4800) / 5))
#define FBIN2FREQ(x, y) ((y) ? (2300 + x) : (4800 + 5 * x))
#define AR9300_MAX_CHAINS            3
#define AR9300_ANT_16S               25
#define AR9300_FUTURE_MODAL_SZ       6

#define AR9300_NUM_ANT_CHAIN_FIELDS     7
#define AR9300_NUM_ANT_COMMON_FIELDS    4
#define AR9300_SIZE_ANT_CHAIN_FIELD     3
#define AR9300_SIZE_ANT_COMMON_FIELD    4
#define AR9300_ANT_CHAIN_MASK           0x7
#define AR9300_ANT_COMMON_MASK          0xf
#define AR9300_CHAIN_0_IDX              0
#define AR9300_CHAIN_1_IDX              1
#define AR9300_CHAIN_2_IDX              2

#define AR928X_NUM_ANT_CHAIN_FIELDS     6
#define AR928X_SIZE_ANT_CHAIN_FIELD     2
#define AR928X_ANT_CHAIN_MASK           0x3

/* Delta from which to start power to pdadc table */
/* This offset is used in both open loop and closed loop power control
 * schemes. In open loop power control, it is not really needed, but for
 * the "sake of consistency" it was kept. For certain AP designs, this
 * value is overwritten by the value in the flag "pwrTableOffset" just
 * before writing the pdadc vs pwr into the chip registers.
 */
#define AR9300_PWR_TABLE_OFFSET  0

/* enable flags for voltage and temp compensation */
#define ENABLE_TEMP_COMPENSATION 0x01
#define ENABLE_VOLT_COMPENSATION 0x02
/* byte addressable */
#define AR9300_EEPROM_SIZE (16*1024)
#define FIXED_CCA_THRESHOLD 15

#define AR9300_BASE_ADDR_4K 0xfff
#define AR9300_BASE_ADDR 0x3ff
#define AR9300_BASE_ADDR_512 0x1ff

#define AR9300_OTP_BASE			0x14000
#define AR9300_OTP_STATUS		0x15f18
#define AR9300_OTP_STATUS_TYPE		0x7
#define AR9300_OTP_STATUS_VALID		0x4
#define AR9300_OTP_STATUS_ACCESS_BUSY	0x2
#define AR9300_OTP_STATUS_SM_BUSY	0x1
#define AR9300_OTP_READ_DATA		0x15f1c

enum targetPowerHTRates {
	HT_TARGET_RATE_0_8_16,
	HT_TARGET_RATE_1_3_9_11_17_19,
	HT_TARGET_RATE_4,
	HT_TARGET_RATE_5,
	HT_TARGET_RATE_6,
	HT_TARGET_RATE_7,
	HT_TARGET_RATE_12,
	HT_TARGET_RATE_13,
	HT_TARGET_RATE_14,
	HT_TARGET_RATE_15,
	HT_TARGET_RATE_20,
	HT_TARGET_RATE_21,
	HT_TARGET_RATE_22,
	HT_TARGET_RATE_23
};

enum targetPowerLegacyRates {
	LEGACY_TARGET_RATE_6_24,
	LEGACY_TARGET_RATE_36,
	LEGACY_TARGET_RATE_48,
	LEGACY_TARGET_RATE_54
};

enum targetPowerCckRates {
	LEGACY_TARGET_RATE_1L_5L,
	LEGACY_TARGET_RATE_5S,
	LEGACY_TARGET_RATE_11L,
	LEGACY_TARGET_RATE_11S
};

enum ar9300_Rates {
	ALL_TARGET_LEGACY_6_24,
	ALL_TARGET_LEGACY_36,
	ALL_TARGET_LEGACY_48,
	ALL_TARGET_LEGACY_54,
	ALL_TARGET_LEGACY_1L_5L,
	ALL_TARGET_LEGACY_5S,
	ALL_TARGET_LEGACY_11L,
	ALL_TARGET_LEGACY_11S,
	ALL_TARGET_HT20_0_8_16,
	ALL_TARGET_HT20_1_3_9_11_17_19,
	ALL_TARGET_HT20_4,
	ALL_TARGET_HT20_5,
	ALL_TARGET_HT20_6,
	ALL_TARGET_HT20_7,
	ALL_TARGET_HT20_12,
	ALL_TARGET_HT20_13,
	ALL_TARGET_HT20_14,
	ALL_TARGET_HT20_15,
	ALL_TARGET_HT20_20,
	ALL_TARGET_HT20_21,
	ALL_TARGET_HT20_22,
	ALL_TARGET_HT20_23,
	ALL_TARGET_HT40_0_8_16,
	ALL_TARGET_HT40_1_3_9_11_17_19,
	ALL_TARGET_HT40_4,
	ALL_TARGET_HT40_5,
	ALL_TARGET_HT40_6,
	ALL_TARGET_HT40_7,
	ALL_TARGET_HT40_12,
	ALL_TARGET_HT40_13,
	ALL_TARGET_HT40_14,
	ALL_TARGET_HT40_15,
	ALL_TARGET_HT40_20,
	ALL_TARGET_HT40_21,
	ALL_TARGET_HT40_22,
	ALL_TARGET_HT40_23,
	ar9300RateSize,
};


struct eepFlags {
	u8 opFlags;
	u8 eepMisc;
} __packed;

enum CompressAlgorithm {
	_CompressNone = 0,
	_CompressLzma,
	_CompressPairs,
	_CompressBlock,
	_Compress4,
	_Compress5,
	_Compress6,
	_Compress7,
};

struct ar9300_base_eep_hdr {
	__le16 regDmn[2];
	/* 4 bits tx and 4 bits rx */
	u8 txrxMask;
	struct eepFlags opCapFlags;
	u8 rfSilent;
	u8 blueToothOptions;
	u8 deviceCap;
	/* takes lower byte in eeprom location */
	u8 deviceType;
	/* offset in dB to be added to beginning
	 * of pdadc table in calibration
	 */
	int8_t pwrTableOffset;
	u8 params_for_tuning_caps[2];
	/*
	 * bit0 - enable tx temp comp
	 * bit1 - enable tx volt comp
	 * bit2 - enable fastClock - default to 1
	 * bit3 - enable doubling - default to 1
	 * bit4 - enable internal regulator - default to 1
	 */
	u8 featureEnable;
	/* misc flags: bit0 - turn down drivestrength */
	u8 miscConfiguration;
	u8 eepromWriteEnableGpio;
	u8 wlanDisableGpio;
	u8 wlanLedGpio;
	u8 rxBandSelectGpio;
	u8 txrxgain;
	/* SW controlled internal regulator fields */
	__le32 swreg;
} __packed;

struct ar9300_modal_eep_header {
	/* 4 idle, t1, t2, b (4 bits per setting) */
	__le32 antCtrlCommon;
	/* 4 ra1l1, ra2l1, ra1l2, ra2l2, ra12 */
	__le32 antCtrlCommon2;
	/* 6 idle, t, r, rx1, rx12, b (2 bits each) */
	__le16 antCtrlChain[AR9300_MAX_CHAINS];
	/* 3 xatten1_db for AR9280 (0xa20c/b20c 5:0) */
	u8 xatten1DB[AR9300_MAX_CHAINS];
	/* 3  xatten1_margin for merlin (0xa20c/b20c 16:12 */
	u8 xatten1Margin[AR9300_MAX_CHAINS];
	int8_t tempSlope;
	int8_t voltSlope;
	/* spur channels in usual fbin coding format */
	u8 spurChans[AR9300_EEPROM_MODAL_SPURS];
	/* 3  Check if the register is per chain */
	int8_t noiseFloorThreshCh[AR9300_MAX_CHAINS];
	u8 ob[AR9300_MAX_CHAINS];
	u8 db_stage2[AR9300_MAX_CHAINS];
	u8 db_stage3[AR9300_MAX_CHAINS];
	u8 db_stage4[AR9300_MAX_CHAINS];
	u8 xpaBiasLvl;
	u8 txFrameToDataStart;
	u8 txFrameToPaOn;
	u8 txClip;
	int8_t antennaGain;
	u8 switchSettling;
	int8_t adcDesiredSize;
	u8 txEndToXpaOff;
	u8 txEndToRxOn;
	u8 txFrameToXpaOn;
	u8 thresh62;
	__le32 papdRateMaskHt20;
	__le32 papdRateMaskHt40;
	u8 futureModal[10];
} __packed;

struct ar9300_cal_data_per_freq_op_loop {
	int8_t refPower;
	/* pdadc voltage at power measurement */
	u8 voltMeas;
	/* pcdac used for power measurement   */
	u8 tempMeas;
	/* range is -60 to -127 create a mapping equation 1db resolution */
	int8_t rxNoisefloorCal;
	/*range is same as noisefloor */
	int8_t rxNoisefloorPower;
	/* temp measured when noisefloor cal was performed */
	u8 rxTempMeas;
} __packed;

struct cal_tgt_pow_legacy {
	u8 tPow2x[4];
} __packed;

struct cal_tgt_pow_ht {
	u8 tPow2x[14];
} __packed;

struct cal_ctl_data_2g {
	u8 ctlEdges[AR9300_NUM_BAND_EDGES_2G];
} __packed;

struct cal_ctl_data_5g {
	u8 ctlEdges[AR9300_NUM_BAND_EDGES_5G];
} __packed;

struct ar9300_BaseExtension_1 {
	u8 ant_div_control;
	u8 future[13];
} __packed;

struct ar9300_BaseExtension_2 {
	int8_t    tempSlopeLow;
	int8_t    tempSlopeHigh;
	u8   xatten1DBLow[AR9300_MAX_CHAINS];
	u8   xatten1MarginLow[AR9300_MAX_CHAINS];
	u8   xatten1DBHigh[AR9300_MAX_CHAINS];
	u8   xatten1MarginHigh[AR9300_MAX_CHAINS];
} __packed;

struct ar9300_eeprom {
	u8 eepromVersion;
	u8 templateVersion;
	u8 macAddr[6];
	u8 custData[AR9300_CUSTOMER_DATA_SIZE];

	struct ar9300_base_eep_hdr baseEepHeader;

	struct ar9300_modal_eep_header modalHeader2G;
	struct ar9300_BaseExtension_1 base_ext1;
	u8 calFreqPier2G[AR9300_NUM_2G_CAL_PIERS];
	struct ar9300_cal_data_per_freq_op_loop
	 calPierData2G[AR9300_MAX_CHAINS][AR9300_NUM_2G_CAL_PIERS];
	u8 calTarget_freqbin_Cck[AR9300_NUM_2G_CCK_TARGET_POWERS];
	u8 calTarget_freqbin_2G[AR9300_NUM_2G_20_TARGET_POWERS];
	u8 calTarget_freqbin_2GHT20[AR9300_NUM_2G_20_TARGET_POWERS];
	u8 calTarget_freqbin_2GHT40[AR9300_NUM_2G_40_TARGET_POWERS];
	struct cal_tgt_pow_legacy
	 calTargetPowerCck[AR9300_NUM_2G_CCK_TARGET_POWERS];
	struct cal_tgt_pow_legacy
	 calTargetPower2G[AR9300_NUM_2G_20_TARGET_POWERS];
	struct cal_tgt_pow_ht
	 calTargetPower2GHT20[AR9300_NUM_2G_20_TARGET_POWERS];
	struct cal_tgt_pow_ht
	 calTargetPower2GHT40[AR9300_NUM_2G_40_TARGET_POWERS];
	u8 ctlIndex_2G[AR9300_NUM_CTLS_2G];
	u8 ctl_freqbin_2G[AR9300_NUM_CTLS_2G][AR9300_NUM_BAND_EDGES_2G];
	struct cal_ctl_data_2g ctlPowerData_2G[AR9300_NUM_CTLS_2G];
	struct ar9300_modal_eep_header modalHeader5G;
	struct ar9300_BaseExtension_2 base_ext2;
	u8 calFreqPier5G[AR9300_NUM_5G_CAL_PIERS];
	struct ar9300_cal_data_per_freq_op_loop
	 calPierData5G[AR9300_MAX_CHAINS][AR9300_NUM_5G_CAL_PIERS];
	u8 calTarget_freqbin_5G[AR9300_NUM_5G_20_TARGET_POWERS];
	u8 calTarget_freqbin_5GHT20[AR9300_NUM_5G_20_TARGET_POWERS];
	u8 calTarget_freqbin_5GHT40[AR9300_NUM_5G_40_TARGET_POWERS];
	struct cal_tgt_pow_legacy
	 calTargetPower5G[AR9300_NUM_5G_20_TARGET_POWERS];
	struct cal_tgt_pow_ht
	 calTargetPower5GHT20[AR9300_NUM_5G_20_TARGET_POWERS];
	struct cal_tgt_pow_ht
	 calTargetPower5GHT40[AR9300_NUM_5G_40_TARGET_POWERS];
	u8 ctlIndex_5G[AR9300_NUM_CTLS_5G];
	u8 ctl_freqbin_5G[AR9300_NUM_CTLS_5G][AR9300_NUM_BAND_EDGES_5G];
	struct cal_ctl_data_5g ctlPowerData_5G[AR9300_NUM_CTLS_5G];
} __packed;

s32 ar9003_hw_get_tx_gain_idx(struct ath_hw *ah);
s32 ar9003_hw_get_rx_gain_idx(struct ath_hw *ah);

#endif
