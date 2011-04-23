/*
 * Copyright (C) 2007 Atmel Corporation.
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2
 */

#include <linux/module.h>
#include <linux/io.h>

#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/cpu.h>

#include "soc.h"
#include "generic.h"

struct at91_soc __initdata at91_boot_soc;

static struct map_desc at91_io_desc __initdata = {
	.virtual	= AT91_VA_BASE_SYS,
	.pfn		= __phys_to_pfn(AT91_BASE_SYS),
	.length		= SZ_16K,
	.type		= MT_DEVICE,
};

void __init at91_map_io(void)
{
	/* Map peripherals */
	iotable_init(&at91_io_desc, 1);

	if (cpu_is_at91cap9())
		at91_boot_soc = at91cap9_soc;
	else if (cpu_is_at91rm9200())
		at91_boot_soc = at91rm9200_soc;
	else if (cpu_is_at91sam9260())
		at91_boot_soc = at91sam9260_soc;
	else if (cpu_is_at91sam9261())
		at91_boot_soc = at91sam9261_soc;
	else if (cpu_is_at91sam9263())
		at91_boot_soc = at91sam9263_soc;
	else if (cpu_is_at91sam9g10())
		at91_boot_soc = at91sam9261_soc;
	else if (cpu_is_at91sam9g20())
		at91_boot_soc = at91sam9260_soc;
	else if (cpu_is_at91sam9g45())
		at91_boot_soc = at91sam9g45_soc;
	else if (cpu_is_at91sam9rl())
		at91_boot_soc = at91sam9rl_soc;
	else if (cpu_is_at91sam9x5())
		at91_boot_soc = at91sam9x5_soc;
	else
		panic("Impossible to detect the SOC type");

	if (at91_boot_soc.map_io)
		at91_boot_soc.map_io();
}

void __init at91_initialize(unsigned long main_clock)
{
	at91_boot_soc.init(main_clock);
}
