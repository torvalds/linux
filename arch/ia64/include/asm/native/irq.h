/* SPDX-License-Identifier: GPL-2.0-or-later */
/******************************************************************************
 * arch/ia64/include/asm/native/irq.h
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 */

#ifndef _ASM_IA64_NATIVE_IRQ_H
#define _ASM_IA64_NATIVE_IRQ_H

#define NR_VECTORS	256

#if (NR_VECTORS + 32 * NR_CPUS) < 1024
#define IA64_NATIVE_NR_IRQS (NR_VECTORS + 32 * NR_CPUS)
#else
#define IA64_NATIVE_NR_IRQS 1024
#endif

#endif /* _ASM_IA64_NATIVE_IRQ_H */
