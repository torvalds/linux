/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 * kernel entry points (interruptions, system call wrappers)
 *  Copyright (C) 1999,2000 Philipp Rumpf 
 *  Copyright (C) 1999 SuSE GmbH Nuernberg 
 *  Copyright (C) 2000 Hewlett-Packard (John Marvin)
 *  Copyright (C) 1999 Hewlett-Packard (Frank Rowand)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <asm/asm-offsets.h>

/* we have the following possibilities to act on an interruption:
 *  - handle in assembly and use shadowed registers only
 *  - save registers to kernel stack and handle in assembly or C */


#include <asm/psw.h>
#include <asm/assembly.h>	/* for LDREG/STREG defines */
#include <asm/pgtable.h>
#include <asm/signal.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>

#ifdef CONFIG_64BIT
#define CMPIB           cmpib,*
#define CMPB            cmpb,*
#define COND(x)		*x

	.level 2.0w
#else
#define CMPIB           cmpib,
#define CMPB            cmpb,
#define COND(x)		x

	.level 2.0
#endif

	.import         pa_dbit_lock,data

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

	/* Switch to virtual mapping, trashing only %r1 */
	.macro  virt_map
	/* pcxt_ssm_bug */
	rsm	PSW_SM_I, %r0	/* barrier for "Relied upon Translation */
	mtsp	%r0, %sr4
	mtsp	%r0, %sr5
	mfsp	%sr7, %r1
	or,=    %r0,%r1,%r0	/* Only save sr7 in sr3 if sr7 != 0 */
	mtsp	%r1, %sr3
	tovirt_r1 %r29
	load32	KERNEL_PSW, %r1

	rsm     PSW_SM_QUIET,%r0	/* second "heavy weight" ctl op */
	mtsp	%r0, %sr6
	mtsp	%r0, %sr7
	mtctl	%r0, %cr17	/* Clear IIASQ tail */
	mtctl	%r0, %cr17	/* Clear IIASQ head */
	mtctl	%r1, %ipsw
	load32	4f, %r1
	mtctl	%r1, %cr18	/* Set IIAOQ tail */
	ldo	4(%r1), %r1
	mtctl	%r1, %cr18	/* Set IIAOQ head */
	rfir
	nop
4:
	.endm

	/*
	 * The "get_stack" macros are responsible for determining the
	 * kernel stack value.
	 *
	 * For Faults:
	 *      If sr7 == 0
	 *          Already using a kernel stack, so call the
	 *          get_stack_use_r30 macro to push a pt_regs structure
	 *          on the stack, and store registers there.
	 *      else
	 *          Need to set up a kernel stack, so call the
	 *          get_stack_use_cr30 macro to set up a pointer
	 *          to the pt_regs structure contained within the
	 *          task pointer pointed to by cr30. Set the stack
	 *          pointer to point to the end of the task structure.
	 *
	 * For Interrupts:
	 *      If sr7 == 0
	 *          Already using a kernel stack, check to see if r30
	 *          is already pointing to the per processor interrupt
	 *          stack. If it is, call the get_stack_use_r30 macro
	 *          to push a pt_regs structure on the stack, and store
	 *          registers there. Otherwise, call get_stack_use_cr31
	 *          to get a pointer to the base of the interrupt stack
	 *          and push a pt_regs structure on that stack.
	 *      else
	 *          Need to set up a kernel stack, so call the
	 *          get_stack_use_cr30 macro to set up a pointer
	 *          to the pt_regs structure contained within the
	 *          task pointer pointed to by cr30. Set the stack
	 *          pointer to point to the end of the task structure.
	 *          N.B: We don't use the interrupt stack for the
	 *          first interrupt from userland, because signals/
	 *          resched's are processed when returning to userland,
	 *          and we can sleep in those cases.
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
	 *
	 * N.B. TASK_SZ_ALGN and PT_SZ_ALGN include space for a stack frame.
	 */

	.macro  get_stack_use_cr30

	/* we save the registers in the task struct */

	mfctl   %cr30, %r1
	tophys  %r1,%r9
	LDREG	TI_TASK(%r9), %r1	/* thread_info -> task_struct */
	tophys  %r1,%r9
	ldo     TASK_REGS(%r9),%r9
	STREG   %r30, PT_GR30(%r9)
	STREG   %r29,PT_GR29(%r9)
	STREG   %r26,PT_GR26(%r9)
	copy    %r9,%r29
	mfctl   %cr30, %r1
	ldo	THREAD_SZ_ALGN(%r1), %r30
	.endm

	.macro  get_stack_use_r30

	/* we put a struct pt_regs on the stack and save the registers there */

	tophys  %r30,%r9
	STREG   %r30,PT_GR30(%r9)
	ldo	PT_SZ_ALGN(%r30),%r30
	STREG   %r29,PT_GR29(%r9)
	STREG   %r26,PT_GR26(%r9)
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
	.word	PA(os_hpmc)	/* address of handler */
	.word	0		/* length of handler */
	.endm

	/*
	 * Performance Note: Instructions will be moved up into
	 * this part of the code later on, once we are sure
	 * that the tlb miss handlers are close to final form.
	 */

	/* Register definitions for tlb miss handler macros */

	va  = r8	/* virtual address for which the trap occured */
	spc = r24	/* space for which the trap occured */

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
	 *
	 * Note: naitlb misses will be treated
	 * as an ordinary itlb miss for now.
	 * However, note that naitlb misses
	 * have the faulting address in the
	 * IOR/ISR.
	 */

	.macro	naitlb_11 code

	mfctl	%isr,spc
	b	itlb_miss_11
	mfctl 	%ior,va
	/* FIXME: If user causes a naitlb miss, the priv level may not be in
	 * lower bits of va, where the itlb miss handler is expecting them
	 */

	.align		32
	.endm
#endif
	
	/*
	 * naitlb miss interruption handler (parisc 2.0)
	 *
	 * Note: naitlb misses will be treated
	 * as an ordinary itlb miss for now.
	 * However, note that naitlb misses
	 * have the faulting address in the
	 * IOR/ISR.
	 */

	.macro	naitlb_20 code

	mfctl	%isr,spc
#ifdef CONFIG_64BIT
	b       itlb_miss_20w
#else
	b	itlb_miss_20
#endif
	mfctl 	%ior,va
	/* FIXME: If user causes a naitlb miss, the priv level may not be in
	 * lower bits of va, where the itlb miss handler is expecting them
	 */

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

	/* The following are simple 32 vs 64 bit instruction
	 * abstractions for the macros */
	.macro		EXTR	reg1,start,length,reg2
#ifdef CONFIG_64BIT
	extrd,u		\reg1,32+\start,\length,\reg2
#else
	extrw,u		\reg1,\start,\length,\reg2
#endif
	.endm

	.macro		DEP	reg1,start,length,reg2
#ifdef CONFIG_64BIT
	depd		\reg1,32+\start,\length,\reg2
#else
	depw		\reg1,\start,\length,\reg2
#endif
	.endm

	.macro		DEPI	val,start,length,reg
#ifdef CONFIG_64BIT
	depdi		\val,32+\start,\length,\reg
#else
	depwi		\val,\start,\length,\reg
#endif
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
#if PT_NLEVELS == 3
	EXTR		\va,31-ASM_PMD_SHIFT,ASM_BITS_PER_PMD,\index
#else
	EXTR		\va,31-ASM_PGDIR_SHIFT,ASM_BITS_PER_PGD,\index
#endif
	DEP             %r0,31,PAGE_SHIFT,\pmd  /* clear offset */
	copy		%r0,\pte
	ldw,s		\index(\pmd),\pmd
	bb,>=,n		\pmd,_PxD_PRESENT_BIT,\fault
	DEP		%r0,31,PxD_FLAG_SHIFT,\pmd /* clear flags */
	copy		\pmd,%r9
#ifdef CONFIG_64BIT
	shld		%r9,PxD_VALUE_SHIFT,\pmd
#else
	shlw		%r9,PxD_VALUE_SHIFT,\pmd
#endif
	EXTR		\va,31-PAGE_SHIFT,ASM_BITS_PER_PTE,\index
	DEP		%r0,31,PAGE_SHIFT,\pmd  /* clear offset */
	shladd		\index,BITS_PER_PTE_ENTRY,\pmd,\pmd
	LDREG		%r0(\pmd),\pte		/* pmd is now pte */
	bb,>=,n		\pte,_PAGE_PRESENT_BIT,\fault
	.endm

	/* Look up PTE in a 3-Level scheme.
	 *
	 * Here we implement a Hybrid L2/L3 scheme: we allocate the
	 * first pmd adjacent to the pgd.  This means that we can
	 * subtract a constant offset to get to it.  The pmd and pgd
	 * sizes are arranged so that a single pmd covers 4GB (giving
	 * a full LP64 process access to 8TB) so our lookups are
	 * effectively L2 for the first 4GB of the kernel (i.e. for
	 * all ILP32 processes and all the kernel for machines with
	 * under 4GB of memory) */
	.macro		L3_ptep pgd,pte,index,va,fault
	extrd,u		\va,63-ASM_PGDIR_SHIFT,ASM_BITS_PER_PGD,\index
	copy		%r0,\pte
	extrd,u,*=	\va,31,32,%r0
	ldw,s		\index(\pgd),\pgd
	extrd,u,*=	\va,31,32,%r0
	bb,>=,n		\pgd,_PxD_PRESENT_BIT,\fault
	extrd,u,*=	\va,31,32,%r0
	shld		\pgd,PxD_VALUE_SHIFT,\index
	extrd,u,*=	\va,31,32,%r0
	copy		\index,\pgd
	extrd,u,*<>	\va,31,32,%r0
	ldo		ASM_PGD_PMD_OFFSET(\pgd),\pgd
	L2_ptep		\pgd,\pte,\index,\va,\fault
	.endm

	/* Set the _PAGE_ACCESSED bit of the PTE.  Be clever and
	 * don't needlessly dirty the cache line if it was already set */
	.macro		update_ptep	ptep,pte,tmp,tmp1
	ldi		_PAGE_ACCESSED,\tmp1
	or		\tmp1,\pte,\tmp
	and,COND(<>)	\tmp1,\pte,%r0
	STREG		\tmp,0(\ptep)
	.endm

	/* Set the dirty bit (and accessed bit).  No need to be
	 * clever, this is only used from the dirty fault */
	.macro		update_dirty	ptep,pte,tmp
	ldi		_PAGE_ACCESSED|_PAGE_DIRTY,\tmp
	or		\tmp,\pte,\pte
	STREG		\pte,0(\ptep)
	.endm

	/* Convert the pte and prot to tlb insertion values.  How
	 * this happens is quite subtle, read below */
	.macro		make_insert_tlb	spc,pte,prot
	space_to_prot   \spc \prot        /* create prot id from space */
	/* The following is the real subtlety.  This is depositing
	 * T <-> _PAGE_REFTRAP
	 * D <-> _PAGE_DIRTY
	 * B <-> _PAGE_DMB (memory break)
	 *
	 * Then incredible subtlety: The access rights are
	 * _PAGE_GATEWAY _PAGE_EXEC _PAGE_READ
	 * See 3-14 of the parisc 2.0 manual
	 *
	 * Finally, _PAGE_READ goes in the top bit of PL1 (so we
	 * trigger an access rights trap in user space if the user
	 * tries to read an unreadable page */
	depd            \pte,8,7,\prot

	/* PAGE_USER indicates the page can be read with user privileges,
	 * so deposit X1|11 to PL1|PL2 (remember the upper bit of PL1
	 * contains _PAGE_READ */
	extrd,u,*=      \pte,_PAGE_USER_BIT+32,1,%r0
	depdi		7,11,3,\prot
	/* If we're a gateway page, drop PL2 back to zero for promotion
	 * to kernel privilege (so we can execute the page as kernel).
	 * Any privilege promotion page always denys read and write */
	extrd,u,*= 	\pte,_PAGE_GATEWAY_BIT+32,1,%r0
	depd		%r0,11,2,\prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for iitlbt */

	depd		%r0,63,PAGE_SHIFT,\pte
	extrd,u		\pte,56,32,\pte
	.endm

	/* Identical macro to make_insert_tlb above, except it
	 * makes the tlb entry for the differently formatted pa11
	 * insertion instructions */
	.macro		make_insert_tlb_11	spc,pte,prot
	zdep		\spc,30,15,\prot
	dep		\pte,8,7,\prot
	extru,=		\pte,_PAGE_NO_CACHE_BIT,1,%r0
	depi		1,12,1,\prot
	extru,=         \pte,_PAGE_USER_BIT,1,%r0
	depi		7,11,3,\prot   /* Set for user space (1 rsvd for read) */
	extru,= 	\pte,_PAGE_GATEWAY_BIT,1,%r0
	depi		0,11,2,\prot	/* If Gateway, Set PL2 to 0 */

	/* Get rid of prot bits and convert to page addr for iitlba */

	depi		0,31,12,\pte
	extru		\pte,24,25,\pte

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

	/* The alias region is an 8MB aligned 16MB to do clear and
	 * copy user pages at addresses congruent with the user
	 * virtual address.
	 *
	 * To use the alias page, you set %r26 up with the to TLB
	 * entry (identifying the physical page) and %r23 up with
	 * the from tlb entry (or nothing if only a to entry---for
	 * clear_user_page_asm) */
	.macro		do_alias	spc,tmp,tmp1,va,pte,prot,fault
	cmpib,COND(<>),n 0,\spc,\fault
	ldil		L%(TMPALIAS_MAP_START),\tmp
#if defined(CONFIG_64BIT) && (TMPALIAS_MAP_START >= 0x80000000)
	/* on LP64, ldi will sign extend into the upper 32 bits,
	 * which is behaviour we don't want */
	depdi		0,31,32,\tmp
#endif
	copy		\va,\tmp1
	DEPI		0,31,23,\tmp1
	cmpb,COND(<>),n	\tmp,\tmp1,\fault
	ldi		(_PAGE_DIRTY|_PAGE_WRITE|_PAGE_READ),\prot
	depd,z		\prot,8,7,\prot
	/*
	 * OK, it is in the temp alias region, check whether "from" or "to".
	 * Check "subtle" note in pacache.S re: r23/r26.
	 */
#ifdef CONFIG_64BIT
	extrd,u,*=	\va,41,1,%r0
#else
	extrw,u,=	\va,9,1,%r0
#endif
	or,COND(tr)	%r23,%r0,\pte
	or		%r26,%r0,\pte
	.endm 


	/*
	 * Align fault_vector_20 on 4K boundary so that both
	 * fault_vector_11 and fault_vector_20 are on the
	 * same page. This is only necessary as long as we
	 * write protect the kernel text, which we may stop
	 * doing once we use large page translations to cover
	 * the static part of the kernel address space.
	 */

	.export fault_vector_20

	.text

	.align 4096

fault_vector_20:
	/* First vector is invalid (0) */
	.ascii	"cows can fly"
	.byte 0
	.align 32

	hpmc		 1
	def		 2
	def		 3
	extint		 4
	def		 5
	itlb_20		 6
	def		 7
	def		 8
	def              9
	def		10
	def		11
	def		12
	def		13
	def		14
	dtlb_20		15
#if 0
	naitlb_20	16
#else
	def             16
#endif
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

#ifndef CONFIG_64BIT

	.export fault_vector_11
	
	.align 2048

fault_vector_11:
	/* First vector is invalid (0) */
	.ascii	"cows can fly"
	.byte 0
	.align 32

	hpmc		 1
	def		 2
	def		 3
	extint		 4
	def		 5
	itlb_11		 6
	def		 7
	def		 8
	def              9
	def		10
	def		11
	def		12
	def		13
	def		14
	dtlb_11		15
#if 0
	naitlb_11	16
#else
	def             16
#endif
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

#endif

	.import		handle_interruption,code
	.import		do_cpu_irq_mask,code

	/*
	 * r26 = function to be called
	 * r25 = argument to pass in
	 * r24 = flags for do_fork()
	 *
	 * Kernel threads don't ever return, so they don't need
	 * a true register context. We just save away the arguments
	 * for copy_thread/ret_ to properly set up the child.
	 */

#define CLONE_VM 0x100	/* Must agree with <linux/sched.h> */
#define CLONE_UNTRACED 0x00800000

	.export __kernel_thread, code
	.import do_fork
__kernel_thread:
	STREG	%r2, -RP_OFFSET(%r30)

	copy	%r30, %r1
	ldo	PT_SZ_ALGN(%r30),%r30
#ifdef CONFIG_64BIT
	/* Yo, function pointers in wide mode are little structs... -PB */
	ldd	24(%r26), %r2
	STREG	%r2, PT_GR27(%r1)	/* Store childs %dp */
	ldd	16(%r26), %r26

	STREG	%r22, PT_GR22(%r1)	/* save r22 (arg5) */
	copy	%r0, %r22		/* user_tid */
#endif
	STREG	%r26, PT_GR26(%r1)  /* Store function & argument for child */
	STREG	%r25, PT_GR25(%r1)
	ldil	L%CLONE_UNTRACED, %r26
	ldo	CLONE_VM(%r26), %r26   /* Force CLONE_VM since only init_mm */
	or	%r26, %r24, %r26      /* will have kernel mappings.	 */
	ldi	1, %r25			/* stack_start, signals kernel thread */
	stw	%r0, -52(%r30)	     	/* user_tid */
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29		/* Reference param save area */
#endif
	BL	do_fork, %r2
	copy	%r1, %r24		/* pt_regs */

	/* Parent Returns here */

	LDREG	-PT_SZ_ALGN-RP_OFFSET(%r30), %r2
	ldo	-PT_SZ_ALGN(%r30), %r30
	bv	%r0(%r2)
	nop

	/*
	 * Child Returns here
	 *
	 * copy_thread moved args from temp save area set up above
	 * into task save area.
	 */

	.export	ret_from_kernel_thread
ret_from_kernel_thread:

	/* Call schedule_tail first though */
	BL	schedule_tail, %r2
	nop

	LDREG	TI_TASK-THREAD_SZ_ALGN(%r30), %r1
	LDREG	TASK_PT_GR25(%r1), %r26
#ifdef CONFIG_64BIT
	LDREG	TASK_PT_GR27(%r1), %r27
	LDREG	TASK_PT_GR22(%r1), %r22
#endif
	LDREG	TASK_PT_GR26(%r1), %r1
	ble	0(%sr7, %r1)
	copy	%r31, %r2

#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29		/* Reference param save area */
	loadgp				/* Thread could have been in a module */
#endif
#ifndef CONFIG_64BIT
	b	sys_exit
#else
	load32	sys_exit, %r1
	bv	%r0(%r1)
#endif
	ldi	0, %r26

	.import	sys_execve, code
	.export	__execve, code
__execve:
	copy	%r2, %r15
	copy	%r30, %r16
	ldo	PT_SZ_ALGN(%r30), %r30
	STREG	%r26, PT_GR26(%r16)
	STREG	%r25, PT_GR25(%r16)
	STREG	%r24, PT_GR24(%r16)
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29		/* Reference param save area */
#endif
	BL	sys_execve, %r2
	copy	%r16, %r26

	cmpib,=,n 0,%r28,intr_return    /* forward */

	/* yes, this will trap and die. */
	copy	%r15, %r2
	copy	%r16, %r30
	bv	%r0(%r2)
	nop

	.align 4

	/*
	 * struct task_struct *_switch_to(struct task_struct *prev,
	 *	struct task_struct *next)
	 *
	 * switch kernel stacks and return prev */
	.export	_switch_to, code
_switch_to:
	STREG	 %r2, -RP_OFFSET(%r30)

	callee_save_float
	callee_save

	load32	_switch_to_ret, %r2

	STREG	%r2, TASK_PT_KPC(%r26)
	LDREG	TASK_PT_KPC(%r25), %r2

	STREG	%r30, TASK_PT_KSP(%r26)
	LDREG	TASK_PT_KSP(%r25), %r30
	LDREG	TASK_THREAD_INFO(%r25), %r25
	bv	%r0(%r2)
	mtctl   %r25,%cr30

_switch_to_ret:
	mtctl	%r0, %cr0		/* Needed for single stepping */
	callee_rest
	callee_rest_float

	LDREG	-RP_OFFSET(%r30), %r2
	bv	%r0(%r2)
	copy	%r26, %r28

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

	.align 4096

	.export	syscall_exit_rfi
syscall_exit_rfi:
	mfctl   %cr30,%r16
	LDREG	TI_TASK(%r16), %r16	/* thread_info -> task_struct */
	ldo	TASK_REGS(%r16),%r16
	/* Force iaoq to userspace, as the user has had access to our current
	 * context via sigcontext. Also Filter the PSW for the same reason.
	 */
	LDREG	PT_IAOQ0(%r16),%r19
	depi	3,31,2,%r19
	STREG	%r19,PT_IAOQ0(%r16)
	LDREG	PT_IAOQ1(%r16),%r19
	depi	3,31,2,%r19
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
	 * be inconsistent if an interrupt occured while on the gateway
	 * page) Note that we may be "trashing" values the user put in
	 * them, but we don't support the the user changing them.
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

intr_return:
	/* NOTE: Need to enable interrupts incase we schedule. */
	ssm     PSW_SM_I, %r0

	/* Check for software interrupts */

	.import irq_stat,data

	load32	irq_stat,%r19
#ifdef CONFIG_SMP
	mfctl   %cr30,%r1
	ldw	TI_CPU(%r1),%r1 /* get cpu # - int */
	/* shift left ____cacheline_aligned (aka L1_CACHE_BYTES) amount
	** irq_stat[] is defined using ____cacheline_aligned.
	*/
#ifdef CONFIG_64BIT
	shld	%r1, 6, %r20
#else
	shlw	%r1, 5, %r20
#endif
	add     %r19,%r20,%r19	/* now have &irq_stat[smp_processor_id()] */
#endif /* CONFIG_SMP */

intr_check_resched:

	/* check for reschedule */
	mfctl   %cr30,%r1
	LDREG   TI_FLAGS(%r1),%r19	/* sched.h: TIF_NEED_RESCHED */
	bb,<,n	%r19,31-TIF_NEED_RESCHED,intr_do_resched /* forward */

intr_check_sig:
	/* As above */
	mfctl   %cr30,%r1
	LDREG	TI_FLAGS(%r1),%r19	/* sched.h: TIF_SIGPENDING */
	bb,<,n %r19, 31-TIF_SIGPENDING, intr_do_signal /* forward */

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
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	.import schedule,code
intr_do_resched:
	/* Only do reschedule if we are returning to user space */
	LDREG	PT_IASQ0(%r16), %r20
	CMPIB= 0,%r20,intr_restore /* backward */
	nop
	LDREG	PT_IASQ1(%r16), %r20
	CMPIB= 0,%r20,intr_restore /* backward */
	nop

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


	.import do_signal,code
intr_do_signal:
	/* 
		This check is critical to having LWS
		working. The IASQ is zero on the gateway
		page and we cannot deliver any signals until
		we get off the gateway page.

		Only do signals if we are returning to user space 
	*/
	LDREG	PT_IASQ0(%r16), %r20
	CMPIB= 0,%r20,intr_restore /* backward */
	nop
	LDREG	PT_IASQ1(%r16), %r20
	CMPIB= 0,%r20,intr_restore /* backward */
	nop

	copy	%r0, %r24			/* unsigned long in_syscall */
	copy	%r16, %r25			/* struct pt_regs *regs */
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif

	BL	do_signal,%r2
	copy	%r0, %r26			/* sigset_t *oldset = NULL */

	b	intr_check_sig
	nop

	/*
	 * External interrupts.
	 */

intr_extint:
	CMPIB=,n 0,%r16,1f
	get_stack_use_cr30
	b,n 3f

1:
#if 0  /* Interrupt Stack support not working yet! */
	mfctl	%cr31,%r1
	copy	%r30,%r17
	/* FIXME! depi below has hardcoded idea of interrupt stack size (32k)*/
#ifdef CONFIG_64BIT
	depdi	0,63,15,%r17
#else
	depi	0,31,15,%r17
#endif
	CMPB=,n	%r1,%r17,2f
	get_stack_use_cr31
	b,n 3f
#endif
2:
	get_stack_use_r30

3:
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


	/* Generic interruptions (illegal insn, unaligned, page fault, etc) */

	.export         intr_save, code /* for os_hpmc */

intr_save:
	mfsp    %sr7,%r16
	CMPIB=,n 0,%r16,1f
	get_stack_use_cr30
	b	2f
	copy    %r8,%r26

1:
	get_stack_use_r30
	copy    %r8,%r26

2:
	save_specials	%r29

	/* If this trap is a itlb miss, skip saving/adjusting isr/ior */

	/*
	 * FIXME: 1) Use a #define for the hardwired "6" below (and in
	 *           traps.c.
	 *        2) Once we start executing code above 4 Gb, we need
	 *           to adjust iasq/iaoq here in the same way we
	 *           adjust isr/ior below.
	 */

	CMPIB=,n        6,%r26,skip_save_ior


	mfctl           %cr20, %r16 /* isr */
	nop		/* serialize mfctl on PA 2.0 to avoid 4 cycle penalty */
	mfctl           %cr21, %r17 /* ior */


#ifdef CONFIG_64BIT
	/*
	 * If the interrupted code was running with W bit off (32 bit),
	 * clear the b bits (bits 0 & 1) in the ior.
	 * save_specials left ipsw value in r8 for us to test.
	 */
	extrd,u,*<>     %r8,PSW_W_BIT,1,%r0
	depdi           0,1,2,%r17

	/*
	 * FIXME: This code has hardwired assumptions about the split
	 *        between space bits and offset bits. This will change
	 *        when we allow alternate page sizes.
	 */

	/* adjust isr/ior. */

	extrd,u         %r16,63,7,%r1    /* get high bits from isr for ior */
	depd            %r1,31,7,%r17    /* deposit them into ior */
	depdi           0,63,7,%r16      /* clear them from isr */
#endif
	STREG           %r16, PT_ISR(%r29)
	STREG           %r17, PT_IOR(%r29)


skip_save_ior:
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
	va = r8		/* virtual address for which the trap occured */
	t1 = r9		/* temporary register 1 */
	pte  = r16	/* pte/phys page # */
	prot = r17	/* prot bits */
	spc  = r24	/* space for which the trap occured */
	ptp = r25	/* page directory/page table pointer */

#ifdef CONFIG_64BIT

dtlb_miss_20w:
	space_adjust	spc,va,t0
	get_pgd		spc,ptp
	space_check	spc,t0,dtlb_fault

	L3_ptep		ptp,pte,t0,va,dtlb_check_alias_20w

	update_ptep	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot
	
	idtlbt          pte,prot

	rfir
	nop

dtlb_check_alias_20w:
	do_alias	spc,t0,t1,va,pte,prot,dtlb_fault

	idtlbt          pte,prot

	rfir
	nop

nadtlb_miss_20w:
	space_adjust	spc,va,t0
	get_pgd		spc,ptp
	space_check	spc,t0,nadtlb_fault

	L3_ptep		ptp,pte,t0,va,nadtlb_check_flush_20w

	update_ptep	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot

	idtlbt          pte,prot

	rfir
	nop

nadtlb_check_flush_20w:
	bb,>=,n          pte,_PAGE_FLUSH_BIT,nadtlb_emulate

	/* Insert a "flush only" translation */

	depdi,z         7,7,3,prot
	depdi           1,10,1,prot

	/* Get rid of prot bits and convert to page addr for idtlbt */

	depdi		0,63,12,pte
	extrd,u         pte,56,52,pte
	idtlbt          pte,prot

	rfir
	nop

#else

dtlb_miss_11:
	get_pgd		spc,ptp

	space_check	spc,t0,dtlb_fault

	L2_ptep		ptp,pte,t0,va,dtlb_check_alias_11

	update_ptep	ptp,pte,t0,t1

	make_insert_tlb_11	spc,pte,prot

	mfsp		%sr1,t0  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	idtlba		pte,(%sr1,va)
	idtlbp		prot,(%sr1,va)

	mtsp		t0, %sr1	/* Restore sr1 */

	rfir
	nop

dtlb_check_alias_11:

	/* Check to see if fault is in the temporary alias region */

	cmpib,<>,n      0,spc,dtlb_fault /* forward */
	ldil            L%(TMPALIAS_MAP_START),t0
	copy            va,t1
	depwi           0,31,23,t1
	cmpb,<>,n       t0,t1,dtlb_fault /* forward */
	ldi             (_PAGE_DIRTY|_PAGE_WRITE|_PAGE_READ),prot
	depw,z          prot,8,7,prot

	/*
	 * OK, it is in the temp alias region, check whether "from" or "to".
	 * Check "subtle" note in pacache.S re: r23/r26.
	 */

	extrw,u,=       va,9,1,r0
	or,tr           %r23,%r0,pte    /* If "from" use "from" page */
	or              %r26,%r0,pte    /* else "to", use "to" page  */

	idtlba          pte,(va)
	idtlbp          prot,(va)

	rfir
	nop

nadtlb_miss_11:
	get_pgd		spc,ptp

	space_check	spc,t0,nadtlb_fault

	L2_ptep		ptp,pte,t0,va,nadtlb_check_flush_11

	update_ptep	ptp,pte,t0,t1

	make_insert_tlb_11	spc,pte,prot


	mfsp		%sr1,t0  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	idtlba		pte,(%sr1,va)
	idtlbp		prot,(%sr1,va)

	mtsp		t0, %sr1	/* Restore sr1 */

	rfir
	nop

nadtlb_check_flush_11:
	bb,>=,n          pte,_PAGE_FLUSH_BIT,nadtlb_emulate

	/* Insert a "flush only" translation */

	zdepi           7,7,3,prot
	depi            1,10,1,prot

	/* Get rid of prot bits and convert to page addr for idtlba */

	depi		0,31,12,pte
	extru		pte,24,25,pte

	mfsp		%sr1,t0  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	idtlba		pte,(%sr1,va)
	idtlbp		prot,(%sr1,va)

	mtsp		t0, %sr1	/* Restore sr1 */

	rfir
	nop

dtlb_miss_20:
	space_adjust	spc,va,t0
	get_pgd		spc,ptp
	space_check	spc,t0,dtlb_fault

	L2_ptep		ptp,pte,t0,va,dtlb_check_alias_20

	update_ptep	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot

	f_extend	pte,t0

	idtlbt          pte,prot

	rfir
	nop

dtlb_check_alias_20:
	do_alias	spc,t0,t1,va,pte,prot,dtlb_fault
	
	idtlbt          pte,prot

	rfir
	nop

nadtlb_miss_20:
	get_pgd		spc,ptp

	space_check	spc,t0,nadtlb_fault

	L2_ptep		ptp,pte,t0,va,nadtlb_check_flush_20

	update_ptep	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot

	f_extend	pte,t0
	
        idtlbt          pte,prot

	rfir
	nop

nadtlb_check_flush_20:
	bb,>=,n          pte,_PAGE_FLUSH_BIT,nadtlb_emulate

	/* Insert a "flush only" translation */

	depdi,z         7,7,3,prot
	depdi           1,10,1,prot

	/* Get rid of prot bits and convert to page addr for idtlbt */

	depdi		0,63,12,pte
	extrd,u         pte,56,32,pte
	idtlbt          pte,prot

	rfir
	nop
#endif

nadtlb_emulate:

	/*
	 * Non access misses can be caused by fdc,fic,pdc,lpa,probe and
	 * probei instructions. We don't want to fault for these
	 * instructions (not only does it not make sense, it can cause
	 * deadlocks, since some flushes are done with the mmap
	 * semaphore held). If the translation doesn't exist, we can't
	 * insert a translation, so have to emulate the side effects
	 * of the instruction. Since we don't insert a translation
	 * we can get a lot of faults during a flush loop, so it makes
	 * sense to try to do it here with minimum overhead. We only
	 * emulate fdc,fic,pdc,probew,prober instructions whose base 
	 * and index registers are not shadowed. We defer everything 
	 * else to the "slow" path.
	 */

	mfctl           %cr19,%r9 /* Get iir */

	/* PA 2.0 Arch Ref. Book pg 382 has a good description of the insn bits.
	   Checks for fdc,fdce,pdc,"fic,4f",prober,probeir,probew, probeiw */

	/* Checks for fdc,fdce,pdc,"fic,4f" only */
	ldi             0x280,%r16
	and             %r9,%r16,%r17
	cmpb,<>,n       %r16,%r17,nadtlb_probe_check
	bb,>=,n         %r9,26,nadtlb_nullify  /* m bit not set, just nullify */
	BL		get_register,%r25
	extrw,u         %r9,15,5,%r8           /* Get index register # */
	CMPIB=,n        -1,%r1,nadtlb_fault    /* have to use slow path */
	copy            %r1,%r24
	BL		get_register,%r25
	extrw,u         %r9,10,5,%r8           /* Get base register # */
	CMPIB=,n        -1,%r1,nadtlb_fault    /* have to use slow path */
	BL		set_register,%r25
	add,l           %r1,%r24,%r1           /* doesn't affect c/b bits */

nadtlb_nullify:
	mfctl           %ipsw,%r8
	ldil            L%PSW_N,%r9
	or              %r8,%r9,%r8            /* Set PSW_N */
	mtctl           %r8,%ipsw

	rfir
	nop

	/* 
		When there is no translation for the probe address then we
		must nullify the insn and return zero in the target regsiter.
		This will indicate to the calling code that it does not have 
		write/read privileges to this address.

		This should technically work for prober and probew in PA 1.1,
		and also probe,r and probe,w in PA 2.0

		WARNING: USE ONLY NON-SHADOW REGISTERS WITH PROBE INSN!
		THE SLOW-PATH EMULATION HAS NOT BEEN WRITTEN YET.

	*/
nadtlb_probe_check:
	ldi             0x80,%r16
	and             %r9,%r16,%r17
	cmpb,<>,n       %r16,%r17,nadtlb_fault /* Must be probe,[rw]*/
	BL              get_register,%r25      /* Find the target register */
	extrw,u         %r9,31,5,%r8           /* Get target register */
	CMPIB=,n        -1,%r1,nadtlb_fault    /* have to use slow path */
	BL		set_register,%r25
	copy            %r0,%r1                /* Write zero to target register */
	b nadtlb_nullify                       /* Nullify return insn */
	nop


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

	update_ptep	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot
	
	iitlbt          pte,prot

	rfir
	nop

#else

itlb_miss_11:
	get_pgd		spc,ptp

	space_check	spc,t0,itlb_fault

	L2_ptep		ptp,pte,t0,va,itlb_fault

	update_ptep	ptp,pte,t0,t1

	make_insert_tlb_11	spc,pte,prot

	mfsp		%sr1,t0  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	iitlba		pte,(%sr1,va)
	iitlbp		prot,(%sr1,va)

	mtsp		t0, %sr1	/* Restore sr1 */

	rfir
	nop

itlb_miss_20:
	get_pgd		spc,ptp

	space_check	spc,t0,itlb_fault

	L2_ptep		ptp,pte,t0,va,itlb_fault

	update_ptep	ptp,pte,t0,t1

	make_insert_tlb	spc,pte,prot

	f_extend	pte,t0	

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

#ifdef CONFIG_SMP
	CMPIB=,n        0,spc,dbit_nolock_20w
	load32		PA(pa_dbit_lock),t0

dbit_spin_20w:
	ldcw            0(t0),t1
	cmpib,=         0,t1,dbit_spin_20w
	nop

dbit_nolock_20w:
#endif
	update_dirty	ptp,pte,t1

	make_insert_tlb	spc,pte,prot
		
	idtlbt          pte,prot
#ifdef CONFIG_SMP
	CMPIB=,n        0,spc,dbit_nounlock_20w
	ldi             1,t1
	stw             t1,0(t0)

dbit_nounlock_20w:
#endif

	rfir
	nop
#else

dbit_trap_11:

	get_pgd		spc,ptp

	space_check	spc,t0,dbit_fault

	L2_ptep		ptp,pte,t0,va,dbit_fault

#ifdef CONFIG_SMP
	CMPIB=,n        0,spc,dbit_nolock_11
	load32		PA(pa_dbit_lock),t0

dbit_spin_11:
	ldcw            0(t0),t1
	cmpib,=         0,t1,dbit_spin_11
	nop

dbit_nolock_11:
#endif
	update_dirty	ptp,pte,t1

	make_insert_tlb_11	spc,pte,prot

	mfsp            %sr1,t1  /* Save sr1 so we can use it in tlb inserts */
	mtsp		spc,%sr1

	idtlba		pte,(%sr1,va)
	idtlbp		prot,(%sr1,va)

	mtsp            t1, %sr1     /* Restore sr1 */
#ifdef CONFIG_SMP
	CMPIB=,n        0,spc,dbit_nounlock_11
	ldi             1,t1
	stw             t1,0(t0)

dbit_nounlock_11:
#endif

	rfir
	nop

dbit_trap_20:
	get_pgd		spc,ptp

	space_check	spc,t0,dbit_fault

	L2_ptep		ptp,pte,t0,va,dbit_fault

#ifdef CONFIG_SMP
	CMPIB=,n        0,spc,dbit_nolock_20
	load32		PA(pa_dbit_lock),t0

dbit_spin_20:
	ldcw            0(t0),t1
	cmpib,=         0,t1,dbit_spin_20
	nop

dbit_nolock_20:
#endif
	update_dirty	ptp,pte,t1

	make_insert_tlb	spc,pte,prot

	f_extend	pte,t1
	
        idtlbt          pte,prot

#ifdef CONFIG_SMP
	CMPIB=,n        0,spc,dbit_nounlock_20
	ldi             1,t1
	stw             t1,0(t0)

dbit_nounlock_20:
#endif

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
	ldi             6,%r8

nadtlb_fault:
	b               intr_save
	ldi             17,%r8

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

	.export sys_fork_wrapper
	.export child_return
sys_fork_wrapper:
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30), %r1
	ldo	TASK_REGS(%r1),%r1
	reg_save %r1
	mfctl	%cr27, %r3
	STREG	%r3, PT_CR27(%r1)

	STREG	%r2,-RP_OFFSET(%r30)
	ldo	FRAME_SIZE(%r30),%r30
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29		/* Reference param save area */
#endif

	/* These are call-clobbered registers and therefore
	   also syscall-clobbered (we hope). */
	STREG	%r2,PT_GR19(%r1)	/* save for child */
	STREG	%r30,PT_GR21(%r1)

	LDREG	PT_GR30(%r1),%r25
	copy	%r1,%r24
	BL	sys_clone,%r2
	ldi	SIGCHLD,%r26

	LDREG	-RP_OFFSET-FRAME_SIZE(%r30),%r2
wrapper_exit:
	ldo	-FRAME_SIZE(%r30),%r30		/* get the stackframe */
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1
	ldo	TASK_REGS(%r1),%r1	 /* get pt regs */

	LDREG	PT_CR27(%r1), %r3
	mtctl	%r3, %cr27
	reg_restore %r1

	/* strace expects syscall # to be preserved in r20 */
	ldi	__NR_fork,%r20
	bv %r0(%r2)
	STREG	%r20,PT_GR20(%r1)

	/* Set the return value for the child */
child_return:
	BL	schedule_tail, %r2
	nop

	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE-FRAME_SIZE(%r30), %r1
	LDREG	TASK_PT_GR19(%r1),%r2
	b	wrapper_exit
	copy	%r0,%r28

	
	.export sys_clone_wrapper
sys_clone_wrapper:
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1
	ldo	TASK_REGS(%r1),%r1	/* get pt regs */
	reg_save %r1
	mfctl	%cr27, %r3
	STREG	%r3, PT_CR27(%r1)

	STREG	%r2,-RP_OFFSET(%r30)
	ldo	FRAME_SIZE(%r30),%r30
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29		/* Reference param save area */
#endif

	STREG	%r2,PT_GR19(%r1)	/* save for child */
	STREG	%r30,PT_GR21(%r1)
	BL	sys_clone,%r2
	copy	%r1,%r24

	b	wrapper_exit
	LDREG	-RP_OFFSET-FRAME_SIZE(%r30),%r2

	.export sys_vfork_wrapper
sys_vfork_wrapper:
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1
	ldo	TASK_REGS(%r1),%r1	/* get pt regs */
	reg_save %r1
	mfctl	%cr27, %r3
	STREG	%r3, PT_CR27(%r1)

	STREG	%r2,-RP_OFFSET(%r30)
	ldo	FRAME_SIZE(%r30),%r30
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29		/* Reference param save area */
#endif

	STREG	%r2,PT_GR19(%r1)	/* save for child */
	STREG	%r30,PT_GR21(%r1)

	BL	sys_vfork,%r2
	copy	%r1,%r26

	b	wrapper_exit
	LDREG	-RP_OFFSET-FRAME_SIZE(%r30),%r2

	
	.macro  execve_wrapper execve
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1
	ldo	TASK_REGS(%r1),%r1	/* get pt regs */

	/*
	 * Do we need to save/restore r3-r18 here?
	 * I don't think so. why would new thread need old
	 * threads registers?
	 */

	/* %arg0 - %arg3 are already saved for us. */

	STREG %r2,-RP_OFFSET(%r30)
	ldo FRAME_SIZE(%r30),%r30
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29		/* Reference param save area */
#endif
	BL \execve,%r2
	copy %r1,%arg0

	ldo -FRAME_SIZE(%r30),%r30
	LDREG -RP_OFFSET(%r30),%r2

	/* If exec succeeded we need to load the args */

	ldo -1024(%r0),%r1
	cmpb,>>= %r28,%r1,error_\execve
	copy %r2,%r19

error_\execve:
	bv %r0(%r19)
	nop
	.endm

	.export sys_execve_wrapper
	.import sys_execve

sys_execve_wrapper:
	execve_wrapper sys_execve

#ifdef CONFIG_64BIT
	.export sys32_execve_wrapper
	.import sys32_execve

sys32_execve_wrapper:
	execve_wrapper sys32_execve
#endif

	.export sys_rt_sigreturn_wrapper
sys_rt_sigreturn_wrapper:
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r26
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
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1
	ldo	TASK_REGS(%r1),%r1	/* get pt regs */
	reg_restore %r1

	/* If the signal was received while the process was blocked on a
	 * syscall, then r2 will take us to syscall_exit; otherwise r2 will
	 * take us to syscall_exit_rfi and on to intr_return.
	 */
	bv	%r0(%r2)
	LDREG	PT_GR28(%r1),%r28  /* reload original r28 for syscall_exit */

	.export sys_sigaltstack_wrapper
sys_sigaltstack_wrapper:
	/* Get the user stack pointer */
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1
	ldo	TASK_REGS(%r1),%r24	/* get pt regs */
	LDREG	TASK_PT_GR30(%r24),%r24
	STREG	%r2, -RP_OFFSET(%r30)
#ifdef CONFIG_64BIT
	ldo	FRAME_SIZE(%r30), %r30
	b,l	do_sigaltstack,%r2
	ldo	-16(%r30),%r29		/* Reference param save area */
#else
	bl	do_sigaltstack,%r2
	ldo	FRAME_SIZE(%r30), %r30
#endif

	ldo	-FRAME_SIZE(%r30), %r30
	LDREG	-RP_OFFSET(%r30), %r2
	bv	%r0(%r2)
	nop

#ifdef CONFIG_64BIT
	.export sys32_sigaltstack_wrapper
sys32_sigaltstack_wrapper:
	/* Get the user stack pointer */
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r24
	LDREG	TASK_PT_GR30(%r24),%r24
	STREG	%r2, -RP_OFFSET(%r30)
	ldo	FRAME_SIZE(%r30), %r30
	b,l	do_sigaltstack32,%r2
	ldo	-16(%r30),%r29		/* Reference param save area */

	ldo	-FRAME_SIZE(%r30), %r30
	LDREG	-RP_OFFSET(%r30), %r2
	bv	%r0(%r2)
	nop
#endif

	.export sys_rt_sigsuspend_wrapper
sys_rt_sigsuspend_wrapper:
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30), %r1
	ldo	TASK_REGS(%r1),%r24
	reg_save %r24

	STREG	%r2, -RP_OFFSET(%r30)
#ifdef CONFIG_64BIT
	ldo	FRAME_SIZE(%r30), %r30
	b,l	sys_rt_sigsuspend,%r2
	ldo	-16(%r30),%r29		/* Reference param save area */
#else
	bl	sys_rt_sigsuspend,%r2
	ldo	FRAME_SIZE(%r30), %r30
#endif

	ldo	-FRAME_SIZE(%r30), %r30
	LDREG	-RP_OFFSET(%r30), %r2

	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30), %r1
	ldo	TASK_REGS(%r1),%r1
	reg_restore %r1

	bv	%r0(%r2)
	nop

	.export syscall_exit
syscall_exit:

	/* NOTE: HP-UX syscalls also come through here
	 * after hpux_syscall_exit fixes up return
	 * values. */

	/* NOTE: Not all syscalls exit this way.  rt_sigreturn will exit
	 * via syscall_exit_rfi if the signal was received while the process
	 * was running.
	 */

	/* save return value now */

	mfctl     %cr30, %r1
	LDREG     TI_TASK(%r1),%r1
	STREG     %r28,TASK_PT_GR28(%r1)

#ifdef CONFIG_HPUX

/* <linux/personality.h> cannot be easily included */
#define PER_HPUX 0x10
	LDREG     TASK_PERSONALITY(%r1),%r19

	/* We can't use "CMPIB<> PER_HPUX" since "im5" field is sign extended */
	ldo	  -PER_HPUX(%r19), %r19
	CMPIB<>,n 0,%r19,1f

	/* Save other hpux returns if personality is PER_HPUX */
	STREG     %r22,TASK_PT_GR22(%r1)
	STREG     %r29,TASK_PT_GR29(%r1)
1:

#endif /* CONFIG_HPUX */

	/* Seems to me that dp could be wrong here, if the syscall involved
	 * calling a module, and nothing got round to restoring dp on return.
	 */
	loadgp

syscall_check_bh:

	/* Check for software interrupts */

	.import irq_stat,data

	load32	irq_stat,%r19

#ifdef CONFIG_SMP
	/* sched.h: int processor */
	/* %r26 is used as scratch register to index into irq_stat[] */
	ldw     TI_CPU-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r26 /* cpu # */

	/* shift left ____cacheline_aligned (aka L1_CACHE_BYTES) bits */
#ifdef CONFIG_64BIT
	shld	%r26, 6, %r20
#else
	shlw	%r26, 5, %r20
#endif
	add     %r19,%r20,%r19	/* now have &irq_stat[smp_processor_id()] */
#endif /* CONFIG_SMP */

syscall_check_resched:

	/* check for reschedule */

	LDREG	TI_FLAGS-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r19	/* long */
	bb,<,n	%r19, 31-TIF_NEED_RESCHED, syscall_do_resched /* forward */

syscall_check_sig:
	LDREG	TI_FLAGS-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r19    /* get ti flags */
	bb,<,n	%r19, 31-TIF_SIGPENDING, syscall_do_signal /* forward */

syscall_restore:
	/* Are we being ptraced? */
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1

	LDREG	TASK_PTRACE(%r1), %r19
	bb,<	%r19,31,syscall_restore_rfi
	nop

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
	rsm     PSW_SM_I, %r0
	LDREG   TASK_PT_GR30(%r1),%r30             /* restore user sp */
	mfsp	%sr3,%r1			   /* Get users space id */
	mtsp    %r1,%sr7                           /* Restore sr7 */
	ssm     PSW_SM_I, %r0

	/* Set sr2 to zero for userspace syscalls to work. */
	mtsp	%r0,%sr2 
	mtsp	%r1,%sr4			   /* Restore sr4 */
	mtsp	%r1,%sr5			   /* Restore sr5 */
	mtsp	%r1,%sr6			   /* Restore sr6 */

	depi	3,31,2,%r31			   /* ensure return to user mode. */

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

	/* The values of PA_SINGLESTEP_BIT and PA_BLOCKSTEP_BIT are
	 * set in include/linux/ptrace.h and converted to PA bitmap
	 * numbers in asm-offsets.c */

	/* if ((%r19.PA_SINGLESTEP_BIT)) { %r20.27=1} */
	extru,=	%r19,PA_SINGLESTEP_BIT,1,%r0
	depi	-1,27,1,%r20			   /* R bit */

	/* if ((%r19.PA_BLOCKSTEP_BIT)) { %r20.7=1} */
	extru,= %r19,PA_BLOCKSTEP_BIT,1,%r0
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

pt_regs_ok:
	LDREG	TASK_PT_GR31(%r1),%r2
	depi	3,31,2,%r2			   /* ensure return to user mode. */
	STREG	%r2,TASK_PT_IAOQ0(%r1)
	ldo	4(%r2),%r2
	STREG	%r2,TASK_PT_IAOQ1(%r1)
	copy	%r25,%r16
	b	intr_restore
	nop

	.import schedule,code
syscall_do_resched:
	BL	schedule,%r2
#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29		/* Reference param save area */
#else
	nop
#endif
	b       syscall_check_bh  /* if resched, we start over again */
	nop

	.import do_signal,code
syscall_do_signal:
	/* Save callee-save registers (for sigcontext).
	   FIXME: After this point the process structure should be
	   consistent with all the relevant state of the process
	   before the syscall.  We need to verify this. */
	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1 
	ldo	TASK_REGS(%r1), %r25		/* struct pt_regs *regs */
	reg_save %r25

	ldi	1, %r24				/* unsigned long in_syscall */

#ifdef CONFIG_64BIT
	ldo	-16(%r30),%r29			/* Reference param save area */
#endif
	BL	do_signal,%r2
	copy	%r0, %r26			/* sigset_t *oldset = NULL */

	LDREG	TI_TASK-THREAD_SZ_ALGN-FRAME_SIZE(%r30),%r1
	ldo	TASK_REGS(%r1), %r20		/* reload pt_regs */
	reg_restore %r20

	b,n     syscall_check_sig

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

get_register:
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

	/*
	 * set_register is used by the non access tlb miss handlers to
	 * copy the value of r1 into the general register specified in
	 * r8.
	 */

set_register:
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
