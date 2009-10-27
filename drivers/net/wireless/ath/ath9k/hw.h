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

#ifndef HW_H
#define HW_H

#include <linux/if_ether.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "mac.h"
#include "ani.h"
#include "eeprom.h"
#include "calib.h"
#include "reg.h"
#include "phy.h"
#include "btcoex.h"

#include "../regd.h"
#include "../debug.h"

#define ATHEROS_VENDOR_ID	0x168c

#define AR5416_DEVID_PCI	0x0023
#define AR5416_DEVID_PCIE	0x0024
#define AR9160_DEVID_PCI	0x0027
#define AR9280_DEVID_PCI	0x0029
#define AR9280_DEVID_PCIE	0x002a
#define AR9285_DEVID_PCIE	0x002b

#define AR5416_AR9100_DEVID	0x000b

#define AR9271_USB             0x9271

#define	AR_SUBVENDOR_ID_NOG	0x0e11
#define AR_SUBVENDOR_ID_NEW_A	0x7065
#define AR5416_MAGIC		0x19641014

#define AR5416_DEVID_AR9287_PCI  0x002D
#define AR5416_DEVID_AR9287_PCIE 0x002E

#define AR9280_COEX2WIRE_SUBSYSID	0x309b
#define AT9285_COEX3WIRE_SA_SUBSYSID	0x30aa
#define AT9285_COEX3WIRE_DA_SUBSYSID	0x30ab

#define ATH_AMPDU_LIMIT_MAX        (64 * 1024 - 1)

#define	ATH_DEFAULT_NOISE_FLOOR -95

#define ATH9K_RSSI_BAD			0x80

/* Register read/write primitives */
#define REG_WRITE(_ah, _reg, _val) \
	ath9k_hw_common(_ah)->ops->write((_ah), (_val), (_reg))

#define REG_READ(_ah, _reg) \
	ath9k_hw_common(_ah)->ops->read((_ah), (_reg))

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

#define DO_DELAY(x) do {			\
		if ((++(x) % 64) == 0)          \
			udelay(1);		\
	} while (0)

#define REG_WRITE_ARRAY(iniarray, column, regWr) do {                   \
		int r;							\
		for (r = 0; r < ((iniarray)->ia_rows); r++) {		\
			REG_WRITE(ah, INI_RA((iniarray), (r), 0),	\
				  INI_RA((iniarray), r, (column)));	\
			DO_DELAY(regWr);				\
		}							\
	} while (0)

#define AR_GPIO_OUTPUT_MUX_AS_OUTPUT             0
#define AR_GPIO_OUTPUT_MUX_AS_PCIE_ATTENTION_LED 1
#define AR_GPIO_OUTPUT_MUX_AS_PCIE_POWER_LED     2
#define AR_GPIO_OUTPUT_MUX_AS_TX_FRAME           3
#define AR_GPIO_OUTPUT_MUX_AS_RX_CLEAR_EXTERNAL  4
#define AR_GPIO_OUTPUT_MUX_AS_MAC_NETWORK_LED    5
#define AR_GPIO_OUTPUT_MUX_AS_MAC_POWER_LED      6

#define AR_GPIOD_MASK               0x00001FFF
#define AR_GPIO_BIT(_gpio)          (1 << (_gpio))

#define BASE_ACTIVATE_DELAY         100
#define RTC_PLL_SETTLE_DELAY        100
#define COEF_SCALE_S                24
#define HT40_CHANNEL_CENTER_SHIFT   10

#define ATH9K_ANTENNA0_CHAINMASK    0x1
#define ATH9K_ANTENNA1_CHAINMASK    0x2

#define ATH9K_NUM_DMA_DEBUG_REGS    8
#define ATH9K_NUM_QUEUES            10

#define MAX_RATE_POWER              63
#define AH_WAIT_TIMEOUT             100000 /* (us) */
#define AH_TSF_WRITE_TIMEOUT        100    /* (us) */
#define AH_TIME_QUANTUM             10
#define AR_KEYTABLE_SIZE            128
#define POWER_UP_TIME               10000
#define SPUR_RSSI_THRESH            40

#define CAB_TIMEOUT_VAL             10
#define BEACON_TIMEOUT_VAL          10
#define MIN_BEACON_TIMEOUT_VAL      1
#define SLEEP_SLOP                  3

#define INIT_CONFIG_STATUS          0x00000000
#define INIT_RSSI_THR               0x00000700
#define INIT_BCON_CNTRL_REG         0x00000000

#define TU_TO_USEC(_tu)             ((_tu) << 10)

enum wireless_mode {
	ATH9K_MODE_11A = 0,
	ATH9K_MODE_11G,
	ATH9K_MODE_11NA_HT20,
	ATH9K_MODE_11NG_HT20,
	ATH9K_MODE_11NA_HT40PLUS,
	ATH9K_MODE_11NA_HT40MINUS,
	ATH9K_MODE_11NG_HT40PLUS,
	ATH9K_MODE_11NG_HT40MINUS,
	ATH9K_MODE_MAX,
};

enum ath9k_ant_setting {
	ATH9K_ANT_VARIABLE = 0,
	ATH9K_ANT_FIXED_A,
	ATH9K_ANT_FIXED_B
};

enum ath9k_hw_caps {
	ATH9K_HW_CAP_MIC_AESCCM                 = BIT(0),
	ATH9K_HW_CAP_MIC_CKIP                   = BIT(1),
	ATH9K_HW_CAP_MIC_TKIP                   = BIT(2),
	ATH9K_HW_CAP_CIPHER_AESCCM              = BIT(3),
	ATH9K_HW_CAP_CIPHER_CKIP                = BIT(4),
	ATH9K_HW_CAP_CIPHER_TKIP                = BIT(5),
	ATH9K_HW_CAP_VEOL                       = BIT(6),
	ATH9K_HW_CAP_BSSIDMASK                  = BIT(7),
	ATH9K_HW_CAP_MCAST_KEYSEARCH            = BIT(8),
	ATH9K_HW_CAP_HT                         = BIT(9),
	ATH9K_HW_CAP_GTT                        = BIT(10),
	ATH9K_HW_CAP_FASTCC                     = BIT(11),
	ATH9K_HW_CAP_RFSILENT                   = BIT(12),
	ATH9K_HW_CAP_CST                        = BIT(13),
	ATH9K_HW_CAP_ENHANCEDPM                 = BIT(14),
	ATH9K_HW_CAP_AUTOSLEEP                  = BIT(15),
	ATH9K_HW_CAP_4KB_SPLITTRANS             = BIT(16),
};

enum ath9k_capability_type {
	ATH9K_CAP_CIPHER = 0,
	ATH9K_CAP_TKIP_MIC,
	ATH9K_CAP_TKIP_SPLIT,
	ATH9K_CAP_DIVERSITY,
	ATH9K_CAP_TXPOW,
	ATH9K_CAP_MCAST_KEYSRCH,
	ATH9K_CAP_DS
};

struct ath9k_hw_capabilities {
	u32 hw_caps; /* ATH9K_HW_CAP_* from ath9k_hw_caps */
	DECLARE_BITMAP(wireless_modes, ATH9K_MODE_MAX); /* ATH9K_MODE_* */
	u16 total_queues;
	u16 keycache_size;
	u16 low_5ghz_chan, high_5ghz_chan;
	u16 low_2ghz_chan, high_2ghz_chan;
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
	u8 pcie_clock_req;
	u32 pcie_waen;
	u8 analog_shiftreg;
	u8 ht_enable;
	u32 ofdm_trig_low;
	u32 ofdm_trig_high;
	u32 cck_trig_high;
	u32 cck_trig_low;
	u32 enable_ani;
	enum ath9k_ant_setting diversity_control;
	u16 antenna_switch_swap;
	int serialize_regmode;
	bool intr_mitigation;
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
	ATH9K_INT_TSFOOR = 0x04000000,
	ATH9K_INT_GENTIMER = 0x08000000,
	ATH9K_INT_CST = 0x10000000,
	ATH9K_INT_GTT = 0x20000000,
	ATH9K_INT_FATAL = 0x40000000,
	ATH9K_INT_GLOBAL = 0x80000000,
	ATH9K_INT_BMISC = ATH9K_INT_TIM |
		ATH9K_INT_DTIM |
		ATH9K_INT_DTIMSYNC |
		ATH9K_INT_TSFOOR |
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
	struct ieee80211_channel *chan;
	u16 channel;
	u32 channelFlags;
	u32 chanmode;
	int32_t CalValid;
	bool oneTimeCalsDone;
	int8_t iCoff;
	int8_t qCoff;
	int16_t rawNoiseFloor;
};

#define IS_CHAN_G(_c) ((((_c)->channelFlags & (CHANNEL_G)) == CHANNEL_G) || \
       (((_c)->channelFlags & CHANNEL_G_HT20) == CHANNEL_G_HT20) || \
       (((_c)->channelFlags & CHANNEL_G_HT40PLUS) == CHANNEL_G_HT40PLUS) || \
       (((_c)->channelFlags & CHANNEL_G_HT40MINUS) == CHANNEL_G_HT40MINUS))
#define IS_CHAN_OFDM(_c) (((_c)->channelFlags & CHANNEL_OFDM) != 0)
#define IS_CHAN_5GHZ(_c) (((_c)->channelFlags & CHANNEL_5GHZ) != 0)
#define IS_CHAN_2GHZ(_c) (((_c)->channelFlags & CHANNEL_2GHZ) != 0)
#define IS_CHAN_HALF_RATE(_c) (((_c)->channelFlags & CHANNEL_HALF) != 0)
#define IS_CHAN_QUARTER_RATE(_c) (((_c)->channelFlags & CHANNEL_QUARTER) != 0)
#define IS_CHAN_A_5MHZ_SPACED(_c)			\
	((((_c)->channelFlags & CHANNEL_5GHZ) != 0) &&	\
	 (((_c)->channel % 20) != 0) &&			\
	 (((_c)->channel % 10) != 0))

/* These macros check chanmode and not channelFlags */
#define IS_CHAN_B(_c) ((_c)->chanmode == CHANNEL_B)
#define IS_CHAN_HT20(_c) (((_c)->chanmode == CHANNEL_A_HT20) ||	\
			  ((_c)->chanmode == CHANNEL_G_HT20))
#define IS_CHAN_HT40(_c) (((_c)->chanmode == CHANNEL_A_HT40PLUS) ||	\
			  ((_c)->chanmode == CHANNEL_A_HT40MINUS) ||	\
			  ((_c)->chanmode == CHANNEL_G_HT40PLUS) ||	\
			  ((_c)->chanmode == CHANNEL_G_HT40MINUS))
#define IS_CHAN_HT(_c) (IS_CHAN_HT20((_c)) || IS_CHAN_HT40((_c)))

enum ath9k_power_mode {
	ATH9K_PM_AWAKE = 0,
	ATH9K_PM_FULL_SLEEP,
	ATH9K_PM_NETWORK_SLEEP,
	ATH9K_PM_UNDEFINED
};

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

struct ath9k_beacon_state {
	u32 bs_nexttbtt;
	u32 bs_nextdtim;
	u32 bs_intval;
#define ATH9K_BEACON_PERIOD       0x0000ffff
#define ATH9K_BEACON_ENA          0x00800000
#define ATH9K_BEACON_RESET_TSF    0x01000000
#define ATH9K_TSFOOR_THRESHOLD    0x00004240 /* 16k us */
	u32 bs_dtimperiod;
	u16 bs_cfpperiod;
	u16 bs_cfpmaxduration;
	u32 bs_cfpnext;
	u16 bs_timoffset;
	u16 bs_bmissthreshold;
	u32 bs_sleepduration;
	u32 bs_tsfoor_threshold;
};

struct chan_centers {
	u16 synth_center;
	u16 ctl_center;
	u16 ext_center;
};

enum {
	ATH9K_RESET_POWER_ON,
	ATH9K_RESET_WARM,
	ATH9K_RESET_COLD,
};

struct ath9k_hw_version {
	u32 magic;
	u16 devid;
	u16 subvendorid;
	u32 macVersion;
	u16 macRev;
	u16 phyRev;
	u16 analog5GhzRev;
	u16 analog2GhzRev;
	u16 subsysid;
};

/* Generic TSF timer definitions */

#define ATH_MAX_GEN_TIMER	16

#define AR_GENTMR_BIT(_index)	(1 << (_index))

/*
 * Using de Bruijin sequence to to look up 1's index in a 32 bit number
 * debruijn32 = 0000 0111 0111 1100 1011 0101 0011 0001
 */
#define debruijn32 0x077CB531UL

struct ath_gen_timer_configuration {
	u32 next_addr;
	u32 period_addr;
	u32 mode_addr;
	u32 mode_mask;
};

struct ath_gen_timer {
	void (*trigger)(void *arg);
	void (*overflow)(void *arg);
	void *arg;
	u8 index;
};

struct ath_gen_timer_table {
	u32 gen_timer_index[32];
	struct ath_gen_timer *timers[ATH_MAX_GEN_TIMER];
	union {
		unsigned long timer_bits;
		u16 val;
	} timer_mask;
};

struct ath_hw {
	struct ieee80211_hw *hw;
	struct ath_common common;
	struct ath9k_hw_version hw_version;
	struct ath9k_ops_config config;
	struct ath9k_hw_capabilities caps;
	struct ath9k_channel channels[38];
	struct ath9k_channel *curchan;

	union {
		struct ar5416_eeprom_def def;
		struct ar5416_eeprom_4k map4k;
		struct ar9287_eeprom map9287;
	} eeprom;
	const struct eeprom_ops *eep_ops;
	enum ath9k_eep_map eep_map;

	bool sw_mgmt_crypto;
	bool is_pciexpress;
	u16 tx_trig_level;
	u16 rfsilent;
	u32 rfkill_gpio;
	u32 rfkill_polarity;
	u32 ah_flags;

	bool htc_reset_init;

	enum nl80211_iftype opmode;
	enum ath9k_power_mode power_mode;

	struct ath9k_nfcal_hist nfCalHist[NUM_NF_READINGS];
	struct ath9k_pacal_info pacal_info;
	struct ar5416Stats stats;
	struct ath9k_tx_queue_info txq[ATH9K_NUM_TX_QUEUES];

	int16_t curchan_rad_index;
	u32 mask_reg;
	u32 txok_interrupt_mask;
	u32 txerr_interrupt_mask;
	u32 txdesc_interrupt_mask;
	u32 txeol_interrupt_mask;
	u32 txurn_interrupt_mask;
	bool chip_fullsleep;
	u32 atim_window;

	/* Calibration */
	enum ath9k_cal_types supp_cals;
	struct ath9k_cal_list iq_caldata;
	struct ath9k_cal_list adcgain_caldata;
	struct ath9k_cal_list adcdc_calinitdata;
	struct ath9k_cal_list adcdc_caldata;
	struct ath9k_cal_list *cal_list;
	struct ath9k_cal_list *cal_list_last;
	struct ath9k_cal_list *cal_list_curr;
#define totalPowerMeasI meas0.unsign
#define totalPowerMeasQ meas1.unsign
#define totalIqCorrMeas meas2.sign
#define totalAdcIOddPhase  meas0.unsign
#define totalAdcIEvenPhase meas1.unsign
#define totalAdcQOddPhase  meas2.unsign
#define totalAdcQEvenPhase meas3.unsign
#define totalAdcDcOffsetIOddPhase  meas0.sign
#define totalAdcDcOffsetIEvenPhase meas1.sign
#define totalAdcDcOffsetQOddPhase  meas2.sign
#define totalAdcDcOffsetQEvenPhase meas3.sign
	union {
		u32 unsign[AR5416_MAX_CHAINS];
		int32_t sign[AR5416_MAX_CHAINS];
	} meas0;
	union {
		u32 unsign[AR5416_MAX_CHAINS];
		int32_t sign[AR5416_MAX_CHAINS];
	} meas1;
	union {
		u32 unsign[AR5416_MAX_CHAINS];
		int32_t sign[AR5416_MAX_CHAINS];
	} meas2;
	union {
		u32 unsign[AR5416_MAX_CHAINS];
		int32_t sign[AR5416_MAX_CHAINS];
	} meas3;
	u16 cal_samples;

	u32 sta_id1_defaults;
	u32 misc_mode;
	enum {
		AUTO_32KHZ,
		USE_32KHZ,
		DONT_USE_32KHZ,
	} enable_32kHz_clock;

	/* RF */
	u32 *analogBank0Data;
	u32 *analogBank1Data;
	u32 *analogBank2Data;
	u32 *analogBank3Data;
	u32 *analogBank6Data;
	u32 *analogBank6TPCData;
	u32 *analogBank7Data;
	u32 *addac5416_21;
	u32 *bank6Temp;

	int16_t txpower_indexoffset;
	u32 beacon_interval;
	u32 slottime;
	u32 acktimeout;
	u32 ctstimeout;
	u32 globaltxtimeout;
	u8 gbeacon_rate;

	/* ANI */
	u32 proc_phyerr;
	u32 aniperiod;
	struct ar5416AniState *curani;
	struct ar5416AniState ani[255];
	int totalSizeDesired[5];
	int coarse_high[5];
	int coarse_low[5];
	int firpwr[5];
	enum ath9k_ani_cmd ani_function;

	/* Bluetooth coexistance */
	struct ath_btcoex_hw btcoex_hw;

	u32 intr_txqs;
	u8 txchainmask;
	u8 rxchainmask;

	u32 originalGain[22];
	int initPDADC;
	int PDADCdelta;
	u8 led_pin;

	struct ar5416IniArray iniModes;
	struct ar5416IniArray iniCommon;
	struct ar5416IniArray iniBank0;
	struct ar5416IniArray iniBB_RfGain;
	struct ar5416IniArray iniBank1;
	struct ar5416IniArray iniBank2;
	struct ar5416IniArray iniBank3;
	struct ar5416IniArray iniBank6;
	struct ar5416IniArray iniBank6TPC;
	struct ar5416IniArray iniBank7;
	struct ar5416IniArray iniAddac;
	struct ar5416IniArray iniPcieSerdes;
	struct ar5416IniArray iniModesAdditional;
	struct ar5416IniArray iniModesRxGain;
	struct ar5416IniArray iniModesTxGain;
	struct ar5416IniArray iniCckfirNormal;
	struct ar5416IniArray iniCckfirJapan2484;

	u32 intr_gen_timer_trigger;
	u32 intr_gen_timer_thresh;
	struct ath_gen_timer_table hw_gen_timers;
};

static inline struct ath_common *ath9k_hw_common(struct ath_hw *ah)
{
	return &ah->common;
}

static inline struct ath_regulatory *ath9k_hw_regulatory(struct ath_hw *ah)
{
	return &(ath9k_hw_common(ah)->regulatory);
}

/* Initialization, Detach, Reset */
const char *ath9k_hw_probe(u16 vendorid, u16 devid);
void ath9k_hw_detach(struct ath_hw *ah);
int ath9k_hw_init(struct ath_hw *ah);
void ath9k_hw_rf_free(struct ath_hw *ah);
int ath9k_hw_reset(struct ath_hw *ah, struct ath9k_channel *chan,
		   bool bChannelChange);
void ath9k_hw_fill_cap_info(struct ath_hw *ah);
bool ath9k_hw_getcapability(struct ath_hw *ah, enum ath9k_capability_type type,
			    u32 capability, u32 *result);
bool ath9k_hw_setcapability(struct ath_hw *ah, enum ath9k_capability_type type,
			    u32 capability, u32 setting, int *status);

/* Key Cache Management */
bool ath9k_hw_keyreset(struct ath_hw *ah, u16 entry);
bool ath9k_hw_keysetmac(struct ath_hw *ah, u16 entry, const u8 *mac);
bool ath9k_hw_set_keycache_entry(struct ath_hw *ah, u16 entry,
				 const struct ath9k_keyval *k,
				 const u8 *mac);
bool ath9k_hw_keyisvalid(struct ath_hw *ah, u16 entry);

/* GPIO / RFKILL / Antennae */
void ath9k_hw_cfg_gpio_input(struct ath_hw *ah, u32 gpio);
u32 ath9k_hw_gpio_get(struct ath_hw *ah, u32 gpio);
void ath9k_hw_cfg_output(struct ath_hw *ah, u32 gpio,
			 u32 ah_signal_type);
void ath9k_hw_set_gpio(struct ath_hw *ah, u32 gpio, u32 val);
u32 ath9k_hw_getdefantenna(struct ath_hw *ah);
void ath9k_hw_setantenna(struct ath_hw *ah, u32 antenna);
bool ath9k_hw_setantennaswitch(struct ath_hw *ah,
			       enum ath9k_ant_setting settings,
			       struct ath9k_channel *chan,
			       u8 *tx_chainmask, u8 *rx_chainmask,
			       u8 *antenna_cfgd);

/* General Operation */
bool ath9k_hw_wait(struct ath_hw *ah, u32 reg, u32 mask, u32 val, u32 timeout);
u32 ath9k_hw_reverse_bits(u32 val, u32 n);
bool ath9k_get_channel_edges(struct ath_hw *ah, u16 flags, u16 *low, u16 *high);
u16 ath9k_hw_computetxtime(struct ath_hw *ah,
			   const struct ath_rate_table *rates,
			   u32 frameLen, u16 rateix, bool shortPreamble);
void ath9k_hw_get_channel_centers(struct ath_hw *ah,
				  struct ath9k_channel *chan,
				  struct chan_centers *centers);
u32 ath9k_hw_getrxfilter(struct ath_hw *ah);
void ath9k_hw_setrxfilter(struct ath_hw *ah, u32 bits);
bool ath9k_hw_phy_disable(struct ath_hw *ah);
bool ath9k_hw_disable(struct ath_hw *ah);
void ath9k_hw_set_txpowerlimit(struct ath_hw *ah, u32 limit);
void ath9k_hw_setmac(struct ath_hw *ah, const u8 *mac);
void ath9k_hw_setopmode(struct ath_hw *ah);
void ath9k_hw_setmcastfilter(struct ath_hw *ah, u32 filter0, u32 filter1);
void ath9k_hw_setbssidmask(struct ath_hw *ah);
void ath9k_hw_write_associd(struct ath_hw *ah);
u64 ath9k_hw_gettsf64(struct ath_hw *ah);
void ath9k_hw_settsf64(struct ath_hw *ah, u64 tsf64);
void ath9k_hw_reset_tsf(struct ath_hw *ah);
void ath9k_hw_set_tsfadjust(struct ath_hw *ah, u32 setting);
bool ath9k_hw_setslottime(struct ath_hw *ah, u32 us);
void ath9k_hw_set11nmac2040(struct ath_hw *ah);
void ath9k_hw_beaconinit(struct ath_hw *ah, u32 next_beacon, u32 beacon_period);
void ath9k_hw_set_sta_beacon_timers(struct ath_hw *ah,
				    const struct ath9k_beacon_state *bs);

bool ath9k_hw_setpower(struct ath_hw *ah, enum ath9k_power_mode mode);

void ath9k_hw_configpcipowersave(struct ath_hw *ah, int restore, int power_off);

/* Interrupt Handling */
bool ath9k_hw_intrpend(struct ath_hw *ah);
bool ath9k_hw_getisr(struct ath_hw *ah, enum ath9k_int *masked);
enum ath9k_int ath9k_hw_set_interrupts(struct ath_hw *ah, enum ath9k_int ints);

/* Generic hw timer primitives */
struct ath_gen_timer *ath_gen_timer_alloc(struct ath_hw *ah,
					  void (*trigger)(void *),
					  void (*overflow)(void *),
					  void *arg,
					  u8 timer_index);
void ath9k_hw_gen_timer_start(struct ath_hw *ah,
			      struct ath_gen_timer *timer,
			      u32 timer_next,
			      u32 timer_period);
void ath9k_hw_gen_timer_stop(struct ath_hw *ah, struct ath_gen_timer *timer);

void ath_gen_timer_free(struct ath_hw *ah, struct ath_gen_timer *timer);
void ath_gen_timer_isr(struct ath_hw *hw);
u32 ath9k_hw_gettsf32(struct ath_hw *ah);

const char *ath9k_hw_mac_bb_name(u32 mac_bb_version);
const char *ath9k_hw_rf_name(u16 rf_version);

#define ATH_PCIE_CAP_LINK_CTRL	0x70
#define ATH_PCIE_CAP_LINK_L0S	1
#define ATH_PCIE_CAP_LINK_L1	2

#endif
