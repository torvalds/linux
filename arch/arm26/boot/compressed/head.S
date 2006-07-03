/*
 *  linux/arch/arm26/boot/compressed/head.S
 *
 *  Copyright (C) 1996-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>

/*
 * Debugging stuff
 *
 * Note that these macros must not contain any code which is not
 * 100% relocatable.  Any attempt to do so will result in a crash.
 * Please select one of the following when turning on debugging.
 */

		.macro	kputc,val
		mov	r0, \val
		bl	putc
		.endm

		.macro	kphex,val,len
		mov	r0, \val
		mov	r1, #\len
		bl	phex
		.endm

		.macro	debug_reloc_start
		.endm

		.macro	debug_reloc_end
		.endm

		.section ".start", #alloc, #execinstr
/*
 * sort out different calling conventions
 */
		.align
start:
		.type	start,#function
		.rept	8
		mov	r0, r0
		.endr

		b	1f
		.word	0x016f2818		@ Magic numbers to help the loader
		.word	start			@ absolute load/run zImage address
		.word	_edata			@ zImage end address
1:		mov	r7, r1			@ save architecture ID
		mov	r8, #0			@ save r0
		teqp	pc, #0x0c000003		@ turn off interrupts

		.text
		adr	r0, LC0
		ldmia	r0, {r1, r2, r3, r4, r5, r6, ip, sp}
		subs	r0, r0, r1		@ calculate the delta offset

		teq	r0, #0			@ if delta is zero, we're
		beq	not_relocated		@ running at the address we
						@ were linked at.

		add	r2, r2, r0		@ different address, so we
		add	r3, r3, r0		@ need to fix up various
		add	r5, r5, r0		@ pointers.
		add	r6, r6, r0
		add	ip, ip, r0
		add	sp, sp, r0

1:		ldr	r1, [r6, #0]		@ relocate entries in the GOT
		add	r1, r1, r0		@ table.  This fixes up the
		str	r1, [r6], #4		@ C references.
		cmp	r6, ip
		blo	1b

not_relocated:	mov	r0, #0
1:		str	r0, [r2], #4		@ clear bss
		str	r0, [r2], #4
		str	r0, [r2], #4
		str	r0, [r2], #4
		cmp	r2, r3
		blo	1b

		bl	cache_on

		mov	r1, sp			@ malloc space above stack
		add	r2, sp, #0x10000	@ 64k max

/*
 * Check to see if we will overwrite ourselves.
 *   r4 = final kernel address
 *   r5 = start of this image
 *   r2 = end of malloc space (and therefore this image)
 * We basically want:
 *   r4 >= r2 -> OK
 *   r4 + image length <= r5 -> OK
 */
		cmp	r4, r2
		bhs	wont_overwrite
		add	r0, r4, #4096*1024	@ 4MB largest kernel size
		cmp	r0, r5
		bls	wont_overwrite

		mov	r5, r2			@ decompress after malloc space
		mov	r0, r5
		mov	r3, r7
		bl	decompress_kernel

		add	r0, r0, #127
		bic	r0, r0, #127		@ align the kernel length
/*
 * r0     = decompressed kernel length
 * r1-r3  = unused
 * r4     = kernel execution address
 * r5     = decompressed kernel start
 * r6     = processor ID
 * r7     = architecture ID
 * r8-r14 = unused
 */
		add	r1, r5, r0		@ end of decompressed kernel
		adr	r2, reloc_start
		ldr	r3, LC1
		add	r3, r2, r3
1:		ldmia	r2!, {r8 - r13}		@ copy relocation code
		stmia	r1!, {r8 - r13}
		ldmia	r2!, {r8 - r13}
		stmia	r1!, {r8 - r13}
		cmp	r2, r3
		blo	1b

		bl	cache_clean_flush
		add	pc, r5, r0		@ call relocation code

/*
 * We're not in danger of overwriting ourselves.  Do this the simple way.
 *
 * r4     = kernel execution address
 * r7     = architecture ID
 */
wont_overwrite:	mov	r0, r4
		mov	r3, r7
		bl	decompress_kernel
		b	call_kernel

		.type	LC0, #object
LC0:		.word	LC0			@ r1
		.word	__bss_start		@ r2
		.word	_end			@ r3
		.word	_load_addr		@ r4
		.word	_start			@ r5
		.word	_got_start		@ r6
		.word	_got_end		@ ip
		.word	user_stack+4096		@ sp
LC1:		.word	reloc_end - reloc_start
		.size	LC0, . - LC0

/*
 * Turn on the cache.  We need to setup some page tables so that we
 * can have both the I and D caches on.
 *
 * We place the page tables 16k down from the kernel execution address,
 * and we hope that nothing else is using it.  If we're using it, we
 * will go pop!
 *
 * On entry,
 *  r4 = kernel execution address
 *  r6 = processor ID
 *  r7 = architecture number
 *  r8 = run-time address of "start"
 * On exit,
 *  r1, r2, r3, r8, r9, r12 corrupted
 * This routine must preserve:
 *  r4, r5, r6, r7
 */
		.align	5
cache_on:	mov	r3, #8			@ cache_on function
		b	call_cache_fn

__setup_mmu:	sub	r3, r4, #16384		@ Page directory size
		bic	r3, r3, #0xff		@ Align the pointer
		bic	r3, r3, #0x3f00
/*
 * Initialise the page tables, turning on the cacheable and bufferable
 * bits for the RAM area only.
 */
		mov	r0, r3
		mov	r8, r0, lsr #18
		mov	r8, r8, lsl #18		@ start of RAM
		add	r9, r8, #0x10000000	@ a reasonable RAM size
		mov	r1, #0x12
		orr	r1, r1, #3 << 10
		add	r2, r3, #16384
1:		cmp	r1, r8			@ if virt > start of RAM
		orrhs	r1, r1, #0x0c		@ set cacheable, bufferable
		cmp	r1, r9			@ if virt > end of RAM
		bichs	r1, r1, #0x0c		@ clear cacheable, bufferable
		str	r1, [r0], #4		@ 1:1 mapping
		add	r1, r1, #1048576
		teq	r0, r2
		bne	1b
/*
 * If ever we are running from Flash, then we surely want the cache
 * to be enabled also for our execution instance...  We map 2MB of it
 * so there is no map overlap problem for up to 1 MB compressed kernel.
 * If the execution is in RAM then we would only be duplicating the above.
 */
		mov	r1, #0x1e
		orr	r1, r1, #3 << 10
		mov	r2, pc, lsr #20
		orr	r1, r1, r2, lsl #20
		add	r0, r3, r2, lsl #2
		str	r1, [r0], #4
		add	r1, r1, #1048576
		str	r1, [r0]
		mov	pc, lr

__armv4_cache_on:
		mov	r12, lr
		bl	__setup_mmu
		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4	@ drain write buffer
		mcr	p15, 0, r0, c8, c7, 0	@ flush I,D TLBs
		mrc	p15, 0, r0, c1, c0, 0	@ read control reg
		orr	r0, r0, #0x1000		@ I-cache enable
		orr	r0, r0, #0x0030
		b	__common_cache_on

__arm6_cache_on:
		mov	r12, lr
		bl	__setup_mmu
		mov	r0, #0
		mcr	p15, 0, r0, c7, c0, 0	@ invalidate whole cache v3
		mcr	p15, 0, r0, c5, c0, 0	@ invalidate whole TLB v3
		mov	r0, #0x30
__common_cache_on:
#ifndef DEBUG
		orr	r0, r0, #0x000d		@ Write buffer, mmu
#endif
		mov	r1, #-1
		mcr	p15, 0, r3, c2, c0, 0	@ load page table pointer
		mcr	p15, 0, r1, c3, c0, 0	@ load domain access control
		mcr	p15, 0, r0, c1, c0, 0	@ load control register
		mov	pc, r12

/*
 * All code following this line is relocatable.  It is relocated by
 * the above code to the end of the decompressed kernel image and
 * executed there.  During this time, we have no stacks.
 *
 * r0     = decompressed kernel length
 * r1-r3  = unused
 * r4     = kernel execution address
 * r5     = decompressed kernel start
 * r6     = processor ID
 * r7     = architecture ID
 * r8-r14 = unused
 */
		.align	5
reloc_start:	add	r8, r5, r0
		debug_reloc_start
		mov	r1, r4
1:
		.rept	4
		ldmia	r5!, {r0, r2, r3, r9 - r13}	@ relocate kernel
		stmia	r1!, {r0, r2, r3, r9 - r13}
		.endr

		cmp	r5, r8
		blo	1b
		debug_reloc_end

call_kernel:	bl	cache_clean_flush
		bl	cache_off
		mov	r0, #0
		mov	r1, r7			@ restore architecture number
		mov	pc, r4			@ call kernel

/*
 * Here follow the relocatable cache support functions for the
 * various processors.  This is a generic hook for locating an
 * entry and jumping to an instruction at the specified offset
 * from the start of the block.  Please note this is all position
 * independent code.
 *
 *  r1  = corrupted
 *  r2  = corrupted
 *  r3  = block offset
 *  r6  = corrupted
 *  r12 = corrupted
 */

call_cache_fn:	adr	r12, proc_types
		mrc	p15, 0, r6, c0, c0	@ get processor ID
1:		ldr	r1, [r12, #0]		@ get value
		ldr	r2, [r12, #4]		@ get mask
		eor	r1, r1, r6		@ (real ^ match)
		tst	r1, r2			@       & mask
		addeq	pc, r12, r3		@ call cache function
		add	r12, r12, #4*5
		b	1b

/*
 * Table for cache operations.  This is basically:
 *   - CPU ID match
 *   - CPU ID mask
 *   - 'cache on' method instruction
 *   - 'cache off' method instruction
 *   - 'cache flush' method instruction
 *
 * We match an entry using: ((real_id ^ match) & mask) == 0
 *
 * Writethrough caches generally only need 'on' and 'off'
 * methods.  Writeback caches _must_ have the flush method
 * defined.
 */
		.type	proc_types,#object
proc_types:
		.word	0x41560600		@ ARM6/610
		.word	0xffffffe0
		b	__arm6_cache_off	@ works, but slow
		b	__arm6_cache_off
		mov	pc, lr
@		b	__arm6_cache_on		@ untested
@		b	__arm6_cache_off
@		b	__armv3_cache_flush

		.word	0x41007000		@ ARM7/710
		.word	0xfff8fe00
		b	__arm7_cache_off
		b	__arm7_cache_off
		mov	pc, lr

		.word	0x41807200		@ ARM720T (writethrough)
		.word	0xffffff00
		b	__armv4_cache_on
		b	__armv4_cache_off
		mov	pc, lr

		.word	0x41129200		@ ARM920T
		.word	0xff00fff0
		b	__armv4_cache_on
		b	__armv4_cache_off
		b	__armv4_cache_flush

		.word	0x4401a100		@ sa110 / sa1100
		.word	0xffffffe0
		b	__armv4_cache_on
		b	__armv4_cache_off
		b	__armv4_cache_flush

		.word	0x6901b110		@ sa1110
		.word	0xfffffff0
		b	__armv4_cache_on
		b	__armv4_cache_off
		b	__armv4_cache_flush

		.word	0x69050000		@ xscale
		.word	0xffff0000
		b	__armv4_cache_on
		b	__armv4_cache_off
		b	__armv4_cache_flush

		.word	0			@ unrecognised type
		.word	0
		mov	pc, lr
		mov	pc, lr
		mov	pc, lr

		.size	proc_types, . - proc_types

/*
 * Turn off the Cache and MMU.  ARMv3 does not support
 * reading the control register, but ARMv4 does.
 *
 * On entry,  r6 = processor ID
 * On exit,   r0, r1, r2, r3, r12 corrupted
 * This routine must preserve: r4, r6, r7
 */
		.align	5
cache_off:	mov	r3, #12			@ cache_off function
		b	call_cache_fn

__armv4_cache_off:
		mrc	p15, 0, r0, c1, c0
		bic	r0, r0, #0x000d
		mcr	p15, 0, r0, c1, c0	@ turn MMU and cache off
		mov	r0, #0
		mcr	p15, 0, r0, c7, c7	@ invalidate whole cache v4
		mcr	p15, 0, r0, c8, c7	@ invalidate whole TLB v4
		mov	pc, lr

__arm6_cache_off:
		mov	r0, #0x00000030		@ ARM6 control reg.
		b	__armv3_cache_off

__arm7_cache_off:
		mov	r0, #0x00000070		@ ARM7 control reg.
		b	__armv3_cache_off

__armv3_cache_off:
		mcr	p15, 0, r0, c1, c0, 0	@ turn MMU and cache off
		mov	r0, #0
		mcr	p15, 0, r0, c7, c0, 0	@ invalidate whole cache v3
		mcr	p15, 0, r0, c5, c0, 0	@ invalidate whole TLB v3
		mov	pc, lr

/*
 * Clean and flush the cache to maintain consistency.
 *
 * On entry,
 *  r6 = processor ID
 * On exit,
 *  r1, r2, r3, r12 corrupted
 * This routine must preserve:
 *  r0, r4, r5, r6, r7
 */
		.align	5
cache_clean_flush:
		mov	r3, #16
		b	call_cache_fn

__armv4_cache_flush:
		bic	r1, pc, #31
		add	r2, r1, #65536		@ 2x the largest dcache size
1:		ldr	r12, [r1], #32		@ s/w flush D cache
		teq	r1, r2
		bne	1b

		mcr	p15, 0, r1, c7, c7, 0	@ flush I cache
		mcr	p15, 0, r1, c7, c10, 4	@ drain WB
		mov	pc, lr

__armv3_cache_flush:
		mov	r1, #0
		mcr	p15, 0, r0, c7, c0, 0	@ invalidate whole cache v3
		mov	pc, lr

/*
 * Various debugging routines for printing hex characters and
 * memory, which again must be relocatable.
 */
#ifdef DEBUG
		.type	phexbuf,#object
phexbuf:	.space	12
		.size	phexbuf, . - phexbuf

phex:		adr	r3, phexbuf
		mov	r2, #0
		strb	r2, [r3, r1]
1:		subs	r1, r1, #1
		movmi	r0, r3
		bmi	puts
		and	r2, r0, #15
		mov	r0, r0, lsr #4
		cmp	r2, #10
		addge	r2, r2, #7
		add	r2, r2, #'0'
		strb	r2, [r3, r1]
		b	1b

puts:		loadsp	r3
1:		ldrb	r2, [r0], #1
		teq	r2, #0
		moveq	pc, lr
2:		writeb	r2
		mov	r1, #0x00020000
3:		subs	r1, r1, #1
		bne	3b
		teq	r2, #'\n'
		moveq	r2, #'\r'
		beq	2b
		teq	r0, #0
		bne	1b
		mov	pc, lr
putc:
		mov	r2, r0
		mov	r0, #0
		loadsp	r3
		b	2b

memdump:	mov	r12, r0
		mov	r10, lr
		mov	r11, #0
2:		mov	r0, r11, lsl #2
		add	r0, r0, r12
		mov	r1, #8
		bl	phex
		mov	r0, #':'
		bl	putc
1:		mov	r0, #' '
		bl	putc
		ldr	r0, [r12, r11, lsl #2]
		mov	r1, #8
		bl	phex
		and	r0, r11, #7
		teq	r0, #3
		moveq	r0, #' '
		bleq	putc
		and	r0, r11, #7
		add	r11, r11, #1
		teq	r0, #7
		bne	1b
		mov	r0, #'\n'
		bl	putc
		cmp	r11, #64
		blt	2b
		mov	pc, r10
#endif

reloc_end:

		.align
		.section ".stack", "aw"
user_stack:	.space	4096
