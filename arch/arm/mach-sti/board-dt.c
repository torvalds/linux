/*
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited.
 * Author(s): Srinivas Kandagatla <srinivas.kandagatla@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/of_platform.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>

#include "smp.h"

static const char *const stih41x_dt_match[] __initconst = {
	"st,stih415",
	"st,stih416",
	"st,stih407",
	"st,stih410",
	"st,stih418",
	NULL
};

static void sti_l2_write_sec(unsigned long val, unsigned reg)
{
	/*
	 * We can't write to secure registers as we are in non-secure
	 * mode, until we have some SMI service available.
	 */
}

DT_MACHINE_START(STM, "STi SoC with Flattened Device Tree")
	.dt_compat	= stih41x_dt_match,
	.l2c_aux_val	= L2C_AUX_CTRL_SHARED_OVERRIDE |
			  L310_AUX_CTRL_DATA_PREFETCH |
			  L310_AUX_CTRL_INSTR_PREFETCH |
			  L2C_AUX_CTRL_WAY_SIZE(4),
	.l2c_aux_mask	= 0xc0000fff,
	.smp		= smp_ops(sti_smp_ops),
	.l2c_write_sec	= sti_l2_write_sec,
MACHINE_END
