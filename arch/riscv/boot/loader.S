/* SPDX-License-Identifier: GPL-2.0 */

	.align 4
	.section .payload, "ax", %progbits
	.globl _start
_start:
	.incbin "arch/riscv/boot/Image"

