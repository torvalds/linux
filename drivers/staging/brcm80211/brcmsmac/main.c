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

#include <linux/pci_ids.h>
#include <linux/if_ether.h>
#include <net/mac80211.h>
#include <brcm_hw_ids.h>
#include <aiutils.h>
#include <chipcommon.h>
#include "rate.h"
#include "scb.h"
#include "phy/phy_hal.h"
#include "channel.h"
#include "antsel.h"
#include "stf.h"
#include "ampdu.h"
#include "alloc.h"
#include "mac80211_if.h"
#include "ucode_loader.h"
#include "main.h"


/*
 * WPA(2) definitions
 */
#define RSN_CAP_4_REPLAY_CNTRS		2
#define RSN_CAP_16_REPLAY_CNTRS		3

#define WPA_CAP_4_REPLAY_CNTRS		RSN_CAP_4_REPLAY_CNTRS
#define WPA_CAP_16_REPLAY_CNTRS		RSN_CAP_16_REPLAY_CNTRS

/*
 * Indication for txflowcontrol that all priority bits in
 * TXQ_STOP_FOR_PRIOFC_MASK are to be considered.
 */
#define ALLPRIO		-1

/*
 * 32 SSID chars, max of 4 chars for each SSID char "\xFF", plus NULL.
 */
#define SSID_FMT_BUF_LEN	((4 * IEEE80211_MAX_SSID_LEN) + 1)

/* watchdog timer, in unit of ms */
#define	TIMER_INTERVAL_WATCHDOG	1000
/* radio monitor timer, in unit of ms */
#define	TIMER_INTERVAL_RADIOCHK	800

/* Max MPC timeout, in unit of watchdog */
#ifndef BRCMS_MPC_MAX_DELAYCNT
#define	BRCMS_MPC_MAX_DELAYCNT	10
#endif

/* Min MPC timeout, in unit of watchdog */
#define	BRCMS_MPC_MIN_DELAYCNT	1
#define	BRCMS_MPC_THRESHOLD	3	/* MPC count threshold level */

/* beacon interval, in unit of 1024TU */
#define	BEACON_INTERVAL_DEFAULT	100
/* DTIM interval, in unit of beacon interval */
#define	DTIM_INTERVAL_DEFAULT	3

/* Scale down delays to accommodate QT slow speed */
/* beacon interval, in unit of 1024TU */
#define	BEACON_INTERVAL_DEF_QT	20
/* DTIM interval, in unit of beacon interval */
#define	DTIM_INTERVAL_DEF_QT	1

#define	TBTT_ALIGN_LEEWAY_US	100	/* min leeway before first TBTT in us */

/* Software feature flag defines used by wlfeatureflag */
#define WL_SWFL_NOHWRADIO	0x0004
#define WL_SWFL_FLOWCONTROL     0x0008	/* Enable backpressure to OS stack */
#define WL_SWFL_WLBSSSORT	0x0010	/* Per-port supports sorting of BSS */

/* n-mode support capability */
/* 2x2 includes both 1x1 & 2x2 devices
 * reserved #define 2 for future when we want to separate 1x1 & 2x2 and
 * control it independently
 */
#define WL_11N_2x2			1
#define WL_11N_3x3			3
#define WL_11N_4x4			4

/* define 11n feature disable flags */
#define WLFEATURE_DISABLE_11N		0x00000001
#define WLFEATURE_DISABLE_11N_STBC_TX	0x00000002
#define WLFEATURE_DISABLE_11N_STBC_RX	0x00000004
#define WLFEATURE_DISABLE_11N_SGI_TX	0x00000008
#define WLFEATURE_DISABLE_11N_SGI_RX	0x00000010
#define WLFEATURE_DISABLE_11N_AMPDU_TX	0x00000020
#define WLFEATURE_DISABLE_11N_AMPDU_RX	0x00000040
#define WLFEATURE_DISABLE_11N_GF	0x00000080

#define EDCF_ACI_MASK                0x60
#define EDCF_ACI_SHIFT               5
#define EDCF_ECWMIN_MASK             0x0f
#define EDCF_ECWMAX_SHIFT            4
#define EDCF_AIFSN_MASK              0x0f
#define EDCF_AIFSN_MAX               15
#define EDCF_ECWMAX_MASK             0xf0

#define EDCF_AC_BE_TXOP_STA          0x0000
#define EDCF_AC_BK_TXOP_STA          0x0000
#define EDCF_AC_VO_ACI_STA           0x62
#define EDCF_AC_VO_ECW_STA           0x32
#define EDCF_AC_VI_ACI_STA           0x42
#define EDCF_AC_VI_ECW_STA           0x43
#define EDCF_AC_BK_ECW_STA           0xA4
#define EDCF_AC_VI_TXOP_STA          0x005e
#define EDCF_AC_VO_TXOP_STA          0x002f
#define EDCF_AC_BE_ACI_STA           0x03
#define EDCF_AC_BE_ECW_STA           0xA4
#define EDCF_AC_BK_ACI_STA           0x27
#define EDCF_AC_VO_TXOP_AP           0x002f

#define EDCF_TXOP2USEC(txop)         ((txop) << 5)
#define EDCF_ECW2CW(exp)             ((1 << (exp)) - 1)

#define APHY_SYMBOL_TIME	4
#define APHY_PREAMBLE_TIME	16
#define APHY_SIGNAL_TIME	4
#define APHY_SIFS_TIME		16
#define APHY_SERVICE_NBITS	16
#define APHY_TAIL_NBITS		6
#define BPHY_SIFS_TIME		10
#define BPHY_PLCP_SHORT_TIME	96

#define PREN_PREAMBLE		24
#define PREN_MM_EXT		12
#define PREN_PREAMBLE_EXT	4

#define DOT11_MAC_HDR_LEN		24
#define	DOT11_ACK_LEN		10
#define DOT11_BA_LEN		4
#define DOT11_OFDM_SIGNAL_EXTENSION	6
#define DOT11_MIN_FRAG_LEN		256
#define	DOT11_RTS_LEN		16
#define	DOT11_CTS_LEN		10
#define DOT11_BA_BITMAP_LEN		128
#define DOT11_MIN_BEACON_PERIOD		1
#define DOT11_MAX_BEACON_PERIOD		0xFFFF
#define	DOT11_MAXNUMFRAGS	16
#define DOT11_MAX_FRAG_LEN		2346

#define BPHY_PLCP_TIME		192
#define RIFS_11N_TIME		2

#define WME_VER			1
#define WME_SUBTYPE_PARAM_IE	1
#define WME_TYPE		2
#define WME_OUI			"\x00\x50\xf2"

#define AC_BE			0
#define AC_BK			1
#define AC_VI			2
#define AC_VO			3

/*
 * driver maintains internal 'tick'(wlc->pub->now) which increments in 1s
 * OS timer(soft watchdog) it is not a wall clock and won't increment when
 * driver is in "down" state this low resolution driver tick can be used
 * for maintenance tasks such as phy calibration and scb update
 */

/*
 * To inform the ucode of the last mcast frame posted
 * so that it can clear moredata bit
 */
#define BCMCFID(wlc, fid) brcms_b_write_shm((wlc)->hw, M_BCMC_FID, (fid))

#define BRCMS_WAR16165(wlc) ((!AP_ENAB(wlc->pub)) && (wlc->war16165))

/* Find basic rate for a given rate */
#define BRCMS_BASIC_RATE(wlc, rspec) \
	(IS_MCS(rspec) \
	? (wlc)->band->basic_rate[mcs_table[rspec & RSPEC_RATE_MASK].leg_ofdm] \
	: (wlc)->band->basic_rate[rspec & RSPEC_RATE_MASK])

#define FRAMETYPE(r, mimoframe)	\
	(IS_MCS(r) ? mimoframe	: (IS_CCK(r) ? FT_CCK : FT_OFDM))

/* rfdisable delay timer 500 ms, runs of ALP clock */
#define RFDISABLE_DEFAULT	10000000

#define BRCMS_TEMPSENSE_PERIOD		10	/* 10 second timeout */

#define SCAN_IN_PROGRESS(x)	0

#define EPI_VERSION_NUM		0x054b0b00

/* precedences numbers for wlc queues. These are twice as may levels as
 * 802.1D priorities.
 * Odd numbers are used for HI priority traffic at same precedence levels
 * These constants are used ONLY by wlc_prio2prec_map.  Do not use them
 * elsewhere.
 */
#define	_BRCMS_PREC_NONE		0	/* None = - */
#define	_BRCMS_PREC_BK		2	/* BK - Background */
#define	_BRCMS_PREC_BE		4	/* BE - Best-effort */
#define	_BRCMS_PREC_EE		6	/* EE - Excellent-effort */
#define	_BRCMS_PREC_CL		8	/* CL - Controlled Load */
#define	_BRCMS_PREC_VI		10	/* Vi - Video */
#define	_BRCMS_PREC_VO		12	/* Vo - Voice */
#define	_BRCMS_PREC_NC		14	/* NC - Network Control */

#define MAXMACLIST		64	/* max # source MAC matches */
#define BCN_TEMPLATE_COUNT	2

/* The BSS is generating beacons in HW */
#define BRCMS_BSSCFG_HW_BCN	0x20

#define HWBCN_ENAB(cfg)		(((cfg)->flags & BRCMS_BSSCFG_HW_BCN) != 0)

#define MBSS_BCN_ENAB(cfg)       0
#define MBSS_PRB_ENAB(cfg)       0
#define SOFTBCN_ENAB(pub)    (0)

#define	SYNTHPU_DLY_APHY_US	3700	/* a phy synthpu_dly time in us */
#define	SYNTHPU_DLY_BPHY_US	1050	/* b/g phy synthpu_dly time in us */
#define	SYNTHPU_DLY_NPHY_US	2048	/* n phy REV3 synthpu_dly time in us */
#define	SYNTHPU_DLY_LPPHY_US	300	/* lpphy synthpu_dly time in us */

#define	SYNTHPU_DLY_PHY_US_QT	100	/* QT synthpu_dly time in us */

#define	ANTCNT			10	/* vanilla M_MAX_ANTCNT value */

#define DMAREG(wlc_hw, direction, fifonum) \
	((direction == DMA_TX) ? \
		&(wlc_hw->regs->fifo64regs[fifonum].dmaxmt) : \
		&(wlc_hw->regs->fifo64regs[fifonum].dmarcv))

#define APHY_SLOT_TIME		9
#define BPHY_SLOT_TIME		20

/*
 * The following table lists the buffer memory allocated to xmt fifos in HW.
 * the size is in units of 256bytes(one block), total size is HW dependent
 * ucode has default fifo partition, sw can overwrite if necessary
 *
 * This is documented in twiki under the topic UcodeTxFifo. Please ensure
 * the twiki is updated before making changes.
 */

/* Starting corerev for the fifo size table */
#define XMTFIFOTBL_STARTREV	20

/* Check if a particular BSS config is AP or STA */
#define BSSCFG_AP(cfg)		(0)
#define BSSCFG_STA(cfg)		(1)
#define BSSCFG_IBSS(cfg)	(!(cfg)->BSS)

/* iterate through all valid bsscfg entries */
#define FOREACH_BSS(wlc, idx, cfg) \
	for (idx = 0; (int) idx < BRCMS_MAXBSSCFG; idx++) { \
		cfg = (wlc)->bsscfg[idx]; \
		if (!cfg) \
			continue;
/* close marker for iterator code block */
#define END_FOREACH_BSS()	}

/* Shared memory location index for various AC params */
#define wme_shmemacindex(ac)	wme_ac2fifo[ac]

/* currently the best mechanism for determining SIFS is the band in use */
#define SIFS(band) ((band)->bandtype == BRCM_BAND_5G ? APHY_SIFS_TIME : \
						       BPHY_SIFS_TIME);

/* dup state between BMAC(struct brcms_hardware) and HIGH(struct brcms_c_info)
   driver */
struct brcms_b_state {
	u32 machwcap;	/* mac hw capibility */
	u32 preamble_ovr;	/* preamble override */
};

/* local prototypes */
static void brcms_b_clkctl_clk(struct brcms_hardware *wlc, uint mode);

/* used by wlc_wakeucode_init() */
static void brcms_c_write_inits(struct brcms_hardware *wlc_hw,
			    const struct d11init *inits);
static void brcms_ucode_write(struct brcms_hardware *wlc_hw, const u32 ucode[],
			    const uint nbytes);
static void brcms_ucode_download(struct brcms_hardware *wlc);
static void brcms_c_ucode_txant_set(struct brcms_hardware *wlc_hw);

/* used by brcms_c_down() */
static void brcms_c_flushqueues(struct brcms_c_info *wlc);

static void brcms_c_write_mhf(struct brcms_hardware *wlc_hw, u16 *mhfs);
static void brcms_c_mctrl_reset(struct brcms_hardware *wlc_hw);
static void brcms_b_corerev_fifofixup(struct brcms_hardware *wlc_hw);
static bool brcms_b_tx_fifo_suspended(struct brcms_hardware *wlc_hw,
				       uint tx_fifo);
static void brcms_b_tx_fifo_resume(struct brcms_hardware *wlc_hw,
				   uint tx_fifo);

/* Low Level Prototypes */

struct brcms_b_state;

static int brcms_b_attach(struct brcms_c_info *wlc, u16 vendor, u16 device,
			  uint unit, bool piomode, void *regsva,
			  struct pci_dev *btparam);

/* up/down, reset, clk */
static void brcms_b_reset(struct brcms_hardware *wlc_hw);
static int brcms_b_state_get(struct brcms_hardware *wlc_hw,
			      struct brcms_b_state *state);
static void brcms_b_copyfrom_vars(struct brcms_hardware *wlc_hw, char **buf,
				   uint *len);

static void brcms_b_hw_etheraddr(struct brcms_hardware *wlc_hw,
				  u8 *ea);

static bool brcms_b_radio_read_hwdisabled(struct brcms_hardware *wlc_hw);
static void brcms_b_wait_for_wake(struct brcms_hardware *wlc_hw);

static void brcms_b_set_addrmatch(struct brcms_hardware *wlc_hw,
				   int match_reg_offset,
				   const u8 *addr);
static void brcms_b_set_cwmin(struct brcms_hardware *wlc_hw, u16 newmin);
static void brcms_b_set_cwmax(struct brcms_hardware *wlc_hw, u16 newmax);

static void brcms_b_retrylimit_upd(struct brcms_hardware *wlc_hw, u16 SRL,
				    u16 LRL);

static void brcms_b_fifoerrors(struct brcms_hardware *wlc_hw);

static void brcms_b_pllreq(struct brcms_hardware *wlc_hw, bool set,
			   u32 req_bit);
static void brcms_b_antsel_set(struct brcms_hardware *wlc_hw,
			       u32 antsel_avail);
static int brcms_b_bandtype(struct brcms_hardware *wlc_hw);
static void brcms_b_info_init(struct brcms_hardware *wlc_hw);
static void brcms_b_xtal(struct brcms_hardware *wlc_hw, bool want);
static u16 brcms_b_read_objmem(struct brcms_hardware *wlc_hw, uint offset,
				   u32 sel);
static void brcms_b_write_objmem(struct brcms_hardware *wlc_hw, uint offset,
				  u16 v, u32 sel);
static void brcms_b_core_phy_clk(struct brcms_hardware *wlc_hw, bool clk);
static bool brcms_b_attach_dmapio(struct brcms_c_info *wlc, uint j, bool wme);
static void brcms_b_detach_dmapio(struct brcms_hardware *wlc_hw);
static void brcms_c_ucode_bsinit(struct brcms_hardware *wlc_hw);
static bool brcms_c_validboardtype(struct brcms_hardware *wlc);
static bool brcms_c_isgoodchip(struct brcms_hardware *wlc_hw);
static bool brcms_b_validate_chip_access(struct brcms_hardware *wlc_hw);
static char *brcms_c_get_macaddr(struct brcms_hardware *wlc_hw);
static void brcms_c_mhfdef(struct brcms_c_info *wlc, u16 *mhfs, u16 mhf2_init);
static void brcms_c_mctrl_write(struct brcms_hardware *wlc_hw);
static void brcms_b_mute(struct brcms_hardware *wlc_hw, bool want, u32 flags);
static void brcms_c_ucode_mute_override_set(struct brcms_hardware *wlc_hw);
static void brcms_c_ucode_mute_override_clear(struct brcms_hardware *wlc_hw);
static u32 brcms_c_wlintrsoff(struct brcms_c_info *wlc);
static void brcms_c_wlintrsrestore(struct brcms_c_info *wlc, u32 macintmask);
static void brcms_c_gpio_init(struct brcms_c_info *wlc);
static void brcms_c_write_hw_bcntemplate0(struct brcms_hardware *wlc_hw,
					  u16 bcn[], int len);
static void brcms_c_write_hw_bcntemplate1(struct brcms_hardware *wlc_hw,
					  u16 bcn[], int len);
static void brcms_b_bsinit(struct brcms_c_info *wlc, u16 chanspec);
static u32 brcms_c_setband_inact(struct brcms_c_info *wlc, uint bandunit);
static void brcms_b_setband(struct brcms_hardware *wlc_hw, uint bandunit,
			     u16 chanspec);
static void brcms_b_update_slot_timing(struct brcms_hardware *wlc_hw,
					bool shortslot);
static void brcms_upd_ofdm_pctl1_table(struct brcms_hardware *wlc_hw);
static u16 brcms_b_ofdm_ratetable_offset(struct brcms_hardware *wlc_hw,
					     u8 rate);

static u16 brcms_c_d11hdrs_mac80211(struct brcms_c_info *wlc,
					       struct ieee80211_hw *hw,
					       struct sk_buff *p,
					       struct scb *scb, uint frag,
					       uint nfrags, uint queue,
					       uint next_frag_len,
					       struct wsec_key *key,
					       u32 rspec_override);
static void brcms_c_bss_default_init(struct brcms_c_info *wlc);
static void brcms_c_ucode_mac_upd(struct brcms_c_info *wlc);
static u32 mac80211_wlc_set_nrate(struct brcms_c_info *wlc,
					 struct brcms_band *cur_band,
					 u32 int_val);
static void brcms_c_tx_prec_map_init(struct brcms_c_info *wlc);
static void brcms_c_watchdog(void *arg);
static void brcms_c_watchdog_by_timer(void *arg);
static u16 brcms_c_rate_shm_offset(struct brcms_c_info *wlc, u8 rate);
static int brcms_c_set_rateset(struct brcms_c_info *wlc,
			       struct brcms_c_rateset *rs_arg);
static u8 brcms_c_local_constraint_qdbm(struct brcms_c_info *wlc);

/* send and receive */
static struct brcms_txq_info *brcms_c_txq_alloc(struct brcms_c_info *wlc);
static void brcms_c_txq_free(struct brcms_c_info *wlc,
			 struct brcms_txq_info *qi);
static void brcms_c_txflowcontrol_signal(struct brcms_c_info *wlc,
				     struct brcms_txq_info *qi,
				     bool on, int prio);
static void brcms_c_txflowcontrol_reset(struct brcms_c_info *wlc);
static void brcms_c_compute_cck_plcp(struct brcms_c_info *wlc, u32 rate,
				 uint length, u8 *plcp);
static void brcms_c_compute_ofdm_plcp(u32 rate, uint length, u8 *plcp);
static void brcms_c_compute_mimo_plcp(u32 rate, uint length, u8 *plcp);
static u16 brcms_c_compute_frame_dur(struct brcms_c_info *wlc, u32 rate,
				    u8 preamble_type, uint next_frag_len);
static u64 brcms_c_recover_tsf64(struct brcms_c_info *wlc,
			     struct brcms_d11rxhdr *rxh);
static void brcms_c_recvctl(struct brcms_c_info *wlc,
			struct d11rxhdr *rxh, struct sk_buff *p);
static uint brcms_c_calc_frame_len(struct brcms_c_info *wlc, u32 rate,
			       u8 preamble_type, uint dur);
static uint brcms_c_calc_ack_time(struct brcms_c_info *wlc, u32 rate,
			      u8 preamble_type);
static uint brcms_c_calc_cts_time(struct brcms_c_info *wlc, u32 rate,
			      u8 preamble_type);
/* interrupt, up/down, band */
static void brcms_c_setband(struct brcms_c_info *wlc, uint bandunit);
static u16 brcms_c_init_chanspec(struct brcms_c_info *wlc);
static void brcms_c_bandinit_ordered(struct brcms_c_info *wlc,
				     u16 chanspec);
static void brcms_c_bsinit(struct brcms_c_info *wlc);
static int brcms_c_duty_cycle_set(struct brcms_c_info *wlc, int duty_cycle,
			      bool isOFDM, bool writeToShm);
static void brcms_c_radio_hwdisable_upd(struct brcms_c_info *wlc);
static bool brcms_c_radio_monitor_start(struct brcms_c_info *wlc);
static void brcms_c_radio_timer(void *arg);
static void brcms_c_radio_enable(struct brcms_c_info *wlc);
static void brcms_c_radio_upd(struct brcms_c_info *wlc);

/* scan, association, BSS */
static uint brcms_c_calc_ba_time(struct brcms_c_info *wlc, u32 rate,
			     u8 preamble_type);
static void brcms_c_update_mimo_band_bwcap(struct brcms_c_info *wlc, u8 bwcap);
static void brcms_c_ht_update_sgi_rx(struct brcms_c_info *wlc, int val);
static void brcms_c_ht_update_ldpc(struct brcms_c_info *wlc, s8 val);
static void brcms_c_war16165(struct brcms_c_info *wlc, bool tx);

static void brcms_c_wme_retries_write(struct brcms_c_info *wlc);
static bool brcms_c_attach_stf_ant_init(struct brcms_c_info *wlc);
static uint brcms_c_attach_module(struct brcms_c_info *wlc);
static void brcms_c_detach_module(struct brcms_c_info *wlc);
static void brcms_c_timers_deinit(struct brcms_c_info *wlc);
static void brcms_c_down_led_upd(struct brcms_c_info *wlc);
static uint brcms_c_down_del_timer(struct brcms_c_info *wlc);
static void brcms_c_ofdm_rateset_war(struct brcms_c_info *wlc);
static int _brcms_c_ioctl(struct brcms_c_info *wlc, int cmd, void *arg, int len,
		      struct brcms_c_if *wlcif);
static void brcms_c_write_hw_bcntemplates(struct brcms_c_info *wlc, u16 bcn[],
					  int len, bool both);

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

/* debug/trace */
uint brcm_msg_level =
#if defined(BCMDBG)
	LOG_ERROR_VAL;
#else
	0;
#endif				/* BCMDBG */

/* TX FIFO number to WME/802.1E Access Category */
const u8 wme_fifo2ac[] = { AC_BK, AC_BE, AC_VI, AC_VO, AC_BE, AC_BE };

/* WME/802.1E Access Category to TX FIFO number */
static const u8 wme_ac2fifo[] = { 1, 0, 2, 3 };

static bool in_send_q;

/* 802.1D Priority to precedence queue mapping */
const u8 wlc_prio2prec_map[] = {
	_BRCMS_PREC_BE,		/* 0 BE - Best-effort */
	_BRCMS_PREC_BK,		/* 1 BK - Background */
	_BRCMS_PREC_NONE,		/* 2 None = - */
	_BRCMS_PREC_EE,		/* 3 EE - Excellent-effort */
	_BRCMS_PREC_CL,		/* 4 CL - Controlled Load */
	_BRCMS_PREC_VI,		/* 5 Vi - Video */
	_BRCMS_PREC_VO,		/* 6 Vo - Voice */
	_BRCMS_PREC_NC,		/* 7 NC - Network Control */
};

static u16 xmtfifo_sz[][NFIFO] = {
	/* corerev 20: 5120, 49152, 49152, 5376, 4352, 1280 */
	{20, 192, 192, 21, 17, 5},
	/* corerev 21: 2304, 14848, 5632, 3584, 3584, 1280 */
	{9, 58, 22, 14, 14, 5},
	/* corerev 22: 5120, 49152, 49152, 5376, 4352, 1280 */
	{20, 192, 192, 21, 17, 5},
	/* corerev 23: 5120, 49152, 49152, 5376, 4352, 1280 */
	{20, 192, 192, 21, 17, 5},
	/* corerev 24: 2304, 14848, 5632, 3584, 3584, 1280 */
	{9, 58, 22, 14, 14, 5},
};

static const u8 acbitmap2maxprio[] = {
	PRIO_8021D_BE, PRIO_8021D_BE, PRIO_8021D_BK, PRIO_8021D_BK,
	PRIO_8021D_VI, PRIO_8021D_VI, PRIO_8021D_VI, PRIO_8021D_VI,
	PRIO_8021D_VO, PRIO_8021D_VO, PRIO_8021D_VO, PRIO_8021D_VO,
	PRIO_8021D_VO, PRIO_8021D_VO, PRIO_8021D_VO, PRIO_8021D_VO
};

#ifdef BCMDBG
static const char * const fifo_names[] = {
	"AC_BK", "AC_BE", "AC_VI", "AC_VO", "BCMC", "ATIM" };
#else
static const char fifo_names[6][0];
#endif

#ifdef BCMDBG
/* pointer to most recently allocated wl/wlc */
static struct brcms_c_info *wlc_info_dbg = (struct brcms_c_info *) (NULL);
#endif

/*
 * Update the slot timing for standard 11b/g (20us slots)
 * or shortslot 11g (9us slots)
 * The PSM needs to be suspended for this call.
 */
static void brcms_b_update_slot_timing(struct brcms_hardware *wlc_hw,
					bool shortslot)
{
	struct d11regs *regs;

	regs = wlc_hw->regs;

	if (shortslot) {
		/* 11g short slot: 11a timing */
		W_REG(&regs->ifs_slot, 0x0207);	/* APHY_SLOT_TIME */
		brcms_b_write_shm(wlc_hw, M_DOT11_SLOT, APHY_SLOT_TIME);
	} else {
		/* 11g long slot: 11b timing */
		W_REG(&regs->ifs_slot, 0x0212);	/* BPHY_SLOT_TIME */
		brcms_b_write_shm(wlc_hw, M_DOT11_SLOT, BPHY_SLOT_TIME);
	}
}

static void brcms_c_ucode_bsinit(struct brcms_hardware *wlc_hw)
{
	struct wiphy *wiphy = wlc_hw->wlc->wiphy;

	/* init microcode host flags */
	brcms_c_write_mhf(wlc_hw, wlc_hw->band->mhfs);

	/* do band-specific ucode IHR, SHM, and SCR inits */
	if (D11REV_IS(wlc_hw->corerev, 23)) {
		if (BRCMS_ISNPHY(wlc_hw->band))
			brcms_c_write_inits(wlc_hw, d11n0bsinitvals16);
		else
			wiphy_err(wiphy, "%s: wl%d: unsupported phy in corerev"
				  " %d\n", __func__, wlc_hw->unit,
				  wlc_hw->corerev);
	} else {
		if (D11REV_IS(wlc_hw->corerev, 24)) {
			if (BRCMS_ISLCNPHY(wlc_hw->band))
				brcms_c_write_inits(wlc_hw,
						    d11lcn0bsinitvals24);
			else
				wiphy_err(wiphy, "%s: wl%d: unsupported phy in"
					  " core rev %d\n", __func__,
					  wlc_hw->unit, wlc_hw->corerev);
		} else {
			wiphy_err(wiphy, "%s: wl%d: unsupported corerev %d\n",
				__func__, wlc_hw->unit, wlc_hw->corerev);
		}
	}
}

/* switch to new band but leave it inactive */
static u32 brcms_c_setband_inact(struct brcms_c_info *wlc, uint bandunit)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	u32 macintmask;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc_hw->unit);

	WARN_ON((R_REG(&wlc_hw->regs->maccontrol) & MCTL_EN_MAC) != 0);

	/* disable interrupts */
	macintmask = brcms_intrsoff(wlc->wl);

	/* radio off */
	wlc_phy_switch_radio(wlc_hw->band->pi, OFF);

	brcms_b_core_phy_clk(wlc_hw, OFF);

	brcms_c_setxband(wlc_hw, bandunit);

	return macintmask;
}

/* Process received frames */
/*
 * Return true if more frames need to be processed. false otherwise.
 * Param 'bound' indicates max. # frames to process before break out.
 */
static bool
brcms_b_recv(struct brcms_hardware *wlc_hw, uint fifo, bool bound)
{
	struct sk_buff *p;
	struct sk_buff *head = NULL;
	struct sk_buff *tail = NULL;
	uint n = 0;
	uint bound_limit = bound ? wlc_hw->wlc->pub->tunables->rxbnd : -1;
	struct brcms_d11rxhdr *wlc_rxhdr = NULL;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);
	/* gather received frames */
	while ((p = dma_rx(wlc_hw->di[fifo]))) {

		if (!tail)
			head = tail = p;
		else {
			tail->prev = p;
			tail = p;
		}

		/* !give others some time to run! */
		if (++n >= bound_limit)
			break;
	}

	/* post more rbufs */
	dma_rxfill(wlc_hw->di[fifo]);

	/* process each frame */
	while ((p = head) != NULL) {
		head = head->prev;
		p->prev = NULL;

		wlc_rxhdr = (struct brcms_d11rxhdr *) p->data;

		/*
		 * compute the RSSI from d11rxhdr and record it in wlc_rxd11hr
		 */
		wlc_phy_rssi_compute(wlc_hw->band->pi, wlc_rxhdr);

		brcms_c_recv(wlc_hw->wlc, p);
	}

	return n >= bound_limit;
}

static bool
brcms_b_dotxstatus(struct brcms_hardware *wlc_hw, struct tx_status *txs,
		   u32 s2)
{
	/* discard intermediate indications for ucode with one legitimate case:
	 *   e.g. if "useRTS" is set. ucode did a successful rts/cts exchange,
	 *   but the subsequent tx of DATA failed. so it will start rts/cts from
	 *   the beginning (resetting the rts transmission count)
	 */
	if (!(txs->status & TX_STATUS_AMPDU)
	    && (txs->status & TX_STATUS_INTERMEDIATE))
		return false;

	return brcms_c_dotxstatus(wlc_hw->wlc, txs, s2);
}

/* process tx completion events in BMAC
 * Return true if more tx status need to be processed. false otherwise.
 */
static bool
brcms_b_txstatus(struct brcms_hardware *wlc_hw, bool bound, bool *fatal)
{
	bool morepending = false;
	struct brcms_c_info *wlc = wlc_hw->wlc;
	struct d11regs *regs;
	struct tx_status txstatus, *txs;
	u32 s1, s2;
	uint n = 0;
	/*
	 * Param 'max_tx_num' indicates max. # tx status to process before
	 * break out.
	 */
	uint max_tx_num = bound ? wlc->pub->tunables->txsbnd : -1;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc_hw->unit);

	txs = &txstatus;
	regs = wlc_hw->regs;
	while (!(*fatal)
	       && (s1 = R_REG(&regs->frmtxstatus)) & TXS_V) {

		if (s1 == 0xffffffff) {
			wiphy_err(wlc->wiphy, "wl%d: %s: dead chip\n",
				wlc_hw->unit, __func__);
			return morepending;
		}

			s2 = R_REG(&regs->frmtxstatus2);

		txs->status = s1 & TXS_STATUS_MASK;
		txs->frameid = (s1 & TXS_FID_MASK) >> TXS_FID_SHIFT;
		txs->sequence = s2 & TXS_SEQ_MASK;
		txs->phyerr = (s2 & TXS_PTX_MASK) >> TXS_PTX_SHIFT;
		txs->lasttxtime = 0;

		*fatal = brcms_b_dotxstatus(wlc_hw, txs, s2);

		/* !give others some time to run! */
		if (++n >= max_tx_num)
			break;
	}

	if (*fatal)
		return 0;

	if (n >= max_tx_num)
		morepending = true;

	if (!pktq_empty(&wlc->pkt_queue->q))
		brcms_c_send_q(wlc);

	return morepending;
}

/* second-level interrupt processing
 *   Return true if another dpc needs to be re-scheduled. false otherwise.
 *   Param 'bounded' indicates if applicable loops should be bounded.
 */
bool brcms_c_dpc(struct brcms_c_info *wlc, bool bounded)
{
	u32 macintstatus;
	struct brcms_hardware *wlc_hw = wlc->hw;
	struct d11regs *regs = wlc_hw->regs;
	bool fatal = false;
	struct wiphy *wiphy = wlc->wiphy;

	if (DEVICEREMOVED(wlc)) {
		wiphy_err(wiphy, "wl%d: %s: dead chip\n", wlc_hw->unit,
			  __func__);
		brcms_down(wlc->wl);
		return false;
	}

	/* grab and clear the saved software intstatus bits */
	macintstatus = wlc->macintstatus;
	wlc->macintstatus = 0;

	BCMMSG(wlc->wiphy, "wl%d: macintstatus 0x%x\n",
	       wlc_hw->unit, macintstatus);

	WARN_ON(macintstatus & MI_PRQ); /* PRQ Interrupt in non-MBSS */

	/* BCN template is available */
	/* ZZZ: Use AP_ACTIVE ? */
	if (AP_ENAB(wlc->pub) && (!APSTA_ENAB(wlc->pub))
	    && (macintstatus & MI_BCNTPL))
		brcms_c_update_beacon(wlc);

	/* tx status */
	if (macintstatus & MI_TFS) {
		if (brcms_b_txstatus(wlc->hw, bounded, &fatal))
			wlc->macintstatus |= MI_TFS;
		if (fatal) {
			wiphy_err(wiphy, "MI_TFS: fatal\n");
			goto fatal;
		}
	}

	if (macintstatus & (MI_TBTT | MI_DTIM_TBTT))
		brcms_c_tbtt(wlc);

	/* ATIM window end */
	if (macintstatus & MI_ATIMWINEND) {
		BCMMSG(wlc->wiphy, "end of ATIM window\n");
		OR_REG(&regs->maccommand, wlc->qvalid);
		wlc->qvalid = 0;
	}

	/*
	 * received data or control frame, MI_DMAINT is
	 * indication of RX_FIFO interrupt
	 */
	if (macintstatus & MI_DMAINT)
		if (brcms_b_recv(wlc_hw, RX_FIFO, bounded))
			wlc->macintstatus |= MI_DMAINT;

	/* TX FIFO suspend/flush completion */
	if (macintstatus & MI_TXSTOP)
		brcms_b_tx_fifo_suspended(wlc_hw, TX_DATA_FIFO);

	/* noise sample collected */
	if (macintstatus & MI_BG_NOISE)
		wlc_phy_noise_sample_intr(wlc_hw->band->pi);

	if (macintstatus & MI_GP0) {
		wiphy_err(wiphy, "wl%d: PSM microcode watchdog fired at %d "
			"(seconds). Resetting.\n", wlc_hw->unit, wlc_hw->now);

		printk_once("%s : PSM Watchdog, chipid 0x%x, chiprev 0x%x\n",
					__func__, wlc_hw->sih->chip,
					wlc_hw->sih->chiprev);
		/* big hammer */
		brcms_init(wlc->wl);
	}

	/* gptimer timeout */
	if (macintstatus & MI_TO)
		W_REG(&regs->gptimer, 0);

	if (macintstatus & MI_RFDISABLE) {
		BCMMSG(wlc->wiphy, "wl%d: BMAC Detected a change on the"
		       " RF Disable Input\n", wlc_hw->unit);
		brcms_rfkill_set_hw_state(wlc->wl);
	}

	/* send any enq'd tx packets. Just makes sure to jump start tx */
	if (!pktq_empty(&wlc->pkt_queue->q))
		brcms_c_send_q(wlc);

	/* it isn't done and needs to be resched if macintstatus is non-zero */
	return wlc->macintstatus != 0;

 fatal:
	brcms_init(wlc->wl);
	return wlc->macintstatus != 0;
}

int brcms_b_state_get(struct brcms_hardware *wlc_hw,
		      struct brcms_b_state *state)
{
	state->machwcap = wlc_hw->machwcap;

	return 0;
}

static bool brcms_b_attach_dmapio(struct brcms_c_info *wlc, uint j, bool wme)
{
	uint i;
	char name[8];
	/*
	 * ucode host flag 2 needed for pio mode, independent of band and fifo
	 */
	u16 pio_mhf2 = 0;
	struct brcms_hardware *wlc_hw = wlc->hw;
	uint unit = wlc_hw->unit;
	struct brcms_tunables *tune = wlc->pub->tunables;
	struct wiphy *wiphy = wlc->wiphy;

	/* name and offsets for dma_attach */
	snprintf(name, sizeof(name), "wl%d", unit);

	if (wlc_hw->di[0] == 0) {	/* Init FIFOs */
		int dma_attach_err = 0;

		/*
		 * FIFO 0
		 * TX: TX_AC_BK_FIFO (TX AC Background data packets)
		 * RX: RX_FIFO (RX data packets)
		 */
		wlc_hw->di[0] = dma_attach(name, wlc_hw->sih,
					   (wme ? DMAREG(wlc_hw, DMA_TX, 0) :
					    NULL), DMAREG(wlc_hw, DMA_RX, 0),
					   (wme ? tune->ntxd : 0), tune->nrxd,
					   tune->rxbufsz, -1, tune->nrxbufpost,
					   BRCMS_HWRXOFF, &brcm_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[0]);

		/*
		 * FIFO 1
		 * TX: TX_AC_BE_FIFO (TX AC Best-Effort data packets)
		 *   (legacy) TX_DATA_FIFO (TX data packets)
		 * RX: UNUSED
		 */
		wlc_hw->di[1] = dma_attach(name, wlc_hw->sih,
					   DMAREG(wlc_hw, DMA_TX, 1), NULL,
					   tune->ntxd, 0, 0, -1, 0, 0,
					   &brcm_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[1]);

		/*
		 * FIFO 2
		 * TX: TX_AC_VI_FIFO (TX AC Video data packets)
		 * RX: UNUSED
		 */
		wlc_hw->di[2] = dma_attach(name, wlc_hw->sih,
					   DMAREG(wlc_hw, DMA_TX, 2), NULL,
					   tune->ntxd, 0, 0, -1, 0, 0,
					   &brcm_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[2]);
		/*
		 * FIFO 3
		 * TX: TX_AC_VO_FIFO (TX AC Voice data packets)
		 *   (legacy) TX_CTL_FIFO (TX control & mgmt packets)
		 */
		wlc_hw->di[3] = dma_attach(name, wlc_hw->sih,
					   DMAREG(wlc_hw, DMA_TX, 3),
					   NULL, tune->ntxd, 0, 0, -1,
					   0, 0, &brcm_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[3]);
/* Cleaner to leave this as if with AP defined */

		if (dma_attach_err) {
			wiphy_err(wiphy, "wl%d: wlc_attach: dma_attach failed"
				  "\n", unit);
			return false;
		}

		/* get pointer to dma engine tx flow control variable */
		for (i = 0; i < NFIFO; i++)
			if (wlc_hw->di[i])
				wlc_hw->txavail[i] =
				    (uint *) dma_getvar(wlc_hw->di[i],
							"&txavail");
	}

	/* initial ucode host flags */
	brcms_c_mhfdef(wlc, wlc_hw->band->mhfs, pio_mhf2);

	return true;
}

static void brcms_b_detach_dmapio(struct brcms_hardware *wlc_hw)
{
	uint j;

	for (j = 0; j < NFIFO; j++) {
		if (wlc_hw->di[j]) {
			dma_detach(wlc_hw->di[j]);
			wlc_hw->di[j] = NULL;
		}
	}
}

/*
 * Initialize brcms_c_info default values ...
 * may get overrides later in this function
 *  BMAC_NOTES, move low out and resolve the dangling ones
 */
static void brcms_b_info_init(struct brcms_hardware *wlc_hw)
{
	struct brcms_c_info *wlc = wlc_hw->wlc;

	/* set default sw macintmask value */
	wlc->defmacintmask = DEF_MACINTMASK;

	/* various 802.11g modes */
	wlc_hw->shortslot = false;

	wlc_hw->SFBL = RETRY_SHORT_FB;
	wlc_hw->LFBL = RETRY_LONG_FB;

	/* default mac retry limits */
	wlc_hw->SRL = RETRY_SHORT_DEF;
	wlc_hw->LRL = RETRY_LONG_DEF;
	wlc_hw->chanspec = CH20MHZ_CHSPEC(1);
}

void brcms_b_wait_for_wake(struct brcms_hardware *wlc_hw)
{
	/* delay before first read of ucode state */
	udelay(40);

	/* wait until ucode is no longer asleep */
	SPINWAIT((brcms_b_read_shm(wlc_hw, M_UCODE_DBGST) ==
		  DBGST_ASLEEP), wlc_hw->wlc->fastpwrup_dly);
}

void brcms_b_hw_etheraddr(struct brcms_hardware *wlc_hw, u8 *ea)
{
	memcpy(ea, wlc_hw->etheraddr, ETH_ALEN);
}

static int brcms_b_bandtype(struct brcms_hardware *wlc_hw)
{
	return wlc_hw->band->bandtype;
}

/* control chip clock to save power, enable dynamic clock or force fast clock */
static void brcms_b_clkctl_clk(struct brcms_hardware *wlc_hw, uint mode)
{
	if (PMUCTL_ENAB(wlc_hw->sih)) {
		/* new chips with PMU, CCS_FORCEHT will distribute the HT clock
		 * on backplane, but mac core will still run on ALP(not HT) when
		 * it enters powersave mode, which means the FCA bit may not be
		 * set. Should wakeup mac if driver wants it to run on HT.
		 */

		if (wlc_hw->clk) {
			if (mode == CLK_FAST) {
				OR_REG(&wlc_hw->regs->clk_ctl_st,
				       CCS_FORCEHT);

				udelay(64);

				SPINWAIT(((R_REG
					   (&wlc_hw->regs->
					    clk_ctl_st) & CCS_HTAVAIL) == 0),
					 PMU_MAX_TRANSITION_DLY);
				WARN_ON(!(R_REG
					  (&wlc_hw->regs->
					   clk_ctl_st) & CCS_HTAVAIL));
			} else {
				if ((wlc_hw->sih->pmurev == 0) &&
				    (R_REG
				     (&wlc_hw->regs->
				      clk_ctl_st) & (CCS_FORCEHT | CCS_HTAREQ)))
					SPINWAIT(((R_REG
						   (&wlc_hw->regs->
						    clk_ctl_st) & CCS_HTAVAIL)
						  == 0),
						 PMU_MAX_TRANSITION_DLY);
				AND_REG(&wlc_hw->regs->clk_ctl_st,
					~CCS_FORCEHT);
			}
		}
		wlc_hw->forcefastclk = (mode == CLK_FAST);
	} else {

		/* old chips w/o PMU, force HT through cc,
		 * then use FCA to verify mac is running fast clock
		 */

		wlc_hw->forcefastclk = ai_clkctl_cc(wlc_hw->sih, mode);

		/* check fast clock is available (if core is not in reset) */
		if (wlc_hw->forcefastclk && wlc_hw->clk)
			WARN_ON(!(ai_core_sflags(wlc_hw->sih, 0, 0) &
				  SISF_FCLKA));

		/*
		 * keep the ucode wake bit on if forcefastclk is on since we
		 * do not want ucode to put us back to slow clock when it dozes
		 * for PM mode. Code below matches the wake override bit with
		 * current forcefastclk state. Only setting bit in wake_override
		 * instead of waking ucode immediately since old code had this
		 * behavior. Older code set wlc->forcefastclk but only had the
		 * wake happen if the wakup_ucode work (protected by an up
		 * check) was executed just below.
		 */
		if (wlc_hw->forcefastclk)
			mboolset(wlc_hw->wake_override,
				 BRCMS_WAKE_OVERRIDE_FORCEFAST);
		else
			mboolclr(wlc_hw->wake_override,
				 BRCMS_WAKE_OVERRIDE_FORCEFAST);
	}
}

/* set initial host flags value */
static void
brcms_c_mhfdef(struct brcms_c_info *wlc, u16 *mhfs, u16 mhf2_init)
{
	struct brcms_hardware *wlc_hw = wlc->hw;

	memset(mhfs, 0, MHFMAX * sizeof(u16));

	mhfs[MHF2] |= mhf2_init;

	/* prohibit use of slowclock on multifunction boards */
	if (wlc_hw->boardflags & BFL_NOPLLDOWN)
		mhfs[MHF1] |= MHF1_FORCEFASTCLK;

	if (BRCMS_ISNPHY(wlc_hw->band) && NREV_LT(wlc_hw->band->phyrev, 2)) {
		mhfs[MHF2] |= MHF2_NPHY40MHZ_WAR;
		mhfs[MHF1] |= MHF1_IQSWAP_WAR;
	}
}

/* set or clear ucode host flag bits
 * it has an optimization for no-change write
 * it only writes through shared memory when the core has clock;
 * pre-CLK changes should use wlc_write_mhf to get around the optimization
 *
 *
 * bands values are: BRCM_BAND_AUTO <--- Current band only
 *                   BRCM_BAND_5G   <--- 5G band only
 *                   BRCM_BAND_2G   <--- 2G band only
 *                   BRCM_BAND_ALL  <--- All bands
 */
void
brcms_b_mhf(struct brcms_hardware *wlc_hw, u8 idx, u16 mask, u16 val,
	     int bands)
{
	u16 save;
	u16 addr[MHFMAX] = {
		M_HOST_FLAGS1, M_HOST_FLAGS2, M_HOST_FLAGS3, M_HOST_FLAGS4,
		M_HOST_FLAGS5
	};
	struct brcms_hw_band *band;

	if ((val & ~mask) || idx >= MHFMAX)
		return; /* error condition */

	switch (bands) {
		/* Current band only or all bands,
		 * then set the band to current band
		 */
	case BRCM_BAND_AUTO:
	case BRCM_BAND_ALL:
		band = wlc_hw->band;
		break;
	case BRCM_BAND_5G:
		band = wlc_hw->bandstate[BAND_5G_INDEX];
		break;
	case BRCM_BAND_2G:
		band = wlc_hw->bandstate[BAND_2G_INDEX];
		break;
	default:
		band = NULL;	/* error condition */
	}

	if (band) {
		save = band->mhfs[idx];
		band->mhfs[idx] = (band->mhfs[idx] & ~mask) | val;

		/* optimization: only write through if changed, and
		 * changed band is the current band
		 */
		if (wlc_hw->clk && (band->mhfs[idx] != save)
		    && (band == wlc_hw->band))
			brcms_b_write_shm(wlc_hw, addr[idx],
					   (u16) band->mhfs[idx]);
	}

	if (bands == BRCM_BAND_ALL) {
		wlc_hw->bandstate[0]->mhfs[idx] =
		    (wlc_hw->bandstate[0]->mhfs[idx] & ~mask) | val;
		wlc_hw->bandstate[1]->mhfs[idx] =
		    (wlc_hw->bandstate[1]->mhfs[idx] & ~mask) | val;
	}
}

static void brcms_c_write_mhf(struct brcms_hardware *wlc_hw, u16 *mhfs)
{
	u8 idx;
	u16 addr[] = {
		M_HOST_FLAGS1, M_HOST_FLAGS2, M_HOST_FLAGS3, M_HOST_FLAGS4,
		M_HOST_FLAGS5
	};

	for (idx = 0; idx < MHFMAX; idx++)
		brcms_b_write_shm(wlc_hw, addr[idx], mhfs[idx]);
}

/* set the maccontrol register to desired reset state and
 * initialize the sw cache of the register
 */
static void brcms_c_mctrl_reset(struct brcms_hardware *wlc_hw)
{
	/* IHR accesses are always enabled, PSM disabled, HPS off and WAKE on */
	wlc_hw->maccontrol = 0;
	wlc_hw->suspended_fifos = 0;
	wlc_hw->wake_override = 0;
	wlc_hw->mute_override = 0;
	brcms_b_mctrl(wlc_hw, ~0, MCTL_IHR_EN | MCTL_WAKE);
}

/* set or clear maccontrol bits */
void brcms_b_mctrl(struct brcms_hardware *wlc_hw, u32 mask, u32 val)
{
	u32 maccontrol;
	u32 new_maccontrol;

	if (val & ~mask)
		return; /* error condition */
	maccontrol = wlc_hw->maccontrol;
	new_maccontrol = (maccontrol & ~mask) | val;

	/* if the new maccontrol value is the same as the old, nothing to do */
	if (new_maccontrol == maccontrol)
		return;

	/* something changed, cache the new value */
	wlc_hw->maccontrol = new_maccontrol;

	/* write the new values with overrides applied */
	brcms_c_mctrl_write(wlc_hw);
}

/*
 * write the software state of maccontrol and
 * overrides to the maccontrol register
 */
static void brcms_c_mctrl_write(struct brcms_hardware *wlc_hw)
{
	u32 maccontrol = wlc_hw->maccontrol;

	/* OR in the wake bit if overridden */
	if (wlc_hw->wake_override)
		maccontrol |= MCTL_WAKE;

	/* set AP and INFRA bits for mute if needed */
	if (wlc_hw->mute_override) {
		maccontrol &= ~(MCTL_AP);
		maccontrol |= MCTL_INFRA;
	}

	W_REG(&wlc_hw->regs->maccontrol, maccontrol);
}

void brcms_c_ucode_wake_override_set(struct brcms_hardware *wlc_hw,
				 u32 override_bit)
{
	if (wlc_hw->wake_override || (wlc_hw->maccontrol & MCTL_WAKE)) {
		mboolset(wlc_hw->wake_override, override_bit);
		return;
	}

	mboolset(wlc_hw->wake_override, override_bit);

	brcms_c_mctrl_write(wlc_hw);
	brcms_b_wait_for_wake(wlc_hw);

	return;
}

void brcms_c_ucode_wake_override_clear(struct brcms_hardware *wlc_hw,
				   u32 override_bit)
{
	mboolclr(wlc_hw->wake_override, override_bit);

	if (wlc_hw->wake_override || (wlc_hw->maccontrol & MCTL_WAKE))
		return;

	brcms_c_mctrl_write(wlc_hw);

	return;
}

/* When driver needs ucode to stop beaconing, it has to make sure that
 * MCTL_AP is clear and MCTL_INFRA is set
 * Mode           MCTL_AP        MCTL_INFRA
 * AP                1              1
 * STA               0              1 <--- This will ensure no beacons
 * IBSS              0              0
 */
static void brcms_c_ucode_mute_override_set(struct brcms_hardware *wlc_hw)
{
	wlc_hw->mute_override = 1;

	/* if maccontrol already has AP == 0 and INFRA == 1 without this
	 * override, then there is no change to write
	 */
	if ((wlc_hw->maccontrol & (MCTL_AP | MCTL_INFRA)) == MCTL_INFRA)
		return;

	brcms_c_mctrl_write(wlc_hw);

	return;
}

/* Clear the override on AP and INFRA bits */
static void brcms_c_ucode_mute_override_clear(struct brcms_hardware *wlc_hw)
{
	if (wlc_hw->mute_override == 0)
		return;

	wlc_hw->mute_override = 0;

	/* if maccontrol already has AP == 0 and INFRA == 1 without this
	 * override, then there is no change to write
	 */
	if ((wlc_hw->maccontrol & (MCTL_AP | MCTL_INFRA)) == MCTL_INFRA)
		return;

	brcms_c_mctrl_write(wlc_hw);
}

/*
 * Write a MAC address to the given match reg offset in the RXE match engine.
 */
void
brcms_b_set_addrmatch(struct brcms_hardware *wlc_hw, int match_reg_offset,
		       const u8 *addr)
{
	struct d11regs *regs;
	u16 mac_l;
	u16 mac_m;
	u16 mac_h;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d: brcms_b_set_addrmatch\n",
		 wlc_hw->unit);

	regs = wlc_hw->regs;
	mac_l = addr[0] | (addr[1] << 8);
	mac_m = addr[2] | (addr[3] << 8);
	mac_h = addr[4] | (addr[5] << 8);

	/* enter the MAC addr into the RXE match registers */
	W_REG(&regs->rcm_ctl, RCM_INC_DATA | match_reg_offset);
	W_REG(&regs->rcm_mat_data, mac_l);
	W_REG(&regs->rcm_mat_data, mac_m);
	W_REG(&regs->rcm_mat_data, mac_h);

}

void
brcms_b_write_template_ram(struct brcms_hardware *wlc_hw, int offset, int len,
			    void *buf)
{
	struct d11regs *regs;
	u32 word;
	bool be_bit;
	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	regs = wlc_hw->regs;
	W_REG(&regs->tplatewrptr, offset);

	/* if MCTL_BIGEND bit set in mac control register,
	 * the chip swaps data in fifo, as well as data in
	 * template ram
	 */
	be_bit = (R_REG(&regs->maccontrol) & MCTL_BIGEND) != 0;

	while (len > 0) {
		memcpy(&word, buf, sizeof(u32));

		if (be_bit)
			word = cpu_to_be32(word);
		else
			word = cpu_to_le32(word);

		W_REG(&regs->tplatewrdata, word);

		buf = (u8 *) buf + sizeof(u32);
		len -= sizeof(u32);
	}
}

void brcms_b_set_cwmin(struct brcms_hardware *wlc_hw, u16 newmin)
{
	wlc_hw->band->CWmin = newmin;

	W_REG(&wlc_hw->regs->objaddr, OBJADDR_SCR_SEL | S_DOT11_CWMIN);
	(void)R_REG(&wlc_hw->regs->objaddr);
	W_REG(&wlc_hw->regs->objdata, newmin);
}

void brcms_b_set_cwmax(struct brcms_hardware *wlc_hw, u16 newmax)
{
	wlc_hw->band->CWmax = newmax;

	W_REG(&wlc_hw->regs->objaddr, OBJADDR_SCR_SEL | S_DOT11_CWMAX);
	(void)R_REG(&wlc_hw->regs->objaddr);
	W_REG(&wlc_hw->regs->objdata, newmax);
}

void brcms_b_bw_set(struct brcms_hardware *wlc_hw, u16 bw)
{
	bool fastclk;

	/* request FAST clock if not on */
	fastclk = wlc_hw->forcefastclk;
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, CLK_FAST);

	wlc_phy_bw_state_set(wlc_hw->band->pi, bw);

	brcms_b_phy_reset(wlc_hw);
	wlc_phy_init(wlc_hw->band->pi, wlc_phy_chanspec_get(wlc_hw->band->pi));

	/* restore the clk */
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, CLK_DYNAMIC);
}

static void
brcms_c_write_hw_bcntemplate0(struct brcms_hardware *wlc_hw, u16 bcn[],
			      int len)
{
	struct d11regs *regs = wlc_hw->regs;

	brcms_b_write_template_ram(wlc_hw, T_BCN0_TPL_BASE, (len + 3) & ~3,
				    bcn);
	/* write beacon length to SCR */
	brcms_b_write_shm(wlc_hw, M_BCN0_FRM_BYTESZ, (u16) len);
	/* mark beacon0 valid */
	OR_REG(&regs->maccommand, MCMD_BCN0VLD);
}

static void
brcms_c_write_hw_bcntemplate1(struct brcms_hardware *wlc_hw, u16 bcn[],
			      int len)
{
	struct d11regs *regs = wlc_hw->regs;

	brcms_b_write_template_ram(wlc_hw, T_BCN1_TPL_BASE, (len + 3) & ~3,
				    bcn);
	/* write beacon length to SCR */
	brcms_b_write_shm(wlc_hw, M_BCN1_FRM_BYTESZ, (u16) len);
	/* mark beacon1 valid */
	OR_REG(&regs->maccommand, MCMD_BCN1VLD);
}

static void brcms_b_upd_synthpu(struct brcms_hardware *wlc_hw)
{
	u16 v;
	struct brcms_c_info *wlc = wlc_hw->wlc;
	/* update SYNTHPU_DLY */

	if (BRCMS_ISLCNPHY(wlc->band))
		v = SYNTHPU_DLY_LPPHY_US;
	else if (BRCMS_ISNPHY(wlc->band) && (NREV_GE(wlc->band->phyrev, 3)))
		v = SYNTHPU_DLY_NPHY_US;
	else
		v = SYNTHPU_DLY_BPHY_US;

	brcms_b_write_shm(wlc_hw, M_SYNTHPU_DLY, v);
}

/* band-specific init */
static void brcms_b_bsinit(struct brcms_c_info *wlc, u16 chanspec)
{
	struct brcms_hardware *wlc_hw = wlc->hw;

	BCMMSG(wlc->wiphy, "wl%d: bandunit %d\n", wlc_hw->unit,
		wlc_hw->band->bandunit);

	brcms_c_ucode_bsinit(wlc_hw);

	wlc_phy_init(wlc_hw->band->pi, chanspec);

	brcms_c_ucode_txant_set(wlc_hw);

	/*
	 * cwmin is band-specific, update hardware
	 * with value for current band
	 */
	brcms_b_set_cwmin(wlc_hw, wlc_hw->band->CWmin);
	brcms_b_set_cwmax(wlc_hw, wlc_hw->band->CWmax);

	brcms_b_update_slot_timing(wlc_hw,
				    BAND_5G(wlc_hw->band->
					    bandtype) ? true : wlc_hw->
				    shortslot);

	/* write phytype and phyvers */
	brcms_b_write_shm(wlc_hw, M_PHYTYPE, (u16) wlc_hw->band->phytype);
	brcms_b_write_shm(wlc_hw, M_PHYVER, (u16) wlc_hw->band->phyrev);

	/*
	 * initialize the txphyctl1 rate table since
	 * shmem is shared between bands
	 */
	brcms_upd_ofdm_pctl1_table(wlc_hw);

	brcms_b_upd_synthpu(wlc_hw);
}

static void brcms_b_core_phy_clk(struct brcms_hardware *wlc_hw, bool clk)
{
	BCMMSG(wlc_hw->wlc->wiphy, "wl%d: clk %d\n", wlc_hw->unit, clk);

	wlc_hw->phyclk = clk;

	if (OFF == clk) {	/* clear gmode bit, put phy into reset */

		ai_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_FGC | SICF_GMODE),
			       (SICF_PRST | SICF_FGC));
		udelay(1);
		ai_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_FGC), SICF_PRST);
		udelay(1);

	} else {		/* take phy out of reset */

		ai_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_FGC), SICF_FGC);
		udelay(1);
		ai_core_cflags(wlc_hw->sih, (SICF_FGC), 0);
		udelay(1);

	}
}

/* Perform a soft reset of the PHY PLL */
void brcms_b_core_phypll_reset(struct brcms_hardware *wlc_hw)
{
	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	ai_corereg(wlc_hw->sih, SI_CC_IDX,
		   offsetof(struct chipcregs, chipcontrol_addr), ~0, 0);
	udelay(1);
	ai_corereg(wlc_hw->sih, SI_CC_IDX,
		   offsetof(struct chipcregs, chipcontrol_data), 0x4, 0);
	udelay(1);
	ai_corereg(wlc_hw->sih, SI_CC_IDX,
		   offsetof(struct chipcregs, chipcontrol_data), 0x4, 4);
	udelay(1);
	ai_corereg(wlc_hw->sih, SI_CC_IDX,
		   offsetof(struct chipcregs, chipcontrol_data), 0x4, 0);
	udelay(1);
}

/* light way to turn on phy clock without reset for NPHY only
 *  refer to brcms_b_core_phy_clk for full version
 */
void brcms_b_phyclk_fgc(struct brcms_hardware *wlc_hw, bool clk)
{
	/* support(necessary for NPHY and HYPHY) only */
	if (!BRCMS_ISNPHY(wlc_hw->band))
		return;

	if (ON == clk)
		ai_core_cflags(wlc_hw->sih, SICF_FGC, SICF_FGC);
	else
		ai_core_cflags(wlc_hw->sih, SICF_FGC, 0);

}

void brcms_b_macphyclk_set(struct brcms_hardware *wlc_hw, bool clk)
{
	if (ON == clk)
		ai_core_cflags(wlc_hw->sih, SICF_MPCLKE, SICF_MPCLKE);
	else
		ai_core_cflags(wlc_hw->sih, SICF_MPCLKE, 0);
}

void brcms_b_phy_reset(struct brcms_hardware *wlc_hw)
{
	struct brcms_phy_pub *pih = wlc_hw->band->pi;
	u32 phy_bw_clkbits;
	bool phy_in_reset = false;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	if (pih == NULL)
		return;

	phy_bw_clkbits = wlc_phy_clk_bwbits(wlc_hw->band->pi);

	/* Specific reset sequence required for NPHY rev 3 and 4 */
	if (BRCMS_ISNPHY(wlc_hw->band) && NREV_GE(wlc_hw->band->phyrev, 3) &&
	    NREV_LE(wlc_hw->band->phyrev, 4)) {
		/* Set the PHY bandwidth */
		ai_core_cflags(wlc_hw->sih, SICF_BWMASK, phy_bw_clkbits);

		udelay(1);

		/* Perform a soft reset of the PHY PLL */
		brcms_b_core_phypll_reset(wlc_hw);

		/* reset the PHY */
		ai_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_PCLKE),
			       (SICF_PRST | SICF_PCLKE));
		phy_in_reset = true;
	} else {
		ai_core_cflags(wlc_hw->sih,
			       (SICF_PRST | SICF_PCLKE | SICF_BWMASK),
			       (SICF_PRST | SICF_PCLKE | phy_bw_clkbits));
	}

	udelay(2);
	brcms_b_core_phy_clk(wlc_hw, ON);

	if (pih)
		wlc_phy_anacore(pih, ON);
}

/* switch to and initialize new band */
static void brcms_b_setband(struct brcms_hardware *wlc_hw, uint bandunit,
			    u16 chanspec) {
	struct brcms_c_info *wlc = wlc_hw->wlc;
	u32 macintmask;

	/* Enable the d11 core before accessing it */
	if (!ai_iscoreup(wlc_hw->sih)) {
		ai_core_reset(wlc_hw->sih, 0, 0);
		brcms_c_mctrl_reset(wlc_hw);
	}

	macintmask = brcms_c_setband_inact(wlc, bandunit);

	if (!wlc_hw->up)
		return;

	brcms_b_core_phy_clk(wlc_hw, ON);

	/* band-specific initializations */
	brcms_b_bsinit(wlc, chanspec);

	/*
	 * If there are any pending software interrupt bits,
	 * then replace these with a harmless nonzero value
	 * so brcms_c_dpc() will re-enable interrupts when done.
	 */
	if (wlc->macintstatus)
		wlc->macintstatus = MI_DMAINT;

	/* restore macintmask */
	brcms_intrsrestore(wlc->wl, macintmask);

	/* ucode should still be suspended.. */
	WARN_ON((R_REG(&wlc_hw->regs->maccontrol) & MCTL_EN_MAC) != 0);
}

/* low-level band switch utility routine */
void brcms_c_setxband(struct brcms_hardware *wlc_hw, uint bandunit)
{
	BCMMSG(wlc_hw->wlc->wiphy, "wl%d: bandunit %d\n", wlc_hw->unit,
		bandunit);

	wlc_hw->band = wlc_hw->bandstate[bandunit];

	/*
	 * BMAC_NOTE:
	 *   until we eliminate need for wlc->band refs in low level code
	 */
	wlc_hw->wlc->band = wlc_hw->wlc->bandstate[bandunit];

	/* set gmode core flag */
	if (wlc_hw->sbclk && !wlc_hw->noreset)
		ai_core_cflags(wlc_hw->sih, SICF_GMODE,
			       ((bandunit == 0) ? SICF_GMODE : 0));
}

static bool brcms_c_isgoodchip(struct brcms_hardware *wlc_hw)
{

	/* reject unsupported corerev */
	if (!VALID_COREREV(wlc_hw->corerev)) {
		wiphy_err(wlc_hw->wlc->wiphy, "unsupported core rev %d\n",
			  wlc_hw->corerev);
		return false;
	}

	return true;
}

/* Validate some board info parameters */
static bool brcms_c_validboardtype(struct brcms_hardware *wlc_hw)
{
	uint boardrev = wlc_hw->boardrev;

	/* 4 bits each for board type, major, minor, and tiny version */
	uint brt = (boardrev & 0xf000) >> 12;
	uint b0 = (boardrev & 0xf00) >> 8;
	uint b1 = (boardrev & 0xf0) >> 4;
	uint b2 = boardrev & 0xf;

	/* voards from other vendors are always considered valid */
	if (wlc_hw->sih->boardvendor != PCI_VENDOR_ID_BROADCOM)
		return true;

	/* do some boardrev sanity checks when boardvendor is Broadcom */
	if (boardrev == 0)
		return false;

	if (boardrev <= 0xff)
		return true;

	if ((brt > 2) || (brt == 0) || (b0 > 9) || (b0 == 0) || (b1 > 9)
		|| (b2 > 9))
		return false;

	return true;
}

static char *brcms_c_get_macaddr(struct brcms_hardware *wlc_hw)
{
	const char *varname = "macaddr";
	char *macaddr;

	/* If macaddr exists, use it (Sromrev4, CIS, ...). */
	macaddr = getvar(wlc_hw->vars, varname);
	if (macaddr != NULL)
		return macaddr;

	if (NBANDS_HW(wlc_hw) > 1)
		varname = "et1macaddr";
	else
		varname = "il0macaddr";

	macaddr = getvar(wlc_hw->vars, varname);
	if (macaddr == NULL)
		wiphy_err(wlc_hw->wlc->wiphy, "wl%d: wlc_get_macaddr: macaddr "
			  "getvar(%s) not found\n", wlc_hw->unit, varname);

	return macaddr;
}

/*
 * Return true if radio is disabled, otherwise false.
 * hw radio disable signal is an external pin, users activate it asynchronously
 * this function could be called when driver is down and w/o clock
 * it operates on different registers depending on corerev and boardflag.
 */
bool brcms_b_radio_read_hwdisabled(struct brcms_hardware *wlc_hw)
{
	bool v, clk, xtal;
	u32 resetbits = 0, flags = 0;

	xtal = wlc_hw->sbclk;
	if (!xtal)
		brcms_b_xtal(wlc_hw, ON);

	/* may need to take core out of reset first */
	clk = wlc_hw->clk;
	if (!clk) {
		/*
		 * mac no longer enables phyclk automatically when driver
		 * accesses phyreg throughput mac. This can be skipped since
		 * only mac reg is accessed below
		 */
		flags |= SICF_PCLKE;

		/*
		 * AI chip doesn't restore bar0win2 on
		 * hibernation/resume, need sw fixup
		 */
		if ((wlc_hw->sih->chip == BCM43224_CHIP_ID) ||
		    (wlc_hw->sih->chip == BCM43225_CHIP_ID))
			wlc_hw->regs = (struct d11regs *)
					ai_setcore(wlc_hw->sih, D11_CORE_ID, 0);
		ai_core_reset(wlc_hw->sih, flags, resetbits);
		brcms_c_mctrl_reset(wlc_hw);
	}

	v = ((R_REG(&wlc_hw->regs->phydebug) & PDBG_RFD) != 0);

	/* put core back into reset */
	if (!clk)
		ai_core_disable(wlc_hw->sih, 0);

	if (!xtal)
		brcms_b_xtal(wlc_hw, OFF);

	return v;
}

static bool wlc_dma_rxreset(struct brcms_hardware *wlc_hw, uint fifo)
{
	struct dma_pub *di = wlc_hw->di[fifo];
	return dma_rxreset(di);
}

/* d11 core reset
 *   ensure fask clock during reset
 *   reset dma
 *   reset d11(out of reset)
 *   reset phy(out of reset)
 *   clear software macintstatus for fresh new start
 * one testing hack wlc_hw->noreset will bypass the d11/phy reset
 */
void brcms_b_corereset(struct brcms_hardware *wlc_hw, u32 flags)
{
	struct d11regs *regs;
	uint i;
	bool fastclk;
	u32 resetbits = 0;

	if (flags == BRCMS_USE_COREFLAGS)
		flags = (wlc_hw->band->pi ? wlc_hw->band->core_flags : 0);

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	regs = wlc_hw->regs;

	/* request FAST clock if not on  */
	fastclk = wlc_hw->forcefastclk;
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, CLK_FAST);

	/* reset the dma engines except first time thru */
	if (ai_iscoreup(wlc_hw->sih)) {
		for (i = 0; i < NFIFO; i++)
			if ((wlc_hw->di[i]) && (!dma_txreset(wlc_hw->di[i])))
				wiphy_err(wlc_hw->wlc->wiphy, "wl%d: %s: "
					  "dma_txreset[%d]: cannot stop dma\n",
					   wlc_hw->unit, __func__, i);

		if ((wlc_hw->di[RX_FIFO])
		    && (!wlc_dma_rxreset(wlc_hw, RX_FIFO)))
			wiphy_err(wlc_hw->wlc->wiphy, "wl%d: %s: dma_rxreset"
				  "[%d]: cannot stop dma\n",
				  wlc_hw->unit, __func__, RX_FIFO);
	}
	/* if noreset, just stop the psm and return */
	if (wlc_hw->noreset) {
		wlc_hw->wlc->macintstatus = 0;	/* skip wl_dpc after down */
		brcms_b_mctrl(wlc_hw, MCTL_PSM_RUN | MCTL_EN_MAC, 0);
		return;
	}

	/*
	 * mac no longer enables phyclk automatically when driver accesses
	 * phyreg throughput mac, AND phy_reset is skipped at early stage when
	 * band->pi is invalid. need to enable PHY CLK
	 */
	flags |= SICF_PCLKE;

	/*
	 * reset the core
	 * In chips with PMU, the fastclk request goes through d11 core
	 * reg 0x1e0, which is cleared by the core_reset. have to re-request it.
	 *
	 * This adds some delay and we can optimize it by also requesting
	 * fastclk through chipcommon during this period if necessary. But
	 * that has to work coordinate with other driver like mips/arm since
	 * they may touch chipcommon as well.
	 */
	wlc_hw->clk = false;
	ai_core_reset(wlc_hw->sih, flags, resetbits);
	wlc_hw->clk = true;
	if (wlc_hw->band && wlc_hw->band->pi)
		wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, true);

	brcms_c_mctrl_reset(wlc_hw);

	if (PMUCTL_ENAB(wlc_hw->sih))
		brcms_b_clkctl_clk(wlc_hw, CLK_FAST);

	brcms_b_phy_reset(wlc_hw);

	/* turn on PHY_PLL */
	brcms_b_core_phypll_ctl(wlc_hw, true);

	/* clear sw intstatus */
	wlc_hw->wlc->macintstatus = 0;

	/* restore the clk setting */
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, CLK_DYNAMIC);
}

/* txfifo sizes needs to be modified(increased) since the newer cores
 * have more memory.
 */
static void brcms_b_corerev_fifofixup(struct brcms_hardware *wlc_hw)
{
	struct d11regs *regs = wlc_hw->regs;
	u16 fifo_nu;
	u16 txfifo_startblk = TXFIFO_START_BLK, txfifo_endblk;
	u16 txfifo_def, txfifo_def1;
	u16 txfifo_cmd;

	/* tx fifos start at TXFIFO_START_BLK from the Base address */
	txfifo_startblk = TXFIFO_START_BLK;

	/* sequence of operations:  reset fifo, set fifo size, reset fifo */
	for (fifo_nu = 0; fifo_nu < NFIFO; fifo_nu++) {

		txfifo_endblk = txfifo_startblk + wlc_hw->xmtfifo_sz[fifo_nu];
		txfifo_def = (txfifo_startblk & 0xff) |
		    (((txfifo_endblk - 1) & 0xff) << TXFIFO_FIFOTOP_SHIFT);
		txfifo_def1 = ((txfifo_startblk >> 8) & 0x1) |
		    ((((txfifo_endblk -
			1) >> 8) & 0x1) << TXFIFO_FIFOTOP_SHIFT);
		txfifo_cmd =
		    TXFIFOCMD_RESET_MASK | (fifo_nu << TXFIFOCMD_FIFOSEL_SHIFT);

		W_REG(&regs->xmtfifocmd, txfifo_cmd);
		W_REG(&regs->xmtfifodef, txfifo_def);
		W_REG(&regs->xmtfifodef1, txfifo_def1);

		W_REG(&regs->xmtfifocmd, txfifo_cmd);

		txfifo_startblk += wlc_hw->xmtfifo_sz[fifo_nu];
	}
	/*
	 * need to propagate to shm location to be in sync since ucode/hw won't
	 * do this
	 */
	brcms_b_write_shm(wlc_hw, M_FIFOSIZE0,
			   wlc_hw->xmtfifo_sz[TX_AC_BE_FIFO]);
	brcms_b_write_shm(wlc_hw, M_FIFOSIZE1,
			   wlc_hw->xmtfifo_sz[TX_AC_VI_FIFO]);
	brcms_b_write_shm(wlc_hw, M_FIFOSIZE2,
			   ((wlc_hw->xmtfifo_sz[TX_AC_VO_FIFO] << 8) | wlc_hw->
			    xmtfifo_sz[TX_AC_BK_FIFO]));
	brcms_b_write_shm(wlc_hw, M_FIFOSIZE3,
			   ((wlc_hw->xmtfifo_sz[TX_ATIM_FIFO] << 8) | wlc_hw->
			    xmtfifo_sz[TX_BCMC_FIFO]));
}

/* This function is used for changing the tsf frac register
 * If spur avoidance mode is off, the mac freq will be 80/120/160Mhz
 * If spur avoidance mode is on1, the mac freq will be 82/123/164Mhz
 * If spur avoidance mode is on2, the mac freq will be 84/126/168Mhz
 * HTPHY Formula is 2^26/freq(MHz) e.g.
 * For spuron2 - 126MHz -> 2^26/126 = 532610.0
 *  - 532610 = 0x82082 => tsf_clk_frac_h = 0x8, tsf_clk_frac_l = 0x2082
 * For spuron: 123MHz -> 2^26/123    = 545600.5
 *  - 545601 = 0x85341 => tsf_clk_frac_h = 0x8, tsf_clk_frac_l = 0x5341
 * For spur off: 120MHz -> 2^26/120    = 559240.5
 *  - 559241 = 0x88889 => tsf_clk_frac_h = 0x8, tsf_clk_frac_l = 0x8889
 */

void brcms_b_switch_macfreq(struct brcms_hardware *wlc_hw, u8 spurmode)
{
	struct d11regs *regs;
	regs = wlc_hw->regs;

	if ((wlc_hw->sih->chip == BCM43224_CHIP_ID) ||
	    (wlc_hw->sih->chip == BCM43225_CHIP_ID)) {
		if (spurmode == WL_SPURAVOID_ON2) {	/* 126Mhz */
			W_REG(&regs->tsf_clk_frac_l, 0x2082);
			W_REG(&regs->tsf_clk_frac_h, 0x8);
		} else if (spurmode == WL_SPURAVOID_ON1) {	/* 123Mhz */
			W_REG(&regs->tsf_clk_frac_l, 0x5341);
			W_REG(&regs->tsf_clk_frac_h, 0x8);
		} else {	/* 120Mhz */
			W_REG(&regs->tsf_clk_frac_l, 0x8889);
			W_REG(&regs->tsf_clk_frac_h, 0x8);
		}
	} else if (BRCMS_ISLCNPHY(wlc_hw->band)) {
		if (spurmode == WL_SPURAVOID_ON1) {	/* 82Mhz */
			W_REG(&regs->tsf_clk_frac_l, 0x7CE0);
			W_REG(&regs->tsf_clk_frac_h, 0xC);
		} else {	/* 80Mhz */
			W_REG(&regs->tsf_clk_frac_l, 0xCCCD);
			W_REG(&regs->tsf_clk_frac_h, 0xC);
		}
	}
}

/* Initialize GPIOs that are controlled by D11 core */
static void brcms_c_gpio_init(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	struct d11regs *regs;
	u32 gc, gm;

	regs = wlc_hw->regs;

	/* use GPIO select 0 to get all gpio signals from the gpio out reg */
	brcms_b_mctrl(wlc_hw, MCTL_GPOUT_SEL_MASK, 0);

	/*
	 * Common GPIO setup:
	 *      G0 = LED 0 = WLAN Activity
	 *      G1 = LED 1 = WLAN 2.4 GHz Radio State
	 *      G2 = LED 2 = WLAN 5 GHz Radio State
	 *      G4 = radio disable input (HI enabled, LO disabled)
	 */

	gc = gm = 0;

	/* Allocate GPIOs for mimo antenna diversity feature */
	if (wlc_hw->antsel_type == ANTSEL_2x3) {
		/* Enable antenna diversity, use 2x3 mode */
		brcms_b_mhf(wlc_hw, MHF3, MHF3_ANTSEL_EN,
			     MHF3_ANTSEL_EN, BRCM_BAND_ALL);
		brcms_b_mhf(wlc_hw, MHF3, MHF3_ANTSEL_MODE,
			     MHF3_ANTSEL_MODE, BRCM_BAND_ALL);

		/* init superswitch control */
		wlc_phy_antsel_init(wlc_hw->band->pi, false);

	} else if (wlc_hw->antsel_type == ANTSEL_2x4) {
		gm |= gc |= (BOARD_GPIO_12 | BOARD_GPIO_13);
		/*
		 * The board itself is powered by these GPIOs
		 * (when not sending pattern) so set them high
		 */
		OR_REG(&regs->psm_gpio_oe,
		       (BOARD_GPIO_12 | BOARD_GPIO_13));
		OR_REG(&regs->psm_gpio_out,
		       (BOARD_GPIO_12 | BOARD_GPIO_13));

		/* Enable antenna diversity, use 2x4 mode */
		brcms_b_mhf(wlc_hw, MHF3, MHF3_ANTSEL_EN,
			     MHF3_ANTSEL_EN, BRCM_BAND_ALL);
		brcms_b_mhf(wlc_hw, MHF3, MHF3_ANTSEL_MODE, 0,
			     BRCM_BAND_ALL);

		/* Configure the desired clock to be 4Mhz */
		brcms_b_write_shm(wlc_hw, M_ANTSEL_CLKDIV,
				   ANTSEL_CLKDIV_4MHZ);
	}

	/*
	 * gpio 9 controls the PA. ucode is responsible
	 * for wiggling out and oe
	 */
	if (wlc_hw->boardflags & BFL_PACTRL)
		gm |= gc |= BOARD_GPIO_PACTRL;

	/* apply to gpiocontrol register */
	ai_gpiocontrol(wlc_hw->sih, gm, gc, GPIO_DRV_PRIORITY);
}

static void brcms_ucode_download(struct brcms_hardware *wlc_hw)
{
	struct brcms_c_info *wlc;
	wlc = wlc_hw->wlc;

	if (wlc_hw->ucode_loaded)
		return;

	if (D11REV_IS(wlc_hw->corerev, 23)) {
		if (BRCMS_ISNPHY(wlc_hw->band)) {
			brcms_ucode_write(wlc_hw, bcm43xx_16_mimo,
					bcm43xx_16_mimosz);
			wlc_hw->ucode_loaded = true;
		} else
			wiphy_err(wlc->wiphy, "%s: wl%d: unsupported phy in "
				  "corerev %d\n",
				  __func__, wlc_hw->unit, wlc_hw->corerev);
	} else if (D11REV_IS(wlc_hw->corerev, 24)) {
		if (BRCMS_ISLCNPHY(wlc_hw->band)) {
			brcms_ucode_write(wlc_hw, bcm43xx_24_lcn,
					bcm43xx_24_lcnsz);
			wlc_hw->ucode_loaded = true;
		} else {
			wiphy_err(wlc->wiphy, "%s: wl%d: unsupported phy in "
				  "corerev %d\n",
				  __func__, wlc_hw->unit, wlc_hw->corerev);
		}
	}
}

static void brcms_ucode_write(struct brcms_hardware *wlc_hw, const u32 ucode[],
			      const uint nbytes) {
	struct d11regs *regs = wlc_hw->regs;
	uint i;
	uint count;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	count = (nbytes / sizeof(u32));

	W_REG(&regs->objaddr, (OBJADDR_AUTO_INC | OBJADDR_UCM_SEL));
	(void)R_REG(&regs->objaddr);
	for (i = 0; i < count; i++)
		W_REG(&regs->objdata, le32_to_cpu(ucode[i]));

}

static void brcms_c_write_inits(struct brcms_hardware *wlc_hw,
			    const struct d11init *inits)
{
	int i;
	u8 *base;
	u8 *addr;
	u16 size;
	u32 value;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	base = (u8 *)wlc_hw->regs;

	for (i = 0; inits[i].addr != 0xffff; i++) {
		size = le16_to_cpu(inits[i].size);
		addr = base + le16_to_cpu(inits[i].addr);
		value = le32_to_cpu(inits[i].value);
		if (size == 2)
			W_REG((u16 *)addr, value);
		else if (size == 4)
			W_REG((u32 *)addr, value);
		else
			break;
	}
}

static void brcms_c_ucode_txant_set(struct brcms_hardware *wlc_hw)
{
	u16 phyctl;
	u16 phytxant = wlc_hw->bmac_phytxant;
	u16 mask = PHY_TXC_ANT_MASK;

	/* set the Probe Response frame phy control word */
	phyctl = brcms_b_read_shm(wlc_hw, M_CTXPRS_BLK + C_CTX_PCTLWD_POS);
	phyctl = (phyctl & ~mask) | phytxant;
	brcms_b_write_shm(wlc_hw, M_CTXPRS_BLK + C_CTX_PCTLWD_POS, phyctl);

	/* set the Response (ACK/CTS) frame phy control word */
	phyctl = brcms_b_read_shm(wlc_hw, M_RSP_PCTLWD);
	phyctl = (phyctl & ~mask) | phytxant;
	brcms_b_write_shm(wlc_hw, M_RSP_PCTLWD, phyctl);
}

void brcms_b_txant_set(struct brcms_hardware *wlc_hw, u16 phytxant)
{
	/* update sw state */
	wlc_hw->bmac_phytxant = phytxant;

	/* push to ucode if up */
	if (!wlc_hw->up)
		return;
	brcms_c_ucode_txant_set(wlc_hw);

}

u16 brcms_b_get_txant(struct brcms_hardware *wlc_hw)
{
	return (u16) wlc_hw->wlc->stf->txant;
}

void brcms_b_antsel_type_set(struct brcms_hardware *wlc_hw, u8 antsel_type)
{
	wlc_hw->antsel_type = antsel_type;

	/* Update the antsel type for phy module to use */
	wlc_phy_antsel_type_set(wlc_hw->band->pi, antsel_type);
}

void brcms_b_fifoerrors(struct brcms_hardware *wlc_hw)
{
	bool fatal = false;
	uint unit;
	uint intstatus, idx;
	struct d11regs *regs = wlc_hw->regs;
	struct wiphy *wiphy = wlc_hw->wlc->wiphy;

	unit = wlc_hw->unit;

	for (idx = 0; idx < NFIFO; idx++) {
		/* read intstatus register and ignore any non-error bits */
		intstatus =
		    R_REG(&regs->intctrlregs[idx].intstatus) & I_ERRORS;
		if (!intstatus)
			continue;

		BCMMSG(wlc_hw->wlc->wiphy, "wl%d: intstatus%d 0x%x\n",
			unit, idx, intstatus);

		if (intstatus & I_RO) {
			wiphy_err(wiphy, "wl%d: fifo %d: receive fifo "
				  "overflow\n", unit, idx);
			fatal = true;
		}

		if (intstatus & I_PC) {
			wiphy_err(wiphy, "wl%d: fifo %d: descriptor error\n",
				 unit, idx);
			fatal = true;
		}

		if (intstatus & I_PD) {
			wiphy_err(wiphy, "wl%d: fifo %d: data error\n", unit,
				  idx);
			fatal = true;
		}

		if (intstatus & I_DE) {
			wiphy_err(wiphy, "wl%d: fifo %d: descriptor protocol "
				  "error\n", unit, idx);
			fatal = true;
		}

		if (intstatus & I_RU)
			wiphy_err(wiphy, "wl%d: fifo %d: receive descriptor "
				  "underflow\n", idx, unit);

		if (intstatus & I_XU) {
			wiphy_err(wiphy, "wl%d: fifo %d: transmit fifo "
				  "underflow\n", idx, unit);
			fatal = true;
		}

		if (fatal) {
			brcms_c_fatal_error(wlc_hw->wlc);	/* big hammer */
			break;
		} else
			W_REG(&regs->intctrlregs[idx].intstatus,
			      intstatus);
	}
}

void brcms_c_intrson(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	wlc->macintmask = wlc->defmacintmask;
	W_REG(&wlc_hw->regs->macintmask, wlc->macintmask);
}

/*
 * callback for siutils.c, which has only wlc handler, no wl they both check
 * up, not only because there is no need to off/restore d11 interrupt but also
 * because per-port code may require sync with valid interrupt.
 */
static u32 brcms_c_wlintrsoff(struct brcms_c_info *wlc)
{
	if (!wlc->hw->up)
		return 0;

	return brcms_intrsoff(wlc->wl);
}

static void brcms_c_wlintrsrestore(struct brcms_c_info *wlc, u32 macintmask)
{
	if (!wlc->hw->up)
		return;

	brcms_intrsrestore(wlc->wl, macintmask);
}

u32 brcms_c_intrsoff(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	u32 macintmask;

	if (!wlc_hw->clk)
		return 0;

	macintmask = wlc->macintmask;	/* isr can still happen */

	W_REG(&wlc_hw->regs->macintmask, 0);
	(void)R_REG(&wlc_hw->regs->macintmask);	/* sync readback */
	udelay(1);		/* ensure int line is no longer driven */
	wlc->macintmask = 0;

	/* return previous macintmask; resolve race between us and our isr */
	return wlc->macintstatus ? 0 : macintmask;
}

void brcms_c_intrsrestore(struct brcms_c_info *wlc, u32 macintmask)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	if (!wlc_hw->clk)
		return;

	wlc->macintmask = macintmask;
	W_REG(&wlc_hw->regs->macintmask, wlc->macintmask);
}

static void brcms_b_tx_fifo_suspend(struct brcms_hardware *wlc_hw,
				    uint tx_fifo)
{
	u8 fifo = 1 << tx_fifo;

	/* Two clients of this code, 11h Quiet period and scanning. */

	/* only suspend if not already suspended */
	if ((wlc_hw->suspended_fifos & fifo) == fifo)
		return;

	/* force the core awake only if not already */
	if (wlc_hw->suspended_fifos == 0)
		brcms_c_ucode_wake_override_set(wlc_hw,
						BRCMS_WAKE_OVERRIDE_TXFIFO);

	wlc_hw->suspended_fifos |= fifo;

	if (wlc_hw->di[tx_fifo]) {
		/*
		 * Suspending AMPDU transmissions in the middle can cause
		 * underflow which may result in mismatch between ucode and
		 * driver so suspend the mac before suspending the FIFO
		 */
		if (BRCMS_PHY_11N_CAP(wlc_hw->band))
			brcms_c_suspend_mac_and_wait(wlc_hw->wlc);

		dma_txsuspend(wlc_hw->di[tx_fifo]);

		if (BRCMS_PHY_11N_CAP(wlc_hw->band))
			brcms_c_enable_mac(wlc_hw->wlc);
	}
}

static void brcms_b_mute(struct brcms_hardware *wlc_hw, bool on, u32 flags)
{
	u8 null_ether_addr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

	if (on) {
		/* suspend tx fifos */
		brcms_b_tx_fifo_suspend(wlc_hw, TX_DATA_FIFO);
		brcms_b_tx_fifo_suspend(wlc_hw, TX_CTL_FIFO);
		brcms_b_tx_fifo_suspend(wlc_hw, TX_AC_BK_FIFO);
		brcms_b_tx_fifo_suspend(wlc_hw, TX_AC_VI_FIFO);

		/* zero the address match register so we do not send ACKs */
		brcms_b_set_addrmatch(wlc_hw, RCM_MAC_OFFSET,
				       null_ether_addr);
	} else {
		/* resume tx fifos */
		if (!wlc_hw->wlc->tx_suspended)
			brcms_b_tx_fifo_resume(wlc_hw, TX_DATA_FIFO);

		brcms_b_tx_fifo_resume(wlc_hw, TX_CTL_FIFO);
		brcms_b_tx_fifo_resume(wlc_hw, TX_AC_BK_FIFO);
		brcms_b_tx_fifo_resume(wlc_hw, TX_AC_VI_FIFO);

		/* Restore address */
		brcms_b_set_addrmatch(wlc_hw, RCM_MAC_OFFSET,
				       wlc_hw->etheraddr);
	}

	wlc_phy_mute_upd(wlc_hw->band->pi, on, flags);

	if (on)
		brcms_c_ucode_mute_override_set(wlc_hw);
	else
		brcms_c_ucode_mute_override_clear(wlc_hw);
}

/* brcms_b_tx_fifo_suspended:
 * Check the MAC's tx suspend status for a tx fifo.
 *
 * When the MAC acknowledges a tx suspend, it indicates that no more
 * packets will be transmitted out the radio. This is independent of
 * DMA channel suspension---the DMA may have finished suspending, or may still
 * be pulling data into a tx fifo, by the time the MAC acks the suspend
 * request.
 */
static bool brcms_b_tx_fifo_suspended(struct brcms_hardware *wlc_hw,
				      uint tx_fifo)
{
	/* check that a suspend has been requested and is no longer pending */

	/*
	 * for DMA mode, the suspend request is set in xmtcontrol of the DMA
	 * engine, and the tx fifo suspend at the lower end of the MAC is
	 * acknowledged in the chnstatus register.
	 *
	 * The tx fifo suspend completion is independent of the DMA suspend
	 * completion and may be acked before or after the DMA is suspended.
	 */
	if (dma_txsuspended(wlc_hw->di[tx_fifo]) &&
	    (R_REG(&wlc_hw->regs->chnstatus) &
	     (1 << tx_fifo)) == 0)
		return true;

	return false;
}

static void brcms_b_tx_fifo_resume(struct brcms_hardware *wlc_hw,
				   uint tx_fifo)
{
	/* BMAC_NOTE: BRCMS_TX_FIFO_ENAB is done in brcms_c_dpc() for DMA case
	 * but need to be done here for PIO otherwise the watchdog will catch
	 * the inconsistency and fire
	 */
	/* Two clients of this code, 11h Quiet period and scanning. */
	if (wlc_hw->di[tx_fifo])
		dma_txresume(wlc_hw->di[tx_fifo]);

	/* allow core to sleep again */
	if (wlc_hw->suspended_fifos == 0)
		return;
	else {
		wlc_hw->suspended_fifos &= ~(1 << tx_fifo);
		if (wlc_hw->suspended_fifos == 0)
			brcms_c_ucode_wake_override_clear(wlc_hw,
						BRCMS_WAKE_OVERRIDE_TXFIFO);
	}
}

/*
 * Read and clear macintmask and macintstatus and intstatus registers.
 * This routine should be called with interrupts off
 * Return:
 *   -1 if DEVICEREMOVED(wlc) evaluates to true;
 *   0 if the interrupt is not for us, or we are in some special cases;
 *   device interrupt status bits otherwise.
 */
static inline u32 wlc_intstatus(struct brcms_c_info *wlc, bool in_isr)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	struct d11regs *regs = wlc_hw->regs;
	u32 macintstatus;

	/* macintstatus includes a DMA interrupt summary bit */
	macintstatus = R_REG(&regs->macintstatus);

	BCMMSG(wlc->wiphy, "wl%d: macintstatus: 0x%x\n", wlc_hw->unit,
		 macintstatus);

	/* detect cardbus removed, in power down(suspend) and in reset */
	if (DEVICEREMOVED(wlc))
		return -1;

	/* DEVICEREMOVED succeeds even when the core is still resetting,
	 * handle that case here.
	 */
	if (macintstatus == 0xffffffff)
		return 0;

	/* defer unsolicited interrupts */
	macintstatus &= (in_isr ? wlc->macintmask : wlc->defmacintmask);

	/* if not for us */
	if (macintstatus == 0)
		return 0;

	/* interrupts are already turned off for CFE build
	 * Caution: For CFE Turning off the interrupts again has some undesired
	 * consequences
	 */
	/* turn off the interrupts */
	W_REG(&regs->macintmask, 0);
	(void)R_REG(&regs->macintmask);	/* sync readback */
	wlc->macintmask = 0;

	/* clear device interrupts */
	W_REG(&regs->macintstatus, macintstatus);

	/* MI_DMAINT is indication of non-zero intstatus */
	if (macintstatus & MI_DMAINT)
		/*
		 * only fifo interrupt enabled is I_RI in
		 * RX_FIFO. If MI_DMAINT is set, assume it
		 * is set and clear the interrupt.
		 */
		W_REG(&regs->intctrlregs[RX_FIFO].intstatus,
		      DEF_RXINTMASK);

	return macintstatus;
}

/* Update wlc->macintstatus and wlc->intstatus[]. */
/* Return true if they are updated successfully. false otherwise */
bool brcms_c_intrsupd(struct brcms_c_info *wlc)
{
	u32 macintstatus;

	/* read and clear macintstatus and intstatus registers */
	macintstatus = wlc_intstatus(wlc, false);

	/* device is removed */
	if (macintstatus == 0xffffffff)
		return false;

	/* update interrupt status in software */
	wlc->macintstatus |= macintstatus;

	return true;
}

/*
 * First-level interrupt processing.
 * Return true if this was our interrupt, false otherwise.
 * *wantdpc will be set to true if further brcms_c_dpc() processing is required,
 * false otherwise.
 */
bool brcms_c_isr(struct brcms_c_info *wlc, bool *wantdpc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	u32 macintstatus;

	*wantdpc = false;

	if (!wlc_hw->up || !wlc->macintmask)
		return false;

	/* read and clear macintstatus and intstatus registers */
	macintstatus = wlc_intstatus(wlc, true);

	if (macintstatus == 0xffffffff)
		wiphy_err(wlc->wiphy, "DEVICEREMOVED detected in the ISR code"
			  " path\n");

	/* it is not for us */
	if (macintstatus == 0)
		return false;

	*wantdpc = true;

	/* save interrupt status bits */
	wlc->macintstatus = macintstatus;

	return true;

}

void brcms_c_suspend_mac_and_wait(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	struct d11regs *regs = wlc_hw->regs;
	u32 mc, mi;
	struct wiphy *wiphy = wlc->wiphy;

	BCMMSG(wlc->wiphy, "wl%d: bandunit %d\n", wlc_hw->unit,
		wlc_hw->band->bandunit);

	/*
	 * Track overlapping suspend requests
	 */
	wlc_hw->mac_suspend_depth++;
	if (wlc_hw->mac_suspend_depth > 1)
		return;

	/* force the core awake */
	brcms_c_ucode_wake_override_set(wlc_hw, BRCMS_WAKE_OVERRIDE_MACSUSPEND);

	mc = R_REG(&regs->maccontrol);

	if (mc == 0xffffffff) {
		wiphy_err(wiphy, "wl%d: %s: dead chip\n", wlc_hw->unit,
			  __func__);
		brcms_down(wlc->wl);
		return;
	}
	WARN_ON(mc & MCTL_PSM_JMP_0);
	WARN_ON(!(mc & MCTL_PSM_RUN));
	WARN_ON(!(mc & MCTL_EN_MAC));

	mi = R_REG(&regs->macintstatus);
	if (mi == 0xffffffff) {
		wiphy_err(wiphy, "wl%d: %s: dead chip\n", wlc_hw->unit,
			  __func__);
		brcms_down(wlc->wl);
		return;
	}
	WARN_ON(mi & MI_MACSSPNDD);

	brcms_b_mctrl(wlc_hw, MCTL_EN_MAC, 0);

	SPINWAIT(!(R_REG(&regs->macintstatus) & MI_MACSSPNDD),
		 BRCMS_MAX_MAC_SUSPEND);

	if (!(R_REG(&regs->macintstatus) & MI_MACSSPNDD)) {
		wiphy_err(wiphy, "wl%d: wlc_suspend_mac_and_wait: waited %d uS"
			  " and MI_MACSSPNDD is still not on.\n",
			  wlc_hw->unit, BRCMS_MAX_MAC_SUSPEND);
		wiphy_err(wiphy, "wl%d: psmdebug 0x%08x, phydebug 0x%08x, "
			  "psm_brc 0x%04x\n", wlc_hw->unit,
			  R_REG(&regs->psmdebug),
			  R_REG(&regs->phydebug),
			  R_REG(&regs->psm_brc));
	}

	mc = R_REG(&regs->maccontrol);
	if (mc == 0xffffffff) {
		wiphy_err(wiphy, "wl%d: %s: dead chip\n", wlc_hw->unit,
			  __func__);
		brcms_down(wlc->wl);
		return;
	}
	WARN_ON(mc & MCTL_PSM_JMP_0);
	WARN_ON(!(mc & MCTL_PSM_RUN));
	WARN_ON(mc & MCTL_EN_MAC);
}

void brcms_c_enable_mac(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	struct d11regs *regs = wlc_hw->regs;
	u32 mc, mi;

	BCMMSG(wlc->wiphy, "wl%d: bandunit %d\n", wlc_hw->unit,
		wlc->band->bandunit);

	/*
	 * Track overlapping suspend requests
	 */
	wlc_hw->mac_suspend_depth--;
	if (wlc_hw->mac_suspend_depth > 0)
		return;

	mc = R_REG(&regs->maccontrol);
	WARN_ON(mc & MCTL_PSM_JMP_0);
	WARN_ON(mc & MCTL_EN_MAC);
	WARN_ON(!(mc & MCTL_PSM_RUN));

	brcms_b_mctrl(wlc_hw, MCTL_EN_MAC, MCTL_EN_MAC);
	W_REG(&regs->macintstatus, MI_MACSSPNDD);

	mc = R_REG(&regs->maccontrol);
	WARN_ON(mc & MCTL_PSM_JMP_0);
	WARN_ON(!(mc & MCTL_EN_MAC));
	WARN_ON(!(mc & MCTL_PSM_RUN));

	mi = R_REG(&regs->macintstatus);
	WARN_ON(mi & MI_MACSSPNDD);

	brcms_c_ucode_wake_override_clear(wlc_hw,
					  BRCMS_WAKE_OVERRIDE_MACSUSPEND);
}

static void brcms_upd_ofdm_pctl1_table(struct brcms_hardware *wlc_hw)
{
	u8 rate;
	u8 rates[8] = {
		BRCM_RATE_6M, BRCM_RATE_9M, BRCM_RATE_12M, BRCM_RATE_18M,
		BRCM_RATE_24M, BRCM_RATE_36M, BRCM_RATE_48M, BRCM_RATE_54M
	};
	u16 entry_ptr;
	u16 pctl1;
	uint i;

	if (!BRCMS_PHY_11N_CAP(wlc_hw->band))
		return;

	/* walk the phy rate table and update the entries */
	for (i = 0; i < ARRAY_SIZE(rates); i++) {
		rate = rates[i];

		entry_ptr = brcms_b_ofdm_ratetable_offset(wlc_hw, rate);

		/* read the SHM Rate Table entry OFDM PCTL1 values */
		pctl1 =
		    brcms_b_read_shm(wlc_hw, entry_ptr + M_RT_OFDM_PCTL1_POS);

		/* modify the value */
		pctl1 &= ~PHY_TXC1_MODE_MASK;
		pctl1 |= (wlc_hw->hw_stf_ss_opmode << PHY_TXC1_MODE_SHIFT);

		/* Update the SHM Rate Table entry OFDM PCTL1 values */
		brcms_b_write_shm(wlc_hw, entry_ptr + M_RT_OFDM_PCTL1_POS,
				   pctl1);
	}
}

static u16 brcms_b_ofdm_ratetable_offset(struct brcms_hardware *wlc_hw,
					 u8 rate)
{
	uint i;
	u8 plcp_rate = 0;
	struct plcp_signal_rate_lookup {
		u8 rate;
		u8 signal_rate;
	};
	/* OFDM RATE sub-field of PLCP SIGNAL field, per 802.11 sec 17.3.4.1 */
	const struct plcp_signal_rate_lookup rate_lookup[] = {
		{BRCM_RATE_6M, 0xB},
		{BRCM_RATE_9M, 0xF},
		{BRCM_RATE_12M, 0xA},
		{BRCM_RATE_18M, 0xE},
		{BRCM_RATE_24M, 0x9},
		{BRCM_RATE_36M, 0xD},
		{BRCM_RATE_48M, 0x8},
		{BRCM_RATE_54M, 0xC}
	};

	for (i = 0; i < ARRAY_SIZE(rate_lookup); i++) {
		if (rate == rate_lookup[i].rate) {
			plcp_rate = rate_lookup[i].signal_rate;
			break;
		}
	}

	/* Find the SHM pointer to the rate table entry by looking in the
	 * Direct-map Table
	 */
	return 2 * brcms_b_read_shm(wlc_hw, M_RT_DIRMAP_A + (plcp_rate * 2));
}

void brcms_b_band_stf_ss_set(struct brcms_hardware *wlc_hw, u8 stf_mode)
{
	wlc_hw->hw_stf_ss_opmode = stf_mode;

	if (wlc_hw->clk)
		brcms_upd_ofdm_pctl1_table(wlc_hw);
}

static bool brcms_b_validate_chip_access(struct brcms_hardware *wlc_hw)
{
	struct d11regs *regs;
	u32 w, val;
	struct wiphy *wiphy = wlc_hw->wlc->wiphy;

	BCMMSG(wiphy, "wl%d\n", wlc_hw->unit);

	regs = wlc_hw->regs;

	/* Validate dchip register access */

	W_REG(&regs->objaddr, OBJADDR_SHM_SEL | 0);
	(void)R_REG(&regs->objaddr);
	w = R_REG(&regs->objdata);

	/* Can we write and read back a 32bit register? */
	W_REG(&regs->objaddr, OBJADDR_SHM_SEL | 0);
	(void)R_REG(&regs->objaddr);
	W_REG(&regs->objdata, (u32) 0xaa5555aa);

	W_REG(&regs->objaddr, OBJADDR_SHM_SEL | 0);
	(void)R_REG(&regs->objaddr);
	val = R_REG(&regs->objdata);
	if (val != (u32) 0xaa5555aa) {
		wiphy_err(wiphy, "wl%d: validate_chip_access: SHM = 0x%x, "
			  "expected 0xaa5555aa\n", wlc_hw->unit, val);
		return false;
	}

	W_REG(&regs->objaddr, OBJADDR_SHM_SEL | 0);
	(void)R_REG(&regs->objaddr);
	W_REG(&regs->objdata, (u32) 0x55aaaa55);

	W_REG(&regs->objaddr, OBJADDR_SHM_SEL | 0);
	(void)R_REG(&regs->objaddr);
	val = R_REG(&regs->objdata);
	if (val != (u32) 0x55aaaa55) {
		wiphy_err(wiphy, "wl%d: validate_chip_access: SHM = 0x%x, "
			  "expected 0x55aaaa55\n", wlc_hw->unit, val);
		return false;
	}

	W_REG(&regs->objaddr, OBJADDR_SHM_SEL | 0);
	(void)R_REG(&regs->objaddr);
	W_REG(&regs->objdata, w);

	/* clear CFPStart */
	W_REG(&regs->tsf_cfpstart, 0);

	w = R_REG(&regs->maccontrol);
	if ((w != (MCTL_IHR_EN | MCTL_WAKE)) &&
	    (w != (MCTL_IHR_EN | MCTL_GMODE | MCTL_WAKE))) {
		wiphy_err(wiphy, "wl%d: validate_chip_access: maccontrol = "
			  "0x%x, expected 0x%x or 0x%x\n", wlc_hw->unit, w,
			  (MCTL_IHR_EN | MCTL_WAKE),
			  (MCTL_IHR_EN | MCTL_GMODE | MCTL_WAKE));
		return false;
	}

	return true;
}

#define PHYPLL_WAIT_US	100000

void brcms_b_core_phypll_ctl(struct brcms_hardware *wlc_hw, bool on)
{
	struct d11regs *regs;
	u32 tmp;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	tmp = 0;
	regs = wlc_hw->regs;

	if (on) {
		if ((wlc_hw->sih->chip == BCM4313_CHIP_ID)) {
			OR_REG(&regs->clk_ctl_st,
			       (CCS_ERSRC_REQ_HT | CCS_ERSRC_REQ_D11PLL |
				CCS_ERSRC_REQ_PHYPLL));
			SPINWAIT((R_REG(&regs->clk_ctl_st) &
				  (CCS_ERSRC_AVAIL_HT)) != (CCS_ERSRC_AVAIL_HT),
				 PHYPLL_WAIT_US);

			tmp = R_REG(&regs->clk_ctl_st);
			if ((tmp & (CCS_ERSRC_AVAIL_HT)) !=
			    (CCS_ERSRC_AVAIL_HT))
				wiphy_err(wlc_hw->wlc->wiphy, "%s: turn on PHY"
					  " PLL failed\n", __func__);
		} else {
			OR_REG(&regs->clk_ctl_st,
			       (CCS_ERSRC_REQ_D11PLL | CCS_ERSRC_REQ_PHYPLL));
			SPINWAIT((R_REG(&regs->clk_ctl_st) &
				  (CCS_ERSRC_AVAIL_D11PLL |
				   CCS_ERSRC_AVAIL_PHYPLL)) !=
				 (CCS_ERSRC_AVAIL_D11PLL |
				  CCS_ERSRC_AVAIL_PHYPLL), PHYPLL_WAIT_US);

			tmp = R_REG(&regs->clk_ctl_st);
			if ((tmp &
			     (CCS_ERSRC_AVAIL_D11PLL | CCS_ERSRC_AVAIL_PHYPLL))
			    !=
			    (CCS_ERSRC_AVAIL_D11PLL | CCS_ERSRC_AVAIL_PHYPLL))
				wiphy_err(wlc_hw->wlc->wiphy, "%s: turn on "
					  "PHY PLL failed\n", __func__);
		}
	} else {
		/*
		 * Since the PLL may be shared, other cores can still
		 * be requesting it; so we'll deassert the request but
		 * not wait for status to comply.
		 */
		AND_REG(&regs->clk_ctl_st, ~CCS_ERSRC_REQ_PHYPLL);
		tmp = R_REG(&regs->clk_ctl_st);
	}
}

void brcms_c_coredisable(struct brcms_hardware *wlc_hw)
{
	bool dev_gone;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	dev_gone = DEVICEREMOVED(wlc_hw->wlc);

	if (dev_gone)
		return;

	if (wlc_hw->noreset)
		return;

	/* radio off */
	wlc_phy_switch_radio(wlc_hw->band->pi, OFF);

	/* turn off analog core */
	wlc_phy_anacore(wlc_hw->band->pi, OFF);

	/* turn off PHYPLL to save power */
	brcms_b_core_phypll_ctl(wlc_hw, false);

	/* No need to set wlc->pub->radio_active = OFF
	 * because this function needs down capability and
	 * radio_active is designed for BCMNODOWN.
	 */

	/* remove gpio controls */
	if (wlc_hw->ucode_dbgsel)
		ai_gpiocontrol(wlc_hw->sih, ~0, 0, GPIO_DRV_PRIORITY);

	wlc_hw->clk = false;
	ai_core_disable(wlc_hw->sih, 0);
	wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, false);
}

/* power both the pll and external oscillator on/off */
static void brcms_b_xtal(struct brcms_hardware *wlc_hw, bool want)
{
	BCMMSG(wlc_hw->wlc->wiphy, "wl%d: want %d\n", wlc_hw->unit, want);

	/*
	 * dont power down if plldown is false or
	 * we must poll hw radio disable
	 */
	if (!want && wlc_hw->pllreq)
		return;

	if (wlc_hw->sih)
		ai_clkctl_xtal(wlc_hw->sih, XTAL | PLL, want);

	wlc_hw->sbclk = want;
	if (!wlc_hw->sbclk) {
		wlc_hw->clk = false;
		if (wlc_hw->band && wlc_hw->band->pi)
			wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, false);
	}
}

static void brcms_c_flushqueues(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	uint i;

	wlc->txpend16165war = 0;

	/* free any posted tx packets */
	for (i = 0; i < NFIFO; i++)
		if (wlc_hw->di[i]) {
			dma_txreclaim(wlc_hw->di[i], DMA_RANGE_ALL);
			TXPKTPENDCLR(wlc, i);
			BCMMSG(wlc->wiphy, "pktpend fifo %d clrd\n", i);
		}

	/* free any posted rx packets */
	dma_rxreclaim(wlc_hw->di[RX_FIFO]);
}

u16 brcms_b_read_shm(struct brcms_hardware *wlc_hw, uint offset)
{
	return brcms_b_read_objmem(wlc_hw, offset, OBJADDR_SHM_SEL);
}

void brcms_b_write_shm(struct brcms_hardware *wlc_hw, uint offset, u16 v)
{
	brcms_b_write_objmem(wlc_hw, offset, v, OBJADDR_SHM_SEL);
}

static u16
brcms_b_read_objmem(struct brcms_hardware *wlc_hw, uint offset, u32 sel)
{
	struct d11regs *regs = wlc_hw->regs;
	u16 *objdata_lo = (u16 *)&regs->objdata;
	u16 *objdata_hi = objdata_lo + 1;
	u16 v;

	W_REG(&regs->objaddr, sel | (offset >> 2));
	(void)R_REG(&regs->objaddr);
	if (offset & 2)
		v = R_REG(objdata_hi);
	else
		v = R_REG(objdata_lo);

	return v;
}

static void
brcms_b_write_objmem(struct brcms_hardware *wlc_hw, uint offset, u16 v,
		     u32 sel)
{
	struct d11regs *regs = wlc_hw->regs;
	u16 *objdata_lo = (u16 *)&regs->objdata;
	u16 *objdata_hi = objdata_lo + 1;

	W_REG(&regs->objaddr, sel | (offset >> 2));
	(void)R_REG(&regs->objaddr);
	if (offset & 2)
		W_REG(objdata_hi, v);
	else
		W_REG(objdata_lo, v);
}

/* Copy a buffer to shared memory of specified type .
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 * 'sel' selects the type of memory
 */
void
brcms_b_copyto_objmem(struct brcms_hardware *wlc_hw, uint offset,
		      const void *buf, int len, u32 sel)
{
	u16 v;
	const u8 *p = (const u8 *)buf;
	int i;

	if (len <= 0 || (offset & 1) || (len & 1))
		return;

	for (i = 0; i < len; i += 2) {
		v = p[i] | (p[i + 1] << 8);
		brcms_b_write_objmem(wlc_hw, offset + i, v, sel);
	}
}

/* Copy a piece of shared memory of specified type to a buffer .
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 * 'sel' selects the type of memory
 */
void
brcms_b_copyfrom_objmem(struct brcms_hardware *wlc_hw, uint offset, void *buf,
			 int len, u32 sel)
{
	u16 v;
	u8 *p = (u8 *) buf;
	int i;

	if (len <= 0 || (offset & 1) || (len & 1))
		return;

	for (i = 0; i < len; i += 2) {
		v = brcms_b_read_objmem(wlc_hw, offset + i, sel);
		p[i] = v & 0xFF;
		p[i + 1] = (v >> 8) & 0xFF;
	}
}

void brcms_b_copyfrom_vars(struct brcms_hardware *wlc_hw, char **buf,
			   uint *len)
{
	BCMMSG(wlc_hw->wlc->wiphy, "nvram vars totlen=%d\n",
		wlc_hw->vars_size);

	*buf = wlc_hw->vars;
	*len = wlc_hw->vars_size;
}

void brcms_b_retrylimit_upd(struct brcms_hardware *wlc_hw, u16 SRL, u16 LRL)
{
	wlc_hw->SRL = SRL;
	wlc_hw->LRL = LRL;

	/* write retry limit to SCR, shouldn't need to suspend */
	if (wlc_hw->up) {
		W_REG(&wlc_hw->regs->objaddr,
		      OBJADDR_SCR_SEL | S_DOT11_SRC_LMT);
		(void)R_REG(&wlc_hw->regs->objaddr);
		W_REG(&wlc_hw->regs->objdata, wlc_hw->SRL);
		W_REG(&wlc_hw->regs->objaddr,
		      OBJADDR_SCR_SEL | S_DOT11_LRC_LMT);
		(void)R_REG(&wlc_hw->regs->objaddr);
		W_REG(&wlc_hw->regs->objdata, wlc_hw->LRL);
	}
}

void brcms_b_pllreq(struct brcms_hardware *wlc_hw, bool set, u32 req_bit)
{
	if (set) {
		if (mboolisset(wlc_hw->pllreq, req_bit))
			return;

		mboolset(wlc_hw->pllreq, req_bit);

		if (mboolisset(wlc_hw->pllreq, BRCMS_PLLREQ_FLIP)) {
			if (!wlc_hw->sbclk)
				brcms_b_xtal(wlc_hw, ON);
		}
	} else {
		if (!mboolisset(wlc_hw->pllreq, req_bit))
			return;

		mboolclr(wlc_hw->pllreq, req_bit);

		if (mboolisset(wlc_hw->pllreq, BRCMS_PLLREQ_FLIP)) {
			if (wlc_hw->sbclk)
				brcms_b_xtal(wlc_hw, OFF);
		}
	}

	return;
}

void brcms_b_antsel_set(struct brcms_hardware *wlc_hw, u32 antsel_avail)
{
	wlc_hw->antsel_avail = antsel_avail;
}

/*
 * conditions under which the PM bit should be set in outgoing frames
 * and STAY_AWAKE is meaningful
 */
bool brcms_c_ps_allowed(struct brcms_c_info *wlc)
{
	int idx;
	struct brcms_bss_cfg *cfg;

	/* disallow PS when one of the following global conditions meets */
	if (!wlc->pub->associated)
		return false;

	/* disallow PS when one of these meets when not scanning */
	if (AP_ACTIVE(wlc) || wlc->monitor)
		return false;

	for (idx = 0; idx < BRCMS_MAXBSSCFG; idx++) {
		cfg = wlc->bsscfg[idx];
		if (cfg && BSSCFG_STA(cfg) && cfg->associated) {
			/*
			 * disallow PS when one of the following
			 * bsscfg specific conditions meets
			 */
			if (!cfg->BSS || !BRCMS_PORTOPEN(cfg))
				return false;

			if (!cfg->dtim_programmed)
				return false;
		}
	}

	return true;
}

void brcms_b_reset(struct brcms_hardware *wlc_hw)
{
	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	/* reset the core */
	if (!DEVICEREMOVED(wlc_hw->wlc))
		brcms_b_corereset(wlc_hw, BRCMS_USE_COREFLAGS);

	/* purge the dma rings */
	brcms_c_flushqueues(wlc_hw->wlc);

	brcms_c_reset_bmac_done(wlc_hw->wlc);
}

void brcms_c_reset(struct brcms_c_info *wlc)
{
	BCMMSG(wlc->wiphy, "wl%d\n", wlc->pub->unit);

	/* slurp up hw mac counters before core reset */
	brcms_c_statsupd(wlc);

	/* reset our snapshot of macstat counters */
	memset((char *)wlc->core->macstat_snapshot, 0,
		sizeof(struct macstat));

	brcms_b_reset(wlc->hw);
}

void brcms_c_fatal_error(struct brcms_c_info *wlc)
{
	wiphy_err(wlc->wiphy, "wl%d: fatal error, reinitializing\n",
		  wlc->pub->unit);
	brcms_init(wlc->wl);
}

/* Return the channel the driver should initialize during brcms_c_init.
 * the channel may have to be changed from the currently configured channel
 * if other configurations are in conflict (bandlocked, 11n mode disabled,
 * invalid channel for current country, etc.)
 */
static u16 brcms_c_init_chanspec(struct brcms_c_info *wlc)
{
	u16 chanspec =
	    1 | WL_CHANSPEC_BW_20 | WL_CHANSPEC_CTL_SB_NONE |
	    WL_CHANSPEC_BAND_2G;

	return chanspec;
}

struct scb global_scb;

static void brcms_c_init_scb(struct brcms_c_info *wlc, struct scb *scb)
{
	int i;
	scb->flags = SCB_WMECAP | SCB_HTCAP;
	for (i = 0; i < NUMPRIO; i++)
		scb->seqnum[i] = 0;
}

/* d11 core init
 *   reset PSM
 *   download ucode/PCM
 *   let ucode run to suspended
 *   download ucode inits
 *   config other core registers
 *   init dma
 */
static void brcms_b_coreinit(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	struct d11regs *regs;
	u32 sflags;
	uint bcnint_us;
	uint i = 0;
	bool fifosz_fixup = false;
	int err = 0;
	u16 buf[NFIFO];
	struct wiphy *wiphy = wlc->wiphy;

	regs = wlc_hw->regs;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc_hw->unit);

	/* reset PSM */
	brcms_b_mctrl(wlc_hw, ~0, (MCTL_IHR_EN | MCTL_PSM_JMP_0 | MCTL_WAKE));

	brcms_ucode_download(wlc_hw);
	/*
	 * FIFOSZ fixup. driver wants to controls the fifo allocation.
	 */
	fifosz_fixup = true;

	/* let the PSM run to the suspended state, set mode to BSS STA */
	W_REG(&regs->macintstatus, -1);
	brcms_b_mctrl(wlc_hw, ~0,
		       (MCTL_IHR_EN | MCTL_INFRA | MCTL_PSM_RUN | MCTL_WAKE));

	/* wait for ucode to self-suspend after auto-init */
	SPINWAIT(((R_REG(&regs->macintstatus) & MI_MACSSPNDD) == 0),
		 1000 * 1000);
	if ((R_REG(&regs->macintstatus) & MI_MACSSPNDD) == 0)
		wiphy_err(wiphy, "wl%d: wlc_coreinit: ucode did not self-"
			  "suspend!\n", wlc_hw->unit);

	brcms_c_gpio_init(wlc);

	sflags = ai_core_sflags(wlc_hw->sih, 0, 0);

	if (D11REV_IS(wlc_hw->corerev, 23)) {
		if (BRCMS_ISNPHY(wlc_hw->band))
			brcms_c_write_inits(wlc_hw, d11n0initvals16);
		else
			wiphy_err(wiphy, "%s: wl%d: unsupported phy in corerev"
				  " %d\n", __func__, wlc_hw->unit,
				  wlc_hw->corerev);
	} else if (D11REV_IS(wlc_hw->corerev, 24)) {
		if (BRCMS_ISLCNPHY(wlc_hw->band))
			brcms_c_write_inits(wlc_hw, d11lcn0initvals24);
		else
			wiphy_err(wiphy, "%s: wl%d: unsupported phy in corerev"
				  " %d\n", __func__, wlc_hw->unit,
				  wlc_hw->corerev);
	} else {
		wiphy_err(wiphy, "%s: wl%d: unsupported corerev %d\n",
			  __func__, wlc_hw->unit, wlc_hw->corerev);
	}

	/* For old ucode, txfifo sizes needs to be modified(increased) */
	if (fifosz_fixup == true)
		brcms_b_corerev_fifofixup(wlc_hw);

	/* check txfifo allocations match between ucode and driver */
	buf[TX_AC_BE_FIFO] = brcms_b_read_shm(wlc_hw, M_FIFOSIZE0);
	if (buf[TX_AC_BE_FIFO] != wlc_hw->xmtfifo_sz[TX_AC_BE_FIFO]) {
		i = TX_AC_BE_FIFO;
		err = -1;
	}
	buf[TX_AC_VI_FIFO] = brcms_b_read_shm(wlc_hw, M_FIFOSIZE1);
	if (buf[TX_AC_VI_FIFO] != wlc_hw->xmtfifo_sz[TX_AC_VI_FIFO]) {
		i = TX_AC_VI_FIFO;
		err = -1;
	}
	buf[TX_AC_BK_FIFO] = brcms_b_read_shm(wlc_hw, M_FIFOSIZE2);
	buf[TX_AC_VO_FIFO] = (buf[TX_AC_BK_FIFO] >> 8) & 0xff;
	buf[TX_AC_BK_FIFO] &= 0xff;
	if (buf[TX_AC_BK_FIFO] != wlc_hw->xmtfifo_sz[TX_AC_BK_FIFO]) {
		i = TX_AC_BK_FIFO;
		err = -1;
	}
	if (buf[TX_AC_VO_FIFO] != wlc_hw->xmtfifo_sz[TX_AC_VO_FIFO]) {
		i = TX_AC_VO_FIFO;
		err = -1;
	}
	buf[TX_BCMC_FIFO] = brcms_b_read_shm(wlc_hw, M_FIFOSIZE3);
	buf[TX_ATIM_FIFO] = (buf[TX_BCMC_FIFO] >> 8) & 0xff;
	buf[TX_BCMC_FIFO] &= 0xff;
	if (buf[TX_BCMC_FIFO] != wlc_hw->xmtfifo_sz[TX_BCMC_FIFO]) {
		i = TX_BCMC_FIFO;
		err = -1;
	}
	if (buf[TX_ATIM_FIFO] != wlc_hw->xmtfifo_sz[TX_ATIM_FIFO]) {
		i = TX_ATIM_FIFO;
		err = -1;
	}
	if (err != 0)
		wiphy_err(wiphy, "wlc_coreinit: txfifo mismatch: ucode size %d"
			  " driver size %d index %d\n", buf[i],
			  wlc_hw->xmtfifo_sz[i], i);

	/* make sure we can still talk to the mac */
	WARN_ON(R_REG(&regs->maccontrol) == 0xffffffff);

	/* band-specific inits done by wlc_bsinit() */

	/* Set up frame burst size and antenna swap threshold init values */
	brcms_b_write_shm(wlc_hw, M_MBURST_SIZE, MAXTXFRAMEBURST);
	brcms_b_write_shm(wlc_hw, M_MAX_ANTCNT, ANTCNT);

	/* enable one rx interrupt per received frame */
	W_REG(&regs->intrcvlazy[0], (1 << IRL_FC_SHIFT));

	/* set the station mode (BSS STA) */
	brcms_b_mctrl(wlc_hw,
		       (MCTL_INFRA | MCTL_DISCARD_PMQ | MCTL_AP),
		       (MCTL_INFRA | MCTL_DISCARD_PMQ));

	/* set up Beacon interval */
	bcnint_us = 0x8000 << 10;
	W_REG(&regs->tsf_cfprep, (bcnint_us << CFPREP_CBI_SHIFT));
	W_REG(&regs->tsf_cfpstart, bcnint_us);
	W_REG(&regs->macintstatus, MI_GP1);

	/* write interrupt mask */
	W_REG(&regs->intctrlregs[RX_FIFO].intmask, DEF_RXINTMASK);

	/* allow the MAC to control the PHY clock (dynamic on/off) */
	brcms_b_macphyclk_set(wlc_hw, ON);

	/* program dynamic clock control fast powerup delay register */
	wlc->fastpwrup_dly = ai_clkctl_fast_pwrup_delay(wlc_hw->sih);
	W_REG(&regs->scc_fastpwrup_dly, wlc->fastpwrup_dly);

	/* tell the ucode the corerev */
	brcms_b_write_shm(wlc_hw, M_MACHW_VER, (u16) wlc_hw->corerev);

	/* tell the ucode MAC capabilities */
	brcms_b_write_shm(wlc_hw, M_MACHW_CAP_L,
			   (u16) (wlc_hw->machwcap & 0xffff));
	brcms_b_write_shm(wlc_hw, M_MACHW_CAP_H,
			   (u16) ((wlc_hw->
				      machwcap >> 16) & 0xffff));

	/* write retry limits to SCR, this done after PSM init */
	W_REG(&regs->objaddr, OBJADDR_SCR_SEL | S_DOT11_SRC_LMT);
	(void)R_REG(&regs->objaddr);
	W_REG(&regs->objdata, wlc_hw->SRL);
	W_REG(&regs->objaddr, OBJADDR_SCR_SEL | S_DOT11_LRC_LMT);
	(void)R_REG(&regs->objaddr);
	W_REG(&regs->objdata, wlc_hw->LRL);

	/* write rate fallback retry limits */
	brcms_b_write_shm(wlc_hw, M_SFRMTXCNTFBRTHSD, wlc_hw->SFBL);
	brcms_b_write_shm(wlc_hw, M_LFRMTXCNTFBRTHSD, wlc_hw->LFBL);

	AND_REG(&regs->ifs_ctl, 0x0FFF);
	W_REG(&regs->ifs_aifsn, EDCF_AIFSN_MIN);

	/* dma initializations */
	wlc->txpend16165war = 0;

	/* init the tx dma engines */
	for (i = 0; i < NFIFO; i++) {
		if (wlc_hw->di[i])
			dma_txinit(wlc_hw->di[i]);
	}

	/* init the rx dma engine(s) and post receive buffers */
	dma_rxinit(wlc_hw->di[RX_FIFO]);
	dma_rxfill(wlc_hw->di[RX_FIFO]);
}

void
brcms_b_init(struct brcms_hardware *wlc_hw, u16 chanspec,
			  bool mute) {
	u32 macintmask;
	bool fastclk;
	struct brcms_c_info *wlc = wlc_hw->wlc;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	/* request FAST clock if not on */
	fastclk = wlc_hw->forcefastclk;
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, CLK_FAST);

	/* disable interrupts */
	macintmask = brcms_intrsoff(wlc->wl);

	/* set up the specified band and chanspec */
	brcms_c_setxband(wlc_hw, CHSPEC_BANDUNIT(chanspec));
	wlc_phy_chanspec_radio_set(wlc_hw->band->pi, chanspec);

	/* do one-time phy inits and calibration */
	wlc_phy_cal_init(wlc_hw->band->pi);

	/* core-specific initialization */
	brcms_b_coreinit(wlc);

	/* suspend the tx fifos and mute the phy for preism cac time */
	if (mute)
		brcms_b_mute(wlc_hw, ON, PHY_MUTE_FOR_PREISM);

	/* band-specific inits */
	brcms_b_bsinit(wlc, chanspec);

	/* restore macintmask */
	brcms_intrsrestore(wlc->wl, macintmask);

	/* seed wake_override with BRCMS_WAKE_OVERRIDE_MACSUSPEND since the mac
	 * is suspended and brcms_c_enable_mac() will clear this override bit.
	 */
	mboolset(wlc_hw->wake_override, BRCMS_WAKE_OVERRIDE_MACSUSPEND);

	/*
	 * initialize mac_suspend_depth to 1 to match ucode
	 * initial suspended state
	 */
	wlc_hw->mac_suspend_depth = 1;

	/* restore the clk */
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, CLK_DYNAMIC);
}

void brcms_c_init(struct brcms_c_info *wlc)
{
	struct d11regs *regs;
	u16 chanspec;
	int i;
	struct brcms_bss_cfg *bsscfg;
	bool mute = false;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc->pub->unit);

	regs = wlc->regs;

	/*
	 * This will happen if a big-hammer was executed. In
	 * that case, we want to go back to the channel that
	 * we were on and not new channel
	 */
	if (wlc->pub->associated)
		chanspec = wlc->home_chanspec;
	else
		chanspec = brcms_c_init_chanspec(wlc);

	brcms_b_init(wlc->hw, chanspec, mute);

	/* update beacon listen interval */
	brcms_c_bcn_li_upd(wlc);

	/* the world is new again, so is our reported rate */
	brcms_c_reprate_init(wlc);

	/* write ethernet address to core */
	FOREACH_BSS(wlc, i, bsscfg)
		brcms_c_set_mac(bsscfg);
		brcms_c_set_bssid(bsscfg);
	END_FOREACH_BSS()

	/* Update tsf_cfprep if associated and up */
	if (wlc->pub->associated) {
		FOREACH_BSS(wlc, i, bsscfg)
			if (bsscfg->up) {
				u32 bi;

				/* get beacon period and convert to uS */
				bi = bsscfg->current_bss->beacon_period << 10;
				/*
				 * update since init path would reset
				 * to default value
				 */
				W_REG(&regs->tsf_cfprep,
				      (bi << CFPREP_CBI_SHIFT));

				/* Update maccontrol PM related bits */
				brcms_c_set_ps_ctrl(wlc);

				break;
			}
		END_FOREACH_BSS()
	}

	brcms_c_bandinit_ordered(wlc, chanspec);

	brcms_c_init_scb(wlc, &global_scb);

	/* init probe response timeout */
	brcms_c_write_shm(wlc, M_PRS_MAXTIME, wlc->prb_resp_timeout);

	/* init max burst txop (framebursting) */
	brcms_c_write_shm(wlc, M_MBURST_TXOP,
		      (wlc->
		       _rifs ? (EDCF_AC_VO_TXOP_AP << 5) : MAXFRAMEBURST_TXOP));

	/* initialize maximum allowed duty cycle */
	brcms_c_duty_cycle_set(wlc, wlc->tx_duty_cycle_ofdm, true, true);
	brcms_c_duty_cycle_set(wlc, wlc->tx_duty_cycle_cck, false, true);

	/*
	 * Update some shared memory locations related to
	 * max AMPDU size allowed to received
	 */
	brcms_c_ampdu_shm_upd(wlc->ampdu);

	/* band-specific inits */
	brcms_c_bsinit(wlc);

	/* Enable EDCF mode (while the MAC is suspended) */
	if (EDCF_ENAB(wlc->pub)) {
		OR_REG(&regs->ifs_ctl, IFS_USEEDCF);
		brcms_c_edcf_setparams(wlc, false);
	}

	/* Init precedence maps for empty FIFOs */
	brcms_c_tx_prec_map_init(wlc);

	/* read the ucode version if we have not yet done so */
	if (wlc->ucode_rev == 0) {
		wlc->ucode_rev =
		    brcms_c_read_shm(wlc, M_BOM_REV_MAJOR) << NBITS(u16);
		wlc->ucode_rev |= brcms_c_read_shm(wlc, M_BOM_REV_MINOR);
	}

	/* ..now really unleash hell (allow the MAC out of suspend) */
	brcms_c_enable_mac(wlc);

	/* clear tx flow control */
	brcms_c_txflowcontrol_reset(wlc);

	/* clear tx data fifo suspends */
	wlc->tx_suspended = false;

	/* enable the RF Disable Delay timer */
	W_REG(&wlc->regs->rfdisabledly, RFDISABLE_DEFAULT);

	/* initialize mpc delay */
	wlc->mpc_delay_off = wlc->mpc_dlycnt = BRCMS_MPC_MIN_DELAYCNT;

	/*
	 * Initialize WME parameters; if they haven't been set by some other
	 * mechanism (IOVar, etc) then read them from the hardware.
	 */
	if (BRCMS_WME_RETRY_SHORT_GET(wlc, 0) == 0) {
		/* Uninitialized; read from HW */
		int ac;

		for (ac = 0; ac < AC_COUNT; ac++)
			wlc->wme_retries[ac] =
			    brcms_c_read_shm(wlc, M_AC_TXLMT_ADDR(ac));
	}
}

void brcms_c_mac_bcn_promisc_change(struct brcms_c_info *wlc, bool promisc)
{
	wlc->bcnmisc_monitor = promisc;
	brcms_c_mac_bcn_promisc(wlc);
}

void brcms_c_mac_bcn_promisc(struct brcms_c_info *wlc)
{
	if ((AP_ENAB(wlc->pub) && (N_ENAB(wlc->pub) || wlc->band->gmode)) ||
	    wlc->bcnmisc_ibss || wlc->bcnmisc_scan || wlc->bcnmisc_monitor)
		brcms_c_mctrl(wlc, MCTL_BCNS_PROMISC, MCTL_BCNS_PROMISC);
	else
		brcms_c_mctrl(wlc, MCTL_BCNS_PROMISC, 0);
}

/* set or clear maccontrol bits MCTL_PROMISC and MCTL_KEEPCONTROL */
void brcms_c_mac_promisc(struct brcms_c_info *wlc)
{
	u32 promisc_bits = 0;

	/*
	 * promiscuous mode just sets MCTL_PROMISC
	 * Note: APs get all BSS traffic without the need to set
	 * the MCTL_PROMISC bit since all BSS data traffic is
	 * directed at the AP
	 */
	if (PROMISC_ENAB(wlc->pub) && !AP_ENAB(wlc->pub))
		promisc_bits |= MCTL_PROMISC;

	/* monitor mode needs both MCTL_PROMISC and MCTL_KEEPCONTROL
	 * Note: monitor mode also needs MCTL_BCNS_PROMISC, but that is
	 * handled in brcms_c_mac_bcn_promisc()
	 */
	if (MONITOR_ENAB(wlc))
		promisc_bits |= MCTL_PROMISC | MCTL_KEEPCONTROL;

	brcms_c_mctrl(wlc, MCTL_PROMISC | MCTL_KEEPCONTROL, promisc_bits);
}

/* push sw hps and wake state through hardware */
void brcms_c_set_ps_ctrl(struct brcms_c_info *wlc)
{
	u32 v1, v2;
	bool hps;
	bool awake_before;

	hps = PS_ALLOWED(wlc);

	BCMMSG(wlc->wiphy, "wl%d: hps %d\n", wlc->pub->unit, hps);

	v1 = R_REG(&wlc->regs->maccontrol);
	v2 = MCTL_WAKE;
	if (hps)
		v2 |= MCTL_HPS;

	brcms_c_mctrl(wlc, MCTL_WAKE | MCTL_HPS, v2);

	awake_before = ((v1 & MCTL_WAKE) || ((v1 & MCTL_HPS) == 0));

	if (!awake_before)
		brcms_b_wait_for_wake(wlc->hw);

}

/*
 * Write this BSS config's MAC address to core.
 * Updates RXE match engine.
 */
int brcms_c_set_mac(struct brcms_bss_cfg *cfg)
{
	int err = 0;
	struct brcms_c_info *wlc = cfg->wlc;

	if (cfg == wlc->cfg)
		/* enter the MAC addr into the RXE match registers */
		brcms_c_set_addrmatch(wlc, RCM_MAC_OFFSET, cfg->cur_etheraddr);

	brcms_c_ampdu_macaddr_upd(wlc);

	return err;
}

/* Write the BSS config's BSSID address to core (set_bssid in d11procs.tcl).
 * Updates RXE match engine.
 */
void brcms_c_set_bssid(struct brcms_bss_cfg *cfg)
{
	struct brcms_c_info *wlc = cfg->wlc;

	/* if primary config, we need to update BSSID in RXE match registers */
	if (cfg == wlc->cfg)
		brcms_c_set_addrmatch(wlc, RCM_BSSID_OFFSET, cfg->BSSID);
}

void brcms_b_set_shortslot(struct brcms_hardware *wlc_hw, bool shortslot)
{
	wlc_hw->shortslot = shortslot;

	if (BAND_2G(brcms_b_bandtype(wlc_hw)) && wlc_hw->up) {
		brcms_c_suspend_mac_and_wait(wlc_hw->wlc);
		brcms_b_update_slot_timing(wlc_hw, shortslot);
		brcms_c_enable_mac(wlc_hw->wlc);
	}
}

/*
 * Suspend the the MAC and update the slot timing
 * for standard 11b/g (20us slots) or shortslot 11g (9us slots).
 */
void brcms_c_switch_shortslot(struct brcms_c_info *wlc, bool shortslot)
{
	int idx;
	struct brcms_bss_cfg *cfg;

	/* use the override if it is set */
	if (wlc->shortslot_override != BRCMS_SHORTSLOT_AUTO)
		shortslot = (wlc->shortslot_override == BRCMS_SHORTSLOT_ON);

	if (wlc->shortslot == shortslot)
		return;

	wlc->shortslot = shortslot;

	/* update the capability based on current shortslot mode */
	FOREACH_BSS(wlc, idx, cfg)
		if (!cfg->associated)
			continue;
		cfg->current_bss->capability &=
					~WLAN_CAPABILITY_SHORT_SLOT_TIME;
		if (wlc->shortslot)
			cfg->current_bss->capability |=
					WLAN_CAPABILITY_SHORT_SLOT_TIME;
	END_FOREACH_BSS()

	brcms_b_set_shortslot(wlc->hw, shortslot);
}

static u8 brcms_c_local_constraint_qdbm(struct brcms_c_info *wlc)
{
	u8 local;
	s16 local_max;

	local = BRCMS_TXPWR_MAX;
	if (wlc->pub->associated &&
	    (brcmu_chspec_ctlchan(wlc->chanspec) ==
	     brcmu_chspec_ctlchan(wlc->home_chanspec))) {

		/* get the local power constraint if we are on the AP's
		 * channel [802.11h, 7.3.2.13]
		 */
		/* Clamp the value between 0 and BRCMS_TXPWR_MAX w/o
		 * overflowing the target */
		local_max =
		    (wlc->txpwr_local_max -
		     wlc->txpwr_local_constraint) * BRCMS_TXPWR_DB_FACTOR;
		if (local_max > 0 && local_max < BRCMS_TXPWR_MAX)
			return (u8) local_max;
		if (local_max < 0)
			return 0;
	}

	return local;
}

/*
 * propagate home chanspec to all bsscfgs in
 * case bsscfg->current_bss->chanspec is referenced
 */
void brcms_c_set_home_chanspec(struct brcms_c_info *wlc, u16 chanspec)
{
	if (wlc->home_chanspec != chanspec) {
		int idx;
		struct brcms_bss_cfg *cfg;

		wlc->home_chanspec = chanspec;

		FOREACH_BSS(wlc, idx, cfg)
			if (!cfg->associated)
				continue;

			cfg->current_bss->chanspec = chanspec;
		END_FOREACH_BSS()


	}
}

static void brcms_c_set_phy_chanspec(struct brcms_c_info *wlc,
				     u16 chanspec)
{
	/* Save our copy of the chanspec */
	wlc->chanspec = chanspec;

	/* Set the chanspec and power limits for this locale after computing
	 * any 11h local tx power constraints.
	 */
	brcms_c_channel_set_chanspec(wlc->cmi, chanspec,
				 brcms_c_local_constraint_qdbm(wlc));

	if (wlc->stf->ss_algosel_auto)
		brcms_c_stf_ss_algo_channel_get(wlc, &wlc->stf->ss_algo_channel,
					    chanspec);

	brcms_c_stf_ss_update(wlc, wlc->band);

}

void
brcms_b_set_chanspec(struct brcms_hardware *wlc_hw, u16 chanspec,
		      bool mute, struct txpwr_limits *txpwr)
{
	uint bandunit;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d: 0x%x\n", wlc_hw->unit, chanspec);

	wlc_hw->chanspec = chanspec;

	/* Switch bands if necessary */
	if (NBANDS_HW(wlc_hw) > 1) {
		bandunit = CHSPEC_BANDUNIT(chanspec);
		if (wlc_hw->band->bandunit != bandunit) {
			/* brcms_b_setband disables other bandunit,
			 *  use light band switch if not up yet
			 */
			if (wlc_hw->up) {
				wlc_phy_chanspec_radio_set(wlc_hw->
							   bandstate[bandunit]->
							   pi, chanspec);
				brcms_b_setband(wlc_hw, bandunit, chanspec);
			} else {
				brcms_c_setxband(wlc_hw, bandunit);
			}
		}
	}

	wlc_phy_initcal_enable(wlc_hw->band->pi, !mute);

	if (!wlc_hw->up) {
		if (wlc_hw->clk)
			wlc_phy_txpower_limit_set(wlc_hw->band->pi, txpwr,
						  chanspec);
		wlc_phy_chanspec_radio_set(wlc_hw->band->pi, chanspec);
	} else {
		wlc_phy_chanspec_set(wlc_hw->band->pi, chanspec);
		wlc_phy_txpower_limit_set(wlc_hw->band->pi, txpwr, chanspec);

		/* Update muting of the channel */
		brcms_b_mute(wlc_hw, mute, 0);
	}
}

void brcms_c_set_chanspec(struct brcms_c_info *wlc, u16 chanspec)
{
	uint bandunit;
	bool switchband = false;
	u16 old_chanspec = wlc->chanspec;

	if (!brcms_c_valid_chanspec_db(wlc->cmi, chanspec)) {
		wiphy_err(wlc->wiphy, "wl%d: %s: Bad channel %d\n",
			  wlc->pub->unit, __func__, CHSPEC_CHANNEL(chanspec));
		return;
	}

	/* Switch bands if necessary */
	if (NBANDS(wlc) > 1) {
		bandunit = CHSPEC_BANDUNIT(chanspec);
		if (wlc->band->bandunit != bandunit || wlc->bandinit_pending) {
			switchband = true;
			if (wlc->bandlocked) {
				wiphy_err(wlc->wiphy, "wl%d: %s: chspec %d "
					  "band is locked!\n",
					  wlc->pub->unit, __func__,
					  CHSPEC_CHANNEL(chanspec));
				return;
			}
			/*
			 * should the setband call come after the
			 * brcms_b_chanspec() ? if the setband updates
			 * (brcms_c_bsinit) use low level calls to inspect and
			 * set state, the state inspected may be from the wrong
			 * band, or the following brcms_b_set_chanspec() may
			 * undo the work.
			 */
			brcms_c_setband(wlc, bandunit);
		}
	}

	/* sync up phy/radio chanspec */
	brcms_c_set_phy_chanspec(wlc, chanspec);

	/* init antenna selection */
	if (CHSPEC_WLC_BW(old_chanspec) != CHSPEC_WLC_BW(chanspec)) {
		brcms_c_antsel_init(wlc->asi);

		/* Fix the hardware rateset based on bw.
		 * Mainly add MCS32 for 40Mhz, remove MCS 32 for 20Mhz
		 */
		brcms_c_rateset_bw_mcs_filter(&wlc->band->hw_rateset,
					  wlc->band->
					  mimo_cap_40 ? CHSPEC_WLC_BW(chanspec)
					  : 0);
	}

	/* update some mac configuration since chanspec changed */
	brcms_c_ucode_mac_upd(wlc);
}

u32 brcms_c_lowest_basic_rspec(struct brcms_c_info *wlc,
				      struct brcms_c_rateset *rs)
{
	u32 lowest_basic_rspec;
	uint i;

	/* Use the lowest basic rate */
	lowest_basic_rspec = rs->rates[0] & BRCMS_RATE_MASK;
	for (i = 0; i < rs->count; i++) {
		if (rs->rates[i] & BRCMS_RATE_FLAG) {
			lowest_basic_rspec = rs->rates[i] & BRCMS_RATE_MASK;
			break;
		}
	}

	/*
	 * pick siso/cdd as default for OFDM (note no basic
	 * rate MCSs are supported yet)
	 */
	if (IS_OFDM(lowest_basic_rspec))
		lowest_basic_rspec |= (wlc->stf->ss_opmode << RSPEC_STF_SHIFT);

	return lowest_basic_rspec;
}

/*
 * This function changes the phytxctl for beacon based on current
 * beacon ratespec AND txant setting as per this table:
 *  ratespec     CCK		ant = wlc->stf->txant
 *		OFDM		ant = 3
 */
void brcms_c_beacon_phytxctl_txant_upd(struct brcms_c_info *wlc,
				       u32 bcn_rspec)
{
	u16 phyctl;
	u16 phytxant = wlc->stf->phytxant;
	u16 mask = PHY_TXC_ANT_MASK;

	/* for non-siso rates or default setting, use the available chains */
	if (BRCMS_PHY_11N_CAP(wlc->band))
		phytxant = brcms_c_stf_phytxchain_sel(wlc, bcn_rspec);

	phyctl = brcms_c_read_shm(wlc, M_BCN_PCTLWD);
	phyctl = (phyctl & ~mask) | phytxant;
	brcms_c_write_shm(wlc, M_BCN_PCTLWD, phyctl);
}

/*
 * centralized protection config change function to simplify debugging, no
 * consistency checking this should be called only on changes to avoid overhead
 * in periodic function
 */
void brcms_c_protection_upd(struct brcms_c_info *wlc, uint idx, int val)
{
	BCMMSG(wlc->wiphy, "idx %d, val %d\n", idx, val);

	switch (idx) {
	case BRCMS_PROT_G_SPEC:
		wlc->protection->_g = (bool) val;
		break;
	case BRCMS_PROT_G_OVR:
		wlc->protection->g_override = (s8) val;
		break;
	case BRCMS_PROT_G_USER:
		wlc->protection->gmode_user = (u8) val;
		break;
	case BRCMS_PROT_OVERLAP:
		wlc->protection->overlap = (s8) val;
		break;
	case BRCMS_PROT_N_USER:
		wlc->protection->nmode_user = (s8) val;
		break;
	case BRCMS_PROT_N_CFG:
		wlc->protection->n_cfg = (s8) val;
		break;
	case BRCMS_PROT_N_CFG_OVR:
		wlc->protection->n_cfg_override = (s8) val;
		break;
	case BRCMS_PROT_N_NONGF:
		wlc->protection->nongf = (bool) val;
		break;
	case BRCMS_PROT_N_NONGF_OVR:
		wlc->protection->nongf_override = (s8) val;
		break;
	case BRCMS_PROT_N_PAM_OVR:
		wlc->protection->n_pam_override = (s8) val;
		break;
	case BRCMS_PROT_N_OBSS:
		wlc->protection->n_obss = (bool) val;
		break;

	default:
		break;
	}

}

static void brcms_c_ht_update_sgi_rx(struct brcms_c_info *wlc, int val)
{
	wlc->ht_cap.cap_info &= ~(IEEE80211_HT_CAP_SGI_20 |
					IEEE80211_HT_CAP_SGI_40);
	wlc->ht_cap.cap_info |= (val & BRCMS_N_SGI_20) ?
					IEEE80211_HT_CAP_SGI_20 : 0;
	wlc->ht_cap.cap_info |= (val & BRCMS_N_SGI_40) ?
					IEEE80211_HT_CAP_SGI_40 : 0;

	if (wlc->pub->up) {
		brcms_c_update_beacon(wlc);
		brcms_c_update_probe_resp(wlc, true);
	}
}

static void brcms_c_ht_update_ldpc(struct brcms_c_info *wlc, s8 val)
{
	wlc->stf->ldpc = val;

	wlc->ht_cap.cap_info &= ~IEEE80211_HT_CAP_LDPC_CODING;
	if (wlc->stf->ldpc != OFF)
		wlc->ht_cap.cap_info |= IEEE80211_HT_CAP_LDPC_CODING;

	if (wlc->pub->up) {
		brcms_c_update_beacon(wlc);
		brcms_c_update_probe_resp(wlc, true);
		wlc_phy_ldpc_override_set(wlc->band->pi, (val ? true : false));
	}
}

/*
 * ucode, hwmac update
 *    Channel dependent updates for ucode and hw
 */
static void brcms_c_ucode_mac_upd(struct brcms_c_info *wlc)
{
	/* enable or disable any active IBSSs depending on whether or not
	 * we are on the home channel
	 */
	if (wlc->home_chanspec == BRCMS_BAND_PI_RADIO_CHANSPEC) {
		if (wlc->pub->associated) {
			/*
			 * BMAC_NOTE: This is something that should be fixed
			 * in ucode inits. I think that the ucode inits set
			 * up the bcn templates and shm values with a bogus
			 * beacon. This should not be done in the inits. If
			 * ucode needs to set up a beacon for testing, the
			 * test routines should write it down, not expect the
			 * inits to populate a bogus beacon.
			 */
			if (BRCMS_PHY_11N_CAP(wlc->band))
				brcms_c_write_shm(wlc, M_BCN_TXTSF_OFFSET,
					      wlc->band->bcntsfoff);
		}
	} else {
		/* disable an active IBSS if we are not on the home channel */
	}

	/* update the various promisc bits */
	brcms_c_mac_bcn_promisc(wlc);
	brcms_c_mac_promisc(wlc);
}

static void brcms_c_bandinit_ordered(struct brcms_c_info *wlc,
				     u16 chanspec)
{
	struct brcms_c_rateset default_rateset;
	uint parkband;
	uint i, band_order[2];

	BCMMSG(wlc->wiphy, "wl%d\n", wlc->pub->unit);
	/*
	 * We might have been bandlocked during down and the chip
	 * power-cycled (hibernate). Figure out the right band to park on
	 */
	if (wlc->bandlocked || NBANDS(wlc) == 1) {
		/* updated in brcms_c_bandlock() */
		parkband = wlc->band->bandunit;
		band_order[0] = band_order[1] = parkband;
	} else {
		/* park on the band of the specified chanspec */
		parkband = CHSPEC_BANDUNIT(chanspec);

		/* order so that parkband initialize last */
		band_order[0] = parkband ^ 1;
		band_order[1] = parkband;
	}

	/* make each band operational, software state init */
	for (i = 0; i < NBANDS(wlc); i++) {
		uint j = band_order[i];

		wlc->band = wlc->bandstate[j];

		brcms_default_rateset(wlc, &default_rateset);

		/* fill in hw_rate */
		brcms_c_rateset_filter(&default_rateset, &wlc->band->hw_rateset,
				   false, BRCMS_RATES_CCK_OFDM, BRCMS_RATE_MASK,
				   (bool) N_ENAB(wlc->pub));

		/* init basic rate lookup */
		brcms_c_rate_lookup_init(wlc, &default_rateset);
	}

	/* sync up phy/radio chanspec */
	brcms_c_set_phy_chanspec(wlc, chanspec);
}

/* band-specific init */
static void brcms_c_bsinit(struct brcms_c_info *wlc)
{
	BCMMSG(wlc->wiphy, "wl%d: bandunit %d\n",
		 wlc->pub->unit, wlc->band->bandunit);

	/* write ucode ACK/CTS rate table */
	brcms_c_set_ratetable(wlc);

	/* update some band specific mac configuration */
	brcms_c_ucode_mac_upd(wlc);

	/* init antenna selection */
	brcms_c_antsel_init(wlc->asi);

}

/* switch to and initialize new band */
static void brcms_c_setband(struct brcms_c_info *wlc,
					   uint bandunit)
{
	int idx;
	struct brcms_bss_cfg *cfg;

	wlc->band = wlc->bandstate[bandunit];

	if (!wlc->pub->up)
		return;

	/* wait for at least one beacon before entering sleeping state */
	for (idx = 0; idx < BRCMS_MAXBSSCFG; idx++) {
		cfg = wlc->bsscfg[idx];
		if (cfg && BSSCFG_STA(cfg) && cfg->associated)
			cfg->PMawakebcn = true;
	}
	brcms_c_set_ps_ctrl(wlc);

	/* band-specific initializations */
	brcms_c_bsinit(wlc);
}

/*
 * Initialize a WME Parameter Info Element with default
 * STA parameters from WMM Spec, Table 12
 */
void
brcms_c_wme_initparams_sta(struct brcms_c_info *wlc, struct wme_param_ie *pe)
{
	static const struct wme_param_ie stadef = {
		WME_OUI,
		WME_TYPE,
		WME_SUBTYPE_PARAM_IE,
		WME_VER,
		0,
		0,
		{
		 {EDCF_AC_BE_ACI_STA, EDCF_AC_BE_ECW_STA,
		  cpu_to_le16(EDCF_AC_BE_TXOP_STA)},
		 {EDCF_AC_BK_ACI_STA, EDCF_AC_BK_ECW_STA,
		  cpu_to_le16(EDCF_AC_BK_TXOP_STA)},
		 {EDCF_AC_VI_ACI_STA, EDCF_AC_VI_ECW_STA,
		  cpu_to_le16(EDCF_AC_VI_TXOP_STA)},
		 {EDCF_AC_VO_ACI_STA, EDCF_AC_VO_ECW_STA,
		  cpu_to_le16(EDCF_AC_VO_TXOP_STA)}
		 }
	};
	memcpy(pe, &stadef, sizeof(*pe));
}

void brcms_c_wme_setparams(struct brcms_c_info *wlc, u16 aci,
		       const struct ieee80211_tx_queue_params *params,
		       bool suspend)
{
	int i;
	struct shm_acparams acp_shm;
	u16 *shm_entry;

	/* Only apply params if the core is out of reset and has clocks */
	if (!wlc->clk) {
		wiphy_err(wlc->wiphy, "wl%d: %s : no-clock\n", wlc->pub->unit,
			  __func__);
		return;
	}

	do {
		memset((char *)&acp_shm, 0, sizeof(struct shm_acparams));
		/* fill in shm ac params struct */
		acp_shm.txop = le16_to_cpu(params->txop);
		/* convert from units of 32us to us for ucode */
		wlc->edcf_txop[aci & 0x3] = acp_shm.txop =
		    EDCF_TXOP2USEC(acp_shm.txop);
		acp_shm.aifs = (params->aifs & EDCF_AIFSN_MASK);

		if (aci == AC_VI && acp_shm.txop == 0
		    && acp_shm.aifs < EDCF_AIFSN_MAX)
			acp_shm.aifs++;

		if (acp_shm.aifs < EDCF_AIFSN_MIN
		    || acp_shm.aifs > EDCF_AIFSN_MAX) {
			wiphy_err(wlc->wiphy, "wl%d: edcf_setparams: bad "
				  "aifs %d\n", wlc->pub->unit, acp_shm.aifs);
			continue;
		}

		acp_shm.cwmin = params->cw_min;
		acp_shm.cwmax = params->cw_max;
		acp_shm.cwcur = acp_shm.cwmin;
		acp_shm.bslots =
		    R_REG(&wlc->regs->tsf_random) & acp_shm.cwcur;
		acp_shm.reggap = acp_shm.bslots + acp_shm.aifs;
		/* Indicate the new params to the ucode */
		acp_shm.status = brcms_c_read_shm(wlc, (M_EDCF_QINFO +
						    wme_shmemacindex(aci) *
						    M_EDCF_QLEN +
						    M_EDCF_STATUS_OFF));
		acp_shm.status |= WME_STATUS_NEWAC;

		/* Fill in shm acparam table */
		shm_entry = (u16 *) &acp_shm;
		for (i = 0; i < (int)sizeof(struct shm_acparams); i += 2)
			brcms_c_write_shm(wlc,
				      M_EDCF_QINFO +
				      wme_shmemacindex(aci) * M_EDCF_QLEN + i,
				      *shm_entry++);

	} while (0);

	if (suspend)
		brcms_c_suspend_mac_and_wait(wlc);

	if (suspend)
		brcms_c_enable_mac(wlc);

}

void brcms_c_edcf_setparams(struct brcms_c_info *wlc, bool suspend)
{
	u16 aci;
	int i_ac;
	struct edcf_acparam *edcf_acp;

	struct ieee80211_tx_queue_params txq_pars;
	struct ieee80211_tx_queue_params *params = &txq_pars;

	/*
	 * AP uses AC params from wme_param_ie_ap.
	 * AP advertises AC params from wme_param_ie.
	 * STA uses AC params from wme_param_ie.
	 */

	edcf_acp = (struct edcf_acparam *) &wlc->wme_param_ie.acparam[0];

	for (i_ac = 0; i_ac < AC_COUNT; i_ac++, edcf_acp++) {
		/* find out which ac this set of params applies to */
		aci = (edcf_acp->ACI & EDCF_ACI_MASK) >> EDCF_ACI_SHIFT;

		/* fill in shm ac params struct */
		params->txop = edcf_acp->TXOP;
		params->aifs = edcf_acp->ACI;

		/* CWmin = 2^(ECWmin) - 1 */
		params->cw_min = EDCF_ECW2CW(edcf_acp->ECW & EDCF_ECWMIN_MASK);
		/* CWmax = 2^(ECWmax) - 1 */
		params->cw_max = EDCF_ECW2CW((edcf_acp->ECW & EDCF_ECWMAX_MASK)
					    >> EDCF_ECWMAX_SHIFT);
		brcms_c_wme_setparams(wlc, aci, params, suspend);
	}

	if (suspend)
		brcms_c_suspend_mac_and_wait(wlc);

	if (AP_ENAB(wlc->pub) && WME_ENAB(wlc->pub)) {
		brcms_c_update_beacon(wlc);
		brcms_c_update_probe_resp(wlc, false);
	}

	if (suspend)
		brcms_c_enable_mac(wlc);

}

bool brcms_c_timers_init(struct brcms_c_info *wlc, int unit)
{
	wlc->wdtimer = brcms_init_timer(wlc->wl, brcms_c_watchdog_by_timer,
		wlc, "watchdog");
	if (!wlc->wdtimer) {
		wiphy_err(wlc->wiphy, "wl%d:  wl_init_timer for wdtimer "
			  "failed\n", unit);
		goto fail;
	}

	wlc->radio_timer = brcms_init_timer(wlc->wl, brcms_c_radio_timer,
		wlc, "radio");
	if (!wlc->radio_timer) {
		wiphy_err(wlc->wiphy, "wl%d:  wl_init_timer for radio_timer "
			  "failed\n", unit);
		goto fail;
	}

	return true;

 fail:
	return false;
}

/*
 * Initialize brcms_c_info default values ...
 * may get overrides later in this function
 */
void brcms_c_info_init(struct brcms_c_info *wlc, int unit)
{
	int i;
	/* Assume the device is there until proven otherwise */
	wlc->device_present = true;

	/* Save our copy of the chanspec */
	wlc->chanspec = CH20MHZ_CHSPEC(1);

	/* various 802.11g modes */
	wlc->shortslot = false;
	wlc->shortslot_override = BRCMS_SHORTSLOT_AUTO;

	brcms_c_protection_upd(wlc, BRCMS_PROT_G_OVR, BRCMS_PROTECTION_AUTO);
	brcms_c_protection_upd(wlc, BRCMS_PROT_G_SPEC, false);

	brcms_c_protection_upd(wlc, BRCMS_PROT_N_CFG_OVR,
			       BRCMS_PROTECTION_AUTO);
	brcms_c_protection_upd(wlc, BRCMS_PROT_N_CFG, BRCMS_N_PROTECTION_OFF);
	brcms_c_protection_upd(wlc, BRCMS_PROT_N_NONGF_OVR,
			       BRCMS_PROTECTION_AUTO);
	brcms_c_protection_upd(wlc, BRCMS_PROT_N_NONGF, false);
	brcms_c_protection_upd(wlc, BRCMS_PROT_N_PAM_OVR, AUTO);

	brcms_c_protection_upd(wlc, BRCMS_PROT_OVERLAP,
			       BRCMS_PROTECTION_CTL_OVERLAP);

	/* 802.11g draft 4.0 NonERP elt advertisement */
	wlc->include_legacy_erp = true;

	wlc->stf->ant_rx_ovr = ANT_RX_DIV_DEF;
	wlc->stf->txant = ANT_TX_DEF;

	wlc->prb_resp_timeout = BRCMS_PRB_RESP_TIMEOUT;

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

	/* Set flag to indicate that hw keys should be used when available. */
	wlc->wsec_swkeys = false;

	/* init the 4 static WEP default keys */
	for (i = 0; i < WSEC_MAX_DEFAULT_KEYS; i++) {
		wlc->wsec_keys[i] = wlc->wsec_def_keys[i];
		wlc->wsec_keys[i]->idx = (u8) i;
	}

	/* WME QoS mode is Auto by default */
	wlc->pub->_wme = AUTO;
	wlc->pub->_ampdu = AMPDU_AGG_HOST;
	wlc->pub->bcmerror = 0;
	wlc->pub->_coex = ON;

	/* initialize mpc delay */
	wlc->mpc_delay_off = wlc->mpc_dlycnt = BRCMS_MPC_MIN_DELAYCNT;
}

static bool brcms_c_state_bmac_sync(struct brcms_c_info *wlc)
{
	struct brcms_b_state state_bmac = {0};

	if (brcms_b_state_get(wlc->hw, &state_bmac) != 0)
		return false;

	wlc->machwcap = state_bmac.machwcap;
	brcms_c_protection_upd(wlc, BRCMS_PROT_N_PAM_OVR,
			   (s8) state_bmac.preamble_ovr);

	return true;
}

static uint brcms_c_attach_module(struct brcms_c_info *wlc)
{
	uint err = 0;
	uint unit;
	unit = wlc->pub->unit;

	wlc->asi = brcms_c_antsel_attach(wlc);
	if (wlc->asi == NULL) {
		wiphy_err(wlc->wiphy, "wl%d: attach: antsel_attach "
			  "failed\n", unit);
		err = 44;
		goto fail;
	}

	wlc->ampdu = brcms_c_ampdu_attach(wlc);
	if (wlc->ampdu == NULL) {
		wiphy_err(wlc->wiphy, "wl%d: attach: ampdu_attach "
			  "failed\n", unit);
		err = 50;
		goto fail;
	}

	if ((brcms_c_stf_attach(wlc) != 0)) {
		wiphy_err(wlc->wiphy, "wl%d: attach: stf_attach "
			  "failed\n", unit);
		err = 68;
		goto fail;
	}
 fail:
	return err;
}

struct brcms_pub *brcms_c_pub(struct brcms_c_info *wlc)
{
	return wlc->pub;
}

#define CHIP_SUPPORTS_11N(wlc)	1

/* low level attach
 *    run backplane attach, init nvram
 *    run phy attach
 *    initialize software state for each core and band
 *    put the whole chip in reset(driver down state), no clock
 */
int brcms_b_attach(struct brcms_c_info *wlc, u16 vendor, u16 device, uint unit,
		   bool piomode, void *regsva, struct pci_dev *btparam)
{
	struct brcms_hardware *wlc_hw;
	struct d11regs *regs;
	char *macaddr = NULL;
	char *vars;
	uint err = 0;
	uint j;
	bool wme = false;
	struct shared_phy_params sha_params;
	struct wiphy *wiphy = wlc->wiphy;
	char *var;
	unsigned long res;

	BCMMSG(wlc->wiphy, "wl%d: vendor 0x%x device 0x%x\n", unit, vendor,
		device);

	wme = true;

	wlc_hw = wlc->hw;
	wlc_hw->wlc = wlc;
	wlc_hw->unit = unit;
	wlc_hw->band = wlc_hw->bandstate[0];
	wlc_hw->_piomode = piomode;

	/* populate struct brcms_hardware with default values  */
	brcms_b_info_init(wlc_hw);

	/*
	 * Do the hardware portion of the attach. Also initialize software
	 * state that depends on the particular hardware we are running.
	 */
	wlc_hw->sih = ai_attach(regsva, btparam,
				&wlc_hw->vars, &wlc_hw->vars_size);
	if (wlc_hw->sih == NULL) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: si_attach failed\n",
			  unit);
		err = 11;
		goto fail;
	}
	vars = wlc_hw->vars;

	/*
	 * Get vendid/devid nvram overwrites, which could be different
	 * than those the BIOS recognizes for devices on PCMCIA_BUS,
	 * SDIO_BUS, and SROMless devices on PCI_BUS.
	 */
	var = getvar(vars, "vendid");
	if (var && !kstrtoul(var, 0, &res)) {
		vendor = (u16)res;
		wiphy_err(wiphy, "Overriding vendor id = 0x%x\n",
			  vendor);
	}
	var = getvar(vars, "devid");
	if (var && !kstrtoul(var, 0, &res)) {
		u16 devid = (u16)res;
		if (devid != 0xffff) {
			device = devid;
			wiphy_err(wiphy, "Overriding device id = 0x%x"
				  "\n", device);
		}
	}

	/* verify again the device is supported */
	if (!brcms_c_chipmatch(vendor, device)) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: Unsupported "
			"vendor/device (0x%x/0x%x)\n",
			 unit, vendor, device);
		err = 12;
		goto fail;
	}

	wlc_hw->vendorid = vendor;
	wlc_hw->deviceid = device;

	/* set bar0 window to point at D11 core */
	wlc_hw->regs = (struct d11regs *) ai_setcore(wlc_hw->sih, D11_CORE_ID,
						     0);
	wlc_hw->corerev = ai_corerev(wlc_hw->sih);

	regs = wlc_hw->regs;

	wlc->regs = wlc_hw->regs;

	/* validate chip, chiprev and corerev */
	if (!brcms_c_isgoodchip(wlc_hw)) {
		err = 13;
		goto fail;
	}

	/* initialize power control registers */
	ai_clkctl_init(wlc_hw->sih);

	/* request fastclock and force fastclock for the rest of attach
	 * bring the d11 core out of reset.
	 *   For PMU chips, the first wlc_clkctl_clk is no-op since core-clk
	 *   is still false; But it will be called again inside wlc_corereset,
	 *   after d11 is out of reset.
	 */
	brcms_b_clkctl_clk(wlc_hw, CLK_FAST);
	brcms_b_corereset(wlc_hw, BRCMS_USE_COREFLAGS);

	if (!brcms_b_validate_chip_access(wlc_hw)) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: validate_chip_access "
			"failed\n", unit);
		err = 14;
		goto fail;
	}

	/* get the board rev, used just below */
	j = getintvar(vars, "boardrev");
	/* promote srom boardrev of 0xFF to 1 */
	if (j == BOARDREV_PROMOTABLE)
		j = BOARDREV_PROMOTED;
	wlc_hw->boardrev = (u16) j;
	if (!brcms_c_validboardtype(wlc_hw)) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: Unsupported Broadcom "
			"board type (0x%x)" " or revision level (0x%x)\n",
			 unit, wlc_hw->sih->boardtype, wlc_hw->boardrev);
		err = 15;
		goto fail;
	}
	wlc_hw->sromrev = (u8) getintvar(vars, "sromrev");
	wlc_hw->boardflags = (u32) getintvar(vars, "boardflags");
	wlc_hw->boardflags2 = (u32) getintvar(vars, "boardflags2");

	if (wlc_hw->boardflags & BFL_NOPLLDOWN)
		brcms_b_pllreq(wlc_hw, true, BRCMS_PLLREQ_SHARED);

	if (ai_pci_war16165(wlc_hw->sih))
		wlc->war16165 = true;

	/* check device id(srom, nvram etc.) to set bands */
	if (wlc_hw->deviceid == BCM43224_D11N_ID ||
	    wlc_hw->deviceid == BCM43224_D11N_ID_VEN1)
		/* Dualband boards */
		wlc_hw->_nbands = 2;
	else
		wlc_hw->_nbands = 1;

	if ((wlc_hw->sih->chip == BCM43225_CHIP_ID))
		wlc_hw->_nbands = 1;

	/* BMAC_NOTE: remove init of pub values when brcms_c_attach()
	 * unconditionally does the init of these values
	 */
	wlc->vendorid = wlc_hw->vendorid;
	wlc->deviceid = wlc_hw->deviceid;
	wlc->pub->sih = wlc_hw->sih;
	wlc->pub->corerev = wlc_hw->corerev;
	wlc->pub->sromrev = wlc_hw->sromrev;
	wlc->pub->boardrev = wlc_hw->boardrev;
	wlc->pub->boardflags = wlc_hw->boardflags;
	wlc->pub->boardflags2 = wlc_hw->boardflags2;
	wlc->pub->_nbands = wlc_hw->_nbands;

	wlc_hw->physhim = wlc_phy_shim_attach(wlc_hw, wlc->wl, wlc);

	if (wlc_hw->physhim == NULL) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: wlc_phy_shim_attach "
			"failed\n", unit);
		err = 25;
		goto fail;
	}

	/* pass all the parameters to wlc_phy_shared_attach in one struct */
	sha_params.sih = wlc_hw->sih;
	sha_params.physhim = wlc_hw->physhim;
	sha_params.unit = unit;
	sha_params.corerev = wlc_hw->corerev;
	sha_params.vars = vars;
	sha_params.vid = wlc_hw->vendorid;
	sha_params.did = wlc_hw->deviceid;
	sha_params.chip = wlc_hw->sih->chip;
	sha_params.chiprev = wlc_hw->sih->chiprev;
	sha_params.chippkg = wlc_hw->sih->chippkg;
	sha_params.sromrev = wlc_hw->sromrev;
	sha_params.boardtype = wlc_hw->sih->boardtype;
	sha_params.boardrev = wlc_hw->boardrev;
	sha_params.boardvendor = wlc_hw->sih->boardvendor;
	sha_params.boardflags = wlc_hw->boardflags;
	sha_params.boardflags2 = wlc_hw->boardflags2;
	sha_params.buscorerev = wlc_hw->sih->buscorerev;

	/* alloc and save pointer to shared phy state area */
	wlc_hw->phy_sh = wlc_phy_shared_attach(&sha_params);
	if (!wlc_hw->phy_sh) {
		err = 16;
		goto fail;
	}

	/* initialize software state for each core and band */
	for (j = 0; j < NBANDS_HW(wlc_hw); j++) {
		/*
		 * band0 is always 2.4Ghz
		 * band1, if present, is 5Ghz
		 */

		/* So if this is a single band 11a card, use band 1 */
		if (IS_SINGLEBAND_5G(wlc_hw->deviceid))
			j = BAND_5G_INDEX;

		brcms_c_setxband(wlc_hw, j);

		wlc_hw->band->bandunit = j;
		wlc_hw->band->bandtype = j ? BRCM_BAND_5G : BRCM_BAND_2G;
		wlc->band->bandunit = j;
		wlc->band->bandtype = j ? BRCM_BAND_5G : BRCM_BAND_2G;
		wlc->core->coreidx = ai_coreidx(wlc_hw->sih);

		wlc_hw->machwcap = R_REG(&regs->machwcap);
		wlc_hw->machwcap_backup = wlc_hw->machwcap;

		/* init tx fifo size */
		wlc_hw->xmtfifo_sz =
		    xmtfifo_sz[(wlc_hw->corerev - XMTFIFOTBL_STARTREV)];

		/* Get a phy for this band */
		wlc_hw->band->pi =
			wlc_phy_attach(wlc_hw->phy_sh, regs,
				       brcms_b_bandtype(wlc_hw), vars,
				       wlc->wiphy);
		if (wlc_hw->band->pi == NULL) {
			wiphy_err(wiphy, "wl%d: brcms_b_attach: wlc_phy_"
				  "attach failed\n", unit);
			err = 17;
			goto fail;
		}

		wlc_phy_machwcap_set(wlc_hw->band->pi, wlc_hw->machwcap);

		wlc_phy_get_phyversion(wlc_hw->band->pi, &wlc_hw->band->phytype,
				       &wlc_hw->band->phyrev,
				       &wlc_hw->band->radioid,
				       &wlc_hw->band->radiorev);
		wlc_hw->band->abgphy_encore =
		    wlc_phy_get_encore(wlc_hw->band->pi);
		wlc->band->abgphy_encore = wlc_phy_get_encore(wlc_hw->band->pi);
		wlc_hw->band->core_flags =
		    wlc_phy_get_coreflags(wlc_hw->band->pi);

		/* verify good phy_type & supported phy revision */
		if (BRCMS_ISNPHY(wlc_hw->band)) {
			if (NCONF_HAS(wlc_hw->band->phyrev))
				goto good_phy;
			else
				goto bad_phy;
		} else if (BRCMS_ISLCNPHY(wlc_hw->band)) {
			if (LCNCONF_HAS(wlc_hw->band->phyrev))
				goto good_phy;
			else
				goto bad_phy;
		} else {
 bad_phy:
			wiphy_err(wiphy, "wl%d: brcms_b_attach: unsupported "
				  "phy type/rev (%d/%d)\n", unit,
				  wlc_hw->band->phytype, wlc_hw->band->phyrev);
			err = 18;
			goto fail;
		}

 good_phy:
		/*
		 * BMAC_NOTE: wlc->band->pi should not be set below and should
		 * be done in the high level attach. However we can not make
		 * that change until all low level access is changed to
		 * wlc_hw->band->pi. Instead do the wlc->band->pi init below,
		 * keeping wlc_hw->band->pi as well for incremental update of
		 * low level fns, and cut over low only init when all fns
		 * updated.
		 */
		wlc->band->pi = wlc_hw->band->pi;
		wlc->band->phytype = wlc_hw->band->phytype;
		wlc->band->phyrev = wlc_hw->band->phyrev;
		wlc->band->radioid = wlc_hw->band->radioid;
		wlc->band->radiorev = wlc_hw->band->radiorev;

		/* default contention windows size limits */
		wlc_hw->band->CWmin = APHY_CWMIN;
		wlc_hw->band->CWmax = PHY_CWMAX;

		if (!brcms_b_attach_dmapio(wlc, j, wme)) {
			err = 19;
			goto fail;
		}
	}

	/* disable core to match driver "down" state */
	brcms_c_coredisable(wlc_hw);

	/* Match driver "down" state */
	ai_pci_down(wlc_hw->sih);

	/* register sb interrupt callback functions */
	ai_register_intr_callback(wlc_hw->sih, (void *)brcms_c_wlintrsoff,
				  (void *)brcms_c_wlintrsrestore, NULL, wlc);

	/* turn off pll and xtal to match driver "down" state */
	brcms_b_xtal(wlc_hw, OFF);

	/* *******************************************************************
	 * The hardware is in the DOWN state at this point. D11 core
	 * or cores are in reset with clocks off, and the board PLLs
	 * are off if possible.
	 *
	 * Beyond this point, wlc->sbclk == false and chip registers
	 * should not be touched.
	 *********************************************************************
	 */

	/* init etheraddr state variables */
	macaddr = brcms_c_get_macaddr(wlc_hw);
	if (macaddr == NULL) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: macaddr not found\n",
			  unit);
		err = 21;
		goto fail;
	}
	if (!mac_pton(macaddr, wlc_hw->etheraddr) ||
	    is_broadcast_ether_addr(wlc_hw->etheraddr) ||
	    is_zero_ether_addr(wlc_hw->etheraddr)) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: bad macaddr %s\n",
			  unit, macaddr);
		err = 22;
		goto fail;
	}

	BCMMSG(wlc->wiphy,
		 "deviceid 0x%x nbands %d board 0x%x macaddr: %s\n",
		 wlc_hw->deviceid, wlc_hw->_nbands,
		 wlc_hw->sih->boardtype, macaddr);

	return err;

 fail:
	wiphy_err(wiphy, "wl%d: brcms_b_attach: failed with err %d\n", unit,
		  err);
	return err;
}

/*
 * The common driver entry routine. Error codes should be unique
 */
struct brcms_c_info *
brcms_c_attach(struct brcms_info *wl, u16 vendor, u16 device, uint unit,
	       bool piomode, void *regsva, struct pci_dev *btparam, uint *perr)
{
	struct brcms_c_info *wlc;
	uint err = 0;
	uint j;
	struct brcms_pub *pub;
	uint n_disabled;

	/* allocate struct brcms_c_info state and its substructures */
	wlc = (struct brcms_c_info *) brcms_c_attach_malloc(unit, &err, device);
	if (wlc == NULL)
		goto fail;
	wlc->wiphy = wl->wiphy;
	pub = wlc->pub;

#if defined(BCMDBG)
	wlc_info_dbg = wlc;
#endif

	wlc->band = wlc->bandstate[0];
	wlc->core = wlc->corestate;
	wlc->wl = wl;
	pub->unit = unit;
	pub->_piomode = piomode;
	wlc->bandinit_pending = false;

	/* populate struct brcms_c_info with default values  */
	brcms_c_info_init(wlc, unit);

	/* update sta/ap related parameters */
	brcms_c_ap_upd(wlc);

	/* 11n_disable nvram */
	n_disabled = getintvar(pub->vars, "11n_disable");

	/*
	 * low level attach steps(all hw accesses go
	 * inside, no more in rest of the attach)
	 */
	err = brcms_b_attach(wlc, vendor, device, unit, piomode, regsva,
			     btparam);
	if (err)
		goto fail;

	/*
	 * for some states, due to different info pointer(e,g, wlc, wlc_hw) or
	 * master/slave split, HIGH driver(both monolithic and HIGH_ONLY) needs
	 * to sync states FROM BMAC portion driver
	 */
	if (!brcms_c_state_bmac_sync(wlc)) {
		err = 20;
		goto fail;
	}

	pub->phy_11ncapable = BRCMS_PHY_11N_CAP(wlc->band);

	/* propagate *vars* from BMAC driver to high driver */
	brcms_b_copyfrom_vars(wlc->hw, &pub->vars, &wlc->vars_size);


	/* set maximum allowed duty cycle */
	wlc->tx_duty_cycle_ofdm =
	    (u16) getintvar(pub->vars, "tx_duty_cycle_ofdm");
	wlc->tx_duty_cycle_cck =
	    (u16) getintvar(pub->vars, "tx_duty_cycle_cck");

	brcms_c_stf_phy_chain_calc(wlc);

	/* txchain 1: txant 0, txchain 2: txant 1 */
	if (BRCMS_ISNPHY(wlc->band) && (wlc->stf->txstreams == 1))
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

	brcms_b_hw_etheraddr(wlc->hw, wlc->perm_etheraddr);

	memcpy(&pub->cur_etheraddr, &wlc->perm_etheraddr, ETH_ALEN);

	for (j = 0; j < NBANDS(wlc); j++) {
		/* Use band 1 for single band 11a */
		if (IS_SINGLEBAND_5G(wlc->deviceid))
			j = BAND_5G_INDEX;

		wlc->band = wlc->bandstate[j];

		if (!brcms_c_attach_stf_ant_init(wlc)) {
			err = 24;
			goto fail;
		}

		/* default contention windows size limits */
		wlc->band->CWmin = APHY_CWMIN;
		wlc->band->CWmax = PHY_CWMAX;

		/* init gmode value */
		if (BAND_2G(wlc->band->bandtype)) {
			wlc->band->gmode = GMODE_AUTO;
			brcms_c_protection_upd(wlc, BRCMS_PROT_G_USER,
					   wlc->band->gmode);
		}

		/* init _n_enab supported mode */
		if (BRCMS_PHY_11N_CAP(wlc->band) && CHIP_SUPPORTS_11N(wlc)) {
			if (n_disabled & WLFEATURE_DISABLE_11N) {
				pub->_n_enab = OFF;
				brcms_c_protection_upd(wlc, BRCMS_PROT_N_USER,
						       OFF);
			} else {
				pub->_n_enab = SUPPORT_11N;
				brcms_c_protection_upd(wlc, BRCMS_PROT_N_USER,
						   ((pub->_n_enab ==
						     SUPPORT_11N) ? WL_11N_2x2 :
						    WL_11N_3x3));
			}
		}

		/* init per-band default rateset, depend on band->gmode */
		brcms_default_rateset(wlc, &wlc->band->defrateset);

		/* fill in hw_rateset (used early by BRCM_SET_RATESET) */
		brcms_c_rateset_filter(&wlc->band->defrateset,
				   &wlc->band->hw_rateset, false,
				   BRCMS_RATES_CCK_OFDM, BRCMS_RATE_MASK,
				   (bool) N_ENAB(wlc->pub));
	}

	/*
	 * update antenna config due to
	 * wlc->stf->txant/txchain/ant_rx_ovr change
	 */
	brcms_c_stf_phy_txant_upd(wlc);

	/* attach each modules */
	err = brcms_c_attach_module(wlc);
	if (err != 0)
		goto fail;

	if (!brcms_c_timers_init(wlc, unit)) {
		wiphy_err(wl->wiphy, "wl%d: %s: init_timer failed\n", unit,
			  __func__);
		err = 32;
		goto fail;
	}

	/* depend on rateset, gmode */
	wlc->cmi = brcms_c_channel_mgr_attach(wlc);
	if (!wlc->cmi) {
		wiphy_err(wl->wiphy, "wl%d: %s: channel_mgr_attach failed"
			  "\n", unit, __func__);
		err = 33;
		goto fail;
	}

	/* init default when all parameters are ready, i.e. ->rateset */
	brcms_c_bss_default_init(wlc);

	/*
	 * Complete the wlc default state initializations..
	 */

	/* allocate our initial queue */
	wlc->pkt_queue = brcms_c_txq_alloc(wlc);
	if (wlc->pkt_queue == NULL) {
		wiphy_err(wl->wiphy, "wl%d: %s: failed to malloc tx queue\n",
			  unit, __func__);
		err = 100;
		goto fail;
	}

	wlc->bsscfg[0] = wlc->cfg;
	wlc->cfg->_idx = 0;
	wlc->cfg->wlc = wlc;
	pub->txmaxpkts = MAXTXPKTS;

	brcms_c_wme_initparams_sta(wlc, &wlc->wme_param_ie);

	wlc->mimoft = FT_HT;
	wlc->ht_cap.cap_info = HT_CAP;
	if (HT_ENAB(wlc->pub))
		wlc->stf->ldpc = AUTO;

	wlc->mimo_40txbw = AUTO;
	wlc->ofdm_40txbw = AUTO;
	wlc->cck_40txbw = AUTO;
	brcms_c_update_mimo_band_bwcap(wlc, BRCMS_N_BW_20IN2G_40IN5G);

	/* Set default values of SGI */
	if (BRCMS_SGI_CAP_PHY(wlc)) {
		brcms_c_ht_update_sgi_rx(wlc, (BRCMS_N_SGI_20 |
					       BRCMS_N_SGI_40));
		wlc->sgi_tx = AUTO;
	} else if (BRCMS_ISSSLPNPHY(wlc->band)) {
		brcms_c_ht_update_sgi_rx(wlc, (BRCMS_N_SGI_20 |
					       BRCMS_N_SGI_40));
		wlc->sgi_tx = AUTO;
	} else {
		brcms_c_ht_update_sgi_rx(wlc, 0);
		wlc->sgi_tx = OFF;
	}

	/* *******nvram 11n config overrides Start ********* */

	/* apply the sgi override from nvram conf */
	if (n_disabled & WLFEATURE_DISABLE_11N_SGI_TX)
		wlc->sgi_tx = OFF;

	if (n_disabled & WLFEATURE_DISABLE_11N_SGI_RX)
		brcms_c_ht_update_sgi_rx(wlc, 0);

	/* apply the stbc override from nvram conf */
	if (n_disabled & WLFEATURE_DISABLE_11N_STBC_TX) {
		wlc->bandstate[BAND_2G_INDEX]->band_stf_stbc_tx = OFF;
		wlc->bandstate[BAND_5G_INDEX]->band_stf_stbc_tx = OFF;
		wlc->ht_cap.cap_info &= ~IEEE80211_HT_CAP_TX_STBC;
	}
	if (n_disabled & WLFEATURE_DISABLE_11N_STBC_RX)
		brcms_c_stf_stbc_rx_set(wlc, HT_CAP_RX_STBC_NO);

	/* apply the GF override from nvram conf */
	if (n_disabled & WLFEATURE_DISABLE_11N_GF)
		wlc->ht_cap.cap_info &= ~IEEE80211_HT_CAP_GRN_FLD;

	/* initialize radio_mpc_disable according to wlc->mpc */
	brcms_c_radio_mpc_upd(wlc);
	brcms_b_antsel_set(wlc->hw, wlc->asi->antsel_avail);

	if (perr)
		*perr = 0;

	return wlc;

 fail:
	wiphy_err(wl->wiphy, "wl%d: %s: failed with err %d\n",
		  unit, __func__, err);
	if (wlc)
		brcms_c_detach(wlc);

	if (perr)
		*perr = err;
	return NULL;
}

static void brcms_c_attach_antgain_init(struct brcms_c_info *wlc)
{
	uint unit;
	unit = wlc->pub->unit;

	if ((wlc->band->antgain == -1) && (wlc->pub->sromrev == 1)) {
		/* default antenna gain for srom rev 1 is 2 dBm (8 qdbm) */
		wlc->band->antgain = 8;
	} else if (wlc->band->antgain == -1) {
		wiphy_err(wlc->wiphy, "wl%d: %s: Invalid antennas available in"
			  " srom, using 2dB\n", unit, __func__);
		wlc->band->antgain = 8;
	} else {
		s8 gain, fract;
		/* Older sroms specified gain in whole dbm only.  In order
		 * be able to specify qdbm granularity and remain backward
		 * compatible the whole dbms are now encoded in only
		 * low 6 bits and remaining qdbms are encoded in the hi 2 bits.
		 * 6 bit signed number ranges from -32 - 31.
		 *
		 * Examples:
		 * 0x1 = 1 db,
		 * 0xc1 = 1.75 db (1 + 3 quarters),
		 * 0x3f = -1 (-1 + 0 quarters),
		 * 0x7f = -.75 (-1 + 1 quarters) = -3 qdbm.
		 * 0xbf = -.50 (-1 + 2 quarters) = -2 qdbm.
		 */
		gain = wlc->band->antgain & 0x3f;
		gain <<= 2;	/* Sign extend */
		gain >>= 2;
		fract = (wlc->band->antgain & 0xc0) >> 6;
		wlc->band->antgain = 4 * gain + fract;
	}
}

static bool brcms_c_attach_stf_ant_init(struct brcms_c_info *wlc)
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
		wiphy_err(wlc->wiphy, "wl%d: %s: Invalid antennas available in"
			  " srom (0x%x), using 3\n", unit, __func__, aa);
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
	brcms_c_attach_antgain_init(wlc);

	return true;
}


static void brcms_c_timers_deinit(struct brcms_c_info *wlc)
{
	/* free timer state */
	if (wlc->wdtimer) {
		brcms_free_timer(wlc->wl, wlc->wdtimer);
		wlc->wdtimer = NULL;
	}
	if (wlc->radio_timer) {
		brcms_free_timer(wlc->wl, wlc->radio_timer);
		wlc->radio_timer = NULL;
	}
}

static void brcms_c_detach_module(struct brcms_c_info *wlc)
{
	if (wlc->asi) {
		brcms_c_antsel_detach(wlc->asi);
		wlc->asi = NULL;
	}

	if (wlc->ampdu) {
		brcms_c_ampdu_detach(wlc->ampdu);
		wlc->ampdu = NULL;
	}

	brcms_c_stf_detach(wlc);
}

/*
 * low level detach
 */
int brcms_b_detach(struct brcms_c_info *wlc)
{
	uint i;
	struct brcms_hw_band *band;
	struct brcms_hardware *wlc_hw = wlc->hw;
	int callbacks;

	callbacks = 0;

	if (wlc_hw->sih) {
		/*
		 * detach interrupt sync mechanism since interrupt is disabled
		 * and per-port interrupt object may has been freed. this must
		 * be done before sb core switch
		 */
		ai_deregister_intr_callback(wlc_hw->sih);
		ai_pci_sleep(wlc_hw->sih);
	}

	brcms_b_detach_dmapio(wlc_hw);

	band = wlc_hw->band;
	for (i = 0; i < NBANDS_HW(wlc_hw); i++) {
		if (band->pi) {
			/* Detach this band's phy */
			wlc_phy_detach(band->pi);
			band->pi = NULL;
		}
		band = wlc_hw->bandstate[OTHERBANDUNIT(wlc)];
	}

	/* Free shared phy state */
	kfree(wlc_hw->phy_sh);

	wlc_phy_shim_detach(wlc_hw->physhim);

	/* free vars */
	kfree(wlc_hw->vars);
	wlc_hw->vars = NULL;

	if (wlc_hw->sih) {
		ai_detach(wlc_hw->sih);
		wlc_hw->sih = NULL;
	}

	return callbacks;

}

/*
 * Return a count of the number of driver callbacks still pending.
 *
 * General policy is that brcms_c_detach can only dealloc/free software states.
 * It can NOT touch hardware registers since the d11core may be in reset and
 * clock may not be available.
 * One exception is sb register access, which is possible if crystal is turned
 * on after "down" state, driver should avoid software timer with the exception
 * of radio_monitor.
 */
uint brcms_c_detach(struct brcms_c_info *wlc)
{
	uint callbacks = 0;

	if (wlc == NULL)
		return 0;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc->pub->unit);

	callbacks += brcms_b_detach(wlc);

	/* delete software timers */
	if (!brcms_c_radio_monitor_stop(wlc))
		callbacks++;

	brcms_c_channel_mgr_detach(wlc->cmi);

	brcms_c_timers_deinit(wlc);

	brcms_c_detach_module(wlc);


	while (wlc->tx_queues != NULL)
		brcms_c_txq_free(wlc, wlc->tx_queues);

	brcms_c_detach_mfree(wlc);
	return callbacks;
}

/* update state that depends on the current value of "ap" */
void brcms_c_ap_upd(struct brcms_c_info *wlc)
{
	if (AP_ENAB(wlc->pub))
		/* AP: short not allowed, but not enforced */
		wlc->PLCPHdr_override = BRCMS_PLCP_AUTO;
	else
		/* STA-BSS; short capable */
		wlc->PLCPHdr_override = BRCMS_PLCP_SHORT;

	/* fixup mpc */
	wlc->mpc = true;
}

/* read hwdisable state and propagate to wlc flag */
static void brcms_c_radio_hwdisable_upd(struct brcms_c_info *wlc)
{
	if (wlc->pub->wlfeatureflag & WL_SWFL_NOHWRADIO || wlc->pub->hw_off)
		return;

	if (brcms_b_radio_read_hwdisabled(wlc->hw))
		mboolset(wlc->pub->radio_disabled, WL_RADIO_HW_DISABLE);
	else
		mboolclr(wlc->pub->radio_disabled, WL_RADIO_HW_DISABLE);
}

/*
 * return true if Minimum Power Consumption should
 * be entered, false otherwise
 */
bool brcms_c_is_non_delay_mpc(struct brcms_c_info *wlc)
{
	return false;
}

bool brcms_c_ismpc(struct brcms_c_info *wlc)
{
	return (wlc->mpc_delay_off == 0) && (brcms_c_is_non_delay_mpc(wlc));
}

void brcms_c_radio_mpc_upd(struct brcms_c_info *wlc)
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
		brcms_c_radio_upd(wlc);
		if (!wlc->pub->radio_disabled)
			brcms_c_radio_monitor_stop(wlc);
		return;
	}

	/*
	 * sync ismpc logic with WL_RADIO_MPC_DISABLE bit in
	 * wlc->pub->radio_disabled to go ON, always call radio_upd
	 * synchronously to go OFF, postpone radio_upd to later when
	 * context is safe(e.g. watchdog)
	 */
	radio_state =
	    (mboolisset(wlc->pub->radio_disabled, WL_RADIO_MPC_DISABLE) ? OFF :
	     ON);
	mpc_radio = (brcms_c_ismpc(wlc) == true) ? OFF : ON;

	if (radio_state == ON && mpc_radio == OFF)
		wlc->mpc_delay_off = wlc->mpc_dlycnt;
	else if (radio_state == OFF && mpc_radio == ON) {
		mboolclr(wlc->pub->radio_disabled, WL_RADIO_MPC_DISABLE);
		brcms_c_radio_upd(wlc);
		if (wlc->mpc_offcnt < BRCMS_MPC_THRESHOLD)
			wlc->mpc_dlycnt = BRCMS_MPC_MAX_DELAYCNT;
		else
			wlc->mpc_dlycnt = BRCMS_MPC_MIN_DELAYCNT;
		wlc->mpc_dur += OSL_SYSUPTIME() - wlc->mpc_laston_ts;
	}
	/*
	 * Below logic is meant to capture the transition from mpc off
	 * to mpc on for reasons other than wlc->mpc_delay_off keeping
	 * the mpc off. In that case reset wlc->mpc_delay_off to
	 * wlc->mpc_dlycnt, so that we restart the countdown of mpc_delay_off
	 */
	if ((wlc->prev_non_delay_mpc == false) &&
	    (brcms_c_is_non_delay_mpc(wlc) == true) && wlc->mpc_delay_off)
		wlc->mpc_delay_off = wlc->mpc_dlycnt;

	wlc->prev_non_delay_mpc = brcms_c_is_non_delay_mpc(wlc);
}

/*
 * centralized radio disable/enable function,
 * invoke radio enable/disable after updating hwradio status
 */
static void brcms_c_radio_upd(struct brcms_c_info *wlc)
{
	if (wlc->pub->radio_disabled)
		brcms_c_radio_disable(wlc);
	else
		brcms_c_radio_enable(wlc);
}

/* maintain LED behavior in down state */
static void brcms_c_down_led_upd(struct brcms_c_info *wlc)
{
	/*
	 * maintain LEDs while in down state, turn on sbclk if
	 * not available yet. Turn on sbclk if necessary
	 */
	if (!AP_ENAB(wlc->pub)) {
		brcms_c_pllreq(wlc, true, BRCMS_PLLREQ_FLIP);

		brcms_c_pllreq(wlc, false, BRCMS_PLLREQ_FLIP);
	}
}

/* update hwradio status and return it */
bool brcms_c_check_radio_disabled(struct brcms_c_info *wlc)
{
	brcms_c_radio_hwdisable_upd(wlc);

	return mboolisset(wlc->pub->radio_disabled, WL_RADIO_HW_DISABLE) ?
			true : false;
}

void brcms_c_radio_disable(struct brcms_c_info *wlc)
{
	if (!wlc->pub->up) {
		brcms_c_down_led_upd(wlc);
		return;
	}

	brcms_c_radio_monitor_start(wlc);
	brcms_down(wlc->wl);
}

static void brcms_c_radio_enable(struct brcms_c_info *wlc)
{
	if (wlc->pub->up)
		return;

	if (DEVICEREMOVED(wlc))
		return;

	brcms_up(wlc->wl);
}

/* periodical query hw radio button while driver is "down" */
static void brcms_c_radio_timer(void *arg)
{
	struct brcms_c_info *wlc = (struct brcms_c_info *) arg;

	if (DEVICEREMOVED(wlc)) {
		wiphy_err(wlc->wiphy, "wl%d: %s: dead chip\n", wlc->pub->unit,
			__func__);
		brcms_down(wlc->wl);
		return;
	}

	/* cap mpc off count */
	if (wlc->mpc_offcnt < BRCMS_MPC_MAX_DELAYCNT)
		wlc->mpc_offcnt++;

	brcms_c_radio_hwdisable_upd(wlc);
	brcms_c_radio_upd(wlc);
}

static bool brcms_c_radio_monitor_start(struct brcms_c_info *wlc)
{
	/* Don't start the timer if HWRADIO feature is disabled */
	if (wlc->radio_monitor || (wlc->pub->wlfeatureflag & WL_SWFL_NOHWRADIO))
		return true;

	wlc->radio_monitor = true;
	brcms_c_pllreq(wlc, true, BRCMS_PLLREQ_RADIO_MON);
	brcms_add_timer(wlc->wl, wlc->radio_timer, TIMER_INTERVAL_RADIOCHK,
			true);
	return true;
}

bool brcms_c_radio_monitor_stop(struct brcms_c_info *wlc)
{
	if (!wlc->radio_monitor)
		return true;

	wlc->radio_monitor = false;
	brcms_c_pllreq(wlc, false, BRCMS_PLLREQ_RADIO_MON);
	return brcms_del_timer(wlc->wl, wlc->radio_timer);
}

/* common low-level watchdog code */
void brcms_b_watchdog(void *arg)
{
	struct brcms_c_info *wlc = (struct brcms_c_info *) arg;
	struct brcms_hardware *wlc_hw = wlc->hw;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc_hw->unit);

	if (!wlc_hw->up)
		return;

	/* increment second count */
	wlc_hw->now++;

	/* Check for FIFO error interrupts */
	brcms_b_fifoerrors(wlc_hw);

	/* make sure RX dma has buffers */
	dma_rxfill(wlc->hw->di[RX_FIFO]);

	wlc_phy_watchdog(wlc_hw->band->pi);
}

static void brcms_c_watchdog_by_timer(void *arg)
{
	brcms_c_watchdog(arg);
}

/* common watchdog code */
static void brcms_c_watchdog(void *arg)
{
	struct brcms_c_info *wlc = (struct brcms_c_info *) arg;
	int i;
	struct brcms_bss_cfg *cfg;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc->pub->unit);

	if (!wlc->pub->up)
		return;

	if (DEVICEREMOVED(wlc)) {
		wiphy_err(wlc->wiphy, "wl%d: %s: dead chip\n", wlc->pub->unit,
			  __func__);
		brcms_down(wlc->wl);
		return;
	}

	/* increment second count */
	wlc->pub->now++;

	/* delay radio disable */
	if (wlc->mpc_delay_off) {
		if (--wlc->mpc_delay_off == 0) {
			mboolset(wlc->pub->radio_disabled,
				 WL_RADIO_MPC_DISABLE);
			if (wlc->mpc && brcms_c_ismpc(wlc))
				wlc->mpc_offcnt = 0;
			wlc->mpc_laston_ts = OSL_SYSUPTIME();
		}
	}

	/* mpc sync */
	brcms_c_radio_mpc_upd(wlc);
	/* radio sync: sw/hw/mpc --> radio_disable/radio_enable */
	brcms_c_radio_hwdisable_upd(wlc);
	brcms_c_radio_upd(wlc);
	/* if radio is disable, driver may be down, quit here */
	if (wlc->pub->radio_disabled)
		return;

	brcms_b_watchdog(wlc);

	/*
	 * occasionally sample mac stat counters to
	 * detect 16-bit counter wrap
	 */
	if ((wlc->pub->now % SW_TIMER_MAC_STAT_UPD) == 0)
		brcms_c_statsupd(wlc);

	/* Manage TKIP countermeasures timers */
	FOREACH_BSS(wlc, i, cfg)
		if (cfg->tk_cm_dt)
			cfg->tk_cm_dt--;
		if (cfg->tk_cm_bt)
			cfg->tk_cm_bt--;
	END_FOREACH_BSS()

	if (BRCMS_ISNPHY(wlc->band) && !wlc->pub->tempsense_disable &&
	    ((wlc->pub->now - wlc->tempsense_lasttime) >=
	     BRCMS_TEMPSENSE_PERIOD)) {
		wlc->tempsense_lasttime = wlc->pub->now;
		brcms_c_tempsense_upd(wlc);
	}
}

/* Initialize just the hardware when coming out of POR or S3/S5 system states */
void brcms_b_hw_up(struct brcms_hardware *wlc_hw)
{
	if (wlc_hw->wlc->pub->hw_up)
		return;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	/*
	 * Enable pll and xtal, initialize the power control registers,
	 * and force fastclock for the remainder of brcms_c_up().
	 */
	brcms_b_xtal(wlc_hw, ON);
	ai_clkctl_init(wlc_hw->sih);
	brcms_b_clkctl_clk(wlc_hw, CLK_FAST);

	ai_pci_fixcfg(wlc_hw->sih);

	/*
	 * AI chip doesn't restore bar0win2 on
	 * hibernation/resume, need sw fixup
	 */
	if ((wlc_hw->sih->chip == BCM43224_CHIP_ID) ||
	    (wlc_hw->sih->chip == BCM43225_CHIP_ID))
		wlc_hw->regs = (struct d11regs *)
				ai_setcore(wlc_hw->sih, D11_CORE_ID, 0);

	/*
	 * Inform phy that a POR reset has occurred so
	 * it does a complete phy init
	 */
	wlc_phy_por_inform(wlc_hw->band->pi);

	wlc_hw->ucode_loaded = false;
	wlc_hw->wlc->pub->hw_up = true;

	if ((wlc_hw->boardflags & BFL_FEM)
	    && (wlc_hw->sih->chip == BCM4313_CHIP_ID)) {
		if (!
		    (wlc_hw->boardrev >= 0x1250
		     && (wlc_hw->boardflags & BFL_FEM_BT)))
			ai_epa_4313war(wlc_hw->sih);
	}
}

int brcms_b_up_prep(struct brcms_hardware *wlc_hw)
{
	uint coremask;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	/*
	 * Enable pll and xtal, initialize the power control registers,
	 * and force fastclock for the remainder of brcms_c_up().
	 */
	brcms_b_xtal(wlc_hw, ON);
	ai_clkctl_init(wlc_hw->sih);
	brcms_b_clkctl_clk(wlc_hw, CLK_FAST);

	/*
	 * Configure pci/pcmcia here instead of in brcms_c_attach()
	 * to allow mfg hotswap:  down, hotswap (chip power cycle), up.
	 */
	coremask = (1 << wlc_hw->wlc->core->coreidx);

	ai_pci_setup(wlc_hw->sih, coremask);

	/*
	 * Need to read the hwradio status here to cover the case where the
	 * system is loaded with the hw radio disabled. We do not want to
	 * bring the driver up in this case.
	 */
	if (brcms_b_radio_read_hwdisabled(wlc_hw)) {
		/* put SB PCI in down state again */
		ai_pci_down(wlc_hw->sih);
		brcms_b_xtal(wlc_hw, OFF);
		return -ENOMEDIUM;
	}

	ai_pci_up(wlc_hw->sih);

	/* reset the d11 core */
	brcms_b_corereset(wlc_hw, BRCMS_USE_COREFLAGS);

	return 0;
}

int brcms_b_up_finish(struct brcms_hardware *wlc_hw)
{
	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	wlc_hw->up = true;
	wlc_phy_hw_state_upd(wlc_hw->band->pi, true);

	/* FULLY enable dynamic power control and d11 core interrupt */
	brcms_b_clkctl_clk(wlc_hw, CLK_DYNAMIC);
	brcms_intrson(wlc_hw->wlc->wl);
	return 0;
}

/* make interface operational */
int brcms_c_up(struct brcms_c_info *wlc)
{
	BCMMSG(wlc->wiphy, "wl%d\n", wlc->pub->unit);

	/* HW is turned off so don't try to access it */
	if (wlc->pub->hw_off || DEVICEREMOVED(wlc))
		return -ENOMEDIUM;

	if (!wlc->pub->hw_up) {
		brcms_b_hw_up(wlc->hw);
		wlc->pub->hw_up = true;
	}

	if ((wlc->pub->boardflags & BFL_FEM)
	    && (wlc->pub->sih->chip == BCM4313_CHIP_ID)) {
		if (wlc->pub->boardrev >= 0x1250
		    && (wlc->pub->boardflags & BFL_FEM_BT))
			brcms_c_mhf(wlc, MHF5, MHF5_4313_GPIOCTRL,
				MHF5_4313_GPIOCTRL, BRCM_BAND_ALL);
		else
			brcms_c_mhf(wlc, MHF4, MHF4_EXTPA_ENABLE,
				    MHF4_EXTPA_ENABLE, BRCM_BAND_ALL);
	}

	/*
	 * Need to read the hwradio status here to cover the case where the
	 * system is loaded with the hw radio disabled. We do not want to bring
	 * the driver up in this case. If radio is disabled, abort up, lower
	 * power, start radio timer and return 0(for NDIS) don't call
	 * radio_update to avoid looping brcms_c_up.
	 *
	 * brcms_b_up_prep() returns either 0 or -BCME_RADIOOFF only
	 */
	if (!wlc->pub->radio_disabled) {
		int status = brcms_b_up_prep(wlc->hw);
		if (status == -ENOMEDIUM) {
			if (!mboolisset
			    (wlc->pub->radio_disabled, WL_RADIO_HW_DISABLE)) {
				int idx;
				struct brcms_bss_cfg *bsscfg;
				mboolset(wlc->pub->radio_disabled,
					 WL_RADIO_HW_DISABLE);

				FOREACH_BSS(wlc, idx, bsscfg)
					if (!BSSCFG_STA(bsscfg)
					    || !bsscfg->enable || !bsscfg->BSS)
						continue;
					wiphy_err(wlc->wiphy, "wl%d.%d: up"
						  ": rfdisable -> "
						  "bsscfg_disable()\n",
						   wlc->pub->unit, idx);
				END_FOREACH_BSS()
			}
		}
	}

	if (wlc->pub->radio_disabled) {
		brcms_c_radio_monitor_start(wlc);
		return 0;
	}

	/* brcms_b_up_prep has done brcms_c_corereset(). so clk is on, set it */
	wlc->clk = true;

	brcms_c_radio_monitor_stop(wlc);

	/* Set EDCF hostflags */
	if (EDCF_ENAB(wlc->pub))
		brcms_c_mhf(wlc, MHF1, MHF1_EDCF, MHF1_EDCF, BRCM_BAND_ALL);
	else
		brcms_c_mhf(wlc, MHF1, MHF1_EDCF, 0, BRCM_BAND_ALL);

	if (BRCMS_WAR16165(wlc))
		brcms_c_mhf(wlc, MHF2, MHF2_PCISLOWCLKWAR, MHF2_PCISLOWCLKWAR,
			BRCM_BAND_ALL);

	brcms_init(wlc->wl);
	wlc->pub->up = true;

	if (wlc->bandinit_pending) {
		brcms_c_suspend_mac_and_wait(wlc);
		brcms_c_set_chanspec(wlc, wlc->default_bss->chanspec);
		wlc->bandinit_pending = false;
		brcms_c_enable_mac(wlc);
	}

	brcms_b_up_finish(wlc->hw);

	/* other software states up after ISR is running */
	/* start APs that were to be brought up but are not up  yet */
	/* if (AP_ENAB(wlc->pub)) brcms_c_restart_ap(wlc->ap); */

	/* Program the TX wme params with the current settings */
	brcms_c_wme_retries_write(wlc);

	/* start one second watchdog timer */
	brcms_add_timer(wlc->wl, wlc->wdtimer, TIMER_INTERVAL_WATCHDOG, true);
	wlc->WDarmed = true;

	/* ensure antenna config is up to date */
	brcms_c_stf_phy_txant_upd(wlc);
	/* ensure LDPC config is in sync */
	brcms_c_ht_update_ldpc(wlc, wlc->stf->ldpc);

	return 0;
}

/*
 * Initialize the base precedence map for dequeueing
 * from txq based on WME settings
 */
static void brcms_c_tx_prec_map_init(struct brcms_c_info *wlc)
{
	wlc->tx_prec_map = BRCMS_PREC_BMP_ALL;
	memset(wlc->fifo2prec_map, 0, NFIFO * sizeof(u16));

	/*
	 * For non-WME, both fifos have overlapping MAXPRIO. So just
	 * disable all precedences if either is full.
	 */
	if (!EDCF_ENAB(wlc->pub)) {
		wlc->fifo2prec_map[TX_DATA_FIFO] = BRCMS_PREC_BMP_ALL;
		wlc->fifo2prec_map[TX_CTL_FIFO] = BRCMS_PREC_BMP_ALL;
	} else {
		wlc->fifo2prec_map[TX_AC_BK_FIFO] = BRCMS_PREC_BMP_AC_BK;
		wlc->fifo2prec_map[TX_AC_BE_FIFO] = BRCMS_PREC_BMP_AC_BE;
		wlc->fifo2prec_map[TX_AC_VI_FIFO] = BRCMS_PREC_BMP_AC_VI;
		wlc->fifo2prec_map[TX_AC_VO_FIFO] = BRCMS_PREC_BMP_AC_VO;
	}
}

static uint brcms_c_down_del_timer(struct brcms_c_info *wlc)
{
	uint callbacks = 0;

	return callbacks;
}

int brcms_b_bmac_down_prep(struct brcms_hardware *wlc_hw)
{
	bool dev_gone;
	uint callbacks = 0;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	if (!wlc_hw->up)
		return callbacks;

	dev_gone = DEVICEREMOVED(wlc_hw->wlc);

	/* disable interrupts */
	if (dev_gone)
		wlc_hw->wlc->macintmask = 0;
	else {
		/* now disable interrupts */
		brcms_intrsoff(wlc_hw->wlc->wl);

		/* ensure we're running on the pll clock again */
		brcms_b_clkctl_clk(wlc_hw, CLK_FAST);
	}
	/* down phy at the last of this stage */
	callbacks += wlc_phy_down(wlc_hw->band->pi);

	return callbacks;
}

int brcms_b_down_finish(struct brcms_hardware *wlc_hw)
{
	uint callbacks = 0;
	bool dev_gone;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);

	if (!wlc_hw->up)
		return callbacks;

	wlc_hw->up = false;
	wlc_phy_hw_state_upd(wlc_hw->band->pi, false);

	dev_gone = DEVICEREMOVED(wlc_hw->wlc);

	if (dev_gone) {
		wlc_hw->sbclk = false;
		wlc_hw->clk = false;
		wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, false);

		/* reclaim any posted packets */
		brcms_c_flushqueues(wlc_hw->wlc);
	} else {

		/* Reset and disable the core */
		if (ai_iscoreup(wlc_hw->sih)) {
			if (R_REG(&wlc_hw->regs->maccontrol) &
			    MCTL_EN_MAC)
				brcms_c_suspend_mac_and_wait(wlc_hw->wlc);
			callbacks += brcms_reset(wlc_hw->wlc->wl);
			brcms_c_coredisable(wlc_hw);
		}

		/* turn off primary xtal and pll */
		if (!wlc_hw->noreset) {
			ai_pci_down(wlc_hw->sih);
			brcms_b_xtal(wlc_hw, OFF);
		}
	}

	return callbacks;
}

/*
 * Mark the interface nonoperational, stop the software mechanisms,
 * disable the hardware, free any transient buffer state.
 * Return a count of the number of driver callbacks still pending.
 */
uint brcms_c_down(struct brcms_c_info *wlc)
{

	uint callbacks = 0;
	int i;
	bool dev_gone = false;
	struct brcms_txq_info *qi;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc->pub->unit);

	/* check if we are already in the going down path */
	if (wlc->going_down) {
		wiphy_err(wlc->wiphy, "wl%d: %s: Driver going down so return"
			  "\n", wlc->pub->unit, __func__);
		return 0;
	}
	if (!wlc->pub->up)
		return callbacks;

	/* in between, mpc could try to bring down again.. */
	wlc->going_down = true;

	callbacks += brcms_b_bmac_down_prep(wlc->hw);

	dev_gone = DEVICEREMOVED(wlc);

	/* Call any registered down handlers */
	for (i = 0; i < BRCMS_MAXMODULES; i++) {
		if (wlc->modulecb[i].down_fn)
			callbacks +=
			    wlc->modulecb[i].down_fn(wlc->modulecb[i].hdl);
	}

	/* cancel the watchdog timer */
	if (wlc->WDarmed) {
		if (!brcms_del_timer(wlc->wl, wlc->wdtimer))
			callbacks++;
		wlc->WDarmed = false;
	}
	/* cancel all other timers */
	callbacks += brcms_c_down_del_timer(wlc);

	wlc->pub->up = false;

	wlc_phy_mute_upd(wlc->band->pi, false, PHY_MUTE_ALL);

	/* clear txq flow control */
	brcms_c_txflowcontrol_reset(wlc);

	/* flush tx queues */
	for (qi = wlc->tx_queues; qi != NULL; qi = qi->next)
		brcmu_pktq_flush(&qi->q, true, NULL, NULL);

	callbacks += brcms_b_down_finish(wlc->hw);

	/* brcms_b_down_finish has done brcms_c_coredisable(). so clk is off */
	wlc->clk = false;

	wlc->going_down = false;
	return callbacks;
}

/* Set the current gmode configuration */
int brcms_c_set_gmode(struct brcms_c_info *wlc, u8 gmode, bool config)
{
	int ret = 0;
	uint i;
	struct brcms_c_rateset rs;
	/* Default to 54g Auto */
	/* Advertise and use shortslot (-1/0/1 Auto/Off/On) */
	s8 shortslot = BRCMS_SHORTSLOT_AUTO;
	bool shortslot_restrict = false; /* Restrict association to stations
					  * that support shortslot
					  */
	bool ofdm_basic = false;	/* Make 6, 12, and 24 basic rates */
	/* Advertise and use short preambles (-1/0/1 Auto/Off/On) */
	int preamble = BRCMS_PLCP_LONG;
	bool preamble_restrict = false;	/* Restrict association to stations
					 * that support short preambles
					 */
	struct brcms_band *band;

	/* if N-support is enabled, allow Gmode set as long as requested
	 * Gmode is not GMODE_LEGACY_B
	 */
	if (N_ENAB(wlc->pub) && gmode == GMODE_LEGACY_B)
		return -ENOTSUPP;

	/* verify that we are dealing with 2G band and grab the band pointer */
	if (wlc->band->bandtype == BRCM_BAND_2G)
		band = wlc->band;
	else if ((NBANDS(wlc) > 1) &&
		 (wlc->bandstate[OTHERBANDUNIT(wlc)]->bandtype == BRCM_BAND_2G))
		band = wlc->bandstate[OTHERBANDUNIT(wlc)];
	else
		return -EINVAL;

	/* Legacy or bust when no OFDM is supported by regulatory */
	if ((brcms_c_channel_locale_flags_in_band(wlc->cmi, band->bandunit) &
	     BRCMS_NO_OFDM) && (gmode != GMODE_LEGACY_B))
		return -EINVAL;

	/* update configuration value */
	if (config == true)
		brcms_c_protection_upd(wlc, BRCMS_PROT_G_USER, gmode);

	/* Clear supported rates filter */
	memset(&wlc->sup_rates_override, 0, sizeof(struct brcms_c_rateset));

	/* Clear rateset override */
	memset(&rs, 0, sizeof(struct brcms_c_rateset));

	switch (gmode) {
	case GMODE_LEGACY_B:
		shortslot = BRCMS_SHORTSLOT_OFF;
		brcms_c_rateset_copy(&gphy_legacy_rates, &rs);

		break;

	case GMODE_LRS:
		if (AP_ENAB(wlc->pub))
			brcms_c_rateset_copy(&cck_rates,
					     &wlc->sup_rates_override);
		break;

	case GMODE_AUTO:
		/* Accept defaults */
		break;

	case GMODE_ONLY:
		ofdm_basic = true;
		preamble = BRCMS_PLCP_SHORT;
		preamble_restrict = true;
		break;

	case GMODE_PERFORMANCE:
		if (AP_ENAB(wlc->pub))
			/* Put all rates into the Supported Rates element */
			brcms_c_rateset_copy(&cck_ofdm_rates,
					 &wlc->sup_rates_override);

		shortslot = BRCMS_SHORTSLOT_ON;
		shortslot_restrict = true;
		ofdm_basic = true;
		preamble = BRCMS_PLCP_SHORT;
		preamble_restrict = true;
		break;

	default:
		/* Error */
		wiphy_err(wlc->wiphy, "wl%d: %s: invalid gmode %d\n",
			  wlc->pub->unit, __func__, gmode);
		return -ENOTSUPP;
	}

	/*
	 * If we are switching to gmode == GMODE_LEGACY_B,
	 * clean up rate info that may refer to OFDM rates.
	 */
	if ((gmode == GMODE_LEGACY_B) && (band->gmode != GMODE_LEGACY_B)) {
		band->gmode = gmode;
		if (band->rspec_override && !IS_CCK(band->rspec_override)) {
			band->rspec_override = 0;
			brcms_c_reprate_init(wlc);
		}
		if (band->mrspec_override && !IS_CCK(band->mrspec_override))
			band->mrspec_override = 0;
	}

	band->gmode = gmode;

	wlc->shortslot_override = shortslot;

	if (AP_ENAB(wlc->pub))
		/* wlc->ap->shortslot_restrict = shortslot_restrict; */
		wlc->PLCPHdr_override =
		    (preamble !=
		     BRCMS_PLCP_LONG) ? BRCMS_PLCP_SHORT : BRCMS_PLCP_AUTO;

	if ((AP_ENAB(wlc->pub) && preamble != BRCMS_PLCP_LONG)
	    || preamble == BRCMS_PLCP_SHORT)
		wlc->default_bss->capability |= WLAN_CAPABILITY_SHORT_PREAMBLE;
	else
		wlc->default_bss->capability &= ~WLAN_CAPABILITY_SHORT_PREAMBLE;

	/* Update shortslot capability bit for AP and IBSS */
	if ((AP_ENAB(wlc->pub) && shortslot == BRCMS_SHORTSLOT_AUTO) ||
	    shortslot == BRCMS_SHORTSLOT_ON)
		wlc->default_bss->capability |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
	else
		wlc->default_bss->capability &=
					~WLAN_CAPABILITY_SHORT_SLOT_TIME;

	/* Use the default 11g rateset */
	if (!rs.count)
		brcms_c_rateset_copy(&cck_ofdm_rates, &rs);

	if (ofdm_basic) {
		for (i = 0; i < rs.count; i++) {
			if (rs.rates[i] == BRCM_RATE_6M
			    || rs.rates[i] == BRCM_RATE_12M
			    || rs.rates[i] == BRCM_RATE_24M)
				rs.rates[i] |= BRCMS_RATE_FLAG;
		}
	}

	/* Set default bss rateset */
	wlc->default_bss->rateset.count = rs.count;
	memcpy(wlc->default_bss->rateset.rates, rs.rates,
	       sizeof(wlc->default_bss->rateset.rates));

	return ret;
}

static int brcms_c_nmode_validate(struct brcms_c_info *wlc, s32 nmode)
{
	int err = 0;

	switch (nmode) {

	case OFF:
		break;

	case AUTO:
	case WL_11N_2x2:
	case WL_11N_3x3:
		if (!(BRCMS_PHY_11N_CAP(wlc->band)))
			err = -EINVAL;
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

int brcms_c_set_nmode(struct brcms_c_info *wlc, s32 nmode)
{
	uint i;
	int err;

	err = brcms_c_nmode_validate(wlc, nmode);
	if (err)
		return err;

	switch (nmode) {
	case OFF:
		wlc->pub->_n_enab = OFF;
		wlc->default_bss->flags &= ~BRCMS_BSS_HT;
		/* delete the mcs rates from the default and hw ratesets */
		brcms_c_rateset_mcs_clear(&wlc->default_bss->rateset);
		for (i = 0; i < NBANDS(wlc); i++) {
			memset(wlc->bandstate[i]->hw_rateset.mcs, 0,
			       MCSSET_LEN);
			if (IS_MCS(wlc->band->rspec_override)) {
				wlc->bandstate[i]->rspec_override = 0;
				brcms_c_reprate_init(wlc);
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
		/* force GMODE_AUTO if NMODE is ON */
		brcms_c_set_gmode(wlc, GMODE_AUTO, true);
		if (nmode == WL_11N_3x3)
			wlc->pub->_n_enab = SUPPORT_HT;
		else
			wlc->pub->_n_enab = SUPPORT_11N;
		wlc->default_bss->flags |= BRCMS_BSS_HT;
		/* add the mcs rates to the default and hw ratesets */
		brcms_c_rateset_mcs_build(&wlc->default_bss->rateset,
				      wlc->stf->txstreams);
		for (i = 0; i < NBANDS(wlc); i++)
			memcpy(wlc->bandstate[i]->hw_rateset.mcs,
			       wlc->default_bss->rateset.mcs, MCSSET_LEN);
		break;

	default:
		break;
	}

	return err;
}

static int
brcms_c_set_rateset(struct brcms_c_info *wlc, struct brcms_c_rateset *rs_arg)
{
	struct brcms_c_rateset rs, new;
	uint bandunit;

	memcpy(&rs, rs_arg, sizeof(struct brcms_c_rateset));

	/* check for bad count value */
	if ((rs.count == 0) || (rs.count > BRCMS_NUMRATES))
		return -EINVAL;

	/* try the current band */
	bandunit = wlc->band->bandunit;
	memcpy(&new, &rs, sizeof(struct brcms_c_rateset));
	if (brcms_c_rate_hwrs_filter_sort_validate
	    (&new, &wlc->bandstate[bandunit]->hw_rateset, true,
	     wlc->stf->txstreams))
		goto good;

	/* try the other band */
	if (IS_MBAND_UNLOCKED(wlc)) {
		bandunit = OTHERBANDUNIT(wlc);
		memcpy(&new, &rs, sizeof(struct brcms_c_rateset));
		if (brcms_c_rate_hwrs_filter_sort_validate(&new,
						       &wlc->
						       bandstate[bandunit]->
						       hw_rateset, true,
						       wlc->stf->txstreams))
			goto good;
	}

	return -EBADE;

 good:
	/* apply new rateset */
	memcpy(&wlc->default_bss->rateset, &new,
	       sizeof(struct brcms_c_rateset));
	memcpy(&wlc->bandstate[bandunit]->defrateset, &new,
	       sizeof(struct brcms_c_rateset));
	return 0;
}

/* simplified integer set interface for common ioctl handler */
int brcms_c_set(struct brcms_c_info *wlc, int cmd, int arg)
{
	return brcms_c_ioctl(wlc, cmd, (void *)&arg, sizeof(arg), NULL);
}

/* simplified integer get interface for common ioctl handler */
int brcms_c_get(struct brcms_c_info *wlc, int cmd, int *arg)
{
	return brcms_c_ioctl(wlc, cmd, arg, sizeof(int), NULL);
}

static void brcms_c_ofdm_rateset_war(struct brcms_c_info *wlc)
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
brcms_c_ioctl(struct brcms_c_info *wlc, int cmd, void *arg, int len,
	      struct brcms_c_if *wlcif)
{
	return _brcms_c_ioctl(wlc, cmd, arg, len, wlcif);
}

/* common ioctl handler. return: 0=ok, -1=error, positive=particular error */
static int
_brcms_c_ioctl(struct brcms_c_info *wlc, int cmd, void *arg, int len,
	       struct brcms_c_if *wlcif)
{
	int val, *pval;
	bool bool_val;
	int bcmerror;
	struct scb *nextscb;
	bool ta_ok;
	uint band;
	struct brcms_bss_cfg *bsscfg;
	struct brcms_bss_info *current_bss;

	/* update bsscfg pointer */
	bsscfg = wlc->cfg;
	current_bss = bsscfg->current_bss;

	/* initialize the following to get rid of compiler warning */
	nextscb = NULL;
	ta_ok = false;
	band = 0;

	/* If the device is turned off, then it's not "removed" */
	if (!wlc->pub->hw_off && DEVICEREMOVED(wlc)) {
		wiphy_err(wlc->wiphy, "wl%d: %s: dead chip\n", wlc->pub->unit,
			  __func__);
		brcms_down(wlc->wl);
		return -EBADE;
	}

	/* default argument is generic integer */
	pval = arg ? (int *)arg : NULL;

	/*
	 * This will prevent misaligned access. The (void *) cast prevents a
	 * memcpy alignment issue on e.g. Sparc64 platforms.
	 */
	if (pval && (u32) len >= sizeof(val))
		memcpy((void *)&val, (void *)pval, sizeof(val));
	else
		val = 0;

	/* bool conversion to avoid duplication below */
	bool_val = val != 0;
	bcmerror = 0;

	if ((arg == NULL) || (len <= 0)) {
		wiphy_err(wlc->wiphy, "wl%d: %s: Command %d needs arguments\n",
			  wlc->pub->unit, __func__, cmd);
		bcmerror = -EINVAL;
		goto done;
	}

	switch (cmd) {

	case BRCM_SET_CHANNEL:{
			u16 chspec = CH20MHZ_CHSPEC(val);

			if (val < 0 || val > MAXCHANNEL) {
				bcmerror = -EINVAL;
				break;
			}

			if (!brcms_c_valid_chanspec_db(wlc->cmi, chspec)) {
				bcmerror = -EINVAL;
				break;
			}

			if (!wlc->pub->up && IS_MBAND_UNLOCKED(wlc)) {
				if (wlc->band->bandunit !=
				    CHSPEC_BANDUNIT(chspec))
					wlc->bandinit_pending = true;
				else
					wlc->bandinit_pending = false;
			}

			wlc->default_bss->chanspec = chspec;
			/* brcms_c_BSSinit() will sanitize the rateset before
			 * using it.. */
			if (wlc->pub->up &&
			    (BRCMS_BAND_PI_RADIO_CHANSPEC != chspec)) {
				brcms_c_set_home_chanspec(wlc, chspec);
				brcms_c_suspend_mac_and_wait(wlc);
				brcms_c_set_chanspec(wlc, chspec);
				brcms_c_enable_mac(wlc);
			}
			break;
		}

	case BRCM_SET_SRL:
		if (val >= 1 && val <= RETRY_SHORT_MAX) {
			int ac;
			wlc->SRL = (u16) val;

			brcms_b_retrylimit_upd(wlc->hw, wlc->SRL, wlc->LRL);

			for (ac = 0; ac < AC_COUNT; ac++)
				BRCMS_WME_RETRY_SHORT_SET(wlc, ac, wlc->SRL);

			brcms_c_wme_retries_write(wlc);
		} else
			bcmerror = -EINVAL;
		break;

	case BRCM_SET_LRL:
		if (val >= 1 && val <= 255) {
			int ac;
			wlc->LRL = (u16) val;

			brcms_b_retrylimit_upd(wlc->hw, wlc->SRL, wlc->LRL);

			for (ac = 0; ac < AC_COUNT; ac++)
				BRCMS_WME_RETRY_LONG_SET(wlc, ac, wlc->LRL);

			brcms_c_wme_retries_write(wlc);
		} else
			bcmerror = -EINVAL;
		break;

	case BRCM_GET_CURR_RATESET:{
			struct brcm_rateset *ret_rs =
						(struct brcm_rateset *) arg;
			struct brcms_c_rateset *rs;

			if (wlc->pub->associated)
				rs = &current_bss->rateset;
			else
				rs = &wlc->default_bss->rateset;

			if (len < (int)(rs->count + sizeof(rs->count))) {
				bcmerror = -EOVERFLOW;
				break;
			}

			/* Copy only legacy rateset section */
			ret_rs->count = rs->count;
			memcpy(&ret_rs->rates, &rs->rates, rs->count);
			break;
		}

	case BRCM_SET_RATESET:{
			struct brcms_c_rateset rs;
			struct brcm_rateset *in_rs =
						(struct brcm_rateset *) arg;

			if (len < (int)(in_rs->count + sizeof(in_rs->count))) {
				bcmerror = -EOVERFLOW;
				break;
			}

			if (in_rs->count > BRCMS_NUMRATES) {
				bcmerror = -ENOBUFS;
				break;
			}

			memset(&rs, 0, sizeof(struct brcms_c_rateset));

			/* Copy only legacy rateset section */
			rs.count = in_rs->count;
			memcpy(&rs.rates, &in_rs->rates, rs.count);

			/* merge rateset coming in with the current mcsset */
			if (N_ENAB(wlc->pub)) {
				struct brcms_bss_info *mcsset_bss;
				if (bsscfg->associated)
					mcsset_bss = current_bss;
				else
					mcsset_bss = wlc->default_bss;
				memcpy(rs.mcs, &mcsset_bss->rateset.mcs[0],
				       MCSSET_LEN);
			}

			bcmerror = brcms_c_set_rateset(wlc, &rs);

			if (!bcmerror)
				brcms_c_ofdm_rateset_war(wlc);

			break;
		}

	case BRCM_SET_BCNPRD:
		/* range [1, 0xffff] */
		if (val >= DOT11_MIN_BEACON_PERIOD
		    && val <= DOT11_MAX_BEACON_PERIOD)
			wlc->default_bss->beacon_period = (u16) val;
		else
			bcmerror = -EINVAL;
		break;

	case BRCM_GET_PHYLIST:
		{
			unsigned char *cp = arg;
			if (len < 3) {
				bcmerror = -EOVERFLOW;
				break;
			}

			if (BRCMS_ISNPHY(wlc->band))
				*cp++ = 'n';
			else if (BRCMS_ISLCNPHY(wlc->band))
				*cp++ = 'c';
			else if (BRCMS_ISSSLPNPHY(wlc->band))
				*cp++ = 's';
			*cp = '\0';
			break;
		}

	case BRCMS_SET_SHORTSLOT_OVERRIDE:
		if (val != BRCMS_SHORTSLOT_AUTO && val != BRCMS_SHORTSLOT_OFF &&
		    val != BRCMS_SHORTSLOT_ON) {
			bcmerror = -EINVAL;
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
			brcms_c_switch_shortslot(wlc, false);
		} else {
			/* driver is down, so just update the brcms_c_info
			 * value */
			if (wlc->shortslot_override == BRCMS_SHORTSLOT_AUTO)
				wlc->shortslot = false;
			else
				wlc->shortslot =
				    (wlc->shortslot_override ==
				     BRCMS_SHORTSLOT_ON);
		}

		break;

	}
 done:

	if (bcmerror)
		wlc->pub->bcmerror = bcmerror;

	return bcmerror;
}

/*
 * register watchdog and down handlers.
 */
int brcms_c_module_register(struct brcms_pub *pub,
			    const char *name, struct brcms_info *hdl,
			    int (*d_fn)(void *handle))
{
	struct brcms_c_info *wlc = (struct brcms_c_info *) pub->wlc;
	int i;

	/* find an empty entry and just add, no duplication check! */
	for (i = 0; i < BRCMS_MAXMODULES; i++) {
		if (wlc->modulecb[i].name[0] == '\0') {
			strncpy(wlc->modulecb[i].name, name,
				sizeof(wlc->modulecb[i].name) - 1);
			wlc->modulecb[i].hdl = hdl;
			wlc->modulecb[i].down_fn = d_fn;
			return 0;
		}
	}

	return -ENOSR;
}

/* unregister module callbacks */
int brcms_c_module_unregister(struct brcms_pub *pub, const char *name,
			      struct brcms_info *hdl)
{
	struct brcms_c_info *wlc = (struct brcms_c_info *) pub->wlc;
	int i;

	if (wlc == NULL)
		return -ENODATA;

	for (i = 0; i < BRCMS_MAXMODULES; i++) {
		if (!strcmp(wlc->modulecb[i].name, name) &&
		    (wlc->modulecb[i].hdl == hdl)) {
			memset(&wlc->modulecb[i], 0, sizeof(struct modulecb));
			return 0;
		}
	}

	/* table not found! */
	return -ENODATA;
}

/*
 * Write WME tunable parameters for retransmit/max rate
 * from wlc struct to ucode
 */
static void brcms_c_wme_retries_write(struct brcms_c_info *wlc)
{
	int ac;

	/* Need clock to do this */
	if (!wlc->clk)
		return;

	for (ac = 0; ac < AC_COUNT; ac++)
		brcms_c_write_shm(wlc, M_AC_TXLMT_ADDR(ac),
				  wlc->wme_retries[ac]);
}

#ifdef BCMDBG
static const char * const supr_reason[] = {
	"None", "PMQ Entry", "Flush request",
	"Previous frag failure", "Channel mismatch",
	"Lifetime Expiry", "Underflow"
};

static void brcms_c_print_txs_status(u16 s)
{
	printk(KERN_DEBUG "[15:12]  %d  frame attempts\n",
	       (s & TX_STATUS_FRM_RTX_MASK) >> TX_STATUS_FRM_RTX_SHIFT);
	printk(KERN_DEBUG " [11:8]  %d  rts attempts\n",
	       (s & TX_STATUS_RTS_RTX_MASK) >> TX_STATUS_RTS_RTX_SHIFT);
	printk(KERN_DEBUG "    [7]  %d  PM mode indicated\n",
	       ((s & TX_STATUS_PMINDCTD) ? 1 : 0));
	printk(KERN_DEBUG "    [6]  %d  intermediate status\n",
	       ((s & TX_STATUS_INTERMEDIATE) ? 1 : 0));
	printk(KERN_DEBUG "    [5]  %d  AMPDU\n",
	       (s & TX_STATUS_AMPDU) ? 1 : 0);
	printk(KERN_DEBUG "  [4:2]  %d  Frame Suppressed Reason (%s)\n",
	       ((s & TX_STATUS_SUPR_MASK) >> TX_STATUS_SUPR_SHIFT),
	       supr_reason[(s & TX_STATUS_SUPR_MASK) >> TX_STATUS_SUPR_SHIFT]);
	printk(KERN_DEBUG "    [1]  %d  acked\n",
	       ((s & TX_STATUS_ACK_RCV) ? 1 : 0));
}
#endif				/* BCMDBG */

void brcms_c_print_txstatus(struct tx_status *txs)
{
#if defined(BCMDBG)
	u16 s = txs->status;
	u16 ackphyrxsh = txs->ackphyrxsh;

	printk(KERN_DEBUG "\ntxpkt (MPDU) Complete\n");

	printk(KERN_DEBUG "FrameID: %04x   ", txs->frameid);
	printk(KERN_DEBUG "TxStatus: %04x", s);
	printk(KERN_DEBUG "\n");

	brcms_c_print_txs_status(s);

	printk(KERN_DEBUG "LastTxTime: %04x ", txs->lasttxtime);
	printk(KERN_DEBUG "Seq: %04x ", txs->sequence);
	printk(KERN_DEBUG "PHYTxStatus: %04x ", txs->phyerr);
	printk(KERN_DEBUG "RxAckRSSI: %04x ",
	       (ackphyrxsh & PRXS1_JSSI_MASK) >> PRXS1_JSSI_SHIFT);
	printk(KERN_DEBUG "RxAckSQ: %04x",
	       (ackphyrxsh & PRXS1_SQ_MASK) >> PRXS1_SQ_SHIFT);
	printk(KERN_DEBUG "\n");
#endif				/* defined(BCMDBG) */
}

void brcms_c_statsupd(struct brcms_c_info *wlc)
{
	int i;
	struct macstat macstats;
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

	/* Read mac stats from contiguous shared memory */
	brcms_b_copyfrom_shm(wlc->hw, M_UCODE_MACSTAT,
			     &macstats, sizeof(struct macstat));

#ifdef BCMDBG
	/* check for rx fifo 0 overflow */
	delta = (u16) (wlc->core->macstat_snapshot->rxf0ovfl - rxf0ovfl);
	if (delta)
		wiphy_err(wlc->wiphy, "wl%d: %u rx fifo 0 overflows!\n",
			  wlc->pub->unit, delta);

	/* check for tx fifo underflows */
	for (i = 0; i < NFIFO; i++) {
		delta =
		    (u16) (wlc->core->macstat_snapshot->txfunfl[i] -
			      txfunfl[i]);
		if (delta)
			wiphy_err(wlc->wiphy, "wl%d: %u tx fifo %d underflows!"
				  "\n", wlc->pub->unit, delta, i);
	}
#endif				/* BCMDBG */

	/* merge counters from dma module */
	for (i = 0; i < NFIFO; i++) {
		if (wlc->hw->di[i])
			dma_counterreset(wlc->hw->di[i]);
	}
}

bool brcms_c_chipmatch(u16 vendor, u16 device)
{
	if (vendor != PCI_VENDOR_ID_BROADCOM) {
		pr_err("chipmatch: unknown vendor id %04x\n", vendor);
		return false;
	}

	if (device == BCM43224_D11N_ID_VEN1)
		return true;
	if ((device == BCM43224_D11N_ID) || (device == BCM43225_D11N2G_ID))
		return true;
	if (device == BCM4313_D11N2G_ID)
		return true;
	if ((device == BCM43236_D11N_ID) || (device == BCM43236_D11N2G_ID))
		return true;

	pr_err("chipmatch: unknown device id %04x\n", device);
	return false;
}

#if defined(BCMDBG)
void brcms_c_print_txdesc(struct d11txh *txh)
{
	u16 mtcl = le16_to_cpu(txh->MacTxControlLow);
	u16 mtch = le16_to_cpu(txh->MacTxControlHigh);
	u16 mfc = le16_to_cpu(txh->MacFrameControl);
	u16 tfest = le16_to_cpu(txh->TxFesTimeNormal);
	u16 ptcw = le16_to_cpu(txh->PhyTxControlWord);
	u16 ptcw_1 = le16_to_cpu(txh->PhyTxControlWord_1);
	u16 ptcw_1_Fbr = le16_to_cpu(txh->PhyTxControlWord_1_Fbr);
	u16 ptcw_1_Rts = le16_to_cpu(txh->PhyTxControlWord_1_Rts);
	u16 ptcw_1_FbrRts = le16_to_cpu(txh->PhyTxControlWord_1_FbrRts);
	u16 mainrates = le16_to_cpu(txh->MainRates);
	u16 xtraft = le16_to_cpu(txh->XtraFrameTypes);
	u8 *iv = txh->IV;
	u8 *ra = txh->TxFrameRA;
	u16 tfestfb = le16_to_cpu(txh->TxFesTimeFallback);
	u8 *rtspfb = txh->RTSPLCPFallback;
	u16 rtsdfb = le16_to_cpu(txh->RTSDurFallback);
	u8 *fragpfb = txh->FragPLCPFallback;
	u16 fragdfb = le16_to_cpu(txh->FragDurFallback);
	u16 mmodelen = le16_to_cpu(txh->MModeLen);
	u16 mmodefbrlen = le16_to_cpu(txh->MModeFbrLen);
	u16 tfid = le16_to_cpu(txh->TxFrameID);
	u16 txs = le16_to_cpu(txh->TxStatus);
	u16 mnmpdu = le16_to_cpu(txh->MaxNMpdus);
	u16 mabyte = le16_to_cpu(txh->MaxABytes_MRT);
	u16 mabyte_f = le16_to_cpu(txh->MaxABytes_FBR);
	u16 mmbyte = le16_to_cpu(txh->MinMBytes);

	u8 *rtsph = txh->RTSPhyHeader;
	struct ieee80211_rts rts = txh->rts_frame;
	char hexbuf[256];

	/* add plcp header along with txh descriptor */
	printk(KERN_DEBUG "Raw TxDesc + plcp header:\n");
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
			     txh, sizeof(struct d11txh) + 48);

	printk(KERN_DEBUG "TxCtlLow: %04x ", mtcl);
	printk(KERN_DEBUG "TxCtlHigh: %04x ", mtch);
	printk(KERN_DEBUG "FC: %04x ", mfc);
	printk(KERN_DEBUG "FES Time: %04x\n", tfest);
	printk(KERN_DEBUG "PhyCtl: %04x%s ", ptcw,
	       (ptcw & PHY_TXC_SHORT_HDR) ? " short" : "");
	printk(KERN_DEBUG "PhyCtl_1: %04x ", ptcw_1);
	printk(KERN_DEBUG "PhyCtl_1_Fbr: %04x\n", ptcw_1_Fbr);
	printk(KERN_DEBUG "PhyCtl_1_Rts: %04x ", ptcw_1_Rts);
	printk(KERN_DEBUG "PhyCtl_1_Fbr_Rts: %04x\n", ptcw_1_FbrRts);
	printk(KERN_DEBUG "MainRates: %04x ", mainrates);
	printk(KERN_DEBUG "XtraFrameTypes: %04x ", xtraft);
	printk(KERN_DEBUG "\n");

	brcmu_format_hex(hexbuf, iv, sizeof(txh->IV));
	printk(KERN_DEBUG "SecIV:       %s\n", hexbuf);
	brcmu_format_hex(hexbuf, ra, sizeof(txh->TxFrameRA));
	printk(KERN_DEBUG "RA:          %s\n", hexbuf);

	printk(KERN_DEBUG "Fb FES Time: %04x ", tfestfb);
	brcmu_format_hex(hexbuf, rtspfb, sizeof(txh->RTSPLCPFallback));
	printk(KERN_DEBUG "RTS PLCP: %s ", hexbuf);
	printk(KERN_DEBUG "RTS DUR: %04x ", rtsdfb);
	brcmu_format_hex(hexbuf, fragpfb, sizeof(txh->FragPLCPFallback));
	printk(KERN_DEBUG "PLCP: %s ", hexbuf);
	printk(KERN_DEBUG "DUR: %04x", fragdfb);
	printk(KERN_DEBUG "\n");

	printk(KERN_DEBUG "MModeLen: %04x ", mmodelen);
	printk(KERN_DEBUG "MModeFbrLen: %04x\n", mmodefbrlen);

	printk(KERN_DEBUG "FrameID:     %04x\n", tfid);
	printk(KERN_DEBUG "TxStatus:    %04x\n", txs);

	printk(KERN_DEBUG "MaxNumMpdu:  %04x\n", mnmpdu);
	printk(KERN_DEBUG "MaxAggbyte:  %04x\n", mabyte);
	printk(KERN_DEBUG "MaxAggbyte_fb:  %04x\n", mabyte_f);
	printk(KERN_DEBUG "MinByte:     %04x\n", mmbyte);

	brcmu_format_hex(hexbuf, rtsph, sizeof(txh->RTSPhyHeader));
	printk(KERN_DEBUG "RTS PLCP: %s ", hexbuf);
	brcmu_format_hex(hexbuf, (u8 *) &rts, sizeof(txh->rts_frame));
	printk(KERN_DEBUG "RTS Frame: %s", hexbuf);
	printk(KERN_DEBUG "\n");
}
#endif				/* defined(BCMDBG) */

#if defined(BCMDBG)
void brcms_c_print_rxh(struct d11rxhdr *rxh)
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
	static const struct brcmu_bit_desc macstat_flags[] = {
		{RXS_FCSERR, "FCSErr"},
		{RXS_RESPFRAMETX, "Reply"},
		{RXS_PBPRES, "PADDING"},
		{RXS_DECATMPT, "DeCr"},
		{RXS_DECERR, "DeCrErr"},
		{RXS_BCNSENT, "Bcn"},
		{0, NULL}
	};

	printk(KERN_DEBUG "Raw RxDesc:\n");
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, rxh,
			     sizeof(struct d11rxhdr));

	brcmu_format_flags(macstat_flags, macstatus1, flagstr, 64);

	snprintf(lenbuf, sizeof(lenbuf), "0x%x", len);

	printk(KERN_DEBUG "RxFrameSize:     %6s (%d)%s\n", lenbuf, len,
	       (rxh->PhyRxStatus_0 & PRXS0_SHORTH) ? " short preamble" : "");
	printk(KERN_DEBUG "RxPHYStatus:     %04x %04x %04x %04x\n",
	       phystatus_0, phystatus_1, phystatus_2, phystatus_3);
	printk(KERN_DEBUG "RxMACStatus:     %x %s\n", macstatus1, flagstr);
	printk(KERN_DEBUG "RXMACaggtype:    %x\n",
	       (macstatus2 & RXS_AGGTYPE_MASK));
	printk(KERN_DEBUG "RxTSFTime:       %04x\n", rxh->RxTSFTime);
}
#endif				/* defined(BCMDBG) */

u16 brcms_b_rate_shm_offset(struct brcms_hardware *wlc_hw, u8 rate)
{
	u16 table_ptr;
	u8 phy_rate, index;

	/* get the phy specific rate encoding for the PLCP SIGNAL field */
	if (IS_OFDM(rate))
		table_ptr = M_RT_DIRMAP_A;
	else
		table_ptr = M_RT_DIRMAP_B;

	/* for a given rate, the LS-nibble of the PLCP SIGNAL field is
	 * the index into the rate table.
	 */
	phy_rate = rate_info[rate] & BRCMS_RATE_MASK;
	index = phy_rate & 0xf;

	/* Find the SHM pointer to the rate table entry by looking in the
	 * Direct-map Table
	 */
	return 2 * brcms_b_read_shm(wlc_hw, table_ptr + (index * 2));
}

static u16 brcms_c_rate_shm_offset(struct brcms_c_info *wlc, u8 rate)
{
	return brcms_b_rate_shm_offset(wlc->hw, rate);
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
bool brcms_c_prec_enq(struct brcms_c_info *wlc, struct pktq *q,
		      struct sk_buff *pkt, int prec)
{
	return brcms_c_prec_enq_head(wlc, q, pkt, prec, false);
}

bool
brcms_c_prec_enq_head(struct brcms_c_info *wlc, struct pktq *q,
		      struct sk_buff *pkt, int prec, bool head)
{
	struct sk_buff *p;
	int eprec = -1;		/* precedence to evict from */

	/* Determine precedence from which to evict packet, if any */
	if (pktq_pfull(q, prec))
		eprec = prec;
	else if (pktq_full(q)) {
		p = brcmu_pktq_peek_tail(q, &eprec);
		if (eprec > prec) {
			wiphy_err(wlc->wiphy, "%s: Failing: eprec %d > prec %d"
				  "\n", __func__, eprec, prec);
			return false;
		}
	}

	/* Evict if needed */
	if (eprec >= 0) {
		bool discard_oldest;

		discard_oldest = AC_BITMAP_TST(wlc->wme_dp, eprec);

		/* Refuse newer packet unless configured to discard oldest */
		if (eprec == prec && !discard_oldest) {
			wiphy_err(wlc->wiphy, "%s: No where to go, prec == %d"
				  "\n", __func__, prec);
			return false;
		}

		/* Evict packet according to discard policy */
		p = discard_oldest ? brcmu_pktq_pdeq(q, eprec) :
			brcmu_pktq_pdeq_tail(q, eprec);
		brcmu_pkt_buf_free_skb(p);
	}

	/* Enqueue */
	if (head)
		p = brcmu_pktq_penq_head(q, prec, pkt);
	else
		p = brcmu_pktq_penq(q, prec, pkt);

	return true;
}

void brcms_c_txq_enq(struct brcms_c_info *wlc, struct scb *scb,
		     struct sk_buff *sdu, uint prec)
{
	struct brcms_txq_info *qi = wlc->pkt_queue;	/* Check me */
	struct pktq *q = &qi->q;
	int prio;

	prio = sdu->priority;

	if (!brcms_c_prec_enq(wlc, q, sdu, prec)) {
		if (!EDCF_ENAB(wlc->pub)
		    || (wlc->pub->wlfeatureflag & WL_SWFL_FLOWCONTROL))
			wiphy_err(wlc->wiphy, "wl%d: txq_enq: txq overflow"
				  "\n", wlc->pub->unit);

		/*
		 * we might hit this condtion in case
		 * packet flooding from mac80211 stack
		 */
		brcmu_pkt_buf_free_skb(sdu);
	}

	/*
	 * Check if flow control needs to be turned on after enqueuing the
	 * packet. Don't turn on flow control if EDCF is enabled. Driver
	 * would make the decision on what to drop instead of relying on
	 * stack to make the right decision
	 */
	if (!EDCF_ENAB(wlc->pub)
	    || (wlc->pub->wlfeatureflag & WL_SWFL_FLOWCONTROL)) {
		if (pktq_len(q) >= wlc->pub->tunables->datahiwat)
			brcms_c_txflowcontrol(wlc, qi, ON, ALLPRIO);
	} else if (wlc->pub->_priofc) {
		if (pktq_plen(q, wlc_prio2prec_map[prio]) >=
		    wlc->pub->tunables->datahiwat)
			brcms_c_txflowcontrol(wlc, qi, ON, prio);
	}
}

bool
brcms_c_sendpkt_mac80211(struct brcms_c_info *wlc, struct sk_buff *sdu,
		     struct ieee80211_hw *hw)
{
	u8 prio;
	uint fifo;
	struct scb *scb = &global_scb;
	struct ieee80211_hdr *d11_header = (struct ieee80211_hdr *)(sdu->data);

	/*
	 * 802.11 standard requires management traffic
	 * to go at highest priority
	 */
	prio = ieee80211_is_data(d11_header->frame_control) ? sdu->priority :
		MAXPRIO;
	fifo = prio2fifo[prio];
	if (unlikely
	    (brcms_c_d11hdrs_mac80211(
		wlc, hw, sdu, scb, 0, 1, fifo, 0, NULL, 0)))
		return -EINVAL;
	brcms_c_txq_enq(wlc, scb, sdu, BRCMS_PRIO_TO_PREC(prio));
	brcms_c_send_q(wlc);
	return 0;
}

void brcms_c_send_q(struct brcms_c_info *wlc)
{
	struct sk_buff *pkt[DOT11_MAXNUMFRAGS];
	int prec;
	u16 prec_map;
	int err = 0, i, count;
	uint fifo;
	struct brcms_txq_info *qi = wlc->pkt_queue;
	struct pktq *q = &qi->q;
	struct ieee80211_tx_info *tx_info;

	if (in_send_q)
		return;
	else
		in_send_q = true;

	prec_map = wlc->tx_prec_map;

	/* Send all the enq'd pkts that we can.
	 * Dequeue packets with precedence with empty HW fifo only
	 */
	while (prec_map && (pkt[0] = brcmu_pktq_mdeq(q, prec_map, &prec))) {
		tx_info = IEEE80211_SKB_CB(pkt[0]);
		if (tx_info->flags & IEEE80211_TX_CTL_AMPDU) {
			err = brcms_c_sendampdu(wlc->ampdu, qi, pkt, prec);
		} else {
			count = 1;
			err = brcms_c_prep_pdu(wlc, pkt[0], &fifo);
			if (!err) {
				for (i = 0; i < count; i++)
					brcms_c_txfifo(wlc, fifo, pkt[i], true,
						       1);
			}
		}

		if (err == -EBUSY) {
			brcmu_pktq_penq_head(q, prec, pkt[0]);
			/*
			 * If send failed due to any other reason than a
			 * change in HW FIFO condition, quit. Otherwise,
			 * read the new prec_map!
			 */
			if (prec_map == wlc->tx_prec_map)
				break;
			prec_map = wlc->tx_prec_map;
		}
	}

	/*
	 * Check if flow control needs to be turned off after
	 * sending the packet
	 */
	if (!EDCF_ENAB(wlc->pub)
	    || (wlc->pub->wlfeatureflag & WL_SWFL_FLOWCONTROL)) {
		if (brcms_c_txflowcontrol_prio_isset(wlc, qi, ALLPRIO)
		    && (pktq_len(q) < wlc->pub->tunables->datahiwat / 2))
			brcms_c_txflowcontrol(wlc, qi, OFF, ALLPRIO);
	} else if (wlc->pub->_priofc) {
		int prio;
		for (prio = MAXPRIO; prio >= 0; prio--) {
			if (brcms_c_txflowcontrol_prio_isset(wlc, qi, prio) &&
			    (pktq_plen(q, wlc_prio2prec_map[prio]) <
			    wlc->pub->tunables->datahiwat / 2))
				brcms_c_txflowcontrol(wlc, qi, OFF, prio);
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
bcmc_fid_generate(struct brcms_c_info *wlc, struct brcms_bss_cfg *bsscfg,
		  struct d11txh *txh)
{
	u16 frameid;

	frameid = le16_to_cpu(txh->TxFrameID) & ~(TXFID_SEQ_MASK |
						  TXFID_QUEUE_MASK);
	frameid |=
	    (((wlc->
	       mc_fid_counter++) << TXFID_SEQ_SHIFT) & TXFID_SEQ_MASK) |
	    TX_BCMC_FIFO;

	return frameid;
}

void
brcms_c_txfifo(struct brcms_c_info *wlc, uint fifo, struct sk_buff *p,
	       bool commit, s8 txpktpend)
{
	u16 frameid = INVALIDFID;
	struct d11txh *txh;

	txh = (struct d11txh *) (p->data);

	/* When a BC/MC frame is being committed to the BCMC fifo
	 * via DMA (NOT PIO), update ucode or BSS info as appropriate.
	 */
	if (fifo == TX_BCMC_FIFO)
		frameid = le16_to_cpu(txh->TxFrameID);

	if (BRCMS_WAR16165(wlc))
		brcms_c_war16165(wlc, true);


	/*
	 * Bump up pending count for if not using rpc. If rpc is
	 * used, this will be handled in brcms_b_txfifo()
	 */
	if (commit) {
		TXPKTPENDINC(wlc, fifo, txpktpend);
		BCMMSG(wlc->wiphy, "pktpend inc %d to %d\n",
			 txpktpend, TXPKTPENDGET(wlc, fifo));
	}

	/* Commit BCMC sequence number in the SHM frame ID location */
	if (frameid != INVALIDFID)
		BCMCFID(wlc, frameid);

	if (dma_txfast(wlc->hw->di[fifo], p, commit) < 0)
		wiphy_err(wlc->wiphy, "txfifo: fatal, toss frames !!!\n");
}

void
brcms_c_compute_plcp(struct brcms_c_info *wlc, u32 rspec,
		     uint length, u8 *plcp)
{
	if (IS_MCS(rspec))
		brcms_c_compute_mimo_plcp(rspec, length, plcp);
	else if (IS_OFDM(rspec))
		brcms_c_compute_ofdm_plcp(rspec, length, plcp);
	else
		brcms_c_compute_cck_plcp(wlc, rspec, length, plcp);
	return;
}

/* Rate: 802.11 rate code, length: PSDU length in octets */
static void brcms_c_compute_mimo_plcp(u32 rspec, uint length, u8 *plcp)
{
	u8 mcs = (u8) (rspec & RSPEC_RATE_MASK);
	plcp[0] = mcs;
	if (RSPEC_IS40MHZ(rspec) || (mcs == 32))
		plcp[0] |= MIMO_PLCP_40MHZ;
	BRCMS_SET_MIMO_PLCP_LEN(plcp, length);
	plcp[3] = RSPEC_MIMOPLCP3(rspec); /* rspec already holds this byte */
	plcp[3] |= 0x7; /* set smoothing, not sounding ppdu & reserved */
	plcp[4] = 0; /* number of extension spatial streams bit 0 & 1 */
	plcp[5] = 0;
}

/* Rate: 802.11 rate code, length: PSDU length in octets */
static void
brcms_c_compute_ofdm_plcp(u32 rspec, u32 length, u8 *plcp)
{
	u8 rate_signal;
	u32 tmp = 0;
	int rate = RSPEC2RATE(rspec);

	/*
	 * encode rate per 802.11a-1999 sec 17.3.4.1, with lsb
	 * transmitted first
	 */
	rate_signal = rate_info[rate] & BRCMS_RATE_MASK;
	memset(plcp, 0, D11_PHY_HDR_LEN);
	D11A_PHY_HDR_SRATE((struct ofdm_phy_hdr *) plcp, rate_signal);

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

static void brcms_c_cck_plcp_set(struct brcms_c_info *wlc, int rate_500,
			     uint length, u8 *plcp)
{
	u16 usec = 0;
	u8 le = 0;

	switch (rate_500) {
	case BRCM_RATE_1M:
		usec = length << 3;
		break;
	case BRCM_RATE_2M:
		usec = length << 2;
		break;
	case BRCM_RATE_5M5:
		usec = (length << 4) / 11;
		if ((length << 4) - (usec * 11) > 0)
			usec++;
		break;
	case BRCM_RATE_11M:
		usec = (length << 3) / 11;
		if ((length << 3) - (usec * 11) > 0) {
			usec++;
			if ((usec * 11) - (length << 3) >= 8)
				le = D11B_PLCP_SIGNAL_LE;
		}
		break;

	default:
		wiphy_err(wlc->wiphy,
			  "brcms_c_cck_plcp_set: unsupported rate %d\n",
			  rate_500);
		rate_500 = BRCM_RATE_1M;
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
static void brcms_c_compute_cck_plcp(struct brcms_c_info *wlc, u32 rspec,
				 uint length, u8 *plcp)
{
	int rate = RSPEC2RATE(rspec);

	brcms_c_cck_plcp_set(wlc, rate, length, plcp);
}

/* brcms_c_compute_frame_dur()
 *
 * Calculate the 802.11 MAC header DUR field for MPDU
 * DUR for a single frame = 1 SIFS + 1 ACK
 * DUR for a frame with following frags = 3 SIFS + 2 ACK + next frag time
 *
 * rate			MPDU rate in unit of 500kbps
 * next_frag_len	next MPDU length in bytes
 * preamble_type	use short/GF or long/MM PLCP header
 */
static u16
brcms_c_compute_frame_dur(struct brcms_c_info *wlc, u32 rate,
		      u8 preamble_type, uint next_frag_len)
{
	u16 dur, sifs;

	sifs = SIFS(wlc->band);

	dur = sifs;
	dur += (u16) brcms_c_calc_ack_time(wlc, rate, preamble_type);

	if (next_frag_len) {
		/* Double the current DUR to get 2 SIFS + 2 ACKs */
		dur *= 2;
		/* add another SIFS and the frag time */
		dur += sifs;
		dur +=
		    (u16) brcms_c_calc_frame_time(wlc, rate, preamble_type,
						 next_frag_len);
	}
	return dur;
}

/* brcms_c_compute_rtscts_dur()
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
u16
brcms_c_compute_rtscts_dur(struct brcms_c_info *wlc, bool cts_only,
			   u32 rts_rate,
			   u32 frame_rate, u8 rts_preamble_type,
			   u8 frame_preamble_type, uint frame_len, bool ba)
{
	u16 dur, sifs;

	sifs = SIFS(wlc->band);

	if (!cts_only) {
		/* RTS/CTS */
		dur = 3 * sifs;
		dur +=
		    (u16) brcms_c_calc_cts_time(wlc, rts_rate,
					       rts_preamble_type);
	} else {
		/* CTS-TO-SELF */
		dur = 2 * sifs;
	}

	dur +=
	    (u16) brcms_c_calc_frame_time(wlc, frame_rate, frame_preamble_type,
					 frame_len);
	if (ba)
		dur +=
		    (u16) brcms_c_calc_ba_time(wlc, frame_rate,
					      BRCMS_SHORT_PREAMBLE);
	else
		dur +=
		    (u16) brcms_c_calc_ack_time(wlc, frame_rate,
					       frame_preamble_type);
	return dur;
}

u16 brcms_c_phytxctl1_calc(struct brcms_c_info *wlc, u32 rspec)
{
	u16 phyctl1 = 0;
	u16 bw;

	if (BRCMS_ISLCNPHY(wlc->band)) {
		bw = PHY_TXC1_BW_20MHZ;
	} else {
		bw = RSPEC_GET_BW(rspec);
		/* 10Mhz is not supported yet */
		if (bw < PHY_TXC1_BW_20MHZ) {
			wiphy_err(wlc->wiphy, "phytxctl1_calc: bw %d is "
				  "not supported yet, set to 20L\n", bw);
			bw = PHY_TXC1_BW_20MHZ;
		}
	}

	if (IS_MCS(rspec)) {
		uint mcs = rspec & RSPEC_RATE_MASK;

		/* bw, stf, coding-type is part of RSPEC_PHYTXBYTE2 returns */
		phyctl1 = RSPEC_PHYTXBYTE2(rspec);
		/* set the upper byte of phyctl1 */
		phyctl1 |= (mcs_table[mcs].tx_phy_ctl3 << 8);
	} else if (IS_CCK(rspec) && !BRCMS_ISLCNPHY(wlc->band)
		   && !BRCMS_ISSSLPNPHY(wlc->band)) {
		/*
		 * In CCK mode LPPHY overloads OFDM Modulation bits with CCK
		 * Data Rate. Eventually MIMOPHY would also be converted to
		 * this format
		 */
		/* 0 = 1Mbps; 1 = 2Mbps; 2 = 5.5Mbps; 3 = 11Mbps */
		phyctl1 = (bw | (RSPEC_STF(rspec) << PHY_TXC1_MODE_SHIFT));
	} else {		/* legacy OFDM/CCK */
		s16 phycfg;
		/* get the phyctl byte from rate phycfg table */
		phycfg = brcms_c_rate_legacy_phyctl(RSPEC2RATE(rspec));
		if (phycfg == -1) {
			wiphy_err(wlc->wiphy, "phytxctl1_calc: wrong "
				  "legacy OFDM/CCK rate\n");
			phycfg = 0;
		}
		/* set the upper byte of phyctl1 */
		phyctl1 =
		    (bw | (phycfg << 8) |
		     (RSPEC_STF(rspec) << PHY_TXC1_MODE_SHIFT));
	}
	return phyctl1;
}

u32
brcms_c_rspec_to_rts_rspec(struct brcms_c_info *wlc, u32 rspec,
			   bool use_rspec, u16 mimo_ctlchbw)
{
	u32 rts_rspec = 0;

	if (use_rspec)
		/* use frame rate as rts rate */
		rts_rspec = rspec;
	else if (wlc->band->gmode && wlc->protection->_g && !IS_CCK(rspec))
		/* Use 11Mbps as the g protection RTS target rate and fallback.
		 * Use the BRCMS_BASIC_RATE() lookup to find the best basic rate
		 * under the target in case 11 Mbps is not Basic.
		 * 6 and 9 Mbps are not usually selected by rate selection, but
		 * even if the OFDM rate we are protecting is 6 or 9 Mbps, 11
		 * is more robust.
		 */
		rts_rspec = BRCMS_BASIC_RATE(wlc, BRCM_RATE_11M);
	else
		/* calculate RTS rate and fallback rate based on the frame rate
		 * RTS must be sent at a basic rate since it is a
		 * control frame, sec 9.6 of 802.11 spec
		 */
		rts_rspec = BRCMS_BASIC_RATE(wlc, rspec);

	if (BRCMS_PHY_11N_CAP(wlc->band)) {
		/* set rts txbw to correct side band */
		rts_rspec &= ~RSPEC_BW_MASK;

		/*
		 * if rspec/rspec_fallback is 40MHz, then send RTS on both
		 * 20MHz channel (DUP), otherwise send RTS on control channel
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
 * Add struct d11txh, struct cck_phy_hdr.
 *
 * 'p' data must start with 802.11 MAC header
 * 'p' must allow enough bytes of local headers to be "pushed" onto the packet
 *
 * headroom == D11_PHY_HDR_LEN + D11_TXH_LEN (D11_TXH_LEN is now 104 bytes)
 *
 */
static u16
brcms_c_d11hdrs_mac80211(struct brcms_c_info *wlc, struct ieee80211_hw *hw,
		     struct sk_buff *p, struct scb *scb, uint frag,
		     uint nfrags, uint queue, uint next_frag_len,
		     struct wsec_key *key, u32 rspec_override)
{
	struct ieee80211_hdr *h;
	struct d11txh *txh;
	u8 *plcp, plcp_fallback[D11_PHY_HDR_LEN];
	int len, phylen, rts_phylen;
	u16 mch, phyctl, xfts, mainrates;
	u16 seq = 0, mcl = 0, status = 0, frameid = 0;
	u32 rspec[2] = { BRCM_RATE_1M, BRCM_RATE_1M }, rts_rspec[2] = {
	BRCM_RATE_1M, BRCM_RATE_1M};
	bool use_rts = false;
	bool use_cts = false;
	bool use_rifs = false;
	bool short_preamble[2] = { false, false };
	u8 preamble_type[2] = { BRCMS_LONG_PREAMBLE, BRCMS_LONG_PREAMBLE };
	u8 rts_preamble_type[2] = { BRCMS_LONG_PREAMBLE, BRCMS_LONG_PREAMBLE };
	u8 *rts_plcp, rts_plcp_fallback[D11_PHY_HDR_LEN];
	struct ieee80211_rts *rts = NULL;
	bool qos;
	uint ac;
	u32 rate_val[2];
	bool hwtkmic = false;
	u16 mimo_ctlchbw = PHY_TXC1_BW_20MHZ;
#define ANTCFG_NONE 0xFF
	u8 antcfg = ANTCFG_NONE;
	u8 fbantcfg = ANTCFG_NONE;
	uint phyctl1_stf = 0;
	u16 durid = 0;
	struct ieee80211_tx_rate *txrate[2];
	int k;
	struct ieee80211_tx_info *tx_info;
	bool is_mcs[2];
	u16 mimo_txbw;
	u8 mimo_preamble_type;

	/* locate 802.11 MAC header */
	h = (struct ieee80211_hdr *)(p->data);
	qos = ieee80211_is_data_qos(h->frame_control);

	/* compute length of frame in bytes for use in PLCP computations */
	len = brcmu_pkttotlen(p);
	phylen = len + FCS_LEN;

	/* If WEP enabled, add room in phylen for the additional bytes of
	 * ICV which MAC generates.  We do NOT add the additional bytes to
	 * the packet itself, thus phylen = packet length + ICV_LEN + FCS_LEN
	 * in this case
	 */
	if (key)
		phylen += key->icv_len;

	/* Get tx_info */
	tx_info = IEEE80211_SKB_CB(p);

	/* add PLCP */
	plcp = skb_push(p, D11_PHY_HDR_LEN);

	/* add Broadcom tx descriptor header */
	txh = (struct d11txh *) skb_push(p, D11_TXH_LEN);
	memset(txh, 0, D11_TXH_LEN);

	/* setup frameid */
	if (tx_info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		/* non-AP STA should never use BCMC queue */
		if (queue == TX_BCMC_FIFO) {
			wiphy_err(wlc->wiphy, "wl%d: %s: ASSERT queue == "
				  "TX_BCMC!\n", BRCMS_UNIT(wlc), __func__);
			frameid = bcmc_fid_generate(wlc, NULL, txh);
		} else {
			/* Increment the counter for first fragment */
			if (tx_info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
				SCB_SEQNUM(scb, p->priority)++;

			/* extract fragment number from frame first */
			seq = le16_to_cpu(seq) & FRAGNUM_MASK;
			seq |= (SCB_SEQNUM(scb, p->priority) << SEQNUM_SHIFT);
			h->seq_ctrl = cpu_to_le16(seq);

			frameid = ((seq << TXFID_SEQ_SHIFT) & TXFID_SEQ_MASK) |
			    (queue & TXFID_QUEUE_MASK);
		}
	}
	frameid |= queue & TXFID_QUEUE_MASK;

	/* set the ignpmq bit for all pkts tx'd in PS mode and for beacons */
	if (SCB_PS(scb) || ieee80211_is_beacon(h->frame_control))
		mcl |= TXC_IGNOREPMQ;

	txrate[0] = tx_info->control.rates;
	txrate[1] = txrate[0] + 1;

	/*
	 * if rate control algorithm didn't give us a fallback
	 * rate, use the primary rate
	 */
	if (txrate[1]->idx < 0)
		txrate[1] = txrate[0];

	for (k = 0; k < hw->max_rates; k++) {
		is_mcs[k] =
		    txrate[k]->flags & IEEE80211_TX_RC_MCS ? true : false;
		if (!is_mcs[k]) {
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
				rate_val[k] = BRCM_RATE_1M;
			}
		} else {
			rate_val[k] = txrate[k]->idx;
		}

		/*
		 * Currently only support same setting for primay and
		 * fallback rates. Unify flags for each rate into a
		 * single value for the frame
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

		/*
		 * (1) RATE:
		 *   determine and validate primary rate
		 *   and fallback rates
		 */
		if (!RSPEC_ACTIVE(rspec[k])) {
			rspec[k] = BRCM_RATE_1M;
		} else {
			if (!is_multicast_ether_addr(h->addr1)) {
				/* set tx antenna config */
				brcms_c_antsel_antcfg_get(wlc->asi, false,
					false, 0, 0, &antcfg, &fbantcfg);
			}
		}
	}

	phyctl1_stf = wlc->stf->ss_opmode;

	if (N_ENAB(wlc->pub)) {
		for (k = 0; k < hw->max_rates; k++) {
			/*
			 * apply siso/cdd to single stream mcs's or ofdm
			 * if rspec is auto selected
			 */
			if (((IS_MCS(rspec[k]) &&
			      IS_SINGLE_STREAM(rspec[k] & RSPEC_RATE_MASK)) ||
			     IS_OFDM(rspec[k]))
			    && ((rspec[k] & RSPEC_OVERRIDE_MCS_ONLY)
				|| !(rspec[k] & RSPEC_OVERRIDE))) {
				rspec[k] &= ~(RSPEC_STF_MASK | RSPEC_STC_MASK);

				/* For SISO MCS use STBC if possible */
				if (IS_MCS(rspec[k])
				    && BRCMS_STF_SS_STBC_TX(wlc, scb)) {
					u8 stc;

					/* Nss for single stream is always 1 */
					stc = 1;
					rspec[k] |= (PHY_TXC1_MODE_STBC <<
							RSPEC_STF_SHIFT) |
						    (stc << RSPEC_STC_SHIFT);
				} else
					rspec[k] |=
					    (phyctl1_stf << RSPEC_STF_SHIFT);
			}

			/*
			 * Is the phy configured to use 40MHZ frames? If
			 * so then pick the desired txbw
			 */
			if (CHSPEC_WLC_BW(wlc->chanspec) == BRCMS_40_MHZ) {
				/* default txbw is 20in40 SB */
				mimo_ctlchbw = mimo_txbw =
				   CHSPEC_SB_UPPER(BRCMS_BAND_PI_RADIO_CHANSPEC)
				   ? PHY_TXC1_BW_20MHZ_UP : PHY_TXC1_BW_20MHZ;

				if (IS_MCS(rspec[k])) {
					/* mcs 32 must be 40b/w DUP */
					if ((rspec[k] & RSPEC_RATE_MASK)
					    == 32) {
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
				} else if (wlc->cck_40txbw != AUTO) {
					mimo_txbw = wlc->cck_40txbw;
				}
			} else {
				/*
				 * mcs32 is 40 b/w only.
				 * This is possible for probe packets on
				 * a STA during SCAN
				 */
				if ((rspec[k] & RSPEC_RATE_MASK) == 32)
					/* mcs 0 */
					rspec[k] = RSPEC_MIMORATE;

				mimo_txbw = PHY_TXC1_BW_20MHZ;
			}

			/* Set channel width */
			rspec[k] &= ~RSPEC_BW_MASK;
			if ((k == 0) || ((k > 0) && IS_MCS(rspec[k])))
				rspec[k] |= (mimo_txbw << RSPEC_BW_SHIFT);
			else
				rspec[k] |= (mimo_ctlchbw << RSPEC_BW_SHIFT);

			/* Disable short GI, not supported yet */
			rspec[k] &= ~RSPEC_SHORT_GI;

			mimo_preamble_type = BRCMS_MM_PREAMBLE;
			if (txrate[k]->flags & IEEE80211_TX_RC_GREEN_FIELD)
				mimo_preamble_type = BRCMS_GF_PREAMBLE;

			if ((txrate[k]->flags & IEEE80211_TX_RC_MCS)
			    && (!IS_MCS(rspec[k]))) {
				wiphy_err(wlc->wiphy, "wl%d: %s: IEEE80211_TX_"
					  "RC_MCS != IS_MCS(rspec)\n",
					  BRCMS_UNIT(wlc), __func__);
			}

			if (IS_MCS(rspec[k])) {
				preamble_type[k] = mimo_preamble_type;

				/*
				 * if SGI is selected, then forced mm
				 * for single stream
				 */
				if ((rspec[k] & RSPEC_SHORT_GI)
				    && IS_SINGLE_STREAM(rspec[k] &
							RSPEC_RATE_MASK))
					preamble_type[k] = BRCMS_MM_PREAMBLE;
			}

			/* should be better conditionalized */
			if (!IS_MCS(rspec[0])
			    && (tx_info->control.rates[0].
				flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE))
				preamble_type[k] = BRCMS_SHORT_PREAMBLE;
		}
	} else {
		for (k = 0; k < hw->max_rates; k++) {
			/* Set ctrlchbw as 20Mhz */
			rspec[k] &= ~RSPEC_BW_MASK;
			rspec[k] |= (PHY_TXC1_BW_20MHZ << RSPEC_BW_SHIFT);

			/* for nphy, stf of ofdm frames must follow policies */
			if (BRCMS_ISNPHY(wlc->band) && IS_OFDM(rspec[k])) {
				rspec[k] &= ~RSPEC_STF_MASK;
				rspec[k] |= phyctl1_stf << RSPEC_STF_SHIFT;
			}
		}
	}

	/* Reset these for use with AMPDU's */
	txrate[0]->count = 0;
	txrate[1]->count = 0;

	/* (2) PROTECTION, may change rspec */
	if ((ieee80211_is_data(h->frame_control) ||
	    ieee80211_is_mgmt(h->frame_control)) &&
	    (phylen > wlc->RTSThresh) && !is_multicast_ether_addr(h->addr1))
		use_rts = true;

	/* (3) PLCP: determine PLCP header and MAC duration,
	 * fill struct d11txh */
	brcms_c_compute_plcp(wlc, rspec[0], phylen, plcp);
	brcms_c_compute_plcp(wlc, rspec[1], phylen, plcp_fallback);
	memcpy(&txh->FragPLCPFallback,
	       plcp_fallback, sizeof(txh->FragPLCPFallback));

	/* Length field now put in CCK FBR CRC field */
	if (IS_CCK(rspec[1])) {
		txh->FragPLCPFallback[4] = phylen & 0xff;
		txh->FragPLCPFallback[5] = (phylen & 0xff00) >> 8;
	}

	/* MIMO-RATE: need validation ?? */
	mainrates = IS_OFDM(rspec[0]) ?
			D11A_PHY_HDR_GRATE((struct ofdm_phy_hdr *) plcp) :
			plcp[0];

	/* DUR field for main rate */
	if (!ieee80211_is_pspoll(h->frame_control) &&
	    !is_multicast_ether_addr(h->addr1) && !use_rifs) {
		durid =
		    brcms_c_compute_frame_dur(wlc, rspec[0], preamble_type[0],
					  next_frag_len);
		h->duration_id = cpu_to_le16(durid);
	} else if (use_rifs) {
		/* NAV protect to end of next max packet size */
		durid =
		    (u16) brcms_c_calc_frame_time(wlc, rspec[0],
						 preamble_type[0],
						 DOT11_MAX_FRAG_LEN);
		durid += RIFS_11N_TIME;
		h->duration_id = cpu_to_le16(durid);
	}

	/* DUR field for fallback rate */
	if (ieee80211_is_pspoll(h->frame_control))
		txh->FragDurFallback = h->duration_id;
	else if (is_multicast_ether_addr(h->addr1) || use_rifs)
		txh->FragDurFallback = 0;
	else {
		durid = brcms_c_compute_frame_dur(wlc, rspec[1],
					      preamble_type[1], next_frag_len);
		txh->FragDurFallback = cpu_to_le16(durid);
	}

	/* (4) MAC-HDR: MacTxControlLow */
	if (frag == 0)
		mcl |= TXC_STARTMSDU;

	if (!is_multicast_ether_addr(h->addr1))
		mcl |= TXC_IMMEDACK;

	if (BAND_5G(wlc->band->bandtype))
		mcl |= TXC_FREQBAND_5G;

	if (CHSPEC_IS40(BRCMS_BAND_PI_RADIO_CHANSPEC))
		mcl |= TXC_BW_40;

	/* set AMIC bit if using hardware TKIP MIC */
	if (hwtkmic)
		mcl |= TXC_AMIC;

	txh->MacTxControlLow = cpu_to_le16(mcl);

	/* MacTxControlHigh */
	mch = 0;

	/* Set fallback rate preamble type */
	if ((preamble_type[1] == BRCMS_SHORT_PREAMBLE) ||
	    (preamble_type[1] == BRCMS_GF_PREAMBLE)) {
		if (RSPEC2RATE(rspec[1]) != BRCM_RATE_1M)
			mch |= TXC_PREAMBLE_DATA_FB_SHORT;
	}

	/* MacFrameControl */
	memcpy(&txh->MacFrameControl, &h->frame_control, sizeof(u16));
	txh->TxFesTimeNormal = cpu_to_le16(0);

	txh->TxFesTimeFallback = cpu_to_le16(0);

	/* TxFrameRA */
	memcpy(&txh->TxFrameRA, &h->addr1, ETH_ALEN);

	/* TxFrameID */
	txh->TxFrameID = cpu_to_le16(frameid);

	/*
	 * TxStatus, Note the case of recreating the first frag of a suppressed
	 * frame then we may need to reset the retry cnt's via the status reg
	 */
	txh->TxStatus = cpu_to_le16(status);

	/*
	 * extra fields for ucode AMPDU aggregation, the new fields are added to
	 * the END of previous structure so that it's compatible in driver.
	 */
	txh->MaxNMpdus = cpu_to_le16(0);
	txh->MaxABytes_MRT = cpu_to_le16(0);
	txh->MaxABytes_FBR = cpu_to_le16(0);
	txh->MinMBytes = cpu_to_le16(0);

	/* (5) RTS/CTS: determine RTS/CTS PLCP header and MAC duration,
	 * furnish struct d11txh */
	/* RTS PLCP header and RTS frame */
	if (use_rts || use_cts) {
		if (use_rts && use_cts)
			use_cts = false;

		for (k = 0; k < 2; k++) {
			rts_rspec[k] = brcms_c_rspec_to_rts_rspec(wlc, rspec[k],
							      false,
							      mimo_ctlchbw);
		}

		if (!IS_OFDM(rts_rspec[0]) &&
		    !((RSPEC2RATE(rts_rspec[0]) == BRCM_RATE_1M) ||
		      (wlc->PLCPHdr_override == BRCMS_PLCP_LONG))) {
			rts_preamble_type[0] = BRCMS_SHORT_PREAMBLE;
			mch |= TXC_PREAMBLE_RTS_MAIN_SHORT;
		}

		if (!IS_OFDM(rts_rspec[1]) &&
		    !((RSPEC2RATE(rts_rspec[1]) == BRCM_RATE_1M) ||
		      (wlc->PLCPHdr_override == BRCMS_PLCP_LONG))) {
			rts_preamble_type[1] = BRCMS_SHORT_PREAMBLE;
			mch |= TXC_PREAMBLE_RTS_FB_SHORT;
		}

		/* RTS/CTS additions to MacTxControlLow */
		if (use_cts) {
			txh->MacTxControlLow |= cpu_to_le16(TXC_SENDCTS);
		} else {
			txh->MacTxControlLow |= cpu_to_le16(TXC_SENDRTS);
			txh->MacTxControlLow |= cpu_to_le16(TXC_LONGFRAME);
		}

		/* RTS PLCP header */
		rts_plcp = txh->RTSPhyHeader;
		if (use_cts)
			rts_phylen = DOT11_CTS_LEN + FCS_LEN;
		else
			rts_phylen = DOT11_RTS_LEN + FCS_LEN;

		brcms_c_compute_plcp(wlc, rts_rspec[0], rts_phylen, rts_plcp);

		/* fallback rate version of RTS PLCP header */
		brcms_c_compute_plcp(wlc, rts_rspec[1], rts_phylen,
				 rts_plcp_fallback);
		memcpy(&txh->RTSPLCPFallback, rts_plcp_fallback,
		       sizeof(txh->RTSPLCPFallback));

		/* RTS frame fields... */
		rts = (struct ieee80211_rts *)&txh->rts_frame;

		durid = brcms_c_compute_rtscts_dur(wlc, use_cts, rts_rspec[0],
					       rspec[0], rts_preamble_type[0],
					       preamble_type[0], phylen, false);
		rts->duration = cpu_to_le16(durid);
		/* fallback rate version of RTS DUR field */
		durid = brcms_c_compute_rtscts_dur(wlc, use_cts,
					       rts_rspec[1], rspec[1],
					       rts_preamble_type[1],
					       preamble_type[1], phylen, false);
		txh->RTSDurFallback = cpu_to_le16(durid);

		if (use_cts) {
			rts->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
							 IEEE80211_STYPE_CTS);

			memcpy(&rts->ra, &h->addr2, ETH_ALEN);
		} else {
			rts->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
							 IEEE80211_STYPE_RTS);

			memcpy(&rts->ra, &h->addr1, 2 * ETH_ALEN);
		}

		/* mainrate
		 *    low 8 bits: main frag rate/mcs,
		 *    high 8 bits: rts/cts rate/mcs
		 */
		mainrates |= (IS_OFDM(rts_rspec[0]) ?
				D11A_PHY_HDR_GRATE(
					(struct ofdm_phy_hdr *) rts_plcp) :
				rts_plcp[0]) << 8;
	} else {
		memset((char *)txh->RTSPhyHeader, 0, D11_PHY_HDR_LEN);
		memset((char *)&txh->rts_frame, 0,
			sizeof(struct ieee80211_rts));
		memset((char *)txh->RTSPLCPFallback, 0,
		      sizeof(txh->RTSPLCPFallback));
		txh->RTSDurFallback = 0;
	}

#ifdef SUPPORT_40MHZ
	/* add null delimiter count */
	if ((tx_info->flags & IEEE80211_TX_CTL_AMPDU) && IS_MCS(rspec))
		txh->RTSPLCPFallback[AMPDU_FBR_NULL_DELIM] =
		   brcm_c_ampdu_null_delim_cnt(wlc->ampdu, scb, rspec, phylen);

#endif

	/*
	 * Now that RTS/RTS FB preamble types are updated, write
	 * the final value
	 */
	txh->MacTxControlHigh = cpu_to_le16(mch);

	/*
	 * MainRates (both the rts and frag plcp rates have
	 * been calculated now)
	 */
	txh->MainRates = cpu_to_le16(mainrates);

	/* XtraFrameTypes */
	xfts = FRAMETYPE(rspec[1], wlc->mimoft);
	xfts |= (FRAMETYPE(rts_rspec[0], wlc->mimoft) << XFTS_RTS_FT_SHIFT);
	xfts |= (FRAMETYPE(rts_rspec[1], wlc->mimoft) << XFTS_FBRRTS_FT_SHIFT);
	xfts |=
	    CHSPEC_CHANNEL(BRCMS_BAND_PI_RADIO_CHANSPEC) << XFTS_CHANNEL_SHIFT;
	txh->XtraFrameTypes = cpu_to_le16(xfts);

	/* PhyTxControlWord */
	phyctl = FRAMETYPE(rspec[0], wlc->mimoft);
	if ((preamble_type[0] == BRCMS_SHORT_PREAMBLE) ||
	    (preamble_type[0] == BRCMS_GF_PREAMBLE)) {
		if (RSPEC2RATE(rspec[0]) != BRCM_RATE_1M)
			phyctl |= PHY_TXC_SHORT_HDR;
	}

	/* phytxant is properly bit shifted */
	phyctl |= brcms_c_stf_d11hdrs_phyctl_txant(wlc, rspec[0]);
	txh->PhyTxControlWord = cpu_to_le16(phyctl);

	/* PhyTxControlWord_1 */
	if (BRCMS_PHY_11N_CAP(wlc->band)) {
		u16 phyctl1 = 0;

		phyctl1 = brcms_c_phytxctl1_calc(wlc, rspec[0]);
		txh->PhyTxControlWord_1 = cpu_to_le16(phyctl1);
		phyctl1 = brcms_c_phytxctl1_calc(wlc, rspec[1]);
		txh->PhyTxControlWord_1_Fbr = cpu_to_le16(phyctl1);

		if (use_rts || use_cts) {
			phyctl1 = brcms_c_phytxctl1_calc(wlc, rts_rspec[0]);
			txh->PhyTxControlWord_1_Rts = cpu_to_le16(phyctl1);
			phyctl1 = brcms_c_phytxctl1_calc(wlc, rts_rspec[1]);
			txh->PhyTxControlWord_1_FbrRts = cpu_to_le16(phyctl1);
		}

		/*
		 * For mcs frames, if mixedmode(overloaded with long preamble)
		 * is going to be set, fill in non-zero MModeLen and/or
		 * MModeFbrLen it will be unnecessary if they are separated
		 */
		if (IS_MCS(rspec[0]) &&
		    (preamble_type[0] == BRCMS_MM_PREAMBLE)) {
			u16 mmodelen =
			    brcms_c_calc_lsig_len(wlc, rspec[0], phylen);
			txh->MModeLen = cpu_to_le16(mmodelen);
		}

		if (IS_MCS(rspec[1]) &&
		    (preamble_type[1] == BRCMS_MM_PREAMBLE)) {
			u16 mmodefbrlen =
			    brcms_c_calc_lsig_len(wlc, rspec[1], phylen);
			txh->MModeFbrLen = cpu_to_le16(mmodefbrlen);
		}
	}

	ac = skb_get_queue_mapping(p);
	if (SCB_WME(scb) && qos && wlc->edcf_txop[ac]) {
		uint frag_dur, dur, dur_fallback;

		/* WME: Update TXOP threshold */
		if (!(tx_info->flags & IEEE80211_TX_CTL_AMPDU) && frag == 0) {
			frag_dur =
			    brcms_c_calc_frame_time(wlc, rspec[0],
					preamble_type[0], phylen);

			if (rts) {
				/* 1 RTS or CTS-to-self frame */
				dur =
				    brcms_c_calc_cts_time(wlc, rts_rspec[0],
						      rts_preamble_type[0]);
				dur_fallback =
				    brcms_c_calc_cts_time(wlc, rts_rspec[1],
						      rts_preamble_type[1]);
				/* (SIFS + CTS) + SIFS + frame + SIFS + ACK */
				dur += le16_to_cpu(rts->duration);
				dur_fallback +=
					le16_to_cpu(txh->RTSDurFallback);
			} else if (use_rifs) {
				dur = frag_dur;
				dur_fallback = 0;
			} else {
				/* frame + SIFS + ACK */
				dur = frag_dur;
				dur +=
				    brcms_c_compute_frame_dur(wlc, rspec[0],
							  preamble_type[0], 0);

				dur_fallback =
				    brcms_c_calc_frame_time(wlc, rspec[1],
							preamble_type[1],
							phylen);
				dur_fallback +=
				    brcms_c_compute_frame_dur(wlc, rspec[1],
							  preamble_type[1], 0);
			}
			/* NEED to set TxFesTimeNormal (hard) */
			txh->TxFesTimeNormal = cpu_to_le16((u16) dur);
			/*
			 * NEED to set fallback rate version of
			 * TxFesTimeNormal (hard)
			 */
			txh->TxFesTimeFallback =
				cpu_to_le16((u16) dur_fallback);

			/*
			 * update txop byte threshold (txop minus intraframe
			 * overhead)
			 */
			if (wlc->edcf_txop[ac] >= (dur - frag_dur)) {
				uint newfragthresh;

				newfragthresh =
				    brcms_c_calc_frame_len(wlc,
					rspec[0], preamble_type[0],
					(wlc->edcf_txop[ac] -
						(dur - frag_dur)));
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
				    (u16) newfragthresh)
					wlc->fragthresh[queue] =
					    (u16) newfragthresh;
			} else {
				wiphy_err(wlc->wiphy, "wl%d: %s txop invalid "
					  "for rate %d\n",
					  wlc->pub->unit, fifo_names[queue],
					  RSPEC2RATE(rspec[0]));
			}

			if (dur > wlc->edcf_txop[ac])
				wiphy_err(wlc->wiphy, "wl%d: %s: %s txop "
					  "exceeded phylen %d/%d dur %d/%d\n",
					  wlc->pub->unit, __func__,
					  fifo_names[queue],
					  phylen, wlc->fragthresh[queue],
					  dur, wlc->edcf_txop[ac]);
		}
	}

	return 0;
}

void brcms_c_tbtt(struct brcms_c_info *wlc)
{
	struct brcms_bss_cfg *cfg = wlc->cfg;

	if (!cfg->BSS)
		/*
		 * DirFrmQ is now valid...defer setting until end
		 * of ATIM window
		 */
		wlc->qvalid |= MCMD_DIRFRMQVAL;
}

static void brcms_c_war16165(struct brcms_c_info *wlc, bool tx)
{
	if (tx) {
		/* the post-increment is used in STAY_AWAKE macro */
		if (wlc->txpend16165war++ == 0)
			brcms_c_set_ps_ctrl(wlc);
	} else {
		wlc->txpend16165war--;
		if (wlc->txpend16165war == 0)
			brcms_c_set_ps_ctrl(wlc);
	}
}

/* process an individual struct tx_status */
bool
brcms_c_dotxstatus(struct brcms_c_info *wlc, struct tx_status *txs, u32 frm_tx2)
{
	struct sk_buff *p;
	uint queue;
	struct d11txh *txh;
	struct scb *scb = NULL;
	bool free_pdu;
	int tx_rts, tx_frame_count, tx_rts_count;
	uint totlen, supr_status;
	bool lastframe;
	struct ieee80211_hdr *h;
	u16 mcl;
	struct ieee80211_tx_info *tx_info;
	struct ieee80211_tx_rate *txrate;
	int i;

	/* Compiler reference to avoid unused variable warning */
	(void)(frm_tx2);

	/* discard intermediate indications for ucode with one legitimate case:
	 *   e.g. if "useRTS" is set. ucode did a successful rts/cts exchange,
	 *   but the subsequent tx of DATA failed. so it will start rts/cts
	 *   from the beginning (resetting the rts transmission count)
	 */
	if (!(txs->status & TX_STATUS_AMPDU)
	    && (txs->status & TX_STATUS_INTERMEDIATE)) {
		wiphy_err(wlc->wiphy, "%s: INTERMEDIATE but not AMPDU\n",
			  __func__);
		return false;
	}

	queue = txs->frameid & TXFID_QUEUE_MASK;
	if (queue >= NFIFO) {
		p = NULL;
		goto fatal;
	}

	p = GETNEXTTXP(wlc, queue);
	if (BRCMS_WAR16165(wlc))
		brcms_c_war16165(wlc, false);
	if (p == NULL)
		goto fatal;

	txh = (struct d11txh *) (p->data);
	mcl = le16_to_cpu(txh->MacTxControlLow);

	if (txs->phyerr) {
		if (WL_ERROR_ON()) {
			wiphy_err(wlc->wiphy, "phyerr 0x%x, rate 0x%x\n",
				  txs->phyerr, txh->MainRates);
			brcms_c_print_txdesc(txh);
		}
		brcms_c_print_txstatus(txs);
	}

	if (txs->frameid != cpu_to_le16(txh->TxFrameID))
		goto fatal;
	tx_info = IEEE80211_SKB_CB(p);
	h = (struct ieee80211_hdr *)((u8 *) (txh + 1) + D11_PHY_HDR_LEN);

	if (tx_info->control.sta)
		scb = (struct scb *)tx_info->control.sta->drv_priv;

	if (tx_info->flags & IEEE80211_TX_CTL_AMPDU) {
		brcms_c_ampdu_dotxstatus(wlc->ampdu, scb, p, txs);
		return false;
	}

	supr_status = txs->status & TX_STATUS_SUPR_MASK;
	if (supr_status == TX_STATUS_SUPR_BADCH)
		BCMMSG(wlc->wiphy,
		       "%s: Pkt tx suppressed, possibly channel %d\n",
		       __func__, CHSPEC_CHANNEL(wlc->default_bss->chanspec));

	tx_rts = cpu_to_le16(txh->MacTxControlLow) & TXC_SENDRTS;
	tx_frame_count =
	    (txs->status & TX_STATUS_FRM_RTX_MASK) >> TX_STATUS_FRM_RTX_SHIFT;
	tx_rts_count =
	    (txs->status & TX_STATUS_RTS_RTX_MASK) >> TX_STATUS_RTS_RTX_SHIFT;

	lastframe = !ieee80211_has_morefrags(h->frame_control);

	if (!lastframe) {
		wiphy_err(wlc->wiphy, "Not last frame!\n");
	} else {
		/*
		 * Set information to be consumed by Minstrel ht.
		 *
		 * The "fallback limit" is the number of tx attempts a given
		 * MPDU is sent at the "primary" rate. Tx attempts beyond that
		 * limit are sent at the "secondary" rate.
		 * A 'short frame' does not exceed RTS treshold.
		 */
		u16 sfbl,	/* Short Frame Rate Fallback Limit */
		    lfbl,	/* Long Frame Rate Fallback Limit */
		    fbl;

		if (queue < AC_COUNT) {
			sfbl = BRCMS_WME_RETRY_SFB_GET(wlc, wme_fifo2ac[queue]);
			lfbl = BRCMS_WME_RETRY_LFB_GET(wlc, wme_fifo2ac[queue]);
		} else {
			sfbl = wlc->SFBL;
			lfbl = wlc->LFBL;
		}

		txrate = tx_info->status.rates;
		if (txrate[0].flags & IEEE80211_TX_RC_USE_RTS_CTS)
			fbl = lfbl;
		else
			fbl = sfbl;

		ieee80211_tx_info_clear_status(tx_info);

		if ((tx_frame_count > fbl) && (txrate[1].idx >= 0)) {
			/*
			 * rate selection requested a fallback rate
			 * and we used it
			 */
			txrate[0].count = fbl;
			txrate[1].count = tx_frame_count - fbl;
		} else {
			/*
			 * rate selection did not request fallback rate, or
			 * we didn't need it
			 */
			txrate[0].count = tx_frame_count;
			/*
			 * rc80211_minstrel.c:minstrel_tx_status() expects
			 * unused rates to be marked with idx = -1
			 */
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

	totlen = brcmu_pkttotlen(p);
	free_pdu = true;

	brcms_c_txfifo_complete(wlc, queue, 1);

	if (lastframe) {
		p->next = NULL;
		p->prev = NULL;
		/* remove PLCP & Broadcom tx descriptor header */
		skb_pull(p, D11_PHY_HDR_LEN);
		skb_pull(p, D11_TXH_LEN);
		ieee80211_tx_status_irqsafe(wlc->pub->ieee_hw, p);
	} else {
		wiphy_err(wlc->wiphy, "%s: Not last frame => not calling "
			  "tx_status\n", __func__);
	}

	return false;

 fatal:
	if (p)
		brcmu_pkt_buf_free_skb(p);

	return true;

}

void
brcms_c_txfifo_complete(struct brcms_c_info *wlc, uint fifo, s8 txpktpend)
{
	TXPKTPENDDEC(wlc, fifo, txpktpend);
	BCMMSG(wlc->wiphy, "pktpend dec %d to %d\n", txpktpend,
		TXPKTPENDGET(wlc, fifo));

	/* There is more room; mark precedences related to this FIFO sendable */
	BRCMS_TX_FIFO_ENAB(wlc, fifo);

	/* Clear MHF2_TXBCMC_NOW flag if BCMC fifo has drained */
	if (AP_ENAB(wlc->pub) &&
	    !TXPKTPENDGET(wlc, TX_BCMC_FIFO))
		brcms_c_mhf(wlc, MHF2, MHF2_TXBCMC_NOW, 0, BRCM_BAND_AUTO);

	/* figure out which bsscfg is being worked on... */
}

/* Update beacon listen interval in shared memory */
void brcms_c_bcn_li_upd(struct brcms_c_info *wlc)
{
	if (AP_ENAB(wlc->pub))
		return;

	/* wake up every DTIM is the default */
	if (wlc->bcn_li_dtim == 1)
		brcms_c_write_shm(wlc, M_BCN_LI, 0);
	else
		brcms_c_write_shm(wlc, M_BCN_LI,
			      (wlc->bcn_li_dtim << 8) | wlc->bcn_li_bcn);
}

void
brcms_b_read_tsf(struct brcms_hardware *wlc_hw, u32 *tsf_l_ptr,
		  u32 *tsf_h_ptr)
{
	struct d11regs *regs = wlc_hw->regs;

	/* read the tsf timer low, then high to get an atomic read */
	*tsf_l_ptr = R_REG(&regs->tsf_timerlow);
	*tsf_h_ptr = R_REG(&regs->tsf_timerhigh);

	return;
}

/*
 * recover 64bit TSF value from the 16bit TSF value in the rx header
 * given the assumption that the TSF passed in header is within 65ms
 * of the current tsf.
 *
 * 6       5       4       4       3       2       1
 * 3.......6.......8.......0.......2.......4.......6.......8......0
 * |<---------- tsf_h ----------->||<--- tsf_l -->||<-RxTSFTime ->|
 *
 * The RxTSFTime are the lowest 16 bits and provided by the ucode. The
 * tsf_l is filled in by brcms_b_recv, which is done earlier in the
 * receive call sequence after rx interrupt. Only the higher 16 bits
 * are used. Finally, the tsf_h is read from the tsf register.
 */
static u64 brcms_c_recover_tsf64(struct brcms_c_info *wlc,
				 struct brcms_d11rxhdr *rxh)
{
	u32 tsf_h, tsf_l;
	u16 rx_tsf_0_15, rx_tsf_16_31;

	brcms_b_read_tsf(wlc->hw, &tsf_l, &tsf_h);

	rx_tsf_16_31 = (u16)(tsf_l >> 16);
	rx_tsf_0_15 = rxh->rxhdr.RxTSFTime;

	/*
	 * a greater tsf time indicates the low 16 bits of
	 * tsf_l wrapped, so decrement the high 16 bits.
	 */
	if ((u16)tsf_l < rx_tsf_0_15) {
		rx_tsf_16_31 -= 1;
		if (rx_tsf_16_31 == 0xffff)
			tsf_h -= 1;
	}

	return ((u64)tsf_h << 32) | (((u32)rx_tsf_16_31 << 16) + rx_tsf_0_15);
}

static void
prep_mac80211_status(struct brcms_c_info *wlc, struct d11rxhdr *rxh,
		     struct sk_buff *p,
		     struct ieee80211_rx_status *rx_status)
{
	struct brcms_d11rxhdr *wlc_rxh = (struct brcms_d11rxhdr *) rxh;
	int preamble;
	int channel;
	u32 rspec;
	unsigned char *plcp;

	/* fill in TSF and flag its presence */
	rx_status->mactime = brcms_c_recover_tsf64(wlc, wlc_rxh);
	rx_status->flag |= RX_FLAG_MACTIME_MPDU;

	channel = BRCMS_CHAN_CHANNEL(rxh->RxChan);

	if (channel > 14) {
		rx_status->band = IEEE80211_BAND_5GHZ;
		rx_status->freq = ieee80211_ofdm_chan_to_freq(
					WF_CHAN_FACTOR_5_G/2, channel);

	} else {
		rx_status->band = IEEE80211_BAND_2GHZ;
		rx_status->freq = ieee80211_dsss_chan_to_freq(channel);
	}

	rx_status->signal = wlc_rxh->rssi;

	/* noise */
	/* qual */
	rx_status->antenna =
		(rxh->PhyRxStatus_0 & PRXS0_RXANT_UPSUBBAND) ? 1 : 0;

	plcp = p->data;

	rspec = brcms_c_compute_rspec(rxh, plcp);
	if (IS_MCS(rspec)) {
		rx_status->rate_idx = rspec & RSPEC_RATE_MASK;
		rx_status->flag |= RX_FLAG_HT;
		if (RSPEC_IS40MHZ(rspec))
			rx_status->flag |= RX_FLAG_40MHZ;
	} else {
		switch (RSPEC2RATE(rspec)) {
		case BRCM_RATE_1M:
			rx_status->rate_idx = 0;
			break;
		case BRCM_RATE_2M:
			rx_status->rate_idx = 1;
			break;
		case BRCM_RATE_5M5:
			rx_status->rate_idx = 2;
			break;
		case BRCM_RATE_11M:
			rx_status->rate_idx = 3;
			break;
		case BRCM_RATE_6M:
			rx_status->rate_idx = 4;
			break;
		case BRCM_RATE_9M:
			rx_status->rate_idx = 5;
			break;
		case BRCM_RATE_12M:
			rx_status->rate_idx = 6;
			break;
		case BRCM_RATE_18M:
			rx_status->rate_idx = 7;
			break;
		case BRCM_RATE_24M:
			rx_status->rate_idx = 8;
			break;
		case BRCM_RATE_36M:
			rx_status->rate_idx = 9;
			break;
		case BRCM_RATE_48M:
			rx_status->rate_idx = 10;
			break;
		case BRCM_RATE_54M:
			rx_status->rate_idx = 11;
			break;
		default:
			wiphy_err(wlc->wiphy, "%s: Unknown rate\n", __func__);
		}

		/*
		 * For 5GHz, we should decrease the index as it is
		 * a subset of the 2.4G rates. See bitrates field
		 * of brcms_band_5GHz_nphy (in mac80211_if.c).
		 */
		if (rx_status->band == IEEE80211_BAND_5GHZ)
			rx_status->rate_idx -= BRCMS_LEGACY_5G_RATE_OFFSET;

		/* Determine short preamble and rate_idx */
		preamble = 0;
		if (IS_CCK(rspec)) {
			if (rxh->PhyRxStatus_0 & PRXS0_SHORTH)
				rx_status->flag |= RX_FLAG_SHORTPRE;
		} else if (IS_OFDM(rspec)) {
			rx_status->flag |= RX_FLAG_SHORTPRE;
		} else {
			wiphy_err(wlc->wiphy, "%s: Unknown modulation\n",
				  __func__);
		}
	}

	if (PLCP3_ISSGI(plcp[3]))
		rx_status->flag |= RX_FLAG_SHORT_GI;

	if (rxh->RxStatus1 & RXS_DECERR) {
		rx_status->flag |= RX_FLAG_FAILED_PLCP_CRC;
		wiphy_err(wlc->wiphy, "%s:  RX_FLAG_FAILED_PLCP_CRC\n",
			  __func__);
	}
	if (rxh->RxStatus1 & RXS_FCSERR) {
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
		wiphy_err(wlc->wiphy, "%s:  RX_FLAG_FAILED_FCS_CRC\n",
			  __func__);
	}
}

static void
brcms_c_recvctl(struct brcms_c_info *wlc, struct d11rxhdr *rxh,
		struct sk_buff *p)
{
	int len_mpdu;
	struct ieee80211_rx_status rx_status;

	memset(&rx_status, 0, sizeof(rx_status));
	prep_mac80211_status(wlc, rxh, p, &rx_status);

	/* mac header+body length, exclude CRC and plcp header */
	len_mpdu = p->len - D11_PHY_HDR_LEN - FCS_LEN;
	skb_pull(p, D11_PHY_HDR_LEN);
	__skb_trim(p, len_mpdu);

	memcpy(IEEE80211_SKB_RXCB(p), &rx_status, sizeof(rx_status));
	ieee80211_rx_irqsafe(wlc->pub->ieee_hw, p);
	return;
}

/* Process received frames */
/*
 * Return true if more frames need to be processed. false otherwise.
 * Param 'bound' indicates max. # frames to process before break out.
 */
void brcms_c_recv(struct brcms_c_info *wlc, struct sk_buff *p)
{
	struct d11rxhdr *rxh;
	struct ieee80211_hdr *h;
	uint len;
	bool is_amsdu;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc->pub->unit);

	/* frame starts with rxhdr */
	rxh = (struct d11rxhdr *) (p->data);

	/* strip off rxhdr */
	skb_pull(p, BRCMS_HWRXOFF);

	/* fixup rx header endianness */
	rxh->RxFrameSize = le16_to_cpu(rxh->RxFrameSize);
	rxh->PhyRxStatus_0 = le16_to_cpu(rxh->PhyRxStatus_0);
	rxh->PhyRxStatus_1 = le16_to_cpu(rxh->PhyRxStatus_1);
	rxh->PhyRxStatus_2 = le16_to_cpu(rxh->PhyRxStatus_2);
	rxh->PhyRxStatus_3 = le16_to_cpu(rxh->PhyRxStatus_3);
	rxh->PhyRxStatus_4 = le16_to_cpu(rxh->PhyRxStatus_4);
	rxh->PhyRxStatus_5 = le16_to_cpu(rxh->PhyRxStatus_5);
	rxh->RxStatus1 = le16_to_cpu(rxh->RxStatus1);
	rxh->RxStatus2 = le16_to_cpu(rxh->RxStatus2);
	rxh->RxTSFTime = le16_to_cpu(rxh->RxTSFTime);
	rxh->RxChan = le16_to_cpu(rxh->RxChan);

	/* MAC inserts 2 pad bytes for a4 headers or QoS or A-MSDU subframes */
	if (rxh->RxStatus1 & RXS_PBPRES) {
		if (p->len < 2) {
			wiphy_err(wlc->wiphy, "wl%d: recv: rcvd runt of "
				  "len %d\n", wlc->pub->unit, p->len);
			goto toss;
		}
		skb_pull(p, 2);
	}

	h = (struct ieee80211_hdr *)(p->data + D11_PHY_HDR_LEN);
	len = p->len;

	if (rxh->RxStatus1 & RXS_FCSERR) {
		if (wlc->pub->mac80211_state & MAC80211_PROMISC_BCNS) {
			wiphy_err(wlc->wiphy, "FCSERR while scanning******* -"
				  " tossing\n");
			goto toss;
		} else {
			wiphy_err(wlc->wiphy, "RCSERR!!!\n");
			goto toss;
		}
	}

	/* check received pkt has at least frame control field */
	if (len < D11_PHY_HDR_LEN + sizeof(h->frame_control))
		goto toss;

	is_amsdu = rxh->RxStatus2 & RXS_AMSDU_MASK;

	/* explicitly test bad src address to avoid sending bad deauth */
	if (!is_amsdu) {
		/* CTS and ACK CTL frames are w/o a2 */

		if (ieee80211_is_data(h->frame_control) ||
		    ieee80211_is_mgmt(h->frame_control)) {
			if ((is_zero_ether_addr(h->addr2) ||
			     is_multicast_ether_addr(h->addr2))) {
				wiphy_err(wlc->wiphy, "wl%d: %s: dropping a "
					  "frame with invalid src mac address,"
					  " a2: %pM\n",
					 wlc->pub->unit, __func__, h->addr2);
				goto toss;
			}
		}
	}

	/* due to sheer numbers, toss out probe reqs for now */
	if (ieee80211_is_probe_req(h->frame_control))
		goto toss;

	if (is_amsdu)
		goto toss;

	brcms_c_recvctl(wlc, rxh, p);
	return;

 toss:
	brcmu_pkt_buf_free_skb(p);
}

/* calculate frame duration for Mixed-mode L-SIG spoofing, return
 * number of bytes goes in the length field
 *
 * Formula given by HT PHY Spec v 1.13
 *   len = 3(nsyms + nstream + 3) - 3
 */
u16
brcms_c_calc_lsig_len(struct brcms_c_info *wlc, u32 ratespec,
		      uint mac_len)
{
	uint nsyms, len = 0, kNdps;

	BCMMSG(wlc->wiphy, "wl%d: rate %d, len%d\n",
		 wlc->pub->unit, RSPEC2RATE(ratespec), mac_len);

	if (IS_MCS(ratespec)) {
		uint mcs = ratespec & RSPEC_RATE_MASK;
		/* MCS_TXS(mcs) returns num tx streams - 1 */
		int tot_streams = (MCS_TXS(mcs) + 1) + RSPEC_STC(ratespec);

		/*
		 * the payload duration calculation matches that
		 * of regular ofdm
		 */
		/* 1000Ndbps = kbps * 4 */
		kNdps =
		    MCS_RATE(mcs, RSPEC_IS40MHZ(ratespec),
			     RSPEC_ISSGI(ratespec)) * 4;

		if (RSPEC_STC(ratespec) == 0)
			nsyms =
			    CEIL((APHY_SERVICE_NBITS + 8 * mac_len +
				  APHY_TAIL_NBITS) * 1000, kNdps);
		else
			/* STBC needs to have even number of symbols */
			nsyms =
			    2 *
			    CEIL((APHY_SERVICE_NBITS + 8 * mac_len +
				  APHY_TAIL_NBITS) * 1000, 2 * kNdps);

		/* (+3) account for HT-SIG(2) and HT-STF(1) */
		nsyms += (tot_streams + 3);
		/*
		 * 3 bytes/symbol @ legacy 6Mbps rate
		 * (-3) excluding service bits and tail bits
		 */
		len = (3 * nsyms) - 3;
	}

	return (u16) len;
}

/*
 * calculate frame duration of a given rate and length, return
 * time in usec unit
 */
uint
brcms_c_calc_frame_time(struct brcms_c_info *wlc, u32 ratespec,
			u8 preamble_type, uint mac_len)
{
	uint nsyms, dur = 0, Ndps, kNdps;
	uint rate = RSPEC2RATE(ratespec);

	if (rate == 0) {
		wiphy_err(wlc->wiphy, "wl%d: WAR: using rate of 1 mbps\n",
			  wlc->pub->unit);
		rate = BRCM_RATE_1M;
	}

	BCMMSG(wlc->wiphy, "wl%d: rspec 0x%x, preamble_type %d, len%d\n",
		 wlc->pub->unit, ratespec, preamble_type, mac_len);

	if (IS_MCS(ratespec)) {
		uint mcs = ratespec & RSPEC_RATE_MASK;
		int tot_streams = MCS_TXS(mcs) + RSPEC_STC(ratespec);

		dur = PREN_PREAMBLE + (tot_streams * PREN_PREAMBLE_EXT);
		if (preamble_type == BRCMS_MM_PREAMBLE)
			dur += PREN_MM_EXT;
		/* 1000Ndbps = kbps * 4 */
		kNdps =
		    MCS_RATE(mcs, RSPEC_IS40MHZ(ratespec),
			     RSPEC_ISSGI(ratespec)) * 4;

		if (RSPEC_STC(ratespec) == 0)
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
		/*
		 * calc # bits * 2 so factor of 2 in rate (1/2 mbps)
		 * will divide out
		 */
		mac_len = mac_len * 8 * 2;
		/* calc ceiling of bits/rate = microseconds of air time */
		dur = (mac_len + rate - 1) / rate;
		if (preamble_type & BRCMS_SHORT_PREAMBLE)
			dur += BPHY_PLCP_SHORT_TIME;
		else
			dur += BPHY_PLCP_TIME;
	}
	return dur;
}

/* The opposite of brcms_c_calc_frame_time */
static uint
brcms_c_calc_frame_len(struct brcms_c_info *wlc, u32 ratespec,
		   u8 preamble_type, uint dur)
{
	uint nsyms, mac_len, Ndps, kNdps;
	uint rate = RSPEC2RATE(ratespec);

	BCMMSG(wlc->wiphy, "wl%d: rspec 0x%x, preamble_type %d, dur %d\n",
		 wlc->pub->unit, ratespec, preamble_type, dur);

	if (IS_MCS(ratespec)) {
		uint mcs = ratespec & RSPEC_RATE_MASK;
		int tot_streams = MCS_TXS(mcs) + RSPEC_STC(ratespec);
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
		if (preamble_type & BRCMS_SHORT_PREAMBLE)
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
brcms_c_calc_ba_time(struct brcms_c_info *wlc, u32 rspec,
		     u8 preamble_type)
{
	BCMMSG(wlc->wiphy, "wl%d: rspec 0x%x, "
		 "preamble_type %d\n", wlc->pub->unit, rspec, preamble_type);
	/*
	 * Spec 9.6: ack rate is the highest rate in BSSBasicRateSet that
	 * is less than or equal to the rate of the immediately previous
	 * frame in the FES
	 */
	rspec = BRCMS_BASIC_RATE(wlc, rspec);
	/* BA len == 32 == 16(ctl hdr) + 4(ba len) + 8(bitmap) + 4(fcs) */
	return brcms_c_calc_frame_time(wlc, rspec, preamble_type,
				   (DOT11_BA_LEN + DOT11_BA_BITMAP_LEN +
				    FCS_LEN));
}

static uint
brcms_c_calc_ack_time(struct brcms_c_info *wlc, u32 rspec,
		      u8 preamble_type)
{
	uint dur = 0;

	BCMMSG(wlc->wiphy, "wl%d: rspec 0x%x, preamble_type %d\n",
		wlc->pub->unit, rspec, preamble_type);
	/*
	 * Spec 9.6: ack rate is the highest rate in BSSBasicRateSet that
	 * is less than or equal to the rate of the immediately previous
	 * frame in the FES
	 */
	rspec = BRCMS_BASIC_RATE(wlc, rspec);
	/* ACK frame len == 14 == 2(fc) + 2(dur) + 6(ra) + 4(fcs) */
	dur =
	    brcms_c_calc_frame_time(wlc, rspec, preamble_type,
				(DOT11_ACK_LEN + FCS_LEN));
	return dur;
}

static uint
brcms_c_calc_cts_time(struct brcms_c_info *wlc, u32 rspec,
		      u8 preamble_type)
{
	BCMMSG(wlc->wiphy, "wl%d: ratespec 0x%x, preamble_type %d\n",
		wlc->pub->unit, rspec, preamble_type);
	return brcms_c_calc_ack_time(wlc, rspec, preamble_type);
}

/* derive wlc->band->basic_rate[] table from 'rateset' */
void brcms_c_rate_lookup_init(struct brcms_c_info *wlc,
			      struct brcms_c_rateset *rateset)
{
	u8 rate;
	u8 mandatory;
	u8 cck_basic = 0;
	u8 ofdm_basic = 0;
	u8 *br = wlc->band->basic_rate;
	uint i;

	/* incoming rates are in 500kbps units as in 802.11 Supported Rates */
	memset(br, 0, BRCM_MAXRATE + 1);

	/* For each basic rate in the rates list, make an entry in the
	 * best basic lookup.
	 */
	for (i = 0; i < rateset->count; i++) {
		/* only make an entry for a basic rate */
		if (!(rateset->rates[i] & BRCMS_RATE_FLAG))
			continue;

		/* mask off basic bit */
		rate = (rateset->rates[i] & BRCMS_RATE_MASK);

		if (rate > BRCM_MAXRATE) {
			wiphy_err(wlc->wiphy, "brcms_c_rate_lookup_init: "
				  "invalid rate 0x%X in rate set\n",
				  rateset->rates[i]);
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
			/*
			 * In 11g and 11a, the OFDM mandatory rates
			 * are 6, 12, and 24 Mbps
			 */
			if (rate >= BRCM_RATE_24M)
				mandatory = BRCM_RATE_24M;
			else if (rate >= BRCM_RATE_12M)
				mandatory = BRCM_RATE_12M;
			else
				mandatory = BRCM_RATE_6M;
		} else {
			/* In 11b, all CCK rates are mandatory 1 - 11 Mbps */
			mandatory = rate;
		}

		br[rate] = mandatory;
	}
}

static void brcms_c_write_rate_shm(struct brcms_c_info *wlc, u8 rate,
				   u8 basic_rate)
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
	phy_rate = rate_info[rate] & BRCMS_RATE_MASK;
	basic_phy_rate = rate_info[basic_rate] & BRCMS_RATE_MASK;
	index = phy_rate & 0xf;
	basic_index = basic_phy_rate & 0xf;

	/* Find the SHM pointer to the ACK rate entry by looking in the
	 * Direct-map Table
	 */
	basic_ptr = brcms_c_read_shm(wlc, (dir_table + basic_index * 2));

	/* Update the SHM BSS-basic-rate-set mapping table with the pointer
	 * to the correct basic rate for the given incoming rate
	 */
	brcms_c_write_shm(wlc, (basic_table + index * 2), basic_ptr);
}

static const struct brcms_c_rateset *
brcms_c_rateset_get_hwrs(struct brcms_c_info *wlc)
{
	const struct brcms_c_rateset *rs_dflt;

	if (BRCMS_PHY_11N_CAP(wlc->band)) {
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

void brcms_c_set_ratetable(struct brcms_c_info *wlc)
{
	const struct brcms_c_rateset *rs_dflt;
	struct brcms_c_rateset rs;
	u8 rate, basic_rate;
	uint i;

	rs_dflt = brcms_c_rateset_get_hwrs(wlc);

	brcms_c_rateset_copy(rs_dflt, &rs);
	brcms_c_rateset_mcs_upd(&rs, wlc->stf->txstreams);

	/* walk the phy rate table and update SHM basic rate lookup table */
	for (i = 0; i < rs.count; i++) {
		rate = rs.rates[i] & BRCMS_RATE_MASK;

		/* for a given rate BRCMS_BASIC_RATE returns the rate at
		 * which a response ACK/CTS should be sent.
		 */
		basic_rate = BRCMS_BASIC_RATE(wlc, rate);
		if (basic_rate == 0)
			/* This should only happen if we are using a
			 * restricted rateset.
			 */
			basic_rate = rs.rates[0] & BRCMS_RATE_MASK;

		brcms_c_write_rate_shm(wlc, rate, basic_rate);
	}
}

/*
 * Return true if the specified rate is supported by the specified band.
 * BRCM_BAND_AUTO indicates the current band.
 */
bool brcms_c_valid_rate(struct brcms_c_info *wlc, u32 rspec, int band,
		    bool verbose)
{
	struct brcms_c_rateset *hw_rateset;
	uint i;

	if ((band == BRCM_BAND_AUTO) || (band == wlc->band->bandtype))
		hw_rateset = &wlc->band->hw_rateset;
	else if (NBANDS(wlc) > 1)
		hw_rateset = &wlc->bandstate[OTHERBANDUNIT(wlc)]->hw_rateset;
	else
		/* other band specified and we are a single band device */
		return false;

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
	if (verbose)
		wiphy_err(wlc->wiphy, "wl%d: valid_rate: rate spec 0x%x "
			  "not in hw_rateset\n", wlc->pub->unit, rspec);

	return false;
}

static void brcms_c_update_mimo_band_bwcap(struct brcms_c_info *wlc, u8 bwcap)
{
	uint i;
	struct brcms_band *band;

	for (i = 0; i < NBANDS(wlc); i++) {
		if (IS_SINGLEBAND_5G(wlc->deviceid))
			i = BAND_5G_INDEX;
		band = wlc->bandstate[i];
		if (band->bandtype == BRCM_BAND_5G) {
			if ((bwcap == BRCMS_N_BW_40ALL)
			    || (bwcap == BRCMS_N_BW_20IN2G_40IN5G))
				band->mimo_cap_40 = true;
			else
				band->mimo_cap_40 = false;
		} else {
			if (bwcap == BRCMS_N_BW_40ALL)
				band->mimo_cap_40 = true;
			else
				band->mimo_cap_40 = false;
		}
	}
}

void brcms_c_mod_prb_rsp_rate_table(struct brcms_c_info *wlc, uint frame_len)
{
	const struct brcms_c_rateset *rs_dflt;
	struct brcms_c_rateset rs;
	u8 rate;
	u16 entry_ptr;
	u8 plcp[D11_PHY_HDR_LEN];
	u16 dur, sifs;
	uint i;

	sifs = SIFS(wlc->band);

	rs_dflt = brcms_c_rateset_get_hwrs(wlc);

	brcms_c_rateset_copy(rs_dflt, &rs);
	brcms_c_rateset_mcs_upd(&rs, wlc->stf->txstreams);

	/*
	 * walk the phy rate table and update MAC core SHM
	 * basic rate table entries
	 */
	for (i = 0; i < rs.count; i++) {
		rate = rs.rates[i] & BRCMS_RATE_MASK;

		entry_ptr = brcms_c_rate_shm_offset(wlc, rate);

		/* Calculate the Probe Response PLCP for the given rate */
		brcms_c_compute_plcp(wlc, rate, frame_len, plcp);

		/*
		 * Calculate the duration of the Probe Response
		 * frame plus SIFS for the MAC
		 */
		dur = (u16) brcms_c_calc_frame_time(wlc, rate,
						BRCMS_LONG_PREAMBLE, frame_len);
		dur += sifs;

		/* Update the SHM Rate Table entry Probe Response values */
		brcms_c_write_shm(wlc, entry_ptr + M_RT_PRS_PLCP_POS,
			      (u16) (plcp[0] + (plcp[1] << 8)));
		brcms_c_write_shm(wlc, entry_ptr + M_RT_PRS_PLCP_POS + 2,
			      (u16) (plcp[2] + (plcp[3] << 8)));
		brcms_c_write_shm(wlc, entry_ptr + M_RT_PRS_DUR_POS, dur);
	}
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
 *	The *len value is set to the number of bytes in buf used, and starts
 *	with the PLCP and included up to, but not including, the 4 byte FCS.
 */
static void
brcms_c_bcn_prb_template(struct brcms_c_info *wlc, u16 type,
			 u32 bcn_rspec,
			 struct brcms_bss_cfg *cfg, u16 *buf, int *len)
{
	static const u8 ether_bcast[ETH_ALEN] = {255, 255, 255, 255, 255, 255};
	struct cck_phy_hdr *plcp;
	struct ieee80211_mgmt *h;
	int hdr_len, body_len;

	if (MBSS_BCN_ENAB(cfg) && type == IEEE80211_STYPE_BEACON)
		hdr_len = DOT11_MAC_HDR_LEN;
	else
		hdr_len = D11_PHY_HDR_LEN + DOT11_MAC_HDR_LEN;

	/* calc buffer size provided for frame body */
	body_len = *len - hdr_len;
	/* return actual size */
	*len = hdr_len + body_len;

	/* format PHY and MAC headers */
	memset((char *)buf, 0, hdr_len);

	plcp = (struct cck_phy_hdr *) buf;

	/*
	 * PLCP for Probe Response frames are filled in from
	 * core's rate table
	 */
	if (type == IEEE80211_STYPE_BEACON && !MBSS_BCN_ENAB(cfg))
		/* fill in PLCP */
		brcms_c_compute_plcp(wlc, bcn_rspec,
				 (DOT11_MAC_HDR_LEN + body_len + FCS_LEN),
				 (u8 *) plcp);

	/* "Regular" and 16 MBSS but not for 4 MBSS */
	/* Update the phytxctl for the beacon based on the rspec */
	if (!SOFTBCN_ENAB(cfg))
		brcms_c_beacon_phytxctl_txant_upd(wlc, bcn_rspec);

	if (MBSS_BCN_ENAB(cfg) && type == IEEE80211_STYPE_BEACON)
		h = (struct ieee80211_mgmt *)&plcp[0];
	else
		h = (struct ieee80211_mgmt *)&plcp[1];

	/* fill in 802.11 header */
	h->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | type);

	/* DUR is 0 for multicast bcn, or filled in by MAC for prb resp */
	/* A1 filled in by MAC for prb resp, broadcast for bcn */
	if (type == IEEE80211_STYPE_BEACON)
		memcpy(&h->da, &ether_bcast, ETH_ALEN);
	memcpy(&h->sa, &cfg->cur_etheraddr, ETH_ALEN);
	memcpy(&h->bssid, &cfg->BSSID, ETH_ALEN);

	/* SEQ filled in by MAC */

	return;
}

int brcms_c_get_header_len()
{
	return TXOFF;
}

/* mac is assumed to be suspended at this point */
void
brcms_b_write_hw_bcntemplates(struct brcms_hardware *wlc_hw, u16 bcn[],
			      int len, bool both)
{
	struct d11regs *regs = wlc_hw->regs;

	if (both) {
		brcms_c_write_hw_bcntemplate0(wlc_hw, bcn, len);
		brcms_c_write_hw_bcntemplate1(wlc_hw, bcn, len);
	} else {
		/* bcn 0 */
		if (!(R_REG(&regs->maccommand) & MCMD_BCN0VLD))
			brcms_c_write_hw_bcntemplate0(wlc_hw, bcn, len);
		/* bcn 1 */
		else if (!
			 (R_REG(&regs->maccommand) & MCMD_BCN1VLD))
			brcms_c_write_hw_bcntemplate1(wlc_hw, bcn, len);
	}
}

/*
 * Update a beacon for a particular BSS
 * For MBSS, this updates the software template and sets "latest" to
 * the index of the template updated. Otherwise, it updates the hardware
 * template.
 */
void brcms_c_bss_update_beacon(struct brcms_c_info *wlc,
			       struct brcms_bss_cfg *cfg)
{
	int len = BCN_TMPL_LEN;

	/* Clear the soft intmask */
	wlc->defmacintmask &= ~MI_BCNTPL;

	if (!cfg->up)
		/* Only allow updates on an UP bss */
		return;

	/* Optimize:  Some of if/else could be combined */
	if (!MBSS_BCN_ENAB(cfg) && HWBCN_ENAB(cfg)) {
		/* Hardware beaconing for this config */
		u16 bcn[BCN_TMPL_LEN / 2];
		u32 both_valid = MCMD_BCN0VLD | MCMD_BCN1VLD;
		struct d11regs *regs = wlc->regs;

		/* Check if both templates are in use, if so sched. an interrupt
		 *      that will call back into this routine
		 */
		if ((R_REG(&regs->maccommand) & both_valid) == both_valid)
			/* clear any previous status */
			W_REG(&regs->macintstatus, MI_BCNTPL);

		/* Check that after scheduling the interrupt both of the
		 *      templates are still busy. if not clear the int. & remask
		 */
		if ((R_REG(&regs->maccommand) & both_valid) == both_valid) {
			wlc->defmacintmask |= MI_BCNTPL;
			return;
		}

		wlc->bcn_rspec =
		    brcms_c_lowest_basic_rspec(wlc, &cfg->current_bss->rateset);
		/* update the template and ucode shm */
		brcms_c_bcn_prb_template(wlc, IEEE80211_STYPE_BEACON,
				     wlc->bcn_rspec, cfg, bcn, &len);
		brcms_c_write_hw_bcntemplates(wlc, bcn, len, false);
	}
}

/*
 * Update all beacons for the system.
 */
void brcms_c_update_beacon(struct brcms_c_info *wlc)
{
	int idx;
	struct brcms_bss_cfg *bsscfg;

	/* update AP or IBSS beacons */
	FOREACH_BSS(wlc, idx, bsscfg)
		if (bsscfg->up && (BSSCFG_AP(bsscfg) || !bsscfg->BSS))
			brcms_c_bss_update_beacon(wlc, bsscfg);
	END_FOREACH_BSS()
}

/* Write ssid into shared memory */
void brcms_c_shm_ssid_upd(struct brcms_c_info *wlc, struct brcms_bss_cfg *cfg)
{
	u8 *ssidptr = cfg->SSID;
	u16 base = M_SSID;
	u8 ssidbuf[IEEE80211_MAX_SSID_LEN];

	/* padding the ssid with zero and copy it into shm */
	memset(ssidbuf, 0, IEEE80211_MAX_SSID_LEN);
	memcpy(ssidbuf, ssidptr, cfg->SSID_len);

	brcms_c_copyto_shm(wlc, base, ssidbuf, IEEE80211_MAX_SSID_LEN);

	if (!MBSS_BCN_ENAB(cfg))
		brcms_c_write_shm(wlc, M_SSIDLEN, (u16) cfg->SSID_len);
}

void brcms_c_update_probe_resp(struct brcms_c_info *wlc, bool suspend)
{
	int idx;
	struct brcms_bss_cfg *bsscfg;

	/* update AP or IBSS probe responses */
	FOREACH_BSS(wlc, idx, bsscfg)
		if (bsscfg->up && (BSSCFG_AP(bsscfg) || !bsscfg->BSS))
			brcms_c_bss_update_probe_resp(wlc, bsscfg, suspend);
	END_FOREACH_BSS()
}

void
brcms_c_bss_update_probe_resp(struct brcms_c_info *wlc,
			      struct brcms_bss_cfg *cfg,
			      bool suspend)
{
	u16 prb_resp[BCN_TMPL_LEN / 2];
	int len = BCN_TMPL_LEN;

	/*
	 * write the probe response to hardware, or save in
	 * the config structure
	 */
	if (!MBSS_PRB_ENAB(cfg)) {

		/* create the probe response template */
		brcms_c_bcn_prb_template(wlc, IEEE80211_STYPE_PROBE_RESP, 0,
					 cfg, prb_resp, &len);

		if (suspend)
			brcms_c_suspend_mac_and_wait(wlc);

		/* write the probe response into the template region */
		brcms_b_write_template_ram(wlc->hw, T_PRS_TPL_BASE,
					    (len + 3) & ~3, prb_resp);

		/* write the length of the probe response frame (+PLCP/-FCS) */
		brcms_c_write_shm(wlc, M_PRB_RESP_FRM_LEN, (u16) len);

		/* write the SSID and SSID length */
		brcms_c_shm_ssid_upd(wlc, cfg);

		/*
		 * Write PLCP headers and durations for probe response frames
		 * at all rates. Use the actual frame length covered by the
		 * PLCP header for the call to brcms_c_mod_prb_rsp_rate_table()
		 * by subtracting the PLCP len and adding the FCS.
		 */
		len += (-D11_PHY_HDR_LEN + FCS_LEN);
		brcms_c_mod_prb_rsp_rate_table(wlc, (u16) len);

		if (suspend)
			brcms_c_enable_mac(wlc);
	} else {
		/* Generating probe resp in sw; update local template */
		/* error: No software probe response support without MBSS */
	}
}

/* prepares pdu for transmission. returns BCM error codes */
int brcms_c_prep_pdu(struct brcms_c_info *wlc, struct sk_buff *pdu, uint *fifop)
{
	uint fifo;
	struct d11txh *txh;
	struct ieee80211_hdr *h;
	struct scb *scb;

	txh = (struct d11txh *) (pdu->data);
	h = (struct ieee80211_hdr *)((u8 *) (txh + 1) + D11_PHY_HDR_LEN);

	/* get the pkt queue info. This was put at brcms_c_sendctl or
	 * brcms_c_send for PDU */
	fifo = le16_to_cpu(txh->TxFrameID) & TXFID_QUEUE_MASK;

	scb = NULL;

	*fifop = fifo;

	/* return if insufficient dma resources */
	if (TXAVAIL(wlc, fifo) < MAX_DMA_SEGS) {
		/* Mark precedences related to this FIFO, unsendable */
		BRCMS_TX_FIFO_CLEAR(wlc, fifo);
		return -EBUSY;
	}
	return 0;
}

/* init tx reported rate mechanism */
void brcms_c_reprate_init(struct brcms_c_info *wlc)
{
	int i;
	struct brcms_bss_cfg *bsscfg;

	FOREACH_BSS(wlc, i, bsscfg)
		brcms_c_bsscfg_reprate_init(bsscfg);
	END_FOREACH_BSS()
}

/* per bsscfg init tx reported rate mechanism */
void brcms_c_bsscfg_reprate_init(struct brcms_bss_cfg *bsscfg)
{
	bsscfg->txrspecidx = 0;
	memset((char *)bsscfg->txrspec, 0, sizeof(bsscfg->txrspec));
}

void brcms_default_rateset(struct brcms_c_info *wlc, struct brcms_c_rateset *rs)
{
	brcms_c_rateset_default(rs, NULL, wlc->band->phytype,
		wlc->band->bandtype, false, BRCMS_RATE_MASK_FULL,
		(bool) N_ENAB(wlc->pub),
		CHSPEC_WLC_BW(wlc->default_bss->chanspec),
		wlc->stf->txstreams);
}

static void brcms_c_bss_default_init(struct brcms_c_info *wlc)
{
	u16 chanspec;
	struct brcms_band *band;
	struct brcms_bss_info *bi = wlc->default_bss;

	/* init default and target BSS with some sane initial values */
	memset((char *)(bi), 0, sizeof(struct brcms_bss_info));
	bi->beacon_period = BEACON_INTERVAL_DEFAULT;
	bi->dtim_period = DTIM_INTERVAL_DEFAULT;

	/* fill the default channel as the first valid channel
	 * starting from the 2G channels
	 */
	chanspec = CH20MHZ_CHSPEC(1);
	wlc->home_chanspec = bi->chanspec = chanspec;

	/* find the band of our default channel */
	band = wlc->band;
	if (NBANDS(wlc) > 1 && band->bandunit != CHSPEC_BANDUNIT(chanspec))
		band = wlc->bandstate[OTHERBANDUNIT(wlc)];

	/* init bss rates to the band specific default rate set */
	brcms_c_rateset_default(&bi->rateset, NULL, band->phytype,
		band->bandtype, false, BRCMS_RATE_MASK_FULL,
		(bool) N_ENAB(wlc->pub), CHSPEC_WLC_BW(chanspec),
		wlc->stf->txstreams);

	if (N_ENAB(wlc->pub))
		bi->flags |= BRCMS_BSS_HT;
}

static u32
mac80211_wlc_set_nrate(struct brcms_c_info *wlc, struct brcms_band *cur_band,
		       u32 int_val)
{
	u8 stf = (int_val & NRATE_STF_MASK) >> NRATE_STF_SHIFT;
	u8 rate = int_val & NRATE_RATE_MASK;
	u32 rspec;
	bool ismcs = ((int_val & NRATE_MCS_INUSE) == NRATE_MCS_INUSE);
	bool issgi = ((int_val & NRATE_SGI_MASK) >> NRATE_SGI_SHIFT);
	bool override_mcs_only = ((int_val & NRATE_OVERRIDE_MCS_ONLY)
				  == NRATE_OVERRIDE_MCS_ONLY);
	int bcmerror = 0;

	if (!ismcs)
		return (u32) rate;

	/* validate the combination of rate/mcs/stf is allowed */
	if (N_ENAB(wlc->pub) && ismcs) {
		/* mcs only allowed when nmode */
		if (stf > PHY_TXC1_MODE_SDM) {
			wiphy_err(wlc->wiphy, "wl%d: %s: Invalid stf\n",
				 BRCMS_UNIT(wlc), __func__);
			bcmerror = -EINVAL;
			goto done;
		}

		/* mcs 32 is a special case, DUP mode 40 only */
		if (rate == 32) {
			if (!CHSPEC_IS40(wlc->home_chanspec) ||
			    ((stf != PHY_TXC1_MODE_SISO)
			     && (stf != PHY_TXC1_MODE_CDD))) {
				wiphy_err(wlc->wiphy, "wl%d: %s: Invalid mcs "
					  "32\n", BRCMS_UNIT(wlc), __func__);
				bcmerror = -EINVAL;
				goto done;
			}
			/* mcs > 7 must use stf SDM */
		} else if (rate > HIGHEST_SINGLE_STREAM_MCS) {
			/* mcs > 7 must use stf SDM */
			if (stf != PHY_TXC1_MODE_SDM) {
				BCMMSG(wlc->wiphy, "wl%d: enabling "
					 "SDM mode for mcs %d\n",
					 BRCMS_UNIT(wlc), rate);
				stf = PHY_TXC1_MODE_SDM;
			}
		} else {
			/*
			 * MCS 0-7 may use SISO, CDD, and for
			 * phy_rev >= 3 STBC
			 */
			if ((stf > PHY_TXC1_MODE_STBC) ||
			    (!BRCMS_STBC_CAP_PHY(wlc)
			     && (stf == PHY_TXC1_MODE_STBC))) {
				wiphy_err(wlc->wiphy, "wl%d: %s: Invalid STBC"
					  "\n", BRCMS_UNIT(wlc), __func__);
				bcmerror = -EINVAL;
				goto done;
			}
		}
	} else if (IS_OFDM(rate)) {
		if ((stf != PHY_TXC1_MODE_CDD) && (stf != PHY_TXC1_MODE_SISO)) {
			wiphy_err(wlc->wiphy, "wl%d: %s: Invalid OFDM\n",
				  BRCMS_UNIT(wlc), __func__);
			bcmerror = -EINVAL;
			goto done;
		}
	} else if (IS_CCK(rate)) {
		if ((cur_band->bandtype != BRCM_BAND_2G)
		    || (stf != PHY_TXC1_MODE_SISO)) {
			wiphy_err(wlc->wiphy, "wl%d: %s: Invalid CCK\n",
				  BRCMS_UNIT(wlc), __func__);
			bcmerror = -EINVAL;
			goto done;
		}
	} else {
		wiphy_err(wlc->wiphy, "wl%d: %s: Unknown rate type\n",
			  BRCMS_UNIT(wlc), __func__);
		bcmerror = -EINVAL;
		goto done;
	}
	/* make sure multiple antennae are available for non-siso rates */
	if ((stf != PHY_TXC1_MODE_SISO) && (wlc->stf->txstreams == 1)) {
		wiphy_err(wlc->wiphy, "wl%d: %s: SISO antenna but !SISO "
			  "request\n", BRCMS_UNIT(wlc), __func__);
		bcmerror = -EINVAL;
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
	    && !brcms_c_valid_rate(wlc, rspec, cur_band->bandtype, true))
		return rate;

	return rspec;
done:
	return rate;
}

/* formula:  IDLE_BUSY_RATIO_X_16 = (100-duty_cycle)/duty_cycle*16 */
static int
brcms_c_duty_cycle_set(struct brcms_c_info *wlc, int duty_cycle, bool isOFDM,
		   bool writeToShm)
{
	int idle_busy_ratio_x_16 = 0;
	uint offset =
	    isOFDM ? M_TX_IDLE_BUSY_RATIO_X_16_OFDM :
	    M_TX_IDLE_BUSY_RATIO_X_16_CCK;
	if (duty_cycle > 100 || duty_cycle < 0) {
		wiphy_err(wlc->wiphy, "wl%d:  duty cycle value off limit\n",
			  wlc->pub->unit);
		return -EINVAL;
	}
	if (duty_cycle)
		idle_busy_ratio_x_16 = (100 - duty_cycle) * 16 / duty_cycle;
	/* Only write to shared memory  when wl is up */
	if (writeToShm)
		brcms_c_write_shm(wlc, offset, (u16) idle_busy_ratio_x_16);

	if (isOFDM)
		wlc->tx_duty_cycle_ofdm = (u16) duty_cycle;
	else
		wlc->tx_duty_cycle_cck = (u16) duty_cycle;

	return 0;
}

/* Read a single u16 from shared memory.
 * SHM 'offset' needs to be an even address
 */
u16 brcms_c_read_shm(struct brcms_c_info *wlc, uint offset)
{
	return brcms_b_read_shm(wlc->hw, offset);
}

/* Write a single u16 to shared memory.
 * SHM 'offset' needs to be an even address
 */
void brcms_c_write_shm(struct brcms_c_info *wlc, uint offset, u16 v)
{
	brcms_b_write_shm(wlc->hw, offset, v);
}

/* Copy a buffer to shared memory.
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 */
void brcms_c_copyto_shm(struct brcms_c_info *wlc, uint offset, const void *buf,
			int len)
{
	/* offset and len need to be even */
	if (len <= 0 || (offset & 1) || (len & 1))
		return;

	brcms_b_copyto_objmem(wlc->hw, offset, buf, len, OBJADDR_SHM_SEL);

}

/* wrapper BMAC functions to for HIGH driver access */
void brcms_c_mctrl(struct brcms_c_info *wlc, u32 mask, u32 val)
{
	brcms_b_mctrl(wlc->hw, mask, val);
}

void brcms_c_mhf(struct brcms_c_info *wlc, u8 idx, u16 mask, u16 val, int bands)
{
	brcms_b_mhf(wlc->hw, idx, mask, val, bands);
}

int brcms_b_xmtfifo_sz_get(struct brcms_hardware *wlc_hw, uint fifo,
			   uint *blocks)
{
	if (fifo >= NFIFO)
		return -EINVAL;

	*blocks = wlc_hw->xmtfifo_sz[fifo];

	return 0;
}

int brcms_c_xmtfifo_sz_get(struct brcms_c_info *wlc, uint fifo, uint *blocks)
{
	return brcms_b_xmtfifo_sz_get(wlc->hw, fifo, blocks);
}

void brcms_c_write_template_ram(struct brcms_c_info *wlc, int offset, int len,
			    void *buf)
{
	brcms_b_write_template_ram(wlc->hw, offset, len, buf);
}

void brcms_c_write_hw_bcntemplates(struct brcms_c_info *wlc, u16 bcn[], int len,
				   bool both)
{
	brcms_b_write_hw_bcntemplates(wlc->hw, bcn, len, both);
}

void
brcms_c_set_addrmatch(struct brcms_c_info *wlc, int match_reg_offset,
		  const u8 *addr)
{
	brcms_b_set_addrmatch(wlc->hw, match_reg_offset, addr);
	if (match_reg_offset == RCM_BSSID_OFFSET)
		memcpy(wlc->cfg->BSSID, addr, ETH_ALEN);
}

void brcms_c_pllreq(struct brcms_c_info *wlc, bool set, u32 req_bit)
{
	brcms_b_pllreq(wlc->hw, set, req_bit);
}

void brcms_c_reset_bmac_done(struct brcms_c_info *wlc)
{
}

/* check for the particular priority flow control bit being set */
bool
brcms_c_txflowcontrol_prio_isset(struct brcms_c_info *wlc,
				 struct brcms_txq_info *q,
				 int prio)
{
	uint prio_mask;

	if (prio == ALLPRIO)
		prio_mask = TXQ_STOP_FOR_PRIOFC_MASK;
	else
		prio_mask = NBITVAL(prio);

	return (q->stopped & prio_mask) == prio_mask;
}

/* propagate the flow control to all interfaces using the given tx queue */
void brcms_c_txflowcontrol(struct brcms_c_info *wlc,
			   struct brcms_txq_info *qi,
			   bool on, int prio)
{
	uint prio_bits;
	uint cur_bits;

	BCMMSG(wlc->wiphy, "flow control kicks in\n");

	if (prio == ALLPRIO)
		prio_bits = TXQ_STOP_FOR_PRIOFC_MASK;
	else
		prio_bits = NBITVAL(prio);

	cur_bits = qi->stopped & prio_bits;

	/* Check for the case of no change and return early
	 * Otherwise update the bit and continue
	 */
	if (on) {
		if (cur_bits == prio_bits)
			return;

		mboolset(qi->stopped, prio_bits);
	} else {
		if (cur_bits == 0)
			return;

		mboolclr(qi->stopped, prio_bits);
	}

	/* If there is a flow control override we will not change the external
	 * flow control state.
	 */
	if (qi->stopped & ~TXQ_STOP_FOR_PRIOFC_MASK)
		return;

	brcms_c_txflowcontrol_signal(wlc, qi, on, prio);
}

void
brcms_c_txflowcontrol_override(struct brcms_c_info *wlc,
			       struct brcms_txq_info *qi,
			       bool on, uint override)
{
	uint prev_override;

	prev_override = (qi->stopped & ~TXQ_STOP_FOR_PRIOFC_MASK);

	/* Update the flow control bits and do an early return if there is
	 * no change in the external flow control state.
	 */
	if (on) {
		mboolset(qi->stopped, override);
		/* if there was a previous override bit on, then setting this
		 * makes no difference.
		 */
		if (prev_override)
			return;

		brcms_c_txflowcontrol_signal(wlc, qi, ON, ALLPRIO);
	} else {
		mboolclr(qi->stopped, override);
		/* clearing an override bit will only make a difference for
		 * flow control if it was the only bit set. For any other
		 * override setting, just return
		 */
		if (prev_override != override)
			return;

		if (qi->stopped == 0) {
			brcms_c_txflowcontrol_signal(wlc, qi, OFF, ALLPRIO);
		} else {
			int prio;

			for (prio = MAXPRIO; prio >= 0; prio--) {
				if (!mboolisset(qi->stopped, NBITVAL(prio)))
					brcms_c_txflowcontrol_signal(
						wlc, qi, OFF, prio);
			}
		}
	}
}

static void brcms_c_txflowcontrol_reset(struct brcms_c_info *wlc)
{
	struct brcms_txq_info *qi;

	for (qi = wlc->tx_queues; qi != NULL; qi = qi->next) {
		if (qi->stopped) {
			brcms_c_txflowcontrol_signal(wlc, qi, OFF, ALLPRIO);
			qi->stopped = 0;
		}
	}
}

static void
brcms_c_txflowcontrol_signal(struct brcms_c_info *wlc,
			     struct brcms_txq_info *qi, bool on, int prio)
{
	/* wlcif_list is never filled so this function is not functional yet */
}

static struct brcms_txq_info *brcms_c_txq_alloc(struct brcms_c_info *wlc)
{
	struct brcms_txq_info *qi, *p;

	qi = kzalloc(sizeof(struct brcms_txq_info), GFP_ATOMIC);
	if (qi != NULL) {
		/*
		 * Have enough room for control packets along with HI watermark
		 * Also, add room to txq for total psq packets if all the SCBs
		 * leave PS mode. The watermark for flowcontrol to OS packets
		 * will remain the same
		 */
		brcmu_pktq_init(&qi->q, BRCMS_PREC_COUNT,
			  (2 * wlc->pub->tunables->datahiwat) + PKTQ_LEN_DEFAULT
			  + wlc->pub->psq_pkts_total);

		/* add this queue to the the global list */
		p = wlc->tx_queues;
		if (p == NULL) {
			wlc->tx_queues = qi;
		} else {
			while (p->next != NULL)
				p = p->next;
			p->next = qi;
		}
	}
	return qi;
}

static void brcms_c_txq_free(struct brcms_c_info *wlc,
			     struct brcms_txq_info *qi)
{
	struct brcms_txq_info *p;

	if (qi == NULL)
		return;

	/* remove the queue from the linked list */
	p = wlc->tx_queues;
	if (p == qi)
		wlc->tx_queues = p->next;
	else {
		while (p != NULL && p->next != qi)
			p = p->next;
		if (p != NULL)
			p->next = p->next->next;
	}

	kfree(qi);
}

/*
 * Flag 'scan in progress' to withhold dynamic phy calibration
 */
void brcms_c_scan_start(struct brcms_c_info *wlc)
{
	wlc_phy_hold_upd(wlc->band->pi, PHY_HOLD_FOR_SCAN, true);
}

void brcms_c_scan_stop(struct brcms_c_info *wlc)
{
	wlc_phy_hold_upd(wlc->band->pi, PHY_HOLD_FOR_SCAN, false);
}

void brcms_c_associate_upd(struct brcms_c_info *wlc, bool state)
{
	wlc->pub->associated = state;
	wlc->cfg->associated = state;
}

/*
 * When a remote STA/AP is removed by Mac80211, or when it can no longer accept
 * AMPDU traffic, packets pending in hardware have to be invalidated so that
 * when later on hardware releases them, they can be handled appropriately.
 */
void brcms_c_inval_dma_pkts(struct brcms_hardware *hw,
			       struct ieee80211_sta *sta,
			       void (*dma_callback_fn))
{
	struct dma_pub *dmah;
	int i;
	for (i = 0; i < NFIFO; i++) {
		dmah = hw->di[i];
		if (dmah != NULL)
			dma_walk_packets(dmah, dma_callback_fn, sta);
	}
}

int brcms_c_get_curband(struct brcms_c_info *wlc)
{
	return wlc->band->bandunit;
}

void brcms_c_wait_for_tx_completion(struct brcms_c_info *wlc, bool drop)
{
	/* flush packet queue when requested */
	if (drop)
		brcmu_pktq_flush(&wlc->pkt_queue->q, false, NULL, NULL);

	/* wait for queue and DMA fifos to run dry */
	while (!pktq_empty(&wlc->pkt_queue->q) ||
	       TXPKTPENDTOT(wlc) > 0)
		brcms_msleep(wlc->wl, 1);
}

int brcms_c_set_par(struct brcms_c_info *wlc, enum wlc_par_id par_id,
		    int int_val)
{
	int err = 0;

	switch (par_id) {
	case IOV_BCN_LI_BCN:
		wlc->bcn_li_bcn = (u8) int_val;
		if (wlc->pub->up)
			brcms_c_bcn_li_upd(wlc);
		break;
		/* As long as override is false, this only sets the *user*
		   targets. User can twiddle this all he wants with no harm.
		   wlc_phy_txpower_set() explicitly sets override to false if
		   not internal or test.
		 */
	case IOV_QTXPOWER:{
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
	case IOV_MPC:
		wlc->mpc = (bool)int_val;
		brcms_c_radio_mpc_upd(wlc);
		break;
	default:
		err = -ENOTSUPP;
	}
	return err;
}

int brcms_c_get_par(struct brcms_c_info *wlc, enum wlc_par_id par_id,
		    int *ret_int_ptr)
{
	int err = 0;

	switch (par_id) {
	case IOV_BCN_LI_BCN:
		*ret_int_ptr = wlc->bcn_li_bcn;
		break;
	case IOV_QTXPOWER: {
		uint qdbm;
		bool override;

		err = wlc_phy_txpower_get(wlc->band->pi, &qdbm,
			&override);
		if (err != 0)
			return err;

		/* Return qdbm units */
		*ret_int_ptr =
		    qdbm | (override ? WL_TXPWR_OVERRIDE : 0);
		break;
		}
	case IOV_MPC:
		*ret_int_ptr = (s32) wlc->mpc;
		break;
	default:
		err = -ENOTSUPP;
	}
	return err;
}

/*
 * Search the name=value vars for a specific one and return its value.
 * Returns NULL if not found.
 */
char *getvar(char *vars, const char *name)
{
	char *s;
	int len;

	if (!name)
		return NULL;

	len = strlen(name);
	if (len == 0)
		return NULL;

	/* first look in vars[] */
	for (s = vars; s && *s;) {
		if ((memcmp(s, name, len) == 0) && (s[len] == '='))
			return &s[len + 1];

		while (*s++)
			;
	}
	/* nothing found */
	return NULL;
}

/*
 * Search the vars for a specific one and return its value as
 * an integer. Returns 0 if not found.
 */
int getintvar(char *vars, const char *name)
{
	char *val;
	unsigned long res;

	val = getvar(vars, name);
	if (val && !kstrtoul(val, 0, &res))
		return res;

	return 0;
}
