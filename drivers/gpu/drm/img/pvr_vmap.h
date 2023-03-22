/*
 * @File        pvr_vmap.h
 * @Title       Utility functions for virtual memory mapping
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef PVR_VMAP_H
#define PVR_VMAP_H

#include <linux/version.h>
#include <linux/vmalloc.h>





#ifndef CACHE_TEST
#define RISCV_STARFIVE_CACHE_COHERENT_FIX
#endif
#ifndef RISCV_STARFIVE_CACHE_COHERENT_FIX
static inline void *pvr_vmap(struct page **pages,
			     unsigned int count,
			     __maybe_unused unsigned long flags,
			     pgprot_t prot)
{
#if !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS)
	return vmap(pages, count, flags, prot);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
	return vm_map_ram(pages, count, -1, prot);
#else
	if (pgprot_val(prot) == pgprot_val(PAGE_KERNEL))
		return vm_map_ram(pages, count, -1);
	else
		return vmap(pages, count, flags, prot);
#endif /* !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS) */
}

static inline void pvr_vunmap(void *pages,
			      __maybe_unused unsigned int count,
			      __maybe_unused pgprot_t prot)
{
#if !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS)
	vunmap(pages);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
	vm_unmap_ram(pages, count);
#else
	if (pgprot_val(prot) == pgprot_val(PAGE_KERNEL))
		vm_unmap_ram(pages, count);
	else
		vunmap(pages);
#endif /* !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS) */
}
#else
extern void *riscv_vmap(struct page **pages, unsigned int count,
	   unsigned long flags, pgprot_t prot, int cached);
static inline void *pvr_vmap(struct page **pages,
			     unsigned int count,
			     __maybe_unused unsigned long flags,
			     pgprot_t prot)
{
	return riscv_vmap(pages, count, flags, prot, 0);
}

static inline void *pvr_vmap_cached(struct page **pages,
			     unsigned int count,
			     __maybe_unused unsigned long flags,
			     pgprot_t prot)
{
	return riscv_vmap(pages, count, flags, prot, 1);
}

static inline void pvr_vunmap(void *pages,
			      __maybe_unused unsigned int count,
			      __maybe_unused pgprot_t prot)
{
	vunmap(pages);
}
#endif /* RISCV_STARFIVE_CACHE_COHERENT_FIX */

#endif /* PVR_VMAP_H */
