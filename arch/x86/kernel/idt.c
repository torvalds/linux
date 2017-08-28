/*
 * Interrupt descriptor table related code
 *
 * This file is licensed under the GPL V2
 */
#include <linux/interrupt.h>

#include <asm/traps.h>
#include <asm/proto.h>
#include <asm/desc.h>

struct idt_data {
	unsigned int	vector;
	unsigned int	segment;
	struct idt_bits	bits;
	const void	*addr;
};

#define DPL0		0x0
#define DPL3		0x3

#define DEFAULT_STACK	0

#define G(_vector, _addr, _ist, _type, _dpl, _segment)	\
	{						\
		.vector		= _vector,		\
		.bits.ist	= _ist,			\
		.bits.type	= _type,		\
		.bits.dpl	= _dpl,			\
		.bits.p		= 1,			\
		.addr		= _addr,		\
		.segment	= _segment,		\
	}

/* Interrupt gate */
#define INTG(_vector, _addr)				\
	G(_vector, _addr, DEFAULT_STACK, GATE_INTERRUPT, DPL0, __KERNEL_CS)

/* System interrupt gate */
#define SYSG(_vector, _addr)				\
	G(_vector, _addr, DEFAULT_STACK, GATE_INTERRUPT, DPL3, __KERNEL_CS)

/* Interrupt gate with interrupt stack */
#define ISTG(_vector, _addr, _ist)			\
	G(_vector, _addr, _ist, GATE_INTERRUPT, DPL0, __KERNEL_CS)

/* Task gate */
#define TSKG(_vector, _gdt)				\
	G(_vector, NULL, DEFAULT_STACK, GATE_TASK, DPL0, _gdt << 3)

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

static inline void idt_init_desc(gate_desc *gate, const struct idt_data *d)
{
	unsigned long addr = (unsigned long) d->addr;

	gate->offset_low	= (u16) addr;
	gate->segment		= (u16) d->segment;
	gate->bits		= d->bits;
	gate->offset_middle	= (u16) (addr >> 16);
#ifdef CONFIG_X86_64
	gate->offset_high	= (u32) (addr >> 32);
	gate->reserved		= 0;
#endif
}

static __init void
idt_setup_from_table(gate_desc *idt, const struct idt_data *t, int size)
{
	gate_desc desc;

	for (; size > 0; t++, size--) {
		idt_init_desc(&desc, t);
		set_bit(t->vector, used_vectors);
		write_idt_entry(idt, t->vector, &desc);
	}
}

/**
 * idt_setup_early_handler - Initializes the idt table with early handlers
 */
void __init idt_setup_early_handler(void)
{
	int i;

	for (i = 0; i < NUM_EXCEPTION_VECTORS; i++)
		set_intr_gate(i, early_idt_handler_array[i]);
#ifdef CONFIG_X86_32
	for ( ; i < NR_VECTORS; i++)
		set_intr_gate(i, early_ignore_irq);
#endif
	load_idt(&idt_descr);
}

/**
 * idt_invalidate - Invalidate interrupt descriptor table
 * @addr:	The virtual address of the 'invalid' IDT
 */
void idt_invalidate(void *addr)
{
	struct desc_ptr idt = { .address = (unsigned long) addr, .size = 0 };

	load_idt(&idt);
}
