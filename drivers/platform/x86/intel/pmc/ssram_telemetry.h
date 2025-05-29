/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel PMC SSRAM Telemetry PCI Driver Header File
 *
 * Copyright (c) 2024, Intel Corporation.
 */

#ifndef PMC_SSRAM_H
#define PMC_SSRAM_H

/**
 * struct pmc_ssram_telemetry - Structure to keep pmc info in ssram device
 * @devid:		device id of the pmc device
 * @base_addr:		contains PWRM base address
 */
struct pmc_ssram_telemetry {
	u16 devid;
	u64 base_addr;
};

int pmc_ssram_telemetry_get_pmc_info(unsigned int pmc_idx,
				     struct pmc_ssram_telemetry *pmc_ssram_telemetry);

#endif /* PMC_SSRAM_H */
