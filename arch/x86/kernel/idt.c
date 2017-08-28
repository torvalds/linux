/*
 * Interrupt descriptor table related code
 *
 * This file is licensed under the GPL V2
 */
#include <linux/interrupt.h>

#include <asm/desc.h>

/* Must be page-aligned because the real IDT is used in a fixmap. */
gate_desc idt_table[IDT_ENTRIES] __page_aligned_bss;

struct desc_ptr idt_descr __ro_after_init = {
	.size		= (IDT_ENTRIES * 2 * sizeof(unsigned long)) - 1,
	.address	= (unsigned long) idt_table,
};

#ifdef CONFIG_X86_64
/* No need to be aligned, but done to keep all IDTs defined the same way. */
gate_desc debug_idt_table[IDT_ENTRIES] __page_aligned_bss;

const struct desc_ptr debug_idt_descr = {
	.size		= IDT_ENTRIES * 16 - 1,
	.address	= (unsigned long) debug_idt_table,
};
#endif
