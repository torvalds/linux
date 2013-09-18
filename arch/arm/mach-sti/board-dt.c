/*
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited.
 * Author(s): Srinivas Kandagatla <srinivas.kandagatla@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/irq.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>

#include "smp.h"

void __init stih41x_l2x0_init(void)
{
	u32 way_size = 0x4;
	u32 aux_ctrl;
	/* may be this can be encoded in macros like BIT*() */
	aux_ctrl = (0x1 << L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT) |
		(0x1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
		(0x1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
		(way_size << L2X0_AUX_CTRL_WAY_SIZE_SHIFT);

	l2x0_of_init(aux_ctrl, L2X0_AUX_CTRL_MASK);
}

static void __init stih41x_timer_init(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
	stih41x_l2x0_init();
}

static const char *stih41x_dt_match[] __initdata = {
	"st,stih415",
	"st,stih416",
	NULL
};

DT_MACHINE_START(STM, "STiH415/416 SoC with Flattened Device Tree")
	.init_time	= stih41x_timer_init,
	.smp		= smp_ops(sti_smp_ops),
	.dt_compat	= stih41x_dt_match,
MACHINE_END
