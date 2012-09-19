/*
 *  Copyright (C) 2012 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/dw_apb_timer.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>

extern void socfpga_init_clocks(void);

const static struct of_device_id irq_match[] = {
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init, },
	{}
};

static void __init gic_init_irq(void)
{
	of_irq_init(irq_match);
}

static void socfpga_cyclone5_restart(char mode, const char *cmd)
{
	/* TODO: */
}

static void __init socfpga_cyclone5_init(void)
{
	l2x0_of_init(0, ~0UL);
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	socfpga_init_clocks();
}

static const char *altera_dt_match[] = {
	"altr,socfpga",
	"altr,socfpga-cyclone5",
	NULL
};

DT_MACHINE_START(SOCFPGA, "Altera SOCFPGA")
	.init_irq	= gic_init_irq,
	.handle_irq     = gic_handle_irq,
	.timer		= &dw_apb_timer,
	.init_machine	= socfpga_cyclone5_init,
	.restart	= socfpga_cyclone5_restart,
	.dt_compat	= altera_dt_match,
MACHINE_END
