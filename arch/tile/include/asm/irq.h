/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_IRQ_H
#define _ASM_TILE_IRQ_H

#include <linux/hardirq.h>

/* The hypervisor interface provides 32 IRQs. */
#define NR_IRQS 32

/* IRQ numbers used for linux IPIs. */
#define IRQ_RESCHEDULE 0

#define irq_canonicalize(irq)   (irq)

void ack_bad_irq(unsigned int irq);

/*
 * Different ways of handling interrupts.  Tile interrupts are always
 * per-cpu; there is no global interrupt controller to implement
 * enable/disable.  Most onboard devices can send their interrupts to
 * many tiles at the same time, and Tile-specific drivers know how to
 * deal with this.
 *
 * However, generic devices (usually PCIE based, sometimes GPIO)
 * expect that interrupts will fire on a single core at a time and
 * that the irq can be enabled or disabled from any core at any time.
 * We implement this by directing such interrupts to a single core.
 *
 * One added wrinkle is that PCI interrupts can be either
 * hardware-cleared (legacy interrupts) or software cleared (MSI).
 * Other generic device systems (GPIO) are always software-cleared.
 *
 * The enums below are used by drivers for onboard devices, including
 * the internals of PCI root complex and GPIO.  They allow the driver
 * to tell the generic irq code what kind of interrupt is mapped to a
 * particular IRQ number.
 */
enum {
	/* per-cpu interrupt; use enable/disable_percpu_irq() to mask */
	TILE_IRQ_PERCPU,
	/* global interrupt, hardware responsible for clearing. */
	TILE_IRQ_HW_CLEAR,
	/* global interrupt, software responsible for clearing. */
	TILE_IRQ_SW_CLEAR,
};


/*
 * Paravirtualized drivers should call this when they dynamically
 * allocate a new IRQ or discover an IRQ that was pre-allocated by the
 * hypervisor for use with their particular device.  This gives the
 * IRQ subsystem an opportunity to do interrupt-type-specific
 * initialization.
 *
 * ISSUE: We should modify this API so that registering anything
 * except percpu interrupts also requires providing callback methods
 * for enabling and disabling the interrupt.  This would allow the
 * generic IRQ code to proxy enable/disable_irq() calls back into the
 * PCI subsystem, which in turn could enable or disable the interrupt
 * at the PCI shim.
 */
void tile_irq_activate(unsigned int irq, int tile_irq_type);

void setup_irq_regs(void);

#endif /* _ASM_TILE_IRQ_H */
