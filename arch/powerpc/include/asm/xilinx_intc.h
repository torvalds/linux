/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Xilinx intc external definitions
 *
 * Copyright 2007 Secret Lab Technologies Ltd.
 */
#ifndef _ASM_POWERPC_XILINX_INTC_H
#define _ASM_POWERPC_XILINX_INTC_H

#ifdef __KERNEL__

extern void __init xilinx_intc_init_tree(void);
extern unsigned int xintc_get_irq(void);

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_XILINX_INTC_H */
