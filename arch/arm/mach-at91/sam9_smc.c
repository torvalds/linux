/*
 * linux/arch/arm/mach-at91/sam9_smc.c
 *
 * Copyright (C) 2008 Andrew Victor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/io.h>

#include <mach/at91sam9_smc.h>

#include "sam9_smc.h"

void __init sam9_smc_configure(int cs, struct sam9_smc_config* config)
{
	/* Setup register */
	at91_sys_write(AT91_SMC_SETUP(cs),
		  AT91_SMC_NWESETUP_(config->nwe_setup)
		| AT91_SMC_NCS_WRSETUP_(config->ncs_write_setup)
		| AT91_SMC_NRDSETUP_(config->nrd_setup)
		| AT91_SMC_NCS_RDSETUP_(config->ncs_read_setup)
	);

	/* Pulse register */
	at91_sys_write(AT91_SMC_PULSE(cs),
		  AT91_SMC_NWEPULSE_(config->nwe_pulse)
		| AT91_SMC_NCS_WRPULSE_(config->ncs_write_pulse)
                | AT91_SMC_NRDPULSE_(config->nrd_pulse)
		| AT91_SMC_NCS_RDPULSE_(config->ncs_read_pulse)
	);

	/* Cycle register */
	at91_sys_write(AT91_SMC_CYCLE(cs),
		  AT91_SMC_NWECYCLE_(config->write_cycle)
		| AT91_SMC_NRDCYCLE_(config->read_cycle)
	);

	/* Mode register */
	at91_sys_write(AT91_SMC_MODE(cs),
		  config->mode
		| AT91_SMC_TDF_(config->tdf_cycles)
	);
}
