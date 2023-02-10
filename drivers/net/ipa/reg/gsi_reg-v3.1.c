// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2023 Linaro Ltd. */

#include <linux/types.h>

#include "../gsi.h"
#include "../reg.h"
#include "../gsi_reg.h"

/* The inter-EE IRQ registers are relative to gsi->virt_raw (IPA v3.5+) */

REG(INTER_EE_SRC_CH_IRQ_MSK, inter_ee_src_ch_irq_msk,
    0x0000c020 + 0x1000 * GSI_EE_AP);

REG(INTER_EE_SRC_EV_CH_IRQ_MSK, inter_ee_src_ev_ch_irq_msk,
    0x0000c024 + 0x1000 * GSI_EE_AP);

/* All other register offsets are relative to gsi->virt */

REG_STRIDE(CH_C_CNTXT_0, ch_c_cntxt_0, 0x0001c000 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_CNTXT_1, ch_c_cntxt_1, 0x0001c004 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_CNTXT_2, ch_c_cntxt_2, 0x0001c008 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_CNTXT_3, ch_c_cntxt_3, 0x0001c00c + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_QOS, ch_c_qos, 0x0001c05c + 0x4000 * GSI_EE_AP, 0x80);

REG(ERROR_LOG, error_log, 0x0001f200 + 0x4000 * GSI_EE_AP);

REG(ERROR_LOG_CLR, error_log_clr, 0x0001f210 + 0x4000 * GSI_EE_AP);

REG_STRIDE(CH_C_SCRATCH_0, ch_c_scratch_0,
	   0x0001c060 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_1, ch_c_scratch_1,
	   0x0001c064 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_2, ch_c_scratch_2,
	   0x0001c068 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_3, ch_c_scratch_3,
	   0x0001c06c + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_0, ev_ch_e_cntxt_0,
	   0x0001d000 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_1, ev_ch_e_cntxt_1,
	   0x0001d004 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_2, ev_ch_e_cntxt_2,
	   0x0001d008 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_3, ev_ch_e_cntxt_3,
	   0x0001d00c + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_4, ev_ch_e_cntxt_4,
	   0x0001d010 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_8, ev_ch_e_cntxt_8,
	   0x0001d020 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_9, ev_ch_e_cntxt_9,
	   0x0001d024 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_10, ev_ch_e_cntxt_10,
	   0x0001d028 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_11, ev_ch_e_cntxt_11,
	   0x0001d02c + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_12, ev_ch_e_cntxt_12,
	   0x0001d030 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_CNTXT_13, ev_ch_e_cntxt_13,
	   0x0001d034 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_SCRATCH_0, ev_ch_e_scratch_0,
	   0x0001d048 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(EV_CH_E_SCRATCH_1, ev_ch_e_scratch_1,
	   0x0001d04c + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_DOORBELL_0, ch_c_doorbell_0,
	   0x0001e000 + 0x4000 * GSI_EE_AP, 0x08);

REG_STRIDE(EV_CH_E_DOORBELL_0, ev_ch_e_doorbell_0,
	   0x0001e100 + 0x4000 * GSI_EE_AP, 0x08);

REG(GSI_STATUS, gsi_status, 0x0001f000 + 0x4000 * GSI_EE_AP);

REG(CH_CMD, ch_cmd, 0x0001f008 + 0x4000 * GSI_EE_AP);

REG(EV_CH_CMD, ev_ch_cmd, 0x0001f010 + 0x4000 * GSI_EE_AP);

REG(GENERIC_CMD, generic_cmd, 0x0001f018 + 0x4000 * GSI_EE_AP);

REG(CNTXT_TYPE_IRQ, cntxt_type_irq, 0x0001f080 + 0x4000 * GSI_EE_AP);

REG(CNTXT_TYPE_IRQ_MSK, cntxt_type_irq_msk, 0x0001f088 + 0x4000 * GSI_EE_AP);

REG(CNTXT_SRC_CH_IRQ, cntxt_src_ch_irq, 0x0001f090 + 0x4000 * GSI_EE_AP);

REG(CNTXT_SRC_EV_CH_IRQ, cntxt_src_ev_ch_irq, 0x0001f094 + 0x4000 * GSI_EE_AP);

REG(CNTXT_SRC_CH_IRQ_MSK, cntxt_src_ch_irq_msk,
    0x0001f098 + 0x4000 * GSI_EE_AP);

REG(CNTXT_SRC_EV_CH_IRQ_MSK, cntxt_src_ev_ch_irq_msk,
    0x0001f09c + 0x4000 * GSI_EE_AP);

REG(CNTXT_SRC_CH_IRQ_CLR, cntxt_src_ch_irq_clr,
    0x0001f0a0 + 0x4000 * GSI_EE_AP);

REG(CNTXT_SRC_EV_CH_IRQ_CLR, cntxt_src_ev_ch_irq_clr,
    0x0001f0a4 + 0x4000 * GSI_EE_AP);

REG(CNTXT_SRC_IEOB_IRQ, cntxt_src_ieob_irq, 0x0001f0b0 + 0x4000 * GSI_EE_AP);

REG(CNTXT_SRC_IEOB_IRQ_MSK, cntxt_src_ieob_irq_msk,
    0x0001f0b8 + 0x4000 * GSI_EE_AP);

REG(CNTXT_SRC_IEOB_IRQ_CLR, cntxt_src_ieob_irq_clr,
    0x0001f0c0 + 0x4000 * GSI_EE_AP);

REG(CNTXT_GLOB_IRQ_STTS, cntxt_glob_irq_stts, 0x0001f100 + 0x4000 * GSI_EE_AP);

REG(CNTXT_GLOB_IRQ_EN, cntxt_glob_irq_en, 0x0001f108 + 0x4000 * GSI_EE_AP);

REG(CNTXT_GLOB_IRQ_CLR, cntxt_glob_irq_clr, 0x0001f110 + 0x4000 * GSI_EE_AP);

REG(CNTXT_GSI_IRQ_STTS, cntxt_gsi_irq_stts, 0x0001f118 + 0x4000 * GSI_EE_AP);

REG(CNTXT_GSI_IRQ_EN, cntxt_gsi_irq_en, 0x0001f120 + 0x4000 * GSI_EE_AP);

REG(CNTXT_GSI_IRQ_CLR, cntxt_gsi_irq_clr, 0x0001f128 + 0x4000 * GSI_EE_AP);

REG(CNTXT_INTSET, cntxt_intset, 0x0001f180 + 0x4000 * GSI_EE_AP);

REG(CNTXT_SCRATCH_0, cntxt_scratch_0, 0x0001f400 + 0x4000 * GSI_EE_AP);

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
	[CNTXT_TYPE_IRQ]		= &reg_cntxt_type_irq,
	[CNTXT_TYPE_IRQ_MSK]		= &reg_cntxt_type_irq_msk,
	[CNTXT_SRC_CH_IRQ]		= &reg_cntxt_src_ch_irq,
	[CNTXT_SRC_EV_CH_IRQ]		= &reg_cntxt_src_ev_ch_irq,
	[CNTXT_SRC_CH_IRQ_MSK]		= &reg_cntxt_src_ch_irq_msk,
	[CNTXT_SRC_EV_CH_IRQ_MSK]	= &reg_cntxt_src_ev_ch_irq_msk,
	[CNTXT_SRC_CH_IRQ_CLR]		= &reg_cntxt_src_ch_irq_clr,
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

const struct regs gsi_regs_v3_1 = {
	.reg_count	= ARRAY_SIZE(reg_array),
	.reg		= reg_array,
};
