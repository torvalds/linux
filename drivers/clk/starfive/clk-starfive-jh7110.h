/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CLK_STARFIVE_JH7110_H
#define __CLK_STARFIVE_JH7110_H

#include "clk-starfive-jh71x0.h"

/* top clocks of ISP/VOUT domain from JH7110 SYSCRG */
struct jh7110_top_sysclk {
	struct clk_bulk_data *top_clks;
	int top_clks_num;
};

int jh7110_reset_controller_register(struct jh71x0_clk_priv *priv,
				     const char *adev_name,
				     u32 adev_id);

#endif
