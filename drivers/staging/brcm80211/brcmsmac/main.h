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

#ifndef _BRCM_MAIN_H_
#define _BRCM_MAIN_H_

#include <linux/etherdevice.h>

#include <brcmu_utils.h>
#include "types.h"
#include "d11.h"

#define MA_WINDOW_SZ		8	/* moving average window size */
#define	BRCMS_HWRXOFF		38	/* chip rx buffer offset */
#define	INVCHANNEL		255	/* invalid channel */
/* max # supported core revisions (0 .. MAXCOREREV - 1) */
#define	MAXCOREREV		28
/* max # brcms_c_module_register() calls */
#define BRCMS_MAXMODULES	22

#define SEQNUM_SHIFT		4
#define AMPDU_DELIMITER_LEN	4
#define SEQNUM_MAX		0x1000

#define	APHY_CWMIN		15
#define PHY_CWMAX		1023

#define EDCF_AIFSN_MIN               1
#define FRAGNUM_MASK		0xF

#define NTXRATE			64	/* # tx MPDUs rate is reported for */

#define BRCMS_BITSCNT(x)	brcmu_bitcount((u8 *)&(x), sizeof(u8))

/* Maximum wait time for a MAC suspend */
/* uS: 83mS is max packet time (64KB ampdu @ 6Mbps) */
#define	BRCMS_MAX_MAC_SUSPEND	83000

/* responses for probe requests older that this are tossed, zero to disable */
#define BRCMS_PRB_RESP_TIMEOUT	0	/* Disable probe response timeout */

/* transmit buffer max headroom for protocol headers */
#define TXOFF (D11_TXH_LEN + D11_PHY_HDR_LEN)

#define AC_COUNT		4

/* Macros for doing definition and get/set of bitfields
 * Usage example, e.g. a three-bit field (bits 4-6):
 *    #define <NAME>_M	BITFIELD_MASK(3)
 *    #define <NAME>_S	4
 * ...
 *    regval = R_REG(osh, &regs->regfoo);
 *    field = GFIELD(regval, <NAME>);
 *    regval = SFIELD(regval, <NAME>, 1);
 *    W_REG(osh, &regs->regfoo, regval);
 */
#define BITFIELD_MASK(width) \
		(((unsigned)1 << (width)) - 1)
#define GFIELD(val, field) \
		(((val) >> field ## _S) & field ## _M)
#define SFIELD(val, field, bits) \
		(((val) & (~(field ## _M << field ## _S))) | \
		 ((unsigned)(bits) << field ## _S))

#define	SW_TIMER_MAC_STAT_UPD		30	/* periodic MAC stats update */

/* Double check that unsupported cores are not enabled */
#if CONF_MSK(D11CONF, 0x4f) || CONF_GE(D11CONF, MAXCOREREV)
#error "Configuration for D11CONF includes unsupported versions."
#endif				/* Bad versions */

#define	VALID_COREREV(corerev)	CONF_HAS(D11CONF, corerev)

/* values for shortslot_override */
#define BRCMS_SHORTSLOT_AUTO	-1 /* Driver will manage Shortslot setting */
#define BRCMS_SHORTSLOT_OFF	0  /* Turn off short slot */
#define BRCMS_SHORTSLOT_ON	1  /* Turn on short slot */

/* value for short/long and mixmode/greenfield preamble */
#define BRCMS_LONG_PREAMBLE	(0)
#define BRCMS_SHORT_PREAMBLE	(1 << 0)
#define BRCMS_GF_PREAMBLE		(1 << 1)
#define BRCMS_MM_PREAMBLE		(1 << 2)
#define BRCMS_IS_MIMO_PREAMBLE(_pre) (((_pre) == BRCMS_GF_PREAMBLE) || \
				      ((_pre) == BRCMS_MM_PREAMBLE))

/* values for barker_preamble */
#define BRCMS_BARKER_SHORT_ALLOWED	0	/* Short pre-amble allowed */

/* A fifo is full. Clear precedences related to that FIFO */
#define BRCMS_TX_FIFO_CLEAR(wlc, fifo) \
			((wlc)->tx_prec_map &= ~(wlc)->fifo2prec_map[fifo])

/* Fifo is NOT full. Enable precedences for that FIFO */
#define BRCMS_TX_FIFO_ENAB(wlc, fifo) \
			((wlc)->tx_prec_map |= (wlc)->fifo2prec_map[fifo])

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

/*
 * if wpa is in use then portopen is true when the
 * group key is plumbed otherwise it is always true
 */
#define WSEC_ENABLED(wsec) ((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))
#define BRCMS_SW_KEYS(wlc, bsscfg) ((((wlc)->wsec_swkeys) || \
	((bsscfg)->wsec & WSEC_SWFLAG)))

#define BRCMS_PORTOPEN(cfg) \
	(((cfg)->WPA_auth != WPA_AUTH_DISABLED && WSEC_ENABLED((cfg)->wsec)) ? \
	(cfg)->wsec_portopen : true)

#define PS_ALLOWED(wlc)	brcms_c_ps_allowed(wlc)

#define DATA_BLOCK_TX_SUPR	(1 << 4)

/* 802.1D Priority to TX FIFO number for wme */
extern const u8 prio2fifo[];

/* Ucode MCTL_WAKE override bits */
#define BRCMS_WAKE_OVERRIDE_CLKCTL	0x01
#define BRCMS_WAKE_OVERRIDE_PHYREG	0x02
#define BRCMS_WAKE_OVERRIDE_MACSUSPEND	0x04
#define BRCMS_WAKE_OVERRIDE_TXFIFO	0x08
#define BRCMS_WAKE_OVERRIDE_FORCEFAST	0x10

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
#define	RETRY_SHORT_FB			3 /* Short count for fallback rate */
#define	RETRY_LONG_FB			2 /* Long count for fallback rate */

#define	MAXTXPKTS		6	/* max # pkts pending */

/* frameburst */
#define	MAXTXFRAMEBURST		8 /* vanilla xpress mode: max frames/burst */
#define	MAXFRAMEBURST_TXOP	10000	/* Frameburst TXOP in usec */

/* Per-AC retry limit register definitions; uses defs.h bitfield macros */
#define EDCF_SHORT_S            0
#define EDCF_SFB_S              4
#define EDCF_LONG_S             8
#define EDCF_LFB_S              12
#define EDCF_SHORT_M            BITFIELD_MASK(4)
#define EDCF_SFB_M              BITFIELD_MASK(4)
#define EDCF_LONG_M             BITFIELD_MASK(4)
#define EDCF_LFB_M              BITFIELD_MASK(4)

#define	NFIFO			6	/* # tx/rx fifopairs */

#define BRCMS_WME_RETRY_SHORT_GET(wlc, ac) \
					GFIELD(wlc->wme_retries[ac], EDCF_SHORT)
#define BRCMS_WME_RETRY_SFB_GET(wlc, ac) \
					GFIELD(wlc->wme_retries[ac], EDCF_SFB)
#define BRCMS_WME_RETRY_LONG_GET(wlc, ac) \
					GFIELD(wlc->wme_retries[ac], EDCF_LONG)
#define BRCMS_WME_RETRY_LFB_GET(wlc, ac) \
					GFIELD(wlc->wme_retries[ac], EDCF_LFB)

#define BRCMS_WME_RETRY_SHORT_SET(wlc, ac, val) \
	(wlc->wme_retries[ac] = SFIELD(wlc->wme_retries[ac], EDCF_SHORT, val))
#define BRCMS_WME_RETRY_SFB_SET(wlc, ac, val) \
	(wlc->wme_retries[ac] = SFIELD(wlc->wme_retries[ac], EDCF_SFB, val))
#define BRCMS_WME_RETRY_LONG_SET(wlc, ac, val) \
	(wlc->wme_retries[ac] = SFIELD(wlc->wme_retries[ac], EDCF_LONG, val))
#define BRCMS_WME_RETRY_LFB_SET(wlc, ac, val) \
	(wlc->wme_retries[ac] = SFIELD(wlc->wme_retries[ac], EDCF_LFB, val))

/* PLL requests */

/* pll is shared on old chips */
#define BRCMS_PLLREQ_SHARED	0x1
/* hold pll for radio monitor register checking */
#define BRCMS_PLLREQ_RADIO_MON	0x2
/* hold/release pll for some short operation */
#define BRCMS_PLLREQ_FLIP		0x4

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
 * Even checking an sbconfig register read will not false trigger when the core
 * is in reset it breaks CF address mechanism. Accessing gphy phyversion will
 * cause SB error if aphy is in reset on 4306B0-DB. Need a simple accessible
 * reg with fixed 0/1 pattern (some platforms return all 0).
 * If clocks are present, call the sb routine which will figure out if the
 * device is removed.
 */
#define DEVICEREMOVED(wlc)      \
	((wlc->hw->clk) ?   \
	((R_REG(&wlc->hw->regs->maccontrol) & \
	(MCTL_PSM_JMP_0 | MCTL_IHR_EN)) != MCTL_IHR_EN) : \
	(ai_deviceremoved(wlc->hw->sih)))

#define BRCMS_UNIT(wlc)		((wlc)->pub->unit)

#define brcms_b_copyfrom_shm(wlc_hw, offset, buf, len)                 \
	brcms_b_copyfrom_objmem(wlc_hw, offset, buf, len, OBJADDR_SHM_SEL)

#define brcms_b_copyto_shm(wlc_hw, offset, buf, len)                   \
	brcms_b_copyto_objmem(wlc_hw, offset, buf, len, OBJADDR_SHM_SEL)

/*
 * 802.11 protection information
 *
 * _g: use g spec protection, driver internal.
 * g_override: override for use of g spec protection.
 * gmode_user: user config gmode, operating band->gmode is different.
 * overlap: Overlap BSS/IBSS protection for both 11g and 11n.
 * nmode_user: user config nmode, operating pub->nmode is different.
 * n_cfg: use OFDM protection on MIMO frames.
 * n_cfg_override: override for use of N protection.
 * nongf: non-GF present protection.
 * nongf_override: override for use of GF protection.
 * n_pam_override: override for preamble: MM or GF.
 * n_obss: indicated OBSS Non-HT STA present.
*/
struct brcms_protection {
	bool _g;
	s8 g_override;
	u8 gmode_user;
	s8 overlap;
	s8 nmode_user;
	s8 n_cfg;
	s8 n_cfg_override;
	bool nongf;
	s8 nongf_override;
	s8 n_pam_override;
	bool n_obss;
};

/*
 * anything affecting the single/dual streams/antenna operation
 *
 * hw_txchain: HW txchain bitmap cfg.
 * txchain: txchain bitmap being used.
 * txstreams: number of txchains being used.
 * hw_rxchain: HW rxchain bitmap cfg.
 * rxchain: rxchain bitmap being used.
 * rxstreams: number of rxchains being used.
 * ant_rx_ovr: rx antenna override.
 * txant: userTx antenna setting.
 * phytxant: phyTx antenna setting in txheader.
 * ss_opmode: singlestream Operational mode, 0:siso; 1:cdd.
 * ss_algosel_auto: if true, use wlc->stf->ss_algo_channel;
 *			else use wlc->band->stf->ss_mode_band.
 * ss_algo_channel: ss based on per-channel algo: 0: SISO, 1: CDD 2: STBC.
 * no_cddstbc: stf override, 1: no CDD (or STBC) allowed.
 * rxchain_restore_delay: delay time to restore default rxchain.
 * ldpc: AUTO/ON/OFF ldpc cap supported.
 * txcore[MAX_STREAMS_SUPPORTED + 1]: bitmap of selected core for each Nsts.
 * spatial_policy:
 */
struct brcms_stf {
	u8 hw_txchain;
	u8 txchain;
	u8 txstreams;
	u8 hw_rxchain;
	u8 rxchain;
	u8 rxstreams;
	u8 ant_rx_ovr;
	s8 txant;
	u16 phytxant;
	u8 ss_opmode;
	bool ss_algosel_auto;
	u16 ss_algo_channel;
	u8 no_cddstbc;
	u8 rxchain_restore_delay;
	s8 ldpc;
	u8 txcore[MAX_STREAMS_SUPPORTED + 1];
	s8 spatial_policy;
};

#define BRCMS_STF_SS_STBC_TX(wlc, scb) \
	(((wlc)->stf->txstreams > 1) && (((wlc)->band->band_stf_stbc_tx == ON) \
	 || (SCB_STBC_CAP((scb)) && (wlc)->band->band_stf_stbc_tx == AUTO && \
	     isset(&((wlc)->stf->ss_algo_channel), PHY_TXC1_MODE_STBC))))

#define BRCMS_STBC_CAP_PHY(wlc) (BRCMS_ISNPHY(wlc->band) && \
				 NREV_GE(wlc->band->phyrev, 3))

#define BRCMS_SGI_CAP_PHY(wlc) ((BRCMS_ISNPHY(wlc->band) && \
				 NREV_GE(wlc->band->phyrev, 3)) || \
				BRCMS_ISLCNPHY(wlc->band))

#define BRCMS_CHAN_PHYTYPE(x)     (((x) & RXS_CHAN_PHYTYPE_MASK) \
				   >> RXS_CHAN_PHYTYPE_SHIFT)
#define BRCMS_CHAN_CHANNEL(x)     (((x) & RXS_CHAN_ID_MASK) \
				   >> RXS_CHAN_ID_SHIFT)
#define BRCMS_RX_CHANNEL(rxh)	(BRCMS_CHAN_CHANNEL((rxh)->RxChan))

/* brcms_bss_info flag bit values */
#define BRCMS_BSS_HT		0x0020	/* BSS is HT (MIMO) capable */

/* Flags used in brcms_c_txq_info.stopped */
/* per prio flow control bits */
#define TXQ_STOP_FOR_PRIOFC_MASK	0x000000FF
/* stop txq enqueue for packet drain */
#define TXQ_STOP_FOR_PKT_DRAIN		0x00000100
/* stop txq enqueue for ampdu flow control */
#define TXQ_STOP_FOR_AMPDU_FLOW_CNTRL	0x00000200

#define BRCMS_HT_WEP_RESTRICT	0x01	/* restrict HT with WEP */
#define BRCMS_HT_TKIP_RESTRICT	0x02	/* restrict HT with TKIP */

/* Maximum # of keys that wl driver supports in S/W.
 * Keys supported in H/W is less than or equal to WSEC_MAX_KEYS.
 */
#define WSEC_MAX_KEYS		54 /* Max # of keys (50 + 4 default keys) */
#define BRCMS_DEFAULT_KEYS	4 /* Default # of keys */

/*
* Max # of keys currently supported:
*
*     s/w keys if WSEC_SW(wlc->wsec).
*     h/w keys otherwise.
*/
#define BRCMS_MAX_WSEC_KEYS(wlc) WSEC_MAX_KEYS

/* number of 802.11 default (non-paired, group keys) */
#define WSEC_MAX_DEFAULT_KEYS	4	/* # of default keys */

struct wsec_iv {
	u32 hi;		/* upper 32 bits of IV */
	u16 lo;		/* lower 16 bits of IV */
};

#define BRCMS_NUMRXIVS	16	/* # rx IVs (one per 802.11e TID) */

struct wsec_key {
	u8 ea[ETH_ALEN];	/* per station */
	u8 idx;		/* key index in wsec_keys array */
	u8 id;		/* key ID [0-3] */
	u8 algo;	/* CRYPTO_ALGO_AES_CCM, CRYPTO_ALGO_WEP128, etc */
	u8 rcmta;	/* rcmta entry index, same as idx by default */
	u16 flags;	/* misc flags */
	u8 algo_hw;	/* cache for hw register */
	u8 aes_mode;	/* cache for hw register */
	s8 iv_len;	/* IV length */
	s8 icv_len;	/* ICV length */
	u32 len;	/* key length..don't move this var */
	/* data is 4byte aligned */
	u8 data[WLAN_MAX_KEY_LEN];	/* key data */
	struct wsec_iv rxiv[BRCMS_NUMRXIVS];	/* Rx IV (one per TID) */
	struct wsec_iv txiv;		/* Tx IV */
};

/*
 * core state (mac)
 */
struct brcms_core {
	uint coreidx;		/* # sb enumerated core */

	/* fifo */
	uint *txavail[NFIFO];	/* # tx descriptors available */
	s16 txpktpend[NFIFO];	/* tx admission control */

	struct macstat *macstat_snapshot;	/* mac hw prev read values */
};

/*
 * band state (phy+ana+radio)
 */
struct brcms_band {
	int bandtype;		/* BRCM_BAND_2G, BRCM_BAND_5G */
	uint bandunit;		/* bandstate[] index */

	u16 phytype;		/* phytype */
	u16 phyrev;
	u16 radioid;
	u16 radiorev;
	struct brcms_phy_pub *pi; /* pointer to phy specific information */
	bool abgphy_encore;

	u8 gmode;		/* currently active gmode */

	struct scb *hwrs_scb;	/* permanent scb for hw rateset */

	/* band-specific copy of default_bss.rateset */
	struct brcms_c_rateset defrateset;

	u32 rspec_override;	/* 802.11 rate override */
	u32 mrspec_override;	/* multicast rate override */
	u8 band_stf_ss_mode;	/* Configured STF type, 0:siso; 1:cdd */
	s8 band_stf_stbc_tx;	/* STBC TX 0:off; 1:force on; -1:auto */
	/* rates supported by chip (phy-specific) */
	struct brcms_c_rateset hw_rateset;
	u8 basic_rate[BRCM_MAXRATE + 1]; /* basic rates indexed by rate */
	bool mimo_cap_40;	/* 40 MHz cap enabled on this band */
	s8 antgain;		/* antenna gain from srom */

	u16 CWmin; /* minimum size of contention window, in unit of aSlotTime */
	u16 CWmax; /* maximum size of contention window, in unit of aSlotTime */
	u16 bcntsfoff;	/* beacon tsf offset */
};

struct pkt_cb {
	/* function to call when tx frame completes */
	/* tx completion callback takes 3 args */
	void (*fn)(struct brcms_c_info *wlc, uint txstatus, void *arg);

	void *arg;		/* void arg for fn */
	u8 nextidx;		/* index of next call back if threading */
	bool entered;		/* recursion check */
};

/* module control blocks */
struct modulecb {
	/* module name : NULL indicates empty array member */
	char name[32];
	/* iovar table */
	const struct brcmu_iovar *iovars;
	/* handle passed when handler 'doiovar' is called */
	struct brcms_info *hdl;

	/* IOVar handler
	 *
	 * handle - a pointer value registered with the function
	 * vi - iovar_info that was looked up
	 * actionid - action ID, calculated by IOV_GVAL() and IOV_SVAL()
	 *    based on varid.
	 * name - the actual iovar name
	 * params/plen - parameters and length for a get, input only.
	 * arg/len - buffer and length for value to be set or retrieved,
	 *      input or output.
	 * vsize - value size, valid for integer type only.
	 * wlcif - interface context (brcms_c_if pointer)
	 *
	 * All pointers may point into the same buffer.
	 */
	int (*iovar_fn)(void *handle, const struct brcmu_iovar *vi,
			u32 actionid, const char *name, void *params,
			uint plen, void *arg, int alen, int vsize,
			struct brcms_c_if *wlcif);

	int (*down_fn)(void *handle); /* down handler. Note: the int returned
				       * by the down function is a count of the
				       * number of timers that could not be
				       * freed.
				       */

};

struct edcf_acparam {
	u8 ACI;
	u8 ECW;
	u16 TXOP;
} __packed;

struct wme_param_ie {
	u8 oui[3];
	u8 type;
	u8 subtype;
	u8 version;
	u8 qosinfo;
	u8 rsvd;
	struct edcf_acparam acparam[AC_COUNT];
} __packed;

/* virtual interface */
struct brcms_c_if {
	struct brcms_c_if *next;
	u8 type;	/* BSS or WDS */
	u8 index;	/* assigned in wl_add_if(), index of the wlif if any,
			 * not necessarily corresponding to bsscfg._idx or
			 * AID2PVBMAP(scb).
			 */
	u8 flags;		/* flags for the interface */
	struct brcms_if *wlif;		/* pointer to wlif */
	struct brcms_txq_info *qi;	/* pointer to associated tx queue */
	union {
		/* pointer to scb if WDS */
		struct scb *scb;
		/* pointer to bsscfg if BSS */
		struct brcms_bss_cfg *bsscfg;
	} u;
};

/* flags for the interface, this interface is linked to a brcms_if */
#define BRCMS_IF_LINKED		0x02

struct brcms_hw_band {
	int bandtype;		/* BRCM_BAND_2G, BRCM_BAND_5G */
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
	struct brcms_phy_pub *pi; /* pointer to phy specific information */
	bool abgphy_encore;
};

struct brcms_hardware {
	bool _piomode;		/* true if pio mode */
	struct brcms_c_info *wlc;

	/* fifo */
	struct dma_pub *di[NFIFO];	/* dma handles, per fifo */

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

	struct si_pub *sih;	/* SI handle (cookie for siutils calls) */
	char *vars;		/* "environment" name=value */
	uint vars_size;		/* size of vars, free vars on detach */
	struct d11regs *regs;	/* pointer to device registers */
	struct phy_shim_info *physhim; /* phy shim layer handler */
	struct shared_phy *phy_sh;	/* pointer to shared phy state */
	struct brcms_hw_band *band;/* pointer to active per-band state */
	/* band state per phy/radio */
	struct brcms_hw_band *bandstate[MAXBANDS];
	u16 bmac_phytxant;	/* cache of high phytxant state */
	bool shortslot;		/* currently using 11g ShortSlot timing */
	u16 SRL;		/* 802.11 dot11ShortRetryLimit */
	u16 LRL;		/* 802.11 dot11LongRetryLimit */
	u16 SFBL;		/* Short Frame Rate Fallback Limit */
	u16 LFBL;		/* Long Frame Rate Fallback Limit */

	bool up;		/* d11 hardware up and running */
	uint now;		/* # elapsed seconds */
	uint _nbands;		/* # bands supported */
	u16 chanspec;	/* bmac chanspec shadow */

	uint *txavail[NFIFO];	/* # tx descriptors available */
	u16 *xmtfifo_sz;	/* fifo size in 256B for each xmt fifo */

	u32 pllreq;		/* pll requests to keep PLL on */

	u8 suspended_fifos;	/* Which TX fifo to remain awake for */
	u32 maccontrol;	/* Cached value of maccontrol */
	uint mac_suspend_depth;	/* current depth of mac_suspend levels */
	u32 wake_override;	/* bit flags to force MAC to WAKE mode */
	u32 mute_override;	/* Prevent ucode from sending beacons */
	u8 etheraddr[ETH_ALEN];	/* currently configured ethernet address */
	u32 led_gpio_mask;	/* LED GPIO Mask */
	bool noreset;		/* true= do not reset hw, used by WLC_OUT */
	bool forcefastclk;	/* true if h/w is forcing to use fast clk */
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
struct brcms_txq_info {
	struct brcms_txq_info *next;
	struct pktq q;
	uint stopped;		/* tx flow control bits */
};

/*
 * Principal common driver data structure.
 *
 * pub: pointer to driver public state.
 * wl: pointer to specific private state.
 * regs: pointer to device registers.
 * hw: HW related state.
 * clkreq_override: setting for clkreq for PCIE : Auto, 0, 1.
 * fastpwrup_dly: time in us needed to bring up d11 fast clock.
 * macintstatus: bit channel between isr and dpc.
 * macintmask: sw runtime master macintmask value.
 * defmacintmask: default "on" macintmask value.
 * device_present: (removable) device is present.
 * clk: core is out of reset and has clock.
 * core: pointer to active io core.
 * band: pointer to active per-band state.
 * corestate: per-core state (one per hw core).
 * bandstate: per-band state (one per phy/radio).
 * war16165: PCI slow clock 16165 war flag.
 * tx_suspended: data fifos need to remain suspended.
 * txpend16165war: PCI slow clock 16165 war flag.
 * qvalid: DirFrmQValid and BcMcFrmQValid.
 * txpwr_local_max: regulatory local txpwr max.
 * txpwr_local_constraint: local power contraint in dB.
 * ampdu: ampdu module handler.
 * asi: antsel module handler.
 * cmi: channel manager module handler.
 * vars_size: size of vars, free vars on detach.
 * vendorid: PCI vendor id.
 * deviceid: PCI device id.
 * ucode_rev: microcode revision.
 * machwcap: MAC capabilities, BMAC shadow.
 * perm_etheraddr: original sprom local ethernet address.
 * bandlocked: disable auto multi-band switching.
 * bandinit_pending: track band init in auto band.
 * radio_monitor: radio timer is running.
 * going_down: down path intermediate variable.
 * mpc: enable minimum power consumption.
 * mpc_dlycnt: # of watchdog cnt before turn disable radio.
 * mpc_offcnt: # of watchdog cnt that radio is disabled.
 * mpc_delay_off: delay radio disable by # of watchdog cnt.
 * prev_non_delay_mpc: prev state brcms_c_is_non_delay_mpc.
 * wdtimer: timer for watchdog routine.
 * radio_timer: timer for hw radio button monitor routine.
 * monitor: monitor (MPDU sniffing) mode.
 * bcnmisc_ibss: bcns promisc mode override for IBSS.
 * bcnmisc_scan: bcns promisc mode override for scan.
 * bcnmisc_monitor: bcns promisc mode override for monitor.
 * _rifs: enable per-packet rifs.
 * sgi_tx: sgi tx.
 * bcn_li_bcn: beacon listen interval in # beacons.
 * bcn_li_dtim: beacon listen interval in # dtims.
 * WDarmed: watchdog timer is armed.
 * WDlast: last time wlc_watchdog() was called.
 * wme_dp: AC bitmap. Discard (oldest first) policy per AC.
 * edcf_txop[AC_COUNT]: current txop for each ac.
 * wme_param_ie: on STA contains parameters in use locally, and on AP
 *		 contains parameters advertised
 * wme_retries: per-AC retry limits.
 * tx_prec_map: Precedence map based on HW FIFO space.
 * fifo2prec_map[NFIFO]: pointer to fifo2_prec map based on WME.
 * bsscfg: set of BSS configurations, idx 0 is default and always valid.
 * cfg: the primary bsscfg (can be AP or STA).
 * tx_queues: common TX Queue list.
 * wsec_keys[WSEC_MAX_KEYS]: dynamic key storage.
 * wsec_def_keys[BRCMS_DEFAULT_KEYS]: default key storage.
 * wsec_swkeys: indicates that all keys should be treated as
 *		sw keys (used for debugging).
 * modulecb:
 * mimoft: SIGN or 11N.
 * cck_40txbw: 11N, cck tx b/w override when in 40MHZ mode.
 * ofdm_40txbw: 11N, ofdm tx b/w override when in 40MHZ mode.
 * mimo_40txbw: 11N, mimo tx b/w override when in 40MHZ mode.
 * ht_cap: HT CAP IE being advertised by this node.
 * default_bss: configured BSS parameters.
 * mc_fid_counter: BC/MC FIFO frame ID counter.
 * country_default: saved country for leaving 802.11d auto-country mode.
 * autocountry_default: initial country for 802.11d auto-country mode.
 * prb_resp_timeout: do not send prb resp if request older
 *		     than this, 0 = disable.
 * sup_rates_override: use only these rates in 11g supported rates if specified.
 * home_chanspec: shared home chanspec.
 * chanspec: target operational channel.
 * usr_fragthresh: user configured fragmentation threshold.
 * fragthresh[NFIFO]: per-fifo fragmentation thresholds.
 * RTSThresh: 802.11 dot11RTSThreshold.
 * SRL: 802.11 dot11ShortRetryLimit.
 * LRL: 802.11 dot11LongRetryLimit.
 * SFBL: Short Frame Rate Fallback Limit.
 * LFBL: Long Frame Rate Fallback Limit.
 * shortslot: currently using 11g ShortSlot timing.
 * shortslot_override: 11g ShortSlot override.
 * include_legacy_erp: include Legacy ERP info elt ID 47 as well as g ID 42.
 * PLCPHdr_override: 802.11b Preamble Type override.
 * stf:
 * bcn_rspec: save bcn ratespec purpose.
 * tempsense_lasttime;
 * tx_duty_cycle_ofdm: maximum allowed duty cycle for OFDM.
 * tx_duty_cycle_cck: maximum allowed duty cycle for CCK.
 * next_bsscfg_ID;
 * pkt_queue: txq for transmit packets.
 * mpc_dur: total time (ms) in mpc mode except for the portion since
 *	    radio is turned off last time.
 * mpc_laston_ts: timestamp (ms) when radio is turned off last time.
 * wiphy:
 */
struct brcms_c_info {
	struct brcms_pub *pub;
	struct brcms_info *wl;
	struct d11regs *regs;
	struct brcms_hardware *hw;

	/* clock */
	int clkreq_override;
	u16 fastpwrup_dly;

	/* interrupt */
	u32 macintstatus;
	u32 macintmask;
	u32 defmacintmask;

	/* up and down */
	bool device_present;

	bool clk;

	/* multiband */
	struct brcms_core *core;
	struct brcms_band *band;
	struct brcms_core *corestate;
	struct brcms_band *bandstate[MAXBANDS];

	bool war16165;

	bool tx_suspended;

	uint txpend16165war;

	/* packet queue */
	uint qvalid;

	/* Regulatory power limits */
	s8 txpwr_local_max;
	u8 txpwr_local_constraint;


	struct ampdu_info *ampdu;
	struct antsel_info *asi;
	struct brcms_cm_info *cmi;

	uint vars_size;

	u16 vendorid;
	u16 deviceid;
	uint ucode_rev;

	u32 machwcap;

	u8 perm_etheraddr[ETH_ALEN];

	bool bandlocked;
	bool bandinit_pending;

	bool radio_monitor;
	bool going_down;

	bool mpc;
	u8 mpc_dlycnt;
	u8 mpc_offcnt;
	u8 mpc_delay_off;
	u8 prev_non_delay_mpc;

	struct brcms_timer *wdtimer;
	struct brcms_timer *radio_timer;

	/* promiscuous */
	bool monitor;
	bool bcnmisc_ibss;
	bool bcnmisc_scan;
	bool bcnmisc_monitor;

	/* driver feature */
	bool _rifs;
	s8 sgi_tx;

	/* AP-STA synchronization, power save */
	u8 bcn_li_bcn;
	u8 bcn_li_dtim;

	bool WDarmed;
	u32 WDlast;

	/* WME */
	u8 wme_dp;
	u16 edcf_txop[AC_COUNT];

	struct wme_param_ie wme_param_ie;
	u16 wme_retries[AC_COUNT];
	u16 tx_prec_map;
	u16 fifo2prec_map[NFIFO];

	struct brcms_bss_cfg *bsscfg[BRCMS_MAXBSSCFG];
	struct brcms_bss_cfg *cfg;

	/* tx queue */
	struct brcms_txq_info *tx_queues;

	/* security */
	struct wsec_key *wsec_keys[WSEC_MAX_KEYS];
	struct wsec_key *wsec_def_keys[BRCMS_DEFAULT_KEYS];
	bool wsec_swkeys;
	struct modulecb *modulecb;

	u8 mimoft;
	s8 cck_40txbw;
	s8 ofdm_40txbw;
	s8 mimo_40txbw;
	struct ieee80211_ht_cap ht_cap;

	struct brcms_bss_info *default_bss;

	u16 mc_fid_counter;

	char country_default[BRCM_CNTRY_BUF_SZ];
	char autocountry_default[BRCM_CNTRY_BUF_SZ];
	u16 prb_resp_timeout;
	struct brcms_c_rateset sup_rates_override;

	u16 home_chanspec;

	/* PHY parameters */
	u16 chanspec;
	u16 usr_fragthresh;
	u16 fragthresh[NFIFO];
	u16 RTSThresh;
	u16 SRL;
	u16 LRL;
	u16 SFBL;
	u16 LFBL;

	/* network config */
	bool shortslot;
	s8 shortslot_override;
	bool include_legacy_erp;

	struct brcms_protection *protection;
	s8 PLCPHdr_override;

	struct brcms_stf *stf;

	u32 bcn_rspec;

	uint tempsense_lasttime;

	u16 tx_duty_cycle_ofdm;
	u16 tx_duty_cycle_cck;

	u16 next_bsscfg_ID;

	struct brcms_txq_info *pkt_queue;
	u32 mpc_dur;
	u32 mpc_laston_ts;
	struct wiphy *wiphy;
};

/* antsel module specific state */
struct antsel_info {
	struct brcms_c_info *wlc;	/* pointer to main wlc structure */
	struct brcms_pub *pub;		/* pointer to public fn */
	u8 antsel_type;	/* Type of boardlevel mimo antenna switch-logic
				 * 0 = N/A, 1 = 2x4 board, 2 = 2x3 CB2 board
				 */
	u8 antsel_antswitch;	/* board level antenna switch type */
	bool antsel_avail;	/* Ant selection availability (SROM based) */
	struct brcms_antselcfg antcfg_11n; /* antenna configuration */
	struct brcms_antselcfg antcfg_cur; /* current antenna config (auto) */
};

/*
 * BSS configuration state
 *
 * wlc: wlc to which this bsscfg belongs to.
 * up: is this configuration up operational
 * enable: is this configuration enabled
 * associated: is BSS in ASSOCIATED state
 * BSS: infraustructure or adhoc
 * dtim_programmed:
 * SSID_len: the length of SSID
 * SSID: SSID string
 * bcmc_scb: one bcmc_scb per band
 * _idx: the index of this bsscfg, assigned at wlc_bsscfg_alloc()
 *
 * MAC filter
 * ----------
 * nmac: # of entries on maclist array
 * macmode: allow/deny stations on maclist array
 * maclist: list of source MAC addrs to match

 * security
 * --------
 * wsec: wireless security bitvec
 * auth: 802.11 authentication: Open, Shared Key, WPA
 * openshared: try Open auth first, then Shared Key
 * wsec_restrict: drop unencrypted packets if wsec is enabled
 * eap_restrict: restrict data until 802.1X auth succeeds
 * WPA_auth: WPA authenticated key management
 * wpa2_preauth: default is true, wpa_cap sets value
 * wsec_portopen: indicates keys are plumbed
 * wpa_none_txiv: global txiv for WPA_NONE, tkip and aes
 * wsec_index: 0-3: default tx key, -1: not set
 * bss_def_keys: default key storage
 *
 * TKIP countermeasures
 * --------------------
 * tkip_countermeasures: flags TKIP no-assoc period
 * tk_cm_dt: detect timer
 * tk_cm_bt: blocking timer
 * tk_cm_bt_tmstmp: Timestamp when TKIP BT is activated
 * tk_cm_activate: activate countermeasures after EAPOL-Key sent
 *
 * BSSID: BSSID (associated)
 * cur_etheraddr: h/w address
 * bcmc_fid: the last BCMC FID queued to TX_BCMC_FIFO
 * bcmc_fid_shm: the last BCMC FID written to shared mem
 * flags: BSSCFG flags; see below
 * bcn: AP beacon
 * bcn_len: AP beacon length
 * ar_disassoc: disassociated in associated recreation
 * auth_atmptd: auth type (open/shared) attempted
 *
 * pmkid_cand: PMKID candidate list
 * npmkid_cand: num PMKID candidates
 * pmkid: PMKID cache
 * npmkid: num cached PMKIDs
 * current_bss: BSS parms in ASSOCIATED state
 *
 * PM states
 * ---------
 * PMawakebcn: bcn recvd during current waking state
 * PMpending: waiting for tx status with PM indicated set
 * priorPMstate: Detecting PM state transitions
 * PSpoll: flags there is an outstanding PS-Poll frame
 *
 * rcmta: BSSID entry in RCMTA, use the wsec key to manage the RCMTA entries.
 *
 * ID: 'unique' ID of this bsscfg, assigned at bsscfg allocation
 *
 * txrspecidx: index into tx rate circular buffer
 * txrspec: circular buffer of prev MPDUs tx rates
 */
struct brcms_bss_cfg {
	struct brcms_c_info *wlc;
	bool up;
	bool enable;
	bool associated;
	bool BSS;
	bool dtim_programmed;
	u8 SSID_len;
	u8 SSID[IEEE80211_MAX_SSID_LEN];
	struct scb *bcmc_scb[MAXBANDS];
	s8 _idx;
	uint nmac;
	int macmode;
	struct ether_addr *maclist;
	u32 wsec;
	s16 auth;
	s16 openshared;
	bool wsec_restrict;
	bool eap_restrict;
	u16 WPA_auth;
	bool wpa2_preauth;
	bool wsec_portopen;
	struct wsec_iv wpa_none_txiv;
	int wsec_index;
	struct wsec_key *bss_def_keys[BRCMS_DEFAULT_KEYS];
	bool tkip_countermeasures;
	u32 tk_cm_dt;
	u32 tk_cm_bt;
	u32 tk_cm_bt_tmstmp;
	bool tk_cm_activate;
	u8 BSSID[ETH_ALEN];
	u8 cur_etheraddr[ETH_ALEN];
	u16 bcmc_fid;
	u16 bcmc_fid_shm;
	u32 flags;
	u8 *bcn;
	uint bcn_len;
	bool ar_disassoc;
	int auth_atmptd;
	struct pmkid_cand pmkid_cand[MAXPMKID];
	uint npmkid_cand;
	struct pmkid pmkid[MAXPMKID];
	uint npmkid;
	struct brcms_bss_info *current_bss;
	bool PMawakebcn;
	bool PMpending;
	bool priorPMstate;
	bool PSpoll;
	struct wsec_key *rcmta;
	u16 ID;
	uint txrspecidx;
	u32 txrspec[NTXRATE][2];
};

#define	CHANNEL_BANDUNIT(wlc, ch) \
	(((ch) <= CH_MAX_2G_CHANNEL) ? BAND_2G_INDEX : BAND_5G_INDEX)
#define	OTHERBANDUNIT(wlc) \
	((uint)((wlc)->band->bandunit ? BAND_2G_INDEX : BAND_5G_INDEX))

#define IS_MBAND_UNLOCKED(wlc) \
	((NBANDS(wlc) > 1) && !(wlc)->bandlocked)

#define BRCMS_BAND_PI_RADIO_CHANSPEC wlc_phy_chanspec_get(wlc->band->pi)

/* sum the individual fifo tx pending packet counts */
#define	TXPKTPENDTOT(wlc) \
	((wlc)->core->txpktpend[0] + (wlc)->core->txpktpend[1] + \
	 (wlc)->core->txpktpend[2] + (wlc)->core->txpktpend[3])
#define TXPKTPENDGET(wlc, fifo) ((wlc)->core->txpktpend[(fifo)])
#define TXPKTPENDINC(wlc, fifo, val) ((wlc)->core->txpktpend[(fifo)] += (val))
#define TXPKTPENDDEC(wlc, fifo, val) ((wlc)->core->txpktpend[(fifo)] -= (val))
#define TXPKTPENDCLR(wlc, fifo)	((wlc)->core->txpktpend[(fifo)] = 0)
#define TXAVAIL(wlc, fifo) (*(wlc)->core->txavail[(fifo)])
#define GETNEXTTXP(wlc, _queue) \
		dma_getnexttxp((wlc)->hw->di[(_queue)], DMA_RANGE_TRANSMITTED)

#define BRCMS_IS_MATCH_SSID(wlc, ssid1, ssid2, len1, len2) \
	((len1 == len2) && !memcmp(ssid1, ssid2, len1))

extern void brcms_c_fatal_error(struct brcms_c_info *wlc);
extern void brcms_b_rpc_watchdog(struct brcms_c_info *wlc);
extern void brcms_c_recv(struct brcms_c_info *wlc, struct sk_buff *p);
extern bool brcms_c_dotxstatus(struct brcms_c_info *wlc, struct tx_status *txs,
			       u32 frm_tx2);
extern void brcms_c_txfifo(struct brcms_c_info *wlc, uint fifo,
			   struct sk_buff *p,
			   bool commit, s8 txpktpend);
extern void brcms_c_txfifo_complete(struct brcms_c_info *wlc, uint fifo,
				    s8 txpktpend);
extern void brcms_c_txq_enq(struct brcms_c_info *wlc, struct scb *scb,
			    struct sk_buff *sdu, uint prec);
extern void brcms_c_info_init(struct brcms_c_info *wlc, int unit);
extern void brcms_c_print_txstatus(struct tx_status *txs);
extern int brcms_c_xmtfifo_sz_get(struct brcms_c_info *wlc, uint fifo,
				  uint *blocks);
extern void brcms_c_write_template_ram(struct brcms_c_info *wlc, int offset,
				       int len, void *buf);
extern void brcms_c_pllreq(struct brcms_c_info *wlc, bool set, u32 req_bit);
extern void brcms_c_reset_bmac_done(struct brcms_c_info *wlc);

#if defined(BCMDBG)
extern void brcms_c_print_rxh(struct d11rxhdr *rxh);
extern void brcms_c_print_txdesc(struct d11txh *txh);
#else
#define brcms_c_print_txdesc(a)
#endif

extern void brcms_c_setxband(struct brcms_hardware *wlc_hw, uint bandunit);
extern void brcms_c_coredisable(struct brcms_hardware *wlc_hw);

extern bool brcms_c_valid_rate(struct brcms_c_info *wlc, u32 rate,
			       int band, bool verbose);
extern void brcms_c_ap_upd(struct brcms_c_info *wlc);

/* helper functions */
extern void brcms_c_shm_ssid_upd(struct brcms_c_info *wlc,
				 struct brcms_bss_cfg *cfg);
extern int brcms_c_set_gmode(struct brcms_c_info *wlc, u8 gmode, bool config);

extern void brcms_c_mac_bcn_promisc_change(struct brcms_c_info *wlc,
					   bool promisc);
extern void brcms_c_mac_bcn_promisc(struct brcms_c_info *wlc);
extern void brcms_c_mac_promisc(struct brcms_c_info *wlc);
extern void brcms_c_txflowcontrol(struct brcms_c_info *wlc,
				  struct brcms_txq_info *qi,
				  bool on, int prio);
extern void brcms_c_txflowcontrol_override(struct brcms_c_info *wlc,
				       struct brcms_txq_info *qi,
				       bool on, uint override);
extern bool brcms_c_txflowcontrol_prio_isset(struct brcms_c_info *wlc,
					     struct brcms_txq_info *qi,
					     int prio);
extern void brcms_c_send_q(struct brcms_c_info *wlc);
extern int brcms_c_prep_pdu(struct brcms_c_info *wlc, struct sk_buff *pdu,
			    uint *fifo);

extern u16 brcms_c_calc_lsig_len(struct brcms_c_info *wlc, u32 ratespec,
				uint mac_len);
extern u32 brcms_c_rspec_to_rts_rspec(struct brcms_c_info *wlc,
					     u32 rspec,
					     bool use_rspec, u16 mimo_ctlchbw);
extern u16 brcms_c_compute_rtscts_dur(struct brcms_c_info *wlc, bool cts_only,
				      u32 rts_rate,
				      u32 frame_rate,
				      u8 rts_preamble_type,
				      u8 frame_preamble_type, uint frame_len,
				      bool ba);

extern void brcms_c_tbtt(struct brcms_c_info *wlc);
extern void brcms_c_inval_dma_pkts(struct brcms_hardware *hw,
			       struct ieee80211_sta *sta,
			       void (*dma_callback_fn));

extern void brcms_c_reprate_init(struct brcms_c_info *wlc);
extern void brcms_c_bsscfg_reprate_init(struct brcms_bss_cfg *bsscfg);

/* Shared memory access */
extern void brcms_c_write_shm(struct brcms_c_info *wlc, uint offset, u16 v);
extern u16 brcms_c_read_shm(struct brcms_c_info *wlc, uint offset);
extern void brcms_c_copyto_shm(struct brcms_c_info *wlc, uint offset,
			       const void *buf, int len);

extern void brcms_c_update_beacon(struct brcms_c_info *wlc);
extern void brcms_c_bss_update_beacon(struct brcms_c_info *wlc,
				  struct brcms_bss_cfg *bsscfg);

extern void brcms_c_update_probe_resp(struct brcms_c_info *wlc, bool suspend);
extern void brcms_c_bss_update_probe_resp(struct brcms_c_info *wlc,
					  struct brcms_bss_cfg *cfg,
					  bool suspend);
extern bool brcms_c_ismpc(struct brcms_c_info *wlc);
extern bool brcms_c_is_non_delay_mpc(struct brcms_c_info *wlc);
extern void brcms_c_radio_mpc_upd(struct brcms_c_info *wlc);
extern bool brcms_c_prec_enq_head(struct brcms_c_info *wlc, struct pktq *q,
			      struct sk_buff *pkt, int prec, bool head);
extern u16 brcms_c_phytxctl1_calc(struct brcms_c_info *wlc, u32 rspec);
extern void brcms_c_compute_plcp(struct brcms_c_info *wlc, u32 rate,
				 uint length, u8 *plcp);
extern uint brcms_c_calc_frame_time(struct brcms_c_info *wlc,
				    u32 ratespec,
				    u8 preamble_type, uint mac_len);

extern void brcms_c_set_chanspec(struct brcms_c_info *wlc,
				 u16 chanspec);

extern bool brcms_c_timers_init(struct brcms_c_info *wlc, int unit);

extern int brcms_c_set_nmode(struct brcms_c_info *wlc, s32 nmode);
extern void brcms_c_mimops_action_ht_send(struct brcms_c_info *wlc,
				      struct brcms_bss_cfg *bsscfg,
				      u8 mimops_mode);

extern void brcms_c_switch_shortslot(struct brcms_c_info *wlc, bool shortslot);
extern void brcms_c_set_bssid(struct brcms_bss_cfg *cfg);
extern void brcms_c_edcf_setparams(struct brcms_c_info *wlc, bool suspend);

extern void brcms_c_set_ratetable(struct brcms_c_info *wlc);
extern int brcms_c_set_mac(struct brcms_bss_cfg *cfg);
extern void brcms_c_beacon_phytxctl_txant_upd(struct brcms_c_info *wlc,
					  u32 bcn_rate);
extern void brcms_c_mod_prb_rsp_rate_table(struct brcms_c_info *wlc,
					   uint frame_len);
extern u32 brcms_c_lowest_basic_rspec(struct brcms_c_info *wlc,
					     struct brcms_c_rateset *rs);
extern void brcms_c_radio_disable(struct brcms_c_info *wlc);
extern void brcms_c_bcn_li_upd(struct brcms_c_info *wlc);
extern void brcms_c_set_home_chanspec(struct brcms_c_info *wlc,
				      u16 chanspec);
extern bool brcms_c_ps_allowed(struct brcms_c_info *wlc);
extern bool brcms_c_stay_awake(struct brcms_c_info *wlc);
extern void brcms_c_wme_initparams_sta(struct brcms_c_info *wlc,
				       struct wme_param_ie *pe);

extern void brcms_b_antsel_type_set(struct brcms_hardware *wlc_hw,
				     u8 antsel_type);

/* chanspec, ucode interface */
extern void brcms_b_set_chanspec(struct brcms_hardware *wlc_hw,
				  u16 chanspec,
				  bool mute, struct txpwr_limits *txpwr);

extern void brcms_b_write_shm(struct brcms_hardware *wlc_hw, uint offset,
			      u16 v);
extern u16 brcms_b_read_shm(struct brcms_hardware *wlc_hw, uint offset);

extern void brcms_b_mhf(struct brcms_hardware *wlc_hw, u8 idx, u16 mask,
			u16 val, int bands);

extern void brcms_b_corereset(struct brcms_hardware *wlc_hw, u32 flags);

extern void brcms_b_mctrl(struct brcms_hardware *wlc_hw, u32 mask, u32 val);

extern void brcms_b_phy_reset(struct brcms_hardware *wlc_hw);
extern void brcms_b_bw_set(struct brcms_hardware *wlc_hw, u16 bw);
extern void brcms_b_core_phypll_reset(struct brcms_hardware *wlc_hw);
extern void brcms_c_ucode_wake_override_set(struct brcms_hardware *wlc_hw,
					u32 override_bit);
extern void brcms_c_ucode_wake_override_clear(struct brcms_hardware *wlc_hw,
					  u32 override_bit);
extern void brcms_b_write_template_ram(struct brcms_hardware *wlc_hw,
				       int offset, int len, void *buf);
extern u16 brcms_b_rate_shm_offset(struct brcms_hardware *wlc_hw, u8 rate);
extern void brcms_b_copyto_objmem(struct brcms_hardware *wlc_hw,
				   uint offset, const void *buf, int len,
				   u32 sel);
extern void brcms_b_copyfrom_objmem(struct brcms_hardware *wlc_hw, uint offset,
				     void *buf, int len, u32 sel);
extern void brcms_b_switch_macfreq(struct brcms_hardware *wlc_hw, u8 spurmode);
extern u16 brcms_b_get_txant(struct brcms_hardware *wlc_hw);
extern void brcms_b_phyclk_fgc(struct brcms_hardware *wlc_hw, bool clk);
extern void brcms_b_macphyclk_set(struct brcms_hardware *wlc_hw, bool clk);
extern void brcms_b_core_phypll_ctl(struct brcms_hardware *wlc_hw, bool on);
extern void brcms_b_txant_set(struct brcms_hardware *wlc_hw, u16 phytxant);
extern void brcms_b_band_stf_ss_set(struct brcms_hardware *wlc_hw,
				    u8 stf_mode);

#endif				/* _BRCM_MAIN_H_ */
