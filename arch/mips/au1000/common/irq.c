/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *		ppopov@mvista.com or source@mvista.com
 *
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/mach-au1x00/au1000.h>
#ifdef CONFIG_MIPS_PB1000
#include <asm/mach-pb1x00/pb1000.h>
#endif

#define EXT_INTC0_REQ0 2 /* IP 2 */
#define EXT_INTC0_REQ1 3 /* IP 3 */
#define EXT_INTC1_REQ0 4 /* IP 4 */
#define EXT_INTC1_REQ1 5 /* IP 5 */
#define MIPS_TIMER_IP  7 /* IP 7 */

void (*board_init_irq)(void) __initdata = NULL;

static DEFINE_SPINLOCK(irq_lock);

#ifdef CONFIG_PM

/*
 * Save/restore the interrupt controller state.
 * Called from the save/restore core registers as part of the
 * au_sleep function in power.c.....maybe I should just pm_register()
 * them instead?
 */
static unsigned int	sleep_intctl_config0[2];
static unsigned int	sleep_intctl_config1[2];
static unsigned int	sleep_intctl_config2[2];
static unsigned int	sleep_intctl_src[2];
static unsigned int	sleep_intctl_assign[2];
static unsigned int	sleep_intctl_wake[2];
static unsigned int	sleep_intctl_mask[2];

void save_au1xxx_intctl(void)
{
	sleep_intctl_config0[0] = au_readl(IC0_CFG0RD);
	sleep_intctl_config1[0] = au_readl(IC0_CFG1RD);
	sleep_intctl_config2[0] = au_readl(IC0_CFG2RD);
	sleep_intctl_src[0] = au_readl(IC0_SRCRD);
	sleep_intctl_assign[0] = au_readl(IC0_ASSIGNRD);
	sleep_intctl_wake[0] = au_readl(IC0_WAKERD);
	sleep_intctl_mask[0] = au_readl(IC0_MASKRD);

	sleep_intctl_config0[1] = au_readl(IC1_CFG0RD);
	sleep_intctl_config1[1] = au_readl(IC1_CFG1RD);
	sleep_intctl_config2[1] = au_readl(IC1_CFG2RD);
	sleep_intctl_src[1] = au_readl(IC1_SRCRD);
	sleep_intctl_assign[1] = au_readl(IC1_ASSIGNRD);
	sleep_intctl_wake[1] = au_readl(IC1_WAKERD);
	sleep_intctl_mask[1] = au_readl(IC1_MASKRD);
}

/*
 * For most restore operations, we clear the entire register and
 * then set the bits we found during the save.
 */
void restore_au1xxx_intctl(void)
{
	au_writel(0xffffffff, IC0_MASKCLR); au_sync();

	au_writel(0xffffffff, IC0_CFG0CLR); au_sync();
	au_writel(sleep_intctl_config0[0], IC0_CFG0SET); au_sync();
	au_writel(0xffffffff, IC0_CFG1CLR); au_sync();
	au_writel(sleep_intctl_config1[0], IC0_CFG1SET); au_sync();
	au_writel(0xffffffff, IC0_CFG2CLR); au_sync();
	au_writel(sleep_intctl_config2[0], IC0_CFG2SET); au_sync();
	au_writel(0xffffffff, IC0_SRCCLR); au_sync();
	au_writel(sleep_intctl_src[0], IC0_SRCSET); au_sync();
	au_writel(0xffffffff, IC0_ASSIGNCLR); au_sync();
	au_writel(sleep_intctl_assign[0], IC0_ASSIGNSET); au_sync();
	au_writel(0xffffffff, IC0_WAKECLR); au_sync();
	au_writel(sleep_intctl_wake[0], IC0_WAKESET); au_sync();
	au_writel(0xffffffff, IC0_RISINGCLR); au_sync();
	au_writel(0xffffffff, IC0_FALLINGCLR); au_sync();
	au_writel(0x00000000, IC0_TESTBIT); au_sync();

	au_writel(0xffffffff, IC1_MASKCLR); au_sync();

	au_writel(0xffffffff, IC1_CFG0CLR); au_sync();
	au_writel(sleep_intctl_config0[1], IC1_CFG0SET); au_sync();
	au_writel(0xffffffff, IC1_CFG1CLR); au_sync();
	au_writel(sleep_intctl_config1[1], IC1_CFG1SET); au_sync();
	au_writel(0xffffffff, IC1_CFG2CLR); au_sync();
	au_writel(sleep_intctl_config2[1], IC1_CFG2SET); au_sync();
	au_writel(0xffffffff, IC1_SRCCLR); au_sync();
	au_writel(sleep_intctl_src[1], IC1_SRCSET); au_sync();
	au_writel(0xffffffff, IC1_ASSIGNCLR); au_sync();
	au_writel(sleep_intctl_assign[1], IC1_ASSIGNSET); au_sync();
	au_writel(0xffffffff, IC1_WAKECLR); au_sync();
	au_writel(sleep_intctl_wake[1], IC1_WAKESET); au_sync();
	au_writel(0xffffffff, IC1_RISINGCLR); au_sync();
	au_writel(0xffffffff, IC1_FALLINGCLR); au_sync();
	au_writel(0x00000000, IC1_TESTBIT); au_sync();

	au_writel(sleep_intctl_mask[1], IC1_MASKSET); au_sync();

	au_writel(sleep_intctl_mask[0], IC0_MASKSET); au_sync();
}
#endif /* CONFIG_PM */


inline void local_enable_irq(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC0_INT_BASE;

	if (bit >= 32) {
		au_writel(1 << (bit - 32), IC1_MASKSET);
		au_writel(1 << (bit - 32), IC1_WAKESET);
	} else {
		au_writel(1 << bit, IC0_MASKSET);
		au_writel(1 << bit, IC0_WAKESET);
	}
	au_sync();
}


inline void local_disable_irq(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC0_INT_BASE;

	if (bit >= 32) {
		au_writel(1 << (bit - 32), IC1_MASKCLR);
		au_writel(1 << (bit - 32), IC1_WAKECLR);
	} else {
		au_writel(1 << bit, IC0_MASKCLR);
		au_writel(1 << bit, IC0_WAKECLR);
	}
	au_sync();
}


static inline void mask_and_ack_rise_edge_irq(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC0_INT_BASE;

	if (bit >= 32) {
		au_writel(1 << (bit - 32), IC1_RISINGCLR);
		au_writel(1 << (bit - 32), IC1_MASKCLR);
	} else {
		au_writel(1 << bit, IC0_RISINGCLR);
		au_writel(1 << bit, IC0_MASKCLR);
	}
	au_sync();
}


static inline void mask_and_ack_fall_edge_irq(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC0_INT_BASE;

	if (bit >= 32) {
		au_writel(1 << (bit - 32), IC1_FALLINGCLR);
		au_writel(1 << (bit - 32), IC1_MASKCLR);
	} else {
		au_writel(1 << bit, IC0_FALLINGCLR);
		au_writel(1 << bit, IC0_MASKCLR);
	}
	au_sync();
}


static inline void mask_and_ack_either_edge_irq(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC0_INT_BASE;

	/*
	 * This may assume that we don't get interrupts from
	 * both edges at once, or if we do, that we don't care.
	 */
	if (bit >= 32) {
		au_writel(1 << (bit - 32), IC1_FALLINGCLR);
		au_writel(1 << (bit - 32), IC1_RISINGCLR);
		au_writel(1 << (bit - 32), IC1_MASKCLR);
	} else {
		au_writel(1 << bit, IC0_FALLINGCLR);
		au_writel(1 << bit, IC0_RISINGCLR);
		au_writel(1 << bit, IC0_MASKCLR);
	}
	au_sync();
}


static inline void mask_and_ack_level_irq(unsigned int irq_nr)
{

	local_disable_irq(irq_nr);
	au_sync();
#if defined(CONFIG_MIPS_PB1000)
	if (irq_nr == AU1000_GPIO_15) {
		au_writel(0x8000, PB1000_MDR); /* ack int */
		au_sync();
	}
#endif
}

static void end_irq(unsigned int irq_nr)
{
	if (!(irq_desc[irq_nr].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		local_enable_irq(irq_nr);

#if defined(CONFIG_MIPS_PB1000)
	if (irq_nr == AU1000_GPIO_15) {
		au_writel(0x4000, PB1000_MDR); /* enable int */
		au_sync();
	}
#endif
}

unsigned long save_local_and_disable(int controller)
{
	int i;
	unsigned long flags, mask;

	spin_lock_irqsave(&irq_lock, flags);
	if (controller) {
		mask = au_readl(IC1_MASKSET);
		for (i = 32; i < 64; i++)
			local_disable_irq(i);
	} else {
		mask = au_readl(IC0_MASKSET);
		for (i = 0; i < 32; i++)
			local_disable_irq(i);
	}
	spin_unlock_irqrestore(&irq_lock, flags);

	return mask;
}

void restore_local_and_enable(int controller, unsigned long mask)
{
	int i;
	unsigned long flags, new_mask;

	spin_lock_irqsave(&irq_lock, flags);
	for (i = 0; i < 32; i++) {
		if (mask & (1 << i)) {
			if (controller)
				local_enable_irq(i + 32);
			else
				local_enable_irq(i);
		}
	}
	if (controller)
		new_mask = au_readl(IC1_MASKSET);
	else
		new_mask = au_readl(IC0_MASKSET);

	spin_unlock_irqrestore(&irq_lock, flags);
}


static struct irq_chip rise_edge_irq_type = {
	.name		= "Au1000 Rise Edge",
	.ack		= mask_and_ack_rise_edge_irq,
	.mask		= local_disable_irq,
	.mask_ack	= mask_and_ack_rise_edge_irq,
	.unmask		= local_enable_irq,
	.end		= end_irq,
};

static struct irq_chip fall_edge_irq_type = {
	.name		= "Au1000 Fall Edge",
	.ack		= mask_and_ack_fall_edge_irq,
	.mask		= local_disable_irq,
	.mask_ack	= mask_and_ack_fall_edge_irq,
	.unmask		= local_enable_irq,
	.end		= end_irq,
};

static struct irq_chip either_edge_irq_type = {
	.name		= "Au1000 Rise or Fall Edge",
	.ack		= mask_and_ack_either_edge_irq,
	.mask		= local_disable_irq,
	.mask_ack	= mask_and_ack_either_edge_irq,
	.unmask		= local_enable_irq,
	.end		= end_irq,
};

static struct irq_chip level_irq_type = {
	.name		= "Au1000 Level",
	.ack		= mask_and_ack_level_irq,
	.mask		= local_disable_irq,
	.mask_ack	= mask_and_ack_level_irq,
	.unmask		= local_enable_irq,
	.end		= end_irq,
};

static void __init setup_local_irq(unsigned int irq_nr, int type, int int_req)
{
	unsigned int bit = irq_nr - AU1000_INTC0_INT_BASE;

	if (irq_nr > AU1000_MAX_INTR)
		return;

	/* Config2[n], Config1[n], Config0[n] */
	if (bit >= 32) {
		switch (type) {
		case INTC_INT_RISE_EDGE: /* 0:0:1 */
			au_writel(1 << (bit - 32), IC1_CFG2CLR);
			au_writel(1 << (bit - 32), IC1_CFG1CLR);
			au_writel(1 << (bit - 32), IC1_CFG0SET);
			set_irq_chip(irq_nr, &rise_edge_irq_type);
			break;
		case INTC_INT_FALL_EDGE: /* 0:1:0 */
			au_writel(1 << (bit - 32), IC1_CFG2CLR);
			au_writel(1 << (bit - 32), IC1_CFG1SET);
			au_writel(1 << (bit - 32), IC1_CFG0CLR);
			set_irq_chip(irq_nr, &fall_edge_irq_type);
			break;
		case INTC_INT_RISE_AND_FALL_EDGE: /* 0:1:1 */
			au_writel(1 << (bit - 32), IC1_CFG2CLR);
			au_writel(1 << (bit - 32), IC1_CFG1SET);
			au_writel(1 << (bit - 32), IC1_CFG0SET);
			set_irq_chip(irq_nr, &either_edge_irq_type);
			break;
		case INTC_INT_HIGH_LEVEL: /* 1:0:1 */
			au_writel(1 << (bit - 32), IC1_CFG2SET);
			au_writel(1 << (bit - 32), IC1_CFG1CLR);
			au_writel(1 << (bit - 32), IC1_CFG0SET);
			set_irq_chip(irq_nr, &level_irq_type);
			break;
		case INTC_INT_LOW_LEVEL: /* 1:1:0 */
			au_writel(1 << (bit - 32), IC1_CFG2SET);
			au_writel(1 << (bit - 32), IC1_CFG1SET);
			au_writel(1 << (bit - 32), IC1_CFG0CLR);
			set_irq_chip(irq_nr, &level_irq_type);
			break;
		case INTC_INT_DISABLED: /* 0:0:0 */
			au_writel(1 << (bit - 32), IC1_CFG0CLR);
			au_writel(1 << (bit - 32), IC1_CFG1CLR);
			au_writel(1 << (bit - 32), IC1_CFG2CLR);
			break;
		default: /* disable the interrupt */
			printk(KERN_WARNING "unexpected int type %d (irq %d)\n",
			       type, irq_nr);
			au_writel(1 << (bit - 32), IC1_CFG0CLR);
			au_writel(1 << (bit - 32), IC1_CFG1CLR);
			au_writel(1 << (bit - 32), IC1_CFG2CLR);
			return;
		}
		if (int_req) /* assign to interrupt request 1 */
			au_writel(1 << (bit - 32), IC1_ASSIGNCLR);
		else	     /* assign to interrupt request 0 */
			au_writel(1 << (bit - 32), IC1_ASSIGNSET);
		au_writel(1 << (bit - 32), IC1_SRCSET);
		au_writel(1 << (bit - 32), IC1_MASKCLR);
		au_writel(1 << (bit - 32), IC1_WAKECLR);
	} else {
		switch (type) {
		case INTC_INT_RISE_EDGE: /* 0:0:1 */
			au_writel(1 << bit, IC0_CFG2CLR);
			au_writel(1 << bit, IC0_CFG1CLR);
			au_writel(1 << bit, IC0_CFG0SET);
			set_irq_chip(irq_nr, &rise_edge_irq_type);
			break;
		case INTC_INT_FALL_EDGE: /* 0:1:0 */
			au_writel(1 << bit, IC0_CFG2CLR);
			au_writel(1 << bit, IC0_CFG1SET);
			au_writel(1 << bit, IC0_CFG0CLR);
			set_irq_chip(irq_nr, &fall_edge_irq_type);
			break;
		case INTC_INT_RISE_AND_FALL_EDGE: /* 0:1:1 */
			au_writel(1 << bit, IC0_CFG2CLR);
			au_writel(1 << bit, IC0_CFG1SET);
			au_writel(1 << bit, IC0_CFG0SET);
			set_irq_chip(irq_nr, &either_edge_irq_type);
			break;
		case INTC_INT_HIGH_LEVEL: /* 1:0:1 */
			au_writel(1 << bit, IC0_CFG2SET);
			au_writel(1 << bit, IC0_CFG1CLR);
			au_writel(1 << bit, IC0_CFG0SET);
			set_irq_chip(irq_nr, &level_irq_type);
			break;
		case INTC_INT_LOW_LEVEL: /* 1:1:0 */
			au_writel(1 << bit, IC0_CFG2SET);
			au_writel(1 << bit, IC0_CFG1SET);
			au_writel(1 << bit, IC0_CFG0CLR);
			set_irq_chip(irq_nr, &level_irq_type);
			break;
		case INTC_INT_DISABLED: /* 0:0:0 */
			au_writel(1 << bit, IC0_CFG0CLR);
			au_writel(1 << bit, IC0_CFG1CLR);
			au_writel(1 << bit, IC0_CFG2CLR);
			break;
		default: /* disable the interrupt */
			printk(KERN_WARNING "unexpected int type %d (irq %d)\n",
			       type, irq_nr);
			au_writel(1 << bit, IC0_CFG0CLR);
			au_writel(1 << bit, IC0_CFG1CLR);
			au_writel(1 << bit, IC0_CFG2CLR);
			return;
		}
		if (int_req) /* assign to interrupt request 1 */
			au_writel(1 << bit, IC0_ASSIGNCLR);
		else	     /* assign to interrupt request 0 */
			au_writel(1 << bit, IC0_ASSIGNSET);
		au_writel(1 << bit, IC0_SRCSET);
		au_writel(1 << bit, IC0_MASKCLR);
		au_writel(1 << bit, IC0_WAKECLR);
	}
	au_sync();
}

/*
 * Interrupts are nested. Even if an interrupt handler is registered
 * as "fast", we might get another interrupt before we return from
 * intcX_reqX_irqdispatch().
 */

static void intc0_req0_irqdispatch(void)
{
	static unsigned long intc0_req0;
	unsigned int bit;

	intc0_req0 |= au_readl(IC0_REQ0INT);

	if (!intc0_req0)
		return;

#ifdef AU1000_USB_DEV_REQ_INT
	/*
	 * Because of the tight timing of SETUP token to reply
	 * transactions, the USB devices-side packet complete
	 * interrupt needs the highest priority.
	 */
	if ((intc0_req0 & (1 << AU1000_USB_DEV_REQ_INT))) {
		intc0_req0 &= ~(1 << AU1000_USB_DEV_REQ_INT);
		do_IRQ(AU1000_USB_DEV_REQ_INT);
		return;
	}
#endif
	bit = ffs(intc0_req0);
	intc0_req0 &= ~(1 << bit);
	do_IRQ(MIPS_CPU_IRQ_BASE + bit);
}


static void intc0_req1_irqdispatch(void)
{
	static unsigned long intc0_req1;
	unsigned int bit;

	intc0_req1 |= au_readl(IC0_REQ1INT);

	if (!intc0_req1)
		return;

	bit = ffs(intc0_req1);
	intc0_req1 &= ~(1 << bit);
	do_IRQ(bit);
}


/*
 * Interrupt Controller 1:
 * interrupts 32 - 63
 */
static void intc1_req0_irqdispatch(void)
{
	static unsigned long intc1_req0;
	unsigned int bit;

	intc1_req0 |= au_readl(IC1_REQ0INT);

	if (!intc1_req0)
		return;

	bit = ffs(intc1_req0);
	intc1_req0 &= ~(1 << bit);
	do_IRQ(MIPS_CPU_IRQ_BASE + 32 + bit);
}


static void intc1_req1_irqdispatch(void)
{
	static unsigned long intc1_req1;
	unsigned int bit;

	intc1_req1 |= au_readl(IC1_REQ1INT);

	if (!intc1_req1)
		return;

	bit = ffs(intc1_req1);
	intc1_req1 &= ~(1 << bit);
	do_IRQ(MIPS_CPU_IRQ_BASE + 32 + bit);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause();

	if (pending & CAUSEF_IP7)
		do_IRQ(MIPS_CPU_IRQ_BASE + 7);
	else if (pending & CAUSEF_IP2)
		intc0_req0_irqdispatch();
	else if (pending & CAUSEF_IP3)
		intc0_req1_irqdispatch();
	else if (pending & CAUSEF_IP4)
		intc1_req0_irqdispatch();
	else if (pending  & CAUSEF_IP5)
		intc1_req1_irqdispatch();
	else
		spurious_interrupt();
}

void __init arch_init_irq(void)
{
	int i;
	struct au1xxx_irqmap *imp;
	extern struct au1xxx_irqmap au1xxx_irq_map[];
	extern struct au1xxx_irqmap au1xxx_ic0_map[];
	extern int au1xxx_nr_irqs;
	extern int au1xxx_ic0_nr_irqs;

	/*
	 * Initialize interrupt controllers to a safe state.
	 */
	au_writel(0xffffffff, IC0_CFG0CLR);
	au_writel(0xffffffff, IC0_CFG1CLR);
	au_writel(0xffffffff, IC0_CFG2CLR);
	au_writel(0xffffffff, IC0_MASKCLR);
	au_writel(0xffffffff, IC0_ASSIGNSET);
	au_writel(0xffffffff, IC0_WAKECLR);
	au_writel(0xffffffff, IC0_SRCSET);
	au_writel(0xffffffff, IC0_FALLINGCLR);
	au_writel(0xffffffff, IC0_RISINGCLR);
	au_writel(0x00000000, IC0_TESTBIT);

	au_writel(0xffffffff, IC1_CFG0CLR);
	au_writel(0xffffffff, IC1_CFG1CLR);
	au_writel(0xffffffff, IC1_CFG2CLR);
	au_writel(0xffffffff, IC1_MASKCLR);
	au_writel(0xffffffff, IC1_ASSIGNSET);
	au_writel(0xffffffff, IC1_WAKECLR);
	au_writel(0xffffffff, IC1_SRCSET);
	au_writel(0xffffffff, IC1_FALLINGCLR);
	au_writel(0xffffffff, IC1_RISINGCLR);
	au_writel(0x00000000, IC1_TESTBIT);

	mips_cpu_irq_init();

	/*
	 * Initialize IC0, which is fixed per processor.
	 */
	imp = au1xxx_ic0_map;
	for (i = 0; i < au1xxx_ic0_nr_irqs; i++) {
		setup_local_irq(imp->im_irq, imp->im_type, imp->im_request);
		imp++;
	}

	/*
	 * Now set up the irq mapping for the board.
	 */
	imp = au1xxx_irq_map;
	for (i = 0; i < au1xxx_nr_irqs; i++) {
		setup_local_irq(imp->im_irq, imp->im_type, imp->im_request);
		imp++;
	}

	set_c0_status(ALLINTS);

	/* Board specific IRQ initialization.
	*/
	if (board_init_irq)
		board_init_irq();
}
