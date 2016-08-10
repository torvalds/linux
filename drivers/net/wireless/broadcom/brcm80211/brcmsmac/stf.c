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

#include <net/mac80211.h>

#include "types.h"
#include "d11.h"
#include "rate.h"
#include "phy/phy_hal.h"
#include "channel.h"
#include "main.h"
#include "stf.h"
#include "debug.h"

#define MIN_SPATIAL_EXPANSION	0
#define MAX_SPATIAL_EXPANSION	1

#define BRCMS_STF_SS_STBC_RX(wlc) (BRCMS_ISNPHY(wlc->band) && \
	NREV_GT(wlc->band->phyrev, 3) && NREV_LE(wlc->band->phyrev, 6))

#define NSTS_1	1
#define NSTS_2	2
#define NSTS_3	3
#define NSTS_4	4

static const u8 txcore_default[5] = {
	(0),			/* bitmap of the core enabled */
	(0x01),			/* For Nsts = 1, enable core 1 */
	(0x03),			/* For Nsts = 2, enable core 1 & 2 */
	(0x07),			/* For Nsts = 3, enable core 1, 2 & 3 */
	(0x0f)			/* For Nsts = 4, enable all cores */
};

static void brcms_c_stf_stbc_rx_ht_update(struct brcms_c_info *wlc, int val)
{
	/* MIMOPHYs rev3-6 cannot receive STBC with only one rx core active */
	if (BRCMS_STF_SS_STBC_RX(wlc)) {
		if ((wlc->stf->rxstreams == 1) && (val != HT_CAP_RX_STBC_NO))
			return;
	}

	if (wlc->pub->up) {
		brcms_c_update_beacon(wlc);
		brcms_c_update_probe_resp(wlc, true);
	}
}

/*
 * every WLC_TEMPSENSE_PERIOD seconds temperature check to decide whether to
 * turn on/off txchain.
 */
void brcms_c_tempsense_upd(struct brcms_c_info *wlc)
{
	struct brcms_phy_pub *pi = wlc->band->pi;
	uint active_chains, txchain;

	/* Check if the chip is too hot. Disable one Tx chain, if it is */
	/* high 4 bits are for Rx chain, low 4 bits are  for Tx chain */
	active_chains = wlc_phy_stf_chain_active_get(pi);
	txchain = active_chains & 0xf;

	if (wlc->stf->txchain == wlc->stf->hw_txchain) {
		if (txchain && (txchain < wlc->stf->hw_txchain))
			/* turn off 1 tx chain */
			brcms_c_stf_txchain_set(wlc, txchain, true);
	} else if (wlc->stf->txchain < wlc->stf->hw_txchain) {
		if (txchain == wlc->stf->hw_txchain)
			/* turn back on txchain */
			brcms_c_stf_txchain_set(wlc, txchain, true);
	}
}

void
brcms_c_stf_ss_algo_channel_get(struct brcms_c_info *wlc, u16 *ss_algo_channel,
			    u16 chanspec)
{
	struct tx_power power = { };
	u8 siso_mcs_id, cdd_mcs_id, stbc_mcs_id;

	/* Clear previous settings */
	*ss_algo_channel = 0;

	if (!wlc->pub->up) {
		*ss_algo_channel = (u16) -1;
		return;
	}

	wlc_phy_txpower_get_current(wlc->band->pi, &power,
				    CHSPEC_CHANNEL(chanspec));

	siso_mcs_id = (CHSPEC_IS40(chanspec)) ?
	    WL_TX_POWER_MCS40_SISO_FIRST : WL_TX_POWER_MCS20_SISO_FIRST;
	cdd_mcs_id = (CHSPEC_IS40(chanspec)) ?
	    WL_TX_POWER_MCS40_CDD_FIRST : WL_TX_POWER_MCS20_CDD_FIRST;
	stbc_mcs_id = (CHSPEC_IS40(chanspec)) ?
	    WL_TX_POWER_MCS40_STBC_FIRST : WL_TX_POWER_MCS20_STBC_FIRST;

	/* criteria to choose stf mode */

	/*
	 * the "+3dbm (12 0.25db units)" is to account for the fact that with
	 * CDD, tx occurs on both chains
	 */
	if (power.target[siso_mcs_id] > (power.target[cdd_mcs_id] + 12))
		setbit(ss_algo_channel, PHY_TXC1_MODE_SISO);
	else
		setbit(ss_algo_channel, PHY_TXC1_MODE_CDD);

	/*
	 * STBC is ORed into to algo channel as STBC requires per-packet SCB
	 * capability check so cannot be default mode of operation. One of
	 * SISO, CDD have to be set
	 */
	if (power.target[siso_mcs_id] <= (power.target[stbc_mcs_id] + 12))
		setbit(ss_algo_channel, PHY_TXC1_MODE_STBC);
}

static bool brcms_c_stf_stbc_tx_set(struct brcms_c_info *wlc, s32 int_val)
{
	if ((int_val != AUTO) && (int_val != OFF) && (int_val != ON))
		return false;

	if ((int_val == ON) && (wlc->stf->txstreams == 1))
		return false;

	wlc->bandstate[BAND_2G_INDEX]->band_stf_stbc_tx = (s8) int_val;
	wlc->bandstate[BAND_5G_INDEX]->band_stf_stbc_tx = (s8) int_val;

	return true;
}

bool brcms_c_stf_stbc_rx_set(struct brcms_c_info *wlc, s32 int_val)
{
	if ((int_val != HT_CAP_RX_STBC_NO)
	    && (int_val != HT_CAP_RX_STBC_ONE_STREAM))
		return false;

	if (BRCMS_STF_SS_STBC_RX(wlc)) {
		if ((int_val != HT_CAP_RX_STBC_NO)
		    && (wlc->stf->rxstreams == 1))
			return false;
	}

	brcms_c_stf_stbc_rx_ht_update(wlc, int_val);
	return true;
}

static int brcms_c_stf_txcore_set(struct brcms_c_info *wlc, u8 Nsts,
				  u8 core_mask)
{
	brcms_dbg_ht(wlc->hw->d11core, "wl%d: Nsts %d core_mask %x\n",
		     wlc->pub->unit, Nsts, core_mask);

	if (hweight8(core_mask) > wlc->stf->txstreams)
		core_mask = 0;

	if ((hweight8(core_mask) == wlc->stf->txstreams) &&
	    ((core_mask & ~wlc->stf->txchain)
	     || !(core_mask & wlc->stf->txchain)))
		core_mask = wlc->stf->txchain;

	wlc->stf->txcore[Nsts] = core_mask;
	/* Nsts = 1..4, txcore index = 1..4 */
	if (Nsts == 1) {
		/* Needs to update beacon and ucode generated response
		 * frames when 1 stream core map changed
		 */
		wlc->stf->phytxant = core_mask << PHY_TXC_ANT_SHIFT;
		brcms_b_txant_set(wlc->hw, wlc->stf->phytxant);
		if (wlc->clk) {
			brcms_c_suspend_mac_and_wait(wlc);
			brcms_c_beacon_phytxctl_txant_upd(wlc, wlc->bcn_rspec);
			brcms_c_enable_mac(wlc);
		}
	}

	return 0;
}

static int brcms_c_stf_spatial_policy_set(struct brcms_c_info *wlc, int val)
{
	int i;
	u8 core_mask = 0;

	brcms_dbg_ht(wlc->hw->d11core, "wl%d: val %x\n", wlc->pub->unit,
		     val);

	wlc->stf->spatial_policy = (s8) val;
	for (i = 1; i <= MAX_STREAMS_SUPPORTED; i++) {
		core_mask = (val == MAX_SPATIAL_EXPANSION) ?
		    wlc->stf->txchain : txcore_default[i];
		brcms_c_stf_txcore_set(wlc, (u8) i, core_mask);
	}
	return 0;
}

/*
 * Centralized txant update function. call it whenever wlc->stf->txant and/or
 * wlc->stf->txchain change.
 *
 * Antennas are controlled by ucode indirectly, which drives PHY or GPIO to
 * achieve various tx/rx antenna selection schemes
 *
 * legacy phy, bit 6 and bit 7 means antenna 0 and 1 respectively, bit6+bit7
 * means auto(last rx).
 * for NREV<3, bit 6 and bit 7 means antenna 0 and 1 respectively, bit6+bit7
 * means last rx and do tx-antenna selection for SISO transmissions
 * for NREV=3, bit 6 and bit _8_ means antenna 0 and 1 respectively, bit6+bit7
 * means last rx and do tx-antenna selection for SISO transmissions
 * for NREV>=7, bit 6 and bit 7 mean antenna 0 and 1 respectively, nit6+bit7
 * means both cores active
*/
static void _brcms_c_stf_phy_txant_upd(struct brcms_c_info *wlc)
{
	s8 txant;

	txant = (s8) wlc->stf->txant;
	if (BRCMS_PHY_11N_CAP(wlc->band)) {
		if (txant == ANT_TX_FORCE_0) {
			wlc->stf->phytxant = PHY_TXC_ANT_0;
		} else if (txant == ANT_TX_FORCE_1) {
			wlc->stf->phytxant = PHY_TXC_ANT_1;

			if (BRCMS_ISNPHY(wlc->band) &&
			    NREV_GE(wlc->band->phyrev, 3)
			    && NREV_LT(wlc->band->phyrev, 7))
				wlc->stf->phytxant = PHY_TXC_ANT_2;
		} else {
			if (BRCMS_ISLCNPHY(wlc->band) ||
			    BRCMS_ISSSLPNPHY(wlc->band))
				wlc->stf->phytxant = PHY_TXC_LCNPHY_ANT_LAST;
			else {
				/* catch out of sync wlc->stf->txcore */
				WARN_ON(wlc->stf->txchain <= 0);
				wlc->stf->phytxant =
				    wlc->stf->txchain << PHY_TXC_ANT_SHIFT;
			}
		}
	} else {
		if (txant == ANT_TX_FORCE_0)
			wlc->stf->phytxant = PHY_TXC_OLD_ANT_0;
		else if (txant == ANT_TX_FORCE_1)
			wlc->stf->phytxant = PHY_TXC_OLD_ANT_1;
		else
			wlc->stf->phytxant = PHY_TXC_OLD_ANT_LAST;
	}

	brcms_b_txant_set(wlc->hw, wlc->stf->phytxant);
}

int brcms_c_stf_txchain_set(struct brcms_c_info *wlc, s32 int_val, bool force)
{
	u8 txchain = (u8) int_val;
	u8 txstreams;
	uint i;

	if (wlc->stf->txchain == txchain)
		return 0;

	if ((txchain & ~wlc->stf->hw_txchain)
	    || !(txchain & wlc->stf->hw_txchain))
		return -EINVAL;

	/*
	 * if nrate override is configured to be non-SISO STF mode, reject
	 * reducing txchain to 1
	 */
	txstreams = (u8) hweight8(txchain);
	if (txstreams > MAX_STREAMS_SUPPORTED)
		return -EINVAL;

	wlc->stf->txchain = txchain;
	wlc->stf->txstreams = txstreams;
	brcms_c_stf_stbc_tx_set(wlc, wlc->band->band_stf_stbc_tx);
	brcms_c_stf_ss_update(wlc, wlc->bandstate[BAND_2G_INDEX]);
	brcms_c_stf_ss_update(wlc, wlc->bandstate[BAND_5G_INDEX]);
	wlc->stf->txant =
	    (wlc->stf->txstreams == 1) ? ANT_TX_FORCE_0 : ANT_TX_DEF;
	_brcms_c_stf_phy_txant_upd(wlc);

	wlc_phy_stf_chain_set(wlc->band->pi, wlc->stf->txchain,
			      wlc->stf->rxchain);

	for (i = 1; i <= MAX_STREAMS_SUPPORTED; i++)
		brcms_c_stf_txcore_set(wlc, (u8) i, txcore_default[i]);

	return 0;
}

/*
 * update wlc->stf->ss_opmode which represents the operational stf_ss mode
 * we're using
 */
int brcms_c_stf_ss_update(struct brcms_c_info *wlc, struct brcms_band *band)
{
	int ret_code = 0;
	u8 prev_stf_ss;
	u8 upd_stf_ss;

	prev_stf_ss = wlc->stf->ss_opmode;

	/*
	 * NOTE: opmode can only be SISO or CDD as STBC is decided on a
	 * per-packet basis
	 */
	if (BRCMS_STBC_CAP_PHY(wlc) &&
	    wlc->stf->ss_algosel_auto
	    && (wlc->stf->ss_algo_channel != (u16) -1)) {
		upd_stf_ss = (wlc->stf->txstreams == 1 ||
			      isset(&wlc->stf->ss_algo_channel,
				    PHY_TXC1_MODE_SISO)) ?
				    PHY_TXC1_MODE_SISO : PHY_TXC1_MODE_CDD;
	} else {
		if (wlc->band != band)
			return ret_code;
		upd_stf_ss = (wlc->stf->txstreams == 1) ?
				PHY_TXC1_MODE_SISO : band->band_stf_ss_mode;
	}
	if (prev_stf_ss != upd_stf_ss) {
		wlc->stf->ss_opmode = upd_stf_ss;
		brcms_b_band_stf_ss_set(wlc->hw, upd_stf_ss);
	}

	return ret_code;
}

int brcms_c_stf_attach(struct brcms_c_info *wlc)
{
	wlc->bandstate[BAND_2G_INDEX]->band_stf_ss_mode = PHY_TXC1_MODE_SISO;
	wlc->bandstate[BAND_5G_INDEX]->band_stf_ss_mode = PHY_TXC1_MODE_CDD;

	if (BRCMS_ISNPHY(wlc->band) &&
	    (wlc_phy_txpower_hw_ctrl_get(wlc->band->pi) != PHY_TPC_HW_ON))
		wlc->bandstate[BAND_2G_INDEX]->band_stf_ss_mode =
		    PHY_TXC1_MODE_CDD;
	brcms_c_stf_ss_update(wlc, wlc->bandstate[BAND_2G_INDEX]);
	brcms_c_stf_ss_update(wlc, wlc->bandstate[BAND_5G_INDEX]);

	brcms_c_stf_stbc_rx_ht_update(wlc, HT_CAP_RX_STBC_NO);
	wlc->bandstate[BAND_2G_INDEX]->band_stf_stbc_tx = OFF;
	wlc->bandstate[BAND_5G_INDEX]->band_stf_stbc_tx = OFF;

	if (BRCMS_STBC_CAP_PHY(wlc)) {
		wlc->stf->ss_algosel_auto = true;
		/* Init the default value */
		wlc->stf->ss_algo_channel = (u16) -1;
	}
	return 0;
}

void brcms_c_stf_detach(struct brcms_c_info *wlc)
{
}

void brcms_c_stf_phy_txant_upd(struct brcms_c_info *wlc)
{
	_brcms_c_stf_phy_txant_upd(wlc);
}

void brcms_c_stf_phy_chain_calc(struct brcms_c_info *wlc)
{
	struct ssb_sprom *sprom = &wlc->hw->d11core->bus->sprom;

	/* get available rx/tx chains */
	wlc->stf->hw_txchain = sprom->txchain;
	wlc->stf->hw_rxchain = sprom->rxchain;

	/* these parameter are intended to be used for all PHY types */
	if (wlc->stf->hw_txchain == 0 || wlc->stf->hw_txchain == 0xf) {
		if (BRCMS_ISNPHY(wlc->band))
			wlc->stf->hw_txchain = TXCHAIN_DEF_NPHY;
		else
			wlc->stf->hw_txchain = TXCHAIN_DEF;
	}

	wlc->stf->txchain = wlc->stf->hw_txchain;
	wlc->stf->txstreams = (u8) hweight8(wlc->stf->hw_txchain);

	if (wlc->stf->hw_rxchain == 0 || wlc->stf->hw_rxchain == 0xf) {
		if (BRCMS_ISNPHY(wlc->band))
			wlc->stf->hw_rxchain = RXCHAIN_DEF_NPHY;
		else
			wlc->stf->hw_rxchain = RXCHAIN_DEF;
	}

	wlc->stf->rxchain = wlc->stf->hw_rxchain;
	wlc->stf->rxstreams = (u8) hweight8(wlc->stf->hw_rxchain);

	/* initialize the txcore table */
	memcpy(wlc->stf->txcore, txcore_default, sizeof(wlc->stf->txcore));

	/* default spatial_policy */
	wlc->stf->spatial_policy = MIN_SPATIAL_EXPANSION;
	brcms_c_stf_spatial_policy_set(wlc, MIN_SPATIAL_EXPANSION);
}

static u16 _brcms_c_stf_phytxchain_sel(struct brcms_c_info *wlc,
				       u32 rspec)
{
	u16 phytxant = wlc->stf->phytxant;

	if (rspec_stf(rspec) != PHY_TXC1_MODE_SISO)
		phytxant = wlc->stf->txchain << PHY_TXC_ANT_SHIFT;
	else if (wlc->stf->txant == ANT_TX_DEF)
		phytxant = wlc->stf->txchain << PHY_TXC_ANT_SHIFT;
	phytxant &= PHY_TXC_ANT_MASK;
	return phytxant;
}

u16 brcms_c_stf_phytxchain_sel(struct brcms_c_info *wlc, u32 rspec)
{
	return _brcms_c_stf_phytxchain_sel(wlc, rspec);
}

u16 brcms_c_stf_d11hdrs_phyctl_txant(struct brcms_c_info *wlc, u32 rspec)
{
	u16 phytxant = wlc->stf->phytxant;
	u16 mask = PHY_TXC_ANT_MASK;

	/* for non-siso rates or default setting, use the available chains */
	if (BRCMS_ISNPHY(wlc->band)) {
		phytxant = _brcms_c_stf_phytxchain_sel(wlc, rspec);
		mask = PHY_TXC_HTANT_MASK;
	}
	phytxant |= phytxant & mask;
	return phytxant;
}
