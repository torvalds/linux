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

#ifndef ANI_H
#define ANI_H

#define HAL_PROCESS_ANI           0x00000001
#define ATH9K_RSSI_EP_MULTIPLIER  (1<<7)

#define DO_ANI(ah) (((ah)->proc_phyerr & HAL_PROCESS_ANI))

#define HAL_EP_RND(x, mul)						\
	((((x)%(mul)) >= ((mul)/2)) ? ((x) + ((mul) - 1)) / (mul) : (x)/(mul))
#define BEACON_RSSI(ahp)					\
	HAL_EP_RND(ahp->stats.ast_nodestats.ns_avgbrssi,	\
		   ATH9K_RSSI_EP_MULTIPLIER)

#define ATH9K_ANI_OFDM_TRIG_HIGH          500
#define ATH9K_ANI_OFDM_TRIG_LOW           200
#define ATH9K_ANI_CCK_TRIG_HIGH           200
#define ATH9K_ANI_CCK_TRIG_LOW            100
#define ATH9K_ANI_NOISE_IMMUNE_LVL        4
#define ATH9K_ANI_USE_OFDM_WEAK_SIG       true
#define ATH9K_ANI_CCK_WEAK_SIG_THR        false
#define ATH9K_ANI_SPUR_IMMUNE_LVL         7
#define ATH9K_ANI_FIRSTEP_LVL             0
#define ATH9K_ANI_RSSI_THR_HIGH           40
#define ATH9K_ANI_RSSI_THR_LOW            7
#define ATH9K_ANI_PERIOD                  100

#define HAL_NOISE_IMMUNE_MAX              4
#define HAL_SPUR_IMMUNE_MAX               7
#define HAL_FIRST_STEP_MAX                2

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

struct ath9k_mib_stats {
	u32 ackrcv_bad;
	u32 rts_bad;
	u32 rts_good;
	u32 fcs_bad;
	u32 beacons;
};

struct ath9k_node_stats {
	u32 ns_avgbrssi;
	u32 ns_avgrssi;
	u32 ns_avgtxrssi;
	u32 ns_avgtxrate;
};

struct ar5416AniState {
	struct ath9k_channel *c;
	u8 noiseImmunityLevel;
	u8 spurImmunityLevel;
	u8 firstepLevel;
	u8 ofdmWeakSigDetectOff;
	u8 cckWeakSigThreshold;
	u32 listenTime;
	u32 ofdmTrigHigh;
	u32 ofdmTrigLow;
	int32_t cckTrigHigh;
	int32_t cckTrigLow;
	int32_t rssiThrLow;
	int32_t rssiThrHigh;
	u32 noiseFloor;
	u32 txFrameCount;
	u32 rxFrameCount;
	u32 cycleCount;
	u32 ofdmPhyErrCount;
	u32 cckPhyErrCount;
	u32 ofdmPhyErrBase;
	u32 cckPhyErrBase;
	int16_t pktRssi[2];
	int16_t ofdmErrRssi[2];
	int16_t cckErrRssi[2];
};

struct ar5416Stats {
	u32 ast_ani_niup;
	u32 ast_ani_nidown;
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
	u32 ast_ani_lzero;
	u32 ast_ani_lneg;
	struct ath9k_mib_stats ast_mibstats;
	struct ath9k_node_stats ast_nodestats;
};
#define ah_mibStats stats.ast_mibstats

void ath9k_ani_reset(struct ath_hw *ah);
void ath9k_hw_ani_monitor(struct ath_hw *ah,
			  const struct ath9k_node_stats *stats,
			  struct ath9k_channel *chan);
bool ath9k_hw_phycounters(struct ath_hw *ah);
void ath9k_enable_mib_counters(struct ath_hw *ah);
void ath9k_hw_disable_mib_counters(struct ath_hw *ah);
u32 ath9k_hw_GetMibCycleCountsPct(struct ath_hw *ah, u32 *rxc_pcnt,
				  u32 *rxf_pcnt, u32 *txf_pcnt);
void ath9k_hw_procmibevent(struct ath_hw *ah,
			   const struct ath9k_node_stats *stats);
void ath9k_hw_ani_setup(struct ath_hw *ah);
void ath9k_hw_ani_attach(struct ath_hw *ah);
void ath9k_hw_ani_detach(struct ath_hw *ah);

#endif /* ANI_H */
