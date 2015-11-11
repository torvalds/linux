#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#define FIXADDR_START		0xffc00000UL
#define FIXADDR_END		0xfff00000UL
#define FIXADDR_TOP		(FIXADDR_END - PAGE_SIZE)

#include <asm/kmap_types.h>
#include <asm/pgtable.h>

enum fixed_addresses {
	FIX_EARLYCON_MEM_BASE,
	__end_of_permanent_fixed_addresses,

	FIX_KMAP_BEGIN = __end_of_permanent_fixed_addresses,
	FIX_KMAP_END = FIX_KMAP_BEGIN + (KM_TYPE_NR * NR_CPUS) - 1,

	/* Support writing RO kernel text via kprobes, jump labels, etc. */
	FIX_TEXT_POKE0,
	FIX_TEXT_POKE1,

	__end_of_fixed_addresses
};

#define FIXMAP_PAGE_COMMON	(L_PTE_YOUNG | L_PTE_PRESENT | L_PTE_XN | L_PTE_DIRTY)

#define FIXMAP_PAGE_NORMAL	(FIXMAP_PAGE_COMMON | L_PTE_MT_WRITEBACK)

/* Used by set_fixmap_(io|nocache), both meant for mapping a device */
#define FIXMAP_PAGE_IO		(FIXMAP_PAGE_COMMON | L_PTE_MT_DEV_SHARED | L_PTE_SHARED)
#define FIXMAP_PAGE_NOCACHE	FIXMAP_PAGE_IO

void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t prot);
void __init early_fixmap_init(void);

#include <asm-generic/fixmap.h>

#endif
