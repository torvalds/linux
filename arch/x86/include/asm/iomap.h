#ifndef _ASM_X86_IOMAP_H
#define _ASM_X86_IOMAP_H

/*
 * Copyright © 2008 Ingo Molnar
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

void *
iomap_atomic_prot_pfn(unsigned long pfn, enum km_type type, pgprot_t prot);

void
iounmap_atomic(void *kvaddr, enum km_type type);

int
iomap_create_wc(resource_size_t base, unsigned long size, pgprot_t *prot);

void
iomap_free(resource_size_t base, unsigned long size);

#endif /* _ASM_X86_IOMAP_H */
