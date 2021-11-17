/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Process execution related definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_EXEC_H
#define _ASM_EXEC_H

/*  Should probably shoot for an 8-byte aligned stack pointer  */
#define STACK_MASK (~7)
#define arch_align_stack(x) (x & STACK_MASK)

#endif /* _ASM_EXEC_H */
