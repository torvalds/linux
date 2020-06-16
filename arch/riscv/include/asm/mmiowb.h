/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_MMIOWB_H
#define _ASM_RISCV_MMIOWB_H

/*
 * "o,w" is sufficient to ensure that all writes to the device have completed
 * before the write to the spinlock is allowed to commit.
 */
#define mmiowb()	__asm__ __volatile__ ("fence o,w" : : : "memory");

#include <linux/smp.h>
#include <asm-generic/mmiowb.h>

#endif	/* _ASM_RISCV_MMIOWB_H */
