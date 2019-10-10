// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2010 Broadcom Corporation
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include <brcm_hw_ids.h>
#include <chipcommon.h>
#include <aiutils.h>
#include <d11.h>
#include <phy_shim.h>
#include "phy_hal.h"
#include "phy_int.h"
#include "phy_radio.h"
#include "phy_lcn.h"
#include "phyreg_n.h"

#define VALID_N_RADIO(radioid) ((radioid == BCM2055_ID) || \
				 (radioid == BCM2056_ID) || \
				 (radioid == BCM2057_ID))

#define VALID_LCN_RADIO(radioid)	(radioid == BCM2064_ID)

#define VALID_RADIO(pi, radioid)        ( \
		(ISNPHY(pi) ? VALID_N_RADIO(radioid) : false) || \
		(ISLCNPHY(pi) ? VALID_LCN_RADIO(radioid) : false))

/* basic mux operation - can be optimized on several architectures */
#define MUX(pred, true, false) ((pred) ? (true) : (false))

/* modulo inc/dec - assumes x E [0, bound - 1] */
#define MODINC(x, bound) MUX((x) == (bound) - 1, 0, (x) + 1)

/* modulo inc/dec, bound = 2^k */
#define MODDEC_POW2(x, bound) (((x) - 1) & ((bound) - 1))
#define MODINC_POW2(x, bound) (((x) + 1) & ((bound) - 1))

struct chan_info_basic {
	u16 chan;
	u16 freq;
};

static const struct chan_info_basic chan_info_all[] = {
	{1, 2412},
	{2, 2417},
	{3, 2422},
	{4, 2427},
	{5, 2432},
	{6, 2437},
	{7, 2442},
	{8, 2447},
	{9, 2452},
	{10, 2457},
	{11, 2462},
	{12, 2467},
	{13, 2472},
	{14, 2484},

	{34, 5170},
	{38, 5190},
	{42, 5210},
	{46, 5230},

	{36, 5180},
	{40, 5200},
	{44, 5220},
	{48, 5240},
	{52, 5260},
	{56, 5280},
	{60, 5300},
	{64, 5320},

	{100, 5500},
	{104, 5520},
	{108, 5540},
	{112, 5560},
	{116, 5580},
	{120, 5600},
	{124, 5620},
	{128, 5640},
	{132, 5660},
	{136, 5680},
	{140, 5700},

	{149, 5745},
	{153, 5765},
	{157, 5785},
	{161, 5805},
	{165, 5825},

	{184, 4920},
	{188, 4940},
	{192, 4960},
	{196, 4980},
	{200, 5000},
	{204, 5020},
	{208, 5040},
	{212, 5060},
	{216, 5080}
};

static const u8 ofdm_rate_lookup[] = {

	BRCM_RATE_48M,
	BRCM_RATE_24M,
	BRCM_RATE_12M,
	BRCM_RATE_6M,
	BRCM_RATE_54M,
	BRCM_RATE_36M,
	BRCM_RATE_18M,
	BRCM_RATE_9M
};

#define PHY_WREG_LIMIT  24

void wlc_phyreg_enter(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	wlapi_bmac_ucode_wake_override_phyreg_set(pi->sh->physhim);
}

void wlc_phyreg_exit(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	wlapi_bmac_ucode_wake_override_phyreg_clear(pi->sh->physhim);
}

void wlc_radioreg_enter(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	wlapi_bmac_mctrl(pi->sh->physhim, MCTL_LOCK_RADIO, MCTL_LOCK_RADIO);

	udelay(10);
}

void wlc_radioreg_exit(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	(void)bcma_read16(pi->d11core, D11REGOFFS(phyversion));
	pi->phy_wreg = 0;
	wlapi_bmac_mctrl(pi->sh->physhim, MCTL_LOCK_RADIO, 0);
}

u16 read_radio_reg(struct brcms_phy *pi, u16 addr)
{
	u16 data;

	if (addr == RADIO_IDCODE)
		return 0xffff;

	switch (pi->pubpi.phy_type) {
	case PHY_TYPE_N:
		if (!CONF_HAS(PHYTYPE, PHY_TYPE_N))
			break;
		if (NREV_GE(pi->pubpi.phy_rev, 7))
			addr |= RADIO_2057_READ_OFF;
		else
			addr |= RADIO_2055_READ_OFF;
		break;

	case PHY_TYPE_LCN:
		if (!CONF_HAS(PHYTYPE, PHY_TYPE_LCN))
			break;
		addr |= RADIO_2064_READ_OFF;
		break;

	default:
		break;
	}

	if ((D11REV_GE(pi->sh->corerev, 24)) ||
	    (D11REV_IS(pi->sh->corerev, 22)
	     && (pi->pubpi.phy_type != PHY_TYPE_SSN))) {
		bcma_wflush16(pi->d11core, D11REGOFFS(radioregaddr), addr);
		data = bcma_read16(pi->d11core, D11REGOFFS(radioregdata));
	} else {
		bcma_wflush16(pi->d11core, D11REGOFFS(phy4waddr), addr);
		data = bcma_read16(pi->d11core, D11REGOFFS(phy4wdatalo));
	}
	pi->phy_wreg = 0;

	return data;
}

void write_radio_reg(struct brcms_phy *pi, u16 addr, u16 val)
{
	if ((D11REV_GE(pi->sh->corerev, 24)) ||
	    (D11REV_IS(pi->sh->corerev, 22)
	     && (pi->pubpi.phy_type != PHY_TYPE_SSN))) {

		bcma_wflush16(pi->d11core, D11REGOFFS(radioregaddr), addr);
		bcma_write16(pi->d11core, D11REGOFFS(radioregdata), val);
	} else {
		bcma_wflush16(pi->d11core, D11REGOFFS(phy4waddr), addr);
		bcma_write16(pi->d11core, D11REGOFFS(phy4wdatalo), val);
	}

	if ((pi->d11core->bus->hosttype == BCMA_HOSTTYPE_PCI) &&
	    (++pi->phy_wreg >= pi->phy_wreg_limit)) {
		(void)bcma_read32(pi->d11core, D11REGOFFS(maccontrol));
		pi->phy_wreg = 0;
	}
}

static u32 read_radio_id(struct brcms_phy *pi)
{
	u32 id;

	if (D11REV_GE(pi->sh->corerev, 24)) {
		u32 b0, b1, b2;

		bcma_wflush16(pi->d11core, D11REGOFFS(radioregaddr), 0);
		b0 = (u32) bcma_read16(pi->d11core, D11REGOFFS(radioregdata));
		bcma_wflush16(pi->d11core, D11REGOFFS(radioregaddr), 1);
		b1 = (u32) bcma_read16(pi->d11core, D11REGOFFS(radioregdata));
		bcma_wflush16(pi->d11core, D11REGOFFS(radioregaddr), 2);
		b2 = (u32) bcma_read16(pi->d11core, D11REGOFFS(radioregdata));

		id = ((b0 & 0xf) << 28) | (((b2 << 8) | b1) << 12) | ((b0 >> 4)
								      & 0xf);
	} else {
		bcma_wflush16(pi->d11core, D11REGOFFS(phy4waddr), RADIO_IDCODE);
		id = (u32) bcma_read16(pi->d11core, D11REGOFFS(phy4wdatalo));
		id |= (u32) bcma_read16(pi->d11core,
					D11REGOFFS(phy4wdatahi)) << 16;
	}
	pi->phy_wreg = 0;
	return id;
}

void and_radio_reg(struct brcms_phy *pi, u16 addr, u16 val)
{
	u16 rval;

	rval = read_radio_reg(pi, addr);
	write_radio_reg(pi, addr, (rval & val));
}

void or_radio_reg(struct brcms_phy *pi, u16 addr, u16 val)
{
	u16 rval;

	rval = read_radio_reg(pi, addr);
	write_radio_reg(pi, addr, (rval | val));
}

void xor_radio_reg(struct brcms_phy *pi, u16 addr, u16 mask)
{
	u16 rval;

	rval = read_radio_reg(pi, addr);
	write_radio_reg(pi, addr, (rval ^ mask));
}

void mod_radio_reg(struct brcms_phy *pi, u16 addr, u16 mask, u16 val)
{
	u16 rval;

	rval = read_radio_reg(pi, addr);
	write_radio_reg(pi, addr, (rval & ~mask) | (val & mask));
}

void write_phy_channel_reg(struct brcms_phy *pi, uint val)
{
	bcma_write16(pi->d11core, D11REGOFFS(phychannel), val);
}

u16 read_phy_reg(struct brcms_phy *pi, u16 addr)
{
	bcma_wflush16(pi->d11core, D11REGOFFS(phyregaddr), addr);

	pi->phy_wreg = 0;
	return bcma_read16(pi->d11core, D11REGOFFS(phyregdata));
}

void write_phy_reg(struct brcms_phy *pi, u16 addr, u16 val)
{
#ifdef CONFIG_BCM47XX
	bcma_wflush16(pi->d11core, D11REGOFFS(phyregaddr), addr);
	bcma_write16(pi->d11core, D11REGOFFS(phyregdata), val);
	if (addr == 0x72)
		(void)bcma_read16(pi->d11core, D11REGOFFS(phyregdata));
#else
	bcma_write32(pi->d11core, D11REGOFFS(phyregaddr), addr | (val << 16));
	if ((pi->d11core->bus->hosttype == BCMA_HOSTTYPE_PCI) &&
	    (++pi->phy_wreg >= pi->phy_wreg_limit)) {
		pi->phy_wreg = 0;
		(void)bcma_read16(pi->d11core, D11REGOFFS(phyversion));
	}
#endif
}

void and_phy_reg(struct brcms_phy *pi, u16 addr, u16 val)
{
	bcma_wflush16(pi->d11core, D11REGOFFS(phyregaddr), addr);
	bcma_mask16(pi->d11core, D11REGOFFS(phyregdata), val);
	pi->phy_wreg = 0;
}

void or_phy_reg(struct brcms_phy *pi, u16 addr, u16 val)
{
	bcma_wflush16(pi->d11core, D11REGOFFS(phyregaddr), addr);
	bcma_set16(pi->d11core, D11REGOFFS(phyregdata), val);
	pi->phy_wreg = 0;
}

void mod_phy_reg(struct brcms_phy *pi, u16 addr, u16 mask, u16 val)
{
	val &= mask;
	bcma_wflush16(pi->d11core, D11REGOFFS(phyregaddr), addr);
	bcma_maskset16(pi->d11core, D11REGOFFS(phyregdata), ~mask, val);
	pi->phy_wreg = 0;
}

static void wlc_set_phy_uninitted(struct brcms_phy *pi)
{
	int i, j;

	pi->initialized = false;

	pi->tx_vos = 0xffff;
	pi->nrssi_table_delta = 0x7fffffff;
	pi->rc_cal = 0xffff;
	pi->mintxbias = 0xffff;
	pi->txpwridx = -1;
	if (ISNPHY(pi)) {
		pi->phy_spuravoid = SPURAVOID_DISABLE;

		if (NREV_GE(pi->pubpi.phy_rev, 3)
		    && NREV_LT(pi->pubpi.phy_rev, 7))
			pi->phy_spuravoid = SPURAVOID_AUTO;

		pi->nphy_papd_skip = 0;
		pi->nphy_papd_epsilon_offset[0] = 0xf588;
		pi->nphy_papd_epsilon_offset[1] = 0xf588;
		pi->nphy_txpwr_idx[0] = 128;
		pi->nphy_txpwr_idx[1] = 128;
		pi->nphy_txpwrindex[0].index_internal = 40;
		pi->nphy_txpwrindex[1].index_internal = 40;
		pi->phy_pabias = 0;
	} else {
		pi->phy_spuravoid = SPURAVOID_AUTO;
	}
	pi->radiopwr = 0xffff;
	for (i = 0; i < STATIC_NUM_RF; i++) {
		for (j = 0; j < STATIC_NUM_BB; j++)
			pi->stats_11b_txpower[i][j] = -1;
	}
}

struct shared_phy *wlc_phy_shared_attach(struct shared_phy_params *shp)
{
	struct shared_phy *sh;

	sh = kzalloc(sizeof(struct shared_phy), GFP_ATOMIC);
	if (sh == NULL)
		return NULL;

	sh->physhim = shp->physhim;
	sh->unit = shp->unit;
	sh->corerev = shp->corerev;

	sh->vid = shp->vid;
	sh->did = shp->did;
	sh->chip = shp->chip;
	sh->chiprev = shp->chiprev;
	sh->chippkg = shp->chippkg;
	sh->sromrev = shp->sromrev;
	sh->boardtype = shp->boardtype;
	sh->boardrev = shp->boardrev;
	sh->boardflags = shp->boardflags;
	sh->boardflags2 = shp->boardflags2;

	sh->fast_timer = PHY_SW_TIMER_FAST;
	sh->slow_timer = PHY_SW_TIMER_SLOW;
	sh->glacial_timer = PHY_SW_TIMER_GLACIAL;

	sh->rssi_mode = RSSI_ANT_MERGE_MAX;

	return sh;
}

static void wlc_phy_timercb_phycal(struct brcms_phy *pi)
{
	uint delay = 5;

	if (PHY_PERICAL_MPHASE_PENDING(pi)) {
		if (!pi->sh->up) {
			wlc_phy_cal_perical_mphase_reset(pi);
			return;
		}

		if (SCAN_RM_IN_PROGRESS(pi) || PLT_INPROG_PHY(pi)) {

			delay = 1000;
			wlc_phy_cal_perical_mphase_restart(pi);
		} else
			wlc_phy_cal_perical_nphy_run(pi, PHY_PERICAL_AUTO);
		wlapi_add_timer(pi->phycal_timer, delay, 0);
		return;
	}

}

static u32 wlc_phy_get_radio_ver(struct brcms_phy *pi)
{
	u32 ver;

	ver = read_radio_id(pi);

	return ver;
}

struct brcms_phy_pub *
wlc_phy_attach(struct shared_phy *sh, struct bcma_device *d11core,
	       int bandtype, struct wiphy *wiphy)
{
	struct brcms_phy *pi;
	u32 sflags = 0;
	uint phyversion;
	u32 idcode;
	int i;

	if (D11REV_IS(sh->corerev, 4))
		sflags = SISF_2G_PHY | SISF_5G_PHY;
	else
		sflags = bcma_aread32(d11core, BCMA_IOST);

	if (bandtype == BRCM_BAND_5G) {
		if ((sflags & (SISF_5G_PHY | SISF_DB_PHY)) == 0)
			return NULL;
	}

	pi = sh->phy_head;
	if ((sflags & SISF_DB_PHY) && pi) {
		wlapi_bmac_corereset(pi->sh->physhim, pi->pubpi.coreflags);
		pi->refcnt++;
		return &pi->pubpi_ro;
	}

	pi = kzalloc(sizeof(struct brcms_phy), GFP_ATOMIC);
	if (pi == NULL)
		return NULL;
	pi->wiphy = wiphy;
	pi->d11core = d11core;
	pi->sh = sh;
	pi->phy_init_por = true;
	pi->phy_wreg_limit = PHY_WREG_LIMIT;

	pi->txpwr_percent = 100;

	pi->do_initcal = true;

	pi->phycal_tempdelta = 0;

	if (bandtype == BRCM_BAND_2G && (sflags & SISF_2G_PHY))
		pi->pubpi.coreflags = SICF_GMODE;

	wlapi_bmac_corereset(pi->sh->physhim, pi->pubpi.coreflags);
	phyversion = bcma_read16(pi->d11core, D11REGOFFS(phyversion));

	pi->pubpi.phy_type = PHY_TYPE(phyversion);
	pi->pubpi.phy_rev = phyversion & PV_PV_MASK;

	if (pi->pubpi.phy_type == PHY_TYPE_LCNXN) {
		pi->pubpi.phy_type = PHY_TYPE_N;
		pi->pubpi.phy_rev += LCNXN_BASEREV;
	}
	pi->pubpi.phy_corenum = PHY_CORE_NUM_2;
	pi->pubpi.ana_rev = (phyversion & PV_AV_MASK) >> PV_AV_SHIFT;

	if (pi->pubpi.phy_type != PHY_TYPE_N &&
	    pi->pubpi.phy_type != PHY_TYPE_LCN)
		goto err;

	if (bandtype == BRCM_BAND_5G) {
		if (!ISNPHY(pi))
			goto err;
	} else if (!ISNPHY(pi) && !ISLCNPHY(pi)) {
		goto err;
	}

	wlc_phy_anacore((struct brcms_phy_pub *) pi, ON);

	idcode = wlc_phy_get_radio_ver(pi);
	pi->pubpi.radioid =
		(idcode & IDCODE_ID_MASK) >> IDCODE_ID_SHIFT;
	pi->pubpi.radiorev =
		(idcode & IDCODE_REV_MASK) >> IDCODE_REV_SHIFT;
	pi->pubpi.radiover =
		(idcode & IDCODE_VER_MASK) >> IDCODE_VER_SHIFT;
	if (!VALID_RADIO(pi, pi->pubpi.radioid))
		goto err;

	wlc_phy_switch_radio((struct brcms_phy_pub *) pi, OFF);

	wlc_set_phy_uninitted(pi);

	pi->bw = WL_CHANSPEC_BW_20;
	pi->radio_chanspec = (bandtype == BRCM_BAND_2G) ?
			     ch20mhz_chspec(1) : ch20mhz_chspec(36);

	pi->rxiq_samps = PHY_NOISE_SAMPLE_LOG_NUM_NPHY;
	pi->rxiq_antsel = ANT_RX_DIV_DEF;

	pi->watchdog_override = true;

	pi->cal_type_override = PHY_PERICAL_AUTO;

	pi->nphy_saved_noisevars.bufcount = 0;

	if (ISNPHY(pi))
		pi->min_txpower = PHY_TXPWR_MIN_NPHY;
	else
		pi->min_txpower = PHY_TXPWR_MIN;

	pi->sh->phyrxchain = 0x3;

	pi->rx2tx_biasentry = -1;

	pi->phy_txcore_disable_temp = PHY_CHAIN_TX_DISABLE_TEMP;
	pi->phy_txcore_enable_temp =
		PHY_CHAIN_TX_DISABLE_TEMP - PHY_HYSTERESIS_DELTATEMP;
	pi->phy_tempsense_offset = 0;
	pi->phy_txcore_heatedup = false;

	pi->nphy_lastcal_temp = -50;

	pi->phynoise_polling = true;
	if (ISNPHY(pi) || ISLCNPHY(pi))
		pi->phynoise_polling = false;

	for (i = 0; i < TXP_NUM_RATES; i++) {
		pi->txpwr_limit[i] = BRCMS_TXPWR_MAX;
		pi->txpwr_env_limit[i] = BRCMS_TXPWR_MAX;
		pi->tx_user_target[i] = BRCMS_TXPWR_MAX;
	}

	pi->radiopwr_override = RADIOPWR_OVERRIDE_DEF;

	pi->user_txpwr_at_rfport = false;

	if (ISNPHY(pi)) {

		pi->phycal_timer = wlapi_init_timer(pi->sh->physhim,
						    wlc_phy_timercb_phycal,
						    pi, "phycal");
		if (!pi->phycal_timer)
			goto err;

		if (!wlc_phy_attach_nphy(pi))
			goto err;

	} else if (ISLCNPHY(pi)) {
		if (!wlc_phy_attach_lcnphy(pi))
			goto err;

	}

	pi->refcnt++;
	pi->next = pi->sh->phy_head;
	sh->phy_head = pi;

	memcpy(&pi->pubpi_ro, &pi->pubpi, sizeof(struct brcms_phy_pub));

	return &pi->pubpi_ro;

err:
	kfree(pi);
	return NULL;
}

void wlc_phy_detach(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	if (pih) {
		if (--pi->refcnt)
			return;

		if (pi->phycal_timer) {
			wlapi_free_timer(pi->phycal_timer);
			pi->phycal_timer = NULL;
		}

		if (pi->sh->phy_head == pi)
			pi->sh->phy_head = pi->next;
		else if (pi->sh->phy_head->next == pi)
			pi->sh->phy_head->next = NULL;

		if (pi->pi_fptr.detach)
			(pi->pi_fptr.detach)(pi);

		kfree(pi);
	}
}

bool
wlc_phy_get_phyversion(struct brcms_phy_pub *pih, u16 *phytype, u16 *phyrev,
		       u16 *radioid, u16 *radiover)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	*phytype = (u16) pi->pubpi.phy_type;
	*phyrev = (u16) pi->pubpi.phy_rev;
	*radioid = pi->pubpi.radioid;
	*radiover = pi->pubpi.radiorev;

	return true;
}

bool wlc_phy_get_encore(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	return pi->pubpi.abgphy_encore;
}

u32 wlc_phy_get_coreflags(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	return pi->pubpi.coreflags;
}

void wlc_phy_anacore(struct brcms_phy_pub *pih, bool on)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	if (ISNPHY(pi)) {
		if (on) {
			if (NREV_GE(pi->pubpi.phy_rev, 3)) {
				write_phy_reg(pi, 0xa6, 0x0d);
				write_phy_reg(pi, 0x8f, 0x0);
				write_phy_reg(pi, 0xa7, 0x0d);
				write_phy_reg(pi, 0xa5, 0x0);
			} else {
				write_phy_reg(pi, 0xa5, 0x0);
			}
		} else {
			if (NREV_GE(pi->pubpi.phy_rev, 3)) {
				write_phy_reg(pi, 0x8f, 0x07ff);
				write_phy_reg(pi, 0xa6, 0x0fd);
				write_phy_reg(pi, 0xa5, 0x07ff);
				write_phy_reg(pi, 0xa7, 0x0fd);
			} else {
				write_phy_reg(pi, 0xa5, 0x7fff);
			}
		}
	} else if (ISLCNPHY(pi)) {
		if (on) {
			and_phy_reg(pi, 0x43b,
				    ~((0x1 << 0) | (0x1 << 1) | (0x1 << 2)));
		} else {
			or_phy_reg(pi, 0x43c,
				   (0x1 << 0) | (0x1 << 1) | (0x1 << 2));
			or_phy_reg(pi, 0x43b,
				   (0x1 << 0) | (0x1 << 1) | (0x1 << 2));
		}
	}
}

u32 wlc_phy_clk_bwbits(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	u32 phy_bw_clkbits = 0;

	if (pi && (ISNPHY(pi) || ISLCNPHY(pi))) {
		switch (pi->bw) {
		case WL_CHANSPEC_BW_10:
			phy_bw_clkbits = SICF_BW10;
			break;
		case WL_CHANSPEC_BW_20:
			phy_bw_clkbits = SICF_BW20;
			break;
		case WL_CHANSPEC_BW_40:
			phy_bw_clkbits = SICF_BW40;
			break;
		default:
			break;
		}
	}

	return phy_bw_clkbits;
}

void wlc_phy_por_inform(struct brcms_phy_pub *ppi)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	pi->phy_init_por = true;
}

void wlc_phy_edcrs_lock(struct brcms_phy_pub *pih, bool lock)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	pi->edcrs_threshold_lock = lock;

	write_phy_reg(pi, 0x22c, 0x46b);
	write_phy_reg(pi, 0x22d, 0x46b);
	write_phy_reg(pi, 0x22e, 0x3c0);
	write_phy_reg(pi, 0x22f, 0x3c0);
}

void wlc_phy_initcal_enable(struct brcms_phy_pub *pih, bool initcal)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	pi->do_initcal = initcal;
}

void wlc_phy_hw_clk_state_upd(struct brcms_phy_pub *pih, bool newstate)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	if (!pi || !pi->sh)
		return;

	pi->sh->clk = newstate;
}

void wlc_phy_hw_state_upd(struct brcms_phy_pub *pih, bool newstate)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	if (!pi || !pi->sh)
		return;

	pi->sh->up = newstate;
}

void wlc_phy_init(struct brcms_phy_pub *pih, u16 chanspec)
{
	u32 mc;
	void (*phy_init)(struct brcms_phy *) = NULL;
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	if (pi->init_in_progress)
		return;

	pi->init_in_progress = true;

	pi->radio_chanspec = chanspec;

	mc = bcma_read32(pi->d11core, D11REGOFFS(maccontrol));
	if (WARN(mc & MCTL_EN_MAC, "HW error MAC running on init"))
		return;

	if (!(pi->measure_hold & PHY_HOLD_FOR_SCAN))
		pi->measure_hold |= PHY_HOLD_FOR_NOT_ASSOC;

	if (WARN(!(bcma_aread32(pi->d11core, BCMA_IOST) & SISF_FCLKA),
		 "HW error SISF_FCLKA\n"))
		return;

	phy_init = pi->pi_fptr.init;

	if (phy_init == NULL)
		return;

	wlc_phy_anacore(pih, ON);

	if (CHSPEC_BW(pi->radio_chanspec) != pi->bw)
		wlapi_bmac_bw_set(pi->sh->physhim,
				  CHSPEC_BW(pi->radio_chanspec));

	pi->nphy_gain_boost = true;

	wlc_phy_switch_radio((struct brcms_phy_pub *) pi, ON);

	(*phy_init)(pi);

	pi->phy_init_por = false;

	if (D11REV_IS(pi->sh->corerev, 11) || D11REV_IS(pi->sh->corerev, 12))
		wlc_phy_do_dummy_tx(pi, true, OFF);

	if (!(ISNPHY(pi)))
		wlc_phy_txpower_update_shm(pi);

	wlc_phy_ant_rxdiv_set((struct brcms_phy_pub *) pi, pi->sh->rx_antdiv);

	pi->init_in_progress = false;
}

void wlc_phy_cal_init(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	void (*cal_init)(struct brcms_phy *) = NULL;

	if (WARN((bcma_read32(pi->d11core, D11REGOFFS(maccontrol)) &
		  MCTL_EN_MAC) != 0, "HW error: MAC enabled during phy cal\n"))
		return;

	if (!pi->initialized) {
		cal_init = pi->pi_fptr.calinit;
		if (cal_init)
			(*cal_init)(pi);

		pi->initialized = true;
	}
}

int wlc_phy_down(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	int callbacks = 0;

	if (pi->phycal_timer
	    && !wlapi_del_timer(pi->phycal_timer))
		callbacks++;

	pi->nphy_iqcal_chanspec_2G = 0;
	pi->nphy_iqcal_chanspec_5G = 0;

	return callbacks;
}

void
wlc_phy_table_addr(struct brcms_phy *pi, uint tbl_id, uint tbl_offset,
		   u16 tblAddr, u16 tblDataHi, u16 tblDataLo)
{
	write_phy_reg(pi, tblAddr, (tbl_id << 10) | tbl_offset);

	pi->tbl_data_hi = tblDataHi;
	pi->tbl_data_lo = tblDataLo;

	if (pi->sh->chip == BCMA_CHIP_ID_BCM43224 &&
	    pi->sh->chiprev == 1) {
		pi->tbl_addr = tblAddr;
		pi->tbl_save_id = tbl_id;
		pi->tbl_save_offset = tbl_offset;
	}
}

void wlc_phy_table_data_write(struct brcms_phy *pi, uint width, u32 val)
{
	if ((pi->sh->chip == BCMA_CHIP_ID_BCM43224) &&
	    (pi->sh->chiprev == 1) &&
	    (pi->tbl_save_id == NPHY_TBL_ID_ANTSWCTRLLUT)) {
		read_phy_reg(pi, pi->tbl_data_lo);

		write_phy_reg(pi, pi->tbl_addr,
			      (pi->tbl_save_id << 10) | pi->tbl_save_offset);
		pi->tbl_save_offset++;
	}

	if (width == 32) {
		write_phy_reg(pi, pi->tbl_data_hi, (u16) (val >> 16));
		write_phy_reg(pi, pi->tbl_data_lo, (u16) val);
	} else {
		write_phy_reg(pi, pi->tbl_data_lo, (u16) val);
	}
}

void
wlc_phy_write_table(struct brcms_phy *pi, const struct phytbl_info *ptbl_info,
		    u16 tblAddr, u16 tblDataHi, u16 tblDataLo)
{
	uint idx;
	uint tbl_id = ptbl_info->tbl_id;
	uint tbl_offset = ptbl_info->tbl_offset;
	uint tbl_width = ptbl_info->tbl_width;
	const u8 *ptbl_8b = (const u8 *)ptbl_info->tbl_ptr;
	const u16 *ptbl_16b = (const u16 *)ptbl_info->tbl_ptr;
	const u32 *ptbl_32b = (const u32 *)ptbl_info->tbl_ptr;

	write_phy_reg(pi, tblAddr, (tbl_id << 10) | tbl_offset);

	for (idx = 0; idx < ptbl_info->tbl_len; idx++) {

		if ((pi->sh->chip == BCMA_CHIP_ID_BCM43224) &&
		    (pi->sh->chiprev == 1) &&
		    (tbl_id == NPHY_TBL_ID_ANTSWCTRLLUT)) {
			read_phy_reg(pi, tblDataLo);

			write_phy_reg(pi, tblAddr,
				      (tbl_id << 10) | (tbl_offset + idx));
		}

		if (tbl_width == 32) {
			write_phy_reg(pi, tblDataHi,
				      (u16) (ptbl_32b[idx] >> 16));
			write_phy_reg(pi, tblDataLo, (u16) ptbl_32b[idx]);
		} else if (tbl_width == 16) {
			write_phy_reg(pi, tblDataLo, ptbl_16b[idx]);
		} else {
			write_phy_reg(pi, tblDataLo, ptbl_8b[idx]);
		}
	}
}

void
wlc_phy_read_table(struct brcms_phy *pi, const struct phytbl_info *ptbl_info,
		   u16 tblAddr, u16 tblDataHi, u16 tblDataLo)
{
	uint idx;
	uint tbl_id = ptbl_info->tbl_id;
	uint tbl_offset = ptbl_info->tbl_offset;
	uint tbl_width = ptbl_info->tbl_width;
	u8 *ptbl_8b = (u8 *)ptbl_info->tbl_ptr;
	u16 *ptbl_16b = (u16 *)ptbl_info->tbl_ptr;
	u32 *ptbl_32b = (u32 *)ptbl_info->tbl_ptr;

	write_phy_reg(pi, tblAddr, (tbl_id << 10) | tbl_offset);

	for (idx = 0; idx < ptbl_info->tbl_len; idx++) {

		if ((pi->sh->chip == BCMA_CHIP_ID_BCM43224) &&
		    (pi->sh->chiprev == 1)) {
			(void)read_phy_reg(pi, tblDataLo);

			write_phy_reg(pi, tblAddr,
				      (tbl_id << 10) | (tbl_offset + idx));
		}

		if (tbl_width == 32) {
			ptbl_32b[idx] = read_phy_reg(pi, tblDataLo);
			ptbl_32b[idx] |= (read_phy_reg(pi, tblDataHi) << 16);
		} else if (tbl_width == 16) {
			ptbl_16b[idx] = read_phy_reg(pi, tblDataLo);
		} else {
			ptbl_8b[idx] = (u8) read_phy_reg(pi, tblDataLo);
		}
	}
}

uint
wlc_phy_init_radio_regs_allbands(struct brcms_phy *pi,
				 struct radio_20xx_regs *radioregs)
{
	uint i = 0;

	do {
		if (radioregs[i].do_init)
			write_radio_reg(pi, radioregs[i].address,
					(u16) radioregs[i].init);

		i++;
	} while (radioregs[i].address != 0xffff);

	return i;
}

uint
wlc_phy_init_radio_regs(struct brcms_phy *pi,
			const struct radio_regs *radioregs,
			u16 core_offset)
{
	uint i = 0;
	uint count = 0;

	do {
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			if (radioregs[i].do_init_a) {
				write_radio_reg(pi,
						radioregs[i].
						address | core_offset,
						(u16) radioregs[i].init_a);
				if (ISNPHY(pi) && (++count % 4 == 0))
					BRCMS_PHY_WAR_PR51571(pi);
			}
		} else {
			if (radioregs[i].do_init_g) {
				write_radio_reg(pi,
						radioregs[i].
						address | core_offset,
						(u16) radioregs[i].init_g);
				if (ISNPHY(pi) && (++count % 4 == 0))
					BRCMS_PHY_WAR_PR51571(pi);
			}
		}

		i++;
	} while (radioregs[i].address != 0xffff);

	return i;
}

void wlc_phy_do_dummy_tx(struct brcms_phy *pi, bool ofdm, bool pa_on)
{
#define DUMMY_PKT_LEN   20
	struct bcma_device *core = pi->d11core;
	int i, count;
	u8 ofdmpkt[DUMMY_PKT_LEN] = {
		0xcc, 0x01, 0x02, 0x00, 0x00, 0x00, 0xd4, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
	};
	u8 cckpkt[DUMMY_PKT_LEN] = {
		0x6e, 0x84, 0x0b, 0x00, 0x00, 0x00, 0xd4, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
	};
	u32 *dummypkt;

	dummypkt = (u32 *) (ofdm ? ofdmpkt : cckpkt);
	wlapi_bmac_write_template_ram(pi->sh->physhim, 0, DUMMY_PKT_LEN,
				      dummypkt);

	bcma_write16(core, D11REGOFFS(xmtsel), 0);

	if (D11REV_GE(pi->sh->corerev, 11))
		bcma_write16(core, D11REGOFFS(wepctl), 0x100);
	else
		bcma_write16(core, D11REGOFFS(wepctl), 0);

	bcma_write16(core, D11REGOFFS(txe_phyctl),
		     (ofdm ? 1 : 0) | PHY_TXC_ANT_0);
	if (ISNPHY(pi) || ISLCNPHY(pi))
		bcma_write16(core, D11REGOFFS(txe_phyctl1), 0x1A02);

	bcma_write16(core, D11REGOFFS(txe_wm_0), 0);
	bcma_write16(core, D11REGOFFS(txe_wm_1), 0);

	bcma_write16(core, D11REGOFFS(xmttplatetxptr), 0);
	bcma_write16(core, D11REGOFFS(xmttxcnt), DUMMY_PKT_LEN);

	bcma_write16(core, D11REGOFFS(xmtsel),
		     ((8 << 8) | (1 << 5) | (1 << 2) | 2));

	bcma_write16(core, D11REGOFFS(txe_ctl), 0);

	if (!pa_on) {
		if (ISNPHY(pi))
			wlc_phy_pa_override_nphy(pi, OFF);
	}

	if (ISNPHY(pi) || ISLCNPHY(pi))
		bcma_write16(core, D11REGOFFS(txe_aux), 0xD0);
	else
		bcma_write16(core, D11REGOFFS(txe_aux), ((1 << 5) | (1 << 4)));

	(void)bcma_read16(core, D11REGOFFS(txe_aux));

	i = 0;
	count = ofdm ? 30 : 250;
	while ((i++ < count)
	       && (bcma_read16(core, D11REGOFFS(txe_status)) & (1 << 7)))
		udelay(10);

	i = 0;

	while ((i++ < 10) &&
	       ((bcma_read16(core, D11REGOFFS(txe_status)) & (1 << 10)) == 0))
		udelay(10);

	i = 0;

	while ((i++ < 10) &&
	       ((bcma_read16(core, D11REGOFFS(ifsstat)) & (1 << 8))))
		udelay(10);

	if (!pa_on) {
		if (ISNPHY(pi))
			wlc_phy_pa_override_nphy(pi, ON);
	}
}

void wlc_phy_hold_upd(struct brcms_phy_pub *pih, u32 id, bool set)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	if (set)
		mboolset(pi->measure_hold, id);
	else
		mboolclr(pi->measure_hold, id);

	return;
}

void wlc_phy_mute_upd(struct brcms_phy_pub *pih, bool mute, u32 flags)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	if (mute)
		mboolset(pi->measure_hold, PHY_HOLD_FOR_MUTE);
	else
		mboolclr(pi->measure_hold, PHY_HOLD_FOR_MUTE);

	if (!mute && (flags & PHY_MUTE_FOR_PREISM))
		pi->nphy_perical_last = pi->sh->now - pi->sh->glacial_timer;
	return;
}

void wlc_phy_clear_tssi(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	if (ISNPHY(pi)) {
		return;
	} else {
		wlapi_bmac_write_shm(pi->sh->physhim, M_B_TSSI_0, NULL_TSSI_W);
		wlapi_bmac_write_shm(pi->sh->physhim, M_B_TSSI_1, NULL_TSSI_W);
		wlapi_bmac_write_shm(pi->sh->physhim, M_G_TSSI_0, NULL_TSSI_W);
		wlapi_bmac_write_shm(pi->sh->physhim, M_G_TSSI_1, NULL_TSSI_W);
	}
}

static bool wlc_phy_cal_txpower_recalc_sw(struct brcms_phy *pi)
{
	return false;
}

void wlc_phy_switch_radio(struct brcms_phy_pub *pih, bool on)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	(void)bcma_read32(pi->d11core, D11REGOFFS(maccontrol));

	if (ISNPHY(pi)) {
		wlc_phy_switch_radio_nphy(pi, on);
	} else if (ISLCNPHY(pi)) {
		if (on) {
			and_phy_reg(pi, 0x44c,
				    ~((0x1 << 8) |
				      (0x1 << 9) |
				      (0x1 << 10) | (0x1 << 11) | (0x1 << 12)));
			and_phy_reg(pi, 0x4b0, ~((0x1 << 3) | (0x1 << 11)));
			and_phy_reg(pi, 0x4f9, ~(0x1 << 3));
		} else {
			and_phy_reg(pi, 0x44d,
				    ~((0x1 << 10) |
				      (0x1 << 11) |
				      (0x1 << 12) | (0x1 << 13) | (0x1 << 14)));
			or_phy_reg(pi, 0x44c,
				   (0x1 << 8) |
				   (0x1 << 9) |
				   (0x1 << 10) | (0x1 << 11) | (0x1 << 12));

			and_phy_reg(pi, 0x4b7, ~((0x7f << 8)));
			and_phy_reg(pi, 0x4b1, ~((0x1 << 13)));
			or_phy_reg(pi, 0x4b0, (0x1 << 3) | (0x1 << 11));
			and_phy_reg(pi, 0x4fa, ~((0x1 << 3)));
			or_phy_reg(pi, 0x4f9, (0x1 << 3));
		}
	}
}

u16 wlc_phy_bw_state_get(struct brcms_phy_pub *ppi)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	return pi->bw;
}

void wlc_phy_bw_state_set(struct brcms_phy_pub *ppi, u16 bw)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	pi->bw = bw;
}

void wlc_phy_chanspec_radio_set(struct brcms_phy_pub *ppi, u16 newch)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	pi->radio_chanspec = newch;

}

u16 wlc_phy_chanspec_get(struct brcms_phy_pub *ppi)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	return pi->radio_chanspec;
}

void wlc_phy_chanspec_set(struct brcms_phy_pub *ppi, u16 chanspec)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	u16 m_cur_channel;
	void (*chanspec_set)(struct brcms_phy *, u16) = NULL;
	m_cur_channel = CHSPEC_CHANNEL(chanspec);
	if (CHSPEC_IS5G(chanspec))
		m_cur_channel |= D11_CURCHANNEL_5G;
	if (CHSPEC_IS40(chanspec))
		m_cur_channel |= D11_CURCHANNEL_40;
	wlapi_bmac_write_shm(pi->sh->physhim, M_CURCHANNEL, m_cur_channel);

	chanspec_set = pi->pi_fptr.chanset;
	if (chanspec_set)
		(*chanspec_set)(pi, chanspec);

}

int wlc_phy_chanspec_freq2bandrange_lpssn(uint freq)
{
	int range = -1;

	if (freq < 2500)
		range = WL_CHAN_FREQ_RANGE_2G;
	else if (freq <= 5320)
		range = WL_CHAN_FREQ_RANGE_5GL;
	else if (freq <= 5700)
		range = WL_CHAN_FREQ_RANGE_5GM;
	else
		range = WL_CHAN_FREQ_RANGE_5GH;

	return range;
}

int wlc_phy_chanspec_bandrange_get(struct brcms_phy *pi, u16 chanspec)
{
	int range = -1;
	uint channel = CHSPEC_CHANNEL(chanspec);
	uint freq = wlc_phy_channel2freq(channel);

	if (ISNPHY(pi))
		range = wlc_phy_get_chan_freq_range_nphy(pi, channel);
	else if (ISLCNPHY(pi))
		range = wlc_phy_chanspec_freq2bandrange_lpssn(freq);

	return range;
}

void wlc_phy_chanspec_ch14_widefilter_set(struct brcms_phy_pub *ppi,
					  bool wide_filter)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	pi->channel_14_wide_filter = wide_filter;

}

int wlc_phy_channel2freq(uint channel)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(chan_info_all); i++)
		if (chan_info_all[i].chan == channel)
			return chan_info_all[i].freq;
	return 0;
}

void
wlc_phy_chanspec_band_validch(struct brcms_phy_pub *ppi, uint band,
			      struct brcms_chanvec *channels)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	uint i;
	uint channel;

	memset(channels, 0, sizeof(struct brcms_chanvec));

	for (i = 0; i < ARRAY_SIZE(chan_info_all); i++) {
		channel = chan_info_all[i].chan;

		if ((pi->a_band_high_disable) && (channel >= FIRST_REF5_CHANNUM)
		    && (channel <= LAST_REF5_CHANNUM))
			continue;

		if ((band == BRCM_BAND_2G && channel <= CH_MAX_2G_CHANNEL) ||
		    (band == BRCM_BAND_5G && channel > CH_MAX_2G_CHANNEL))
			setbit(channels->vec, channel);
	}
}

u16 wlc_phy_chanspec_band_firstch(struct brcms_phy_pub *ppi, uint band)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	uint i;
	uint channel;
	u16 chspec;

	for (i = 0; i < ARRAY_SIZE(chan_info_all); i++) {
		channel = chan_info_all[i].chan;

		if (ISNPHY(pi) && pi->bw == WL_CHANSPEC_BW_40) {
			uint j;

			for (j = 0; j < ARRAY_SIZE(chan_info_all); j++) {
				if (chan_info_all[j].chan ==
				    channel + CH_10MHZ_APART)
					break;
			}

			if (j == ARRAY_SIZE(chan_info_all))
				continue;

			channel = upper_20_sb(channel);
			chspec =  channel | WL_CHANSPEC_BW_40 |
				  WL_CHANSPEC_CTL_SB_LOWER;
			if (band == BRCM_BAND_2G)
				chspec |= WL_CHANSPEC_BAND_2G;
			else
				chspec |= WL_CHANSPEC_BAND_5G;
		} else
			chspec = ch20mhz_chspec(channel);

		if ((pi->a_band_high_disable) && (channel >= FIRST_REF5_CHANNUM)
		    && (channel <= LAST_REF5_CHANNUM))
			continue;

		if ((band == BRCM_BAND_2G && channel <= CH_MAX_2G_CHANNEL) ||
		    (band == BRCM_BAND_5G && channel > CH_MAX_2G_CHANNEL))
			return chspec;
	}

	return (u16) INVCHANSPEC;
}

int wlc_phy_txpower_get(struct brcms_phy_pub *ppi, uint *qdbm, bool *override)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	*qdbm = pi->tx_user_target[0];
	if (override != NULL)
		*override = pi->txpwroverride;
	return 0;
}

void wlc_phy_txpower_target_set(struct brcms_phy_pub *ppi,
				struct txpwr_limits *txpwr)
{
	bool mac_enabled = false;
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	memcpy(&pi->tx_user_target[TXP_FIRST_CCK],
	       &txpwr->cck[0], BRCMS_NUM_RATES_CCK);

	memcpy(&pi->tx_user_target[TXP_FIRST_OFDM],
	       &txpwr->ofdm[0], BRCMS_NUM_RATES_OFDM);
	memcpy(&pi->tx_user_target[TXP_FIRST_OFDM_20_CDD],
	       &txpwr->ofdm_cdd[0], BRCMS_NUM_RATES_OFDM);

	memcpy(&pi->tx_user_target[TXP_FIRST_OFDM_40_SISO],
	       &txpwr->ofdm_40_siso[0], BRCMS_NUM_RATES_OFDM);
	memcpy(&pi->tx_user_target[TXP_FIRST_OFDM_40_CDD],
	       &txpwr->ofdm_40_cdd[0], BRCMS_NUM_RATES_OFDM);

	memcpy(&pi->tx_user_target[TXP_FIRST_MCS_20_SISO],
	       &txpwr->mcs_20_siso[0], BRCMS_NUM_RATES_MCS_1_STREAM);
	memcpy(&pi->tx_user_target[TXP_FIRST_MCS_20_CDD],
	       &txpwr->mcs_20_cdd[0], BRCMS_NUM_RATES_MCS_1_STREAM);
	memcpy(&pi->tx_user_target[TXP_FIRST_MCS_20_STBC],
	       &txpwr->mcs_20_stbc[0], BRCMS_NUM_RATES_MCS_1_STREAM);
	memcpy(&pi->tx_user_target[TXP_FIRST_MCS_20_SDM],
	       &txpwr->mcs_20_mimo[0], BRCMS_NUM_RATES_MCS_2_STREAM);

	memcpy(&pi->tx_user_target[TXP_FIRST_MCS_40_SISO],
	       &txpwr->mcs_40_siso[0], BRCMS_NUM_RATES_MCS_1_STREAM);
	memcpy(&pi->tx_user_target[TXP_FIRST_MCS_40_CDD],
	       &txpwr->mcs_40_cdd[0], BRCMS_NUM_RATES_MCS_1_STREAM);
	memcpy(&pi->tx_user_target[TXP_FIRST_MCS_40_STBC],
	       &txpwr->mcs_40_stbc[0], BRCMS_NUM_RATES_MCS_1_STREAM);
	memcpy(&pi->tx_user_target[TXP_FIRST_MCS_40_SDM],
	       &txpwr->mcs_40_mimo[0], BRCMS_NUM_RATES_MCS_2_STREAM);

	if (bcma_read32(pi->d11core, D11REGOFFS(maccontrol)) & MCTL_EN_MAC)
		mac_enabled = true;

	if (mac_enabled)
		wlapi_suspend_mac_and_wait(pi->sh->physhim);

	wlc_phy_txpower_recalc_target(pi);
	wlc_phy_cal_txpower_recalc_sw(pi);

	if (mac_enabled)
		wlapi_enable_mac(pi->sh->physhim);
}

int wlc_phy_txpower_set(struct brcms_phy_pub *ppi, uint qdbm, bool override)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	int i;

	if (qdbm > 127)
		return -EINVAL;

	for (i = 0; i < TXP_NUM_RATES; i++)
		pi->tx_user_target[i] = (u8) qdbm;

	pi->txpwroverride = false;

	if (pi->sh->up) {
		if (!SCAN_INPROG_PHY(pi)) {
			bool suspend;

			suspend = (0 == (bcma_read32(pi->d11core,
						     D11REGOFFS(maccontrol)) &
					 MCTL_EN_MAC));

			if (!suspend)
				wlapi_suspend_mac_and_wait(pi->sh->physhim);

			wlc_phy_txpower_recalc_target(pi);
			wlc_phy_cal_txpower_recalc_sw(pi);

			if (!suspend)
				wlapi_enable_mac(pi->sh->physhim);
		}
	}
	return 0;
}

void
wlc_phy_txpower_sromlimit(struct brcms_phy_pub *ppi, uint channel, u8 *min_pwr,
			  u8 *max_pwr, int txp_rate_idx)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	uint i;

	*min_pwr = pi->min_txpower * BRCMS_TXPWR_DB_FACTOR;

	if (ISNPHY(pi)) {
		if (txp_rate_idx < 0)
			txp_rate_idx = TXP_FIRST_CCK;
		wlc_phy_txpower_sromlimit_get_nphy(pi, channel, max_pwr,
						   (u8) txp_rate_idx);

	} else if ((channel <= CH_MAX_2G_CHANNEL)) {
		if (txp_rate_idx < 0)
			txp_rate_idx = TXP_FIRST_CCK;
		*max_pwr = pi->tx_srom_max_rate_2g[txp_rate_idx];
	} else {

		*max_pwr = BRCMS_TXPWR_MAX;

		if (txp_rate_idx < 0)
			txp_rate_idx = TXP_FIRST_OFDM;

		for (i = 0; i < ARRAY_SIZE(chan_info_all); i++) {
			if (channel == chan_info_all[i].chan)
				break;
		}

		if (pi->hwtxpwr) {
			*max_pwr = pi->hwtxpwr[i];
		} else {

			if ((i >= FIRST_MID_5G_CHAN) && (i <= LAST_MID_5G_CHAN))
				*max_pwr =
				    pi->tx_srom_max_rate_5g_mid[txp_rate_idx];
			if ((i >= FIRST_HIGH_5G_CHAN)
			    && (i <= LAST_HIGH_5G_CHAN))
				*max_pwr =
				    pi->tx_srom_max_rate_5g_hi[txp_rate_idx];
			if ((i >= FIRST_LOW_5G_CHAN) && (i <= LAST_LOW_5G_CHAN))
				*max_pwr =
				    pi->tx_srom_max_rate_5g_low[txp_rate_idx];
		}
	}
}

void
wlc_phy_txpower_sromlimit_max_get(struct brcms_phy_pub *ppi, uint chan,
				  u8 *max_txpwr, u8 *min_txpwr)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	u8 tx_pwr_max = 0;
	u8 tx_pwr_min = 255;
	u8 max_num_rate;
	u8 maxtxpwr, mintxpwr, rate, pactrl;

	pactrl = 0;

	max_num_rate = ISNPHY(pi) ? TXP_NUM_RATES :
		       ISLCNPHY(pi) ? (TXP_LAST_SISO_MCS_20 +
				       1) : (TXP_LAST_OFDM + 1);

	for (rate = 0; rate < max_num_rate; rate++) {

		wlc_phy_txpower_sromlimit(ppi, chan, &mintxpwr, &maxtxpwr,
					  rate);

		maxtxpwr = (maxtxpwr > pactrl) ? (maxtxpwr - pactrl) : 0;

		maxtxpwr = (maxtxpwr > 6) ? (maxtxpwr - 6) : 0;

		tx_pwr_max = max(tx_pwr_max, maxtxpwr);
		tx_pwr_min = min(tx_pwr_min, maxtxpwr);
	}
	*max_txpwr = tx_pwr_max;
	*min_txpwr = tx_pwr_min;
}

void
wlc_phy_txpower_boardlimit_band(struct brcms_phy_pub *ppi, uint bandunit,
				s32 *max_pwr, s32 *min_pwr, u32 *step_pwr)
{
	return;
}

u8 wlc_phy_txpower_get_target_min(struct brcms_phy_pub *ppi)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	return pi->tx_power_min;
}

u8 wlc_phy_txpower_get_target_max(struct brcms_phy_pub *ppi)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	return pi->tx_power_max;
}

static s8 wlc_phy_env_measure_vbat(struct brcms_phy *pi)
{
	if (ISLCNPHY(pi))
		return wlc_lcnphy_vbatsense(pi, 0);
	else
		return 0;
}

static s8 wlc_phy_env_measure_temperature(struct brcms_phy *pi)
{
	if (ISLCNPHY(pi))
		return wlc_lcnphy_tempsense_degree(pi, 0);
	else
		return 0;
}

static void wlc_phy_upd_env_txpwr_rate_limits(struct brcms_phy *pi, u32 band)
{
	u8 i;
	s8 temp, vbat;

	for (i = 0; i < TXP_NUM_RATES; i++)
		pi->txpwr_env_limit[i] = BRCMS_TXPWR_MAX;

	vbat = wlc_phy_env_measure_vbat(pi);
	temp = wlc_phy_env_measure_temperature(pi);

}

static s8
wlc_user_txpwr_antport_to_rfport(struct brcms_phy *pi, uint chan, u32 band,
				 u8 rate)
{
	return 0;
}

void wlc_phy_txpower_recalc_target(struct brcms_phy *pi)
{
	u8 maxtxpwr, mintxpwr, rate, pactrl;
	uint target_chan;
	u8 tx_pwr_target[TXP_NUM_RATES];
	u8 tx_pwr_max = 0;
	u8 tx_pwr_min = 255;
	u8 tx_pwr_max_rate_ind = 0;
	u8 max_num_rate;
	u8 start_rate = 0;
	u16 chspec;
	u32 band = CHSPEC2BAND(pi->radio_chanspec);
	void (*txpwr_recalc_fn)(struct brcms_phy *) = NULL;

	chspec = pi->radio_chanspec;
	if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_NONE)
		target_chan = CHSPEC_CHANNEL(chspec);
	else if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_UPPER)
		target_chan = upper_20_sb(CHSPEC_CHANNEL(chspec));
	else
		target_chan = lower_20_sb(CHSPEC_CHANNEL(chspec));

	pactrl = 0;
	if (ISLCNPHY(pi)) {
		u32 offset_mcs, i;

		if (CHSPEC_IS40(pi->radio_chanspec)) {
			offset_mcs = pi->mcs40_po;
			for (i = TXP_FIRST_SISO_MCS_20;
			     i <= TXP_LAST_SISO_MCS_20; i++) {
				pi->tx_srom_max_rate_2g[i - 8] =
					pi->tx_srom_max_2g -
					((offset_mcs & 0xf) * 2);
				offset_mcs >>= 4;
			}
		} else {
			offset_mcs = pi->mcs20_po;
			for (i = TXP_FIRST_SISO_MCS_20;
			     i <= TXP_LAST_SISO_MCS_20; i++) {
				pi->tx_srom_max_rate_2g[i - 8] =
					pi->tx_srom_max_2g -
					((offset_mcs & 0xf) * 2);
				offset_mcs >>= 4;
			}
		}
	}

	max_num_rate = ((ISNPHY(pi)) ? (TXP_NUM_RATES) :
			((ISLCNPHY(pi)) ?
			 (TXP_LAST_SISO_MCS_20 + 1) : (TXP_LAST_OFDM + 1)));

	wlc_phy_upd_env_txpwr_rate_limits(pi, band);

	for (rate = start_rate; rate < max_num_rate; rate++) {

		tx_pwr_target[rate] = pi->tx_user_target[rate];

		if (pi->user_txpwr_at_rfport)
			tx_pwr_target[rate] +=
				wlc_user_txpwr_antport_to_rfport(pi,
								 target_chan,
								 band,
								 rate);

		wlc_phy_txpower_sromlimit((struct brcms_phy_pub *) pi,
					  target_chan,
					  &mintxpwr, &maxtxpwr, rate);

		maxtxpwr = min(maxtxpwr, pi->txpwr_limit[rate]);

		maxtxpwr = (maxtxpwr > pactrl) ? (maxtxpwr - pactrl) : 0;

		maxtxpwr = (maxtxpwr > 6) ? (maxtxpwr - 6) : 0;

		maxtxpwr = min(maxtxpwr, tx_pwr_target[rate]);

		if (pi->txpwr_percent <= 100)
			maxtxpwr = (maxtxpwr * pi->txpwr_percent) / 100;

		tx_pwr_target[rate] = max(maxtxpwr, mintxpwr);

		tx_pwr_target[rate] =
			min(tx_pwr_target[rate], pi->txpwr_env_limit[rate]);

		if (tx_pwr_target[rate] > tx_pwr_max)
			tx_pwr_max_rate_ind = rate;

		tx_pwr_max = max(tx_pwr_max, tx_pwr_target[rate]);
		tx_pwr_min = min(tx_pwr_min, tx_pwr_target[rate]);
	}

	memset(pi->tx_power_offset, 0, sizeof(pi->tx_power_offset));
	pi->tx_power_max = tx_pwr_max;
	pi->tx_power_min = tx_pwr_min;
	pi->tx_power_max_rate_ind = tx_pwr_max_rate_ind;
	for (rate = 0; rate < max_num_rate; rate++) {

		pi->tx_power_target[rate] = tx_pwr_target[rate];

		if (!pi->hwpwrctrl || ISNPHY(pi))
			pi->tx_power_offset[rate] =
				pi->tx_power_max - pi->tx_power_target[rate];
		else
			pi->tx_power_offset[rate] =
				pi->tx_power_target[rate] - pi->tx_power_min;
	}

	txpwr_recalc_fn = pi->pi_fptr.txpwrrecalc;
	if (txpwr_recalc_fn)
		(*txpwr_recalc_fn)(pi);
}

static void
wlc_phy_txpower_reg_limit_calc(struct brcms_phy *pi, struct txpwr_limits *txpwr,
			       u16 chanspec)
{
	u8 tmp_txpwr_limit[2 * BRCMS_NUM_RATES_OFDM];
	u8 *txpwr_ptr1 = NULL, *txpwr_ptr2 = NULL;
	int rate_start_index = 0, rate1, rate2, k;

	for (rate1 = WL_TX_POWER_CCK_FIRST, rate2 = 0;
	     rate2 < WL_TX_POWER_CCK_NUM; rate1++, rate2++)
		pi->txpwr_limit[rate1] = txpwr->cck[rate2];

	for (rate1 = WL_TX_POWER_OFDM_FIRST, rate2 = 0;
	     rate2 < WL_TX_POWER_OFDM_NUM; rate1++, rate2++)
		pi->txpwr_limit[rate1] = txpwr->ofdm[rate2];

	if (ISNPHY(pi)) {

		for (k = 0; k < 4; k++) {
			switch (k) {
			case 0:

				txpwr_ptr1 = txpwr->mcs_20_siso;
				txpwr_ptr2 = txpwr->ofdm;
				rate_start_index = WL_TX_POWER_OFDM_FIRST;
				break;
			case 1:

				txpwr_ptr1 = txpwr->mcs_20_cdd;
				txpwr_ptr2 = txpwr->ofdm_cdd;
				rate_start_index = WL_TX_POWER_OFDM20_CDD_FIRST;
				break;
			case 2:

				txpwr_ptr1 = txpwr->mcs_40_siso;
				txpwr_ptr2 = txpwr->ofdm_40_siso;
				rate_start_index =
					WL_TX_POWER_OFDM40_SISO_FIRST;
				break;
			case 3:

				txpwr_ptr1 = txpwr->mcs_40_cdd;
				txpwr_ptr2 = txpwr->ofdm_40_cdd;
				rate_start_index = WL_TX_POWER_OFDM40_CDD_FIRST;
				break;
			}

			for (rate2 = 0; rate2 < BRCMS_NUM_RATES_OFDM;
			     rate2++) {
				tmp_txpwr_limit[rate2] = 0;
				tmp_txpwr_limit[BRCMS_NUM_RATES_OFDM + rate2] =
					txpwr_ptr1[rate2];
			}
			wlc_phy_mcs_to_ofdm_powers_nphy(
				tmp_txpwr_limit, 0,
				BRCMS_NUM_RATES_OFDM -
				1, BRCMS_NUM_RATES_OFDM);
			for (rate1 = rate_start_index, rate2 = 0;
			     rate2 < BRCMS_NUM_RATES_OFDM; rate1++, rate2++)
				pi->txpwr_limit[rate1] =
					min(txpwr_ptr2[rate2],
					    tmp_txpwr_limit[rate2]);
		}

		for (k = 0; k < 4; k++) {
			switch (k) {
			case 0:

				txpwr_ptr1 = txpwr->ofdm;
				txpwr_ptr2 = txpwr->mcs_20_siso;
				rate_start_index = WL_TX_POWER_MCS20_SISO_FIRST;
				break;
			case 1:

				txpwr_ptr1 = txpwr->ofdm_cdd;
				txpwr_ptr2 = txpwr->mcs_20_cdd;
				rate_start_index = WL_TX_POWER_MCS20_CDD_FIRST;
				break;
			case 2:

				txpwr_ptr1 = txpwr->ofdm_40_siso;
				txpwr_ptr2 = txpwr->mcs_40_siso;
				rate_start_index = WL_TX_POWER_MCS40_SISO_FIRST;
				break;
			case 3:

				txpwr_ptr1 = txpwr->ofdm_40_cdd;
				txpwr_ptr2 = txpwr->mcs_40_cdd;
				rate_start_index = WL_TX_POWER_MCS40_CDD_FIRST;
				break;
			}
			for (rate2 = 0; rate2 < BRCMS_NUM_RATES_OFDM;
			     rate2++) {
				tmp_txpwr_limit[rate2] = 0;
				tmp_txpwr_limit[BRCMS_NUM_RATES_OFDM + rate2] =
					txpwr_ptr1[rate2];
			}
			wlc_phy_ofdm_to_mcs_powers_nphy(
				tmp_txpwr_limit, 0,
				BRCMS_NUM_RATES_OFDM -
				1, BRCMS_NUM_RATES_OFDM);
			for (rate1 = rate_start_index, rate2 = 0;
			     rate2 < BRCMS_NUM_RATES_MCS_1_STREAM;
			     rate1++, rate2++)
				pi->txpwr_limit[rate1] =
					min(txpwr_ptr2[rate2],
					    tmp_txpwr_limit[rate2]);
		}

		for (k = 0; k < 2; k++) {
			switch (k) {
			case 0:

				rate_start_index = WL_TX_POWER_MCS20_STBC_FIRST;
				txpwr_ptr1 = txpwr->mcs_20_stbc;
				break;
			case 1:

				rate_start_index = WL_TX_POWER_MCS40_STBC_FIRST;
				txpwr_ptr1 = txpwr->mcs_40_stbc;
				break;
			}
			for (rate1 = rate_start_index, rate2 = 0;
			     rate2 < BRCMS_NUM_RATES_MCS_1_STREAM;
			     rate1++, rate2++)
				pi->txpwr_limit[rate1] = txpwr_ptr1[rate2];
		}

		for (k = 0; k < 2; k++) {
			switch (k) {
			case 0:

				rate_start_index = WL_TX_POWER_MCS20_SDM_FIRST;
				txpwr_ptr1 = txpwr->mcs_20_mimo;
				break;
			case 1:

				rate_start_index = WL_TX_POWER_MCS40_SDM_FIRST;
				txpwr_ptr1 = txpwr->mcs_40_mimo;
				break;
			}
			for (rate1 = rate_start_index, rate2 = 0;
			     rate2 < BRCMS_NUM_RATES_MCS_2_STREAM;
			     rate1++, rate2++)
				pi->txpwr_limit[rate1] = txpwr_ptr1[rate2];
		}

		pi->txpwr_limit[WL_TX_POWER_MCS_32] = txpwr->mcs32;

		pi->txpwr_limit[WL_TX_POWER_MCS40_CDD_FIRST] =
			min(pi->txpwr_limit[WL_TX_POWER_MCS40_CDD_FIRST],
			    pi->txpwr_limit[WL_TX_POWER_MCS_32]);
		pi->txpwr_limit[WL_TX_POWER_MCS_32] =
			pi->txpwr_limit[WL_TX_POWER_MCS40_CDD_FIRST];
	}
}

void wlc_phy_txpwr_percent_set(struct brcms_phy_pub *ppi, u8 txpwr_percent)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	pi->txpwr_percent = txpwr_percent;
}

void wlc_phy_machwcap_set(struct brcms_phy_pub *ppi, u32 machwcap)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	pi->sh->machwcap = machwcap;
}

void wlc_phy_runbist_config(struct brcms_phy_pub *ppi, bool start_end)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	u16 rxc;
	rxc = 0;

	if (start_end == ON) {
		if (!ISNPHY(pi))
			return;

		if (NREV_IS(pi->pubpi.phy_rev, 3)
		    || NREV_IS(pi->pubpi.phy_rev, 4)) {
			bcma_wflush16(pi->d11core, D11REGOFFS(phyregaddr),
				      0xa0);
			bcma_set16(pi->d11core, D11REGOFFS(phyregdata),
				   0x1 << 15);
		}
	} else {
		if (NREV_IS(pi->pubpi.phy_rev, 3)
		    || NREV_IS(pi->pubpi.phy_rev, 4)) {
			bcma_wflush16(pi->d11core, D11REGOFFS(phyregaddr),
				      0xa0);
			bcma_write16(pi->d11core, D11REGOFFS(phyregdata), rxc);
		}

		wlc_phy_por_inform(ppi);
	}
}

void
wlc_phy_txpower_limit_set(struct brcms_phy_pub *ppi, struct txpwr_limits *txpwr,
			  u16 chanspec)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	wlc_phy_txpower_reg_limit_calc(pi, txpwr, chanspec);

	if (ISLCNPHY(pi)) {
		int i, j;
		for (i = TXP_FIRST_OFDM_20_CDD, j = 0;
		     j < BRCMS_NUM_RATES_MCS_1_STREAM; i++, j++) {
			if (txpwr->mcs_20_siso[j])
				pi->txpwr_limit[i] = txpwr->mcs_20_siso[j];
			else
				pi->txpwr_limit[i] = txpwr->ofdm[j];
		}
	}

	wlapi_suspend_mac_and_wait(pi->sh->physhim);

	wlc_phy_txpower_recalc_target(pi);
	wlc_phy_cal_txpower_recalc_sw(pi);
	wlapi_enable_mac(pi->sh->physhim);
}

void wlc_phy_ofdm_rateset_war(struct brcms_phy_pub *pih, bool war)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	pi->ofdm_rateset_war = war;
}

void wlc_phy_bf_preempt_enable(struct brcms_phy_pub *pih, bool bf_preempt)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	pi->bf_preempt_4306 = bf_preempt;
}

void wlc_phy_txpower_update_shm(struct brcms_phy *pi)
{
	int j;
	if (ISNPHY(pi))
		return;

	if (!pi->sh->clk)
		return;

	if (pi->hwpwrctrl) {
		u16 offset;

		wlapi_bmac_write_shm(pi->sh->physhim, M_TXPWR_MAX, 63);
		wlapi_bmac_write_shm(pi->sh->physhim, M_TXPWR_N,
				     1 << NUM_TSSI_FRAMES);

		wlapi_bmac_write_shm(pi->sh->physhim, M_TXPWR_TARGET,
				     pi->tx_power_min << NUM_TSSI_FRAMES);

		wlapi_bmac_write_shm(pi->sh->physhim, M_TXPWR_CUR,
				     pi->hwpwr_txcur);

		for (j = TXP_FIRST_OFDM; j <= TXP_LAST_OFDM; j++) {
			static const u8 ucode_ofdm_rates[] = {
				0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c
			};
			offset = wlapi_bmac_rate_shm_offset(
				pi->sh->physhim,
				ucode_ofdm_rates[j - TXP_FIRST_OFDM]);
			wlapi_bmac_write_shm(pi->sh->physhim, offset + 6,
					     pi->tx_power_offset[j]);
			wlapi_bmac_write_shm(pi->sh->physhim, offset + 14,
					     -(pi->tx_power_offset[j] / 2));
		}

		wlapi_bmac_mhf(pi->sh->physhim, MHF2, MHF2_HWPWRCTL,
			       MHF2_HWPWRCTL, BRCM_BAND_ALL);
	} else {
		int i;

		for (i = TXP_FIRST_OFDM; i <= TXP_LAST_OFDM; i++)
			pi->tx_power_offset[i] =
				(u8) roundup(pi->tx_power_offset[i], 8);
		wlapi_bmac_write_shm(pi->sh->physhim, M_OFDM_OFFSET,
				     (u16)
				     ((pi->tx_power_offset[TXP_FIRST_OFDM]
				       + 7) >> 3));
	}
}

bool wlc_phy_txpower_hw_ctrl_get(struct brcms_phy_pub *ppi)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	if (ISNPHY(pi))
		return pi->nphy_txpwrctrl;
	else
		return pi->hwpwrctrl;
}

void wlc_phy_txpower_hw_ctrl_set(struct brcms_phy_pub *ppi, bool hwpwrctrl)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	bool suspend;

	if (!pi->hwpwrctrl_capable)
		return;

	pi->hwpwrctrl = hwpwrctrl;
	pi->nphy_txpwrctrl = hwpwrctrl;
	pi->txpwrctrl = hwpwrctrl;

	if (ISNPHY(pi)) {
		suspend = (0 == (bcma_read32(pi->d11core,
					     D11REGOFFS(maccontrol)) &
				 MCTL_EN_MAC));
		if (!suspend)
			wlapi_suspend_mac_and_wait(pi->sh->physhim);

		wlc_phy_txpwrctrl_enable_nphy(pi, pi->nphy_txpwrctrl);
		if (pi->nphy_txpwrctrl == PHY_TPC_HW_OFF)
			wlc_phy_txpwr_fixpower_nphy(pi);
		else
			mod_phy_reg(pi, 0x1e7, (0x7f << 0),
				    pi->saved_txpwr_idx);

		if (!suspend)
			wlapi_enable_mac(pi->sh->physhim);
	}
}

void wlc_phy_txpower_ipa_upd(struct brcms_phy *pi)
{

	if (NREV_GE(pi->pubpi.phy_rev, 3)) {
		pi->ipa2g_on = (pi->srom_fem2g.extpagain == 2);
		pi->ipa5g_on = (pi->srom_fem5g.extpagain == 2);
	} else {
		pi->ipa2g_on = false;
		pi->ipa5g_on = false;
	}
}

static u32 wlc_phy_txpower_est_power_nphy(struct brcms_phy *pi)
{
	s16 tx0_status, tx1_status;
	u16 estPower1, estPower2;
	u8 pwr0, pwr1, adj_pwr0, adj_pwr1;
	u32 est_pwr;

	estPower1 = read_phy_reg(pi, 0x118);
	estPower2 = read_phy_reg(pi, 0x119);

	if ((estPower1 & (0x1 << 8)) == (0x1 << 8))
		pwr0 = (u8) (estPower1 & (0xff << 0)) >> 0;
	else
		pwr0 = 0x80;

	if ((estPower2 & (0x1 << 8)) == (0x1 << 8))
		pwr1 = (u8) (estPower2 & (0xff << 0)) >> 0;
	else
		pwr1 = 0x80;

	tx0_status = read_phy_reg(pi, 0x1ed);
	tx1_status = read_phy_reg(pi, 0x1ee);

	if ((tx0_status & (0x1 << 15)) == (0x1 << 15))
		adj_pwr0 = (u8) (tx0_status & (0xff << 0)) >> 0;
	else
		adj_pwr0 = 0x80;
	if ((tx1_status & (0x1 << 15)) == (0x1 << 15))
		adj_pwr1 = (u8) (tx1_status & (0xff << 0)) >> 0;
	else
		adj_pwr1 = 0x80;

	est_pwr = (u32) ((pwr0 << 24) | (pwr1 << 16) | (adj_pwr0 << 8) |
			 adj_pwr1);

	return est_pwr;
}

void
wlc_phy_txpower_get_current(struct brcms_phy_pub *ppi, struct tx_power *power,
			    uint channel)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	uint rate, num_rates;
	u8 min_pwr, max_pwr;

#if WL_TX_POWER_RATES != TXP_NUM_RATES
#error "struct tx_power out of sync with this fn"
#endif

	if (ISNPHY(pi)) {
		power->rf_cores = 2;
		power->flags |= (WL_TX_POWER_F_MIMO);
		if (pi->nphy_txpwrctrl == PHY_TPC_HW_ON)
			power->flags |=
				(WL_TX_POWER_F_ENABLED | WL_TX_POWER_F_HW);
	} else if (ISLCNPHY(pi)) {
		power->rf_cores = 1;
		power->flags |= (WL_TX_POWER_F_SISO);
		if (pi->radiopwr_override == RADIOPWR_OVERRIDE_DEF)
			power->flags |= WL_TX_POWER_F_ENABLED;
		if (pi->hwpwrctrl)
			power->flags |= WL_TX_POWER_F_HW;
	}

	num_rates = ((ISNPHY(pi)) ? (TXP_NUM_RATES) :
		     ((ISLCNPHY(pi)) ?
		      (TXP_LAST_OFDM_20_CDD + 1) : (TXP_LAST_OFDM + 1)));

	for (rate = 0; rate < num_rates; rate++) {
		power->user_limit[rate] = pi->tx_user_target[rate];
		wlc_phy_txpower_sromlimit(ppi, channel, &min_pwr, &max_pwr,
					  rate);
		power->board_limit[rate] = (u8) max_pwr;
		power->target[rate] = pi->tx_power_target[rate];
	}

	if (ISNPHY(pi)) {
		u32 est_pout;

		wlapi_suspend_mac_and_wait(pi->sh->physhim);
		wlc_phyreg_enter((struct brcms_phy_pub *) pi);
		est_pout = wlc_phy_txpower_est_power_nphy(pi);
		wlc_phyreg_exit((struct brcms_phy_pub *) pi);
		wlapi_enable_mac(pi->sh->physhim);

		power->est_Pout[0] = (est_pout >> 8) & 0xff;
		power->est_Pout[1] = est_pout & 0xff;

		power->est_Pout_act[0] = est_pout >> 24;
		power->est_Pout_act[1] = (est_pout >> 16) & 0xff;

		if (power->est_Pout[0] == 0x80)
			power->est_Pout[0] = 0;
		if (power->est_Pout[1] == 0x80)
			power->est_Pout[1] = 0;

		if (power->est_Pout_act[0] == 0x80)
			power->est_Pout_act[0] = 0;
		if (power->est_Pout_act[1] == 0x80)
			power->est_Pout_act[1] = 0;

		power->est_Pout_cck = 0;

		power->tx_power_max[0] = pi->tx_power_max;
		power->tx_power_max[1] = pi->tx_power_max;

		power->tx_power_max_rate_ind[0] = pi->tx_power_max_rate_ind;
		power->tx_power_max_rate_ind[1] = pi->tx_power_max_rate_ind;
	} else if (pi->hwpwrctrl && pi->sh->up) {

		wlc_phyreg_enter(ppi);
		if (ISLCNPHY(pi)) {

			power->tx_power_max[0] = pi->tx_power_max;
			power->tx_power_max[1] = pi->tx_power_max;

			power->tx_power_max_rate_ind[0] =
				pi->tx_power_max_rate_ind;
			power->tx_power_max_rate_ind[1] =
				pi->tx_power_max_rate_ind;

			if (wlc_phy_tpc_isenabled_lcnphy(pi))
				power->flags |=
					(WL_TX_POWER_F_HW |
					 WL_TX_POWER_F_ENABLED);
			else
				power->flags &=
					~(WL_TX_POWER_F_HW |
					  WL_TX_POWER_F_ENABLED);

			wlc_lcnphy_get_tssi(pi, (s8 *) &power->est_Pout[0],
					    (s8 *) &power->est_Pout_cck);
		}
		wlc_phyreg_exit(ppi);
	}
}

void wlc_phy_antsel_type_set(struct brcms_phy_pub *ppi, u8 antsel_type)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	pi->antsel_type = antsel_type;
}

bool wlc_phy_test_ison(struct brcms_phy_pub *ppi)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	return pi->phytest_on;
}

void wlc_phy_ant_rxdiv_set(struct brcms_phy_pub *ppi, u8 val)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);
	bool suspend;

	pi->sh->rx_antdiv = val;

	if (!(ISNPHY(pi) && D11REV_IS(pi->sh->corerev, 16))) {
		if (val > ANT_RX_DIV_FORCE_1)
			wlapi_bmac_mhf(pi->sh->physhim, MHF1, MHF1_ANTDIV,
				       MHF1_ANTDIV, BRCM_BAND_ALL);
		else
			wlapi_bmac_mhf(pi->sh->physhim, MHF1, MHF1_ANTDIV, 0,
				       BRCM_BAND_ALL);
	}

	if (ISNPHY(pi))
		return;

	if (!pi->sh->clk)
		return;

	suspend = (0 == (bcma_read32(pi->d11core, D11REGOFFS(maccontrol)) &
			 MCTL_EN_MAC));
	if (!suspend)
		wlapi_suspend_mac_and_wait(pi->sh->physhim);

	if (ISLCNPHY(pi)) {
		if (val > ANT_RX_DIV_FORCE_1) {
			mod_phy_reg(pi, 0x410, (0x1 << 1), 0x01 << 1);
			mod_phy_reg(pi, 0x410,
				    (0x1 << 0),
				    ((ANT_RX_DIV_START_1 == val) ? 1 : 0) << 0);
		} else {
			mod_phy_reg(pi, 0x410, (0x1 << 1), 0x00 << 1);
			mod_phy_reg(pi, 0x410, (0x1 << 0), (u16) val << 0);
		}
	}

	if (!suspend)
		wlapi_enable_mac(pi->sh->physhim);

	return;
}

static bool
wlc_phy_noise_calc_phy(struct brcms_phy *pi, u32 *cmplx_pwr, s8 *pwr_ant)
{
	s8 cmplx_pwr_dbm[PHY_CORE_MAX];
	u8 i;

	memset((u8 *) cmplx_pwr_dbm, 0, sizeof(cmplx_pwr_dbm));
	wlc_phy_compute_dB(cmplx_pwr, cmplx_pwr_dbm, pi->pubpi.phy_corenum);

	for (i = 0; i < pi->pubpi.phy_corenum; i++) {
		if (NREV_GE(pi->pubpi.phy_rev, 3))
			cmplx_pwr_dbm[i] += (s8) PHY_NOISE_OFFSETFACT_4322;
		else

			cmplx_pwr_dbm[i] += (s8) (16 - (15) * 3 - 70);
	}

	for (i = 0; i < pi->pubpi.phy_corenum; i++) {
		pi->nphy_noise_win[i][pi->nphy_noise_index] = cmplx_pwr_dbm[i];
		pwr_ant[i] = cmplx_pwr_dbm[i];
	}
	pi->nphy_noise_index =
		MODINC_POW2(pi->nphy_noise_index, PHY_NOISE_WINDOW_SZ);
	return true;
}

static void wlc_phy_noise_cb(struct brcms_phy *pi, u8 channel, s8 noise_dbm)
{
	if (!pi->phynoise_state)
		return;

	if (pi->phynoise_state & PHY_NOISE_STATE_MON) {
		if (pi->phynoise_chan_watchdog == channel) {
			pi->sh->phy_noise_window[pi->sh->phy_noise_index] =
				noise_dbm;
			pi->sh->phy_noise_index =
				MODINC(pi->sh->phy_noise_index, MA_WINDOW_SZ);
		}
		pi->phynoise_state &= ~PHY_NOISE_STATE_MON;
	}

	if (pi->phynoise_state & PHY_NOISE_STATE_EXTERNAL)
		pi->phynoise_state &= ~PHY_NOISE_STATE_EXTERNAL;

}

static s8 wlc_phy_noise_read_shmem(struct brcms_phy *pi)
{
	u32 cmplx_pwr[PHY_CORE_MAX];
	s8 noise_dbm_ant[PHY_CORE_MAX];
	u16 lo, hi;
	u32 cmplx_pwr_tot = 0;
	s8 noise_dbm = PHY_NOISE_FIXED_VAL_NPHY;
	u8 idx, core;

	memset((u8 *) cmplx_pwr, 0, sizeof(cmplx_pwr));
	memset((u8 *) noise_dbm_ant, 0, sizeof(noise_dbm_ant));

	for (idx = 0, core = 0; core < pi->pubpi.phy_corenum; idx += 2,
	     core++) {
		lo = wlapi_bmac_read_shm(pi->sh->physhim, M_PWRIND_MAP(idx));
		hi = wlapi_bmac_read_shm(pi->sh->physhim,
					 M_PWRIND_MAP(idx + 1));
		cmplx_pwr[core] = (hi << 16) + lo;
		cmplx_pwr_tot += cmplx_pwr[core];
		if (cmplx_pwr[core] == 0)
			noise_dbm_ant[core] = PHY_NOISE_FIXED_VAL_NPHY;
		else
			cmplx_pwr[core] >>= PHY_NOISE_SAMPLE_LOG_NUM_UCODE;
	}

	if (cmplx_pwr_tot != 0)
		wlc_phy_noise_calc_phy(pi, cmplx_pwr, noise_dbm_ant);

	for (core = 0; core < pi->pubpi.phy_corenum; core++) {
		pi->nphy_noise_win[core][pi->nphy_noise_index] =
			noise_dbm_ant[core];

		if (noise_dbm_ant[core] > noise_dbm)
			noise_dbm = noise_dbm_ant[core];
	}
	pi->nphy_noise_index =
		MODINC_POW2(pi->nphy_noise_index, PHY_NOISE_WINDOW_SZ);

	return noise_dbm;

}

void wlc_phy_noise_sample_intr(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	u16 jssi_aux;
	u8 channel = 0;
	s8 noise_dbm = PHY_NOISE_FIXED_VAL_NPHY;

	if (ISLCNPHY(pi)) {
		u32 cmplx_pwr, cmplx_pwr0, cmplx_pwr1;
		u16 lo, hi;
		s32 pwr_offset_dB, gain_dB;
		u16 status_0, status_1;

		jssi_aux = wlapi_bmac_read_shm(pi->sh->physhim, M_JSSI_AUX);
		channel = jssi_aux & D11_CURCHANNEL_MAX;

		lo = wlapi_bmac_read_shm(pi->sh->physhim, M_PWRIND_MAP0);
		hi = wlapi_bmac_read_shm(pi->sh->physhim, M_PWRIND_MAP1);
		cmplx_pwr0 = (hi << 16) + lo;

		lo = wlapi_bmac_read_shm(pi->sh->physhim, M_PWRIND_MAP2);
		hi = wlapi_bmac_read_shm(pi->sh->physhim, M_PWRIND_MAP3);
		cmplx_pwr1 = (hi << 16) + lo;
		cmplx_pwr = (cmplx_pwr0 + cmplx_pwr1) >> 6;

		status_0 = 0x44;
		status_1 = wlapi_bmac_read_shm(pi->sh->physhim, M_JSSI_0);
		if ((cmplx_pwr > 0 && cmplx_pwr < 500)
		    && ((status_1 & 0xc000) == 0x4000)) {

			wlc_phy_compute_dB(&cmplx_pwr, &noise_dbm,
					   pi->pubpi.phy_corenum);
			pwr_offset_dB = (read_phy_reg(pi, 0x434) & 0xFF);
			if (pwr_offset_dB > 127)
				pwr_offset_dB -= 256;

			noise_dbm += (s8) (pwr_offset_dB - 30);

			gain_dB = (status_0 & 0x1ff);
			noise_dbm -= (s8) (gain_dB);
		} else {
			noise_dbm = PHY_NOISE_FIXED_VAL_LCNPHY;
		}
	} else if (ISNPHY(pi)) {

		jssi_aux = wlapi_bmac_read_shm(pi->sh->physhim, M_JSSI_AUX);
		channel = jssi_aux & D11_CURCHANNEL_MAX;

		noise_dbm = wlc_phy_noise_read_shmem(pi);
	}

	wlc_phy_noise_cb(pi, channel, noise_dbm);

}

static void
wlc_phy_noise_sample_request(struct brcms_phy_pub *pih, u8 reason, u8 ch)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	s8 noise_dbm = PHY_NOISE_FIXED_VAL_NPHY;
	bool sampling_in_progress = (pi->phynoise_state != 0);
	bool wait_for_intr = true;

	switch (reason) {
	case PHY_NOISE_SAMPLE_MON:
		pi->phynoise_chan_watchdog = ch;
		pi->phynoise_state |= PHY_NOISE_STATE_MON;
		break;

	case PHY_NOISE_SAMPLE_EXTERNAL:
		pi->phynoise_state |= PHY_NOISE_STATE_EXTERNAL;
		break;

	default:
		break;
	}

	if (sampling_in_progress)
		return;

	pi->phynoise_now = pi->sh->now;

	if (pi->phy_fixed_noise) {
		if (ISNPHY(pi)) {
			pi->nphy_noise_win[WL_ANT_IDX_1][pi->nphy_noise_index] =
				PHY_NOISE_FIXED_VAL_NPHY;
			pi->nphy_noise_win[WL_ANT_IDX_2][pi->nphy_noise_index] =
				PHY_NOISE_FIXED_VAL_NPHY;
			pi->nphy_noise_index = MODINC_POW2(pi->nphy_noise_index,
							   PHY_NOISE_WINDOW_SZ);
			noise_dbm = PHY_NOISE_FIXED_VAL_NPHY;
		} else {
			noise_dbm = PHY_NOISE_FIXED_VAL;
		}

		wait_for_intr = false;
		goto done;
	}

	if (ISLCNPHY(pi)) {
		if (!pi->phynoise_polling
		    || (reason == PHY_NOISE_SAMPLE_EXTERNAL)) {
			wlapi_bmac_write_shm(pi->sh->physhim, M_JSSI_0, 0);
			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP0, 0);
			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP1, 0);
			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP2, 0);
			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP3, 0);

			bcma_set32(pi->d11core, D11REGOFFS(maccommand),
				   MCMD_BG_NOISE);
		} else {
			wlapi_suspend_mac_and_wait(pi->sh->physhim);
			wlc_lcnphy_deaf_mode(pi, (bool) 0);
			noise_dbm = (s8) wlc_lcnphy_rx_signal_power(pi, 20);
			wlc_lcnphy_deaf_mode(pi, (bool) 1);
			wlapi_enable_mac(pi->sh->physhim);
			wait_for_intr = false;
		}
	} else if (ISNPHY(pi)) {
		if (!pi->phynoise_polling
		    || (reason == PHY_NOISE_SAMPLE_EXTERNAL)) {

			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP0, 0);
			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP1, 0);
			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP2, 0);
			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP3, 0);

			bcma_set32(pi->d11core, D11REGOFFS(maccommand),
				   MCMD_BG_NOISE);
		} else {
			struct phy_iq_est est[PHY_CORE_MAX];
			u32 cmplx_pwr[PHY_CORE_MAX];
			s8 noise_dbm_ant[PHY_CORE_MAX];
			u16 log_num_samps, num_samps, classif_state = 0;
			u8 wait_time = 32;
			u8 wait_crs = 0;
			u8 i;

			memset((u8 *) est, 0, sizeof(est));
			memset((u8 *) cmplx_pwr, 0, sizeof(cmplx_pwr));
			memset((u8 *) noise_dbm_ant, 0, sizeof(noise_dbm_ant));

			log_num_samps = PHY_NOISE_SAMPLE_LOG_NUM_NPHY;
			num_samps = 1 << log_num_samps;

			wlapi_suspend_mac_and_wait(pi->sh->physhim);
			classif_state = wlc_phy_classifier_nphy(pi, 0, 0);
			wlc_phy_classifier_nphy(pi, 3, 0);
			wlc_phy_rx_iq_est_nphy(pi, est, num_samps, wait_time,
					       wait_crs);
			wlc_phy_classifier_nphy(pi, (0x7 << 0), classif_state);
			wlapi_enable_mac(pi->sh->physhim);

			for (i = 0; i < pi->pubpi.phy_corenum; i++)
				cmplx_pwr[i] = (est[i].i_pwr + est[i].q_pwr) >>
					       log_num_samps;

			wlc_phy_noise_calc_phy(pi, cmplx_pwr, noise_dbm_ant);

			for (i = 0; i < pi->pubpi.phy_corenum; i++) {
				pi->nphy_noise_win[i][pi->nphy_noise_index] =
					noise_dbm_ant[i];

				if (noise_dbm_ant[i] > noise_dbm)
					noise_dbm = noise_dbm_ant[i];
			}
			pi->nphy_noise_index = MODINC_POW2(pi->nphy_noise_index,
							   PHY_NOISE_WINDOW_SZ);

			wait_for_intr = false;
		}
	}

done:

	if (!wait_for_intr)
		wlc_phy_noise_cb(pi, ch, noise_dbm);

}

void wlc_phy_noise_sample_request_external(struct brcms_phy_pub *pih)
{
	u8 channel;

	channel = CHSPEC_CHANNEL(wlc_phy_chanspec_get(pih));

	wlc_phy_noise_sample_request(pih, PHY_NOISE_SAMPLE_EXTERNAL, channel);
}

static const s8 lcnphy_gain_index_offset_for_pkt_rssi[] = {
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	9,
	10,
	8,
	8,
	7,
	7,
	1,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	1,
	1,
	0,
	0,
	0,
	0
};

void wlc_phy_compute_dB(u32 *cmplx_pwr, s8 *p_cmplx_pwr_dB, u8 core)
{
	u8 msb, secondmsb, i;
	u32 tmp;

	for (i = 0; i < core; i++) {
		secondmsb = 0;
		tmp = cmplx_pwr[i];
		msb = fls(tmp);
		if (msb)
			secondmsb = (u8) ((tmp >> (--msb - 1)) & 1);
		p_cmplx_pwr_dB[i] = (s8) (3 * msb + 2 * secondmsb);
	}
}

int wlc_phy_rssi_compute(struct brcms_phy_pub *pih,
			 struct d11rxhdr *rxh)
{
	int rssi = rxh->PhyRxStatus_1 & PRXS1_JSSI_MASK;
	uint radioid = pih->radioid;
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	if ((pi->sh->corerev >= 11)
	    && !(rxh->RxStatus2 & RXS_PHYRXST_VALID)) {
		rssi = BRCMS_RSSI_INVALID;
		goto end;
	}

	if (ISLCNPHY(pi)) {
		u8 gidx = (rxh->PhyRxStatus_2 & 0xFC00) >> 10;
		struct brcms_phy_lcnphy *pi_lcn = pi->u.pi_lcnphy;

		if (rssi > 127)
			rssi -= 256;

		rssi = rssi + lcnphy_gain_index_offset_for_pkt_rssi[gidx];
		if ((rssi > -46) && (gidx > 18))
			rssi = rssi + 7;

		rssi = rssi + pi_lcn->lcnphy_pkteng_rssi_slope;

		rssi = rssi + 2;

	}

	if (ISLCNPHY(pi)) {
		if (rssi > 127)
			rssi -= 256;
	} else if (radioid == BCM2055_ID || radioid == BCM2056_ID
		   || radioid == BCM2057_ID) {
		rssi = wlc_phy_rssi_compute_nphy(pi, rxh);
	}

end:
	return rssi;
}

void wlc_phy_freqtrack_start(struct brcms_phy_pub *pih)
{
	return;
}

void wlc_phy_freqtrack_end(struct brcms_phy_pub *pih)
{
	return;
}

void wlc_phy_set_deaf(struct brcms_phy_pub *ppi, bool user_flag)
{
	struct brcms_phy *pi;
	pi = (struct brcms_phy *) ppi;

	if (ISLCNPHY(pi))
		wlc_lcnphy_deaf_mode(pi, true);
	else if (ISNPHY(pi))
		wlc_nphy_deaf_mode(pi, true);
}

void wlc_phy_watchdog(struct brcms_phy_pub *pih)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	bool delay_phy_cal = false;
	pi->sh->now++;

	if (!pi->watchdog_override)
		return;

	if (!(SCAN_RM_IN_PROGRESS(pi) || PLT_INPROG_PHY(pi)))
		wlc_phy_noise_sample_request((struct brcms_phy_pub *) pi,
					     PHY_NOISE_SAMPLE_MON,
					     CHSPEC_CHANNEL(pi->
							    radio_chanspec));

	if (pi->phynoise_state && (pi->sh->now - pi->phynoise_now) > 5)
		pi->phynoise_state = 0;

	if ((!pi->phycal_txpower) ||
	    ((pi->sh->now - pi->phycal_txpower) >= pi->sh->fast_timer)) {

		if (!SCAN_INPROG_PHY(pi) && wlc_phy_cal_txpower_recalc_sw(pi))
			pi->phycal_txpower = pi->sh->now;
	}

	if ((SCAN_RM_IN_PROGRESS(pi) || PLT_INPROG_PHY(pi)
	     || ASSOC_INPROG_PHY(pi)))
		return;

	if (ISNPHY(pi) && !pi->disable_percal && !delay_phy_cal) {

		if ((pi->nphy_perical != PHY_PERICAL_DISABLE) &&
		    (pi->nphy_perical != PHY_PERICAL_MANUAL) &&
		    ((pi->sh->now - pi->nphy_perical_last) >=
		     pi->sh->glacial_timer))
			wlc_phy_cal_perical((struct brcms_phy_pub *) pi,
					    PHY_PERICAL_WATCHDOG);

		wlc_phy_txpwr_papd_cal_nphy(pi);
	}

	if (ISLCNPHY(pi)) {
		if (pi->phy_forcecal ||
		    ((pi->sh->now - pi->phy_lastcal) >=
		     pi->sh->glacial_timer)) {
			if (!(SCAN_RM_IN_PROGRESS(pi) || ASSOC_INPROG_PHY(pi)))
				wlc_lcnphy_calib_modes(
					pi,
					LCNPHY_PERICAL_TEMPBASED_TXPWRCTRL);
			if (!
			    (SCAN_RM_IN_PROGRESS(pi) || PLT_INPROG_PHY(pi)
			     || ASSOC_INPROG_PHY(pi)
			     || pi->carrier_suppr_disable
			     || pi->disable_percal))
				wlc_lcnphy_calib_modes(pi,
						       PHY_PERICAL_WATCHDOG);
		}
	}
}

void wlc_phy_BSSinit(struct brcms_phy_pub *pih, bool bonlyap, int rssi)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	uint i;
	uint k;

	for (i = 0; i < MA_WINDOW_SZ; i++)
		pi->sh->phy_noise_window[i] = (s8) (rssi & 0xff);
	if (ISLCNPHY(pi)) {
		for (i = 0; i < MA_WINDOW_SZ; i++)
			pi->sh->phy_noise_window[i] =
				PHY_NOISE_FIXED_VAL_LCNPHY;
	}
	pi->sh->phy_noise_index = 0;

	for (i = 0; i < PHY_NOISE_WINDOW_SZ; i++) {
		for (k = WL_ANT_IDX_1; k < WL_ANT_RX_MAX; k++)
			pi->nphy_noise_win[k][i] = PHY_NOISE_FIXED_VAL_NPHY;
	}
	pi->nphy_noise_index = 0;
}

void
wlc_phy_papd_decode_epsilon(u32 epsilon, s32 *eps_real, s32 *eps_imag)
{
	*eps_imag = (epsilon >> 13);
	if (*eps_imag > 0xfff)
		*eps_imag -= 0x2000;

	*eps_real = (epsilon & 0x1fff);
	if (*eps_real > 0xfff)
		*eps_real -= 0x2000;
}

void wlc_phy_cal_perical_mphase_reset(struct brcms_phy *pi)
{
	wlapi_del_timer(pi->phycal_timer);

	pi->cal_type_override = PHY_PERICAL_AUTO;
	pi->mphase_cal_phase_id = MPHASE_CAL_STATE_IDLE;
	pi->mphase_txcal_cmdidx = 0;
}

static void
wlc_phy_cal_perical_mphase_schedule(struct brcms_phy *pi, uint delay)
{

	if ((pi->nphy_perical != PHY_PERICAL_MPHASE) &&
	    (pi->nphy_perical != PHY_PERICAL_MANUAL))
		return;

	wlapi_del_timer(pi->phycal_timer);

	pi->mphase_cal_phase_id = MPHASE_CAL_STATE_INIT;
	wlapi_add_timer(pi->phycal_timer, delay, 0);
}

void wlc_phy_cal_perical(struct brcms_phy_pub *pih, u8 reason)
{
	s16 nphy_currtemp = 0;
	s16 delta_temp = 0;
	bool do_periodic_cal = true;
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	if (!ISNPHY(pi))
		return;

	if ((pi->nphy_perical == PHY_PERICAL_DISABLE) ||
	    (pi->nphy_perical == PHY_PERICAL_MANUAL))
		return;

	switch (reason) {
	case PHY_PERICAL_DRIVERUP:
		break;

	case PHY_PERICAL_PHYINIT:
		if (pi->nphy_perical == PHY_PERICAL_MPHASE) {
			if (PHY_PERICAL_MPHASE_PENDING(pi))
				wlc_phy_cal_perical_mphase_reset(pi);

			wlc_phy_cal_perical_mphase_schedule(
				pi,
				PHY_PERICAL_INIT_DELAY);
		}
		break;

	case PHY_PERICAL_JOIN_BSS:
	case PHY_PERICAL_START_IBSS:
	case PHY_PERICAL_UP_BSS:
		if ((pi->nphy_perical == PHY_PERICAL_MPHASE) &&
		    PHY_PERICAL_MPHASE_PENDING(pi))
			wlc_phy_cal_perical_mphase_reset(pi);

		pi->first_cal_after_assoc = true;

		pi->cal_type_override = PHY_PERICAL_FULL;

		if (pi->phycal_tempdelta)
			pi->nphy_lastcal_temp = wlc_phy_tempsense_nphy(pi);

		wlc_phy_cal_perical_nphy_run(pi, PHY_PERICAL_FULL);
		break;

	case PHY_PERICAL_WATCHDOG:
		if (pi->phycal_tempdelta) {
			nphy_currtemp = wlc_phy_tempsense_nphy(pi);
			delta_temp =
				(nphy_currtemp > pi->nphy_lastcal_temp) ?
				nphy_currtemp - pi->nphy_lastcal_temp :
				pi->nphy_lastcal_temp - nphy_currtemp;

			if ((delta_temp < (s16) pi->phycal_tempdelta) &&
			    (pi->nphy_txiqlocal_chanspec ==
			     pi->radio_chanspec))
				do_periodic_cal = false;
			else
				pi->nphy_lastcal_temp = nphy_currtemp;
		}

		if (do_periodic_cal) {
			if (pi->nphy_perical == PHY_PERICAL_MPHASE) {
				if (!PHY_PERICAL_MPHASE_PENDING(pi))
					wlc_phy_cal_perical_mphase_schedule(
						pi,
						PHY_PERICAL_WDOG_DELAY);
			} else if (pi->nphy_perical == PHY_PERICAL_SPHASE)
				wlc_phy_cal_perical_nphy_run(pi,
							     PHY_PERICAL_AUTO);
		}
		break;
	default:
		break;
	}
}

void wlc_phy_cal_perical_mphase_restart(struct brcms_phy *pi)
{
	pi->mphase_cal_phase_id = MPHASE_CAL_STATE_INIT;
	pi->mphase_txcal_cmdidx = 0;
}

u8 wlc_phy_nbits(s32 value)
{
	s32 abs_val;
	u8 nbits = 0;

	abs_val = abs(value);
	while ((abs_val >> nbits) > 0)
		nbits++;

	return nbits;
}

void wlc_phy_stf_chain_init(struct brcms_phy_pub *pih, u8 txchain, u8 rxchain)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	pi->sh->hw_phytxchain = txchain;
	pi->sh->hw_phyrxchain = rxchain;
	pi->sh->phytxchain = txchain;
	pi->sh->phyrxchain = rxchain;
	pi->pubpi.phy_corenum = (u8)hweight8(pi->sh->phyrxchain);
}

void wlc_phy_stf_chain_set(struct brcms_phy_pub *pih, u8 txchain, u8 rxchain)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	pi->sh->phytxchain = txchain;

	if (ISNPHY(pi))
		wlc_phy_rxcore_setstate_nphy(pih, rxchain);

	pi->pubpi.phy_corenum = (u8)hweight8(pi->sh->phyrxchain);
}

void wlc_phy_stf_chain_get(struct brcms_phy_pub *pih, u8 *txchain, u8 *rxchain)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	*txchain = pi->sh->phytxchain;
	*rxchain = pi->sh->phyrxchain;
}

u8 wlc_phy_stf_chain_active_get(struct brcms_phy_pub *pih)
{
	s16 nphy_currtemp;
	u8 active_bitmap;
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);

	active_bitmap = (pi->phy_txcore_heatedup) ? 0x31 : 0x33;

	if (!pi->watchdog_override)
		return active_bitmap;

	if (NREV_GE(pi->pubpi.phy_rev, 6)) {
		wlapi_suspend_mac_and_wait(pi->sh->physhim);
		nphy_currtemp = wlc_phy_tempsense_nphy(pi);
		wlapi_enable_mac(pi->sh->physhim);

		if (!pi->phy_txcore_heatedup) {
			if (nphy_currtemp >= pi->phy_txcore_disable_temp) {
				active_bitmap &= 0xFD;
				pi->phy_txcore_heatedup = true;
			}
		} else {
			if (nphy_currtemp <= pi->phy_txcore_enable_temp) {
				active_bitmap |= 0x2;
				pi->phy_txcore_heatedup = false;
			}
		}
	}

	return active_bitmap;
}

s8 wlc_phy_stf_ssmode_get(struct brcms_phy_pub *pih, u16 chanspec)
{
	struct brcms_phy *pi = container_of(pih, struct brcms_phy, pubpi_ro);
	u8 siso_mcs_id, cdd_mcs_id;

	siso_mcs_id =
		(CHSPEC_IS40(chanspec)) ? TXP_FIRST_MCS_40_SISO :
		TXP_FIRST_MCS_20_SISO;
	cdd_mcs_id =
		(CHSPEC_IS40(chanspec)) ? TXP_FIRST_MCS_40_CDD :
		TXP_FIRST_MCS_20_CDD;

	if (pi->tx_power_target[siso_mcs_id] >
	    (pi->tx_power_target[cdd_mcs_id] + 12))
		return PHY_TXC1_MODE_SISO;
	else
		return PHY_TXC1_MODE_CDD;
}

const u8 *wlc_phy_get_ofdm_rate_lookup(void)
{
	return ofdm_rate_lookup;
}

void wlc_lcnphy_epa_switch(struct brcms_phy *pi, bool mode)
{
	if ((pi->sh->chip == BCMA_CHIP_ID_BCM4313) &&
	    (pi->sh->boardflags & BFL_FEM)) {
		if (mode) {
			u16 txant = 0;
			txant = wlapi_bmac_get_txant(pi->sh->physhim);
			if (txant == 1) {
				mod_phy_reg(pi, 0x44d, (0x1 << 2), (1) << 2);

				mod_phy_reg(pi, 0x44c, (0x1 << 2), (1) << 2);

			}

			bcma_chipco_gpio_control(&pi->d11core->bus->drv_cc,
						 0x0, 0x0);
			bcma_chipco_gpio_out(&pi->d11core->bus->drv_cc,
					     ~0x40, 0x40);
			bcma_chipco_gpio_outen(&pi->d11core->bus->drv_cc,
					       ~0x40, 0x40);
		} else {
			mod_phy_reg(pi, 0x44c, (0x1 << 2), (0) << 2);

			mod_phy_reg(pi, 0x44d, (0x1 << 2), (0) << 2);

			bcma_chipco_gpio_out(&pi->d11core->bus->drv_cc,
					     ~0x40, 0x00);
			bcma_chipco_gpio_outen(&pi->d11core->bus->drv_cc,
					       ~0x40, 0x00);
			bcma_chipco_gpio_control(&pi->d11core->bus->drv_cc,
						 0x0, 0x40);
		}
	}
}

void wlc_phy_ldpc_override_set(struct brcms_phy_pub *ppi, bool ldpc)
{
	return;
}

void
wlc_phy_get_pwrdet_offsets(struct brcms_phy *pi, s8 *cckoffset, s8 *ofdmoffset)
{
	*cckoffset = 0;
	*ofdmoffset = 0;
}

s8 wlc_phy_upd_rssi_offset(struct brcms_phy *pi, s8 rssi, u16 chanspec)
{

	return rssi;
}

bool wlc_phy_txpower_ipa_ison(struct brcms_phy_pub *ppi)
{
	struct brcms_phy *pi = container_of(ppi, struct brcms_phy, pubpi_ro);

	if (ISNPHY(pi))
		return wlc_phy_n_txpower_ipa_ison(pi);
	else
		return false;
}
