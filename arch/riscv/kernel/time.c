/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/delay.h>

#ifdef CONFIG_RISCV_TIMER
#include <linux/timer_riscv.h>
#endif

#include <asm/sbi.h>

unsigned long riscv_timebase;

DECLARE_PER_CPU(struct clock_event_device, riscv_clock_event);

void riscv_timer_interrupt(void)
{
#ifdef CONFIG_RISCV_TIMER
	/*
	 * FIXME: This needs to be cleaned up along with the rest of the IRQ
	 * handling cleanup.  See irq.c for more details.
	 */
	struct clock_event_device *evdev = this_cpu_ptr(&riscv_clock_event);

	evdev->event_handler(evdev);
#endif
}

void __init init_clockevent(void)
{
	timer_probe();
	csr_set(sie, SIE_STIE);
}

void __init time_init(void)
{
	struct device_node *cpu;
	u32 prop;

	cpu = of_find_node_by_path("/cpus");
	if (!cpu || of_property_read_u32(cpu, "timebase-frequency", &prop))
		panic(KERN_WARNING "RISC-V system with no 'timebase-frequency' in DTS\n");
	riscv_timebase = prop;

	lpj_fine = riscv_timebase / HZ;

	init_clockevent();
}
