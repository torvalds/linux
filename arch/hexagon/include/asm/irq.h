/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_IRQ_H_
#define _ASM_IRQ_H_

/* Number of first-level interrupts associated with the CPU core. */
#define HEXAGON_CPUINTS 32

/*
 * Must define NR_IRQS before including <asm-generic/irq.h>
 * 64 == the two SIRC's, 176 == the two gpio's
 *
 * IRQ configuration is still in flux; defining this to a comfortably
 * large number.
 */
#define NR_IRQS 512

#include <asm-generic/irq.h>

#endif
