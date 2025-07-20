/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Limited
 */
#ifndef __ASM_VDSO_H
#define __ASM_VDSO_H

#define __VDSO_PAGES    4

#ifndef __ASSEMBLY__

#include <generated/vdso-offsets.h>

#define VDSO_SYMBOL(base, name)						   \
({									   \
	(void *)(vdso_offset_##name + (unsigned long)(base)); \
})

extern char vdso_start[], vdso_end[];
extern char vdso32_start[], vdso32_end[];

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_H */
