/*
 * arch/m68k/q40/q40ints.c
 *
 * Copyright (C) 1999,2001 Richard Zidlicky
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * .. used to be loosely based on bvme6000ints.c
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/machdep.h>
#include <asm/ptrace.h>
#include <asm/traps.h>

#include <asm/q40_master.h>
#include <asm/q40ints.h>

#include "q40.h"

/*
 * Q40 IRQs are defined as follows:
 *            3,4,5,6,7,10,11,14,15 : ISA dev IRQs
 *            16-31: reserved
 *            32   : keyboard int
 *            33   : frame int (50/200 Hz periodic timer)
 *            34   : sample int (10/20 KHz periodic timer)
 *
 */

static void q40_irq_handler(unsigned int, struct pt_regs *fp);
static void q40_irq_enable(struct irq_data *data);
static void q40_irq_disable(struct irq_data *data);

unsigned short q40_ablecount[35];
unsigned short q40_state[35];

static unsigned int q40_irq_startup(struct irq_data *data)
{
	unsigned int irq = data->irq;

	/* test for ISA ints not implemented by HW */
	switch (irq) {
	case 1: case 2: case 8: case 9:
	case 11: case 12: case 13:
		pr_warn("%s: ISA IRQ %d not implemented by HW\n", __func__,
			irq);
		/* FIXME return -ENXIO; */
	}
	return 0;
}

static void q40_irq_shutdown(struct irq_data *data)
{
}

static struct irq_chip q40_irq_chip = {
	.name		= "q40",
	.irq_startup	= q40_irq_startup,
	.irq_shutdown	= q40_irq_shutdown,
	.irq_enable	= q40_irq_enable,
	.irq_disable	= q40_irq_disable,
};

/*
 * void q40_init_IRQ (void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function is called during kernel startup to initialize
 * the q40 IRQ handling routines.
 */

static int disabled;

void __init q40_init_IRQ(void)
{
	m68k_setup_irq_controller(&q40_irq_chip, handle_simple_irq, 1,
				  Q40_IRQ_MAX);

	/* setup handler for ISA ints */
	m68k_setup_auto_interrupt(q40_irq_handler);

	m68k_irq_startup_irq(IRQ_AUTO_2);
	m68k_irq_startup_irq(IRQ_AUTO_4);

	/* now enable some ints.. */
	master_outb(1, EXT_ENABLE_REG);  /* ISA IRQ 5-15 */

	/* make sure keyboard IRQ is disabled */
	master_outb(0, KEY_IRQ_ENABLE_REG);
}


/*
 * this stuff doesn't really belong here..
 */

int ql_ticks;              /* 200Hz ticks since last jiffie */
static int sound_ticks;

#define SVOL 45

void q40_mksound(unsigned int hz, unsigned int ticks)
{
	/* for now ignore hz, except that hz==0 switches off sound */
	/* simply alternate the ampl (128-SVOL)-(128+SVOL)-..-.. at 200Hz */
	if (hz == 0) {
		if (sound_ticks)
			sound_ticks = 1;

		*DAC_LEFT = 128;
		*DAC_RIGHT = 128;

		return;
	}
	/* sound itself is done in q40_timer_int */
	if (sound_ticks == 0)
		sound_ticks = 1000; /* pretty long beep */
	sound_ticks = ticks << 1;
}

static irqreturn_t q40_timer_int(int irq, void *dev_id)
{
	ql_ticks = ql_ticks ? 0 : 1;
	if (sound_ticks) {
		unsigned char sval=(sound_ticks & 1) ? 128-SVOL : 128+SVOL;
		sound_ticks--;
		*DAC_LEFT=sval;
		*DAC_RIGHT=sval;
	}

	if (!ql_ticks) {
		unsigned long flags;

		local_irq_save(flags);
		legacy_timer_tick(1);
		timer_heartbeat();
		local_irq_restore(flags);
	}
	return IRQ_HANDLED;
}

void q40_sched_init (void)
{
	int timer_irq;

	timer_irq = Q40_IRQ_FRAME;

	if (request_irq(timer_irq, q40_timer_int, 0, "timer", NULL))
		panic("Couldn't register timer int");

	master_outb(-1, FRAME_CLEAR_REG);
	master_outb( 1, FRAME_RATE_REG);
}


/*
 * tables to translate bits into IRQ numbers
 * it is a good idea to order the entries by priority
 *
*/

struct IRQ_TABLE{ unsigned mask; int irq ;};
#if 0
static struct IRQ_TABLE iirqs[]={
  {Q40_IRQ_FRAME_MASK,Q40_IRQ_FRAME},
  {Q40_IRQ_KEYB_MASK,Q40_IRQ_KEYBOARD},
  {0,0}};
#endif
static struct IRQ_TABLE eirqs[] = {
  { .mask = Q40_IRQ3_MASK,	.irq = 3 },	/* ser 1 */
  { .mask = Q40_IRQ4_MASK,	.irq = 4 },	/* ser 2 */
  { .mask = Q40_IRQ14_MASK,	.irq = 14 },	/* IDE 1 */
  { .mask = Q40_IRQ15_MASK,	.irq = 15 },	/* IDE 2 */
  { .mask = Q40_IRQ6_MASK,	.irq = 6 },	/* floppy, handled elsewhere */
  { .mask = Q40_IRQ7_MASK,	.irq = 7 },	/* par */
  { .mask = Q40_IRQ5_MASK,	.irq = 5 },
  { .mask = Q40_IRQ10_MASK,	.irq = 10 },
  {0,0}
};

/* complain only this many times about spurious ints : */
static int ccleirq=60;    /* ISA dev IRQs*/
/*static int cclirq=60;*/     /* internal */

/* FIXME: add shared ints,mask,unmask,probing.... */

#define IRQ_INPROGRESS 1
/*static unsigned short saved_mask;*/
//static int do_tint=0;

#define DEBUG_Q40INT
/*#define IP_USE_DISABLE *//* would be nice, but crashes ???? */

static int mext_disabled;	/* ext irq disabled by master chip? */
static int aliased_irq;		/* how many times inside handler ?*/


/* got interrupt, dispatch to ISA or keyboard/timer IRQs */
static void q40_irq_handler(unsigned int irq, struct pt_regs *fp)
{
	unsigned mir, mer;
	int i;

//repeat:
	mir = master_inb(IIRQ_REG);
#ifdef CONFIG_BLK_DEV_FD
	if ((mir & Q40_IRQ_EXT_MASK) &&
	    (master_inb(EIRQ_REG) & Q40_IRQ6_MASK)) {
		floppy_hardint();
		return;
	}
#endif
	switch (irq) {
	case 4:
	case 6:
		do_IRQ(Q40_IRQ_SAMPLE, fp);
		return;
	}
	if (mir & Q40_IRQ_FRAME_MASK) {
		do_IRQ(Q40_IRQ_FRAME, fp);
		master_outb(-1, FRAME_CLEAR_REG);
	}
	if ((mir & Q40_IRQ_SER_MASK) || (mir & Q40_IRQ_EXT_MASK)) {
		mer = master_inb(EIRQ_REG);
		for (i = 0; eirqs[i].mask; i++) {
			if (mer & eirqs[i].mask) {
				irq = eirqs[i].irq;
/*
 * There is a little mess wrt which IRQ really caused this irq request. The
 * main problem is that IIRQ_REG and EIRQ_REG reflect the state when they
 * are read - which is long after the request came in. In theory IRQs should
 * not just go away but they occasionally do
 */
				if (irq > 4 && irq <= 15 && mext_disabled) {
					/*aliased_irq++;*/
					goto iirq;
				}
				if (q40_state[irq] & IRQ_INPROGRESS) {
					/* some handlers do local_irq_enable() for irq latency reasons, */
					/* however reentering an active irq handler is not permitted */
#ifdef IP_USE_DISABLE
					/* in theory this is the better way to do it because it still */
					/* lets through eg the serial irqs, unfortunately it crashes */
					disable_irq(irq);
					disabled = 1;
#else
					/*pr_warn("IRQ_INPROGRESS detected for irq %d, disabling - %s disabled\n",
						irq, disabled ? "already" : "not yet"); */
					fp->sr = (((fp->sr) & (~0x700))+0x200);
					disabled = 1;
#endif
					goto iirq;
				}
				q40_state[irq] |= IRQ_INPROGRESS;
				do_IRQ(irq, fp);
				q40_state[irq] &= ~IRQ_INPROGRESS;

				/* naively enable everything, if that fails than    */
				/* this function will be reentered immediately thus */
				/* getting another chance to disable the IRQ        */

				if (disabled) {
#ifdef IP_USE_DISABLE
					if (irq > 4) {
						disabled = 0;
						enable_irq(irq);
					}
#else
					disabled = 0;
					/*pr_info("reenabling irq %d\n", irq); */
#endif
				}
// used to do 'goto repeat;' here, this delayed bh processing too long
				return;
			}
		}
		if (mer && ccleirq > 0 && !aliased_irq) {
			pr_warn("ISA interrupt from unknown source? EIRQ_REG = %x\n",
				mer);
			ccleirq--;
		}
	}
 iirq:
	mir = master_inb(IIRQ_REG);
	/* should test whether keyboard irq is really enabled, doing it in defhand */
	if (mir & Q40_IRQ_KEYB_MASK)
		do_IRQ(Q40_IRQ_KEYBOARD, fp);

	return;
}

void q40_irq_enable(struct irq_data *data)
{
	unsigned int irq = data->irq;

	if (irq >= 5 && irq <= 15) {
		mext_disabled--;
		if (mext_disabled > 0)
			pr_warn("q40_irq_enable : nested disable/enable\n");
		if (mext_disabled == 0)
			master_outb(1, EXT_ENABLE_REG);
	}
}


void q40_irq_disable(struct irq_data *data)
{
	unsigned int irq = data->irq;

	/* disable ISA iqs : only do something if the driver has been
	 * verified to be Q40 "compatible" - right now IDE, NE2K
	 * Any driver should not attempt to sleep across disable_irq !!
	 */

	if (irq >= 5 && irq <= 15) {
		master_outb(0, EXT_ENABLE_REG);
		mext_disabled++;
		if (mext_disabled > 1)
			pr_info("disable_irq nesting count %d\n",
				mext_disabled);
	}
}
