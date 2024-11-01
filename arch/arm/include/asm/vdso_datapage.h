/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Adapted from arm64 version.
 *
 * Copyright (C) 2012 ARM Limited
 */
#ifndef __ASM_VDSO_DATAPAGE_H
#define __ASM_VDSO_DATAPAGE_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <vdso/datapage.h>
#include <asm/page.h>

union vdso_data_store {
	struct vdso_data	data[CS_BASES];
	u8			page[PAGE_SIZE];
};

#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __ASM_VDSO_DATAPAGE_H */
