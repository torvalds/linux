/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2005-2017 Andes Technology Corporation
 */

#ifndef __ASM_VDSO_H
#define __ASM_VDSO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <generated/vdso-offsets.h>

#define VDSO_SYMBOL(base, name)						   \
({									   \
	(unsigned long)(vdso_offset_##name + (unsigned long)(base)); \
})

#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __ASM_VDSO_H */
