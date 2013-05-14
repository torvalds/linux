/*
 * Copyright (C) 2009 Thomas Chou <thomas@wytron.com.tw>
 *
 * Based on arch/nios2/kernel/head.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

/*
 *  This code can be loaded anywhere, eg FLASH ROM as reset vector,
 *  as long as output does not overlap it.
 */

#include <linux/linkage.h>
#include <asm/cache.h>

	.text
	.set noat
ENTRY(_start)
	wrctl	status, r0		/* disable interrupt */
	/* invalidate all instruction cache */
	movia	r1, NIOS2_ICACHE_SIZE
	movui	r2, NIOS2_ICACHE_LINE_SIZE
1:	initi	r1
	sub	r1, r1, r2
	bgt	r1, r0, 1b
	/* invalidate all data cache */
	movia	r1, NIOS2_DCACHE_SIZE
	movui	r2, NIOS2_DCACHE_LINE_SIZE
1:	initd	0(r1)
	sub	r1, r1, r2
	bgt	r1, r0, 1b

	nextpc	r1			/* Find out where we are */
chkadr:
	movia	r2, chkadr
	beq	r1, r2, finish_move	/* We are running in correct address,
					   done */
	/* move code, r1: src, r2: dest, r3: last dest */
	addi	r1, r1, (_start - chkadr)	/* Source */
	movia	r2, _start		/* Destination */
	movia	r3, __bss_start		/* End of copy */
1:	ldw	r8, 0(r1)		/* load a word from [r1] */
	stw	r8, 0(r2)		/* stort a word to dest [r2] */
	addi	r1, r1, 4		/* inc the src addr */
	addi	r2, r2, 4		/* inc the dest addr */
	blt	r2, r3, 1b
	/* flush the data cache after moving */
	movia	r1, NIOS2_DCACHE_SIZE
	movui	r2, NIOS2_DCACHE_LINE_SIZE
1:	flushd	0(r1)
	sub	r1, r1, r2
	bgt	r1, r0, 1b
	movia	r1, finish_move
	jmp	r1			/* jmp to linked address */

finish_move:
	/* zero out the .bss segment (uninitialized common data) */
	movia	r2, __bss_start		/* presume nothing is between */
	movia	r1, _end		/* the .bss and _end. */
1: 	stb	r0, 0(r2)
	addi	r2, r2, 1
	bne	r1, r2, 1b
	/*
	 * set up the stack pointer, some where higher than _end.
	 * The stack space must be greater than 32K for decompress.
	 */
	movia	sp, 0x10000
	add	sp, sp, r1
	/* save args passed from u-boot, maybe */
	addi	sp, sp, -16
	stw	r4, 0(sp)
	stw	r5, 4(sp)
	stw	r6, 8(sp)
	stw	r7, 12(sp)
	/* decompress the kernel */
	call	decompress_kernel
	/* pass saved args to kernel */
	ldw	r4, 0(sp)
	ldw	r5, 4(sp)
	ldw	r6, 8(sp)
	ldw	r7, 12(sp)

	/* flush all data cache after decompressing */
	movia	r1, NIOS2_DCACHE_SIZE
	movui	r2, NIOS2_DCACHE_LINE_SIZE
1:	flushd	0(r1)
	sub	r1, r1, r2
	bgt	r1, r0, 1b
	/* flush all instruction cache */
	movia	r1, NIOS2_ICACHE_SIZE
	movui	r2, NIOS2_ICACHE_LINE_SIZE
1:	flushi	r1
	sub	r1, r1, r2
	bgt	r1, r0, 1b
	flushp
	/* jump to start real kernel */
	movia	r1, (CONFIG_MEM_BASE | CONFIG_KERNEL_REGION_BASE)
	jmp	r1

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
