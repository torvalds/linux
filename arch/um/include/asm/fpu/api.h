/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_UM_FPU_API_H
#define _ASM_UM_FPU_API_H

#include <linux/types.h>

/* Copyright (c) 2020 Cambridge Greys Ltd
 * Copyright (c) 2020 Red Hat Inc.
 * A set of "dummy" defines to allow the direct inclusion
 * of x86 optimized copy, xor, etc routines into the
 * UML code tree. */

#define kernel_fpu_begin() (void)0
#define kernel_fpu_end() (void)0

static inline bool irq_fpu_usable(void)
{
	return true;
}


#endif
