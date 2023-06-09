/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CLK_STARFIVE_JH7110_H
#define __CLK_STARFIVE_JH7110_H

#include "clk-starfive-jh71x0.h"

int jh7110_reset_controller_register(struct jh71x0_clk_priv *priv,
				     const char *adev_name,
				     u32 adev_id);

#endif
