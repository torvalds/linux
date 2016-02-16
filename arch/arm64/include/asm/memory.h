/*
 * Based on arch/arm/include/asm/memory.h
 *
 * Copyright (C) 2000-2002 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Note: this file should not be included by non-asm/.h files
 */
#ifndef __ASM_MEMORY_H
#define __ASM_MEMORY_H

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>
#include <asm/sizes.h>

/*
 * Allow for constants defined here to be used from assembly code
 * by prepending the UL suffix only with actual C code compilation.
 */
#define UL(x) _AC(x, UL)

/*
 * Size of the PCI I/O space. This must remain a power of two so that
 * IO_SPACE_LIMIT acts as a mask for the low bits of I/O addresses.
 */
#define PCI_IO_SIZE		SZ_16M

/*
 * PAGE_OFFSET - the virtual address of the start of the kernel image (top
 *		 (VA_BITS - 1))
 * VA_BITS - the maximum number of bits for virtual addresses.
 * VA_START - the first kernel virtual address.
 * TASK_SIZE - the maximum size of a user space task.
 * TASK_UNMAPPED_BASE - the lower boundary of the mmap VM area.
 */
#define VA_BITS			(CONFIG_ARM64_VA_BITS)
#define VA_START		(UL(0xffffffffffffffff) << VA_BITS)
#define PAGE_OFFSET		(UL(0xffffffffffffffff) << (VA_BITS - 1))
#define KIMAGE_VADDR		(MODULES_END)
#define MODULES_END		(MODULES_VADDR + MODULES_VSIZE)
#define MODULES_VADDR		(VA_START + KASAN_SHADOW_SIZE)
#define MODULES_VSIZE		(SZ_64M)
#define PCI_IO_END		(PAGE_OFFSET - SZ_2M)
#define PCI_IO_START		(PCI_IO_END - PCI_IO_SIZE)
#define FIXADDR_TOP		(PCI_IO_START - SZ_2M)
#define TASK_SIZE_64		(UL(1) << VA_BITS)

#ifdef CONFIG_COMPAT
#define TASK_SIZE_32		UL(0x100000000)
#define TASK_SIZE		(test_thread_flag(TIF_32BIT) ? \
				TASK_SIZE_32 : TASK_SIZE_64)
#define TASK_SIZE_OF(tsk)	(test_tsk_thread_flag(tsk, TIF_32BIT) ? \
				TASK_SIZE_32 : TASK_SIZE_64)
#else
#define TASK_SIZE		TASK_SIZE_64
#endif /* CONFIG_COMPAT */

#define TASK_UNMAPPED_BASE	(PAGE_ALIGN(TASK_SIZE / 4))

/*
 * The size of the KASAN shadow region. This should be 1/8th of the
 * size of the entire kernel virtual address space.
 */
#ifdef CONFIG_KASAN
#define KASAN_SHADOW_SIZE	(UL(1) << (VA_BITS - 3))
#else
#define KASAN_SHADOW_SIZE	(0)
#endif

/*
 * Physical vs virtual RAM address space conversion.  These are
 * private definitions which should NOT be used outside memory.h
 * files.  Use virt_to_phys/phys_to_virt/__pa/__va instead.
 */
#define __virt_to_phys(x) ({						\
	phys_addr_t __x = (phys_addr_t)(x);				\
	__x >= PAGE_OFFSET ? (__x - PAGE_OFFSET + PHYS_OFFSET) :	\
			     (__x - KIMAGE_VADDR + PHYS_OFFSET); })

#define __phys_to_virt(x)	((unsigned long)((x) - PHYS_OFFSET + PAGE_OFFSET))
#define __phys_to_kimg(x)	((unsigned long)((x) - PHYS_OFFSET + KIMAGE_VADDR))

/*
 * Convert a page to/from a physical address
 */
#define page_to_phys(page)	(__pfn_to_phys(page_to_pfn(page)))
#define phys_to_page(phys)	(pfn_to_page(__phys_to_pfn(phys)))

/*
 * Memory types available.
 */
#define MT_DEVICE_nGnRnE	0
#define MT_DEVICE_nGnRE		1
#define MT_DEVICE_GRE		2
#define MT_NORMAL_NC		3
#define MT_NORMAL		4
#define MT_NORMAL_WT		5

/*
 * Memory types for Stage-2 translation
 */
#define MT_S2_NORMAL		0xf
#define MT_S2_DEVICE_nGnRE	0x1

#ifdef CONFIG_ARM64_4K_PAGES
#define IOREMAP_MAX_ORDER	(PUD_SHIFT)
#else
#define IOREMAP_MAX_ORDER	(PMD_SHIFT)
#endif

#ifndef __ASSEMBLY__

extern phys_addr_t		memstart_addr;
/* PHYS_OFFSET - the physical address of the start of memory. */
#define PHYS_OFFSET		({ memstart_addr; })

/*
 * The maximum physical address that the linear direct mapping
 * of system RAM can cover. (PAGE_OFFSET can be interpreted as
 * a 2's complement signed quantity and negated to derive the
 * maximum size of the linear mapping.)
 */
#define MAX_MEMBLOCK_ADDR	({ memstart_addr - PAGE_OFFSET - 1; })

/*
 * PFNs are used to describe any physical page; this means
 * PFN 0 == physical address 0.
 *
 * This is the PFN of the first RAM page in the kernel
 * direct-mapped view.  We assume this is the first page
 * of RAM in the mem_map as well.
 */
#define PHYS_PFN_OFFSET	(PHYS_OFFSET >> PAGE_SHIFT)

/*
 * Note: Drivers should NOT use these.  They are the wrong
 * translation for translating DMA addresses.  Use the driver
 * DMA support - see dma-mapping.h.
 */
#define virt_to_phys virt_to_phys
static inline phys_addr_t virt_to_phys(const volatile void *x)
{
	return __virt_to_phys((unsigned long)(x));
}

#define phys_to_virt phys_to_virt
static inline void *phys_to_virt(phys_addr_t x)
{
	return (void *)(__phys_to_virt(x));
}

/*
 * Drivers should NOT use these either.
 */
#define __pa(x)			__virt_to_phys((unsigned long)(x))
#define __va(x)			((void *)__phys_to_virt((phys_addr_t)(x)))
#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)
#define virt_to_pfn(x)      __phys_to_pfn(__virt_to_phys(x))

/*
 *  virt_to_page(k)	convert a _valid_ virtual address to struct page *
 *  virt_addr_valid(k)	indicates whether a virtual address is valid
 */
#define ARCH_PFN_OFFSET		((unsigned long)PHYS_PFN_OFFSET)

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define	virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#endif

#include <asm-generic/memory_model.h>

#endif
