/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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
#include <linux/firmware.h>

#include "mac.h"
#include "ani.h"
#include "eeprom.h"
#include "calib.h"
#include "reg.h"
#include "reg_mci.h"
#include "phy.h"
#include "btcoex.h"
#include "dynack.h"

#include "../regd.h"

#define ATHEROS_VENDOR_ID	0x168c

#define AR5416_DEVID_PCI	0x0023
#define AR5416_DEVID_PCIE	0x0024
#define AR9160_DEVID_PCI	0x0027
#define AR9280_DEVID_PCI	0x0029
#define AR9280_DEVID_PCIE	0x002a
#define AR9285_DEVID_PCIE	0x002b
#define AR2427_DEVID_PCIE	0x002c
#define AR9287_DEVID_PCI	0x002d
#define AR9287_DEVID_PCIE	0x002e
#define AR9300_DEVID_PCIE	0x0030
#define AR9300_DEVID_AR9340	0x0031
#define AR9300_DEVID_AR9485_PCIE 0x0032
#define AR9300_DEVID_AR9580	0x0033
#define AR9300_DEVID_AR9462	0x0034
#define AR9300_DEVID_AR9330	0x0035
#define AR9300_DEVID_QCA955X	0x0038
#define AR9485_DEVID_AR1111	0x0037
#define AR9300_DEVID_AR9565     0x0036
#define AR9300_DEVID_AR953X     0x003d
#define AR9300_DEVID_QCA956X    0x003f

#define AR5416_AR9100_DEVID	0x000b

#define	AR_SUBVENDOR_ID_NOG	0x0e11
#define AR_SUBVENDOR_ID_NEW_A	0x7065
#define AR5416_MAGIC		0x19641014

#define AR9280_COEX2WIRE_SUBSYSID	0x309b
#define AT9285_COEX3WIRE_SA_SUBSYSID	0x30aa
#define AT9285_COEX3WIRE_DA_SUBSYSID	0x30ab

#define ATH_AMPDU_LIMIT_MAX        (64 * 1024 - 1)

#define	ATH_DEFAULT_NOISE_FLOOR -95

#define ATH9K_RSSI_BAD			-128

#define ATH9K_NUM_CHANNELS	38

/* Register read/write primitives */
#define REG_WRITE(_ah, _reg, _val) \
	(_ah)->reg_ops.write((_ah), (_val), (_reg))

#define REG_READ(_ah, _reg) \
	(_ah)->reg_ops.read((_ah), (_reg))

#define REG_READ_MULTI(_ah, _addr, _val, _cnt)		\
	(_ah)->reg_ops.multi_read((_ah), (_addr), (_val), (_cnt))

#define REG_RMW(_ah, _reg, _set, _clr) \
	(_ah)->reg_ops.rmw((_ah), (_reg), (_set), (_clr))

#define ENABLE_REGWRITE_BUFFER(_ah)					\
	do {								\
		if ((_ah)->reg_ops.enable_write_buffer)	\
			(_ah)->reg_ops.enable_write_buffer((_ah)); \
	} while (0)

#define REGWRITE_BUFFER_FLUSH(_ah)					\
	do {								\
		if ((_ah)->reg_ops.write_flush)		\
			(_ah)->reg_ops.write_flush((_ah));	\
	} while (0)

#define ENABLE_REG_RMW_BUFFER(_ah)					\
	do {								\
		if ((_ah)->reg_ops.enable_rmw_buffer)	\
			(_ah)->reg_ops.enable_rmw_buffer((_ah)); \
	} while (0)

#define REG_RMW_BUFFER_FLUSH(_ah)					\
	do {								\
		if ((_ah)->reg_ops.rmw_flush)		\
			(_ah)->reg_ops.rmw_flush((_ah));	\
	} while (0)

#define PR_EEP(_s, _val)						\
	do {								\
		len += scnprintf(buf + len, size - len, "%20s : %10d\n",\
				 _s, (_val));				\
	} while (0)

#define SM(_v, _f)  (((_v) << _f##_S) & _f)
#define MS(_v, _f)  (((_v) & _f) >> _f##_S)
#define REG_RMW_FIELD(_a, _r, _f, _v) \
	REG_RMW(_a, _r, (((_v) << _f##_S) & _f), (_f))
#define REG_READ_FIELD(_a, _r, _f) \
	(((REG_READ(_a, _r) & _f) >> _f##_S))
#define REG_SET_BIT(_a, _r, _f) \
	REG_RMW(_a, _r, (_f), 0)
#define REG_CLR_BIT(_a, _r, _f) \
	REG_RMW(_a, _r, 0, (_f))

#define DO_DELAY(x) do {					\
		if (((++(x) % 64) == 0) &&			\
		    (ath9k_hw_common(ah)->bus_ops->ath_bus_type	\
			!= ATH_USB))				\
			udelay(1);				\
	} while (0)

#define REG_WRITE_ARRAY(iniarray, column, regWr) \
	ath9k_hw_write_array(ah, iniarray, column, &(regWr))
#define REG_READ_ARRAY(ah, array, size) \
	ath9k_hw_read_array(ah, array, size)

#define AR_GPIO_OUTPUT_MUX_AS_OUTPUT             0
#define AR_GPIO_OUTPUT_MUX_AS_PCIE_ATTENTION_LED 1
#define AR_GPIO_OUTPUT_MUX_AS_PCIE_POWER_LED     2
#define AR_GPIO_OUTPUT_MUX_AS_TX_FRAME           3
#define AR_GPIO_OUTPUT_MUX_AS_RX_CLEAR_EXTERNAL  4
#define AR_GPIO_OUTPUT_MUX_AS_MAC_NETWORK_LED    5
#define AR_GPIO_OUTPUT_MUX_AS_MAC_POWER_LED      6
#define AR_GPIO_OUTPUT_MUX_AS_MCI_WLAN_DATA      0x16
#define AR_GPIO_OUTPUT_MUX_AS_MCI_WLAN_CLK       0x17
#define AR_GPIO_OUTPUT_MUX_AS_MCI_BT_DATA        0x18
#define AR_GPIO_OUTPUT_MUX_AS_MCI_BT_CLK         0x19
#define AR_GPIO_OUTPUT_MUX_AS_WL_IN_TX           0x14
#define AR_GPIO_OUTPUT_MUX_AS_WL_IN_RX           0x13
#define AR_GPIO_OUTPUT_MUX_AS_BT_IN_TX           9
#define AR_GPIO_OUTPUT_MUX_AS_BT_IN_RX           8
#define AR_GPIO_OUTPUT_MUX_AS_RUCKUS_STROBE      0x1d
#define AR_GPIO_OUTPUT_MUX_AS_RUCKUS_DATA        0x1e

#define AR_GPIOD_MASK               0x00001FFF

#define BASE_ACTIVATE_DELAY         100
#define RTC_PLL_SETTLE_DELAY        (AR_SREV_9340(ah) ? 1000 : 100)
#define COEF_SCALE_S                24
#define HT40_CHANNEL_CENTER_SHIFT   10

#define ATH9K_ANTENNA0_CHAINMASK    0x1
#define ATH9K_ANTENNA1_CHAINMASK    0x2

#define ATH9K_NUM_DMA_DEBUG_REGS    8
#define ATH9K_NUM_QUEUES            10

#define MAX_RATE_POWER              63
#define MAX_COMBINED_POWER          254 /* 128 dBm, chosen to fit in u8 */
#define AH_WAIT_TIMEOUT             100000 /* (us) */
#define AH_TSF_WRITE_TIMEOUT        100    /* (us) */
#define AH_TIME_QUANTUM             10
#define AR_KEYTABLE_SIZE            128
#define POWER_UP_TIME               10000
#define SPUR_RSSI_THRESH            40
#define UPPER_5G_SUB_BAND_START		5700
#define MID_5G_SUB_BAND_START		5400

#define CAB_TIMEOUT_VAL             10
#define BEACON_TIMEOUT_VAL          10
#define MIN_BEACON_TIMEOUT_VAL      1
#define SLEEP_SLOP                  TU_TO_USEC(3)

#define INIT_CONFIG_STATUS          0x00000000
#define INIT_RSSI_THR               0x00000700
#define INIT_BCON_CNTRL_REG         0x00000000

#define TU_TO_USEC(_tu)             ((_tu) << 10)

#define ATH9K_HW_RX_HP_QDEPTH	16
#define ATH9K_HW_RX_LP_QDEPTH	128

#define PAPRD_GAIN_TABLE_ENTRIES	32
#define PAPRD_TABLE_SZ			24
#define PAPRD_IDEAL_AGC2_PWR_RANGE	0xe0

/*
 * Wake on Wireless
 */

/* Keep Alive Frame */
#define KAL_FRAME_LEN		28
#define KAL_FRAME_TYPE		0x2	/* data frame */
#define KAL_FRAME_SUB_TYPE	0x4	/* null data frame */
#define KAL_DURATION_ID		0x3d
#define KAL_NUM_DATA_WORDS	6
#define KAL_NUM_DESC_WORDS	12
#define KAL_ANTENNA_MODE	1
#define KAL_TO_DS		1
#define KAL_DELAY		4	/* delay of 4ms between 2 KAL frames */
#define KAL_TIMEOUT		900

#define MAX_PATTERN_SIZE		256
#define MAX_PATTERN_MASK_SIZE		32
#define MAX_NUM_PATTERN			16
#define MAX_NUM_PATTERN_LEGACY		8
#define MAX_NUM_USER_PATTERN		6 /*  deducting the disassociate and
					      deauthenticate packets */

/*
 * WoW trigger mapping to hardware code
 */

#define AH_WOW_USER_PATTERN_EN		BIT(0)
#define AH_WOW_MAGIC_PATTERN_EN		BIT(1)
#define AH_WOW_LINK_CHANGE		BIT(2)
#define AH_WOW_BEACON_MISS		BIT(3)

enum ath_hw_txq_subtype {
	ATH_TXQ_AC_BK = 0,
	ATH_TXQ_AC_BE = 1,
	ATH_TXQ_AC_VI = 2,
	ATH_TXQ_AC_VO = 3,
};

enum ath_ini_subsys {
	ATH_INI_PRE = 0,
	ATH_INI_CORE,
	ATH_INI_POST,
	ATH_INI_NUM_SPLIT,
};

enum ath9k_hw_caps {
	ATH9K_HW_CAP_HT                         = BIT(0),
	ATH9K_HW_CAP_RFSILENT                   = BIT(1),
	ATH9K_HW_CAP_AUTOSLEEP                  = BIT(2),
	ATH9K_HW_CAP_4KB_SPLITTRANS             = BIT(3),
	ATH9K_HW_CAP_EDMA			= BIT(4),
	ATH9K_HW_CAP_RAC_SUPPORTED		= BIT(5),
	ATH9K_HW_CAP_LDPC			= BIT(6),
	ATH9K_HW_CAP_FASTCLOCK			= BIT(7),
	ATH9K_HW_CAP_SGI_20			= BIT(8),
	ATH9K_HW_CAP_ANT_DIV_COMB		= BIT(10),
	ATH9K_HW_CAP_2GHZ			= BIT(11),
	ATH9K_HW_CAP_5GHZ			= BIT(12),
	ATH9K_HW_CAP_APM			= BIT(13),
#ifdef CONFIG_ATH9K_PCOEM
	ATH9K_HW_CAP_RTT			= BIT(14),
	ATH9K_HW_CAP_MCI			= BIT(15),
	ATH9K_HW_CAP_BT_ANT_DIV			= BIT(17),
#else
	ATH9K_HW_CAP_RTT			= 0,
	ATH9K_HW_CAP_MCI			= 0,
	ATH9K_HW_CAP_BT_ANT_DIV			= 0,
#endif
	ATH9K_HW_CAP_DFS			= BIT(18),
	ATH9K_HW_CAP_PAPRD			= BIT(19),
	ATH9K_HW_CAP_FCC_BAND_SWITCH		= BIT(20),
};

/*
 * WoW device capabilities
 * @ATH9K_HW_WOW_DEVICE_CAPABLE: device revision is capable of WoW.
 * @ATH9K_HW_WOW_PATTERN_MATCH_EXACT: device is capable of matching
 * an exact user defined pattern or de-authentication/disassoc pattern.
 * @ATH9K_HW_WOW_PATTERN_MATCH_DWORD: device requires the first four
 * bytes of the pattern for user defined pattern, de-authentication and
 * disassociation patterns for all types of possible frames received
 * of those types.
 */

struct ath9k_hw_wow {
	u32 wow_event_mask;
	u32 wow_event_mask2;
	u8 max_patterns;
};

struct ath9k_hw_capabilities {
	u32 hw_caps; /* ATH9K_HW_CAP_* from ath9k_hw_caps */
	u16 rts_aggr_limit;
	u8 tx_chainmask;
	u8 rx_chainmask;
	u8 chip_chainmask;
	u8 max_txchains;
	u8 max_rxchains;
	u8 num_gpio_pins;
	u32 gpio_mask;
	u32 gpio_requested;
	u8 rx_hp_qdepth;
	u8 rx_lp_qdepth;
	u8 rx_status_len;
	u8 tx_desc_len;
	u8 txs_len;
};

#define AR_NO_SPUR      	0x8000
#define AR_BASE_FREQ_2GHZ   	2300
#define AR_BASE_FREQ_5GHZ   	4900
#define AR_SPUR_FEEQ_BOUND_HT40 19
#define AR_SPUR_FEEQ_BOUND_HT20 10

enum ath9k_hw_hang_checks {
	HW_BB_WATCHDOG            = BIT(0),
	HW_PHYRESTART_CLC_WAR     = BIT(1),
	HW_BB_RIFS_HANG           = BIT(2),
	HW_BB_DFS_HANG            = BIT(3),
	HW_BB_RX_CLEAR_STUCK_HANG = BIT(4),
	HW_MAC_HANG               = BIT(5),
};

#define AR_PCIE_PLL_PWRSAVE_CONTROL BIT(0)
#define AR_PCIE_PLL_PWRSAVE_ON_D3   BIT(1)
#define AR_PCIE_PLL_PWRSAVE_ON_D0   BIT(2)
#define AR_PCIE_CDR_PWRSAVE_ON_D3   BIT(3)
#define AR_PCIE_CDR_PWRSAVE_ON_D0   BIT(4)

struct ath9k_ops_config {
	int dma_beacon_response_time;
	int sw_beacon_response_time;
	bool cwm_ignore_extcca;
	u32 pcie_waen;
	u8 analog_shiftreg;
	u32 ofdm_trig_low;
	u32 ofdm_trig_high;
	u32 cck_trig_high;
	u32 cck_trig_low;
	bool enable_paprd;
	int serialize_regmode;
	bool rx_intr_mitigation;
	bool tx_intr_mitigation;
	u8 max_txtrig_level;
	u16 ani_poll_interval; /* ANI poll interval in ms */
	u16 hw_hang_checks;
	u16 rimt_first;
	u16 rimt_last;

	/* Platform specific config */
	u32 aspm_l1_fix;
	u32 xlna_gpio;
	u32 ant_ctrl_comm2g_switch_enable;
	bool xatten_margin_cfg;
	bool alt_mingainidx;
	u8 pll_pwrsave;
	bool tx_gain_buffalo;
	bool led_active_high;
};

enum ath9k_int {
	ATH9K_INT_RX = 0x00000001,
	ATH9K_INT_RXDESC = 0x00000002,
	ATH9K_INT_RXHP = 0x00000001,
	ATH9K_INT_RXLP = 0x00000002,
	ATH9K_INT_RXNOFRM = 0x00000008,
	ATH9K_INT_RXEOL = 0x00000010,
	ATH9K_INT_RXORN = 0x00000020,
	ATH9K_INT_TX = 0x00000040,
	ATH9K_INT_TXDESC = 0x00000080,
	ATH9K_INT_TIM_TIMER = 0x00000100,
	ATH9K_INT_MCI = 0x00000200,
	ATH9K_INT_BB_WATCHDOG = 0x00000400,
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

#define MAX_RTT_TABLE_ENTRY     6
#define MAX_IQCAL_MEASUREMENT	8
#define MAX_CL_TAB_ENTRY	16
#define CL_TAB_ENTRY(reg_base)	(reg_base + (4 * j))

enum ath9k_cal_flags {
	RTT_DONE,
	PAPRD_PACKET_SENT,
	PAPRD_DONE,
	NFCAL_PENDING,
	NFCAL_INTF,
	TXIQCAL_DONE,
	TXCLCAL_DONE,
	SW_PKDET_DONE,
	LONGCAL_PENDING,
};

struct ath9k_hw_cal_data {
	u16 channel;
	u16 channelFlags;
	unsigned long cal_flags;
	int32_t CalValid;
	int8_t iCoff;
	int8_t qCoff;
	u8 caldac[2];
	u16 small_signal_gain[AR9300_MAX_CHAINS];
	u32 pa_table[AR9300_MAX_CHAINS][PAPRD_TABLE_SZ];
	u32 num_measures[AR9300_MAX_CHAINS];
	int tx_corr_coeff[MAX_IQCAL_MEASUREMENT][AR9300_MAX_CHAINS];
	u32 tx_clcal[AR9300_MAX_CHAINS][MAX_CL_TAB_ENTRY];
	u32 rtt_table[AR9300_MAX_CHAINS][MAX_RTT_TABLE_ENTRY];
	struct ath9k_nfcal_hist nfCalHist[NUM_NF_READINGS];
};

struct ath9k_channel {
	struct ieee80211_channel *chan;
	u16 channel;
	u16 channelFlags;
	s16 noisefloor;
};

#define CHANNEL_5GHZ		BIT(0)
#define CHANNEL_HALF		BIT(1)
#define CHANNEL_QUARTER		BIT(2)
#define CHANNEL_HT		BIT(3)
#define CHANNEL_HT40PLUS	BIT(4)
#define CHANNEL_HT40MINUS	BIT(5)

#define IS_CHAN_5GHZ(_c) (!!((_c)->channelFlags & CHANNEL_5GHZ))
#define IS_CHAN_2GHZ(_c) (!IS_CHAN_5GHZ(_c))

#define IS_CHAN_HALF_RATE(_c) (!!((_c)->channelFlags & CHANNEL_HALF))
#define IS_CHAN_QUARTER_RATE(_c) (!!((_c)->channelFlags & CHANNEL_QUARTER))
#define IS_CHAN_A_FAST_CLOCK(_ah, _c)			\
	(IS_CHAN_5GHZ(_c) && ((_ah)->caps.hw_caps & ATH9K_HW_CAP_FASTCLOCK))

#define IS_CHAN_HT(_c) ((_c)->channelFlags & CHANNEL_HT)

#define IS_CHAN_HT20(_c) (IS_CHAN_HT(_c) && !IS_CHAN_HT40(_c))

#define IS_CHAN_HT40(_c) \
	(!!((_c)->channelFlags & (CHANNEL_HT40PLUS | CHANNEL_HT40MINUS)))

#define IS_CHAN_HT40PLUS(_c) ((_c)->channelFlags & CHANNEL_HT40PLUS)
#define IS_CHAN_HT40MINUS(_c) ((_c)->channelFlags & CHANNEL_HT40MINUS)

enum ath9k_power_mode {
	ATH9K_PM_AWAKE = 0,
	ATH9K_PM_FULL_SLEEP,
	ATH9K_PM_NETWORK_SLEEP,
	ATH9K_PM_UNDEFINED
};

enum ser_reg_mode {
	SER_REG_MODE_OFF = 0,
	SER_REG_MODE_ON = 1,
	SER_REG_MODE_AUTO = 2,
};

enum ath9k_rx_qtype {
	ATH9K_RX_QUEUE_HP,
	ATH9K_RX_QUEUE_LP,
	ATH9K_RX_QUEUE_MAX,
};

struct ath9k_beacon_state {
	u32 bs_nexttbtt;
	u32 bs_nextdtim;
	u32 bs_intval;
#define ATH9K_TSFOOR_THRESHOLD    0x00004240 /* 16k us */
	u32 bs_dtimperiod;
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
	enum ath_usb_dev usbdev;
};

/* Generic TSF timer definitions */

#define ATH_MAX_GEN_TIMER	16

#define AR_GENTMR_BIT(_index)	(1 << (_index))

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
	struct ath_gen_timer *timers[ATH_MAX_GEN_TIMER];
	u16 timer_mask;
	bool tsf2_enabled;
};

struct ath_hw_antcomb_conf {
	u8 main_lna_conf;
	u8 alt_lna_conf;
	u8 fast_div_bias;
	u8 main_gaintb;
	u8 alt_gaintb;
	int lna1_lna2_delta;
	int lna1_lna2_switch_delta;
	u8 div_group;
};

/**
 * struct ath_hw_radar_conf - radar detection initialization parameters
 *
 * @pulse_inband: threshold for checking the ratio of in-band power
 *	to total power for short radar pulses (half dB steps)
 * @pulse_inband_step: threshold for checking an in-band power to total
 *	power ratio increase for short radar pulses (half dB steps)
 * @pulse_height: threshold for detecting the beginning of a short
 *	radar pulse (dB step)
 * @pulse_rssi: threshold for detecting if a short radar pulse is
 *	gone (dB step)
 * @pulse_maxlen: maximum pulse length (0.8 us steps)
 *
 * @radar_rssi: RSSI threshold for starting long radar detection (dB steps)
 * @radar_inband: threshold for checking the ratio of in-band power
 *	to total power for long radar pulses (half dB steps)
 * @fir_power: threshold for detecting the end of a long radar pulse (dB)
 *
 * @ext_channel: enable extension channel radar detection
 */
struct ath_hw_radar_conf {
	unsigned int pulse_inband;
	unsigned int pulse_inband_step;
	unsigned int pulse_height;
	unsigned int pulse_rssi;
	unsigned int pulse_maxlen;

	unsigned int radar_rssi;
	unsigned int radar_inband;
	int fir_power;

	bool ext_channel;
};

/**
 * struct ath_hw_private_ops - callbacks used internally by hardware code
 *
 * This structure contains private callbacks designed to only be used internally
 * by the hardware core.
 *
 * @init_cal_settings: setup types of calibrations supported
 * @init_cal: starts actual calibration
 *
 * @init_mode_gain_regs: Initialize TX/RX gain registers
 *
 * @rf_set_freq: change frequency
 * @spur_mitigate_freq: spur mitigation
 * @set_rf_regs:
 * @compute_pll_control: compute the PLL control value to use for
 *	AR_RTC_PLL_CONTROL for a given channel
 * @setup_calibration: set up calibration
 * @iscal_supported: used to query if a type of calibration is supported
 *
 * @ani_cache_ini_regs: cache the values for ANI from the initial
 *	register settings through the register initialization.
 */
struct ath_hw_private_ops {
	void (*init_hang_checks)(struct ath_hw *ah);
	bool (*detect_mac_hang)(struct ath_hw *ah);
	bool (*detect_bb_hang)(struct ath_hw *ah);

	/* Calibration ops */
	void (*init_cal_settings)(struct ath_hw *ah);
	bool (*init_cal)(struct ath_hw *ah, struct ath9k_channel *chan);

	void (*init_mode_gain_regs)(struct ath_hw *ah);
	void (*setup_calibration)(struct ath_hw *ah,
				  struct ath9k_cal_list *currCal);

	/* PHY ops */
	int (*rf_set_freq)(struct ath_hw *ah,
			   struct ath9k_channel *chan);
	void (*spur_mitigate_freq)(struct ath_hw *ah,
				   struct ath9k_channel *chan);
	bool (*set_rf_regs)(struct ath_hw *ah,
			    struct ath9k_channel *chan,
			    u16 modesIndex);
	void (*set_channel_regs)(struct ath_hw *ah, struct ath9k_channel *chan);
	void (*init_bb)(struct ath_hw *ah,
			struct ath9k_channel *chan);
	int (*process_ini)(struct ath_hw *ah, struct ath9k_channel *chan);
	void (*olc_init)(struct ath_hw *ah);
	void (*set_rfmode)(struct ath_hw *ah, struct ath9k_channel *chan);
	void (*mark_phy_inactive)(struct ath_hw *ah);
	void (*set_delta_slope)(struct ath_hw *ah, struct ath9k_channel *chan);
	bool (*rfbus_req)(struct ath_hw *ah);
	void (*rfbus_done)(struct ath_hw *ah);
	void (*restore_chainmask)(struct ath_hw *ah);
	u32 (*compute_pll_control)(struct ath_hw *ah,
				   struct ath9k_channel *chan);
	bool (*ani_control)(struct ath_hw *ah, enum ath9k_ani_cmd cmd,
			    int param);
	void (*do_getnf)(struct ath_hw *ah, int16_t nfarray[NUM_NF_READINGS]);
	void (*set_radar_params)(struct ath_hw *ah,
				 struct ath_hw_radar_conf *conf);
	int (*fast_chan_change)(struct ath_hw *ah, struct ath9k_channel *chan,
				u8 *ini_reloaded);

	/* ANI */
	void (*ani_cache_ini_regs)(struct ath_hw *ah);

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
	bool (*is_aic_enabled)(struct ath_hw *ah);
#endif /* CONFIG_ATH9K_BTCOEX_SUPPORT */
};

/**
 * struct ath_spec_scan - parameters for Atheros spectral scan
 *
 * @enabled: enable/disable spectral scan
 * @short_repeat: controls whether the chip is in spectral scan mode
 *		  for 4 usec (enabled) or 204 usec (disabled)
 * @count: number of scan results requested. There are special meanings
 *	   in some chip revisions:
 *	   AR92xx: highest bit set (>=128) for endless mode
 *		   (spectral scan won't stopped until explicitly disabled)
 *	   AR9300 and newer: 0 for endless mode
 * @endless: true if endless mode is intended. Otherwise, count value is
 *           corrected to the next possible value.
 * @period: time duration between successive spectral scan entry points
 *	    (period*256*Tclk). Tclk = ath_common->clockrate
 * @fft_period: PHY passes FFT frames to MAC every (fft_period+1)*4uS
 *
 * Note: Tclk = 40MHz or 44MHz depending upon operating mode.
 *	 Typically it's 44MHz in 2/5GHz on later chips, but there's
 *	 a "fast clock" check for this in 5GHz.
 *
 */
struct ath_spec_scan {
	bool enabled;
	bool short_repeat;
	bool endless;
	u8 count;
	u8 period;
	u8 fft_period;
};

/**
 * struct ath_hw_ops - callbacks used by hardware code and driver code
 *
 * This structure contains callbacks designed to be used internally by
 * hardware code and also by the lower level driver.
 *
 * @config_pci_powersave:
 * @calibrate: periodic calibration for NF, ANI, IQ, ADC gain, ADC-DC
 *
 * @spectral_scan_config: set parameters for spectral scan and enable/disable it
 * @spectral_scan_trigger: trigger a spectral scan run
 * @spectral_scan_wait: wait for a spectral scan run to finish
 */
struct ath_hw_ops {
	void (*config_pci_powersave)(struct ath_hw *ah,
				     bool power_off);
	void (*rx_enable)(struct ath_hw *ah);
	void (*set_desc_link)(void *ds, u32 link);
	int (*calibrate)(struct ath_hw *ah, struct ath9k_channel *chan,
			 u8 rxchainmask, bool longcal);
	bool (*get_isr)(struct ath_hw *ah, enum ath9k_int *masked,
			u32 *sync_cause_p);
	void (*set_txdesc)(struct ath_hw *ah, void *ds,
			   struct ath_tx_info *i);
	int (*proc_txdesc)(struct ath_hw *ah, void *ds,
			   struct ath_tx_status *ts);
	int (*get_duration)(struct ath_hw *ah, const void *ds, int index);
	void (*antdiv_comb_conf_get)(struct ath_hw *ah,
			struct ath_hw_antcomb_conf *antconf);
	void (*antdiv_comb_conf_set)(struct ath_hw *ah,
			struct ath_hw_antcomb_conf *antconf);
	void (*spectral_scan_config)(struct ath_hw *ah,
				     struct ath_spec_scan *param);
	void (*spectral_scan_trigger)(struct ath_hw *ah);
	void (*spectral_scan_wait)(struct ath_hw *ah);

	void (*tx99_start)(struct ath_hw *ah, u32 qnum);
	void (*tx99_stop)(struct ath_hw *ah);
	void (*tx99_set_txpower)(struct ath_hw *ah, u8 power);

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
	void (*set_bt_ant_diversity)(struct ath_hw *hw, bool enable);
#endif
};

struct ath_nf_limits {
	s16 max;
	s16 min;
	s16 nominal;
	s16 cal[AR5416_MAX_CHAINS];
	s16 pwr[AR5416_MAX_CHAINS];
};

enum ath_cal_list {
	TX_IQ_CAL         =	BIT(0),
	TX_IQ_ON_AGC_CAL  =	BIT(1),
	TX_CL_CAL         =	BIT(2),
};

/* ah_flags */
#define AH_USE_EEPROM   0x1
#define AH_UNPLUGGED    0x2 /* The card has been physically removed. */
#define AH_FASTCC       0x4
#define AH_NO_EEP_SWAP  0x8 /* Do not swap EEPROM data */

struct ath_hw {
	struct ath_ops reg_ops;

	struct device *dev;
	struct ieee80211_hw *hw;
	struct ath_common common;
	struct ath9k_hw_version hw_version;
	struct ath9k_ops_config config;
	struct ath9k_hw_capabilities caps;
	struct ath9k_channel channels[ATH9K_NUM_CHANNELS];
	struct ath9k_channel *curchan;

	union {
		struct ar5416_eeprom_def def;
		struct ar5416_eeprom_4k map4k;
		struct ar9287_eeprom map9287;
		struct ar9300_eeprom ar9300_eep;
	} eeprom;
	const struct eeprom_ops *eep_ops;

	bool sw_mgmt_crypto_tx;
	bool sw_mgmt_crypto_rx;
	bool is_pciexpress;
	bool aspm_enabled;
	bool is_monitoring;
	bool need_an_top2_fixup;
	u16 tx_trig_level;

	u32 nf_regs[6];
	struct ath_nf_limits nf_2g;
	struct ath_nf_limits nf_5g;
	u16 rfsilent;
	u32 rfkill_gpio;
	u32 rfkill_polarity;
	u32 ah_flags;
	s16 nf_override;

	bool reset_power_on;
	bool htc_reset_init;

	enum nl80211_iftype opmode;
	enum ath9k_power_mode power_mode;

	s8 noise;
	struct ath9k_hw_cal_data *caldata;
	struct ath9k_pacal_info pacal_info;
	struct ar5416Stats stats;
	struct ath9k_tx_queue_info txq[ATH9K_NUM_TX_QUEUES];
	DECLARE_BITMAP(pending_del_keymap, ATH_KEYMAX);

	enum ath9k_int imask;
	u32 imrs2_reg;
	u32 txok_interrupt_mask;
	u32 txerr_interrupt_mask;
	u32 txdesc_interrupt_mask;
	u32 txeol_interrupt_mask;
	u32 txurn_interrupt_mask;
	atomic_t intr_ref_cnt;
	bool chip_fullsleep;
	u32 modes_index;

	/* Calibration */
	u32 supp_cals;
	unsigned long cal_start_time;
	struct ath9k_cal_list iq_caldata;
	struct ath9k_cal_list adcgain_caldata;
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
	u8 enabled_cals;

	u32 sta_id1_defaults;
	u32 misc_mode;

	/* Private to hardware code */
	struct ath_hw_private_ops private_ops;
	/* Accessed by the lower level driver */
	struct ath_hw_ops ops;

	/* Used to program the radio on non single-chip devices */
	u32 *analogBank6Data;

	int coverage_class;
	u32 slottime;
	u32 globaltxtimeout;

	/* ANI */
	u32 aniperiod;
	enum ath9k_ani_cmd ani_function;
	u32 ani_skip_count;
	struct ar5416AniState ani;

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
	struct ath_btcoex_hw btcoex_hw;
#endif

	u32 intr_txqs;
	u8 txchainmask;
	u8 rxchainmask;

	struct ath_hw_radar_conf radar_conf;

	u32 originalGain[22];
	int initPDADC;
	int PDADCdelta;
	int led_pin;
	u32 gpio_mask;
	u32 gpio_val;

	struct ar5416IniArray ini_dfs;
	struct ar5416IniArray iniModes;
	struct ar5416IniArray iniCommon;
	struct ar5416IniArray iniBB_RfGain;
	struct ar5416IniArray iniBank6;
	struct ar5416IniArray iniAddac;
	struct ar5416IniArray iniPcieSerdes;
	struct ar5416IniArray iniPcieSerdesLowPower;
	struct ar5416IniArray iniModesFastClock;
	struct ar5416IniArray iniAdditional;
	struct ar5416IniArray iniModesRxGain;
	struct ar5416IniArray ini_modes_rx_gain_bounds;
	struct ar5416IniArray iniModesTxGain;
	struct ar5416IniArray iniCckfirNormal;
	struct ar5416IniArray iniCckfirJapan2484;
	struct ar5416IniArray iniModes_9271_ANI_reg;
	struct ar5416IniArray ini_radio_post_sys2ant;
	struct ar5416IniArray ini_modes_rxgain_xlna;
	struct ar5416IniArray ini_modes_rxgain_bb_core;
	struct ar5416IniArray ini_modes_rxgain_bb_postamble;

	struct ar5416IniArray iniMac[ATH_INI_NUM_SPLIT];
	struct ar5416IniArray iniBB[ATH_INI_NUM_SPLIT];
	struct ar5416IniArray iniRadio[ATH_INI_NUM_SPLIT];
	struct ar5416IniArray iniSOC[ATH_INI_NUM_SPLIT];

	u32 intr_gen_timer_trigger;
	u32 intr_gen_timer_thresh;
	struct ath_gen_timer_table hw_gen_timers;

	struct ar9003_txs *ts_ring;
	u32 ts_paddr_start;
	u32 ts_paddr_end;
	u16 ts_tail;
	u16 ts_size;

	u32 bb_watchdog_last_status;
	u32 bb_watchdog_timeout_ms; /* in ms, 0 to disable */
	u8 bb_hang_rx_ofdm; /* true if bb hang due to rx_ofdm */

	unsigned int paprd_target_power;
	unsigned int paprd_training_power;
	unsigned int paprd_ratemask;
	unsigned int paprd_ratemask_ht40;
	bool paprd_table_write_done;
	u32 paprd_gain_table_entries[PAPRD_GAIN_TABLE_ENTRIES];
	u8 paprd_gain_table_index[PAPRD_GAIN_TABLE_ENTRIES];
	/*
	 * Store the permanent value of Reg 0x4004in WARegVal
	 * so we dont have to R/M/W. We should not be reading
	 * this register when in sleep states.
	 */
	u32 WARegVal;

	/* Enterprise mode cap */
	u32 ent_mode;

#ifdef CONFIG_ATH9K_WOW
	struct ath9k_hw_wow wow;
#endif
	bool is_clk_25mhz;
	int (*get_mac_revision)(void);
	int (*external_reset)(void);
	bool disable_2ghz;
	bool disable_5ghz;

	const struct firmware *eeprom_blob;
	u16 *nvmem_blob;	/* devres managed */
	size_t nvmem_blob_len;

	struct ath_dynack dynack;

	bool tpc_enabled;
	u8 tx_power[Ar5416RateSize];
	u8 tx_power_stbc[Ar5416RateSize];
	bool msi_enabled;
	u32 msi_mask;
	u32 msi_reg;
};

struct ath_bus_ops {
	enum ath_bus_type ath_bus_type;
	void (*read_cachesize)(struct ath_common *common, int *csz);
	bool (*eeprom_read)(struct ath_common *common, u32 off, u16 *data);
	void (*bt_coex_prep)(struct ath_common *common);
	void (*aspm_init)(struct ath_common *common);
};

static inline struct ath_common *ath9k_hw_common(struct ath_hw *ah)
{
	return &ah->common;
}

static inline struct ath_regulatory *ath9k_hw_regulatory(struct ath_hw *ah)
{
	return &(ath9k_hw_common(ah)->regulatory);
}

static inline struct ath_hw_private_ops *ath9k_hw_private_ops(struct ath_hw *ah)
{
	return &ah->private_ops;
}

static inline struct ath_hw_ops *ath9k_hw_ops(struct ath_hw *ah)
{
	return &ah->ops;
}

static inline u8 get_streams(int mask)
{
	return !!(mask & BIT(0)) + !!(mask & BIT(1)) + !!(mask & BIT(2));
}

/* Initialization, Detach, Reset */
void ath9k_hw_deinit(struct ath_hw *ah);
int ath9k_hw_init(struct ath_hw *ah);
int ath9k_hw_reset(struct ath_hw *ah, struct ath9k_channel *chan,
		   struct ath9k_hw_cal_data *caldata, bool fastcc);
int ath9k_hw_fill_cap_info(struct ath_hw *ah);
u32 ath9k_regd_get_ctl(struct ath_regulatory *reg, struct ath9k_channel *chan);

/* GPIO / RFKILL / Antennae */
void ath9k_hw_gpio_request_in(struct ath_hw *ah, u32 gpio, const char *label);
void ath9k_hw_gpio_request_out(struct ath_hw *ah, u32 gpio, const char *label,
			       u32 ah_signal_type);
void ath9k_hw_gpio_free(struct ath_hw *ah, u32 gpio);
u32 ath9k_hw_gpio_get(struct ath_hw *ah, u32 gpio);
void ath9k_hw_set_gpio(struct ath_hw *ah, u32 gpio, u32 val);
void ath9k_hw_setantenna(struct ath_hw *ah, u32 antenna);

/* General Operation */
void ath9k_hw_synth_delay(struct ath_hw *ah, struct ath9k_channel *chan,
			  int hw_delay);
bool ath9k_hw_wait(struct ath_hw *ah, u32 reg, u32 mask, u32 val, u32 timeout);
void ath9k_hw_write_array(struct ath_hw *ah, const struct ar5416IniArray *array,
			  int column, unsigned int *writecnt);
void ath9k_hw_read_array(struct ath_hw *ah, u32 array[][2], int size);
u32 ath9k_hw_reverse_bits(u32 val, u32 n);
u16 ath9k_hw_computetxtime(struct ath_hw *ah,
			   u8 phy, int kbps,
			   u32 frameLen, u16 rateix, bool shortPreamble);
void ath9k_hw_get_channel_centers(struct ath_hw *ah,
				  struct ath9k_channel *chan,
				  struct chan_centers *centers);
u32 ath9k_hw_getrxfilter(struct ath_hw *ah);
void ath9k_hw_setrxfilter(struct ath_hw *ah, u32 bits);
bool ath9k_hw_phy_disable(struct ath_hw *ah);
bool ath9k_hw_disable(struct ath_hw *ah);
void ath9k_hw_set_txpowerlimit(struct ath_hw *ah, u32 limit, bool test);
void ath9k_hw_setopmode(struct ath_hw *ah);
void ath9k_hw_setmcastfilter(struct ath_hw *ah, u32 filter0, u32 filter1);
void ath9k_hw_write_associd(struct ath_hw *ah);
u32 ath9k_hw_gettsf32(struct ath_hw *ah);
u64 ath9k_hw_gettsf64(struct ath_hw *ah);
void ath9k_hw_settsf64(struct ath_hw *ah, u64 tsf64);
void ath9k_hw_reset_tsf(struct ath_hw *ah);
u32 ath9k_hw_get_tsf_offset(ktime_t last, ktime_t cur);
void ath9k_hw_set_tsfadjust(struct ath_hw *ah, bool set);
void ath9k_hw_init_global_settings(struct ath_hw *ah);
u32 ar9003_get_pll_sqsum_dvc(struct ath_hw *ah);
void ath9k_hw_set11nmac2040(struct ath_hw *ah, struct ath9k_channel *chan);
void ath9k_hw_beaconinit(struct ath_hw *ah, u32 next_beacon, u32 beacon_period);
void ath9k_hw_set_sta_beacon_timers(struct ath_hw *ah,
				    const struct ath9k_beacon_state *bs);
void ath9k_hw_check_nav(struct ath_hw *ah);
bool ath9k_hw_check_alive(struct ath_hw *ah);

bool ath9k_hw_setpower(struct ath_hw *ah, enum ath9k_power_mode mode);

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
void ath9k_hw_gen_timer_start_tsf2(struct ath_hw *ah);
void ath9k_hw_gen_timer_stop(struct ath_hw *ah, struct ath_gen_timer *timer);

void ath_gen_timer_free(struct ath_hw *ah, struct ath_gen_timer *timer);
void ath_gen_timer_isr(struct ath_hw *hw);

void ath9k_hw_name(struct ath_hw *ah, char *hw_name, size_t len);

/* PHY */
void ath9k_hw_get_delta_slope_vals(struct ath_hw *ah, u32 coef_scaled,
				   u32 *coef_mantissa, u32 *coef_exponent);
void ath9k_hw_apply_txpower(struct ath_hw *ah, struct ath9k_channel *chan,
			    bool test);

/*
 * Code Specific to AR5008, AR9001 or AR9002,
 * we stuff these here to avoid callbacks for AR9003.
 */
int ar9002_hw_rf_claim(struct ath_hw *ah);
void ar9002_hw_enable_async_fifo(struct ath_hw *ah);

/*
 * Code specific to AR9003, we stuff these here to avoid callbacks
 * for older families
 */
bool ar9003_hw_bb_watchdog_check(struct ath_hw *ah);
void ar9003_hw_bb_watchdog_config(struct ath_hw *ah);
void ar9003_hw_bb_watchdog_read(struct ath_hw *ah);
void ar9003_hw_bb_watchdog_dbg_info(struct ath_hw *ah);
void ar9003_hw_disable_phy_restart(struct ath_hw *ah);
void ar9003_paprd_enable(struct ath_hw *ah, bool val);
void ar9003_paprd_populate_single_table(struct ath_hw *ah,
					struct ath9k_hw_cal_data *caldata,
					int chain);
int ar9003_paprd_create_curve(struct ath_hw *ah,
			      struct ath9k_hw_cal_data *caldata, int chain);
void ar9003_paprd_setup_gain_table(struct ath_hw *ah, int chain);
int ar9003_paprd_init_table(struct ath_hw *ah);
bool ar9003_paprd_is_done(struct ath_hw *ah);
bool ar9003_is_paprd_enabled(struct ath_hw *ah);
void ar9003_hw_set_chain_masks(struct ath_hw *ah, u8 rx, u8 tx);
void ar9003_hw_init_rate_txpower(struct ath_hw *ah, u8 *rate_array,
				 struct ath9k_channel *chan);
void ar5008_hw_cmn_spur_mitigate(struct ath_hw *ah,
				 struct ath9k_channel *chan, int bin);
void ar5008_hw_init_rate_txpower(struct ath_hw *ah, int16_t *rate_array,
				 struct ath9k_channel *chan, int ht40_delta);

/* Hardware family op attach helpers */
int ar5008_hw_attach_phy_ops(struct ath_hw *ah);
void ar9002_hw_attach_phy_ops(struct ath_hw *ah);
void ar9003_hw_attach_phy_ops(struct ath_hw *ah);

void ar9002_hw_attach_calib_ops(struct ath_hw *ah);
void ar9003_hw_attach_calib_ops(struct ath_hw *ah);

int ar9002_hw_attach_ops(struct ath_hw *ah);
void ar9003_hw_attach_ops(struct ath_hw *ah);

void ar9002_hw_load_ani_reg(struct ath_hw *ah, struct ath9k_channel *chan);

void ath9k_ani_reset(struct ath_hw *ah, bool is_scanning);
void ath9k_hw_ani_monitor(struct ath_hw *ah, struct ath9k_channel *chan);

void ath9k_hw_set_ack_timeout(struct ath_hw *ah, u32 us);
void ath9k_hw_set_cts_timeout(struct ath_hw *ah, u32 us);
void ath9k_hw_setslottime(struct ath_hw *ah, u32 us);

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
void ar9003_hw_attach_aic_ops(struct ath_hw *ah);
static inline bool ath9k_hw_btcoex_is_enabled(struct ath_hw *ah)
{
	return ah->btcoex_hw.enabled;
}
static inline bool ath9k_hw_mci_is_enabled(struct ath_hw *ah)
{
	return ah->common.btcoex_enabled &&
	       (ah->caps.hw_caps & ATH9K_HW_CAP_MCI);

}
void ath9k_hw_btcoex_enable(struct ath_hw *ah);
static inline enum ath_btcoex_scheme
ath9k_hw_get_btcoex_scheme(struct ath_hw *ah)
{
	return ah->btcoex_hw.scheme;
}
#else
static inline void ar9003_hw_attach_aic_ops(struct ath_hw *ah)
{
}
static inline bool ath9k_hw_btcoex_is_enabled(struct ath_hw *ah)
{
	return false;
}
static inline bool ath9k_hw_mci_is_enabled(struct ath_hw *ah)
{
	return false;
}
static inline void ath9k_hw_btcoex_enable(struct ath_hw *ah)
{
}
static inline enum ath_btcoex_scheme
ath9k_hw_get_btcoex_scheme(struct ath_hw *ah)
{
	return ATH_BTCOEX_CFG_NONE;
}
#endif /* CONFIG_ATH9K_BTCOEX_SUPPORT */


#ifdef CONFIG_ATH9K_WOW
int ath9k_hw_wow_apply_pattern(struct ath_hw *ah, u8 *user_pattern,
			       u8 *user_mask, int pattern_count,
			       int pattern_len);
u32 ath9k_hw_wow_wakeup(struct ath_hw *ah);
void ath9k_hw_wow_enable(struct ath_hw *ah, u32 pattern_enable);
#else
static inline int ath9k_hw_wow_apply_pattern(struct ath_hw *ah,
					     u8 *user_pattern,
					     u8 *user_mask,
					     int pattern_count,
					     int pattern_len)
{
	return 0;
}
static inline u32 ath9k_hw_wow_wakeup(struct ath_hw *ah)
{
	return 0;
}
static inline void ath9k_hw_wow_enable(struct ath_hw *ah, u32 pattern_enable)
{
}
#endif

#define ATH9K_CLOCK_RATE_CCK		22
#define ATH9K_CLOCK_RATE_5GHZ_OFDM	40
#define ATH9K_CLOCK_RATE_2GHZ_OFDM	44
#define ATH9K_CLOCK_FAST_RATE_5GHZ_OFDM 44

#endif
