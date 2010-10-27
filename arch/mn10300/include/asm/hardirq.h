/* MN10300 Hardware IRQ statistics and management
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Modified by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_HARDIRQ_H
#define _ASM_HARDIRQ_H

#include <linux/threads.h>
#include <linux/irq.h>
#include <asm/exceptions.h>

/* assembly code in softirq.h is sensitive to the offsets of these fields */
typedef struct {
	unsigned int	__softirq_pending;
#ifdef CONFIG_MN10300_WD_TIMER
	unsigned int	__nmi_count;	/* arch dependent */
	unsigned int	__irq_count;	/* arch dependent */
#endif
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

extern void ack_bad_irq(int irq);

/*
 * manipulate stubs in the MN10300 CPU Trap/Interrupt Vector table
 * - these should jump to __common_exception in entry.S unless there's a good
 *   reason to do otherwise (see trap_preinit() in traps.c)
 */
typedef void (*intr_stub_fnx)(struct pt_regs *regs,
			      enum exception_code intcode);

/*
 * manipulate pointers in the Exception table (see entry.S)
 * - these are indexed by decoding the lower 24 bits of the TBR register
 * - note that the MN103E010 doesn't always trap through the correct vector,
 *   but does always set the TBR correctly
 */
extern asmlinkage void set_excp_vector(enum exception_code code,
				       intr_stub_fnx handler);

#endif /* _ASM_HARDIRQ_H */
