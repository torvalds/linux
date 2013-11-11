/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_STRING_H
#define _ASM_C6X_STRING_H

#include <asm/page.h>
#include <linux/linkage.h>

asmlinkage extern void *memcpy(void *to, const void *from, size_t n);

#define __HAVE_ARCH_MEMCPY

#endif /* _ASM_C6X_STRING_H */
