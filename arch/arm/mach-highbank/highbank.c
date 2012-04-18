/*
 * Copyright 2010-2011 Calxeda, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/smp_twd.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/timer-sp.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include "core.h"
#include "sysregs.h"

void __iomem *sregs_base;

#define HB_SCU_VIRT_BASE	0xfee00000
void __iomem *scu_base_addr = ((void __iomem *)(HB_SCU_VIRT_BASE));

static struct map_desc scu_io_desc __initdata = {
	.virtual	= HB_SCU_VIRT_BASE,
	.pfn		= 0, /* run-time */
	.length		= SZ_4K,
	.type		= MT_DEVICE,
};

static void __init highbank_scu_map_io(void)
{
	unsigned long base;

	/* Get SCU base */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (base));

	scu_io_desc.pfn = __phys_to_pfn(base);
	iotable_init(&scu_io_desc, 1);
}

static void __init highbank_map_io(void)
{
	highbank_scu_map_io();
	highbank_lluart_map_io();
}

#define HB_JUMP_TABLE_PHYS(cpu)		(0x40 + (0x10 * (cpu)))
#define HB_JUMP_TABLE_VIRT(cpu)		phys_to_virt(HB_JUMP_TABLE_PHYS(cpu))

void highbank_set_cpu_jump(int cpu, void *jump_addr)
{
	cpu = cpu_logical_map(cpu);
	writel(virt_to_phys(jump_addr), HB_JUMP_TABLE_VIRT(cpu));
	__cpuc_flush_dcache_area(HB_JUMP_TABLE_VIRT(cpu), 16);
	outer_clean_range(HB_JUMP_TABLE_PHYS(cpu),
			  HB_JUMP_TABLE_PHYS(cpu) + 15);
}

const static struct of_device_id irq_match[] = {
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init, },
	{}
};

static void __init highbank_init_irq(void)
{
	of_irq_init(irq_match);
	l2x0_of_init(0, ~0UL);
}

static void __init highbank_timer_init(void)
{
	int irq;
	struct device_node *np;
	void __iomem *timer_base;

	/* Map system registers */
	np = of_find_compatible_node(NULL, NULL, "calxeda,hb-sregs");
	sregs_base = of_iomap(np, 0);
	WARN_ON(!sregs_base);

	np = of_find_compatible_node(NULL, NULL, "arm,sp804");
	timer_base = of_iomap(np, 0);
	WARN_ON(!timer_base);
	irq = irq_of_parse_and_map(np, 0);

	highbank_clocks_init();

	sp804_clocksource_and_sched_clock_init(timer_base + 0x20, "timer1");
	sp804_clockevents_init(timer_base, irq, "timer0");

	twd_local_timer_of_register();
}

static struct sys_timer highbank_timer = {
	.init = highbank_timer_init,
};

static void highbank_power_off(void)
{
	hignbank_set_pwr_shutdown();
	scu_power_mode(scu_base_addr, SCU_PM_POWEROFF);

	while (1)
		cpu_do_idle();
}

static void __init highbank_init(void)
{
	pm_power_off = highbank_power_off;

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *highbank_match[] __initconst = {
	"calxeda,highbank",
	NULL,
};

DT_MACHINE_START(HIGHBANK, "Highbank")
	.map_io		= highbank_map_io,
	.init_irq	= highbank_init_irq,
	.timer		= &highbank_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= highbank_init,
	.dt_compat	= highbank_match,
	.restart	= highbank_restart,
MACHINE_END
