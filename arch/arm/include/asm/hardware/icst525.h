/*
 *  arch/arm/include/asm/hardware/icst525.h
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Support functions for calculating clocks/divisors for the ICST525
 *  clock generators.  See http://www.icst.com/ for more information
 *  on these devices.
 */
#ifndef ASMARM_HARDWARE_ICST525_H
#define ASMARM_HARDWARE_ICST525_H

#include <asm/hardware/icst.h>

unsigned long icst525_khz(const struct icst_params *p, struct icst_vco vco);
struct icst_vco icst525_khz_to_vco(const struct icst_params *p, unsigned long freq);

/*
 * ICST525 VCO frequency must be between 10MHz and 200MHz (3V) or 320MHz (5V).
 * This frequency is pre-output divider.
 */
#define ICST525_VCO_MIN		10000
#define ICST525_VCO_MAX_3V	200000
#define ICST525_VCO_MAX_5V	320000

#endif
