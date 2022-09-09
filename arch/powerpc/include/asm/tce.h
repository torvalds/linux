/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * Rewrite, cleanup:
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 */

#ifndef _ASM_POWERPC_TCE_H
#define _ASM_POWERPC_TCE_H
#ifdef __KERNEL__

#include <asm/iommu.h>

/*
 * Tces come in two formats, one for the virtual bus and a different
 * format for PCI.  PCI TCEs can have hardware or software maintianed
 * coherency.
 */
#define TCE_VB			0
#define TCE_PCI			1

#define TCE_ENTRY_SIZE		8		/* each TCE is 64 bits */
#define TCE_VALID		0x800		/* TCE valid */
#define TCE_ALLIO		0x400		/* TCE valid for all lpars */
#define TCE_PCI_WRITE		0x2		/* write from PCI allowed */
#define TCE_PCI_READ		0x1		/* read from PCI allowed */
#define TCE_VB_WRITE		0x1		/* write from VB allowed */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_TCE_H */
