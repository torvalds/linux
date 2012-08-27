/*
 * Clock definitions for u8500 platform.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/platform_data/clk-ux500.h>

#include "clk.h"

void u8500_clk_init(void)
{
	/* register clocks here */
}
