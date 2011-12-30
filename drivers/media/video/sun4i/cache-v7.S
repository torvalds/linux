
#ifndef __ASSEMBLY__
#define __ASSEMBLY__
#endif
#include <asm/assembler.h>
/*
    c code declared as follows:
    int flush_clean_user_range(long start, long end);
*/
	.macro	dcache_line_size, reg, tmp
	mrc	p15, 1, \tmp, c0, c0, 0		@ read CSIDR
	and	\tmp, \tmp, #7			@ cache line size encoding
	mov	\reg, #16			@ size offset
	mov	\reg, \reg, lsl \tmp		@ actual cache line size
	.endm

    .text
    .globl flush_clean_user_range
flush_clean_user_range:
    dcache_line_size r2, r3
    sub	r3, r2, #1
    bic	r0, r0, r3
1:
    USER(	mcr	p15, 0, r0, c7, c14, 1	)	@ clean and flush D line to the point of unification
    add	r0, r0, r2
2:
    cmp	r0, r1
    blo	1b
    mov	r0, #0
    dsb
    mov	pc, lr

/*
 * Fault handling for the cache operation above. If the virtual address in r0
 * isn't mapped, just try the next page.
 */
9001:
    mov	r0, r0, lsr #12
    mov	r0, r0, lsl #12
    add	r0, r0, #4096
    b	2b


