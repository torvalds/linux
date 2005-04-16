/*
 *  linux/arch/arm26/lib/uaccess-kernel.S
 *
 *  Copyright (C) 1998 Russell King
 *
 *  Note!  Some code fragments found in here have a special calling
 *  convention - they are not APCS compliant!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text

//FIXME - surely this can be done in C not asm, removing the problem of keeping C and asm in sync? (this is a struct uaccess_t)

		.globl	uaccess_kernel
uaccess_kernel:
		.word	uaccess_kernel_put_byte
		.word	uaccess_kernel_get_byte
		.word	uaccess_kernel_put_half
		.word	uaccess_kernel_get_half
		.word	uaccess_kernel_put_word
		.word	uaccess_kernel_get_word
		.word	uaccess_kernel_put_dword
		.word	uaccess_kernel_copy
		.word	uaccess_kernel_copy
		.word	uaccess_kernel_clear
		.word	uaccess_kernel_strncpy
		.word	uaccess_kernel_strnlen

@ In : r0 = x, r1 = addr, r2 = error
@ Out: r2 = error
uaccess_kernel_put_byte:
		stmfd	sp!, {lr}
		strb	r0, [r1]
		ldmfd	sp!, {pc}^

@ In : r0 = x, r1 = addr, r2 = error
@ Out: r2 = error
uaccess_kernel_put_half:
		stmfd	sp!, {lr}
		strb	r0, [r1]
		mov	r0, r0, lsr #8
		strb	r0, [r1, #1]
		ldmfd	sp!, {pc}^

@ In : r0 = x, r1 = addr, r2 = error
@ Out: r2 = error
uaccess_kernel_put_word:
		stmfd	sp!, {lr}
		str	r0, [r1]
		ldmfd	sp!, {pc}^

@ In : r0 = x, r1 = addr, r2 = error
@ Out: r2 = error
uaccess_kernel_put_dword:
                stmfd   sp!, {lr}
                str     r0, [r1], #4
		str	r0, [r1], #0
                ldmfd   sp!, {pc}^

@ In : r0 = addr, r1 = error
@ Out: r0 = x, r1 = error
uaccess_kernel_get_byte:
		stmfd	sp!, {lr}
		ldrb	r0, [r0]
		ldmfd	sp!, {pc}^

@ In : r0 = addr, r1 = error
@ Out: r0 = x, r1 = error
uaccess_kernel_get_half:
		stmfd	sp!, {lr}
		ldr	r0, [r0]
		mov	r0, r0, lsl #16
		mov	r0, r0, lsr #16
		ldmfd	sp!, {pc}^

@ In : r0 = addr, r1 = error
@ Out: r0 = x, r1 = error
uaccess_kernel_get_word:
		stmfd	sp!, {lr}
		ldr	r0, [r0]
		ldmfd	sp!, {pc}^


/* Prototype: int uaccess_kernel_copy(void *to, const char *from, size_t n)
 * Purpose  : copy a block to kernel memory from kernel memory
 * Params   : to   - kernel memory
 *          : from - kernel memory
 *          : n    - number of bytes to copy
 * Returns  : Number of bytes NOT copied.
 */
uaccess_kernel_copy:
		stmfd	sp!, {lr}
		bl	memcpy
		mov	r0, #0
		ldmfd	sp!, {pc}^

/* Prototype: int uaccess_kernel_clear(void *addr, size_t sz)
 * Purpose  : clear some kernel memory
 * Params   : addr - kernel memory address to clear
 *          : sz   - number of bytes to clear
 * Returns  : number of bytes NOT cleared
 */
uaccess_kernel_clear:
		stmfd	sp!, {lr}
		mov	r2, #0
		cmp	r1, #4
		blt	2f
		ands	ip, r0, #3
		beq	1f
		cmp	ip, #1
		strb	r2, [r0], #1
		strleb	r2, [r0], #1
		strltb	r2, [r0], #1
		rsb	ip, ip, #4
		sub	r1, r1, ip		@  7  6  5  4  3  2  1
1:		subs	r1, r1, #8		@ -1 -2 -3 -4 -5 -6 -7
		bmi	2f
		str	r2, [r0], #4
		str	r2, [r0], #4
		b	1b
2:		adds	r1, r1, #4		@  3  2  1  0 -1 -2 -3
		strpl	r2, [r0], #4
		tst	r1, #2			@ 1x 1x 0x 0x 1x 1x 0x
		strneb	r2, [r0], #1
		strneb	r2, [r0], #1
		tst	r1, #1			@ x1 x0 x1 x0 x1 x0 x1
		strneb	r2, [r0], #1
		mov	r0, #0
		ldmfd	sp!, {pc}^

/* Prototype: size_t uaccess_kernel_strncpy(char *dst, char *src, size_t len)
 * Purpose  : copy a string from kernel memory to kernel memory
 * Params   : dst - kernel memory destination
 *          : src - kernel memory source
 *          : len - maximum length of string
 * Returns  : number of characters copied
 */
uaccess_kernel_strncpy:
		stmfd	sp!, {lr}
		mov	ip, r2
1:		subs	r2, r2, #1
		bmi	2f
		ldrb	r3, [r1], #1
		strb	r3, [r0], #1
		teq	r3, #0
		bne	1b
2:		subs	r0, ip, r2
		ldmfd	sp!, {pc}^

/* Prototype: int uaccess_kernel_strlen(char *str, long n)
 * Purpose  : get length of a string in kernel memory
 * Params   : str - address of string in kernel memory
 * Returns  : length of string *including terminator*,
 *            or zero on exception, or n + 1 if too long
 */
uaccess_kernel_strnlen:
		stmfd	sp!, {lr}
		mov	r2, r0
1:		ldrb	r1, [r0], #1
		teq	r1, #0
		beq	2f
		subs	r1, r1, #1
		bne	1b
		add	r0, r0, #1
2:		sub	r0, r0, r2
		ldmfd	sp!, {pc}^

