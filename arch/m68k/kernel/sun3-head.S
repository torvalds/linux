/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/entry.h>
#include <asm/page.h>
#include <asm/contregs.h>
#include <asm/sun3-head.h>

PSL_HIGHIPL     = 0x2700
NBSG            = 0x20000
ICACHE_ONLY	= 0x00000009
CACHES_OFF	= 0x00000008	| actually a clear and disable --m
#define MAS_STACK INT_STACK
ROOT_TABLE_SIZE = 128
PAGESIZE	= 8192
SUN3_INVALID_PMEG = 255
.globl bootup_user_stack
.globl bootup_kernel_stack
.globl pg0
.globl swapper_pg_dir
.globl kernel_pmd_table
.globl availmem
.global m68k_pgtable_cachemode
.global kpt
| todo: all these should be in bss!
swapper_pg_dir:                .skip 0x2000
pg0:                           .skip 0x2000
kernel_pmd_table:              .skip 0x2000

.globl kernel_pg_dir
.equ    kernel_pg_dir,kernel_pmd_table

	__HEAD
ENTRY(_stext)
ENTRY(_start)

/* Firstly, disable interrupts and set up function codes. */
	movew	#PSL_HIGHIPL, %sr
	moveq	#FC_CONTROL, %d0
	movec	%d0, %sfc
	movec	%d0, %dfc

/* Make sure we're in context zero. */
	moveq	#0, %d0
	movsb	%d0, AC_CONTEXT

/* map everything the bootloader left us into high memory, clean up the
   excess later */
	lea	(AC_SEGMAP+0),%a0
	lea	(AC_SEGMAP+KERNBASE),%a1
1:
	movsb	%a0@, %d1
	movsb	%d1, %a1@
	cmpib	#SUN3_INVALID_PMEG, %d1
	beq	2f
	addl	#NBSG,%a0
	addl	#NBSG,%a1
	jmp	1b

2:

/* Disable caches and jump to high code. */
	moveq	#ICACHE_ONLY,%d0	| Cache disabled until we're ready to enable it
	movc	%d0, %cacr	|   is this the right value? (yes --m)
	jmp	1f:l

/* Following code executes at high addresses (0xE000xxx). */
1:	lea	init_task,%curptr			| get initial thread...
	lea	init_thread_union+THREAD_SIZE,%sp	| ...and its stack.

/* Point MSP at an invalid page to trap if it's used. --m */
	movl	#(PAGESIZE),%d0
	movc	%d0,%msp
	moveq	#-1,%d0
	movsb	%d0,(AC_SEGMAP+0x0)

	jbsr	sun3_init

	jbsr	base_trap_init

        jbsr    start_kernel
	trap	#15

        .data
        .even
kpt:
        .long 0
availmem:
        .long 0
