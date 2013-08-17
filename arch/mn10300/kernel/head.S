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
#ifdef CONFIG_SMP
#include <asm/smp.h>
#include <asm/intctl-regs.h>
#include <asm/cpu-regs.h>
#include <proc/smp-regs.h>
#endif /* CONFIG_SMP */

	__HEAD

###############################################################################
#
# bootloader entry point
#
###############################################################################
	.globl	_start
	.type	_start,@function
_start:
#ifdef CONFIG_SMP
	#
	# If this is a secondary CPU (AP), then deal with that elsewhere
	#
	mov	(CPUID),d3
	and	CPUID_MASK,d3
	bne	startup_secondary

	#
	# We're dealing with the primary CPU (BP) here, then.
	# Keep BP's D0,D1,D2 register for boot check.
	#

	# Set up the Boot IPI for each secondary CPU
	mov	0x1,a0
loop_set_secondary_icr:
	mov	a0,a1
	asl	CROSS_ICR_CPU_SHIFT,a1
	add	CROSS_GxICR(SMP_BOOT_IRQ,0),a1
	movhu	(a1),d3
	or	GxICR_ENABLE|GxICR_LEVEL_0,d3
	movhu	d3,(a1)
	movhu	(a1),d3				# flush
	inc	a0
	cmp	NR_CPUS,a0
	bne	loop_set_secondary_icr
#endif /* CONFIG_SMP */

	# save commandline pointer
	mov	d0,a3

	# preload the PGD pointer register
	mov	swapper_pg_dir,d0
	mov	d0,(PTBR)
	clr	d0
	movbu	d0,(PIDR)

	# turn on the TLBs
	mov	MMUCTR_IIV|MMUCTR_DIV,d0
	mov	d0,(MMUCTR)
#ifdef CONFIG_AM34_2
	mov	MMUCTR_ITE|MMUCTR_DTE|MMUCTR_CE|MMUCTR_WTE,d0
#else
	mov	MMUCTR_ITE|MMUCTR_DTE|MMUCTR_CE,d0
#endif
	mov	d0,(MMUCTR)

	# turn on AM33v2 exception handling mode and set the trap table base
	movhu	(CPUP),d0
	or	CPUP_EXM_AM33V2,d0
	movhu	d0,(CPUP)
	mov	CONFIG_INTERRUPT_VECTOR_BASE,d0
	mov	d0,(TBR)

	# invalidate and enable both of the caches
#ifdef CONFIG_SMP
	mov	ECHCTR,a0
	clr	d0
	mov	d0,(a0)
#endif
	mov	CHCTR,a0
	clr	d0
	movhu	d0,(a0)					# turn off first
	mov	CHCTR_ICINV|CHCTR_DCINV,d0
	movhu	d0,(a0)
	setlb
	mov	(a0),d0
	btst	CHCTR_ICBUSY|CHCTR_DCBUSY,d0		# wait till not busy
	lne

#ifdef CONFIG_MN10300_CACHE_ENABLED
#ifdef CONFIG_MN10300_CACHE_WBACK
#ifndef CONFIG_MN10300_CACHE_WBACK_NOWRALLOC
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRBACK,d0
#else
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRBACK|CHCTR_DCALMD,d0
#endif /* NOWRALLOC */
#else
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRTHROUGH,d0
#endif /* WBACK */
	movhu	d0,(a0)					# enable
#endif /* ENABLED */

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

#ifdef CONFIG_SMP
	# mark the primary CPU in cpu_boot_map
	mov	cpu_boot_map,a0
	mov	0x1,d0
	mov	d0,(a0)

	# signal each secondary CPU to begin booting
	mov	0x1,d2				# CPU ID

loop_request_boot_secondary:
	mov	d2,a0
	# send SMP_BOOT_IPI to secondary CPU
	asl	CROSS_ICR_CPU_SHIFT,a0
	add	CROSS_GxICR(SMP_BOOT_IRQ,0),a0
	movhu	(a0),d0
	or	GxICR_REQUEST|GxICR_DETECT,d0
	movhu	d0,(a0)
	movhu	(a0),d0				# flush

	# wait up to 100ms for AP's IPI to be received
	clr	d3
wait_on_secondary_boot:
	mov	DELAY_TIME_BOOT_IPI,d0
	call	__delay[],0
	inc	d3
	mov	cpu_boot_map,a0
	mov	(a0),d0
	lsr	d2,d0
	btst	0x1,d0
	bne	1f
	cmp	TIME_OUT_COUNT_BOOT_IPI,d3
	bne	wait_on_secondary_boot
1:
	inc	d2
	cmp	NR_CPUS,d2
	bne	loop_request_boot_secondary
#endif /* CONFIG_SMP */

#ifdef CONFIG_GDBSTUB
	call	gdbstub_init[],0

#ifdef CONFIG_GDBSTUB_IMMEDIATE
	.globl	__gdbstub_pause
__gdbstub_pause:
	bra	__gdbstub_pause
#endif
#endif

	jmp	start_kernel
	.size	_start,.-_start

###############################################################################
#
# Secondary CPU boot point
#
###############################################################################
#ifdef CONFIG_SMP
startup_secondary:
	# preload the PGD pointer register
	mov	swapper_pg_dir,d0
	mov	d0,(PTBR)
	clr	d0
	movbu	d0,(PIDR)

	# turn on the TLBs
	mov	MMUCTR_IIV|MMUCTR_DIV,d0
	mov	d0,(MMUCTR)
#ifdef CONFIG_AM34_2
	mov	MMUCTR_ITE|MMUCTR_DTE|MMUCTR_CE|MMUCTR_WTE,d0
#else
	mov	MMUCTR_ITE|MMUCTR_DTE|MMUCTR_CE,d0
#endif
	mov	d0,(MMUCTR)

	# turn on AM33v2 exception handling mode and set the trap table base
	movhu	(CPUP),d0
	or	CPUP_EXM_AM33V2,d0
	movhu	d0,(CPUP)

	# set the interrupt vector table
	mov	CONFIG_INTERRUPT_VECTOR_BASE,d0
	mov	d0,(TBR)

	# invalidate and enable both of the caches
	mov	ECHCTR,a0
	clr	d0
	mov	d0,(a0)
	mov	CHCTR,a0
	clr	d0
	movhu	d0,(a0)					# turn off first
	mov	CHCTR_ICINV|CHCTR_DCINV,d0
	movhu	d0,(a0)
	setlb
	mov	(a0),d0
	btst	CHCTR_ICBUSY|CHCTR_DCBUSY,d0		# wait till not busy (use CPU loop buffer)
	lne

#ifdef CONFIG_MN10300_CACHE_ENABLED
#ifdef  CONFIG_MN10300_CACHE_WBACK
#ifndef CONFIG_MN10300_CACHE_WBACK_NOWRALLOC
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRBACK,d0
#else
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRBACK|CHCTR_DCALMD,d0
#endif  /* !NOWRALLOC */
#else
	mov	CHCTR_ICEN|CHCTR_DCEN|CHCTR_DCWTMD_WRTHROUGH,d0
#endif  /* WBACK */
	movhu	d0,(a0)					# enable
#endif  /* ENABLED */

	# Clear the boot IPI interrupt for this CPU
	movhu	(GxICR(SMP_BOOT_IRQ)),d0
	and	~GxICR_REQUEST,d0
	movhu	d0,(GxICR(SMP_BOOT_IRQ))
	movhu	(GxICR(SMP_BOOT_IRQ)),d0		# flush

	/* get stack */
	mov	CONFIG_INTERRUPT_VECTOR_BASE + CONFIG_BOOT_STACK_OFFSET,a0
	mov	(CPUID),d0
	and	CPUID_MASK,d0
	mulu	CONFIG_BOOT_STACK_SIZE,d0
	sub	d0,a0
	mov	a0,sp

	# init interrupt for AP
	call	smp_prepare_cpu_init[],0

	# mark this secondary CPU in cpu_boot_map
	mov	(CPUID),d0
	mov	0x1,d1
	asl	d0,d1
	mov	cpu_boot_map,a0
	bset	d1,(a0)

	or	EPSW_IE|EPSW_IM_1,epsw  # permit level 0 interrupts
	nop
	nop
#ifdef  CONFIG_MN10300_CACHE_WBACK
	# flush the local cache if it's in writeback mode
	call	mn10300_local_dcache_flush_inv[],0
	setlb
	mov	(CHCTR),d0
	btst	CHCTR_DCBUSY,d0		# wait till not busy (use CPU loop buffer)
	lne
#endif

	# now sleep waiting for further instructions
secondary_sleep:
	mov	CPUM_SLEEP,d0
	movhu	d0,(CPUM)
	nop
	nop
	bra	secondary_sleep
	.size	startup_secondary,.-startup_secondary
#endif /* CONFIG_SMP */

###############################################################################
#
#
#
###############################################################################
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
