/*
 *  Copyright (C) 2014 Free Electrons
 *
 *  Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/mtd/nand.h>

static const struct nand_data_interface onfi_sdr_timings[] = {
	/* Mode 0 */
	{
		.type = NAND_SDR_IFACE,
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
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
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
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
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
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
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
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
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
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
		.timings.sdr = {
			.tCCS_min = 500000,
			.tR_max = 200000000,
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

/**
 * onfi_async_timing_mode_to_sdr_timings - [NAND Interface] Retrieve NAND
 * timings according to the given ONFI timing mode
 * @mode: ONFI timing mode
 */
const struct nand_sdr_timings *onfi_async_timing_mode_to_sdr_timings(int mode)
{
	if (mode < 0 || mode >= ARRAY_SIZE(onfi_sdr_timings))
		return ERR_PTR(-EINVAL);

	return &onfi_sdr_timings[mode].timings.sdr;
}
EXPORT_SYMBOL(onfi_async_timing_mode_to_sdr_timings);

/**
 * onfi_init_data_interface - [NAND Interface] Initialize a data interface from
 * given ONFI mode
 * @iface: The data interface to be initialized
 * @mode: The ONFI timing mode
 */
int onfi_init_data_interface(struct nand_chip *chip,
			     struct nand_data_interface *iface,
			     enum nand_data_interface_type type,
			     int timing_mode)
{
	if (type != NAND_SDR_IFACE)
		return -EINVAL;

	if (timing_mode < 0 || timing_mode >= ARRAY_SIZE(onfi_sdr_timings))
		return -EINVAL;

	*iface = onfi_sdr_timings[timing_mode];

	/*
	 * Initialize timings that cannot be deduced from timing mode:
	 * tR, tPROG, tCCS, ...
	 * These information are part of the ONFI parameter page.
	 */
	if (chip->onfi_version) {
		struct nand_onfi_params *params = &chip->onfi_params;
		struct nand_sdr_timings *timings = &iface->timings.sdr;

		/* microseconds -> picoseconds */
		timings->tPROG_max = 1000000UL * le16_to_cpu(params->t_prog);
		timings->tBERS_max = 1000000UL * le16_to_cpu(params->t_bers);
		timings->tR_max = 1000000UL * le16_to_cpu(params->t_r);

		/* nanoseconds -> picoseconds */
		timings->tCCS_min = 1000UL * le16_to_cpu(params->t_ccs);
	}

	return 0;
}
EXPORT_SYMBOL(onfi_init_data_interface);

/**
 * nand_get_default_data_interface - [NAND Interface] Retrieve NAND
 * data interface for mode 0. This is used as default timing after
 * reset.
 */
const struct nand_data_interface *nand_get_default_data_interface(void)
{
	return &onfi_sdr_timings[0];
}
EXPORT_SYMBOL(nand_get_default_data_interface);
