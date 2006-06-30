/*
 *  linux/arch/m32r/boot/compressed/head.S
 *
 *  Copyright (c) 2001-2003	Hiroyuki Kondo, Hirokazu Takata,
 *				Hitoshi Yamamoto, Takeo Takahashi
 *  Copyright (c) 2004		Hirokazu Takata
 */

	.text
#include <linux/linkage.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/assembler.h>

	/*
	 * This code can be loaded anywhere, as long as output will not
	 * overlap it.
	 *
	 * NOTE: This head.S should *NOT* be compiled with -fpic.
	 *
	 */

	.global	startup
	.global __bss_start, _ebss, end, zimage_data, zimage_len
	__ALIGN
startup:
	ldi	r0, #0x0000			/* SPI, disable EI */
	mvtc	r0, psw

	ldi	r12, #-8
	bl	1f
	.fillinsn
1:
	seth	r1, #high(CONFIG_MEMORY_START + 0x00400000) /* Start address */
	add	r12, r14				/* Real address */
	sub	r12, r1					/* difference */

	.global got_len
	seth	r3, #high(_GLOBAL_OFFSET_TABLE_+8)
	or3	r3, r3, #low(_GLOBAL_OFFSET_TABLE_+12)
	add	r3, r14

	/* Update the contents of global offset table */
	ldi	r1, #low(got_len)
	srli	r1, #2
	beqz	r1, 2f
	.fillinsn
1:
	ld	r2, @r3
	add	r2, r12
	st	r2, @r3
	addi	r3, #4
	addi	r1, #-1
	bnez	r1, 1b
	.fillinsn
2:
	/* XXX: resolve plt */

/*
 * Clear BSS first so that there are no surprises...
 */
#ifdef CONFIG_ISA_DUAL_ISSUE
	seth	r2, #high(__bss_start)
	or3	r2, r2, #low(__bss_start)
	add	r2, r12
	seth	r3, #high(_ebss)
	or3	r3, r3, #low(_ebss)
	add	r3, r12
	sub	r3, r2

	; R4 = BSS size in longwords (rounded down)
	mv	r4, r3		    ||	ldi	r1, #0
	srli	r4, #4		    ||	addi	r2, #-4
	beqz	r4, .Lendloop1
.Lloop1:
#ifndef CONFIG_CHIP_M32310
	; Touch memory for the no-write-allocating cache.
	ld	r0, @(4,r2)
#endif
	st	r1, @+r2	    ||	addi	r4, #-1
	st	r1, @+r2
	st	r1, @+r2
	st	r1, @+r2	    ||	cmpeq	r1, r4	; R4 = 0?
	bnc	.Lloop1
.Lendloop1:
	and3	r4, r3, #15
	addi	r2, #4
	beqz	r4, .Lendloop2
.Lloop2:
	stb	r1, @r2		    ||	addi	r4, #-1
	addi	r2, #1
	bnez	r4, .Lloop2
.Lendloop2:

#else /* not CONFIG_ISA_DUAL_ISSUE */
	seth	r2, #high(__bss_start)
	or3	r2, r2, #low(__bss_start)
	add	r2, r12
	seth	r3, #high(_ebss)
	or3	r3, r3, #low(_ebss)
	add	r3, r12
	sub	r3, r2
	mv	r4, r3
	srli	r4, #2		; R4 = BSS size in longwords (rounded down)
	ldi	r1, #0		; clear R1 for longwords store
	addi	r2, #-4		; account for pre-inc store
	beqz	r4, .Lendloop1	; any more to go?
.Lloop1:
	st	r1, @+r2	; yep, zero out another longword
	addi	r4, #-1		; decrement count
	bnez	r4, .Lloop1	; go do some more
.Lendloop1:

#endif /* not CONFIG_ISA_DUAL_ISSUE */

	seth	r1, #high(end)
	or3	r1, r1, #low(end)
	add	r1, r12
	mv	sp, r1

/*
 * decompress the kernel
 */
	mv	r0, sp
	srli	r0, 31				/* MMU is ON or OFF */
        seth	r1, #high(zimage_data)
        or3	r1, r1, #low(zimage_data)
	add	r1, r12
        seth	r2, #high(zimage_len)
        or3	r2, r2, #low(zimage_len)
	mv	r3, sp

	bl	decompress_kernel

#if defined(CONFIG_CHIP_M32700) || defined(CONFIG_CHIP_OPSP) || defined(CONFIG_CHIP_VDEC2)
	/* Cache flush */
	ldi	r0, -1
	ldi	r1, 0xd0	; invalidate i-cache, copy back d-cache
	stb	r1, @r0
#elif defined(CONFIG_CHIP_M32102)
	/* Cache flush */
	ldi	r0, -2
	ldi	r1, 0x0100	; invalidate
	stb	r1, @r0
#elif defined(CONFIG_CHIP_M32104)
	/* Cache flush */
	ldi	r0, -2
	ldi	r1, 0x0700	; invalidate i-cache, copy back d-cache
	sth	r1, @r0
#else
#error "put your cache flush function, please"
#endif

	mv	r0, sp
	srli	r0, 31				/* MMU is ON or OFF */
	slli	r0, 31
	or3	r0, r0, #0x2000
	seth	r1, #high(CONFIG_MEMORY_START)
	or	r0, r1
	jmp	r0

	.balign 512
fake_headers_as_bzImage:
	.short	0
	.ascii	"HdrS"
	.short	0x0202
	.short	0
	.short	0
	.byte	0x00, 0x10
	.short	0
	.byte	0
	.byte	1
	.byte	0x00, 0x80
	.long	0
	.long	0

