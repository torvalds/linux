// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2023-2024 Linaro Ltd. */

#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/types.h>

#include "../gsi_reg.h"
#include "../ipa_version.h"
#include "../reg.h"

REG(INTER_EE_SRC_CH_IRQ_MSK, inter_ee_src_ch_irq_msk,
    0x0000c01c + 0x1000 * GSI_EE_AP);

REG(INTER_EE_SRC_EV_CH_IRQ_MSK, inter_ee_src_ev_ch_irq_msk,
    0x0000c028 + 0x1000 * GSI_EE_AP);

static const u32 reg_ch_c_cntxt_0_fmask[] = {
	[CHTYPE_PROTOCOL]				= GENMASK(6, 0),
	[CHTYPE_DIR]					= BIT(7),
	[CH_EE]						= GENMASK(11, 8),
	[CHID]						= GENMASK(19, 12),
	[CHSTATE]					= GENMASK(23, 20),
	[ELEMENT_SIZE]					= GENMASK(31, 24),
};

REG_STRIDE_FIELDS(CH_C_CNTXT_0, ch_c_cntxt_0,
		  0x00014000 + 0x12000 * GSI_EE_AP, 0x80);

static const u32 reg_ch_c_cntxt_1_fmask[] = {
	[CH_R_LENGTH]					= GENMASK(23, 0),
	[ERINDEX]					= GENMASK(31, 24),
};

REG_STRIDE_FIELDS(CH_C_CNTXT_1, ch_c_cntxt_1,
		  0x00014004 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_CNTXT_2, ch_c_cntxt_2, 0x00014008 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_CNTXT_3, ch_c_cntxt_3, 0x0001400c + 0x12000 * GSI_EE_AP, 0x80);

static const u32 reg_ch_c_qos_fmask[] = {
	[WRR_WEIGHT]					= GENMASK(3, 0),
						/* Bits 4-7 reserved */
	[MAX_PREFETCH]					= BIT(8),
	[USE_DB_ENG]					= BIT(9),
	[PREFETCH_MODE]					= GENMASK(13, 10),
						/* Bits 14-15 reserved */
	[EMPTY_LVL_THRSHOLD]				= GENMASK(23, 16),
	[DB_IN_BYTES]					= BIT(24),
	[LOW_LATENCY_EN]				= BIT(25),
						/* Bits 26-31 reserved */
};

REG_STRIDE_FIELDS(CH_C_QOS, ch_c_qos, 0x00014048 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_0, ch_c_scratch_0,
	   0x0001404c + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_1, ch_c_scratch_1,
	   0x00014050 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_2, ch_c_scratch_2,
	   0x00014054 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_3, ch_c_scratch_3,
	   0x00014058 + 0x12000 * GSI_EE_AP, 0x80);

static const u32 reg_ev_ch_e_cntxt_0_fmask[] = {
	[EV_CHTYPE]					= GENMASK(6, 0),
	[EV_INTYPE]					= BIT(7),
	[EV_EVCHID]					= GENMASK(15, 8),
	[EV_EE]						= GENMASK(19, 16),
	[EV_CHSTATE]					= GENMASK(23, 20),
	[EV_ELEMENT_SIZE]				= GENMASK(31, 24),
};

REG_STRIDE_FIELDS(EV_CH_E_CNTXT_0, ev_ch_e_cntxt_0,
		  0x0001c000 + 0x12000 * GSI_EE_AP, 0x80);

static const u32 reg_ev_ch_e_cntxt_1_fmask[] = {
	[R_LENGTH]					= GENMASK(23, 0),
};

REG_STRIDE_FIELDS(EV_CH_E_CNTXT_1, ev_ch_e_cntxt_1,
		  0x0001c004 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_2, ev_ch_e_cntxt_2,
	   0x0001c008 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_3, ev_ch_e_cntxt_3,
	   0x0001c00c + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_4, ev_ch_e_cntxt_4,
	   0x0001c010 + 0x12000 * GSI_EE_AP, 0x80);

static const u32 reg_ev_ch_e_cntxt_8_fmask[] = {
	[EV_MODT]					= GENMASK(15, 0),
	[EV_MODC]					= GENMASK(23, 16),
	[EV_MOD_CNT]					= GENMASK(31, 24),
};

REG_STRIDE_FIELDS(EV_CH_E_CNTXT_8, ev_ch_e_cntxt_8,
		  0x0001c020 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_9, ev_ch_e_cntxt_9,
	   0x0001c024 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_10, ev_ch_e_cntxt_10,
	   0x0001c028 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_11, ev_ch_e_cntxt_11,
	   0x0001c02c + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_12, ev_ch_e_cntxt_12,
	   0x0001c030 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_13, ev_ch_e_cntxt_13,
	   0x0001c034 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_SCRATCH_0, ev_ch_e_scratch_0,
	   0x0001c048 + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_SCRATCH_1, ev_ch_e_scratch_1,
	   0x0001c04c + 0x12000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_DOORBELL_0, ch_c_doorbell_0,
	   0x00024000 + 0x12000 * GSI_EE_AP, 0x08);

REG_STRIDE(EV_CH_E_DOORBELL_0, ev_ch_e_doorbell_0,
	   0x00024800 + 0x12000 * GSI_EE_AP, 0x08);

static const u32 reg_gsi_status_fmask[] = {
	[ENABLED]					= BIT(0),
						/* Bits 1-31 reserved */
};

REG_FIELDS(GSI_STATUS, gsi_status, 0x00025000 + 0x12000 * GSI_EE_AP);

static const u32 reg_ch_cmd_fmask[] = {
	[CH_CHID]					= GENMASK(7, 0),
						/* Bits 8-23 reserved */
	[CH_OPCODE]					= GENMASK(31, 24),
};

REG_FIELDS(CH_CMD, ch_cmd, 0x00025008 + 0x12000 * GSI_EE_AP);

static const u32 reg_ev_ch_cmd_fmask[] = {
	[EV_CHID]					= GENMASK(7, 0),
						/* Bits 8-23 reserved */
	[EV_OPCODE]					= GENMASK(31, 24),
};

REG_FIELDS(EV_CH_CMD, ev_ch_cmd, 0x00025010 + 0x12000 * GSI_EE_AP);

static const u32 reg_generic_cmd_fmask[] = {
	[GENERIC_OPCODE]				= GENMASK(4, 0),
	[GENERIC_CHID]					= GENMASK(9, 5),
	[GENERIC_EE]					= GENMASK(13, 10),
						/* Bits 14-31 reserved */
};

REG_FIELDS(GENERIC_CMD, generic_cmd, 0x00025018 + 0x12000 * GSI_EE_AP);

static const u32 reg_hw_param_2_fmask[] = {
	[NUM_CH_PER_EE]					= GENMASK(7, 0),
	[IRAM_SIZE]					= GENMASK(12, 8),
	[GSI_CH_PEND_TRANSLATE]				= BIT(13),
	[GSI_CH_FULL_LOGIC]				= BIT(14),
	[GSI_USE_SDMA]					= BIT(15),
	[GSI_SDMA_N_INT]				= GENMASK(18, 16),
	[GSI_SDMA_MAX_BURST]				= GENMASK(26, 19),
	[GSI_SDMA_N_IOVEC]				= GENMASK(29, 27),
	[GSI_USE_RD_WR_ENG]				= BIT(30),
	[GSI_USE_INTER_EE]				= BIT(31),
};

REG_FIELDS(HW_PARAM_2, hw_param_2, 0x00025040 + 0x12000 * GSI_EE_AP);

static const u32 reg_hw_param_4_fmask[] = {
	[EV_PER_EE]					= GENMASK(7, 0),
	[IRAM_PROTOCOL_COUNT]				= GENMASK(15, 8),
						/* Bits 16-31 reserved */
};

REG_FIELDS(HW_PARAM_4, hw_param_4, 0x00025050 + 0x12000 * GSI_EE_AP);

REG(CNTXT_TYPE_IRQ, cntxt_type_irq, 0x00025080 + 0x12000 * GSI_EE_AP);

REG(CNTXT_TYPE_IRQ_MSK, cntxt_type_irq_msk, 0x00025088 + 0x12000 * GSI_EE_AP);

REG(CNTXT_SRC_CH_IRQ, cntxt_src_ch_irq, 0x00025090 + 0x12000 * GSI_EE_AP);

REG(CNTXT_SRC_CH_IRQ_MSK, cntxt_src_ch_irq_msk,
    0x00025094 + 0x12000 * GSI_EE_AP);

REG(CNTXT_SRC_CH_IRQ_CLR, cntxt_src_ch_irq_clr,
    0x00025098 + 0x12000 * GSI_EE_AP);

REG(CNTXT_SRC_EV_CH_IRQ, cntxt_src_ev_ch_irq, 0x0002509c + 0x12000 * GSI_EE_AP);

REG(CNTXT_SRC_EV_CH_IRQ_MSK, cntxt_src_ev_ch_irq_msk,
    0x000250a0 + 0x12000 * GSI_EE_AP);

REG(CNTXT_SRC_EV_CH_IRQ_CLR, cntxt_src_ev_ch_irq_clr,
    0x000250a4 + 0x12000 * GSI_EE_AP);

REG(CNTXT_SRC_IEOB_IRQ, cntxt_src_ieob_irq, 0x000250a8 + 0x12000 * GSI_EE_AP);

REG(CNTXT_SRC_IEOB_IRQ_MSK, cntxt_src_ieob_irq_msk,
    0x000250ac + 0x12000 * GSI_EE_AP);

REG(CNTXT_SRC_IEOB_IRQ_CLR, cntxt_src_ieob_irq_clr,
    0x000250b0 + 0x12000 * GSI_EE_AP);

REG(CNTXT_GLOB_IRQ_STTS, cntxt_glob_irq_stts, 0x00025200 + 0x12000 * GSI_EE_AP);

REG(CNTXT_GLOB_IRQ_EN, cntxt_glob_irq_en, 0x00025204 + 0x12000 * GSI_EE_AP);

REG(CNTXT_GLOB_IRQ_CLR, cntxt_glob_irq_clr, 0x00025208 + 0x12000 * GSI_EE_AP);

REG(CNTXT_GSI_IRQ_STTS, cntxt_gsi_irq_stts, 0x0002520c + 0x12000 * GSI_EE_AP);

REG(CNTXT_GSI_IRQ_EN, cntxt_gsi_irq_en, 0x00025210 + 0x12000 * GSI_EE_AP);

REG(CNTXT_GSI_IRQ_CLR, cntxt_gsi_irq_clr, 0x00025214 + 0x12000 * GSI_EE_AP);

static const u32 reg_cntxt_intset_fmask[] = {
	[INTYPE]					= BIT(0)
						/* Bits 1-31 reserved */
};

REG_FIELDS(CNTXT_INTSET, cntxt_intset, 0x00025220 + 0x12000 * GSI_EE_AP);

static const u32 reg_error_log_fmask[] = {
	[ERR_ARG3]					= GENMASK(3, 0),
	[ERR_ARG2]					= GENMASK(7, 4),
	[ERR_ARG1]					= GENMASK(11, 8),
	[ERR_CODE]					= GENMASK(15, 12),
						/* Bits 16-18 reserved */
	[ERR_VIRT_IDX]					= GENMASK(23, 19),
	[ERR_TYPE]					= GENMASK(27, 24),
	[ERR_EE]					= GENMASK(31, 28),
};

REG_FIELDS(ERROR_LOG, error_log, 0x00025240 + 0x12000 * GSI_EE_AP);

REG(ERROR_LOG_CLR, error_log_clr, 0x00025244 + 0x12000 * GSI_EE_AP);

static const u32 reg_cntxt_scratch_0_fmask[] = {
	[INTER_EE_RESULT]				= GENMASK(2, 0),
						/* Bits 3-4 reserved */
	[GENERIC_EE_RESULT]				= GENMASK(7, 5),
						/* Bits 8-31 reserved */
};

REG_FIELDS(CNTXT_SCRATCH_0, cntxt_scratch_0, 0x00025400 + 0x12000 * GSI_EE_AP);

static const struct reg *reg_array[] = {
	[INTER_EE_SRC_CH_IRQ_MSK]	= &reg_inter_ee_src_ch_irq_msk,
	[INTER_EE_SRC_EV_CH_IRQ_MSK]	= &reg_inter_ee_src_ev_ch_irq_msk,
	[CH_C_CNTXT_0]			= &reg_ch_c_cntxt_0,
	[CH_C_CNTXT_1]			= &reg_ch_c_cntxt_1,
	[CH_C_CNTXT_2]			= &reg_ch_c_cntxt_2,
	[CH_C_CNTXT_3]			= &reg_ch_c_cntxt_3,
	[CH_C_QOS]			= &reg_ch_c_qos,
	[CH_C_SCRATCH_0]		= &reg_ch_c_scratch_0,
	[CH_C_SCRATCH_1]		= &reg_ch_c_scratch_1,
	[CH_C_SCRATCH_2]		= &reg_ch_c_scratch_2,
	[CH_C_SCRATCH_3]		= &reg_ch_c_scratch_3,
	[EV_CH_E_CNTXT_0]		= &reg_ev_ch_e_cntxt_0,
	[EV_CH_E_CNTXT_1]		= &reg_ev_ch_e_cntxt_1,
	[EV_CH_E_CNTXT_2]		= &reg_ev_ch_e_cntxt_2,
	[EV_CH_E_CNTXT_3]		= &reg_ev_ch_e_cntxt_3,
	[EV_CH_E_CNTXT_4]		= &reg_ev_ch_e_cntxt_4,
	[EV_CH_E_CNTXT_8]		= &reg_ev_ch_e_cntxt_8,
	[EV_CH_E_CNTXT_9]		= &reg_ev_ch_e_cntxt_9,
	[EV_CH_E_CNTXT_10]		= &reg_ev_ch_e_cntxt_10,
	[EV_CH_E_CNTXT_11]		= &reg_ev_ch_e_cntxt_11,
	[EV_CH_E_CNTXT_12]		= &reg_ev_ch_e_cntxt_12,
	[EV_CH_E_CNTXT_13]		= &reg_ev_ch_e_cntxt_13,
	[EV_CH_E_SCRATCH_0]		= &reg_ev_ch_e_scratch_0,
	[EV_CH_E_SCRATCH_1]		= &reg_ev_ch_e_scratch_1,
	[CH_C_DOORBELL_0]		= &reg_ch_c_doorbell_0,
	[EV_CH_E_DOORBELL_0]		= &reg_ev_ch_e_doorbell_0,
	[GSI_STATUS]			= &reg_gsi_status,
	[CH_CMD]			= &reg_ch_cmd,
	[EV_CH_CMD]			= &reg_ev_ch_cmd,
	[GENERIC_CMD]			= &reg_generic_cmd,
	[HW_PARAM_2]			= &reg_hw_param_2,
	[HW_PARAM_4]			= &reg_hw_param_4,
	[CNTXT_TYPE_IRQ]		= &reg_cntxt_type_irq,
	[CNTXT_TYPE_IRQ_MSK]		= &reg_cntxt_type_irq_msk,
	[CNTXT_SRC_CH_IRQ]		= &reg_cntxt_src_ch_irq,
	[CNTXT_SRC_CH_IRQ_MSK]		= &reg_cntxt_src_ch_irq_msk,
	[CNTXT_SRC_CH_IRQ_CLR]		= &reg_cntxt_src_ch_irq_clr,
	[CNTXT_SRC_EV_CH_IRQ]		= &reg_cntxt_src_ev_ch_irq,
	[CNTXT_SRC_EV_CH_IRQ_MSK]	= &reg_cntxt_src_ev_ch_irq_msk,
	[CNTXT_SRC_EV_CH_IRQ_CLR]	= &reg_cntxt_src_ev_ch_irq_clr,
	[CNTXT_SRC_IEOB_IRQ]		= &reg_cntxt_src_ieob_irq,
	[CNTXT_SRC_IEOB_IRQ_MSK]	= &reg_cntxt_src_ieob_irq_msk,
	[CNTXT_SRC_IEOB_IRQ_CLR]	= &reg_cntxt_src_ieob_irq_clr,
	[CNTXT_GLOB_IRQ_STTS]		= &reg_cntxt_glob_irq_stts,
	[CNTXT_GLOB_IRQ_EN]		= &reg_cntxt_glob_irq_en,
	[CNTXT_GLOB_IRQ_CLR]		= &reg_cntxt_glob_irq_clr,
	[CNTXT_GSI_IRQ_STTS]		= &reg_cntxt_gsi_irq_stts,
	[CNTXT_GSI_IRQ_EN]		= &reg_cntxt_gsi_irq_en,
	[CNTXT_GSI_IRQ_CLR]		= &reg_cntxt_gsi_irq_clr,
	[CNTXT_INTSET]			= &reg_cntxt_intset,
	[ERROR_LOG]			= &reg_error_log,
	[ERROR_LOG_CLR]			= &reg_error_log_clr,
	[CNTXT_SCRATCH_0]		= &reg_cntxt_scratch_0,
};

const struct regs gsi_regs_v5_0 = {
	.reg_count	= ARRAY_SIZE(reg_array),
	.reg		= reg_array,
};
