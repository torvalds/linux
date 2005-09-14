/* 
 * Copyright (C) 2000 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * Looks after interrupts on the overdrive board.
 *
 * Bases on the IPR irq system
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/system.h>
#include <asm/io.h>

#include <asm/overdrive/overdrive.h>

struct od_data {
	int overdrive_irq;
	int irq_mask;
};

#define NUM_EXTERNAL_IRQS 16
#define EXTERNAL_IRQ_NOT_IN_USE (-1)
#define EXTERNAL_IRQ_NOT_ASSIGNED (-1)

/*
 * This table is used to determine what to program into the FPGA's CT register
 * for the specified Linux IRQ.
 *
 * The irq_mask gives the interrupt number from the PCI board (PCI_Int(6:0))
 * but is one greater than that because the because the FPGA treats 0
 * as disabled, a value of 1 asserts PCI_Int0, and so on.
 *
 * The overdrive_irq specifies which of the eight interrupt sources generates
 * that interrupt, and but is multiplied by four to give the bit offset into
 * the CT register.
 *
 * The seven interrupts levels (SH4 IRL's) we have available here is hardwired
 * by the EPLD. The assignments here of which PCI interrupt generates each
 * level is arbitary.
 */
static struct od_data od_data_table[NUM_EXTERNAL_IRQS] = {
	/*    overdrive_irq       , irq_mask */
	{EXTERNAL_IRQ_NOT_ASSIGNED, EXTERNAL_IRQ_NOT_IN_USE},	/* 0 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, 7},	/* 1 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, 6},	/* 2 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, EXTERNAL_IRQ_NOT_IN_USE},	/* 3 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, 5},	/* 4 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, EXTERNAL_IRQ_NOT_IN_USE},	/* 5 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, EXTERNAL_IRQ_NOT_IN_USE},	/* 6 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, 4},	/* 7 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, EXTERNAL_IRQ_NOT_IN_USE},	/* 8 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, EXTERNAL_IRQ_NOT_IN_USE},	/* 9 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, 3},	/* 10 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, 2},	/* 11 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, EXTERNAL_IRQ_NOT_IN_USE},	/* 12 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, 1},	/* 13 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, EXTERNAL_IRQ_NOT_IN_USE},	/* 14 */
	{EXTERNAL_IRQ_NOT_ASSIGNED, EXTERNAL_IRQ_NOT_IN_USE}	/* 15 */
};

static void set_od_data(int overdrive_irq, int irq)
{
	if (irq >= NUM_EXTERNAL_IRQS || irq < 0)
		return;
	od_data_table[irq].overdrive_irq = overdrive_irq << 2;
}

static void enable_od_irq(unsigned int irq);
void disable_od_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_od_irq disable_od_irq

static void mask_and_ack_od(unsigned int);
static void end_od_irq(unsigned int irq);

static unsigned int startup_od_irq(unsigned int irq)
{
	enable_od_irq(irq);
	return 0;		/* never anything pending */
}

static struct hw_interrupt_type od_irq_type = {
	.typename = "Overdrive-IRQ",
	.startup = startup_od_irq,
	.shutdown = shutdown_od_irq,
	.enable = enable_od_irq,
	.disable = disable_od_irq,
	.ack = mask_and_ack_od,
	.end = end_od_irq
};

static void disable_od_irq(unsigned int irq)
{
	unsigned val, flags;
	int overdrive_irq;
	unsigned mask;

	/* Not a valid interrupt */
	if (irq < 0 || irq >= NUM_EXTERNAL_IRQS)
		return;

        /* Is is necessary to use a cli here? Would a spinlock not be 
         * mroe efficient?
         */
	local_irq_save(flags);
	overdrive_irq = od_data_table[irq].overdrive_irq;
	if (overdrive_irq != EXTERNAL_IRQ_NOT_ASSIGNED) {
		mask = ~(0x7 << overdrive_irq);
		val = ctrl_inl(OVERDRIVE_INT_CT);
		val &= mask;
		ctrl_outl(val, OVERDRIVE_INT_CT);
	}
	local_irq_restore(flags);
}

static void enable_od_irq(unsigned int irq)
{
	unsigned val, flags;
	int overdrive_irq;
	unsigned mask;

	/* Not a valid interrupt */
	if (irq < 0 || irq >= NUM_EXTERNAL_IRQS)
		return;

	/* Set priority in OD back to original value */
	local_irq_save(flags);
	/* This one is not in use currently */
	overdrive_irq = od_data_table[irq].overdrive_irq;
	if (overdrive_irq != EXTERNAL_IRQ_NOT_ASSIGNED) {
		val = ctrl_inl(OVERDRIVE_INT_CT);
		mask = ~(0x7 << overdrive_irq);
		val &= mask;
		mask = od_data_table[irq].irq_mask << overdrive_irq;
		val |= mask;
		ctrl_outl(val, OVERDRIVE_INT_CT);
	}
	local_irq_restore(flags);
}



/* this functions sets the desired irq handler to be an overdrive type */
static void __init make_od_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &od_irq_type;
	disable_od_irq(irq);
}


static void mask_and_ack_od(unsigned int irq)
{
	disable_od_irq(irq);
}

static void end_od_irq(unsigned int irq)
{
	enable_od_irq(irq);
}

void __init init_overdrive_irq(void)
{
	int i;

	/* Disable all interrupts */
	ctrl_outl(0, OVERDRIVE_INT_CT);

	/* Update interrupt pin mode to use encoded interrupts */
	i = ctrl_inw(INTC_ICR);
	i &= ~INTC_ICR_IRLM;
	ctrl_outw(i, INTC_ICR);

	for (i = 0; i < NUM_EXTERNAL_IRQS; i++) {
		if (od_data_table[i].irq_mask != EXTERNAL_IRQ_NOT_IN_USE) {
			make_od_irq(i);
		} else if (i != 15) {	// Cannot use imask on level 15
			make_imask_irq(i);
		}
	}

	/* Set up the interrupts */
	set_od_data(OVERDRIVE_PCI_INTA, OVERDRIVE_PCI_IRQ1);
	set_od_data(OVERDRIVE_PCI_INTB, OVERDRIVE_PCI_IRQ2);
	set_od_data(OVERDRIVE_AUDIO_INT, OVERDRIVE_ESS_IRQ);
}
