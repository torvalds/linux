/*
 *  Copyright (C) 2008 STMicroelectronics
 *  Copyright (C) 2009 ST-Ericsson.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef ASM_ARCH_IRQS_H
#define ASM_ARCH_IRQS_H

#include <mach/irqs-db5500.h>
#include <mach/irqs-db8500.h>

#define IRQ_LOCALTIMER                  29
#define IRQ_LOCALWDOG                   30

/* Shared Peripheral Interrupt (SHPI) */
#define IRQ_SHPI_START			32

/* Interrupt numbers generic for shared peripheral */
#define IRQ_MTU0		(IRQ_SHPI_START + 4)

/* There are 128 shared peripheral interrupts assigned to
 * INTID[160:32]. The first 32 interrupts are reserved.
 */
#define DBX500_NR_INTERNAL_IRQS		161

/* After chip-specific IRQ numbers we have the GPIO ones */
#define NOMADIK_NR_GPIO			288
#define NOMADIK_GPIO_TO_IRQ(gpio)	((gpio) + DBX500_NR_INTERNAL_IRQS)
#define NOMADIK_IRQ_TO_GPIO(irq)	((irq) - DBX500_NR_INTERNAL_IRQS)
#define IRQ_BOARD_START			NOMADIK_GPIO_TO_IRQ(NOMADIK_NR_GPIO)

/* This will be overridden by board-specific irq headers */
#define IRQ_BOARD_END			IRQ_BOARD_START

#ifdef CONFIG_MACH_U8500
#include <mach/irqs-board-mop500.h>
#endif

/*
 * After the board specific IRQ:s we reserve a range of IRQ:s in which virtual
 * IRQ:s representing modem IRQ:s can be allocated
 */
#define IRQ_MODEM_EVENTS_BASE (IRQ_BOARD_END + 1)
#define IRQ_MODEM_EVENTS_NBR 72
#define IRQ_MODEM_EVENTS_END (IRQ_MODEM_EVENTS_BASE + IRQ_MODEM_EVENTS_NBR)

/* List of virtual IRQ:s that are allocated from the range above */
#define MBOX_PAIR0_VIRT_IRQ (IRQ_MODEM_EVENTS_BASE + 43)
#define MBOX_PAIR1_VIRT_IRQ (IRQ_MODEM_EVENTS_BASE + 45)
#define MBOX_PAIR2_VIRT_IRQ (IRQ_MODEM_EVENTS_BASE + 41)

#define NR_IRQS				IRQ_MODEM_EVENTS_END

#endif /* ASM_ARCH_IRQS_H */
