/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 ARM Limited
 */
#ifndef __ASM_SECTIONS_H
#define __ASM_SECTIONS_H

#include <asm-generic/sections.h>

extern char __alt_instructions[], __alt_instructions_end[];
extern char __hibernate_exit_text_start[], __hibernate_exit_text_end[];
extern char __hyp_idmap_text_start[], __hyp_idmap_text_end[];
extern char __hyp_text_start[], __hyp_text_end[];
extern char __hyp_rodata_start[], __hyp_rodata_end[];
extern char __hyp_reloc_begin[], __hyp_reloc_end[];
extern char __hyp_bss_start[], __hyp_bss_end[];
extern char __idmap_text_start[], __idmap_text_end[];
extern char __initdata_begin[], __initdata_end[];
extern char __inittext_begin[], __inittext_end[];
extern char __exittext_begin[], __exittext_end[];
extern char __irqentry_text_start[], __irqentry_text_end[];
extern char __mmuoff_data_start[], __mmuoff_data_end[];
extern char __entry_tramp_text_start[], __entry_tramp_text_end[];
extern char __relocate_new_kernel_start[], __relocate_new_kernel_end[];

static inline size_t entry_tramp_text_size(void)
{
	return __entry_tramp_text_end - __entry_tramp_text_start;
}

#endif /* __ASM_SECTIONS_H */
