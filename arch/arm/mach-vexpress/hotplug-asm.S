#include <linux/linkage.h>
#include <asm/asm-offsets.h>

	.text
ENTRY(disable_clean_inv_dcache)
 ARM(	stmfd	sp!, {r4-r5, r7, r9-r11, lr}	)
 THUMB(	stmfd	sp!, {r4-r7, r9-r11, lr}	)

	mrc	p15, 0, r3, c1, c0, 0
	bic	r3, #4			@ clear C bit
	mcr	p15, 0, r3, c1, c0, 0
	dsb
	isb
	mov	r12, r0
	cmp	r12, #0
	bleq	v7_flush_dcache_louis
	cmp	r12, #0
	blne	v7_flush_dcache_all
	clrex
	mrc	p15, 0, r3, c1, c0, 1
	bic	r3, #0x40		@ clear SMP bit
	mcr	p15, 0, r3, c1, c0, 1
	isb
	dsb
 ARM(	ldmfd	sp!, {r4-r5, r7, r9-r11, lr}	)
 THUMB(	ldmfd	sp!, {r4-r7, r9-r11, lr}	)
	mov	pc, lr
ENDPROC(disable_clean_inv_dcache)
