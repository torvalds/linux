/*
 * arch/arm/mach-prima2/include/mach/clkdev.h
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __MACH_CLKDEV_H
#define __MACH_CLKDEV_H

#define __clk_get(clk) ({ 1; })
#define __clk_put(clk) do { } while (0)

#endif
