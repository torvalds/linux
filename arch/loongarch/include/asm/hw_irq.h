/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_HW_IRQ_H
#define __ASM_HW_IRQ_H

#include <linux/atomic.h>

extern atomic_t irq_err_count;

/*
 * 256 vectors Map:
 *
 * 0 - 15: mapping legacy IPs, e.g. IP0-12.
 * 16 - 255: mapping a vector for external IRQ.
 *
 */
#define NR_VECTORS		256
#define IRQ_MATRIX_BITS		NR_VECTORS
#define NR_LEGACY_VECTORS	16
/*
 * interrupt-retrigger: NOP for now. This may not be appropriate for all
 * machines, we'll see ...
 */

#endif /* __ASM_HW_IRQ_H */
