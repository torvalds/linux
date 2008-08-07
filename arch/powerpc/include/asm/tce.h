/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * Rewrite, cleanup:
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _ASM_POWERPC_TCE_H
#define _ASM_POWERPC_TCE_H
#ifdef __KERNEL__

#include <asm/iommu.h>

/*
 * Tces come in two formats, one for the virtual bus and a different
 * format for PCI
 */
#define TCE_VB  0
#define TCE_PCI 1

/* TCE page size is 4096 bytes (1 << 12) */

#define TCE_SHIFT	12
#define TCE_PAGE_SIZE	(1 << TCE_SHIFT)

#define TCE_ENTRY_SIZE		8		/* each TCE is 64 bits */

#define TCE_RPN_MASK		0xfffffffffful  /* 40-bit RPN (4K pages) */
#define TCE_RPN_SHIFT		12
#define TCE_VALID		0x800		/* TCE valid */
#define TCE_ALLIO		0x400		/* TCE valid for all lpars */
#define TCE_PCI_WRITE		0x2		/* write from PCI allowed */
#define TCE_PCI_READ		0x1		/* read from PCI allowed */
#define TCE_VB_WRITE		0x1		/* write from VB allowed */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_TCE_H */
