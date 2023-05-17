/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_IRQ_H
#define _ASM_RISCV_IRQ_H

#include <linux/interrupt.h>
#include <linux/linkage.h>

#include <asm-generic/irq.h>

void riscv_set_intc_hwnode_fn(struct fwnode_handle *(*fn)(void));

struct fwnode_handle *riscv_get_intc_hwnode(void);

extern void __init init_IRQ(void);

#endif /* _ASM_RISCV_IRQ_H */
