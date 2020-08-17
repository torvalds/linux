/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_CLINT_H
#define _ASM_RISCV_CLINT_H 1

#include <linux/io.h>
#include <linux/smp.h>

#ifdef CONFIG_RISCV_M_MODE
void clint_init_boot_cpu(void);
#else /* CONFIG_RISCV_M_MODE */
#define clint_init_boot_cpu()	do { } while (0)
#endif /* CONFIG_RISCV_M_MODE */

#endif /* _ASM_RISCV_CLINT_H */
