/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * IBM PPC4xx UIC external definitions and structure.
 *
 * Maintainer: David Gibson <dwg@au1.ibm.com>
 * Copyright 2007 IBM Corporation.
 */
#ifndef _ASM_POWERPC_UIC_H
#define _ASM_POWERPC_UIC_H

#ifdef __KERNEL__

extern void __init uic_init_tree(void);
extern unsigned int uic_get_irq(void);

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_UIC_H */
