/*
 * Copyright (C) 2006 Hollis Blanchard <hollisb@us.ibm.com>, IBM Corporation
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

#ifndef _ASM_IA64_XEN_XENCOMM_H
#define _ASM_IA64_XEN_XENCOMM_H

#include <xen/xencomm.h>
#include <asm/pgtable.h>

/* Must be called before any hypercall.  */
extern void xencomm_initialize(void);
extern int xencomm_is_initialized(void);

/* Check if virtual contiguity means physical contiguity
 * where the passed address is a pointer value in virtual address.
 * On ia64, identity mapping area in region 7 or the piece of region 5
 * that is mapped by itr[IA64_TR_KERNEL]/dtr[IA64_TR_KERNEL]
 */
static inline int xencomm_is_phys_contiguous(unsigned long addr)
{
	return (PAGE_OFFSET <= addr &&
		addr < (PAGE_OFFSET + (1UL << IA64_MAX_PHYS_BITS))) ||
		(KERNEL_START <= addr &&
		 addr < KERNEL_START + KERNEL_TR_PAGE_SIZE);
}

#endif /* _ASM_IA64_XEN_XENCOMM_H */
