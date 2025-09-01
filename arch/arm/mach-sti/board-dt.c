// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited.
 * Author(s): Srinivas Kandagatla <srinivas.kandagatla@st.com>
 */

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>

#include "smp.h"

static const char *const stih41x_dt_match[] __initconst = {
	"st,stih407",
	"st,stih410",
	"st,stih418",
	NULL
};

DT_MACHINE_START(STM, "STi SoC with Flattened Device Tree")
	.dt_compat	= stih41x_dt_match,
	.l2c_aux_val	= L2C_AUX_CTRL_SHARED_OVERRIDE |
			  L310_AUX_CTRL_DATA_PREFETCH |
			  L310_AUX_CTRL_INSTR_PREFETCH |
			  L2C_AUX_CTRL_WAY_SIZE(4),
	.l2c_aux_mask	= 0xc0000fff,
	.smp		= smp_ops(sti_smp_ops),
MACHINE_END
