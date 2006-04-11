/**
 * \file drm_memory.h
 * Memory management wrappers for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Thu Feb  4 14:00:34 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/config.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#include "drmP.h"

/**
 * Cut down version of drm_memory_debug.h, which used to be called
 * drm_memory.h.
 */

#if __OS_HAS_AGP

#include <linux/vmalloc.h>

#ifdef HAVE_PAGE_AGP
#include <asm/agp.h>
#else
# ifdef __powerpc__
#  define PAGE_AGP	__pgprot(_PAGE_KERNEL | _PAGE_NO_CACHE)
# else
#  define PAGE_AGP	PAGE_KERNEL
# endif
#endif

static inline unsigned long drm_follow_page(void *vaddr)
{
	pgd_t *pgd = pgd_offset_k((unsigned long)vaddr);
	pud_t *pud = pud_offset(pgd, (unsigned long)vaddr);
	pmd_t *pmd = pmd_offset(pud, (unsigned long)vaddr);
	pte_t *ptep = pte_offset_kernel(pmd, (unsigned long)vaddr);
	return pte_pfn(*ptep) << PAGE_SHIFT;
}

#else				/* __OS_HAS_AGP */

static inline unsigned long drm_follow_page(void *vaddr)
{
	return 0;
}

#endif

void *drm_ioremap(unsigned long offset, unsigned long size,
				drm_device_t * dev);

void drm_ioremapfree(void *pt, unsigned long size,
				   drm_device_t * dev);
