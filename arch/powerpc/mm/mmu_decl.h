/*
 * Declarations of procedures and variables shared between files
 * in arch/ppc/mm/.
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */
#include <linux/mm.h>
#include <asm/tlbflush.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_MMU_NOHASH

/*
 * On 40x and 8xx, we directly inline tlbia and tlbivax
 */
#if defined(CONFIG_40x) || defined(CONFIG_8xx)
static inline void _tlbil_all(void)
{
	asm volatile ("sync; tlbia; isync" : : : "memory");
}
static inline void _tlbil_pid(unsigned int pid)
{
	asm volatile ("sync; tlbia; isync" : : : "memory");
}
#else /* CONFIG_40x || CONFIG_8xx */
extern void _tlbil_all(void);
extern void _tlbil_pid(unsigned int pid);
#endif /* !(CONFIG_40x || CONFIG_8xx) */

/*
 * On 8xx, we directly inline tlbie, on others, it's extern
 */
#ifdef CONFIG_8xx
static inline void _tlbil_va(unsigned long address, unsigned int pid)
{
	asm volatile ("tlbie %0; sync" : : "r" (address) : "memory");
}
#else /* CONFIG_8xx */
extern void _tlbil_va(unsigned long address, unsigned int pid);
#endif /* CONIFG_8xx */

/*
 * As of today, we don't support tlbivax broadcast on any
 * implementation. When that becomes the case, this will be
 * an extern.
 */
static inline void _tlbivax_bcast(unsigned long address, unsigned int pid)
{
	BUG();
}

#else /* CONFIG_PPC_MMU_NOHASH */

extern void hash_preload(struct mm_struct *mm, unsigned long ea,
			 unsigned long access, unsigned long trap);


extern void _tlbie(unsigned long address);
extern void _tlbia(void);

#endif /* CONFIG_PPC_MMU_NOHASH */

#ifdef CONFIG_PPC32

struct tlbcam {
	u32	MAS0;
	u32	MAS1;
	u32	MAS2;
	u32	MAS3;
	u32	MAS7;
};

extern void mapin_ram(void);
extern int map_page(unsigned long va, phys_addr_t pa, int flags);
extern void setbat(int index, unsigned long virt, phys_addr_t phys,
		   unsigned int size, int flags);
extern void settlbcam(int index, unsigned long virt, phys_addr_t phys,
		      unsigned int size, int flags, unsigned int pid);
extern void invalidate_tlbcam_entry(int index);

extern int __map_without_bats;
extern unsigned long ioremap_base;
extern unsigned int rtas_data, rtas_size;

struct hash_pte;
extern struct hash_pte *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
#endif

extern unsigned long ioremap_bot;
extern unsigned long __max_low_memory;
extern phys_addr_t __initial_memory_limit_addr;
extern phys_addr_t total_memory;
extern phys_addr_t total_lowmem;
extern phys_addr_t memstart_addr;
extern phys_addr_t lowmem_end_addr;

/* ...and now those things that may be slightly different between processor
 * architectures.  -- Dan
 */
#if defined(CONFIG_8xx)
#define MMU_init_hw()		do { } while(0)
#define mmu_mapin_ram()		(0UL)

#elif defined(CONFIG_4xx)
extern void MMU_init_hw(void);
extern unsigned long mmu_mapin_ram(void);

#elif defined(CONFIG_FSL_BOOKE)
extern void MMU_init_hw(void);
extern unsigned long mmu_mapin_ram(void);
extern void adjust_total_lowmem(void);

#elif defined(CONFIG_PPC32)
/* anything 32-bit except 4xx or 8xx */
extern void MMU_init_hw(void);
extern unsigned long mmu_mapin_ram(void);
#endif
