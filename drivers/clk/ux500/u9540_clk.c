/*
 * Clock definitions for u9540 platform.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/clk-provider.h>
#include <linux/mfd/dbx500-prcmu.h>
#include "clk.h"

static void u9540_clk_init(struct device_node *np)
{
	/* register clocks here */
}
CLK_OF_DECLARE(u9540_clks, "stericsson,u9540-clks", u9540_clk_init);
