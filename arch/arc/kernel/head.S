/*
 * ARC CPU startup Code
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vineetg: Dec 2007
 *  -Check if we are running on Simulator or on real hardware
 *      to skip certain things during boot on simulator
 */

#include <asm/asm-offsets.h>
#include <asm/entry.h>
#include <linux/linkage.h>
#include <asm/arcregs.h>

	.cpu A7

	.section .init.text, "ax",@progbits
	.type stext, @function
	.globl stext
stext:
	;-------------------------------------------------------------------
	; Don't clobber r0-r2 yet. It might have bootloader provided info
	;-------------------------------------------------------------------

	sr	@_int_vec_base_lds, [AUX_INTR_VEC_BASE]

#ifdef CONFIG_SMP
	; Ensure Boot (Master) proceeds. Others wait in platform dependent way
	;	IDENTITY Reg [ 3  2  1  0 ]
	;	(cpu-id)             ^^^	=> Zero for UP ARC700
	;					=> #Core-ID if SMP (Master 0)
	; Note that non-boot CPUs might not land here if halt-on-reset and
	; instead breath life from @first_lines_of_secondary, but we still
	; need to make sure only boot cpu takes this path.
	GET_CPU_ID  r5
	cmp	r5, 0
	mov.ne	r0, r5
	jne	arc_platform_smp_wait_to_boot
#endif
	; Clear BSS before updating any globals
	; XXX: use ZOL here
	mov	r5, __bss_start
	mov	r6, __bss_stop
1:
	st.ab   0, [r5,4]
	brlt    r5, r6, 1b

	; Uboot - kernel ABI
	;    r0 = [0] No uboot interaction, [1] cmdline in r2, [2] DTB in r2
	;    r1 = magic number (board identity, unused as of now
	;    r2 = pointer to uboot provided cmdline or external DTB in mem
	; These are handled later in setup_arch()
	st	r0, [@uboot_tag]
	st	r2, [@uboot_arg]

	; Identify if running on ISS vs Silicon
	; 	IDENTITY Reg [ 3  2  1  0 ]
	;	(chip-id)      ^^^^^		==> 0xffff for ISS
	lr	r0, [identity]
	lsr	r3, r0, 16
	cmp	r3, 0xffff
	mov.z	r4, 0
	mov.nz	r4, 1
	st	r4, [@running_on_hw]

	; setup "current" tsk and optionally cache it in dedicated r25
	mov	r9, @init_task
	SET_CURR_TASK_ON_CPU  r9, r0	; r9 = tsk, r0 = scratch

	; setup stack (fp, sp)
	mov	fp, 0

	; tsk->thread_info is really a PAGE, whose bottom hoists stack
	GET_TSK_STACK_BASE r9, sp	; r9 = tsk, sp = stack base(output)

	j	start_kernel	; "C" entry point

#ifdef CONFIG_SMP
;----------------------------------------------------------------
;     First lines of code run by secondary before jumping to 'C'
;----------------------------------------------------------------
	.section .text, "ax",@progbits
	.type first_lines_of_secondary, @function
	.globl first_lines_of_secondary

first_lines_of_secondary:

	sr	@_int_vec_base_lds, [AUX_INTR_VEC_BASE]

	; setup per-cpu idle task as "current" on this CPU
	ld	r0, [@secondary_idle_tsk]
	SET_CURR_TASK_ON_CPU  r0, r1

	; setup stack (fp, sp)
	mov	fp, 0

	; set it's stack base to tsk->thread_info bottom
	GET_TSK_STACK_BASE r0, sp

	j	start_kernel_secondary

#endif
