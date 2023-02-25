/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

/* Internal to xe_pcode */

#include "regs/xe_reg_defs.h"

#define PCODE_MAILBOX			_MMIO(0x138124)
#define   PCODE_READY			REG_BIT(31)
#define   PCODE_MB_PARAM2		REG_GENMASK(23, 16)
#define   PCODE_MB_PARAM1		REG_GENMASK(15, 8)
#define   PCODE_MB_COMMAND		REG_GENMASK(7, 0)
#define   PCODE_ERROR_MASK		0xFF
#define     PCODE_SUCCESS		0x0
#define     PCODE_ILLEGAL_CMD		0x1
#define     PCODE_TIMEOUT		0x2
#define     PCODE_ILLEGAL_DATA		0x3
#define     PCODE_ILLEGAL_SUBCOMMAND	0x4
#define     PCODE_LOCKED		0x6
#define     PCODE_GT_RATIO_OUT_OF_RANGE	0x10
#define     PCODE_REJECTED		0x11

#define PCODE_DATA0			_MMIO(0x138128)
#define PCODE_DATA1			_MMIO(0x13812C)

/* Min Freq QOS Table */
#define   PCODE_WRITE_MIN_FREQ_TABLE	0x8
#define   PCODE_READ_MIN_FREQ_TABLE	0x9
#define   PCODE_FREQ_RING_RATIO_SHIFT	16

/* PCODE Init */
#define   DGFX_PCODE_STATUS		0x7E
#define     DGFX_GET_INIT_STATUS	0x0
#define     DGFX_INIT_STATUS_COMPLETE	0x1

struct pcode_err_decode {
	int errno;
	const char *str;
};

