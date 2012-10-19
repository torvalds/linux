/*
 * Mostly IRQ support for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/kernel.h>
#include <asm/registers.h>
#include <linux/irq.h>
#include <linux/hardirq.h>

/*
 * show_regs - print pt_regs structure
 * @regs: pointer to pt_regs
 *
 * To-do:  add all the accessor definitions to registers.h
 *
 * Will make this routine a lot easier to write.
 */
void show_regs(struct pt_regs *regs)
{
	printk(KERN_EMERG "restart_r0: \t0x%08lx   syscall_nr: %ld\n",
	       regs->restart_r0, regs->syscall_nr);
	printk(KERN_EMERG "preds: \t\t0x%08lx\n", regs->preds);
	printk(KERN_EMERG "lc0: \t0x%08lx   sa0: 0x%08lx   m0:  0x%08lx\n",
	       regs->lc0, regs->sa0, regs->m0);
	printk(KERN_EMERG "lc1: \t0x%08lx   sa1: 0x%08lx   m1:  0x%08lx\n",
	       regs->lc1, regs->sa1, regs->m1);
	printk(KERN_EMERG "gp: \t0x%08lx   ugp: 0x%08lx   usr: 0x%08lx\n",
	       regs->gp, regs->ugp, regs->usr);
	printk(KERN_EMERG "r0: \t0x%08lx %08lx %08lx %08lx\n", regs->r00,
		regs->r01,
		regs->r02,
		regs->r03);
	printk(KERN_EMERG "r4:  \t0x%08lx %08lx %08lx %08lx\n", regs->r04,
		regs->r05,
		regs->r06,
		regs->r07);
	printk(KERN_EMERG "r8:  \t0x%08lx %08lx %08lx %08lx\n", regs->r08,
		regs->r09,
		regs->r10,
		regs->r11);
	printk(KERN_EMERG "r12: \t0x%08lx %08lx %08lx %08lx\n", regs->r12,
		regs->r13,
		regs->r14,
		regs->r15);
	printk(KERN_EMERG "r16: \t0x%08lx %08lx %08lx %08lx\n", regs->r16,
		regs->r17,
		regs->r18,
		regs->r19);
	printk(KERN_EMERG "r20: \t0x%08lx %08lx %08lx %08lx\n", regs->r20,
		regs->r21,
		regs->r22,
		regs->r23);
	printk(KERN_EMERG "r24: \t0x%08lx %08lx %08lx %08lx\n", regs->r24,
		regs->r25,
		regs->r26,
		regs->r27);
	printk(KERN_EMERG "r28: \t0x%08lx %08lx %08lx %08lx\n", regs->r28,
		regs->r29,
		regs->r30,
		regs->r31);

	printk(KERN_EMERG "elr: \t0x%08lx   cause: 0x%08lx   user_mode: %d\n",
		pt_elr(regs), pt_cause(regs), user_mode(regs));
	printk(KERN_EMERG "psp: \t0x%08lx   badva: 0x%08lx   int_enabled: %d\n",
		pt_psp(regs), pt_badva(regs), ints_enabled(regs));
}

void dummy_handler(struct pt_regs *regs)
{
	unsigned int elr = pt_elr(regs);
	printk(KERN_ERR "Unimplemented handler; ELR=0x%08x\n", elr);
}


void arch_do_IRQ(struct pt_regs *regs)
{
	int irq = pt_cause(regs);
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();
	generic_handle_irq(irq);
	irq_exit();
	set_irq_regs(old_regs);
}
