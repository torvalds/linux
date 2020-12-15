/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_X86_IOMAP_H
#define _ASM_X86_IOMAP_H

/*
 * Copyright © 2008 Ingo Molnar
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

void __iomem *
iomap_atomic_prot_pfn(unsigned long pfn, pgprot_t prot);

void
iounmap_atomic(void __iomem *kvaddr);

int
iomap_create_wc(resource_size_t base, unsigned long size, pgprot_t *prot);

void
iomap_free(resource_size_t base, unsigned long size);

#endif /* _ASM_X86_IOMAP_H */
