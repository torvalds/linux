/* Support for the SMC91C111 NIC on an ASB2303
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_UNIT_SMC91111_H
#define _ASM_UNIT_SMC91111_H

#include <asm/intctl-regs.h>

#define SMC91111_BASE		0xAA000300UL
#define SMC91111_BASE_END	0xAA000400UL
#define SMC91111_IRQ		XIRQ3

#define SMC_CAN_USE_8BIT	0
#define SMC_CAN_USE_16BIT	1
#define SMC_CAN_USE_32BIT	0
#define SMC_NOWAIT		1
#define SMC_IRQ_FLAGS		(0)

#if SMC_CAN_USE_8BIT
#define SMC_inb(a, r)		inb((unsigned long) ((a) + (r)))
#define SMC_outb(v, a, r)	outb(v, (unsigned long) ((a) + (r)))
#endif

#if SMC_CAN_USE_16BIT
#define SMC_inw(a, r)		inw((unsigned long) ((a) + (r)))
#define SMC_outw(lp, v, a, r)	outw(v, (unsigned long) ((a) + (r)))
#define SMC_insw(a, r, p, l)	insw((unsigned long) ((a) + (r)), (p), (l))
#define SMC_outsw(a, r, p, l)	outsw((unsigned long) ((a) + (r)), (p), (l))
#endif

#if SMC_CAN_USE_32BIT
#define SMC_inl(a, r)		inl((unsigned long) ((a) + (r)))
#define SMC_outl(v, a, r)	outl(v, (unsigned long) ((a) + (r)))
#define SMC_insl(a, r, p, l)	insl((unsigned long) ((a) + (r)), (p), (l))
#define SMC_outsl(a, r, p, l)	outsl((unsigned long) ((a) + (r)), (p), (l))
#endif

#define RPC_LSA_DEFAULT		RPC_LED_100_10
#define RPC_LSB_DEFAULT		RPC_LED_TX_RX

#define set_irq_type(irq, type)

#endif /*  _ASM_UNIT_SMC91111_H */
