/* MN103E010 On-board interrupt controller numbers
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_PROC_IRQ_H
#define _ASM_PROC_IRQ_H

#ifdef __KERNEL__

#define GxICR_NUM_IRQS		42

#define GxICR_NUM_XIRQS		8

#define XIRQ0		34
#define XIRQ1		35
#define XIRQ2		36
#define XIRQ3		37
#define XIRQ4		38
#define XIRQ5		39
#define XIRQ6		40
#define XIRQ7		41

#define XIRQ2IRQ(num)	(XIRQ0 + num)

#endif /* __KERNEL__ */

#endif /* _ASM_PROC_IRQ_H */
