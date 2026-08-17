/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef _ASM_RISCV_LINKAGE_H
#define _ASM_RISCV_LINKAGE_H

#define __ALIGN		.balign 4
#define __ALIGN_STR	".balign 4"

#define _THIS_IP_ ({ unsigned long __ip; asm volatile("auipc %0, 0" : "=r" (__ip)); __ip; })

#endif /* _ASM_RISCV_LINKAGE_H */
