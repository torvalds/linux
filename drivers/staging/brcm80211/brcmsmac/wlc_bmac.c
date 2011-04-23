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
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <proto/802.11.h>
#include <bcmdefs.h>
#include <bcmdevs.h>
#include <bcmwifi.h>
#include <siutils.h>
#include <bcmsrom.h>
#include <bcmotp.h>
#include <bcmutils.h>
#include <wlioctl.h>
#include <sbconfig.h>
#include <sbchipc.h>
#include <pcicfg.h>
#include <sbhnddma.h>
#include <hnddma.h>
#include <hndpmu.h>

#include "wlc_types.h"
#include "d11.h"
#include "wlc_cfg.h"
#include "wlc_rate.h"
#include "wlc_scb.h"
#include "wlc_pub.h"
#include "wlc_key.h"
#include "wlc_phy_shim.h"
#include "phy/wlc_phy_hal.h"
#include "wlc_channel.h"
#include "wlc_main.h"
#include "wl_export.h"
#include "wl_ucode.h"
#include "wlc_antsel.h"
#include "pcie_core.h"
#include "wlc_alloc.h"
#include "wl_dbg.h"
#include "wlc_bmac.h"

#define	TIMER_INTERVAL_WATCHDOG_BMAC	1000	/* watchdog timer, in unit of ms */

#define	SYNTHPU_DLY_APHY_US	3700	/* a phy synthpu_dly time in us */
#define	SYNTHPU_DLY_BPHY_US	1050	/* b/g phy synthpu_dly time in us, default */
#define	SYNTHPU_DLY_NPHY_US	2048	/* n phy REV3 synthpu_dly time in us, default */
#define	SYNTHPU_DLY_LPPHY_US	300	/* lpphy synthpu_dly time in us */

#define	SYNTHPU_DLY_PHY_US_QT	100	/* QT synthpu_dly time in us */

#ifndef BMAC_DUP_TO_REMOVE
#define WLC_RM_WAIT_TX_SUSPEND		4	/* Wait Tx Suspend */

#define	ANTCNT			10	/* vanilla M_MAX_ANTCNT value */

#endif				/* BMAC_DUP_TO_REMOVE */

#define DMAREG(wlc_hw, direction, fifonum) \
	((direction == DMA_TX) ? \
		(void *)&(wlc_hw->regs->fifo64regs[fifonum].dmaxmt) : \
		(void *)&(wlc_hw->regs->fifo64regs[fifonum].dmarcv))

/*
 * The following table lists the buffer memory allocated to xmt fifos in HW.
 * the size is in units of 256bytes(one block), total size is HW dependent
 * ucode has default fifo partition, sw can overwrite if necessary
 *
 * This is documented in twiki under the topic UcodeTxFifo. Please ensure
 * the twiki is updated before making changes.
 */

#define XMTFIFOTBL_STARTREV	20	/* Starting corerev for the fifo size table */

static u16 xmtfifo_sz[][NFIFO] = {
	{20, 192, 192, 21, 17, 5},	/* corerev 20: 5120, 49152, 49152, 5376, 4352, 1280 */
	{9, 58, 22, 14, 14, 5},	/* corerev 21: 2304, 14848, 5632, 3584, 3584, 1280 */
	{20, 192, 192, 21, 17, 5},	/* corerev 22: 5120, 49152, 49152, 5376, 4352, 1280 */
	{20, 192, 192, 21, 17, 5},	/* corerev 23: 5120, 49152, 49152, 5376, 4352, 1280 */
	{9, 58, 22, 14, 14, 5},	/* corerev 24: 2304, 14848, 5632, 3584, 3584, 1280 */
};

static void wlc_clkctl_clk(struct wlc_hw_info *wlc, uint mode);
static void wlc_coreinit(struct wlc_info *wlc);

/* used by wlc_wakeucode_init() */
static void wlc_write_inits(struct wlc_hw_info *wlc_hw,
			    const struct d11init *inits);
static void wlc_ucode_write(struct wlc_hw_info *wlc_hw, const u32 ucode[],
			    const uint nbytes);
static void wlc_ucode_download(struct wlc_hw_info *wlc);
static void wlc_ucode_txant_set(struct wlc_hw_info *wlc_hw);

/* used by wlc_dpc() */
static bool wlc_bmac_dotxstatus(struct wlc_hw_info *wlc, tx_status_t *txs,
				u32 s2);
static bool wlc_bmac_txstatus(struct wlc_hw_info *wlc, bool bound, bool *fatal);
static bool wlc_bmac_recv(struct wlc_hw_info *wlc_hw, uint fifo, bool bound);

/* used by wlc_down() */
static void wlc_flushqueues(struct wlc_info *wlc);

static void wlc_write_mhf(struct wlc_hw_info *wlc_hw, u16 *mhfs);
static void wlc_mctrl_reset(struct wlc_hw_info *wlc_hw);
static void wlc_corerev_fifofixup(struct wlc_hw_info *wlc_hw);
static bool wlc_bmac_tx_fifo_suspended(struct wlc_hw_info *wlc_hw,
				       uint tx_fifo);
static void wlc_bmac_tx_fifo_suspend(struct wlc_hw_info *wlc_hw, uint tx_fifo);
static void wlc_bmac_tx_fifo_resume(struct wlc_hw_info *wlc_hw, uint tx_fifo);

/* Low Level Prototypes */
static int wlc_bmac_bandtype(struct wlc_hw_info *wlc_hw);
static void wlc_bmac_info_init(struct wlc_hw_info *wlc_hw);
static void wlc_bmac_xtal(struct wlc_hw_info *wlc_hw, bool want);
static u16 wlc_bmac_read_objmem(struct wlc_hw_info *wlc_hw, uint offset,
				   u32 sel);
static void wlc_bmac_write_objmem(struct wlc_hw_info *wlc_hw, uint offset,
				  u16 v, u32 sel);
static void wlc_bmac_core_phy_clk(struct wlc_hw_info *wlc_hw, bool clk);
static bool wlc_bmac_attach_dmapio(struct wlc_info *wlc, uint j, bool wme);
static void wlc_bmac_detach_dmapio(struct wlc_hw_info *wlc_hw);
static void wlc_ucode_bsinit(struct wlc_hw_info *wlc_hw);
static bool wlc_validboardtype(struct wlc_hw_info *wlc);
static bool wlc_isgoodchip(struct wlc_hw_info *wlc_hw);
static bool wlc_bmac_validate_chip_access(struct wlc_hw_info *wlc_hw);
static char *wlc_get_macaddr(struct wlc_hw_info *wlc_hw);
static void wlc_mhfdef(struct wlc_info *wlc, u16 *mhfs, u16 mhf2_init);
static void wlc_mctrl_write(struct wlc_hw_info *wlc_hw);
static void wlc_bmac_mute(struct wlc_hw_info *wlc_hw, bool want, mbool flags);
static void wlc_ucode_mute_override_set(struct wlc_hw_info *wlc_hw);
static void wlc_ucode_mute_override_clear(struct wlc_hw_info *wlc_hw);
static u32 wlc_wlintrsoff(struct wlc_info *wlc);
static void wlc_wlintrsrestore(struct wlc_info *wlc, u32 macintmask);
static void wlc_gpio_init(struct wlc_info *wlc);
static void wlc_write_hw_bcntemplate0(struct wlc_hw_info *wlc_hw, void *bcn,
				      int len);
static void wlc_write_hw_bcntemplate1(struct wlc_hw_info *wlc_hw, void *bcn,
				      int len);
static void wlc_bmac_bsinit(struct wlc_info *wlc, chanspec_t chanspec);
static u32 wlc_setband_inact(struct wlc_info *wlc, uint bandunit);
static void wlc_bmac_setband(struct wlc_hw_info *wlc_hw, uint bandunit,
			     chanspec_t chanspec);
static void wlc_bmac_update_slot_timing(struct wlc_hw_info *wlc_hw,
					bool shortslot);
static void wlc_upd_ofdm_pctl1_table(struct wlc_hw_info *wlc_hw);
static u16 wlc_bmac_ofdm_ratetable_offset(struct wlc_hw_info *wlc_hw,
					     u8 rate);

/* === Low Level functions === */

void wlc_bmac_set_shortslot(struct wlc_hw_info *wlc_hw, bool shortslot)
{
	wlc_hw->shortslot = shortslot;

	if (BAND_2G(wlc_bmac_bandtype(wlc_hw)) && wlc_hw->up) {
		wlc_suspend_mac_and_wait(wlc_hw->wlc);
		wlc_bmac_update_slot_timing(wlc_hw, shortslot);
		wlc_enable_mac(wlc_hw->wlc);
	}
}

/*
 * Update the slot timing for standard 11b/g (20us slots)
 * or shortslot 11g (9us slots)
 * The PSM needs to be suspended for this call.
 */
static void wlc_bmac_update_slot_timing(struct wlc_hw_info *wlc_hw,
					bool shortslot)
{
	d11regs_t *regs;

	regs = wlc_hw->regs;

	if (shortslot) {
		/* 11g short slot: 11a timing */
		W_REG(&regs->ifs_slot, 0x0207);	/* APHY_SLOT_TIME */
		wlc_bmac_write_shm(wlc_hw, M_DOT11_SLOT, APHY_SLOT_TIME);
	} else {
		/* 11g long slot: 11b timing */
		W_REG(&regs->ifs_slot, 0x0212);	/* BPHY_SLOT_TIME */
		wlc_bmac_write_shm(wlc_hw, M_DOT11_SLOT, BPHY_SLOT_TIME);
	}
}

static void WLBANDINITFN(wlc_ucode_bsinit) (struct wlc_hw_info *wlc_hw)
{
	/* init microcode host flags */
	wlc_write_mhf(wlc_hw, wlc_hw->band->mhfs);

	/* do band-specific ucode IHR, SHM, and SCR inits */
	if (D11REV_IS(wlc_hw->corerev, 23)) {
		if (WLCISNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11n0bsinitvals16);
		} else {
			WL_ERROR("%s: wl%d: unsupported phy in corerev %d\n",
				 __func__, wlc_hw->unit, wlc_hw->corerev);
		}
	} else {
		if (D11REV_IS(wlc_hw->corerev, 24)) {
			if (WLCISLCNPHY(wlc_hw->band)) {
				wlc_write_inits(wlc_hw, d11lcn0bsinitvals24);
			} else
				WL_ERROR("%s: wl%d: unsupported phy in corerev %d\n",
					 __func__, wlc_hw->unit,
					 wlc_hw->corerev);
		} else {
			WL_ERROR("%s: wl%d: unsupported corerev %d\n",
				 __func__, wlc_hw->unit, wlc_hw->corerev);
		}
	}
}

/* switch to new band but leave it inactive */
static u32 WLBANDINITFN(wlc_setband_inact) (struct wlc_info *wlc, uint bandunit)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
	u32 macintmask;

	WL_TRACE("wl%d: wlc_setband_inact\n", wlc_hw->unit);

	ASSERT(bandunit != wlc_hw->band->bandunit);
	ASSERT(si_iscoreup(wlc_hw->sih));
	ASSERT((R_REG(&wlc_hw->regs->maccontrol) & MCTL_EN_MAC) ==
	       0);

	/* disable interrupts */
	macintmask = wl_intrsoff(wlc->wl);

	/* radio off */
	wlc_phy_switch_radio(wlc_hw->band->pi, OFF);

	ASSERT(wlc_hw->clk);

	wlc_bmac_core_phy_clk(wlc_hw, OFF);

	wlc_setxband(wlc_hw, bandunit);

	return macintmask;
}

/* Process received frames */
/*
 * Return true if more frames need to be processed. false otherwise.
 * Param 'bound' indicates max. # frames to process before break out.
 */
static bool BCMFASTPATH
wlc_bmac_recv(struct wlc_hw_info *wlc_hw, uint fifo, bool bound)
{
	struct sk_buff *p;
	struct sk_buff *head = NULL;
	struct sk_buff *tail = NULL;
	uint n = 0;
	uint bound_limit = bound ? wlc_hw->wlc->pub->tunables->rxbnd : -1;
	u32 tsf_h, tsf_l;
	wlc_d11rxhdr_t *wlc_rxhdr = NULL;

	WL_TRACE("wl%d: %s\n", wlc_hw->unit, __func__);
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

	/* get the TSF REG reading */
	wlc_bmac_read_tsf(wlc_hw, &tsf_l, &tsf_h);

	/* post more rbufs */
	dma_rxfill(wlc_hw->di[fifo]);

	/* process each frame */
	while ((p = head) != NULL) {
		head = head->prev;
		p->prev = NULL;

		/* record the tsf_l in wlc_rxd11hdr */
		wlc_rxhdr = (wlc_d11rxhdr_t *) p->data;
		wlc_rxhdr->tsf_l = cpu_to_le32(tsf_l);

		/* compute the RSSI from d11rxhdr and record it in wlc_rxd11hr */
		wlc_phy_rssi_compute(wlc_hw->band->pi, wlc_rxhdr);

		wlc_recv(wlc_hw->wlc, p);
	}

	return n >= bound_limit;
}

/* second-level interrupt processing
 *   Return true if another dpc needs to be re-scheduled. false otherwise.
 *   Param 'bounded' indicates if applicable loops should be bounded.
 */
bool BCMFASTPATH wlc_dpc(struct wlc_info *wlc, bool bounded)
{
	u32 macintstatus;
	struct wlc_hw_info *wlc_hw = wlc->hw;
	d11regs_t *regs = wlc_hw->regs;
	bool fatal = false;

	if (DEVICEREMOVED(wlc)) {
		WL_ERROR("wl%d: %s: dead chip\n", wlc_hw->unit, __func__);
		wl_down(wlc->wl);
		return false;
	}

	/* grab and clear the saved software intstatus bits */
	macintstatus = wlc->macintstatus;
	wlc->macintstatus = 0;

	WL_TRACE("wl%d: wlc_dpc: macintstatus 0x%x\n",
		 wlc_hw->unit, macintstatus);

	if (macintstatus & MI_PRQ) {
		/* Process probe request FIFO */
		ASSERT(0 && "PRQ Interrupt in non-MBSS");
	}

	/* BCN template is available */
	/* ZZZ: Use AP_ACTIVE ? */
	if (AP_ENAB(wlc->pub) && (!APSTA_ENAB(wlc->pub) || wlc->aps_associated)
	    && (macintstatus & MI_BCNTPL)) {
		wlc_update_beacon(wlc);
	}

	/* PMQ entry addition */
	if (macintstatus & MI_PMQ) {
	}

	/* tx status */
	if (macintstatus & MI_TFS) {
		if (wlc_bmac_txstatus(wlc->hw, bounded, &fatal))
			wlc->macintstatus |= MI_TFS;
		if (fatal) {
			WL_ERROR("MI_TFS: fatal\n");
			goto fatal;
		}
	}

	if (macintstatus & (MI_TBTT | MI_DTIM_TBTT))
		wlc_tbtt(wlc, regs);

	/* ATIM window end */
	if (macintstatus & MI_ATIMWINEND) {
		WL_TRACE("wlc_isr: end of ATIM window\n");

		OR_REG(&regs->maccommand, wlc->qvalid);
		wlc->qvalid = 0;
	}

	/* phy tx error */
	if (macintstatus & MI_PHYTXERR) {
		wlc->pub->_cnt->txphyerr++;
	}

	/* received data or control frame, MI_DMAINT is indication of RX_FIFO interrupt */
	if (macintstatus & MI_DMAINT) {
		if (wlc_bmac_recv(wlc_hw, RX_FIFO, bounded)) {
			wlc->macintstatus |= MI_DMAINT;
		}
	}

	/* TX FIFO suspend/flush completion */
	if (macintstatus & MI_TXSTOP) {
		if (wlc_bmac_tx_fifo_suspended(wlc_hw, TX_DATA_FIFO)) {
			/*      WL_ERROR("dpc: fifo_suspend_comlete\n"); */
		}
	}

	/* noise sample collected */
	if (macintstatus & MI_BG_NOISE) {
		wlc_phy_noise_sample_intr(wlc_hw->band->pi);
	}

	if (macintstatus & MI_GP0) {
		WL_ERROR("wl%d: PSM microcode watchdog fired at %d (seconds). Resetting.\n",
			 wlc_hw->unit, wlc_hw->now);

		printk_once("%s : PSM Watchdog, chipid 0x%x, chiprev 0x%x\n",
					__func__, wlc_hw->sih->chip,
					wlc_hw->sih->chiprev);

		wlc->pub->_cnt->psmwds++;

		/* big hammer */
		wl_init(wlc->wl);
	}

	/* gptimer timeout */
	if (macintstatus & MI_TO) {
		W_REG(&regs->gptimer, 0);
	}

	if (macintstatus & MI_RFDISABLE) {
		WL_TRACE("wl%d: BMAC Detected a change on the RF Disable Input\n", wlc_hw->unit);

		wlc->pub->_cnt->rfdisable++;
		wl_rfkill_set_hw_state(wlc->wl);
	}

	/* send any enq'd tx packets. Just makes sure to jump start tx */
	if (!pktq_empty(&wlc->active_queue->q))
		wlc_send_q(wlc, wlc->active_queue);

	ASSERT(wlc_ps_check(wlc));

	/* make sure the bound indication and the implementation are in sync */
	ASSERT(bounded == true || wlc->macintstatus == 0);

	/* it isn't done and needs to be resched if macintstatus is non-zero */
	return wlc->macintstatus != 0;

 fatal:
	wl_init(wlc->wl);
	return wlc->macintstatus != 0;
}

/* common low-level watchdog code */
void wlc_bmac_watchdog(void *arg)
{
	struct wlc_info *wlc = (struct wlc_info *) arg;
	struct wlc_hw_info *wlc_hw = wlc->hw;

	WL_TRACE("wl%d: wlc_bmac_watchdog\n", wlc_hw->unit);

	if (!wlc_hw->up)
		return;

	/* increment second count */
	wlc_hw->now++;

	/* Check for FIFO error interrupts */
	wlc_bmac_fifoerrors(wlc_hw);

	/* make sure RX dma has buffers */
	dma_rxfill(wlc->hw->di[RX_FIFO]);

	wlc_phy_watchdog(wlc_hw->band->pi);
}

void
wlc_bmac_set_chanspec(struct wlc_hw_info *wlc_hw, chanspec_t chanspec,
		      bool mute, struct txpwr_limits *txpwr)
{
	uint bandunit;

	WL_TRACE("wl%d: wlc_bmac_set_chanspec 0x%x\n",
		 wlc_hw->unit, chanspec);

	wlc_hw->chanspec = chanspec;

	/* Switch bands if necessary */
	if (NBANDS_HW(wlc_hw) > 1) {
		bandunit = CHSPEC_WLCBANDUNIT(chanspec);
		if (wlc_hw->band->bandunit != bandunit) {
			/* wlc_bmac_setband disables other bandunit,
			 *  use light band switch if not up yet
			 */
			if (wlc_hw->up) {
				wlc_phy_chanspec_radio_set(wlc_hw->
							   bandstate[bandunit]->
							   pi, chanspec);
				wlc_bmac_setband(wlc_hw, bandunit, chanspec);
			} else {
				wlc_setxband(wlc_hw, bandunit);
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
		wlc_bmac_mute(wlc_hw, mute, 0);
	}
}

int wlc_bmac_state_get(struct wlc_hw_info *wlc_hw, wlc_bmac_state_t *state)
{
	state->machwcap = wlc_hw->machwcap;

	return 0;
}

static bool wlc_bmac_attach_dmapio(struct wlc_info *wlc, uint j, bool wme)
{
	uint i;
	char name[8];
	/* ucode host flag 2 needed for pio mode, independent of band and fifo */
	u16 pio_mhf2 = 0;
	struct wlc_hw_info *wlc_hw = wlc->hw;
	uint unit = wlc_hw->unit;
	wlc_tunables_t *tune = wlc->pub->tunables;

	/* name and offsets for dma_attach */
	snprintf(name, sizeof(name), "wl%d", unit);

	if (wlc_hw->di[0] == 0) {	/* Init FIFOs */
		uint addrwidth;
		int dma_attach_err = 0;
		/* Find out the DMA addressing capability and let OS know
		 * All the channels within one DMA core have 'common-minimum' same
		 * capability
		 */
		addrwidth =
		    dma_addrwidth(wlc_hw->sih, DMAREG(wlc_hw, DMA_TX, 0));

		if (!wl_alloc_dma_resources(wlc_hw->wlc->wl, addrwidth)) {
			WL_ERROR("wl%d: wlc_attach: alloc_dma_resources failed\n",
				 unit);
			return false;
		}

		/*
		 * FIFO 0
		 * TX: TX_AC_BK_FIFO (TX AC Background data packets)
		 * RX: RX_FIFO (RX data packets)
		 */
		ASSERT(TX_AC_BK_FIFO == 0);
		ASSERT(RX_FIFO == 0);
		wlc_hw->di[0] = dma_attach(name, wlc_hw->sih,
					   (wme ? DMAREG(wlc_hw, DMA_TX, 0) :
					    NULL), DMAREG(wlc_hw, DMA_RX, 0),
					   (wme ? tune->ntxd : 0), tune->nrxd,
					   tune->rxbufsz, -1, tune->nrxbufpost,
					   WL_HWRXOFF, &wl_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[0]);

		/*
		 * FIFO 1
		 * TX: TX_AC_BE_FIFO (TX AC Best-Effort data packets)
		 *   (legacy) TX_DATA_FIFO (TX data packets)
		 * RX: UNUSED
		 */
		ASSERT(TX_AC_BE_FIFO == 1);
		ASSERT(TX_DATA_FIFO == 1);
		wlc_hw->di[1] = dma_attach(name, wlc_hw->sih,
					   DMAREG(wlc_hw, DMA_TX, 1), NULL,
					   tune->ntxd, 0, 0, -1, 0, 0,
					   &wl_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[1]);

		/*
		 * FIFO 2
		 * TX: TX_AC_VI_FIFO (TX AC Video data packets)
		 * RX: UNUSED
		 */
		ASSERT(TX_AC_VI_FIFO == 2);
		wlc_hw->di[2] = dma_attach(name, wlc_hw->sih,
					   DMAREG(wlc_hw, DMA_TX, 2), NULL,
					   tune->ntxd, 0, 0, -1, 0, 0,
					   &wl_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[2]);
		/*
		 * FIFO 3
		 * TX: TX_AC_VO_FIFO (TX AC Voice data packets)
		 *   (legacy) TX_CTL_FIFO (TX control & mgmt packets)
		 */
		ASSERT(TX_AC_VO_FIFO == 3);
		ASSERT(TX_CTL_FIFO == 3);
		wlc_hw->di[3] = dma_attach(name, wlc_hw->sih,
					   DMAREG(wlc_hw, DMA_TX, 3),
					   NULL, tune->ntxd, 0, 0, -1,
					   0, 0, &wl_msg_level);
		dma_attach_err |= (NULL == wlc_hw->di[3]);
/* Cleaner to leave this as if with AP defined */

		if (dma_attach_err) {
			WL_ERROR("wl%d: wlc_attach: dma_attach failed\n", unit);
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
	wlc_mhfdef(wlc, wlc_hw->band->mhfs, pio_mhf2);

	return true;
}

static void wlc_bmac_detach_dmapio(struct wlc_hw_info *wlc_hw)
{
	uint j;

	for (j = 0; j < NFIFO; j++) {
		if (wlc_hw->di[j]) {
			dma_detach(wlc_hw->di[j]);
			wlc_hw->di[j] = NULL;
		}
	}
}

/* low level attach
 *    run backplane attach, init nvram
 *    run phy attach
 *    initialize software state for each core and band
 *    put the whole chip in reset(driver down state), no clock
 */
int wlc_bmac_attach(struct wlc_info *wlc, u16 vendor, u16 device, uint unit,
		    bool piomode, void *regsva, uint bustype, void *btparam)
{
	struct wlc_hw_info *wlc_hw;
	d11regs_t *regs;
	char *macaddr = NULL;
	char *vars;
	uint err = 0;
	uint j;
	bool wme = false;
	shared_phy_params_t sha_params;

	WL_TRACE("wl%d: wlc_bmac_attach: vendor 0x%x device 0x%x\n",
		 unit, vendor, device);

	ASSERT(sizeof(wlc_d11rxhdr_t) <= WL_HWRXOFF);

	wme = true;

	wlc_hw = wlc->hw;
	wlc_hw->wlc = wlc;
	wlc_hw->unit = unit;
	wlc_hw->band = wlc_hw->bandstate[0];
	wlc_hw->_piomode = piomode;

	/* populate struct wlc_hw_info with default values  */
	wlc_bmac_info_init(wlc_hw);

	/*
	 * Do the hardware portion of the attach.
	 * Also initialize software state that depends on the particular hardware
	 * we are running.
	 */
	wlc_hw->sih = si_attach((uint) device, regsva, bustype, btparam,
				&wlc_hw->vars, &wlc_hw->vars_size);
	if (wlc_hw->sih == NULL) {
		WL_ERROR("wl%d: wlc_bmac_attach: si_attach failed\n", unit);
		err = 11;
		goto fail;
	}
	vars = wlc_hw->vars;

	/*
	 * Get vendid/devid nvram overwrites, which could be different
	 * than those the BIOS recognizes for devices on PCMCIA_BUS,
	 * SDIO_BUS, and SROMless devices on PCI_BUS.
	 */
#ifdef BCMBUSTYPE
	bustype = BCMBUSTYPE;
#endif
	if (bustype != SI_BUS) {
		char *var;

		var = getvar(vars, "vendid");
		if (var) {
			vendor = (u16) simple_strtoul(var, NULL, 0);
			WL_ERROR("Overriding vendor id = 0x%x\n", vendor);
		}
		var = getvar(vars, "devid");
		if (var) {
			u16 devid = (u16) simple_strtoul(var, NULL, 0);
			if (devid != 0xffff) {
				device = devid;
				WL_ERROR("Overriding device id = 0x%x\n",
					 device);
			}
		}

		/* verify again the device is supported */
		if (!wlc_chipmatch(vendor, device)) {
			WL_ERROR("wl%d: wlc_bmac_attach: Unsupported vendor/device (0x%x/0x%x)\n",
				 unit, vendor, device);
			err = 12;
			goto fail;
		}
	}

	wlc_hw->vendorid = vendor;
	wlc_hw->deviceid = device;

	/* set bar0 window to point at D11 core */
	wlc_hw->regs = (d11regs_t *) si_setcore(wlc_hw->sih, D11_CORE_ID, 0);
	wlc_hw->corerev = si_corerev(wlc_hw->sih);

	regs = wlc_hw->regs;

	wlc->regs = wlc_hw->regs;

	/* validate chip, chiprev and corerev */
	if (!wlc_isgoodchip(wlc_hw)) {
		err = 13;
		goto fail;
	}

	/* initialize power control registers */
	si_clkctl_init(wlc_hw->sih);

	/* request fastclock and force fastclock for the rest of attach
	 * bring the d11 core out of reset.
	 *   For PMU chips, the first wlc_clkctl_clk is no-op since core-clk is still false;
	 *   But it will be called again inside wlc_corereset, after d11 is out of reset.
	 */
	wlc_clkctl_clk(wlc_hw, CLK_FAST);
	wlc_bmac_corereset(wlc_hw, WLC_USE_COREFLAGS);

	if (!wlc_bmac_validate_chip_access(wlc_hw)) {
		WL_ERROR("wl%d: wlc_bmac_attach: validate_chip_access failed\n",
			 unit);
		err = 14;
		goto fail;
	}

	/* get the board rev, used just below */
	j = getintvar(vars, "boardrev");
	/* promote srom boardrev of 0xFF to 1 */
	if (j == BOARDREV_PROMOTABLE)
		j = BOARDREV_PROMOTED;
	wlc_hw->boardrev = (u16) j;
	if (!wlc_validboardtype(wlc_hw)) {
		WL_ERROR("wl%d: wlc_bmac_attach: Unsupported Broadcom board type (0x%x)" " or revision level (0x%x)\n",
			 unit, wlc_hw->sih->boardtype, wlc_hw->boardrev);
		err = 15;
		goto fail;
	}
	wlc_hw->sromrev = (u8) getintvar(vars, "sromrev");
	wlc_hw->boardflags = (u32) getintvar(vars, "boardflags");
	wlc_hw->boardflags2 = (u32) getintvar(vars, "boardflags2");

	if (wlc_hw->boardflags & BFL_NOPLLDOWN)
		wlc_bmac_pllreq(wlc_hw, true, WLC_PLLREQ_SHARED);

	if ((wlc_hw->sih->bustype == PCI_BUS)
	    && (si_pci_war16165(wlc_hw->sih)))
		wlc->war16165 = true;

	/* check device id(srom, nvram etc.) to set bands */
	if (wlc_hw->deviceid == BCM43224_D11N_ID) {
		/* Dualband boards */
		wlc_hw->_nbands = 2;
	} else
		wlc_hw->_nbands = 1;

	if ((wlc_hw->sih->chip == BCM43225_CHIP_ID))
		wlc_hw->_nbands = 1;

	/* BMAC_NOTE: remove init of pub values when wlc_attach() unconditionally does the
	 * init of these values
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
		WL_ERROR("wl%d: wlc_bmac_attach: wlc_phy_shim_attach failed\n",
			 unit);
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
	sha_params.bustype = wlc_hw->sih->bustype;
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

		wlc_setxband(wlc_hw, j);

		wlc_hw->band->bandunit = j;
		wlc_hw->band->bandtype = j ? WLC_BAND_5G : WLC_BAND_2G;
		wlc->band->bandunit = j;
		wlc->band->bandtype = j ? WLC_BAND_5G : WLC_BAND_2G;
		wlc->core->coreidx = si_coreidx(wlc_hw->sih);

		wlc_hw->machwcap = R_REG(&regs->machwcap);
		wlc_hw->machwcap_backup = wlc_hw->machwcap;

		/* init tx fifo size */
		ASSERT((wlc_hw->corerev - XMTFIFOTBL_STARTREV) <
		       ARRAY_SIZE(xmtfifo_sz));
		wlc_hw->xmtfifo_sz =
		    xmtfifo_sz[(wlc_hw->corerev - XMTFIFOTBL_STARTREV)];

		/* Get a phy for this band */
		wlc_hw->band->pi = wlc_phy_attach(wlc_hw->phy_sh,
			(void *)regs, wlc_bmac_bandtype(wlc_hw), vars);
		if (wlc_hw->band->pi == NULL) {
			WL_ERROR("wl%d: wlc_bmac_attach: wlc_phy_attach failed\n",
				 unit);
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
		if (WLCISNPHY(wlc_hw->band)) {
			if (NCONF_HAS(wlc_hw->band->phyrev))
				goto good_phy;
			else
				goto bad_phy;
		} else if (WLCISLCNPHY(wlc_hw->band)) {
			if (LCNCONF_HAS(wlc_hw->band->phyrev))
				goto good_phy;
			else
				goto bad_phy;
		} else {
 bad_phy:
			WL_ERROR("wl%d: wlc_bmac_attach: unsupported phy type/rev (%d/%d)\n",
				 unit,
				 wlc_hw->band->phytype, wlc_hw->band->phyrev);
			err = 18;
			goto fail;
		}

 good_phy:
		/* BMAC_NOTE: wlc->band->pi should not be set below and should be done in the
		 * high level attach. However we can not make that change until all low level access
		 * is changed to wlc_hw->band->pi. Instead do the wlc->band->pi init below, keeping
		 * wlc_hw->band->pi as well for incremental update of low level fns, and cut over
		 * low only init when all fns updated.
		 */
		wlc->band->pi = wlc_hw->band->pi;
		wlc->band->phytype = wlc_hw->band->phytype;
		wlc->band->phyrev = wlc_hw->band->phyrev;
		wlc->band->radioid = wlc_hw->band->radioid;
		wlc->band->radiorev = wlc_hw->band->radiorev;

		/* default contention windows size limits */
		wlc_hw->band->CWmin = APHY_CWMIN;
		wlc_hw->band->CWmax = PHY_CWMAX;

		if (!wlc_bmac_attach_dmapio(wlc, j, wme)) {
			err = 19;
			goto fail;
		}
	}

	/* disable core to match driver "down" state */
	wlc_coredisable(wlc_hw);

	/* Match driver "down" state */
	if (wlc_hw->sih->bustype == PCI_BUS)
		si_pci_down(wlc_hw->sih);

	/* register sb interrupt callback functions */
	si_register_intr_callback(wlc_hw->sih, (void *)wlc_wlintrsoff,
				  (void *)wlc_wlintrsrestore, NULL, wlc);

	/* turn off pll and xtal to match driver "down" state */
	wlc_bmac_xtal(wlc_hw, OFF);

	/* *********************************************************************
	 * The hardware is in the DOWN state at this point. D11 core
	 * or cores are in reset with clocks off, and the board PLLs
	 * are off if possible.
	 *
	 * Beyond this point, wlc->sbclk == false and chip registers
	 * should not be touched.
	 *********************************************************************
	 */

	/* init etheraddr state variables */
	macaddr = wlc_get_macaddr(wlc_hw);
	if (macaddr == NULL) {
		WL_ERROR("wl%d: wlc_bmac_attach: macaddr not found\n", unit);
		err = 21;
		goto fail;
	}
	bcm_ether_atoe(macaddr, wlc_hw->etheraddr);
	if (is_broadcast_ether_addr(wlc_hw->etheraddr) ||
	    is_zero_ether_addr(wlc_hw->etheraddr)) {
		WL_ERROR("wl%d: wlc_bmac_attach: bad macaddr %s\n",
			 unit, macaddr);
		err = 22;
		goto fail;
	}

	WL_TRACE("%s:: deviceid 0x%x nbands %d board 0x%x macaddr: %s\n",
		 __func__, wlc_hw->deviceid, wlc_hw->_nbands,
		 wlc_hw->sih->boardtype, macaddr);

	return err;

 fail:
	WL_ERROR("wl%d: wlc_bmac_attach: failed with err %d\n", unit, err);
	return err;
}

/*
 * Initialize wlc_info default values ...
 * may get overrides later in this function
 *  BMAC_NOTES, move low out and resolve the dangling ones
 */
static void wlc_bmac_info_init(struct wlc_hw_info *wlc_hw)
{
	struct wlc_info *wlc = wlc_hw->wlc;

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

/*
 * low level detach
 */
int wlc_bmac_detach(struct wlc_info *wlc)
{
	uint i;
	struct wlc_hwband *band;
	struct wlc_hw_info *wlc_hw = wlc->hw;
	int callbacks;

	callbacks = 0;

	if (wlc_hw->sih) {
		/* detach interrupt sync mechanism since interrupt is disabled and per-port
		 * interrupt object may has been freed. this must be done before sb core switch
		 */
		si_deregister_intr_callback(wlc_hw->sih);

		if (wlc_hw->sih->bustype == PCI_BUS)
			si_pci_sleep(wlc_hw->sih);
	}

	wlc_bmac_detach_dmapio(wlc_hw);

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
	wlc_phy_shared_detach(wlc_hw->phy_sh);

	wlc_phy_shim_detach(wlc_hw->physhim);

	/* free vars */
	kfree(wlc_hw->vars);
	wlc_hw->vars = NULL;

	if (wlc_hw->sih) {
		si_detach(wlc_hw->sih);
		wlc_hw->sih = NULL;
	}

	return callbacks;

}

void wlc_bmac_reset(struct wlc_hw_info *wlc_hw)
{
	WL_TRACE("wl%d: wlc_bmac_reset\n", wlc_hw->unit);

	wlc_hw->wlc->pub->_cnt->reset++;

	/* reset the core */
	if (!DEVICEREMOVED(wlc_hw->wlc))
		wlc_bmac_corereset(wlc_hw, WLC_USE_COREFLAGS);

	/* purge the dma rings */
	wlc_flushqueues(wlc_hw->wlc);

	wlc_reset_bmac_done(wlc_hw->wlc);
}

void
wlc_bmac_init(struct wlc_hw_info *wlc_hw, chanspec_t chanspec,
			  bool mute) {
	u32 macintmask;
	bool fastclk;
	struct wlc_info *wlc = wlc_hw->wlc;

	WL_TRACE("wl%d: wlc_bmac_init\n", wlc_hw->unit);

	/* request FAST clock if not on */
	fastclk = wlc_hw->forcefastclk;
	if (!fastclk)
		wlc_clkctl_clk(wlc_hw, CLK_FAST);

	/* disable interrupts */
	macintmask = wl_intrsoff(wlc->wl);

	/* set up the specified band and chanspec */
	wlc_setxband(wlc_hw, CHSPEC_WLCBANDUNIT(chanspec));
	wlc_phy_chanspec_radio_set(wlc_hw->band->pi, chanspec);

	/* do one-time phy inits and calibration */
	wlc_phy_cal_init(wlc_hw->band->pi);

	/* core-specific initialization */
	wlc_coreinit(wlc);

	/* suspend the tx fifos and mute the phy for preism cac time */
	if (mute)
		wlc_bmac_mute(wlc_hw, ON, PHY_MUTE_FOR_PREISM);

	/* band-specific inits */
	wlc_bmac_bsinit(wlc, chanspec);

	/* restore macintmask */
	wl_intrsrestore(wlc->wl, macintmask);

	/* seed wake_override with WLC_WAKE_OVERRIDE_MACSUSPEND since the mac is suspended
	 * and wlc_enable_mac() will clear this override bit.
	 */
	mboolset(wlc_hw->wake_override, WLC_WAKE_OVERRIDE_MACSUSPEND);

	/*
	 * initialize mac_suspend_depth to 1 to match ucode initial suspended state
	 */
	wlc_hw->mac_suspend_depth = 1;

	/* restore the clk */
	if (!fastclk)
		wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);
}

int wlc_bmac_up_prep(struct wlc_hw_info *wlc_hw)
{
	uint coremask;

	WL_TRACE("wl%d: %s:\n", wlc_hw->unit, __func__);

	ASSERT(wlc_hw->wlc->pub->hw_up && wlc_hw->wlc->macintmask == 0);

	/*
	 * Enable pll and xtal, initialize the power control registers,
	 * and force fastclock for the remainder of wlc_up().
	 */
	wlc_bmac_xtal(wlc_hw, ON);
	si_clkctl_init(wlc_hw->sih);
	wlc_clkctl_clk(wlc_hw, CLK_FAST);

	/*
	 * Configure pci/pcmcia here instead of in wlc_attach()
	 * to allow mfg hotswap:  down, hotswap (chip power cycle), up.
	 */
	coremask = (1 << wlc_hw->wlc->core->coreidx);

	if (wlc_hw->sih->bustype == PCI_BUS)
		si_pci_setup(wlc_hw->sih, coremask);

	ASSERT(si_coreid(wlc_hw->sih) == D11_CORE_ID);

	/*
	 * Need to read the hwradio status here to cover the case where the system
	 * is loaded with the hw radio disabled. We do not want to bring the driver up in this case.
	 */
	if (wlc_bmac_radio_read_hwdisabled(wlc_hw)) {
		/* put SB PCI in down state again */
		if (wlc_hw->sih->bustype == PCI_BUS)
			si_pci_down(wlc_hw->sih);
		wlc_bmac_xtal(wlc_hw, OFF);
		return BCME_RADIOOFF;
	}

	if (wlc_hw->sih->bustype == PCI_BUS)
		si_pci_up(wlc_hw->sih);

	/* reset the d11 core */
	wlc_bmac_corereset(wlc_hw, WLC_USE_COREFLAGS);

	return 0;
}

int wlc_bmac_up_finish(struct wlc_hw_info *wlc_hw)
{
	WL_TRACE("wl%d: %s:\n", wlc_hw->unit, __func__);

	wlc_hw->up = true;
	wlc_phy_hw_state_upd(wlc_hw->band->pi, true);

	/* FULLY enable dynamic power control and d11 core interrupt */
	wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);
	ASSERT(wlc_hw->wlc->macintmask == 0);
	wl_intrson(wlc_hw->wlc->wl);
	return 0;
}

int wlc_bmac_down_prep(struct wlc_hw_info *wlc_hw)
{
	bool dev_gone;
	uint callbacks = 0;

	WL_TRACE("wl%d: %s:\n", wlc_hw->unit, __func__);

	if (!wlc_hw->up)
		return callbacks;

	dev_gone = DEVICEREMOVED(wlc_hw->wlc);

	/* disable interrupts */
	if (dev_gone)
		wlc_hw->wlc->macintmask = 0;
	else {
		/* now disable interrupts */
		wl_intrsoff(wlc_hw->wlc->wl);

		/* ensure we're running on the pll clock again */
		wlc_clkctl_clk(wlc_hw, CLK_FAST);
	}
	/* down phy at the last of this stage */
	callbacks += wlc_phy_down(wlc_hw->band->pi);

	return callbacks;
}

int wlc_bmac_down_finish(struct wlc_hw_info *wlc_hw)
{
	uint callbacks = 0;
	bool dev_gone;

	WL_TRACE("wl%d: %s:\n", wlc_hw->unit, __func__);

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
		wlc_flushqueues(wlc_hw->wlc);
	} else {

		/* Reset and disable the core */
		if (si_iscoreup(wlc_hw->sih)) {
			if (R_REG(&wlc_hw->regs->maccontrol) &
			    MCTL_EN_MAC)
				wlc_suspend_mac_and_wait(wlc_hw->wlc);
			callbacks += wl_reset(wlc_hw->wlc->wl);
			wlc_coredisable(wlc_hw);
		}

		/* turn off primary xtal and pll */
		if (!wlc_hw->noreset) {
			if (wlc_hw->sih->bustype == PCI_BUS)
				si_pci_down(wlc_hw->sih);
			wlc_bmac_xtal(wlc_hw, OFF);
		}
	}

	return callbacks;
}

void wlc_bmac_wait_for_wake(struct wlc_hw_info *wlc_hw)
{
	/* delay before first read of ucode state */
	udelay(40);

	/* wait until ucode is no longer asleep */
	SPINWAIT((wlc_bmac_read_shm(wlc_hw, M_UCODE_DBGST) ==
		  DBGST_ASLEEP), wlc_hw->wlc->fastpwrup_dly);

	ASSERT(wlc_bmac_read_shm(wlc_hw, M_UCODE_DBGST) != DBGST_ASLEEP);
}

void wlc_bmac_hw_etheraddr(struct wlc_hw_info *wlc_hw, u8 *ea)
{
	memcpy(ea, wlc_hw->etheraddr, ETH_ALEN);
}

static int wlc_bmac_bandtype(struct wlc_hw_info *wlc_hw)
{
	return wlc_hw->band->bandtype;
}

/* control chip clock to save power, enable dynamic clock or force fast clock */
static void wlc_clkctl_clk(struct wlc_hw_info *wlc_hw, uint mode)
{
	if (PMUCTL_ENAB(wlc_hw->sih)) {
		/* new chips with PMU, CCS_FORCEHT will distribute the HT clock on backplane,
		 *  but mac core will still run on ALP(not HT) when it enters powersave mode,
		 *      which means the FCA bit may not be set.
		 *      should wakeup mac if driver wants it to run on HT.
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
				ASSERT(R_REG
				       (&wlc_hw->regs->
					clk_ctl_st) & CCS_HTAVAIL);
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

		wlc_hw->forcefastclk = si_clkctl_cc(wlc_hw->sih, mode);

		/* check fast clock is available (if core is not in reset) */
		if (wlc_hw->forcefastclk && wlc_hw->clk)
			ASSERT(si_core_sflags(wlc_hw->sih, 0, 0) & SISF_FCLKA);

		/* keep the ucode wake bit on if forcefastclk is on
		 * since we do not want ucode to put us back to slow clock
		 * when it dozes for PM mode.
		 * Code below matches the wake override bit with current forcefastclk state
		 * Only setting bit in wake_override instead of waking ucode immediately
		 * since old code (wlc.c 1.4499) had this behavior. Older code set
		 * wlc->forcefastclk but only had the wake happen if the wakup_ucode work
		 * (protected by an up check) was executed just below.
		 */
		if (wlc_hw->forcefastclk)
			mboolset(wlc_hw->wake_override,
				 WLC_WAKE_OVERRIDE_FORCEFAST);
		else
			mboolclr(wlc_hw->wake_override,
				 WLC_WAKE_OVERRIDE_FORCEFAST);
	}
}

/* set initial host flags value */
static void
wlc_mhfdef(struct wlc_info *wlc, u16 *mhfs, u16 mhf2_init)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;

	memset(mhfs, 0, MHFMAX * sizeof(u16));

	mhfs[MHF2] |= mhf2_init;

	/* prohibit use of slowclock on multifunction boards */
	if (wlc_hw->boardflags & BFL_NOPLLDOWN)
		mhfs[MHF1] |= MHF1_FORCEFASTCLK;

	if (WLCISNPHY(wlc_hw->band) && NREV_LT(wlc_hw->band->phyrev, 2)) {
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
 * bands values are: WLC_BAND_AUTO <--- Current band only
 *                   WLC_BAND_5G   <--- 5G band only
 *                   WLC_BAND_2G   <--- 2G band only
 *                   WLC_BAND_ALL  <--- All bands
 */
void
wlc_bmac_mhf(struct wlc_hw_info *wlc_hw, u8 idx, u16 mask, u16 val,
	     int bands)
{
	u16 save;
	u16 addr[MHFMAX] = {
		M_HOST_FLAGS1, M_HOST_FLAGS2, M_HOST_FLAGS3, M_HOST_FLAGS4,
		M_HOST_FLAGS5
	};
	struct wlc_hwband *band;

	ASSERT((val & ~mask) == 0);
	ASSERT(idx < MHFMAX);
	ASSERT(ARRAY_SIZE(addr) == MHFMAX);

	switch (bands) {
		/* Current band only or all bands,
		 * then set the band to current band
		 */
	case WLC_BAND_AUTO:
	case WLC_BAND_ALL:
		band = wlc_hw->band;
		break;
	case WLC_BAND_5G:
		band = wlc_hw->bandstate[BAND_5G_INDEX];
		break;
	case WLC_BAND_2G:
		band = wlc_hw->bandstate[BAND_2G_INDEX];
		break;
	default:
		ASSERT(0);
		band = NULL;
	}

	if (band) {
		save = band->mhfs[idx];
		band->mhfs[idx] = (band->mhfs[idx] & ~mask) | val;

		/* optimization: only write through if changed, and
		 * changed band is the current band
		 */
		if (wlc_hw->clk && (band->mhfs[idx] != save)
		    && (band == wlc_hw->band))
			wlc_bmac_write_shm(wlc_hw, addr[idx],
					   (u16) band->mhfs[idx]);
	}

	if (bands == WLC_BAND_ALL) {
		wlc_hw->bandstate[0]->mhfs[idx] =
		    (wlc_hw->bandstate[0]->mhfs[idx] & ~mask) | val;
		wlc_hw->bandstate[1]->mhfs[idx] =
		    (wlc_hw->bandstate[1]->mhfs[idx] & ~mask) | val;
	}
}

u16 wlc_bmac_mhf_get(struct wlc_hw_info *wlc_hw, u8 idx, int bands)
{
	struct wlc_hwband *band;
	ASSERT(idx < MHFMAX);

	switch (bands) {
	case WLC_BAND_AUTO:
		band = wlc_hw->band;
		break;
	case WLC_BAND_5G:
		band = wlc_hw->bandstate[BAND_5G_INDEX];
		break;
	case WLC_BAND_2G:
		band = wlc_hw->bandstate[BAND_2G_INDEX];
		break;
	default:
		ASSERT(0);
		band = NULL;
	}

	if (!band)
		return 0;

	return band->mhfs[idx];
}

static void wlc_write_mhf(struct wlc_hw_info *wlc_hw, u16 *mhfs)
{
	u8 idx;
	u16 addr[] = {
		M_HOST_FLAGS1, M_HOST_FLAGS2, M_HOST_FLAGS3, M_HOST_FLAGS4,
		M_HOST_FLAGS5
	};

	ASSERT(ARRAY_SIZE(addr) == MHFMAX);

	for (idx = 0; idx < MHFMAX; idx++) {
		wlc_bmac_write_shm(wlc_hw, addr[idx], mhfs[idx]);
	}
}

/* set the maccontrol register to desired reset state and
 * initialize the sw cache of the register
 */
static void wlc_mctrl_reset(struct wlc_hw_info *wlc_hw)
{
	/* IHR accesses are always enabled, PSM disabled, HPS off and WAKE on */
	wlc_hw->maccontrol = 0;
	wlc_hw->suspended_fifos = 0;
	wlc_hw->wake_override = 0;
	wlc_hw->mute_override = 0;
	wlc_bmac_mctrl(wlc_hw, ~0, MCTL_IHR_EN | MCTL_WAKE);
}

/* set or clear maccontrol bits */
void wlc_bmac_mctrl(struct wlc_hw_info *wlc_hw, u32 mask, u32 val)
{
	u32 maccontrol;
	u32 new_maccontrol;

	ASSERT((val & ~mask) == 0);

	maccontrol = wlc_hw->maccontrol;
	new_maccontrol = (maccontrol & ~mask) | val;

	/* if the new maccontrol value is the same as the old, nothing to do */
	if (new_maccontrol == maccontrol)
		return;

	/* something changed, cache the new value */
	wlc_hw->maccontrol = new_maccontrol;

	/* write the new values with overrides applied */
	wlc_mctrl_write(wlc_hw);
}

/* write the software state of maccontrol and overrides to the maccontrol register */
static void wlc_mctrl_write(struct wlc_hw_info *wlc_hw)
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

void wlc_ucode_wake_override_set(struct wlc_hw_info *wlc_hw, u32 override_bit)
{
	ASSERT((wlc_hw->wake_override & override_bit) == 0);

	if (wlc_hw->wake_override || (wlc_hw->maccontrol & MCTL_WAKE)) {
		mboolset(wlc_hw->wake_override, override_bit);
		return;
	}

	mboolset(wlc_hw->wake_override, override_bit);

	wlc_mctrl_write(wlc_hw);
	wlc_bmac_wait_for_wake(wlc_hw);

	return;
}

void wlc_ucode_wake_override_clear(struct wlc_hw_info *wlc_hw, u32 override_bit)
{
	ASSERT(wlc_hw->wake_override & override_bit);

	mboolclr(wlc_hw->wake_override, override_bit);

	if (wlc_hw->wake_override || (wlc_hw->maccontrol & MCTL_WAKE))
		return;

	wlc_mctrl_write(wlc_hw);

	return;
}

/* When driver needs ucode to stop beaconing, it has to make sure that
 * MCTL_AP is clear and MCTL_INFRA is set
 * Mode           MCTL_AP        MCTL_INFRA
 * AP                1              1
 * STA               0              1 <--- This will ensure no beacons
 * IBSS              0              0
 */
static void wlc_ucode_mute_override_set(struct wlc_hw_info *wlc_hw)
{
	wlc_hw->mute_override = 1;

	/* if maccontrol already has AP == 0 and INFRA == 1 without this
	 * override, then there is no change to write
	 */
	if ((wlc_hw->maccontrol & (MCTL_AP | MCTL_INFRA)) == MCTL_INFRA)
		return;

	wlc_mctrl_write(wlc_hw);

	return;
}

/* Clear the override on AP and INFRA bits */
static void wlc_ucode_mute_override_clear(struct wlc_hw_info *wlc_hw)
{
	if (wlc_hw->mute_override == 0)
		return;

	wlc_hw->mute_override = 0;

	/* if maccontrol already has AP == 0 and INFRA == 1 without this
	 * override, then there is no change to write
	 */
	if ((wlc_hw->maccontrol & (MCTL_AP | MCTL_INFRA)) == MCTL_INFRA)
		return;

	wlc_mctrl_write(wlc_hw);
}

/*
 * Write a MAC address to the rcmta structure
 */
void
wlc_bmac_set_rcmta(struct wlc_hw_info *wlc_hw, int idx,
		   const u8 *addr)
{
	d11regs_t *regs = wlc_hw->regs;
	volatile u16 *objdata16 = (volatile u16 *)&regs->objdata;
	u32 mac_hm;
	u16 mac_l;

	WL_TRACE("wl%d: %s\n", wlc_hw->unit, __func__);

	mac_hm =
	    (addr[3] << 24) | (addr[2] << 16) |
	    (addr[1] << 8) | addr[0];
	mac_l = (addr[5] << 8) | addr[4];

	W_REG(&regs->objaddr, (OBJADDR_RCMTA_SEL | (idx * 2)));
	(void)R_REG(&regs->objaddr);
	W_REG(&regs->objdata, mac_hm);
	W_REG(&regs->objaddr, (OBJADDR_RCMTA_SEL | ((idx * 2) + 1)));
	(void)R_REG(&regs->objaddr);
	W_REG(objdata16, mac_l);
}

/*
 * Write a MAC address to the given match reg offset in the RXE match engine.
 */
void
wlc_bmac_set_addrmatch(struct wlc_hw_info *wlc_hw, int match_reg_offset,
		       const u8 *addr)
{
	d11regs_t *regs;
	u16 mac_l;
	u16 mac_m;
	u16 mac_h;

	WL_TRACE("wl%d: wlc_bmac_set_addrmatch\n", wlc_hw->unit);

	ASSERT(match_reg_offset < RCM_SIZE);

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
wlc_bmac_write_template_ram(struct wlc_hw_info *wlc_hw, int offset, int len,
			    void *buf)
{
	d11regs_t *regs;
	u32 word;
	bool be_bit;
#ifdef IL_BIGENDIAN
	volatile u16 *dptr = NULL;
#endif				/* IL_BIGENDIAN */
	WL_TRACE("wl%d: wlc_bmac_write_template_ram\n", wlc_hw->unit);

	regs = wlc_hw->regs;

	ASSERT(IS_ALIGNED(offset, sizeof(u32)));
	ASSERT(IS_ALIGNED(len, sizeof(u32)));
	ASSERT((offset & ~0xffff) == 0);

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

void wlc_bmac_set_cwmin(struct wlc_hw_info *wlc_hw, u16 newmin)
{
	wlc_hw->band->CWmin = newmin;

	W_REG(&wlc_hw->regs->objaddr, OBJADDR_SCR_SEL | S_DOT11_CWMIN);
	(void)R_REG(&wlc_hw->regs->objaddr);
	W_REG(&wlc_hw->regs->objdata, newmin);
}

void wlc_bmac_set_cwmax(struct wlc_hw_info *wlc_hw, u16 newmax)
{
	wlc_hw->band->CWmax = newmax;

	W_REG(&wlc_hw->regs->objaddr, OBJADDR_SCR_SEL | S_DOT11_CWMAX);
	(void)R_REG(&wlc_hw->regs->objaddr);
	W_REG(&wlc_hw->regs->objdata, newmax);
}

void wlc_bmac_bw_set(struct wlc_hw_info *wlc_hw, u16 bw)
{
	bool fastclk;

	/* request FAST clock if not on */
	fastclk = wlc_hw->forcefastclk;
	if (!fastclk)
		wlc_clkctl_clk(wlc_hw, CLK_FAST);

	wlc_phy_bw_state_set(wlc_hw->band->pi, bw);

	ASSERT(wlc_hw->clk);

	wlc_bmac_phy_reset(wlc_hw);
	wlc_phy_init(wlc_hw->band->pi, wlc_phy_chanspec_get(wlc_hw->band->pi));

	/* restore the clk */
	if (!fastclk)
		wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);
}

static void
wlc_write_hw_bcntemplate0(struct wlc_hw_info *wlc_hw, void *bcn, int len)
{
	d11regs_t *regs = wlc_hw->regs;

	wlc_bmac_write_template_ram(wlc_hw, T_BCN0_TPL_BASE, (len + 3) & ~3,
				    bcn);
	/* write beacon length to SCR */
	ASSERT(len < 65536);
	wlc_bmac_write_shm(wlc_hw, M_BCN0_FRM_BYTESZ, (u16) len);
	/* mark beacon0 valid */
	OR_REG(&regs->maccommand, MCMD_BCN0VLD);
}

static void
wlc_write_hw_bcntemplate1(struct wlc_hw_info *wlc_hw, void *bcn, int len)
{
	d11regs_t *regs = wlc_hw->regs;

	wlc_bmac_write_template_ram(wlc_hw, T_BCN1_TPL_BASE, (len + 3) & ~3,
				    bcn);
	/* write beacon length to SCR */
	ASSERT(len < 65536);
	wlc_bmac_write_shm(wlc_hw, M_BCN1_FRM_BYTESZ, (u16) len);
	/* mark beacon1 valid */
	OR_REG(&regs->maccommand, MCMD_BCN1VLD);
}

/* mac is assumed to be suspended at this point */
void
wlc_bmac_write_hw_bcntemplates(struct wlc_hw_info *wlc_hw, void *bcn, int len,
			       bool both)
{
	d11regs_t *regs = wlc_hw->regs;

	if (both) {
		wlc_write_hw_bcntemplate0(wlc_hw, bcn, len);
		wlc_write_hw_bcntemplate1(wlc_hw, bcn, len);
	} else {
		/* bcn 0 */
		if (!(R_REG(&regs->maccommand) & MCMD_BCN0VLD))
			wlc_write_hw_bcntemplate0(wlc_hw, bcn, len);
		/* bcn 1 */
		else if (!
			 (R_REG(&regs->maccommand) & MCMD_BCN1VLD))
			wlc_write_hw_bcntemplate1(wlc_hw, bcn, len);
		else		/* one template should always have been available */
			ASSERT(0);
	}
}

static void WLBANDINITFN(wlc_bmac_upd_synthpu) (struct wlc_hw_info *wlc_hw)
{
	u16 v;
	struct wlc_info *wlc = wlc_hw->wlc;
	/* update SYNTHPU_DLY */

	if (WLCISLCNPHY(wlc->band)) {
		v = SYNTHPU_DLY_LPPHY_US;
	} else if (WLCISNPHY(wlc->band) && (NREV_GE(wlc->band->phyrev, 3))) {
		v = SYNTHPU_DLY_NPHY_US;
	} else {
		v = SYNTHPU_DLY_BPHY_US;
	}

	wlc_bmac_write_shm(wlc_hw, M_SYNTHPU_DLY, v);
}

/* band-specific init */
static void
WLBANDINITFN(wlc_bmac_bsinit) (struct wlc_info *wlc, chanspec_t chanspec)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;

	WL_TRACE("wl%d: wlc_bmac_bsinit: bandunit %d\n",
		 wlc_hw->unit, wlc_hw->band->bandunit);

	/* sanity check */
	if (PHY_TYPE(R_REG(&wlc_hw->regs->phyversion)) !=
	    PHY_TYPE_LCNXN)
		ASSERT((uint)
		       PHY_TYPE(R_REG(&wlc_hw->regs->phyversion))
		       == wlc_hw->band->phytype);

	wlc_ucode_bsinit(wlc_hw);

	wlc_phy_init(wlc_hw->band->pi, chanspec);

	wlc_ucode_txant_set(wlc_hw);

	/* cwmin is band-specific, update hardware with value for current band */
	wlc_bmac_set_cwmin(wlc_hw, wlc_hw->band->CWmin);
	wlc_bmac_set_cwmax(wlc_hw, wlc_hw->band->CWmax);

	wlc_bmac_update_slot_timing(wlc_hw,
				    BAND_5G(wlc_hw->band->
					    bandtype) ? true : wlc_hw->
				    shortslot);

	/* write phytype and phyvers */
	wlc_bmac_write_shm(wlc_hw, M_PHYTYPE, (u16) wlc_hw->band->phytype);
	wlc_bmac_write_shm(wlc_hw, M_PHYVER, (u16) wlc_hw->band->phyrev);

	/* initialize the txphyctl1 rate table since shmem is shared between bands */
	wlc_upd_ofdm_pctl1_table(wlc_hw);

	wlc_bmac_upd_synthpu(wlc_hw);
}

static void wlc_bmac_core_phy_clk(struct wlc_hw_info *wlc_hw, bool clk)
{
	WL_TRACE("wl%d: wlc_bmac_core_phy_clk: clk %d\n", wlc_hw->unit, clk);

	wlc_hw->phyclk = clk;

	if (OFF == clk) {	/* clear gmode bit, put phy into reset */

		si_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_FGC | SICF_GMODE),
			       (SICF_PRST | SICF_FGC));
		udelay(1);
		si_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_FGC), SICF_PRST);
		udelay(1);

	} else {		/* take phy out of reset */

		si_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_FGC), SICF_FGC);
		udelay(1);
		si_core_cflags(wlc_hw->sih, (SICF_FGC), 0);
		udelay(1);

	}
}

/* Perform a soft reset of the PHY PLL */
void wlc_bmac_core_phypll_reset(struct wlc_hw_info *wlc_hw)
{
	WL_TRACE("wl%d: wlc_bmac_core_phypll_reset\n", wlc_hw->unit);

	si_corereg(wlc_hw->sih, SI_CC_IDX,
		   offsetof(chipcregs_t, chipcontrol_addr), ~0, 0);
	udelay(1);
	si_corereg(wlc_hw->sih, SI_CC_IDX,
		   offsetof(chipcregs_t, chipcontrol_data), 0x4, 0);
	udelay(1);
	si_corereg(wlc_hw->sih, SI_CC_IDX,
		   offsetof(chipcregs_t, chipcontrol_data), 0x4, 4);
	udelay(1);
	si_corereg(wlc_hw->sih, SI_CC_IDX,
		   offsetof(chipcregs_t, chipcontrol_data), 0x4, 0);
	udelay(1);
}

/* light way to turn on phy clock without reset for NPHY only
 *  refer to wlc_bmac_core_phy_clk for full version
 */
void wlc_bmac_phyclk_fgc(struct wlc_hw_info *wlc_hw, bool clk)
{
	/* support(necessary for NPHY and HYPHY) only */
	if (!WLCISNPHY(wlc_hw->band))
		return;

	if (ON == clk)
		si_core_cflags(wlc_hw->sih, SICF_FGC, SICF_FGC);
	else
		si_core_cflags(wlc_hw->sih, SICF_FGC, 0);

}

void wlc_bmac_macphyclk_set(struct wlc_hw_info *wlc_hw, bool clk)
{
	if (ON == clk)
		si_core_cflags(wlc_hw->sih, SICF_MPCLKE, SICF_MPCLKE);
	else
		si_core_cflags(wlc_hw->sih, SICF_MPCLKE, 0);
}

void wlc_bmac_phy_reset(struct wlc_hw_info *wlc_hw)
{
	wlc_phy_t *pih = wlc_hw->band->pi;
	u32 phy_bw_clkbits;
	bool phy_in_reset = false;

	WL_TRACE("wl%d: wlc_bmac_phy_reset\n", wlc_hw->unit);

	if (pih == NULL)
		return;

	phy_bw_clkbits = wlc_phy_clk_bwbits(wlc_hw->band->pi);

	/* Specific reset sequence required for NPHY rev 3 and 4 */
	if (WLCISNPHY(wlc_hw->band) && NREV_GE(wlc_hw->band->phyrev, 3) &&
	    NREV_LE(wlc_hw->band->phyrev, 4)) {
		/* Set the PHY bandwidth */
		si_core_cflags(wlc_hw->sih, SICF_BWMASK, phy_bw_clkbits);

		udelay(1);

		/* Perform a soft reset of the PHY PLL */
		wlc_bmac_core_phypll_reset(wlc_hw);

		/* reset the PHY */
		si_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_PCLKE),
			       (SICF_PRST | SICF_PCLKE));
		phy_in_reset = true;
	} else {

		si_core_cflags(wlc_hw->sih,
			       (SICF_PRST | SICF_PCLKE | SICF_BWMASK),
			       (SICF_PRST | SICF_PCLKE | phy_bw_clkbits));
	}

	udelay(2);
	wlc_bmac_core_phy_clk(wlc_hw, ON);

	if (pih)
		wlc_phy_anacore(pih, ON);
}

/* switch to and initialize new band */
static void
WLBANDINITFN(wlc_bmac_setband) (struct wlc_hw_info *wlc_hw, uint bandunit,
				chanspec_t chanspec) {
	struct wlc_info *wlc = wlc_hw->wlc;
	u32 macintmask;

	ASSERT(NBANDS_HW(wlc_hw) > 1);
	ASSERT(bandunit != wlc_hw->band->bandunit);

	/* Enable the d11 core before accessing it */
	if (!si_iscoreup(wlc_hw->sih)) {
		si_core_reset(wlc_hw->sih, 0, 0);
		ASSERT(si_iscoreup(wlc_hw->sih));
		wlc_mctrl_reset(wlc_hw);
	}

	macintmask = wlc_setband_inact(wlc, bandunit);

	if (!wlc_hw->up)
		return;

	wlc_bmac_core_phy_clk(wlc_hw, ON);

	/* band-specific initializations */
	wlc_bmac_bsinit(wlc, chanspec);

	/*
	 * If there are any pending software interrupt bits,
	 * then replace these with a harmless nonzero value
	 * so wlc_dpc() will re-enable interrupts when done.
	 */
	if (wlc->macintstatus)
		wlc->macintstatus = MI_DMAINT;

	/* restore macintmask */
	wl_intrsrestore(wlc->wl, macintmask);

	/* ucode should still be suspended.. */
	ASSERT((R_REG(&wlc_hw->regs->maccontrol) & MCTL_EN_MAC) ==
	       0);
}

/* low-level band switch utility routine */
void WLBANDINITFN(wlc_setxband) (struct wlc_hw_info *wlc_hw, uint bandunit)
{
	WL_TRACE("wl%d: wlc_setxband: bandunit %d\n", wlc_hw->unit, bandunit);

	wlc_hw->band = wlc_hw->bandstate[bandunit];

	/* BMAC_NOTE: until we eliminate need for wlc->band refs in low level code */
	wlc_hw->wlc->band = wlc_hw->wlc->bandstate[bandunit];

	/* set gmode core flag */
	if (wlc_hw->sbclk && !wlc_hw->noreset) {
		si_core_cflags(wlc_hw->sih, SICF_GMODE,
			       ((bandunit == 0) ? SICF_GMODE : 0));
	}
}

static bool wlc_isgoodchip(struct wlc_hw_info *wlc_hw)
{

	/* reject unsupported corerev */
	if (!VALID_COREREV(wlc_hw->corerev)) {
		WL_ERROR("unsupported core rev %d\n", wlc_hw->corerev);
		return false;
	}

	return true;
}

static bool wlc_validboardtype(struct wlc_hw_info *wlc_hw)
{
	bool goodboard = true;
	uint boardrev = wlc_hw->boardrev;

	if (boardrev == 0)
		goodboard = false;
	else if (boardrev > 0xff) {
		uint brt = (boardrev & 0xf000) >> 12;
		uint b0 = (boardrev & 0xf00) >> 8;
		uint b1 = (boardrev & 0xf0) >> 4;
		uint b2 = boardrev & 0xf;

		if ((brt > 2) || (brt == 0) || (b0 > 9) || (b0 == 0) || (b1 > 9)
		    || (b2 > 9))
			goodboard = false;
	}

	if (wlc_hw->sih->boardvendor != VENDOR_BROADCOM)
		return goodboard;

	return goodboard;
}

static char *wlc_get_macaddr(struct wlc_hw_info *wlc_hw)
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
	if (macaddr == NULL) {
		WL_ERROR("wl%d: wlc_get_macaddr: macaddr getvar(%s) not found\n",
			 wlc_hw->unit, varname);
	}

	return macaddr;
}

/*
 * Return true if radio is disabled, otherwise false.
 * hw radio disable signal is an external pin, users activate it asynchronously
 * this function could be called when driver is down and w/o clock
 * it operates on different registers depending on corerev and boardflag.
 */
bool wlc_bmac_radio_read_hwdisabled(struct wlc_hw_info *wlc_hw)
{
	bool v, clk, xtal;
	u32 resetbits = 0, flags = 0;

	xtal = wlc_hw->sbclk;
	if (!xtal)
		wlc_bmac_xtal(wlc_hw, ON);

	/* may need to take core out of reset first */
	clk = wlc_hw->clk;
	if (!clk) {
		/*
		 * mac no longer enables phyclk automatically when driver
		 * accesses phyreg throughput mac. This can be skipped since
		 * only mac reg is accessed below
		 */
		flags |= SICF_PCLKE;

		/* AI chip doesn't restore bar0win2 on hibernation/resume, need sw fixup */
		if ((wlc_hw->sih->chip == BCM43224_CHIP_ID) ||
		    (wlc_hw->sih->chip == BCM43225_CHIP_ID) ||
		    (wlc_hw->sih->chip == BCM43421_CHIP_ID))
			wlc_hw->regs =
			    (d11regs_t *) si_setcore(wlc_hw->sih, D11_CORE_ID,
						     0);
		si_core_reset(wlc_hw->sih, flags, resetbits);
		wlc_mctrl_reset(wlc_hw);
	}

	v = ((R_REG(&wlc_hw->regs->phydebug) & PDBG_RFD) != 0);

	/* put core back into reset */
	if (!clk)
		si_core_disable(wlc_hw->sih, 0);

	if (!xtal)
		wlc_bmac_xtal(wlc_hw, OFF);

	return v;
}

/* Initialize just the hardware when coming out of POR or S3/S5 system states */
void wlc_bmac_hw_up(struct wlc_hw_info *wlc_hw)
{
	if (wlc_hw->wlc->pub->hw_up)
		return;

	WL_TRACE("wl%d: %s:\n", wlc_hw->unit, __func__);

	/*
	 * Enable pll and xtal, initialize the power control registers,
	 * and force fastclock for the remainder of wlc_up().
	 */
	wlc_bmac_xtal(wlc_hw, ON);
	si_clkctl_init(wlc_hw->sih);
	wlc_clkctl_clk(wlc_hw, CLK_FAST);

	if (wlc_hw->sih->bustype == PCI_BUS) {
		si_pci_fixcfg(wlc_hw->sih);

		/* AI chip doesn't restore bar0win2 on hibernation/resume, need sw fixup */
		if ((wlc_hw->sih->chip == BCM43224_CHIP_ID) ||
		    (wlc_hw->sih->chip == BCM43225_CHIP_ID) ||
		    (wlc_hw->sih->chip == BCM43421_CHIP_ID))
			wlc_hw->regs =
			    (d11regs_t *) si_setcore(wlc_hw->sih, D11_CORE_ID,
						     0);
	}

	/* Inform phy that a POR reset has occurred so it does a complete phy init */
	wlc_phy_por_inform(wlc_hw->band->pi);

	wlc_hw->ucode_loaded = false;
	wlc_hw->wlc->pub->hw_up = true;

	if ((wlc_hw->boardflags & BFL_FEM)
	    && (wlc_hw->sih->chip == BCM4313_CHIP_ID)) {
		if (!
		    (wlc_hw->boardrev >= 0x1250
		     && (wlc_hw->boardflags & BFL_FEM_BT)))
			si_epa_4313war(wlc_hw->sih);
	}
}

static bool wlc_dma_rxreset(struct wlc_hw_info *wlc_hw, uint fifo)
{
	struct hnddma_pub *di = wlc_hw->di[fifo];
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
void wlc_bmac_corereset(struct wlc_hw_info *wlc_hw, u32 flags)
{
	d11regs_t *regs;
	uint i;
	bool fastclk;
	u32 resetbits = 0;

	if (flags == WLC_USE_COREFLAGS)
		flags = (wlc_hw->band->pi ? wlc_hw->band->core_flags : 0);

	WL_TRACE("wl%d: %s\n", wlc_hw->unit, __func__);

	regs = wlc_hw->regs;

	/* request FAST clock if not on  */
	fastclk = wlc_hw->forcefastclk;
	if (!fastclk)
		wlc_clkctl_clk(wlc_hw, CLK_FAST);

	/* reset the dma engines except first time thru */
	if (si_iscoreup(wlc_hw->sih)) {
		for (i = 0; i < NFIFO; i++)
			if ((wlc_hw->di[i]) && (!dma_txreset(wlc_hw->di[i]))) {
				WL_ERROR("wl%d: %s: dma_txreset[%d]: cannot stop dma\n",
					 wlc_hw->unit, __func__, i);
			}

		if ((wlc_hw->di[RX_FIFO])
		    && (!wlc_dma_rxreset(wlc_hw, RX_FIFO))) {
			WL_ERROR("wl%d: %s: dma_rxreset[%d]: cannot stop dma\n",
				 wlc_hw->unit, __func__, RX_FIFO);
		}
	}
	/* if noreset, just stop the psm and return */
	if (wlc_hw->noreset) {
		wlc_hw->wlc->macintstatus = 0;	/* skip wl_dpc after down */
		wlc_bmac_mctrl(wlc_hw, MCTL_PSM_RUN | MCTL_EN_MAC, 0);
		return;
	}

	/*
	 * mac no longer enables phyclk automatically when driver accesses
	 * phyreg throughput mac, AND phy_reset is skipped at early stage when
	 * band->pi is invalid. need to enable PHY CLK
	 */
	flags |= SICF_PCLKE;

	/* reset the core
	 * In chips with PMU, the fastclk request goes through d11 core reg 0x1e0, which
	 *  is cleared by the core_reset. have to re-request it.
	 *  This adds some delay and we can optimize it by also requesting fastclk through
	 *  chipcommon during this period if necessary. But that has to work coordinate
	 *  with other driver like mips/arm since they may touch chipcommon as well.
	 */
	wlc_hw->clk = false;
	si_core_reset(wlc_hw->sih, flags, resetbits);
	wlc_hw->clk = true;
	if (wlc_hw->band && wlc_hw->band->pi)
		wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, true);

	wlc_mctrl_reset(wlc_hw);

	if (PMUCTL_ENAB(wlc_hw->sih))
		wlc_clkctl_clk(wlc_hw, CLK_FAST);

	wlc_bmac_phy_reset(wlc_hw);

	/* turn on PHY_PLL */
	wlc_bmac_core_phypll_ctl(wlc_hw, true);

	/* clear sw intstatus */
	wlc_hw->wlc->macintstatus = 0;

	/* restore the clk setting */
	if (!fastclk)
		wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);
}

/* txfifo sizes needs to be modified(increased) since the newer cores
 * have more memory.
 */
static void wlc_corerev_fifofixup(struct wlc_hw_info *wlc_hw)
{
	d11regs_t *regs = wlc_hw->regs;
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
	wlc_bmac_write_shm(wlc_hw, M_FIFOSIZE0,
			   wlc_hw->xmtfifo_sz[TX_AC_BE_FIFO]);
	wlc_bmac_write_shm(wlc_hw, M_FIFOSIZE1,
			   wlc_hw->xmtfifo_sz[TX_AC_VI_FIFO]);
	wlc_bmac_write_shm(wlc_hw, M_FIFOSIZE2,
			   ((wlc_hw->xmtfifo_sz[TX_AC_VO_FIFO] << 8) | wlc_hw->
			    xmtfifo_sz[TX_AC_BK_FIFO]));
	wlc_bmac_write_shm(wlc_hw, M_FIFOSIZE3,
			   ((wlc_hw->xmtfifo_sz[TX_ATIM_FIFO] << 8) | wlc_hw->
			    xmtfifo_sz[TX_BCMC_FIFO]));
}

/* d11 core init
 *   reset PSM
 *   download ucode/PCM
 *   let ucode run to suspended
 *   download ucode inits
 *   config other core registers
 *   init dma
 */
static void wlc_coreinit(struct wlc_info *wlc)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
	d11regs_t *regs;
	u32 sflags;
	uint bcnint_us;
	uint i = 0;
	bool fifosz_fixup = false;
	int err = 0;
	u16 buf[NFIFO];

	regs = wlc_hw->regs;

	WL_TRACE("wl%d: wlc_coreinit\n", wlc_hw->unit);

	/* reset PSM */
	wlc_bmac_mctrl(wlc_hw, ~0, (MCTL_IHR_EN | MCTL_PSM_JMP_0 | MCTL_WAKE));

	wlc_ucode_download(wlc_hw);
	/*
	 * FIFOSZ fixup. driver wants to controls the fifo allocation.
	 */
	fifosz_fixup = true;

	/* let the PSM run to the suspended state, set mode to BSS STA */
	W_REG(&regs->macintstatus, -1);
	wlc_bmac_mctrl(wlc_hw, ~0,
		       (MCTL_IHR_EN | MCTL_INFRA | MCTL_PSM_RUN | MCTL_WAKE));

	/* wait for ucode to self-suspend after auto-init */
	SPINWAIT(((R_REG(&regs->macintstatus) & MI_MACSSPNDD) == 0),
		 1000 * 1000);
	if ((R_REG(&regs->macintstatus) & MI_MACSSPNDD) == 0)
		WL_ERROR("wl%d: wlc_coreinit: ucode did not self-suspend!\n",
			 wlc_hw->unit);

	wlc_gpio_init(wlc);

	sflags = si_core_sflags(wlc_hw->sih, 0, 0);

	if (D11REV_IS(wlc_hw->corerev, 23)) {
		if (WLCISNPHY(wlc_hw->band))
			wlc_write_inits(wlc_hw, d11n0initvals16);
		else
			WL_ERROR("%s: wl%d: unsupported phy in corerev %d\n",
				 __func__, wlc_hw->unit, wlc_hw->corerev);
	} else if (D11REV_IS(wlc_hw->corerev, 24)) {
		if (WLCISLCNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11lcn0initvals24);
		} else {
			WL_ERROR("%s: wl%d: unsupported phy in corerev %d\n",
				 __func__, wlc_hw->unit, wlc_hw->corerev);
		}
	} else {
		WL_ERROR("%s: wl%d: unsupported corerev %d\n",
			 __func__, wlc_hw->unit, wlc_hw->corerev);
	}

	/* For old ucode, txfifo sizes needs to be modified(increased) */
	if (fifosz_fixup == true) {
		wlc_corerev_fifofixup(wlc_hw);
	}

	/* check txfifo allocations match between ucode and driver */
	buf[TX_AC_BE_FIFO] = wlc_bmac_read_shm(wlc_hw, M_FIFOSIZE0);
	if (buf[TX_AC_BE_FIFO] != wlc_hw->xmtfifo_sz[TX_AC_BE_FIFO]) {
		i = TX_AC_BE_FIFO;
		err = -1;
	}
	buf[TX_AC_VI_FIFO] = wlc_bmac_read_shm(wlc_hw, M_FIFOSIZE1);
	if (buf[TX_AC_VI_FIFO] != wlc_hw->xmtfifo_sz[TX_AC_VI_FIFO]) {
		i = TX_AC_VI_FIFO;
		err = -1;
	}
	buf[TX_AC_BK_FIFO] = wlc_bmac_read_shm(wlc_hw, M_FIFOSIZE2);
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
	buf[TX_BCMC_FIFO] = wlc_bmac_read_shm(wlc_hw, M_FIFOSIZE3);
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
	if (err != 0) {
		WL_ERROR("wlc_coreinit: txfifo mismatch: ucode size %d driver size %d index %d\n",
			 buf[i], wlc_hw->xmtfifo_sz[i], i);
		ASSERT(0);
	}

	/* make sure we can still talk to the mac */
	ASSERT(R_REG(&regs->maccontrol) != 0xffffffff);

	/* band-specific inits done by wlc_bsinit() */

	/* Set up frame burst size and antenna swap threshold init values */
	wlc_bmac_write_shm(wlc_hw, M_MBURST_SIZE, MAXTXFRAMEBURST);
	wlc_bmac_write_shm(wlc_hw, M_MAX_ANTCNT, ANTCNT);

	/* enable one rx interrupt per received frame */
	W_REG(&regs->intrcvlazy[0], (1 << IRL_FC_SHIFT));

	/* set the station mode (BSS STA) */
	wlc_bmac_mctrl(wlc_hw,
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
	wlc_bmac_macphyclk_set(wlc_hw, ON);

	/* program dynamic clock control fast powerup delay register */
	wlc->fastpwrup_dly = si_clkctl_fast_pwrup_delay(wlc_hw->sih);
	W_REG(&regs->scc_fastpwrup_dly, wlc->fastpwrup_dly);

	/* tell the ucode the corerev */
	wlc_bmac_write_shm(wlc_hw, M_MACHW_VER, (u16) wlc_hw->corerev);

	/* tell the ucode MAC capabilities */
	wlc_bmac_write_shm(wlc_hw, M_MACHW_CAP_L,
			   (u16) (wlc_hw->machwcap & 0xffff));
	wlc_bmac_write_shm(wlc_hw, M_MACHW_CAP_H,
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
	wlc_bmac_write_shm(wlc_hw, M_SFRMTXCNTFBRTHSD, wlc_hw->SFBL);
	wlc_bmac_write_shm(wlc_hw, M_LFRMTXCNTFBRTHSD, wlc_hw->LFBL);

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

void wlc_bmac_switch_macfreq(struct wlc_hw_info *wlc_hw, u8 spurmode)
{
	d11regs_t *regs;
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
	} else if (WLCISLCNPHY(wlc_hw->band)) {
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
static void wlc_gpio_init(struct wlc_info *wlc)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
	d11regs_t *regs;
	u32 gc, gm;

	regs = wlc_hw->regs;

	/* use GPIO select 0 to get all gpio signals from the gpio out reg */
	wlc_bmac_mctrl(wlc_hw, MCTL_GPOUT_SEL_MASK, 0);

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
		wlc_bmac_mhf(wlc_hw, MHF3, MHF3_ANTSEL_EN,
			     MHF3_ANTSEL_EN, WLC_BAND_ALL);
		wlc_bmac_mhf(wlc_hw, MHF3, MHF3_ANTSEL_MODE,
			     MHF3_ANTSEL_MODE, WLC_BAND_ALL);

		/* init superswitch control */
		wlc_phy_antsel_init(wlc_hw->band->pi, false);

	} else if (wlc_hw->antsel_type == ANTSEL_2x4) {
		ASSERT((gm & BOARD_GPIO_12) == 0);
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
		wlc_bmac_mhf(wlc_hw, MHF3, MHF3_ANTSEL_EN,
			     MHF3_ANTSEL_EN, WLC_BAND_ALL);
		wlc_bmac_mhf(wlc_hw, MHF3, MHF3_ANTSEL_MODE, 0,
			     WLC_BAND_ALL);

		/* Configure the desired clock to be 4Mhz */
		wlc_bmac_write_shm(wlc_hw, M_ANTSEL_CLKDIV,
				   ANTSEL_CLKDIV_4MHZ);
	}

	/* gpio 9 controls the PA.  ucode is responsible for wiggling out and oe */
	if (wlc_hw->boardflags & BFL_PACTRL)
		gm |= gc |= BOARD_GPIO_PACTRL;

	/* apply to gpiocontrol register */
	si_gpiocontrol(wlc_hw->sih, gm, gc, GPIO_DRV_PRIORITY);
}

static void wlc_ucode_download(struct wlc_hw_info *wlc_hw)
{
	struct wlc_info *wlc;
	wlc = wlc_hw->wlc;

	if (wlc_hw->ucode_loaded)
		return;

	if (D11REV_IS(wlc_hw->corerev, 23)) {
		if (WLCISNPHY(wlc_hw->band)) {
			wlc_ucode_write(wlc_hw, bcm43xx_16_mimo,
					bcm43xx_16_mimosz);
			wlc_hw->ucode_loaded = true;
		} else
			WL_ERROR("%s: wl%d: unsupported phy in corerev %d\n",
				 __func__, wlc_hw->unit, wlc_hw->corerev);
	} else if (D11REV_IS(wlc_hw->corerev, 24)) {
		if (WLCISLCNPHY(wlc_hw->band)) {
			wlc_ucode_write(wlc_hw, bcm43xx_24_lcn,
					bcm43xx_24_lcnsz);
			wlc_hw->ucode_loaded = true;
		} else {
			WL_ERROR("%s: wl%d: unsupported phy in corerev %d\n",
				 __func__, wlc_hw->unit, wlc_hw->corerev);
		}
	}
}

static void wlc_ucode_write(struct wlc_hw_info *wlc_hw, const u32 ucode[],
			      const uint nbytes) {
	d11regs_t *regs = wlc_hw->regs;
	uint i;
	uint count;

	WL_TRACE("wl%d: wlc_ucode_write\n", wlc_hw->unit);

	ASSERT(IS_ALIGNED(nbytes, sizeof(u32)));

	count = (nbytes / sizeof(u32));

	W_REG(&regs->objaddr, (OBJADDR_AUTO_INC | OBJADDR_UCM_SEL));
	(void)R_REG(&regs->objaddr);
	for (i = 0; i < count; i++)
		W_REG(&regs->objdata, ucode[i]);
}

static void wlc_write_inits(struct wlc_hw_info *wlc_hw,
			    const struct d11init *inits)
{
	int i;
	volatile u8 *base;

	WL_TRACE("wl%d: wlc_write_inits\n", wlc_hw->unit);

	base = (volatile u8 *)wlc_hw->regs;

	for (i = 0; inits[i].addr != 0xffff; i++) {
		ASSERT((inits[i].size == 2) || (inits[i].size == 4));

		if (inits[i].size == 2)
			W_REG((u16 *)(base + inits[i].addr),
			      inits[i].value);
		else if (inits[i].size == 4)
			W_REG((u32 *)(base + inits[i].addr),
			      inits[i].value);
	}
}

static void wlc_ucode_txant_set(struct wlc_hw_info *wlc_hw)
{
	u16 phyctl;
	u16 phytxant = wlc_hw->bmac_phytxant;
	u16 mask = PHY_TXC_ANT_MASK;

	/* set the Probe Response frame phy control word */
	phyctl = wlc_bmac_read_shm(wlc_hw, M_CTXPRS_BLK + C_CTX_PCTLWD_POS);
	phyctl = (phyctl & ~mask) | phytxant;
	wlc_bmac_write_shm(wlc_hw, M_CTXPRS_BLK + C_CTX_PCTLWD_POS, phyctl);

	/* set the Response (ACK/CTS) frame phy control word */
	phyctl = wlc_bmac_read_shm(wlc_hw, M_RSP_PCTLWD);
	phyctl = (phyctl & ~mask) | phytxant;
	wlc_bmac_write_shm(wlc_hw, M_RSP_PCTLWD, phyctl);
}

void wlc_bmac_txant_set(struct wlc_hw_info *wlc_hw, u16 phytxant)
{
	/* update sw state */
	wlc_hw->bmac_phytxant = phytxant;

	/* push to ucode if up */
	if (!wlc_hw->up)
		return;
	wlc_ucode_txant_set(wlc_hw);

}

u16 wlc_bmac_get_txant(struct wlc_hw_info *wlc_hw)
{
	return (u16) wlc_hw->wlc->stf->txant;
}

void wlc_bmac_antsel_type_set(struct wlc_hw_info *wlc_hw, u8 antsel_type)
{
	wlc_hw->antsel_type = antsel_type;

	/* Update the antsel type for phy module to use */
	wlc_phy_antsel_type_set(wlc_hw->band->pi, antsel_type);
}

void wlc_bmac_fifoerrors(struct wlc_hw_info *wlc_hw)
{
	bool fatal = false;
	uint unit;
	uint intstatus, idx;
	d11regs_t *regs = wlc_hw->regs;

	unit = wlc_hw->unit;

	for (idx = 0; idx < NFIFO; idx++) {
		/* read intstatus register and ignore any non-error bits */
		intstatus =
		    R_REG(&regs->intctrlregs[idx].intstatus) & I_ERRORS;
		if (!intstatus)
			continue;

		WL_TRACE("wl%d: wlc_bmac_fifoerrors: intstatus%d 0x%x\n",
			 unit, idx, intstatus);

		if (intstatus & I_RO) {
			WL_ERROR("wl%d: fifo %d: receive fifo overflow\n",
				 unit, idx);
			wlc_hw->wlc->pub->_cnt->rxoflo++;
			fatal = true;
		}

		if (intstatus & I_PC) {
			WL_ERROR("wl%d: fifo %d: descriptor error\n",
				 unit, idx);
			wlc_hw->wlc->pub->_cnt->dmade++;
			fatal = true;
		}

		if (intstatus & I_PD) {
			WL_ERROR("wl%d: fifo %d: data error\n", unit, idx);
			wlc_hw->wlc->pub->_cnt->dmada++;
			fatal = true;
		}

		if (intstatus & I_DE) {
			WL_ERROR("wl%d: fifo %d: descriptor protocol error\n",
				 unit, idx);
			wlc_hw->wlc->pub->_cnt->dmape++;
			fatal = true;
		}

		if (intstatus & I_RU) {
			WL_ERROR("wl%d: fifo %d: receive descriptor underflow\n",
				 idx, unit);
			wlc_hw->wlc->pub->_cnt->rxuflo[idx]++;
		}

		if (intstatus & I_XU) {
			WL_ERROR("wl%d: fifo %d: transmit fifo underflow\n",
				 idx, unit);
			wlc_hw->wlc->pub->_cnt->txuflo++;
			fatal = true;
		}

		if (fatal) {
			wlc_fatal_error(wlc_hw->wlc);	/* big hammer */
			break;
		} else
			W_REG(&regs->intctrlregs[idx].intstatus,
			      intstatus);
	}
}

void wlc_intrson(struct wlc_info *wlc)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
	ASSERT(wlc->defmacintmask);
	wlc->macintmask = wlc->defmacintmask;
	W_REG(&wlc_hw->regs->macintmask, wlc->macintmask);
}

/* callback for siutils.c, which has only wlc handler, no wl
 * they both check up, not only because there is no need to off/restore d11 interrupt
 *  but also because per-port code may require sync with valid interrupt.
 */

static u32 wlc_wlintrsoff(struct wlc_info *wlc)
{
	if (!wlc->hw->up)
		return 0;

	return wl_intrsoff(wlc->wl);
}

static void wlc_wlintrsrestore(struct wlc_info *wlc, u32 macintmask)
{
	if (!wlc->hw->up)
		return;

	wl_intrsrestore(wlc->wl, macintmask);
}

u32 wlc_intrsoff(struct wlc_info *wlc)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
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

void wlc_intrsrestore(struct wlc_info *wlc, u32 macintmask)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
	if (!wlc_hw->clk)
		return;

	wlc->macintmask = macintmask;
	W_REG(&wlc_hw->regs->macintmask, wlc->macintmask);
}

static void wlc_bmac_mute(struct wlc_hw_info *wlc_hw, bool on, mbool flags)
{
	u8 null_ether_addr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

	if (on) {
		/* suspend tx fifos */
		wlc_bmac_tx_fifo_suspend(wlc_hw, TX_DATA_FIFO);
		wlc_bmac_tx_fifo_suspend(wlc_hw, TX_CTL_FIFO);
		wlc_bmac_tx_fifo_suspend(wlc_hw, TX_AC_BK_FIFO);
		wlc_bmac_tx_fifo_suspend(wlc_hw, TX_AC_VI_FIFO);

		/* zero the address match register so we do not send ACKs */
		wlc_bmac_set_addrmatch(wlc_hw, RCM_MAC_OFFSET,
				       null_ether_addr);
	} else {
		/* resume tx fifos */
		if (!wlc_hw->wlc->tx_suspended) {
			wlc_bmac_tx_fifo_resume(wlc_hw, TX_DATA_FIFO);
		}
		wlc_bmac_tx_fifo_resume(wlc_hw, TX_CTL_FIFO);
		wlc_bmac_tx_fifo_resume(wlc_hw, TX_AC_BK_FIFO);
		wlc_bmac_tx_fifo_resume(wlc_hw, TX_AC_VI_FIFO);

		/* Restore address */
		wlc_bmac_set_addrmatch(wlc_hw, RCM_MAC_OFFSET,
				       wlc_hw->etheraddr);
	}

	wlc_phy_mute_upd(wlc_hw->band->pi, on, flags);

	if (on)
		wlc_ucode_mute_override_set(wlc_hw);
	else
		wlc_ucode_mute_override_clear(wlc_hw);
}

int wlc_bmac_xmtfifo_sz_get(struct wlc_hw_info *wlc_hw, uint fifo, uint *blocks)
{
	if (fifo >= NFIFO)
		return BCME_RANGE;

	*blocks = wlc_hw->xmtfifo_sz[fifo];

	return 0;
}

/* wlc_bmac_tx_fifo_suspended:
 * Check the MAC's tx suspend status for a tx fifo.
 *
 * When the MAC acknowledges a tx suspend, it indicates that no more
 * packets will be transmitted out the radio. This is independent of
 * DMA channel suspension---the DMA may have finished suspending, or may still
 * be pulling data into a tx fifo, by the time the MAC acks the suspend
 * request.
 */
static bool wlc_bmac_tx_fifo_suspended(struct wlc_hw_info *wlc_hw, uint tx_fifo)
{
	/* check that a suspend has been requested and is no longer pending */

	/*
	 * for DMA mode, the suspend request is set in xmtcontrol of the DMA engine,
	 * and the tx fifo suspend at the lower end of the MAC is acknowledged in the
	 * chnstatus register.
	 * The tx fifo suspend completion is independent of the DMA suspend completion and
	 *   may be acked before or after the DMA is suspended.
	 */
	if (dma_txsuspended(wlc_hw->di[tx_fifo]) &&
	    (R_REG(&wlc_hw->regs->chnstatus) &
	     (1 << tx_fifo)) == 0)
		return true;

	return false;
}

static void wlc_bmac_tx_fifo_suspend(struct wlc_hw_info *wlc_hw, uint tx_fifo)
{
	u8 fifo = 1 << tx_fifo;

	/* Two clients of this code, 11h Quiet period and scanning. */

	/* only suspend if not already suspended */
	if ((wlc_hw->suspended_fifos & fifo) == fifo)
		return;

	/* force the core awake only if not already */
	if (wlc_hw->suspended_fifos == 0)
		wlc_ucode_wake_override_set(wlc_hw, WLC_WAKE_OVERRIDE_TXFIFO);

	wlc_hw->suspended_fifos |= fifo;

	if (wlc_hw->di[tx_fifo]) {
		/* Suspending AMPDU transmissions in the middle can cause underflow
		 * which may result in mismatch between ucode and driver
		 * so suspend the mac before suspending the FIFO
		 */
		if (WLC_PHY_11N_CAP(wlc_hw->band))
			wlc_suspend_mac_and_wait(wlc_hw->wlc);

		dma_txsuspend(wlc_hw->di[tx_fifo]);

		if (WLC_PHY_11N_CAP(wlc_hw->band))
			wlc_enable_mac(wlc_hw->wlc);
	}
}

static void wlc_bmac_tx_fifo_resume(struct wlc_hw_info *wlc_hw, uint tx_fifo)
{
	/* BMAC_NOTE: WLC_TX_FIFO_ENAB is done in wlc_dpc() for DMA case but need to be done
	 * here for PIO otherwise the watchdog will catch the inconsistency and fire
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
			wlc_ucode_wake_override_clear(wlc_hw,
						      WLC_WAKE_OVERRIDE_TXFIFO);
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
static inline u32 wlc_intstatus(struct wlc_info *wlc, bool in_isr)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
	d11regs_t *regs = wlc_hw->regs;
	u32 macintstatus;

	/* macintstatus includes a DMA interrupt summary bit */
	macintstatus = R_REG(&regs->macintstatus);

	WL_TRACE("wl%d: macintstatus: 0x%x\n", wlc_hw->unit, macintstatus);

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
	if (macintstatus & MI_DMAINT) {
		/*
		 * only fifo interrupt enabled is I_RI in
		 * RX_FIFO. If MI_DMAINT is set, assume it
		 * is set and clear the interrupt.
		 */
		W_REG(&regs->intctrlregs[RX_FIFO].intstatus,
		      DEF_RXINTMASK);
	}

	return macintstatus;
}

/* Update wlc->macintstatus and wlc->intstatus[]. */
/* Return true if they are updated successfully. false otherwise */
bool wlc_intrsupd(struct wlc_info *wlc)
{
	u32 macintstatus;

	ASSERT(wlc->macintstatus != 0);

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
 * *wantdpc will be set to true if further wlc_dpc() processing is required,
 * false otherwise.
 */
bool BCMFASTPATH wlc_isr(struct wlc_info *wlc, bool *wantdpc)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
	u32 macintstatus;

	*wantdpc = false;

	if (!wlc_hw->up || !wlc->macintmask)
		return false;

	/* read and clear macintstatus and intstatus registers */
	macintstatus = wlc_intstatus(wlc, true);

	if (macintstatus == 0xffffffff)
		WL_ERROR("DEVICEREMOVED detected in the ISR code path\n");

	/* it is not for us */
	if (macintstatus == 0)
		return false;

	*wantdpc = true;

	/* save interrupt status bits */
	ASSERT(wlc->macintstatus == 0);
	wlc->macintstatus = macintstatus;

	return true;

}

static bool BCMFASTPATH
wlc_bmac_dotxstatus(struct wlc_hw_info *wlc_hw, tx_status_t *txs, u32 s2)
{
	/* discard intermediate indications for ucode with one legitimate case:
	 *   e.g. if "useRTS" is set. ucode did a successful rts/cts exchange, but the subsequent
	 *   tx of DATA failed. so it will start rts/cts from the beginning (resetting the rts
	 *   transmission count)
	 */
	if (!(txs->status & TX_STATUS_AMPDU)
	    && (txs->status & TX_STATUS_INTERMEDIATE)) {
		return false;
	}

	return wlc_dotxstatus(wlc_hw->wlc, txs, s2);
}

/* process tx completion events in BMAC
 * Return true if more tx status need to be processed. false otherwise.
 */
static bool BCMFASTPATH
wlc_bmac_txstatus(struct wlc_hw_info *wlc_hw, bool bound, bool *fatal)
{
	bool morepending = false;
	struct wlc_info *wlc = wlc_hw->wlc;
	d11regs_t *regs;
	tx_status_t txstatus, *txs;
	u32 s1, s2;
	uint n = 0;
	/*
	 * Param 'max_tx_num' indicates max. # tx status to process before
	 * break out.
	 */
	uint max_tx_num = bound ? wlc->pub->tunables->txsbnd : -1;

	WL_TRACE("wl%d: wlc_bmac_txstatus\n", wlc_hw->unit);

	txs = &txstatus;
	regs = wlc_hw->regs;
	while (!(*fatal)
	       && (s1 = R_REG(&regs->frmtxstatus)) & TXS_V) {

		if (s1 == 0xffffffff) {
			WL_ERROR("wl%d: %s: dead chip\n",
				wlc_hw->unit, __func__);
			ASSERT(s1 != 0xffffffff);
			return morepending;
		}

			s2 = R_REG(&regs->frmtxstatus2);

		txs->status = s1 & TXS_STATUS_MASK;
		txs->frameid = (s1 & TXS_FID_MASK) >> TXS_FID_SHIFT;
		txs->sequence = s2 & TXS_SEQ_MASK;
		txs->phyerr = (s2 & TXS_PTX_MASK) >> TXS_PTX_SHIFT;
		txs->lasttxtime = 0;

		*fatal = wlc_bmac_dotxstatus(wlc_hw, txs, s2);

		/* !give others some time to run! */
		if (++n >= max_tx_num)
			break;
	}

	if (*fatal)
		return 0;

	if (n >= max_tx_num)
		morepending = true;

	if (!pktq_empty(&wlc->active_queue->q))
		wlc_send_q(wlc, wlc->active_queue);

	return morepending;
}

void wlc_suspend_mac_and_wait(struct wlc_info *wlc)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
	d11regs_t *regs = wlc_hw->regs;
	u32 mc, mi;

	WL_TRACE("wl%d: wlc_suspend_mac_and_wait: bandunit %d\n",
		 wlc_hw->unit, wlc_hw->band->bandunit);

	/*
	 * Track overlapping suspend requests
	 */
	wlc_hw->mac_suspend_depth++;
	if (wlc_hw->mac_suspend_depth > 1)
		return;

	/* force the core awake */
	wlc_ucode_wake_override_set(wlc_hw, WLC_WAKE_OVERRIDE_MACSUSPEND);

	mc = R_REG(&regs->maccontrol);

	if (mc == 0xffffffff) {
		WL_ERROR("wl%d: %s: dead chip\n", wlc_hw->unit, __func__);
		wl_down(wlc->wl);
		return;
	}
	ASSERT(!(mc & MCTL_PSM_JMP_0));
	ASSERT(mc & MCTL_PSM_RUN);
	ASSERT(mc & MCTL_EN_MAC);

	mi = R_REG(&regs->macintstatus);
	if (mi == 0xffffffff) {
		WL_ERROR("wl%d: %s: dead chip\n", wlc_hw->unit, __func__);
		wl_down(wlc->wl);
		return;
	}
	ASSERT(!(mi & MI_MACSSPNDD));

	wlc_bmac_mctrl(wlc_hw, MCTL_EN_MAC, 0);

	SPINWAIT(!(R_REG(&regs->macintstatus) & MI_MACSSPNDD),
		 WLC_MAX_MAC_SUSPEND);

	if (!(R_REG(&regs->macintstatus) & MI_MACSSPNDD)) {
		WL_ERROR("wl%d: wlc_suspend_mac_and_wait: waited %d uS and MI_MACSSPNDD is still not on.\n",
			 wlc_hw->unit, WLC_MAX_MAC_SUSPEND);
		WL_ERROR("wl%d: psmdebug 0x%08x, phydebug 0x%08x, psm_brc 0x%04x\n",
			 wlc_hw->unit,
			 R_REG(&regs->psmdebug),
			 R_REG(&regs->phydebug),
			 R_REG(&regs->psm_brc));
	}

	mc = R_REG(&regs->maccontrol);
	if (mc == 0xffffffff) {
		WL_ERROR("wl%d: %s: dead chip\n", wlc_hw->unit, __func__);
		wl_down(wlc->wl);
		return;
	}
	ASSERT(!(mc & MCTL_PSM_JMP_0));
	ASSERT(mc & MCTL_PSM_RUN);
	ASSERT(!(mc & MCTL_EN_MAC));
}

void wlc_enable_mac(struct wlc_info *wlc)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
	d11regs_t *regs = wlc_hw->regs;
	u32 mc, mi;

	WL_TRACE("wl%d: wlc_enable_mac: bandunit %d\n",
		 wlc_hw->unit, wlc->band->bandunit);

	/*
	 * Track overlapping suspend requests
	 */
	ASSERT(wlc_hw->mac_suspend_depth > 0);
	wlc_hw->mac_suspend_depth--;
	if (wlc_hw->mac_suspend_depth > 0)
		return;

	mc = R_REG(&regs->maccontrol);
	ASSERT(!(mc & MCTL_PSM_JMP_0));
	ASSERT(!(mc & MCTL_EN_MAC));
	ASSERT(mc & MCTL_PSM_RUN);

	wlc_bmac_mctrl(wlc_hw, MCTL_EN_MAC, MCTL_EN_MAC);
	W_REG(&regs->macintstatus, MI_MACSSPNDD);

	mc = R_REG(&regs->maccontrol);
	ASSERT(!(mc & MCTL_PSM_JMP_0));
	ASSERT(mc & MCTL_EN_MAC);
	ASSERT(mc & MCTL_PSM_RUN);

	mi = R_REG(&regs->macintstatus);
	ASSERT(!(mi & MI_MACSSPNDD));

	wlc_ucode_wake_override_clear(wlc_hw, WLC_WAKE_OVERRIDE_MACSUSPEND);
}

static void wlc_upd_ofdm_pctl1_table(struct wlc_hw_info *wlc_hw)
{
	u8 rate;
	u8 rates[8] = {
		WLC_RATE_6M, WLC_RATE_9M, WLC_RATE_12M, WLC_RATE_18M,
		WLC_RATE_24M, WLC_RATE_36M, WLC_RATE_48M, WLC_RATE_54M
	};
	u16 entry_ptr;
	u16 pctl1;
	uint i;

	if (!WLC_PHY_11N_CAP(wlc_hw->band))
		return;

	/* walk the phy rate table and update the entries */
	for (i = 0; i < ARRAY_SIZE(rates); i++) {
		rate = rates[i];

		entry_ptr = wlc_bmac_ofdm_ratetable_offset(wlc_hw, rate);

		/* read the SHM Rate Table entry OFDM PCTL1 values */
		pctl1 =
		    wlc_bmac_read_shm(wlc_hw, entry_ptr + M_RT_OFDM_PCTL1_POS);

		/* modify the value */
		pctl1 &= ~PHY_TXC1_MODE_MASK;
		pctl1 |= (wlc_hw->hw_stf_ss_opmode << PHY_TXC1_MODE_SHIFT);

		/* Update the SHM Rate Table entry OFDM PCTL1 values */
		wlc_bmac_write_shm(wlc_hw, entry_ptr + M_RT_OFDM_PCTL1_POS,
				   pctl1);
	}
}

static u16 wlc_bmac_ofdm_ratetable_offset(struct wlc_hw_info *wlc_hw, u8 rate)
{
	uint i;
	u8 plcp_rate = 0;
	struct plcp_signal_rate_lookup {
		u8 rate;
		u8 signal_rate;
	};
	/* OFDM RATE sub-field of PLCP SIGNAL field, per 802.11 sec 17.3.4.1 */
	const struct plcp_signal_rate_lookup rate_lookup[] = {
		{WLC_RATE_6M, 0xB},
		{WLC_RATE_9M, 0xF},
		{WLC_RATE_12M, 0xA},
		{WLC_RATE_18M, 0xE},
		{WLC_RATE_24M, 0x9},
		{WLC_RATE_36M, 0xD},
		{WLC_RATE_48M, 0x8},
		{WLC_RATE_54M, 0xC}
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
	return 2 * wlc_bmac_read_shm(wlc_hw, M_RT_DIRMAP_A + (plcp_rate * 2));
}

void wlc_bmac_band_stf_ss_set(struct wlc_hw_info *wlc_hw, u8 stf_mode)
{
	wlc_hw->hw_stf_ss_opmode = stf_mode;

	if (wlc_hw->clk)
		wlc_upd_ofdm_pctl1_table(wlc_hw);
}

void BCMFASTPATH
wlc_bmac_read_tsf(struct wlc_hw_info *wlc_hw, u32 *tsf_l_ptr,
		  u32 *tsf_h_ptr)
{
	d11regs_t *regs = wlc_hw->regs;

	/* read the tsf timer low, then high to get an atomic read */
	*tsf_l_ptr = R_REG(&regs->tsf_timerlow);
	*tsf_h_ptr = R_REG(&regs->tsf_timerhigh);

	return;
}

static bool wlc_bmac_validate_chip_access(struct wlc_hw_info *wlc_hw)
{
	d11regs_t *regs;
	u32 w, val;

	WL_TRACE("wl%d: validate_chip_access\n", wlc_hw->unit);

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
		WL_ERROR("wl%d: validate_chip_access: SHM = 0x%x, expected 0xaa5555aa\n",
			 wlc_hw->unit, val);
		return false;
	}

	W_REG(&regs->objaddr, OBJADDR_SHM_SEL | 0);
	(void)R_REG(&regs->objaddr);
	W_REG(&regs->objdata, (u32) 0x55aaaa55);

	W_REG(&regs->objaddr, OBJADDR_SHM_SEL | 0);
	(void)R_REG(&regs->objaddr);
	val = R_REG(&regs->objdata);
	if (val != (u32) 0x55aaaa55) {
		WL_ERROR("wl%d: validate_chip_access: SHM = 0x%x, expected 0x55aaaa55\n",
			 wlc_hw->unit, val);
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
		WL_ERROR("wl%d: validate_chip_access: maccontrol = 0x%x, expected 0x%x or 0x%x\n",
			 wlc_hw->unit, w,
			 (MCTL_IHR_EN | MCTL_WAKE),
			 (MCTL_IHR_EN | MCTL_GMODE | MCTL_WAKE));
		return false;
	}

	return true;
}

#define PHYPLL_WAIT_US	100000

void wlc_bmac_core_phypll_ctl(struct wlc_hw_info *wlc_hw, bool on)
{
	d11regs_t *regs;
	u32 tmp;

	WL_TRACE("wl%d: wlc_bmac_core_phypll_ctl\n", wlc_hw->unit);

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
			    (CCS_ERSRC_AVAIL_HT)) {
				WL_ERROR("%s: turn on PHY PLL failed\n",
					 __func__);
				ASSERT(0);
			}
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
			    (CCS_ERSRC_AVAIL_D11PLL | CCS_ERSRC_AVAIL_PHYPLL)) {
				WL_ERROR("%s: turn on PHY PLL failed\n",
					 __func__);
				ASSERT(0);
			}
		}
	} else {
		/* Since the PLL may be shared, other cores can still be requesting it;
		 * so we'll deassert the request but not wait for status to comply.
		 */
		AND_REG(&regs->clk_ctl_st, ~CCS_ERSRC_REQ_PHYPLL);
		tmp = R_REG(&regs->clk_ctl_st);
	}
}

void wlc_coredisable(struct wlc_hw_info *wlc_hw)
{
	bool dev_gone;

	WL_TRACE("wl%d: %s\n", wlc_hw->unit, __func__);

	ASSERT(!wlc_hw->up);

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
	wlc_bmac_core_phypll_ctl(wlc_hw, false);

	/* No need to set wlc->pub->radio_active = OFF
	 * because this function needs down capability and
	 * radio_active is designed for BCMNODOWN.
	 */

	/* remove gpio controls */
	if (wlc_hw->ucode_dbgsel)
		si_gpiocontrol(wlc_hw->sih, ~0, 0, GPIO_DRV_PRIORITY);

	wlc_hw->clk = false;
	si_core_disable(wlc_hw->sih, 0);
	wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, false);
}

/* power both the pll and external oscillator on/off */
static void wlc_bmac_xtal(struct wlc_hw_info *wlc_hw, bool want)
{
	WL_TRACE("wl%d: wlc_bmac_xtal: want %d\n", wlc_hw->unit, want);

	/* dont power down if plldown is false or we must poll hw radio disable */
	if (!want && wlc_hw->pllreq)
		return;

	if (wlc_hw->sih)
		si_clkctl_xtal(wlc_hw->sih, XTAL | PLL, want);

	wlc_hw->sbclk = want;
	if (!wlc_hw->sbclk) {
		wlc_hw->clk = false;
		if (wlc_hw->band && wlc_hw->band->pi)
			wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, false);
	}
}

static void wlc_flushqueues(struct wlc_info *wlc)
{
	struct wlc_hw_info *wlc_hw = wlc->hw;
	uint i;

	wlc->txpend16165war = 0;

	/* free any posted tx packets */
	for (i = 0; i < NFIFO; i++)
		if (wlc_hw->di[i]) {
			dma_txreclaim(wlc_hw->di[i], HNDDMA_RANGE_ALL);
			TXPKTPENDCLR(wlc, i);
			WL_TRACE("wlc_flushqueues: pktpend fifo %d cleared\n",
				 i);
		}

	/* free any posted rx packets */
	dma_rxreclaim(wlc_hw->di[RX_FIFO]);
}

u16 wlc_bmac_read_shm(struct wlc_hw_info *wlc_hw, uint offset)
{
	return wlc_bmac_read_objmem(wlc_hw, offset, OBJADDR_SHM_SEL);
}

void wlc_bmac_write_shm(struct wlc_hw_info *wlc_hw, uint offset, u16 v)
{
	wlc_bmac_write_objmem(wlc_hw, offset, v, OBJADDR_SHM_SEL);
}

/* Set a range of shared memory to a value.
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 */
void wlc_bmac_set_shm(struct wlc_hw_info *wlc_hw, uint offset, u16 v, int len)
{
	int i;

	/* offset and len need to be even */
	ASSERT((offset & 1) == 0);
	ASSERT((len & 1) == 0);

	if (len <= 0)
		return;

	for (i = 0; i < len; i += 2) {
		wlc_bmac_write_objmem(wlc_hw, offset + i, v, OBJADDR_SHM_SEL);
	}
}

static u16
wlc_bmac_read_objmem(struct wlc_hw_info *wlc_hw, uint offset, u32 sel)
{
	d11regs_t *regs = wlc_hw->regs;
	volatile u16 *objdata_lo = (volatile u16 *)&regs->objdata;
	volatile u16 *objdata_hi = objdata_lo + 1;
	u16 v;

	ASSERT((offset & 1) == 0);

	W_REG(&regs->objaddr, sel | (offset >> 2));
	(void)R_REG(&regs->objaddr);
	if (offset & 2) {
		v = R_REG(objdata_hi);
	} else {
		v = R_REG(objdata_lo);
	}

	return v;
}

static void
wlc_bmac_write_objmem(struct wlc_hw_info *wlc_hw, uint offset, u16 v, u32 sel)
{
	d11regs_t *regs = wlc_hw->regs;
	volatile u16 *objdata_lo = (volatile u16 *)&regs->objdata;
	volatile u16 *objdata_hi = objdata_lo + 1;

	ASSERT((offset & 1) == 0);

	W_REG(&regs->objaddr, sel | (offset >> 2));
	(void)R_REG(&regs->objaddr);
	if (offset & 2) {
		W_REG(objdata_hi, v);
	} else {
		W_REG(objdata_lo, v);
	}
}

/* Copy a buffer to shared memory of specified type .
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 * 'sel' selects the type of memory
 */
void
wlc_bmac_copyto_objmem(struct wlc_hw_info *wlc_hw, uint offset, const void *buf,
		       int len, u32 sel)
{
	u16 v;
	const u8 *p = (const u8 *)buf;
	int i;

	/* offset and len need to be even */
	ASSERT((offset & 1) == 0);
	ASSERT((len & 1) == 0);

	if (len <= 0)
		return;

	for (i = 0; i < len; i += 2) {
		v = p[i] | (p[i + 1] << 8);
		wlc_bmac_write_objmem(wlc_hw, offset + i, v, sel);
	}
}

/* Copy a piece of shared memory of specified type to a buffer .
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 * 'sel' selects the type of memory
 */
void
wlc_bmac_copyfrom_objmem(struct wlc_hw_info *wlc_hw, uint offset, void *buf,
			 int len, u32 sel)
{
	u16 v;
	u8 *p = (u8 *) buf;
	int i;

	/* offset and len need to be even */
	ASSERT((offset & 1) == 0);
	ASSERT((len & 1) == 0);

	if (len <= 0)
		return;

	for (i = 0; i < len; i += 2) {
		v = wlc_bmac_read_objmem(wlc_hw, offset + i, sel);
		p[i] = v & 0xFF;
		p[i + 1] = (v >> 8) & 0xFF;
	}
}

void wlc_bmac_copyfrom_vars(struct wlc_hw_info *wlc_hw, char **buf, uint *len)
{
	WL_TRACE("wlc_bmac_copyfrom_vars, nvram vars totlen=%d\n",
		 wlc_hw->vars_size);

	*buf = wlc_hw->vars;
	*len = wlc_hw->vars_size;
}

void wlc_bmac_retrylimit_upd(struct wlc_hw_info *wlc_hw, u16 SRL, u16 LRL)
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

void wlc_bmac_set_noreset(struct wlc_hw_info *wlc_hw, bool noreset_flag)
{
	wlc_hw->noreset = noreset_flag;
}

void wlc_bmac_pllreq(struct wlc_hw_info *wlc_hw, bool set, mbool req_bit)
{
	ASSERT(req_bit);

	if (set) {
		if (mboolisset(wlc_hw->pllreq, req_bit))
			return;

		mboolset(wlc_hw->pllreq, req_bit);

		if (mboolisset(wlc_hw->pllreq, WLC_PLLREQ_FLIP)) {
			if (!wlc_hw->sbclk) {
				wlc_bmac_xtal(wlc_hw, ON);
			}
		}
	} else {
		if (!mboolisset(wlc_hw->pllreq, req_bit))
			return;

		mboolclr(wlc_hw->pllreq, req_bit);

		if (mboolisset(wlc_hw->pllreq, WLC_PLLREQ_FLIP)) {
			if (wlc_hw->sbclk) {
				wlc_bmac_xtal(wlc_hw, OFF);
			}
		}
	}

	return;
}

/* this will be true for all ai chips */
bool wlc_bmac_taclear(struct wlc_hw_info *wlc_hw, bool ta_ok)
{
	return true;
}

u16 wlc_bmac_rate_shm_offset(struct wlc_hw_info *wlc_hw, u8 rate)
{
	u16 table_ptr;
	u8 phy_rate, index;

	/* get the phy specific rate encoding for the PLCP SIGNAL field */
	/* XXX4321 fixup needed ? */
	if (IS_OFDM(rate))
		table_ptr = M_RT_DIRMAP_A;
	else
		table_ptr = M_RT_DIRMAP_B;

	/* for a given rate, the LS-nibble of the PLCP SIGNAL field is
	 * the index into the rate table.
	 */
	phy_rate = rate_info[rate] & RATE_MASK;
	index = phy_rate & 0xf;

	/* Find the SHM pointer to the rate table entry by looking in the
	 * Direct-map Table
	 */
	return 2 * wlc_bmac_read_shm(wlc_hw, table_ptr + (index * 2));
}

void wlc_bmac_antsel_set(struct wlc_hw_info *wlc_hw, u32 antsel_avail)
{
	wlc_hw->antsel_avail = antsel_avail;
}
