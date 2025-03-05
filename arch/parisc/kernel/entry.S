/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 * kernel entry points (interruptions, system call wrappers)
 *  Copyright (C) 1999,2000 Philipp Rumpf 
 *  Copyright (C) 1999 SuSE GmbH Nuernberg 
 *  Copyright (C) 2000 Hewlett-Packard (John Marvin)
 *  Copyright (C) 1999 Hewlett-Packard (Frank Rowand)
 */

#include <asm/asm-offsets.h>

/* we have the following possibilities to act on an interruption:
 *  - handle in assembly and use shadowed registers only
 *  - save registers to kernel stack and handle in assembly or C */


#include <asm/psw.h>
#include <asm/cache.h>		/* for L1_CACHE_SHIFT */
#include <asm/assembly.h>	/* for LDREG/STREG defines */
#include <asm/signal.h>
#include <asm/unistd.h>
#include <asm/ldcw.h>
#include <asm/traps.h>
#include <asm/thread_info.h>
#include <asm/alternative.h>

#include <linux/linkage.h>
#include <linux/pgtable.h>

#ifdef CONFIG_64BIT
	.level 2.0w
#else
	.level 2.0
#endif

	/* Get aligned page_table_lock address for this mm from cr28/tr4 */
	.macro  get_ptl reg
	mfctl	%cr28,\reg
	.endm

	/* space_to_prot macro creates a prot id from a space id */

#if (SPACEID_SHIFT) == 0
	.macro  space_to_prot spc prot
	depd,z  \spc,62,31,\prot
	.endm
#else
	.macro  space_to_prot spc prot
	extrd,u \spc,(64 - (SPACEID_SHIFT)),32,\prot
	.endm
#endif
	/*
	 * The "get_stack" macros are responsible for determining the
	 * kernel stack value.
	 *
	 *      If sr7 == 0
	 *          Already using a kernel stack, so call the
	 *          get_stack_use_r30 macro to push a pt_regs structure
	 *          on the stack, and store registers there.
	 *      else
	 *          Need to set up a kernel stack, so call the
	 *          get_stack_use_cr30 macro to set up a pointer
	 *          to the pt_regs structure contained within the
	 *          task pointer pointed to by cr30. Load the stack
	 *          pointer from the task structure.
	 *
	 * Note that we use shadowed registers for temps until
	 * we can save %r26 and %r29. %r26 is used to preserve
	 * %r8 (a shadowed register) which temporarily contained
	 * either the fault type ("code") or the eirr. We need
	 * to use a non-shadowed register to carry the value over
	 * the rfir in virt_map. We use %r26 since this value winds
	 * up being passed as the argument to either do_cpu_irq_mask
	 * or handle_interruption. %r29 is used to hold a pointer
	 * the register save area, and once again, it needs to
	 * be a non-shadowed register so that it survives the rfir.
	 */

	.macro  get_stack_use_cr30

	/* we save the registers in the task struct */

	copy	%r30, %r17
	mfctl   %cr30, %r1
	tophys  %r1,%r9		/* task_struct */
	LDREG	TASK_STACK(%r9),%r30
	ldo	PT_SZ_ALGN(%r30),%r30
	mtsp	%r0,%sr7	/* clear sr7 after kernel stack was set! */
	mtsp	%r16,%sr3
	ldo     TASK_REGS(%r9),%r9
	STREG   %r17,PT_GR30(%r9)
	STREG   %r29,PT_GR29(%r9)
	STREG   %r26,PT_GR26(%r9)
	STREG	%r16,PT_SR7(%r9)
	copy    %r9,%r29
	.endm

	.macro  get_stack_use_r30

	/* we put a struct pt_regs on the stack and save the registers there */

	tophys  %r30,%r9
	copy	%r30,%r1
	ldo	PT_SZ_ALGN(%r30),%r30
	STREG   %r1,PT_GR30(%r9)
	STREG   %r29,PT_GR29(%r9)
	STREG   %r26,PT_GR26(%r9)
	STREG	%r16,PT_SR7(%r9)
	copy    %r9,%r29
	.endm

	.macro  rest_stack
	LDREG   PT_GR1(%r29), %r1
	LDREG   PT_GR30(%r29),%r30
	LDREG   PT_GR29(%r29),%r29
	.endm

	/* default interruption handler
	 * (calls traps.c:handle_interruption) */
	.macro	def code
	b	intr_save
	ldi     \code, %r8
	.align	32
	.endm

	/* Interrupt interruption handler
	 * (calls irq.c:do_cpu_irq_mask) */
	.macro	extint code
	b	intr_extint
	mfsp    %sr7,%r16
	.align	32
	.endm	

	.import	os_hpmc, code

	/* HPMC handler */
	.macro	hpmc code
	nop			/* must be a NOP, will be patched later */
	load32	PA(os_hpmc), %r3
	bv,n	0(%r3)
	nop
	.word	0		/* checksum (will be patched) */
	.word	0		/* address of handler */
	.word	0		/* length of handler */
	.endm

	/*
	 * Performance Note: Instructions will be moved up into
	 * this part of the code later on, once we are sure
	 * that the tlb miss handlers are close to final form.
	 */

	/* Register definitions for tlb miss handler macros */

	va  = r8	/* virtual address for which the trap occurred */
	spc = r24	/* space for which the trap occurred */

#ifndef CONFIG_64BIT

	/*
	 * itlb miss interruption handler (parisc 1.1 - 32 bit)
	 */

	.macro	itlb_11 code

	mfctl	%pcsq, spc
	b	itlb_miss_11
	mfctl	%pcoq, va

	.align		32
	.endm
#endif
	
	/*
	 * itlb miss interruption handler (parisc 2.0)
	 */

	.macro	itlb_20 code
	mfctl	%pcsq, spc
#ifdef CONFIG_64BIT
	b       itlb_miss_20w
#else
	b	itlb_miss_20
#endif
	mfctl	%pcoq, va

	.align		32
	.endm
	
#ifndef CONFIG_64BIT
	/*
	 * naitlb miss interruption handler (parisc 1.1 - 32 bit)
	 */

	.macro	naitlb_11 code

	mfctl	%isr,spc
	b	naitlb_miss_11
	mfctl 	%ior,va

	.align		32
	.endm
#endif
	
	/*
	 * naitlb miss interruption handler (parisc 2.0)
	 */

	.macro	naitlb_20 code

	mfctl	%isr,spc
#ifdef CONFIG_64BIT
	b       naitlb_miss_20w
#else
	b	naitlb_miss_20
#endif
	mfctl 	%ior,va

	.align		32
	.endm
	
#ifndef CONFIG_64BIT
	/*
	 * dtlb miss interruption handler (parisc 1.1 - 32 bit)
	 */

	.macro	dtlb_11 code

	mfctl	%isr, spc
	b	dtlb_miss_11
	mfctl	%ior, va

	.align		32
	.endm
#endif

	/*
	 * dtlb miss interruption handler (parisc 2.0)
	 */

	.macro	dtlb_20 code

	mfctl	%isr, spc
#ifdef CONFIG_64BIT
	b       dtlb_miss_20w
#else
	b	dtlb_miss_20
#endif
	mfctl	%ior, va

	.align		32
	.endm
	
#ifndef CONFIG_64BIT
	/* nadtlb miss interruption handler (parisc 1.1 - 32 bit) */

	.macro	nadtlb_11 code

	mfctl	%isr,spc
	b       nadtlb_miss_11
	mfctl	%ior,va

	.align		32
	.endm
#endif
	
	/* nadtlb miss interruption handler (parisc 2.0) */

	.macro	nadtlb_20 code

	mfctl	%isr,spc
#ifdef CONFIG_64BIT
	b       nadtlb_miss_20w
#else
	b       nadtlb_miss_20
#endif
	mfctl	%ior,va

	.align		32
	.endm
	
#ifndef CONFIG_64BIT
	/*
	 * dirty bit trap interruption handler (parisc 1.1 - 32 bit)
	 */

	.macro	dbit_11 code

	mfctl	%isr,spc
	b	dbit_trap_11
	mfctl	%ior,va

	.align		32
	.endm
#endif

	/*
	 * dirty bit trap interruption handler (parisc 2.0)
	 */

	.macro	dbit_20 code

	mfctl	%isr,spc
#ifdef CONFIG_64BIT
	b       dbit_trap_20w
#else
	b	dbit_trap_20
#endif
	mfctl	%ior,va

	.align		32
	.endm

	/* In LP64, the space contains part of the upper 32 bits of the
	 * fault.  We have to extract this and place it in the va,
	 * zeroing the corresponding bits in the space register */
	.macro		space_adjust	spc,va,tmp
#ifdef CONFIG_64BIT
	extrd,u		\spc,63,SPACEID_SHIFT,\tmp
	depd		%r0,63,SPACEID_SHIFT,\spc
	depd		\tmp,31,SPACEID_SHIFT,\va
#endif
	.endm

	.import		swapper_pg_dir,code

	/* Get the pgd.  For faults on space zero (kernel space), this
	 * is simply swapper_pg_dir.  For user space faults, the
	 * pgd is stored in %cr25 */
	.macro		get_pgd		spc,reg
	ldil		L%PA(swapper_pg_dir),\reg
	ldo		R%PA(swapper_pg_dir)(\reg),\reg
	or,COND(=)	%r0,\spc,%r0
	mfctl		%cr25,\reg
	.endm

	/* 
		space_check(spc,tmp,fault)

		spc - The space we saw the fault with.
		tmp - The place to store the current space.
		fault - Function to call on failure.

		Only allow faults on different spaces from the
		currently active one if we're the kernel 

	*/
	.macro		space_check	spc,tmp,fault
	mfsp		%sr7,\tmp
	/* check against %r0 which is same value as LINUX_GATEWAY_SPACE */
	or,COND(<>)	%r0,\spc,%r0	/* user may execute gateway page
					 * as kernel, so defeat the space
					 * check if it is */
	copy		\spc,\tmp
	or,COND(=)	%r0,\tmp,%r0	/* nullify if executing as kernel */
	cmpb,COND(<>),n	\tmp,\spc,\fault
	.endm

	/* Look up a PTE in a 2-Level scheme (faulting at each
	 * level if the entry isn't present 
	 *
	 * NOTE: we use ldw even for LP64, since the short pointers
	 * can address up to 1TB
	 */
	.macro		L2_ptep	pmd,pte,index,va,fault
#if CONFIG_PGTABLE_LEVELS == 3
	extru_safe	\va,31-ASM_PMD_SHIFT,ASM_BITS_PER_PMD,\index
#else
	extru_safe	\va,31-ASM_PGDIR_SHIFT,ASM_BITS_PER_PGD,\index
#endif
	dep             %r0,31,PAGE_SHIFT,\pmd  /* clear offset */
#if CONFIG_PGTABLE_LEVELS < 3
	copy		%r0,\pte
#endif
	ldw,s		\index(\pmd),\pmd
	bb,>=,n		\pmd,_PxD_PRESENT_BIT,\fault
	dep		%r0,31,PxD_FLAG_SHIFT,\pmd /* clear flags */
	SHLREG		\pmd,PxD_VALUE_SHIFT,\pmd
	extru_safe	\va,31-PAGE_SHIFT,ASM_BITS_PER_PTE,\index
	dep		%r0,31,PAGE_SHIFT,\pmd  /* clear offset */
	shladd		\index,BITS_PER_PTE_ENTRY,\pmd,\pmd /* pmd is now pte */
	.endm

	/* Look up PTE in a 3-Level scheme. */
	.macro		L3_ptep pgd,pte,index,va,fault
#if CONFIG_PGTABLE_LEVELS == 3
	copy		%r0,\pte
	extrd,u		\va,63-ASM_PGDIR_SHIFT,ASM_BITS_PER_PGD,\index
	ldw,s		\index(\pgd),\pgd
	bb,>=,n		\pgd,_PxD_PRESENT_BIT,\fault
	shld		\pgd,PxD_VALUE_SHIFT,\pgd
#endif
	L2_ptep		\pgd,\pte,\index,\va,\fault
	.endm

	/* Acquire page_table_lock and check page is present. */
	.macro		ptl_lock	spc,ptp,pte,tmp,tmp1,fault
#ifdef CONFIG_TLB_PTLOCK
98:	cmpib,COND(=),n	0,\spc,2f
	get_ptl		\tmp
1:	LDCW		0(\tmp),\tmp1
	cmpib,COND(=)	0,\tmp1,1b
	nop
	LDREG		0(\ptp),\pte
	bb,<,n		\pte,_PAGE_PRESENT_BIT,3f
	b		\fault
	stw		\spc,0(\tmp)
99:	ALTERNATIVE(98b, 99b, ALT_COND_NO_SMP, INSN_NOP)
#endif
2:	LDREG		0(\ptp),\pte
	bb,>=,n		\pte,_PAGE_PRESENT_BIT,\fault
3:
	.endm

	/* Release page_table_lock without reloading lock address.
	   Note that the values in the register spc are limited to
	   NR_SPACE_IDS (262144). Thus, the stw instruction always
	   stores a nonzero value even when register spc is 64 bits.
	   We use an ordered store to ensure all prior accesses are
	   performed prior to releasing the lock. */
	.macro		ptl_unlock0	spc,tmp
#ifdef CONFIG_TLB_PTLOCK
98:	or,COND(=)	%r0,\spc,%r0
	stw,ma		\spc,0(\tmp)
99:	ALTERNATIVE(98b, 99b, ALT_COND_NO_SMP, INSN_NOP)
#endif
	.endm

	/* Release page_table_lock. */
	.macro		ptl_unlock1	spc,tmp
#ifdef CONFIG_TLB_PTLOCK
98:	get_ptl		\tmp
	ptl_unlock0	\spc,\tmp
99:	ALTERNATIVE(98b, 99b, ALT_COND_NO_SMP, INSN_NOP)
#endif
	.endm

	/* Set the _PAGE_ACCESSED bit of the PTE.  Be clever and
	 * don't needlessly dirty the cache line if it was already set */
	.macro		update_accessed	ptp,pte,tmp,tmp1
	ldi		_PAGE_ACCESSED,\tmp1
	or		\tmp1,\pte,\tmp
	and,COND(<>)	\tmp1,\pte,%r0
	STREG		\tmp,0(\ptp)
	.endm

	/* Set the dirty bit (and accessed bit).  No need to be
	 * clever, this is only used from the dirty fault */
	.macro		update_dirty	ptp,pte,tmp
	ldi		_PAGE_ACCESSED|_PAGE_DIRTY,\tmp
	or		\tmp,\pte,\pte
	STREG		\pte,0(\ptp)
	.endm

	/* We have (depending on the page size):
	 * - 38 to 52-bit Physical Page Number
	 * - 12 to 26-bit page offset
	 */
	/* bitshift difference between a PFN (based on kernel's PAGE_SIZE)
	 * to a CPU TLB 4k PFN (4k => 12 bits to shift) */
	#define PAGE_ADD_SHIFT		(PAGE_SHIFT-12)
	#define PAGE_ADD_HUGE_SHIFT	(REAL_HPAGE_SHIFT-12)
	#define PFN_START_BIT	(63-ASM_PFN_PTE_SHIFT+(63-58)-PAGE_ADD_SHIFT)

	/* Drop prot bits and convert to page addr for iitlbt and idtlbt */
	.macro		convert_for_tlb_insert20 pte,tmp
#ifdef CONFIG_HUGETLB_PAGE
	copy		\pte,\tmp
	extrd,u		\tmp,PFN_START_BIT,PFN_START_BIT+1,\pte

	depdi		_PAGE_SIZE_ENCODING_DEFAULT,63,\
				(63-58)+PAGE_ADD_SHIFT,\pte
	extrd,u,*=	\tmp,_PAGE_HPAGE_BIT+32,1,%r0
	depdi		_HUGE_PAGE_SIZE_ENCODING_DEFAULT,63,\
				(63-58)+PAGE_ADD_HUGE_SHIFT,\pte
#else /* Huge pages disabled */
	extrd,u		\pte,PFN_START_BIT,PFN_START_BIT+1,\pte
	depdi		_PAGE_SIZE_ENCODING_DEFAULT,63,\
				(63-58)+PAGE_ADD_SHIFT,\pte
#endif
	.endm

	/* Convert the pte and prot to tlb insertion values.  How
	 * this happens is quite subtle, read below */
	.macro		make_insert_tlb	spc,pte,prot,tmp
	space_to_prot   \spc \prot        /* create prot id from space */
	/* The following is the real subtlety.  This is depositing
	 * T <-> _PAGE_REFTRAP
	 * D <-> _PAGE_DIRTY
	 * B <-> _PAGE_DMB (memory break)
	 *
	 * Then incredible subtlety: The access rights are
	 * _PAGE_GATEWAY, _PAGE_EXEC and _PAGE_WRITE
	 * See 3-14 of the parisc 2.0 manual
	 *
	 * Finally, _PAGE_READ goes in the top bit of PL1 (so we
	 * trigger an access rights trap in user space if the user
	 * tries to read an unreadable page */
#if _PAGE_SPECIAL_BIT == _PAGE_DMB_BIT
	/* need to drop DMB bit, as it's used as SPECIAL flag */
	depi		0,_PAGE_SPECIAL_BIT,1,\pte
#endif
	depd            \pte,8,7,\prot

	/* PAGE_USER indicates the page can be read with user privileges,
	 * so deposit X1|11 to PL1|PL2 (remember the upper bit of PL1
	 * contains _PAGE_READ) */
	extrd,u,*=      \pte,_PAGE_USER_BIT+32,1,%r0
	depdi		7,11,3,\prot
	/* If we're a gateway page, drop PL2 back to zero for promotion
	 * to kernel privilege (so we can execute the page as kernel).
	 * Any privilege promotion page always denys read and write */
	extrd,u,*= 	\pte,_PAGE_GATEWAY_BIT+32,1,%r0
	depd		%r0,11,2,\prot	/* If Gateway, Set PL2 to 0 */

	/* Enforce uncacheable pages.
	 * This should ONLY be use for MMIO on PA 2.0 machines.
	 * Memory/DMA is cache coherent on all PA2.0 machines we support
	 * (that means T-class is NOT supported) and the memory controllers
	 * on most of those machines only handles cache transactions.
	 */
	extrd,u,*=	\pte,_PAGE_NO_CACHE_BIT+32,1,%r0
	depdi		1,12,1,\prot

	/* Drop prot bits and convert to page addr for iitlbt and idtlbt */
	convert_for_tlb_insert20 \pte \tmp
	.endm

	/* Identical macro to make_insert_tlb above, except it
	 * makes the tlb entry for the differently formatted pa11
	 * insertion instructions */
	.macro		make_insert_tlb_11	spc,pte,prot
#if _PAGE_SPECIAL_BIT == _PAGE_DMB_BIT
	/* need to drop DMB bit, as it's used as SPECIAL flag */
	depi		0,_PAGE_SPECIAL_BIT,1,\pte
#endif
	zdep		\spc,30,15,\prot
	dep		\pte,8,7,\prot
	extru,=		\pte,_PAGE_NO_CACHE_BIT,1,%r0
	depi		1,12,1,\prot
	extru,=         \pte,_PAGE_USER_BIT,1,%r0
	depi		7,11,3,\prot   /* Set for user space (1 rsvd for read) */
	extru,= 	\pte,_PAGE_GATEWAY_BIT,1,%r0
	depi		0,11,2,\prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for iitlba */

	depi		0,31,ASM_PFN_PTE_SHIFT,\pte
	SHRREG		\pte,(ASM_PFN_PTE_SHIFT-(31-26)),\pte
	.endm

	/* This is for ILP32 PA2.0 only.  The TLB insertion needs
	 * to extend into I/O space if the address is 0xfXXXXXXX
	 * so we extend the f's into the top word of the pte in
	 * this case */
	.macro		f_extend	pte,tmp
	extrd,s		\pte,42,4,\tmp
	addi,<>		1,\tmp,%r0
	extrd,s		\pte,63,25,\pte
	.endm

	/* The alias region is comprised of a pair of 4 MB regions
	 * aligned to 8 MB. It is used to clear/copy/flush user pages
	 * using kernel virtual addresses congruent with the user
	 * virtual address.
	 *
	 * To use the alias page, you set %r26 up with the to TLB
	 * entry (identifying the physical page) and %r23 up with
	 * the from tlb entry (or nothing if only a to entry---for
	 * clear_user_page_asm) */
	.macro		do_alias	spc,tmp,tmp1,va,pte,prot,fault,patype
	cmpib,COND(<>),n 0,\spc,\fault
	ldil		L%(TMPALIAS_MAP_START),\tmp
	copy		\va,\tmp1
	depi_safe	0,31,TMPALIAS_SIZE_BITS+1,\tmp1
	cmpb,COND(<>),n	\tmp,\tmp1,\fault
	mfctl		%cr19,\tmp	/* iir */
	/* get the opcode (first six bits) into \tmp */
	extrw,u		\tmp,5,6,\tmp
	/*
	 * Only setting the T bit prevents data cache movein
	 * Setting access rights to zero prevents instruction cache movein
	 *
	 * Note subtlety here: _PAGE_GATEWAY, _PAGE_EXEC and _PAGE_WRITE go
	 * to type field and _PAGE_READ goes to top bit of PL1
	 */
	ldi		(_PAGE_REFTRAP|_PAGE_READ|_PAGE_WRITE),\prot
	/*
	 * so if the opcode is one (i.e. this is a memory management
	 * instruction) nullify the next load so \prot is only T.
	 * Otherwise this is a normal data operation
	 */
	cmpiclr,=	0x01,\tmp,%r0
	ldi		(_PAGE_DIRTY|_PAGE_READ|_PAGE_WRITE),\prot
.ifc \patype,20
	depd,z		\prot,8,7,\prot
.else
.ifc \patype,11
	depw,z		\prot,8,7,\prot
.else
	.error "undefined PA type to do_alias"
.endif
.endif
	/*
	 * OK, it is in the temp alias region, check whether "from" or "to".
	 * Check "subtle" note in pacache.S re: r23/r26.
	 */
	extrw,u,=	\va,31-TMPALIAS_SIZE_BITS,1,%r0
	or,COND(tr)	%r23,%r0,\pte
	or		%r26,%r0,\pte

	/* convert phys addr in \pte (from r23 or r26) to tlb insert format */
	SHRREG		\pte,PAGE_SHIFT+PAGE_ADD_SHIFT-5, \pte
	depi_safe	_PAGE_SIZE_ENCODING_DEFAULT, 31,5, \pte
	.endm 


	/*
	 * Fault_vectors are architecturally required to be aligned on a 2K
	 * boundary
	 */

	.section .text.hot
	.align 2048

ENTRY(fault_vector_20)
	/* First vector is invalid (0) */
	.ascii	"cows can fly"
	.byte 0
	.align 32

	hpmc		 1
	def		 2
	def		 3
	extint		 4
	def		 5
	itlb_20		 PARISC_ITLB_TRAP
	def		 7
	def		 8
	def              9
	def		10
	def		11
	def		12
	def		13
	def		14
	dtlb_20		15
	naitlb_20	16
	nadtlb_20	17
	def		18
	def		19
	dbit_20		20
	def		21
	def		22
	def		23
	def		24
	def		25
	def		26
	def		27
	def		28
	def		29
	def		30
	def		31
END(fault_vector_20)

#ifndef CONFIG_64BIT

	.align 2048

ENTRY(fault_vector_11)
	/* First vector is invalid (0) */
	.ascii	"cows can fly"
	.byte 0
	.align 32

	hpmc		 1
	def		 2
	def		 3
	extint		 4
	def		 5
	itlb_11		 PARISC_ITLB_TRAP
	def		 7
	def		 8
	def              9
	def		10
	def		11
	def		12
	def		13
	def		14
	dtlb_11		15
	naitlb_11	16
	nadtlb_11	17
	def		18
	def		19
	dbit_11		20
	def		21
	def		22
	def		23
	def		24
	def		25
	def		26
	def		27
	def		28
	def		29
	def		30
	def		31
END(fault_vector_11)

#endif
	/* Fault vector is separately protected and *must* be on its own page */
	.align		PAGE_SIZE

	.import		handle_interruption,code
	.import		do_cpu_irq_mask,code

	/*
	 * Child Returns here
	 *
	 * copy_thread moved args into task save area.
	 */

ENTRY(ret_from_kernel_thread)
	/* Call schedule_tail first though */
	BL	schedule_tail, %r2
	nop

	mfctl	%cr30,%r1	/* task_struct */
	LDREG	TASK_PT_GR25(%r1), %r26
#ifdef CONFIG_64BIT
	LDREG	TASK_PT_GR27(%r1), %r27
#endif
	LDREG	TASK_PT_GR26(%r1), %r1
	ble	0(%sr7, %r1)
	copy	%r31, %r2
	b	finish_child_return
	nop
END(ret_from_kernel_thread)


	/*
	 * struct task_struct *_switch_to(struct task_struct *prev,
	 *	struct task_struct *next)
	 *
	 * switch kernel stacks and return prev */
ENTRY_CFI(_switch_to)
	STREG	 %r2, -RP_OFFSET(%r30)

	callee_save_float
	callee_save

	load32	_switch_to_ret, %r2

	STREG	%r2, TASK_PT_KPC(%r26)
	LDREG	TASK_PT_KPC(%r25), %r2

	STREG	%r30, TASK_PT_KSP(%r26)
	LDREG	TASK_PT_KSP(%r25), %r30
	bv	%r0(%r2)
	mtctl   %r25,%cr30

ENTRY(_switch_to_ret)
	mtctl	%r0, %cr0		/* Needed for single stepping */
	callee_rest
	callee_rest_float

	LDREG	-RP_OFFSET(%r30), %r2
	bv	%r0(%r2)
	copy	%r26, %r28
ENDPROC_CFI(_switch_to)

	/*
	 * Common rfi return path for interruptions, kernel execve, and
	 * sys_rt_sigreturn (sometimes).  The sys_rt_sigreturn syscall will
	 * return via this path if the signal was received when the process
	 * was running; if the process was blocked on a syscall then the
	 * normal syscall_exit path is used.  All syscalls for traced
	 * proceses exit via intr_restore.
	 *
	 * XXX If any syscalls that change a processes space id ever exit
	 * this way, then we will need to copy %sr3 in to PT_SR[3..7], and
	 * adjust IASQ[0..1].
	 *
	 */

	.align	PAGE_SIZE

ENTRY_CFI(syscall_exit_rfi)
	mfctl	%cr30,%r16		/* task_struct */
	ldo	TASK_REGS(%r16),%r16
	/* Force iaoq to userspace, as the user has had access to our current
	 * context via sigcontext. Also Filter the PSW for the same reason.
	 */
	LDREG	PT_IAOQ0(%r16),%r19
	depi	PRIV_USER,31,2,%r19
	STREG	%r19,PT_IAOQ0(%r16)
	LDREG	PT_IAOQ1(%r16),%r19
	depi	PRIV_USER,31,2,%r19
	STREG	%r19,PT_IAOQ1(%r16)
	LDREG   PT_PSW(%r16),%r19
	load32	USER_PSW_MASK,%r1
#ifdef CONFIG_64BIT
	load32	USER_PSW_HI_MASK,%r20
	depd    %r20,31,32,%r1
#endif
	and     %r19,%r1,%r19 /* Mask out bits that user shouldn't play with */
	load32	USER_PSW,%r1
	or      %r19,%r1,%r19 /* Make sure default USER_PSW bits are set */
	STREG   %r19,PT_PSW(%r16)

	/*
	 * If we aren't being traced, we never saved space registers
	 * (we don't store them in the sigcontext), so set them
	 * to "proper" values now (otherwise we'll wind up restoring
	 * whatever was last stored in the task structure, which might
	 * be inconsistent if an interrupt occurred while on the gateway
	 * page). Note that we may be "trashing" values the user put in
	 * them, but we don't support the user changing them.
	 */

	STREG   %r0,PT_SR2(%r16)
	mfsp    %sr3,%r19
	STREG   %r19,PT_SR0(%r16)
	STREG   %r19,PT_SR1(%r16)
	STREG   %r19,PT_SR3(%r16)
	STREG   %r19,PT_SR4(%r16)
	STREG   %r19,PT_SR5(%r16)
	STREG   %r19,PT_SR6(%r16)
	STREG   %r19,PT_SR7(%r16)

ENTRY(intr_return)
	/* check for reschedule */
	mfctl   %cr30,%r1
	LDREG   TASK_TI_FLAGS(%r1),%r19	/* sched.h: TIF_NEED_RESCHED */
	bb,<,n	%r19,31-TIF_NEED_RESCHED,intr_do_resched /* forward */

	.import do_notify_resume,code
intr_check_sig:
	/* As above */
	mfctl   %cr30,%r1
	LDREG	TASK_TI_FLAGS(%r1),%r19
	ldi	(_TIF_USER_WORK_MASK & ~_TIF_NEED_RESCHED), %r20
	and,COND(<>)	%r19, %r20, %r0
	b,n	intr_restore	/* skip past if we've nothing to do */

	/* This check is critical to having LWS
	 * working. The IASQ is zero on the gateway
	 * page and we cannot deliver any signals until
	 * we get off the gateway page.
	 *
	 * Only do signals if we are returning to user space
	 */
	LDREG	PT_IASQ0(%r16), %r20
	cmpib,COND(=),n LINUX_GATEWAY_SPACE, %r20, intr_restore /* forward */
	LDREG	PT_IASQ1(%r16), %r20
	cmpib,COND(=),n LINUX_GATEWAY_SPACE, %r20, intr_restore /* forward */

	copy	%r0, %r25			/* long in_syscall = 0 */
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif

	/* NOTE: We need to enable interrupts if we have to deliver
	 * signals. We used to do this earlier but it caused kernel
	 * stack overflows. */
	ssm	PSW_SM_I, %r0

	BL	do_notify_resume,%r2
	copy	%r16, %r26			/* struct pt_regs *regs */

	b,n	intr_check_sig

intr_restore:
	copy            %r16,%r29
	ldo             PT_FR31(%r29),%r1
	rest_fp         %r1
	rest_general    %r29

	/* inverse of virt_map */
	pcxt_ssm_bug
	rsm             PSW_SM_QUIET,%r0	/* prepare for rfi */
	tophys_r1       %r29

	/* Restore space id's and special cr's from PT_REGS
	 * structure pointed to by r29
	 */
	rest_specials	%r29

	/* IMPORTANT: rest_stack restores r29 last (we are using it)!
	 * It also restores r1 and r30.
	 */
	rest_stack

	rfi
	nop

#ifndef CONFIG_PREEMPTION
# define intr_do_preempt	intr_restore
#endif /* !CONFIG_PREEMPTION */

	.import schedule,code
intr_do_resched:
	/* Only call schedule on return to userspace. If we're returning
	 * to kernel space, we may schedule if CONFIG_PREEMPTION, otherwise
	 * we jump back to intr_restore.
	 */
	LDREG	PT_IASQ0(%r16), %r20
	cmpib,COND(=)	0, %r20, intr_do_preempt
	nop
	LDREG	PT_IASQ1(%r16), %r20
	cmpib,COND(=)	0, %r20, intr_do_preempt
	nop

	/* NOTE: We need to enable interrupts if we schedule.  We used
	 * to do this earlier but it caused kernel stack overflows. */
	ssm     PSW_SM_I, %r0

#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29		/* Reference param save area */
#endif

	ldil	L%intr_check_sig, %r2
#ifndef CONFIG_64BIT
	b	schedule
#else
	load32	schedule, %r20
	bv	%r0(%r20)
#endif
	ldo	R%intr_check_sig(%r2), %r2

	/* preempt the current task on returning to kernel
	 * mode from an interrupt, iff need_resched is set,
	 * and preempt_count is 0. otherwise, we continue on
	 * our merry way back to the current running task.
	 */
#ifdef CONFIG_PREEMPTION
	.import preempt_schedule_irq,code
intr_do_preempt:
	rsm	PSW_SM_I, %r0		/* disable interrupts */

	/* current_thread_info()->preempt_count */
	mfctl	%cr30, %r1
	ldw	TI_PRE_COUNT(%r1), %r19
	cmpib,<>	0, %r19, intr_restore	/* if preempt_count > 0 */
	nop				/* prev insn branched backwards */

	/* check if we interrupted a critical path */
	LDREG	PT_PSW(%r16), %r20
	bb,<,n	%r20, 31 - PSW_SM_I, intr_restore
	nop

	/* ssm PSW_SM_I done later in intr_restore */
#ifdef CONFIG_MLONGCALLS
	ldil	L%intr_restore, %r2
	load32	preempt_schedule_irq, %r1
	bv	%r0(%r1)
	ldo	R%intr_restore(%r2), %r2
#else
	ldil	L%intr_restore, %r1
	BL	preempt_schedule_irq, %r2
	ldo	R%intr_restore(%r1), %r2
#endif
#endif /* CONFIG_PREEMPTION */

	/*
	 * External interrupts.
	 */

intr_extint:
	cmpib,COND(=),n 0,%r16,1f

	get_stack_use_cr30
	b,n 2f

1:
	get_stack_use_r30
2:
	save_specials	%r29
	virt_map
	save_general	%r29

	ldo	PT_FR0(%r29), %r24
	save_fp	%r24
	
	loadgp

	copy	%r29, %r26	/* arg0 is pt_regs */
	copy	%r29, %r16	/* save pt_regs */

	ldil	L%intr_return, %r2

#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29	/* Reference param save area */
#endif

	b	do_cpu_irq_mask
	ldo	R%intr_return(%r2), %r2	/* return to intr_return, not here */
ENDPROC_CFI(syscall_exit_rfi)


	/* Generic interruptions (illegal insn, unaligned, page fault, etc) */

ENTRY_CFI(intr_save)		/* for os_hpmc */
	mfsp    %sr7,%r16
	cmpib,COND(=),n 0,%r16,1f
	get_stack_use_cr30
	b	2f
	copy    %r8,%r26

1:
	get_stack_use_r30
	copy    %r8,%r26

2:
	save_specials	%r29

	/* If this trap is a itlb miss, skip saving/adjusting isr/ior */
	cmpib,COND(=),n        PARISC_ITLB_TRAP,%r26,skip_save_ior


	mfctl           %isr, %r16
	nop		/* serialize mfctl on PA 2.0 to avoid 4 cycle penalty */
	mfctl           %ior, %r17


#ifdef CONFIG_64BIT
	/*
	 * If the interrupted code was running with W bit off (32 bit),
	 * clear the b bits (bits 0 & 1) in the ior.
	 * save_specials left ipsw value in r8 for us to test.
	 */
	extrd,u,*<>     %r8,PSW_W_BIT,1,%r0
	depdi           0,1,2,%r17

	/* adjust isr/ior: get high bits from isr and deposit in ior */
	space_adjust	%r16,%r17,%r1
#endif
	STREG           %r16, PT_ISR(%r29)
	STREG           %r17, PT_IOR(%r29)

#if defined(CONFIG_64BIT)
	b,n		intr_save2

skip_save_ior:
	/* We have a itlb miss, and when executing code above 4 Gb on ILP64, we
	 * need to adjust iasq/iaoq here in the same way we adjusted isr/ior
	 * above.
	 */
	bb,COND(>=),n	%r8,PSW_W_BIT,intr_save2
	LDREG		PT_IASQ0(%r29), %r16
	LDREG		PT_IAOQ0(%r29), %r17
	/* adjust iasq/iaoq */
	space_adjust	%r16,%r17,%r1
	STREG           %r16, PT_IASQ0(%r29)
	STREG           %r17, PT_IAOQ0(%r29)
#else
skip_save_ior:
#endif

intr_save2:
	virt_map
	save_general	%r29

	ldo		PT_FR0(%r29), %r25
	save_fp		%r25
	
	loadgp

	copy		%r29, %r25	/* arg1 is pt_regs */
#ifdef CONFIG_64BIT
	ldo		-16(%r30),%r29	/* Reference param save area */
#endif

	ldil		L%intr_check_sig, %r2
	copy		%r25, %r16	/* save pt_regs */

	b		handle_interruption
	ldo		R%intr_check_sig(%r2), %r2
ENDPROC_CFI(intr_save)


	/*
	 * Note for all tlb miss handlers:
	 *
	 * cr24 contains a pointer to the kernel address space
	 * page directory.
	 *
	 * cr25 contains a pointer to the current user address
	 * space page directory.
	 *
	 * sr3 will contain the space id of the user address space
	 * of the current running thread while that thread is
	 * running in the kernel.
	 */

	/*
	 * register number allocations.  Note that these are all
	 * in the shadowed registers
	 */

	t0 = r1		/* temporary register 0 */
	va = r8		/* virtual address for which the trap occurred */
	t1 = r9		/* temporary register 1 */
	pte  = r16	/* pte/phys page # */
	prot = r17	/* prot bits */
	spc  = r24	/* space for which the trap occurred */
	ptp = r25	/* page directory/page table pointer */

#ifdef CONFIG_64BIT

dtlb_miss_20w:
	space_adjust	spc,va,t0
	get_pgd		spc,ptp
	space_check	spc,t0,dtlb_fault

	L3_ptep		ptp,pte,t0,va,dtlb_check_alias_20w

	ptl_lock	spc,ptp,pte,t0,t1,dtlb_check_alias_20w
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot,t1
	
	idtlbt          pte,prot

	ptl_unlock1	spc,t0
	rfir
	nop

dtlb_check_alias_20w:
	do_alias	spc,t0,t1,va,pte,prot,dtlb_fault,20

	idtlbt          pte,prot

	rfir
	nop

nadtlb_miss_20w:
	space_adjust	spc,va,t0
	get_pgd		spc,ptp
	space_check	spc,t0,nadtlb_fault

	L3_ptep		ptp,pte,t0,va,nadtlb_check_alias_20w

	ptl_lock	spc,ptp,pte,t0,t1,nadtlb_check_alias_20w
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot,t1

	idtlbt          pte,prot

	ptl_unlock1	spc,t0
	rfir
	nop

nadtlb_check_alias_20w:
	do_alias	spc,t0,t1,va,pte,prot,nadtlb_emulate,20

	idtlbt          pte,prot

	rfir
	nop

#else

dtlb_miss_11:
	get_pgd		spc,ptp

	space_check	spc,t0,dtlb_fault

	L2_ptep		ptp,pte,t0,va,dtlb_check_alias_11

	ptl_lock	spc,ptp,pte,t0,t1,dtlb_check_alias_11
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb_11	spc,pte,prot

	mfsp		%sr1,t1  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	idtlba		pte,(%sr1,va)
	idtlbp		prot,(%sr1,va)

	mtsp		t1, %sr1	/* Restore sr1 */

	ptl_unlock1	spc,t0
	rfir
	nop

dtlb_check_alias_11:
	do_alias	spc,t0,t1,va,pte,prot,dtlb_fault,11

	idtlba          pte,(va)
	idtlbp          prot,(va)

	rfir
	nop

nadtlb_miss_11:
	get_pgd		spc,ptp

	space_check	spc,t0,nadtlb_fault

	L2_ptep		ptp,pte,t0,va,nadtlb_check_alias_11

	ptl_lock	spc,ptp,pte,t0,t1,nadtlb_check_alias_11
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb_11	spc,pte,prot

	mfsp		%sr1,t1  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	idtlba		pte,(%sr1,va)
	idtlbp		prot,(%sr1,va)

	mtsp		t1, %sr1	/* Restore sr1 */

	ptl_unlock1	spc,t0
	rfir
	nop

nadtlb_check_alias_11:
	do_alias	spc,t0,t1,va,pte,prot,nadtlb_emulate,11

	idtlba          pte,(va)
	idtlbp          prot,(va)

	rfir
	nop

dtlb_miss_20:
	space_adjust	spc,va,t0
	get_pgd		spc,ptp
	space_check	spc,t0,dtlb_fault

	L2_ptep		ptp,pte,t0,va,dtlb_check_alias_20

	ptl_lock	spc,ptp,pte,t0,t1,dtlb_check_alias_20
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot,t1

	f_extend	pte,t1

	idtlbt          pte,prot

	ptl_unlock1	spc,t0
	rfir
	nop

dtlb_check_alias_20:
	do_alias	spc,t0,t1,va,pte,prot,dtlb_fault,20
	
	idtlbt          pte,prot

	rfir
	nop

nadtlb_miss_20:
	get_pgd		spc,ptp

	space_check	spc,t0,nadtlb_fault

	L2_ptep		ptp,pte,t0,va,nadtlb_check_alias_20

	ptl_lock	spc,ptp,pte,t0,t1,nadtlb_check_alias_20
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot,t1

	f_extend	pte,t1
	
	idtlbt		pte,prot

	ptl_unlock1	spc,t0
	rfir
	nop

nadtlb_check_alias_20:
	do_alias	spc,t0,t1,va,pte,prot,nadtlb_emulate,20

	idtlbt          pte,prot

	rfir
	nop

#endif

nadtlb_emulate:

	/*
	 * Non-access misses can be caused by fdc,fic,pdc,lpa,probe and
	 * probei instructions. The kernel no longer faults doing flushes.
	 * Use of lpa and probe instructions is rare. Given the issue
	 * with shadow registers, we defer everything to the "slow" path.
	 */
	b,n		nadtlb_fault

#ifdef CONFIG_64BIT
itlb_miss_20w:

	/*
	 * I miss is a little different, since we allow users to fault
	 * on the gateway page which is in the kernel address space.
	 */

	space_adjust	spc,va,t0
	get_pgd		spc,ptp
	space_check	spc,t0,itlb_fault

	L3_ptep		ptp,pte,t0,va,itlb_fault

	ptl_lock	spc,ptp,pte,t0,t1,itlb_fault
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot,t1
	
	iitlbt          pte,prot

	ptl_unlock1	spc,t0
	rfir
	nop

naitlb_miss_20w:

	/*
	 * I miss is a little different, since we allow users to fault
	 * on the gateway page which is in the kernel address space.
	 */

	space_adjust	spc,va,t0
	get_pgd		spc,ptp
	space_check	spc,t0,naitlb_fault

	L3_ptep		ptp,pte,t0,va,naitlb_check_alias_20w

	ptl_lock	spc,ptp,pte,t0,t1,naitlb_check_alias_20w
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot,t1

	iitlbt          pte,prot

	ptl_unlock1	spc,t0
	rfir
	nop

naitlb_check_alias_20w:
	do_alias	spc,t0,t1,va,pte,prot,naitlb_fault,20

	iitlbt		pte,prot

	rfir
	nop

#else

itlb_miss_11:
	get_pgd		spc,ptp

	space_check	spc,t0,itlb_fault

	L2_ptep		ptp,pte,t0,va,itlb_fault

	ptl_lock	spc,ptp,pte,t0,t1,itlb_fault
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb_11	spc,pte,prot

	mfsp		%sr1,t1  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	iitlba		pte,(%sr1,va)
	iitlbp		prot,(%sr1,va)

	mtsp		t1, %sr1	/* Restore sr1 */

	ptl_unlock1	spc,t0
	rfir
	nop

naitlb_miss_11:
	get_pgd		spc,ptp

	space_check	spc,t0,naitlb_fault

	L2_ptep		ptp,pte,t0,va,naitlb_check_alias_11

	ptl_lock	spc,ptp,pte,t0,t1,naitlb_check_alias_11
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb_11	spc,pte,prot

	mfsp		%sr1,t1  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	iitlba		pte,(%sr1,va)
	iitlbp		prot,(%sr1,va)

	mtsp		t1, %sr1	/* Restore sr1 */

	ptl_unlock1	spc,t0
	rfir
	nop

naitlb_check_alias_11:
	do_alias	spc,t0,t1,va,pte,prot,itlb_fault,11

	iitlba          pte,(%sr0, va)
	iitlbp          prot,(%sr0, va)

	rfir
	nop


itlb_miss_20:
	get_pgd		spc,ptp

	space_check	spc,t0,itlb_fault

	L2_ptep		ptp,pte,t0,va,itlb_fault

	ptl_lock	spc,ptp,pte,t0,t1,itlb_fault
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot,t1

	f_extend	pte,t1

	iitlbt          pte,prot

	ptl_unlock1	spc,t0
	rfir
	nop

naitlb_miss_20:
	get_pgd		spc,ptp

	space_check	spc,t0,naitlb_fault

	L2_ptep		ptp,pte,t0,va,naitlb_check_alias_20

	ptl_lock	spc,ptp,pte,t0,t1,naitlb_check_alias_20
	update_accessed	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot,t1

	f_extend	pte,t1

	iitlbt          pte,prot

	ptl_unlock1	spc,t0
	rfir
	nop

naitlb_check_alias_20:
	do_alias	spc,t0,t1,va,pte,prot,naitlb_fault,20

	iitlbt          pte,prot

	rfir
	nop

#endif

#ifdef CONFIG_64BIT

dbit_trap_20w:
	space_adjust	spc,va,t0
	get_pgd		spc,ptp
	space_check	spc,t0,dbit_fault

	L3_ptep		ptp,pte,t0,va,dbit_fault

	ptl_lock	spc,ptp,pte,t0,t1,dbit_fault
	update_dirty	ptp,pte,t1

	make_insert_tlb	spc,pte,prot,t1
		
	idtlbt          pte,prot

	ptl_unlock0	spc,t0
	rfir
	nop
#else

dbit_trap_11:

	get_pgd		spc,ptp

	space_check	spc,t0,dbit_fault

	L2_ptep		ptp,pte,t0,va,dbit_fault

	ptl_lock	spc,ptp,pte,t0,t1,dbit_fault
	update_dirty	ptp,pte,t1

	make_insert_tlb_11	spc,pte,prot

	mfsp            %sr1,t1  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	idtlba		pte,(%sr1,va)
	idtlbp		prot,(%sr1,va)

	mtsp            t1, %sr1     /* Restore sr1 */

	ptl_unlock0	spc,t0
	rfir
	nop

dbit_trap_20:
	get_pgd		spc,ptp

	space_check	spc,t0,dbit_fault

	L2_ptep		ptp,pte,t0,va,dbit_fault

	ptl_lock	spc,ptp,pte,t0,t1,dbit_fault
	update_dirty	ptp,pte,t1

	make_insert_tlb	spc,pte,prot,t1

	f_extend	pte,t1
	
	idtlbt		pte,prot

	ptl_unlock0	spc,t0
	rfir
	nop
#endif

	.import handle_interruption,code

kernel_bad_space:
	b               intr_save
	ldi             31,%r8  /* Use an unused code */

dbit_fault:
	b               intr_save
	ldi             20,%r8

itlb_fault:
	b               intr_save
	ldi             PARISC_ITLB_TRAP,%r8

nadtlb_fault:
	b               intr_save
	ldi             17,%r8

naitlb_fault:
	b               intr_save
	ldi             16,%r8

dtlb_fault:
	b               intr_save
	ldi             15,%r8

	/* Register saving semantics for system calls:

	   %r1		   clobbered by system call macro in userspace
	   %r2		   saved in PT_REGS by gateway page
	   %r3  - %r18	   preserved by C code (saved by signal code)
	   %r19 - %r20	   saved in PT_REGS by gateway page
	   %r21 - %r22	   non-standard syscall args
			   stored in kernel stack by gateway page
	   %r23 - %r26	   arg3-arg0, saved in PT_REGS by gateway page
	   %r27 - %r30	   saved in PT_REGS by gateway page
	   %r31		   syscall return pointer
	 */

	/* Floating point registers (FIXME: what do we do with these?)

	   %fr0  - %fr3	   status/exception, not preserved
	   %fr4  - %fr7	   arguments
	   %fr8	 - %fr11   not preserved by C code
	   %fr12 - %fr21   preserved by C code
	   %fr22 - %fr31   not preserved by C code
	 */

	.macro	reg_save regs
	STREG	%r3, PT_GR3(\regs)
	STREG	%r4, PT_GR4(\regs)
	STREG	%r5, PT_GR5(\regs)
	STREG	%r6, PT_GR6(\regs)
	STREG	%r7, PT_GR7(\regs)
	STREG	%r8, PT_GR8(\regs)
	STREG	%r9, PT_GR9(\regs)
	STREG   %r10,PT_GR10(\regs)
	STREG   %r11,PT_GR11(\regs)
	STREG   %r12,PT_GR12(\regs)
	STREG   %r13,PT_GR13(\regs)
	STREG   %r14,PT_GR14(\regs)
	STREG   %r15,PT_GR15(\regs)
	STREG   %r16,PT_GR16(\regs)
	STREG   %r17,PT_GR17(\regs)
	STREG   %r18,PT_GR18(\regs)
	.endm

	.macro	reg_restore regs
	LDREG	PT_GR3(\regs), %r3
	LDREG	PT_GR4(\regs), %r4
	LDREG	PT_GR5(\regs), %r5
	LDREG	PT_GR6(\regs), %r6
	LDREG	PT_GR7(\regs), %r7
	LDREG	PT_GR8(\regs), %r8
	LDREG	PT_GR9(\regs), %r9
	LDREG   PT_GR10(\regs),%r10
	LDREG   PT_GR11(\regs),%r11
	LDREG   PT_GR12(\regs),%r12
	LDREG   PT_GR13(\regs),%r13
	LDREG   PT_GR14(\regs),%r14
	LDREG   PT_GR15(\regs),%r15
	LDREG   PT_GR16(\regs),%r16
	LDREG   PT_GR17(\regs),%r17
	LDREG   PT_GR18(\regs),%r18
	.endm

	.macro	fork_like name
ENTRY_CFI(sys_\name\()_wrapper)
	mfctl	%cr30,%r1
	ldo	TASK_REGS(%r1),%r1
	reg_save %r1
	mfctl	%cr27, %r28
	ldil	L%sys_\name, %r31
	be	R%sys_\name(%sr4,%r31)
	STREG	%r28, PT_CR27(%r1)
ENDPROC_CFI(sys_\name\()_wrapper)
	.endm

fork_like clone
fork_like clone3
fork_like fork
fork_like vfork

	/* Set the return value for the child */
ENTRY(child_return)
	BL	schedule_tail, %r2
	nop
finish_child_return:
	mfctl	%cr30,%r1
	ldo	TASK_REGS(%r1),%r1	 /* get pt regs */

	LDREG	PT_CR27(%r1), %r3
	mtctl	%r3, %cr27
	reg_restore %r1
	b	syscall_exit
	copy	%r0,%r28
END(child_return)

ENTRY_CFI(sys_rt_sigreturn_wrapper)
	mfctl	%cr30,%r26
	ldo	TASK_REGS(%r26),%r26	/* get pt regs */
	/* Don't save regs, we are going to restore them from sigcontext. */
	STREG	%r2, -RP_OFFSET(%r30)
#ifdef CONFIG_64BIT
	ldo	FRAME_SIZE(%r30), %r30
	BL	sys_rt_sigreturn,%r2
	ldo	-16(%r30),%r29		/* Reference param save area */
#else
	BL	sys_rt_sigreturn,%r2
	ldo	FRAME_SIZE(%r30), %r30
#endif

	ldo	-FRAME_SIZE(%r30), %r30
	LDREG	-RP_OFFSET(%r30), %r2

	/* FIXME: I think we need to restore a few more things here. */
	mfctl	%cr30,%r1
	ldo	TASK_REGS(%r1),%r1	/* get pt regs */
	reg_restore %r1

	/* If the signal was received while the process was blocked on a
	 * syscall, then r2 will take us to syscall_exit; otherwise r2 will
	 * take us to syscall_exit_rfi and on to intr_return.
	 */
	bv	%r0(%r2)
	LDREG	PT_GR28(%r1),%r28  /* reload original r28 for syscall_exit */
ENDPROC_CFI(sys_rt_sigreturn_wrapper)

ENTRY(syscall_exit)
	/* NOTE: Not all syscalls exit this way.  rt_sigreturn will exit
	 * via syscall_exit_rfi if the signal was received while the process
	 * was running.
	 */

	/* save return value now */
	mfctl     %cr30, %r1
	STREG     %r28,TASK_PT_GR28(%r1)

	/* Seems to me that dp could be wrong here, if the syscall involved
	 * calling a module, and nothing got round to restoring dp on return.
	 */
	loadgp

syscall_check_resched:

	/* check for reschedule */
	mfctl	%cr30,%r19
	LDREG	TASK_TI_FLAGS(%r19),%r19	/* long */
	bb,<,n	%r19, 31-TIF_NEED_RESCHED, syscall_do_resched /* forward */

	.import do_signal,code
syscall_check_sig:
	mfctl	%cr30,%r19
	LDREG	TASK_TI_FLAGS(%r19),%r19
	ldi	(_TIF_USER_WORK_MASK & ~_TIF_NEED_RESCHED), %r26
	and,COND(<>)	%r19, %r26, %r0
	b,n	syscall_restore	/* skip past if we've nothing to do */

syscall_do_signal:
	/* Save callee-save registers (for sigcontext).
	 * FIXME: After this point the process structure should be
	 * consistent with all the relevant state of the process
	 * before the syscall.  We need to verify this.
	 */
	mfctl	%cr30,%r1
	ldo	TASK_REGS(%r1), %r26		/* struct pt_regs *regs */
	reg_save %r26

#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif

	BL	do_notify_resume,%r2
	ldi	1, %r25				/* long in_syscall = 1 */

	mfctl	%cr30,%r1
	ldo	TASK_REGS(%r1), %r20		/* reload pt_regs */
	reg_restore %r20

	b,n     syscall_check_sig

syscall_restore:
	mfctl	%cr30,%r1

	/* Are we being ptraced? */
	LDREG	TASK_TI_FLAGS(%r1),%r19
	ldi	_TIF_SINGLESTEP|_TIF_BLOCKSTEP,%r2
	and,COND(=)	%r19,%r2,%r0
	b,n	syscall_restore_rfi

	ldo	TASK_PT_FR31(%r1),%r19		   /* reload fpregs */
	rest_fp	%r19

	LDREG	TASK_PT_SAR(%r1),%r19		   /* restore SAR */
	mtsar	%r19

	LDREG	TASK_PT_GR2(%r1),%r2		   /* restore user rp */
	LDREG	TASK_PT_GR19(%r1),%r19
	LDREG   TASK_PT_GR20(%r1),%r20
	LDREG	TASK_PT_GR21(%r1),%r21
	LDREG	TASK_PT_GR22(%r1),%r22
	LDREG	TASK_PT_GR23(%r1),%r23
	LDREG	TASK_PT_GR24(%r1),%r24
	LDREG	TASK_PT_GR25(%r1),%r25
	LDREG	TASK_PT_GR26(%r1),%r26
	LDREG	TASK_PT_GR27(%r1),%r27	   /* restore user dp */
	LDREG	TASK_PT_GR28(%r1),%r28	   /* syscall return value */
	LDREG	TASK_PT_GR29(%r1),%r29
	LDREG	TASK_PT_GR31(%r1),%r31	   /* restore syscall rp */

	/* NOTE: We use rsm/ssm pair to make this operation atomic */
	LDREG   TASK_PT_GR30(%r1),%r1              /* Get user sp */
	rsm     PSW_SM_I, %r0
	copy    %r1,%r30                           /* Restore user sp */
	mfsp    %sr3,%r1                           /* Get user space id */
	mtsp    %r1,%sr7                           /* Restore sr7 */
	ssm     PSW_SM_I, %r0

	/* Set sr2 to zero for userspace syscalls to work. */
	mtsp	%r0,%sr2 
	mtsp	%r1,%sr4			   /* Restore sr4 */
	mtsp	%r1,%sr5			   /* Restore sr5 */
	mtsp	%r1,%sr6			   /* Restore sr6 */

	depi	PRIV_USER,31,2,%r31	/* ensure return to user mode. */

#ifdef CONFIG_64BIT
	/* decide whether to reset the wide mode bit
	 *
	 * For a syscall, the W bit is stored in the lowest bit
	 * of sp.  Extract it and reset W if it is zero */
	extrd,u,*<>	%r30,63,1,%r1
	rsm	PSW_SM_W, %r0
	/* now reset the lowest bit of sp if it was set */
	xor	%r30,%r1,%r30
#endif
	be,n    0(%sr3,%r31)                       /* return to user space */

	/* We have to return via an RFI, so that PSW T and R bits can be set
	 * appropriately.
	 * This sets up pt_regs so we can return via intr_restore, which is not
	 * the most efficient way of doing things, but it works.
	 */
syscall_restore_rfi:
	ldo	-1(%r0),%r2			   /* Set recovery cntr to -1 */
	mtctl	%r2,%cr0			   /*   for immediate trap */
	LDREG	TASK_PT_PSW(%r1),%r2		   /* Get old PSW */
	ldi	0x0b,%r20			   /* Create new PSW */
	depi	-1,13,1,%r20			   /* C, Q, D, and I bits */

	/* The values of SINGLESTEP_BIT and BLOCKSTEP_BIT are
	 * set in thread_info.h and converted to PA bitmap
	 * numbers in asm-offsets.c */

	/* if ((%r19.SINGLESTEP_BIT)) { %r20.27=1} */
	extru,=	%r19,TIF_SINGLESTEP_PA_BIT,1,%r0
	depi	-1,27,1,%r20			   /* R bit */

	/* if ((%r19.BLOCKSTEP_BIT)) { %r20.7=1} */
	extru,= %r19,TIF_BLOCKSTEP_PA_BIT,1,%r0
	depi	-1,7,1,%r20			   /* T bit */

	STREG	%r20,TASK_PT_PSW(%r1)

	/* Always store space registers, since sr3 can be changed (e.g. fork) */

	mfsp    %sr3,%r25
	STREG   %r25,TASK_PT_SR3(%r1)
	STREG   %r25,TASK_PT_SR4(%r1)
	STREG   %r25,TASK_PT_SR5(%r1)
	STREG   %r25,TASK_PT_SR6(%r1)
	STREG   %r25,TASK_PT_SR7(%r1)
	STREG   %r25,TASK_PT_IASQ0(%r1)
	STREG   %r25,TASK_PT_IASQ1(%r1)

	/* XXX W bit??? */
	/* Now if old D bit is clear, it means we didn't save all registers
	 * on syscall entry, so do that now.  This only happens on TRACEME
	 * calls, or if someone attached to us while we were on a syscall.
	 * We could make this more efficient by not saving r3-r18, but
	 * then we wouldn't be able to use the common intr_restore path.
	 * It is only for traced processes anyway, so performance is not
	 * an issue.
	 */
	bb,<	%r2,30,pt_regs_ok		   /* Branch if D set */
	ldo	TASK_REGS(%r1),%r25
	reg_save %r25				   /* Save r3 to r18 */

	/* Save the current sr */
	mfsp	%sr0,%r2
	STREG	%r2,TASK_PT_SR0(%r1)

	/* Save the scratch sr */
	mfsp	%sr1,%r2
	STREG	%r2,TASK_PT_SR1(%r1)

	/* sr2 should be set to zero for userspace syscalls */
	STREG	%r0,TASK_PT_SR2(%r1)

	LDREG	TASK_PT_GR31(%r1),%r2
	depi	PRIV_USER,31,2,%r2	/* ensure return to user mode. */
	STREG   %r2,TASK_PT_IAOQ0(%r1)
	ldo	4(%r2),%r2
	STREG	%r2,TASK_PT_IAOQ1(%r1)
	b	intr_restore
	copy	%r25,%r16

pt_regs_ok:
	LDREG	TASK_PT_IAOQ0(%r1),%r2
	depi	PRIV_USER,31,2,%r2	/* ensure return to user mode. */
	STREG	%r2,TASK_PT_IAOQ0(%r1)
	LDREG	TASK_PT_IAOQ1(%r1),%r2
	depi	PRIV_USER,31,2,%r2
	STREG	%r2,TASK_PT_IAOQ1(%r1)
	b	intr_restore
	copy	%r25,%r16

syscall_do_resched:
	load32	syscall_check_resched,%r2 /* if resched, we start over again */
	load32	schedule,%r19
	bv	%r0(%r19)		/* jumps to schedule() */
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29		/* Reference param save area */
#else
	nop
#endif
END(syscall_exit)


#ifdef CONFIG_FUNCTION_TRACER

	.import ftrace_function_trampoline,code
	.align L1_CACHE_BYTES
ENTRY_CFI(mcount, caller)
_mcount:
	.export _mcount,data
	/*
	 * The 64bit mcount() function pointer needs 4 dwords, of which the
	 * first two are free.  We optimize it here and put 2 instructions for
	 * calling mcount(), and 2 instructions for ftrace_stub().  That way we
	 * have all on one L1 cacheline.
	 */
	ldi	0, %arg3
	b	ftrace_function_trampoline
	copy	%r3, %arg2	/* caller original %sp */
ftrace_stub:
	.globl ftrace_stub
        .type  ftrace_stub, @function
#ifdef CONFIG_64BIT
	bve	(%rp)
#else
	bv	%r0(%rp)
#endif
	nop
#ifdef CONFIG_64BIT
	.dword mcount
	.dword 0 /* code in head.S puts value of global gp here */
#endif
ENDPROC_CFI(mcount)

#ifdef CONFIG_DYNAMIC_FTRACE

#ifdef CONFIG_64BIT
#define FTRACE_FRAME_SIZE (2*FRAME_SIZE)
#else
#define FTRACE_FRAME_SIZE FRAME_SIZE
#endif
ENTRY_CFI(ftrace_caller, caller,frame=FTRACE_FRAME_SIZE,CALLS,SAVE_RP,SAVE_SP)
ftrace_caller:
	.global ftrace_caller

	STREG	%r3, -FTRACE_FRAME_SIZE+1*REG_SZ(%sp)
	ldo	-FTRACE_FRAME_SIZE(%sp), %r3
	STREG	%rp, -RP_OFFSET(%r3)

	/* Offset 0 is already allocated for %r1 */
	STREG	%r23, 2*REG_SZ(%r3)
	STREG	%r24, 3*REG_SZ(%r3)
	STREG	%r25, 4*REG_SZ(%r3)
	STREG	%r26, 5*REG_SZ(%r3)
	STREG	%r28, 6*REG_SZ(%r3)
	STREG	%r29, 7*REG_SZ(%r3)
#ifdef CONFIG_64BIT
	STREG	%r19, 8*REG_SZ(%r3)
	STREG	%r20, 9*REG_SZ(%r3)
	STREG	%r21, 10*REG_SZ(%r3)
	STREG	%r22, 11*REG_SZ(%r3)
	STREG	%r27, 12*REG_SZ(%r3)
	STREG	%r31, 13*REG_SZ(%r3)
	loadgp
	ldo	-16(%sp),%r29
#endif
	LDREG	0(%r3), %r25
	copy	%rp, %r26
	ldo	-8(%r25), %r25
	ldi	0, %r23		/* no pt_regs */
	b,l	ftrace_function_trampoline, %rp
	copy	%r3, %r24

	LDREG	-RP_OFFSET(%r3), %rp
	LDREG	2*REG_SZ(%r3), %r23
	LDREG	3*REG_SZ(%r3), %r24
	LDREG	4*REG_SZ(%r3), %r25
	LDREG	5*REG_SZ(%r3), %r26
	LDREG	6*REG_SZ(%r3), %r28
	LDREG	7*REG_SZ(%r3), %r29
#ifdef CONFIG_64BIT
	LDREG	8*REG_SZ(%r3), %r19
	LDREG	9*REG_SZ(%r3), %r20
	LDREG	10*REG_SZ(%r3), %r21
	LDREG	11*REG_SZ(%r3), %r22
	LDREG	12*REG_SZ(%r3), %r27
	LDREG	13*REG_SZ(%r3), %r31
#endif
	LDREG	1*REG_SZ(%r3), %r3

	LDREGM	-FTRACE_FRAME_SIZE(%sp), %r1
	/* Adjust return point to jump back to beginning of traced function */
	ldo	-4(%r1), %r1
	bv,n	(%r1)

ENDPROC_CFI(ftrace_caller)

#ifdef CONFIG_HAVE_DYNAMIC_FTRACE_WITH_REGS
ENTRY_CFI(ftrace_regs_caller,caller,frame=FTRACE_FRAME_SIZE+PT_SZ_ALGN,
	CALLS,SAVE_RP,SAVE_SP)
ftrace_regs_caller:
	.global ftrace_regs_caller

	ldo	-FTRACE_FRAME_SIZE(%sp), %r1
	STREG	%rp, -RP_OFFSET(%r1)

	copy	%sp, %r1
	ldo	PT_SZ_ALGN(%sp), %sp

	STREG	%rp, PT_GR2(%r1)
	STREG	%r3, PT_GR3(%r1)
	STREG	%r4, PT_GR4(%r1)
	STREG	%r5, PT_GR5(%r1)
	STREG	%r6, PT_GR6(%r1)
	STREG	%r7, PT_GR7(%r1)
	STREG	%r8, PT_GR8(%r1)
	STREG	%r9, PT_GR9(%r1)
	STREG   %r10, PT_GR10(%r1)
	STREG   %r11, PT_GR11(%r1)
	STREG   %r12, PT_GR12(%r1)
	STREG   %r13, PT_GR13(%r1)
	STREG   %r14, PT_GR14(%r1)
	STREG   %r15, PT_GR15(%r1)
	STREG   %r16, PT_GR16(%r1)
	STREG   %r17, PT_GR17(%r1)
	STREG   %r18, PT_GR18(%r1)
	STREG	%r19, PT_GR19(%r1)
	STREG	%r20, PT_GR20(%r1)
	STREG	%r21, PT_GR21(%r1)
	STREG	%r22, PT_GR22(%r1)
	STREG	%r23, PT_GR23(%r1)
	STREG	%r24, PT_GR24(%r1)
	STREG	%r25, PT_GR25(%r1)
	STREG	%r26, PT_GR26(%r1)
	STREG	%r27, PT_GR27(%r1)
	STREG	%r28, PT_GR28(%r1)
	STREG	%r29, PT_GR29(%r1)
	STREG	%r30, PT_GR30(%r1)
	STREG	%r31, PT_GR31(%r1)
	mfctl	%cr11, %r26
	STREG	%r26, PT_SAR(%r1)

	copy	%rp, %r26
	LDREG	-FTRACE_FRAME_SIZE-PT_SZ_ALGN(%sp), %r25
	ldo	-8(%r25), %r25
	ldo	-FTRACE_FRAME_SIZE(%r1), %arg2
	b,l	ftrace_function_trampoline, %rp
	copy	%r1, %arg3 /* struct pt_regs */

	ldo	-PT_SZ_ALGN(%sp), %r1

	LDREG	PT_SAR(%r1), %rp
	mtctl	%rp, %cr11

	LDREG	PT_GR2(%r1), %rp
	LDREG	PT_GR3(%r1), %r3
	LDREG	PT_GR4(%r1), %r4
	LDREG	PT_GR5(%r1), %r5
	LDREG	PT_GR6(%r1), %r6
	LDREG	PT_GR7(%r1), %r7
	LDREG	PT_GR8(%r1), %r8
	LDREG	PT_GR9(%r1), %r9
	LDREG   PT_GR10(%r1),%r10
	LDREG   PT_GR11(%r1),%r11
	LDREG   PT_GR12(%r1),%r12
	LDREG   PT_GR13(%r1),%r13
	LDREG   PT_GR14(%r1),%r14
	LDREG   PT_GR15(%r1),%r15
	LDREG   PT_GR16(%r1),%r16
	LDREG   PT_GR17(%r1),%r17
	LDREG   PT_GR18(%r1),%r18
	LDREG   PT_GR19(%r1),%r19
	LDREG   PT_GR20(%r1),%r20
	LDREG   PT_GR21(%r1),%r21
	LDREG   PT_GR22(%r1),%r22
	LDREG   PT_GR23(%r1),%r23
	LDREG   PT_GR24(%r1),%r24
	LDREG   PT_GR25(%r1),%r25
	LDREG   PT_GR26(%r1),%r26
	LDREG   PT_GR27(%r1),%r27
	LDREG   PT_GR28(%r1),%r28
	LDREG   PT_GR29(%r1),%r29
	LDREG   PT_GR30(%r1),%r30
	LDREG   PT_GR31(%r1),%r31

	ldo	-PT_SZ_ALGN(%sp), %sp
	LDREGM	-FTRACE_FRAME_SIZE(%sp), %r1
	/* Adjust return point to jump back to beginning of traced function */
	ldo	-4(%r1), %r1
	bv,n	(%r1)

ENDPROC_CFI(ftrace_regs_caller)

#endif
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.align 8
ENTRY_CFI(return_to_handler, caller,frame=FRAME_SIZE)
	.export parisc_return_to_handler,data
parisc_return_to_handler:
	copy %r3,%r1
	STREG %r0,-RP_OFFSET(%sp)	/* store 0 as %rp */
	copy %sp,%r3
	STREGM %r1,FRAME_SIZE(%sp)
	STREG %ret0,8(%r3)
	STREG %ret1,16(%r3)

#ifdef CONFIG_64BIT
	loadgp
#endif

	/* call ftrace_return_to_handler(0) */
	.import ftrace_return_to_handler,code
	load32 ftrace_return_to_handler,%ret0
	load32 .Lftrace_ret,%r2
#ifdef CONFIG_64BIT
	ldo -16(%sp),%ret1		/* Reference param save area */
	bve	(%ret0)
#else
	bv	%r0(%ret0)
#endif
	ldi 0,%r26
.Lftrace_ret:
	copy %ret0,%rp

	/* restore original return values */
	LDREG 8(%r3),%ret0
	LDREG 16(%r3),%ret1

	/* return from function */
#ifdef CONFIG_64BIT
	bve	(%rp)
#else
	bv	%r0(%rp)
#endif
	LDREGM -FRAME_SIZE(%sp),%r3
ENDPROC_CFI(return_to_handler)

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#endif	/* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_IRQSTACKS
/* void call_on_stack(unsigned long param1, void *func,
		      unsigned long new_stack) */
ENTRY_CFI(call_on_stack, FRAME=2*FRAME_SIZE,CALLS,SAVE_RP,SAVE_SP)
ENTRY(_call_on_stack)
	copy	%sp, %r1

	/* Regarding the HPPA calling conventions for function pointers,
	   we assume the PIC register is not changed across call.  For
	   CONFIG_64BIT, the argument pointer is left to point at the
	   argument region allocated for the call to call_on_stack. */

	/* Switch to new stack.  We allocate two frames.  */
	ldo	2*FRAME_SIZE(%arg2), %sp
# ifdef CONFIG_64BIT
	/* Save previous stack pointer and return pointer in frame marker */
	STREG	%rp, -FRAME_SIZE-RP_OFFSET(%sp)
	/* Calls always use function descriptor */
	LDREG	16(%arg1), %arg1
	bve,l	(%arg1), %rp
	STREG	%r1, -FRAME_SIZE-REG_SZ(%sp)
	LDREG	-FRAME_SIZE-RP_OFFSET(%sp), %rp
	bve	(%rp)
	LDREG	-FRAME_SIZE-REG_SZ(%sp), %sp
# else
	/* Save previous stack pointer and return pointer in frame marker */
	STREG	%r1, -FRAME_SIZE-REG_SZ(%sp)
	STREG	%rp, -FRAME_SIZE-RP_OFFSET(%sp)
	/* Calls use function descriptor if PLABEL bit is set */
	bb,>=,n	%arg1, 30, 1f
	depwi	0,31,2, %arg1
	LDREG	0(%arg1), %arg1
1:
	be,l	0(%sr4,%arg1), %sr0, %r31
	copy	%r31, %rp
	LDREG	-FRAME_SIZE-RP_OFFSET(%sp), %rp
	bv	(%rp)
	LDREG	-FRAME_SIZE-REG_SZ(%sp), %sp
# endif /* CONFIG_64BIT */
ENDPROC_CFI(call_on_stack)
#endif /* CONFIG_IRQSTACKS */

ENTRY_CFI(get_register)
	/*
	 * get_register is used by the non access tlb miss handlers to
	 * copy the value of the general register specified in r8 into
	 * r1. This routine can't be used for shadowed registers, since
	 * the rfir will restore the original value. So, for the shadowed
	 * registers we put a -1 into r1 to indicate that the register
	 * should not be used (the register being copied could also have
	 * a -1 in it, but that is OK, it just means that we will have
	 * to use the slow path instead).
	 */
	blr     %r8,%r0
	nop
	bv      %r0(%r25)    /* r0 */
	copy    %r0,%r1
	bv      %r0(%r25)    /* r1 - shadowed */
	ldi     -1,%r1
	bv      %r0(%r25)    /* r2 */
	copy    %r2,%r1
	bv      %r0(%r25)    /* r3 */
	copy    %r3,%r1
	bv      %r0(%r25)    /* r4 */
	copy    %r4,%r1
	bv      %r0(%r25)    /* r5 */
	copy    %r5,%r1
	bv      %r0(%r25)    /* r6 */
	copy    %r6,%r1
	bv      %r0(%r25)    /* r7 */
	copy    %r7,%r1
	bv      %r0(%r25)    /* r8 - shadowed */
	ldi     -1,%r1
	bv      %r0(%r25)    /* r9 - shadowed */
	ldi     -1,%r1
	bv      %r0(%r25)    /* r10 */
	copy    %r10,%r1
	bv      %r0(%r25)    /* r11 */
	copy    %r11,%r1
	bv      %r0(%r25)    /* r12 */
	copy    %r12,%r1
	bv      %r0(%r25)    /* r13 */
	copy    %r13,%r1
	bv      %r0(%r25)    /* r14 */
	copy    %r14,%r1
	bv      %r0(%r25)    /* r15 */
	copy    %r15,%r1
	bv      %r0(%r25)    /* r16 - shadowed */
	ldi     -1,%r1
	bv      %r0(%r25)    /* r17 - shadowed */
	ldi     -1,%r1
	bv      %r0(%r25)    /* r18 */
	copy    %r18,%r1
	bv      %r0(%r25)    /* r19 */
	copy    %r19,%r1
	bv      %r0(%r25)    /* r20 */
	copy    %r20,%r1
	bv      %r0(%r25)    /* r21 */
	copy    %r21,%r1
	bv      %r0(%r25)    /* r22 */
	copy    %r22,%r1
	bv      %r0(%r25)    /* r23 */
	copy    %r23,%r1
	bv      %r0(%r25)    /* r24 - shadowed */
	ldi     -1,%r1
	bv      %r0(%r25)    /* r25 - shadowed */
	ldi     -1,%r1
	bv      %r0(%r25)    /* r26 */
	copy    %r26,%r1
	bv      %r0(%r25)    /* r27 */
	copy    %r27,%r1
	bv      %r0(%r25)    /* r28 */
	copy    %r28,%r1
	bv      %r0(%r25)    /* r29 */
	copy    %r29,%r1
	bv      %r0(%r25)    /* r30 */
	copy    %r30,%r1
	bv      %r0(%r25)    /* r31 */
	copy    %r31,%r1
ENDPROC_CFI(get_register)


ENTRY_CFI(set_register)
	/*
	 * set_register is used by the non access tlb miss handlers to
	 * copy the value of r1 into the general register specified in
	 * r8.
	 */
	blr     %r8,%r0
	nop
	bv      %r0(%r25)    /* r0 (silly, but it is a place holder) */
	copy    %r1,%r0
	bv      %r0(%r25)    /* r1 */
	copy    %r1,%r1
	bv      %r0(%r25)    /* r2 */
	copy    %r1,%r2
	bv      %r0(%r25)    /* r3 */
	copy    %r1,%r3
	bv      %r0(%r25)    /* r4 */
	copy    %r1,%r4
	bv      %r0(%r25)    /* r5 */
	copy    %r1,%r5
	bv      %r0(%r25)    /* r6 */
	copy    %r1,%r6
	bv      %r0(%r25)    /* r7 */
	copy    %r1,%r7
	bv      %r0(%r25)    /* r8 */
	copy    %r1,%r8
	bv      %r0(%r25)    /* r9 */
	copy    %r1,%r9
	bv      %r0(%r25)    /* r10 */
	copy    %r1,%r10
	bv      %r0(%r25)    /* r11 */
	copy    %r1,%r11
	bv      %r0(%r25)    /* r12 */
	copy    %r1,%r12
	bv      %r0(%r25)    /* r13 */
	copy    %r1,%r13
	bv      %r0(%r25)    /* r14 */
	copy    %r1,%r14
	bv      %r0(%r25)    /* r15 */
	copy    %r1,%r15
	bv      %r0(%r25)    /* r16 */
	copy    %r1,%r16
	bv      %r0(%r25)    /* r17 */
	copy    %r1,%r17
	bv      %r0(%r25)    /* r18 */
	copy    %r1,%r18
	bv      %r0(%r25)    /* r19 */
	copy    %r1,%r19
	bv      %r0(%r25)    /* r20 */
	copy    %r1,%r20
	bv      %r0(%r25)    /* r21 */
	copy    %r1,%r21
	bv      %r0(%r25)    /* r22 */
	copy    %r1,%r22
	bv      %r0(%r25)    /* r23 */
	copy    %r1,%r23
	bv      %r0(%r25)    /* r24 */
	copy    %r1,%r24
	bv      %r0(%r25)    /* r25 */
	copy    %r1,%r25
	bv      %r0(%r25)    /* r26 */
	copy    %r1,%r26
	bv      %r0(%r25)    /* r27 */
	copy    %r1,%r27
	bv      %r0(%r25)    /* r28 */
	copy    %r1,%r28
	bv      %r0(%r25)    /* r29 */
	copy    %r1,%r29
	bv      %r0(%r25)    /* r30 */
	copy    %r1,%r30
	bv      %r0(%r25)    /* r31 */
	copy    %r1,%r31
ENDPROC_CFI(set_register)

