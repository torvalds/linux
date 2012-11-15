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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci_ids.h>
#include <linux/if_ether.h>
#include <net/cfg80211.h>
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
#include "mac80211_if.h"
#include "ucode_loader.h"
#include "main.h"
#include "soc.h"
#include "dma.h"
#include "debug.h"

/* watchdog timer, in unit of ms */
#define TIMER_INTERVAL_WATCHDOG		1000
/* radio monitor timer, in unit of ms */
#define TIMER_INTERVAL_RADIOCHK		800

/* beacon interval, in unit of 1024TU */
#define BEACON_INTERVAL_DEFAULT		100

/* n-mode support capability */
/* 2x2 includes both 1x1 & 2x2 devices
 * reserved #define 2 for future when we want to separate 1x1 & 2x2 and
 * control it independently
 */
#define WL_11N_2x2			1
#define WL_11N_3x3			3
#define WL_11N_4x4			4

#define EDCF_ACI_MASK			0x60
#define EDCF_ACI_SHIFT			5
#define EDCF_ECWMIN_MASK		0x0f
#define EDCF_ECWMAX_SHIFT		4
#define EDCF_AIFSN_MASK			0x0f
#define EDCF_AIFSN_MAX			15
#define EDCF_ECWMAX_MASK		0xf0

#define EDCF_AC_BE_TXOP_STA		0x0000
#define EDCF_AC_BK_TXOP_STA		0x0000
#define EDCF_AC_VO_ACI_STA		0x62
#define EDCF_AC_VO_ECW_STA		0x32
#define EDCF_AC_VI_ACI_STA		0x42
#define EDCF_AC_VI_ECW_STA		0x43
#define EDCF_AC_BK_ECW_STA		0xA4
#define EDCF_AC_VI_TXOP_STA		0x005e
#define EDCF_AC_VO_TXOP_STA		0x002f
#define EDCF_AC_BE_ACI_STA		0x03
#define EDCF_AC_BE_ECW_STA		0xA4
#define EDCF_AC_BK_ACI_STA		0x27
#define EDCF_AC_VO_TXOP_AP		0x002f

#define EDCF_TXOP2USEC(txop)		((txop) << 5)
#define EDCF_ECW2CW(exp)		((1 << (exp)) - 1)

#define APHY_SYMBOL_TIME		4
#define APHY_PREAMBLE_TIME		16
#define APHY_SIGNAL_TIME		4
#define APHY_SIFS_TIME			16
#define APHY_SERVICE_NBITS		16
#define APHY_TAIL_NBITS			6
#define BPHY_SIFS_TIME			10
#define BPHY_PLCP_SHORT_TIME		96

#define PREN_PREAMBLE			24
#define PREN_MM_EXT			12
#define PREN_PREAMBLE_EXT		4

#define DOT11_MAC_HDR_LEN		24
#define DOT11_ACK_LEN			10
#define DOT11_BA_LEN			4
#define DOT11_OFDM_SIGNAL_EXTENSION	6
#define DOT11_MIN_FRAG_LEN		256
#define DOT11_RTS_LEN			16
#define DOT11_CTS_LEN			10
#define DOT11_BA_BITMAP_LEN		128
#define DOT11_MIN_BEACON_PERIOD		1
#define DOT11_MAX_BEACON_PERIOD		0xFFFF
#define DOT11_MAXNUMFRAGS		16
#define DOT11_MAX_FRAG_LEN		2346

#define BPHY_PLCP_TIME			192
#define RIFS_11N_TIME			2

/* length of the BCN template area */
#define BCN_TMPL_LEN			512

/* brcms_bss_info flag bit values */
#define BRCMS_BSS_HT			0x0020	/* BSS is HT (MIMO) capable */

/* chip rx buffer offset */
#define BRCMS_HWRXOFF			38

/* rfdisable delay timer 500 ms, runs of ALP clock */
#define RFDISABLE_DEFAULT		10000000

#define BRCMS_TEMPSENSE_PERIOD		10	/* 10 second timeout */

/* precedences numbers for wlc queues. These are twice as may levels as
 * 802.1D priorities.
 * Odd numbers are used for HI priority traffic at same precedence levels
 * These constants are used ONLY by wlc_prio2prec_map.  Do not use them
 * elsewhere.
 */
#define _BRCMS_PREC_NONE		0	/* None = - */
#define _BRCMS_PREC_BK			2	/* BK - Background */
#define _BRCMS_PREC_BE			4	/* BE - Best-effort */
#define _BRCMS_PREC_EE			6	/* EE - Excellent-effort */
#define _BRCMS_PREC_CL			8	/* CL - Controlled Load */
#define _BRCMS_PREC_VI			10	/* Vi - Video */
#define _BRCMS_PREC_VO			12	/* Vo - Voice */
#define _BRCMS_PREC_NC			14	/* NC - Network Control */

/* synthpu_dly times in us */
#define SYNTHPU_DLY_APHY_US		3700
#define SYNTHPU_DLY_BPHY_US		1050
#define SYNTHPU_DLY_NPHY_US		2048
#define SYNTHPU_DLY_LPPHY_US		300

#define ANTCNT				10	/* vanilla M_MAX_ANTCNT val */

/* Per-AC retry limit register definitions; uses defs.h bitfield macros */
#define EDCF_SHORT_S			0
#define EDCF_SFB_S			4
#define EDCF_LONG_S			8
#define EDCF_LFB_S			12
#define EDCF_SHORT_M			BITFIELD_MASK(4)
#define EDCF_SFB_M			BITFIELD_MASK(4)
#define EDCF_LONG_M			BITFIELD_MASK(4)
#define EDCF_LFB_M			BITFIELD_MASK(4)

#define RETRY_SHORT_DEF			7	/* Default Short retry Limit */
#define RETRY_SHORT_MAX			255	/* Maximum Short retry Limit */
#define RETRY_LONG_DEF			4	/* Default Long retry count */
#define RETRY_SHORT_FB			3	/* Short count for fb rate */
#define RETRY_LONG_FB			2	/* Long count for fb rate */

#define APHY_CWMIN			15
#define PHY_CWMAX			1023

#define EDCF_AIFSN_MIN			1

#define FRAGNUM_MASK			0xF

#define APHY_SLOT_TIME			9
#define BPHY_SLOT_TIME			20

#define WL_SPURAVOID_OFF		0
#define WL_SPURAVOID_ON1		1
#define WL_SPURAVOID_ON2		2

/* invalid core flags, use the saved coreflags */
#define BRCMS_USE_COREFLAGS		0xffffffff

/* values for PLCPHdr_override */
#define BRCMS_PLCP_AUTO			-1
#define BRCMS_PLCP_SHORT		0
#define BRCMS_PLCP_LONG			1

/* values for g_protection_override and n_protection_override */
#define BRCMS_PROTECTION_AUTO		-1
#define BRCMS_PROTECTION_OFF		0
#define BRCMS_PROTECTION_ON		1
#define BRCMS_PROTECTION_MMHDR_ONLY	2
#define BRCMS_PROTECTION_CTS_ONLY	3

/* values for g_protection_control and n_protection_control */
#define BRCMS_PROTECTION_CTL_OFF	0
#define BRCMS_PROTECTION_CTL_LOCAL	1
#define BRCMS_PROTECTION_CTL_OVERLAP	2

/* values for n_protection */
#define BRCMS_N_PROTECTION_OFF		0
#define BRCMS_N_PROTECTION_OPTIONAL	1
#define BRCMS_N_PROTECTION_20IN40	2
#define BRCMS_N_PROTECTION_MIXEDMODE	3

/* values for band specific 40MHz capabilities */
#define BRCMS_N_BW_20ALL		0
#define BRCMS_N_BW_40ALL		1
#define BRCMS_N_BW_20IN2G_40IN5G	2

/* bitflags for SGI support (sgi_rx iovar) */
#define BRCMS_N_SGI_20			0x01
#define BRCMS_N_SGI_40			0x02

/* defines used by the nrate iovar */
/* MSC in use,indicates b0-6 holds an mcs */
#define NRATE_MCS_INUSE			0x00000080
/* rate/mcs value */
#define NRATE_RATE_MASK			0x0000007f
/* stf mode mask: siso, cdd, stbc, sdm */
#define NRATE_STF_MASK			0x0000ff00
/* stf mode shift */
#define NRATE_STF_SHIFT			8
/* bit indicate to override mcs only */
#define NRATE_OVERRIDE_MCS_ONLY		0x40000000
#define NRATE_SGI_MASK			0x00800000	/* sgi mode */
#define NRATE_SGI_SHIFT			23		/* sgi mode */
#define NRATE_LDPC_CODING		0x00400000	/* adv coding in use */
#define NRATE_LDPC_SHIFT		22		/* ldpc shift */

#define NRATE_STF_SISO			0		/* stf mode SISO */
#define NRATE_STF_CDD			1		/* stf mode CDD */
#define NRATE_STF_STBC			2		/* stf mode STBC */
#define NRATE_STF_SDM			3		/* stf mode SDM */

#define MAX_DMA_SEGS			4

/* # of entries in Tx FIFO */
#define NTXD				64
/* Max # of entries in Rx FIFO based on 4kb page size */
#define NRXD				256

/* Amount of headroom to leave in Tx FIFO */
#define TX_HEADROOM			4

/* try to keep this # rbufs posted to the chip */
#define NRXBUFPOST			32

/* max # frames to process in brcms_c_recv() */
#define RXBND				8
/* max # tx status to process in wlc_txstatus() */
#define TXSBND				8

/* brcmu_format_flags() bit description structure */
struct brcms_c_bit_desc {
	u32 bit;
	const char *name;
};

/*
 * The following table lists the buffer memory allocated to xmt fifos in HW.
 * the size is in units of 256bytes(one block), total size is HW dependent
 * ucode has default fifo partition, sw can overwrite if necessary
 *
 * This is documented in twiki under the topic UcodeTxFifo. Please ensure
 * the twiki is updated before making changes.
 */

/* Starting corerev for the fifo size table */
#define XMTFIFOTBL_STARTREV	17

struct d11init {
	__le16 addr;
	__le16 size;
	__le32 value;
};

struct edcf_acparam {
	u8 ACI;
	u8 ECW;
	u16 TXOP;
} __packed;

/* debug/trace */
uint brcm_msg_level;

/* TX FIFO number to WME/802.1E Access Category */
static const u8 wme_fifo2ac[] = {
	IEEE80211_AC_BK,
	IEEE80211_AC_BE,
	IEEE80211_AC_VI,
	IEEE80211_AC_VO,
	IEEE80211_AC_BE,
	IEEE80211_AC_BE
};

/* ieee80211 Access Category to TX FIFO number */
static const u8 wme_ac2fifo[] = {
	TX_AC_VO_FIFO,
	TX_AC_VI_FIFO,
	TX_AC_BE_FIFO,
	TX_AC_BK_FIFO
};

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

static const u16 xmtfifo_sz[][NFIFO] = {
	/* corerev 17: 5120, 49152, 49152, 5376, 4352, 1280 */
	{20, 192, 192, 21, 17, 5},
	/* corerev 18: */
	{0, 0, 0, 0, 0, 0},
	/* corerev 19: */
	{0, 0, 0, 0, 0, 0},
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
	/* corerev 25: */
	{0, 0, 0, 0, 0, 0},
	/* corerev 26: */
	{0, 0, 0, 0, 0, 0},
	/* corerev 27: */
	{0, 0, 0, 0, 0, 0},
	/* corerev 28: 2304, 14848, 5632, 3584, 3584, 1280 */
	{9, 58, 22, 14, 14, 5},
};

#ifdef DEBUG
static const char * const fifo_names[] = {
	"AC_BK", "AC_BE", "AC_VI", "AC_VO", "BCMC", "ATIM" };
#else
static const char fifo_names[6][0];
#endif

#ifdef DEBUG
/* pointer to most recently allocated wl/wlc */
static struct brcms_c_info *wlc_info_dbg = (struct brcms_c_info *) (NULL);
#endif

/* Mapping of ieee80211 AC numbers to tx fifos */
static const u8 ac_to_fifo_mapping[IEEE80211_NUM_ACS] = {
	[IEEE80211_AC_VO]	= TX_AC_VO_FIFO,
	[IEEE80211_AC_VI]	= TX_AC_VI_FIFO,
	[IEEE80211_AC_BE]	= TX_AC_BE_FIFO,
	[IEEE80211_AC_BK]	= TX_AC_BK_FIFO,
};

/* Mapping of tx fifos to ieee80211 AC numbers */
static const u8 fifo_to_ac_mapping[IEEE80211_NUM_ACS] = {
	[TX_AC_BK_FIFO]	= IEEE80211_AC_BK,
	[TX_AC_BE_FIFO]	= IEEE80211_AC_BE,
	[TX_AC_VI_FIFO]	= IEEE80211_AC_VI,
	[TX_AC_VO_FIFO]	= IEEE80211_AC_VO,
};

static u8 brcms_ac_to_fifo(u8 ac)
{
	if (ac >= ARRAY_SIZE(ac_to_fifo_mapping))
		return TX_AC_BE_FIFO;
	return ac_to_fifo_mapping[ac];
}

static u8 brcms_fifo_to_ac(u8 fifo)
{
	if (fifo >= ARRAY_SIZE(fifo_to_ac_mapping))
		return IEEE80211_AC_BE;
	return fifo_to_ac_mapping[fifo];
}

/* Find basic rate for a given rate */
static u8 brcms_basic_rate(struct brcms_c_info *wlc, u32 rspec)
{
	if (is_mcs_rate(rspec))
		return wlc->band->basic_rate[mcs_table[rspec & RSPEC_RATE_MASK]
		       .leg_ofdm];
	return wlc->band->basic_rate[rspec & RSPEC_RATE_MASK];
}

static u16 frametype(u32 rspec, u8 mimoframe)
{
	if (is_mcs_rate(rspec))
		return mimoframe;
	return is_cck_rate(rspec) ? FT_CCK : FT_OFDM;
}

/* currently the best mechanism for determining SIFS is the band in use */
static u16 get_sifs(struct brcms_band *band)
{
	return band->bandtype == BRCM_BAND_5G ? APHY_SIFS_TIME :
				 BPHY_SIFS_TIME;
}

/*
 * Detect Card removed.
 * Even checking an sbconfig register read will not false trigger when the core
 * is in reset it breaks CF address mechanism. Accessing gphy phyversion will
 * cause SB error if aphy is in reset on 4306B0-DB. Need a simple accessible
 * reg with fixed 0/1 pattern (some platforms return all 0).
 * If clocks are present, call the sb routine which will figure out if the
 * device is removed.
 */
static bool brcms_deviceremoved(struct brcms_c_info *wlc)
{
	u32 macctrl;

	if (!wlc->hw->clk)
		return ai_deviceremoved(wlc->hw->sih);
	macctrl = bcma_read32(wlc->hw->d11core,
			      D11REGOFFS(maccontrol));
	return (macctrl & (MCTL_PSM_JMP_0 | MCTL_IHR_EN)) != MCTL_IHR_EN;
}

/* sum the individual fifo tx pending packet counts */
static int brcms_txpktpendtot(struct brcms_c_info *wlc)
{
	int i;
	int pending = 0;

	for (i = 0; i < ARRAY_SIZE(wlc->hw->di); i++)
		if (wlc->hw->di[i])
			pending += dma_txpending(wlc->hw->di[i]);
	return pending;
}

static bool brcms_is_mband_unlocked(struct brcms_c_info *wlc)
{
	return wlc->pub->_nbands > 1 && !wlc->bandlocked;
}

static int brcms_chspec_bw(u16 chanspec)
{
	if (CHSPEC_IS40(chanspec))
		return BRCMS_40_MHZ;
	if (CHSPEC_IS20(chanspec))
		return BRCMS_20_MHZ;

	return BRCMS_10_MHZ;
}

static void brcms_c_bsscfg_mfree(struct brcms_bss_cfg *cfg)
{
	if (cfg == NULL)
		return;

	kfree(cfg->current_bss);
	kfree(cfg);
}

static void brcms_c_detach_mfree(struct brcms_c_info *wlc)
{
	if (wlc == NULL)
		return;

	brcms_c_bsscfg_mfree(wlc->bsscfg);
	kfree(wlc->pub);
	kfree(wlc->modulecb);
	kfree(wlc->default_bss);
	kfree(wlc->protection);
	kfree(wlc->stf);
	kfree(wlc->bandstate[0]);
	kfree(wlc->corestate->macstat_snapshot);
	kfree(wlc->corestate);
	kfree(wlc->hw->bandstate[0]);
	kfree(wlc->hw);

	/* free the wlc */
	kfree(wlc);
	wlc = NULL;
}

static struct brcms_bss_cfg *brcms_c_bsscfg_malloc(uint unit)
{
	struct brcms_bss_cfg *cfg;

	cfg = kzalloc(sizeof(struct brcms_bss_cfg), GFP_ATOMIC);
	if (cfg == NULL)
		goto fail;

	cfg->current_bss = kzalloc(sizeof(struct brcms_bss_info), GFP_ATOMIC);
	if (cfg->current_bss == NULL)
		goto fail;

	return cfg;

 fail:
	brcms_c_bsscfg_mfree(cfg);
	return NULL;
}

static struct brcms_c_info *
brcms_c_attach_malloc(uint unit, uint *err, uint devid)
{
	struct brcms_c_info *wlc;

	wlc = kzalloc(sizeof(struct brcms_c_info), GFP_ATOMIC);
	if (wlc == NULL) {
		*err = 1002;
		goto fail;
	}

	/* allocate struct brcms_c_pub state structure */
	wlc->pub = kzalloc(sizeof(struct brcms_pub), GFP_ATOMIC);
	if (wlc->pub == NULL) {
		*err = 1003;
		goto fail;
	}
	wlc->pub->wlc = wlc;

	/* allocate struct brcms_hardware state structure */

	wlc->hw = kzalloc(sizeof(struct brcms_hardware), GFP_ATOMIC);
	if (wlc->hw == NULL) {
		*err = 1005;
		goto fail;
	}
	wlc->hw->wlc = wlc;

	wlc->hw->bandstate[0] =
		kzalloc(sizeof(struct brcms_hw_band) * MAXBANDS, GFP_ATOMIC);
	if (wlc->hw->bandstate[0] == NULL) {
		*err = 1006;
		goto fail;
	} else {
		int i;

		for (i = 1; i < MAXBANDS; i++)
			wlc->hw->bandstate[i] = (struct brcms_hw_band *)
			    ((unsigned long)wlc->hw->bandstate[0] +
			     (sizeof(struct brcms_hw_band) * i));
	}

	wlc->modulecb =
		kzalloc(sizeof(struct modulecb) * BRCMS_MAXMODULES, GFP_ATOMIC);
	if (wlc->modulecb == NULL) {
		*err = 1009;
		goto fail;
	}

	wlc->default_bss = kzalloc(sizeof(struct brcms_bss_info), GFP_ATOMIC);
	if (wlc->default_bss == NULL) {
		*err = 1010;
		goto fail;
	}

	wlc->bsscfg = brcms_c_bsscfg_malloc(unit);
	if (wlc->bsscfg == NULL) {
		*err = 1011;
		goto fail;
	}

	wlc->protection = kzalloc(sizeof(struct brcms_protection),
				  GFP_ATOMIC);
	if (wlc->protection == NULL) {
		*err = 1016;
		goto fail;
	}

	wlc->stf = kzalloc(sizeof(struct brcms_stf), GFP_ATOMIC);
	if (wlc->stf == NULL) {
		*err = 1017;
		goto fail;
	}

	wlc->bandstate[0] =
		kzalloc(sizeof(struct brcms_band)*MAXBANDS, GFP_ATOMIC);
	if (wlc->bandstate[0] == NULL) {
		*err = 1025;
		goto fail;
	} else {
		int i;

		for (i = 1; i < MAXBANDS; i++)
			wlc->bandstate[i] = (struct brcms_band *)
				((unsigned long)wlc->bandstate[0]
				+ (sizeof(struct brcms_band)*i));
	}

	wlc->corestate = kzalloc(sizeof(struct brcms_core), GFP_ATOMIC);
	if (wlc->corestate == NULL) {
		*err = 1026;
		goto fail;
	}

	wlc->corestate->macstat_snapshot =
		kzalloc(sizeof(struct macstat), GFP_ATOMIC);
	if (wlc->corestate->macstat_snapshot == NULL) {
		*err = 1027;
		goto fail;
	}

	return wlc;

 fail:
	brcms_c_detach_mfree(wlc);
	return NULL;
}

/*
 * Update the slot timing for standard 11b/g (20us slots)
 * or shortslot 11g (9us slots)
 * The PSM needs to be suspended for this call.
 */
static void brcms_b_update_slot_timing(struct brcms_hardware *wlc_hw,
					bool shortslot)
{
	struct bcma_device *core = wlc_hw->d11core;

	if (shortslot) {
		/* 11g short slot: 11a timing */
		bcma_write16(core, D11REGOFFS(ifs_slot), 0x0207);
		brcms_b_write_shm(wlc_hw, M_DOT11_SLOT, APHY_SLOT_TIME);
	} else {
		/* 11g long slot: 11b timing */
		bcma_write16(core, D11REGOFFS(ifs_slot), 0x0212);
		brcms_b_write_shm(wlc_hw, M_DOT11_SLOT, BPHY_SLOT_TIME);
	}
}

/*
 * calculate frame duration of a given rate and length, return
 * time in usec unit
 */
static uint brcms_c_calc_frame_time(struct brcms_c_info *wlc, u32 ratespec,
				    u8 preamble_type, uint mac_len)
{
	uint nsyms, dur = 0, Ndps, kNdps;
	uint rate = rspec2rate(ratespec);

	if (rate == 0) {
		brcms_err(wlc->hw->d11core, "wl%d: WAR: using rate of 1 mbps\n",
			  wlc->pub->unit);
		rate = BRCM_RATE_1M;
	}

	BCMMSG(wlc->wiphy, "wl%d: rspec 0x%x, preamble_type %d, len%d\n",
		 wlc->pub->unit, ratespec, preamble_type, mac_len);

	if (is_mcs_rate(ratespec)) {
		uint mcs = ratespec & RSPEC_RATE_MASK;
		int tot_streams = mcs_2_txstreams(mcs) + rspec_stc(ratespec);

		dur = PREN_PREAMBLE + (tot_streams * PREN_PREAMBLE_EXT);
		if (preamble_type == BRCMS_MM_PREAMBLE)
			dur += PREN_MM_EXT;
		/* 1000Ndbps = kbps * 4 */
		kNdps = mcs_2_rate(mcs, rspec_is40mhz(ratespec),
				   rspec_issgi(ratespec)) * 4;

		if (rspec_stc(ratespec) == 0)
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
		if (wlc->band->bandtype == BRCM_BAND_2G)
			dur += DOT11_OFDM_SIGNAL_EXTENSION;
	} else if (is_ofdm_rate(rate)) {
		dur = APHY_PREAMBLE_TIME;
		dur += APHY_SIGNAL_TIME;
		/* Ndbps = Mbps * 4 = rate(500Kbps) * 2 */
		Ndps = rate * 2;
		/* NSyms = CEILING((SERVICE + 8*NBytes + TAIL) / Ndbps) */
		nsyms =
		    CEIL((APHY_SERVICE_NBITS + 8 * mac_len + APHY_TAIL_NBITS),
			 Ndps);
		dur += APHY_SYMBOL_TIME * nsyms;
		if (wlc->band->bandtype == BRCM_BAND_2G)
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

static void brcms_c_write_inits(struct brcms_hardware *wlc_hw,
				const struct d11init *inits)
{
	struct bcma_device *core = wlc_hw->d11core;
	int i;
	uint offset;
	u16 size;
	u32 value;

	brcms_dbg_info(wlc_hw->d11core, "wl%d\n", wlc_hw->unit);

	for (i = 0; inits[i].addr != cpu_to_le16(0xffff); i++) {
		size = le16_to_cpu(inits[i].size);
		offset = le16_to_cpu(inits[i].addr);
		value = le32_to_cpu(inits[i].value);
		if (size == 2)
			bcma_write16(core, offset, value);
		else if (size == 4)
			bcma_write32(core, offset, value);
		else
			break;
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

static void brcms_c_ucode_bsinit(struct brcms_hardware *wlc_hw)
{
	struct brcms_ucode *ucode = &wlc_hw->wlc->wl->ucode;

	/* init microcode host flags */
	brcms_c_write_mhf(wlc_hw, wlc_hw->band->mhfs);

	/* do band-specific ucode IHR, SHM, and SCR inits */
	if (D11REV_IS(wlc_hw->corerev, 23)) {
		if (BRCMS_ISNPHY(wlc_hw->band))
			brcms_c_write_inits(wlc_hw, ucode->d11n0bsinitvals16);
		else
			brcms_err(wlc_hw->d11core,
				  "%s: wl%d: unsupported phy in corerev %d\n",
				  __func__, wlc_hw->unit,
				  wlc_hw->corerev);
	} else {
		if (D11REV_IS(wlc_hw->corerev, 24)) {
			if (BRCMS_ISLCNPHY(wlc_hw->band))
				brcms_c_write_inits(wlc_hw,
						    ucode->d11lcn0bsinitvals24);
			else
				brcms_err(wlc_hw->d11core,
					  "%s: wl%d: unsupported phy in core rev %d\n",
					  __func__, wlc_hw->unit,
					  wlc_hw->corerev);
		} else {
			brcms_err(wlc_hw->d11core,
				  "%s: wl%d: unsupported corerev %d\n",
				  __func__, wlc_hw->unit, wlc_hw->corerev);
		}
	}
}

static void brcms_b_core_ioctl(struct brcms_hardware *wlc_hw, u32 m, u32 v)
{
	struct bcma_device *core = wlc_hw->d11core;
	u32 ioctl = bcma_aread32(core, BCMA_IOCTL) & ~m;

	bcma_awrite32(core, BCMA_IOCTL, ioctl | v);
}

static void brcms_b_core_phy_clk(struct brcms_hardware *wlc_hw, bool clk)
{
	brcms_dbg_info(wlc_hw->d11core, "wl%d: clk %d\n", wlc_hw->unit, clk);

	wlc_hw->phyclk = clk;

	if (OFF == clk) {	/* clear gmode bit, put phy into reset */

		brcms_b_core_ioctl(wlc_hw, (SICF_PRST | SICF_FGC | SICF_GMODE),
				   (SICF_PRST | SICF_FGC));
		udelay(1);
		brcms_b_core_ioctl(wlc_hw, (SICF_PRST | SICF_FGC), SICF_PRST);
		udelay(1);

	} else {		/* take phy out of reset */

		brcms_b_core_ioctl(wlc_hw, (SICF_PRST | SICF_FGC), SICF_FGC);
		udelay(1);
		brcms_b_core_ioctl(wlc_hw, SICF_FGC, 0);
		udelay(1);

	}
}

/* low-level band switch utility routine */
static void brcms_c_setxband(struct brcms_hardware *wlc_hw, uint bandunit)
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
	if (wlc_hw->sbclk && !wlc_hw->noreset) {
		u32 gmode = 0;

		if (bandunit == 0)
			gmode = SICF_GMODE;

		brcms_b_core_ioctl(wlc_hw, SICF_GMODE, gmode);
	}
}

/* switch to new band but leave it inactive */
static u32 brcms_c_setband_inact(struct brcms_c_info *wlc, uint bandunit)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	u32 macintmask;
	u32 macctrl;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc_hw->unit);
	macctrl = bcma_read32(wlc_hw->d11core,
			      D11REGOFFS(maccontrol));
	WARN_ON((macctrl & MCTL_EN_MAC) != 0);

	/* disable interrupts */
	macintmask = brcms_intrsoff(wlc->wl);

	/* radio off */
	wlc_phy_switch_radio(wlc_hw->band->pi, OFF);

	brcms_b_core_phy_clk(wlc_hw, OFF);

	brcms_c_setxband(wlc_hw, bandunit);

	return macintmask;
}

/* process an individual struct tx_status */
static bool
brcms_c_dotxstatus(struct brcms_c_info *wlc, struct tx_status *txs)
{
	struct sk_buff *p = NULL;
	uint queue = NFIFO;
	struct dma_pub *dma = NULL;
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
	bool fatal = true;

	/* discard intermediate indications for ucode with one legitimate case:
	 *   e.g. if "useRTS" is set. ucode did a successful rts/cts exchange,
	 *   but the subsequent tx of DATA failed. so it will start rts/cts
	 *   from the beginning (resetting the rts transmission count)
	 */
	if (!(txs->status & TX_STATUS_AMPDU)
	    && (txs->status & TX_STATUS_INTERMEDIATE)) {
		BCMMSG(wlc->wiphy, "INTERMEDIATE but not AMPDU\n");
		fatal = false;
		goto out;
	}

	queue = txs->frameid & TXFID_QUEUE_MASK;
	if (queue >= NFIFO)
		goto out;

	dma = wlc->hw->di[queue];

	p = dma_getnexttxp(wlc->hw->di[queue], DMA_RANGE_TRANSMITTED);
	if (p == NULL)
		goto out;

	txh = (struct d11txh *) (p->data);
	mcl = le16_to_cpu(txh->MacTxControlLow);

	if (txs->phyerr) {
		if (brcm_msg_level & BRCM_DL_INFO) {
			brcms_err(wlc->hw->d11core, "phyerr 0x%x, rate 0x%x\n",
				  txs->phyerr, txh->MainRates);
			brcms_c_print_txdesc(txh);
		}
		brcms_c_print_txstatus(txs);
	}

	if (txs->frameid != le16_to_cpu(txh->TxFrameID))
		goto out;
	tx_info = IEEE80211_SKB_CB(p);
	h = (struct ieee80211_hdr *)((u8 *) (txh + 1) + D11_PHY_HDR_LEN);

	if (tx_info->rate_driver_data[0])
		scb = &wlc->pri_scb;

	if (tx_info->flags & IEEE80211_TX_CTL_AMPDU) {
		brcms_c_ampdu_dotxstatus(wlc->ampdu, scb, p, txs);
		fatal = false;
		goto out;
	}

	supr_status = txs->status & TX_STATUS_SUPR_MASK;
	if (supr_status == TX_STATUS_SUPR_BADCH)
		BCMMSG(wlc->wiphy,
		       "%s: Pkt tx suppressed, possibly channel %d\n",
		       __func__, CHSPEC_CHANNEL(wlc->default_bss->chanspec));

	tx_rts = le16_to_cpu(txh->MacTxControlLow) & TXC_SENDRTS;
	tx_frame_count =
	    (txs->status & TX_STATUS_FRM_RTX_MASK) >> TX_STATUS_FRM_RTX_SHIFT;
	tx_rts_count =
	    (txs->status & TX_STATUS_RTS_RTX_MASK) >> TX_STATUS_RTS_RTX_SHIFT;

	lastframe = !ieee80211_has_morefrags(h->frame_control);

	if (!lastframe) {
		brcms_err(wlc->hw->d11core, "Not last frame!\n");
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

		if (queue < IEEE80211_NUM_ACS) {
			sfbl = GFIELD(wlc->wme_retries[wme_fifo2ac[queue]],
				      EDCF_SFB);
			lfbl = GFIELD(wlc->wme_retries[wme_fifo2ac[queue]],
				      EDCF_LFB);
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

	totlen = p->len;
	free_pdu = true;

	if (lastframe) {
		/* remove PLCP & Broadcom tx descriptor header */
		skb_pull(p, D11_PHY_HDR_LEN);
		skb_pull(p, D11_TXH_LEN);
		ieee80211_tx_status_irqsafe(wlc->pub->ieee_hw, p);
	} else {
		brcms_err(wlc->hw->d11core,
			  "%s: Not last frame => not calling tx_status\n",
			  __func__);
	}

	fatal = false;

 out:
	if (fatal && p)
		brcmu_pkt_buf_free_skb(p);

	if (dma && queue < NFIFO) {
		u16 ac_queue = brcms_fifo_to_ac(queue);
		if (dma->txavail > TX_HEADROOM && queue < TX_BCMC_FIFO &&
		    ieee80211_queue_stopped(wlc->pub->ieee_hw, ac_queue))
			ieee80211_wake_queue(wlc->pub->ieee_hw, ac_queue);
		dma_kick_tx(dma);
	}

	return fatal;
}

/* process tx completion events in BMAC
 * Return true if more tx status need to be processed. false otherwise.
 */
static bool
brcms_b_txstatus(struct brcms_hardware *wlc_hw, bool bound, bool *fatal)
{
	bool morepending = false;
	struct brcms_c_info *wlc = wlc_hw->wlc;
	struct bcma_device *core;
	struct tx_status txstatus, *txs;
	u32 s1, s2;
	uint n = 0;
	/*
	 * Param 'max_tx_num' indicates max. # tx status to process before
	 * break out.
	 */
	uint max_tx_num = bound ? TXSBND : -1;

	BCMMSG(wlc->wiphy, "wl%d\n", wlc_hw->unit);

	txs = &txstatus;
	core = wlc_hw->d11core;
	*fatal = false;
	s1 = bcma_read32(core, D11REGOFFS(frmtxstatus));
	while (!(*fatal)
	       && (s1 & TXS_V)) {

		if (s1 == 0xffffffff) {
			brcms_err(core, "wl%d: %s: dead chip\n", wlc_hw->unit,
				  __func__);
			return morepending;
		}
		s2 = bcma_read32(core, D11REGOFFS(frmtxstatus2));

		txs->status = s1 & TXS_STATUS_MASK;
		txs->frameid = (s1 & TXS_FID_MASK) >> TXS_FID_SHIFT;
		txs->sequence = s2 & TXS_SEQ_MASK;
		txs->phyerr = (s2 & TXS_PTX_MASK) >> TXS_PTX_SHIFT;
		txs->lasttxtime = 0;

		*fatal = brcms_c_dotxstatus(wlc_hw->wlc, txs);

		/* !give others some time to run! */
		if (++n >= max_tx_num)
			break;
		s1 = bcma_read32(core, D11REGOFFS(frmtxstatus));
	}

	if (*fatal)
		return 0;

	if (n >= max_tx_num)
		morepending = true;

	return morepending;
}

static void brcms_c_tbtt(struct brcms_c_info *wlc)
{
	if (!wlc->bsscfg->BSS)
		/*
		 * DirFrmQ is now valid...defer setting until end
		 * of ATIM window
		 */
		wlc->qvalid |= MCMD_DIRFRMQVAL;
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

static uint
dmareg(uint direction, uint fifonum)
{
	if (direction == DMA_TX)
		return offsetof(struct d11regs, fifo64regs[fifonum].dmaxmt);
	return offsetof(struct d11regs, fifo64regs[fifonum].dmarcv);
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

	/* name and offsets for dma_attach */
	snprintf(name, sizeof(name), "wl%d", unit);

	if (wlc_hw->di[0] == NULL) {	/* Init FIFOs */
		int dma_attach_err = 0;

		/*
		 * FIFO 0
		 * TX: TX_AC_BK_FIFO (TX AC Background data packets)
		 * RX: RX_FIFO (RX data packets)
		 */
		wlc_hw->di[0] = dma_attach(name, wlc,
					   (wme ? dmareg(DMA_TX, 0) : 0),
					   dmareg(DMA_RX, 0),
					   (wme ? NTXD : 0), NRXD,
					   RXBUFSZ, -1, NRXBUFPOST,
					   BRCMS_HWRXOFF, &brcm_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[0]);

		/*
		 * FIFO 1
		 * TX: TX_AC_BE_FIFO (TX AC Best-Effort data packets)
		 *   (legacy) TX_DATA_FIFO (TX data packets)
		 * RX: UNUSED
		 */
		wlc_hw->di[1] = dma_attach(name, wlc,
					   dmareg(DMA_TX, 1), 0,
					   NTXD, 0, 0, -1, 0, 0,
					   &brcm_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[1]);

		/*
		 * FIFO 2
		 * TX: TX_AC_VI_FIFO (TX AC Video data packets)
		 * RX: UNUSED
		 */
		wlc_hw->di[2] = dma_attach(name, wlc,
					   dmareg(DMA_TX, 2), 0,
					   NTXD, 0, 0, -1, 0, 0,
					   &brcm_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[2]);
		/*
		 * FIFO 3
		 * TX: TX_AC_VO_FIFO (TX AC Voice data packets)
		 *   (legacy) TX_CTL_FIFO (TX control & mgmt packets)
		 */
		wlc_hw->di[3] = dma_attach(name, wlc,
					   dmareg(DMA_TX, 3),
					   0, NTXD, 0, 0, -1,
					   0, 0, &brcm_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[3]);
/* Cleaner to leave this as if with AP defined */

		if (dma_attach_err) {
			brcms_err(wlc_hw->d11core,
				  "wl%d: wlc_attach: dma_attach failed\n",
				  unit);
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
	wlc_hw->chanspec = ch20mhz_chspec(1);
}

static void brcms_b_wait_for_wake(struct brcms_hardware *wlc_hw)
{
	/* delay before first read of ucode state */
	udelay(40);

	/* wait until ucode is no longer asleep */
	SPINWAIT((brcms_b_read_shm(wlc_hw, M_UCODE_DBGST) ==
		  DBGST_ASLEEP), wlc_hw->wlc->fastpwrup_dly);
}

/* control chip clock to save power, enable dynamic clock or force fast clock */
static void brcms_b_clkctl_clk(struct brcms_hardware *wlc_hw, enum bcma_clkmode mode)
{
	if (ai_get_cccaps(wlc_hw->sih) & CC_CAP_PMU) {
		/* new chips with PMU, CCS_FORCEHT will distribute the HT clock
		 * on backplane, but mac core will still run on ALP(not HT) when
		 * it enters powersave mode, which means the FCA bit may not be
		 * set. Should wakeup mac if driver wants it to run on HT.
		 */

		if (wlc_hw->clk) {
			if (mode == BCMA_CLKMODE_FAST) {
				bcma_set32(wlc_hw->d11core,
					   D11REGOFFS(clk_ctl_st),
					   CCS_FORCEHT);

				udelay(64);

				SPINWAIT(
				    ((bcma_read32(wlc_hw->d11core,
				      D11REGOFFS(clk_ctl_st)) &
				      CCS_HTAVAIL) == 0),
				      PMU_MAX_TRANSITION_DLY);
				WARN_ON(!(bcma_read32(wlc_hw->d11core,
					D11REGOFFS(clk_ctl_st)) &
					CCS_HTAVAIL));
			} else {
				if ((ai_get_pmurev(wlc_hw->sih) == 0) &&
				    (bcma_read32(wlc_hw->d11core,
					D11REGOFFS(clk_ctl_st)) &
					(CCS_FORCEHT | CCS_HTAREQ)))
					SPINWAIT(
					    ((bcma_read32(wlc_hw->d11core,
					      offsetof(struct d11regs,
						       clk_ctl_st)) &
					      CCS_HTAVAIL) == 0),
					      PMU_MAX_TRANSITION_DLY);
				bcma_mask32(wlc_hw->d11core,
					D11REGOFFS(clk_ctl_st),
					~CCS_FORCEHT);
			}
		}
		wlc_hw->forcefastclk = (mode == BCMA_CLKMODE_FAST);
	} else {

		/* old chips w/o PMU, force HT through cc,
		 * then use FCA to verify mac is running fast clock
		 */

		wlc_hw->forcefastclk = ai_clkctl_cc(wlc_hw->sih, mode);

		/* check fast clock is available (if core is not in reset) */
		if (wlc_hw->forcefastclk && wlc_hw->clk)
			WARN_ON(!(bcma_aread32(wlc_hw->d11core, BCMA_IOST) &
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

	bcma_write32(wlc_hw->d11core, D11REGOFFS(maccontrol),
		     maccontrol);
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
}

void brcms_c_ucode_wake_override_clear(struct brcms_hardware *wlc_hw,
				   u32 override_bit)
{
	mboolclr(wlc_hw->wake_override, override_bit);

	if (wlc_hw->wake_override || (wlc_hw->maccontrol & MCTL_WAKE))
		return;

	brcms_c_mctrl_write(wlc_hw);
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
static void
brcms_b_set_addrmatch(struct brcms_hardware *wlc_hw, int match_reg_offset,
		       const u8 *addr)
{
	struct bcma_device *core = wlc_hw->d11core;
	u16 mac_l;
	u16 mac_m;
	u16 mac_h;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d: brcms_b_set_addrmatch\n",
		 wlc_hw->unit);

	mac_l = addr[0] | (addr[1] << 8);
	mac_m = addr[2] | (addr[3] << 8);
	mac_h = addr[4] | (addr[5] << 8);

	/* enter the MAC addr into the RXE match registers */
	bcma_write16(core, D11REGOFFS(rcm_ctl),
		     RCM_INC_DATA | match_reg_offset);
	bcma_write16(core, D11REGOFFS(rcm_mat_data), mac_l);
	bcma_write16(core, D11REGOFFS(rcm_mat_data), mac_m);
	bcma_write16(core, D11REGOFFS(rcm_mat_data), mac_h);
}

void
brcms_b_write_template_ram(struct brcms_hardware *wlc_hw, int offset, int len,
			    void *buf)
{
	struct bcma_device *core = wlc_hw->d11core;
	u32 word;
	__le32 word_le;
	__be32 word_be;
	bool be_bit;
	brcms_dbg_info(core, "wl%d\n", wlc_hw->unit);

	bcma_write32(core, D11REGOFFS(tplatewrptr), offset);

	/* if MCTL_BIGEND bit set in mac control register,
	 * the chip swaps data in fifo, as well as data in
	 * template ram
	 */
	be_bit = (bcma_read32(core, D11REGOFFS(maccontrol)) & MCTL_BIGEND) != 0;

	while (len > 0) {
		memcpy(&word, buf, sizeof(u32));

		if (be_bit) {
			word_be = cpu_to_be32(word);
			word = *(u32 *)&word_be;
		} else {
			word_le = cpu_to_le32(word);
			word = *(u32 *)&word_le;
		}

		bcma_write32(core, D11REGOFFS(tplatewrdata), word);

		buf = (u8 *) buf + sizeof(u32);
		len -= sizeof(u32);
	}
}

static void brcms_b_set_cwmin(struct brcms_hardware *wlc_hw, u16 newmin)
{
	wlc_hw->band->CWmin = newmin;

	bcma_write32(wlc_hw->d11core, D11REGOFFS(objaddr),
		     OBJADDR_SCR_SEL | S_DOT11_CWMIN);
	(void)bcma_read32(wlc_hw->d11core, D11REGOFFS(objaddr));
	bcma_write32(wlc_hw->d11core, D11REGOFFS(objdata), newmin);
}

static void brcms_b_set_cwmax(struct brcms_hardware *wlc_hw, u16 newmax)
{
	wlc_hw->band->CWmax = newmax;

	bcma_write32(wlc_hw->d11core, D11REGOFFS(objaddr),
		     OBJADDR_SCR_SEL | S_DOT11_CWMAX);
	(void)bcma_read32(wlc_hw->d11core, D11REGOFFS(objaddr));
	bcma_write32(wlc_hw->d11core, D11REGOFFS(objdata), newmax);
}

void brcms_b_bw_set(struct brcms_hardware *wlc_hw, u16 bw)
{
	bool fastclk;

	/* request FAST clock if not on */
	fastclk = wlc_hw->forcefastclk;
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_FAST);

	wlc_phy_bw_state_set(wlc_hw->band->pi, bw);

	brcms_b_phy_reset(wlc_hw);
	wlc_phy_init(wlc_hw->band->pi, wlc_phy_chanspec_get(wlc_hw->band->pi));

	/* restore the clk */
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_DYNAMIC);
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
				   wlc_hw->band->bandtype == BRCM_BAND_5G ?
				   true : wlc_hw->shortslot);

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

/* Perform a soft reset of the PHY PLL */
void brcms_b_core_phypll_reset(struct brcms_hardware *wlc_hw)
{
	ai_cc_reg(wlc_hw->sih, offsetof(struct chipcregs, chipcontrol_addr),
		  ~0, 0);
	udelay(1);
	ai_cc_reg(wlc_hw->sih, offsetof(struct chipcregs, chipcontrol_data),
		  0x4, 0);
	udelay(1);
	ai_cc_reg(wlc_hw->sih, offsetof(struct chipcregs, chipcontrol_data),
		  0x4, 4);
	udelay(1);
	ai_cc_reg(wlc_hw->sih, offsetof(struct chipcregs, chipcontrol_data),
		  0x4, 0);
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
		brcms_b_core_ioctl(wlc_hw, SICF_FGC, SICF_FGC);
	else
		brcms_b_core_ioctl(wlc_hw, SICF_FGC, 0);

}

void brcms_b_macphyclk_set(struct brcms_hardware *wlc_hw, bool clk)
{
	if (ON == clk)
		brcms_b_core_ioctl(wlc_hw, SICF_MPCLKE, SICF_MPCLKE);
	else
		brcms_b_core_ioctl(wlc_hw, SICF_MPCLKE, 0);
}

void brcms_b_phy_reset(struct brcms_hardware *wlc_hw)
{
	struct brcms_phy_pub *pih = wlc_hw->band->pi;
	u32 phy_bw_clkbits;
	bool phy_in_reset = false;

	brcms_dbg_info(wlc_hw->d11core, "wl%d: reset phy\n", wlc_hw->unit);

	if (pih == NULL)
		return;

	phy_bw_clkbits = wlc_phy_clk_bwbits(wlc_hw->band->pi);

	/* Specific reset sequence required for NPHY rev 3 and 4 */
	if (BRCMS_ISNPHY(wlc_hw->band) && NREV_GE(wlc_hw->band->phyrev, 3) &&
	    NREV_LE(wlc_hw->band->phyrev, 4)) {
		/* Set the PHY bandwidth */
		brcms_b_core_ioctl(wlc_hw, SICF_BWMASK, phy_bw_clkbits);

		udelay(1);

		/* Perform a soft reset of the PHY PLL */
		brcms_b_core_phypll_reset(wlc_hw);

		/* reset the PHY */
		brcms_b_core_ioctl(wlc_hw, (SICF_PRST | SICF_PCLKE),
				   (SICF_PRST | SICF_PCLKE));
		phy_in_reset = true;
	} else {
		brcms_b_core_ioctl(wlc_hw,
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
	if (!bcma_core_is_enabled(wlc_hw->d11core)) {
		bcma_core_enable(wlc_hw->d11core, 0);
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
	WARN_ON((bcma_read32(wlc_hw->d11core, D11REGOFFS(maccontrol)) &
		 MCTL_EN_MAC) != 0);
}

static bool brcms_c_isgoodchip(struct brcms_hardware *wlc_hw)
{

	/* reject unsupported corerev */
	if (!CONF_HAS(D11CONF, wlc_hw->corerev)) {
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
	if (ai_get_boardvendor(wlc_hw->sih) != PCI_VENDOR_ID_BROADCOM)
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

static void brcms_c_get_macaddr(struct brcms_hardware *wlc_hw, u8 etheraddr[ETH_ALEN])
{
	struct ssb_sprom *sprom = &wlc_hw->d11core->bus->sprom;

	/* If macaddr exists, use it (Sromrev4, CIS, ...). */
	if (!is_zero_ether_addr(sprom->il0mac)) {
		memcpy(etheraddr, sprom->il0mac, 6);
		return;
	}

	if (wlc_hw->_nbands > 1)
		memcpy(etheraddr, sprom->et1mac, 6);
	else
		memcpy(etheraddr, sprom->il0mac, 6);
}

/* power both the pll and external oscillator on/off */
static void brcms_b_xtal(struct brcms_hardware *wlc_hw, bool want)
{
	brcms_dbg_info(wlc_hw->d11core, "wl%d: want %d\n", wlc_hw->unit, want);

	/*
	 * dont power down if plldown is false or
	 * we must poll hw radio disable
	 */
	if (!want && wlc_hw->pllreq)
		return;

	wlc_hw->sbclk = want;
	if (!wlc_hw->sbclk) {
		wlc_hw->clk = false;
		if (wlc_hw->band && wlc_hw->band->pi)
			wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, false);
	}
}

/*
 * Return true if radio is disabled, otherwise false.
 * hw radio disable signal is an external pin, users activate it asynchronously
 * this function could be called when driver is down and w/o clock
 * it operates on different registers depending on corerev and boardflag.
 */
static bool brcms_b_radio_read_hwdisabled(struct brcms_hardware *wlc_hw)
{
	bool v, clk, xtal;
	u32 flags = 0;

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
		if (D11REV_GE(wlc_hw->corerev, 18))
			flags |= SICF_PCLKE;

		/*
		 * TODO: test suspend/resume
		 *
		 * AI chip doesn't restore bar0win2 on
		 * hibernation/resume, need sw fixup
		 */

		bcma_core_enable(wlc_hw->d11core, flags);
		brcms_c_mctrl_reset(wlc_hw);
	}

	v = ((bcma_read32(wlc_hw->d11core,
			  D11REGOFFS(phydebug)) & PDBG_RFD) != 0);

	/* put core back into reset */
	if (!clk)
		bcma_core_disable(wlc_hw->d11core, 0);

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
	uint i;
	bool fastclk;

	if (flags == BRCMS_USE_COREFLAGS)
		flags = (wlc_hw->band->pi ? wlc_hw->band->core_flags : 0);

	brcms_dbg_info(wlc_hw->d11core, "wl%d: core reset\n", wlc_hw->unit);

	/* request FAST clock if not on  */
	fastclk = wlc_hw->forcefastclk;
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_FAST);

	/* reset the dma engines except first time thru */
	if (bcma_core_is_enabled(wlc_hw->d11core)) {
		for (i = 0; i < NFIFO; i++)
			if ((wlc_hw->di[i]) && (!dma_txreset(wlc_hw->di[i])))
				brcms_err(wlc_hw->d11core, "wl%d: %s: "
					  "dma_txreset[%d]: cannot stop dma\n",
					   wlc_hw->unit, __func__, i);

		if ((wlc_hw->di[RX_FIFO])
		    && (!wlc_dma_rxreset(wlc_hw, RX_FIFO)))
			brcms_err(wlc_hw->d11core, "wl%d: %s: dma_rxreset"
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
	if (D11REV_GE(wlc_hw->corerev, 18))
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
	bcma_core_enable(wlc_hw->d11core, flags);
	wlc_hw->clk = true;
	if (wlc_hw->band && wlc_hw->band->pi)
		wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, true);

	brcms_c_mctrl_reset(wlc_hw);

	if (ai_get_cccaps(wlc_hw->sih) & CC_CAP_PMU)
		brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_FAST);

	brcms_b_phy_reset(wlc_hw);

	/* turn on PHY_PLL */
	brcms_b_core_phypll_ctl(wlc_hw, true);

	/* clear sw intstatus */
	wlc_hw->wlc->macintstatus = 0;

	/* restore the clk setting */
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_DYNAMIC);
}

/* txfifo sizes needs to be modified(increased) since the newer cores
 * have more memory.
 */
static void brcms_b_corerev_fifofixup(struct brcms_hardware *wlc_hw)
{
	struct bcma_device *core = wlc_hw->d11core;
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

		bcma_write16(core, D11REGOFFS(xmtfifocmd), txfifo_cmd);
		bcma_write16(core, D11REGOFFS(xmtfifodef), txfifo_def);
		bcma_write16(core, D11REGOFFS(xmtfifodef1), txfifo_def1);

		bcma_write16(core, D11REGOFFS(xmtfifocmd), txfifo_cmd);

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
	struct bcma_device *core = wlc_hw->d11core;

	if ((ai_get_chip_id(wlc_hw->sih) == BCMA_CHIP_ID_BCM43224) ||
	    (ai_get_chip_id(wlc_hw->sih) == BCMA_CHIP_ID_BCM43225)) {
		if (spurmode == WL_SPURAVOID_ON2) {	/* 126Mhz */
			bcma_write16(core, D11REGOFFS(tsf_clk_frac_l), 0x2082);
			bcma_write16(core, D11REGOFFS(tsf_clk_frac_h), 0x8);
		} else if (spurmode == WL_SPURAVOID_ON1) {	/* 123Mhz */
			bcma_write16(core, D11REGOFFS(tsf_clk_frac_l), 0x5341);
			bcma_write16(core, D11REGOFFS(tsf_clk_frac_h), 0x8);
		} else {	/* 120Mhz */
			bcma_write16(core, D11REGOFFS(tsf_clk_frac_l), 0x8889);
			bcma_write16(core, D11REGOFFS(tsf_clk_frac_h), 0x8);
		}
	} else if (BRCMS_ISLCNPHY(wlc_hw->band)) {
		if (spurmode == WL_SPURAVOID_ON1) {	/* 82Mhz */
			bcma_write16(core, D11REGOFFS(tsf_clk_frac_l), 0x7CE0);
			bcma_write16(core, D11REGOFFS(tsf_clk_frac_h), 0xC);
		} else {	/* 80Mhz */
			bcma_write16(core, D11REGOFFS(tsf_clk_frac_l), 0xCCCD);
			bcma_write16(core, D11REGOFFS(tsf_clk_frac_h), 0xC);
		}
	}
}

/* Initialize GPIOs that are controlled by D11 core */
static void brcms_c_gpio_init(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	u32 gc, gm;

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
		bcma_set16(wlc_hw->d11core, D11REGOFFS(psm_gpio_oe),
			   (BOARD_GPIO_12 | BOARD_GPIO_13));
		bcma_set16(wlc_hw->d11core, D11REGOFFS(psm_gpio_out),
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
	bcma_chipco_gpio_control(&wlc_hw->d11core->bus->drv_cc, gm, gc);
}

static void brcms_ucode_write(struct brcms_hardware *wlc_hw,
			      const __le32 ucode[], const size_t nbytes)
{
	struct bcma_device *core = wlc_hw->d11core;
	uint i;
	uint count;

	brcms_dbg_info(wlc_hw->d11core, "wl%d\n", wlc_hw->unit);

	count = (nbytes / sizeof(u32));

	bcma_write32(core, D11REGOFFS(objaddr),
		     OBJADDR_AUTO_INC | OBJADDR_UCM_SEL);
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	for (i = 0; i < count; i++)
		bcma_write32(core, D11REGOFFS(objdata), le32_to_cpu(ucode[i]));

}

static void brcms_ucode_download(struct brcms_hardware *wlc_hw)
{
	struct brcms_c_info *wlc;
	struct brcms_ucode *ucode = &wlc_hw->wlc->wl->ucode;

	wlc = wlc_hw->wlc;

	if (wlc_hw->ucode_loaded)
		return;

	if (D11REV_IS(wlc_hw->corerev, 23)) {
		if (BRCMS_ISNPHY(wlc_hw->band)) {
			brcms_ucode_write(wlc_hw, ucode->bcm43xx_16_mimo,
					  ucode->bcm43xx_16_mimosz);
			wlc_hw->ucode_loaded = true;
		} else
			brcms_err(wlc_hw->d11core,
				  "%s: wl%d: unsupported phy in corerev %d\n",
				  __func__, wlc_hw->unit, wlc_hw->corerev);
	} else if (D11REV_IS(wlc_hw->corerev, 24)) {
		if (BRCMS_ISLCNPHY(wlc_hw->band)) {
			brcms_ucode_write(wlc_hw, ucode->bcm43xx_24_lcn,
					  ucode->bcm43xx_24_lcnsz);
			wlc_hw->ucode_loaded = true;
		} else {
			brcms_err(wlc_hw->d11core,
				  "%s: wl%d: unsupported phy in corerev %d\n",
				  __func__, wlc_hw->unit, wlc_hw->corerev);
		}
	}
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

static void brcms_b_fifoerrors(struct brcms_hardware *wlc_hw)
{
	bool fatal = false;
	uint unit;
	uint intstatus, idx;
	struct bcma_device *core = wlc_hw->d11core;

	unit = wlc_hw->unit;

	for (idx = 0; idx < NFIFO; idx++) {
		/* read intstatus register and ignore any non-error bits */
		intstatus =
			bcma_read32(core,
				    D11REGOFFS(intctrlregs[idx].intstatus)) &
			I_ERRORS;
		if (!intstatus)
			continue;

		BCMMSG(wlc_hw->wlc->wiphy, "wl%d: intstatus%d 0x%x\n",
			unit, idx, intstatus);

		if (intstatus & I_RO) {
			brcms_err(core, "wl%d: fifo %d: receive fifo "
				  "overflow\n", unit, idx);
			fatal = true;
		}

		if (intstatus & I_PC) {
			brcms_err(core, "wl%d: fifo %d: descriptor error\n",
				  unit, idx);
			fatal = true;
		}

		if (intstatus & I_PD) {
			brcms_err(core, "wl%d: fifo %d: data error\n", unit,
				  idx);
			fatal = true;
		}

		if (intstatus & I_DE) {
			brcms_err(core, "wl%d: fifo %d: descriptor protocol "
				  "error\n", unit, idx);
			fatal = true;
		}

		if (intstatus & I_RU)
			brcms_err(core, "wl%d: fifo %d: receive descriptor "
				  "underflow\n", idx, unit);

		if (intstatus & I_XU) {
			brcms_err(core, "wl%d: fifo %d: transmit fifo "
				  "underflow\n", idx, unit);
			fatal = true;
		}

		if (fatal) {
			brcms_fatal_error(wlc_hw->wlc->wl); /* big hammer */
			break;
		} else
			bcma_write32(core,
				     D11REGOFFS(intctrlregs[idx].intstatus),
				     intstatus);
	}
}

void brcms_c_intrson(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	wlc->macintmask = wlc->defmacintmask;
	bcma_write32(wlc_hw->d11core, D11REGOFFS(macintmask), wlc->macintmask);
}

u32 brcms_c_intrsoff(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	u32 macintmask;

	if (!wlc_hw->clk)
		return 0;

	macintmask = wlc->macintmask;	/* isr can still happen */

	bcma_write32(wlc_hw->d11core, D11REGOFFS(macintmask), 0);
	(void)bcma_read32(wlc_hw->d11core, D11REGOFFS(macintmask));
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
	bcma_write32(wlc_hw->d11core, D11REGOFFS(macintmask), wlc->macintmask);
}

/* assumes that the d11 MAC is enabled */
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

/* precondition: requires the mac core to be enabled */
static void brcms_b_mute(struct brcms_hardware *wlc_hw, bool mute_tx)
{
	static const u8 null_ether_addr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

	if (mute_tx) {
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
		brcms_b_tx_fifo_resume(wlc_hw, TX_DATA_FIFO);
		brcms_b_tx_fifo_resume(wlc_hw, TX_CTL_FIFO);
		brcms_b_tx_fifo_resume(wlc_hw, TX_AC_BK_FIFO);
		brcms_b_tx_fifo_resume(wlc_hw, TX_AC_VI_FIFO);

		/* Restore address */
		brcms_b_set_addrmatch(wlc_hw, RCM_MAC_OFFSET,
				       wlc_hw->etheraddr);
	}

	wlc_phy_mute_upd(wlc_hw->band->pi, mute_tx, 0);

	if (mute_tx)
		brcms_c_ucode_mute_override_set(wlc_hw);
	else
		brcms_c_ucode_mute_override_clear(wlc_hw);
}

void
brcms_c_mute(struct brcms_c_info *wlc, bool mute_tx)
{
	brcms_b_mute(wlc->hw, mute_tx);
}

/*
 * Read and clear macintmask and macintstatus and intstatus registers.
 * This routine should be called with interrupts off
 * Return:
 *   -1 if brcms_deviceremoved(wlc) evaluates to true;
 *   0 if the interrupt is not for us, or we are in some special cases;
 *   device interrupt status bits otherwise.
 */
static inline u32 wlc_intstatus(struct brcms_c_info *wlc, bool in_isr)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	struct bcma_device *core = wlc_hw->d11core;
	u32 macintstatus;

	/* macintstatus includes a DMA interrupt summary bit */
	macintstatus = bcma_read32(core, D11REGOFFS(macintstatus));

	BCMMSG(wlc->wiphy, "wl%d: macintstatus: 0x%x\n", wlc_hw->unit,
		 macintstatus);

	/* detect cardbus removed, in power down(suspend) and in reset */
	if (brcms_deviceremoved(wlc))
		return -1;

	/* brcms_deviceremoved() succeeds even when the core is still resetting,
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
	bcma_write32(core, D11REGOFFS(macintmask), 0);
	(void)bcma_read32(core, D11REGOFFS(macintmask));
	wlc->macintmask = 0;

	/* clear device interrupts */
	bcma_write32(core, D11REGOFFS(macintstatus), macintstatus);

	/* MI_DMAINT is indication of non-zero intstatus */
	if (macintstatus & MI_DMAINT)
		/*
		 * only fifo interrupt enabled is I_RI in
		 * RX_FIFO. If MI_DMAINT is set, assume it
		 * is set and clear the interrupt.
		 */
		bcma_write32(core, D11REGOFFS(intctrlregs[RX_FIFO].intstatus),
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
		brcms_err(wlc_hw->d11core,
			  "DEVICEREMOVED detected in the ISR code path\n");

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
	struct bcma_device *core = wlc_hw->d11core;
	u32 mc, mi;

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

	mc = bcma_read32(core, D11REGOFFS(maccontrol));

	if (mc == 0xffffffff) {
		brcms_err(core, "wl%d: %s: dead chip\n", wlc_hw->unit,
			  __func__);
		brcms_down(wlc->wl);
		return;
	}
	WARN_ON(mc & MCTL_PSM_JMP_0);
	WARN_ON(!(mc & MCTL_PSM_RUN));
	WARN_ON(!(mc & MCTL_EN_MAC));

	mi = bcma_read32(core, D11REGOFFS(macintstatus));
	if (mi == 0xffffffff) {
		brcms_err(core, "wl%d: %s: dead chip\n", wlc_hw->unit,
			  __func__);
		brcms_down(wlc->wl);
		return;
	}
	WARN_ON(mi & MI_MACSSPNDD);

	brcms_b_mctrl(wlc_hw, MCTL_EN_MAC, 0);

	SPINWAIT(!(bcma_read32(core, D11REGOFFS(macintstatus)) & MI_MACSSPNDD),
		 BRCMS_MAX_MAC_SUSPEND);

	if (!(bcma_read32(core, D11REGOFFS(macintstatus)) & MI_MACSSPNDD)) {
		brcms_err(core, "wl%d: wlc_suspend_mac_and_wait: waited %d uS"
			  " and MI_MACSSPNDD is still not on.\n",
			  wlc_hw->unit, BRCMS_MAX_MAC_SUSPEND);
		brcms_err(core, "wl%d: psmdebug 0x%08x, phydebug 0x%08x, "
			  "psm_brc 0x%04x\n", wlc_hw->unit,
			  bcma_read32(core, D11REGOFFS(psmdebug)),
			  bcma_read32(core, D11REGOFFS(phydebug)),
			  bcma_read16(core, D11REGOFFS(psm_brc)));
	}

	mc = bcma_read32(core, D11REGOFFS(maccontrol));
	if (mc == 0xffffffff) {
		brcms_err(core, "wl%d: %s: dead chip\n", wlc_hw->unit,
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
	struct bcma_device *core = wlc_hw->d11core;
	u32 mc, mi;

	BCMMSG(wlc->wiphy, "wl%d: bandunit %d\n", wlc_hw->unit,
		wlc->band->bandunit);

	/*
	 * Track overlapping suspend requests
	 */
	wlc_hw->mac_suspend_depth--;
	if (wlc_hw->mac_suspend_depth > 0)
		return;

	mc = bcma_read32(core, D11REGOFFS(maccontrol));
	WARN_ON(mc & MCTL_PSM_JMP_0);
	WARN_ON(mc & MCTL_EN_MAC);
	WARN_ON(!(mc & MCTL_PSM_RUN));

	brcms_b_mctrl(wlc_hw, MCTL_EN_MAC, MCTL_EN_MAC);
	bcma_write32(core, D11REGOFFS(macintstatus), MI_MACSSPNDD);

	mc = bcma_read32(core, D11REGOFFS(maccontrol));
	WARN_ON(mc & MCTL_PSM_JMP_0);
	WARN_ON(!(mc & MCTL_EN_MAC));
	WARN_ON(!(mc & MCTL_PSM_RUN));

	mi = bcma_read32(core, D11REGOFFS(macintstatus));
	WARN_ON(mi & MI_MACSSPNDD);

	brcms_c_ucode_wake_override_clear(wlc_hw,
					  BRCMS_WAKE_OVERRIDE_MACSUSPEND);
}

void brcms_b_band_stf_ss_set(struct brcms_hardware *wlc_hw, u8 stf_mode)
{
	wlc_hw->hw_stf_ss_opmode = stf_mode;

	if (wlc_hw->clk)
		brcms_upd_ofdm_pctl1_table(wlc_hw);
}

static bool brcms_b_validate_chip_access(struct brcms_hardware *wlc_hw)
{
	struct bcma_device *core = wlc_hw->d11core;
	u32 w, val;
	struct wiphy *wiphy = wlc_hw->wlc->wiphy;

	/* Validate dchip register access */

	bcma_write32(core, D11REGOFFS(objaddr), OBJADDR_SHM_SEL | 0);
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	w = bcma_read32(core, D11REGOFFS(objdata));

	/* Can we write and read back a 32bit register? */
	bcma_write32(core, D11REGOFFS(objaddr), OBJADDR_SHM_SEL | 0);
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	bcma_write32(core, D11REGOFFS(objdata), (u32) 0xaa5555aa);

	bcma_write32(core, D11REGOFFS(objaddr), OBJADDR_SHM_SEL | 0);
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	val = bcma_read32(core, D11REGOFFS(objdata));
	if (val != (u32) 0xaa5555aa) {
		wiphy_err(wiphy, "wl%d: validate_chip_access: SHM = 0x%x, "
			  "expected 0xaa5555aa\n", wlc_hw->unit, val);
		return false;
	}

	bcma_write32(core, D11REGOFFS(objaddr), OBJADDR_SHM_SEL | 0);
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	bcma_write32(core, D11REGOFFS(objdata), (u32) 0x55aaaa55);

	bcma_write32(core, D11REGOFFS(objaddr), OBJADDR_SHM_SEL | 0);
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	val = bcma_read32(core, D11REGOFFS(objdata));
	if (val != (u32) 0x55aaaa55) {
		wiphy_err(wiphy, "wl%d: validate_chip_access: SHM = 0x%x, "
			  "expected 0x55aaaa55\n", wlc_hw->unit, val);
		return false;
	}

	bcma_write32(core, D11REGOFFS(objaddr), OBJADDR_SHM_SEL | 0);
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	bcma_write32(core, D11REGOFFS(objdata), w);

	/* clear CFPStart */
	bcma_write32(core, D11REGOFFS(tsf_cfpstart), 0);

	w = bcma_read32(core, D11REGOFFS(maccontrol));
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
	struct bcma_device *core = wlc_hw->d11core;
	u32 tmp;

	brcms_dbg_info(core, "wl%d\n", wlc_hw->unit);

	tmp = 0;

	if (on) {
		if ((ai_get_chip_id(wlc_hw->sih) == BCMA_CHIP_ID_BCM4313)) {
			bcma_set32(core, D11REGOFFS(clk_ctl_st),
				   CCS_ERSRC_REQ_HT |
				   CCS_ERSRC_REQ_D11PLL |
				   CCS_ERSRC_REQ_PHYPLL);
			SPINWAIT((bcma_read32(core, D11REGOFFS(clk_ctl_st)) &
				  CCS_ERSRC_AVAIL_HT) != CCS_ERSRC_AVAIL_HT,
				 PHYPLL_WAIT_US);

			tmp = bcma_read32(core, D11REGOFFS(clk_ctl_st));
			if ((tmp & CCS_ERSRC_AVAIL_HT) != CCS_ERSRC_AVAIL_HT)
				brcms_err(core, "%s: turn on PHY PLL failed\n",
					  __func__);
		} else {
			bcma_set32(core, D11REGOFFS(clk_ctl_st),
				   tmp | CCS_ERSRC_REQ_D11PLL |
				   CCS_ERSRC_REQ_PHYPLL);
			SPINWAIT((bcma_read32(core, D11REGOFFS(clk_ctl_st)) &
				  (CCS_ERSRC_AVAIL_D11PLL |
				   CCS_ERSRC_AVAIL_PHYPLL)) !=
				 (CCS_ERSRC_AVAIL_D11PLL |
				  CCS_ERSRC_AVAIL_PHYPLL), PHYPLL_WAIT_US);

			tmp = bcma_read32(core, D11REGOFFS(clk_ctl_st));
			if ((tmp &
			     (CCS_ERSRC_AVAIL_D11PLL | CCS_ERSRC_AVAIL_PHYPLL))
			    !=
			    (CCS_ERSRC_AVAIL_D11PLL | CCS_ERSRC_AVAIL_PHYPLL))
				brcms_err(core, "%s: turn on PHY PLL failed\n",
					  __func__);
		}
	} else {
		/*
		 * Since the PLL may be shared, other cores can still
		 * be requesting it; so we'll deassert the request but
		 * not wait for status to comply.
		 */
		bcma_mask32(core, D11REGOFFS(clk_ctl_st),
			    ~CCS_ERSRC_REQ_PHYPLL);
		(void)bcma_read32(core, D11REGOFFS(clk_ctl_st));
	}
}

static void brcms_c_coredisable(struct brcms_hardware *wlc_hw)
{
	bool dev_gone;

	brcms_dbg_info(wlc_hw->d11core, "wl%d: disable core\n", wlc_hw->unit);

	dev_gone = brcms_deviceremoved(wlc_hw->wlc);

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

	wlc_hw->clk = false;
	bcma_core_disable(wlc_hw->d11core, 0);
	wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, false);
}

static void brcms_c_flushqueues(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;
	uint i;

	/* free any posted tx packets */
	for (i = 0; i < NFIFO; i++) {
		if (wlc_hw->di[i]) {
			dma_txreclaim(wlc_hw->di[i], DMA_RANGE_ALL);
			if (i < TX_BCMC_FIFO)
				ieee80211_wake_queue(wlc->pub->ieee_hw,
						     brcms_fifo_to_ac(i));
		}
	}

	/* free any posted rx packets */
	dma_rxreclaim(wlc_hw->di[RX_FIFO]);
}

static u16
brcms_b_read_objmem(struct brcms_hardware *wlc_hw, uint offset, u32 sel)
{
	struct bcma_device *core = wlc_hw->d11core;
	u16 objoff = D11REGOFFS(objdata);

	bcma_write32(core, D11REGOFFS(objaddr), sel | (offset >> 2));
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	if (offset & 2)
		objoff += 2;

	return bcma_read16(core, objoff);
}

static void
brcms_b_write_objmem(struct brcms_hardware *wlc_hw, uint offset, u16 v,
		     u32 sel)
{
	struct bcma_device *core = wlc_hw->d11core;
	u16 objoff = D11REGOFFS(objdata);

	bcma_write32(core, D11REGOFFS(objaddr), sel | (offset >> 2));
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	if (offset & 2)
		objoff += 2;

	bcma_write16(core, objoff, v);
}

/*
 * Read a single u16 from shared memory.
 * SHM 'offset' needs to be an even address
 */
u16 brcms_b_read_shm(struct brcms_hardware *wlc_hw, uint offset)
{
	return brcms_b_read_objmem(wlc_hw, offset, OBJADDR_SHM_SEL);
}

/*
 * Write a single u16 to shared memory.
 * SHM 'offset' needs to be an even address
 */
void brcms_b_write_shm(struct brcms_hardware *wlc_hw, uint offset, u16 v)
{
	brcms_b_write_objmem(wlc_hw, offset, v, OBJADDR_SHM_SEL);
}

/*
 * Copy a buffer to shared memory of specified type .
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

/*
 * Copy a piece of shared memory of specified type to a buffer .
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

/* Copy a buffer to shared memory.
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 */
static void brcms_c_copyto_shm(struct brcms_c_info *wlc, uint offset,
			const void *buf, int len)
{
	brcms_b_copyto_objmem(wlc->hw, offset, buf, len, OBJADDR_SHM_SEL);
}

static void brcms_b_retrylimit_upd(struct brcms_hardware *wlc_hw,
				   u16 SRL, u16 LRL)
{
	wlc_hw->SRL = SRL;
	wlc_hw->LRL = LRL;

	/* write retry limit to SCR, shouldn't need to suspend */
	if (wlc_hw->up) {
		bcma_write32(wlc_hw->d11core, D11REGOFFS(objaddr),
			     OBJADDR_SCR_SEL | S_DOT11_SRC_LMT);
		(void)bcma_read32(wlc_hw->d11core, D11REGOFFS(objaddr));
		bcma_write32(wlc_hw->d11core, D11REGOFFS(objdata), wlc_hw->SRL);
		bcma_write32(wlc_hw->d11core, D11REGOFFS(objaddr),
			     OBJADDR_SCR_SEL | S_DOT11_LRC_LMT);
		(void)bcma_read32(wlc_hw->d11core, D11REGOFFS(objaddr));
		bcma_write32(wlc_hw->d11core, D11REGOFFS(objdata), wlc_hw->LRL);
	}
}

static void brcms_b_pllreq(struct brcms_hardware *wlc_hw, bool set, u32 req_bit)
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
}

static void brcms_b_antsel_set(struct brcms_hardware *wlc_hw, u32 antsel_avail)
{
	wlc_hw->antsel_avail = antsel_avail;
}

/*
 * conditions under which the PM bit should be set in outgoing frames
 * and STAY_AWAKE is meaningful
 */
static bool brcms_c_ps_allowed(struct brcms_c_info *wlc)
{
	struct brcms_bss_cfg *cfg = wlc->bsscfg;

	/* disallow PS when one of the following global conditions meets */
	if (!wlc->pub->associated)
		return false;

	/* disallow PS when one of these meets when not scanning */
	if (wlc->filter_flags & FIF_PROMISC_IN_BSS)
		return false;

	if (cfg->associated) {
		/*
		 * disallow PS when one of the following
		 * bsscfg specific conditions meets
		 */
		if (!cfg->BSS)
			return false;

		return false;
	}

	return true;
}

static void brcms_c_statsupd(struct brcms_c_info *wlc)
{
	int i;
	struct macstat macstats;
#ifdef DEBUG
	u16 delta;
	u16 rxf0ovfl;
	u16 txfunfl[NFIFO];
#endif				/* DEBUG */

	/* if driver down, make no sense to update stats */
	if (!wlc->pub->up)
		return;

#ifdef DEBUG
	/* save last rx fifo 0 overflow count */
	rxf0ovfl = wlc->core->macstat_snapshot->rxf0ovfl;

	/* save last tx fifo  underflow count */
	for (i = 0; i < NFIFO; i++)
		txfunfl[i] = wlc->core->macstat_snapshot->txfunfl[i];
#endif				/* DEBUG */

	/* Read mac stats from contiguous shared memory */
	brcms_b_copyfrom_objmem(wlc->hw, M_UCODE_MACSTAT, &macstats,
				sizeof(struct macstat), OBJADDR_SHM_SEL);

#ifdef DEBUG
	/* check for rx fifo 0 overflow */
	delta = (u16) (wlc->core->macstat_snapshot->rxf0ovfl - rxf0ovfl);
	if (delta)
		brcms_err(wlc->hw->d11core, "wl%d: %u rx fifo 0 overflows!\n",
			  wlc->pub->unit, delta);

	/* check for tx fifo underflows */
	for (i = 0; i < NFIFO; i++) {
		delta =
		    (u16) (wlc->core->macstat_snapshot->txfunfl[i] -
			      txfunfl[i]);
		if (delta)
			brcms_err(wlc->hw->d11core,
				  "wl%d: %u tx fifo %d underflows!\n",
				  wlc->pub->unit, delta, i);
	}
#endif				/* DEBUG */

	/* merge counters from dma module */
	for (i = 0; i < NFIFO; i++) {
		if (wlc->hw->di[i])
			dma_counterreset(wlc->hw->di[i]);
	}
}

static void brcms_b_reset(struct brcms_hardware *wlc_hw)
{
	/* reset the core */
	if (!brcms_deviceremoved(wlc_hw->wlc))
		brcms_b_corereset(wlc_hw, BRCMS_USE_COREFLAGS);

	/* purge the dma rings */
	brcms_c_flushqueues(wlc_hw->wlc);
}

void brcms_c_reset(struct brcms_c_info *wlc)
{
	brcms_dbg_info(wlc->hw->d11core, "wl%d\n", wlc->pub->unit);

	/* slurp up hw mac counters before core reset */
	brcms_c_statsupd(wlc);

	/* reset our snapshot of macstat counters */
	memset((char *)wlc->core->macstat_snapshot, 0,
		sizeof(struct macstat));

	brcms_b_reset(wlc->hw);
}

void brcms_c_init_scb(struct scb *scb)
{
	int i;

	memset(scb, 0, sizeof(struct scb));
	scb->flags = SCB_WMECAP | SCB_HTCAP;
	for (i = 0; i < NUMPRIO; i++) {
		scb->seqnum[i] = 0;
		scb->seqctl[i] = 0xFFFF;
	}

	scb->seqctl_nonqos = 0xFFFF;
	scb->magic = SCB_MAGIC;
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
	struct bcma_device *core = wlc_hw->d11core;
	u32 sflags;
	u32 bcnint_us;
	uint i = 0;
	bool fifosz_fixup = false;
	int err = 0;
	u16 buf[NFIFO];
	struct brcms_ucode *ucode = &wlc_hw->wlc->wl->ucode;

	brcms_dbg_info(core, "wl%d: core init\n", wlc_hw->unit);

	/* reset PSM */
	brcms_b_mctrl(wlc_hw, ~0, (MCTL_IHR_EN | MCTL_PSM_JMP_0 | MCTL_WAKE));

	brcms_ucode_download(wlc_hw);
	/*
	 * FIFOSZ fixup. driver wants to controls the fifo allocation.
	 */
	fifosz_fixup = true;

	/* let the PSM run to the suspended state, set mode to BSS STA */
	bcma_write32(core, D11REGOFFS(macintstatus), -1);
	brcms_b_mctrl(wlc_hw, ~0,
		       (MCTL_IHR_EN | MCTL_INFRA | MCTL_PSM_RUN | MCTL_WAKE));

	/* wait for ucode to self-suspend after auto-init */
	SPINWAIT(((bcma_read32(core, D11REGOFFS(macintstatus)) &
		   MI_MACSSPNDD) == 0), 1000 * 1000);
	if ((bcma_read32(core, D11REGOFFS(macintstatus)) & MI_MACSSPNDD) == 0)
		brcms_err(core, "wl%d: wlc_coreinit: ucode did not self-"
			  "suspend!\n", wlc_hw->unit);

	brcms_c_gpio_init(wlc);

	sflags = bcma_aread32(core, BCMA_IOST);

	if (D11REV_IS(wlc_hw->corerev, 23)) {
		if (BRCMS_ISNPHY(wlc_hw->band))
			brcms_c_write_inits(wlc_hw, ucode->d11n0initvals16);
		else
			brcms_err(core, "%s: wl%d: unsupported phy in corerev"
				  " %d\n", __func__, wlc_hw->unit,
				  wlc_hw->corerev);
	} else if (D11REV_IS(wlc_hw->corerev, 24)) {
		if (BRCMS_ISLCNPHY(wlc_hw->band))
			brcms_c_write_inits(wlc_hw, ucode->d11lcn0initvals24);
		else
			brcms_err(core, "%s: wl%d: unsupported phy in corerev"
				  " %d\n", __func__, wlc_hw->unit,
				  wlc_hw->corerev);
	} else {
		brcms_err(core, "%s: wl%d: unsupported corerev %d\n",
			  __func__, wlc_hw->unit, wlc_hw->corerev);
	}

	/* For old ucode, txfifo sizes needs to be modified(increased) */
	if (fifosz_fixup)
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
		brcms_err(core, "wlc_coreinit: txfifo mismatch: ucode size %d"
			  " driver size %d index %d\n", buf[i],
			  wlc_hw->xmtfifo_sz[i], i);

	/* make sure we can still talk to the mac */
	WARN_ON(bcma_read32(core, D11REGOFFS(maccontrol)) == 0xffffffff);

	/* band-specific inits done by wlc_bsinit() */

	/* Set up frame burst size and antenna swap threshold init values */
	brcms_b_write_shm(wlc_hw, M_MBURST_SIZE, MAXTXFRAMEBURST);
	brcms_b_write_shm(wlc_hw, M_MAX_ANTCNT, ANTCNT);

	/* enable one rx interrupt per received frame */
	bcma_write32(core, D11REGOFFS(intrcvlazy[0]), (1 << IRL_FC_SHIFT));

	/* set the station mode (BSS STA) */
	brcms_b_mctrl(wlc_hw,
		       (MCTL_INFRA | MCTL_DISCARD_PMQ | MCTL_AP),
		       (MCTL_INFRA | MCTL_DISCARD_PMQ));

	/* set up Beacon interval */
	bcnint_us = 0x8000 << 10;
	bcma_write32(core, D11REGOFFS(tsf_cfprep),
		     (bcnint_us << CFPREP_CBI_SHIFT));
	bcma_write32(core, D11REGOFFS(tsf_cfpstart), bcnint_us);
	bcma_write32(core, D11REGOFFS(macintstatus), MI_GP1);

	/* write interrupt mask */
	bcma_write32(core, D11REGOFFS(intctrlregs[RX_FIFO].intmask),
		     DEF_RXINTMASK);

	/* allow the MAC to control the PHY clock (dynamic on/off) */
	brcms_b_macphyclk_set(wlc_hw, ON);

	/* program dynamic clock control fast powerup delay register */
	wlc->fastpwrup_dly = ai_clkctl_fast_pwrup_delay(wlc_hw->sih);
	bcma_write16(core, D11REGOFFS(scc_fastpwrup_dly), wlc->fastpwrup_dly);

	/* tell the ucode the corerev */
	brcms_b_write_shm(wlc_hw, M_MACHW_VER, (u16) wlc_hw->corerev);

	/* tell the ucode MAC capabilities */
	brcms_b_write_shm(wlc_hw, M_MACHW_CAP_L,
			   (u16) (wlc_hw->machwcap & 0xffff));
	brcms_b_write_shm(wlc_hw, M_MACHW_CAP_H,
			   (u16) ((wlc_hw->
				      machwcap >> 16) & 0xffff));

	/* write retry limits to SCR, this done after PSM init */
	bcma_write32(core, D11REGOFFS(objaddr),
		     OBJADDR_SCR_SEL | S_DOT11_SRC_LMT);
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	bcma_write32(core, D11REGOFFS(objdata), wlc_hw->SRL);
	bcma_write32(core, D11REGOFFS(objaddr),
		     OBJADDR_SCR_SEL | S_DOT11_LRC_LMT);
	(void)bcma_read32(core, D11REGOFFS(objaddr));
	bcma_write32(core, D11REGOFFS(objdata), wlc_hw->LRL);

	/* write rate fallback retry limits */
	brcms_b_write_shm(wlc_hw, M_SFRMTXCNTFBRTHSD, wlc_hw->SFBL);
	brcms_b_write_shm(wlc_hw, M_LFRMTXCNTFBRTHSD, wlc_hw->LFBL);

	bcma_mask16(core, D11REGOFFS(ifs_ctl), 0x0FFF);
	bcma_write16(core, D11REGOFFS(ifs_aifsn), EDCF_AIFSN_MIN);

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
static brcms_b_init(struct brcms_hardware *wlc_hw, u16 chanspec) {
	u32 macintmask;
	bool fastclk;
	struct brcms_c_info *wlc = wlc_hw->wlc;

	/* request FAST clock if not on */
	fastclk = wlc_hw->forcefastclk;
	if (!fastclk)
		brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_FAST);

	/* disable interrupts */
	macintmask = brcms_intrsoff(wlc->wl);

	/* set up the specified band and chanspec */
	brcms_c_setxband(wlc_hw, chspec_bandunit(chanspec));
	wlc_phy_chanspec_radio_set(wlc_hw->band->pi, chanspec);

	/* do one-time phy inits and calibration */
	wlc_phy_cal_init(wlc_hw->band->pi);

	/* core-specific initialization */
	brcms_b_coreinit(wlc);

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
		brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_DYNAMIC);
}

static void brcms_c_set_phy_chanspec(struct brcms_c_info *wlc,
				     u16 chanspec)
{
	/* Save our copy of the chanspec */
	wlc->chanspec = chanspec;

	/* Set the chanspec and power limits for this locale */
	brcms_c_channel_set_chanspec(wlc->cmi, chanspec, BRCMS_TXPWR_MAX);

	if (wlc->stf->ss_algosel_auto)
		brcms_c_stf_ss_algo_channel_get(wlc, &wlc->stf->ss_algo_channel,
					    chanspec);

	brcms_c_stf_ss_update(wlc, wlc->band);
}

static void
brcms_default_rateset(struct brcms_c_info *wlc, struct brcms_c_rateset *rs)
{
	brcms_c_rateset_default(rs, NULL, wlc->band->phytype,
		wlc->band->bandtype, false, BRCMS_RATE_MASK_FULL,
		(bool) (wlc->pub->_n_enab & SUPPORT_11N),
		brcms_chspec_bw(wlc->default_bss->chanspec),
		wlc->stf->txstreams);
}

/* derive wlc->band->basic_rate[] table from 'rateset' */
static void brcms_c_rate_lookup_init(struct brcms_c_info *wlc,
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
			brcms_err(wlc->hw->d11core, "brcms_c_rate_lookup_init: "
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
			if (is_ofdm_rate(rate))
				ofdm_basic = rate;
			else
				cck_basic = rate;

			continue;
		}

		/* This rate is not a basic rate so figure out the
		 * best basic rate less than this rate and fill in
		 * the hole in the table
		 */

		br[rate] = is_ofdm_rate(rate) ? ofdm_basic : cck_basic;

		if (br[rate] != 0)
			continue;

		if (is_ofdm_rate(rate)) {
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

static void brcms_c_bandinit_ordered(struct brcms_c_info *wlc,
				     u16 chanspec)
{
	struct brcms_c_rateset default_rateset;
	uint parkband;
	uint i, band_order[2];

	/*
	 * We might have been bandlocked during down and the chip
	 * power-cycled (hibernate). Figure out the right band to park on
	 */
	if (wlc->bandlocked || wlc->pub->_nbands == 1) {
		/* updated in brcms_c_bandlock() */
		parkband = wlc->band->bandunit;
		band_order[0] = band_order[1] = parkband;
	} else {
		/* park on the band of the specified chanspec */
		parkband = chspec_bandunit(chanspec);

		/* order so that parkband initialize last */
		band_order[0] = parkband ^ 1;
		band_order[1] = parkband;
	}

	/* make each band operational, software state init */
	for (i = 0; i < wlc->pub->_nbands; i++) {
		uint j = band_order[i];

		wlc->band = wlc->bandstate[j];

		brcms_default_rateset(wlc, &default_rateset);

		/* fill in hw_rate */
		brcms_c_rateset_filter(&default_rateset, &wlc->band->hw_rateset,
				   false, BRCMS_RATES_CCK_OFDM, BRCMS_RATE_MASK,
				   (bool) (wlc->pub->_n_enab & SUPPORT_11N));

		/* init basic rate lookup */
		brcms_c_rate_lookup_init(wlc, &default_rateset);
	}

	/* sync up phy/radio chanspec */
	brcms_c_set_phy_chanspec(wlc, chanspec);
}

/*
 * Set or clear filtering related maccontrol bits based on
 * specified filter flags
 */
void brcms_c_mac_promisc(struct brcms_c_info *wlc, uint filter_flags)
{
	u32 promisc_bits = 0;

	wlc->filter_flags = filter_flags;

	if (filter_flags & (FIF_PROMISC_IN_BSS | FIF_OTHER_BSS))
		promisc_bits |= MCTL_PROMISC;

	if (filter_flags & FIF_BCN_PRBRESP_PROMISC)
		promisc_bits |= MCTL_BCNS_PROMISC;

	if (filter_flags & FIF_FCSFAIL)
		promisc_bits |= MCTL_KEEPBADFCS;

	if (filter_flags & (FIF_CONTROL | FIF_PSPOLL))
		promisc_bits |= MCTL_KEEPCONTROL;

	brcms_b_mctrl(wlc->hw,
		MCTL_PROMISC | MCTL_BCNS_PROMISC |
		MCTL_KEEPCONTROL | MCTL_KEEPBADFCS,
		promisc_bits);
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
	if (wlc->home_chanspec == wlc_phy_chanspec_get(wlc->band->pi)) {
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
				brcms_b_write_shm(wlc->hw,
						M_BCN_TXTSF_OFFSET, 0);
		}
	} else {
		/* disable an active IBSS if we are not on the home channel */
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
	dir_table = is_ofdm_rate(basic_rate) ? M_RT_DIRMAP_A : M_RT_DIRMAP_B;

	/* Shared memory address for the table we are writing */
	basic_table = is_ofdm_rate(rate) ? M_RT_BBRSMAP_A : M_RT_BBRSMAP_B;

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
	basic_ptr = brcms_b_read_shm(wlc->hw, (dir_table + basic_index * 2));

	/* Update the SHM BSS-basic-rate-set mapping table with the pointer
	 * to the correct basic rate for the given incoming rate
	 */
	brcms_b_write_shm(wlc->hw, (basic_table + index * 2), basic_ptr);
}

static const struct brcms_c_rateset *
brcms_c_rateset_get_hwrs(struct brcms_c_info *wlc)
{
	const struct brcms_c_rateset *rs_dflt;

	if (BRCMS_PHY_11N_CAP(wlc->band)) {
		if (wlc->band->bandtype == BRCM_BAND_5G)
			rs_dflt = &ofdm_mimo_rates;
		else
			rs_dflt = &cck_ofdm_mimo_rates;
	} else if (wlc->band->gmode)
		rs_dflt = &cck_ofdm_rates;
	else
		rs_dflt = &cck_rates;

	return rs_dflt;
}

static void brcms_c_set_ratetable(struct brcms_c_info *wlc)
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

		/* for a given rate brcms_basic_rate returns the rate at
		 * which a response ACK/CTS should be sent.
		 */
		basic_rate = brcms_basic_rate(wlc, rate);
		if (basic_rate == 0)
			/* This should only happen if we are using a
			 * restricted rateset.
			 */
			basic_rate = rs.rates[0] & BRCMS_RATE_MASK;

		brcms_c_write_rate_shm(wlc, rate, basic_rate);
	}
}

/* band-specific init */
static void brcms_c_bsinit(struct brcms_c_info *wlc)
{
	brcms_dbg_info(wlc->hw->d11core, "wl%d: bandunit %d\n",
		       wlc->pub->unit, wlc->band->bandunit);

	/* write ucode ACK/CTS rate table */
	brcms_c_set_ratetable(wlc);

	/* update some band specific mac configuration */
	brcms_c_ucode_mac_upd(wlc);

	/* init antenna selection */
	brcms_c_antsel_init(wlc->asi);

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
		brcms_err(wlc->hw->d11core,
			  "wl%d:  duty cycle value off limit\n",
			  wlc->pub->unit);
		return -EINVAL;
	}
	if (duty_cycle)
		idle_busy_ratio_x_16 = (100 - duty_cycle) * 16 / duty_cycle;
	/* Only write to shared memory  when wl is up */
	if (writeToShm)
		brcms_b_write_shm(wlc->hw, offset, (u16) idle_busy_ratio_x_16);

	if (isOFDM)
		wlc->tx_duty_cycle_ofdm = (u16) duty_cycle;
	else
		wlc->tx_duty_cycle_cck = (u16) duty_cycle;

	return 0;
}

/* push sw hps and wake state through hardware */
static void brcms_c_set_ps_ctrl(struct brcms_c_info *wlc)
{
	u32 v1, v2;
	bool hps;
	bool awake_before;

	hps = brcms_c_ps_allowed(wlc);

	BCMMSG(wlc->wiphy, "wl%d: hps %d\n", wlc->pub->unit, hps);

	v1 = bcma_read32(wlc->hw->d11core, D11REGOFFS(maccontrol));
	v2 = MCTL_WAKE;
	if (hps)
		v2 |= MCTL_HPS;

	brcms_b_mctrl(wlc->hw, MCTL_WAKE | MCTL_HPS, v2);

	awake_before = ((v1 & MCTL_WAKE) || ((v1 & MCTL_HPS) == 0));

	if (!awake_before)
		brcms_b_wait_for_wake(wlc->hw);
}

/*
 * Write this BSS config's MAC address to core.
 * Updates RXE match engine.
 */
static int brcms_c_set_mac(struct brcms_bss_cfg *bsscfg)
{
	int err = 0;
	struct brcms_c_info *wlc = bsscfg->wlc;

	/* enter the MAC addr into the RXE match registers */
	brcms_c_set_addrmatch(wlc, RCM_MAC_OFFSET, bsscfg->cur_etheraddr);

	brcms_c_ampdu_macaddr_upd(wlc);

	return err;
}

/* Write the BSS config's BSSID address to core (set_bssid in d11procs.tcl).
 * Updates RXE match engine.
 */
static void brcms_c_set_bssid(struct brcms_bss_cfg *bsscfg)
{
	/* we need to update BSSID in RXE match registers */
	brcms_c_set_addrmatch(bsscfg->wlc, RCM_BSSID_OFFSET, bsscfg->BSSID);
}

static void brcms_b_set_shortslot(struct brcms_hardware *wlc_hw, bool shortslot)
{
	wlc_hw->shortslot = shortslot;

	if (wlc_hw->band->bandtype == BRCM_BAND_2G && wlc_hw->up) {
		brcms_c_suspend_mac_and_wait(wlc_hw->wlc);
		brcms_b_update_slot_timing(wlc_hw, shortslot);
		brcms_c_enable_mac(wlc_hw->wlc);
	}
}

/*
 * Suspend the the MAC and update the slot timing
 * for standard 11b/g (20us slots) or shortslot 11g (9us slots).
 */
static void brcms_c_switch_shortslot(struct brcms_c_info *wlc, bool shortslot)
{
	/* use the override if it is set */
	if (wlc->shortslot_override != BRCMS_SHORTSLOT_AUTO)
		shortslot = (wlc->shortslot_override == BRCMS_SHORTSLOT_ON);

	if (wlc->shortslot == shortslot)
		return;

	wlc->shortslot = shortslot;

	brcms_b_set_shortslot(wlc->hw, shortslot);
}

static void brcms_c_set_home_chanspec(struct brcms_c_info *wlc, u16 chanspec)
{
	if (wlc->home_chanspec != chanspec) {
		wlc->home_chanspec = chanspec;

		if (wlc->bsscfg->associated)
			wlc->bsscfg->current_bss->chanspec = chanspec;
	}
}

void
brcms_b_set_chanspec(struct brcms_hardware *wlc_hw, u16 chanspec,
		      bool mute_tx, struct txpwr_limits *txpwr)
{
	uint bandunit;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d: 0x%x\n", wlc_hw->unit, chanspec);

	wlc_hw->chanspec = chanspec;

	/* Switch bands if necessary */
	if (wlc_hw->_nbands > 1) {
		bandunit = chspec_bandunit(chanspec);
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

	wlc_phy_initcal_enable(wlc_hw->band->pi, !mute_tx);

	if (!wlc_hw->up) {
		if (wlc_hw->clk)
			wlc_phy_txpower_limit_set(wlc_hw->band->pi, txpwr,
						  chanspec);
		wlc_phy_chanspec_radio_set(wlc_hw->band->pi, chanspec);
	} else {
		wlc_phy_chanspec_set(wlc_hw->band->pi, chanspec);
		wlc_phy_txpower_limit_set(wlc_hw->band->pi, txpwr, chanspec);

		/* Update muting of the channel */
		brcms_b_mute(wlc_hw, mute_tx);
	}
}

/* switch to and initialize new band */
static void brcms_c_setband(struct brcms_c_info *wlc,
					   uint bandunit)
{
	wlc->band = wlc->bandstate[bandunit];

	if (!wlc->pub->up)
		return;

	/* wait for at least one beacon before entering sleeping state */
	brcms_c_set_ps_ctrl(wlc);

	/* band-specific initializations */
	brcms_c_bsinit(wlc);
}

static void brcms_c_set_chanspec(struct brcms_c_info *wlc, u16 chanspec)
{
	uint bandunit;
	bool switchband = false;
	u16 old_chanspec = wlc->chanspec;

	if (!brcms_c_valid_chanspec_db(wlc->cmi, chanspec)) {
		brcms_err(wlc->hw->d11core, "wl%d: %s: Bad channel %d\n",
			  wlc->pub->unit, __func__, CHSPEC_CHANNEL(chanspec));
		return;
	}

	/* Switch bands if necessary */
	if (wlc->pub->_nbands > 1) {
		bandunit = chspec_bandunit(chanspec);
		if (wlc->band->bandunit != bandunit || wlc->bandinit_pending) {
			switchband = true;
			if (wlc->bandlocked) {
				brcms_err(wlc->hw->d11core,
					  "wl%d: %s: chspec %d band is locked!\n",
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
	if (brcms_chspec_bw(old_chanspec) != brcms_chspec_bw(chanspec)) {
		brcms_c_antsel_init(wlc->asi);

		/* Fix the hardware rateset based on bw.
		 * Mainly add MCS32 for 40Mhz, remove MCS 32 for 20Mhz
		 */
		brcms_c_rateset_bw_mcs_filter(&wlc->band->hw_rateset,
			wlc->band->mimo_cap_40 ? brcms_chspec_bw(chanspec) : 0);
	}

	/* update some mac configuration since chanspec changed */
	brcms_c_ucode_mac_upd(wlc);
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

	phyctl = brcms_b_read_shm(wlc->hw, M_BCN_PCTLWD);
	phyctl = (phyctl & ~mask) | phytxant;
	brcms_b_write_shm(wlc->hw, M_BCN_PCTLWD, phyctl);
}

/*
 * centralized protection config change function to simplify debugging, no
 * consistency checking this should be called only on changes to avoid overhead
 * in periodic function
 */
void brcms_c_protection_upd(struct brcms_c_info *wlc, uint idx, int val)
{
	/*
	 * Cannot use brcms_dbg_* here because this function is called
	 * before wlc is sufficiently initialized.
	 */
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
	if (wlc->pub->up) {
		brcms_c_update_beacon(wlc);
		brcms_c_update_probe_resp(wlc, true);
	}
}

static void brcms_c_ht_update_ldpc(struct brcms_c_info *wlc, s8 val)
{
	wlc->stf->ldpc = val;

	if (wlc->pub->up) {
		brcms_c_update_beacon(wlc);
		brcms_c_update_probe_resp(wlc, true);
		wlc_phy_ldpc_override_set(wlc->band->pi, (val ? true : false));
	}
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
		brcms_err(wlc->hw->d11core, "wl%d: %s : no-clock\n",
			  wlc->pub->unit, __func__);
		return;
	}

	memset((char *)&acp_shm, 0, sizeof(struct shm_acparams));
	/* fill in shm ac params struct */
	acp_shm.txop = params->txop;
	/* convert from units of 32us to us for ucode */
	wlc->edcf_txop[aci & 0x3] = acp_shm.txop =
	    EDCF_TXOP2USEC(acp_shm.txop);
	acp_shm.aifs = (params->aifs & EDCF_AIFSN_MASK);

	if (aci == IEEE80211_AC_VI && acp_shm.txop == 0
	    && acp_shm.aifs < EDCF_AIFSN_MAX)
		acp_shm.aifs++;

	if (acp_shm.aifs < EDCF_AIFSN_MIN
	    || acp_shm.aifs > EDCF_AIFSN_MAX) {
		brcms_err(wlc->hw->d11core, "wl%d: edcf_setparams: bad "
			  "aifs %d\n", wlc->pub->unit, acp_shm.aifs);
	} else {
		acp_shm.cwmin = params->cw_min;
		acp_shm.cwmax = params->cw_max;
		acp_shm.cwcur = acp_shm.cwmin;
		acp_shm.bslots =
			bcma_read16(wlc->hw->d11core, D11REGOFFS(tsf_random)) &
			acp_shm.cwcur;
		acp_shm.reggap = acp_shm.bslots + acp_shm.aifs;
		/* Indicate the new params to the ucode */
		acp_shm.status = brcms_b_read_shm(wlc->hw, (M_EDCF_QINFO +
						  wme_ac2fifo[aci] *
						  M_EDCF_QLEN +
						  M_EDCF_STATUS_OFF));
		acp_shm.status |= WME_STATUS_NEWAC;

		/* Fill in shm acparam table */
		shm_entry = (u16 *) &acp_shm;
		for (i = 0; i < (int)sizeof(struct shm_acparams); i += 2)
			brcms_b_write_shm(wlc->hw,
					  M_EDCF_QINFO +
					  wme_ac2fifo[aci] * M_EDCF_QLEN + i,
					  *shm_entry++);
	}

	if (suspend) {
		brcms_c_suspend_mac_and_wait(wlc);
		brcms_c_enable_mac(wlc);
	}
}

static void brcms_c_edcf_setparams(struct brcms_c_info *wlc, bool suspend)
{
	u16 aci;
	int i_ac;
	struct ieee80211_tx_queue_params txq_pars;
	static const struct edcf_acparam default_edcf_acparams[] = {
		 {EDCF_AC_BE_ACI_STA, EDCF_AC_BE_ECW_STA, EDCF_AC_BE_TXOP_STA},
		 {EDCF_AC_BK_ACI_STA, EDCF_AC_BK_ECW_STA, EDCF_AC_BK_TXOP_STA},
		 {EDCF_AC_VI_ACI_STA, EDCF_AC_VI_ECW_STA, EDCF_AC_VI_TXOP_STA},
		 {EDCF_AC_VO_ACI_STA, EDCF_AC_VO_ECW_STA, EDCF_AC_VO_TXOP_STA}
	}; /* ucode needs these parameters during its initialization */
	const struct edcf_acparam *edcf_acp = &default_edcf_acparams[0];

	for (i_ac = 0; i_ac < IEEE80211_NUM_ACS; i_ac++, edcf_acp++) {
		/* find out which ac this set of params applies to */
		aci = (edcf_acp->ACI & EDCF_ACI_MASK) >> EDCF_ACI_SHIFT;

		/* fill in shm ac params struct */
		txq_pars.txop = edcf_acp->TXOP;
		txq_pars.aifs = edcf_acp->ACI;

		/* CWmin = 2^(ECWmin) - 1 */
		txq_pars.cw_min = EDCF_ECW2CW(edcf_acp->ECW & EDCF_ECWMIN_MASK);
		/* CWmax = 2^(ECWmax) - 1 */
		txq_pars.cw_max = EDCF_ECW2CW((edcf_acp->ECW & EDCF_ECWMAX_MASK)
					    >> EDCF_ECWMAX_SHIFT);
		brcms_c_wme_setparams(wlc, aci, &txq_pars, suspend);
	}

	if (suspend) {
		brcms_c_suspend_mac_and_wait(wlc);
		brcms_c_enable_mac(wlc);
	}
}

static void brcms_c_radio_monitor_start(struct brcms_c_info *wlc)
{
	/* Don't start the timer if HWRADIO feature is disabled */
	if (wlc->radio_monitor)
		return;

	wlc->radio_monitor = true;
	brcms_b_pllreq(wlc->hw, true, BRCMS_PLLREQ_RADIO_MON);
	brcms_add_timer(wlc->radio_timer, TIMER_INTERVAL_RADIOCHK, true);
}

static bool brcms_c_radio_monitor_stop(struct brcms_c_info *wlc)
{
	if (!wlc->radio_monitor)
		return true;

	wlc->radio_monitor = false;
	brcms_b_pllreq(wlc->hw, false, BRCMS_PLLREQ_RADIO_MON);
	return brcms_del_timer(wlc->radio_timer);
}

/* read hwdisable state and propagate to wlc flag */
static void brcms_c_radio_hwdisable_upd(struct brcms_c_info *wlc)
{
	if (wlc->pub->hw_off)
		return;

	if (brcms_b_radio_read_hwdisabled(wlc->hw))
		mboolset(wlc->pub->radio_disabled, WL_RADIO_HW_DISABLE);
	else
		mboolclr(wlc->pub->radio_disabled, WL_RADIO_HW_DISABLE);
}

/* update hwradio status and return it */
bool brcms_c_check_radio_disabled(struct brcms_c_info *wlc)
{
	brcms_c_radio_hwdisable_upd(wlc);

	return mboolisset(wlc->pub->radio_disabled, WL_RADIO_HW_DISABLE) ?
			true : false;
}

/* periodical query hw radio button while driver is "down" */
static void brcms_c_radio_timer(void *arg)
{
	struct brcms_c_info *wlc = (struct brcms_c_info *) arg;

	if (brcms_deviceremoved(wlc)) {
		brcms_err(wlc->hw->d11core, "wl%d: %s: dead chip\n",
			  wlc->pub->unit, __func__);
		brcms_down(wlc->wl);
		return;
	}

	brcms_c_radio_hwdisable_upd(wlc);
}

/* common low-level watchdog code */
static void brcms_b_watchdog(struct brcms_c_info *wlc)
{
	struct brcms_hardware *wlc_hw = wlc->hw;

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

/* common watchdog code */
static void brcms_c_watchdog(struct brcms_c_info *wlc)
{
	brcms_dbg_info(wlc->hw->d11core, "wl%d\n", wlc->pub->unit);

	if (!wlc->pub->up)
		return;

	if (brcms_deviceremoved(wlc)) {
		brcms_err(wlc->hw->d11core, "wl%d: %s: dead chip\n",
			  wlc->pub->unit, __func__);
		brcms_down(wlc->wl);
		return;
	}

	/* increment second count */
	wlc->pub->now++;

	brcms_c_radio_hwdisable_upd(wlc);
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

	if (BRCMS_ISNPHY(wlc->band) &&
	    ((wlc->pub->now - wlc->tempsense_lasttime) >=
	     BRCMS_TEMPSENSE_PERIOD)) {
		wlc->tempsense_lasttime = wlc->pub->now;
		brcms_c_tempsense_upd(wlc);
	}
}

static void brcms_c_watchdog_by_timer(void *arg)
{
	struct brcms_c_info *wlc = (struct brcms_c_info *) arg;

	brcms_c_watchdog(wlc);
}

static bool brcms_c_timers_init(struct brcms_c_info *wlc, int unit)
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
static void brcms_c_info_init(struct brcms_c_info *wlc, int unit)
{
	int i;

	/* Save our copy of the chanspec */
	wlc->chanspec = ch20mhz_chspec(1);

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

	/* WME QoS mode is Auto by default */
	wlc->pub->_ampdu = AMPDU_AGG_HOST;
	wlc->pub->bcmerror = 0;
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

/* low level attach
 *    run backplane attach, init nvram
 *    run phy attach
 *    initialize software state for each core and band
 *    put the whole chip in reset(driver down state), no clock
 */
static int brcms_b_attach(struct brcms_c_info *wlc, struct bcma_device *core,
			  uint unit, bool piomode)
{
	struct brcms_hardware *wlc_hw;
	uint err = 0;
	uint j;
	bool wme = false;
	struct shared_phy_params sha_params;
	struct wiphy *wiphy = wlc->wiphy;
	struct pci_dev *pcidev = core->bus->host_pci;
	struct ssb_sprom *sprom = &core->bus->sprom;

	if (core->bus->hosttype == BCMA_HOSTTYPE_PCI)
		brcms_dbg_info(core, "wl%d: vendor 0x%x device 0x%x\n", unit,
			       pcidev->vendor,
			       pcidev->device);
	else
		brcms_dbg_info(core, "wl%d: vendor 0x%x device 0x%x\n", unit,
			       core->bus->boardinfo.vendor,
			       core->bus->boardinfo.type);

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
	wlc_hw->sih = ai_attach(core->bus);
	if (wlc_hw->sih == NULL) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: si_attach failed\n",
			  unit);
		err = 11;
		goto fail;
	}

	/* verify again the device is supported */
	if (!brcms_c_chipmatch(core)) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: Unsupported device\n",
			 unit);
		err = 12;
		goto fail;
	}

	if (core->bus->hosttype == BCMA_HOSTTYPE_PCI) {
		wlc_hw->vendorid = pcidev->vendor;
		wlc_hw->deviceid = pcidev->device;
	} else {
		wlc_hw->vendorid = core->bus->boardinfo.vendor;
		wlc_hw->deviceid = core->bus->boardinfo.type;
	}

	wlc_hw->d11core = core;
	wlc_hw->corerev = core->id.rev;

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
	brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_FAST);
	brcms_b_corereset(wlc_hw, BRCMS_USE_COREFLAGS);

	if (!brcms_b_validate_chip_access(wlc_hw)) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: validate_chip_access "
			"failed\n", unit);
		err = 14;
		goto fail;
	}

	/* get the board rev, used just below */
	j = sprom->board_rev;
	/* promote srom boardrev of 0xFF to 1 */
	if (j == BOARDREV_PROMOTABLE)
		j = BOARDREV_PROMOTED;
	wlc_hw->boardrev = (u16) j;
	if (!brcms_c_validboardtype(wlc_hw)) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: Unsupported Broadcom "
			  "board type (0x%x)" " or revision level (0x%x)\n",
			  unit, ai_get_boardtype(wlc_hw->sih),
			  wlc_hw->boardrev);
		err = 15;
		goto fail;
	}
	wlc_hw->sromrev = sprom->revision;
	wlc_hw->boardflags = sprom->boardflags_lo + (sprom->boardflags_hi << 16);
	wlc_hw->boardflags2 = sprom->boardflags2_lo + (sprom->boardflags2_hi << 16);

	if (wlc_hw->boardflags & BFL_NOPLLDOWN)
		brcms_b_pllreq(wlc_hw, true, BRCMS_PLLREQ_SHARED);

	/* check device id(srom, nvram etc.) to set bands */
	if (wlc_hw->deviceid == BCM43224_D11N_ID ||
	    wlc_hw->deviceid == BCM43224_D11N_ID_VEN1)
		/* Dualband boards */
		wlc_hw->_nbands = 2;
	else
		wlc_hw->_nbands = 1;

	if ((ai_get_chip_id(wlc_hw->sih) == BCMA_CHIP_ID_BCM43225))
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
	sha_params.vid = wlc_hw->vendorid;
	sha_params.did = wlc_hw->deviceid;
	sha_params.chip = ai_get_chip_id(wlc_hw->sih);
	sha_params.chiprev = ai_get_chiprev(wlc_hw->sih);
	sha_params.chippkg = ai_get_chippkg(wlc_hw->sih);
	sha_params.sromrev = wlc_hw->sromrev;
	sha_params.boardtype = ai_get_boardtype(wlc_hw->sih);
	sha_params.boardrev = wlc_hw->boardrev;
	sha_params.boardflags = wlc_hw->boardflags;
	sha_params.boardflags2 = wlc_hw->boardflags2;

	/* alloc and save pointer to shared phy state area */
	wlc_hw->phy_sh = wlc_phy_shared_attach(&sha_params);
	if (!wlc_hw->phy_sh) {
		err = 16;
		goto fail;
	}

	/* initialize software state for each core and band */
	for (j = 0; j < wlc_hw->_nbands; j++) {
		/*
		 * band0 is always 2.4Ghz
		 * band1, if present, is 5Ghz
		 */

		brcms_c_setxband(wlc_hw, j);

		wlc_hw->band->bandunit = j;
		wlc_hw->band->bandtype = j ? BRCM_BAND_5G : BRCM_BAND_2G;
		wlc->band->bandunit = j;
		wlc->band->bandtype = j ? BRCM_BAND_5G : BRCM_BAND_2G;
		wlc->core->coreidx = core->core_index;

		wlc_hw->machwcap = bcma_read32(core, D11REGOFFS(machwcap));
		wlc_hw->machwcap_backup = wlc_hw->machwcap;

		/* init tx fifo size */
		WARN_ON((wlc_hw->corerev - XMTFIFOTBL_STARTREV) < 0 ||
			(wlc_hw->corerev - XMTFIFOTBL_STARTREV) >
				ARRAY_SIZE(xmtfifo_sz));
		wlc_hw->xmtfifo_sz =
		    xmtfifo_sz[(wlc_hw->corerev - XMTFIFOTBL_STARTREV)];
		WARN_ON(!wlc_hw->xmtfifo_sz[0]);

		/* Get a phy for this band */
		wlc_hw->band->pi =
			wlc_phy_attach(wlc_hw->phy_sh, core,
				       wlc_hw->band->bandtype,
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
	brcms_c_get_macaddr(wlc_hw, wlc_hw->etheraddr);

	if (is_broadcast_ether_addr(wlc_hw->etheraddr) ||
	    is_zero_ether_addr(wlc_hw->etheraddr)) {
		wiphy_err(wiphy, "wl%d: brcms_b_attach: bad macaddr\n",
			  unit);
		err = 22;
		goto fail;
	}

	brcms_dbg_info(wlc_hw->d11core, "deviceid 0x%x nbands %d board 0x%x\n",
		       wlc_hw->deviceid, wlc_hw->_nbands,
		       ai_get_boardtype(wlc_hw->sih));

	return err;

 fail:
	wiphy_err(wiphy, "wl%d: brcms_b_attach: failed with err %d\n", unit,
		  err);
	return err;
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
	int bandtype;
	struct ssb_sprom *sprom = &wlc->hw->d11core->bus->sprom;

	unit = wlc->pub->unit;
	bandtype = wlc->band->bandtype;

	/* get antennas available */
	if (bandtype == BRCM_BAND_5G)
		aa = sprom->ant_available_a;
	else
		aa = sprom->ant_available_bg;

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
	if (bandtype == BRCM_BAND_5G)
		wlc->band->antgain = sprom->antenna_gain.a1;
	else
		wlc->band->antgain = sprom->antenna_gain.a0;

	brcms_c_attach_antgain_init(wlc);

	return true;
}

static void brcms_c_bss_default_init(struct brcms_c_info *wlc)
{
	u16 chanspec;
	struct brcms_band *band;
	struct brcms_bss_info *bi = wlc->default_bss;

	/* init default and target BSS with some sane initial values */
	memset((char *)(bi), 0, sizeof(struct brcms_bss_info));
	bi->beacon_period = BEACON_INTERVAL_DEFAULT;

	/* fill the default channel as the first valid channel
	 * starting from the 2G channels
	 */
	chanspec = ch20mhz_chspec(1);
	wlc->home_chanspec = bi->chanspec = chanspec;

	/* find the band of our default channel */
	band = wlc->band;
	if (wlc->pub->_nbands > 1 &&
	    band->bandunit != chspec_bandunit(chanspec))
		band = wlc->bandstate[OTHERBANDUNIT(wlc)];

	/* init bss rates to the band specific default rate set */
	brcms_c_rateset_default(&bi->rateset, NULL, band->phytype,
		band->bandtype, false, BRCMS_RATE_MASK_FULL,
		(bool) (wlc->pub->_n_enab & SUPPORT_11N),
		brcms_chspec_bw(chanspec), wlc->stf->txstreams);

	if (wlc->pub->_n_enab & SUPPORT_11N)
		bi->flags |= BRCMS_BSS_HT;
}

static void brcms_c_update_mimo_band_bwcap(struct brcms_c_info *wlc, u8 bwcap)
{
	uint i;
	struct brcms_band *band;

	for (i = 0; i < wlc->pub->_nbands; i++) {
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

static void brcms_c_timers_deinit(struct brcms_c_info *wlc)
{
	/* free timer state */
	if (wlc->wdtimer) {
		brcms_free_timer(wlc->wdtimer);
		wlc->wdtimer = NULL;
	}
	if (wlc->radio_timer) {
		brcms_free_timer(wlc->radio_timer);
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
static int brcms_b_detach(struct brcms_c_info *wlc)
{
	uint i;
	struct brcms_hw_band *band;
	struct brcms_hardware *wlc_hw = wlc->hw;
	int callbacks;

	callbacks = 0;

	brcms_b_detach_dmapio(wlc_hw);

	band = wlc_hw->band;
	for (i = 0; i < wlc_hw->_nbands; i++) {
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

	brcms_c_detach_mfree(wlc);
	return callbacks;
}

/* update state that depends on the current value of "ap" */
static void brcms_c_ap_upd(struct brcms_c_info *wlc)
{
	/* STA-BSS; short capable */
	wlc->PLCPHdr_override = BRCMS_PLCP_SHORT;
}

/* Initialize just the hardware when coming out of POR or S3/S5 system states */
static void brcms_b_hw_up(struct brcms_hardware *wlc_hw)
{
	if (wlc_hw->wlc->pub->hw_up)
		return;

	brcms_dbg_info(wlc_hw->d11core, "wl%d\n", wlc_hw->unit);

	/*
	 * Enable pll and xtal, initialize the power control registers,
	 * and force fastclock for the remainder of brcms_c_up().
	 */
	brcms_b_xtal(wlc_hw, ON);
	ai_clkctl_init(wlc_hw->sih);
	brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_FAST);

	/*
	 * TODO: test suspend/resume
	 *
	 * AI chip doesn't restore bar0win2 on
	 * hibernation/resume, need sw fixup
	 */

	/*
	 * Inform phy that a POR reset has occurred so
	 * it does a complete phy init
	 */
	wlc_phy_por_inform(wlc_hw->band->pi);

	wlc_hw->ucode_loaded = false;
	wlc_hw->wlc->pub->hw_up = true;

	if ((wlc_hw->boardflags & BFL_FEM)
	    && (ai_get_chip_id(wlc_hw->sih) == BCMA_CHIP_ID_BCM4313)) {
		if (!
		    (wlc_hw->boardrev >= 0x1250
		     && (wlc_hw->boardflags & BFL_FEM_BT)))
			ai_epa_4313war(wlc_hw->sih);
	}
}

static int brcms_b_up_prep(struct brcms_hardware *wlc_hw)
{
	brcms_dbg_info(wlc_hw->d11core, "wl%d\n", wlc_hw->unit);

	/*
	 * Enable pll and xtal, initialize the power control registers,
	 * and force fastclock for the remainder of brcms_c_up().
	 */
	brcms_b_xtal(wlc_hw, ON);
	ai_clkctl_init(wlc_hw->sih);
	brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_FAST);

	/*
	 * Configure pci/pcmcia here instead of in brcms_c_attach()
	 * to allow mfg hotswap:  down, hotswap (chip power cycle), up.
	 */
	bcma_core_pci_irq_ctl(&wlc_hw->d11core->bus->drv_pci[0], wlc_hw->d11core,
			      true);

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

static int brcms_b_up_finish(struct brcms_hardware *wlc_hw)
{
	wlc_hw->up = true;
	wlc_phy_hw_state_upd(wlc_hw->band->pi, true);

	/* FULLY enable dynamic power control and d11 core interrupt */
	brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_DYNAMIC);
	brcms_intrson(wlc_hw->wlc->wl);
	return 0;
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

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		brcms_b_write_shm(wlc->hw, M_AC_TXLMT_ADDR(ac),
				  wlc->wme_retries[ac]);
}

/* make interface operational */
int brcms_c_up(struct brcms_c_info *wlc)
{
	struct ieee80211_channel *ch;

	brcms_dbg_info(wlc->hw->d11core, "wl%d\n", wlc->pub->unit);

	/* HW is turned off so don't try to access it */
	if (wlc->pub->hw_off || brcms_deviceremoved(wlc))
		return -ENOMEDIUM;

	if (!wlc->pub->hw_up) {
		brcms_b_hw_up(wlc->hw);
		wlc->pub->hw_up = true;
	}

	if ((wlc->pub->boardflags & BFL_FEM)
	    && (ai_get_chip_id(wlc->hw->sih) == BCMA_CHIP_ID_BCM4313)) {
		if (wlc->pub->boardrev >= 0x1250
		    && (wlc->pub->boardflags & BFL_FEM_BT))
			brcms_b_mhf(wlc->hw, MHF5, MHF5_4313_GPIOCTRL,
				MHF5_4313_GPIOCTRL, BRCM_BAND_ALL);
		else
			brcms_b_mhf(wlc->hw, MHF4, MHF4_EXTPA_ENABLE,
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
				struct brcms_bss_cfg *bsscfg = wlc->bsscfg;
				mboolset(wlc->pub->radio_disabled,
					 WL_RADIO_HW_DISABLE);

				if (bsscfg->enable && bsscfg->BSS)
					brcms_err(wlc->hw->d11core,
						  "wl%d: up: rfdisable -> "
						  "bsscfg_disable()\n",
						   wlc->pub->unit);
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
	brcms_b_mhf(wlc->hw, MHF1, MHF1_EDCF, MHF1_EDCF, BRCM_BAND_ALL);

	brcms_init(wlc->wl);
	wlc->pub->up = true;

	if (wlc->bandinit_pending) {
		ch = wlc->pub->ieee_hw->conf.channel;
		brcms_c_suspend_mac_and_wait(wlc);
		brcms_c_set_chanspec(wlc, ch20mhz_chspec(ch->hw_value));
		wlc->bandinit_pending = false;
		brcms_c_enable_mac(wlc);
	}

	brcms_b_up_finish(wlc->hw);

	/* Program the TX wme params with the current settings */
	brcms_c_wme_retries_write(wlc);

	/* start one second watchdog timer */
	brcms_add_timer(wlc->wdtimer, TIMER_INTERVAL_WATCHDOG, true);
	wlc->WDarmed = true;

	/* ensure antenna config is up to date */
	brcms_c_stf_phy_txant_upd(wlc);
	/* ensure LDPC config is in sync */
	brcms_c_ht_update_ldpc(wlc, wlc->stf->ldpc);

	return 0;
}

static uint brcms_c_down_del_timer(struct brcms_c_info *wlc)
{
	uint callbacks = 0;

	return callbacks;
}

static int brcms_b_bmac_down_prep(struct brcms_hardware *wlc_hw)
{
	bool dev_gone;
	uint callbacks = 0;

	if (!wlc_hw->up)
		return callbacks;

	dev_gone = brcms_deviceremoved(wlc_hw->wlc);

	/* disable interrupts */
	if (dev_gone)
		wlc_hw->wlc->macintmask = 0;
	else {
		/* now disable interrupts */
		brcms_intrsoff(wlc_hw->wlc->wl);

		/* ensure we're running on the pll clock again */
		brcms_b_clkctl_clk(wlc_hw, BCMA_CLKMODE_FAST);
	}
	/* down phy at the last of this stage */
	callbacks += wlc_phy_down(wlc_hw->band->pi);

	return callbacks;
}

static int brcms_b_down_finish(struct brcms_hardware *wlc_hw)
{
	uint callbacks = 0;
	bool dev_gone;

	if (!wlc_hw->up)
		return callbacks;

	wlc_hw->up = false;
	wlc_phy_hw_state_upd(wlc_hw->band->pi, false);

	dev_gone = brcms_deviceremoved(wlc_hw->wlc);

	if (dev_gone) {
		wlc_hw->sbclk = false;
		wlc_hw->clk = false;
		wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, false);

		/* reclaim any posted packets */
		brcms_c_flushqueues(wlc_hw->wlc);
	} else {

		/* Reset and disable the core */
		if (bcma_core_is_enabled(wlc_hw->d11core)) {
			if (bcma_read32(wlc_hw->d11core,
					D11REGOFFS(maccontrol)) & MCTL_EN_MAC)
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

	brcms_dbg_info(wlc->hw->d11core, "wl%d\n", wlc->pub->unit);

	/* check if we are already in the going down path */
	if (wlc->going_down) {
		brcms_err(wlc->hw->d11core,
			  "wl%d: %s: Driver going down so return\n",
			  wlc->pub->unit, __func__);
		return 0;
	}
	if (!wlc->pub->up)
		return callbacks;

	wlc->going_down = true;

	callbacks += brcms_b_bmac_down_prep(wlc->hw);

	dev_gone = brcms_deviceremoved(wlc);

	/* Call any registered down handlers */
	for (i = 0; i < BRCMS_MAXMODULES; i++) {
		if (wlc->modulecb[i].down_fn)
			callbacks +=
			    wlc->modulecb[i].down_fn(wlc->modulecb[i].hdl);
	}

	/* cancel the watchdog timer */
	if (wlc->WDarmed) {
		if (!brcms_del_timer(wlc->wdtimer))
			callbacks++;
		wlc->WDarmed = false;
	}
	/* cancel all other timers */
	callbacks += brcms_c_down_del_timer(wlc);

	wlc->pub->up = false;

	wlc_phy_mute_upd(wlc->band->pi, false, PHY_MUTE_ALL);

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
	if ((wlc->pub->_n_enab & SUPPORT_11N) && gmode == GMODE_LEGACY_B)
		return -ENOTSUPP;

	/* verify that we are dealing with 2G band and grab the band pointer */
	if (wlc->band->bandtype == BRCM_BAND_2G)
		band = wlc->band;
	else if ((wlc->pub->_nbands > 1) &&
		 (wlc->bandstate[OTHERBANDUNIT(wlc)]->bandtype == BRCM_BAND_2G))
		band = wlc->bandstate[OTHERBANDUNIT(wlc)];
	else
		return -EINVAL;

	/* update configuration value */
	if (config)
		brcms_c_protection_upd(wlc, BRCMS_PROT_G_USER, gmode);

	/* Clear rateset override */
	memset(&rs, 0, sizeof(struct brcms_c_rateset));

	switch (gmode) {
	case GMODE_LEGACY_B:
		shortslot = BRCMS_SHORTSLOT_OFF;
		brcms_c_rateset_copy(&gphy_legacy_rates, &rs);

		break;

	case GMODE_LRS:
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
		shortslot = BRCMS_SHORTSLOT_ON;
		shortslot_restrict = true;
		ofdm_basic = true;
		preamble = BRCMS_PLCP_SHORT;
		preamble_restrict = true;
		break;

	default:
		/* Error */
		brcms_err(wlc->hw->d11core, "wl%d: %s: invalid gmode %d\n",
			  wlc->pub->unit, __func__, gmode);
		return -ENOTSUPP;
	}

	band->gmode = gmode;

	wlc->shortslot_override = shortslot;

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

int brcms_c_set_nmode(struct brcms_c_info *wlc)
{
	uint i;
	s32 nmode = AUTO;

	if (wlc->stf->txstreams == WL_11N_3x3)
		nmode = WL_11N_3x3;
	else
		nmode = WL_11N_2x2;

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
	for (i = 0; i < wlc->pub->_nbands; i++)
		memcpy(wlc->bandstate[i]->hw_rateset.mcs,
		       wlc->default_bss->rateset.mcs, MCSSET_LEN);

	return 0;
}

static int
brcms_c_set_internal_rateset(struct brcms_c_info *wlc,
			     struct brcms_c_rateset *rs_arg)
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
	if (brcms_is_mband_unlocked(wlc)) {
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

static void brcms_c_ofdm_rateset_war(struct brcms_c_info *wlc)
{
	u8 r;
	bool war = false;

	if (wlc->bsscfg->associated)
		r = wlc->bsscfg->current_bss->rateset.rates[0];
	else
		r = wlc->default_bss->rateset.rates[0];

	wlc_phy_ofdm_rateset_war(wlc->band->pi, war);
}

int brcms_c_set_channel(struct brcms_c_info *wlc, u16 channel)
{
	u16 chspec = ch20mhz_chspec(channel);

	if (channel < 0 || channel > MAXCHANNEL)
		return -EINVAL;

	if (!brcms_c_valid_chanspec_db(wlc->cmi, chspec))
		return -EINVAL;


	if (!wlc->pub->up && brcms_is_mband_unlocked(wlc)) {
		if (wlc->band->bandunit != chspec_bandunit(chspec))
			wlc->bandinit_pending = true;
		else
			wlc->bandinit_pending = false;
	}

	wlc->default_bss->chanspec = chspec;
	/* brcms_c_BSSinit() will sanitize the rateset before
	 * using it.. */
	if (wlc->pub->up && (wlc_phy_chanspec_get(wlc->band->pi) != chspec)) {
		brcms_c_set_home_chanspec(wlc, chspec);
		brcms_c_suspend_mac_and_wait(wlc);
		brcms_c_set_chanspec(wlc, chspec);
		brcms_c_enable_mac(wlc);
	}
	return 0;
}

int brcms_c_set_rate_limit(struct brcms_c_info *wlc, u16 srl, u16 lrl)
{
	int ac;

	if (srl < 1 || srl > RETRY_SHORT_MAX ||
	    lrl < 1 || lrl > RETRY_SHORT_MAX)
		return -EINVAL;

	wlc->SRL = srl;
	wlc->LRL = lrl;

	brcms_b_retrylimit_upd(wlc->hw, wlc->SRL, wlc->LRL);

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		wlc->wme_retries[ac] =	SFIELD(wlc->wme_retries[ac],
					       EDCF_SHORT,  wlc->SRL);
		wlc->wme_retries[ac] =	SFIELD(wlc->wme_retries[ac],
					       EDCF_LONG, wlc->LRL);
	}
	brcms_c_wme_retries_write(wlc);

	return 0;
}

void brcms_c_get_current_rateset(struct brcms_c_info *wlc,
				 struct brcm_rateset *currs)
{
	struct brcms_c_rateset *rs;

	if (wlc->pub->associated)
		rs = &wlc->bsscfg->current_bss->rateset;
	else
		rs = &wlc->default_bss->rateset;

	/* Copy only legacy rateset section */
	currs->count = rs->count;
	memcpy(&currs->rates, &rs->rates, rs->count);
}

int brcms_c_set_rateset(struct brcms_c_info *wlc, struct brcm_rateset *rs)
{
	struct brcms_c_rateset internal_rs;
	int bcmerror;

	if (rs->count > BRCMS_NUMRATES)
		return -ENOBUFS;

	memset(&internal_rs, 0, sizeof(struct brcms_c_rateset));

	/* Copy only legacy rateset section */
	internal_rs.count = rs->count;
	memcpy(&internal_rs.rates, &rs->rates, internal_rs.count);

	/* merge rateset coming in with the current mcsset */
	if (wlc->pub->_n_enab & SUPPORT_11N) {
		struct brcms_bss_info *mcsset_bss;
		if (wlc->bsscfg->associated)
			mcsset_bss = wlc->bsscfg->current_bss;
		else
			mcsset_bss = wlc->default_bss;
		memcpy(internal_rs.mcs, &mcsset_bss->rateset.mcs[0],
		       MCSSET_LEN);
	}

	bcmerror = brcms_c_set_internal_rateset(wlc, &internal_rs);
	if (!bcmerror)
		brcms_c_ofdm_rateset_war(wlc);

	return bcmerror;
}

int brcms_c_set_beacon_period(struct brcms_c_info *wlc, u16 period)
{
	if (period < DOT11_MIN_BEACON_PERIOD ||
	    period > DOT11_MAX_BEACON_PERIOD)
		return -EINVAL;

	wlc->default_bss->beacon_period = period;
	return 0;
}

u16 brcms_c_get_phy_type(struct brcms_c_info *wlc, int phyidx)
{
	return wlc->band->phytype;
}

void brcms_c_set_shortslot_override(struct brcms_c_info *wlc, s8 sslot_override)
{
	wlc->shortslot_override = sslot_override;

	/*
	 * shortslot is an 11g feature, so no more work if we are
	 * currently on the 5G band
	 */
	if (wlc->band->bandtype == BRCM_BAND_5G)
		return;

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

void brcms_c_print_txstatus(struct tx_status *txs)
{
	pr_debug("\ntxpkt (MPDU) Complete\n");

	pr_debug("FrameID: %04x   TxStatus: %04x\n", txs->frameid, txs->status);

	pr_debug("[15:12]  %d  frame attempts\n",
		  (txs->status & TX_STATUS_FRM_RTX_MASK) >>
		 TX_STATUS_FRM_RTX_SHIFT);
	pr_debug(" [11:8]  %d  rts attempts\n",
		 (txs->status & TX_STATUS_RTS_RTX_MASK) >>
		 TX_STATUS_RTS_RTX_SHIFT);
	pr_debug("    [7]  %d  PM mode indicated\n",
		 txs->status & TX_STATUS_PMINDCTD ? 1 : 0);
	pr_debug("    [6]  %d  intermediate status\n",
		 txs->status & TX_STATUS_INTERMEDIATE ? 1 : 0);
	pr_debug("    [5]  %d  AMPDU\n",
		 txs->status & TX_STATUS_AMPDU ? 1 : 0);
	pr_debug("  [4:2]  %d  Frame Suppressed Reason (%s)\n",
		 (txs->status & TX_STATUS_SUPR_MASK) >> TX_STATUS_SUPR_SHIFT,
		 (const char *[]) {
			"None",
			"PMQ Entry",
			"Flush request",
			"Previous frag failure",
			"Channel mismatch",
			"Lifetime Expiry",
			"Underflow"
		 } [(txs->status & TX_STATUS_SUPR_MASK) >>
		    TX_STATUS_SUPR_SHIFT]);
	pr_debug("    [1]  %d  acked\n",
		 txs->status & TX_STATUS_ACK_RCV ? 1 : 0);

	pr_debug("LastTxTime: %04x Seq: %04x PHYTxStatus: %04x RxAckRSSI: %04x RxAckSQ: %04x\n",
		 txs->lasttxtime, txs->sequence, txs->phyerr,
		 (txs->ackphyrxsh & PRXS1_JSSI_MASK) >> PRXS1_JSSI_SHIFT,
		 (txs->ackphyrxsh & PRXS1_SQ_MASK) >> PRXS1_SQ_SHIFT);
}

static bool brcms_c_chipmatch_pci(struct bcma_device *core)
{
	struct pci_dev *pcidev = core->bus->host_pci;
	u16 vendor = pcidev->vendor;
	u16 device = pcidev->device;

	if (vendor != PCI_VENDOR_ID_BROADCOM) {
		pr_err("unknown vendor id %04x\n", vendor);
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

	pr_err("unknown device id %04x\n", device);
	return false;
}

static bool brcms_c_chipmatch_soc(struct bcma_device *core)
{
	struct bcma_chipinfo *chipinfo = &core->bus->chipinfo;

	if (chipinfo->id == BCMA_CHIP_ID_BCM4716)
		return true;

	pr_err("unknown chip id %04x\n", chipinfo->id);
	return false;
}

bool brcms_c_chipmatch(struct bcma_device *core)
{
	switch (core->bus->hosttype) {
	case BCMA_HOSTTYPE_PCI:
		return brcms_c_chipmatch_pci(core);
	case BCMA_HOSTTYPE_SOC:
		return brcms_c_chipmatch_soc(core);
	default:
		pr_err("unknown host type: %i\n", core->bus->hosttype);
		return false;
	}
}

#if defined(DEBUG)
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

	/* add plcp header along with txh descriptor */
	brcmu_dbg_hex_dump(txh, sizeof(struct d11txh) + 48,
			   "Raw TxDesc + plcp header:\n");

	pr_debug("TxCtlLow: %04x ", mtcl);
	pr_debug("TxCtlHigh: %04x ", mtch);
	pr_debug("FC: %04x ", mfc);
	pr_debug("FES Time: %04x\n", tfest);
	pr_debug("PhyCtl: %04x%s ", ptcw,
	       (ptcw & PHY_TXC_SHORT_HDR) ? " short" : "");
	pr_debug("PhyCtl_1: %04x ", ptcw_1);
	pr_debug("PhyCtl_1_Fbr: %04x\n", ptcw_1_Fbr);
	pr_debug("PhyCtl_1_Rts: %04x ", ptcw_1_Rts);
	pr_debug("PhyCtl_1_Fbr_Rts: %04x\n", ptcw_1_FbrRts);
	pr_debug("MainRates: %04x ", mainrates);
	pr_debug("XtraFrameTypes: %04x ", xtraft);
	pr_debug("\n");

	print_hex_dump_bytes("SecIV:", DUMP_PREFIX_OFFSET, iv, sizeof(txh->IV));
	print_hex_dump_bytes("RA:", DUMP_PREFIX_OFFSET,
			     ra, sizeof(txh->TxFrameRA));

	pr_debug("Fb FES Time: %04x ", tfestfb);
	print_hex_dump_bytes("Fb RTS PLCP:", DUMP_PREFIX_OFFSET,
			     rtspfb, sizeof(txh->RTSPLCPFallback));
	pr_debug("RTS DUR: %04x ", rtsdfb);
	print_hex_dump_bytes("PLCP:", DUMP_PREFIX_OFFSET,
			     fragpfb, sizeof(txh->FragPLCPFallback));
	pr_debug("DUR: %04x", fragdfb);
	pr_debug("\n");

	pr_debug("MModeLen: %04x ", mmodelen);
	pr_debug("MModeFbrLen: %04x\n", mmodefbrlen);

	pr_debug("FrameID:     %04x\n", tfid);
	pr_debug("TxStatus:    %04x\n", txs);

	pr_debug("MaxNumMpdu:  %04x\n", mnmpdu);
	pr_debug("MaxAggbyte:  %04x\n", mabyte);
	pr_debug("MaxAggbyte_fb:  %04x\n", mabyte_f);
	pr_debug("MinByte:     %04x\n", mmbyte);

	print_hex_dump_bytes("RTS PLCP:", DUMP_PREFIX_OFFSET,
			     rtsph, sizeof(txh->RTSPhyHeader));
	print_hex_dump_bytes("RTS Frame:", DUMP_PREFIX_OFFSET,
			     (u8 *)&rts, sizeof(txh->rts_frame));
	pr_debug("\n");
}
#endif				/* defined(DEBUG) */

#if defined(DEBUG)
static int
brcms_c_format_flags(const struct brcms_c_bit_desc *bd, u32 flags, char *buf,
		     int len)
{
	int i;
	char *p = buf;
	char hexstr[16];
	int slen = 0, nlen = 0;
	u32 bit;
	const char *name;

	if (len < 2 || !buf)
		return 0;

	buf[0] = '\0';

	for (i = 0; flags != 0; i++) {
		bit = bd[i].bit;
		name = bd[i].name;
		if (bit == 0 && flags != 0) {
			/* print any unnamed bits */
			snprintf(hexstr, 16, "0x%X", flags);
			name = hexstr;
			flags = 0;	/* exit loop */
		} else if ((flags & bit) == 0)
			continue;
		flags &= ~bit;
		nlen = strlen(name);
		slen += nlen;
		/* count btwn flag space */
		if (flags != 0)
			slen += 1;
		/* need NULL char as well */
		if (len <= slen)
			break;
		/* copy NULL char but don't count it */
		strncpy(p, name, nlen + 1);
		p += nlen;
		/* copy btwn flag space and NULL char */
		if (flags != 0)
			p += snprintf(p, 2, " ");
		len -= slen;
	}

	/* indicate the str was too short */
	if (flags != 0) {
		if (len < 2)
			p -= 2 - len;	/* overwrite last char */
		p += snprintf(p, 2, ">");
	}

	return (int)(p - buf);
}
#endif				/* defined(DEBUG) */

#if defined(DEBUG)
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
	static const struct brcms_c_bit_desc macstat_flags[] = {
		{RXS_FCSERR, "FCSErr"},
		{RXS_RESPFRAMETX, "Reply"},
		{RXS_PBPRES, "PADDING"},
		{RXS_DECATMPT, "DeCr"},
		{RXS_DECERR, "DeCrErr"},
		{RXS_BCNSENT, "Bcn"},
		{0, NULL}
	};

	brcmu_dbg_hex_dump(rxh, sizeof(struct d11rxhdr), "Raw RxDesc:\n");

	brcms_c_format_flags(macstat_flags, macstatus1, flagstr, 64);

	snprintf(lenbuf, sizeof(lenbuf), "0x%x", len);

	pr_debug("RxFrameSize:     %6s (%d)%s\n", lenbuf, len,
	       (rxh->PhyRxStatus_0 & PRXS0_SHORTH) ? " short preamble" : "");
	pr_debug("RxPHYStatus:     %04x %04x %04x %04x\n",
	       phystatus_0, phystatus_1, phystatus_2, phystatus_3);
	pr_debug("RxMACStatus:     %x %s\n", macstatus1, flagstr);
	pr_debug("RXMACaggtype:    %x\n",
	       (macstatus2 & RXS_AGGTYPE_MASK));
	pr_debug("RxTSFTime:       %04x\n", rxh->RxTSFTime);
}
#endif				/* defined(DEBUG) */

u16 brcms_b_rate_shm_offset(struct brcms_hardware *wlc_hw, u8 rate)
{
	u16 table_ptr;
	u8 phy_rate, index;

	/* get the phy specific rate encoding for the PLCP SIGNAL field */
	if (is_ofdm_rate(rate))
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
	rspec = brcms_basic_rate(wlc, rspec);
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
	rspec = brcms_basic_rate(wlc, rspec);
	/* BA len == 32 == 16(ctl hdr) + 4(ba len) + 8(bitmap) + 4(fcs) */
	return brcms_c_calc_frame_time(wlc, rspec, preamble_type,
				   (DOT11_BA_LEN + DOT11_BA_BITMAP_LEN +
				    FCS_LEN));
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

	sifs = get_sifs(wlc->band);

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

/* The opposite of brcms_c_calc_frame_time */
static uint
brcms_c_calc_frame_len(struct brcms_c_info *wlc, u32 ratespec,
		   u8 preamble_type, uint dur)
{
	uint nsyms, mac_len, Ndps, kNdps;
	uint rate = rspec2rate(ratespec);

	BCMMSG(wlc->wiphy, "wl%d: rspec 0x%x, preamble_type %d, dur %d\n",
		 wlc->pub->unit, ratespec, preamble_type, dur);

	if (is_mcs_rate(ratespec)) {
		uint mcs = ratespec & RSPEC_RATE_MASK;
		int tot_streams = mcs_2_txstreams(mcs) + rspec_stc(ratespec);
		dur -= PREN_PREAMBLE + (tot_streams * PREN_PREAMBLE_EXT);
		/* payload calculation matches that of regular ofdm */
		if (wlc->band->bandtype == BRCM_BAND_2G)
			dur -= DOT11_OFDM_SIGNAL_EXTENSION;
		/* kNdbps = kbps * 4 */
		kNdps =	mcs_2_rate(mcs, rspec_is40mhz(ratespec),
				   rspec_issgi(ratespec)) * 4;
		nsyms = dur / APHY_SYMBOL_TIME;
		mac_len =
		    ((nsyms * kNdps) -
		     ((APHY_SERVICE_NBITS + APHY_TAIL_NBITS) * 1000)) / 8000;
	} else if (is_ofdm_rate(ratespec)) {
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

/*
 * Return true if the specified rate is supported by the specified band.
 * BRCM_BAND_AUTO indicates the current band.
 */
static bool brcms_c_valid_rate(struct brcms_c_info *wlc, u32 rspec, int band,
		    bool verbose)
{
	struct brcms_c_rateset *hw_rateset;
	uint i;

	if ((band == BRCM_BAND_AUTO) || (band == wlc->band->bandtype))
		hw_rateset = &wlc->band->hw_rateset;
	else if (wlc->pub->_nbands > 1)
		hw_rateset = &wlc->bandstate[OTHERBANDUNIT(wlc)]->hw_rateset;
	else
		/* other band specified and we are a single band device */
		return false;

	/* check if this is a mimo rate */
	if (is_mcs_rate(rspec)) {
		if ((rspec & RSPEC_RATE_MASK) >= MCS_TABLE_SIZE)
			goto error;

		return isset(hw_rateset->mcs, (rspec & RSPEC_RATE_MASK));
	}

	for (i = 0; i < hw_rateset->count; i++)
		if (hw_rateset->rates[i] == rspec2rate(rspec))
			return true;
 error:
	if (verbose)
		brcms_err(wlc->hw->d11core, "wl%d: valid_rate: rate spec 0x%x "
			  "not in hw_rateset\n", wlc->pub->unit, rspec);

	return false;
}

static u32
mac80211_wlc_set_nrate(struct brcms_c_info *wlc, struct brcms_band *cur_band,
		       u32 int_val)
{
	struct bcma_device *core = wlc->hw->d11core;
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
	if ((wlc->pub->_n_enab & SUPPORT_11N) && ismcs) {
		/* mcs only allowed when nmode */
		if (stf > PHY_TXC1_MODE_SDM) {
			brcms_err(core, "wl%d: %s: Invalid stf\n",
				  wlc->pub->unit, __func__);
			bcmerror = -EINVAL;
			goto done;
		}

		/* mcs 32 is a special case, DUP mode 40 only */
		if (rate == 32) {
			if (!CHSPEC_IS40(wlc->home_chanspec) ||
			    ((stf != PHY_TXC1_MODE_SISO)
			     && (stf != PHY_TXC1_MODE_CDD))) {
				brcms_err(core, "wl%d: %s: Invalid mcs 32\n",
					  wlc->pub->unit, __func__);
				bcmerror = -EINVAL;
				goto done;
			}
			/* mcs > 7 must use stf SDM */
		} else if (rate > HIGHEST_SINGLE_STREAM_MCS) {
			/* mcs > 7 must use stf SDM */
			if (stf != PHY_TXC1_MODE_SDM) {
				BCMMSG(wlc->wiphy, "wl%d: enabling "
				       "SDM mode for mcs %d\n",
				       wlc->pub->unit, rate);
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
				brcms_err(core, "wl%d: %s: Invalid STBC\n",
					  wlc->pub->unit, __func__);
				bcmerror = -EINVAL;
				goto done;
			}
		}
	} else if (is_ofdm_rate(rate)) {
		if ((stf != PHY_TXC1_MODE_CDD) && (stf != PHY_TXC1_MODE_SISO)) {
			brcms_err(core, "wl%d: %s: Invalid OFDM\n",
				  wlc->pub->unit, __func__);
			bcmerror = -EINVAL;
			goto done;
		}
	} else if (is_cck_rate(rate)) {
		if ((cur_band->bandtype != BRCM_BAND_2G)
		    || (stf != PHY_TXC1_MODE_SISO)) {
			brcms_err(core, "wl%d: %s: Invalid CCK\n",
				  wlc->pub->unit, __func__);
			bcmerror = -EINVAL;
			goto done;
		}
	} else {
		brcms_err(core, "wl%d: %s: Unknown rate type\n",
			  wlc->pub->unit, __func__);
		bcmerror = -EINVAL;
		goto done;
	}
	/* make sure multiple antennae are available for non-siso rates */
	if ((stf != PHY_TXC1_MODE_SISO) && (wlc->stf->txstreams == 1)) {
		brcms_err(core, "wl%d: %s: SISO antenna but !SISO "
			  "request\n", wlc->pub->unit, __func__);
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
		brcms_err(wlc->hw->d11core,
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
static void brcms_c_compute_mimo_plcp(u32 rspec, uint length, u8 *plcp)
{
	u8 mcs = (u8) (rspec & RSPEC_RATE_MASK);
	plcp[0] = mcs;
	if (rspec_is40mhz(rspec) || (mcs == 32))
		plcp[0] |= MIMO_PLCP_40MHZ;
	BRCMS_SET_MIMO_PLCP_LEN(plcp, length);
	plcp[3] = rspec_mimoplcp3(rspec); /* rspec already holds this byte */
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
	int rate = rspec2rate(rspec);

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
}

/* Rate: 802.11 rate code, length: PSDU length in octets */
static void brcms_c_compute_cck_plcp(struct brcms_c_info *wlc, u32 rspec,
				 uint length, u8 *plcp)
{
	int rate = rspec2rate(rspec);

	brcms_c_cck_plcp_set(wlc, rate, length, plcp);
}

static void
brcms_c_compute_plcp(struct brcms_c_info *wlc, u32 rspec,
		     uint length, u8 *plcp)
{
	if (is_mcs_rate(rspec))
		brcms_c_compute_mimo_plcp(rspec, length, plcp);
	else if (is_ofdm_rate(rspec))
		brcms_c_compute_ofdm_plcp(rspec, length, plcp);
	else
		brcms_c_compute_cck_plcp(wlc, rspec, length, plcp);
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

	sifs = get_sifs(wlc->band);

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

static u16 brcms_c_phytxctl1_calc(struct brcms_c_info *wlc, u32 rspec)
{
	u16 phyctl1 = 0;
	u16 bw;

	if (BRCMS_ISLCNPHY(wlc->band)) {
		bw = PHY_TXC1_BW_20MHZ;
	} else {
		bw = rspec_get_bw(rspec);
		/* 10Mhz is not supported yet */
		if (bw < PHY_TXC1_BW_20MHZ) {
			brcms_err(wlc->hw->d11core, "phytxctl1_calc: bw %d is "
				  "not supported yet, set to 20L\n", bw);
			bw = PHY_TXC1_BW_20MHZ;
		}
	}

	if (is_mcs_rate(rspec)) {
		uint mcs = rspec & RSPEC_RATE_MASK;

		/* bw, stf, coding-type is part of rspec_phytxbyte2 returns */
		phyctl1 = rspec_phytxbyte2(rspec);
		/* set the upper byte of phyctl1 */
		phyctl1 |= (mcs_table[mcs].tx_phy_ctl3 << 8);
	} else if (is_cck_rate(rspec) && !BRCMS_ISLCNPHY(wlc->band)
		   && !BRCMS_ISSSLPNPHY(wlc->band)) {
		/*
		 * In CCK mode LPPHY overloads OFDM Modulation bits with CCK
		 * Data Rate. Eventually MIMOPHY would also be converted to
		 * this format
		 */
		/* 0 = 1Mbps; 1 = 2Mbps; 2 = 5.5Mbps; 3 = 11Mbps */
		phyctl1 = (bw | (rspec_stf(rspec) << PHY_TXC1_MODE_SHIFT));
	} else {		/* legacy OFDM/CCK */
		s16 phycfg;
		/* get the phyctl byte from rate phycfg table */
		phycfg = brcms_c_rate_legacy_phyctl(rspec2rate(rspec));
		if (phycfg == -1) {
			brcms_err(wlc->hw->d11core, "phytxctl1_calc: wrong "
				  "legacy OFDM/CCK rate\n");
			phycfg = 0;
		}
		/* set the upper byte of phyctl1 */
		phyctl1 =
		    (bw | (phycfg << 8) |
		     (rspec_stf(rspec) << PHY_TXC1_MODE_SHIFT));
	}
	return phyctl1;
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
		     uint nfrags, uint queue, uint next_frag_len)
{
	struct ieee80211_hdr *h;
	struct d11txh *txh;
	u8 *plcp, plcp_fallback[D11_PHY_HDR_LEN];
	int len, phylen, rts_phylen;
	u16 mch, phyctl, xfts, mainrates;
	u16 seq = 0, mcl = 0, status = 0, frameid = 0;
	u32 rspec[2] = { BRCM_RATE_1M, BRCM_RATE_1M };
	u32 rts_rspec[2] = { BRCM_RATE_1M, BRCM_RATE_1M };
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
	bool is_mcs;
	u16 mimo_txbw;
	u8 mimo_preamble_type;

	/* locate 802.11 MAC header */
	h = (struct ieee80211_hdr *)(p->data);
	qos = ieee80211_is_data_qos(h->frame_control);

	/* compute length of frame in bytes for use in PLCP computations */
	len = p->len;
	phylen = len + FCS_LEN;

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
			brcms_err(wlc->hw->d11core,
				  "wl%d: %s: ASSERT queue == TX_BCMC!\n",
				  wlc->pub->unit, __func__);
			frameid = bcmc_fid_generate(wlc, NULL, txh);
		} else {
			/* Increment the counter for first fragment */
			if (tx_info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
				scb->seqnum[p->priority]++;

			/* extract fragment number from frame first */
			seq = le16_to_cpu(h->seq_ctrl) & FRAGNUM_MASK;
			seq |= (scb->seqnum[p->priority] << SEQNUM_SHIFT);
			h->seq_ctrl = cpu_to_le16(seq);

			frameid = ((seq << TXFID_SEQ_SHIFT) & TXFID_SEQ_MASK) |
			    (queue & TXFID_QUEUE_MASK);
		}
	}
	frameid |= queue & TXFID_QUEUE_MASK;

	/* set the ignpmq bit for all pkts tx'd in PS mode and for beacons */
	if (ieee80211_is_beacon(h->frame_control))
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
		is_mcs = txrate[k]->flags & IEEE80211_TX_RC_MCS ? true : false;
		if (!is_mcs) {
			if ((txrate[k]->idx >= 0)
			    && (txrate[k]->idx <
				hw->wiphy->bands[tx_info->band]->n_bitrates)) {
				rspec[k] =
				    hw->wiphy->bands[tx_info->band]->
				    bitrates[txrate[k]->idx].hw_value;
				short_preamble[k] =
				    txrate[k]->
				    flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE ?
				    true : false;
			} else {
				rspec[k] = BRCM_RATE_1M;
			}
		} else {
			rspec[k] = mac80211_wlc_set_nrate(wlc, wlc->band,
					NRATE_MCS_INUSE | txrate[k]->idx);
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


		/*
		 * (1) RATE:
		 *   determine and validate primary rate
		 *   and fallback rates
		 */
		if (!rspec_active(rspec[k])) {
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

	if (wlc->pub->_n_enab & SUPPORT_11N) {
		for (k = 0; k < hw->max_rates; k++) {
			/*
			 * apply siso/cdd to single stream mcs's or ofdm
			 * if rspec is auto selected
			 */
			if (((is_mcs_rate(rspec[k]) &&
			      is_single_stream(rspec[k] & RSPEC_RATE_MASK)) ||
			     is_ofdm_rate(rspec[k]))
			    && ((rspec[k] & RSPEC_OVERRIDE_MCS_ONLY)
				|| !(rspec[k] & RSPEC_OVERRIDE))) {
				rspec[k] &= ~(RSPEC_STF_MASK | RSPEC_STC_MASK);

				/* For SISO MCS use STBC if possible */
				if (is_mcs_rate(rspec[k])
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
			if (brcms_chspec_bw(wlc->chanspec) == BRCMS_40_MHZ) {
				/* default txbw is 20in40 SB */
				mimo_ctlchbw = mimo_txbw =
				   CHSPEC_SB_UPPER(wlc_phy_chanspec_get(
								 wlc->band->pi))
				   ? PHY_TXC1_BW_20MHZ_UP : PHY_TXC1_BW_20MHZ;

				if (is_mcs_rate(rspec[k])) {
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
				} else if (is_ofdm_rate(rspec[k])) {
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
			if ((k == 0) || ((k > 0) && is_mcs_rate(rspec[k])))
				rspec[k] |= (mimo_txbw << RSPEC_BW_SHIFT);
			else
				rspec[k] |= (mimo_ctlchbw << RSPEC_BW_SHIFT);

			/* Disable short GI, not supported yet */
			rspec[k] &= ~RSPEC_SHORT_GI;

			mimo_preamble_type = BRCMS_MM_PREAMBLE;
			if (txrate[k]->flags & IEEE80211_TX_RC_GREEN_FIELD)
				mimo_preamble_type = BRCMS_GF_PREAMBLE;

			if ((txrate[k]->flags & IEEE80211_TX_RC_MCS)
			    && (!is_mcs_rate(rspec[k]))) {
				brcms_err(wlc->hw->d11core,
					  "wl%d: %s: IEEE80211_TX_"
					  "RC_MCS != is_mcs_rate(rspec)\n",
					  wlc->pub->unit, __func__);
			}

			if (is_mcs_rate(rspec[k])) {
				preamble_type[k] = mimo_preamble_type;

				/*
				 * if SGI is selected, then forced mm
				 * for single stream
				 */
				if ((rspec[k] & RSPEC_SHORT_GI)
				    && is_single_stream(rspec[k] &
							RSPEC_RATE_MASK))
					preamble_type[k] = BRCMS_MM_PREAMBLE;
			}

			/* should be better conditionalized */
			if (!is_mcs_rate(rspec[0])
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
			if (BRCMS_ISNPHY(wlc->band) && is_ofdm_rate(rspec[k])) {
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
	if (is_cck_rate(rspec[1])) {
		txh->FragPLCPFallback[4] = phylen & 0xff;
		txh->FragPLCPFallback[5] = (phylen & 0xff00) >> 8;
	}

	/* MIMO-RATE: need validation ?? */
	mainrates = is_ofdm_rate(rspec[0]) ?
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

	if (wlc->band->bandtype == BRCM_BAND_5G)
		mcl |= TXC_FREQBAND_5G;

	if (CHSPEC_IS40(wlc_phy_chanspec_get(wlc->band->pi)))
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
		if (rspec2rate(rspec[1]) != BRCM_RATE_1M)
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

		if (!is_ofdm_rate(rts_rspec[0]) &&
		    !((rspec2rate(rts_rspec[0]) == BRCM_RATE_1M) ||
		      (wlc->PLCPHdr_override == BRCMS_PLCP_LONG))) {
			rts_preamble_type[0] = BRCMS_SHORT_PREAMBLE;
			mch |= TXC_PREAMBLE_RTS_MAIN_SHORT;
		}

		if (!is_ofdm_rate(rts_rspec[1]) &&
		    !((rspec2rate(rts_rspec[1]) == BRCM_RATE_1M) ||
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
		mainrates |= (is_ofdm_rate(rts_rspec[0]) ?
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
	if ((tx_info->flags & IEEE80211_TX_CTL_AMPDU) && is_mcs_rate(rspec))
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
	xfts = frametype(rspec[1], wlc->mimoft);
	xfts |= (frametype(rts_rspec[0], wlc->mimoft) << XFTS_RTS_FT_SHIFT);
	xfts |= (frametype(rts_rspec[1], wlc->mimoft) << XFTS_FBRRTS_FT_SHIFT);
	xfts |= CHSPEC_CHANNEL(wlc_phy_chanspec_get(wlc->band->pi)) <<
							     XFTS_CHANNEL_SHIFT;
	txh->XtraFrameTypes = cpu_to_le16(xfts);

	/* PhyTxControlWord */
	phyctl = frametype(rspec[0], wlc->mimoft);
	if ((preamble_type[0] == BRCMS_SHORT_PREAMBLE) ||
	    (preamble_type[0] == BRCMS_GF_PREAMBLE)) {
		if (rspec2rate(rspec[0]) != BRCM_RATE_1M)
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
		if (is_mcs_rate(rspec[0]) &&
		    (preamble_type[0] == BRCMS_MM_PREAMBLE)) {
			u16 mmodelen =
			    brcms_c_calc_lsig_len(wlc, rspec[0], phylen);
			txh->MModeLen = cpu_to_le16(mmodelen);
		}

		if (is_mcs_rate(rspec[1]) &&
		    (preamble_type[1] == BRCMS_MM_PREAMBLE)) {
			u16 mmodefbrlen =
			    brcms_c_calc_lsig_len(wlc, rspec[1], phylen);
			txh->MModeFbrLen = cpu_to_le16(mmodefbrlen);
		}
	}

	ac = skb_get_queue_mapping(p);
	if ((scb->flags & SCB_WMECAP) && qos && wlc->edcf_txop[ac]) {
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
				brcms_err(wlc->hw->d11core,
					  "wl%d: %s txop invalid "
					  "for rate %d\n",
					  wlc->pub->unit, fifo_names[queue],
					  rspec2rate(rspec[0]));
			}

			if (dur > wlc->edcf_txop[ac])
				brcms_err(wlc->hw->d11core,
					  "wl%d: %s: %s txop "
					  "exceeded phylen %d/%d dur %d/%d\n",
					  wlc->pub->unit, __func__,
					  fifo_names[queue],
					  phylen, wlc->fragthresh[queue],
					  dur, wlc->edcf_txop[ac]);
		}
	}

	return 0;
}

static int brcms_c_tx(struct brcms_c_info *wlc, struct sk_buff *skb)
{
	struct dma_pub *dma;
	int fifo, ret = -ENOSPC;
	struct d11txh *txh;
	u16 frameid = INVALIDFID;

	fifo = brcms_ac_to_fifo(skb_get_queue_mapping(skb));
	dma = wlc->hw->di[fifo];
	txh = (struct d11txh *)(skb->data);

	if (dma->txavail == 0) {
		/*
		 * We sometimes get a frame from mac80211 after stopping
		 * the queues. This only ever seems to be a single frame
		 * and is seems likely to be a race. TX_HEADROOM should
		 * ensure that we have enough space to handle these stray
		 * packets, so warn if there isn't. If we're out of space
		 * in the tx ring and the tx queue isn't stopped then
		 * we've really got a bug; warn loudly if that happens.
		 */
		brcms_warn(wlc->hw->d11core,
			   "Received frame for tx with no space in DMA ring\n");
		WARN_ON(!ieee80211_queue_stopped(wlc->pub->ieee_hw,
						 skb_get_queue_mapping(skb)));
		return -ENOSPC;
	}

	/* When a BC/MC frame is being committed to the BCMC fifo
	 * via DMA (NOT PIO), update ucode or BSS info as appropriate.
	 */
	if (fifo == TX_BCMC_FIFO)
		frameid = le16_to_cpu(txh->TxFrameID);

	/* Commit BCMC sequence number in the SHM frame ID location */
	if (frameid != INVALIDFID) {
		/*
		 * To inform the ucode of the last mcast frame posted
		 * so that it can clear moredata bit
		 */
		brcms_b_write_shm(wlc->hw, M_BCMC_FID, frameid);
	}

	ret = brcms_c_txfifo(wlc, fifo, skb);
	/*
	 * The only reason for brcms_c_txfifo to fail is because
	 * there weren't any DMA descriptors, but we've already
	 * checked for that. So if it does fail yell loudly.
	 */
	WARN_ON_ONCE(ret);

	return ret;
}

void brcms_c_sendpkt_mac80211(struct brcms_c_info *wlc, struct sk_buff *sdu,
			      struct ieee80211_hw *hw)
{
	uint fifo;
	struct scb *scb = &wlc->pri_scb;

	fifo = brcms_ac_to_fifo(skb_get_queue_mapping(sdu));
	if (brcms_c_d11hdrs_mac80211(wlc, hw, sdu, scb, 0, 1, fifo, 0))
		return;
	if (brcms_c_tx(wlc, sdu))
		dev_kfree_skb_any(sdu);
}

int
brcms_c_txfifo(struct brcms_c_info *wlc, uint fifo, struct sk_buff *p)
{
	struct dma_pub *dma = wlc->hw->di[fifo];
	int ret;
	u16 queue;

	ret = dma_txfast(wlc, dma, p);
	if (ret	< 0)
		wiphy_err(wlc->wiphy, "txfifo: fatal, toss frames !!!\n");

	/*
	 * Stop queue if DMA ring is full. Reserve some free descriptors,
	 * as we sometimes receive a frame from mac80211 after the queues
	 * are stopped.
	 */
	queue = skb_get_queue_mapping(p);
	if (dma->txavail <= TX_HEADROOM && fifo < TX_BCMC_FIFO &&
	    !ieee80211_queue_stopped(wlc->pub->ieee_hw, queue))
		ieee80211_stop_queue(wlc->pub->ieee_hw, queue);

	return ret;
}

u32
brcms_c_rspec_to_rts_rspec(struct brcms_c_info *wlc, u32 rspec,
			   bool use_rspec, u16 mimo_ctlchbw)
{
	u32 rts_rspec = 0;

	if (use_rspec)
		/* use frame rate as rts rate */
		rts_rspec = rspec;
	else if (wlc->band->gmode && wlc->protection->_g && !is_cck_rate(rspec))
		/* Use 11Mbps as the g protection RTS target rate and fallback.
		 * Use the brcms_basic_rate() lookup to find the best basic rate
		 * under the target in case 11 Mbps is not Basic.
		 * 6 and 9 Mbps are not usually selected by rate selection, but
		 * even if the OFDM rate we are protecting is 6 or 9 Mbps, 11
		 * is more robust.
		 */
		rts_rspec = brcms_basic_rate(wlc, BRCM_RATE_11M);
	else
		/* calculate RTS rate and fallback rate based on the frame rate
		 * RTS must be sent at a basic rate since it is a
		 * control frame, sec 9.6 of 802.11 spec
		 */
		rts_rspec = brcms_basic_rate(wlc, rspec);

	if (BRCMS_PHY_11N_CAP(wlc->band)) {
		/* set rts txbw to correct side band */
		rts_rspec &= ~RSPEC_BW_MASK;

		/*
		 * if rspec/rspec_fallback is 40MHz, then send RTS on both
		 * 20MHz channel (DUP), otherwise send RTS on control channel
		 */
		if (rspec_is40mhz(rspec) && !is_cck_rate(rts_rspec))
			rts_rspec |= (PHY_TXC1_BW_40MHZ_DUP << RSPEC_BW_SHIFT);
		else
			rts_rspec |= (mimo_ctlchbw << RSPEC_BW_SHIFT);

		/* pick siso/cdd as default for ofdm */
		if (is_ofdm_rate(rts_rspec)) {
			rts_rspec &= ~RSPEC_STF_MASK;
			rts_rspec |= (wlc->stf->ss_opmode << RSPEC_STF_SHIFT);
		}
	}
	return rts_rspec;
}

/* Update beacon listen interval in shared memory */
static void brcms_c_bcn_li_upd(struct brcms_c_info *wlc)
{
	/* wake up every DTIM is the default */
	if (wlc->bcn_li_dtim == 1)
		brcms_b_write_shm(wlc->hw, M_BCN_LI, 0);
	else
		brcms_b_write_shm(wlc->hw, M_BCN_LI,
			      (wlc->bcn_li_dtim << 8) | wlc->bcn_li_bcn);
}

static void
brcms_b_read_tsf(struct brcms_hardware *wlc_hw, u32 *tsf_l_ptr,
		  u32 *tsf_h_ptr)
{
	struct bcma_device *core = wlc_hw->d11core;

	/* read the tsf timer low, then high to get an atomic read */
	*tsf_l_ptr = bcma_read32(core, D11REGOFFS(tsf_timerlow));
	*tsf_h_ptr = bcma_read32(core, D11REGOFFS(tsf_timerhigh));
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
				 struct d11rxhdr *rxh)
{
	u32 tsf_h, tsf_l;
	u16 rx_tsf_0_15, rx_tsf_16_31;

	brcms_b_read_tsf(wlc->hw, &tsf_l, &tsf_h);

	rx_tsf_16_31 = (u16)(tsf_l >> 16);
	rx_tsf_0_15 = rxh->RxTSFTime;

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
	int preamble;
	int channel;
	u32 rspec;
	unsigned char *plcp;

	/* fill in TSF and flag its presence */
	rx_status->mactime = brcms_c_recover_tsf64(wlc, rxh);
	rx_status->flag |= RX_FLAG_MACTIME_MPDU;

	channel = BRCMS_CHAN_CHANNEL(rxh->RxChan);

	rx_status->band =
		channel > 14 ? IEEE80211_BAND_5GHZ : IEEE80211_BAND_2GHZ;
	rx_status->freq =
		ieee80211_channel_to_frequency(channel, rx_status->band);

	rx_status->signal = wlc_phy_rssi_compute(wlc->hw->band->pi, rxh);

	/* noise */
	/* qual */
	rx_status->antenna =
		(rxh->PhyRxStatus_0 & PRXS0_RXANT_UPSUBBAND) ? 1 : 0;

	plcp = p->data;

	rspec = brcms_c_compute_rspec(rxh, plcp);
	if (is_mcs_rate(rspec)) {
		rx_status->rate_idx = rspec & RSPEC_RATE_MASK;
		rx_status->flag |= RX_FLAG_HT;
		if (rspec_is40mhz(rspec))
			rx_status->flag |= RX_FLAG_40MHZ;
	} else {
		switch (rspec2rate(rspec)) {
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
			brcms_err(wlc->hw->d11core,
				  "%s: Unknown rate\n", __func__);
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
		if (is_cck_rate(rspec)) {
			if (rxh->PhyRxStatus_0 & PRXS0_SHORTH)
				rx_status->flag |= RX_FLAG_SHORTPRE;
		} else if (is_ofdm_rate(rspec)) {
			rx_status->flag |= RX_FLAG_SHORTPRE;
		} else {
			brcms_err(wlc->hw->d11core, "%s: Unknown modulation\n",
				  __func__);
		}
	}

	if (plcp3_issgi(plcp[3]))
		rx_status->flag |= RX_FLAG_SHORT_GI;

	if (rxh->RxStatus1 & RXS_DECERR) {
		rx_status->flag |= RX_FLAG_FAILED_PLCP_CRC;
		brcms_err(wlc->hw->d11core, "%s:  RX_FLAG_FAILED_PLCP_CRC\n",
			  __func__);
	}
	if (rxh->RxStatus1 & RXS_FCSERR) {
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
		brcms_err(wlc->hw->d11core, "%s:  RX_FLAG_FAILED_FCS_CRC\n",
			  __func__);
	}
}

static void
brcms_c_recvctl(struct brcms_c_info *wlc, struct d11rxhdr *rxh,
		struct sk_buff *p)
{
	int len_mpdu;
	struct ieee80211_rx_status rx_status;
	struct ieee80211_hdr *hdr;

	memset(&rx_status, 0, sizeof(rx_status));
	prep_mac80211_status(wlc, rxh, p, &rx_status);

	/* mac header+body length, exclude CRC and plcp header */
	len_mpdu = p->len - D11_PHY_HDR_LEN - FCS_LEN;
	skb_pull(p, D11_PHY_HDR_LEN);
	__skb_trim(p, len_mpdu);

	/* unmute transmit */
	if (wlc->hw->suspended_fifos) {
		hdr = (struct ieee80211_hdr *)p->data;
		if (ieee80211_is_beacon(hdr->frame_control))
			brcms_b_mute(wlc->hw, false);
	}

	memcpy(IEEE80211_SKB_RXCB(p), &rx_status, sizeof(rx_status));
	ieee80211_rx_irqsafe(wlc->pub->ieee_hw, p);
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
		 wlc->pub->unit, rspec2rate(ratespec), mac_len);

	if (is_mcs_rate(ratespec)) {
		uint mcs = ratespec & RSPEC_RATE_MASK;
		int tot_streams = (mcs_2_txstreams(mcs) + 1) +
				  rspec_stc(ratespec);

		/*
		 * the payload duration calculation matches that
		 * of regular ofdm
		 */
		/* 1000Ndbps = kbps * 4 */
		kNdps = mcs_2_rate(mcs, rspec_is40mhz(ratespec),
				   rspec_issgi(ratespec)) * 4;

		if (rspec_stc(ratespec) == 0)
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

static void
brcms_c_mod_prb_rsp_rate_table(struct brcms_c_info *wlc, uint frame_len)
{
	const struct brcms_c_rateset *rs_dflt;
	struct brcms_c_rateset rs;
	u8 rate;
	u16 entry_ptr;
	u8 plcp[D11_PHY_HDR_LEN];
	u16 dur, sifs;
	uint i;

	sifs = get_sifs(wlc->band);

	rs_dflt = brcms_c_rateset_get_hwrs(wlc);

	brcms_c_rateset_copy(rs_dflt, &rs);
	brcms_c_rateset_mcs_upd(&rs, wlc->stf->txstreams);

	/*
	 * walk the phy rate table and update MAC core SHM
	 * basic rate table entries
	 */
	for (i = 0; i < rs.count; i++) {
		rate = rs.rates[i] & BRCMS_RATE_MASK;

		entry_ptr = brcms_b_rate_shm_offset(wlc->hw, rate);

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
		brcms_b_write_shm(wlc->hw, entry_ptr + M_RT_PRS_PLCP_POS,
			      (u16) (plcp[0] + (plcp[1] << 8)));
		brcms_b_write_shm(wlc->hw, entry_ptr + M_RT_PRS_PLCP_POS + 2,
			      (u16) (plcp[2] + (plcp[3] << 8)));
		brcms_b_write_shm(wlc->hw, entry_ptr + M_RT_PRS_DUR_POS, dur);
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
	if (type == IEEE80211_STYPE_BEACON)
		/* fill in PLCP */
		brcms_c_compute_plcp(wlc, bcn_rspec,
				 (DOT11_MAC_HDR_LEN + body_len + FCS_LEN),
				 (u8 *) plcp);

	/* "Regular" and 16 MBSS but not for 4 MBSS */
	/* Update the phytxctl for the beacon based on the rspec */
	brcms_c_beacon_phytxctl_txant_upd(wlc, bcn_rspec);

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
}

int brcms_c_get_header_len(void)
{
	return TXOFF;
}

/*
 * Update all beacons for the system.
 */
void brcms_c_update_beacon(struct brcms_c_info *wlc)
{
	struct brcms_bss_cfg *bsscfg = wlc->bsscfg;

	if (bsscfg->up && !bsscfg->BSS)
		/* Clear the soft intmask */
		wlc->defmacintmask &= ~MI_BCNTPL;
}

/* Write ssid into shared memory */
static void
brcms_c_shm_ssid_upd(struct brcms_c_info *wlc, struct brcms_bss_cfg *cfg)
{
	u8 *ssidptr = cfg->SSID;
	u16 base = M_SSID;
	u8 ssidbuf[IEEE80211_MAX_SSID_LEN];

	/* padding the ssid with zero and copy it into shm */
	memset(ssidbuf, 0, IEEE80211_MAX_SSID_LEN);
	memcpy(ssidbuf, ssidptr, cfg->SSID_len);

	brcms_c_copyto_shm(wlc, base, ssidbuf, IEEE80211_MAX_SSID_LEN);
	brcms_b_write_shm(wlc->hw, M_SSIDLEN, (u16) cfg->SSID_len);
}

static void
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

	/* create the probe response template */
	brcms_c_bcn_prb_template(wlc, IEEE80211_STYPE_PROBE_RESP, 0,
				 cfg, prb_resp, &len);

	if (suspend)
		brcms_c_suspend_mac_and_wait(wlc);

	/* write the probe response into the template region */
	brcms_b_write_template_ram(wlc->hw, T_PRS_TPL_BASE,
				    (len + 3) & ~3, prb_resp);

	/* write the length of the probe response frame (+PLCP/-FCS) */
	brcms_b_write_shm(wlc->hw, M_PRB_RESP_FRM_LEN, (u16) len);

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
}

void brcms_c_update_probe_resp(struct brcms_c_info *wlc, bool suspend)
{
	struct brcms_bss_cfg *bsscfg = wlc->bsscfg;

	/* update AP or IBSS probe responses */
	if (bsscfg->up && !bsscfg->BSS)
		brcms_c_bss_update_probe_resp(wlc, bsscfg, suspend);
}

int brcms_b_xmtfifo_sz_get(struct brcms_hardware *wlc_hw, uint fifo,
			   uint *blocks)
{
	if (fifo >= NFIFO)
		return -EINVAL;

	*blocks = wlc_hw->xmtfifo_sz[fifo];

	return 0;
}

void
brcms_c_set_addrmatch(struct brcms_c_info *wlc, int match_reg_offset,
		  const u8 *addr)
{
	brcms_b_set_addrmatch(wlc->hw, match_reg_offset, addr);
	if (match_reg_offset == RCM_BSSID_OFFSET)
		memcpy(wlc->bsscfg->BSSID, addr, ETH_ALEN);
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
	wlc->bsscfg->associated = state;
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
	int timeout = 20;
	int i;

	/* Kick DMA to send any pending AMPDU */
	for (i = 0; i < ARRAY_SIZE(wlc->hw->di); i++)
		if (wlc->hw->di[i])
			dma_txflush(wlc->hw->di[i]);

	/* wait for queue and DMA fifos to run dry */
	while (brcms_txpktpendtot(wlc) > 0) {
		brcms_msleep(wlc->wl, 1);

		if (--timeout == 0)
			break;
	}

	WARN_ON_ONCE(timeout == 0);
}

void brcms_c_set_beacon_listen_interval(struct brcms_c_info *wlc, u8 interval)
{
	wlc->bcn_li_bcn = interval;
	if (wlc->pub->up)
		brcms_c_bcn_li_upd(wlc);
}

int brcms_c_set_tx_power(struct brcms_c_info *wlc, int txpwr)
{
	uint qdbm;

	/* Remove override bit and clip to max qdbm value */
	qdbm = min_t(uint, txpwr * BRCMS_TXPWR_DB_FACTOR, 0xff);
	return wlc_phy_txpower_set(wlc->band->pi, qdbm, false);
}

int brcms_c_get_tx_power(struct brcms_c_info *wlc)
{
	uint qdbm;
	bool override;

	wlc_phy_txpower_get(wlc->band->pi, &qdbm, &override);

	/* Return qdbm units */
	return (int)(qdbm / BRCMS_TXPWR_DB_FACTOR);
}

/* Process received frames */
/*
 * Return true if more frames need to be processed. false otherwise.
 * Param 'bound' indicates max. # frames to process before break out.
 */
static void brcms_c_recv(struct brcms_c_info *wlc, struct sk_buff *p)
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

	/* MAC inserts 2 pad bytes for a4 headers or QoS or A-MSDU subframes */
	if (rxh->RxStatus1 & RXS_PBPRES) {
		if (p->len < 2) {
			brcms_err(wlc->hw->d11core,
				  "wl%d: recv: rcvd runt of len %d\n",
				  wlc->pub->unit, p->len);
			goto toss;
		}
		skb_pull(p, 2);
	}

	h = (struct ieee80211_hdr *)(p->data + D11_PHY_HDR_LEN);
	len = p->len;

	if (rxh->RxStatus1 & RXS_FCSERR) {
		if (!(wlc->filter_flags & FIF_FCSFAIL))
			goto toss;
	}

	/* check received pkt has at least frame control field */
	if (len < D11_PHY_HDR_LEN + sizeof(h->frame_control))
		goto toss;

	/* not supporting A-MSDU */
	is_amsdu = rxh->RxStatus2 & RXS_AMSDU_MASK;
	if (is_amsdu)
		goto toss;

	brcms_c_recvctl(wlc, rxh, p);
	return;

 toss:
	brcmu_pkt_buf_free_skb(p);
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
	struct sk_buff *next = NULL;
	struct sk_buff_head recv_frames;

	uint n = 0;
	uint bound_limit = bound ? RXBND : -1;

	BCMMSG(wlc_hw->wlc->wiphy, "wl%d\n", wlc_hw->unit);
	skb_queue_head_init(&recv_frames);

	/* gather received frames */
	while (dma_rx(wlc_hw->di[fifo], &recv_frames)) {

		/* !give others some time to run! */
		if (++n >= bound_limit)
			break;
	}

	/* post more rbufs */
	dma_rxfill(wlc_hw->di[fifo]);

	/* process each frame */
	skb_queue_walk_safe(&recv_frames, p, next) {
		struct d11rxhdr_le *rxh_le;
		struct d11rxhdr *rxh;

		skb_unlink(p, &recv_frames);
		rxh_le = (struct d11rxhdr_le *)p->data;
		rxh = (struct d11rxhdr *)p->data;

		/* fixup rx header endianness */
		rxh->RxFrameSize = le16_to_cpu(rxh_le->RxFrameSize);
		rxh->PhyRxStatus_0 = le16_to_cpu(rxh_le->PhyRxStatus_0);
		rxh->PhyRxStatus_1 = le16_to_cpu(rxh_le->PhyRxStatus_1);
		rxh->PhyRxStatus_2 = le16_to_cpu(rxh_le->PhyRxStatus_2);
		rxh->PhyRxStatus_3 = le16_to_cpu(rxh_le->PhyRxStatus_3);
		rxh->PhyRxStatus_4 = le16_to_cpu(rxh_le->PhyRxStatus_4);
		rxh->PhyRxStatus_5 = le16_to_cpu(rxh_le->PhyRxStatus_5);
		rxh->RxStatus1 = le16_to_cpu(rxh_le->RxStatus1);
		rxh->RxStatus2 = le16_to_cpu(rxh_le->RxStatus2);
		rxh->RxTSFTime = le16_to_cpu(rxh_le->RxTSFTime);
		rxh->RxChan = le16_to_cpu(rxh_le->RxChan);

		brcms_c_recv(wlc_hw->wlc, p);
	}

	return n >= bound_limit;
}

/* second-level interrupt processing
 *   Return true if another dpc needs to be re-scheduled. false otherwise.
 *   Param 'bounded' indicates if applicable loops should be bounded.
 */
bool brcms_c_dpc(struct brcms_c_info *wlc, bool bounded)
{
	u32 macintstatus;
	struct brcms_hardware *wlc_hw = wlc->hw;
	struct bcma_device *core = wlc_hw->d11core;

	if (brcms_deviceremoved(wlc)) {
		brcms_err(core, "wl%d: %s: dead chip\n", wlc_hw->unit,
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

	/* tx status */
	if (macintstatus & MI_TFS) {
		bool fatal;
		if (brcms_b_txstatus(wlc->hw, bounded, &fatal))
			wlc->macintstatus |= MI_TFS;
		if (fatal) {
			brcms_err(core, "MI_TFS: fatal\n");
			goto fatal;
		}
	}

	if (macintstatus & (MI_TBTT | MI_DTIM_TBTT))
		brcms_c_tbtt(wlc);

	/* ATIM window end */
	if (macintstatus & MI_ATIMWINEND) {
		brcms_dbg_info(core, "end of ATIM window\n");
		bcma_set32(core, D11REGOFFS(maccommand), wlc->qvalid);
		wlc->qvalid = 0;
	}

	/*
	 * received data or control frame, MI_DMAINT is
	 * indication of RX_FIFO interrupt
	 */
	if (macintstatus & MI_DMAINT)
		if (brcms_b_recv(wlc_hw, RX_FIFO, bounded))
			wlc->macintstatus |= MI_DMAINT;

	/* noise sample collected */
	if (macintstatus & MI_BG_NOISE)
		wlc_phy_noise_sample_intr(wlc_hw->band->pi);

	if (macintstatus & MI_GP0) {
		brcms_err(core, "wl%d: PSM microcode watchdog fired at %d "
			  "(seconds). Resetting.\n", wlc_hw->unit, wlc_hw->now);

		printk_once("%s : PSM Watchdog, chipid 0x%x, chiprev 0x%x\n",
			    __func__, ai_get_chip_id(wlc_hw->sih),
			    ai_get_chiprev(wlc_hw->sih));
		brcms_fatal_error(wlc_hw->wlc->wl);
	}

	/* gptimer timeout */
	if (macintstatus & MI_TO)
		bcma_write32(core, D11REGOFFS(gptimer), 0);

	if (macintstatus & MI_RFDISABLE) {
		brcms_dbg_info(core, "wl%d: BMAC Detected a change on the"
			       " RF Disable Input\n", wlc_hw->unit);
		brcms_rfkill_set_hw_state(wlc->wl);
	}

	/* it isn't done and needs to be resched if macintstatus is non-zero */
	return wlc->macintstatus != 0;

 fatal:
	brcms_fatal_error(wlc_hw->wlc->wl);
	return wlc->macintstatus != 0;
}

void brcms_c_init(struct brcms_c_info *wlc, bool mute_tx)
{
	struct bcma_device *core = wlc->hw->d11core;
	struct ieee80211_channel *ch = wlc->pub->ieee_hw->conf.channel;
	u16 chanspec;

	brcms_dbg_info(core, "wl%d\n", wlc->pub->unit);

	chanspec = ch20mhz_chspec(ch->hw_value);

	brcms_b_init(wlc->hw, chanspec);

	/* update beacon listen interval */
	brcms_c_bcn_li_upd(wlc);

	/* write ethernet address to core */
	brcms_c_set_mac(wlc->bsscfg);
	brcms_c_set_bssid(wlc->bsscfg);

	/* Update tsf_cfprep if associated and up */
	if (wlc->pub->associated && wlc->bsscfg->up) {
		u32 bi;

		/* get beacon period and convert to uS */
		bi = wlc->bsscfg->current_bss->beacon_period << 10;
		/*
		 * update since init path would reset
		 * to default value
		 */
		bcma_write32(core, D11REGOFFS(tsf_cfprep),
			     bi << CFPREP_CBI_SHIFT);

		/* Update maccontrol PM related bits */
		brcms_c_set_ps_ctrl(wlc);
	}

	brcms_c_bandinit_ordered(wlc, chanspec);

	/* init probe response timeout */
	brcms_b_write_shm(wlc->hw, M_PRS_MAXTIME, wlc->prb_resp_timeout);

	/* init max burst txop (framebursting) */
	brcms_b_write_shm(wlc->hw, M_MBURST_TXOP,
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
	bcma_set16(core, D11REGOFFS(ifs_ctl), IFS_USEEDCF);
	brcms_c_edcf_setparams(wlc, false);

	/* read the ucode version if we have not yet done so */
	if (wlc->ucode_rev == 0) {
		wlc->ucode_rev =
		    brcms_b_read_shm(wlc->hw, M_BOM_REV_MAJOR) << NBITS(u16);
		wlc->ucode_rev |= brcms_b_read_shm(wlc->hw, M_BOM_REV_MINOR);
	}

	/* ..now really unleash hell (allow the MAC out of suspend) */
	brcms_c_enable_mac(wlc);

	/* suspend the tx fifos and mute the phy for preism cac time */
	if (mute_tx)
		brcms_b_mute(wlc->hw, true);

	/* enable the RF Disable Delay timer */
	bcma_write32(core, D11REGOFFS(rfdisabledly), RFDISABLE_DEFAULT);

	/*
	 * Initialize WME parameters; if they haven't been set by some other
	 * mechanism (IOVar, etc) then read them from the hardware.
	 */
	if (GFIELD(wlc->wme_retries[0], EDCF_SHORT) == 0) {
		/* Uninitialized; read from HW */
		int ac;

		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			wlc->wme_retries[ac] =
			    brcms_b_read_shm(wlc->hw, M_AC_TXLMT_ADDR(ac));
	}
}

/*
 * The common driver entry routine. Error codes should be unique
 */
struct brcms_c_info *
brcms_c_attach(struct brcms_info *wl, struct bcma_device *core, uint unit,
	       bool piomode, uint *perr)
{
	struct brcms_c_info *wlc;
	uint err = 0;
	uint i, j;
	struct brcms_pub *pub;

	/* allocate struct brcms_c_info state and its substructures */
	wlc = brcms_c_attach_malloc(unit, &err, 0);
	if (wlc == NULL)
		goto fail;
	wlc->wiphy = wl->wiphy;
	pub = wlc->pub;

#if defined(DEBUG)
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

	/*
	 * low level attach steps(all hw accesses go
	 * inside, no more in rest of the attach)
	 */
	err = brcms_b_attach(wlc, core, unit, piomode);
	if (err)
		goto fail;

	brcms_c_protection_upd(wlc, BRCMS_PROT_N_PAM_OVR, OFF);

	pub->phy_11ncapable = BRCMS_PHY_11N_CAP(wlc->band);

	/* disable allowed duty cycle */
	wlc->tx_duty_cycle_ofdm = 0;
	wlc->tx_duty_cycle_cck = 0;

	brcms_c_stf_phy_chain_calc(wlc);

	/* txchain 1: txant 0, txchain 2: txant 1 */
	if (BRCMS_ISNPHY(wlc->band) && (wlc->stf->txstreams == 1))
		wlc->stf->txant = wlc->stf->hw_txchain - 1;

	/* push to BMAC driver */
	wlc_phy_stf_chain_init(wlc->band->pi, wlc->stf->hw_txchain,
			       wlc->stf->hw_rxchain);

	/* pull up some info resulting from the low attach */
	for (i = 0; i < NFIFO; i++)
		wlc->core->txavail[i] = wlc->hw->txavail[i];

	memcpy(&wlc->perm_etheraddr, &wlc->hw->etheraddr, ETH_ALEN);
	memcpy(&pub->cur_etheraddr, &wlc->hw->etheraddr, ETH_ALEN);

	for (j = 0; j < wlc->pub->_nbands; j++) {
		wlc->band = wlc->bandstate[j];

		if (!brcms_c_attach_stf_ant_init(wlc)) {
			err = 24;
			goto fail;
		}

		/* default contention windows size limits */
		wlc->band->CWmin = APHY_CWMIN;
		wlc->band->CWmax = PHY_CWMAX;

		/* init gmode value */
		if (wlc->band->bandtype == BRCM_BAND_2G) {
			wlc->band->gmode = GMODE_AUTO;
			brcms_c_protection_upd(wlc, BRCMS_PROT_G_USER,
					   wlc->band->gmode);
		}

		/* init _n_enab supported mode */
		if (BRCMS_PHY_11N_CAP(wlc->band)) {
			pub->_n_enab = SUPPORT_11N;
			brcms_c_protection_upd(wlc, BRCMS_PROT_N_USER,
						   ((pub->_n_enab ==
						     SUPPORT_11N) ? WL_11N_2x2 :
						    WL_11N_3x3));
		}

		/* init per-band default rateset, depend on band->gmode */
		brcms_default_rateset(wlc, &wlc->band->defrateset);

		/* fill in hw_rateset */
		brcms_c_rateset_filter(&wlc->band->defrateset,
				   &wlc->band->hw_rateset, false,
				   BRCMS_RATES_CCK_OFDM, BRCMS_RATE_MASK,
				   (bool) (wlc->pub->_n_enab & SUPPORT_11N));
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

	wlc->bsscfg->wlc = wlc;

	wlc->mimoft = FT_HT;
	wlc->mimo_40txbw = AUTO;
	wlc->ofdm_40txbw = AUTO;
	wlc->cck_40txbw = AUTO;
	brcms_c_update_mimo_band_bwcap(wlc, BRCMS_N_BW_20IN2G_40IN5G);

	/* Set default values of SGI */
	if (BRCMS_SGI_CAP_PHY(wlc)) {
		brcms_c_ht_update_sgi_rx(wlc, (BRCMS_N_SGI_20 |
					       BRCMS_N_SGI_40));
	} else if (BRCMS_ISSSLPNPHY(wlc->band)) {
		brcms_c_ht_update_sgi_rx(wlc, (BRCMS_N_SGI_20 |
					       BRCMS_N_SGI_40));
	} else {
		brcms_c_ht_update_sgi_rx(wlc, 0);
	}

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
