/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Misc utility routines for accessing PMU corerev specific features
 * of the SiliconBackplane-based Broadcom chips.
 *
 * Copyright (C) 1999-2019, Broadcom.
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
 * $Id: hndpmu.c 783841 2018-10-09 06:24:16Z $
 */

/**
 * @file
 * Note: this file contains PLL/FLL related functions. A chip can contain multiple PLLs/FLLs.
 * However, in the context of this file the baseband ('BB') PLL/FLL is referred to.
 *
 * Throughout this code, the prefixes 'pmu1_' and 'pmu2_' are used.
 * They refer to different revisions of the PMU (which is at revision 18 @ Apr 25, 2012)
 * pmu1_ marks the transition from PLL to ADFLL (Digital Frequency Locked Loop). It supports
 * fractional frequency generation. pmu2_ does not support fractional frequency generation.
 */

#include <bcm_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmdevs.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <hndchipc.h>
#include <hndpmu.h>
#include <hndlhl.h>
#if defined(BCMULP)
#include <ulp.h>
#endif /* defined(BCMULP) */
#include <sbgci.h>
#ifdef EVENT_LOG_COMPILE
#include <event_log.h>
#endif // endif
#include <sbgci.h>
#include <lpflags.h>

#define	PMU_ERROR(args)

#define	PMU_MSG(args)

/* To check in verbose debugging messages not intended
 * to be on except on private builds.
 */
#define	PMU_NONE(args)
#define flags_shift	14

/** contains resource bit positions for a specific chip */
struct rsc_per_chip_s {
	uint8 ht_avail;
	uint8 macphy_clkavail;
	uint8 ht_start;
	uint8 otp_pu;
	uint8 macphy_aux_clkavail;
};

typedef struct rsc_per_chip_s rsc_per_chip_t;

#if defined(BCMPMU_STATS) && !defined(BCMPMU_STATS_DISABLED)
bool	_pmustatsenab = TRUE;
#else
bool	_pmustatsenab = FALSE;
#endif /* BCMPMU_STATS */

/**
 * Balance between stable SDIO operation and power consumption is achieved using this function.
 * Note that each drive strength table is for a specific VDDIO of the SDIO pads, ideally this
 * function should read the VDDIO itself to select the correct table. For now it has been solved
 * with the 'BCM_SDIO_VDDIO' preprocessor constant.
 *
 * 'drivestrength': desired pad drive strength in mA. Drive strength of 0 requests tri-state (if
 *		    hardware supports this), if no hw support drive strength is not programmed.
 */
void
si_sdiod_drive_strength_init(si_t *sih, osl_t *osh, uint32 drivestrength)
{
	/*
	 * Note:
	 * This function used to set the SDIO drive strength via PMU_CHIPCTL1 for the
	 * 43143, 4330, 4334, 4336, 43362 chips.  These chips are now no longer supported, so
	 * the code has been deleted.
	 * Newer chips have the SDIO drive strength setting via a GCI Chip Control register,
	 * but the bit definitions are chip-specific.  We are keeping this function available
	 * (accessed via DHD 'sdiod_drive' IOVar) in case these newer chips need to provide access.
	 */
	UNUSED_PARAMETER(sih);
	UNUSED_PARAMETER(osh);
	UNUSED_PARAMETER(drivestrength);
}

void
si_switch_pmu_dependency(si_t *sih, uint mode)
{
#ifdef DUAL_PMU_SEQUENCE
	osl_t *osh = si_osh(sih);
	uint32 current_res_state;
	uint32 min_mask, max_mask;
	const pmu_res_depend_t *pmu_res_depend_table = NULL;
	uint pmu_res_depend_table_sz = 0;
	uint origidx;
	pmuregs_t *pmu;
	chipcregs_t *cc;
	BCM_REFERENCE(cc);

	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
		cc  = si_setcore(sih, CC_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
		cc  = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	current_res_state = R_REG(osh, &pmu->res_state);
	min_mask = R_REG(osh, &pmu->min_res_mask);
	max_mask = R_REG(osh, &pmu->max_res_mask);
	W_REG(osh, &pmu->min_res_mask, (min_mask | current_res_state));
	switch (mode) {
		case PMU_4364_1x1_MODE:
		{
			if (CHIPID(sih->chip) == BCM4364_CHIP_ID) {
					pmu_res_depend_table = bcm4364a0_res_depend_1x1;
					pmu_res_depend_table_sz =
						ARRAYSIZE(bcm4364a0_res_depend_1x1);
			max_mask = PMU_4364_MAX_MASK_1x1;
			W_REG(osh, &pmu->res_table_sel, RES4364_SR_SAVE_RESTORE);
			W_REG(osh, &pmu->res_updn_timer, PMU_4364_SAVE_RESTORE_UPDNTIME_1x1);
#if defined(SAVERESTORE)
				if (SR_ENAB()) {
					/* Disable 3x3 SR engine */
					W_REG(osh, &cc->sr1_control0,
					CC_SR0_4364_SR_ENG_CLK_EN |
					CC_SR0_4364_SR_RSRC_TRIGGER |
					CC_SR0_4364_SR_WD_MEM_MIN_DIV |
					CC_SR0_4364_SR_INVERT_CLK |
					CC_SR0_4364_SR_ENABLE_HT |
					CC_SR0_4364_SR_ALLOW_PIC |
					CC_SR0_4364_SR_PMU_MEM_DISABLE);
				}
#endif /* SAVERESTORE */
			}
			break;
		}
		case PMU_4364_3x3_MODE:
		{
			if (CHIPID(sih->chip) == BCM4364_CHIP_ID) {
				W_REG(osh, &pmu->res_table_sel, RES4364_SR_SAVE_RESTORE);
				W_REG(osh, &pmu->res_updn_timer,
					PMU_4364_SAVE_RESTORE_UPDNTIME_3x3);
				/* Change the dependency table only if required */
				if ((max_mask != PMU_4364_MAX_MASK_3x3) ||
					(max_mask != PMU_4364_MAX_MASK_RSDB)) {
						pmu_res_depend_table = bcm4364a0_res_depend_rsdb;
						pmu_res_depend_table_sz =
							ARRAYSIZE(bcm4364a0_res_depend_rsdb);
						max_mask = PMU_4364_MAX_MASK_3x3;
				}
#if defined(SAVERESTORE)
				if (SR_ENAB()) {
					/* Enable 3x3 SR engine */
					W_REG(osh, &cc->sr1_control0,
					CC_SR0_4364_SR_ENG_CLK_EN |
					CC_SR0_4364_SR_RSRC_TRIGGER |
					CC_SR0_4364_SR_WD_MEM_MIN_DIV |
					CC_SR0_4364_SR_INVERT_CLK |
					CC_SR0_4364_SR_ENABLE_HT |
					CC_SR0_4364_SR_ALLOW_PIC |
					CC_SR0_4364_SR_PMU_MEM_DISABLE |
					CC_SR0_4364_SR_ENG_EN_MASK);
				}
#endif /* SAVERESTORE */
			}
			break;
		}
		case PMU_4364_RSDB_MODE:
		default:
		{
			if (CHIPID(sih->chip) == BCM4364_CHIP_ID) {
				W_REG(osh, &pmu->res_table_sel, RES4364_SR_SAVE_RESTORE);
				W_REG(osh, &pmu->res_updn_timer,
					PMU_4364_SAVE_RESTORE_UPDNTIME_3x3);
				/* Change the dependency table only if required */
				if ((max_mask != PMU_4364_MAX_MASK_3x3) ||
					(max_mask != PMU_4364_MAX_MASK_RSDB)) {
						pmu_res_depend_table =
							bcm4364a0_res_depend_rsdb;
						pmu_res_depend_table_sz =
							ARRAYSIZE(bcm4364a0_res_depend_rsdb);
						max_mask = PMU_4364_MAX_MASK_RSDB;
				}
#if defined(SAVERESTORE)
			if (SR_ENAB()) {
					/* Enable 3x3 SR engine */
					W_REG(osh, &cc->sr1_control0,
					CC_SR0_4364_SR_ENG_CLK_EN |
					CC_SR0_4364_SR_RSRC_TRIGGER |
					CC_SR0_4364_SR_WD_MEM_MIN_DIV |
					CC_SR0_4364_SR_INVERT_CLK |
					CC_SR0_4364_SR_ENABLE_HT |
					CC_SR0_4364_SR_ALLOW_PIC |
					CC_SR0_4364_SR_PMU_MEM_DISABLE |
					CC_SR0_4364_SR_ENG_EN_MASK);
				}
#endif /* SAVERESTORE */
			}
			break;
		}
	}
	si_pmu_resdeptbl_upd(sih, osh, pmu, pmu_res_depend_table, pmu_res_depend_table_sz);
	W_REG(osh, &pmu->max_res_mask, max_mask);
	W_REG(osh, &pmu->min_res_mask, min_mask);
	si_pmu_wait_for_steady_state(sih, osh, pmu);
	/* Add some delay; allow resources to come up and settle. */
	OSL_DELAY(200);
	si_setcoreidx(sih, origidx);
#endif /* DUAL_PMU_SEQUENCE */
}

#if defined(BCMULP)

int
si_pmu_ulp_register(si_t *sih)
{
	return ulp_p1_module_register(ULP_MODULE_ID_PMU, &ulp_pmu_ctx, (void *)sih);
}

static uint
si_pmu_ulp_get_retention_size_cb(void *handle, ulp_ext_info_t *einfo)
{
	ULP_DBG(("%s: sz: %d\n", __FUNCTION__, sizeof(si_pmu_ulp_cr_dat_t)));
	return sizeof(si_pmu_ulp_cr_dat_t);
}

static int
si_pmu_ulp_enter_cb(void *handle, ulp_ext_info_t *einfo, uint8 *cache_data)
{
	si_pmu_ulp_cr_dat_t crinfo = {0};
	crinfo.ilpcycles_per_sec = ilpcycles_per_sec;
	ULP_DBG(("%s: ilpcycles_per_sec: %x\n", __FUNCTION__, ilpcycles_per_sec));
	memcpy(cache_data, (void*)&crinfo, sizeof(crinfo));
	return BCME_OK;
}

static int
si_pmu_ulp_exit_cb(void *handle, uint8 *cache_data,
	uint8 *p2_cache_data)
{
	si_pmu_ulp_cr_dat_t *crinfo = (si_pmu_ulp_cr_dat_t *)cache_data;

	ilpcycles_per_sec = crinfo->ilpcycles_per_sec;
	ULP_DBG(("%s: ilpcycles_per_sec: %x, cache_data: %p\n", __FUNCTION__,
		ilpcycles_per_sec, cache_data));
	return BCME_OK;
}

void
si_pmu_ulp_chipconfig(si_t *sih, osl_t *osh)
{
	uint32 reg_val;

	BCM_REFERENCE(reg_val);

	if (CHIPID(sih->chip) == BCM43012_CHIP_ID) {
		/* DS1 reset and clk enable init value config */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL14, ~0x0,
			(PMUCCTL14_43012_ARMCM3_RESET_INITVAL |
			PMUCCTL14_43012_DOT11MAC_CLKEN_INITVAL |
			PMUCCTL14_43012_SDIOD_RESET_INIVAL |
			PMUCCTL14_43012_SDIO_CLK_DMN_RESET_INITVAL |
			PMUCCTL14_43012_SOCRAM_CLKEN_INITVAL |
			PMUCCTL14_43012_M2MDMA_RESET_INITVAL |
			PMUCCTL14_43012_DOT11MAC_PHY_CLK_EN_INITVAL |
			PMUCCTL14_43012_DOT11MAC_PHY_CNTL_EN_INITVAL));

		/* Clear SFlash clock request and enable High Quality clock */
		CHIPC_REG(sih, clk_ctl_st, CCS_SFLASH_CLKREQ | CCS_HQCLKREQ, CCS_HQCLKREQ);

		reg_val = PMU_REG(sih, min_res_mask, ~0x0, ULP_MIN_RES_MASK);
		ULP_DBG(("si_pmu_ulp_chipconfig: min_res_mask: 0x%08x\n", reg_val));

		/* Force power switch off */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL2,
				(PMUCCTL02_43012_SUBCORE_PWRSW_FORCE_ON |
				PMUCCTL02_43012_PHY_PWRSW_FORCE_ON), 0);

	}
}

void
si_pmu_ulp_ilp_config(si_t *sih, osl_t *osh, uint32 ilp_period)
{
	pmuregs_t *pmu;
	pmu = si_setcoreidx(sih, si_findcoreidx(sih, PMU_CORE_ID, 0));
	W_REG(osh, &pmu->ILPPeriod, ilp_period);
	si_lhl_ilp_config(sih, osh, ilp_period);
}

/** Initialize DS1 PMU hardware resources */
void
si_pmu_ds1_res_init(si_t *sih, osl_t *osh)
{
	pmuregs_t *pmu;
	uint origidx;
	const pmu_res_updown_t *pmu_res_updown_table = NULL;
	uint pmu_res_updown_table_sz = 0;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM43012_CHIP_ID:
		pmu_res_updown_table = bcm43012a0_res_updown_ds1;
		pmu_res_updown_table_sz = ARRAYSIZE(bcm43012a0_res_updown_ds1);
		break;

	default:
		break;
	}

	/* Program up/down timers */
	while (pmu_res_updown_table_sz--) {
		ASSERT(pmu_res_updown_table != NULL);
		PMU_MSG(("DS1: Changing rsrc %d res_updn_timer to 0x%x\n",
			pmu_res_updown_table[pmu_res_updown_table_sz].resnum,
			pmu_res_updown_table[pmu_res_updown_table_sz].updown));
		W_REG(osh, &pmu->res_table_sel,
			pmu_res_updown_table[pmu_res_updown_table_sz].resnum);
		W_REG(osh, &pmu->res_updn_timer,
			pmu_res_updown_table[pmu_res_updown_table_sz].updown);
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

#endif /* defined(BCMULP) */

uint32
si_pmu_wake_bit_offset(si_t *sih)
{
	uint32 wakebit;

	switch (CHIPID(sih->chip)) {
	case BCM4347_CHIP_GRPID:
		wakebit = CC2_4347_GCI2WAKE_MASK;
		break;
	default:
		wakebit = 0;
		ASSERT(0);
		break;
	}

	return wakebit;
}

void si_pmu_set_min_res_mask(si_t *sih, osl_t *osh, uint min_res_mask)
{
	pmuregs_t *pmu;
	uint origidx;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	}
	else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	W_REG(osh, &pmu->min_res_mask, min_res_mask);
	OSL_DELAY(100);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

bool
si_pmu_cap_fast_lpo(si_t *sih)
{
	return (PMU_REG(sih, core_cap_ext, 0, 0) & PCAP_EXT_USE_MUXED_ILP_CLK_MASK) ? TRUE : FALSE;
}

int
si_pmu_fast_lpo_disable(si_t *sih)
{
	if (!si_pmu_cap_fast_lpo(sih)) {
		PMU_ERROR(("%s: No Fast LPO capability\n", __FUNCTION__));
		return BCME_ERROR;
	}

	PMU_REG(sih, pmucontrol_ext,
		PCTL_EXT_FASTLPO_ENAB |
		PCTL_EXT_FASTLPO_SWENAB |
		PCTL_EXT_FASTLPO_PCIE_SWENAB,
		0);
	OSL_DELAY(1000);
	return BCME_OK;
}

#ifdef BCMPMU_STATS
/*
 * 8 pmu statistics timer default map
 *
 * for CORE_RDY_AUX measure, set as below for timer 6 and 7 instead of CORE_RDY_MAIN.
 *	//core-n active duration : pmu_rsrc_state(CORE_RDY_AUX)
 *	{ SRC_CORE_RDY_AUX, FALSE, TRUE, LEVEL_HIGH},
 *	//core-n active duration : pmu_rsrc_state(CORE_RDY_AUX)
 *	{ SRC_CORE_RDY_AUX, FALSE, TRUE, EDGE_RISE}
 */
static pmu_stats_timer_t pmustatstimer[] = {
	{ SRC_LINK_IN_L12, FALSE, TRUE, PMU_STATS_LEVEL_HIGH},	//link_in_l12
	{ SRC_LINK_IN_L23, FALSE, TRUE, PMU_STATS_LEVEL_HIGH},	//link_in_l23
	{ SRC_PM_ST_IN_D0, FALSE, TRUE, PMU_STATS_LEVEL_HIGH},	//pm_st_in_d0
	{ SRC_PM_ST_IN_D3, FALSE, TRUE, PMU_STATS_LEVEL_HIGH},	//pm_st_in_d3
	//deep-sleep duration : pmu_rsrc_state(XTAL_PU)
	{ SRC_XTAL_PU, FALSE, TRUE, PMU_STATS_LEVEL_LOW},
	//deep-sleep entry count : pmu_rsrc_state(XTAL_PU)
	{ SRC_XTAL_PU, FALSE, TRUE, PMU_STATS_EDGE_FALL},
	//core-n active duration : pmu_rsrc_state(CORE_RDY_MAIN)
	{ SRC_CORE_RDY_MAIN, FALSE, TRUE, PMU_STATS_LEVEL_HIGH},
	//core-n active duration : pmu_rsrc_state(CORE_RDY_MAIN)
	{ SRC_CORE_RDY_MAIN, FALSE, TRUE, PMU_STATS_EDGE_RISE}
};

static void
si_pmustatstimer_update(osl_t *osh, pmuregs_t *pmu, uint8 timerid)
{
	uint32 stats_timer_ctrl;

	W_REG(osh, &pmu->pmu_statstimer_addr, timerid);
	stats_timer_ctrl =
		((pmustatstimer[timerid].src_num << PMU_ST_SRC_SHIFT) &
			PMU_ST_SRC_MASK) |
		((pmustatstimer[timerid].cnt_mode << PMU_ST_CNT_MODE_SHIFT) &
			PMU_ST_CNT_MODE_MASK) |
		((pmustatstimer[timerid].enable << PMU_ST_EN_SHIFT) & PMU_ST_EN_MASK) |
		((pmustatstimer[timerid].int_enable << PMU_ST_INT_EN_SHIFT) & PMU_ST_INT_EN_MASK);
	W_REG(osh, &pmu->pmu_statstimer_ctrl, stats_timer_ctrl);
	W_REG(osh, &pmu->pmu_statstimer_N, 0);
}

void
si_pmustatstimer_int_enable(si_t *sih)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	OR_REG(osh, &pmu->pmuintmask0, PMU_INT_STAT_TIMER_INT_MASK);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

void
si_pmustatstimer_int_disable(si_t *sih)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	AND_REG(osh, &pmu->pmuintmask0, ~PMU_INT_STAT_TIMER_INT_MASK);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

void
si_pmustatstimer_init(si_t *sih)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);
	uint32 core_cap_ext;
	uint8 max_stats_timer_num;
	int8 i;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	core_cap_ext = R_REG(osh, &pmu->core_cap_ext);

	max_stats_timer_num = ((core_cap_ext & PCAP_EXT_ST_NUM_MASK) >> PCAP_EXT_ST_NUM_SHIFT) + 1;

	for (i = 0; i < max_stats_timer_num; i++) {
		si_pmustatstimer_update(osh, pmu, i);
	}

	OR_REG(osh, &pmu->pmuintmask0, PMU_INT_STAT_TIMER_INT_MASK);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

void
si_pmustatstimer_dump(si_t *sih)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);
	uint32 core_cap_ext, pmucapabilities, AlpPeriod, ILPPeriod, pmuintmask0, pmuintstatus;
	uint8 max_stats_timer_num, max_stats_timer_src_num;
	uint32 stat_timer_ctrl, stat_timer_N;
	uint8 i;
	uint32 current_time_ms = OSL_SYSUPTIME();

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	pmucapabilities = R_REG(osh, &pmu->pmucapabilities);
	core_cap_ext = R_REG(osh, &pmu->core_cap_ext);
	AlpPeriod = R_REG(osh, &pmu->slowclkperiod);
	ILPPeriod = R_REG(osh, &pmu->ILPPeriod);

	max_stats_timer_num = ((core_cap_ext & PCAP_EXT_ST_NUM_MASK) >>
		PCAP_EXT_ST_NUM_SHIFT) + 1;
	max_stats_timer_src_num = ((core_cap_ext & PCAP_EXT_ST_SRC_NUM_MASK) >>
		PCAP_EXT_ST_SRC_NUM_SHIFT) + 1;

	pmuintstatus = R_REG(osh, &pmu->pmuintstatus);
	pmuintmask0 = R_REG(osh, &pmu->pmuintmask0);

	PMU_ERROR(("%s : TIME %d\n", __FUNCTION__, current_time_ms));

	PMU_ERROR(("\tMAX Timer Num %d, MAX Source Num %d\n",
		max_stats_timer_num, max_stats_timer_src_num));
	PMU_ERROR(("\tpmucapabilities 0x%8x, core_cap_ext 0x%8x, AlpPeriod 0x%8x, ILPPeriod 0x%8x, "
		"pmuintmask0 0x%8x, pmuintstatus 0x%8x, pmurev %d\n",
		pmucapabilities, core_cap_ext, AlpPeriod, ILPPeriod,
		pmuintmask0, pmuintstatus, PMUREV(sih->pmurev)));

	for (i = 0; i < max_stats_timer_num; i++) {
		W_REG(osh, &pmu->pmu_statstimer_addr, i);
		stat_timer_ctrl = R_REG(osh, &pmu->pmu_statstimer_ctrl);
		stat_timer_N = R_REG(osh, &pmu->pmu_statstimer_N);
		PMU_ERROR(("\t Timer %d : control 0x%8x, %d\n",
			i, stat_timer_ctrl, stat_timer_N));
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

void
si_pmustatstimer_start(si_t *sih, uint8 timerid)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	pmustatstimer[timerid].enable = TRUE;

	W_REG(osh, &pmu->pmu_statstimer_addr, timerid);
	OR_REG(osh, &pmu->pmu_statstimer_ctrl, PMU_ST_ENAB << PMU_ST_EN_SHIFT);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

void
si_pmustatstimer_stop(si_t *sih, uint8 timerid)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	pmustatstimer[timerid].enable = FALSE;

	W_REG(osh, &pmu->pmu_statstimer_addr, timerid);
	AND_REG(osh, &pmu->pmu_statstimer_ctrl, ~(PMU_ST_ENAB << PMU_ST_EN_SHIFT));

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

void
si_pmustatstimer_clear(si_t *sih, uint8 timerid)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	W_REG(osh, &pmu->pmu_statstimer_addr, timerid);
	W_REG(osh, &pmu->pmu_statstimer_N, 0);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

void
si_pmustatstimer_clear_overflow(si_t *sih)
{
	uint8 i;
	uint32 core_cap_ext;
	uint8 max_stats_timer_num;
	uint32 timerN;
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	core_cap_ext = R_REG(osh, &pmu->core_cap_ext);
	max_stats_timer_num = ((core_cap_ext & PCAP_EXT_ST_NUM_MASK) >> PCAP_EXT_ST_NUM_SHIFT) + 1;

	for (i = 0; i < max_stats_timer_num; i++) {
		W_REG(osh, &pmu->pmu_statstimer_addr, i);
		timerN = R_REG(osh, &pmu->pmu_statstimer_N);
		if (timerN == 0xFFFFFFFF) {
			PMU_ERROR(("pmustatstimer overflow clear - timerid : %d\n", i));
			si_pmustatstimer_clear(sih, i);
		}
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

uint32
si_pmustatstimer_read(si_t *sih, uint8 timerid)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);
	uint32 stats_timer_N;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	W_REG(osh, &pmu->pmu_statstimer_addr, timerid);
	stats_timer_N = R_REG(osh, &pmu->pmu_statstimer_N);

	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return stats_timer_N;
}

void
si_pmustatstimer_cfg_src_num(si_t *sih, uint8 src_num, uint8 timerid)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	pmustatstimer[timerid].src_num = src_num;
	si_pmustatstimer_update(osh, pmu, timerid);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

void
si_pmustatstimer_cfg_cnt_mode(si_t *sih, uint8 cnt_mode, uint8 timerid)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	pmustatstimer[timerid].cnt_mode = cnt_mode;
	si_pmustatstimer_update(osh, pmu, timerid);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}
#endif /* BCMPMU_STATS */
