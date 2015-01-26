/*
 * arch/arm/mach-at91/at91rm9200.c
 *
 *  Copyright (C) 2005 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/clk/at91_pmc.h>

#include <asm/mach/map.h>
#include <asm/system_misc.h>
#include <mach/at91_st.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"



/* --------------------------------------------------------------------
 *  AT91RM9200 processor initialization
 * -------------------------------------------------------------------- */


AT91_SOC_START(at91rm9200)
AT91_SOC_END
