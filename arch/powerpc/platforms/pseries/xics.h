/*
 * arch/powerpc/platforms/pseries/xics.h
 *
 * Copyright 2000 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#ifndef _POWERPC_KERNEL_XICS_H
#define _POWERPC_KERNEL_XICS_H

extern void xics_init_IRQ(void);
extern void xics_setup_cpu(void);
extern void xics_teardown_cpu(void);
extern void xics_kexec_teardown_cpu(int secondary);
extern void xics_migrate_irqs_away(void);
extern int smp_xics_probe(void);
extern void smp_xics_message_pass(int target, int msg);

#endif /* _POWERPC_KERNEL_XICS_H */
