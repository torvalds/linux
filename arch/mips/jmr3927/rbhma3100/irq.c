/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              ahennessy@mvista.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/config.h>
#include <linux/init.h>

#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/jmr3927/irq.h>
#include <asm/debug.h>
#include <asm/jmr3927/jmr3927.h>

#if JMR3927_IRQ_END > NR_IRQS
#error JMR3927_IRQ_END > NR_IRQS
#endif

struct tb_irq_space* tb_irq_spaces;

static int jmr3927_irq_base = -1;

#ifdef CONFIG_PCI
static int jmr3927_gen_iack(void)
{
	/* generate ACK cycle */
#ifdef __BIG_ENDIAN
	return (tx3927_pcicptr->iiadp >> 24) & 0xff;
#else
	return tx3927_pcicptr->iiadp & 0xff;
#endif
}
#endif

#define irc_dlevel	0
#define irc_elevel	1

static unsigned char irc_level[TX3927_NUM_IR] = {
	5, 5, 5, 5, 5, 5,	/* INT[5:0] */
	7, 7,			/* SIO */
	5, 5, 5, 0, 0,		/* DMA, PIO, PCI */
	6, 6, 6			/* TMR */
};

static void jmr3927_irq_disable(unsigned int irq_nr);
static void jmr3927_irq_enable(unsigned int irq_nr);

static DEFINE_SPINLOCK(jmr3927_irq_lock);

static unsigned int jmr3927_irq_startup(unsigned int irq)
{
	jmr3927_irq_enable(irq);

	return 0;
}

#define	jmr3927_irq_shutdown	jmr3927_irq_disable

static void jmr3927_irq_ack(unsigned int irq)
{
	if (irq == JMR3927_IRQ_IRC_TMR0)
		jmr3927_tmrptr->tisr = 0;       /* ack interrupt */

	jmr3927_irq_disable(irq);
}

static void jmr3927_irq_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		jmr3927_irq_enable(irq);
}

static void jmr3927_irq_disable(unsigned int irq_nr)
{
	struct tb_irq_space* sp;
	unsigned long flags;

	spin_lock_irqsave(&jmr3927_irq_lock, flags);
	for (sp = tb_irq_spaces; sp; sp = sp->next) {
		if (sp->start_irqno <= irq_nr &&
		    irq_nr < sp->start_irqno + sp->nr_irqs) {
			if (sp->mask_func)
				sp->mask_func(irq_nr - sp->start_irqno,
					      sp->space_id);
			break;
		}
	}
	spin_unlock_irqrestore(&jmr3927_irq_lock, flags);
}

static void jmr3927_irq_enable(unsigned int irq_nr)
{
	struct tb_irq_space* sp;
	unsigned long flags;

	spin_lock_irqsave(&jmr3927_irq_lock, flags);
	for (sp = tb_irq_spaces; sp; sp = sp->next) {
		if (sp->start_irqno <= irq_nr &&
		    irq_nr < sp->start_irqno + sp->nr_irqs) {
			if (sp->unmask_func)
				sp->unmask_func(irq_nr - sp->start_irqno,
						sp->space_id);
			break;
		}
	}
	spin_unlock_irqrestore(&jmr3927_irq_lock, flags);
}

/*
 * CP0_STATUS is a thread's resource (saved/restored on context switch).
 * So disable_irq/enable_irq MUST handle IOC/ISAC/IRC registers.
 */
static void mask_irq_isac(int irq_nr, int space_id)
{
	/* 0: mask */
	unsigned char imask =
		jmr3927_isac_reg_in(JMR3927_ISAC_INTM_ADDR);
	unsigned int bit  = 1 << irq_nr;
	jmr3927_isac_reg_out(imask & ~bit, JMR3927_ISAC_INTM_ADDR);
	/* flush write buffer */
	(void)jmr3927_ioc_reg_in(JMR3927_IOC_REV_ADDR);
}
static void unmask_irq_isac(int irq_nr, int space_id)
{
	/* 0: mask */
	unsigned char imask = jmr3927_isac_reg_in(JMR3927_ISAC_INTM_ADDR);
	unsigned int bit  = 1 << irq_nr;
	jmr3927_isac_reg_out(imask | bit, JMR3927_ISAC_INTM_ADDR);
	/* flush write buffer */
	(void)jmr3927_ioc_reg_in(JMR3927_IOC_REV_ADDR);
}

static void mask_irq_ioc(int irq_nr, int space_id)
{
	/* 0: mask */
	unsigned char imask = jmr3927_ioc_reg_in(JMR3927_IOC_INTM_ADDR);
	unsigned int bit = 1 << irq_nr;
	jmr3927_ioc_reg_out(imask & ~bit, JMR3927_IOC_INTM_ADDR);
	/* flush write buffer */
	(void)jmr3927_ioc_reg_in(JMR3927_IOC_REV_ADDR);
}
static void unmask_irq_ioc(int irq_nr, int space_id)
{
	/* 0: mask */
	unsigned char imask = jmr3927_ioc_reg_in(JMR3927_IOC_INTM_ADDR);
	unsigned int bit = 1 << irq_nr;
	jmr3927_ioc_reg_out(imask | bit, JMR3927_IOC_INTM_ADDR);
	/* flush write buffer */
	(void)jmr3927_ioc_reg_in(JMR3927_IOC_REV_ADDR);
}

static void mask_irq_irc(int irq_nr, int space_id)
{
	volatile unsigned long *ilrp = &tx3927_ircptr->ilr[irq_nr / 2];
	if (irq_nr & 1)
		*ilrp = (*ilrp & 0x00ff) | (irc_dlevel << 8);
	else
		*ilrp = (*ilrp & 0xff00) | irc_dlevel;
	/* update IRCSR */
	tx3927_ircptr->imr = 0;
	tx3927_ircptr->imr = irc_elevel;
	/* flush write buffer */
	(void)tx3927_ircptr->ssr;
}

static void unmask_irq_irc(int irq_nr, int space_id)
{
	volatile unsigned long *ilrp = &tx3927_ircptr->ilr[irq_nr / 2];
	if (irq_nr & 1)
		*ilrp = (*ilrp & 0x00ff) | (irc_level[irq_nr] << 8);
	else
		*ilrp = (*ilrp & 0xff00) | irc_level[irq_nr];
	/* update IRCSR */
	tx3927_ircptr->imr = 0;
	tx3927_ircptr->imr = irc_elevel;
}

struct tb_irq_space jmr3927_isac_irqspace = {
	.next = NULL,
	.start_irqno = JMR3927_IRQ_ISAC,
	nr_irqs : JMR3927_NR_IRQ_ISAC,
	.mask_func = mask_irq_isac,
	.unmask_func = unmask_irq_isac,
	.name = "ISAC",
	.space_id = 0,
	can_share : 0
};
struct tb_irq_space jmr3927_ioc_irqspace = {
	.next = NULL,
	.start_irqno = JMR3927_IRQ_IOC,
	nr_irqs : JMR3927_NR_IRQ_IOC,
	.mask_func = mask_irq_ioc,
	.unmask_func = unmask_irq_ioc,
	.name = "IOC",
	.space_id = 0,
	can_share : 1
};
struct tb_irq_space jmr3927_irc_irqspace = {
	.next = NULL,
	.start_irqno = JMR3927_IRQ_IRC,
	nr_irqs : JMR3927_NR_IRQ_IRC,
	.mask_func = mask_irq_irc,
	.unmask_func = unmask_irq_irc,
	.name = "on-chip",
	.space_id = 0,
	can_share : 0
};

void jmr3927_spurious(struct pt_regs *regs)
{
#ifdef CONFIG_TX_BRANCH_LIKELY_BUG_WORKAROUND
	tx_branch_likely_bug_fixup(regs);
#endif
	printk(KERN_WARNING "spurious interrupt (cause 0x%lx, pc 0x%lx, ra 0x%lx).\n",
	       regs->cp0_cause, regs->cp0_epc, regs->regs[31]);
}

asmlinkage void plat_irq_dispatch(struct pt_regs *regs)
{
	int irq;

#ifdef CONFIG_TX_BRANCH_LIKELY_BUG_WORKAROUND
	tx_branch_likely_bug_fixup(regs);
#endif
	if ((regs->cp0_cause & CAUSEF_IP7) == 0) {
#if 0
		jmr3927_spurious(regs);
#endif
		return;
	}
	irq = (regs->cp0_cause >> CAUSEB_IP2) & 0x0f;

	do_IRQ(irq + JMR3927_IRQ_IRC, regs);
}

static irqreturn_t jmr3927_ioc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned char istat = jmr3927_ioc_reg_in(JMR3927_IOC_INTS2_ADDR);
	int i;

	for (i = 0; i < JMR3927_NR_IRQ_IOC; i++) {
		if (istat & (1 << i)) {
			irq = JMR3927_IRQ_IOC + i;
			do_IRQ(irq, regs);
		}
	}
	return IRQ_HANDLED;
}

static struct irqaction ioc_action = {
	jmr3927_ioc_interrupt, 0, CPU_MASK_NONE, "IOC", NULL, NULL,
};

static irqreturn_t jmr3927_isac_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned char istat = jmr3927_isac_reg_in(JMR3927_ISAC_INTS2_ADDR);
	int i;

	for (i = 0; i < JMR3927_NR_IRQ_ISAC; i++) {
		if (istat & (1 << i)) {
			irq = JMR3927_IRQ_ISAC + i;
			do_IRQ(irq, regs);
		}
	}
	return IRQ_HANDLED;
}

static struct irqaction isac_action = {
	jmr3927_isac_interrupt, 0, CPU_MASK_NONE, "ISAC", NULL, NULL,
};


static irqreturn_t jmr3927_isaerr_interrupt(int irq, void * dev_id, struct pt_regs * regs)
{
	printk(KERN_WARNING "ISA error interrupt (irq 0x%x).\n", irq);

	return IRQ_HANDLED;
}
static struct irqaction isaerr_action = {
	jmr3927_isaerr_interrupt, 0, CPU_MASK_NONE, "ISA error", NULL, NULL,
};

static irqreturn_t jmr3927_pcierr_interrupt(int irq, void * dev_id, struct pt_regs * regs)
{
	printk(KERN_WARNING "PCI error interrupt (irq 0x%x).\n", irq);
	printk(KERN_WARNING "pcistat:%02x, lbstat:%04lx\n",
	       tx3927_pcicptr->pcistat, tx3927_pcicptr->lbstat);

	return IRQ_HANDLED;
}
static struct irqaction pcierr_action = {
	jmr3927_pcierr_interrupt, 0, CPU_MASK_NONE, "PCI error", NULL, NULL,
};

int jmr3927_ether1_irq = 0;

void jmr3927_irq_init(u32 irq_base);

void __init arch_init_irq(void)
{
	/* look for io board's presence */
	int have_isac = jmr3927_have_isac();

	/* Now, interrupt control disabled, */
	/* all IRC interrupts are masked, */
	/* all IRC interrupt mode are Low Active. */

	if (have_isac) {

		/* ETHER1 (NE2000 compatible 10M-Ether) parameter setup */
		/* temporary enable interrupt control */
		tx3927_ircptr->cer = 1;
		/* ETHER1 Int. Is High-Active. */
		if (tx3927_ircptr->ssr & (1 << 0))
			jmr3927_ether1_irq = JMR3927_IRQ_IRC_INT0;
#if 0	/* INT3 may be asserted by ether0 (even after reboot...) */
		else if (tx3927_ircptr->ssr & (1 << 3))
			jmr3927_ether1_irq = JMR3927_IRQ_IRC_INT3;
#endif
		/* disable interrupt control */
		tx3927_ircptr->cer = 0;

		/* Ether1: High Active */
		if (jmr3927_ether1_irq) {
			int ether1_irc = jmr3927_ether1_irq - JMR3927_IRQ_IRC;
			tx3927_ircptr->cr[ether1_irc / 8] |=
				TX3927_IRCR_HIGH << ((ether1_irc % 8) * 2);
		}
	}

	/* mask all IOC interrupts */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_INTM_ADDR);
	/* setup IOC interrupt mode (SOFT:High Active, Others:Low Active) */
	jmr3927_ioc_reg_out(JMR3927_IOC_INTF_SOFT, JMR3927_IOC_INTP_ADDR);

	if (have_isac) {
		/* mask all ISAC interrupts */
		jmr3927_isac_reg_out(0, JMR3927_ISAC_INTM_ADDR);
		/* setup ISAC interrupt mode (ISAIRQ3,ISAIRQ5:Low Active ???) */
		jmr3927_isac_reg_out(JMR3927_ISAC_INTF_IRQ3|JMR3927_ISAC_INTF_IRQ5, JMR3927_ISAC_INTP_ADDR);
	}

	/* clear PCI Soft interrupts */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_INTS1_ADDR);
	/* clear PCI Reset interrupts */
	jmr3927_ioc_reg_out(0, JMR3927_IOC_RESET_ADDR);

	/* enable interrupt control */
	tx3927_ircptr->cer = TX3927_IRCER_ICE;
	tx3927_ircptr->imr = irc_elevel;

	jmr3927_irq_init(NR_ISA_IRQS);

	/* setup irq space */
	add_tb_irq_space(&jmr3927_isac_irqspace);
	add_tb_irq_space(&jmr3927_ioc_irqspace);
	add_tb_irq_space(&jmr3927_irc_irqspace);

	/* setup IOC interrupt 1 (PCI, MODEM) */
	setup_irq(JMR3927_IRQ_IOCINT, &ioc_action);

	if (have_isac) {
		setup_irq(JMR3927_IRQ_ISACINT, &isac_action);
		setup_irq(JMR3927_IRQ_ISAC_ISAER, &isaerr_action);
	}

#ifdef CONFIG_PCI
	setup_irq(JMR3927_IRQ_IRC_PCI, &pcierr_action);
#endif

	/* enable all CPU interrupt bits. */
	set_c0_status(ST0_IM);	/* IE bit is still 0. */
}

static hw_irq_controller jmr3927_irq_controller = {
	.typename = "jmr3927_irq",
	.startup = jmr3927_irq_startup,
	.shutdown = jmr3927_irq_shutdown,
	.enable = jmr3927_irq_enable,
	.disable = jmr3927_irq_disable,
	.ack = jmr3927_irq_ack,
	.end = jmr3927_irq_end,
};

void jmr3927_irq_init(u32 irq_base)
{
	u32 i;

	for (i= irq_base; i< irq_base + JMR3927_NR_IRQ_IRC + JMR3927_NR_IRQ_IOC; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].handler = &jmr3927_irq_controller;
	}

	jmr3927_irq_base = irq_base;
}

#ifdef CONFIG_TX_BRANCH_LIKELY_BUG_WORKAROUND
static int tx_branch_likely_bug_count = 0;
static int have_tx_branch_likely_bug = 0;
void tx_branch_likely_bug_fixup(struct pt_regs *regs)
{
	/* TX39/49-BUG: Under this condition, the insn in delay slot
           of the branch likely insn is executed (not nullified) even
           the branch condition is false. */
	if (!have_tx_branch_likely_bug)
		return;
	if ((regs->cp0_epc & 0xfff) == 0xffc &&
	    KSEGX(regs->cp0_epc) != KSEG0 &&
	    KSEGX(regs->cp0_epc) != KSEG1) {
		unsigned int insn = *(unsigned int*)(regs->cp0_epc - 4);
		/* beql,bnel,blezl,bgtzl */
		/* bltzl,bgezl,blezall,bgezall */
		/* bczfl, bcztl */
		if ((insn & 0xf0000000) == 0x50000000 ||
		    (insn & 0xfc0e0000) == 0x04020000 ||
		    (insn & 0xf3fe0000) == 0x41020000) {
			regs->cp0_epc -= 4;
			tx_branch_likely_bug_count++;
			printk(KERN_INFO
			       "fix branch-likery bug in %s (insn %08x)\n",
			       current->comm, insn);
		}
	}
}
#endif
