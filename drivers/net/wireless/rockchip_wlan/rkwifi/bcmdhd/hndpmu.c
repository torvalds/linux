/*
 * Misc utility routines for accessing PMU corerev specific features
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
#if defined DONGLEBUILD
#include <hndcpu.h>
#ifdef __arm__
#include <hndarm.h>
#endif
#endif /* DONGLEBUILD */
#if !defined(BCMDONGLEHOST)
#include <bcm_math.h>
#include <bcmotp.h>
#ifdef BCM_OTP_API
#include <bcm_otp_api.h>
#endif /* BCM_OTP_API */
#endif /* !BCMDONGLEHOST */
#if !defined(BCMDONGLEHOST)
#include <saverestore.h>
#endif
#include <hndlhl.h>
#include <sbgci.h>
#ifdef EVENT_LOG_COMPILE
#include <event_log.h>
#endif
#include <sbgci.h>
#include <lpflags.h>

#include "siutils_priv.h"

#ifdef BCM_AVS
#include <bcm_avs.h>
#endif

#if defined(EVENT_LOG_COMPILE) && defined(BCMDBG_ERR) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define PMU_ERROR(args) EVENT_LOG_RA(EVENT_LOG_TAG_PMU_ERROR, args)
#else
#define PMU_ERROR(args) EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_PMU_ERROR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#elif defined(BCMDBG_ERR)
#define	PMU_ERROR(args)	printf args
#else
#define	PMU_ERROR(args)
#endif	/* defined(BCMDBG_ERR) && defined(ERR_USE_EVENT_LOG) */

#ifdef BCMDBG
//#define BCMDBG_PMU
#endif

#ifdef BCMDBG_PMU
#define	PMU_MSG(args)	printf args
#else
#define	PMU_MSG(args)
#endif	/* BCMDBG_MPU */

/* To check in verbose debugging messages not intended
 * to be on except on private builds.
 */
#define	PMU_NONE(args)
#define flags_shift	14

/** contains resource bit positions for a specific chip */
struct rsc_per_chip {
	uint8 ht_avail;
	uint8 macphy_clkavail;
	uint8 ht_start;
	uint8 otp_pu;
	uint8 macphy_aux_clkavail;
	uint8 macphy_scan_clkavail;
	uint8 cb_ready;
	uint8 dig_ready;
};

typedef struct rsc_per_chip rsc_per_chip_t;

#if defined(BCMPMU_STATS) && !defined(BCMPMU_STATS_DISABLED)
bool	_pmustatsenab = TRUE;
#else
bool	_pmustatsenab = FALSE;
#endif /* BCMPMU_STATS */

/* 1MHz lpo enable */
/* PLEASE USE THIS MACRO IN ATTACH PATH ONLY! */
#if defined(BCM_FASTLPO) && !defined(BCM_FASTLPO_DISABLED)
	#define FASTLPO_ENAB()	(TRUE)
#else
	#define FASTLPO_ENAB()	(FALSE)
#endif

/* Disable the power optimization feature */
bool	_bcm_pwr_opt_dis = FALSE;

#ifdef BCMSRTOPOFF
bool _srtopoff_enab = FALSE;
#endif

pmuregs_t *hnd_pmur = NULL;  /* PMU core regs */

#if !defined(BCMDONGLEHOST)
static void si_pmu_chipcontrol_xtal_settings_4369(si_t *sih);
static void si_pmu_chipcontrol_xtal_settings_4362(si_t *sih);
static void si_pmu_chipcontrol_xtal_settings_4378(si_t *sih);

/* PLL controls/clocks */
static void si_pmu1_pllinit1(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 xtal);
static void si_pmu_pll_off(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 *min_mask,
	uint32 *max_mask, uint32 *clk_ctl_st);
static void si_pmu_pll_on(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 min_mask,
	uint32 max_mask, uint32 clk_ctl_st);
static void si_pmu_otp_pllcontrol(si_t *sih, osl_t *osh);
static void si_pmu_otp_vreg_control(si_t *sih, osl_t *osh);
static void si_pmu_otp_chipcontrol(si_t *sih, osl_t *osh);
static uint32 si_pmu_def_alp_clock(si_t *sih, osl_t *osh);
static bool si_pmu_update_pllcontrol(si_t *sih, osl_t *osh, uint32 xtal, bool update_required);
static uint32 si_pmu_htclk_mask(si_t *sih);

static uint32 si_pmu1_cpuclk0(si_t *sih, osl_t *osh, pmuregs_t *pmu);
static uint32 si_pmu1_alpclk0(si_t *sih, osl_t *osh, pmuregs_t *pmu);

static uint32 si_pmu1_cpuclk0_pll2(si_t *sih);

/* PMU resources */
static uint32 si_pmu_res_deps(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 rsrcs, bool all);
static uint si_pmu_res_uptime(si_t *sih, osl_t *osh, pmuregs_t *pmu,
	uint8 rsrc, bool pmu_fast_trans_en);
static void si_pmu_res_masks(si_t *sih, uint32 *pmin, uint32 *pmax);

uint32 si_pmu_get_pmutime_diff(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 *prev);
bool si_pmu_wait_for_res_pending(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint usec,
	bool cond, uint32 *elapsed_time);

#ifdef __ARM_ARCH_7A__
static uint32 si_pmu_mem_ca7clock(si_t *sih, osl_t *osh);
#endif

static uint8 fastlpo_dis_get(void);
static uint8 fastlpo_pcie_dis_get(void);

static uint32 si_pmu_bpclk_4387(si_t *sih);

static int si_pmu_openloop_cal_43012(si_t *sih, uint16 currtemp);

static uint32 si_pmu_pll6val_armclk_calc(osl_t *osh, pmuregs_t *pmu, uint32 armclk, uint32 xtal,
		bool write);
static bool si_pmu_armpll_write_required(si_t *sih, uint32 xtal);

uint8 si_pmu_pll28nm_calc_ndiv(uint32 fvco, uint32 xtal, uint32 *ndiv_int, uint32 *ndiv_frac);

void si_pmu_armpll_freq_upd(si_t *sih, uint8 p1div, uint32 ndiv_int, uint32 ndiv_frac);
void si_pmu_bbpll_freq_upd(si_t *sih, uint8 p1div, uint32 ndiv_int, uint32 ndiv_frac);
void si_pmu_armpll_chmdiv_upd(si_t *sih, uint32 ch0_mdiv, uint32 ch1_mdiv);

#ifdef BCM_LDO3P3_SOFTSTART
static int si_pmu_ldo3p3_soft_start_get(si_t *sih, osl_t *osh, uint32 bt_or_wl, int *res);
static int si_pmu_ldo3p3_soft_start_set(si_t *sih, osl_t *osh, uint32 bt_or_wl, uint32 slew_rate);
#endif /* BCM_LDO3P3_SOFTSTART */
#ifdef XTAL_BIAS_FROM_OTP
static void si_pmu_chipcontrol_xtal_bias_from_otp(si_t *sih, uint8* flag, uint8* val);
#ifndef BCM_OTP_API
static void si_pmu_chipcontrol_xtal_bias_cal_done_offsets(si_t *sih, uint16* wrd_offset,
		uint8* wrd_shift, uint8* wrd_mask);
static void si_pmu_chipcontrol_xtal_bias_val_offsets(si_t *sih, uint16* wrd_offset,
		uint8* wrd_shift, uint8* wrd_mask);
#endif /* !BCM_OTP_API */
#endif /* XTAL_BIAS_FROM_OTP */

/* PMU timer ticks once in 32uS */
#define PMU_US_STEPS (32)

void *g_si_pmutmr_lock_arg = NULL;
si_pmu_callback_t g_si_pmutmr_lock_cb = NULL, g_si_pmutmr_unlock_cb = NULL;

/* FVCO frequency in [KHz] */
#define FVCO_640	640000  /**< 640MHz */
#define FVCO_880	880000	/**< 880MHz */
#define FVCO_1760	1760000	/**< 1760MHz */
#define FVCO_1440	1440000	/**< 1440MHz */
#define FVCO_960	960000	/**< 960MHz */
#define FVCO_960p1	960100  /**< 960.1MHz */
#define FVCO_960010	960010	/**< 960.0098MHz */
#define FVCO_961	961000	/**< 961MHz */
#define FVCO_960p5	960500	/**< 960.5MHz */
#define FVCO_963	963000	/**< 963MHz */
#define FVCO_963p01	963010  /**< 963.01MHz */
#define FVCO_1000	1000000	/**< 1000MHz */
#define FVCO_1600	1600000	/**< 1600MHz */
#define FVCO_1920	1920000	/**< 1920MHz */
#define FVCO_1938	1938000 /* 1938MHz */
#define FVCO_385	385000  /**< 385MHz */
#define FVCO_400	400000  /**< 400MHz */
#define FVCO_720	720000  /**< 720MHz */
#define FVCO_2880	2880000	/**< 2880 MHz */
#define FVCO_2946	2946000	/**< 2946 MHz */
#define FVCO_3000	3000000	/**< 3000 MHz */
#define FVCO_3200	3200000	/**< 3200 MHz */
#define FVCO_1002p8	1002823 /**< 1002.823MHz */

/* defines to make the code more readable */
/* But 0 is a valid resource number! */
#define NO_SUCH_RESOURCE	0	/**< means: chip does not have such a PMU resource */

/* uses these defines instead of 'magic' values when writing to register pllcontrol_addr */
#define PMU_PLL_CTRL_REG0	0
#define PMU_PLL_CTRL_REG1	1
#define PMU_PLL_CTRL_REG2	2
#define PMU_PLL_CTRL_REG3	3
#define PMU_PLL_CTRL_REG4	4
#define PMU_PLL_CTRL_REG5	5
#define PMU_PLL_CTRL_REG6	6
#define PMU_PLL_CTRL_REG7	7
#define PMU_PLL_CTRL_REG8	8
#define PMU_PLL_CTRL_REG9	9
#define PMU_PLL_CTRL_REG10	10
#define PMU_PLL_CTRL_REG11	11
#define PMU_PLL_CTRL_REG12	12
#define PMU_PLL_CTRL_REG13	13
#define PMU_PLL_CTRL_REG14	14
#define PMU_PLL_CTRL_REG15	15

#ifndef BCM_OTP_API
#define OTP_XTAL_BIAS_CAL_DONE_4378_WRD_OFFSET 743
#define OTP_XTAL_BIAS_CAL_DONE_4378_WRD_SHIFT  8
#define OTP_XTAL_BIAS_CAL_DONE_4378_WRD_MASK   0x1

#define OTP_XTAL_BIAS_VAL_4378_WRD_OFFSET 743
#define OTP_XTAL_BIAS_VAL_4378_WRD_SHIFT  0
#define OTP_XTAL_BIAS_VAL_4378_WRD_MASK   0xFF
#endif /* !BCM_OTP_API */

/* changes the drive strength of gpio_12 and gpio_14 from 0x3 to 0x01 */
#define GPIO_DRIVE_4378_MASK	0x3Fu
#define GPIO_DRIVE_4378_VAL	0x09u

/**
 * The chip has one or more PLLs/FLLs (e.g. baseband PLL, USB PHY PLL). The settings of each PLL are
 * contained within one or more 'PLL control' registers. Since the PLL hardware requires that
 * changes for one PLL are committed at once, the PMU has a provision for 'updating' all PLL control
 * registers at once.
 *
 * When software wants to change the any PLL parameters, it withdraws requests for that PLL clock,
 * updates the PLL control registers being careful not to alter any control signals for the other
 * PLLs, and then writes a 1 to PMUCtl.PllCtnlUpdate to commit the changes. Best usage model would
 * be bring PLL down then update the PLL control register.
 */
void
si_pmu_pllupd(si_t *sih)
{
	pmu_corereg(sih, SI_CC_IDX, pmucontrol,
	           PCTL_PLL_PLLCTL_UPD, PCTL_PLL_PLLCTL_UPD);
}

/* 4360_OTP_PU is used for 4352, not a typo */
static rsc_per_chip_t rsc_4352 =  {NO_SUCH_RESOURCE,  NO_SUCH_RESOURCE,
		NO_SUCH_RESOURCE,  RES4360_OTP_PU, NO_SUCH_RESOURCE,
		NO_SUCH_RESOURCE, NO_SUCH_RESOURCE, NO_SUCH_RESOURCE};
static rsc_per_chip_t rsc_4360 =  {RES4360_HT_AVAIL,  NO_SUCH_RESOURCE,
		NO_SUCH_RESOURCE,  RES4360_OTP_PU, NO_SUCH_RESOURCE,
		NO_SUCH_RESOURCE, NO_SUCH_RESOURCE, NO_SUCH_RESOURCE};
static rsc_per_chip_t rsc_43602 = {RES43602_HT_AVAIL, RES43602_MACPHY_CLKAVAIL,
		RES43602_HT_START, NO_SUCH_RESOURCE, NO_SUCH_RESOURCE,
		NO_SUCH_RESOURCE, NO_SUCH_RESOURCE, NO_SUCH_RESOURCE};
static rsc_per_chip_t rsc_43012 = {RES43012_HT_AVAIL, RES43012_MACPHY_CLK_AVAIL,
		RES43012_HT_START, RES43012_OTP_PU, NO_SUCH_RESOURCE,
		NO_SUCH_RESOURCE, NO_SUCH_RESOURCE, NO_SUCH_RESOURCE};
/* As per the chip team OTP doesn't have the resource in 4369 */
static rsc_per_chip_t rsc_4369 = {RES4369_HT_AVAIL, RES4369_MACPHY_MAIN_CLK_AVAIL,
		RES4369_HT_AVAIL, NO_SUCH_RESOURCE, RES4369_MACPHY_AUX_CLK_AVAIL,
		NO_SUCH_RESOURCE, NO_SUCH_RESOURCE, RES4369_DIG_CORE_RDY};
static rsc_per_chip_t rsc_4378 = {RES4378_HT_AVAIL, RES4378_MACPHY_MAIN_CLK_AVAIL,
		RES4378_HT_AVAIL, RES4378_PMU_SLEEP, RES4378_MACPHY_AUX_CLK_AVAIL,
		NO_SUCH_RESOURCE, RES4378_CORE_RDY_CB, RES4378_CORE_RDY_DIG};
static rsc_per_chip_t rsc_4387 = {RES4387_HT_AVAIL, RES4387_MACPHY_CLK_MAIN,
		RES4387_HT_AVAIL, RES4387_PMU_SLEEP, RES4387_MACPHY_CLK_AUX,
		RES4387_MACPHY_CLK_SCAN, RES4387_CORE_RDY_CB, RES4387_CORE_RDY_DIG};
static rsc_per_chip_t rsc_4388 = {RES4388_HT_AVAIL, RES4388_MACPHY_CLK_MAIN,
		RES4388_HT_AVAIL, RES4388_PMU_LP, RES4388_MACPHY_CLK_AUX,
		RES4388_MACPHY_CLK_SCAN, RES4388_CORE_RDY_CB, RES4388_CORE_RDY_DIG};
static rsc_per_chip_t rsc_4389 = {RES4389_HT_AVAIL, RES4389_MACPHY_CLK_MAIN,
		RES4389_HT_AVAIL, RES4389_PMU_LP, RES4389_MACPHY_CLK_AUX,
		RES4389_MACPHY_CLK_SCAN, RES4389_CORE_RDY_CB, RES4389_CORE_RDY_DIG};
static rsc_per_chip_t rsc_4397 = {RES4397_HT_AVAIL, RES4397_MACPHY_CLK_MAIN,
		RES4397_HT_AVAIL, RES4397_PMU_LP, RES4397_MACPHY_CLK_AUX,
		RES4397_MACPHY_CLK_SCAN, RES4397_CORE_RDY_CB, RES4397_CORE_RDY_DIG};

static rsc_per_chip_t rsc_4362 = {RES4362_HT_AVAIL, RES4362_MACPHY_MAIN_CLK_AVAIL,
		RES4362_HT_AVAIL, /* macphy aux clk */
		NO_SUCH_RESOURCE, NO_SUCH_RESOURCE, NO_SUCH_RESOURCE, NO_SUCH_RESOURCE,
		RES4362_DIG_CORE_RDY};

/**
* For each chip, location of resource bits (e.g., ht bit) in resource mask registers may differ.
* This function abstracts the bit position of commonly used resources, thus making the rest of the
* code in hndpmu.c cleaner.
*/
static rsc_per_chip_t* BCMRAMFN(si_pmu_get_rsc_positions)(si_t *sih)
{
	rsc_per_chip_t *rsc = NULL;

	switch (CHIPID(sih->chip)) {
	case BCM4352_CHIP_ID:
	case BCM43526_CHIP_ID:	/* usb variant of 4352 */
		rsc = &rsc_4352;
		break;
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
		rsc = &rsc_4360;
		break;
	CASE_BCM43602_CHIP:
		rsc = &rsc_43602;
		break;
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		rsc = &rsc_43012;
		break;
	case BCM4369_CHIP_GRPID:
		rsc = &rsc_4369;
		break;
	case BCM4362_CHIP_GRPID:
		rsc = &rsc_4362;
		break;
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		rsc = &rsc_4378;
		break;
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
		rsc = &rsc_4387;
		break;
	case BCM4388_CHIP_GRPID:
		rsc = &rsc_4388;
		break;
	case BCM4389_CHIP_GRPID:
		rsc = &rsc_4389;
		break;
	case BCM4397_CHIP_GRPID:
		rsc = &rsc_4397;
		break;
	default:
		ASSERT(0);
		break;
	}

	return rsc;
}; /* si_pmu_get_rsc_positions */

static const char BCMATTACHDATA(rstr_pllD)[] = "pll%d";
static const char BCMATTACHDATA(rstr_regD)[] = "reg%d";
static const char BCMATTACHDATA(rstr_chipcD)[] = "chipc%d";
static const char BCMATTACHDATA(rstr_rDt)[] = "r%dt";
static const char BCMATTACHDATA(rstr_rDd)[] = "r%dd";
static const char BCMATTACHDATA(rstr_Invalid_Unsupported_xtal_value_D)[] =
	"Invalid/Unsupported xtal value %d";
static const char BCMATTACHDATA(rstr_xtalfreq)[] = "xtalfreq";
#if defined(SAVERESTORE) && defined(LDO3P3_MIN_RES_MASK)
static const char BCMATTACHDATA(rstr_ldo_prot)[] = "ldo_prot";
#endif /* SAVERESTORE && LDO3P3_MIN_RES_MASK */
static const char BCMATTACHDATA(rstr_btldo3p3pu)[] = "btldopu";
#if defined(BCM_FASTLPO_PMU) && !defined(BCM_FASTLPO_PMU_DISABLED)
static const char BCMATTACHDATA(rstr_fastlpo_dis)[] = "fastlpo_dis";
#endif /* BCM_FASTLPO_PMU */
static const char BCMATTACHDATA(rstr_fastlpo_pcie_dis)[] = "fastlpo_pcie_dis";
static const char BCMATTACHDATA(rstr_memlpldo_volt)[] = "memlpldo_volt";
static const char BCMATTACHDATA(rstr_lpldo_volt)[] = "lpldo_volt";
static const char BCMATTACHDATA(rstr_dyn_clksw_en)[] = "dyn_clksw_en";
static const char BCMATTACHDATA(rstr_abuck_volt)[] = "abuck_volt";
static const char BCMATTACHDATA(rstr_cbuck_volt)[] = "cbuck_volt";
static const char BCMATTACHDATA(rstr_csrtune)[] = "csr_tune";

/* The check for OTP parameters for the PLL control registers is done and if found the
 * registers are updated accordingly.
 */

/**
 * As a hardware bug workaround, OTP can contain variables in the form 'pll%d=%d'.
 * If these variables are present, the corresponding PLL control register(s) are
 * overwritten, but not yet 'updated'.
 */
static void
BCMATTACHFN(si_pmu_otp_pllcontrol)(si_t *sih, osl_t *osh)
{
	char name[16];
	const char *otp_val;
	uint8 i;
	uint32 val;
	uint8 pll_ctrlcnt = 0;

	if (FWSIGN_ENAB()) {
		return;
	}

	if (PMUREV(sih->pmurev) >= 5) {
		pll_ctrlcnt = (sih->pmucaps & PCAP5_PC_MASK) >> PCAP5_PC_SHIFT;
	} else {
		pll_ctrlcnt = (sih->pmucaps & PCAP_PC_MASK) >> PCAP_PC_SHIFT;
	}

	for (i = 0; i < pll_ctrlcnt; i++) {
		snprintf(name, sizeof(name), rstr_pllD, i);
		if ((otp_val = getvar(NULL, name)) == NULL)
			continue;

		val = (uint32)bcm_strtoul(otp_val, NULL, 0);
		si_pmu_pllcontrol(sih, i, ~0, val);
	}
}

/**
 * The check for OTP parameters for the Voltage Regulator registers is done and if found the
 * registers are updated accordingly.
 */
static void
BCMATTACHFN(si_pmu_otp_vreg_control)(si_t *sih, osl_t *osh)
{
	char name[16];
	const char *otp_val;
	uint8 i;
	uint32 val;
	uint8 vreg_ctrlcnt = 0;

	if (FWSIGN_ENAB()) {
		return;
	}

	if (PMUREV(sih->pmurev) >= 5) {
		vreg_ctrlcnt = (sih->pmucaps & PCAP5_VC_MASK) >> PCAP5_VC_SHIFT;
	} else {
		vreg_ctrlcnt = (sih->pmucaps & PCAP_VC_MASK) >> PCAP_VC_SHIFT;
	}

	for (i = 0; i < vreg_ctrlcnt; i++) {
		snprintf(name, sizeof(name), rstr_regD, i);
		if ((otp_val = getvar(NULL, name)) == NULL)
			continue;

		val = (uint32)bcm_strtoul(otp_val, NULL, 0);
		si_pmu_vreg_control(sih, i, ~0, val);
	}
}

/**
 * The check for OTP parameters for the chip control registers is done and if found the
 * registers are updated accordingly.
 */
static void
BCMATTACHFN(si_pmu_otp_chipcontrol)(si_t *sih, osl_t *osh)
{
	uint32 val, cc_ctrlcnt, i;
	char name[16];
	const char *otp_val;

	if (FWSIGN_ENAB()) {
		return;
	}
	if (PMUREV(sih->pmurev) >= 5) {
		cc_ctrlcnt = (sih->pmucaps & PCAP5_CC_MASK) >> PCAP5_CC_SHIFT;
	} else {
		cc_ctrlcnt = (sih->pmucaps & PCAP_CC_MASK) >> PCAP_CC_SHIFT;
	}

	for (i = 0; i < cc_ctrlcnt; i++) {
		snprintf(name, sizeof(name), rstr_chipcD, i);
		if ((otp_val = getvar(NULL, name)) == NULL)
			continue;

		val = (uint32)bcm_strtoul(otp_val, NULL, 0);
		si_pmu_chipcontrol(sih, i, 0xFFFFFFFF, val); /* writes to PMU chipctrl reg 'i' */
	}
}

/**
 * A chip contains one or more LDOs (Low Drop Out regulators). During chip bringup, it can turn out
 * that the default (POR) voltage of a regulator is not right or optimal.
 * This function is called only by si_pmu_swreg_init() for specific chips
 */
void
si_pmu_set_ldo_voltage(si_t *sih, osl_t *osh, uint8 ldo, uint8 voltage)
{
	uint8 sr_cntl_shift = 0, rc_shift = 0, shift = 0, mask = 0;
	uint8 addr = 0;
	uint8 do_reg2 = 0, rshift2 = 0, rc_shift2 = 0, mask2 = 0, addr2 = 0;

	BCM_REFERENCE(osh);

	ASSERT(sih->cccaps & CC_CAP_PMU);

	switch (CHIPID(sih->chip)) {
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
	case BCM4352_CHIP_ID:
	case BCM43526_CHIP_ID:
		switch (ldo) {
		case  SET_LDO_VOLTAGE_PAREF:
			addr = 1;
			rc_shift = 0;
			mask = 0xf;
			break;
		default:
			ASSERT(FALSE);
			break;
		}
		break;
	CASE_BCM43602_CHIP:
		switch (ldo) {
		case  SET_LDO_VOLTAGE_PAREF:
			addr = 0;
			rc_shift = 29;
			mask = 0x7;
			do_reg2 = 1;
			addr2 = 1;
			rshift2 = 3;
			mask2 = 0x8;
			break;
		default:
			ASSERT(FALSE);
			break;
		}
		break;
	default:
		ASSERT(FALSE);
		return;
	}

	shift = sr_cntl_shift + rc_shift;

	pmu_corereg(sih, SI_CC_IDX, regcontrol_addr, /* PMU VREG register */
		~0, addr);
	pmu_corereg(sih, SI_CC_IDX, regcontrol_data,
		mask << shift, (voltage & mask) << shift);
	if (do_reg2) {
		/* rshift2 - right shift moves mask2 to bit 0, rc_shift2 - left shift in reg */
		si_pmu_vreg_control(sih, addr2, (mask2 >> rshift2) << rc_shift2,
			((voltage & mask2) >> rshift2) << rc_shift2);
	}
} /* si_pmu_set_ldo_voltage */

/* d11 slow to fast clock transition time in slow clock cycles */
#define D11SCC_SLOW2FAST_TRANSITION	2

/* For legacy chips only, will be discarded eventually */
static uint16
BCMINITFN(si_pmu_fast_pwrup_delay_legacy)(si_t *sih, osl_t *osh, pmuregs_t *pmu)
{
	uint pmudelay = PMU_MAX_TRANSITION_DLY;
	uint32 ilp;			/* ILP clock frequency in [Hz] */
	rsc_per_chip_t *rsc;		/* chip specific resource bit positions */

	/* Should be calculated based on the PMU updown/depend tables */
	switch (CHIPID(sih->chip)) {
	case BCM43460_CHIP_ID:
	case BCM43526_CHIP_ID:
		pmudelay = 3700;
		break;
	case BCM4360_CHIP_ID:
	case BCM4352_CHIP_ID:
		if (CHIPREV(sih->chiprev) < 4) {
			pmudelay = 1500;
		} else {
			pmudelay = 3000;
		}
		break;
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		pmudelay = 1500; /* In micro seconds for 43012 chip */
		break;
	CASE_BCM43602_CHIP:
		rsc = si_pmu_get_rsc_positions(sih);
		/* Retrieve time by reading it out of the hardware */
		ilp = si_ilp_clock(sih);
		if (ilp != 0) {
			pmudelay = (si_pmu_res_uptime(sih, osh, pmu, rsc->macphy_clkavail, FALSE) +
				D11SCC_SLOW2FAST_TRANSITION) * ((1000000 + ilp - 1) / ilp);
			pmudelay = (11 * pmudelay) / 10;
		}
		break;
	case BCM4362_CHIP_GRPID:
		rsc = si_pmu_get_rsc_positions(sih);
		/* Retrieve time by reading it out of the hardware */
		ilp = si_ilp_clock(sih);
		if (ilp != 0) {
			pmudelay = si_pmu_res_uptime(sih, osh, pmu, rsc->ht_avail, FALSE) +
				D11SCC_SLOW2FAST_TRANSITION;
			pmudelay = (11 * pmudelay) / 10;
			/* With PWR SW optimization, Need to add this addtional
			time to fast power up delay to avoid beacon loss
			*/
			pmudelay += 600;
		}
		break;
	default:
		break;
	}

	return (uint16)pmudelay;
} /* si_pmu_fast_pwrup_delay_legacy */

/**
 * d11 core has a 'fastpwrup_dly' register that must be written to.
 * This function returns d11 slow to fast clock transition time in [us] units.
 * It does not write to the d11 core.
 */
uint16
BCMINITFN(si_pmu_fast_pwrup_delay)(si_t *sih, osl_t *osh)
{
	uint pmudelay = PMU_MAX_TRANSITION_DLY;
	pmuregs_t *pmu;
	uint origidx;
	rsc_per_chip_t *rsc;		/* chip specific resource bit positions */
	uint macunit;
	bool pmu_fast_trans_en;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	if (ISSIM_ENAB(sih)) {
		pmudelay = 1000;
		goto exit;
	}

	macunit = si_coreunit(sih);

	origidx = si_coreidx(sih);
	/* Still support 43602 so need AOB check,
	 * 43602 is the only non-AOB chip supported now
	 */
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	pmu_fast_trans_en = (R_REG(osh, &pmu->pmucontrol_ext) & PCTL_EXT_FAST_TRANS_ENAB) ?
		TRUE : FALSE;

	rsc = si_pmu_get_rsc_positions(sih);

	switch (CHIPID(sih->chip)) {
	case BCM4369_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
		if (macunit == 0) {
			pmudelay = si_pmu_res_uptime(sih, osh, pmu,
				rsc->macphy_clkavail, pmu_fast_trans_en);
		} else if (macunit == 1) {
			pmudelay = si_pmu_res_uptime(sih, osh, pmu,
				rsc->macphy_aux_clkavail, pmu_fast_trans_en);
		} else {
			ASSERT(0);
		}
		break;
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		if (macunit == 0) {
			pmudelay = si_pmu_res_uptime(sih, osh, pmu,
				rsc->macphy_clkavail, pmu_fast_trans_en);
		} else if (macunit == 1) {
			pmudelay = si_pmu_res_uptime(sih, osh, pmu,
				rsc->macphy_aux_clkavail, pmu_fast_trans_en);
		} else if (macunit == 2) {
			pmudelay = si_pmu_res_uptime(sih, osh, pmu,
				rsc->macphy_scan_clkavail, pmu_fast_trans_en);
		} else {
			ASSERT(0);
		}
		break;

	default:
		pmudelay = si_pmu_fast_pwrup_delay_legacy(sih, osh, pmu);
		break;
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);

exit:
	return (uint16)pmudelay;
} /* si_pmu_fast_pwrup_delay */

/*
 * Get fast pwrup delay for given resource
 */
static uint
BCMINITFN(si_pmu_fast_pwrup_delay_rsrc)(si_t *sih, osl_t *osh, uint8 rsrc)
{
	uint pmudelay = PMU_MAX_TRANSITION_DLY;
	pmuregs_t *pmu = NULL;
	bool pmu_fast_trans_en = TRUE;
	uint origidx;

	origidx = si_coreidx(sih);
	pmu = si_setcore(sih, PMU_CORE_ID, 0);
	ASSERT(pmu != NULL);

	pmu_fast_trans_en = (R_REG(osh, &pmu->pmucontrol_ext) & PCTL_EXT_FAST_TRANS_ENAB) ?
		TRUE : FALSE;

	pmudelay = si_pmu_res_uptime(sih, osh, pmu, rsrc, pmu_fast_trans_en);

	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return pmudelay;
}

/*
 * Get fast pwrup delay for given DIG_READY resource
 */
uint
BCMINITFN(si_pmu_fast_pwrup_delay_dig)(si_t *sih, osl_t *osh)
{
	uint delay = 0;
	rsc_per_chip_t *rsc = si_pmu_get_rsc_positions(sih);
	ASSERT(rsc);

	if (rsc) {
		delay = si_pmu_fast_pwrup_delay_rsrc(sih, osh, rsc->dig_ready);
	}
	return delay;
}

/*
 * During chip bringup, it can turn out that the 'hard wired' PMU dependencies are not fully
 * correct, or that up/down time values can be optimized. The following data structures and arrays
 * deal with that.
 */

/* Setup resource up/down timers */
typedef struct {
	uint8 resnum;
	uint32 updown;
} pmu_res_updown_t;

#define PMU_RES_SUBSTATE_SHIFT		8

/* Setup resource substate transition timer value */
typedef struct {
	uint8 resnum;
	uint8 substate;
	uint32 tmr;
} pmu_res_subst_trans_tmr_t;

/* Change resource dependencies masks */
typedef struct {
	uint32 res_mask;		/* resources (chip specific) */
	int8 action;			/* action, e.g. RES_DEPEND_SET */
	uint32 depend_mask;		/* changes to the dependencies mask */
	bool (*filter)(si_t *sih);	/* action is taken when filter is NULL or return TRUE */
} pmu_res_depend_t;

/* Resource dependencies mask change action */
#define RES_DEPEND_SET		0	/* Override the dependencies mask */
#define RES_DEPEND_ADD		1	/* Add to the  dependencies mask */
#define RES_DEPEND_REMOVE	-1	/* Remove from the dependencies mask */

/* Using a safe SAVE_RESTORE up/down time, it will get updated after openloop cal */
static const pmu_res_updown_t BCMATTACHDATA(bcm43012a0_res_updown_ds0)[] = {
	{RES43012_MEMLPLDO_PU,		0x00200020},
	{RES43012_PMU_SLEEP,		0x00a600a6},
	{RES43012_FAST_LPO,		0x00D20000},
	{RES43012_BTLPO_3P3,		0x007D0000},
	{RES43012_SR_POK,		0x00c80000},
	{RES43012_DUMMY_PWRSW,		0x01400000},
	{RES43012_DUMMY_LDO3P3,		0x00000000},
	{RES43012_DUMMY_BT_LDO3P3,	0x00000000},
	{RES43012_DUMMY_RADIO,		0x00000000},
	{RES43012_VDDB_VDDRET,		0x0020000a},
	{RES43012_HV_LDO3P3,		0x002C0000},
	{RES43012_XTAL_PU,		0x04000000},
	{RES43012_SR_CLK_START,		0x00080000},
	{RES43012_XTAL_STABLE,		0x00000000},
	{RES43012_FCBS,			0x00000000},
	{RES43012_CBUCK_MODE,		0x00000000},
	{RES43012_CORE_READY,		0x00000000},
	{RES43012_ILP_REQ,		0x00000000},
	{RES43012_ALP_AVAIL,		0x00280008},
	{RES43012_RADIOLDO_1P8,		0x00220000},
	{RES43012_MINI_PMU,		0x00220000},
	{RES43012_SR_SAVE_RESTORE,	0x02600260},
	{RES43012_PHY_PWRSW,		0x00800005},
	{RES43012_VDDB_CLDO,		0x0020000a},
	{RES43012_SUBCORE_PWRSW,	0x0060000a},
	{RES43012_SR_SLEEP,		0x00000000},
	{RES43012_HT_START,		0x00A00000},
	{RES43012_HT_AVAIL,		0x00000000},
	{RES43012_MACPHY_CLK_AVAIL,	0x00000000},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4360_res_updown)[] = {
	{RES4360_BBPLLPWRSW_PU,		0x00200001}
};

static const pmu_res_updown_t BCMATTACHDATA(bcm43602_res_updown)[] = {
	{RES43602_SR_SAVE_RESTORE,	0x00190019},
	{RES43602_XTAL_PU,		0x00280002},
	{RES43602_RFLDO_PU,		0x00430005}
};

static pmu_res_depend_t BCMATTACHDATA(bcm43012a0_res_depend_ds0)[] = {
	{0, 0, 0, NULL}
};

static pmu_res_depend_t BCMATTACHDATA(bcm43602_res_depend)[] = {
	/* JIRA HW43602-131 : PCIe SERDES dependency problem */
	{
		PMURES_BIT(RES43602_SR_SUBCORE_PWRSW) | PMURES_BIT(RES43602_SR_CLK_STABLE) |
		PMURES_BIT(RES43602_SR_SAVE_RESTORE)  | PMURES_BIT(RES43602_SR_SLEEP) |
		PMURES_BIT(RES43602_LQ_START) | PMURES_BIT(RES43602_LQ_AVAIL) |
		PMURES_BIT(RES43602_WL_CORE_RDY) | PMURES_BIT(RES43602_ILP_REQ) |
		PMURES_BIT(RES43602_ALP_AVAIL) | PMURES_BIT(RES43602_RFLDO_PU) |
		PMURES_BIT(RES43602_HT_START) | PMURES_BIT(RES43602_HT_AVAIL) |
		PMURES_BIT(RES43602_MACPHY_CLKAVAIL),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_SERDES_PU),
		NULL
	},
	/* set rsrc  7, 8, 9, 12, 13, 14 & 17 add (1<<10 | 1<<4 )] */
	{
		PMURES_BIT(RES43602_SR_CLK_START) | PMURES_BIT(RES43602_SR_PHY_PWRSW) |
		PMURES_BIT(RES43602_SR_SUBCORE_PWRSW) | PMURES_BIT(RES43602_SR_CLK_STABLE) |
		PMURES_BIT(RES43602_SR_SAVE_RESTORE) | PMURES_BIT(RES43602_SR_SLEEP) |
		PMURES_BIT(RES43602_WL_CORE_RDY),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_XTALLDO_PU) | PMURES_BIT(RES43602_XTAL_PU),
		NULL
	},
	/* set rsrc 11 add (1<<13 | 1<<12 | 1<<9 | 1<<8 | 1<<7 )] */
	{
		PMURES_BIT(RES43602_PERST_OVR),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_SR_CLK_START) | PMURES_BIT(RES43602_SR_PHY_PWRSW) |
		PMURES_BIT(RES43602_SR_SUBCORE_PWRSW) | PMURES_BIT(RES43602_SR_CLK_STABLE) |
		PMURES_BIT(RES43602_SR_SAVE_RESTORE),
		NULL
	},
	/* set rsrc 19, 21, 22, 23 & 24 remove ~(1<<16 | 1<<15 )] */
	{
		PMURES_BIT(RES43602_ALP_AVAIL) | PMURES_BIT(RES43602_RFLDO_PU) |
		PMURES_BIT(RES43602_HT_START) | PMURES_BIT(RES43602_HT_AVAIL) |
		PMURES_BIT(RES43602_MACPHY_CLKAVAIL),
		RES_DEPEND_REMOVE,
		PMURES_BIT(RES43602_LQ_START) | PMURES_BIT(RES43602_LQ_AVAIL),
		NULL
	}
};

#ifndef BCM_BOOTLOADER
/** switch off LPLDO for 12x12 package because it can cause a problem when chip is reset */
static const pmu_res_depend_t BCMATTACHDATA(bcm43602_12x12_res_depend)[] = {
	/* set rsrc 19, 21, 22, 23 & 24 remove ~(1<<16 | 1<<15 )] */
	{	/* resources no longer dependent on resource that is going to be removed */
		PMURES_BIT(RES43602_LPLDO_PU)        | PMURES_BIT(RES43602_REGULATOR)        |
		PMURES_BIT(RES43602_PMU_SLEEP)       | PMURES_BIT(RES43602_RSVD_3)           |
		PMURES_BIT(RES43602_XTALLDO_PU)      | PMURES_BIT(RES43602_SERDES_PU)        |
		PMURES_BIT(RES43602_BBPLL_PWRSW_PU)  | PMURES_BIT(RES43602_SR_CLK_START)     |
		PMURES_BIT(RES43602_SR_PHY_PWRSW)    | PMURES_BIT(RES43602_SR_SUBCORE_PWRSW) |
		PMURES_BIT(RES43602_XTAL_PU)         | PMURES_BIT(RES43602_PERST_OVR)        |
		PMURES_BIT(RES43602_SR_CLK_STABLE)   | PMURES_BIT(RES43602_SR_SAVE_RESTORE)  |
		PMURES_BIT(RES43602_SR_SLEEP)        | PMURES_BIT(RES43602_LQ_START)         |
		PMURES_BIT(RES43602_LQ_AVAIL)        | PMURES_BIT(RES43602_WL_CORE_RDY)      |
		PMURES_BIT(RES43602_ILP_REQ)         | PMURES_BIT(RES43602_ALP_AVAIL)        |
		PMURES_BIT(RES43602_RADIO_PU)        | PMURES_BIT(RES43602_RFLDO_PU)         |
		PMURES_BIT(RES43602_HT_START)        | PMURES_BIT(RES43602_HT_AVAIL)         |
		PMURES_BIT(RES43602_MACPHY_CLKAVAIL) | PMURES_BIT(RES43602_PARLDO_PU)        |
		PMURES_BIT(RES43602_RSVD_26),
		RES_DEPEND_REMOVE,
		/* resource that is going to be removed */
		PMURES_BIT(RES43602_LPLDO_PU),
		NULL
	}
};

static const pmu_res_depend_t BCMATTACHDATA(bcm43602_res_pciewar)[] = {
	{
		PMURES_BIT(RES43602_PERST_OVR),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_REGULATOR) |
		PMURES_BIT(RES43602_PMU_SLEEP) |
		PMURES_BIT(RES43602_XTALLDO_PU) |
		PMURES_BIT(RES43602_XTAL_PU) |
		PMURES_BIT(RES43602_RADIO_PU),
		NULL
	},
	{
		PMURES_BIT(RES43602_WL_CORE_RDY),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_PERST_OVR),
		NULL
	},
	{
		PMURES_BIT(RES43602_LQ_START),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_PERST_OVR),
		NULL
	},
	{
		PMURES_BIT(RES43602_LQ_AVAIL),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_PERST_OVR),
		NULL
	},
	{
		PMURES_BIT(RES43602_ALP_AVAIL),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_PERST_OVR),
		NULL
	},
	{
		PMURES_BIT(RES43602_HT_START),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_PERST_OVR),
		NULL
	},
	{
		PMURES_BIT(RES43602_HT_AVAIL),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_PERST_OVR),
		NULL
	},
	{
		PMURES_BIT(RES43602_MACPHY_CLKAVAIL),
		RES_DEPEND_ADD,
		PMURES_BIT(RES43602_PERST_OVR),
		NULL
	}
};
#endif /* BCM_BOOTLOADER */

static const pmu_res_updown_t BCMATTACHDATA(bcm4360B1_res_updown)[] = {
	/* Need to change elements here, should get default values for this - 4360B1 */
	{RES4360_XTAL_PU,               0x00430002}, /* Changed for 4360B1 */
};

static pmu_res_depend_t BCMATTACHDATA(bcm4369a0_res_depend)[] = {
	{PMURES_BIT(RES4369_DUMMY),			RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4369_ABUCK),			RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4369_PMU_SLEEP),			RES_DEPEND_SET, 0x00000001, NULL},
	{PMURES_BIT(RES4369_MISCLDO),			RES_DEPEND_SET, 0x00000007, NULL},
	{PMURES_BIT(RES4369_LDO3P3),			RES_DEPEND_SET, 0x00000001, NULL},
	{PMURES_BIT(RES4369_FAST_LPO_AVAIL),		RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4369_XTAL_PU),			RES_DEPEND_SET, 0x00000007, NULL},
	{PMURES_BIT(RES4369_XTAL_STABLE),		RES_DEPEND_SET, 0x00000047, NULL},
	{PMURES_BIT(RES4369_PWRSW_DIG),			RES_DEPEND_SET, 0x060000cf, NULL},
	{PMURES_BIT(RES4369_SR_DIG),			RES_DEPEND_SET, 0x060001cf, NULL},
	{PMURES_BIT(RES4369_SLEEP_DIG),			RES_DEPEND_SET, 0x060003cf, NULL},
	{PMURES_BIT(RES4369_PWRSW_AUX),			RES_DEPEND_SET, 0x040000cf, NULL},
	{PMURES_BIT(RES4369_SR_AUX),			RES_DEPEND_SET, 0x040008cf, NULL},
	{PMURES_BIT(RES4369_SLEEP_AUX),			RES_DEPEND_SET, 0x040018cf, NULL},
	{PMURES_BIT(RES4369_PWRSW_MAIN),		RES_DEPEND_SET, 0x040000cf, NULL},
	{PMURES_BIT(RES4369_SR_MAIN),			RES_DEPEND_SET, 0x040040cf, NULL},
	{PMURES_BIT(RES4369_SLEEP_MAIN),		RES_DEPEND_SET, 0x0400c0cf, NULL},
	{PMURES_BIT(RES4369_DIG_CORE_RDY),		RES_DEPEND_SET, 0x060007cf, NULL},
	{PMURES_BIT(RES4369_CORE_RDY_AUX),		RES_DEPEND_SET, 0x040038cf, NULL},
	{PMURES_BIT(RES4369_ALP_AVAIL),			RES_DEPEND_SET, 0x060207cf, NULL},
	{PMURES_BIT(RES4369_RADIO_AUX_PU),		RES_DEPEND_SET, 0x040438df, NULL},
	{PMURES_BIT(RES4369_MINIPMU_AUX_PU),		RES_DEPEND_SET, 0x041438df, NULL},
	{PMURES_BIT(RES4369_CORE_RDY_MAIN),		RES_DEPEND_SET, 0x0401c0cf, NULL},
	{PMURES_BIT(RES4369_RADIO_MAIN_PU),		RES_DEPEND_SET, 0x0441c0df, NULL},
	{PMURES_BIT(RES4369_MINIPMU_MAIN_PU),		RES_DEPEND_SET, 0x04c1c0df, NULL},
	{PMURES_BIT(RES4369_PCIE_EP_PU),		RES_DEPEND_SET, 0x040000cf, NULL},
	{PMURES_BIT(RES4369_COLD_START_WAIT),		RES_DEPEND_SET, 0x0000000f, NULL},
	{PMURES_BIT(RES4369_ARMHTAVAIL),		RES_DEPEND_SET, 0x060a07cf, NULL},
	{PMURES_BIT(RES4369_HT_AVAIL),			RES_DEPEND_SET, 0x060a07cf, NULL},
	{PMURES_BIT(RES4369_MACPHY_AUX_CLK_AVAIL),	RES_DEPEND_SET, 0x163e3fdf, NULL},
	{PMURES_BIT(RES4369_MACPHY_MAIN_CLK_AVAIL),	RES_DEPEND_SET, 0x17cbc7df, NULL},
};

static pmu_res_depend_t BCMATTACHDATA(bcm4369a0_res_depend_fastlpo_pcie)[] = {
	{PMURES_BIT(RES4369_DUMMY),			RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4369_ABUCK),			RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4369_PMU_SLEEP),			RES_DEPEND_SET, 0x00000001, NULL},
	{PMURES_BIT(RES4369_MISCLDO),			RES_DEPEND_SET, 0x00000007, NULL},
	{PMURES_BIT(RES4369_LDO3P3),			RES_DEPEND_SET, 0x00000001, NULL},
	{PMURES_BIT(RES4369_FAST_LPO_AVAIL),		RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4369_XTAL_PU),			RES_DEPEND_SET, 0x00000007, NULL},
	{PMURES_BIT(RES4369_XTAL_STABLE),		RES_DEPEND_SET, 0x00000047, NULL},
	{PMURES_BIT(RES4369_PWRSW_DIG),                 RES_DEPEND_SET, 0x060000ef, NULL},
	{PMURES_BIT(RES4369_SR_DIG),                    RES_DEPEND_SET, 0x060001ef, NULL},
	{PMURES_BIT(RES4369_SLEEP_DIG),                 RES_DEPEND_SET, 0x060003ef, NULL},
	{PMURES_BIT(RES4369_PWRSW_AUX),                 RES_DEPEND_SET, 0x040000ef, NULL},
	{PMURES_BIT(RES4369_SR_AUX),                    RES_DEPEND_SET, 0x040008ef, NULL},
	{PMURES_BIT(RES4369_SLEEP_AUX),                 RES_DEPEND_SET, 0x040018ef, NULL},
	{PMURES_BIT(RES4369_PWRSW_MAIN),                RES_DEPEND_SET, 0x040000ef, NULL},
	{PMURES_BIT(RES4369_SR_MAIN),                   RES_DEPEND_SET, 0x040040ef, NULL},
	{PMURES_BIT(RES4369_SLEEP_MAIN),                RES_DEPEND_SET, 0x0400c0ef, NULL},
	{PMURES_BIT(RES4369_DIG_CORE_RDY),              RES_DEPEND_SET, 0x060007ef, NULL},
	{PMURES_BIT(RES4369_CORE_RDY_AUX),              RES_DEPEND_SET, 0x040038ef, NULL},
	{PMURES_BIT(RES4369_ALP_AVAIL),                 RES_DEPEND_SET, 0x060207ef, NULL},
	{PMURES_BIT(RES4369_RADIO_AUX_PU),              RES_DEPEND_SET, 0x040438ff, NULL},
	{PMURES_BIT(RES4369_MINIPMU_AUX_PU),            RES_DEPEND_SET, 0x041438ff, NULL},
	{PMURES_BIT(RES4369_CORE_RDY_MAIN),             RES_DEPEND_SET, 0x0401c0ef, NULL},
	{PMURES_BIT(RES4369_RADIO_MAIN_PU),             RES_DEPEND_SET, 0x0441c0ff, NULL},
	{PMURES_BIT(RES4369_MINIPMU_MAIN_PU),           RES_DEPEND_SET, 0x04c1c0ff, NULL},
	{PMURES_BIT(RES4369_PCIE_EP_PU),                RES_DEPEND_SET, 0x0400002f, NULL},
	{PMURES_BIT(RES4369_COLD_START_WAIT),           RES_DEPEND_SET, 0x0000002f, NULL},
	{PMURES_BIT(RES4369_ARMHTAVAIL),                RES_DEPEND_SET, 0x060a07ef, NULL},
	{PMURES_BIT(RES4369_HT_AVAIL),                  RES_DEPEND_SET, 0x060a07ef, NULL},
	{PMURES_BIT(RES4369_MACPHY_AUX_CLK_AVAIL),      RES_DEPEND_SET, 0x163e3fff, NULL},
	{PMURES_BIT(RES4369_MACPHY_MAIN_CLK_AVAIL),     RES_DEPEND_SET, 0x17cbc7ff, NULL},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4369a0_res_updown)[] = {
	{RES4369_DUMMY,                 0x00220022},
	{RES4369_ABUCK,                 0x00c80022},
	{RES4369_PMU_SLEEP,             0x00c80022},
	{RES4369_MISCLDO,               0x00bd0022},
	{RES4369_LDO3P3,                0x00bd0022},
	{RES4369_FAST_LPO_AVAIL,        0x01500022},
	{RES4369_XTAL_PU,               0x07d00022},
	{RES4369_XTAL_STABLE,           0x00220022},
	{RES4369_PWRSW_DIG,             0x02100087},
	{RES4369_SR_DIG,                0x02000200},
	{RES4369_SLEEP_DIG,             0x00220022},
	{RES4369_PWRSW_AUX,             0x03900087},
	{RES4369_SR_AUX,                0x01cc01cc},
	{RES4369_SLEEP_AUX,             0x00220022},
	{RES4369_PWRSW_MAIN,            0x03900087},
	{RES4369_SR_MAIN,               0x02000200},
	{RES4369_SLEEP_MAIN,            0x00220022},
	{RES4369_DIG_CORE_RDY,          0x00220044},
	{RES4369_CORE_RDY_AUX,          0x00220044},
	{RES4369_ALP_AVAIL,             0x00220044},
	{RES4369_RADIO_AUX_PU,          0x006e0022},
	{RES4369_MINIPMU_AUX_PU,        0x00460022},
	{RES4369_CORE_RDY_MAIN,         0x00220022},
	{RES4369_RADIO_MAIN_PU,         0x006e0022},
	{RES4369_MINIPMU_MAIN_PU,       0x00460022},
	{RES4369_PCIE_EP_PU,            0x02100087},
	{RES4369_COLD_START_WAIT,       0x00220022},
	{RES4369_ARMHTAVAIL,            0x00a80022},
	{RES4369_HT_AVAIL,              RES4369_HTAVAIL_VAL},
	{RES4369_MACPHY_AUX_CLK_AVAIL,  0x00640022},
	{RES4369_MACPHY_MAIN_CLK_AVAIL, 0x00640022},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4369a0_res_updown_fastlpo_pmu)[] = {
	{RES4369_DUMMY,                 0x00220022},
	{RES4369_ABUCK,                 0x00c80022},
	{RES4369_PMU_SLEEP,             0x00c80022},
	{RES4369_MISCLDO,               0x00bd0022},
	{RES4369_LDO3P3,                0x00bd0022},
	{RES4369_FAST_LPO_AVAIL,        0x01500022},
	{RES4369_XTAL_PU,               0x07d00022},
	{RES4369_XTAL_STABLE,           0x00220022},
	{RES4369_PWRSW_DIG,             0x02100087},
	{RES4369_SR_DIG,                0x02000200},
	{RES4369_SLEEP_DIG,             0x00220022},
	{RES4369_PWRSW_AUX,             0x03900087},
	{RES4369_SR_AUX,                0x01cc01cc},
	{RES4369_SLEEP_AUX,             0x00220022},
	{RES4369_PWRSW_MAIN,            0x03900087},
	{RES4369_SR_MAIN,               0x02000200},
	{RES4369_SLEEP_MAIN,            0x00220022},
	{RES4369_DIG_CORE_RDY,          0x00220044},
	{RES4369_CORE_RDY_AUX,          0x00220044},
	{RES4369_ALP_AVAIL,             0x00220044},
	{RES4369_RADIO_AUX_PU,          0x006e0022},
	{RES4369_MINIPMU_AUX_PU,        0x00460022},
	{RES4369_CORE_RDY_MAIN,         0x00220022},
	{RES4369_RADIO_MAIN_PU,         0x006e0022},
	{RES4369_MINIPMU_MAIN_PU,       0x00460022},
	{RES4369_PCIE_EP_PU,            0x01200087},
	{RES4369_COLD_START_WAIT,       0x00220022},
	{RES4369_ARMHTAVAIL,            0x00a80022},
	{RES4369_HT_AVAIL,              RES4369_HTAVAIL_VAL},
	{RES4369_MACPHY_AUX_CLK_AVAIL,  0x00640022},
	{RES4369_MACPHY_MAIN_CLK_AVAIL, 0x00640022},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4369b0_res_updown)[] = {
	{RES4369_DUMMY,                 0x00220022},
	{RES4369_ABUCK,                 0x00c80022},
	{RES4369_PMU_SLEEP,             0x00c80022},
	{RES4369_MISCLDO,               0x00bd0022},
	{RES4369_LDO3P3,                0x01ad0022},
	{RES4369_FAST_LPO_AVAIL,        0x01500022},
	{RES4369_XTAL_PU,               0x05dc0022},
	{RES4369_XTAL_STABLE,           0x00220022},
	{RES4369_PWRSW_DIG,             0x02100087},
	{RES4369_SR_DIG,                0x00A000A0},
	{RES4369_SLEEP_DIG,             0x00220022},
	{RES4369_PWRSW_AUX,             0x03900087},
	{RES4369_SR_AUX,                0x01400140},
	{RES4369_SLEEP_AUX,             0x00220022},
	{RES4369_PWRSW_MAIN,            0x03900087},
	{RES4369_SR_MAIN,               0x01A001A0},
	{RES4369_SLEEP_MAIN,            0x00220022},
	{RES4369_DIG_CORE_RDY,          0x00220044},
	{RES4369_CORE_RDY_AUX,          0x00220044},
	{RES4369_ALP_AVAIL,             0x00220044},
	{RES4369_RADIO_AUX_PU,          0x006e0022},
	{RES4369_MINIPMU_AUX_PU,        0x00460022},
	{RES4369_CORE_RDY_MAIN,         0x00220022},
	{RES4369_RADIO_MAIN_PU,         0x006e0022},
	{RES4369_MINIPMU_MAIN_PU,       0x00460022},
	{RES4369_PCIE_EP_PU,            0x02100087},
	{RES4369_COLD_START_WAIT,       0x00220022},
	{RES4369_ARMHTAVAIL,            0x00a80022},
	{RES4369_HT_AVAIL,              RES4369_HTAVAIL_VAL},
	{RES4369_MACPHY_AUX_CLK_AVAIL,  0x00640022},
	{RES4369_MACPHY_MAIN_CLK_AVAIL, 0x00640022},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4369b0_res_updown_fastlpo_pmu)[] = {
	{RES4369_DUMMY,                 0x00220022},
	{RES4369_ABUCK,                 0x00c80022},
	{RES4369_PMU_SLEEP,             0x00c80022},
	{RES4369_MISCLDO,               0x00bd0022},
	{RES4369_LDO3P3,                0x01ad0022},
	{RES4369_FAST_LPO_AVAIL,        0x01500022},
	{RES4369_XTAL_PU,               0x05dc0022},
	{RES4369_XTAL_STABLE,           0x00220022},
	{RES4369_PWRSW_DIG,             0x02100087},
	{RES4369_SR_DIG,                0x02000200},
	{RES4369_SLEEP_DIG,             0x00220022},
	{RES4369_PWRSW_AUX,             0x03900087},
	{RES4369_SR_AUX,                0x01cc01cc},
	{RES4369_SLEEP_AUX,             0x00220022},
	{RES4369_PWRSW_MAIN,            0x03900087},
	{RES4369_SR_MAIN,               0x02000200},
	{RES4369_SLEEP_MAIN,            0x00220022},
	{RES4369_DIG_CORE_RDY,          0x00220044},
	{RES4369_CORE_RDY_AUX,          0x00220044},
	{RES4369_ALP_AVAIL,             0x00220044},
	{RES4369_RADIO_AUX_PU,          0x006e0022},
	{RES4369_MINIPMU_AUX_PU,        0x00460022},
	{RES4369_CORE_RDY_MAIN,         0x00220022},
	{RES4369_RADIO_MAIN_PU,         0x006e0022},
	{RES4369_MINIPMU_MAIN_PU,       0x00460022},
	{RES4369_PCIE_EP_PU,            0x01200087},
	{RES4369_COLD_START_WAIT,       0x00220022},
	{RES4369_ARMHTAVAIL,            0x00a80022},
	{RES4369_HT_AVAIL,              RES4369_HTAVAIL_VAL},
	{RES4369_MACPHY_AUX_CLK_AVAIL,  0x00640022},
	{RES4369_MACPHY_MAIN_CLK_AVAIL, 0x00640022},
};

static pmu_res_depend_t BCMATTACHDATA(bcm4362_res_depend)[] = {
	{PMURES_BIT(RES4362_DUMMY),                 RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4362_ABUCK),                 RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4362_PMU_SLEEP),             RES_DEPEND_SET, 0x00000001, NULL},
	{PMURES_BIT(RES4362_MISCLDO_PU),            RES_DEPEND_SET, 0x00000007, NULL},
	{PMURES_BIT(RES4362_LDO3P3_PU),             RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4362_FAST_LPO_AVAIL),        RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4362_XTAL_PU),               RES_DEPEND_SET, 0x00000007, NULL},
	{PMURES_BIT(RES4362_XTAL_STABLE),           RES_DEPEND_SET, 0x00000047, NULL},
	{PMURES_BIT(RES4362_PWRSW_DIG),             RES_DEPEND_SET, 0x060000cf, NULL},
	{PMURES_BIT(RES4362_SR_DIG),                RES_DEPEND_SET, 0x060001cf, NULL},
	{PMURES_BIT(RES4362_SLEEP_DIG),             RES_DEPEND_SET, 0x060003cf, NULL},
	{PMURES_BIT(RES4362_PWRSW_AUX),             RES_DEPEND_SET, 0x040000cf, NULL},
	{PMURES_BIT(RES4362_SR_AUX),                RES_DEPEND_SET, 0x040008cf, NULL},
	{PMURES_BIT(RES4362_SLEEP_AUX),             RES_DEPEND_SET, 0x040018cf, NULL},
	{PMURES_BIT(RES4362_PWRSW_MAIN),            RES_DEPEND_SET, 0x040000cf, NULL},
	{PMURES_BIT(RES4362_SR_MAIN),               RES_DEPEND_SET, 0x040040cf, NULL},
	{PMURES_BIT(RES4362_SLEEP_MAIN),            RES_DEPEND_SET, 0x0400c0cf, NULL},
	{PMURES_BIT(RES4362_DIG_CORE_RDY),          RES_DEPEND_SET, 0x060007cf, NULL},
	{PMURES_BIT(RES4362_CORE_RDY_AUX),          RES_DEPEND_SET, 0x040038cf, NULL},
	{PMURES_BIT(RES4362_ALP_AVAIL),             RES_DEPEND_SET, 0x060207cf, NULL},
	{PMURES_BIT(RES4362_RADIO_AUX_PU),          RES_DEPEND_SET, 0x040438df, NULL},
	{PMURES_BIT(RES4362_MINIPMU_AUX_PU),        RES_DEPEND_SET, 0x041438df, NULL},
	{PMURES_BIT(RES4362_CORE_RDY_MAIN),         RES_DEPEND_SET, 0x0401c0cf, NULL},
	{PMURES_BIT(RES4362_RADIO_MAIN_PU),         RES_DEPEND_SET, 0x0441c0df, NULL},
	{PMURES_BIT(RES4362_MINIPMU_MAIN_PU),       RES_DEPEND_SET, 0x04c1c0df, NULL},
	{PMURES_BIT(RES4362_PCIE_EP_PU),            RES_DEPEND_SET, 0x040000cf, NULL},
	{PMURES_BIT(RES4362_COLD_START_WAIT),       RES_DEPEND_SET, 0x0000000f, NULL},
	{PMURES_BIT(RES4362_ARMHTAVAIL),            RES_DEPEND_SET, 0x060a07cf, NULL},
	{PMURES_BIT(RES4362_HT_AVAIL),              RES_DEPEND_SET, 0x060a07cf, NULL},
	{PMURES_BIT(RES4362_MACPHY_AUX_CLK_AVAIL),  RES_DEPEND_SET, 0x163e3fdf, NULL},
	{PMURES_BIT(RES4362_MACPHY_MAIN_CLK_AVAIL), RES_DEPEND_SET, 0x17cbc7df, NULL},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4362_res_updown)[] = {
	{RES4362_DUMMY,                 0x00220022},
	{RES4362_ABUCK,                 0x00c80022},
	{RES4362_PMU_SLEEP,             0x00c80022},
	{RES4362_MISCLDO_PU,            0x00bd0022},
	{RES4362_LDO3P3_PU,             0x01ad0022},
	{RES4362_FAST_LPO_AVAIL,        0x01500022},
	{RES4362_XTAL_PU,               0x05dc0022},
	{RES4362_XTAL_STABLE,           0x00220022},
	{RES4362_PWRSW_DIG,             0x009000ca},
	{RES4362_SR_DIG,                0x00A000A0},
	{RES4362_SLEEP_DIG,             0x00220022},
	{RES4362_PWRSW_AUX,             0x039000ca},
	{RES4362_SR_AUX,                0x01400140},
	{RES4362_SLEEP_AUX,             0x00220022},
	{RES4362_PWRSW_MAIN,            0x039000ca},
	{RES4362_SR_MAIN,               0x01a001a0},
	{RES4362_SLEEP_MAIN,            0x00220022},
	{RES4362_DIG_CORE_RDY,          0x00220044},
	{RES4362_CORE_RDY_AUX,          0x00220044},
	{RES4362_ALP_AVAIL,             0x00220044},
	{RES4362_RADIO_AUX_PU,          0x006e0022},
	{RES4362_MINIPMU_AUX_PU,        0x00460022},
	{RES4362_CORE_RDY_MAIN,         0x00220022},
	{RES4362_RADIO_MAIN_PU,         0x006e0022},
	{RES4362_MINIPMU_MAIN_PU,       0x00460022},
	{RES4362_PCIE_EP_PU,            0x009000ca},
	{RES4362_COLD_START_WAIT,       0x00220022},
	{RES4362_ARMHTAVAIL,            0x00a80022},
	{RES4362_HT_AVAIL,              0x00a80022},
	{RES4362_MACPHY_AUX_CLK_AVAIL,  0x00640022},
	{RES4362_MACPHY_MAIN_CLK_AVAIL, 0x00640022},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4378b0_res_updown)[] = {
	{RES4378_ABUCK,			0x00c80022},
	{RES4378_PMU_SLEEP,		0x011c0022},
	{RES4378_MISC_LDO,		0x00c80022},
	{RES4378_XTAL_PU,		0x05dc0022},
	{RES4378_SR_DIG,		0x00700070},
	{RES4378_SR_AUX,		0x01800180},
	{RES4378_SR_MAIN,		0x01a001a0},
	{RES4378_RADIO_AUX_PU,		0x006e0022},
	{RES4378_MINIPMU_AUX_PU,	0x00460022},
	{RES4378_RADIO_MAIN_PU,		0x006e0022},
	{RES4378_MINIPMU_MAIN_PU,	0x00460022},
	{RES4378_CORE_RDY_CB,		0x00220022},
#ifdef BCMPCIE_TREFUP_HW_SUPPORT
	{RES4378_PWRSW_CB,              0x015e00ca},
#endif
	{RES4378_MACPHY_AUX_CLK_AVAIL,	0x00640022},
	{RES4378_MACPHY_MAIN_CLK_AVAIL,	0x00640022},
};

static pmu_res_depend_t BCMATTACHDATA(bcm4378b0_res_depend)[] = {
	{PMURES_BIT(RES4378_ABUCK),			RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4378_PMU_SLEEP),			RES_DEPEND_SET, 0x00000001, NULL},
	{PMURES_BIT(RES4378_MISC_LDO),			RES_DEPEND_SET, 0x00000007, NULL},
	{PMURES_BIT(RES4378_LDO3P3_PU),			RES_DEPEND_SET, 0x00000001, NULL},
	{PMURES_BIT(RES4378_FAST_LPO_AVAIL),		RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4378_XTAL_PU),			RES_DEPEND_SET, 0x00000007, NULL},
	{PMURES_BIT(RES4378_XTAL_STABLE),		RES_DEPEND_SET, 0x00000047, NULL},
	{PMURES_BIT(RES4378_PWRSW_DIG),			RES_DEPEND_SET, 0x060000ef, NULL},
	{PMURES_BIT(RES4378_SR_DIG),			RES_DEPEND_SET, 0x060001ef, NULL},
	{PMURES_BIT(RES4378_SLEEP_DIG),			RES_DEPEND_SET, 0x060003ef, NULL},
	{PMURES_BIT(RES4378_PWRSW_AUX),			RES_DEPEND_SET, 0x060000ef, NULL},
	{PMURES_BIT(RES4378_SR_AUX),			RES_DEPEND_SET, 0x060008ef, NULL},
	{PMURES_BIT(RES4378_SLEEP_AUX),			RES_DEPEND_SET, 0x060018ef, NULL},
	{PMURES_BIT(RES4378_PWRSW_MAIN),		RES_DEPEND_SET, 0x060000ef, NULL},
	{PMURES_BIT(RES4378_SR_MAIN),			RES_DEPEND_SET, 0x060040ef, NULL},
	{PMURES_BIT(RES4378_SLEEP_MAIN),		RES_DEPEND_SET, 0x0600c0ef, NULL},
	{PMURES_BIT(RES4378_CORE_RDY_DIG),		RES_DEPEND_SET, 0x060007ef, NULL},
	{PMURES_BIT(RES4378_CORE_RDY_AUX),		RES_DEPEND_SET, 0x06023fef, NULL},
	{PMURES_BIT(RES4378_ALP_AVAIL),			RES_DEPEND_SET, 0x000000c7, NULL},
	{PMURES_BIT(RES4378_RADIO_AUX_PU),		RES_DEPEND_SET, 0x06063fff, NULL},
	{PMURES_BIT(RES4378_MINIPMU_AUX_PU),		RES_DEPEND_SET, 0x06163fff, NULL},
	{PMURES_BIT(RES4378_CORE_RDY_MAIN),		RES_DEPEND_SET, 0x0603c7ef, NULL},
	{PMURES_BIT(RES4378_RADIO_MAIN_PU),		RES_DEPEND_SET, 0x0643c7ff, NULL},
	{PMURES_BIT(RES4378_MINIPMU_MAIN_PU),		RES_DEPEND_SET, 0x06c3c7ff, NULL},
#ifdef BCMPCIE_TREFUP_HW_SUPPORT
	{PMURES_BIT(RES4378_CORE_RDY_CB),		RES_DEPEND_SET, 0x0400002f, NULL},
#else
	{PMURES_BIT(RES4378_CORE_RDY_CB),		RES_DEPEND_SET, 0x040000ef, NULL},
#endif
	{PMURES_BIT(RES4378_PWRSW_CB),			RES_DEPEND_SET, 0x0000002f, NULL},
	{PMURES_BIT(RES4378_ARMHTAVAIL),		RES_DEPEND_SET, 0x000800c7, NULL},
	{PMURES_BIT(RES4378_HT_AVAIL),			RES_DEPEND_SET, 0x000800c7, NULL},
	{PMURES_BIT(RES4378_MACPHY_AUX_CLK_AVAIL),	RES_DEPEND_SET, 0x163e3fff, NULL},
	{PMURES_BIT(RES4378_MACPHY_MAIN_CLK_AVAIL),	RES_DEPEND_SET, 0x17cbc7ff, NULL},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4387b0_res_updown_qt)[] = {
	{RES4387_XTAL_PU,		0x012c0033},
	{RES4387_PWRSW_DIG,		0x38993899},
	{RES4387_PWRSW_AUX,		0x38993899},
	{RES4387_PWRSW_SCAN,		0x38993899},
	{RES4387_PWRSW_MAIN,		0x38993899},
	{RES4387_CORE_RDY_CB,		0x00960033},
};

static const pmu_res_subst_trans_tmr_t BCMATTACHDATA(bcm4387b0_res_subst_trans_tmr_qt)[] = {
	{RES4387_PWRSW_DIG,		0, 0x38993800},
	{RES4387_PWRSW_DIG,		1, 0x36000600},
	{RES4387_PWRSW_DIG,		2, 0x01000002},

	{RES4387_PWRSW_AUX,		0, 0x38993800},
	{RES4387_PWRSW_AUX,		1, 0x36000600},
	{RES4387_PWRSW_AUX,		2, 0x01000002},

	{RES4387_PWRSW_SCAN,		0, 0x38993800},
	{RES4387_PWRSW_SCAN,		1, 0x36000600},
	{RES4387_PWRSW_SCAN,		2, 0x01000002},

	{RES4387_PWRSW_MAIN,		0, 0x38993800},
	{RES4387_PWRSW_MAIN,		1, 0x36000600},
	{RES4387_PWRSW_MAIN,		2, 0x01000002},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4387b0_res_updown)[] = {
	{RES4387_PMU_SLEEP,		0x00960022},
	{RES4387_MISC_LDO,		0x00320022},
	{RES4387_XTAL_HQ,		0x00210021},
	{RES4387_XTAL_PU,		0x03e80033},
	{RES4387_PWRSW_DIG,		0x04b002bc},
	{RES4387_PWRSW_AUX,		0x060e03bc},
	{RES4387_PWRSW_SCAN,		0x060e03bc},
	{RES4387_PWRSW_MAIN,		0x060e03bc},
	{RES4387_CORE_RDY_CB,		0x000a0033},
	{RES4387_PWRSW_CB,		0x006400ca},
};

static const pmu_res_subst_trans_tmr_t BCMATTACHDATA(bcm4387b0_res_subst_trans_tmr)[] = {
	{RES4387_PWRSW_DIG,		0, 0x04b002bc},
	{RES4387_PWRSW_DIG,		1, 0x02500210},
	{RES4387_PWRSW_DIG,		2, 0x00a00010},

	{RES4387_PWRSW_AUX,		0, 0x060e03ac},
	{RES4387_PWRSW_AUX,		1, 0x028a0134},
	{RES4387_PWRSW_AUX,		2, 0x00320002},

	{RES4387_PWRSW_MAIN,		0, 0x060e03b2},
	{RES4387_PWRSW_MAIN,		1, 0x028a0134},
	{RES4387_PWRSW_MAIN,		2, 0x00320002},

	{RES4387_PWRSW_SCAN,		0, 0x060e03b2},
	{RES4387_PWRSW_SCAN,		1, 0x028a0134},
	{RES4387_PWRSW_SCAN,		2, 0x00320002},
};

static pmu_res_depend_t BCMATTACHDATA(bcm4387b0_res_depend)[] = {
	{PMURES_BIT(RES4387_DUMMY),			RES_DEPEND_SET,  0x0, NULL},
	{PMURES_BIT(RES4387_RESERVED_1),		RES_DEPEND_SET,  0x0, NULL},
	{PMURES_BIT(RES4387_PMU_SLEEP),			RES_DEPEND_SET,  0x1, NULL},
	{PMURES_BIT(RES4387_MISC_LDO),			RES_DEPEND_SET,  0x5, NULL},
	{PMURES_BIT(RES4387_RESERVED_4),		RES_DEPEND_SET,  0x0, NULL},
	{PMURES_BIT(RES4387_XTAL_HQ),			RES_DEPEND_SET,  0xc5, NULL},
	{PMURES_BIT(RES4387_XTAL_PU),			RES_DEPEND_SET,  0x5, NULL},
	{PMURES_BIT(RES4387_XTAL_STABLE),		RES_DEPEND_SET,  0x45, NULL},
	{PMURES_BIT(RES4387_PWRSW_DIG),			RES_DEPEND_SET,  0x060000CD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_BTMAIN),		RES_DEPEND_SET,  0xCD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_BTSC),		RES_DEPEND_SET,  0xC5, NULL},
	{PMURES_BIT(RES4387_PWRSW_AUX),			RES_DEPEND_SET,  0xCD, NULL},
	{PMURES_BIT(RES4387_PWRSW_SCAN),		RES_DEPEND_SET,  0xCD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_SCAN),		RES_DEPEND_SET,  0x060010CD, NULL},
	{PMURES_BIT(RES4387_PWRSW_MAIN),		RES_DEPEND_SET,  0xCD, NULL},
	{PMURES_BIT(RES4387_RESERVED_15),		RES_DEPEND_SET,  0x0, NULL},
	{PMURES_BIT(RES4387_RESERVED_16),		RES_DEPEND_SET,  0x0, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_DIG),		RES_DEPEND_SET,  0x060001CD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_AUX),		RES_DEPEND_SET,  0x060209CD, NULL},
	{PMURES_BIT(RES4387_ALP_AVAIL),			RES_DEPEND_SET,  0xC5, NULL},
	{PMURES_BIT(RES4387_RADIO_PU_AUX),		RES_DEPEND_SET,  0x060609CD, NULL},
	{PMURES_BIT(RES4387_RADIO_PU_SCAN),		RES_DEPEND_SET,  0x060030CD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_MAIN),		RES_DEPEND_SET,  0x060241CD, NULL},
	{PMURES_BIT(RES4387_RADIO_PU_MAIN),		RES_DEPEND_SET,  0x064241CD, NULL},
	{PMURES_BIT(RES4387_MACPHY_CLK_SCAN),		RES_DEPEND_SET,  0x162830CD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_CB),		RES_DEPEND_SET,  0x0400000D, NULL},
	{PMURES_BIT(RES4387_PWRSW_CB),			RES_DEPEND_SET,  0x0000000D, NULL},
	{PMURES_BIT(RES4387_ARMCLK_AVAIL),		RES_DEPEND_SET,  0x000800CD, NULL},
	{PMURES_BIT(RES4387_HT_AVAIL),			RES_DEPEND_SET,  0x000800CD, NULL},
	{PMURES_BIT(RES4387_MACPHY_CLK_AUX),		RES_DEPEND_SET,  0x161E09ED, NULL},
	{PMURES_BIT(RES4387_MACPHY_CLK_MAIN),		RES_DEPEND_SET,  0x16CA41ED, NULL},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4387c0_res_updown_topoff)[] = {
	{RES4387_PMU_SLEEP,		0x02000022},
	{RES4387_MISC_LDO,		0x00320022},
	{RES4387_SERDES_AFE_RET,	0x00010001},
	{RES4387_XTAL_HQ,		0x00210021},
	{RES4387_XTAL_PU,		0x03e80033},
	{RES4387_PWRSW_DIG,	        0x00d20102},
	{RES4387_PWRSW_AUX,	        0x01c201e2},
	{RES4387_PWRSW_SCAN,	        0x01020122},
	{RES4387_PWRSW_MAIN,	        0x02220242},
	{RES4387_CORE_RDY_CB,		0x000a0033},
	{RES4387_PWRSW_CB,		0x006400ca},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4387c0_res_updown)[] = {
#ifdef BCM_PMU_FLL_PU_MANAGE
	{RES4387_FAST_LPO_AVAIL,	0x00960001},
#endif
	{RES4387_PMU_SLEEP,		0x00960022},
	{RES4387_MISC_LDO,		0x00320022},
	{RES4387_XTAL_HQ,		0x00210021},
	{RES4387_XTAL_PU,		0x03e80033},
	{RES4387_PWRSW_DIG,	        0x01320172},
	{RES4387_PWRSW_AUX,	        0x01c201e2},
	{RES4387_PWRSW_SCAN,	        0x019201b2},
	{RES4387_PWRSW_MAIN,	        0x02220242},
	{RES4387_CORE_RDY_CB,		0x000a0033},
	{RES4387_PWRSW_CB,		0x006400ca},
};

static const pmu_res_subst_trans_tmr_t BCMATTACHDATA(bcm4387c0_res_subst_trans_tmr)[] = {
	{RES4387_PWRSW_DIG,		0, 0x01320142},
	{RES4387_PWRSW_DIG,		1, 0x00e2005a},
	{RES4387_PWRSW_DIG,		2, 0x00c20052},
	{RES4387_PWRSW_DIG,		3, 0x00020002},

	{RES4387_PWRSW_AUX,		0, 0x01c201b2},
	{RES4387_PWRSW_AUX,		1, 0x0172005a},
	{RES4387_PWRSW_AUX,		2, 0x01520052},
	{RES4387_PWRSW_AUX,		3, 0x00020002},

	{RES4387_PWRSW_MAIN,		0, 0x02220212},
	{RES4387_PWRSW_MAIN,		1, 0x01d2005a},
	{RES4387_PWRSW_MAIN,		2, 0x01b20052},
	{RES4387_PWRSW_MAIN,		3, 0x00020002},

	{RES4387_PWRSW_SCAN,		0, 0x01920182},
	{RES4387_PWRSW_SCAN,		1, 0x0142005a},
	{RES4387_PWRSW_SCAN,		2, 0x01220052},
	{RES4387_PWRSW_SCAN,		3, 0x00020002},
};

static pmu_res_depend_t BCMATTACHDATA(bcm4387c0_res_depend)[] = {
	{PMURES_BIT(RES4387_DUMMY),			RES_DEPEND_SET,  0x0, NULL},
	{PMURES_BIT(RES4387_FAST_LPO_AVAIL),		RES_DEPEND_SET,  0x0, NULL},
	{PMURES_BIT(RES4387_PMU_LP),			RES_DEPEND_SET,  0x1, NULL},
	{PMURES_BIT(RES4387_MISC_LDO),			RES_DEPEND_SET,  0x5, NULL},
	{PMURES_BIT(RES4387_SERDES_AFE_RET),		RES_DEPEND_SET,  0xD, NULL},
	{PMURES_BIT(RES4387_XTAL_HQ),			RES_DEPEND_SET,  0xC5, NULL},
	{PMURES_BIT(RES4387_XTAL_PU),			RES_DEPEND_SET,  0x5, NULL},
	{PMURES_BIT(RES4387_XTAL_STABLE),		RES_DEPEND_SET,  0x45, NULL},
	{PMURES_BIT(RES4387_PWRSW_DIG),			RES_DEPEND_SET,  0x060000DD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_BTMAIN),		RES_DEPEND_SET,  0xCD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_BTSC),		RES_DEPEND_SET,  0xC5, NULL},
	{PMURES_BIT(RES4387_PWRSW_AUX),			RES_DEPEND_SET,  0xCD, NULL},
	{PMURES_BIT(RES4387_PWRSW_SCAN),		RES_DEPEND_SET,  0xCD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_SCAN),		RES_DEPEND_SET,  0x060010DD, NULL},
	{PMURES_BIT(RES4387_PWRSW_MAIN),		RES_DEPEND_SET,  0xCD, NULL},
	{PMURES_BIT(RES4387_XTAL_PM_CLK),		RES_DEPEND_SET,  0xC5, NULL},
	{PMURES_BIT(RES4387_RESERVED_16),		RES_DEPEND_SET,  0x0, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_DIG),		RES_DEPEND_SET,  0x060001DD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_AUX),		RES_DEPEND_SET,  0x060209DD, NULL},
	{PMURES_BIT(RES4387_ALP_AVAIL),			RES_DEPEND_SET,  0x80C5, NULL},
	{PMURES_BIT(RES4387_RADIO_PU_AUX),		RES_DEPEND_SET,  0x060609DD, NULL},
	{PMURES_BIT(RES4387_RADIO_PU_SCAN),		RES_DEPEND_SET,  0x060030DD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_MAIN),		RES_DEPEND_SET,  0x060241DD, NULL},
	{PMURES_BIT(RES4387_RADIO_PU_MAIN),		RES_DEPEND_SET,  0x064241DD, NULL},
	{PMURES_BIT(RES4387_MACPHY_CLK_SCAN),		RES_DEPEND_SET,  0x1628B0DD, NULL},
	{PMURES_BIT(RES4387_CORE_RDY_CB),		RES_DEPEND_SET,  0x0400001D, NULL},
	{PMURES_BIT(RES4387_PWRSW_CB),			RES_DEPEND_SET,  0x0000001D, NULL},
	{PMURES_BIT(RES4387_ARMCLK_AVAIL),		RES_DEPEND_SET,  0x000880CD, NULL},
	{PMURES_BIT(RES4387_HT_AVAIL),			RES_DEPEND_SET,  0x000880CD, NULL},
	{PMURES_BIT(RES4387_MACPHY_CLK_AUX),		RES_DEPEND_SET,  0x161E89FD, NULL},
	{PMURES_BIT(RES4387_MACPHY_CLK_MAIN),		RES_DEPEND_SET,  0x16CAC1FD, NULL},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4388a0_res_updown_qt)[] = {
	{RES4388_XTAL_PU,	0x012c0033},
	{RES4388_PWRSW_DIG,	0x38993899},
	{RES4388_PWRSW_AUX,	0x38993899},
	{RES4388_PWRSW_SCAN,	0x38993899},
	{RES4388_PWRSW_MAIN,	0x38993899},
	{RES4388_CORE_RDY_CB,	0x00960033},
};

static const pmu_res_subst_trans_tmr_t BCMATTACHDATA(bcm4388a0_res_subst_trans_tmr_qt)[] = {
	{RES4388_PWRSW_DIG,	0, 0x38993800},
	{RES4388_PWRSW_DIG,	1, 0x36c00600},
	{RES4388_PWRSW_DIG,	2, 0x360005a0},
	{RES4388_PWRSW_DIG,	3, 0x01000002},

	{RES4388_PWRSW_AUX,	0, 0x38993800},
	{RES4388_PWRSW_AUX,	1, 0x36c00600},
	{RES4388_PWRSW_AUX,	2, 0x360005a0},
	{RES4388_PWRSW_AUX,	3, 0x01000002},

	{RES4388_PWRSW_MAIN,	0, 0x38993800},
	{RES4388_PWRSW_MAIN,	1, 0x36c00600},
	{RES4388_PWRSW_MAIN,	2, 0x360005a0},
	{RES4388_PWRSW_MAIN,	3, 0x01000002},

	{RES4388_PWRSW_SCAN,	0, 0x38993800},
	{RES4388_PWRSW_SCAN,	1, 0x33c00600},
	{RES4388_PWRSW_SCAN,	2, 0x330005a0},
	{RES4388_PWRSW_SCAN,	3, 0x01000002},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4388a0_res_updown)[] = {
#ifdef BCM_PMU_FLL_PU_MANAGE
	{RES4388_FAST_LPO_AVAIL,	0x00960001},
#endif /* BCM_PMU_FLL_PU_MANAGE */
	{RES4388_PMU_LP,		0x00960022},
	{RES4388_MISC_LDO,		0x00320022},
	{RES4388_XTAL_HQ,		0x00210021},
	{RES4388_XTAL_PU,		0x03e80033},
	{RES4388_PWRSW_DIG,		0x042c0349},
	{RES4388_PWRSW_AUX,		0x0740046a},
	{RES4388_PWRSW_SCAN,		0x03c802e8},
	{RES4388_PWRSW_MAIN,		0x08080532},
	{RES4388_CORE_RDY_CB,		0x000a0033},
	{RES4388_PWRSW_CB,		0x006400ca},
	{RES4388_MACPHY_CLK_MAIN,	0x00860022},
};

static const pmu_res_subst_trans_tmr_t BCMATTACHDATA(bcm4388a0_res_subst_trans_tmr)[] = {
	{RES4388_PWRSW_DIG,	0, 0x0428033c},
	{RES4388_PWRSW_DIG,	1, 0x028c0210},
	{RES4388_PWRSW_DIG,	2, 0x01cc01b0},
	{RES4388_PWRSW_DIG,	3, 0x00a00010},

	{RES4388_PWRSW_AUX,	0, 0x0740045a},
	{RES4388_PWRSW_AUX,	1, 0x03580202},
	{RES4388_PWRSW_AUX,	2, 0x02f801a2},
	{RES4388_PWRSW_AUX,	3, 0x00a00002},

	{RES4388_PWRSW_MAIN,	0, 0x08080522},
	{RES4388_PWRSW_MAIN,	1, 0x04200202},
	{RES4388_PWRSW_MAIN,	2, 0x03c001a2},
	{RES4388_PWRSW_MAIN,	3, 0x00a00002},

	{RES4388_PWRSW_SCAN,	0, 0x03c402d8},
	{RES4388_PWRSW_SCAN,	1, 0x02280210},
	{RES4388_PWRSW_SCAN,	2, 0x016801b0},
	{RES4388_PWRSW_SCAN,	3, 0x00a00010},
};

static pmu_res_depend_t BCMATTACHDATA(bcm4388a0_res_depend)[] = {
	{PMURES_BIT(RES4388_DUMMY),		RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4388_FAST_LPO_AVAIL),	RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4388_PMU_LP),		RES_DEPEND_SET, 0x00000001, NULL},
	{PMURES_BIT(RES4388_MISC_LDO),		RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4388_SERDES_AFE_RET),	RES_DEPEND_SET, 0x0000000d, NULL},
	{PMURES_BIT(RES4388_XTAL_HQ),		RES_DEPEND_SET, 0x000000c5, NULL},
	{PMURES_BIT(RES4388_XTAL_PU),		RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4388_XTAL_STABLE),	RES_DEPEND_SET, 0x00000045, NULL},
	{PMURES_BIT(RES4388_PWRSW_DIG),		RES_DEPEND_SET, 0x060000dd, NULL},
	{PMURES_BIT(RES4388_BTMC_TOP_RDY),	RES_DEPEND_SET, 0x000000cd, NULL},
	{PMURES_BIT(RES4388_BTSC_TOP_RDY),	RES_DEPEND_SET, 0x000000c5, NULL},
	{PMURES_BIT(RES4388_PWRSW_AUX),		RES_DEPEND_SET, 0x000000cd, NULL},
	{PMURES_BIT(RES4388_PWRSW_SCAN),	RES_DEPEND_SET, 0x000000cd, NULL},
	{PMURES_BIT(RES4388_CORE_RDY_SCAN),	RES_DEPEND_SET, 0x060211dd, NULL},
	{PMURES_BIT(RES4388_PWRSW_MAIN),	RES_DEPEND_SET, 0x000000cd, NULL},
	{PMURES_BIT(RES4388_RESERVED_15),	RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4388_RESERVED_16),	RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4388_CORE_RDY_DIG),	RES_DEPEND_SET, 0x060001dd, NULL},
	{PMURES_BIT(RES4388_CORE_RDY_AUX),	RES_DEPEND_SET, 0x060209dd, NULL},
	{PMURES_BIT(RES4388_ALP_AVAIL),		RES_DEPEND_SET, 0x000000c5, NULL},
	{PMURES_BIT(RES4388_RADIO_PU_AUX),	RES_DEPEND_SET, 0x060609dd, NULL},
	{PMURES_BIT(RES4388_RADIO_PU_SCAN),	RES_DEPEND_SET, 0x060231dd, NULL},
	{PMURES_BIT(RES4388_CORE_RDY_MAIN),	RES_DEPEND_SET, 0x060241dd, NULL},
	{PMURES_BIT(RES4388_RADIO_PU_MAIN),	RES_DEPEND_SET, 0x064241dd, NULL},
	{PMURES_BIT(RES4388_MACPHY_CLK_SCAN),	RES_DEPEND_SET, 0x162a31fd, NULL},
	{PMURES_BIT(RES4388_CORE_RDY_CB),	RES_DEPEND_SET, 0x040000dd, NULL},
	{PMURES_BIT(RES4388_PWRSW_CB),		RES_DEPEND_SET, 0x000000dd, NULL},
	{PMURES_BIT(RES4388_ARMCLKAVAIL),	RES_DEPEND_SET, 0x000800cd, NULL},
	{PMURES_BIT(RES4388_HT_AVAIL),		RES_DEPEND_SET, 0x000800cd, NULL},
	{PMURES_BIT(RES4388_MACPHY_CLK_AUX),	RES_DEPEND_SET, 0x161e09fd, NULL},
	{PMURES_BIT(RES4388_MACPHY_CLK_MAIN),	RES_DEPEND_SET, 0x16ca41fd, NULL},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4389b0_res_updown_qt)[] = {
	{RES4389_XTAL_PU,	0x012c0033},
	{RES4389_PWRSW_DIG,	0x38993899},
	{RES4389_PWRSW_AUX,	0x38993899},
	{RES4389_PWRSW_SCAN,	0x38993899},
	{RES4389_PWRSW_MAIN,	0x38993899},
	{RES4389_CORE_RDY_CB,	0x00960033},
};

static const pmu_res_subst_trans_tmr_t BCMATTACHDATA(bcm4389b0_res_subst_trans_tmr_qt)[] = {
	{RES4389_PWRSW_DIG,	0, 0x38993800},
	{RES4389_PWRSW_DIG,	1, 0x36c00600},
	{RES4389_PWRSW_DIG,	2, 0x360005a0},
	{RES4389_PWRSW_DIG,	3, 0x01000002},

	{RES4389_PWRSW_AUX,	0, 0x38993800},
	{RES4389_PWRSW_AUX,	1, 0x36c00600},
	{RES4389_PWRSW_AUX,	2, 0x360005a0},
	{RES4389_PWRSW_AUX,	3, 0x01000002},

	{RES4389_PWRSW_MAIN,	0, 0x38993800},
	{RES4389_PWRSW_MAIN,	1, 0x36c00600},
	{RES4389_PWRSW_MAIN,	2, 0x360005a0},
	{RES4389_PWRSW_MAIN,	3, 0x01000002},

	{RES4389_PWRSW_SCAN,	0, 0x38993800},
	{RES4389_PWRSW_SCAN,	1, 0x33c00600},
	{RES4389_PWRSW_SCAN,	2, 0x330005a0},
	{RES4389_PWRSW_SCAN,	3, 0x01000002},
};

static const pmu_res_updown_t BCMATTACHDATA(bcm4389b0_res_updown)[] = {
#ifdef BCM_PMU_FLL_PU_MANAGE
	{RES4389_FAST_LPO_AVAIL,	0x001e0001},
#endif /* BCM_PMU_FLL_PU_MANAGE */
	{RES4389_PMU_LP,		0x00960022},
	{RES4389_MISC_LDO,		0x00320022},
	{RES4389_XTAL_HQ,		0x00210021},
	{RES4389_XTAL_PU,		0x03e80033},
	{RES4389_PWRSW_DIG,		0x042c0349},
	{RES4389_PWRSW_AUX,		0x0740046a},
	{RES4389_PWRSW_SCAN,		0x03c802e8},
	{RES4389_PWRSW_MAIN,		0x08080532},
	{RES4389_CORE_RDY_CB,		0x000a0033},
	{RES4389_PWRSW_CB,		0x006400ca},
	{RES4389_MACPHY_CLK_MAIN,	0x00860022},
};

static const pmu_res_subst_trans_tmr_t BCMATTACHDATA(bcm4389b0_res_subst_trans_tmr)[] = {
	{RES4389_PWRSW_DIG,	0, 0x0428033c},
	{RES4389_PWRSW_DIG,	1, 0x028c0210},
	{RES4389_PWRSW_DIG,	2, 0x01cc01b0},
	{RES4389_PWRSW_DIG,	3, 0x00a00010},

	{RES4389_PWRSW_AUX,	0, 0x0740045a},
	{RES4389_PWRSW_AUX,	1, 0x03580202},
	{RES4389_PWRSW_AUX,	2, 0x02f801a2},
	{RES4389_PWRSW_AUX,	3, 0x00a00002},

	{RES4389_PWRSW_MAIN,	0, 0x08080522},
	{RES4389_PWRSW_MAIN,	1, 0x04200202},
	{RES4389_PWRSW_MAIN,	2, 0x03c001a2},
	{RES4389_PWRSW_MAIN,	3, 0x00a00002},

	{RES4389_PWRSW_SCAN,	0, 0x03c402d8},
	{RES4389_PWRSW_SCAN,	1, 0x02280210},
	{RES4389_PWRSW_SCAN,	2, 0x016801b0},
	{RES4389_PWRSW_SCAN,	3, 0x00a00010},
};

static pmu_res_depend_t BCMATTACHDATA(bcm4389b0_res_depend)[] = {
	{PMURES_BIT(RES4389_DUMMY),		RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4389_FAST_LPO_AVAIL),	RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4389_PMU_LP),		RES_DEPEND_SET, 0x00000001, NULL},
	{PMURES_BIT(RES4389_MISC_LDO),		RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4389_SERDES_AFE_RET),	RES_DEPEND_SET, 0x0000000d, NULL},
	{PMURES_BIT(RES4389_XTAL_HQ),		RES_DEPEND_SET, 0x000000c5, NULL},
	{PMURES_BIT(RES4389_XTAL_PU),		RES_DEPEND_SET, 0x00000005, NULL},
	{PMURES_BIT(RES4389_XTAL_STABLE),	RES_DEPEND_SET, 0x00000045, NULL},
	{PMURES_BIT(RES4389_PWRSW_DIG),		RES_DEPEND_SET, 0x060000dd, NULL},
	{PMURES_BIT(RES4389_BTMC_TOP_RDY),	RES_DEPEND_SET, 0x000000cd, NULL},
	{PMURES_BIT(RES4389_BTSC_TOP_RDY),	RES_DEPEND_SET, 0x000000c5, NULL},
	{PMURES_BIT(RES4389_PWRSW_AUX),		RES_DEPEND_SET, 0x000000cd, NULL},
	{PMURES_BIT(RES4389_PWRSW_SCAN),	RES_DEPEND_SET, 0x000000cd, NULL},
	{PMURES_BIT(RES4389_CORE_RDY_SCAN),	RES_DEPEND_SET, 0x060211dd, NULL},
	{PMURES_BIT(RES4389_PWRSW_MAIN),	RES_DEPEND_SET, 0x000000cd, NULL},
	{PMURES_BIT(RES4389_RESERVED_15),	RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4389_RESERVED_16),	RES_DEPEND_SET, 0x00000000, NULL},
	{PMURES_BIT(RES4389_CORE_RDY_DIG),	RES_DEPEND_SET, 0x060001dd, NULL},
	{PMURES_BIT(RES4389_CORE_RDY_AUX),	RES_DEPEND_SET, 0x060209dd, NULL},
	{PMURES_BIT(RES4389_ALP_AVAIL),		RES_DEPEND_SET, 0x000000c5, NULL},
	{PMURES_BIT(RES4389_RADIO_PU_AUX),	RES_DEPEND_SET, 0x060609dd, NULL},
	{PMURES_BIT(RES4389_RADIO_PU_SCAN),	RES_DEPEND_SET, 0x060231dd, NULL},
	{PMURES_BIT(RES4389_CORE_RDY_MAIN),	RES_DEPEND_SET, 0x060241dd, NULL},
	{PMURES_BIT(RES4389_RADIO_PU_MAIN),	RES_DEPEND_SET, 0x064241dd, NULL},
	{PMURES_BIT(RES4389_MACPHY_CLK_SCAN),	RES_DEPEND_SET, 0x162a31fd, NULL},
	{PMURES_BIT(RES4389_CORE_RDY_CB),	RES_DEPEND_SET, 0x040000dd, NULL},
	{PMURES_BIT(RES4389_PWRSW_CB),		RES_DEPEND_SET, 0x000000dd, NULL},
	{PMURES_BIT(RES4389_ARMCLKAVAIL),	RES_DEPEND_SET, 0x000800cd, NULL},
	{PMURES_BIT(RES4389_HT_AVAIL),		RES_DEPEND_SET, 0x000800cd, NULL},
	{PMURES_BIT(RES4389_MACPHY_CLK_AUX),	RES_DEPEND_SET, 0x161e09fd, NULL},
	{PMURES_BIT(RES4389_MACPHY_CLK_MAIN),	RES_DEPEND_SET, 0x16ca41fd, NULL},
};

/** To enable avb timer clock feature */
void si_pmu_avbtimer_enable(si_t *sih, osl_t *osh, bool set_flag)
{
	uint32 min_mask = 0, max_mask = 0;
	pmuregs_t *pmu;
	uint origidx;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	if ((CHIPID(sih->chip) == BCM4360_CHIP_ID || CHIPID(sih->chip) == BCM43460_CHIP_ID) &&
		CHIPREV(sih->chiprev) >= 0x3) {
		int cst_ht = CST4360_RSRC_INIT_MODE(sih->chipst) & 0x1;
		if (cst_ht == 0) {
			/* Enable the AVB timers for proxd feature */
			min_mask = R_REG(osh, &pmu->min_res_mask);
			max_mask = R_REG(osh, &pmu->max_res_mask);
			if (set_flag) {
				max_mask |= PMURES_BIT(RES4360_AVB_PLL_PWRSW_PU);
				max_mask |= PMURES_BIT(RES4360_PCIE_TL_CLK_AVAIL);
				min_mask |= PMURES_BIT(RES4360_AVB_PLL_PWRSW_PU);
				min_mask |= PMURES_BIT(RES4360_PCIE_TL_CLK_AVAIL);
				W_REG(osh, &pmu->min_res_mask, min_mask);
				W_REG(osh, &pmu->max_res_mask, max_mask);
			} else {
				AND_REG(osh, &pmu->min_res_mask,
					~PMURES_BIT(RES4360_AVB_PLL_PWRSW_PU));
				AND_REG(osh, &pmu->min_res_mask,
					~PMURES_BIT(RES4360_PCIE_TL_CLK_AVAIL));
				AND_REG(osh, &pmu->max_res_mask,
					~PMURES_BIT(RES4360_AVB_PLL_PWRSW_PU));
				AND_REG(osh, &pmu->max_res_mask,
					~PMURES_BIT(RES4360_PCIE_TL_CLK_AVAIL));
			}
			/* Need to wait 100 millisecond for the uptime */
			OSL_DELAY(100);
		}
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

/**
 * Determines min/max rsrc masks. Normally hardware contains these masks, and software reads the
 * masks from hardware. Note that masks are sometimes dependent on chip straps.
 */
static void
si_pmu_res_masks(si_t *sih, uint32 *pmin, uint32 *pmax)
{
	uint32 min_mask = 0, max_mask = 0;

	/* determine min/max rsrc masks */
	switch (CHIPID(sih->chip)) {
	case BCM4360_CHIP_ID:
	case BCM4352_CHIP_ID:
		if (CHIPREV(sih->chiprev) >= 0x4) {
			min_mask = 0x103;
		}
		/* Continue - Don't break */
	case BCM43460_CHIP_ID:
	case BCM43526_CHIP_ID:
		if (CHIPREV(sih->chiprev) >= 0x3) {
			/* PR 110203 */
			int cst_ht = CST4360_RSRC_INIT_MODE(sih->chipst) & 0x1;
			if (cst_ht == 0)
				max_mask = 0x1ff;
		}
		break;

	CASE_BCM43602_CHIP:
		/* as a bare minimum, have ALP clock running */
		min_mask = PMURES_BIT(RES43602_LPLDO_PU)  | PMURES_BIT(RES43602_REGULATOR)      |
			PMURES_BIT(RES43602_PMU_SLEEP)    | PMURES_BIT(RES43602_XTALLDO_PU)     |
			PMURES_BIT(RES43602_SERDES_PU)    | PMURES_BIT(RES43602_BBPLL_PWRSW_PU) |
			PMURES_BIT(RES43602_SR_CLK_START) | PMURES_BIT(RES43602_SR_PHY_PWRSW)   |
			PMURES_BIT(RES43602_SR_SUBCORE_PWRSW) | PMURES_BIT(RES43602_XTAL_PU)    |
			PMURES_BIT(RES43602_PERST_OVR)    | PMURES_BIT(RES43602_SR_CLK_STABLE)  |
			PMURES_BIT(RES43602_SR_SAVE_RESTORE) | PMURES_BIT(RES43602_SR_SLEEP)    |
			PMURES_BIT(RES43602_LQ_START)     | PMURES_BIT(RES43602_LQ_AVAIL)       |
			PMURES_BIT(RES43602_WL_CORE_RDY)  |
			PMURES_BIT(RES43602_ALP_AVAIL);

		if (sih->chippkg == BCM43602_12x12_PKG_ID) /* LPLDO WAR */
			min_mask &= ~PMURES_BIT(RES43602_LPLDO_PU);

		max_mask = (1<<3) | min_mask          | PMURES_BIT(RES43602_RADIO_PU)        |
			PMURES_BIT(RES43602_RFLDO_PU) | PMURES_BIT(RES43602_HT_START)        |
			PMURES_BIT(RES43602_HT_AVAIL) | PMURES_BIT(RES43602_MACPHY_CLKAVAIL);

#if defined(SAVERESTORE)
		/* min_mask is updated after SR code is downloaded to txfifo */
		if (SR_ENAB() && sr_isenab(sih)) {
			ASSERT(sih->chippkg != BCM43602_12x12_PKG_ID);
			min_mask = PMURES_BIT(RES43602_LPLDO_PU);
		}
#endif /* SAVERESTORE */
		break;

	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		/* Set the bits for all resources in the max mask except for the SR Engine */
		max_mask = 0x7FFFFFFF;
		break;
	case BCM4369_CHIP_GRPID:
		min_mask = 0x64fffff;
#if defined(SAVERESTORE)
		if (SR_ENAB() && sr_isenab(sih)) {
			if (si_get_nvram_rfldo3p3_war(sih)) {
				min_mask = 0x0000011;
			} else {
				min_mask = 0x0000001;
			}
		}
#endif /* SAVERESTORE */
		max_mask = 0x7FFFFFFF;
		break;
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		min_mask = 0x064fffff;
#if defined(SAVERESTORE)
		if (SR_ENAB()) {
			if (!sr_isenab(sih)) {
				min_mask = 0x064fffff;
			} else {
				min_mask = PMURES_BIT(RES4378_DUMMY);
			}
		}
#endif /* SAVERESTORE */
		max_mask = 0x7FFFFFFF;
		break;
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
		min_mask = 0x64fffff;
#if defined(SAVERESTORE)
		if (SR_ENAB()) {
			if (sr_isenab(sih)) {
				min_mask = PMURES_BIT(RES4387_DUMMY);
			} else {
				min_mask = pmu_corereg(sih, SI_CC_IDX, min_res_mask, 0, 0);
				if (PMU_FLL_PU_ENAB()) {
					min_mask |= PMURES_BIT(RES4387_FAST_LPO_AVAIL) |
						PMURES_BIT(RES4387_PMU_LP);
				}
			}
		}
#endif /* SAVERESTORE */

		max_mask = 0x7FFFFFFF;
		break;

	case BCM4388_CHIP_GRPID:
		min_mask = 0x64fffff;
#if defined(SAVERESTORE)
		if (SR_ENAB()) {
			if (sr_isenab(sih)) {
				min_mask = PMURES_BIT(RES4388_DUMMY);
			} else {
				min_mask = pmu_corereg(sih, SI_CC_IDX, min_res_mask, 0, 0);
				if (PMU_FLL_PU_ENAB()) {
					min_mask |= PMURES_BIT(RES4388_FAST_LPO_AVAIL) |
						PMURES_BIT(RES4388_PMU_LP);
				}
			}
		}
#endif /* SAVERESTORE */
		max_mask = 0x7FFFFFFF;
		break;

	case BCM4389_CHIP_GRPID:
		/*
		 * check later if this can be replaced with chip default value read from
		 * PMU register - min_res_mask and remove the code in SR_ENAB() portion
		 */
		min_mask = 0x64fffff;
#if defined(SAVERESTORE)
		if (SR_ENAB()) {
			if (sr_isenab(sih)) {
				min_mask = PMURES_BIT(RES4389_DUMMY);
			} else {
				min_mask = pmu_corereg(sih, SI_CC_IDX, min_res_mask, 0, 0);
				if (PMU_FLL_PU_ENAB()) {
					min_mask |= PMURES_BIT(RES4389_FAST_LPO_AVAIL) |
						PMURES_BIT(RES4389_PMU_LP);
				}
			}
		}
#endif /* SAVERESTORE */
		max_mask = 0x7FFFFFFF;
		break;

	case BCM4397_CHIP_GRPID:
		min_mask = 0x64fffff;
		max_mask = 0x7FFFFFFF;
	break;

	case BCM4362_CHIP_GRPID:
		min_mask = 0x64fffff;
#if defined(SAVERESTORE)
		if (SR_ENAB() && sr_isenab(sih)) {
			min_mask = (PMURES_BIT(RES4362_DUMMY));
		}
#endif /* SAVERESTORE */
		max_mask = 0x7FFFFFFF;
		break;

	default:
		PMU_ERROR(("MIN and MAX mask is not programmed\n"));
		break;
	}

	if (!FWSIGN_ENAB()) {
		/* nvram override */
		si_nvram_res_masks(sih, &min_mask, &max_mask);
	}

	*pmin = min_mask;
	*pmax = max_mask;
} /* si_pmu_res_masks */

/**
 * resource dependencies can change because of the host interface
 * selected, to work around an issue, or for more optimal power
 * savings after tape out
 */
#ifdef DUAL_PMU_SEQUENCE
static void
si_pmu_resdeptbl_upd(si_t *sih, osl_t *osh, pmuregs_t *pmu,
	const pmu_res_depend_t *restable, uint tablesz)
#else
static void
BCMATTACHFN(si_pmu_resdeptbl_upd)(si_t *sih, osl_t *osh, pmuregs_t *pmu,
	const pmu_res_depend_t *restable, uint tablesz)
#endif /* DUAL_PMU_SEQUENCE */
{
	uint i, rsrcs;

	if (tablesz == 0)
		return;

	ASSERT(restable != NULL);

	rsrcs = (sih->pmucaps & PCAP_RC_MASK) >> PCAP_RC_SHIFT;
	/* Program resource dependencies table */
	while (tablesz--) {
		if (restable[tablesz].filter != NULL &&
		    !(restable[tablesz].filter)(sih))
			continue;
		for (i = 0; i < rsrcs; i ++) {
			if ((restable[tablesz].res_mask &
			     PMURES_BIT(i)) == 0)
				continue;
			W_REG(osh, &pmu->res_table_sel, i);
			switch (restable[tablesz].action) {
				case RES_DEPEND_SET:
					PMU_MSG(("Changing rsrc %d res_dep_mask to 0x%x\n", i,
						restable[tablesz].depend_mask));
					W_REG(osh, &pmu->res_dep_mask,
					      restable[tablesz].depend_mask);
					break;
				case RES_DEPEND_ADD:
					PMU_MSG(("Adding 0x%x to rsrc %d res_dep_mask\n",
						restable[tablesz].depend_mask, i));
					OR_REG(osh, &pmu->res_dep_mask,
					       restable[tablesz].depend_mask);
					break;
				case RES_DEPEND_REMOVE:
					PMU_MSG(("Removing 0x%x from rsrc %d res_dep_mask\n",
						restable[tablesz].depend_mask, i));
					AND_REG(osh, &pmu->res_dep_mask,
						~restable[tablesz].depend_mask);
					break;
				default:
					ASSERT(0);
					break;
			}
		}
	}
} /* si_pmu_resdeptbl_upd */

static void
BCMATTACHFN(si_pmu_dep_table_fll_pu_fixup)(si_t *sih, osl_t *osh,
	pmu_res_depend_t *pmu_res_depend_table, uint pmu_res_depend_table_sz)
{
	uint i;

	if (!PMU_FLL_PU_ENAB()) {
		return;
	}

	switch (CHIPID(sih->chip)) {
	case BCM4387_CHIP_GRPID:
		for (i = 0; i < pmu_res_depend_table_sz; i ++) {
			if (pmu_res_depend_table[i].res_mask ==
				PMURES_BIT(RES4387_FAST_LPO_AVAIL)) {
				pmu_res_depend_table[i].depend_mask = PMURES_BIT(RES4387_DUMMY) |
					PMURES_BIT(RES4387_PMU_LP);
			} else if ((pmu_res_depend_table[i].res_mask !=
					PMURES_BIT(RES4387_DUMMY)) &&
				(pmu_res_depend_table[i].res_mask !=
					PMURES_BIT(RES4387_PMU_LP)) &&
				(pmu_res_depend_table[i].res_mask !=
					PMURES_BIT(RES4387_RESERVED_16))) {
				pmu_res_depend_table[i].depend_mask |=
					PMURES_BIT(RES4387_FAST_LPO_AVAIL);
			}
		}
		break;
	case BCM4388_CHIP_GRPID:
		for (i = 0; i < pmu_res_depend_table_sz; i ++) {
			if (pmu_res_depend_table[i].res_mask ==
				PMURES_BIT(RES4388_FAST_LPO_AVAIL)) {
				pmu_res_depend_table[i].depend_mask = PMURES_BIT(RES4388_DUMMY) |
					PMURES_BIT(RES4388_PMU_LP);
			} else if ((pmu_res_depend_table[i].res_mask !=
					PMURES_BIT(RES4388_DUMMY)) &&
				(pmu_res_depend_table[i].res_mask !=
					PMURES_BIT(RES4388_PMU_LP)) &&
				(pmu_res_depend_table[i].res_mask !=
					PMURES_BIT(RES4388_RESERVED_15)) &&
				(pmu_res_depend_table[i].res_mask !=
					PMURES_BIT(RES4388_RESERVED_16))) {
				pmu_res_depend_table[i].depend_mask |=
					PMURES_BIT(RES4388_FAST_LPO_AVAIL);
			}
		}
		break;
	case BCM4389_CHIP_GRPID:
		for (i = 0; i < pmu_res_depend_table_sz; i ++) {
			if (pmu_res_depend_table[i].res_mask ==
				PMURES_BIT(RES4389_FAST_LPO_AVAIL)) {
				pmu_res_depend_table[i].depend_mask = PMURES_BIT(RES4389_DUMMY) |
					PMURES_BIT(RES4389_PMU_LP);
			} else if ((pmu_res_depend_table[i].res_mask !=
					PMURES_BIT(RES4389_DUMMY)) &&
				(pmu_res_depend_table[i].res_mask !=
					PMURES_BIT(RES4389_PMU_LP)) &&
				(pmu_res_depend_table[i].res_mask !=
					PMURES_BIT(RES4389_RESERVED_16))) {
				pmu_res_depend_table[i].depend_mask |=
					PMURES_BIT(RES4389_FAST_LPO_AVAIL);
			}
		}
		break;
	default:
		PMU_MSG(("si_pmu_dep_table_fll_pu_fixup: unsupported chip!\n"));
		ASSERT(0);
		break;
	}
}

/** Initialize PMU hardware resources. */
void
BCMATTACHFN(si_pmu_res_init)(si_t *sih, osl_t *osh)
{
	pmuregs_t *pmu;
	uint origidx;
	const pmu_res_updown_t *pmu_res_updown_table = NULL;
	uint pmu_res_updown_table_sz = 0;
	const pmu_res_subst_trans_tmr_t *pmu_res_subst_trans_tmr_table = NULL;
	uint pmu_res_subst_trans_tmr_table_sz = 0;
	pmu_res_depend_t *pmu_res_depend_table = NULL;
	uint pmu_res_depend_table_sz = 0;
#ifndef BCM_BOOTLOADER
	const pmu_res_depend_t *pmu_res_depend_pciewar_table[2] = {NULL, NULL};
	uint pmu_res_depend_pciewar_table_sz[2] = {0, 0};
#endif /* BCM_BOOTLOADER */
	uint32 min_mask = 0, max_mask = 0;
	char name[8];
	const char *val;
	uint i, rsrcs;
	uint8   fastlpo_dis = fastlpo_dis_get();
	uint8   fastlpo_pcie_dis = fastlpo_pcie_dis_get();

	ASSERT(sih->cccaps & CC_CAP_PMU);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	/*
	 * Hardware contains the resource updown and dependency tables. Only if a chip has a
	 * hardware problem, software tables can be used to override hardware tables.
	 */
	switch (CHIPID(sih->chip)) {
	case BCM4360_CHIP_ID:
	case BCM4352_CHIP_ID:
		if (CHIPREV(sih->chiprev) < 4) {
			pmu_res_updown_table = bcm4360_res_updown;
			pmu_res_updown_table_sz = ARRAYSIZE(bcm4360_res_updown);
		} else {
			/* FOR 4360B1 */
			pmu_res_updown_table = bcm4360B1_res_updown;
			pmu_res_updown_table_sz = ARRAYSIZE(bcm4360B1_res_updown);
		}
		break;
	CASE_BCM43602_CHIP:
		pmu_res_updown_table = bcm43602_res_updown;
		pmu_res_updown_table_sz = ARRAYSIZE(bcm43602_res_updown);
		pmu_res_depend_table = bcm43602_res_depend;
		pmu_res_depend_table_sz = ARRAYSIZE(bcm43602_res_depend);
#ifndef BCM_BOOTLOADER
		pmu_res_depend_pciewar_table[0] = bcm43602_res_pciewar;
		pmu_res_depend_pciewar_table_sz[0] = ARRAYSIZE(bcm43602_res_pciewar);
		if (sih->chippkg == BCM43602_12x12_PKG_ID) { /* LPLDO WAR */
			pmu_res_depend_pciewar_table[1] = bcm43602_12x12_res_depend;
			pmu_res_depend_pciewar_table_sz[1] = ARRAYSIZE(bcm43602_12x12_res_depend);
		}
#endif /* !BCM_BOOTLOADER */
		break;
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		pmu_res_updown_table = bcm43012a0_res_updown_ds0;
		pmu_res_updown_table_sz = ARRAYSIZE(bcm43012a0_res_updown_ds0);
		pmu_res_depend_table = bcm43012a0_res_depend_ds0;
		pmu_res_depend_table_sz = ARRAYSIZE(bcm43012a0_res_depend_ds0);
		break;
	case BCM4369_CHIP_GRPID:
		/* fastlpo_dis is override for PMU1M, updown times are updated accordingly
		 * if PMU 1M is enabled only Resource Up/Down times are changed
		 * Also the Up/Down times are different for A0 and B0
		 */
		if (fastlpo_dis) {
			/* Only Resource Up/Down times are different b/w A0 and B0 */
			if (CHIPREV(sih->chiprev) == 0) {
				pmu_res_updown_table = bcm4369a0_res_updown;
				pmu_res_updown_table_sz = ARRAYSIZE(bcm4369a0_res_updown);
			} else {
				pmu_res_updown_table = bcm4369b0_res_updown;
				pmu_res_updown_table_sz = ARRAYSIZE(bcm4369b0_res_updown);
			}
		} else {
			if (fastlpo_pcie_dis) {
				PMU_ERROR(("INVALID: PCIE 1MHz disabled but PMU 1MHz enabled\n"));
				ASSERT(0);
			}
			/* Only Resource Up/Down times are different b/w A0 and B0 */
			if (CHIPREV(sih->chiprev) == 0) {
				pmu_res_updown_table = bcm4369a0_res_updown_fastlpo_pmu;
				pmu_res_updown_table_sz =
					ARRAYSIZE(bcm4369a0_res_updown_fastlpo_pmu);
			} else {
				pmu_res_updown_table = bcm4369b0_res_updown_fastlpo_pmu;
				pmu_res_updown_table_sz =
					ARRAYSIZE(bcm4369b0_res_updown_fastlpo_pmu);
			}
		}

		/* fastlpo_pcie_dis is override for PCIE1M, resource dependencies are updated
		 * if pcie 1M is enabled resource dependency are different
		 * for A0 and B0  chiprev there is no resource dependency change
		 */
		if (fastlpo_pcie_dis) {
			pmu_res_depend_table = bcm4369a0_res_depend;
			pmu_res_depend_table_sz = ARRAYSIZE(bcm4369a0_res_depend);
		} else {
			pmu_res_depend_table = bcm4369a0_res_depend_fastlpo_pcie;
			pmu_res_depend_table_sz = ARRAYSIZE(bcm4369a0_res_depend_fastlpo_pcie);
		}
		break;

	case BCM4362_CHIP_GRPID:
		pmu_res_updown_table = bcm4362_res_updown;
		pmu_res_updown_table_sz = ARRAYSIZE(bcm4362_res_updown);

		GCI_REG_NEW(sih, bt_smem_control1, (0xFF<<16), 0);

		si_pmu_chipcontrol(sih, PMU_CHIPCTL14,
			(PMU_CC14_MAIN_VDDB2VDDRET_UP_DLY_MASK |
			PMU_CC14_MAIN_VDDB2VDD_UP_DLY_MASK |
			PMU_CC14_AUX_VDDB2VDDRET_UP_DLY_MASK |
			PMU_CC14_AUX_VDDB2VDD_UP_DLY_MASK |
			PMU_CC14_PCIE_VDDB2VDDRET_UP_DLY_MASK |
			PMU_CC14_PCIE_VDDB2VDD_UP_DLY_MASK), 0);

		si_pmu_chipcontrol(sih, PMU_CHIPCTL15,
			(PMU_CC15_PCIE_VDDB_CURRENT_LIMIT_DELAY_MASK |
			PMU_CC15_PCIE_VDDB_FORCE_RPS_PWROK_DELAY_MASK), 0);

		si_pmu_chipcontrol(sih, PMU_CHIPCTL10,
			(PMU_CC10_PCIE_RESET0_CNT_SLOW_MASK |
			PMU_CC10_PCIE_RESET1_CNT_SLOW_MASK), 0);

		GCI_REG_NEW(sih, bt_smem_control0, (0xF<<16), 0);
		GCI_REG_NEW(sih, bt_smem_control0, (0xF<<24), 0);

		pmu_res_depend_table = bcm4362_res_depend;
		pmu_res_depend_table_sz = ARRAYSIZE(bcm4362_res_depend);
		break;

	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		if (SR_ENAB()) {
			pmu_res_updown_table = bcm4378b0_res_updown;
			pmu_res_updown_table_sz = ARRAYSIZE(bcm4378b0_res_updown);
			pmu_res_depend_table = bcm4378b0_res_depend;
			pmu_res_depend_table_sz = ARRAYSIZE(bcm4378b0_res_depend);
		}
		break;

	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
		if (SR_ENAB()) {
			if (ISSIM_ENAB(sih)) {
				if (PMUREV(sih->pmurev) == 39) {
					pmu_res_updown_table = bcm4387c0_res_updown;
					pmu_res_updown_table_sz =
						ARRAYSIZE(bcm4387c0_res_updown);

					pmu_res_subst_trans_tmr_table =
						bcm4387c0_res_subst_trans_tmr;
					pmu_res_subst_trans_tmr_table_sz =
						ARRAYSIZE(bcm4387c0_res_subst_trans_tmr);

					pmu_res_depend_table = bcm4387c0_res_depend;
					pmu_res_depend_table_sz = ARRAYSIZE(bcm4387c0_res_depend);
				} else {
					pmu_res_updown_table = bcm4387b0_res_updown_qt;
					pmu_res_updown_table_sz =
						ARRAYSIZE(bcm4387b0_res_updown_qt);

					pmu_res_subst_trans_tmr_table =
						bcm4387b0_res_subst_trans_tmr_qt;
					pmu_res_subst_trans_tmr_table_sz =
						ARRAYSIZE(bcm4387b0_res_subst_trans_tmr_qt);

					pmu_res_depend_table = bcm4387b0_res_depend;
					pmu_res_depend_table_sz = ARRAYSIZE(bcm4387b0_res_depend);
				}
			} else {
				if (PMUREV(sih->pmurev) == 39) {
					if (BCMSRTOPOFF_ENAB()) {
						pmu_res_updown_table = bcm4387c0_res_updown_topoff;
						pmu_res_updown_table_sz =
							ARRAYSIZE(bcm4387c0_res_updown_topoff);
					} else {
						pmu_res_updown_table = bcm4387c0_res_updown;
						pmu_res_updown_table_sz =
							ARRAYSIZE(bcm4387c0_res_updown);
					}

					pmu_res_subst_trans_tmr_table =
						bcm4387c0_res_subst_trans_tmr;
					pmu_res_subst_trans_tmr_table_sz =
						ARRAYSIZE(bcm4387c0_res_subst_trans_tmr);

					pmu_res_depend_table = bcm4387c0_res_depend;
					pmu_res_depend_table_sz = ARRAYSIZE(bcm4387c0_res_depend);

					if (PMU_FLL_PU_ENAB()) {
						si_pmu_dep_table_fll_pu_fixup(sih, osh,
							pmu_res_depend_table,
							pmu_res_depend_table_sz);
					}
				} else {
					pmu_res_updown_table = bcm4387b0_res_updown;
					pmu_res_updown_table_sz = ARRAYSIZE(bcm4387b0_res_updown);

					pmu_res_subst_trans_tmr_table =
						bcm4387b0_res_subst_trans_tmr;
					pmu_res_subst_trans_tmr_table_sz =
						ARRAYSIZE(bcm4387b0_res_subst_trans_tmr);

					pmu_res_depend_table = bcm4387b0_res_depend;
					pmu_res_depend_table_sz = ARRAYSIZE(bcm4387b0_res_depend);
				}
			}
		}
		break;

	case BCM4388_CHIP_GRPID:
		if (SR_ENAB()) {
			if (ISSIM_ENAB(sih)) {
				pmu_res_updown_table = bcm4388a0_res_updown_qt;
				pmu_res_updown_table_sz = ARRAYSIZE(bcm4388a0_res_updown_qt);

				pmu_res_subst_trans_tmr_table = bcm4388a0_res_subst_trans_tmr_qt;
				pmu_res_subst_trans_tmr_table_sz =
					ARRAYSIZE(bcm4388a0_res_subst_trans_tmr_qt);
			} else {
				pmu_res_updown_table = bcm4388a0_res_updown;
				pmu_res_updown_table_sz = ARRAYSIZE(bcm4388a0_res_updown);

				pmu_res_subst_trans_tmr_table = bcm4388a0_res_subst_trans_tmr;
				pmu_res_subst_trans_tmr_table_sz =
					ARRAYSIZE(bcm4388a0_res_subst_trans_tmr);
			}

			pmu_res_depend_table = bcm4388a0_res_depend;
			pmu_res_depend_table_sz = ARRAYSIZE(bcm4388a0_res_depend);

			if (PMU_FLL_PU_ENAB()) {
				si_pmu_dep_table_fll_pu_fixup(sih, osh,
					pmu_res_depend_table,
					pmu_res_depend_table_sz);
			}
		}
		break;

	case BCM4389_CHIP_GRPID:
		if (SR_ENAB()) {
			if (ISSIM_ENAB(sih)) {
				pmu_res_updown_table = bcm4389b0_res_updown_qt;
				pmu_res_updown_table_sz = ARRAYSIZE(bcm4389b0_res_updown_qt);

				pmu_res_subst_trans_tmr_table = bcm4389b0_res_subst_trans_tmr_qt;
				pmu_res_subst_trans_tmr_table_sz =
					ARRAYSIZE(bcm4389b0_res_subst_trans_tmr_qt);
			} else {
				pmu_res_updown_table = bcm4389b0_res_updown;
				pmu_res_updown_table_sz = ARRAYSIZE(bcm4389b0_res_updown);

				pmu_res_subst_trans_tmr_table = bcm4389b0_res_subst_trans_tmr;
				pmu_res_subst_trans_tmr_table_sz =
					ARRAYSIZE(bcm4389b0_res_subst_trans_tmr);
			}

			pmu_res_depend_table = bcm4389b0_res_depend;
			pmu_res_depend_table_sz = ARRAYSIZE(bcm4389b0_res_depend);
			if (PMU_FLL_PU_ENAB()) {
				si_pmu_dep_table_fll_pu_fixup(sih, osh,
					pmu_res_depend_table, pmu_res_depend_table_sz);
			}
		}
		break;

	default:
		break;
	}

	/* Program up/down timers */
	while (pmu_res_updown_table_sz--) {
		ASSERT(pmu_res_updown_table != NULL);
		PMU_MSG(("Changing rsrc %d res_updn_timer to 0x%x\n",
		         pmu_res_updown_table[pmu_res_updown_table_sz].resnum,
		         pmu_res_updown_table[pmu_res_updown_table_sz].updown));
		W_REG(osh, &pmu->res_table_sel,
		      pmu_res_updown_table[pmu_res_updown_table_sz].resnum);
		W_REG(osh, &pmu->res_updn_timer,
		      pmu_res_updown_table[pmu_res_updown_table_sz].updown);
	}

	if (!FWSIGN_ENAB()) {
		/* # resources */
		rsrcs = (sih->pmucaps & PCAP_RC_MASK) >> PCAP_RC_SHIFT;

		/* Apply nvram overrides to up/down timers */
		for (i = 0; i < rsrcs; i ++) {
			uint32 r_val;
			snprintf(name, sizeof(name), rstr_rDt, i);
			if ((val = getvar(NULL, name)) == NULL)
				continue;
			r_val = (uint32)bcm_strtoul(val, NULL, 0);
			/* PMUrev = 13, pmu resource updown times are 12 bits(0:11 DT, 16:27 UT) */
			/* OLD values are 8 bits for UT/DT, handle the old nvram format */
			if (PMUREV(sih->pmurev) >= 13) {
				if (r_val < (1 << 16)) {
					uint16 up_time = (r_val >> 8) & 0xFF;
					r_val &= 0xFF;
					r_val |= (up_time << 16);
				}
			}
			PMU_MSG(("Applying %s=%s to rsrc %d res_updn_timer\n", name, val, i));
			W_REG(osh, &pmu->res_table_sel, (uint32)i);
			W_REG(osh, &pmu->res_updn_timer, r_val);
		}
	}

	/* Program Rsrc Substate Transition Timer */
	while (pmu_res_subst_trans_tmr_table_sz --) {
		ASSERT(pmu_res_subst_trans_tmr_table != NULL);
		PMU_MSG(("Changing rsrc %d substate %d res_subst_trans_timer to 0x%x\n",
			pmu_res_subst_trans_tmr_table[pmu_res_subst_trans_tmr_table_sz].resnum,
			pmu_res_subst_trans_tmr_table[pmu_res_subst_trans_tmr_table_sz].substate,
			pmu_res_subst_trans_tmr_table[pmu_res_subst_trans_tmr_table_sz].tmr));
		W_REG(osh, &pmu->res_table_sel,
			pmu_res_subst_trans_tmr_table[pmu_res_subst_trans_tmr_table_sz].resnum |
			(pmu_res_subst_trans_tmr_table[pmu_res_subst_trans_tmr_table_sz].substate
			<< PMU_RES_SUBSTATE_SHIFT));
		W_REG(osh, &pmu->rsrc_substate_trans_tmr,
			pmu_res_subst_trans_tmr_table[pmu_res_subst_trans_tmr_table_sz].tmr);
	}

	/* Program resource dependencies table */
	si_pmu_resdeptbl_upd(sih, osh, pmu, pmu_res_depend_table, pmu_res_depend_table_sz);

	if (!FWSIGN_ENAB()) {
		/* Apply nvram overrides to dependencies masks */
		for (i = 0; i < rsrcs; i ++) {
			snprintf(name, sizeof(name), rstr_rDd, i);
			if ((val = getvar(NULL, name)) == NULL)
				continue;
			PMU_MSG(("Applying %s=%s to rsrc %d res_dep_mask\n", name, val, i));
			W_REG(osh, &pmu->res_table_sel, (uint32)i);
			W_REG(osh, &pmu->res_dep_mask, (uint32)bcm_strtoul(val, NULL, 0));
		}
	}

#if !defined(BCM_BOOTLOADER)
	/* Initial any chip interface dependent PMU rsrc by looking at the
	 * chipstatus register to figure the selected interface
	 */
	/* this should be a general change to cover all the chips.
	 * this also should validate the build where the dongle is
	 * built for SDIO but downloaded on PCIE dev
	 */
	if (BUSTYPE(sih->bustype) == PCI_BUS || BUSTYPE(sih->bustype) == SI_BUS) {
		bool is_pciedev = BCM43602_CHIP(sih->chip);

		for (i = 0; i < ARRAYSIZE(pmu_res_depend_pciewar_table); i++) {
			if (is_pciedev && pmu_res_depend_pciewar_table[i] &&
			    pmu_res_depend_pciewar_table_sz[i]) {
				si_pmu_resdeptbl_upd(sih, osh, pmu,
					pmu_res_depend_pciewar_table[i],
					pmu_res_depend_pciewar_table_sz[i]);
			}
		}
	}
#endif /* !BCM_BOOTLOADER */
	/* Determine min/max rsrc masks */
	si_pmu_res_masks(sih, &min_mask, &max_mask);
	/* Add min mask dependencies */
	min_mask |= si_pmu_res_deps(sih, osh, pmu, min_mask, FALSE);

#ifdef BCM_BOOTLOADER
	/* Apply nvram override to max mask */
	if ((val = getvar(NULL, "brmax")) != NULL) {
		PMU_MSG(("Applying brmax=%s to max_res_mask\n", val));
		max_mask = (uint32)bcm_strtoul(val, NULL, 0);
	}

	/* Apply nvram override to min mask */
	if ((val = getvar(NULL, "brmin")) != NULL) {
		PMU_MSG(("Applying brmin=%s to min_res_mask\n", val));
		min_mask = (uint32)bcm_strtoul(val, NULL, 0);
	}
#endif /* BCM_BOOTLOADER */

	/* apply new PLL setting if is ALP strap (need to close out
	 * if possible apply if is HT strap)
	 */
	if (((CHIPID(sih->chip) == BCM4360_CHIP_ID) || (CHIPID(sih->chip) == BCM4352_CHIP_ID)) &&
	    (CHIPREV(sih->chiprev) < 4) &&
	    ((CST4360_RSRC_INIT_MODE(sih->chipst) & 1) == 0)) {
		/* BBPLL */
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6, ~0, 0x09048562);
		/* AVB PLL */
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG14, ~0, 0x09048562);
		si_pmu_pllupd(sih);
	} else if (((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM4352_CHIP_ID)) &&
		(CHIPREV(sih->chiprev) >= 4) &&
		((CST4360_RSRC_INIT_MODE(sih->chipst) & 1) == 0)) {
		/* Changes for 4360B1 */

		/* Enable REFCLK bit 11 */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL1, 0x800, 0x800);

		/* BBPLL */
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6, ~0, 0x080004e2);
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG7, ~0, 0xE);
		/* AVB PLL */
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG14, ~0, 0x080004e2);
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG15, ~0, 0xE);
		si_pmu_pllupd(sih);
	}
	/* disable PLL open loop operation */
	si_pll_closeloop(sih);

	if (max_mask) {
		/* Ensure there is no bit set in min_mask which is not set in max_mask */
		max_mask |= min_mask;

		/* First set the bits which change from 0 to 1 in max, then update the
		 * min_mask register and then reset the bits which change from 1 to 0
		 * in max. This is required as the bit in MAX should never go to 0 when
		 * the corresponding bit in min is still 1. Similarly the bit in min cannot
		 * be 1 when the corresponding bit in max is still 0.
		 */
		OR_REG(osh, &pmu->max_res_mask, max_mask);
	} else {
		/* First set the bits which change from 0 to 1 in max, then update the
		 * min_mask register and then reset the bits which change from 1 to 0
		 * in max. This is required as the bit in MAX should never go to 0 when
		 * the corresponding bit in min is still 1. Similarly the bit in min cannot
		 * be 1 when the corresponding bit in max is still 0.
		 */
		if (min_mask)
			OR_REG(osh, &pmu->max_res_mask, min_mask);
	}

	/* Program min resource mask */
	if (min_mask) {
		PMU_MSG(("Changing min_res_mask to 0x%x\n", min_mask));
		W_REG(osh, &pmu->min_res_mask, min_mask);
	}

	/* Program max resource mask */
	if (max_mask) {
		PMU_MSG(("Changing max_res_mask to 0x%x\n", max_mask));
		W_REG(osh, &pmu->max_res_mask, max_mask);
	}
#if defined(SAVERESTORE) && defined(LDO3P3_MIN_RES_MASK)
	if (SR_ENAB()) {
		/* Set the default state for LDO3P3 protection */
		if (getintvar(NULL, rstr_ldo_prot) == 1) {
			si_pmu_min_res_ldo3p3_set(sih, osh, TRUE);
		}
	}
#endif /* SAVERESTORE && LDO3P3_MIN_RES_MASK */

	/* request htavail thru pcie core */
	if (((CHIPID(sih->chip) == BCM4360_CHIP_ID) || (CHIPID(sih->chip) == BCM4352_CHIP_ID)) &&
	    (BUSTYPE(sih->bustype) == PCI_BUS) &&
	    (CHIPREV(sih->chiprev) < 4)) {
		uint32 pcie_clk_ctl_st;

		pcie_clk_ctl_st = si_corereg(sih, 3, 0x1e0, 0, 0);
		si_corereg(sih, 3, 0x1e0, ~0, (pcie_clk_ctl_st | CCS_HTAREQ));
	}

	si_pmu_wait_for_steady_state(sih, osh, pmu);
	/* Add some delay; allow resources to come up and settle. */
	OSL_DELAY(2000);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
} /* si_pmu_res_init */

/* setup pll and query clock speed */
typedef struct {
	uint16	fref;	/* x-tal frequency in [hz] */
	uint8	xf;	/* x-tal index as contained in PMU control reg, see PMU programmers guide */
	uint8	p1div;
	uint8	p2div;
	uint8	ndiv_int;
	uint32	ndiv_frac;
} pmu1_xtaltab0_t;

/* 'xf' values corresponding to the 'xf' definition in the PMU control register */
/* unclear why this enum contains '_640_' since the PMU prog guide says nothing about that */
enum xtaltab0_640 {
	XTALTAB0_640_12000K = 1,
	XTALTAB0_640_13000K,
	XTALTAB0_640_14400K,
	XTALTAB0_640_15360K,
	XTALTAB0_640_16200K,
	XTALTAB0_640_16800K,
	XTALTAB0_640_19200K,
	XTALTAB0_640_19800K,
	XTALTAB0_640_20000K,
	XTALTAB0_640_24000K, /* warning: unknown in PMU programmers guide. seems incorrect. */
	XTALTAB0_640_25000K,
	XTALTAB0_640_26000K,
	XTALTAB0_640_30000K,
	XTALTAB0_640_33600K, /* warning: unknown in PMU programmers guide. seems incorrect. */
	XTALTAB0_640_37400K,
	XTALTAB0_640_38400K, /* warning: unknown in PMU programmers guide. seems incorrect. */
	XTALTAB0_640_40000K,
	XTALTAB0_640_48000K, /* warning: unknown in PMU programmers guide. seems incorrect. */
	XTALTAB0_640_52000K
};

/* the following table is based on 880Mhz fvco */
static const pmu1_xtaltab0_t BCMINITDATA(pmu1_xtaltab0_880)[] = {
	{12000,	1,	3,	22,	0x9,	0xFFFFEF},
	{13000,	2,	1,	6,	0xb,	0x483483},
	{14400,	3,	1,	10,	0xa,	0x1C71C7},
	{15360,	4,	1,	5,	0xb,	0x755555},
	{16200,	5,	1,	10,	0x5,	0x6E9E06},
	{16800,	6,	1,	10,	0x5,	0x3Cf3Cf},
	{19200,	7,	1,	4,	0xb,	0x755555},
	{19800,	8,	1,	11,	0x4,	0xA57EB},
	{20000,	9,	1,	11,	0x4,	0x0},
	{24000,	10,	3,	11,	0xa,	0x0},
	{25000,	11,	5,	16,	0xb,	0x0},
	{26000,	12,	1,	2,	0x10,	0xEC4EC4},
	{30000,	13,	3,	8,	0xb,	0x0},
	{33600,	14,	1,	2,	0xd,	0x186186},
	{38400,	15,	1,	2,	0xb,	0x755555},
	{40000,	16,	1,	2,	0xb,	0},
	{0,	0,	0,	0,	0,	0}
};

/* indices into pmu1_xtaltab0_880[] */
#define PMU1_XTALTAB0_880_12000K	0
#define PMU1_XTALTAB0_880_13000K	1
#define PMU1_XTALTAB0_880_14400K	2
#define PMU1_XTALTAB0_880_15360K	3
#define PMU1_XTALTAB0_880_16200K	4
#define PMU1_XTALTAB0_880_16800K	5
#define PMU1_XTALTAB0_880_19200K	6
#define PMU1_XTALTAB0_880_19800K	7
#define PMU1_XTALTAB0_880_20000K	8
#define PMU1_XTALTAB0_880_24000K	9
#define PMU1_XTALTAB0_880_25000K	10
#define PMU1_XTALTAB0_880_26000K	11
#define PMU1_XTALTAB0_880_30000K	12
#define PMU1_XTALTAB0_880_37400K	13
#define PMU1_XTALTAB0_880_38400K	14
#define PMU1_XTALTAB0_880_40000K	15

/* the following table is based on 1760Mhz fvco */
static const pmu1_xtaltab0_t BCMINITDATA(pmu1_xtaltab0_1760)[] = {
	{12000,	1,	3,	44,	0x9,	0xFFFFEF},
	{13000,	2,	1,	12,	0xb,	0x483483},
	{14400,	3,	1,	20,	0xa,	0x1C71C7},
	{15360,	4,	1,	10,	0xb,	0x755555},
	{16200,	5,	1,	20,	0x5,	0x6E9E06},
	{16800,	6,	1,	20,	0x5,	0x3Cf3Cf},
	{19200,	7,	1,	18,	0x5,	0x17B425},
	{19800,	8,	1,	22,	0x4,	0xA57EB},
	{20000,	9,	1,	22,	0x4,	0x0},
	{24000,	10,	3,	22,	0xa,	0x0},
	{25000,	11,	5,	32,	0xb,	0x0},
	{26000,	12,	1,	4,	0x10,	0xEC4EC4},
	{30000,	13,	3,	16,	0xb,	0x0},
	{38400,	14,	1,	10,	0x4,	0x955555},
	{40000,	15,	1,	4,	0xb,	0},
	{0,	0,	0,	0,	0,	0}
};

#define XTAL_FREQ_24000MHZ		24000
#define XTAL_FREQ_29985MHZ		29985
#define XTAL_FREQ_30000MHZ		30000
#define XTAL_FREQ_37400MHZ		37400
#define XTAL_FREQ_48000MHZ		48000
#define XTAL_FREQ_59970MHZ		59970

/* 'xf' values corresponding to the 'xf' definition in the PMU control register */
/* unclear why this enum contains '_960_' since the PMU prog guide says nothing about that */
enum xtaltab0_960 {
	XTALTAB0_960_12000K = 1,
	XTALTAB0_960_13000K,
	XTALTAB0_960_14400K,
	XTALTAB0_960_15360K,
	XTALTAB0_960_16200K,
	XTALTAB0_960_16800K,
	XTALTAB0_960_19200K,
	XTALTAB0_960_19800K,
	XTALTAB0_960_20000K,
	XTALTAB0_960_24000K, /* warning: unknown in PMU programmers guide. seems incorrect. */
	XTALTAB0_960_25000K,
	XTALTAB0_960_26000K,
	XTALTAB0_960_30000K,
	XTALTAB0_960_33600K, /* warning: unknown in PMU programmers guide. seems incorrect. */
	XTALTAB0_960_37400K,
	XTALTAB0_960_38400K, /* warning: unknown in PMU programmers guide. seems incorrect. */
	XTALTAB0_960_40000K,
	XTALTAB0_960_48000K, /* warning: unknown in PMU programmers guide. seems incorrect. */
	XTALTAB0_960_52000K,
	XTALTAB0_960_59970K
};

/**
 * given an x-tal frequency, this table specifies the PLL params to use to generate a 960Mhz output
 * clock. This output clock feeds the clock divider network. The defines of the form
 * PMU1_XTALTAB0_960_* index into this array.
 */
static const pmu1_xtaltab0_t BCMINITDATA(pmu1_xtaltab0_960)[] = {
/*	fref      xf     p1div   p2div  ndiv_int  ndiv_frac */
	{12000,   1,       1,      1,     0x50,   0x0     }, /* array index 0 */
	{13000,   2,       1,      1,     0x49,   0xD89D89},
	{14400,   3,       1,      1,     0x42,   0xAAAAAA},
	{15360,   4,       1,      1,     0x3E,   0x800000},
	{16200,   5,       1,      1,     0x3B,   0x425ED0},
	{16800,   6,       1,      1,     0x39,   0x249249},
	{19200,   7,       1,      1,     0x32,   0x0     },
	{19800,   8,       1,      1,     0x30,   0x7C1F07},
	{20000,   9,       1,      1,     0x30,   0x0     },
	{24000,   10,      1,      1,     0x28,   0x0     },
	{25000,   11,      1,      1,     0x26,   0x666666}, /* array index 10 */
	{26000,   12,      1,      1,     0x24,   0xEC4EC4},
	{30000,   13,      1,      1,     0x20,   0x0     },
	{33600,   14,      1,      1,     0x1C,   0x924924},
	{37400,   15,      2,      1,     0x33,   0x563EF9},
	{38400,   16,      2,      1,     0x32,   0x0	  },
	{40000,   17,      2,      1,     0x30,   0x0     },
	{48000,   18,      2,      1,     0x28,   0x0     },
	{52000,   19,      2,      1,     0x24,   0xEC4EC4}, /* array index 18 */
	{59970,   20,      0,      0,     0,      0       },
	/* TBD: will separate 59970 for 4387B0 for new pll scheme */
	{0,	      0,       0,      0,     0,      0	      }
};

static const pmu1_xtaltab0_t BCMINITDATA(pmu1_xtaltab0_4369_963)[] = {
/*	fref      xf     p1div   NA  ndiv_int  ndiv_frac */
	{12000,   1,       1,      1,     0x50,   0x40000}, /* array index 0 */
	{13000,   2,       1,      1,     0x4A,   0x13B14},
	{14400,   3,       1,      1,     0x42,   0xE0000},
	{15360,   4,       1,      1,     0x3E,   0xB2000},
	{16200,   5,       1,      1,     0x3B,   0x71C72},
	{16800,   6,       1,      1,     0x39,   0x52492},
	{19200,   7,       1,      1,     0x32,   0x28000},
	{19800,   8,       1,      1,     0x30,   0xA2E8C},
	{20000,   9,       1,      1,     0x30,   0x26666},
	{24000,   10,      1,      1,     0x28,   0x20000},
	{25000,   11,      1,      1,     0x26,   0x851EC}, /* array index 10 */
	{26000,   12,      1,      1,     0x25,   0x09D8A},
	{30000,   13,      1,      1,     0x20,   0x1999A},
	{33600,   14,      1,      1,     0x1C,   0xA9249},
	{37400,   15,      1,      1,     0x19,   0xBFA86},
	{38400,   16,      1,      1,     0x19,   0x14000},
	{40000,   17,      1,      1,     0x18,   0x13333},
	{48000,   18,      1,      1,     0x14,   0x10000},
	{52000,   19,      1,      1,     0x12,   0x84EC5}, /* array index 18 */
	{0,	      0,       0,      0,     0,      0	     }
};

static const pmu1_xtaltab0_t BCMINITDATA(pmu1_xtaltab0_4362_963)[] = {
/*	fref      xf     p1div   NA  ndiv_int  ndiv_frac */
	{12000,   1,       1,      1,     0x50,   0x40000}, /* array index 0 */
	{13000,   2,       1,      1,     0x4A,   0x13B14},
	{14400,   3,       1,      1,     0x42,   0xE0000},
	{15360,   4,       1,      1,     0x3E,   0xB2000},
	{16200,   5,       1,      1,     0x3B,   0x71C72},
	{16800,   6,       1,      1,     0x39,   0x52492},
	{19200,   7,       1,      1,     0x32,   0x28000},
	{19800,   8,       1,      1,     0x30,   0xA2E8C},
	{20000,   9,       1,      1,     0x30,   0x26666},
	{24000,   10,      1,      1,     0x28,   0x20000},
	{25000,   11,      1,      1,     0x26,   0x851EC}, /* array index 10 */
	{26000,   12,      1,      1,     0x25,   0x09D8A},
	{30000,   13,      1,      1,     0x20,   0x1999A},
	{33600,   14,      1,      1,     0x1C,   0xA9249},
	{37400,   15,      1,      1,     0x19,   0xBFA86},
	{38400,   16,      1,      1,     0x19,   0x14000},
	{40000,   17,      1,      1,     0x18,   0x13333},
	{48000,   18,      1,      1,     0x14,   0x10000},
	{52000,   19,      1,      1,     0x12,   0x84EC5}, /* array index 18 */
	{0,	      0,       0,      0,     0,      0	     }
};

/* Indices into array pmu1_xtaltab0_960[]. Keep array and these defines synchronized. */
#define PMU1_XTALTAB0_960_12000K	0
#define PMU1_XTALTAB0_960_13000K	1
#define PMU1_XTALTAB0_960_14400K	2
#define PMU1_XTALTAB0_960_15360K	3
#define PMU1_XTALTAB0_960_16200K	4
#define PMU1_XTALTAB0_960_16800K	5
#define PMU1_XTALTAB0_960_19200K	6
#define PMU1_XTALTAB0_960_19800K	7
#define PMU1_XTALTAB0_960_20000K	8
#define PMU1_XTALTAB0_960_24000K	9
#define PMU1_XTALTAB0_960_25000K	10
#define PMU1_XTALTAB0_960_26000K	11
#define PMU1_XTALTAB0_960_30000K	12
#define PMU1_XTALTAB0_960_33600K	13
#define PMU1_XTALTAB0_960_37400K	14
#define PMU1_XTALTAB0_960_38400K	15
#define PMU1_XTALTAB0_960_40000K	16
#define PMU1_XTALTAB0_960_48000K	17
#define PMU1_XTALTAB0_960_52000K	18
#define PMU1_XTALTAB0_960_59970K	19

#define PMU15_XTALTAB0_12000K	0
#define PMU15_XTALTAB0_20000K	1
#define PMU15_XTALTAB0_26000K	2
#define PMU15_XTALTAB0_37400K	3
#define PMU15_XTALTAB0_52000K	4
#define PMU15_XTALTAB0_END	5

/* For having the pllcontrol data (info)
 * The table with the values of the registers will have one - one mapping.
 */
typedef struct {
	uint16	clock;	/**< x-tal frequency in [KHz] */
	uint8	mode;	/**< spur mode */
	uint8	xf;	/**< corresponds with xf bitfield in PMU control register */
} pllctrl_data_t;

/*  *****************************  tables for 43012a0 *********************** */

/**
 * PLL control register table giving info about the xtal supported for 43012
 * There should be a one to one mapping between pmu1_pllctrl_tab_43012_960mhz[] and this table.
 */
static const pllctrl_data_t(pmu1_xtaltab0_43012)[] = {
/*       clock  mode xf */
	{37400, 0,   XTALTAB0_960_37400K},
	{37400,	100, XTALTAB0_960_37400K},
	{26000, 0,   XTALTAB0_960_26000K},
	{24000, 0,   XTALTAB0_960_24000K}
};

/*
There should be a one to one mapping between pmu1_pllctrl_tab_43012_640mhz[]
* and this table. PLL control5 register is related to HSIC which is not supported in 43012
* Use a safe DCO code=56 by default, Across PVT openloop VCO Max=320MHz, Min=100
* Mhz
*/
#ifdef BCMQT
static const uint32 (pmu1_pllctrl_tab_43012_1600mhz)[] = {
/* Fvco is taken as 160.1 */
/*	 PLL 0	     PLL 1	 PLL 2	     PLL 3	 PLL 4	     PLL 5		  */
	0x072fe811, 0x00800000, 0x00000000, 0x038051e8, 0x00000000, 0x00000000,
	0x0e5fd422, 0x00800000,	0x00000000, 0x000011e8,	0x00000000, 0x00000000
};
#else
static const uint32 (pmu1_pllctrl_tab_43012_1600mhz)[] = {
/* Fvco is taken as 160.1 */
/*	 PLL 0	     PLL 1	 PLL 2	     PLL 3	 PLL 4  */
	0x07df2411, 0x00800000, 0x00000000, 0x038051e8, 0x00000000,
	0x0e5fd422, 0x00800000, 0x00000000, 0x000011e8, 0x00000000,
	0x1d89dc12, 0x00800000, 0x00000000, 0x06d04de8, 0x00000000,
	0x072fe828, 0x00800000, 0x00000000, 0x06d04de8, 0x00000000
};
#endif /* BCMQT */
/*  ************************  tables for 43012a0 END *********************** */

/*  *****************************  tables for 4369a0 *********************** */
/* should get real value from hardware guys */
/**
 * PLL control register table giving info about the xtal supported for 4369
 * There should be a one to one mapping between pmu1_pllctrl_tab_4369_960mhz[] and this table.
 * Even though macro suggests XTALTAB0_960_37400K --> BBPLL VCO is set to 963MHz
 */
static const pllctrl_data_t BCMATTACHDATA(pmu1_xtaltab0_4369)[] = {
/*       clock  mode xf */
	{37400, 0,   XTALTAB0_960_37400K}
};

/**
 * PLL control register table giving info about the xtal supported for 4369.
 * There should be a one to one mapping between pmu1_pllctrl_tab_4369_963mhz[] and this table.
 */

/* For 4369, 960.1MHz BBPLL freq is chosen to avoid the spurs
* freq table : pll1 : fvco 960.1M, pll2 for arm : 400 MHz
*/
#define PMU_PLL3_4369B0_DEFAULT	0x006ABF86
static const uint32	BCMATTACHDATA(pmu1_pllctrl_tab_4369_960p1mhz)[] = {
/* Default values for unused registers 4-7 as sw loop execution will go for 8 times */
/* Fvco is taken as 963M */
/*	PLL 0  PLL 1   PLL 2   PLL 3   PLL 4   PLL 5  PLL 6  PLL 7   PLL 8   PLL 9   PLL 10 */
	0x15000000, 0x06050603, 0x01910806, PMU_PLL3_4369B0_DEFAULT,
	0x00000000, 0x32800000, 0xC7AE00A9, 0x40800000,
	0x00000000, 0x00000000, 0x00000000
};

/*  ************************  tables for 4369a0 END *********************** */

/*  *****************************  tables for 4362a0 *********************** */
/* should get real value from hardware guys */
/**
 * PLL control register table giving info about the xtal supported for 4362
 * There should be a one to one mapping between pmu1_pllctrl_tab_4362_960mhz[] and this table.
 * Even though macro suggests XTALTAB0_960_37400K --> BBPLL VCO is set to 963MHz
 */
static const pllctrl_data_t BCMATTACHDATA(pmu1_xtaltab0_4362)[] = {
/*       clock  mode xf */
	{37400, 0,   XTALTAB0_960_37400K}
};

/* For 4362, 960.1MHz BBPLL freq is chosen to avoid the spurs
* freq table : pll1 : fvco 960.1M, pll2 for arm : 400 MHz
*/
/* This freq actually around 960.123 */
#define PMU_PLL3_4362A0_DEFAULT	0x006ABF86

static const uint32	BCMATTACHDATA(pmu1_pllctrl_tab_4362_960p1mhz)[] = {
/* Default values for unused registers 4-7 as sw loop execution will go for 8 times */
/* Fvco is taken as 963M */
/*	PLL 0  PLL 1   PLL 2   PLL 3   PLL 4   PLL 5  PLL 6  PLL 7   PLL 8   PLL 9   PLL 10 */
	0x15000000, 0x06050603, 0x01910806, PMU_PLL3_4362A0_DEFAULT,
	0x00000000, 0x32800000, 0xC7AE00A9, 0x40800000,
	0x00000000, 0x00000000, 0x00000000
};

/*  ************************  tables for 4362a0 END *********************** */

/*  *****************************  tables for 4389 *********************** */
static const pllctrl_data_t BCMATTACHDATA(pmu1_xtaltab0_4389)[] = {
/*       clock               mode xf */
	{XTAL_FREQ_59970MHZ, 0,   XTALTAB0_960_59970K}
};

static const uint32 BCMATTACHDATA(pmu1_pllctrl_tab_4389_963mhz)[] = {
/* Default values for all registers */
/* Fvco (BBPLL) is taken as 963M */
/*	PLL 0  PLL 1   PLL 2   PLL 3   PLL 4   PLL 5  PLL 6  PLL 7   PLL 8   PLL 9   PLL 10 */
	0x29d00000, 0x30100c03, 0x00240c06, 0x597ff060,
	0x00000000, 0x00000800, 0x00321d3a, 0x000551ff,
	0x00000000, 0x10000000, 0x00000000
};

/*  ************************  tables for 4389 END *********************** */

/** returns a table that instructs how to program the BBPLL for a particular xtal frequency */
static const pmu1_xtaltab0_t *
BCMPOSTTRAPFN(si_pmu1_xtaltab0)(si_t *sih)
{
#ifdef BCMDBG_PMU
	char chn[8];
#endif
	switch (CHIPID(sih->chip)) {
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
	case BCM4352_CHIP_ID:
	case BCM43526_CHIP_ID:
	CASE_BCM43602_CHIP:
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		return pmu1_xtaltab0_960;
	case BCM4369_CHIP_GRPID:
		return pmu1_xtaltab0_4369_963;
	case BCM4362_CHIP_GRPID:
		return pmu1_xtaltab0_4362_963;
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		return pmu1_xtaltab0_960;
	default:
		PMU_MSG(("si_pmu1_xtaltab0: Unknown chipid %s\n", bcm_chipname(sih->chip, chn, 8)));
		break;
	}
	ASSERT(0);
	return NULL;
} /* si_pmu1_xtaltab0 */

/** returns chip specific PLL settings for default xtal frequency and VCO output frequency */
static const pmu1_xtaltab0_t *
BCMPOSTTRAPFN(si_pmu1_xtaldef0)(si_t *sih)
{
#ifdef BCMDBG_PMU
	char chn[8];
#endif

	switch (CHIPID(sih->chip)) {
	case BCM4360_CHIP_ID:
	case BCM4352_CHIP_ID:
	case BCM43460_CHIP_ID:
	case BCM43526_CHIP_ID:
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
		/* Default to 37400Khz */
		return &pmu1_xtaltab0_960[PMU1_XTALTAB0_960_37400K];
	case BCM43014_CHIP_ID:
		/* Default to 24000Khz */
		return &pmu1_xtaltab0_960[PMU1_XTALTAB0_960_24000K];
	CASE_BCM43602_CHIP:
		return &pmu1_xtaltab0_960[PMU1_XTALTAB0_960_40000K];

	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		return &pmu1_xtaltab0_960[PMU1_XTALTAB0_960_37400K];
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		return &pmu1_xtaltab0_960[PMU1_XTALTAB0_960_59970K];
	case BCM4369_CHIP_GRPID:
		return &pmu1_xtaltab0_4369_963[PMU1_XTALTAB0_960_37400K];
	case BCM4362_CHIP_GRPID:
		return &pmu1_xtaltab0_4362_963[PMU1_XTALTAB0_960_37400K];
	default:
		PMU_MSG(("si_pmu1_xtaldef0: Unknown chipid %s\n", bcm_chipname(sih->chip, chn, 8)));
		break;
	}
	ASSERT(0);
	return NULL;
} /* si_pmu1_xtaldef0 */

static uint32 fvco_4360 = 0;

/**
 * store the val on init, then if func is called during normal operation
 * don't touch core regs anymore
 */
static uint32
BCMPOSTTRAPFN(si_pmu_pll1_fvco_4360)(si_t *sih, osl_t *osh)
{
	uint32 xf, ndiv_int, ndiv_frac, fvco, pll_reg, p1_div_scale;
	uint32 r_high, r_low, int_part, frac_part, rounding_const;
	uint8 p1_div;
	uint origidx = 0;
	bcm_int_bitmask_t intr_val;

	if (fvco_4360) {
		printf("si_pmu_pll1_fvco_4360:attempt to query fvco during normal operation\n");
		/* this will insure that the func is called only once upon init */
		return fvco_4360;
	}

	/* Remember original core before switch to chipc */
	si_switch_core(sih, CC_CORE_ID, &origidx, &intr_val);

	xf = si_pmu_alp_clock(sih, osh)/1000;

	/* pll reg 10 , p1div, ndif_mode, ndiv_int */
	pll_reg = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG10, 0, 0);
	p1_div = pll_reg & 0xf;
	ndiv_int = (pll_reg >> 7)  & 0x1f;

	/* pllctrl11 */
	pll_reg = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG11, 0, 0);
	ndiv_frac = pll_reg & 0xfffff;

	int_part = xf * ndiv_int;

	rounding_const = 1 << (BBPLL_NDIV_FRAC_BITS - 1);
	math_uint64_multiple_add(&r_high, &r_low, ndiv_frac, xf, rounding_const);
	math_uint64_right_shift(&frac_part, r_high, r_low, BBPLL_NDIV_FRAC_BITS);

	if (!p1_div) {
		PMU_ERROR(("p1_div calc returned 0! [%d]\n", __LINE__));
		ROMMABLE_ASSERT(0);
	}

	if (p1_div == 0) {
		ASSERT(p1_div != 0);
		p1_div_scale = 0;
	} else

	p1_div_scale = (1 << P1_DIV_SCALE_BITS) / p1_div;
	rounding_const = 1 << (P1_DIV_SCALE_BITS - 1);

	math_uint64_multiple_add(&r_high, &r_low, (int_part + frac_part),
		p1_div_scale, rounding_const);
	math_uint64_right_shift(&fvco, r_high, r_low, P1_DIV_SCALE_BITS);

	/* Return to original core */
	si_restore_core(sih, origidx, &intr_val);

	fvco_4360 = fvco;
	return fvco;
} /* si_pmu_pll1_fvco_4360 */

/**
 * Specific to 43012 and calculate the FVCO frequency from XTAL freq
 *  Returns the FCVO frequency in [khz] units
 */
static uint32
BCMPOSTTRAPFN(si_pmu_pll1_fvco_43012)(si_t *sih, osl_t *osh)
{
	uint32 xf, ndiv_int, ndiv_frac, fvco, pll_reg, p1_div_scale;
	uint32 r_high, r_low, int_part, frac_part, rounding_const;
	uint8 p_div;
	chipcregs_t *cc;
	uint origidx = 0;
	bcm_int_bitmask_t intr_val;

	/* Remember original core before switch to chipc */
	cc = (chipcregs_t *)si_switch_core(sih, CC_CORE_ID, &origidx, &intr_val);
	ASSERT(cc != NULL);
	BCM_REFERENCE(cc);

	xf = si_pmu_alp_clock(sih, osh)/1000;

	pll_reg = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG0, 0, 0);

	ndiv_int = (pll_reg & PMU43012_PLL0_PC0_NDIV_INT_MASK) >>
			PMU43012_PLL0_PC0_NDIV_INT_SHIFT;

	ndiv_frac = (pll_reg & PMU43012_PLL0_PC0_NDIV_FRAC_MASK) >>
			PMU43012_PLL0_PC0_NDIV_FRAC_SHIFT;

	pll_reg = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG3, 0, 0);

	p_div = (pll_reg & PMU43012_PLL0_PC3_PDIV_MASK) >>
			PMU43012_PLL0_PC3_PDIV_SHIFT;

	/* If the p_div value read from PLL control register is zero,
	* then return default FVCO value instead of computing the FVCO frequency
	* using XTAL frequency
	*/
	if (!p_div) {
		PMU_ERROR(("pll control register read failed [%d]\n", __LINE__));
		ROMMABLE_ASSERT(0);
		fvco = 0;
		goto done;
	}
	/* Actual expression is as below */
	/* fvco1 = ((xf * (1/p1_div)) * (ndiv_int + (ndiv_frac /(1 << 20)))); */

	int_part = xf * ndiv_int;
	rounding_const = 1 << (PMU43012_PLL_NDIV_FRAC_BITS - 1);
	math_uint64_multiple_add(&r_high, &r_low, ndiv_frac, xf, rounding_const);
	math_uint64_right_shift(&frac_part, r_high, r_low, PMU43012_PLL_NDIV_FRAC_BITS);

	p1_div_scale = (1 << PMU43012_PLL_P_DIV_SCALE_BITS) / p_div;
	rounding_const = 1 << (PMU43012_PLL_P_DIV_SCALE_BITS - 1);

	math_uint64_multiple_add(&r_high, &r_low, (int_part + frac_part),
		p1_div_scale, rounding_const);
	math_uint64_right_shift(&fvco, r_high, r_low, PMU43012_PLL_P_DIV_SCALE_BITS);

done:
	/* Return to original core */
	si_restore_core(sih, origidx, &intr_val);
	return fvco;
} /* si_pmu_pll1_fvco_43012 */

/** returns chip specific default BaseBand pll fvco frequency in [khz] units */
static uint32
BCMPOSTTRAPFN(si_pmu1_pllfvco0)(si_t *sih)
{
#ifdef BCMDBG_PMU
	char chn[8];
#endif

	switch (CHIPID(sih->chip)) {
	case BCM4352_CHIP_ID:
	case BCM43526_CHIP_ID:
		return FVCO_960;

	CASE_BCM43602_CHIP:
		return FVCO_960;
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
	{
		osl_t *osh;
		osh = si_osh(sih);
		return si_pmu_pll1_fvco_4360(sih, osh);
	}
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
	{
		osl_t *osh;
		osh = si_osh(sih);
		return si_pmu_pll1_fvco_43012(sih, osh);
	}
	case BCM4369_CHIP_GRPID:
		return FVCO_960p1;
	case BCM4362_CHIP_GRPID:
		return FVCO_960p1;
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		return FVCO_960p1;
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		return FVCO_963p01;
	default:
		PMU_MSG(("si_pmu1_pllfvco0: Unknown chipid %s\n", bcm_chipname(sih->chip, chn, 8)));
		break;
	}
	ASSERT(0);
	return 0;
} /* si_pmu1_pllfvco0 */

/**
 * returns chip specific default pll fvco frequency in [khz] units
 */
static uint32
BCMPOSTTRAPFN(si_pmu1_pllfvco0_pll2)(si_t *sih)
{
#ifdef BCMDBG_PMU
	char chn[8];
#endif

	switch (CHIPID(sih->chip)) {
	case BCM4369_CHIP_GRPID:
	case BCM4362_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		return si_get_armpllclkfreq(sih) * 1000;
	case BCM4389_CHIP_GRPID:
		return SI_INFO(sih)->armpllclkfreq ? si_get_armpllclkfreq(sih) * 1000 : FVCO_1002p8;
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
		return FVCO_400;
	default:
		PMU_MSG(("si_pmu1_pllfvco0_pll2 : Unknown chipid %s\n",
				bcm_chipname(sih->chip, chn, 8)));
		ASSERT(0);
		break;
	}
	return 0;
} /* si_pmu1_pllfvco0_pll2 */

/** query alp/xtal clock frequency */
static uint32
BCMPOSTTRAPFN(si_pmu1_alpclk0)(si_t *sih, osl_t *osh, pmuregs_t *pmu)
{
	const pmu1_xtaltab0_t *xt;
	uint32 xf;
	uint8 xtdiv = 1;

	BCM_REFERENCE(sih);

	/* Find the frequency in the table */
	xf = (R_REG(osh, &pmu->pmucontrol) & PCTL_XTALFREQ_MASK) >>
	        PCTL_XTALFREQ_SHIFT;
	for (xt = si_pmu1_xtaltab0(sih); xt != NULL && xt->fref != 0; xt ++)
		if (xt->xf == xf)
			break;
	/* Could not find it so assign a default value */
	if (xt == NULL || xt->fref == 0)
		xt = si_pmu1_xtaldef0(sih);
	ASSERT(xt != NULL && xt->fref != 0);

	switch (CHIPID(sih->chip))
	{
		case BCM4385_CHIP_GRPID:
		case BCM4387_CHIP_GRPID:
		case BCM4388_CHIP_GRPID:
		case BCM4389_CHIP_GRPID:
		case BCM4397_CHIP_GRPID:
			/* xtalfreq for 4378B0 is 59.97 MHz and
			 * but ALP clk is xtal / 2 (29.985 MHz) by default.
			 */
			xtdiv = 2;
			break;
		default:
			break;
	}

	return (xt->fref * 1000) / xtdiv;
}

/**
 * Before the PLL is switched off, the HT clocks need to be deactivated, and reactivated
 * when the PLL is switched on again.
 * This function returns the chip specific HT clock resources (HT and MACPHY clocks).
 */
static uint32
si_pmu_htclk_mask(si_t *sih)
{
	/* chip specific bit position of various resources */
	rsc_per_chip_t *rsc = si_pmu_get_rsc_positions(sih);

	uint32 ht_req = (PMURES_BIT(rsc->ht_avail) | PMURES_BIT(rsc->macphy_clkavail));

	switch (CHIPID(sih->chip))
	{
		CASE_BCM43602_CHIP:
		case BCM43012_CHIP_ID:
		case BCM43013_CHIP_ID:
		case BCM43014_CHIP_ID:
		case BCM4369_CHIP_GRPID:
		case BCM4362_CHIP_GRPID:
		case BCM4376_CHIP_GRPID:
		case BCM4378_CHIP_GRPID:
		case BCM4385_CHIP_GRPID:
		case BCM4387_CHIP_GRPID:
		case BCM4388_CHIP_GRPID:
		case BCM4389_CHIP_GRPID:
		case BCM4397_CHIP_GRPID:
			ht_req |= PMURES_BIT(rsc->ht_start);
			break;
		default:
			ASSERT(0);
			break;
	}

	return ht_req;
} /* si_pmu_htclk_mask */

/** returns ALP frequency in [Hz] */
static uint32
BCMATTACHFN(si_pmu_def_alp_clock)(si_t *sih, osl_t *osh)
{
	uint32 clock = ALP_CLOCK;

	BCM_REFERENCE(osh);

	switch (CHIPID(sih->chip)) {
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
	case BCM4369_CHIP_GRPID:
	case BCM4362_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:

#ifdef UNRELEASEDCHIP
#endif

		clock = 37400*1000;
		break;
	CASE_BCM43602_CHIP:
		clock = 40000 * 1000;
		break;
	}

	return clock;
}

/**
 * The BBPLL register set needs to be reprogrammed because the x-tal frequency is not known at
 * compile time, or a different spur mode is selected. This function writes appropriate values into
 * the BBPLL registers. It returns the 'xf', corresponding to the 'xf' bitfield in the PMU control
 * register.
 *     'xtal'             : xtal frequency in [KHz]
 *     'pllctrlreg_update': contains info on what entries to use in 'pllctrlreg_val' for the given
 *                          x-tal frequency and spur mode
 *     'pllctrlreg_val'   : contains a superset of the BBPLL values to write
 *
 * Note: if pmu is NULL, this function returns xf, without programming PLL registers.
 * This function is only called for pmu1_ type chips, perhaps we should rename it.
 */
static uint8
si_pmu_pllctrlreg_update(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 xtal,
            uint8 spur_mode, const pllctrl_data_t *pllctrlreg_update, uint32 array_size,
            const uint32 *pllctrlreg_val)
{
	uint8 indx, reg_offset, xf = 0;
	uint8 pll_ctrlcnt = 0;

	ASSERT(pllctrlreg_update);

	if (PMUREV(sih->pmurev) >= 5) {
		pll_ctrlcnt = (sih->pmucaps & PCAP5_PC_MASK) >> PCAP5_PC_SHIFT;
	} else {
		pll_ctrlcnt = (sih->pmucaps & PCAP_PC_MASK) >> PCAP_PC_SHIFT;
	}

	/* Program the PLL control register if the xtal value matches with the table entry value */
	for (indx = 0; indx < array_size; indx++) {
		/* If the entry does not match the xtal and spur_mode just continue the loop */
		if (!((pllctrlreg_update[indx].clock == (uint16)xtal) &&
			(pllctrlreg_update[indx].mode == spur_mode)))
			continue;
		/*
		 * Don't program the PLL registers if register base is NULL.
		 * If NULL just return the xref.
		 */
		if (pmu) {
			for (reg_offset = 0; reg_offset < pll_ctrlcnt; reg_offset++) {
				si_pmu_pllcontrol(sih, reg_offset, ~0,
					pllctrlreg_val[indx*pll_ctrlcnt + reg_offset]);
			}

			/* for 4369, arm clk cycle can be set from nvram - default is 400 MHz */
			if ((BCM4369_CHIP(sih->chip) || BCM4362_CHIP(sih->chip)) &&
				(pll_ctrlcnt > PMU1_PLL0_PLLCTL6)) {
				si_pmu_pll6val_armclk_calc(osh, pmu,
					si_get_armpllclkfreq(sih), xtal, TRUE);
			}
		}
		xf = pllctrlreg_update[indx].xf;
		break;
	}
	return xf;
} /* si_pmu_pllctrlreg_update */

/*
 * Calculate p1div, ndiv_int, ndiv_frac for clock ratio.
 * Input: fvco, xtal
 * Output: ndiv_int, ndiv_frac
 * Returns: p1div
 *
 */
uint8
si_pmu_pll28nm_calc_ndiv(uint32 fvco, uint32 xtal, uint32 *ndiv_int, uint32 *ndiv_frac)
{
	uint8 p1div;
	uint32 temp_high, temp_low;
	ASSERT(xtal <= 0xFFFFFFFF / 1000);
	p1div = 1 + (uint8) ((xtal * 1000) / 54000000UL);
	*ndiv_int = (fvco * p1div) / xtal;
	/* nfrac = 20 */
	/* ndiv_frac = (uint32) (((uint64) (fvco * p1div - xtal * ndiv_int) * (1 << 20)) / xtal) */
	math_uint64_multiple_add(&temp_high, &temp_low, fvco * p1div - xtal * (*ndiv_int), 1 << 20,
		0);
	math_uint64_divide(ndiv_frac, temp_high, temp_low, xtal);
	return p1div;
}

void
si_pmu_armpll_freq_upd(si_t *sih, uint8 p1div, uint32 ndiv_int, uint32 ndiv_frac)
{
	switch (CHIPID(sih->chip)) {
	case BCM4388_CHIP_GRPID:
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6, PMU4388_ARMPLL_I_NDIV_INT_MASK,
			ndiv_int << PMU4388_ARMPLL_I_NDIV_INT_SHIFT);
		si_pmu_pllupd(sih);
		break;
	case BCM4389_CHIP_GRPID:
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6, PMU4389_ARMPLL_I_NDIV_INT_MASK,
			ndiv_int << PMU4389_ARMPLL_I_NDIV_INT_SHIFT);
		si_pmu_pllupd(sih);
		break;
	case BCM4369_CHIP_GRPID:
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG5, PMU4369_PLL1_PC5_P1DIV_MASK,
			((p1div >> PMU4369_P1DIV_LO_SHIFT) << PMU4369_PLL1_PC5_P1DIV_SHIFT));
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6, PMU4369_PLL1_PC6_P1DIV_MASK,
			(p1div >> PMU4369_P1DIV_HI_SHIFT));
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6, PMU4369_PLL1_PC6_NDIV_INT_MASK,
			ndiv_int << PMU4369_PLL1_PC6_NDIV_INT_SHIFT);
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6, PMU4369_PLL1_PC6_NDIV_FRAC_MASK,
			ndiv_frac << PMU4369_PLL1_PC6_NDIV_FRAC_SHIFT);
		si_pmu_pllupd(sih);
		break;
	case BCM4362_CHIP_GRPID:
		/* 4362/69 PLL definitions are same. so reusing definitions */
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG5, PMU4369_PLL1_PC5_P1DIV_MASK,
			((p1div >> PMU4369_P1DIV_LO_SHIFT) << PMU4369_PLL1_PC5_P1DIV_SHIFT));
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6, PMU4369_PLL1_PC6_P1DIV_MASK,
			(p1div >> PMU4369_P1DIV_HI_SHIFT));
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6, PMU4369_PLL1_PC6_NDIV_INT_MASK,
			ndiv_int << PMU4369_PLL1_PC6_NDIV_INT_SHIFT);
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6, PMU4369_PLL1_PC6_NDIV_FRAC_MASK,
			ndiv_frac << PMU4369_PLL1_PC6_NDIV_FRAC_SHIFT);
		si_pmu_pllupd(sih);
		break;
	default:
		ASSERT(0);
		break;
	}
}

void
si_pmu_bbpll_freq_upd(si_t *sih, uint8 p1div, uint32 ndiv_int, uint32 ndiv_frac)
{
	switch (CHIPID(sih->chip)) {
	case BCM4369_CHIP_GRPID:
	case BCM4362_CHIP_GRPID:
		/* PLL Control 2 Register are the same for 4368, 4369 */
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG2, PMU4369_PLL0_PC2_PDIV_MASK, p1div);
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG2, PMU4369_PLL0_PC2_NDIV_INT_MASK,
			ndiv_int << PMU4369_PLL0_PC2_NDIV_INT_SHIFT);
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG2, PMU4369_PLL0_PC3_NDIV_FRAC_MASK,
			ndiv_frac << PMU4369_PLL0_PC3_NDIV_FRAC_SHIFT);
		si_pmu_pllupd(sih);
		break;
	default:
		ASSERT(0);
		break;
	}
}

void
si_pmu_armpll_chmdiv_upd(si_t *sih, uint32 ch0_mdiv, uint32 ch1_mdiv)
{
	switch (CHIPID(sih->chip)) {
	default:
		ASSERT(0);
		break;
	}
}

static bool
si_pmu_armpll_write_required(si_t *sih, uint32 xtal)
{
	uint32 def_xtal = 0;
	uint32 def_armclk_mhz = 0;
	uint32 armclk_mhz = si_get_armpllclkfreq(sih);

	switch (CHIPID(sih->chip)) {
	case BCM4369_CHIP_GRPID:
		def_xtal = XTAL_FREQ_37400MHZ;
		def_armclk_mhz = ARMPLL_FREQ_400MHZ;
		break;
	case BCM4388_CHIP_GRPID:
		def_xtal = XTAL_FREQ_59970MHZ;
		def_armclk_mhz = ARMPLL_FREQ_1000MHZ;
		break;
	case BCM4389_CHIP_GRPID:
		def_xtal = XTAL_FREQ_59970MHZ;
		def_armclk_mhz = ARMPLL_FREQ_1000MHZ;
		break;
	default:
		break;
	}

	/*
	 * If programmed xtalfreq is same as xtal, no need to enable pll write. Check for
	 * armclk and xtalfreq instead of comparing calculated value and pll register value.
	 */
	return (((armclk_mhz == def_armclk_mhz) && (xtal == def_xtal)) ? FALSE : TRUE);
}

/**
 * Chip-specific overrides to PLLCONTROL registers during init. If certain conditions (dependent on
 * x-tal frequency and current ALP frequency) are met, an update of the PLL is required.
 *
 * This takes less precedence over OTP PLLCONTROL overrides.
 * If update_required=FALSE, it returns TRUE if a update is about to occur.
 * No write happens.
 *
 * Return value: TRUE if the BBPLL registers 'update' field should be written by the caller.
 *
 * This function is only called for pmu1_ type chips, perhaps we should rename it.
 */
static bool
BCMATTACHFN(si_pmu_update_pllcontrol)(si_t *sih, osl_t *osh, uint32 xtal, bool update_required)
{
	pmuregs_t *pmu;
	uint origidx;
	bool write_en = FALSE;
	uint8 xf = 0;
	const pmu1_xtaltab0_t *xt;
	uint32 tmp;
	const pllctrl_data_t *pllctrlreg_update = NULL;
	uint32 array_size = 0;
	/* points at a set of PLL register values to write for a given x-tal frequency: */
	const uint32 *pllctrlreg_val = NULL;
	uint8 ndiv_mode = PMU1_PLL0_PC2_NDIV_MODE_MASH;
	uint32 xtalfreq = 0;
	uint32 ndiv_int;
	uint32 ndiv_frac;
	uint8 pdiv;

	BCM_REFERENCE(ndiv_int);
	BCM_REFERENCE(ndiv_frac);
	BCM_REFERENCE(pdiv);
	/* If there is OTP or NVRAM entry for xtalfreq, program the
	 * PLL control register even if it is default xtal.
	 */
	xtalfreq = getintvar(NULL, rstr_xtalfreq);
	/* CASE1 */
	if (xtalfreq) {
		write_en = TRUE;
		xtal = xtalfreq;
	} else {
		/* There is NO OTP value */
		if (xtal) {
			/* CASE2: If the xtal value was calculated, program the PLL control
			 * registers only if it is not default xtal value.
			 */
			if (xtal != (si_pmu_def_alp_clock(sih, osh)/1000))
				write_en = TRUE;
		} else {
			/* CASE3: If the xtal obtained is "0", ie., clock is not measured, then
			 * leave the PLL control register as it is but program the xf in
			 * pmucontrol register with the default xtal value.
			 */
			xtal = si_pmu_def_alp_clock(sih, osh)/1000;
		}
	}

	switch (CHIPID(sih->chip)) {
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		pllctrlreg_update = pmu1_xtaltab0_43012;
		array_size = ARRAYSIZE(pmu1_xtaltab0_43012);
		pllctrlreg_val = pmu1_pllctrl_tab_43012_1600mhz;
		break;
	case BCM4369_CHIP_GRPID:
		pllctrlreg_update = pmu1_xtaltab0_4369;
		array_size = ARRAYSIZE(pmu1_xtaltab0_4369);
		pllctrlreg_val = pmu1_pllctrl_tab_4369_960p1mhz;
		/* default pll programming be true, later based on need disable it */
		write_en = TRUE;
		break;
	case BCM4362_CHIP_GRPID:
		pllctrlreg_update = pmu1_xtaltab0_4362;
		array_size = ARRAYSIZE(pmu1_xtaltab0_4362);
		pllctrlreg_val = pmu1_pllctrl_tab_4362_960p1mhz;
		/* default pll programming be true, later based on need disable it */
		write_en = TRUE;
		break;
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
		/* TBD : bypass PLL programming, So use chip default values */
		pllctrlreg_update = NULL;
		array_size = 0;
		pllctrlreg_val = NULL;
		write_en = FALSE;
		break;
	case BCM4388_CHIP_GRPID:
		/* TBD : bypass PLL programming, So use chip default values */
		pllctrlreg_update = NULL;
		array_size = 0;
		pllctrlreg_val = NULL;
		write_en = FALSE;
		break;
	case BCM4389_CHIP_GRPID:
		pllctrlreg_update = pmu1_xtaltab0_4389;
		array_size = ARRAYSIZE(pmu1_xtaltab0_4389);
		pllctrlreg_val = pmu1_pllctrl_tab_4389_963mhz;
		break;
	CASE_BCM43602_CHIP:
		/*
		 * 43602 has only 1 x-tal value, possibly insert case when an other BBPLL
		 * frequency than 960Mhz is required (e.g., for spur avoidance)
		 */
		 /* fall through */
	default:
		/* write_en is FALSE in this case. So returns from the function */
		write_en = FALSE;
		break;
	}

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	/* Check if the table has PLL control register values for the requested xtal */
	if (!update_required && pllctrlreg_update) {
		/* Here the chipcommon register base is passed as NULL, so that we just get
		 * the xf for the xtal being programmed but don't program the registers now
		 * as the PLL is not yet turned OFF.
		 */
		xf = si_pmu_pllctrlreg_update(sih, osh, NULL, xtal, 0, pllctrlreg_update,
			array_size, pllctrlreg_val);

		/* Program the PLL based on the xtal value. */
		if (xf != 0) {
			/* Write XtalFreq. Set the divisor also. */
			tmp = R_REG(osh, &pmu->pmucontrol) &
				~(PCTL_ILP_DIV_MASK | PCTL_XTALFREQ_MASK);
			tmp |= (((((xtal + 127) / 128) - 1) << PCTL_ILP_DIV_SHIFT) &
				PCTL_ILP_DIV_MASK) |
				((xf << PCTL_XTALFREQ_SHIFT) & PCTL_XTALFREQ_MASK);
			W_REG(osh, &pmu->pmucontrol, tmp);
		} else {
			write_en = FALSE;
			if (!FWSIGN_ENAB()) {
				printf(rstr_Invalid_Unsupported_xtal_value_D, xtal);
			}
		}

		write_en = si_pmu_armpll_write_required(sih, xtal);
	}

	/* If its a check sequence or if there is nothing to write, return here */
	if ((update_required == FALSE) || (write_en == FALSE)) {
		goto exit;
	}

	/* Update the PLL control register based on the xtal used. */
	if (pllctrlreg_val) {
		si_pmu_pllctrlreg_update(sih, osh, pmu, xtal, 0, pllctrlreg_update, array_size,
			pllctrlreg_val);
	}

	/* Chip specific changes to PLL Control registers is done here. */
	switch (CHIPID(sih->chip)) {
	case BCM4388_CHIP_ID: {
		uint32 armclk_mhz = si_get_armpllclkfreq(sih);
		uint32 vco_freq = (armclk_mhz * PMU4388_APLL_PDIV * 1000);

		ASSERT(vco_freq <= FVCO_3200);

		/*
		 * ndiv_init = Fvco / Frefeff
		 * Frefeff = Fref / pdiv
		 * Fref = xtal / 2
		 * pdiv = 3
		 *
		 * ndiv_init = ((Fvco * pdiv * 1000000) / ((xtal * 1000) / 2)
		 */
		ndiv_int = (vco_freq / (xtal / 2));
		si_pmu_armpll_freq_upd(sih, 0, ndiv_int, 0);
		break;
	}

	case BCM4389_CHIP_ID: {
		uint32 armclk_mhz = si_get_armpllclkfreq(sih);
		uint32 vco_freq = (armclk_mhz * PMU4389_APLL_PDIV * 1000);

		ASSERT(vco_freq <= FVCO_3200);

		/*
		 * ndiv_init = Fvco / Frefeff
		 * Frefeff = Fref / pdiv
		 * Fref = xtal / 2
		 * pdiv = 3
		 *
		 * ndiv_init = ((Fvco * pdiv * 1000000) / ((xtal * 1000) / 2)
		 */
		ndiv_int = (vco_freq / (xtal / 2));
		si_pmu_armpll_freq_upd(sih, 0, ndiv_int, 0);
		break;
	}

	default:
		break;
	}

	/* Program the PLL based on the xtal value. */
	if (xtal != 0) {
		/* Find the frequency in the table */
		for (xt = si_pmu1_xtaltab0(sih); xt != NULL && xt->fref != 0; xt ++)
			if (xt->fref == xtal) {
				break;
			}

		/* Check current PLL state, bail out if it has been programmed or
		 * we don't know how to program it. But we might still have some programming
		 * like changing the ARM clock, etc. So cannot return from here.
		 */
		if (xt == NULL || xt->fref == 0) {
			goto exit;
		}

		/* If the PLL is already programmed exit from here. */
		if (((R_REG(osh, &pmu->pmucontrol) &
			PCTL_XTALFREQ_MASK) >> PCTL_XTALFREQ_SHIFT) == xt->xf) {
			goto exit;
		}

		PMU_MSG(("XTAL %d.%d MHz (%d)\n", xtal / 1000, xtal % 1000, xt->xf));
		PMU_MSG(("Programming PLL for %d.%d MHz\n", xt->fref / 1000, xt->fref % 1000));

		if (BCM4389_CHIP(sih->chip)) {
			/* Write ndiv_int to pllcontrol[6] */
			tmp = ((xt->ndiv_int << PMU4389_ARMPLL_I_NDIV_INT_SHIFT)
				& PMU4389_ARMPLL_I_NDIV_INT_MASK);
			si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6,
				(PMU4389_ARMPLL_I_NDIV_INT_MASK), tmp);
		} else if (BCM4388_CHIP(sih->chip)) {
			/* Write ndiv_int to pllcontrol[6] */
			tmp = ((xt->ndiv_int << PMU4388_ARMPLL_I_NDIV_INT_SHIFT)
				& PMU4388_ARMPLL_I_NDIV_INT_MASK);
			si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG6,
				(PMU4388_ARMPLL_I_NDIV_INT_MASK), tmp);
		} else if (BCM4369_CHIP(sih->chip) ||
				BCM4362_CHIP(sih->chip) ||
				FALSE) {
			/* Write pdiv (Actually it is mapped to p1div in the struct)
			 to pllcontrol[2]
			 */
			tmp = ((xt->p1div << PMU4369_PLL0_PC2_PDIV_SHIFT) &
				PMU4369_PLL0_PC2_PDIV_MASK);
			si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG2,
				(PMU4369_PLL0_PC2_PDIV_MASK), tmp);

			/* Write ndiv_int to pllcontrol[2] */
			tmp = ((xt->ndiv_int << PMU4369_PLL0_PC2_NDIV_INT_SHIFT)
					& PMU4369_PLL0_PC2_NDIV_INT_MASK);
			si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG2,
				(PMU4369_PLL0_PC2_NDIV_INT_MASK), tmp);

			/* Write ndiv_frac to pllcontrol[3] */
			tmp = ((xt->ndiv_frac << PMU4369_PLL0_PC3_NDIV_FRAC_SHIFT) &
				PMU4369_PLL0_PC3_NDIV_FRAC_MASK);
			si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG3,
				PMU4369_PLL0_PC3_NDIV_FRAC_MASK, tmp);
		} else {
			/* Write p1div and p2div to pllcontrol[0] */
			tmp = ((xt->p1div << PMU1_PLL0_PC0_P1DIV_SHIFT) &
				PMU1_PLL0_PC0_P1DIV_MASK) |
				((xt->p2div << PMU1_PLL0_PC0_P2DIV_SHIFT) &
				PMU1_PLL0_PC0_P2DIV_MASK);
			si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG0,
				(PMU1_PLL0_PC0_P1DIV_MASK | PMU1_PLL0_PC0_P2DIV_MASK), tmp);

			/* Write ndiv_int and ndiv_mode to pllcontrol[2] */
			tmp = ((xt->ndiv_int << PMU1_PLL0_PC2_NDIV_INT_SHIFT)
					& PMU1_PLL0_PC2_NDIV_INT_MASK) |
					((ndiv_mode << PMU1_PLL0_PC2_NDIV_MODE_SHIFT)
					& PMU1_PLL0_PC2_NDIV_MODE_MASK);
			si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG2,
				(PMU1_PLL0_PC2_NDIV_INT_MASK | PMU1_PLL0_PC2_NDIV_MODE_MASK), tmp);
			/* Write ndiv_frac to pllcontrol[3] */
			tmp = ((xt->ndiv_frac << PMU1_PLL0_PC3_NDIV_FRAC_SHIFT) &
				PMU1_PLL0_PC3_NDIV_FRAC_MASK);
			si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG3,
				PMU1_PLL0_PC3_NDIV_FRAC_MASK, tmp);
		}

		/* Write XtalFreq. Set the divisor also. */
		tmp = R_REG(osh, &pmu->pmucontrol) &
			~(PCTL_ILP_DIV_MASK | PCTL_XTALFREQ_MASK);
		tmp |= (((((xt->fref + 127) / 128) - 1) << PCTL_ILP_DIV_SHIFT) &
			PCTL_ILP_DIV_MASK) |
			((xt->xf << PCTL_XTALFREQ_SHIFT) & PCTL_XTALFREQ_MASK);
		W_REG(osh, &pmu->pmucontrol, tmp);
	}

exit:
	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return write_en;
} /* si_pmu_update_pllcontrol */

/* returns current value of PMUTimer.
	also taking care of PR88659 by multiple reads.
*/
uint32
BCMPOSTTRAPFN(si_pmu_get_pmutimer)(si_t *sih)
{
	osl_t *osh = si_osh(sih);
	pmuregs_t *pmu;
	uint origidx;
	uint32 start;
	BCM_REFERENCE(sih);

	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	start = R_REG(osh, &pmu->pmutimer);
	if (start != R_REG(osh, &pmu->pmutimer))
		start = R_REG(osh, &pmu->pmutimer);

	si_setcoreidx(sih, origidx);

	return (start);
}

/* Get current pmu time API */
uint32
si_cur_pmu_time(si_t *sih)
{
	uint origidx;
	uint32 pmu_time;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);

	pmu_time = si_pmu_get_pmutimer(sih);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
	return (pmu_time);
}

/**
 * returns
 * a) diff between a 'prev' value of pmu timer and current value
 * b) the current pmutime value in 'prev'
 *  So, 'prev' is an IO parameter.
 */
uint32
BCMPOSTTRAPFN(si_pmu_get_pmutime_diff)(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 *prev)
{
	uint32 pmutime_diff = 0, pmutime_val = 0;
	uint32 prev_val = *prev;
	BCM_REFERENCE(osh);
	BCM_REFERENCE(pmu);
	/* read current value */
	pmutime_val = si_pmu_get_pmutimer(sih);
	/* diff btween prev and current value, take on wraparound case as well. */
	pmutime_diff = (pmutime_val >= prev_val) ?
		(pmutime_val - prev_val) :
		(~prev_val + pmutime_val + 1);
	*prev = pmutime_val;
	return pmutime_diff;
}

/**
 * wait for usec for the res_pending register to change.
 * NOTE: usec SHOULD be > 32uS
 * if cond = TRUE, res_pending will be read until it becomes == 0;
 * If cond = FALSE, res_pending will be read until it becomes != 0;
 * returns TRUE if timedout.
 * returns elapsed time in this loop in elapsed_time
 */
bool
BCMPOSTTRAPFN(si_pmu_wait_for_res_pending)(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint usec,
	bool cond, uint32 *elapsed_time)
{
	/* add 32uSec more */
	uint countdown = usec;
	uint32 pmutime_prev = 0, pmutime_elapsed = 0, res_pend;
	bool pending = FALSE;

	/* store current time */
	pmutime_prev = si_pmu_get_pmutimer(sih);
	while (1) {
		res_pend = R_REG(osh, &pmu->res_pending);

		/* based on the condition, check */
		if (cond == TRUE) {
			if (res_pend == 0) break;
		} else {
			if (res_pend != 0) break;
		}

		/* if required time over */
		if ((pmutime_elapsed * PMU_US_STEPS) >= countdown) {
			/* timeout. so return as still pending */
			pending = TRUE;
			break;
		}

		/* get elapsed time after adding diff between prev and current
		* pmutimer value
		*/
		pmutime_elapsed += si_pmu_get_pmutime_diff(sih, osh, pmu, &pmutime_prev);
	}

	*elapsed_time = pmutime_elapsed * PMU_US_STEPS;
	return pending;
} /* si_pmu_wait_for_res_pending */

/**
 *	The algorithm for pending check is that,
 *	step1: 	wait till (res_pending !=0) OR pmu_max_trans_timeout.
 *			if max_trans_timeout, flag error and exit.
 *			wait for 1 ILP clk [64uS] based on pmu timer,
 *			polling to see if res_pending again goes high.
 *			if res_pending again goes high, go back to step1.
 *	Note: res_pending is checked repeatedly because, in between switching
 *	of dependent
 *	resources, res_pending resets to 0 for a short duration of time before
 *	it becomes 1 again.
 *	Note: return 0 is GOOD, 1 is BAD [mainly timeout].
 */
int
BCMPOSTTRAPFN(si_pmu_wait_for_steady_state)(si_t *sih, osl_t *osh, pmuregs_t *pmu)
{
	si_info_t *sii = SI_INFO(sih);
	int stat = 0;
	bool timedout = FALSE;
	uint32 elapsed = 0, pmutime_total_elapsed = 0;
	uint32 pmutime_prev;

	sii->res_pend_count = 0;

	pmutime_prev = si_pmu_get_pmutimer(sih);

	while (1) {
		/* wait until all resources are settled down [till res_pending becomes 0] */
		timedout = si_pmu_wait_for_res_pending(sih, osh, pmu,
			PMU_MAX_TRANSITION_DLY, TRUE, &elapsed);

		sii->res_state[sii->res_pend_count].low_time =
			si_pmu_get_pmutime_diff(sih, osh, pmu, &pmutime_prev);
		sii->res_state[sii->res_pend_count].low = R_REG(osh, &pmu->res_pending);

		if (timedout) {
			stat = 1;
			break;
		}

		pmutime_total_elapsed += elapsed;
		/* wait to check if any resource comes back to non-zero indicating
		* that it pends again. The res_pending goes 0 for 1 ILP clock before
		* getting set for next resource in the sequence , so if res_pending
		* is 0 for more than 1 ILP clk it means nothing is pending
		* to indicate some pending dependency.
		*/
		pmutime_prev = R_REG(osh, &pmu->pmutimer);
		timedout = si_pmu_wait_for_res_pending(sih, osh, pmu,
			64, FALSE, &elapsed);

		pmutime_total_elapsed += elapsed;

		sii->res_state[sii->res_pend_count].high_time =
			si_pmu_get_pmutime_diff(sih, osh, pmu, &pmutime_prev);
		sii->res_state[sii->res_pend_count].high = R_REG(osh, &pmu->res_pending);

		/* Here, we can also check timedout, but we make sure that,
		* we read the res_pending again.
		*/

		if (timedout) {
			stat = 0;
			break;
		}

		/* Total wait time for all the waits above added should be
		* less than  PMU_MAX_TRANSITION_DLY
		*/
		if (pmutime_total_elapsed >= PMU_MAX_TRANSITION_DLY) {
			/* timeout. so return as still pending */
			stat = 1;
			break;
		}

		sii->res_pend_count++;
		sii->res_pend_count %= RES_PEND_STATS_COUNT;
		pmutime_prev = R_REG(osh, &pmu->pmutimer);
	}
	return stat;
} /* si_pmu_wait_for_steady_state */

static uint32
si_pmu_pll_delay_43012(si_t *sih, uint32 delay_us, uint32 poll)
{
	uint32 delay = 0;

	/* In case of NIC builds, we can use OSL_DELAY() for 1 us delay. But in case of DONGLE
	 * builds, we can't rely on the OSL_DELAY() as it is internally relying on HT clock and
	 * we are calling this function when ALP clock is present.
	 */
#if defined(DONGLEBUILD)
	uint32 initial, current;

	initial = get_arm_cyclecount();
	while (delay < delay_us) {
		if (poll == 1) {
			if (si_gci_chipstatus(sih, GCI_CHIPSTATUS_07) &
					GCI43012_CHIPSTATUS_07_BBPLL_LOCK_MASK) {
				goto exit;
			}
		}
		current = get_arm_cyclecount();
		delay = ((current - initial) * 1000) / si_xtalfreq(sih);
	}
#else
	for (delay = 0; delay < delay_us; delay++) {
		if (poll == 1) {
			if (si_gci_chipstatus(sih, GCI_CHIPSTATUS_07) &
					GCI43012_CHIPSTATUS_07_BBPLL_LOCK_MASK) {
				goto exit;
			}
		}
		OSL_DELAY(1);
	}
#endif /* BCMDONGLEHOST */

	if (poll == 1) {
		PMU_ERROR(("si_pmu_pll_delay_43012: PLL not locked!"));
		ASSERT(0);
	}
exit:
	return delay;
}

static void
si_pmu_pll_on_43012(si_t *sih, osl_t *osh, pmuregs_t *pmu, bool openloop_cal)
{
	uint32 rsrc_ht, total_time = 0;

	si_pmu_chipcontrol(sih, PMU_CHIPCTL4, PMUCCTL04_43012_FORCE_BBPLL_PWROFF, 0);
	total_time += si_pmu_pll_delay_43012(sih, 2, 0);
	si_pmu_chipcontrol(sih, PMU_CHIPCTL4, PMUCCTL04_43012_FORCE_BBPLL_ISOONHIGH |
			PMUCCTL04_43012_FORCE_BBPLL_PWRDN, 0);
	total_time += si_pmu_pll_delay_43012(sih, 2, 0);
	si_pmu_chipcontrol(sih, PMU_CHIPCTL4, PMUCCTL04_43012_FORCE_BBPLL_ARESET, 0);

	rsrc_ht = R_REG(osh, &pmu->res_state) &
			((1 << RES43012_HT_AVAIL) | (1 << RES43012_HT_START));

	if (rsrc_ht)
	{
		/* Wait for PLL to lock in close-loop */
		total_time += si_pmu_pll_delay_43012(sih, 200, 1);
	}
	else {
		/* Wait for 1 us for the open-loop clock to start */
		total_time += si_pmu_pll_delay_43012(sih, 1, 0);
	}

	if (!openloop_cal) {
		/* Allow clk to be used if its not calibration */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL4, PMUCCTL04_43012_FORCE_BBPLL_DRESET, 0);
		total_time += si_pmu_pll_delay_43012(sih, 1, 0);
		si_pmu_chipcontrol(sih, PMU_CHIPCTL4, PMUCCTL04_43012_DISABLE_LQ_AVAIL, 0);
		si_pmu_chipcontrol(sih, PMU_CHIPCTL4, PMUCCTL04_43012_DISABLE_HT_AVAIL, 0);
	}

	PMU_MSG(("si_pmu_pll_on_43012: time taken: %d us\n", total_time));
}

static void
si_pmu_pll_off_43012(si_t *sih, osl_t *osh, pmuregs_t *pmu)
{
	uint32 total_time = 0;
	BCM_REFERENCE(osh);
	BCM_REFERENCE(pmu);
	si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
			PMUCCTL04_43012_DISABLE_LQ_AVAIL | PMUCCTL04_43012_DISABLE_HT_AVAIL,
			PMUCCTL04_43012_DISABLE_LQ_AVAIL | PMUCCTL04_43012_DISABLE_HT_AVAIL);
	total_time += si_pmu_pll_delay_43012(sih, 1, 0);

	si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
			(PMUCCTL04_43012_FORCE_BBPLL_ARESET | PMUCCTL04_43012_FORCE_BBPLL_DRESET |
			PMUCCTL04_43012_FORCE_BBPLL_PWRDN |PMUCCTL04_43012_FORCE_BBPLL_ISOONHIGH),
			(PMUCCTL04_43012_FORCE_BBPLL_ARESET | PMUCCTL04_43012_FORCE_BBPLL_DRESET |
			PMUCCTL04_43012_FORCE_BBPLL_PWRDN |PMUCCTL04_43012_FORCE_BBPLL_ISOONHIGH));
	total_time += si_pmu_pll_delay_43012(sih, 1, 0);

	si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
			PMUCCTL04_43012_FORCE_BBPLL_PWROFF,
			PMUCCTL04_43012_FORCE_BBPLL_PWROFF);

	PMU_MSG(("si_pmu_pll_off_43012: time taken: %d us\n", total_time));
}

/** Turn Off the PLL - Required before setting the PLL registers */
static void
si_pmu_pll_off(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 *min_mask,
	uint32 *max_mask, uint32 *clk_ctl_st)
{
	uint32 ht_req;

	/* Save the original register values */
	*min_mask = R_REG(osh, &pmu->min_res_mask);
	*max_mask = R_REG(osh, &pmu->max_res_mask);
	*clk_ctl_st = si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0);

	ht_req = si_pmu_htclk_mask(sih);
	if (ht_req == 0)
		return;

	if ((CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43014_CHIP_ID) ||
		(BCM4369_CHIP(sih->chip)) ||
		(BCM4362_CHIP(sih->chip)) ||
		(BCM4376_CHIP(sih->chip)) ||
		(BCM4378_CHIP(sih->chip)) ||
		(BCM4385_CHIP(sih->chip)) ||
		(BCM4387_CHIP(sih->chip)) ||
		(BCM4388_CHIP(sih->chip)) ||
		(BCM4389_CHIP(sih->chip)) ||
		BCM43602_CHIP(sih->chip) ||
		0) {
		/*
		* If HT_AVAIL is not set, wait to see if any resources are availing HT.
		*/
		if (((si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL) != CCS_HTAVAIL))
			si_pmu_wait_for_steady_state(sih, osh, pmu);
	} else {
		OR_REG(osh,  &pmu->max_res_mask, ht_req);
		/* wait for HT to be ready before taking the HT away...HT could be coming up... */
		SPINWAIT(((si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL) != CCS_HTAVAIL), PMU_MAX_TRANSITION_DLY);
		ASSERT((si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL));
	}

	if ((CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43014_CHIP_ID)) {
		si_pmu_pll_off_43012(sih, osh, pmu);
	} else {
		AND_REG(osh, &pmu->min_res_mask, ~ht_req);
		AND_REG(osh, &pmu->max_res_mask, ~ht_req);

		SPINWAIT(((si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL) == CCS_HTAVAIL), PMU_MAX_TRANSITION_DLY);
		ASSERT(!(si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL));
		OSL_DELAY(100);
	}
} /* si_pmu_pll_off */

/* below function are for BBPLL parallel purpose */
/** Turn Off the PLL - Required before setting the PLL registers */
void
si_pmu_pll_off_PARR(si_t *sih, osl_t *osh, uint32 *min_mask,
uint32 *max_mask, uint32 *clk_ctl_st)
{
	pmuregs_t *pmu;
	uint origidx;
	bcm_int_bitmask_t intr_val;
	uint32 ht_req;

	/* Block ints and save current core */
	si_introff(sih, &intr_val);
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	/* Save the original register values */
	*min_mask = R_REG(osh, &pmu->min_res_mask);
	*max_mask = R_REG(osh, &pmu->max_res_mask);
	*clk_ctl_st = si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0);
	ht_req = si_pmu_htclk_mask(sih);
	if (ht_req == 0) {
		/* Return to original core */
		si_setcoreidx(sih, origidx);
		si_intrrestore(sih, &intr_val);
		return;
	}

	if ((CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43014_CHIP_ID) ||
		(BCM4369_CHIP(sih->chip)) ||
		(BCM4362_CHIP(sih->chip)) ||
		(BCM4376_CHIP(sih->chip)) ||
		(BCM4378_CHIP(sih->chip)) ||
		(BCM4385_CHIP(sih->chip)) ||
		(BCM4387_CHIP(sih->chip)) ||
		(BCM4388_CHIP(sih->chip)) ||
		(BCM4389_CHIP(sih->chip)) ||
		(BCM4397_CHIP(sih->chip)) ||
		(BCM43602_CHIP(sih->chip)) ||
		0) {
		/*
		* If HT_AVAIL is not set, wait to see if any resources are availing HT.
		*/
		if (((si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL)
			!= CCS_HTAVAIL))
			si_pmu_wait_for_steady_state(sih, osh, pmu);
	} else {
		OR_REG(osh, &pmu->max_res_mask, ht_req);
		/* wait for HT to be ready before taking the HT away...HT could be coming up... */
		SPINWAIT(((si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL) != CCS_HTAVAIL), PMU_MAX_TRANSITION_DLY);
		ASSERT((si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL));
	}

	AND_REG(osh, &pmu->min_res_mask, ~ht_req);
	AND_REG(osh, &pmu->max_res_mask, ~ht_req);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
	si_intrrestore(sih, &intr_val);
} /* si_pmu_pll_off_PARR */

/** Turn ON/restore the PLL based on the mask received */
static void
si_pmu_pll_on(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 min_mask_mask,
	uint32 max_mask_mask, uint32 clk_ctl_st_mask)
{
	uint32 ht_req;

	ht_req = si_pmu_htclk_mask(sih);
	if (ht_req == 0)
		return;

	max_mask_mask &= ht_req;
	min_mask_mask &= ht_req;

	if (max_mask_mask != 0)
		OR_REG(osh, &pmu->max_res_mask, max_mask_mask);

	if (min_mask_mask != 0)
		OR_REG(osh, &pmu->min_res_mask, min_mask_mask);

	if (clk_ctl_st_mask & CCS_HTAVAIL) {
		/* Wait for HT_AVAIL to come back */
		SPINWAIT(((si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL) != CCS_HTAVAIL), PMU_MAX_TRANSITION_DLY);
		ASSERT((si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
		& CCS_HTAVAIL));
	}

	if ((CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43014_CHIP_ID)) {
		si_pmu_pll_on_43012(sih, osh, pmu, 0);
	}
}

/**
 * Set up PLL registers in the PMU as per the (optional) OTP values, or, if no OTP values are
 * present, optionally update with POR override values contained in firmware. Enables the BBPLL
 * when done.
 */
static void
BCMATTACHFN(si_pmu1_pllinit1)(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 xtal)
{
	char name[16];
	const char *otp_val;
	uint8 i, otp_entry_found = FALSE;
	uint32 pll_ctrlcnt;
	uint32 min_mask = 0, max_mask = 0, clk_ctl_st = 0;
#if defined(BTOVERPCIE) && !defined(BTOVERPCIE_DISABLED)
	uint32 otpval = 0, regval = 0;
#endif /* defined(BTOVERPCIE) && !defined(BTOVERPCIE_DISABLED) */

	if (!FWSIGN_ENAB()) {
		if (PMUREV(sih->pmurev) >= 5) {
			pll_ctrlcnt = (sih->pmucaps & PCAP5_PC_MASK) >> PCAP5_PC_SHIFT;
		} else {
			pll_ctrlcnt = (sih->pmucaps & PCAP_PC_MASK) >> PCAP_PC_SHIFT;
		}

		/* Check if there is any otp enter for PLLcontrol registers */
		for (i = 0; i < pll_ctrlcnt; i++) {
			snprintf(name, sizeof(name), rstr_pllD, i);
			if ((otp_val = getvar(NULL, name)) == NULL)
				continue;

			/* If OTP entry is found for PLL register, then turn off the PLL
			 * and set the status of the OTP entry accordingly.
			 */
			otp_entry_found = TRUE;
			break;
		}
	}

	/* If no OTP parameter is found and no chip-specific updates are needed, return. */
	if ((otp_entry_found == FALSE) &&
		(si_pmu_update_pllcontrol(sih, osh, xtal, FALSE) == FALSE)) {
#if defined(BTOVERPCIE) && !defined(BTOVERPCIE_DISABLED)
		/*
		 * For 4369/4362 PLL3 could be prorammed by BT, check the value is default and not
		 * overrided by BT
		 */
		if ((BCM4369_CHIP(sih->chip) || BCM4362_CHIP(sih->chip)) &&
			(regval = si_pmu_pllcontrol(sih, 3, 0, 0)) != PMU_PLL3_4369B0_DEFAULT) {
			PMU_ERROR(("Default PLL3 value 0x%x is not same as programmed"
						"value 0x%x\n", PMU_PLL3_4369B0_DEFAULT, regval));
			hnd_gcisem_set_err(GCI_PLL_LOCK_SEM);
			return;
		}

		/* Update SW_READY bit indicating WLAN is ready and verified PLL3 */
		si_gci_output(sih, GCI_ECI_SW1(GCI_WLAN_IP_ID), GCI_SWREADY, GCI_SWREADY);
#endif /* defined(BTOVERPCIE) && !defined(BTOVERPCIE_DISABLED) */
		return;
	}

#if defined(BTOVERPCIE) && !defined(BTOVERPCIE_DISABLED)
	if ((hnd_gcisem_acquire(GCI_PLL_LOCK_SEM, TRUE, GCI_PLL_LOCK_SEM_TIMEOUT) != BCME_OK)) {
		PMU_ERROR(("Failed to get GCI PLL Lock semaphore...\n"));
		hnd_gcisem_set_err(GCI_PLL_LOCK_SEM);
		return;
	}

	/* Skip BB PLL programming if BT has already done it, which is indicated by SW_READY bit */
	if (si_gci_input(sih, GCI_ECI_SW1(GCI_BT_IP_ID)) & GCI_SWREADY) {
		PMU_MSG(("PLL is already programmed\n"));

		/* Program ARM PLL only if xtalfreq(pllctrl6) programmed is different from xtal */
		if (si_pmu_update_pllcontrol(sih, osh, xtal, FALSE)) {
			/* Make sure PLL is off */
			si_pmu_pll_off(sih, osh, pmu, &min_mask, &max_mask, &clk_ctl_st);

			/* for 4369, arm clk cycle can be set from nvram - default is 400 MHz */
			if ((BCM4369_CHIP(sih->chip) || BCM4362_CHIP(sih->chip)) &&
				(pll_ctrlcnt > PMU1_PLL0_PLLCTL6)) {
				PMU_MSG(("Programming ARM CLK\n"));
				si_pmu_pll6val_armclk_calc(osh, pmu,
					si_get_armpllclkfreq(sih), xtal, TRUE);
			}

			/* Flush ('update') the deferred pll control registers writes */
			if (PMUREV(sih->pmurev) >= 2)
				OR_REG(osh, &pmu->pmucontrol, PCTL_PLL_PLLCTL_UPD);

			/* Restore back the register values. This ensures PLL remains on if it
			 * was originally on and remains off if it was originally off.
			 */
			si_pmu_pll_on(sih, osh, pmu, min_mask, max_mask, clk_ctl_st);
		}

		snprintf(name, sizeof(name), rstr_pllD, 3);
		if ((otp_val = getvar(NULL, name)) != NULL) {
			otpval = (uint32)bcm_strtoul(otp_val, NULL, 0);
			if ((regval = si_pmu_pllcontrol(sih, 3, 0, 0)) != otpval) {
				PMU_ERROR(("PLL3 programming value 0x%x is not same as programmed"
					"value 0x%x\n", otpval, regval));
				hnd_gcisem_set_err(GCI_PLL_LOCK_SEM);
			}
		}
		goto done;
	}
#endif /* defined(BTOVERPCIE) && !defined(BTOVERPCIE_DISABLED) */

	/* Make sure PLL is off */
	si_pmu_pll_off(sih, osh, pmu, &min_mask, &max_mask, &clk_ctl_st);

	/* Update any chip-specific PLL registers. Does not write PLL 'update' bit yet. */
	si_pmu_update_pllcontrol(sih, osh, xtal, TRUE);

	/* Update the PLL register if there is a OTP entry for PLL registers */
	si_pmu_otp_pllcontrol(sih, osh);

	/* Flush ('update') the deferred pll control registers writes */
	if (PMUREV(sih->pmurev) >= 2)
		OR_REG(osh, &pmu->pmucontrol, PCTL_PLL_PLLCTL_UPD);

	/* Restore back the register values. This ensures PLL remains on if it
	 * was originally on and remains off if it was originally off.
	 */
	si_pmu_pll_on(sih, osh, pmu, min_mask, max_mask, clk_ctl_st);

	if ((CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43014_CHIP_ID)) {
		uint32 origidx;
		/* PMU clock stretch to be decreased to 8 for HT and ALP
		* to reduce DS0 current during high traffic
		*/
		W_REG(osh, &pmu->clkstretch, CSTRETCH_REDUCE_8);

		/* SDIOD to request for ALP
		* to reduce DS0 current during high traffic
		*/
		origidx = si_coreidx(sih);
		si_setcore(sih, SDIOD_CORE_ID, 0);
		/* Clear the Bit 8 for ALP REQUEST change */
		si_wrapperreg(sih, AI_OOBSELOUTB30, (AI_OOBSEL_MASK << AI_OOBSEL_1_SHIFT),
			OOB_B_ALP_REQUEST << AI_OOBSEL_1_SHIFT);
		si_setcoreidx(sih, origidx);
	}

#if defined(BTOVERPCIE) && !defined(BTOVERPCIE_DISABLED)
done:
	/* Update SW_READY bit indicating WLAN is done programming PLL registers */
	si_gci_output(sih, GCI_ECI_SW1(GCI_WLAN_IP_ID), GCI_SWREADY, GCI_SWREADY);
	if ((hnd_gcisem_release(GCI_PLL_LOCK_SEM) != BCME_OK)) {
		PMU_ERROR(("Failed to release GCI PLL Lock semaphore...\n"));
		hnd_gcisem_set_err(GCI_PLL_LOCK_SEM);
	}
#endif /* defined(BTOVERPCIE) && !defined(BTOVERPCIE_DISABLED) */
} /* si_pmu1_pllinit1 */

#if defined(EDV)
/* returns backplane clk programmed in pll cntl 1 */
/* WHY NOT JUST CALL si_pmu_si_clock()? */
uint32 si_pmu_get_backplaneclkspeed(si_t *sih)
{
	uint32 FVCO;
	uint32 tmp, mdiv = 1;

	switch (CHIPID(sih->chip)) {
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		return si_pmu_bpclk_4387(sih);
	default:
		break;
	}

	FVCO =  si_pmu1_pllfvco0(sih);

	switch (CHIPID(sih->chip)) {
	case BCM4369_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		tmp = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG1, 0, 0);
		mdiv = (tmp & PMU1_PLL0_PC1_M4DIV_MASK) >> PMU1_PLL0_PC1_M4DIV_SHIFT;
		break;
	default:
		ASSERT(FALSE);
		break;
	}
	return FVCO / mdiv * 1000u;
}

/* Update backplane clock speed */
void
si_pmu_update_backplane_clock(si_t *sih, osl_t *osh, uint reg, uint32 mask, uint32 val)
{

	pmuregs_t *pmu;
	uint origidx;
	uint32 max_mask = 0, min_mask = 0, clk_ctl_st = 0;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);

	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}

	ASSERT(pmu != NULL);

	/* Make sure PLL is off */
	si_pmu_pll_off(sih, osh, pmu, &min_mask, &max_mask, &clk_ctl_st);

	si_pmu_pllcontrol(sih, reg, mask, val);

	/* Flush ('update') the deferred pll control registers writes */
	if (PMUREV(sih->pmurev) >= 2)
		OR_REG(osh, &pmu->pmucontrol, PCTL_PLL_PLLCTL_UPD);

	/* Restore back the register values. This ensures PLL remains on if it
	 * was originally on and remains off if it was originally off.
	 */
	si_pmu_pll_on(sih, osh, pmu, min_mask, max_mask, clk_ctl_st);
	si_setcoreidx(sih, origidx);
}
#endif /* si_pmu_update_backplane_clock */

/**
 * returns the backplane clock frequency.
 * Does this by determining current Fvco and the setting of the
 * clock divider that leads up to the backplane. Returns value in [Hz] units.
 */
static uint32
BCMPOSTTRAPFN(si_pmu_bpclk_4387)(si_t *sih)
{
	uint32 tmp, mdiv;
	uint32 FVCO;	/* in [khz] units */

	FVCO = si_pmu1_pllfvco0(sih);

	tmp = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG2, 0, 0);
	mdiv = (tmp & PMU4387_PLL0_PC2_ICH3_MDIV_MASK);
	ASSERT(mdiv != 0);

	return FVCO / mdiv * 1000;
}

/**
 * returns the CPU clock frequency. Does this by determining current Fvco and the setting of the
 * clock divider that leads up to the ARM. Returns value in [Hz] units.
 */
static uint32
BCMPOSTTRAPFN(si_pmu1_cpuclk0)(si_t *sih, osl_t *osh, pmuregs_t *pmu)
{
	uint32 tmp, mdiv = 1;
#ifdef BCMDBG
	uint32 ndiv_int, ndiv_frac, p2div, p1div, fvco;
	uint32 fref;
#endif
#ifdef BCMDBG_PMU
	char chn[8];
#endif
	uint32 FVCO;	/* in [khz] units */

	FVCO = si_pmu1_pllfvco0(sih);

	if (BCM43602_CHIP(sih->chip) &&
#ifdef DONGLEBUILD
#ifdef __arm__
	    (si_arm_clockratio(sih, 0) == 1) &&
#endif
#endif /* DONGLEBUILD */
		TRUE) {
		/* CR4 running on backplane_clk */
		return si_pmu_si_clock(sih, osh);	/* in [hz] units */
	}

	switch (CHIPID(sih->chip)) {
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
	case BCM43526_CHIP_ID:
	case BCM4352_CHIP_ID:
		/* Read m6div from pllcontrol[5] */
		tmp = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG5, 0, 0);
		mdiv = (tmp & PMU1_PLL0_PC2_M6DIV_MASK) >> PMU1_PLL0_PC2_M6DIV_SHIFT;
		break;
#ifdef DONGLEBUILD
	CASE_BCM43602_CHIP:
#ifdef __arm__
		ASSERT(si_arm_clockratio(sih, 0) == 2);
#endif
		/* CR4 running on armcr4_clk (Ch5). Read 'bbpll_i_m5div' from pllctl[5] */
		tmp = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG5, 0, 0);
		mdiv = (tmp & PMU1_PLL0_PC2_M5DIV_MASK) >> PMU1_PLL0_PC2_M5DIV_SHIFT;
		break;
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		/* mdiv is not supported for 43012 and FVCO frequency should be divided by 2 */
		mdiv = 2;
		break;
#endif /* DONGLEBUILD */

	case BCM4369_CHIP_GRPID:
		tmp = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG1, 0, 0);
		mdiv = (tmp & PMU1_PLL0_PC1_M4DIV_MASK) >> PMU1_PLL0_PC1_M4DIV_SHIFT;
		break;
	case BCM4362_CHIP_GRPID:
		tmp = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG1, 0, 0);
		mdiv = (tmp & PMU1_PLL0_PC1_M4DIV_MASK) >> PMU1_PLL0_PC1_M4DIV_SHIFT;
		break;

	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		tmp = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG1, 0, 0);
		mdiv = (tmp & PMU1_PLL0_PC1_M4DIV_MASK) >> PMU1_PLL0_PC1_M4DIV_SHIFT;
		break;

	default:
		PMU_MSG(("si_pmu1_cpuclk0: Unknown chipid %s\n", bcm_chipname(sih->chip, chn, 8)));
		ASSERT(0);
		break;
	}

	ASSERT(mdiv != 0);

#ifdef BCMDBG
	/* Read p2div/p1div from pllcontrol[0] */
	tmp = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG0, 0, 0);
	p2div = (tmp & PMU1_PLL0_PC0_P2DIV_MASK) >> PMU1_PLL0_PC0_P2DIV_SHIFT;
	p1div = (tmp & PMU1_PLL0_PC0_P1DIV_MASK) >> PMU1_PLL0_PC0_P1DIV_SHIFT;

	/* Calculate fvco based on xtal freq and ndiv and pdiv */
	tmp = PMU1_PLL0_PLLCTL2;

	tmp = si_pmu_pllcontrol(sih, tmp, 0, 0);

	if (BCM4362_CHIP(sih->chip) ||
		BCM4369_CHIP(sih->chip)) {
		p2div = 1;
		p1div = (tmp & PMU4369_PLL0_PC2_PDIV_MASK) >> PMU4369_PLL0_PC2_PDIV_SHIFT;
		ndiv_int = (tmp & PMU4369_PLL0_PC2_NDIV_INT_MASK) >>
					PMU4369_PLL0_PC2_NDIV_INT_SHIFT;
	} else if (BCM4378_CHIP(sih->chip) || BCM4376_CHIP(sih->chip)) {
		p2div = 1;
		p1div = (tmp & PMU4378_PLL0_PC2_P1DIV_MASK) >> PMU4378_PLL0_PC2_P1DIV_SHIFT;
		ndiv_int = (tmp & PMU4378_PLL0_PC2_NDIV_INT_MASK) >>
					PMU4378_PLL0_PC2_NDIV_INT_SHIFT;
	} else {
		ndiv_int = (tmp & PMU1_PLL0_PC2_NDIV_INT_MASK) >> PMU1_PLL0_PC2_NDIV_INT_SHIFT;
	}

	ASSERT(p1div != 0);

	tmp = PMU1_PLL0_PLLCTL3;

	tmp = si_pmu_pllcontrol(sih, tmp, 0, 0);

	if (BCM4369_CHIP(sih->chip) || BCM4362_CHIP(sih->chip) ||
	    BCM4376_CHIP(sih->chip) ||
	    BCM4378_CHIP(sih->chip) ||
	    FALSE) {
			ndiv_frac =
				(tmp & PMU4369_PLL0_PC3_NDIV_FRAC_MASK) >>
				PMU4369_PLL0_PC3_NDIV_FRAC_SHIFT;
		fref = si_pmu1_alpclk0(sih, osh, pmu) / 1000; /* [KHz] */

		fvco = (fref * ndiv_int) << 8;
		fvco += (fref * ((ndiv_frac & 0xfffff) >> 4)) >> 8;
		fvco >>= 8;
		fvco *= p1div;
		fvco /= 1000;
		fvco *= 1000;
	} else {
		ndiv_frac =
			(tmp & PMU1_PLL0_PC3_NDIV_FRAC_MASK) >> PMU1_PLL0_PC3_NDIV_FRAC_SHIFT;

		fref = si_pmu1_alpclk0(sih, osh, pmu) / 1000;

		fvco = (fref * ndiv_int) << 8;
		fvco += (fref * (ndiv_frac >> 12)) >> 4;
		fvco += (fref * (ndiv_frac & 0xfff)) >> 12;
		fvco >>= 8;
		fvco *= p2div;
		fvco /= p1div;
		fvco /= 1000;
		fvco *= 1000;
	}

	PMU_MSG(("si_pmu1_cpuclk0: ndiv_int %u ndiv_frac %u p2div %u p1div %u fvco %u\n",
	         ndiv_int, ndiv_frac, p2div, p1div, fvco));

	FVCO = fvco;
#endif	/* BCMDBG */

	return FVCO / mdiv * 1000; /* Return CPU clock in [Hz] */
} /* si_pmu1_cpuclk0 */

/**
 * BCM4369/4378/4387 specific function returning the CPU clock frequency.
 * Does this by determining current Fvco and the setting of the clock divider that leads up to
 * the ARM.
 * Returns value in [Hz] units.
 */
static uint32
BCMPOSTTRAPFN(si_pmu1_cpuclk0_pll2)(si_t *sih)
{
	uint32 FVCO = si_pmu1_pllfvco0_pll2(sih);	/* in [khz] units */

	/* Return ARM/SB clock */
	return FVCO * 1000;
} /* si_pmu1_cpuclk0_pll2 */

/**
 * Returns the MAC clock frequency. Called when e.g. MAC clk frequency has to change because of
 * interference mitigation.
 */
uint32
si_mac_clk(si_t *sih, osl_t *osh)
{
	uint8 mdiv2 = 0;
	uint32 mac_clk = 0;
	chipcregs_t *cc;
	uint origidx;
	bcm_int_bitmask_t intr_val;
#ifdef BCMDBG_PMU
	char chn[8];
#endif

	uint32 FVCO = si_pmu1_pllfvco0(sih);	/* in [khz] units */

	BCM_REFERENCE(osh);

	/* Remember original core before switch to chipc */
	cc = (chipcregs_t *)si_switch_core(sih, CC_CORE_ID, &origidx, &intr_val);
	ASSERT(cc != NULL);
	BCM_REFERENCE(cc);

	switch (CHIPID(sih->chip)) {
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		mdiv2 = 2;
		mac_clk = FVCO / mdiv2;
		break;
	default:
		PMU_MSG(("si_mac_clk: Unknown chipid %s\n",
			bcm_chipname(CHIPID(sih->chip), chn, 8)));
		ASSERT(0);
		break;
	}

	/* Return to original core */
	si_restore_core(sih, origidx, &intr_val);

	return mac_clk;
} /* si_mac_clk */

/* 4387 pll MAC channel divisor - for ftm */
static uint32
si_pmu_macdiv_4387(si_t *sih)
{
	uint32 tmp, mdiv;

	/* TODO: when it's needed return different MAC clock freq.
	 * for different MAC/slice!
	 */
	tmp = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG1, 0, 0);
	mdiv = (tmp & PMU4387_PLL0_PC1_ICH2_MDIV_MASK) >> PMU4387_PLL0_PC1_ICH2_MDIV_SHIFT;
	ASSERT(mdiv != 0);

	return mdiv;
}

/** Get chip's FVCO and PLLCTRL1 register value */
int
si_pmu_fvco_macdiv(si_t *sih, uint32 *fvco, uint32 *div)
{
	chipcregs_t *cc;
	uint origidx;
	bcm_int_bitmask_t intr_val;
	int err = BCME_OK;
#ifdef BCMDBG_PMU
	char chn[8];
#endif

	if (fvco)
		*fvco = si_pmu1_pllfvco0(sih)/1000;

	/* Remember original core before switch to chipc */
	cc = (chipcregs_t *)si_switch_core(sih, CC_CORE_ID, &origidx, &intr_val);
	ASSERT(cc != NULL);
	BCM_REFERENCE(cc);

	switch (CHIPID(sih->chip)) {
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
		if (div)
			*div = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG12, 0, 0) &
				PMU1_PLL0_PC1_M1DIV_MASK;
		break;

	case BCM43602_CHIP_ID:
		if (div) {
			*div = (si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG4, 0, 0) &
				PMU1_PLL0_PC1_M3DIV_MASK) >> PMU1_PLL0_PC1_M3DIV_SHIFT;
		}
		break;
	case BCM4369_CHIP_GRPID:
	case BCM4362_CHIP_GRPID:
		if (div) {
			*div = (si_pmu_pllcontrol(sih, PMU1_PLL0_PLLCTL1, 0, 0)
				& PMU1_PLL0_PC1_M4DIV_MASK) >> PMU1_PLL0_PC1_M4DIV_SHIFT;
		}
		break;
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		 /* mDIV is not supported for 43012 & divisor value is always 2 */
		if (div)
			*div = 2;
		break;
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		if (div) {
			*div = (si_pmu_pllcontrol(sih, PMU1_PLL0_PLLCTL1, 0, 0)
				& PMU1_PLL0_PC1_M4DIV_MASK) >> PMU1_PLL0_PC1_M4DIV_SHIFT;
		}
		break;
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
		if (div) {
			*div = si_pmu_macdiv_4387(sih);
		}
		break;
	default:
		PMU_MSG(("si_mac_clk: Unknown chipid %s\n", bcm_chipname(sih->chip, chn, 8)));
		err = BCME_ERROR;
	}

	/* Return to original core */
	si_restore_core(sih, origidx, &intr_val);

	return err;
}

/** Return TRUE if scan retention memory's sleep/pm signal was asserted */
bool
BCMPOSTTRAPFN(si_pmu_reset_ret_sleep_log)(si_t *sih, osl_t *osh)
{
	pmuregs_t *pmu;
	uint origidx;
	uint32 ret_ctl;
	bool was_sleep = FALSE;

	/* Remember original core before switch to chipc */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	ret_ctl = R_REG(osh, &pmu->retention_ctl);
	if (ret_ctl & RCTL_MEM_RET_SLEEP_LOG_MASK) {
		W_REG(osh, &pmu->retention_ctl, ret_ctl);
		was_sleep = TRUE;
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return was_sleep;
}

/** Return TRUE if pmu rsrc XTAL_PU was de-asserted */
bool
BCMPOSTTRAPFN(si_pmu_reset_chip_sleep_log)(si_t *sih, osl_t *osh)
{
	pmuregs_t *pmu;
	uint origidx;
	bool was_sleep = FALSE;

	/* Remember original core before switch to chipc */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	if (PMUREV(sih->pmurev) >= 36) {
		uint32 pmu_int_sts = R_REG(osh, &pmu->pmuintstatus);
		if (pmu_int_sts & PMU_INT_STAT_RSRC_EVENT_INT0_MASK) {
			/* write 1 to clear the status */
			W_REG(osh, &pmu->pmuintstatus, PMU_INT_STAT_RSRC_EVENT_INT0_MASK);
			was_sleep = TRUE;
		}
	} else {
		was_sleep = si_pmu_reset_ret_sleep_log(sih, osh);
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return was_sleep;
}

/* For 43602a0 MCH2/MCH5 boards: power up PA Reference LDO */
void
si_pmu_switch_on_PARLDO(si_t *sih, osl_t *osh)
{
	uint32 mask;
	pmuregs_t *pmu;
	uint origidx;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	switch (CHIPID(sih->chip)) {
	CASE_BCM43602_CHIP:
		mask = R_REG(osh, &pmu->min_res_mask) | PMURES_BIT(RES43602_PARLDO_PU);
		W_REG(osh, &pmu->min_res_mask, mask);
		mask = R_REG(osh, &pmu->max_res_mask) | PMURES_BIT(RES43602_PARLDO_PU);
		W_REG(osh, &pmu->max_res_mask, mask);
		break;
	default:
		break;
	}
	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

/* For 43602a0 MCH2/MCH5 boards: power off PA Reference LDO */
void
si_pmu_switch_off_PARLDO(si_t *sih, osl_t *osh)
{
	uint32 mask;
	pmuregs_t *pmu;
	uint origidx;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM43602_CHIP_ID:
	case BCM43462_CHIP_ID:
		mask = R_REG(osh, &pmu->min_res_mask) & ~PMURES_BIT(RES43602_PARLDO_PU);
		W_REG(osh, &pmu->min_res_mask, mask);
		mask = R_REG(osh, &pmu->max_res_mask) & ~PMURES_BIT(RES43602_PARLDO_PU);
		W_REG(osh, &pmu->max_res_mask, mask);
		break;
	default:
		break;
	}
	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

/**
 * Change VCO frequency (slightly), e.g. to avoid PHY errors due to spurs.
 */
static void
BCMATTACHFN(si_set_bb_vcofreq_frac)(si_t *sih, osl_t *osh, int vcofreq, int frac, int xtalfreq)
{
	uint32 vcofreq_withfrac, p1div, ndiv_int, fraca, ndiv_mode, reg;
	/* shifts / masks for PMU PLL control register #2 : */
	uint32 ndiv_int_shift, ndiv_mode_shift, p1div_shift, pllctrl2_mask;
	/* shifts / masks for PMU PLL control register #3 : */
	uint32 pllctrl3_mask;
	BCM_REFERENCE(osh);

	if ((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
	    (CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
	    (CHIPID(sih->chip) == BCM43526_CHIP_ID) ||
	    (CHIPID(sih->chip) == BCM4352_CHIP_ID) ||
	    BCM43602_CHIP(sih->chip)) {
		if (si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL) {
			PMU_MSG(("HTAVAIL is set, so not updating BBPLL Frequency \n"));
			return;
		}

		ndiv_int_shift = 7;
		ndiv_mode_shift = 4;
		p1div_shift = 0;
		pllctrl2_mask = 0xffffffff;
		pllctrl3_mask = 0xffffffff;
	} else {
		/* put more chips here */
		PMU_ERROR(("si_set_bb_vcofreq_frac: only work on 4360, 4352\n"));
		return;
	}

	vcofreq_withfrac = vcofreq * 10000 + frac;
	p1div = 0x1;
	ndiv_int = vcofreq / xtalfreq;
	ndiv_mode = (vcofreq_withfrac % (xtalfreq * 10000)) ? 3 : 0;
	PMU_ERROR(("ChangeVCO => vco:%d, xtalF:%d, frac: %d, ndivMode: %d, ndivint: %d\n",
		vcofreq, xtalfreq, frac, ndiv_mode, ndiv_int));

	reg = (ndiv_int << ndiv_int_shift) |
	      (ndiv_mode << ndiv_mode_shift) |
	      (p1div << p1div_shift);
	PMU_ERROR(("Data written into the PLL_CNTRL_ADDR2: %08x\n", reg));
	si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG2, pllctrl2_mask, reg);

	if (ndiv_mode) {
		/* frac = (vcofreq_withfrac % (xtalfreq * 10000)) * 2^24) / (xtalfreq * 10000) */
		uint32 r1, r0;
		math_uint64_multiple_add(
			&r1, &r0, vcofreq_withfrac % (xtalfreq * 10000), 1 << 24, 0);
		math_uint64_divide(&fraca, r1, r0, xtalfreq * 10000);
		PMU_ERROR(("Data written into the PLL_CNTRL_ADDR3 (Fractional): %08x\n", fraca));
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG3, pllctrl3_mask, fraca);
	}

	si_pmu_pllupd(sih);
} /* si_set_bb_vcofreq_frac */

/**
 * given x-tal frequency, returns BaseBand vcofreq with fraction in 100Hz
 * @param   xtalfreq In [Mhz] units.
 * @return           In [100Hz] units.
 */
uint32
si_pmu_get_bb_vcofreq(si_t *sih, osl_t *osh, int xtalfreq)
{
	uint32  ndiv_int,	/* 9 bits integer divider */
		ndiv_mode,
		frac = 0,	/* 24 bits fractional divider */
		p1div;		/* predivider: divides x-tal freq */
	uint32 xtal1, vcofrac = 0, vcofreq;
	uint32 r1, r0, reg;

	BCM_REFERENCE(osh);

	if ((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
	    (CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
	    (CHIPID(sih->chip) == BCM43526_CHIP_ID) ||
	    (CHIPID(sih->chip) == BCM4352_CHIP_ID) ||
	    BCM43602_CHIP(sih->chip)) {
		reg = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG2, 0, 0);
		ndiv_int = reg >> 7;
		ndiv_mode = (reg >> 4) & 7;
		p1div = 1; /* do not divide x-tal frequency */

		if (ndiv_mode)
			frac = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG3, 0, 0);
	} else if ((BCM4369_CHIP(sih->chip) &&
			CST4369_CHIPMODE_PCIE(sih->chipst)) ||
			BCM4376_CHIP(sih->chip) ||
			BCM4378_CHIP(sih->chip) ||
			(BCM4362_CHIP(sih->chip) &&
			CST4362_CHIPMODE_PCIE(sih->chipst))) {
		reg = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG2, 0, 0);
		ndiv_int = reg >> 20;
		p1div = (reg >> 16) & 0xf;
		frac = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG3, 0, 0) & 0x00fffff;
		ndiv_mode = 1;
	} else {
		/* put more chips here */
		PMU_ERROR(("si_pmu_get_bb_vcofreq: only work on 4360, 4352, 4369, 4378\n"));
		ASSERT(FALSE);
		return 0;
	}

	xtal1 = 10000 * xtalfreq / p1div;		/* in [100Hz] units */

	if (ndiv_mode) {
		/* vcofreq fraction = (xtal1 * frac + (1 << 23)) / (1 << 24);
		 * handle overflow
		 */
		math_uint64_multiple_add(&r1, &r0, xtal1, frac, 1 << 23);
		vcofrac = (r1 << 8) | (r0 >> 24);
	}

	if (ndiv_int == 0) {
		ASSERT(0);
		return 0;
	}

	if ((int)xtal1 > (int)((0xffffffff - vcofrac) / ndiv_int)) {
		PMU_ERROR(("si_pmu_get_bb_vcofreq: xtalfreq is too big, %d\n", xtalfreq));
		return 0;
	}

	vcofreq = xtal1 * ndiv_int + vcofrac;
	return vcofreq;
} /* si_pmu_get_bb_vcofreq */

/** Enable PMU 1Mhz clock */
static void
si_pmu_enb_slow_clk(si_t *sih, osl_t *osh, uint32 xtalfreq)
{
	uint32 val;
	pmuregs_t *pmu;
	uint origidx;

	if (PMUREV(sih->pmurev) < 24) {
		PMU_ERROR(("si_pmu_enb_slow_clk: Not supported %d\n", PMUREV(sih->pmurev)));
		return;
	}

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	/* twiki PmuRev30, OneMhzToggleEn:31, AlpPeriod[23:0] */
	if (PMUREV(sih->pmurev) >= 38) {
		/* Use AlpPeriod[23:0] only chip default value for PmuRev >= 38 chips
		 *  eg. ROUND(POWER(2,26) / (55.970 / 2 MHz) for 4387/4385, etc
		 */
		val = R_REG(osh, &pmu->slowclkperiod) | PMU30_ALPCLK_ONEMHZ_ENAB;
	} else {
		if (PMUREV(sih->pmurev) >= 30) {
			/* AlpPeriod = ROUND(POWER(2,26)/ALP_CLK_FREQ_IN_MHz,0) */
			/* Calculation will be accurate for only one decimal of xtal (like 37.4),
			* and will not be accurate for more than one decimal
			* of xtal freq (like 37.43)
			* Also no rounding is done on final result
			*/
			ROMMABLE_ASSERT((xtalfreq/100)*100 == xtalfreq);
			val = (((1 << 26)*10)/(xtalfreq/100));
			/* set the 32 bit to enable OneMhzToggle
			* -usec wide toggle signal will be generated
			*/
			val |= PMU30_ALPCLK_ONEMHZ_ENAB;
		} else { /* twiki PmuRev24, OneMhzToggleEn:16, AlpPeriod[15:0] */
			if (xtalfreq == 37400) {
				val = 0x101B6;
			} else if (xtalfreq == 40000) {
				val = 0x10199;
			} else {
				PMU_ERROR(("si_pmu_enb_slow_clk: xtalfreq is not supported, %d\n",
					xtalfreq));
				/* Return to original core */
				si_setcoreidx(sih, origidx);
				return;
			}
		}
	}

	W_REG(osh, &pmu->slowclkperiod, val);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

/**
 * Initializes PLL given an x-tal frequency.
 * Calls si_pmuX_pllinitY() type of functions, where the reasoning behind 'X' and 'Y' is historical
 * rather than logical.
 *
 * xtalfreq : x-tal frequency in [KHz]
 */
void
BCMATTACHFN(si_pmu_pll_init)(si_t *sih, osl_t *osh, uint xtalfreq)
{
	pmuregs_t *pmu;
	uint origidx;
#ifdef BCMDBG_PMU
	char chn[8];
#endif
	BCM_REFERENCE(pmu1_xtaltab0_880);
	BCM_REFERENCE(pmu1_xtaltab0_1760);

	ASSERT(sih->cccaps & CC_CAP_PMU);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
	case BCM4352_CHIP_ID: {
		if (CHIPREV(sih->chiprev) > 2)
			si_set_bb_vcofreq_frac(sih, osh, 960, 98, 40);
		break;
	}
	CASE_BCM43602_CHIP:
		si_set_bb_vcofreq_frac(sih, osh, 960, 98, 40);
		break;
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
	case BCM4369_CHIP_GRPID:
	case BCM4362_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
		si_pmu1_pllinit1(sih, osh, pmu, xtalfreq); /* nvram PLL overrides + enables PLL */
		break;
	default:
		PMU_MSG(("No PLL init done for chip %s rev %d pmurev %d\n",
		         bcm_chipname(
			 CHIPID(sih->chip), chn, 8), CHIPREV(sih->chiprev), PMUREV(sih->pmurev)));
		break;
	}

#ifdef BCMDBG_FORCEHT
	si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), CCS_FORCEHT, CCS_FORCEHT)
#endif

	si_pmu_enb_slow_clk(sih, osh, xtalfreq);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
} /* si_pmu_pll_init */

/** get alp clock frequency in [Hz] units */
uint32
BCMPOSTTRAPFN(si_pmu_alp_clock)(si_t *sih, osl_t *osh)
{
	pmuregs_t *pmu;
	uint origidx;
	uint32 clock = ALP_CLOCK;
#ifdef BCMDBG_PMU
	char chn[8];
#endif

	ASSERT(sih->cccaps & CC_CAP_PMU);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
	case BCM4352_CHIP_ID:
	case BCM43526_CHIP_ID:
		if (sih->chipst & CST4360_XTAL_40MZ)
			clock = 40000 * 1000;
		else
			clock = 20000 * 1000;
		break;

	CASE_BCM43602_CHIP:
		/* always 40Mhz */
		clock = 40000 * 1000;
		break;
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
#ifndef BCMSDIOLITE
	case BCM4369_CHIP_GRPID:
#endif /* BCMSDIOLITE */
	case BCM4362_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		clock = si_pmu1_alpclk0(sih, osh, pmu);
		break;
#ifdef BCMSDIOLITE
	case BCM4369_CHIP_ID:
		/* always 25Mhz */
		clock = 25000 * 1000;
		break;
#endif /* BCMSDIOLITE */
	default:
		PMU_MSG(("No ALP clock specified "
			"for chip %s rev %d pmurev %d, using default %d Hz\n",
			bcm_chipname(
			CHIPID(sih->chip), chn, 8), CHIPREV(sih->chiprev),
			PMUREV(sih->pmurev), clock));
		break;
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return clock; /* in [Hz] units */
} /* si_pmu_alp_clock */

/**
 * Find the output of the "m" pll divider given pll controls that start with
 * pllreg "pll0" i.e. 12 for main 6 for phy, 0 for misc.
 */
static uint32
BCMPOSTTRAPFN(si_pmu5_clock)(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint pll0, uint m)
{
	uint32 tmp, div, ndiv, p1, p2, fc;

	if ((pll0 & 3) || (pll0 > PMU4716_MAINPLL_PLL0)) {
		PMU_ERROR(("si_pmu5_clock: Bad pll0: %d\n", pll0));
		return 0;
	}

	/* Strictly there is an m5 divider, but I'm not sure we use it */
	if ((m == 0) || (m > 4)) {
		PMU_ERROR(("si_pmu5_clock: Bad m divider: %d\n", m));
		return 0;
	}

	W_REG(osh, &pmu->pllcontrol_addr, pll0 + PMU5_PLL_P1P2_OFF);
	(void)R_REG(osh, &pmu->pllcontrol_addr);
	tmp = R_REG(osh, &pmu->pllcontrol_data);
	p1 = (tmp & PMU5_PLL_P1_MASK) >> PMU5_PLL_P1_SHIFT;
	p2 = (tmp & PMU5_PLL_P2_MASK) >> PMU5_PLL_P2_SHIFT;

	W_REG(osh, &pmu->pllcontrol_addr, pll0 + PMU5_PLL_M14_OFF);
	(void)R_REG(osh, &pmu->pllcontrol_addr);
	tmp = R_REG(osh, &pmu->pllcontrol_data);
	div = (tmp >> ((m - 1) * PMU5_PLL_MDIV_WIDTH)) & PMU5_PLL_MDIV_MASK;

	W_REG(osh, &pmu->pllcontrol_addr, pll0 + PMU5_PLL_NM5_OFF);
	(void)R_REG(osh, &pmu->pllcontrol_addr);
	tmp = R_REG(osh, &pmu->pllcontrol_data);
	ndiv = (tmp & PMU5_PLL_NDIV_MASK) >> PMU5_PLL_NDIV_SHIFT;

	/* Do calculation in Mhz */
	fc = si_pmu_alp_clock(sih, osh) / 1000000;
	fc = (p1 * ndiv * fc) / p2;

	PMU_NONE(("si_pmu5_clock: p1=%d, p2=%d, ndiv=%d(0x%x), m%d=%d; fc=%d, clock=%d\n",
	          p1, p2, ndiv, ndiv, m, div, fc, fc / div));

	/* Return clock in Hertz */
	return ((fc / div) * 1000000);
} /* si_pmu5_clock */

/**
 * Get backplane clock frequency, returns a value in [hz] units.
 * For designs that feed the same clock to both backplane and CPU just return the CPU clock speed.
 */
uint32
BCMPOSTTRAPFN(si_pmu_si_clock)(si_t *sih, osl_t *osh)
{
	pmuregs_t *pmu;
	uint origidx;
	uint32 clock = HT_CLOCK;	/* in [hz] units */
#ifdef BCMDBG_PMU
	char chn[8];
#endif

	ASSERT(sih->cccaps & CC_CAP_PMU);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
	case BCM43526_CHIP_ID:
	case BCM4352_CHIP_ID:
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		clock = si_pmu1_cpuclk0(sih, osh, pmu);
		break;

	CASE_BCM43602_CHIP: {
			uint32 mdiv;
			/* Ch3 is connected to backplane_clk. Read 'bbpll_i_m3div' from pllctl[4] */
			mdiv = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG4, 0, 0);
			mdiv = (mdiv & PMU1_PLL0_PC1_M3DIV_MASK) >> PMU1_PLL0_PC1_M3DIV_SHIFT;
			ASSERT(mdiv != 0);
			clock = si_pmu1_pllfvco0(sih) / mdiv * 1000;
			break;
		}

	case BCM4369_CHIP_GRPID:
	case BCM4362_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		clock = si_pmu1_cpuclk0(sih, osh, pmu);
		break;

	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		clock = si_pmu_bpclk_4387(sih);
		break;

	default:
		PMU_MSG(("No backplane clock specified "
			"for chip %s rev %d pmurev %d, using default %d Hz\n",
			bcm_chipname(
			CHIPID(sih->chip), chn, 8), CHIPREV(sih->chiprev),
			PMUREV(sih->pmurev), clock));
		break;
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return clock;
} /* si_pmu_si_clock */

/** returns CPU clock frequency in [hz] units */
uint32
BCMPOSTTRAPFN(si_pmu_cpu_clock)(si_t *sih, osl_t *osh)
{
	pmuregs_t *pmu;
	uint origidx;
	uint32 clock;	/* in [hz] units */

	uint32 tmp;
	uint32 armclk_offcnt, armclk_oncnt;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	if (BCM4369_CHIP(sih->chip) ||
		BCM4376_CHIP(sih->chip) ||
		BCM4378_CHIP(sih->chip) ||
		BCM4385_CHIP(sih->chip) ||
		BCM4387_CHIP(sih->chip) ||
		BCM4388_CHIP(sih->chip) ||
		BCM4389_CHIP(sih->chip) ||
		BCM4397_CHIP(sih->chip) ||
		BCM4362_CHIP(sih->chip)) {
		clock = si_pmu1_cpuclk0_pll2(sih); /* for chips with separate CPU PLL */
	} else if ((PMUREV(sih->pmurev) >= 5) &&
		!((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM4352_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43526_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43014_CHIP_ID) ||
		0)) {
		uint pll = PMU4716_MAINPLL_PLL0;

		if (BCM43602_CHIP(sih->chip)) {
			clock = si_pmu1_cpuclk0(sih, osh, pmu);
		} else {
			clock = si_pmu5_clock(sih, osh, pmu, pll, PMU5_MAINPLL_CPU);
		}
	} else {
		clock = si_pmu_si_clock(sih, osh);
	}

	if ((CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43014_CHIP_ID)) {
		/* Fout = (on_count + 1) * Fin/(on_count + 1 + off_count)
		* ARM clock using Fast divider calculation
		* Fin = FVCO/2
		*/
		tmp = si_pmu_chipcontrol(sih, PMU1_PLL0_CHIPCTL1, 0, 0);
		armclk_offcnt =
			(tmp & CCTL_43012_ARM_OFFCOUNT_MASK) >> CCTL_43012_ARM_OFFCOUNT_SHIFT;
		armclk_oncnt =
			(tmp & CCTL_43012_ARM_ONCOUNT_MASK) >> CCTL_43012_ARM_ONCOUNT_SHIFT;
		clock = (armclk_oncnt + 1) * clock/(armclk_oncnt + 1 + armclk_offcnt);
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
	return clock;
} /* si_pmu_cpu_clock */

#ifdef __ARM_ARCH_7A__
static uint32
si_pmu_mem_ca7clock(si_t *sih, osl_t *osh)
{
	uint32 clock = 0;
	int8 mdiv = 1;
	uint idx = si_coreidx(sih);
	bool fastclk;
	ca7regs_t *regs = si_setcore(sih, ARMCA7_CORE_ID, 0);

	if (regs == NULL) {
		goto end;
	}

	fastclk = ((R_REG(osh, ARMREG(regs, clk_ctl_st)) & CCS_ARMFASTCLOCKREQ) != 0);

	if (fastclk) {
		uint32 fvco = si_pmu_pll28nm_fvco(sih);
		if (si_corerev(sih) >= 7) {
			mdiv = (R_REG(osh, ARMREG(regs, corecontrol)) & ACC_CLOCKRATIO_MASK) >>
				ACC_CLOCKRATIO_SHIFT;
		} else {
			ASSERT(0);
		}

		if (mdiv == 0) {
			ASSERT(0);
			clock = 0;
		} else {
			clock = (fvco / mdiv);
		}
	} else {
		clock = si_pmu_si_clock(sih, osh);
	}

end:
	si_setcoreidx(sih, idx);
	return clock;

}
#endif /* __ARM_ARCH_7A__ */

/** get memory clock frequency, which is the same as the HT clock for newer chips. Returns [Hz]. */
uint32
BCMINITFN(si_pmu_mem_clock)(si_t *sih, osl_t *osh)
{
	pmuregs_t *pmu;
	uint origidx;
	uint32 clock;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	if ((PMUREV(sih->pmurev) >= 5) &&
		!((BCM4369_CHIP(sih->chip)) ||
		(BCM4362_CHIP(sih->chip)) ||
		BCM43602_CHIP(sih->chip) ||
		(CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43014_CHIP_ID) ||
		BCM4376_CHIP(sih->chip) ||
		BCM4378_CHIP(sih->chip) ||
		BCM4387_CHIP(sih->chip) ||
		BCM4388_CHIP(sih->chip) ||
		BCM4389_CHIP(sih->chip) ||
		BCM4397_CHIP(sih->chip) ||
		0)) {
		uint pll = PMU4716_MAINPLL_PLL0;

		clock = si_pmu5_clock(sih, osh, pmu, pll, PMU5_MAINPLL_MEM);
	} else {
#ifdef __ARM_ARCH_7A__
		clock = si_pmu_mem_ca7clock(sih, osh);
#else /* !__ARM_ARCH_7A__ */
		clock = si_pmu_si_clock(sih, osh); /* mem clk same as backplane clk */
#endif /* __ARM_ARCH_7A__ */
	}
	/* Return to original core */
	si_setcoreidx(sih, origidx);
	return clock;
} /* si_pmu_mem_clock */

/*
 * ilpcycles per sec are now calculated during CPU init in a new way
 * for better accuracy.  We set it here for compatability.
 *
 * On platforms that do not do this we resort to the old way.
 */

#define ILP_CALC_DUR	10	/* ms, make sure 1000 can be divided by it. */

static uint32 ilpcycles_per_sec = 0;

void
BCMPOSTTRAPFN(si_pmu_ilp_clock_set)(uint32 cycles_per_sec)
{
	ilpcycles_per_sec = cycles_per_sec;
}

/**
 * Measure ILP clock frequency. Returns a value in [Hz] units.
 *
 * The variable ilpcycles_per_sec is used to store the ILP clock speed. The value
 * is calculated when the function is called the first time and then cached.
 * The change in PMU timer count is measured across a delay of ILP_CALC_DUR msec.
 * Before the first time the function is called, one must make sure the HT clock is
 * turned on and used to feed the CPU and that OSL_DELAY() is calibrated.
 */
uint32
BCMINITFN(si_pmu_ilp_clock)(si_t *sih, osl_t *osh)
{
	if (ISSIM_ENAB(sih))
		return ILP_CLOCK;

	if (ilpcycles_per_sec == 0) {
		uint32 start, end, delta;
		pmuregs_t *pmu;
		uint origidx = si_coreidx(sih);

		if (AOB_ENAB(sih)) {
			pmu = si_setcore(sih, PMU_CORE_ID, 0);
		} else {
			pmu = si_setcoreidx(sih, SI_CC_IDX);
		}
		ASSERT(pmu != NULL);
		start = R_REG(osh, &pmu->pmutimer);
		/* PR88659: verify pmutimer reads */
		if (start != R_REG(osh, &pmu->pmutimer))
			start = R_REG(osh, &pmu->pmutimer);
		OSL_DELAY(ILP_CALC_DUR * 1000);
		end = R_REG(osh, &pmu->pmutimer);
		if (end != R_REG(osh, &pmu->pmutimer))
			end = R_REG(osh, &pmu->pmutimer);
		delta = end - start;
		ilpcycles_per_sec = delta * (1000 / ILP_CALC_DUR);
		/* Return to original core */
		si_setcoreidx(sih, origidx);
	}

	ASSERT(ilpcycles_per_sec != 0);
	return ilpcycles_per_sec;
}
#endif /* !defined(BCMDONGLEHOST) */

/**
 * Reads/writes a chipcontrol reg. Performes core switching if required, at function exit the
 * original core is restored. Depending on chip type, read/writes to chipcontrol regs in CC core
 * (older chips) or to chipcontrol regs in PMU core (later chips).
 */
uint32
BCMPOSTTRAPFN(si_pmu_chipcontrol)(si_t *sih, uint reg, uint32 mask, uint32 val)
{
	pmu_corereg(sih, SI_CC_IDX, chipcontrol_addr, ~0, reg);
	return pmu_corereg(sih, SI_CC_IDX, chipcontrol_data, mask, val);
}

/**
 * Reads/writes a voltage regulator (vreg) register. Performes core switching if required, at
 * function exit the original core is restored. Depending on chip type, writes to regulator regs
 * in CC core (older chips) or to regulator regs in PMU core (later chips).
 */
uint32
BCMPOSTTRAPFN(si_pmu_vreg_control)(si_t *sih, uint reg, uint32 mask, uint32 val)
{
	pmu_corereg(sih, SI_CC_IDX, regcontrol_addr, ~0, reg);
	return pmu_corereg(sih, SI_CC_IDX, regcontrol_data, mask, val);
}

/**
 * Reads/writes a PLL control register. Performes core switching if required, at function exit the
 * original core is restored. Depending on chip type, writes to PLL control regs in CC core (older
 * chips) or to PLL control regs in PMU core (later chips).
 */
uint32
BCMPOSTTRAPFN(si_pmu_pllcontrol)(si_t *sih, uint reg, uint32 mask, uint32 val)
{
	pmu_corereg(sih, SI_CC_IDX, pllcontrol_addr, ~0, reg);
	return pmu_corereg(sih, SI_CC_IDX, pllcontrol_data, mask, val);
}

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
BCMINITFN(si_sdiod_drive_strength_init)(si_t *sih, osl_t *osh, uint32 drivestrength)
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

#if !defined(BCMDONGLEHOST)
/** initialize PMU */
void
BCMATTACHFN(si_pmu_init)(si_t *sih, osl_t *osh)
{
	pmuregs_t *pmu;
	uint origidx;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

#if defined(BT_WLAN_REG_ON_WAR)
	si_pmu_reg_on_war_ext_wake_perst_clear(sih);
	si_pmu_reg_on_war_ext_wake_perst_set(sih);
#endif /* BT_WLAN_REG_ON_WAR */

	/* Feature is added in PMU rev. 1 but doesn't work until rev. 2 */
	if (PMUREV(sih->pmurev) == 1)
		AND_REG(osh, &pmu->pmucontrol, ~PCTL_NOILP_ON_WAIT);
	else if (PMUREV(sih->pmurev) >= 2)
		OR_REG(osh, &pmu->pmucontrol, PCTL_NOILP_ON_WAIT);

	/* Changes from PMU revision 26 are not included in revision 27 */
	if ((PMUREV(sih->pmurev) >= 26) && (PMUREV(sih->pmurev) != 27)) {
		uint32 val = PMU_INTC_ALP_REQ | PMU_INTC_HT_REQ | PMU_INTC_HQ_REQ;
		pmu_corereg(sih, SI_CC_IDX, pmuintctrl0, val, val);

		val = RSRC_INTR_MASK_TIMER_INT_0;
		pmu_corereg(sih, SI_CC_IDX, pmuintmask0, val, val);
		(void)pmu_corereg(sih, SI_CC_IDX, pmuintmask0, 0, 0);
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

uint32
si_pmu_rsrc_macphy_clk_deps(si_t *sih, osl_t *osh, int macunit)
{
	uint32 deps = 0;
	rsc_per_chip_t *rsc;
	uint origidx;
	pmuregs_t *pmu = NULL;
	uint8 rsc_num;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}

	ASSERT(pmu != NULL);

	rsc = si_pmu_get_rsc_positions(sih);
	if (macunit == 0) {
		rsc_num = rsc->macphy_clkavail;
	} else if (macunit == 1) {
		rsc_num = rsc->macphy_aux_clkavail;
	} else if (macunit == 2) {
		rsc_num = rsc->macphy_scan_clkavail;
	} else {
		PMU_ERROR(("si_pmu_rsrc_macphy_clk_deps: slice %d is not supported\n", macunit));
		rsc_num = NO_SUCH_RESOURCE;	/* to satisfy the compiler */
		ASSERT(0);
	}
	deps = si_pmu_res_deps(sih, osh, pmu, PMURES_BIT(rsc_num), TRUE);
	deps |= PMURES_BIT(rsc_num);

	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return deps;
}

void
si_pmu_set_mac_rsrc_req_sc(si_t *sih, osl_t *osh)
{
	uint32 deps = 0;
	rsc_per_chip_t *rsc;
	uint origidx;
	pmuregs_t *pmu = NULL;
	uint32 rsrc = 0;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	pmu = si_setcore(sih, PMU_CORE_ID, 0);
	ASSERT(pmu != NULL);

	rsc = si_pmu_get_rsc_positions(sih);

	rsrc = (PMURES_BIT(rsc->macphy_scan_clkavail) |
		PMURES_BIT(rsc->dig_ready));

	deps = si_pmu_res_deps(sih, osh, pmu, rsrc, TRUE);
	deps |= rsrc;

	W_REG(osh, &pmu->mac_res_req_timer2, PMU32_MAC_SCAN_RSRC_REQ_TIMER);
	W_REG(osh, &pmu->mac_res_req_mask2, deps);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

uint32
BCMATTACHFN(si_pmu_rsrc_ht_avail_clk_deps)(si_t *sih, osl_t *osh)
{
	uint32 deps;
	rsc_per_chip_t *rsc;
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

	rsc = si_pmu_get_rsc_positions(sih);
	deps = si_pmu_res_deps(sih, osh, pmu, PMURES_BIT(rsc->ht_avail), FALSE);
	deps |= PMURES_BIT(rsc->ht_avail);

	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return deps;
}

uint32
BCMATTACHFN(si_pmu_rsrc_cb_ready_deps)(si_t *sih, osl_t *osh)
{
	uint32 deps;
	rsc_per_chip_t *rsc;
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

	rsc = si_pmu_get_rsc_positions(sih);
	if (rsc->cb_ready == NO_SUCH_RESOURCE) {
		deps = 0;
	} else {
		deps = si_pmu_res_deps(sih, osh, pmu, PMURES_BIT(rsc->cb_ready), FALSE);
		deps |= PMURES_BIT(rsc->cb_ready);
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return deps;
}

void
si_pmu_set_mac_rsrc_req(si_t *sih, int macunit)
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

	if (macunit == 0) {
		W_REG(osh, &pmu->mac_res_req_timer, PMU32_MAC_MAIN_RSRC_REQ_TIMER);
		W_REG(osh, &pmu->mac_res_req_mask, si_pmu_rsrc_macphy_clk_deps(sih, osh, macunit));
	} else if (macunit == 1) {
		W_REG(osh, &pmu->mac_res_req_timer1, PMU32_MAC_AUX_RSRC_REQ_TIMER);
		W_REG(osh, &pmu->mac_res_req_mask1, si_pmu_rsrc_macphy_clk_deps(sih, osh, macunit));
	} else if (macunit == 2) {
		W_REG(osh, &pmu->mac_res_req_timer2, PMU32_MAC_SCAN_RSRC_REQ_TIMER);
		W_REG(osh, &pmu->mac_res_req_mask2, si_pmu_rsrc_macphy_clk_deps(sih, osh, macunit));
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}

/**
 * Return worst case up time in [ILP cycles] for the given resource.
 *
 * Example use case: the d11 core needs to be programmed with the max time it
 * takes to make the HT clock available.
 *
 * need to check circular dependancies and prevent dead recursion.
 */
static uint
BCMINITFN(si_pmu_res_uptime)(si_t *sih, osl_t *osh,
	pmuregs_t *pmu, uint8 rsrc, bool pmu_fast_trans_en)
{
	uint32 deps;
	uint uptime, i, dup, dmax, uptrans, ret;
	uint32 min_mask = 0;
#ifndef SR_DEBUG
	uint32 max_mask = 0;
#endif /* SR_DEBUG */

	/* uptime of resource 'rsrc' */
	W_REG(osh, &pmu->res_table_sel, rsrc);
	if (PMUREV(sih->pmurev) >= 30)
		uptime = (R_REG(osh, &pmu->res_updn_timer) >> 16) & 0x7fff;
	else if (PMUREV(sih->pmurev) >= 13)
		uptime = (R_REG(osh, &pmu->res_updn_timer) >> 16) & 0x3ff;
	else
		uptime = (R_REG(osh, &pmu->res_updn_timer) >> 8) & 0xff;

	/* direct dependencies of resource 'rsrc' */
	deps = si_pmu_res_deps(sih, osh, pmu, PMURES_BIT(rsrc), FALSE);
	for (i = 0; i <= PMURES_MAX_RESNUM; i ++) {
		if (!(deps & PMURES_BIT(i)))
			continue;
		deps &= ~si_pmu_res_deps(sih, osh, pmu, PMURES_BIT(i), TRUE);
	}
#ifndef SR_DEBUG
	si_pmu_res_masks(sih, &min_mask, &max_mask);
#else
	/* Recalculate fast pwr up delay if min res mask/max res mask has changed */
	min_mask = R_REG(osh, &pmu->min_res_mask);
#endif /* SR_DEBUG */
	deps &= ~min_mask;

	/* max uptime of direct dependencies */
	dmax = 0;
	for (i = 0; i <= PMURES_MAX_RESNUM; i ++) {
		if (!(deps & PMURES_BIT(i)))
			continue;
		dup = si_pmu_res_uptime(sih, osh, pmu, (uint8)i, pmu_fast_trans_en);
		if (dmax < dup)
			dmax = dup;
	}

	PMU_MSG(("si_pmu_res_uptime: rsrc %u uptime %u(deps 0x%08x uptime %u)\n",
	         rsrc, uptime, deps, dmax));

	uptrans = pmu_fast_trans_en ? 0 : PMURES_UP_TRANSITION;
	ret = uptime + dmax + uptrans;
	return ret;
}

/* Return dependencies (direct or all/indirect) for the given resources */
/* need to check circular dependencies and prevent dead recursion */
static uint32
si_pmu_res_deps(si_t *sih, osl_t *osh, pmuregs_t *pmu, uint32 rsrcs, bool all)
{
	uint32 deps = 0;
	uint32 i;

	for (i = 0; i <= PMURES_MAX_RESNUM; i ++) {
		if (!(rsrcs & PMURES_BIT(i)))
			continue;
		W_REG(osh, &pmu->res_table_sel, i);
		deps |= R_REG(osh, &pmu->res_dep_mask);
	}

	return !all ? deps : (deps ? (deps | si_pmu_res_deps(sih, osh, pmu, deps, TRUE)) : 0);
}

static bool
si_pmu_otp_is_ready(si_t *sih)
{
	uint32 otps = 0u;

	if (AOB_ENAB(sih)) {
		otps = si_corereg(sih, si_findcoreidx(sih, GCI_CORE_ID, 0u),
			OFFSETOF(gciregs_t, otpstatus), 0u, 0u);
	} else {
		otps = si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, otpstatus), 0u, 0u);
	}
	return !!(otps & OTPS_READY);
}

static bool
si_pmu_otp_is_ready_and_wait(si_t *sih, bool on)
{
	SPINWAIT((si_pmu_otp_is_ready(sih) != on), 3000u);

	if (si_pmu_otp_is_ready(sih) != on) {
		PMU_ERROR(("OTP ready bit not %s after wait\n", (on ? "Set" : "Clear")));
		OSL_SYS_HALT();
	}

	return si_pmu_otp_is_ready(sih) == on;
}

/**
 * OTP is powered down/up as a means of resetting it, or for saving current when OTP is unused.
 * OTP is powered up/down through PMU resources.
 * OTP will turn OFF only if its not in the dependency of any "higher" rsrc in min_res_mask
 */
void
si_pmu_otp_power(si_t *sih, osl_t *osh, bool on, uint32* min_res_mask)
{
	pmuregs_t *pmu;
	uint origidx;
	uint32 rsrcs = 0;	/* rsrcs to turn on/off OTP power */
	rsc_per_chip_t *rsc;	/* chip specific resource bit positions */

	ASSERT(sih->cccaps & CC_CAP_PMU);

	/* Don't do anything if OTP is disabled */
	if (si_is_otp_disabled(sih)) {
		PMU_MSG(("si_pmu_otp_power: OTP is disabled\n"));
		return;
	}

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	/*
	 * OTP can't be power cycled by toggling OTP_PU for always on OTP chips. For now
	 * corerev 45 is the only one that has always on OTP.
	 * Instead, the chipc register OTPCtrl1 (Offset 0xF4) bit 25 (forceOTPpwrDis) is used.
	 * Please refer to http://hwnbu-twiki.broadcom.com/bin/view/Mwgroup/ChipcommonRev45
	 */
	if (CCREV(sih->ccrev) == 45) {
		uint32 otpctrl1;
		otpctrl1 = si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, otpcontrol1), 0, 0);
		if (on)
			otpctrl1 &= ~OTPC_FORCE_PWR_OFF;
		else
			otpctrl1 |= OTPC_FORCE_PWR_OFF;
		si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, otpcontrol1), ~0, otpctrl1);
		/* Return to original core */
		si_setcoreidx(sih, origidx);
		return;
	}

	switch (CHIPID(sih->chip)) {
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
	case BCM4352_CHIP_ID:
	case BCM43526_CHIP_ID:
	case BCM4376_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:

#ifdef UNRELEASEDCHIP
#endif

		rsc = si_pmu_get_rsc_positions(sih);
		rsrcs = PMURES_BIT(rsc->otp_pu);
		break;
	case BCM4378_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		si_gci_direct(sih, GCI_OFFSETOF(sih, otpcontrol), OTPC_FORCE_OTP_PWR_DIS,
			on ? 0u : OTPC_FORCE_OTP_PWR_DIS);
		if (!si_pmu_otp_is_ready_and_wait(sih, on)) {
			PMU_MSG(("OTP ready bit not %s after wait\n", (on ? "ON" : "OFF")));
		}
		break;
	default:
		break;
	}

	if (rsrcs != 0) {
		bool on_check = FALSE; /* Stores otp_ready state */
		uint32 min_mask = 0;

		/* Turn on/off the power */
		if (on) {
			min_mask = R_REG(osh, &pmu->min_res_mask);
			*min_res_mask = min_mask;

			min_mask |= rsrcs;
			min_mask |= si_pmu_res_deps(sih, osh, pmu, min_mask, TRUE);
			on_check = TRUE;
			/* Assuming max rsc mask defines OTP_PU, so not programming max */
			PMU_MSG(("Adding rsrc 0x%x to min_res_mask\n", min_mask));
			W_REG(osh, &pmu->min_res_mask, min_mask);
			si_pmu_wait_for_steady_state(sih, osh, pmu);
			OSL_DELAY(1000);
			SPINWAIT(!(R_REG(osh, &pmu->res_state) & rsrcs),
				PMU_MAX_TRANSITION_DLY);
			ASSERT(R_REG(osh, &pmu->res_state) & rsrcs);
		} else {
			/*
			 * Restore back the min_res_mask,
			 * but keep OTP powered off if allowed by dependencies
			 */
			if (*min_res_mask)
				min_mask = *min_res_mask;
			else
				min_mask = R_REG(osh, &pmu->min_res_mask);

			min_mask &= ~rsrcs;
			/*
			 * OTP rsrc can be cleared only if its not
			 * in the dependency of any "higher" rsrc in min_res_mask
			 */
			min_mask |= si_pmu_res_deps(sih, osh, pmu, min_mask, TRUE);
			on_check = ((min_mask & rsrcs) != 0);

			PMU_MSG(("Removing rsrc 0x%x from min_res_mask\n", min_mask));
			W_REG(osh, &pmu->min_res_mask, min_mask);
			si_pmu_wait_for_steady_state(sih, osh, pmu);
		}

		if (!si_pmu_otp_is_ready_and_wait(sih, on_check)) {
			PMU_MSG(("OTP ready bit not %s after wait\n", (on_check ? "ON" : "OFF")));
		}
#ifdef NOT_YET
		/*
		 * FIXME: Temporarily disabling OTPS_READY ASSERT check. Right now ASSERT in
		 * ROM is enabled only for 4389B0/C0. Therefore this change anyway will not
		 * affect other chips. Once the correct spin-wait value is updated by the
		 * HW team, then this ASSERT will be enabled back.
		 */
		ASSERT(si_pmu_otp_is_ready(sih) == on_check);
#endif /* NOT_YET */
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
} /* si_pmu_otp_power */

void
si_pmu_spuravoid(si_t *sih, osl_t *osh, uint8 spuravoid)
{
	uint origidx;
	bcm_int_bitmask_t intr_val;

	/* Block ints and save current core */
	si_introff(sih, &intr_val);
	origidx = si_coreidx(sih);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
	si_intrrestore(sih, &intr_val);
} /* si_pmu_spuravoid */

/* below function are only for BBPLL parallel purpose */
/* For having the pllcontrol data values for spuravoid */
typedef struct {
	uint8	spuravoid_mode;
	uint8	pllctrl_reg;
	uint32	pllctrl_regval;
} pllctrl_spuravoid_t;

uint32
si_pmu_pll28nm_fvco(si_t *sih)
{
	uint32 r_high, r_low, r;
	uint32 xf = si_alp_clock(sih);
	/* PLL registers for 4368 */
	uint32 pllreg5 = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG5, 0, 0);
	uint32 pllreg4 = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG4, 0, 0);
	/* p1div has lower 2 bits in pll4 and high 2 bits in pll5  */
	uint8 p1div_lo = (pllreg4 & PMU4368_PLL1_PC4_P1DIV_MASK) >> PMU4368_PLL1_PC4_P1DIV_SHIFT;
	uint8 p1div_hi = (pllreg5 & PMU4368_PLL1_PC5_P1DIV_MASK) >> PMU4368_PLL1_PC5_P1DIV_SHIFT;
	uint8 p1div = (p1div_hi << PMU4368_P1DIV_HI_SHIFT) | (p1div_lo << PMU4368_P1DIV_LO_SHIFT);
	uint32 ndiv_int = (pllreg5 & PMU4368_PLL1_PC5_NDIV_INT_MASK) >>
		PMU4368_PLL1_PC5_NDIV_INT_SHIFT;
	uint32 ndiv_frac = (pllreg5 & PMU4368_PLL1_PC5_NDIV_FRAC_MASK) >>
		PMU4368_PLL1_PC5_NDIV_FRAC_SHIFT;

	if (ISSIM_ENAB(sih)) {
		/* PLL CTRL registers are meaningless under QT, return the pre-configured freq */
		return (FVCO_720 * 1000);
	} else if (p1div == 0) {
		/* PLL register read fails, return 0 so caller can retry */
		PMU_ERROR(("p1div is invalid\n"));
		return 0;
	}

	/* Calculate xf * ( ndiv_frac / (1 << 20) + ndiv_int) / p1div)
	 * To reduce the inaccuracy in division,
	 * Covert to (xf * ndiv_frac / (1 << 20) + xf * ndiv_int) / p1div
	 */
	math_uint64_multiple_add(&r_high, &r_low, xf, ndiv_frac, 0);
	/* Make sure the caclulated 64 bits number is in the safe rage (with in 52 bits),
	 * so we have a valid 32 bits result after divided by 1<<20
	 */
	ASSERT((r_high & 0xFFE00000) == 0);
	math_uint64_right_shift(&r, r_high, r_low, 20);

	return (r + ndiv_int * xf) / p1div;
}

bool
si_pmu_is_otp_powered(si_t *sih, osl_t *osh)
{
	uint idx;
	pmuregs_t *pmu;
	bool st;
	rsc_per_chip_t *rsc;		/* chip specific resource bit positions */

	/* Remember original core before switch to chipc/pmu */
	idx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	si_pmu_wait_for_steady_state(sih, osh, pmu);

	switch (CHIPID(sih->chip)) {
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
	case BCM4360_CHIP_ID:
	case BCM43460_CHIP_ID:
	case BCM43526_CHIP_ID:
	case BCM4352_CHIP_ID:
	case BCM4369_CHIP_GRPID:
	case BCM4362_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
		rsc = si_pmu_get_rsc_positions(sih);
		st = (R_REG(osh, &pmu->res_state) & PMURES_BIT(rsc->otp_pu)) != 0;
		break;
	case BCM4378_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		st = (!(si_gci_direct(sih, GCI_OFFSETOF(sih, otpcontrol), 0u, 0u) &
			OTPC_FORCE_OTP_PWR_DIS)) && si_pmu_otp_is_ready_and_wait(sih, TRUE);
		break;
	default:
		st = TRUE;
		break;
	}

	/* Return to original core */
	si_setcoreidx(sih, idx);
	return st;
} /* si_pmu_is_otp_powered */

/**
 * Some chip/boards can be optionally fitted with an external 32Khz clock source for increased power
 * savings (due to more accurate sleep intervals).
 */
static void
BCMATTACHFN(si_pmu_set_lpoclk)(si_t *sih, osl_t *osh)
{
	uint32 ext_lpo_sel, int_lpo_sel, timeout = 0,
		ext_lpo_avail = 0, lpo_sel = 0;
	uint32 ext_lpo_isclock; /* On e.g. 43602a0, either x-tal or clock can be on LPO pins */
	pmuregs_t *pmu;
	uint origidx;

	if (!(getintvar(NULL, "boardflags3")))
		return;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	ext_lpo_sel = getintvar(NULL, "boardflags3") & BFL3_FORCE_EXT_LPO_SEL;
	int_lpo_sel = getintvar(NULL, "boardflags3") & BFL3_FORCE_INT_LPO_SEL;
	ext_lpo_isclock = getintvar(NULL, "boardflags3") & BFL3_EXT_LPO_ISCLOCK;

	BCM_REFERENCE(ext_lpo_isclock);

	if (ext_lpo_sel != 0) {
		switch (CHIPID(sih->chip)) {
		CASE_BCM43602_CHIP:
			/* External LPO is POR default enabled */
			si_pmu_chipcontrol(sih, PMU_CHIPCTL2, PMU43602_CC2_XTAL32_SEL,
				ext_lpo_isclock ? 0 : PMU43602_CC2_XTAL32_SEL);
			break;
		default:
			/* Force External LPO Power Up */
			si_pmu_chipcontrol(sih, PMU_CHIPCTL0, CC_EXT_LPO_PU, CC_EXT_LPO_PU);
			si_gci_chipcontrol(sih, CHIPCTRLREG6, GC_EXT_LPO_PU, GC_EXT_LPO_PU);
			break;
		}

		ext_lpo_avail = R_REG(osh, &pmu->pmustatus) & EXT_LPO_AVAIL;
		while (ext_lpo_avail == 0 && timeout < LPO_SEL_TIMEOUT) {
			OSL_DELAY(1000);
			ext_lpo_avail = R_REG(osh, &pmu->pmustatus) & EXT_LPO_AVAIL;
			timeout++;
		}

		if (timeout >= LPO_SEL_TIMEOUT) {
			PMU_ERROR(("External LPO is not available\n"));
		} else {
			/* External LPO is available, lets use (=select) it */
			OSL_DELAY(1000);
			timeout = 0;

			switch (CHIPID(sih->chip)) {
			CASE_BCM43602_CHIP:
				si_pmu_chipcontrol(sih, PMU_CHIPCTL2, PMU43602_CC2_FORCE_EXT_LPO,
					PMU43602_CC2_FORCE_EXT_LPO); /* switches to external LPO */
				break;
			default:
				/* Force External LPO Sel up */
				si_gci_chipcontrol(sih, CHIPCTRLREG6, EXT_LPO_SEL, EXT_LPO_SEL);
				/* Clear Force Internal LPO Sel */
				si_gci_chipcontrol(sih, CHIPCTRLREG6, INT_LPO_SEL, 0x0);
				OSL_DELAY(1000);

				lpo_sel = R_REG(osh, &pmu->pmucontrol) & LPO_SEL;
				while (lpo_sel != 0 && timeout < LPO_SEL_TIMEOUT) {
					OSL_DELAY(1000);
					lpo_sel = R_REG(osh, &pmu->pmucontrol) & LPO_SEL;
					timeout++;
				}
			}

			if (timeout >= LPO_SEL_TIMEOUT) {
				PMU_ERROR(("External LPO is not set\n"));
				/* Clear Force External LPO Sel */
				switch (CHIPID(sih->chip)) {
				CASE_BCM43602_CHIP:
					si_pmu_chipcontrol(sih, PMU_CHIPCTL2,
						PMU43602_CC2_FORCE_EXT_LPO, 0);
					break;
				default:
					si_gci_chipcontrol(sih, CHIPCTRLREG6, EXT_LPO_SEL, 0x0);
					break;
				}
			} else {
				/* Clear Force Internal LPO Power Up */
				switch (CHIPID(sih->chip)) {
				CASE_BCM43602_CHIP:
					break;
				default:
					si_pmu_chipcontrol(sih, PMU_CHIPCTL0, CC_INT_LPO_PU, 0x0);
					si_gci_chipcontrol(sih, CHIPCTRLREG6, GC_INT_LPO_PU, 0x0);
					break;
				}
			} /* if (timeout) */
		} /* if (timeout) */
	} else if (int_lpo_sel != 0) {
		switch (CHIPID(sih->chip)) {
		CASE_BCM43602_CHIP:
			break; /* do nothing, internal LPO is POR default powered and selected */
		default:
			/* Force Internal LPO Power Up */
			si_pmu_chipcontrol(sih, PMU_CHIPCTL0, CC_INT_LPO_PU, CC_INT_LPO_PU);
			si_gci_chipcontrol(sih, CHIPCTRLREG6, GC_INT_LPO_PU, GC_INT_LPO_PU);

			OSL_DELAY(1000);

			/* Force Internal LPO Sel up */
			si_gci_chipcontrol(sih, CHIPCTRLREG6, INT_LPO_SEL, INT_LPO_SEL);
			/* Clear Force External LPO Sel */
			si_gci_chipcontrol(sih, CHIPCTRLREG6, EXT_LPO_SEL, 0x0);

			OSL_DELAY(1000);

			lpo_sel = R_REG(osh, &pmu->pmucontrol) & LPO_SEL;
			timeout = 0;
			while (lpo_sel == 0 && timeout < LPO_SEL_TIMEOUT) {
				OSL_DELAY(1000);
				lpo_sel = R_REG(osh, &pmu->pmucontrol) & LPO_SEL;
				timeout++;
			}
			if (timeout >= LPO_SEL_TIMEOUT) {
				PMU_ERROR(("Internal LPO is not set\n"));
				/* Clear Force Internal LPO Sel */
				si_gci_chipcontrol(sih, CHIPCTRLREG6, INT_LPO_SEL, 0x0);
			} else {
				/* Clear Force External LPO Power Up */
				si_pmu_chipcontrol(sih, PMU_CHIPCTL0, CC_EXT_LPO_PU, 0x0);
				si_gci_chipcontrol(sih, CHIPCTRLREG6, GC_EXT_LPO_PU, 0x0);
			}
			break;
		}
		if ((PMUREV(sih->pmurev) >= 33)) {
			/* Enabling FAST_SEQ */
			PMU_REG(sih, pmucontrol_ext, PCTL_EXT_FASTSEQ_ENAB, PCTL_EXT_FASTSEQ_ENAB);
		}
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
} /* si_pmu_set_lpoclk */

static int
si_pmu_fast_lpo_locked(si_t *sih, osl_t *osh)
{
	int lock = 0;
	switch (CHIPID(sih->chip)) {
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		lock = CHIPC_REG(sih, chipstatus, 0, 0) & CST43012_FLL_LOCK;
		break;
	case BCM4369_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		lock = si_gci_chipstatus(sih, GCI_CHIPSTATUS_13) & GCI_CS_4369_FLL1MHZ_LOCK_MASK;
		break;
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		lock = si_gci_chipstatus(sih, GCI_CHIPSTATUS_15) & GCI_CS_4387_FLL1MHZ_LOCK_MASK;
		break;
	default:
		PMU_MSG(("si_pmu_fast_lpo_locked: LPO enable: unsupported chip!\n"));
	}
	return lock ? 1 : 0;
}

/* Turn ON FAST LPO FLL (1MHz) */
static void
BCMATTACHFN(si_pmu_fast_lpo_enable)(si_t *sih, osl_t *osh)
{
	int i = 0, lock = 0;

	BCM_REFERENCE(i);
	BCM_REFERENCE(lock);

	switch (CHIPID(sih->chip)) {
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		PMU_REG(sih, pmucontrol_ext, PCTL_EXT_FASTLPO_ENAB, PCTL_EXT_FASTLPO_ENAB);
		lock = CHIPC_REG(sih, chipstatus, 0, 0) & CST43012_FLL_LOCK;

		for (i = 0; ((i <= 30) && (!lock)); i++)
		{
			lock = CHIPC_REG(sih, chipstatus, 0, 0) & CST43012_FLL_LOCK;
			OSL_DELAY(10);
		}

		PMU_MSG(("si_pmu_fast_lpo_enable: duration: %d\n", i*10));

		if (!lock) {
			PMU_MSG(("si_pmu_fast_lpo_enable: FLL lock not present!"));
			ROMMABLE_ASSERT(0);
		}

		/* Now switch to using FAST LPO clk */
		PMU_REG(sih, pmucontrol_ext, PCTL_EXT_FASTLPO_SWENAB, PCTL_EXT_FASTLPO_SWENAB);
		break;
	case BCM4369_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
	{
		uint8	fastlpo_dis = fastlpo_dis_get();
		uint8	fastlpo_pcie_dis = fastlpo_pcie_dis_get();

		if (!fastlpo_dis || !fastlpo_pcie_dis) {
			/* LHL rev 6 in 4387 requires this bit to be set first */
			if ((LHLREV(sih->lhlrev) >= 6) && !PMU_FLL_PU_ENAB()) {
				LHL_REG(sih, lhl_top_pwrseq_ctl_adr,
				        LHL_PWRSEQCTL_PMU_LPLDO_PD, LHL_PWRSEQCTL_WL_FLLPU_EN);
			}

			PMU_REG(sih, pmucontrol_ext, PCTL_EXT_FASTLPO_ENAB, PCTL_EXT_FASTLPO_ENAB);

			lock = si_pmu_fast_lpo_locked(sih, osh);
			for (i = 0; ((i < 300) && (!lock)); i++) {
				lock = si_pmu_fast_lpo_locked(sih, osh);
				OSL_DELAY(10);
			}
			ASSERT(lock);
		}

		if (!fastlpo_dis) {
			/* Now switch to using FAST LPO clk */
			PMU_REG(sih, pmucontrol_ext,
				PCTL_EXT_FASTLPO_SWENAB, PCTL_EXT_FASTLPO_SWENAB);

			OSL_DELAY(1000);
			PMU_MSG(("pmu fast lpo enabled\n"));
		}
		break;
	}
	default:
		PMU_MSG(("si_pmu_fast_lpo_enable: LPO enable: unsupported chip!\n"));
	}
}

/* Turn ON FAST LPO FLL (1MHz) for PCIE */
bool
BCMATTACHFN(si_pmu_fast_lpo_enable_pcie)(si_t *sih)
{
	if (!FASTLPO_ENAB()) {
		return FALSE;
	}

	switch (CHIPID(sih->chip)) {
	case BCM4369_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
	{
		uint8	fastlpo_pcie_dis = fastlpo_pcie_dis_get();

		if (!fastlpo_pcie_dis) {
			PMU_REG(sih, pmucontrol_ext,
				PCTL_EXT_FASTLPO_PCIE_SWENAB, PCTL_EXT_FASTLPO_PCIE_SWENAB);
			OSL_DELAY(1000);
			PMU_MSG(("pcie fast lpo enabled\n"));
			return TRUE;
		}
		break;
	}
	default:
		PMU_MSG(("si_pmu_fast_lpo_enable_pcie: LPO enable: unsupported chip!\n"));
	}

	return FALSE;
}

/* Turn ON FAST LPO FLL (1MHz) for PMU */
bool
BCMATTACHFN(si_pmu_fast_lpo_enable_pmu)(si_t *sih)
{
	if (!FASTLPO_ENAB()) {
		return FALSE;
	}

	switch (CHIPID(sih->chip)) {
	case BCM4369_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
	{
		uint8	fastlpo_dis = fastlpo_dis_get();

		if (!fastlpo_dis) {
			PMU_MSG(("pmu fast lpo enabled\n"));
			return TRUE;
		}
		break;
	}
	default:
		PMU_MSG(("si_pmu_fast_lpo_enable_pmu: LPO enable: unsupported chip!\n"));
	}

	return FALSE;
}

static uint8
BCMATTACHFN(fastlpo_dis_get)(void)
{
	uint8 fastlpo_dis = 1;

#if defined(BCM_FASTLPO_PMU) && !defined(BCM_FASTLPO_PMU_DISABLED)
	if (FASTLPO_ENAB()) {
		fastlpo_dis = 0;
		if (getvar(NULL, rstr_fastlpo_dis) != NULL) {
			fastlpo_dis = (uint8)getintvar(NULL, rstr_fastlpo_dis);
		}
	}
#endif  /* BCM_FASTLPO_PMU */
	return fastlpo_dis;
}

static uint8
BCMATTACHFN(fastlpo_pcie_dis_get)(void)
{
	uint8 fastlpo_pcie_dis = 1;

	if (FASTLPO_ENAB()) {
		fastlpo_pcie_dis = 0;
		if (getvar(NULL, rstr_fastlpo_pcie_dis) != NULL) {
			fastlpo_pcie_dis = (uint8)getintvar(NULL, rstr_fastlpo_pcie_dis);
		}
	}
	return fastlpo_pcie_dis;
}

static void
BCMATTACHFN(si_pmu_fll_preload_enable)(si_t *sih)
{
	if (!PMU_FLL_PU_ENAB()) {
		return;
	}

	switch (CHIPID(sih->chip)) {
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	{
		uint32 fll_dac_out;

		fll_dac_out = (si_gci_chipstatus(sih, GCI_CHIPSTATUS_15) &
			GCI_CS_4387_FLL1MHZ_DAC_OUT_MASK)
			>> GCI_CS_4387_FLL1MHZ_DAC_OUT_SHIFT;

		LHL_REG(sih, lhl_wl_hw_ctl_adr[1],
			LHL_1MHZ_FLL_DAC_EXT_MASK,
			(fll_dac_out) << LHL_1MHZ_FLL_DAC_EXT_SHIFT);
		LHL_REG(sih, lhl_wl_hw_ctl_adr[1],
			LHL_1MHZ_FLL_PRELOAD_MASK,
			LHL_1MHZ_FLL_PRELOAD_MASK);
		break;
	}
	default:
		PMU_MSG(("si_pmu_fll_preload_enable: unsupported chip!\n"));
		ASSERT(0);
		break;
	}
}

/* LV sleep mode summary:
 * LV mode is where both ABUCK and CBUCK are programmed to low voltages during
 * sleep, and VMUX selects ABUCK as VDDOUT_AON. LPLDO needs to power off.
 * With ASR ON, LPLDO OFF
 */
#if defined(SAVERESTORE)
static void
BCMATTACHFN(si_set_lv_sleep_mode_pmu)(si_t *sih, osl_t *osh)
{
	/* jtag_udr_write USER_REG9W jtag_serdes_pic_enable 1 */
	if (BCM4369_CHIP(sih->chip) && (CHIPREV(sih->chiprev) == 0)) {
		si_pmu_chipcontrol(sih, PMU_CHIPCTL4, PMU_CC4_4369_AUX_PD_MEMLPLDO2VDDB_ON, 0);

		//JTAG_SEL override. When this bit is set, jtag_sel 0, Required for JTAG writes
		/* Temporarily we are disabling this as it is not required..
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_06, 0x10, 0x10);
		jtag_setbit_128(sih, 9, 103, 1);
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_06, 0x10, 0x0);
		*/

	}

	/* Program pmu VREG resgiter for Resouce based ABUCK and CBUCK modes
	 *      cbuck rsrc 0 - PWM and abuck rsrc 0 - Auto, rsrc 1 - PWM
	 */
	si_pmu_vreg_control(sih, PMU_VREG_16, PMU_4369_VREG16_RSRC0_CBUCK_MODE_MASK,
		0x3u << PMU_4369_VREG16_RSRC0_CBUCK_MODE_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_16, PMU_4369_VREG16_RSRC0_ABUCK_MODE_MASK,
		0x3u << PMU_4369_VREG16_RSRC0_ABUCK_MODE_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_16, PMU_4369_VREG16_RSRC1_ABUCK_MODE_MASK,
		0x3u << PMU_4369_VREG16_RSRC1_ABUCK_MODE_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_16, PMU_4369_VREG16_RSRC2_ABUCK_MODE_MASK,
		0x3u << PMU_4369_VREG16_RSRC2_ABUCK_MODE_SHIFT);

	/* asr voltage adjust PWM - 0.8V */
	si_pmu_vreg_control(sih, PMU_VREG_8, PMU_4369_VREG8_ASR_OVADJ_LPPFM_MASK,
		0x10u << PMU_4369_VREG8_ASR_OVADJ_LPPFM_SHIFT);

	/* Enable rsrc_en_asr_msk[0] and msk[1] */
	si_pmu_vreg_control(sih, PMU_VREG_13, PMU_4369_VREG13_RSRC_EN0_ASR_MASK,
		0x1u << PMU_4369_VREG13_RSRC_EN0_ASR_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_13, PMU_4369_VREG13_RSRC_EN1_ASR_MASK,
		0x1u << PMU_4369_VREG13_RSRC_EN1_ASR_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_13, PMU_4369_VREG13_RSRC_EN2_ASR_MASK,
		0x1u << PMU_4369_VREG13_RSRC_EN2_ASR_SHIFT);

	si_pmu_vreg_control(sih, PMU_VREG_14, PMU_4369_VREG14_RSRC_EN_CSR_MASK0_MASK,
		0x1u << PMU_4369_VREG14_RSRC_EN_CSR_MASK0_SHIFT);

	/* disable force_hp_mode and enable wl_pmu_lv_mod */
	si_pmu_vreg_control(sih, PMU_VREG_7,
		(PMU_4369_VREG_7_WL_PMU_LV_MODE_MASK | PMU_4369_VREG_7_WL_PMU_LP_MODE_MASK |
		PMU_4369_VREG_7_PMU_FORCE_HP_MODE_MASK), PMU_4369_VREG_7_WL_PMU_LV_MODE_MASK);

	/* Enable MISCLDO only for A0, MEMLPLDO_adj -0.7V, Disable LPLDO power up */
	/* For 4387, should not disable because this is PU when analog PMU is out of sleep
	 * and bypass when in sleep mode
	 */
	if (!(BCM4389_CHIP(sih->chip) || BCM4388_CHIP(sih->chip) || BCM4397_CHIP(sih->chip) ||
		BCM4387_CHIP(sih->chip))) {
		si_pmu_vreg_control(sih, PMU_VREG_5, PMU_4369_VREG_5_MISCLDO_POWER_UP_MASK,
			((CHIPREV(sih->chiprev) == 0) ? 1 : 0) <<
			  PMU_4369_VREG_5_MISCLDO_POWER_UP_SHIFT);
	}
	si_pmu_vreg_control(sih, PMU_VREG_5, PMU_4369_VREG_5_LPLDO_POWER_UP_MASK, 0x0u);
	si_pmu_vreg_control(sih, PMU_VREG_5, PMU_4369_VREG_5_MEMLPLDO_OP_VLT_ADJ_CTRL_MASK,
		0xDu << PMU_4369_VREG_5_MEMLPLDO_OP_VLT_ADJ_CTRL_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_5, PMU_4369_VREG_5_LPLDO_OP_VLT_ADJ_CTRL_MASK,
		0xFu << PMU_4369_VREG_5_LPLDO_OP_VLT_ADJ_CTRL_SHIFT);

	/* Enabale MEMLPLDO ( to enable 0x08)and BTLDO is enabled. At sleep RFLDO is disabled */
	si_pmu_vreg_control(sih, PMU_VREG_6, PMU_4369_VREG_6_MEMLPLDO_POWER_UP_MASK,
		0x1u << PMU_4369_VREG_6_MEMLPLDO_POWER_UP_SHIFT);

	/* Program PMU chip cntrl register to control
	 *     cbuck2vddb_pwrsw_force_on =1 and memlpldo2vddb_pwrsw_force_off = 1
	 *     cbuck2ret_pwrsw_force_on = 1 and memlpldo2vddb_pwrsw_force_off = 1
	 *     set d11_2x2_bw80_cbuck2vddb_pwrsw_force_on and
	 *     d11_2x2_bw20_cbuck2vddb_pwrsw_force_on cbuck2ret_pwrsw on 4 cores
	 */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
		(PMU_CC4_4369_MAIN_PD_CBUCK2VDDB_ON | PMU_CC4_4369_MAIN_PD_CBUCK2VDDRET_ON |
		PMU_CC4_4369_MAIN_PD_MEMLPLDO2VDDB_ON | PMU_CC4_4369_MAIN_PD_MEMLPDLO2VDDRET_ON),
		(PMU_CC4_4369_MAIN_PD_CBUCK2VDDB_ON | PMU_CC4_4369_MAIN_PD_CBUCK2VDDRET_ON));
	si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
		(PMU_CC4_4369_AUX_PD_CBUCK2VDDB_ON | PMU_CC4_4369_AUX_PD_CBUCK2VDDRET_ON |
		PMU_CC4_4369_AUX_PD_MEMLPLDO2VDDB_ON | PMU_CC4_4369_AUX_PD_MEMLPLDO2VDDRET_ON),
		(PMU_CC4_4369_AUX_PD_CBUCK2VDDB_ON | PMU_CC4_4369_AUX_PD_CBUCK2VDDRET_ON));

	/* set subcore_cbuck2vddb_pwrsw_force_on */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL5,
		(PMU_CC5_4369_SUBCORE_CBUCK2VDDB_ON | PMU_CC5_4369_SUBCORE_CBUCK2VDDRET_ON |
		PMU_CC5_4369_SUBCORE_MEMLPLDO2VDDB_ON | PMU_CC5_4369_SUBCORE_MEMLPLDO2VDDRET_ON),
		(PMU_CC5_4369_SUBCORE_CBUCK2VDDB_ON | PMU_CC5_4369_SUBCORE_CBUCK2VDDRET_ON));

	/* Set subcore_memlpldo2vddb_pwrsw_force_off, d11_2x2_bw80_memlpldo2vddb_pwrsw_force_off
	 * and d11_2x2_bw20_memlpldo2vddb_pwrsw_force_off
	 * Set subcore_memlpldo2vddret_pwrsw_force_off,d11_2x2_bw80_memlpldo2vddret_pwrsw_force_off
	 * and d11_2x2_bw20_memlpldo2vddret_pwrsw_force_off
	 */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
		(PMU_CC13_SUBCORE_CBUCK2VDDB_OFF | PMU_CC13_SUBCORE_CBUCK2VDDRET_OFF |
		PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF | PMU_CC13_SUBCORE_MEMLPLDO2VDDRET_OFF),
		(PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF | PMU_CC13_SUBCORE_MEMLPLDO2VDDRET_OFF));
	si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
		(PMU_CC13_MAIN_CBUCK2VDDB_OFF | PMU_CC13_MAIN_CBUCK2VDDRET_OFF |
		PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF | PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF),
		(PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF | PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF));
	si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
		(PMU_CC13_AUX_CBUCK2VDDB_OFF | PMU_CC13_AUX_CBUCK2VDDRET_OFF |
		PMU_CC13_AUX_MEMLPLDO2VDDB_OFF | PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF),
		(PMU_CC13_AUX_MEMLPLDO2VDDB_OFF | PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF));

	/* PCIE retention mode enable */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL6,
		PMU_CC6_ENABLE_PCIE_RETENTION, PMU_CC6_ENABLE_PCIE_RETENTION);
}

static void
BCMATTACHFN(si_set_lv_sleep_mode_4369)(si_t *sih, osl_t *osh)
{
	si_set_lv_sleep_mode_pmu(sih, osh);

	si_set_lv_sleep_mode_lhl_config_4369(sih);

	/* Enable PMU interrupts */
	CHIPC_REG(sih, intmask, (1u << 4u), (1u << 4u));
}

void si_set_abuck_mode_4362(si_t *sih, uint8 mode)
{

	if (mode < 2 || mode > 4) {
		ASSERT(0);
		return;
	}

	si_pmu_vreg_control(sih, PMU_VREG_16, PMU_4362_VREG16_RSRC0_ABUCK_MODE_MASK,
		mode << PMU_4362_VREG16_RSRC0_ABUCK_MODE_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_16, PMU_4362_VREG16_RSRC1_ABUCK_MODE_MASK,
		mode << PMU_4362_VREG16_RSRC1_ABUCK_MODE_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_16, PMU_4362_VREG16_RSRC2_ABUCK_MODE_MASK,
		mode << PMU_4362_VREG16_RSRC2_ABUCK_MODE_SHIFT);
}

static void
BCMATTACHFN(si_set_lv_sleep_mode_4378)(si_t *sih, osl_t *osh)
{
	si_set_lv_sleep_mode_pmu(sih, osh);

	si_set_lv_sleep_mode_lhl_config_4378(sih);
}

static void
BCMATTACHFN(si_set_lv_sleep_mode_pmu_4387)(si_t *sih, osl_t *osh)
{
	/* Program pmu VREG resgiter for Resouce based ABUCK and CBUCK modes
	 *      cbuck rsrc 0 - PWM and abuck rsrc 0 - Auto, rsrc 1 - PWM
	 */
	si_pmu_vreg_control(sih, PMU_VREG_16, PMU_4369_VREG16_RSRC1_ABUCK_MODE_MASK,
		0x2u << PMU_4369_VREG16_RSRC1_ABUCK_MODE_SHIFT);

	/* asr voltage adjust PWM - 0.8V */
	si_pmu_vreg_control(sih, PMU_VREG_8, PMU_4369_VREG8_ASR_OVADJ_LPPFM_MASK,
		0x10u << PMU_4369_VREG8_ASR_OVADJ_LPPFM_SHIFT);

	/* Enable rsrc_en_asr_msk[0] and msk[1] */
	si_pmu_vreg_control(sih, PMU_VREG_13, PMU_4369_VREG13_RSRC_EN0_ASR_MASK,
		0x1u << PMU_4369_VREG13_RSRC_EN0_ASR_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_13, PMU_4369_VREG13_RSRC_EN1_ASR_MASK,
		0x1u << PMU_4369_VREG13_RSRC_EN1_ASR_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_13, PMU_4369_VREG13_RSRC_EN2_ASR_MASK,
		0x1u << PMU_4369_VREG13_RSRC_EN2_ASR_SHIFT);

	si_pmu_vreg_control(sih, PMU_VREG_14, PMU_4369_VREG14_RSRC_EN_CSR_MASK0_MASK,
		0x1u << PMU_4369_VREG14_RSRC_EN_CSR_MASK0_SHIFT);

	/* disable force_hp_mode and enable wl_pmu_lv_mod */
	si_pmu_vreg_control(sih, PMU_VREG_7,
		(PMU_4369_VREG_7_WL_PMU_LV_MODE_MASK | PMU_4369_VREG_7_WL_PMU_LP_MODE_MASK |
		PMU_4369_VREG_7_PMU_FORCE_HP_MODE_MASK), PMU_4369_VREG_7_WL_PMU_LV_MODE_MASK);

	/* Enabale MEMLPLDO ( to enable 0x08)and BTLDO is enabled. At sleep RFLDO is disabled */
	si_pmu_vreg_control(sih, PMU_VREG_6, PMU_4369_VREG_6_MEMLPLDO_POWER_UP_MASK,
		0x1u << PMU_4369_VREG_6_MEMLPLDO_POWER_UP_SHIFT);

	/* For 4387C0, we don't need memlpldo2vddret_on nor cldo2vddb_on.
	 * We just need to clear the memlpldo2vddb_forceoff to turn on all the memlpldo2vddb pwrsw
	 */
	if (PMUREV(sih->pmurev) < 39) {
		/* Program PMU chip cntrl register to control
		 *     cbuck2vddb_pwrsw_force_on =1 and memlpldo2vddb_pwrsw_force_off = 1
		 *     cbuck2ret_pwrsw_force_on = 1 and memlpldo2vddb_pwrsw_force_off = 1
		 *     set d11_2x2_bw80_cbuck2vddb_pwrsw_force_on and
		 *     d11_2x2_bw20_cbuck2vddb_pwrsw_force_on cbuck2ret_pwrsw on 4 cores
		 */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
			(PMU_CC4_4369_MAIN_PD_CBUCK2VDDB_ON | PMU_CC4_4369_MAIN_PD_CBUCK2VDDRET_ON |
			PMU_CC4_4369_MAIN_PD_MEMLPLDO2VDDB_ON |
			PMU_CC4_4369_MAIN_PD_MEMLPDLO2VDDRET_ON),
			(PMU_CC4_4369_MAIN_PD_CBUCK2VDDB_ON |
			PMU_CC4_4369_MAIN_PD_CBUCK2VDDRET_ON));
		si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
			(PMU_CC4_4369_AUX_PD_CBUCK2VDDB_ON | PMU_CC4_4369_AUX_PD_CBUCK2VDDRET_ON |
			PMU_CC4_4369_AUX_PD_MEMLPLDO2VDDB_ON |
			PMU_CC4_4369_AUX_PD_MEMLPLDO2VDDRET_ON),
			(PMU_CC4_4369_AUX_PD_CBUCK2VDDB_ON | PMU_CC4_4369_AUX_PD_CBUCK2VDDRET_ON));

		/* set subcore_cbuck2vddb_pwrsw_force_on */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL5,
			(PMU_CC5_4369_SUBCORE_CBUCK2VDDB_ON | PMU_CC5_4369_SUBCORE_CBUCK2VDDRET_ON |
			PMU_CC5_4369_SUBCORE_MEMLPLDO2VDDB_ON |
			PMU_CC5_4369_SUBCORE_MEMLPLDO2VDDRET_ON),
			(PMU_CC5_4369_SUBCORE_CBUCK2VDDB_ON |
			PMU_CC5_4369_SUBCORE_CBUCK2VDDRET_ON));

		/* Set subcore_memlpldo2vddb_pwrsw_force_off,
		 * d11_2x2_bw80_memlpldo2vddb_pwrsw_force_off
		 * and d11_2x2_bw20_memlpldo2vddb_pwrsw_force_off
		 * Set subcore_memlpldo2vddret_pwrsw_force_off,
		 * d11_2x2_bw80_memlpldo2vddret_pwrsw_force_off
		 * and d11_2x2_bw20_memlpldo2vddret_pwrsw_force_off
		 */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
			(PMU_CC13_SUBCORE_CBUCK2VDDB_OFF | PMU_CC13_SUBCORE_CBUCK2VDDRET_OFF |
			PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF | PMU_CC13_SUBCORE_MEMLPLDO2VDDRET_OFF),
			(PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF |
			PMU_CC13_SUBCORE_MEMLPLDO2VDDRET_OFF));
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
			(PMU_CC13_MAIN_CBUCK2VDDB_OFF | PMU_CC13_MAIN_CBUCK2VDDRET_OFF |
			PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF | PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF),
			(PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF | PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF));
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
			(PMU_CC13_AUX_CBUCK2VDDB_OFF | PMU_CC13_AUX_CBUCK2VDDRET_OFF |
			PMU_CC13_AUX_MEMLPLDO2VDDB_OFF | PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF),
			(PMU_CC13_AUX_MEMLPLDO2VDDB_OFF | PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF));
		si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
			PMU_CC4_4369_AUX_PD_MEMLPLDO2VDDRET_ON,
			PMU_CC4_4369_AUX_PD_MEMLPLDO2VDDRET_ON);
		si_pmu_chipcontrol(sih, PMU_CHIPCTL5,
			PMU_CC5_4369_SUBCORE_MEMLPLDO2VDDRET_ON,
			PMU_CC5_4369_SUBCORE_MEMLPLDO2VDDRET_ON),
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
			PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF |
			PMU_CC13_SUBCORE_MEMLPLDO2VDDRET_OFF |
			PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF |
			PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF |
			PMU_CC13_AUX_MEMLPLDO2VDDB_OFF |
			PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF |
			PMU_CC13_CMN_MEMLPLDO2VDDRET_ON,
			PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF |
			PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF |
			PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF |
			PMU_CC13_AUX_MEMLPLDO2VDDB_OFF |
			PMU_CC13_CMN_MEMLPLDO2VDDRET_ON);
		si_pmu_chipcontrol(sih, PMU_CHIPCTL17,
			PMU_CC17_SCAN_MEMLPLDO2VDDRET_ON |
			PMU_CC17_SCAN_CBUCK2VDDB_ON |
			PMU_CC17_SCAN_MEMLPLDO2VDDRET_OFF,
			PMU_CC17_SCAN_MEMLPLDO2VDDRET_ON |
			PMU_CC17_SCAN_CBUCK2VDDB_ON);
	} else {
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
			PMU_CC13_CMN_MEMLPLDO2VDDRET_ON |
			PMU_CC13_AUX_MEMLPLDO2VDDB_OFF |
			PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF |
			PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF,
			PMU_CC13_CMN_MEMLPLDO2VDDRET_ON);
		si_pmu_chipcontrol(sih, PMU_CHIPCTL17,
			PMU_CC17_SCAN_MEMLPLDO2VDDB_OFF, 0);
	}

	/* PCIE retention mode enable */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL6,
		PMU_CC6_ENABLE_PCIE_RETENTION, PMU_CC6_ENABLE_PCIE_RETENTION);

	/* H/W JIRA http://jira.broadcom.com/browse/HW4387-825
	 * B0 only, the h/w bug is fixed in C0
	 */
	if (PMUREV(sih->pmurev) == 38) {
		si_pmu_vreg_control(sih, PMU_VREG_14,
			PMU_VREG14_RSRC_EN_ASR_PWM_PFM_MASK,
			PMU_VREG14_RSRC_EN_ASR_PWM_PFM_MASK);
	}

	/* WAR for jira HW4387-922 */
	si_pmu_vreg_control(sih, PMU_VREG_1,
		PMU_4387_VREG1_CSR_OVERI_DIS_MASK,
		PMU_4387_VREG1_CSR_OVERI_DIS_MASK);

	/* Clear Misc_LDO override */
	si_pmu_vreg_control(sih, PMU_VREG_5, VREG5_4387_MISCLDO_PU_MASK, 0);

	si_pmu_vreg_control(sih, PMU_VREG_8,
		PMU_4387_VREG8_ASR_OVERI_DIS_MASK,
		PMU_4387_VREG8_ASR_OVERI_DIS_MASK);

	if (BCMSRTOPOFF_ENAB()) {
		si_pmu_vreg_control(sih, PMU_VREG_6,
			PMU_4387_VREG6_WL_PMU_LV_MODE_MASK, 0);

		/* Clear memldo_pu bit as 4387 doesn't plan to use MEMLDO */
		si_pmu_vreg_control(sih, PMU_VREG_6,
			PMU_4387_VREG6_MEMLDO_PU_MASK, 0);
	} else {
		si_pmu_vreg_control(sih, PMU_VREG_6,
			PMU_4387_VREG6_WL_PMU_LV_MODE_MASK,
			PMU_4387_VREG6_WL_PMU_LV_MODE_MASK);
	}
}

static void
BCMATTACHFN(si_set_lv_sleep_mode_4387)(si_t *sih, osl_t *osh)
{
	si_set_lv_sleep_mode_pmu_4387(sih, osh);
	si_set_lv_sleep_mode_lhl_config_4387(sih);
}

static void
BCMATTACHFN(si_set_lv_sleep_mode_4389)(si_t *sih, osl_t *osh)
{
	si_set_lv_sleep_mode_pmu(sih, osh);

	si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
		PMU_CC4_4387_MAIN_PD_CBUCK2VDDRET_ON |
		PMU_CC4_4387_AUX_PD_CBUCK2VDDRET_ON,
		0);
	si_pmu_chipcontrol(sih, PMU_CHIPCTL5,
		PMU_CC5_4387_SUBCORE_CBUCK2VDDRET_ON,
		0);
	si_pmu_chipcontrol(sih, PMU_CHIPCTL6,
		PMU_CC6_RX4_CLK_SEQ_SELECT_MASK,
		0);
	/* Disable lq_clk - HW4387-254 */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL12,
		PMU_CC12_DISABLE_LQ_CLK_ON,
		PMU_CC12_DISABLE_LQ_CLK_ON);
	si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
		PMU_CC13_SUBCORE_MEMLPLDO2VDDRET_OFF |
		PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF |
		PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF,
		0);

#ifdef NOT_YET
	/* FIXME: this setting is causing the load switch from CSR to ASR */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
		PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF |
		PMU_CC13_SUBCORE_MEMLPLDO2VDDRET_OFF |
		PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF |
		PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF |
		PMU_CC13_AUX_MEMLPLDO2VDDB_OFF |
		PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF |
		PMU_CC13_CMN_MEMLPLDO2VDDRET_ON, 0);
#endif /* NOT_YET */

	si_pmu_chipcontrol(sih, PMU_CHIPCTL17,
		PMU_CC17_SCAN_MEMLPLDO2VDDRET_ON |
		PMU_CC17_SCAN_CBUCK2VDDB_ON |
		PMU_CC17_SCAN_MEMLPLDO2VDDRET_OFF,
		PMU_CC17_SCAN_MEMLPLDO2VDDRET_ON |
		PMU_CC17_SCAN_CBUCK2VDDB_ON);

	si_set_lv_sleep_mode_lhl_config_4389(sih);

	si_pmu_vreg_control(sih, PMU_VREG_6,
		(PMU_4389_VREG6_WL_PMU_LV_MODE_MASK | PMU_4389_VREG6_MEMLDO_PU_MASK),
		PMU_4389_VREG6_WL_PMU_LV_MODE_MASK);

	/* SW WAR for 4389B0(rev 01) issue - HW4387-922. 4389C0(rev 02) already has HW fix */
	if (CHIPREV(sih->chiprev) == 1) {
		si_pmu_vreg_control(sih, PMU_VREG_1,
			PMU_4387_VREG1_CSR_OVERI_DIS_MASK,
			PMU_4387_VREG1_CSR_OVERI_DIS_MASK);

		si_pmu_vreg_control(sih, PMU_VREG_8,
			PMU_4387_VREG8_ASR_OVERI_DIS_MASK,
			PMU_4387_VREG8_ASR_OVERI_DIS_MASK);
	}
}

static void
BCMATTACHFN(si_set_lv_sleep_mode_4362)(si_t *sih, osl_t *osh)
{
	/* Program pmu VREG resgiter for Resouce based ABUCK and CBUCK modes
	 *      cbuck rsrc 0 - PWM and abuck rsrc 0 - Auto, rsrc 1 - PWM
	 */
	si_pmu_vreg_control(sih, PMU_VREG_16, PMU_4362_VREG16_RSRC0_CBUCK_MODE_MASK,
		0x3u << PMU_4362_VREG16_RSRC0_CBUCK_MODE_SHIFT);

	si_set_abuck_mode_4362(sih, 0x3u);

	/* asr voltage adjust PWM - 0.8V */
	si_pmu_vreg_control(sih, PMU_VREG_8, PMU_4362_VREG8_ASR_OVADJ_LPPFM_MASK,
		0x10u << PMU_4362_VREG8_ASR_OVADJ_LPPFM_SHIFT);

	/* Enable rsrc_en_asr_msk[0] and msk[1] */
	si_pmu_vreg_control(sih, PMU_VREG_13, PMU_4362_VREG13_RSRC_EN0_ASR_MASK,
		0x1u << PMU_4362_VREG13_RSRC_EN0_ASR_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_13, PMU_4362_VREG13_RSRC_EN1_ASR_MASK,
		0x1u << PMU_4362_VREG13_RSRC_EN1_ASR_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_13, PMU_4362_VREG13_RSRC_EN2_ASR_MASK,
		0x1u << PMU_4362_VREG13_RSRC_EN2_ASR_SHIFT);

	si_pmu_vreg_control(sih, PMU_VREG_14, PMU_4362_VREG14_RSRC_EN_CSR_MASK0_MASK,
		0x1u << PMU_4362_VREG14_RSRC_EN_CSR_MASK0_SHIFT);

	/* disable force_hp_mode and enable wl_pmu_lv_mod */
	si_pmu_vreg_control(sih, PMU_VREG_7,
		(PMU_4362_VREG_7_WL_PMU_LV_MODE_MASK | PMU_4362_VREG_7_WL_PMU_LP_MODE_MASK |
		PMU_4362_VREG_7_PMU_FORCE_HP_MODE_MASK), PMU_4362_VREG_7_WL_PMU_LV_MODE_MASK);

	/* Enable MISCLDO, MEMLPLDO_adj -0.7V, Disable LPLDO power up */
	si_pmu_vreg_control(sih, PMU_VREG_5, PMU_4362_VREG_5_MISCLDO_POWER_UP_MASK,
		0x1u << PMU_4362_VREG_5_MISCLDO_POWER_UP_SHIFT);
	si_pmu_vreg_control(sih, PMU_VREG_5, PMU_4362_VREG_5_LPLDO_POWER_UP_MASK, 0x0u);
	si_pmu_vreg_control(sih, PMU_VREG_5, PMU_4362_VREG_5_MEMLPLDO_OP_VLT_ADJ_CTRL_MASK,
		0xBu << PMU_4362_VREG_5_MEMLPLDO_OP_VLT_ADJ_CTRL_SHIFT);

	/* Enabale MEMLPLDO ( to enable 0x08)and BTLDO is enabled. At sleep RFLDO is disabled */
	si_pmu_vreg_control(sih, PMU_VREG_6, PMU_4362_VREG_6_MEMLPLDO_POWER_UP_MASK,
		0x1u << PMU_4362_VREG_6_MEMLPLDO_POWER_UP_SHIFT);

	/* Program PMU chip cntrl register to control
	 *     cbuck2vddb_pwrsw_force_on =1 and memlpldo2vddb_pwrsw_force_off = 1
	 *     cbuck2ret_pwrsw_force_on = 1 and memlpldo2vddb_pwrsw_force_off = 1
	 *     set d11_2x2_bw80_cbuck2vddb_pwrsw_force_on and
	 *     d11_2x2_bw20_cbuck2vddb_pwrsw_force_on cbuck2ret_pwrsw on 4 cores
	 */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
		(PMU_CC4_4362_PD_CBUCK2VDDB_ON | PMU_CC4_4362_PD_CBUCK2VDDRET_ON |
		PMU_CC4_4362_PD_MEMLPLDO2VDDB_ON | PMU_CC4_4362_PD_MEMLPDLO2VDDRET_ON),
		(PMU_CC4_4362_PD_CBUCK2VDDB_ON | PMU_CC4_4362_PD_CBUCK2VDDRET_ON));

	/* set subcore_cbuck2vddb_pwrsw_force_on */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL5,
		(PMU_CC5_4362_SUBCORE_CBUCK2VDDB_ON | PMU_CC5_4362_SUBCORE_CBUCK2VDDRET_ON |
		PMU_CC5_4362_SUBCORE_MEMLPLDO2VDDB_ON | PMU_CC5_4362_SUBCORE_MEMLPLDO2VDDRET_ON),
		(PMU_CC5_4362_SUBCORE_CBUCK2VDDB_ON | PMU_CC5_4362_SUBCORE_CBUCK2VDDRET_ON));

	/* Set subcore_memlpldo2vddb_pwrsw_force_off, d11_2x2_bw80_memlpldo2vddb_pwrsw_force_off
	 * and d11_2x2_bw20_memlpldo2vddb_pwrsw_force_off
	 * Set subcore_memlpldo2vddret_pwrsw_force_off,d11_2x2_bw80_memlpldo2vddret_pwrsw_force_off
	 * and d11_2x2_bw20_memlpldo2vddret_pwrsw_force_off
	 */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
		(PMU_CC13_SUBCORE_CBUCK2VDDB_OFF | PMU_CC13_SUBCORE_CBUCK2VDDRET_OFF |
		PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF | PMU_CC13_SUBCORE_MEMLPLDO2VDDRET_OFF),
		(PMU_CC13_SUBCORE_MEMLPLDO2VDDB_OFF | PMU_CC13_SUBCORE_MEMLPLDO2VDDRET_OFF));
	si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
		(PMU_CC13_MAIN_CBUCK2VDDB_OFF | PMU_CC13_MAIN_CBUCK2VDDB_OFF |
		PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF | PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF),
		(PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF | PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF));

	/* PCIE retention mode enable */
	si_pmu_chipcontrol(sih, PMU_CHIPCTL6,
		PMU_CC6_ENABLE_PCIE_RETENTION, PMU_CC6_ENABLE_PCIE_RETENTION);

	si_set_lv_sleep_mode_lhl_config_4362(sih);

	/* Enable PMU interrupts */
	CHIPC_REG(sih, intmask, (1u << 4u), (1u << 4u));
}

void
BCMATTACHFN(si_pmu_fis_setup)(si_t *sih)
{
	uint origidx;
	pmuregs_t *pmu;
	int val;
	osl_t *osh = si_osh(sih);

	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	switch (CHIPID(sih->chip)) {
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		val = R_REG(osh, &pmu->max_res_mask);
		W_REG(osh, &pmu->fis_start_min_res_mask, val);

		val = R_REG(osh, &pmu->min_res_mask);
		W_REG(osh, &pmu->fis_min_res_mask, val);

		W_REG(osh, &pmu->fis_ctrl_status,
			(PMU_FIS_DN_TIMER_VAL_4378 << PMU_FIS_DN_TIMER_VAL_SHIFT)
			& PMU_FIS_DN_TIMER_VAL_MASK);
		break;
	case BCM4388_CHIP_GRPID:
		val = R_REG(osh, &pmu->max_res_mask);
		W_REG(osh, &pmu->fis_start_min_res_mask, val);

		val = R_REG(osh, &pmu->min_res_mask);
		W_REG(osh, &pmu->fis_min_res_mask, val);

		W_REG(osh, &pmu->fis_ctrl_status,
			((PMU_FIS_DN_TIMER_VAL_4388 << PMU_FIS_DN_TIMER_VAL_SHIFT)
			 & PMU_FIS_DN_TIMER_VAL_MASK) | PMU_FIS_PCIE_SAVE_EN_VALUE);
		break;
	case BCM4389_CHIP_GRPID:
		val = R_REG(osh, &pmu->max_res_mask);
		W_REG(osh, &pmu->fis_start_min_res_mask, val);

		val = R_REG(osh, &pmu->min_res_mask);
		W_REG(osh, &pmu->fis_min_res_mask, val);

		W_REG(osh, &pmu->fis_ctrl_status,
			((PMU_FIS_DN_TIMER_VAL_4389 << PMU_FIS_DN_TIMER_VAL_SHIFT)
			 & PMU_FIS_DN_TIMER_VAL_MASK) | PMU_FIS_PCIE_SAVE_EN_VALUE);
		break;

	default:
		break;
	}
	si_setcoreidx(sih, origidx);
}
#endif /* defined(SAVERESTORE) */

/*
 * Enable: Dynamic Clk Switching
 * Disable: Mirrored Mode
 * use nvram to enable
 */
static void
BCMATTACHFN(si_pmu_dynamic_clk_switch_enab)(si_t *sih)
{
	if (PMUREV(sih->pmurev) >= 36) {
		if (getintvar(NULL, rstr_dyn_clksw_en)) {
			PMU_REG(sih, pmucontrol_ext,
				PCTL_EXT_REQ_MIRROR_ENAB, 0);
			si_pmu_chipcontrol(sih, PMU_CHIPCTL2,
				CC2_4378_USE_WLAN_BP_CLK_ON_REQ_MASK |
				CC2_4378_USE_CMN_BP_CLK_ON_REQ_MASK,
				0);
		}
	}
}

/* use pmu rsrc XTAL_PU to count deep sleep of chip */
static void
BCMATTACHFN(si_pmu_enb_slp_cnt_on_rsrc)(si_t *sih, osl_t *osh)
{
	uint origidx;
	pmuregs_t *pmu;
	uint32 rsrc_slp = 0xffffffff;

	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	switch (CHIPID(sih->chip)) {

	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		rsrc_slp = RES4378_XTAL_PU;
		break;

	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
		rsrc_slp = RES4387_XTAL_PU;
		break;

	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		rsrc_slp = RES4389_XTAL_PU;
		break;

	default:
		break;
	}

	if (rsrc_slp != 0xffffffff) {
		W_REG(osh, &pmu->rsrc_event0, PMURES_BIT(rsrc_slp));
	}

	si_setcoreidx(sih, origidx);
}

#define MISC_LDO_STEPPING_DELAY	(150u)	/* 150 us, includes 50us additional margin */

/** initialize PMU chip controls and other chip level stuff */
void
BCMATTACHFN(si_pmu_chip_init)(si_t *sih, osl_t *osh)
{
	ASSERT(sih->cccaps & CC_CAP_PMU);
	if (AOB_ENAB(sih)) {
		if (hnd_pmur == NULL) {
			uint coreidx = si_coreidx(sih);
			hnd_pmur = si_setcore(sih, PMU_CORE_ID, 0);
			ASSERT(hnd_pmur != NULL);
			/* Restore to CC */
			si_setcoreidx(sih, coreidx);
		}
	}

	si_pmu_otp_chipcontrol(sih, osh);

#ifdef CHIPC_UART_ALWAYS_ON
	si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), CCS_FORCEALP, CCS_FORCEALP);
#endif /* CHIPC_UART_ALWAYS_ON */

	si_pmu_enb_slp_cnt_on_rsrc(sih, osh);

	/* Misc. chip control, has nothing to do with PMU */
	switch (CHIPID(sih->chip)) {

	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
	{
#ifdef USE_LHL_TIMER
		si_pmu_chipcontrol(sih, PMU_CHIPCTL2, PMUCCTL02_43012_LHL_TIMER_SELECT,
			PMUCCTL02_43012_LHL_TIMER_SELECT);
#else
		si_pmu_chipcontrol(sih, PMU_CHIPCTL2, PMUCCTL02_43012_LHL_TIMER_SELECT, 0);
#endif /* USE_LHL_TIMER */

		si_pmu_chipcontrol(sih, PMU_CHIPCTL2, PMUCCTL02_43012_RFLDO3P3_PU_FORCE_ON, 0);
		si_pmu_chipcontrol(sih, PMU_CHIPCTL4, PMUCCTL14_43012_DISABLE_LQ_AVAIL, 0);

		PMU_REG_NEW(sih, extwakemask0,
				PMU_EXT_WAKE_MASK_0_SDIO, PMU_EXT_WAKE_MASK_0_SDIO);
		PMU_REG_NEW(sih, extwakereqmask[0], ~0, si_pmu_rsrc_ht_avail_clk_deps(sih, osh));

		if (sih->lpflags & LPFLAGS_SI_FORCE_PWM_WHEN_RADIO_ON) {
			/* Force PWM when Radio ON */
			/* 2G_Listen/2G_RX/2G_TX/5G_Listen/5G_RX/5G_TX = PWM */
			si_pmu_vreg_control(sih, PMU_VREG_8,
				PMU_43012_VREG8_DYNAMIC_CBUCK_MODE_MASK,
				PMU_43012_VREG8_DYNAMIC_CBUCK_MODE0);
			si_pmu_vreg_control(sih, PMU_VREG_9,
				PMU_43012_VREG9_DYNAMIC_CBUCK_MODE_MASK,
				PMU_43012_VREG9_DYNAMIC_CBUCK_MODE0);
		}
		else {
			/* LPPFM opt setting for ePA */
			si_pmu_chipcontrol(sih, PMU_CHIPCTL16, PMU_CC16_CLK4M_DIS, 1);
			si_pmu_chipcontrol(sih, PMU_CHIPCTL16, PMU_CC16_FF_ZERO_ADJ, 4);
			/* 2G_Listen/2G_RX = LPPFM, 2G_TX/5G_Listen/5G_RX/5G_TX = PWM */
			si_pmu_vreg_control(sih, PMU_VREG_8,
				PMU_43012_VREG8_DYNAMIC_CBUCK_MODE_MASK,
				PMU_43012_VREG8_DYNAMIC_CBUCK_MODE1);
			si_pmu_vreg_control(sih, PMU_VREG_9,
				PMU_43012_VREG9_DYNAMIC_CBUCK_MODE_MASK,
				PMU_43012_VREG9_DYNAMIC_CBUCK_MODE1);
		}
		/* Set external LPO */
		si_lhl_set_lpoclk(sih, osh, LHL_LPO_AUTO);

		/* Enabling WL2CDIG sleep */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL2, PMUCCTL02_43012_WL2CDIG_I_PMU_SLEEP_ENAB,
			PMUCCTL02_43012_WL2CDIG_I_PMU_SLEEP_ENAB);

		si_pmu_chipcontrol(sih, PMU_CHIPCTL9,
			PMUCCTL09_43012_XTAL_CORESIZE_BIAS_ADJ_STARTUP_MASK,
			PMUCCTL09_43012_XTAL_CORESIZE_BIAS_ADJ_STARTUP_VAL <<
				PMUCCTL09_43012_XTAL_CORESIZE_BIAS_ADJ_STARTUP_SHIFT);

		/* Setting MemLPLDO voltage to 0.74 */
		si_pmu_vreg_control(sih, PMU_VREG_6, VREG6_43012_MEMLPLDO_ADJ_MASK,
			0x8 << VREG6_43012_MEMLPLDO_ADJ_SHIFT);

		/* Setting LPLDO voltage to 0.8 */
		si_pmu_vreg_control(sih, PMU_VREG_6, VREG6_43012_LPLDO_ADJ_MASK,
			0xB << VREG6_43012_LPLDO_ADJ_SHIFT);

		/* Turn off power switch 1P8 in sleep */
		si_pmu_vreg_control(sih, PMU_VREG_7, VREG7_43012_PWRSW_1P8_PU_MASK, 0);

		/* Enable PMU sleep mode0 (DS0-PS0) */
		LHL_REG(sih, lhl_top_pwrseq_ctl_adr, ~0, PMU_SLEEP_MODE_0);

		si_pmu_fast_lpo_enable(sih, osh);

		/* Enable the 'power kill' (power off selected retention memories) */
		GCI_REG_NEW(sih, bt_smem_control0, GCI_BT_SMEM_CTRL0_SUBCORE_ENABLE_PKILL,
			GCI_BT_SMEM_CTRL0_SUBCORE_ENABLE_PKILL);

		break;
	}
	case BCM4362_CHIP_GRPID:
	{
		pmuregs_t *pmu = si_setcore(sih, PMU_CORE_ID, 0);
		uint32 lpo = LHL_LPO_AUTO;
		uint32 lhl_tmr_sel = 0;

		/* DMAHANG WAR:SWWLAN:171729
		 * Stretch the ALP and HT clocks after de-asserting
		 * the request. During the RX frame transfer from RXFIFO to
		 * DP FIFO, in certain cases the clock is getting de-asserted
		 * by ucode as it does not have visibility beyond BM
		 */
		W_REG(osh, &pmu->clkstretch, 0x0fff0fff);

#ifdef USE_LHL_TIMER
		lhl_tmr_sel = PMU_CC13_LHL_TIMER_SELECT;
#endif /* USE_LHL_TIMER */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13, PMU_CC13_LHL_TIMER_SELECT, lhl_tmr_sel);

		if (R_REG(osh, &pmu->pmustatus) & PST_EXTLPOAVAIL) {
			lpo = LHL_EXT_LPO_ENAB;
		}

		if (!ISSIM_ENAB(sih)) {
			si_lhl_set_lpoclk(sih, osh, lpo);
		}

		if (getintvar(NULL, rstr_btldo3p3pu)) {
			si_pmu_regcontrol(sih, 4,
				PMU_28NM_VREG4_WL_LDO_CNTL_EN,
				PMU_28NM_VREG4_WL_LDO_CNTL_EN);
			si_pmu_regcontrol(sih, 6,
				PMU_28NM_VREG6_BTLDO3P3_PU,
				PMU_28NM_VREG6_BTLDO3P3_PU);
		}

		/* write the XTAL preferred startup/normal A0/B0 revision */
		si_pmu_chipcontrol_xtal_settings_4362(sih);

		si_pmu_chipcontrol(sih, PMU_CHIPCTL6,
			(PMU_CC6_ENABLE_CLKREQ_WAKEUP | PMU_CC6_ENABLE_PCIE_RETENTION),
			(PMU_CC6_ENABLE_CLKREQ_WAKEUP | PMU_CC6_ENABLE_PCIE_RETENTION));

		si_pmu_vreg_control(sih, PMU_VREG_8, PMU_4362_VREG8_ASR_OVADJ_LPPFM_MASK,
			0x02u << PMU_4362_VREG8_ASR_OVADJ_LPPFM_SHIFT);
		si_pmu_vreg_control(sih, PMU_VREG_8, PMU_4362_VREG8_ASR_OVADJ_PFM_MASK,
			0x02u << PMU_4362_VREG8_ASR_OVADJ_PFM_SHIFT);
		si_pmu_vreg_control(sih, PMU_VREG_8, PMU_4362_VREG8_ASR_OVADJ_PWM_MASK,
			0x02u << PMU_4362_VREG8_ASR_OVADJ_PWM_SHIFT);
#if defined(SAVERESTORE)
		if (SR_ENAB()) {
			si_set_lv_sleep_mode_4362(sih, osh);
		}
#endif /* SAVERESTORE */

		si_pmu_fast_lpo_enable(sih, osh);
		if ((PMUREV(sih->pmurev) >= 33) && FASTLPO_ENAB()) {
			/* Enabling FAST_SEQ */
			uint8	fastlpo_dis = fastlpo_dis_get();
			uint8	fastlpo_pcie_dis = fastlpo_pcie_dis_get();
			if (!fastlpo_dis || !fastlpo_pcie_dis) {
				PMU_REG(sih, pmucontrol_ext, PCTL_EXT_FASTSEQ_ENAB,
					PCTL_EXT_FASTSEQ_ENAB);
			}
		}

		break;
	}

	case BCM4369_CHIP_GRPID:
	{
		pmuregs_t *pmu = si_setcore(sih, PMU_CORE_ID, 0);
		uint32 lpo = LHL_LPO_AUTO;
		uint32 lhl_tmr_sel = 0;

		/* DMAHANG WAR:SWWLAN:171729
		 * Stretch the ALP and HT clocks after de-asserting
		 * the request. During the RX frame transfer from RXFIFO to
		 * DP FIFO, in certain cases the clock is getting de-asserted
		 * by ucode as it does not have visibility beyond BM
		 */
#ifndef ATE_BUILD
		W_REG(osh, &pmu->clkstretch, 0x0fff0fff);
#endif

#ifdef USE_LHL_TIMER
		lhl_tmr_sel = PMU_CC13_4369_LHL_TIMER_SELECT;
#endif
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13, PMU_CC13_4369_LHL_TIMER_SELECT, lhl_tmr_sel);

		if (R_REG(osh, &pmu->pmustatus) & PST_EXTLPOAVAIL) {
			lpo = LHL_EXT_LPO_ENAB;
		}

		if (!ISSIM_ENAB(sih)) {
			si_lhl_set_lpoclk(sih, osh, lpo);
		}

		if (getintvar(NULL, rstr_btldo3p3pu)) {
			si_pmu_regcontrol(sih, 4,
				PMU_28NM_VREG4_WL_LDO_CNTL_EN,
				PMU_28NM_VREG4_WL_LDO_CNTL_EN);
			si_pmu_regcontrol(sih, 6,
				PMU_28NM_VREG6_BTLDO3P3_PU,
				PMU_28NM_VREG6_BTLDO3P3_PU);
		}

		/* write the XTAL preferred startup/normal A0/B0 revision */
		si_pmu_chipcontrol_xtal_settings_4369(sih);

		si_pmu_chipcontrol(sih, PMU_CHIPCTL6,
			(PMU_CC6_ENABLE_CLKREQ_WAKEUP | PMU_CC6_ENABLE_PCIE_RETENTION),
			(PMU_CC6_ENABLE_CLKREQ_WAKEUP | PMU_CC6_ENABLE_PCIE_RETENTION));

		/* write the PWRSW CLK start/stop delay only for A0 revision */
		if (CHIPREV(sih->chiprev) == 0) {
			si_pmu_chipcontrol(sih, PMU_CHIPCTL1, PMU_CC1_PWRSW_CLKSTRSTP_DELAY_MASK,
				PMU_CC1_PWRSW_CLKSTRSTP_DELAY);
		}

#if defined(SAVERESTORE)
		if (SR_ENAB()) {
			si_set_lv_sleep_mode_4369(sih, osh);
		}
#endif /* SAVERESTORE */

		si_pmu_fast_lpo_enable(sih, osh);
#ifdef BCM_LDO3P3_SOFTSTART
		if (CHIPID(sih->chip) != BCM4377_CHIP_ID) {
			si_pmu_ldo3p3_soft_start_wl_set(sih, osh, 3);
		}
#endif
		if ((PMUREV(sih->pmurev) >= 33) && FASTLPO_ENAB()) {
			/* Enabling FAST_SEQ */
			uint8	fastlpo_dis = fastlpo_dis_get();
			uint8	fastlpo_pcie_dis = fastlpo_pcie_dis_get();
			if (!fastlpo_dis || !fastlpo_pcie_dis) {
				PMU_REG(sih, pmucontrol_ext, PCTL_EXT_FASTSEQ_ENAB,
					PCTL_EXT_FASTSEQ_ENAB);
			}
		}

		break;
	}

	CASE_BCM43602_CHIP: /* fall through */
		/* Set internal/external LPO */
		si_pmu_set_lpoclk(sih, osh);
		break;

	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	{
		pmuregs_t *pmu = si_setcore(sih, PMU_CORE_ID, 0);
		uint32 lpo = LHL_LPO_AUTO;
		uint32 lhl_tmr_sel = 0;

#ifdef USE_LHL_TIMER
		lhl_tmr_sel = PMU_CC13_4378_LHL_TIMER_SELECT;
#endif
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13, PMU_CC13_4378_LHL_TIMER_SELECT, lhl_tmr_sel);

		if (R_REG(osh, &pmu->pmustatus) & PST_EXTLPOAVAIL) {
			lpo = LHL_EXT_LPO_ENAB;
		}

		if (!ISSIM_ENAB(sih)) {
			si_lhl_set_lpoclk(sih, osh, lpo);
		}

		/* JIRA: SWWLAN-228979
		 * BT LDO is required for Aux 2G Tx only. Keep powerd down until Aux is up
		 */
		si_pmu_bt_ldo_pu(sih, FALSE);

		/* Updating xtal pmu registers to combat slow powerup issue */
		si_pmu_chipcontrol_xtal_settings_4378(sih);

		if (LHL_IS_PSMODE_1(sih)) {
			si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_07,
				((1 << GCI_CC7_AAON_BYPASS_PWRSW_SEL) |
				(1 << GCI_CC7_AAON_BYPASS_PWRSW_SEQ_ON)),
				0);
		}

		si_lhl_setup(sih, osh);

		/* Setting MemLPLDO voltage */
		if (getvar(NULL, rstr_memlpldo_volt) != NULL) {
			int memlpldo_volt = getintvar(NULL, rstr_memlpldo_volt);

			if (memlpldo_volt >= PMU_VREG5_LPLDO_VOLT_0_90 &&
				memlpldo_volt <= PMU_VREG5_LPLDO_VOLT_0_88) {
				si_pmu_regcontrol(sih, PMU_VREG_5, VREG5_4378_MEMLPLDO_ADJ_MASK,
					memlpldo_volt << VREG5_4378_MEMLPLDO_ADJ_SHIFT);
			} else {
				PMU_MSG(("Invalid memlpldo value: %d\n", memlpldo_volt));
			}
		}

		/* Setting LPLDO voltage */
		if (getvar(NULL, rstr_lpldo_volt) != NULL) {
			int lpldo_volt = getintvar(NULL, rstr_lpldo_volt);

			if (lpldo_volt >= PMU_VREG5_LPLDO_VOLT_0_90 &&
				lpldo_volt <= PMU_VREG5_LPLDO_VOLT_0_88) {
				si_pmu_regcontrol(sih, PMU_VREG_5, VREG5_4378_LPLDO_ADJ_MASK,
					lpldo_volt << VREG5_4378_LPLDO_ADJ_SHIFT);
			} else {
				PMU_MSG(("Invalid lpldo value: %d\n", lpldo_volt));
			}
		}

		/* Enable fast LPO */
		si_pmu_fast_lpo_enable(sih, osh);

#if defined(SAVERESTORE)
		if (SR_ENAB()) {
			si_set_lv_sleep_mode_4378(sih, osh);
		}
#endif /* SAVERESTORE */

		si_pmu_dynamic_clk_switch_enab(sih);

		if (CHIPID(sih->chip) == BCM4378_CHIP_GRPID) {
			si_pmu_vreg_control(sih, PMU_VREG_0,
				VREG0_4378_CSR_VOLT_ADJ_PWM_MASK |
				VREG0_4378_CSR_VOLT_ADJ_PFM_MASK |
				VREG0_4378_CSR_VOLT_ADJ_LP_PFM_MASK |
				VREG0_4378_CSR_OUT_VOLT_TRIM_ADJ_MASK,
				(CSR_VOLT_ADJ_PWM_4378 << VREG0_4378_CSR_VOLT_ADJ_PWM_SHIFT) |
				(CSR_VOLT_ADJ_PFM_4378 << VREG0_4378_CSR_VOLT_ADJ_PFM_SHIFT) |
				(CSR_VOLT_ADJ_LP_PFM_4378 << VREG0_4378_CSR_VOLT_ADJ_LP_PFM_SHIFT) |
				(CSR_OUT_VOLT_TRIM_ADJ_4378 <<
					VREG0_4378_CSR_OUT_VOLT_TRIM_ADJ_SHIFT));
#ifdef BCM_LDO3P3_SOFTSTART
			si_pmu_ldo3p3_soft_start_wl_set(sih, osh, 0x03u);
			si_pmu_ldo3p3_soft_start_bt_set(sih, osh, 0x03u);
#endif
		} else {
			/* 4368 */
			int nvcsr;
			if ((nvcsr = getintvar(NULL, rstr_csrtune))) {
				si_pmu_vreg_control(sih, PMU_VREG_0,
					VREG0_4378_CSR_VOLT_ADJ_PWM_MASK |
					VREG0_4378_CSR_VOLT_ADJ_PFM_MASK |
					VREG0_4378_CSR_VOLT_ADJ_LP_PFM_MASK,
					(nvcsr << VREG0_4378_CSR_VOLT_ADJ_PWM_SHIFT) |
					(nvcsr << VREG0_4378_CSR_VOLT_ADJ_PFM_SHIFT) |
					(nvcsr << VREG0_4378_CSR_VOLT_ADJ_LP_PFM_SHIFT));
			}
		}
	}
		break;

	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	{
		pmuregs_t *pmu = si_setcore(sih, PMU_CORE_ID, 0);
		uint32 lpo = LHL_LPO_AUTO;
		uint32 lhl_tmr_sel = 0;
		uint32 abuck_volt, cbuck_volt;
		uint32 min_mask;
		uint32 misc_ldo_volt, curr_misc_ldo_volt, i;

#ifdef DONGLEBUILD
		si_set_arm_clkfreq_high(sih);
#endif

		if (PMU_FLL_PU_ENAB()) {
			min_mask = R_REG(osh, &pmu->min_res_mask) |
				PMURES_BIT(RES4387_FAST_LPO_AVAIL) |
				PMURES_BIT(RES4387_PMU_LP);
			W_REG(osh, &pmu->min_res_mask, min_mask);
		}

#ifdef USE_LHL_TIMER
		lhl_tmr_sel = PMU_CC13_4387_LHL_TIMER_SELECT;
#endif
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13, PMU_CC13_4387_LHL_TIMER_SELECT, lhl_tmr_sel);

#if defined(SAVERESTORE)
		if (SR_ENAB()) {
			si_set_lv_sleep_mode_4387(sih, osh);
		}
#endif /* SAVERESTORE */

		if (R_REG(osh, &pmu->pmustatus) & PST_EXTLPOAVAIL) {
			lpo = LHL_EXT_LPO_ENAB;
		}

		if (!ISSIM_ENAB(sih)) {
			si_lhl_set_lpoclk(sih, osh, lpo);
		}

		if (getintvar(NULL, rstr_btldo3p3pu)) {
			si_pmu_regcontrol(sih, 4,
				PMU_28NM_VREG4_WL_LDO_CNTL_EN,
				PMU_28NM_VREG4_WL_LDO_CNTL_EN);
			si_pmu_regcontrol(sih, 6,
				PMU_4387_VREG6_BTLDO3P3_PU,
				PMU_4387_VREG6_BTLDO3P3_PU);
		}

		if (LHL_IS_PSMODE_1(sih)) {
			si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_07,
				((1 << GCI_CC7_AAON_BYPASS_PWRSW_SEL) |
				(1 << GCI_CC7_AAON_BYPASS_PWRSW_SEQ_ON)),
				0);
		}

		si_lhl_setup(sih, osh);

		/* Setting MemLPLDO voltage */
		if (getvar(NULL, rstr_memlpldo_volt) != NULL) {
			int memlpldo_volt = getintvar(NULL, rstr_memlpldo_volt);

			if (memlpldo_volt >= PMU_VREG5_LPLDO_VOLT_0_90 &&
				memlpldo_volt <= PMU_VREG5_LPLDO_VOLT_0_88) {
				si_pmu_regcontrol(sih, PMU_VREG_5, VREG5_4387_MEMLPLDO_ADJ_MASK,
					memlpldo_volt << VREG5_4387_MEMLPLDO_ADJ_SHIFT);
			} else {
				PMU_MSG(("Invalid memlpldo value: %d\n", memlpldo_volt));
			}
		}

		/* Setting LPLDO voltage */
		if (getvar(NULL, rstr_lpldo_volt) != NULL) {
			int lpldo_volt = getintvar(NULL, rstr_lpldo_volt);

			if (lpldo_volt >= PMU_VREG5_LPLDO_VOLT_0_90 &&
				lpldo_volt <= PMU_VREG5_LPLDO_VOLT_0_88) {
				si_pmu_regcontrol(sih, PMU_VREG_5, VREG5_4387_LPLDO_ADJ_MASK,
					lpldo_volt << VREG5_4387_LPLDO_ADJ_SHIFT);
			} else {
				PMU_MSG(("Invalid lpldo value: %d\n", lpldo_volt));
			}
		}

		/* Setting misc ldo voltage to 0.85625V but need stepping up */
		curr_misc_ldo_volt = (si_pmu_regcontrol(sih, PMU_VREG_5, 0, 0) &
			VREG5_4387_MISC_LDO_ADJ_MASK) >> VREG5_4387_MISC_LDO_ADJ_SHIFT;

		/* Only after POR, chip default is 0.8V */
		if (curr_misc_ldo_volt == PMU_VREG5_MISC_LDO_VOLT_0p800) {
			misc_ldo_volt = PMU_VREG5_MISC_LDO_VOLT_0p856;  /* 0.85625V */

			for (i = PMU_VREG5_MISC_LDO_VOLT_0p818; i <= misc_ldo_volt; i ++) {
				si_pmu_regcontrol(sih, PMU_VREG_5, VREG5_4387_MISC_LDO_ADJ_MASK,
					i << VREG5_4387_MISC_LDO_ADJ_SHIFT);
				OSL_DELAY(MISC_LDO_STEPPING_DELAY);
			}
		}

		/* Enable fast LPO */
		si_pmu_fast_lpo_enable(sih, osh);

		if (PMU_FLL_PU_ENAB()) {
			/* Wait until fast LPO is stable */
			OSL_DELAY(500u);
			si_pmu_fll_preload_enable(sih);
		}

		si_pmu_dynamic_clk_switch_enab(sih);

		/* HQ settings */
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_25,
			0xFFFFFFFF, XTAL_HQ_SETTING_4387);

		/* LQ settings */
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_26,
			0xFFFFFFFF, XTAL_LQ_SETTING_4387);

		/* Enable Radiodig Clk Gating */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13, PMU_CC13_4387_ENAB_RADIO_REG_CLK, 0);

		/* Set up HW based switch-off of select BBPLL channels when SCAN-only mode
		 *
		 * Assign bbpll_ch_control_grp_pd_trigger_mask = {gci_chip_cntrl[559:554],
		 * gci_chip_cntrl[543:522], 1'b0, gci_chip_cntrl[521], 1'b0};
		 */
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_16,
			CC_GCI_16_BBPLL_CH_CTRL_GRP_PD_TRIG_1_MASK |
			CC_GCI_16_BBPLL_CH_CTRL_GRP_PD_TRIG_24_3_MASK,
			(((GRP_PD_TRIGGER_MASK_4387 >> 1) & 0x1) <<	/* bit 1 */
			CC_GCI_16_BBPLL_CH_CTRL_GRP_PD_TRIG_1_SHIFT) |
			(((GRP_PD_TRIGGER_MASK_4387 >> 3) & 0x3FFFFF) << /* bit 24:3 */
			CC_GCI_16_BBPLL_CH_CTRL_GRP_PD_TRIG_24_3_SHIFT));

		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_17,
			CC_GCI_17_BBPLL_CH_CTRL_GRP_PD_TRIG_30_25_MASK |
			CC_GCI_17_BBPLL_CH_CTRL_EN_MASK,
			(((GRP_PD_TRIGGER_MASK_4387 >> 25) & 0x3F) <<	/* bits 30:25 */
			CC_GCI_17_BBPLL_CH_CTRL_GRP_PD_TRIG_30_25_SHIFT) |
			CC_GCI_17_BBPLL_CH_CTRL_EN_MASK);

		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_20,
			CC_GCI_20_BBPLL_CH_CTRL_GRP_MASK,
			(GRP_PD_MASK_4387 << CC_GCI_20_BBPLL_CH_CTRL_GRP_SHIFT));

		if (CHIPID(sih->chip) == BCM4397_CHIP_GRPID) {
			/* For Phy Reg Access configure IHRP access */
			si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_28,
				GCI_CC28_IHRP_SEL_MASK,
				0u << GCI_CC28_IHRP_SEL_SHIFT);
		}

		if (getvar(NULL, rstr_abuck_volt) != NULL) {
			abuck_volt = getintvar(NULL, rstr_abuck_volt);
		} else {
			abuck_volt = ABUCK_VOLT_SW_DEFAULT_4387;
		}

		if (CHIPID(sih->chip) == BCM4397_CHIP_GRPID) {
			/* For Phy Reg Access configure IHRP access */
			si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_28,
				GCI_CC28_IHRP_SEL_MASK,
				0u << GCI_CC28_IHRP_SEL_SHIFT);
		}

		/* For 4397, PMU has only 11 Regulator Registers */
		if (sih->chip != BCM4397_CHIP_ID) {
			si_pmu_vreg_control(sih, PMU_VREG_13,
				PMU_VREG13_ASR_OVADJ_PWM_MASK,
				abuck_volt << PMU_VREG13_ASR_OVADJ_PWM_SHIFT);
		}
		if (BCM_PWR_OPT_ENAB()) {
			if (getvar(NULL, rstr_cbuck_volt) != NULL) {
				cbuck_volt = getintvar(NULL, rstr_cbuck_volt);
			} else {
				cbuck_volt = CBUCK_VOLT_SW_DEFAULT_4387;
			}

			si_pmu_vreg_control(sih, PMU_VREG_0,
				VREG0_4378_CSR_VOLT_ADJ_PWM_MASK,
				cbuck_volt << VREG0_4378_CSR_VOLT_ADJ_PWM_SHIFT);
		}

		PMU_REG(sih, pmucontrol_ext, PCTL_EXT_FAST_TRANS_ENAB, PCTL_EXT_FAST_TRANS_ENAB);

		if (si_hib_ext_wakeup_isenab(sih)) {
			/* pull up common BP */
			int rsrc_num = RES4387_CORE_RDY_CB;
			uint32 deps = PMURES_BIT(rsrc_num) |
				si_pmu_res_deps(sih, osh, pmu, PMURES_BIT(rsrc_num), TRUE);
			W_REG(osh, &pmu->extwakereqmask[0], deps);
		}

		si_pmu_chipcontrol(sih, PMU_CHIPCTL17,
			PMU_CC17_SCAN_DIG_SR_CLK_MASK,
			SCAN_DIG_SR_CLK_40_MHZ << PMU_CC17_SCAN_DIG_SR_CLK_SHIFT);
	}
		break;

	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
	{
		pmuregs_t *pmu = si_setcore(sih, PMU_CORE_ID, 0);
		uint32 lpo = LHL_LPO_AUTO;
		uint32 lhl_tmr_sel = 0;
		uint32 abuck_volt, cbuck_volt;
		uint32 min_mask;

		if (PMU_FLL_PU_ENAB()) {
			min_mask = R_REG(osh, &pmu->min_res_mask) |
				PMURES_BIT(RES4389_FAST_LPO_AVAIL) |
				PMURES_BIT(RES4389_PMU_LP);
			W_REG(osh, &pmu->min_res_mask, min_mask);
		}

#ifdef USE_LHL_TIMER
		lhl_tmr_sel = PMU_CC13_4387_LHL_TIMER_SELECT;
#endif
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13, PMU_CC13_4387_LHL_TIMER_SELECT, lhl_tmr_sel);

#if defined(SAVERESTORE)
		if (SR_ENAB()) {
			si_set_lv_sleep_mode_4389(sih, osh);
		}
#endif /* SAVERESTORE */
		/* SET CB2WL Intr PWR Request irrespective of Default value */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL2, PMU_CC2_CB2WL_INTR_PWRREQ_EN,
				PMU_CC2_CB2WL_INTR_PWRREQ_EN);

		if (R_REG(osh, &pmu->pmustatus) & PST_EXTLPOAVAIL) {
			lpo = LHL_EXT_LPO_ENAB;
		}

		if (!ISSIM_ENAB(sih)) {
			si_lhl_set_lpoclk(sih, osh, lpo);
		}

		if (getintvar(NULL, rstr_btldo3p3pu)) {
			si_pmu_regcontrol(sih, 4,
				PMU_28NM_VREG4_WL_LDO_CNTL_EN,
				PMU_28NM_VREG4_WL_LDO_CNTL_EN);
			si_pmu_regcontrol(sih, 6,
				PMU_4387_VREG6_BTLDO3P3_PU,
				PMU_4387_VREG6_BTLDO3P3_PU);
		}

		if (LHL_IS_PSMODE_1(sih)) {
			si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_07,
				((1 << GCI_CC7_AAON_BYPASS_PWRSW_SEL) |
				(1 << GCI_CC7_AAON_BYPASS_PWRSW_SEQ_ON)),
				0);
		}

		si_lhl_setup(sih, osh);

		/* Setting MemLPLDO voltage */
		if (getvar(NULL, rstr_memlpldo_volt) != NULL) {
			int memlpldo_volt = getintvar(NULL, rstr_memlpldo_volt);

			if (memlpldo_volt >= PMU_VREG5_LPLDO_VOLT_0_90 &&
				memlpldo_volt <= PMU_VREG5_LPLDO_VOLT_0_88) {
				si_pmu_regcontrol(sih, PMU_VREG_5, VREG5_4387_MEMLPLDO_ADJ_MASK,
					memlpldo_volt << VREG5_4387_MEMLPLDO_ADJ_SHIFT);
			} else {
				PMU_MSG(("Invalid memlpldo value: %d\n", memlpldo_volt));
			}
		}

		/* Setting LPLDO voltage */
		if (getvar(NULL, rstr_lpldo_volt) != NULL) {
			int lpldo_volt = getintvar(NULL, rstr_lpldo_volt);

			if (lpldo_volt >= PMU_VREG5_LPLDO_VOLT_0_90 &&
				lpldo_volt <= PMU_VREG5_LPLDO_VOLT_0_88) {
				si_pmu_regcontrol(sih, PMU_VREG_5, VREG5_4387_LPLDO_ADJ_MASK,
					lpldo_volt << VREG5_4387_LPLDO_ADJ_SHIFT);
			} else {
				PMU_MSG(("Invalid lpldo value: %d\n", lpldo_volt));
			}
		}

		/* Enable fast LPO */
		si_pmu_fast_lpo_enable(sih, osh);

		if (PMU_FLL_PU_ENAB()) {
			/* Wait until fast LPO is stable */
			OSL_DELAY(500u);
			si_pmu_fll_preload_enable(sih);
		}

		si_pmu_dynamic_clk_switch_enab(sih);

		/* HQ settings */
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_25,
			0xFFFFFFFF, XTAL_HQ_SETTING_4387);

		/* LQ settings */
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_26,
			0xFFFFFFFF, XTAL_LQ_SETTING_4387);

		/* Enable Radiodig Clk Gating */
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13, PMU_CC13_4387_ENAB_RADIO_REG_CLK, 0);

		/* Set up HW based switch-off of select BBPLL channels when SCAN-only mode
		 *
		 * Assign bbpll_ch_control_grp_pd_trigger_mask = {gci_chip_cntrl[559:554],
		 * gci_chip_cntrl[543:522], 1'b0, gci_chip_cntrl[521], 1'b0};
		 */
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_16,
			CC_GCI_16_BBPLL_CH_CTRL_GRP_PD_TRIG_1_MASK |
			CC_GCI_16_BBPLL_CH_CTRL_GRP_PD_TRIG_24_3_MASK,
			(((GRP_PD_TRIGGER_MASK_4387 >> 1) & 0x1) <<	/* bit 1 */
			CC_GCI_16_BBPLL_CH_CTRL_GRP_PD_TRIG_1_SHIFT) |
			(((GRP_PD_TRIGGER_MASK_4387 >> 3) & 0x3FFFFF) << /* bit 24:3 */
			CC_GCI_16_BBPLL_CH_CTRL_GRP_PD_TRIG_24_3_SHIFT));

		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_17,
			CC_GCI_17_BBPLL_CH_CTRL_GRP_PD_TRIG_30_25_MASK |
			CC_GCI_17_BBPLL_CH_CTRL_EN_MASK,
			(((GRP_PD_TRIGGER_MASK_4387 >> 25) & 0x3F) <<	/* bits 30:25 */
			CC_GCI_17_BBPLL_CH_CTRL_GRP_PD_TRIG_30_25_SHIFT) |
			CC_GCI_17_BBPLL_CH_CTRL_EN_MASK);

		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_20,
			CC_GCI_20_BBPLL_CH_CTRL_GRP_MASK,
			(GRP_PD_MASK_4387 << CC_GCI_20_BBPLL_CH_CTRL_GRP_SHIFT));

		if (getvar(NULL, rstr_abuck_volt) != NULL) {
			abuck_volt = getintvar(NULL, rstr_abuck_volt);
		} else {
			abuck_volt = ABUCK_VOLT_SW_DEFAULT_4387;
		}

		if (CHIPID(sih->chip) == BCM4397_CHIP_GRPID) {
			/* For Phy Reg Access configure IHRP access */
			si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_28,
				GCI_CC28_IHRP_SEL_MASK,
				0u << GCI_CC28_IHRP_SEL_SHIFT);
		}

		/* For 4397, PMU has only 11 Regulator Registers */
		if (sih->chip != BCM4397_CHIP_ID) {
			si_pmu_vreg_control(sih, PMU_VREG_13,
				PMU_VREG13_ASR_OVADJ_PWM_MASK,
				abuck_volt << PMU_VREG13_ASR_OVADJ_PWM_SHIFT);
		}

		if (BCM_PWR_OPT_ENAB()) {
			if (getvar(NULL, rstr_cbuck_volt) != NULL) {
				cbuck_volt = getintvar(NULL, rstr_cbuck_volt);
			} else {
				cbuck_volt = CBUCK_VOLT_SW_DEFAULT_4387;
			}

			si_pmu_vreg_control(sih, PMU_VREG_0,
				VREG0_4378_CSR_VOLT_ADJ_PWM_MASK,
				cbuck_volt << VREG0_4378_CSR_VOLT_ADJ_PWM_SHIFT);
		}

		PMU_REG(sih, pmucontrol_ext, PCTL_EXT_FAST_TRANS_ENAB, PCTL_EXT_FAST_TRANS_ENAB);

		if (PMUREV(sih->pmurev) == 39) {
			si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_04,
				CC_GCI_04_4387C0_XTAL_PM_CLK,
				CC_GCI_04_4387C0_XTAL_PM_CLK);
		}
	}
		break;

	default:
		break;
	}
} /* si_pmu_chip_init */

/** Reference: http://confluence.broadcom.com/display/WLAN/Open+loop+Calibration+Sequence */
int
si_pmu_openloop_cal(si_t *sih, uint16 currtemp)
{
	int err = BCME_OK;
	switch (CHIPID(sih->chip)) {
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		err = si_pmu_openloop_cal_43012(sih, currtemp);
		break;

	default:
		PMU_MSG(("si_pmu_openloop_cal: chip not supported!\n"));
		break;
	}
	return err;
}

static int
si_pmu_openloop_cal_43012(si_t *sih, uint16 currtemp)
{
	int32 a1 = -27, a2 = -15, b1 = 18704, b2 = 7531, a3, y1, y2, b3, y3;
	int32 xtal, array_size = 0, dco_code = 0, origidx = 0, pll_reg = 0, err;
	bcm_int_bitmask_t intr_val;
	pmuregs_t *pmu = NULL;
	const pllctrl_data_t *pllctrlreg_update;
	const uint32 *pllctrlreg_val;
	osl_t *osh = si_osh(sih);
	uint32 final_dco_code = si_get_openloop_dco_code(sih);

	xtal = si_xtalfreq(sih);
	err = BCME_OK;

	origidx = si_coreidx(sih);
	pmu = si_setcore(sih, PMU_CORE_ID, 0);
	if (!pmu) {
		PMU_MSG(("si_pmu_openloop_cal_43012: NULL pmu pointer \n"));
		err = BCME_ERROR;
		goto done;
	}

	if (final_dco_code == 0) {
		currtemp = (currtemp == 0)?-1: currtemp;

		SPINWAIT(((si_corereg(sih, SI_CC_IDX,
			OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL) != CCS_HTAVAIL), PMU_MAX_TRANSITION_DLY);
		ASSERT((si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, clk_ctl_st), 0, 0)
			& CCS_HTAVAIL));

		/* Stop using PLL clks, by programming the disable_ht_avail */
		/* and disable_lq_avail in the pmu chip control bit */
		/* Turn Off PLL */
		si_pmu_pll_off_43012(sih, osh, pmu);

		/* Program PLL for 320MHz VCO */
		pllctrlreg_update = pmu1_xtaltab0_43012;
		array_size = ARRAYSIZE(pmu1_xtaltab0_43012);
		pllctrlreg_val = pmu1_pllctrl_tab_43012_1600mhz;
		si_pmu_pllctrlreg_update(sih, osh, pmu, xtal, 100,
			pllctrlreg_update, array_size, pllctrlreg_val);

		/* Update PLL control register */
		/* Set the Update bit (bit 10) in PMU for PLL registers */
		OR_REG(osh, &pmu->pmucontrol, PCTL_PLL_PLLCTL_UPD);

		/* Turn PLL ON but ensure that force_bbpll_dreset */
		/* bit is set , so that PLL 320Mhz clocks cannot be consumed */
		si_pmu_pll_on_43012(sih, osh, pmu, 1);

		/* Settings to get dco_code on PLL test outputs and then read */
		/* from gci chip status */
		pll_reg = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG1, 0, 0);
		pll_reg = (pll_reg & (~0x3C000)) | (0x4<<14);
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG1, ~0, pll_reg);
		OR_REG(osh, &pmu->pmucontrol, PCTL_PLL_PLLCTL_UPD);

		pll_reg = pll_reg | (1<<17);
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG1, ~0, pll_reg);
		OR_REG(osh, &pmu->pmucontrol, PCTL_PLL_PLLCTL_UPD);

		/* Get the DCO code from GCI CHIP STATUS Register 7 , bits 27 downto 16 */
		dco_code = (si_gci_chipstatus(sih, GCI_CHIPSTATUS_07));
		dco_code = ((dco_code & 0x0FFF0000) >> 16);
		dco_code = (dco_code  >> 4);

		/* The DCO code obtained above and the temperature */
		/* sensed at this time will give us the DCO code */
		/* that needs to be programmed to ensure VCO does not crosses 160 MHz at 125C */
		y1 = ((a1 * currtemp) + b1);
		y2 = ((a2 * currtemp) + b2);
		dco_code = (dco_code * 100);
		b3 = b1 + (((b2-b1)/(y2 - y1)) * (dco_code - y1));
		if (b3 > dco_code) {
			a3 = (b3 - dco_code) / currtemp;
			y3 = b3 - (a3 * 125);
		}
		else {
			 a3 = (dco_code - b3) / currtemp;
			 y3 = b3 + (a3 * 125);
		}
		y3 = (y3/100);
		PMU_MSG(("DCO_CODE = %d\n", y3));

		/* Turning ON PLL at 160.1 MHz for Normal Operation */
		si_pmu_pll_off_43012(sih, osh, pmu);
		pllctrlreg_update = pmu1_xtaltab0_43012;
		array_size = ARRAYSIZE(pmu1_xtaltab0_43012);
		pllctrlreg_val = pmu1_pllctrl_tab_43012_1600mhz;
		si_pmu_pllctrlreg_update(sih, osh, pmu, xtal, 0,
			pllctrlreg_update, array_size, pllctrlreg_val);

		/* Update PLL control register */
		/* Set the Update bit (bit 10) in PMU for PLL registers */
		OR_REG(osh, &pmu->pmucontrol, PCTL_PLL_PLLCTL_UPD);

		si_pmu_pll_on_43012(sih, osh, pmu, 0);
		y3 = (y3 << 4);
		final_dco_code = y3;
		PMU_MSG(("openloop_dco_code = %x\n", final_dco_code));
	}

	pll_reg = si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG3, 0, 0);
	y3 = (pll_reg >> 16) & 0xFFF;

	if (final_dco_code != (uint32)y3) {

		/* Program the DCO code to bits 27 */
		/* downto 16 of the PLL control 3 register */
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG3,
			0x0FFF0000, (final_dco_code << 16));

		/* Enable Extra post divison for Open Loop */
		/* by writing 1 to bit 14 of above register */
		si_pmu_pllcontrol(sih, PMU_PLL_CTRL_REG3, 0x00004000, (1<<14));

		/* Update PLL control register */
		/* Set the Update bit (bit 10) in PMU for PLL registers */
		OR_REG(osh, &pmu->pmucontrol, PCTL_PLL_PLLCTL_UPD);
		/* After cal openloop VCO Max=320MHz, Min=240Mhz (with extra margin */
		/* 230-220MHz). Update SAVE_RESTORE up/down times accordingly */
		W_REG(osh, &pmu->res_table_sel,	RES43012_SR_SAVE_RESTORE);
		W_REG(osh, &pmu->res_updn_timer, 0x01800180);
	}

	si_restore_core(sih, origidx, &intr_val);
	si_set_openloop_dco_code(sih, final_dco_code);
done:
	return err;
}

void
si_pmu_slow_clk_reinit(si_t *sih, osl_t *osh)
{
#if !defined(BCMDONGLEHOST)
	chipcregs_t *cc;
	uint origidx;
	uint32 xtalfreq;

	/* PMU specific initializations */
	if (!PMUCTL_ENAB(sih))
		return;
	/* Remember original core before switch to chipc */
	origidx = si_coreidx(sih);
	cc = si_setcoreidx(sih, SI_CC_IDX);
	ASSERT(cc != NULL);
	if (cc == NULL)
		return;

	xtalfreq = getintvar(NULL, rstr_xtalfreq);
	/*
	 * workaround for chips that don't support external LPO, thus ALP clock
	 * can not be measured accurately:
	 */
	switch (CHIPID(sih->chip)) {
		CASE_BCM43602_CHIP:
			xtalfreq = XTAL_FREQ_54MHZ;
			break;
		default:
			break;
	}
	/* If xtalfreq var not available, try to measure it */
	if (xtalfreq == 0)
		xtalfreq = si_pmu_measure_alpclk(sih, osh);
	si_pmu_enb_slow_clk(sih, osh, xtalfreq);
	/* Return to original core */
	si_setcoreidx(sih, origidx);
#endif /* !BCMDONGLEHOST */
}

/** initialize PMU registers in case default values proved to be suboptimal */
void
BCMATTACHFN(si_pmu_swreg_init)(si_t *sih, osl_t *osh)
{
	ASSERT(sih->cccaps & CC_CAP_PMU);

	switch (CHIPID(sih->chip)) {
	CASE_BCM43602_CHIP:
		/* adjust PA Vref to 2.80V */
		si_pmu_set_ldo_voltage(sih, osh, SET_LDO_VOLTAGE_PAREF, 0x0c);
		break;
	case BCM4378_CHIP_GRPID:
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_04, GPIO_DRIVE_4378_MASK,
			GPIO_DRIVE_4378_VAL);
		/* fall through */
	case BCM4376_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
#ifdef BCM_AVS
		if (BCM_AVS_ENAB()) {
			si_pmu_set_avs(sih);
		}
#endif
		break;
	default:
		break;
	}
	si_pmu_otp_vreg_control(sih, osh);
} /* si_pmu_swreg_init */

/** Wait for a particular clock level to be on the backplane */
uint32
si_pmu_waitforclk_on_backplane(si_t *sih, osl_t *osh, uint32 clk, uint32 delay_val)
{
	pmuregs_t *pmu;
	uint origidx;
	uint32 val;

	ASSERT(sih->cccaps & CC_CAP_PMU);
	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	if (delay_val)
		SPINWAIT(((R_REG(osh, &pmu->pmustatus) & clk) != clk), delay_val);
	val = R_REG(osh, &pmu->pmustatus) & clk;

	/* Return to original core */
	si_setcoreidx(sih, origidx);
	return (val);
}

#define EXT_ILP_HZ 32768

/**
 * Measures the ALP clock frequency in KHz.  Returns 0 if not possible.
 * Possible only if PMU rev >= 10 and there is an external LPO 32768Hz crystal.
 */
uint32
BCMATTACHFN(si_pmu_measure_alpclk)(si_t *sih, osl_t *osh)
{
	uint32 alp_khz;
	uint32 pmustat_lpo = 0;
	pmuregs_t *pmu;
	uint origidx;

	if (PMUREV(sih->pmurev) < 10)
		return 0;

	ASSERT(sih->cccaps & CC_CAP_PMU);

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	if ((CHIPID(sih->chip) == BCM43012_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43013_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43014_CHIP_ID) ||
		(PMUREV(sih->pmurev) >= 22))
		pmustat_lpo = !(R_REG(osh, &pmu->pmucontrol) & PCTL_LPO_SEL);
	else
		pmustat_lpo = R_REG(osh, &pmu->pmustatus) & PST_EXTLPOAVAIL;

	if (pmustat_lpo) {
		uint32 ilp_ctr, alp_hz;

		/* Enable the reg to measure the freq, in case disabled before */
		W_REG(osh, &pmu->pmu_xtalfreq, 1U << PMU_XTALFREQ_REG_MEASURE_SHIFT);

		/* Delay for well over 4 ILP clocks */
		OSL_DELAY(1000);

		/* Read the latched number of ALP ticks per 4 ILP ticks */
		ilp_ctr = R_REG(osh, &pmu->pmu_xtalfreq) & PMU_XTALFREQ_REG_ILPCTR_MASK;

		/* Turn off the PMU_XTALFREQ_REG_MEASURE_SHIFT bit to save power */
		W_REG(osh, &pmu->pmu_xtalfreq, 0);

		/* Calculate ALP frequency */
		alp_hz = (ilp_ctr * EXT_ILP_HZ) / 4;

		/* Round to nearest 100KHz, and at the same time convert to KHz */
		alp_khz = (alp_hz + 50000) / 100000 * 100;
	} else
		alp_khz = 0;

	/* Return to original core */
	si_setcoreidx(sih, origidx);

	return alp_khz;
} /* si_pmu_measure_alpclk */

/** Update min/max resources after SR-ASM download to d11 txfifo */
void
si_pmu_res_minmax_update(si_t *sih, osl_t *osh)
{
	uint32 min_mask = 0, max_mask = 0;
	pmuregs_t *pmu;
	uint origidx;
	bcm_int_bitmask_t intr_val;

	/* Block ints and save current core */
	si_introff(sih, &intr_val);
	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	switch (CHIPID(sih->chip)) {
	CASE_BCM43602_CHIP:
		max_mask = 0; /* Only care about min_mask for now */
		break;
	case BCM43012_CHIP_ID:
	case BCM43013_CHIP_ID:
	case BCM43014_CHIP_ID:
		min_mask = RES43012_PMU_SLEEP;
		break;
	case BCM4369_CHIP_GRPID:
	case BCM4362_CHIP_GRPID:
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		si_pmu_res_masks(sih, &min_mask, &max_mask);
		max_mask = 0; /* Don't need to update max */
		break;
	default:
		break;
	}
	if (min_mask) {
		/* Add min mask dependencies */
		min_mask |= si_pmu_res_deps(sih, osh, pmu, min_mask, FALSE);
		W_REG(osh, &pmu->min_res_mask, min_mask);
	}
	if (max_mask) {
		max_mask |= si_pmu_res_deps(sih, osh, pmu, max_mask, FALSE);
		W_REG(osh, &pmu->max_res_mask, max_mask);
	}

	si_pmu_wait_for_steady_state(sih, osh, pmu);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
	si_intrrestore(sih, &intr_val);
} /* si_pmu_res_minmax_update */

#ifdef DONGLEBUILD

#define PMUCAP_DUMP_TAG_SIZE_BYTES	4

/* Move the below definitions to .ro_ontrap section so they
 * won't be reused when reusing rodata section after trap.
 */
static const uint32 BCMPOST_TRAP_RODATA(chipc_regs_to_dump)[] = {
	OFFSETOF(chipcregs_t, clk_ctl_st),
	OFFSETOF(chipcregs_t, powerctl)
};

static const uint BCMPOST_TRAP_RODATA(pmuregsdump)[] = {
		OFFSETOF(pmuregs_t, pmucontrol),
		OFFSETOF(pmuregs_t, pmucapabilities),
		OFFSETOF(pmuregs_t, pmustatus),
		OFFSETOF(pmuregs_t, res_state),
		OFFSETOF(pmuregs_t, res_pending),
		OFFSETOF(pmuregs_t, pmutimer),
		OFFSETOF(pmuregs_t, min_res_mask),
		OFFSETOF(pmuregs_t, max_res_mask),
		OFFSETOF(pmuregs_t, clkstretch),
		OFFSETOF(pmuregs_t, res_req_timer),
		OFFSETOF(pmuregs_t, res_req_mask),
		OFFSETOF(pmuregs_t, mac_res_req_timer),
		OFFSETOF(pmuregs_t, mac_res_req_mask),
		OFFSETOF(pmuregs_t, pmuintmask0),
		OFFSETOF(pmuregs_t, pmuintstatus),
		OFFSETOF(pmuregs_t, pmuintctrl0),
		OFFSETOF(pmuregs_t, extwakeupstatus),
		OFFSETOF(pmuregs_t, extwakemask0)
};

static const uint BCMPOST_TRAP_RODATA(pmuregsdump_mac_res1)[] = {
		OFFSETOF(pmuregs_t, mac_res_req_timer1),
		OFFSETOF(pmuregs_t, mac_res_req_mask1)
};

static const uint BCMPOST_TRAP_RODATA(pmuregsdump_mac_res2)[] = {
		OFFSETOF(pmuregs_t, mac_res_req_timer2),
		OFFSETOF(pmuregs_t, mac_res_req_mask2)
};

static const uint BCMPOST_TRAP_RODATA(pmuregsdump_pmu_int1)[] = {
		OFFSETOF(pmuregs_t, pmuintmask1),
		OFFSETOF(pmuregs_t, pmuintctrl1)
};

/* Pointer to location in ROdata where PMU registers are stored.
 * It is good to avoid re-reading PMU registers as: 1. reading regs is slow
 * 2. As part of trap, these registers are dumped to RO data section anyway.
 * so why not read directly from ROdata section and send to host?
 * these registers will be dumped n RODATA first and then hnd_minidump_pmuegs_dump()
 * will pick these up. For it to pick these up, it
 * needs to know where they are stored.
 */
/* Length of the reg dump containing address, value pair */
#define SI_PMU_REG_DUMP_BASE_SIZE      (ARRAYSIZE(pmuregsdump) * 2u * sizeof(uint32))
#define SI_PMU_REG_DUMP_MACRSRC1_SIZE  (ARRAYSIZE(pmuregsdump_mac_res1) * 2u * sizeof(uint32))
#define SI_PMU_REG_DUMP_MACRSRC2_SIZE  (ARRAYSIZE(pmuregsdump_mac_res2) * 2u * sizeof(uint32))
#define SI_PMU_REG_DUMP_INTRCV1_SIZE   (ARRAYSIZE(pmuregsdump_pmu_int1) * 2u * sizeof(uint32))

static uint32 *rodata_pmuregdump_ptr = NULL;

/** size of the buffer needed to store the PMU register dump specifically PMU indirect registers */
uint32
BCMATTACHFN(si_pmu_dump_buf_size_pmucap)(si_t *sih)
{
	uint32 buf_size = 0;
	uint32 pmu_size = 0;
	uint32 cnt;

	if (PMUREV(sih->pmurev) < 5)
		return 0;

	/* pmu resources resource mask and resource updown */
	cnt = (sih->pmucaps & PCAP_RC_MASK) >> PCAP_RC_SHIFT;
	if (cnt) {
		buf_size += (cnt * 2 * sizeof(uint32)) + PMUCAP_DUMP_TAG_SIZE_BYTES;
	}
	/* pll controls */
	cnt = (sih->pmucaps & PCAP5_PC_MASK) >> PCAP5_PC_SHIFT;
	if (cnt) {
		buf_size += (cnt * sizeof(uint32)) + PMUCAP_DUMP_TAG_SIZE_BYTES;
	}

	/* voltage controls */
	cnt = (sih->pmucaps & PCAP5_VC_MASK) >> PCAP5_VC_SHIFT;
	if (cnt) {
		buf_size += (cnt * sizeof(uint32)) + PMUCAP_DUMP_TAG_SIZE_BYTES;
	}

	/* chip controls */
	cnt = (sih->pmucaps & PCAP5_CC_MASK) >> PCAP5_CC_SHIFT;
	if (cnt) {
		buf_size += (cnt * sizeof(uint32)) + PMUCAP_DUMP_TAG_SIZE_BYTES;
	}

	/* include chip common regsiters from the list */
	/* cnt indicates how many registers, tag_id 0 will say these are address/value */
	if (ARRAYSIZE(chipc_regs_to_dump)) {
		buf_size += PMUCAP_DUMP_TAG_SIZE_BYTES;
		/* address/value pairs */
		buf_size += sizeof(chipc_regs_to_dump);
		buf_size += sizeof(chipc_regs_to_dump);
	}

	/* include PMU regsiters from the list 'pmuregsdumpXX' */
	if ((PMUREV(sih->pmurev) > 27) && ARRAYSIZE(pmuregsdump) != 0) {
		uint8 rsrc_cnt = si_pmu_get_mac_rsrc_req_tmr_cnt(sih);
		buf_size += PMUCAP_DUMP_TAG_SIZE_BYTES;
		pmu_size += sizeof(pmuregsdump);
		if (ARRAYSIZE(pmuregsdump_mac_res1) != 0 && rsrc_cnt > 1) {
			buf_size += PMUCAP_DUMP_TAG_SIZE_BYTES;
			pmu_size += sizeof(pmuregsdump_mac_res1);
		}
		if (ARRAYSIZE(pmuregsdump_mac_res2) != 0 && rsrc_cnt > 2) {
			buf_size += PMUCAP_DUMP_TAG_SIZE_BYTES;
			pmu_size += sizeof(pmuregsdump_mac_res2);
		}
		if (ARRAYSIZE(pmuregsdump_pmu_int1) != 0 &&
			si_pmu_get_pmu_interrupt_rcv_cnt(sih) > 1) {
			buf_size += PMUCAP_DUMP_TAG_SIZE_BYTES;
			pmu_size += sizeof(pmuregsdump_pmu_int1);
		}
		/* address/value pairs */
		buf_size += (pmu_size << 1);
	}

	return buf_size;
}

/**
 * routine to dump the registers into the user specified buffer
 * needed to store the PMU register dump specifically PMU indirect registers
 * format is sets of count, base regiser, register values
*/
uint32
BCMPOSTTRAPFN(si_pmu_dump_pmucap_binary)(si_t *sih, uchar *p)
{
	uint32 cnt, i;
	osl_t *osh;
	pmuregs_t *pmu;
	uint origidx;
	uint mac_res_cnt;
	uint pmu_int_rcv_cnt;
	uint32 pmu_totalsize = 0;

	uint32 *p32 = (uint32 *)p;

	if (PMUREV(sih->pmurev) < 5)
		return 0;

	origidx = si_coreidx(sih);

	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	}
	else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	osh = si_osh(sih);

	cnt = (sih->pmucaps & PCAP_RC_MASK) >> PCAP_RC_SHIFT;
	if (cnt) {
		*p32++ = (cnt << 16 | RSRCTABLEADDR);
		for (i = 0; i < cnt; i++) {
			W_REG(osh, &pmu->res_table_sel, i);
			*p32++ = R_REG(osh, &pmu->res_dep_mask);
			*p32++ = R_REG(osh, &pmu->res_updn_timer);
		}
	}

	cnt = (sih->pmucaps & PCAP5_PC_MASK) >> PCAP5_PC_SHIFT;
	if (cnt) {
		*p32++ = (cnt << 16 | PMU_PLL_CONTROL_ADDR);
		for (i = 0; i < cnt; i++) {
			*p32++ = si_pmu_pllcontrol(sih, i, 0, 0);
		}
	}

	cnt = (sih->pmucaps & PCAP5_VC_MASK) >> PCAP5_VC_SHIFT;
	if (cnt) {
		*p32++ = (cnt << 16 | PMU_REG_CONTROL_ADDR);
		for (i = 0; i < cnt; i++) {
			*p32++ = si_pmu_vreg_control(sih, i, 0, 0);
		}
	}
	cnt = (sih->pmucaps & PCAP5_CC_MASK) >> PCAP5_CC_SHIFT;
	if (cnt) {
		*p32++ = (cnt << 16 | CC_CHIPCTL_ADDR);
		for (i = 0; i < cnt; i++) {
			*p32++ = si_pmu_chipcontrol(sih, i, 0, 0);
		}
	}
	if (ARRAYSIZE(chipc_regs_to_dump)) {
		uint32 *addr;
		*p32++ = (ARRAYSIZE(chipc_regs_to_dump) << 16 | 0);
		for (i = 0; i < ARRAYSIZE(chipc_regs_to_dump); i++) {
			addr = (uint32 *)(SI_ENUM_BASE(sih) + chipc_regs_to_dump[i]);
			*p32++ = (uint32)addr;
			*p32++ = R_REG(osh, addr);
		}
	}

	if ((PMUREV(sih->pmurev) > 27)) {
		volatile uint32 *addr;
		*p32++ = (ARRAYSIZE(pmuregsdump) << 16 | 1);
		for (i = 0; i < ARRAYSIZE(pmuregsdump); i++) {
			addr = (volatile uint32*)((volatile char*)pmu + pmuregsdump[i]);
			*p32++ = (uint32)addr;
			*p32++ = R_REG(osh, addr);
		}
		pmu_totalsize += (ARRAYSIZE(pmuregsdump));
		mac_res_cnt = si_pmu_get_mac_rsrc_req_tmr_cnt(sih);
		if (mac_res_cnt > 1) {
			*p32++ = (ARRAYSIZE(pmuregsdump_mac_res1) << 16 | 1);
			for (i = 0; i < ARRAYSIZE(pmuregsdump_mac_res1); i++) {
				addr = (volatile uint32*)((volatile char*)pmu +
						pmuregsdump_mac_res1[i]);
				*p32++ = (uint32)addr;
				*p32++ = R_REG(osh, addr);
			}
			pmu_totalsize += (ARRAYSIZE(pmuregsdump_mac_res1));
		}
		if (mac_res_cnt > 2) {
			*p32++ = (ARRAYSIZE(pmuregsdump_mac_res2) << 16 | 1);
			for (i = 0; i < ARRAYSIZE(pmuregsdump_mac_res2); i++) {
				addr = (volatile uint32*)((volatile char*)pmu +
						pmuregsdump_mac_res2[i]);
				*p32++ = (uint32)addr;
				*p32++ = R_REG(osh, addr);
			}
			pmu_totalsize += (ARRAYSIZE(pmuregsdump_mac_res2));
		}
		pmu_int_rcv_cnt = si_pmu_get_pmu_interrupt_rcv_cnt(sih);
		if (pmu_int_rcv_cnt > 1) {
			*p32++ = (ARRAYSIZE(pmuregsdump_pmu_int1) << 16 | 1);
			for (i = 0; i < ARRAYSIZE(pmuregsdump_pmu_int1); i++) {
				addr = (volatile uint32*)((volatile char*)pmu +
						pmuregsdump_pmu_int1[i]);
				*p32++ = (uint32)addr;
				*p32++ = R_REG(osh, addr);
			}
			pmu_totalsize += (ARRAYSIZE(pmuregsdump_pmu_int1));
		}
		/* Mark the location where these registers are dumped  to avoid a re-read in
		 * trap context.
		 */
		rodata_pmuregdump_ptr = (p32 - (2 * pmu_totalsize));
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
	return 1;
} /* si_pmu_dump_pmucap_binary */

#endif /* DONGLEBUILD */
/**
 * Function to enable the min_mask with specified resources along with its dependencies.
 * Also it can be used for bringing back to the default value of the device.
 */
int
si_pmu_min_res_set(si_t *sih, osl_t *osh, uint min_mask, bool set)
{
	uint32 min_res, max_res;
	uint origidx;
	bcm_int_bitmask_t intr_val;
	pmuregs_t *pmu;

	/* Block ints and save current core */
	si_introff(sih, &intr_val);

	/* Remember original core before switch to chipc */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);

	si_pmu_res_masks(sih, &min_res, &max_res);
	min_mask |= si_pmu_res_deps(sih, osh, pmu, min_mask, TRUE);

	/*
	 * If set is enabled, the resources specified in the min_mask is brought up. If not set,
	 * go to the default min_resource of the device.
	 */
	if (set) {
		OR_REG(osh, &pmu->min_res_mask, min_mask);
	} else {
		min_mask &= ~min_res;
		AND_REG(osh, &pmu->min_res_mask, ~min_mask);
	}

	si_pmu_wait_for_steady_state(sih, osh, pmu);

	/* Return to original core */
	si_setcoreidx(sih, origidx);
	si_intrrestore(sih, &intr_val);

	return min_mask;
}

void
si_pmu_bt_ldo_pu(si_t *sih, bool up)
{
	si_pmu_regcontrol(sih, PMU_VREG_6, PMU_28NM_VREG6_BTLDO3P3_PU,
		(up == TRUE) ? PMU_28NM_VREG6_BTLDO3P3_PU : 0x00);
}

#ifdef BCM_LDO3P3_SOFTSTART
int si_pmu_ldo3p3_soft_start_wl_get(si_t *sih, osl_t *osh, int *res)
{
	uint32 bt_or_wl = 0u;
	return si_pmu_ldo3p3_soft_start_get(sih, osh, bt_or_wl, res);
}

int si_pmu_ldo3p3_soft_start_bt_get(si_t *sih, osl_t *osh, int *res)
{
	uint32 bt_or_wl = 1u;
	return si_pmu_ldo3p3_soft_start_get(sih, osh, bt_or_wl, res);
}

static int
si_pmu_soft_start_params(si_t *sih, uint32 bt_or_wl, uint *en_reg, uint32 *en_shift,
	uint32 *en_mask, uint32 *en_val, uint *val_reg, uint32 *val_shift, uint32 *val_mask)
{
	switch (CHIPID(sih->chip)) {
		case BCM4369_CHIP_GRPID:
			*en_reg = SOFT_START_EN_REG_4369;
			*en_shift = SOFT_START_EN_SHIFT_4369(bt_or_wl);
			*en_mask = SOFT_START_EN_MASK_4369;
			*en_val = SOFT_START_EN_VALUE_4369;
			*val_reg = SLEW_RATE_VALUE_REG_4369;
			*val_shift = SLEW_RATE_SHIFT_4369(bt_or_wl);
			*val_mask = SLEW_RATE_MASK_4369;
			break;
		case BCM4376_CHIP_GRPID:
		case BCM4378_CHIP_GRPID:
			*en_reg = SOFT_START_EN_REG_4378;
			*en_shift = SOFT_START_EN_SHIFT_4378(bt_or_wl);
			*en_mask = SOFT_START_EN_MASK_4378;
			*en_val = SOFT_START_EN_VALUE_4378;
			*val_reg = SLEW_RATE_VALUE_REG_4378;
			*val_shift = SLEW_RATE_SHIFT_4378(bt_or_wl);
			*val_mask = SLEW_RATE_MASK_4378;
			if (BCM4378_CHIP(sih->chip) && PMUREV(sih->pmurev) == 37) {
				*en_val = SOFT_START_EN_VALUE_4378_REV37;
			}
			break;
		case BCM4387_CHIP_GRPID:
			if (bt_or_wl == 0) {
				return BCME_UNSUPPORTED;
			}
			*en_reg = SOFT_START_EN_REG_4387;
			*en_shift = SOFT_START_EN_SHIFT_4387(bt_or_wl);
			*en_mask = SOFT_START_EN_MASK_4387;
			*en_val = SOFT_START_EN_VALUE_4387;
			*val_reg = SLEW_RATE_VALUE_REG_4387;
			*val_shift = SLEW_RATE_SHIFT_4387(bt_or_wl);
			*val_mask = SLEW_RATE_MASK_4387;
			break;
		default:
			/* Add support */
			ASSERT(0);
			break;
	}
	return BCME_OK;
}

static int si_pmu_ldo3p3_soft_start_get(si_t *sih, osl_t *osh, uint32 bt_or_wl, int *res)
{
	uint en_reg = 0, val_reg = 0;
	uint32 en_shift = 0, en_mask = 0, en_val = 0, val_shift = 0, val_mask = 0;
	uint32 soft_start_en, slew_rate;
	int ret = si_pmu_soft_start_params(sih, bt_or_wl, &en_reg, &en_shift, &en_mask, &en_val,
		&val_reg, &val_shift, &val_mask);

	if (BCME_OK != ret) {
		return ret;
	}
	soft_start_en = (si_pmu_vreg_control(sih, en_reg, 0, 0) >> en_shift);
	soft_start_en &= en_mask;
	if (en_val == 0u) {
		soft_start_en = !soft_start_en;
	}
	if (soft_start_en) {
		slew_rate = (si_pmu_vreg_control(sih, val_reg, 0, 0) >> val_shift);
		slew_rate &= val_mask;
		*res = slew_rate;
	} else {
		*res =  -1;
	}
	return BCME_OK;
}

int si_pmu_ldo3p3_soft_start_wl_set(si_t *sih, osl_t *osh, uint32 slew_rate)
{
	uint32 bt_or_wl = 0u;
	return si_pmu_ldo3p3_soft_start_set(sih, osh, bt_or_wl, slew_rate);
}

int si_pmu_ldo3p3_soft_start_bt_set(si_t *sih, osl_t *osh, uint32 slew_rate)
{
	uint32 bt_or_wl = 1u;
	return si_pmu_ldo3p3_soft_start_set(sih, osh, bt_or_wl, slew_rate);
}

static int si_pmu_ldo3p3_soft_start_set(si_t *sih, osl_t *osh, uint32 bt_or_wl, uint32 slew_rate)
{
	uint en_reg = 0, val_reg = 0;
	uint32 en_shift = 0, en_mask = 0, en_val = 0, val_shift = 0, val_mask = 0;
	int ret = si_pmu_soft_start_params(sih, bt_or_wl, &en_reg, &en_shift, &en_mask, &en_val,
		&val_reg, &val_shift, &val_mask);
	uint32 dis_val = en_val ? 0u : 1u;

	if (BCME_OK != ret) {
		return ret;
	}

	if (slew_rate != (uint32)(~0u)) {

		/* Without disabling soft start bit
		 * programming a new slew rate value
		 * doesn't take effect
		 */

		/* Disable soft start */
		si_pmu_vreg_control(sih, en_reg, (en_mask << en_shift), (dis_val << en_shift));

		/* Program Slew rate */
		si_pmu_vreg_control(sih, val_reg, (val_mask << val_shift),
			((slew_rate & val_mask) << val_shift));

		/* Enable Soft start */
		si_pmu_vreg_control(sih, en_reg, (en_mask << en_shift), (en_val << en_shift));
	} else {
		/* Slew rate value of 0xFFFF is used as a special value
		 * to disable/reset soft start feature
		 */

		/* Disable soft start */
		si_pmu_vreg_control(sih, en_reg, (en_mask << en_shift), (dis_val << en_shift));

		/* Set slew rate value to zero */
		si_pmu_vreg_control(sih, val_reg, (val_mask << val_shift), 0u);
	}
	return BCME_OK;
}
#endif /* BCM_LDO3P3_SOFTSTART */

#ifdef LDO3P3_MIN_RES_MASK
static bool ldo3p3_min_res_enabled = FALSE;
/** Set ldo 3.3V mask in the min resources mask register */
int
si_pmu_min_res_ldo3p3_set(si_t *sih, osl_t *osh, bool on)
{
	uint32 min_mask = 0;
	uint coreidx = si_findcoreidx(sih, GCI_CORE_ID, 0);

	switch (CHIPID(sih->chip)) {
		case BCM4369_CHIP_GRPID:
		case BCM4362_CHIP_GRPID:
			min_mask = PMURES_BIT(RES4369_LDO3P3_PU);
			if (on) {
				si_corereg(sih, coreidx, LHL_REG_OFF(lhl_lp_main_ctl1_adr),
						BCM_MASK32(23, 0), 0x9E9F9F);
			} else {
				si_corereg(sih, coreidx, LHL_REG_OFF(lhl_lp_main_ctl1_adr),
						BCM_MASK32(23, 0), 0x9E9F97);
			}
			break;
		case BCM4376_CHIP_GRPID:
		case BCM4378_CHIP_GRPID:
			min_mask = PMURES_BIT(RES4378_LDO3P3_PU);
			break;
		default:
			return BCME_UNSUPPORTED;
	}

	si_pmu_min_res_set(sih, osh, min_mask, on);
	ldo3p3_min_res_enabled = on;

	return BCME_OK;
}

int
si_pmu_min_res_ldo3p3_get(si_t *sih, osl_t *osh, int *res)
{
	*res = (int)ldo3p3_min_res_enabled;
	return BCME_OK;
}
#endif /* LDO3P3_MIN_RES_MASK */
int
si_pmu_min_res_otp_pu_set(si_t *sih, osl_t *osh, bool on)
{
	uint32 min_mask = 0;
	rsc_per_chip_t *rsc;

	rsc = si_pmu_get_rsc_positions(sih);
	if (rsc) {
		min_mask = PMURES_BIT(rsc->otp_pu);
	} else {
		return BCME_UNSUPPORTED;
	}
	si_pmu_min_res_set(sih, osh, min_mask, on);
	return BCME_OK;
}
#endif /* !defined(BCMDONGLEHOST) */

uint32
BCMPOSTTRAPFN(si_pmu_wake_bit_offset)(si_t *sih)
{
	uint32 wakebit;

	switch (CHIPID(sih->chip)) {
	case BCM4369_CHIP_GRPID:
		wakebit = PMU_CC2_GCI2_WAKE;
		break;
	case BCM4376_CHIP_GRPID:
	case BCM4378_CHIP_GRPID:
		wakebit = CC2_4378_GCI2WAKE_MASK;
		break;
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
		wakebit = CC2_4387_GCI2WAKE_MASK;
		break;
	case BCM4388_CHIP_GRPID:
	case BCM4389_CHIP_GRPID:
	case BCM4397_CHIP_GRPID:
		wakebit = CC2_4389_GCI2WAKE_MASK;
		break;
	default:
		wakebit = 0;
		ASSERT(0);
		break;
	}

	return wakebit;
}

#ifdef ATE_BUILD
void hnd_pmu_clr_int_sts_req_active(osl_t *hnd_osh, si_t *sih)
{
	uint32 res_req_timer;
	pmuregs_t *pmu;
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}
	ASSERT(pmu != NULL);
	W_REG(hnd_osh, &pmu->pmuintstatus,
		RSRC_INTR_MASK_TIMER_INT_0);
	(void)R_REG(hnd_osh, &pmu->pmuintstatus);
	res_req_timer = R_REG(hnd_osh, &pmu->res_req_timer);
	W_REG(hnd_osh, &pmu->res_req_timer,
			res_req_timer & ~(PRRT_REQ_ACTIVE << flags_shift));
	(void)R_REG(hnd_osh, &pmu->res_req_timer);
}
#endif /* ATE_BUILD */

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
		PMU_ERROR(("si_pmu_fast_lpo_disable: No Fast LPO capability\n"));
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

/*
* 4389B0/C0 - WL and BT turn on WAR,
* set below bits in PMU chip control 6
* - global bit[195] / bit[3] - enable legacy pmu_wakeup to make
* domain 1 (WL) power request
* - global bit[206] / bit[14] - perst_wake_en
*/
void
si_pmu_dmn1_perst_wakeup(si_t *sih, bool set)
{
	if (PMUREV(sih->pmurev) == 40) {
		if (set) {
			si_pmu_chipcontrol(sih, PMU_CHIPCTL6,
				(PMU_CC6_ENABLE_DMN1_WAKEUP |
				PMU_CC6_ENABLE_PMU_WAKEUP_PERST),
				(PMU_CC6_ENABLE_DMN1_WAKEUP |
				PMU_CC6_ENABLE_PMU_WAKEUP_PERST));
		} else {
			si_pmu_chipcontrol(sih, PMU_CHIPCTL6,
				(PMU_CC6_ENABLE_DMN1_WAKEUP |
				PMU_CC6_ENABLE_PMU_WAKEUP_PERST),
				0);
		}
	}
}

#if !defined(BCMDONGLEHOST)

/* write :
 *   TRUE - Programs the PLLCTRL6 with xtal and returns value written in pllctrl6 register.
 *   FALSE - returns 0 if xtal programming is same as pllctrl6 register else retruns value of
 *   pllctrl6 val. This will not program any register.
 */
static uint32
si_pmu_pll6val_armclk_calc(osl_t *osh, pmuregs_t *pmu, uint32 armclk, uint32 xtal, bool write)
{
	uint32 q, r;
	uint32 xtal_scale;
	uint32 pll6val;
	if (armclk == 0 || xtal == 0) {
		PMU_ERROR((" si_pmu_pll6val_armclk_calc: invalid armclk = %d or xtal = %d\n",
			armclk, xtal));
		return 0;
	}
	q = (armclk * 1000 * PMU4369_PLL6VAL_P1DIV) / xtal;
	xtal_scale = xtal / 100;
	r = ((armclk * 10 * PMU4369_PLL6VAL_P1DIV * PMU4369_PLL6VAL_PRE_SCALE) / xtal_scale) -
		(q * PMU4369_PLL6VAL_PRE_SCALE);
	r *= PMU4369_PLL6VAL_POST_SCALE;

	pll6val = (r << PMU4369_PLL1_PC6_NDIV_FRAC_SHIFT) |
		(q << PMU4369_PLL1_PC6_NDIV_INT_SHIFT) | PMU4369_PLL6VAL_P1DIV_BIT3_2;

	PMU_MSG(("si_pmu_pll6val_armclk_calc, armclk %d, xtal %d, q %d, r 0x%8x, pll6val 0x%8x\n",
		armclk, xtal, q, r, pll6val));

	if (write) {
		W_REG(osh, &pmu->pllcontrol_addr, PMU1_PLL0_PLLCTL6);
		W_REG(osh, &pmu->pllcontrol_data, pll6val);
	} else {
		W_REG(osh, &pmu->pllcontrol_addr, PMU1_PLL0_PLLCTL6);
		if (pll6val == R_REG(osh, &pmu->pllcontrol_data))
			return 0;
	}

	return pll6val;
}

static void
BCMATTACHFN(si_pmu_chipcontrol_xtal_settings_4369)(si_t *sih)
{

/* 4369 XTAL Bias settings */
/*
	Reg name		startup		Normal
	xtal_bias_adj		0xFF		0x1A
	xtal_coresize_nmos	0x3f		0x3f
	xtal_coresize_pmos	0x3f		0x3f
	xtal_sel_bias_res	0x2		0x6
	xt_res_bypass		0x0		0x1
*/
	uint32 u32Val;
	uint32 u32Mask;
	u32Val = (PMU_CC0_4369B0_XTALCORESIZE_BIAS_ADJ_NORMAL_VAL |
		PMU_CC0_4369_XTAL_RES_BYPASS_NORMAL_VAL);

	u32Mask = (PMU_CC0_4369_XTALCORESIZE_BIAS_ADJ_NORMAL_MASK |
		PMU_CC0_4369_XTAL_RES_BYPASS_NORMAL_MASK);

	si_pmu_chipcontrol(sih, PMU_CHIPCTL0, u32Mask, u32Val);

	u32Val = (PMU_CC2_4369_XTALCORESIZE_BIAS_ADJ_NORMAL_VAL);
	u32Mask = (PMU_CC2_4369_XTALCORESIZE_BIAS_ADJ_NORMAL_MASK);
	si_pmu_chipcontrol(sih, PMU_CHIPCTL2, u32Mask, u32Val);

	u32Val = (PMU_CC3_4369_XTALCORESIZE_PMOS_NORMAL_VAL |
		PMU_CC3_4369_XTALCORESIZE_NMOS_NORMAL_VAL |
		PMU_CC3_4369_XTALSEL_BIAS_RES_NORMAL_VAL);

	u32Mask = (PMU_CC3_4369_XTALCORESIZE_PMOS_NORMAL_MASK |
		PMU_CC3_4369_XTALCORESIZE_NMOS_NORMAL_MASK |
		PMU_CC3_4369_XTALSEL_BIAS_RES_NORMAL_MASK);

	si_pmu_chipcontrol(sih, PMU_CHIPCTL3, u32Mask, u32Val);

}

static void
BCMATTACHFN(si_pmu_chipcontrol_xtal_settings_4362)(si_t *sih)
{
	/* 4369 XTAL Bias settings */
	/*
		Reg name		startup		Normal
		xtal_bias_adj		0xFF		0x1A
		xtal_coresize_nmos	0x3f		0x3f
		xtal_coresize_pmos	0x3f		0x3f
		xtal_sel_bias_res	0x2		0x6
		xt_res_bypass		0x0		0x1
	*/
	uint32 u32Val;
	uint32 u32Mask;
	u32Val = (PMU_CC0_4362_XTALCORESIZE_BIAS_ADJ_NORMAL_VAL |
		PMU_CC0_4362_XTAL_RES_BYPASS_NORMAL_VAL);

	u32Mask = (PMU_CC0_4362_XTALCORESIZE_BIAS_ADJ_NORMAL_MASK |
		PMU_CC0_4362_XTAL_RES_BYPASS_NORMAL_MASK);

	si_pmu_chipcontrol(sih, PMU_CHIPCTL0, u32Mask, u32Val);

	u32Val = (PMU_CC2_4362_XTALCORESIZE_BIAS_ADJ_NORMAL_VAL);
	u32Mask = (PMU_CC2_4362_XTALCORESIZE_BIAS_ADJ_NORMAL_MASK);
	si_pmu_chipcontrol(sih, PMU_CHIPCTL2, u32Mask, u32Val);

	u32Val = (PMU_CC3_4362_XTALCORESIZE_PMOS_NORMAL_VAL |
		PMU_CC3_4362_XTALCORESIZE_NMOS_NORMAL_VAL |
		PMU_CC3_4362_XTALSEL_BIAS_RES_NORMAL_VAL);

	u32Mask = (PMU_CC3_4362_XTALCORESIZE_PMOS_NORMAL_MASK |
		PMU_CC3_4362_XTALCORESIZE_NMOS_NORMAL_MASK |
		PMU_CC3_4362_XTALSEL_BIAS_RES_NORMAL_MASK);

	si_pmu_chipcontrol(sih, PMU_CHIPCTL3, u32Mask, u32Val);

}

/* 4378 based on 4369 XTAL Bias settings
 *	Reg name		startup		Normal
 *	xtal_bias_adj		0xFF		0x1A
 *	xtal_coresize_nmos	0x3f		0x3f
 *	xtal_coresize_pmos	0x3f		0x3f
 *	xtal_sel_bias_res	0x2		0x2
 *	xt_res_bypass		0x0		0x2
 */
static void
BCMATTACHFN(si_pmu_chipcontrol_xtal_settings_4378)(si_t *sih)
{
	uint32 u32Val;
	uint32 u32Mask;
	uint16 xtal_bias_adj;
	uint8 xtal_bias_adj_otp = 0, xtal_bias_cal_otp_done = 0;

#ifdef XTAL_BIAS_FROM_OTP
	/* Read xtal bias cal done bit and xtal biase from OTP */
	si_pmu_chipcontrol_xtal_bias_from_otp(sih, &xtal_bias_cal_otp_done, &xtal_bias_adj_otp);
#endif /* XTAL_BIAS_FROM_OTP */

	/*
	 * If xtal_bias_cal_done flag is read as non zero, write the xtal biase in PMU control
	 * register from OTP otherwise write the default value of 0x1a.
	 */
	xtal_bias_adj = (uint16)xtal_bias_adj_otp;
	xtal_bias_adj = xtal_bias_cal_otp_done != 0 ? (xtal_bias_adj << 6) :
		PMU_CC0_4378_XTALCORESIZE_BIAS_ADJ_NORMAL_VAL;

	u32Val = (xtal_bias_adj | PMU_CC0_4378_XTAL_RES_BYPASS_NORMAL_VAL);

	u32Mask = (PMU_CC0_4378_XTALCORESIZE_BIAS_ADJ_NORMAL_MASK |
		PMU_CC0_4378_XTAL_RES_BYPASS_NORMAL_MASK);

	si_pmu_chipcontrol(sih, PMU_CHIPCTL0, u32Mask, u32Val);

	u32Val = (PMU_CC2_4378_XTALCORESIZE_BIAS_ADJ_NORMAL_VAL);
	u32Mask = (PMU_CC2_4378_XTALCORESIZE_BIAS_ADJ_NORMAL_MASK);
	si_pmu_chipcontrol(sih, PMU_CHIPCTL2, u32Mask, u32Val);

	u32Val = (PMU_CC3_4378_XTALCORESIZE_PMOS_NORMAL_VAL |
		PMU_CC3_4378_XTALCORESIZE_NMOS_NORMAL_VAL |
		PMU_CC3_4378_XTALSEL_BIAS_RES_NORMAL_VAL);

	u32Mask = (PMU_CC3_4378_XTALCORESIZE_PMOS_NORMAL_MASK |
		PMU_CC3_4378_XTALCORESIZE_NMOS_NORMAL_MASK |
		PMU_CC3_4378_XTALSEL_BIAS_RES_NORMAL_MASK);

	si_pmu_chipcontrol(sih, PMU_CHIPCTL3, u32Mask, u32Val);

}

#ifdef XTAL_BIAS_FROM_OTP
static void
BCMATTACHFN(si_pmu_chipcontrol_xtal_bias_from_otp)(si_t *sih, uint8* flag, uint8* val)
{
	uint8 xtal_bias_adj = 0, xtal_bias_cal_otp_done = 0;
#ifndef BCM_OTP_API
	uint16 datum, offset;
	uint8 shift, mask;
#endif /* !BCM_OTP_API */

	/* Read the XTAL BIAS CAL value from OTP.
	 * 1) Read the xtal cal done bit and the xtal biase value from OTP.
	 * 2) OTP memory is zero by default, so the chips which aren't OTP programmed will read a
	 *   '0' for xtal_bias_cal_otp_done.
	*/
#ifdef BCM_OTP_API
	otp_read_8b_field(sih, BCM_OTP_FLD_XTAL_BIAS_FLAG, &xtal_bias_cal_otp_done);
	if (xtal_bias_cal_otp_done) {
		otp_read_8b_field(sih, BCM_OTP_FLD_XTAL_BIAS_ADJ, &xtal_bias_adj);
	}
#else
	si_pmu_chipcontrol_xtal_bias_cal_done_offsets(sih, &offset, &shift, &mask);
	if (!otp_read_word(sih, offset, &datum)) {
		xtal_bias_cal_otp_done = ((datum >> shift) & mask);
	}

	si_pmu_chipcontrol_xtal_bias_val_offsets(sih, &offset, &shift, &mask);
	if (xtal_bias_cal_otp_done && (!otp_read_word(sih, offset, &datum)))
	{
		xtal_bias_adj = ((datum >> shift) & mask);
	}
#endif /* BCM_OTP_API */
	*flag = xtal_bias_cal_otp_done;
	*val = xtal_bias_adj;
}

#ifndef BCM_OTP_API
static void
BCMATTACHFN(si_pmu_chipcontrol_xtal_bias_cal_done_offsets)(si_t *sih, uint16* wrd_offset,
		uint8* wrd_shift, uint8* wrd_mask)
{
	/* Offset is 16 bit aligned address, shift is the starting bit position of the value
	 * mask defines the bitwidth of the value. Each value in the array is for one of the
	 * cores.
	 */
	/* XTAL BIAS CAL done                 11896 */
	switch (CHIPID(sih->chip)) {
		case BCM4376_CHIP_GRPID:
		case BCM4378_CHIP_GRPID:
			*wrd_offset = OTP_XTAL_BIAS_CAL_DONE_4378_WRD_OFFSET;
			*wrd_shift  = OTP_XTAL_BIAS_CAL_DONE_4378_WRD_SHIFT;
			*wrd_mask   = OTP_XTAL_BIAS_CAL_DONE_4378_WRD_MASK;
			break;
		default:
			ASSERT(0);
			break;
	}
}

static void
BCMATTACHFN(si_pmu_chipcontrol_xtal_bias_val_offsets)(si_t *sih, uint16* wrd_offset,
		uint8* wrd_shift, uint8* wrd_mask)
{
	/* Offset is 16 bit aligned address, shift is the starting bit position of the value
	 * mask defines the bitwidth of the value. Each value in the array is for one of the
	 * cores.
	 */
	/* XTAL BIAS value                    11888 */
	switch (CHIPID(sih->chip)) {
		case BCM4376_CHIP_GRPID:
		case BCM4378_CHIP_GRPID:
			*wrd_offset = OTP_XTAL_BIAS_VAL_4378_WRD_OFFSET;
			*wrd_shift  = OTP_XTAL_BIAS_VAL_4378_WRD_SHIFT;
			*wrd_mask   = OTP_XTAL_BIAS_VAL_4378_WRD_MASK;
			break;
		default:
			ASSERT(0);
			break;
	}
}
#endif /* !BCM_OTP_API */
#endif /* XTAL_BIAS_FROM_OTP */

#endif /* !BCMDONGLEHOST */

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

	PMU_ERROR(("si_pmustatstimer_dump : TIME %d\n", current_time_ms));

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

#ifdef DONGLEBUILD
/* Note this could be called from trap context !!
 * So observe caution. Do NOT ASSERT() in this function
 * len parameter is dual purpose - On input it is length of the
 * buffer provided. On output it is the amount of data written in
 * bytes.
 */
/* This includes address data pair
 * Note presence of arg2. arg2 could further define what subset of information
 * needs to be dumped. Some external entities such as SMD could optionally pass
 * arg2 to define subset of information needed
 */
int
BCMPOSTTRAPFN(si_pmu_regs_in_rodata_dump)(void *sih, void *arg2,
	uint32 *bufptr, uint16 *len)
{
	int rc = BCME_OK;
	uint16 totalsize = SI_PMU_REG_DUMP_BASE_SIZE;

	if ((bufptr == NULL) || (len == NULL)) {
		rc = BCME_NOMEM;
		goto fail;
	}

	/* Are PMU registers available in rodata? If not, bail out
	 * Avoid re-read. If data is not there, then there could have been
	 * an error in reading these regs.
	 */
	if (rodata_pmuregdump_ptr == NULL) {
		rc = BCME_ERROR;
		goto fail;
	}

	if (si_pmu_get_mac_rsrc_req_tmr_cnt(sih) > 1) {
		totalsize += SI_PMU_REG_DUMP_MACRSRC1_SIZE;
	}
	if (si_pmu_get_mac_rsrc_req_tmr_cnt(sih) > 2) {
		totalsize += SI_PMU_REG_DUMP_MACRSRC2_SIZE;
	}
	if (si_pmu_get_pmu_interrupt_rcv_cnt(sih) > 1) {
		totalsize += SI_PMU_REG_DUMP_INTRCV1_SIZE;
	}

	/* Make sure there is enough space for address value pair */
	if (len && *len < totalsize) {
		rc = BCME_BUFTOOSHORT;
		goto fail;
	}

	/* Write registers to supplied buffer */
	/* Note that rodata_pmuregdump_size needs to be
	 * a multiple of a word size
	 */
	memcpy((uint8*)bufptr, rodata_pmuregdump_ptr, totalsize);

	*len = totalsize;
fail:
	return rc;

}
#endif /* DONGLEBUILD */

/* query the # of mac resource request timers */
uint
BCMPOSTTRAPFN(si_pmu_get_mac_rsrc_req_tmr_cnt)(si_t *sih)
{
	if (PMUREV(sih->pmurev) >= 26) {
		uint32 core_cap_ext = PMU_REG(sih, core_cap_ext, 0, 0);
		uint mac_rsrc_cnt =
		        ((core_cap_ext & PCAP_EXT_MAC_RSRC_REQ_TMR_CNT_MASK) >>
		         PCAP_EXT_MAC_RSRC_REQ_TMR_CNT_SHIFT) + 1;
		return mac_rsrc_cnt;
	}

	return si_numd11coreunits(sih);
}

/* query the # of pmu interrupt recevier */
uint
BCMPOSTTRAPFN(si_pmu_get_pmu_interrupt_rcv_cnt)(si_t *sih)
{
	if (PMUREV(sih->pmurev) >= 26) {
		uint32 core_cap_ext = PMU_REG(sih, core_cap_ext, 0, 0);
		uint pmu_intr_rcvr_cnt =
		        ((core_cap_ext & PCAP_EXT_PMU_INTR_RCVR_CNT_MASK) >>
		         PCAP_EXT_PMU_INTR_RCVR_CNT_SHIFT) + 1;
		return pmu_intr_rcvr_cnt;
	}

	return si_numd11coreunits(sih);
}

#ifdef DONGLEBUILD
int
si_pmu_mem_pwr_off(si_t *sih, int core_idx)
{
	int ret = BCME_OK;

	if (si_setcore(sih, D11_CORE_ID, core_idx) == NULL) {
		/* core_idx doesn't exsist */
		return BCME_BADOPTION;
	}

	switch (CHIPID(sih->chip)) {
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
		if (core_idx == 0) {
			si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
				(PMU_CC4_4387_MAIN_PD_CBUCK2VDDB_ON |
				PMU_CC4_4387_MAIN_PD_CBUCK2VDDRET_ON |
				PMU_CC4_4387_MAIN_PD_MEMLPLDO2VDDB_ON |
				PMU_CC4_4387_MAIN_PD_MEMLPDLO2VDDRET_ON),
				0);

			si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
				(PMU_CC13_MAIN_CBUCK2VDDB_OFF |
				PMU_CC13_MAIN_CBUCK2VDDRET_OFF |
				PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF |
				PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF),
				(PMU_CC13_MAIN_CBUCK2VDDB_OFF |
				PMU_CC13_MAIN_CBUCK2VDDRET_OFF |
				PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF |
				PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF));

			/* LQ settings */
			si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_25,
				0xFFFFFFFF, XTAL_LQ_SETTING_4387);
		} else if (core_idx == 1) {
			si_pmu_chipcontrol(sih, PMU_CHIPCTL4,
				(PMU_CC4_4387_AUX_PD_CBUCK2VDDB_ON |
				PMU_CC4_4387_AUX_PD_CBUCK2VDDRET_ON |
				PMU_CC4_4387_AUX_PD_MEMLPLDO2VDDB_ON |
				PMU_CC4_4387_AUX_PD_MEMLPLDO2VDDRET_ON),
				0);

			si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
				(PMU_CC13_AUX_CBUCK2VDDB_OFF |
				PMU_CC13_AUX_CBUCK2VDDRET_OFF |
				PMU_CC13_AUX_MEMLPLDO2VDDB_OFF |
				PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF),
				(PMU_CC13_AUX_CBUCK2VDDB_OFF |
				PMU_CC13_AUX_CBUCK2VDDRET_OFF |
				PMU_CC13_AUX_MEMLPLDO2VDDB_OFF |
				PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF));
		} else if (core_idx == 2) {
			si_pmu_chipcontrol(sih, PMU_CHIPCTL17,
				(PMU_CC17_SCAN_CBUCK2VDDB_ON |
				PMU_CC17_SCAN_MEMLPLDO2VDDB_ON |
				PMU_CC17_SCAN_MEMLPLDO2VDDRET_ON),
				0);
			si_pmu_chipcontrol(sih, PMU_CHIPCTL17,
				(PMU_CC17_SCAN_CBUCK2VDDB_OFF |
				PMU_CC17_SCAN_MEMLPLDO2VDDB_OFF |
				PMU_CC17_SCAN_MEMLPLDO2VDDRET_OFF),
				(PMU_CC17_SCAN_CBUCK2VDDB_OFF |
				PMU_CC17_SCAN_MEMLPLDO2VDDB_OFF |
				PMU_CC17_SCAN_MEMLPLDO2VDDRET_OFF));
		}
		break;

	default:
		ret = BCME_UNSUPPORTED;
		break;
	}

	return ret;
}

int
BCMPOSTTRAPFN(si_pmu_mem_pwr_on)(si_t *sih)
{
	int ret = BCME_OK;

	switch (CHIPID(sih->chip)) {
	case BCM4385_CHIP_GRPID:
	case BCM4387_CHIP_GRPID:
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
			(PMU_CC13_MAIN_CBUCK2VDDB_OFF |
			PMU_CC13_MAIN_CBUCK2VDDRET_OFF |
			PMU_CC13_MAIN_MEMLPLDO2VDDB_OFF |
			PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF),
			PMU_CC13_MAIN_MEMLPLDO2VDDRET_OFF);
		si_pmu_chipcontrol(sih, PMU_CHIPCTL13,
			(PMU_CC13_AUX_CBUCK2VDDB_OFF |
			PMU_CC13_AUX_CBUCK2VDDRET_OFF |
			PMU_CC13_AUX_MEMLPLDO2VDDB_OFF |
			PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF),
			PMU_CC13_AUX_MEMLPLDO2VDDRET_OFF);
		si_pmu_chipcontrol(sih, PMU_CHIPCTL17,
			(PMU_CC17_SCAN_CBUCK2VDDB_OFF |
			PMU_CC17_SCAN_MEMLPLDO2VDDB_OFF |
			PMU_CC17_SCAN_MEMLPLDO2VDDRET_OFF),
			PMU_CC17_SCAN_MEMLPLDO2VDDRET_OFF);

		/* HQ settings */
		si_gci_chipcontrol(sih, CC_GCI_CHIPCTRL_25,
			0xFFFFFFFF, XTAL_HQ_SETTING_4387);
	break;

	default:
		ret = BCME_UNSUPPORTED;
		break;
	}

	return ret;
}

void
BCMPOSTTRAPFN(si_pmu_disable_intr_pwrreq)(si_t *sih)
{
	if (MULTIBP_CAP(sih)) {
		switch (CHIPID(sih->chip)) {
		case BCM4376_CHIP_GRPID:
		case BCM4378_CHIP_GRPID:
		case BCM4385_CHIP_GRPID:
		case BCM4387_CHIP_GRPID:
		case BCM4388_CHIP_GRPID:
		case BCM4389_CHIP_GRPID:
		case BCM4397_CHIP_GRPID:
			si_pmu_chipcontrol(sih, PMU_CHIPCTL2, PMU_CC2_CB2WL_INTR_PWRREQ_EN, 0);
			si_pmu_chipcontrol(sih, PMU_CHIPCTL6, PMU_CC6_ENABLE_DMN1_WAKEUP, 0);
			break;
		default:
			PMU_ERROR(("si_pmu_disable_intr_pwrreq: add support for this chip!\n"));
			OSL_SYS_HALT();
			break;
		}
	}
}

void
BCMPOSTTRAPFN(si_pmu_clear_intmask)(si_t *sih)
{
	pmuregs_t *pmu;
	uint origidx;
	osl_t *osh = si_osh(sih);
	uint pmu_intr_recvr_cnt;

	/* Remember original core before switch to chipc/pmu */
	origidx = si_coreidx(sih);
	if (AOB_ENAB(sih)) {
		pmu = si_setcore(sih, PMU_CORE_ID, 0);
	} else {
		pmu = si_setcoreidx(sih, SI_CC_IDX);
	}

	ASSERT(pmu != NULL);
	W_REG(osh, &pmu->pmuintmask0, 0);

	pmu_intr_recvr_cnt = ((R_REG(osh, &pmu->core_cap_ext) & PCAP_EXT_PMU_INTR_RCVR_CNT_MASK)
			>> PCAP_EXT_PMU_INTR_RCVR_CNT_SHIFT) + 1;

	if (pmu_intr_recvr_cnt > 1) {
		W_REG(osh, &pmu->pmuintmask1, 0);
	}

	/* Return to original core */
	si_setcoreidx(sih, origidx);
}
#endif /* DONGLEBUILD */

int
si_pmu_res_state_pwrsw_main_wait(si_t *sih)
{
	int ret = BCME_OK;

	switch (CHIPID(sih->chip)) {
	case BCM4387_CHIP_GRPID:
		if (PMU_REG(sih, res_state, 0, 0) & PMURES_BIT(RES4387_PWRSW_MAIN)) {
			SPINWAIT((PMU_REG(sih, res_state, 0, 0) &
				PMURES_BIT(RES4387_PWRSW_MAIN)), 10000);
			OSL_DELAY(1000);
		}
		ret = (PMU_REG(sih, res_state, 0, 0) & PMURES_BIT(RES4387_PWRSW_MAIN)) ?
			BCME_ERROR : BCME_OK;
		break;
	default:
		PMU_ERROR(("si_pmu_res_state_pwrsw_main_wait: add support for this chip!\n"));
		OSL_SYS_HALT();
		break;
	}

	return ret;
}

int
si_pmu_lvm_csr_update(si_t *sih, bool lvm)
{

#ifdef BCMDVFS
	if (BCMDVFS_ENAB() && si_dvfs_enable_status(sih)) {
		uint32 ndv_volt = lvm ? DVFS_VOLTAGE_NDV : DVFS_VOLTAGE_NDV_NON_LVM;
		si_dvfs_set_ndv_voltage(sih, ndv_volt);
	} else
#endif /* BCMDVFS */
	{
		uint32 cbuck_volt = lvm ? CBUCK_VOLT_SW_DEFAULT_4387 : CBUCK_VOLT_NON_LVM;
		si_pmu_vreg_control(sih, PMU_VREG_0,
			VREG0_4378_CSR_VOLT_ADJ_PWM_MASK,
			cbuck_volt << VREG0_4378_CSR_VOLT_ADJ_PWM_SHIFT);
	}
	return BCME_OK;
}

#if defined(BT_WLAN_REG_ON_WAR)
void
si_pmu_reg_on_war_ext_wake_perst_set(si_t *sih)
{
	uint origidx = si_coreidx(sih);
	pmuregs_t *pmu = si_setcore(sih, PMU_CORE_ID, 0);
	osl_t *osh = si_osh(sih);

	if (PMUREV(sih->pmurev) == 40) {
		/*
		 * set PCIEPerstReq (bit-5) as a wake-up source in
		 * ExtWakeMask0 (0x760) register
		 */
		W_REG(osh, &pmu->extwakemask0, PMU_EXT_WAKE_MASK_0_PCIE_PERST);

		/*
		 * configure the wakemask as "common backplane" resources to
		 * be up during wake-up in ExtWakeReqMask0 (0x770) register
		 */
		W_REG(osh, &pmu->extwakereqmask[0], REG_ON_WAR_PMU_EXT_WAKE_REQ_MASK0_VAL);
	}

	si_setcoreidx(sih, origidx);
}

void
si_pmu_reg_on_war_ext_wake_perst_clear(si_t *sih)
{
	uint32 val = 0;
	uint origidx = si_coreidx(sih);
	pmuregs_t *pmu = si_setcore(sih, PMU_CORE_ID, 0);
	osl_t *osh = si_osh(sih);

	if (PMUREV(sih->pmurev) == 40) {
		/* clear all set bits in ExtWakeupStatus (0x744) register */
		val = R_REG(osh, &pmu->extwakeupstatus);
		W_REG(osh, &pmu->extwakeupstatus, val);
	}

	si_setcoreidx(sih, origidx);
}
#endif /* BT_WLAN_REG_ON_WAR */

void
si_pmu_res_state_wait(si_t *sih, uint rsrc)
{
	SPINWAIT(!(PMU_REG(sih, res_state, 0, 0) & PMURES_BIT(rsrc)), PMU_MAX_TRANSITION_DLY);
	ASSERT(PMU_REG(sih, res_state, 0, 0) & PMURES_BIT(rsrc));
}
