/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/page-def.h>

SECTIONS {
#ifdef CONFIG_ARM64_MODULE_PLTS
	.plt 0 : { BYTE(0) }
	.init.plt 0 : { BYTE(0) }
	.text.ftrace_trampoline 0 : { BYTE(0) }
#endif

#ifdef CONFIG_KASAN_SW_TAGS
	/*
	 * Outlined checks go into comdat-deduplicated sections named .text.hot.
	 * Because they are in comdats they are not combined by the linker and
	 * we otherwise end up with multiple sections with the same .text.hot
	 * name in the .ko file. The kernel module loader warns if it sees
	 * multiple sections with the same name so we use this sections
	 * directive to force them into a single section and silence the
	 * warning.
	 */
	.text.hot : { *(.text.hot) }
#endif

#ifdef CONFIG_KVM
	.hyp.text : ALIGN(PAGE_SIZE) {
		*(.hyp.text)
		. = ALIGN(PAGE_SIZE);
	}
	.hyp.bss : ALIGN(PAGE_SIZE) {
		*(.hyp.bss)
		. = ALIGN(PAGE_SIZE);
	}
	.hyp.rodata : ALIGN(PAGE_SIZE) {
		*(.hyp.rodata)
		. = ALIGN(PAGE_SIZE);
	}
	.hyp.data : ALIGN(PAGE_SIZE) {
		*(.hyp.data)
		. = ALIGN(PAGE_SIZE);
	}
	.hyp.reloc : ALIGN(4) {	*(.hyp.reloc) }
#endif
}
