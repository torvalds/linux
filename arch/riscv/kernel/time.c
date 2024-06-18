// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/acpi.h>
#include <linux/of_clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <asm/sbi.h>
#include <asm/processor.h>
#include <asm/timex.h>
#include <asm/paravirt.h>

unsigned long riscv_timebase __ro_after_init;
EXPORT_SYMBOL_GPL(riscv_timebase);

void __init time_init(void)
{
	struct device_node *cpu;
	struct acpi_table_rhct *rhct;
	acpi_status status;
	u32 prop;

	if (acpi_disabled) {
		cpu = of_find_node_by_path("/cpus");
		if (!cpu || of_property_read_u32(cpu, "timebase-frequency", &prop))
			panic("RISC-V system with no 'timebase-frequency' in DTS\n");

		of_node_put(cpu);
		riscv_timebase = prop;
		of_clk_init(NULL);
	} else {
		status = acpi_get_table(ACPI_SIG_RHCT, 0, (struct acpi_table_header **)&rhct);
		if (ACPI_FAILURE(status))
			panic("RISC-V ACPI system with no RHCT table\n");

		riscv_timebase = rhct->time_base_freq;
		acpi_put_table((struct acpi_table_header *)rhct);
	}

	lpj_fine = riscv_timebase / HZ;

	timer_probe();

	tick_setup_hrtimer_broadcast();

	pv_time_init();
}
