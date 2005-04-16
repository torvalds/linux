/*
 * linux/arch/sh/kernel/irq_intc2.c
 *
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * Interrupt handling for INTC2-based IRQ.
 *
 * These are the "new Hitachi style" interrupts, as present on the 
 * Hitachi 7751 and the STM ST40 STB1.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/machvec.h>


struct intc2_data {
	unsigned char msk_offset;
	unsigned char msk_shift;
#ifdef CONFIG_CPU_SUBTYPE_ST40
	int (*clear_irq) (int);
#endif
};


static struct intc2_data intc2_data[NR_INTC2_IRQS];

static void enable_intc2_irq(unsigned int irq);
static void disable_intc2_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_intc2_irq disable_intc2_irq

static void mask_and_ack_intc2(unsigned int);
static void end_intc2_irq(unsigned int irq);

static unsigned int startup_intc2_irq(unsigned int irq)
{ 
	enable_intc2_irq(irq);
	return 0; /* never anything pending */
}

static struct hw_interrupt_type intc2_irq_type = {
	"INTC2-IRQ",
	startup_intc2_irq,
	shutdown_intc2_irq,
	enable_intc2_irq,
	disable_intc2_irq,
	mask_and_ack_intc2,
	end_intc2_irq
};

static void disable_intc2_irq(unsigned int irq)
{
	int irq_offset = irq - INTC2_FIRST_IRQ;
	int msk_shift, msk_offset;

	// Sanity check
	if((irq_offset<0) || (irq_offset>=NR_INTC2_IRQS))
		return;

	msk_shift = intc2_data[irq_offset].msk_shift;
	msk_offset = intc2_data[irq_offset].msk_offset;

	ctrl_outl(1<<msk_shift,
		  INTC2_BASE+INTC2_INTMSK_OFFSET+msk_offset);
}

static void enable_intc2_irq(unsigned int irq)
{
	int irq_offset = irq - INTC2_FIRST_IRQ;
	int msk_shift, msk_offset;

	/* Sanity check */
	if((irq_offset<0) || (irq_offset>=NR_INTC2_IRQS))
		return;

	msk_shift = intc2_data[irq_offset].msk_shift;
	msk_offset = intc2_data[irq_offset].msk_offset;

	ctrl_outl(1<<msk_shift,
		  INTC2_BASE+INTC2_INTMSKCLR_OFFSET+msk_offset);
}

static void mask_and_ack_intc2(unsigned int irq)
{
	disable_intc2_irq(irq);
}

static void end_intc2_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_intc2_irq(irq);

#ifdef CONFIG_CPU_SUBTYPE_ST40
	if (intc2_data[irq - INTC2_FIRST_IRQ].clear_irq)
		intc2_data[irq - INTC2_FIRST_IRQ].clear_irq (irq);
#endif
}

/*
 * Setup an INTC2 style interrupt.
 * NOTE: Unlike IPR interrupts, parameters are not shifted by this code,
 * allowing the use of the numbers straight out of the datasheet.
 * For example:
 *    PIO1 which is INTPRI00[19,16] and INTMSK00[13]
 * would be:               ^     ^             ^  ^
 *                         |     |             |  |
 *    make_intc2_irq(84,   0,   16,            0, 13);
 */
void make_intc2_irq(unsigned int irq,
		    unsigned int ipr_offset, unsigned int ipr_shift,
		    unsigned int msk_offset, unsigned int msk_shift,
		    unsigned int priority)
{
	int irq_offset = irq - INTC2_FIRST_IRQ;
	unsigned int flags;
	unsigned long ipr;

	if((irq_offset<0) || (irq_offset>=NR_INTC2_IRQS))
		return;

	disable_irq_nosync(irq);

	/* Fill the data we need */
	intc2_data[irq_offset].msk_offset = msk_offset;
	intc2_data[irq_offset].msk_shift  = msk_shift;
#ifdef CONFIG_CPU_SUBTYPE_ST40
	intc2_data[irq_offset].clear_irq = NULL;
#endif
		
	/* Set the priority level */
	local_irq_save(flags);

	ipr=ctrl_inl(INTC2_BASE+INTC2_INTPRI_OFFSET+ipr_offset);
	ipr&=~(0xf<<ipr_shift);
	ipr|=(priority)<<ipr_shift;
	ctrl_outl(ipr, INTC2_BASE+INTC2_INTPRI_OFFSET+ipr_offset);

	local_irq_restore(flags);

	irq_desc[irq].handler=&intc2_irq_type;

	disable_intc2_irq(irq);
}

#ifdef CONFIG_CPU_SUBTYPE_ST40

struct intc2_init {
	unsigned short irq;
	unsigned char ipr_offset, ipr_shift;
	unsigned char msk_offset, msk_shift;
};

static struct intc2_init intc2_init_data[]  __initdata = {
	{64,  0,  0, 0,  0},	/* PCI serr */
	{65,  0,  4, 0,  1},	/* PCI err */
	{66,  0,  4, 0,  2},	/* PCI ad */
	{67,  0,  4, 0,  3},	/* PCI pwd down */
	{72,  0,  8, 0,  5},	/* DMAC INT0 */
	{73,  0,  8, 0,  6},	/* DMAC INT1 */
	{74,  0,  8, 0,  7},	/* DMAC INT2 */
	{75,  0,  8, 0,  8},	/* DMAC INT3 */
	{76,  0,  8, 0,  9},	/* DMAC INT4 */
	{78,  0,  8, 0, 11},	/* DMAC ERR */
	{80,  0, 12, 0, 12},	/* PIO0 */
	{84,  0, 16, 0, 13},	/* PIO1 */
	{88,  0, 20, 0, 14},	/* PIO2 */
	{112, 4,  0, 4,  0},	/* Mailbox */
#ifdef CONFIG_CPU_SUBTYPE_ST40GX1
	{116, 4,  4, 4,  4},	/* SSC0 */
	{120, 4,  8, 4,  8},	/* IR Blaster */
	{124, 4, 12, 4, 12},	/* USB host */
	{128, 4, 16, 4, 16},	/* Video processor BLITTER */
	{132, 4, 20, 4, 20},	/* UART0 */
	{134, 4, 20, 4, 22},	/* UART2 */
	{136, 4, 24, 4, 24},	/* IO_PIO0 */
	{140, 4, 28, 4, 28},	/* EMPI */
	{144, 8,  0, 8,  0},	/* MAFE */
	{148, 8,  4, 8,  4},	/* PWM */
	{152, 8,  8, 8,  8},	/* SSC1 */
	{156, 8, 12, 8, 12},	/* IO_PIO1 */
	{160, 8, 16, 8, 16},	/* USB target */
	{164, 8, 20, 8, 20},	/* UART1 */
	{168, 8, 24, 8, 24},	/* Teletext */
	{172, 8, 28, 8, 28},	/* VideoSync VTG */
	{173, 8, 28, 8, 29},	/* VideoSync DVP0 */
	{174, 8, 28, 8, 30},	/* VideoSync DVP1 */
#endif
};

void __init init_IRQ_intc2(void)
{
	struct intc2_init *p;

	printk(KERN_ALERT "init_IRQ_intc2\n");

	for (p = intc2_init_data;
	     p<intc2_init_data+ARRAY_SIZE(intc2_init_data);
	     p++) {
		make_intc2_irq(p->irq, p->ipr_offset, p->ipr_shift,
			       p-> msk_offset, p->msk_shift, 13);
	}
}

/* Adds a termination callback to the interrupt */
void intc2_add_clear_irq(int irq, int (*fn)(int))
{
	if (irq < INTC2_FIRST_IRQ)
		return;

	intc2_data[irq - INTC2_FIRST_IRQ].clear_irq = fn;
}

#endif /* CONFIG_CPU_SUBTYPE_ST40 */
