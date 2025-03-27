/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _VDSO_DATAPAGE_H
#define _VDSO_DATAPAGE_H
#ifdef __KERNEL__

/*
 * Copyright (C) 2002 Peter Bergner <bergner@vnet.ibm.com>, IBM
 * Copyright (C) 2005 Benjamin Herrenschmidy <benh@kernel.crashing.org>,
 * 		      IBM Corp.
 */

#ifndef __ASSEMBLY__

#include <vdso/datapage.h>

#else /* __ASSEMBLY__ */

.macro get_datapage ptr symbol
	bcl	20, 31, .+4
999:
	mflr	\ptr
	addis	\ptr, \ptr, (\symbol - 999b)@ha
	addi	\ptr, \ptr, (\symbol - 999b)@l
.endm

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _SYSTEMCFG_H */
