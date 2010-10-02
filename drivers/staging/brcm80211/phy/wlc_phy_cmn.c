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

#include <wlc_cfg.h>

#include <typedefs.h>
#include <osl.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linuxver.h>
#include <bcmendian.h>
#include <bcmnvram.h>
#include <sbchipc.h>

#include <wlc_phy_int.h>
#include <wlc_phyreg_n.h>
#include <wlc_phy_radio.h>
#include <wlc_phy_lcn.h>

uint32 phyhal_msg_level = PHYHAL_ERROR;

typedef struct _chan_info_basic {
	uint16 chan;
	uint16 freq;
} chan_info_basic_t;

static chan_info_basic_t chan_info_all[] = {

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
	{216, 50800}
};

uint16 ltrn_list[PHY_LTRN_LIST_LEN] = {
	0x18f9, 0x0d01, 0x00e4, 0xdef4, 0x06f1, 0x0ffc,
	0xfa27, 0x1dff, 0x10f0, 0x0918, 0xf20a, 0xe010,
	0x1417, 0x1104, 0xf114, 0xf2fa, 0xf7db, 0xe2fc,
	0xe1fb, 0x13ee, 0xff0d, 0xe91c, 0x171a, 0x0318,
	0xda00, 0x03e8, 0x17e6, 0xe9e4, 0xfff3, 0x1312,
	0xe105, 0xe204, 0xf725, 0xf206, 0xf1ec, 0x11fc,
	0x14e9, 0xe0f0, 0xf2f6, 0x09e8, 0x1010, 0x1d01,
	0xfad9, 0x0f04, 0x060f, 0xde0c, 0x001c, 0x0dff,
	0x1807, 0xf61a, 0xe40e, 0x0f16, 0x05f9, 0x18ec,
	0x0a1b, 0xff1e, 0x2600, 0xffe2, 0x0ae5, 0x1814,
	0x0507, 0x0fea, 0xe4f2, 0xf6e6
};

const uint8 ofdm_rate_lookup[] = {

	WLC_RATE_48M,
	WLC_RATE_24M,
	WLC_RATE_12M,
	WLC_RATE_6M,
	WLC_RATE_54M,
	WLC_RATE_36M,
	WLC_RATE_18M,
	WLC_RATE_9M
};

#define PHY_WREG_LIMIT	24

static void wlc_set_phy_uninitted(phy_info_t *pi);
static uint32 wlc_phy_get_radio_ver(phy_info_t *pi);
static void wlc_phy_timercb_phycal(void *arg);

static bool wlc_phy_noise_calc_phy(phy_info_t *pi, uint32 *cmplx_pwr,
				   int8 *pwr_ant);

static void wlc_phy_cal_perical_mphase_schedule(phy_info_t *pi, uint delay);
static void wlc_phy_noise_cb(phy_info_t *pi, uint8 channel, int8 noise_dbm);
static void wlc_phy_noise_sample_request(wlc_phy_t *pih, uint8 reason,
					 uint8 ch);

static void wlc_phy_txpower_reg_limit_calc(phy_info_t *pi,
					   struct txpwr_limits *tp, chanspec_t);
static bool wlc_phy_cal_txpower_recalc_sw(phy_info_t *pi);

static int8 wlc_user_txpwr_antport_to_rfport(phy_info_t *pi, uint chan,
					     uint32 band, uint8 rate);
static void wlc_phy_upd_env_txpwr_rate_limits(phy_info_t *pi, uint32 band);
static int8 wlc_phy_env_measure_vbat(phy_info_t *pi);
static int8 wlc_phy_env_measure_temperature(phy_info_t *pi);

char *phy_getvar(phy_info_t *pi, const char *name)
{
	char *vars = pi->vars;
	char *s;
	int len;

	ASSERT(pi->vars != (char *)&pi->vars);

	if (!name)
		return NULL;

	len = strlen(name);
	if (len == 0)
		return NULL;

	for (s = vars; s && *s;) {
		if ((bcmp(s, name, len) == 0) && (s[len] == '='))
			return &s[len + 1];

		while (*s++)
			;
	}

	return nvram_get(name);
}

int phy_getintvar(phy_info_t *pi, const char *name)
{
	char *val;

	val = PHY_GETVAR(pi, name);
	if (val == NULL)
		return 0;

	return simple_strtoul(val, NULL, 0);
}

void wlc_phyreg_enter(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;
	wlapi_bmac_ucode_wake_override_phyreg_set(pi->sh->physhim);
}

void wlc_phyreg_exit(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;
	wlapi_bmac_ucode_wake_override_phyreg_clear(pi->sh->physhim);
}

void wlc_radioreg_enter(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;
	wlapi_bmac_mctrl(pi->sh->physhim, MCTL_LOCK_RADIO, MCTL_LOCK_RADIO);

	OSL_DELAY(10);
}

void wlc_radioreg_exit(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;
	volatile uint16 dummy;

	dummy = R_REG(pi->sh->osh, &pi->regs->phyversion);
	pi->phy_wreg = 0;
	wlapi_bmac_mctrl(pi->sh->physhim, MCTL_LOCK_RADIO, 0);
}

uint16 read_radio_reg(phy_info_t *pi, uint16 addr)
{
	uint16 data;

	if ((addr == RADIO_IDCODE))
		return 0xffff;

	if (NORADIO_ENAB(pi->pubpi))
		return NORADIO_IDCODE & 0xffff;

	switch (pi->pubpi.phy_type) {
	case PHY_TYPE_N:
		CASECHECK(PHYTYPE, PHY_TYPE_N);
		if (NREV_GE(pi->pubpi.phy_rev, 7))
			addr |= RADIO_2057_READ_OFF;
		else
			addr |= RADIO_2055_READ_OFF;
		break;

	case PHY_TYPE_LCN:
		CASECHECK(PHYTYPE, PHY_TYPE_LCN);
		addr |= RADIO_2064_READ_OFF;
		break;

	default:
		ASSERT(VALID_PHYTYPE(pi->pubpi.phy_type));
	}

	if ((D11REV_GE(pi->sh->corerev, 24)) ||
	    (D11REV_IS(pi->sh->corerev, 22)
	     && (pi->pubpi.phy_type != PHY_TYPE_SSN))) {
		W_REG(pi->sh->osh, &pi->regs->radioregaddr, addr);
#ifdef __mips__
		(void)R_REG(pi->sh->osh, &pi->regs->radioregaddr);
#endif
		data = R_REG(pi->sh->osh, &pi->regs->radioregdata);
	} else {
		W_REG(pi->sh->osh, &pi->regs->phy4waddr, addr);
#ifdef __mips__
		(void)R_REG(pi->sh->osh, &pi->regs->phy4waddr);
#endif

#ifdef __ARM_ARCH_4T__
		__asm__(" .align 4 ");
		__asm__(" nop ");
		data = R_REG(pi->sh->osh, &pi->regs->phy4wdatalo);
#else
		data = R_REG(pi->sh->osh, &pi->regs->phy4wdatalo);
#endif

	}
	pi->phy_wreg = 0;

	return data;
}

void write_radio_reg(phy_info_t *pi, uint16 addr, uint16 val)
{
	osl_t *osh;

	if (NORADIO_ENAB(pi->pubpi))
		return;

	osh = pi->sh->osh;

	if ((D11REV_GE(pi->sh->corerev, 24)) ||
	    (D11REV_IS(pi->sh->corerev, 22)
	     && (pi->pubpi.phy_type != PHY_TYPE_SSN))) {

		W_REG(osh, &pi->regs->radioregaddr, addr);
#ifdef __mips__
		(void)R_REG(osh, &pi->regs->radioregaddr);
#endif
		W_REG(osh, &pi->regs->radioregdata, val);
	} else {
		W_REG(osh, &pi->regs->phy4waddr, addr);
#ifdef __mips__
		(void)R_REG(osh, &pi->regs->phy4waddr);
#endif
		W_REG(osh, &pi->regs->phy4wdatalo, val);
	}

	if (BUSTYPE(pi->sh->bustype) == PCI_BUS) {
		if (++pi->phy_wreg >= pi->phy_wreg_limit) {
			(void)R_REG(osh, &pi->regs->maccontrol);
			pi->phy_wreg = 0;
		}
	}
}

static uint32 read_radio_id(phy_info_t *pi)
{
	uint32 id;

	if (NORADIO_ENAB(pi->pubpi))
		return NORADIO_IDCODE;

	if (D11REV_GE(pi->sh->corerev, 24)) {
		uint32 b0, b1, b2;

		W_REG(pi->sh->osh, &pi->regs->radioregaddr, 0);
#ifdef __mips__
		(void)R_REG(pi->sh->osh, &pi->regs->radioregaddr);
#endif
		b0 = (uint32) R_REG(pi->sh->osh, &pi->regs->radioregdata);
		W_REG(pi->sh->osh, &pi->regs->radioregaddr, 1);
#ifdef __mips__
		(void)R_REG(pi->sh->osh, &pi->regs->radioregaddr);
#endif
		b1 = (uint32) R_REG(pi->sh->osh, &pi->regs->radioregdata);
		W_REG(pi->sh->osh, &pi->regs->radioregaddr, 2);
#ifdef __mips__
		(void)R_REG(pi->sh->osh, &pi->regs->radioregaddr);
#endif
		b2 = (uint32) R_REG(pi->sh->osh, &pi->regs->radioregdata);

		id = ((b0 & 0xf) << 28) | (((b2 << 8) | b1) << 12) | ((b0 >> 4)
								      & 0xf);
	} else {
		W_REG(pi->sh->osh, &pi->regs->phy4waddr, RADIO_IDCODE);
#ifdef __mips__
		(void)R_REG(pi->sh->osh, &pi->regs->phy4waddr);
#endif
		id = (uint32) R_REG(pi->sh->osh, &pi->regs->phy4wdatalo);
		id |= (uint32) R_REG(pi->sh->osh, &pi->regs->phy4wdatahi) << 16;
	}
	pi->phy_wreg = 0;
	return id;
}

void and_radio_reg(phy_info_t *pi, uint16 addr, uint16 val)
{
	uint16 rval;

	if (NORADIO_ENAB(pi->pubpi))
		return;

	rval = read_radio_reg(pi, addr);
	write_radio_reg(pi, addr, (rval & val));
}

void or_radio_reg(phy_info_t *pi, uint16 addr, uint16 val)
{
	uint16 rval;

	if (NORADIO_ENAB(pi->pubpi))
		return;

	rval = read_radio_reg(pi, addr);
	write_radio_reg(pi, addr, (rval | val));
}

void xor_radio_reg(phy_info_t *pi, uint16 addr, uint16 mask)
{
	uint16 rval;

	if (NORADIO_ENAB(pi->pubpi))
		return;

	rval = read_radio_reg(pi, addr);
	write_radio_reg(pi, addr, (rval ^ mask));
}

void mod_radio_reg(phy_info_t *pi, uint16 addr, uint16 mask, uint16 val)
{
	uint16 rval;

	if (NORADIO_ENAB(pi->pubpi))
		return;

	rval = read_radio_reg(pi, addr);
	write_radio_reg(pi, addr, (rval & ~mask) | (val & mask));
}

void write_phy_channel_reg(phy_info_t *pi, uint val)
{
	W_REG(pi->sh->osh, &pi->regs->phychannel, val);
}

#if defined(BCMDBG)
static bool wlc_phy_war41476(phy_info_t *pi)
{
	uint32 mc = R_REG(pi->sh->osh, &pi->regs->maccontrol);

	return ((mc & MCTL_EN_MAC) == 0)
	    || ((mc & MCTL_PHYLOCK) == MCTL_PHYLOCK);
}
#endif

uint16 read_phy_reg(phy_info_t *pi, uint16 addr)
{
	osl_t *osh;
	d11regs_t *regs;

	osh = pi->sh->osh;
	regs = pi->regs;

	W_REG(osh, &regs->phyregaddr, addr);
#ifdef __mips__
	(void)R_REG(osh, &regs->phyregaddr);
#endif

	ASSERT(!
	       (D11REV_IS(pi->sh->corerev, 11)
		|| D11REV_IS(pi->sh->corerev, 12)) || wlc_phy_war41476(pi));

	pi->phy_wreg = 0;
	return R_REG(osh, &regs->phyregdata);
}

void write_phy_reg(phy_info_t *pi, uint16 addr, uint16 val)
{
	osl_t *osh;
	d11regs_t *regs;

	osh = pi->sh->osh;
	regs = pi->regs;

#ifdef __mips__
	W_REG(osh, &regs->phyregaddr, addr);
	(void)R_REG(osh, &regs->phyregaddr);
	W_REG(osh, &regs->phyregdata, val);
	if (addr == 0x72)
		(void)R_REG(osh, &regs->phyregdata);
#else
	W_REG(osh, (volatile uint32 *)(uintptr) (&regs->phyregaddr),
	      addr | (val << 16));
	if (BUSTYPE(pi->sh->bustype) == PCI_BUS) {
		if (++pi->phy_wreg >= pi->phy_wreg_limit) {
			pi->phy_wreg = 0;
			(void)R_REG(osh, &regs->phyversion);
		}
	}
#endif
}

void and_phy_reg(phy_info_t *pi, uint16 addr, uint16 val)
{
	osl_t *osh;
	d11regs_t *regs;

	osh = pi->sh->osh;
	regs = pi->regs;

	W_REG(osh, &regs->phyregaddr, addr);
#ifdef __mips__
	(void)R_REG(osh, &regs->phyregaddr);
#endif

	ASSERT(!
	       (D11REV_IS(pi->sh->corerev, 11)
		|| D11REV_IS(pi->sh->corerev, 12)) || wlc_phy_war41476(pi));

	W_REG(osh, &regs->phyregdata, (R_REG(osh, &regs->phyregdata) & val));
	pi->phy_wreg = 0;
}

void or_phy_reg(phy_info_t *pi, uint16 addr, uint16 val)
{
	osl_t *osh;
	d11regs_t *regs;

	osh = pi->sh->osh;
	regs = pi->regs;

	W_REG(osh, &regs->phyregaddr, addr);
#ifdef __mips__
	(void)R_REG(osh, &regs->phyregaddr);
#endif

	ASSERT(!
	       (D11REV_IS(pi->sh->corerev, 11)
		|| D11REV_IS(pi->sh->corerev, 12)) || wlc_phy_war41476(pi));

	W_REG(osh, &regs->phyregdata, (R_REG(osh, &regs->phyregdata) | val));
	pi->phy_wreg = 0;
}

void mod_phy_reg(phy_info_t *pi, uint16 addr, uint16 mask, uint16 val)
{
	osl_t *osh;
	d11regs_t *regs;

	osh = pi->sh->osh;
	regs = pi->regs;

	W_REG(osh, &regs->phyregaddr, addr);
#ifdef __mips__
	(void)R_REG(osh, &regs->phyregaddr);
#endif

	ASSERT(!
	       (D11REV_IS(pi->sh->corerev, 11)
		|| D11REV_IS(pi->sh->corerev, 12)) || wlc_phy_war41476(pi));

	W_REG(osh, &regs->phyregdata,
	      ((R_REG(osh, &regs->phyregdata) & ~mask) | (val & mask)));
	pi->phy_wreg = 0;
}

static void WLBANDINITFN(wlc_set_phy_uninitted) (phy_info_t *pi)
{
	int i, j;

	pi->initialized = FALSE;

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
		for (j = 0; j < STATIC_NUM_BB; j++) {
			pi->stats_11b_txpower[i][j] = -1;
		}
	}
}

shared_phy_t *BCMATTACHFN(wlc_phy_shared_attach) (shared_phy_params_t *shp)
{
	shared_phy_t *sh;

	sh = (shared_phy_t *) MALLOC(shp->osh, sizeof(shared_phy_t));
	if (sh == NULL) {
		return NULL;
	}
	bzero((char *)sh, sizeof(shared_phy_t));

	sh->osh = shp->osh;
	sh->sih = shp->sih;
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
	sh->boardvendor = shp->boardvendor;
	sh->boardflags = shp->boardflags;
	sh->boardflags2 = shp->boardflags2;
	sh->bustype = shp->bustype;
	sh->buscorerev = shp->buscorerev;

	sh->fast_timer = PHY_SW_TIMER_FAST;
	sh->slow_timer = PHY_SW_TIMER_SLOW;
	sh->glacial_timer = PHY_SW_TIMER_GLACIAL;

	sh->rssi_mode = RSSI_ANT_MERGE_MAX;

	return sh;
}

void BCMATTACHFN(wlc_phy_shared_detach) (shared_phy_t *phy_sh)
{
	osl_t *osh;

	if (phy_sh) {
		osh = phy_sh->osh;

		if (phy_sh->phy_head) {
			ASSERT(!phy_sh->phy_head);
		}
		MFREE(osh, phy_sh, sizeof(shared_phy_t));
	}
}

wlc_phy_t *BCMATTACHFN(wlc_phy_attach) (shared_phy_t *sh, void *regs,
					int bandtype, char *vars) {
	phy_info_t *pi;
	uint32 sflags = 0;
	uint phyversion;
	int i;
	osl_t *osh;

	osh = sh->osh;

	if (D11REV_IS(sh->corerev, 4))
		sflags = SISF_2G_PHY | SISF_5G_PHY;
	else
		sflags = si_core_sflags(sh->sih, 0, 0);

	if (BAND_5G(bandtype)) {
		if ((sflags & (SISF_5G_PHY | SISF_DB_PHY)) == 0) {
			return NULL;
		}
	}

	pi = sh->phy_head;
	if ((sflags & SISF_DB_PHY) && pi) {

		wlapi_bmac_corereset(pi->sh->physhim, pi->pubpi.coreflags);
		pi->refcnt++;
		return &pi->pubpi_ro;
	}

	pi = (phy_info_t *) MALLOC(osh, sizeof(phy_info_t));
	if (pi == NULL) {
		return NULL;
	}
	bzero((char *)pi, sizeof(phy_info_t));
	pi->regs = (d11regs_t *) regs;
	pi->sh = sh;
	pi->phy_init_por = TRUE;
	pi->phy_wreg_limit = PHY_WREG_LIMIT;

	pi->vars = vars;

	pi->txpwr_percent = 100;

	pi->do_initcal = TRUE;

	pi->phycal_tempdelta = 0;

	if (BAND_2G(bandtype) && (sflags & SISF_2G_PHY)) {

		pi->pubpi.coreflags = SICF_GMODE;
	}

	wlapi_bmac_corereset(pi->sh->physhim, pi->pubpi.coreflags);
	phyversion = R_REG(osh, &pi->regs->phyversion);

	pi->pubpi.phy_type = PHY_TYPE(phyversion);
	pi->pubpi.phy_rev = phyversion & PV_PV_MASK;

	if (pi->pubpi.phy_type == PHY_TYPE_LCNXN) {
		pi->pubpi.phy_type = PHY_TYPE_N;
		pi->pubpi.phy_rev += LCNXN_BASEREV;
	}
	pi->pubpi.phy_corenum = PHY_CORE_NUM_2;
	pi->pubpi.ana_rev = (phyversion & PV_AV_MASK) >> PV_AV_SHIFT;

	if (!VALID_PHYTYPE(pi->pubpi.phy_type)) {
		goto err;
	}
	if (BAND_5G(bandtype)) {
		if (!ISNPHY(pi)) {
			goto err;
		}
	} else {
		if (!ISNPHY(pi) && !ISLCNPHY(pi)) {
			goto err;
		}
	}

	if (ISSIM_ENAB(pi->sh->sih)) {
		pi->pubpi.radioid = NORADIO_ID;
		pi->pubpi.radiorev = 5;
	} else {
		uint32 idcode;

		wlc_phy_anacore((wlc_phy_t *) pi, ON);

		idcode = wlc_phy_get_radio_ver(pi);
		pi->pubpi.radioid =
		    (idcode & IDCODE_ID_MASK) >> IDCODE_ID_SHIFT;
		pi->pubpi.radiorev =
		    (idcode & IDCODE_REV_MASK) >> IDCODE_REV_SHIFT;
		pi->pubpi.radiover =
		    (idcode & IDCODE_VER_MASK) >> IDCODE_VER_SHIFT;
		if (!VALID_RADIO(pi, pi->pubpi.radioid)) {
			goto err;
		}

		wlc_phy_switch_radio((wlc_phy_t *) pi, OFF);
	}

	wlc_set_phy_uninitted(pi);

	pi->bw = WL_CHANSPEC_BW_20;
	pi->radio_chanspec =
	    BAND_2G(bandtype) ? CH20MHZ_CHSPEC(1) : CH20MHZ_CHSPEC(36);

	pi->rxiq_samps = PHY_NOISE_SAMPLE_LOG_NUM_NPHY;
	pi->rxiq_antsel = ANT_RX_DIV_DEF;

	pi->watchdog_override = TRUE;

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
	pi->phy_txcore_heatedup = FALSE;

	pi->nphy_lastcal_temp = -50;

	pi->phynoise_polling = TRUE;
	if (ISNPHY(pi) || ISLCNPHY(pi))
		pi->phynoise_polling = FALSE;

	for (i = 0; i < TXP_NUM_RATES; i++) {
		pi->txpwr_limit[i] = WLC_TXPWR_MAX;
		pi->txpwr_env_limit[i] = WLC_TXPWR_MAX;
		pi->tx_user_target[i] = WLC_TXPWR_MAX;
	}

	pi->radiopwr_override = RADIOPWR_OVERRIDE_DEF;

	pi->user_txpwr_at_rfport = FALSE;

	if (ISNPHY(pi)) {

		pi->phycal_timer = wlapi_init_timer(pi->sh->physhim,
							  wlc_phy_timercb_phycal,
							  pi, "phycal");
		if (!pi->phycal_timer) {
			goto err;
		}

		if (!wlc_phy_attach_nphy(pi))
			goto err;

	} else if (ISLCNPHY(pi)) {
		if (!wlc_phy_attach_lcnphy(pi))
			goto err;

	} else {

	}

	pi->refcnt++;
	pi->next = pi->sh->phy_head;
	sh->phy_head = pi;

	pi->vars = (char *)&pi->vars;

	bcopy(&pi->pubpi, &pi->pubpi_ro, sizeof(wlc_phy_t));

	return &pi->pubpi_ro;

 err:
	if (pi)
		MFREE(sh->osh, pi, sizeof(phy_info_t));
	return NULL;
}

void BCMATTACHFN(wlc_phy_detach) (wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;

	if (pih) {
		if (--pi->refcnt) {
			return;
		}

		if (pi->phycal_timer) {
			wlapi_free_timer(pi->sh->physhim, pi->phycal_timer);
			pi->phycal_timer = NULL;
		}

		if (pi->sh->phy_head == pi)
			pi->sh->phy_head = pi->next;
		else if (pi->sh->phy_head->next == pi)
			pi->sh->phy_head->next = NULL;
		else
			ASSERT(0);

		if (pi->pi_fptr.detach)
			(pi->pi_fptr.detach) (pi);

		MFREE(pi->sh->osh, pi, sizeof(phy_info_t));
	}
}

bool
wlc_phy_get_phyversion(wlc_phy_t *pih, uint16 *phytype, uint16 *phyrev,
		       uint16 *radioid, uint16 *radiover)
{
	phy_info_t *pi = (phy_info_t *) pih;
	*phytype = (uint16) pi->pubpi.phy_type;
	*phyrev = (uint16) pi->pubpi.phy_rev;
	*radioid = pi->pubpi.radioid;
	*radiover = pi->pubpi.radiorev;

	return TRUE;
}

bool wlc_phy_get_encore(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;
	return pi->pubpi.abgphy_encore;
}

uint32 wlc_phy_get_coreflags(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;
	return pi->pubpi.coreflags;
}

static void wlc_phy_timercb_phycal(void *arg)
{
	phy_info_t *pi = (phy_info_t *) arg;
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
		wlapi_add_timer(pi->sh->physhim, pi->phycal_timer, delay, 0);
		return;
	}

}

void wlc_phy_anacore(wlc_phy_t *pih, bool on)
{
	phy_info_t *pi = (phy_info_t *) pih;

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

uint32 wlc_phy_clk_bwbits(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;

	uint32 phy_bw_clkbits = 0;

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
			ASSERT(0);
			break;
		}
	}

	return phy_bw_clkbits;
}

void WLBANDINITFN(wlc_phy_por_inform) (wlc_phy_t *ppi)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	pi->phy_init_por = TRUE;
}

void wlc_phy_edcrs_lock(wlc_phy_t *pih, bool lock)
{
	phy_info_t *pi = (phy_info_t *) pih;

	pi->edcrs_threshold_lock = lock;

	write_phy_reg(pi, 0x22c, 0x46b);
	write_phy_reg(pi, 0x22d, 0x46b);
	write_phy_reg(pi, 0x22e, 0x3c0);
	write_phy_reg(pi, 0x22f, 0x3c0);
}

void wlc_phy_initcal_enable(wlc_phy_t *pih, bool initcal)
{
	phy_info_t *pi = (phy_info_t *) pih;

	pi->do_initcal = initcal;
}

void wlc_phy_hw_clk_state_upd(wlc_phy_t *pih, bool newstate)
{
	phy_info_t *pi = (phy_info_t *) pih;

	if (!pi || !pi->sh)
		return;

	pi->sh->clk = newstate;
}

void wlc_phy_hw_state_upd(wlc_phy_t *pih, bool newstate)
{
	phy_info_t *pi = (phy_info_t *) pih;

	if (!pi || !pi->sh)
		return;

	pi->sh->up = newstate;
}

void WLBANDINITFN(wlc_phy_init) (wlc_phy_t *pih, chanspec_t chanspec)
{
	uint32 mc;
	initfn_t phy_init = NULL;
	phy_info_t *pi = (phy_info_t *) pih;

	if (pi->init_in_progress)
		return;

	pi->init_in_progress = TRUE;

	pi->radio_chanspec = chanspec;

	mc = R_REG(pi->sh->osh, &pi->regs->maccontrol);
	if ((mc & MCTL_EN_MAC) != 0) {
		ASSERT((const char *)
		       "wlc_phy_init: Called with the MAC running!" == NULL);
	}

	ASSERT(pi != NULL);

	if (!(pi->measure_hold & PHY_HOLD_FOR_SCAN)) {
		pi->measure_hold |= PHY_HOLD_FOR_NOT_ASSOC;
	}

	if (D11REV_GE(pi->sh->corerev, 5))
		ASSERT(si_core_sflags(pi->sh->sih, 0, 0) & SISF_FCLKA);

	phy_init = pi->pi_fptr.init;

	if (phy_init == NULL) {
		ASSERT(phy_init != NULL);
		return;
	}

	wlc_phy_anacore(pih, ON);

	if (CHSPEC_BW(pi->radio_chanspec) != pi->bw)
		wlapi_bmac_bw_set(pi->sh->physhim,
				  CHSPEC_BW(pi->radio_chanspec));

	pi->nphy_gain_boost = TRUE;

	wlc_phy_switch_radio((wlc_phy_t *) pi, ON);

	(*phy_init) (pi);

	pi->phy_init_por = FALSE;

	if (D11REV_IS(pi->sh->corerev, 11) || D11REV_IS(pi->sh->corerev, 12))
		wlc_phy_do_dummy_tx(pi, TRUE, OFF);

	if (!(ISNPHY(pi)))
		wlc_phy_txpower_update_shm(pi);

	wlc_phy_ant_rxdiv_set((wlc_phy_t *) pi, pi->sh->rx_antdiv);

	pi->init_in_progress = FALSE;
}

void BCMINITFN(wlc_phy_cal_init) (wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;
	initfn_t cal_init = NULL;

	ASSERT((R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC) == 0);

	if (!pi->initialized) {
		cal_init = pi->pi_fptr.calinit;
		if (cal_init)
			(*cal_init) (pi);

		pi->initialized = TRUE;
	}
}

int BCMUNINITFN(wlc_phy_down) (wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;
	int callbacks = 0;

	ASSERT(pi->phytest_on == FALSE);

	if (pi->phycal_timer
	    && !wlapi_del_timer(pi->sh->physhim, pi->phycal_timer))
		callbacks++;

	pi->nphy_iqcal_chanspec_2G = 0;
	pi->nphy_iqcal_chanspec_5G = 0;

	return callbacks;
}

static uint32 wlc_phy_get_radio_ver(phy_info_t *pi)
{
	uint32 ver;

	ver = read_radio_id(pi);

	return ver;
}

void
wlc_phy_table_addr(phy_info_t *pi, uint tbl_id, uint tbl_offset,
		   uint16 tblAddr, uint16 tblDataHi, uint16 tblDataLo)
{
	write_phy_reg(pi, tblAddr, (tbl_id << 10) | tbl_offset);

	pi->tbl_data_hi = tblDataHi;
	pi->tbl_data_lo = tblDataLo;

	if ((CHIPID(pi->sh->chip) == BCM43224_CHIP_ID ||
	     CHIPID(pi->sh->chip) == BCM43421_CHIP_ID) &&
	    (pi->sh->chiprev == 1)) {
		pi->tbl_addr = tblAddr;
		pi->tbl_save_id = tbl_id;
		pi->tbl_save_offset = tbl_offset;
	}
}

void wlc_phy_table_data_write(phy_info_t *pi, uint width, uint32 val)
{
	ASSERT((width == 8) || (width == 16) || (width == 32));

	if ((CHIPID(pi->sh->chip) == BCM43224_CHIP_ID ||
	     CHIPID(pi->sh->chip) == BCM43421_CHIP_ID) &&
	    (pi->sh->chiprev == 1) &&
	    (pi->tbl_save_id == NPHY_TBL_ID_ANTSWCTRLLUT)) {
		read_phy_reg(pi, pi->tbl_data_lo);

		write_phy_reg(pi, pi->tbl_addr,
			      (pi->tbl_save_id << 10) | pi->tbl_save_offset);
		pi->tbl_save_offset++;
	}

	if (width == 32) {

		write_phy_reg(pi, pi->tbl_data_hi, (uint16) (val >> 16));
		write_phy_reg(pi, pi->tbl_data_lo, (uint16) val);
	} else {

		write_phy_reg(pi, pi->tbl_data_lo, (uint16) val);
	}
}

void
wlc_phy_write_table(phy_info_t *pi, const phytbl_info_t *ptbl_info,
		    uint16 tblAddr, uint16 tblDataHi, uint16 tblDataLo)
{
	uint idx;
	uint tbl_id = ptbl_info->tbl_id;
	uint tbl_offset = ptbl_info->tbl_offset;
	uint tbl_width = ptbl_info->tbl_width;
	const uint8 *ptbl_8b = (const uint8 *)ptbl_info->tbl_ptr;
	const uint16 *ptbl_16b = (const uint16 *)ptbl_info->tbl_ptr;
	const uint32 *ptbl_32b = (const uint32 *)ptbl_info->tbl_ptr;

	ASSERT((tbl_width == 8) || (tbl_width == 16) || (tbl_width == 32));

	write_phy_reg(pi, tblAddr, (tbl_id << 10) | tbl_offset);

	for (idx = 0; idx < ptbl_info->tbl_len; idx++) {

		if ((CHIPID(pi->sh->chip) == BCM43224_CHIP_ID ||
		     CHIPID(pi->sh->chip) == BCM43421_CHIP_ID) &&
		    (pi->sh->chiprev == 1) &&
		    (tbl_id == NPHY_TBL_ID_ANTSWCTRLLUT)) {
			read_phy_reg(pi, tblDataLo);

			write_phy_reg(pi, tblAddr,
				      (tbl_id << 10) | (tbl_offset + idx));
		}

		if (tbl_width == 32) {

			write_phy_reg(pi, tblDataHi,
				      (uint16) (ptbl_32b[idx] >> 16));
			write_phy_reg(pi, tblDataLo, (uint16) ptbl_32b[idx]);
		} else if (tbl_width == 16) {

			write_phy_reg(pi, tblDataLo, ptbl_16b[idx]);
		} else {

			write_phy_reg(pi, tblDataLo, ptbl_8b[idx]);
		}
	}
}

void
wlc_phy_read_table(phy_info_t *pi, const phytbl_info_t *ptbl_info,
		   uint16 tblAddr, uint16 tblDataHi, uint16 tblDataLo)
{
	uint idx;
	uint tbl_id = ptbl_info->tbl_id;
	uint tbl_offset = ptbl_info->tbl_offset;
	uint tbl_width = ptbl_info->tbl_width;
	uint8 *ptbl_8b = (uint8 *) (uintptr) ptbl_info->tbl_ptr;
	uint16 *ptbl_16b = (uint16 *) (uintptr) ptbl_info->tbl_ptr;
	uint32 *ptbl_32b = (uint32 *) (uintptr) ptbl_info->tbl_ptr;

	ASSERT((tbl_width == 8) || (tbl_width == 16) || (tbl_width == 32));

	write_phy_reg(pi, tblAddr, (tbl_id << 10) | tbl_offset);

	for (idx = 0; idx < ptbl_info->tbl_len; idx++) {

		if ((CHIPID(pi->sh->chip) == BCM43224_CHIP_ID ||
		     CHIPID(pi->sh->chip) == BCM43421_CHIP_ID) &&
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

			ptbl_8b[idx] = (uint8) read_phy_reg(pi, tblDataLo);
		}
	}
}

uint
wlc_phy_init_radio_regs_allbands(phy_info_t *pi, radio_20xx_regs_t *radioregs)
{
	uint i = 0;

	do {
		if (radioregs[i].do_init) {
			write_radio_reg(pi, radioregs[i].address,
					(uint16) radioregs[i].init);
		}

		i++;
	} while (radioregs[i].address != 0xffff);

	return i;
}

uint
wlc_phy_init_radio_regs(phy_info_t *pi, radio_regs_t *radioregs,
			uint16 core_offset)
{
	uint i = 0;
	uint count = 0;

	do {
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			if (radioregs[i].do_init_a) {
				write_radio_reg(pi,
						radioregs[i].
						address | core_offset,
						(uint16) radioregs[i].init_a);
				if (ISNPHY(pi) && (++count % 4 == 0))
					WLC_PHY_WAR_PR51571(pi);
			}
		} else {
			if (radioregs[i].do_init_g) {
				write_radio_reg(pi,
						radioregs[i].
						address | core_offset,
						(uint16) radioregs[i].init_g);
				if (ISNPHY(pi) && (++count % 4 == 0))
					WLC_PHY_WAR_PR51571(pi);
			}
		}

		i++;
	} while (radioregs[i].address != 0xffff);

	return i;
}

void wlc_phy_do_dummy_tx(phy_info_t *pi, bool ofdm, bool pa_on)
{
#define	DUMMY_PKT_LEN	20
	d11regs_t *regs = pi->regs;
	int i, count;
	uint8 ofdmpkt[DUMMY_PKT_LEN] = {
		0xcc, 0x01, 0x02, 0x00, 0x00, 0x00, 0xd4, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
	};
	uint8 cckpkt[DUMMY_PKT_LEN] = {
		0x6e, 0x84, 0x0b, 0x00, 0x00, 0x00, 0xd4, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
	};
	uint32 *dummypkt;

	ASSERT((R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC) == 0);

	dummypkt = (uint32 *) (ofdm ? ofdmpkt : cckpkt);
	wlapi_bmac_write_template_ram(pi->sh->physhim, 0, DUMMY_PKT_LEN,
				      dummypkt);

	W_REG(pi->sh->osh, &regs->xmtsel, 0);

	if (D11REV_GE(pi->sh->corerev, 11))
		W_REG(pi->sh->osh, &regs->wepctl, 0x100);
	else
		W_REG(pi->sh->osh, &regs->wepctl, 0);

	W_REG(pi->sh->osh, &regs->txe_phyctl, (ofdm ? 1 : 0) | PHY_TXC_ANT_0);
	if (ISNPHY(pi) || ISLCNPHY(pi)) {
		ASSERT(ofdm);
		W_REG(pi->sh->osh, &regs->txe_phyctl1, 0x1A02);
	}

	W_REG(pi->sh->osh, &regs->txe_wm_0, 0);
	W_REG(pi->sh->osh, &regs->txe_wm_1, 0);

	W_REG(pi->sh->osh, &regs->xmttplatetxptr, 0);
	W_REG(pi->sh->osh, &regs->xmttxcnt, DUMMY_PKT_LEN);

	W_REG(pi->sh->osh, &regs->xmtsel, ((8 << 8) | (1 << 5) | (1 << 2) | 2));

	W_REG(pi->sh->osh, &regs->txe_ctl, 0);

	if (!pa_on) {
		if (ISNPHY(pi))
			wlc_phy_pa_override_nphy(pi, OFF);
	}

	if (ISNPHY(pi) || ISLCNPHY(pi))
		W_REG(pi->sh->osh, &regs->txe_aux, 0xD0);
	else
		W_REG(pi->sh->osh, &regs->txe_aux, ((1 << 5) | (1 << 4)));

	(void)R_REG(pi->sh->osh, &regs->txe_aux);

	i = 0;
	count = ofdm ? 30 : 250;

	if (ISSIM_ENAB(pi->sh->sih)) {
		count *= 100;
	}

	while ((i++ < count)
	       && (R_REG(pi->sh->osh, &regs->txe_status) & (1 << 7))) {
		OSL_DELAY(10);
	}

	i = 0;

	while ((i++ < 10)
	       && ((R_REG(pi->sh->osh, &regs->txe_status) & (1 << 10)) == 0)) {
		OSL_DELAY(10);
	}

	i = 0;

	while ((i++ < 10) && ((R_REG(pi->sh->osh, &regs->ifsstat) & (1 << 8)))) {
		OSL_DELAY(10);
	}
	if (!pa_on) {
		if (ISNPHY(pi))
			wlc_phy_pa_override_nphy(pi, ON);
	}
}

void wlc_phy_hold_upd(wlc_phy_t *pih, mbool id, bool set)
{
	phy_info_t *pi = (phy_info_t *) pih;
	ASSERT(id);

	if (set) {
		mboolset(pi->measure_hold, id);
	} else {
		mboolclr(pi->measure_hold, id);
	}

	return;
}

void wlc_phy_mute_upd(wlc_phy_t *pih, bool mute, mbool flags)
{
	phy_info_t *pi = (phy_info_t *) pih;

	if (mute) {
		mboolset(pi->measure_hold, PHY_HOLD_FOR_MUTE);
	} else {
		mboolclr(pi->measure_hold, PHY_HOLD_FOR_MUTE);
	}

	if (!mute && (flags & PHY_MUTE_FOR_PREISM))
		pi->nphy_perical_last = pi->sh->now - pi->sh->glacial_timer;
	return;
}

void wlc_phy_clear_tssi(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;

	if (ISNPHY(pi)) {
		return;
	} else {
		wlapi_bmac_write_shm(pi->sh->physhim, M_B_TSSI_0, NULL_TSSI_W);
		wlapi_bmac_write_shm(pi->sh->physhim, M_B_TSSI_1, NULL_TSSI_W);
		wlapi_bmac_write_shm(pi->sh->physhim, M_G_TSSI_0, NULL_TSSI_W);
		wlapi_bmac_write_shm(pi->sh->physhim, M_G_TSSI_1, NULL_TSSI_W);
	}
}

static bool wlc_phy_cal_txpower_recalc_sw(phy_info_t *pi)
{
	return FALSE;
}

void wlc_phy_switch_radio(wlc_phy_t *pih, bool on)
{
	phy_info_t *pi = (phy_info_t *) pih;

	if (NORADIO_ENAB(pi->pubpi))
		return;

	{
		uint mc;

		mc = R_REG(pi->sh->osh, &pi->regs->maccontrol);
	}

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

uint16 wlc_phy_bw_state_get(wlc_phy_t *ppi)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	return pi->bw;
}

void wlc_phy_bw_state_set(wlc_phy_t *ppi, uint16 bw)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	pi->bw = bw;
}

void wlc_phy_chanspec_radio_set(wlc_phy_t *ppi, chanspec_t newch)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	pi->radio_chanspec = newch;

}

chanspec_t wlc_phy_chanspec_get(wlc_phy_t *ppi)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	return pi->radio_chanspec;
}

void wlc_phy_chanspec_set(wlc_phy_t *ppi, chanspec_t chanspec)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	uint16 m_cur_channel;
	chansetfn_t chanspec_set = NULL;

	ASSERT(!wf_chspec_malformed(chanspec));

	m_cur_channel = CHSPEC_CHANNEL(chanspec);
	if (CHSPEC_IS5G(chanspec))
		m_cur_channel |= D11_CURCHANNEL_5G;
	if (CHSPEC_IS40(chanspec))
		m_cur_channel |= D11_CURCHANNEL_40;
	wlapi_bmac_write_shm(pi->sh->physhim, M_CURCHANNEL, m_cur_channel);

	chanspec_set = pi->pi_fptr.chanset;
	if (chanspec_set)
		(*chanspec_set) (pi, chanspec);

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

int wlc_phy_chanspec_bandrange_get(phy_info_t *pi, chanspec_t chanspec)
{
	int range = -1;
	uint channel = CHSPEC_CHANNEL(chanspec);
	uint freq = wlc_phy_channel2freq(channel);

	if (ISNPHY(pi)) {
		range = wlc_phy_get_chan_freq_range_nphy(pi, channel);
	} else if (ISLCNPHY(pi)) {
		range = wlc_phy_chanspec_freq2bandrange_lpssn(freq);
	} else
		ASSERT(0);

	return range;
}

void wlc_phy_chanspec_ch14_widefilter_set(wlc_phy_t *ppi, bool wide_filter)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	pi->channel_14_wide_filter = wide_filter;

}

int wlc_phy_channel2freq(uint channel)
{
	uint i;

	for (i = 0; i < ARRAYSIZE(chan_info_all); i++)
		if (chan_info_all[i].chan == channel)
			return chan_info_all[i].freq;
	return 0;
}

void
wlc_phy_chanspec_band_validch(wlc_phy_t *ppi, uint band, chanvec_t *channels)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	uint i;
	uint channel;

	ASSERT((band == WLC_BAND_2G) || (band == WLC_BAND_5G));

	bzero(channels, sizeof(chanvec_t));

	for (i = 0; i < ARRAYSIZE(chan_info_all); i++) {
		channel = chan_info_all[i].chan;

		if ((pi->a_band_high_disable) && (channel >= FIRST_REF5_CHANNUM)
		    && (channel <= LAST_REF5_CHANNUM))
			continue;

		if (((band == WLC_BAND_2G) && (channel <= CH_MAX_2G_CHANNEL)) ||
		    ((band == WLC_BAND_5G) && (channel > CH_MAX_2G_CHANNEL)))
			setbit(channels->vec, channel);
	}
}

chanspec_t wlc_phy_chanspec_band_firstch(wlc_phy_t *ppi, uint band)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	uint i;
	uint channel;
	chanspec_t chspec;

	ASSERT((band == WLC_BAND_2G) || (band == WLC_BAND_5G));

	for (i = 0; i < ARRAYSIZE(chan_info_all); i++) {
		channel = chan_info_all[i].chan;

		if (ISNPHY(pi) && IS40MHZ(pi)) {
			uint j;

			for (j = 0; j < ARRAYSIZE(chan_info_all); j++) {
				if (chan_info_all[j].chan ==
				    channel + CH_10MHZ_APART)
					break;
			}

			if (j == ARRAYSIZE(chan_info_all))
				continue;

			channel = UPPER_20_SB(channel);
			chspec =
			    channel | WL_CHANSPEC_BW_40 |
			    WL_CHANSPEC_CTL_SB_LOWER;
			if (band == WLC_BAND_2G)
				chspec |= WL_CHANSPEC_BAND_2G;
			else
				chspec |= WL_CHANSPEC_BAND_5G;
		} else
			chspec = CH20MHZ_CHSPEC(channel);

		if ((pi->a_band_high_disable) && (channel >= FIRST_REF5_CHANNUM)
		    && (channel <= LAST_REF5_CHANNUM))
			continue;

		if (((band == WLC_BAND_2G) && (channel <= CH_MAX_2G_CHANNEL)) ||
		    ((band == WLC_BAND_5G) && (channel > CH_MAX_2G_CHANNEL)))
			return chspec;
	}

	ASSERT(0);

	return (chanspec_t) INVCHANSPEC;
}

int wlc_phy_txpower_get(wlc_phy_t *ppi, uint *qdbm, bool *override)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	ASSERT(qdbm != NULL);
	*qdbm = pi->tx_user_target[0];
	if (override != NULL)
		*override = pi->txpwroverride;
	return 0;
}

void wlc_phy_txpower_target_set(wlc_phy_t *ppi, struct txpwr_limits *txpwr)
{
	bool mac_enabled = FALSE;
	phy_info_t *pi = (phy_info_t *) ppi;

	bcopy(&txpwr->cck[0], &pi->tx_user_target[TXP_FIRST_CCK],
	      WLC_NUM_RATES_CCK);

	bcopy(&txpwr->ofdm[0], &pi->tx_user_target[TXP_FIRST_OFDM],
	      WLC_NUM_RATES_OFDM);
	bcopy(&txpwr->ofdm_cdd[0], &pi->tx_user_target[TXP_FIRST_OFDM_20_CDD],
	      WLC_NUM_RATES_OFDM);

	bcopy(&txpwr->ofdm_40_siso[0],
	      &pi->tx_user_target[TXP_FIRST_OFDM_40_SISO], WLC_NUM_RATES_OFDM);
	bcopy(&txpwr->ofdm_40_cdd[0],
	      &pi->tx_user_target[TXP_FIRST_OFDM_40_CDD], WLC_NUM_RATES_OFDM);

	bcopy(&txpwr->mcs_20_siso[0],
	      &pi->tx_user_target[TXP_FIRST_MCS_20_SISO],
	      WLC_NUM_RATES_MCS_1_STREAM);
	bcopy(&txpwr->mcs_20_cdd[0], &pi->tx_user_target[TXP_FIRST_MCS_20_CDD],
	      WLC_NUM_RATES_MCS_1_STREAM);
	bcopy(&txpwr->mcs_20_stbc[0],
	      &pi->tx_user_target[TXP_FIRST_MCS_20_STBC],
	      WLC_NUM_RATES_MCS_1_STREAM);
	bcopy(&txpwr->mcs_20_mimo[0], &pi->tx_user_target[TXP_FIRST_MCS_20_SDM],
	      WLC_NUM_RATES_MCS_2_STREAM);

	bcopy(&txpwr->mcs_40_siso[0],
	      &pi->tx_user_target[TXP_FIRST_MCS_40_SISO],
	      WLC_NUM_RATES_MCS_1_STREAM);
	bcopy(&txpwr->mcs_40_cdd[0], &pi->tx_user_target[TXP_FIRST_MCS_40_CDD],
	      WLC_NUM_RATES_MCS_1_STREAM);
	bcopy(&txpwr->mcs_40_stbc[0],
	      &pi->tx_user_target[TXP_FIRST_MCS_40_STBC],
	      WLC_NUM_RATES_MCS_1_STREAM);
	bcopy(&txpwr->mcs_40_mimo[0], &pi->tx_user_target[TXP_FIRST_MCS_40_SDM],
	      WLC_NUM_RATES_MCS_2_STREAM);

	if (R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC)
		mac_enabled = TRUE;

	if (mac_enabled)
		wlapi_suspend_mac_and_wait(pi->sh->physhim);

	wlc_phy_txpower_recalc_target(pi);
	wlc_phy_cal_txpower_recalc_sw(pi);

	if (mac_enabled)
		wlapi_enable_mac(pi->sh->physhim);
}

int wlc_phy_txpower_set(wlc_phy_t *ppi, uint qdbm, bool override)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	int i;

	if (qdbm > 127)
		return 5;

	for (i = 0; i < TXP_NUM_RATES; i++)
		pi->tx_user_target[i] = (uint8) qdbm;

	pi->txpwroverride = FALSE;

	if (pi->sh->up) {
		if (!SCAN_INPROG_PHY(pi)) {
			bool suspend;

			suspend =
			    (0 ==
			     (R_REG(pi->sh->osh, &pi->regs->maccontrol) &
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
wlc_phy_txpower_sromlimit(wlc_phy_t *ppi, uint channel, uint8 *min_pwr,
			  uint8 *max_pwr, int txp_rate_idx)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	uint i;

	*min_pwr = pi->min_txpower * WLC_TXPWR_DB_FACTOR;

	if (ISNPHY(pi)) {
		if (txp_rate_idx < 0)
			txp_rate_idx = TXP_FIRST_CCK;
		wlc_phy_txpower_sromlimit_get_nphy(pi, channel, max_pwr,
						   (uint8) txp_rate_idx);

	} else if ((channel <= CH_MAX_2G_CHANNEL)) {
		if (txp_rate_idx < 0)
			txp_rate_idx = TXP_FIRST_CCK;
		*max_pwr = pi->tx_srom_max_rate_2g[txp_rate_idx];
	} else {

		*max_pwr = WLC_TXPWR_MAX;

		if (txp_rate_idx < 0)
			txp_rate_idx = TXP_FIRST_OFDM;

		for (i = 0; i < ARRAYSIZE(chan_info_all); i++) {
			if (channel == chan_info_all[i].chan) {
				break;
			}
		}
		ASSERT(i < ARRAYSIZE(chan_info_all));

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
wlc_phy_txpower_sromlimit_max_get(wlc_phy_t *ppi, uint chan, uint8 *max_txpwr,
				  uint8 *min_txpwr)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	uint8 tx_pwr_max = 0;
	uint8 tx_pwr_min = 255;
	uint8 max_num_rate;
	uint8 maxtxpwr, mintxpwr, rate, pactrl;

	pactrl = 0;

	max_num_rate = ISNPHY(pi) ? TXP_NUM_RATES :
	    ISLCNPHY(pi) ? (TXP_LAST_SISO_MCS_20 + 1) : (TXP_LAST_OFDM + 1);

	for (rate = 0; rate < max_num_rate; rate++) {

		wlc_phy_txpower_sromlimit(ppi, chan, &mintxpwr, &maxtxpwr,
					  rate);

		maxtxpwr = (maxtxpwr > pactrl) ? (maxtxpwr - pactrl) : 0;

		maxtxpwr = (maxtxpwr > 6) ? (maxtxpwr - 6) : 0;

		tx_pwr_max = MAX(tx_pwr_max, maxtxpwr);
		tx_pwr_min = MIN(tx_pwr_min, maxtxpwr);
	}
	*max_txpwr = tx_pwr_max;
	*min_txpwr = tx_pwr_min;
}

void
wlc_phy_txpower_boardlimit_band(wlc_phy_t *ppi, uint bandunit, int32 *max_pwr,
				int32 *min_pwr, uint32 *step_pwr)
{
	return;
}

uint8 wlc_phy_txpower_get_target_min(wlc_phy_t *ppi)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	return pi->tx_power_min;
}

uint8 wlc_phy_txpower_get_target_max(wlc_phy_t *ppi)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	return pi->tx_power_max;
}

void wlc_phy_txpower_recalc_target(phy_info_t *pi)
{
	uint8 maxtxpwr, mintxpwr, rate, pactrl;
	uint target_chan;
	uint8 tx_pwr_target[TXP_NUM_RATES];
	uint8 tx_pwr_max = 0;
	uint8 tx_pwr_min = 255;
	uint8 tx_pwr_max_rate_ind = 0;
	uint8 max_num_rate;
	uint8 start_rate = 0;
	chanspec_t chspec;
	uint32 band = CHSPEC2WLC_BAND(pi->radio_chanspec);
	initfn_t txpwr_recalc_fn = NULL;

	chspec = pi->radio_chanspec;
	if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_NONE)
		target_chan = CHSPEC_CHANNEL(chspec);
	else if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_UPPER)
		target_chan = UPPER_20_SB(CHSPEC_CHANNEL(chspec));
	else
		target_chan = LOWER_20_SB(CHSPEC_CHANNEL(chspec));

	pactrl = 0;
	if (ISLCNPHY(pi)) {
		uint32 offset_mcs, i;

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
#if WL11N
	max_num_rate = ((ISNPHY(pi)) ? (TXP_NUM_RATES) :
			((ISLCNPHY(pi)) ?
			 (TXP_LAST_SISO_MCS_20 + 1) : (TXP_LAST_OFDM + 1)));
#else
	max_num_rate = ((ISNPHY(pi)) ? (TXP_NUM_RATES) : (TXP_LAST_OFDM + 1));
#endif

	wlc_phy_upd_env_txpwr_rate_limits(pi, band);

	for (rate = start_rate; rate < max_num_rate; rate++) {

		tx_pwr_target[rate] = pi->tx_user_target[rate];

		if (pi->user_txpwr_at_rfport) {
			tx_pwr_target[rate] +=
			    wlc_user_txpwr_antport_to_rfport(pi, target_chan,
							     band, rate);
		}

		{

			wlc_phy_txpower_sromlimit((wlc_phy_t *) pi, target_chan,
						  &mintxpwr, &maxtxpwr, rate);

			maxtxpwr = MIN(maxtxpwr, pi->txpwr_limit[rate]);

			maxtxpwr =
			    (maxtxpwr > pactrl) ? (maxtxpwr - pactrl) : 0;

			maxtxpwr = (maxtxpwr > 6) ? (maxtxpwr - 6) : 0;

			maxtxpwr = MIN(maxtxpwr, tx_pwr_target[rate]);

			if (pi->txpwr_percent <= 100)
				maxtxpwr = (maxtxpwr * pi->txpwr_percent) / 100;

			tx_pwr_target[rate] = MAX(maxtxpwr, mintxpwr);
		}

		tx_pwr_target[rate] =
		    MIN(tx_pwr_target[rate], pi->txpwr_env_limit[rate]);

		if (tx_pwr_target[rate] > tx_pwr_max)
			tx_pwr_max_rate_ind = rate;

		tx_pwr_max = MAX(tx_pwr_max, tx_pwr_target[rate]);
		tx_pwr_min = MIN(tx_pwr_min, tx_pwr_target[rate]);
	}

	bzero(pi->tx_power_offset, sizeof(pi->tx_power_offset));
	pi->tx_power_max = tx_pwr_max;
	pi->tx_power_min = tx_pwr_min;
	pi->tx_power_max_rate_ind = tx_pwr_max_rate_ind;
	for (rate = 0; rate < max_num_rate; rate++) {

		pi->tx_power_target[rate] = tx_pwr_target[rate];

		if (!pi->hwpwrctrl || ISNPHY(pi)) {
			pi->tx_power_offset[rate] =
			    pi->tx_power_max - pi->tx_power_target[rate];
		} else {
			pi->tx_power_offset[rate] =
			    pi->tx_power_target[rate] - pi->tx_power_min;
		}
	}

	txpwr_recalc_fn = pi->pi_fptr.txpwrrecalc;
	if (txpwr_recalc_fn)
		(*txpwr_recalc_fn) (pi);
}

void
wlc_phy_txpower_reg_limit_calc(phy_info_t *pi, struct txpwr_limits *txpwr,
			       chanspec_t chanspec)
{
	uint8 tmp_txpwr_limit[2 * WLC_NUM_RATES_OFDM];
	uint8 *txpwr_ptr1 = NULL, *txpwr_ptr2 = NULL;
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

			for (rate2 = 0; rate2 < WLC_NUM_RATES_OFDM; rate2++) {
				tmp_txpwr_limit[rate2] = 0;
				tmp_txpwr_limit[WLC_NUM_RATES_OFDM + rate2] =
				    txpwr_ptr1[rate2];
			}
			wlc_phy_mcs_to_ofdm_powers_nphy(tmp_txpwr_limit, 0,
							WLC_NUM_RATES_OFDM - 1,
							WLC_NUM_RATES_OFDM);
			for (rate1 = rate_start_index, rate2 = 0;
			     rate2 < WLC_NUM_RATES_OFDM; rate1++, rate2++)
				pi->txpwr_limit[rate1] =
				    MIN(txpwr_ptr2[rate2],
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
			for (rate2 = 0; rate2 < WLC_NUM_RATES_OFDM; rate2++) {
				tmp_txpwr_limit[rate2] = 0;
				tmp_txpwr_limit[WLC_NUM_RATES_OFDM + rate2] =
				    txpwr_ptr1[rate2];
			}
			wlc_phy_ofdm_to_mcs_powers_nphy(tmp_txpwr_limit, 0,
							WLC_NUM_RATES_OFDM - 1,
							WLC_NUM_RATES_OFDM);
			for (rate1 = rate_start_index, rate2 = 0;
			     rate2 < WLC_NUM_RATES_MCS_1_STREAM;
			     rate1++, rate2++)
				pi->txpwr_limit[rate1] =
				    MIN(txpwr_ptr2[rate2],
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
			     rate2 < WLC_NUM_RATES_MCS_1_STREAM;
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
			     rate2 < WLC_NUM_RATES_MCS_2_STREAM;
			     rate1++, rate2++)
				pi->txpwr_limit[rate1] = txpwr_ptr1[rate2];
		}

		pi->txpwr_limit[WL_TX_POWER_MCS_32] = txpwr->mcs32;

		pi->txpwr_limit[WL_TX_POWER_MCS40_CDD_FIRST] =
		    MIN(pi->txpwr_limit[WL_TX_POWER_MCS40_CDD_FIRST],
			pi->txpwr_limit[WL_TX_POWER_MCS_32]);
		pi->txpwr_limit[WL_TX_POWER_MCS_32] =
		    pi->txpwr_limit[WL_TX_POWER_MCS40_CDD_FIRST];
	}
}

void wlc_phy_txpwr_percent_set(wlc_phy_t *ppi, uint8 txpwr_percent)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	pi->txpwr_percent = txpwr_percent;
}

void wlc_phy_machwcap_set(wlc_phy_t *ppi, uint32 machwcap)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	pi->sh->machwcap = machwcap;
}

void wlc_phy_runbist_config(wlc_phy_t *ppi, bool start_end)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	uint16 rxc;
	rxc = 0;

	if (start_end == ON) {
		if (!ISNPHY(pi))
			return;

		if (NREV_IS(pi->pubpi.phy_rev, 3)
		    || NREV_IS(pi->pubpi.phy_rev, 4)) {
			W_REG(pi->sh->osh, &pi->regs->phyregaddr, 0xa0);
			(void)R_REG(pi->sh->osh, &pi->regs->phyregaddr);
			rxc = R_REG(pi->sh->osh, &pi->regs->phyregdata);
			W_REG(pi->sh->osh, &pi->regs->phyregdata,
			      (0x1 << 15) | rxc);
		}
	} else {
		if (NREV_IS(pi->pubpi.phy_rev, 3)
		    || NREV_IS(pi->pubpi.phy_rev, 4)) {
			W_REG(pi->sh->osh, &pi->regs->phyregaddr, 0xa0);
			(void)R_REG(pi->sh->osh, &pi->regs->phyregaddr);
			W_REG(pi->sh->osh, &pi->regs->phyregdata, rxc);
		}

		wlc_phy_por_inform(ppi);
	}
}

void
wlc_phy_txpower_limit_set(wlc_phy_t *ppi, struct txpwr_limits *txpwr,
			  chanspec_t chanspec)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	wlc_phy_txpower_reg_limit_calc(pi, txpwr, chanspec);

	if (ISLCNPHY(pi)) {
		int i, j;
		for (i = TXP_FIRST_OFDM_20_CDD, j = 0;
		     j < WLC_NUM_RATES_MCS_1_STREAM; i++, j++) {
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

void wlc_phy_ofdm_rateset_war(wlc_phy_t *pih, bool war)
{
	phy_info_t *pi = (phy_info_t *) pih;

	pi->ofdm_rateset_war = war;
}

void wlc_phy_bf_preempt_enable(wlc_phy_t *pih, bool bf_preempt)
{
	phy_info_t *pi = (phy_info_t *) pih;

	pi->bf_preempt_4306 = bf_preempt;
}

void wlc_phy_txpower_update_shm(phy_info_t *pi)
{
	int j;
	if (ISNPHY(pi)) {
		ASSERT(0);
		return;
	}

	if (!pi->sh->clk)
		return;

	if (pi->hwpwrctrl) {
		uint16 offset;

		wlapi_bmac_write_shm(pi->sh->physhim, M_TXPWR_MAX, 63);
		wlapi_bmac_write_shm(pi->sh->physhim, M_TXPWR_N,
				     1 << NUM_TSSI_FRAMES);

		wlapi_bmac_write_shm(pi->sh->physhim, M_TXPWR_TARGET,
				     pi->tx_power_min << NUM_TSSI_FRAMES);

		wlapi_bmac_write_shm(pi->sh->physhim, M_TXPWR_CUR,
				     pi->hwpwr_txcur);

		for (j = TXP_FIRST_OFDM; j <= TXP_LAST_OFDM; j++) {
			const uint8 ucode_ofdm_rates[] = {
				0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c
			};
			offset = wlapi_bmac_rate_shm_offset(pi->sh->physhim,
							    ucode_ofdm_rates[j -
									     TXP_FIRST_OFDM]);
			wlapi_bmac_write_shm(pi->sh->physhim, offset + 6,
					     pi->tx_power_offset[j]);
			wlapi_bmac_write_shm(pi->sh->physhim, offset + 14,
					     -(pi->tx_power_offset[j] / 2));
		}

		wlapi_bmac_mhf(pi->sh->physhim, MHF2, MHF2_HWPWRCTL,
			       MHF2_HWPWRCTL, WLC_BAND_ALL);
	} else {
		int i;

		for (i = TXP_FIRST_OFDM; i <= TXP_LAST_OFDM; i++)
			pi->tx_power_offset[i] =
			    (uint8) ROUNDUP(pi->tx_power_offset[i], 8);
		wlapi_bmac_write_shm(pi->sh->physhim, M_OFDM_OFFSET,
				     (uint16) ((pi->
						tx_power_offset[TXP_FIRST_OFDM]
						+ 7) >> 3));
	}
}

bool wlc_phy_txpower_hw_ctrl_get(wlc_phy_t *ppi)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	if (ISNPHY(pi)) {
		return pi->nphy_txpwrctrl;
	} else {
		return pi->hwpwrctrl;
	}
}

void wlc_phy_txpower_hw_ctrl_set(wlc_phy_t *ppi, bool hwpwrctrl)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	bool cur_hwpwrctrl = pi->hwpwrctrl;
	bool suspend;

	if (!pi->hwpwrctrl_capable) {
		return;
	}

	pi->hwpwrctrl = hwpwrctrl;
	pi->nphy_txpwrctrl = hwpwrctrl;
	pi->txpwrctrl = hwpwrctrl;

	if (ISNPHY(pi)) {
		suspend =
		    (0 ==
		     (R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC));
		if (!suspend)
			wlapi_suspend_mac_and_wait(pi->sh->physhim);

		wlc_phy_txpwrctrl_enable_nphy(pi, pi->nphy_txpwrctrl);
		if (pi->nphy_txpwrctrl == PHY_TPC_HW_OFF) {
			wlc_phy_txpwr_fixpower_nphy(pi);
		} else {

			mod_phy_reg(pi, 0x1e7, (0x7f << 0),
				    pi->saved_txpwr_idx);
		}

		if (!suspend)
			wlapi_enable_mac(pi->sh->physhim);
	} else if (hwpwrctrl != cur_hwpwrctrl) {

		return;
	}
}

void wlc_phy_txpower_ipa_upd(phy_info_t *pi)
{

	if (NREV_GE(pi->pubpi.phy_rev, 3)) {
		pi->ipa2g_on = (pi->srom_fem2g.extpagain == 2);
		pi->ipa5g_on = (pi->srom_fem5g.extpagain == 2);
	} else {
		pi->ipa2g_on = FALSE;
		pi->ipa5g_on = FALSE;
	}
}

static uint32 wlc_phy_txpower_est_power_nphy(phy_info_t *pi);

static uint32 wlc_phy_txpower_est_power_nphy(phy_info_t *pi)
{
	int16 tx0_status, tx1_status;
	uint16 estPower1, estPower2;
	uint8 pwr0, pwr1, adj_pwr0, adj_pwr1;
	uint32 est_pwr;

	estPower1 = read_phy_reg(pi, 0x118);
	estPower2 = read_phy_reg(pi, 0x119);

	if ((estPower1 & (0x1 << 8))
	    == (0x1 << 8)) {
		pwr0 = (uint8) (estPower1 & (0xff << 0))
		    >> 0;
	} else {
		pwr0 = 0x80;
	}

	if ((estPower2 & (0x1 << 8))
	    == (0x1 << 8)) {
		pwr1 = (uint8) (estPower2 & (0xff << 0))
		    >> 0;
	} else {
		pwr1 = 0x80;
	}

	tx0_status = read_phy_reg(pi, 0x1ed);
	tx1_status = read_phy_reg(pi, 0x1ee);

	if ((tx0_status & (0x1 << 15))
	    == (0x1 << 15)) {
		adj_pwr0 = (uint8) (tx0_status & (0xff << 0))
		    >> 0;
	} else {
		adj_pwr0 = 0x80;
	}
	if ((tx1_status & (0x1 << 15))
	    == (0x1 << 15)) {
		adj_pwr1 = (uint8) (tx1_status & (0xff << 0))
		    >> 0;
	} else {
		adj_pwr1 = 0x80;
	}

	est_pwr =
	    (uint32) ((pwr0 << 24) | (pwr1 << 16) | (adj_pwr0 << 8) | adj_pwr1);
	return est_pwr;
}

void
wlc_phy_txpower_get_current(wlc_phy_t *ppi, tx_power_t *power, uint channel)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	uint rate, num_rates;
	uint8 min_pwr, max_pwr;

#if WL_TX_POWER_RATES != TXP_NUM_RATES
#error "tx_power_t struct out of sync with this fn"
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
		power->board_limit[rate] = (uint8) max_pwr;
		power->target[rate] = pi->tx_power_target[rate];
	}

	if (ISNPHY(pi)) {
		uint32 est_pout;

		wlapi_suspend_mac_and_wait(pi->sh->physhim);
		wlc_phyreg_enter((wlc_phy_t *) pi);
		est_pout = wlc_phy_txpower_est_power_nphy(pi);
		wlc_phyreg_exit((wlc_phy_t *) pi);
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
	} else if (!pi->hwpwrctrl) {
	} else if (pi->sh->up) {

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
				    (WL_TX_POWER_F_HW | WL_TX_POWER_F_ENABLED);
			else
				power->flags &=
				    ~(WL_TX_POWER_F_HW | WL_TX_POWER_F_ENABLED);

			wlc_lcnphy_get_tssi(pi, (int8 *) &power->est_Pout[0],
					    (int8 *) &power->est_Pout_cck);
		}
		wlc_phyreg_exit(ppi);
	}
}

void wlc_phy_antsel_type_set(wlc_phy_t *ppi, uint8 antsel_type)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	pi->antsel_type = antsel_type;
}

bool wlc_phy_test_ison(wlc_phy_t *ppi)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	return pi->phytest_on;
}

bool wlc_phy_ant_rxdiv_get(wlc_phy_t *ppi, uint8 *pval)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	bool ret = TRUE;

	wlc_phyreg_enter(ppi);

	if (ISNPHY(pi)) {

		ret = FALSE;
	} else if (ISLCNPHY(pi)) {
		uint16 crsctrl = read_phy_reg(pi, 0x410);
		uint16 div = crsctrl & (0x1 << 1);
		*pval = (div | ((crsctrl & (0x1 << 0)) ^ (div >> 1)));
	}

	wlc_phyreg_exit(ppi);

	return ret;
}

void wlc_phy_ant_rxdiv_set(wlc_phy_t *ppi, uint8 val)
{
	phy_info_t *pi = (phy_info_t *) ppi;
	bool suspend;

	pi->sh->rx_antdiv = val;

	if (!(ISNPHY(pi) && D11REV_IS(pi->sh->corerev, 16))) {
		if (val > ANT_RX_DIV_FORCE_1)
			wlapi_bmac_mhf(pi->sh->physhim, MHF1, MHF1_ANTDIV,
				       MHF1_ANTDIV, WLC_BAND_ALL);
		else
			wlapi_bmac_mhf(pi->sh->physhim, MHF1, MHF1_ANTDIV, 0,
				       WLC_BAND_ALL);
	}

	if (ISNPHY(pi)) {

		return;
	}

	if (!pi->sh->clk)
		return;

	suspend =
	    (0 == (R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC));
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
			mod_phy_reg(pi, 0x410, (0x1 << 0), (uint16) val << 0);
		}
	} else {
		ASSERT(0);
	}

	if (!suspend)
		wlapi_enable_mac(pi->sh->physhim);

	return;
}

static bool
wlc_phy_noise_calc_phy(phy_info_t *pi, uint32 *cmplx_pwr, int8 *pwr_ant)
{
	int8 cmplx_pwr_dbm[PHY_CORE_MAX];
	uint8 i;

	bzero((uint8 *) cmplx_pwr_dbm, sizeof(cmplx_pwr_dbm));
	ASSERT(pi->pubpi.phy_corenum <= PHY_CORE_MAX);
	wlc_phy_compute_dB(cmplx_pwr, cmplx_pwr_dbm, pi->pubpi.phy_corenum);

	for (i = 0; i < pi->pubpi.phy_corenum; i++) {
		if (NREV_GE(pi->pubpi.phy_rev, 3))
			cmplx_pwr_dbm[i] += (int8) PHY_NOISE_OFFSETFACT_4322;
		else

			cmplx_pwr_dbm[i] += (int8) (16 - (15) * 3 - 70);
	}

	for (i = 0; i < pi->pubpi.phy_corenum; i++) {
		pi->nphy_noise_win[i][pi->nphy_noise_index] = cmplx_pwr_dbm[i];
		pwr_ant[i] = cmplx_pwr_dbm[i];
	}
	pi->nphy_noise_index =
	    MODINC_POW2(pi->nphy_noise_index, PHY_NOISE_WINDOW_SZ);
	return TRUE;
}

static void
wlc_phy_noise_sample_request(wlc_phy_t *pih, uint8 reason, uint8 ch)
{
	phy_info_t *pi = (phy_info_t *) pih;
	int8 noise_dbm = PHY_NOISE_FIXED_VAL_NPHY;
	bool sampling_in_progress = (pi->phynoise_state != 0);
	bool wait_for_intr = TRUE;

	if (NORADIO_ENAB(pi->pubpi)) {
		return;
	}

	switch (reason) {
	case PHY_NOISE_SAMPLE_MON:

		pi->phynoise_chan_watchdog = ch;
		pi->phynoise_state |= PHY_NOISE_STATE_MON;

		break;

	case PHY_NOISE_SAMPLE_EXTERNAL:

		pi->phynoise_state |= PHY_NOISE_STATE_EXTERNAL;
		break;

	default:
		ASSERT(0);
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

		wait_for_intr = FALSE;
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

			OR_REG(pi->sh->osh, &pi->regs->maccommand,
			       MCMD_BG_NOISE);
		} else {
			wlapi_suspend_mac_and_wait(pi->sh->physhim);
			wlc_lcnphy_deaf_mode(pi, (bool) 0);
			noise_dbm = (int8) wlc_lcnphy_rx_signal_power(pi, 20);
			wlc_lcnphy_deaf_mode(pi, (bool) 1);
			wlapi_enable_mac(pi->sh->physhim);
			wait_for_intr = FALSE;
		}
	} else if (ISNPHY(pi)) {
		if (!pi->phynoise_polling
		    || (reason == PHY_NOISE_SAMPLE_EXTERNAL)) {

			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP0, 0);
			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP1, 0);
			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP2, 0);
			wlapi_bmac_write_shm(pi->sh->physhim, M_PWRIND_MAP3, 0);

			OR_REG(pi->sh->osh, &pi->regs->maccommand,
			       MCMD_BG_NOISE);
		} else {
			phy_iq_est_t est[PHY_CORE_MAX];
			uint32 cmplx_pwr[PHY_CORE_MAX];
			int8 noise_dbm_ant[PHY_CORE_MAX];
			uint16 log_num_samps, num_samps, classif_state = 0;
			uint8 wait_time = 32;
			uint8 wait_crs = 0;
			uint8 i;

			bzero((uint8 *) est, sizeof(est));
			bzero((uint8 *) cmplx_pwr, sizeof(cmplx_pwr));
			bzero((uint8 *) noise_dbm_ant, sizeof(noise_dbm_ant));

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
				cmplx_pwr[i] =
				    (est[i].i_pwr +
				     est[i].q_pwr) >> log_num_samps;

			wlc_phy_noise_calc_phy(pi, cmplx_pwr, noise_dbm_ant);

			for (i = 0; i < pi->pubpi.phy_corenum; i++) {
				pi->nphy_noise_win[i][pi->nphy_noise_index] =
				    noise_dbm_ant[i];

				if (noise_dbm_ant[i] > noise_dbm)
					noise_dbm = noise_dbm_ant[i];
			}
			pi->nphy_noise_index = MODINC_POW2(pi->nphy_noise_index,
							   PHY_NOISE_WINDOW_SZ);

			wait_for_intr = FALSE;
		}
	}

 done:

	if (!wait_for_intr)
		wlc_phy_noise_cb(pi, ch, noise_dbm);

}

void wlc_phy_noise_sample_request_external(wlc_phy_t *pih)
{
	uint8 channel;

	channel = CHSPEC_CHANNEL(wlc_phy_chanspec_get(pih));

	wlc_phy_noise_sample_request(pih, PHY_NOISE_SAMPLE_EXTERNAL, channel);
}

static void wlc_phy_noise_cb(phy_info_t *pi, uint8 channel, int8 noise_dbm)
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

	if (pi->phynoise_state & PHY_NOISE_STATE_EXTERNAL) {
		pi->phynoise_state &= ~PHY_NOISE_STATE_EXTERNAL;
	}

}

static int8 wlc_phy_noise_read_shmem(phy_info_t *pi)
{
	uint32 cmplx_pwr[PHY_CORE_MAX];
	int8 noise_dbm_ant[PHY_CORE_MAX];
	uint16 lo, hi;
	uint32 cmplx_pwr_tot = 0;
	int8 noise_dbm = PHY_NOISE_FIXED_VAL_NPHY;
	uint8 idx, core;

	ASSERT(pi->pubpi.phy_corenum <= PHY_CORE_MAX);
	bzero((uint8 *) cmplx_pwr, sizeof(cmplx_pwr));
	bzero((uint8 *) noise_dbm_ant, sizeof(noise_dbm_ant));

	for (idx = 0, core = 0; core < pi->pubpi.phy_corenum; idx += 2, core++) {
		lo = wlapi_bmac_read_shm(pi->sh->physhim, M_PWRIND_MAP(idx));
		hi = wlapi_bmac_read_shm(pi->sh->physhim,
					 M_PWRIND_MAP(idx + 1));
		cmplx_pwr[core] = (hi << 16) + lo;
		cmplx_pwr_tot += cmplx_pwr[core];
		if (cmplx_pwr[core] == 0) {
			noise_dbm_ant[core] = PHY_NOISE_FIXED_VAL_NPHY;
		} else
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

void wlc_phy_noise_sample_intr(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;
	uint16 jssi_aux;
	uint8 channel = 0;
	int8 noise_dbm = PHY_NOISE_FIXED_VAL_NPHY;

	if (ISLCNPHY(pi)) {
		uint32 cmplx_pwr, cmplx_pwr0, cmplx_pwr1;
		uint16 lo, hi;
		int32 pwr_offset_dB, gain_dB;
		uint16 status_0, status_1;

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

			noise_dbm += (int8) (pwr_offset_dB - 30);

			gain_dB = (status_0 & 0x1ff);
			noise_dbm -= (int8) (gain_dB);
		} else {
			noise_dbm = PHY_NOISE_FIXED_VAL_LCNPHY;
		}
	} else if (ISNPHY(pi)) {

		jssi_aux = wlapi_bmac_read_shm(pi->sh->physhim, M_JSSI_AUX);
		channel = jssi_aux & D11_CURCHANNEL_MAX;

		noise_dbm = wlc_phy_noise_read_shmem(pi);
	} else {
		ASSERT(0);
	}

	wlc_phy_noise_cb(pi, channel, noise_dbm);

}

int8 lcnphy_gain_index_offset_for_pkt_rssi[] = {
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

void wlc_phy_compute_dB(uint32 *cmplx_pwr, int8 *p_cmplx_pwr_dB, uint8 core)
{
	uint8 shift_ct, lsb, msb, secondmsb, i;
	uint32 tmp;

	for (i = 0; i < core; i++) {
		tmp = cmplx_pwr[i];
		shift_ct = msb = secondmsb = 0;
		while (tmp != 0) {
			tmp = tmp >> 1;
			shift_ct++;
			lsb = (uint8) (tmp & 1);
			if (lsb == 1)
				msb = shift_ct;
		}
		secondmsb = (uint8) ((cmplx_pwr[i] >> (msb - 1)) & 1);
		p_cmplx_pwr_dB[i] = (int8) (3 * msb + 2 * secondmsb);
	}
}

void BCMFASTPATH wlc_phy_rssi_compute(wlc_phy_t *pih, void *ctx)
{
	wlc_d11rxhdr_t *wlc_rxhdr = (wlc_d11rxhdr_t *) ctx;
	d11rxhdr_t *rxh = &wlc_rxhdr->rxhdr;
	int rssi = ltoh16(rxh->PhyRxStatus_1) & PRXS1_JSSI_MASK;
	uint radioid = pih->radioid;
	phy_info_t *pi = (phy_info_t *) pih;

	if (NORADIO_ENAB(pi->pubpi)) {
		rssi = WLC_RSSI_INVALID;
		goto end;
	}

	if ((pi->sh->corerev >= 11)
	    && !(ltoh16(rxh->RxStatus2) & RXS_PHYRXST_VALID)) {
		rssi = WLC_RSSI_INVALID;
		goto end;
	}

	if (ISLCNPHY(pi)) {
		uint8 gidx = (ltoh16(rxh->PhyRxStatus_2) & 0xFC00) >> 10;
		phy_info_lcnphy_t *pi_lcn = pi->u.pi_lcnphy;

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
		ASSERT(ISNPHY(pi));
		rssi = wlc_phy_rssi_compute_nphy(pi, wlc_rxhdr);
	} else {
		ASSERT((const char *)"Unknown radio" == NULL);
	}

 end:
	wlc_rxhdr->rssi = (int8) rssi;
}

void wlc_phy_freqtrack_start(wlc_phy_t *pih)
{
	return;
}

void wlc_phy_freqtrack_end(wlc_phy_t *pih)
{
	return;
}

void wlc_phy_set_deaf(wlc_phy_t *ppi, bool user_flag)
{
	phy_info_t *pi;
	pi = (phy_info_t *) ppi;

	if (ISLCNPHY(pi))
		wlc_lcnphy_deaf_mode(pi, TRUE);
	else if (ISNPHY(pi))
		wlc_nphy_deaf_mode(pi, TRUE);
	else {
		ASSERT(0);
	}
}

void wlc_phy_watchdog(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *) pih;
	bool delay_phy_cal = FALSE;
	pi->sh->now++;

	if (!pi->watchdog_override)
		return;

	if (!(SCAN_RM_IN_PROGRESS(pi) || PLT_INPROG_PHY(pi))) {
		wlc_phy_noise_sample_request((wlc_phy_t *) pi,
					     PHY_NOISE_SAMPLE_MON,
					     CHSPEC_CHANNEL(pi->
							    radio_chanspec));
	}

	if (pi->phynoise_state && (pi->sh->now - pi->phynoise_now) > 5) {
		pi->phynoise_state = 0;
	}

	if ((!pi->phycal_txpower) ||
	    ((pi->sh->now - pi->phycal_txpower) >= pi->sh->fast_timer)) {

		if (!SCAN_INPROG_PHY(pi) && wlc_phy_cal_txpower_recalc_sw(pi)) {
			pi->phycal_txpower = pi->sh->now;
		}
	}

	if (NORADIO_ENAB(pi->pubpi))
		return;

	if ((SCAN_RM_IN_PROGRESS(pi) || PLT_INPROG_PHY(pi)
	     || ASSOC_INPROG_PHY(pi)))
		return;

	if (ISNPHY(pi) && !pi->disable_percal && !delay_phy_cal) {

		if ((pi->nphy_perical != PHY_PERICAL_DISABLE) &&
		    (pi->nphy_perical != PHY_PERICAL_MANUAL) &&
		    ((pi->sh->now - pi->nphy_perical_last) >=
		     pi->sh->glacial_timer))
			wlc_phy_cal_perical((wlc_phy_t *) pi,
					    PHY_PERICAL_WATCHDOG);

		wlc_phy_txpwr_papd_cal_nphy(pi);
	}

	if (ISLCNPHY(pi)) {
		if (pi->phy_forcecal ||
		    ((pi->sh->now - pi->phy_lastcal) >=
		     pi->sh->glacial_timer)) {
			if (!(SCAN_RM_IN_PROGRESS(pi) || ASSOC_INPROG_PHY(pi)))
				wlc_lcnphy_calib_modes(pi,
						       LCNPHY_PERICAL_TEMPBASED_TXPWRCTRL);
			if (!
			    (SCAN_RM_IN_PROGRESS(pi) || PLT_INPROG_PHY(pi)
			     || ASSOC_INPROG_PHY(pi)
			     || pi->carrier_suppr_disable
			     || pi->pkteng_in_progress || pi->disable_percal))
				wlc_lcnphy_calib_modes(pi,
						       PHY_PERICAL_WATCHDOG);
		}
	}
}

void wlc_phy_BSSinit(wlc_phy_t *pih, bool bonlyap, int rssi)
{
	phy_info_t *pi = (phy_info_t *) pih;
	uint i;
	uint k;

	for (i = 0; i < MA_WINDOW_SZ; i++) {
		pi->sh->phy_noise_window[i] = (int8) (rssi & 0xff);
	}
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
wlc_phy_papd_decode_epsilon(uint32 epsilon, int32 *eps_real, int32 *eps_imag)
{
	*eps_imag = (epsilon >> 13);
	if (*eps_imag > 0xfff)
		*eps_imag -= 0x2000;

	*eps_real = (epsilon & 0x1fff);
	if (*eps_real > 0xfff)
		*eps_real -= 0x2000;
}

static const fixed AtanTbl[] = {
	2949120,
	1740967,
	919879,
	466945,
	234379,
	117304,
	58666,
	29335,
	14668,
	7334,
	3667,
	1833,
	917,
	458,
	229,
	115,
	57,
	29
};

void wlc_phy_cordic(fixed theta, cint32 *val)
{
	fixed angle, valtmp;
	unsigned iter;
	int signx = 1;
	int signtheta;

	val[0].i = CORDIC_AG;
	val[0].q = 0;
	angle = 0;

	signtheta = (theta < 0) ? -1 : 1;
	theta =
	    ((theta + FIXED(180) * signtheta) % FIXED(360)) -
	    FIXED(180) * signtheta;

	if (FLOAT(theta) > 90) {
		theta -= FIXED(180);
		signx = -1;
	} else if (FLOAT(theta) < -90) {
		theta += FIXED(180);
		signx = -1;
	}

	for (iter = 0; iter < CORDIC_NI; iter++) {
		if (theta > angle) {
			valtmp = val[0].i - (val[0].q >> iter);
			val[0].q = (val[0].i >> iter) + val[0].q;
			val[0].i = valtmp;
			angle += AtanTbl[iter];
		} else {
			valtmp = val[0].i + (val[0].q >> iter);
			val[0].q = -(val[0].i >> iter) + val[0].q;
			val[0].i = valtmp;
			angle -= AtanTbl[iter];
		}
	}

	val[0].i = val[0].i * signx;
	val[0].q = val[0].q * signx;
}

void wlc_phy_cal_perical_mphase_reset(phy_info_t *pi)
{
	wlapi_del_timer(pi->sh->physhim, pi->phycal_timer);

	pi->cal_type_override = PHY_PERICAL_AUTO;
	pi->mphase_cal_phase_id = MPHASE_CAL_STATE_IDLE;
	pi->mphase_txcal_cmdidx = 0;
}

static void wlc_phy_cal_perical_mphase_schedule(phy_info_t *pi, uint delay)
{

	if ((pi->nphy_perical != PHY_PERICAL_MPHASE) &&
	    (pi->nphy_perical != PHY_PERICAL_MANUAL))
		return;

	wlapi_del_timer(pi->sh->physhim, pi->phycal_timer);

	pi->mphase_cal_phase_id = MPHASE_CAL_STATE_INIT;
	wlapi_add_timer(pi->sh->physhim, pi->phycal_timer, delay, 0);
}

void wlc_phy_cal_perical(wlc_phy_t *pih, uint8 reason)
{
	int16 nphy_currtemp = 0;
	int16 delta_temp = 0;
	bool do_periodic_cal = TRUE;
	phy_info_t *pi = (phy_info_t *) pih;

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
			if (PHY_PERICAL_MPHASE_PENDING(pi)) {
				wlc_phy_cal_perical_mphase_reset(pi);
			}
			wlc_phy_cal_perical_mphase_schedule(pi,
							    PHY_PERICAL_INIT_DELAY);
		}
		break;

	case PHY_PERICAL_JOIN_BSS:
	case PHY_PERICAL_START_IBSS:
	case PHY_PERICAL_UP_BSS:
		if ((pi->nphy_perical == PHY_PERICAL_MPHASE) &&
		    PHY_PERICAL_MPHASE_PENDING(pi)) {
			wlc_phy_cal_perical_mphase_reset(pi);
		}

		pi->first_cal_after_assoc = TRUE;

		pi->cal_type_override = PHY_PERICAL_FULL;

		if (pi->phycal_tempdelta) {
			pi->nphy_lastcal_temp = wlc_phy_tempsense_nphy(pi);
		}
		wlc_phy_cal_perical_nphy_run(pi, PHY_PERICAL_FULL);
		break;

	case PHY_PERICAL_WATCHDOG:
		if (pi->phycal_tempdelta) {
			nphy_currtemp = wlc_phy_tempsense_nphy(pi);
			delta_temp =
			    (nphy_currtemp > pi->nphy_lastcal_temp) ?
			    nphy_currtemp - pi->nphy_lastcal_temp :
			    pi->nphy_lastcal_temp - nphy_currtemp;

			if ((delta_temp < (int16) pi->phycal_tempdelta) &&
			    (pi->nphy_txiqlocal_chanspec ==
			     pi->radio_chanspec)) {
				do_periodic_cal = FALSE;
			} else {
				pi->nphy_lastcal_temp = nphy_currtemp;
			}
		}

		if (do_periodic_cal) {

			if (pi->nphy_perical == PHY_PERICAL_MPHASE) {

				if (!PHY_PERICAL_MPHASE_PENDING(pi))
					wlc_phy_cal_perical_mphase_schedule(pi,
									    PHY_PERICAL_WDOG_DELAY);
			} else if (pi->nphy_perical == PHY_PERICAL_SPHASE)
				wlc_phy_cal_perical_nphy_run(pi,
							     PHY_PERICAL_AUTO);
			else {
				ASSERT(0);
			}
		}
		break;
	default:
		ASSERT(0);
		break;
	}
}

void wlc_phy_cal_perical_mphase_restart(phy_info_t *pi)
{
	pi->mphase_cal_phase_id = MPHASE_CAL_STATE_INIT;
	pi->mphase_txcal_cmdidx = 0;
}

uint8 wlc_phy_nbits(int32 value)
{
	int32 abs_val;
	uint8 nbits = 0;

	abs_val = ABS(value);
	while ((abs_val >> nbits) > 0)
		nbits++;

	return nbits;
}

uint32 wlc_phy_sqrt_int(uint32 value)
{
	uint32 root = 0, shift = 0;

	for (shift = 0; shift < 32; shift += 2) {
		if (((0x40000000 >> shift) + root) <= value) {
			value -= ((0x40000000 >> shift) + root);
			root = (root >> 1) | (0x40000000 >> shift);
		} else {
			root = root >> 1;
		}
	}

	if (root < value)
		++root;

	return root;
}

void wlc_phy_stf_chain_init(wlc_phy_t *pih, uint8 txchain, uint8 rxchain)
{
	phy_info_t *pi = (phy_info_t *) pih;

	pi->sh->hw_phytxchain = txchain;
	pi->sh->hw_phyrxchain = rxchain;
	pi->sh->phytxchain = txchain;
	pi->sh->phyrxchain = rxchain;
	pi->pubpi.phy_corenum = (uint8) PHY_BITSCNT(pi->sh->phyrxchain);
}

void wlc_phy_stf_chain_set(wlc_phy_t *pih, uint8 txchain, uint8 rxchain)
{
	phy_info_t *pi = (phy_info_t *) pih;

	pi->sh->phytxchain = txchain;

	if (ISNPHY(pi)) {
		wlc_phy_rxcore_setstate_nphy(pih, rxchain);
	}
	pi->pubpi.phy_corenum = (uint8) PHY_BITSCNT(pi->sh->phyrxchain);
}

void wlc_phy_stf_chain_get(wlc_phy_t *pih, uint8 *txchain, uint8 *rxchain)
{
	phy_info_t *pi = (phy_info_t *) pih;

	*txchain = pi->sh->phytxchain;
	*rxchain = pi->sh->phyrxchain;
}

uint8 wlc_phy_stf_chain_active_get(wlc_phy_t *pih)
{
	int16 nphy_currtemp;
	uint8 active_bitmap;
	phy_info_t *pi = (phy_info_t *) pih;

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
				pi->phy_txcore_heatedup = TRUE;
			}
		} else {
			if (nphy_currtemp <= pi->phy_txcore_enable_temp) {
				active_bitmap |= 0x2;
				pi->phy_txcore_heatedup = FALSE;
			}
		}
	}

	return active_bitmap;
}

int8 wlc_phy_stf_ssmode_get(wlc_phy_t *pih, chanspec_t chanspec)
{
	phy_info_t *pi = (phy_info_t *) pih;
	uint8 siso_mcs_id, cdd_mcs_id;

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

const uint8 *wlc_phy_get_ofdm_rate_lookup(void)
{
	return ofdm_rate_lookup;
}

void wlc_lcnphy_epa_switch(phy_info_t *pi, bool mode)
{
	if ((CHIPID(pi->sh->chip) == BCM4313_CHIP_ID) &&
	    (pi->sh->boardflags & BFL_FEM)) {
		if (mode) {
			uint16 txant = 0;
			txant = wlapi_bmac_get_txant(pi->sh->physhim);
			if (txant == 1) {
				mod_phy_reg(pi, 0x44d, (0x1 << 2), (1) << 2);

				mod_phy_reg(pi, 0x44c, (0x1 << 2), (1) << 2);

			}
			si_corereg(pi->sh->sih, SI_CC_IDX,
				   OFFSETOF(chipcregs_t, gpiocontrol), ~0x0,
				   0x0);
			si_corereg(pi->sh->sih, SI_CC_IDX,
				   OFFSETOF(chipcregs_t, gpioout), 0x40, 0x40);
			si_corereg(pi->sh->sih, SI_CC_IDX,
				   OFFSETOF(chipcregs_t, gpioouten), 0x40,
				   0x40);
		} else {
			mod_phy_reg(pi, 0x44c, (0x1 << 2), (0) << 2);

			mod_phy_reg(pi, 0x44d, (0x1 << 2), (0) << 2);

			si_corereg(pi->sh->sih, SI_CC_IDX,
				   OFFSETOF(chipcregs_t, gpioout), 0x40, 0x00);
			si_corereg(pi->sh->sih, SI_CC_IDX,
				   OFFSETOF(chipcregs_t, gpioouten), 0x40, 0x0);
			si_corereg(pi->sh->sih, SI_CC_IDX,
				   OFFSETOF(chipcregs_t, gpiocontrol), ~0x0,
				   0x40);
		}
	}
}

static int8
wlc_user_txpwr_antport_to_rfport(phy_info_t *pi, uint chan, uint32 band,
				 uint8 rate)
{
	int8 offset = 0;

	if (!pi->user_txpwr_at_rfport)
		return offset;
	return offset;
}

static int8 wlc_phy_env_measure_vbat(phy_info_t *pi)
{
	if (ISLCNPHY(pi))
		return wlc_lcnphy_vbatsense(pi, 0);
	else
		return 0;
}

static int8 wlc_phy_env_measure_temperature(phy_info_t *pi)
{
	if (ISLCNPHY(pi))
		return wlc_lcnphy_tempsense_degree(pi, 0);
	else
		return 0;
}

static void wlc_phy_upd_env_txpwr_rate_limits(phy_info_t *pi, uint32 band)
{
	uint8 i;
	int8 temp, vbat;

	for (i = 0; i < TXP_NUM_RATES; i++)
		pi->txpwr_env_limit[i] = WLC_TXPWR_MAX;

	vbat = wlc_phy_env_measure_vbat(pi);
	temp = wlc_phy_env_measure_temperature(pi);

}

void wlc_phy_ldpc_override_set(wlc_phy_t *ppi, bool ldpc)
{
	return;
}

void
wlc_phy_get_pwrdet_offsets(phy_info_t *pi, int8 *cckoffset, int8 *ofdmoffset)
{
	*cckoffset = 0;
	*ofdmoffset = 0;
}

uint32 wlc_phy_qdiv_roundup(uint32 dividend, uint32 divisor, uint8 precision)
{
	uint32 quotient, remainder, roundup, rbit;

	ASSERT(divisor);

	quotient = dividend / divisor;
	remainder = dividend % divisor;
	rbit = divisor & 1;
	roundup = (divisor >> 1) + rbit;

	while (precision--) {
		quotient <<= 1;
		if (remainder >= roundup) {
			quotient++;
			remainder = ((remainder - roundup) << 1) + rbit;
		} else {
			remainder <<= 1;
		}
	}

	if (remainder >= roundup)
		quotient++;

	return quotient;
}

int8 wlc_phy_upd_rssi_offset(phy_info_t *pi, int8 rssi, chanspec_t chanspec)
{

	return rssi;
}

bool wlc_phy_txpower_ipa_ison(wlc_phy_t *ppi)
{
	phy_info_t *pi = (phy_info_t *) ppi;

	if (ISNPHY(pi))
		return wlc_phy_n_txpower_ipa_ison(pi);
	else
		return 0;
}
