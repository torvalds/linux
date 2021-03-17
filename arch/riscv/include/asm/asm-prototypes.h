/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_PROTOTYPES_H
#define _ASM_RISCV_PROTOTYPES_H

#include <linux/ftrace.h>
#include <asm-generic/asm-prototypes.h>

long long __lshrti3(long long a, int b);
long long __ashrti3(long long a, int b);
long long __ashlti3(long long a, int b);

#endif /* _ASM_RISCV_PROTOTYPES_H */
