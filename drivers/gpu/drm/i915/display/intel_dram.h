/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_DRAM_H__
#define __INTEL_DRAM_H__

#include <linux/types.h>

struct intel_display;

struct dram_info {
	enum intel_dram_type {
		INTEL_DRAM_UNKNOWN,
		INTEL_DRAM_DDR2,
		INTEL_DRAM_DDR3,
		INTEL_DRAM_DDR4,
		INTEL_DRAM_LPDDR3,
		INTEL_DRAM_LPDDR4,
		INTEL_DRAM_DDR5,
		INTEL_DRAM_LPDDR5,
		INTEL_DRAM_GDDR,
		INTEL_DRAM_GDDR_ECC,
		__INTEL_DRAM_TYPE_MAX,
	} type;
	unsigned int fsb_freq;
	unsigned int mem_freq;
	u8 num_channels;
	u8 num_qgv_points;
	u8 num_psf_gv_points;
	bool ecc_impacting_de_bw; /* Only valid from Xe3p_LPD onward. */
	bool symmetric_memory;
	bool has_16gb_dimms;
};

int intel_dram_detect(struct intel_display *display);
unsigned int intel_fsb_freq(struct intel_display *display);
unsigned int intel_mem_freq(struct intel_display *display);
const struct dram_info *intel_dram_info(struct intel_display *display);
const char *intel_dram_type_str(enum intel_dram_type type);

#endif /* __INTEL_DRAM_H__ */
