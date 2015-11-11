/*
 * linux/arch/h8300/kernel/irq.c
 *
 * Copyright 2014-2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <asm/traps.h>

#ifdef CONFIG_RAMKERNEL
typedef void (*h8300_vector)(void);

static const h8300_vector __initconst trap_table[] = {
	0, 0, 0, 0,
	_trace_break,
	0, 0,
	_nmi,
	_system_call,
	0, 0,
	_trace_break,
};

static unsigned long __init *get_vector_address(void)
{
	unsigned long *rom_vector = CPU_VECTOR;
	unsigned long base, tmp;
	int vec_no;

	base = rom_vector[EXT_IRQ0] & ADDR_MASK;

	/* check romvector format */
	for (vec_no = EXT_IRQ0 + 1; vec_no <= EXT_IRQ0+EXT_IRQS; vec_no++) {
		if ((base+(vec_no - EXT_IRQ0)*4) !=
		    (rom_vector[vec_no] & ADDR_MASK))
			return NULL;
	}

	/* ramvector base address */
	base -= EXT_IRQ0*4;

	/* writerble? */
	tmp = ~(*(volatile unsigned long *)base);
	(*(volatile unsigned long *)base) = tmp;
	if ((*(volatile unsigned long *)base) != tmp)
		return NULL;
	return (unsigned long *)base;
}

static void __init setup_vector(void)
{
	int i;
	unsigned long *ramvec, *ramvec_p;
	const h8300_vector *trap_entry;

	ramvec = get_vector_address();
	if (ramvec == NULL)
		panic("interrupt vector serup failed.");
	else
		pr_debug("virtual vector at 0x%p\n", ramvec);

	/* create redirect table */
	ramvec_p = ramvec;
	trap_entry = trap_table;
	for (i = 0; i < NR_IRQS; i++) {
		if (i < 12) {
			if (*trap_entry)
				*ramvec_p = VECTOR(*trap_entry);
			ramvec_p++;
			trap_entry++;
		} else
			*ramvec_p++ = REDIRECT(_interrupt_entry);
	}
	_interrupt_redirect_table = ramvec;
}
#else
void setup_vector(void)
{
	/* noting do */
}
#endif

void __init init_IRQ(void)
{
	setup_vector();
	irqchip_init();
}

asmlinkage void do_IRQ(int irq)
{
	irq_enter();
	generic_handle_irq(irq);
	irq_exit();
}
