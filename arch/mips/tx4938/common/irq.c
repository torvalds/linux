/*
 * linux/arch/mps/tx4938/common/irq.c
 *
 * Common tx4938 irq handler
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/errno.h>
#include <linux/init.h>
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
#include <linux/irq.h>
#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/tx4938/rbtx4938.h>

/**********************************************************************************/
/* Forwad definitions for all pic's                                               */
/**********************************************************************************/

static unsigned int tx4938_irq_cp0_startup(unsigned int irq);
static void tx4938_irq_cp0_shutdown(unsigned int irq);
static void tx4938_irq_cp0_enable(unsigned int irq);
static void tx4938_irq_cp0_disable(unsigned int irq);
static void tx4938_irq_cp0_mask_and_ack(unsigned int irq);
static void tx4938_irq_cp0_end(unsigned int irq);

static unsigned int tx4938_irq_pic_startup(unsigned int irq);
static void tx4938_irq_pic_shutdown(unsigned int irq);
static void tx4938_irq_pic_enable(unsigned int irq);
static void tx4938_irq_pic_disable(unsigned int irq);
static void tx4938_irq_pic_mask_and_ack(unsigned int irq);
static void tx4938_irq_pic_end(unsigned int irq);

/**********************************************************************************/
/* Kernel structs for all pic's                                                   */
/**********************************************************************************/
DEFINE_SPINLOCK(tx4938_cp0_lock);
DEFINE_SPINLOCK(tx4938_pic_lock);

#define TX4938_CP0_NAME "TX4938-CP0"
static struct hw_interrupt_type tx4938_irq_cp0_type = {
	.typename = TX4938_CP0_NAME,
	.startup = tx4938_irq_cp0_startup,
	.shutdown = tx4938_irq_cp0_shutdown,
	.enable = tx4938_irq_cp0_enable,
	.disable = tx4938_irq_cp0_disable,
	.ack = tx4938_irq_cp0_mask_and_ack,
	.end = tx4938_irq_cp0_end,
	.set_affinity = NULL
};

#define TX4938_PIC_NAME "TX4938-PIC"
static struct hw_interrupt_type tx4938_irq_pic_type = {
	.typename = TX4938_PIC_NAME,
	.startup = tx4938_irq_pic_startup,
	.shutdown = tx4938_irq_pic_shutdown,
	.enable = tx4938_irq_pic_enable,
	.disable = tx4938_irq_pic_disable,
	.ack = tx4938_irq_pic_mask_and_ack,
	.end = tx4938_irq_pic_end,
	.set_affinity = NULL
};

static struct irqaction tx4938_irq_pic_action = {
	.handler = no_action,
	.flags = 0,
	.mask = CPU_MASK_NONE,
	.name = TX4938_PIC_NAME
};

/**********************************************************************************/
/* Functions for cp0                                                              */
/**********************************************************************************/

#define tx4938_irq_cp0_mask(irq) ( 1 << ( irq-TX4938_IRQ_CP0_BEG+8 ) )

static void __init
tx4938_irq_cp0_init(void)
{
	int i;

	for (i = TX4938_IRQ_CP0_BEG; i <= TX4938_IRQ_CP0_END; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = 0;
		irq_desc[i].depth = 1;
		irq_desc[i].handler = &tx4938_irq_cp0_type;
	}

	return;
}

static unsigned int
tx4938_irq_cp0_startup(unsigned int irq)
{
	tx4938_irq_cp0_enable(irq);

	return (0);
}

static void
tx4938_irq_cp0_shutdown(unsigned int irq)
{
	tx4938_irq_cp0_disable(irq);
}

static void
tx4938_irq_cp0_enable(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&tx4938_cp0_lock, flags);

	set_c0_status(tx4938_irq_cp0_mask(irq));

	spin_unlock_irqrestore(&tx4938_cp0_lock, flags);
}

static void
tx4938_irq_cp0_disable(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&tx4938_cp0_lock, flags);

	clear_c0_status(tx4938_irq_cp0_mask(irq));

	spin_unlock_irqrestore(&tx4938_cp0_lock, flags);

	return;
}

static void
tx4938_irq_cp0_mask_and_ack(unsigned int irq)
{
	tx4938_irq_cp0_disable(irq);

	return;
}

static void
tx4938_irq_cp0_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		tx4938_irq_cp0_enable(irq);
	}

	return;
}

/**********************************************************************************/
/* Functions for pic                                                              */
/**********************************************************************************/

u32
tx4938_irq_pic_addr(int irq)
{
	/* MVMCP -- need to formulize this */
	irq -= TX4938_IRQ_PIC_BEG;

	switch (irq) {
	case 17:
	case 16:
	case 1:
	case 0:{
			return (TX4938_MKA(TX4938_IRC_IRLVL0));
		}
	case 19:
	case 18:
	case 3:
	case 2:{
			return (TX4938_MKA(TX4938_IRC_IRLVL1));
		}
	case 21:
	case 20:
	case 5:
	case 4:{
			return (TX4938_MKA(TX4938_IRC_IRLVL2));
		}
	case 23:
	case 22:
	case 7:
	case 6:{
			return (TX4938_MKA(TX4938_IRC_IRLVL3));
		}
	case 25:
	case 24:
	case 9:
	case 8:{
			return (TX4938_MKA(TX4938_IRC_IRLVL4));
		}
	case 27:
	case 26:
	case 11:
	case 10:{
			return (TX4938_MKA(TX4938_IRC_IRLVL5));
		}
	case 29:
	case 28:
	case 13:
	case 12:{
			return (TX4938_MKA(TX4938_IRC_IRLVL6));
		}
	case 31:
	case 30:
	case 15:
	case 14:{
			return (TX4938_MKA(TX4938_IRC_IRLVL7));
		}
	}

	return (0);
}

u32
tx4938_irq_pic_mask(int irq)
{
	/* MVMCP -- need to formulize this */
	irq -= TX4938_IRQ_PIC_BEG;

	switch (irq) {
	case 31:
	case 29:
	case 27:
	case 25:
	case 23:
	case 21:
	case 19:
	case 17:{
			return (0x07000000);
		}
	case 30:
	case 28:
	case 26:
	case 24:
	case 22:
	case 20:
	case 18:
	case 16:{
			return (0x00070000);
		}
	case 15:
	case 13:
	case 11:
	case 9:
	case 7:
	case 5:
	case 3:
	case 1:{
			return (0x00000700);
		}
	case 14:
	case 12:
	case 10:
	case 8:
	case 6:
	case 4:
	case 2:
	case 0:{
			return (0x00000007);
		}
	}
	return (0x00000000);
}

static void
tx4938_irq_pic_modify(unsigned pic_reg, unsigned clr_bits, unsigned set_bits)
{
	unsigned long val = 0;

	val = TX4938_RD(pic_reg);
	val &= (~clr_bits);
	val |= (set_bits);
	TX4938_WR(pic_reg, val);
	mmiowb();
	TX4938_RD(pic_reg);

	return;
}

static void __init
tx4938_irq_pic_init(void)
{
	unsigned long flags;
	int i;

	for (i = TX4938_IRQ_PIC_BEG; i <= TX4938_IRQ_PIC_END; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = 0;
		irq_desc[i].depth = 2;
		irq_desc[i].handler = &tx4938_irq_pic_type;
	}

	setup_irq(TX4938_IRQ_NEST_PIC_ON_CP0, &tx4938_irq_pic_action);

	spin_lock_irqsave(&tx4938_pic_lock, flags);

	TX4938_WR(0xff1ff640, 0x6);	/* irq level mask -- only accept hightest */
	TX4938_WR(0xff1ff600, TX4938_RD(0xff1ff600) | 0x1);	/* irq enable */

	spin_unlock_irqrestore(&tx4938_pic_lock, flags);

	return;
}

static unsigned int
tx4938_irq_pic_startup(unsigned int irq)
{
	tx4938_irq_pic_enable(irq);

	return (0);
}

static void
tx4938_irq_pic_shutdown(unsigned int irq)
{
	tx4938_irq_pic_disable(irq);

	return;
}

static void
tx4938_irq_pic_enable(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&tx4938_pic_lock, flags);

	tx4938_irq_pic_modify(tx4938_irq_pic_addr(irq), 0,
			      tx4938_irq_pic_mask(irq));

	spin_unlock_irqrestore(&tx4938_pic_lock, flags);

	return;
}

static void
tx4938_irq_pic_disable(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&tx4938_pic_lock, flags);

	tx4938_irq_pic_modify(tx4938_irq_pic_addr(irq),
			      tx4938_irq_pic_mask(irq), 0);

	spin_unlock_irqrestore(&tx4938_pic_lock, flags);

	return;
}

static void
tx4938_irq_pic_mask_and_ack(unsigned int irq)
{
	tx4938_irq_pic_disable(irq);

	return;
}

static void
tx4938_irq_pic_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		tx4938_irq_pic_enable(irq);
	}

	return;
}

/**********************************************************************************/
/* Main init functions                                                            */
/**********************************************************************************/

void __init
tx4938_irq_init(void)
{
	extern asmlinkage void tx4938_irq_handler(void);

	tx4938_irq_cp0_init();
	tx4938_irq_pic_init();
	set_except_vector(0, tx4938_irq_handler);

	return;
}

int
tx4938_irq_nested(void)
{
	int sw_irq = 0;
	u32 level2;

	level2 = TX4938_RD(0xff1ff6a0);
	if ((level2 & 0x10000) == 0) {
		level2 &= 0x1f;
		sw_irq = TX4938_IRQ_PIC_BEG + level2;
		if (sw_irq == 26) {
			{
				extern int toshiba_rbtx4938_irq_nested(int sw_irq);
				sw_irq = toshiba_rbtx4938_irq_nested(sw_irq);
			}
		}
	}

	wbflush();
	return (sw_irq);
}
