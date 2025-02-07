// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Ice Lake PCH.
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include "core.h"

const struct pmc_bit_map icl_pfear_map[] = {
	{"RES_65",		BIT(0)},
	{"RES_66",		BIT(1)},
	{"RES_67",		BIT(2)},
	{"TAM",			BIT(3)},
	{"GBETSN",		BIT(4)},
	{"TBTLSX",		BIT(5)},
	{"RES_71",		BIT(6)},
	{"RES_72",		BIT(7)},
	{}
};

const struct pmc_bit_map *ext_icl_pfear_map[] = {
	/*
	 * Check intel_pmc_core_ids[] users of icl_reg_map for
	 * a list of core SoCs using this.
	 */
	cnp_pfear_map,
	icl_pfear_map,
	NULL
};

const struct pmc_reg_map icl_reg_map = {
	.pfear_sts = ext_icl_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = ICL_PMC_SLP_S0_RES_COUNTER_STEP,
	.slps0_dbg_maps = cnp_slps0_dbg_maps,
	.ltr_show_sts = cnp_ltr_show_map,
	.msr_sts = msr_map,
	.slps0_dbg_offset = CNP_PMC_SLPS0_DBG_OFFSET,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = ICL_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = ICL_NUM_IP_IGN_ALLOWED,
	.etr3_offset = ETR3_OFFSET,
};

struct pmc_dev_info icl_pmc_dev = {
	.map = &icl_reg_map,
};
