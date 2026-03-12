/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#define mb() asm volatile ("l.msync" ::: "memory")

#define nop() asm volatile ("l.nop")

#include <asm-generic/barrier.h>

#endif /* __ASM_BARRIER_H */
