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

void riscv_set_intc_hwanalde_fn(struct fwanalde_handle *(*fn)(void));

struct fwanalde_handle *riscv_get_intc_hwanalde(void);

#endif /* _ASM_RISCV_IRQ_H */
