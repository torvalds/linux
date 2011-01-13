/*
 * arch/score/kernel/irq.c
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>

#include <asm/io.h>

/* the interrupt controller is hardcoded at this address */
#define SCORE_PIC		((u32 __iomem __force *)0x95F50000)

#define INT_PNDL		0
#define INT_PNDH		1
#define INT_PRIORITY_M		2
#define INT_PRIORITY_SG0	4
#define INT_PRIORITY_SG1	5
#define INT_PRIORITY_SG2	6
#define INT_PRIORITY_SG3	7
#define INT_MASKL		8
#define INT_MASKH		9

/*
 * handles all normal device IRQs
 */
asmlinkage void do_IRQ(int irq)
{
	irq_enter();
	generic_handle_irq(irq);
	irq_exit();
}

static void score_mask(unsigned int irq_nr)
{
	unsigned int irq_source = 63 - irq_nr;

	if (irq_source < 32)
		__raw_writel((__raw_readl(SCORE_PIC + INT_MASKL) | \
			(1 << irq_source)), SCORE_PIC + INT_MASKL);
	else
		__raw_writel((__raw_readl(SCORE_PIC + INT_MASKH) | \
			(1 << (irq_source - 32))), SCORE_PIC + INT_MASKH);
}

static void score_unmask(unsigned int irq_nr)
{
	unsigned int irq_source = 63 - irq_nr;

	if (irq_source < 32)
		__raw_writel((__raw_readl(SCORE_PIC + INT_MASKL) & \
			~(1 << irq_source)), SCORE_PIC + INT_MASKL);
	else
		__raw_writel((__raw_readl(SCORE_PIC + INT_MASKH) & \
			~(1 << (irq_source - 32))), SCORE_PIC + INT_MASKH);
}

struct irq_chip score_irq_chip = {
	.name		= "Score7-level",
	.mask		= score_mask,
	.mask_ack	= score_mask,
	.unmask		= score_unmask,
};

/*
 * initialise the interrupt system
 */
void __init init_IRQ(void)
{
	int index;
	unsigned long target_addr;

	for (index = 0; index < NR_IRQS; ++index)
		set_irq_chip_and_handler(index, &score_irq_chip,
					 handle_level_irq);

	for (target_addr = IRQ_VECTOR_BASE_ADDR;
		target_addr <= IRQ_VECTOR_END_ADDR;
		target_addr += IRQ_VECTOR_SIZE)
		memcpy((void *)target_addr, \
			interrupt_exception_vector, IRQ_VECTOR_SIZE);

	__raw_writel(0xffffffff, SCORE_PIC + INT_MASKL);
	__raw_writel(0xffffffff, SCORE_PIC + INT_MASKH);

	__asm__ __volatile__(
		"mtcr	%0, cr3\n\t"
		: : "r" (EXCEPTION_VECTOR_BASE_ADDR | \
			VECTOR_ADDRESS_OFFSET_MODE16));
}

/*
 * Generic, controller-independent functions:
 */
int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *)v, cpu;
	struct irqaction *action;
	unsigned long flags;

	if (i == 0) {
		seq_puts(p, "           ");
		for_each_online_cpu(cpu)
			seq_printf(p, "CPU%d       ", cpu);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto unlock;

		seq_printf(p, "%3d: ", i);
		seq_printf(p, "%10u ", kstat_irqs(i));
		seq_printf(p, " %8s", irq_desc[i].chip->name ? : "-");
		seq_printf(p, "  %s", action->name);
		for (action = action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
unlock:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	}

	return 0;
}
