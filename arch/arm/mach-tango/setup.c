// SPDX-License-Identifier: GPL-2.0
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>
#include "smc.h"

static void tango_l2c_write(unsigned long val, unsigned int reg)
{
	if (reg == L2X0_CTRL)
		tango_set_l2_control(val);
}

static const char *const tango_dt_compat[] = { "sigma,tango4", NULL };

DT_MACHINE_START(TANGO_DT, "Sigma Tango DT")
	.dt_compat	= tango_dt_compat,
	.l2c_aux_mask	= ~0,
	.l2c_write_sec	= tango_l2c_write,
MACHINE_END
