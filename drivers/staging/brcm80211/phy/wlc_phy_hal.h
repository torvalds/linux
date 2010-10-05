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

#ifndef _wlc_phy_h_
#define _wlc_phy_h_

#include <typedefs.h>
#include <wlioctl.h>
#include <siutils.h>
#include <d11.h>
#include <wlc_phy_shim.h>

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

#define PHY_TPC_HW_OFF		FALSE
#define PHY_TPC_HW_ON		TRUE

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

#define PHY_NOISE_FIXED_VAL 		(-95)
#define PHY_NOISE_FIXED_VAL_NPHY       	(-92)
#define PHY_NOISE_FIXED_VAL_LCNPHY     	(-92)

#define PHY_MODE_CAL		0x0002
#define PHY_MODE_NOISEM		0x0004

#define WLC_TXPWR_DB_FACTOR	4

#define WLC_NUM_RATES_CCK           4
#define WLC_NUM_RATES_OFDM          8
#define WLC_NUM_RATES_MCS_1_STREAM  8
#define WLC_NUM_RATES_MCS_2_STREAM  8
#define WLC_NUM_RATES_MCS_3_STREAM  8
#define WLC_NUM_RATES_MCS_4_STREAM  8
typedef struct txpwr_limits {
	u8 cck[WLC_NUM_RATES_CCK];
	u8 ofdm[WLC_NUM_RATES_OFDM];

	u8 ofdm_cdd[WLC_NUM_RATES_OFDM];

	u8 ofdm_40_siso[WLC_NUM_RATES_OFDM];
	u8 ofdm_40_cdd[WLC_NUM_RATES_OFDM];

	u8 mcs_20_siso[WLC_NUM_RATES_MCS_1_STREAM];
	u8 mcs_20_cdd[WLC_NUM_RATES_MCS_1_STREAM];
	u8 mcs_20_stbc[WLC_NUM_RATES_MCS_1_STREAM];
	u8 mcs_20_mimo[WLC_NUM_RATES_MCS_2_STREAM];

	u8 mcs_40_siso[WLC_NUM_RATES_MCS_1_STREAM];
	u8 mcs_40_cdd[WLC_NUM_RATES_MCS_1_STREAM];
	u8 mcs_40_stbc[WLC_NUM_RATES_MCS_1_STREAM];
	u8 mcs_40_mimo[WLC_NUM_RATES_MCS_2_STREAM];
	u8 mcs32;
} txpwr_limits_t;

typedef struct {
	u8 vec[MAXCHANNEL / NBBY];
} chanvec_t;

struct rpc_info;
typedef struct shared_phy shared_phy_t;

struct phy_pub;

#ifdef WLC_HIGH_ONLY
typedef struct wlc_rpc_phy wlc_phy_t;
#else
typedef struct phy_pub wlc_phy_t;
#endif

typedef struct shared_phy_params {
	void *osh;
	si_t *sih;
	void *physhim;
	uint unit;
	uint corerev;
	uint bustype;
	uint buscorerev;
	char *vars;
	uint16 vid;
	uint16 did;
	uint chip;
	uint chiprev;
	uint chippkg;
	uint sromrev;
	uint boardtype;
	uint boardrev;
	uint boardvendor;
	uint32 boardflags;
	uint32 boardflags2;
} shared_phy_params_t;

#ifdef WLC_LOW

extern shared_phy_t *wlc_phy_shared_attach(shared_phy_params_t *shp);
extern void wlc_phy_shared_detach(shared_phy_t *phy_sh);
extern wlc_phy_t *wlc_phy_attach(shared_phy_t *sh, void *regs, int bandtype,
				 char *vars);
extern void wlc_phy_detach(wlc_phy_t *ppi);

extern bool wlc_phy_get_phyversion(wlc_phy_t *pih, uint16 *phytype,
				   uint16 *phyrev, uint16 *radioid,
				   uint16 *radiover);
extern bool wlc_phy_get_encore(wlc_phy_t *pih);
extern uint32 wlc_phy_get_coreflags(wlc_phy_t *pih);

extern void wlc_phy_hw_clk_state_upd(wlc_phy_t *ppi, bool newstate);
extern void wlc_phy_hw_state_upd(wlc_phy_t *ppi, bool newstate);
extern void wlc_phy_init(wlc_phy_t *ppi, chanspec_t chanspec);
extern void wlc_phy_watchdog(wlc_phy_t *ppi);
extern int wlc_phy_down(wlc_phy_t *ppi);
extern uint32 wlc_phy_clk_bwbits(wlc_phy_t *pih);
extern void wlc_phy_cal_init(wlc_phy_t *ppi);
extern void wlc_phy_antsel_init(wlc_phy_t *ppi, bool lut_init);

extern void wlc_phy_chanspec_set(wlc_phy_t *ppi, chanspec_t chanspec);
extern chanspec_t wlc_phy_chanspec_get(wlc_phy_t *ppi);
extern void wlc_phy_chanspec_radio_set(wlc_phy_t *ppi, chanspec_t newch);
extern uint16 wlc_phy_bw_state_get(wlc_phy_t *ppi);
extern void wlc_phy_bw_state_set(wlc_phy_t *ppi, uint16 bw);

extern void wlc_phy_rssi_compute(wlc_phy_t *pih, void *ctx);
extern void wlc_phy_por_inform(wlc_phy_t *ppi);
extern void wlc_phy_noise_sample_intr(wlc_phy_t *ppi);
extern bool wlc_phy_bist_check_phy(wlc_phy_t *ppi);

extern void wlc_phy_set_deaf(wlc_phy_t *ppi, bool user_flag);

extern void wlc_phy_switch_radio(wlc_phy_t *ppi, bool on);
extern void wlc_phy_anacore(wlc_phy_t *ppi, bool on);

#endif				/* WLC_LOW */

extern void wlc_phy_BSSinit(wlc_phy_t *ppi, bool bonlyap, int rssi);

extern void wlc_phy_chanspec_ch14_widefilter_set(wlc_phy_t *ppi,
						 bool wide_filter);
extern void wlc_phy_chanspec_band_validch(wlc_phy_t *ppi, uint band,
					  chanvec_t *channels);
extern chanspec_t wlc_phy_chanspec_band_firstch(wlc_phy_t *ppi, uint band);

extern void wlc_phy_txpower_sromlimit(wlc_phy_t *ppi, uint chan,
				      u8 *_min_, u8 *_max_, int rate);
extern void wlc_phy_txpower_sromlimit_max_get(wlc_phy_t *ppi, uint chan,
					      u8 *_max_, u8 *_min_);
extern void wlc_phy_txpower_boardlimit_band(wlc_phy_t *ppi, uint band, int32 *,
					    int32 *, uint32 *);
extern void wlc_phy_txpower_limit_set(wlc_phy_t *ppi, struct txpwr_limits *,
				      chanspec_t chanspec);
extern int wlc_phy_txpower_get(wlc_phy_t *ppi, uint *qdbm, bool *override);
extern int wlc_phy_txpower_set(wlc_phy_t *ppi, uint qdbm, bool override);
extern void wlc_phy_txpower_target_set(wlc_phy_t *ppi, struct txpwr_limits *);
extern bool wlc_phy_txpower_hw_ctrl_get(wlc_phy_t *ppi);
extern void wlc_phy_txpower_hw_ctrl_set(wlc_phy_t *ppi, bool hwpwrctrl);
extern u8 wlc_phy_txpower_get_target_min(wlc_phy_t *ppi);
extern u8 wlc_phy_txpower_get_target_max(wlc_phy_t *ppi);
extern bool wlc_phy_txpower_ipa_ison(wlc_phy_t *pih);

extern void wlc_phy_stf_chain_init(wlc_phy_t *pih, u8 txchain,
				   u8 rxchain);
extern void wlc_phy_stf_chain_set(wlc_phy_t *pih, u8 txchain,
				  u8 rxchain);
extern void wlc_phy_stf_chain_get(wlc_phy_t *pih, u8 *txchain,
				  u8 *rxchain);
extern u8 wlc_phy_stf_chain_active_get(wlc_phy_t *pih);
extern s8 wlc_phy_stf_ssmode_get(wlc_phy_t *pih, chanspec_t chanspec);
extern void wlc_phy_ldpc_override_set(wlc_phy_t *ppi, bool val);

extern void wlc_phy_cal_perical(wlc_phy_t *ppi, u8 reason);
extern void wlc_phy_noise_sample_request_external(wlc_phy_t *ppi);
extern void wlc_phy_edcrs_lock(wlc_phy_t *pih, bool lock);
extern void wlc_phy_cal_papd_recal(wlc_phy_t *ppi);

extern void wlc_phy_ant_rxdiv_set(wlc_phy_t *ppi, u8 val);
extern bool wlc_phy_ant_rxdiv_get(wlc_phy_t *ppi, u8 *pval);
extern void wlc_phy_clear_tssi(wlc_phy_t *ppi);
extern void wlc_phy_hold_upd(wlc_phy_t *ppi, mbool id, bool val);
extern void wlc_phy_mute_upd(wlc_phy_t *ppi, bool val, mbool flags);

extern void wlc_phy_antsel_type_set(wlc_phy_t *ppi, u8 antsel_type);

extern void wlc_phy_txpower_get_current(wlc_phy_t *ppi, tx_power_t *power,
					uint channel);

extern void wlc_phy_initcal_enable(wlc_phy_t *pih, bool initcal);
extern bool wlc_phy_test_ison(wlc_phy_t *ppi);
extern void wlc_phy_txpwr_percent_set(wlc_phy_t *ppi, u8 txpwr_percent);
extern void wlc_phy_ofdm_rateset_war(wlc_phy_t *pih, bool war);
extern void wlc_phy_bf_preempt_enable(wlc_phy_t *pih, bool bf_preempt);
extern void wlc_phy_machwcap_set(wlc_phy_t *ppi, uint32 machwcap);

extern void wlc_phy_runbist_config(wlc_phy_t *ppi, bool start_end);

extern void wlc_phy_freqtrack_start(wlc_phy_t *ppi);
extern void wlc_phy_freqtrack_end(wlc_phy_t *ppi);

extern const u8 *wlc_phy_get_ofdm_rate_lookup(void);

extern s8 wlc_phy_get_tx_power_offset_by_mcs(wlc_phy_t *ppi,
					       u8 mcs_offset);
extern s8 wlc_phy_get_tx_power_offset(wlc_phy_t *ppi, u8 tbl_offset);
#endif				/* _wlc_phy_h_ */
