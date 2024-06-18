/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

/* Internal to xe_pcode */

#include "regs/xe_reg_defs.h"

#define PCODE_MAILBOX			XE_REG(0x138124)
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

#define PCODE_DATA0			XE_REG(0x138128)
#define PCODE_DATA1			XE_REG(0x13812C)

/* Min Freq QOS Table */
#define   PCODE_WRITE_MIN_FREQ_TABLE	0x8
#define   PCODE_READ_MIN_FREQ_TABLE	0x9
#define   PCODE_FREQ_RING_RATIO_SHIFT	16

/* PCODE Init */
#define   DGFX_PCODE_STATUS		0x7E
#define     DGFX_GET_INIT_STATUS	0x0
#define     DGFX_INIT_STATUS_COMPLETE	0x1

#define   PCODE_POWER_SETUP			0x7C
#define     POWER_SETUP_SUBCOMMAND_READ_I1	0x4
#define     POWER_SETUP_SUBCOMMAND_WRITE_I1	0x5
#define	    POWER_SETUP_I1_WATTS		REG_BIT(31)
#define	    POWER_SETUP_I1_SHIFT		6	/* 10.6 fixed point format */
#define	    POWER_SETUP_I1_DATA_MASK		REG_GENMASK(15, 0)

#define   PCODE_FREQUENCY_CONFIG		0x6e
/* Frequency Config Sub Commands (param1) */
#define     PCODE_MBOX_FC_SC_READ_FUSED_P0	0x0
#define     PCODE_MBOX_FC_SC_READ_FUSED_PN	0x1
/* Domain IDs (param2) */
#define     PCODE_MBOX_DOMAIN_HBM		0x2

struct pcode_err_decode {
	int errno;
	const char *str;
};

