/*
 * Misc utility routines for accessing lhl specific features
 * of the SiliconBackplane-based Broadcom chips.
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: hndpmu.c 547757 2015-04-13 10:18:04Z $
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
#ifdef BCMULP
#include <ulp.h>
#endif // endif

#define SI_LHL_EXT_WAKE_REQ_MASK_MAGIC		0x7FBBF7FF	/* magic number for LHL EXT */

/* PmuRev1 has a 24-bit PMU RsrcReq timer. However it pushes all other bits
 * upward. To make the code to run for all revs we use a variable to tell how
 * many bits we need to shift.
 */
#define FLAGS_SHIFT	14
#define	LHL_ERROR(args) printf args

void
si_lhl_setup(si_t *sih, osl_t *osh)
{
	if (CHIPID(sih->chip) == BCM43012_CHIP_ID) {
		/* Enable PMU sleep mode0 */
#ifdef BCMQT
		LHL_REG(sih, lhl_top_pwrseq_ctl_adr, LHL_PWRSEQ_CTL, PMU_SLEEP_MODE_0);
#else
		LHL_REG(sih, lhl_top_pwrseq_ctl_adr, LHL_PWRSEQ_CTL, PMU_SLEEP_MODE_2);
#endif // endif
		/* Modify as per the
		BCM43012/LHL#LHL-RecommendedsettingforvariousPMUSleepModes:
		*/
		LHL_REG(sih, lhl_top_pwrup_ctl_adr, LHL_PWRUP_CTL_MASK, LHL_PWRUP_CTL);
		LHL_REG(sih, lhl_top_pwrup2_ctl_adr, LHL_PWRUP2_CTL_MASK, LHL_PWRUP2_CTL);
		LHL_REG(sih, lhl_top_pwrdn_ctl_adr, LHL_PWRDN_CTL_MASK, LHL_PWRDN_SLEEP_CNT);
		LHL_REG(sih, lhl_top_pwrdn2_ctl_adr, LHL_PWRDN2_CTL_MASK, LHL_PWRDN2_CTL);
	} else if (BCM4347_CHIP(sih->chip)) {
		if (LHL_IS_PSMODE_1(sih)) {
			LHL_REG(sih, lhl_top_pwrseq_ctl_adr, LHL_PWRSEQ_CTL, PMU_SLEEP_MODE_1);
		} else {
			LHL_REG(sih, lhl_top_pwrseq_ctl_adr, LHL_PWRSEQ_CTL, PMU_SLEEP_MODE_0);
		}

		LHL_REG(sih, lhl_top_pwrup_ctl_adr, LHL_PWRUP_CTL_MASK, LHL_PWRUP_CTL_4347);
		LHL_REG(sih, lhl_top_pwrup2_ctl_adr, LHL_PWRUP2_CTL_MASK, LHL_PWRUP2_CTL);
		LHL_REG(sih, lhl_top_pwrdn_ctl_adr,
			LHL_PWRDN_CTL_MASK, LHL_PWRDN_SLEEP_CNT);
		LHL_REG(sih, lhl_top_pwrdn2_ctl_adr, LHL_PWRDN2_CTL_MASK, LHL_PWRDN2_CTL);

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
#if !defined(_CFEZ_)
		si_gci_set_functionsel(sih, 1, CC4347_FNSEL_SAMEASPIN);
#endif // endif

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
			(1 << GCI_GPIO_STS_EDGE_TRIG_BIT |
			1 << GCI_GPIO_STS_NEG_EDGE_TRIG_BIT |
			1 << GCI_GPIO_STS_WL_DIN_SELECT));
		LHL_REG(sih, gpio_int_en_port_adr[0],
			1 << PCIE_CLKREQ_GPIO_PIN, 1 << PCIE_CLKREQ_GPIO_PIN);
		LHL_REG(sih, gpio_int_st_port_adr[0],
			1 << PCIE_CLKREQ_GPIO_PIN, 1 << PCIE_CLKREQ_GPIO_PIN);
	}
}

/* To skip this function, specify a invalid "lpo_select" value in nvram */
int
si_lhl_set_lpoclk(si_t *sih, osl_t *osh, uint32 lpo_force)
{
	gciregs_t *gciregs;
	uint clk_det_cnt, status;
	int lhl_wlclk_sel;
	uint32 lpo = 0;
	int timeout = 0;
	gciregs = si_setcore(sih, GCI_CORE_ID, 0);

	ASSERT(gciregs != NULL);

	/* Apply nvram override to lpo */
	if ((lpo_force == LHL_LPO_AUTO) && ((lpo = (uint32)getintvar(NULL, "lpo_select")) == 0)) {
		lpo = LHL_OSC_32k_ENAB;
	} else {
		lpo = lpo_force;
	}

	/* Power up the desired LPO */
	switch (lpo) {
		case LHL_EXT_LPO_ENAB:
			LHL_REG(sih, lhl_main_ctl_adr, EXTLPO_BUF_PD, 0);
			lhl_wlclk_sel = LHL_EXT_SEL;
			break;

		case LHL_LPO1_ENAB:
			LHL_REG(sih, lhl_main_ctl_adr, LPO1_PD_EN, 0);
			lhl_wlclk_sel = LHL_LPO1_SEL;
			break;

		case LHL_LPO2_ENAB:
			LHL_REG(sih, lhl_main_ctl_adr, LPO2_PD_EN, 0);
			lhl_wlclk_sel = LHL_LPO2_SEL;
			break;

		case LHL_OSC_32k_ENAB:
			LHL_REG(sih, lhl_main_ctl_adr, OSC_32k_PD, 0);
			lhl_wlclk_sel = LHL_32k_SEL;
			break;

		default:
			goto done;
	}

	LHL_REG(sih, lhl_clk_det_ctl_adr,
		LHL_CLK_DET_CTL_AD_CNTR_CLK_SEL, lhl_wlclk_sel);

	/* Detect the desired LPO */

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
		goto error;
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
		goto error;
	}

	/* Select the desired LPO */

	LHL_REG(sih, lhl_main_ctl_adr,
		LHL_MAIN_CTL_ADR_LHL_WLCLK_SEL, (lhl_wlclk_sel) << LPO_SEL_SHIFT);

	status = ((R_REG(osh, &gciregs->lhl_clk_status_adr) & LHL_MAIN_CTL_ADR_FINAL_CLK_SEL) ==
		(unsigned)(((1 << lhl_wlclk_sel) << LPO_FINAL_SEL_SHIFT))) ? 1 : 0;
	timeout = 0;
	while (!status && timeout <= LPO_SEL_TIMEOUT) {
		OSL_DELAY(10);
		status =
		((R_REG(osh, &gciregs->lhl_clk_status_adr) & LHL_MAIN_CTL_ADR_FINAL_CLK_SEL) ==
		(unsigned)(((1 << lhl_wlclk_sel) << LPO_FINAL_SEL_SHIFT))) ? 1 : 0;
		timeout++;
	}

	if (timeout >= LPO_SEL_TIMEOUT) {
		LHL_ERROR(("LPO is not available timeout = %u\n, timeout", timeout));
		goto error;
	}
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
done:
	return BCME_OK;
error:
	ROMMABLE_ASSERT(0);
	return BCME_ERROR;
}

void
si_lhl_timer_config(si_t *sih, osl_t *osh, int timer_type)
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
			(LHL_WL_MACTIM0_INTRP_EN | LHL_WL_MACTIM0_INTRP_EDGE_TRIGGER),
			(LHL_WL_MACTIM0_INTRP_EN | LHL_WL_MACTIM0_INTRP_EDGE_TRIGGER));

		/* Programs bits for MACPHY_CLK_AVAIL and all its dependent bits in
		 * MacResourceReqMask0.
		 */
		PMU_REG(sih, mac_res_req_mask, ~0, si_pmu_rsrc_macphy_clk_deps(sih, osh, 0));

		/* One time init of mac_res_req_timer to enable interrupt and clock request */
		HND_PMU_SYNC_WR(sih, pmu, pmu, osh,
				PMUREGADDR(sih, pmu, pmu, mac_res_req_timer),
				((PRRT_ALP_REQ | PRRT_HQ_REQ | PRRT_INTEN) << FLAGS_SHIFT));

		if (si_numd11coreunits(sih) > 1) {
			LHL_REG(sih, lhl_wl_mactim1_intrp_adr,
				(LHL_WL_MACTIM0_INTRP_EN | LHL_WL_MACTIM0_INTRP_EDGE_TRIGGER),
				(LHL_WL_MACTIM0_INTRP_EN | LHL_WL_MACTIM0_INTRP_EDGE_TRIGGER));

			PMU_REG(sih, mac_res_req_mask1, ~0,
				si_pmu_rsrc_macphy_clk_deps(sih, osh, 1));

			HND_PMU_SYNC_WR(sih, pmu, pmu, osh,
					PMUREGADDR(sih, pmu, pmu, mac_res_req_timer1),
					((PRRT_ALP_REQ | PRRT_HQ_REQ | PRRT_INTEN) << FLAGS_SHIFT));
		}

		break;

	case LHL_ARM_TIMER:
		/* Enable ARM Timer interrupt */
		LHL_REG(sih, lhl_wl_armtim0_intrp_adr,
				(LHL_WL_ARMTIM0_INTRP_EN | LHL_WL_ARMTIM0_INTRP_EDGE_TRIGGER),
				(LHL_WL_ARMTIM0_INTRP_EN | LHL_WL_ARMTIM0_INTRP_EDGE_TRIGGER));

		/* Programs bits for HT_AVAIL and all its dependent bits in ResourceReqMask0 */
		PMU_REG(sih, res_req_mask, ~0, si_pmu_rsrc_ht_avail_clk_deps(sih, osh));

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
si_lhl_timer_enable(si_t *sih)
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
si_lhl_ilp_config(si_t *sih, osl_t *osh, uint32 ilp_period)
{
	 gciregs_t *gciregs;
	 if (CHIPID(sih->chip) == BCM43012_CHIP_ID) {
		gciregs = si_setcore(sih, GCI_CORE_ID, 0);
		ASSERT(gciregs != NULL);
		W_REG(osh, &gciregs->lhl_wl_ilp_val_adr, ilp_period);
	 }
}

#ifdef BCMULP
void
si_lhl_disable_sdio_wakeup(si_t *sih)
{
	/* Disable the interrupt */
	LHL_REG(sih, gpio_int_en_port_adr[0], (1 << ULP_SDIO_CMD_PIN), 0);

	/* Clear the pending interrupt status */
	LHL_REG(sih, gpio_int_st_port_adr[0], (1 << ULP_SDIO_CMD_PIN), (1 << ULP_SDIO_CMD_PIN));
}

void
si_lhl_enable_sdio_wakeup(si_t *sih, osl_t *osh)
{

	gciregs_t *gciregs;
	pmuregs_t *pmu;
	gciregs = si_setcore(sih, GCI_CORE_ID, 0);
	ASSERT(gciregs != NULL);
	if (CHIPID(sih->chip) == BCM43012_CHIP_ID) {
		/* For SDIO_CMD configure P8 for wake on negedge
		  * LHL  0 -> edge trigger intr mode,
		  * 1 -> neg edge trigger intr mode ,
		  * 6 -> din from wl side enable
		  */
		OR_REG(osh, &gciregs->gpio_ctrl_iocfg_p_adr[ULP_SDIO_CMD_PIN],
			(1 << GCI_GPIO_STS_EDGE_TRIG_BIT |
			1 << GCI_GPIO_STS_NEG_EDGE_TRIG_BIT |
			1 << GCI_GPIO_STS_WL_DIN_SELECT));
		/* Clear any old interrupt status */
		OR_REG(osh, &gciregs->gpio_int_st_port_adr[0], 1 << ULP_SDIO_CMD_PIN);

		/* LHL GPIO[8] intr en , GPIO[8] is mapped to SDIO_CMD */
		/* Enable P8 to generate interrupt */
		OR_REG(osh, &gciregs->gpio_int_en_port_adr[0], 1 << ULP_SDIO_CMD_PIN);

		/* Clear LHL GPIO status to trigger GCI Interrupt */
		OR_REG(osh, &gciregs->gci_intstat, GCI_INTSTATUS_LHLWLWAKE);
		/* Enable LHL GPIO Interrupt to trigger GCI Interrupt */
		OR_REG(osh, &gciregs->gci_intmask, GCI_INTMASK_LHLWLWAKE);
		OR_REG(osh, &gciregs->gci_wakemask, GCI_WAKEMASK_LHLWLWAKE);
		/* Note ->Enable GCI interrupt to trigger Chipcommon interrupt
		 * Set EciGciIntEn in IntMask and will be done from FCBS saved tuple
		 */
		/* Enable LHL to trigger extWake upto HT_AVAIL */
		/* LHL GPIO Interrupt is mapped to extWake[7] */
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
		ASSERT(pmu != NULL);
		/* Set bit 4 and 7 in ExtWakeMask */
		W_REG(osh, &pmu->extwakemask[0], CI_ECI	| CI_WECI);
		/* Program bits for MACPHY_CLK_AVAIL rsrc in ExtWakeReqMaskN */
		W_REG(osh, &pmu->extwakereqmask[0], SI_LHL_EXT_WAKE_REQ_MASK_MAGIC);
		/* Program 0 (no need to request explicitly for any backplane clk) */
		W_REG(osh, &pmu->extwakectrl, 0x0);
		/* Note: Configure MAC/Ucode to receive interrupt
		  * it will be done from saved tuple using FCBS code
		 */
	}
}
#endif /* BCMULP */

lhl_reg_set_t lv_sleep_mode_4369_lhl_reg_set[] =
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
	{LHL_REG_OFF(lhl_lp_main_ctl1_adr), BCM_MASK32(23, 0), 0x9E8F97},

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

	/* Making forceOTPpwrOn 0 */
	{LHL_REG_OFF(otpcontrol), (1 << 16), 0}
};

/* LV sleep mode summary:
 * LV mode is where both ABUCK and CBUCK are programmed to low voltages during
 * sleep, and VMUX selects ABUCK as VDDOUT_AON. LPLDO needs to power off.
 * With ASR ON, LPLDO OFF
 */
void
si_set_lv_sleep_mode_lhl_config_4369(si_t *sih)
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
}
