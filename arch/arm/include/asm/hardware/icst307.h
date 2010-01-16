/*
 *  arch/arm/include/asm/hardware/icst307.h
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Support functions for calculating clocks/divisors for the ICS307
 *  clock generators.  See http://www.icst.com/ for more information
 *  on these devices.
 *
 *  This file is similar to the icst525.h file
 */
#ifndef ASMARM_HARDWARE_ICST307_H
#define ASMARM_HARDWARE_ICST307_H

#include <asm/hardware/icst.h>

unsigned long icst307_khz(const struct icst_params *p, struct icst_vco vco);
struct icst_vco icst307_khz_to_vco(const struct icst_params *p, unsigned long freq);

/*
 * ICST307 VCO frequency must be between 6MHz and 200MHz (3.3 or 5V).
 * This frequency is pre-output divider.
 */
#define ICST307_VCO_MIN	6000
#define ICST307_VCO_MAX	200000

#endif
