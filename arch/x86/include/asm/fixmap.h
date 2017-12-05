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
#include <uapi/asm/vsyscall.h>
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
#else
#define FIXADDR_TOP	(round_up(VSYSCALL_ADDR + PAGE_SIZE, 1<<PMD_SHIFT) - \
			 PAGE_SIZE)
#endif

/*
 * cpu_entry_area is a percpu region in the fixmap that contains things
 * needed by the CPU and early entry/exit code.  Real types aren't used
 * for all fields here to avoid circular header dependencies.
 *
 * Every field is a virtual alias of some other allocated backing store.
 * There is no direct allocation of a struct cpu_entry_area.
 */
struct cpu_entry_area {
	char gdt[PAGE_SIZE];

	/*
	 * The GDT is just below entry_stack and thus serves (on x86_64) as
	 * a a read-only guard page.
	 */
	struct entry_stack_page entry_stack_page;

	/*
	 * On x86_64, the TSS is mapped RO.  On x86_32, it's mapped RW because
	 * we need task switches to work, and task switches write to the TSS.
	 */
	struct tss_struct tss;

	char entry_trampoline[PAGE_SIZE];

#ifdef CONFIG_X86_64
	/*
	 * Exception stacks used for IST entries.
	 *
	 * In the future, this should have a separate slot for each stack
	 * with guard pages between them.
	 */
	char exception_stacks[(N_EXCEPTION_STACKS - 1) * EXCEPTION_STKSZ + DEBUG_STKSZ];
#endif
};

#define CPU_ENTRY_AREA_PAGES (sizeof(struct cpu_entry_area) / PAGE_SIZE)

extern void setup_cpu_entry_areas(void);

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
#else
#ifdef CONFIG_X86_VSYSCALL_EMULATION
	VSYSCALL_PAGE = (FIXADDR_TOP - VSYSCALL_ADDR) >> PAGE_SHIFT,
#endif
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
	FIX_RO_IDT,	/* Virtual mapping for read-only IDT */
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
#ifdef	CONFIG_X86_INTEL_MID
	FIX_LNW_VRTC,
#endif
	/* Fixmap entries to remap the GDTs, one per processor. */
	FIX_CPU_ENTRY_AREA_TOP,
	FIX_CPU_ENTRY_AREA_BOTTOM = FIX_CPU_ENTRY_AREA_TOP + (CPU_ENTRY_AREA_PAGES * NR_CPUS) - 1,

#ifdef CONFIG_ACPI_APEI_GHES
	/* Used for GHES mapping from assorted contexts */
	FIX_APEI_GHES_IRQ,
	FIX_APEI_GHES_NMI,
#endif

	__end_of_permanent_fixed_addresses,

	/*
	 * 512 temporary boot-time mappings, used by early_ioremap(),
	 * before ioremap() is functional.
	 *
	 * If necessary we round it up to the next 512 pages boundary so
	 * that we can have a single pgd entry and a single pte table:
	 */
#define NR_FIX_BTMAPS		64
#define FIX_BTMAPS_SLOTS	8
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
#define FIXADDR_START		(FIXADDR_TOP - FIXADDR_SIZE)

extern int fixmaps_set;

extern pte_t *kmap_pte;
#define kmap_prot PAGE_KERNEL
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

/*
 * FIXMAP_PAGE_NOCACHE is used for MMIO. Memory encryption is not
 * supported for MMIO addresses, so make sure that the memory encryption
 * mask is not part of the page attributes.
 */
#define FIXMAP_PAGE_NOCACHE PAGE_KERNEL_IO_NOCACHE

/*
 * Early memremap routines used for in-place encryption. The mappings created
 * by these routines are intended to be used as temporary mappings.
 */
void __init *early_memremap_encrypted(resource_size_t phys_addr,
				      unsigned long size);
void __init *early_memremap_encrypted_wp(resource_size_t phys_addr,
					 unsigned long size);
void __init *early_memremap_decrypted(resource_size_t phys_addr,
				      unsigned long size);
void __init *early_memremap_decrypted_wp(resource_size_t phys_addr,
					 unsigned long size);

#include <asm-generic/fixmap.h>

#define __late_set_fixmap(idx, phys, flags) __set_fixmap(idx, phys, flags)
#define __late_clear_fixmap(idx) __set_fixmap(idx, 0, __pgprot(0))

void __early_set_fixmap(enum fixed_addresses idx,
			phys_addr_t phys, pgprot_t flags);

static inline unsigned int __get_cpu_entry_area_page_index(int cpu, int page)
{
	BUILD_BUG_ON(sizeof(struct cpu_entry_area) % PAGE_SIZE != 0);

	return FIX_CPU_ENTRY_AREA_BOTTOM - cpu*CPU_ENTRY_AREA_PAGES - page;
}

#define __get_cpu_entry_area_offset_index(cpu, offset) ({		\
	BUILD_BUG_ON(offset % PAGE_SIZE != 0);				\
	__get_cpu_entry_area_page_index(cpu, offset / PAGE_SIZE);	\
	})

#define get_cpu_entry_area_index(cpu, field)				\
	__get_cpu_entry_area_offset_index((cpu), offsetof(struct cpu_entry_area, field))

static inline struct cpu_entry_area *get_cpu_entry_area(int cpu)
{
	return (struct cpu_entry_area *)__fix_to_virt(__get_cpu_entry_area_page_index(cpu, 0));
}

static inline struct entry_stack *cpu_entry_stack(int cpu)
{
	return &get_cpu_entry_area(cpu)->entry_stack_page.stack;
}

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_FIXMAP_H */
