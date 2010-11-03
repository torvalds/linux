/*
 * fixmap.h: compile-time virtual memory allocation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ingo Molnar
 *
 * Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 * x86_32 and x86_64 integration by Gustavo F. Padovan, February 2009
 */

#ifndef _ASM_X86_FIXMAP_H
#define _ASM_X86_FIXMAP_H

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#include <asm/acpi.h>
#include <asm/apicdef.h>
#include <asm/page.h>
#ifdef CONFIG_X86_32
#include <linux/threads.h>
#include <asm/kmap_types.h>
#else
#include <asm/vsyscall.h>
#endif

/*
 * We can't declare FIXADDR_TOP as variable for x86_64 because vsyscall
 * uses fixmaps that relies on FIXADDR_TOP for proper address calculation.
 * Because of this, FIXADDR_TOP x86 integration was left as later work.
 */
#ifdef CONFIG_X86_32
/* used by vmalloc.c, vsyscall.lds.S.
 *
 * Leave one empty page between vmalloc'ed areas and
 * the start of the fixmap.
 */
extern unsigned long __FIXADDR_TOP;
#define FIXADDR_TOP	((unsigned long)__FIXADDR_TOP)

#define FIXADDR_USER_START     __fix_to_virt(FIX_VDSO)
#define FIXADDR_USER_END       __fix_to_virt(FIX_VDSO - 1)
#else
#define FIXADDR_TOP	(VSYSCALL_END-PAGE_SIZE)

/* Only covers 32bit vsyscalls currently. Need another set for 64bit. */
#define FIXADDR_USER_START	((unsigned long)VSYSCALL32_VSYSCALL)
#define FIXADDR_USER_END	(FIXADDR_USER_START + PAGE_SIZE)
#endif


/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process.
 * for x86_32: We allocate these special addresses
 * from the end of virtual memory (0xfffff000) backwards.
 * Also this lets us do fail-safe vmalloc(), we
 * can guarantee that these special addresses and
 * vmalloc()-ed addresses never overlap.
 *
 * These 'compile-time allocated' memory buffers are
 * fixed-size 4k pages (or larger if used with an increment
 * higher than 1). Use set_fixmap(idx,phys) to associate
 * physical memory with fixmap indices.
 *
 * TLB entries of such buffers will not be flushed across
 * task switches.
 */
enum fixed_addresses {
#ifdef CONFIG_X86_32
	FIX_HOLE,
	FIX_VDSO,
#else
	VSYSCALL_LAST_PAGE,
	VSYSCALL_FIRST_PAGE = VSYSCALL_LAST_PAGE
			    + ((VSYSCALL_END-VSYSCALL_START) >> PAGE_SHIFT) - 1,
	VSYSCALL_HPET,
#endif
	FIX_DBGP_BASE,
	FIX_EARLYCON_MEM_BASE,
#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	FIX_OHCI1394_BASE,
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	FIX_APIC_BASE,	/* local (CPU) APIC) -- required for SMP or not */
#endif
#ifdef CONFIG_X86_IO_APIC
	FIX_IO_APIC_BASE_0,
	FIX_IO_APIC_BASE_END = FIX_IO_APIC_BASE_0 + MAX_IO_APICS - 1,
#endif
#ifdef CONFIG_X86_VISWS_APIC
	FIX_CO_CPU,	/* Cobalt timer */
	FIX_CO_APIC,	/* Cobalt APIC Redirection Table */
	FIX_LI_PCIA,	/* Lithium PCI Bridge A */
	FIX_LI_PCIB,	/* Lithium PCI Bridge B */
#endif
#ifdef CONFIG_X86_F00F_BUG
	FIX_F00F_IDT,	/* Virtual mapping for IDT */
#endif
#ifdef CONFIG_X86_CYCLONE_TIMER
	FIX_CYCLONE_TIMER, /*cyclone timer register*/
#endif
#ifdef CONFIG_X86_32
	FIX_KMAP_BEGIN,	/* reserved pte's for temporary kernel mappings */
	FIX_KMAP_END = FIX_KMAP_BEGIN+(KM_TYPE_NR*NR_CPUS)-1,
#ifdef CONFIG_PCI_MMCONFIG
	FIX_PCIE_MCFG,
#endif
#endif
#ifdef CONFIG_PARAVIRT
	FIX_PARAVIRT_BOOTMAP,
#endif
	FIX_TEXT_POKE1,	/* reserve 2 pages for text_poke() */
	FIX_TEXT_POKE0, /* first page is last, because allocation is backward */
	__end_of_permanent_fixed_addresses,
	/*
	 * 256 temporary boot-time mappings, used by early_ioremap(),
	 * before ioremap() is functional.
	 *
	 * If necessary we round it up to the next 256 pages boundary so
	 * that we can have a single pgd entry and a single pte table:
	 */
#define NR_FIX_BTMAPS		64
#define FIX_BTMAPS_SLOTS	4
#define TOTAL_FIX_BTMAPS	(NR_FIX_BTMAPS * FIX_BTMAPS_SLOTS)
	FIX_BTMAP_END =
	 (__end_of_permanent_fixed_addresses ^
	  (__end_of_permanent_fixed_addresses + TOTAL_FIX_BTMAPS - 1)) &
	 -PTRS_PER_PTE
	 ? __end_of_permanent_fixed_addresses + TOTAL_FIX_BTMAPS -
	   (__end_of_permanent_fixed_addresses & (TOTAL_FIX_BTMAPS - 1))
	 : __end_of_permanent_fixed_addresses,
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + TOTAL_FIX_BTMAPS - 1,
#ifdef CONFIG_X86_32
	FIX_WP_TEST,
#endif
#ifdef CONFIG_INTEL_TXT
	FIX_TBOOT_BASE,
#endif
	__end_of_fixed_addresses
};


extern void reserve_top_address(unsigned long reserve);

#define FIXADDR_SIZE	(__end_of_permanent_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_BOOT_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START		(FIXADDR_TOP - FIXADDR_SIZE)
#define FIXADDR_BOOT_START	(FIXADDR_TOP - FIXADDR_BOOT_SIZE)

extern int fixmaps_set;

extern pte_t *kmap_pte;
extern pgprot_t kmap_prot;
extern pte_t *pkmap_page_table;

void __native_set_fixmap(enum fixed_addresses idx, pte_t pte);
void native_set_fixmap(enum fixed_addresses idx,
		       phys_addr_t phys, pgprot_t flags);

#ifndef CONFIG_PARAVIRT
static inline void __set_fixmap(enum fixed_addresses idx,
				phys_addr_t phys, pgprot_t flags)
{
	native_set_fixmap(idx, phys, flags);
}
#endif

#define set_fixmap(idx, phys)				\
	__set_fixmap(idx, phys, PAGE_KERNEL)

/*
 * Some hardware wants to get fixmapped without caching.
 */
#define set_fixmap_nocache(idx, phys)			\
	__set_fixmap(idx, phys, PAGE_KERNEL_NOCACHE)

#define clear_fixmap(idx)			\
	__set_fixmap(idx, 0, __pgprot(0))

#define __fix_to_virt(x)	(FIXADDR_TOP - ((x) << PAGE_SHIFT))
#define __virt_to_fix(x)	((FIXADDR_TOP - ((x)&PAGE_MASK)) >> PAGE_SHIFT)

extern void __this_fixmap_does_not_exist(void);

/*
 * 'index to address' translation. If anyone tries to use the idx
 * directly without translation, we catch the bug with a NULL-deference
 * kernel oops. Illegal ranges of incoming indices are caught too.
 */
static __always_inline unsigned long fix_to_virt(const unsigned int idx)
{
	/*
	 * this branch gets completely eliminated after inlining,
	 * except when someone tries to use fixaddr indices in an
	 * illegal way. (such as mixing up address types or using
	 * out-of-range indices).
	 *
	 * If it doesn't get removed, the linker will complain
	 * loudly with a reasonably clear error message..
	 */
	if (idx >= __end_of_fixed_addresses)
		__this_fixmap_does_not_exist();

	return __fix_to_virt(idx);
}

static inline unsigned long virt_to_fix(const unsigned long vaddr)
{
	BUG_ON(vaddr >= FIXADDR_TOP || vaddr < FIXADDR_START);
	return __virt_to_fix(vaddr);
}

/* Return an pointer with offset calculated */
static inline unsigned long __set_fixmap_offset(enum fixed_addresses idx,
				phys_addr_t phys, pgprot_t flags)
{
	__set_fixmap(idx, phys, flags);
	return fix_to_virt(idx) + (phys & (PAGE_SIZE - 1));
}

#define set_fixmap_offset(idx, phys)			\
	__set_fixmap_offset(idx, phys, PAGE_KERNEL)

#define set_fixmap_offset_nocache(idx, phys)			\
	__set_fixmap_offset(idx, phys, PAGE_KERNEL_NOCACHE)

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_FIXMAP_H */
