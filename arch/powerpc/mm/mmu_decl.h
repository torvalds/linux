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
#include <asm/mmu.h>

#ifdef CONFIG_PPC_MMU_NOHASH
#include <asm/trace.h>

/*
 * On 40x and 8xx, we directly inline tlbia and tlbivax
 */
#if defined(CONFIG_40x) || defined(CONFIG_PPC_8xx)
static inline void _tlbil_all(void)
{
	asm volatile ("sync; tlbia; isync" : : : "memory");
	trace_tlbia(MMU_NO_CONTEXT);
}
static inline void _tlbil_pid(unsigned int pid)
{
	asm volatile ("sync; tlbia; isync" : : : "memory");
	trace_tlbia(pid);
}
#define _tlbil_pid_noind(pid)	_tlbil_pid(pid)

#else /* CONFIG_40x || CONFIG_PPC_8xx */
extern void _tlbil_all(void);
extern void _tlbil_pid(unsigned int pid);
#ifdef CONFIG_PPC_BOOK3E
extern void _tlbil_pid_noind(unsigned int pid);
#else
#define _tlbil_pid_noind(pid)	_tlbil_pid(pid)
#endif
#endif /* !(CONFIG_40x || CONFIG_PPC_8xx) */

/*
 * On 8xx, we directly inline tlbie, on others, it's extern
 */
#ifdef CONFIG_PPC_8xx
static inline void _tlbil_va(unsigned long address, unsigned int pid,
			     unsigned int tsize, unsigned int ind)
{
	asm volatile ("tlbie %0; sync" : : "r" (address) : "memory");
	trace_tlbie(0, 0, address, pid, 0, 0, 0);
}
#elif defined(CONFIG_PPC_BOOK3E)
extern void _tlbil_va(unsigned long address, unsigned int pid,
		      unsigned int tsize, unsigned int ind);
#else
extern void __tlbil_va(unsigned long address, unsigned int pid);
static inline void _tlbil_va(unsigned long address, unsigned int pid,
			     unsigned int tsize, unsigned int ind)
{
	__tlbil_va(address, pid);
}
#endif /* CONFIG_PPC_8xx */

#if defined(CONFIG_PPC_BOOK3E) || defined(CONFIG_PPC_47x)
extern void _tlbivax_bcast(unsigned long address, unsigned int pid,
			   unsigned int tsize, unsigned int ind);
#else
static inline void _tlbivax_bcast(unsigned long address, unsigned int pid,
				   unsigned int tsize, unsigned int ind)
{
	BUG();
}
#endif

#else /* CONFIG_PPC_MMU_NOHASH */

extern void hash_preload(struct mm_struct *mm, unsigned long ea,
			 bool is_exec, unsigned long trap);


extern void _tlbie(unsigned long address);
extern void _tlbia(void);

#endif /* CONFIG_PPC_MMU_NOHASH */

#ifdef CONFIG_PPC32

extern void mapin_ram(void);
extern void setbat(int index, unsigned long virt, phys_addr_t phys,
		   unsigned int size, pgprot_t prot);

extern int __map_without_bats;
extern unsigned int rtas_data, rtas_size;

struct hash_pte;
extern struct hash_pte *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;

#endif /* CONFIG_PPC32 */

extern unsigned long ioremap_bot;
extern unsigned long __max_low_memory;
extern phys_addr_t __initial_memory_limit_addr;
extern phys_addr_t total_memory;
extern phys_addr_t total_lowmem;
extern phys_addr_t memstart_addr;
extern phys_addr_t lowmem_end_addr;

#ifdef CONFIG_WII
extern unsigned long wii_hole_start;
extern unsigned long wii_hole_size;

extern unsigned long wii_mmu_mapin_mem2(unsigned long top);
extern void wii_memory_fixups(void);
#endif

/* ...and now those things that may be slightly different between processor
 * architectures.  -- Dan
 */
#ifdef CONFIG_PPC32
extern void MMU_init_hw(void);
extern unsigned long mmu_mapin_ram(unsigned long top);
#endif

#ifdef CONFIG_PPC_FSL_BOOK3E
extern unsigned long map_mem_in_cams(unsigned long ram, int max_cam_idx,
				     bool dryrun);
extern unsigned long calc_cam_sz(unsigned long ram, unsigned long virt,
				 phys_addr_t phys);
#ifdef CONFIG_PPC32
extern void adjust_total_lowmem(void);
extern int switch_to_as1(void);
extern void restore_to_as0(int esel, int offset, void *dt_ptr, int bootcpu);
#endif
extern void loadcam_entry(unsigned int index);
extern void loadcam_multi(int first_idx, int num, int tmp_idx);

struct tlbcam {
	u32	MAS0;
	u32	MAS1;
	unsigned long	MAS2;
	u32	MAS3;
	u32	MAS7;
};
#endif

#if defined(CONFIG_PPC_BOOK3S_32) || defined(CONFIG_FSL_BOOKE) || defined(CONFIG_PPC_8xx)
/* 6xx have BATS */
/* FSL_BOOKE have TLBCAM */
/* 8xx have LTLB */
phys_addr_t v_block_mapped(unsigned long va);
unsigned long p_block_mapped(phys_addr_t pa);
#else
static inline phys_addr_t v_block_mapped(unsigned long va) { return 0; }
static inline unsigned long p_block_mapped(phys_addr_t pa) { return 0; }
#endif
