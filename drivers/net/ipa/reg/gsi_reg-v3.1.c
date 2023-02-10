// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2023 Linaro Ltd. */

#include <linux/types.h>

#include "../gsi.h"
#include "../reg.h"
#include "../gsi_reg.h"

REG_STRIDE(CH_C_QOS, ch_c_qos, 0x0001c05c + 0x4000 * GSI_EE_AP, 0x80);

static const struct reg *reg_array[] = {
	[CH_C_QOS]			= &reg_ch_c_qos,
};

const struct regs gsi_regs_v3_1 = {
	.reg_count	= ARRAY_SIZE(reg_array),
	.reg		= reg_array,
};
