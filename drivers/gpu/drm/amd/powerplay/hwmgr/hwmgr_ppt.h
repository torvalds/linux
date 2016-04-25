/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef PP_HWMGR_PPT_H
#define PP_HWMGR_PPT_H

#include "hardwaremanager.h"
#include "smumgr.h"
#include "atom-types.h"

struct phm_ppt_v1_clock_voltage_dependency_record {
	uint32_t clk;
	uint8_t vddInd;
	uint16_t vdd_offset;
	uint16_t vddc;
	uint16_t vddgfx;
	uint16_t vddci;
	uint16_t mvdd;
	uint8_t phases;
	uint8_t cks_enable;
	uint8_t cks_voffset;
};

typedef struct phm_ppt_v1_clock_voltage_dependency_record phm_ppt_v1_clock_voltage_dependency_record;

struct phm_ppt_v1_clock_voltage_dependency_table {
	uint32_t count;                                            /* Number of entries. */
	phm_ppt_v1_clock_voltage_dependency_record entries[1];     /* Dynamically allocate count entries. */
};

typedef struct phm_ppt_v1_clock_voltage_dependency_table phm_ppt_v1_clock_voltage_dependency_table;


/* Multimedia Clock Voltage Dependency records and table */
struct phm_ppt_v1_mm_clock_voltage_dependency_record {
	uint32_t  dclk;                                              /* UVD D-clock */
	uint32_t  vclk;                                              /* UVD V-clock */
	uint32_t  eclk;                                              /* VCE clock */
	uint32_t  aclk;                                              /* ACP clock */
	uint32_t  samclock;                                          /* SAMU clock */
	uint8_t	vddcInd;
	uint16_t vddgfx_offset;
	uint16_t vddc;
	uint16_t vddgfx;
	uint8_t phases;
};
typedef struct phm_ppt_v1_mm_clock_voltage_dependency_record phm_ppt_v1_mm_clock_voltage_dependency_record;

struct phm_ppt_v1_mm_clock_voltage_dependency_table {
	uint32_t count;													/* Number of entries. */
	phm_ppt_v1_mm_clock_voltage_dependency_record entries[1];		/* Dynamically allocate count entries. */
};
typedef struct phm_ppt_v1_mm_clock_voltage_dependency_table phm_ppt_v1_mm_clock_voltage_dependency_table;

struct phm_ppt_v1_voltage_lookup_record {
	uint16_t us_calculated;
	uint16_t us_vdd;												/* Base voltage */
	uint16_t us_cac_low;
	uint16_t us_cac_mid;
	uint16_t us_cac_high;
};
typedef struct phm_ppt_v1_voltage_lookup_record phm_ppt_v1_voltage_lookup_record;

struct phm_ppt_v1_voltage_lookup_table {
	uint32_t count;
	phm_ppt_v1_voltage_lookup_record entries[1];    /* Dynamically allocate count entries. */
};
typedef struct phm_ppt_v1_voltage_lookup_table phm_ppt_v1_voltage_lookup_table;

/* PCIE records and Table */

struct phm_ppt_v1_pcie_record {
	uint8_t gen_speed;
	uint8_t lane_width;
};
typedef struct phm_ppt_v1_pcie_record phm_ppt_v1_pcie_record;

struct phm_ppt_v1_pcie_table {
	uint32_t count;                                            /* Number of entries. */
	phm_ppt_v1_pcie_record entries[1];                         /* Dynamically allocate count entries. */
};
typedef struct phm_ppt_v1_pcie_table phm_ppt_v1_pcie_table;

#endif

