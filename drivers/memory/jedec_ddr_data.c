// SPDX-License-Identifier: GPL-2.0-only
/*
 * DDR addressing details and AC timing parameters from JEDEC specs
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Aneesh V <aneesh@ti.com>
 */

#include <linux/export.h>

#include "jedec_ddr.h"

/* LPDDR2 addressing details from JESD209-2 section 2.4 */
const struct lpddr2_addressing
	lpddr2_jedec_addressing_table[NUM_DDR_ADDR_TABLE_ENTRIES] = {
	{B4, T_REFI_15_6, T_RFC_90}, /* 64M */
	{B4, T_REFI_15_6, T_RFC_90}, /* 128M */
	{B4, T_REFI_7_8,  T_RFC_90}, /* 256M */
	{B4, T_REFI_7_8,  T_RFC_90}, /* 512M */
	{B8, T_REFI_7_8, T_RFC_130}, /* 1GS4 */
	{B8, T_REFI_3_9, T_RFC_130}, /* 2GS4 */
	{B8, T_REFI_3_9, T_RFC_130}, /* 4G */
	{B8, T_REFI_3_9, T_RFC_210}, /* 8G */
	{B4, T_REFI_7_8, T_RFC_130}, /* 1GS2 */
	{B4, T_REFI_3_9, T_RFC_130}, /* 2GS2 */
};
EXPORT_SYMBOL_GPL(lpddr2_jedec_addressing_table);

/* LPDDR2 AC timing parameters from JESD209-2 section 12 */
const struct lpddr2_timings
	lpddr2_jedec_timings[NUM_DDR_TIMING_TABLE_ENTRIES] = {
	/* Speed bin 400(200 MHz) */
	[0] = {
		.max_freq	= 200000000,
		.min_freq	= 10000000,
		.tRPab		= 21000,
		.tRCD		= 18000,
		.tWR		= 15000,
		.tRAS_min	= 42000,
		.tRRD		= 10000,
		.tWTR		= 10000,
		.tXP		= 7500,
		.tRTP		= 7500,
		.tCKESR		= 15000,
		.tDQSCK_max	= 5500,
		.tFAW		= 50000,
		.tZQCS		= 90000,
		.tZQCL		= 360000,
		.tZQinit	= 1000000,
		.tRAS_max_ns	= 70000,
		.tDQSCK_max_derated = 6000,
	},
	/* Speed bin 533(266 MHz) */
	[1] = {
		.max_freq	= 266666666,
		.min_freq	= 10000000,
		.tRPab		= 21000,
		.tRCD		= 18000,
		.tWR		= 15000,
		.tRAS_min	= 42000,
		.tRRD		= 10000,
		.tWTR		= 7500,
		.tXP		= 7500,
		.tRTP		= 7500,
		.tCKESR		= 15000,
		.tDQSCK_max	= 5500,
		.tFAW		= 50000,
		.tZQCS		= 90000,
		.tZQCL		= 360000,
		.tZQinit	= 1000000,
		.tRAS_max_ns	= 70000,
		.tDQSCK_max_derated = 6000,
	},
	/* Speed bin 800(400 MHz) */
	[2] = {
		.max_freq	= 400000000,
		.min_freq	= 10000000,
		.tRPab		= 21000,
		.tRCD		= 18000,
		.tWR		= 15000,
		.tRAS_min	= 42000,
		.tRRD		= 10000,
		.tWTR		= 7500,
		.tXP		= 7500,
		.tRTP		= 7500,
		.tCKESR		= 15000,
		.tDQSCK_max	= 5500,
		.tFAW		= 50000,
		.tZQCS		= 90000,
		.tZQCL		= 360000,
		.tZQinit	= 1000000,
		.tRAS_max_ns	= 70000,
		.tDQSCK_max_derated = 6000,
	},
	/* Speed bin 1066(533 MHz) */
	[3] = {
		.max_freq	= 533333333,
		.min_freq	= 10000000,
		.tRPab		= 21000,
		.tRCD		= 18000,
		.tWR		= 15000,
		.tRAS_min	= 42000,
		.tRRD		= 10000,
		.tWTR		= 7500,
		.tXP		= 7500,
		.tRTP		= 7500,
		.tCKESR		= 15000,
		.tDQSCK_max	= 5500,
		.tFAW		= 50000,
		.tZQCS		= 90000,
		.tZQCL		= 360000,
		.tZQinit	= 1000000,
		.tRAS_max_ns	= 70000,
		.tDQSCK_max_derated = 5620,
	},
};
EXPORT_SYMBOL_GPL(lpddr2_jedec_timings);

const struct lpddr2_min_tck lpddr2_jedec_min_tck = {
	.tRPab		= 3,
	.tRCD		= 3,
	.tWR		= 3,
	.tRASmin	= 3,
	.tRRD		= 2,
	.tWTR		= 2,
	.tXP		= 2,
	.tRTP		= 2,
	.tCKE		= 3,
	.tCKESR		= 3,
	.tFAW		= 8
};
EXPORT_SYMBOL_GPL(lpddr2_jedec_min_tck);
