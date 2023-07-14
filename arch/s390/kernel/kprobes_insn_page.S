/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/linkage.h>

/*
 * insn_page is a special 4k aligned dummy function for kprobes.
 * It will contain all kprobed instructions that are out-of-line executed.
 * The page must be within the kernel image to guarantee that the
 * out-of-line instructions are within 2GB distance of their original
 * location. Using a dummy function ensures that the insn_page is within
 * the text section of the kernel and mapped read-only/executable from
 * the beginning on, thus avoiding to split large mappings if the page
 * would be in the data section instead.
 */
	.section .kprobes.text, "ax"
	.balign 4096
SYM_CODE_START(kprobes_insn_page)
	.rept 2048
	.word 0x07fe
	.endr
SYM_CODE_END(kprobes_insn_page)
	.previous
