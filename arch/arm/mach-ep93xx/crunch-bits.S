/*
 * arch/arm/kernel/crunch-bits.S
 * Cirrus MaverickCrunch context switching and handling
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * Shamelessly stolen from the iWMMXt code by Nicolas Pitre, which is
 * Copyright (c) 2003-2004, MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>
#include <mach/ep93xx-regs.h>

/*
 * We can't use hex constants here due to a bug in gas.
 */
#define CRUNCH_MVDX0		0
#define CRUNCH_MVDX1		8
#define CRUNCH_MVDX2		16
#define CRUNCH_MVDX3		24
#define CRUNCH_MVDX4		32
#define CRUNCH_MVDX5		40
#define CRUNCH_MVDX6		48
#define CRUNCH_MVDX7		56
#define CRUNCH_MVDX8		64
#define CRUNCH_MVDX9		72
#define CRUNCH_MVDX10		80
#define CRUNCH_MVDX11		88
#define CRUNCH_MVDX12		96
#define CRUNCH_MVDX13		104
#define CRUNCH_MVDX14		112
#define CRUNCH_MVDX15		120
#define CRUNCH_MVAX0L		128
#define CRUNCH_MVAX0M		132
#define CRUNCH_MVAX0H		136
#define CRUNCH_MVAX1L		140
#define CRUNCH_MVAX1M		144
#define CRUNCH_MVAX1H		148
#define CRUNCH_MVAX2L		152
#define CRUNCH_MVAX2M		156
#define CRUNCH_MVAX2H		160
#define CRUNCH_MVAX3L		164
#define CRUNCH_MVAX3M		168
#define CRUNCH_MVAX3H		172
#define CRUNCH_DSPSC		176

#define CRUNCH_SIZE		184

	.text

/*
 * Lazy switching of crunch coprocessor context
 *
 * r10 = struct thread_info pointer
 * r9  = ret_from_exception
 * lr  = undefined instr exit
 *
 * called from prefetch exception handler with interrupts enabled
 */
ENTRY(crunch_task_enable)
	inc_preempt_count r10, r3

	ldr	r8, =(EP93XX_APB_VIRT_BASE + 0x00130000)	@ syscon addr

	ldr	r1, [r8, #0x80]
	tst	r1, #0x00800000			@ access to crunch enabled?
	bne	2f				@ if so no business here
	mov	r3, #0xaa			@ unlock syscon swlock
	str	r3, [r8, #0xc0]
	orr	r1, r1, #0x00800000		@ enable access to crunch
	str	r1, [r8, #0x80]

	ldr	r3, =crunch_owner
	add	r0, r10, #TI_CRUNCH_STATE	@ get task crunch save area
	ldr	r2, [sp, #60]			@ current task pc value
	ldr	r1, [r3]			@ get current crunch owner
	str	r0, [r3]			@ this task now owns crunch
	sub	r2, r2, #4			@ adjust pc back
	str	r2, [sp, #60]

	ldr	r2, [r8, #0x80]
	mov	r2, r2				@ flush out enable (@@@)

	teq	r1, #0				@ test for last ownership
	mov	lr, r9				@ normal exit from exception
	beq	crunch_load			@ no owner, skip save

crunch_save:
	cfstr64		mvdx0, [r1, #CRUNCH_MVDX0]	@ save 64b registers
	cfstr64		mvdx1, [r1, #CRUNCH_MVDX1]
	cfstr64		mvdx2, [r1, #CRUNCH_MVDX2]
	cfstr64		mvdx3, [r1, #CRUNCH_MVDX3]
	cfstr64		mvdx4, [r1, #CRUNCH_MVDX4]
	cfstr64		mvdx5, [r1, #CRUNCH_MVDX5]
	cfstr64		mvdx6, [r1, #CRUNCH_MVDX6]
	cfstr64		mvdx7, [r1, #CRUNCH_MVDX7]
	cfstr64		mvdx8, [r1, #CRUNCH_MVDX8]
	cfstr64		mvdx9, [r1, #CRUNCH_MVDX9]
	cfstr64		mvdx10, [r1, #CRUNCH_MVDX10]
	cfstr64		mvdx11, [r1, #CRUNCH_MVDX11]
	cfstr64		mvdx12, [r1, #CRUNCH_MVDX12]
	cfstr64		mvdx13, [r1, #CRUNCH_MVDX13]
	cfstr64		mvdx14, [r1, #CRUNCH_MVDX14]
	cfstr64		mvdx15, [r1, #CRUNCH_MVDX15]

#ifdef __ARMEB__
#error fix me for ARMEB
#endif

	cfmv32al	mvfx0, mvax0			@ save 72b accumulators
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX0L]
	cfmv32am	mvfx0, mvax0
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX0M]
	cfmv32ah	mvfx0, mvax0
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX0H]
	cfmv32al	mvfx0, mvax1
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX1L]
	cfmv32am	mvfx0, mvax1
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX1M]
	cfmv32ah	mvfx0, mvax1
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX1H]
	cfmv32al	mvfx0, mvax2
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX2L]
	cfmv32am	mvfx0, mvax2
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX2M]
	cfmv32ah	mvfx0, mvax2
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX2H]
	cfmv32al	mvfx0, mvax3
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX3L]
	cfmv32am	mvfx0, mvax3
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX3M]
	cfmv32ah	mvfx0, mvax3
	cfstr32		mvfx0, [r1, #CRUNCH_MVAX3H]

	cfmv32sc	mvdx0, dspsc			@ save status word
	cfstr64		mvdx0, [r1, #CRUNCH_DSPSC]

	teq		r0, #0				@ anything to load?
	cfldr64eq	mvdx0, [r1, #CRUNCH_MVDX0]	@ mvdx0 was clobbered
	beq		1f

crunch_load:
	cfldr64		mvdx0, [r0, #CRUNCH_DSPSC]	@ load status word
	cfmvsc32	dspsc, mvdx0

	cfldr32		mvfx0, [r0, #CRUNCH_MVAX0L]	@ load 72b accumulators
	cfmval32	mvax0, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX0M]
	cfmvam32	mvax0, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX0H]
	cfmvah32	mvax0, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX1L]
	cfmval32	mvax1, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX1M]
	cfmvam32	mvax1, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX1H]
	cfmvah32	mvax1, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX2L]
	cfmval32	mvax2, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX2M]
	cfmvam32	mvax2, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX2H]
	cfmvah32	mvax2, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX3L]
	cfmval32	mvax3, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX3M]
	cfmvam32	mvax3, mvfx0
	cfldr32		mvfx0, [r0, #CRUNCH_MVAX3H]
	cfmvah32	mvax3, mvfx0

	cfldr64		mvdx0, [r0, #CRUNCH_MVDX0]	@ load 64b registers
	cfldr64		mvdx1, [r0, #CRUNCH_MVDX1]
	cfldr64		mvdx2, [r0, #CRUNCH_MVDX2]
	cfldr64		mvdx3, [r0, #CRUNCH_MVDX3]
	cfldr64		mvdx4, [r0, #CRUNCH_MVDX4]
	cfldr64		mvdx5, [r0, #CRUNCH_MVDX5]
	cfldr64		mvdx6, [r0, #CRUNCH_MVDX6]
	cfldr64		mvdx7, [r0, #CRUNCH_MVDX7]
	cfldr64		mvdx8, [r0, #CRUNCH_MVDX8]
	cfldr64		mvdx9, [r0, #CRUNCH_MVDX9]
	cfldr64		mvdx10, [r0, #CRUNCH_MVDX10]
	cfldr64		mvdx11, [r0, #CRUNCH_MVDX11]
	cfldr64		mvdx12, [r0, #CRUNCH_MVDX12]
	cfldr64		mvdx13, [r0, #CRUNCH_MVDX13]
	cfldr64		mvdx14, [r0, #CRUNCH_MVDX14]
	cfldr64		mvdx15, [r0, #CRUNCH_MVDX15]

1:
#ifdef CONFIG_PREEMPT_COUNT
	get_thread_info r10
#endif
2:	dec_preempt_count r10, r3
	mov	pc, lr

/*
 * Back up crunch regs to save area and disable access to them
 * (mainly for gdb or sleep mode usage)
 *
 * r0 = struct thread_info pointer of target task or NULL for any
 */
ENTRY(crunch_task_disable)
	stmfd	sp!, {r4, r5, lr}

	mrs	ip, cpsr
	orr	r2, ip, #PSR_I_BIT		@ disable interrupts
	msr	cpsr_c, r2

	ldr	r4, =(EP93XX_APB_VIRT_BASE + 0x00130000)	@ syscon addr

	ldr	r3, =crunch_owner
	add	r2, r0, #TI_CRUNCH_STATE	@ get task crunch save area
	ldr	r1, [r3]			@ get current crunch owner
	teq	r1, #0				@ any current owner?
	beq	1f				@ no: quit
	teq	r0, #0				@ any owner?
	teqne	r1, r2				@ or specified one?
	bne	1f				@ no: quit

	ldr	r5, [r4, #0x80]			@ enable access to crunch
	mov	r2, #0xaa
	str	r2, [r4, #0xc0]
	orr	r5, r5, #0x00800000
	str	r5, [r4, #0x80]

	mov	r0, #0				@ nothing to load
	str	r0, [r3]			@ no more current owner
	ldr	r2, [r4, #0x80]			@ flush out enable (@@@)
	mov	r2, r2
	bl	crunch_save

	mov	r2, #0xaa			@ disable access to crunch
	str	r2, [r4, #0xc0]
	bic	r5, r5, #0x00800000
	str	r5, [r4, #0x80]
	ldr	r5, [r4, #0x80]			@ flush out enable (@@@)
	mov	r5, r5

1:	msr	cpsr_c, ip			@ restore interrupt mode
	ldmfd	sp!, {r4, r5, pc}

/*
 * Copy crunch state to given memory address
 *
 * r0 = struct thread_info pointer of target task
 * r1 = memory address where to store crunch state
 *
 * this is called mainly in the creation of signal stack frames
 */
ENTRY(crunch_task_copy)
	mrs	ip, cpsr
	orr	r2, ip, #PSR_I_BIT		@ disable interrupts
	msr	cpsr_c, r2

	ldr	r3, =crunch_owner
	add	r2, r0, #TI_CRUNCH_STATE	@ get task crunch save area
	ldr	r3, [r3]			@ get current crunch owner
	teq	r2, r3				@ does this task own it...
	beq	1f

	@ current crunch values are in the task save area
	msr	cpsr_c, ip			@ restore interrupt mode
	mov	r0, r1
	mov	r1, r2
	mov	r2, #CRUNCH_SIZE
	b	memcpy

1:	@ this task owns crunch regs -- grab a copy from there
	mov	r0, #0				@ nothing to load
	mov	r3, lr				@ preserve return address
	bl	crunch_save
	msr	cpsr_c, ip			@ restore interrupt mode
	mov	pc, r3

/*
 * Restore crunch state from given memory address
 *
 * r0 = struct thread_info pointer of target task
 * r1 = memory address where to get crunch state from
 *
 * this is used to restore crunch state when unwinding a signal stack frame
 */
ENTRY(crunch_task_restore)
	mrs	ip, cpsr
	orr	r2, ip, #PSR_I_BIT		@ disable interrupts
	msr	cpsr_c, r2

	ldr	r3, =crunch_owner
	add	r2, r0, #TI_CRUNCH_STATE	@ get task crunch save area
	ldr	r3, [r3]			@ get current crunch owner
	teq	r2, r3				@ does this task own it...
	beq	1f

	@ this task doesn't own crunch regs -- use its save area
	msr	cpsr_c, ip			@ restore interrupt mode
	mov	r0, r2
	mov	r2, #CRUNCH_SIZE
	b	memcpy

1:	@ this task owns crunch regs -- load them directly
	mov	r0, r1
	mov	r1, #0				@ nothing to save
	mov	r3, lr				@ preserve return address
	bl	crunch_load
	msr	cpsr_c, ip			@ restore interrupt mode
	mov	pc, r3
