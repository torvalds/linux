/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SECTIONS_H
#define _ASM_X86_SECTIONS_H

#include <asm-generic/sections.h>
#include <asm/extable.h>

extern char __brk_base[], __brk_limit[];
extern struct exception_table_entry __stop___ex_table[];
extern char __end_rodata_aligned[];

#if defined(CONFIG_X86_64)
extern char __end_rodata_hpage_align[];
extern char __entry_trampoline_start[], __entry_trampoline_end[];
#endif

#endif	/* _ASM_X86_SECTIONS_H */
