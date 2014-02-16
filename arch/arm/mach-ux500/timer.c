/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 * Author: Mattias Wallin <mattias.wallin@stericsson.com> for ST-Ericsson
 */
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/clksrc-dbx500-prcmu.h>
#include <linux/clocksource.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "setup.h"

#include "db8500-regs.h"
#include "id.h"

const static struct of_device_id prcmu_timer_of_match[] __initconst = {
	{ .compatible = "stericsson,db8500-prcmu-timer-4", },
	{ },
};

void __init ux500_timer_init(void)
{
	void __iomem *prcmu_timer_base;
	void __iomem *tmp_base;
	struct device_node *np;

	if (cpu_is_u8500_family() || cpu_is_ux540_family())
		prcmu_timer_base = __io_address(U8500_PRCMU_TIMER_4_BASE);
	else
		ux500_unknown_soc();

	np = of_find_matching_node(NULL, prcmu_timer_of_match);
	if (!np)
		goto dt_fail;

	tmp_base = of_iomap(np, 0);
	if (!tmp_base)
		goto dt_fail;

	prcmu_timer_base = tmp_base;

dt_fail:
	clksrc_dbx500_prcmu_init(prcmu_timer_base);
	clocksource_of_init();
}
