/* SPDX-License-Identifier: GPL-2.0 */
	.section ".rodata","a"
	.balign	16

/*
 * When a bootloader hands off to the kernel in 32-bit mode an
 * IDT with a 2-byte limit and 4-byte base is needed. When a boot
 * loader hands off to a kernel 64-bit mode the base address
 * extends to 8-bytes. Reserve enough space for either scenario.
 */
SYM_DATA_START_LOCAL(tr_idt)
	.short  0
	.quad   0
SYM_DATA_END(tr_idt)
