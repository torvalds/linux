/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * phy_hal.h:  functionality exported from the phy to higher layers
 */

#ifndef _BRCM_PHY_HAL_H_
#define _BRCM_PHY_HAL_H_

#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include <phy_shim.h>

#define	IDCODE_VER_MASK		0x0000000f
#define	IDCODE_VER_SHIFT	0
#define	IDCODE_MFG_MASK		0x00000fff
#define	IDCODE_MFG_SHIFT	0
#define	IDCODE_ID_MASK		0x0ffff000
#define	IDCODE_ID_SHIFT		12
#define	IDCODE_REV_MASK		0xf0000000
#define	IDCODE_REV_SHIFT	28

#define	NORADIO_ID		0xe4f5
#define	NORADIO_IDCODE		0x4e4f5246

#define BCM2055_ID		0x2055
#define BCM2055_IDCODE		0x02055000
#define BCM2055A0_IDCODE	0x1205517f

#define BCM2056_ID		0x2056
#define BCM2056_IDCODE		0x02056000
#define BCM2056A0_IDCODE	0x1205617f

#define BCM2057_ID		0x2057
#define BCM2057_IDCODE		0x02057000
#define BCM2057A0_IDCODE	0x1205717f

#define BCM2064_ID		0x2064
#define BCM2064_IDCODE		0x02064000
#define BCM2064A0_IDCODE	0x0206417f

#define PHY_TPC_HW_OFF		false
#define PHY_TPC_HW_ON		true

#define PHY_PERICAL_DRIVERUP	1
#define PHY_PERICAL_WATCHDOG	2
#define PHY_PERICAL_PHYINIT	3
#define PHY_PERICAL_JOIN_BSS	4
#define PHY_PERICAL_START_IBSS	5
#define PHY_PERICAL_UP_BSS	6
#define PHY_PERICAL_CHAN	7
#define PHY_FULLCAL	8

#define PHY_PERICAL_DISABLE	0
#define PHY_PERICAL_SPHASE	1
#define PHY_PERICAL_MPHASE	2
#define PHY_PERICAL_MANUAL	3

#define PHY_HOLD_FOR_ASSOC	1
#define PHY_HOLD_FOR_SCAN	2
#define PHY_HOLD_FOR_RM		4
#define PHY_HOLD_FOR_PLT	8
#define PHY_HOLD_FOR_MUTE	16
#define PHY_HOLD_FOR_NOT_ASSOC 0x20

#define PHY_MUTE_FOR_PREISM	1
#define PHY_MUTE_ALL		0xffffffff

#define PHY_NOISE_FIXED_VAL		(-95)
#define PHY_NOISE_FIXED_VAL_NPHY	(-92)
#define PHY_NOISE_FIXED_VAL_LCNPHY	(-92)

#define PHY_MODE_CAL		0x0002
#define PHY_MODE_NOISEM		0x0004

#define BRCMS_TXPWR_DB_FACTOR	4

/* a large TX Power as an init value to factor out of min() calculations,
 * keep low enough to fit in an s8, units are .25 dBm
 */
#define BRCMS_TXPWR_MAX		(127)	/* ~32 dBm = 1,500 mW */

#define BRCMS_NUM_RATES_CCK           4
#define BRCMS_NUM_RATES_OFDM          8
#define BRCMS_NUM_RATES_MCS_1_STREAM  8
#define BRCMS_NUM_RATES_MCS_2_STREAM  8
#define BRCMS_NUM_RATES_MCS_3_STREAM  8
#define BRCMS_NUM_RATES_MCS_4_STREAM  8

#define	BRCMS_RSSI_INVALID	 0	/* invalid RSSI value */

struct d11regs;
struct phy_shim_info;

struct txpwr_limits {
	u8 cck[BRCMS_NUM_RATES_CCK];
	u8 ofdm[BRCMS_NUM_RATES_OFDM];

	u8 ofdm_cdd[BRCMS_NUM_RATES_OFDM];

	u8 ofdm_40_siso[BRCMS_NUM_RATES_OFDM];
	u8 ofdm_40_cdd[BRCMS_NUM_RATES_OFDM];

	u8 mcs_20_siso[BRCMS_NUM_RATES_MCS_1_STREAM];
	u8 mcs_20_cdd[BRCMS_NUM_RATES_MCS_1_STREAM];
	u8 mcs_20_stbc[BRCMS_NUM_RATES_MCS_1_STREAM];
	u8 mcs_20_mimo[BRCMS_NUM_RATES_MCS_2_STREAM];

	u8 mcs_40_siso[BRCMS_NUM_RATES_MCS_1_STREAM];
	u8 mcs_40_cdd[BRCMS_NUM_RATES_MCS_1_STREAM];
	u8 mcs_40_stbc[BRCMS_NUM_RATES_MCS_1_STREAM];
	u8 mcs_40_mimo[BRCMS_NUM_RATES_MCS_2_STREAM];
	u8 mcs32;
};

struct tx_power {
	u32 flags;
	u16 chanspec;   /* txpwr report for this channel */
	u16 local_chanspec;     /* channel on which we are associated */
	u8 local_max;   /* local max according to the AP */
	u8 local_constraint;    /* local constraint according to the AP */
	s8 antgain[2];  /* Ant gain for each band - from SROM */
	u8 rf_cores;            /* count of RF Cores being reported */
	u8 est_Pout[4]; /* Latest tx power out estimate per RF chain */
	u8 est_Pout_act[4];     /* Latest tx power out estimate per RF chain
				 * without adjustment */
	u8 est_Pout_cck;        /* Latest CCK tx power out estimate */
	u8 tx_power_max[4];     /* Maximum target power among all rates */
	/* Index of the rate with the max target power */
	u8 tx_power_max_rate_ind[4];
	/* User limit */
	u8 user_limit[WL_TX_POWER_RATES];
	/* Regulatory power limit */
	u8 reg_limit[WL_TX_POWER_RATES];
	/* Max power board can support (SROM) */
	u8 board_limit[WL_TX_POWER_RATES];
	/* Latest target power */
	u8 target[WL_TX_POWER_RATES];
};

struct tx_inst_power {
	u8 txpwr_est_Pout[2];   /* Latest estimate for 2.4 and 5 Ghz */
	u8 txpwr_est_Pout_gofdm;        /* Pwr estimate for 2.4 OFDM */
};

struct brcms_chanvec {
	u8 vec[MAXCHANNEL / NBBY];
};

struct shared_phy_params {
	struct si_pub *sih;
	struct phy_shim_info *physhim;
	uint unit;
	uint corerev;
	u16 vid;
	u16 did;
	uint chip;
	uint chiprev;
	uint chippkg;
	uint sromrev;
	uint boardtype;
	uint boardrev;
	u32 boardflags;
	u32 boardflags2;
};


extern struct shared_phy *wlc_phy_shared_attach(struct shared_phy_params *shp);
extern struct brcms_phy_pub *wlc_phy_attach(struct shared_phy *sh,
					    struct bcma_device *d11core,
					    int bandtype, struct wiphy *wiphy);
extern void wlc_phy_detach(struct brcms_phy_pub *ppi);

extern bool wlc_phy_get_phyversion(struct brcms_phy_pub *pih, u16 *phytype,
				   u16 *phyrev, u16 *radioid,
				   u16 *radiover);
extern bool wlc_phy_get_encore(struct brcms_phy_pub *pih);
extern u32 wlc_phy_get_coreflags(struct brcms_phy_pub *pih);

extern void wlc_phy_hw_clk_state_upd(struct brcms_phy_pub *ppi, bool newstate);
extern void wlc_phy_hw_state_upd(struct brcms_phy_pub *ppi, bool newstate);
extern void wlc_phy_init(struct brcms_phy_pub *ppi, u16 chanspec);
extern void wlc_phy_watchdog(struct brcms_phy_pub *ppi);
extern int wlc_phy_down(struct brcms_phy_pub *ppi);
extern u32 wlc_phy_clk_bwbits(struct brcms_phy_pub *pih);
extern void wlc_phy_cal_init(struct brcms_phy_pub *ppi);
extern void wlc_phy_antsel_init(struct brcms_phy_pub *ppi, bool lut_init);

extern void wlc_phy_chanspec_set(struct brcms_phy_pub *ppi,
				 u16 chanspec);
extern u16 wlc_phy_chanspec_get(struct brcms_phy_pub *ppi);
extern void wlc_phy_chanspec_radio_set(struct brcms_phy_pub *ppi,
				       u16 newch);
extern u16 wlc_phy_bw_state_get(struct brcms_phy_pub *ppi);
extern void wlc_phy_bw_state_set(struct brcms_phy_pub *ppi, u16 bw);

extern int wlc_phy_rssi_compute(struct brcms_phy_pub *pih,
				struct d11rxhdr *rxh);
extern void wlc_phy_por_inform(struct brcms_phy_pub *ppi);
extern void wlc_phy_noise_sample_intr(struct brcms_phy_pub *ppi);
extern bool wlc_phy_bist_check_phy(struct brcms_phy_pub *ppi);

extern void wlc_phy_set_deaf(struct brcms_phy_pub *ppi, bool user_flag);

extern void wlc_phy_switch_radio(struct brcms_phy_pub *ppi, bool on);
extern void wlc_phy_anacore(struct brcms_phy_pub *ppi, bool on);


extern void wlc_phy_BSSinit(struct brcms_phy_pub *ppi, bool bonlyap, int rssi);

extern void wlc_phy_chanspec_ch14_widefilter_set(struct brcms_phy_pub *ppi,
						 bool wide_filter);
extern void wlc_phy_chanspec_band_validch(struct brcms_phy_pub *ppi, uint band,
					  struct brcms_chanvec *channels);
extern u16 wlc_phy_chanspec_band_firstch(struct brcms_phy_pub *ppi,
					 uint band);

extern void wlc_phy_txpower_sromlimit(struct brcms_phy_pub *ppi, uint chan,
				      u8 *_min_, u8 *_max_, int rate);
extern void wlc_phy_txpower_sromlimit_max_get(struct brcms_phy_pub *ppi,
					      uint chan, u8 *_max_, u8 *_min_);
extern void wlc_phy_txpower_boardlimit_band(struct brcms_phy_pub *ppi,
					    uint band, s32 *, s32 *, u32 *);
extern void wlc_phy_txpower_limit_set(struct brcms_phy_pub *ppi,
				      struct txpwr_limits *,
				      u16 chanspec);
extern int wlc_phy_txpower_get(struct brcms_phy_pub *ppi, uint *qdbm,
			       bool *override);
extern int wlc_phy_txpower_set(struct brcms_phy_pub *ppi, uint qdbm,
			       bool override);
extern void wlc_phy_txpower_target_set(struct brcms_phy_pub *ppi,
				       struct txpwr_limits *);
extern bool wlc_phy_txpower_hw_ctrl_get(struct brcms_phy_pub *ppi);
extern void wlc_phy_txpower_hw_ctrl_set(struct brcms_phy_pub *ppi,
					bool hwpwrctrl);
extern u8 wlc_phy_txpower_get_target_min(struct brcms_phy_pub *ppi);
extern u8 wlc_phy_txpower_get_target_max(struct brcms_phy_pub *ppi);
extern bool wlc_phy_txpower_ipa_ison(struct brcms_phy_pub *pih);

extern void wlc_phy_stf_chain_init(struct brcms_phy_pub *pih, u8 txchain,
				   u8 rxchain);
extern void wlc_phy_stf_chain_set(struct brcms_phy_pub *pih, u8 txchain,
				  u8 rxchain);
extern void wlc_phy_stf_chain_get(struct brcms_phy_pub *pih, u8 *txchain,
				  u8 *rxchain);
extern u8 wlc_phy_stf_chain_active_get(struct brcms_phy_pub *pih);
extern s8 wlc_phy_stf_ssmode_get(struct brcms_phy_pub *pih,
				 u16 chanspec);
extern void wlc_phy_ldpc_override_set(struct brcms_phy_pub *ppi, bool val);

extern void wlc_phy_cal_perical(struct brcms_phy_pub *ppi, u8 reason);
extern void wlc_phy_noise_sample_request_external(struct brcms_phy_pub *ppi);
extern void wlc_phy_edcrs_lock(struct brcms_phy_pub *pih, bool lock);
extern void wlc_phy_cal_papd_recal(struct brcms_phy_pub *ppi);

extern void wlc_phy_ant_rxdiv_set(struct brcms_phy_pub *ppi, u8 val);
extern void wlc_phy_clear_tssi(struct brcms_phy_pub *ppi);
extern void wlc_phy_hold_upd(struct brcms_phy_pub *ppi, u32 id, bool val);
extern void wlc_phy_mute_upd(struct brcms_phy_pub *ppi, bool val, u32 flags);

extern void wlc_phy_antsel_type_set(struct brcms_phy_pub *ppi, u8 antsel_type);

extern void wlc_phy_txpower_get_current(struct brcms_phy_pub *ppi,
					struct tx_power *power, uint channel);

extern void wlc_phy_initcal_enable(struct brcms_phy_pub *pih, bool initcal);
extern bool wlc_phy_test_ison(struct brcms_phy_pub *ppi);
extern void wlc_phy_txpwr_percent_set(struct brcms_phy_pub *ppi,
				      u8 txpwr_percent);
extern void wlc_phy_ofdm_rateset_war(struct brcms_phy_pub *pih, bool war);
extern void wlc_phy_bf_preempt_enable(struct brcms_phy_pub *pih,
				      bool bf_preempt);
extern void wlc_phy_machwcap_set(struct brcms_phy_pub *ppi, u32 machwcap);

extern void wlc_phy_runbist_config(struct brcms_phy_pub *ppi, bool start_end);

extern void wlc_phy_freqtrack_start(struct brcms_phy_pub *ppi);
extern void wlc_phy_freqtrack_end(struct brcms_phy_pub *ppi);

extern const u8 *wlc_phy_get_ofdm_rate_lookup(void);

extern s8 wlc_phy_get_tx_power_offset_by_mcs(struct brcms_phy_pub *ppi,
					     u8 mcs_offset);
extern s8 wlc_phy_get_tx_power_offset(struct brcms_phy_pub *ppi, u8 tbl_offset);
#endif                          /* _BRCM_PHY_HAL_H_ */
