/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2023 Linaro Ltd.
 */
#ifndef _GSI_REG_H_
#define _GSI_REG_H_

/* === Only "gsi.c" and "gsi_reg.c" should include this file === */

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

/* enum gsi_reg_id - GSI register IDs */
enum gsi_reg_id {
	INTER_EE_SRC_CH_IRQ_MSK,			/* IPA v3.5+ */
	INTER_EE_SRC_EV_CH_IRQ_MSK,			/* IPA v3.5+ */
	CH_C_CNTXT_0,
	CH_C_CNTXT_1,
	CH_C_CNTXT_2,
	CH_C_CNTXT_3,
	CH_C_QOS,
	CH_C_SCRATCH_0,
	CH_C_SCRATCH_1,
	CH_C_SCRATCH_2,
	CH_C_SCRATCH_3,
	EV_CH_E_CNTXT_0,
	EV_CH_E_CNTXT_1,
	EV_CH_E_CNTXT_2,
	EV_CH_E_CNTXT_3,
	EV_CH_E_CNTXT_4,
	EV_CH_E_CNTXT_8,
	EV_CH_E_CNTXT_9,
	EV_CH_E_CNTXT_10,
	EV_CH_E_CNTXT_11,
	EV_CH_E_CNTXT_12,
	EV_CH_E_CNTXT_13,
	EV_CH_E_SCRATCH_0,
	EV_CH_E_SCRATCH_1,
	CH_C_DOORBELL_0,
	EV_CH_E_DOORBELL_0,
	GSI_STATUS,
	CH_CMD,
	EV_CH_CMD,
	GENERIC_CMD,
	HW_PARAM_2,					/* IPA v3.5.1+ */
	CNTXT_TYPE_IRQ,
	CNTXT_TYPE_IRQ_MSK,
	CNTXT_SRC_CH_IRQ,
	CNTXT_SRC_CH_IRQ_MSK,
	CNTXT_SRC_CH_IRQ_CLR,
	CNTXT_SRC_EV_CH_IRQ,
	CNTXT_SRC_EV_CH_IRQ_MSK,
	CNTXT_SRC_EV_CH_IRQ_CLR,
	CNTXT_SRC_IEOB_IRQ,
	CNTXT_SRC_IEOB_IRQ_MSK,
	CNTXT_SRC_IEOB_IRQ_CLR,
	CNTXT_GLOB_IRQ_STTS,
	CNTXT_GLOB_IRQ_EN,
	CNTXT_GLOB_IRQ_CLR,
	CNTXT_GSI_IRQ_STTS,
	CNTXT_GSI_IRQ_EN,
	CNTXT_GSI_IRQ_CLR,
	CNTXT_INTSET,
	ERROR_LOG,
	ERROR_LOG_CLR,
	CNTXT_SCRATCH_0,
	GSI_REG_ID_COUNT,				/* Last; not an ID */
};

/* The inter-EE IRQ registers are relative to gsi->virt_raw (IPA v3.5+) */

#define GSI_INTER_EE_SRC_CH_IRQ_MSK_OFFSET \
			(0x0000c020 + 0x1000 * GSI_EE_AP)

#define GSI_INTER_EE_SRC_EV_CH_IRQ_MSK_OFFSET \
			(0x0000c024 + 0x1000 * GSI_EE_AP)

/* All other register offsets are relative to gsi->virt */

/* CH_C_CNTXT_0 register */
#define CHTYPE_PROTOCOL_FMASK		GENMASK(2, 0)
#define CHTYPE_DIR_FMASK		GENMASK(3, 3)
#define EE_FMASK			GENMASK(7, 4)
#define CHID_FMASK			GENMASK(12, 8)
/* The next field is present for IPA v4.5 and above */
#define CHTYPE_PROTOCOL_MSB_FMASK	GENMASK(13, 13)
#define ERINDEX_FMASK			GENMASK(18, 14)
#define CHSTATE_FMASK			GENMASK(23, 20)
#define ELEMENT_SIZE_FMASK		GENMASK(31, 24)

/** enum gsi_channel_type - CHTYPE_PROTOCOL field values in CH_C_CNTXT_0 */
enum gsi_channel_type {
	GSI_CHANNEL_TYPE_MHI			= 0x0,
	GSI_CHANNEL_TYPE_XHCI			= 0x1,
	GSI_CHANNEL_TYPE_GPI			= 0x2,
	GSI_CHANNEL_TYPE_XDCI			= 0x3,
	GSI_CHANNEL_TYPE_WDI2			= 0x4,
	GSI_CHANNEL_TYPE_GCI			= 0x5,
	GSI_CHANNEL_TYPE_WDI3			= 0x6,
	GSI_CHANNEL_TYPE_MHIP			= 0x7,
	GSI_CHANNEL_TYPE_AQC			= 0x8,
	GSI_CHANNEL_TYPE_11AD			= 0x9,
};

/* CH_C_QOS register */
#define WRR_WEIGHT_FMASK		GENMASK(3, 0)
#define MAX_PREFETCH_FMASK		GENMASK(8, 8)
#define USE_DB_ENG_FMASK		GENMASK(9, 9)
/* The next field is only present for IPA v4.0, v4.1, and v4.2 */
#define USE_ESCAPE_BUF_ONLY_FMASK	GENMASK(10, 10)
/* The next two fields are present for IPA v4.5 and above */
#define PREFETCH_MODE_FMASK		GENMASK(13, 10)
#define EMPTY_LVL_THRSHOLD_FMASK	GENMASK(23, 16)
/* The next field is present for IPA v4.9 and above */
#define DB_IN_BYTES			GENMASK(24, 24)

/** enum gsi_prefetch_mode - PREFETCH_MODE field in CH_C_QOS */
enum gsi_prefetch_mode {
	GSI_USE_PREFETCH_BUFS			= 0x0,
	GSI_ESCAPE_BUF_ONLY			= 0x1,
	GSI_SMART_PREFETCH			= 0x2,
	GSI_FREE_PREFETCH			= 0x3,
};

#define GSI_EV_CH_E_CNTXT_0_OFFSET(ev) \
			(0x0001d000 + 0x4000 * GSI_EE_AP + 0x80 * (ev))
/* enum gsi_channel_type defines EV_CHTYPE field values in EV_CH_E_CNTXT_0 */
#define EV_CHTYPE_FMASK			GENMASK(3, 0)
#define EV_EE_FMASK			GENMASK(7, 4)
#define EV_EVCHID_FMASK			GENMASK(15, 8)
#define EV_INTYPE_FMASK			GENMASK(16, 16)
#define EV_CHSTATE_FMASK		GENMASK(23, 20)
#define EV_ELEMENT_SIZE_FMASK		GENMASK(31, 24)

#define GSI_EV_CH_E_CNTXT_1_OFFSET(ev) \
			(0x0001d004 + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_2_OFFSET(ev) \
			(0x0001d008 + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_3_OFFSET(ev) \
			(0x0001d00c + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_4_OFFSET(ev) \
			(0x0001d010 + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_8_OFFSET(ev) \
			(0x0001d020 + 0x4000 * GSI_EE_AP + 0x80 * (ev))
#define MODT_FMASK			GENMASK(15, 0)
#define MODC_FMASK			GENMASK(23, 16)
#define MOD_CNT_FMASK			GENMASK(31, 24)

#define GSI_EV_CH_E_CNTXT_9_OFFSET(ev) \
			(0x0001d024 + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_10_OFFSET(ev) \
			(0x0001d028 + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_11_OFFSET(ev) \
			(0x0001d02c + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_12_OFFSET(ev) \
			(0x0001d030 + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_CNTXT_13_OFFSET(ev) \
			(0x0001d034 + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_SCRATCH_0_OFFSET(ev) \
			(0x0001d048 + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_SCRATCH_1_OFFSET(ev) \
			(0x0001d04c + 0x4000 * GSI_EE_AP + 0x80 * (ev))

#define GSI_EV_CH_E_DOORBELL_0_OFFSET(ev) \
			(0x0001e100 + 0x4000 * GSI_EE_AP + 0x08 * (ev))

#define GSI_GSI_STATUS_OFFSET \
			(0x0001f000 + 0x4000 * GSI_EE_AP)
#define ENABLED_FMASK			GENMASK(0, 0)

#define GSI_CH_CMD_OFFSET \
			(0x0001f008 + 0x4000 * GSI_EE_AP)
#define CH_CHID_FMASK			GENMASK(7, 0)
#define CH_OPCODE_FMASK			GENMASK(31, 24)

/** enum gsi_ch_cmd_opcode - CH_OPCODE field values in CH_CMD */
enum gsi_ch_cmd_opcode {
	GSI_CH_ALLOCATE				= 0x0,
	GSI_CH_START				= 0x1,
	GSI_CH_STOP				= 0x2,
	GSI_CH_RESET				= 0x9,
	GSI_CH_DE_ALLOC				= 0xa,
	GSI_CH_DB_STOP				= 0xb,
};

#define GSI_EV_CH_CMD_OFFSET \
			(0x0001f010 + 0x4000 * GSI_EE_AP)
#define EV_CHID_FMASK			GENMASK(7, 0)
#define EV_OPCODE_FMASK			GENMASK(31, 24)

/** enum gsi_evt_cmd_opcode - EV_OPCODE field values in EV_CH_CMD */
enum gsi_evt_cmd_opcode {
	GSI_EVT_ALLOCATE			= 0x0,
	GSI_EVT_RESET				= 0x9,
	GSI_EVT_DE_ALLOC			= 0xa,
};

#define GSI_GENERIC_CMD_OFFSET \
			(0x0001f018 + 0x4000 * GSI_EE_AP)
#define GENERIC_OPCODE_FMASK		GENMASK(4, 0)
#define GENERIC_CHID_FMASK		GENMASK(9, 5)
#define GENERIC_EE_FMASK		GENMASK(13, 10)
#define GENERIC_PARAMS_FMASK		GENMASK(31, 24)	/* IPA v4.11+ */

/** enum gsi_generic_cmd_opcode - GENERIC_OPCODE field values in GENERIC_CMD */
enum gsi_generic_cmd_opcode {
	GSI_GENERIC_HALT_CHANNEL		= 0x1,
	GSI_GENERIC_ALLOCATE_CHANNEL		= 0x2,
	GSI_GENERIC_ENABLE_FLOW_CONTROL		= 0x3,	/* IPA v4.2+ */
	GSI_GENERIC_DISABLE_FLOW_CONTROL	= 0x4,	/* IPA v4.2+ */
	GSI_GENERIC_QUERY_FLOW_CONTROL		= 0x5,	/* IPA v4.11+ */
};

/* The next register is present for IPA v3.5.1 and above */
#define GSI_GSI_HW_PARAM_2_OFFSET \
			(0x0001f040 + 0x4000 * GSI_EE_AP)
#define IRAM_SIZE_FMASK			GENMASK(2, 0)
#define NUM_CH_PER_EE_FMASK		GENMASK(7, 3)
#define NUM_EV_PER_EE_FMASK		GENMASK(12, 8)
#define GSI_CH_PEND_TRANSLATE_FMASK	GENMASK(13, 13)
#define GSI_CH_FULL_LOGIC_FMASK		GENMASK(14, 14)
/* Fields below are present for IPA v4.0 and above */
#define GSI_USE_SDMA_FMASK		GENMASK(15, 15)
#define GSI_SDMA_N_INT_FMASK		GENMASK(18, 16)
#define GSI_SDMA_MAX_BURST_FMASK	GENMASK(26, 19)
#define GSI_SDMA_N_IOVEC_FMASK		GENMASK(29, 27)
/* Fields below are present for IPA v4.2 and above */
#define GSI_USE_RD_WR_ENG_FMASK		GENMASK(30, 30)
#define GSI_USE_INTER_EE_FMASK		GENMASK(31, 31)

/** enum gsi_iram_size - IRAM_SIZE field values in HW_PARAM_2 */
enum gsi_iram_size {
	IRAM_SIZE_ONE_KB			= 0x0,
	IRAM_SIZE_TWO_KB			= 0x1,
	/* The next two values are available for IPA v4.0 and above */
	IRAM_SIZE_TWO_N_HALF_KB			= 0x2,
	IRAM_SIZE_THREE_KB			= 0x3,
	/* The next two values are available for IPA v4.5 and above */
	IRAM_SIZE_THREE_N_HALF_KB		= 0x4,
	IRAM_SIZE_FOUR_KB			= 0x5,
};

/* IRQ condition for each type is cleared by writing type-specific register */
#define GSI_CNTXT_TYPE_IRQ_OFFSET \
			(0x0001f080 + 0x4000 * GSI_EE_AP)
#define GSI_CNTXT_TYPE_IRQ_MSK_OFFSET \
			(0x0001f088 + 0x4000 * GSI_EE_AP)

/**
 * enum gsi_irq_type_id: GSI IRQ types
 * @GSI_CH_CTRL:		Channel allocation, deallocation, etc.
 * @GSI_EV_CTRL:		Event ring allocation, deallocation, etc.
 * @GSI_GLOB_EE:		Global/general event
 * @GSI_IEOB:			Transfer (TRE) completion
 * @GSI_INTER_EE_CH_CTRL:	Remote-issued stop/reset (unused)
 * @GSI_INTER_EE_EV_CTRL:	Remote-issued event reset (unused)
 * @GSI_GENERAL:		General hardware event (bus error, etc.)
 */
enum gsi_irq_type_id {
	GSI_CH_CTRL				= BIT(0),
	GSI_EV_CTRL				= BIT(1),
	GSI_GLOB_EE				= BIT(2),
	GSI_IEOB				= BIT(3),
	GSI_INTER_EE_CH_CTRL			= BIT(4),
	GSI_INTER_EE_EV_CTRL			= BIT(5),
	GSI_GENERAL				= BIT(6),
	/* IRQ types 7-31 (and their bit values) are reserved */
};

#define GSI_CNTXT_SRC_CH_IRQ_OFFSET \
			(0x0001f090 + 0x4000 * GSI_EE_AP)

#define GSI_CNTXT_SRC_EV_CH_IRQ_OFFSET \
			(0x0001f094 + 0x4000 * GSI_EE_AP)

#define GSI_CNTXT_SRC_CH_IRQ_MSK_OFFSET \
			(0x0001f098 + 0x4000 * GSI_EE_AP)

#define GSI_CNTXT_SRC_EV_CH_IRQ_MSK_OFFSET \
			(0x0001f09c + 0x4000 * GSI_EE_AP)

#define GSI_CNTXT_SRC_CH_IRQ_CLR_OFFSET \
			(0x0001f0a0 + 0x4000 * GSI_EE_AP)

#define GSI_CNTXT_SRC_EV_CH_IRQ_CLR_OFFSET \
			(0x0001f0a4 + 0x4000 * GSI_EE_AP)

#define GSI_CNTXT_SRC_IEOB_IRQ_OFFSET \
			(0x0001f0b0 + 0x4000 * GSI_EE_AP)

#define GSI_CNTXT_SRC_IEOB_IRQ_MSK_OFFSET \
			(0x0001f0b8 + 0x4000 * GSI_EE_AP)

#define GSI_CNTXT_SRC_IEOB_IRQ_CLR_OFFSET \
			(0x0001f0c0 + 0x4000 * GSI_EE_AP)

#define GSI_CNTXT_GLOB_IRQ_STTS_OFFSET \
			(0x0001f100 + 0x4000 * GSI_EE_AP)
#define GSI_CNTXT_GLOB_IRQ_EN_OFFSET \
			(0x0001f108 + 0x4000 * GSI_EE_AP)
#define GSI_CNTXT_GLOB_IRQ_CLR_OFFSET \
			(0x0001f110 + 0x4000 * GSI_EE_AP)

/** enum gsi_global_irq_id: Global GSI interrupt events */
enum gsi_global_irq_id {
	ERROR_INT				= BIT(0),
	GP_INT1					= BIT(1),
	GP_INT2					= BIT(2),
	GP_INT3					= BIT(3),
	/* Global IRQ types 4-31 (and their bit values) are reserved */
};

#define GSI_CNTXT_GSI_IRQ_STTS_OFFSET \
			(0x0001f118 + 0x4000 * GSI_EE_AP)
#define GSI_CNTXT_GSI_IRQ_EN_OFFSET \
			(0x0001f120 + 0x4000 * GSI_EE_AP)
#define GSI_CNTXT_GSI_IRQ_CLR_OFFSET \
			(0x0001f128 + 0x4000 * GSI_EE_AP)

/** enum gsi_general_irq_id: GSI general IRQ conditions */
enum gsi_general_irq_id {
	BREAK_POINT				= BIT(0),
	BUS_ERROR				= BIT(1),
	CMD_FIFO_OVRFLOW			= BIT(2),
	MCS_STACK_OVRFLOW			= BIT(3),
	/* General IRQ types 4-31 (and their bit values) are reserved */
};

#define GSI_CNTXT_INTSET_OFFSET \
			(0x0001f180 + 0x4000 * GSI_EE_AP)
#define INTYPE_FMASK			GENMASK(0, 0)

#define GSI_ERROR_LOG_OFFSET \
			(0x0001f200 + 0x4000 * GSI_EE_AP)

#define ERR_ARG3_FMASK			GENMASK(3, 0)
#define ERR_ARG2_FMASK			GENMASK(7, 4)
#define ERR_ARG1_FMASK			GENMASK(11, 8)
#define ERR_CODE_FMASK			GENMASK(15, 12)
#define ERR_VIRT_IDX_FMASK		GENMASK(23, 19)
#define ERR_TYPE_FMASK			GENMASK(27, 24)
#define ERR_EE_FMASK			GENMASK(31, 28)

/** enum gsi_err_code - ERR_CODE field values in EE_ERR_LOG */
enum gsi_err_code {
	GSI_INVALID_TRE				= 0x1,
	GSI_OUT_OF_BUFFERS			= 0x2,
	GSI_OUT_OF_RESOURCES			= 0x3,
	GSI_UNSUPPORTED_INTER_EE_OP		= 0x4,
	GSI_EVT_RING_EMPTY			= 0x5,
	GSI_NON_ALLOCATED_EVT_ACCESS		= 0x6,
	/* 7 is not assigned */
	GSI_HWO_1				= 0x8,
};

/** enum gsi_err_type - ERR_TYPE field values in EE_ERR_LOG */
enum gsi_err_type {
	GSI_ERR_TYPE_GLOB			= 0x1,
	GSI_ERR_TYPE_CHAN			= 0x2,
	GSI_ERR_TYPE_EVT			= 0x3,
};

#define GSI_ERROR_LOG_CLR_OFFSET \
			(0x0001f210 + 0x4000 * GSI_EE_AP)

#define GSI_CNTXT_SCRATCH_0_OFFSET \
			(0x0001f400 + 0x4000 * GSI_EE_AP)
#define INTER_EE_RESULT_FMASK		GENMASK(2, 0)
#define GENERIC_EE_RESULT_FMASK		GENMASK(7, 5)

/** enum gsi_generic_ee_result - GENERIC_EE_RESULT field values in SCRATCH_0 */
enum gsi_generic_ee_result {
	GENERIC_EE_SUCCESS			= 0x1,
	GENERIC_EE_INCORRECT_CHANNEL_STATE	= 0x2,
	GENERIC_EE_INCORRECT_DIRECTION		= 0x3,
	GENERIC_EE_INCORRECT_CHANNEL_TYPE	= 0x4,
	GENERIC_EE_INCORRECT_CHANNEL		= 0x5,
	GENERIC_EE_RETRY			= 0x6,
	GENERIC_EE_NO_RESOURCES			= 0x7,
};

extern const struct regs gsi_regs_v3_1;

/**
 * gsi_reg() - Return the structure describing a GSI register
 * @gsi:	GSI pointer
 * @reg_id:	GSI register ID
 */
const struct reg *gsi_reg(struct gsi *gsi, enum gsi_reg_id reg_id);

/**
 * gsi_reg_init() - Perform GSI register initialization
 * @gsi:	GSI pointer
 * @pdev:	GSI (IPA) platform device
 *
 * Initialize GSI registers, including looking up and I/O mapping
 * the "gsi" memory space.  This function sets gsi->virt_raw and
 * gsi->virt.
 */
int gsi_reg_init(struct gsi *gsi, struct platform_device *pdev);

/**
 * gsi_reg_exit() - Inverse of gsi_reg_init()
 * @gsi:	GSI pointer
 */
void gsi_reg_exit(struct gsi *gsi);

#endif	/* _GSI_REG_H_ */
