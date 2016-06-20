/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * Author: Lee Jones <lee.jones@linaro.org> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/sys_soc.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/platform_data/arm-ux500-pm.h>

#include <asm/mach/map.h>

#include "setup.h"

#include "board-mop500.h"
#include "db8500-regs.h"

void ux500_restart(enum reboot_mode mode, const char *cmd)
{
	local_irq_disable();
	local_fiq_disable();

	prcmu_system_reset(0);
}

/*
 * FIXME: Should we set up the GPIO domain here?
 *
 * The problem is that we cannot put the interrupt resources into the platform
 * device until the irqdomain has been added. Right now, we set the GIC interrupt
 * domain from init_irq(), then load the gpio driver from
 * core_initcall(nmk_gpio_init) and add the platform devices from
 * arch_initcall(customize_machine).
 *
 * This feels fragile because it depends on the gpio device getting probed
 * _before_ any device uses the gpio interrupts.
*/
void __init ux500_init_irq(void)
{
	struct device_node *np;
	struct resource r;

	irqchip_init();
	np = of_find_compatible_node(NULL, NULL, "stericsson,db8500-prcmu");
	of_address_to_resource(np, 0, &r);
	of_node_put(np);
	if (!r.start) {
		pr_err("could not find PRCMU base resource\n");
		return;
	}
	prcmu_early_init(r.start, r.end-r.start);
	ux500_pm_init(r.start, r.end-r.start);
	ux500_l2x0_init();
}
