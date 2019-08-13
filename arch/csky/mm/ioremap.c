// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/io.h>

pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
			      unsigned long size, pgprot_t vma_prot)
{
	if (!pfn_valid(pfn)) {
		return pgprot_noncached(vma_prot);
	} else if (file->f_flags & O_SYNC) {
		return pgprot_writecombine(vma_prot);
	}

	return vma_prot;
}
EXPORT_SYMBOL(phys_mem_access_prot);
