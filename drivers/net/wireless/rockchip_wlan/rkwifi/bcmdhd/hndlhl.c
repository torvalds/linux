/*
 * Misc utility routines for accessing lhl specific features
 * of the SiliconBackplane-based Broadcom chips.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#include <hndpmu.h>
#include <hndlhl.h>
#include <sbchipc.h>
#include <hndsoc.h>
#include <bcmdevs.h>
#include <osl.h>
#include <sbgci.h>
#include <siutils.h>
#include <bcmutils.h>

#define SI_LHL_EXT_WAKE_REQ_MASK_MAGIC		0x7FBBF7FF	/* magic number for LHL EXT */

/* PmuRev1 has a 24-bit PMU RsrcReq timer. However it pushes all other bits
 * upward. To make the code to run for all revs we use a variable to tell how
 * many bits we need to shift.
 */
#define FLAGS_SHIFT	14
#define	LHL_ERROR(args) printf args
static const char BCMATTACHDATA(rstr_rfldo3p3_cap_war)[] = "rfldo3p3_cap_war";
static const char BCMATTACHDATA(rstr_abuck_volt_sleep)[] = "abuck_volt_sleep";
static const char BCMATTACHDATA(rstr_cbuck_volt_sleep)[] = "cbuck_volt_sleep";

void
si_lhl_setup(si_t *sih, osl_t *osh)
{
	if ((CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43014_CHIP_ID)) {
		/* Enable PMU sleep mode0 */
#ifdef BCMQT
		LHL_REG(sih, lhl_top_pwrseq_ctl_adr, LHL_PWRSEQ_CTL, PMU_SLEEP_MODE_0);
#else
		LHL_REG(sih, lhl_top_pwrseq_ctl_adr, LHL_PWRSEQ_CTL, PMU_SLEEP_MODE_2);
#endif
		/* Modify as per the
		BCM43012/LHL#LHL-RecommendedsettingforvariousPMUSleepModes:
		*/
		LHL_REG(sih, lhl_top_pwrup_ctl_adr, LHL_PWRUP_CTL_MASK, LHL_PWRUP_CTL);
		LHL_REG(sih, lhl_top_pwrup2_ctl_adr, LHL_PWRUP2_CTL_MASK, LHL_PWRUP2_CTL);
		LHL_REG(sih, lhl_top_pwrdn_ctl_adr, LHL_PWRDN_CTL_MASK, LHL_PWRDN_SLEEP_CNT);
		LHL_REG(sih, lhl_top_pwrdn2_ctl_adr, LHL_PWRDN2_CTL_MASK, LHL_PWRDN2_CTL);
	}

	if (!FWSIGN_ENAB() && si_hib_ext_wakeup_isenab(sih)) {
		/*
		 * Enable wakeup on GPIO1, PCIE clkreq and perst signal,
		 * GPIO[0] is mapped to GPIO1
		 * GPIO[1] is mapped to PCIE perst
		 * GPIO[2] is mapped to PCIE clkreq
		 */

		/* GPIO1 */
		/* Clear any old interrupt status */
		LHL_REG(sih, gpio_int_st_port_adr[0],
			1 << PCIE_GPIO1_GPIO_PIN, 1 << PCIE_GPIO1_GPIO_PIN);
		/* active high level trigger */
		LHL_REG(sih, gpio_ctrl_iocfg_p_adr[PCIE_GPIO1_GPIO_PIN], ~0,
			1 << GCI_GPIO_STS_WL_DIN_SELECT);
		LHL_REG(sih, gpio_int_en_port_adr[0],
			1 << PCIE_GPIO1_GPIO_PIN, 1 << PCIE_GPIO1_GPIO_PIN);
		LHL_REG(sih, gpio_int_st_port_adr[0],
			1 << PCIE_GPIO1_GPIO_PIN, 1 << PCIE_GPIO1_GPIO_PIN);
		si_gci_set_functionsel(sih, 1, CC_FNSEL_SAMEASPIN);

		/* PCIE perst */
		LHL_REG(sih, gpio_int_st_port_adr[0],
			1 << PCIE_PERST_GPIO_PIN, 1 << PCIE_PERST_GPIO_PIN);
		LHL_REG(sih, gpio_ctrl_iocfg_p_adr[PCIE_PERST_GPIO_PIN], ~0,
			(1 << GCI_GPIO_STS_EDGE_TRIG_BIT |
			1 << GCI_GPIO_STS_WL_DIN_SELECT));
		LHL_REG(sih, gpio_int_en_port_adr[0],
			1 << PCIE_PERST_GPIO_PIN, 1 << PCIE_PERST_GPIO_PIN);
		LHL_REG(sih, gpio_int_st_port_adr[0],
			1 << PCIE_PERST_GPIO_PIN, 1 << PCIE_PERST_GPIO_PIN);

		/* PCIE clkreq */
		LHL_REG(sih, gpio_int_st_port_adr[0],
			1 << PCIE_CLKREQ_GPIO_PIN, 1 << PCIE_CLKREQ_GPIO_PIN);
		LHL_REG(sih, gpio_ctrl_iocfg_p_adr[PCIE_CLKREQ_GPIO_PIN], ~0,
			(1 << GCI_GPIO_STS_NEG_EDGE_TRIG_BIT) |
			(1 << GCI_GPIO_STS_WL_DIN_SELECT));
		LHL_REG(sih, gpio_int_en_port_adr[0],
			1 << PCIE_CLKREQ_GPIO_PIN, 1 << PCIE_CLKREQ_GPIO_PIN);
		LHL_REG(sih, gpio_int_st_port_adr[0],
			1 << PCIE_CLKREQ_GPIO_PIN, 1 << PCIE_CLKREQ_GPIO_PIN);
	}
}

static const uint32 lpo_opt_tab[4][2] = {
	{ LPO1_PD_EN, LHL_LPO1_SEL },
	{ LPO2_PD_EN, LHL_LPO2_SEL },
	{ OSC_32k_PD, LHL_32k_SEL},
	{ EXTLPO_BUF_PD, LHL_EXT_SEL }
};

#define LPO_EN_OFFSET 0u
#define LPO_SEL_OFFSET 1u

static int
si_lhl_get_lpo_sel(si_t *sih, uint32 lpo)
{
	int sel;
	if (lpo <= LHL_EXT_SEL) {
		LHL_REG(sih, lhl_main_ctl_adr, lpo_opt_tab[lpo - 1u][LPO_EN_OFFSET], 0u);
		sel = lpo_opt_tab[lpo - 1u][LPO_SEL_OFFSET];
	} else {
		sel = BCME_NOTFOUND;
	}
	return sel;
}

static void
si_lhl_detect_lpo(si_t *sih, osl_t *osh)
{
	uint clk_det_cnt;
	int timeout = 0;
	gciregs_t *gciregs;
	gciregs = si_setcore(sih, GCI_CORE_ID, 0);
	ASSERT(gciregs != NULL);

	LHL_REG(sih, lhl_clk_det_ctl_adr, LHL_CLK_DET_CTL_ADR_LHL_CNTR_EN, 0);
	LHL_REG(sih, lhl_clk_det_ctl_adr,
		LHL_CLK_DET_CTL_ADR_LHL_CNTR_CLR, LHL_CLK_DET_CTL_ADR_LHL_CNTR_CLR);
	timeout = 0;
	clk_det_cnt =
		((R_REG(osh, &gciregs->lhl_clk_det_ctl_adr) & LHL_CLK_DET_CNT) >>
		LHL_CLK_DET_CNT_SHIFT);
	while (clk_det_cnt != 0 && timeout <= LPO_SEL_TIMEOUT) {
		OSL_DELAY(10);
		clk_det_cnt =
		((R_REG(osh, &gciregs->lhl_clk_det_ctl_adr) & LHL_CLK_DET_CNT) >>
		LHL_CLK_DET_CNT_SHIFT);
		timeout++;
	}

	if (clk_det_cnt != 0) {
		LHL_ERROR(("Clock not present as clear did not work timeout = %d\n", timeout));
		ROMMABLE_ASSERT(0);
	}
	LHL_REG(sih, lhl_clk_det_ctl_adr, LHL_CLK_DET_CTL_ADR_LHL_CNTR_CLR, 0);
	LHL_REG(sih, lhl_clk_det_ctl_adr, LHL_CLK_DET_CTL_ADR_LHL_CNTR_EN,
		LHL_CLK_DET_CTL_ADR_LHL_CNTR_EN);
	clk_det_cnt =
		((R_REG(osh, &gciregs->lhl_clk_det_ctl_adr) & LHL_CLK_DET_CNT) >>
		LHL_CLK_DET_CNT_SHIFT);
	timeout = 0;

	while (clk_det_cnt <= CLK_DET_CNT_THRESH && timeout <= LPO_SEL_TIMEOUT) {
		OSL_DELAY(10);
		clk_det_cnt =
		((R_REG(osh, &gciregs->lhl_clk_det_ctl_adr) & LHL_CLK_DET_CNT) >>
		LHL_CLK_DET_CNT_SHIFT);
		timeout++;
	}

	if (timeout >= LPO_SEL_TIMEOUT) {
		LHL_ERROR(("LPO is not available timeout = %u\n, timeout", timeout));
		ROMMABLE_ASSERT(0);
	}
}

static void
si_lhl_select_lpo(si_t *sih, osl_t *osh, int sel, uint32 lpo)
{
	uint status;
	int timeout = 0u;
	gciregs_t *gciregs;
	uint32 final_clk_sel;
	uint32 final_lpo_sel;
	gciregs = si_setcore(sih, GCI_CORE_ID, 0);
	ASSERT(gciregs != NULL);

	LHL_REG(sih, lhl_main_ctl_adr,
		LHL_MAIN_CTL_ADR_LHL_WLCLK_SEL, (sel) << LPO_SEL_SHIFT);
	final_clk_sel = (R_REG(osh, &gciregs->lhl_clk_status_adr)
		& LHL_MAIN_CTL_ADR_FINAL_CLK_SEL);
	final_lpo_sel = (unsigned)(((1u << sel) << LPO_FINAL_SEL_SHIFT));

	status = (final_clk_sel == final_lpo_sel) ? 1u : 0u;
	timeout = 0;
	while (!status && timeout <= LPO_SEL_TIMEOUT) {
		OSL_DELAY(10);
		final_clk_sel = (R_REG(osh, &gciregs->lhl_clk_status_adr)
			& LHL_MAIN_CTL_ADR_FINAL_CLK_SEL);
		status = (final_clk_sel == final_lpo_sel) ? 1u : 0u;
		timeout++;
	}

	if (timeout >= LPO_SEL_TIMEOUT) {
		LHL_ERROR(("LPO is not available timeout = %u\n, timeout", timeout));
		ROMMABLE_ASSERT(0);
	}

	/* for 4377 and chiprev B0 and greater do not power-off other LPOs */
	if (BCM4389_CHIP(sih->chip) || BCM4378_CHIP(sih->chip) || BCM4397_CHIP(sih->chip) ||
		BCM4388_CHIP(sih->chip) || BCM4387_CHIP(sih->chip) ||
		(CHIPID(sih->chip) == BCM4377_CHIP_ID)) {
		LHL_ERROR(("NOT Power Down other LPO\n"));
	} else {
		/* Power down the rest of the LPOs */

		if (lpo != LHL_EXT_LPO_ENAB) {
			LHL_REG(sih, lhl_main_ctl_adr, EXTLPO_BUF_PD, EXTLPO_BUF_PD);
		}

		if (lpo != LHL_LPO1_ENAB) {
			LHL_REG(sih, lhl_main_ctl_adr, LPO1_PD_EN, LPO1_PD_EN);
			LHL_REG(sih, lhl_main_ctl_adr, LPO1_PD_SEL, LPO1_PD_SEL_VAL);
		}
		if (lpo != LHL_LPO2_ENAB) {
			LHL_REG(sih, lhl_main_ctl_adr, LPO2_PD_EN, LPO2_PD_EN);
			LHL_REG(sih, lhl_main_ctl_adr, LPO2_PD_SEL, LPO2_PD_SEL_VAL);
		}
		if (lpo != LHL_OSC_32k_ENAB) {
			LHL_REG(sih, lhl_main_ctl_adr, OSC_32k_PD, OSC_32k_PD);
		}
		if (lpo != RADIO_LPO_ENAB) {
			si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_06, LPO_SEL, 0);
		}
	}

}

/* To skip this function, specify a invalid "lpo_select" value in nvram */
int
BCMATTACHFN(si_lhl_set_lpoclk)(si_t *sih, osl_t *osh, uint32 lpo_force)
{
	int lhl_wlclk_sel;
	uint32 lpo = 0;

	/* Apply nvram override to lpo */
	if (!FWSIGN_ENAB()) {
		if ((lpo = (uint32)getintvar(NULL, "lpo_select")) == 0) {
			if (lpo_force == LHL_LPO_AUTO) {
				lpo = LHL_OSC_32k_ENAB;
			} else {
				lpo = lpo_force;
			}
		}
	} else {
		lpo = lpo_force;
	}

	lhl_wlclk_sel = si_lhl_get_lpo_sel(sih, lpo);

	if (lhl_wlclk_sel < 0) {
		return BCME_OK;
	}

	LHL_REG(sih, lhl_clk_det_ctl_adr,
		LHL_CLK_DET_CTL_AD_CNTR_CLK_SEL, lhl_wlclk_sel);

	/* Detect the desired LPO */
	si_lhl_detect_lpo(sih, osh);

	/* Select the desired LPO */
	si_lhl_select_lpo(sih, osh, lhl_wlclk_sel, lpo);

	return BCME_OK;
}

void
BCMATTACHFN(si_lhl_timer_config)(si_t *sih, osl_t *osh, int timer_type)
{
	uint origidx;
	pmuregs_t *pmu = NULL;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}

	ASSERT(pmu != NULL);

	switch (timer_type) {
	case LHL_MAC_TIMER:
		/* Enable MAC Timer interrupt */
		LHL_REG(sih, lhl_wl_mactim0_intrp_adr,
			(LHL_WL_MACTIM_INTRP_EN | LHL_WL_MACTIM_INTRP_EDGE_TRIGGER),
			(LHL_WL_MACTIM_INTRP_EN | LHL_WL_MACTIM_INTRP_EDGE_TRIGGER));

		/* Programs bits for MACPHY_CLK_AVAIL and all its dependent bits in
		 * MacResourceReqMask0.
		 */
		PMU_REG(sih, mac_res_req_mask, ~0, si_pmu_rsrc_macphy_clk_deps(sih, osh, 0));

		/* One time init of mac_res_req_timer to enable interrupt and clock request */
		HND_PMU_SYNC_WR(sih, pmu, pmu, osh,
				PMUREGADDR(sih, pmu, pmu, mac_res_req_timer),
				((PRRT_ALP_REQ | PRRT_HQ_REQ | PRRT_INTEN) << FLAGS_SHIFT));

		/*
		 * Reset MAC Main Timer if in case it is running due to previous instance
		 * This also resets the interrupt status
		 */
		LHL_REG(sih, lhl_wl_mactim_int0_adr, LHL_WL_MACTIMER_MASK, 0x0);

		if (si_pmu_get_mac_rsrc_req_tmr_cnt(sih) > 1) {
			LHL_REG(sih, lhl_wl_mactim1_intrp_adr,
				(LHL_WL_MACTIM_INTRP_EN | LHL_WL_MACTIM_INTRP_EDGE_TRIGGER),
				(LHL_WL_MACTIM_INTRP_EN | LHL_WL_MACTIM_INTRP_EDGE_TRIGGER));

			PMU_REG(sih, mac_res_req_mask1, ~0,
				si_pmu_rsrc_macphy_clk_deps(sih, osh, 1));

			HND_PMU_SYNC_WR(sih, pmu, pmu, osh,
					PMUREGADDR(sih, pmu, pmu, mac_res_req_timer1),
					((PRRT_ALP_REQ | PRRT_HQ_REQ | PRRT_INTEN) << FLAGS_SHIFT));

			/*
			 * Reset MAC Aux Timer if in case it is running due to previous instance
			 * This also resets the interrupt status
			 */
			LHL_REG(sih, lhl_wl_mactim_int1_adr, LHL_WL_MACTIMER_MASK, 0x0);
		}

		if (si_pmu_get_mac_rsrc_req_tmr_cnt(sih) > 2) {
			LHL_REG(sih, lhl_wl_mactim2_intrp_adr,
				(LHL_WL_MACTIM_INTRP_EN | LHL_WL_MACTIM_INTRP_EDGE_TRIGGER),
				(LHL_WL_MACTIM_INTRP_EN | LHL_WL_MACTIM_INTRP_EDGE_TRIGGER));

			PMU_REG_NEW(sih, mac_res_req_mask2, ~0,
				si_pmu_rsrc_macphy_clk_deps(sih, osh, 2));

			HND_PMU_SYNC_WR(sih, pmu, pmu, osh,
					PMUREGADDR(sih, pmu, pmu, mac_res_req_timer2),
					((PRRT_ALP_REQ | PRRT_HQ_REQ | PRRT_INTEN) << FLAGS_SHIFT));

			/*
			 * Reset Scan MAC Timer if in case it is running due to previous instance
			 * This also resets the interrupt status
			 */
			LHL_REG(sih, lhl_wl_mactim_int2_adr, LHL_WL_MACTIMER_MASK, 0x0);
		}

		break;

	case LHL_ARM_TIMER:
		/* Enable ARM Timer interrupt */
		LHL_REG(sih, lhl_wl_armtim0_intrp_adr,
				(LHL_WL_ARMTIM0_INTRP_EN | LHL_WL_ARMTIM0_INTRP_EDGE_TRIGGER),
				(LHL_WL_ARMTIM0_INTRP_EN | LHL_WL_ARMTIM0_INTRP_EDGE_TRIGGER));

		/* Programs bits for HT_AVAIL and all its dependent bits in ResourceReqMask0 */
		/* Programs bits for CORE_RDY_CB and all its dependent bits in ResourceReqMask0 */
		PMU_REG(sih, res_req_mask, ~0, (si_pmu_rsrc_ht_avail_clk_deps(sih, osh) |
			si_pmu_rsrc_cb_ready_deps(sih, osh)));

		/* One time init of res_req_timer to enable interrupt and clock request
		 * For low power request only ALP (HT_AVAIL is anyway requested by res_req_mask)
		 */
		HND_PMU_SYNC_WR(sih, pmu, pmu, osh,
				PMUREGADDR(sih, pmu, pmu, res_req_timer),
				((PRRT_ALP_REQ | PRRT_HQ_REQ | PRRT_INTEN) << FLAGS_SHIFT));
		break;
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

void
BCMATTACHFN(si_lhl_timer_enable)(si_t *sih)
{
	/* Enable clks for pmu int propagation */
	PMU_REG(sih, pmuintctrl0, PMU_INTC_ALP_REQ, PMU_INTC_ALP_REQ);

	PMU_REG(sih, pmuintmask0, RSRC_INTR_MASK_TIMER_INT_0, RSRC_INTR_MASK_TIMER_INT_0);
#ifndef BCMQT
	LHL_REG(sih, lhl_main_ctl_adr, LHL_FAST_WRITE_EN, LHL_FAST_WRITE_EN);
#endif /* BCMQT */
	PMU_REG(sih, pmucontrol_ext, PCTL_EXT_USE_LHL_TIMER, PCTL_EXT_USE_LHL_TIMER);
}

void
BCMPOSTTRAPFN(si_lhl_timer_reset)(si_t *sih, uint coreid, uint coreunit)
{
	switch (coreid) {
	case D11_CORE_ID:
		switch (coreunit) {
		case 0: /* MAC_CORE_UNIT_0 */
			LHL_REG(sih, lhl_wl_mactim_int0_adr, LHL_WL_MACTIMER_MASK, 0x0);
			LHL_REG(sih, lhl_wl_mactim0_st_adr,
				LHL_WL_MACTIMER_INT_ST_MASK, LHL_WL_MACTIMER_INT_ST_MASK);
			break;
		case 1: /* MAC_CORE_UNIT_1 */
			LHL_REG(sih, lhl_wl_mactim_int1_adr, LHL_WL_MACTIMER_MASK, 0x0);
			LHL_REG(sih, lhl_wl_mactim1_st_adr,
				LHL_WL_MACTIMER_INT_ST_MASK, LHL_WL_MACTIMER_INT_ST_MASK);
			break;
		case 2: /* SCAN_CORE_UNIT */
			LHL_REG(sih, lhl_wl_mactim_int2_adr, LHL_WL_MACTIMER_MASK, 0x0);
			LHL_REG(sih, lhl_wl_mactim2_st_adr,
				LHL_WL_MACTIMER_INT_ST_MASK, LHL_WL_MACTIMER_INT_ST_MASK);
			break;
		default:
			LHL_ERROR(("Cannot reset lhl timer, wrong coreunit = %d\n", coreunit));
		}
		break;
	case ARMCR4_CORE_ID: /* intentional fallthrough */
	case ARMCA7_CORE_ID:
		LHL_REG(sih, lhl_wl_armtim0_adr, LHL_WL_MACTIMER_MASK, 0x0);
		LHL_REG(sih, lhl_wl_armtim0_st_adr,
			LHL_WL_MACTIMER_INT_ST_MASK, LHL_WL_MACTIMER_INT_ST_MASK);
		break;
	default:
		LHL_ERROR(("Cannot reset lhl timer, wrong coreid = 0x%x\n", coreid));
	}
}

void
si_lhl_ilp_config(si_t *sih, osl_t *osh, uint32 ilp_period)
{
	 gciregs_t *gciregs;
	 if ((CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
	     (CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
	     (CHIPID(sih->chip) == BCM43014_CHIP_ID)) {
		gciregs = si_setcore(sih, GCI_CORE_ID, 0);
		ASSERT(gciregs != NULL);
		W_REG(osh, &gciregs->lhl_wl_ilp_val_adr, ilp_period);
	 }
}

lhl_reg_set_t BCMATTACHDATA(lv_sleep_mode_4369_lhl_reg_set)[] =
{
	/* set wl_sleep_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), (1 << 0), (1 << 0)},

	/* set top_pwrsw_en, top_slb_en, top_iso_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), BCM_MASK32(5, 3), (0x0 << 3)},

	/* set VMUX_asr_sel_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), (1 << 8), (1 << 8)},

	/* lhl_lp_main_ctl_adr, disable lp_mode_en, set CSR and ASR field enables for LV mode */
	{LHL_REG_OFF(lhl_lp_main_ctl_adr), BCM_MASK32(21, 0), 0x3F89FF},

	/* lhl_lp_main_ctl1_adr, set CSR field values - CSR_adj - 0.64V and trim_adj -5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl1_adr), BCM_MASK32(23, 0), 0x9E9F97},

	/* lhl_lp_main_ctl2_adr, set ASR field values - ASR_adj - 0.76V and trim_adj +5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl2_adr), BCM_MASK32(13, 0), 0x07EE},

	/* lhl_lp_dn_ctl_adr, set down count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl_adr), ~0, ((LHL4369_CSR_OVERI_DIS_DWN_CNT << 16) |
		(LHL4369_CSR_MODE_DWN_CNT << 8) | (LHL4369_CSR_ADJ_DWN_CNT << 0))},

	/* lhl_lp_up_ctl_adr, set up count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl_adr), ~0, ((LHL4369_CSR_OVERI_DIS_UP_CNT << 16) |
		(LHL4369_CSR_MODE_UP_CNT << 8) | (LHL4369_CSR_ADJ_UP_CNT << 0))},

	/* lhl_lp_dn_ctl1_adr, set down count for hpbg_chop_dis, ASR_adj, vddc_sw_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl1_adr), ~0, ((LHL4369_VDDC_SW_DIS_DWN_CNT << 24) |
		(LHL4369_ASR_ADJ_DWN_CNT << 16) | (LHL4369_HPBG_CHOP_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl1_adr, set up count for hpbg_chop_dis, ASR_adj, vddc_sw_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl1_adr), ~0, ((LHL4369_VDDC_SW_DIS_UP_CNT << 24) |
		(LHL4369_ASR_ADJ_UP_CNT << 16) | (LHL4369_HPBG_CHOP_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl4_adr, set down count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl4_adr), ~0, ((LHL4369_ASR_MANUAL_MODE_DWN_CNT << 24) |
		(LHL4369_ASR_MODE_SEL_DWN_CNT << 16) | (LHL4369_ASR_LPPFM_MODE_DWN_CNT << 8) |
		(LHL4369_ASR_CLK4M_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl4_adr, set up count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl4_adr), ~0, ((LHL4369_ASR_MANUAL_MODE_UP_CNT << 24) |
		(LHL4369_ASR_MODE_SEL_UP_CNT << 16)| (LHL4369_ASR_LPPFM_MODE_UP_CNT << 8) |
		(LHL4369_ASR_CLK4M_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl3_adr, set down count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl3_adr), ~0, ((LHL4369_PFM_PWR_SLICE_DWN_CNT << 24) |
		(LHL4369_ASR_OVERI_DIS_DWN_CNT << 16) | (LHL4369_SRBG_REF_SEL_DWN_CNT << 8) |
		(LHL4369_HPBG_PU_EN_DWN_CNT << 0))},

	/* lhl_lp_up_ctl3_adr, set up count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl3_adr), ~0, ((LHL4369_PFM_PWR_SLICE_UP_CNT << 24) |
		(LHL4369_ASR_OVERI_DIS_UP_CNT << 16) | (LHL4369_SRBG_REF_SEL_UP_CNT << 8) |
		(LHL4369_HPBG_PU_EN_UP_CNT << 0))},

	/* lhl_lp_dn_ctl2_adr, set down count for CSR_trim_adj */
	{LHL_REG_OFF(lhl_lp_dn_ctl2_adr), ~0, (LHL4369_CSR_TRIM_ADJ_DWN_CNT << 16)},

	/* lhl_lp_up_ctl2_adr, set up count for CSR_trim_adj */
	{LHL_REG_OFF(lhl_lp_up_ctl2_adr), ~0, (LHL4369_CSR_TRIM_ADJ_UP_CNT << 16)},

	/* lhl_lp_dn_ctl5_adr, set down count for ASR_trim_adj */
	{LHL_REG_OFF(lhl_lp_dn_ctl5_adr), ~0, (LHL4369_ASR_TRIM_ADJ_DWN_CNT << 0)},

	/* lhl_lp_up_ctl5_adr, set down count for ASR_trim_adj */
	{LHL_REG_OFF(lhl_lp_up_ctl5_adr), ~0, (LHL4369_ASR_TRIM_ADJ_UP_CNT << 0)},

	/* Change the default down count values for the resources */
	/* lhl_top_pwrdn_ctl_adr, set down count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrdn_ctl_adr), ~0, ((LHL4369_PWRSW_EN_DWN_CNT << 24) |
		(LHL4369_SLB_EN_DWN_CNT << 16) | (LHL4369_ISO_EN_DWN_CNT << 8))},

	/* lhl_top_pwrdn2_ctl_adr, set down count for VMUX_asr_sel */
	{LHL_REG_OFF(lhl_top_pwrdn2_ctl_adr), ~0, (LHL4369_VMUX_ASR_SEL_DWN_CNT << 16)},

	/* Change the default up count values for the resources */
	/* lhl_top_pwrup_ctl_adr, set up count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrup_ctl_adr), ~0, ((LHL4369_PWRSW_EN_UP_CNT << 24) |
		(LHL4369_SLB_EN_UP_CNT << 16) | (LHL4369_ISO_EN_UP_CNT << 8))},

	/* lhl_top_pwrdn2_ctl_adr, set down count for VMUX_asr_sel */
	{LHL_REG_OFF(lhl_top_pwrup2_ctl_adr), ~0, ((LHL4369_VMUX_ASR_SEL_UP_CNT << 16))},

	/* Enable lhl interrupt */
	{LHL_REG_OFF(gci_intmask), (1 << 30), (1 << 30)},

	/* Enable LHL Wake up */
	{LHL_REG_OFF(gci_wakemask), (1 << 30), (1 << 30)},

	/* Making forceOTPpwrOn 1 */
	{LHL_REG_OFF(otpcontrol), (1 << 16), (1 << 16)}
};

lhl_reg_set_t BCMATTACHDATA(lv_sleep_mode_4378_lhl_reg_set)[] =
{
	/* set wl_sleep_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), (1 << 0), (1 << 0)},

	/* set top_pwrsw_en, top_slb_en, top_iso_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), BCM_MASK32(5, 3), (0x0 << 3)},

	/* set VMUX_asr_sel_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), (1 << 8), (1 << 8)},

	/* lhl_lp_main_ctl_adr, disable lp_mode_en, set CSR and ASR field enables for LV mode */
	{LHL_REG_OFF(lhl_lp_main_ctl_adr), BCM_MASK32(21, 0), 0x3F89FF},

	/* lhl_lp_main_ctl1_adr, set CSR field values - CSR_adj - 0.66V and trim_adj -5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl1_adr), BCM_MASK32(23, 0), 0x9E9F97},

	/* lhl_lp_main_ctl2_adr, set ASR field values - ASR_adj - 0.76V and trim_adj +5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl2_adr), BCM_MASK32(13, 0), 0x07EE},

	/* lhl_lp_dn_ctl_adr, set down count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl_adr), ~0, ((LHL4378_CSR_OVERI_DIS_DWN_CNT << 16) |
		(LHL4378_CSR_MODE_DWN_CNT << 8) | (LHL4378_CSR_ADJ_DWN_CNT << 0))},

	/* lhl_lp_up_ctl_adr, set up count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl_adr), ~0, ((LHL4378_CSR_OVERI_DIS_UP_CNT << 16) |
		(LHL4378_CSR_MODE_UP_CNT << 8) | (LHL4378_CSR_ADJ_UP_CNT << 0))},

	/* lhl_lp_dn_ctl1_adr, set down count for hpbg_chop_dis, ASR_adj, vddc_sw_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl1_adr), ~0, ((LHL4378_VDDC_SW_DIS_DWN_CNT << 24) |
		(LHL4378_ASR_ADJ_DWN_CNT << 16) | (LHL4378_HPBG_CHOP_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl1_adr, set up count for hpbg_chop_dis, ASR_adj, vddc_sw_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl1_adr), ~0, ((LHL4378_VDDC_SW_DIS_UP_CNT << 24) |
		(LHL4378_ASR_ADJ_UP_CNT << 16) | (LHL4378_HPBG_CHOP_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl4_adr, set down count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl4_adr), ~0, ((LHL4378_ASR_MANUAL_MODE_DWN_CNT << 24) |
		(LHL4378_ASR_MODE_SEL_DWN_CNT << 16) | (LHL4378_ASR_LPPFM_MODE_DWN_CNT << 8) |
		(LHL4378_ASR_CLK4M_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl4_adr, set up count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl4_adr), ~0, ((LHL4378_ASR_MANUAL_MODE_UP_CNT << 24) |
		(LHL4378_ASR_MODE_SEL_UP_CNT << 16)| (LHL4378_ASR_LPPFM_MODE_UP_CNT << 8) |
		(LHL4378_ASR_CLK4M_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl3_adr, set down count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl3_adr), ~0, ((LHL4378_PFM_PWR_SLICE_DWN_CNT << 24) |
		(LHL4378_ASR_OVERI_DIS_DWN_CNT << 16) | (LHL4378_SRBG_REF_SEL_DWN_CNT << 8) |
		(LHL4378_HPBG_PU_EN_DWN_CNT << 0))},

	/* lhl_lp_up_ctl3_adr, set up count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl3_adr), ~0, ((LHL4378_PFM_PWR_SLICE_UP_CNT << 24) |
		(LHL4378_ASR_OVERI_DIS_UP_CNT << 16) | (LHL4378_SRBG_REF_SEL_UP_CNT << 8) |
		(LHL4378_HPBG_PU_EN_UP_CNT << 0))},

	/* lhl_lp_dn_ctl2_adr, set down count for CSR_trim_adj */
	{LHL_REG_OFF(lhl_lp_dn_ctl2_adr), LHL4378_CSR_TRIM_ADJ_CNT_MASK,
		(LHL4378_CSR_TRIM_ADJ_DWN_CNT << LHL4378_CSR_TRIM_ADJ_CNT_SHIFT)},

	/* lhl_lp_up_ctl2_adr, set up count for CSR_trim_adj */
	{LHL_REG_OFF(lhl_lp_up_ctl2_adr), LHL4378_CSR_TRIM_ADJ_CNT_MASK,
		(LHL4378_CSR_TRIM_ADJ_UP_CNT << LHL4378_CSR_TRIM_ADJ_CNT_SHIFT)},

	/* lhl_lp_dn_ctl5_adr, set down count for ASR_trim_adj */
	{LHL_REG_OFF(lhl_lp_dn_ctl5_adr), ~0, (LHL4378_ASR_TRIM_ADJ_DWN_CNT << 0)},

	/* lhl_lp_up_ctl5_adr, set down count for ASR_trim_adj */
	{LHL_REG_OFF(lhl_lp_up_ctl5_adr), LHL4378_ASR_TRIM_ADJ_CNT_MASK,
		(LHL4378_ASR_TRIM_ADJ_UP_CNT << LHL4378_ASR_TRIM_ADJ_CNT_SHIFT)},

	/* Change the default down count values for the resources */
	/* lhl_top_pwrdn_ctl_adr, set down count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrdn_ctl_adr), ~0, ((LHL4378_PWRSW_EN_DWN_CNT << 24) |
		(LHL4378_SLB_EN_DWN_CNT << 16) | (LHL4378_ISO_EN_DWN_CNT << 8))},

	/* lhl_top_pwrdn2_ctl_adr, set down count for VMUX_asr_sel */
	{LHL_REG_OFF(lhl_top_pwrdn2_ctl_adr), ~0, (LHL4378_VMUX_ASR_SEL_DWN_CNT << 16)},

	/* Change the default up count values for the resources */
	/* lhl_top_pwrup_ctl_adr, set up count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrup_ctl_adr), ~0, ((LHL4378_PWRSW_EN_UP_CNT << 24) |
		(LHL4378_SLB_EN_UP_CNT << 16) | (LHL4378_ISO_EN_UP_CNT << 8))},

	/* lhl_top_pwrdn2_ctl_adr, set down count for VMUX_asr_sel */
	{LHL_REG_OFF(lhl_top_pwrup2_ctl_adr), ~0, ((LHL4378_VMUX_ASR_SEL_UP_CNT << 16))},

	/* Enable lhl interrupt */
	{LHL_REG_OFF(gci_intmask), (1 << 30), (1 << 30)},

	/* Enable LHL Wake up */
	{LHL_REG_OFF(gci_wakemask), (1 << 30), (1 << 30)},

	/* Making forceOTPpwrOn 1 */
	{LHL_REG_OFF(otpcontrol), (1 << 16), (1 << 16)}
};

lhl_reg_set_t BCMATTACHDATA(lv_sleep_mode_4387_lhl_reg_set)[] =
{
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr),
		LHL_TOP_PWRSEQ_SLEEP_ENAB_MASK |
		LHL_TOP_PWRSEQ_MISCLDO_PU_EN_MASK |
		LHL_TOP_PWRSEQ_SERDES_SLB_EN_MASK |
		LHL_TOP_PWRSEQ_SERDES_CLK_DIS_EN_MASK,
		LHL_TOP_PWRSEQ_SLEEP_ENAB_MASK |
		LHL_TOP_PWRSEQ_MISCLDO_PU_EN_MASK |
		LHL_TOP_PWRSEQ_SERDES_SLB_EN_MASK |
		LHL_TOP_PWRSEQ_SERDES_CLK_DIS_EN_MASK},

	/* lhl_lp_main_ctl_adr, disable lp_mode_en, set CSR and ASR field enables for LV mode */
	{LHL_REG_OFF(lhl_lp_main_ctl_adr), BCM_MASK32(21, 0), 0x3F89FF},

	/* lhl_lp_main_ctl1_adr, set CSR field values - CSR_adj - 0.64V and trim_adj -5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl1_adr), BCM_MASK32(23, 0), 0x9ED797},

	/* lhl_lp_main_ctl2_adr, set ASR field values - ASR_adj - 0.64V and trim_adj +5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl2_adr), BCM_MASK32(13, 0), 0x076D},

	/* lhl_lp_dn_ctl_adr, set down count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl_adr), ~0, ((LHL4378_CSR_OVERI_DIS_DWN_CNT << 16) |
		(LHL4378_CSR_MODE_DWN_CNT << 8) | (LHL4378_CSR_ADJ_DWN_CNT << 0))},

	/* lhl_lp_up_ctl_adr, set up count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl_adr), ~0, ((LHL4378_CSR_OVERI_DIS_UP_CNT << 16) |
		(LHL4378_CSR_MODE_UP_CNT << 8) | (LHL4378_CSR_ADJ_UP_CNT << 0))},

	/* lhl_lp_dn_ctl1_adr, set down count for hpbg_chop_dis, ASR_adj, vddc_sw_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl1_adr), ~0, ((LHL4378_VDDC_SW_DIS_DWN_CNT << 24) |
		(LHL4378_ASR_ADJ_DWN_CNT << 16) | (LHL4378_HPBG_CHOP_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl1_adr, set up count for hpbg_chop_dis, ASR_adj, vddc_sw_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl1_adr), ~0, ((LHL4378_VDDC_SW_DIS_UP_CNT << 24) |
		(LHL4378_ASR_ADJ_UP_CNT << 16) | (LHL4378_HPBG_CHOP_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl4_adr, set down count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl4_adr), ~0, ((LHL4378_ASR_MANUAL_MODE_DWN_CNT << 24) |
		(LHL4378_ASR_MODE_SEL_DWN_CNT << 16) | (LHL4378_ASR_LPPFM_MODE_DWN_CNT << 8) |
		(LHL4378_ASR_CLK4M_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl4_adr, set up count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl4_adr), ~0, ((LHL4378_ASR_MANUAL_MODE_UP_CNT << 24) |
		(LHL4378_ASR_MODE_SEL_UP_CNT << 16)| (LHL4378_ASR_LPPFM_MODE_UP_CNT << 8) |
		(LHL4378_ASR_CLK4M_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl3_adr, set down count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl3_adr), ~0, ((LHL4378_PFM_PWR_SLICE_DWN_CNT << 24) |
		(LHL4378_ASR_OVERI_DIS_DWN_CNT << 16) | (LHL4378_SRBG_REF_SEL_DWN_CNT << 8) |
		(LHL4378_HPBG_PU_EN_DWN_CNT << 0))},

	/* lhl_lp_up_ctl3_adr, set up count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl3_adr), ~0, ((LHL4378_PFM_PWR_SLICE_UP_CNT << 24) |
		(LHL4378_ASR_OVERI_DIS_UP_CNT << 16) | (LHL4378_SRBG_REF_SEL_UP_CNT << 8) |
		(LHL4378_HPBG_PU_EN_UP_CNT << 0))},

	/* lhl_lp_dn_ctl2_adr, set down count for CSR_trim_adj */
	{LHL_REG_OFF(lhl_lp_dn_ctl2_adr), LHL4378_CSR_TRIM_ADJ_CNT_MASK,
		(LHL4378_CSR_TRIM_ADJ_DWN_CNT << LHL4378_CSR_TRIM_ADJ_CNT_SHIFT)},

	/* lhl_lp_up_ctl2_adr, set up count for CSR_trim_adj */
	{LHL_REG_OFF(lhl_lp_up_ctl2_adr), LHL4378_CSR_TRIM_ADJ_CNT_MASK,
		(LHL4378_CSR_TRIM_ADJ_UP_CNT << LHL4378_CSR_TRIM_ADJ_CNT_SHIFT)},

	/* lhl_lp_dn_ctl5_adr, set down count for ASR_trim_adj */
	{LHL_REG_OFF(lhl_lp_dn_ctl5_adr), LHL4378_ASR_TRIM_ADJ_CNT_MASK,
		(LHL4378_ASR_TRIM_ADJ_DWN_CNT << LHL4378_ASR_TRIM_ADJ_CNT_SHIFT)},

	/* lhl_lp_up_ctl5_adr, set down count for ASR_trim_adj */
	{LHL_REG_OFF(lhl_lp_up_ctl5_adr), LHL4378_ASR_TRIM_ADJ_CNT_MASK,
		(LHL4378_ASR_TRIM_ADJ_UP_CNT << LHL4378_ASR_TRIM_ADJ_CNT_SHIFT)},

	/* Change the default down count values for the resources */
	/* lhl_top_pwrdn_ctl_adr, set down count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrdn_ctl_adr), ~0, ((LHL4378_PWRSW_EN_DWN_CNT << 24) |
		(LHL4378_SLB_EN_DWN_CNT << 16) | (LHL4378_ISO_EN_DWN_CNT << 8))},

	/* lhl_top_pwrdn2_ctl_adr, set down count for VMUX_asr_sel */
	{LHL_REG_OFF(lhl_top_pwrdn2_ctl_adr), ~0, (LHL4387_VMUX_ASR_SEL_DWN_CNT << 16)},

	/* Change the default up count values for the resources */
	/* lhl_top_pwrup_ctl_adr, set up count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrup_ctl_adr), ~0, ((LHL4378_PWRSW_EN_UP_CNT << 24) |
		(LHL4378_SLB_EN_UP_CNT << 16) | (LHL4378_ISO_EN_UP_CNT << 8))},

	/* lhl_top_pwrdn2_ctl_adr, set down count for VMUX_asr_sel */
	{LHL_REG_OFF(lhl_top_pwrup2_ctl_adr), ~0, ((LHL4387_VMUX_ASR_SEL_UP_CNT << 16))},

	/* Enable lhl interrupt */
	{LHL_REG_OFF(gci_intmask), (1 << 30), (1 << 30)},

	/* Enable LHL Wake up */
	{LHL_REG_OFF(gci_wakemask), (1 << 30), (1 << 30)},

	/* Making forceOTPpwrOn 1 */
	{LHL_REG_OFF(otpcontrol), (1 << 16), (1 << 16)},

	/* serdes_clk_dis dn=2, miscldo_pu dn=6; Also include CRWLLHL-48 WAR set bit31 */
	{LHL_REG_OFF(lhl_top_pwrdn3_ctl_adr), ~0, 0x80040c02},

	/* serdes_clk_dis dn=11, miscldo_pu dn=0 */
	{LHL_REG_OFF(lhl_top_pwrup3_ctl_adr), ~0, 0x00160010}
};

lhl_reg_set_t BCMATTACHDATA(lv_sleep_mode_4387_lhl_reg_set_top_off)[] =
{
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr),
		LHL_TOP_PWRSEQ_SLEEP_ENAB_MASK |
		LHL_TOP_PWRSEQ_TOP_ISO_EN_MASK |
		LHL_TOP_PWRSEQ_TOP_SLB_EN_MASK |
		LHL_TOP_PWRSEQ_TOP_PWRSW_EN_MASK |
		LHL_TOP_PWRSEQ_MISCLDO_PU_EN_MASK,
		LHL_TOP_PWRSEQ_SLEEP_ENAB_MASK |
		LHL_TOP_PWRSEQ_TOP_ISO_EN_MASK |
		LHL_TOP_PWRSEQ_TOP_SLB_EN_MASK |
		LHL_TOP_PWRSEQ_TOP_PWRSW_EN_MASK |
		LHL_TOP_PWRSEQ_MISCLDO_PU_EN_MASK},

	/* lhl_lp_main_ctl_adr, disable lp_mode_en, set CSR and ASR field enables for LV mode */
	{LHL_REG_OFF(lhl_lp_main_ctl_adr), BCM_MASK32(21, 0), 0x3F87DB},

	/* lhl_lp_main_ctl1_adr, set CSR field values - CSR_adj - 0.64V and trim_adj -5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl1_adr), BCM_MASK32(23, 0), 0x9ED7B7},

	/* lhl_lp_main_ctl2_adr, set ASR field values - ASR_adj - 0.64V and trim_adj +5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl2_adr), BCM_MASK32(13, 0), 0x076D},

	/* lhl_lp_dn_ctl_adr, set down count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl_adr), ~0, ((LHL4387_TO_CSR_OVERI_DIS_DWN_CNT << 16) |
		(LHL4387_TO_CSR_MODE_DWN_CNT << 8) | (LHL4387_TO_CSR_ADJ_DWN_CNT << 0))},

	/* lhl_lp_up_ctl_adr, set up count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl_adr), ~0, ((LHL4387_TO_CSR_OVERI_DIS_UP_CNT << 16) |
		(LHL4387_TO_CSR_MODE_UP_CNT << 8) | (LHL4387_TO_CSR_ADJ_UP_CNT << 0))},

	/* lhl_lp_dn_ctl1_adr, set down count for hpbg_chop_dis, lp_mode_dn_cnt,
	 * ASR_adj, vddc_sw_dis
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl1_adr), ~0, ((LHL4387_TO_VDDC_SW_DIS_DWN_CNT << 24) |
		(LHL4387_TO_ASR_ADJ_DWN_CNT << 16) | (LHL4387_TO_LP_MODE_DWN_CNT << 8) |
		(LHL4387_TO_HPBG_CHOP_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl1_adr, set up count for hpbg_chop_dis, lp_mode_dn_cnt,
	 * ASR_adj, vddc_sw_dis
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl1_adr), ~0, ((LHL4387_TO_VDDC_SW_DIS_UP_CNT << 24) |
		(LHL4387_TO_ASR_ADJ_UP_CNT << 16) | (LHL4387_TO_LP_MODE_UP_CNT << 8) |
		(LHL4387_TO_HPBG_CHOP_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl4_adr, set down count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl4_adr), ~0, ((LHL4387_TO_ASR_MANUAL_MODE_DWN_CNT << 24) |
		(LHL4387_TO_ASR_MODE_SEL_DWN_CNT << 16) | (LHL4387_TO_ASR_LPPFM_MODE_DWN_CNT << 8) |
		(LHL4387_TO_ASR_CLK4M_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl4_adr, set up count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl4_adr), ~0, ((LHL4387_TO_ASR_MANUAL_MODE_UP_CNT << 24) |
		(LHL4387_TO_ASR_MODE_SEL_UP_CNT << 16)| (LHL4387_TO_ASR_LPPFM_MODE_UP_CNT << 8) |
		(LHL4387_TO_ASR_CLK4M_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl3_adr, set down count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl3_adr), ~0, ((LHL4387_TO_PFM_PWR_SLICE_DWN_CNT << 24) |
		(LHL4387_TO_ASR_OVERI_DIS_DWN_CNT << 16) | (LHL4387_TO_SRBG_REF_SEL_DWN_CNT << 8) |
		(LHL4387_TO_HPBG_PU_EN_DWN_CNT << 0))},

	/* lhl_lp_up_ctl3_adr, set up count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl3_adr), ~0, ((LHL4387_TO_PFM_PWR_SLICE_UP_CNT << 24) |
		(LHL4387_TO_ASR_OVERI_DIS_UP_CNT << 16) | (LHL4387_TO_SRBG_REF_SEL_UP_CNT << 8) |
		(LHL4387_TO_HPBG_PU_EN_UP_CNT << 0))},

	/* ASR_trim_adj downcount=0x3, [30:24] is default value for spmi_*io_sel */
	{LHL_REG_OFF(lhl_lp_dn_ctl5_adr), LHL4378_ASR_TRIM_ADJ_CNT_MASK, 0x3},

	/* ASR_trim_adj upcount=0x1, [30:24] is default value for spmi_*io_sel */
	{LHL_REG_OFF(lhl_lp_up_ctl5_adr), LHL4378_ASR_TRIM_ADJ_CNT_MASK, 0x1},

	/* Change the default down count values for the resources */
	/* lhl_top_pwrdn_ctl_adr, set down count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrdn_ctl_adr), ~0, ((LHL4387_TO_PWRSW_EN_DWN_CNT << 24) |
		(LHL4387_TO_SLB_EN_DWN_CNT << 16) | (LHL4387_TO_ISO_EN_DWN_CNT << 8) |
		(LHL4387_TO_TOP_SLP_EN_DWN_CNT))},

	/* Change the default up count values for the resources */
	/* lhl_top_pwrup_ctl_adr, set up count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrup_ctl_adr), ~0, ((LHL4387_TO_PWRSW_EN_UP_CNT << 24) |
		(LHL4387_TO_SLB_EN_UP_CNT << 16) | (LHL4387_TO_ISO_EN_UP_CNT << 8) |
		(LHL4387_TO_TOP_SLP_EN_UP_CNT))},

	/* lhl_top_pwrup2_ctl, serdes_slb_en_up_cnt=0x7 */
	{LHL_REG_OFF(lhl_top_pwrup2_ctl_adr), LHL4378_CSR_TRIM_ADJ_CNT_MASK, 0xe0000},

	/* lhl_top_pwrdn2_ctl, serdes_slb_en_dn_cnt=0x2 */
	{LHL_REG_OFF(lhl_top_pwrdn2_ctl_adr), LHL4378_CSR_TRIM_ADJ_CNT_MASK, 0x40000},

	/* Enable lhl interrupt */
	{LHL_REG_OFF(gci_intmask), (1 << 30), (1 << 30)},

	/* Enable LHL Wake up */
	{LHL_REG_OFF(gci_wakemask), (1 << 30), (1 << 30)},

	/* Making forceOTPpwrOn 1 */
	{LHL_REG_OFF(otpcontrol), (1 << 16), (1 << 16)},

	/* lhl_top_pwrup3_ctl, FLL pu power up count=0x8, miscldo pu power up count=0x0,
	 * serdes_clk_dis up count=0x7
	 */
	{LHL_REG_OFF(lhl_top_pwrup3_ctl_adr), ~0, 0xe0010},

	/* lhl_top_pwrdn3_ctl, FLL pu power up count=0x1,miscldo pu power up count=0x3,
	 * serdes_clk_dis up count=0x1
	 */
	{LHL_REG_OFF(lhl_top_pwrdn3_ctl_adr), ~0, 0x20602}
};

lhl_reg_set_t BCMATTACHDATA(lv_sleep_mode_4389_lhl_reg_set)[] =
{
	/* set wl_sleep_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), (1 << 0), (1 << 0)},

	/* set top_pwrsw_en, top_slb_en, top_iso_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), BCM_MASK32(5, 3), (0x0 << 3)},

	/* set VMUX_asr_sel_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), (1 << 8), (1 << 8)},

	/* lhl_lp_main_ctl_adr, disable lp_mode_en, set CSR and ASR field enables for LV mode */
	{LHL_REG_OFF(lhl_lp_main_ctl_adr), BCM_MASK32(21, 0), 0x3F89FF},

	/* lhl_lp_main_ctl1_adr, set CSR field values - CSR_adj - 0.64V and trim_adj -5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl1_adr), BCM_MASK32(23, 0), 0x9EDF97},

	/* lhl_lp_main_ctl2_adr, set ASR field values - ASR_adj - 0.64V and trim_adj +5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl2_adr), BCM_MASK32(13, 0), 0x07ED},

	/* lhl_lp_dn_ctl_adr, set down count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl_adr), ~0, ((LHL4378_CSR_OVERI_DIS_DWN_CNT << 16) |
		(LHL4378_CSR_MODE_DWN_CNT << 8) | (LHL4378_CSR_ADJ_DWN_CNT << 0))},

	/* lhl_lp_up_ctl_adr, set up count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl_adr), ~0, ((LHL4378_CSR_OVERI_DIS_UP_CNT << 16) |
		(LHL4378_CSR_MODE_UP_CNT << 8) | (LHL4378_CSR_ADJ_UP_CNT << 0))},

	/* lhl_lp_dn_ctl1_adr, set down count for hpbg_chop_dis, ASR_adj, vddc_sw_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl1_adr), ~0, ((LHL4378_VDDC_SW_DIS_DWN_CNT << 24) |
		(LHL4378_ASR_ADJ_DWN_CNT << 16) | (LHL4378_HPBG_CHOP_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl1_adr, set up count for hpbg_chop_dis, ASR_adj, vddc_sw_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl1_adr), ~0, ((LHL4378_VDDC_SW_DIS_UP_CNT << 24) |
		(LHL4378_ASR_ADJ_UP_CNT << 16) | (LHL4378_HPBG_CHOP_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl4_adr, set down count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl4_adr), ~0, ((LHL4378_ASR_MANUAL_MODE_DWN_CNT << 24) |
		(LHL4378_ASR_MODE_SEL_DWN_CNT << 16) | (LHL4378_ASR_LPPFM_MODE_DWN_CNT << 8) |
		(LHL4378_ASR_CLK4M_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl4_adr, set up count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl4_adr), ~0, ((LHL4378_ASR_MANUAL_MODE_UP_CNT << 24) |
		(LHL4378_ASR_MODE_SEL_UP_CNT << 16)| (LHL4378_ASR_LPPFM_MODE_UP_CNT << 8) |
		(LHL4378_ASR_CLK4M_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl3_adr, set down count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl3_adr), ~0, ((LHL4378_PFM_PWR_SLICE_DWN_CNT << 24) |
		(LHL4378_ASR_OVERI_DIS_DWN_CNT << 16) | (LHL4378_SRBG_REF_SEL_DWN_CNT << 8) |
		(LHL4378_HPBG_PU_EN_DWN_CNT << 0))},

	/* lhl_lp_up_ctl3_adr, set up count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl3_adr), ~0, ((LHL4378_PFM_PWR_SLICE_UP_CNT << 24) |
		(LHL4378_ASR_OVERI_DIS_UP_CNT << 16) | (LHL4378_SRBG_REF_SEL_UP_CNT << 8) |
		(LHL4378_HPBG_PU_EN_UP_CNT << 0))},

	/* lhl_lp_dn_ctl2_adr, set down count for CSR_trim_adj */
	{LHL_REG_OFF(lhl_lp_dn_ctl2_adr), ~0, (LHL4378_CSR_TRIM_ADJ_DWN_CNT << 16)},

	/* lhl_lp_up_ctl2_adr, set up count for CSR_trim_adj */
	{LHL_REG_OFF(lhl_lp_up_ctl2_adr), ~0, (LHL4378_CSR_TRIM_ADJ_UP_CNT << 16)},

	/* lhl_lp_dn_ctl5_adr, set down count for ASR_trim_adj */
	{LHL_REG_OFF(lhl_lp_dn_ctl5_adr), LHL4378_ASR_TRIM_ADJ_CNT_MASK,
		(LHL4378_ASR_TRIM_ADJ_DWN_CNT << LHL4378_ASR_TRIM_ADJ_CNT_SHIFT)},

	/* lhl_lp_up_ctl5_adr, set down count for ASR_trim_adj */
	{LHL_REG_OFF(lhl_lp_up_ctl5_adr), LHL4378_ASR_TRIM_ADJ_CNT_MASK,
		(LHL4378_ASR_TRIM_ADJ_UP_CNT << LHL4378_ASR_TRIM_ADJ_CNT_SHIFT)},

	/* Change the default down count values for the resources */
	/* lhl_top_pwrdn_ctl_adr, set down count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrdn_ctl_adr), ~0, ((LHL4378_PWRSW_EN_DWN_CNT << 24) |
		(LHL4378_SLB_EN_DWN_CNT << 16) | (LHL4378_ISO_EN_DWN_CNT << 8))},

	/* lhl_top_pwrdn2_ctl_adr, set down count for VMUX_asr_sel */
	{LHL_REG_OFF(lhl_top_pwrdn2_ctl_adr), ~0, (LHL4387_VMUX_ASR_SEL_DWN_CNT << 16)},

	/* Change the default up count values for the resources */
	/* lhl_top_pwrup_ctl_adr, set up count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrup_ctl_adr), ~0, ((LHL4378_PWRSW_EN_UP_CNT << 24) |
		(LHL4378_SLB_EN_UP_CNT << 16) | (LHL4378_ISO_EN_UP_CNT << 8))},

	/* lhl_top_pwrdn2_ctl_adr, set down count for VMUX_asr_sel */
	{LHL_REG_OFF(lhl_top_pwrup2_ctl_adr), ~0, ((LHL4387_VMUX_ASR_SEL_UP_CNT << 16))},

	/* Enable lhl interrupt */
	{LHL_REG_OFF(gci_intmask), (1 << 30), (1 << 30)},

	/* Enable LHL Wake up */
	{LHL_REG_OFF(gci_wakemask), (1 << 30), (1 << 30)},

	/* Making forceOTPpwrOn 1 */
	{LHL_REG_OFF(otpcontrol), (1 << 16), (1 << 16)},

	/* serdes_clk_dis dn=2, miscldo_pu dn=6; Also include CRWLLHL-48 WAR set bit31 */
	{LHL_REG_OFF(lhl_top_pwrdn3_ctl_adr), ~0, 0x80040c02},

	/* serdes_clk_dis dn=11, miscldo_pu dn=0 */
	{LHL_REG_OFF(lhl_top_pwrup3_ctl_adr), ~0, 0x00160010}
};

/* LV sleep mode summary:
 * LV mode is where both ABUCK and CBUCK are programmed to low voltages during
 * sleep, and VMUX selects ABUCK as VDDOUT_AON. LPLDO needs to power off.
 * With ASR ON, LPLDO OFF
 */
void
BCMATTACHFN(si_set_lv_sleep_mode_lhl_config_4369)(si_t *sih)
{
	uint i;
	uint coreidx = si_findcoreidx(sih, GCI_CORE_ID, 0);
	lhl_reg_set_t *regs = lv_sleep_mode_4369_lhl_reg_set;

	/* Enable LHL LV mode:
	 * lhl_top_pwrseq_ctl_adr, set wl_sleep_en, iso_en, slb_en, pwrsw_en,VMUX_asr_sel_en
	 */
	for (i = 0; i < ARRAYSIZE(lv_sleep_mode_4369_lhl_reg_set); i++) {
		si_corereg(sih, coreidx, regs[i].offset, regs[i].mask, regs[i].val);
	}
	if (getintvar(NULL, rstr_rfldo3p3_cap_war)) {
		si_corereg(sih, coreidx, LHL_REG_OFF(lhl_lp_main_ctl1_adr),
				BCM_MASK32(23, 0), 0x9E9F9F);
	}
}

void
BCMATTACHFN(si_set_lv_sleep_mode_lhl_config_4378)(si_t *sih)
{
	uint i;
	uint coreidx = si_findcoreidx(sih, GCI_CORE_ID, 0);
	lhl_reg_set_t *regs = lv_sleep_mode_4378_lhl_reg_set;

	/* Enable LHL LV mode:
	 * lhl_top_pwrseq_ctl_adr, set wl_sleep_en, iso_en, slb_en, pwrsw_en,VMUX_asr_sel_en
	 */
	for (i = 0; i < ARRAYSIZE(lv_sleep_mode_4378_lhl_reg_set); i++) {
		si_corereg(sih, coreidx, regs[i].offset, regs[i].mask, regs[i].val);
	}
}

void
BCMATTACHFN(si_set_lv_sleep_mode_lhl_config_4387)(si_t *sih)
{
	uint i;
	uint coreidx = si_findcoreidx(sih, GCI_CORE_ID, 0);
	lhl_reg_set_t *regs;
	uint32 abuck_volt_sleep, cbuck_volt_sleep;
	uint regs_size;

	if (BCMSRTOPOFF_ENAB()) {
		regs = lv_sleep_mode_4387_lhl_reg_set_top_off;
		regs_size = ARRAYSIZE(lv_sleep_mode_4387_lhl_reg_set_top_off);
	} else {
		/* Enable LHL LV mode:
		 * lhl_top_pwrseq_ctl_adr, set wl_sleep_en, iso_en, slb_en, pwrsw_en,VMUX_asr_sel_en
		 */
		regs = lv_sleep_mode_4387_lhl_reg_set;
		regs_size = ARRAYSIZE(lv_sleep_mode_4387_lhl_reg_set);
	}

	for (i = 0; i < regs_size; i++) {
		si_corereg(sih, coreidx, regs[i].offset, regs[i].mask, regs[i].val);
	}

	if (getvar(NULL, rstr_cbuck_volt_sleep) != NULL) {
		cbuck_volt_sleep = getintvar(NULL, rstr_cbuck_volt_sleep);
		LHL_REG(sih, lhl_lp_main_ctl1_adr, LHL_CBUCK_VOLT_SLEEP_MASK,
			(cbuck_volt_sleep << LHL_CBUCK_VOLT_SLEEP_SHIFT));
	}

	if (getvar(NULL, rstr_abuck_volt_sleep) != NULL) {
		abuck_volt_sleep = getintvar(NULL, rstr_abuck_volt_sleep);
		LHL_REG(sih, lhl_lp_main_ctl2_adr, LHL_ABUCK_VOLT_SLEEP_MASK,
			(abuck_volt_sleep << LHL_ABUCK_VOLT_SLEEP_SHIFT));
	}

	if (BCMSRTOPOFF_ENAB()) {
		/* Serdes AFE retention control enable */
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_05,
			CC_GCI_05_4387C0_AFE_RET_ENB_MASK,
			CC_GCI_05_4387C0_AFE_RET_ENB_MASK);
	}
}

void
BCMATTACHFN(si_set_lv_sleep_mode_lhl_config_4389)(si_t *sih)
{
	uint i;
	uint coreidx = si_findcoreidx(sih, GCI_CORE_ID, 0);
	lhl_reg_set_t *regs = lv_sleep_mode_4389_lhl_reg_set;
	uint32 abuck_volt_sleep, cbuck_volt_sleep;

	/* Enable LHL LV mode:
	 * lhl_top_pwrseq_ctl_adr, set wl_sleep_en, iso_en, slb_en, pwrsw_en,VMUX_asr_sel_en
	 */
	for (i = 0; i < ARRAYSIZE(lv_sleep_mode_4389_lhl_reg_set); i++) {
		si_corereg(sih, coreidx, regs[i].offset, regs[i].mask, regs[i].val);
	}

	if (getvar(NULL, rstr_cbuck_volt_sleep) != NULL) {
		cbuck_volt_sleep = getintvar(NULL, rstr_cbuck_volt_sleep);
		LHL_REG(sih, lhl_lp_main_ctl1_adr, LHL_CBUCK_VOLT_SLEEP_MASK,
			(cbuck_volt_sleep << LHL_CBUCK_VOLT_SLEEP_SHIFT));
	}

	if (getvar(NULL, rstr_abuck_volt_sleep) != NULL) {
		abuck_volt_sleep = getintvar(NULL, rstr_abuck_volt_sleep);
		LHL_REG(sih, lhl_lp_main_ctl2_adr, LHL_ABUCK_VOLT_SLEEP_MASK,
			(abuck_volt_sleep << LHL_ABUCK_VOLT_SLEEP_SHIFT));
	}

	OSL_DELAY(100);
	LHL_REG(sih, lhl_top_pwrseq_ctl_adr, ~0, 0x00000101);

	/* Clear Misc_LDO override */
	si_pmu_vreg_control(sih, PMU_VREG_5, VREG5_4387_MISCLDO_PU_MASK, 0);
}

lhl_reg_set_t BCMATTACHDATA(lv_sleep_mode_4362_lhl_reg_set)[] =
{
	/* set wl_sleep_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), (1 << 0), (1 << 0)},

	/* set top_pwrsw_en, top_slb_en, top_iso_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), BCM_MASK32(5, 3), (0x0 << 3)},

	/* set VMUX_asr_sel_en */
	{LHL_REG_OFF(lhl_top_pwrseq_ctl_adr), (1 << 8), (1 << 8)},

	/* lhl_lp_main_ctl_adr, disable lp_mode_en, set CSR and ASR field enables for LV mode */
	{LHL_REG_OFF(lhl_lp_main_ctl_adr), BCM_MASK32(21, 0), 0x3F89FF},

	/* lhl_lp_main_ctl1_adr, set CSR field values - CSR_adj - 0.66V and trim_adj -5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl1_adr), BCM_MASK32(23, 0), 0x9E9F97},

	/* lhl_lp_main_ctl2_adr, set ASR field values - ASR_adj - 0.76V and trim_adj +5mV */
	{LHL_REG_OFF(lhl_lp_main_ctl2_adr), BCM_MASK32(13, 0), 0x07EE},

	/* lhl_lp_dn_ctl_adr, set down count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl_adr), ~0, ((LHL4362_CSR_OVERI_DIS_DWN_CNT << 16) |
		(LHL4362_CSR_MODE_DWN_CNT << 8) | (LHL4362_CSR_ADJ_DWN_CNT << 0))},

	/* lhl_lp_up_ctl_adr, set up count for CSR fields- adj, mode, overi_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl_adr), ~0, ((LHL4362_CSR_OVERI_DIS_UP_CNT << 16) |
		(LHL4362_CSR_MODE_UP_CNT << 8) | (LHL4362_CSR_ADJ_UP_CNT << 0))},

	/* lhl_lp_dn_ctl1_adr, set down count for hpbg_chop_dis, ASR_adj, vddc_sw_dis */
	{LHL_REG_OFF(lhl_lp_dn_ctl1_adr), ~0, ((LHL4362_VDDC_SW_DIS_DWN_CNT << 24) |
		(LHL4362_ASR_ADJ_DWN_CNT << 16) | (LHL4362_HPBG_CHOP_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl1_adr, set up count for hpbg_chop_dis, ASR_adj, vddc_sw_dis */
	{LHL_REG_OFF(lhl_lp_up_ctl1_adr), ~0, ((LHL4362_VDDC_SW_DIS_UP_CNT << 24) |
		(LHL4362_ASR_ADJ_UP_CNT << 16) | (LHL4362_HPBG_CHOP_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl4_adr, set down count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl4_adr), ~0, ((LHL4362_ASR_MANUAL_MODE_DWN_CNT << 24) |
		(LHL4362_ASR_MODE_SEL_DWN_CNT << 16) | (LHL4362_ASR_LPPFM_MODE_DWN_CNT << 8) |
		(LHL4362_ASR_CLK4M_DIS_DWN_CNT << 0))},

	/* lhl_lp_up_ctl4_adr, set up count for ASR fields -
	 *     clk4m_dis, lppfm_mode, mode_sel, manual_mode
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl4_adr), ~0, ((LHL4362_ASR_MANUAL_MODE_UP_CNT << 24) |
		(LHL4362_ASR_MODE_SEL_UP_CNT << 16)| (LHL4362_ASR_LPPFM_MODE_UP_CNT << 8) |
		(LHL4362_ASR_CLK4M_DIS_UP_CNT << 0))},

	/* lhl_lp_dn_ctl3_adr, set down count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_dn_ctl3_adr), ~0, ((LHL4362_PFM_PWR_SLICE_DWN_CNT << 24) |
		(LHL4362_ASR_OVERI_DIS_DWN_CNT << 16) | (LHL4362_SRBG_REF_SEL_DWN_CNT << 8) |
		(LHL4362_HPBG_PU_EN_DWN_CNT << 0))},

	/* lhl_lp_up_ctl3_adr, set up count for hpbg_pu, srbg_ref, ASR_overi_dis,
	 * CSR_pfm_pwr_slice_en
	 */
	{LHL_REG_OFF(lhl_lp_up_ctl3_adr), ~0, ((LHL4362_PFM_PWR_SLICE_UP_CNT << 24) |
		(LHL4362_ASR_OVERI_DIS_UP_CNT << 16) | (LHL4362_SRBG_REF_SEL_UP_CNT << 8) |
		(LHL4362_HPBG_PU_EN_UP_CNT << 0))},

	/* lhl_lp_dn_ctl2_adr, set down count for CSR_trim_adj */
	{LHL_REG_OFF(lhl_lp_dn_ctl2_adr), ~0, (LHL4362_CSR_TRIM_ADJ_DWN_CNT << 16)},

	/* lhl_lp_up_ctl2_adr, set up count for CSR_trim_adj */
	{LHL_REG_OFF(lhl_lp_up_ctl2_adr), ~0, (LHL4362_CSR_TRIM_ADJ_UP_CNT << 16)},

	/* lhl_lp_dn_ctl5_adr, set down count for ASR_trim_adj */
	{LHL_REG_OFF(lhl_lp_dn_ctl5_adr), ~0, (LHL4362_ASR_TRIM_ADJ_DWN_CNT << 0)},

	/* lhl_lp_up_ctl5_adr, set down count for ASR_trim_adj */
	{LHL_REG_OFF(lhl_lp_up_ctl5_adr), ~0, (LHL4362_ASR_TRIM_ADJ_UP_CNT << 0)},

	/* Change the default down count values for the resources */
	/* lhl_top_pwrdn_ctl_adr, set down count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrdn_ctl_adr), ~0, ((LHL4362_PWRSW_EN_DWN_CNT << 24) |
		(LHL4362_SLB_EN_DWN_CNT << 16) | (LHL4362_ISO_EN_DWN_CNT << 8))},

	/* lhl_top_pwrdn2_ctl_adr, set down count for VMUX_asr_sel */
	{LHL_REG_OFF(lhl_top_pwrdn2_ctl_adr), ~0, (LHL4362_VMUX_ASR_SEL_DWN_CNT << 16)},

	/* Change the default up count values for the resources */
	/* lhl_top_pwrup_ctl_adr, set up count for top_level_sleep, iso, slb and pwrsw */
	{LHL_REG_OFF(lhl_top_pwrup_ctl_adr), ~0, ((LHL4362_PWRSW_EN_UP_CNT << 24) |
		(LHL4362_SLB_EN_UP_CNT << 16) | (LHL4362_ISO_EN_UP_CNT << 8))},

	/* lhl_top_pwrdn2_ctl_adr, set down count for VMUX_asr_sel */
	{LHL_REG_OFF(lhl_top_pwrup2_ctl_adr), ~0, ((LHL4362_VMUX_ASR_SEL_UP_CNT << 16))},

	/* Enable lhl interrupt */
	{LHL_REG_OFF(gci_intmask), (1 << 30), (1 << 30)},

	/* Enable LHL Wake up */
	{LHL_REG_OFF(gci_wakemask), (1 << 30), (1 << 30)},

	/* Making forceOTPpwrOn 1 */
	{LHL_REG_OFF(otpcontrol), (1 << 16), (1 << 16)}
};

/* LV sleep mode summary:
 * LV mode is where both ABUCK and CBUCK are programmed to low voltages during
 * sleep, and VMUX selects ABUCK as VDDOUT_AON. LPLDO needs to power off.
 * With ASR ON, LPLDO OFF
 */
void
BCMATTACHFN(si_set_lv_sleep_mode_lhl_config_4362)(si_t *sih)
{
	uint i;
	uint coreidx = si_findcoreidx(sih, GCI_CORE_ID, 0);
	lhl_reg_set_t *regs = lv_sleep_mode_4362_lhl_reg_set;

	/* Enable LHL LV mode:
	 * lhl_top_pwrseq_ctl_adr, set wl_sleep_en, iso_en, slb_en, pwrsw_en,VMUX_asr_sel_en
	 */
	for (i = 0; i < ARRAYSIZE(lv_sleep_mode_4362_lhl_reg_set); i++) {
		si_corereg(sih, coreidx, regs[i].offset, regs[i].mask, regs[i].val);
	}
}

void
si_lhl_mactim0_set(si_t *sih, uint32 val)
{
	LHL_REG(sih, lhl_wl_mactim_int0_adr, LHL_WL_MACTIMER_MASK, val);
}
