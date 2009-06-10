/* Boot entry point for MN10300 kernel
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/threads.h>
#include <linux/linkage.h>
#include <linux/serial_reg.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/frame.inc>
#include <asm/param.h>
#include <unit/serial.h>

	__HEAD

###############################################################################
#
# bootloader entry point
#
###############################################################################
	.globl	_start
	.type	_start,@function
_start:
	# save commandline pointer
	mov	d0,a3

	# preload the PGD pointer register
	mov	swapper_pg_dir,d0
	mov	d0,(PTBR)

	# turn on the TLBs
	mov	MMUCTR_IIV|MMUCTR_DIV,d0
	mov	d0,(MMUCTR)
	mov	MMUCTR_ITE|MMUCTR_DTE|MMUCTR_CE,d0
	mov	d0,(MMUCTR)

	# turn on AM33v2 exception handling mode and set the trap table base
	movhu	(CPUP),d0
	or	CPUP_EXM_AM33V2,d0
	movhu	d0,(CPUP)
	mov	CONFIG_INTERRUPT_VECTOR_BASE,d0
	mov	d0,(TBR)

	# invalidate and enable both of the caches
	mov	CHCTR,a0
	clr	d0
	movhu	d0,(a0)					# turn off first
	mov	CHCTR_ICINV|CHCTR_DCINV,d0
	movhu	d0,(a0)
	setlb
	mov	(a0),d0
	btst	CHCTR_ICBUSY|CHCTR_DCBUSY,d0		# wait till not busy
	lne

#ifndef CONFIG_MN10300_CACHE_DISABLED
#ifdef CONFIG_MN10300_CACHE_WBACK
#ifndef CONFIG_MN10300_CACHE_WBACK_NOWRALLOC
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRBACK,d0
#else
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRBACK|CHCTR_DCALMD,d0
#endif /* CACHE_DISABLED */
#else
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRTHROUGH,d0
#endif /* WBACK */
	movhu	d0,(a0)					# enable
#endif /* NOWRALLOC */

	# turn on RTS on the debug serial port if applicable
#ifdef CONFIG_MN10300_UNIT_ASB2305
	bset	UART_MCR_RTS,(ASB2305_DEBUG_MCR)
#endif

	# clear the BSS area
	mov	__bss_start,a0
	mov	__bss_stop,a1
	clr	d0
bssclear:
	cmp	a1,a0
	bge	bssclear_end
	mov	d0,(a0)
	inc4	a0
	bra	bssclear
bssclear_end:

	# retrieve the parameters (including command line) before we overwrite
	# them
	cmp	0xabadcafe,d1
	bne	__no_parameters

__copy_parameters:
	mov	redboot_command_line,a0
	mov	a0,a1
	add	COMMAND_LINE_SIZE,a1
1:
	movbu	(a3),d0
	inc	a3
	movbu	d0,(a0)
	inc	a0
	cmp	a1,a0
	blt	1b

	mov	redboot_platform_name,a0
	mov	a0,a1
	add	COMMAND_LINE_SIZE,a1
	mov	d2,a3
1:
	movbu	(a3),d0
	inc	a3
	movbu	d0,(a0)
	inc	a0
	cmp	a1,a0
	blt	1b

__no_parameters:

	# set up the registers with recognisable rubbish in them
	mov	init_thread_union+THREAD_SIZE-12,sp

	mov	0xea01eaea,d0
	mov	d0,(4,sp)		# EPSW save area
	mov	0xea02eaea,d0
	mov	d0,(8,sp)		# PC save area

	mov	0xeb0060ed,d0
	mov	d0,mdr
	mov	0xeb0061ed,d0
	mov	d0,mdrq
	mov	0xeb0062ed,d0
	mov	d0,mcrh
	mov	0xeb0063ed,d0
	mov	d0,mcrl
	mov	0xeb0064ed,d0
	mov	d0,mcvf
	mov	0xed0065ed,a3
	mov	a3,usp

	mov	0xed00e0ed,e0
	mov	0xed00e1ed,e1
	mov	0xed00e2ed,e2
	mov	0xed00e3ed,e3
	mov	0xed00e4ed,e4
	mov	0xed00e5ed,e5
	mov	0xed00e6ed,e6
	mov	0xed00e7ed,e7

	mov	0xed00d0ed,d0
	mov	0xed00d1ed,d1
	mov	0xed00d2ed,d2
	mov	0xed00d3ed,d3
	mov	0xed00a0ed,a0
	mov	0xed00a1ed,a1
	mov	0xed00a2ed,a2
	mov	0,a3

	# set up the initial kernel stack
	SAVE_ALL
	mov	0xffffffff,d0
	mov	d0,(REG_ORIG_D0,fp)

	# put different recognisable rubbish in the regs
	mov	0xfb0060ed,d0
	mov	d0,mdr
	mov	0xfb0061ed,d0
	mov	d0,mdrq
	mov	0xfb0062ed,d0
	mov	d0,mcrh
	mov	0xfb0063ed,d0
	mov	d0,mcrl
	mov	0xfb0064ed,d0
	mov	d0,mcvf
	mov	0xfd0065ed,a0
	mov	a0,usp

	mov	0xfd00e0ed,e0
	mov	0xfd00e1ed,e1
	mov	0xfd00e2ed,e2
	mov	0xfd00e3ed,e3
	mov	0xfd00e4ed,e4
	mov	0xfd00e5ed,e5
	mov	0xfd00e6ed,e6
	mov	0xfd00e7ed,e7

	mov	0xfd00d0ed,d0
	mov	0xfd00d1ed,d1
	mov	0xfd00d2ed,d2
	mov	0xfd00d3ed,d3
	mov	0xfd00a0ed,a0
	mov	0xfd00a1ed,a1
	mov	0xfd00a2ed,a2

	# we may be holding current in E2
#ifdef CONFIG_MN10300_CURRENT_IN_E2
	mov	init_task,e2
#endif

	# initialise the processor and the unit
	call	processor_init[],0
	call	unit_init[],0

#ifdef CONFIG_GDBSTUB
	call	gdbstub_init[],0

#ifdef CONFIG_GDBSTUB_IMMEDIATE
	.globl	__gdbstub_pause
__gdbstub_pause:
	bra	__gdbstub_pause
#endif
#endif

	jmp	start_kernel
	.size	_start, _start-.
ENTRY(__head_end)

/*
 * This is initialized to disallow all access to the low 2G region
 * - the high 2G region is managed directly by the MMU
 * - range 0x70000000-0x7C000000 are initialised for use by VMALLOC
 */
	.section .bss
	.balign PAGE_SIZE
ENTRY(swapper_pg_dir)
        .space PTRS_PER_PGD*4

/*
 * The page tables are initialized to only 8MB here - the final page
 * tables are set up later depending on memory size.
 */

	.balign PAGE_SIZE
ENTRY(empty_zero_page)
	.space PAGE_SIZE

	.balign PAGE_SIZE
ENTRY(empty_bad_page)
	.space PAGE_SIZE

	.balign PAGE_SIZE
ENTRY(empty_bad_pte_table)
	.space PAGE_SIZE

	.balign PAGE_SIZE
ENTRY(large_page_table)
	.space PAGE_SIZE

	.balign PAGE_SIZE
ENTRY(kernel_vmalloc_ptes)
	.space ((VMALLOC_END-VMALLOC_START)/PAGE_SIZE)*4
