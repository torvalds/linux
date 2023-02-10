// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2023 Linaro Ltd. */

#include <linux/types.h>

#include "../gsi.h"
#include "../reg.h"
#include "../gsi_reg.h"

REG_STRIDE(CH_C_CNTXT_0, ch_c_cntxt_0, 0x0001c000 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_CNTXT_1, ch_c_cntxt_1, 0x0001c004 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_CNTXT_2, ch_c_cntxt_2, 0x0001c008 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_CNTXT_3, ch_c_cntxt_3, 0x0001c00c + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_QOS, ch_c_qos, 0x0001c05c + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_0, ch_c_scratch_0,
	   0x0001c060 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_1, ch_c_scratch_1,
	   0x0001c064 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_2, ch_c_scratch_2,
	   0x0001c068 + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_SCRATCH_3, ch_c_scratch_3,
	   0x0001c06c + 0x4000 * GSI_EE_AP, 0x80);

REG_STRIDE(CH_C_DOORBELL_0, ch_c_doorbell_0,
	   0x0001e000 + 0x4000 * GSI_EE_AP, 0x08);

static const struct reg *reg_array[] = {
	[CH_C_CNTXT_0]			= &reg_ch_c_cntxt_0,
	[CH_C_CNTXT_1]			= &reg_ch_c_cntxt_1,
	[CH_C_CNTXT_2]			= &reg_ch_c_cntxt_2,
	[CH_C_CNTXT_3]			= &reg_ch_c_cntxt_3,
	[CH_C_QOS]			= &reg_ch_c_qos,
	[CH_C_SCRATCH_0]		= &reg_ch_c_scratch_0,
	[CH_C_SCRATCH_1]		= &reg_ch_c_scratch_1,
	[CH_C_SCRATCH_2]		= &reg_ch_c_scratch_2,
	[CH_C_SCRATCH_3]		= &reg_ch_c_scratch_3,
	[CH_C_DOORBELL_0]		= &reg_ch_c_doorbell_0,
};

const struct regs gsi_regs_v3_1 = {
	.reg_count	= ARRAY_SIZE(reg_array),
	.reg		= reg_array,
};
