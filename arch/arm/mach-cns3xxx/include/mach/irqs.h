/*
 * Copyright 2000 Deep Blue Solutions Ltd.
 * Copyright 2003 ARM Limited
 * Copyright 2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

#define IRQ_LOCALTIMER		29
#define IRQ_LOCALWDOG		30
#define IRQ_TC11MP_GIC_START	32

#include <mach/cns3xxx.h>

#ifndef NR_IRQS
#error "NR_IRQS not defined by the board-specific files"
#endif

#endif
