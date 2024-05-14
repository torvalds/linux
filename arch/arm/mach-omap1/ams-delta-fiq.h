/* SPDX-License-Identifier: GPL-2.0 */

/*
 * arch/arm/mach-omap1/ams-delta-fiq.h
 *
 * Taken from the original Amstrad modifications to fiq.h
 *
 * Copyright (c) 2004 Amstrad Plc
 * Copyright (c) 2006 Matt Callow
 * Copyright (c) 2010 Janusz Krzysztofik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __AMS_DELTA_FIQ_H
#define __AMS_DELTA_FIQ_H

#include "irqs.h"

/*
 * Interrupt number used for passing control from FIQ to IRQ.
 * IRQ12, described as reserved, has been selected.
 */
#define INT_DEFERRED_FIQ	INT_1510_RES12
/*
 * Base address of an interrupt handler that the INT_DEFERRED_FIQ belongs to.
 */
#if (INT_DEFERRED_FIQ < IH2_BASE)
#define DEFERRED_FIQ_IH_BASE	OMAP_IH1_BASE
#else
#define DEFERRED_FIQ_IH_BASE	OMAP_IH2_BASE
#endif

#ifndef __ASSEMBLER__
extern unsigned char qwerty_fiqin_start, qwerty_fiqin_end;

extern void __init ams_delta_init_fiq(struct gpio_chip *chip,
				      struct platform_device *pdev);
#endif

#endif
