/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/drivers/acorn/scsi/acornscsi-io.S: Acorn SCSI card IO
 */
#include <linux/linkage.h>

#include <asm/assembler.h>
#include <mach/hardware.h>

#if defined(__APCS_32__)
#define LOADREGS(t,r,l...)	ldm##t	r, l
#elif defined(__APCS_26__)
#define LOADREGS(t,r,l...)	ldm##t	r, l##^
#endif

@ Purpose: transfer a block of data from the acorn scsi card to memory
@ Proto  : void acornscsi_in(unsigned int addr_start, char *buffer, int length)
@ Returns: nothing

		.align
ENTRY(__acornscsi_in)
		stmfd	sp!, {r4 - r7, lr}
		bic	r0, r0, #3
		mov	lr, #0xff
		orr	lr, lr, #0xff00
acornscsi_in16lp:
		subs	r2, r2, #16
		bmi	acornscsi_in8
		ldmia	r0!, {r3, r4, r5, r6}
		and	r3, r3, lr
		orr	r3, r3, r4, lsl #16
		and 	r4, r5, lr
		orr	r4, r4, r6, lsl #16
		ldmia	r0!, {r5, r6, r7, ip}
		and	r5, r5, lr
		orr	r5, r5, r6, lsl #16
		and	r6, r7, lr
		orr	r6, r6, ip, lsl #16
		stmia	r1!, {r3 - r6}
		bne	acornscsi_in16lp
		LOADREGS(fd, sp!, {r4 - r7, pc})

acornscsi_in8:	adds	r2, r2, #8
		bmi	acornscsi_in4
		ldmia	r0!, {r3, r4, r5, r6}
		and	r3, r3, lr
		orr	r3, r3, r4, lsl #16
		and	r4, r5, lr
		orr	r4, r4, r6, lsl #16
		stmia	r1!, {r3 - r4}
		LOADREGS(eqfd, sp!, {r4 - r7, pc})
		sub	r2, r2, #8

acornscsi_in4:	adds	r2, r2, #4
		bmi	acornscsi_in2
		ldmia	r0!, {r3, r4}
		and	r3, r3, lr
		orr	r3, r3, r4, lsl #16
		str	r3, [r1], #4
		LOADREGS(eqfd, sp!, {r4 - r7, pc})
		sub	r2, r2, #4

acornscsi_in2:	adds	r2, r2, #2
		ldr	r3, [r0], #4
		and	r3, r3, lr
		strb	r3, [r1], #1
		mov	r3, r3, lsr #8
		strplb	r3, [r1], #1
		LOADREGS(fd, sp!, {r4 - r7, pc})

@ Purpose: transfer a block of data from memory to the acorn scsi card
@ Proto  : void acornscsi_in(unsigned int addr_start, char *buffer, int length)
@ Returns: nothing

ENTRY(__acornscsi_out)
		stmfd	sp!, {r4 - r6, lr}
		bic	r0, r0, #3
acornscsi_out16lp:
		subs	r2, r2, #16
		bmi	acornscsi_out8
		ldmia	r1!, {r4, r6, ip, lr}
		mov	r3, r4, lsl #16
		orr	r3, r3, r3, lsr #16
		mov	r4, r4, lsr #16
		orr	r4, r4, r4, lsl #16
		mov	r5, r6, lsl #16
		orr	r5, r5, r5, lsr #16
		mov	r6, r6, lsr #16
		orr	r6, r6, r6, lsl #16
		stmia	r0!, {r3, r4, r5, r6}
		mov	r3, ip, lsl #16
		orr	r3, r3, r3, lsr #16
		mov	r4, ip, lsr #16
		orr	r4, r4, r4, lsl #16
		mov	ip, lr, lsl #16
		orr	ip, ip, ip, lsr #16
		mov	lr, lr, lsr #16
		orr	lr, lr, lr, lsl #16
		stmia	r0!, {r3, r4, ip, lr}
		bne	acornscsi_out16lp
		LOADREGS(fd, sp!, {r4 - r6, pc})

acornscsi_out8:	adds	r2, r2, #8
		bmi	acornscsi_out4
		ldmia	r1!, {r4, r6}
		mov	r3, r4, lsl #16
		orr	r3, r3, r3, lsr #16
		mov	r4, r4, lsr #16
		orr	r4, r4, r4, lsl #16
		mov	r5, r6, lsl #16
		orr	r5, r5, r5, lsr #16
		mov	r6, r6, lsr #16
		orr	r6, r6, r6, lsl #16
		stmia	r0!, {r3, r4, r5, r6}
		LOADREGS(eqfd, sp!, {r4 - r6, pc})

		sub	r2, r2, #8
acornscsi_out4:	adds	r2, r2, #4
		bmi	acornscsi_out2
		ldr	r4, [r1], #4
		mov	r3, r4, lsl #16
		orr	r3, r3, r3, lsr #16
		mov	r4, r4, lsr #16
		orr	r4, r4, r4, lsl #16
		stmia	r0!, {r3, r4}
		LOADREGS(eqfd, sp!, {r4 - r6, pc})

		sub	r2, r2, #4
acornscsi_out2:	adds	r2, r2, #2
		ldr	r3, [r1], #2
		strb	r3, [r0], #1
		mov	r3, r3, lsr #8
		strplb	r3, [r0], #1
		LOADREGS(fd, sp!, {r4 - r6, pc})

