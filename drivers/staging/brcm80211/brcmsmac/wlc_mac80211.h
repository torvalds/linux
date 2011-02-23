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

#ifndef _wlc_h_
#define _wlc_h_

#include <wlioctl.h>
#include <wlc_phy_hal.h>
#include <wlc_channel.h>
#include <wlc_bsscfg.h>
#include <wlc_scb.h>

#define MA_WINDOW_SZ		8	/* moving average window size */
#define	WL_HWRXOFF		38	/* chip rx buffer offset */
#define	INVCHANNEL		255	/* invalid channel */
#define	MAXCOREREV		28	/* max # supported core revisions (0 .. MAXCOREREV - 1) */
#define WLC_MAXMODULES		22	/* max #  wlc_module_register() calls */

#define WLC_BITSCNT(x)	bcm_bitcount((u8 *)&(x), sizeof(u8))

/* Maximum wait time for a MAC suspend */
#define	WLC_MAX_MAC_SUSPEND	83000	/* uS: 83mS is max packet time (64KB ampdu @ 6Mbps) */

/* Probe Response timeout - responses for probe requests older that this are tossed, zero to disable
 */
#define WLC_PRB_RESP_TIMEOUT	0	/* Disable probe response timeout */

/* transmit buffer max headroom for protocol headers */
#define TXOFF (D11_TXH_LEN + D11_PHY_HDR_LEN)

/* For managing scan result lists */
typedef struct wlc_bss_list {
	uint count;
	bool beacon;		/* set for beacon, cleared for probe response */
	wlc_bss_info_t *ptrs[MAXBSS];
} wlc_bss_list_t;

#define	SW_TIMER_MAC_STAT_UPD		30	/* periodic MAC stats update */

/* Double check that unsupported cores are not enabled */
#if CONF_MSK(D11CONF, 0x4f) || CONF_GE(D11CONF, MAXCOREREV)
#error "Configuration for D11CONF includes unsupported versions."
#endif				/* Bad versions */

#define	VALID_COREREV(corerev)	CONF_HAS(D11CONF, corerev)

/* values for shortslot_override */
#define WLC_SHORTSLOT_AUTO	-1	/* Driver will manage Shortslot setting */
#define WLC_SHORTSLOT_OFF	0	/* Turn off short slot */
#define WLC_SHORTSLOT_ON	1	/* Turn on short slot */

/* value for short/long and mixmode/greenfield preamble */

#define WLC_LONG_PREAMBLE	(0)
#define WLC_SHORT_PREAMBLE	(1 << 0)
#define WLC_GF_PREAMBLE		(1 << 1)
#define WLC_MM_PREAMBLE		(1 << 2)
#define WLC_IS_MIMO_PREAMBLE(_pre) (((_pre) == WLC_GF_PREAMBLE) || ((_pre) == WLC_MM_PREAMBLE))

/* values for barker_preamble */
#define WLC_BARKER_SHORT_ALLOWED	0	/* Short pre-amble allowed */

/* A fifo is full. Clear precedences related to that FIFO */
#define WLC_TX_FIFO_CLEAR(wlc, fifo) ((wlc)->tx_prec_map &= ~(wlc)->fifo2prec_map[fifo])

/* Fifo is NOT full. Enable precedences for that FIFO */
#define WLC_TX_FIFO_ENAB(wlc, fifo)  ((wlc)->tx_prec_map |= (wlc)->fifo2prec_map[fifo])

/* TxFrameID */
/* seq and frag bits: SEQNUM_SHIFT, FRAGNUM_MASK (802.11.h) */
/* rate epoch bits: TXFID_RATE_SHIFT, TXFID_RATE_MASK ((wlc_rate.c) */
#define TXFID_QUEUE_MASK	0x0007	/* Bits 0-2 */
#define TXFID_SEQ_MASK		0x7FE0	/* Bits 5-15 */
#define TXFID_SEQ_SHIFT		5	/* Number of bit shifts */
#define	TXFID_RATE_PROBE_MASK	0x8000	/* Bit 15 for rate probe */
#define TXFID_RATE_MASK		0x0018	/* Mask for bits 3 and 4 */
#define TXFID_RATE_SHIFT	3	/* Shift 3 bits for rate mask */

/* promote boardrev */
#define BOARDREV_PROMOTABLE	0xFF	/* from */
#define BOARDREV_PROMOTED	1	/* to */

/* if wpa is in use then portopen is true when the group key is plumbed otherwise it is always true
 */
#define WSEC_ENABLED(wsec) ((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))
#define WLC_SW_KEYS(wlc, bsscfg) ((((wlc)->wsec_swkeys) || \
	((bsscfg)->wsec & WSEC_SWFLAG)))

#define WLC_PORTOPEN(cfg) \
	(((cfg)->WPA_auth != WPA_AUTH_DISABLED && WSEC_ENABLED((cfg)->wsec)) ? \
	(cfg)->wsec_portopen : true)

#define PS_ALLOWED(wlc)	wlc_ps_allowed(wlc)
#define STAY_AWAKE(wlc) wlc_stay_awake(wlc)

#define DATA_BLOCK_TX_SUPR	(1 << 4)

/* 802.1D Priority to TX FIFO number for wme */
extern const u8 prio2fifo[];

/* Ucode MCTL_WAKE override bits */
#define WLC_WAKE_OVERRIDE_CLKCTL	0x01
#define WLC_WAKE_OVERRIDE_PHYREG	0x02
#define WLC_WAKE_OVERRIDE_MACSUSPEND	0x04
#define WLC_WAKE_OVERRIDE_TXFIFO	0x08
#define WLC_WAKE_OVERRIDE_FORCEFAST	0x10

/* stuff pulled in from wlc.c */

/* Interrupt bit error summary.  Don't include I_RU: we refill DMA at other
 * times; and if we run out, constant I_RU interrupts may cause lockup.  We
 * will still get error counts from rx0ovfl.
 */
#define	I_ERRORS	(I_PC | I_PD | I_DE | I_RO | I_XU)
/* default software intmasks */
#define	DEF_RXINTMASK	(I_RI)	/* enable rx int on rxfifo only */
#define	DEF_MACINTMASK	(MI_TXSTOP | MI_TBTT | MI_ATIMWINEND | MI_PMQ | \
			 MI_PHYTXERR | MI_DMAINT | MI_TFS | MI_BG_NOISE | \
			 MI_CCA | MI_TO | MI_GP0 | MI_RFDISABLE | MI_PWRUP)

#define	RETRY_SHORT_DEF			7	/* Default Short retry Limit */
#define	RETRY_SHORT_MAX			255	/* Maximum Short retry Limit */
#define	RETRY_LONG_DEF			4	/* Default Long retry count */
#define	RETRY_SHORT_FB			3	/* Short retry count for fallback rate */
#define	RETRY_LONG_FB			2	/* Long retry count for fallback rate */

#define	MAXTXPKTS		6	/* max # pkts pending */

/* frameburst */
#define	MAXTXFRAMEBURST		8	/* vanilla xpress mode: max frames/burst */
#define	MAXFRAMEBURST_TXOP	10000	/* Frameburst TXOP in usec */

/* Per-AC retry limit register definitions; uses bcmdefs.h bitfield macros */
#define EDCF_SHORT_S            0
#define EDCF_SFB_S              4
#define EDCF_LONG_S             8
#define EDCF_LFB_S              12
#define EDCF_SHORT_M            BITFIELD_MASK(4)
#define EDCF_SFB_M              BITFIELD_MASK(4)
#define EDCF_LONG_M             BITFIELD_MASK(4)
#define EDCF_LFB_M              BITFIELD_MASK(4)

#define WLC_WME_RETRY_SHORT_GET(wlc, ac)    GFIELD(wlc->wme_retries[ac], EDCF_SHORT)
#define WLC_WME_RETRY_SFB_GET(wlc, ac)      GFIELD(wlc->wme_retries[ac], EDCF_SFB)
#define WLC_WME_RETRY_LONG_GET(wlc, ac)     GFIELD(wlc->wme_retries[ac], EDCF_LONG)
#define WLC_WME_RETRY_LFB_GET(wlc, ac)      GFIELD(wlc->wme_retries[ac], EDCF_LFB)

#define WLC_WME_RETRY_SHORT_SET(wlc, ac, val) \
	(wlc->wme_retries[ac] = SFIELD(wlc->wme_retries[ac], EDCF_SHORT, val))
#define WLC_WME_RETRY_SFB_SET(wlc, ac, val) \
	(wlc->wme_retries[ac] = SFIELD(wlc->wme_retries[ac], EDCF_SFB, val))
#define WLC_WME_RETRY_LONG_SET(wlc, ac, val) \
	(wlc->wme_retries[ac] = SFIELD(wlc->wme_retries[ac], EDCF_LONG, val))
#define WLC_WME_RETRY_LFB_SET(wlc, ac, val) \
	(wlc->wme_retries[ac] = SFIELD(wlc->wme_retries[ac], EDCF_LFB, val))

/* PLL requests */
#define WLC_PLLREQ_SHARED	0x1	/* pll is shared on old chips */
#define WLC_PLLREQ_RADIO_MON	0x2	/* hold pll for radio monitor register checking */
#define WLC_PLLREQ_FLIP		0x4	/* hold/release pll for some short operation */

/* Do we support this rate? */
#define VALID_RATE_DBG(wlc, rspec) wlc_valid_rate(wlc, rspec, WLC_BAND_AUTO, true)

/*
 * Macros to check if AP or STA is active.
 * AP Active means more than just configured: driver and BSS are "up";
 * that is, we are beaconing/responding as an AP (aps_associated).
 * STA Active similarly means the driver is up and a configured STA BSS
 * is up: either associated (stas_associated) or trying.
 *
 * Macro definitions vary as per AP/STA ifdefs, allowing references to
 * ifdef'd structure fields and constant values (0) for optimization.
 * Make sure to enclose blocks of code such that any routines they
 * reference can also be unused and optimized out by the linker.
 */
/* NOTE: References structure fields defined in wlc.h */
#define AP_ACTIVE(wlc)	(0)

/*
 * Detect Card removed.
 * Even checking an sbconfig register read will not false trigger when the core is in reset.
 * it breaks CF address mechanism. Accessing gphy phyversion will cause SB error if aphy
 * is in reset on 4306B0-DB. Need a simple accessible reg with fixed 0/1 pattern
 * (some platforms return all 0).
 * If clocks are present, call the sb routine which will figure out if the device is removed.
 */
#define DEVICEREMOVED(wlc)      \
	((wlc->hw->clk) ?   \
	((R_REG(wlc->hw->osh, &wlc->hw->regs->maccontrol) & \
	(MCTL_PSM_JMP_0 | MCTL_IHR_EN)) != MCTL_IHR_EN) : \
	(si_deviceremoved(wlc->hw->sih)))

#define WLCWLUNIT(wlc)		((wlc)->pub->unit)

typedef struct wlc_protection {
	bool _g;		/* use g spec protection, driver internal */
	s8 g_override;	/* override for use of g spec protection */
	u8 gmode_user;	/* user config gmode, operating band->gmode is different */
	s8 overlap;		/* Overlap BSS/IBSS protection for both 11g and 11n */
	s8 nmode_user;	/* user config nmode, operating pub->nmode is different */
	s8 n_cfg;		/* use OFDM protection on MIMO frames */
	s8 n_cfg_override;	/* override for use of N protection */
	bool nongf;		/* non-GF present protection */
	s8 nongf_override;	/* override for use of GF protection */
	s8 n_pam_override;	/* override for preamble: MM or GF */
	bool n_obss;		/* indicated OBSS Non-HT STA present */

	uint longpre_detect_timeout;	/* #sec until long preamble bcns gone */
	uint barker_detect_timeout;	/* #sec until bcns signaling Barker long preamble */
	/* only is gone */
	uint ofdm_ibss_timeout;	/* #sec until ofdm IBSS beacons gone */
	uint ofdm_ovlp_timeout;	/* #sec until ofdm overlapping BSS bcns gone */
	uint nonerp_ibss_timeout;	/* #sec until nonerp IBSS beacons gone */
	uint nonerp_ovlp_timeout;	/* #sec until nonerp overlapping BSS bcns gone */
	uint g_ibss_timeout;	/* #sec until bcns signaling Use_Protection gone */
	uint n_ibss_timeout;	/* #sec until bcns signaling Use_OFDM_Protection gone */
	uint ht20in40_ovlp_timeout;	/* #sec until 20MHz overlapping OPMODE gone */
	uint ht20in40_ibss_timeout;	/* #sec until 20MHz-only HT station bcns gone */
	uint non_gf_ibss_timeout;	/* #sec until non-GF bcns gone */
} wlc_protection_t;

/* anything affects the single/dual streams/antenna operation */
typedef struct wlc_stf {
	u8 hw_txchain;	/* HW txchain bitmap cfg */
	u8 txchain;		/* txchain bitmap being used */
	u8 txstreams;	/* number of txchains being used */

	u8 hw_rxchain;	/* HW rxchain bitmap cfg */
	u8 rxchain;		/* rxchain bitmap being used */
	u8 rxstreams;	/* number of rxchains being used */

	u8 ant_rx_ovr;	/* rx antenna override */
	s8 txant;		/* userTx antenna setting */
	u16 phytxant;	/* phyTx antenna setting in txheader */

	u8 ss_opmode;	/* singlestream Operational mode, 0:siso; 1:cdd */
	bool ss_algosel_auto;	/* if true, use wlc->stf->ss_algo_channel; */
	/* else use wlc->band->stf->ss_mode_band; */
	u16 ss_algo_channel;	/* ss based on per-channel algo: 0: SISO, 1: CDD 2: STBC */
	u8 no_cddstbc;	/* stf override, 1: no CDD (or STBC) allowed */

	u8 rxchain_restore_delay;	/* delay time to restore default rxchain */

	s8 ldpc;		/* AUTO/ON/OFF ldpc cap supported */
	u8 txcore[MAX_STREAMS_SUPPORTED + 1];	/* bitmap of selected core for each Nsts */
	s8 spatial_policy;
} wlc_stf_t;

#define WLC_STF_SS_STBC_TX(wlc, scb) \
	(((wlc)->stf->txstreams > 1) && (((wlc)->band->band_stf_stbc_tx == ON) || \
	 (SCB_STBC_CAP((scb)) &&					\
	  (wlc)->band->band_stf_stbc_tx == AUTO &&			\
	  isset(&((wlc)->stf->ss_algo_channel), PHY_TXC1_MODE_STBC))))

#define WLC_STBC_CAP_PHY(wlc) (WLCISNPHY(wlc->band) && NREV_GE(wlc->band->phyrev, 3))

#define WLC_SGI_CAP_PHY(wlc) ((WLCISNPHY(wlc->band) && NREV_GE(wlc->band->phyrev, 3)) || \
	WLCISLCNPHY(wlc->band))

#define WLC_CHAN_PHYTYPE(x)     (((x) & RXS_CHAN_PHYTYPE_MASK) >> RXS_CHAN_PHYTYPE_SHIFT)
#define WLC_CHAN_CHANNEL(x)     (((x) & RXS_CHAN_ID_MASK) >> RXS_CHAN_ID_SHIFT)
#define WLC_RX_CHANNEL(rxh)	(WLC_CHAN_CHANNEL((rxh)->RxChan))

/* wlc_bss_info flag bit values */
#define WLC_BSS_HT		0x0020	/* BSS is HT (MIMO) capable */

/* Flags used in wlc_txq_info.stopped */
#define TXQ_STOP_FOR_PRIOFC_MASK	0x000000FF	/* per prio flow control bits */
#define TXQ_STOP_FOR_PKT_DRAIN		0x00000100	/* stop txq enqueue for packet drain */
#define TXQ_STOP_FOR_AMPDU_FLOW_CNTRL	0x00000200	/* stop txq enqueue for ampdu flow control */

#define WLC_HT_WEP_RESTRICT	0x01	/* restrict HT with WEP */
#define WLC_HT_TKIP_RESTRICT	0x02	/* restrict HT with TKIP */

/*
 * core state (mac)
 */
struct wlccore {
	uint coreidx;		/* # sb enumerated core */

	/* fifo */
	uint *txavail[NFIFO];	/* # tx descriptors available */
	s16 txpktpend[NFIFO];	/* tx admission control */

	macstat_t *macstat_snapshot;	/* mac hw prev read values */
};

/*
 * band state (phy+ana+radio)
 */
struct wlcband {
	int bandtype;		/* WLC_BAND_2G, WLC_BAND_5G */
	uint bandunit;		/* bandstate[] index */

	u16 phytype;		/* phytype */
	u16 phyrev;
	u16 radioid;
	u16 radiorev;
	wlc_phy_t *pi;		/* pointer to phy specific information */
	bool abgphy_encore;

	u8 gmode;		/* currently active gmode (see wlioctl.h) */

	struct scb *hwrs_scb;	/* permanent scb for hw rateset */

	wlc_rateset_t defrateset;	/* band-specific copy of default_bss.rateset */

	ratespec_t rspec_override;	/* 802.11 rate override */
	ratespec_t mrspec_override;	/* multicast rate override */
	u8 band_stf_ss_mode;	/* Configured STF type, 0:siso; 1:cdd */
	s8 band_stf_stbc_tx;	/* STBC TX 0:off; 1:force on; -1:auto */
	wlc_rateset_t hw_rateset;	/* rates supported by chip (phy-specific) */
	u8 basic_rate[WLC_MAXRATE + 1];	/* basic rates indexed by rate */
	bool mimo_cap_40;	/* 40 MHz cap enabled on this band */
	s8 antgain;		/* antenna gain from srom */

	u16 CWmin;		/* The minimum size of contention window, in unit of aSlotTime */
	u16 CWmax;		/* The maximum size of contention window, in unit of aSlotTime */
	u16 bcntsfoff;	/* beacon tsf offset */
};

/* tx completion callback takes 3 args */
typedef void (*pkcb_fn_t) (struct wlc_info *wlc, uint txstatus, void *arg);

typedef struct pkt_cb {
	pkcb_fn_t fn;		/* function to call when tx frame completes */
	void *arg;		/* void arg for fn */
	u8 nextidx;		/* index of next call back if threading */
	bool entered;		/* recursion check */
} pkt_cb_t;

	/* module control blocks */
typedef struct modulecb {
	char name[32];		/* module name : NULL indicates empty array member */
	const bcm_iovar_t *iovars;	/* iovar table */
	void *hdl;		/* handle passed when handler 'doiovar' is called */
	watchdog_fn_t watchdog_fn;	/* watchdog handler */
	iovar_fn_t iovar_fn;	/* iovar handler */
	down_fn_t down_fn;	/* down handler. Note: the int returned
				 * by the down function is a count of the
				 * number of timers that could not be
				 * freed.
				 */
} modulecb_t;

	/* dump control blocks */
typedef struct dumpcb_s {
	const char *name;	/* dump name */
	dump_fn_t dump_fn;	/* 'wl dump' handler */
	void *dump_fn_arg;
	struct dumpcb_s *next;
} dumpcb_t;

/* virtual interface */
struct wlc_if {
	struct wlc_if *next;
	u8 type;		/* WLC_IFTYPE_BSS or WLC_IFTYPE_WDS */
	u8 index;		/* assigned in wl_add_if(), index of the wlif if any,
				 * not necessarily corresponding to bsscfg._idx or
				 * AID2PVBMAP(scb).
				 */
	u8 flags;		/* flags for the interface */
	struct wl_if *wlif;		/* pointer to wlif */
	struct wlc_txq_info *qi;	/* pointer to associated tx queue */
	union {
		struct scb *scb;	/* pointer to scb if WLC_IFTYPE_WDS */
		struct wlc_bsscfg *bsscfg;	/* pointer to bsscfg if WLC_IFTYPE_BSS */
	} u;
};

/* flags for the interface */
#define WLC_IF_LINKED		0x02	/* this interface is linked to a wl_if */

typedef struct wlc_hwband {
	int bandtype;		/* WLC_BAND_2G, WLC_BAND_5G */
	uint bandunit;		/* bandstate[] index */
	u16 mhfs[MHFMAX];	/* MHF array shadow */
	u8 bandhw_stf_ss_mode;	/* HW configured STF type, 0:siso; 1:cdd */
	u16 CWmin;
	u16 CWmax;
	u32 core_flags;

	u16 phytype;		/* phytype */
	u16 phyrev;
	u16 radioid;
	u16 radiorev;
	wlc_phy_t *pi;		/* pointer to phy specific information */
	bool abgphy_encore;
} wlc_hwband_t;

struct wlc_hw_info {
	struct osl_info *osh;		/* pointer to os handle */
	bool _piomode;		/* true if pio mode */
	struct wlc_info *wlc;

	/* fifo */
	struct hnddma_pub *di[NFIFO];	/* hnddma handles, per fifo */

	uint unit;		/* device instance number */

	/* version info */
	u16 vendorid;	/* PCI vendor id */
	u16 deviceid;	/* PCI device id */
	uint corerev;		/* core revision */
	u8 sromrev;		/* version # of the srom */
	u16 boardrev;	/* version # of particular board */
	u32 boardflags;	/* Board specific flags from srom */
	u32 boardflags2;	/* More board flags if sromrev >= 4 */
	u32 machwcap;	/* MAC capabilities */
	u32 machwcap_backup;	/* backup of machwcap */
	u16 ucode_dbgsel;	/* dbgsel for ucode debug(config gpio) */

	si_t *sih;		/* SB handle (cookie for siutils calls) */
	char *vars;		/* "environment" name=value */
	uint vars_size;		/* size of vars, free vars on detach */
	d11regs_t *regs;	/* pointer to device registers */
	void *physhim;		/* phy shim layer handler */
	void *phy_sh;		/* pointer to shared phy state */
	wlc_hwband_t *band;	/* pointer to active per-band state */
	wlc_hwband_t *bandstate[MAXBANDS];	/* per-band state (one per phy/radio) */
	u16 bmac_phytxant;	/* cache of high phytxant state */
	bool shortslot;		/* currently using 11g ShortSlot timing */
	u16 SRL;		/* 802.11 dot11ShortRetryLimit */
	u16 LRL;		/* 802.11 dot11LongRetryLimit */
	u16 SFBL;		/* Short Frame Rate Fallback Limit */
	u16 LFBL;		/* Long Frame Rate Fallback Limit */

	bool up;		/* d11 hardware up and running */
	uint now;		/* # elapsed seconds */
	uint _nbands;		/* # bands supported */
	chanspec_t chanspec;	/* bmac chanspec shadow */

	uint *txavail[NFIFO];	/* # tx descriptors available */
	u16 *xmtfifo_sz;	/* fifo size in 256B for each xmt fifo */

	mbool pllreq;		/* pll requests to keep PLL on */

	u8 suspended_fifos;	/* Which TX fifo to remain awake for */
	u32 maccontrol;	/* Cached value of maccontrol */
	uint mac_suspend_depth;	/* current depth of mac_suspend levels */
	u32 wake_override;	/* Various conditions to force MAC to WAKE mode */
	u32 mute_override;	/* Prevent ucode from sending beacons */
	u8 etheraddr[ETH_ALEN];	/* currently configured ethernet address */
	u32 led_gpio_mask;	/* LED GPIO Mask */
	bool noreset;		/* true= do not reset hw, used by WLC_OUT */
	bool forcefastclk;	/* true if the h/w is forcing the use of fast clk */
	bool clk;		/* core is out of reset and has clock */
	bool sbclk;		/* sb has clock */
	struct bmac_pmq *bmac_pmq; /*  bmac PM states derived from ucode PMQ */
	bool phyclk;		/* phy is out of reset and has clock */
	bool dma_lpbk;		/* core is in DMA loopback */

	bool ucode_loaded;	/* true after ucode downloaded */


	u8 hw_stf_ss_opmode;	/* STF single stream operation mode */
	u8 antsel_type;	/* Type of boardlevel mimo antenna switch-logic
				 * 0 = N/A, 1 = 2x4 board, 2 = 2x3 CB2 board
				 */
	u32 antsel_avail;	/*
				 * put struct antsel_info here if more info is
				 * needed
				 */
};

/* TX Queue information
 *
 * Each flow of traffic out of the device has a TX Queue with independent
 * flow control. Several interfaces may be associated with a single TX Queue
 * if they belong to the same flow of traffic from the device. For multi-channel
 * operation there are independent TX Queues for each channel.
 */
typedef struct wlc_txq_info {
	struct wlc_txq_info *next;
	struct pktq q;
	uint stopped;		/* tx flow control bits */
} wlc_txq_info_t;

/*
 * Principal common (os-independent) software data structure.
 */
struct wlc_info {
	struct wlc_pub *pub;		/* pointer to wlc public state */
	struct osl_info *osh;		/* pointer to os handle */
	struct wl_info *wl;	/* pointer to os-specific private state */
	d11regs_t *regs;	/* pointer to device registers */

	struct wlc_hw_info *hw;	/* HW related state used primarily by BMAC */

	/* clock */
	int clkreq_override;	/* setting for clkreq for PCIE : Auto, 0, 1 */
	u16 fastpwrup_dly;	/* time in us needed to bring up d11 fast clock */

	/* interrupt */
	u32 macintstatus;	/* bit channel between isr and dpc */
	u32 macintmask;	/* sw runtime master macintmask value */
	u32 defmacintmask;	/* default "on" macintmask value */

	/* up and down */
	bool device_present;	/* (removable) device is present */

	bool clk;		/* core is out of reset and has clock */

	/* multiband */
	struct wlccore *core;	/* pointer to active io core */
	struct wlcband *band;	/* pointer to active per-band state */
	struct wlccore *corestate;	/* per-core state (one per hw core) */
	/* per-band state (one per phy/radio): */
	struct wlcband *bandstate[MAXBANDS];

	bool war16165;		/* PCI slow clock 16165 war flag */

	bool tx_suspended;	/* data fifos need to remain suspended */

	uint txpend16165war;

	/* packet queue */
	uint qvalid;		/* DirFrmQValid and BcMcFrmQValid */

	/* Regulatory power limits */
	s8 txpwr_local_max;	/* regulatory local txpwr max */
	u8 txpwr_local_constraint;	/* local power contraint in dB */


	struct ampdu_info *ampdu;	/* ampdu module handler */
	struct antsel_info *asi;	/* antsel module handler */
	wlc_cm_info_t *cmi;	/* channel manager module handler */

	void *btparam;		/* bus type specific cookie */

	uint vars_size;		/* size of vars, free vars on detach */

	u16 vendorid;	/* PCI vendor id */
	u16 deviceid;	/* PCI device id */
	uint ucode_rev;		/* microcode revision */

	u32 machwcap;	/* MAC capabilities, BMAC shadow */

	u8 perm_etheraddr[ETH_ALEN];	/* original sprom local ethernet address */

	bool bandlocked;	/* disable auto multi-band switching */
	bool bandinit_pending;	/* track band init in auto band */

	bool radio_monitor;	/* radio timer is running */
	bool down_override;	/* true=down */
	bool going_down;	/* down path intermediate variable */

	bool mpc;		/* enable minimum power consumption */
	u8 mpc_dlycnt;	/* # of watchdog cnt before turn disable radio */
	u8 mpc_offcnt;	/* # of watchdog cnt that radio is disabled */
	u8 mpc_delay_off;	/* delay radio disable by # of watchdog cnt */
	u8 prev_non_delay_mpc;	/* prev state wlc_is_non_delay_mpc */

	/* timer */
	struct wl_timer *wdtimer;	/* timer for watchdog routine */
	uint fast_timer;	/* Periodic timeout for 'fast' timer */
	uint slow_timer;	/* Periodic timeout for 'slow' timer */
	uint glacial_timer;	/* Periodic timeout for 'glacial' timer */
	uint phycal_mlo;	/* last time measurelow calibration was done */
	uint phycal_txpower;	/* last time txpower calibration was done */

	struct wl_timer *radio_timer;	/* timer for hw radio button monitor routine */
	struct wl_timer *pspoll_timer;	/* periodic pspoll timer */

	/* promiscuous */
	bool monitor;		/* monitor (MPDU sniffing) mode */
	bool bcnmisc_ibss;	/* bcns promisc mode override for IBSS */
	bool bcnmisc_scan;	/* bcns promisc mode override for scan */
	bool bcnmisc_monitor;	/* bcns promisc mode override for monitor */

	u8 bcn_wait_prd;	/* max waiting period (for beacon) in 1024TU */

	/* driver feature */
	bool _rifs;		/* enable per-packet rifs */
	s32 rifs_advert;	/* RIFS mode advertisement */
	s8 sgi_tx;		/* sgi tx */
	bool wet;		/* true if wireless ethernet bridging mode */

	/* AP-STA synchronization, power save */
	bool check_for_unaligned_tbtt;	/* check unaligned tbtt flag */
	bool PM_override;	/* no power-save flag, override PM(user input) */
	bool PMenabled;		/* current power-management state (CAM or PS) */
	bool PMpending;		/* waiting for tx status with PM indicated set */
	bool PMblocked;		/* block any PSPolling in PS mode, used to buffer
				 * AP traffic, also used to indicate in progress
				 * of scan, rm, etc. off home channel activity.
				 */
	bool PSpoll;		/* whether there is an outstanding PS-Poll frame */
	u8 PM;		/* power-management mode (CAM, PS or FASTPS) */
	bool PMawakebcn;	/* bcn recvd during current waking state */

	bool WME_PM_blocked;	/* Can STA go to PM when in WME Auto mode */
	bool wake;		/* host-specified PS-mode sleep state */
	u8 pspoll_prd;	/* pspoll interval in milliseconds */
	u8 bcn_li_bcn;	/* beacon listen interval in # beacons */
	u8 bcn_li_dtim;	/* beacon listen interval in # dtims */

	bool WDarmed;		/* watchdog timer is armed */
	u32 WDlast;		/* last time wlc_watchdog() was called */

	/* WME */
	ac_bitmap_t wme_dp;	/* Discard (oldest first) policy per AC */
	bool wme_apsd;		/* enable Advanced Power Save Delivery */
	ac_bitmap_t wme_admctl;	/* bit i set if AC i under admission control */
	u16 edcf_txop[AC_COUNT];	/* current txop for each ac */
	wme_param_ie_t wme_param_ie;	/* WME parameter info element, which on STA
					 * contains parameters in use locally, and on
					 * AP contains parameters advertised to STA
					 * in beacons and assoc responses.
					 */
	bool wme_prec_queuing;	/* enable/disable non-wme STA prec queuing */
	u16 wme_retries[AC_COUNT];	/* per-AC retry limits */

	int vlan_mode;		/* OK to use 802.1Q Tags (ON, OFF, AUTO) */
	u16 tx_prec_map;	/* Precedence map based on HW FIFO space */
	u16 fifo2prec_map[NFIFO];	/* pointer to fifo2_prec map based on WME */

	/* BSS Configurations */
	wlc_bsscfg_t *bsscfg[WLC_MAXBSSCFG];	/* set of BSS configurations, idx 0 is default and
						 * always valid
						 */
	wlc_bsscfg_t *cfg;	/* the primary bsscfg (can be AP or STA) */
	u8 stas_associated;	/* count of ASSOCIATED STA bsscfgs */
	u8 aps_associated;	/* count of UP AP bsscfgs */
	u8 block_datafifo;	/* prohibit posting frames to data fifos */
	bool bcmcfifo_drain;	/* TX_BCMC_FIFO is set to drain */

	/* tx queue */
	wlc_txq_info_t *tx_queues;	/* common TX Queue list */

	/* event */
	wlc_eventq_t *eventq;	/* event queue for deferred processing */

	/* security */
	wsec_key_t *wsec_keys[WSEC_MAX_KEYS];	/* dynamic key storage */
	wsec_key_t *wsec_def_keys[WLC_DEFAULT_KEYS];	/* default key storage */
	bool wsec_swkeys;	/* indicates that all keys should be
				 * treated as sw keys (used for debugging)
				 */
	modulecb_t *modulecb;
	dumpcb_t *dumpcb_head;

	u8 mimoft;		/* SIGN or 11N */
	u8 mimo_band_bwcap;	/* bw cap per band type */
	s8 txburst_limit_override;	/* tx burst limit override */
	u16 txburst_limit;	/* tx burst limit value */
	s8 cck_40txbw;	/* 11N, cck tx b/w override when in 40MHZ mode */
	s8 ofdm_40txbw;	/* 11N, ofdm tx b/w override when in 40MHZ mode */
	s8 mimo_40txbw;	/* 11N, mimo tx b/w override when in 40MHZ mode */
	/* HT CAP IE being advertised by this node: */
	struct ieee80211_ht_cap ht_cap;

	uint seckeys;		/* 54 key table shm address */
	uint tkmickeys;		/* 12 TKIP MIC key table shm address */

	wlc_bss_info_t *default_bss;	/* configured BSS parameters */

	u16 AID;		/* association ID */
	u16 counter;		/* per-sdu monotonically increasing counter */
	u16 mc_fid_counter;	/* BC/MC FIFO frame ID counter */

	bool ibss_allowed;	/* false, all IBSS will be ignored during a scan
				 * and the driver will not allow the creation of
				 * an IBSS network
				 */
	bool ibss_coalesce_allowed;

	char country_default[WLC_CNTRY_BUF_SZ];	/* saved country for leaving 802.11d
						 * auto-country mode
						 */
	char autocountry_default[WLC_CNTRY_BUF_SZ];	/* initial country for 802.11d
							 * auto-country mode
							 */
#ifdef BCMDBG
	bcm_tlv_t *country_ie_override;	/* debug override of announced Country IE */
#endif

	u16 prb_resp_timeout;	/* do not send prb resp if request older than this,
					 * 0 = disable
					 */

	wlc_rateset_t sup_rates_override;	/* use only these rates in 11g supported rates if
						 * specifed
						 */

	chanspec_t home_chanspec;	/* shared home chanspec */

	/* PHY parameters */
	chanspec_t chanspec;	/* target operational channel */
	u16 usr_fragthresh;	/* user configured fragmentation threshold */
	u16 fragthresh[NFIFO];	/* per-fifo fragmentation thresholds */
	u16 RTSThresh;	/* 802.11 dot11RTSThreshold */
	u16 SRL;		/* 802.11 dot11ShortRetryLimit */
	u16 LRL;		/* 802.11 dot11LongRetryLimit */
	u16 SFBL;		/* Short Frame Rate Fallback Limit */
	u16 LFBL;		/* Long Frame Rate Fallback Limit */

	/* network config */
	bool shortpreamble;	/* currently operating with CCK ShortPreambles */
	bool shortslot;		/* currently using 11g ShortSlot timing */
	s8 barker_preamble;	/* current Barker Preamble Mode */
	s8 shortslot_override;	/* 11g ShortSlot override */
	bool include_legacy_erp;	/* include Legacy ERP info elt ID 47 as well as g ID 42 */
	bool barker_overlap_control;	/* true: be aware of overlapping BSSs for barker */
	bool ignore_bcns;	/* override: ignore non shortslot bcns in a 11g network */
	bool legacy_probe;	/* restricts probe requests to CCK rates */

	wlc_protection_t *protection;
	s8 PLCPHdr_override;	/* 802.11b Preamble Type override */

	wlc_stf_t *stf;

	pkt_cb_t *pkt_callback;	/* tx completion callback handlers */

	u32 txretried;	/* tx retried number in one msdu */

	ratespec_t bcn_rspec;	/* save bcn ratespec purpose */

	bool apsd_sta_usp;	/* Unscheduled Service Period in progress on STA */
	struct wl_timer *apsd_trigger_timer;	/* timer for wme apsd trigger frames */
	u32 apsd_trigger_timeout;	/* timeout value for apsd_trigger_timer (in ms)
					 * 0 == disable
					 */
	ac_bitmap_t apsd_trigger_ac;	/* Permissible Access Category in which APSD Null
					 * Trigger frames can be send
					 */
	u8 htphy_membership;	/* HT PHY membership */

	bool _regulatory_domain;	/* 802.11d enabled? */

	u8 mimops_PM;

	u8 txpwr_percent;	/* power output percentage */

	u8 ht_wsec_restriction;	/* the restriction of HT with TKIP or WEP */

	uint tempsense_lasttime;

	u16 tx_duty_cycle_ofdm;	/* maximum allowed duty cycle for OFDM */
	u16 tx_duty_cycle_cck;	/* maximum allowed duty cycle for CCK */

	u16 next_bsscfg_ID;

	struct wlc_if *wlcif_list;	/* linked list of wlc_if structs */
	wlc_txq_info_t *active_queue;	/* txq for the currently active transmit context */
	u32 mpc_dur;		/* total time (ms) in mpc mode except for the
				 * portion since radio is turned off last time
				 */
	u32 mpc_laston_ts;	/* timestamp (ms) when radio is turned off last
				 * time
				 */
	bool pr80838_war;
	uint hwrxoff;
};

/* antsel module specific state */
struct antsel_info {
	struct wlc_info *wlc;	/* pointer to main wlc structure */
	struct wlc_pub *pub;		/* pointer to public fn */
	u8 antsel_type;	/* Type of boardlevel mimo antenna switch-logic
				 * 0 = N/A, 1 = 2x4 board, 2 = 2x3 CB2 board
				 */
	u8 antsel_antswitch;	/* board level antenna switch type */
	bool antsel_avail;	/* Ant selection availability (SROM based) */
	wlc_antselcfg_t antcfg_11n;	/* antenna configuration */
	wlc_antselcfg_t antcfg_cur;	/* current antenna config (auto) */
};

#define	CHANNEL_BANDUNIT(wlc, ch) (((ch) <= CH_MAX_2G_CHANNEL) ? BAND_2G_INDEX : BAND_5G_INDEX)
#define	OTHERBANDUNIT(wlc)	((uint)((wlc)->band->bandunit ? BAND_2G_INDEX : BAND_5G_INDEX))

#define IS_MBAND_UNLOCKED(wlc) \
	((NBANDS(wlc) > 1) && !(wlc)->bandlocked)

#define WLC_BAND_PI_RADIO_CHANSPEC wlc_phy_chanspec_get(wlc->band->pi)

/* sum the individual fifo tx pending packet counts */
#define	TXPKTPENDTOT(wlc) ((wlc)->core->txpktpend[0] + (wlc)->core->txpktpend[1] + \
	(wlc)->core->txpktpend[2] + (wlc)->core->txpktpend[3])
#define TXPKTPENDGET(wlc, fifo)		((wlc)->core->txpktpend[(fifo)])
#define TXPKTPENDINC(wlc, fifo, val)	((wlc)->core->txpktpend[(fifo)] += (val))
#define TXPKTPENDDEC(wlc, fifo, val)	((wlc)->core->txpktpend[(fifo)] -= (val))
#define TXPKTPENDCLR(wlc, fifo)		((wlc)->core->txpktpend[(fifo)] = 0)
#define TXAVAIL(wlc, fifo)		(*(wlc)->core->txavail[(fifo)])
#define GETNEXTTXP(wlc, _queue)								\
		dma_getnexttxp((wlc)->hw->di[(_queue)], HNDDMA_RANGE_TRANSMITTED)

#define WLC_IS_MATCH_SSID(wlc, ssid1, ssid2, len1, len2) \
	((len1 == len2) && !memcmp(ssid1, ssid2, len1))

extern void wlc_high_dpc(struct wlc_info *wlc, u32 macintstatus);
extern void wlc_fatal_error(struct wlc_info *wlc);
extern void wlc_bmac_rpc_watchdog(struct wlc_info *wlc);
extern void wlc_recv(struct wlc_info *wlc, struct sk_buff *p);
extern bool wlc_dotxstatus(struct wlc_info *wlc, tx_status_t *txs, u32 frm_tx2);
extern void wlc_txfifo(struct wlc_info *wlc, uint fifo, struct sk_buff *p,
		       bool commit, s8 txpktpend);
extern void wlc_txfifo_complete(struct wlc_info *wlc, uint fifo, s8 txpktpend);
extern void wlc_txq_enq(void *ctx, struct scb *scb, struct sk_buff *sdu,
			uint prec);
extern void wlc_info_init(struct wlc_info *wlc, int unit);
extern void wlc_print_txstatus(tx_status_t *txs);
extern int wlc_xmtfifo_sz_get(struct wlc_info *wlc, uint fifo, uint *blocks);
extern void wlc_write_template_ram(struct wlc_info *wlc, int offset, int len,
				   void *buf);
extern void wlc_write_hw_bcntemplates(struct wlc_info *wlc, void *bcn, int len,
				      bool both);
#if defined(BCMDBG)
extern void wlc_get_rcmta(struct wlc_info *wlc, int idx,
			  u8 *addr);
#endif
extern void wlc_set_rcmta(struct wlc_info *wlc, int idx,
			  const u8 *addr);
extern void wlc_read_tsf(struct wlc_info *wlc, u32 *tsf_l_ptr,
			 u32 *tsf_h_ptr);
extern void wlc_set_cwmin(struct wlc_info *wlc, u16 newmin);
extern void wlc_set_cwmax(struct wlc_info *wlc, u16 newmax);
extern void wlc_fifoerrors(struct wlc_info *wlc);
extern void wlc_pllreq(struct wlc_info *wlc, bool set, mbool req_bit);
extern void wlc_reset_bmac_done(struct wlc_info *wlc);
extern void wlc_hwtimer_gptimer_set(struct wlc_info *wlc, uint us);
extern void wlc_hwtimer_gptimer_abort(struct wlc_info *wlc);

#if defined(BCMDBG)
extern void wlc_print_rxh(d11rxhdr_t *rxh);
extern void wlc_print_hdrs(struct wlc_info *wlc, const char *prefix, u8 *frame,
			   d11txh_t *txh, d11rxhdr_t *rxh, uint len);
extern void wlc_print_txdesc(d11txh_t *txh);
#else
#define wlc_print_txdesc(a)
#endif
#if defined(BCMDBG)
extern void wlc_print_dot11_mac_hdr(u8 *buf, int len);
#endif

extern void wlc_setxband(struct wlc_hw_info *wlc_hw, uint bandunit);
extern void wlc_coredisable(struct wlc_hw_info *wlc_hw);

extern bool wlc_valid_rate(struct wlc_info *wlc, ratespec_t rate, int band,
			   bool verbose);
extern void wlc_ap_upd(struct wlc_info *wlc);

/* helper functions */
extern void wlc_shm_ssid_upd(struct wlc_info *wlc, wlc_bsscfg_t *cfg);
extern int wlc_set_gmode(struct wlc_info *wlc, u8 gmode, bool config);

extern void wlc_mac_bcn_promisc_change(struct wlc_info *wlc, bool promisc);
extern void wlc_mac_bcn_promisc(struct wlc_info *wlc);
extern void wlc_mac_promisc(struct wlc_info *wlc);
extern void wlc_txflowcontrol(struct wlc_info *wlc, wlc_txq_info_t *qi, bool on,
			      int prio);
extern void wlc_txflowcontrol_override(struct wlc_info *wlc, wlc_txq_info_t *qi,
				       bool on, uint override);
extern bool wlc_txflowcontrol_prio_isset(struct wlc_info *wlc,
					 wlc_txq_info_t *qi, int prio);
extern void wlc_send_q(struct wlc_info *wlc, wlc_txq_info_t *qi);
extern int wlc_prep_pdu(struct wlc_info *wlc, struct sk_buff *pdu, uint *fifo);

extern u16 wlc_calc_lsig_len(struct wlc_info *wlc, ratespec_t ratespec,
				uint mac_len);
extern ratespec_t wlc_rspec_to_rts_rspec(struct wlc_info *wlc, ratespec_t rspec,
					 bool use_rspec, u16 mimo_ctlchbw);
extern u16 wlc_compute_rtscts_dur(struct wlc_info *wlc, bool cts_only,
				     ratespec_t rts_rate, ratespec_t frame_rate,
				     u8 rts_preamble_type,
				     u8 frame_preamble_type, uint frame_len,
				     bool ba);

extern void wlc_tbtt(struct wlc_info *wlc, d11regs_t *regs);

#if defined(BCMDBG)
extern void wlc_dump_ie(struct wlc_info *wlc, bcm_tlv_t *ie,
			struct bcmstrbuf *b);
#endif

extern bool wlc_ps_check(struct wlc_info *wlc);
extern void wlc_reprate_init(struct wlc_info *wlc);
extern void wlc_bsscfg_reprate_init(wlc_bsscfg_t *bsscfg);
extern void wlc_uint64_sub(u32 *a_high, u32 *a_low, u32 b_high,
			   u32 b_low);
extern u32 wlc_calc_tbtt_offset(u32 bi, u32 tsf_h, u32 tsf_l);

/* Shared memory access */
extern void wlc_write_shm(struct wlc_info *wlc, uint offset, u16 v);
extern u16 wlc_read_shm(struct wlc_info *wlc, uint offset);
extern void wlc_set_shm(struct wlc_info *wlc, uint offset, u16 v, int len);
extern void wlc_copyto_shm(struct wlc_info *wlc, uint offset, const void *buf,
			   int len);
extern void wlc_copyfrom_shm(struct wlc_info *wlc, uint offset, void *buf,
			     int len);

extern void wlc_update_beacon(struct wlc_info *wlc);
extern void wlc_bss_update_beacon(struct wlc_info *wlc,
				  struct wlc_bsscfg *bsscfg);

extern void wlc_update_probe_resp(struct wlc_info *wlc, bool suspend);
extern void wlc_bss_update_probe_resp(struct wlc_info *wlc, wlc_bsscfg_t *cfg,
				      bool suspend);

extern bool wlc_ismpc(struct wlc_info *wlc);
extern bool wlc_is_non_delay_mpc(struct wlc_info *wlc);
extern void wlc_radio_mpc_upd(struct wlc_info *wlc);
extern bool wlc_prec_enq(struct wlc_info *wlc, struct pktq *q, void *pkt,
			 int prec);
extern bool wlc_prec_enq_head(struct wlc_info *wlc, struct pktq *q,
			      struct sk_buff *pkt, int prec, bool head);
extern u16 wlc_phytxctl1_calc(struct wlc_info *wlc, ratespec_t rspec);
extern void wlc_compute_plcp(struct wlc_info *wlc, ratespec_t rate, uint length,
			     u8 *plcp);
extern uint wlc_calc_frame_time(struct wlc_info *wlc, ratespec_t ratespec,
				u8 preamble_type, uint mac_len);

extern void wlc_set_chanspec(struct wlc_info *wlc, chanspec_t chanspec);

extern bool wlc_timers_init(struct wlc_info *wlc, int unit);

extern const bcm_iovar_t wlc_iovars[];

extern int wlc_doiovar(void *hdl, const bcm_iovar_t *vi, u32 actionid,
		       const char *name, void *params, uint p_len, void *arg,
		       int len, int val_size, struct wlc_if *wlcif);

#if defined(BCMDBG)
extern void wlc_print_ies(struct wlc_info *wlc, u8 *ies, uint ies_len);
#endif

extern int wlc_set_nmode(struct wlc_info *wlc, s32 nmode);
extern void wlc_ht_mimops_cap_update(struct wlc_info *wlc, u8 mimops_mode);
extern void wlc_mimops_action_ht_send(struct wlc_info *wlc,
				      wlc_bsscfg_t *bsscfg, u8 mimops_mode);

extern void wlc_switch_shortslot(struct wlc_info *wlc, bool shortslot);
extern void wlc_set_bssid(wlc_bsscfg_t *cfg);
extern void wlc_edcf_setparams(wlc_bsscfg_t *cfg, bool suspend);

extern void wlc_set_ratetable(struct wlc_info *wlc);
extern int wlc_set_mac(wlc_bsscfg_t *cfg);
extern void wlc_beacon_phytxctl_txant_upd(struct wlc_info *wlc,
					  ratespec_t bcn_rate);
extern void wlc_mod_prb_rsp_rate_table(struct wlc_info *wlc, uint frame_len);
extern ratespec_t wlc_lowest_basic_rspec(struct wlc_info *wlc,
					 wlc_rateset_t *rs);
extern u16 wlc_compute_bcntsfoff(struct wlc_info *wlc, ratespec_t rspec,
				    bool short_preamble, bool phydelay);
extern void wlc_radio_disable(struct wlc_info *wlc);
extern void wlc_bcn_li_upd(struct wlc_info *wlc);

extern int wlc_get_revision_info(struct wlc_info *wlc, void *buf, uint len);
extern void wlc_out(struct wlc_info *wlc);
extern void wlc_set_home_chanspec(struct wlc_info *wlc, chanspec_t chanspec);
extern void wlc_watchdog_upd(struct wlc_info *wlc, bool tbtt);
extern bool wlc_ps_allowed(struct wlc_info *wlc);
extern bool wlc_stay_awake(struct wlc_info *wlc);
extern void wlc_wme_initparams_sta(struct wlc_info *wlc, wme_param_ie_t *pe);

extern void wlc_bss_list_free(struct wlc_info *wlc, wlc_bss_list_t *bss_list);
extern void wlc_ht_mimops_cap_update(struct wlc_info *wlc, u8 mimops_mode);
#endif				/* _wlc_h_ */
