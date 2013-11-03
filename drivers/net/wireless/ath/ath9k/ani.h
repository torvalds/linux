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

#ifndef ANI_H
#define ANI_H

#define BEACON_RSSI(ahp) (ahp->stats.avgbrssi)

/* units are errors per second */
#define ATH9K_ANI_OFDM_TRIG_HIGH           3500
#define ATH9K_ANI_OFDM_TRIG_HIGH_BELOW_INI 1000

#define ATH9K_ANI_OFDM_TRIG_LOW           400
#define ATH9K_ANI_OFDM_TRIG_LOW_ABOVE_INI 900

#define ATH9K_ANI_CCK_TRIG_HIGH           600
#define ATH9K_ANI_CCK_TRIG_LOW            300

#define ATH9K_ANI_SPUR_IMMUNE_LVL         3
#define ATH9K_ANI_FIRSTEP_LVL             2

#define ATH9K_ANI_RSSI_THR_HIGH           40
#define ATH9K_ANI_RSSI_THR_LOW            7

#define ATH9K_ANI_PERIOD                  300

/* in ms */
#define ATH9K_ANI_POLLINTERVAL            1000

#define ATH9K_SIG_FIRSTEP_SETTING_MIN     0
#define ATH9K_SIG_FIRSTEP_SETTING_MAX     20
#define ATH9K_SIG_SPUR_IMM_SETTING_MIN    0
#define ATH9K_SIG_SPUR_IMM_SETTING_MAX    22

/* values here are relative to the INI */

enum ath9k_ani_cmd {
	ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION = 0x1,
	ATH9K_ANI_FIRSTEP_LEVEL = 0x2,
	ATH9K_ANI_SPUR_IMMUNITY_LEVEL = 0x4,
	ATH9K_ANI_MRC_CCK = 0x8,
	ATH9K_ANI_ALL = 0xfff
};

struct ath9k_mib_stats {
	u32 ackrcv_bad;
	u32 rts_bad;
	u32 rts_good;
	u32 fcs_bad;
	u32 beacons;
};

/* INI default values for ANI registers */
struct ath9k_ani_default {
	u16 m1ThreshLow;
	u16 m2ThreshLow;
	u16 m1Thresh;
	u16 m2Thresh;
	u16 m2CountThr;
	u16 m2CountThrLow;
	u16 m1ThreshLowExt;
	u16 m2ThreshLowExt;
	u16 m1ThreshExt;
	u16 m2ThreshExt;
	u16 firstep;
	u16 firstepLow;
	u16 cycpwrThr1;
	u16 cycpwrThr1Ext;
};

struct ar5416AniState {
	u8 noiseImmunityLevel;
	u8 ofdmNoiseImmunityLevel;
	u8 cckNoiseImmunityLevel;
	bool ofdmsTurn;
	u8 mrcCCK;
	u8 spurImmunityLevel;
	u8 firstepLevel;
	bool ofdmWeakSigDetect;
	u32 listenTime;
	u32 ofdmPhyErrCount;
	u32 cckPhyErrCount;
	struct ath9k_ani_default iniDef;
};

struct ar5416Stats {
	u32 ast_ani_spurup;
	u32 ast_ani_spurdown;
	u32 ast_ani_ofdmon;
	u32 ast_ani_ofdmoff;
	u32 ast_ani_cckhigh;
	u32 ast_ani_ccklow;
	u32 ast_ani_stepup;
	u32 ast_ani_stepdown;
	u32 ast_ani_ofdmerrs;
	u32 ast_ani_cckerrs;
	u32 ast_ani_reset;
	u32 ast_ani_lneg_or_lzero;
	u32 avgbrssi;
	struct ath9k_mib_stats ast_mibstats;
};
#define ah_mibStats stats.ast_mibstats

void ath9k_enable_mib_counters(struct ath_hw *ah);
void ath9k_hw_disable_mib_counters(struct ath_hw *ah);
void ath9k_hw_ani_init(struct ath_hw *ah);

#endif /* ANI_H */
