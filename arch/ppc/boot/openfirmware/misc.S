/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
	.text

/*
 * Use the BAT2 & 3 registers to map the 1st 16MB of RAM to
 * the address given as the 1st argument.
 */
	.globl	setup_bats
setup_bats:
	mfpvr	5
	rlwinm	5,5,16,16,31		/* r3 = 1 for 601, 4 for 604 */
	cmpwi	0,5,1
	li	0,0
	bne	4f
	mtibatl	3,0			/* invalidate BAT first */
	ori	3,3,4			/* set up BAT registers for 601 */
	li	4,0x7f
	mtibatu	2,3
	mtibatl	2,4
	oris	3,3,0x80
	oris	4,4,0x80
	mtibatu	3,3
	mtibatl	3,4
	b	5f
4:	mtdbatu	3,0			/* invalidate BATs first */
	mtibatu	3,0
	ori	3,3,0xff		/* set up BAT registers for 604 */
	li	4,2
	mtdbatl	2,4
	mtdbatu	2,3
	mtibatl	2,4
	mtibatu	2,3
	oris	3,3,0x80
	oris	4,4,0x80
	mtdbatl	3,4
	mtdbatu	3,3
	mtibatl	3,4
	mtibatu	3,3
5:	sync
	isync
	blr

/*
 * Flush the dcache and invalidate the icache for a range of addresses.
 *
 * flush_cache(addr, len)
 */
	.global	flush_cache
flush_cache:
	addi	4,4,0x1f	/* len = (len + 0x1f) / 0x20 */
	rlwinm.	4,4,27,5,31
	mtctr	4
	beqlr
1:	dcbf	0,3
	icbi	0,3
	addi	3,3,0x20
	bdnz	1b
	sync
	isync
	blr
