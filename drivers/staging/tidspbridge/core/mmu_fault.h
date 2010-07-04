/*
 * mmu_fault.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Defines DSP MMU fault handling functions.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef MMU_FAULT_
#define MMU_FAULT_

/*
 *  ======== mmu_fault_dpc ========
 *      Deferred procedure call to handle DSP MMU fault.
 */
void mmu_fault_dpc(IN unsigned long pRefData);

/*
 *  ======== mmu_fault_isr ========
 *      ISR to be triggered by a DSP MMU fault interrupt.
 */
irqreturn_t mmu_fault_isr(int irq, IN void *pRefData);

#endif /* MMU_FAULT_ */
