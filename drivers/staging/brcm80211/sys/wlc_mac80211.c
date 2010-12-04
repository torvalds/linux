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
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <bcmdefs.h>
#include <bcmdevs.h>
#include <wlc_cfg.h>
#include <osl.h>
#include <bcmutils.h>
#include <bcmwifi.h>
#include <siutils.h>
#include <bcmendian.h>
#include <proto/wpa.h>
#include <pcicfg.h>
#include <bcmsrom.h>
#include <wlioctl.h>
#include <sbhndpio.h>
#include <sbhnddma.h>
#include <hnddma.h>
#include <hndpmu.h>
#include <d11.h>
#include <wlc_rate.h>
#include <wlc_pub.h>
#include <wlc_key.h>
#include <wlc_bsscfg.h>
#include <wlc_channel.h>
#include <wlc_event.h>
#include <wlc_mac80211.h>
#include <wlc_bmac.h>
#include <wlc_scb.h>
#include <wlc_phy_hal.h>
#include <wlc_phy_shim.h>
#include <wlc_antsel.h>
#include <wlc_stf.h>
#include <wlc_ampdu.h>
#include <wlc_event.h>
#include <wl_export.h>
#include "d11ucode_ext.h"
#include <wlc_alloc.h>
#include <net/mac80211.h>
#include <wl_dbg.h>


/*
 * buffer length needed for wlc_format_ssid
 * 32 SSID chars, max of 4 chars for each SSID char "\xFF", plus NULL.
 */
#define SSID_FMT_BUF_LEN	((4 * DOT11_MAX_SSID_LEN) + 1)

#define	TIMER_INTERVAL_WATCHDOG	1000	/* watchdog timer, in unit of ms */
#define	TIMER_INTERVAL_RADIOCHK	800	/* radio monitor timer, in unit of ms */

#ifndef WLC_MPC_MAX_DELAYCNT
#define	WLC_MPC_MAX_DELAYCNT	10	/* Max MPC timeout, in unit of watchdog */
#endif
#define	WLC_MPC_MIN_DELAYCNT	1	/* Min MPC timeout, in unit of watchdog */
#define	WLC_MPC_THRESHOLD	3	/* MPC count threshold level */

#define	BEACON_INTERVAL_DEFAULT	100	/* beacon interval, in unit of 1024TU */
#define	DTIM_INTERVAL_DEFAULT	3	/* DTIM interval, in unit of beacon interval */

/* Scale down delays to accommodate QT slow speed */
#define	BEACON_INTERVAL_DEF_QT	20	/* beacon interval, in unit of 1024TU */
#define	DTIM_INTERVAL_DEF_QT	1	/* DTIM interval, in unit of beacon interval */

#define	TBTT_ALIGN_LEEWAY_US	100	/* min leeway before first TBTT in us */

/*
 * driver maintains internal 'tick'(wlc->pub->now) which increments in 1s OS timer(soft
 * watchdog) it is not a wall clock and won't increment when driver is in "down" state
 * this low resolution driver tick can be used for maintenance tasks such as phy
 * calibration and scb update
 */

/* watchdog trigger mode: OSL timer or TBTT */
#define WLC_WATCHDOG_TBTT(wlc) \
	(wlc->stas_associated > 0 && wlc->PM != PM_OFF && wlc->pub->align_wd_tbtt)

/* To inform the ucode of the last mcast frame posted so that it can clear moredata bit */
#define BCMCFID(wlc, fid) wlc_bmac_write_shm((wlc)->hw, M_BCMC_FID, (fid))

#define WLC_WAR16165(wlc) (wlc->pub->sih->bustype == PCI_BUS && \
				(!AP_ENAB(wlc->pub)) && (wlc->war16165))

/* debug/trace */
uint wl_msg_level =
#if defined(BCMDBG)
    WL_ERROR_VAL;
#else
    0;
#endif				/* BCMDBG */

/* Find basic rate for a given rate */
#define WLC_BASIC_RATE(wlc, rspec)	(IS_MCS(rspec) ? \
			(wlc)->band->basic_rate[mcs_table[rspec & RSPEC_RATE_MASK].leg_ofdm] : \
			(wlc)->band->basic_rate[rspec & RSPEC_RATE_MASK])

#define FRAMETYPE(r, mimoframe)	(IS_MCS(r) ? mimoframe	: (IS_CCK(r) ? FT_CCK : FT_OFDM))

#define RFDISABLE_DEFAULT	10000000	/* rfdisable delay timer 500 ms, runs of ALP clock */

#define WLC_TEMPSENSE_PERIOD		10	/* 10 second timeout */

#define SCAN_IN_PROGRESS(x)	0

#define EPI_VERSION_NUM		0x054b0b00

#ifdef BCMDBG
/* pointer to most recently allocated wl/wlc */
static struct wlc_info *wlc_info_dbg = (struct wlc_info *) (NULL);
#endif

/* IOVar table */

/* Parameter IDs, for use only internally to wlc -- in the wlc_iovars
 * table and by the wlc_doiovar() function.  No ordering is imposed:
 * the table is keyed by name, and the function uses a switch.
 */
enum {
	IOV_MPC = 1,
	IOV_QTXPOWER,
	IOV_BCN_LI_BCN,		/* Beacon listen interval in # of beacons */
	IOV_LAST		/* In case of a need to check max ID number */
};

const bcm_iovar_t wlc_iovars[] = {
	{"mpc", IOV_MPC, (IOVF_OPEN_ALLOW), IOVT_BOOL, 0},
	{"qtxpower", IOV_QTXPOWER, (IOVF_WHL | IOVF_OPEN_ALLOW), IOVT_UINT32,
	 0},
	{"bcn_li_bcn", IOV_BCN_LI_BCN, 0, IOVT_UINT8, 0},
	{NULL, 0, 0, 0, 0}
};

const u8 prio2fifo[NUMPRIO] = {
	TX_AC_BE_FIFO,		/* 0    BE      AC_BE   Best Effort */
	TX_AC_BK_FIFO,		/* 1    BK      AC_BK   Background */
	TX_AC_BK_FIFO,		/* 2    --      AC_BK   Background */
	TX_AC_BE_FIFO,		/* 3    EE      AC_BE   Best Effort */
	TX_AC_VI_FIFO,		/* 4    CL      AC_VI   Video */
	TX_AC_VI_FIFO,		/* 5    VI      AC_VI   Video */
	TX_AC_VO_FIFO,		/* 6    VO      AC_VO   Voice */
	TX_AC_VO_FIFO		/* 7    NC      AC_VO   Voice */
};

/* precedences numbers for wlc queues. These are twice as may levels as
 * 802.1D priorities.
 * Odd numbers are used for HI priority traffic at same precedence levels
 * These constants are used ONLY by wlc_prio2prec_map.  Do not use them elsewhere.
 */
#define	_WLC_PREC_NONE		0	/* None = - */
#define	_WLC_PREC_BK		2	/* BK - Background */
#define	_WLC_PREC_BE		4	/* BE - Best-effort */
#define	_WLC_PREC_EE		6	/* EE - Excellent-effort */
#define	_WLC_PREC_CL		8	/* CL - Controlled Load */
#define	_WLC_PREC_VI		10	/* Vi - Video */
#define	_WLC_PREC_VO		12	/* Vo - Voice */
#define	_WLC_PREC_NC		14	/* NC - Network Control */

/* 802.1D Priority to precedence queue mapping */
const u8 wlc_prio2prec_map[] = {
	_WLC_PREC_BE,		/* 0 BE - Best-effort */
	_WLC_PREC_BK,		/* 1 BK - Background */
	_WLC_PREC_NONE,		/* 2 None = - */
	_WLC_PREC_EE,		/* 3 EE - Excellent-effort */
	_WLC_PREC_CL,		/* 4 CL - Controlled Load */
	_WLC_PREC_VI,		/* 5 Vi - Video */
	_WLC_PREC_VO,		/* 6 Vo - Voice */
	_WLC_PREC_NC,		/* 7 NC - Network Control */
};

/* Sanity check for tx_prec_map and fifo synchup
 * Either there are some packets pending for the fifo, else if fifo is empty then
 * all the corresponding precmap bits should be set
 */
#define WLC_TX_FIFO_CHECK(wlc, fifo) (TXPKTPENDGET((wlc), (fifo)) ||	\
	(TXPKTPENDGET((wlc), (fifo)) == 0 && \
	((wlc)->tx_prec_map & (wlc)->fifo2prec_map[(fifo)]) == \
	(wlc)->fifo2prec_map[(fifo)]))

/* TX FIFO number to WME/802.1E Access Category */
const u8 wme_fifo2ac[] = { AC_BK, AC_BE, AC_VI, AC_VO, AC_BE, AC_BE };

/* WME/802.1E Access Category to TX FIFO number */
static const u8 wme_ac2fifo[] = { 1, 0, 2, 3 };

static bool in_send_q = false;

/* Shared memory location index for various AC params */
#define wme_shmemacindex(ac)	wme_ac2fifo[ac]

#ifdef BCMDBG
static const char *fifo_names[] = {
	"AC_BK", "AC_BE", "AC_VI", "AC_VO", "BCMC", "ATIM" };
const char *aci_names[] = { "AC_BE", "AC_BK", "AC_VI", "AC_VO" };
#endif

static const u8 acbitmap2maxprio[] = {
	PRIO_8021D_BE, PRIO_8021D_BE, PRIO_8021D_BK, PRIO_8021D_BK,
	PRIO_8021D_VI, PRIO_8021D_VI, PRIO_8021D_VI, PRIO_8021D_VI,
	PRIO_8021D_VO, PRIO_8021D_VO, PRIO_8021D_VO, PRIO_8021D_VO,
	PRIO_8021D_VO, PRIO_8021D_VO, PRIO_8021D_VO, PRIO_8021D_VO
};

/* currently the best mechanism for determining SIFS is the band in use */
#define SIFS(band) ((band)->bandtype == WLC_BAND_5G ? APHY_SIFS_TIME : BPHY_SIFS_TIME);

/* value for # replay counters currently supported */
#define WLC_REPLAY_CNTRS_VALUE	WPA_CAP_16_REPLAY_CNTRS

/* local prototypes */
static u16 BCMFASTPATH wlc_d11hdrs_mac80211(struct wlc_info *wlc,
					       struct ieee80211_hw *hw,
					       struct sk_buff *p,
					       struct scb *scb, uint frag,
					       uint nfrags, uint queue,
					       uint next_frag_len,
					       wsec_key_t *key,
					       ratespec_t rspec_override);

static void wlc_bss_default_init(struct wlc_info *wlc);
static void wlc_ucode_mac_upd(struct wlc_info *wlc);
static ratespec_t mac80211_wlc_set_nrate(struct wlc_info *wlc,
					 wlcband_t *cur_band, u32 int_val);
static void wlc_tx_prec_map_init(struct wlc_info *wlc);
static void wlc_watchdog(void *arg);
static void wlc_watchdog_by_timer(void *arg);
static int wlc_set_rateset(struct wlc_info *wlc, wlc_rateset_t *rs_arg);
static int wlc_iovar_rangecheck(struct wlc_info *wlc, u32 val,
				const bcm_iovar_t *vi);
static u8 wlc_local_constraint_qdbm(struct wlc_info *wlc);

/* send and receive */
static wlc_txq_info_t *wlc_txq_alloc(struct wlc_info *wlc,
				     struct osl_info *osh);
static void wlc_txq_free(struct wlc_info *wlc, struct osl_info *osh,
			 wlc_txq_info_t *qi);
static void wlc_txflowcontrol_signal(struct wlc_info *wlc, wlc_txq_info_t *qi,
				     bool on, int prio);
static void wlc_txflowcontrol_reset(struct wlc_info *wlc);
static u16 wlc_compute_airtime(struct wlc_info *wlc, ratespec_t rspec,
				  uint length);
static void wlc_compute_cck_plcp(ratespec_t rate, uint length, u8 *plcp);
static void wlc_compute_ofdm_plcp(ratespec_t rate, uint length, u8 *plcp);
static void wlc_compute_mimo_plcp(ratespec_t rate, uint length, u8 *plcp);
static u16 wlc_compute_frame_dur(struct wlc_info *wlc, ratespec_t rate,
				    u8 preamble_type, uint next_frag_len);
static void wlc_recvctl(struct wlc_info *wlc, struct osl_info *osh,
			d11rxhdr_t *rxh, struct sk_buff *p);
static uint wlc_calc_frame_len(struct wlc_info *wlc, ratespec_t rate,
			       u8 preamble_type, uint dur);
static uint wlc_calc_ack_time(struct wlc_info *wlc, ratespec_t rate,
			      u8 preamble_type);
static uint wlc_calc_cts_time(struct wlc_info *wlc, ratespec_t rate,
			      u8 preamble_type);
/* interrupt, up/down, band */
static void wlc_setband(struct wlc_info *wlc, uint bandunit);
static chanspec_t wlc_init_chanspec(struct wlc_info *wlc);
static void wlc_bandinit_ordered(struct wlc_info *wlc, chanspec_t chanspec);
static void wlc_bsinit(struct wlc_info *wlc);
static int wlc_duty_cycle_set(struct wlc_info *wlc, int duty_cycle, bool isOFDM,
			      bool writeToShm);
static void wlc_radio_hwdisable_upd(struct wlc_info *wlc);
static bool wlc_radio_monitor_start(struct wlc_info *wlc);
static void wlc_radio_timer(void *arg);
static void wlc_radio_enable(struct wlc_info *wlc);
static void wlc_radio_upd(struct wlc_info *wlc);

/* scan, association, BSS */
static uint wlc_calc_ba_time(struct wlc_info *wlc, ratespec_t rate,
			     u8 preamble_type);
static void wlc_update_mimo_band_bwcap(struct wlc_info *wlc, u8 bwcap);
static void wlc_ht_update_sgi_rx(struct wlc_info *wlc, int val);
static void wlc_ht_update_ldpc(struct wlc_info *wlc, s8 val);
static void wlc_war16165(struct wlc_info *wlc, bool tx);

static void wlc_process_eventq(void *arg);
static void wlc_wme_retries_write(struct wlc_info *wlc);
static bool wlc_attach_stf_ant_init(struct wlc_info *wlc);
static uint wlc_attach_module(struct wlc_info *wlc);
static void wlc_detach_module(struct wlc_info *wlc);
static void wlc_timers_deinit(struct wlc_info *wlc);
static void wlc_down_led_upd(struct wlc_info *wlc);
static uint wlc_down_del_timer(struct wlc_info *wlc);
static void wlc_ofdm_rateset_war(struct wlc_info *wlc);
static int _wlc_ioctl(struct wlc_info *wlc, int cmd, void *arg, int len,
		      struct wlc_if *wlcif);

#if defined(BCMDBG)
void wlc_get_rcmta(struct wlc_info *wlc, int idx, struct ether_addr *addr)
{
	d11regs_t *regs = wlc->regs;
	u32 v32;
	struct osl_info *osh;

	WL_TRACE(("wl%d: %s\n", WLCWLUNIT(wlc), __func__));

	ASSERT(wlc->pub->corerev > 4);

	osh = wlc->osh;

	W_REG(osh, &regs->objaddr, (OBJADDR_RCMTA_SEL | (idx * 2)));
	(void)R_REG(osh, &regs->objaddr);
	v32 = R_REG(osh, &regs->objdata);
	addr->octet[0] = (u8) v32;
	addr->octet[1] = (u8) (v32 >> 8);
	addr->octet[2] = (u8) (v32 >> 16);
	addr->octet[3] = (u8) (v32 >> 24);
	W_REG(osh, &regs->objaddr, (OBJADDR_RCMTA_SEL | ((idx * 2) + 1)));
	(void)R_REG(osh, &regs->objaddr);
	v32 = R_REG(osh, (volatile u16 *)&regs->objdata);
	addr->octet[4] = (u8) v32;
	addr->octet[5] = (u8) (v32 >> 8);
}
#endif				/* defined(BCMDBG) */

/* keep the chip awake if needed */
bool wlc_stay_awake(struct wlc_info *wlc)
{
	return true;
}

/* conditions under which the PM bit should be set in outgoing frames and STAY_AWAKE is meaningful
 */
bool wlc_ps_allowed(struct wlc_info *wlc)
{
	int idx;
	wlc_bsscfg_t *cfg;

	/* disallow PS when one of the following global conditions meets */
	if (!wlc->pub->associated || !wlc->PMenabled || wlc->PM_override)
		return false;

	/* disallow PS when one of these meets when not scanning */
	if (!wlc->PMblocked) {
		if (AP_ACTIVE(wlc) || wlc->monitor)
			return false;
	}

	FOREACH_AS_STA(wlc, idx, cfg) {
		/* disallow PS when one of the following bsscfg specific conditions meets */
		if (!cfg->BSS || !WLC_PORTOPEN(cfg))
			return false;

		if (!cfg->dtim_programmed)
			return false;
	}

	return true;
}

void wlc_reset(struct wlc_info *wlc)
{
	WL_TRACE(("wl%d: wlc_reset\n", wlc->pub->unit));

	wlc->check_for_unaligned_tbtt = false;

	/* slurp up hw mac counters before core reset */
	if (WLC_UPDATE_STATS(wlc)) {
		wlc_statsupd(wlc);

		/* reset our snapshot of macstat counters */
		memset((char *)wlc->core->macstat_snapshot, 0,
			sizeof(macstat_t));
	}

	wlc_bmac_reset(wlc->hw);
	wlc_ampdu_reset(wlc->ampdu);
	wlc->txretried = 0;

}

void wlc_fatal_error(struct wlc_info *wlc)
{
	WL_ERROR(("wl%d: fatal error, reinitializing\n", wlc->pub->unit));
	wl_init(wlc->wl);
}

/* Return the channel the driver should initialize during wlc_init.
 * the channel may have to be changed from the currently configured channel
 * if other configurations are in conflict (bandlocked, 11n mode disabled,
 * invalid channel for current country, etc.)
 */
static chanspec_t wlc_init_chanspec(struct wlc_info *wlc)
{
	chanspec_t chanspec =
	    1 | WL_CHANSPEC_BW_20 | WL_CHANSPEC_CTL_SB_NONE |
	    WL_CHANSPEC_BAND_2G;

	/* make sure the channel is on the supported band if we are band-restricted */
	if (wlc->bandlocked || NBANDS(wlc) == 1) {
		ASSERT(CHSPEC_WLCBANDUNIT(chanspec) == wlc->band->bandunit);
	}
	ASSERT(wlc_valid_chanspec_db(wlc->cmi, chanspec));
	return chanspec;
}

struct scb global_scb;

static void wlc_init_scb(struct wlc_info *wlc, struct scb *scb)
{
	int i;
	scb->flags = SCB_WMECAP | SCB_HTCAP;
	for (i = 0; i < NUMPRIO; i++)
		scb->seqnum[i] = 0;
}

void wlc_init(struct wlc_info *wlc)
{
	d11regs_t *regs;
	chanspec_t chanspec;
	int i;
	wlc_bsscfg_t *bsscfg;
	bool mute = false;

	WL_TRACE(("wl%d: wlc_init\n", wlc->pub->unit));

	regs = wlc->regs;

	/* This will happen if a big-hammer was executed. In that case, we want to go back
	 * to the channel that we were on and not new channel
	 */
	if (wlc->pub->associated)
		chanspec = wlc->home_chanspec;
	else
		chanspec = wlc_init_chanspec(wlc);

	wlc_bmac_init(wlc->hw, chanspec, mute);

	wlc->seckeys = wlc_bmac_read_shm(wlc->hw, M_SECRXKEYS_PTR) * 2;
	if (D11REV_GE(wlc->pub->corerev, 15) && (wlc->machwcap & MCAP_TKIPMIC))
		wlc->tkmickeys =
		    wlc_bmac_read_shm(wlc->hw, M_TKMICKEYS_PTR) * 2;

	/* update beacon listen interval */
	wlc_bcn_li_upd(wlc);
	wlc->bcn_wait_prd =
	    (u8) (wlc_bmac_read_shm(wlc->hw, M_NOSLPZNATDTIM) >> 10);
	ASSERT(wlc->bcn_wait_prd > 0);

	/* the world is new again, so is our reported rate */
	wlc_reprate_init(wlc);

	/* write ethernet address to core */
	FOREACH_BSS(wlc, i, bsscfg) {
		wlc_set_mac(bsscfg);
		wlc_set_bssid(bsscfg);
	}

	/* Update tsf_cfprep if associated and up */
	if (wlc->pub->associated) {
		FOREACH_BSS(wlc, i, bsscfg) {
			if (bsscfg->up) {
				u32 bi;

				/* get beacon period from bsscfg and convert to uS */
				bi = bsscfg->current_bss->beacon_period << 10;
				/* update the tsf_cfprep register */
				/* since init path would reset to default value */
				W_REG(wlc->osh, &regs->tsf_cfprep,
				      (bi << CFPREP_CBI_SHIFT));

				/* Update maccontrol PM related bits */
				wlc_set_ps_ctrl(wlc);

				break;
			}
		}
	}

	wlc_key_hw_init_all(wlc);

	wlc_bandinit_ordered(wlc, chanspec);

	wlc_init_scb(wlc, &global_scb);

	/* init probe response timeout */
	wlc_write_shm(wlc, M_PRS_MAXTIME, wlc->prb_resp_timeout);

	/* init max burst txop (framebursting) */
	wlc_write_shm(wlc, M_MBURST_TXOP,
		      (wlc->
		       _rifs ? (EDCF_AC_VO_TXOP_AP << 5) : MAXFRAMEBURST_TXOP));

	/* initialize maximum allowed duty cycle */
	wlc_duty_cycle_set(wlc, wlc->tx_duty_cycle_ofdm, true, true);
	wlc_duty_cycle_set(wlc, wlc->tx_duty_cycle_cck, false, true);

	/* Update some shared memory locations related to max AMPDU size allowed to received */
	wlc_ampdu_shm_upd(wlc->ampdu);

	/* band-specific inits */
	wlc_bsinit(wlc);

	/* Enable EDCF mode (while the MAC is suspended) */
	if (EDCF_ENAB(wlc->pub)) {
		OR_REG(wlc->osh, &regs->ifs_ctl, IFS_USEEDCF);
		wlc_edcf_setparams(wlc->cfg, false);
	}

	/* Init precedence maps for empty FIFOs */
	wlc_tx_prec_map_init(wlc);

	/* read the ucode version if we have not yet done so */
	if (wlc->ucode_rev == 0) {
		wlc->ucode_rev =
		    wlc_read_shm(wlc, M_BOM_REV_MAJOR) << NBITS(u16);
		wlc->ucode_rev |= wlc_read_shm(wlc, M_BOM_REV_MINOR);
	}

	/* ..now really unleash hell (allow the MAC out of suspend) */
	wlc_enable_mac(wlc);

	/* clear tx flow control */
	wlc_txflowcontrol_reset(wlc);

	/* clear tx data fifo suspends */
	wlc->tx_suspended = false;

	/* enable the RF Disable Delay timer */
	if (D11REV_GE(wlc->pub->corerev, 10))
		W_REG(wlc->osh, &wlc->regs->rfdisabledly, RFDISABLE_DEFAULT);

	/* initialize mpc delay */
	wlc->mpc_delay_off = wlc->mpc_dlycnt = WLC_MPC_MIN_DELAYCNT;

	/*
	 * Initialize WME parameters; if they haven't been set by some other
	 * mechanism (IOVar, etc) then read them from the hardware.
	 */
	if (WLC_WME_RETRY_SHORT_GET(wlc, 0) == 0) {	/* Unintialized; read from HW */
		int ac;

		ASSERT(wlc->clk);
		for (ac = 0; ac < AC_COUNT; ac++) {
			wlc->wme_retries[ac] =
			    wlc_read_shm(wlc, M_AC_TXLMT_ADDR(ac));
		}
	}
}

void wlc_mac_bcn_promisc_change(struct wlc_info *wlc, bool promisc)
{
	wlc->bcnmisc_monitor = promisc;
	wlc_mac_bcn_promisc(wlc);
}

void wlc_mac_bcn_promisc(struct wlc_info *wlc)
{
	if ((AP_ENAB(wlc->pub) && (N_ENAB(wlc->pub) || wlc->band->gmode)) ||
	    wlc->bcnmisc_ibss || wlc->bcnmisc_scan || wlc->bcnmisc_monitor)
		wlc_mctrl(wlc, MCTL_BCNS_PROMISC, MCTL_BCNS_PROMISC);
	else
		wlc_mctrl(wlc, MCTL_BCNS_PROMISC, 0);
}

/* set or clear maccontrol bits MCTL_PROMISC and MCTL_KEEPCONTROL */
void wlc_mac_promisc(struct wlc_info *wlc)
{
	u32 promisc_bits = 0;

	/* promiscuous mode just sets MCTL_PROMISC
	 * Note: APs get all BSS traffic without the need to set the MCTL_PROMISC bit
	 * since all BSS data traffic is directed at the AP
	 */
	if (PROMISC_ENAB(wlc->pub) && !AP_ENAB(wlc->pub) && !wlc->wet)
		promisc_bits |= MCTL_PROMISC;

	/* monitor mode needs both MCTL_PROMISC and MCTL_KEEPCONTROL
	 * Note: monitor mode also needs MCTL_BCNS_PROMISC, but that is
	 * handled in wlc_mac_bcn_promisc()
	 */
	if (MONITOR_ENAB(wlc))
		promisc_bits |= MCTL_PROMISC | MCTL_KEEPCONTROL;

	wlc_mctrl(wlc, MCTL_PROMISC | MCTL_KEEPCONTROL, promisc_bits);
}

/* check if hps and wake states of sw and hw are in sync */
bool wlc_ps_check(struct wlc_info *wlc)
{
	bool res = true;
	bool hps, wake;
	bool wake_ok;

	if (!AP_ACTIVE(wlc)) {
		volatile u32 tmp;
		tmp = R_REG(wlc->osh, &wlc->regs->maccontrol);

		/* If deviceremoved is detected, then don't take any action as this can be called
		 * in any context. Assume that caller will take care of the condition. This is just
		 * to avoid assert
		 */
		if (tmp == 0xffffffff) {
			WL_ERROR(("wl%d: %s: dead chip\n", wlc->pub->unit,
				  __func__));
			return DEVICEREMOVED(wlc);
		}

		hps = PS_ALLOWED(wlc);

		if (hps != ((tmp & MCTL_HPS) != 0)) {
			int idx;
			wlc_bsscfg_t *cfg;
			WL_ERROR(("wl%d: hps not sync, sw %d, maccontrol 0x%x\n", wlc->pub->unit, hps, tmp));
			FOREACH_BSS(wlc, idx, cfg) {
				if (!BSSCFG_STA(cfg))
					continue;
			}

			res = false;
		}
		/* For a monolithic build the wake check can be exact since it looks at wake
		 * override bits. The MCTL_WAKE bit should match the 'wake' value.
		 */
		wake = STAY_AWAKE(wlc) || wlc->hw->wake_override;
		wake_ok = (wake == ((tmp & MCTL_WAKE) != 0));
		if (hps && !wake_ok) {
			WL_ERROR(("wl%d: wake not sync, sw %d maccontrol 0x%x\n", wlc->pub->unit, wake, tmp));
			res = false;
		}
	}
	ASSERT(res);
	return res;
}

/* push sw hps and wake state through hardware */
void wlc_set_ps_ctrl(struct wlc_info *wlc)
{
	u32 v1, v2;
	bool hps, wake;
	bool awake_before;

	hps = PS_ALLOWED(wlc);
	wake = hps ? (STAY_AWAKE(wlc)) : true;

	WL_TRACE(("wl%d: wlc_set_ps_ctrl: hps %d wake %d\n", wlc->pub->unit,
		  hps, wake));

	v1 = R_REG(wlc->osh, &wlc->regs->maccontrol);
	v2 = 0;
	if (hps)
		v2 |= MCTL_HPS;
	if (wake)
		v2 |= MCTL_WAKE;

	wlc_mctrl(wlc, MCTL_WAKE | MCTL_HPS, v2);

	awake_before = ((v1 & MCTL_WAKE) || ((v1 & MCTL_HPS) == 0));

	if (wake && !awake_before)
		wlc_bmac_wait_for_wake(wlc->hw);

}

/*
 * Write this BSS config's MAC address to core.
 * Updates RXE match engine.
 */
int wlc_set_mac(wlc_bsscfg_t *cfg)
{
	int err = 0;
	struct wlc_info *wlc = cfg->wlc;

	if (cfg == wlc->cfg) {
		/* enter the MAC addr into the RXE match registers */
		wlc_set_addrmatch(wlc, RCM_MAC_OFFSET, &cfg->cur_etheraddr);
	}

	wlc_ampdu_macaddr_upd(wlc);

	return err;
}

/* Write the BSS config's BSSID address to core (set_bssid in d11procs.tcl).
 * Updates RXE match engine.
 */
void wlc_set_bssid(wlc_bsscfg_t *cfg)
{
	struct wlc_info *wlc = cfg->wlc;

	/* if primary config, we need to update BSSID in RXE match registers */
	if (cfg == wlc->cfg) {
		wlc_set_addrmatch(wlc, RCM_BSSID_OFFSET, &cfg->BSSID);
	}
#ifdef SUPPORT_HWKEYS
	else if (BSSCFG_STA(cfg) && cfg->BSS) {
		wlc_rcmta_add_bssid(wlc, cfg);
	}
#endif
}

/*
 * Suspend the the MAC and update the slot timing
 * for standard 11b/g (20us slots) or shortslot 11g (9us slots).
 */
void wlc_switch_shortslot(struct wlc_info *wlc, bool shortslot)
{
	int idx;
	wlc_bsscfg_t *cfg;

	ASSERT(wlc->band->gmode);

	/* use the override if it is set */
	if (wlc->shortslot_override != WLC_SHORTSLOT_AUTO)
		shortslot = (wlc->shortslot_override == WLC_SHORTSLOT_ON);

	if (wlc->shortslot == shortslot)
		return;

	wlc->shortslot = shortslot;

	/* update the capability based on current shortslot mode */
	FOREACH_BSS(wlc, idx, cfg) {
		if (!cfg->associated)
			continue;
		cfg->current_bss->capability &= ~DOT11_CAP_SHORTSLOT;
		if (wlc->shortslot)
			cfg->current_bss->capability |= DOT11_CAP_SHORTSLOT;
	}

	wlc_bmac_set_shortslot(wlc->hw, shortslot);
}

static u8 wlc_local_constraint_qdbm(struct wlc_info *wlc)
{
	u8 local;
	s16 local_max;

	local = WLC_TXPWR_MAX;
	if (wlc->pub->associated &&
	    (wf_chspec_ctlchan(wlc->chanspec) ==
	     wf_chspec_ctlchan(wlc->home_chanspec))) {

		/* get the local power constraint if we are on the AP's
		 * channel [802.11h, 7.3.2.13]
		 */
		/* Clamp the value between 0 and WLC_TXPWR_MAX w/o overflowing the target */
		local_max =
		    (wlc->txpwr_local_max -
		     wlc->txpwr_local_constraint) * WLC_TXPWR_DB_FACTOR;
		if (local_max > 0 && local_max < WLC_TXPWR_MAX)
			return (u8) local_max;
		if (local_max < 0)
			return 0;
	}

	return local;
}

/* propagate home chanspec to all bsscfgs in case bsscfg->current_bss->chanspec is referenced */
void wlc_set_home_chanspec(struct wlc_info *wlc, chanspec_t chanspec)
{
	if (wlc->home_chanspec != chanspec) {
		int idx;
		wlc_bsscfg_t *cfg;

		wlc->home_chanspec = chanspec;

		FOREACH_BSS(wlc, idx, cfg) {
			if (!cfg->associated)
				continue;
			cfg->target_bss->chanspec = chanspec;
			cfg->current_bss->chanspec = chanspec;
		}

	}
}

static void wlc_set_phy_chanspec(struct wlc_info *wlc, chanspec_t chanspec)
{
	/* Save our copy of the chanspec */
	wlc->chanspec = chanspec;

	/* Set the chanspec and power limits for this locale after computing
	 * any 11h local tx power constraints.
	 */
	wlc_channel_set_chanspec(wlc->cmi, chanspec,
				 wlc_local_constraint_qdbm(wlc));

	if (wlc->stf->ss_algosel_auto)
		wlc_stf_ss_algo_channel_get(wlc, &wlc->stf->ss_algo_channel,
					    chanspec);

	wlc_stf_ss_update(wlc, wlc->band);

}

void wlc_set_chanspec(struct wlc_info *wlc, chanspec_t chanspec)
{
	uint bandunit;
	bool switchband = false;
	chanspec_t old_chanspec = wlc->chanspec;

	if (!wlc_valid_chanspec_db(wlc->cmi, chanspec)) {
		WL_ERROR(("wl%d: %s: Bad channel %d\n",
			  wlc->pub->unit, __func__, CHSPEC_CHANNEL(chanspec)));
		ASSERT(wlc_valid_chanspec_db(wlc->cmi, chanspec));
		return;
	}

	/* Switch bands if necessary */
	if (NBANDS(wlc) > 1) {
		bandunit = CHSPEC_WLCBANDUNIT(chanspec);
		if (wlc->band->bandunit != bandunit || wlc->bandinit_pending) {
			switchband = true;
			if (wlc->bandlocked) {
				WL_ERROR(("wl%d: %s: chspec %d band is locked!\n", wlc->pub->unit, __func__, CHSPEC_CHANNEL(chanspec)));
				return;
			}
			/* BMAC_NOTE: should the setband call come after the wlc_bmac_chanspec() ?
			 * if the setband updates (wlc_bsinit) use low level calls to inspect and
			 * set state, the state inspected may be from the wrong band, or the
			 * following wlc_bmac_set_chanspec() may undo the work.
			 */
			wlc_setband(wlc, bandunit);
		}
	}

	ASSERT(N_ENAB(wlc->pub) || !CHSPEC_IS40(chanspec));

	/* sync up phy/radio chanspec */
	wlc_set_phy_chanspec(wlc, chanspec);

	/* init antenna selection */
	if (CHSPEC_WLC_BW(old_chanspec) != CHSPEC_WLC_BW(chanspec)) {
		if (WLANTSEL_ENAB(wlc))
			wlc_antsel_init(wlc->asi);

		/* Fix the hardware rateset based on bw.
		 * Mainly add MCS32 for 40Mhz, remove MCS 32 for 20Mhz
		 */
		wlc_rateset_bw_mcs_filter(&wlc->band->hw_rateset,
					  wlc->band->
					  mimo_cap_40 ? CHSPEC_WLC_BW(chanspec)
					  : 0);
	}

	/* update some mac configuration since chanspec changed */
	wlc_ucode_mac_upd(wlc);
}

#if defined(BCMDBG)
static int wlc_get_current_txpwr(struct wlc_info *wlc, void *pwr, uint len)
{
	txpwr_limits_t txpwr;
	tx_power_t power;
	tx_power_legacy_t *old_power = NULL;
	int r, c;
	uint qdbm;
	bool override;

	if (len == sizeof(tx_power_legacy_t))
		old_power = (tx_power_legacy_t *) pwr;
	else if (len < sizeof(tx_power_t))
		return BCME_BUFTOOSHORT;

	memset(&power, 0, sizeof(tx_power_t));

	power.chanspec = WLC_BAND_PI_RADIO_CHANSPEC;
	if (wlc->pub->associated)
		power.local_chanspec = wlc->home_chanspec;

	/* Return the user target tx power limits for the various rates.  Note  wlc_phy.c's
	 * public interface only implements getting and setting a single value for all of
	 * rates, so we need to fill the array ourselves.
	 */
	wlc_phy_txpower_get(wlc->band->pi, &qdbm, &override);
	for (r = 0; r < WL_TX_POWER_RATES; r++) {
		power.user_limit[r] = (u8) qdbm;
	}

	power.local_max = wlc->txpwr_local_max * WLC_TXPWR_DB_FACTOR;
	power.local_constraint =
	    wlc->txpwr_local_constraint * WLC_TXPWR_DB_FACTOR;

	power.antgain[0] = wlc->bandstate[BAND_2G_INDEX]->antgain;
	power.antgain[1] = wlc->bandstate[BAND_5G_INDEX]->antgain;

	wlc_channel_reg_limits(wlc->cmi, power.chanspec, &txpwr);

#if WL_TX_POWER_CCK_NUM != WLC_NUM_RATES_CCK
#error "WL_TX_POWER_CCK_NUM != WLC_NUM_RATES_CCK"
#endif

	/* CCK tx power limits */
	for (c = 0, r = WL_TX_POWER_CCK_FIRST; c < WL_TX_POWER_CCK_NUM;
	     c++, r++)
		power.reg_limit[r] = txpwr.cck[c];

#if WL_TX_POWER_OFDM_NUM != WLC_NUM_RATES_OFDM
#error "WL_TX_POWER_OFDM_NUM != WLC_NUM_RATES_OFDM"
#endif

	/* 20 MHz OFDM SISO tx power limits */
	for (c = 0, r = WL_TX_POWER_OFDM_FIRST; c < WL_TX_POWER_OFDM_NUM;
	     c++, r++)
		power.reg_limit[r] = txpwr.ofdm[c];

	if (WLC_PHY_11N_CAP(wlc->band)) {

		/* 20 MHz OFDM CDD tx power limits */
		for (c = 0, r = WL_TX_POWER_OFDM20_CDD_FIRST;
		     c < WL_TX_POWER_OFDM_NUM; c++, r++)
			power.reg_limit[r] = txpwr.ofdm_cdd[c];

		/* 40 MHz OFDM SISO tx power limits */
		for (c = 0, r = WL_TX_POWER_OFDM40_SISO_FIRST;
		     c < WL_TX_POWER_OFDM_NUM; c++, r++)
			power.reg_limit[r] = txpwr.ofdm_40_siso[c];

		/* 40 MHz OFDM CDD tx power limits */
		for (c = 0, r = WL_TX_POWER_OFDM40_CDD_FIRST;
		     c < WL_TX_POWER_OFDM_NUM; c++, r++)
			power.reg_limit[r] = txpwr.ofdm_40_cdd[c];

#if WL_TX_POWER_MCS_1_STREAM_NUM != WLC_NUM_RATES_MCS_1_STREAM
#error "WL_TX_POWER_MCS_1_STREAM_NUM != WLC_NUM_RATES_MCS_1_STREAM"
#endif

		/* 20MHz MCS0-7 SISO tx power limits */
		for (c = 0, r = WL_TX_POWER_MCS20_SISO_FIRST;
		     c < WLC_NUM_RATES_MCS_1_STREAM; c++, r++)
			power.reg_limit[r] = txpwr.mcs_20_siso[c];

		/* 20MHz MCS0-7 CDD tx power limits */
		for (c = 0, r = WL_TX_POWER_MCS20_CDD_FIRST;
		     c < WLC_NUM_RATES_MCS_1_STREAM; c++, r++)
			power.reg_limit[r] = txpwr.mcs_20_cdd[c];

		/* 20MHz MCS0-7 STBC tx power limits */
		for (c = 0, r = WL_TX_POWER_MCS20_STBC_FIRST;
		     c < WLC_NUM_RATES_MCS_1_STREAM; c++, r++)
			power.reg_limit[r] = txpwr.mcs_20_stbc[c];

		/* 40MHz MCS0-7 SISO tx power limits */
		for (c = 0, r = WL_TX_POWER_MCS40_SISO_FIRST;
		     c < WLC_NUM_RATES_MCS_1_STREAM; c++, r++)
			power.reg_limit[r] = txpwr.mcs_40_siso[c];

		/* 40MHz MCS0-7 CDD tx power limits */
		for (c = 0, r = WL_TX_POWER_MCS40_CDD_FIRST;
		     c < WLC_NUM_RATES_MCS_1_STREAM; c++, r++)
			power.reg_limit[r] = txpwr.mcs_40_cdd[c];

		/* 40MHz MCS0-7 STBC tx power limits */
		for (c = 0, r = WL_TX_POWER_MCS40_STBC_FIRST;
		     c < WLC_NUM_RATES_MCS_1_STREAM; c++, r++)
			power.reg_limit[r] = txpwr.mcs_40_stbc[c];

#if WL_TX_POWER_MCS_2_STREAM_NUM != WLC_NUM_RATES_MCS_2_STREAM
#error "WL_TX_POWER_MCS_2_STREAM_NUM != WLC_NUM_RATES_MCS_2_STREAM"
#endif

		/* 20MHz MCS8-15 SDM tx power limits */
		for (c = 0, r = WL_TX_POWER_MCS20_SDM_FIRST;
		     c < WLC_NUM_RATES_MCS_2_STREAM; c++, r++)
			power.reg_limit[r] = txpwr.mcs_20_mimo[c];

		/* 40MHz MCS8-15 SDM tx power limits */
		for (c = 0, r = WL_TX_POWER_MCS40_SDM_FIRST;
		     c < WLC_NUM_RATES_MCS_2_STREAM; c++, r++)
			power.reg_limit[r] = txpwr.mcs_40_mimo[c];

		/* MCS 32 */
		power.reg_limit[WL_TX_POWER_MCS_32] = txpwr.mcs32;
	}

	wlc_phy_txpower_get_current(wlc->band->pi, &power,
				    CHSPEC_CHANNEL(power.chanspec));

	/* copy the tx_power_t struct to the return buffer,
	 * or convert to a tx_power_legacy_t struct
	 */
	if (!old_power) {
		bcopy(&power, pwr, sizeof(tx_power_t));
	} else {
		int band_idx = CHSPEC_IS2G(power.chanspec) ? 0 : 1;

		memset(old_power, 0, sizeof(tx_power_legacy_t));

		old_power->txpwr_local_max = power.local_max;
		old_power->txpwr_local_constraint = power.local_constraint;
		if (CHSPEC_IS2G(power.chanspec)) {
			old_power->txpwr_chan_reg_max = txpwr.cck[0];
			old_power->txpwr_est_Pout[band_idx] =
			    power.est_Pout_cck;
			old_power->txpwr_est_Pout_gofdm = power.est_Pout[0];
		} else {
			old_power->txpwr_chan_reg_max = txpwr.ofdm[0];
			old_power->txpwr_est_Pout[band_idx] = power.est_Pout[0];
		}
		old_power->txpwr_antgain[0] = power.antgain[0];
		old_power->txpwr_antgain[1] = power.antgain[1];

		for (r = 0; r < NUM_PWRCTRL_RATES; r++) {
			old_power->txpwr_band_max[r] = power.user_limit[r];
			old_power->txpwr_limit[r] = power.reg_limit[r];
			old_power->txpwr_target[band_idx][r] = power.target[r];
			if (CHSPEC_IS2G(power.chanspec))
				old_power->txpwr_bphy_cck_max[r] =
				    power.board_limit[r];
			else
				old_power->txpwr_aphy_max[r] =
				    power.board_limit[r];
		}
	}

	return 0;
}
#endif				/* defined(BCMDBG) */

static u32 wlc_watchdog_backup_bi(struct wlc_info *wlc)
{
	u32 bi;
	bi = 2 * wlc->cfg->current_bss->dtim_period *
	    wlc->cfg->current_bss->beacon_period;
	if (wlc->bcn_li_dtim)
		bi *= wlc->bcn_li_dtim;
	else if (wlc->bcn_li_bcn)
		/* recalculate bi based on bcn_li_bcn */
		bi = 2 * wlc->bcn_li_bcn * wlc->cfg->current_bss->beacon_period;

	if (bi < 2 * TIMER_INTERVAL_WATCHDOG)
		bi = 2 * TIMER_INTERVAL_WATCHDOG;
	return bi;
}

/* Change to run the watchdog either from a periodic timer or from tbtt handler.
 * Call watchdog from tbtt handler if tbtt is true, watchdog timer otherwise.
 */
void wlc_watchdog_upd(struct wlc_info *wlc, bool tbtt)
{
	/* make sure changing watchdog driver is allowed */
	if (!wlc->pub->up || !wlc->pub->align_wd_tbtt)
		return;
	if (!tbtt && wlc->WDarmed) {
		wl_del_timer(wlc->wl, wlc->wdtimer);
		wlc->WDarmed = false;
	}

	/* stop watchdog timer and use tbtt interrupt to drive watchdog */
	if (tbtt && wlc->WDarmed) {
		wl_del_timer(wlc->wl, wlc->wdtimer);
		wlc->WDarmed = false;
		wlc->WDlast = OSL_SYSUPTIME();
	}
	/* arm watchdog timer and drive the watchdog there */
	else if (!tbtt && !wlc->WDarmed) {
		wl_add_timer(wlc->wl, wlc->wdtimer, TIMER_INTERVAL_WATCHDOG,
			     true);
		wlc->WDarmed = true;
	}
	if (tbtt && !wlc->WDarmed) {
		wl_add_timer(wlc->wl, wlc->wdtimer, wlc_watchdog_backup_bi(wlc),
			     true);
		wlc->WDarmed = true;
	}
}

ratespec_t wlc_lowest_basic_rspec(struct wlc_info *wlc, wlc_rateset_t *rs)
{
	ratespec_t lowest_basic_rspec;
	uint i;

	/* Use the lowest basic rate */
	lowest_basic_rspec = rs->rates[0] & RATE_MASK;
	for (i = 0; i < rs->count; i++) {
		if (rs->rates[i] & WLC_RATE_FLAG) {
			lowest_basic_rspec = rs->rates[i] & RATE_MASK;
			break;
		}
	}
#if NCONF
	/* pick siso/cdd as default for OFDM (note no basic rate MCSs are supported yet) */
	if (IS_OFDM(lowest_basic_rspec)) {
		lowest_basic_rspec |= (wlc->stf->ss_opmode << RSPEC_STF_SHIFT);
	}
#endif

	return lowest_basic_rspec;
}

/* This function changes the phytxctl for beacon based on current beacon ratespec AND txant
 * setting as per this table:
 *  ratespec     CCK		ant = wlc->stf->txant
 *  		OFDM		ant = 3
 */
void wlc_beacon_phytxctl_txant_upd(struct wlc_info *wlc, ratespec_t bcn_rspec)
{
	u16 phyctl;
	u16 phytxant = wlc->stf->phytxant;
	u16 mask = PHY_TXC_ANT_MASK;

	/* for non-siso rates or default setting, use the available chains */
	if (WLC_PHY_11N_CAP(wlc->band)) {
		phytxant = wlc_stf_phytxchain_sel(wlc, bcn_rspec);
	}

	phyctl = wlc_read_shm(wlc, M_BCN_PCTLWD);
	phyctl = (phyctl & ~mask) | phytxant;
	wlc_write_shm(wlc, M_BCN_PCTLWD, phyctl);
}

/* centralized protection config change function to simplify debugging, no consistency checking
 * this should be called only on changes to avoid overhead in periodic function
*/
void wlc_protection_upd(struct wlc_info *wlc, uint idx, int val)
{
	WL_TRACE(("wlc_protection_upd: idx %d, val %d\n", idx, val));

	switch (idx) {
	case WLC_PROT_G_SPEC:
		wlc->protection->_g = (bool) val;
		break;
	case WLC_PROT_G_OVR:
		wlc->protection->g_override = (s8) val;
		break;
	case WLC_PROT_G_USER:
		wlc->protection->gmode_user = (u8) val;
		break;
	case WLC_PROT_OVERLAP:
		wlc->protection->overlap = (s8) val;
		break;
	case WLC_PROT_N_USER:
		wlc->protection->nmode_user = (s8) val;
		break;
	case WLC_PROT_N_CFG:
		wlc->protection->n_cfg = (s8) val;
		break;
	case WLC_PROT_N_CFG_OVR:
		wlc->protection->n_cfg_override = (s8) val;
		break;
	case WLC_PROT_N_NONGF:
		wlc->protection->nongf = (bool) val;
		break;
	case WLC_PROT_N_NONGF_OVR:
		wlc->protection->nongf_override = (s8) val;
		break;
	case WLC_PROT_N_PAM_OVR:
		wlc->protection->n_pam_override = (s8) val;
		break;
	case WLC_PROT_N_OBSS:
		wlc->protection->n_obss = (bool) val;
		break;

	default:
		ASSERT(0);
		break;
	}

}

static void wlc_ht_update_sgi_rx(struct wlc_info *wlc, int val)
{
	wlc->ht_cap.cap &= ~(HT_CAP_SHORT_GI_20 | HT_CAP_SHORT_GI_40);
	wlc->ht_cap.cap |= (val & WLC_N_SGI_20) ? HT_CAP_SHORT_GI_20 : 0;
	wlc->ht_cap.cap |= (val & WLC_N_SGI_40) ? HT_CAP_SHORT_GI_40 : 0;

	if (wlc->pub->up) {
		wlc_update_beacon(wlc);
		wlc_update_probe_resp(wlc, true);
	}
}

static void wlc_ht_update_ldpc(struct wlc_info *wlc, s8 val)
{
	wlc->stf->ldpc = val;

	wlc->ht_cap.cap &= ~HT_CAP_LDPC_CODING;
	if (wlc->stf->ldpc != OFF)
		wlc->ht_cap.cap |= HT_CAP_LDPC_CODING;

	if (wlc->pub->up) {
		wlc_update_beacon(wlc);
		wlc_update_probe_resp(wlc, true);
		wlc_phy_ldpc_override_set(wlc->band->pi, (val ? true : false));
	}
}

/*
 * ucode, hwmac update
 *    Channel dependent updates for ucode and hw
 */
static void wlc_ucode_mac_upd(struct wlc_info *wlc)
{
	/* enable or disable any active IBSSs depending on whether or not
	 * we are on the home channel
	 */
	if (wlc->home_chanspec == WLC_BAND_PI_RADIO_CHANSPEC) {
		if (wlc->pub->associated) {
			/* BMAC_NOTE: This is something that should be fixed in ucode inits.
			 * I think that the ucode inits set up the bcn templates and shm values
			 * with a bogus beacon. This should not be done in the inits. If ucode needs
			 * to set up a beacon for testing, the test routines should write it down,
			 * not expect the inits to populate a bogus beacon.
			 */
			if (WLC_PHY_11N_CAP(wlc->band)) {
				wlc_write_shm(wlc, M_BCN_TXTSF_OFFSET,
					      wlc->band->bcntsfoff);
			}
		}
	} else {
		/* disable an active IBSS if we are not on the home channel */
	}

	/* update the various promisc bits */
	wlc_mac_bcn_promisc(wlc);
	wlc_mac_promisc(wlc);
}

static void wlc_bandinit_ordered(struct wlc_info *wlc, chanspec_t chanspec)
{
	wlc_rateset_t default_rateset;
	uint parkband;
	uint i, band_order[2];

	WL_TRACE(("wl%d: wlc_bandinit_ordered\n", wlc->pub->unit));
	/*
	 * We might have been bandlocked during down and the chip power-cycled (hibernate).
	 * figure out the right band to park on
	 */
	if (wlc->bandlocked || NBANDS(wlc) == 1) {
		ASSERT(CHSPEC_WLCBANDUNIT(chanspec) == wlc->band->bandunit);

		parkband = wlc->band->bandunit;	/* updated in wlc_bandlock() */
		band_order[0] = band_order[1] = parkband;
	} else {
		/* park on the band of the specified chanspec */
		parkband = CHSPEC_WLCBANDUNIT(chanspec);

		/* order so that parkband initialize last */
		band_order[0] = parkband ^ 1;
		band_order[1] = parkband;
	}

	/* make each band operational, software state init */
	for (i = 0; i < NBANDS(wlc); i++) {
		uint j = band_order[i];

		wlc->band = wlc->bandstate[j];

		wlc_default_rateset(wlc, &default_rateset);

		/* fill in hw_rate */
		wlc_rateset_filter(&default_rateset, &wlc->band->hw_rateset,
				   false, WLC_RATES_CCK_OFDM, RATE_MASK,
				   (bool) N_ENAB(wlc->pub));

		/* init basic rate lookup */
		wlc_rate_lookup_init(wlc, &default_rateset);
	}

	/* sync up phy/radio chanspec */
	wlc_set_phy_chanspec(wlc, chanspec);
}

/* band-specific init */
static void WLBANDINITFN(wlc_bsinit) (struct wlc_info *wlc)
{
	WL_TRACE(("wl%d: wlc_bsinit: bandunit %d\n", wlc->pub->unit,
		  wlc->band->bandunit));

	/* write ucode ACK/CTS rate table */
	wlc_set_ratetable(wlc);

	/* update some band specific mac configuration */
	wlc_ucode_mac_upd(wlc);

	/* init antenna selection */
	if (WLANTSEL_ENAB(wlc))
		wlc_antsel_init(wlc->asi);

}

/* switch to and initialize new band */
static void WLBANDINITFN(wlc_setband) (struct wlc_info *wlc, uint bandunit)
{
	int idx;
	wlc_bsscfg_t *cfg;

	ASSERT(NBANDS(wlc) > 1);
	ASSERT(!wlc->bandlocked);
	ASSERT(bandunit != wlc->band->bandunit || wlc->bandinit_pending);

	wlc->band = wlc->bandstate[bandunit];

	if (!wlc->pub->up)
		return;

	/* wait for at least one beacon before entering sleeping state */
	wlc->PMawakebcn = true;
	FOREACH_AS_STA(wlc, idx, cfg)
	    cfg->PMawakebcn = true;
	wlc_set_ps_ctrl(wlc);

	/* band-specific initializations */
	wlc_bsinit(wlc);
}

/* Initialize a WME Parameter Info Element with default STA parameters from WMM Spec, Table 12 */
void wlc_wme_initparams_sta(struct wlc_info *wlc, wme_param_ie_t *pe)
{
	static const wme_param_ie_t stadef = {
		WME_OUI,
		WME_TYPE,
		WME_SUBTYPE_PARAM_IE,
		WME_VER,
		0,
		0,
		{
		 {EDCF_AC_BE_ACI_STA, EDCF_AC_BE_ECW_STA,
		  HTOL16(EDCF_AC_BE_TXOP_STA)},
		 {EDCF_AC_BK_ACI_STA, EDCF_AC_BK_ECW_STA,
		  HTOL16(EDCF_AC_BK_TXOP_STA)},
		 {EDCF_AC_VI_ACI_STA, EDCF_AC_VI_ECW_STA,
		  HTOL16(EDCF_AC_VI_TXOP_STA)},
		 {EDCF_AC_VO_ACI_STA, EDCF_AC_VO_ECW_STA,
		  HTOL16(EDCF_AC_VO_TXOP_STA)}
		 }
	};

	ASSERT(sizeof(*pe) == WME_PARAM_IE_LEN);
	memcpy(pe, &stadef, sizeof(*pe));
}

void wlc_wme_setparams(struct wlc_info *wlc, u16 aci, void *arg, bool suspend)
{
	int i;
	shm_acparams_t acp_shm;
	u16 *shm_entry;
	struct ieee80211_tx_queue_params *params = arg;

	ASSERT(wlc);

	/* Only apply params if the core is out of reset and has clocks */
	if (!wlc->clk) {
		WL_ERROR(("wl%d: %s : no-clock\n", wlc->pub->unit, __func__));
		return;
	}

	/*
	 * AP uses AC params from wme_param_ie_ap.
	 * AP advertises AC params from wme_param_ie.
	 * STA uses AC params from wme_param_ie.
	 */

	wlc->wme_admctl = 0;

	do {
		memset((char *)&acp_shm, 0, sizeof(shm_acparams_t));
		/* find out which ac this set of params applies to */
		ASSERT(aci < AC_COUNT);
		/* set the admission control policy for this AC */
		/* wlc->wme_admctl |= 1 << aci; *//* should be set ??  seems like off by default */

		/* fill in shm ac params struct */
		acp_shm.txop = ltoh16(params->txop);
		/* convert from units of 32us to us for ucode */
		wlc->edcf_txop[aci & 0x3] = acp_shm.txop =
		    EDCF_TXOP2USEC(acp_shm.txop);
		acp_shm.aifs = (params->aifs & EDCF_AIFSN_MASK);

		if (aci == AC_VI && acp_shm.txop == 0
		    && acp_shm.aifs < EDCF_AIFSN_MAX)
			acp_shm.aifs++;

		if (acp_shm.aifs < EDCF_AIFSN_MIN
		    || acp_shm.aifs > EDCF_AIFSN_MAX) {
			WL_ERROR(("wl%d: wlc_edcf_setparams: bad aifs %d\n",
				  wlc->pub->unit, acp_shm.aifs));
			continue;
		}

		acp_shm.cwmin = params->cw_min;
		acp_shm.cwmax = params->cw_max;
		acp_shm.cwcur = acp_shm.cwmin;
		acp_shm.bslots =
		    R_REG(wlc->osh, &wlc->regs->tsf_random) & acp_shm.cwcur;
		acp_shm.reggap = acp_shm.bslots + acp_shm.aifs;
		/* Indicate the new params to the ucode */
		acp_shm.status = wlc_read_shm(wlc, (M_EDCF_QINFO +
						    wme_shmemacindex(aci) *
						    M_EDCF_QLEN +
						    M_EDCF_STATUS_OFF));
		acp_shm.status |= WME_STATUS_NEWAC;

		/* Fill in shm acparam table */
		shm_entry = (u16 *) &acp_shm;
		for (i = 0; i < (int)sizeof(shm_acparams_t); i += 2)
			wlc_write_shm(wlc,
				      M_EDCF_QINFO +
				      wme_shmemacindex(aci) * M_EDCF_QLEN + i,
				      *shm_entry++);

	} while (0);

	if (suspend)
		wlc_suspend_mac_and_wait(wlc);

	if (suspend)
		wlc_enable_mac(wlc);

}

void wlc_edcf_setparams(wlc_bsscfg_t *cfg, bool suspend)
{
	struct wlc_info *wlc = cfg->wlc;
	uint aci, i, j;
	edcf_acparam_t *edcf_acp;
	shm_acparams_t acp_shm;
	u16 *shm_entry;

	ASSERT(cfg);
	ASSERT(wlc);

	/* Only apply params if the core is out of reset and has clocks */
	if (!wlc->clk)
		return;

	/*
	 * AP uses AC params from wme_param_ie_ap.
	 * AP advertises AC params from wme_param_ie.
	 * STA uses AC params from wme_param_ie.
	 */

	edcf_acp = (edcf_acparam_t *) &wlc->wme_param_ie.acparam[0];

	wlc->wme_admctl = 0;

	for (i = 0; i < AC_COUNT; i++, edcf_acp++) {
		memset((char *)&acp_shm, 0, sizeof(shm_acparams_t));
		/* find out which ac this set of params applies to */
		aci = (edcf_acp->ACI & EDCF_ACI_MASK) >> EDCF_ACI_SHIFT;
		ASSERT(aci < AC_COUNT);
		/* set the admission control policy for this AC */
		if (edcf_acp->ACI & EDCF_ACM_MASK) {
			wlc->wme_admctl |= 1 << aci;
		}

		/* fill in shm ac params struct */
		acp_shm.txop = ltoh16(edcf_acp->TXOP);
		/* convert from units of 32us to us for ucode */
		wlc->edcf_txop[aci] = acp_shm.txop =
		    EDCF_TXOP2USEC(acp_shm.txop);
		acp_shm.aifs = (edcf_acp->ACI & EDCF_AIFSN_MASK);

		if (aci == AC_VI && acp_shm.txop == 0
		    && acp_shm.aifs < EDCF_AIFSN_MAX)
			acp_shm.aifs++;

		if (acp_shm.aifs < EDCF_AIFSN_MIN
		    || acp_shm.aifs > EDCF_AIFSN_MAX) {
			WL_ERROR(("wl%d: wlc_edcf_setparams: bad aifs %d\n",
				  wlc->pub->unit, acp_shm.aifs));
			continue;
		}

		/* CWmin = 2^(ECWmin) - 1 */
		acp_shm.cwmin = EDCF_ECW2CW(edcf_acp->ECW & EDCF_ECWMIN_MASK);
		/* CWmax = 2^(ECWmax) - 1 */
		acp_shm.cwmax = EDCF_ECW2CW((edcf_acp->ECW & EDCF_ECWMAX_MASK)
					    >> EDCF_ECWMAX_SHIFT);
		acp_shm.cwcur = acp_shm.cwmin;
		acp_shm.bslots =
		    R_REG(wlc->osh, &wlc->regs->tsf_random) & acp_shm.cwcur;
		acp_shm.reggap = acp_shm.bslots + acp_shm.aifs;
		/* Indicate the new params to the ucode */
		acp_shm.status = wlc_read_shm(wlc, (M_EDCF_QINFO +
						    wme_shmemacindex(aci) *
						    M_EDCF_QLEN +
						    M_EDCF_STATUS_OFF));
		acp_shm.status |= WME_STATUS_NEWAC;

		/* Fill in shm acparam table */
		shm_entry = (u16 *) &acp_shm;
		for (j = 0; j < (int)sizeof(shm_acparams_t); j += 2)
			wlc_write_shm(wlc,
				      M_EDCF_QINFO +
				      wme_shmemacindex(aci) * M_EDCF_QLEN + j,
				      *shm_entry++);
	}

	if (suspend)
		wlc_suspend_mac_and_wait(wlc);

	if (AP_ENAB(wlc->pub) && WME_ENAB(wlc->pub)) {
		wlc_update_beacon(wlc);
		wlc_update_probe_resp(wlc, false);
	}

	if (suspend)
		wlc_enable_mac(wlc);

}

bool wlc_timers_init(struct wlc_info *wlc, int unit)
{
	wlc->wdtimer = wl_init_timer(wlc->wl, wlc_watchdog_by_timer,
		wlc, "watchdog");
	if (!wlc->wdtimer) {
		WL_ERROR(("wl%d:  wl_init_timer for wdtimer failed\n", unit));
		goto fail;
	}

	wlc->radio_timer = wl_init_timer(wlc->wl, wlc_radio_timer,
		wlc, "radio");
	if (!wlc->radio_timer) {
		WL_ERROR(("wl%d:  wl_init_timer for radio_timer failed\n",
			  unit));
		goto fail;
	}

	return true;

 fail:
	return false;
}

/*
 * Initialize wlc_info default values ...
 * may get overrides later in this function
 */
void wlc_info_init(struct wlc_info *wlc, int unit)
{
	int i;
	/* Assume the device is there until proven otherwise */
	wlc->device_present = true;

	/* set default power output percentage to 100 percent */
	wlc->txpwr_percent = 100;

	/* Save our copy of the chanspec */
	wlc->chanspec = CH20MHZ_CHSPEC(1);

	/* initialize CCK preamble mode to unassociated state */
	wlc->shortpreamble = false;

	wlc->legacy_probe = true;

	/* various 802.11g modes */
	wlc->shortslot = false;
	wlc->shortslot_override = WLC_SHORTSLOT_AUTO;

	wlc->barker_overlap_control = true;
	wlc->barker_preamble = WLC_BARKER_SHORT_ALLOWED;
	wlc->txburst_limit_override = AUTO;

	wlc_protection_upd(wlc, WLC_PROT_G_OVR, WLC_PROTECTION_AUTO);
	wlc_protection_upd(wlc, WLC_PROT_G_SPEC, false);

	wlc_protection_upd(wlc, WLC_PROT_N_CFG_OVR, WLC_PROTECTION_AUTO);
	wlc_protection_upd(wlc, WLC_PROT_N_CFG, WLC_N_PROTECTION_OFF);
	wlc_protection_upd(wlc, WLC_PROT_N_NONGF_OVR, WLC_PROTECTION_AUTO);
	wlc_protection_upd(wlc, WLC_PROT_N_NONGF, false);
	wlc_protection_upd(wlc, WLC_PROT_N_PAM_OVR, AUTO);

	wlc_protection_upd(wlc, WLC_PROT_OVERLAP, WLC_PROTECTION_CTL_OVERLAP);

	/* 802.11g draft 4.0 NonERP elt advertisement */
	wlc->include_legacy_erp = true;

	wlc->stf->ant_rx_ovr = ANT_RX_DIV_DEF;
	wlc->stf->txant = ANT_TX_DEF;

	wlc->prb_resp_timeout = WLC_PRB_RESP_TIMEOUT;

	wlc->usr_fragthresh = DOT11_DEFAULT_FRAG_LEN;
	for (i = 0; i < NFIFO; i++)
		wlc->fragthresh[i] = DOT11_DEFAULT_FRAG_LEN;
	wlc->RTSThresh = DOT11_DEFAULT_RTS_LEN;

	/* default rate fallback retry limits */
	wlc->SFBL = RETRY_SHORT_FB;
	wlc->LFBL = RETRY_LONG_FB;

	/* default mac retry limits */
	wlc->SRL = RETRY_SHORT_DEF;
	wlc->LRL = RETRY_LONG_DEF;

	/* init PM state */
	wlc->PM = PM_OFF;	/* User's setting of PM mode through IOCTL */
	wlc->PM_override = false;	/* Prevents from going to PM if our AP is 'ill' */
	wlc->PMenabled = false;	/* Current PM state */
	wlc->PMpending = false;	/* Tracks whether STA indicated PM in the last attempt */
	wlc->PMblocked = false;	/* To allow blocking going into PM during RM and scans */

	/* In WMM Auto mode, PM is allowed if association is a UAPSD association */
	wlc->WME_PM_blocked = false;

	/* Init wme queuing method */
	wlc->wme_prec_queuing = false;

	/* Overrides for the core to stay awake under zillion conditions Look for STAY_AWAKE */
	wlc->wake = false;
	/* Are we waiting for a response to PS-Poll that we sent */
	wlc->PSpoll = false;

	/* APSD defaults */
	wlc->wme_apsd = true;
	wlc->apsd_sta_usp = false;
	wlc->apsd_trigger_timeout = 0;	/* disable the trigger timer */
	wlc->apsd_trigger_ac = AC_BITMAP_ALL;

	/* Set flag to indicate that hw keys should be used when available. */
	wlc->wsec_swkeys = false;

	/* init the 4 static WEP default keys */
	for (i = 0; i < WSEC_MAX_DEFAULT_KEYS; i++) {
		wlc->wsec_keys[i] = wlc->wsec_def_keys[i];
		wlc->wsec_keys[i]->idx = (u8) i;
	}

	wlc->_regulatory_domain = false;	/* 802.11d */

	/* WME QoS mode is Auto by default */
	wlc->pub->_wme = AUTO;

#ifdef BCMSDIODEV_ENABLED
	wlc->pub->_priofc = true;	/* enable priority flow control for sdio dongle */
#endif

	wlc->pub->_ampdu = AMPDU_AGG_HOST;
	wlc->pub->bcmerror = 0;
	wlc->ibss_allowed = true;
	wlc->ibss_coalesce_allowed = true;
	wlc->pub->_coex = ON;

	/* intialize mpc delay */
	wlc->mpc_delay_off = wlc->mpc_dlycnt = WLC_MPC_MIN_DELAYCNT;

	wlc->pr80838_war = true;
}

static bool wlc_state_bmac_sync(struct wlc_info *wlc)
{
	wlc_bmac_state_t state_bmac;

	if (wlc_bmac_state_get(wlc->hw, &state_bmac) != 0)
		return false;

	wlc->machwcap = state_bmac.machwcap;
	wlc_protection_upd(wlc, WLC_PROT_N_PAM_OVR,
			   (s8) state_bmac.preamble_ovr);

	return true;
}

static uint wlc_attach_module(struct wlc_info *wlc)
{
	uint err = 0;
	uint unit;
	unit = wlc->pub->unit;

	wlc->asi = wlc_antsel_attach(wlc, wlc->osh, wlc->pub, wlc->hw);
	if (wlc->asi == NULL) {
		WL_ERROR(("wl%d: wlc_attach: wlc_antsel_attach failed\n",
			  unit));
		err = 44;
		goto fail;
	}

	wlc->ampdu = wlc_ampdu_attach(wlc);
	if (wlc->ampdu == NULL) {
		WL_ERROR(("wl%d: wlc_attach: wlc_ampdu_attach failed\n", unit));
		err = 50;
		goto fail;
	}

	/* Initialize event queue; needed before following calls */
	wlc->eventq =
	    wlc_eventq_attach(wlc->pub, wlc, wlc->wl, wlc_process_eventq);
	if (wlc->eventq == NULL) {
		WL_ERROR(("wl%d: wlc_attach: wlc_eventq_attachfailed\n", unit));
		err = 57;
		goto fail;
	}

	if ((wlc_stf_attach(wlc) != 0)) {
		WL_ERROR(("wl%d: wlc_attach: wlc_stf_attach failed\n", unit));
		err = 68;
		goto fail;
	}
 fail:
	return err;
}

wlc_pub_t *wlc_pub(void *wlc)
{
	return ((struct wlc_info *) wlc)->pub;
}

#define CHIP_SUPPORTS_11N(wlc) 	1

/*
 * The common driver entry routine. Error codes should be unique
 */
void *wlc_attach(void *wl, u16 vendor, u16 device, uint unit, bool piomode,
		 struct osl_info *osh, void *regsva, uint bustype,
		 void *btparam, uint *perr)
{
	struct wlc_info *wlc;
	uint err = 0;
	uint j;
	wlc_pub_t *pub;
	wlc_txq_info_t *qi;
	uint n_disabled;

	WL_NONE(("wl%d: %s: vendor 0x%x device 0x%x\n", unit, __func__, vendor,
		 device));

	ASSERT(WSEC_MAX_RCMTA_KEYS <= WSEC_MAX_KEYS);
	ASSERT(WSEC_MAX_DEFAULT_KEYS == WLC_DEFAULT_KEYS);

	/* some code depends on packed structures */
	ASSERT(sizeof(struct ether_addr) == ETHER_ADDR_LEN);
	ASSERT(sizeof(struct ether_header) == ETHER_HDR_LEN);
	ASSERT(sizeof(d11regs_t) == SI_CORE_SIZE);
	ASSERT(sizeof(ofdm_phy_hdr_t) == D11_PHY_HDR_LEN);
	ASSERT(sizeof(cck_phy_hdr_t) == D11_PHY_HDR_LEN);
	ASSERT(sizeof(d11txh_t) == D11_TXH_LEN);
	ASSERT(sizeof(d11rxhdr_t) == RXHDR_LEN);
	ASSERT(sizeof(struct dot11_header) == DOT11_A4_HDR_LEN);
	ASSERT(sizeof(struct dot11_rts_frame) == DOT11_RTS_LEN);
	ASSERT(sizeof(struct dot11_management_header) == DOT11_MGMT_HDR_LEN);
	ASSERT(sizeof(struct dot11_bcn_prb) == DOT11_BCN_PRB_LEN);
	ASSERT(sizeof(tx_status_t) == TXSTATUS_LEN);
	ASSERT(sizeof(ht_cap_ie_t) == HT_CAP_IE_LEN);
#ifdef BRCM_FULLMAC
	ASSERT(offsetof(wl_scan_params_t, channel_list) ==
	       WL_SCAN_PARAMS_FIXED_SIZE);
#endif
	ASSERT(IS_ALIGNED(offsetof(wsec_key_t, data), sizeof(u32)));
	ASSERT(ISPOWEROF2(MA_WINDOW_SZ));

	ASSERT(sizeof(wlc_d11rxhdr_t) <= WL_HWRXOFF);

	/*
	 * Number of replay counters value used in WPA IE must match # rxivs
	 * supported in wsec_key_t struct. See 802.11i/D3.0 sect. 7.3.2.17
	 * 'RSN Information Element' figure 8 for this mapping.
	 */
	ASSERT((WPA_CAP_16_REPLAY_CNTRS == WLC_REPLAY_CNTRS_VALUE
		&& 16 == WLC_NUMRXIVS)
	       || (WPA_CAP_4_REPLAY_CNTRS == WLC_REPLAY_CNTRS_VALUE
		   && 4 == WLC_NUMRXIVS));

	/* allocate struct wlc_info state and its substructures */
	wlc = (struct wlc_info *) wlc_attach_malloc(osh, unit, &err, device);
	if (wlc == NULL)
		goto fail;
	wlc->osh = osh;
	pub = wlc->pub;

#if defined(BCMDBG)
	wlc_info_dbg = wlc;
#endif

	wlc->band = wlc->bandstate[0];
	wlc->core = wlc->corestate;
	wlc->wl = wl;
	pub->unit = unit;
	pub->osh = osh;
	wlc->btparam = btparam;
	pub->_piomode = piomode;
	wlc->bandinit_pending = false;
	/* By default restrict TKIP associations from 11n STA's */
	wlc->ht_wsec_restriction = WLC_HT_TKIP_RESTRICT;

	/* populate struct wlc_info with default values  */
	wlc_info_init(wlc, unit);

	/* update sta/ap related parameters */
	wlc_ap_upd(wlc);

	/* 11n_disable nvram */
	n_disabled = getintvar(pub->vars, "11n_disable");

	/* register a module (to handle iovars) */
	wlc_module_register(wlc->pub, wlc_iovars, "wlc_iovars", wlc,
			    wlc_doiovar, NULL, NULL);

	/* low level attach steps(all hw accesses go inside, no more in rest of the attach) */
	err = wlc_bmac_attach(wlc, vendor, device, unit, piomode, osh, regsva,
			      bustype, btparam);
	if (err)
		goto fail;

	/* for some states, due to different info pointer(e,g, wlc, wlc_hw) or master/slave split,
	 * HIGH driver(both monolithic and HIGH_ONLY) needs to sync states FROM BMAC portion driver
	 */
	if (!wlc_state_bmac_sync(wlc)) {
		err = 20;
		goto fail;
	}

	pub->phy_11ncapable = WLC_PHY_11N_CAP(wlc->band);

	/* propagate *vars* from BMAC driver to high driver */
	wlc_bmac_copyfrom_vars(wlc->hw, &pub->vars, &wlc->vars_size);


	/* set maximum allowed duty cycle */
	wlc->tx_duty_cycle_ofdm =
	    (u16) getintvar(pub->vars, "tx_duty_cycle_ofdm");
	wlc->tx_duty_cycle_cck =
	    (u16) getintvar(pub->vars, "tx_duty_cycle_cck");

	wlc_stf_phy_chain_calc(wlc);

	/* txchain 1: txant 0, txchain 2: txant 1 */
	if (WLCISNPHY(wlc->band) && (wlc->stf->txstreams == 1))
		wlc->stf->txant = wlc->stf->hw_txchain - 1;

	/* push to BMAC driver */
	wlc_phy_stf_chain_init(wlc->band->pi, wlc->stf->hw_txchain,
			       wlc->stf->hw_rxchain);

	/* pull up some info resulting from the low attach */
	{
		int i;
		for (i = 0; i < NFIFO; i++)
			wlc->core->txavail[i] = wlc->hw->txavail[i];
	}

	wlc_bmac_hw_etheraddr(wlc->hw, &wlc->perm_etheraddr);

	bcopy((char *)&wlc->perm_etheraddr, (char *)&pub->cur_etheraddr,
	      ETHER_ADDR_LEN);

	for (j = 0; j < NBANDS(wlc); j++) {
		/* Use band 1 for single band 11a */
		if (IS_SINGLEBAND_5G(wlc->deviceid))
			j = BAND_5G_INDEX;

		wlc->band = wlc->bandstate[j];

		if (!wlc_attach_stf_ant_init(wlc)) {
			err = 24;
			goto fail;
		}

		/* default contention windows size limits */
		wlc->band->CWmin = APHY_CWMIN;
		wlc->band->CWmax = PHY_CWMAX;

		/* init gmode value */
		if (BAND_2G(wlc->band->bandtype)) {
			wlc->band->gmode = GMODE_AUTO;
			wlc_protection_upd(wlc, WLC_PROT_G_USER,
					   wlc->band->gmode);
		}

		/* init _n_enab supported mode */
		if (WLC_PHY_11N_CAP(wlc->band) && CHIP_SUPPORTS_11N(wlc)) {
			if (n_disabled & WLFEATURE_DISABLE_11N) {
				pub->_n_enab = OFF;
				wlc_protection_upd(wlc, WLC_PROT_N_USER, OFF);
			} else {
				pub->_n_enab = SUPPORT_11N;
				wlc_protection_upd(wlc, WLC_PROT_N_USER,
						   ((pub->_n_enab ==
						     SUPPORT_11N) ? WL_11N_2x2 :
						    WL_11N_3x3));
			}
		}

		/* init per-band default rateset, depend on band->gmode */
		wlc_default_rateset(wlc, &wlc->band->defrateset);

		/* fill in hw_rateset (used early by WLC_SET_RATESET) */
		wlc_rateset_filter(&wlc->band->defrateset,
				   &wlc->band->hw_rateset, false,
				   WLC_RATES_CCK_OFDM, RATE_MASK,
				   (bool) N_ENAB(wlc->pub));
	}

	/* update antenna config due to wlc->stf->txant/txchain/ant_rx_ovr change */
	wlc_stf_phy_txant_upd(wlc);

	/* attach each modules */
	err = wlc_attach_module(wlc);
	if (err != 0)
		goto fail;

	if (!wlc_timers_init(wlc, unit)) {
		WL_ERROR(("wl%d: %s: wlc_init_timer failed\n", unit, __func__));
		err = 32;
		goto fail;
	}

	/* depend on rateset, gmode */
	wlc->cmi = wlc_channel_mgr_attach(wlc);
	if (!wlc->cmi) {
		WL_ERROR(("wl%d: %s: wlc_channel_mgr_attach failed\n", unit,
			  __func__));
		err = 33;
		goto fail;
	}

	/* init default when all parameters are ready, i.e. ->rateset */
	wlc_bss_default_init(wlc);

	/*
	 * Complete the wlc default state initializations..
	 */

	/* allocate our initial queue */
	qi = wlc_txq_alloc(wlc, osh);
	if (qi == NULL) {
		WL_ERROR(("wl%d: %s: failed to malloc tx queue\n", unit,
			  __func__));
		err = 100;
		goto fail;
	}
	wlc->active_queue = qi;

	wlc->bsscfg[0] = wlc->cfg;
	wlc->cfg->_idx = 0;
	wlc->cfg->wlc = wlc;
	pub->txmaxpkts = MAXTXPKTS;

	WLCNTSET(pub->_cnt->version, WL_CNT_T_VERSION);
	WLCNTSET(pub->_cnt->length, sizeof(wl_cnt_t));

	WLCNTSET(pub->_wme_cnt->version, WL_WME_CNT_VERSION);
	WLCNTSET(pub->_wme_cnt->length, sizeof(wl_wme_cnt_t));

	wlc_wme_initparams_sta(wlc, &wlc->wme_param_ie);

	wlc->mimoft = FT_HT;
	wlc->ht_cap.cap = HT_CAP;
	if (HT_ENAB(wlc->pub))
		wlc->stf->ldpc = AUTO;

	wlc->mimo_40txbw = AUTO;
	wlc->ofdm_40txbw = AUTO;
	wlc->cck_40txbw = AUTO;
	wlc_update_mimo_band_bwcap(wlc, WLC_N_BW_20IN2G_40IN5G);

	/* Enable setting the RIFS Mode bit by default in HT Info IE */
	wlc->rifs_advert = AUTO;

	/* Set default values of SGI */
	if (WLC_SGI_CAP_PHY(wlc)) {
		wlc_ht_update_sgi_rx(wlc, (WLC_N_SGI_20 | WLC_N_SGI_40));
		wlc->sgi_tx = AUTO;
	} else if (WLCISSSLPNPHY(wlc->band)) {
		wlc_ht_update_sgi_rx(wlc, (WLC_N_SGI_20 | WLC_N_SGI_40));
		wlc->sgi_tx = AUTO;
	} else {
		wlc_ht_update_sgi_rx(wlc, 0);
		wlc->sgi_tx = OFF;
	}

	/* *******nvram 11n config overrides Start ********* */

	/* apply the sgi override from nvram conf */
	if (n_disabled & WLFEATURE_DISABLE_11N_SGI_TX)
		wlc->sgi_tx = OFF;

	if (n_disabled & WLFEATURE_DISABLE_11N_SGI_RX)
		wlc_ht_update_sgi_rx(wlc, 0);

	/* apply the stbc override from nvram conf */
	if (n_disabled & WLFEATURE_DISABLE_11N_STBC_TX) {
		wlc->bandstate[BAND_2G_INDEX]->band_stf_stbc_tx = OFF;
		wlc->bandstate[BAND_5G_INDEX]->band_stf_stbc_tx = OFF;
		wlc->ht_cap.cap &= ~HT_CAP_TX_STBC;
	}
	if (n_disabled & WLFEATURE_DISABLE_11N_STBC_RX)
		wlc_stf_stbc_rx_set(wlc, HT_CAP_RX_STBC_NO);

	/* apply the GF override from nvram conf */
	if (n_disabled & WLFEATURE_DISABLE_11N_GF)
		wlc->ht_cap.cap &= ~HT_CAP_GF;

	/* initialize radio_mpc_disable according to wlc->mpc */
	wlc_radio_mpc_upd(wlc);

	if (WLANTSEL_ENAB(wlc)) {
		if ((wlc->pub->sih->chip) == BCM43235_CHIP_ID) {
			if ((getintvar(wlc->pub->vars, "aa2g") == 7) ||
			    (getintvar(wlc->pub->vars, "aa5g") == 7)) {
				wlc_bmac_antsel_set(wlc->hw, 1);
			}
		} else {
			wlc_bmac_antsel_set(wlc->hw, wlc->asi->antsel_avail);
		}
	}

	if (perr)
		*perr = 0;

	return (void *)wlc;

 fail:
	WL_ERROR(("wl%d: %s: failed with err %d\n", unit, __func__, err));
	if (wlc)
		wlc_detach(wlc);

	if (perr)
		*perr = err;
	return NULL;
}

static void wlc_attach_antgain_init(struct wlc_info *wlc)
{
	uint unit;
	unit = wlc->pub->unit;

	if ((wlc->band->antgain == -1) && (wlc->pub->sromrev == 1)) {
		/* default antenna gain for srom rev 1 is 2 dBm (8 qdbm) */
		wlc->band->antgain = 8;
	} else if (wlc->band->antgain == -1) {
		WL_ERROR(("wl%d: %s: Invalid antennas available in srom, using 2dB\n", unit, __func__));
		wlc->band->antgain = 8;
	} else {
		s8 gain, fract;
		/* Older sroms specified gain in whole dbm only.  In order
		 * be able to specify qdbm granularity and remain backward compatible
		 * the whole dbms are now encoded in only low 6 bits and remaining qdbms
		 * are encoded in the hi 2 bits. 6 bit signed number ranges from
		 * -32 - 31. Examples: 0x1 = 1 db,
		 * 0xc1 = 1.75 db (1 + 3 quarters),
		 * 0x3f = -1 (-1 + 0 quarters),
		 * 0x7f = -.75 (-1 in low 6 bits + 1 quarters in hi 2 bits) = -3 qdbm.
		 * 0xbf = -.50 (-1 in low 6 bits + 2 quarters in hi 2 bits) = -2 qdbm.
		 */
		gain = wlc->band->antgain & 0x3f;
		gain <<= 2;	/* Sign extend */
		gain >>= 2;
		fract = (wlc->band->antgain & 0xc0) >> 6;
		wlc->band->antgain = 4 * gain + fract;
	}
}

static bool wlc_attach_stf_ant_init(struct wlc_info *wlc)
{
	int aa;
	uint unit;
	char *vars;
	int bandtype;

	unit = wlc->pub->unit;
	vars = wlc->pub->vars;
	bandtype = wlc->band->bandtype;

	/* get antennas available */
	aa = (s8) getintvar(vars, (BAND_5G(bandtype) ? "aa5g" : "aa2g"));
	if (aa == 0)
		aa = (s8) getintvar(vars,
				      (BAND_5G(bandtype) ? "aa1" : "aa0"));
	if ((aa < 1) || (aa > 15)) {
		WL_ERROR(("wl%d: %s: Invalid antennas available in srom (0x%x), using 3.\n", unit, __func__, aa));
		aa = 3;
	}

	/* reset the defaults if we have a single antenna */
	if (aa == 1) {
		wlc->stf->ant_rx_ovr = ANT_RX_DIV_FORCE_0;
		wlc->stf->txant = ANT_TX_FORCE_0;
	} else if (aa == 2) {
		wlc->stf->ant_rx_ovr = ANT_RX_DIV_FORCE_1;
		wlc->stf->txant = ANT_TX_FORCE_1;
	} else {
	}

	/* Compute Antenna Gain */
	wlc->band->antgain =
	    (s8) getintvar(vars, (BAND_5G(bandtype) ? "ag1" : "ag0"));
	wlc_attach_antgain_init(wlc);

	return true;
}


static void wlc_timers_deinit(struct wlc_info *wlc)
{
	/* free timer state */
	if (wlc->wdtimer) {
		wl_free_timer(wlc->wl, wlc->wdtimer);
		wlc->wdtimer = NULL;
	}
	if (wlc->radio_timer) {
		wl_free_timer(wlc->wl, wlc->radio_timer);
		wlc->radio_timer = NULL;
	}
}

static void wlc_detach_module(struct wlc_info *wlc)
{
	if (wlc->asi) {
		wlc_antsel_detach(wlc->asi);
		wlc->asi = NULL;
	}

	if (wlc->ampdu) {
		wlc_ampdu_detach(wlc->ampdu);
		wlc->ampdu = NULL;
	}

	wlc_stf_detach(wlc);
}

/*
 * Return a count of the number of driver callbacks still pending.
 *
 * General policy is that wlc_detach can only dealloc/free software states. It can NOT
 *  touch hardware registers since the d11core may be in reset and clock may not be available.
 *    One exception is sb register access, which is possible if crystal is turned on
 * After "down" state, driver should avoid software timer with the exception of radio_monitor.
 */
uint wlc_detach(struct wlc_info *wlc)
{
	uint i;
	uint callbacks = 0;

	if (wlc == NULL)
		return 0;

	WL_TRACE(("wl%d: %s\n", wlc->pub->unit, __func__));

	ASSERT(!wlc->pub->up);

	callbacks += wlc_bmac_detach(wlc);

	/* delete software timers */
	if (!wlc_radio_monitor_stop(wlc))
		callbacks++;

	if (wlc->eventq) {
		wlc_eventq_detach(wlc->eventq);
		wlc->eventq = NULL;
	}

	wlc_channel_mgr_detach(wlc->cmi);

	wlc_timers_deinit(wlc);

	wlc_detach_module(wlc);

	/* free other state */


#ifdef BCMDBG
	if (wlc->country_ie_override) {
		kfree(wlc->country_ie_override);
		wlc->country_ie_override = NULL;
	}
#endif				/* BCMDBG */

	{
		/* free dumpcb list */
		dumpcb_t *prev, *ptr;
		prev = ptr = wlc->dumpcb_head;
		while (ptr) {
			ptr = prev->next;
			kfree(prev);
			prev = ptr;
		}
		wlc->dumpcb_head = NULL;
	}

	/* Detach from iovar manager */
	wlc_module_unregister(wlc->pub, "wlc_iovars", wlc);

	/*
	   if (wlc->ap) {
	   wlc_ap_detach(wlc->ap);
	   wlc->ap = NULL;
	   }
	 */

	while (wlc->tx_queues != NULL) {
		wlc_txq_free(wlc, wlc->osh, wlc->tx_queues);
	}

	/*
	 * consistency check: wlc_module_register/wlc_module_unregister calls
	 * should match therefore nothing should be left here.
	 */
	for (i = 0; i < WLC_MAXMODULES; i++)
		ASSERT(wlc->modulecb[i].name[0] == '\0');

	wlc_detach_mfree(wlc, wlc->osh);
	return callbacks;
}

/* update state that depends on the current value of "ap" */
void wlc_ap_upd(struct wlc_info *wlc)
{
	if (AP_ENAB(wlc->pub))
		wlc->PLCPHdr_override = WLC_PLCP_AUTO;	/* AP: short not allowed, but not enforced */
	else
		wlc->PLCPHdr_override = WLC_PLCP_SHORT;	/* STA-BSS; short capable */

	/* disable vlan_mode on AP since some legacy STAs cannot rx tagged pkts */
	wlc->vlan_mode = AP_ENAB(wlc->pub) ? OFF : AUTO;

	/* fixup mpc */
	wlc->mpc = true;
}

/* read hwdisable state and propagate to wlc flag */
static void wlc_radio_hwdisable_upd(struct wlc_info *wlc)
{
	if (wlc->pub->wlfeatureflag & WL_SWFL_NOHWRADIO || wlc->pub->hw_off)
		return;

	if (wlc_bmac_radio_read_hwdisabled(wlc->hw)) {
		mboolset(wlc->pub->radio_disabled, WL_RADIO_HW_DISABLE);
	} else {
		mboolclr(wlc->pub->radio_disabled, WL_RADIO_HW_DISABLE);
	}
}

/* return true if Minimum Power Consumption should be entered, false otherwise */
bool wlc_is_non_delay_mpc(struct wlc_info *wlc)
{
	return false;
}

bool wlc_ismpc(struct wlc_info *wlc)
{
	return (wlc->mpc_delay_off == 0) && (wlc_is_non_delay_mpc(wlc));
}

void wlc_radio_mpc_upd(struct wlc_info *wlc)
{
	bool mpc_radio, radio_state;

	/*
	 * Clear the WL_RADIO_MPC_DISABLE bit when mpc feature is disabled
	 * in case the WL_RADIO_MPC_DISABLE bit was set. Stop the radio
	 * monitor also when WL_RADIO_MPC_DISABLE is the only reason that
	 * the radio is going down.
	 */
	if (!wlc->mpc) {
		if (!wlc->pub->radio_disabled)
			return;
		mboolclr(wlc->pub->radio_disabled, WL_RADIO_MPC_DISABLE);
		wlc_radio_upd(wlc);
		if (!wlc->pub->radio_disabled)
			wlc_radio_monitor_stop(wlc);
		return;
	}

	/*
	 * sync ismpc logic with WL_RADIO_MPC_DISABLE bit in wlc->pub->radio_disabled
	 * to go ON, always call radio_upd synchronously
	 * to go OFF, postpone radio_upd to later when context is safe(e.g. watchdog)
	 */
	radio_state =
	    (mboolisset(wlc->pub->radio_disabled, WL_RADIO_MPC_DISABLE) ? OFF :
	     ON);
	mpc_radio = (wlc_ismpc(wlc) == true) ? OFF : ON;

	if (radio_state == ON && mpc_radio == OFF)
		wlc->mpc_delay_off = wlc->mpc_dlycnt;
	else if (radio_state == OFF && mpc_radio == ON) {
		mboolclr(wlc->pub->radio_disabled, WL_RADIO_MPC_DISABLE);
		wlc_radio_upd(wlc);
		if (wlc->mpc_offcnt < WLC_MPC_THRESHOLD) {
			wlc->mpc_dlycnt = WLC_MPC_MAX_DELAYCNT;
		} else
			wlc->mpc_dlycnt = WLC_MPC_MIN_DELAYCNT;
		wlc->mpc_dur += OSL_SYSUPTIME() - wlc->mpc_laston_ts;
	}
	/* Below logic is meant to capture the transition from mpc off to mpc on for reasons
	 * other than wlc->mpc_delay_off keeping the mpc off. In that case reset
	 * wlc->mpc_delay_off to wlc->mpc_dlycnt, so that we restart the countdown of mpc_delay_off
	 */
	if ((wlc->prev_non_delay_mpc == false) &&
	    (wlc_is_non_delay_mpc(wlc) == true) && wlc->mpc_delay_off) {
		wlc->mpc_delay_off = wlc->mpc_dlycnt;
	}
	wlc->prev_non_delay_mpc = wlc_is_non_delay_mpc(wlc);
}

/*
 * centralized radio disable/enable function,
 * invoke radio enable/disable after updating hwradio status
 */
static void wlc_radio_upd(struct wlc_info *wlc)
{
	if (wlc->pub->radio_disabled)
		wlc_radio_disable(wlc);
	else
		wlc_radio_enable(wlc);
}

/* maintain LED behavior in down state */
static void wlc_down_led_upd(struct wlc_info *wlc)
{
	ASSERT(!wlc->pub->up);

	/* maintain LEDs while in down state, turn on sbclk if not available yet */
	/* turn on sbclk if necessary */
	if (!AP_ENAB(wlc->pub)) {
		wlc_pllreq(wlc, true, WLC_PLLREQ_FLIP);

		wlc_pllreq(wlc, false, WLC_PLLREQ_FLIP);
	}
}

void wlc_radio_disable(struct wlc_info *wlc)
{
	if (!wlc->pub->up) {
		wlc_down_led_upd(wlc);
		return;
	}

	wlc_radio_monitor_start(wlc);
	wl_down(wlc->wl);
}

static void wlc_radio_enable(struct wlc_info *wlc)
{
	if (wlc->pub->up)
		return;

	if (DEVICEREMOVED(wlc))
		return;

	if (!wlc->down_override) {	/* imposed by wl down/out ioctl */
		wl_up(wlc->wl);
	}
}

/* periodical query hw radio button while driver is "down" */
static void wlc_radio_timer(void *arg)
{
	struct wlc_info *wlc = (struct wlc_info *) arg;

	if (DEVICEREMOVED(wlc)) {
		WL_ERROR(("wl%d: %s: dead chip\n", wlc->pub->unit, __func__));
		wl_down(wlc->wl);
		return;
	}

	/* cap mpc off count */
	if (wlc->mpc_offcnt < WLC_MPC_MAX_DELAYCNT)
		wlc->mpc_offcnt++;

	/* validate all the reasons driver could be down and running this radio_timer */
	ASSERT(wlc->pub->radio_disabled || wlc->down_override);
	wlc_radio_hwdisable_upd(wlc);
	wlc_radio_upd(wlc);
}

static bool wlc_radio_monitor_start(struct wlc_info *wlc)
{
	/* Don't start the timer if HWRADIO feature is disabled */
	if (wlc->radio_monitor || (wlc->pub->wlfeatureflag & WL_SWFL_NOHWRADIO))
		return true;

	wlc->radio_monitor = true;
	wlc_pllreq(wlc, true, WLC_PLLREQ_RADIO_MON);
	wl_add_timer(wlc->wl, wlc->radio_timer, TIMER_INTERVAL_RADIOCHK, true);
	return true;
}

bool wlc_radio_monitor_stop(struct wlc_info *wlc)
{
	if (!wlc->radio_monitor)
		return true;

	ASSERT((wlc->pub->wlfeatureflag & WL_SWFL_NOHWRADIO) !=
	       WL_SWFL_NOHWRADIO);

	wlc->radio_monitor = false;
	wlc_pllreq(wlc, false, WLC_PLLREQ_RADIO_MON);
	return wl_del_timer(wlc->wl, wlc->radio_timer);
}

/* bring the driver down, but don't reset hardware */
void wlc_out(struct wlc_info *wlc)
{
	wlc_bmac_set_noreset(wlc->hw, true);
	wlc_radio_upd(wlc);
	wl_down(wlc->wl);
	wlc_bmac_set_noreset(wlc->hw, false);

	/* core clk is true in BMAC driver due to noreset, need to mirror it in HIGH */
	wlc->clk = true;

	/* This will make sure that when 'up' is done
	 * after 'out' it'll restore hardware (especially gpios)
	 */
	wlc->pub->hw_up = false;
}

#if defined(BCMDBG)
/* Verify the sanity of wlc->tx_prec_map. This can be done only by making sure that
 * if there is no packet pending for the FIFO, then the corresponding prec bits should be set
 * in prec_map. Of course, ignore this rule when block_datafifo is set
 */
static bool wlc_tx_prec_map_verify(struct wlc_info *wlc)
{
	/* For non-WME, both fifos have overlapping prec_map. So it's an error only if both
	 * fail the check.
	 */
	if (!EDCF_ENAB(wlc->pub)) {
		if (!(WLC_TX_FIFO_CHECK(wlc, TX_DATA_FIFO) ||
		      WLC_TX_FIFO_CHECK(wlc, TX_CTL_FIFO)))
			return false;
		else
			return true;
	}

	return WLC_TX_FIFO_CHECK(wlc, TX_AC_BK_FIFO)
		&& WLC_TX_FIFO_CHECK(wlc, TX_AC_BE_FIFO)
		&& WLC_TX_FIFO_CHECK(wlc, TX_AC_VI_FIFO)
		&& WLC_TX_FIFO_CHECK(wlc, TX_AC_VO_FIFO);
}
#endif				/* BCMDBG */

static void wlc_watchdog_by_timer(void *arg)
{
	struct wlc_info *wlc = (struct wlc_info *) arg;
	wlc_watchdog(arg);
	if (WLC_WATCHDOG_TBTT(wlc)) {
		/* set to normal osl watchdog period */
		wl_del_timer(wlc->wl, wlc->wdtimer);
		wl_add_timer(wlc->wl, wlc->wdtimer, TIMER_INTERVAL_WATCHDOG,
			     true);
	}
}

/* common watchdog code */
static void wlc_watchdog(void *arg)
{
	struct wlc_info *wlc = (struct wlc_info *) arg;
	int i;
	wlc_bsscfg_t *cfg;

	WL_TRACE(("wl%d: wlc_watchdog\n", wlc->pub->unit));

	if (!wlc->pub->up)
		return;

	if (DEVICEREMOVED(wlc)) {
		WL_ERROR(("wl%d: %s: dead chip\n", wlc->pub->unit, __func__));
		wl_down(wlc->wl);
		return;
	}

	/* increment second count */
	wlc->pub->now++;

	/* delay radio disable */
	if (wlc->mpc_delay_off) {
		if (--wlc->mpc_delay_off == 0) {
			mboolset(wlc->pub->radio_disabled,
				 WL_RADIO_MPC_DISABLE);
			if (wlc->mpc && wlc_ismpc(wlc))
				wlc->mpc_offcnt = 0;
			wlc->mpc_laston_ts = OSL_SYSUPTIME();
		}
	}

	/* mpc sync */
	wlc_radio_mpc_upd(wlc);
	/* radio sync: sw/hw/mpc --> radio_disable/radio_enable */
	wlc_radio_hwdisable_upd(wlc);
	wlc_radio_upd(wlc);
	/* if ismpc, driver should be in down state if up/down is allowed */
	if (wlc->mpc && wlc_ismpc(wlc))
		ASSERT(!wlc->pub->up);
	/* if radio is disable, driver may be down, quit here */
	if (wlc->pub->radio_disabled)
		return;

	wlc_bmac_watchdog(wlc);

	/* occasionally sample mac stat counters to detect 16-bit counter wrap */
	if ((WLC_UPDATE_STATS(wlc))
	    && (!(wlc->pub->now % SW_TIMER_MAC_STAT_UPD)))
		wlc_statsupd(wlc);

	/* Manage TKIP countermeasures timers */
	FOREACH_BSS(wlc, i, cfg) {
		if (cfg->tk_cm_dt) {
			cfg->tk_cm_dt--;
		}
		if (cfg->tk_cm_bt) {
			cfg->tk_cm_bt--;
		}
	}

	/* Call any registered watchdog handlers */
	for (i = 0; i < WLC_MAXMODULES; i++) {
		if (wlc->modulecb[i].watchdog_fn)
			wlc->modulecb[i].watchdog_fn(wlc->modulecb[i].hdl);
	}

	if (WLCISNPHY(wlc->band) && !wlc->pub->tempsense_disable &&
	    ((wlc->pub->now - wlc->tempsense_lasttime) >=
	     WLC_TEMPSENSE_PERIOD)) {
		wlc->tempsense_lasttime = wlc->pub->now;
		wlc_tempsense_upd(wlc);
	}
	/* BMAC_NOTE: for HIGH_ONLY driver, this seems being called after RPC bus failed */
	ASSERT(wlc_bmac_taclear(wlc->hw, true));

	/* Verify that tx_prec_map and fifos are in sync to avoid lock ups */
	ASSERT(wlc_tx_prec_map_verify(wlc));

	ASSERT(wlc_ps_check(wlc));
}

/* make interface operational */
int wlc_up(struct wlc_info *wlc)
{
	WL_TRACE(("wl%d: %s:\n", wlc->pub->unit, __func__));

	/* HW is turned off so don't try to access it */
	if (wlc->pub->hw_off || DEVICEREMOVED(wlc))
		return BCME_RADIOOFF;

	if (!wlc->pub->hw_up) {
		wlc_bmac_hw_up(wlc->hw);
		wlc->pub->hw_up = true;
	}

	if ((wlc->pub->boardflags & BFL_FEM)
	    && (wlc->pub->sih->chip == BCM4313_CHIP_ID)) {
		if (wlc->pub->boardrev >= 0x1250
		    && (wlc->pub->boardflags & BFL_FEM_BT)) {
			wlc_mhf(wlc, MHF5, MHF5_4313_GPIOCTRL,
				MHF5_4313_GPIOCTRL, WLC_BAND_ALL);
		} else {
			wlc_mhf(wlc, MHF4, MHF4_EXTPA_ENABLE, MHF4_EXTPA_ENABLE,
				WLC_BAND_ALL);
		}
	}

	/*
	 * Need to read the hwradio status here to cover the case where the system
	 * is loaded with the hw radio disabled. We do not want to bring the driver up in this case.
	 * if radio is disabled, abort up, lower power, start radio timer and return 0(for NDIS)
	 * don't call radio_update to avoid looping wlc_up.
	 *
	 * wlc_bmac_up_prep() returns either 0 or BCME_RADIOOFF only
	 */
	if (!wlc->pub->radio_disabled) {
		int status = wlc_bmac_up_prep(wlc->hw);
		if (status == BCME_RADIOOFF) {
			if (!mboolisset
			    (wlc->pub->radio_disabled, WL_RADIO_HW_DISABLE)) {
				int idx;
				wlc_bsscfg_t *bsscfg;
				mboolset(wlc->pub->radio_disabled,
					 WL_RADIO_HW_DISABLE);

				FOREACH_BSS(wlc, idx, bsscfg) {
					if (!BSSCFG_STA(bsscfg)
					    || !bsscfg->enable || !bsscfg->BSS)
						continue;
					WL_ERROR(("wl%d.%d: wlc_up: rfdisable -> " "wlc_bsscfg_disable()\n", wlc->pub->unit, idx));
				}
			}
		} else
			ASSERT(!status);
	}

	if (wlc->pub->radio_disabled) {
		wlc_radio_monitor_start(wlc);
		return 0;
	}

	/* wlc_bmac_up_prep has done wlc_corereset(). so clk is on, set it */
	wlc->clk = true;

	wlc_radio_monitor_stop(wlc);

	/* Set EDCF hostflags */
	if (EDCF_ENAB(wlc->pub)) {
		wlc_mhf(wlc, MHF1, MHF1_EDCF, MHF1_EDCF, WLC_BAND_ALL);
	} else {
		wlc_mhf(wlc, MHF1, MHF1_EDCF, 0, WLC_BAND_ALL);
	}

	if (WLC_WAR16165(wlc))
		wlc_mhf(wlc, MHF2, MHF2_PCISLOWCLKWAR, MHF2_PCISLOWCLKWAR,
			WLC_BAND_ALL);

	wl_init(wlc->wl);
	wlc->pub->up = true;

	if (wlc->bandinit_pending) {
		wlc_suspend_mac_and_wait(wlc);
		wlc_set_chanspec(wlc, wlc->default_bss->chanspec);
		wlc->bandinit_pending = false;
		wlc_enable_mac(wlc);
	}

	wlc_bmac_up_finish(wlc->hw);

	/* other software states up after ISR is running */
	/* start APs that were to be brought up but are not up  yet */
	/* if (AP_ENAB(wlc->pub)) wlc_restart_ap(wlc->ap); */

	/* Program the TX wme params with the current settings */
	wlc_wme_retries_write(wlc);

	/* start one second watchdog timer */
	ASSERT(!wlc->WDarmed);
	wl_add_timer(wlc->wl, wlc->wdtimer, TIMER_INTERVAL_WATCHDOG, true);
	wlc->WDarmed = true;

	/* ensure antenna config is up to date */
	wlc_stf_phy_txant_upd(wlc);
	/* ensure LDPC config is in sync */
	wlc_ht_update_ldpc(wlc, wlc->stf->ldpc);

	return 0;
}

/* Initialize the base precedence map for dequeueing from txq based on WME settings */
static void wlc_tx_prec_map_init(struct wlc_info *wlc)
{
	wlc->tx_prec_map = WLC_PREC_BMP_ALL;
	memset(wlc->fifo2prec_map, 0, NFIFO * sizeof(u16));

	/* For non-WME, both fifos have overlapping MAXPRIO. So just disable all precedences
	 * if either is full.
	 */
	if (!EDCF_ENAB(wlc->pub)) {
		wlc->fifo2prec_map[TX_DATA_FIFO] = WLC_PREC_BMP_ALL;
		wlc->fifo2prec_map[TX_CTL_FIFO] = WLC_PREC_BMP_ALL;
	} else {
		wlc->fifo2prec_map[TX_AC_BK_FIFO] = WLC_PREC_BMP_AC_BK;
		wlc->fifo2prec_map[TX_AC_BE_FIFO] = WLC_PREC_BMP_AC_BE;
		wlc->fifo2prec_map[TX_AC_VI_FIFO] = WLC_PREC_BMP_AC_VI;
		wlc->fifo2prec_map[TX_AC_VO_FIFO] = WLC_PREC_BMP_AC_VO;
	}
}

static uint wlc_down_del_timer(struct wlc_info *wlc)
{
	uint callbacks = 0;

	return callbacks;
}

/*
 * Mark the interface nonoperational, stop the software mechanisms,
 * disable the hardware, free any transient buffer state.
 * Return a count of the number of driver callbacks still pending.
 */
uint wlc_down(struct wlc_info *wlc)
{

	uint callbacks = 0;
	int i;
	bool dev_gone = false;
	wlc_txq_info_t *qi;

	WL_TRACE(("wl%d: %s:\n", wlc->pub->unit, __func__));

	/* check if we are already in the going down path */
	if (wlc->going_down) {
		WL_ERROR(("wl%d: %s: Driver going down so return\n",
			  wlc->pub->unit, __func__));
		return 0;
	}
	if (!wlc->pub->up)
		return callbacks;

	/* in between, mpc could try to bring down again.. */
	wlc->going_down = true;

	callbacks += wlc_bmac_down_prep(wlc->hw);

	dev_gone = DEVICEREMOVED(wlc);

	/* Call any registered down handlers */
	for (i = 0; i < WLC_MAXMODULES; i++) {
		if (wlc->modulecb[i].down_fn)
			callbacks +=
			    wlc->modulecb[i].down_fn(wlc->modulecb[i].hdl);
	}

	/* cancel the watchdog timer */
	if (wlc->WDarmed) {
		if (!wl_del_timer(wlc->wl, wlc->wdtimer))
			callbacks++;
		wlc->WDarmed = false;
	}
	/* cancel all other timers */
	callbacks += wlc_down_del_timer(wlc);

	/* interrupt must have been blocked */
	ASSERT((wlc->macintmask == 0) || !wlc->pub->up);

	wlc->pub->up = false;

	wlc_phy_mute_upd(wlc->band->pi, false, PHY_MUTE_ALL);

	/* clear txq flow control */
	wlc_txflowcontrol_reset(wlc);

	/* flush tx queues */
	for (qi = wlc->tx_queues; qi != NULL; qi = qi->next) {
		pktq_flush(wlc->osh, &qi->q, true, NULL, 0);
		ASSERT(pktq_empty(&qi->q));
	}

	/* flush event queue.
	 * Should be the last thing done after all the events are generated
	 * Just delivers the events synchronously instead of waiting for a timer
	 */
	callbacks += wlc_eventq_down(wlc->eventq);

	callbacks += wlc_bmac_down_finish(wlc->hw);

	/* wlc_bmac_down_finish has done wlc_coredisable(). so clk is off */
	wlc->clk = false;


	/* Verify all packets are flushed from the driver */
	if (wlc->osh->pktalloced != 0) {
		WL_ERROR(("%d packets not freed at wlc_down!!!!!!\n",
			  wlc->osh->pktalloced));
	}
#ifdef BCMDBG
	/* Since all the packets should have been freed,
	 * all callbacks should have been called
	 */
	for (i = 1; i <= wlc->pub->tunables->maxpktcb; i++)
		ASSERT(wlc->pkt_callback[i].fn == NULL);
#endif
	wlc->going_down = false;
	return callbacks;
}

/* Set the current gmode configuration */
int wlc_set_gmode(struct wlc_info *wlc, u8 gmode, bool config)
{
	int ret = 0;
	uint i;
	wlc_rateset_t rs;
	/* Default to 54g Auto */
	s8 shortslot = WLC_SHORTSLOT_AUTO;	/* Advertise and use shortslot (-1/0/1 Auto/Off/On) */
	bool shortslot_restrict = false;	/* Restrict association to stations that support shortslot
						 */
	bool ignore_bcns = true;	/* Ignore legacy beacons on the same channel */
	bool ofdm_basic = false;	/* Make 6, 12, and 24 basic rates */
	int preamble = WLC_PLCP_LONG;	/* Advertise and use short preambles (-1/0/1 Auto/Off/On) */
	bool preamble_restrict = false;	/* Restrict association to stations that support short
					 * preambles
					 */
	wlcband_t *band;

	/* if N-support is enabled, allow Gmode set as long as requested
	 * Gmode is not GMODE_LEGACY_B
	 */
	if (N_ENAB(wlc->pub) && gmode == GMODE_LEGACY_B)
		return BCME_UNSUPPORTED;

	/* verify that we are dealing with 2G band and grab the band pointer */
	if (wlc->band->bandtype == WLC_BAND_2G)
		band = wlc->band;
	else if ((NBANDS(wlc) > 1) &&
		 (wlc->bandstate[OTHERBANDUNIT(wlc)]->bandtype == WLC_BAND_2G))
		band = wlc->bandstate[OTHERBANDUNIT(wlc)];
	else
		return BCME_BADBAND;

	/* Legacy or bust when no OFDM is supported by regulatory */
	if ((wlc_channel_locale_flags_in_band(wlc->cmi, band->bandunit) &
	     WLC_NO_OFDM) && (gmode != GMODE_LEGACY_B))
		return BCME_RANGE;

	/* update configuration value */
	if (config == true)
		wlc_protection_upd(wlc, WLC_PROT_G_USER, gmode);

	/* Clear supported rates filter */
	memset(&wlc->sup_rates_override, 0, sizeof(wlc_rateset_t));

	/* Clear rateset override */
	memset(&rs, 0, sizeof(wlc_rateset_t));

	switch (gmode) {
	case GMODE_LEGACY_B:
		shortslot = WLC_SHORTSLOT_OFF;
		wlc_rateset_copy(&gphy_legacy_rates, &rs);

		break;

	case GMODE_LRS:
		if (AP_ENAB(wlc->pub))
			wlc_rateset_copy(&cck_rates, &wlc->sup_rates_override);
		break;

	case GMODE_AUTO:
		/* Accept defaults */
		break;

	case GMODE_ONLY:
		ofdm_basic = true;
		preamble = WLC_PLCP_SHORT;
		preamble_restrict = true;
		break;

	case GMODE_PERFORMANCE:
		if (AP_ENAB(wlc->pub))	/* Put all rates into the Supported Rates element */
			wlc_rateset_copy(&cck_ofdm_rates,
					 &wlc->sup_rates_override);

		shortslot = WLC_SHORTSLOT_ON;
		shortslot_restrict = true;
		ofdm_basic = true;
		preamble = WLC_PLCP_SHORT;
		preamble_restrict = true;
		break;

	default:
		/* Error */
		WL_ERROR(("wl%d: %s: invalid gmode %d\n", wlc->pub->unit,
			  __func__, gmode));
		return BCME_UNSUPPORTED;
	}

	/*
	 * If we are switching to gmode == GMODE_LEGACY_B,
	 * clean up rate info that may refer to OFDM rates.
	 */
	if ((gmode == GMODE_LEGACY_B) && (band->gmode != GMODE_LEGACY_B)) {
		band->gmode = gmode;
		if (band->rspec_override && !IS_CCK(band->rspec_override)) {
			band->rspec_override = 0;
			wlc_reprate_init(wlc);
		}
		if (band->mrspec_override && !IS_CCK(band->mrspec_override)) {
			band->mrspec_override = 0;
		}
	}

	band->gmode = gmode;

	wlc->ignore_bcns = ignore_bcns;

	wlc->shortslot_override = shortslot;

	if (AP_ENAB(wlc->pub)) {
		/* wlc->ap->shortslot_restrict = shortslot_restrict; */
		wlc->PLCPHdr_override =
		    (preamble !=
		     WLC_PLCP_LONG) ? WLC_PLCP_SHORT : WLC_PLCP_AUTO;
	}

	if ((AP_ENAB(wlc->pub) && preamble != WLC_PLCP_LONG)
	    || preamble == WLC_PLCP_SHORT)
		wlc->default_bss->capability |= DOT11_CAP_SHORT;
	else
		wlc->default_bss->capability &= ~DOT11_CAP_SHORT;

	/* Update shortslot capability bit for AP and IBSS */
	if ((AP_ENAB(wlc->pub) && shortslot == WLC_SHORTSLOT_AUTO) ||
	    shortslot == WLC_SHORTSLOT_ON)
		wlc->default_bss->capability |= DOT11_CAP_SHORTSLOT;
	else
		wlc->default_bss->capability &= ~DOT11_CAP_SHORTSLOT;

	/* Use the default 11g rateset */
	if (!rs.count)
		wlc_rateset_copy(&cck_ofdm_rates, &rs);

	if (ofdm_basic) {
		for (i = 0; i < rs.count; i++) {
			if (rs.rates[i] == WLC_RATE_6M
			    || rs.rates[i] == WLC_RATE_12M
			    || rs.rates[i] == WLC_RATE_24M)
				rs.rates[i] |= WLC_RATE_FLAG;
		}
	}

	/* Set default bss rateset */
	wlc->default_bss->rateset.count = rs.count;
	bcopy((char *)rs.rates, (char *)wlc->default_bss->rateset.rates,
	      sizeof(wlc->default_bss->rateset.rates));

	return ret;
}

static int wlc_nmode_validate(struct wlc_info *wlc, s32 nmode)
{
	int err = 0;

	switch (nmode) {

	case OFF:
		break;

	case AUTO:
	case WL_11N_2x2:
	case WL_11N_3x3:
		if (!(WLC_PHY_11N_CAP(wlc->band)))
			err = BCME_BADBAND;
		break;

	default:
		err = BCME_RANGE;
		break;
	}

	return err;
}

int wlc_set_nmode(struct wlc_info *wlc, s32 nmode)
{
	uint i;
	int err;

	err = wlc_nmode_validate(wlc, nmode);
	ASSERT(err == 0);
	if (err)
		return err;

	switch (nmode) {
	case OFF:
		wlc->pub->_n_enab = OFF;
		wlc->default_bss->flags &= ~WLC_BSS_HT;
		/* delete the mcs rates from the default and hw ratesets */
		wlc_rateset_mcs_clear(&wlc->default_bss->rateset);
		for (i = 0; i < NBANDS(wlc); i++) {
			memset(wlc->bandstate[i]->hw_rateset.mcs, 0,
			       MCSSET_LEN);
			if (IS_MCS(wlc->band->rspec_override)) {
				wlc->bandstate[i]->rspec_override = 0;
				wlc_reprate_init(wlc);
			}
			if (IS_MCS(wlc->band->mrspec_override))
				wlc->bandstate[i]->mrspec_override = 0;
		}
		break;

	case AUTO:
		if (wlc->stf->txstreams == WL_11N_3x3)
			nmode = WL_11N_3x3;
		else
			nmode = WL_11N_2x2;
	case WL_11N_2x2:
	case WL_11N_3x3:
		ASSERT(WLC_PHY_11N_CAP(wlc->band));
		/* force GMODE_AUTO if NMODE is ON */
		wlc_set_gmode(wlc, GMODE_AUTO, true);
		if (nmode == WL_11N_3x3)
			wlc->pub->_n_enab = SUPPORT_HT;
		else
			wlc->pub->_n_enab = SUPPORT_11N;
		wlc->default_bss->flags |= WLC_BSS_HT;
		/* add the mcs rates to the default and hw ratesets */
		wlc_rateset_mcs_build(&wlc->default_bss->rateset,
				      wlc->stf->txstreams);
		for (i = 0; i < NBANDS(wlc); i++)
			memcpy(wlc->bandstate[i]->hw_rateset.mcs,
			       wlc->default_bss->rateset.mcs, MCSSET_LEN);
		break;

	default:
		ASSERT(0);
		break;
	}

	return err;
}

static int wlc_set_rateset(struct wlc_info *wlc, wlc_rateset_t *rs_arg)
{
	wlc_rateset_t rs, new;
	uint bandunit;

	bcopy((char *)rs_arg, (char *)&rs, sizeof(wlc_rateset_t));

	/* check for bad count value */
	if ((rs.count == 0) || (rs.count > WLC_NUMRATES))
		return BCME_BADRATESET;

	/* try the current band */
	bandunit = wlc->band->bandunit;
	bcopy((char *)&rs, (char *)&new, sizeof(wlc_rateset_t));
	if (wlc_rate_hwrs_filter_sort_validate
	    (&new, &wlc->bandstate[bandunit]->hw_rateset, true,
	     wlc->stf->txstreams))
		goto good;

	/* try the other band */
	if (IS_MBAND_UNLOCKED(wlc)) {
		bandunit = OTHERBANDUNIT(wlc);
		bcopy((char *)&rs, (char *)&new, sizeof(wlc_rateset_t));
		if (wlc_rate_hwrs_filter_sort_validate(&new,
						       &wlc->
						       bandstate[bandunit]->
						       hw_rateset, true,
						       wlc->stf->txstreams))
			goto good;
	}

	return BCME_ERROR;

 good:
	/* apply new rateset */
	bcopy((char *)&new, (char *)&wlc->default_bss->rateset,
	      sizeof(wlc_rateset_t));
	bcopy((char *)&new, (char *)&wlc->bandstate[bandunit]->defrateset,
	      sizeof(wlc_rateset_t));
	return 0;
}

/* simplified integer set interface for common ioctl handler */
int wlc_set(struct wlc_info *wlc, int cmd, int arg)
{
	return wlc_ioctl(wlc, cmd, (void *)&arg, sizeof(arg), NULL);
}

/* simplified integer get interface for common ioctl handler */
int wlc_get(struct wlc_info *wlc, int cmd, int *arg)
{
	return wlc_ioctl(wlc, cmd, arg, sizeof(int), NULL);
}

static void wlc_ofdm_rateset_war(struct wlc_info *wlc)
{
	u8 r;
	bool war = false;

	if (wlc->cfg->associated)
		r = wlc->cfg->current_bss->rateset.rates[0];
	else
		r = wlc->default_bss->rateset.rates[0];

	wlc_phy_ofdm_rateset_war(wlc->band->pi, war);

	return;
}

int
wlc_ioctl(struct wlc_info *wlc, int cmd, void *arg, int len,
	  struct wlc_if *wlcif)
{
	return _wlc_ioctl(wlc, cmd, arg, len, wlcif);
}

/* common ioctl handler. return: 0=ok, -1=error, positive=particular error */
static int
_wlc_ioctl(struct wlc_info *wlc, int cmd, void *arg, int len,
	   struct wlc_if *wlcif)
{
	int val, *pval;
	bool bool_val;
	int bcmerror;
	d11regs_t *regs;
	uint i;
	struct scb *nextscb;
	bool ta_ok;
	uint band;
	rw_reg_t *r;
	wlc_bsscfg_t *bsscfg;
	struct osl_info *osh;
	wlc_bss_info_t *current_bss;

	/* update bsscfg pointer */
	bsscfg = NULL;		/* XXX: Hack bsscfg to be size one and use this globally */
	current_bss = NULL;

	/* initialize the following to get rid of compiler warning */
	nextscb = NULL;
	ta_ok = false;
	band = 0;
	r = NULL;

	/* If the device is turned off, then it's not "removed" */
	if (!wlc->pub->hw_off && DEVICEREMOVED(wlc)) {
		WL_ERROR(("wl%d: %s: dead chip\n", wlc->pub->unit, __func__));
		wl_down(wlc->wl);
		return BCME_ERROR;
	}

	ASSERT(!(wlc->pub->hw_off && wlc->pub->up));

	/* default argument is generic integer */
	pval = arg ? (int *)arg:NULL;

	/* This will prevent the misaligned access */
	if (pval && (u32) len >= sizeof(val))
		bcopy(pval, &val, sizeof(val));
	else
		val = 0;

	/* bool conversion to avoid duplication below */
	bool_val = val != 0;

	if (cmd != WLC_SET_CHANNEL)
		WL_NONE(("WLC_IOCTL: cmd %d val 0x%x (%d) len %d\n", cmd,
			 (uint) val, val, len));

	bcmerror = 0;
	regs = wlc->regs;
	osh = wlc->osh;

	/* A few commands don't need any arguments; all the others do. */
	switch (cmd) {
	case WLC_UP:
	case WLC_OUT:
	case WLC_DOWN:
	case WLC_DISASSOC:
	case WLC_RESTART:
	case WLC_REBOOT:
	case WLC_START_CHANNEL_QA:
	case WLC_INIT:
		break;

	default:
		if ((arg == NULL) || (len <= 0)) {
			WL_ERROR(("wl%d: %s: Command %d needs arguments\n",
				  wlc->pub->unit, __func__, cmd));
			bcmerror = BCME_BADARG;
			goto done;
		}
	}

	switch (cmd) {

#if defined(BCMDBG)
	case WLC_GET_MSGLEVEL:
		*pval = wl_msg_level;
		break;

	case WLC_SET_MSGLEVEL:
		wl_msg_level = val;
		break;
#endif

	case WLC_GET_INSTANCE:
		*pval = wlc->pub->unit;
		break;

	case WLC_GET_CHANNEL:{
			channel_info_t *ci = (channel_info_t *) arg;

			ASSERT(len > (int)sizeof(ci));

			ci->hw_channel =
			    CHSPEC_CHANNEL(WLC_BAND_PI_RADIO_CHANSPEC);
			ci->target_channel =
			    CHSPEC_CHANNEL(wlc->default_bss->chanspec);
			ci->scan_channel = 0;

			break;
		}

	case WLC_SET_CHANNEL:{
			chanspec_t chspec = CH20MHZ_CHSPEC(val);

			if (val < 0 || val > MAXCHANNEL) {
				bcmerror = BCME_OUTOFRANGECHAN;
				break;
			}

			if (!wlc_valid_chanspec_db(wlc->cmi, chspec)) {
				bcmerror = BCME_BADCHAN;
				break;
			}

			if (!wlc->pub->up && IS_MBAND_UNLOCKED(wlc)) {
				if (wlc->band->bandunit !=
				    CHSPEC_WLCBANDUNIT(chspec))
					wlc->bandinit_pending = true;
				else
					wlc->bandinit_pending = false;
			}

			wlc->default_bss->chanspec = chspec;
			/* wlc_BSSinit() will sanitize the rateset before using it.. */
			if (wlc->pub->up && !wlc->pub->associated &&
			    (WLC_BAND_PI_RADIO_CHANSPEC != chspec)) {
				wlc_set_home_chanspec(wlc, chspec);
				wlc_suspend_mac_and_wait(wlc);
				wlc_set_chanspec(wlc, chspec);
				wlc_enable_mac(wlc);
			}
			break;
		}

#if defined(BCMDBG)
	case WLC_GET_UCFLAGS:
		if (!wlc->pub->up) {
			bcmerror = BCME_NOTUP;
			break;
		}

		/* optional band is stored in the second integer of incoming buffer */
		band =
		    (len <
		     (int)(2 * sizeof(int))) ? WLC_BAND_AUTO : ((int *)arg)[1];

		/* bcmerror checking */
		bcmerror = wlc_iocregchk(wlc, band);
		if (bcmerror)
			break;

		if (val >= MHFMAX) {
			bcmerror = BCME_RANGE;
			break;
		}

		*pval = wlc_bmac_mhf_get(wlc->hw, (u8) val, WLC_BAND_AUTO);
		break;

	case WLC_SET_UCFLAGS:
		if (!wlc->pub->up) {
			bcmerror = BCME_NOTUP;
			break;
		}

		/* optional band is stored in the second integer of incoming buffer */
		band =
		    (len <
		     (int)(2 * sizeof(int))) ? WLC_BAND_AUTO : ((int *)arg)[1];

		/* bcmerror checking */
		bcmerror = wlc_iocregchk(wlc, band);
		if (bcmerror)
			break;

		i = (u16) val;
		if (i >= MHFMAX) {
			bcmerror = BCME_RANGE;
			break;
		}

		wlc_mhf(wlc, (u8) i, 0xffff, (u16) (val >> NBITS(u16)),
			WLC_BAND_AUTO);
		break;

	case WLC_GET_SHMEM:
		ta_ok = true;

		/* optional band is stored in the second integer of incoming buffer */
		band =
		    (len <
		     (int)(2 * sizeof(int))) ? WLC_BAND_AUTO : ((int *)arg)[1];

		/* bcmerror checking */
		bcmerror = wlc_iocregchk(wlc, band);
		if (bcmerror)
			break;

		if (val & 1) {
			bcmerror = BCME_BADADDR;
			break;
		}

		*pval = wlc_read_shm(wlc, (u16) val);
		break;

	case WLC_SET_SHMEM:
		ta_ok = true;

		/* optional band is stored in the second integer of incoming buffer */
		band =
		    (len <
		     (int)(2 * sizeof(int))) ? WLC_BAND_AUTO : ((int *)arg)[1];

		/* bcmerror checking */
		bcmerror = wlc_iocregchk(wlc, band);
		if (bcmerror)
			break;

		if (val & 1) {
			bcmerror = BCME_BADADDR;
			break;
		}

		wlc_write_shm(wlc, (u16) val,
			      (u16) (val >> NBITS(u16)));
		break;

	case WLC_R_REG:	/* MAC registers */
		ta_ok = true;
		r = (rw_reg_t *) arg;
		band = WLC_BAND_AUTO;

		if (len < (int)(sizeof(rw_reg_t) - sizeof(uint))) {
			bcmerror = BCME_BUFTOOSHORT;
			break;
		}

		if (len >= (int)sizeof(rw_reg_t))
			band = r->band;

		/* bcmerror checking */
		bcmerror = wlc_iocregchk(wlc, band);
		if (bcmerror)
			break;

		if ((r->byteoff + r->size) > sizeof(d11regs_t)) {
			bcmerror = BCME_BADADDR;
			break;
		}
		if (r->size == sizeof(u32))
			r->val =
			    R_REG(osh,
				  (u32 *)((unsigned char *)(unsigned long)regs +
					      r->byteoff));
		else if (r->size == sizeof(u16))
			r->val =
			    R_REG(osh,
				  (u16 *)((unsigned char *)(unsigned long)regs +
					      r->byteoff));
		else
			bcmerror = BCME_BADADDR;
		break;

	case WLC_W_REG:
		ta_ok = true;
		r = (rw_reg_t *) arg;
		band = WLC_BAND_AUTO;

		if (len < (int)(sizeof(rw_reg_t) - sizeof(uint))) {
			bcmerror = BCME_BUFTOOSHORT;
			break;
		}

		if (len >= (int)sizeof(rw_reg_t))
			band = r->band;

		/* bcmerror checking */
		bcmerror = wlc_iocregchk(wlc, band);
		if (bcmerror)
			break;

		if (r->byteoff + r->size > sizeof(d11regs_t)) {
			bcmerror = BCME_BADADDR;
			break;
		}
		if (r->size == sizeof(u32))
			W_REG(osh,
			      (u32 *)((unsigned char *)(unsigned long) regs +
					  r->byteoff), r->val);
		else if (r->size == sizeof(u16))
			W_REG(osh,
			      (u16 *)((unsigned char *)(unsigned long) regs +
					  r->byteoff), r->val);
		else
			bcmerror = BCME_BADADDR;
		break;
#endif				/* BCMDBG */

	case WLC_GET_TXANT:
		*pval = wlc->stf->txant;
		break;

	case WLC_SET_TXANT:
		bcmerror = wlc_stf_ant_txant_validate(wlc, (s8) val);
		if (bcmerror < 0)
			break;

		wlc->stf->txant = (s8) val;

		/* if down, we are done */
		if (!wlc->pub->up)
			break;

		wlc_suspend_mac_and_wait(wlc);

		wlc_stf_phy_txant_upd(wlc);
		wlc_beacon_phytxctl_txant_upd(wlc, wlc->bcn_rspec);

		wlc_enable_mac(wlc);

		break;

	case WLC_GET_ANTDIV:{
			u8 phy_antdiv;

			/* return configured value if core is down */
			if (!wlc->pub->up) {
				*pval = wlc->stf->ant_rx_ovr;

			} else {
				if (wlc_phy_ant_rxdiv_get
				    (wlc->band->pi, &phy_antdiv))
					*pval = (int)phy_antdiv;
				else
					*pval = (int)wlc->stf->ant_rx_ovr;
			}

			break;
		}
	case WLC_SET_ANTDIV:
		/* values are -1=driver default, 0=force0, 1=force1, 2=start1, 3=start0 */
		if ((val < -1) || (val > 3)) {
			bcmerror = BCME_RANGE;
			break;
		}

		if (val == -1)
			val = ANT_RX_DIV_DEF;

		wlc->stf->ant_rx_ovr = (u8) val;
		wlc_phy_ant_rxdiv_set(wlc->band->pi, (u8) val);
		break;

	case WLC_GET_RX_ANT:{	/* get latest used rx antenna */
			u16 rxstatus;

			if (!wlc->pub->up) {
				bcmerror = BCME_NOTUP;
				break;
			}

			rxstatus = R_REG(wlc->osh, &wlc->regs->phyrxstatus0);
			if (rxstatus == 0xdead || rxstatus == (u16) -1) {
				bcmerror = BCME_ERROR;
				break;
			}
			*pval = (rxstatus & PRXS0_RXANT_UPSUBBAND) ? 1 : 0;
			break;
		}

#if defined(BCMDBG)
	case WLC_GET_UCANTDIV:
		if (!wlc->clk) {
			bcmerror = BCME_NOCLK;
			break;
		}

		*pval =
		    (wlc_bmac_mhf_get(wlc->hw, MHF1, WLC_BAND_AUTO) &
		     MHF1_ANTDIV);
		break;

	case WLC_SET_UCANTDIV:{
			if (!wlc->pub->up) {
				bcmerror = BCME_NOTUP;
				break;
			}

			/* if multiband, band must be locked */
			if (IS_MBAND_UNLOCKED(wlc)) {
				bcmerror = BCME_NOTBANDLOCKED;
				break;
			}

			/* 4322 supports antdiv in phy, no need to set it to ucode */
			if (WLCISNPHY(wlc->band)
			    && D11REV_IS(wlc->pub->corerev, 16)) {
				WL_ERROR(("wl%d: can't set ucantdiv for 4322\n",
					  wlc->pub->unit));
				bcmerror = BCME_UNSUPPORTED;
			} else
				wlc_mhf(wlc, MHF1, MHF1_ANTDIV,
					(val ? MHF1_ANTDIV : 0), WLC_BAND_AUTO);
			break;
		}
#endif				/* defined(BCMDBG) */

	case WLC_GET_SRL:
		*pval = wlc->SRL;
		break;

	case WLC_SET_SRL:
		if (val >= 1 && val <= RETRY_SHORT_MAX) {
			int ac;
			wlc->SRL = (u16) val;

			wlc_bmac_retrylimit_upd(wlc->hw, wlc->SRL, wlc->LRL);

			for (ac = 0; ac < AC_COUNT; ac++) {
				WLC_WME_RETRY_SHORT_SET(wlc, ac, wlc->SRL);
			}
			wlc_wme_retries_write(wlc);
		} else
			bcmerror = BCME_RANGE;
		break;

	case WLC_GET_LRL:
		*pval = wlc->LRL;
		break;

	case WLC_SET_LRL:
		if (val >= 1 && val <= 255) {
			int ac;
			wlc->LRL = (u16) val;

			wlc_bmac_retrylimit_upd(wlc->hw, wlc->SRL, wlc->LRL);

			for (ac = 0; ac < AC_COUNT; ac++) {
				WLC_WME_RETRY_LONG_SET(wlc, ac, wlc->LRL);
			}
			wlc_wme_retries_write(wlc);
		} else
			bcmerror = BCME_RANGE;
		break;

	case WLC_GET_CWMIN:
		*pval = wlc->band->CWmin;
		break;

	case WLC_SET_CWMIN:
		if (!wlc->clk) {
			bcmerror = BCME_NOCLK;
			break;
		}

		if (val >= 1 && val <= 255) {
			wlc_set_cwmin(wlc, (u16) val);
		} else
			bcmerror = BCME_RANGE;
		break;

	case WLC_GET_CWMAX:
		*pval = wlc->band->CWmax;
		break;

	case WLC_SET_CWMAX:
		if (!wlc->clk) {
			bcmerror = BCME_NOCLK;
			break;
		}

		if (val >= 255 && val <= 2047) {
			wlc_set_cwmax(wlc, (u16) val);
		} else
			bcmerror = BCME_RANGE;
		break;

	case WLC_GET_RADIO:	/* use mask if don't want to expose some internal bits */
		*pval = wlc->pub->radio_disabled;
		break;

	case WLC_SET_RADIO:{	/* 32 bits input, higher 16 bits are mask, lower 16 bits are value to
				 * set
				 */
			u16 radiomask, radioval;
			uint validbits =
			    WL_RADIO_SW_DISABLE | WL_RADIO_HW_DISABLE;
			mbool new = 0;

			radiomask = (val & 0xffff0000) >> 16;
			radioval = val & 0x0000ffff;

			if ((radiomask == 0) || (radiomask & ~validbits)
			    || (radioval & ~validbits)
			    || ((radioval & ~radiomask) != 0)) {
				WL_ERROR(("SET_RADIO with wrong bits 0x%x\n",
					  val));
				bcmerror = BCME_RANGE;
				break;
			}

			new =
			    (wlc->pub->radio_disabled & ~radiomask) | radioval;
			wlc->pub->radio_disabled = new;

			wlc_radio_hwdisable_upd(wlc);
			wlc_radio_upd(wlc);
			break;
		}

	case WLC_GET_PHYTYPE:
		*pval = WLC_PHYTYPE(wlc->band->phytype);
		break;

#if defined(BCMDBG)
	case WLC_GET_KEY:
		if ((val >= 0) && (val < WLC_MAX_WSEC_KEYS(wlc))) {
			wl_wsec_key_t key;

			wsec_key_t *src_key = wlc->wsec_keys[val];

			if (len < (int)sizeof(key)) {
				bcmerror = BCME_BUFTOOSHORT;
				break;
			}

			memset((char *)&key, 0, sizeof(key));
			if (src_key) {
				key.index = src_key->id;
				key.len = src_key->len;
				bcopy(src_key->data, key.data, key.len);
				key.algo = src_key->algo;
				if (WSEC_SOFTKEY(wlc, src_key, bsscfg))
					key.flags |= WL_SOFT_KEY;
				if (src_key->flags & WSEC_PRIMARY_KEY)
					key.flags |= WL_PRIMARY_KEY;

				bcopy(src_key->ea.octet, key.ea.octet,
				      ETHER_ADDR_LEN);
			}

			bcopy((char *)&key, arg, sizeof(key));
		} else
			bcmerror = BCME_BADKEYIDX;
		break;
#endif				/* defined(BCMDBG) */

	case WLC_SET_KEY:
		bcmerror =
		    wlc_iovar_op(wlc, "wsec_key", NULL, 0, arg, len, IOV_SET,
				 wlcif);
		break;

	case WLC_GET_KEY_SEQ:{
			wsec_key_t *key;

			if (len < DOT11_WPA_KEY_RSC_LEN) {
				bcmerror = BCME_BUFTOOSHORT;
				break;
			}

			/* Return the key's tx iv as an EAPOL sequence counter.
			 * This will be used to supply the RSC value to a supplicant.
			 * The format is 8 bytes, with least significant in seq[0].
			 */

			key = WSEC_KEY(wlc, val);
			if ((val >= 0) && (val < WLC_MAX_WSEC_KEYS(wlc)) &&
				(key != NULL)) {
				u8 seq[DOT11_WPA_KEY_RSC_LEN];
				u16 lo;
				u32 hi;
				/* group keys in WPA-NONE (IBSS only, AES and TKIP) use a global TXIV */
				if ((bsscfg->WPA_auth & WPA_AUTH_NONE)
				    && ETHER_ISNULLADDR(&key->ea)) {
					lo = bsscfg->wpa_none_txiv.lo;
					hi = bsscfg->wpa_none_txiv.hi;
				} else {
					lo = key->txiv.lo;
					hi = key->txiv.hi;
				}

				/* format the buffer, low to high */
				seq[0] = lo & 0xff;
				seq[1] = (lo >> 8) & 0xff;
				seq[2] = hi & 0xff;
				seq[3] = (hi >> 8) & 0xff;
				seq[4] = (hi >> 16) & 0xff;
				seq[5] = (hi >> 24) & 0xff;
				seq[6] = 0;
				seq[7] = 0;

				bcopy((char *)seq, arg, sizeof(seq));
			} else {
				bcmerror = BCME_BADKEYIDX;
			}
			break;
		}

	case WLC_GET_CURR_RATESET:{
			wl_rateset_t *ret_rs = (wl_rateset_t *) arg;
			wlc_rateset_t *rs;

			if (bsscfg->associated)
				rs = &current_bss->rateset;
			else
				rs = &wlc->default_bss->rateset;

			if (len < (int)(rs->count + sizeof(rs->count))) {
				bcmerror = BCME_BUFTOOSHORT;
				break;
			}

			/* Copy only legacy rateset section */
			ret_rs->count = rs->count;
			bcopy(&rs->rates, &ret_rs->rates, rs->count);
			break;
		}

	case WLC_GET_RATESET:{
			wlc_rateset_t rs;
			wl_rateset_t *ret_rs = (wl_rateset_t *) arg;

			memset(&rs, 0, sizeof(wlc_rateset_t));
			wlc_default_rateset(wlc, (wlc_rateset_t *) &rs);

			if (len < (int)(rs.count + sizeof(rs.count))) {
				bcmerror = BCME_BUFTOOSHORT;
				break;
			}

			/* Copy only legacy rateset section */
			ret_rs->count = rs.count;
			bcopy(&rs.rates, &ret_rs->rates, rs.count);
			break;
		}

	case WLC_SET_RATESET:{
			wlc_rateset_t rs;
			wl_rateset_t *in_rs = (wl_rateset_t *) arg;

			if (len < (int)(in_rs->count + sizeof(in_rs->count))) {
				bcmerror = BCME_BUFTOOSHORT;
				break;
			}

			if (in_rs->count > WLC_NUMRATES) {
				bcmerror = BCME_BUFTOOLONG;
				break;
			}

			memset(&rs, 0, sizeof(wlc_rateset_t));

			/* Copy only legacy rateset section */
			rs.count = in_rs->count;
			bcopy(&in_rs->rates, &rs.rates, rs.count);

			/* merge rateset coming in with the current mcsset */
			if (N_ENAB(wlc->pub)) {
				if (bsscfg->associated)
					bcopy(&current_bss->rateset.mcs[0],
					      rs.mcs, MCSSET_LEN);
				else
					bcopy(&wlc->default_bss->rateset.mcs[0],
					      rs.mcs, MCSSET_LEN);
			}

			bcmerror = wlc_set_rateset(wlc, &rs);

			if (!bcmerror)
				wlc_ofdm_rateset_war(wlc);

			break;
		}

	case WLC_GET_BCNPRD:
		if (BSSCFG_STA(bsscfg) && bsscfg->BSS && bsscfg->associated)
			*pval = current_bss->beacon_period;
		else
			*pval = wlc->default_bss->beacon_period;
		break;

	case WLC_SET_BCNPRD:
		/* range [1, 0xffff] */
		if (val >= DOT11_MIN_BEACON_PERIOD
		    && val <= DOT11_MAX_BEACON_PERIOD) {
			wlc->default_bss->beacon_period = (u16) val;
		} else
			bcmerror = BCME_RANGE;
		break;

	case WLC_GET_DTIMPRD:
		if (BSSCFG_STA(bsscfg) && bsscfg->BSS && bsscfg->associated)
			*pval = current_bss->dtim_period;
		else
			*pval = wlc->default_bss->dtim_period;
		break;

	case WLC_SET_DTIMPRD:
		/* range [1, 0xff] */
		if (val >= DOT11_MIN_DTIM_PERIOD
		    && val <= DOT11_MAX_DTIM_PERIOD) {
			wlc->default_bss->dtim_period = (u8) val;
		} else
			bcmerror = BCME_RANGE;
		break;

#ifdef SUPPORT_PS
	case WLC_GET_PM:
		*pval = wlc->PM;
		break;

	case WLC_SET_PM:
		if ((val >= PM_OFF) && (val <= PM_MAX)) {
			wlc->PM = (u8) val;
			if (wlc->pub->up) {
			}
			/* Change watchdog driver to align watchdog with tbtt if possible */
			wlc_watchdog_upd(wlc, PS_ALLOWED(wlc));
		} else
			bcmerror = BCME_ERROR;
		break;
#endif				/* SUPPORT_PS */

#ifdef SUPPORT_PS
#ifdef BCMDBG
	case WLC_GET_WAKE:
		if (AP_ENAB(wlc->pub)) {
			bcmerror = BCME_NOTSTA;
			break;
		}
		*pval = wlc->wake;
		break;

	case WLC_SET_WAKE:
		if (AP_ENAB(wlc->pub)) {
			bcmerror = BCME_NOTSTA;
			break;
		}

		wlc->wake = val ? true : false;

		/* if down, we're done */
		if (!wlc->pub->up)
			break;

		/* apply to the mac */
		wlc_set_ps_ctrl(wlc);
		break;
#endif				/* BCMDBG */
#endif				/* SUPPORT_PS */

	case WLC_GET_REVINFO:
		bcmerror = wlc_get_revision_info(wlc, arg, (uint) len);
		break;

	case WLC_GET_AP:
		*pval = (int)AP_ENAB(wlc->pub);
		break;

	case WLC_GET_ATIM:
		if (bsscfg->associated)
			*pval = (int)current_bss->atim_window;
		else
			*pval = (int)wlc->default_bss->atim_window;
		break;

	case WLC_SET_ATIM:
		wlc->default_bss->atim_window = (u32) val;
		break;

	case WLC_GET_PKTCNTS:{
			get_pktcnt_t *pktcnt = (get_pktcnt_t *) pval;
			if (WLC_UPDATE_STATS(wlc))
				wlc_statsupd(wlc);
			pktcnt->rx_good_pkt = WLCNTVAL(wlc->pub->_cnt->rxframe);
			pktcnt->rx_bad_pkt = WLCNTVAL(wlc->pub->_cnt->rxerror);
			pktcnt->tx_good_pkt =
			    WLCNTVAL(wlc->pub->_cnt->txfrmsnt);
			pktcnt->tx_bad_pkt =
			    WLCNTVAL(wlc->pub->_cnt->txerror) +
			    WLCNTVAL(wlc->pub->_cnt->txfail);
			if (len >= (int)sizeof(get_pktcnt_t)) {
				/* Be backward compatible - only if buffer is large enough  */
				pktcnt->rx_ocast_good_pkt =
				    WLCNTVAL(wlc->pub->_cnt->rxmfrmocast);
			}
			break;
		}

#ifdef SUPPORT_HWKEY
	case WLC_GET_WSEC:
		bcmerror =
		    wlc_iovar_op(wlc, "wsec", NULL, 0, arg, len, IOV_GET,
				 wlcif);
		break;

	case WLC_SET_WSEC:
		bcmerror =
		    wlc_iovar_op(wlc, "wsec", NULL, 0, arg, len, IOV_SET,
				 wlcif);
		break;

	case WLC_GET_WPA_AUTH:
		*pval = (int)bsscfg->WPA_auth;
		break;

	case WLC_SET_WPA_AUTH:
		/* change of WPA_Auth modifies the PS_ALLOWED state */
		if (BSSCFG_STA(bsscfg)) {
			bsscfg->WPA_auth = (u16) val;
		} else
			bsscfg->WPA_auth = (u16) val;
		break;
#endif				/* SUPPORT_HWKEY */

	case WLC_GET_BANDLIST:
		/* count of number of bands, followed by each band type */
		*pval++ = NBANDS(wlc);
		*pval++ = wlc->band->bandtype;
		if (NBANDS(wlc) > 1)
			*pval++ = wlc->bandstate[OTHERBANDUNIT(wlc)]->bandtype;
		break;

	case WLC_GET_BAND:
		*pval = wlc->bandlocked ? wlc->band->bandtype : WLC_BAND_AUTO;
		break;

	case WLC_GET_PHYLIST:
		{
			unsigned char *cp = arg;
			if (len < 3) {
				bcmerror = BCME_BUFTOOSHORT;
				break;
			}

			if (WLCISNPHY(wlc->band)) {
				*cp++ = 'n';
			} else if (WLCISLCNPHY(wlc->band)) {
				*cp++ = 'c';
			} else if (WLCISSSLPNPHY(wlc->band)) {
				*cp++ = 's';
			}
			*cp = '\0';
			break;
		}

	case WLC_GET_SHORTSLOT:
		*pval = wlc->shortslot;
		break;

	case WLC_GET_SHORTSLOT_OVERRIDE:
		*pval = wlc->shortslot_override;
		break;

	case WLC_SET_SHORTSLOT_OVERRIDE:
		if ((val != WLC_SHORTSLOT_AUTO) &&
		    (val != WLC_SHORTSLOT_OFF) && (val != WLC_SHORTSLOT_ON)) {
			bcmerror = BCME_RANGE;
			break;
		}

		wlc->shortslot_override = (s8) val;

		/* shortslot is an 11g feature, so no more work if we are
		 * currently on the 5G band
		 */
		if (BAND_5G(wlc->band->bandtype))
			break;

		if (wlc->pub->up && wlc->pub->associated) {
			/* let watchdog or beacon processing update shortslot */
		} else if (wlc->pub->up) {
			/* unassociated shortslot is off */
			wlc_switch_shortslot(wlc, false);
		} else {
			/* driver is down, so just update the wlc_info value */
			if (wlc->shortslot_override == WLC_SHORTSLOT_AUTO) {
				wlc->shortslot = false;
			} else {
				wlc->shortslot =
				    (wlc->shortslot_override ==
				     WLC_SHORTSLOT_ON);
			}
		}

		break;

	case WLC_GET_LEGACY_ERP:
		*pval = wlc->include_legacy_erp;
		break;

	case WLC_SET_LEGACY_ERP:
		if (wlc->include_legacy_erp == bool_val)
			break;

		wlc->include_legacy_erp = bool_val;

		if (AP_ENAB(wlc->pub) && wlc->clk) {
			wlc_update_beacon(wlc);
			wlc_update_probe_resp(wlc, true);
		}
		break;

	case WLC_GET_GMODE:
		if (wlc->band->bandtype == WLC_BAND_2G)
			*pval = wlc->band->gmode;
		else if (NBANDS(wlc) > 1)
			*pval = wlc->bandstate[OTHERBANDUNIT(wlc)]->gmode;
		break;

	case WLC_SET_GMODE:
		if (!wlc->pub->associated)
			bcmerror = wlc_set_gmode(wlc, (u8) val, true);
		else {
			bcmerror = BCME_ASSOCIATED;
			break;
		}
		break;

	case WLC_GET_GMODE_PROTECTION:
		*pval = wlc->protection->_g;
		break;

	case WLC_GET_PROTECTION_CONTROL:
		*pval = wlc->protection->overlap;
		break;

	case WLC_SET_PROTECTION_CONTROL:
		if ((val != WLC_PROTECTION_CTL_OFF) &&
		    (val != WLC_PROTECTION_CTL_LOCAL) &&
		    (val != WLC_PROTECTION_CTL_OVERLAP)) {
			bcmerror = BCME_RANGE;
			break;
		}

		wlc_protection_upd(wlc, WLC_PROT_OVERLAP, (s8) val);

		/* Current g_protection will sync up to the specified control alg in watchdog
		 * if the driver is up and associated.
		 * If the driver is down or not associated, the control setting has no effect.
		 */
		break;

	case WLC_GET_GMODE_PROTECTION_OVERRIDE:
		*pval = wlc->protection->g_override;
		break;

	case WLC_SET_GMODE_PROTECTION_OVERRIDE:
		if ((val != WLC_PROTECTION_AUTO) &&
		    (val != WLC_PROTECTION_OFF) && (val != WLC_PROTECTION_ON)) {
			bcmerror = BCME_RANGE;
			break;
		}

		wlc_protection_upd(wlc, WLC_PROT_G_OVR, (s8) val);

		break;

	case WLC_SET_SUP_RATESET_OVERRIDE:{
			wlc_rateset_t rs, new;

			/* copyin */
			if (len < (int)sizeof(wlc_rateset_t)) {
				bcmerror = BCME_BUFTOOSHORT;
				break;
			}
			bcopy((char *)arg, (char *)&rs, sizeof(wlc_rateset_t));

			/* check for bad count value */
			if (rs.count > WLC_NUMRATES) {
				bcmerror = BCME_BADRATESET;	/* invalid rateset */
				break;
			}

			/* this command is only appropriate for gmode operation */
			if (!(wlc->band->gmode ||
			      ((NBANDS(wlc) > 1)
			       && wlc->bandstate[OTHERBANDUNIT(wlc)]->gmode))) {
				bcmerror = BCME_BADBAND;	/* gmode only command when not in gmode */
				break;
			}

			/* check for an empty rateset to clear the override */
			if (rs.count == 0) {
				memset(&wlc->sup_rates_override, 0,
				      sizeof(wlc_rateset_t));
				break;
			}

			/* validate rateset by comparing pre and post sorted against 11g hw rates */
			wlc_rateset_filter(&rs, &new, false, WLC_RATES_CCK_OFDM,
					   RATE_MASK, BSS_N_ENAB(wlc, bsscfg));
			wlc_rate_hwrs_filter_sort_validate(&new,
							   &cck_ofdm_rates,
							   false,
							   wlc->stf->txstreams);
			if (rs.count != new.count) {
				bcmerror = BCME_BADRATESET;	/* invalid rateset */
				break;
			}

			/* apply new rateset to the override */
			bcopy((char *)&new, (char *)&wlc->sup_rates_override,
			      sizeof(wlc_rateset_t));

			/* update bcn and probe resp if needed */
			if (wlc->pub->up && AP_ENAB(wlc->pub)
			    && wlc->pub->associated) {
				wlc_update_beacon(wlc);
				wlc_update_probe_resp(wlc, true);
			}
			break;
		}

	case WLC_GET_SUP_RATESET_OVERRIDE:
		/* this command is only appropriate for gmode operation */
		if (!(wlc->band->gmode ||
		      ((NBANDS(wlc) > 1)
		       && wlc->bandstate[OTHERBANDUNIT(wlc)]->gmode))) {
			bcmerror = BCME_BADBAND;	/* gmode only command when not in gmode */
			break;
		}
		if (len < (int)sizeof(wlc_rateset_t)) {
			bcmerror = BCME_BUFTOOSHORT;
			break;
		}
		bcopy((char *)&wlc->sup_rates_override, (char *)arg,
		      sizeof(wlc_rateset_t));

		break;

	case WLC_GET_PRB_RESP_TIMEOUT:
		*pval = wlc->prb_resp_timeout;
		break;

	case WLC_SET_PRB_RESP_TIMEOUT:
		if (wlc->pub->up) {
			bcmerror = BCME_NOTDOWN;
			break;
		}
		if (val < 0 || val >= 0xFFFF) {
			bcmerror = BCME_RANGE;	/* bad value */
			break;
		}
		wlc->prb_resp_timeout = (u16) val;
		break;

	case WLC_GET_KEY_PRIMARY:{
			wsec_key_t *key;

			/* treat the 'val' parm as the key id */
			key = WSEC_BSS_DEFAULT_KEY(bsscfg);
			if (key != NULL) {
				*pval = key->id == val ? true : false;
			} else {
				bcmerror = BCME_BADKEYIDX;
			}
			break;
		}

	case WLC_SET_KEY_PRIMARY:{
			wsec_key_t *key, *old_key;

			bcmerror = BCME_BADKEYIDX;

			/* treat the 'val' parm as the key id */
			for (i = 0; i < WSEC_MAX_DEFAULT_KEYS; i++) {
				key = bsscfg->bss_def_keys[i];
				if (key != NULL && key->id == val) {
					old_key = WSEC_BSS_DEFAULT_KEY(bsscfg);
					if (old_key != NULL)
						old_key->flags &=
						    ~WSEC_PRIMARY_KEY;
					key->flags |= WSEC_PRIMARY_KEY;
					bsscfg->wsec_index = i;
					bcmerror = BCME_OK;
				}
			}
			break;
		}

#ifdef BCMDBG
	case WLC_INIT:
		wl_init(wlc->wl);
		break;
#endif

	case WLC_SET_VAR:
	case WLC_GET_VAR:{
			char *name;
			/* validate the name value */
			name = (char *)arg;
			for (i = 0; i < (uint) len && *name != '\0';
			     i++, name++)
				;

			if (i == (uint) len) {
				bcmerror = BCME_BUFTOOSHORT;
				break;
			}
			i++;	/* include the null in the string length */

			if (cmd == WLC_GET_VAR) {
				bcmerror =
				    wlc_iovar_op(wlc, arg,
						 (void *)((s8 *) arg + i),
						 len - i, arg, len, IOV_GET,
						 wlcif);
			} else
				bcmerror =
				    wlc_iovar_op(wlc, arg, NULL, 0,
						 (void *)((s8 *) arg + i),
						 len - i, IOV_SET, wlcif);

			break;
		}

	case WLC_SET_WSEC_PMK:
		bcmerror = BCME_UNSUPPORTED;
		break;

#if defined(BCMDBG)
	case WLC_CURRENT_PWR:
		if (!wlc->pub->up)
			bcmerror = BCME_NOTUP;
		else
			bcmerror = wlc_get_current_txpwr(wlc, arg, len);
		break;
#endif

	case WLC_LAST:
		WL_ERROR(("%s: WLC_LAST\n", __func__));
	}
 done:

	if (bcmerror) {
		if (VALID_BCMERROR(bcmerror))
			wlc->pub->bcmerror = bcmerror;
		else {
			bcmerror = 0;
		}

	}
	/* BMAC_NOTE: for HIGH_ONLY driver, this seems being called after RPC bus failed */
	/* In hw_off condition, IOCTLs that reach here are deemed safe but taclear would
	 * certainly result in getting -1 for register reads. So skip ta_clear altogether
	 */
	if (!(wlc->pub->hw_off))
		ASSERT(wlc_bmac_taclear(wlc->hw, ta_ok) || !ta_ok);

	return bcmerror;
}

#if defined(BCMDBG)
/* consolidated register access ioctl error checking */
int wlc_iocregchk(struct wlc_info *wlc, uint band)
{
	/* if band is specified, it must be the current band */
	if ((band != WLC_BAND_AUTO) && (band != (uint) wlc->band->bandtype))
		return BCME_BADBAND;

	/* if multiband and band is not specified, band must be locked */
	if ((band == WLC_BAND_AUTO) && IS_MBAND_UNLOCKED(wlc))
		return BCME_NOTBANDLOCKED;

	/* must have core clocks */
	if (!wlc->clk)
		return BCME_NOCLK;

	return 0;
}
#endif				/* defined(BCMDBG) */

#if defined(BCMDBG)
/* For some ioctls, make sure that the pi pointer matches the current phy */
int wlc_iocpichk(struct wlc_info *wlc, uint phytype)
{
	if (wlc->band->phytype != phytype)
		return BCME_BADBAND;
	return 0;
}
#endif

/* Look up the given var name in the given table */
static const bcm_iovar_t *wlc_iovar_lookup(const bcm_iovar_t *table,
					   const char *name)
{
	const bcm_iovar_t *vi;
	const char *lookup_name;

	/* skip any ':' delimited option prefixes */
	lookup_name = strrchr(name, ':');
	if (lookup_name != NULL)
		lookup_name++;
	else
		lookup_name = name;

	ASSERT(table != NULL);

	for (vi = table; vi->name; vi++) {
		if (!strcmp(vi->name, lookup_name))
			return vi;
	}
	/* ran to end of table */

	return NULL;		/* var name not found */
}

/* simplified integer get interface for common WLC_GET_VAR ioctl handler */
int wlc_iovar_getint(struct wlc_info *wlc, const char *name, int *arg)
{
	return wlc_iovar_op(wlc, name, NULL, 0, arg, sizeof(s32), IOV_GET,
			    NULL);
}

/* simplified integer set interface for common WLC_SET_VAR ioctl handler */
int wlc_iovar_setint(struct wlc_info *wlc, const char *name, int arg)
{
	return wlc_iovar_op(wlc, name, NULL, 0, (void *)&arg, sizeof(arg),
			    IOV_SET, NULL);
}

/* simplified s8 get interface for common WLC_GET_VAR ioctl handler */
int wlc_iovar_gets8(struct wlc_info *wlc, const char *name, s8 *arg)
{
	int iovar_int;
	int err;

	err =
	    wlc_iovar_op(wlc, name, NULL, 0, &iovar_int, sizeof(iovar_int),
			 IOV_GET, NULL);
	if (!err)
		*arg = (s8) iovar_int;

	return err;
}

/*
 * register iovar table, watchdog and down handlers.
 * calling function must keep 'iovars' until wlc_module_unregister is called.
 * 'iovar' must have the last entry's name field being NULL as terminator.
 */
int wlc_module_register(wlc_pub_t *pub, const bcm_iovar_t *iovars,
			const char *name, void *hdl, iovar_fn_t i_fn,
			watchdog_fn_t w_fn, down_fn_t d_fn)
{
	struct wlc_info *wlc = (struct wlc_info *) pub->wlc;
	int i;

	ASSERT(name != NULL);
	ASSERT(i_fn != NULL || w_fn != NULL || d_fn != NULL);

	/* find an empty entry and just add, no duplication check! */
	for (i = 0; i < WLC_MAXMODULES; i++) {
		if (wlc->modulecb[i].name[0] == '\0') {
			strncpy(wlc->modulecb[i].name, name,
				sizeof(wlc->modulecb[i].name) - 1);
			wlc->modulecb[i].iovars = iovars;
			wlc->modulecb[i].hdl = hdl;
			wlc->modulecb[i].iovar_fn = i_fn;
			wlc->modulecb[i].watchdog_fn = w_fn;
			wlc->modulecb[i].down_fn = d_fn;
			return 0;
		}
	}

	/* it is time to increase the capacity */
	ASSERT(i < WLC_MAXMODULES);
	return BCME_NORESOURCE;
}

/* unregister module callbacks */
int wlc_module_unregister(wlc_pub_t *pub, const char *name, void *hdl)
{
	struct wlc_info *wlc = (struct wlc_info *) pub->wlc;
	int i;

	if (wlc == NULL)
		return BCME_NOTFOUND;

	ASSERT(name != NULL);

	for (i = 0; i < WLC_MAXMODULES; i++) {
		if (!strcmp(wlc->modulecb[i].name, name) &&
		    (wlc->modulecb[i].hdl == hdl)) {
			memset(&wlc->modulecb[i], 0, sizeof(modulecb_t));
			return 0;
		}
	}

	/* table not found! */
	return BCME_NOTFOUND;
}

/* Write WME tunable parameters for retransmit/max rate from wlc struct to ucode */
static void wlc_wme_retries_write(struct wlc_info *wlc)
{
	int ac;

	/* Need clock to do this */
	if (!wlc->clk)
		return;

	for (ac = 0; ac < AC_COUNT; ac++) {
		wlc_write_shm(wlc, M_AC_TXLMT_ADDR(ac), wlc->wme_retries[ac]);
	}
}

/* Get or set an iovar.  The params/p_len pair specifies any additional
 * qualifying parameters (e.g. an "element index") for a get, while the
 * arg/len pair is the buffer for the value to be set or retrieved.
 * Operation (get/set) is specified by the last argument.
 * interface context provided by wlcif
 *
 * All pointers may point into the same buffer.
 */
int
wlc_iovar_op(struct wlc_info *wlc, const char *name,
	     void *params, int p_len, void *arg, int len,
	     bool set, struct wlc_if *wlcif)
{
	int err = 0;
	int val_size;
	const bcm_iovar_t *vi = NULL;
	u32 actionid;
	int i;

	ASSERT(name != NULL);

	ASSERT(len >= 0);

	/* Get MUST have return space */
	ASSERT(set || (arg && len));

	ASSERT(!(wlc->pub->hw_off && wlc->pub->up));

	/* Set does NOT take qualifiers */
	ASSERT(!set || (!params && !p_len));

	if (!set && (len == sizeof(int)) &&
	    !(IS_ALIGNED((unsigned long)(arg), (uint) sizeof(int)))) {
		WL_ERROR(("wl%d: %s unaligned get ptr for %s\n",
			  wlc->pub->unit, __func__, name));
		ASSERT(0);
	}

	/* find the given iovar name */
	for (i = 0; i < WLC_MAXMODULES; i++) {
		if (!wlc->modulecb[i].iovars)
			continue;
		vi = wlc_iovar_lookup(wlc->modulecb[i].iovars, name);
		if (vi)
			break;
	}
	/* iovar name not found */
	if (i >= WLC_MAXMODULES) {
		err = BCME_UNSUPPORTED;
		goto exit;
	}

	/* set up 'params' pointer in case this is a set command so that
	 * the convenience int and bool code can be common to set and get
	 */
	if (params == NULL) {
		params = arg;
		p_len = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		/* all other types are integer sized */
		val_size = sizeof(int);

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);

	/* Do the actual parameter implementation */
	err = wlc->modulecb[i].iovar_fn(wlc->modulecb[i].hdl, vi, actionid,
					name, params, p_len, arg, len, val_size,
					wlcif);

 exit:
	return err;
}

int
wlc_iovar_check(wlc_pub_t *pub, const bcm_iovar_t *vi, void *arg, int len,
		bool set)
{
	struct wlc_info *wlc = (struct wlc_info *) pub->wlc;
	int err = 0;
	s32 int_val = 0;

	/* check generic condition flags */
	if (set) {
		if (((vi->flags & IOVF_SET_DOWN) && wlc->pub->up) ||
		    ((vi->flags & IOVF_SET_UP) && !wlc->pub->up)) {
			err = (wlc->pub->up ? BCME_NOTDOWN : BCME_NOTUP);
		} else if ((vi->flags & IOVF_SET_BAND)
			   && IS_MBAND_UNLOCKED(wlc)) {
			err = BCME_NOTBANDLOCKED;
		} else if ((vi->flags & IOVF_SET_CLK) && !wlc->clk) {
			err = BCME_NOCLK;
		}
	} else {
		if (((vi->flags & IOVF_GET_DOWN) && wlc->pub->up) ||
		    ((vi->flags & IOVF_GET_UP) && !wlc->pub->up)) {
			err = (wlc->pub->up ? BCME_NOTDOWN : BCME_NOTUP);
		} else if ((vi->flags & IOVF_GET_BAND)
			   && IS_MBAND_UNLOCKED(wlc)) {
			err = BCME_NOTBANDLOCKED;
		} else if ((vi->flags & IOVF_GET_CLK) && !wlc->clk) {
			err = BCME_NOCLK;
		}
	}

	if (err)
		goto exit;

	/* length check on io buf */
	err = bcm_iovar_lencheck(vi, arg, len, set);
	if (err)
		goto exit;

	/* On set, check value ranges for integer types */
	if (set) {
		switch (vi->type) {
		case IOVT_BOOL:
		case IOVT_INT8:
		case IOVT_INT16:
		case IOVT_INT32:
		case IOVT_UINT8:
		case IOVT_UINT16:
		case IOVT_UINT32:
			bcopy(arg, &int_val, sizeof(int));
			err = wlc_iovar_rangecheck(wlc, int_val, vi);
			break;
		}
	}
 exit:
	return err;
}

/* handler for iovar table wlc_iovars */
/*
 * IMPLEMENTATION NOTE: In order to avoid checking for get/set in each
 * iovar case, the switch statement maps the iovar id into separate get
 * and set values.  If you add a new iovar to the switch you MUST use
 * IOV_GVAL and/or IOV_SVAL in the case labels to avoid conflict with
 * another case.
 * Please use params for additional qualifying parameters.
 */
int
wlc_doiovar(void *hdl, const bcm_iovar_t *vi, u32 actionid,
	    const char *name, void *params, uint p_len, void *arg, int len,
	    int val_size, struct wlc_if *wlcif)
{
	struct wlc_info *wlc = hdl;
	wlc_bsscfg_t *bsscfg;
	int err = 0;
	s32 int_val = 0;
	s32 int_val2 = 0;
	s32 *ret_int_ptr;
	bool bool_val;
	bool bool_val2;
	wlc_bss_info_t *current_bss;

	WL_TRACE(("wl%d: %s\n", wlc->pub->unit, __func__));

	bsscfg = NULL;
	current_bss = NULL;

	err = wlc_iovar_check(wlc->pub, vi, arg, len, IOV_ISSET(actionid));
	if (err != 0)
		return err;

	/* convenience int and bool vals for first 8 bytes of buffer */
	if (p_len >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	if (p_len >= (int)sizeof(int_val) * 2)
		bcopy((void *)((unsigned long)params + sizeof(int_val)), &int_val2,
		      sizeof(int_val));

	/* convenience int ptr for 4-byte gets (requires int aligned arg) */
	ret_int_ptr = (s32 *) arg;

	bool_val = (int_val != 0) ? true : false;
	bool_val2 = (int_val2 != 0) ? true : false;

	WL_TRACE(("wl%d: %s: id %d\n", wlc->pub->unit, __func__,
		  IOV_ID(actionid)));
	/* Do the actual parameter implementation */
	switch (actionid) {

	case IOV_GVAL(IOV_QTXPOWER):{
			uint qdbm;
			bool override;

			err = wlc_phy_txpower_get(wlc->band->pi, &qdbm,
				&override);
			if (err != BCME_OK)
				return err;

			/* Return qdbm units */
			*ret_int_ptr =
			    qdbm | (override ? WL_TXPWR_OVERRIDE : 0);
			break;
		}

		/* As long as override is false, this only sets the *user* targets.
		   User can twiddle this all he wants with no harm.
		   wlc_phy_txpower_set() explicitly sets override to false if
		   not internal or test.
		 */
	case IOV_SVAL(IOV_QTXPOWER):{
			u8 qdbm;
			bool override;

			/* Remove override bit and clip to max qdbm value */
			qdbm = (u8)min_t(u32, (int_val & ~WL_TXPWR_OVERRIDE), 0xff);
			/* Extract override setting */
			override = (int_val & WL_TXPWR_OVERRIDE) ? true : false;
			err =
			    wlc_phy_txpower_set(wlc->band->pi, qdbm, override);
			break;
		}

	case IOV_GVAL(IOV_MPC):
		*ret_int_ptr = (s32) wlc->mpc;
		break;

	case IOV_SVAL(IOV_MPC):
		wlc->mpc = bool_val;
		wlc_radio_mpc_upd(wlc);

		break;

	case IOV_GVAL(IOV_BCN_LI_BCN):
		*ret_int_ptr = wlc->bcn_li_bcn;
		break;

	case IOV_SVAL(IOV_BCN_LI_BCN):
		wlc->bcn_li_bcn = (u8) int_val;
		if (wlc->pub->up)
			wlc_bcn_li_upd(wlc);
		break;

	default:
		WL_ERROR(("wl%d: %s: unsupported\n", wlc->pub->unit, __func__));
		err = BCME_UNSUPPORTED;
		break;
	}

	goto exit;		/* avoid unused label warning */

 exit:
	return err;
}

static int
wlc_iovar_rangecheck(struct wlc_info *wlc, u32 val, const bcm_iovar_t *vi)
{
	int err = 0;
	u32 min_val = 0;
	u32 max_val = 0;

	/* Only ranged integers are checked */
	switch (vi->type) {
	case IOVT_INT32:
		max_val |= 0x7fffffff;
		/* fall through */
	case IOVT_INT16:
		max_val |= 0x00007fff;
		/* fall through */
	case IOVT_INT8:
		max_val |= 0x0000007f;
		min_val = ~max_val;
		if (vi->flags & IOVF_NTRL)
			min_val = 1;
		else if (vi->flags & IOVF_WHL)
			min_val = 0;
		/* Signed values are checked against max_val and min_val */
		if ((s32) val < (s32) min_val
		    || (s32) val > (s32) max_val)
			err = BCME_RANGE;
		break;

	case IOVT_UINT32:
		max_val |= 0xffffffff;
		/* fall through */
	case IOVT_UINT16:
		max_val |= 0x0000ffff;
		/* fall through */
	case IOVT_UINT8:
		max_val |= 0x000000ff;
		if (vi->flags & IOVF_NTRL)
			min_val = 1;
		if ((val < min_val) || (val > max_val))
			err = BCME_RANGE;
		break;
	}

	return err;
}

#ifdef BCMDBG
static const char *supr_reason[] = {
	"None", "PMQ Entry", "Flush request",
	"Previous frag failure", "Channel mismatch",
	"Lifetime Expiry", "Underflow"
};

static void wlc_print_txs_status(u16 s)
{
	printf("[15:12]  %d  frame attempts\n", (s & TX_STATUS_FRM_RTX_MASK) >>
	       TX_STATUS_FRM_RTX_SHIFT);
	printf(" [11:8]  %d  rts attempts\n", (s & TX_STATUS_RTS_RTX_MASK) >>
	       TX_STATUS_RTS_RTX_SHIFT);
	printf("    [7]  %d  PM mode indicated\n",
	       ((s & TX_STATUS_PMINDCTD) ? 1 : 0));
	printf("    [6]  %d  intermediate status\n",
	       ((s & TX_STATUS_INTERMEDIATE) ? 1 : 0));
	printf("    [5]  %d  AMPDU\n", (s & TX_STATUS_AMPDU) ? 1 : 0);
	printf("  [4:2]  %d  Frame Suppressed Reason (%s)\n",
	       ((s & TX_STATUS_SUPR_MASK) >> TX_STATUS_SUPR_SHIFT),
	       supr_reason[(s & TX_STATUS_SUPR_MASK) >> TX_STATUS_SUPR_SHIFT]);
	printf("    [1]  %d  acked\n", ((s & TX_STATUS_ACK_RCV) ? 1 : 0));
}
#endif				/* BCMDBG */

void wlc_print_txstatus(tx_status_t *txs)
{
#if defined(BCMDBG)
	u16 s = txs->status;
	u16 ackphyrxsh = txs->ackphyrxsh;

	printf("\ntxpkt (MPDU) Complete\n");

	printf("FrameID: %04x   ", txs->frameid);
	printf("TxStatus: %04x", s);
	printf("\n");
#ifdef BCMDBG
	wlc_print_txs_status(s);
#endif
	printf("LastTxTime: %04x ", txs->lasttxtime);
	printf("Seq: %04x ", txs->sequence);
	printf("PHYTxStatus: %04x ", txs->phyerr);
	printf("RxAckRSSI: %04x ",
	       (ackphyrxsh & PRXS1_JSSI_MASK) >> PRXS1_JSSI_SHIFT);
	printf("RxAckSQ: %04x", (ackphyrxsh & PRXS1_SQ_MASK) >> PRXS1_SQ_SHIFT);
	printf("\n");
#endif				/* defined(BCMDBG) */
}

#define MACSTATUPD(name) \
	wlc_ctrupd_cache(macstats.name, &wlc->core->macstat_snapshot->name, &wlc->pub->_cnt->name)

void wlc_statsupd(struct wlc_info *wlc)
{
	int i;
#ifdef BCMDBG
	u16 delta;
	u16 rxf0ovfl;
	u16 txfunfl[NFIFO];
#endif				/* BCMDBG */

	/* if driver down, make no sense to update stats */
	if (!wlc->pub->up)
		return;

#ifdef BCMDBG
	/* save last rx fifo 0 overflow count */
	rxf0ovfl = wlc->core->macstat_snapshot->rxf0ovfl;

	/* save last tx fifo  underflow count */
	for (i = 0; i < NFIFO; i++)
		txfunfl[i] = wlc->core->macstat_snapshot->txfunfl[i];
#endif				/* BCMDBG */

#ifdef BCMDBG
	/* check for rx fifo 0 overflow */
	delta = (u16) (wlc->core->macstat_snapshot->rxf0ovfl - rxf0ovfl);
	if (delta)
		WL_ERROR(("wl%d: %u rx fifo 0 overflows!\n", wlc->pub->unit,
			  delta));

	/* check for tx fifo underflows */
	for (i = 0; i < NFIFO; i++) {
		delta =
		    (u16) (wlc->core->macstat_snapshot->txfunfl[i] -
			      txfunfl[i]);
		if (delta)
			WL_ERROR(("wl%d: %u tx fifo %d underflows!\n",
				  wlc->pub->unit, delta, i));
	}
#endif				/* BCMDBG */

	/* dot11 counter update */

	WLCNTSET(wlc->pub->_cnt->txrts,
		 (wlc->pub->_cnt->rxctsucast -
		  wlc->pub->_cnt->d11cnt_txrts_off));
	WLCNTSET(wlc->pub->_cnt->rxcrc,
		 (wlc->pub->_cnt->rxbadfcs - wlc->pub->_cnt->d11cnt_rxcrc_off));
	WLCNTSET(wlc->pub->_cnt->txnocts,
		 ((wlc->pub->_cnt->txrtsfrm - wlc->pub->_cnt->rxctsucast) -
		  wlc->pub->_cnt->d11cnt_txnocts_off));

	/* merge counters from dma module */
	for (i = 0; i < NFIFO; i++) {
		if (wlc->hw->di[i]) {
			WLCNTADD(wlc->pub->_cnt->txnobuf,
				 (wlc->hw->di[i])->txnobuf);
			WLCNTADD(wlc->pub->_cnt->rxnobuf,
				 (wlc->hw->di[i])->rxnobuf);
			WLCNTADD(wlc->pub->_cnt->rxgiant,
				 (wlc->hw->di[i])->rxgiants);
			dma_counterreset(wlc->hw->di[i]);
		}
	}

	/*
	 * Aggregate transmit and receive errors that probably resulted
	 * in the loss of a frame are computed on the fly.
	 */
	WLCNTSET(wlc->pub->_cnt->txerror,
		 wlc->pub->_cnt->txnobuf + wlc->pub->_cnt->txnoassoc +
		 wlc->pub->_cnt->txuflo + wlc->pub->_cnt->txrunt +
		 wlc->pub->_cnt->dmade + wlc->pub->_cnt->dmada +
		 wlc->pub->_cnt->dmape);
	WLCNTSET(wlc->pub->_cnt->rxerror,
		 wlc->pub->_cnt->rxoflo + wlc->pub->_cnt->rxnobuf +
		 wlc->pub->_cnt->rxfragerr + wlc->pub->_cnt->rxrunt +
		 wlc->pub->_cnt->rxgiant + wlc->pub->_cnt->rxnoscb +
		 wlc->pub->_cnt->rxbadsrcmac);
	for (i = 0; i < NFIFO; i++)
		WLCNTADD(wlc->pub->_cnt->rxerror, wlc->pub->_cnt->rxuflo[i]);
}

bool wlc_chipmatch(u16 vendor, u16 device)
{
	if (vendor != VENDOR_BROADCOM) {
		WL_ERROR(("wlc_chipmatch: unknown vendor id %04x\n", vendor));
		return false;
	}

	if ((device == BCM43224_D11N_ID) || (device == BCM43225_D11N2G_ID))
		return true;

	if (device == BCM4313_D11N2G_ID)
		return true;
	if ((device == BCM43236_D11N_ID) || (device == BCM43236_D11N2G_ID))
		return true;

	WL_ERROR(("wlc_chipmatch: unknown device id %04x\n", device));
	return false;
}

#if defined(BCMDBG)
void wlc_print_txdesc(d11txh_t *txh)
{
	u16 mtcl = ltoh16(txh->MacTxControlLow);
	u16 mtch = ltoh16(txh->MacTxControlHigh);
	u16 mfc = ltoh16(txh->MacFrameControl);
	u16 tfest = ltoh16(txh->TxFesTimeNormal);
	u16 ptcw = ltoh16(txh->PhyTxControlWord);
	u16 ptcw_1 = ltoh16(txh->PhyTxControlWord_1);
	u16 ptcw_1_Fbr = ltoh16(txh->PhyTxControlWord_1_Fbr);
	u16 ptcw_1_Rts = ltoh16(txh->PhyTxControlWord_1_Rts);
	u16 ptcw_1_FbrRts = ltoh16(txh->PhyTxControlWord_1_FbrRts);
	u16 mainrates = ltoh16(txh->MainRates);
	u16 xtraft = ltoh16(txh->XtraFrameTypes);
	u8 *iv = txh->IV;
	u8 *ra = txh->TxFrameRA;
	u16 tfestfb = ltoh16(txh->TxFesTimeFallback);
	u8 *rtspfb = txh->RTSPLCPFallback;
	u16 rtsdfb = ltoh16(txh->RTSDurFallback);
	u8 *fragpfb = txh->FragPLCPFallback;
	u16 fragdfb = ltoh16(txh->FragDurFallback);
	u16 mmodelen = ltoh16(txh->MModeLen);
	u16 mmodefbrlen = ltoh16(txh->MModeFbrLen);
	u16 tfid = ltoh16(txh->TxFrameID);
	u16 txs = ltoh16(txh->TxStatus);
	u16 mnmpdu = ltoh16(txh->MaxNMpdus);
	u16 mabyte = ltoh16(txh->MaxABytes_MRT);
	u16 mabyte_f = ltoh16(txh->MaxABytes_FBR);
	u16 mmbyte = ltoh16(txh->MinMBytes);

	u8 *rtsph = txh->RTSPhyHeader;
	struct dot11_rts_frame rts = txh->rts_frame;
	char hexbuf[256];

	/* add plcp header along with txh descriptor */
	prhex("Raw TxDesc + plcp header", (unsigned char *) txh, sizeof(d11txh_t) + 48);

	printf("TxCtlLow: %04x ", mtcl);
	printf("TxCtlHigh: %04x ", mtch);
	printf("FC: %04x ", mfc);
	printf("FES Time: %04x\n", tfest);
	printf("PhyCtl: %04x%s ", ptcw,
	       (ptcw & PHY_TXC_SHORT_HDR) ? " short" : "");
	printf("PhyCtl_1: %04x ", ptcw_1);
	printf("PhyCtl_1_Fbr: %04x\n", ptcw_1_Fbr);
	printf("PhyCtl_1_Rts: %04x ", ptcw_1_Rts);
	printf("PhyCtl_1_Fbr_Rts: %04x\n", ptcw_1_FbrRts);
	printf("MainRates: %04x ", mainrates);
	printf("XtraFrameTypes: %04x ", xtraft);
	printf("\n");

	bcm_format_hex(hexbuf, iv, sizeof(txh->IV));
	printf("SecIV:       %s\n", hexbuf);
	bcm_format_hex(hexbuf, ra, sizeof(txh->TxFrameRA));
	printf("RA:          %s\n", hexbuf);

	printf("Fb FES Time: %04x ", tfestfb);
	bcm_format_hex(hexbuf, rtspfb, sizeof(txh->RTSPLCPFallback));
	printf("RTS PLCP: %s ", hexbuf);
	printf("RTS DUR: %04x ", rtsdfb);
	bcm_format_hex(hexbuf, fragpfb, sizeof(txh->FragPLCPFallback));
	printf("PLCP: %s ", hexbuf);
	printf("DUR: %04x", fragdfb);
	printf("\n");

	printf("MModeLen: %04x ", mmodelen);
	printf("MModeFbrLen: %04x\n", mmodefbrlen);

	printf("FrameID:     %04x\n", tfid);
	printf("TxStatus:    %04x\n", txs);

	printf("MaxNumMpdu:  %04x\n", mnmpdu);
	printf("MaxAggbyte:  %04x\n", mabyte);
	printf("MaxAggbyte_fb:  %04x\n", mabyte_f);
	printf("MinByte:     %04x\n", mmbyte);

	bcm_format_hex(hexbuf, rtsph, sizeof(txh->RTSPhyHeader));
	printf("RTS PLCP: %s ", hexbuf);
	bcm_format_hex(hexbuf, (u8 *) &rts, sizeof(txh->rts_frame));
	printf("RTS Frame: %s", hexbuf);
	printf("\n");

}
#endif				/* defined(BCMDBG) */

#if defined(BCMDBG)
void wlc_print_rxh(d11rxhdr_t *rxh)
{
	u16 len = rxh->RxFrameSize;
	u16 phystatus_0 = rxh->PhyRxStatus_0;
	u16 phystatus_1 = rxh->PhyRxStatus_1;
	u16 phystatus_2 = rxh->PhyRxStatus_2;
	u16 phystatus_3 = rxh->PhyRxStatus_3;
	u16 macstatus1 = rxh->RxStatus1;
	u16 macstatus2 = rxh->RxStatus2;
	char flagstr[64];
	char lenbuf[20];
	static const bcm_bit_desc_t macstat_flags[] = {
		{RXS_FCSERR, "FCSErr"},
		{RXS_RESPFRAMETX, "Reply"},
		{RXS_PBPRES, "PADDING"},
		{RXS_DECATMPT, "DeCr"},
		{RXS_DECERR, "DeCrErr"},
		{RXS_BCNSENT, "Bcn"},
		{0, NULL}
	};

	prhex("Raw RxDesc", (unsigned char *) rxh, sizeof(d11rxhdr_t));

	bcm_format_flags(macstat_flags, macstatus1, flagstr, 64);

	snprintf(lenbuf, sizeof(lenbuf), "0x%x", len);

	printf("RxFrameSize:     %6s (%d)%s\n", lenbuf, len,
	       (rxh->PhyRxStatus_0 & PRXS0_SHORTH) ? " short preamble" : "");
	printf("RxPHYStatus:     %04x %04x %04x %04x\n",
	       phystatus_0, phystatus_1, phystatus_2, phystatus_3);
	printf("RxMACStatus:     %x %s\n", macstatus1, flagstr);
	printf("RXMACaggtype: %x\n", (macstatus2 & RXS_AGGTYPE_MASK));
	printf("RxTSFTime:       %04x\n", rxh->RxTSFTime);
}
#endif				/* defined(BCMDBG) */

#if defined(BCMDBG)
int wlc_format_ssid(char *buf, const unsigned char ssid[], uint ssid_len)
{
	uint i, c;
	char *p = buf;
	char *endp = buf + SSID_FMT_BUF_LEN;

	if (ssid_len > DOT11_MAX_SSID_LEN)
		ssid_len = DOT11_MAX_SSID_LEN;

	for (i = 0; i < ssid_len; i++) {
		c = (uint) ssid[i];
		if (c == '\\') {
			*p++ = '\\';
			*p++ = '\\';
		} else if (isprint((unsigned char) c)) {
			*p++ = (char)c;
		} else {
			p += snprintf(p, (endp - p), "\\x%02X", c);
		}
	}
	*p = '\0';
	ASSERT(p < endp);

	return (int)(p - buf);
}
#endif				/* defined(BCMDBG) */

u16 wlc_rate_shm_offset(struct wlc_info *wlc, u8 rate)
{
	return wlc_bmac_rate_shm_offset(wlc->hw, rate);
}

/* Callback for device removed */

/*
 * Attempts to queue a packet onto a multiple-precedence queue,
 * if necessary evicting a lower precedence packet from the queue.
 *
 * 'prec' is the precedence number that has already been mapped
 * from the packet priority.
 *
 * Returns true if packet consumed (queued), false if not.
 */
bool BCMFASTPATH
wlc_prec_enq(struct wlc_info *wlc, struct pktq *q, void *pkt, int prec)
{
	return wlc_prec_enq_head(wlc, q, pkt, prec, false);
}

bool BCMFASTPATH
wlc_prec_enq_head(struct wlc_info *wlc, struct pktq *q, struct sk_buff *pkt,
		  int prec, bool head)
{
	struct sk_buff *p;
	int eprec = -1;		/* precedence to evict from */

	/* Determine precedence from which to evict packet, if any */
	if (pktq_pfull(q, prec))
		eprec = prec;
	else if (pktq_full(q)) {
		p = pktq_peek_tail(q, &eprec);
		ASSERT(p != NULL);
		if (eprec > prec) {
			WL_ERROR(("%s: Failing: eprec %d > prec %d\n", __func__,
				  eprec, prec));
			return false;
		}
	}

	/* Evict if needed */
	if (eprec >= 0) {
		bool discard_oldest;

		/* Detect queueing to unconfigured precedence */
		ASSERT(!pktq_pempty(q, eprec));

		discard_oldest = AC_BITMAP_TST(wlc->wme_dp, eprec);

		/* Refuse newer packet unless configured to discard oldest */
		if (eprec == prec && !discard_oldest) {
			WL_ERROR(("%s: No where to go, prec == %d\n", __func__,
				  prec));
			return false;
		}

		/* Evict packet according to discard policy */
		p = discard_oldest ? pktq_pdeq(q, eprec) : pktq_pdeq_tail(q,
									  eprec);
		ASSERT(p != NULL);

		/* Increment wme stats */
		if (WME_ENAB(wlc->pub)) {
			WLCNTINCR(wlc->pub->_wme_cnt->
				  tx_failed[WME_PRIO2AC(p->priority)].packets);
			WLCNTADD(wlc->pub->_wme_cnt->
				 tx_failed[WME_PRIO2AC(p->priority)].bytes,
				 pkttotlen(wlc->osh, p));
		}

		ASSERT(0);
		pkt_buf_free_skb(wlc->osh, p, true);
		WLCNTINCR(wlc->pub->_cnt->txnobuf);
	}

	/* Enqueue */
	if (head)
		p = pktq_penq_head(q, prec, pkt);
	else
		p = pktq_penq(q, prec, pkt);
	ASSERT(p != NULL);

	return true;
}

void BCMFASTPATH wlc_txq_enq(void *ctx, struct scb *scb, struct sk_buff *sdu,
			     uint prec)
{
	struct wlc_info *wlc = (struct wlc_info *) ctx;
	wlc_txq_info_t *qi = wlc->active_queue;	/* Check me */
	struct pktq *q = &qi->q;
	int prio;

	prio = sdu->priority;

	ASSERT(pktq_max(q) >= wlc->pub->tunables->datahiwat);

	if (!wlc_prec_enq(wlc, q, sdu, prec)) {
		if (!EDCF_ENAB(wlc->pub)
		    || (wlc->pub->wlfeatureflag & WL_SWFL_FLOWCONTROL))
			WL_ERROR(("wl%d: wlc_txq_enq: txq overflow\n",
				  wlc->pub->unit));

		/* ASSERT(9 == 8); *//* XXX we might hit this condtion in case packet flooding from mac80211 stack */
		pkt_buf_free_skb(wlc->osh, sdu, true);
		WLCNTINCR(wlc->pub->_cnt->txnobuf);
	}

	/* Check if flow control needs to be turned on after enqueuing the packet
	 *   Don't turn on flow control if EDCF is enabled. Driver would make the decision on what
	 *   to drop instead of relying on stack to make the right decision
	 */
	if (!EDCF_ENAB(wlc->pub)
	    || (wlc->pub->wlfeatureflag & WL_SWFL_FLOWCONTROL)) {
		if (pktq_len(q) >= wlc->pub->tunables->datahiwat) {
			wlc_txflowcontrol(wlc, qi, ON, ALLPRIO);
		}
	} else if (wlc->pub->_priofc) {
		if (pktq_plen(q, wlc_prio2prec_map[prio]) >=
		    wlc->pub->tunables->datahiwat) {
			wlc_txflowcontrol(wlc, qi, ON, prio);
		}
	}
}

bool BCMFASTPATH
wlc_sendpkt_mac80211(struct wlc_info *wlc, struct sk_buff *sdu,
		     struct ieee80211_hw *hw)
{
	u8 prio;
	uint fifo;
	void *pkt;
	struct scb *scb = &global_scb;
	struct dot11_header *d11_header = (struct dot11_header *)(sdu->data);
	u16 type, fc;

	ASSERT(sdu);

	fc = ltoh16(d11_header->fc);
	type = FC_TYPE(fc);

	/* 802.11 standard requires management traffic to go at highest priority */
	prio = (type == FC_TYPE_DATA ? sdu->priority : MAXPRIO);
	fifo = prio2fifo[prio];

	ASSERT((uint) skb_headroom(sdu) >= TXOFF);
	ASSERT(!(sdu->cloned));
	ASSERT(!(sdu->next));
	ASSERT(!(sdu->prev));
	ASSERT(fifo < NFIFO);

	pkt = sdu;
	if (unlikely
	    (wlc_d11hdrs_mac80211(wlc, hw, pkt, scb, 0, 1, fifo, 0, NULL, 0)))
		return -EINVAL;
	wlc_txq_enq(wlc, scb, pkt, WLC_PRIO_TO_PREC(prio));
	wlc_send_q(wlc, wlc->active_queue);

	WLCNTINCR(wlc->pub->_cnt->ieee_tx);
	return 0;
}

void BCMFASTPATH wlc_send_q(struct wlc_info *wlc, wlc_txq_info_t *qi)
{
	struct sk_buff *pkt[DOT11_MAXNUMFRAGS];
	int prec;
	u16 prec_map;
	int err = 0, i, count;
	uint fifo;
	struct pktq *q = &qi->q;
	struct ieee80211_tx_info *tx_info;

	/* only do work for the active queue */
	if (qi != wlc->active_queue)
		return;

	if (in_send_q)
		return;
	else
		in_send_q = true;

	prec_map = wlc->tx_prec_map;

	/* Send all the enq'd pkts that we can.
	 * Dequeue packets with precedence with empty HW fifo only
	 */
	while (prec_map && (pkt[0] = pktq_mdeq(q, prec_map, &prec))) {
		tx_info = IEEE80211_SKB_CB(pkt[0]);
		if (tx_info->flags & IEEE80211_TX_CTL_AMPDU) {
			err = wlc_sendampdu(wlc->ampdu, qi, pkt, prec);
		} else {
			count = 1;
			err = wlc_prep_pdu(wlc, pkt[0], &fifo);
			if (!err) {
				for (i = 0; i < count; i++) {
					wlc_txfifo(wlc, fifo, pkt[i], true, 1);
				}
			}
		}

		if (err == BCME_BUSY) {
			pktq_penq_head(q, prec, pkt[0]);
			/* If send failed due to any other reason than a change in
			 * HW FIFO condition, quit. Otherwise, read the new prec_map!
			 */
			if (prec_map == wlc->tx_prec_map)
				break;
			prec_map = wlc->tx_prec_map;
		}
	}

	/* Check if flow control needs to be turned off after sending the packet */
	if (!EDCF_ENAB(wlc->pub)
	    || (wlc->pub->wlfeatureflag & WL_SWFL_FLOWCONTROL)) {
		if (wlc_txflowcontrol_prio_isset(wlc, qi, ALLPRIO)
		    && (pktq_len(q) < wlc->pub->tunables->datahiwat / 2)) {
			wlc_txflowcontrol(wlc, qi, OFF, ALLPRIO);
		}
	} else if (wlc->pub->_priofc) {
		int prio;
		for (prio = MAXPRIO; prio >= 0; prio--) {
			if (wlc_txflowcontrol_prio_isset(wlc, qi, prio) &&
			    (pktq_plen(q, wlc_prio2prec_map[prio]) <
			     wlc->pub->tunables->datahiwat / 2)) {
				wlc_txflowcontrol(wlc, qi, OFF, prio);
			}
		}
	}
	in_send_q = false;
}

/*
 * bcmc_fid_generate:
 * Generate frame ID for a BCMC packet.  The frag field is not used
 * for MC frames so is used as part of the sequence number.
 */
static inline u16
bcmc_fid_generate(struct wlc_info *wlc, wlc_bsscfg_t *bsscfg, d11txh_t *txh)
{
	u16 frameid;

	frameid = ltoh16(txh->TxFrameID) & ~(TXFID_SEQ_MASK | TXFID_QUEUE_MASK);
	frameid |=
	    (((wlc->
	       mc_fid_counter++) << TXFID_SEQ_SHIFT) & TXFID_SEQ_MASK) |
	    TX_BCMC_FIFO;

	return frameid;
}

void BCMFASTPATH
wlc_txfifo(struct wlc_info *wlc, uint fifo, struct sk_buff *p, bool commit,
	   s8 txpktpend)
{
	u16 frameid = INVALIDFID;
	d11txh_t *txh;

	ASSERT(fifo < NFIFO);
	txh = (d11txh_t *) (p->data);

	/* When a BC/MC frame is being committed to the BCMC fifo via DMA (NOT PIO), update
	 * ucode or BSS info as appropriate.
	 */
	if (fifo == TX_BCMC_FIFO) {
		frameid = ltoh16(txh->TxFrameID);

	}

	if (WLC_WAR16165(wlc))
		wlc_war16165(wlc, true);


	/* Bump up pending count for if not using rpc. If rpc is used, this will be handled
	 * in wlc_bmac_txfifo()
	 */
	if (commit) {
		TXPKTPENDINC(wlc, fifo, txpktpend);
		WL_TRACE(("wlc_txfifo, pktpend inc %d to %d\n", txpktpend,
			  TXPKTPENDGET(wlc, fifo)));
	}

	/* Commit BCMC sequence number in the SHM frame ID location */
	if (frameid != INVALIDFID)
		BCMCFID(wlc, frameid);

	if (dma_txfast(wlc->hw->di[fifo], p, commit) < 0) {
		WL_ERROR(("wlc_txfifo: fatal, toss frames !!!\n"));
	}
}

static u16
wlc_compute_airtime(struct wlc_info *wlc, ratespec_t rspec, uint length)
{
	u16 usec = 0;
	uint mac_rate = RSPEC2RATE(rspec);
	uint nsyms;

	if (IS_MCS(rspec)) {
		/* not supported yet */
		ASSERT(0);
	} else if (IS_OFDM(rspec)) {
		/* nsyms = Ceiling(Nbits / (Nbits/sym))
		 *
		 * Nbits = length * 8
		 * Nbits/sym = Mbps * 4 = mac_rate * 2
		 */
		nsyms = CEIL((length * 8), (mac_rate * 2));

		/* usec = symbols * usec/symbol */
		usec = (u16) (nsyms * APHY_SYMBOL_TIME);
		return usec;
	} else {
		switch (mac_rate) {
		case WLC_RATE_1M:
			usec = length << 3;
			break;
		case WLC_RATE_2M:
			usec = length << 2;
			break;
		case WLC_RATE_5M5:
			usec = (length << 4) / 11;
			break;
		case WLC_RATE_11M:
			usec = (length << 3) / 11;
			break;
		default:
			WL_ERROR(("wl%d: wlc_compute_airtime: unsupported rspec 0x%x\n", wlc->pub->unit, rspec));
			ASSERT((const char *)"Bad phy_rate" == NULL);
			break;
		}
	}

	return usec;
}

void BCMFASTPATH
wlc_compute_plcp(struct wlc_info *wlc, ratespec_t rspec, uint length, u8 *plcp)
{
	if (IS_MCS(rspec)) {
		wlc_compute_mimo_plcp(rspec, length, plcp);
	} else if (IS_OFDM(rspec)) {
		wlc_compute_ofdm_plcp(rspec, length, plcp);
	} else {
		wlc_compute_cck_plcp(rspec, length, plcp);
	}
	return;
}

/* Rate: 802.11 rate code, length: PSDU length in octets */
static void wlc_compute_mimo_plcp(ratespec_t rspec, uint length, u8 *plcp)
{
	u8 mcs = (u8) (rspec & RSPEC_RATE_MASK);
	ASSERT(IS_MCS(rspec));
	plcp[0] = mcs;
	if (RSPEC_IS40MHZ(rspec) || (mcs == 32))
		plcp[0] |= MIMO_PLCP_40MHZ;
	WLC_SET_MIMO_PLCP_LEN(plcp, length);
	plcp[3] = RSPEC_MIMOPLCP3(rspec);	/* rspec already holds this byte */
	plcp[3] |= 0x7;		/* set smoothing, not sounding ppdu & reserved */
	plcp[4] = 0;		/* number of extension spatial streams bit 0 & 1 */
	plcp[5] = 0;
}

/* Rate: 802.11 rate code, length: PSDU length in octets */
static void BCMFASTPATH
wlc_compute_ofdm_plcp(ratespec_t rspec, u32 length, u8 *plcp)
{
	u8 rate_signal;
	u32 tmp = 0;
	int rate = RSPEC2RATE(rspec);

	ASSERT(IS_OFDM(rspec));

	/* encode rate per 802.11a-1999 sec 17.3.4.1, with lsb transmitted first */
	rate_signal = rate_info[rate] & RATE_MASK;
	ASSERT(rate_signal != 0);

	memset(plcp, 0, D11_PHY_HDR_LEN);
	D11A_PHY_HDR_SRATE((ofdm_phy_hdr_t *) plcp, rate_signal);

	tmp = (length & 0xfff) << 5;
	plcp[2] |= (tmp >> 16) & 0xff;
	plcp[1] |= (tmp >> 8) & 0xff;
	plcp[0] |= tmp & 0xff;

	return;
}

/*
 * Compute PLCP, but only requires actual rate and length of pkt.
 * Rate is given in the driver standard multiple of 500 kbps.
 * le is set for 11 Mbps rate if necessary.
 * Broken out for PRQ.
 */

static void wlc_cck_plcp_set(int rate_500, uint length, u8 *plcp)
{
	u16 usec = 0;
	u8 le = 0;

	switch (rate_500) {
	case WLC_RATE_1M:
		usec = length << 3;
		break;
	case WLC_RATE_2M:
		usec = length << 2;
		break;
	case WLC_RATE_5M5:
		usec = (length << 4) / 11;
		if ((length << 4) - (usec * 11) > 0)
			usec++;
		break;
	case WLC_RATE_11M:
		usec = (length << 3) / 11;
		if ((length << 3) - (usec * 11) > 0) {
			usec++;
			if ((usec * 11) - (length << 3) >= 8)
				le = D11B_PLCP_SIGNAL_LE;
		}
		break;

	default:
		WL_ERROR(("wlc_cck_plcp_set: unsupported rate %d\n", rate_500));
		rate_500 = WLC_RATE_1M;
		usec = length << 3;
		break;
	}
	/* PLCP signal byte */
	plcp[0] = rate_500 * 5;	/* r (500kbps) * 5 == r (100kbps) */
	/* PLCP service byte */
	plcp[1] = (u8) (le | D11B_PLCP_SIGNAL_LOCKED);
	/* PLCP length u16, little endian */
	plcp[2] = usec & 0xff;
	plcp[3] = (usec >> 8) & 0xff;
	/* PLCP CRC16 */
	plcp[4] = 0;
	plcp[5] = 0;
}

/* Rate: 802.11 rate code, length: PSDU length in octets */
static void wlc_compute_cck_plcp(ratespec_t rspec, uint length, u8 *plcp)
{
	int rate = RSPEC2RATE(rspec);

	ASSERT(IS_CCK(rspec));

	wlc_cck_plcp_set(rate, length, plcp);
}

/* wlc_compute_frame_dur()
 *
 * Calculate the 802.11 MAC header DUR field for MPDU
 * DUR for a single frame = 1 SIFS + 1 ACK
 * DUR for a frame with following frags = 3 SIFS + 2 ACK + next frag time
 *
 * rate			MPDU rate in unit of 500kbps
 * next_frag_len	next MPDU length in bytes
 * preamble_type	use short/GF or long/MM PLCP header
 */
static u16 BCMFASTPATH
wlc_compute_frame_dur(struct wlc_info *wlc, ratespec_t rate, u8 preamble_type,
		      uint next_frag_len)
{
	u16 dur, sifs;

	sifs = SIFS(wlc->band);

	dur = sifs;
	dur += (u16) wlc_calc_ack_time(wlc, rate, preamble_type);

	if (next_frag_len) {
		/* Double the current DUR to get 2 SIFS + 2 ACKs */
		dur *= 2;
		/* add another SIFS and the frag time */
		dur += sifs;
		dur +=
		    (u16) wlc_calc_frame_time(wlc, rate, preamble_type,
						 next_frag_len);
	}
	return dur;
}

/* wlc_compute_rtscts_dur()
 *
 * Calculate the 802.11 MAC header DUR field for an RTS or CTS frame
 * DUR for normal RTS/CTS w/ frame = 3 SIFS + 1 CTS + next frame time + 1 ACK
 * DUR for CTS-TO-SELF w/ frame    = 2 SIFS         + next frame time + 1 ACK
 *
 * cts			cts-to-self or rts/cts
 * rts_rate		rts or cts rate in unit of 500kbps
 * rate			next MPDU rate in unit of 500kbps
 * frame_len		next MPDU frame length in bytes
 */
u16 BCMFASTPATH
wlc_compute_rtscts_dur(struct wlc_info *wlc, bool cts_only, ratespec_t rts_rate,
		       ratespec_t frame_rate, u8 rts_preamble_type,
		       u8 frame_preamble_type, uint frame_len, bool ba)
{
	u16 dur, sifs;

	sifs = SIFS(wlc->band);

	if (!cts_only) {	/* RTS/CTS */
		dur = 3 * sifs;
		dur +=
		    (u16) wlc_calc_cts_time(wlc, rts_rate,
					       rts_preamble_type);
	} else {		/* CTS-TO-SELF */
		dur = 2 * sifs;
	}

	dur +=
	    (u16) wlc_calc_frame_time(wlc, frame_rate, frame_preamble_type,
					 frame_len);
	if (ba)
		dur +=
		    (u16) wlc_calc_ba_time(wlc, frame_rate,
					      WLC_SHORT_PREAMBLE);
	else
		dur +=
		    (u16) wlc_calc_ack_time(wlc, frame_rate,
					       frame_preamble_type);
	return dur;
}

static bool wlc_phy_rspec_check(struct wlc_info *wlc, u16 bw, ratespec_t rspec)
{
	if (IS_MCS(rspec)) {
		uint mcs = rspec & RSPEC_RATE_MASK;

		if (mcs < 8) {
			ASSERT(RSPEC_STF(rspec) < PHY_TXC1_MODE_SDM);
		} else if ((mcs >= 8) && (mcs <= 23)) {
			ASSERT(RSPEC_STF(rspec) == PHY_TXC1_MODE_SDM);
		} else if (mcs == 32) {
			ASSERT(RSPEC_STF(rspec) < PHY_TXC1_MODE_SDM);
			ASSERT(bw == PHY_TXC1_BW_40MHZ_DUP);
		}
	} else if (IS_OFDM(rspec)) {
		ASSERT(RSPEC_STF(rspec) < PHY_TXC1_MODE_STBC);
	} else {
		ASSERT(IS_CCK(rspec));

		ASSERT((bw == PHY_TXC1_BW_20MHZ)
		       || (bw == PHY_TXC1_BW_20MHZ_UP));
		ASSERT(RSPEC_STF(rspec) == PHY_TXC1_MODE_SISO);
	}

	return true;
}

u16 BCMFASTPATH wlc_phytxctl1_calc(struct wlc_info *wlc, ratespec_t rspec)
{
	u16 phyctl1 = 0;
	u16 bw;

	if (WLCISLCNPHY(wlc->band)) {
		bw = PHY_TXC1_BW_20MHZ;
	} else {
		bw = RSPEC_GET_BW(rspec);
		/* 10Mhz is not supported yet */
		if (bw < PHY_TXC1_BW_20MHZ) {
			WL_ERROR(("wlc_phytxctl1_calc: bw %d is not supported yet, set to 20L\n", bw));
			bw = PHY_TXC1_BW_20MHZ;
		}

		wlc_phy_rspec_check(wlc, bw, rspec);
	}

	if (IS_MCS(rspec)) {
		uint mcs = rspec & RSPEC_RATE_MASK;

		/* bw, stf, coding-type is part of RSPEC_PHYTXBYTE2 returns */
		phyctl1 = RSPEC_PHYTXBYTE2(rspec);
		/* set the upper byte of phyctl1 */
		phyctl1 |= (mcs_table[mcs].tx_phy_ctl3 << 8);
	} else if (IS_CCK(rspec) && !WLCISLCNPHY(wlc->band)
		   && !WLCISSSLPNPHY(wlc->band)) {
		/* In CCK mode LPPHY overloads OFDM Modulation bits with CCK Data Rate */
		/* Eventually MIMOPHY would also be converted to this format */
		/* 0 = 1Mbps; 1 = 2Mbps; 2 = 5.5Mbps; 3 = 11Mbps */
		phyctl1 = (bw | (RSPEC_STF(rspec) << PHY_TXC1_MODE_SHIFT));
	} else {		/* legacy OFDM/CCK */
		s16 phycfg;
		/* get the phyctl byte from rate phycfg table */
		phycfg = wlc_rate_legacy_phyctl(RSPEC2RATE(rspec));
		if (phycfg == -1) {
			WL_ERROR(("wlc_phytxctl1_calc: wrong legacy OFDM/CCK rate\n"));
			ASSERT(0);
			phycfg = 0;
		}
		/* set the upper byte of phyctl1 */
		phyctl1 =
		    (bw | (phycfg << 8) |
		     (RSPEC_STF(rspec) << PHY_TXC1_MODE_SHIFT));
	}

#ifdef BCMDBG
	/* phy clock must support 40Mhz if tx descriptor uses it */
	if ((phyctl1 & PHY_TXC1_BW_MASK) >= PHY_TXC1_BW_40MHZ) {
		ASSERT(CHSPEC_WLC_BW(wlc->chanspec) == WLC_40_MHZ);
		ASSERT(wlc->chanspec == wlc_phy_chanspec_get(wlc->band->pi));
	}
#endif				/* BCMDBG */
	return phyctl1;
}

ratespec_t BCMFASTPATH
wlc_rspec_to_rts_rspec(struct wlc_info *wlc, ratespec_t rspec, bool use_rspec,
		       u16 mimo_ctlchbw)
{
	ratespec_t rts_rspec = 0;

	if (use_rspec) {
		/* use frame rate as rts rate */
		rts_rspec = rspec;

	} else if (wlc->band->gmode && wlc->protection->_g && !IS_CCK(rspec)) {
		/* Use 11Mbps as the g protection RTS target rate and fallback.
		 * Use the WLC_BASIC_RATE() lookup to find the best basic rate under the
		 * target in case 11 Mbps is not Basic.
		 * 6 and 9 Mbps are not usually selected by rate selection, but even
		 * if the OFDM rate we are protecting is 6 or 9 Mbps, 11 is more robust.
		 */
		rts_rspec = WLC_BASIC_RATE(wlc, WLC_RATE_11M);
	} else {
		/* calculate RTS rate and fallback rate based on the frame rate
		 * RTS must be sent at a basic rate since it is a
		 * control frame, sec 9.6 of 802.11 spec
		 */
		rts_rspec = WLC_BASIC_RATE(wlc, rspec);
	}

	if (WLC_PHY_11N_CAP(wlc->band)) {
		/* set rts txbw to correct side band */
		rts_rspec &= ~RSPEC_BW_MASK;

		/* if rspec/rspec_fallback is 40MHz, then send RTS on both 20MHz channel
		 * (DUP), otherwise send RTS on control channel
		 */
		if (RSPEC_IS40MHZ(rspec) && !IS_CCK(rts_rspec))
			rts_rspec |= (PHY_TXC1_BW_40MHZ_DUP << RSPEC_BW_SHIFT);
		else
			rts_rspec |= (mimo_ctlchbw << RSPEC_BW_SHIFT);

		/* pick siso/cdd as default for ofdm */
		if (IS_OFDM(rts_rspec)) {
			rts_rspec &= ~RSPEC_STF_MASK;
			rts_rspec |= (wlc->stf->ss_opmode << RSPEC_STF_SHIFT);
		}
	}
	return rts_rspec;
}

/*
 * Add d11txh_t, cck_phy_hdr_t.
 *
 * 'p' data must start with 802.11 MAC header
 * 'p' must allow enough bytes of local headers to be "pushed" onto the packet
 *
 * headroom == D11_PHY_HDR_LEN + D11_TXH_LEN (D11_TXH_LEN is now 104 bytes)
 *
 */
static u16 BCMFASTPATH
wlc_d11hdrs_mac80211(struct wlc_info *wlc, struct ieee80211_hw *hw,
		     struct sk_buff *p, struct scb *scb, uint frag,
		     uint nfrags, uint queue, uint next_frag_len,
		     wsec_key_t *key, ratespec_t rspec_override)
{
	struct dot11_header *h;
	d11txh_t *txh;
	u8 *plcp, plcp_fallback[D11_PHY_HDR_LEN];
	struct osl_info *osh;
	int len, phylen, rts_phylen;
	u16 fc, type, frameid, mch, phyctl, xfts, mainrates;
	u16 seq = 0, mcl = 0, status = 0;
	ratespec_t rspec[2] = { WLC_RATE_1M, WLC_RATE_1M }, rts_rspec[2] = {
	WLC_RATE_1M, WLC_RATE_1M};
	bool use_rts = false;
	bool use_cts = false;
	bool use_rifs = false;
	bool short_preamble[2] = { false, false };
	u8 preamble_type[2] = { WLC_LONG_PREAMBLE, WLC_LONG_PREAMBLE };
	u8 rts_preamble_type[2] = { WLC_LONG_PREAMBLE, WLC_LONG_PREAMBLE };
	u8 *rts_plcp, rts_plcp_fallback[D11_PHY_HDR_LEN];
	struct dot11_rts_frame *rts = NULL;
	bool qos;
	uint ac;
	u32 rate_val[2];
	bool hwtkmic = false;
	u16 mimo_ctlchbw = PHY_TXC1_BW_20MHZ;
#ifdef WLANTSEL
#define ANTCFG_NONE 0xFF
	u8 antcfg = ANTCFG_NONE;
	u8 fbantcfg = ANTCFG_NONE;
#endif
	uint phyctl1_stf = 0;
	u16 durid = 0;
	struct ieee80211_tx_rate *txrate[2];
	int k;
	struct ieee80211_tx_info *tx_info;
	bool is_mcs[2];
	u16 mimo_txbw;
	u8 mimo_preamble_type;

	frameid = 0;

	ASSERT(queue < NFIFO);

	osh = wlc->osh;

	/* locate 802.11 MAC header */
	h = (struct dot11_header *)(p->data);
	fc = ltoh16(h->fc);
	type = FC_TYPE(fc);

	qos = (type == FC_TYPE_DATA && FC_SUBTYPE_ANY_QOS(FC_SUBTYPE(fc)));

	/* compute length of frame in bytes for use in PLCP computations */
	len = pkttotlen(osh, p);
	phylen = len + DOT11_FCS_LEN;

	/* If WEP enabled, add room in phylen for the additional bytes of
	 * ICV which MAC generates.  We do NOT add the additional bytes to
	 * the packet itself, thus phylen = packet length + ICV_LEN + FCS_LEN
	 * in this case
	 */
	if (key) {
		phylen += key->icv_len;
	}

	/* Get tx_info */
	tx_info = IEEE80211_SKB_CB(p);
	ASSERT(tx_info);

	/* add PLCP */
	plcp = skb_push(p, D11_PHY_HDR_LEN);

	/* add Broadcom tx descriptor header */
	txh = (d11txh_t *) skb_push(p, D11_TXH_LEN);
	memset((char *)txh, 0, D11_TXH_LEN);

	/* setup frameid */
	if (tx_info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		/* non-AP STA should never use BCMC queue */
		ASSERT(queue != TX_BCMC_FIFO);
		if (queue == TX_BCMC_FIFO) {
			WL_ERROR(("wl%d: %s: ASSERT queue == TX_BCMC!\n",
				  WLCWLUNIT(wlc), __func__));
			frameid = bcmc_fid_generate(wlc, NULL, txh);
		} else {
			/* Increment the counter for first fragment */
			if (tx_info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT) {
				SCB_SEQNUM(scb, p->priority)++;
			}

			/* extract fragment number from frame first */
			seq = ltoh16(seq) & FRAGNUM_MASK;
			seq |= (SCB_SEQNUM(scb, p->priority) << SEQNUM_SHIFT);
			h->seq = htol16(seq);

			frameid = ((seq << TXFID_SEQ_SHIFT) & TXFID_SEQ_MASK) |
			    (queue & TXFID_QUEUE_MASK);
		}
	}
	frameid |= queue & TXFID_QUEUE_MASK;

	/* set the ignpmq bit for all pkts tx'd in PS mode and for beacons */
	if (SCB_PS(scb) || ((fc & FC_KIND_MASK) == FC_BEACON))
		mcl |= TXC_IGNOREPMQ;

	ASSERT(hw->max_rates <= IEEE80211_TX_MAX_RATES);
	ASSERT(hw->max_rates == 2);

	txrate[0] = tx_info->control.rates;
	txrate[1] = txrate[0] + 1;

	ASSERT(txrate[0]->idx >= 0);
	/* if rate control algorithm didn't give us a fallback rate, use the primary rate */
	if (txrate[1]->idx < 0) {
		txrate[1] = txrate[0];
	}

	for (k = 0; k < hw->max_rates; k++) {
		is_mcs[k] =
		    txrate[k]->flags & IEEE80211_TX_RC_MCS ? true : false;
		if (!is_mcs[k]) {
			ASSERT(!(tx_info->flags & IEEE80211_TX_CTL_AMPDU));
			if ((txrate[k]->idx >= 0)
			    && (txrate[k]->idx <
				hw->wiphy->bands[tx_info->band]->n_bitrates)) {
				rate_val[k] =
				    hw->wiphy->bands[tx_info->band]->
				    bitrates[txrate[k]->idx].hw_value;
				short_preamble[k] =
				    txrate[k]->
				    flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE ?
				    true : false;
			} else {
				ASSERT((txrate[k]->idx >= 0) &&
				       (txrate[k]->idx <
					hw->wiphy->bands[tx_info->band]->
					n_bitrates));
				rate_val[k] = WLC_RATE_1M;
			}
		} else {
			rate_val[k] = txrate[k]->idx;
		}
		/* Currently only support same setting for primay and fallback rates.
		 * Unify flags for each rate into a single value for the frame
		 */
		use_rts |=
		    txrate[k]->
		    flags & IEEE80211_TX_RC_USE_RTS_CTS ? true : false;
		use_cts |=
		    txrate[k]->
		    flags & IEEE80211_TX_RC_USE_CTS_PROTECT ? true : false;

		if (is_mcs[k])
			rate_val[k] |= NRATE_MCS_INUSE;

		rspec[k] = mac80211_wlc_set_nrate(wlc, wlc->band, rate_val[k]);

		/* (1) RATE: determine and validate primary rate and fallback rates */
		if (!RSPEC_ACTIVE(rspec[k])) {
			ASSERT(RSPEC_ACTIVE(rspec[k]));
			rspec[k] = WLC_RATE_1M;
		} else {
			if (WLANTSEL_ENAB(wlc) && !ETHER_ISMULTI(&h->a1)) {
				/* set tx antenna config */
				wlc_antsel_antcfg_get(wlc->asi, false, false, 0,
						      0, &antcfg, &fbantcfg);
			}
		}
	}

	phyctl1_stf = wlc->stf->ss_opmode;

	if (N_ENAB(wlc->pub)) {
		for (k = 0; k < hw->max_rates; k++) {
			/* apply siso/cdd to single stream mcs's or ofdm if rspec is auto selected */
			if (((IS_MCS(rspec[k]) &&
			      IS_SINGLE_STREAM(rspec[k] & RSPEC_RATE_MASK)) ||
			     IS_OFDM(rspec[k]))
			    && ((rspec[k] & RSPEC_OVERRIDE_MCS_ONLY)
				|| !(rspec[k] & RSPEC_OVERRIDE))) {
				rspec[k] &= ~(RSPEC_STF_MASK | RSPEC_STC_MASK);

				/* For SISO MCS use STBC if possible */
				if (IS_MCS(rspec[k])
				    && WLC_STF_SS_STBC_TX(wlc, scb)) {
					u8 stc;

					ASSERT(WLC_STBC_CAP_PHY(wlc));
					stc = 1;	/* Nss for single stream is always 1 */
					rspec[k] |=
					    (PHY_TXC1_MODE_STBC <<
					     RSPEC_STF_SHIFT) | (stc <<
								 RSPEC_STC_SHIFT);
				} else
					rspec[k] |=
					    (phyctl1_stf << RSPEC_STF_SHIFT);
			}

			/* Is the phy configured to use 40MHZ frames? If so then pick the desired txbw */
			if (CHSPEC_WLC_BW(wlc->chanspec) == WLC_40_MHZ) {
				/* default txbw is 20in40 SB */
				mimo_ctlchbw = mimo_txbw =
				    CHSPEC_SB_UPPER(WLC_BAND_PI_RADIO_CHANSPEC)
				    ? PHY_TXC1_BW_20MHZ_UP : PHY_TXC1_BW_20MHZ;

				if (IS_MCS(rspec[k])) {
					/* mcs 32 must be 40b/w DUP */
					if ((rspec[k] & RSPEC_RATE_MASK) == 32) {
						mimo_txbw =
						    PHY_TXC1_BW_40MHZ_DUP;
						/* use override */
					} else if (wlc->mimo_40txbw != AUTO)
						mimo_txbw = wlc->mimo_40txbw;
					/* else check if dst is using 40 Mhz */
					else if (scb->flags & SCB_IS40)
						mimo_txbw = PHY_TXC1_BW_40MHZ;
				} else if (IS_OFDM(rspec[k])) {
					if (wlc->ofdm_40txbw != AUTO)
						mimo_txbw = wlc->ofdm_40txbw;
				} else {
					ASSERT(IS_CCK(rspec[k]));
					if (wlc->cck_40txbw != AUTO)
						mimo_txbw = wlc->cck_40txbw;
				}
			} else {
				/* mcs32 is 40 b/w only.
				 * This is possible for probe packets on a STA during SCAN
				 */
				if ((rspec[k] & RSPEC_RATE_MASK) == 32) {
					/* mcs 0 */
					rspec[k] = RSPEC_MIMORATE;
				}
				mimo_txbw = PHY_TXC1_BW_20MHZ;
			}

			/* Set channel width */
			rspec[k] &= ~RSPEC_BW_MASK;
			if ((k == 0) || ((k > 0) && IS_MCS(rspec[k])))
				rspec[k] |= (mimo_txbw << RSPEC_BW_SHIFT);
			else
				rspec[k] |= (mimo_ctlchbw << RSPEC_BW_SHIFT);

			/* Set Short GI */
#ifdef NOSGIYET
			if (IS_MCS(rspec[k])
			    && (txrate[k]->flags & IEEE80211_TX_RC_SHORT_GI))
				rspec[k] |= RSPEC_SHORT_GI;
			else if (!(txrate[k]->flags & IEEE80211_TX_RC_SHORT_GI))
				rspec[k] &= ~RSPEC_SHORT_GI;
#else
			rspec[k] &= ~RSPEC_SHORT_GI;
#endif

			mimo_preamble_type = WLC_MM_PREAMBLE;
			if (txrate[k]->flags & IEEE80211_TX_RC_GREEN_FIELD)
				mimo_preamble_type = WLC_GF_PREAMBLE;

			if ((txrate[k]->flags & IEEE80211_TX_RC_MCS)
			    && (!IS_MCS(rspec[k]))) {
				WL_ERROR(("wl%d: %s: IEEE80211_TX_RC_MCS != IS_MCS(rspec)\n", WLCWLUNIT(wlc), __func__));
				ASSERT(0 && "Rate mismatch");
			}

			if (IS_MCS(rspec[k])) {
				preamble_type[k] = mimo_preamble_type;

				/* if SGI is selected, then forced mm for single stream */
				if ((rspec[k] & RSPEC_SHORT_GI)
				    && IS_SINGLE_STREAM(rspec[k] &
							RSPEC_RATE_MASK)) {
					preamble_type[k] = WLC_MM_PREAMBLE;
				}
			}

			/* mimo bw field MUST now be valid in the rspec (it affects duration calculations) */
			ASSERT(VALID_RATE_DBG(wlc, rspec[0]));

			/* should be better conditionalized */
			if (!IS_MCS(rspec[0])
			    && (tx_info->control.rates[0].
				flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE))
				preamble_type[k] = WLC_SHORT_PREAMBLE;

			ASSERT(!IS_MCS(rspec[0])
			       || WLC_IS_MIMO_PREAMBLE(preamble_type[k]));
		}
	} else {
		for (k = 0; k < hw->max_rates; k++) {
			/* Set ctrlchbw as 20Mhz */
			ASSERT(!IS_MCS(rspec[k]));
			rspec[k] &= ~RSPEC_BW_MASK;
			rspec[k] |= (PHY_TXC1_BW_20MHZ << RSPEC_BW_SHIFT);

			/* for nphy, stf of ofdm frames must follow policies */
			if (WLCISNPHY(wlc->band) && IS_OFDM(rspec[k])) {
				rspec[k] &= ~RSPEC_STF_MASK;
				rspec[k] |= phyctl1_stf << RSPEC_STF_SHIFT;
			}
		}
	}

	/* Reset these for use with AMPDU's */
	txrate[0]->count = 0;
	txrate[1]->count = 0;

	/* (3) PLCP: determine PLCP header and MAC duration, fill d11txh_t */
	wlc_compute_plcp(wlc, rspec[0], phylen, plcp);
	wlc_compute_plcp(wlc, rspec[1], phylen, plcp_fallback);
	bcopy(plcp_fallback, (char *)&txh->FragPLCPFallback,
	      sizeof(txh->FragPLCPFallback));

	/* Length field now put in CCK FBR CRC field */
	if (IS_CCK(rspec[1])) {
		txh->FragPLCPFallback[4] = phylen & 0xff;
		txh->FragPLCPFallback[5] = (phylen & 0xff00) >> 8;
	}

	/* MIMO-RATE: need validation ?? */
	mainrates =
	    IS_OFDM(rspec[0]) ? D11A_PHY_HDR_GRATE((ofdm_phy_hdr_t *) plcp) :
	    plcp[0];

	/* DUR field for main rate */
	if ((fc != FC_PS_POLL) && !ETHER_ISMULTI(&h->a1) && !use_rifs) {
		durid =
		    wlc_compute_frame_dur(wlc, rspec[0], preamble_type[0],
					  next_frag_len);
		h->durid = htol16(durid);
	} else if (use_rifs) {
		/* NAV protect to end of next max packet size */
		durid =
		    (u16) wlc_calc_frame_time(wlc, rspec[0],
						 preamble_type[0],
						 DOT11_MAX_FRAG_LEN);
		durid += RIFS_11N_TIME;
		h->durid = htol16(durid);
	}

	/* DUR field for fallback rate */
	if (fc == FC_PS_POLL)
		txh->FragDurFallback = h->durid;
	else if (ETHER_ISMULTI(&h->a1) || use_rifs)
		txh->FragDurFallback = 0;
	else {
		durid = wlc_compute_frame_dur(wlc, rspec[1],
					      preamble_type[1], next_frag_len);
		txh->FragDurFallback = htol16(durid);
	}

	/* (4) MAC-HDR: MacTxControlLow */
	if (frag == 0)
		mcl |= TXC_STARTMSDU;

	if (!ETHER_ISMULTI(&h->a1))
		mcl |= TXC_IMMEDACK;

	if (BAND_5G(wlc->band->bandtype))
		mcl |= TXC_FREQBAND_5G;

	if (CHSPEC_IS40(WLC_BAND_PI_RADIO_CHANSPEC))
		mcl |= TXC_BW_40;

	/* set AMIC bit if using hardware TKIP MIC */
	if (hwtkmic)
		mcl |= TXC_AMIC;

	txh->MacTxControlLow = htol16(mcl);

	/* MacTxControlHigh */
	mch = 0;

	/* Set fallback rate preamble type */
	if ((preamble_type[1] == WLC_SHORT_PREAMBLE) ||
	    (preamble_type[1] == WLC_GF_PREAMBLE)) {
		ASSERT((preamble_type[1] == WLC_GF_PREAMBLE) ||
		       (!IS_MCS(rspec[1])));
		if (RSPEC2RATE(rspec[1]) != WLC_RATE_1M)
			mch |= TXC_PREAMBLE_DATA_FB_SHORT;
	}

	/* MacFrameControl */
	bcopy((char *)&h->fc, (char *)&txh->MacFrameControl, sizeof(u16));

	txh->TxFesTimeNormal = htol16(0);

	txh->TxFesTimeFallback = htol16(0);

	/* TxFrameRA */
	bcopy((char *)&h->a1, (char *)&txh->TxFrameRA, ETHER_ADDR_LEN);

	/* TxFrameID */
	txh->TxFrameID = htol16(frameid);

	/* TxStatus, Note the case of recreating the first frag of a suppressed frame
	 * then we may need to reset the retry cnt's via the status reg
	 */
	txh->TxStatus = htol16(status);

	if (D11REV_GE(wlc->pub->corerev, 16)) {
		/* extra fields for ucode AMPDU aggregation, the new fields are added to
		 * the END of previous structure so that it's compatible in driver.
		 * In old rev ucode, these fields should be ignored
		 */
		txh->MaxNMpdus = htol16(0);
		txh->MaxABytes_MRT = htol16(0);
		txh->MaxABytes_FBR = htol16(0);
		txh->MinMBytes = htol16(0);
	}

	/* (5) RTS/CTS: determine RTS/CTS PLCP header and MAC duration, furnish d11txh_t */
	/* RTS PLCP header and RTS frame */
	if (use_rts || use_cts) {
		if (use_rts && use_cts)
			use_cts = false;

		for (k = 0; k < 2; k++) {
			rts_rspec[k] = wlc_rspec_to_rts_rspec(wlc, rspec[k],
							      false,
							      mimo_ctlchbw);
		}

		if (!IS_OFDM(rts_rspec[0]) &&
		    !((RSPEC2RATE(rts_rspec[0]) == WLC_RATE_1M) ||
		      (wlc->PLCPHdr_override == WLC_PLCP_LONG))) {
			rts_preamble_type[0] = WLC_SHORT_PREAMBLE;
			mch |= TXC_PREAMBLE_RTS_MAIN_SHORT;
		}

		if (!IS_OFDM(rts_rspec[1]) &&
		    !((RSPEC2RATE(rts_rspec[1]) == WLC_RATE_1M) ||
		      (wlc->PLCPHdr_override == WLC_PLCP_LONG))) {
			rts_preamble_type[1] = WLC_SHORT_PREAMBLE;
			mch |= TXC_PREAMBLE_RTS_FB_SHORT;
		}

		/* RTS/CTS additions to MacTxControlLow */
		if (use_cts) {
			txh->MacTxControlLow |= htol16(TXC_SENDCTS);
		} else {
			txh->MacTxControlLow |= htol16(TXC_SENDRTS);
			txh->MacTxControlLow |= htol16(TXC_LONGFRAME);
		}

		/* RTS PLCP header */
		ASSERT(IS_ALIGNED((unsigned long)txh->RTSPhyHeader, sizeof(u16)));
		rts_plcp = txh->RTSPhyHeader;
		if (use_cts)
			rts_phylen = DOT11_CTS_LEN + DOT11_FCS_LEN;
		else
			rts_phylen = DOT11_RTS_LEN + DOT11_FCS_LEN;

		wlc_compute_plcp(wlc, rts_rspec[0], rts_phylen, rts_plcp);

		/* fallback rate version of RTS PLCP header */
		wlc_compute_plcp(wlc, rts_rspec[1], rts_phylen,
				 rts_plcp_fallback);
		bcopy(rts_plcp_fallback, (char *)&txh->RTSPLCPFallback,
		      sizeof(txh->RTSPLCPFallback));

		/* RTS frame fields... */
		rts = (struct dot11_rts_frame *)&txh->rts_frame;

		durid = wlc_compute_rtscts_dur(wlc, use_cts, rts_rspec[0],
					       rspec[0], rts_preamble_type[0],
					       preamble_type[0], phylen, false);
		rts->durid = htol16(durid);
		/* fallback rate version of RTS DUR field */
		durid = wlc_compute_rtscts_dur(wlc, use_cts,
					       rts_rspec[1], rspec[1],
					       rts_preamble_type[1],
					       preamble_type[1], phylen, false);
		txh->RTSDurFallback = htol16(durid);

		if (use_cts) {
			rts->fc = htol16(FC_CTS);
			bcopy((char *)&h->a2, (char *)&rts->ra, ETHER_ADDR_LEN);
		} else {
			rts->fc = htol16((u16) FC_RTS);
			bcopy((char *)&h->a1, (char *)&rts->ra,
			      2 * ETHER_ADDR_LEN);
		}

		/* mainrate
		 *    low 8 bits: main frag rate/mcs,
		 *    high 8 bits: rts/cts rate/mcs
		 */
		mainrates |= (IS_OFDM(rts_rspec[0]) ?
			      D11A_PHY_HDR_GRATE((ofdm_phy_hdr_t *) rts_plcp) :
			      rts_plcp[0]) << 8;
	} else {
		memset((char *)txh->RTSPhyHeader, 0, D11_PHY_HDR_LEN);
		memset((char *)&txh->rts_frame, 0,
			sizeof(struct dot11_rts_frame));
		memset((char *)txh->RTSPLCPFallback, 0,
		      sizeof(txh->RTSPLCPFallback));
		txh->RTSDurFallback = 0;
	}

#ifdef SUPPORT_40MHZ
	/* add null delimiter count */
	if ((tx_info->flags & IEEE80211_TX_CTL_AMPDU) && IS_MCS(rspec)) {
		txh->RTSPLCPFallback[AMPDU_FBR_NULL_DELIM] =
		    wlc_ampdu_null_delim_cnt(wlc->ampdu, scb, rspec, phylen);
	}
#endif

	/* Now that RTS/RTS FB preamble types are updated, write the final value */
	txh->MacTxControlHigh = htol16(mch);

	/* MainRates (both the rts and frag plcp rates have been calculated now) */
	txh->MainRates = htol16(mainrates);

	/* XtraFrameTypes */
	xfts = FRAMETYPE(rspec[1], wlc->mimoft);
	xfts |= (FRAMETYPE(rts_rspec[0], wlc->mimoft) << XFTS_RTS_FT_SHIFT);
	xfts |= (FRAMETYPE(rts_rspec[1], wlc->mimoft) << XFTS_FBRRTS_FT_SHIFT);
	xfts |=
	    CHSPEC_CHANNEL(WLC_BAND_PI_RADIO_CHANSPEC) << XFTS_CHANNEL_SHIFT;
	txh->XtraFrameTypes = htol16(xfts);

	/* PhyTxControlWord */
	phyctl = FRAMETYPE(rspec[0], wlc->mimoft);
	if ((preamble_type[0] == WLC_SHORT_PREAMBLE) ||
	    (preamble_type[0] == WLC_GF_PREAMBLE)) {
		ASSERT((preamble_type[0] == WLC_GF_PREAMBLE)
		       || !IS_MCS(rspec[0]));
		if (RSPEC2RATE(rspec[0]) != WLC_RATE_1M)
			phyctl |= PHY_TXC_SHORT_HDR;
		WLCNTINCR(wlc->pub->_cnt->txprshort);
	}

	/* phytxant is properly bit shifted */
	phyctl |= wlc_stf_d11hdrs_phyctl_txant(wlc, rspec[0]);
	txh->PhyTxControlWord = htol16(phyctl);

	/* PhyTxControlWord_1 */
	if (WLC_PHY_11N_CAP(wlc->band)) {
		u16 phyctl1 = 0;

		phyctl1 = wlc_phytxctl1_calc(wlc, rspec[0]);
		txh->PhyTxControlWord_1 = htol16(phyctl1);
		phyctl1 = wlc_phytxctl1_calc(wlc, rspec[1]);
		txh->PhyTxControlWord_1_Fbr = htol16(phyctl1);

		if (use_rts || use_cts) {
			phyctl1 = wlc_phytxctl1_calc(wlc, rts_rspec[0]);
			txh->PhyTxControlWord_1_Rts = htol16(phyctl1);
			phyctl1 = wlc_phytxctl1_calc(wlc, rts_rspec[1]);
			txh->PhyTxControlWord_1_FbrRts = htol16(phyctl1);
		}

		/*
		 * For mcs frames, if mixedmode(overloaded with long preamble) is going to be set,
		 * fill in non-zero MModeLen and/or MModeFbrLen
		 *  it will be unnecessary if they are separated
		 */
		if (IS_MCS(rspec[0]) && (preamble_type[0] == WLC_MM_PREAMBLE)) {
			u16 mmodelen =
			    wlc_calc_lsig_len(wlc, rspec[0], phylen);
			txh->MModeLen = htol16(mmodelen);
		}

		if (IS_MCS(rspec[1]) && (preamble_type[1] == WLC_MM_PREAMBLE)) {
			u16 mmodefbrlen =
			    wlc_calc_lsig_len(wlc, rspec[1], phylen);
			txh->MModeFbrLen = htol16(mmodefbrlen);
		}
	}

	if (IS_MCS(rspec[0]))
		ASSERT(IS_MCS(rspec[1]));

	ASSERT(!IS_MCS(rspec[0]) ||
	       ((preamble_type[0] == WLC_MM_PREAMBLE) == (txh->MModeLen != 0)));
	ASSERT(!IS_MCS(rspec[1]) ||
	       ((preamble_type[1] == WLC_MM_PREAMBLE) ==
		(txh->MModeFbrLen != 0)));

	ac = wme_fifo2ac[queue];
	if (SCB_WME(scb) && qos && wlc->edcf_txop[ac]) {
		uint frag_dur, dur, dur_fallback;

		ASSERT(!ETHER_ISMULTI(&h->a1));

		/* WME: Update TXOP threshold */
		if ((!(tx_info->flags & IEEE80211_TX_CTL_AMPDU)) && (frag == 0)) {
			frag_dur =
			    wlc_calc_frame_time(wlc, rspec[0], preamble_type[0],
						phylen);

			if (rts) {
				/* 1 RTS or CTS-to-self frame */
				dur =
				    wlc_calc_cts_time(wlc, rts_rspec[0],
						      rts_preamble_type[0]);
				dur_fallback =
				    wlc_calc_cts_time(wlc, rts_rspec[1],
						      rts_preamble_type[1]);
				/* (SIFS + CTS) + SIFS + frame + SIFS + ACK */
				dur += ltoh16(rts->durid);
				dur_fallback += ltoh16(txh->RTSDurFallback);
			} else if (use_rifs) {
				dur = frag_dur;
				dur_fallback = 0;
			} else {
				/* frame + SIFS + ACK */
				dur = frag_dur;
				dur +=
				    wlc_compute_frame_dur(wlc, rspec[0],
							  preamble_type[0], 0);

				dur_fallback =
				    wlc_calc_frame_time(wlc, rspec[1],
							preamble_type[1],
							phylen);
				dur_fallback +=
				    wlc_compute_frame_dur(wlc, rspec[1],
							  preamble_type[1], 0);
			}
			/* NEED to set TxFesTimeNormal (hard) */
			txh->TxFesTimeNormal = htol16((u16) dur);
			/* NEED to set fallback rate version of TxFesTimeNormal (hard) */
			txh->TxFesTimeFallback = htol16((u16) dur_fallback);

			/* update txop byte threshold (txop minus intraframe overhead) */
			if (wlc->edcf_txop[ac] >= (dur - frag_dur)) {
				{
					uint newfragthresh;

					newfragthresh =
					    wlc_calc_frame_len(wlc, rspec[0],
							       preamble_type[0],
							       (wlc->
								edcf_txop[ac] -
								(dur -
								 frag_dur)));
					/* range bound the fragthreshold */
					if (newfragthresh < DOT11_MIN_FRAG_LEN)
						newfragthresh =
						    DOT11_MIN_FRAG_LEN;
					else if (newfragthresh >
						 wlc->usr_fragthresh)
						newfragthresh =
						    wlc->usr_fragthresh;
					/* update the fragthresh and do txc update */
					if (wlc->fragthresh[queue] !=
					    (u16) newfragthresh) {
						wlc->fragthresh[queue] =
						    (u16) newfragthresh;
					}
				}
			} else
				WL_ERROR(("wl%d: %s txop invalid for rate %d\n",
					  wlc->pub->unit, fifo_names[queue],
					  RSPEC2RATE(rspec[0])));

			if (dur > wlc->edcf_txop[ac])
				WL_ERROR(("wl%d: %s: %s txop exceeded phylen %d/%d dur %d/%d\n", wlc->pub->unit, __func__, fifo_names[queue], phylen, wlc->fragthresh[queue], dur, wlc->edcf_txop[ac]));
		}
	}

	return 0;
}

void wlc_tbtt(struct wlc_info *wlc, d11regs_t *regs)
{
	wlc_bsscfg_t *cfg = wlc->cfg;

	WLCNTINCR(wlc->pub->_cnt->tbtt);

	if (BSSCFG_STA(cfg)) {
		/* run watchdog here if the watchdog timer is not armed */
		if (WLC_WATCHDOG_TBTT(wlc)) {
			u32 cur, delta;
			if (wlc->WDarmed) {
				wl_del_timer(wlc->wl, wlc->wdtimer);
				wlc->WDarmed = false;
			}

			cur = OSL_SYSUPTIME();
			delta = cur > wlc->WDlast ? cur - wlc->WDlast :
			    (u32) ~0 - wlc->WDlast + cur + 1;
			if (delta >= TIMER_INTERVAL_WATCHDOG) {
				wlc_watchdog((void *)wlc);
				wlc->WDlast = cur;
			}

			wl_add_timer(wlc->wl, wlc->wdtimer,
				     wlc_watchdog_backup_bi(wlc), true);
			wlc->WDarmed = true;
		}
	}

	if (!cfg->BSS) {
		/* DirFrmQ is now valid...defer setting until end of ATIM window */
		wlc->qvalid |= MCMD_DIRFRMQVAL;
	}
}

/* GP timer is a freerunning 32 bit counter, decrements at 1 us rate */
void wlc_hwtimer_gptimer_set(struct wlc_info *wlc, uint us)
{
	ASSERT(wlc->pub->corerev >= 3);	/* no gptimer in earlier revs */
	W_REG(wlc->osh, &wlc->regs->gptimer, us);
}

void wlc_hwtimer_gptimer_abort(struct wlc_info *wlc)
{
	ASSERT(wlc->pub->corerev >= 3);
	W_REG(wlc->osh, &wlc->regs->gptimer, 0);
}

static void wlc_hwtimer_gptimer_cb(struct wlc_info *wlc)
{
	/* when interrupt is generated, the counter is loaded with last value
	 * written and continue to decrement. So it has to be cleaned first
	 */
	W_REG(wlc->osh, &wlc->regs->gptimer, 0);
}

/*
 * This fn has all the high level dpc processing from wlc_dpc.
 * POLICY: no macinstatus change, no bounding loop.
 *         All dpc bounding should be handled in BMAC dpc, like txstatus and rxint
 */
void wlc_high_dpc(struct wlc_info *wlc, u32 macintstatus)
{
	d11regs_t *regs = wlc->regs;
#ifdef BCMDBG
	char flagstr[128];
	static const bcm_bit_desc_t int_flags[] = {
		{MI_MACSSPNDD, "MACSSPNDD"},
		{MI_BCNTPL, "BCNTPL"},
		{MI_TBTT, "TBTT"},
		{MI_BCNSUCCESS, "BCNSUCCESS"},
		{MI_BCNCANCLD, "BCNCANCLD"},
		{MI_ATIMWINEND, "ATIMWINEND"},
		{MI_PMQ, "PMQ"},
		{MI_NSPECGEN_0, "NSPECGEN_0"},
		{MI_NSPECGEN_1, "NSPECGEN_1"},
		{MI_MACTXERR, "MACTXERR"},
		{MI_NSPECGEN_3, "NSPECGEN_3"},
		{MI_PHYTXERR, "PHYTXERR"},
		{MI_PME, "PME"},
		{MI_GP0, "GP0"},
		{MI_GP1, "GP1"},
		{MI_DMAINT, "DMAINT"},
		{MI_TXSTOP, "TXSTOP"},
		{MI_CCA, "CCA"},
		{MI_BG_NOISE, "BG_NOISE"},
		{MI_DTIM_TBTT, "DTIM_TBTT"},
		{MI_PRQ, "PRQ"},
		{MI_PWRUP, "PWRUP"},
		{MI_RFDISABLE, "RFDISABLE"},
		{MI_TFS, "TFS"},
		{MI_PHYCHANGED, "PHYCHANGED"},
		{MI_TO, "TO"},
		{0, NULL}
	};

	if (macintstatus & ~(MI_TBTT | MI_TXSTOP)) {
		bcm_format_flags(int_flags, macintstatus, flagstr,
				 sizeof(flagstr));
		WL_TRACE(("wl%d: macintstatus 0x%x %s\n", wlc->pub->unit,
			  macintstatus, flagstr));
	}
#endif				/* BCMDBG */

	if (macintstatus & MI_PRQ) {
		/* Process probe request FIFO */
		ASSERT(0 && "PRQ Interrupt in non-MBSS");
	}

	/* TBTT indication */
	/* ucode only gives either TBTT or DTIM_TBTT, not both */
	if (macintstatus & (MI_TBTT | MI_DTIM_TBTT))
		wlc_tbtt(wlc, regs);

	if (macintstatus & MI_GP0) {
		WL_ERROR(("wl%d: PSM microcode watchdog fired at %d (seconds). Resetting.\n", wlc->pub->unit, wlc->pub->now));

		printk_once("%s : PSM Watchdog, chipid 0x%x, chiprev 0x%x\n",
					__func__, wlc->pub->sih->chip,
					wlc->pub->sih->chiprev);

		WLCNTINCR(wlc->pub->_cnt->psmwds);

		/* big hammer */
		wl_init(wlc->wl);
	}

	/* gptimer timeout */
	if (macintstatus & MI_TO) {
		wlc_hwtimer_gptimer_cb(wlc);
	}

	if (macintstatus & MI_RFDISABLE) {
		WL_ERROR(("wl%d: MAC Detected a change on the RF Disable Input 0x%x\n", wlc->pub->unit, R_REG(wlc->osh, &regs->phydebug) & PDBG_RFD));
		/* delay the cleanup to wl_down in IBSS case */
		if ((R_REG(wlc->osh, &regs->phydebug) & PDBG_RFD)) {
			int idx;
			wlc_bsscfg_t *bsscfg;
			FOREACH_BSS(wlc, idx, bsscfg) {
				if (!BSSCFG_STA(bsscfg) || !bsscfg->enable
				    || !bsscfg->BSS)
					continue;
				WL_ERROR(("wl%d: wlc_dpc: rfdisable -> wlc_bsscfg_disable()\n", wlc->pub->unit));
			}
		}
	}

	/* send any enq'd tx packets. Just makes sure to jump start tx */
	if (!pktq_empty(&wlc->active_queue->q))
		wlc_send_q(wlc, wlc->active_queue);

	ASSERT(wlc_ps_check(wlc));
}

static void *wlc_15420war(struct wlc_info *wlc, uint queue)
{
	hnddma_t *di;
	void *p;

	ASSERT(queue < NFIFO);

	if ((D11REV_IS(wlc->pub->corerev, 4))
	    || (D11REV_GT(wlc->pub->corerev, 6)))
		return NULL;

	di = wlc->hw->di[queue];
	ASSERT(di != NULL);

	/* get next packet, ignoring XmtStatus.Curr */
	p = dma_getnexttxp(di, HNDDMA_RANGE_ALL);

	/* sw block tx dma */
	dma_txblock(di);

	/* if tx ring is now empty, reset and re-init the tx dma channel */
	if (dma_txactive(wlc->hw->di[queue]) == 0) {
		WLCNTINCR(wlc->pub->_cnt->txdmawar);
		if (!dma_txreset(di))
			WL_ERROR(("wl%d: %s: dma_txreset[%d]: cannot stop dma\n", wlc->pub->unit, __func__, queue));
		dma_txinit(di);
	}
	return p;
}

static void wlc_war16165(struct wlc_info *wlc, bool tx)
{
	if (tx) {
		/* the post-increment is used in STAY_AWAKE macro */
		if (wlc->txpend16165war++ == 0)
			wlc_set_ps_ctrl(wlc);
	} else {
		wlc->txpend16165war--;
		if (wlc->txpend16165war == 0)
			wlc_set_ps_ctrl(wlc);
	}
}

/* process an individual tx_status_t */
/* WLC_HIGH_API */
bool BCMFASTPATH
wlc_dotxstatus(struct wlc_info *wlc, tx_status_t *txs, u32 frm_tx2)
{
	struct sk_buff *p;
	uint queue;
	d11txh_t *txh;
	struct scb *scb = NULL;
	bool free_pdu;
	struct osl_info *osh;
	int tx_rts, tx_frame_count, tx_rts_count;
	uint totlen, supr_status;
	bool lastframe;
	struct dot11_header *h;
	u16 fc;
	u16 mcl;
	struct ieee80211_tx_info *tx_info;
	struct ieee80211_tx_rate *txrate;
	int i;

	(void)(frm_tx2);	/* Compiler reference to avoid unused variable warning */

	/* discard intermediate indications for ucode with one legitimate case:
	 *   e.g. if "useRTS" is set. ucode did a successful rts/cts exchange, but the subsequent
	 *   tx of DATA failed. so it will start rts/cts from the beginning (resetting the rts
	 *   transmission count)
	 */
	if (!(txs->status & TX_STATUS_AMPDU)
	    && (txs->status & TX_STATUS_INTERMEDIATE)) {
		WLCNTADD(wlc->pub->_cnt->txnoack,
			 ((txs->
			   status & TX_STATUS_FRM_RTX_MASK) >>
			  TX_STATUS_FRM_RTX_SHIFT));
		WL_ERROR(("%s: INTERMEDIATE but not AMPDU\n", __func__));
		return false;
	}

	osh = wlc->osh;
	queue = txs->frameid & TXFID_QUEUE_MASK;
	ASSERT(queue < NFIFO);
	if (queue >= NFIFO) {
		p = NULL;
		goto fatal;
	}

	p = GETNEXTTXP(wlc, queue);
	if (WLC_WAR16165(wlc))
		wlc_war16165(wlc, false);
	if (p == NULL)
		p = wlc_15420war(wlc, queue);
	ASSERT(p != NULL);
	if (p == NULL)
		goto fatal;

	txh = (d11txh_t *) (p->data);
	mcl = ltoh16(txh->MacTxControlLow);

	if (txs->phyerr) {
		WL_ERROR(("phyerr 0x%x, rate 0x%x\n", txs->phyerr,
			  txh->MainRates));
		wlc_print_txdesc(txh);
		wlc_print_txstatus(txs);
	}

	ASSERT(txs->frameid == htol16(txh->TxFrameID));
	if (txs->frameid != htol16(txh->TxFrameID))
		goto fatal;

	tx_info = IEEE80211_SKB_CB(p);
	h = (struct dot11_header *)((u8 *) (txh + 1) + D11_PHY_HDR_LEN);
	fc = ltoh16(h->fc);

	scb = (struct scb *)tx_info->control.sta->drv_priv;

	if (N_ENAB(wlc->pub)) {
		u8 *plcp = (u8 *) (txh + 1);
		if (PLCP3_ISSGI(plcp[3]))
			WLCNTINCR(wlc->pub->_cnt->txmpdu_sgi);
		if (PLCP3_ISSTBC(plcp[3]))
			WLCNTINCR(wlc->pub->_cnt->txmpdu_stbc);
	}

	if (tx_info->flags & IEEE80211_TX_CTL_AMPDU) {
		ASSERT((mcl & TXC_AMPDU_MASK) != TXC_AMPDU_NONE);
		wlc_ampdu_dotxstatus(wlc->ampdu, scb, p, txs);
		return false;
	}

	supr_status = txs->status & TX_STATUS_SUPR_MASK;
	if (supr_status == TX_STATUS_SUPR_BADCH)
		WL_NONE(("%s: Pkt tx suppressed, possibly channel %d\n",
			 __func__, CHSPEC_CHANNEL(wlc->default_bss->chanspec)));

	tx_rts = htol16(txh->MacTxControlLow) & TXC_SENDRTS;
	tx_frame_count =
	    (txs->status & TX_STATUS_FRM_RTX_MASK) >> TX_STATUS_FRM_RTX_SHIFT;
	tx_rts_count =
	    (txs->status & TX_STATUS_RTS_RTX_MASK) >> TX_STATUS_RTS_RTX_SHIFT;

	lastframe = (fc & FC_MOREFRAG) == 0;

	if (!lastframe) {
		WL_ERROR(("Not last frame!\n"));
	} else {
		u16 sfbl, lfbl;
		ieee80211_tx_info_clear_status(tx_info);
		if (queue < AC_COUNT) {
			sfbl = WLC_WME_RETRY_SFB_GET(wlc, wme_fifo2ac[queue]);
			lfbl = WLC_WME_RETRY_LFB_GET(wlc, wme_fifo2ac[queue]);
		} else {
			sfbl = wlc->SFBL;
			lfbl = wlc->LFBL;
		}

		txrate = tx_info->status.rates;
		/* FIXME: this should use a combination of sfbl, lfbl depending on frame length and RTS setting */
		if ((tx_frame_count > sfbl) && (txrate[1].idx >= 0)) {
			/* rate selection requested a fallback rate and we used it */
			txrate->count = lfbl;
			txrate[1].count = tx_frame_count - lfbl;
		} else {
			/* rate selection did not request fallback rate, or we didn't need it */
			txrate->count = tx_frame_count;
			/* rc80211_minstrel.c:minstrel_tx_status() expects unused rates to be marked with idx = -1 */
			txrate[1].idx = -1;
			txrate[1].count = 0;
		}

		/* clear the rest of the rates */
		for (i = 2; i < IEEE80211_TX_MAX_RATES; i++) {
			txrate[i].idx = -1;
			txrate[i].count = 0;
		}

		if (txs->status & TX_STATUS_ACK_RCV)
			tx_info->flags |= IEEE80211_TX_STAT_ACK;
	}

	totlen = pkttotlen(osh, p);
	free_pdu = true;

	wlc_txfifo_complete(wlc, queue, 1);

	if (lastframe) {
		p->next = NULL;
		p->prev = NULL;
		wlc->txretried = 0;
		/* remove PLCP & Broadcom tx descriptor header */
		skb_pull(p, D11_PHY_HDR_LEN);
		skb_pull(p, D11_TXH_LEN);
		ieee80211_tx_status_irqsafe(wlc->pub->ieee_hw, p);
		WLCNTINCR(wlc->pub->_cnt->ieee_tx_status);
	} else {
		WL_ERROR(("%s: Not last frame => not calling tx_status\n",
			  __func__));
	}

	return false;

 fatal:
	ASSERT(0);
	if (p)
		pkt_buf_free_skb(osh, p, true);

	return true;

}

void BCMFASTPATH
wlc_txfifo_complete(struct wlc_info *wlc, uint fifo, s8 txpktpend)
{
	TXPKTPENDDEC(wlc, fifo, txpktpend);
	WL_TRACE(("wlc_txfifo_complete, pktpend dec %d to %d\n", txpktpend,
		  TXPKTPENDGET(wlc, fifo)));

	/* There is more room; mark precedences related to this FIFO sendable */
	WLC_TX_FIFO_ENAB(wlc, fifo);
	ASSERT(TXPKTPENDGET(wlc, fifo) >= 0);

	if (!TXPKTPENDTOT(wlc)) {
		if (wlc->block_datafifo & DATA_BLOCK_TX_SUPR)
			wlc_bsscfg_tx_check(wlc);
	}

	/* Clear MHF2_TXBCMC_NOW flag if BCMC fifo has drained */
	if (AP_ENAB(wlc->pub) &&
	    wlc->bcmcfifo_drain && !TXPKTPENDGET(wlc, TX_BCMC_FIFO)) {
		wlc->bcmcfifo_drain = false;
		wlc_mhf(wlc, MHF2, MHF2_TXBCMC_NOW, 0, WLC_BAND_AUTO);
	}

	/* figure out which bsscfg is being worked on... */
}

/* Given the beacon interval in kus, and a 64 bit TSF in us,
 * return the offset (in us) of the TSF from the last TBTT
 */
u32 wlc_calc_tbtt_offset(u32 bp, u32 tsf_h, u32 tsf_l)
{
	u32 k, btklo, btkhi, offset;

	/* TBTT is always an even multiple of the beacon_interval,
	 * so the TBTT less than or equal to the beacon timestamp is
	 * the beacon timestamp minus the beacon timestamp modulo
	 * the beacon interval.
	 *
	 * TBTT = BT - (BT % BIu)
	 *      = (BTk - (BTk % BP)) * 2^10
	 *
	 * BT = beacon timestamp (usec, 64bits)
	 * BTk = beacon timestamp (Kusec, 54bits)
	 * BP = beacon interval (Kusec, 16bits)
	 * BIu = BP * 2^10 = beacon interval (usec, 26bits)
	 *
	 * To keep the calculations in u32s, the modulo operation
	 * on the high part of BT needs to be done in parts using the
	 * relations:
	 * X*Y mod Z = ((X mod Z) * (Y mod Z)) mod Z
	 * and
	 * (X + Y) mod Z = ((X mod Z) + (Y mod Z)) mod Z
	 *
	 * So, if BTk[n] = u16 n [0,3] of BTk.
	 * BTk % BP = SUM((BTk[n] * 2^16n) % BP , 0<=n<4) % BP
	 * and the SUM term can be broken down:
	 * (BTk[n] *     2^16n)    % BP
	 * (BTk[n] * (2^16n % BP)) % BP
	 *
	 * Create a set of power of 2 mod BP constants:
	 * K[n] = 2^(16n) % BP
	 *      = (K[n-1] * 2^16) % BP
	 * K[2] = 2^32 % BP = ((2^16 % BP) * 2^16) % BP
	 *
	 * BTk % BP = BTk[0-1] % BP +
	 *            (BTk[2] * K[2]) % BP +
	 *            (BTk[3] * K[3]) % BP
	 *
	 * Since K[n] < 2^16 and BTk[n] is < 2^16, then BTk[n] * K[n] < 2^32
	 */

	/* BTk = BT >> 10, btklo = BTk[0-3], bkthi = BTk[4-6] */
	btklo = (tsf_h << 22) | (tsf_l >> 10);
	btkhi = tsf_h >> 10;

	/* offset = BTk % BP */
	offset = btklo % bp;

	/* K[2] = ((2^16 % BP) * 2^16) % BP */
	k = (u32) (1 << 16) % bp;
	k = (u32) (k * 1 << 16) % (u32) bp;

	/* offset += (BTk[2] * K[2]) % BP */
	offset += ((btkhi & 0xffff) * k) % bp;

	/* BTk[3] */
	btkhi = btkhi >> 16;

	/* k[3] = (K[2] * 2^16) % BP */
	k = (k << 16) % bp;

	/* offset += (BTk[3] * K[3]) % BP */
	offset += ((btkhi & 0xffff) * k) % bp;

	offset = offset % bp;

	/* convert offset from kus to us by shifting up 10 bits and
	 * add in the low 10 bits of tsf that we ignored
	 */
	offset = (offset << 10) + (tsf_l & 0x3FF);

	return offset;
}

/* Update beacon listen interval in shared memory */
void wlc_bcn_li_upd(struct wlc_info *wlc)
{
	if (AP_ENAB(wlc->pub))
		return;

	/* wake up every DTIM is the default */
	if (wlc->bcn_li_dtim == 1)
		wlc_write_shm(wlc, M_BCN_LI, 0);
	else
		wlc_write_shm(wlc, M_BCN_LI,
			      (wlc->bcn_li_dtim << 8) | wlc->bcn_li_bcn);
}

static void
prep_mac80211_status(struct wlc_info *wlc, d11rxhdr_t *rxh, struct sk_buff *p,
		     struct ieee80211_rx_status *rx_status)
{
	u32 tsf_l, tsf_h;
	wlc_d11rxhdr_t *wlc_rxh = (wlc_d11rxhdr_t *) rxh;
	int preamble;
	int channel;
	ratespec_t rspec;
	unsigned char *plcp;

	wlc_read_tsf(wlc, &tsf_l, &tsf_h);	/* mactime */
	rx_status->mactime = tsf_h;
	rx_status->mactime <<= 32;
	rx_status->mactime |= tsf_l;
	rx_status->flag |= RX_FLAG_TSFT;

	channel = WLC_CHAN_CHANNEL(rxh->RxChan);

	/* XXX  Channel/badn needs to be filtered against whether we are single/dual band card */
	if (channel > 14) {
		rx_status->band = IEEE80211_BAND_5GHZ;
		rx_status->freq = wf_channel2mhz(channel, WF_CHAN_FACTOR_5_G);
	} else {
		rx_status->band = IEEE80211_BAND_2GHZ;
		rx_status->freq = wf_channel2mhz(channel, WF_CHAN_FACTOR_2_4_G);
	}

	rx_status->signal = wlc_rxh->rssi;	/* signal */

	/* noise */
	/* qual */
	rx_status->antenna = (rxh->PhyRxStatus_0 & PRXS0_RXANT_UPSUBBAND) ? 1 : 0;	/* ant */

	plcp = p->data;

	rspec = wlc_compute_rspec(rxh, plcp);
	if (IS_MCS(rspec)) {
		rx_status->rate_idx = rspec & RSPEC_RATE_MASK;
		rx_status->flag |= RX_FLAG_HT;
		if (RSPEC_IS40MHZ(rspec))
			rx_status->flag |= RX_FLAG_40MHZ;
	} else {
		switch (RSPEC2RATE(rspec)) {
		case WLC_RATE_1M:
			rx_status->rate_idx = 0;
			break;
		case WLC_RATE_2M:
			rx_status->rate_idx = 1;
			break;
		case WLC_RATE_5M5:
			rx_status->rate_idx = 2;
			break;
		case WLC_RATE_11M:
			rx_status->rate_idx = 3;
			break;
		case WLC_RATE_6M:
			rx_status->rate_idx = 4;
			break;
		case WLC_RATE_9M:
			rx_status->rate_idx = 5;
			break;
		case WLC_RATE_12M:
			rx_status->rate_idx = 6;
			break;
		case WLC_RATE_18M:
			rx_status->rate_idx = 7;
			break;
		case WLC_RATE_24M:
			rx_status->rate_idx = 8;
			break;
		case WLC_RATE_36M:
			rx_status->rate_idx = 9;
			break;
		case WLC_RATE_48M:
			rx_status->rate_idx = 10;
			break;
		case WLC_RATE_54M:
			rx_status->rate_idx = 11;
			break;
		default:
			WL_ERROR(("%s: Unknown rate\n", __func__));
		}

		/* Determine short preamble and rate_idx */
		preamble = 0;
		if (IS_CCK(rspec)) {
			if (rxh->PhyRxStatus_0 & PRXS0_SHORTH)
				WL_ERROR(("Short CCK\n"));
			rx_status->flag |= RX_FLAG_SHORTPRE;
		} else if (IS_OFDM(rspec)) {
			rx_status->flag |= RX_FLAG_SHORTPRE;
		} else {
			WL_ERROR(("%s: Unknown modulation\n", __func__));
		}
	}

	if (PLCP3_ISSGI(plcp[3]))
		rx_status->flag |= RX_FLAG_SHORT_GI;

	if (rxh->RxStatus1 & RXS_DECERR) {
		rx_status->flag |= RX_FLAG_FAILED_PLCP_CRC;
		WL_ERROR(("%s:  RX_FLAG_FAILED_PLCP_CRC\n", __func__));
	}
	if (rxh->RxStatus1 & RXS_FCSERR) {
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
		WL_ERROR(("%s:  RX_FLAG_FAILED_FCS_CRC\n", __func__));
	}
}

static void
wlc_recvctl(struct wlc_info *wlc, struct osl_info *osh, d11rxhdr_t *rxh,
	    struct sk_buff *p)
{
	int len_mpdu;
	struct ieee80211_rx_status rx_status;
#if defined(BCMDBG)
	struct sk_buff *skb = p;
#endif				/* BCMDBG */
	/* Todo:
	 * Cache plcp for first MPDU of AMPD and use chacched version for INTERMEDIATE.
	 * Test for INTERMEDIATE  like so:
	 * if (!(plcp[0] | plcp[1] | plcp[2]))
	 */

	memset(&rx_status, 0, sizeof(rx_status));
	prep_mac80211_status(wlc, rxh, p, &rx_status);

	/* mac header+body length, exclude CRC and plcp header */
	len_mpdu = p->len - D11_PHY_HDR_LEN - DOT11_FCS_LEN;
	skb_pull(p, D11_PHY_HDR_LEN);
	__skb_trim(p, len_mpdu);

	ASSERT(!(p->next));
	ASSERT(!(p->prev));

	ASSERT(IS_ALIGNED((unsigned long)skb->data, 2));

	memcpy(IEEE80211_SKB_RXCB(p), &rx_status, sizeof(rx_status));
	ieee80211_rx_irqsafe(wlc->pub->ieee_hw, p);

	WLCNTINCR(wlc->pub->_cnt->ieee_rx);
	osh->pktalloced--;
	return;
}

void wlc_bss_list_free(struct wlc_info *wlc, wlc_bss_list_t *bss_list)
{
	uint index;
	wlc_bss_info_t *bi;

	if (!bss_list) {
		WL_ERROR(("%s: Attempting to free NULL list\n", __func__));
		return;
	}
	/* inspect all BSS descriptor */
	for (index = 0; index < bss_list->count; index++) {
		bi = bss_list->ptrs[index];
		if (bi) {
			if (bi->bcn_prb) {
				kfree(bi->bcn_prb);
			}
			kfree(bi);
			bss_list->ptrs[index] = NULL;
		}
	}
	bss_list->count = 0;
}

/* Process received frames */
/*
 * Return true if more frames need to be processed. false otherwise.
 * Param 'bound' indicates max. # frames to process before break out.
 */
/* WLC_HIGH_API */
void BCMFASTPATH wlc_recv(struct wlc_info *wlc, struct sk_buff *p)
{
	d11rxhdr_t *rxh;
	struct dot11_header *h;
	struct osl_info *osh;
	u16 fc;
	uint len;
	bool is_amsdu;

	WL_TRACE(("wl%d: wlc_recv\n", wlc->pub->unit));

	osh = wlc->osh;

	/* frame starts with rxhdr */
	rxh = (d11rxhdr_t *) (p->data);

	/* strip off rxhdr */
	skb_pull(p, wlc->hwrxoff);

	/* fixup rx header endianness */
	ltoh16_buf((void *)rxh, sizeof(d11rxhdr_t));

	/* MAC inserts 2 pad bytes for a4 headers or QoS or A-MSDU subframes */
	if (rxh->RxStatus1 & RXS_PBPRES) {
		if (p->len < 2) {
			WLCNTINCR(wlc->pub->_cnt->rxrunt);
			WL_ERROR(("wl%d: wlc_recv: rcvd runt of len %d\n",
				  wlc->pub->unit, p->len));
			goto toss;
		}
		skb_pull(p, 2);
	}

	h = (struct dot11_header *)(p->data + D11_PHY_HDR_LEN);
	len = p->len;

	if (rxh->RxStatus1 & RXS_FCSERR) {
		if (wlc->pub->mac80211_state & MAC80211_PROMISC_BCNS) {
			WL_ERROR(("FCSERR while scanning******* - tossing\n"));
			goto toss;
		} else {
			WL_ERROR(("RCSERR!!!\n"));
			goto toss;
		}
	}

	/* check received pkt has at least frame control field */
	if (len >= D11_PHY_HDR_LEN + sizeof(h->fc)) {
		fc = ltoh16(h->fc);
	} else {
		WLCNTINCR(wlc->pub->_cnt->rxrunt);
		goto toss;
	}

	is_amsdu = rxh->RxStatus2 & RXS_AMSDU_MASK;

	/* explicitly test bad src address to avoid sending bad deauth */
	if (!is_amsdu) {
		/* CTS and ACK CTL frames are w/o a2 */
		if (FC_TYPE(fc) == FC_TYPE_DATA || FC_TYPE(fc) == FC_TYPE_MNG) {
			if ((ETHER_ISNULLADDR(&h->a2) || ETHER_ISMULTI(&h->a2))) {
				WL_ERROR(("wl%d: %s: dropping a frame with "
					"invalid src mac address, a2: %pM\n",
					wlc->pub->unit, __func__, &h->a2));
				WLCNTINCR(wlc->pub->_cnt->rxbadsrcmac);
				goto toss;
			}
			WLCNTINCR(wlc->pub->_cnt->rxfrag);
		}
	}

	/* due to sheer numbers, toss out probe reqs for now */
	if (FC_TYPE(fc) == FC_TYPE_MNG) {
		if ((fc & FC_KIND_MASK) == FC_PROBE_REQ)
			goto toss;
	}

	if (is_amsdu) {
		WL_ERROR(("%s: is_amsdu causing toss\n", __func__));
		goto toss;
	}

	wlc_recvctl(wlc, osh, rxh, p);
	return;

 toss:
	pkt_buf_free_skb(osh, p, false);
}

/* calculate frame duration for Mixed-mode L-SIG spoofing, return
 * number of bytes goes in the length field
 *
 * Formula given by HT PHY Spec v 1.13
 *   len = 3(nsyms + nstream + 3) - 3
 */
u16 BCMFASTPATH
wlc_calc_lsig_len(struct wlc_info *wlc, ratespec_t ratespec, uint mac_len)
{
	uint nsyms, len = 0, kNdps;

	WL_TRACE(("wl%d: wlc_calc_lsig_len: rate %d, len%d\n", wlc->pub->unit,
		  RSPEC2RATE(ratespec), mac_len));

	if (IS_MCS(ratespec)) {
		uint mcs = ratespec & RSPEC_RATE_MASK;
		/* MCS_TXS(mcs) returns num tx streams - 1 */
		int tot_streams = (MCS_TXS(mcs) + 1) + RSPEC_STC(ratespec);

		ASSERT(WLC_PHY_11N_CAP(wlc->band));
		/* the payload duration calculation matches that of regular ofdm */
		/* 1000Ndbps = kbps * 4 */
		kNdps =
		    MCS_RATE(mcs, RSPEC_IS40MHZ(ratespec),
			     RSPEC_ISSGI(ratespec)) * 4;

		if (RSPEC_STC(ratespec) == 0)
			/* NSyms = CEILING((SERVICE + 8*NBytes + TAIL) / Ndbps) */
			nsyms =
			    CEIL((APHY_SERVICE_NBITS + 8 * mac_len +
				  APHY_TAIL_NBITS) * 1000, kNdps);
		else
			/* STBC needs to have even number of symbols */
			nsyms =
			    2 *
			    CEIL((APHY_SERVICE_NBITS + 8 * mac_len +
				  APHY_TAIL_NBITS) * 1000, 2 * kNdps);

		nsyms += (tot_streams + 3);	/* (+3) account for HT-SIG(2) and HT-STF(1) */
		/* 3 bytes/symbol @ legacy 6Mbps rate */
		len = (3 * nsyms) - 3;	/* (-3) excluding service bits and tail bits */
	}

	return (u16) len;
}

/* calculate frame duration of a given rate and length, return time in usec unit */
uint BCMFASTPATH
wlc_calc_frame_time(struct wlc_info *wlc, ratespec_t ratespec, u8 preamble_type,
		    uint mac_len)
{
	uint nsyms, dur = 0, Ndps, kNdps;
	uint rate = RSPEC2RATE(ratespec);

	if (rate == 0) {
		ASSERT(0);
		WL_ERROR(("wl%d: WAR: using rate of 1 mbps\n", wlc->pub->unit));
		rate = WLC_RATE_1M;
	}

	WL_TRACE(("wl%d: wlc_calc_frame_time: rspec 0x%x, preamble_type %d, len%d\n", wlc->pub->unit, ratespec, preamble_type, mac_len));

	if (IS_MCS(ratespec)) {
		uint mcs = ratespec & RSPEC_RATE_MASK;
		int tot_streams = MCS_TXS(mcs) + RSPEC_STC(ratespec);
		ASSERT(WLC_PHY_11N_CAP(wlc->band));
		ASSERT(WLC_IS_MIMO_PREAMBLE(preamble_type));

		dur = PREN_PREAMBLE + (tot_streams * PREN_PREAMBLE_EXT);
		if (preamble_type == WLC_MM_PREAMBLE)
			dur += PREN_MM_EXT;
		/* 1000Ndbps = kbps * 4 */
		kNdps =
		    MCS_RATE(mcs, RSPEC_IS40MHZ(ratespec),
			     RSPEC_ISSGI(ratespec)) * 4;

		if (RSPEC_STC(ratespec) == 0)
			/* NSyms = CEILING((SERVICE + 8*NBytes + TAIL) / Ndbps) */
			nsyms =
			    CEIL((APHY_SERVICE_NBITS + 8 * mac_len +
				  APHY_TAIL_NBITS) * 1000, kNdps);
		else
			/* STBC needs to have even number of symbols */
			nsyms =
			    2 *
			    CEIL((APHY_SERVICE_NBITS + 8 * mac_len +
				  APHY_TAIL_NBITS) * 1000, 2 * kNdps);

		dur += APHY_SYMBOL_TIME * nsyms;
		if (BAND_2G(wlc->band->bandtype))
			dur += DOT11_OFDM_SIGNAL_EXTENSION;
	} else if (IS_OFDM(rate)) {
		dur = APHY_PREAMBLE_TIME;
		dur += APHY_SIGNAL_TIME;
		/* Ndbps = Mbps * 4 = rate(500Kbps) * 2 */
		Ndps = rate * 2;
		/* NSyms = CEILING((SERVICE + 8*NBytes + TAIL) / Ndbps) */
		nsyms =
		    CEIL((APHY_SERVICE_NBITS + 8 * mac_len + APHY_TAIL_NBITS),
			 Ndps);
		dur += APHY_SYMBOL_TIME * nsyms;
		if (BAND_2G(wlc->band->bandtype))
			dur += DOT11_OFDM_SIGNAL_EXTENSION;
	} else {
		/* calc # bits * 2 so factor of 2 in rate (1/2 mbps) will divide out */
		mac_len = mac_len * 8 * 2;
		/* calc ceiling of bits/rate = microseconds of air time */
		dur = (mac_len + rate - 1) / rate;
		if (preamble_type & WLC_SHORT_PREAMBLE)
			dur += BPHY_PLCP_SHORT_TIME;
		else
			dur += BPHY_PLCP_TIME;
	}
	return dur;
}

/* The opposite of wlc_calc_frame_time */
static uint
wlc_calc_frame_len(struct wlc_info *wlc, ratespec_t ratespec, u8 preamble_type,
		   uint dur)
{
	uint nsyms, mac_len, Ndps, kNdps;
	uint rate = RSPEC2RATE(ratespec);

	WL_TRACE(("wl%d: wlc_calc_frame_len: rspec 0x%x, preamble_type %d, dur %d\n", wlc->pub->unit, ratespec, preamble_type, dur));

	if (IS_MCS(ratespec)) {
		uint mcs = ratespec & RSPEC_RATE_MASK;
		int tot_streams = MCS_TXS(mcs) + RSPEC_STC(ratespec);
		ASSERT(WLC_PHY_11N_CAP(wlc->band));
		dur -= PREN_PREAMBLE + (tot_streams * PREN_PREAMBLE_EXT);
		/* payload calculation matches that of regular ofdm */
		if (BAND_2G(wlc->band->bandtype))
			dur -= DOT11_OFDM_SIGNAL_EXTENSION;
		/* kNdbps = kbps * 4 */
		kNdps =
		    MCS_RATE(mcs, RSPEC_IS40MHZ(ratespec),
			     RSPEC_ISSGI(ratespec)) * 4;
		nsyms = dur / APHY_SYMBOL_TIME;
		mac_len =
		    ((nsyms * kNdps) -
		     ((APHY_SERVICE_NBITS + APHY_TAIL_NBITS) * 1000)) / 8000;
	} else if (IS_OFDM(ratespec)) {
		dur -= APHY_PREAMBLE_TIME;
		dur -= APHY_SIGNAL_TIME;
		/* Ndbps = Mbps * 4 = rate(500Kbps) * 2 */
		Ndps = rate * 2;
		nsyms = dur / APHY_SYMBOL_TIME;
		mac_len =
		    ((nsyms * Ndps) -
		     (APHY_SERVICE_NBITS + APHY_TAIL_NBITS)) / 8;
	} else {
		if (preamble_type & WLC_SHORT_PREAMBLE)
			dur -= BPHY_PLCP_SHORT_TIME;
		else
			dur -= BPHY_PLCP_TIME;
		mac_len = dur * rate;
		/* divide out factor of 2 in rate (1/2 mbps) */
		mac_len = mac_len / 8 / 2;
	}
	return mac_len;
}

static uint
wlc_calc_ba_time(struct wlc_info *wlc, ratespec_t rspec, u8 preamble_type)
{
	WL_TRACE(("wl%d: wlc_calc_ba_time: rspec 0x%x, preamble_type %d\n",
		  wlc->pub->unit, rspec, preamble_type));
	/* Spec 9.6: ack rate is the highest rate in BSSBasicRateSet that is less than
	 * or equal to the rate of the immediately previous frame in the FES
	 */
	rspec = WLC_BASIC_RATE(wlc, rspec);
	ASSERT(VALID_RATE_DBG(wlc, rspec));

	/* BA len == 32 == 16(ctl hdr) + 4(ba len) + 8(bitmap) + 4(fcs) */
	return wlc_calc_frame_time(wlc, rspec, preamble_type,
				   (DOT11_BA_LEN + DOT11_BA_BITMAP_LEN +
				    DOT11_FCS_LEN));
}

static uint BCMFASTPATH
wlc_calc_ack_time(struct wlc_info *wlc, ratespec_t rspec, u8 preamble_type)
{
	uint dur = 0;

	WL_TRACE(("wl%d: wlc_calc_ack_time: rspec 0x%x, preamble_type %d\n",
		  wlc->pub->unit, rspec, preamble_type));
	/* Spec 9.6: ack rate is the highest rate in BSSBasicRateSet that is less than
	 * or equal to the rate of the immediately previous frame in the FES
	 */
	rspec = WLC_BASIC_RATE(wlc, rspec);
	ASSERT(VALID_RATE_DBG(wlc, rspec));

	/* ACK frame len == 14 == 2(fc) + 2(dur) + 6(ra) + 4(fcs) */
	dur =
	    wlc_calc_frame_time(wlc, rspec, preamble_type,
				(DOT11_ACK_LEN + DOT11_FCS_LEN));
	return dur;
}

static uint
wlc_calc_cts_time(struct wlc_info *wlc, ratespec_t rspec, u8 preamble_type)
{
	WL_TRACE(("wl%d: wlc_calc_cts_time: ratespec 0x%x, preamble_type %d\n",
		  wlc->pub->unit, rspec, preamble_type));
	return wlc_calc_ack_time(wlc, rspec, preamble_type);
}

/* derive wlc->band->basic_rate[] table from 'rateset' */
void wlc_rate_lookup_init(struct wlc_info *wlc, wlc_rateset_t *rateset)
{
	u8 rate;
	u8 mandatory;
	u8 cck_basic = 0;
	u8 ofdm_basic = 0;
	u8 *br = wlc->band->basic_rate;
	uint i;

	/* incoming rates are in 500kbps units as in 802.11 Supported Rates */
	memset(br, 0, WLC_MAXRATE + 1);

	/* For each basic rate in the rates list, make an entry in the
	 * best basic lookup.
	 */
	for (i = 0; i < rateset->count; i++) {
		/* only make an entry for a basic rate */
		if (!(rateset->rates[i] & WLC_RATE_FLAG))
			continue;

		/* mask off basic bit */
		rate = (rateset->rates[i] & RATE_MASK);

		if (rate > WLC_MAXRATE) {
			WL_ERROR(("wlc_rate_lookup_init: invalid rate 0x%X in rate set\n", rateset->rates[i]));
			continue;
		}

		br[rate] = rate;
	}

	/* The rate lookup table now has non-zero entries for each
	 * basic rate, equal to the basic rate: br[basicN] = basicN
	 *
	 * To look up the best basic rate corresponding to any
	 * particular rate, code can use the basic_rate table
	 * like this
	 *
	 * basic_rate = wlc->band->basic_rate[tx_rate]
	 *
	 * Make sure there is a best basic rate entry for
	 * every rate by walking up the table from low rates
	 * to high, filling in holes in the lookup table
	 */

	for (i = 0; i < wlc->band->hw_rateset.count; i++) {
		rate = wlc->band->hw_rateset.rates[i];
		ASSERT(rate <= WLC_MAXRATE);

		if (br[rate] != 0) {
			/* This rate is a basic rate.
			 * Keep track of the best basic rate so far by
			 * modulation type.
			 */
			if (IS_OFDM(rate))
				ofdm_basic = rate;
			else
				cck_basic = rate;

			continue;
		}

		/* This rate is not a basic rate so figure out the
		 * best basic rate less than this rate and fill in
		 * the hole in the table
		 */

		br[rate] = IS_OFDM(rate) ? ofdm_basic : cck_basic;

		if (br[rate] != 0)
			continue;

		if (IS_OFDM(rate)) {
			/* In 11g and 11a, the OFDM mandatory rates are 6, 12, and 24 Mbps */
			if (rate >= WLC_RATE_24M)
				mandatory = WLC_RATE_24M;
			else if (rate >= WLC_RATE_12M)
				mandatory = WLC_RATE_12M;
			else
				mandatory = WLC_RATE_6M;
		} else {
			/* In 11b, all the CCK rates are mandatory 1 - 11 Mbps */
			mandatory = rate;
		}

		br[rate] = mandatory;
	}
}

static void wlc_write_rate_shm(struct wlc_info *wlc, u8 rate, u8 basic_rate)
{
	u8 phy_rate, index;
	u8 basic_phy_rate, basic_index;
	u16 dir_table, basic_table;
	u16 basic_ptr;

	/* Shared memory address for the table we are reading */
	dir_table = IS_OFDM(basic_rate) ? M_RT_DIRMAP_A : M_RT_DIRMAP_B;

	/* Shared memory address for the table we are writing */
	basic_table = IS_OFDM(rate) ? M_RT_BBRSMAP_A : M_RT_BBRSMAP_B;

	/*
	 * for a given rate, the LS-nibble of the PLCP SIGNAL field is
	 * the index into the rate table.
	 */
	phy_rate = rate_info[rate] & RATE_MASK;
	basic_phy_rate = rate_info[basic_rate] & RATE_MASK;
	index = phy_rate & 0xf;
	basic_index = basic_phy_rate & 0xf;

	/* Find the SHM pointer to the ACK rate entry by looking in the
	 * Direct-map Table
	 */
	basic_ptr = wlc_read_shm(wlc, (dir_table + basic_index * 2));

	/* Update the SHM BSS-basic-rate-set mapping table with the pointer
	 * to the correct basic rate for the given incoming rate
	 */
	wlc_write_shm(wlc, (basic_table + index * 2), basic_ptr);
}

static const wlc_rateset_t *wlc_rateset_get_hwrs(struct wlc_info *wlc)
{
	const wlc_rateset_t *rs_dflt;

	if (WLC_PHY_11N_CAP(wlc->band)) {
		if (BAND_5G(wlc->band->bandtype))
			rs_dflt = &ofdm_mimo_rates;
		else
			rs_dflt = &cck_ofdm_mimo_rates;
	} else if (wlc->band->gmode)
		rs_dflt = &cck_ofdm_rates;
	else
		rs_dflt = &cck_rates;

	return rs_dflt;
}

void wlc_set_ratetable(struct wlc_info *wlc)
{
	const wlc_rateset_t *rs_dflt;
	wlc_rateset_t rs;
	u8 rate, basic_rate;
	uint i;

	rs_dflt = wlc_rateset_get_hwrs(wlc);
	ASSERT(rs_dflt != NULL);

	wlc_rateset_copy(rs_dflt, &rs);
	wlc_rateset_mcs_upd(&rs, wlc->stf->txstreams);

	/* walk the phy rate table and update SHM basic rate lookup table */
	for (i = 0; i < rs.count; i++) {
		rate = rs.rates[i] & RATE_MASK;

		/* for a given rate WLC_BASIC_RATE returns the rate at
		 * which a response ACK/CTS should be sent.
		 */
		basic_rate = WLC_BASIC_RATE(wlc, rate);
		if (basic_rate == 0) {
			/* This should only happen if we are using a
			 * restricted rateset.
			 */
			basic_rate = rs.rates[0] & RATE_MASK;
		}

		wlc_write_rate_shm(wlc, rate, basic_rate);
	}
}

/*
 * Return true if the specified rate is supported by the specified band.
 * WLC_BAND_AUTO indicates the current band.
 */
bool wlc_valid_rate(struct wlc_info *wlc, ratespec_t rspec, int band,
		    bool verbose)
{
	wlc_rateset_t *hw_rateset;
	uint i;

	if ((band == WLC_BAND_AUTO) || (band == wlc->band->bandtype)) {
		hw_rateset = &wlc->band->hw_rateset;
	} else if (NBANDS(wlc) > 1) {
		hw_rateset = &wlc->bandstate[OTHERBANDUNIT(wlc)]->hw_rateset;
	} else {
		/* other band specified and we are a single band device */
		return false;
	}

	/* check if this is a mimo rate */
	if (IS_MCS(rspec)) {
		if (!VALID_MCS((rspec & RSPEC_RATE_MASK)))
			goto error;

		return isset(hw_rateset->mcs, (rspec & RSPEC_RATE_MASK));
	}

	for (i = 0; i < hw_rateset->count; i++)
		if (hw_rateset->rates[i] == RSPEC2RATE(rspec))
			return true;
 error:
	if (verbose) {
		WL_ERROR(("wl%d: wlc_valid_rate: rate spec 0x%x not in hw_rateset\n", wlc->pub->unit, rspec));
	}

	return false;
}

static void wlc_update_mimo_band_bwcap(struct wlc_info *wlc, u8 bwcap)
{
	uint i;
	wlcband_t *band;

	for (i = 0; i < NBANDS(wlc); i++) {
		if (IS_SINGLEBAND_5G(wlc->deviceid))
			i = BAND_5G_INDEX;
		band = wlc->bandstate[i];
		if (band->bandtype == WLC_BAND_5G) {
			if ((bwcap == WLC_N_BW_40ALL)
			    || (bwcap == WLC_N_BW_20IN2G_40IN5G))
				band->mimo_cap_40 = true;
			else
				band->mimo_cap_40 = false;
		} else {
			ASSERT(band->bandtype == WLC_BAND_2G);
			if (bwcap == WLC_N_BW_40ALL)
				band->mimo_cap_40 = true;
			else
				band->mimo_cap_40 = false;
		}
	}

	wlc->mimo_band_bwcap = bwcap;
}

void wlc_mod_prb_rsp_rate_table(struct wlc_info *wlc, uint frame_len)
{
	const wlc_rateset_t *rs_dflt;
	wlc_rateset_t rs;
	u8 rate;
	u16 entry_ptr;
	u8 plcp[D11_PHY_HDR_LEN];
	u16 dur, sifs;
	uint i;

	sifs = SIFS(wlc->band);

	rs_dflt = wlc_rateset_get_hwrs(wlc);
	ASSERT(rs_dflt != NULL);

	wlc_rateset_copy(rs_dflt, &rs);
	wlc_rateset_mcs_upd(&rs, wlc->stf->txstreams);

	/* walk the phy rate table and update MAC core SHM basic rate table entries */
	for (i = 0; i < rs.count; i++) {
		rate = rs.rates[i] & RATE_MASK;

		entry_ptr = wlc_rate_shm_offset(wlc, rate);

		/* Calculate the Probe Response PLCP for the given rate */
		wlc_compute_plcp(wlc, rate, frame_len, plcp);

		/* Calculate the duration of the Probe Response frame plus SIFS for the MAC */
		dur =
		    (u16) wlc_calc_frame_time(wlc, rate, WLC_LONG_PREAMBLE,
						 frame_len);
		dur += sifs;

		/* Update the SHM Rate Table entry Probe Response values */
		wlc_write_shm(wlc, entry_ptr + M_RT_PRS_PLCP_POS,
			      (u16) (plcp[0] + (plcp[1] << 8)));
		wlc_write_shm(wlc, entry_ptr + M_RT_PRS_PLCP_POS + 2,
			      (u16) (plcp[2] + (plcp[3] << 8)));
		wlc_write_shm(wlc, entry_ptr + M_RT_PRS_DUR_POS, dur);
	}
}

u16
wlc_compute_bcntsfoff(struct wlc_info *wlc, ratespec_t rspec,
		      bool short_preamble, bool phydelay)
{
	uint bcntsfoff = 0;

	if (IS_MCS(rspec)) {
		WL_ERROR(("wl%d: recd beacon with mcs rate; rspec 0x%x\n",
			  wlc->pub->unit, rspec));
	} else if (IS_OFDM(rspec)) {
		/* tx delay from MAC through phy to air (2.1 usec) +
		 * phy header time (preamble + PLCP SIGNAL == 20 usec) +
		 * PLCP SERVICE + MAC header time (SERVICE + FC + DUR + A1 + A2 + A3 + SEQ == 26
		 * bytes at beacon rate)
		 */
		bcntsfoff += phydelay ? D11A_PHY_TX_DELAY : 0;
		bcntsfoff += APHY_PREAMBLE_TIME + APHY_SIGNAL_TIME;
		bcntsfoff +=
		    wlc_compute_airtime(wlc, rspec,
					APHY_SERVICE_NBITS / 8 +
					DOT11_MAC_HDR_LEN);
	} else {
		/* tx delay from MAC through phy to air (3.4 usec) +
		 * phy header time (long preamble + PLCP == 192 usec) +
		 * MAC header time (FC + DUR + A1 + A2 + A3 + SEQ == 24 bytes at beacon rate)
		 */
		bcntsfoff += phydelay ? D11B_PHY_TX_DELAY : 0;
		bcntsfoff +=
		    short_preamble ? D11B_PHY_SPREHDR_TIME :
		    D11B_PHY_LPREHDR_TIME;
		bcntsfoff += wlc_compute_airtime(wlc, rspec, DOT11_MAC_HDR_LEN);
	}
	return (u16) (bcntsfoff);
}

/*	Max buffering needed for beacon template/prb resp template is 142 bytes.
 *
 *	PLCP header is 6 bytes.
 *	802.11 A3 header is 24 bytes.
 *	Max beacon frame body template length is 112 bytes.
 *	Max probe resp frame body template length is 110 bytes.
 *
 *      *len on input contains the max length of the packet available.
 *
 *	The *len value is set to the number of bytes in buf used, and starts with the PLCP
 *	and included up to, but not including, the 4 byte FCS.
 */
static void
wlc_bcn_prb_template(struct wlc_info *wlc, uint type, ratespec_t bcn_rspec,
		     wlc_bsscfg_t *cfg, u16 *buf, int *len)
{
	cck_phy_hdr_t *plcp;
	struct dot11_management_header *h;
	int hdr_len, body_len;

	ASSERT(*len >= 142);
	ASSERT(type == FC_BEACON || type == FC_PROBE_RESP);

	if (MBSS_BCN_ENAB(cfg) && type == FC_BEACON)
		hdr_len = DOT11_MAC_HDR_LEN;
	else
		hdr_len = D11_PHY_HDR_LEN + DOT11_MAC_HDR_LEN;
	body_len = *len - hdr_len;	/* calc buffer size provided for frame body */

	*len = hdr_len + body_len;	/* return actual size */

	/* format PHY and MAC headers */
	memset((char *)buf, 0, hdr_len);

	plcp = (cck_phy_hdr_t *) buf;

	/* PLCP for Probe Response frames are filled in from core's rate table */
	if (type == FC_BEACON && !MBSS_BCN_ENAB(cfg)) {
		/* fill in PLCP */
		wlc_compute_plcp(wlc, bcn_rspec,
				 (DOT11_MAC_HDR_LEN + body_len + DOT11_FCS_LEN),
				 (u8 *) plcp);

	}
	/* "Regular" and 16 MBSS but not for 4 MBSS */
	/* Update the phytxctl for the beacon based on the rspec */
	if (!SOFTBCN_ENAB(cfg))
		wlc_beacon_phytxctl_txant_upd(wlc, bcn_rspec);

	if (MBSS_BCN_ENAB(cfg) && type == FC_BEACON)
		h = (struct dot11_management_header *)&plcp[0];
	else
		h = (struct dot11_management_header *)&plcp[1];

	/* fill in 802.11 header */
	h->fc = htol16((u16) type);

	/* DUR is 0 for multicast bcn, or filled in by MAC for prb resp */
	/* A1 filled in by MAC for prb resp, broadcast for bcn */
	if (type == FC_BEACON)
		bcopy((const char *)&ether_bcast, (char *)&h->da,
		      ETHER_ADDR_LEN);
	bcopy((char *)&cfg->cur_etheraddr, (char *)&h->sa, ETHER_ADDR_LEN);
	bcopy((char *)&cfg->BSSID, (char *)&h->bssid, ETHER_ADDR_LEN);

	/* SEQ filled in by MAC */

	return;
}

int wlc_get_header_len()
{
	return TXOFF;
}

/* Update a beacon for a particular BSS
 * For MBSS, this updates the software template and sets "latest" to the index of the
 * template updated.
 * Otherwise, it updates the hardware template.
 */
void wlc_bss_update_beacon(struct wlc_info *wlc, wlc_bsscfg_t *cfg)
{
	int len = BCN_TMPL_LEN;

	/* Clear the soft intmask */
	wlc->defmacintmask &= ~MI_BCNTPL;

	if (!cfg->up) {		/* Only allow updates on an UP bss */
		return;
	}

	if (MBSS_BCN_ENAB(cfg)) {	/* Optimize:  Some of if/else could be combined */
	} else if (HWBCN_ENAB(cfg)) {	/* Hardware beaconing for this config */
		u16 bcn[BCN_TMPL_LEN / 2];
		u32 both_valid = MCMD_BCN0VLD | MCMD_BCN1VLD;
		d11regs_t *regs = wlc->regs;
		struct osl_info *osh = NULL;

		osh = wlc->osh;

		/* Check if both templates are in use, if so sched. an interrupt
		 *      that will call back into this routine
		 */
		if ((R_REG(osh, &regs->maccommand) & both_valid) == both_valid) {
			/* clear any previous status */
			W_REG(osh, &regs->macintstatus, MI_BCNTPL);
		}
		/* Check that after scheduling the interrupt both of the
		 *      templates are still busy. if not clear the int. & remask
		 */
		if ((R_REG(osh, &regs->maccommand) & both_valid) == both_valid) {
			wlc->defmacintmask |= MI_BCNTPL;
			return;
		}

		wlc->bcn_rspec =
		    wlc_lowest_basic_rspec(wlc, &cfg->current_bss->rateset);
		ASSERT(wlc_valid_rate
		       (wlc, wlc->bcn_rspec,
			CHSPEC_IS2G(cfg->current_bss->
				    chanspec) ? WLC_BAND_2G : WLC_BAND_5G,
			true));

		/* update the template and ucode shm */
		wlc_bcn_prb_template(wlc, FC_BEACON, wlc->bcn_rspec, cfg, bcn,
				     &len);
		wlc_write_hw_bcntemplates(wlc, bcn, len, false);
	}
}

/*
 * Update all beacons for the system.
 */
void wlc_update_beacon(struct wlc_info *wlc)
{
	int idx;
	wlc_bsscfg_t *bsscfg;

	/* update AP or IBSS beacons */
	FOREACH_BSS(wlc, idx, bsscfg) {
		if (bsscfg->up && (BSSCFG_AP(bsscfg) || !bsscfg->BSS))
			wlc_bss_update_beacon(wlc, bsscfg);
	}
}

/* Write ssid into shared memory */
void wlc_shm_ssid_upd(struct wlc_info *wlc, wlc_bsscfg_t *cfg)
{
	u8 *ssidptr = cfg->SSID;
	u16 base = M_SSID;
	u8 ssidbuf[DOT11_MAX_SSID_LEN];

	/* padding the ssid with zero and copy it into shm */
	memset(ssidbuf, 0, DOT11_MAX_SSID_LEN);
	bcopy(ssidptr, ssidbuf, cfg->SSID_len);

	wlc_copyto_shm(wlc, base, ssidbuf, DOT11_MAX_SSID_LEN);

	if (!MBSS_BCN_ENAB(cfg))
		wlc_write_shm(wlc, M_SSIDLEN, (u16) cfg->SSID_len);
}

void wlc_update_probe_resp(struct wlc_info *wlc, bool suspend)
{
	int idx;
	wlc_bsscfg_t *bsscfg;

	/* update AP or IBSS probe responses */
	FOREACH_BSS(wlc, idx, bsscfg) {
		if (bsscfg->up && (BSSCFG_AP(bsscfg) || !bsscfg->BSS))
			wlc_bss_update_probe_resp(wlc, bsscfg, suspend);
	}
}

void
wlc_bss_update_probe_resp(struct wlc_info *wlc, wlc_bsscfg_t *cfg, bool suspend)
{
	u16 prb_resp[BCN_TMPL_LEN / 2];
	int len = BCN_TMPL_LEN;

	/* write the probe response to hardware, or save in the config structure */
	if (!MBSS_PRB_ENAB(cfg)) {

		/* create the probe response template */
		wlc_bcn_prb_template(wlc, FC_PROBE_RESP, 0, cfg, prb_resp,
				     &len);

		if (suspend)
			wlc_suspend_mac_and_wait(wlc);

		/* write the probe response into the template region */
		wlc_bmac_write_template_ram(wlc->hw, T_PRS_TPL_BASE,
					    (len + 3) & ~3, prb_resp);

		/* write the length of the probe response frame (+PLCP/-FCS) */
		wlc_write_shm(wlc, M_PRB_RESP_FRM_LEN, (u16) len);

		/* write the SSID and SSID length */
		wlc_shm_ssid_upd(wlc, cfg);

		/*
		 * Write PLCP headers and durations for probe response frames at all rates.
		 * Use the actual frame length covered by the PLCP header for the call to
		 * wlc_mod_prb_rsp_rate_table() by subtracting the PLCP len and adding the FCS.
		 */
		len += (-D11_PHY_HDR_LEN + DOT11_FCS_LEN);
		wlc_mod_prb_rsp_rate_table(wlc, (u16) len);

		if (suspend)
			wlc_enable_mac(wlc);
	} else {		/* Generating probe resp in sw; update local template */
		ASSERT(0 && "No software probe response support without MBSS");
	}
}

/* prepares pdu for transmission. returns BCM error codes */
int wlc_prep_pdu(struct wlc_info *wlc, struct sk_buff *pdu, uint *fifop)
{
	struct osl_info *osh;
	uint fifo;
	d11txh_t *txh;
	struct dot11_header *h;
	struct scb *scb;
	u16 fc;

	osh = wlc->osh;

	ASSERT(pdu);
	txh = (d11txh_t *) (pdu->data);
	ASSERT(txh);
	h = (struct dot11_header *)((u8 *) (txh + 1) + D11_PHY_HDR_LEN);
	ASSERT(h);
	fc = ltoh16(h->fc);

	/* get the pkt queue info. This was put at wlc_sendctl or wlc_send for PDU */
	fifo = ltoh16(txh->TxFrameID) & TXFID_QUEUE_MASK;

	scb = NULL;

	*fifop = fifo;

	/* return if insufficient dma resources */
	if (TXAVAIL(wlc, fifo) < MAX_DMA_SEGS) {
		/* Mark precedences related to this FIFO, unsendable */
		WLC_TX_FIFO_CLEAR(wlc, fifo);
		return BCME_BUSY;
	}

	if (FC_TYPE(ltoh16(txh->MacFrameControl)) != FC_TYPE_DATA)
		WLCNTINCR(wlc->pub->_cnt->txctl);

	return 0;
}

/* init tx reported rate mechanism */
void wlc_reprate_init(struct wlc_info *wlc)
{
	int i;
	wlc_bsscfg_t *bsscfg;

	FOREACH_BSS(wlc, i, bsscfg) {
		wlc_bsscfg_reprate_init(bsscfg);
	}
}

/* per bsscfg init tx reported rate mechanism */
void wlc_bsscfg_reprate_init(wlc_bsscfg_t *bsscfg)
{
	bsscfg->txrspecidx = 0;
	memset((char *)bsscfg->txrspec, 0, sizeof(bsscfg->txrspec));
}

/* Retrieve a consolidated set of revision information,
 * typically for the WLC_GET_REVINFO ioctl
 */
int wlc_get_revision_info(struct wlc_info *wlc, void *buf, uint len)
{
	wlc_rev_info_t *rinfo = (wlc_rev_info_t *) buf;

	if (len < WL_REV_INFO_LEGACY_LENGTH)
		return BCME_BUFTOOSHORT;

	rinfo->vendorid = wlc->vendorid;
	rinfo->deviceid = wlc->deviceid;
	rinfo->radiorev = (wlc->band->radiorev << IDCODE_REV_SHIFT) |
	    (wlc->band->radioid << IDCODE_ID_SHIFT);
	rinfo->chiprev = wlc->pub->sih->chiprev;
	rinfo->corerev = wlc->pub->corerev;
	rinfo->boardid = wlc->pub->sih->boardtype;
	rinfo->boardvendor = wlc->pub->sih->boardvendor;
	rinfo->boardrev = wlc->pub->boardrev;
	rinfo->ucoderev = wlc->ucode_rev;
	rinfo->driverrev = EPI_VERSION_NUM;
	rinfo->bus = wlc->pub->sih->bustype;
	rinfo->chipnum = wlc->pub->sih->chip;

	if (len >= (offsetof(wlc_rev_info_t, chippkg))) {
		rinfo->phytype = wlc->band->phytype;
		rinfo->phyrev = wlc->band->phyrev;
		rinfo->anarev = 0;	/* obsolete stuff, suppress */
	}

	if (len >= sizeof(*rinfo)) {
		rinfo->chippkg = wlc->pub->sih->chippkg;
	}

	return BCME_OK;
}

void wlc_default_rateset(struct wlc_info *wlc, wlc_rateset_t *rs)
{
	wlc_rateset_default(rs, NULL, wlc->band->phytype, wlc->band->bandtype,
			    false, RATE_MASK_FULL, (bool) N_ENAB(wlc->pub),
			    CHSPEC_WLC_BW(wlc->default_bss->chanspec),
			    wlc->stf->txstreams);
}

static void wlc_bss_default_init(struct wlc_info *wlc)
{
	chanspec_t chanspec;
	wlcband_t *band;
	wlc_bss_info_t *bi = wlc->default_bss;

	/* init default and target BSS with some sane initial values */
	memset((char *)(bi), 0, sizeof(wlc_bss_info_t));
	bi->beacon_period = ISSIM_ENAB(wlc->pub->sih) ? BEACON_INTERVAL_DEF_QT :
	    BEACON_INTERVAL_DEFAULT;
	bi->dtim_period = ISSIM_ENAB(wlc->pub->sih) ? DTIM_INTERVAL_DEF_QT :
	    DTIM_INTERVAL_DEFAULT;

	/* fill the default channel as the first valid channel
	 * starting from the 2G channels
	 */
	chanspec = CH20MHZ_CHSPEC(1);
	ASSERT(chanspec != INVCHANSPEC);

	wlc->home_chanspec = bi->chanspec = chanspec;

	/* find the band of our default channel */
	band = wlc->band;
	if (NBANDS(wlc) > 1 && band->bandunit != CHSPEC_WLCBANDUNIT(chanspec))
		band = wlc->bandstate[OTHERBANDUNIT(wlc)];

	/* init bss rates to the band specific default rate set */
	wlc_rateset_default(&bi->rateset, NULL, band->phytype, band->bandtype,
			    false, RATE_MASK_FULL, (bool) N_ENAB(wlc->pub),
			    CHSPEC_WLC_BW(chanspec), wlc->stf->txstreams);

	if (N_ENAB(wlc->pub))
		bi->flags |= WLC_BSS_HT;
}

/* Deferred event processing */
static void wlc_process_eventq(void *arg)
{
	struct wlc_info *wlc = (struct wlc_info *) arg;
	wlc_event_t *etmp;

	while ((etmp = wlc_eventq_deq(wlc->eventq))) {
		/* Perform OS specific event processing */
		wl_event(wlc->wl, etmp->event.ifname, etmp);
		if (etmp->data) {
			kfree(etmp->data);
			etmp->data = NULL;
		}
		wlc_event_free(wlc->eventq, etmp);
	}
}

void
wlc_uint64_sub(u32 *a_high, u32 *a_low, u32 b_high, u32 b_low)
{
	if (b_low > *a_low) {
		/* low half needs a carry */
		b_high += 1;
	}
	*a_low -= b_low;
	*a_high -= b_high;
}

static ratespec_t
mac80211_wlc_set_nrate(struct wlc_info *wlc, wlcband_t *cur_band, u32 int_val)
{
	u8 stf = (int_val & NRATE_STF_MASK) >> NRATE_STF_SHIFT;
	u8 rate = int_val & NRATE_RATE_MASK;
	ratespec_t rspec;
	bool ismcs = ((int_val & NRATE_MCS_INUSE) == NRATE_MCS_INUSE);
	bool issgi = ((int_val & NRATE_SGI_MASK) >> NRATE_SGI_SHIFT);
	bool override_mcs_only = ((int_val & NRATE_OVERRIDE_MCS_ONLY)
				  == NRATE_OVERRIDE_MCS_ONLY);
	int bcmerror = 0;

	if (!ismcs) {
		return (ratespec_t) rate;
	}

	/* validate the combination of rate/mcs/stf is allowed */
	if (N_ENAB(wlc->pub) && ismcs) {
		/* mcs only allowed when nmode */
		if (stf > PHY_TXC1_MODE_SDM) {
			WL_ERROR(("wl%d: %s: Invalid stf\n", WLCWLUNIT(wlc),
				  __func__));
			bcmerror = BCME_RANGE;
			goto done;
		}

		/* mcs 32 is a special case, DUP mode 40 only */
		if (rate == 32) {
			if (!CHSPEC_IS40(wlc->home_chanspec) ||
			    ((stf != PHY_TXC1_MODE_SISO)
			     && (stf != PHY_TXC1_MODE_CDD))) {
				WL_ERROR(("wl%d: %s: Invalid mcs 32\n",
					  WLCWLUNIT(wlc), __func__));
				bcmerror = BCME_RANGE;
				goto done;
			}
			/* mcs > 7 must use stf SDM */
		} else if (rate > HIGHEST_SINGLE_STREAM_MCS) {
			/* mcs > 7 must use stf SDM */
			if (stf != PHY_TXC1_MODE_SDM) {
				WL_TRACE(("wl%d: %s: enabling SDM mode for mcs %d\n", WLCWLUNIT(wlc), __func__, rate));
				stf = PHY_TXC1_MODE_SDM;
			}
		} else {
			/* MCS 0-7 may use SISO, CDD, and for phy_rev >= 3 STBC */
			if ((stf > PHY_TXC1_MODE_STBC) ||
			    (!WLC_STBC_CAP_PHY(wlc)
			     && (stf == PHY_TXC1_MODE_STBC))) {
				WL_ERROR(("wl%d: %s: Invalid STBC\n",
					  WLCWLUNIT(wlc), __func__));
				bcmerror = BCME_RANGE;
				goto done;
			}
		}
	} else if (IS_OFDM(rate)) {
		if ((stf != PHY_TXC1_MODE_CDD) && (stf != PHY_TXC1_MODE_SISO)) {
			WL_ERROR(("wl%d: %s: Invalid OFDM\n", WLCWLUNIT(wlc),
				  __func__));
			bcmerror = BCME_RANGE;
			goto done;
		}
	} else if (IS_CCK(rate)) {
		if ((cur_band->bandtype != WLC_BAND_2G)
		    || (stf != PHY_TXC1_MODE_SISO)) {
			WL_ERROR(("wl%d: %s: Invalid CCK\n", WLCWLUNIT(wlc),
				  __func__));
			bcmerror = BCME_RANGE;
			goto done;
		}
	} else {
		WL_ERROR(("wl%d: %s: Unknown rate type\n", WLCWLUNIT(wlc),
			  __func__));
		bcmerror = BCME_RANGE;
		goto done;
	}
	/* make sure multiple antennae are available for non-siso rates */
	if ((stf != PHY_TXC1_MODE_SISO) && (wlc->stf->txstreams == 1)) {
		WL_ERROR(("wl%d: %s: SISO antenna but !SISO request\n",
			  WLCWLUNIT(wlc), __func__));
		bcmerror = BCME_RANGE;
		goto done;
	}

	rspec = rate;
	if (ismcs) {
		rspec |= RSPEC_MIMORATE;
		/* For STBC populate the STC field of the ratespec */
		if (stf == PHY_TXC1_MODE_STBC) {
			u8 stc;
			stc = 1;	/* Nss for single stream is always 1 */
			rspec |= (stc << RSPEC_STC_SHIFT);
		}
	}

	rspec |= (stf << RSPEC_STF_SHIFT);

	if (override_mcs_only)
		rspec |= RSPEC_OVERRIDE_MCS_ONLY;

	if (issgi)
		rspec |= RSPEC_SHORT_GI;

	if ((rate != 0)
	    && !wlc_valid_rate(wlc, rspec, cur_band->bandtype, true)) {
		return rate;
	}

	return rspec;
 done:
	WL_ERROR(("Hoark\n"));
	return rate;
}

/* formula:  IDLE_BUSY_RATIO_X_16 = (100-duty_cycle)/duty_cycle*16 */
static int
wlc_duty_cycle_set(struct wlc_info *wlc, int duty_cycle, bool isOFDM,
		   bool writeToShm)
{
	int idle_busy_ratio_x_16 = 0;
	uint offset =
	    isOFDM ? M_TX_IDLE_BUSY_RATIO_X_16_OFDM :
	    M_TX_IDLE_BUSY_RATIO_X_16_CCK;
	if (duty_cycle > 100 || duty_cycle < 0) {
		WL_ERROR(("wl%d:  duty cycle value off limit\n",
			  wlc->pub->unit));
		return BCME_RANGE;
	}
	if (duty_cycle)
		idle_busy_ratio_x_16 = (100 - duty_cycle) * 16 / duty_cycle;
	/* Only write to shared memory  when wl is up */
	if (writeToShm)
		wlc_write_shm(wlc, offset, (u16) idle_busy_ratio_x_16);

	if (isOFDM)
		wlc->tx_duty_cycle_ofdm = (u16) duty_cycle;
	else
		wlc->tx_duty_cycle_cck = (u16) duty_cycle;

	return BCME_OK;
}

/* Read a single u16 from shared memory.
 * SHM 'offset' needs to be an even address
 */
u16 wlc_read_shm(struct wlc_info *wlc, uint offset)
{
	return wlc_bmac_read_shm(wlc->hw, offset);
}

/* Write a single u16 to shared memory.
 * SHM 'offset' needs to be an even address
 */
void wlc_write_shm(struct wlc_info *wlc, uint offset, u16 v)
{
	wlc_bmac_write_shm(wlc->hw, offset, v);
}

/* Set a range of shared memory to a value.
 * SHM 'offset' needs to be an even address and
 * Range length 'len' must be an even number of bytes
 */
void wlc_set_shm(struct wlc_info *wlc, uint offset, u16 v, int len)
{
	/* offset and len need to be even */
	ASSERT((offset & 1) == 0);
	ASSERT((len & 1) == 0);

	if (len <= 0)
		return;

	wlc_bmac_set_shm(wlc->hw, offset, v, len);
}

/* Copy a buffer to shared memory.
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 */
void wlc_copyto_shm(struct wlc_info *wlc, uint offset, const void *buf, int len)
{
	/* offset and len need to be even */
	ASSERT((offset & 1) == 0);
	ASSERT((len & 1) == 0);

	if (len <= 0)
		return;
	wlc_bmac_copyto_objmem(wlc->hw, offset, buf, len, OBJADDR_SHM_SEL);

}

/* Copy from shared memory to a buffer.
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 */
void wlc_copyfrom_shm(struct wlc_info *wlc, uint offset, void *buf, int len)
{
	/* offset and len need to be even */
	ASSERT((offset & 1) == 0);
	ASSERT((len & 1) == 0);

	if (len <= 0)
		return;

	wlc_bmac_copyfrom_objmem(wlc->hw, offset, buf, len, OBJADDR_SHM_SEL);
}

/* wrapper BMAC functions to for HIGH driver access */
void wlc_mctrl(struct wlc_info *wlc, u32 mask, u32 val)
{
	wlc_bmac_mctrl(wlc->hw, mask, val);
}

void wlc_corereset(struct wlc_info *wlc, u32 flags)
{
	wlc_bmac_corereset(wlc->hw, flags);
}

void wlc_mhf(struct wlc_info *wlc, u8 idx, u16 mask, u16 val, int bands)
{
	wlc_bmac_mhf(wlc->hw, idx, mask, val, bands);
}

u16 wlc_mhf_get(struct wlc_info *wlc, u8 idx, int bands)
{
	return wlc_bmac_mhf_get(wlc->hw, idx, bands);
}

int wlc_xmtfifo_sz_get(struct wlc_info *wlc, uint fifo, uint *blocks)
{
	return wlc_bmac_xmtfifo_sz_get(wlc->hw, fifo, blocks);
}

void wlc_write_template_ram(struct wlc_info *wlc, int offset, int len,
			    void *buf)
{
	wlc_bmac_write_template_ram(wlc->hw, offset, len, buf);
}

void wlc_write_hw_bcntemplates(struct wlc_info *wlc, void *bcn, int len,
			       bool both)
{
	wlc_bmac_write_hw_bcntemplates(wlc->hw, bcn, len, both);
}

void
wlc_set_addrmatch(struct wlc_info *wlc, int match_reg_offset,
		  const struct ether_addr *addr)
{
	wlc_bmac_set_addrmatch(wlc->hw, match_reg_offset, addr);
}

void wlc_set_rcmta(struct wlc_info *wlc, int idx, const struct ether_addr *addr)
{
	wlc_bmac_set_rcmta(wlc->hw, idx, addr);
}

void wlc_read_tsf(struct wlc_info *wlc, u32 *tsf_l_ptr, u32 *tsf_h_ptr)
{
	wlc_bmac_read_tsf(wlc->hw, tsf_l_ptr, tsf_h_ptr);
}

void wlc_set_cwmin(struct wlc_info *wlc, u16 newmin)
{
	wlc->band->CWmin = newmin;
	wlc_bmac_set_cwmin(wlc->hw, newmin);
}

void wlc_set_cwmax(struct wlc_info *wlc, u16 newmax)
{
	wlc->band->CWmax = newmax;
	wlc_bmac_set_cwmax(wlc->hw, newmax);
}

void wlc_fifoerrors(struct wlc_info *wlc)
{

	wlc_bmac_fifoerrors(wlc->hw);
}

/* Search mem rw utilities */

void wlc_pllreq(struct wlc_info *wlc, bool set, mbool req_bit)
{
	wlc_bmac_pllreq(wlc->hw, set, req_bit);
}

void wlc_reset_bmac_done(struct wlc_info *wlc)
{
}

void wlc_ht_mimops_cap_update(struct wlc_info *wlc, u8 mimops_mode)
{
	wlc->ht_cap.cap &= ~HT_CAP_MIMO_PS_MASK;
	wlc->ht_cap.cap |= (mimops_mode << HT_CAP_MIMO_PS_SHIFT);

	if (AP_ENAB(wlc->pub) && wlc->clk) {
		wlc_update_beacon(wlc);
		wlc_update_probe_resp(wlc, true);
	}
}

/* check for the particular priority flow control bit being set */
bool
wlc_txflowcontrol_prio_isset(struct wlc_info *wlc, wlc_txq_info_t *q, int prio)
{
	uint prio_mask;

	if (prio == ALLPRIO) {
		prio_mask = TXQ_STOP_FOR_PRIOFC_MASK;
	} else {
		ASSERT(prio >= 0 && prio <= MAXPRIO);
		prio_mask = NBITVAL(prio);
	}

	return (q->stopped & prio_mask) == prio_mask;
}

/* propogate the flow control to all interfaces using the given tx queue */
void wlc_txflowcontrol(struct wlc_info *wlc, wlc_txq_info_t *qi,
		       bool on, int prio)
{
	uint prio_bits;
	uint cur_bits;

	WL_ERROR(("%s: flow contro kicks in\n", __func__));

	if (prio == ALLPRIO) {
		prio_bits = TXQ_STOP_FOR_PRIOFC_MASK;
	} else {
		ASSERT(prio >= 0 && prio <= MAXPRIO);
		prio_bits = NBITVAL(prio);
	}

	cur_bits = qi->stopped & prio_bits;

	/* Check for the case of no change and return early
	 * Otherwise update the bit and continue
	 */
	if (on) {
		if (cur_bits == prio_bits) {
			return;
		}
		mboolset(qi->stopped, prio_bits);
	} else {
		if (cur_bits == 0) {
			return;
		}
		mboolclr(qi->stopped, prio_bits);
	}

	/* If there is a flow control override we will not change the external
	 * flow control state.
	 */
	if (qi->stopped & ~TXQ_STOP_FOR_PRIOFC_MASK) {
		return;
	}

	wlc_txflowcontrol_signal(wlc, qi, on, prio);
}

void
wlc_txflowcontrol_override(struct wlc_info *wlc, wlc_txq_info_t *qi, bool on,
			   uint override)
{
	uint prev_override;

	ASSERT(override != 0);
	ASSERT((override & TXQ_STOP_FOR_PRIOFC_MASK) == 0);

	prev_override = (qi->stopped & ~TXQ_STOP_FOR_PRIOFC_MASK);

	/* Update the flow control bits and do an early return if there is
	 * no change in the external flow control state.
	 */
	if (on) {
		mboolset(qi->stopped, override);
		/* if there was a previous override bit on, then setting this
		 * makes no difference.
		 */
		if (prev_override) {
			return;
		}

		wlc_txflowcontrol_signal(wlc, qi, ON, ALLPRIO);
	} else {
		mboolclr(qi->stopped, override);
		/* clearing an override bit will only make a difference for
		 * flow control if it was the only bit set. For any other
		 * override setting, just return
		 */
		if (prev_override != override) {
			return;
		}

		if (qi->stopped == 0) {
			wlc_txflowcontrol_signal(wlc, qi, OFF, ALLPRIO);
		} else {
			int prio;

			for (prio = MAXPRIO; prio >= 0; prio--) {
				if (!mboolisset(qi->stopped, NBITVAL(prio)))
					wlc_txflowcontrol_signal(wlc, qi, OFF,
								 prio);
			}
		}
	}
}

static void wlc_txflowcontrol_reset(struct wlc_info *wlc)
{
	wlc_txq_info_t *qi;

	for (qi = wlc->tx_queues; qi != NULL; qi = qi->next) {
		if (qi->stopped) {
			wlc_txflowcontrol_signal(wlc, qi, OFF, ALLPRIO);
			qi->stopped = 0;
		}
	}
}

static void
wlc_txflowcontrol_signal(struct wlc_info *wlc, wlc_txq_info_t *qi, bool on,
			 int prio)
{
	wlc_if_t *wlcif;

	for (wlcif = wlc->wlcif_list; wlcif != NULL; wlcif = wlcif->next) {
		if (wlcif->qi == qi && wlcif->flags & WLC_IF_LINKED)
			wl_txflowcontrol(wlc->wl, wlcif->wlif, on, prio);
	}
}

static wlc_txq_info_t *wlc_txq_alloc(struct wlc_info *wlc, struct osl_info *osh)
{
	wlc_txq_info_t *qi, *p;

	qi = (wlc_txq_info_t *) wlc_calloc(osh, wlc->pub->unit,
					   sizeof(wlc_txq_info_t));
	if (qi == NULL) {
		return NULL;
	}

	/* Have enough room for control packets along with HI watermark */
	/* Also, add room to txq for total psq packets if all the SCBs leave PS mode */
	/* The watermark for flowcontrol to OS packets will remain the same */
	pktq_init(&qi->q, WLC_PREC_COUNT,
		  (2 * wlc->pub->tunables->datahiwat) + PKTQ_LEN_DEFAULT +
		  wlc->pub->psq_pkts_total);

	/* add this queue to the the global list */
	p = wlc->tx_queues;
	if (p == NULL) {
		wlc->tx_queues = qi;
	} else {
		while (p->next != NULL)
			p = p->next;
		p->next = qi;
	}

	return qi;
}

static void wlc_txq_free(struct wlc_info *wlc, struct osl_info *osh,
			 wlc_txq_info_t *qi)
{
	wlc_txq_info_t *p;

	if (qi == NULL)
		return;

	/* remove the queue from the linked list */
	p = wlc->tx_queues;
	if (p == qi)
		wlc->tx_queues = p->next;
	else {
		while (p != NULL && p->next != qi)
			p = p->next;
		ASSERT(p->next == qi);
		if (p != NULL)
			p->next = p->next->next;
	}

	kfree(qi);
}
