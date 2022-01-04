// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/of_clk.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <asm/sbi.h>
#include <asm/processor.h>
#include <asm/timex.h>

unsigned long riscv_timebase __ro_after_init;
EXPORT_SYMBOL_GPL(riscv_timebase);

void __init time_init(void)
{
	struct device_node *cpu;
	u32 prop;

	cpu = of_find_node_by_path("/cpus");
	if (!cpu || of_property_read_u32(cpu, "timebase-frequency", &prop))
		panic(KERN_WARNING "RISC-V system with no 'timebase-frequency' in DTS\n");
	of_node_put(cpu);
	riscv_timebase = prop;

	lpj_fine = riscv_timebase / HZ;

	of_clk_init(NULL);
	timer_probe();
}

void clocksource_arch_init(struct clocksource *cs)
{
#ifdef CONFIG_GENERIC_GETTIMEOFDAY
	cs->vdso_clock_mode = VDSO_CLOCKMODE_ARCHTIMER;
#else
	cs->vdso_clock_mode = VDSO_CLOCKMODE_NONE;
#endif
}
