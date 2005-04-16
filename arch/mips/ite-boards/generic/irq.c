/*
 * BRIEF MODULE DESCRIPTION
 *	ITE 8172G interrupt/setup routines.
 *
 * Copyright 2000,2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * Part of this file was derived from Carsten Langgaard's
 * arch/mips/mips-boards/atlas/atlas_int.c.
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/serial_reg.h>
#include <linux/bitops.h>

#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/it8172/it8172.h>
#include <asm/it8172/it8172_int.h>
#include <asm/it8172/it8172_dbg.h>

/* revisit */
#define EXT_IRQ0_TO_IP 2 /* IP 2 */
#define EXT_IRQ5_TO_IP 7 /* IP 7 */

#define ALLINTS_NOTIMER (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4)

void disable_it8172_irq(unsigned int irq_nr);
void enable_it8172_irq(unsigned int irq_nr);

extern void set_debug_traps(void);
extern void mips_timer_interrupt(int irq, struct pt_regs *regs);
extern asmlinkage void it8172_IRQ(void);

struct it8172_intc_regs volatile *it8172_hw0_icregs =
	(struct it8172_intc_regs volatile *)(KSEG1ADDR(IT8172_PCI_IO_BASE + IT_INTC_BASE));

static void disable_it8172_irq(unsigned int irq_nr)
{
	if ( (irq_nr >= IT8172_LPC_IRQ_BASE) && (irq_nr <= IT8172_SERIRQ_15)) {
		/* LPC interrupt */
		it8172_hw0_icregs->lpc_mask |=
			(1 << (irq_nr - IT8172_LPC_IRQ_BASE));
	} else if ( (irq_nr >= IT8172_LB_IRQ_BASE) && (irq_nr <= IT8172_IOCHK_IRQ)) {
		/* Local Bus interrupt */
		it8172_hw0_icregs->lb_mask |=
			(1 << (irq_nr - IT8172_LB_IRQ_BASE));
	} else if ( (irq_nr >= IT8172_PCI_DEV_IRQ_BASE) && (irq_nr <= IT8172_DMA_IRQ)) {
		/* PCI and other interrupts */
		it8172_hw0_icregs->pci_mask |=
			(1 << (irq_nr - IT8172_PCI_DEV_IRQ_BASE));
	} else if ( (irq_nr >= IT8172_NMI_IRQ_BASE) && (irq_nr <= IT8172_POWER_NMI_IRQ)) {
		/* NMI interrupts */
		it8172_hw0_icregs->nmi_mask |=
			(1 << (irq_nr - IT8172_NMI_IRQ_BASE));
	} else {
		panic("disable_it8172_irq: bad irq %d", irq_nr);
	}
}

static void enable_it8172_irq(unsigned int irq_nr)
{
	if ( (irq_nr >= IT8172_LPC_IRQ_BASE) && (irq_nr <= IT8172_SERIRQ_15)) {
		/* LPC interrupt */
		it8172_hw0_icregs->lpc_mask &=
			~(1 << (irq_nr - IT8172_LPC_IRQ_BASE));
	}
	else if ( (irq_nr >= IT8172_LB_IRQ_BASE) && (irq_nr <= IT8172_IOCHK_IRQ)) {
		/* Local Bus interrupt */
		it8172_hw0_icregs->lb_mask &=
			~(1 << (irq_nr - IT8172_LB_IRQ_BASE));
	}
	else if ( (irq_nr >= IT8172_PCI_DEV_IRQ_BASE) && (irq_nr <= IT8172_DMA_IRQ)) {
		/* PCI and other interrupts */
		it8172_hw0_icregs->pci_mask &=
			~(1 << (irq_nr - IT8172_PCI_DEV_IRQ_BASE));
	}
	else if ( (irq_nr >= IT8172_NMI_IRQ_BASE) && (irq_nr <= IT8172_POWER_NMI_IRQ)) {
		/* NMI interrupts */
		it8172_hw0_icregs->nmi_mask &=
			~(1 << (irq_nr - IT8172_NMI_IRQ_BASE));
	}
	else {
		panic("enable_it8172_irq: bad irq %d", irq_nr);
	}
}

static unsigned int startup_ite_irq(unsigned int irq)
{
	enable_it8172_irq(irq);
	return 0;
}

#define shutdown_ite_irq	disable_it8172_irq
#define mask_and_ack_ite_irq    disable_it8172_irq

static void end_ite_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_it8172_irq(irq);
}

static struct hw_interrupt_type it8172_irq_type = {
	"ITE8172",
	startup_ite_irq,
	shutdown_ite_irq,
	enable_it8172_irq,
	disable_it8172_irq,
	mask_and_ack_ite_irq,
	end_ite_irq,
	NULL
};


static void enable_none(unsigned int irq) { }
static unsigned int startup_none(unsigned int irq) { return 0; }
static void disable_none(unsigned int irq) { }
static void ack_none(unsigned int irq) { }

/* startup is the same as "enable", shutdown is same as "disable" */
#define shutdown_none	disable_none
#define end_none	enable_none

static struct hw_interrupt_type cp0_irq_type = {
	"CP0 Count",
	startup_none,
	shutdown_none,
	enable_none,
	disable_none,
	ack_none,
	end_none
};

void enable_cpu_timer(void)
{
        unsigned long flags;

        local_irq_save(flags);
	set_c0_status(0x100 << EXT_IRQ5_TO_IP);
        local_irq_restore(flags);
}

void __init arch_init_irq(void)
{
	int i;
        unsigned long flags;

        memset(irq_desc, 0, sizeof(irq_desc));
        set_except_vector(0, it8172_IRQ);

	/* mask all interrupts */
	it8172_hw0_icregs->lb_mask  = 0xffff;
	it8172_hw0_icregs->lpc_mask = 0xffff;
	it8172_hw0_icregs->pci_mask = 0xffff;
	it8172_hw0_icregs->nmi_mask = 0xffff;

	/* make all interrupts level triggered */
	it8172_hw0_icregs->lb_trigger  = 0;
	it8172_hw0_icregs->lpc_trigger = 0;
	it8172_hw0_icregs->pci_trigger = 0;
	it8172_hw0_icregs->nmi_trigger = 0;

	/* active level setting */
	/* uart, keyboard, and mouse are active high */
	it8172_hw0_icregs->lpc_level = (0x10 | 0x2 | 0x1000);
	it8172_hw0_icregs->lb_level |= 0x20;

	/* keyboard and mouse are edge triggered */
	it8172_hw0_icregs->lpc_trigger |= (0x2 | 0x1000);


#if 0
	// Enable this piece of code to make internal USB interrupt
	// edge triggered.
	it8172_hw0_icregs->pci_trigger |=
		(1 << (IT8172_USB_IRQ - IT8172_PCI_DEV_IRQ_BASE));
	it8172_hw0_icregs->pci_level &=
		~(1 << (IT8172_USB_IRQ - IT8172_PCI_DEV_IRQ_BASE));
#endif

	for (i = 0; i <= IT8172_LAST_IRQ; i++) {
		irq_desc[i].handler = &it8172_irq_type;
		spin_lock_init(&irq_desc[i].lock);
	}
	irq_desc[MIPS_CPU_TIMER_IRQ].handler = &cp0_irq_type;
	set_c0_status(ALLINTS_NOTIMER);
}

void mips_spurious_interrupt(struct pt_regs *regs)
{
#if 1
	return;
#else
	unsigned long status, cause;

	printk("got spurious interrupt\n");
	status = read_c0_status();
	cause = read_c0_cause();
	printk("status %x cause %x\n", status, cause);
	printk("epc %x badvaddr %x \n", regs->cp0_epc, regs->cp0_badvaddr);
#endif
}

void it8172_hw0_irqdispatch(struct pt_regs *regs)
{
	int irq;
	unsigned short intstatus = 0, status = 0;

	intstatus = it8172_hw0_icregs->intstatus;
	if (intstatus & 0x8) {
		panic("Got NMI interrupt");
	} else if (intstatus & 0x4) {
		/* PCI interrupt */
		irq = 0;
		status |= it8172_hw0_icregs->pci_req;
		while (!(status & 0x1)) {
			irq++;
			status >>= 1;
		}
		irq += IT8172_PCI_DEV_IRQ_BASE;
	} else if (intstatus & 0x1) {
		/* Local Bus interrupt */
		irq = 0;
		status |= it8172_hw0_icregs->lb_req;
		while (!(status & 0x1)) {
			irq++;
			status >>= 1;
		}
		irq += IT8172_LB_IRQ_BASE;
	} else if (intstatus & 0x2) {
		/* LPC interrupt */
		/* Since some lpc interrupts are edge triggered,
		 * we could lose an interrupt this way because
		 * we acknowledge all ints at onces. Revisit.
		 */
		status |= it8172_hw0_icregs->lpc_req;
		it8172_hw0_icregs->lpc_req = 0; /* acknowledge ints */
		irq = 0;
		while (!(status & 0x1)) {
			irq++;
			status >>= 1;
		}
		irq += IT8172_LPC_IRQ_BASE;
	} else
		return;

	do_IRQ(irq, regs);
}

void show_pending_irqs(void)
{
	fputs("intstatus:  ");
	put32(it8172_hw0_icregs->intstatus);
	puts("");

	fputs("pci_req:  ");
	put32(it8172_hw0_icregs->pci_req);
	puts("");

	fputs("lb_req:  ");
	put32(it8172_hw0_icregs->lb_req);
	puts("");

	fputs("lpc_req:  ");
	put32(it8172_hw0_icregs->lpc_req);
	puts("");
}
