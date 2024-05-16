/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 * Copyright (C) 1995 - 1999 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Modified for DECStation and hence R3000 support by Paul M. Antoine
 * Further modifications by David S. Miller and Harald Koerfgen
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */

#include <asm/asm.h>
#include <asm/regdef.h>

	LEAF(start)
	/* Save boot rom start args */
	move	s0, a0
	move	s1, a1
	move	s2, a2
	move	s3, a3

	/* Clear BSS */
	PTR_LA	a0, _edata
	PTR_LA	a2, _end
1:	PTR_S	zero, 0(a0)
	PTR_ADDIU a0, a0, PTRSIZE
	bne	a2, a0, 1b

	PTR_LA	a0, (.heap)	     /* heap address */
	PTR_LA	sp, (.stack + 8192)  /* stack address */

	PTR_LA	t9, decompress_kernel
	jalr	t9

2:
	move	a0, s0
	move	a1, s1
	move	a2, s2
	move	a3, s3
	PTR_LI	t9, KERNEL_ENTRY
	jalr	t9

3:
	b	3b

	END(start)

	.comm .heap,BOOT_HEAP_SIZE,4
	.comm .stack,4096*2,4
