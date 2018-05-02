/*
 *  arch/arm/include/asm/memory.h
 *
 *  Copyright (C) 2000-2002 Russell King
 *  modification for nommu, Hyok S. Choi, 2004
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Note: this file should not be included by non-asm/.h files
 */
#ifndef __ASM_ARM_MEMORY_H
#define __ASM_ARM_MEMORY_H

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>
#include <linux/sizes.h>

#ifdef CONFIG_NEED_MACH_MEMORY_H
#include <mach/memory.h>
#endif

/* PAGE_OFFSET - the virtual address of the start of the kernel image */
#define PAGE_OFFSET		UL(CONFIG_PAGE_OFFSET)

#ifdef CONFIG_MMU

/*
 * TASK_SIZE - the maximum size of a user space task.
 * TASK_UNMAPPED_BASE - the lower boundary of the mmap VM area
 */
#define TASK_SIZE		(UL(CONFIG_PAGE_OFFSET) - UL(SZ_16M))
#define TASK_UNMAPPED_BASE	ALIGN(TASK_SIZE / 3, SZ_16M)

/*
 * The maximum size of a 26-bit user space task.
 */
#define TASK_SIZE_26		(UL(1) << 26)

/*
 * The module space lives between the addresses given by TASK_SIZE
 * and PAGE_OFFSET - it must be within 32MB of the kernel text.
 */
#ifndef CONFIG_THUMB2_KERNEL
#define MODULES_VADDR		(PAGE_OFFSET - SZ_16M)
#else
/* smaller range for Thumb-2 symbols relocation (2^24)*/
#define MODULES_VADDR		(PAGE_OFFSET - SZ_8M)
#endif

#if TASK_SIZE > MODULES_VADDR
#error Top of user space clashes with start of module space
#endif

/*
 * The highmem pkmap virtual space shares the end of the module area.
 */
#ifdef CONFIG_HIGHMEM
#define MODULES_END		(PAGE_OFFSET - PMD_SIZE)
#else
#define MODULES_END		(PAGE_OFFSET)
#endif

/*
 * The XIP kernel gets mapped at the bottom of the module vm area.
 * Since we use sections to map it, this macro replaces the physical address
 * with its virtual address while keeping offset from the base section.
 */
#define XIP_VIRT_ADDR(physaddr)  (MODULES_VADDR + ((physaddr) & 0x000fffff))

#if !defined(CONFIG_SMP) && !defined(CONFIG_ARM_LPAE)
/*
 * Allow 16MB-aligned ioremap pages
 */
#define IOREMAP_MAX_ORDER	24
#endif

#define VECTORS_BASE		UL(0xffff0000)

#else /* CONFIG_MMU */

#ifndef __ASSEMBLY__
extern unsigned long setup_vectors_base(void);
extern unsigned long vectors_base;
#define VECTORS_BASE		vectors_base
#endif

/*
 * The limitation of user task size can grow up to the end of free ram region.
 * It is difficult to define and perhaps will never meet the original meaning
 * of this define that was meant to.
 * Fortunately, there is no reference for this in noMMU mode, for now.
 */
#define TASK_SIZE		UL(0xffffffff)

#ifndef TASK_UNMAPPED_BASE
#define TASK_UNMAPPED_BASE	UL(0x00000000)
#endif

#ifndef END_MEM
#define END_MEM     		(UL(CONFIG_DRAM_BASE) + CONFIG_DRAM_SIZE)
#endif

/*
 * The module can be at any place in ram in nommu mode.
 */
#define MODULES_END		(END_MEM)
#define MODULES_VADDR		PAGE_OFFSET

#define XIP_VIRT_ADDR(physaddr)  (physaddr)

#endif /* !CONFIG_MMU */

#ifdef CONFIG_XIP_KERNEL
#define KERNEL_START		_sdata
#else
#define KERNEL_START		_stext
#endif
#define KERNEL_END		_end

/*
 * We fix the TCM memories max 32 KiB ITCM resp DTCM at these
 * locations
 */
#ifdef CONFIG_HAVE_TCM
#define ITCM_OFFSET	UL(0xfffe0000)
#define DTCM_OFFSET	UL(0xfffe8000)
#endif

/*
 * Convert a page to/from a physical address
 */
#define page_to_phys(page)	(__pfn_to_phys(page_to_pfn(page)))
#define phys_to_page(phys)	(pfn_to_page(__phys_to_pfn(phys)))

/*
 * PLAT_PHYS_OFFSET is the offset (from zero) of the start of physical
 * memory.  This is used for XIP and NoMMU kernels, and on platforms that don't
 * have CONFIG_ARM_PATCH_PHYS_VIRT. Assembly code must always use
 * PLAT_PHYS_OFFSET and not PHYS_OFFSET.
 */
#define PLAT_PHYS_OFFSET	UL(CONFIG_PHYS_OFFSET)

#ifdef CONFIG_XIP_KERNEL
/*
 * When referencing data in RAM from the XIP region in a relative manner
 * with the MMU off, we need the relative offset between the two physical
 * addresses.  The macro below achieves this, which is:
 *    __pa(v_data) - __xip_pa(v_text)
 */
#define PHYS_RELATIVE(v_data, v_text) \
	(((v_data) - PAGE_OFFSET + PLAT_PHYS_OFFSET) - \
	 ((v_text) - XIP_VIRT_ADDR(CONFIG_XIP_PHYS_ADDR) + \
          CONFIG_XIP_PHYS_ADDR))
#else
#define PHYS_RELATIVE(v_data, v_text) ((v_data) - (v_text))
#endif

#ifndef __ASSEMBLY__

/*
 * Physical vs virtual RAM address space conversion.  These are
 * private definitions which should NOT be used outside memory.h
 * files.  Use virt_to_phys/phys_to_virt/__pa/__va instead.
 *
 * PFNs are used to describe any physical page; this means
 * PFN 0 == physical address 0.
 */

#if defined(CONFIG_ARM_PATCH_PHYS_VIRT)

/*
 * Constants used to force the right instruction encodings and shifts
 * so that all we need to do is modify the 8-bit constant field.
 */
#define __PV_BITS_31_24	0x81000000
#define __PV_BITS_7_0	0x81

extern unsigned long __pv_phys_pfn_offset;
extern u64 __pv_offset;
extern void fixup_pv_table(const void *, unsigned long);
extern const void *__pv_table_begin, *__pv_table_end;

#define PHYS_OFFSET	((phys_addr_t)__pv_phys_pfn_offset << PAGE_SHIFT)
#define PHYS_PFN_OFFSET	(__pv_phys_pfn_offset)

#define __pv_stub(from,to,instr,type)			\
	__asm__("@ __pv_stub\n"				\
	"1:	" instr "	%0, %1, %2\n"		\
	"	.pushsection .pv_table,\"a\"\n"		\
	"	.long	1b\n"				\
	"	.popsection\n"				\
	: "=r" (to)					\
	: "r" (from), "I" (type))

#define __pv_stub_mov_hi(t)				\
	__asm__ volatile("@ __pv_stub_mov\n"		\
	"1:	mov	%R0, %1\n"			\
	"	.pushsection .pv_table,\"a\"\n"		\
	"	.long	1b\n"				\
	"	.popsection\n"				\
	: "=r" (t)					\
	: "I" (__PV_BITS_7_0))

#define __pv_add_carry_stub(x, y)			\
	__asm__ volatile("@ __pv_add_carry_stub\n"	\
	"1:	adds	%Q0, %1, %2\n"			\
	"	adc	%R0, %R0, #0\n"			\
	"	.pushsection .pv_table,\"a\"\n"		\
	"	.long	1b\n"				\
	"	.popsection\n"				\
	: "+r" (y)					\
	: "r" (x), "I" (__PV_BITS_31_24)		\
	: "cc")

static inline phys_addr_t __virt_to_phys_nodebug(unsigned long x)
{
	phys_addr_t t;

	if (sizeof(phys_addr_t) == 4) {
		__pv_stub(x, t, "add", __PV_BITS_31_24);
	} else {
		__pv_stub_mov_hi(t);
		__pv_add_carry_stub(x, t);
	}
	return t;
}

static inline unsigned long __phys_to_virt(phys_addr_t x)
{
	unsigned long t;

	/*
	 * 'unsigned long' cast discard upper word when
	 * phys_addr_t is 64 bit, and makes sure that inline
	 * assembler expression receives 32 bit argument
	 * in place where 'r' 32 bit operand is expected.
	 */
	__pv_stub((unsigned long) x, t, "sub", __PV_BITS_31_24);
	return t;
}

#else

#define PHYS_OFFSET	PLAT_PHYS_OFFSET
#define PHYS_PFN_OFFSET	((unsigned long)(PHYS_OFFSET >> PAGE_SHIFT))

static inline phys_addr_t __virt_to_phys_nodebug(unsigned long x)
{
	return (phys_addr_t)x - PAGE_OFFSET + PHYS_OFFSET;
}

static inline unsigned long __phys_to_virt(phys_addr_t x)
{
	return x - PHYS_OFFSET + PAGE_OFFSET;
}

#endif

#define virt_to_pfn(kaddr) \
	((((unsigned long)(kaddr) - PAGE_OFFSET) >> PAGE_SHIFT) + \
	 PHYS_PFN_OFFSET)

#define __pa_symbol_nodebug(x)	__virt_to_phys_nodebug((x))

#ifdef CONFIG_DEBUG_VIRTUAL
extern phys_addr_t __virt_to_phys(unsigned long x);
extern phys_addr_t __phys_addr_symbol(unsigned long x);
#else
#define __virt_to_phys(x)	__virt_to_phys_nodebug(x)
#define __phys_addr_symbol(x)	__pa_symbol_nodebug(x)
#endif

/*
 * These are *only* valid on the kernel direct mapped RAM memory.
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
	return (void *)__phys_to_virt(x);
}

/*
 * Drivers should NOT use these either.
 */
#define __pa(x)			__virt_to_phys((unsigned long)(x))
#define __pa_symbol(x)		__phys_addr_symbol(RELOC_HIDE((unsigned long)(x), 0))
#define __va(x)			((void *)__phys_to_virt((phys_addr_t)(x)))
#define pfn_to_kaddr(pfn)	__va((phys_addr_t)(pfn) << PAGE_SHIFT)

extern long long arch_phys_to_idmap_offset;

/*
 * These are for systems that have a hardware interconnect supported alias
 * of physical memory for idmap purposes.  Most cases should leave these
 * untouched.  Note: this can only return addresses less than 4GiB.
 */
static inline bool arm_has_idmap_alias(void)
{
	return IS_ENABLED(CONFIG_MMU) && arch_phys_to_idmap_offset != 0;
}

#define IDMAP_INVALID_ADDR ((u32)~0)

static inline unsigned long phys_to_idmap(phys_addr_t addr)
{
	if (IS_ENABLED(CONFIG_MMU) && arch_phys_to_idmap_offset) {
		addr += arch_phys_to_idmap_offset;
		if (addr > (u32)~0)
			addr = IDMAP_INVALID_ADDR;
	}
	return addr;
}

static inline phys_addr_t idmap_to_phys(unsigned long idmap)
{
	phys_addr_t addr = idmap;

	if (IS_ENABLED(CONFIG_MMU) && arch_phys_to_idmap_offset)
		addr -= arch_phys_to_idmap_offset;

	return addr;
}

static inline unsigned long __virt_to_idmap(unsigned long x)
{
	return phys_to_idmap(__virt_to_phys(x));
}

#define virt_to_idmap(x)	__virt_to_idmap((unsigned long)(x))

/*
 * Virtual <-> DMA view memory address translations
 * Again, these are *only* valid on the kernel direct mapped RAM
 * memory.  Use of these is *deprecated* (and that doesn't mean
 * use the __ prefixed forms instead.)  See dma-mapping.h.
 */
#ifndef __virt_to_bus
#define __virt_to_bus	__virt_to_phys
#define __bus_to_virt	__phys_to_virt
#define __pfn_to_bus(x)	__pfn_to_phys(x)
#define __bus_to_pfn(x)	__phys_to_pfn(x)
#endif

/*
 * Conversion between a struct page and a physical address.
 *
 *  page_to_pfn(page)	convert a struct page * to a PFN number
 *  pfn_to_page(pfn)	convert a _valid_ PFN number to struct page *
 *
 *  virt_to_page(k)	convert a _valid_ virtual address to struct page *
 *  virt_addr_valid(k)	indicates whether a virtual address is valid
 */
#define ARCH_PFN_OFFSET		PHYS_PFN_OFFSET

#define virt_to_page(kaddr)	pfn_to_page(virt_to_pfn(kaddr))
#define virt_addr_valid(kaddr)	(((unsigned long)(kaddr) >= PAGE_OFFSET && (unsigned long)(kaddr) < (unsigned long)high_memory) \
					&& pfn_valid(virt_to_pfn(kaddr)))

#endif

#include <asm-generic/memory_model.h>

#endif
