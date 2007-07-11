/*
 *  PS3 bootwrapper entry.
 *
 *  Copyright (C) 2007 Sony Computer Entertainment Inc.
 *  Copyright 2007 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ppc_asm.h"

	.text

/*
 * __system_reset_overlay - The PS3 first stage entry.
 *
 * The bootwraper build script copies the 0x100 bytes at symbol
 * __system_reset_overlay to offset 0x100 of the rom image.
 *
 * The PS3 has a single processor with two threads.
 */

	.globl __system_reset_overlay
__system_reset_overlay:

	/* Switch to 32-bit mode. */

	mfmsr	r9
	clrldi	r9,r9,1
	mtmsrd	r9
	nop

	/* Get thread number in r3 and branch. */

	mfspr	r3, 0x88
	cntlzw.	r3, r3
	li	r4, 0
	li	r5, 0
	beq	1f

	/* Secondary goes to __secondary_hold in kernel. */

	li	r4, 0x60
	mtctr	r4
	bctr

	/* Primary delays then goes to _zimage_start in wrapper. */
1:
	or	31, 31, 31 /* db16cyc */
	or	31, 31, 31 /* db16cyc */

	lis	r4, _zimage_start@ha
	addi	r4, r4, _zimage_start@l
	mtctr	r4
	bctr

/*
 * __system_reset_kernel - Place holder for the kernel reset vector.
 *
 * The bootwrapper build script copies 0x100 bytes from offset 0x100
 * of the rom image to the symbol __system_reset_kernel.  At runtime
 * the bootwrapper program copies the 0x100 bytes at __system_reset_kernel
 * to ram address 0x100.  This symbol must occupy 0x100 bytes.
 */

	.globl __system_reset_kernel
__system_reset_kernel:

	. = __system_reset_kernel + 0x100
