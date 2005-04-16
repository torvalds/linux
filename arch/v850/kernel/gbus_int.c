/*
 * arch/v850/kernel/gbus_int.c -- Midas labs GBUS interrupt support
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/signal.h>

#include <asm/machdep.h>


/* The number of shared GINT interrupts. */
#define NUM_GINTS   	4

/* For each GINT interrupt, how many GBUS interrupts are using it.  */
static unsigned gint_num_active_irqs[NUM_GINTS] = { 0 };

/* A table of GINTn interrupts we actually use.
   Note that we don't use GINT0 because all the boards we support treat it
   specially.  */
struct used_gint {
	unsigned gint;
	unsigned priority;
} used_gint[] = {
	{ 1, GBUS_INT_PRIORITY_HIGH },
	{ 3, GBUS_INT_PRIORITY_LOW }
};
#define NUM_USED_GINTS	(sizeof used_gint / sizeof used_gint[0])

/* A table of which GINT is used by each GBUS interrupts (they are
   assigned based on priority).  */
static unsigned char gbus_int_gint[IRQ_GBUS_INT_NUM];


/* Interrupt enabling/disabling.  */

/* Enable interrupt handling for interrupt IRQ.  */
void gbus_int_enable_irq (unsigned irq)
{
	unsigned gint = gbus_int_gint[irq - GBUS_INT_BASE_IRQ];
	GBUS_INT_ENABLE (GBUS_INT_IRQ_WORD(irq), gint)
		|= GBUS_INT_IRQ_MASK (irq);
}

/* Disable interrupt handling for interrupt IRQ.  Note that any
   interrupts received while disabled will be delivered once the
   interrupt is enabled again, unless they are explicitly cleared using
   `gbus_int_clear_pending_irq'.  */
void gbus_int_disable_irq (unsigned irq)
{
	unsigned gint = gbus_int_gint[irq - GBUS_INT_BASE_IRQ];
	GBUS_INT_ENABLE (GBUS_INT_IRQ_WORD(irq), gint)
		&= ~GBUS_INT_IRQ_MASK (irq);
}

/* Return true if interrupt handling for interrupt IRQ is enabled.  */
int gbus_int_irq_enabled (unsigned irq)
{
	unsigned gint = gbus_int_gint[irq - GBUS_INT_BASE_IRQ];
	return (GBUS_INT_ENABLE (GBUS_INT_IRQ_WORD(irq), gint)
		& GBUS_INT_IRQ_MASK(irq));
}

/* Disable all GBUS irqs.  */
void gbus_int_disable_irqs ()
{
	unsigned w, n;
	for (w = 0; w < GBUS_INT_NUM_WORDS; w++)
		for (n = 0; n < IRQ_GINT_NUM; n++)
			GBUS_INT_ENABLE (w, n) = 0;
}

/* Clear any pending interrupts for IRQ.  */
void gbus_int_clear_pending_irq (unsigned irq)
{
	GBUS_INT_CLEAR (GBUS_INT_IRQ_WORD(irq)) = GBUS_INT_IRQ_MASK (irq);
}

/* Return true if interrupt IRQ is pending (but disabled).  */
int gbus_int_irq_pending (unsigned irq)
{
	return (GBUS_INT_STATUS (GBUS_INT_IRQ_WORD(irq))
		& GBUS_INT_IRQ_MASK(irq));
}


/* Delegating interrupts.  */

/* Handle a shared GINT interrupt by passing to the appropriate GBUS
   interrupt handler.  */
static irqreturn_t gbus_int_handle_irq (int irq, void *dev_id,
					struct pt_regs *regs)
{
	unsigned w;
	irqreturn_t rval = IRQ_NONE;
	unsigned gint = irq - IRQ_GINT (0);

	for (w = 0; w < GBUS_INT_NUM_WORDS; w++) {
		unsigned status = GBUS_INT_STATUS (w);
		unsigned enable = GBUS_INT_ENABLE (w, gint);

		/* Only pay attention to enabled interrupts.  */
		status &= enable;
		if (status) {
			irq = IRQ_GBUS_INT (w * GBUS_INT_BITS_PER_WORD);
			do {
				/* There's an active interrupt in word
				   W, find out which one, and call its
				   handler.  */

				while (! (status & 0x1)) {
					irq++;
					status >>= 1;
				}
				status &= ~0x1;

				/* Recursively call handle_irq to handle it. */
				handle_irq (irq, regs);
				rval = IRQ_HANDLED;
			} while (status);
		}
	}

	/* Toggle the `all enable' bit back and forth, which should cause
	   another edge transition if there are any other interrupts
	   still pending, and so result in another CPU interrupt.  */
	GBUS_INT_ENABLE (0, gint) &= ~0x1;
	GBUS_INT_ENABLE (0, gint) |=  0x1;

	return rval;
}


/* Initialize GBUS interrupt sources.  */

static void irq_nop (unsigned irq) { }

static unsigned gbus_int_startup_irq (unsigned irq)
{
	unsigned gint = gbus_int_gint[irq - GBUS_INT_BASE_IRQ];

	if (gint_num_active_irqs[gint] == 0) {
		/* First enable the CPU interrupt.  */
		int rval =
			request_irq (IRQ_GINT(gint), gbus_int_handle_irq,
				     SA_INTERRUPT,
				     "gbus_int_handler",
				     &gint_num_active_irqs[gint]);
		if (rval != 0)
			return rval;
	}

	gint_num_active_irqs[gint]++;

	gbus_int_clear_pending_irq (irq);
	gbus_int_enable_irq (irq);

	return 0;
}

static void gbus_int_shutdown_irq (unsigned irq)
{
	unsigned gint = gbus_int_gint[irq - GBUS_INT_BASE_IRQ];

	gbus_int_disable_irq (irq);

	if (--gint_num_active_irqs[gint] == 0)
		/* Disable the CPU interrupt.  */
		free_irq (IRQ_GINT(gint), &gint_num_active_irqs[gint]);
}

/* Initialize HW_IRQ_TYPES for INTC-controlled irqs described in array
   INITS (which is terminated by an entry with the name field == 0).  */
void __init gbus_int_init_irq_types (struct gbus_int_irq_init *inits,
				     struct hw_interrupt_type *hw_irq_types)
{
	struct gbus_int_irq_init *init;
	for (init = inits; init->name; init++) {
		unsigned i;
		struct hw_interrupt_type *hwit = hw_irq_types++;

		hwit->typename = init->name;

		hwit->startup  = gbus_int_startup_irq;
		hwit->shutdown = gbus_int_shutdown_irq;
		hwit->enable   = gbus_int_enable_irq;
		hwit->disable  = gbus_int_disable_irq;
		hwit->ack      = irq_nop;
		hwit->end      = irq_nop;
		
		/* Initialize kernel IRQ infrastructure for this interrupt.  */
		init_irq_handlers(init->base, init->num, init->interval, hwit);

		/* Set the interrupt priorities.  */
		for (i = 0; i < init->num; i++) {
			unsigned j;
			for (j = 0; j < NUM_USED_GINTS; j++)
				if (used_gint[j].priority > init->priority)
					break;
			/* Wherever we stopped looking is one past the
			   GINT we want. */
			gbus_int_gint[init->base + i * init->interval
				      - GBUS_INT_BASE_IRQ]
				= used_gint[j > 0 ? j - 1 : 0].gint;
		}
	}
}


/* Initialize IRQS.  */

/* Chip interrupts (GINTn) shared among GBUS interrupts.  */
static struct hw_interrupt_type gint_hw_itypes[NUM_USED_GINTS];


/* GBUS interrupts themselves.  */

struct gbus_int_irq_init gbus_irq_inits[] __initdata = {
	/* First set defaults.  */
	{ "GBUS_INT", IRQ_GBUS_INT(0), IRQ_GBUS_INT_NUM, 1, 6},
	{ 0 }
};
#define NUM_GBUS_IRQ_INITS  \
   ((sizeof gbus_irq_inits / sizeof gbus_irq_inits[0]) - 1)

static struct hw_interrupt_type gbus_hw_itypes[NUM_GBUS_IRQ_INITS];


/* Initialize GBUS interrupts.  */
void __init gbus_int_init_irqs (void)
{
	unsigned i;

	/* First initialize the shared gint interrupts.  */
	for (i = 0; i < NUM_USED_GINTS; i++) {
		unsigned gint = used_gint[i].gint;
		struct v850e_intc_irq_init gint_irq_init[2];

		/* We initialize one GINT interrupt at a time.  */
		gint_irq_init[0].name = "GINT";
		gint_irq_init[0].base = IRQ_GINT (gint);
		gint_irq_init[0].num = 1;
		gint_irq_init[0].interval = 1;
		gint_irq_init[0].priority = used_gint[i].priority;

		gint_irq_init[1].name = 0; /* Terminate the vector.  */

		v850e_intc_init_irq_types (gint_irq_init, gint_hw_itypes);
	}

	/* Then the GBUS interrupts.  */
	gbus_int_disable_irqs ();
	gbus_int_init_irq_types (gbus_irq_inits, gbus_hw_itypes);
	/* Turn on the `all enable' bits, which are ANDed with
	   individual interrupt enable bits; we only want to bother with
	   the latter.  They are the first bit in the first word of each
	   interrupt-enable area.  */
	for (i = 0; i < NUM_USED_GINTS; i++)
		GBUS_INT_ENABLE (0, used_gint[i].gint) = 0x1;
}
