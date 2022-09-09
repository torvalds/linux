// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2014 Free Electrons
 *
 *  Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/export.h>

#include "internals.h"

#define ONFI_DYN_TIMING_MAX U16_MAX

/*
 * For non-ONFI chips we use the highest possible value for tPROG and tBERS.
 * tR and tCCS will take the default values precised in the ONFI specification
 * for timing mode 0, respectively 200us and 500ns.
 *
 * These four values are tweaked to be more accurate in the case of ONFI chips.
 */
static const struct nand_interface_config onfi_sdr_timings[] = {
	/* Mode 0 */
	{
		.type = NAND_SDR_IFACE,
		.timings.mode = 0,
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tADL_min = 400000,
			.tALH_min = 20000,
			.tALS_min = 50000,
			.tAR_min = 25000,
			.tCEA_max = 100000,
			.tCEH_min = 20000,
			.tCH_min = 20000,
			.tCHZ_max = 100000,
			.tCLH_min = 20000,
			.tCLR_min = 20000,
			.tCLS_min = 50000,
			.tCOH_min = 0,
			.tCS_min = 70000,
			.tDH_min = 20000,
			.tDS_min = 40000,
			.tFEAT_max = 1000000,
			.tIR_min = 10000,
			.tITC_max = 1000000,
			.tRC_min = 100000,
			.tREA_max = 40000,
			.tREH_min = 30000,
			.tRHOH_min = 0,
			.tRHW_min = 200000,
			.tRHZ_max = 200000,
			.tRLOH_min = 0,
			.tRP_min = 50000,
			.tRR_min = 40000,
			.tRST_max = 250000000000ULL,
			.tWB_max = 200000,
			.tWC_min = 100000,
			.tWH_min = 30000,
			.tWHR_min = 120000,
			.tWP_min = 50000,
			.tWW_min = 100000,
		},
	},
	/* Mode 1 */
	{
		.type = NAND_SDR_IFACE,
		.timings.mode = 1,
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tADL_min = 400000,
			.tALH_min = 10000,
			.tALS_min = 25000,
			.tAR_min = 10000,
			.tCEA_max = 45000,
			.tCEH_min = 20000,
			.tCH_min = 10000,
			.tCHZ_max = 50000,
			.tCLH_min = 10000,
			.tCLR_min = 10000,
			.tCLS_min = 25000,
			.tCOH_min = 15000,
			.tCS_min = 35000,
			.tDH_min = 10000,
			.tDS_min = 20000,
			.tFEAT_max = 1000000,
			.tIR_min = 0,
			.tITC_max = 1000000,
			.tRC_min = 50000,
			.tREA_max = 30000,
			.tREH_min = 15000,
			.tRHOH_min = 15000,
			.tRHW_min = 100000,
			.tRHZ_max = 100000,
			.tRLOH_min = 0,
			.tRP_min = 25000,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tWC_min = 45000,
			.tWH_min = 15000,
			.tWHR_min = 80000,
			.tWP_min = 25000,
			.tWW_min = 100000,
		},
	},
	/* Mode 2 */
	{
		.type = NAND_SDR_IFACE,
		.timings.mode = 2,
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tADL_min = 400000,
			.tALH_min = 10000,
			.tALS_min = 15000,
			.tAR_min = 10000,
			.tCEA_max = 30000,
			.tCEH_min = 20000,
			.tCH_min = 10000,
			.tCHZ_max = 50000,
			.tCLH_min = 10000,
			.tCLR_min = 10000,
			.tCLS_min = 15000,
			.tCOH_min = 15000,
			.tCS_min = 25000,
			.tDH_min = 5000,
			.tDS_min = 15000,
			.tFEAT_max = 1000000,
			.tIR_min = 0,
			.tITC_max = 1000000,
			.tRC_min = 35000,
			.tREA_max = 25000,
			.tREH_min = 15000,
			.tRHOH_min = 15000,
			.tRHW_min = 100000,
			.tRHZ_max = 100000,
			.tRLOH_min = 0,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tRP_min = 17000,
			.tWC_min = 35000,
			.tWH_min = 15000,
			.tWHR_min = 80000,
			.tWP_min = 17000,
			.tWW_min = 100000,
		},
	},
	/* Mode 3 */
	{
		.type = NAND_SDR_IFACE,
		.timings.mode = 3,
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tADL_min = 400000,
			.tALH_min = 5000,
			.tALS_min = 10000,
			.tAR_min = 10000,
			.tCEA_max = 25000,
			.tCEH_min = 20000,
			.tCH_min = 5000,
			.tCHZ_max = 50000,
			.tCLH_min = 5000,
			.tCLR_min = 10000,
			.tCLS_min = 10000,
			.tCOH_min = 15000,
			.tCS_min = 25000,
			.tDH_min = 5000,
			.tDS_min = 10000,
			.tFEAT_max = 1000000,
			.tIR_min = 0,
			.tITC_max = 1000000,
			.tRC_min = 30000,
			.tREA_max = 20000,
			.tREH_min = 10000,
			.tRHOH_min = 15000,
			.tRHW_min = 100000,
			.tRHZ_max = 100000,
			.tRLOH_min = 0,
			.tRP_min = 15000,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tWC_min = 30000,
			.tWH_min = 10000,
			.tWHR_min = 80000,
			.tWP_min = 15000,
			.tWW_min = 100000,
		},
	},
	/* Mode 4 */
	{
		.type = NAND_SDR_IFACE,
		.timings.mode = 4,
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tADL_min = 400000,
			.tALH_min = 5000,
			.tALS_min = 10000,
			.tAR_min = 10000,
			.tCEA_max = 25000,
			.tCEH_min = 20000,
			.tCH_min = 5000,
			.tCHZ_max = 30000,
			.tCLH_min = 5000,
			.tCLR_min = 10000,
			.tCLS_min = 10000,
			.tCOH_min = 15000,
			.tCS_min = 20000,
			.tDH_min = 5000,
			.tDS_min = 10000,
			.tFEAT_max = 1000000,
			.tIR_min = 0,
			.tITC_max = 1000000,
			.tRC_min = 25000,
			.tREA_max = 20000,
			.tREH_min = 10000,
			.tRHOH_min = 15000,
			.tRHW_min = 100000,
			.tRHZ_max = 100000,
			.tRLOH_min = 5000,
			.tRP_min = 12000,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tWC_min = 25000,
			.tWH_min = 10000,
			.tWHR_min = 80000,
			.tWP_min = 12000,
			.tWW_min = 100000,
		},
	},
	/* Mode 5 */
	{
		.type = NAND_SDR_IFACE,
		.timings.mode = 5,
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tADL_min = 400000,
			.tALH_min = 5000,
			.tALS_min = 10000,
			.tAR_min = 10000,
			.tCEA_max = 25000,
			.tCEH_min = 20000,
			.tCH_min = 5000,
			.tCHZ_max = 30000,
			.tCLH_min = 5000,
			.tCLR_min = 10000,
			.tCLS_min = 10000,
			.tCOH_min = 15000,
			.tCS_min = 15000,
			.tDH_min = 5000,
			.tDS_min = 7000,
			.tFEAT_max = 1000000,
			.tIR_min = 0,
			.tITC_max = 1000000,
			.tRC_min = 20000,
			.tREA_max = 16000,
			.tREH_min = 7000,
			.tRHOH_min = 15000,
			.tRHW_min = 100000,
			.tRHZ_max = 100000,
			.tRLOH_min = 5000,
			.tRP_min = 10000,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tWC_min = 20000,
			.tWH_min = 7000,
			.tWHR_min = 80000,
			.tWP_min = 10000,
			.tWW_min = 100000,
		},
	},
};

static const struct nand_interface_config onfi_nvddr_timings[] = {
	/* Mode 0 */
	{
		.type = NAND_NVDDR_IFACE,
		.timings.mode = 0,
		.timings.nvddr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tAC_min = 3000,
			.tAC_max = 25000,
			.tADL_min = 400000,
			.tCAD_min = 45000,
			.tCAH_min = 10000,
			.tCALH_min = 10000,
			.tCALS_min = 10000,
			.tCAS_min = 10000,
			.tCEH_min = 20000,
			.tCH_min = 10000,
			.tCK_min = 50000,
			.tCS_min = 35000,
			.tDH_min = 5000,
			.tDQSCK_min = 3000,
			.tDQSCK_max = 25000,
			.tDQSD_min = 0,
			.tDQSD_max = 18000,
			.tDQSHZ_max = 20000,
			.tDQSQ_max = 5000,
			.tDS_min = 5000,
			.tDSC_min = 50000,
			.tFEAT_max = 1000000,
			.tITC_max = 1000000,
			.tQHS_max = 6000,
			.tRHW_min = 100000,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tWHR_min = 80000,
			.tWRCK_min = 20000,
			.tWW_min = 100000,
		},
	},
	/* Mode 1 */
	{
		.type = NAND_NVDDR_IFACE,
		.timings.mode = 1,
		.timings.nvddr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tAC_min = 3000,
			.tAC_max = 25000,
			.tADL_min = 400000,
			.tCAD_min = 45000,
			.tCAH_min = 5000,
			.tCALH_min = 5000,
			.tCALS_min = 5000,
			.tCAS_min = 5000,
			.tCEH_min = 20000,
			.tCH_min = 5000,
			.tCK_min = 30000,
			.tCS_min = 25000,
			.tDH_min = 2500,
			.tDQSCK_min = 3000,
			.tDQSCK_max = 25000,
			.tDQSD_min = 0,
			.tDQSD_max = 18000,
			.tDQSHZ_max = 20000,
			.tDQSQ_max = 2500,
			.tDS_min = 3000,
			.tDSC_min = 30000,
			.tFEAT_max = 1000000,
			.tITC_max = 1000000,
			.tQHS_max = 3000,
			.tRHW_min = 100000,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tWHR_min = 80000,
			.tWRCK_min = 20000,
			.tWW_min = 100000,
		},
	},
	/* Mode 2 */
	{
		.type = NAND_NVDDR_IFACE,
		.timings.mode = 2,
		.timings.nvddr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tAC_min = 3000,
			.tAC_max = 25000,
			.tADL_min = 400000,
			.tCAD_min = 45000,
			.tCAH_min = 4000,
			.tCALH_min = 4000,
			.tCALS_min = 4000,
			.tCAS_min = 4000,
			.tCEH_min = 20000,
			.tCH_min = 4000,
			.tCK_min = 20000,
			.tCS_min = 15000,
			.tDH_min = 1700,
			.tDQSCK_min = 3000,
			.tDQSCK_max = 25000,
			.tDQSD_min = 0,
			.tDQSD_max = 18000,
			.tDQSHZ_max = 20000,
			.tDQSQ_max = 1700,
			.tDS_min = 2000,
			.tDSC_min = 20000,
			.tFEAT_max = 1000000,
			.tITC_max = 1000000,
			.tQHS_max = 2000,
			.tRHW_min = 100000,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tWHR_min = 80000,
			.tWRCK_min = 20000,
			.tWW_min = 100000,
		},
	},
	/* Mode 3 */
	{
		.type = NAND_NVDDR_IFACE,
		.timings.mode = 3,
		.timings.nvddr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tAC_min = 3000,
			.tAC_max = 25000,
			.tADL_min = 400000,
			.tCAD_min = 45000,
			.tCAH_min = 3000,
			.tCALH_min = 3000,
			.tCALS_min = 3000,
			.tCAS_min = 3000,
			.tCEH_min = 20000,
			.tCH_min = 3000,
			.tCK_min = 15000,
			.tCS_min = 15000,
			.tDH_min = 1300,
			.tDQSCK_min = 3000,
			.tDQSCK_max = 25000,
			.tDQSD_min = 0,
			.tDQSD_max = 18000,
			.tDQSHZ_max = 20000,
			.tDQSQ_max = 1300,
			.tDS_min = 1500,
			.tDSC_min = 15000,
			.tFEAT_max = 1000000,
			.tITC_max = 1000000,
			.tQHS_max = 1500,
			.tRHW_min = 100000,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tWHR_min = 80000,
			.tWRCK_min = 20000,
			.tWW_min = 100000,
		},
	},
	/* Mode 4 */
	{
		.type = NAND_NVDDR_IFACE,
		.timings.mode = 4,
		.timings.nvddr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tAC_min = 3000,
			.tAC_max = 25000,
			.tADL_min = 400000,
			.tCAD_min = 45000,
			.tCAH_min = 2500,
			.tCALH_min = 2500,
			.tCALS_min = 2500,
			.tCAS_min = 2500,
			.tCEH_min = 20000,
			.tCH_min = 2500,
			.tCK_min = 12000,
			.tCS_min = 15000,
			.tDH_min = 1100,
			.tDQSCK_min = 3000,
			.tDQSCK_max = 25000,
			.tDQSD_min = 0,
			.tDQSD_max = 18000,
			.tDQSHZ_max = 20000,
			.tDQSQ_max = 1000,
			.tDS_min = 1100,
			.tDSC_min = 12000,
			.tFEAT_max = 1000000,
			.tITC_max = 1000000,
			.tQHS_max = 1200,
			.tRHW_min = 100000,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tWHR_min = 80000,
			.tWRCK_min = 20000,
			.tWW_min = 100000,
		},
	},
	/* Mode 5 */
	{
		.type = NAND_NVDDR_IFACE,
		.timings.mode = 5,
		.timings.nvddr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
			.tPROG_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tBERS_max = 1000000ULL * ONFI_DYN_TIMING_MAX,
			.tAC_min = 3000,
			.tAC_max = 25000,
			.tADL_min = 400000,
			.tCAD_min = 45000,
			.tCAH_min = 2000,
			.tCALH_min = 2000,
			.tCALS_min = 2000,
			.tCAS_min = 2000,
			.tCEH_min = 20000,
			.tCH_min = 2000,
			.tCK_min = 10000,
			.tCS_min = 15000,
			.tDH_min = 900,
			.tDQSCK_min = 3000,
			.tDQSCK_max = 25000,
			.tDQSD_min = 0,
			.tDQSD_max = 18000,
			.tDQSHZ_max = 20000,
			.tDQSQ_max = 850,
			.tDS_min = 900,
			.tDSC_min = 10000,
			.tFEAT_max = 1000000,
			.tITC_max = 1000000,
			.tQHS_max = 1000,
			.tRHW_min = 100000,
			.tRR_min = 20000,
			.tRST_max = 500000000,
			.tWB_max = 100000,
			.tWHR_min = 80000,
			.tWRCK_min = 20000,
			.tWW_min = 100000,
		},
	},
};

/* All NAND chips share the same reset data interface: SDR mode 0 */
const struct nand_interface_config *nand_get_reset_interface_config(void)
{
	return &onfi_sdr_timings[0];
}

/**
 * onfi_find_closest_sdr_mode - Derive the closest ONFI SDR timing mode given a
 *                              set of timings
 * @spec_timings: the timings to challenge
 */
unsigned int
onfi_find_closest_sdr_mode(const struct nand_sdr_timings *spec_timings)
{
	const struct nand_sdr_timings *onfi_timings;
	int mode;

	for (mode = ARRAY_SIZE(onfi_sdr_timings) - 1; mode > 0; mode--) {
		onfi_timings = &onfi_sdr_timings[mode].timings.sdr;

		if (spec_timings->tCCS_min <= onfi_timings->tCCS_min &&
		    spec_timings->tADL_min <= onfi_timings->tADL_min &&
		    spec_timings->tALH_min <= onfi_timings->tALH_min &&
		    spec_timings->tALS_min <= onfi_timings->tALS_min &&
		    spec_timings->tAR_min <= onfi_timings->tAR_min &&
		    spec_timings->tCEH_min <= onfi_timings->tCEH_min &&
		    spec_timings->tCH_min <= onfi_timings->tCH_min &&
		    spec_timings->tCLH_min <= onfi_timings->tCLH_min &&
		    spec_timings->tCLR_min <= onfi_timings->tCLR_min &&
		    spec_timings->tCLS_min <= onfi_timings->tCLS_min &&
		    spec_timings->tCOH_min <= onfi_timings->tCOH_min &&
		    spec_timings->tCS_min <= onfi_timings->tCS_min &&
		    spec_timings->tDH_min <= onfi_timings->tDH_min &&
		    spec_timings->tDS_min <= onfi_timings->tDS_min &&
		    spec_timings->tIR_min <= onfi_timings->tIR_min &&
		    spec_timings->tRC_min <= onfi_timings->tRC_min &&
		    spec_timings->tREH_min <= onfi_timings->tREH_min &&
		    spec_timings->tRHOH_min <= onfi_timings->tRHOH_min &&
		    spec_timings->tRHW_min <= onfi_timings->tRHW_min &&
		    spec_timings->tRLOH_min <= onfi_timings->tRLOH_min &&
		    spec_timings->tRP_min <= onfi_timings->tRP_min &&
		    spec_timings->tRR_min <= onfi_timings->tRR_min &&
		    spec_timings->tWC_min <= onfi_timings->tWC_min &&
		    spec_timings->tWH_min <= onfi_timings->tWH_min &&
		    spec_timings->tWHR_min <= onfi_timings->tWHR_min &&
		    spec_timings->tWP_min <= onfi_timings->tWP_min &&
		    spec_timings->tWW_min <= onfi_timings->tWW_min)
			return mode;
	}

	return 0;
}

/**
 * onfi_find_closest_nvddr_mode - Derive the closest ONFI NVDDR timing mode
 *                                given a set of timings
 * @spec_timings: the timings to challenge
 */
unsigned int
onfi_find_closest_nvddr_mode(const struct nand_nvddr_timings *spec_timings)
{
	const struct nand_nvddr_timings *onfi_timings;
	int mode;

	for (mode = ARRAY_SIZE(onfi_nvddr_timings) - 1; mode > 0; mode--) {
		onfi_timings = &onfi_nvddr_timings[mode].timings.nvddr;

		if (spec_timings->tCCS_min <= onfi_timings->tCCS_min &&
		    spec_timings->tAC_min <= onfi_timings->tAC_min &&
		    spec_timings->tADL_min <= onfi_timings->tADL_min &&
		    spec_timings->tCAD_min <= onfi_timings->tCAD_min &&
		    spec_timings->tCAH_min <= onfi_timings->tCAH_min &&
		    spec_timings->tCALH_min <= onfi_timings->tCALH_min &&
		    spec_timings->tCALS_min <= onfi_timings->tCALS_min &&
		    spec_timings->tCAS_min <= onfi_timings->tCAS_min &&
		    spec_timings->tCEH_min <= onfi_timings->tCEH_min &&
		    spec_timings->tCH_min <= onfi_timings->tCH_min &&
		    spec_timings->tCK_min <= onfi_timings->tCK_min &&
		    spec_timings->tCS_min <= onfi_timings->tCS_min &&
		    spec_timings->tDH_min <= onfi_timings->tDH_min &&
		    spec_timings->tDQSCK_min <= onfi_timings->tDQSCK_min &&
		    spec_timings->tDQSD_min <= onfi_timings->tDQSD_min &&
		    spec_timings->tDS_min <= onfi_timings->tDS_min &&
		    spec_timings->tDSC_min <= onfi_timings->tDSC_min &&
		    spec_timings->tRHW_min <= onfi_timings->tRHW_min &&
		    spec_timings->tRR_min <= onfi_timings->tRR_min &&
		    spec_timings->tWHR_min <= onfi_timings->tWHR_min &&
		    spec_timings->tWRCK_min <= onfi_timings->tWRCK_min &&
		    spec_timings->tWW_min <= onfi_timings->tWW_min)
			return mode;
	}

	return 0;
}

/*
 * onfi_fill_sdr_interface_config - Initialize a SDR interface config from a
 *                                  given ONFI mode
 * @chip: The NAND chip
 * @iface: The interface configuration to fill
 * @timing_mode: The ONFI timing mode
 */
static void onfi_fill_sdr_interface_config(struct nand_chip *chip,
					   struct nand_interface_config *iface,
					   unsigned int timing_mode)
{
	struct onfi_params *onfi = chip->parameters.onfi;

	if (WARN_ON(timing_mode >= ARRAY_SIZE(onfi_sdr_timings)))
		return;

	*iface = onfi_sdr_timings[timing_mode];

	/*
	 * Initialize timings that cannot be deduced from timing mode:
	 * tPROG, tBERS, tR and tCCS.
	 * These information are part of the ONFI parameter page.
	 */
	if (onfi) {
		struct nand_sdr_timings *timings = &iface->timings.sdr;

		/* microseconds -> picoseconds */
		timings->tPROG_max = 1000000ULL * onfi->tPROG;
		timings->tBERS_max = 1000000ULL * onfi->tBERS;
		timings->tR_max = 1000000ULL * onfi->tR;

		/* nanoseconds -> picoseconds */
		timings->tCCS_min = 1000UL * onfi->tCCS;
	}
}

/**
 * onfi_fill_nvddr_interface_config - Initialize a NVDDR interface config from a
 *                                    given ONFI mode
 * @chip: The NAND chip
 * @iface: The interface configuration to fill
 * @timing_mode: The ONFI timing mode
 */
static void onfi_fill_nvddr_interface_config(struct nand_chip *chip,
					     struct nand_interface_config *iface,
					     unsigned int timing_mode)
{
	struct onfi_params *onfi = chip->parameters.onfi;

	if (WARN_ON(timing_mode >= ARRAY_SIZE(onfi_nvddr_timings)))
		return;

	*iface = onfi_nvddr_timings[timing_mode];

	/*
	 * Initialize timings that cannot be deduced from timing mode:
	 * tPROG, tBERS, tR, tCCS and tCAD.
	 * These information are part of the ONFI parameter page.
	 */
	if (onfi) {
		struct nand_nvddr_timings *timings = &iface->timings.nvddr;

		/* microseconds -> picoseconds */
		timings->tPROG_max = 1000000ULL * onfi->tPROG;
		timings->tBERS_max = 1000000ULL * onfi->tBERS;
		timings->tR_max = 1000000ULL * onfi->tR;

		/* nanoseconds -> picoseconds */
		timings->tCCS_min = 1000UL * onfi->tCCS;

		if (onfi->fast_tCAD)
			timings->tCAD_min = 25000;
	}
}

/**
 * onfi_fill_interface_config - Initialize an interface config from a given
 *                              ONFI mode
 * @chip: The NAND chip
 * @iface: The interface configuration to fill
 * @type: The interface type
 * @timing_mode: The ONFI timing mode
 */
void onfi_fill_interface_config(struct nand_chip *chip,
				struct nand_interface_config *iface,
				enum nand_interface_type type,
				unsigned int timing_mode)
{
	if (type == NAND_SDR_IFACE)
		return onfi_fill_sdr_interface_config(chip, iface, timing_mode);
	else
		return onfi_fill_nvddr_interface_config(chip, iface, timing_mode);
}
