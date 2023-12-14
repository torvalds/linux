/* -*- mode: asm -*-
 *
 * head-atarinommu.S -- This file contains the initial boot code for the
 *	     		Linux/68k kernel.
 */

// FIXME(mreis): cleanup, merge or some other solution

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/bootinfo.h>
#include <asm/bootinfo-atari.h>
#include <asm/setup.h>
#include <asm/entry.h>
#include <asm/asm-offsets.h>

#define DEBUG

.globl kernel_pg_dir
.globl availmem
.globl _ramstart

/* Miscellaneous definitions */
PAGESIZE	= 4096
PAGESHIFT	= 12

#ifdef DEBUG
/* When debugging use readable names for labels */
#ifdef __STDC__
#define L(name) .head.S.##name
#else
#define L(name) .head.S./**/name
#endif
#else
#ifdef __STDC__
#define L(name) .L##name
#else
#define L(name) .L/**/name
#endif
#endif

/* The __INITDATA stuff is a no-op when ftrace or kgdb are turned on */
#ifndef __INITDATA
#define __INITDATA	.data
#define __FINIT		.previous
#endif

/* Several macros to make the writing of subroutines easier:
 * - func_start marks the beginning of the routine which setups the frame
 *   register and saves the registers, it also defines another macro
 *   to automatically restore the registers again.
 * - func_return marks the end of the routine and simply calls the prepared
 *   macro to restore registers and jump back to the caller.
 * - func_define generates another macro to automatically put arguments
 *   onto the stack call the subroutine and cleanup the stack again.
 */

/* Within subroutines these macros can be used to access the arguments
 * on the stack. With STACK some allocated memory on the stack can be
 * accessed and ARG0 points to the return address (used by mmu_engage).
 */
#define STACK   %a6@(stackstart)
#define ARG0    %a6@(4)
#define ARG1    %a6@(8)
#define ARG2    %a6@(12)
#define ARG3    %a6@(16)
#define ARG4    %a6@(20)


.macro  func_start      name,saveregs,stack=0
L(\name):
        linkw   %a6,#-\stack
        moveml  \saveregs,%sp@-
.set    stackstart,-\stack

.macro  func_return_\name
        moveml  %sp@+,\saveregs
        unlk    %a6
        rts
.endm
.endm

.macro  func_return     name
        func_return_\name
.endm

.macro  func_call       name
        jbsr    L(\name)
.endm

.macro  move_stack      nr,arg1,arg2,arg3,arg4
.if     \nr
        move_stack      "(\nr-1)",\arg2,\arg3,\arg4
        movel   \arg1,%sp@-
.endif
.endm

.macro  func_define     name,nr=0
.macro  \name   arg1,arg2,arg3,arg4
        move_stack      \nr,\arg1,\arg2,\arg3,\arg4
        func_call       \name
.if     \nr
        lea     %sp@(\nr*4),%sp
.endif
.endm
.endm

.macro  get_bi_record   record
        pea     \record
        func_call       get_bi_record
        addql   #4,%sp
.endm

__INITDATA
	.align	4
	
L(cputype):
	.long	0
L(phys_kernel_start):
	.long	0
L(kernel_end):
	.long	0
L(kernel_pgdir_ptr):
	.long	0
L(temp_mmap_mem):
	.long	0
_ramstart:
	.long	0
	
__HEAD
ENTRY(_stext)
/*
 * Version numbers of the bootinfo interface
 * The area from _stext to _start will later be used as kernel pointer table
 */
	bras	1f	/* Jump over bootinfo version numbers */

	.long	BOOTINFOV_MAGIC
	.long	MACH_ATARI, ATARI_BOOTI_VERSION
	.long	0
1:	jra	__start

.equ	kernel_pg_dir,_stext

.equ	.,_stext+PAGESIZE

ENTRY(_start)
	jra	__start
__INIT
ENTRY(__start)
/*
 * Setup initial stack pointer
 */
        lea     _stext,%sp


/*
 * Record the CPU and machine type.
 */

        get_bi_record   BI_MACHTYPE
        lea     m68k_machtype,%a1
        movel   %a0@,%a1@

        get_bi_record   BI_FPUTYPE
        lea     m68k_fputype,%a1
        movel   %a0@,%a1@

        get_bi_record   BI_MMUTYPE
        lea     m68k_mmutype,%a1
        movel   %a0@,%a1@

        get_bi_record   BI_CPUTYPE
        lea     m68k_cputype,%a1
        movel   %a0@,%a1@

        get_bi_record   BI_ATARI_MCH_TYPE
        lea     atari_mch_type,%a1
	movel   %a0@,%a1@

/*
 * raise interrupt level
 */

	movew	#0x2700,%sr


/*
 * Save physical start address of kernel
 */
        lea     L(phys_kernel_start),%a0
        lea     _stext,%a1


	subl	#_stext,%a1
	addl	#PAGE_OFFSET,%a1
	movel	%a1,%a0@

/* Find the start of free memory, get_bi_record does this for us,
 * as the bootinfo structure is located directly behind the kernel
 * and and we simply search for the last entry.
 */
        get_bi_record   BI_LAST
        addw    #PAGESIZE-1,%a0
        movel   %a0,%d0
        andw    #-PAGESIZE,%d0

        movel   %a0,_ramstart
        lea     L(kernel_end),%a0
	lea	_ramstart,%a0

/*
 * After this point no new memory is allocated and
 * the start of available memory is stored in availmem.
 * (The bootmem allocator requires now the physicall address.)
 */

        movel   memory_start,availmem


/*
 * Setup initial stack pointer
 */
	lea	init_thread_union+THREAD_SIZE,%sp


	subl	%a6,%a6		/* clear a6 for gdb */

/*
 * The new 64bit printf support requires an early exception initialization.
 */

	jbsr	base_trap_init 


/* jump to the kernel start */


	jbsr	start_kernel

/*
 * Find a tag record in the bootinfo structure
 * The bootinfo structure is located right after the kernel bss
 * Returns: d0: size (-1 if not found)
 *          a0: data pointer (end-of-records if not found)
 */
func_start      get_bi_record,%d1

        movel   ARG1,%d0
        lea     _end,%a0
1:      tstw    %a0@(BIR_TAG)
        jeq     3f
        cmpw    %a0@(BIR_TAG),%d0
        jeq     2f
        addw    %a0@(BIR_SIZE),%a0
        jra     1b
2:      moveq   #0,%d0
        movew   %a0@(BIR_SIZE),%d0
        lea     %a0@(BIR_DATA),%a0
        jra     4f
3:      moveq   #-1,%d0
        lea     %a0@(BIR_SIZE),%a0
4:
func_return     get_bi_record


__FINIT
	.data
	.align	4
	
availmem:
	.long	0
