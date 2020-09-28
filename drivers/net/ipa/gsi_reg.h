/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */
#ifndef _GSI_REG_H_
#define _GSI_REG_H_

/* === Only "gsi.c" should include this file === */

#include <linux/bits.h>

/**
 * DOC: GSI Registers
 *
 * GSI registers are located within the "gsi" address space defined by Device
 * Tree.  The offset of each register within that space is specified by
 * symbols defined below.  The GSI address space is mapped to virtual memory
 * space in gsi_init().  All GSI registers are 32 bits wide.
 *
 * Each register type is duplicated for a number of instances of something.
 * For example, each GSI channel has its own set of registers defining its
 * configuration.  The offset to a channel's set of registers is computed
 * based on a "base" offset plus an additional "stride" amount computed
 * from the channel's ID.  For such registers, the offset is computed by a
 * function-like macro that takes a parameter used in the computation.
 *
 * The offset of a register dependent on execution environment is computed
 * by a macro that is supplied a parameter "ee".  The "ee" value is a member
 * of the gsi_ee_id enumerated type.
 *
 * The offset of a channel register is computed by a macro that is supplied a
 * parameter "ch".  The "ch" value is a channel id whose maximum value is 30
 * (though the actual limit is hardware-dependent).
 *
 * The offset of an event register is computed by a macro that is supplied a
 * parameter "ev".  The "ev" value is an event id whose maximum value is 15
 * (though the actual limit is hardware-dependent).
 */

#define GSI_INTER_EE_SRC_CH_IRQ_OFFSET \
			GSI_INTER_EE_N_SRC_CH_IRQ_OFFSET(GSI_EE_AP)
#define GSI_INTER_EE_N_SRC_CH_IRQ_OFFSET(ee) \
			(0x0000c018 + 0x1000 * (ee))

#define GSI_INTER_EE_SRC_EV_CH_IRQ_OFFSET \
			GSI_INTER_EE_N_SRC_EV_CH_IRQ_OFFSET(GSI_EE_AP)
#define GSI_INTER_EE_N_SRC_EV_CH_IRQ_OFFSET(ee) \
			(0x0000c01c + 0x1000 * (ee))

#define GSI_INTER_EE_SRC_CH_IRQ_CLR_OFFSET \
			GSI_INTER_EE_N_SRC_CH_IRQ_CLR_OFFSET(GSI_EE_AP)
#define GSI_INTER_EE_N_SRC_CH_IRQ_CLR_OFFSET(ee) \
			(0x0000c028 + 0x1000 * (ee))

#define GSI_INTER_EE_SRC_EV_CH_IRQ_CLR_OFFSET \
			GSI_INTER_EE_N_SRC_EV_CH_IRQ_CLR_OFFSET(GSI_EE_AP)
#define GSI_INTER_EE_N_SRC_EV_CH_IRQ_CLR_OFFSET(ee) \
			(0x0000c02c + 0x1000 * (ee))

#define GSI_CH_C_CNTXT_0_OFFSET(ch) \
		GSI_EE_N_CH_C_CNTXT_0_OFFSET((ch), GSI_EE_AP)
#define GSI_EE_N_CH_C_CNTXT_0_OFFSET(ch, ee) \
		(0x0001c000 + 0x4000 * (ee) + 0x80 * (ch))
#define CHTYPE_PROTOCOL_FMASK		GENMASK(2, 0)
#define CHTYPE_DIR_FMASK		GENMASK(3, 3)
#define EE_FMASK			GENMASK(7, 4)
#define CHID_FMASK			GENMASK(12, 8)
/* The next field is present for GSI v2.0 and above */
#define CHTYPE_PROTOCOL_MSB_FMASK	GENMASK(13, 13)
#define ERINDEX_FMASK			GENMASK(18, 14)
#define CHSTATE_FMASK			GENMASK(23, 20)
#define ELEMENT_SIZE_FMASK		GENMASK(31, 24)

#define GSI_CH_C_CNTXT_1_OFFSET(ch) \
		GSI_EE_N_CH_C_CNTXT_1_OFFSET((ch), GSI_EE_AP)
#define GSI_EE_N_CH_C_CNTXT_1_OFFSET(ch, ee) \
		(0x0001c004 + 0x4000 * (ee) + 0x80 * (ch))
#define R_LENGTH_FMASK			GENMASK(15, 0)

#define GSI_CH_C_CNTXT_2_OFFSET(ch) \
		GSI_EE_N_CH_C_CNTXT_2_OFFSET((ch), GSI_EE_AP)
#define GSI_EE_N_CH_C_CNTXT_2_OFFSET(ch, ee) \
		(0x0001c008 + 0x4000 * (ee) + 0x80 * (ch))

#define GSI_CH_C_CNTXT_3_OFFSET(ch) \
		GSI_EE_N_CH_C_CNTXT_3_OFFSET((ch), GSI_EE_AP)
#define GSI_EE_N_CH_C_CNTXT_3_OFFSET(ch, ee) \
		(0x0001c00c + 0x4000 * (ee) + 0x80 * (ch))

#define GSI_CH_C_QOS_OFFSET(ch) \
		GSI_EE_N_CH_C_QOS_OFFSET((ch), GSI_EE_AP)
#define GSI_EE_N_CH_C_QOS_OFFSET(ch, ee) \
		(0x0001c05c + 0x4000 * (ee) + 0x80 * (ch))
#define WRR_WEIGHT_FMASK		GENMASK(3, 0)
#define MAX_PREFETCH_FMASK		GENMASK(8, 8)
#define USE_DB_ENG_FMASK		GENMASK(9, 9)
/* The next field is present for GSI v2.0 and above */
#define USE_ESCAPE_BUF_ONLY_FMASK	GENMASK(10, 10)

#define GSI_CH_C_SCRATCH_0_OFFSET(ch) \
		GSI_EE_N_CH_C_SCRATCH_0_OFFSET((ch), GSI_EE_AP)
#define GSI_EE_N_CH_C_SCRATCH_0_OFFSET(ch, ee) \
		(0x0001c060 + 0x4000 * (ee) + 0x80 * (ch))

#define GSI_CH_C_SCRATCH_1_OFFSET(ch) \
		GSI_EE_N_CH_C_SCRATCH_1_OFFSET((ch), GSI_EE_AP)
#define GSI_EE_N_CH_C_SCRATCH_1_OFFSET(ch, ee) \
		(0x0001c064 + 0x4000 * (ee) + 0x80 * (ch))

#define GSI_CH_C_SCRATCH_2_OFFSET(ch) \
		GSI_EE_N_CH_C_SCRATCH_2_OFFSET((ch), GSI_EE_AP)
#define GSI_EE_N_CH_C_SCRATCH_2_OFFSET(ch, ee) \
		(0x0001c068 + 0x4000 * (ee) + 0x80 * (ch))

#define GSI_CH_C_SCRATCH_3_OFFSET(ch) \
		GSI_EE_N_CH_C_SCRATCH_3_OFFSET((ch), GSI_EE_AP)
#define GSI_EE_N_CH_C_SCRATCH_3_OFFSET(ch, ee) \
		(0x0001c06c + 0x4000 * (ee) + 0x80 * (ch))

#define GSI_EV_CH_E_CNTXT_0_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_0_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_0_OFFSET(ev, ee) \
		(0x0001d000 + 0x4000 * (ee) + 0x80 * (ev))
#define EV_CHTYPE_FMASK			GENMASK(3, 0)
#define EV_EE_FMASK			GENMASK(7, 4)
#define EV_EVCHID_FMASK			GENMASK(15, 8)
#define EV_INTYPE_FMASK			GENMASK(16, 16)
#define EV_CHSTATE_FMASK		GENMASK(23, 20)
#define EV_ELEMENT_SIZE_FMASK		GENMASK(31, 24)

#define GSI_EV_CH_E_CNTXT_1_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_1_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_1_OFFSET(ev, ee) \
		(0x0001d004 + 0x4000 * (ee) + 0x80 * (ev))
#define EV_R_LENGTH_FMASK		GENMASK(15, 0)

#define GSI_EV_CH_E_CNTXT_2_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_2_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_2_OFFSET(ev, ee) \
		(0x0001d008 + 0x4000 * (ee) + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_3_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_3_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_3_OFFSET(ev, ee) \
		(0x0001d00c + 0x4000 * (ee) + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_4_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_4_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_4_OFFSET(ev, ee) \
		(0x0001d010 + 0x4000 * (ee) + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_8_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_8_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_8_OFFSET(ev, ee) \
		(0x0001d020 + 0x4000 * (ee) + 0x80 * (ev))
#define MODT_FMASK			GENMASK(15, 0)
#define MODC_FMASK			GENMASK(23, 16)
#define MOD_CNT_FMASK			GENMASK(31, 24)

#define GSI_EV_CH_E_CNTXT_9_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_9_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_9_OFFSET(ev, ee) \
		(0x0001d024 + 0x4000 * (ee) + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_10_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_10_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_10_OFFSET(ev, ee) \
		(0x0001d028 + 0x4000 * (ee) + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_11_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_11_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_11_OFFSET(ev, ee) \
		(0x0001d02c + 0x4000 * (ee) + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_12_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_12_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_12_OFFSET(ev, ee) \
		(0x0001d030 + 0x4000 * (ee) + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_13_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_CNTXT_13_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_CNTXT_13_OFFSET(ev, ee) \
		(0x0001d034 + 0x4000 * (ee) + 0x80 * (ev))

#define GSI_EV_CH_E_SCRATCH_0_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_SCRATCH_0_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_SCRATCH_0_OFFSET(ev, ee) \
		(0x0001d048 + 0x4000 * (ee) + 0x80 * (ev))

#define GSI_EV_CH_E_SCRATCH_1_OFFSET(ev) \
		GSI_EE_N_EV_CH_E_SCRATCH_1_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_SCRATCH_1_OFFSET(ev, ee) \
		(0x0001d04c + 0x4000 * (ee) + 0x80 * (ev))

#define GSI_CH_C_DOORBELL_0_OFFSET(ch) \
		GSI_EE_N_CH_C_DOORBELL_0_OFFSET((ch), GSI_EE_AP)
#define GSI_EE_N_CH_C_DOORBELL_0_OFFSET(ch, ee) \
			(0x0001e000 + 0x4000 * (ee) + 0x08 * (ch))

#define GSI_EV_CH_E_DOORBELL_0_OFFSET(ev) \
			GSI_EE_N_EV_CH_E_DOORBELL_0_OFFSET((ev), GSI_EE_AP)
#define GSI_EE_N_EV_CH_E_DOORBELL_0_OFFSET(ev, ee) \
			(0x0001e100 + 0x4000 * (ee) + 0x08 * (ev))

#define GSI_GSI_STATUS_OFFSET \
			GSI_EE_N_GSI_STATUS_OFFSET(GSI_EE_AP)
#define GSI_EE_N_GSI_STATUS_OFFSET(ee) \
			(0x0001f000 + 0x4000 * (ee))
#define ENABLED_FMASK			GENMASK(0, 0)

#define GSI_CH_CMD_OFFSET \
			GSI_EE_N_CH_CMD_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CH_CMD_OFFSET(ee) \
			(0x0001f008 + 0x4000 * (ee))
#define CH_CHID_FMASK			GENMASK(7, 0)
#define CH_OPCODE_FMASK			GENMASK(31, 24)

#define GSI_EV_CH_CMD_OFFSET \
			GSI_EE_N_EV_CH_CMD_OFFSET(GSI_EE_AP)
#define GSI_EE_N_EV_CH_CMD_OFFSET(ee) \
			(0x0001f010 + 0x4000 * (ee))
#define EV_CHID_FMASK			GENMASK(7, 0)
#define EV_OPCODE_FMASK			GENMASK(31, 24)

#define GSI_GENERIC_CMD_OFFSET \
			GSI_EE_N_GENERIC_CMD_OFFSET(GSI_EE_AP)
#define GSI_EE_N_GENERIC_CMD_OFFSET(ee) \
			(0x0001f018 + 0x4000 * (ee))
#define GENERIC_OPCODE_FMASK		GENMASK(4, 0)
#define GENERIC_CHID_FMASK		GENMASK(9, 5)
#define GENERIC_EE_FMASK		GENMASK(13, 10)

#define GSI_GSI_HW_PARAM_2_OFFSET \
			GSI_EE_N_GSI_HW_PARAM_2_OFFSET(GSI_EE_AP)
#define GSI_EE_N_GSI_HW_PARAM_2_OFFSET(ee) \
			(0x0001f040 + 0x4000 * (ee))
#define IRAM_SIZE_FMASK			GENMASK(2, 0)
#define IRAM_SIZE_ONE_KB_FVAL			0
#define IRAM_SIZE_TWO_KB_FVAL			1
/* The next two values are available for GSI v2.0 and above */
#define IRAM_SIZE_TWO_N_HALF_KB_FVAL		2
#define IRAM_SIZE_THREE_KB_FVAL			3
#define NUM_CH_PER_EE_FMASK		GENMASK(7, 3)
#define NUM_EV_PER_EE_FMASK		GENMASK(12, 8)
#define GSI_CH_PEND_TRANSLATE_FMASK	GENMASK(13, 13)
#define GSI_CH_FULL_LOGIC_FMASK		GENMASK(14, 14)
/* Fields below are present for GSI v2.0 and above */
#define GSI_USE_SDMA_FMASK		GENMASK(15, 15)
#define GSI_SDMA_N_INT_FMASK		GENMASK(18, 16)
#define GSI_SDMA_MAX_BURST_FMASK	GENMASK(26, 19)
#define GSI_SDMA_N_IOVEC_FMASK		GENMASK(29, 27)
/* Fields below are present for GSI v2.2 and above */
#define GSI_USE_RD_WR_ENG_FMASK		GENMASK(30, 30)
#define GSI_USE_INTER_EE_FMASK		GENMASK(31, 31)

#define GSI_CNTXT_TYPE_IRQ_OFFSET \
			GSI_EE_N_CNTXT_TYPE_IRQ_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_TYPE_IRQ_OFFSET(ee) \
			(0x0001f080 + 0x4000 * (ee))
#define GSI_CNTXT_TYPE_IRQ_MSK_OFFSET \
			GSI_EE_N_CNTXT_TYPE_IRQ_MSK_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_TYPE_IRQ_MSK_OFFSET(ee) \
			(0x0001f088 + 0x4000 * (ee))
/* The masks below are used for the TYPE_IRQ and TYPE_IRQ_MASK registers */
#define CH_CTRL_FMASK			GENMASK(0, 0)
#define EV_CTRL_FMASK			GENMASK(1, 1)
#define GLOB_EE_FMASK			GENMASK(2, 2)
#define IEOB_FMASK			GENMASK(3, 3)
#define INTER_EE_CH_CTRL_FMASK		GENMASK(4, 4)
#define INTER_EE_EV_CTRL_FMASK		GENMASK(5, 5)
#define GENERAL_FMASK			GENMASK(6, 6)
#define GSI_CNTXT_TYPE_IRQ_MSK_ALL	GENMASK(6, 0)

#define GSI_CNTXT_SRC_CH_IRQ_OFFSET \
			GSI_EE_N_CNTXT_SRC_CH_IRQ_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_SRC_CH_IRQ_OFFSET(ee) \
			(0x0001f090 + 0x4000 * (ee))

#define GSI_CNTXT_SRC_EV_CH_IRQ_OFFSET \
			GSI_EE_N_CNTXT_SRC_EV_CH_IRQ_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_SRC_EV_CH_IRQ_OFFSET(ee) \
			(0x0001f094 + 0x4000 * (ee))

#define GSI_CNTXT_SRC_CH_IRQ_MSK_OFFSET \
			GSI_EE_N_CNTXT_SRC_CH_IRQ_MSK_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_SRC_CH_IRQ_MSK_OFFSET(ee) \
			(0x0001f098 + 0x4000 * (ee))

#define GSI_CNTXT_SRC_EV_CH_IRQ_MSK_OFFSET \
			GSI_EE_N_CNTXT_SRC_EV_CH_IRQ_MSK_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_SRC_EV_CH_IRQ_MSK_OFFSET(ee) \
			(0x0001f09c + 0x4000 * (ee))

#define GSI_CNTXT_SRC_CH_IRQ_CLR_OFFSET \
			GSI_EE_N_CNTXT_SRC_CH_IRQ_CLR_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_SRC_CH_IRQ_CLR_OFFSET(ee) \
			(0x0001f0a0 + 0x4000 * (ee))

#define GSI_CNTXT_SRC_EV_CH_IRQ_CLR_OFFSET \
			GSI_EE_N_CNTXT_SRC_EV_CH_IRQ_CLR_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_SRC_EV_CH_IRQ_CLR_OFFSET(ee) \
			(0x0001f0a4 + 0x4000 * (ee))

#define GSI_CNTXT_SRC_IEOB_IRQ_OFFSET \
			GSI_EE_N_CNTXT_SRC_IEOB_IRQ_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_SRC_IEOB_IRQ_OFFSET(ee) \
			(0x0001f0b0 + 0x4000 * (ee))

#define GSI_CNTXT_SRC_IEOB_IRQ_MSK_OFFSET \
			GSI_EE_N_CNTXT_SRC_IEOB_IRQ_MSK_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_SRC_IEOB_IRQ_MSK_OFFSET(ee) \
			(0x0001f0b8 + 0x4000 * (ee))

#define GSI_CNTXT_SRC_IEOB_IRQ_CLR_OFFSET \
			GSI_EE_N_CNTXT_SRC_IEOB_IRQ_CLR_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_SRC_IEOB_IRQ_CLR_OFFSET(ee) \
			(0x0001f0c0 + 0x4000 * (ee))

#define GSI_CNTXT_GLOB_IRQ_STTS_OFFSET \
			GSI_EE_N_CNTXT_GLOB_IRQ_STTS_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_GLOB_IRQ_STTS_OFFSET(ee) \
			(0x0001f100 + 0x4000 * (ee))
#define GSI_CNTXT_GLOB_IRQ_EN_OFFSET \
			GSI_EE_N_CNTXT_GLOB_IRQ_EN_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_GLOB_IRQ_EN_OFFSET(ee) \
			(0x0001f108 + 0x4000 * (ee))
#define GSI_CNTXT_GLOB_IRQ_CLR_OFFSET \
			GSI_EE_N_CNTXT_GLOB_IRQ_CLR_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_GLOB_IRQ_CLR_OFFSET(ee) \
			(0x0001f110 + 0x4000 * (ee))
/* The masks below are used for the general IRQ STTS, EN, and CLR registers */
#define ERROR_INT_FMASK			GENMASK(0, 0)
#define GP_INT1_FMASK			GENMASK(1, 1)
#define GP_INT2_FMASK			GENMASK(2, 2)
#define GP_INT3_FMASK			GENMASK(3, 3)
#define GSI_CNTXT_GLOB_IRQ_ALL		GENMASK(3, 0)

#define GSI_CNTXT_GSI_IRQ_STTS_OFFSET \
			GSI_EE_N_CNTXT_GSI_IRQ_STTS_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_GSI_IRQ_STTS_OFFSET(ee) \
			(0x0001f118 + 0x4000 * (ee))
#define BREAK_POINT_FMASK		GENMASK(0, 0)
#define BUS_ERROR_FMASK			GENMASK(1, 1)
#define CMD_FIFO_OVRFLOW_FMASK		GENMASK(2, 2)
#define MCS_STACK_OVRFLOW_FMASK		GENMASK(3, 3)

#define GSI_CNTXT_GSI_IRQ_EN_OFFSET \
			GSI_EE_N_CNTXT_GSI_IRQ_EN_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_GSI_IRQ_EN_OFFSET(ee) \
			(0x0001f120 + 0x4000 * (ee))
#define EN_BREAK_POINT_FMASK		GENMASK(0, 0)
#define EN_BUS_ERROR_FMASK		GENMASK(1, 1)
#define EN_CMD_FIFO_OVRFLOW_FMASK	GENMASK(2, 2)
#define EN_MCS_STACK_OVRFLOW_FMASK	GENMASK(3, 3)
#define GSI_CNTXT_GSI_IRQ_ALL		GENMASK(3, 0)

#define GSI_CNTXT_GSI_IRQ_CLR_OFFSET \
			GSI_EE_N_CNTXT_GSI_IRQ_CLR_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_GSI_IRQ_CLR_OFFSET(ee) \
			(0x0001f128 + 0x4000 * (ee))
#define CLR_BREAK_POINT_FMASK		GENMASK(0, 0)
#define CLR_BUS_ERROR_FMASK		GENMASK(1, 1)
#define CLR_CMD_FIFO_OVRFLOW_FMASK	GENMASK(2, 2)
#define CLR_MCS_STACK_OVRFLOW_FMASK	GENMASK(3, 3)

#define GSI_CNTXT_INTSET_OFFSET \
			GSI_EE_N_CNTXT_INTSET_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_INTSET_OFFSET(ee) \
			(0x0001f180 + 0x4000 * (ee))
#define INTYPE_FMASK			GENMASK(0, 0)

#define GSI_ERROR_LOG_OFFSET \
			GSI_EE_N_ERROR_LOG_OFFSET(GSI_EE_AP)
#define GSI_EE_N_ERROR_LOG_OFFSET(ee) \
			(0x0001f200 + 0x4000 * (ee))
#define ERR_ARG3_FMASK			GENMASK(3, 0)
#define ERR_ARG2_FMASK			GENMASK(7, 4)
#define ERR_ARG1_FMASK			GENMASK(11, 8)
#define ERR_CODE_FMASK			GENMASK(15, 12)
#define ERR_VIRT_IDX_FMASK		GENMASK(23, 19)
#define ERR_TYPE_FMASK			GENMASK(27, 24)
#define ERR_EE_FMASK			GENMASK(31, 28)

#define GSI_ERROR_LOG_CLR_OFFSET \
			GSI_EE_N_ERROR_LOG_CLR_OFFSET(GSI_EE_AP)
#define GSI_EE_N_ERROR_LOG_CLR_OFFSET(ee) \
			(0x0001f210 + 0x4000 * (ee))

#define GSI_CNTXT_SCRATCH_0_OFFSET \
			GSI_EE_N_CNTXT_SCRATCH_0_OFFSET(GSI_EE_AP)
#define GSI_EE_N_CNTXT_SCRATCH_0_OFFSET(ee) \
			(0x0001f400 + 0x4000 * (ee))
#define INTER_EE_RESULT_FMASK		GENMASK(2, 0)
#define GENERIC_EE_RESULT_FMASK		GENMASK(7, 5)
#define GENERIC_EE_SUCCESS_FVAL			1
#define GENERIC_EE_INCORRECT_DIRECTION_FVAL	3
#define GENERIC_EE_INCORRECT_CHANNEL_FVAL	5
#define GENERIC_EE_NO_RESOURCES_FVAL		7
#define USB_MAX_PACKET_FMASK		GENMASK(15, 15)	/* 0: HS; 1: SS */
#define MHI_BASE_CHANNEL_FMASK		GENMASK(31, 24)

#endif	/* _GSI_REG_H_ */
