/* SPDX-License-Identifier: GPL-2.0 */
	.type	initrd_start,#object
	.globl	initrd_start
initrd_start:
	.incbin	INITRD
	.globl	initrd_end
initrd_end:
